#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

const int max_word_count = 1000;
const int max_word_length = 1000; // including null terminator

// on success returns number of loaded words
int read_input(char *const *buff) {
    int word_count = 0;
    int idx = 0;
    char c;
    do {
        c = getchar();
        if (isspace(c)) {
            // mark word
            if (idx > 0) {
                buff[word_count][idx] = '\0';
                word_count++;
                idx = 0;
            }
        } else {
            buff[word_count][idx] = c;
            idx++;
        }
    } while (c != EOF && c != '\n');
    return word_count;
}

// has side effects, adds NULL at the end of the buff
void execute_command(char *name, char **args, const int args_count) {
    pid_t id = fork();
    if (id == 0) {
        args[args_count] = NULL;
        execvp(name, args);
        printf("Error: %s\n", strerror(errno));
    } else {
        wait(NULL);
    }
}

void prompt() {
    char *path = getcwd(NULL, 0);
    printf("[%s] $", path);
}

void cmd_exit() {
    printf("exit command read, exiting...\n");
    exit(0);
}

void cmd_cd(int count, char **buff) {
     if (count == 1)
         printf("provide path!\n");
     else if(count == 2)
         chdir(buff[1]);
     else
         printf("too many arguments!\n");
}

int main() {
    // main loop
    while (true) {
        prompt();

        char **buff = malloc(max_word_count * sizeof(char*));
        for (int i = 0; i < max_word_count; i++)
            buff[i] = malloc(max_word_length * sizeof(char));

        int count = read_input(buff);


        if (strcmp(buff[0], "exit") == 0) {
            cmd_exit();
        } else if (strcmp(buff[0], "cd") == 0) {
            cmd_cd(count, buff);
        } else {
            execute_command(buff[0], buff, count);
        }

        for (int i = 0; i < max_word_count; i++)
            free(buff[i]);
        free(buff);
    }

    return 0;
}

