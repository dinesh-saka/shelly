#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <termios.h>
#include <ctype.h>

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
    int command_count = 1;

    // Count commands and validate pipe syntax
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "|") == 0) {
            if (i == 0 || args[i + 1] == NULL ||
                strcmp(args[i + 1], "|") == 0) {
                fprintf(stderr, "sheldon: invalid pipe syntax\n");
                return 1;
            }

            command_count++;
        }
    }

    // Allocate command and process arrays
    char ***commands = malloc(sizeof(char **) * command_count);
    pid_t *pids = malloc(sizeof(pid_t) * command_count);

    if (commands == NULL || pids == NULL) {
        perror("malloc");
        free(commands);
        free(pids);
        return 1;
    }

    // Split arguments into individual commands
    int command_index = 0;
    commands[command_index++] = args;

    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "|") == 0) {
            args[i] = NULL;
            commands[command_index++] = &args[i + 1];
        }
    }

    // Allocate N-1 pipes for N commands
    int pipe_count = command_count - 1;
    int (*pipes)[2] = NULL;

    if (pipe_count > 0) {
        pipes = malloc(sizeof(int[2]) * pipe_count);

        if (pipes == NULL) {
            perror("malloc");
            free(commands);
            free(pids);
            return 1;
        }

        for (int i = 0; i < pipe_count; i++) {
            if (pipe(pipes[i]) < 0) {
                perror("pipe");

                for (int j = 0; j < i; j++) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }

                free(pipes);
                free(commands);
                free(pids);
                return 1;
            }
        }
    }

    int started = 0;

    // Create one child process for every command
    for (int i = 0; i < command_count; i++) {
        pid_t pid = fork();

        if (pid == 0) {
            // Read from previous pipe
            if (i > 0) {
                if (dup2(pipes[i - 1][0], STDIN_FILENO) < 0) {
                    perror("dup2");
                    _exit(EXIT_FAILURE);
                }
            }

            // Write to next pipe
            if (i < command_count - 1) {
                if (dup2(pipes[i][1], STDOUT_FILENO) < 0) {
                    perror("dup2");
                    _exit(EXIT_FAILURE);
                }
            }

            // Close all pipe descriptors in the child
            for (int j = 0; j < pipe_count; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            execute_command(commands[i]);
        }
        else if (pid < 0) {
            perror("fork");
            break;
        }
        else {
            pids[started++] = pid;
        }
    }

    // Parent closes all pipe descriptors
    for (int i = 0; i < pipe_count; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // Wait for all successfully created children
    for (int i = 0; i < started; i++) {
        while (waitpid(pids[i], NULL, 0) < 0) {
            if (errno == EINTR) {
                continue;
            }

            perror("waitpid");
            break;
        }
    }

    free(pipes);
    free(commands);
    free(pids);

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


#define HISTORY_SIZE 100
#define INPUT_SIZE 4096

char *history[HISTORY_SIZE];
int history_count = 0;

void save_history(const char *command) {
    if (command[0] == '\0') {
        return;
    }

    if (history_count == HISTORY_SIZE) {
        free(history[0]);
        memmove(history, history + 1,
                sizeof(char *) * (HISTORY_SIZE - 1));
        history_count--;
    }

    history[history_count++] = strdup(command);
}

int read_command(const char *prompt, char **output) {
    struct termios original, raw;

    if (tcgetattr(STDIN_FILENO, &original) == -1) {
        perror("tcgetattr");
        return -2;
    }

    raw = original;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == -1) {
        perror("tcsetattr");
        return -2;
    }

    char buffer[INPUT_SIZE];
    int length = 0;
    int history_position = history_count;

    buffer[0] = '\0';

    printf("%s", prompt);
    fflush(stdout);

    while (1) {
        char c;
        ssize_t n = read(STDIN_FILENO, &c, 1);

        if (n == -1) {
            tcsetattr(STDIN_FILENO, TCSANOW, &original);

            if (errno == EINTR) {
                return -1;
            }

            perror("read");
            return -2;
        }

        if (n == 0) {
            tcsetattr(STDIN_FILENO, TCSANOW, &original);
            return 0;
        }

        // Enter
        if (c == '\n' || c == '\r') {
            buffer[length] = '\0';
            write(STDOUT_FILENO, "\n", 1);

            tcsetattr(STDIN_FILENO, TCSANOW, &original);

            *output = strdup(buffer);
            save_history(buffer);

            return 1;
        }

        // Ctrl+D
        if (c == 4 && length == 0) {
            tcsetattr(STDIN_FILENO, TCSANOW, &original);
            return 0;
        }

        // Backspace
        if (c == 127 || c == 8) {
            if (length > 0) {
                length--;
                buffer[length] = '\0';
                write(STDOUT_FILENO, "\b \b", 3);
            }
            continue;
        }

        // Arrow keys
        if (c == '\033') {
            char seq[2];

            if (read(STDIN_FILENO, &seq[0], 1) != 1 ||
                read(STDIN_FILENO, &seq[1], 1) != 1) {
                continue;
            }

            if (seq[0] == '[') {
                if (seq[1] == 'A') {
                    // Up arrow
                    if (history_position > 0) {
                        history_position--;
                        strncpy(buffer, history[history_position],
                                INPUT_SIZE - 1);
                        buffer[INPUT_SIZE - 1] = '\0';
                    }
                }
                else if (seq[1] == 'B') {
                    // Down arrow
                    if (history_position < history_count) {
                        history_position++;
                    }

                    if (history_position == history_count) {
                        buffer[0] = '\0';
                    }
                    else {
                        strncpy(buffer, history[history_position],
                                INPUT_SIZE - 1);
                        buffer[INPUT_SIZE - 1] = '\0';
                    }
                }

                length = strlen(buffer);

                printf("\r\033[2K%s%s", prompt, buffer);
                fflush(stdout);
            }

            continue;
        }

        // Regular printable characters
        if (isprint((unsigned char)c) && length < INPUT_SIZE - 1) {
            buffer[length++] = c;
            buffer[length] = '\0';

            write(STDOUT_FILENO, &c, 1);
        }
    }
}


void load_history(void) {
    char *home = getenv("HOME");

    if (home == NULL) {
        return;
    }

    char path[4096];
    snprintf(path, sizeof(path), "%s/.shelly_history", home);

    FILE *file = fopen(path, "r");

    if (file == NULL) {
        return;
    }

    char *line = NULL;
    size_t size = 0;

    while (getline(&line, &size, file) != -1) {
        line[strcspn(line, "\n")] = '\0';
        save_history(line);
    }

    free(line);
    fclose(file);
}

void write_history(void) {
    char *home = getenv("HOME");

    if (home == NULL) {
        return;
    }

    char path[4096];
    snprintf(path, sizeof(path), "%s/.shelly_history", home);

    FILE *file = fopen(path, "w");

    if (file == NULL) {
        perror("sheldon: history");
        return;
    }

    for (int i = 0; i < history_count; i++) {
        fprintf(file, "%s\n", history[i]);
    }

    fclose(file);
}


int main() {
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    load_history();

    while (1) {
        char cwd[1024];
        char prompt[1200];
        char *home = getenv("HOME");

        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            if (home != NULL &&
                strncmp(cwd, home, strlen(home)) == 0 &&
                (cwd[strlen(home)] == '/' ||
                 cwd[strlen(home)] == '\0')) {

                snprintf(prompt, sizeof(prompt),
                         "sheldon:~%s> ", cwd + strlen(home));
            }
            else {
                snprintf(prompt, sizeof(prompt),
                         "sheldon:%s> ", cwd);
            }
        }
        else {
            snprintf(prompt, sizeof(prompt), "sheldon> ");
        }

        char *line = NULL;
        int result = read_command(prompt, &line);

        if (result == 0) {
            break;
        }

        if (result == -1) {
            continue;
        }

        if (result == -2) {
            break;
        }

        char **args = split_line(line);
        free(line);

        if (args[0] != NULL) {
            if (strcmp(args[0], "exit") == 0) {
                free(args);
                break;
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

    write_history();
    for (int i = 0; i < history_count; i++) {
        free(history[i]);
    }

    return 0;
}
