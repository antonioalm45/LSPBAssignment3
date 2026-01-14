#include <fcntl.h>
#include <syslog.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char *argv[]){
    if (argc != 3) {  // 1 nombre del programa + 2 argumentos
        printf("Uso: %s arg1 arg2\n", argv[0]);
        return 1; // código de error
    }

    char * writefile = argv[1];
    char * writestr = argv[2];


    int fd = open(writefile, O_CREAT | O_WRONLY, 666);

    ssize_t bytesWritten = write(fd, writestr, strlen(writestr));
    (void)bytesWritten;
    // Abrir conexion syslog
     openlog("MiPrograma", LOG_PID | LOG_CONS, LOG_USER );

     // Enviar mensaje
    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);

    // Cerrar conexión
    closelog();

    printf("Argumento 1: %s\n", writefile);
    printf("Argumento 2: %s\n", writestr);
    return 0;
}