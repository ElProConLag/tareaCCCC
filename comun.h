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

// Constantes que se usan
#define FIFO_REGISTRO "/tmp/chat_registro"
#define RUTA_C2S "/tmp/c2s_%d"
#define RUTA_S2C "/tmp/s2c_%d"
#define MAX_CLIENTES 50
#define MAX_LINEA 512

// Estructura del cliente que esat en el servidor
typedef struct {
    pid_t pid;

    // pipe del cliente hacia el servidor
    int fd_c2s;    
    // pipe del servidor hacia el cliente              
    int fd_s2c;                  
    char ruta_c2s[PATH_MAX];
    char ruta_s2c[PATH_MAX];
    int activo;
} Cliente;

// Funciones de ayuda, para llammar
static inline void error_fatal(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

#endif
