#include <tokenize.h>

char processBackslash(char ch) {
    switch (ch) {
    case 'a':
        return '\a';
    case 'b':
        return '\b';
    case 'e':
        return '\e';
    case 'f':
        return '\f';
    case 'n':
        return '\n';
    case 'r':
        return '\r';
    case 't':
        return '\t';
    case 'v':
        return '\v';
    case '\\':
        return '\\';
    case '\'':
        return '\'';
    case '"':
        return '\"';
    case '?':
        return '\?';
        // case '0':  // TODO: add better support for \0
        // return '\0';

    default:
        return ch;
    }
}

int readToken(FILE *file, char **buffer, size_t *bufferSize, size_t bufferSkip,
              bool stopAtString, size_t *currentLine, size_t *currentCol,
              bool lastTokenWasRef) {
    char ch;
    size_t len = bufferSkip;
    bool allowSpace = true;
    while ((ch = fgetc(file)) != EOF &&
           !((ch == '\n' || ch == ' ' && allowSpace) && (len > 0))) {
        (*currentCol)++;
        if (ch == '\n') {
            (*currentLine)++;
            (*currentCol) = 0;
        }
        if (ch == ' ' || ch == '\n') {
            if (lastTokenWasRef && len == 0) {
                len++;
                (*buffer)[0] = ' ';
                break;
            }
            continue;
        }
        if (len > *bufferSize - 1) {
            *bufferSize += 128;
            *buffer = realloc(*buffer, *bufferSize);
        }
        (*buffer)[len++] = ch;
        if (((ch == '(' || ch == '*' || ch == '#' || ch == ')') &&
             (*buffer)[0] != '"') &&
                allowSpace ||
            (ch == '"' && stopAtString)) {

            (*buffer)[len] = '\0';
            printf("STOP--------- %d %d %c %s\n", stopAtString, allowSpace,
                   (*buffer)[0], (*buffer));
            break;
        }
        if (ch == '"' && !stopAtString && allowSpace) {
            stopAtString = true;
        }
        if (ch == ';' && !stopAtString) {
            allowSpace = false;
        }
        if ((*buffer)[0] == '"' && ch == '\\') {
            char ch2 = fgetc(file);
            (*buffer)[len - 1] = processBackslash(ch2);
            if (ch2 == EOF) {
                break;
            }
        }
    }
    if (ch == ' ') {
        (*currentCol)++;
    }
    if (ch == '\n') {
        (*currentLine)++;
        (*currentCol) = 0;
    }
    (*buffer)[len] = '\0';
    return len;
}

