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
#include <sys/wait.h>

#include "common.h"

typedef struct Client {
    pid_t pid;
    int fd_in;   // cs: extremo de lectura del pipe cliente->servidor (el servidor lee)
    int fd_out;  // sc: extremo de escritura del pipe servidor->cliente (el servidor escribe)
    struct Client *next;
} Client;

static Client *clients = NULL;
static int reg_fd = -1;
static int report_pipe_parent[2] = {-1,-1}; // el padre escribe, el hijo lee

static void cleanup_fifo(const char *path) {
    unlink(path);
}

static void add_client(pid_t pid, int fd_in, int fd_out) {
    Client *c = (Client*)calloc(1, sizeof(Client));
    c->pid = pid;
    c->fd_in = fd_in;
    c->fd_out = fd_out;
    c->next = clients;
    clients = c;
}

static void remove_client(pid_t pid) {
    Client **pp = &clients;
    while (*pp) {
        if ((*pp)->pid == pid) {
            Client *dead = *pp;
            *pp = dead->next;
            if (dead->fd_in >= 0) close(dead->fd_in);
            if (dead->fd_out >= 0) close(dead->fd_out);
            char p1[PATH_MAX_LEN], p2[PATH_MAX_LEN];
            make_fifo_cs_path(pid, p1, sizeof p1);
            make_fifo_sc_path(pid, p2, sizeof p2);
            cleanup_fifo(p1);
            cleanup_fifo(p2);
            free(dead);
            return;
        }
        pp = &((*pp)->next);
    }
}

static Client* find_client(pid_t pid) {
    for (Client *c = clients; c; c = c->next) if (c->pid == pid) return c;
    return NULL;
}

static void broadcast(const char *msg, size_t len, pid_t from_pid) {
    char prefix[64];
    int pref = snprintf(prefix, sizeof prefix, "%d: ", (int)from_pid);
    for (Client *c = clients; c; c = c->next) {
        if (c->fd_out >= 0) {
            if (from_pid > 0) {
                // anteponer el prefijo con el PID
                if (write(c->fd_out, prefix, pref) < 0) {}
            }
            if (write(c->fd_out, msg, len) < 0) {}
            if (msg[len-1] != '\n') write(c->fd_out, "\n", 1);
        }
    }
}

static void send_line(Client *c, const char *line) {
    size_t n = strlen(line);
    if (n && line[n-1] != '\n') {
        if (write(c->fd_out, line, n) >= 0) write(c->fd_out, "\n", 1);
    } else {
        write(c->fd_out, line, n);
    }
}

static void setup_registration_fifo() {
    // Crear FIFO de registro
    unlink(REGISTER_FIFO);
    if (mkfifo(REGISTER_FIFO, 0666) < 0 && errno != EEXIST) {
        perror("mkfifo register");
        exit(1);
    }
    // Abrir en RDWR para evitar EOF cuando no hay escritores conectados
    reg_fd = open(REGISTER_FIFO, O_RDWR | O_NONBLOCK);
    if (reg_fd < 0) {
        perror("open register fifo");
        exit(1);
    }
}

static void spawn_report_manager() {
    if (pipe(report_pipe_parent) < 0) {
        perror("pipe report");
        exit(1);
    }
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork report");
        exit(1);
    }
    if (pid == 0) {
        // Proceso hijo: gestor de reportes
        close(report_pipe_parent[1]);
        // Mapa simple de contadores en memoria
        struct Entry { pid_t pid; int count; };
        struct Entry arr[256];
        int used = 0;
        char buf[64];
        for (;;) {
            ssize_t r = read(report_pipe_parent[0], buf, sizeof buf - 1);
            if (r <= 0) {
                if (errno == EINTR) continue;
                usleep(10000);
                continue;
            }
            buf[r] = '\0';
            // Se espera un PID en ASCII
            pid_t target = (pid_t)atoi(buf);
            int i;
            for (i = 0; i < used; ++i) if (arr[i].pid == target) break;
            if (i == used && used < (int)(sizeof arr/sizeof arr[0])) {
                arr[used].pid = target; arr[used].count = 0; used++;
            }
            if (i < used) {
                arr[i].count++;
                if (arr[i].count > 10) {
                    kill(target, SIGKILL);
                }
            }
        }
        _exit(0);
    } else {
        // Proceso padre: mantiene el extremo de escritura
        close(report_pipe_parent[0]);
        // Establecer CLOEXEC en el extremo de escritura
        int flags = fcntl(report_pipe_parent[1], F_GETFD);
        fcntl(report_pipe_parent[1], F_SETFD, flags | FD_CLOEXEC);
    }
}

