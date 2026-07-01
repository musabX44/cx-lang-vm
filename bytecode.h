#ifndef BYTECODE_H
#define BYTECODE_H

#include <stdint.h>
#include <stddef.h>

/* Bytecode Opcodes */
typedef enum {
    /* Literals & Variables */
    OP_LOAD_INT,      /* Load integer constant */
    OP_LOAD_FLOAT,    /* Load float constant */
    OP_LOAD_STR,      /* Load string constant */
    OP_LOAD_VAR,      /* Load variable */
    OP_STORE_VAR,     /* Store to variable */
    OP_LOAD_GLOBAL,   /* Load global variable */
    OP_STORE_GLOBAL,  /* Store global variable */
    
    /* Arithmetic */
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    
    /* Comparison */
    OP_EQ,
    OP_NEQ,
    OP_LT,
    OP_GT,
    OP_LTE,
    OP_GTE,
    
    /* Logical */
    OP_AND,
    OP_OR,
    OP_NOT,
    
    /* Control Flow */
    OP_JUMP,
    OP_JUMP_IF_FALSE,
    OP_JUMP_IF_TRUE,
    
    /* Functions */
    OP_CALL,
    OP_RET,
    OP_CALL_NATIVE,   /* Call built-in library function */
    
    /* Stack */
    OP_POP,
    OP_DUP,
    
    /* I/O & Debug */
    OP_PRINT,
    
    /* Other */
    OP_HALT,
    OP_NOP,
} Opcode;

/* Bytecode Instruction */
typedef struct {
    Opcode op;
    int64_t arg1;  /* First argument (operand, offset, etc) */
    int64_t arg2;  /* Second argument */
    char *str_arg; /* String argument */
} Instruction;

/* Bytecode Module */
typedef struct {
    Instruction *instructions;
    size_t instr_count;
    size_t instr_capacity;
    
    char **string_pool;  /* String literals */
    size_t string_count;
    size_t string_capacity;
    
    char **function_names;
    int64_t *function_offsets;  /* Bytecode offsets */
    size_t function_count;
    size_t function_capacity;
} BytecodeModule;

/* VM Value */
typedef enum {
    VAL_INT,
    VAL_FLOAT,
    VAL_STR,
    VAL_BOOL,
    VAL_PTR,
    VAL_NULL,
} ValueType;

typedef struct {
    ValueType type;
    union {
        int64_t i_val;
        double f_val;
        char *s_val;
        int b_val;
        void *ptr_val;
    } data;
} Value;

/* Virtual Machine */
typedef struct {
    Value *stack;
    size_t stack_size;
    size_t stack_capacity;
    
    int64_t pc;  /* Program counter */
    
    BytecodeModule *module;
    
    /* Variable storage */
    Value *locals;
    size_t local_count;
    size_t local_capacity;
    
    Value *globals;
    size_t global_count;
    size_t global_capacity;
} VM;

/* Function prototypes */
BytecodeModule *bytecode_module_create(void);
void bytecode_module_free(BytecodeModule *module);
void bytecode_emit(BytecodeModule *module, Opcode op, int64_t arg1, int64_t arg2);
void bytecode_emit_str(BytecodeModule *module, Opcode op, const char *str);
int bytecode_add_string(BytecodeModule *module, const char *str);
int bytecode_add_function(BytecodeModule *module, const char *name, int64_t offset);

/* Bytecode generation from AST */
typedef struct ASTNode ASTNode;  /* Forward declaration */
void generate_bytecode(BytecodeModule *module, ASTNode *ast);

VM *vm_create(BytecodeModule *module);
void vm_free(VM *vm);
int vm_execute(VM *vm);
Value vm_pop(VM *vm);
void vm_push(VM *vm, Value val);
Value vm_peek(VM *vm);

#endif /* BYTECODE_H */
