#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <termios.h>
#include <locale.h>
#include <math.h>

#define ESC 27
#define ARROW_UP 65
#define ARROW_DOWN 66
#define ARROW_RIGHT 67
#define ARROW_LEFT 68
#define BACKSPACE 127
#define DELETE 51

// flushing after every printf (for debugging)
#define FLUSH 0
#if FLUSH == 1
#define printf(...); printf(__VA_ARGS__);fflush(stdout);
#endif

// Terminal manipulation is done with escape codes:
// https://en.wikipedia.org/wiki/ANSI_escape_code

// SGR (Select Graphic Rendition) parameters
#define RESET "\e[0m"
#define BOLD "\e[1m"
#define ITALIC "\e[3m"
#define UNDERLINE "\e[4m"

// foreground - \e[38;2;r;g;b
#define FG_RED "\e[38;2;255;0;0m"
// #478C5C
#define FG_GREEN "\e[38;2;71;140;92m"
#define FG_BLUE "\e[38;2;0;0;255m"
#define FG_WHITE "\e[38;2;255;255;255m"
#define FG_BLACK "\e[38;2;0;0;0m"
#define FG_YELLOW "\e[38;2;255;255;0m"

// background - \e[38;2;r;g;b
#define BG_RED "\e[48;2;255;0;0m"

// custom
// #6C8197
#define C_PATH "\e[38;2;108;129;151m"
// #ECC667
#define C_PROMPT "\e[38;2;236;198;103m"

// ps command
// #B4F8C8
#define PS_RUNNING "\e[38;2;180;248;200m"
// #A0E7E5
#define PS_IDLE "\e[38;2;160;231;229m"
// #FFAEBC
#define PS_SLEEPING "\e[38;2;255;174;188m"

// strlen but not counting escape codes
int _strlen(const char * const str) {
    int len = 0;
    bool is_escaped = false;
    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\e')
            is_escaped = true;
        if (!is_escaped)
            len++;
        if (is_escaped && str[i] == 'm')
            is_escaped = false;
    }
    return len;
}

void print_tcflag(tcflag_t flag) {
    for (int i = sizeof(tcflag_t) * 8 - 1; i >= 0; i--) {
        tcflag_t mask = 1 << i;
        printf("%d", (mask & flag) >> i);
    }
    printf("\n");
}

const int max_word_count = 100;
const int max_word_length = 1000; // including null terminator
const int user_buffer_size = 1000;

int get_terminal_width() {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return w.ws_col;
}

int max(int a, int b) {
    return a > b ? a : b;
}

int min(int a, int b) {
    return a < b ? a : b;
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
    for (int i = 0; i < _strlen(buff); i++) {
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
        fprintf(stderr, "%sError: cursor control is uninitialized%s, \n", FG_RED, RESET);
        return;
    }
    va_list args;
    va_start(args, format);
    char buff[100000];
    vsprintf(buff, format, args);
    va_end(args);
    int width = get_terminal_width();
    bool is_escaped = false;
    for (int i = 0; i < strlen(buff); i++) {
        printf("%c", buff[i]);
        if (buff[i] == '\e')
            is_escaped = true;
        if (!is_escaped) {
            if (_x == width - 1) {
                printf("\n");
                _x = 0;
                _y++;
            }
            else
                _x++;
        }
        if (is_escaped && buff[i] == 'm')
            is_escaped = false;
    }
}

// moves cursor on screen
void ccmove_cursor(int dx, int dy) {
    if (!_cursor_control) {
        fprintf(stderr, "%sError: cursor control is uninitialized%s\n", FG_RED, RESET);
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
        fprintf(stderr, "%sError: cursor control is uninitialized%s\n", FG_RED, RESET);
        return 0;
    }
    return _x;
}

int ccget_y() {
    if (!_cursor_control) {
        fprintf(stderr, "%sError: cursor control is uninitialized%s\n", FG_RED, RESET);
        return 0;
    }
    return _y;
}

// move cursor to 0, 0 (0, 0 is set by calling init_cursor_control())
void ccreset_cursor() {
    ccmove_cursor(-ccget_x(), -ccget_y());
}

