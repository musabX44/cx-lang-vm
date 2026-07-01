#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "bytecode.h"

/* ============================================================================
 * VIRTUAL MACHINE IMPLEMENTATION
 * ============================================================================ */

VM *vm_create(BytecodeModule *module) {
    VM *vm = malloc(sizeof(VM));
    vm->module = module;
    vm->pc = 0;
    
    vm->stack = malloc(sizeof(Value) * 4096);
    vm->stack_size = 0;
    vm->stack_capacity = 4096;
    
    vm->locals = malloc(sizeof(Value) * 1024);
    vm->local_count = 0;
    vm->local_capacity = 1024;
    
    vm->globals = malloc(sizeof(Value) * 256);
    vm->global_count = 0;
    vm->global_capacity = 256;
    
    return vm;
}

void vm_free(VM *vm) {
    free(vm->stack);
    free(vm->locals);
    free(vm->globals);
    free(vm);
}

void vm_push(VM *vm, Value val) {
    if (vm->stack_size >= vm->stack_capacity) {
        vm->stack_capacity *= 2;
        vm->stack = realloc(vm->stack, sizeof(Value) * vm->stack_capacity);
    }
    vm->stack[vm->stack_size++] = val;
}

Value vm_pop(VM *vm) {
    if (vm->stack_size == 0) {
        fprintf(stderr, "Stack underflow!\n");
        Value null_val = {.type = VAL_NULL};
        return null_val;
    }
    return vm->stack[--vm->stack_size];
}

Value vm_peek(VM *vm) {
    if (vm->stack_size == 0) {
        fprintf(stderr, "Stack empty!\n");
        Value null_val = {.type = VAL_NULL};
        return null_val;
    }
    return vm->stack[vm->stack_size - 1];
}

/* Built-in library functions */
int native_io_print(VM *vm, int arg_count) {
    if (arg_count > 0) {
        Value val = vm_pop(vm);
        
        switch (val.type) {
            case VAL_INT:
                printf("%ld", val.data.i_val);
                break;
            case VAL_FLOAT:
                printf("%f", val.data.f_val);
                break;
            case VAL_STR:
                printf("%s", val.data.s_val);
                break;
            case VAL_BOOL:
                printf("%s", val.data.b_val ? "true" : "false");
                break;
            default:
                printf("<unknown>");
                break;
        }
    }
    
    Value ret = {.type = VAL_INT, .data.i_val = 0};
    vm_push(vm, ret);
    return 0;
}

int native_math_sqrt(VM *vm, int arg_count) {
    if (arg_count > 0) {
        Value val = vm_pop(vm);
        double num = 0;
        
        if (val.type == VAL_INT) {
            num = (double)val.data.i_val;
        } else if (val.type == VAL_FLOAT) {
            num = val.data.f_val;
        }
        
        Value ret = {.type = VAL_FLOAT, .data.f_val = sqrt(num)};
        vm_push(vm, ret);
    }
    return 0;
}

int native_math_pow(VM *vm, int arg_count) {
    if (arg_count >= 2) {
        Value exp = vm_pop(vm);
        Value base = vm_pop(vm);
        
        double b = base.type == VAL_INT ? (double)base.data.i_val : base.data.f_val;
        double e = exp.type == VAL_INT ? (double)exp.data.i_val : exp.data.f_val;
        
        Value ret = {.type = VAL_FLOAT, .data.f_val = pow(b, e)};
        vm_push(vm, ret);
    }
    return 0;
}

int native_math_sin(VM *vm, int arg_count) {
    if (arg_count > 0) {
        Value val = vm_pop(vm);
        double num = val.type == VAL_INT ? (double)val.data.i_val : val.data.f_val;
        
        Value ret = {.type = VAL_FLOAT, .data.f_val = sin(num)};
        vm_push(vm, ret);
    }
    return 0;
}

int native_math_cos(VM *vm, int arg_count) {
    if (arg_count > 0) {
        Value val = vm_pop(vm);
        double num = val.type == VAL_INT ? (double)val.data.i_val : val.data.f_val;
        
        Value ret = {.type = VAL_FLOAT, .data.f_val = cos(num)};
        vm_push(vm, ret);
    }
    return 0;
}

int vm_call_native(VM *vm, const char *lib, const char *func, int arg_count) {
    if (strcmp(lib, "io") == 0) {
        if (strcmp(func, "print") == 0) {
            return native_io_print(vm, arg_count);
        }
    } else if (strcmp(lib, "math") == 0) {
        if (strcmp(func, "sqrt") == 0) {
            return native_math_sqrt(vm, arg_count);
        } else if (strcmp(func, "pow") == 0) {
            return native_math_pow(vm, arg_count);
        } else if (strcmp(func, "sin") == 0) {
            return native_math_sin(vm, arg_count);
        } else if (strcmp(func, "cos") == 0) {
            return native_math_cos(vm, arg_count);
        }
    }
    
    fprintf(stderr, "Unknown native function: %s.%s\n", lib, func);
    return -1;
}

