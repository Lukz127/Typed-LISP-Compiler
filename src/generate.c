#include <generate.h>

LLVMTypeRef mallocType;
LLVMValueRef mallocFunc;

LLVMTypeRef *generateType(struct TypeData *type, struct ModuleData *module);
struct ValueData *generateToken(struct Token *token, struct ModuleData *module,
                                bool charPtrInsteadOfString,
                                bool falseInsteadOfNil);
LLVMValueRef *generateTokenOfType(struct Token *token, struct TypeData type,
                                  struct ModuleData *module);
bool cmptype(struct TypeData *cmpType, struct TypeData *type, size_t lineNum,
             size_t colNum, bool printError);

LLVMValueRef *generateNil(LLVMTypeRef *llvmType, struct ModuleData *module) {
    LLVMValueRef *val = malloc(sizeof(LLVMValueRef));
    *val = LLVMConstNull(*llvmType);
    return val;
}

LLVMValueRef *generateVector(LLVMTypeRef vectorElementType,
                             LLVMValueRef *values, size_t len,
                             struct ModuleData *module) {
    // TODO
    module->nextTempNum++;
    LLVMTypeRef arrayType = LLVMArrayType(vectorElementType, len);
    LLVMPositionBuilderAtEnd(module->builder, module->allocaBlock);
    LLVMValueRef arrayPtr = LLVMBuildAlloca(module->builder, arrayType, "");
    LLVMPositionBuilderAtEnd(module->builder, module->currentBlock);
    for (int i = 0; i < len; i++) {
        LLVMValueRef indices[] = {LLVMConstInt(LLVMInt32Type(), 0, 0),
                                  LLVMConstInt(LLVMInt32Type(), i, 0)};
        LLVMValueRef elementPtr =
            LLVMBuildGEP2(module->builder, LLVMArrayType(LLVMInt32Type(), 3),
                          arrayPtr, indices, 2, "");
        LLVMBuildStore(module->builder, values[i], elementPtr);
    }

    LLVMTypeRef structType = LLVMStructTypeInContext(
        module->context,
        (LLVMTypeRef[]){arrayType, LLVMInt32TypeInContext(module->context),
                        LLVMInt32TypeInContext(module->context)},
        3, 0);
    LLVMValueRef *structPtr = malloc(sizeof(LLVMValueRef));
    LLVMPositionBuilderAtEnd(module->builder, module->allocaBlock);
    *structPtr = LLVMBuildAlloca(module->builder, structType, "");
    LLVMPositionBuilderAtEnd(module->builder, module->currentBlock);

    LLVMValueRef dataPtrIndices[] = {LLVMConstInt(LLVMInt32Type(), 0, 0),
                                     LLVMConstInt(LLVMInt32Type(), 0, 0)};
    LLVMValueRef dataPtrField = LLVMBuildGEP2(
        module->builder, structType, *structPtr, dataPtrIndices, 2, "");
    LLVMBuildStore(module->builder, arrayPtr, dataPtrField);

    LLVMValueRef sizeIndices[] = {LLVMConstInt(LLVMInt32Type(), 0, 0),
                                  LLVMConstInt(LLVMInt32Type(), 1, 0)};
    LLVMValueRef sizeField = LLVMBuildGEP2(
        module->builder, structType, *structPtr, sizeIndices, len - 1, "");
    LLVMBuildStore(module->builder, LLVMConstInt(LLVMInt32Type(), 3, 0),
                   sizeField);

    LLVMValueRef capacityIndices[] = {LLVMConstInt(LLVMInt32Type(), 0, 0),
                                      LLVMConstInt(LLVMInt32Type(), 2, 0)};
    LLVMValueRef capacityField = LLVMBuildGEP2(
        module->builder, structType, *structPtr, capacityIndices, len - 1, "");
    LLVMBuildStore(module->builder, LLVMConstInt(LLVMInt32Type(), 3, 0),
                   capacityField);

    return structPtr;
}

LLVMValueRef *generateString(struct Token *token, struct ModuleData *module,
                             char *name) {
    size_t len = strlen((char *)token->data);
    LLVMValueRef size = LLVMConstInt(LLVMInt64Type(), len + 1, 0);
    LLVMValueRef stringAlloc =
        LLVMBuildCall2(module->builder, mallocType, mallocFunc, &size, 1, "");
    LLVMPositionBuilderAtEnd(module->builder, module->allocaBlock);
    LLVMValueRef stringPtr = LLVMBuildAlloca(
        module->builder,
        LLVMPointerType(LLVMInt8TypeInContext(module->context), 0), "");
    LLVMPositionBuilderAtEnd(module->builder, module->currentBlock);
    LLVMBuildStore(module->builder,
                   LLVMConstString((char *)token->data, len, 0), stringPtr);
    LLVMBuildMemCpy(module->builder, stringAlloc, 1, stringPtr, 1, size);

    LLVMTypeRef structType = LLVMStructTypeInContext(
        module->context,
        (LLVMTypeRef[]){
            LLVMPointerType(LLVMInt8TypeInContext(module->context), 0),
            LLVMInt32TypeInContext(module->context),
            LLVMInt32TypeInContext(module->context)},
        3, 0);
    LLVMValueRef *structPtr = malloc(sizeof(LLVMValueRef));
    LLVMPositionBuilderAtEnd(module->builder, module->allocaBlock);
    *structPtr = LLVMBuildAlloca(module->builder, structType, name);
    LLVMPositionBuilderAtEnd(module->builder, module->currentBlock);

    LLVMValueRef dataPtrIndices[] = {LLVMConstInt(LLVMInt32Type(), 0, 0),
                                     LLVMConstInt(LLVMInt32Type(), 0, 0)};
    LLVMValueRef dataPtrField = LLVMBuildGEP2(
        module->builder, structType, *structPtr, dataPtrIndices, 2, "");
    LLVMBuildStore(module->builder, stringAlloc, dataPtrField);

    LLVMValueRef sizeIndices[] = {LLVMConstInt(LLVMInt32Type(), 0, 0),
                                  LLVMConstInt(LLVMInt32Type(), 1, 0)};
    LLVMValueRef sizeField = LLVMBuildGEP2(module->builder, structType,
                                           *structPtr, sizeIndices, 2, "");
    LLVMBuildStore(module->builder, LLVMConstInt(LLVMInt32Type(), 3, 0),
                   sizeField);

    LLVMValueRef capacityIndices[] = {LLVMConstInt(LLVMInt32Type(), 0, 0),
                                      LLVMConstInt(LLVMInt32Type(), 2, 0)};
    LLVMValueRef capacityField = LLVMBuildGEP2(
        module->builder, structType, *structPtr, capacityIndices, 2, "");
    LLVMBuildStore(module->builder, LLVMConstInt(LLVMInt32Type(), 3, 0),
                   capacityField);

    return structPtr;
}

size_t findArgIndex(struct FunctionArgType *args, char *name, size_t numArgs) {
    for (size_t i = 0; i < numArgs; i++) {
        if (strcmp(args[i].name, name) == 0) {
            return i;
        }
    }
    return numArgs + 1;
}

struct ValueData *generateFuncCall(struct Token *token,
                                   struct ModuleData *module, int exprLen) {
    struct FuncData *funcData = stbds_shget(
        module->functions, (char *)(((struct Token **)token->data)[0])->data);

    bool nextArg = false;
    char *nextArgName = NULL;
    LLVMValueRef *llvmArgs = NULL;
    LLVMValueRef *llvmVA = NULL;
    LLVMValueRef *llvmRest = NULL;
    size_t numArgs = 0;
    size_t numExtraArgs = 0;
    struct {
        char *key;
        LLVMValueRef *value;
    } *args = NULL;
    for (size_t i = 1; i < exprLen; i++) {
        struct Token *token2 = ((struct Token **)token->data)[i];
        if (token2->type == IDENT_TOKEN) {
            if (nextArgName == NULL && ((char *)token2->data)[0] == ':') {
                nextArg = true;
                nextArgName = ((char *)token2->data) + 1;
                bool isValid = false;
                for (size_t j = 0; j < funcData->numArgs; j++) {
                    if (strcmp(nextArgName, funcData->args[j].name) == 0) {
                        isValid = true;
                        break;
                    }
                }
                if (!isValid) {
                    fprintf(stderr,
                            "ERROR on line %llu column %llu: Unknown argument "
                            "\"%s\"\n",
                            token2->lineNum, token2->colNum,
                            (char *)token2->data);
                    return NULL;
                }
                continue;
            }
        }

        if (nextArg && nextArgName == NULL) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Unexpected unnamed "
                    "argument after a named argument, unnamed arguments must "
                    "come before named arguments\n",
                    token2->lineNum, token2->colNum);
        }
        if (nextArg) {
            if (stbds_shgetp_null(args, nextArgName) != NULL) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Argument "
                        "\"%s\" already defined\n",
                        token2->lineNum, token2->colNum, nextArgName);
                return NULL;
            }
            size_t argIdx =
                findArgIndex(funcData->args, nextArgName, funcData->numArgs);
            if (argIdx > funcData->numArgs) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Argument \"%s\" "
                        "doesn't exist\n",
                        token2->lineNum, token2->colNum, nextArgName);
            }
            LLVMValueRef *val = generateTokenOfType(
                token2, *(funcData->args[argIdx].type), module);
            if (val == NULL) {
                return NULL;
            }
            stbds_shput(args, nextArgName, val);
            nextArgName = NULL;
            continue;
        }
        if (i > funcData->numArgs) {
            struct ValueData *val = generateToken(token2, module, false, true);
            LLVMValueRef *llvmVal = val->value;
            free(val);
            numExtraArgs++;
            if (funcData->isVarArg) {
                stbds_arrpush(llvmVA, *llvmVal);
                continue;
            }
            if (funcData->restArg != NULL) {
                stbds_arrpush(llvmRest, *llvmVal);
                continue;
            }
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Too many arguments in "
                    "function call\n",
                    token2->lineNum, token2->colNum);
            return NULL;
        }
        LLVMValueRef *val =
            generateTokenOfType(token2, *(funcData->args[i - 1].type), module);
        if (val == NULL) {
            return NULL;
        }
        numArgs++;
        stbds_shput(args, funcData->args[i - 1].name, val);
    }

    for (size_t i = 0; i < funcData->numArgs; i++) {
        if (stbds_shgetp_null(args, funcData->args[i].name) == NULL) {
            if (!funcData->args[i].optional) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Required argument "
                        "\"%s\" not provided\n",
                        token->lineNum, token->colNum, funcData->args[i].name);
            }
            LLVMValueRef *nilValue =
                generateNil(funcData->args[i].llvmType, module);
            stbds_shput(args, funcData->args[i].name, nilValue);
        }
        LLVMValueRef arg = *stbds_shget(args, funcData->args[i].name);
        stbds_arrpush(llvmArgs, arg);
    }
    if (funcData->restArg != NULL) {
        LLVMTypeRef *restArgType =
            generateType(funcData->restArg->type->otherType, module);
        if (restArgType == NULL) {
            return NULL;
        }
        LLVMValueRef *restVar =
            generateVector(*restArgType, llvmRest, numExtraArgs, module);
        if (restVar == NULL) {
            return NULL;
        }
        stbds_arrpush(llvmArgs, *restVar);
        numArgs++;
    }
    if (llvmVA != NULL) {
        for (size_t i = 0; i < numExtraArgs; i++) {
            stbds_arrpush(llvmArgs, llvmVA[i]);
            numArgs++;
        }
    }

    LLVMValueRef *callVal = malloc(sizeof(LLVMValueRef));
    *callVal = LLVMBuildCall2(module->builder, *funcData->funcType,
                              *funcData->function, llvmArgs, numArgs, "");
    struct ValueData *ret = malloc(sizeof(struct ValueData));
    ret->value = callVal;
    ret->type = funcData->retType;
    return ret;
}