// useful insight: https://en.wikibooks.org/wiki/Serial_Programming/termios
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
    int old_x = 0, old_y = 0;
    buff_shift(_old_buffer, &old_x, &old_y);

    for (int i = 0; i <= old_y; i++) {
        printf("\e[2K"); // erase line (cursor position does not change)
        // if this isn't the last line
        if (i < old_y) {
            ccmove_cursor(0, 1); // move cursor down
        }
    }
    ccreset_cursor();

    // redraw
    ccprintf("%s", prompt);
    ccprintf("%s", user_buffer);
    sprintf(_old_buffer, "%s%s", prompt, user_buffer);

    // move cursor to the correct position
    int width = get_terminal_width();
    pos += _strlen(prompt);
    int x = pos % width;
    int y = pos / width;
    ccreset_cursor();
    ccmove_cursor(x, y);

    free(prompt);
}

void insert_character_at(char c, char *str, int pos) {
    int n = strlen(str);
    if (n > 0) {
        for (int i = n; i >= pos; i--)
            str[i] = str[i - 1];
    }
    str[pos] = c;
}

void remove_character_at(char *str, int pos) {
    int n = strlen(str);
    for (int i = pos; i < n; i++)
        str[i] = str[i + 1];
}

char history[200][1000];
int his_top = 0; // first free slot / length
void read_input(char * const buff, const int buff_size) {
    char c;
    int pos = 0;
    int length = 0;
    memset(buff, 0, buff_size);
    // position in history counting from the end of the array
    int his_cur = -1; // -1 - clean buffer

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
                        // older in history
                        if (his_cur < his_top - 1) {
                            his_cur++;
                            int idx = his_top - 1 - his_cur;
                            // set buffer
                            memset(buff, 0, buff_size);
                            strcpy(buff, history[idx]);
                            length = strlen(buff);
                            pos = min(pos, length);
                        }
                        break;
                    case ARROW_DOWN:
                        // newer in history or clear
                        if (his_cur > -1) {
                            his_cur--;
                            if (his_cur == -1) {
                                // set clean buffer
                                memset(buff, 0, buff_size);
                                length = 0;
                                pos = 0;
                            } else {
                                // set buffer from history
                                int idx = his_top - 1 - his_cur;
                                memset(buff, 0, buff_size);
                                strcpy(buff, history[idx]);
                                length = strlen(buff);
                                pos = min(pos, length);
                            }
                        }
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
                if (length > 0 && pos > 0) {
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
    end_cursor_control();

    // don't add empty input
    if (length > 0)
        strcpy(history[his_top++], buff);
}

// returns number of arguments
int parse_arguments(const char *const line, char **buff) {
    int top = 0;
    int idx = 0;
    const char no_quote = -1;
    char opening_quote = no_quote;
    for (int i = 0; i < strlen(line); i++) {
        if (opening_quote == no_quote && isspace(line[i])) {
            if (idx > 0) {
                buff[top++][idx] = '\0';
                idx = 0;
            }
        } else {
            if (line[i] == '\'' || line[i] == '\"') {
                if (opening_quote == no_quote) {
                    opening_quote = line[i];
                } else {
                    if (opening_quote == line[i])
                        opening_quote = no_quote;
                }
            } else
                buff[top][idx++] = line[i];
        }
    }
    if (idx > 0) {
        buff[top++][idx] = '\0';
    }
    buff[top] = NULL;
    return top;
}

// has side effects, adds NULL at the end of the buff
void execute_command(char *name, char **args, const int args_count) {
    pid_t id = fork();
    if (id == 0) {
        args[args_count] = NULL;
        execvp(name, args);
        printf("%s", FG_RED);
        perror("Error");
        printf("%s", RESET);
        exit(EXIT_FAILURE);
    } else {
        wait(NULL);
    }
}

// returns prompt's content
char *get_prompt() {
    char *path = getcwd(NULL, 0);
    char *out = malloc(1000);
    sprintf(out, "%s[%s]%s %s$%s ", C_PATH, path, RESET, C_PROMPT, RESET);
    free(path);
    return out;
}

void cmd_exit() {
    printf("bye!\n");
    exit(0);
}

char last_cd_location[1000] = {'\0'};
void cmd_cd(int argc, char **argv) {
    if (argc > 2) {
        fprintf(stderr, "%stoo many arguments!%s\n", FG_RED, RESET);
        return;
    }
    char target_location[1000];

    if (argc == 1)
        strcpy(target_location, getenv("HOME"));
    else {
        if (strcmp(argv[1], "-") == 0) {
            if (last_cd_location[0] == '\0')
                return;
            strcpy(target_location, last_cd_location);
        } else if (strcmp(argv[1], "~") == 0)
            strcpy(target_location, getenv("HOME"));
        else
            strcpy(target_location, argv[1]);
    }
    char tmp[1000];
    strcpy(tmp, getcwd(NULL, 0));
    int ret = chdir(target_location);
    if (ret == -1)
        fprintf(stderr, "%scd: The directory \"%s\" does not exist%s\n", FG_RED, target_location, RESET);
    else
        // update last_cd_location only on success
        strcpy(last_cd_location, tmp);
}

void cmd_type(int argc, char **argv) {
    if (argc == 1) {
        fprintf(stderr, "%sname a command!%s\n", FG_RED, RESET);
        return;
    }
    if (argc > 2) {
        fprintf(stderr, "%stoo many arguments!%s\n", FG_RED, RESET);
        return;
    }
    // argc == 2
    bool builtin = false;
    builtin |= strcmp(argv[1], "help") == 0;
    builtin |= strcmp(argv[1], "exit") == 0;
    builtin |= strcmp(argv[1], "type") == 0;
    builtin |= strcmp(argv[1], "calc") == 0;
    builtin |= strcmp(argv[1], "cd") == 0;
    builtin |= strcmp(argv[1], "ps") == 0;
    builtin |= strcmp(argv[1], "args") == 0;
    if (builtin)
        printf("builtin\n");
    else
        printf("external\n");
}

// for testing parsing
void cmd_args(int argc, char **argv) {
    printf("%d args:\n", argc);
    for (int i = 0; i < argc; i++)
        printf("%s\n", argv[i]);
}

void cmd_help() {
    printf("%smicroshell%s by Maciej Kowalski (481828), avaible commands:\n", BOLD, RESET);
    printf("  %shelp%s - see this list of avaible commands\n", ITALIC, RESET);
    printf("  %sexit%s - exit microshell\n", ITALIC, RESET);
    printf("  %stype%s - see if command is external or a bulitin\n", ITALIC, RESET);
    printf("  %scalc%s - evaluate an arithmetic expression (dodatkowa komenda powłoki #1)\n", ITALIC, RESET);
    printf("    %scd%s - change working directory\n", ITALIC, RESET);
    printf("    %sps%s - list running processes (dodatkowa komenda powłoki #2)\n", ITALIC, RESET);
    printf("%sbajery:%s\n", BOLD, RESET);
    printf("* pełna obsługa strzałek\n");
    printf("* historia poleceń\n");
    printf("* obsługa argumentów w cudzysłowach\n");
    printf("* kolorowanie terminala\n");
}

bool is_numeric(char *str) {
    for (int i = 0; i < strlen(str); i++) {
        if (!isdigit(str[i]))
            return false;
    }
    return true;
}

void extract(const char * const content, const char * const field, char * outbuff) {
    char *start = strstr(content, field);
    if (start == NULL) {
        fprintf(stderr, "%serror 1%s\n", FG_RED, RESET);
        exit(1);
    }
    start += strlen(field);
    char *end = strstr(start, "\n") - 1;
    if (end == NULL) {
        fprintf(stderr, "%serror 2%s\n", FG_RED, RESET);
        exit(2);
    }
    int length = end - start + 1;
    memcpy(outbuff, start, length);
    outbuff[length] = '\0';
}

struct PSTable {
    int length;
    char ***content;
};

void init_ps_table(struct PSTable *tab) {
    tab->length = 0;
    tab->content = malloc(4 * sizeof(char **));
    for (int i = 0; i < 4; i++) {
        tab->content[i] = malloc(1000 * sizeof(char *));
        for (int j = 0; j < 1000; j++)
            tab->content[i][j] = malloc(200);
    }
}

void append_ps_table(struct PSTable *tab, char *col0, char *col1, char *col2, char *col3) {
    strcpy(tab->content[0][tab->length], col0);
    strcpy(tab->content[1][tab->length], col1);
    strcpy(tab->content[2][tab->length], col2);
    strcpy(tab->content[3][tab->length], col3);
    tab->length++;
}

int longest_len(char *column[], int n) {
    int out = 0;
    for (int i = 0; i < n; i++) {
        int len = strlen(column[i]);
        if (len > out)
            out = len;
    }
    return out;
}

void print_ps_table(struct PSTable *tab) {
    // column lengths
    int col_w0 = longest_len(tab->content[0], tab->length);
    int col_w1 = longest_len(tab->content[1], tab->length);
    int col_w2 = longest_len(tab->content[2], tab->length);
    int col_w3 = longest_len(tab->content[3], tab->length);
    for (int i = 0; i < tab->length; i++) {
        char color_es[50]; // color escape code
        strcpy(color_es, RESET);
        // skip header
        if (i > 0) {
            // get first letter of STATE column
            char state = tab->content[3][i][0];
            // possible values: R, I, S
            // R - running
            // I - idle
            // S - sleeping
            switch (state) {
                case 'R':
                    strcpy(color_es, PS_RUNNING);
                    break;
                case 'I':
                    strcpy(color_es, PS_IDLE);
                    break;
                case 'S':
                    strcpy(color_es, PS_SLEEPING);
                    break;
            }
        }
        char format[1000];
        sprintf(format, "%%%ds  %%%ds  %%-%ds  %s%%-%ds%s\n",
                col_w0, col_w1, col_w2, color_es, col_w3, RESET);
        printf(format,
                tab->content[0][i],
                tab->content[1][i],
                tab->content[2][i],
                tab->content[3][i]);
    }
}

void free_ps_table(struct PSTable *tab) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 1000; j++)
            free(tab->content[i][j]);
        free(tab->content[i]);
    }
    free(tab->content);
}

