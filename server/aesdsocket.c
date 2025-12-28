#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syslog.h>
#include <signal.h>

#define PORT 9000
#define BACKLOG 1
#define DATAFILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

static volatile sig_atomic_t exit_requested = 0;

void signal_handler(int sig)
{
    (void)sig;
    exit_requested = 1;
    syslog(LOG_INFO, "Caught signal, exiting");
}

int main(int argc, char *argv[]) {
    bool daemon_mode = false;

    int server_fd = -1, client_fd = -1;
    struct sockaddr_in server_address, client_address;
    int opt = 1;
    int client_len = sizeof(client_address);
    char recv_buf[BUFFER_SIZE] = {0};
    char send_buf[BUFFER_SIZE] = {0};
    ssize_t bytes_received = 0;
    FILE *fp = NULL;

    // 1. Verificar si se pasó el argumento -d
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = true;
    }

    // 3. Entrar en modo demonio si es necesario
    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(-1);
        }
        if (pid > 0) {
            // Proceso padre termina
            exit(0);
        }
        
        // Proceso hijo: Crear nueva sesión y cambiar directorio
        if (setsid() < 0) exit(-1);
        if (chdir("/") < 0) exit(-1);
        
        // Opcional: Redirigir descriptores de archivo estándar a /dev/null
        open("/dev/null", O_RDWR); // stdin
        (void)dup(0); // stdout
        (void)dup(0); // stderr
    }

    /* Register signal handlers */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;   // IMPORTANTE: NO SA_RESTART

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // Open syslog
    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);

    // Create socket file descriptor
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        syslog(LOG_ERR, "Socket creation failed: %s", strerror(errno));
        return -1;
    }

    // Set socket options
    // level=SOL_SOCKET, which indicates an option that applies at the sockets API level.
    // optname=SO_REUSEADDR, which allows the socket to bind to an address that is already in use.
    opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        syslog(LOG_ERR, "Set socket options failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    // Bind the socket
    // Initialize address structure
    // sin_family: Address family (AF_INET for IPv4)
    // sin_addr.s_addr: IP address (INADDR_ANY to bind to all interfaces)
    // sin_port: Port number (converted to network byte order using htons)
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        syslog(LOG_ERR, "Bind failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    // Listen for incoming connections
    if (listen(server_fd, BACKLOG) < 0) {
        syslog(LOG_ERR, "Listen failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    while (!exit_requested) {
        // Accept a connection
        // client_fd: New socket file descriptor for the accepted connection
        // client_address: Pointer to sockaddr structure to hold client client_address
        // client_len: Pointer to socklen_t variable that holds the size of the client_address structure
        if ((client_fd = accept(server_fd, (struct sockaddr *)&client_address,
                                 (socklen_t*)&client_len)) < 0) {
            if (errno == EINTR && exit_requested) 
                break;
            syslog(LOG_ERR, "Accept failed: %s", strerror(errno));
            close(server_fd);
            continue;
        }
        // Logs message to the syslog “Accepted connection from xxx” where XXXX is the IP client_address of the connected client. 
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_address.sin_addr, client_ip, sizeof(client_ip));
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        // Open data file
        // a+ mode: Open for reading and appending; create file if it doesn't exist
        fp = fopen(DATAFILE, "a+");
        if (!fp) {
            syslog(LOG_ERR, "File open failed: %s", strerror(errno));
            close(client_fd);
            close(server_fd);
            continue;
        }

        // Read data from socket and write to file
        // bytes_received: Number of bytes read from socket
        while (!exit_requested && (bytes_received = read(client_fd, recv_buf, BUFFER_SIZE)) > 0) {
            fwrite(recv_buf, 1, bytes_received, fp);
            fflush(fp);
            if (memchr(recv_buf, '\n', bytes_received)) {
                rewind(fp);

                while ((bytes_received = fread(send_buf, 1, BUFFER_SIZE, fp)) > 0) {
                    send(client_fd, send_buf, bytes_received, 0);
                }
                break;
            }
        }

        fclose(fp);
        close(client_fd);
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
         
    }  

    /* Cleanup */
    close(server_fd);
    remove(DATAFILE);
    closelog();

    return 0;
}