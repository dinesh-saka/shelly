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
#include <time.h>

#define HISTORY_SIZE 100
#define INPUT_SIZE 4096
#define MAX_JOBS 100

typedef struct {
    int id;
    pid_t *pids;
    int process_count;
    char *command;
    int active;
} Job;

Job jobs[MAX_JOBS] = {0};
int job_count = 0;
int next_job_id = 1;

char *history[HISTORY_SIZE];
int history_count = 0;

int shell_exit_requested = 0;

static int add_token(char ***tokens, int *count, int *capacity,
                     const char *value, int length) {
    if (*count >= *capacity - 1) {
        int new_capacity = *capacity * 2;
        char **new_tokens = realloc(*tokens,
                                    new_capacity * sizeof(char *));

        if (new_tokens == NULL) {
            return -1;
        }

        *tokens = new_tokens;
        *capacity = new_capacity;
    }

    char *copy = malloc(length + 1);

    if (copy == NULL) {
        return -1;
    }

    memcpy(copy, value, length);
    copy[length] = '\0';

    (*tokens)[(*count)++] = copy;
    return 0;
}

static int append_char(char **buffer, int *length, int *capacity, char c) {
    if (*length + 1 >= *capacity) {
        int new_capacity = *capacity * 2;
        char *new_buffer = realloc(*buffer, new_capacity);

        if (new_buffer == NULL) {
            return -1;
        }

        *buffer = new_buffer;
        *capacity = new_capacity;
    }

    (*buffer)[(*length)++] = c;
    return 0;
}

static int append_string(char **buffer, int *length, int *capacity,
                         const char *value) {
    if (value == NULL) {
        return 0;
    }

    while (*value != '\0') {
        if (append_char(buffer, length, capacity, *value++) < 0) {
            return -1;
        }
    }

    return 0;
}

