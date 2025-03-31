#include <generate.h>

bool outlineFuncDeclare(struct Token *token, struct ModuleData *module,
                        size_t exprLen) {
    if (exprLen < 3) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too few arguments for an "
                "extern function definition - expected 2 arguments\n",
                token->lineNum, token->colNum);
        return false;
    }
    if (exprLen > 3) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too many arguments for an "
                "extern function definition - expected 2 arguments\n",
                token->lineNum, token->colNum);
        return false;
    }
    if (((struct Token **)token->data)[1]->type != IDENT_TOKEN) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected an identifier\n",
                ((struct Token **)token->data)[1]->lineNum,
                ((struct Token **)token->data)[1]->colNum);
        return false;
    }

    char *name = (char *)(((struct Token **)token->data)[1]->data);
    if (stbds_shgetp_null(module->functions, name) != NULL) {
        fprintf(
            stderr,
            "ERROR on line %llu column %llu: Function \"%s\" already defined\n",
            token->lineNum, token->colNum, name);
        return false;
    }
    struct FuncData *funcData =
        generateFuncType(((struct Token **)token->data)[2], module, true, NULL);

    if (funcData == NULL) {
        return false;
    }

    LLVMValueRef *func = malloc(sizeof(LLVMValueRef));
    *func = LLVMAddFunction(module->module, name, *funcData->funcType);
    LLVMSetLinkage(*func, LLVMExternalLinkage);

    funcData->function = func;
    stbds_shput(module->functions, name, funcData);
    return true;
}

bool outlineFuncDefine(struct Token *token, struct ModuleData *module,
                       size_t exprLen) {
    if (exprLen > 3) {
        exprLen = 3;
    }
    return outlineFuncDeclare(token, module, exprLen);
}

bool outlineMacroDefine(struct Token *token, struct ModuleData *module,
                        size_t exprLen) {
    if (exprLen < 3) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too few arguments for a "
                "macro definition - expected at least 3 arguments (defmacro "
                "<name> <arguments> <body1> ...)\n",
                token->lineNum, token->colNum);
        return false;
    }
    if (((struct Token **)(token->data))[1]->type != IDENT_TOKEN) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected an identifier\n",
                ((struct Token **)(token->data))[1]->lineNum,
                ((struct Token **)(token->data))[1]->colNum);
        return false;
    }
    if (((struct Token **)(token->data))[2]->type != LIST_TOKEN) {
        fprintf(
            stderr,
            "ERROR on line %llu column %llu: Expected a list of arguments\n",
            ((struct Token **)(token->data))[1]->lineNum,
            ((struct Token **)(token->data))[1]->colNum);
        return false;
    }
    char *macroName = (char *)((struct Token **)(token->data))[1]->data;
    char **args = NULL;
    char *restArg = NULL;
    size_t argNum = stbds_arrlen(
        (struct Token **)(((struct Token **)(token->data))[2]->data));
    for (size_t i = 0; i < argNum; i++) {
        struct Token *token2 =
            ((struct Token **)(((struct Token **)(token->data))[2]->data))[i];
        if (token2->type != IDENT_TOKEN) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Expected an identifier\n",
                    token2->lineNum, token2->colNum);
            return false;
        }
        if (restArg != NULL) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Can only define one rest "
                    "argument and it must be at the and of the argument list\n",
                    token2->lineNum, token2->colNum);
            return false;
        }
        char *name = (char *)token2->data;
        if (strcmp(name, ":rest") == 0) {
            i++;
            restArg = (char *)((
                struct Token **)(((struct Token **)(token->data))[2]->data))[i]
                          ->data;
            continue;
        }
        stbds_arrpush(args, name);
    }
    struct MacroData *macroData = malloc(sizeof(struct MacroData));
    *macroData = (struct MacroData){args, restArg, (struct Token **)token->data,
                                    exprLen, argNum - (restArg != NULL)};
    stbds_shput(module->macros, macroName, macroData);
    return true;
}

