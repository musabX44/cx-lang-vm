#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bytecode.h"

/* ============================================================================
 * BYTECODE MODULE
 * ============================================================================ */

ByteCodeModule *bytecode_module_create(void) {
    BytecodeModule *module = malloc(sizeof(BytecodeModule));
    module->instructions = malloc(sizeof(Instruction) * 4096);
    module->instr_count = 0;
    module->instr_capacity = 4096;
    
    module->string_pool = malloc(sizeof(char*) * 1024);
    module->string_count = 0;
    module->string_capacity = 1024;
    
    module->function_names = malloc(sizeof(char*) * 256);
    module->function_offsets = malloc(sizeof(int64_t) * 256);
    module->function_count = 0;
    module->function_capacity = 256;
    
    return module;
}

void bytecode_module_free(BytecodeModule *module) {
    free(module->instructions);
    
    for (size_t i = 0; i < module->string_count; i++) {
        free(module->string_pool[i]);
    }
    free(module->string_pool);
    
    for (size_t i = 0; i < module->function_count; i++) {
        free(module->function_names[i]);
    }
    free(module->function_names);
    free(module->function_offsets);
    
    free(module);
}

void bytecode_emit(BytecodeModule *module, Opcode op, int64_t arg1, int64_t arg2) {
    if (module->instr_count >= module->instr_capacity) {
        module->instr_capacity *= 2;
        module->instructions = realloc(module->instructions, sizeof(Instruction) * module->instr_capacity);
    }
    
    module->instructions[module->instr_count].op = op;
    module->instructions[module->instr_count].arg1 = arg1;
    module->instructions[module->instr_count].arg2 = arg2;
    module->instructions[module->instr_count].str_arg = NULL;
    module->instr_count++;
}

void bytecode_emit_str(BytecodeModule *module, Opcode op, const char *str) {
    if (module->instr_count >= module->instr_capacity) {
        module->instr_capacity *= 2;
        module->instructions = realloc(module->instructions, sizeof(Instruction) * module->instr_capacity);
    }
    
    module->instructions[module->instr_count].op = op;
    module->instructions[module->instr_count].arg1 = 0;
    module->instructions[module->instr_count].arg2 = 0;
    module->instructions[module->instr_count].str_arg = strdup(str);
    module->instr_count++;
}

int bytecode_add_string(BytecodeModule *module, const char *str) {
    if (module->string_count >= module->string_capacity) {
        module->string_capacity *= 2;
        module->string_pool = realloc(module->string_pool, sizeof(char*) * module->string_capacity);
    }
    
    int index = module->string_count;
    module->string_pool[module->string_count++] = strdup(str);
    return index;
}

int bytecode_add_function(BytecodeModule *module, const char *name, int64_t offset) {
    if (module->function_count >= module->function_capacity) {
        module->function_capacity *= 2;
        module->function_names = realloc(module->function_names, sizeof(char*) * module->function_capacity);
        module->function_offsets = realloc(module->function_offsets, sizeof(int64_t) * module->function_capacity);
    }
    
    int index = module->function_count;
    module->function_names[module->function_count] = strdup(name);
    module->function_offsets[module->function_count] = offset;
    module->function_count++;
    return index;
}