void cmd_ps() {
    DIR *proc_dir = opendir("/proc");
    struct dirent *entry;
    struct PSTable tab;
    init_ps_table(&tab);
    append_ps_table(&tab, "PID", "PPID", "NAME", "STATE");
    while ((entry = readdir(proc_dir)) != NULL) {
        if (is_numeric(entry->d_name)) {
            char status_path[1000];
            sprintf(status_path, "/proc/%s/status", entry->d_name);
            int status_fd = open(status_path, O_RDONLY);

            char file_content[1000];
            read(status_fd, file_content, 1000);
            close(status_fd);

            char name[1000];
            extract(file_content, "Name:\t", name);
            char pid[1000];
            extract(file_content, "Pid:\t", pid);
            char ppid[1000];
            extract(file_content, "PPid:\t", ppid);
            char state[1000];
            extract(file_content, "State:\t", state);

            append_ps_table(&tab, pid, ppid, name, state);
        }
    }
    print_ps_table(&tab);
    free_ps_table(&tab);
    closedir(proc_dir);
}

struct MathToken {
    double value;
    char operation; // '+', '-', '*', '/', '^', '\0' - for value tokens
    int nesting_level;
};

int operation_priority(struct MathToken *token) {
    int priority = token->nesting_level * 3;
    if (token->operation == '+' || token->operation == '-')
        priority += 0;
    if (token->operation == '*' || token->operation == '/')
        priority += 1;
    if (token->operation == '^')
        priority += 2;
    return priority;
}

