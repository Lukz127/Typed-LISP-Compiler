#include <generate.h>

LLVMTypeRef mallocType;
LLVMValueRef mallocFunc;
LLVMValueRef abortFunc;

LLVMTypeRef *generateType(struct TypeData *type, struct ModuleData *module);
struct ValueData *generateToken(struct Token *token, struct ModuleData *module,
                                bool charPtrInsteadOfString,
                                bool falseInsteadOfNil,
                                bool vectorInsteadOfArray,
                                bool doubleInsteadOfFloat);
LLVMValueRef *generateTokenOfType(struct Token *token, struct TypeData type,
                                  struct ModuleData *module);
bool cmptype(struct TypeData *cmpType, struct TypeData *expectedType,
             size_t lineNum, size_t colNum, bool printError);

bool cmpFunctionArgs(struct FuncData *func1, struct FuncData *func2) {
    if (func1->numArgs != func2->numArgs ||
        func1->isVarArg != func2->isVarArg ||
        (func1->restArg == NULL && func2->restArg != NULL) ||
        (func1->restArg != NULL && func2->restArg == NULL) ||
        (func1->retType == NULL && func2->retType != NULL) ||
        (func1->retType != NULL && func2->restArg == NULL)) {
        return false;
    }
    if (func1->retType != NULL) {
        if (!cmptype(func1->retType, func2->retType, 0, 0, false)) {
            return false;
        }
    }
    if (func1->restArg != NULL) {
        if (!cmptype(func1->restArg->type, func2->restArg->type, 0, 0, false)) {
            return false;
        }
    }
    for (size_t i = 0; i < func1->numArgs; i++) {
        if (!cmptype(func1->args[i].type, func2->args[i].type, 0, 0, false)) {
            return false;
        }
    }
    return true;
}

LLVMValueRef *generateNil(LLVMTypeRef *llvmType, struct ModuleData *module) {
    LLVMValueRef *val = malloc(sizeof(LLVMValueRef));
    *val = LLVMConstNull(*llvmType);
    return val;
}

struct ValueData *generateBlock(struct Token *token, struct ModuleData *module,
                                size_t exprLen, size_t startTokenIdx,
                                bool *returned);

void generateFreeMallocedVars(struct ModuleData *module) {
    size_t numVars = stbds_arrlen(
        module->contexts[module->numContexts - 1]->mallocedVarsToFree);
    for (size_t i = 0; i < numVars; i++) {
        LLVMBuildFree(module->builder,
                      *(module->contexts[module->numContexts - 1]
                            ->mallocedVarsToFree[i]));
    }
}

LLVMBasicBlockRef generateOutOfBoundsErrorBlock(LLVMValueRef func,
                                                LLVMBasicBlockRef currentBlock,
                                                LLVMBuilderRef builder) {
    LLVMBasicBlockRef outOfBoundsErrorBlock =
        LLVMAppendBasicBlock(func, "outOfBounds");
    LLVMPositionBuilderAtEnd(builder, outOfBoundsErrorBlock);
    LLVMBuildCall2(builder, LLVMFunctionType(LLVMVoidType(), NULL, 0, 0),
                   abortFunc, NULL, 0, "");
    LLVMBuildUnreachable(builder);
    LLVMPositionBuilderAtEnd(builder, currentBlock);
    return outOfBoundsErrorBlock;
}

LLVMValueRef generateConstArray(LLVMTypeRef elementType, LLVMValueRef *values,
                                size_t len, struct ModuleData *module) {
    LLVMValueRef constArray = LLVMConstArray(elementType, values, len);
    LLVMValueRef globalArray =
        LLVMAddGlobal(module->module, LLVMArrayType(elementType, len), "");
    LLVMSetLinkage(globalArray, LLVMPrivateLinkage);
    LLVMSetGlobalConstant(globalArray, 1);
    LLVMSetInitializer(globalArray, constArray);
    return globalArray;
}

LLVMValueRef *generateVector(LLVMTypeRef vectorElementType,
                             LLVMValueRef *values, size_t len,
                             struct ModuleData *module, char *name,
                             bool global) {
    LLVMValueRef *vectorAlloc = malloc(sizeof(LLVMValueRef));
    *vectorAlloc =
        LLVMBuildArrayMalloc(module->builder, vectorElementType,
                             LLVMConstInt(LLVMInt64Type(), len + 1, 0), "");
    stbds_arrpush(module->contexts[module->numContexts - 1]->mallocedVarsToFree,
                  vectorAlloc);
    // LLVMValueRef constArray =
    //     generateConstArray(vectorElementType, values, len, module);
    // LLVMBuildMemCpy(module->builder, vectorAlloc,
    // LLVMGetAlignment(vectorAlloc),
    //                 constArray, LLVMGetAlignment(constArray),
    //                 LLVMSizeOf(LLVMArrayType2(vectorElementType, len)));
    for (size_t i = 0; i < len; i++) {
        LLVMValueRef index =
            LLVMConstInt(LLVMInt32TypeInContext(module->context), i, 0);
        LLVMValueRef ep = LLVMBuildGEP2(module->builder, vectorElementType,
                                        *vectorAlloc, &index, 1, "");
        LLVMBuildStore(module->builder, values[i], ep);
    }

    LLVMTypeRef structType = LLVMStructTypeInContext(
        module->context,
        (LLVMTypeRef[]){
            LLVMPointerType(LLVMInt8TypeInContext(module->context), 0),
            LLVMInt32TypeInContext(module->context),
            LLVMInt32TypeInContext(module->context)},
        3, 0);
    LLVMValueRef *structPtr = malloc(sizeof(LLVMValueRef));
    LLVMPositionBuilderAtEnd(
        module->builder,
        module->contexts[module->numContexts - 1]->allocaBlock);
    if (global) {
        *structPtr = LLVMAddGlobal(module->module, structType, name);
        LLVMSetLinkage(*structPtr, LLVMExternalLinkage);
        LLVMSetInitializer(*structPtr, LLVMGetUndef(structType));
    } else {
        *structPtr = LLVMBuildAlloca(module->builder, structType, name);
    }
    LLVMPositionBuilderAtEnd(
        module->builder,
        module->contexts[module->numContexts - 1]->currentBlock);

    LLVMValueRef dataPtrIndices[] = {LLVMConstInt(LLVMInt32Type(), 0, 0),
                                     LLVMConstInt(LLVMInt32Type(), 0, 0)};
    LLVMValueRef dataPtrField = LLVMBuildGEP2(
        module->builder, structType, *structPtr, dataPtrIndices, 2, "");
    LLVMBuildStore(module->builder, *vectorAlloc, dataPtrField);

    LLVMValueRef sizeIndices[] = {LLVMConstInt(LLVMInt32Type(), 0, 0),
                                  LLVMConstInt(LLVMInt32Type(), 1, 0)};
    LLVMValueRef sizeField = LLVMBuildGEP2(module->builder, structType,
                                           *structPtr, sizeIndices, 2, "");
    LLVMBuildStore(module->builder, LLVMConstInt(LLVMInt32Type(), len, 0),
                   sizeField);

    LLVMValueRef capacityIndices[] = {LLVMConstInt(LLVMInt32Type(), 0, 0),
                                      LLVMConstInt(LLVMInt32Type(), 2, 0)};
    LLVMValueRef capacityField = LLVMBuildGEP2(
        module->builder, structType, *structPtr, capacityIndices, 2, "");
    LLVMBuildStore(module->builder, LLVMConstInt(LLVMInt32Type(), len, 0),
                   capacityField);
    return structPtr;
}

struct ValueData *generateVectorFromToken(struct Token *token,
                                          struct ModuleData *module, char *name,
                                          bool global) {
    size_t len = stbds_arrlen((struct Token **)token->data);
    LLVMValueRef *values = malloc(sizeof(LLVMValueRef) * len);
    struct TypeData *elementType;
    bool isStatic = true;
    for (size_t i = 0; i < len; i++) {
        struct Token *token2 = ((struct Token **)token->data)[i];
        struct ValueData *val =
            generateToken(token2, module, false, true, false, false);
        if (i == 0) {
            elementType = val->type;
        } else if (!cmptype(val->type, elementType, token2->lineNum,
                            token2->colNum, false)) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Got mismatching types\n",
                    token2->lineNum, token2->colNum);
            return NULL;
        }
        isStatic &= val->isStatic;
        values[i] = *(val->value);
        free(val);
    }
    LLVMTypeRef *llvmElementType = generateType(elementType, module);
    if (llvmElementType == NULL) {
        return NULL;
    }

    LLVMValueRef *vectorValue =
        generateVector(*llvmElementType, values, len, module, name, global);
    if (vectorValue == NULL) {
        return NULL;
    }
    free(values);
    free(llvmElementType);
    struct ValueData *ret = malloc(sizeof(struct ValueData));
    ret->value = vectorValue;
    ret->type = malloc(sizeof(struct TypeData));
    ret->type->type = VECTOR;
    ret->type->length = -1;
    ret->type->otherType = elementType;
    ret->isStatic = isStatic;
    return ret;
}

struct ValueData *generateArray(struct Token *token, struct ModuleData *module,
                                char *name, bool global) {
    size_t len = stbds_arrlen((struct Token **)token->data);
    LLVMValueRef *values = malloc(sizeof(LLVMValueRef) * len);
    struct TypeData *elementType;
    bool isStatic = true;
    for (size_t i = 0; i < len; i++) {
        struct Token *token2 = ((struct Token **)token->data)[i];
        struct ValueData *val =
            generateToken(token2, module, false, true, false, false);
        if (val == NULL) {
            return NULL;
        }
        if (i == 0) {
            elementType = val->type;
        } else if (!cmptype(val->type, elementType, token2->lineNum,
                            token2->colNum, false)) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Got mismatching types\n",
                    token2->lineNum, token2->colNum);
            return NULL;
        }
        isStatic &= val->isStatic;
        values[i] = *(val->value);
        free(val);
    }
    LLVMTypeRef *llvmElementType = generateType(elementType, module);
    if (llvmElementType == NULL) {
        return NULL;
    }

    // LLVMValueRef constArray =
    //     generateConstArray(*llvmElementType, values, len, module);
    LLVMTypeRef llvmArrayType = LLVMArrayType(*llvmElementType, len);
    LLVMPositionBuilderAtEnd(
        module->builder,
        module->contexts[module->numContexts - 1]->allocaBlock);
    LLVMValueRef arrayAlloca;
    if (global) {
        arrayAlloca = LLVMAddGlobal(module->module, llvmArrayType, name);
        LLVMSetInitializer(arrayAlloca, LLVMGetUndef(llvmArrayType));
        LLVMSetLinkage(arrayAlloca, LLVMExternalLinkage);
    } else {
        arrayAlloca = LLVMBuildAlloca(module->builder, llvmArrayType, name);
    }
    LLVMPositionBuilderAtEnd(
        module->builder,
        module->contexts[module->numContexts - 1]->currentBlock);
    for (size_t i = 0; i < len; i++) {
        LLVMValueRef indices[2] = {
            LLVMConstInt(LLVMInt32TypeInContext(module->context), 0, 0),
            LLVMConstInt(LLVMInt32TypeInContext(module->context), i, 0)};
        LLVMValueRef ep = LLVMBuildInBoundsGEP2(module->builder, llvmArrayType,
                                                arrayAlloca, indices, 2, "");
        LLVMBuildStore(module->builder, values[i], ep);
    }

    free(values);
    free(llvmElementType);
    struct ValueData *ret = malloc(sizeof(struct ValueData));
    ret->value = malloc(sizeof(LLVMValueRef));
    *(ret->value) = arrayAlloca;
    ret->type = malloc(sizeof(struct TypeData));
    ret->type->type = ARRAY;
    ret->type->otherType = elementType;
    ret->type->length = len;
    ret->isStatic = isStatic;
    return ret;
}

LLVMValueRef *generateString(struct Token *token, struct ModuleData *module,
                             char *name, bool global) {
    size_t len = strlen((char *)token->data);
    LLVMValueRef size = LLVMConstInt(LLVMInt64Type(), len + 1, 0);
    LLVMValueRef *stringAlloc = malloc(sizeof(LLVMValueRef));
    *stringAlloc =
        LLVMBuildCall2(module->builder, mallocType, mallocFunc, &size, 1, "");
    stbds_arrpush(module->contexts[module->numContexts - 1]->mallocedVarsToFree,
                  stringAlloc);
    LLVMPositionBuilderAtEnd(
        module->builder,
        module->contexts[module->numContexts - 1]->allocaBlock);
    LLVMValueRef stringPtr = LLVMBuildAlloca(
        module->builder,
        LLVMPointerType(LLVMInt8TypeInContext(module->context), 0), "");
    LLVMPositionBuilderAtEnd(
        module->builder,
        module->contexts[module->numContexts - 1]->currentBlock);
    LLVMBuildStore(module->builder,
                   LLVMConstString((char *)token->data, len, 0), stringPtr);
    LLVMBuildMemCpy(module->builder, *stringAlloc, 1, stringPtr, 1, size);

    LLVMTypeRef structType = LLVMStructTypeInContext(
        module->context,
        (LLVMTypeRef[]){
            LLVMPointerType(LLVMInt8TypeInContext(module->context), 0),
            LLVMInt32TypeInContext(module->context),
            LLVMInt32TypeInContext(module->context)},
        3, 0);
    LLVMValueRef *structPtr = malloc(sizeof(LLVMValueRef));
    LLVMPositionBuilderAtEnd(
        module->builder,
        module->contexts[module->numContexts - 1]->allocaBlock);
    if (global) {
        *structPtr = LLVMAddGlobal(module->module, structType, name);
        LLVMSetInitializer(*structPtr, LLVMGetUndef(structType));
        LLVMSetLinkage(*structPtr, LLVMExternalLinkage);
    } else {
        *structPtr = LLVMBuildAlloca(module->builder, structType, name);
    }
    LLVMPositionBuilderAtEnd(
        module->builder,
        module->contexts[module->numContexts - 1]->currentBlock);

    LLVMValueRef dataPtrIndices[] = {LLVMConstInt(LLVMInt32Type(), 0, 0),
                                     LLVMConstInt(LLVMInt32Type(), 0, 0)};
    LLVMValueRef dataPtrField = LLVMBuildGEP2(
        module->builder, structType, *structPtr, dataPtrIndices, 2, "");
    LLVMBuildStore(module->builder, *stringAlloc, dataPtrField);

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

struct ValueData *generateMacro(struct Token *token, struct ModuleData *module,
                                int exprLen) {
    struct MacroData *macroData = stbds_shget(
        module->macros, (char *)((struct Token **)token->data)[0]->data);
    struct MacroArg *args = NULL;
    struct Token **restArgs = NULL;
    size_t numRestArgs = 0;
    if (exprLen < macroData->numArgs) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: "
                "Too few arguments for macro \"%s\"\n",
                token->lineNum, token->colNum,
                (char *)((struct Token **)token->data)[0]->data);
        return NULL;
    }
    if (exprLen - 1 > macroData->numArgs && macroData->restArg == NULL) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: "
                "Too many arguments for macro \"%s\"\n",
                token->lineNum, token->colNum,
                (char *)((struct Token **)token->data)[0]->data);
        return NULL;
    }
    for (size_t i = 1; i < exprLen; i++) {
        if (i >= macroData->numArgs && macroData->restArg != NULL) {
            numRestArgs++;
            stbds_arrpush(restArgs, ((struct Token **)token->data)[i]);
            continue;
        }
        stbds_shput(args, macroData->args[i - 1],
                    ((struct Token **)token->data)[i]);
    }
    module->contexts[module->numContexts - 1]->macroArgs = args;
    if (macroData->restArg != NULL) {
        module->contexts[module->numContexts - 1]->macroRestArg =
            malloc(sizeof(struct MacroRestArg));
        module->contexts[module->numContexts - 1]->macroRestArg->name =
            macroData->restArg;
        module->contexts[module->numContexts - 1]->macroRestArg->numValues =
            numRestArgs;
        module->contexts[module->numContexts - 1]->macroRestArg->values =
            restArgs;
    }
    for (size_t i = 3; i < macroData->bodyLen; i++) {
        struct ValueData *val = generateToken(macroData->body[i], module, false,
                                              true, false, false);
        if (val == NULL) {
            return NULL;
        }
        if (i == macroData->bodyLen - 1) {
            stbds_shfree(module->contexts[module->numContexts - 1]->macroArgs);
            if (macroData->restArg != NULL) {
                stbds_arrfree(module->contexts[module->numContexts - 1]
                                  ->macroRestArg->values);
                free(module->contexts[module->numContexts - 1]->macroRestArg);
            }
            module->contexts[module->numContexts - 1]->macroArgs = NULL;
            module->contexts[module->numContexts - 1]->macroRestArg = NULL;
            return val;
        }
        free(val);
    }
    fprintf(stderr, "COMPILER ERROR on line %llu column %llu\n", token->lineNum,
            token->colNum);
    return NULL;
}

