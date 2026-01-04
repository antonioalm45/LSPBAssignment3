# Nombre del ejecutable
TARGET = aesdsocket

# Lista de fuentes
SRCS = aesdsocket.c

# Generar objetos a partir de fuentes
OBJS = $(SRCS:.c=.o)

# Compilador: usa CROSS_COMPILE si está definido, sino gcc nativo
CC ?= $(CROSS_COMPILE)gcc

# Flags de compilación
CFLAGS ?= -Wall -Wextra -O2

LDFLAGS ?= -pthread -lrt

# Default target
all: $(TARGET)

# Compilar el ejecutable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Compilar cada archivo .c a .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Limpiar binarios y objetos
clean:
	rm -f aesdsocket *.o

# Phony targets
.PHONY: all clean
