#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

const int max_word_count = 1000;
const int max_word_length = 1000; // including null terminator

// on success returns number of loaded words
int read_input(char *const* buff) {
    int word_count = 0;
    int idx = 0;
    char c;
    do {
        c = getchar();
        if (isspace(c)) {
            // mark word
            if (idx > 0) {
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

int main() {
    char **buff = malloc(max_word_count * sizeof(char*));
    for (int i = 0; i < max_word_count; i++)
        buff[i] = malloc(max_word_length * sizeof(char));

    int count = read_input(buff);
    printf("%d words\n", count);
    for (int i = 0; i < count; i++)
        printf("%s\n", buff[i]);

    return 0;
}

