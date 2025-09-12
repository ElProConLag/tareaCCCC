#include "comun.h"

#define MAX_ENTRADAS_REPORTES 256  // evita numero maagico 256

typedef struct {
    pid_t pid;
    // lectura desde cliente
    int fd_c2s;  
    // escritura hacia cliente
    int fd_s2c;               
    char ruta_c2s[PATH_MAX];
    char ruta_s2c[PATH_MAX];
    int activo;
} RegCliente;

static RegCliente clientes[MAX_CLIENTES];
static int n_clientes = 0;

// FIFO de registro, la lectura
static int fd_reg = -1;       
static FILE *logf = NULL;

// pipes con proceso de reportes
static int p_srv_rep[2] = {-1,-1};   // servidor escribe REPORT
static int p_rep_srv[2] = {-1,-1};   // reportes escribe KILLED
static pid_t pid_rep = -1;

// Utilidades minimas
static int idx_por_pid(pid_t pid) {
    for (int i = 0; i < n_clientes; ++i)
        if (clientes[i].activo && clientes[i].pid == pid) return i;
    return -1;
}

static void difundir(const char *linea) {
    for (int i = 0; i < n_clientes; ++i) {
        if (!clientes[i].activo) continue;
        (void)write(clientes[i].fd_s2c, linea, strlen(linea));
    }
}

static void cerrar_cliente(int i) {
    if (i < 0 || i >= n_clientes) return;
    if (!clientes[i].activo) return;
    if (clientes[i].fd_c2s >= 0) close(clientes[i].fd_c2s);
    if (clientes[i].fd_s2c >= 0) close(clientes[i].fd_s2c);
    clientes[i].activo = 0;
}

/*Proceso secundario de reportes */
static void proceso_reportes(void) {
    close(p_srv_rep[1]);  // lee de [0]
    close(p_rep_srv[0]);  // escribe en [1]

    FILE *in = fdopen(p_srv_rep[0], "r");
    FILE *out = fdopen(p_rep_srv[1], "w");
    if (!in || !out) _exit(1);

    pid_t pids[MAX_ENTRADAS_REPORTES];
    int   cnt[MAX_ENTRADAS_REPORTES];
    int   m = 0;

    char linea[MAX_LINEA];

    while (fgets(linea, sizeof(linea), in)) {
        int objetivo = -1;
        if (sscanf(linea, "REPORT pid=%d", &objetivo) == 1 && objetivo > 0) {
            int k;
            for (k = 0; k < m; ++k) if (pids[k] == (pid_t)objetivo) break;
            if (k == m && m < MAX_ENTRADAS_REPORTES) { pids[m] = (pid_t)objetivo; cnt[m] = 0; m++; }
            if (k < m) {
                cnt[k]++;
                if (cnt[k] > 10) {
                    kill((pid_t)objetivo, SIGTERM);
                    fprintf(out, "KILLED pid=%d\n", objetivo);
                    fflush(out);
                }
            }
        }
    }
    fclose(in);
    fclose(out);
    _exit(0);
}

/*Registro de nuevos clientes */
static void abrir_fifo_registro(void) {
    // crea si no existe el cliente
    if (mkfifo(FIFO_REGISTRO, 0666) < 0 && errno != EEXIST) {
        error_fatal("mkfifo registro");
    }
    fd_reg = open(FIFO_REGISTRO, O_RDONLY | O_NONBLOCK);
    if (fd_reg < 0) error_fatal("open registro");
}

static void reabrir_fifo_registro_si_eof(FILE **preg) {
    if (*preg && feof(*preg)) {
        fclose(*preg);
        close(fd_reg);
        fd_reg = open(FIFO_REGISTRO, O_RDONLY | O_NONBLOCK);
        *preg = fdopen(fd_reg, "r");
        clearerr(*preg);
    }
}

static int abrir_pares(RegCliente *c) {
    c->fd_c2s = open(c->ruta_c2s, O_RDONLY | O_NONBLOCK);
    if (c->fd_c2s < 0) return -1;
    c->fd_s2c = open(c->ruta_s2c, O_WRONLY);
    if (c->fd_s2c < 0) { close(c->fd_c2s); return -1; }
    c->activo = 1;
    return 0;
}

static void procesar_register_linea(const char *linea) {
    // "REGISTER pid=%d c2s=%s s2c=%s"
    RegCliente c; memset(&c, 0, sizeof(c));
    c.fd_c2s = c.fd_s2c = -1;
    int pid;
    char r1[PATH_MAX], r2[PATH_MAX];
    if (sscanf(linea, "REGISTER pid=%d c2s=%s s2c=%s", &pid, r1, r2) == 3) {
        c.pid = (pid_t)pid;
        strncpy(c.ruta_c2s, r1, sizeof(c.ruta_c2s)-1);
        strncpy(c.ruta_s2c, r2, sizeof(c.ruta_s2c)-1);
        if (n_clientes < MAX_CLIENTES && abrir_pares(&c) == 0) {
            clientes[n_clientes++] = c;
            if (logf) fprintf(logf, "REG pid=%d\n", (int)c.pid);
        }
    }
}

