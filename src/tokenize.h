#pragma once
#include <stb_ds.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

typedef enum TokenType {
    INT_TOKEN,
    FLOAT_TOKEN,
    STRING_TOKEN,
    IDENT_TOKEN,
    EXPR_TOKEN,
    DE_REF_TOKEN,
    REF_TOKEN,
    LIST_TOKEN,
    NULL_TOKEN,
} TokenType;

struct Token {
    TokenType type;
    void *data;
    size_t lineNum;
    size_t colNum;
};

int tokenize(struct Token *body, FILE *file);
void printTokenSpaces(struct Token *token, int numSpaces);

#define printToken(token) printTokenSpaces(token, 0)