char **split_line(char *line) {
    int tokens_size = 64;
    int token_count = 0;

    char **tokens = malloc(tokens_size * sizeof(char *));

    if (tokens == NULL) {
        perror("sheldon: malloc");
        return NULL;
    }

    int line_length = strlen(line);
    int token_capacity = line_length + 1;
    int token_length = 0;

    char *token = malloc(token_capacity);

    if (token == NULL) {
        perror("sheldon: malloc");
        free(tokens);
        return NULL;
    }

    int token_started = 0;
    char quote = '\0';

    for (int i = 0; i <= line_length; i++) {
        char c = line[i];

        /*
         * Outside single quotes, backslash escapes the next character.
         * Inside double quotes, it only escapes ", \ and $.
         * For other characters (such as the n in \n), preserve it.
         */
        if (c == '\\' && i < line_length && quote != '\'') {
            char next = line[i + 1];

            if (quote == '"' &&
                next != '"' && next != '\\' && next != '$') {
                if (append_char(&token, &token_length,
                                &token_capacity, '\\') < 0) {
                    goto allocation_error;
                }

                token_started = 1;
                continue;
            }

            if (append_char(&token, &token_length,
                            &token_capacity, next) < 0) {
                goto allocation_error;
            }

            token_started = 1;
            i++;
            continue;
        }

        // Start single or double quotes.
        if (quote == '\0' && (c == '\'' || c == '"')) {
            quote = c;
            token_started = 1;
            continue;
        }

        // End the current quote.
        if (quote == c && quote != '\0') {
            quote = '\0';
            continue;
        }

        // An open quote at end-of-input is a syntax error.
        if (quote != '\0' && c == '\0') {
            fprintf(stderr, "sheldon: unmatched %s quote\n",
                    quote == '\'' ? "single" : "double");
            goto syntax_error;
        }

        // Expand environment variables unless inside single quotes.
        if (c == '$' && quote != '\'') {
            int start;
            int end;
            int braced = 0;

            if (line[i + 1] == '{') {
                braced = 1;
                start = i + 2;
                end = start;

                while (line[end] != '\0' && line[end] != '}') {
                    end++;
                }

                if (line[end] != '}') {
                    token_started = 1;

                    if (append_char(&token, &token_length,
                                    &token_capacity, '$') < 0) {
                        goto allocation_error;
                    }

                    continue;
                }
            }
            else if (isalpha((unsigned char)line[i + 1]) ||
                     line[i + 1] == '_') {
                start = i + 1;
                end = start;

                while (isalnum((unsigned char)line[end]) ||
                       line[end] == '_') {
                    end++;
                }
            }
            else {
                token_started = 1;

                if (append_char(&token, &token_length,
                                &token_capacity, '$') < 0) {
                    goto allocation_error;
                }

                continue;
            }

            int name_length = end - start;
            char *name = malloc(name_length + 1);

            if (name == NULL) {
                goto allocation_error;
            }

            memcpy(name, line + start, name_length);
            name[name_length] = '\0';

            const char *value = getenv(name);
            free(name);

            if (append_string(&token, &token_length,
                              &token_capacity, value) < 0) {
                goto allocation_error;
            }

            token_started = 1;
            i = braced ? end : end - 1;
            continue;
        }

        // Characters inside quotes belong to the same token.
        if (quote != '\0') {
            if (append_char(&token, &token_length,
                            &token_capacity, c) < 0) {
                goto allocation_error;
            }

            token_started = 1;
            continue;
        }

        // Finish a token at whitespace or end of input.
        if (c == '\0' || isspace((unsigned char)c)) {
            if (token_started) {
                if (add_token(&tokens, &token_count, &tokens_size,
                              token, token_length) < 0) {
                    goto allocation_error;
                }

                token_length = 0;
                token_started = 0;
            }

            if (c == '\0') {
                break;
            }

            continue;
        }

        // Recognize operators without requiring spaces.
        if (c == '|' || c == '&' || c == '>' ||
            c == '<' || c == ';') {

            if (token_started) {
                if (add_token(&tokens, &token_count, &tokens_size,
                              token, token_length) < 0) {
                    goto allocation_error;
                }

                token_length = 0;
                token_started = 0;
            }

            int operator_length = 1;

            if ((c == '>' && line[i + 1] == '>') ||
                (c == '&' && line[i + 1] == '&') ||
                (c == '|' && line[i + 1] == '|')) {
                operator_length = 2;
            }

            if (add_token(&tokens, &token_count, &tokens_size,
                          &line[i], operator_length) < 0) {
                goto allocation_error;
            }

            if (operator_length == 2) {
                i++;
            }

            continue;
        }

        if (append_char(&token, &token_length,
                        &token_capacity, c) < 0) {
            goto allocation_error;
        }

        token_started = 1;
    }

    if (token_started) {
        if (add_token(&tokens, &token_count, &tokens_size,
                      token, token_length) < 0) {
            goto allocation_error;
        }
    }

    free(token);
    tokens[token_count] = NULL;

    return tokens;

syntax_error:
    for (int i = 0; i < token_count; i++) {
        free(tokens[i]);
    }

    free(tokens);
    free(token);
    return NULL;

allocation_error:
    perror("sheldon: token allocation");

    for (int i = 0; i < token_count; i++) {
        free(tokens[i]);
    }

    free(tokens);
    free(token);

    return NULL;
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
            if (args[i + 1] == NULL ||
                strcmp(args[i + 1], ">") == 0 ||
                strcmp(args[i + 1], ">>") == 0 ||
                strcmp(args[i + 1], "<") == 0) {
                fprintf(stderr,
                        "sheldon: missing or invalid filename after redirection\n");
                _exit(EXIT_FAILURE);
            }

            int fd = open(args[i + 1], flags, 0644);

            if (fd < 0) {
                perror(args[i + 1]);
                _exit(EXIT_FAILURE);
            }

            if (dup2(fd, target_fd) < 0) {
                perror("sheldon: dup2");
                close(fd);
                _exit(EXIT_FAILURE);
            }

            close(fd);

            // Remove the redirection operator and filename from arguments.
            int j = i;
            while (args[j + 2] != NULL) {
                args[j] = args[j + 2];
                j++;
            }
            args[j] = NULL;

            i--;
        }
    }

    if (args[0] == NULL) {
        fprintf(stderr, "sheldon: missing command\n");
        _exit(EXIT_FAILURE);
    }

    execvp(args[0], args);

    if (errno == ENOENT) {
        fprintf(stderr, "sheldon: %s: command not found\n", args[0]);
    }
    else {
        perror(args[0]);
    }

    _exit(EXIT_FAILURE);
}