bool outlineVarDef(struct Token *token, struct ModuleData *module,
                   int exprLen) {
    char *name;
    struct TypeData *type;
    struct Token *initValue;

    if (exprLen < 4) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too few arguments for a "
                "variable definition - expected 3 arguments\n",
                token->lineNum, token->colNum);
        return false;
    }
    if (exprLen > 4) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too many arguments for a "
                "variable definition - expected 3 arguments\n",
                token->lineNum, token->colNum);
        return false;
    }

    if (((struct Token **)token->data)[1]->type != IDENT_TOKEN) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected an identifier\n",
                ((struct Token **)token->data)[1]->lineNum,
                ((struct Token **)token->data)[1]->colNum);
        return false;
    }
    if (((struct Token **)token->data)[2]->type != IDENT_TOKEN &&
        ((struct Token **)token->data)[2]->type != EXPR_TOKEN &&
        ((struct Token **)token->data)[2]->type != REF_TOKEN &&
        ((struct Token **)token->data)[2]->type != DE_REF_TOKEN) {
        fprintf(stderr, "ERROR on line %llu column %llu: Expected a type\n",
                ((struct Token **)token->data)[2]->lineNum,
                ((struct Token **)token->data)[2]->colNum);
        return false;
    }

    name = (char *)(((struct Token **)token->data)[1]->data);
    type = getType(((struct Token **)token->data)[2], module);

    if (type == NULL) {
        return false;
    }

    if (stbds_shgetp_null(*(module->variables), name) != NULL ||
        stbds_shgetp_null(
            module->contexts[module->numContexts - 1]->localVariables, name)) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: variable \"%s\" is already "
                "defined\n",
                token->lineNum, token->colNum, (char *)token->data);
        return false;
    }

    LLVMTypeRef *llvmType = generateType(type, module);
    if (llvmType == NULL) {
        return false;
    }

    LLVMValueRef *llvmVar = malloc(sizeof(LLVMValueRef));
    *llvmVar = LLVMAddGlobal(module->module, *llvmType, name);
    LLVMSetLinkage(*llvmVar, LLVMExternalLinkage);

    struct VariableData variableData =
        (struct VariableData){type, llvmType, llvmVar};
    stbds_shput(module->contexts[module->numContexts - 1]->localVariables, name,
                variableData);

    return true;
}

