#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

char **split_line(char *line) {
    int tokens_size = 64;
    int token_count = 0;
    char **tokens = malloc(tokens_size * sizeof(char *));

    int line_length = strlen(line);
    char *token = malloc(line_length + 1);

    int token_length = 0;
    int quote_state = 0;
    int token_started = 0;

    for (int i = 0; ; i++) {
        char c = line[i];

        if (c == '"' && quote_state == 0) {
            quote_state = 1;
            token_started = 1;
        }
        else if (c == '"' && quote_state == 1) {
            quote_state = 0;
        }
        else if ((c == ' ' || c == '\n' || c == '\0') && quote_state == 0) {
            if (token_started) {
                token[token_length] = '\0';

                if (token_count >= tokens_size - 1) {
                    tokens_size *= 2;
                    tokens = realloc(tokens, tokens_size * sizeof(char *));
                }

                tokens[token_count++] = strdup(token);
                token_length = 0;
                token_started = 0;
            }

            if (c == '\0') {
                break;
            }
        }
        else {
            token[token_length++] = c;
            token_started = 1;
        }
    }

    free(token);
    tokens[token_count] = NULL;

    return tokens;
}

void execute_command(char **args) {
    signal(SIGINT, SIG_DFL);
    for (int i = 0; args[i] != NULL; i++) {
        int target_fd = -1;
        int flags = 0;

        if (strcmp(args[i], ">") == 0) {
            target_fd = STDOUT_FILENO;
            flags = O_WRONLY | O_CREAT | O_TRUNC;
        }
        else if (strcmp(args[i], ">>") == 0) {
            target_fd = STDOUT_FILENO;
            flags = O_WRONLY | O_CREAT | O_APPEND;
        }
        else if (strcmp(args[i], "<") == 0) {
            target_fd = STDIN_FILENO;
            flags = O_RDONLY;
        }

        if (target_fd != -1) {
            if (args[i + 1] == NULL) {
                fprintf(stderr, "sheldon: expected filename after redirection\n");
                exit(EXIT_FAILURE);
            }

            int fd = open(args[i + 1], flags, 0644);

            if (fd < 0) {
                perror("sheldon");
                exit(EXIT_FAILURE);
            }

            if (dup2(fd, target_fd) < 0) {
                perror("dup2");
                close(fd);
                exit(EXIT_FAILURE);
            }

            close(fd);

            int j = i;
            while (args[j + 2] != NULL) {
                args[j] = args[j + 2];
                j++;
            }
            args[j] = NULL;

            i--;
        }
    }

    execvp(args[0], args);
    perror("sheldon");
    exit(EXIT_FAILURE);
}


int launch(char **args) {
    int pipe_index = -1;

    // Find the pipe operator
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "|") == 0) {
            if (pipe_index != -1) {
                fprintf(stderr, "sheldon: only one pipe supported for now\n");
                return 1;
            }
            pipe_index = i;
        }
    }

    // No pipe: execute a normal command
    if (pipe_index == -1) {
        pid_t pid = fork();

        if (pid == 0) {
            execute_command(args);
        }
        else if (pid < 0) {
            perror("fork");
        }
        else {
            waitpid(pid, NULL, 0);
        }

        return 1;
    }

    // Validate both sides of the pipe
    if (pipe_index == 0 || args[pipe_index + 1] == NULL) {
        fprintf(stderr, "sheldon: invalid pipe syntax\n");
        return 1;
    }

    // Separate the two commands
    args[pipe_index] = NULL;
    char **right_command = &args[pipe_index + 1];

    int fd[2];

    if (pipe(fd) < 0) {
        perror("pipe");
        return 1;
    }

    pid_t left_pid = fork();

    if (left_pid == 0) {
        dup2(fd[1], STDOUT_FILENO);
        close(fd[0]);
        close(fd[1]);

        execute_command(args);
    }
    else if (left_pid < 0) {
        perror("fork");
        close(fd[0]);
        close(fd[1]);
        return 1;
    }

    pid_t right_pid = fork();

    if (right_pid == 0) {
        dup2(fd[0], STDIN_FILENO);
        close(fd[0]);
        close(fd[1]);

        execute_command(right_command);
    }
    else if (right_pid < 0) {
        perror("fork");
        close(fd[0]);
        close(fd[1]);
        waitpid(left_pid, NULL, 0);
        return 1;
    }

    // Parent doesn't need either end of the pipe
    close(fd[0]);
    close(fd[1]);

    waitpid(left_pid, NULL, 0);
    waitpid(right_pid, NULL, 0);

    return 1;
}

int shell_cd(char **args) {
    char *directory;

    if (args[1] == NULL) {
        directory = getenv("HOME");
    }
    else {
	directory = args[1];
    }

    if (directory == NULL) {
        fprintf(stderr, "sheldon: HOME not set\n");
    }
    else if (chdir(directory) != 0) {
        perror("sheldon");
    }

    return 1;

}

int shell_help(void) {
    printf("Shelly - A simple Unix shell\n\n");
    printf("Built-in commands:\n");
    printf("  cd [directory]   Change directory\n");
    printf("  help             Display this help\n");
    printf("  exit             Exit Shelly\n");

    return 1;
}

void handle_sigint(int sig) {
    (void)sig;
    write(STDOUT_FILENO, "\n", 1);
}

int main() {
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    char *line = NULL;
    size_t bufsize = 0;

    while (1) {
	char cwd[1024];
	char *home = getenv("HOME");

	if (getcwd(cwd, sizeof(cwd)) != NULL) {

	    if (home != NULL &&
    		strncmp(cwd, home, strlen(home)) == 0 &&
		(cwd[strlen(home)] == '/' || cwd[strlen(home)] == '\0')) {

		printf("sheldon:~%s> ", cwd + strlen(home));

	    }

	    else {
   	        printf("sheldon:%s> ", cwd);
	    }
	}
	else {
	    printf("sheldon> ");
	}

        fflush(stdout);

        if (getline(&line, &bufsize, stdin) == -1) {
	    if (feof(stdin)) {
		break;
            }

            if (errno == EINTR) {
		clearerr(stdin);
		continue;
            }

	    break;
        }

        char **args = split_line(line);

        if (args[0] != NULL) {
	    if (strcmp(args[0], "exit") == 0) {
		free(args);
		free(line);
		return 0;
	    }
            else if (strcmp(args[0], "cd") == 0) {
	        shell_cd(args);
            }
	    else if (strcmp(args[0], "help") == 0) {
    		shell_help();
	    }
	    else {
		launch(args);
            }
        }

        free(args);
    }

    free(line);
    return 0;
}
