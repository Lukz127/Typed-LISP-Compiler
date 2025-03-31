// This project uses stb_image.h under the MIT License.
#define STB_DS_IMPLEMENTATION
#include <generate.c>
#include <generate.h>
#include <outline.c>
#include <stdio.h>
#include <stdlib.h>
#include <tokenize.c>
#include <tokenize.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Error: invalid input\nusage: tlc < filename "
                        ">\nexample : tlc ./main.tlisp\n");
        return 1;
    }
    const char *filenameIn = argv[1];
    // const char *filenameIn = "../examples/raylib-flappy-bird/main.tlisp";
    if (strcmp(filenameIn, "--help") == 0 || strcmp(filenameIn, "-h") == 0) {
        printf("tlc - Typed LISP Compiler\nusage: tlc <filename>\nexample: tlc "
               "./main.tlisp");
        return 0;
    }
    size_t filenameInLen = strlen(filenameIn);
    if (filenameInLen < 7 || filenameIn[filenameInLen - 5] != 't' ||
        filenameIn[filenameInLen - 4] != 'l' ||
        filenameIn[filenameInLen - 3] != 'i' ||
        filenameIn[filenameInLen - 2] != 's' ||
        filenameIn[filenameInLen - 1] != 'p') {
        fprintf(stderr, "Error: invalid file \"%s\"\n", filenameIn);
        return 1;
    }
    char *absFilenameIn = calloc(FILENAME_MAX, sizeof(char));
    if (toAbsolutePath(filenameIn, absFilenameIn, FILENAME_MAX) == NULL) {
        fprintf(stderr, "Error: file \"%s\" doesn't exist\n", filenameIn);
        return 1;
    }
    filenameInLen = strlen(absFilenameIn);
    char *filenameOut = malloc((filenameInLen - 2) * sizeof(char));
    memcpy(filenameOut, absFilenameIn, filenameInLen - 3);
    filenameOut[filenameInLen - 5] = 'l';
    filenameOut[filenameInLen - 4] = 'l';
    filenameOut[filenameInLen - 3] = '\0';

    FILE *file = fopen(absFilenameIn, "r");

    if (file == NULL) {
        fprintf(stderr, "Error opening input file \"%s\"\n", absFilenameIn);
        return 1;
    }

    struct Token token = {NULL_TOKEN, NULL, 0, 0};
    int e = tokenize(&token, file);
    if (e) {
        return e;
    }
    // printToken(&token);

    fclose(file);

    e = generate(&token, filenameOut, absFilenameIn, "main");
    if (e) {
        return e;
    }

    return 0;
}