/* Manejo de mensajes de los clientes */
static void manejar_linea_cliente(int i, const char *linea) {
    // MSG, reportar <pid>, REPORT pid=<pid>, QUIT
    pid_t pid = clientes[i].pid;

    if (strncmp(linea, "MSG ", 4) == 0) {
        // difundir tal cual pero con formato CHAT, solo en una sola linea
        const char *ptext = strstr(linea, "text=");
        if (!ptext) return;
        char out[MAX_LINEA];
        int len = snprintf(out, sizeof(out), "CHAT from=%d %s", (int)pid, ptext);
        if (len > 0 && out[len-1] != '\n' && len < (int)sizeof(out)-1) {
            out[len++] = '\n';
            out[len] = '\0';
        }
        difundir(out);
        if (logf) fprintf(logf, "%s", out);

    } else if (strncmp(linea, "REPORT ", 7) == 0) {
        int objetivo = -1;
        if (sscanf(linea, "REPORT pid=%d", &objetivo) == 1 && objetivo > 0) {
            dprintf(p_srv_rep[1], "REPORT pid=%d\n", objetivo);
        }

    } else if (strncmp(linea, "reportar", 8) == 0) {
        // acepta dos formatos: "reportar <pid>" y "reportar pid"
        int objetivo = 0;
        if (sscanf(linea, "reportar <%d>", &objetivo) != 1) {
            sscanf(linea, "reportar %d", &objetivo);
        }
        if (objetivo > 0) {
            dprintf(p_srv_rep[1], "REPORT pid=%d\n", objetivo);
        }

    } else if (strncmp(linea, "QUIT", 4) == 0) {
        if (logf) fprintf(logf, "QUIT pid=%d\n", (int)pid);
        cerrar_cliente(i);
    }
}

/*Manejo de mensajes desde proceso de reportes*/
static void manejar_linea_reportes(const char *linea) {
    int pid = -1;
    if (sscanf(linea, "KILLED pid=%d", &pid) == 1 && pid > 0) {
        int i = idx_por_pid((pid_t)pid);
        if (i >= 0) {
            cerrar_cliente(i);
        }
        // se avisa
        char out[64];
        snprintf(out, sizeof(out), "CHAT from=0 text=KILLED %d\n", pid);
        difundir(out);
        if (logf) fprintf(logf, "KILLED pid=%d\n", pid);
    }
}

int main(void) {
    // log
    logf = fopen("chat.log", "a");

    // fifo de registro
    abrir_fifo_registro();
    FILE *reg_in = fdopen(fd_reg, "r");
    if (!reg_in) error_fatal("fdopen registro");

    // pipes y fork del proceso de reportes
    if (pipe(p_srv_rep) < 0 || pipe(p_rep_srv) < 0) error_fatal("pipe");
    pid_rep = fork();
    if (pid_rep < 0) error_fatal("fork");
    if (pid_rep == 0) {
        // hijo: reportes
        close(p_srv_rep[1]);
        close(p_rep_srv[0]);
        proceso_reportes();
        return 0;
    }
    // servidor conserva: p_srv_rep[1] (escribe), p_rep_srv[0] (lee)
    close(p_srv_rep[0]);
    close(p_rep_srv[1]);
    FILE *rep_in = fdopen(p_rep_srv[0], "r");
    if (!rep_in) error_fatal("fdopen rep_in");

    char linea[MAX_LINEA];

    while (1) {
        fd_set rfds; FD_ZERO(&rfds);
        int maxfd = -1;

        // registro
        FD_SET(fd_reg, &rfds);
        if (fd_reg > maxfd) maxfd = fd_reg;

        // reportes -> servidor
        int fd_rep_r = fileno(rep_in);
        FD_SET(fd_rep_r, &rfds);
        if (fd_rep_r > maxfd) maxfd = fd_rep_r;

        // clientes
        for (int i = 0; i < n_clientes; ++i) {
            if (!clientes[i].activo) continue;
            FD_SET(clientes[i].fd_c2s, &rfds);
            if (clientes[i].fd_c2s > maxfd) maxfd = clientes[i].fd_c2s;
        }

        if (select(maxfd + 1, &rfds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) continue;
            error_fatal("select");
        }

        // registro: puede haber varias lineas
        if (FD_ISSET(fd_reg, &rfds)) {
            while (fgets(linea, sizeof(linea), reg_in)) {
                procesar_register_linea(linea);
            }
            // si EOF, se reabre para seguir aceptando futuros clientes
            reabrir_fifo_registro_si_eof(&reg_in);
        }

        // reportes
        if (FD_ISSET(fd_rep_r, &rfds)) {
            while (fgets(linea, sizeof(linea), rep_in)) {
                manejar_linea_reportes(linea);
            }
            clearerr(rep_in); // si no hay nada maas, limpiar error / EOF
        }

        // clientes
        for (int i = 0; i < n_clientes; ++i) {
            if (!clientes[i].activo) continue;
            int fd = clientes[i].fd_c2s;
            if (!FD_ISSET(fd, &rfds)) continue;

            // Se leen lneas disponibles, se esta utilizando FILE* temporal
            FILE *fin = fdopen(dup(fd), "r");
            if (!fin) continue;
            while (fgets(linea, sizeof(linea), fin)) {
                manejar_linea_cliente(i, linea);
                if (!clientes[i].activo) break;
            }
            fclose(fin);
        }
    }

    if (logf) fclose(logf);
    return 0;
}