struct TypeData *getType(struct Token *token, struct ModuleData *module) {
    if (token->type == IDENT_TOKEN) {
        if (strcmp((char *)token->data, "bool") == 0) {
            struct TypeData *type = malloc(sizeof(struct TypeData));
            *type = (struct TypeData){BOOL, NULL, NULL, -1, NULL};
            return type;
        }
        if (strcmp((char *)token->data, "int") == 0) {
            struct TypeData *type = malloc(sizeof(struct TypeData));
            *type = (struct TypeData){INT32, NULL, NULL, -1, NULL};
            return type;
        }
        if (strcmp((char *)token->data, "uint") == 0) {
            struct TypeData *type = malloc(sizeof(struct TypeData));
            *type = (struct TypeData){UNSIGNED32, NULL, NULL, -1, NULL};
            return type;
        }
        if (strcmp((char *)token->data, "float") == 0) {
            struct TypeData *type = malloc(sizeof(struct TypeData));
            *type = (struct TypeData){FLOAT32, NULL, NULL, -1, NULL};
            return type;
        }
        if (strcmp((char *)token->data, "char") == 0) {
            struct TypeData *type = malloc(sizeof(struct TypeData));
            *type = (struct TypeData){CHAR, NULL, NULL, -1, NULL};
            return type;
        }
        if (strcmp((char *)token->data, "string") == 0) {
            struct TypeData *type = malloc(sizeof(struct TypeData));
            *type = (struct TypeData){STRING, NULL, NULL, -1, NULL};
            return type;
        }
        if (stbds_shgetp_null(module->classes, (char *)token->data) != NULL) {
            struct TypeData *type = malloc(sizeof(struct TypeData));
            *type =
                (struct TypeData){CLASS, NULL, NULL, 0, (char *)token->data};
            return type;
        }
    }
    if (token->type == REF_TOKEN) {
        struct TypeData *otherType =
            getType(((struct Token **)token->data)[0], module);
        if (otherType == NULL) {
            return NULL;
        }
        struct TypeData *type = malloc(sizeof(struct TypeData));
        *type = (struct TypeData){POINTER, otherType, NULL, -1, NULL};
        return type;
    }
    if (token->type == EXPR_TOKEN) {
        int exprLen = stbds_arrlen((struct Token **)token->data);
        if (exprLen == 0) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Expected a valid type\n",
                    token->lineNum, token->colNum);
            return NULL;
        }
        if (((struct Token **)token->data)[0]->type != IDENT_TOKEN) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Expected a valid type\n",
                    ((struct Token **)token->data)[0]->lineNum,
                    ((struct Token **)token->data)[0]->colNum);
            return NULL;
        }
        char *typeName = (char *)((struct Token **)token->data)[0]->data;
        if (strcmp(typeName, "array") == 0) {
            if (exprLen != 3) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Expected 3 arguments "
                        "to specify the array type - (array <array_type> "
                        "<array_size>)\n",
                        token->lineNum, token->colNum);
                return NULL;
            }
            struct TypeData *otherType =
                getType(((struct Token **)token->data)[1], module);
            if (otherType == NULL) {
                return NULL;
            }
            if (((struct Token **)token->data)[2]->type != INT_TOKEN) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Expected an integer "
                        "specifying the array size\n",
                        ((struct Token **)token->data)[2]->lineNum,
                        ((struct Token **)token->data)[2]->colNum);
                return NULL;
            }
            struct TypeData *type = malloc(sizeof(struct TypeData));
            *type = (struct TypeData){
                ARRAY, otherType, NULL,
                *((int *)((struct Token **)token->data)[2]->data), NULL};
            return type;
        }
        if (strcmp(typeName, "vector") == 0) {
            if (exprLen != 2) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Expected 2 arguments "
                        "to specify the vector type - (vector <vector_type>)\n",
                        token->lineNum, token->colNum);
                return NULL;
            }
            struct TypeData *otherType =
                getType(((struct Token **)token->data)[1], module);
            if (otherType == NULL) {
                return NULL;
            }
            struct TypeData *type = malloc(sizeof(struct TypeData));
            *type = (struct TypeData){VECTOR, otherType, NULL, -1, NULL};
            return type;
        }
        if (strcmp(typeName, "optional") == 0) {
            if (exprLen != 2) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Expected 2 arguments "
                        "to specify the optional type - (optional "
                        "<optional_type>)\n",
                        token->lineNum, token->colNum);
                return NULL;
            }
            struct TypeData *otherType =
                getType(((struct Token **)token->data)[1], module);
            if (otherType == NULL) {
                return NULL;
            }
            struct TypeData *type = malloc(sizeof(struct TypeData));
            *type = (struct TypeData){NULLABLE, otherType, NULL, -1, NULL};
            return type;
        }
        if (strcmp(typeName, "map") == 0) {
            if (exprLen != 3) {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Expected 3 arguments "
                    "to specify the map type - (map <key_type> <value_type>)\n",
                    token->lineNum, token->colNum);
                return NULL;
            }
            struct TypeData *keyType =
                getType(((struct Token **)token->data)[1], module);
            struct TypeData *valueType =
                getType(((struct Token **)token->data)[2], module);
            if (keyType == NULL || valueType == NULL) {
                return NULL;
            }
            if (((struct Token **)token->data)[2]->type != INT_TOKEN) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Expected an integer "
                        "specifying the array size\n",
                        ((struct Token **)token->data)[2]->lineNum,
                        ((struct Token **)token->data)[2]->colNum);
                return NULL;
            }
            struct TypeData *type = malloc(sizeof(struct TypeData));
            *type = (struct TypeData){MAP, keyType, valueType, -1, NULL};
            return type;
        }
    }
    fprintf(stderr, "ERROR on line %llu column %llu: Expected a valid type\n",
            token->lineNum, token->colNum);
    return NULL;
}

LLVMTypeRef *generateType(struct TypeData *type, struct ModuleData *module) {
    LLVMTypeRef *llvmType = malloc(sizeof(LLVMTypeRef));
    if (type->type == INT32 || type->type == UNSIGNED32) {
        *llvmType = LLVMInt32TypeInContext(module->context);
        return llvmType;
    }
    if (type->type == UNSIGNED64) {
        *llvmType = LLVMInt64TypeInContext(module->context);
        return llvmType;
    }
    if (type->type == FLOAT32) {
        *llvmType = LLVMFloatTypeInContext(module->context);
        return llvmType;
    }
    if (type->type == BOOL) {
        *llvmType = LLVMInt1TypeInContext(module->context);
        return llvmType;
    }
    if (type->type == CHAR) {
        *llvmType = LLVMInt8TypeInContext(module->context);
        return llvmType;
    }
    if (type->type == STRING) {
        *llvmType = LLVMStructTypeInContext(
            module->context,
            (LLVMTypeRef[]){
                LLVMPointerType(LLVMInt8TypeInContext(module->context), 0),
                LLVMInt32TypeInContext(module->context),
                LLVMInt32TypeInContext(module->context)},
            3, 0);
        return llvmType;
    }
    if (type->type == CLASS) {
        return stbds_shget(module->classes, type->name).type;
    }
    if (type->type == POINTER) {
        LLVMTypeRef *otherType = generateType(type->otherType, module);
        if (otherType == NULL) {
            return NULL;
        }
        *llvmType = LLVMPointerType(*otherType, 0);
        return llvmType;
    }
    if (type->type == VECTOR) {
        LLVMTypeRef *otherType = generateType(type->otherType, module);
        if (otherType == NULL) {
            return NULL;
        }
        *llvmType = LLVMStructTypeInContext(
            module->context,
            (LLVMTypeRef[]){LLVMPointerType(*otherType, 0),
                            LLVMInt32TypeInContext(module->context),
                            LLVMInt32TypeInContext(module->context)},
            3, 0);
        return llvmType;
    }
    if (type->type == NULLABLE) {
        LLVMTypeRef *otherType = generateType(type->otherType, module);
        if (otherType == NULL) {
            return NULL;
        }
        *llvmType = LLVMStructTypeInContext(
            module->context,
            (LLVMTypeRef[]){*otherType, LLVMInt1TypeInContext(module->context)},
            2, 0);
        return llvmType;
    }
    // TODO: add MAP
    fprintf(stderr, "COMPILER ERROR during type generation\n");
    free(llvmType);
    return NULL;
}