int tokenize(struct Token *body, FILE *file) {
    int len = 0;
    size_t currentLine = 1;
    size_t currentCol = 1;
    char *buffer = malloc(sizeof(char) * 128);
    size_t bufferSize = 128;
    bool pushToken = true;
    bool closeParen = false;
    bool insideExpr = false;
    bool stopAtString = false;
    bool lastTokenWasRef = false;
    unsigned int refCount = 0;

    *body = (struct Token){EXPR_TOKEN, NULL, 0, 0};
    struct Token ****stack = malloc(sizeof(struct Token ***) * 16);
    struct Token **baseStack = malloc(sizeof(struct Token *) * 16);
    size_t stackSize = 0;
    size_t maxStackSize = 16;
    stack[stackSize] = (struct Token ***)&body->data;
    baseStack[stackSize] = body;

    struct Token token = (struct Token){NULL_TOKEN, NULL, 1, 1};
    while ((len = readToken(file, &buffer, &bufferSize, len, stopAtString,
                            &currentLine, &currentCol, lastTokenWasRef)) != 0) {
        printf("%s\n", buffer);
        if (lastTokenWasRef && len == 1 && buffer[0] == ' ') {
            // stackSize--;
            // refCount = 0;
            // insideExpr = false;
            // lastTokenWasRef = false;
            // continue;

            refCount--;
            closeParen = true;
            len = 0;
        }
        token.colNum -= len;
        lastTokenWasRef = false;

        if (buffer[len - 1] == ')' && buffer[0] != ';' && buffer[0] != '\"') {
            len--;
            buffer[len] = '\0';
            closeParen = true;
        }

        if (buffer[0] == '"') {
            if (buffer[len - 1] != '"') {
                buffer[len++] = ' ';
                stopAtString = true;
                continue;
            }
            stopAtString = false;
            token.type = STRING_TOKEN;
            token.data = malloc(sizeof(char) * (len - 1));
            strncpy(token.data, buffer + 1, len - 2);
            ((char *)token.data)[len - 2] = '\0';
        } else if (buffer[0] <= '9' && buffer[0] >= '0' ||
                   (buffer[0] == '-' && buffer[1] <= '9' && buffer[1] >= '0')) {
            token.type = INT_TOKEN;
            for (size_t i = buffer[0] == '-'; i < len; i++) {
                if ((buffer[i] < '0' || buffer[i] > '9') && buffer[i] != '.') {
                    fprintf(
                        stderr,
                        "ERROR on line %llu, column %llu: Invalid number %s\n",
                        token.lineNum, token.colNum, buffer);
                    return 1;
                }
                if (buffer[i] == '.') {
                    token.type = FLOAT_TOKEN;
                }
            }
            if (token.type == FLOAT_TOKEN) {
                token.data = malloc(sizeof(float));
                char *p;
                *(float *)token.data = strtof(buffer, &p);
            } else {
                token.data = malloc(sizeof(int));
                *(int *)token.data = atoi(buffer);
            }
        } else if (buffer[0] == ';') {
            pushToken = false;
        } else if (buffer[0] == '(' || buffer[0] == '*' || buffer[0] == '#' ||
                   buffer[0] == '\'') {
            if (buffer[0] == '(') {
                token.type = EXPR_TOKEN;
                if (refCount > 0) {
                    insideExpr = true;
                }
            } else if (buffer[0] == '\'') {
                if (buffer[1] != '(') {
                    fprintf(stderr,
                            "ERROR on line %llu column %llu: to make a "
                            "list you have to type a (",
                            token.lineNum, token.colNum);
                    return 1;
                }
                token.type = LIST_TOKEN;
            } else if (buffer[0] == '*') {
                refCount++;
                token.type = REF_TOKEN;
                lastTokenWasRef = true;
            } else {
                refCount++;
                token.type = DE_REF_TOKEN;
                insideExpr = false;
            }
            stackSize++;
            if (stackSize > maxStackSize - 1) {
                maxStackSize += 16;
                stack = realloc(stack, maxStackSize);
                baseStack = realloc(baseStack, maxStackSize);
            }
        } else if (len > 0) {
            token.type = IDENT_TOKEN;
            token.data = malloc(sizeof(char) * len);
            strcpy(token.data, buffer);
        }

        if (pushToken && token.type != NULL_TOKEN) {
            struct Token *tokenPtr = malloc(sizeof(struct Token));
            *tokenPtr = token;
            if (buffer[0] == '(' || buffer[0] == '*' || buffer[0] == '#' ||
                buffer[0] == '\'') {
                stack[stackSize] = (struct Token ***)&tokenPtr->data;
                baseStack[stackSize] = tokenPtr;
                stbds_arrpush(*(stack[stackSize - 1]), tokenPtr);
            } else {
                stbds_arrpush(*(stack[stackSize]), tokenPtr);
            }
        } else {
            pushToken = true;
        }

        if (refCount > 0) {
            if (!insideExpr && token.type != IDENT_TOKEN &&
                token.type != REF_TOKEN && token.type != DE_REF_TOKEN &&
                token.type != EXPR_TOKEN && token.type != NULL_TOKEN) {
                fprintf(stderr,
                        "ERROR on line %llu, column %llu: Can only "
                        "reference and dereference an "
                        "identifier or an expression\n",
                        token.lineNum, token.colNum);
                return 1;
            }
            if (token.type == IDENT_TOKEN && !insideExpr ||
                closeParen &&
                    (baseStack[stackSize - 1]->type == REF_TOKEN ||
                     baseStack[stackSize - 1]->type == DE_REF_TOKEN)) {
                refCount--;
                stackSize--;
                if (refCount == 0) {
                    insideExpr = false;
                }
            }
        }

        if (closeParen) {
            closeParen = false;
            stackSize--;
        }

        len = 0;
        buffer[0] = '\0';
        token = (struct Token){NULL_TOKEN, NULL, currentLine, currentCol};
    }
    return 0;
}

void printTokenSpaces(struct Token *token, int numSpaces) {

    if (token->type == INT_TOKEN) {
        printf("%*sINT_TOKEN     %d\n", numSpaces, "", *(int *)token->data);
    } else if (token->type == FLOAT_TOKEN) {
        printf("%*sFLOAT_TOKEN   %f\n", numSpaces, "", *(float *)token->data);
    } else if (token->type == STRING_TOKEN) {
        printf("%*sSTRING_TOKEN  %s\n", numSpaces, "", (char *)token->data);
    } else if (token->type == IDENT_TOKEN) {
        printf("%*sIDENT_TOKEN   %s\n", numSpaces, "", (char *)token->data);
    } else if (token->type == EXPR_TOKEN) {
        printf("%*sEXPR_TOKEN:\n", numSpaces, "");
        size_t numTokens = stbds_arrlen((struct Token **)token->data);
        for (size_t i = 0; i < numTokens; i++) {
            printTokenSpaces(((struct Token **)(token->data))[i],
                             numSpaces + 2);
        }
    } else if (token->type == LIST_TOKEN) {
        printf("%*sLIST_TOKEN:\n", numSpaces, "");
        size_t numTokens = stbds_arrlen((struct Token **)token->data);
        for (size_t i = 0; i < numTokens; i++) {
            printTokenSpaces(((struct Token **)(token->data))[i],
                             numSpaces + 2);
        }
    } else if (token->type == REF_TOKEN) {
        printf("%*sREF_TOKEN:    *\n", numSpaces, "");
        size_t numTokens = stbds_arrlen((struct Token **)token->data);
        for (size_t i = 0; i < numTokens; i++) {
            printTokenSpaces(((struct Token **)(token->data))[i],
                             numSpaces + 2);
        }
    } else if (token->type == DE_REF_TOKEN) {
        printf("%*sDE_REF_TOKEN: #\n", numSpaces, "");
        size_t numTokens = stbds_arrlen((struct Token **)token->data);
        for (size_t i = 0; i < numTokens; i++) {
            printTokenSpaces(((struct Token **)(token->data))[i],
                             numSpaces + 2);
        }
    } else {
        printf("NULL_TOKEN\n");
    }
}
