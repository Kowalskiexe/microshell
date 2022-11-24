#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <termios.h>

#define ESC 27
#define ARROW_UP 65
#define ARROW_DOWN 66
#define ARROW_RIGHT 67
#define ARROW_LEFT 68

void print_tcflag(tcflag_t flag) {
    for (int i = sizeof(tcflag_t) * 8 - 1; i >= 0; i--) {
        tcflag_t mask = 1 << i;
        printf("%d", (mask & flag) >> i);
    }
    printf("\n");
}

const int max_word_count = 1000;
const int max_word_length = 1000; // including null terminator

char getchar_unbuffered() {
    // terminal config
    struct termios config;
    tcgetattr(STDIN_FILENO, &config);
    struct termios old_config = config;

    // VTIME - timeout
    config.c_cc[VTIME] = 0; // don't wait
    // VMIN - minimal number of charaters to flush
    config.c_cc[VMIN] = 1; // flush every letter

    // ICANON - canonical mode
    // ECHO - echo input
    config.c_lflag &= ~(ICANON | ECHO);

    // ICRNL - map CR (carret return) to NL (newline)
    config.c_iflag |= ICRNL;

    tcsetattr(STDIN_FILENO, TCSANOW, &config);
    char c = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &old_config);
    return c;
}

// on success returns number of loaded words
int read_input(char *const *buff) {
    int word_count = 0;
    int idx = 0;
    char c;
    do {
        c = getchar_unbuffered();
        //printf("(%d) \rxdxd", c);
        if (c == ESC) {
            getchar_unbuffered(); // consume one character
            char c3 = getchar_unbuffered();
            if (c3 == ARROW_UP)
                printf("UP");
            if (c3 == ARROW_DOWN)
                printf("DOWN");
            if (c3 == ARROW_RIGHT)
                printf("RIGHT");
            if (c3 == ARROW_LEFT)
                printf("LEFT");
        } else {
            printf("%c(%d)", c, c);
        }

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
    printf("exitted yet???\n");
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


        if (strcmp(buff[0], "exit") == 0)
            cmd_exit();
        else if (strcmp(buff[0], "cd") == 0)
            cmd_cd(count, buff);
        else
            execute_command(buff[0], buff, count);

        for (int i = 0; i < max_word_count; i++)
            free(buff[i]);
        free(buff);
    }

    return 0;
}

