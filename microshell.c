#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <termios.h>

#define ESC 27
#define ARROW_UP 65
#define ARROW_DOWN 66
#define ARROW_RIGHT 67
#define ARROW_LEFT 68
#define BACKSPACE 127
#define DELETE 51

void print_tcflag(tcflag_t flag) {
    for (int i = sizeof(tcflag_t) * 8 - 1; i >= 0; i--) {
        tcflag_t mask = 1 << i;
        printf("%d", (mask & flag) >> i);
    }
    printf("\n");
}

const int max_word_count = 1000;
const int max_word_length = 1000; // including null terminator

int get_terminal_width() {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return w.ws_col;
}

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
    // ECHONL - echo newline
    config.c_lflag &= ~(ICANON | ECHO | ECHONL);

    // ICRNL - map CR (carret return) to NL (newline)
    config.c_iflag |= ICRNL;

    tcsetattr(STDIN_FILENO, TCSANOW, &config);
    char c = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &old_config);
    return c;
}

void prompt();
void print_line(const char * const line, const int pos) {
    // TODO: wrap lines longer than screen size
    // clear line
    printf("\r");
    int width = get_terminal_width();
    for (int i = 0; i < width; i++)
        printf(" ");
    printf("\r");
    prompt();
    printf("%s", line);
    char left_side[1000];
    strncpy(left_side, line, pos);
    left_side[pos] = '\0';
    printf("\r");
    prompt();
    printf("%s", left_side);
}

void insert_character_at(char c, char *str, int pos) {
    int n = strlen(str);
    for (int i = n; i >= pos; i--)
        str[i] = str[i - 1];
    str[pos] = c;
}

void remove_character_at(char *str, int pos) {
    int n = strlen(str);
    for (int i = pos; i < n; i++)
        str[i] = str[i + 1];
}

void read_input(char *const buff, int buff_size) {
    char c;
    int pos = 0;
    int length = 0;
    memset(buff, 0, buff_size * sizeof(char));
    do {
        c = getchar_unbuffered();
        switch (c) {
            case ESC:
                //printf("ESC\n");
                getchar_unbuffered(); // consume one character
                char c3 = getchar_unbuffered();
                switch (c3) {
                    case ARROW_UP:
                        printf("UP\n");
                          break;
                    case ARROW_DOWN:
                        printf("DOWN\n");
                        break;
                    case ARROW_RIGHT:
                        if (pos < length)
                            pos++;
                        break;
                    case ARROW_LEFT:
                        if (pos > 0)
                            pos--;
                        break;
                    case DELETE:
                        if (pos < length && length > 0) {
                            remove_character_at(buff, pos);
                            length--;
                        }
                        getchar_unbuffered(); // consume one character
                        break;
                }
                break;
            case BACKSPACE:
                if (length > 0) {
                    remove_character_at(buff, pos - 1);
                    pos--;
                    length--;
                }
                break;
            default:
                // add charater to buffer
                if (c != '\n') {
                    insert_character_at(c, buff, pos++);
                    length++;
                }
                break;
        }
        print_line(buff, pos);
    } while (c != EOF && c != '\n');
}

// returns number of arguments
int parse_arguments(const char *const line, char **buff) {
    int top = 0;
    int idx = 0;
    for (int i = 0; i < strlen(line); i++) {
        if (isspace(line[i])) {
            if (idx > 0) {
                buff[top++][idx] = '\0';
                idx = 0;
            }
        } else {
            buff[top][idx] = line[i];
            idx++;
        }
    }
    // make sure the last argument ends with a null terminator
    buff[top][idx] = '\0';
    return top + 1;
}

// has side effects, adds NULL at the end of the buff
void execute_command(char *name, char **args, const int args_count) {
    pid_t id = fork();
    if (id == 0) {
        args[args_count] = NULL;
        execvp(name, args);
        perror("Error");
        exit(EXIT_FAILURE);
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

        char *line = malloc(1000 * sizeof(char));
        read_input(line, 1000);
        printf("\n");

        char **args = malloc(max_word_count * sizeof(char*));
        for (int i = 0; i < max_word_count; i++)
            args[i] = malloc(max_word_length * sizeof(char));
        int count = parse_arguments(line, args);

        if (strcmp(args[0], "exit") == 0)
            cmd_exit();
        else if (strcmp(args[0], "cd") == 0)
            cmd_cd(count, args);
        else
            execute_command(args[0], args, count);

        free(line);
        for (int i = 0; i < max_word_count; i++)
            free(args[i]);
        free(args);
    }
    return 0;
}

