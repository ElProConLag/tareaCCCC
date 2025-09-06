#ifndef CHAT_COMMON_H
#define CHAT_COMMON_H

#include <sys/types.h>
#include <stdio.h>

#define REGISTER_FIFO "/tmp/chat_register.fifo"
#define FIFO_DIR "/tmp"

#define PATH_MAX_LEN 256
#define MSG_MAX_LEN 512
#define LINE_BUF 1024

// Utilidad para formatear las rutas de los FIFOs por cliente
// cliente -> servidor
static inline void make_fifo_cs_path(pid_t pid, char *out, size_t n) {
    // p. ej., /tmp/chat_<pid>_cs.fifo
    (void)snprintf(out, n, "%s/chat_%d_cs.fifo", FIFO_DIR, (int)pid);
}

// servidor -> cliente
static inline void make_fifo_sc_path(pid_t pid, char *out, size_t n) {
    // p. ej., /tmp/chat_<pid>_sc.fifo
    (void)snprintf(out, n, "%s/chat_%d_sc.fifo", FIFO_DIR, (int)pid);
}

#endif // CHAT_COMMON_H