void reap_background_jobs(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!jobs[i].active) {
            continue;
        }

        int all_finished = 1;

        for (int j = 0; j < jobs[i].process_count; j++) {
            if (jobs[i].pids[j] == 0) {
                continue;
            }

            int status;
            pid_t result = waitpid(
                jobs[i].pids[j], &status, WNOHANG
            );

            if (result == jobs[i].pids[j]) {
                jobs[i].pids[j] = 0;
            }
            else if (result == 0) {
                all_finished = 0;
            }
            else if (result < 0 && errno == EINTR) {
                all_finished = 0;
            }
            else if (result < 0 && errno == ECHILD) {
                jobs[i].pids[j] = 0;
            }
            else if (result < 0) {
                perror("sheldon: waitpid");
                jobs[i].pids[j] = 0;
            }
        }

        if (all_finished) {
            for (int j = 0; j < jobs[i].process_count; j++) {
                if (jobs[i].pids[j] != 0) {
                    all_finished = 0;
                    break;
                }
            }
        }

        if (all_finished) {
            printf("[%d] Done\t%s\n",
                   jobs[i].id, jobs[i].command);

            free(jobs[i].pids);
            free(jobs[i].command);

            jobs[i].pids = NULL;
            jobs[i].command = NULL;
            jobs[i].active = 0;
            job_count--;
        }
    }
}

void list_jobs(void) {
    reap_background_jobs();

    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].active) {
            printf("[%d] Running\t%s\n",
                   jobs[i].id, jobs[i].command);
        }
    }
}

int wait_for_jobs(void) {
    int final_status = 1;

    for (int i = 0; i < MAX_JOBS; i++) {
        if (!jobs[i].active) {
            continue;
        }

        for (int j = 0; j < jobs[i].process_count; j++) {
            if (jobs[i].pids[j] == 0) {
                continue;
            }

            int status;
            pid_t result;

            do {
                result = waitpid(
                    jobs[i].pids[j], &status, 0
                );
            } while (result < 0 && errno == EINTR);

            if (result < 0) {
                perror("sheldon: waitpid");
                final_status = 0;
            }
            else if (j == jobs[i].process_count - 1) {
                final_status = WIFEXITED(status) &&
                               WEXITSTATUS(status) == 0;
            }

            jobs[i].pids[j] = 0;
        }

        printf("[%d] Done\t%s\n",
               jobs[i].id, jobs[i].command);

        free(jobs[i].pids);
        free(jobs[i].command);

        jobs[i].pids = NULL;
        jobs[i].command = NULL;
        jobs[i].active = 0;
        job_count--;
    }

    return final_status;
}

int register_job(pid_t *pids, int count, const char *command) {
    if (job_count >= MAX_JOBS) {
        fprintf(stderr, "sheldon: maximum background jobs reached\n");
        return 0;
    }

    for (int i = 0; i < MAX_JOBS; i++) {
        if (!jobs[i].active) {
            jobs[i].id = next_job_id++;
            jobs[i].pids = malloc(sizeof(pid_t) * count);
            jobs[i].command = strdup(command);

            if (jobs[i].pids == NULL ||
                jobs[i].command == NULL) {
                free(jobs[i].pids);
                free(jobs[i].command);
                jobs[i].pids = NULL;
                jobs[i].command = NULL;
                return 0;
            }

            memcpy(jobs[i].pids, pids, sizeof(pid_t) * count);
            jobs[i].process_count = count;
            jobs[i].active = 1;
            job_count++;

            printf("[%d] %d\n", jobs[i].id, pids[0]);
            return 1;
        }
    }

    return 0;
}

