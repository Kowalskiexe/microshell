#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const int max_word_count = 1000;
const int max_word_length = 1000; // including null terminator

int read_input(char *const* buff) {
    int word_count = 0;
    do {
        scanf(" %s", buff[word_count]);
        word_count++;
    } while (strcmp(buff[word_count - 1], ""));
    return word_count - 1;
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

