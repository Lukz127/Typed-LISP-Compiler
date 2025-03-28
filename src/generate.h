#pragma once
#include <limits.h>
#include <llvm-c/Core.h>
#include <tokenize.h>

char *toAbsolutePath(const char *relative_path, char *absolute_path,
                     size_t size) {
#ifdef _WIN32
    return _fullpath(absolute_path, relative_path, size);
#else
    return realpath(relative_path, absolute_path);
#endif
}

typedef enum StaticType {
    INT32,
    UNSIGNED32,
    UNSIGNED64,
    FLOAT,
    DOUBLE,
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
    size_t index;
};

struct ClassVariableList {
    char *key;
    struct ClassVariableData value;
};

struct ClassFunctionList {
    char *key;
    struct FuncData *value;
};

struct ClassData {
    struct TypeData *classType;
    LLVMTypeRef *structType;
    struct ClassVariableList *variables;
    struct ClassFunctionList *functions;
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
    LLVMBasicBlockRef *continueBlock; // Null if continue is not allowed
    LLVMBasicBlockRef *breakBlock;    // Null if break is not allowed
    struct VariableList *localVariables;
    struct VariableList *args;
    struct MacroArg *macroArgs;
    struct MacroRestArg *macroRestArg;
    struct TypeData *returnType; // NULL if return is not allowed
    LLVMValueRef *loopIndexVar;  // NULL if not in an indexed loop
    LLVMValueRef **mallocedVarsToFree;
    bool isVarArg;
    bool unreachable;
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
    char **outlinedFiles;
    char **toOutline;
};

struct ToGenerateData {
    struct Token *body;
    char *filePath;
    size_t mainIdx;
};

int generate(struct Token *body, const char *filename,
             const char *inputFilename, char *mainName);
struct TypeData *getType(struct Token *token, struct ModuleData *module);
LLVMTypeRef *generateType(struct TypeData *type, struct ModuleData *module);
bool outlineFile(struct Token *body, struct ModuleData *module);

size_t numImports = 0;
struct ToGenerateData *toGenerate = NULL;
struct {
    char *key;
    char *value;
} *mainNames = NULL;
