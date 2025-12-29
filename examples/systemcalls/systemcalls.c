#include "systemcalls.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h> 
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{

/*
 * TODO  add your code here
 *  Call the system() function with the command set in the cmd
 *   and return a boolean true if the system() call completed with success
 *   or false() if it returned a failure
*/
    int ret = system(cmd);
    if (ret == -1) return false;
    return (WIFEXITED(ret) && WEXITSTATUS(ret) == 0);
}

/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];

/*
 * TODO:
 *   Execute a system command by calling fork, execv(),
 *   and wait instead of system (see LSP page 161).
 *   Use the command[0] as the full path to the command to execute
 *   (first argument to execv), and use the remaining arguments
 *   as second argument to the execv() command.
 *
*/
    // Crear un proceso hijo
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        va_end(args);
        return false;
    } else if (pid == 0) {
        // Proceso hijo: ejecutar el comando
        execv(command[0], command);
        // Si execv falla, termina el hijo con error
        perror("execv failed");
        _exit(1);
    } else {
        // Proceso padre: esperar a que el hijo termine
        int status;
        if (waitpid(pid, &status, 0) == -1) {
            va_end(args);
            return false;
        }
        va_end(args);
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }

    return true;
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
        printf("Printing command[%u]: %s\n", i, command[i]);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed
    command[count] = command[count];


/*
 * TODO
 *   Call execv, but first using https://stackoverflow.com/a/13784315/1446624 as a refernce,
 *   redirect standard out to a file specified by outputfile.
 *   The rest of the behaviour is same as do_exec()
 *
*/
    int fd = open(outputfile, O_WRONLY|O_TRUNC|O_CREAT, 0644);
    if (fd < 0) { 
        perror("open"); 
        va_end(args);
        return false; 
    }
    // Crear un proceso hijo
    pid_t pid = fork();
    if (pid < 0) {
        close(fd);
        perror("fork failed");
        va_end(args);
        return false;
    } else if (pid == 0) {
        if (dup2(fd, 1) < 0) { perror("dup2"); abort(); }
        close(fd);
        execvp(command[0], command);
        perror("execvp");
        abort();
        _exit(1);
    } else {
        close(fd);
        // Proceso padre: esperar a que el hijo termine
        int status;
        waitpid(pid, &status, 0);

        va_end(args);
        return WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }

    return true;
}