LLVMValueRef *generateOptionalFromValue(LLVMValueRef *value,
                                        LLVMTypeRef *valueType,
                                        struct ModuleData *module) {
    LLVMValueRef *val = malloc(sizeof(LLVMValueRef));
    LLVMTypeRef valType = LLVMStructTypeInContext(
        module->context,
        (LLVMTypeRef[]){LLVMPointerType(*valueType, 0),
                        LLVMInt1TypeInContext(module->context)},
        2, 0);

    *val = LLVMConstStructInContext(
        module->context,
        (LLVMValueRef[]){
            *value, LLVMConstInt(LLVMInt1TypeInContext(module->context), 0, 0)},
        2, 0);
    // *val = LLVMBuildAlloca(module->builder, valType, "");
    // LLVMBuildStore(
    //     module->builder,
    //     LLVMConstStructInContext(
    //         module->context,
    //         (LLVMValueRef[]){
    //             *value,
    //             LLVMConstInt(LLVMInt1TypeInContext(module->context), 0, 0)},
    //         2, 0),
    //     *val);

    // LLVMValueRef e1 =
    //     LLVMBuildStructGEP2(module->builder, valType, *val, 0, "");
    // LLVMValueRef e2 =
    //     LLVMBuildStructGEP2(module->builder, valType, *val, 1, "");
    // LLVMBuildStore(module->builder, *value, e1);
    // LLVMBuildStore(module->builder,
    //                LLVMConstInt(LLVMInt1TypeInContext(module->context), 0,
    //                0), e2);

    // LLVMBuildInsertValue(module->builder, LLVMGetUndef(valType), *value,
    // 0,
    // ""); LLVMBuildInsertValue( module->builder, LLVMGetUndef(valType),
    // LLVMConstInt(LLVMInt1TypeInContext(module->context), 0, 0), 1, "");
    return val;
}

LLVMValueRef *generateNilOptional(LLVMTypeRef *valueType,
                                  struct ModuleData *module) {
    LLVMValueRef *val = malloc(sizeof(LLVMValueRef));
    LLVMTypeRef valType = LLVMStructTypeInContext(
        module->context,
        (LLVMTypeRef[]){LLVMPointerType(*valueType, 0),
                        LLVMInt1TypeInContext(module->context)},
        2, 0);

    LLVMPositionBuilderAtEnd(module->builder, module->allocaBlock);
    *val = LLVMBuildAlloca(module->builder, valType, "");
    LLVMPositionBuilderAtEnd(module->builder, module->currentBlock);
    LLVMBuildInsertValue(module->builder, LLVMGetUndef(valType),
                         LLVMConstNull(*valueType), 0, "");
    LLVMBuildInsertValue(
        module->builder, LLVMGetUndef(valType),
        LLVMConstInt(LLVMInt1TypeInContext(module->context), 1, 0), 1, "");
    return val;
}

LLVMValueRef *generateVarDef(struct Token *token, struct ModuleData *module,
                             int exprLen) {
    char *name;
    struct TypeData *type;
    struct Token *initValue;

    if (exprLen < 4) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too few arguments for a "
                "variable definition - expected 3 arguments\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    if (exprLen > 4) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too many arguments for a "
                "variable definition - expected 3 arguments\n",
                token->lineNum, token->colNum);
        return NULL;
    }

    if (((struct Token **)token->data)[1]->type != IDENT_TOKEN) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected an identifier\n",
                ((struct Token **)token->data)[1]->lineNum,
                ((struct Token **)token->data)[1]->colNum);
        return NULL;
    }
    if (((struct Token **)token->data)[2]->type != IDENT_TOKEN &&
        ((struct Token **)token->data)[2]->type != EXPR_TOKEN &&
        ((struct Token **)token->data)[2]->type != REF_TOKEN &&
        ((struct Token **)token->data)[2]->type != DE_REF_TOKEN) {
        fprintf(stderr, "ERROR on line %llu column %llu: Expected a type\n",
                ((struct Token **)token->data)[2]->lineNum,
                ((struct Token **)token->data)[2]->colNum);
        return NULL;
    }

    name = (char *)(((struct Token **)token->data)[1]->data);
    initValue = ((struct Token **)token->data)[3];
    type = getType(((struct Token **)token->data)[2], module);

    if (type == NULL) {
        return NULL;
    }

    if (stbds_shgetp_null(module->variables, name) != NULL) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: variable \"%s\" is already "
                "defined\n",
                token->lineNum, token->colNum, (char *)token->data);
        return NULL;
    }

    LLVMTypeRef *llvmType = generateType(type, module);
    if (llvmType == NULL) {
        return NULL;
    }

    LLVMValueRef *llvmVar;
    if (type->type == STRING) {
        llvmVar = generateString(initValue, module, name);
        if (llvmVar == NULL) {
            return NULL;
        }
    } else {
        LLVMValueRef *llvmInitValue;
        if (type->type == NULLABLE) {
            struct ValueData *val = generateToken(
                initValue, module, type->otherType->type != STRING,
                type->otherType->type == BOOL);
            if (val == NULL) {
                return NULL;
            }
            if (val->type->type != NULLABLE) {
                if (val->type->type != NIL &&
                    !cmptype(val->type, type->otherType, initValue->lineNum,
                             initValue->colNum, true)) {
                    return NULL;
                }
                LLVMTypeRef *llvmValType = generateType(val->type, module);
                if (llvmValType == NULL) {
                    return NULL;
                }
                if (val->type->type == NIL) {
                    llvmInitValue = generateNilOptional(llvmValType, module);
                } else {
                    llvmInitValue = generateOptionalFromValue(
                        val->value, llvmValType, module);
                }
            } else if (!cmptype(val->type, type, initValue->lineNum,
                                initValue->colNum, true)) {
                return NULL;
            } else {
                llvmInitValue = val->value;
                free(val->type);
                free(val);
            }
        } else {
            llvmInitValue = generateTokenOfType(initValue, *type, module);
        }

        llvmVar = malloc(sizeof(LLVMValueRef));
        LLVMPositionBuilderAtEnd(module->builder, module->allocaBlock);
        *llvmVar = LLVMBuildAlloca(module->builder, *llvmType, name);
        LLVMPositionBuilderAtEnd(module->builder, module->currentBlock);
        LLVMBuildStore(module->builder, *llvmInitValue, *llvmVar);

        free(llvmInitValue);
    }

    struct VariableData variableData =
        (struct VariableData){type, llvmType, llvmVar};
    stbds_shput(module->variables, name, variableData);

    return llvmVar;
}

struct FuncData *generateFuncType(struct Token *token,
                                  struct ModuleData *module) {
    if (token->type != LIST_TOKEN) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Invalid function type "
                "- expected a list\n",
                token->lineNum, token->colNum);
        return NULL;
    }

    size_t listLen = stbds_arrlen((struct Token *)token->data);
    LLVMTypeRef *llvmArgs = NULL;
    struct FunctionArgType *args = NULL;
    struct FunctionArgType *restArg = NULL;
    bool va = false;
    bool optional = false;
    bool rest = false;
    int numRest = 0;
    int numArgs = 0;

    for (size_t i = 0; i < listLen; i++) {
        struct Token *token2 = ((struct Token **)token->data)[i];
        if (i == listLen - 1) {
            struct TypeData *type = getType(token2, module);
            if (type == NULL) {
                return NULL;
            }
            LLVMTypeRef *llvmType = generateType(type, module);
            if (llvmType == NULL) {
                return NULL;
            }
            struct FunctionArgType arg = {type, llvmType, ""};
            stbds_arrpush(args, arg);
            stbds_arrpush(llvmArgs, *llvmType);
            numArgs++;
        } else if (token2->type == LIST_TOKEN) {
            size_t typeLen = stbds_arrlen((struct Token *)token2->data);
            if (typeLen != 2) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Expected a function "
                        "argument type - '(<arg_name> <arg_type>)\n",
                        token2->lineNum, token2->colNum);
                return NULL;
            }
            if (((struct Token **)(struct Token *)token2->data)[0]->type !=
                IDENT_TOKEN) {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Expected an identifier\n",
                    ((struct Token **)(struct Token *)token2->data)[0]->lineNum,
                    ((struct Token **)(struct Token *)token2->data)[0]->colNum);
                return NULL;
            }
            if (numRest > 0) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: A rest argument "
                        "already defined\n",
                        token2->lineNum, token2->colNum);
                return NULL;
            }
            struct TypeData *type = getType(
                ((struct Token **)(struct Token *)token2->data)[1], module);
            if (type == NULL) {
                return NULL;
            }
            if (rest) {
                struct TypeData *newType =
                    (struct TypeData *)malloc(sizeof(struct TypeData));
                *newType = (struct TypeData){VECTOR, type, NULL, 0, NULL};
                type = newType;
            } else if (optional) {
                struct TypeData *newType =
                    (struct TypeData *)malloc(sizeof(struct TypeData));
                *newType = (struct TypeData){NULLABLE, type, NULL, -1, NULL};
                type = newType;
            }
            LLVMTypeRef *llvmType = generateType(type, module);
            if (llvmType == NULL) {
                return NULL;
            }
            struct FunctionArgType arg = {
                type, llvmType,
                (char *)(((struct Token **)(struct Token *)token2->data)[1]
                             ->data)};
            if (rest) {
                restArg = (struct FunctionArgType *)malloc(
                    sizeof(struct FunctionArgType));
                *restArg = arg;
                continue;
            }
            stbds_arrpush(args, arg);
            stbds_arrpush(llvmArgs, *llvmType);
            numArgs++;
        } else if (token2->type == IDENT_TOKEN) {
            if (strcmp((char *)token2->data, "...") == 0 &&
                (i == listLen - 1 || i == listLen - 2) && !va) {
                va = true;
                continue;
            }
            if (strcmp((char *)token2->data, ":optional") == 0) {
                if (rest) {
                    fprintf(stderr,
                            "ERROR on line %llu column %llu: Can't define an "
                            "optional argument after the rest argument\n",
                            token2->lineNum, token2->colNum);
                    return NULL;
                }
                if (optional) {
                    fprintf(stderr,
                            "ERROR on line %llu column %llu: Already defining "
                            "optional arguments\n",
                            token2->lineNum, token2->colNum);
                    return NULL;
                }
                optional = true;
                continue;
            }
            if (strcmp((char *)token2->data, ":rest") == 0) {
                if (rest) {
                    fprintf(stderr,
                            "ERROR on line %llu column %llu: Rest argument "
                            "already defined\n",
                            token2->lineNum, token2->colNum);
                    return NULL;
                }
                rest = true;
                continue;
            }
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Invalid function type "
                    "syntax\n",
                    token2->lineNum, token2->colNum);
            return NULL;
        } else {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Invalid function type "
                    "syntax\n",
                    token2->lineNum, token2->colNum);
            return NULL;
        }
    }

    if (numArgs == 0) {
        fprintf(
            stderr,
            "ERROR on line %llu column %llu: Expected at least the function "
            "return type or nil if function doesn't return anything\n",
            token->lineNum, token->colNum);
        return NULL;
    }

    struct TypeData *ret = args[numArgs - 1].type;
    LLVMTypeRef *llvmRet = args[numArgs - 1].llvmType;
    stbds_arrpop(args);
    stbds_arrpop(llvmArgs);
    numArgs--;

    if (rest && va) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Function can't be vararg and "
                "have a rest argument at the same time\n",
                token->lineNum, token->colNum);
        return NULL;
    }

    if (rest) {
        stbds_arrpush(llvmArgs, *restArg->llvmType);
        numArgs++;
    }

    LLVMTypeRef *funcType = malloc(sizeof(LLVMTypeRef));
    *funcType = LLVMFunctionType(*llvmRet, llvmArgs, numArgs, va);

    struct FuncData *funcData = malloc(sizeof(struct FuncData));
    *funcData = (struct FuncData){NULL, funcType, ret,     llvmRet,
                                  args, restArg,  numArgs, va};
    return funcData;
}

