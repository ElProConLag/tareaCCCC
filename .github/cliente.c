// cliente.c
#include "comun.h"

/*
 * Cliente mínimo:
 * - Crea sus FIFOs: /tmp/c2s_<pid> y /tmp/s2c_<pid>
 * - Se registra en /tmp/chat_registro
 * - Bucle con select(): lee teclado y lee s2c
 * - Comandos:
 *     texto libre          -> MSG
 *     /report <pid>        -> REPORT
 *     /quit                -> QUIT y salir
 *     /fork                -> duplica proceso y el hijo se registra aparte
 */

static int fd_c2s = -1;       // write -> servidor
static int fd_s2c = -1;       // read  <- servidor
static char ruta_c2s[PATH_MAX];
static char ruta_s2c[PATH_MAX];
static pid_t mi_pid = 0;

static void limpiar_y_salir(int code) {
    if (fd_c2s >= 0) close(fd_c2s);
    if (fd_s2c >= 0) close(fd_s2c);
    if (ruta_c2s[0]) unlink(ruta_c2s);
    if (ruta_s2c[0]) unlink(ruta_s2c);
    exit(code);
}

static void registrar_en_servidor(void) {
    // abrir FIFO de registro para escribir una sola línea
    int fd_reg = open(FIFO_REGISTRO, O_WRONLY);
    if (fd_reg < 0) {
        perror("open registro");
        limpiar_y_salir(1);
    }
    char linea[MAX_LINEA];
    snprintf(linea, sizeof(linea),
             "REGISTER pid=%d c2s=%s s2c=%s\n",
             (int)mi_pid, ruta_c2s, ruta_s2c);
    write(fd_reg, linea, strlen(linea));
    close(fd_reg);
}

static void preparar_fifos_y_conectar(void) {
    // construir rutas
    snprintf(ruta_c2s, sizeof(ruta_c2s), RUTA_C2S, (int)mi_pid);
    snprintf(ruta_s2c, sizeof(ruta_s2c), RUTA_S2C, (int)mi_pid);

    // crear FIFOs (ignorar si ya existen)
    if (mkfifo(ruta_c2s, 0666) < 0 && errno != EEXIST) { perror("mkfifo c2s"); exit(1); }
    if (mkfifo(ruta_s2c, 0666) < 0 && errno != EEXIST) { perror("mkfifo s2c"); exit(1); }

    // 1) REGISTRARSE PRIMERO (servidor sabrá que debe abrir estos FIFOs)
    registrar_en_servidor();

    // 2) Abrir c2s en escritura **no bloqueante** con reintentos
    int intentos = 50; // ~5s en total
    while (intentos--) {
        fd_c2s = open(ruta_c2s, O_WRONLY | O_NONBLOCK);
        if (fd_c2s >= 0) break;
        if (errno != ENXIO && errno != ENOENT) { perror("open c2s"); }
        usleep(100 * 1000); // 100 ms
    }
    if (fd_c2s < 0) { fprintf(stderr, "No pude abrir %s para escribir\n", ruta_c2s); limpiar_y_salir(1); }

    // 3) Abrir s2c en lectura **no bloqueante** con reintentos
    intentos = 50;
    while (intentos--) {
        fd_s2c = open(ruta_s2c, O_RDONLY | O_NONBLOCK);
        if (fd_s2c >= 0) break;
        if (errno != ENXIO && errno != ENOENT) { perror("open s2c"); }
        usleep(100 * 1000);
    }
    if (fd_s2c < 0) { fprintf(stderr, "No pude abrir %s para leer\n", ruta_s2c); limpiar_y_salir(1); }
}


static void enviar_msg_texto(const char *texto) {
    char linea[MAX_LINEA];
    // recorta salto de línea final si viene
    size_t n = strlen(texto);
    while (n > 0 && (texto[n-1] == '\n' || texto[n-1] == '\r')) n--;
    snprintf(linea, sizeof(linea), "MSG pid=%d text=%.*s\n", (int)mi_pid, (int)n, texto);
    write(fd_c2s, linea, strlen(linea));
}

static void enviar_report(int objetivo) {
    char linea[MAX_LINEA];
    snprintf(linea, sizeof(linea), "reportar %d\n", objetivo);
    write(fd_c2s, linea, strlen(linea));
}



static void enviar_quit(void) {
    char linea[MAX_LINEA];
    snprintf(linea, sizeof(linea), "QUIT pid=%d\n", (int)mi_pid);
    write(fd_c2s, linea, strlen(linea));
}

static void bucle(void);

static void manejar_fork(void) {
    pid_t h = fork();
    if (h < 0) {
        perror("fork");
        return;
    }
    if (h == 0) {
        // hijo: nuevo participante independiente
        // cerrar FDs heredados del padre para no interferir
        if (fd_c2s >= 0) close(fd_c2s);
        if (fd_s2c >= 0) close(fd_s2c);
        fd_c2s = fd_s2c = -1;
        ruta_c2s[0] = ruta_s2c[0] = '\0';

        mi_pid = getpid();
        preparar_fifos_y_conectar();
        printf("[hijo %d] conectado.\n", (int)mi_pid);
        bucle();  // el hijo entra a su propio bucle
        // no vuelve
        exit(0);
    } else {
        // padre continúa normal
        printf("[padre %d] duplicó a %d\n", (int)mi_pid, (int)h);
    }
}

static void bucle(void) {
    char buf_in[MAX_LINEA];

    while (1) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(0, &rfds);             // stdin
        FD_SET(fd_s2c, &rfds);        // servidor -> cliente

        int maxfd = fd_s2c;
        int r = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (r < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        // Entrada del servidor
        if (FD_ISSET(fd_s2c, &rfds)) {
            // leer y mostrar en stdout
            char buf[256];
            ssize_t n = read(fd_s2c, buf, sizeof(buf));
            if (n > 0) {
                write(1, buf, n); // imprimir tal cual
            } else if (n == 0) {
                // servidor cerró
                printf("Servidor cerró conexión.\n");
                break;
            }
        }

        // Teclado
            if (FD_ISSET(0, &rfds)) {
                if (!fgets(buf_in, sizeof(buf_in), stdin)) {
                    // EOF de stdin
                    enviar_quit();
                    break;
                }
                // comandos simples
                if (strncmp(buf_in, "/quit", 5) == 0) {
                    enviar_quit();
                    break;

                } else if (strncmp(buf_in, "/reportar ", 10) == 0 || strncmp(buf_in, "/report ", 8) == 0) {
                    const char *p = (strncmp(buf_in, "/reportar ", 10) == 0) ? buf_in + 10 : buf_in + 8;
                    int objetivo = atoi(p);
                    if (objetivo > 0) {
                        enviar_report(objetivo);   // debe enviar "reportar <pid>\n"
                    } else {
                        printf("Uso: /reportar <pid>\n");
                    }

                } else if (strncmp(buf_in, "/fork", 5) == 0) {
                    manejar_fork();

                } else {
                    enviar_msg_texto(buf_in);
                }
            }

    }

    limpiar_y_salir(0);
}

int main(void) {
    mi_pid = getpid();
    printf("Cliente %d iniciando...\n", (int)mi_pid); fflush(stdout);

    preparar_fifos_y_conectar();

    printf("Cliente %d listo. Comandos: /report <pid>, /fork, /quit\n", (int)mi_pid);
    fflush(stdout);

    bucle();
    return 0;
}
