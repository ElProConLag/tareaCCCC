// comun.h
#ifndef COMUN_H
#define COMUN_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/select.h>
#include <limits.h>
#include <time.h>

// --- Compat: algunos entornos no traen PATH_MAX en limits.h
#ifndef PATH_MAX
#define PATH_MAX 256
#endif

// Rutas y tamaños
#define FIFO_REGISTRO "/tmp/chat_registro"
#define RUTA_C2S "/tmp/c2s_%d"
#define RUTA_S2C "/tmp/s2c_%d"

#define MAX_CLIENTES 50
#define MAX_LINEA    512

// Utilidad
static inline void error_fatal(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

#endif // COMUN_H
