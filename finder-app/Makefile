# Nombre del ejecutable
TARGET = writer

# Lista de fuentes
SRCS = writer.c

# Generar objetos a partir de fuentes
OBJS = $(SRCS:.c=.o)

# Compilador: usa CROSS_COMPILE si está definido, sino gcc nativo
CC = $(CROSS_COMPILE)gcc

# Flags de compilación
CFLAGS = -Wall -Wextra -O2

# Default target
all: $(TARGET)

# Compilar el ejecutable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Compilar cada archivo .c a .o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Limpiar binarios y objetos
clean:
	rm -f writer *.o

# Phony targets
.PHONY: all clean