#pragma once
#include <llvm-c/BitWriter.h>
#include <llvm-c/Core.h>
#include <tokenize.h>

typedef enum StaticType {
    INT32,
    UNSIGNED32,
    UNSIGNED64,
    FLOAT32,
    BOOL,
    CHAR,
    STRING,
    CLASS,
    POINTER,
    ARRAY,  // specific number of elements
    VECTOR, // dynamic
    MAP,
    NULLABLE,
    NIL,
} StaticType;

struct TypeData {
    StaticType type;
    struct TypeData *otherType;  // for pointers, array, vector and map key
    struct TypeData *otherType2; // for map value
    int length;                  // for array
    char *name;                  // class or enum name
};

struct VariableData {
    struct TypeData *type;
    LLVMTypeRef *llvmType;
    LLVMValueRef *llvmVar;
};

struct ClassData {
    LLVMTypeRef *type;
    struct {
        char *key;
        struct VariableData value;
    } *variables;
    size_t numVars;
};

struct FunctionArgType {
    struct TypeData *type;
    LLVMTypeRef *llvmType;
    char *name;
    bool optional;
};

struct FuncData {
    LLVMValueRef *function;
    LLVMTypeRef *funcType;
    struct TypeData *retType;
    LLVMTypeRef *llvmRetType;
    struct FunctionArgType *args;
    struct FunctionArgType
        *restArg;   // NULL for no rest argument, value of "optional" is ignored
    size_t numArgs; // including optional
    bool isVarArg;
};

struct ValueData {
    struct TypeData *type;
    LLVMValueRef *value;
};

struct ModuleData {
    LLVMBuilderRef builder;
    LLVMContextRef context;
    LLVMModuleRef module;
    LLVMValueRef mainFunc;
    LLVMBasicBlockRef allocaBlock;
    LLVMBasicBlockRef currentBlock;
    struct {
        char *key;
        struct VariableData value;
    } *variables;
    struct {
        char *key;
        struct ClassData value;
    } *classes;
    struct {
        char *key;
        struct FuncData *value;
    } *functions;
    size_t nextTempNum;
};

int generate(struct Token *body, const char *filename,
             const char *inputFilename);
