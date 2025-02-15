#define STB_DS_IMPLEMENTATION
#include <generate.c>
#include <generate.h>
#include <stdio.h>
#include <stdlib.h>
#include <tokenize.c>
#include <tokenize.h>

int main(int argc, char **argv) {
    const char *filenameIn = "D:/Documents/C/Compiler/in.sao";
    const char *filenameOut = "D:/Documents/C/Compiler/out.ll";
    FILE *file = fopen(filenameIn, "r");
    if (file == NULL) {
        fprintf(stderr, "Error opening input file %s\n", filenameIn);
        return 1;
    }

    struct Token token = {NULL_TOKEN, NULL, 0, 0};
    int e = tokenize(&token, file);
    if (e) {
        // return e;
    }

    fclose(file);

    printToken(&token);
    printf("\n");

    e = generate(&token, filenameOut, filenameIn);
    if (e) {
        // return e;
    }

    getc(stdin);
    return 0;
}