void push_value_token(struct MathToken *tokens, int *size, double value, int nesting_level) {
    tokens[*size].operation = '\0';
    tokens[*size].value = value;
    tokens[*size].nesting_level = nesting_level;
    (*size)++;
}

void push_operation_token(struct MathToken *tokens, int *size, char operation, int nesting_level) {
    tokens[*size].operation = operation;
    tokens[*size].value = -2137.;
    tokens[*size].nesting_level = nesting_level;
    (*size)++;
}

void print_token(const struct MathToken * const token) {
    if (token->operation == '\0')
        printf("%10f  %d\n", token->value, token->nesting_level);
    else
        printf("%10c  %d\n", token->operation, token->nesting_level);
}

void print_tokens(struct MathToken *tokens, int size) {
    printf("%10s  %s\n", "value", "nesting level");
    for (int i = 0; i < size; i++)
        print_token(tokens + i);
}

// [from, to] inclusive
void replace_tokens(struct MathToken *tokens, int *size, int from, int to, const struct MathToken * const new_token) {
    int removed_count = to - from + 1;
    int shift = removed_count - 1;
    // shift tokens [to + 1, size - 1] by `shift` to left
    for (int i = to + 1; i < *size; i++)
        memcpy(tokens + i - shift, tokens + i, sizeof(struct MathToken));
    // insert copy of new token into correct position
    memcpy(tokens + from, new_token, sizeof(struct MathToken));

    // adjuest size
    *size -= removed_count;
    (*size)++; // one new was added
}


