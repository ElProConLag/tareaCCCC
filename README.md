En esta tarea se pide que construya un chat comunitario en C/C++. La arquitectura de este chat 
debe constar de: 
 
1.  Un proceso central que maneja la totalidad del log del chat, esto es, la conversación de 
todos los otros procesos que participan en el chat. 
2.  Un  número  indeterminado  de  procesos  independientes  y  no  emparentados  que  se 
conectan al proceso central por medio de pipes (bidireccionales) y que tienen la opción de 
enviar  mensajes  y  leer  mensajes  desde  este  proceso  central.  Estos  procesos  pueden 
interrumpir su ejecución y salirse del sistema en cualquier momento. 
3.  Asimismo,  cada  proceso  independiente  (no  el  central),  tiene  la  capacidad  de  poder 
compartirse. Esto es, crea una copia de él mismo y se maneja luego como un proceso 
participante del chat que se conecta al proceso central y cumple las mismas funciones que 
su proceso padre. 
4.  Un sistema de reportes, en el cual un proceso secundario anexo al proceso central verifica 
cuantos reportes por mala conducta ha recibido algún proceso en particular (por su pid), 
en caso de tener más de 10 reportes en total, este proceso se encarga de matar al proceso 
en cuestión para desconectarlo del chat. Los reportes son enviados como mensajes al 
proceso central, mensajes de la forma “reportar pid”, donde pid es el pid del proceso a 
reportar. 
 
Nota: está prohibido el uso de programación con threads, mutex lock, semáforos.... 

---

# Implementación incluida en esta carpeta

Se incluye una implementación mínima en C que cumple los requisitos usando FIFOs con nombre (named pipes) y sin hilos:

- `server`: proceso central que:
  - Gestiona registro de clientes mediante un FIFO global `/tmp/chat_register.fifo`.
  - Crea y usa FIFOs por cliente para comunicación bidireccional.
  - Reenvía/broadcastea los mensajes a todos los clientes, con prefijo del `pid` de origen.
  - Acepta mensajes `reportar <pid>` y se los pasa a un subproceso de reportes.
  - Ejecuta un subproceso anexo que cuenta reportes por `pid` y mata (`SIGKILL`) al proceso reportado cuando supera 10.

- `client`: proceso independiente que:
  - Crea sus propios FIFOs, se registra ante el servidor y permite enviar/recibir mensajes.
  - Permite desconectarse con `salir`.
  - Permite clonarse con `clonar` (se hace un `fork()+exec` para crear otro cliente no emparentado lógicamente con el mismo rol en el chat).

## Compilación

Requisitos: gcc y make.

1. Compilar:

	make

2. Se generarán los binarios en `bin/`:
	- `bin/server`
	- `bin/client`

## Uso rápido

1) Inicie el servidor en una terminal:

	make run-server

	(o `./bin/server`)

2) En otra(s) terminal(es), inicie uno o más clientes:

	./bin/client

3) En un cliente, escriba mensajes y presione Enter para enviarlos. Comandos:
	- `reportar <pid>`: reporta al cliente con ese PID.
	- `clonar`: duplica el cliente actual (aparece un nuevo cliente que se registra solo).
	- `salir`: desconecta el cliente.

Notas:
- Los FIFOs temporales se crean bajo `/tmp/` con prefijos `chat_<pid>_cs.fifo` y `chat_<pid>_sc.fifo`.
- El servidor puede finalizarse con Ctrl+C; limpiará los FIFOs creados.
- El subproceso de reportes es simple y mantiene un contador en memoria; al pasar 10 reportes, envía `SIGKILL` al `pid` objetivo.