LLVMValueRef *generateFuncDeclare(struct Token *token,
                                  struct ModuleData *module, int exprLen) {
    if (exprLen < 3) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too few arguments for an "
                "extern function definition - expected 2 arguments\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    if (exprLen > 3) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too many arguments for an "
                "extern function definition - expected 2 arguments\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    if (((struct Token **)token->data)[1]->type != IDENT_TOKEN) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected an identifier\n",
                ((struct Token **)token->data)[1]->lineNum,
                ((struct Token **)token->data)[1]->colNum);
        return NULL;
    }

    char *name = (char *)(((struct Token **)token->data)[1]->data);
    struct FuncData *funcData =
        generateFuncType(((struct Token **)token->data)[2], module);

    if (funcData == NULL) {
        return NULL;
    }

    LLVMValueRef *func = malloc(sizeof(LLVMValueRef));
    *func = LLVMAddFunction(module->module, name, *funcData->funcType);

    funcData->function = func;
    stbds_shput(module->functions, name, funcData);
    return func;
}

struct ValueData *generateAdd(struct Token *token, struct ModuleData *module,
                              size_t exprLen) {
    if (exprLen < 3) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too few arguments for "
                "addition - expected 3 arguments\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    if (exprLen > 3) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too many arguments for "
                "addition - expected 3 arguments\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    struct ValueData *left =
        generateToken(((struct Token **)token->data)[1], module, false, true);
    if (left == NULL) {
        return NULL;
    }
    struct ValueData *right =
        generateToken(((struct Token **)token->data)[2], module, false, true);
    if (right == NULL) {
        return NULL;
    }
    struct ValueData *val = malloc(sizeof(struct ValueData));
    val->value = malloc(sizeof(LLVMValueRef));
    val->type = malloc(sizeof(struct TypeData));
    val->type->length = -1;
    if (left->type->type == FLOAT32 || right->type->type == FLOAT32) {
        if (left->type->type != FLOAT32) {
            if (left->type->type == INT32) {
                *(left->value) = LLVMBuildSIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during addition, "
                       "possible lost of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == UNSIGNED32) {
                *(left->value) = LLVMBuildUIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during addition, "
                       "possible lost of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Can only add types "
                        "float, int, uint\n",
                        token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        if (right->type->type != FLOAT32) {
            if (right->type->type == INT32) {
                *(right->value) = LLVMBuildSIToFP(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during addition, "
                       "possible lost of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == UNSIGNED32) {
                *(right->value) = LLVMBuildUIToFP(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during addition, "
                       "possible lost of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Can only add types "
                        "float, int, uint\n",
                        token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        *(val->value) =
            LLVMBuildFAdd(module->builder, *(left->value), *(right->value), "");
        val->type->type = FLOAT32;
    } else {
        if ((left->type->type != INT32 && left->type->type != UNSIGNED32) ||
            (right->type->type != INT32 && right->type->type != UNSIGNED32)) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Can only add types "
                    "float, int, uint\n",
                    token->lineNum, token->colNum);
            free(val->type);
            free(val);
            return NULL;
        }
        *(val->value) =
            LLVMBuildAdd(module->builder, *(left->value), *(right->value), "");
        val->type->type = INT32;
    }
    return val;
}

struct ValueData *generateSubtract(struct Token *token,
                                   struct ModuleData *module, size_t exprLen) {
    if (exprLen < 3) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too few arguments for "
                "subtraction - expected 3 arguments\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    if (exprLen > 3) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too many arguments for "
                "subtraction - expected 3 arguments\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    struct ValueData *left =
        generateToken(((struct Token **)token->data)[1], module, false, true);
    if (left == NULL) {
        return NULL;
    }
    struct ValueData *right =
        generateToken(((struct Token **)token->data)[2], module, false, true);
    if (right == NULL) {
        return NULL;
    }
    struct ValueData *val = malloc(sizeof(struct ValueData));
    val->value = malloc(sizeof(LLVMValueRef));
    val->type = malloc(sizeof(struct TypeData));
    val->type->length = -1;
    if (left->type->type == FLOAT32 || right->type->type == FLOAT32) {
        if (left->type->type != FLOAT32) {
            if (left->type->type == INT32) {
                *(left->value) = LLVMBuildSIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during subtraction, "
                       "possible lost of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == UNSIGNED32) {
                *(left->value) = LLVMBuildUIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during subtraction, "
                       "possible lost of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only subtract types "
                    "float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        if (right->type->type != FLOAT32) {
            if (right->type->type == INT32) {
                *(right->value) = LLVMBuildSIToFP(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during subtraction, "
                       "possible lost of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == UNSIGNED32) {
                *(right->value) = LLVMBuildUIToFP(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during subtraction, "
                       "possible lost of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only subtract types "
                    "float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        *(val->value) =
            LLVMBuildFSub(module->builder, *(left->value), *(right->value), "");
        val->type->type = FLOAT32;
    } else {
        if ((left->type->type != INT32 && left->type->type != UNSIGNED32) ||
            (right->type->type != INT32 && right->type->type != UNSIGNED32)) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Can only subtract types "
                    "float, int, uint\n",
                    token->lineNum, token->colNum);
            free(val->type);
            free(val);
            return NULL;
        }
        *(val->value) =
            LLVMBuildSub(module->builder, *(left->value), *(right->value), "");
        val->type->type = INT32;
    }
    return val;
}

struct ValueData *generateMultiply(struct Token *token,
                                   struct ModuleData *module, size_t exprLen) {
    if (exprLen < 3) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too few arguments for "
                "multiplication - expected 3 arguments\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    if (exprLen > 3) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too many arguments for "
                "multiplication - expected 3 arguments\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    struct ValueData *left =
        generateToken(((struct Token **)token->data)[1], module, false, true);
    if (left == NULL) {
        return NULL;
    }
    struct ValueData *right =
        generateToken(((struct Token **)token->data)[2], module, false, true);
    if (right == NULL) {
        return NULL;
    }
    struct ValueData *val = malloc(sizeof(struct ValueData));
    val->value = malloc(sizeof(LLVMValueRef));
    val->type = malloc(sizeof(struct TypeData));
    val->type->length = -1;
    if (left->type->type == FLOAT32 || right->type->type == FLOAT32) {
        if (left->type->type != FLOAT32) {
            if (left->type->type == INT32) {
                *(left->value) = LLVMBuildSIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf(
                    "WARNING on line %llu column %llu: Converting an "
                    "unsigned integer type to a float during multiplication, "
                    "possible lost of data\n",
                    token->lineNum, token->colNum);
            } else if (left->type->type == UNSIGNED32) {
                *(left->value) = LLVMBuildUIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during multiplication, "
                       "possible lost of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only multiply types "
                    "float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        if (right->type->type != FLOAT32) {
            if (right->type->type == INT32) {
                *(right->value) = LLVMBuildSIToFP(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during multiplication, "
                       "possible lost of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == UNSIGNED32) {
                *(right->value) = LLVMBuildUIToFP(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf(
                    "WARNING on line %llu column %llu: Converting an "
                    "unsigned integer type to a float during multiplication, "
                    "possible lost of data\n",
                    token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only multiply types "
                    "float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        *(val->value) =
            LLVMBuildFMul(module->builder, *(left->value), *(right->value), "");
        val->type->type = FLOAT32;
    } else {
        if ((left->type->type != INT32 && left->type->type != UNSIGNED32) ||
            (right->type->type != INT32 && right->type->type != UNSIGNED32)) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Can only multiply types "
                    "float, int, uint\n",
                    token->lineNum, token->colNum);
            free(val->type);
            free(val);
            return NULL;
        }
        *(val->value) =
            LLVMBuildMul(module->builder, *(left->value), *(right->value), "");
        val->type->type = INT32;
    }
    return val;
}

struct ValueData *generateDivide(struct Token *token, struct ModuleData *module,
                                 size_t exprLen) {
    if (exprLen < 3) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too few arguments for "
                "multiplication - expected 3 arguments\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    if (exprLen > 3) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too many arguments for "
                "multiplication - expected 3 arguments\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    struct ValueData *left =
        generateToken(((struct Token **)token->data)[1], module, false, true);
    if (left == NULL) {
        return NULL;
    }
    struct ValueData *right =
        generateToken(((struct Token **)token->data)[2], module, false, true);
    if (right == NULL) {
        return NULL;
    }
    struct ValueData *val = malloc(sizeof(struct ValueData));
    val->type = malloc(sizeof(struct TypeData));
    val->type->length = -1;
    if (left->type->type == FLOAT32 || right->type->type == FLOAT32) {
        if (left->type->type != FLOAT32) {
            if (left->type->type == INT32) {
                *(left->value) = LLVMBuildSIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during division, "
                       "possible lost of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == UNSIGNED32) {
                *(left->value) = LLVMBuildUIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during division, possible lost "
                       "of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Can only divide types "
                        "float, int, uint\n",
                        token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        if (right->type->type != FLOAT32) {
            if (right->type->type == INT32) {
                *(right->value) = LLVMBuildSIToFP(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during division, possible lost "
                       "of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == UNSIGNED32) {
                *(right->value) = LLVMBuildUIToFP(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during division, "
                       "possible lost of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Can only divide types "
                        "float, int, uint\n",
                        token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        *(val->value) =
            LLVMBuildFDiv(module->builder, *(left->value), *(right->value), "");
        val->type->type = FLOAT32;
    } else if (left->type->type == INT32 || right->type->type == INT32) {
        if (left->type->type != INT32) {
            if (left->type->type != UNSIGNED32) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Can only divide types "
                        "float, int, uint\n",
                        token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
            printf("WARNING on line %llu column %llu: Converting an unsigned "
                   "integer to an integer during division, possible lost of "
                   "data\n",
                   token->lineNum, token->colNum);
        }
        if (right->type->type != INT32) {
            if (right->type->type != UNSIGNED32) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Can only divide types "
                        "float, int, uint\n",
                        token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
            printf("WARNING on line %llu column %llu: Converting an unsigned "
                   "integer to an integer during division, possible lost of "
                   "data\n",
                   token->lineNum, token->colNum);
        }
        *(val->value) =
            LLVMBuildSDiv(module->builder, *(left->value), *(right->value), "");
        val->type->type = INT32;
    } else {
        if ((left->type->type != INT32 && left->type->type != UNSIGNED32) ||
            (right->type->type != INT32 && right->type->type != UNSIGNED32)) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Can only "
                    "divide types "
                    "float, int, uint\n",
                    token->lineNum, token->colNum);
            free(val->type);
            free(val);
            return NULL;
        }
        if (left->type->type != UNSIGNED32) {
            if (left->type->type != INT32) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Can only "
                        "divide types "
                        "float, int, uint\n",
                        token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
            printf("WARNING on line %llu column %llu: Converting an "
                   "integer to an unsigned integer during division, possible "
                   "lost of "
                   "data\n",
                   token->lineNum, token->colNum);
        }
        if (right->type->type != UNSIGNED32) {
            if (right->type->type != INT32) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Can only divide types "
                        "float, int, uint\n",
                        token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
            printf("WARNING on line %llu column %llu: Converting an integer "
                   "to an unsigned integer during division, possible lost of "
                   "data\n",
                   token->lineNum, token->colNum);
        }
        *(val->value) =
            LLVMBuildUDiv(module->builder, *(left->value), *(right->value), "");
        val->type->type = UNSIGNED32;
    }
    return val;
}

struct ValueData *generateEquals(struct Token *token, struct ModuleData *module,
                                 size_t exprLen) {
    if (exprLen < 3) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too few arguments for "
                "equals operation - expected 3 arguments\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    if (exprLen > 3) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too many arguments for "
                "equals operation - expected 3 arguments\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    struct ValueData *left =
        generateToken(((struct Token **)token->data)[1], module, false, true);
    if (left == NULL) {
        return NULL;
    }
    struct ValueData *right =
        generateToken(((struct Token **)token->data)[2], module, false, true);
    if (right == NULL) {
        return NULL;
    }
    struct ValueData *val = malloc(sizeof(struct ValueData));
    val->value = malloc(sizeof(LLVMValueRef));
    val->type = malloc(sizeof(struct TypeData));
    val->type->length = -1;
    if (left->type->type == FLOAT32 || right->type->type == FLOAT32) {
        if (left->type->type != FLOAT32) {
            if (left->type->type == INT32) {
                *(left->value) = LLVMBuildSIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf(
                    "WARNING on line %llu column %llu: Converting an "
                    "unsigned integer type to a float during equals operation, "
                    "possible lost of data\n",
                    token->lineNum, token->colNum);
            } else if (left->type->type == UNSIGNED32) {
                *(left->value) = LLVMBuildUIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during equals operation, "
                       "possible lost of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only compare types "
                    "float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        if (right->type->type != FLOAT32) {
            if (right->type->type == INT32) {
                *(right->value) = LLVMBuildSIToFP(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during equals operation, "
                       "possible lost of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == UNSIGNED32) {
                *(right->value) = LLVMBuildUIToFP(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf(
                    "WARNING on line %llu column %llu: Converting an "
                    "unsigned integer type to a float during equals operation, "
                    "possible lost of data\n",
                    token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only compare types "
                    "float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        *(val->value) = LLVMBuildFCmp(module->builder, LLVMRealOEQ,
                                      *(left->value), *(right->value), "");
        val->type->type = BOOL;
    } else {
        if ((left->type->type != INT32 && left->type->type != UNSIGNED32) ||
            (right->type->type != INT32 && right->type->type != UNSIGNED32)) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Can only compare types "
                    "float, int, uint\n",
                    token->lineNum, token->colNum);
            free(val->type);
            free(val);
            return NULL;
        }
        *(val->value) = LLVMBuildICmp(module->builder, LLVMIntEQ,
                                      *(left->value), *(right->value), "");
        val->type->type = BOOL;
    }
    return val;
}

struct ValueData *generateLessThan(struct Token *token,
                                   struct ModuleData *module, size_t exprLen) {
    if (exprLen < 3) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too few arguments for "
                "less than operation - expected 3 arguments\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    if (exprLen > 3) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too many arguments for "
                "less than operation - expected 3 arguments\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    struct ValueData *left =
        generateToken(((struct Token **)token->data)[1], module, false, true);
    if (left == NULL) {
        return NULL;
    }
    struct ValueData *right =
        generateToken(((struct Token **)token->data)[2], module, false, true);
    if (right == NULL) {
        return NULL;
    }
    struct ValueData *val = malloc(sizeof(struct ValueData));
    val->value = malloc(sizeof(LLVMValueRef));
    val->type = malloc(sizeof(struct TypeData));
    val->type->length = -1;
    if (left->type->type == FLOAT32 || right->type->type == FLOAT32) {
        if (left->type->type != FLOAT32) {
            if (left->type->type == INT32) {
                *(left->value) = LLVMBuildSIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during less than "
                       "operation, possible lost of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == UNSIGNED32) {
                *(left->value) = LLVMBuildUIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during less than operation, "
                       "possible lost of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only compare types "
                    "float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        if (right->type->type != FLOAT32) {
            if (right->type->type == INT32) {
                *(right->value) = LLVMBuildSIToFP(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during less than operation, "
                       "possible lost of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == UNSIGNED32) {
                *(right->value) = LLVMBuildUIToFP(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during less than "
                       "operation, possible lost of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only compare types "
                    "float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        *(val->value) = LLVMBuildFCmp(module->builder, LLVMRealOLT,
                                      *(left->value), *(right->value), "");
        val->type->type = BOOL;
    } else if (left->type->type == INT32 || right->type->type == INT32) {
        if (left->type->type != INT32) {
            if (left->type->type != UNSIGNED32) {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only compare types "
                    "float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
            printf("WARNING on line %llu column %llu: Converting an unsigned "
                   "integer to an integer during less than operation, possible "
                   "lost of data\n",
                   token->lineNum, token->colNum);
        }
        if (right->type->type != INT32) {
            if (right->type->type != UNSIGNED32) {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only compare types "
                    "float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
            printf("WARNING on line %llu column %llu: Converting an unsigned "
                   "integer to an integer during less than operation, possible "
                   "lost of data\n",
                   token->lineNum, token->colNum);
        }
        *(val->value) = LLVMBuildICmp(module->builder, LLVMIntSLT,
                                      *(left->value), *(right->value), "");
        val->type->type = BOOL;
    } else {
        if ((left->type->type != INT32 && left->type->type != UNSIGNED32) ||
            (right->type->type != INT32 && right->type->type != UNSIGNED32)) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Can only "
                    "compare types float, int, uint\n",
                    token->lineNum, token->colNum);
            free(val->type);
            free(val);
            return NULL;
        }
        if (left->type->type != UNSIGNED32) {
            if (left->type->type != INT32) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Can only "
                        "compare types float, int, uint\n",
                        token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
            printf("WARNING on line %llu column %llu: Converting an "
                   "integer to an unsigned integer during less than operation, "
                   "possible lost of data\n",
                   token->lineNum, token->colNum);
        }
        if (right->type->type != UNSIGNED32) {
            if (right->type->type != INT32) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Can only divide types "
                        "float, int, uint\n",
                        token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
            printf("WARNING on line %llu column %llu: Converting an integer "
                   "to an unsigned integer during less than operation, "
                   "possible lost of data\n",
                   token->lineNum, token->colNum);
        }
        *(val->value) = LLVMBuildICmp(module->builder, LLVMIntULT,
                                      *(left->value), *(right->value), "");
        val->type->type = BOOL;
    }
    return val;
}

struct ValueData *generateGreaterThan(struct Token *token,
                                      struct ModuleData *module,
                                      size_t exprLen) {
    if (exprLen < 3) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too few arguments for "
                "less greater operation - expected 3 arguments\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    if (exprLen > 3) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too many arguments for "
                "less greater operation - expected 3 arguments\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    struct ValueData *left =
        generateToken(((struct Token **)token->data)[1], module, false, true);
    if (left == NULL) {
        return NULL;
    }
    struct ValueData *right =
        generateToken(((struct Token **)token->data)[2], module, false, true);
    if (right == NULL) {
        return NULL;
    }
    struct ValueData *val = malloc(sizeof(struct ValueData));
    val->value = malloc(sizeof(LLVMValueRef));
    val->type = malloc(sizeof(struct TypeData));
    val->type->length = -1;
    if (left->type->type == FLOAT32 || right->type->type == FLOAT32) {
        if (left->type->type != FLOAT32) {
            if (left->type->type == INT32) {
                *(left->value) = LLVMBuildSIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during greater than "
                       "operation, possible lost of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == UNSIGNED32) {
                *(left->value) = LLVMBuildUIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during greater than operation, "
                       "possible lost of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only compare types "
                    "float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        if (right->type->type != FLOAT32) {
            if (right->type->type == INT32) {
                *(right->value) = LLVMBuildSIToFP(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during greater than operation, "
                       "possible lost of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == UNSIGNED32) {
                *(right->value) = LLVMBuildUIToFP(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during greater than "
                       "operation, possible lost of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only compare types "
                    "float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        *(val->value) = LLVMBuildFCmp(module->builder, LLVMRealOGT,
                                      *(left->value), *(right->value), "");
        val->type->type = BOOL;
    } else if (left->type->type == INT32 || right->type->type == INT32) {
        if (left->type->type != INT32) {
            if (left->type->type != UNSIGNED32) {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only compare types "
                    "float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
            printf(
                "WARNING on line %llu column %llu: Converting an unsigned "
                "integer to an integer during greater than operation, possible "
                "lost of data\n",
                token->lineNum, token->colNum);
        }
        if (right->type->type != INT32) {
            if (right->type->type != UNSIGNED32) {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only compare types "
                    "float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
            printf(
                "WARNING on line %llu column %llu: Converting an unsigned "
                "integer to an integer during greater than operation, possible "
                "lost of data\n",
                token->lineNum, token->colNum);
        }
        *(val->value) = LLVMBuildICmp(module->builder, LLVMIntSGT,
                                      *(left->value), *(right->value), "");
        val->type->type = BOOL;
    } else {
        if ((left->type->type != INT32 && left->type->type != UNSIGNED32) ||
            (right->type->type != INT32 && right->type->type != UNSIGNED32)) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Can only "
                    "compare types float, int, uint\n",
                    token->lineNum, token->colNum);
            free(val->type);
            free(val);
            return NULL;
        }
        if (left->type->type != UNSIGNED32) {
            if (left->type->type != INT32) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Can only "
                        "compare types float, int, uint\n",
                        token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
            printf(
                "WARNING on line %llu column %llu: Converting an "
                "integer to an unsigned integer during greater than operation, "
                "possible lost of data\n",
                token->lineNum, token->colNum);
        }
        if (right->type->type != UNSIGNED32) {
            if (right->type->type != INT32) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Can only divide types "
                        "float, int, uint\n",
                        token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
            printf("WARNING on line %llu column %llu: Converting an integer "
                   "to an unsigned integer during greater than operation, "
                   "possible lost of data\n",
                   token->lineNum, token->colNum);
        }
        *(val->value) = LLVMBuildICmp(module->builder, LLVMIntUGT,
                                      *(left->value), *(right->value), "");
        val->type->type = BOOL;
    }
    return val;
}

struct ValueData *generateBlock(struct Token *token, struct ModuleData *module,
                                size_t exprLen, size_t startTokenIdx) {
    for (size_t i = startTokenIdx; i < exprLen; i++) {
        struct ValueData *val = generateToken(((struct Token **)token->data)[i],
                                              module, false, true);
        if (val == NULL) {
            return NULL;
        }
        if (i == exprLen - 1) {
            return val;
        }
        free(val);
    }
    fprintf(stderr, "COMPILER ERROR on line %llu column %llu\n", token->lineNum,
            token->colNum);
    return NULL;
}

struct ValueData *generateSizeof(struct Token *token, struct ModuleData *module,
                                 size_t exprLen) {
    if (exprLen < 2) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too few arguments for "
                "sizeof - expected at 2 arguments - (sizeof "
                "<type>)\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    if (((struct Token **)token->data)[1]->type != IDENT_TOKEN &&
        ((struct Token **)token->data)[1]->type != EXPR_TOKEN &&
        ((struct Token **)token->data)[1]->type != REF_TOKEN &&
        ((struct Token **)token->data)[1]->type != DE_REF_TOKEN) {
        fprintf(stderr, "ERROR on line %llu column %llu: Expected a type\n",
                ((struct Token **)token->data)[2]->lineNum,
                ((struct Token **)token->data)[2]->colNum);
        return NULL;
    }
    struct TypeData *type = getType(((struct Token **)token->data)[1], module);
    LLVMTypeRef *llvmType = generateType(type, module);
    struct ValueData *val = malloc(sizeof(struct ValueData));
    val->value = malloc(sizeof(LLVMValueRef));
    val->type = malloc(sizeof(struct ValueData));
    *(val->value) = LLVMSizeOf(*llvmType);
    val->type->type = UNSIGNED64;
    val->type->length = -1;
    free(type);
    free(llvmType);
    return val;
}

struct ValueData *generateWhen(struct Token *token, struct ModuleData *module,
                               size_t exprLen, bool negate) {
    if (exprLen < 3) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too few arguments for "
                "when statement - expected at least 3 arguments - (when "
                "<condition> <body1> ...)\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    LLVMValueRef *cond = generateTokenOfType(
        ((struct Token **)token->data)[1],
        (struct TypeData){BOOL, NULL, NULL, -1, NULL}, module);
    if (cond == NULL) {
        return NULL;
    }

    LLVMBasicBlockRef thenBlock = LLVMAppendBasicBlockInContext(
        module->context, module->mainFunc, "then");
    LLVMBasicBlockRef mergeBlock = LLVMAppendBasicBlockInContext(
        module->context, module->mainFunc, "merge");
    LLVMBasicBlockRef lastBlock = module->currentBlock;

    LLVMPositionBuilderAtEnd(module->builder, thenBlock);
    module->currentBlock = thenBlock;
    struct ValueData *block = generateBlock(token, module, exprLen, 2);
    if (block == NULL) {
        return NULL;
    }
    LLVMTypeRef *blockLlvmType = generateType(block->type, module);
    if (blockLlvmType == NULL) {
        return NULL;
    }
    LLVMPositionBuilderAtEnd(module->builder, module->allocaBlock);
    LLVMValueRef boolValue = LLVMBuildAlloca(
        module->builder, LLVMInt1TypeInContext(module->context), "");
    LLVMValueRef blockValue =
        LLVMBuildAlloca(module->builder, *blockLlvmType, "");
    LLVMPositionBuilderAtEnd(module->builder, lastBlock);
    LLVMBuildStore(module->builder,
                   LLVMConstInt(LLVMInt1TypeInContext(module->context), 1, 0),
                   boolValue);
    LLVMBuildStore(module->builder, LLVMConstNull(*blockLlvmType), blockValue);
    if (negate) {
        LLVMBuildCondBr(module->builder, *cond, mergeBlock, thenBlock);
    } else {
        LLVMBuildCondBr(module->builder, *cond, thenBlock, mergeBlock);
    }
    LLVMPositionBuilderAtEnd(module->builder, module->currentBlock);
    LLVMBuildStore(module->builder,
                   LLVMConstInt(LLVMInt1TypeInContext(module->context), 0, 0),
                   boolValue);
    LLVMBuildStore(module->builder, *(block->value), blockValue);
    LLVMBuildBr(module->builder, mergeBlock);

    LLVMPositionBuilderAtEnd(module->builder, mergeBlock);
    module->currentBlock = mergeBlock;
    LLVMTypeRef llvmRetType = LLVMStructTypeInContext(
        module->context,
        (LLVMTypeRef[]){*blockLlvmType, LLVMInt1TypeInContext(module->context)},
        2, 0);
    LLVMValueRef loadedBoolVal = LLVMBuildLoad2(
        module->builder, LLVMInt1TypeInContext(module->context), boolValue, "");
    LLVMValueRef loadedBlockVal =
        LLVMBuildLoad2(module->builder, *blockLlvmType, blockValue, "");
    LLVMPositionBuilderAtEnd(module->builder, module->allocaBlock);
    LLVMValueRef retValPtr = LLVMBuildAlloca(module->builder, llvmRetType, "");
    LLVMPositionBuilderAtEnd(module->builder, module->currentBlock);
    LLVMValueRef e1 =
        LLVMBuildStructGEP2(module->builder, llvmRetType, retValPtr, 0, "");
    LLVMValueRef e2 =
        LLVMBuildStructGEP2(module->builder, llvmRetType, retValPtr, 1, "");
    LLVMBuildStore(module->builder, loadedBlockVal, e1);
    LLVMBuildStore(module->builder, loadedBoolVal, e2);
    LLVMValueRef retVal =
        LLVMBuildLoad2(module->builder, llvmRetType, retValPtr, "");

    struct ValueData *ret = malloc(sizeof(struct ValueData));
    ret->value = malloc(sizeof(LLVMValueRef));
    *(ret->value) = retVal;
    ret->type = malloc(sizeof(struct TypeData));
    ret->type->type = NULLABLE;
    ret->type->length = -1;
    ret->type->otherType = block->type;
    return ret;
}

struct ValueData *generateExpr(struct Token *token, struct ModuleData *module) {
    int exprLen = stbds_arrlen((struct Token **)token->data);
    if (exprLen == 0) {
        fprintf(stderr, "ERROR on line %llu column %llu: Empty expression\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    if (((struct Token **)token->data)[0]->type != IDENT_TOKEN) {
        if (((struct Token **)token->data)[0]->type == REF_TOKEN) {
            if (((struct Token **)token->data)[0]->data == NULL) {
                return generateMultiply(token, module, exprLen);
            }
        }
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected an identifier\n",
                ((struct Token **)token->data)[0]->lineNum,
                ((struct Token **)token->data)[0]->colNum);
        return NULL;
    }
    char *funcName = (char *)((struct Token **)token->data)[0]->data;
    if (strcmp(funcName, "def") == 0) {
        LLVMValueRef *val = generateVarDef(token, module, exprLen);
        if (val == NULL) {
            return NULL;
        }
        struct ValueData *ret = malloc(sizeof(struct ValueData));
        ret->value = val;
        ret->type = malloc(sizeof(struct TypeData));
        ret->type->type = NIL;
        ret->type->length = -1;
        return ret;
    };
    if (strcmp(funcName, "extern-fn") == 0) {
        LLVMValueRef *val = generateFuncDeclare(token, module, exprLen);
        struct ValueData *ret = malloc(sizeof(struct ValueData));
        ret->value = val;
        ret->type = malloc(sizeof(struct TypeData));
        ret->type->type = NIL;
        ret->type->length = -1;
        return ret;
    };
    if (stbds_shgetp_null(module->functions, funcName) != NULL) {
        return generateFuncCall(token, module, exprLen);
    }
    if (strcmp(funcName, "+") == 0) {
        return generateAdd(token, module, exprLen);
    }
    if (strcmp(funcName, "-") == 0) {
        return generateSubtract(token, module, exprLen);
    }
    if (strcmp(funcName, "/") == 0) {
        return generateDivide(token, module, exprLen);
    }
    if (strcmp(funcName, "=") == 0) {
        return generateEquals(token, module, exprLen);
    }
    if (strcmp(funcName, "<") == 0) {
        return generateLessThan(token, module, exprLen);
    }
    if (strcmp(funcName, ">") == 0) {
        return generateGreaterThan(token, module, exprLen);
    }
    if (strcmp(funcName, "when") == 0) {
        return generateWhen(token, module, exprLen, false);
    }
    if (strcmp(funcName, "unless") == 0) {
        return generateWhen(token, module, exprLen, true);
    }
    if (strcmp(funcName, "sizeof") == 0) {
        return generateSizeof(token, module, exprLen);
    }
    if (stbds_shgetp_null(module->variables, funcName) != NULL) {
        struct VariableData var = stbds_shget(module->variables, funcName);
        if (var.type->type == NULLABLE) {
            if (exprLen < 2) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Too little arguments "
                        "for optional type function call\n",
                        token->lineNum, token->colNum);
                return NULL;
            }
            if (exprLen > 2) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Too many arguments "
                        "for optional type function call\n",
                        token->lineNum, token->colNum);
                return NULL;
            }
            funcName = (char *)((struct Token **)token->data)[1]->data;
            if (strcmp(funcName, "isNil") == 0) {
                // LLVMValueRef valPtr = LLVMBuildStructGEP2(
                //     module->builder, *(var.llvmType), *(var.llvmVar), 1, "");
                // LLVMValueRef val = LLVMBuildLoad2(
                //     module->builder, LLVMInt1TypeInContext(module->context),
                //     valPtr, "");
                LLVMValueRef loadedVar = LLVMBuildLoad2(
                    module->builder, *(var.llvmType), *(var.llvmVar), "");
                LLVMValueRef val =
                    LLVMBuildExtractValue(module->builder, loadedVar, 1, "");

                struct ValueData *ret = malloc(sizeof(struct ValueData));
                ret->value = malloc(sizeof(LLVMValueRef));
                *(ret->value) = val;
                ret->type = malloc(sizeof(struct TypeData));
                ret->type->type = BOOL;
                ret->type->length = -1;
                return ret;
            }
            if (strcmp(funcName, "value") == 0) {
                // LLVMValueRef valPtr = LLVMBuildStructGEP2(
                //     module->builder, *(var.llvmType), *(var.llvmVar), 0, "");
                // LLVMTypeRef *llvmType =
                //     generateType(var.type->otherType, module);
                // if (llvmType == NULL) {
                // return NULL;
                // }
                // LLVMValueRef val =
                // LLVMBuildLoad2(module->builder, *llvmType, valPtr, "");
                LLVMValueRef loadedVar = LLVMBuildLoad2(
                    module->builder, *(var.llvmType), *(var.llvmVar), "");
                LLVMValueRef val =
                    LLVMBuildExtractValue(module->builder, loadedVar, 0, "");

                struct ValueData *ret = malloc(sizeof(struct ValueData));
                ret->value = malloc(sizeof(LLVMValueRef));
                *(ret->value) = val;
                ret->type = var.type->otherType;
                return ret;
            }
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Unknown function for "
                    "optional type \"%s\"\n",
                    token->lineNum, token->colNum, funcName);
            return NULL;
        }
    }
    fprintf(stderr, "ERROR on line %llu column %llu: Unknown function \"%s\"\n",
            token->lineNum, token->colNum, funcName);
    return NULL;
}

struct ValueData *generateIdent(struct Token *token, struct ModuleData *module,
                                bool falseInsteadOfNil) {
    char *name = (char *)token->data;
    if (stbds_shgetp_null(module->variables, name) != NULL) {
        struct VariableData var = stbds_shget(module->variables, name);
        LLVMValueRef *loadedVar = malloc(sizeof(LLVMValueRef));
        if (var.type->type == STRING) {
            LLVMValueRef dataPtrField = LLVMBuildStructGEP2(
                module->builder,
                LLVMStructTypeInContext(
                    module->context,
                    (LLVMTypeRef[]){
                        LLVMPointerType(LLVMInt8TypeInContext(module->context),
                                        0),
                        LLVMInt32TypeInContext(module->context),
                        LLVMInt32TypeInContext(module->context)},
                    3, 0),
                *var.llvmVar, 0, "");
            *loadedVar = LLVMBuildLoad2(
                module->builder,
                LLVMPointerType(LLVMInt8TypeInContext(module->context), 0),
                dataPtrField, "");
            struct ValueData *ret = malloc(sizeof(struct ValueData));
            ret->type = var.type;
            ret->value = loadedVar;
            return ret;
        }
        *loadedVar =
            LLVMBuildLoad2(module->builder, *var.llvmType, *var.llvmVar, "");
        struct ValueData *ret = malloc(sizeof(struct ValueData));
        ret->type = var.type;
        ret->value = loadedVar;
        return ret;
    }
    if (strcmp(name, "t") == 0) {
        struct ValueData *ret = malloc(sizeof(struct ValueData));
        ret->value = malloc(sizeof(LLVMValueRef));
        *(ret->value) =
            LLVMConstInt(LLVMInt1TypeInContext(module->context), 1, 0);
        ret->type = malloc(sizeof(struct TypeData));
        ret->type->type = BOOL;
        ret->type->length = -1;
        return ret;
    }
    if (strcmp(name, "nil") == 0) {
        if (falseInsteadOfNil) {
            struct ValueData *ret = malloc(sizeof(struct ValueData));
            ret->value = malloc(sizeof(LLVMValueRef));
            *(ret->value) =
                LLVMConstInt(LLVMInt1TypeInContext(module->context), 0, 0);
            ret->type = malloc(sizeof(struct TypeData));
            ret->type->type = BOOL;
            ret->type->length = -1;
            return ret;
        }
        struct ValueData *ret = malloc(sizeof(struct ValueData));
        ret->value = malloc(sizeof(LLVMValueRef));
        *(ret->value) = NULL;
        ret->type = malloc(sizeof(struct TypeData));
        ret->type->type = NIL;
        ret->type->length = -1;
        return ret;
    }
    fprintf(stderr, "ERROR on line %llu column %llu: Unknow identifier\n",
            token->lineNum, token->colNum);
    return NULL;
}

struct ValueData *generateDeRef(struct Token *token,
                                struct ModuleData *module) {
    struct Token *other = ((struct Token **)token->data)[0];
    struct ValueData *val = generateToken(other, module, false, true);
    if (val->type->type != POINTER) {
        fprintf(
            stderr,
            "ERROR on line %llu column %llu: Can only dereference a pointer\n",
            token->lineNum, token->colNum);
        return NULL;
    }
    LLVMTypeRef *llvmType = generateType(val->type, module);
    if (llvmType == NULL) {
        return NULL;
    }
    struct ValueData *newVal = malloc(sizeof(struct ValueData));
    newVal->value = malloc(sizeof(LLVMValueRef));
    *(newVal->value) =
        LLVMBuildLoad2(module->builder, *llvmType, *(val->value), "");
    newVal->type = val->type->otherType;
    return newVal;
}

struct ValueData *generateRef(struct Token *token, struct ModuleData *module) {
    struct Token *other = ((struct Token **)token->data)[0];
    struct ValueData *val = generateToken(other, module, false, true);
    LLVMTypeRef *llvmType = generateType(val->type, module);
    if (llvmType == NULL) {
        return NULL;
    }
    struct ValueData *newVal = malloc(sizeof(struct ValueData));
    newVal->value = malloc(sizeof(LLVMValueRef));
    *(newVal->value) =
        LLVMBuildAlloca(module->builder, LLVMPointerType(*llvmType, 0), "");
    LLVMBuildStore(module->builder, *(val->value), *(newVal->value));
    newVal->type = malloc(sizeof(struct TypeData));
    newVal->type->type = POINTER;
    newVal->type->length = -1;
    newVal->type->otherType = val->type;
    return newVal;
}

LLVMValueRef *generateInt(struct Token *token, struct ModuleData *module) {
    LLVMValueRef *val = malloc(sizeof(LLVMValueRef));
    *val = LLVMConstInt(LLVMInt32TypeInContext(module->context),
                        (unsigned long long)(*(int *)token->data),
                        *(int *)token->data < 0);
    return val;
}

LLVMValueRef *generateFloat(struct Token *token, struct ModuleData *module) {
    LLVMValueRef *val = malloc(sizeof(LLVMValueRef));
    *val = LLVMConstReal(LLVMFloatTypeInContext(module->context),
                         (double)(*(float *)token->data));
    return val;
}

LLVMValueRef *generateCharPtr(struct Token *token, struct ModuleData *module) {
    LLVMValueRef *val = malloc(sizeof(LLVMValueRef));
    LLVMPositionBuilderAtEnd(module->builder, module->allocaBlock);
    *val = LLVMBuildAlloca(
        module->builder,
        LLVMPointerType(LLVMInt8TypeInContext(module->context), 0), "");
    LLVMPositionBuilderAtEnd(module->builder, module->currentBlock);
    LLVMBuildStore(
        module->builder,
        LLVMConstString((char *)token->data, strlen((char *)token->data), 0),
        *val);
    return val;
}

struct ValueData *generateToken(struct Token *token, struct ModuleData *module,
                                bool charPtrInsteadOfString,
                                bool falseInsteadOfNil) {
    struct ValueData *ret = malloc(sizeof(struct ValueData));
    LLVMValueRef *val;
    switch (token->type) {
    case INT_TOKEN:
        LLVMValueRef *val = generateInt(token, module);
        if (val == NULL) {
            return NULL;
        }
        ret->value = val;
        ret->type = malloc(sizeof(struct TypeData));
        ret->type->type = INT32;
        ret->type->length = -1;
        return ret;

    case FLOAT_TOKEN:
        val = generateFloat(token, module);
        ret->value = val;
        ret->type = malloc(sizeof(struct TypeData));
        ret->type->type = FLOAT32;
        ret->type->length = -1;
        return ret;

    case STRING_TOKEN:
        if (charPtrInsteadOfString) {
            val = generateCharPtr(token, module);
            ret->value = val;
            ret->type = malloc(sizeof(struct TypeData));
            ret->type->type = POINTER;
            ret->type->length = -1;
            ret->type->otherType = malloc(sizeof(struct TypeData));
            ret->type->otherType->type = CHAR;
            ret->type->otherType->length = -1;
            return ret;
        }
        val = generateString(token, module, "");
        ret->value = val;
        ret->type = malloc(sizeof(struct TypeData));
        ret->type->type = STRING;
        ret->type->length = -1;
        return ret;

    case IDENT_TOKEN:
        return generateIdent(token, module, falseInsteadOfNil);

    case EXPR_TOKEN:
        return generateExpr(token, module);

    case DE_REF_TOKEN:
        return generateDeRef(token, module);

    case REF_TOKEN:
        return generateRef(token, module);

    default:
        free(ret);
        fprintf(stderr,
                "Syntax error on line %llu column %llu: Invalid token.\n",
                token->lineNum, token->colNum);
        return NULL;
    }
}

bool cmptype(struct TypeData *cmpType, struct TypeData *type, size_t lineNum,
             size_t colNum, bool printError) {
    if (type->type == POINTER) {
        if (cmpType->type != POINTER) {
            if (printError) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Expected a "
                        "pointer type\n",
                        lineNum, colNum);
            }
            return false;
        }
        bool res = cmptype(cmpType->otherType, type->otherType, lineNum, colNum,
                           false);
        if (!res) {
            if (printError) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Expected a different "
                        "pointer type\n",
                        lineNum, colNum);
            }
            return false;
        }
        return true;
    }
    if (type->type == CLASS) {
        if (cmpType->type != CLASS) {
            if (printError) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Expected a class "
                        "\"%s\"\n",
                        lineNum, colNum, type->name);
            }
            return false;
        }
        bool res = strcmp(type->name, cmpType->name) == 0;
        if (!res && printError) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Expected a class \"%s\"\n",
                    lineNum, colNum, type->name);
        }
        return res;
    }
    if (type->type == ARRAY) {
        if (cmpType->type != ARRAY) {
            if (printError) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Expected an array\n",
                        lineNum, colNum);
            }
            return false;
        }
        bool res = cmptype(cmpType->otherType, type->otherType, lineNum, colNum,
                           false);
        if (!res && printError) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Expected a different type "
                    "array\n",
                    lineNum, colNum);
        }
        if (type->length != cmpType->length) {
            if (printError) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Expected a %d long "
                        "array\n",
                        lineNum, colNum, type->length);
            }
            return false;
        }
        return res;
    }
    if (type->type == VECTOR) {
        if (cmpType->type != VECTOR) {
            if (printError) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Expected a vector\n",
                        lineNum, colNum);
            }
            return false;
        }
        bool res = cmptype(cmpType->otherType, type->otherType, lineNum, colNum,
                           false);
        if (!res && printError) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Expected a different type "
                    "vector\n",
                    lineNum, colNum);
        }
        return res;
    }
    if (type->type == NULLABLE) {
        if (cmpType->type != NULLABLE) {
            if (printError) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Expected a nullable "
                        "type\n",
                        lineNum, colNum);
            }
            return false;
        }
        bool res = cmptype(cmpType->otherType, type->otherType, lineNum, colNum,
                           false);
        if (!res && printError) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Expected a different type "
                    "for optional type\n",
                    lineNum, colNum);
        }
        return res;
    }
    if (type->type == MAP) {
        if (cmpType->type != MAP) {
            if (printError) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Expected a map\n",
                        lineNum, colNum);
            }
            return false;
        }
        bool res = cmptype(cmpType->otherType, type->otherType, lineNum, colNum,
                           false);
        bool res2 = cmptype(cmpType->otherType2, type->otherType2, lineNum,
                            colNum, false);
        if (!res || !res2) {
            if (printError) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Expected a different "
                        "type map\n",
                        lineNum, colNum);
            }
            return false;
        }
        return true;
    }
    if (type->type != cmpType->type && printError && type->type == INT32) {
        fprintf(stderr, "ERROR on line %llu column %llu: Expected an integer\n",
                lineNum, colNum);
    }
    if (type->type != cmpType->type && printError && type->type == UNSIGNED32) {
        fprintf(
            stderr,
            "ERROR on line %llu column %llu: Expected an unsigned integer\n",
            lineNum, colNum);
    }
    if (type->type != cmpType->type && printError && type->type == FLOAT32) {
        fprintf(stderr, "ERROR on line %llu column %llu: Expected a float\n",
                lineNum, colNum);
    }
    if (type->type != cmpType->type && printError && type->type == BOOL) {
        fprintf(stderr, "ERROR on line %llu column %llu: Expected a bool\n",
                lineNum, colNum);
    }
    if (type->type != cmpType->type && printError && type->type == CHAR) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected a character\n",
                lineNum, colNum);
    }
    if (type->type != cmpType->type && printError && type->type == STRING) {
        fprintf(stderr, "ERROR on line %llu column %llu: Expected a string\n",
                lineNum, colNum);
    }
    if (type->type != cmpType->type && printError && type->type == NIL) {
        fprintf(stderr, "ERROR on line %llu column %llu: Expected a nil type\n",
                lineNum, colNum);
    }
    return type->type == cmpType->type;
}