struct ValueData *generateFuncDataCall(struct FuncData *funcData,
                                       struct Token *token, size_t argsStartI,
                                       size_t exprLen,
                                       struct ModuleData *module,
                                       LLVMValueRef *extraFirstArg) {
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

    if (extraFirstArg != NULL) {
        // TODO: add type checking for extra first arg
        stbds_shput(args, funcData->args[0].name, extraFirstArg);
        numArgs++;
    }

    for (size_t i = argsStartI; i < exprLen; i++) {
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
            return NULL;
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
            if (funcData->args[argIdx].type->type == STRING ||
                funcData->args[argIdx].type->type == CLASS ||
                funcData->args[argIdx].type->type == VECTOR ||
                funcData->args[argIdx].type->type == MAP ||
                funcData->args[argIdx].type->type == NULLABLE) {
                LLVMPositionBuilderAtEnd(
                    module->builder,
                    module->contexts[module->numContexts - 1]->allocaBlock);
                LLVMValueRef alloca = LLVMBuildAlloca(
                    module->builder, *(funcData->args[argIdx].llvmType), "");
                LLVMPositionBuilderAtEnd(
                    module->builder,
                    module->contexts[module->numContexts - 1]->currentBlock);
                LLVMBuildStore(module->builder, *val, alloca);
                *val = alloca;
            }
            if (val == NULL) {
                return NULL;
            }
            stbds_shput(args, nextArgName, val);
            nextArgName = NULL;
            continue;
        }
        if (i > funcData->numArgs) {
            numExtraArgs++;
            if (funcData->isVarArg) {
                struct ValueData *val =
                    generateToken(token2, module, false, true, false, false);
                if (val == NULL) {
                    return NULL;
                }
                LLVMValueRef *llvmVal = val->value;
                if (val->type->type == STRING || val->type->type == CLASS ||
                    val->type->type == VECTOR || val->type->type == MAP ||
                    val->type->type == NULLABLE) {
                    LLVMPositionBuilderAtEnd(
                        module->builder,
                        module->contexts[module->numContexts - 1]->allocaBlock);
                    LLVMTypeRef *llvmType = generateType(val->type, module);
                    LLVMValueRef alloca =
                        LLVMBuildAlloca(module->builder, *llvmType, "");
                    LLVMPositionBuilderAtEnd(
                        module->builder,
                        module->contexts[module->numContexts - 1]
                            ->currentBlock);
                    LLVMBuildStore(module->builder, *llvmVal, alloca);
                    *llvmVal = alloca;
                }
                stbds_arrpush(llvmVA, *llvmVal);
                free(val);
                continue;
            }
            if (funcData->restArg != NULL) {
                struct ValueData *val = generateToken(
                    token2, module,
                    funcData->restArg->type->otherType->type != STRING,
                    funcData->restArg->type->otherType->type == BOOL,
                    funcData->restArg->type->otherType->type == VECTOR,
                    funcData->restArg->type->otherType->type == DOUBLE);
                if (val == NULL) {
                    return NULL;
                }
                if (!cmptype(val->type, funcData->restArg->type->otherType,
                             token2->lineNum, token2->colNum, false)) {
                    fprintf(stderr,
                            "ERROR on line %llu column %llu: Expected a "
                            "different type for rest argument\n",
                            token2->lineNum, token2->colNum);
                    return NULL;
                }
                LLVMValueRef *llvmVal = val->value;
                if (val->type->type == STRING || val->type->type == CLASS ||
                    val->type->type == VECTOR || val->type->type == MAP ||
                    val->type->type == NULLABLE) {
                    LLVMPositionBuilderAtEnd(
                        module->builder,
                        module->contexts[module->numContexts - 1]->allocaBlock);
                    LLVMTypeRef *llvmType = generateType(val->type, module);
                    LLVMValueRef alloca =
                        LLVMBuildAlloca(module->builder, *llvmType, "");
                    LLVMPositionBuilderAtEnd(
                        module->builder,
                        module->contexts[module->numContexts - 1]
                            ->currentBlock);
                    LLVMBuildStore(module->builder, *llvmVal, alloca);
                    *llvmVal = alloca;
                }
                stbds_arrpush(llvmRest, *llvmVal);
                free(val);
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
        if (funcData->args[i - 1].type->type == STRING ||
            funcData->args[i - 1].type->type == CLASS ||
            funcData->args[i - 1].type->type == ARRAY ||
            funcData->args[i - 1].type->type == VECTOR ||
            funcData->args[i - 1].type->type == MAP ||
            funcData->args[i - 1].type->type == NULLABLE) {
            LLVMPositionBuilderAtEnd(
                module->builder,
                module->contexts[module->numContexts - 1]->allocaBlock);
            LLVMValueRef alloca = LLVMBuildAlloca(
                module->builder, *(funcData->args[i - 1].llvmType), "");
            LLVMPositionBuilderAtEnd(
                module->builder,
                module->contexts[module->numContexts - 1]->currentBlock);
            LLVMBuildStore(module->builder, *val, alloca);
            *val = alloca;
        }
        numArgs++;
        stbds_shput(
            args, funcData->args[i - argsStartI + (extraFirstArg != NULL)].name,
            val);
    }

    for (size_t i = 0; i < funcData->numArgs; i++) {
        if (stbds_shgetp_null(args, funcData->args[i].name) == NULL) {
            if (!funcData->args[i].optional) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Required argument "
                        "\"%s\" not provided\n",
                        token->lineNum, token->colNum, funcData->args[i].name);
                return NULL;
            }
            LLVMTypeRef *type =
                generateType(funcData->args[i].type->otherType, module);
            LLVMTypeRef *type2 = generateType(funcData->args[i].type, module);
            if (type == NULL || type2 == NULL) {
                return NULL;
            }
            LLVMValueRef optionalValue = LLVMConstStructInContext(
                module->context,
                (LLVMValueRef[]){
                    LLVMConstNull(*type),
                    LLVMConstInt(LLVMInt1TypeInContext(module->context), 1, 0)},
                2, 0);
            LLVMValueRef *globalVal = malloc(sizeof(LLVMValueRef));
            *globalVal = LLVMAddGlobal(module->module, *type2, "");
            LLVMSetLinkage(*globalVal, LLVMPrivateLinkage);
            LLVMSetUnnamedAddress(*globalVal, LLVMGlobalUnnamedAddr);
            LLVMSetGlobalConstant(*globalVal, 1);
            LLVMSetInitializer(*globalVal, optionalValue);
            stbds_shput(args, funcData->args[i].name, globalVal);
            numArgs++;
            free(type);
            free(type2);
        }
        LLVMValueRef arg = *stbds_shget(args, funcData->args[i].name);
        stbds_arrpush(llvmArgs, arg);
    }
    if (llvmRest != NULL) {
        LLVMTypeRef *restArgType =
            generateType(funcData->restArg->type->otherType, module);
        if (restArgType == NULL) {
            return NULL;
        }
        LLVMValueRef *restVar = generateVector(*restArgType, llvmRest,
                                               numExtraArgs, module, "", false);
        if (restVar == NULL) {
            return NULL;
        }
        stbds_arrpush(llvmArgs, *restVar);
        numArgs++;
    } else if (funcData->restArg != NULL) {
        stbds_arrpush(llvmArgs,
                      *generateNil(funcData->restArg->llvmType, module));
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
    ret->isStatic = false;
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
            *type = (struct TypeData){FLOAT, NULL, NULL, -1, NULL};
            return type;
        }
        if (strcmp((char *)token->data, "double") == 0) {
            struct TypeData *type = malloc(sizeof(struct TypeData));
            *type = (struct TypeData){DOUBLE, NULL, NULL, -1, NULL};
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
    if (type->type == FLOAT) {
        *llvmType = LLVMFloatTypeInContext(module->context);
        return llvmType;
    }
    if (type->type == DOUBLE) {
        *llvmType = LLVMDoubleTypeInContext(module->context);
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
        return stbds_shget(module->classes, type->name)->structType;
    }
    if (type->type == POINTER) {
        LLVMTypeRef *otherType = generateType(type->otherType, module);
        if (otherType == NULL) {
            return NULL;
        }
        *llvmType = LLVMPointerType(*otherType, 0);
        free(otherType);
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
        free(otherType);
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
        free(otherType);
        return llvmType;
    }
    if (type->type == ARRAY) {
        LLVMTypeRef *otherType = generateType(type->otherType, module);
        if (otherType == NULL) {
            return NULL;
        }
        *llvmType = LLVMArrayType(*otherType, type->length);
        free(otherType);
        return llvmType;
    }
    // TODO: add MAP
    fprintf(stderr, "COMPILER ERROR during type generation");
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

    LLVMPositionBuilderAtEnd(
        module->builder,
        module->contexts[module->numContexts - 1]->allocaBlock);
    *val = LLVMBuildAlloca(module->builder, valType, "");
    LLVMPositionBuilderAtEnd(
        module->builder,
        module->contexts[module->numContexts - 1]->currentBlock);
    LLVMBuildInsertValue(module->builder, LLVMGetUndef(valType),
                         LLVMConstNull(*valueType), 0, "");
    LLVMBuildInsertValue(
        module->builder, LLVMGetUndef(valType),
        LLVMConstInt(LLVMInt1TypeInContext(module->context), 1, 0), 1, "");
    return val;
}

struct ValueData *generateClassInit(struct Token *token,
                                    struct ModuleData *module, int exprLen,
                                    char *className, char *name, bool global) {
    struct Token **tokens = (struct Token **)token->data;
    struct ClassData *classData = stbds_shget(module->classes, className);
    if ((exprLen - 1) < classData->numVars) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too few arguments to "
                "initialize an instance of class \"%s\"\n",
                token->lineNum, token->colNum, className);
        return NULL;
    }
    if ((exprLen - 1) > classData->numVars) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too many arguments to "
                "initialize an instance of class \"%s\"\n",
                token->lineNum, token->colNum, className);
        return NULL;
    }
    LLVMValueRef var = LLVMGetUndef(*(classData->structType));
    for (size_t i = 1; i < exprLen; i++) {
        LLVMValueRef *val = generateTokenOfType(
            tokens[i], *(classData->variables[i - 1].value.type), module);
        if (val == NULL) {
            return NULL;
        }
        var = LLVMBuildInsertValue(module->builder, var, *val, i - 1, "");
        free(val);
    }
    LLVMPositionBuilderAtEnd(
        module->builder,
        module->contexts[module->numContexts - 1]->allocaBlock);
    LLVMValueRef alloca;
    if (global) {
        alloca = LLVMAddGlobal(module->module, *(classData->structType), name);
        LLVMSetInitializer(alloca, LLVMGetUndef(*(classData->structType)));
        LLVMSetLinkage(alloca, LLVMExternalLinkage);
    } else {
        alloca =
            LLVMBuildAlloca(module->builder, *(classData->structType), name);
    }
    LLVMPositionBuilderAtEnd(
        module->builder,
        module->contexts[module->numContexts - 1]->currentBlock);
    LLVMBuildStore(module->builder, var, alloca);
    struct ValueData *ret = malloc(sizeof(struct ValueData));
    ret->value = malloc(sizeof(LLVMValueRef));
    *(ret->value) = alloca;
    ret->type = malloc(sizeof(struct TypeData));
    ret->type->type = CLASS;
    ret->type->length = -1;
    ret->type->name = className;
    ret->isStatic = false;
    return ret;
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

    if (stbds_shgetp_null(*(module->variables), name) != NULL ||
        stbds_shgetp_null(
            module->contexts[module->numContexts - 1]->localVariables, name)) {
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
    if (type->type == STRING && initValue->type == STRING_TOKEN) {
        llvmVar =
            generateString(initValue, module, name, module->numContexts == 1);
        if (llvmVar == NULL) {
            return NULL;
        }
    } else if (type->type == ARRAY && initValue->type == LIST_TOKEN) {
        struct ValueData *val =
            generateArray(initValue, module, name, module->numContexts == 1);
        if (val == NULL) {
            return NULL;
        }
        if (!cmptype(val->type, type, initValue->lineNum, initValue->colNum,
                     true)) {
            return NULL;
        }
        llvmVar = val->value;
        free(val->type);
        free(val);
    } else if (type->type == VECTOR && initValue->type == LIST_TOKEN) {
        struct ValueData *val = generateVectorFromToken(
            initValue, module, name, module->numContexts == 1);
        if (val == NULL) {
            return NULL;
        }
        if (!cmptype(val->type, type, initValue->lineNum, initValue->colNum,
                     true)) {
            return NULL;
        }
        llvmVar = val->value;
        free(val->type);
        free(val);
    } else if (type->type == CLASS && initValue->type == EXPR_TOKEN &&
               ((struct Token **)initValue->data)[0]->type == IDENT_TOKEN &&
               strcmp((char *)((struct Token **)initValue->data)[0]->data,
                      type->name) == 0) {
        struct ValueData *val = generateClassInit(
            initValue, module, stbds_arrlen((struct Token **)initValue->data),
            type->name, name, true);
        if (val == NULL) {
            return NULL;
        }
        llvmVar = val->value;
        free(val->type);
        free(val);
    } else {
        LLVMValueRef *llvmInitValue;
        if (type->type == NULLABLE) {
            struct ValueData *val = generateToken(
                initValue, module, type->otherType->type != STRING,
                type->otherType->type == BOOL, type->otherType->type == VECTOR,
                type->otherType->type == DOUBLE);
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
            if (llvmInitValue == NULL) {
                return NULL;
            }
        }

        llvmVar = malloc(sizeof(LLVMValueRef));
        LLVMPositionBuilderAtEnd(
            module->builder,
            module->contexts[module->numContexts - 1]->allocaBlock);
        if (module->numContexts == 1) {
            *llvmVar = LLVMAddGlobal(module->module, *llvmType, name);
            LLVMSetInitializer(*llvmVar, LLVMGetUndef(*llvmType));
            LLVMSetLinkage(*llvmVar, LLVMExternalLinkage);
        } else {
            *llvmVar = LLVMBuildAlloca(module->builder, *llvmType, name);
        }
        LLVMPositionBuilderAtEnd(
            module->builder,
            module->contexts[module->numContexts - 1]->currentBlock);
        if (type->type == ARRAY) {
            LLVMBuildMemCpy(module->builder, *llvmVar,
                            LLVMGetAlignment(*llvmVar), *llvmInitValue,
                            LLVMGetAlignment(*llvmInitValue),
                            LLVMSizeOf(*llvmType));
        } else {
            LLVMBuildStore(module->builder, *llvmInitValue, *llvmVar);
        }

        free(llvmInitValue);
    }

    struct VariableData variableData =
        (struct VariableData){type, llvmType, llvmVar};
    stbds_shput(module->contexts[module->numContexts - 1]->localVariables, name,
                variableData);

    return llvmVar;
}

struct FuncData *generateFuncType(struct Token *token,
                                  struct ModuleData *module, bool needsReturn,
                                  struct FunctionArgType *extraArg) {
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

    if (extraArg != NULL) {
        stbds_arrpush(args, *extraArg);
        stbds_arrpush(llvmArgs, *(extraArg->llvmType));
        numArgs++;
    }

    if (listLen == 1 &&
        ((struct Token **)token->data)[0]->type == IDENT_TOKEN &&
        strcmp((char *)((struct Token **)token->data)[0]->data, "nil") == 0) {
        struct TypeData *type = malloc(sizeof(struct TypeData));
        type->type = NIL;
        type->length = -1;
        LLVMTypeRef *llvmType = malloc(sizeof(LLVMTypeRef));
        *llvmType = LLVMVoidTypeInContext(module->context);
        struct FunctionArgType arg = {type, llvmType, ""};
        stbds_arrpush(args, arg);
        stbds_arrpush(llvmArgs, *llvmType);
        numArgs++;
    } else {
        for (size_t i = 0; i < listLen; i++) {
            struct Token *token2 = ((struct Token **)token->data)[i];
            if (i == listLen - 1 && needsReturn) {
                if (token2->type == IDENT_TOKEN &&
                    strcmp((char *)token2->data, "nil") == 0) {
                    struct TypeData *type = malloc(sizeof(struct TypeData));
                    type->type = NIL;
                    type->length = -1;
                    LLVMTypeRef *llvmType = malloc(sizeof(LLVMTypeRef));
                    *llvmType = LLVMVoidType();
                    struct FunctionArgType arg = {type, llvmType, ""};
                    stbds_arrpush(args, arg);
                    stbds_arrpush(llvmArgs, *llvmType);
                    numArgs++;
                    continue;
                }
                struct TypeData *type = getType(token2, module);
                if (type == NULL) {
                    return NULL;
                }
                LLVMTypeRef *llvmType = NULL;
                if (type->type == STRING || type->type == CLASS ||
                    type->type == VECTOR || type->type == MAP ||
                    type->type == NULLABLE || type->type == ARRAY) {
                    struct TypeData ptrType =
                        (struct TypeData){POINTER, type, NULL, -1, NULL};
                    llvmType = generateType(&ptrType, module);
                } else {
                    llvmType = generateType(type, module);
                }
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
                    fprintf(
                        stderr,
                        "ERROR on line %llu column %llu: Expected a function "
                        "argument type - '(<arg_name> <arg_type>)\n",
                        token2->lineNum, token2->colNum);
                    return NULL;
                }
                if (((struct Token **)(struct Token *)token2->data)[0]->type !=
                    IDENT_TOKEN) {
                    fprintf(stderr,
                            "ERROR on line %llu column %llu: Expected an "
                            "identifier\n",
                            ((struct Token **)(struct Token *)token2->data)[0]
                                ->lineNum,
                            ((struct Token **)(struct Token *)token2->data)[0]
                                ->colNum);
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
                    *newType = (struct TypeData){VECTOR, type, NULL, -1, NULL};
                    type = newType;
                }
                LLVMTypeRef *llvmType;
                if (type->type == STRING || type->type == CLASS ||
                    type->type == VECTOR || type->type == MAP ||
                    type->type == NULLABLE) {
                    struct TypeData ptrType =
                        (struct TypeData){POINTER, type, NULL, -1, NULL};
                    llvmType = generateType(&ptrType, module);
                } else {
                    llvmType = generateType(type, module);
                }
                if (llvmType == NULL) {
                    return NULL;
                }
                struct FunctionArgType arg = {
                    type, llvmType,
                    (char *)(((struct Token **)(struct Token *)token2->data)[0]
                                 ->data),
                    type->type == NULLABLE};
                if (rest) {
                    arg.type = type;
                    arg.llvmType = llvmType;
                    if (arg.llvmType == NULL) {
                        return NULL;
                    }
                    restArg = (struct FunctionArgType *)malloc(
                        sizeof(struct FunctionArgType));
                    *restArg = arg;
                    numRest++;
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
                // if (strcmp((char *)token2->data, ":optional") == 0) {
                //     if (rest) {
                //         fprintf(stderr,
                //                 "ERROR on line %llu column %llu: Can't define
                //                 an " "optional argument after the rest
                //                 argument\n", token2->lineNum,
                //                 token2->colNum);
                //         return NULL;
                //     }
                //     if (optional) {
                //         fprintf(stderr,
                //                 "ERROR on line %llu column %llu: Already
                //                 defining " "optional arguments\n",
                //                 token2->lineNum, token2->colNum);
                //         return NULL;
                //     }
                //     optional = true;
                //     continue;
                // }
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
    }

    if (numArgs == 0) {
        fprintf(
            stderr,
            "ERROR on line %llu column %llu: Expected at least the function "
            "return type or nil if function doesn't return anything\n",
            token->lineNum, token->colNum);
        return NULL;
    }

    struct TypeData *ret = NULL;
    LLVMTypeRef *llvmRet = NULL;
    if (needsReturn) {
        ret = args[numArgs - 1].type;
        llvmRet = args[numArgs - 1].llvmType;
        stbds_arrpop(args);
        stbds_arrpop(llvmArgs);
        numArgs--;
    } else {
        llvmRet = malloc(sizeof(LLVMTypeRef));
        *llvmRet = LLVMVoidTypeInContext(module->context);
        ret = malloc(sizeof(struct TypeData));
        ret->type = NIL;
        ret->length = -1;
    }

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
    if (rest) {
        numArgs--;
    }

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
    if (stbds_shgetp_null(module->functions, name) != NULL) {
        fprintf(
            stderr,
            "ERROR on line %llu column %llu: Function \"%s\" already defined\n",
            token->lineNum, token->colNum, name);
        return NULL;
    }
    struct FuncData *funcData =
        generateFuncType(((struct Token **)token->data)[2], module, true, NULL);

    if (funcData == NULL) {
        return NULL;
    }

    LLVMValueRef *func = malloc(sizeof(LLVMValueRef));
    *func = LLVMAddFunction(module->module, name, *funcData->funcType);

    funcData->function = func;
    stbds_shput(module->functions, name, funcData);
    return func;
}

bool generateMacroDefine(struct Token *token, struct ModuleData *module,
                         int exprLen) {
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

LLVMValueRef *generateClassFunc(struct Token *token, struct ModuleData *module,
                                int exprLen) {
    if (exprLen < 5) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too few arguments for class "
                "function definition, expected (classfun <class_name> "
                "<function_name> <function_arguments> <body1> ...)",
                token->lineNum, token->colNum);
        return NULL;
    }
    if (exprLen > 5) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too many arguments for class "
                "function definition, expected (classfun <class_name> "
                "<function_name> <function_arguments> <body1> ...)",
                token->lineNum, token->colNum);
        return NULL;
    }
    struct Token **tokens = (struct Token **)token->data;
    if (tokens[1]->type != IDENT_TOKEN) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected an identifier to "
                "specify the class this class function belongs to\n",
                tokens[1]->lineNum, tokens[1]->colNum);
        return NULL;
    }
    if (tokens[2]->type != IDENT_TOKEN) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected an identifier to "
                "specify the class function name\n",
                tokens[2]->lineNum, tokens[2]->colNum);
        return NULL;
    }
    if (tokens[3]->type != LIST_TOKEN) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected a list with the "
                "class arguments\n",
                tokens[3]->lineNum, tokens[3]->colNum);
        return NULL;
    }

    char *funcName = (char *)tokens[2]->data;

    if (stbds_shgetp_null(module->classes, (char *)tokens[1]->data) == NULL) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Invalid class \"%s\"\n",
                tokens[1]->lineNum, tokens[1]->colNum, (char *)tokens[1]->data);
        return NULL;
    }
    struct ClassData *classData =
        stbds_shget(module->classes, (char *)tokens[1]->data);

    if (stbds_shgetp_null(classData->functions, funcName) != NULL) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Class function \"%s\" for "
                "class \"%s\"\n",
                token->lineNum, token->colNum, funcName,
                (char *)tokens[1]->data);
        return NULL;
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
        return NULL;
    }

    LLVMValueRef func =
        LLVMAddFunction(module->module, funcName, *funcData->funcType);
    funcData->function = malloc(sizeof(LLVMValueRef));
    *(funcData->function) = func;
    stbds_shput(classData->functions, funcName, funcData);

    LLVMBasicBlockRef allocaBlock = LLVMAppendBasicBlock(func, "alloca");
    LLVMBasicBlockRef entryBlock = LLVMAppendBasicBlock(func, "entry");
    LLVMBasicBlockRef outOfBoundsErrorBlock =
        generateOutOfBoundsErrorBlock(func, entryBlock, module->builder);

    struct ContextData *context = malloc(sizeof(struct ContextData));
    context->func = func;
    context->unreachable = false;
    context->allocaBlock = allocaBlock;
    context->currentBlock = entryBlock;
    context->outOfBoundsErrorBlock = outOfBoundsErrorBlock;
    context->localVariables = NULL;
    context->macroArgs = NULL;
    context->macroRestArg = NULL;
    context->continueBlock = NULL;
    context->breakBlock = NULL;
    context->loopIndexVar = NULL;
    context->args = NULL;
    context->isVarArg = funcData->isVarArg;
    context->returnType = funcData->retType;
    context->mallocedVarsToFree = NULL;

    for (size_t i = 0; i < funcData->numArgs; i++) {
        LLVMValueRef *val = malloc(sizeof(LLVMValueRef));
        *val = LLVMGetParam(func, i);
        stbds_shput(context->args, funcData->args[i].name,
                    ((struct VariableData){funcData->args[i].type,
                                           funcData->args[i].llvmType, val}));
    }
    if (funcData->restArg != NULL) {
        LLVMValueRef *val = malloc(sizeof(LLVMValueRef));
        *val = LLVMGetParam(func, funcData->numArgs);
        stbds_shput(context->args, funcData->restArg->name,
                    ((struct VariableData){funcData->restArg->type,
                                           funcData->restArg->llvmType, val}));
    }

    stbds_arrpush(module->contexts, context);
    module->numContexts++;

    LLVMPositionBuilderAtEnd(module->builder, entryBlock);
    bool returned = false;
    struct ValueData *block =
        generateBlock(token, module, exprLen, 4, &returned);
    if (block == NULL) {
        return NULL;
    }
    if (funcData->retType->type != NIL && !returned) {
        if (!cmptype(block->type, funcData->retType, token->lineNum,
                     token->colNum, false)) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Expected a correct type "
                    "of last expression inside the function or an explicit "
                    "return\n",
                    ((struct Token **)(token->data))[exprLen - 1]->lineNum,
                    ((struct Token **)(token->data))[exprLen - 1]->colNum);
            return NULL;
        }
        generateFreeMallocedVars(module);
        LLVMBuildRet(module->builder, *(block->value));
    } else if (!returned) {
        generateFreeMallocedVars(module);
        LLVMBuildRetVoid(module->builder);
    }
    LLVMPositionBuilderAtEnd(module->builder, allocaBlock);
    LLVMBuildBr(module->builder, entryBlock);
    stbds_arrpop(module->contexts);
    module->numContexts--;
    LLVMPositionBuilderAtEnd(
        module->builder,
        module->contexts[module->numContexts - 1]->currentBlock);
    return funcData->function;
}

