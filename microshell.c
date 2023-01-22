#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <termios.h>
#include <locale.h>

#define ESC 27
#define ARROW_UP 65
#define ARROW_DOWN 66
#define ARROW_RIGHT 67
#define ARROW_LEFT 68
#define BACKSPACE 127
#define DELETE 51

#define FLUSH 0
#if FLUSH == 1
#define fflush(stdout); fflush(stdout);
#else
#define fflush(stdout);
#endif


void print_tcflag(tcflag_t flag) {
    for (int i = sizeof(tcflag_t) * 8 - 1; i >= 0; i--) {
        tcflag_t mask = 1 << i;
        printf("%d", (mask & flag) >> i);
    }
    printf("\n");
}

const int max_word_count = 1000;
const int max_word_length = 1000; // including null terminator
const int user_buffer_size = 1000;

int get_terminal_width() {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return w.ws_col;
}

bool _cursor_control = false;
int _x = 0, _y = 0;
char _old_buffer[100000] = {'\0'};
void init_cursor_control() {
    _cursor_control = true;
    _x = 0; _y = 0;
}

void end_cursor_control() {
    _cursor_control = false;
    _old_buffer[0] = '\0';
}

// adjusts x and y as if buff's contents were printed
void buff_shift(const char * const buff, int *x, int *y) {
    int width = get_terminal_width();
    for (int i = 0; i < strlen(buff); i++) {
        if (buff[i] == '\n') {
            *x = 0;
            (*y)++;
        } else {
            (*x)++;
            if (*x == width) {
                *x = 0;
                (*y)++;
            }
        }
    }
}


void ccprintf(const char *const format, ...) {
    if (!_cursor_control) {
        fprintf(stderr, "Error: cursor control is uninitialized\n");
        return;
    }
    va_list args;
    va_start(args, format);
    char buff[100000];
    vsprintf(buff, format, args);
    va_end(args);
    int width = get_terminal_width();
    for (int i = 0; i < strlen(buff); i++) {
        printf("%c", buff[i]);
        fflush(stdout);
        if (_x == width - 1) {
            printf("\n");
            fflush(stdout);
            _x = 0;
            _y++;
        }
        else
            _x++;
    }
}

// moves cursor on screen
void ccmove_cursor(int dx, int dy) {
    if (!_cursor_control) {
        fprintf(stderr, "Error: cursor control is uninitialized\n");
        return;
    }
    // escape codes with 0 still move cursor by one
    if (dx != 0) {
        if (dx > 0)
            printf("\e[%dC", dx); // move cursor right
        else
            printf("\e[%dD", -dx); // move curosr left
    }
    if (dy != 0) {
        if (dy > 0)
            printf("\e[%dB", dy); // move cursor down
        else
            printf("\e[%dA", -dy); // move cursor up
    }
    _x += dx;
    _y += dy;
}

int ccget_x() {
    if (!_cursor_control) {
        fprintf(stderr, "Error: cursor control is uninitialized\n");
        return 0;
    }
    return _x;
}

int ccget_y() {
    if (!_cursor_control) {
        fprintf(stderr, "Error: cursor control is uninitialized\n");
        return 0;
    }
    return _y;
}

// move cursor to 0, 0 (0, 0 is set by calling init_cursor_control())
void ccreset_cursor() {
    ccmove_cursor(-ccget_x(), -ccget_y());
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


char * get_prompt();
void print_buffer(const char * const user_buffer, int pos) {
    // clear
    char *prompt = get_prompt();
    ccreset_cursor();
    fflush(stdout);
    int old_x = 0, old_y = 0;
    buff_shift(_old_buffer, &old_x, &old_y);

    for (int i = 0; i <= old_y; i++) {
        printf("\e[2K"); // erase line (cursor position does not change)
        fflush(stdout);
        // if this isn't the last line
        if (i < old_y) {
            ccmove_cursor(0, 1); // move cursor down
            fflush(stdout);
        }
    }
    fflush(stdout);
    ccreset_cursor();
    fflush(stdout);

    // redraw
    ccprintf("%s", prompt);
    fflush(stdout);
    ccprintf("%s", user_buffer);
    fflush(stdout);
    sprintf(_old_buffer, "%s%s", prompt, user_buffer);

    // move cursor to the correct position
    int width = get_terminal_width();
    pos += strlen(prompt);
    int x = pos % width;
    int y = pos / width;
    ccreset_cursor();
    ccmove_cursor(x, y);
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

void read_input(char * const buff, const int buff_size) {
    char c;
    int pos = 0;
    int length = 0;
    memset(buff, 0, buff_size * sizeof(char));
    init_cursor_control();
    print_buffer(buff, pos);
    do {
        c = getchar_unbuffered();
        switch (c) {
            case ESC:
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
                if (isprint(c)) {
                    insert_character_at(c, buff, pos++);
                    length++;
                }
                break;
        }
        print_buffer(buff, pos);
    } while (c != EOF && c != '\n');
    // move cursor to the end
    print_buffer(buff, length);
    fflush(stdout);
    end_cursor_control();
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
    char *out = malloc(1000 * sizeof(char));
    sprintf(out, "[%s] $", path);
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
    setlocale(LC_ALL, "pl_PL.utf8");
    // main loop
    while (true) {
        char *line = malloc(user_buffer_size * sizeof(char));
        read_input(line, user_buffer_size);
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

