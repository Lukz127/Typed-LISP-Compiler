#pragma once
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

struct ClassVariableData {
    struct TypeData *type;
    LLVMTypeRef *llvmType;
    LLVMValueRef *defaultValue;
    size_t index;
};

struct ClassVariableList {
    char *key;
    struct ClassVariableData value;
};

struct ClassData {
    struct TypeData *classType;
    LLVMTypeRef *structType;
    struct ClassVariableList *variables;
    struct FuncData **initFunctions;
    size_t numInitFunctions;
    size_t numVars;
    bool allVarsHaveDefaults;
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
    bool isStatic;
};

struct ValueDataExtra {
    struct TypeData *type;
    LLVMValueRef *value;
    bool isStatic;
    size_t lineNum;
    size_t colNum;
};

struct NamedValueData {
    char *key;
    struct ValueDataExtra *value;
};

struct VariableList {
    char *key;
    struct VariableData value;
};

struct MacroArg {
    char *key;
    struct Token *value;
};

struct MacroRestArg {
    char *name;
    struct Token **values;
    size_t numValues;
};

struct MacroData {
    char **args;
    char *restArg;       // NULL for no rest arg
    struct Token **body; // the body to generate
    size_t bodyLen;
    size_t numArgs;
};

struct ContextData {
    LLVMValueRef func;
    LLVMBasicBlockRef allocaBlock;
    LLVMBasicBlockRef currentBlock;
    LLVMBasicBlockRef outOfBoundsErrorBlock;
    struct VariableList *localVariables;
    struct VariableList *args;
    struct MacroArg *macroArgs;
    struct MacroRestArg *macroRestArg;
    struct TypeData *returnType; // NULL if return is not allowed
    bool isVarArg;
};

struct ModuleData {
    LLVMBuilderRef builder;
    LLVMContextRef context;
    LLVMModuleRef module;
    struct ContextData **contexts;
    struct VariableList **variables;
    struct {
        char *key;
        struct ClassData *value;
    } *classes;
    struct {
        char *key;
        struct FuncData *value;
    } *functions;
    struct {
        char *key;
        struct MacroData *value;
    } *macros;
    size_t nextTempNum;
    size_t numContexts;
};

int generate(struct Token *body, const char *filename,
             const char *inputFilename);