struct ValueData *generateClassDefine(struct Token *token,
                                      struct ModuleData *module, int exprLen) {
    if (exprLen < 5) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too few arguments for class "
                "definition, expected (defclass <class_name> "
                "'(<inherited_class1> ...) :variables '('(<var_name1> "
                "<var_type1>) ...))\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    if (exprLen > 5) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too many arguments for class "
                "definition, expected (defclass <class_name> "
                "'(<inherited_class1> ...) :variables '('(<var_name1> "
                "<var_type1>) ...))\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    struct Token **tokens = (struct Token **)token->data;
    if (tokens[1]->type != IDENT_TOKEN) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected an identifier as the "
                "class name\n",
                tokens[1]->lineNum, tokens[1]->colNum);
        return NULL;
    }
    if (tokens[2]->type != LIST_TOKEN) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected a list with the "
                "inherited classes, if this class doesn't inherit any classes, "
                "put an empty list there\n",
                tokens[2]->lineNum, tokens[2]->colNum);
        return NULL;
    }
    if (tokens[3]->type != IDENT_TOKEN) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected \":variables\"\n",
                tokens[3]->lineNum, tokens[3]->colNum);
        return NULL;
    }
    if (tokens[4]->type != LIST_TOKEN) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected a list of variable "
                "names and their types\n",
                tokens[4]->lineNum, tokens[4]->colNum);
        return NULL;
    }
    if (strcmp((char *)tokens[3]->data, ":variables") != 0) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected \":variables\"\n",
                tokens[3]->lineNum, tokens[3]->colNum);
        return NULL;
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
            return NULL;
        }
        if (stbds_shgetp_null(module->classes, (char *)tokens2[i]->data) ==
            NULL) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Invalid class %s\n",
                    tokens2[i]->lineNum, tokens2[i]->colNum,
                    (char *)tokens2[i]->data);
            return NULL;
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
    *data =
        (struct ClassData){classType, llvmStructType, variables, NULL, numVars};
    stbds_shput(module->classes, className, data);

    struct ValueData *ret = malloc(sizeof(struct ValueData));
    ret->value = NULL;
    ret->type = malloc(sizeof(struct TypeData));
    ret->type->type = NIL;
    ret->type->length = -1;
    ret->isStatic = false;
    return ret;
}

bool generateImport(struct Token *token, struct ModuleData *module,
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

    char *relFilePath = (char *)((struct Token **)token->data)[1]->data;
    size_t pathLen = strlen(relFilePath);
    relFilePath = realloc(relFilePath, (pathLen + 4) * sizeof(char));
    strcat(relFilePath, ".sao");
    char *filePath = calloc(FILENAME_MAX, sizeof(char));
    if (toAbsolutePath(relFilePath, filePath, FILENAME_MAX) == NULL) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Error resolving path %s\n",
                token->lineNum, token->colNum, relFilePath);
        return false;
    };

    size_t numOutlinedFiles = stbds_arrlen(module->outlinedFiles);
    for (size_t i = 0; i < numOutlinedFiles; i++) {
        if (strcmp(module->outlinedFiles[i], filePath) == 0) {
            return true;
        }
    }
    stbds_arrpush(module->outlinedFiles, filePath);

    FILE *file = fopen(filePath, "r");
    if (file == NULL) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Error opening file %s\n",
                token->lineNum, token->colNum, filePath);
        return false;
    }

    struct Token *body = malloc(sizeof(struct Token));
    *body = (struct Token){NULL_TOKEN, NULL, 0, 0};
    if (tokenize(body, file)) {
        return false;
    }

    char *mainName = NULL;
    if (stbds_shgetp_null(mainNames, filePath) != NULL) {
        mainName = stbds_shget(mainNames, filePath);
    } else {
        mainName = malloc(32 * sizeof(char));
        sprintf_s(mainName, 32, "main%d", numImports);
        stbds_arrpush(toGenerate,
                      ((struct ToGenerateData){body, filePath, numImports++}));
    }
    LLVMTypeRef funcType =
        LLVMFunctionType(LLVMInt32TypeInContext(module->context), NULL, 0, 0);
    LLVMValueRef mainFunc = LLVMAddFunction(module->module, mainName, funcType);
    free(mainName);
    LLVMValueRef retVal =
        LLVMBuildCall2(module->builder, funcType, mainFunc, NULL, 0, "");
    // TODO: return the value if it is not 0 (on error)

    if (!outlineFile(body, module)) {
        return false;
    }

    while (stbds_arrlen(module->toOutline) > 0) {
        char *filePath = stbds_arrpop(module->toOutline);
        numOutlinedFiles = stbds_arrlen(module->outlinedFiles);
        for (size_t i = 0; i < numOutlinedFiles; i++) {
            if (strcmp(module->outlinedFiles[i], filePath) == 0) {
                continue;
            }
        }
        stbds_arrpush(module->outlinedFiles, filePath);

        FILE *file = fopen(filePath, "r");
        if (file == NULL) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Error opening file %s\n",
                    token->lineNum, token->colNum, filePath);
            return false;
        }

        struct Token *body = malloc(sizeof(struct Token));
        *body = (struct Token){NULL_TOKEN, NULL, 0, 0};
        if (tokenize(body, file)) {
            return false;
        }

        if (!outlineFile(body, module)) {
            return false;
        }
    }

    return true;
}