bool outlineClassDefine(struct Token *token, struct ModuleData *module,
                        int exprLen) {
    if (exprLen < 5) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too few arguments for class "
                "definition, expected (defclass <class_name> "
                "'(<inherited_class1> ...) :variables '('(<var_name1> "
                "<var_type1>) ...))\n",
                token->lineNum, token->colNum);
        return false;
    }
    if (exprLen > 5) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too many arguments for class "
                "definition, expected (defclass <class_name> "
                "'(<inherited_class1> ...) :variables '('(<var_name1> "
                "<var_type1>) ...))\n",
                token->lineNum, token->colNum);
        return false;
    }
    struct Token **tokens = (struct Token **)token->data;
    if (tokens[1]->type != IDENT_TOKEN) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected an identifier as the "
                "class name\n",
                tokens[1]->lineNum, tokens[1]->colNum);
        return false;
    }
    if (tokens[2]->type != LIST_TOKEN) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected a list with the "
                "inherited classes, if this class doesn't inherit any classes, "
                "put an empty list there\n",
                tokens[2]->lineNum, tokens[2]->colNum);
        return false;
    }
    if (tokens[3]->type != IDENT_TOKEN) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected \":variables\"\n",
                tokens[3]->lineNum, tokens[3]->colNum);
        return false;
    }
    if (tokens[4]->type != LIST_TOKEN) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected a list of variable "
                "names and their types\n",
                tokens[4]->lineNum, tokens[4]->colNum);
        return false;
    }
    if (strcmp((char *)tokens[3]->data, ":variables") != 0) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected \":variables\"\n",
                tokens[3]->lineNum, tokens[3]->colNum);
        return false;
    }

    char *className = (char *)tokens[1]->data;
    struct ClassData **inheritClasses = NULL;
    struct ClassVariableList *variables = NULL;
    LLVMTypeRef *llvmVars = NULL;
    bool *isVarInherited = NULL;
    size_t numVars = 0;
    size_t numInherited = 0;

    struct Token **tokens2 = (struct Token **)tokens[2]->data;
    numInherited = stbds_arrlen(tokens2);
    for (size_t i = 0; i < numInherited; i++) {
        if (tokens2[i]->type != IDENT_TOKEN) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Expected an identifier as "
                    "the class to inherit from\n",
                    tokens2[i]->lineNum, tokens2[i]->colNum);
            return false;
        }
        if (stbds_shgetp_null(module->classes, (char *)tokens2[i]->data) ==
            NULL) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Invalid class %s\n",
                    tokens2[i]->lineNum, tokens2[i]->colNum,
                    (char *)tokens2[i]->data);
            return false;
        }
        stbds_arrpush(inheritClasses,
                      stbds_shget(module->classes, (char *)tokens2[i]->data));
    }

    for (size_t i = 0; i < numInherited; i++) {
        for (size_t j = 0; j < inheritClasses[i]->numVars; j++) {
            struct ClassVariableData var =
                inheritClasses[i]->variables[j].value;
            var.index = numVars;
            stbds_shput(variables, inheritClasses[i]->variables[j].key, var);
            stbds_arrpush(llvmVars,
                          *(inheritClasses[i]->variables[j].value.llvmType));
            stbds_arrpush(isVarInherited, true);
            numVars++;
        }
    }

    tokens2 = (struct Token **)tokens[4]->data;
    size_t tokens2Len = stbds_arrlen(tokens2);

    size_t intSize = 0;
    for (size_t i = 0; i < tokens2Len; i++) {
        if (tokens2[i]->type != LIST_TOKEN) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Expected a list with the "
                    "variable name, type and optionally the default value\n",
                    tokens2[i]->lineNum, tokens2[i]->colNum);
            return NULL;
        }
        struct Token **tokens3 = (struct Token **)tokens2[i]->data;
        size_t tokens3Len = stbds_arrlen(tokens3);
        if (tokens3Len != 2) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Expected a list with the "
                    "variable name and type\n",
                    tokens2[i]->lineNum, tokens2[i]->colNum);
            return NULL;
        }
        if (tokens3[0]->type != IDENT_TOKEN) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Expected an identifier\n",
                    tokens3[0]->lineNum, tokens3[0]->colNum);
            return NULL;
        }

        char *varName = (char *)tokens3[0]->data;
        struct TypeData *type = getType(tokens3[1], module);
        if (type == NULL) {
            return NULL;
        }
        if (type->type == CHAR || type->type == UNSIGNED8 ||
            type->type == INT8 || type->type == BOOL) {
            intSize += 8;
        } else if (type->type == INT32 || type->type == UNSIGNED32 ||
                   type->type == FLOAT) {
            intSize += 32;
        } else if (type->type == POINTER || type->type == UNSIGNED64 ||
                   type->type == DOUBLE) {
            intSize += 64;
        } else if (type->type == CLASS) {
            if (stbds_shgetp_null(module->classes, (char *)type->name) ==
                NULL) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Class \"%s\" doesn't "
                        "exist\n",
                        tokens2[i]->lineNum, tokens2[i]->colNum,
                        (char *)type->name);
                return NULL;
            }
            struct ClassData *classData =
                stbds_shget(module->classes, (char *)type->name);
            intSize += classData->intSize;
        }
        if (stbds_shgetp_null(variables, varName) != NULL) {
            struct ClassVariableData var = stbds_shget(variables, varName);
            if (!isVarInherited[var.index]) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Class variable \"%s\" "
                        "already defined\n",
                        tokens2[i]->lineNum, tokens2[i]->colNum, varName);
                return NULL;
            }
            stbds_shput(variables, varName, var);
            isVarInherited[var.index] = false;
            continue;
        }
        LLVMTypeRef *llvmType = generateType(type, module);
        if (llvmType == NULL) {
            return NULL;
        }
        if (type->type == STRING || type->type == CLASS ||
            type->type == VECTOR || type->type == MAP ||
            type->type == NULLABLE || type->type == ARRAY) {
            *llvmType = LLVMPointerType(*llvmType, 0);
        }

        struct ClassVariableData var =
            (struct ClassVariableData){type, llvmType, numVars};
        stbds_shput(variables, varName, var);
        stbds_arrpush(llvmVars, *llvmType);
        stbds_arrpush(isVarInherited, false);
        numVars++;
    }

    LLVMTypeRef *llvmStructType = malloc(sizeof(LLVMTypeRef));
    *llvmStructType = LLVMStructCreateNamed(module->context, className);
    LLVMStructSetBody(*llvmStructType, llvmVars, numVars, 0);
    struct ClassData *data = malloc(sizeof(struct ClassData));
    struct TypeData *classType = malloc(sizeof(struct TypeData));
    classType->type = CLASS;
    classType->name = className;
    classType->length = -1;
    *data = (struct ClassData){classType, llvmStructType, variables,
                               NULL,      numVars,        intSize};
    stbds_shput(module->classes, className, data);
    return true;
}

bool outlineImport(struct Token *token, struct ModuleData *module,
                   int exprLen) {
    if (exprLen < 2) {
        fprintf(
            stderr,
            "ERROR on line %llu column %llu: Too few arguments for import\n",
            token->lineNum, token->colNum);
        return false;
    }
    if (exprLen > 2) {
        fprintf(
            stderr,
            "ERROR on line %llu column %llu: Too many arguments for import\n",
            token->lineNum, token->colNum);
        return false;
    }
    if (((struct Token **)token->data)[1]->type != STRING_TOKEN) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected a string containing "
                "the path to the file to import without the file extension\n",
                ((struct Token **)token->data)[1]->lineNum,
                ((struct Token **)token->data)[1]->colNum);
        return false;
    }

    char *relFilePath2 = (char *)((struct Token **)token->data)[1]->data;
    size_t pathLen = strlen(relFilePath2);
    char *relFilePath = malloc((pathLen + 7) * sizeof(char));
    sprintf_s(relFilePath, (pathLen + 7), "%s.tlisp", relFilePath2);
    char *filePath = calloc(FILENAME_MAX, sizeof(char));
    if (toAbsolutePath(relFilePath, filePath, FILENAME_MAX) == NULL) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Error resolving path %s\n",
                token->lineNum, token->colNum, relFilePath);
        return false;
    };

    stbds_arrpush(module->toOutline, filePath);
    return true;
}

