#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/select.h>

#include "common.h"

static int fd_in = -1;   // leer desde el servidor (sc)
static int fd_out = -1;  // escribir hacia el servidor (cs)
static pid_t self_pid;
static char path_cs[PATH_MAX_LEN];
static char path_sc[PATH_MAX_LEN];

static void cleanup(void) {
    if (fd_in >= 0) close(fd_in);
    if (fd_out >= 0) close(fd_out);
    unlink(path_cs);
    unlink(path_sc);
}

static void handle_exit(int sig) {
    (void)sig;
    cleanup();
    _exit(0);
}

static void register_with_server(void) {
    int rfd = -1;
    int warned = 0;
    // Esperar hasta que el servidor cree y abra el FIFO de registro
    for (int i = 0; i < 500; ++i) { // ~10s con 20ms
        rfd = open(REGISTER_FIFO, O_WRONLY | O_NONBLOCK);
        if (rfd >= 0) break;
        if (!warned) {
            fprintf(stderr, "Esperando al servidor (FIFO de registro no disponible)...\n");
            warned = 1;
        }
        if (errno == ENOENT || errno == ENXIO) {
            usleep(20000);
            continue;
        }
        // Otros errores: esperar y reintentar igualmente
        usleep(20000);
    }
    if (rfd < 0) {
        fprintf(stderr, "No se pudo conectar con el servidor: %s\n", strerror(errno));
        exit(1);
    }
    char msg[64];
    int n = snprintf(msg, sizeof msg, "%d\n", (int)self_pid);
    if (write(rfd, msg, (size_t)n) < 0) {
        perror("write register");
        close(rfd);
        exit(1);
    }
    close(rfd);
}

static void setup_fifos_read_first(void) {
    make_fifo_cs_path(self_pid, path_cs, sizeof path_cs);
    make_fifo_sc_path(self_pid, path_sc, sizeof path_sc);
    unlink(path_cs);
    unlink(path_sc);
    if (mkfifo(path_cs, 0666) < 0 && errno != EEXIST) {
        perror("mkfifo cs");
        exit(1);
    }
    if (mkfifo(path_sc, 0666) < 0 && errno != EEXIST) {
        perror("mkfifo sc");
        exit(1);
    }
    // Abrir primero el extremo de lectura servidor->cliente para que el servidor pueda adjuntar el escritor después
    fd_in = open(path_sc, O_RDONLY | O_NONBLOCK);
    if (fd_in < 0) {
        perror("open sc (server->client)");
        exit(1);
    }
}

static void open_cs_write_with_retry(void) {
    // Intentar repetidamente en modo no bloqueante hasta que el servidor abra su extremo de lectura
    for (int i = 0; i < 100; ++i) { // ~1s total
        fd_out = open(path_cs, O_WRONLY | O_NONBLOCK);
        if (fd_out >= 0) return;
        if (errno != ENXIO && errno != ENOENT) {
            perror("open cs (client->server)");
        }
        usleep(10000); // 10ms
    }
    // Como último recurso, intentar abrir en modo bloqueante
    fd_out = open(path_cs, O_WRONLY);
    if (fd_out < 0) {
        perror("open cs (client->server) final");
        exit(1);
    }
}

// Uso/documentación disponible en README; no se mantiene la función 'usage' para evitar advertencias del compilador.

static void clone_self(void) {
    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        return;
    }
    if (child == 0) {
    // En el hijo, ejecutar el mismo programa para tener un conjunto de FIFOs nuevo
    // No se necesitan argumentos
        execlp("/proc/self/exe", "/proc/self/exe", (char*)NULL);
    // Alternativa: intentar con argv[0] mediante readlink
        char path[PATH_MAX_LEN];
        ssize_t r = readlink("/proc/self/exe", path, sizeof path - 1);
        if (r > 0) { path[r] = '\0'; execlp(path, path, (char*)NULL); }
        perror("execlp clone");
        _exit(1);
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    signal(SIGINT, handle_exit);
    signal(SIGTERM, handle_exit);
    atexit(cleanup);

    self_pid = getpid();
    setup_fifos_read_first();
    register_with_server();
    open_cs_write_with_retry();

    fprintf(stderr, "Cliente %d listo. Escribe mensajes y presiona Enter.\n", (int)self_pid);

    char inbuf[LINE_BUF];
    size_t inlen = 0;
    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
    FD_SET(0, &rfds); // entrada estándar (teclado)
    FD_SET(fd_in, &rfds); // mensajes desde el servidor
        int maxfd = fd_in > 0 ? fd_in : 0;
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int ready = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }
        if (ready == 0) {
            continue;
        }
        if (FD_ISSET(fd_in, &rfds)) {
            char buf[LINE_BUF];
            ssize_t r = read(fd_in, buf, sizeof buf - 1);
            if (r <= 0) {
                if (r == 0 || errno != EAGAIN) {
                    fprintf(stderr, "Desconectado del servidor.\n");
                    break;
                }
            } else {
                buf[r] = '\0';
                fputs(buf, stdout);
                fflush(stdout);
            }
        }
        if (FD_ISSET(0, &rfds)) {
            char ch;
            ssize_t r = read(0, &ch, 1);
            if (r <= 0) continue;
            if (ch == '\n') {
                inbuf[inlen] = '\0';
                if (strcmp(inbuf, "salir") == 0) {
                    // avisar al servidor y salir
                    write(fd_out, "salir\n", 6);
                    break;
                } else if (strcmp(inbuf, "clonar") == 0) {
                    clone_self();
                    // también enviar una nota al servidor
                    write(fd_out, "[nota] cliente solicito clonar\n", 30);
                } else {
                    size_t n = strlen(inbuf);
                    if (n > 0) {
                        write(fd_out, inbuf, n);
                        write(fd_out, "\n", 1);
                    }
                }
                inlen = 0;
            } else if (inlen + 1 < sizeof inbuf) {
                inbuf[inlen++] = ch;
            }
        }
    }

    return 0;
}