static void handle_new_registration() {
    // Protocolo de registro: el cliente escribe su PID como una línea en REGISTER_FIFO
    char line[64];
    ssize_t r = read(reg_fd, line, sizeof line - 1);
    if (r <= 0) return;
    line[r] = '\0';
    // Manejar múltiples PIDs que puedan venir en el buffer
    char *saveptr;
    char *tok = strtok_r(line, "\n", &saveptr);
    while (tok) {
        pid_t pid = (pid_t)atoi(tok);
        if (pid > 0 && !find_client(pid)) {
            char p_cs[PATH_MAX_LEN], p_sc[PATH_MAX_LEN];
            make_fifo_cs_path(pid, p_cs, sizeof p_cs);
            make_fifo_sc_path(pid, p_sc, sizeof p_sc);
            // Abrir cliente->servidor (el servidor lee)
            int fd_in = open(p_cs, O_RDONLY | O_NONBLOCK);
            if (fd_in < 0) {
                tok = strtok_r(NULL, "\n", &saveptr);
                continue;
            }
            // Abrir servidor->cliente (el servidor escribe). Reintentar brevemente si el lector no está listo.
            int fd_out = -1;
            for (int i = 0; i < 100; ++i) { // ~1s
                fd_out = open(p_sc, O_WRONLY | O_NONBLOCK);
                if (fd_out >= 0) break;
                if (errno != ENXIO && errno != ENOENT) break;
                usleep(10000);
            }
            if (fd_out < 0) { close(fd_in); tok = strtok_r(NULL, "\n", &saveptr); continue; }
            add_client(pid, fd_in, fd_out);
            // Mensaje de bienvenida
            const char *welcome = "Bienvenido al chat. Comandos: 'reportar <pid>' para reportar. 'salir' para desconectar.";
            write(fd_out, welcome, strlen(welcome));
            write(fd_out, "\n", 1);
            char joined[128];
            int n = snprintf(joined, sizeof joined, "[Servidor] %d se unió al chat\n", (int)pid);
            broadcast(joined, (size_t)n, 0);
        }
        tok = strtok_r(NULL, "\n", &saveptr);
    }
}

static void handle_client_input(Client *c) {
    char buf[LINE_BUF];
    ssize_t r = read(c->fd_in, buf, sizeof buf - 1);
    if (r <= 0) {
        if (r == 0 || errno != EAGAIN) {
            // client closed
            char msg[128];
            int n = snprintf(msg, sizeof msg, "[Servidor] %d salió del chat\n", (int)c->pid);
            broadcast(msg, (size_t)n, 0);
            remove_client(c->pid);
        }
        return;
    }
    buf[r] = '\0';
    // Procesar líneas individualmente
    char *saveptr;
    char *line = strtok_r(buf, "\n", &saveptr);
    while (line) {
        // recortar espacios iniciales
        while (*line == ' ' || *line == '\t') line++;
        if (strcmp(line, "salir") == 0) {
            char msg[128];
            int n = snprintf(msg, sizeof msg, "[Servidor] %d se desconectó\n", (int)c->pid);
            broadcast(msg, (size_t)n, 0);
            remove_client(c->pid);
            return; // c ya fue liberado
        }
        if (strncmp(line, "reportar ", 9) == 0) {
            pid_t target = (pid_t)atoi(line + 9);
            if (report_pipe_parent[1] >= 0 && target > 0) {
                char tmp[32];
                int n = snprintf(tmp, sizeof tmp, "%d\n", (int)target);
                write(report_pipe_parent[1], tmp, (size_t)n);
                char msg[128];
                snprintf(msg, sizeof msg, "[Servidor] reporte recibido contra %d\n", (int)target);
                send_line(c, msg);
            }
        } else if (strncmp(line, "clonar", 6) == 0) {
            // El servidor solo aclara; la clonación es una acción del lado del cliente.
            send_line(c, "[Servidor] Usa 'clonar' en el cliente para duplicarlo.");
        } else {
            size_t n = strlen(line);
            if (n > 0) broadcast(line, n, c->pid);
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
}

static void handle_sigint(int sig) {
    (void)sig;
    // limpieza
    for (Client *c = clients; c;) { Client *next = c->next; remove_client(c->pid); c = next; }
    if (reg_fd >= 0) close(reg_fd);
    cleanup_fifo(REGISTER_FIFO);
    if (report_pipe_parent[1] >= 0) close(report_pipe_parent[1]);
    // Recolectar procesos hijos (reap)
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
    fprintf(stderr, "Servidor finalizado.\n");
    exit(0);
}

int main(void) {
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);
    signal(SIGPIPE, SIG_IGN);

    setup_registration_fifo();
    spawn_report_manager();

    fprintf(stderr, "Servidor listo. FIFO de registro: %s\n", REGISTER_FIFO);

    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        int maxfd = reg_fd;
        FD_SET(reg_fd, &rfds);
        for (Client *c = clients; c; c = c->next) {
            FD_SET(c->fd_in, &rfds);
            if (c->fd_in > maxfd) maxfd = c->fd_in;
        }
        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int ready = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }
        if (ready == 0) {
            // tiempo de espera agotado: continuar el bucle
        } else {
            if (FD_ISSET(reg_fd, &rfds)) handle_new_registration();
            // Iterar con cuidado ya que handle_client_input puede eliminar el nodo actual
            Client *c = clients;
            while (c) {
                Client *next = c->next; // capturar el siguiente
                if (FD_ISSET(c->fd_in, &rfds)) {
                    handle_client_input(c);
                }
                c = next;
            }
        }
    }

    handle_sigint(0);
    return 0;
}