bool outlineClassFunc(struct Token *token, struct ModuleData *module,
                      int exprLen) {
    if (exprLen < 5) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too few arguments for class "
                "function definition, expected (classfun <class_name> "
                "<function_name> <function_arguments> <body1> ...)",
                token->lineNum, token->colNum);
        return false;
    }
    if (exprLen > 5) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too many arguments for class "
                "function definition, expected (classfun <class_name> "
                "<function_name> <function_arguments> <body1> ...)",
                token->lineNum, token->colNum);
        return false;
    }
    struct Token **tokens = (struct Token **)token->data;
    if (tokens[1]->type != IDENT_TOKEN) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected an identifier to "
                "specify the class this class function belongs to\n",
                tokens[1]->lineNum, tokens[1]->colNum);
        return false;
    }
    if (tokens[2]->type != IDENT_TOKEN) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected an identifier to "
                "specify the class function name\n",
                tokens[2]->lineNum, tokens[2]->colNum);
        return false;
    }
    if (tokens[3]->type != LIST_TOKEN) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected a list with the "
                "class arguments\n",
                tokens[3]->lineNum, tokens[3]->colNum);
        return false;
    }

    char *funcName = (char *)tokens[2]->data;

    if (stbds_shgetp_null(module->classes, (char *)tokens[1]->data) == NULL) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Invalid class \"%s\"\n",
                tokens[1]->lineNum, tokens[1]->colNum, (char *)tokens[1]->data);
        return false;
    }
    struct ClassData *classData =
        stbds_shget(module->classes, (char *)tokens[1]->data);

    if (stbds_shgetp_null(classData->functions, funcName) != NULL) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Class function \"%s\" for "
                "class \"%s\"\n",
                token->lineNum, token->colNum, funcName,
                (char *)tokens[1]->data);
        return false;
    }

    struct TypeData *classPtrType = malloc(sizeof(struct TypeData));
    classPtrType->type = POINTER;
    classPtrType->otherType = classData->classType;
    classPtrType->length = -1;
    LLVMTypeRef *llvmClassPtrType = malloc(sizeof(struct TypeData));
    *llvmClassPtrType = LLVMPointerType(*(classData->structType), 0);
    struct FunctionArgType firstArg = {classPtrType, llvmClassPtrType, "this",
                                       false};
    struct FuncData *funcData =
        generateFuncType(tokens[3], module, true, &firstArg);
    if (funcData == NULL) {
        return false;
    }

    LLVMValueRef func =
        LLVMAddFunction(module->module, funcName, *funcData->funcType);
    LLVMSetLinkage(func, LLVMExternalLinkage);
    funcData->function = malloc(sizeof(LLVMValueRef));
    *(funcData->function) = func;
    stbds_shput(classData->functions, funcName, funcData);
    return true;
}

bool outlineFile(struct Token *body, struct ModuleData *module) {
    size_t numTokens = stbds_arrlen((struct Token **)body->data);
    for (size_t i = 0; i < numTokens; i++) {
        struct Token *token = ((struct Token **)body->data)[i];
        if (token->type != EXPR_TOKEN) {
            continue;
        }

        size_t exprLen = stbds_arrlen((struct Token **)token->data);
        if (exprLen < 1) {
            continue;
        }

        if (((struct Token **)token->data)[0]->type != IDENT_TOKEN) {
            continue;
        }
        char *funcName = (char *)(((struct Token **)token->data)[0]->data);

        if (strcmp(funcName, "extern-fn") == 0) {
            if (!outlineFuncDeclare(token, module, exprLen)) {
                return false;
            }
        } else if (strcmp(funcName, "defun") == 0) {
            if (!outlineFuncDefine(token, module, exprLen)) {
                return false;
            }
        } else if (strcmp(funcName, "defmacro") == 0) {
            if (!outlineMacroDefine(token, module, exprLen)) {
                return false;
            }
        } else if (strcmp(funcName, "def") == 0) {
            if (!outlineVarDef(token, module, exprLen)) {
                return false;
            }
        } else if (strcmp(funcName, "defclass") == 0) {
            if (!outlineClassDefine(token, module, exprLen)) {
                return false;
            }
        } else if (strcmp(funcName, "classfun") == 0) {
            if (!outlineClassFunc(token, module, exprLen)) {
                return false;
            }
        } else if (strcmp(funcName, "import") == 0) {
            if (!outlineImport(token, module, exprLen)) {
                return false;
            }
        }
    }
    return true;
}