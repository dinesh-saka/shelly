#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

char **split_line(char *line) {
    int bufsize = 64;
    int position = 0;

    char **tokens = malloc(bufsize * sizeof(char *));

    if (tokens == NULL) {
        fprintf(stderr, "Allocation error\n");
        exit(EXIT_FAILURE);
    }

    char *token = strtok(line, " \t\r\n\a");

    while (token != NULL) {
        tokens[position] = token;
        position++;

        if (position >= bufsize) {
            bufsize *= 2;
            char **new_tokens = realloc(tokens, bufsize * sizeof(char *));

            if (new_tokens == NULL) {
		free(tokens);
                fprintf(stderr, "Allocation error\n");
                exit(EXIT_FAILURE);
            }

	    tokens = new_tokens;
        }

        token = strtok(NULL, " \t\r\n\a");
    }

    tokens[position] = NULL;

    return tokens;
}

int launch(char **args) {
	pid_t pid;
	int status;

	pid = fork();

	if (pid == 0) {
		execvp(args[0], args);
		perror("sheldon");
		exit(EXIT_FAILURE);
	}
	else if (pid<0) {
		perror("fork");
	}
	else {
		waitpid(pid, &status, 0);
	}
	return 1;
}

int shell_cd(char **args) {
    if (args[1] == NULL) {
        fprintf(stderr, "sheldon: expected directory\n");
    }
    else if (chdir(args[1]) != 0) {
        perror("sheldon");
    }

    return 1;

}

int main() {
    char *line = NULL;
    size_t bufsize = 0;

    while (1) {
        printf("sheldon> ");
        fflush(stdout);

        if (getline(&line, &bufsize, stdin) == -1) {
            break;
        }

        char **args = split_line(line);

        if (args[0] != NULL) {
            if (strcmp(args[0], "cd") == 0) {
	        shell_cd(args);
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