// isdigit extened by '.' for parsing floating point numbers
bool isdigitx(char c) {
    return isdigit(c) || c == '.';
}

bool is_digits_buffer_zero(const char * const db) {
    for (int i = 0; db[i] != '\0'; i++) {
        if (db[i] != '0' && db[i] != '.')
            return false;
    }
    return true;
}

// returns true on success, false on failure
bool parse_digits(char *db, double *out) {
    int dot_pos = -1;
    int length = 0; // number of digits
    for (int i = 0; db[i] != '\0'; i++) {
        if (db[i] == '.') {
            if (dot_pos == -1)
                dot_pos = i;
            else {
                fprintf(stderr, "%sError: Multiple dots in parsed string%s\n", FG_RED, RESET);
                return false;
            }
        } else
            length++;
    }
    if (dot_pos == -1)
        dot_pos = length;
    // dot_pos = (number of digits before the dot)
    // length - dot_pos = (number of digits after the dot)
    double num = 0;
    double power = 1;
    for (int i = dot_pos - 1; i >= 0; i--) {
        num += (db[i] - '0') * power;
        power *= 10;
    }
    power = .1;
    for (int i = dot_pos + 1; i <= length; i++) {
        num += (db[i] - '0') * power;
        power /= 10;
    }
    *out = num;
    return true;
}

// returns true on success, false on failure
bool perform_token2(struct MathToken *tokens, int *size, int oper_idx, double (*f)(double, double)) {
    if (oper_idx == 0 || oper_idx == *size - 1) {
        fprintf(stderr, "%sError #1: missing operand%s\n", FG_RED, RESET);
        return false;
    }
    struct MathToken *lop = &tokens[oper_idx - 1]; // left operand
    struct MathToken *rop = &tokens[oper_idx + 1]; // right operand
    if (lop->nesting_level != tokens[oper_idx].nesting_level ||
            rop->nesting_level != tokens[oper_idx].nesting_level) {
        fprintf(stderr, "%sError #2: wrong nesting level of operands%s\n", FG_RED, RESET);
        return false;
    }
    if (lop->operation != '\0' || rop->operation != '\0') {
        fprintf(stderr, "%sError #3: wrong operands%s\n", FG_RED, RESET);
        return false;
    }
    // replace tokens oper_idx-1, oper_idx, oper_idx+1 with new result token
    struct MathToken result;
    result.value = f(lop->value, rop->value);
    result.operation = '\0';
    // set nesting level of result to the highest nesting level of neigboring tokens
    // After addition those tokens are token[oper_idx - 2] and token[oper_idx + 2].
    int left_nl = 0;
    if (oper_idx - 2 >= 0)
        left_nl = tokens[oper_idx - 2].nesting_level;
    int right_nl = 0;
    if (oper_idx + 2 < *size)
        left_nl = tokens[oper_idx + 2].nesting_level;
    int nl = max(left_nl, right_nl);
    result.nesting_level = nl;
    replace_tokens(tokens, size, oper_idx - 1, oper_idx + 1, &result);
    return true;
}

