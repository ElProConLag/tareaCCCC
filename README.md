# Chat IPC con FIFO y Pipes

## 1. Compilación y ejecución

Requisitos: entorno tipo Unix (Linux), `gcc`, soporte para `mkfifo(3)` y señales POSIX.

Compilar todo (servidor y cliente):
```
make
```
Ejecución típica (en terminales separadas):
```
./servidor          # Terminal 1: inicia el proceso central
./cliente           # Terminal 2: primer participante
./cliente           # Terminal 3: segundo participante, etc.
```
Al iniciar, cada cliente muestra su PID y los comandos disponibles:
```
Cliente 12345 listo. Comandos: /reportar <pid> (o /report), /fork, /quit
```

### Flujo mínimo de prueba
1. Inicie el servidor.
2. Inicie 1–2 clientes y escriba texto libre para difundir mensajes.
3. Use `/fork` en un cliente para crear otro participante automáticamente registrado.
4. Reporte reiteradamente a un PID con `/reportar <pid>` hasta que el sistema lo elimine.
5. Salga con `/quit` o Ctrl+D.

## 2. Funcionamiento General (Resumen)

### 2.1 Proceso central (servidor del chat)
El servidor:
- Mantiene un arreglo de clientes activos (máx. `MAX_CLIENTES`).
- Acepta registros leyendo líneas `REGISTER ...` desde la FIFO global `/tmp/chat_registro` (no bloqueante). Cada registro incluye PID y rutas de dos FIFOs específicas del cliente (c2s y s2c).
- Abre para cada cliente: FIFO cliente→servidor (lectura no bloqueante) y servidor→cliente (escritura). Usa `select()` para multiplexar: registro, mensajes de clientes y canal de retorno del subsistema de reportes.
- Difunde mensajes de chat formateados como `CHAT from=<pid> text=...` a todos los clientes activos.
- Opera un proceso hijo dedicado a la lógica de reportes (ver sección 2.4) comunicado mediante dos pipes unidireccionales.

### 2.2 Procesos participantes (clientes del chat)
Cada cliente:
- Genera sus dos FIFOs únicas: `/tmp/c2s_<PID>` y `/tmp/s2c_<PID>`.
- Escribe una línea de registro `REGISTER pid=<pid> c2s=<ruta> s2c=<ruta>` en la FIFO global para que el servidor abra los extremos correspondientes.
- Tras registrarse, abre su FIFO de envío (c2s) en escritura no bloqueante y la de recepción (s2c) en lectura no bloqueante con reintentos (esperas de 100 ms).
- Ejecuta un bucle con `select()` sobre STDIN y FIFO s2c:
  - Línea libre → `MSG pid=<pid> text=<texto>` al servidor.
  - Comandos: `/reportar <pid>` o `/report <pid>` para denunciar, `/fork` para duplicarse, `/quit` (o EOF) para salir limpiamente enviando `QUIT`.
- Imprime cualquier salida recibida del servidor (incluye mensajes de chat y avisos de eliminación de procesos reportados).
- Al salir cierra descriptores y elimina sus FIFOs.

### 2.3 Capacidad de duplicación de procesos participantes
- El comando `/fork` ejecuta `fork()` en el cliente actual.
- El hijo cierra descriptores heredados, borra rutas previas y repite la secuencia de registro con su nuevo PID, creando sus propias FIFOs.
- Así se pueden multiplicar clientes rápidamente sin lanzar manualmente nuevos binarios.
- La duplicación sólo aplica a clientes; el servidor permanece único (proceso central).

### 2.4 Sistema de reportes
- Cuando un cliente genera un reporte (`/reportar <pid>`), se envía internamente al servidor una línea tipo `reportar <pid>` que éste traduce a `REPORT pid=<pid>` y escribe por el pipe hacia el proceso hijo de reportes.
- El proceso de reportes mantiene contadores por PID denunciado. Al superar 10 envía `kill(SIGTERM)` al PID objetivo y notifica al servidor con una línea `KILLED pid=<pid>`.
- El servidor al recibir `KILLED`:
  1. Desactiva al cliente (cierra sus FIFOs).
  2. Difunde por chat `CHAT from=0 text=KILLED <pid>` (indicando acción del sistema).
- El umbral >10 es configurable cambiando la condición `if (cnt[k] > 10)` en `servidor.c`.

## 3. Formato de Mensajes Internos (Referencia Breve)
- Registro: `REGISTER pid=<pid> c2s=<ruta> s2c=<ruta>` (cliente→servidor por `/tmp/chat_registro`).
- Mensaje de chat (interno): `MSG pid=<pid> text=<contenido>` (cliente→servidor). El servidor lo transforma a `CHAT from=<pid> text=<contenido>` (servidor→todos los clientes).
- Reporte (cliente→servidor): `reportar <pid>`. Servidor → proceso reportes: `REPORT pid=<pid>`.
- Eliminación (reportes→servidor): `KILLED pid=<pid>`; difusión: `CHAT from=0 text=KILLED <pid>`.
- Salida de un cliente: `QUIT pid=<pid>`.

## 4. Notas y Limitaciones
- No hay autenticación ni verificación de existencia de PID externos.
- La comunicación asume un entorno confiable; no se robusteció contra flooding masivo o corrupción deliberada de líneas.
- El log crece indefinidamente.
- Los FIFOs en `/tmp` podrían quedar huérfanos tras un fallo.

## 5. Ejemplo de Sesión (ilustrativo)
Servidor (T1):
```
$ ./servidor
```
Cliente A (T2):
```
$ ./cliente
Cliente 12001 listo. Comandos: /reportar <pid> (o /report), /fork, /quit
Hola a todos
/fork
```
Cliente B (T3) generado por fork:
```
[hijo 12037] conectado.
```
Cliente C (T4 manual):
```
$ ./cliente
Cliente 12055 listo. Comandos: ...
/reportar 12037
```
Tras suficientes `/reportar 12037`, aparece en todos:
```
CHAT from=0 text=KILLED 12037
```
