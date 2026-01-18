// the shell should has a infinite loop, which keep reading user input and executing commands
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>

#define MAX_ARGS 100
#define MAX_PATH_ENTRIES 100

void error_message(void) {
    char error_message[30] = "An error has occurred\n";
    write(STDERR_FILENO, error_message, strlen(error_message));
}

void execute_command(char **args, char **path_entries, int path_count) {
    // Check for redirection (handling both "cmd > file" and "cmd>file" formats)
    char *redirect_file = NULL;
    int arg_count = 0;
    for (int i = 0; args[i] != NULL; i++) {
        // Check if this arg contains > (e.g., "file>output" or just ">")
        char *redir_pos = strchr(args[i], '>');
        if (redir_pos != NULL) {
            if (strcmp(args[i], ">") == 0) {
                // Case: separate > argument
                if (args[i + 1] == NULL || args[i + 2] != NULL) {
                    error_message();
                    return;
                }
                redirect_file = args[i + 1];
                args[i] = NULL;
            } else if (redir_pos == args[i]) {
                // Case: ">file"
                redirect_file = redir_pos + 1;
                args[i] = NULL;
            } else {
                // Case: "arg>file" - split the argument
                *redir_pos = '\0';
                redirect_file = redir_pos + 1;
                if (args[i + 1] != NULL) {
                    error_message();
                    return;
                }
            }
            break;
        }
        arg_count++;
    }

    // If command is empty, return
    if (args[0] == NULL) {
        return;
    }

    // Try to execute the command with an absolute or relative path
    if (strchr(args[0], '/') != NULL) {
        // Absolute or relative path specified
        if (access(args[0], X_OK) != 0) {
            error_message();
            return;
        }
        pid_t child = fork();
        if (child == 0) {
            // Child process
            if (redirect_file != NULL) {
                int fd = open(redirect_file, O_CREAT | O_WRONLY | O_TRUNC, 0644);
                if (fd == -1) {
                    error_message();
                    exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                dup2(fd, STDERR_FILENO);
                close(fd);
            }
            execv(args[0], args);
            error_message();
            exit(1);
        } else if (child > 0) {
            // Parent process does nothing, will wait later
        } else {
            error_message();
        }
    } else {
        // Search in path
        int found = 0;
        for (int i = 0; i < path_count; i++) {
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s", path_entries[i], args[0]);
            if (access(full_path, X_OK) == 0) {
                found = 1;
                pid_t child = fork();
                if (child == 0) {
                    // Child process
                    if (redirect_file != NULL) {
                        int fd = open(redirect_file, O_CREAT | O_WRONLY | O_TRUNC, 0644);
                        if (fd == -1) {
                            error_message();
                            exit(1);
                        }
                        dup2(fd, STDOUT_FILENO);
                        dup2(fd, STDERR_FILENO);
                        close(fd);
                    }
                    execv(full_path, args);
                    error_message();
                    exit(1);
                } else if (child > 0) {
                    // Parent process does nothing, will wait later
                } else {
                    error_message();
                }
                break;
            }
        }
        if (!found) {
            error_message();
        }
    }
}

int main(int argc, char *argv[]) {
    // 1. Determine mode (Interactive vs Batch)
    FILE *input = stdin;
    int interactive = 1;

    if (argc == 2) {
        input = fopen(argv[1], "r");
        if (input == NULL) {
            error_message();
            exit(1);
        }
        interactive = 0;
    } else if (argc > 2) {
        error_message();
        exit(1);
    }

    // 2. Initialize path (default: /bin)
    char **path_entries = (char **)malloc(MAX_PATH_ENTRIES * sizeof(char *));
    int path_count = 1;
    path_entries[0] = "/bin";

    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;

    while (1) {
        // 3. Print prompt (if interactive)
        if (interactive) {
            printf("wish> ");
            fflush(stdout);
        }

        // 4. Read input using getline()
        linelen = getline(&line, &linecap, input);
        
        if (linelen == -1) {
            // EOF reached
            exit(0);
        }

        // Remove trailing newline
        if (linelen > 0 && line[linelen - 1] == '\n') {
            line[linelen - 1] = '\0';
        }

        // 5. Parse input (handle & for parallel commands)
        char *line_copy = strdup(line);
        char **commands = (char **)malloc(MAX_ARGS * sizeof(char *));
        int cmd_count = 0;

        char *cmd = strsep(&line_copy, "&");
        while (cmd != NULL) {
            // Trim whitespace
            while (*cmd == ' ' || *cmd == '\t') cmd++;
            char *end = cmd + strlen(cmd) - 1;
            while (end > cmd && (*end == ' ' || *end == '\t' || *end == '\n')) {
                *end = '\0';
                end--;
            }

            if (*cmd != '\0') {
                commands[cmd_count++] = strdup(cmd);
            }
            cmd = strsep(&line_copy, "&");
        }
        free(line_copy);

        // 6. Execute commands
        for (int i = 0; i < cmd_count; i++) {
            char *cmd_line = commands[i];
            char **args = (char **)malloc(MAX_ARGS * sizeof(char *));
            int arg_count = 0;

            char *cmd_copy = strdup(cmd_line);
            char *arg = strsep(&cmd_copy, " \t");
            while (arg != NULL) {
                if (*arg != '\0') {
                    args[arg_count++] = arg;
                }
                arg = strsep(&cmd_copy, " \t");
            }
            args[arg_count] = NULL;

            if (arg_count == 0) {
                free(cmd_copy);
                free(args);
                continue;
            }

            // Check for built-in commands
            if (strcmp(args[0], "exit") == 0) {
                if (arg_count > 1) {
                    error_message();
                } else {
                    exit(0);
                }
            } else if (strcmp(args[0], "cd") == 0) {
                if (arg_count != 2) {
                    error_message();
                } else {
                    if (chdir(args[1]) != 0) {
                        error_message();
                    }
                }
            } else if (strcmp(args[0], "path") == 0) {
                // Free old path entries (except the first one which is static)
                for (int j = 1; j < path_count; j++) {
                    free(path_entries[j]);
                }
                
                path_count = arg_count - 1;
                for (int j = 0; j < path_count; j++) {
                    path_entries[j] = strdup(args[j + 1]);
                }
            } else if (strcmp(args[0], ">") == 0) {
                // Redirection without command
                error_message();
            } else {
                // Regular command
                execute_command(args, path_entries, path_count);
            }

            free(cmd_copy);
            free(args);
            free(commands[i]);
        }

        // Wait for all child processes to finish
        while (wait(NULL) > 0);

        free(commands);
    }

    free(line);
    free(path_entries);
    return 0;
}