// returns true on success, false on failure
bool perform_token1(struct MathToken *tokens, int *size, int oper_idx, double (*f)(double)) {
    // make sure there is a token on the right
    if (oper_idx == *size - 1) {
        fprintf(stderr, "%sError #1: missing operand%s\n", FG_RED, RESET);
        return false;
    }
    // make sure token on the right is a number with same nesting level
    if (tokens[oper_idx + 1].operation != '\0' ||
        tokens[oper_idx + 1].nesting_level != tokens[oper_idx].nesting_level) {
        fprintf(stderr, "%sError #2: wrong operand%s\n", FG_RED, RESET);
        return false;
    }
    struct MathToken *rop = &tokens[oper_idx + 1]; // right operand
    // replace tokens oper_idx, oper_idx, oper_idx+1 with new result token
    struct MathToken result;
    result.value = f(rop->value);
    result.operation = '\0';
    // set nesting level of result to the highest nesting level of neigboring tokens
    // After addition those tokens are token[oper_idx - 2] and token[oper_idx + 2].
    int left_nl = 0;
    if (oper_idx - 2 >= 0)
        left_nl = tokens[oper_idx - 2].nesting_level;
    int right_nl = 0;
    if (oper_idx + 2 < *size)
        left_nl = tokens[oper_idx + 2].nesting_level;
    int nl = max(left_nl, right_nl);
    result.nesting_level = nl;
    replace_tokens(tokens, size, oper_idx, oper_idx + 1, &result);
    return true;
}

double op_addition(double a, double b) {
    return a + b;
}

double op_subtraction(double a, double b) {
    return a - b;
}

double op_opposite(double a) {
    return -a;
}

double op_multiplication(double a, double b) {
    return a * b;
}

double op_division(double a, double b) {
    return a / b;
}

double op_exponentiation(double a, double b) {
    return pow(a, b);
}

