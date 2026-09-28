#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_LINE 1024
#define MAX_ARGS 128
#define MAX_CMDS 64

int main() {
    char line[MAX_LINE];
    char cwd[1024];

    struct sigaction sa_ignore;
    sa_ignore.sa_handler = SIG_IGN;
    sigemptyset(&sa_ignore.sa_mask);
    sa_ignore.sa_flags = 0;
    sigaction(SIGINT, &sa_ignore, NULL);

    while (1) {
        // imprimir prompt
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("miShell:%s$ ", cwd);
        } else {
            printf("miShell:$ ");
        }
        fflush(stdout);

        // leer linea
        if (fgets(line, MAX_LINE, stdin) == NULL) {
            printf("\n");
            break;
        }
        line[strcspn(line, "\n")] = '\0';

        if (strlen(line) == 0) {
            continue;
        }

        // fraccionar primero por los pipes
        char *cmds[MAX_CMDS];
        int num_cmds = 0;
        cmds[num_cmds] = strtok(line, "|");
        while (cmds[num_cmds] != NULL && num_cmds < MAX_CMDS - 1) {
            num_cmds++;
            cmds[num_cmds] = strtok(NULL, "|");
        }

        int in_fd = 0; // guarda la entrada del pipe anterior
        int fd[2];
        pid_t pids[MAX_CMDS];

        // iterar sobre cada comando separado por pipe
        for (int i = 0; i < num_cmds; i++) {
            
            // fraccionar el comando actual por espacios
            char *args[MAX_ARGS];
            int a = 0;
            args[a] = strtok(cmds[i], " \t");
            while (args[a] != NULL && a < MAX_ARGS - 1) {
                a++;
                args[a] = strtok(NULL, " \t");
            }
            args[a] = NULL;

            if (args[0] == NULL) continue;

            // comandos internos solo aplican si es un comando suelto
            if (num_cmds == 1) {
                if (strcmp(args[0], "exit") == 0) exit(0);
                if (strcmp(args[0], "cd") == 0) {
                    const char *dir = (args[1] != NULL) ? args[1] : getenv("HOME");
                    if (chdir(dir) < 0) perror("cd");
                    continue;
                }
            }

            // crear el pipe si no es el ultimo comando
            if (i < num_cmds - 1) {
                if (pipe(fd) < 0) {
                    perror("Error en pipe");
                    exit(EXIT_FAILURE);
                }
            }

            pids[i] = fork();

            if (pids[i] < 0) {
                perror("Error en fork");
            } else if (pids[i] == 0) {
                // memoria del hijo

                //restaurar Ctrl+C para que el comando externo si muera
                struct sigaction sa_default;
                sa_default.sa_handler = SIG_DFL;
                sigemptyset(&sa_default.sa_mask);
                sa_default.sa_flags = 0;
                sigaction(SIGINT, &sa_default, NULL);

                // si hay entrada del pipe anterior la conectamos a stdin
                 if (in_fd != 0) {
                 dup2(in_fd, 0);
                 close(in_fd);
                  }
                
                // si no es el ultimo comando conectamos stdout al pipe nuevo
                if (i < num_cmds - 1) {
                    dup2(fd[1], 1);
                    close(fd[0]); // no usamos el lado de lectura aca
                    close(fd[1]);
                }

                // revisar redirecciones en los argumentos de este comando
                for (int j = 0; args[j] != NULL; j++) {
                    if (strcmp(args[j], ">") == 0) {
                        int out_fd = open(args[j+1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        dup2(out_fd, 1);
                        close(out_fd);
                        args[j] = NULL;
                        break;
                    } else if (strcmp(args[j], ">>") == 0) {
                        int out_fd = open(args[j+1], O_WRONLY | O_CREAT | O_APPEND, 0644);
                        dup2(out_fd, 1);
                        close(out_fd);
                        args[j] = NULL;
                        break;
                    } else if (strcmp(args[j], "<") == 0) {
                        int input_fd = open(args[j+1], O_RDONLY);
                        dup2(input_fd, 0);
                        close(input_fd);
                        args[j] = NULL;
                        break;
                    }
                }

                if (execvp(args[0], args) == -1) {
                    perror("Comando no encontrado");
                }
                exit(EXIT_FAILURE);
                
            } else {
                // memoria del padre
                
                // cerramos la entrada vieja porque el hijo ya la tiene
                if (in_fd != 0) {
                    close(in_fd);
                }
                
                // guardamos el lado de lectura del pipe para el proximo comando
                if (i < num_cmds - 1) {
                    close(fd[1]);
                    in_fd = fd[0];
                }
            }
        }

        // esperar a que todos los procesos de la tuberia terminen
        for (int i = 0; i < num_cmds; i++) {
            if (pids[i] > 0) {
                waitpid(pids[i], NULL, 0);
            }
        }
    }

    return 0;
}