int launch(char **args, int background) {
    int command_count = 1;

    // Validate pipe syntax and count commands.
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

    char ***commands = malloc(sizeof(char **) * command_count);
    pid_t *pids = malloc(sizeof(pid_t) * command_count);

    if (commands == NULL || pids == NULL) {
        perror("sheldon: malloc");
        free(commands);
        free(pids);
        return 1;
    }

        // Preserve the command before modifying its arguments.
    size_t command_length = 1;

    for (int i = 0; args[i] != NULL; i++) {
        command_length += strlen(args[i]) + 1;
    }

    char *command_text = malloc(command_length);

    if (command_text == NULL) {
        perror("sheldon: malloc");
        return 0;
    }

    command_text[0] = '\0';

    for (int i = 0; args[i] != NULL; i++) {
        if (i > 0) {
            strcat(command_text, " ");
        }

        strcat(command_text, args[i]);
    }

    // Split arguments into individual commands.
    int command_index = 0;
    commands[command_index++] = args;

    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "|") == 0) {
            args[i] = NULL;
            commands[command_index++] = &args[i + 1];
        }
    }

    // Create the required pipes.
    int pipe_count = command_count - 1;
    int (*pipes)[2] = NULL;

    if (pipe_count > 0) {
        pipes = malloc(sizeof(int[2]) * pipe_count);

        if (pipes == NULL) {
            perror("sheldon: malloc");
            free(commands);
            free(pids);
            return 1;
        }

        int created = 0;

        for (int i = 0; i < pipe_count; i++) {
            if (pipe(pipes[i]) < 0) {
                perror("sheldon: pipe");

                for (int j = 0; j < created; j++) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }

                free(pipes);
                free(commands);
                free(pids);
                return 1;
            }

            created++;
        }
    }

    int started = 0;
    int launch_failed = 0;

    // Start one child process per command.
    for (int i = 0; i < command_count; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("sheldon: fork");
            launch_failed = 1;
            break;
        }

        if (pid == 0) {
            // Connect stdin to the previous pipe.
            if (i > 0) {
                if (dup2(pipes[i - 1][0], STDIN_FILENO) < 0) {
                    perror("sheldon: dup2");
                    _exit(EXIT_FAILURE);
                }
            }

            // Connect stdout to the next pipe.
            if (i < command_count - 1) {
                if (dup2(pipes[i][1], STDOUT_FILENO) < 0) {
                    perror("sheldon: dup2");
                    _exit(EXIT_FAILURE);
                }
            }

            // Close all original pipe descriptors.
            for (int j = 0; j < pipe_count; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            execute_command(commands[i]);
        }

        pids[started++] = pid;
    }

    // Parent must close its pipe descriptors so children can receive EOF.
    for (int i = 0; i < pipe_count; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }


    // Background jobs return immediately without waiting.
    if (background && started == command_count && !launch_failed) {
        if (register_job(pids, started, command_text)) {
            free(command_text);
            free(pipes);
            free(commands);
            free(pids);
            return 1;
        }

        // Registration failed; wait for children to avoid leaving zombies.
        launch_failed = 1;
    }

    free(command_text);

    // Wait for foreground processes or clean up after registration failure.
    int last_status = 1;

    for (int i = 0; i < started; i++) {
        int status;
        pid_t result;

        do {
            result = waitpid(pids[i], &status, 0);
        } while (result < 0 && errno == EINTR);

        if (result < 0) {
            perror("sheldon: waitpid");
            launch_failed = 1;
        }
        else if (i == started - 1) {
            last_status = WIFEXITED(status) &&
                          WEXITSTATUS(status) == 0;
        }
    }

    free(pipes);
    free(commands);
    free(pids);

    return (!launch_failed &&
            started == command_count &&
            last_status) ? 1 : 0;
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
        return 0;
    }

    if (chdir(directory) != 0) {
        perror("sheldon");
        return 0;
    }

    return 1;
}

