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

void move_cursor_by(int x, int y) {
    // escape codes with 0 still move cursor by one
    if (x != 0) {
        if (x > 0)
            printf("\e[%dC", x); // move cursor right
        else
            printf("\e[%dD", -x); // move curosr left
    }
    if (y != 0) {
        if (y > 0)
            printf("\e[%dB", y); // move cursor down
        else
            printf("\e[%dA", -y); // move cursor up
    }
}

char * get_prompt();
// FIXME: ARBITRALNE PORUSZANIE KURSOREM JEST BARDZO TRUDNE
void print_buffer(const char * const buffer, const int old_buffer_length, int pos) {
    // clear
    char *prompt = get_prompt();
    int old_total_length = strlen(prompt) + old_buffer_length;
    int width = get_terminal_width();

    int old_x = (old_total_length - 1) % width + 1; // counting from 1
    int old_y = (old_total_length  - 1) / width + 1; // counting from 1

    pos += strlen(prompt);
    // target cursor position
    int cursor_x = (pos - 1) % width + 1; // counting from 1
    int cursor_y = (pos - 1) / width + 1; // counting from 1

    int diff_x = old_x - cursor_x + 1;
    int diff_y = old_y - cursor_y;
    move_cursor_by(-cursor_x, diff_y);
    fflush(stdout);

   for (int i = 0; i < old_y; i++) {
        printf("\e[2K"); // erase line (cursor position does not change)
        fflush(stdout);
        printf("\e[1A"); // move cursor up
        fflush(stdout);
    }
    printf("\e[1B"); // move cursor down
    fflush(stdout);
    //printf("\e[%dD", old_x); // move cursor left (to the edge of the screen)
    //fflush(stdout);

    // redraw
    printf("%s", prompt);
    fflush(stdout);
    printf("%s", buffer);
    fflush(stdout);

    // move cursor to the correct position
    int total_length = strlen(prompt) + strlen(buffer);
    // assuming cursor is at the end of the buffer
    int current_x = (total_length - 1) % width + 1; // counting from 1
    int current_y = (total_length  - 1) / width + 1; // counting from 1
    int x_diff = cursor_x - current_x;
    int y_diff = cursor_y - current_y;
    move_cursor_by(x_diff, y_diff);
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
    get_prompt();
    char c;
    int pos = 0;
    int length = 0;
    int old_length = 0;
    memset(buff, 0, buff_size * sizeof(char));
    print_buffer(buff, old_length, pos);
    do {
        c = getchar_unbuffered();
        switch (c) {
            case ESC:
                //printf("ESC");
                getchar_unbuffered(); // consume one character
                char c3 = getchar_unbuffered();
                switch (c3) {
                    case ARROW_UP:
                        //printf("UP");
                        break;
                    case ARROW_DOWN:
                        //printf("DOWN");
                        break;
                    case ARROW_RIGHT:
                        if (pos < length) {
                            //printf("\e[1C");
                            pos++;
                        }
                        break;
                    case ARROW_LEFT:
                        if (pos > 0) {
                            //printf("\e[1D");
                            pos--;
                        }
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
                // TODO: add only printable characters ??
                if (c != '\n') {
                    insert_character_at(c, buff, pos++);
                    length++;
                    //printf("%c", c);
                }
                break;
        }
        print_buffer(buff, old_length, pos);
        old_length = length;
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

// returns prompt's content
char *get_prompt() {
    char *path = getcwd(NULL, 0);
    //printf("[%s] $", path);
    char *out = malloc(1000 * sizeof(char));
    sprintf(out, "[%s] $", path);
    //printf("%s", out);
    return out;
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
        //get_prompt();

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

