CC := gcc
CFLAGS := -Wall -Wextra -O2 -std=c11
LDFLAGS :=

SRC_DIR := src
BIN_DIR := bin

SRV := $(BIN_DIR)/server
CLI := $(BIN_DIR)/client

SRCS_COMMON := $(SRC_DIR)/common.h
SRCS_SERVER := $(SRC_DIR)/server.c $(SRCS_COMMON)
SRCS_CLIENT := $(SRC_DIR)/client.c $(SRCS_COMMON)

all: $(SRV) $(CLI)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(SRV): $(SRCS_SERVER) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(SRC_DIR)/server.c $(LDFLAGS)

$(CLI): $(SRCS_CLIENT) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(SRC_DIR)/client.c $(LDFLAGS)

.PHONY: clean run-server

clean:
	rm -rf $(BIN_DIR)
	rm -f /tmp/chat_*_cs.fifo /tmp/chat_*_sc.fifo /tmp/chat_register.fifo

run-server: $(SRV)
	$<