int shell_help(void) {
    printf("Shelly - A simple Unix shell\n\n");
    printf("Built-in commands:\n");
    printf("  cd [directory]   Change directory\n");
    printf("  help             Display this help\n");
    printf("  exit             Exit Shelly\n");
    printf("  jobs             List background jobs\n");
    printf("  wait             Wait for background jobs\n");

    return 1;
}

int is_chain_operator(const char *token) {
    return strcmp(token, ";") == 0 ||
           strcmp(token, "&&") == 0 ||
           strcmp(token, "||") == 0;
}

int execute_segment(char **args) {
    if (args[0] == NULL) {
        return 1;
    }

    int background = 0;
    int count = 0;

    while (args[count] != NULL) {
        count++;
    }

    if (strcmp(args[count - 1], "&") == 0) {
        background = 1;
        args[count - 1] = NULL;
    }

    if (args[0] == NULL) {
        return 1;
    }

    if (strcmp(args[0], "exit") == 0) {
        shell_exit_requested = 1;
        return 1;
    }
    else if (strcmp(args[0], "cd") == 0) {
        if (background) {
            fprintf(stderr,
                    "sheldon: cd cannot run in the background\n");
            return 0;
        }

        return shell_cd(args);
    }
    else if (strcmp(args[0], "help") == 0) {
        return shell_help();
    }
    else if (strcmp(args[0], "jobs") == 0) {
        list_jobs();
        return 1;
    }
    else if (strcmp(args[0], "wait") == 0) {
        return wait_for_jobs();
    }

    return launch(args, background);
}

int execute_chain(char **args) {
    int segment_count = 1;

    // Count operators and validate the chain.
    for (int i = 0; args[i] != NULL; i++) {
        if (is_chain_operator(args[i])) {
            if (i == 0 || args[i + 1] == NULL ||
                is_chain_operator(args[i + 1])) {
                fprintf(stderr,
                        "sheldon: invalid command chain syntax\n");
                return 0;
            }

            segment_count++;
        }
    }

    char ***segments = malloc(sizeof(char **) * segment_count);
    char **operators = NULL;

    if (segment_count > 1) {
        operators = malloc(sizeof(char *) * (segment_count - 1));
    }

    if (segments == NULL ||
        (segment_count > 1 && operators == NULL)) {
        perror("sheldon: malloc");
        free(segments);
        free(operators);
        return 0;
    }

    // Divide the input into individual command segments.
    int segment_index = 0;
    segments[segment_index++] = args;

    for (int i = 0; args[i] != NULL; i++) {
        if (is_chain_operator(args[i])) {
            operators[segment_index - 1] = args[i];
            args[i] = NULL;
            segments[segment_index++] = &args[i + 1];
        }
    }

    // Execute segments according to their operators.
    int status = 1;

    for (int i = 0; i < segment_count; i++) {
        int should_execute = 1;

        if (i > 0) {
            char *op = operators[i - 1];

            if (strcmp(op, "&&") == 0) {
                should_execute = (status == 1);
            }
            else if (strcmp(op, "||") == 0) {
                should_execute = (status == 0);
            }
            // Semicolon always executes the next command.
        }

        if (should_execute && segments[i][0] != NULL) {
            status = execute_segment(segments[i]);
        }

        if (shell_exit_requested) {
            break;
        }
    }

    free(segments);
    free(operators);

    return status;
}

void handle_sigint(int sig) {
    (void)sig;
    write(STDOUT_FILENO, "\n", 1);
}

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

