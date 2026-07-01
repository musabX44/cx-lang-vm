#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bytecode.h"

/* Forward declarations */
Lexer *lexer_create(const char *source, ErrorHandler *errors);
void lexer_tokenize(Lexer *lex);
void lexer_free(Lexer *lex);

Parser *parser_create(Lexer *lex, ErrorHandler *errors);
ASTNode *parser_parse_program(Parser *p);
void parser_free(Parser *p);

SemanticAnalyzer *semantic_analyzer_create(ErrorHandler *errors);
void semantic_analyze(SemanticAnalyzer *sa, ASTNode *program);
void semantic_analyzer_free(SemanticAnalyzer *sa);

void ast_free(ASTNode *node);

ErrorHandler *eh_create(void);
void eh_print_errors(ErrorHandler *eh);
void eh_free(ErrorHandler *eh);

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input.cx>\n", argv[0]);
        return 1;
    }
    
    const char *input_file = argv[1];
    
    FILE *f = fopen(input_file, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file %s\n", input_file);
        return 1;
    }
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *source = malloc(fsize + 1);
    fread(source, 1, fsize, f);
    source[fsize] = '\0';
    fclose(f);
    
    ErrorHandler *errors = eh_create();
    
    printf("[*] Lexing...\n");
    Lexer *lex = lexer_create(source, errors);
    lexer_tokenize(lex);
    printf("[+] Generated %zu tokens\n", lex->token_count);
    
    if (errors->count > 0) {
        eh_print_errors(errors);
        return 1;
    }
    
    printf("[*] Parsing...\n");
    Parser *parser = parser_create(lex, errors);
    ASTNode *ast = parser_parse_program(parser);
    printf("[+] AST generated\n");
    
    if (errors->count > 0) {
        eh_print_errors(errors);
        return 1;
    }
    
    printf("[*] Semantic Analysis...\n");
    SemanticAnalyzer *sa = semantic_analyzer_create(errors);
    semantic_analyze(sa, ast);
    printf("[+] Semantic analysis completed\n");
    
    if (errors->count > 0) {
        eh_print_errors(errors);
        return 1;
    }
    
    printf("[*] Generating bytecode...\n");
    BytecodeModule *module = bytecode_module_create();
    
    /* TODO: Bytecode generation from AST */
    
    printf("[+] Bytecode generated\n");
    
    printf("[*] Executing bytecode...\n");
    VM *vm = vm_create(module);
    int result = vm_execute(vm);
    printf("[+] Execution completed with code %d\n", result);
    
    vm_free(vm);
    bytecode_module_free(module);
    semantic_analyzer_free(sa);
    parser_free(parser);
    ast_free(ast);
    lexer_free(lex);
    eh_free(errors);
    free(source);
    
    return result;
}
