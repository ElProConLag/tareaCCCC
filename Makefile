CC=gcc
CFLAGS=-Wall -Wextra -O2

all: servidor cliente

servidor: servidor.c comun.h
	$(CC) $(CFLAGS) servidor.c -o servidor

cliente: cliente.c comun.h
	$(CC) $(CFLAGS) cliente.c -o cliente

clean:
	rm -f servidor cliente chat.log
	-rm -f /tmp/chat_registro /tmp/c2s_* /tmp/s2c_*
