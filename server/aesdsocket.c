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
#include <pthread.h>
#include <time.h>
#include <sys/ioctl.h>
#include "../aesd-char-driver/aesd_ioctl.h"

#define PORT 9000
#define BACKLOG 1
#define DATAFILE "/dev/aesdchar"
#define BUFFER_SIZE 1024

// Estructura para pasar datos a los hilos
typedef struct thread_data
{
    int client_fd;
    struct sockaddr_in client_addr;
    pthread_t thread_id;
    bool completed;
    struct thread_data *next;
} thread_data_t;

static volatile sig_atomic_t exit_requested = 0;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

void signal_handler(int sig)
{
    (void)sig;
    exit_requested = 1;
    syslog(LOG_INFO, "Caught signal, exiting");
}

// HILO DEL TEMPORIZADOR (Escribe cada 10s)
void *timer_thread(void *arg)
{
    (void)arg;
    while (!exit_requested)
    {
        // Esperar 10 segundos (usando sleep en intervalos cortos para responder rápido al exit)
        for (int i = 0; i < 10 && !exit_requested; i++)
        {
            sleep(1);
        }

        if (exit_requested)
            break;

        time_t rawtime;
        struct tm *info;
        char timestamp[100];

        time(&rawtime);
        info = localtime(&rawtime);
        // Formato RFC 2822: %a, %d %b %Y %H:%M:%S %z
        strftime(timestamp, sizeof(timestamp), "timestamp:%a, %d %b %Y %H:%M:%S %z\n", info);

        pthread_mutex_lock(&file_mutex);
        FILE *fp = fopen(DATAFILE, "a+");
        if (fp)
        {
            fputs(timestamp, fp);
            fclose(fp);
        }
        pthread_mutex_unlock(&file_mutex);
    }
    return NULL;
}

void *handle_connection(void *arg)
{
    thread_data_t *data = (thread_data_t *)arg;
    char client_ip[INET_ADDRSTRLEN];
    char *recv_buf = malloc(BUFFER_SIZE);
    char *send_buf = malloc(BUFFER_SIZE);

    inet_ntop(AF_INET, &data->client_addr.sin_addr, client_ip, sizeof(client_ip));

    // El archivo debe abrirse y cerrarse dentro del hilo o manejarse con cuidado
    // Usaremos fopen/fclose dentro de la sección crítica para asegurar consistencia

    ssize_t bytes_received;
    bool newline_found = false;
    bool is_ioctl_command = false;
    uint32_t write_cmd = 0, write_cmd_offset = 0;

    while (!exit_requested && (bytes_received = recv(data->client_fd, recv_buf, BUFFER_SIZE, 0)) > 0)
    {
        // Check if this is an AESDCHAR_IOCSEEKTO command
        syslog(LOG_DEBUG, "Received: %.*s", (int)bytes_received, recv_buf);
        if (bytes_received > 0 && strncmp(recv_buf, "AESDCHAR_IOCSEEKTO:", 19) == 0)
        {
            // Parse X,Y from the command
            char *cmd_params = recv_buf + 19; // Skip "AESDCHAR_IOCSEEKTO:"
            int parsed = sscanf(cmd_params, "%u,%u", &write_cmd, &write_cmd_offset);
            syslog(LOG_DEBUG, "Parsed ioctl: write_cmd=%u, write_cmd_offset=%u, result=%d", write_cmd, write_cmd_offset, parsed);
            if (parsed == 2)
            {
                is_ioctl_command = true;
                newline_found = true;
                syslog(LOG_INFO, "AESDCHAR_IOCSEEKTO command detected: cmd=%u offset=%u", write_cmd, write_cmd_offset);
                break;
            }
        }

        pthread_mutex_lock(&file_mutex);
        FILE *fp = fopen(DATAFILE, "a+");
        if (!fp)
        {
            syslog(LOG_ERR, "File open failed: %s", strerror(errno));
            close(data->client_fd);
            continue;
        }
        if (fp)
        {
            // 1. Obtener el tiempo actual
            time_t rawtime;
            struct tm *info;
            char timestamp[64];

            time(&rawtime);
            info = localtime(&rawtime);

            // 2. Formatear la fecha según RFC 2822
            // %a: nombre día semana abreviado, %d: día, %b: mes abreviado, %Y: año, %H:%M:%S: hora, %z: zona horaria
            strftime(timestamp, sizeof(timestamp), "%a, %d %b %Y %H:%M:%S %z", info);

            // 3. Escribir primero el timestamp (opcionalmente con un prefijo para identificarlo)
            fprintf(fp, "timestamp:%s\n", timestamp);
            fwrite(recv_buf, 1, bytes_received, fp);
            fclose(fp);
        }
        pthread_mutex_unlock(&file_mutex);

        if (memchr(recv_buf, '\n', bytes_received))
        {
            newline_found = true;
            break;
        }
    }

    if (newline_found)
    {
        if (is_ioctl_command)
        {
            // Handle AESDCHAR_IOCSEEKTO command
            pthread_mutex_lock(&file_mutex);

            // Open device file with file descriptor (not FILE*)
            int fd = open(DATAFILE, O_RDWR);
            if (fd < 0)
            {
                syslog(LOG_ERR, "Failed to open device for ioctl: %s", strerror(errno));
                pthread_mutex_unlock(&file_mutex);
            }
            else
            {
                // Prepare ioctl structure
                struct aesd_seekto seekto;
                seekto.write_cmd = write_cmd;
                seekto.write_cmd_offset = write_cmd_offset;

                // Perform ioctl
                int ret = ioctl(fd, AESDCHAR_IOCSEEKTO, &seekto);
                if (ret != 0)
                {
                    syslog(LOG_ERR, "ioctl failed: %s", strerror(errno));
                }
                else
                {
                    // Read from the same file descriptor after ioctl
                    ssize_t bytes_read;
                    while ((bytes_read = read(fd, send_buf, BUFFER_SIZE)) > 0)
                    {
                        send(data->client_fd, send_buf, bytes_read, 0);
                    }
                }

                close(fd);
                pthread_mutex_unlock(&file_mutex);
            }
        }
        else
        {
            // Normal case: read entire file and send back
            pthread_mutex_lock(&file_mutex);
            FILE *fp = fopen(DATAFILE, "r");
            if (fp)
            {
                size_t bytes_read;
                while ((bytes_read = fread(send_buf, 1, BUFFER_SIZE, fp)) > 0)
                {
                    send(data->client_fd, send_buf, bytes_read, 0);
                }
                fclose(fp);
            }
            pthread_mutex_unlock(&file_mutex);
        }
    }

    close(data->client_fd);
    syslog(LOG_INFO, "Closed connection from %s", client_ip);

    free(recv_buf);
    free(send_buf);
    data->completed = true;
    return arg;
}