static void redraw_input(const char *prompt, const char *buffer,
                         int length, int cursor) {
    printf("\r\033[2K%s", prompt);

    if (length > 0) {
        fwrite(buffer, 1, (size_t)length, stdout);
    }

    if (cursor < length) {
        printf("\033[%dD", length - cursor);
    }

    fflush(stdout);
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

    char buffer[INPUT_SIZE] = {0};
    char saved_line[INPUT_SIZE] = {0};
    int length = 0;
    int cursor = 0;
    int history_position = history_count;
    int saved_line_valid = 0;

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
            if (*output == NULL) {
                perror("sheldon: strdup");
                return -2;
            }

            save_history(buffer);
            return 1;
        }

        // Ctrl+D: exit only when the input line is empty.
        if (c == 4 && length == 0) {
            tcsetattr(STDIN_FILENO, TCSANOW, &original);
            return 0;
        }

        // Ctrl+A / Ctrl+E: move to beginning / end.
        if (c == 1) {
            cursor = 0;
            redraw_input(prompt, buffer, length, cursor);
            continue;
        }
        if (c == 5) {
            cursor = length;
            redraw_input(prompt, buffer, length, cursor);
            continue;
        }

        // Backspace removes the character immediately before the cursor.
        if (c == 127 || c == 8) {
            if (cursor > 0) {
                memmove(&buffer[cursor - 1], &buffer[cursor],
                        (size_t)(length - cursor + 1));
                cursor--;
                length--;
                redraw_input(prompt, buffer, length, cursor);
            }
            continue;
        }

        // Escape sequences: arrows, Home, End and Delete.
        if (c == '\033') {
            char seq[8];
            size_t seq_length = 0;

            if (read(STDIN_FILENO, &seq[seq_length], 1) != 1) {
                continue;
            }
            seq_length++;

            if (seq[0] == '[') {
                while (seq_length < sizeof(seq) &&
                       read(STDIN_FILENO, &seq[seq_length], 1) == 1) {
                    char last = seq[seq_length++];
                    if ((last >= 'A' && last <= 'Z') || last == '~') {
                        break;
                    }
                }

                char key = seq[seq_length - 1];

                if (key == 'A') {
                    // Up: save the user's current draft before browsing history.
                    if (history_position == history_count && !saved_line_valid) {
                        memcpy(saved_line, buffer, (size_t)length + 1);
                        saved_line_valid = 1;
                    }

                    if (history_position > 0) {
                        history_position--;
                        snprintf(buffer, sizeof(buffer), "%s",
                                 history[history_position]);
                        length = (int)strlen(buffer);
                        cursor = length;
                    }
                }
                else if (key == 'B') {
                    // Down: restore the draft after the newest history entry.
                    if (history_position < history_count) {
                        history_position++;
                    }

                    if (history_position == history_count) {
                        if (saved_line_valid) {
                            memcpy(buffer, saved_line, sizeof(buffer));
                        }
                        else {
                            buffer[0] = '\0';
                        }
                    }
                    else {
                        snprintf(buffer, sizeof(buffer), "%s",
                                 history[history_position]);
                    }

                    length = (int)strlen(buffer);
                    cursor = length;
                    if (history_position == history_count) {
                        saved_line_valid = 0;
                    }
                }
                else if (key == 'C' && cursor < length) {
                    cursor++;
                }
                else if (key == 'D' && cursor > 0) {
                    cursor--;
                }
                else if (key == 'H' ||
                         (seq_length >= 2 && seq[1] == '1' && key == '~')) {
                    cursor = 0;
                }
                else if (key == 'F' ||
                         (seq_length >= 2 && seq[1] == '4' && key == '~')) {
                    cursor = length;
                }
                else if (key == '~' && seq_length >= 2 && seq[1] == '3') {
                    // Delete removes the character under the cursor.
                    if (cursor < length) {
                        memmove(&buffer[cursor], &buffer[cursor + 1],
                                (size_t)(length - cursor));
                        length--;
                    }
                }

                redraw_input(prompt, buffer, length, cursor);
            }

            continue;
        }

        // Insert printable characters at the current cursor position.
        if (isprint((unsigned char)c) && length < INPUT_SIZE - 1) {
            memmove(&buffer[cursor + 1], &buffer[cursor],
                    (size_t)(length - cursor + 1));
            buffer[cursor] = c;
            cursor++;
            length++;
            redraw_input(prompt, buffer, length, cursor);
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


void cleanup_background_jobs(void) {
    // Ask all remaining background processes to terminate gracefully.
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!jobs[i].active) {
            continue;
        }

        for (int j = 0; j < jobs[i].process_count; j++) {
            if (jobs[i].pids[j] > 0 &&
                kill(jobs[i].pids[j], SIGTERM) < 0 &&
                errno != ESRCH) {
                perror("sheldon: kill");
            }
        }
    }

    // Give them up to one second to exit and reap them as they finish.
    for (int attempt = 0; attempt < 10; attempt++) {
        int remaining = 0;

        for (int i = 0; i < MAX_JOBS; i++) {
            if (!jobs[i].active) {
                continue;
            }

            for (int j = 0; j < jobs[i].process_count; j++) {
                pid_t pid = jobs[i].pids[j];

                if (pid <= 0) {
                    continue;
                }

                int status;
                pid_t result = waitpid(pid, &status, WNOHANG);

                if (result == pid ||
                    (result < 0 && errno == ECHILD)) {
                    jobs[i].pids[j] = 0;
                }
                else if (result == 0 ||
                         (result < 0 && errno == EINTR)) {
                    remaining++;
                }
                else if (result < 0) {
                    perror("sheldon: waitpid");
                    jobs[i].pids[j] = 0;
                }
            }
        }

        if (remaining == 0) {
            break;
        }

        struct timespec delay = {0, 100000000L};
        nanosleep(&delay, NULL);
    }

    // Force-stop any process that ignored SIGTERM, then reap it.
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!jobs[i].active) {
            continue;
        }

        for (int j = 0; j < jobs[i].process_count; j++) {
            pid_t pid = jobs[i].pids[j];

            if (pid <= 0) {
                continue;
            }

            if (kill(pid, SIGKILL) < 0 && errno != ESRCH) {
                perror("sheldon: kill");
            }

            int status;
            pid_t result;

            do {
                result = waitpid(pid, &status, 0);
            } while (result < 0 && errno == EINTR);

            if (result < 0 && errno != ECHILD) {
                perror("sheldon: waitpid");
            }

            jobs[i].pids[j] = 0;
        }

        free(jobs[i].pids);
        free(jobs[i].command);

        jobs[i].pids = NULL;
        jobs[i].command = NULL;
        jobs[i].active = 0;
    }

    job_count = 0;
}

void free_tokens(char **tokens, size_t count) {
    for (size_t i = 0; i < count; i++) {
        free(tokens[i]);
    }

    free(tokens);
}

int main() {
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    load_history();

    while (1) {
        reap_background_jobs();

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

        if (args == NULL) {
            continue;
        }

        // Count tokens before command execution modifies the array.
        size_t token_count = 0;

        while (args[token_count] != NULL) {
            token_count++;
        }

        // Preserve the original token pointers.
        char **original_tokens = NULL;

        if (token_count > 0) {
            original_tokens = malloc(token_count * sizeof(char *));

            if (original_tokens == NULL) {
                perror("sheldon: malloc");
                free_tokens(args, token_count);
                continue;
            }

            memcpy(original_tokens, args,
                token_count * sizeof(char *));
        }

        if (args[0] != NULL) {
            execute_chain(args);
        }

        // Free every original token, including operators
        // that execution replaced with NULL.
        for (size_t i = 0; i < token_count; i++) {
            free(original_tokens[i]);
        }

        free(original_tokens);
        free(args);

        if (shell_exit_requested) {
            break;
        }

    }

    cleanup_background_jobs();
    write_history();
    for (int i = 0; i < history_count; i++) {
        free(history[i]);
    }

    return 0;
}