int vm_execute(VM *vm) {
    while (vm->pc < (int64_t)vm->module->instr_count) {
        Instruction instr = vm->module->instructions[vm->pc];
        
        switch (instr.op) {
            case OP_LOAD_INT: {
                Value val = {.type = VAL_INT, .data.i_val = instr.arg1};
                vm_push(vm, val);
                break;
            }
            
            case OP_LOAD_FLOAT: {
                Value val = {.type = VAL_FLOAT};
                val.data.f_val = *(double*)&instr.arg1;
                vm_push(vm, val);
                break;
            }
            
            case OP_LOAD_STR: {
                Value val = {.type = VAL_STR};
                val.data.s_val = vm->module->string_pool[instr.arg1];
                vm_push(vm, val);
                break;
            }
            
            case OP_ADD: {
                Value b = vm_pop(vm);
                Value a = vm_pop(vm);
                Value result;
                
                if (a.type == VAL_INT && b.type == VAL_INT) {
                    result.type = VAL_INT;
                    result.data.i_val = a.data.i_val + b.data.i_val;
                } else {
                    result.type = VAL_FLOAT;
                    double av = a.type == VAL_INT ? (double)a.data.i_val : a.data.f_val;
                    double bv = b.type == VAL_INT ? (double)b.data.i_val : b.data.f_val;
                    result.data.f_val = av + bv;
                }
                vm_push(vm, result);
                break;
            }
            
            case OP_SUB: {
                Value b = vm_pop(vm);
                Value a = vm_pop(vm);
                Value result;
                
                if (a.type == VAL_INT && b.type == VAL_INT) {
                    result.type = VAL_INT;
                    result.data.i_val = a.data.i_val - b.data.i_val;
                } else {
                    result.type = VAL_FLOAT;
                    double av = a.type == VAL_INT ? (double)a.data.i_val : a.data.f_val;
                    double bv = b.type == VAL_INT ? (double)b.data.i_val : b.data.f_val;
                    result.data.f_val = av - bv;
                }
                vm_push(vm, result);
                break;
            }
            
            case OP_MUL: {
                Value b = vm_pop(vm);
                Value a = vm_pop(vm);
                Value result;
                
                if (a.type == VAL_INT && b.type == VAL_INT) {
                    result.type = VAL_INT;
                    result.data.i_val = a.data.i_val * b.data.i_val;
                } else {
                    result.type = VAL_FLOAT;
                    double av = a.type == VAL_INT ? (double)a.data.i_val : a.data.f_val;
                    double bv = b.type == VAL_INT ? (double)b.data.i_val : b.data.f_val;
                    result.data.f_val = av * bv;
                }
                vm_push(vm, result);
                break;
            }
            
            case OP_DIV: {
                Value b = vm_pop(vm);
                Value a = vm_pop(vm);
                Value result;
                
                if (a.type == VAL_INT && b.type == VAL_INT) {
                    result.type = VAL_INT;
                    result.data.i_val = b.data.i_val != 0 ? a.data.i_val / b.data.i_val : 0;
                } else {
                    result.type = VAL_FLOAT;
                    double av = a.type == VAL_INT ? (double)a.data.i_val : a.data.f_val;
                    double bv = b.type == VAL_INT ? (double)b.data.i_val : b.data.f_val;
                    result.data.f_val = bv != 0 ? av / bv : 0;
                }
                vm_push(vm, result);
                break;
            }
            
            case OP_MOD: {
                Value b = vm_pop(vm);
                Value a = vm_pop(vm);
                Value result = {.type = VAL_INT};
                
                if (b.data.i_val != 0) {
                    result.data.i_val = a.data.i_val % b.data.i_val;
                }
                vm_push(vm, result);
                break;
            }
            
            case OP_EQ: {
                Value b = vm_pop(vm);
                Value a = vm_pop(vm);
                Value result = {.type = VAL_BOOL};
                
                if (a.type == VAL_INT && b.type == VAL_INT) {
                    result.data.b_val = (a.data.i_val == b.data.i_val);
                } else if (a.type == VAL_FLOAT || b.type == VAL_FLOAT) {
                    double av = a.type == VAL_INT ? (double)a.data.i_val : a.data.f_val;
                    double bv = b.type == VAL_INT ? (double)b.data.i_val : b.data.f_val;
                    result.data.b_val = (av == bv);
                }
                vm_push(vm, result);
                break;
            }
            
            case OP_NEQ: {
                Value b = vm_pop(vm);
                Value a = vm_pop(vm);
                Value result = {.type = VAL_BOOL};
                
                if (a.type == VAL_INT && b.type == VAL_INT) {
                    result.data.b_val = (a.data.i_val != b.data.i_val);
                } else if (a.type == VAL_FLOAT || b.type == VAL_FLOAT) {
                    double av = a.type == VAL_INT ? (double)a.data.i_val : a.data.f_val;
                    double bv = b.type == VAL_INT ? (double)b.data.i_val : b.data.f_val;
                    result.data.b_val = (av != bv);
                }
                vm_push(vm, result);
                break;
            }
            
            case OP_LT: {
                Value b = vm_pop(vm);
                Value a = vm_pop(vm);
                Value result = {.type = VAL_BOOL};
                
                if (a.type == VAL_INT && b.type == VAL_INT) {
                    result.data.b_val = (a.data.i_val < b.data.i_val);
                } else if (a.type == VAL_FLOAT || b.type == VAL_FLOAT) {
                    double av = a.type == VAL_INT ? (double)a.data.i_val : a.data.f_val;
                    double bv = b.type == VAL_INT ? (double)b.data.i_val : b.data.f_val;
                    result.data.b_val = (av < bv);
                }
                vm_push(vm, result);
                break;
            }
            
            case OP_GT: {
                Value b = vm_pop(vm);
                Value a = vm_pop(vm);
                Value result = {.type = VAL_BOOL};
                
                if (a.type == VAL_INT && b.type == VAL_INT) {
                    result.data.b_val = (a.data.i_val > b.data.i_val);
                } else if (a.type == VAL_FLOAT || b.type == VAL_FLOAT) {
                    double av = a.type == VAL_INT ? (double)a.data.i_val : a.data.f_val;
                    double bv = b.type == VAL_INT ? (double)b.data.i_val : b.data.f_val;
                    result.data.b_val = (av > bv);
                }
                vm_push(vm, result);
                break;
            }
            
            case OP_LTE: {
                Value b = vm_pop(vm);
                Value a = vm_pop(vm);
                Value result = {.type = VAL_BOOL};
                
                if (a.type == VAL_INT && b.type == VAL_INT) {
                    result.data.b_val = (a.data.i_val <= b.data.i_val);
                } else if (a.type == VAL_FLOAT || b.type == VAL_FLOAT) {
                    double av = a.type == VAL_INT ? (double)a.data.i_val : a.data.f_val;
                    double bv = b.type == VAL_INT ? (double)b.data.i_val : b.data.f_val;
                    result.data.b_val = (av <= bv);
                }
                vm_push(vm, result);
                break;
            }
            
            case OP_GTE: {
                Value b = vm_pop(vm);
                Value a = vm_pop(vm);
                Value result = {.type = VAL_BOOL};
                
                if (a.type == VAL_INT && b.type == VAL_INT) {
                    result.data.b_val = (a.data.i_val >= b.data.i_val);
                } else if (a.type == VAL_FLOAT || b.type == VAL_FLOAT) {
                    double av = a.type == VAL_INT ? (double)a.data.i_val : a.data.f_val;
                    double bv = b.type == VAL_INT ? (double)b.data.i_val : b.data.f_val;
                    result.data.b_val = (av >= bv);
                }
                vm_push(vm, result);
                break;
            }
            
            case OP_AND: {
                Value b = vm_pop(vm);
                Value a = vm_pop(vm);
                Value result = {.type = VAL_BOOL};
                result.data.b_val = (a.data.b_val && b.data.b_val);
                vm_push(vm, result);
                break;
            }
            
            case OP_OR: {
                Value b = vm_pop(vm);
                Value a = vm_pop(vm);
                Value result = {.type = VAL_BOOL};
                result.data.b_val = (a.data.b_val || b.data.b_val);
                vm_push(vm, result);
                break;
            }
            
            case OP_NOT: {
                Value a = vm_pop(vm);
                Value result = {.type = VAL_BOOL};
                result.data.b_val = !a.data.b_val;
                vm_push(vm, result);
                break;
            }
            
            case OP_JUMP:
                vm->pc = instr.arg1;
                continue;
            
            case OP_JUMP_IF_FALSE: {
                Value cond = vm_pop(vm);
                if (!cond.data.b_val) {
                    vm->pc = instr.arg1;
                    continue;
                }
                break;
            }
            
            case OP_JUMP_IF_TRUE: {
                Value cond = vm_pop(vm);
                if (cond.data.b_val) {
                    vm->pc = instr.arg1;
                    continue;
                }
                break;
            }
            
            case OP_CALL_NATIVE: {
                int arg_count = instr.arg1;
                char *lib = strtok(instr.str_arg, ".");
                char *func = strtok(NULL, ".");
                vm_call_native(vm, lib, func, arg_count);
                break;
            }
            
            case OP_PRINT: {
                Value val = vm_pop(vm);
                switch (val.type) {
                    case VAL_INT:
                        printf("%ld\n", val.data.i_val);
                        break;
                    case VAL_FLOAT:
                        printf("%f\n", val.data.f_val);
                        break;
                    case VAL_STR:
                        printf("%s\n", val.data.s_val);
                        break;
                    case VAL_BOOL:
                        printf("%s\n", val.data.b_val ? "true" : "false");
                        break;
                    default:
                        printf("<unknown>\n");
                        break;
                }
                break;
            }
            
            case OP_HALT:
                return 0;
            
            case OP_NOP:
                break;
            
            default:
                fprintf(stderr, "Unknown opcode: %d\n", instr.op);
                return -1;
        }
        
        vm->pc++;
    }
    
    return 0;
}