int main(int argc, char *argv[])
{
    bool daemon_mode = (argc > 1 && strcmp(argv[1], "-d") == 0);

    if (daemon_mode)
    {
        pid_t pid = fork();
        if (pid < 0)
            exit(-1);
        if (pid > 0)
            exit(0);
        setsid();
        chdir("/");
        int devnull = open("/dev/null", O_RDWR);
        dup2(devnull, STDIN_FILENO);
        dup2(devnull, STDOUT_FILENO);
        dup2(devnull, STDERR_FILENO);
    }

    struct sigaction sa = {.sa_handler = signal_handler};
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    openlog("aesdsocket", LOG_PID, LOG_USER);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_address = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(PORT)};

    if (bind(server_fd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0)
    {
        syslog(LOG_ERR, "Bind failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }
    if (listen(server_fd, BACKLOG) < 0)
    {
        syslog(LOG_ERR, "Listen failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    // LANZAR EL HILO DEL TEMPORIZADOR
    pthread_t timer_tid;
    pthread_create(&timer_tid, NULL, timer_thread, NULL);

    thread_data_t *head = NULL;

    while (!exit_requested)
    {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

        if (client_fd < 0)
        {
            if (errno == EINTR && exit_requested)
                break;
            syslog(LOG_ERR, "Accept failed: %s", strerror(errno));
            close(server_fd);

            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        // Crear nodo para el nuevo hilo
        thread_data_t *new_thread = malloc(sizeof(thread_data_t));
        new_thread->client_fd = client_fd;
        new_thread->client_addr = client_addr;
        new_thread->completed = false;
        new_thread->next = head;
        head = new_thread;

        pthread_create(&new_thread->thread_id, NULL, handle_connection, new_thread);

        // Limpieza de hilos finalizados (Join)
        thread_data_t *curr = head, *prev = NULL;
        while (curr)
        {
            if (curr->completed)
            {
                pthread_join(curr->thread_id, NULL);
                if (prev)
                    prev->next = curr->next;
                else
                    head = curr->next;

                thread_data_t *temp = curr;
                curr = curr->next;
                free(temp);
            }
            else
            {
                prev = curr;
                curr = curr->next;
            }
        }
    }

    /* Cleanup */
    while (head)
    {
        pthread_join(head->thread_id, NULL);
        thread_data_t *temp = head;
        head = head->next;
        free(temp);
    }

    // Limpieza final
    pthread_join(timer_tid, NULL);
    while (head)
    {
        pthread_join(head->thread_id, NULL);
        thread_data_t *temp = head;
        head = head->next;
        free(temp);
    }

    close(server_fd);
    remove(DATAFILE);
    pthread_mutex_destroy(&file_mutex);
    closelog();
    return 0;
}