LLVMValueRef *generateTokenOfType(struct Token *token, struct TypeData type,
                                  struct ModuleData *module) {
    struct ValueData *val =
        generateToken(token, module, type.type != STRING, type.type == BOOL);
    if (val == NULL) {
        return NULL;
    }
    if (val->type->type == NIL) {
        LLVMTypeRef *llvmType = generateType(&type, module);
        if (llvmType == NULL) {
            return NULL;
        }
        *(val->value) = LLVMConstNull(*llvmType);
        return val->value;
    }
    if (!cmptype(val->type, &type, token->lineNum, token->colNum, true)) {
        return NULL;
    }
    return val->value;
}

void stdInit(struct ModuleData *module) {
    mallocType = LLVMFunctionType(LLVMPointerType(LLVMInt8Type(), 0),
                                  (LLVMTypeRef[]){LLVMInt64Type()}, 1, 0);
    mallocFunc = LLVMAddFunction(module->module, "malloc", mallocType);
}

int generate(struct Token *body, const char *filename,
             const char *inputFilename) {
    LLVMContextRef context = LLVMContextCreate();
    LLVMModuleRef module =
        LLVMModuleCreateWithNameInContext(inputFilename, context);
    LLVMSetSourceFileName(module, inputFilename, strlen(inputFilename));
    LLVMBuilderRef builder = LLVMCreateBuilderInContext(context);

    LLVMTypeRef mainType = LLVMFunctionType(LLVMInt32Type(), NULL, 0, 0);
    LLVMValueRef mainFunc = LLVMAddFunction(module, "main", mainType);
    LLVMBasicBlockRef allocaBlock = LLVMAppendBasicBlock(mainFunc, "alloca");
    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(mainFunc, "entry");

    struct ModuleData moduleData = {builder,     context, module, mainFunc,
                                    allocaBlock, entry,   NULL,   NULL,
                                    NULL,        0};

    stdInit(&moduleData);

    LLVMPositionBuilderAtEnd(builder, entry);
    size_t numTokens = stbds_arrlen((struct Token **)body->data);
    for (size_t i = 0; i < numTokens; i++) {
        struct ValueData *val = generateToken(((struct Token **)body->data)[i],
                                              &moduleData, false, false);
        if (val == NULL) {
            return 1;
        }
        free(val);
    }

    LLVMBuildRet(builder, LLVMConstInt(LLVMInt32TypeInContext(context), 0, 0));
    LLVMPositionBuilderAtEnd(builder, allocaBlock);
    LLVMBuildBr(builder, entry);

    char *outString = LLVMPrintModuleToString(module);
    printf("%s\n", outString);
    LLVMDisposeMessage(outString);

    LLVMPrintModuleToFile(module, filename, NULL);
    LLVMDisposeBuilder(builder);
    LLVMDisposeModule(module);
    LLVMContextDispose(context);
    return 0;
}