LLVMValueRef *generateFuncDefine(struct Token *token, struct ModuleData *module,
                                 int exprLen) {
    if (exprLen < 3) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too few arguments for a "
                "function definition - expected at least 2 arguments (defun "
                "<name> <arguments> <body1> ...)\n",
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
        generateFuncType(((struct Token **)token->data)[2], module, true, NULL);

    if (funcData == NULL) {
        return NULL;
    }

    LLVMValueRef func =
        LLVMAddFunction(module->module, name, *funcData->funcType);
    funcData->function = malloc(sizeof(LLVMValueRef));
    *(funcData->function) = func;
    stbds_shput(module->functions, name, funcData);

    LLVMBasicBlockRef allocaBlock = LLVMAppendBasicBlock(func, "alloca");
    LLVMBasicBlockRef entryBlock = LLVMAppendBasicBlock(func, "entry");
    LLVMBasicBlockRef outOfBoundsErrorBlock =
        generateOutOfBoundsErrorBlock(func, entryBlock, module->builder);

    struct ContextData *context = malloc(sizeof(struct ContextData));
    context->func = func;
    context->unreachable = false;
    context->allocaBlock = allocaBlock;
    context->currentBlock = entryBlock;
    context->outOfBoundsErrorBlock = outOfBoundsErrorBlock;
    context->localVariables = NULL;
    context->macroArgs = NULL;
    context->macroRestArg = NULL;
    context->continueBlock = NULL;
    context->breakBlock = NULL;
    context->loopIndexVar = NULL;
    context->args = NULL;
    context->isVarArg = funcData->isVarArg;
    context->returnType = funcData->retType;
    context->mallocedVarsToFree = NULL;

    for (size_t i = 0; i < funcData->numArgs; i++) {
        LLVMValueRef *val = malloc(sizeof(LLVMValueRef));
        *val = LLVMGetParam(func, i);
        stbds_shput(context->args, funcData->args[i].name,
                    ((struct VariableData){funcData->args[i].type,
                                           funcData->args[i].llvmType, val}));
    }
    if (funcData->restArg != NULL) {
        LLVMValueRef *val = malloc(sizeof(LLVMValueRef));
        *val = LLVMGetParam(func, funcData->numArgs);
        stbds_shput(context->args, funcData->restArg->name,
                    ((struct VariableData){funcData->restArg->type,
                                           funcData->restArg->llvmType, val}));
    }

    stbds_arrpush(module->contexts, context);
    module->numContexts++;

    LLVMPositionBuilderAtEnd(module->builder, entryBlock);
    bool returned = false;
    struct ValueData *block =
        generateBlock(token, module, exprLen, 3, &returned);
    if (block == NULL) {
        return NULL;
    }
    if (funcData->retType->type != NIL && !returned) {
        if (!cmptype(block->type, funcData->retType, token->lineNum,
                     token->colNum, false)) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Expected a correct type "
                    "of last expression inside the function or an explicit "
                    "return\n",
                    ((struct Token **)(token->data))[exprLen - 1]->lineNum,
                    ((struct Token **)(token->data))[exprLen - 1]->colNum);
            return NULL;
        }
        generateFreeMallocedVars(module);
        LLVMBuildRet(module->builder, *(block->value));
    } else if (!returned) {
        generateFreeMallocedVars(module);
        LLVMBuildRetVoid(module->builder);
    }
    LLVMPositionBuilderAtEnd(module->builder, allocaBlock);
    LLVMBuildBr(module->builder, entryBlock);
    stbds_arrpop(module->contexts);
    module->numContexts--;
    LLVMPositionBuilderAtEnd(
        module->builder,
        module->contexts[module->numContexts - 1]->currentBlock);
    return funcData->function;
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
    struct ValueData *left = generateToken(((struct Token **)token->data)[1],
                                           module, false, true, false, false);
    if (left == NULL) {
        return NULL;
    }
    struct ValueData *right =
        generateToken(((struct Token **)token->data)[2], module, false, true,
                      false, left->type->type == DOUBLE);
    if (right == NULL) {
        return NULL;
    }
    struct ValueData *val = malloc(sizeof(struct ValueData));
    val->value = malloc(sizeof(LLVMValueRef));
    val->type = malloc(sizeof(struct TypeData));
    val->isStatic = left->isStatic && right->isStatic;
    val->type->length = -1;
    if (left->type->type == DOUBLE || right->type->type == DOUBLE) {
        if (left->type->type != DOUBLE) {
            if (left->type->type == INT32) {
                *(left->value) = LLVMBuildSIToFP(
                    module->builder, *(left->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during addition, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == UNSIGNED32) {
                *(left->value) = LLVMBuildUIToFP(
                    module->builder, *(left->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during addition, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == FLOAT) {
                *(left->value) = LLVMBuildFPExt(
                    module->builder, *(left->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting a "
                       "double type to a float during addition, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Can only add types "
                        "double, float, int, uint\n",
                        token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        if (right->type->type != DOUBLE) {
            if (right->type->type == INT32) {
                *(right->value) = LLVMBuildSIToFP(
                    module->builder, *(right->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during addition, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == UNSIGNED32) {
                *(right->value) = LLVMBuildUIToFP(
                    module->builder, *(right->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during addition, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == FLOAT) {
                *(right->value) = LLVMBuildFPExt(
                    module->builder, *(right->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting a "
                       "double type to a float during addition, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Can only add types "
                        "double, float, int, uint\n",
                        token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        *(val->value) =
            LLVMBuildFAdd(module->builder, *(left->value), *(right->value), "");
        val->type->type = DOUBLE;
    } else if (left->type->type == FLOAT || right->type->type == FLOAT) {
        if (left->type->type != FLOAT) {
            if (left->type->type == INT32) {
                *(left->value) = LLVMBuildSIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during addition, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == UNSIGNED32) {
                *(left->value) = LLVMBuildUIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during addition, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == DOUBLE) {
                *(left->value) = LLVMBuildFPTrunc(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting a "
                       "double type to a float during addition, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Can only add types "
                        "double, float, int, uint\n",
                        token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        if (right->type->type != FLOAT) {
            if (right->type->type == INT32) {
                *(right->value) = LLVMBuildSIToFP(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during addition, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == UNSIGNED32) {
                *(right->value) = LLVMBuildUIToFP(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during addition, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == DOUBLE) {
                *(right->value) = LLVMBuildFPTrunc(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting a "
                       "double type to a float during addition, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Can only add types "
                        "double, float, int, uint\n",
                        token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        *(val->value) =
            LLVMBuildFAdd(module->builder, *(left->value), *(right->value), "");
        val->type->type = FLOAT;
    } else {
        if ((left->type->type != INT32 && left->type->type != UNSIGNED32) ||
            (right->type->type != INT32 && right->type->type != UNSIGNED32)) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Can only add types "
                    "double, float, int, uint\n",
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
    struct ValueData *left = generateToken(((struct Token **)token->data)[1],
                                           module, false, true, false, false);
    if (left == NULL) {
        return NULL;
    }
    struct ValueData *right =
        generateToken(((struct Token **)token->data)[2], module, false, true,
                      false, left->type->type == DOUBLE);
    if (right == NULL) {
        return NULL;
    }
    struct ValueData *val = malloc(sizeof(struct ValueData));
    val->value = malloc(sizeof(LLVMValueRef));
    val->type = malloc(sizeof(struct TypeData));
    val->isStatic = left->isStatic && right->isStatic;
    val->type->length = -1;
    if (left->type->type == DOUBLE || right->type->type == DOUBLE) {
        if (left->type->type != DOUBLE) {
            if (left->type->type == INT32) {
                *(left->value) = LLVMBuildSIToFP(
                    module->builder, *(left->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during subtraction, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == UNSIGNED32) {
                *(left->value) = LLVMBuildUIToFP(
                    module->builder, *(left->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during subtraction, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == FLOAT) {
                *(left->value) = LLVMBuildFPExt(
                    module->builder, *(left->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting a "
                       "double type to a float during subtraction, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only subtract types "
                    "double, float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        if (right->type->type != DOUBLE) {
            if (right->type->type == INT32) {
                *(right->value) = LLVMBuildSIToFP(
                    module->builder, *(right->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during subtraction, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == UNSIGNED32) {
                *(right->value) = LLVMBuildUIToFP(
                    module->builder, *(right->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during subtraction, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == FLOAT) {
                *(right->value) = LLVMBuildFPExt(
                    module->builder, *(right->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting a "
                       "double type to a float during subtraction, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only subtract types "
                    "double, float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        *(val->value) =
            LLVMBuildFSub(module->builder, *(left->value), *(right->value), "");
        val->type->type = DOUBLE;
    } else if (left->type->type == FLOAT || right->type->type == FLOAT) {
        if (left->type->type != FLOAT) {
            if (left->type->type == INT32) {
                *(left->value) = LLVMBuildSIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during subtraction, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == UNSIGNED32) {
                *(left->value) = LLVMBuildUIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during subtraction, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == DOUBLE) {
                *(left->value) = LLVMBuildFPTrunc(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during subtraction, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only subtract types "
                    "double, float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        if (right->type->type != FLOAT) {
            if (right->type->type == INT32) {
                *(right->value) = LLVMBuildSIToFP(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during subtraction, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == UNSIGNED32) {
                *(right->value) = LLVMBuildUIToFP(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during subtraction, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == DOUBLE) {
                *(right->value) = LLVMBuildFPTrunc(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during subtraction, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only subtract types "
                    "double, float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        *(val->value) =
            LLVMBuildFSub(module->builder, *(left->value), *(right->value), "");
        val->type->type = FLOAT;
    } else {
        if ((left->type->type != INT32 && left->type->type != UNSIGNED32) ||
            (right->type->type != INT32 && right->type->type != UNSIGNED32)) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Can only subtract types "
                    "double, float, int, uint\n",
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
    struct ValueData *left = generateToken(((struct Token **)token->data)[1],
                                           module, false, true, false, false);
    if (left == NULL) {
        return NULL;
    }
    struct ValueData *right =
        generateToken(((struct Token **)token->data)[2], module, false, true,
                      false, left->type->type == DOUBLE);
    if (right == NULL) {
        return NULL;
    }
    struct ValueData *val = malloc(sizeof(struct ValueData));
    val->value = malloc(sizeof(LLVMValueRef));
    val->type = malloc(sizeof(struct TypeData));
    val->isStatic = left->isStatic && right->isStatic;
    val->type->length = -1;
    if (left->type->type == DOUBLE || right->type->type == DOUBLE) {
        if (left->type->type != DOUBLE) {
            if (left->type->type == INT32) {
                *(left->value) = LLVMBuildSIToFP(
                    module->builder, *(left->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf(
                    "WARNING on line %llu column %llu: Converting an "
                    "unsigned integer type to a float during multiplication, "
                    "possible loss of data\n",
                    token->lineNum, token->colNum);
            } else if (left->type->type == UNSIGNED32) {
                *(left->value) = LLVMBuildUIToFP(
                    module->builder, *(left->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf(
                    "WARNING on line %llu column %llu: Converting an "
                    "unsigned integer type to a float during multiplication, "
                    "possible loss of data\n",
                    token->lineNum, token->colNum);
            } else if (left->type->type == FLOAT) {
                *(left->value) = LLVMBuildFPExt(
                    module->builder, *(left->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting a "
                       "double type to a float during multiplication, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only subtract types "
                    "double, float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        if (right->type->type != DOUBLE) {
            if (right->type->type == INT32) {
                *(right->value) = LLVMBuildSIToFP(
                    module->builder, *(right->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during multiplication, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == UNSIGNED32) {
                *(right->value) = LLVMBuildUIToFP(
                    module->builder, *(right->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf(
                    "WARNING on line %llu column %llu: Converting an "
                    "unsigned integer type to a float during multiplication, "
                    "possible loss of data\n",
                    token->lineNum, token->colNum);
            } else if (right->type->type == FLOAT) {
                *(right->value) = LLVMBuildFPExt(
                    module->builder, *(right->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting a "
                       "double type to a float during multiplication, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only multiply types "
                    "double, float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        *(val->value) =
            LLVMBuildFMul(module->builder, *(left->value), *(right->value), "");
        val->type->type = DOUBLE;
    } else if (left->type->type == FLOAT || right->type->type == FLOAT) {
        if (left->type->type != FLOAT) {
            if (left->type->type == INT32) {
                *(left->value) = LLVMBuildSIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf(
                    "WARNING on line %llu column %llu: Converting an "
                    "unsigned integer type to a float during multiplication, "
                    "possible loss of data\n",
                    token->lineNum, token->colNum);
            } else if (left->type->type == UNSIGNED32) {
                *(left->value) = LLVMBuildUIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during multiplication, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == DOUBLE) {
                *(left->value) = LLVMBuildFPTrunc(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during multiplication, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only multiply types "
                    "double, float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        if (right->type->type != FLOAT) {
            if (right->type->type == INT32) {
                *(right->value) = LLVMBuildSIToFP(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during multiplication, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == UNSIGNED32) {
                *(right->value) = LLVMBuildUIToFP(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf(
                    "WARNING on line %llu column %llu: Converting an "
                    "unsigned integer type to a float during multiplication, "
                    "possible loss of data\n",
                    token->lineNum, token->colNum);
            } else if (right->type->type == DOUBLE) {
                *(right->value) = LLVMBuildFPTrunc(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf(
                    "WARNING on line %llu column %llu: Converting an "
                    "unsigned integer type to a float during multiplication, "
                    "possible loss of data\n",
                    token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only multiply types "
                    "double, float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        *(val->value) =
            LLVMBuildFMul(module->builder, *(left->value), *(right->value), "");
        val->type->type = FLOAT;
    } else {
        if ((left->type->type != INT32 && left->type->type != UNSIGNED32) ||
            (right->type->type != INT32 && right->type->type != UNSIGNED32)) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Can only multiply types "
                    "double, float, int, uint\n",
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
    struct ValueData *left = generateToken(((struct Token **)token->data)[1],
                                           module, false, true, false, false);
    if (left == NULL) {
        return NULL;
    }
    struct ValueData *right =
        generateToken(((struct Token **)token->data)[2], module, false, true,
                      false, left->type->type == DOUBLE);
    if (right == NULL) {
        return NULL;
    }
    struct ValueData *val = malloc(sizeof(struct ValueData));
    val->type = malloc(sizeof(struct TypeData));
    val->isStatic = left->isStatic && right->isStatic;
    val->type->length = -1;
    val->value = malloc(sizeof(LLVMValueRef));
    if (left->type->type == DOUBLE || right->type->type == DOUBLE) {
        if (left->type->type != DOUBLE) {
            if (left->type->type == INT32) {
                *(left->value) = LLVMBuildSIToFP(
                    module->builder, *(left->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during division, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == UNSIGNED32) {
                *(left->value) = LLVMBuildUIToFP(
                    module->builder, *(left->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during division, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == FLOAT) {
                *(left->value) = LLVMBuildFPExt(
                    module->builder, *(left->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting a "
                       "double type to a float during division, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Can only divide types "
                        "double, float, int, uint\n",
                        token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        if (right->type->type != DOUBLE) {
            if (right->type->type == INT32) {
                *(right->value) = LLVMBuildSIToFP(
                    module->builder, *(right->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during division, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == UNSIGNED32) {
                *(right->value) = LLVMBuildUIToFP(
                    module->builder, *(right->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during division, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == FLOAT) {
                *(right->value) = LLVMBuildFPExt(
                    module->builder, *(right->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting a "
                       "double type to a float during division, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Can only divide types "
                        "double, float, int, uint\n",
                        token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        *(val->value) =
            LLVMBuildFDiv(module->builder, *(left->value), *(right->value), "");
        val->type->type = DOUBLE;
    } else if (left->type->type == FLOAT || right->type->type == FLOAT) {
        if (left->type->type != FLOAT) {
            if (left->type->type == INT32) {
                *(left->value) = LLVMBuildSIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during division, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == UNSIGNED32) {
                *(left->value) = LLVMBuildUIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during division, possible lost "
                       "of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == DOUBLE) {
                *(left->value) = LLVMBuildFPTrunc(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during division, possible lost "
                       "of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Can only divide types "
                        "double, float, int, uint\n",
                        token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        if (right->type->type != FLOAT) {
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
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == DOUBLE) {
                *(right->value) = LLVMBuildFPTrunc(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during division, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Can only divide types "
                        "double, float, int, uint\n",
                        token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        *(val->value) =
            LLVMBuildFDiv(module->builder, *(left->value), *(right->value), "");
        val->type->type = FLOAT;
    } else if (left->type->type == INT32 || right->type->type == INT32) {
        if (left->type->type != INT32) {
            if (left->type->type != UNSIGNED32) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Can only divide types "
                        "double, float, int, uint\n",
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
                        "double, float, int, uint\n",
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
                    "double, float, int, uint\n",
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
                        "double, float, int, uint\n",
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
                        "double, float, int, uint\n",
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
    struct ValueData *left = generateToken(((struct Token **)token->data)[1],
                                           module, false, true, false, false);
    if (left == NULL) {
        return NULL;
    }
    struct ValueData *right =
        generateToken(((struct Token **)token->data)[2], module, false, true,
                      false, left->type->type == DOUBLE);
    if (right == NULL) {
        return NULL;
    }
    struct ValueData *val = malloc(sizeof(struct ValueData));
    val->value = malloc(sizeof(LLVMValueRef));
    val->type = malloc(sizeof(struct TypeData));
    val->isStatic = left->isStatic && right->isStatic;
    val->type->length = -1;
    if (left->type->type == DOUBLE || right->type->type == DOUBLE) {
        if (left->type->type != DOUBLE) {
            if (left->type->type == INT32) {
                *(left->value) = LLVMBuildSIToFP(
                    module->builder, *(left->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf(
                    "WARNING on line %llu column %llu: Converting an "
                    "unsigned integer type to a float during equals operation, "
                    "possible loss of data\n",
                    token->lineNum, token->colNum);
            } else if (left->type->type == UNSIGNED32) {
                *(left->value) = LLVMBuildUIToFP(
                    module->builder, *(left->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf(
                    "WARNING on line %llu column %llu: Converting an "
                    "unsigned integer type to a float during equals operation, "
                    "possible loss of data\n",
                    token->lineNum, token->colNum);
            } else if (left->type->type == FLOAT) {
                *(left->value) = LLVMBuildFPExt(
                    module->builder, *(left->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting a "
                       "double type to a float during equals operation, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only compare types "
                    "double, float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        if (right->type->type != DOUBLE) {
            if (right->type->type == INT32) {
                *(right->value) = LLVMBuildSIToFP(
                    module->builder, *(right->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during equals operation, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == UNSIGNED32) {
                *(right->value) = LLVMBuildUIToFP(
                    module->builder, *(right->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf(
                    "WARNING on line %llu column %llu: Converting an "
                    "unsigned integer type to a float during equals operation, "
                    "possible loss of data\n",
                    token->lineNum, token->colNum);
            } else if (right->type->type == FLOAT) {
                *(right->value) = LLVMBuildFPExt(
                    module->builder, *(right->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting a "
                       "double type to a float during equals operation, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only compare types "
                    "double, float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        *(val->value) = LLVMBuildFCmp(module->builder, LLVMRealOEQ,
                                      *(left->value), *(right->value), "");
        val->type->type = FLOAT;
    } else if (left->type->type == FLOAT || right->type->type == FLOAT) {
        if (left->type->type != FLOAT) {
            if (left->type->type == INT32) {
                *(left->value) = LLVMBuildSIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf(
                    "WARNING on line %llu column %llu: Converting an "
                    "unsigned integer type to a float during equals operation, "
                    "possible loss of data\n",
                    token->lineNum, token->colNum);
            } else if (left->type->type == UNSIGNED32) {
                *(left->value) = LLVMBuildUIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during equals operation, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == DOUBLE) {
                *(left->value) = LLVMBuildFPTrunc(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during equals operation, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only compare types "
                    "double, float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        if (right->type->type != FLOAT) {
            if (right->type->type == INT32) {
                *(right->value) = LLVMBuildSIToFP(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during equals operation, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == UNSIGNED32) {
                *(right->value) = LLVMBuildUIToFP(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf(
                    "WARNING on line %llu column %llu: Converting an "
                    "unsigned integer type to a float during equals operation, "
                    "possible loss of data\n",
                    token->lineNum, token->colNum);
            } else if (right->type->type == DOUBLE) {
                *(right->value) = LLVMBuildFPTrunc(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf(
                    "WARNING on line %llu column %llu: Converting an "
                    "unsigned integer type to a float during equals operation, "
                    "possible loss of data\n",
                    token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only compare types "
                    "double, float, int, uint\n",
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
                    "double, float, int, uint\n",
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
    struct ValueData *left = generateToken(((struct Token **)token->data)[1],
                                           module, false, true, false, false);
    if (left == NULL) {
        return NULL;
    }
    struct ValueData *right =
        generateToken(((struct Token **)token->data)[2], module, false, true,
                      false, left->type->type == DOUBLE);
    if (right == NULL) {
        return NULL;
    }
    struct ValueData *val = malloc(sizeof(struct ValueData));
    val->value = malloc(sizeof(LLVMValueRef));
    val->isStatic = left->isStatic && right->isStatic;
    val->type = malloc(sizeof(struct TypeData));
    val->type->length = -1;
    if (left->type->type == DOUBLE || right->type->type == DOUBLE) {
        if (left->type->type != DOUBLE) {
            if (left->type->type == INT32) {
                *(left->value) = LLVMBuildSIToFP(
                    module->builder, *(left->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during less than "
                       "operation, possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == UNSIGNED32) {
                *(left->value) = LLVMBuildUIToFP(
                    module->builder, *(left->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during less than "
                       "operation, possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == FLOAT) {
                *(left->value) = LLVMBuildFPExt(
                    module->builder, *(left->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting a "
                       "double type to a float during less than operation, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only compare types "
                    "double, float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        if (right->type->type != DOUBLE) {
            if (right->type->type == INT32) {
                *(right->value) = LLVMBuildSIToFP(
                    module->builder, *(right->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during less than operation, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == UNSIGNED32) {
                *(right->value) = LLVMBuildUIToFP(
                    module->builder, *(right->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during less than "
                       "operation, possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == FLOAT) {
                *(right->value) = LLVMBuildFPExt(
                    module->builder, *(right->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting a "
                       "double type to a float during less than operation, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only compare types "
                    "double, float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        *(val->value) = LLVMBuildFCmp(module->builder, LLVMRealOLT,
                                      *(left->value), *(right->value), "");
        val->type->type = FLOAT;
    } else if (left->type->type == FLOAT || right->type->type == FLOAT) {
        if (left->type->type != FLOAT) {
            if (left->type->type == INT32) {
                *(left->value) = LLVMBuildSIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during less than "
                       "operation, possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == UNSIGNED32) {
                *(left->value) = LLVMBuildUIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during less than operation, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == DOUBLE) {
                *(left->value) = LLVMBuildFPTrunc(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during less than operation, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only compare types "
                    "double, float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        if (right->type->type != FLOAT) {
            if (right->type->type == INT32) {
                *(right->value) = LLVMBuildSIToFP(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during less than operation, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == UNSIGNED32) {
                *(right->value) = LLVMBuildUIToFP(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during less than "
                       "operation, possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == DOUBLE) {
                *(right->value) = LLVMBuildFPTrunc(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during less than "
                       "operation, possible loss of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only compare types "
                    "double, float, int, uint\n",
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
                    "double, float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
            printf("WARNING on line %llu column %llu: Converting an unsigned "
                   "integer to an integer during less than operation, possible "
                   "loss of data\n",
                   token->lineNum, token->colNum);
        }
        if (right->type->type != INT32) {
            if (right->type->type != UNSIGNED32) {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only compare types "
                    "double, float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
            printf("WARNING on line %llu column %llu: Converting an unsigned "
                   "integer to an integer during less than operation, possible "
                   "loss of data\n",
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
                    "compare types double, float, int, uint\n",
                    token->lineNum, token->colNum);
            free(val->type);
            free(val);
            return NULL;
        }
        if (left->type->type != UNSIGNED32) {
            if (left->type->type != INT32) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Can only "
                        "compare types double, float, int, uint\n",
                        token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
            printf("WARNING on line %llu column %llu: Converting an "
                   "integer to an unsigned integer during less than operation, "
                   "possible loss of data\n",
                   token->lineNum, token->colNum);
        }
        if (right->type->type != UNSIGNED32) {
            if (right->type->type != INT32) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Can only divide types "
                        "double, float, int, uint\n",
                        token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
            printf("WARNING on line %llu column %llu: Converting an integer "
                   "to an unsigned integer during less than operation, "
                   "possible loss of data\n",
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
    struct ValueData *left = generateToken(((struct Token **)token->data)[1],
                                           module, false, true, false, false);
    if (left == NULL) {
        return NULL;
    }
    struct ValueData *right =
        generateToken(((struct Token **)token->data)[2], module, false, true,
                      false, left->type->type == DOUBLE);
    if (right == NULL) {
        return NULL;
    }
    struct ValueData *val = malloc(sizeof(struct ValueData));
    val->value = malloc(sizeof(LLVMValueRef));
    val->isStatic = left->isStatic && right->isStatic;
    val->type = malloc(sizeof(struct TypeData));
    val->type->length = -1;
    if (left->type->type == DOUBLE || right->type->type == DOUBLE) {
        if (left->type->type != DOUBLE) {
            if (left->type->type == INT32) {
                *(left->value) = LLVMBuildSIToFP(
                    module->builder, *(left->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during more than "
                       "operation, possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == UNSIGNED32) {
                *(left->value) = LLVMBuildUIToFP(
                    module->builder, *(left->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during more than "
                       "operation, possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == FLOAT) {
                *(left->value) = LLVMBuildFPExt(
                    module->builder, *(left->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting a "
                       "double type to a float during more than operation, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only compare types "
                    "double, float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        if (right->type->type != DOUBLE) {
            if (right->type->type == INT32) {
                *(right->value) = LLVMBuildSIToFP(
                    module->builder, *(right->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during more than operation, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == UNSIGNED32) {
                *(right->value) = LLVMBuildUIToFP(
                    module->builder, *(right->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during more than "
                       "operation, possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == FLOAT) {
                *(right->value) = LLVMBuildFPExt(
                    module->builder, *(right->value),
                    LLVMDoubleTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting a "
                       "double type to a float during more than operation, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only compare types "
                    "double, float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        *(val->value) = LLVMBuildFCmp(module->builder, LLVMRealOGT,
                                      *(left->value), *(right->value), "");
        val->type->type = FLOAT;
    } else if (left->type->type == FLOAT || right->type->type == FLOAT) {
        if (left->type->type != FLOAT) {
            if (left->type->type == INT32) {
                *(left->value) = LLVMBuildSIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during greater than "
                       "operation, possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == UNSIGNED32) {
                *(left->value) = LLVMBuildUIToFP(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during greater than operation, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (left->type->type == DOUBLE) {
                *(left->value) = LLVMBuildFPTrunc(
                    module->builder, *(left->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during greater than operation, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only compare types "
                    "double, float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
        }
        if (right->type->type != FLOAT) {
            if (right->type->type == INT32) {
                *(right->value) = LLVMBuildSIToFP(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "integer type to a float during greater than operation, "
                       "possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == UNSIGNED32) {
                *(right->value) = LLVMBuildUIToFP(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during greater than "
                       "operation, possible loss of data\n",
                       token->lineNum, token->colNum);
            } else if (right->type->type == DOUBLE) {
                *(right->value) = LLVMBuildFPTrunc(
                    module->builder, *(right->value),
                    LLVMFloatTypeInContext(module->context), "");
                printf("WARNING on line %llu column %llu: Converting an "
                       "unsigned integer type to a float during greater than "
                       "operation, possible loss of data\n",
                       token->lineNum, token->colNum);
            } else {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only compare types "
                    "double, float, int, uint\n",
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
                    "double, float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
            printf(
                "WARNING on line %llu column %llu: Converting an unsigned "
                "integer to an integer during greater than operation, possible "
                "loss of data\n",
                token->lineNum, token->colNum);
        }
        if (right->type->type != INT32) {
            if (right->type->type != UNSIGNED32) {
                fprintf(
                    stderr,
                    "ERROR on line %llu column %llu: Can only compare types "
                    "double, float, int, uint\n",
                    token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
            printf(
                "WARNING on line %llu column %llu: Converting an unsigned "
                "integer to an integer during greater than operation, possible "
                "loss of data\n",
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
                    "compare types double, float, int, uint\n",
                    token->lineNum, token->colNum);
            free(val->type);
            free(val);
            return NULL;
        }
        if (left->type->type != UNSIGNED32) {
            if (left->type->type != INT32) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Can only "
                        "compare types double, float, int, uint\n",
                        token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
            printf(
                "WARNING on line %llu column %llu: Converting an "
                "integer to an unsigned integer during greater than operation, "
                "possible loss of data\n",
                token->lineNum, token->colNum);
        }
        if (right->type->type != UNSIGNED32) {
            if (right->type->type != INT32) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Can only divide types "
                        "double, float, int, uint\n",
                        token->lineNum, token->colNum);
                free(val->type);
                free(val);
                return NULL;
            }
            printf("WARNING on line %llu column %llu: Converting an integer "
                   "to an unsigned integer during greater than operation, "
                   "possible loss of data\n",
                   token->lineNum, token->colNum);
        }
        *(val->value) = LLVMBuildICmp(module->builder, LLVMIntUGT,
                                      *(left->value), *(right->value), "");
        val->type->type = BOOL;
    }
    return val;
}

struct ValueData *generateBlock(struct Token *token, struct ModuleData *module,
                                size_t exprLen, size_t startTokenIdx,
                                bool *returned) {
    for (size_t i = startTokenIdx; i < exprLen; i++) {
        struct Token *token2 = ((struct Token **)token->data)[i];
        if (token2->type == EXPR_TOKEN &&
            ((struct Token **)(token2->data))[0]->type == IDENT_TOKEN &&
            strcmp("return",
                   (char *)(((struct Token **)(token2->data))[0]->data)) == 0) {
            *returned = true;
        }
        struct ValueData *val =
            generateToken(token2, module, false, true, false, false);
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

struct ValueData *generateDolist(struct Token *token, struct ModuleData *module,
                                 size_t exprLen) {
    if (exprLen < 3) {
        fprintf(
            stderr,
            "ERROR on line %llu column %llu: Too few arguments for dolist "
            "(dolist <array_or_vector> <variable_name_for_iteration_item> ...)",
            token->lineNum, token->colNum);
        return NULL;
    }

    LLVMBasicBlockRef condBlock = LLVMAppendBasicBlock(
        module->contexts[module->numContexts - 1]->func, "dolistCond");
    LLVMBasicBlockRef bodyBlock = LLVMAppendBasicBlock(
        module->contexts[module->numContexts - 1]->func, "dolistBody");
    LLVMBasicBlockRef endBlock = LLVMAppendBasicBlock(
        module->contexts[module->numContexts - 1]->func, "dolistEnd");
    LLVMBasicBlockRef lastCurrentBlock =
        module->contexts[module->numContexts - 1]->currentBlock;

    struct ValueData *llvmList = generateToken(
        ((struct Token **)token->data)[1], module, false, false, false, false);
    if (llvmList == NULL) {
        return NULL;
    }
    LLVMTypeRef *llvmListType = generateType(llvmList->type->otherType, module);
    LLVMTypeRef *itemLlvmType =
        generateType(llvmList->type->otherType->otherType, module);
    if (llvmListType == NULL) {
        return NULL;
    }
    if (itemLlvmType == NULL) {
        return NULL;
    }
    if (llvmList->type->type != POINTER ||
        llvmList->type->otherType->type != VECTOR &&
            llvmList->type->otherType->type != ARRAY) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected a pointer to the "
                "array or vector as the list to iterate\n",
                ((struct Token **)token->data)[1]->lineNum,
                ((struct Token **)token->data)[1]->colNum);
        return NULL;
    }
    if (((struct Token **)token->data)[2]->type != IDENT_TOKEN) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected an identifier as the "
                "variable name to use for the current item of the iteration\n",
                ((struct Token **)token->data)[2]->lineNum,
                ((struct Token **)token->data)[2]->colNum);
        return NULL;
    }
    char *itemVarName = (char *)((struct Token **)token->data)[2]->data;
    if (stbds_shgetp_null(*(module->variables), itemVarName) != NULL ||
        stbds_shgetp_null(
            module->contexts[module->numContexts - 1]->localVariables,
            itemVarName) != NULL) {
        fprintf(
            stderr,
            "ERROR on line %llu column %llu: Variable \"%s\" already exists\n",
            ((struct Token **)token->data)[2]->lineNum,
            ((struct Token **)token->data)[2]->colNum, itemVarName);
        return NULL;
    }
    LLVMValueRef listLen;
    LLVMValueRef llvmListEp;
    if (llvmList->type->otherType->type == ARRAY) {
        listLen = LLVMConstInt(LLVMInt32TypeInContext(module->context),
                               llvmList->type->otherType->length - 1, 0);
    } else {
        llvmListEp = LLVMBuildStructGEP2(module->builder, *llvmListType,
                                         *(llvmList->value), 0, "");
        llvmListEp = LLVMBuildLoad2(
            module->builder, LLVMPointerType(*itemLlvmType, 0), llvmListEp, "");
        LLVMValueRef listLenEP = LLVMBuildStructGEP2(
            module->builder, *llvmListType, *(llvmList->value), 1, "");
        listLen = LLVMBuildLoad2(module->builder,
                                 LLVMInt32TypeInContext(module->context),
                                 listLenEP, "");
        listLen = LLVMBuildSub(
            module->builder, listLen,
            LLVMConstInt(LLVMInt32TypeInContext(module->context), 1, 0), "");
    }

    LLVMPositionBuilderAtEnd(
        module->builder,
        module->contexts[module->numContexts - 1]->allocaBlock);
    LLVMValueRef idxVar = LLVMBuildAlloca(
        module->builder, LLVMInt32TypeInContext(module->context), "");

    LLVMPositionBuilderAtEnd(module->builder, lastCurrentBlock);
    LLVMBuildStore(module->builder,
                   LLVMConstInt(LLVMInt32TypeInContext(module->context), 0, 0),
                   idxVar);

    LLVMPositionBuilderAtEnd(module->builder, condBlock);
    LLVMValueRef loadedIdx = LLVMBuildLoad2(
        module->builder, LLVMInt32TypeInContext(module->context), idxVar, "");
    LLVMValueRef cond =
        LLVMBuildICmp(module->builder, LLVMIntUGT, loadedIdx, listLen, "");
    LLVMBuildCondBr(module->builder, cond, endBlock, bodyBlock);

    LLVMPositionBuilderAtEnd(module->builder, bodyBlock);
    struct ContextData *context = malloc(sizeof(struct ContextData));
    context->func = module->contexts[module->numContexts - 1]->func;
    context->unreachable = false;
    context->allocaBlock =
        module->contexts[module->numContexts - 1]->allocaBlock;
    context->currentBlock = bodyBlock;
    context->outOfBoundsErrorBlock =
        module->contexts[module->numContexts - 1]->outOfBoundsErrorBlock;
    context->continueBlock = malloc(sizeof(LLVMBasicBlockRef));
    *(context->continueBlock) = condBlock;
    context->breakBlock = malloc(sizeof(LLVMBasicBlockRef));
    *(context->breakBlock) = endBlock;
    context->loopIndexVar = malloc(sizeof(LLVMValueRef));
    *(context->loopIndexVar) = idxVar;
    context->localVariables = NULL;
    stbds_sh_new_arena(context->localVariables);
    size_t numVars = stbds_shlen(context->localVariables);
    for (size_t i = 0; i < numVars; i++) {
        stbds_shput(
            context->localVariables,
            module->contexts[module->numContexts - 1]->localVariables[i].key,
            module->contexts[module->numContexts - 1]->localVariables[i].value);
    }
    context->args = module->contexts[module->numContexts - 1]->args;
    context->macroArgs = module->contexts[module->numContexts - 1]->macroArgs;
    context->macroRestArg =
        module->contexts[module->numContexts - 1]->macroRestArg;
    context->returnType = module->contexts[module->numContexts - 1]->returnType;
    context->isVarArg = module->contexts[module->numContexts - 1]->isVarArg;
    context->mallocedVarsToFree =
        module->contexts[module->numContexts - 1]->mallocedVarsToFree;
    stbds_arrpush(module->contexts, context);
    module->numContexts++;

    LLVMValueRef *loopEP = malloc(sizeof(LLVMValueRef));
    if (llvmList->type->otherType->type == ARRAY) {
        *loopEP = LLVMBuildGEP2(
            module->builder, *llvmListType, (*llvmList->value),
            (LLVMValueRef[]){
                LLVMConstInt(LLVMInt32TypeInContext(module->context), 0, 0),
                loadedIdx},
            2, itemVarName);
    } else {
        *loopEP = LLVMBuildGEP2(module->builder, *itemLlvmType, llvmListEp,
                                &loadedIdx, 1, itemVarName);
    }
    struct VariableData itemVar = {llvmList->type->otherType->otherType,
                                   itemLlvmType, loopEP};
    stbds_shput(module->contexts[module->numContexts - 1]->localVariables,
                itemVarName, itemVar);

    bool returned = false;
    struct ValueData *blockRet =
        generateBlock(token, module, exprLen, 2, &returned);
    if (blockRet == NULL) {
        return NULL;
    }
    LLVMValueRef idxAddVal = LLVMBuildAdd(
        module->builder,
        LLVMBuildLoad2(module->builder, LLVMInt32TypeInContext(module->context),
                       idxVar, ""),
        LLVMConstInt(LLVMInt32TypeInContext(module->context), 1, 0), "");
    LLVMBuildStore(module->builder, idxAddVal, idxVar);

    LLVMBasicBlockRef lastLastCurrentBlock =
        module->contexts[module->numContexts - 1]->currentBlock;
    module->numContexts--;
    stbds_arrpop(module->contexts);

    LLVMTypeRef *blockLlvmType = generateType(blockRet->type, module);
    struct TypeData *optionalType = malloc(sizeof(struct TypeData));
    optionalType->type = NULLABLE;
    optionalType->otherType = blockRet->type;
    optionalType->length = -1;
    LLVMTypeRef *optionalLLvmType = generateType(optionalType, module);
    if (blockLlvmType == NULL || optionalLLvmType == NULL) {
        return NULL;
    }

    LLVMPositionBuilderAtEnd(
        module->builder,
        module->contexts[module->numContexts - 1]->allocaBlock);
    LLVMValueRef boolAlloca = LLVMBuildAlloca(
        module->builder, LLVMInt1TypeInContext(module->context), "");
    LLVMValueRef blockAlloca =
        LLVMBuildAlloca(module->builder, *blockLlvmType, "");

    LLVMPositionBuilderBefore(module->builder,
                              LLVMGetFirstInstruction(bodyBlock));
    LLVMBuildStore(module->builder,
                   LLVMConstInt(LLVMInt1TypeInContext(module->context), 1, 0),
                   boolAlloca);
    LLVMBuildStore(module->builder, LLVMConstNull(*blockLlvmType), blockAlloca);

    LLVMPositionBuilderAtEnd(module->builder, lastLastCurrentBlock);
    LLVMBuildStore(module->builder, *(blockRet->value), blockAlloca);
    LLVMBuildStore(module->builder,
                   LLVMConstInt(LLVMInt1TypeInContext(module->context), 0, 0),
                   boolAlloca);
    LLVMBuildBr(module->builder, condBlock);

    LLVMPositionBuilderAtEnd(module->builder, endBlock);
    module->contexts[module->numContexts - 1]->currentBlock = endBlock;
    LLVMValueRef blockVal =
        LLVMBuildLoad2(module->builder, *blockLlvmType, blockAlloca, "");
    LLVMValueRef retVal = LLVMGetUndef(*optionalLLvmType);
    retVal = LLVMBuildInsertValue(module->builder, retVal, blockVal, 0, "");
    LLVMValueRef boolVal =
        LLVMBuildLoad2(module->builder, LLVMInt1TypeInContext(module->context),
                       boolAlloca, "");
    retVal = LLVMBuildInsertValue(module->builder, retVal, boolVal, 1, "");

    LLVMPositionBuilderAtEnd(module->builder, lastCurrentBlock);
    LLVMBuildBr(module->builder, condBlock);
    LLVMPositionBuilderAtEnd(
        module->builder,
        module->contexts[module->numContexts - 1]->currentBlock);

    free(optionalLLvmType);
    free(blockLlvmType);
    free(llvmListType);
    free(llvmList->value);
    free(llvmList);

    struct ValueData *ret = malloc(sizeof(struct ValueData));
    ret->value = malloc(sizeof(LLVMValueRef));
    *(ret->value) = retVal;
    ret->type = optionalType;
    ret->type->otherType = blockRet->type;
    return ret;
}

struct ValueData *generateContinue(struct Token *token,
                                   struct ModuleData *module, size_t exprLen) {
    if (exprLen > 2) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too many arguments for "
                "continue\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    int idxIncrementValue = 1;
    if (exprLen == 2) {
        if (((struct Token **)token->data)[1]->type != INT_TOKEN) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Expected an integer\n",
                    ((struct Token **)token->data)[1]->lineNum,
                    ((struct Token **)token->data)[1]->colNum);
            return NULL;
        }
        idxIncrementValue = *(int *)((struct Token **)token->data)[1]->data;
    }
    if (module->contexts[module->numContexts - 1]->continueBlock == NULL) {
        fprintf(stderr, "ERROR on line %llu column %llu: Not inside a loop\n",
                token->lineNum, token->colNum);
    }
    if (module->contexts[module->numContexts - 1]->loopIndexVar != NULL) {
        LLVMValueRef loadedIdx = LLVMBuildLoad2(
            module->builder, LLVMInt32TypeInContext(module->context),
            *(module->contexts[module->numContexts - 1]->loopIndexVar), "");
        LLVMValueRef addedIdx =
            LLVMBuildAdd(module->builder, loadedIdx,
                         LLVMConstInt(LLVMInt32TypeInContext(module->context),
                                      idxIncrementValue, 0),
                         "");
        LLVMBuildStore(
            module->builder, addedIdx,
            *(module->contexts[module->numContexts - 1]->loopIndexVar));
    }
    LLVMBuildBr(module->builder,
                *(module->contexts[module->numContexts - 1]->continueBlock));
    module->contexts[module->numContexts - 1]->unreachable = true;
    struct ValueData *ret = malloc(sizeof(struct ValueData));
    ret->value = NULL;
    ret->type = malloc(sizeof(struct TypeData));
    ret->type->type = NIL;
    ret->type->length = -1;
    return ret;
}

struct ValueData *generateBreak(struct Token *token, struct ModuleData *module,
                                size_t exprLen) {
    if (exprLen > 1) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected no arguments for "
                "break\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    if (module->contexts[module->numContexts - 1]->continueBlock == NULL) {
        fprintf(stderr, "ERROR on line %llu column %llu: Not inside a loop\n",
                token->lineNum, token->colNum);
    }
    LLVMBuildBr(module->builder,
                *(module->contexts[module->numContexts - 1]->breakBlock));
    module->contexts[module->numContexts - 1]->unreachable = true;
    struct ValueData *ret = malloc(sizeof(struct ValueData));
    ret->value = NULL;
    ret->type = malloc(sizeof(struct TypeData));
    ret->type->type = NIL;
    ret->type->length = -1;
    return ret;
}

struct ValueData *generateWhile(struct Token *token, struct ModuleData *module,
                                size_t exprLen) {
    if (exprLen < 2) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too few arguments for while "
                "loop (while <condition> <body1> ...)",
                token->lineNum, token->colNum);
        return NULL;
    }

    LLVMBasicBlockRef condBlock = LLVMAppendBasicBlock(
        module->contexts[module->numContexts - 1]->func, "whileCond");
    LLVMBasicBlockRef bodyBlock = LLVMAppendBasicBlock(
        module->contexts[module->numContexts - 1]->func, "whileBody");
    LLVMBasicBlockRef endBlock = LLVMAppendBasicBlock(
        module->contexts[module->numContexts - 1]->func, "whileEnd");

    LLVMBuildBr(module->builder, condBlock);

    LLVMPositionBuilderAtEnd(module->builder, condBlock);
    LLVMValueRef *cond = generateTokenOfType(
        ((struct Token **)token->data)[1],
        (struct TypeData){BOOL, NULL, NULL, -1, NULL}, module);
    if (cond == NULL) {
        return NULL;
    }
    LLVMBuildCondBr(module->builder, *cond, bodyBlock, endBlock);
    free(cond);

    LLVMPositionBuilderAtEnd(module->builder, bodyBlock);
    struct ContextData *context = malloc(sizeof(struct ContextData));
    context->func = module->contexts[module->numContexts - 1]->func;
    context->unreachable = false;
    context->allocaBlock =
        module->contexts[module->numContexts - 1]->allocaBlock;
    context->currentBlock = bodyBlock;
    context->outOfBoundsErrorBlock =
        module->contexts[module->numContexts - 1]->outOfBoundsErrorBlock;
    context->continueBlock = malloc(sizeof(LLVMBasicBlockRef));
    *(context->continueBlock) = condBlock;
    context->breakBlock = malloc(sizeof(LLVMBasicBlockRef));
    *(context->breakBlock) = endBlock;
    context->loopIndexVar = NULL;
    context->localVariables = NULL;
    stbds_sh_new_arena(context->localVariables);
    size_t numVars = stbds_shlen(context->localVariables);
    for (size_t i = 0; i < numVars; i++) {
        stbds_shput(
            context->localVariables,
            module->contexts[module->numContexts - 1]->localVariables[i].key,
            module->contexts[module->numContexts - 1]->localVariables[i].value);
    }
    context->args = module->contexts[module->numContexts - 1]->args;
    context->macroArgs = module->contexts[module->numContexts - 1]->macroArgs;
    context->macroRestArg =
        module->contexts[module->numContexts - 1]->macroRestArg;
    context->returnType = module->contexts[module->numContexts - 1]->returnType;
    context->isVarArg = module->contexts[module->numContexts - 1]->isVarArg;
    context->mallocedVarsToFree =
        module->contexts[module->numContexts - 1]->mallocedVarsToFree;
    stbds_arrpush(module->contexts, context);
    module->numContexts++;

    bool returned = false;
    struct ValueData *blockRet =
        generateBlock(token, module, exprLen, 2, &returned);
    if (blockRet == NULL) {
        return NULL;
    }

    LLVMBasicBlockRef lastCurrentBlock =
        module->contexts[module->numContexts - 1]->currentBlock;
    module->numContexts--;
    stbds_arrpop(module->contexts);

    LLVMTypeRef *blockLlvmType = generateType(blockRet->type, module);
    struct TypeData *optionalType = malloc(sizeof(struct TypeData));
    optionalType->type = NULLABLE;
    optionalType->otherType = blockRet->type;
    optionalType->length = -1;
    LLVMTypeRef *optionalLLvmType = generateType(optionalType, module);
    if (blockLlvmType == NULL || optionalLLvmType == NULL) {
        return NULL;
    }

    LLVMPositionBuilderAtEnd(
        module->builder,
        module->contexts[module->numContexts - 1]->allocaBlock);
    LLVMValueRef boolAlloca = LLVMBuildAlloca(
        module->builder, LLVMInt1TypeInContext(module->context), "");
    LLVMValueRef blockAlloca =
        LLVMBuildAlloca(module->builder, *blockLlvmType, "");

    LLVMPositionBuilderBefore(module->builder,
                              LLVMGetFirstInstruction(bodyBlock));
    LLVMBuildStore(module->builder,
                   LLVMConstInt(LLVMInt1TypeInContext(module->context), 1, 0),
                   boolAlloca);
    LLVMBuildStore(module->builder, LLVMConstNull(*blockLlvmType), blockAlloca);

    LLVMPositionBuilderAtEnd(module->builder, lastCurrentBlock);
    LLVMBuildStore(module->builder, *(blockRet->value), blockAlloca);
    LLVMBuildStore(module->builder,
                   LLVMConstInt(LLVMInt1TypeInContext(module->context), 0, 0),
                   boolAlloca);
    LLVMBuildBr(module->builder, condBlock);

    LLVMPositionBuilderAtEnd(module->builder, endBlock);
    module->contexts[module->numContexts - 1]->currentBlock = endBlock;
    LLVMValueRef blockVal =
        LLVMBuildLoad2(module->builder, *blockLlvmType, blockAlloca, "");
    LLVMValueRef retVal = LLVMGetUndef(*optionalLLvmType);
    retVal = LLVMBuildInsertValue(module->builder, retVal, blockVal, 0, "");
    LLVMValueRef boolVal =
        LLVMBuildLoad2(module->builder, LLVMInt1TypeInContext(module->context),
                       boolAlloca, "");
    retVal = LLVMBuildInsertValue(module->builder, retVal, boolVal, 1, "");
    free(optionalLLvmType);
    free(blockLlvmType);

    struct ValueData *ret = malloc(sizeof(struct ValueData));
    ret->value = malloc(sizeof(LLVMValueRef));
    *(ret->value) = retVal;
    ret->type = optionalType;
    ret->type->otherType = blockRet->type;
    return ret;
}

struct ValueData *generateDotimes(struct Token *token,
                                  struct ModuleData *module, size_t exprLen) {
    if (exprLen < 3) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too few arguments for dotimes "
                "(dotimes <number_of_times> '(<index_variable_name> "
                "<index_start_value>) <body1> ...)",
                token->lineNum, token->colNum);
        return NULL;
    }
    if (((struct Token **)token->data)[2]->type != LIST_TOKEN ||
        stbds_arrlen(
            (struct Token **)((struct Token **)token->data)[2]->data) != 2 ||
        ((struct Token **)((struct Token **)token->data)[2]->data)[0]->type !=
            IDENT_TOKEN) {
        fprintf(
            stderr,
            "ERROR on line %llu column %llu: Expected a list with the variable "
            "name and the start value as the second argument for dotimes "
            "(dotimes <number_of_times> '(<index_variable_name> "
            "<index_start_value>) <body1> ...)",
            token->lineNum, token->colNum);
        return NULL;
    }

    char *idxName =
        (char *)((struct Token **)((struct Token **)token->data)[2]->data)[0]
            ->data;

    LLVMBasicBlockRef condBlock = LLVMAppendBasicBlock(
        module->contexts[module->numContexts - 1]->func, "dotimesCond");
    LLVMBasicBlockRef bodyBlock = LLVMAppendBasicBlock(
        module->contexts[module->numContexts - 1]->func, "dotimesBody");
    LLVMBasicBlockRef endBlock = LLVMAppendBasicBlock(
        module->contexts[module->numContexts - 1]->func, "dotimesEnd");

    struct ValueData *idxStartValue = generateToken(
        ((struct Token **)((struct Token **)token->data)[2]->data)[1], module,
        false, false, false, false);
    if (idxStartValue == NULL) {
        return NULL;
    }
    if (idxStartValue->type->type != INT32 &&
        idxStartValue->type->type != UNSIGNED32 &&
        idxStartValue->type->type != UNSIGNED64 &&
        idxStartValue->type->type != FLOAT &&
        idxStartValue->type->type != DOUBLE) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected a number as "
                "the index start value\n",
                ((struct Token **)((struct Token **)token->data)[2]->data)[1]
                    ->lineNum,
                ((struct Token **)((struct Token **)token->data)[2]->data)[1]
                    ->colNum);
        return NULL;
    }
    LLVMTypeRef *idxStartValueLlvmType =
        generateType(idxStartValue->type, module);
    if (idxStartValueLlvmType == NULL) {
        return NULL;
    }
    LLVMValueRef *numTimes = generateTokenOfType(
        ((struct Token **)token->data)[1], *(idxStartValue->type), module);
    if (numTimes == NULL) {
        return NULL;
    }
    *numTimes = LLVMBuildSub(
        module->builder, *numTimes,
        LLVMConstInt(LLVMInt32TypeInContext(module->context), 1, 0), "");
    LLVMValueRef stopValue;
    LLVMValueRef *incrementNum = NULL;
    bool hasCustomIncrement = false;
    if (exprLen >= 5 &&
        ((struct Token **)token->data)[3]->type == IDENT_TOKEN &&
        strcmp(((struct Token **)token->data)[3]->data, ":increment") == 0) {
        incrementNum = generateTokenOfType(((struct Token **)token->data)[4],
                                           *(idxStartValue->type), module);
        if (incrementNum == NULL) {
            return NULL;
        }
        hasCustomIncrement = true;
        if (idxStartValue->type->type == FLOAT ||
            idxStartValue->type->type == DOUBLE) {
            stopValue = LLVMBuildFAdd(
                module->builder,
                LLVMBuildFMul(module->builder, *numTimes, *incrementNum, ""),
                *(idxStartValue->value), "");
        } else {
            stopValue = LLVMBuildAdd(
                module->builder,
                LLVMBuildMul(module->builder, *numTimes, *incrementNum, ""),
                *(idxStartValue->value), "");
        }
    } else {
        incrementNum = malloc(sizeof(LLVMValueRef));
        if (idxStartValue->type->type == FLOAT ||
            idxStartValue->type->type == DOUBLE) {
            *incrementNum = LLVMConstReal(*idxStartValueLlvmType, 1.0);
            stopValue = LLVMBuildFAdd(module->builder, *numTimes,
                                      *(idxStartValue->value), "");
        } else {
            *incrementNum =
                LLVMConstInt(*idxStartValueLlvmType, 1,
                             idxStartValue->type->type != UNSIGNED32 &&
                                 idxStartValue->type->type != UNSIGNED64);
            stopValue = LLVMBuildAdd(module->builder, *numTimes,
                                     *(idxStartValue->value), "");
        }
    }

    LLVMPositionBuilderAtEnd(
        module->builder,
        module->contexts[module->numContexts - 1]->allocaBlock);
    LLVMValueRef idxVar =
        LLVMBuildAlloca(module->builder, *idxStartValueLlvmType, idxName);
    LLVMPositionBuilderAtEnd(
        module->builder,
        module->contexts[module->numContexts - 1]->currentBlock);
    LLVMBuildStore(module->builder, *(idxStartValue->value), idxVar);
    LLVMBuildBr(module->builder, condBlock);

    LLVMPositionBuilderAtEnd(module->builder, condBlock);
    LLVMValueRef loadedIdxVar =
        LLVMBuildLoad2(module->builder, *idxStartValueLlvmType, idxVar, "");
    LLVMValueRef cond;
    if (idxStartValue->type->type == FLOAT ||
        idxStartValue->type->type == DOUBLE) {
        cond = LLVMBuildFCmp(module->builder, LLVMRealOGT, loadedIdxVar,
                             stopValue, "");
    } else {
        cond = LLVMBuildICmp(module->builder, LLVMIntUGT, loadedIdxVar,
                             stopValue, "");
    }
    free(numTimes);

    LLVMPositionBuilderAtEnd(module->builder, bodyBlock);
    struct ContextData *context = malloc(sizeof(struct ContextData));
    context->func = module->contexts[module->numContexts - 1]->func;
    context->unreachable = false;
    context->allocaBlock =
        module->contexts[module->numContexts - 1]->allocaBlock;
    context->currentBlock = bodyBlock;
    context->outOfBoundsErrorBlock =
        module->contexts[module->numContexts - 1]->outOfBoundsErrorBlock;
    context->continueBlock = malloc(sizeof(LLVMBasicBlockRef));
    *(context->continueBlock) = condBlock;
    context->breakBlock = malloc(sizeof(LLVMBasicBlockRef));
    *(context->breakBlock) = endBlock;
    context->loopIndexVar = malloc(sizeof(LLVMValueRef));
    *(context->loopIndexVar) = idxVar;
    context->localVariables = NULL;
    stbds_sh_new_arena(context->localVariables);
    size_t numVars = stbds_shlen(context->localVariables);
    for (size_t i = 0; i < numVars; i++) {
        stbds_shput(
            context->localVariables,
            module->contexts[module->numContexts - 1]->localVariables[i].key,
            module->contexts[module->numContexts - 1]->localVariables[i].value);
    }
    context->args = module->contexts[module->numContexts - 1]->args;
    context->macroArgs = module->contexts[module->numContexts - 1]->macroArgs;
    context->macroRestArg =
        module->contexts[module->numContexts - 1]->macroRestArg;
    context->returnType = module->contexts[module->numContexts - 1]->returnType;
    context->isVarArg = module->contexts[module->numContexts - 1]->isVarArg;
    context->mallocedVarsToFree =
        module->contexts[module->numContexts - 1]->mallocedVarsToFree;
    stbds_arrpush(module->contexts, context);
    module->numContexts++;
    if (stbds_shgetp_null(context->localVariables, idxName) != NULL ||
        stbds_shgetp_null(*(module->variables), idxName) != NULL) {
        fprintf(
            stderr,
            "ERROR on line %llu column %llu: Variable \"%s\" already exists\n",
            ((struct Token **)((struct Token **)token->data)[2]->data)[0]
                ->lineNum,
            ((struct Token **)((struct Token **)token->data)[2]->data)[0]
                ->colNum,
            idxName);
        return NULL;
    }
    LLVMValueRef *idxVarMalloc = malloc(sizeof(LLVMValueRef));
    *idxVarMalloc = idxVar;
    stbds_shput(context->localVariables, idxName,
                ((struct VariableData){idxStartValue->type,
                                       idxStartValueLlvmType, idxVarMalloc}));

    bool returned = false;
    struct ValueData *blockRet = generateBlock(
        token, module, exprLen, 3 + (2 * hasCustomIncrement), &returned);
    if (blockRet == NULL) {
        return NULL;
    }
    LLVMPositionBuilderAtEnd(module->builder, condBlock);
    // if (module->contexts[module->numContexts - 1]->currentBlock, )
    // ;
    LLVMBuildCondBr(module->builder, cond, endBlock, bodyBlock);

    LLVMPositionBuilderAtEnd(
        module->builder,
        module->contexts[module->numContexts - 1]->currentBlock);
    loadedIdxVar =
        LLVMBuildLoad2(module->builder, *idxStartValueLlvmType, idxVar, "");
    if (idxStartValue->type->type == FLOAT ||
        idxStartValue->type->type == DOUBLE) {
        loadedIdxVar =
            LLVMBuildFAdd(module->builder, loadedIdxVar, *incrementNum, "");
    } else {
        loadedIdxVar =
            LLVMBuildAdd(module->builder, loadedIdxVar, *incrementNum, "");
    }
    LLVMBuildStore(module->builder, loadedIdxVar, idxVar);
    free(incrementNum);
    free(idxStartValue->value);
    free(idxStartValue);

    bool isUnreachable = module->contexts[module->numContexts - 1]->unreachable;
    LLVMBasicBlockRef lastCurrentBlock =
        module->contexts[module->numContexts - 1]->currentBlock;
    module->numContexts--;
    stbds_arrpop(module->contexts);

    LLVMValueRef blockAlloca;
    LLVMValueRef boolAlloca;
    LLVMTypeRef *blockLlvmType;
    LLVMTypeRef *optionalLLvmType;
    struct TypeData *optionalType;
    if (blockRet->type->type != NIL) {
        blockLlvmType = generateType(blockRet->type, module);
        optionalType = malloc(sizeof(struct TypeData));
        optionalType->type = NULLABLE;
        optionalType->otherType = blockRet->type;
        optionalType->length = -1;
        optionalLLvmType = generateType(optionalType, module);
        if (blockLlvmType == NULL || optionalLLvmType == NULL) {
            return NULL;
        }

        LLVMPositionBuilderAtEnd(
            module->builder,
            module->contexts[module->numContexts - 1]->allocaBlock);
        boolAlloca = LLVMBuildAlloca(
            module->builder, LLVMInt1TypeInContext(module->context), "");
        blockAlloca = LLVMBuildAlloca(module->builder, *blockLlvmType, "");

        LLVMPositionBuilderBefore(module->builder,
                                  LLVMGetFirstInstruction(bodyBlock));
        LLVMBuildStore(
            module->builder,
            LLVMConstInt(LLVMInt1TypeInContext(module->context), 1, 0),
            boolAlloca);
        LLVMBuildStore(module->builder, LLVMConstNull(*blockLlvmType),
                       blockAlloca);
    }

    if (!isUnreachable) {
        LLVMPositionBuilderAtEnd(module->builder, lastCurrentBlock);
        if (blockRet->type->type != NIL) {
            LLVMBuildStore(module->builder, *(blockRet->value), blockAlloca);
            LLVMBuildStore(
                module->builder,
                LLVMConstInt(LLVMInt1TypeInContext(module->context), 0, 0),
                boolAlloca);
        }
        LLVMBuildBr(module->builder, condBlock);
    }

    if (blockRet->type->type == NIL) {
        struct ValueData *ret = malloc(sizeof(struct ValueData));
        ret->value = NULL;
        ret->type = malloc(sizeof(struct TypeData));
        ret->type->type = NIL;
        ret->type->length = -1;
        return ret;
    }

    LLVMPositionBuilderAtEnd(module->builder, endBlock);
    module->contexts[module->numContexts - 1]->currentBlock = endBlock;
    LLVMValueRef blockVal =
        LLVMBuildLoad2(module->builder, *blockLlvmType, blockAlloca, "");
    LLVMValueRef retVal = LLVMGetUndef(*optionalLLvmType);
    retVal = LLVMBuildInsertValue(module->builder, retVal, blockVal, 0, "");
    LLVMValueRef boolVal =
        LLVMBuildLoad2(module->builder, LLVMInt1TypeInContext(module->context),
                       boolAlloca, "");
    retVal = LLVMBuildInsertValue(module->builder, retVal, boolVal, 1, "");
    free(optionalLLvmType);
    free(blockLlvmType);

    struct ValueData *ret = malloc(sizeof(struct ValueData));
    ret->value = malloc(sizeof(LLVMValueRef));
    *(ret->value) = retVal;
    ret->type = optionalType;
    ret->type->otherType = blockRet->type;
    return ret;
}

struct ValueData *generateSet(struct Token *token, struct ModuleData *module,
                              size_t exprLen) {
    if (exprLen < 3) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too few arguments for setting "
                "a variable value (set <pointer_or_variable> <value>)\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    if (exprLen > 3) {
        fprintf(
            stderr,
            "ERROR on line %llu column %llu: Too many arguments for setting "
            "a variable value (set <pointer_or_variable> <value>)\n",
            token->lineNum, token->colNum);
        return NULL;
    }
    struct ValueData *var = generateToken(((struct Token **)token->data)[1],
                                          module, false, false, false, false);
    if (var == NULL) {
        return NULL;
    }
    if (var->type->type != POINTER) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected a pointer type (a "
                "variable that is not dereferenced or any other pointer)\n",
                ((struct Token **)token->data)[1]->lineNum,
                ((struct Token **)token->data)[1]->colNum);
        return NULL;
    }
    if (var->type->otherType->type == NULLABLE) {
        struct ValueData *val = generateToken(
            ((struct Token **)token->data)[2], module,
            var->type->otherType->otherType->type == POINTER &&
                var->type->otherType->otherType->otherType->type == CHAR,
            false, var->type->otherType->otherType->type == VECTOR,
            var->type->otherType->otherType->type == DOUBLE);
        if (val == NULL) {
            return NULL;
        }
        LLVMTypeRef *llvmStructType =
            generateType(var->type->otherType, module);
        LLVMTypeRef *llvmElementType =
            generateType(var->type->otherType->otherType, module);
        if (llvmStructType == NULL || llvmElementType == NULL) {
            return NULL;
        }
        if (val->type->type == NIL) {
            LLVMValueRef ep = LLVMBuildStructGEP2(
                module->builder, *llvmStructType, *(var->value), 0, "");
            LLVMBuildStore(module->builder, LLVMConstNull(*llvmElementType),
                           ep);
            ep = LLVMBuildStructGEP2(module->builder, *llvmStructType,
                                     *(var->value), 1, "");
            LLVMBuildStore(
                module->builder,
                LLVMConstInt(LLVMInt1TypeInContext(module->context), 1, 0), ep);
        } else {
            if (!cmptype(val->type, var->type->otherType->otherType,
                         ((struct Token **)token->data)[2]->lineNum,
                         ((struct Token **)token->data)[2]->colNum, true)) {
                return NULL;
            }
            LLVMValueRef ep = LLVMBuildStructGEP2(
                module->builder, *llvmStructType, *(var->value), 0, "");
            LLVMBuildStore(module->builder, *(val->value), ep);
            ep = LLVMBuildStructGEP2(module->builder, *llvmStructType,
                                     *(var->value), 1, "");
            LLVMBuildStore(
                module->builder,
                LLVMConstInt(LLVMInt1TypeInContext(module->context), 0, 0), ep);
        }
        free(llvmStructType);
        free(llvmElementType);
        free(val->value);
        free(val->type);
        free(val);
    } else {
        LLVMValueRef *val = generateTokenOfType(
            ((struct Token **)token->data)[2], *(var->type->otherType), module);
        if (val == NULL) {
            return NULL;
        }
        LLVMBuildStore(module->builder, *val, *(var->value));
        free(val);
    }

    return var;
}

struct ValueData *generateReturn(struct Token *token, struct ModuleData *module,
                                 size_t exprLen) {
    if (exprLen < 2) {
        fprintf(
            stderr,
            "ERROR on line %llu column %llu: Too few arguments for return\n",
            token->lineNum, token->colNum);
        return NULL;
    }
    if (exprLen > 2) {
        fprintf(
            stderr,
            "ERROR on line %llu column %llu: Too many arguments for return\n",
            token->lineNum, token->colNum);
        return NULL;
    }
    struct TypeData *retType =
        module->contexts[module->numContexts - 1]->returnType;
    if (retType == NULL) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Return not allowed inside "
                "this block\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    LLVMValueRef *val = generateTokenOfType(((struct Token **)token->data)[1],
                                            *retType, module);
    if (val == NULL) {
        return NULL;
    }
    struct ValueData *ret = malloc(sizeof(struct ValueData));
    ret->type = retType;
    ret->value = malloc(sizeof(LLVMValueRef));
    generateFreeMallocedVars(module);
    *(ret->value) = LLVMBuildRet(module->builder, *val);
    return ret;
}

struct ValueData *generateNth(struct Token *token, struct ModuleData *module,
                              size_t exprLen) {
    if (exprLen < 3) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too few arguments for "
                "nth - expected 2 arguments - (nth "
                "<index> <array/vector>)\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    if (exprLen > 3) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too many arguments for "
                "nth - expected 2 arguments - (nth "
                "<index> <array/vector>)\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    struct ValueData *val = generateToken(((struct Token **)token->data)[2],
                                          module, false, true, false, false);
    struct ValueData *idxVal = generateToken(((struct Token **)token->data)[1],
                                             module, false, true, false, false);
    if (val == NULL || idxVal == NULL) {
        return NULL;
    }
    if (val->type->type != POINTER ||
        val->type->otherType->type != ARRAY &&
            val->type->otherType->type != VECTOR &&
            val->type->otherType->type != STRING) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected a string pointer, a "
                "vector pointer or an array pointer\n",
                ((struct Token **)token->data)[2]->lineNum,
                ((struct Token **)token->data)[2]->colNum);
        return NULL;
    }
    if (idxVal->type->type != INT32 && idxVal->type->type != UNSIGNED32) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected an integer or an "
                "unsigned integer\n",
                ((struct Token **)token->data)[1]->lineNum,
                ((struct Token **)token->data)[1]->colNum);
        return NULL;
    }

    LLVMValueRef len;
    LLVMTypeRef *valType = generateType(val->type->otherType, module);
    if (val->type->otherType->type == ARRAY) {
        len = LLVMConstInt(LLVMInt32TypeInContext(module->context),
                           val->type->otherType->length, 0);
    } else {
        LLVMValueRef ep = LLVMBuildStructGEP2(module->builder, *valType,
                                              *(val->value), 1, "");
        len = LLVMBuildLoad2(module->builder,
                             LLVMInt32TypeInContext(module->context), ep, "");
    }
    LLVMValueRef isNegative = LLVMBuildICmp(
        module->builder, LLVMIntSLT, *(idxVal->value),
        LLVMConstInt(LLVMInt32TypeInContext(module->context), 0, 0), "");
    LLVMValueRef adjustedIdx =
        LLVMBuildAdd(module->builder, *(idxVal->value), len, "");
    LLVMValueRef finalIdx = LLVMBuildSelect(module->builder, isNegative,
                                            adjustedIdx, *(idxVal->value), "");

    LLVMValueRef lowerBound = LLVMBuildICmp(
        module->builder, LLVMIntSLT, finalIdx,
        LLVMConstInt(LLVMInt32TypeInContext(module->context), 0, 0), "");
    LLVMValueRef upperBound =
        LLVMBuildICmp(module->builder, LLVMIntSGE, finalIdx, len, "");
    LLVMValueRef outOfBounds =
        LLVMBuildOr(module->builder, lowerBound, upperBound, "");

    LLVMBasicBlockRef successBlock =
        LLVMAppendBasicBlock(module->contexts[module->numContexts - 1]->func,
                             "nthBoundsCheckSuccess");

    LLVMBuildCondBr(
        module->builder, outOfBounds,
        module->contexts[module->numContexts - 1]->outOfBoundsErrorBlock,
        successBlock);
    LLVMPositionBuilderAtEnd(module->builder, successBlock);
    module->contexts[module->numContexts - 1]->currentBlock = successBlock;

    struct ValueData *ret = malloc(sizeof(struct ValueData));
    ret->value = malloc(sizeof(LLVMValueRef));
    ret->isStatic = false;
    ret->type = val->type->otherType->otherType;
    LLVMTypeRef *type = generateType(val->type->otherType->otherType, module);
    if (type == NULL || valType == NULL) {
        return NULL;
    }
    if (val->type->otherType->type == ARRAY) {
        LLVMValueRef indices[2] = {
            LLVMConstInt(LLVMInt32TypeInContext(module->context), 0, 0),
            *(idxVal->value)};
        LLVMValueRef ep = LLVMBuildInBoundsGEP2(module->builder, *valType,
                                                *(val->value), indices, 2, "");
        *(ret->value) = LLVMBuildLoad2(module->builder, *type, ep, "");
    } else {
        LLVMValueRef arrayEp = LLVMBuildStructGEP2(module->builder, *valType,
                                                   *(val->value), 0, "");
        LLVMValueRef loadedArrayPtr = LLVMBuildLoad2(
            module->builder, LLVMPointerType(LLVMPointerType(*type, 0), 0),
            arrayEp, "");
        LLVMValueRef ep = LLVMBuildGEP2(module->builder, *type, loadedArrayPtr,
                                        &finalIdx, 1, "");
        *(ret->value) = LLVMBuildLoad2(module->builder, *type, ep, "");
    }
    free(type);
    free(valType);
    free(val);
    free(idxVal);
    return ret;
}

struct ValueData *generateSizeof(struct Token *token, struct ModuleData *module,
                                 size_t exprLen) {
    if (exprLen < 2) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too few arguments for "
                "sizeof - expected 1 argument - (sizeof "
                "<type>)\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    if (exprLen > 2) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Too many arguments for "
                "sizeof - expected 1 argument - (sizeof "
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
    if (type->type == VECTOR || type->type == STRING) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Can only generate the size of "
                "static types\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    LLVMTypeRef *llvmType = generateType(type, module);
    struct ValueData *val = malloc(sizeof(struct ValueData));
    val->value = malloc(sizeof(LLVMValueRef));
    val->type = malloc(sizeof(struct ValueData));
    *(val->value) = LLVMSizeOf(*llvmType);
    val->type->type = UNSIGNED64;
    val->type->length = -1;
    val->isStatic = true;
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
        module->context, module->contexts[module->numContexts - 1]->func,
        "then");
    LLVMBasicBlockRef mergeBlock = LLVMAppendBasicBlockInContext(
        module->context, module->contexts[module->numContexts - 1]->func,
        "merge");
    LLVMBasicBlockRef lastBlock =
        module->contexts[module->numContexts - 1]->currentBlock;

    LLVMPositionBuilderAtEnd(module->builder, thenBlock);
    struct ContextData *context = malloc(sizeof(struct ContextData));
    context->func = module->contexts[module->numContexts - 1]->func;
    context->unreachable = false;
    context->allocaBlock =
        module->contexts[module->numContexts - 1]->allocaBlock;
    context->currentBlock = thenBlock;
    context->outOfBoundsErrorBlock =
        module->contexts[module->numContexts - 1]->outOfBoundsErrorBlock;
    context->continueBlock =
        module->contexts[module->numContexts - 1]->continueBlock;
    context->breakBlock = module->contexts[module->numContexts - 1]->breakBlock;
    context->loopIndexVar =
        module->contexts[module->numContexts - 1]->loopIndexVar;
    context->localVariables = NULL;
    stbds_sh_new_arena(context->localVariables);
    size_t numVars = stbds_shlen(context->localVariables);
    for (size_t i = 0; i < numVars; i++) {
        stbds_shput(
            context->localVariables,
            module->contexts[module->numContexts - 1]->localVariables[i].key,
            module->contexts[module->numContexts - 1]->localVariables[i].value);
    }
    context->args = module->contexts[module->numContexts - 1]->args;
    context->macroArgs = module->contexts[module->numContexts - 1]->macroArgs;
    context->macroRestArg =
        module->contexts[module->numContexts - 1]->macroRestArg;
    context->returnType = module->contexts[module->numContexts - 1]->returnType;
    context->isVarArg = module->contexts[module->numContexts - 1]->isVarArg;
    context->mallocedVarsToFree =
        module->contexts[module->numContexts - 1]->mallocedVarsToFree;
    stbds_arrpush(module->contexts, context);
    module->numContexts++;

    LLVMPositionBuilderAtEnd(
        module->builder,
        module->contexts[module->numContexts - 1]->currentBlock);
    bool returned = false;
    struct ValueData *blockRet =
        generateBlock(token, module, exprLen, 2, &returned);
    if (blockRet == NULL) {
        return NULL;
    }
    module->numContexts--;
    stbds_arrpop(module->contexts);
    if (blockRet->type->type == NIL) {
        LLVMPositionBuilderAtEnd(module->builder, lastBlock);
        if (negate) {
            LLVMBuildCondBr(module->builder, *cond, mergeBlock, thenBlock);
        } else {
            LLVMBuildCondBr(module->builder, *cond, thenBlock, mergeBlock);
        }
        LLVMPositionBuilderAtEnd(module->builder, mergeBlock);
        module->contexts[module->numContexts - 1]->currentBlock = mergeBlock;

        struct ValueData *ret = malloc(sizeof(struct ValueData));
        ret->value = NULL;
        ret->type = malloc(sizeof(struct TypeData));
        ret->type->type = NIL;
        ret->type->length = -1;
        ret->isStatic = false;
        return ret;
    };
    LLVMTypeRef *blockLlvmType = generateType(blockRet->type, module);
    if (blockLlvmType == NULL) {
        return NULL;
    }

    LLVMPositionBuilderAtEnd(
        module->builder,
        module->contexts[module->numContexts - 1]->allocaBlock);
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
    LLVMPositionBuilderAtEnd(module->builder, thenBlock);
    LLVMBuildStore(module->builder,
                   LLVMConstInt(LLVMInt1TypeInContext(module->context), 0, 0),
                   boolValue);
    LLVMBuildStore(module->builder, *(blockRet->value), blockValue);
    LLVMBuildBr(module->builder, mergeBlock);

    LLVMPositionBuilderAtEnd(module->builder, mergeBlock);
    module->contexts[module->numContexts - 1]->currentBlock = mergeBlock;
    LLVMValueRef loadedBlockVal =
        LLVMBuildLoad2(module->builder, *blockLlvmType, blockValue, "");
    LLVMValueRef retVal = LLVMGetUndef(LLVMStructTypeInContext(
        module->context,
        (LLVMTypeRef[]){*blockLlvmType, LLVMInt1TypeInContext(module->context)},
        2, 0));
    retVal =
        LLVMBuildInsertValue(module->builder, retVal, loadedBlockVal, 0, "");
    LLVMValueRef loadedBoolVal = LLVMBuildLoad2(
        module->builder, LLVMInt1TypeInContext(module->context), boolValue, "");
    retVal =
        LLVMBuildInsertValue(module->builder, retVal, loadedBoolVal, 1, "");

    struct ValueData *ret = malloc(sizeof(struct ValueData));
    ret->value = malloc(sizeof(LLVMValueRef));
    *(ret->value) = retVal;
    ret->type = malloc(sizeof(struct TypeData));
    ret->type->type = NULLABLE;
    ret->type->length = -1;
    ret->type->otherType = blockRet->type;
    ret->isStatic = false;
    return ret;
}

struct ValueData *generateExpr(struct Token *token, struct ModuleData *module) {
    int exprLen = stbds_arrlen((struct Token **)token->data);
    if (exprLen == 0) {
        fprintf(stderr, "ERROR on line %llu column %llu: Invalid expression\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    char *funcName;
    bool varExists = false;
    struct VariableData var;
    if (((struct Token **)token->data)[0]->type != IDENT_TOKEN) {
        if (((struct Token **)token->data)[0]->type == REF_TOKEN) {
            if (((struct Token **)token->data)[0]->data == NULL) {
                return generateMultiply(token, module, exprLen);
            }
        }
        if (exprLen < 2) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Invalid expression\n",
                    token->lineNum, token->colNum);
            return NULL;
        }
        struct ValueData *val =
            generateToken(((struct Token **)token->data)[0], module, false,
                          true, false, false);
        if (val == NULL) {
            return NULL;
        }
        LLVMTypeRef *llvmType = generateType(val->type, module);
        if (llvmType == NULL) {
            return NULL;
        }
        var = (struct VariableData){val->type, llvmType, val->value};
        funcName = (char *)((struct Token **)token->data)[1]->data;
        varExists = true;
    } else {
        funcName = (char *)((struct Token **)token->data)[0]->data;
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
            ret->isStatic = false;
            return ret;
        }
        if (strcmp(funcName, "extern-fn") == 0) {
            LLVMValueRef *val = generateFuncDeclare(token, module, exprLen);
            if (val == NULL) {
                return NULL;
            }
            struct ValueData *ret = malloc(sizeof(struct ValueData));
            ret->value = val;
            ret->type = malloc(sizeof(struct TypeData));
            ret->type->type = NIL;
            ret->type->length = -1;
            ret->isStatic = false;
            return ret;
        }
        if (stbds_shgetp_null(module->functions, funcName) != NULL) {
            struct FuncData *funcData =
                stbds_shget(module->functions,
                            (char *)(((struct Token **)token->data)[0])->data);

            return generateFuncDataCall(funcData, token, 1, exprLen, module,
                                        NULL);
        }
        if (stbds_shgetp_null(module->macros, funcName) != NULL) {
            return generateMacro(token, module, exprLen);
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
        if (strcmp(funcName, "nth") == 0) {
            return generateNth(token, module, exprLen);
        }
        if (strcmp(funcName, "return") == 0) {
            return generateReturn(token, module, exprLen);
        }
        if (strcmp(funcName, "while") == 0) {
            return generateWhile(token, module, exprLen);
        }
        if (strcmp(funcName, "dotimes") == 0) {
            return generateDotimes(token, module, exprLen);
        }
        if (strcmp(funcName, "dolist") == 0) {
            return generateDolist(token, module, exprLen);
        }
        if (strcmp(funcName, "continue") == 0) {
            return generateContinue(token, module, exprLen);
        }
        if (strcmp(funcName, "break") == 0) {
            return generateBreak(token, module, exprLen);
        }
        if (strcmp(funcName, "set") == 0) {
            return generateSet(token, module, exprLen);
        }
        if (strcmp(funcName, "defun") == 0) {
            LLVMValueRef *val = generateFuncDefine(token, module, exprLen);
            if (val == NULL) {
                return NULL;
            }
            struct ValueData *ret = malloc(sizeof(struct ValueData));
            ret->value = val;
            ret->type = malloc(sizeof(struct TypeData));
            ret->type->type = NIL;
            ret->type->length = -1;
            ret->isStatic = false;
            return ret;
        }
        if (strcmp(funcName, "import") == 0) {
            if (!generateImport(token, module, exprLen)) {
                return NULL;
            }
            struct ValueData *ret = malloc(sizeof(struct ValueData));
            ret->value = NULL;
            ret->type = malloc(sizeof(struct TypeData));
            ret->type->type = NIL;
            ret->type->length = -1;
            ret->isStatic = false;
            return ret;
        }
        if (strcmp(funcName, "defmacro") == 0) {
            bool success = generateMacroDefine(token, module, exprLen);
            if (success == false) {
                return NULL;
            }
            struct ValueData *ret = malloc(sizeof(struct ValueData));
            ret->value = NULL;
            ret->type = malloc(sizeof(struct TypeData));
            ret->type->type = NIL;
            ret->type->length = -1;
            ret->isStatic = false;
            return ret;
        }
        if (strcmp(funcName, "defclass") == 0) {
            return generateClassDefine(token, module, exprLen);
        }
        if (strcmp(funcName, "classfun") == 0) {
            LLVMValueRef *val = generateClassFunc(token, module, exprLen);
            if (val == NULL) {
                return NULL;
            }
            struct ValueData *ret = malloc(sizeof(struct ValueData));
            ret->value = val;
            ret->type = malloc(sizeof(struct TypeData));
            ret->type->type = NIL;
            ret->type->length = -1;
            ret->isStatic = false;
            return ret;
        }
    }
    if (stbds_shgetp_null(module->classes, funcName) != NULL) {
        return generateClassInit(token, module, exprLen, funcName, "", false);
    }
    if ((stbds_shgetp_null(*(module->variables), funcName) != NULL ||
         stbds_shgetp_null(
             module->contexts[module->numContexts - 1]->localVariables,
             funcName) != NULL ||
         (module->contexts[module->numContexts - 1]->args != NULL &&
          stbds_shgetp_null(module->contexts[module->numContexts - 1]->args,
                            funcName) != NULL)) &&
        exprLen > 1) {
        struct ValueData *val =
            generateToken(((struct Token **)token->data)[0], module, false,
                          true, false, false);
        // No need to check if the generated token is a variable, the if
        // statement this is contained in already checks that
        if (val == NULL) {
            return NULL;
        }

        var.llvmVar = val->value;
        var.type = val->type;
        var.llvmType = generateType(val->type, module);
        if (var.llvmType == NULL) {
            return NULL;
        }

        funcName = (char *)((struct Token **)token->data)[1]->data;
        varExists = true;
    }
    if (varExists) {
        if ((var.type->type == VECTOR || var.type->type == ARRAY ||
             var.type->type == STRING) &&
            ((struct Token **)token->data)[1]->type == IDENT_TOKEN) {
            if (strcmp(funcName, "length") == 0) {
                if (exprLen > 2) {
                    fprintf(
                        stderr,
                        "ERROR on line %llu column %llu: Too many arguments "
                        "for %s type length access\n",
                        token->lineNum, token->colNum,
                        var.type->type == VECTOR  ? "vector"
                        : var.type->type == ARRAY ? "array"
                                                  : "string");
                    return NULL;
                }
                if (var.type->type == ARRAY) {
                    struct ValueData *ret = malloc(sizeof(struct ValueData));
                    ret->value = malloc(sizeof(LLVMValueRef));
                    ret->type = malloc(sizeof(struct TypeData));
                    *(ret->value) =
                        LLVMConstInt(LLVMInt32TypeInContext(module->context),
                                     var.type->length, 0);
                    ret->type->type = UNSIGNED32;
                    ret->type->length = -1;
                    return ret;
                }
                LLVMTypeRef *varType = generateType(var.type, module);
                if (varType == NULL) {
                    return NULL;
                }
                LLVMValueRef retVal = LLVMBuildStructGEP2(
                    module->builder, *varType, *(var.llvmVar), 2, "");
                free(varType);
                LLVMTypeRef type;
                if (var.type->type == STRING) {
                    type = LLVMInt8TypeInContext(module->context);
                } else {
                    LLVMTypeRef *genType =
                        generateType(var.type->otherType, module);
                    if (genType == NULL) {
                        return NULL;
                    }
                    type = *genType;
                    free(genType);
                }
                LLVMValueRef loadedRetVal =
                    LLVMBuildLoad2(module->builder, type, retVal, "");

                struct ValueData *ret = malloc(sizeof(struct ValueData));
                ret->value = malloc(sizeof(LLVMValueRef));
                ret->type = malloc(sizeof(LLVMTypeRef));
                ret->isStatic = false;
                *(ret->value) = loadedRetVal;
                if (var.type->type == STRING) {
                    ret->type->type = CHAR;
                    ret->type->length = -1;
                } else {
                    *(ret->type) = *(var.type->otherType);
                }
                return ret;
            }
        }
        if (var.type->type == POINTER &&
            var.type->otherType->type == NULLABLE) {
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
            if (strcmp(funcName, "isNil") == 0) {
                // LLVMValueRef loadedVar = LLVMBuildLoad2(
                //     module->builder, *(var.llvmType), *(var.llvmVar), "");
                LLVMTypeRef *llvmType =
                    generateType(var.type->otherType, module);
                if (llvmType == NULL) {
                    return NULL;
                }
                LLVMValueRef valPtr = LLVMBuildStructGEP2(
                    module->builder, *llvmType, *(var.llvmVar), 1, "");
                LLVMValueRef val = LLVMBuildLoad2(
                    module->builder, LLVMInt1TypeInContext(module->context),
                    valPtr, "");
                free(llvmType);

                // LLVMValueRef loadedVar = LLVMBuildLoad2(
                //     module->builder, *(var.llvmType), *(var.llvmVar),
                //     "");
                // LLVMValueRef val = LLVMBuildExtractValue(module->builder,
                //                                          *(var.llvmVar),
                //                                          1,
                //                                          "");

                struct ValueData *ret = malloc(sizeof(struct ValueData));
                ret->value = malloc(sizeof(LLVMValueRef));
                *(ret->value) = val;
                ret->type = malloc(sizeof(struct TypeData));
                ret->type->type = BOOL;
                ret->type->length = -1;
                ret->isStatic = false;
                return ret;
            }
            if (strcmp(funcName, "value") == 0) {
                LLVMTypeRef *llvmType =
                    generateType(var.type->otherType, module);
                if (llvmType == NULL) {
                    return NULL;
                }
                LLVMValueRef valPtr = LLVMBuildStructGEP2(
                    module->builder, *llvmType, *(var.llvmVar), 0, "");
                free(llvmType);

                LLVMValueRef val;
                if (var.type->otherType->otherType->type == STRING ||
                    var.type->otherType->otherType->type == CLASS ||
                    var.type->otherType->otherType->type == VECTOR ||
                    var.type->otherType->otherType->type == MAP ||
                    var.type->otherType->otherType->type == NULLABLE ||
                    var.type->otherType->otherType->type == ARRAY) {
                    val = valPtr;
                } else {
                    llvmType =
                        generateType(var.type->otherType->otherType, module);
                    if (llvmType == NULL) {
                        return NULL;
                    }
                    val =
                        LLVMBuildLoad2(module->builder, *llvmType, valPtr, "");
                    free(llvmType);
                }

                // LLVMValueRef loadedVar = LLVMBuildLoad2(
                //     module->builder, *(var.llvmType), *(var.llvmVar), "");
                // LLVMValueRef val =
                //     LLVMBuildExtractValue(module->builder, loadedVar, 0, "");

                struct ValueData *ret = malloc(sizeof(struct ValueData));
                ret->value = malloc(sizeof(LLVMValueRef));
                *(ret->value) = val;
                ret->type = var.type->otherType->otherType;
                ret->isStatic = false;
                return ret;
            }
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Unknown function for "
                    "optional type \"%s\"\n",
                    token->lineNum, token->colNum, funcName);
            return NULL;
        }
        if (var.type->type == NIL) {
            if (strcmp(funcName, "bool") == 0) {
                struct ValueData *ret = malloc(sizeof(struct ValueData));
                ret->value = malloc(sizeof(LLVMValueRef));
                *(ret->value) =
                    LLVMConstInt(LLVMInt1TypeInContext(module->context), 0, 0);
                ret->type = malloc(sizeof(struct TypeData));
                ret->type->type = BOOL;
                ret->type->length = -1;
                return ret;
            }
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Unknow function for nil "
                    "type \"%s\"\n",
                    token->lineNum, token->colNum, funcName);
            return NULL;
        }
        if (var.type->type == BOOL) {
            if (strcmp(funcName, "int") == 0) {
                if (exprLen > 2) {
                    fprintf(stderr,
                            "ERROR on line %llu column %llu: Too many "
                            "arguments for bool to int type conversion\n",
                            token->lineNum, token->colNum);
                    return NULL;
                }
                LLVMValueRef val =
                    LLVMBuildZExt(module->builder, *(var.llvmVar),
                                  LLVMInt32TypeInContext(module->context), "");
                struct ValueData *ret = malloc(sizeof(struct ValueData));
                ret->value = malloc(sizeof(LLVMValueRef));
                *(ret->value) = val;
                ret->type = malloc(sizeof(struct TypeData));
                ret->type->type = INT32;
                ret->type->length = -1;
                return ret;
            }
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Unknow function for bool "
                    "type \"%s\"\n",
                    token->lineNum, token->colNum, funcName);
            return NULL;
        }
        if (var.type->type == POINTER && var.type->otherType->type == STRING) {
            if (strcmp(funcName, "cstring") == 0) {
                if (exprLen > 2) {
                    fprintf(stderr,
                            "ERROR on line %llu column %llu: Too many "
                            "arguments for \"cstring\" string class function\n",
                            token->lineNum, token->colNum);
                    return NULL;
                }
                LLVMTypeRef *llvmType =
                    generateType(var.type->otherType, module);
                if (llvmType == NULL) {
                    return NULL;
                }
                LLVMValueRef ep = LLVMBuildStructGEP2(
                    module->builder, *llvmType, *(var.llvmVar), 0, "");
                free(llvmType);
                LLVMValueRef loadedVal = LLVMBuildLoad2(
                    module->builder,
                    LLVMPointerType(LLVMInt8TypeInContext(module->context), 0),
                    ep, "");
                struct ValueData *ret = malloc(sizeof(struct ValueData));
                ret->value = malloc(sizeof(LLVMValueRef));
                *(ret->value) = loadedVal;
                ret->type = malloc(sizeof(struct TypeData));
                ret->type->type = POINTER;
                ret->type->length = -1;
                ret->type->otherType = malloc(sizeof(struct TypeData));
                ret->type->otherType->type = CHAR;
                ret->type->otherType->length = -1;
                return ret;
            }
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Unknown class function or "
                    "variable %s for a string type\n",
                    token->lineNum, token->colNum, funcName);
            return NULL;
        }
        if (var.type->type == POINTER && var.type->otherType->type == CLASS) {
            struct ClassData *classData =
                stbds_shget(module->classes, var.type->otherType->name);
            if (stbds_shgetp_null(classData->functions, funcName) != NULL) {
                struct FuncData *funcData =
                    stbds_shget(classData->functions, funcName);
                return generateFuncDataCall(funcData, token, 2, exprLen, module,
                                            var.llvmVar);
            }
            if (stbds_shgetp_null(classData->variables, funcName) != NULL) {
                struct ClassVariableData varData =
                    stbds_shget(classData->variables, funcName);
                LLVMValueRef val = LLVMBuildStructGEP2(
                    module->builder, *(classData->structType), *(var.llvmVar),
                    varData.index, "");
                struct ValueData *ret = malloc(sizeof(struct ValueData));
                if (varData.type->type == STRING ||
                    varData.type->type == CLASS ||
                    varData.type->type == VECTOR || varData.type->type == MAP ||
                    varData.type->type == NULLABLE ||
                    varData.type->type == ARRAY) {
                    LLVMTypeRef *llvmType = generateType(varData.type, module);
                    if (llvmType == NULL) {
                        return NULL;
                    }
                    val =
                        LLVMBuildLoad2(module->builder,
                                       LLVMPointerType(*llvmType, 0), val, "");
                    free(llvmType);
                }
                ret->type = malloc(sizeof(struct TypeData));
                ret->type->type = POINTER;
                ret->type->length = -1;
                ret->type->otherType = varData.type;
                ret->value = malloc(sizeof(LLVMValueRef));
                *(ret->value) = val;
                ret->isStatic = false;
                return ret;
            }
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Unknown class "
                    "function or variable \"%s\"\n",
                    ((struct Token **)token->data)[1]->lineNum,
                    ((struct Token **)token->data)[1]->colNum, funcName);
            return NULL;
        }
        if (var.type->type == FLOAT) {
            if (strcmp(funcName, "double") == 0) {
                if (exprLen > 2) {
                    fprintf(
                        stderr,
                        "ERROR on line %llu column %llu: Too many arguments "
                        "for converting a float to a double\n",
                        token->lineNum, token->colNum);
                    return NULL;
                }
                struct ValueData *ret = malloc(sizeof(struct ValueData));
                // LLVMValueRef loadedVar = LLVMBuildLoad2(
                // module->builder, *(var.llvmType), *(var.llvmVar), "");
                ret->value = malloc(sizeof(LLVMValueRef));
                *(ret->value) = LLVMBuildFPExt(
                    module->builder, *(var.llvmVar),
                    LLVMDoubleTypeInContext(module->context), "");
                ret->type = malloc(sizeof(struct TypeData));
                ret->type->type = DOUBLE;
                ret->type->length = -1;
                return ret;
            }
        }
        fprintf(stderr,
                "ERROR on line %llu column %llu: Unknown class function or "
                "variable %s\n",
                token->lineNum, token->colNum, funcName);
        return NULL;
    }
    fprintf(stderr, "ERROR on line %llu column %llu: Unknown function \"%s\"\n",
            token->lineNum, token->colNum, funcName);
    return NULL;
}

struct ValueData *generateIdent(struct Token *token, struct ModuleData *module,
                                bool falseInsteadOfNil) {
    char *name = (char *)token->data;
    struct VariableData *var = NULL;
    if (stbds_shgetp_null(*(module->variables), name) != NULL) {
        var = &stbds_shget(*(module->variables), name);
    } else if (stbds_shgetp_null(
                   module->contexts[module->numContexts - 1]->localVariables,
                   name) != NULL) {
        var = &stbds_shget(
            module->contexts[module->numContexts - 1]->localVariables, name);
    } else if (module->contexts[module->numContexts - 1]->args != NULL &&
               stbds_shgetp_null(
                   module->contexts[module->numContexts - 1]->args, name) !=
                   NULL) {
        var =
            &stbds_shget(module->contexts[module->numContexts - 1]->args, name);
        struct ValueData *val = malloc(sizeof(struct ValueData));
        val->value = var->llvmVar;
        val->type = var->type;
        val->isStatic = false;
        return val;
    }
    if (var != NULL) {
        LLVMValueRef *loadedVar = malloc(sizeof(LLVMValueRef));
        struct ValueData *ret = malloc(sizeof(struct ValueData));
        ret->type = malloc(sizeof(struct TypeData));
        ret->type->type = POINTER;
        ret->type->length = -1;
        ret->type->otherType = var->type;
        ret->value = var->llvmVar;
        ret->isStatic = false;
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
        ret->isStatic = true;
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
            ret->isStatic = true;
            return ret;
        }
        struct ValueData *ret = malloc(sizeof(struct ValueData));
        ret->value = malloc(sizeof(LLVMValueRef));
        *(ret->value) = NULL;
        ret->type = malloc(sizeof(struct TypeData));
        ret->type->type = NIL;
        ret->type->length = -1;
        ret->isStatic = true;
        return ret;
    }
    fprintf(stderr,
            "ERROR on line %llu column %llu: Unknown identifier \"%s\"\n",
            token->lineNum, token->colNum, (char *)token->data);
    return NULL;
}

struct ValueData *generateDeRef(struct Token *token, struct ModuleData *module,
                                bool charPtrInsteadOfString) {
    struct Token *other = ((struct Token **)token->data)[0];
    if (other->type == IDENT_TOKEN &&
        stbds_shgetp_null(module->contexts[module->numContexts - 1]->macroArgs,
                          (char *)other->data) != NULL) {
        return generateToken(
            stbds_shget(module->contexts[module->numContexts - 1]->macroArgs,
                        (char *)other->data),
            module, false, true, false, false);
    }
    if (other->type == IDENT_TOKEN &&
        module->contexts[module->numContexts - 1]->macroRestArg != NULL &&
        strcmp(module->contexts[module->numContexts - 1]->macroRestArg->name,
               (char *)other->data) == 0) {
        size_t numRestArgs =
            module->contexts[module->numContexts - 1]->macroRestArg->numValues;
        for (size_t i = 0; i < numRestArgs; i++) {
            struct ValueData *val = generateToken(
                module->contexts[module->numContexts - 1]
                    ->macroRestArg->values[i],
                module, charPtrInsteadOfString, true, false, false);
            if (val == NULL) {
                return NULL;
            }
            if (i == numRestArgs - 1) {
                return val;
            }
        }
    }

    struct ValueData *val =
        generateToken(other, module, false, true, false, false);
    if (val == NULL) {
        return NULL;
    }
    if (val->type->type != POINTER) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Can only dereference a "
                "pointer\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    if (val->type->otherType == NULL) {
        fprintf(stderr, "COMPILER ERROR on line %llu column %llu\n",
                token->lineNum, token->colNum);
        return NULL;
    }
    LLVMTypeRef *llvmType = generateType(val->type->otherType, module);
    if (llvmType == NULL) {
        return NULL;
    }
    struct ValueData *newVal = malloc(sizeof(struct ValueData));
    newVal->value = malloc(sizeof(LLVMValueRef));
    *(newVal->value) =
        LLVMBuildLoad2(module->builder, *llvmType, *(val->value), "");
    newVal->type = val->type->otherType;
    newVal->isStatic = false;
    return newVal;
}

struct ValueData *generateRef(struct Token *token, struct ModuleData *module) {
    struct Token *other = ((struct Token **)token->data)[0];
    struct ValueData *val =
        generateToken(other, module, false, true, false, false);
    LLVMTypeRef *llvmType = generateType(val->type, module);
    if (llvmType == NULL) {
        return NULL;
    }
    struct ValueData *newVal = malloc(sizeof(struct ValueData));
    newVal->value = malloc(sizeof(LLVMValueRef));
    newVal->isStatic = false;
    LLVMPositionBuilderAtEnd(
        module->builder,
        module->contexts[module->numContexts - 1]->allocaBlock);
    *(newVal->value) =
        LLVMBuildAlloca(module->builder, LLVMPointerType(*llvmType, 0), "");
    LLVMPositionBuilderAtEnd(
        module->builder,
        module->contexts[module->numContexts - 1]->currentBlock);
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
                         (*(double *)token->data));
    return val;
}

LLVMValueRef *generateDouble(struct Token *token, struct ModuleData *module) {
    LLVMValueRef *val = malloc(sizeof(LLVMValueRef));
    *val = LLVMConstReal(LLVMDoubleTypeInContext(module->context),
                         (*(double *)token->data));
    return val;
}

LLVMValueRef *generateCharPtr(struct Token *token, struct ModuleData *module) {
    LLVMValueRef *val = malloc(sizeof(LLVMValueRef));
    // LLVMPositionBuilderAtEnd(
    //     module->builder,
    //     module->contexts[module->numContexts - 1]->allocaBlock);
    // *val = LLVMBuildAlloca(
    //     module->builder,
    //     LLVMPointerType(LLVMInt8TypeInContext(module->context), 0), "");
    // LLVMPositionBuilderAtEnd(
    //     module->builder,
    //     module->contexts[module->numContexts - 1]->currentBlock);
    // LLVMBuildStore(
    //     module->builder,
    //     LLVMConstString((char *)token->data, strlen((char *)token->data),
    //     0), *val);

    size_t len = strlen((char *)token->data);
    LLVMTypeRef llvmType =
        LLVMArrayType(LLVMInt8TypeInContext(module->context), len + 1);
    LLVMValueRef strValue = LLVMConstStringInContext(
        module->context, (char *)token->data, len, false);

    LLVMValueRef globalStr = LLVMAddGlobal(module->module, llvmType, "");
    LLVMSetInitializer(globalStr, strValue);
    LLVMSetLinkage(globalStr, LLVMPrivateLinkage);
    LLVMSetUnnamedAddr(globalStr, 1);
    LLVMSetGlobalConstant(globalStr, 1);
    LLVMSetAlignment(globalStr, 1);
    *val = globalStr;
    return val;
}

struct ValueData *generateToken(struct Token *token, struct ModuleData *module,
                                bool charPtrInsteadOfString,
                                bool falseInsteadOfNil,
                                bool vectorInsteadOfArray,
                                bool doubleInsteadOfFloat) {
    struct ValueData *ret = malloc(sizeof(struct ValueData));
    if (module->contexts[module->numContexts - 1]->unreachable == true) {
        fprintf(stderr,
                "WARNING on line %llu column %llu: Code is unreachable\n",
                token->lineNum, token->colNum);
        ret->type = malloc(sizeof(struct TypeData));
        ret->type->type = NIL;
        ret->type->length = -1;
        ret->value = NULL;
        return ret;
    }
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
        ret->isStatic = true;
        return ret;

    case FLOAT_TOKEN:
        if (doubleInsteadOfFloat) {
            val = generateDouble(token, module);
            ret->value = val;
            ret->type = malloc(sizeof(struct TypeData));
            ret->type->type = DOUBLE;
            ret->type->length = -1;
            ret->isStatic = true;
            return ret;
        }
        val = generateFloat(token, module);
        ret->value = val;
        ret->type = malloc(sizeof(struct TypeData));
        ret->type->type = FLOAT;
        ret->type->length = -1;
        ret->isStatic = true;
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
            ret->isStatic = true;
            return ret;
        }
        val = generateString(token, module, "", false);
        ret->value = val;
        ret->type = malloc(sizeof(struct TypeData));
        ret->type->type = STRING;
        ret->type->length = -1;
        ret->isStatic = true;
        return ret;

    case IDENT_TOKEN:
        free(ret);
        return generateIdent(token, module, falseInsteadOfNil);

    case EXPR_TOKEN:
        free(ret);
        return generateExpr(token, module);

    case DE_REF_TOKEN:
        free(ret);
        return generateDeRef(token, module, charPtrInsteadOfString);

    case REF_TOKEN:
        free(ret);
        return generateRef(token, module);

    case LIST_TOKEN:
        free(ret);
        if (vectorInsteadOfArray) {
            return generateVectorFromToken(token, module, "", false);
        }
        return generateArray(token, module, "", false);

    default:
        free(ret);
        fprintf(stderr,
                "Syntax error on line %llu column %llu: Invalid token.\n",
                token->lineNum, token->colNum);
        return NULL;
    }
}

bool cmptype(struct TypeData *cmpType, struct TypeData *expectedType,
             size_t lineNum, size_t colNum, bool printError) {
    if (expectedType->type == POINTER) {
        if (cmpType->type != POINTER) {
            if (printError) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Expected a "
                        "pointer type\n",
                        lineNum, colNum);
            }
            return false;
        }
        bool res = cmptype(cmpType->otherType, expectedType->otherType, lineNum,
                           colNum, false);
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
    if (expectedType->type == CLASS) {
        if (cmpType->type != CLASS) {
            if (printError) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Expected a class "
                        "\"%s\"\n",
                        lineNum, colNum, expectedType->name);
            }
            return false;
        }
        bool res = strcmp(expectedType->name, cmpType->name) == 0;
        if (!res && printError) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Expected a class \"%s\"\n",
                    lineNum, colNum, expectedType->name);
        }
        return res;
    }
    if (expectedType->type == ARRAY) {
        if (cmpType->type != ARRAY) {
            if (printError) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Expected an array\n",
                        lineNum, colNum);
            }
            return false;
        }
        bool res = cmptype(cmpType->otherType, expectedType->otherType, lineNum,
                           colNum, false);
        if (!res && printError) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Expected a different type "
                    "array\n",
                    lineNum, colNum);
        }
        if (expectedType->length != cmpType->length) {
            if (printError) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Expected a %d long "
                        "array\n",
                        lineNum, colNum, expectedType->length);
            }
            return false;
        }
        return res;
    }
    if (expectedType->type == VECTOR) {
        if (cmpType->type != VECTOR) {
            if (printError) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Expected a vector\n",
                        lineNum, colNum);
            }
            return false;
        }
        bool res = cmptype(cmpType->otherType, expectedType->otherType, lineNum,
                           colNum, false);
        if (!res && printError) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Expected a different type "
                    "vector\n",
                    lineNum, colNum);
        }
        return res;
    }
    if (expectedType->type == NULLABLE) {
        if (cmpType->type != NULLABLE) {
            if (printError) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Expected a nullable "
                        "type\n",
                        lineNum, colNum);
            }
            return false;
        }
        bool res = cmptype(cmpType->otherType, expectedType->otherType, lineNum,
                           colNum, false);
        if (!res && printError) {
            fprintf(stderr,
                    "ERROR on line %llu column %llu: Expected a different type "
                    "for optional type\n",
                    lineNum, colNum);
        }
        return res;
    }
    if (expectedType->type == MAP) {
        if (cmpType->type != MAP) {
            if (printError) {
                fprintf(stderr,
                        "ERROR on line %llu column %llu: Expected a map\n",
                        lineNum, colNum);
            }
            return false;
        }
        bool res = cmptype(cmpType->otherType, expectedType->otherType, lineNum,
                           colNum, false);
        bool res2 = cmptype(cmpType->otherType2, expectedType->otherType2,
                            lineNum, colNum, false);
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
    if (expectedType->type != cmpType->type && printError &&
        expectedType->type == INT32) {
        fprintf(stderr, "ERROR on line %llu column %llu: Expected an integer\n",
                lineNum, colNum);
    }
    if (expectedType->type != cmpType->type && printError &&
        expectedType->type == UNSIGNED32) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected an unsigned "
                "integer\n",
                lineNum, colNum);
    }
    if (expectedType->type != cmpType->type && printError &&
        expectedType->type == FLOAT) {
        fprintf(stderr, "ERROR on line %llu column %llu: Expected a float\n",
                lineNum, colNum);
    }
    if (expectedType->type != cmpType->type && printError &&
        expectedType->type == BOOL) {
        fprintf(stderr, "ERROR on line %llu column %llu: Expected a bool\n",
                lineNum, colNum);
    }
    if (expectedType->type != cmpType->type && printError &&
        expectedType->type == CHAR) {
        fprintf(stderr,
                "ERROR on line %llu column %llu: Expected a character\n",
                lineNum, colNum);
    }
    if (expectedType->type != cmpType->type && printError &&
        expectedType->type == STRING) {
        fprintf(stderr, "ERROR on line %llu column %llu: Expected a string\n",
                lineNum, colNum);
    }
    if (expectedType->type != cmpType->type && printError &&
        expectedType->type == NIL) {
        fprintf(stderr, "ERROR on line %llu column %llu: Expected a nil type\n",
                lineNum, colNum);
    }
    return expectedType->type == cmpType->type;
}

LLVMValueRef *generateTokenOfType(struct Token *token, struct TypeData type,
                                  struct ModuleData *module) {
    struct ValueData *val = NULL;
    if (type.type == NULLABLE) {
        val = generateToken(token, module, type.otherType->type != STRING,
                            type.otherType->type == BOOL,
                            type.otherType->type == VECTOR,
                            type.otherType->type == DOUBLE);
    } else {
        val =
            generateToken(token, module, type.type != STRING, type.type == BOOL,
                          type.type == VECTOR, type.type == DOUBLE);
    }
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
    if (type.type == NULLABLE) {
        if (!cmptype(val->type, type.otherType, token->lineNum, token->colNum,
                     true)) {
            return NULL;
        }

        LLVMTypeRef *llvmType = generateType(type.otherType, module);
        if (llvmType == NULL) {
            return NULL;
        }
        if (type.type == STRING || type.type == CLASS || type.type == VECTOR ||
            type.type == MAP || type.type == NULLABLE || type.type == ARRAY) {
            *(val->value) =
                LLVMBuildLoad2(module->builder, *llvmType, *(val->value), "");
        }

        llvmType = generateType(&type, module);
        if (llvmType == NULL) {
            return NULL;
        }
        LLVMValueRef structVal = LLVMGetUndef(*llvmType);
        structVal = LLVMBuildInsertValue(module->builder, structVal,
                                         *(val->value), 0, "");
        structVal = LLVMBuildInsertValue(
            module->builder, structVal,
            LLVMConstInt(LLVMInt1TypeInContext(module->context), 0, 0), 1, "");
        free(llvmType);
        *(val->value) = structVal;
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
    abortFunc = LLVMAddFunction(module->module, "abort",
                                LLVMFunctionType(LLVMVoidType(), NULL, 0, 0));
}

int generate(struct Token *body, const char *filename,
             const char *inputFilename, char *mainName) {
    LLVMContextRef context = LLVMContextCreate();
    LLVMModuleRef module =
        LLVMModuleCreateWithNameInContext(inputFilename, context);
    LLVMSetSourceFileName(module, inputFilename, strlen(inputFilename));
    LLVMBuilderRef builder = LLVMCreateBuilderInContext(context);

    LLVMTypeRef mainType = LLVMFunctionType(LLVMInt32Type(), NULL, 0, 0);
    LLVMValueRef mainFunc = LLVMAddFunction(module, mainName, mainType);
    LLVMBasicBlockRef allocaBlock = LLVMAppendBasicBlock(mainFunc, "alloca");
    LLVMBasicBlockRef entry = LLVMAppendBasicBlock(mainFunc, "entry");

    struct ContextData *contextData = malloc(sizeof(struct ContextData));
    *contextData = (struct ContextData){
        mainFunc, allocaBlock, entry, NULL, NULL, NULL,  NULL,
        NULL,     NULL,        NULL,  NULL, NULL, false, false};
    struct ModuleData moduleData = {builder, context, module, NULL, NULL,
                                    NULL,    NULL,    NULL,   0,    1};
    stbds_arrpush(moduleData.contexts, contextData);
    stbds_shput(contextData->localVariables, "dummy",
                ((struct VariableData){NULL, NULL}));
    stbds_shdel(contextData->localVariables, "dummy");
    moduleData.variables = &(contextData->localVariables);

    stdInit(&moduleData);

    contextData->outOfBoundsErrorBlock =
        generateOutOfBoundsErrorBlock(mainFunc, entry, builder);

    LLVMPositionBuilderAtEnd(builder, entry);
    size_t numTokens = stbds_arrlen((struct Token **)body->data);
    for (size_t i = 0; i < numTokens; i++) {
        struct ValueData *val =
            generateToken(((struct Token **)body->data)[i], &moduleData, false,
                          false, false, false);
        if (val == NULL) {
            return 1;
        }
        free(val);
    }

    while (stbds_arrlen(toGenerate)) {
        struct ToGenerateData data = stbds_arrpop(toGenerate);
        size_t pathLen = strlen(data.filePath);
        char *filenameOut = malloc(pathLen * sizeof(char));
        memcpy(filenameOut, data.filePath, pathLen - 1);
        filenameOut[pathLen - 3] = 'l';
        filenameOut[pathLen - 2] = 'l';
        filenameOut[pathLen - 1] = '\0';
        char *mainName2 = malloc(32 * sizeof(char));
        sprintf_s(mainName2, 32, "main%d", data.mainIdx);
        if (stbds_shgetp_null(mainNames, data.filePath) == NULL) {
            stbds_shput(mainNames, data.filePath, mainName2);
            if (generate(data.body, filenameOut, data.filePath, mainName2)) {
                return 1;
            }
        }
        free(mainName2);
        free(filenameOut);
    }

    generateFreeMallocedVars(&moduleData);
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