void cmd_calc(int argc, char **argv) {
    if (argc == 1) {
        fprintf(stderr, "%sprovide expression, e.g. (2 + 2) * 8%s\n", FG_RED, RESET);
        printf("supported operations:\n");
        printf("  + - addtion\n");
        printf("  - - subtraction\n");
        printf("  * - multiplication\n");
        printf("  / - division\n");
        printf("  ^ - exponentiation\n");
        return;
    }
    // merge argv into expression
    char expression[1000];
    int idx = 0;
    for (int i = 1; i < argc; i++) {
        strcpy(expression + idx, argv[i]);
        idx += strlen(argv[i]);
    }
    // remove whitespace
    int lgc = -1; // index of last graphic character
    for (int i = 0; i < strlen(expression); i++) {
        if (isgraph(expression[i]))
            expression[++lgc] = expression[i];
    }
    // replace ',' with '.'
    for (int i = 0; i < strlen(expression); i++) {
        if (expression[i] == ',')
            expression[i] = '.';
    }
    // check characters
    for (int i = 0; i < strlen(expression); i++) {
        if (!isdigit(expression[i]) &&
                expression[i] != '+' &&
                expression[i] != '-' &&
                expression[i] != '*' &&
                expression[i] != '/' &&
                expression[i] != '^' &&
                expression[i] != '(' &&
                expression[i] != ')' &&
                expression[i] != '.') {
            fprintf(stderr, "%sinvalid character %c\n", FG_RED, expression[i]);
            fprintf(stderr, "%s\n", expression);
            for(int j = 0; j < i; j++)
                fprintf(stderr, " ");
            fprintf(stderr, "^%s\n", RESET);
            return;
        }
    }
    // check parenthesis
    int open_count = 0;
    for (int i = 0; i < strlen(expression); i++) {
        if (expression[i] == '(')
            open_count++;
        if (expression[i] == ')')
            open_count--;
        if (open_count < 0) {
            fprintf(stderr, "%smissing opening bracket\n", FG_RED);
            fprintf(stderr, "%s\n", expression);
            for (int j = 0; j < i; j++)
                fprintf(stderr, " ");
            fprintf(stderr, "^%s\n", RESET);
            return;
        }
    }
    if (open_count > 0) {
        fprintf(stderr, "%sError: missing closing bracket%s\n", FG_RED, RESET);
        return;
    }
    printf("%s = ?\n", expression);
    // tokenize
    struct MathToken tokens[1000];
    int tokens_size = 0;
    int nesting_level = 0;
    char digits_buffer[1000];
    memset(digits_buffer, 0, 1000);
    int db_idx = 0;
    for (int i = 0; i < strlen(expression); i++) {
        if (isdigitx(expression[i])) {
            digits_buffer[db_idx++] = expression[i];
        } else {
            if (db_idx > 0) {
                double num = 0;
                bool succ = parse_digits(digits_buffer, &num);
                if (!succ) {
                    fprintf(stderr, "%sError: couldn't parse %s%s\n", digits_buffer, FG_RED, RESET);
                    return;
                }
                memset(digits_buffer, 0, 1000);
                db_idx = 0;
                push_value_token(tokens, &tokens_size, num, nesting_level);
            }
            if (expression[i] == '(')
                nesting_level++;
            else if (expression[i] == ')')
                nesting_level--;
            else
                push_operation_token(tokens, &tokens_size, expression[i], nesting_level);
        }
    }
    if (db_idx > 0) {
        double num = 0;
        bool succ = parse_digits(digits_buffer, &num);
        if (!succ) {
            fprintf(stderr, "%sError: couldn't parse %s%s\n", digits_buffer, FG_RED, RESET);
            return;
        }
        memset(digits_buffer, 0, 1000);
        db_idx = 0;
        push_value_token(tokens, &tokens_size, num, nesting_level);
    }
    // evaluate expression
    while (tokens_size > 1) {
        print_tokens(tokens, tokens_size);

        // find operation with highest priority
        int highest_priority = -1;
        int oper_idx = -1;
        for (int i = 0; i < tokens_size; i++) {
            if (tokens[i].operation != '\0') {
                int op = operation_priority(&tokens[i]);
                if (op > highest_priority) {
                    highest_priority = op;
                    oper_idx = i;
                }
            }

        }
        if (oper_idx == -1) {
            fprintf(stderr, "%sError: no operations%s\n", FG_RED, RESET);
            return;
        }
        // perform operation
        bool succ = false;
        switch (tokens[oper_idx].operation) {
            case '+': {
                succ = perform_token2(tokens, &tokens_size, oper_idx, op_addition);
                break;
            }
            case '-': {
                // is token on left a number
                if (tokens[oper_idx - 1].operation == '\0' &&
                    tokens[oper_idx - 1].nesting_level == tokens[oper_idx].nesting_level)
                    succ = perform_token2(tokens, &tokens_size, oper_idx, op_subtraction);
                else
                    succ = perform_token1(tokens, &tokens_size, oper_idx, op_opposite);
                break;
            }
            case '*': {
                succ = perform_token2(tokens, &tokens_size, oper_idx, op_multiplication);
                break;
            }
            case '/': {
                succ = perform_token2(tokens, &tokens_size, oper_idx, op_division);
                break;
            }
            case '^': {
                succ = perform_token2(tokens, &tokens_size, oper_idx, op_exponentiation);
                break;
            }
        }
        if (!succ) {
            fprintf(stderr, "%sError: operation failed%s\n", FG_RED, RESET);
            return;
        }
        printf("\n");
    }
    // print results
    printf("%s%f%s\n", FG_GREEN, tokens[0].value, RESET);
}

void free_args(char **args) {
    for (int i = 0; i < max_word_count; i++)
        free(args[i]);
    free(args);
}

int main() {
    setlocale(LC_ALL, "en_EN.utf8");
    // main loop
    while (true) {
        char *line = malloc(user_buffer_size);
        read_input(line, user_buffer_size);
        printf("\n");

        char **args = malloc(max_word_count * sizeof(char*));
        for (int i = 0; i < max_word_count; i++)
            args[i] = malloc(max_word_length);
        int count = parse_arguments(line, args);
        free(line);

        if (args[0] == NULL)
            ; // skip
        else if (strcmp(args[0], "exit") == 0) {
            free_args(args);
            cmd_exit();
        } else if (strcmp(args[0], "cd") == 0)
            cmd_cd(count, args);
        else if (strcmp(args[0], "type") == 0)
            cmd_type(count, args);
        else if (strcmp(args[0], "args") == 0)
            cmd_args(count, args);
        else if (strcmp(args[0], "help") == 0)
            cmd_help();
        else if (strcmp(args[0], "ps") == 0)
            cmd_ps();
        else if (strcmp(args[0], "calc") == 0)
            cmd_calc(count, args);
        else
            execute_command(args[0], args, count);

        free_args(args);
    }
    return 0;
}

