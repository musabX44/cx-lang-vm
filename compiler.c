#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdarg.h>
#include <assert.h>

#include "bytecode.h"

/* ============================================================================
 * UTILITIES & ERROR HANDLING
 * ============================================================================ */

#define MAX_ERRORS 100
#define MAX_SYMBOLS 1000
#define MAX_STRINGS 500
#define MAX_SCOPES 256
#define MAX_STACK_VARS 256

typedef struct {
    char *messages[MAX_ERRORS];
    int count;
} ErrorHandler;

ErrorHandler *eh_create(void) {
    ErrorHandler *eh = malloc(sizeof(ErrorHandler));
    eh->count = 0;
    return eh;
}

void eh_add_error(ErrorHandler *eh, const char *fmt, ...) {
    if (eh->count >= MAX_ERRORS) return;
    
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    eh->messages[eh->count++] = strdup(buffer);
}

void eh_print_errors(ErrorHandler *eh) {
    if (eh->count == 0) return;
    
    fprintf(stderr, "\n=== COMPILATION ERRORS ===\n");
    for (int i = 0; i < eh->count; i++) {
        fprintf(stderr, "[ERROR %d] %s\n", i + 1, eh->messages[i]);
    }
    fprintf(stderr, "Total: %d error(s)\n\n", eh->count);
}

void eh_free(ErrorHandler *eh) {
    for (int i = 0; i < eh->count; i++) {
        free(eh->messages[i]);
    }
    free(eh);
}

/* ============================================================================
 * TOKEN TYPES & LEXER
 * ============================================================================ */

typedef enum {
    TOKEN_INT, TOKEN_FLOAT, TOKEN_STRING, TOKEN_BOOL, TOKEN_CHAR,
    TOKEN_USE, TOKEN_FN, TOKEN_RET, TOKEN_IF, TOKEN_ELSE,
    TOKEN_LOOP, TOKEN_STRUCT, TOKEN_ENUM, TOKEN_MATCH,
    TOKEN_IDENT, TOKEN_TYPE,
    TOKEN_PLUS, TOKEN_MINUS, TOKEN_STAR, TOKEN_SLASH, TOKEN_PERCENT,
    TOKEN_EQ, TOKEN_NEQ, TOKEN_LT, TOKEN_GT, TOKEN_LTE, TOKEN_GTE,
    TOKEN_AND, TOKEN_OR, TOKEN_NOT, TOKEN_AMPERSAND, TOKEN_ASSIGN,
    TOKEN_DOT, TOKEN_LBRACKET, TOKEN_RBRACKET,
    TOKEN_LPAREN, TOKEN_RPAREN, TOKEN_LBRACE, TOKEN_RBRACE,
    TOKEN_COMMA, TOKEN_COLON, TOKEN_SEMICOLON, TOKEN_ARROW,
    TOKEN_EOF, TOKEN_NEWLINE
} TokenType;

typedef struct {
    TokenType type;
    char *value;
    int line;
    int column;
} Token;

typedef struct {
    const char *source;
    size_t pos;
    int line;
    int column;
    Token *tokens;
    size_t token_count;
    size_t token_capacity;
    ErrorHandler *errors;
} Lexer;

Lexer *lexer_create(const char *source, ErrorHandler *errors) {
    Lexer *lex = malloc(sizeof(Lexer));
    lex->source = source;
    lex->pos = 0;
    lex->line = 1;
    lex->column = 1;
    lex->tokens = malloc(sizeof(Token) * 4096);
    lex->token_count = 0;
    lex->token_capacity = 4096;
    lex->errors = errors;
    return lex;
}

char lexer_peek(Lexer *lex, int offset) {
    size_t p = lex->pos + offset;
    return p < strlen(lex->source) ? lex->source[p] : '\0';
}

char lexer_advance(Lexer *lex) {
    if (lex->pos >= strlen(lex->source)) return '\0';
    char ch = lex->source[lex->pos++];
    if (ch == '\n') {
        lex->line++;
        lex->column = 1;
    } else {
        lex->column++;
    }
    return ch;
}

void lexer_skip_whitespace(Lexer *lex) {
    while (lexer_peek(lex, 0) == ' ' || lexer_peek(lex, 0) == '\t' || 
           lexer_peek(lex, 0) == '\r') {
        lexer_advance(lex);
    }
}

void lexer_skip_comment(Lexer *lex) {
    if (lexer_peek(lex, 0) == '/' && lexer_peek(lex, 1) == '/') {
        while (lexer_peek(lex, 0) && lexer_peek(lex, 0) != '\n') {
            lexer_advance(lex);
        }
    }
}

char *lexer_read_string(Lexer *lex, char quote) {
    char *result = malloc(2048);
    size_t idx = 0;
    lexer_advance(lex);
    
    while (lexer_peek(lex, 0) && lexer_peek(lex, 0) != quote) {
        if (lexer_peek(lex, 0) == '\\') {
            lexer_advance(lex);
            char next = lexer_advance(lex);
            switch (next) {
                case 'n': result[idx++] = '\n'; break;
                case 't': result[idx++] = '\t'; break;
                case 'r': result[idx++] = '\r'; break;
                case '\\': result[idx++] = '\\'; break;
                case '"': result[idx++] = '"'; break;
                case '\'': result[idx++] = '\''; break;
                default: result[idx++] = next; break;
            }
        } else {
            result[idx++] = lexer_advance(lex);
        }
        if (idx >= 2048 - 1) {
            eh_add_error(lex->errors, "String too long at line %d", lex->line);
            break;
        }
    }
    
    if (lexer_peek(lex, 0) != quote) {
        eh_add_error(lex->errors, "Unterminated string at line %d", lex->line);
    } else {
        lexer_advance(lex);
    }
    
    result[idx] = '\0';
    return result;
}

Token lexer_read_number(Lexer *lex) {
    Token tok;
    tok.line = lex->line;
    tok.column = lex->column;
    tok.type = TOKEN_INT;
    
    char *num_str = malloc(256);
    size_t idx = 0;
    int has_dot = 0;
    
    while (isdigit(lexer_peek(lex, 0)) || lexer_peek(lex, 0) == '.') {
        if (lexer_peek(lex, 0) == '.') {
            if (has_dot) break;
            has_dot = 1;
            tok.type = TOKEN_FLOAT;
        }
        num_str[idx++] = lexer_advance(lex);
    }
    num_str[idx] = '\0';
    tok.value = num_str;
    return tok;
}

Token lexer_read_ident(Lexer *lex) {
    Token tok;
    tok.line = lex->line;
    tok.column = lex->column;
    tok.type = TOKEN_IDENT;
    
    char *ident = malloc(256);
    size_t idx = 0;
    
    while (isalnum(lexer_peek(lex, 0)) || lexer_peek(lex, 0) == '_') {
        ident[idx++] = lexer_advance(lex);
    }
    ident[idx] = '\0';
    
    if (strcmp(ident, "use") == 0) tok.type = TOKEN_USE;
    else if (strcmp(ident, "fn") == 0) tok.type = TOKEN_FN;
    else if (strcmp(ident, "ret") == 0) tok.type = TOKEN_RET;
    else if (strcmp(ident, "if") == 0) tok.type = TOKEN_IF;
    else if (strcmp(ident, "else") == 0) tok.type = TOKEN_ELSE;
    else if (strcmp(ident, "loop") == 0) tok.type = TOKEN_LOOP;
    else if (strcmp(ident, "struct") == 0) tok.type = TOKEN_STRUCT;
    else if (strcmp(ident, "enum") == 0) tok.type = TOKEN_ENUM;
    else if (strcmp(ident, "match") == 0) tok.type = TOKEN_MATCH;
    else if (strcmp(ident, "true") == 0 || strcmp(ident, "false") == 0) tok.type = TOKEN_BOOL;
    else if (strcmp(ident, "i") == 0 || strcmp(ident, "u") == 0 || 
             strcmp(ident, "f") == 0 || strcmp(ident, "b") == 0 || 
             strcmp(ident, "c") == 0 || strcmp(ident, "s") == 0 || 
             strcmp(ident, "ptr") == 0 || strcmp(ident, "void") == 0) {
        tok.type = TOKEN_TYPE;
    }
    
    tok.value = ident;
    return tok;
}

void lexer_add_token(Lexer *lex, Token tok) {
    if (lex->token_count >= lex->token_capacity) {
        lex->token_capacity *= 2;
        lex->tokens = realloc(lex->tokens, sizeof(Token) * lex->token_capacity);
    }
    lex->tokens[lex->token_count++] = tok;
}

void lexer_tokenize(Lexer *lex) {
    while (lex->pos < strlen(lex->source)) {
        lexer_skip_whitespace(lex);
        if (lex->pos >= strlen(lex->source)) break;
        
        if (lexer_peek(lex, 0) == '/' && lexer_peek(lex, 1) == '/') {
            lexer_skip_comment(lex);
            continue;
        }
        
        int line = lex->line;
        int column = lex->column;
        char ch = lexer_peek(lex, 0);
        Token tok = {.line = line, .column = column, .value = NULL};
        
        if (ch == '\n') {
            lexer_advance(lex);
            tok.type = TOKEN_NEWLINE;
            tok.value = strdup("\n");
            lexer_add_token(lex, tok);
        } else if (ch == '"') {
            tok.type = TOKEN_STRING;
            tok.value = lexer_read_string(lex, '"');
            lexer_add_token(lex, tok);
        } else if (ch == '\'') {
            tok.type = TOKEN_CHAR;
            tok.value = lexer_read_string(lex, '\'');
            lexer_add_token(lex, tok);
        } else if (isdigit(ch)) {
            tok = lexer_read_number(lex);
            lexer_add_token(lex, tok);
        } else if (isalpha(ch) || ch == '_') {
            tok = lexer_read_ident(lex);
            lexer_add_token(lex, tok);
        } else if (ch == '+') {
            lexer_advance(lex);
            tok.type = TOKEN_PLUS;
            tok.value = strdup("+");
            lexer_add_token(lex, tok);
        } else if (ch == '-') {
            lexer_advance(lex);
            if (lexer_peek(lex, 0) == '>') {
                lexer_advance(lex);
                tok.type = TOKEN_ARROW;
                tok.value = strdup("=>");
            } else {
                tok.type = TOKEN_MINUS;
                tok.value = strdup("-");
            }
            lexer_add_token(lex, tok);
        } else if (ch == '*') {
            lexer_advance(lex);
            tok.type = TOKEN_STAR;
            tok.value = strdup("*");
            lexer_add_token(lex, tok);
        } else if (ch == '/') {
            lexer_advance(lex);
            tok.type = TOKEN_SLASH;
            tok.value = strdup("/");
            lexer_add_token(lex, tok);
        } else if (ch == '%') {
            lexer_advance(lex);
            tok.type = TOKEN_PERCENT;
            tok.value = strdup("%");
            lexer_add_token(lex, tok);
        } else if (ch == '=') {
            lexer_advance(lex);
            if (lexer_peek(lex, 0) == '=') {
                lexer_advance(lex);
                tok.type = TOKEN_EQ;
                tok.value = strdup("==");
            } else {
                tok.type = TOKEN_ASSIGN;
                tok.value = strdup("=");
            }
            lexer_add_token(lex, tok);
        } else if (ch == '!') {
            lexer_advance(lex);
            if (lexer_peek(lex, 0) == '=') {
                lexer_advance(lex);
                tok.type = TOKEN_NEQ;
                tok.value = strdup("!=");
            } else {
                tok.type = TOKEN_NOT;
                tok.value = strdup("!");
            }
            lexer_add_token(lex, tok);
        } else if (ch == '<') {
            lexer_advance(lex);
            if (lexer_peek(lex, 0) == '=') {
                lexer_advance(lex);
                tok.type = TOKEN_LTE;
                tok.value = strdup("<=");
            } else {
                tok.type = TOKEN_LT;
                tok.value = strdup("<");
            }
            lexer_add_token(lex, tok);
        } else if (ch == '>') {
            lexer_advance(lex);
            if (lexer_peek(lex, 0) == '=') {
                lexer_advance(lex);
                tok.type = TOKEN_GTE;
                tok.value = strdup(">=");
            } else {
                tok.type = TOKEN_GT;
                tok.value = strdup(">");
            }
            lexer_add_token(lex, tok);
        } else if (ch == '&') {
            lexer_advance(lex);
            if (lexer_peek(lex, 0) == '&') {
                lexer_advance(lex);
                tok.type = TOKEN_AND;
                tok.value = strdup("&&");
            } else {
                tok.type = TOKEN_AMPERSAND;
                tok.value = strdup("&");
            }
            lexer_add_token(lex, tok);
        } else if (ch == '|') {
            lexer_advance(lex);
            if (lexer_peek(lex, 0) == '|') {
                lexer_advance(lex);
                tok.type = TOKEN_OR;
                tok.value = strdup("||");
            }
            lexer_add_token(lex, tok);
        } else if (ch == '(') {
            lexer_advance(lex);
            tok.type = TOKEN_LPAREN;
            tok.value = strdup("(");
            lexer_add_token(lex, tok);
        } else if (ch == ')') {
            lexer_advance(lex);
            tok.type = TOKEN_RPAREN;
            tok.value = strdup(")");
            lexer_add_token(lex, tok);
        } else if (ch == '{') {
            lexer_advance(lex);
            tok.type = TOKEN_LBRACE;
            tok.value = strdup("{");
            lexer_add_token(lex, tok);
        } else if (ch == '}') {
            lexer_advance(lex);
            tok.type = TOKEN_RBRACE;
            tok.value = strdup("}");
            lexer_add_token(lex, tok);
        } else if (ch == '[') {
            lexer_advance(lex);
            tok.type = TOKEN_LBRACKET;
            tok.value = strdup("[");
            lexer_add_token(lex, tok);
        } else if (ch == ']') {
            lexer_advance(lex);
            tok.type = TOKEN_RBRACKET;
            tok.value = strdup("]");
            lexer_add_token(lex, tok);
        } else if (ch == ',') {
            lexer_advance(lex);
            tok.type = TOKEN_COMMA;
            tok.value = strdup(",");
            lexer_add_token(lex, tok);
        } else if (ch == ':') {
            lexer_advance(lex);
            tok.type = TOKEN_COLON;
            tok.value = strdup(":");
            lexer_add_token(lex, tok);
        } else if (ch == ';') {
            lexer_advance(lex);
            tok.type = TOKEN_SEMICOLON;
            tok.value = strdup(";");
            lexer_add_token(lex, tok);
        } else if (ch == '.') {
            lexer_advance(lex);
            tok.type = TOKEN_DOT;
            tok.value = strdup(".");
            lexer_add_token(lex, tok);
        } else {
            eh_add_error(lex->errors, "Unexpected character '%c' at line %d:%d", ch, lex->line, lex->column);
            lexer_advance(lex);
        }
    }
    
    Token eof = {.type = TOKEN_EOF, .value = NULL, .line = lex->line, .column = lex->column};
    lexer_add_token(lex, eof);
}

void lexer_free(Lexer *lex) {
    for (size_t i = 0; i < lex->token_count; i++) {
        if (lex->tokens[i].value) free(lex->tokens[i].value);
    }
    free(lex->tokens);
    free(lex);
}

/* ============================================================================
 * AST NODES
 * ============================================================================ */

typedef enum {
    NODE_PROGRAM, NODE_IMPORT,
    NODE_FUNCTION, NODE_STRUCT, NODE_ENUM,
    NODE_BLOCK, NODE_VAR_DECL, NODE_VAR_ASSIGN, NODE_RETURN,
    NODE_IF, NODE_LOOP, NODE_MATCH,
    NODE_EXPR_STMT,
    NODE_BINARY_OP, NODE_UNARY_OP, NODE_CALL, NODE_INDEX, NODE_MEMBER,
    NODE_INT_LIT, NODE_FLOAT_LIT, NODE_STRING_LIT, NODE_CHAR_LIT, NODE_BOOL_LIT,
    NODE_IDENT, NODE_ADDRESSOF, NODE_DEREF
} NodeType;

typedef struct ASTNode {
    NodeType type;
    char *name;
    char *value;
    char *type_name;
    int line, column;
    struct ASTNode **children;
    size_t child_count;
    size_t child_capacity;
} ASTNode;

ASTNode *ast_create(NodeType type, int line, int column) {
    ASTNode *node = malloc(sizeof(ASTNode));
    node->type = type;
    node->name = NULL;
    node->value = NULL;
    node->type_name = NULL;
    node->line = line;
    node->column = column;
    node->children = malloc(sizeof(ASTNode*) * 64);
    node->child_count = 0;
    node->child_capacity = 64;
    return node;
}

void ast_add_child(ASTNode *parent, ASTNode *child) {
    if (parent->child_count >= parent->child_capacity) {
        parent->child_capacity *= 2;
        parent->children = realloc(parent->children, sizeof(ASTNode*) * parent->child_capacity);
    }
    parent->children[parent->child_count++] = child;
}

void ast_free(ASTNode *node) {
    if (!node) return;
    if (node->name) free(node->name);
    if (node->value) free(node->value);
    if (node->type_name) free(node->type_name);
    for (size_t i = 0; i < node->child_count; i++) {
        ast_free(node->children[i]);
    }
    free(node->children);
    free(node);
}

/* ============================================================================
 * SYMBOL TABLE & SCOPE MANAGEMENT
 * ============================================================================ */

typedef struct {
    char *name;
    char *type;
    int stack_offset;
    int is_function;
    int is_struct;
    int is_enum;
    char *struct_name;
} Symbol;

typedef struct Scope {
    Symbol **symbols;
    int symbol_count;
    struct Scope *parent;
} Scope;

Scope *scope_create(Scope *parent) {
    Scope *scope = malloc(sizeof(Scope));
    scope->symbols = malloc(sizeof(Symbol*) * MAX_SYMBOLS);
    scope->symbol_count = 0;
    scope->parent = parent;
    return scope;
}

Symbol *scope_lookup(Scope *scope, const char *name) {
    while (scope) {
        for (int i = 0; i < scope->symbol_count; i++) {
            if (strcmp(scope->symbols[i]->name, name) == 0) {
                return scope->symbols[i];
            }
        }
        scope = scope->parent;
    }
    return NULL;
}

Symbol *scope_lookup_local(Scope *scope, const char *name) {
    for (int i = 0; i < scope->symbol_count; i++) {
        if (strcmp(scope->symbols[i]->name, name) == 0) {
            return scope->symbols[i];
        }
    }
    return NULL;
}

void scope_define(Scope *scope, const char *name, const char *type, int stack_offset, ErrorHandler *eh) {
    if (scope_lookup_local(scope, name)) {
        eh_add_error(eh, "Symbol '%s' already defined in current scope", name);
        return;
    }
    
    if (scope->symbol_count >= MAX_SYMBOLS) {
        eh_add_error(eh, "Too many symbols");
        return;
    }
    
    Symbol *sym = malloc(sizeof(Symbol));
    sym->name = strdup(name);
    sym->type = strdup(type);
    sym->stack_offset = stack_offset;
    sym->is_function = 0;
    sym->is_struct = 0;
    sym->is_enum = 0;
    sym->struct_name = NULL;
    
    scope->symbols[scope->symbol_count++] = sym;
}

void scope_free(Scope *scope) {
    for (int i = 0; i < scope->symbol_count; i++) {
        free(scope->symbols[i]->name);
        free(scope->symbols[i]->type);
        if (scope->symbols[i]->struct_name) free(scope->symbols[i]->struct_name);
        free(scope->symbols[i]);
    }
    free(scope->symbols);
    free(scope);
}

/* ============================================================================
 * TYPE SYSTEM
 * ============================================================================ */

int type_is_valid(const char *type) {
    return strcmp(type, "i") == 0 || strcmp(type, "u") == 0 ||
           strcmp(type, "f") == 0 || strcmp(type, "b") == 0 ||
           strcmp(type, "c") == 0 || strcmp(type, "s") == 0 ||
           strcmp(type, "ptr") == 0 || strcmp(type, "void") == 0;
}

int type_is_numeric(const char *type) {
    return strcmp(type, "i") == 0 || strcmp(type, "u") == 0 || strcmp(type, "f") == 0;
}

int type_is_comparable(const char *type) {
    return type_is_numeric(type) || strcmp(type, "b") == 0 || strcmp(type, "c") == 0;
}

int types_equal(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

/* ============================================================================
 * PARSER
 * ============================================================================ */

typedef struct {
    Token *tokens;
    size_t token_count;
    size_t pos;
    ErrorHandler *errors;
} Parser;

Parser *parser_create(Lexer *lex, ErrorHandler *errors) {
    Parser *p = malloc(sizeof(Parser));
    p->tokens = lex->tokens;
    p->token_count = lex->token_count;
    p->pos = 0;
    p->errors = errors;
    return p;
}

Token *parser_current(Parser *p) {
    if (p->pos < p->token_count) {
        return &p->tokens[p->pos];
    }
    return &p->tokens[p->token_count - 1];
}

Token *parser_peek(Parser *p, int offset) {
    size_t pos = p->pos + offset;
    return pos < p->token_count ? &p->tokens[pos] : &p->tokens[p->token_count - 1];
}

Token *parser_advance(Parser *p) {
    Token *tok = parser_current(p);
    if (tok->type != TOKEN_EOF) p->pos++;
    return tok;
}

void parser_expect(Parser *p, TokenType type) {
    Token *tok = parser_current(p);
    if (tok->type != type) {
        eh_add_error(p->errors, "Expected token type %d, got %d at line %d:%d",
                     type, tok->type, tok->line, tok->column);
    }
    parser_advance(p);
}

void parser_skip_newlines(Parser *p) {
    while (parser_current(p)->type == TOKEN_NEWLINE) {
        parser_advance(p);
    }
}

ASTNode *parser_parse_expression(Parser *p);
ASTNode *parser_parse_statement(Parser *p);
ASTNode *parser_parse_block(Parser *p);

ASTNode *parser_parse_primary(Parser *p) {
    Token *tok = parser_current(p);
    int line = tok->line, col = tok->column;
    
    if (tok->type == TOKEN_INT) {
        ASTNode *node = ast_create(NODE_INT_LIT, line, col);
        node->value = strdup(tok->value);
        parser_advance(p);
        return node;
    } else if (tok->type == TOKEN_FLOAT) {
        ASTNode *node = ast_create(NODE_FLOAT_LIT, line, col);
        node->value = strdup(tok->value);
        parser_advance(p);
        return node;
    } else if (tok->type == TOKEN_STRING) {
        ASTNode *node = ast_create(NODE_STRING_LIT, line, col);
        node->value = strdup(tok->value);
        parser_advance(p);
        return node;
    } else if (tok->type == TOKEN_CHAR) {
        ASTNode *node = ast_create(NODE_CHAR_LIT, line, col);
        node->value = strdup(tok->value);
        parser_advance(p);
        return node;
    } else if (tok->type == TOKEN_BOOL) {
        ASTNode *node = ast_create(NODE_BOOL_LIT, line, col);
        node->value = strdup(tok->value);
        parser_advance(p);
        return node;
    } else if (tok->type == TOKEN_IDENT) {
        ASTNode *node = ast_create(NODE_IDENT, line, col);
        node->name = strdup(tok->value);
        parser_advance(p);
        return node;
    } else if (tok->type == TOKEN_LPAREN) {
        parser_advance(p);
        ASTNode *expr = parser_parse_expression(p);
        parser_expect(p, TOKEN_RPAREN);
        return expr;
    } else {
        eh_add_error(p->errors, "Unexpected token in primary expression at line %d:%d", line, col);
        parser_advance(p);
        return ast_create(NODE_INT_LIT, line, col);
    }
}

ASTNode *parser_parse_postfix(Parser *p) {
    ASTNode *expr = parser_parse_primary(p);
    
    while (1) {
        if (parser_current(p)->type == TOKEN_LPAREN) {
            int line = parser_current(p)->line;
            parser_advance(p);
            ASTNode *call = ast_create(NODE_CALL, line, 0);
            ast_add_child(call, expr);
            
            while (parser_current(p)->type != TOKEN_RPAREN && 
                   parser_current(p)->type != TOKEN_EOF) {
                ast_add_child(call, parser_parse_expression(p));
                if (parser_current(p)->type == TOKEN_COMMA) {
                    parser_advance(p);
                }
            }
            parser_expect(p, TOKEN_RPAREN);
            expr = call;
        } else if (parser_current(p)->type == TOKEN_LBRACKET) {
            int line = parser_current(p)->line;
            parser_advance(p);
            ASTNode *index = ast_create(NODE_INDEX, line, 0);
            ast_add_child(index, expr);
            ast_add_child(index, parser_parse_expression(p));
            parser_expect(p, TOKEN_RBRACKET);
            expr = index;
        } else if (parser_current(p)->type == TOKEN_DOT) {
            int line = parser_current(p)->line;
            parser_advance(p);
            Token *member_tok = parser_current(p);
            ASTNode *member = ast_create(NODE_MEMBER, line, 0);
            member->name = strdup(member_tok->value);
            ast_add_child(member, expr);
            parser_advance(p);
            expr = member;
        } else {
            break;
        }
    }
    
    return expr;
}

ASTNode *parser_parse_unary(Parser *p) {
    Token *tok = parser_current(p);
    int line = tok->line, col = tok->column;
    
    if (tok->type == TOKEN_NOT) {
        parser_advance(p);
        ASTNode *node = ast_create(NODE_UNARY_OP, line, col);
        node->value = strdup("!");
        ast_add_child(node, parser_parse_unary(p));
        return node;
    } else if (tok->type == TOKEN_MINUS) {
        parser_advance(p);
        ASTNode *node = ast_create(NODE_UNARY_OP, line, col);
        node->value = strdup("-");
        ast_add_child(node, parser_parse_unary(p));
        return node;
    } else if (tok->type == TOKEN_AMPERSAND) {
        parser_advance(p);
        ASTNode *node = ast_create(NODE_ADDRESSOF, line, col);
        ast_add_child(node, parser_parse_unary(p));
        return node;
    } else if (tok->type == TOKEN_STAR) {
        parser_advance(p);
        ASTNode *node = ast_create(NODE_DEREF, line, col);
        ast_add_child(node, parser_parse_unary(p));
        return node;
    }
    
    return parser_parse_postfix(p);
}

ASTNode *parser_parse_multiplicative(Parser *p) {
    ASTNode *left = parser_parse_unary(p);
    
    while (parser_current(p)->type == TOKEN_STAR || 
           parser_current(p)->type == TOKEN_SLASH || 
           parser_current(p)->type == TOKEN_PERCENT) {
        int line = parser_current(p)->line;
        ASTNode *node = ast_create(NODE_BINARY_OP, line, 0);
        node->value = strdup(parser_current(p)->value);
        parser_advance(p);
        ast_add_child(node, left);
        ast_add_child(node, parser_parse_unary(p));
        left = node;
    }
    
    return left;
}

ASTNode *parser_parse_additive(Parser *p) {
    ASTNode *left = parser_parse_multiplicative(p);
    
    while (parser_current(p)->type == TOKEN_PLUS || 
           parser_current(p)->type == TOKEN_MINUS) {
        int line = parser_current(p)->line;
        ASTNode *node = ast_create(NODE_BINARY_OP, line, 0);
        node->value = strdup(parser_current(p)->value);
        parser_advance(p);
        ast_add_child(node, left);
        ast_add_child(node, parser_parse_multiplicative(p));
        left = node;
    }
    
    return left;
}

ASTNode *parser_parse_comparison(Parser *p) {
    ASTNode *left = parser_parse_additive(p);
    
    while (parser_current(p)->type == TOKEN_LT || 
           parser_current(p)->type == TOKEN_GT || 
           parser_current(p)->type == TOKEN_LTE || 
           parser_current(p)->type == TOKEN_GTE) {
        int line = parser_current(p)->line;
        ASTNode *node = ast_create(NODE_BINARY_OP, line, 0);
        node->value = strdup(parser_current(p)->value);
        parser_advance(p);
        ast_add_child(node, left);
        ast_add_child(node, parser_parse_additive(p));
        left = node;
    }
    
    return left;
}

ASTNode *parser_parse_equality(Parser *p) {
    ASTNode *left = parser_parse_comparison(p);
    
    while (parser_current(p)->type == TOKEN_EQ || 
           parser_current(p)->type == TOKEN_NEQ) {
        int line = parser_current(p)->line;
        ASTNode *node = ast_create(NODE_BINARY_OP, line, 0);
        node->value = strdup(parser_current(p)->value);
        parser_advance(p);
        ast_add_child(node, left);
        ast_add_child(node, parser_parse_comparison(p));
        left = node;
    }
    
    return left;
}

ASTNode *parser_parse_and(Parser *p) {
    ASTNode *left = parser_parse_equality(p);
    
    while (parser_current(p)->type == TOKEN_AND) {
        int line = parser_current(p)->line;
        parser_advance(p);
        ASTNode *node = ast_create(NODE_BINARY_OP, line, 0);
        node->value = strdup("&&");
        ast_add_child(node, left);
        ast_add_child(node, parser_parse_equality(p));
        left = node;
    }
    
    return left;
}

ASTNode *parser_parse_or(Parser *p) {
    ASTNode *left = parser_parse_and(p);
    
    while (parser_current(p)->type == TOKEN_OR) {
        int line = parser_current(p)->line;
        parser_advance(p);
        ASTNode *node = ast_create(NODE_BINARY_OP, line, 0);
        node->value = strdup("||");
        ast_add_child(node, left);
        ast_add_child(node, parser_parse_and(p));
        left = node;
    }
    
    return left;
}

ASTNode *parser_parse_expression(Parser *p) {
    return parser_parse_or(p);
}

ASTNode *parser_parse_block(Parser *p) {
    int line = parser_current(p)->line;
    parser_expect(p, TOKEN_LBRACE);
    parser_skip_newlines(p);
    
    ASTNode *block = ast_create(NODE_BLOCK, line, 0);
    
    while (parser_current(p)->type != TOKEN_RBRACE && 
           parser_current(p)->type != TOKEN_EOF) {
        ast_add_child(block, parser_parse_statement(p));
        parser_skip_newlines(p);
    }
    
    parser_expect(p, TOKEN_RBRACE);
    parser_skip_newlines(p);
    
    return block;
}

ASTNode *parser_parse_statement(Parser *p) {
    parser_skip_newlines(p);
    Token *tok = parser_current(p);
    int line = tok->line, col = tok->column;
    
    if (tok->type == TOKEN_RET) {
        parser_advance(p);
        ASTNode *node = ast_create(NODE_RETURN, line, col);
        if (parser_current(p)->type != TOKEN_SEMICOLON && 
            parser_current(p)->type != TOKEN_NEWLINE && 
            parser_current(p)->type != TOKEN_RBRACE &&
            parser_current(p)->type != TOKEN_EOF) {
            ast_add_child(node, parser_parse_expression(p));
        }
        if (parser_current(p)->type == TOKEN_SEMICOLON) {
            parser_advance(p);
        }
        return node;
    } else if (tok->type == TOKEN_IF) {
        parser_advance(p);
        ASTNode *node = ast_create(NODE_IF, line, col);
        ast_add_child(node, parser_parse_expression(p));
        ast_add_child(node, parser_parse_block(p));
        
        if (parser_current(p)->type == TOKEN_ELSE) {
            parser_advance(p);
            ast_add_child(node, parser_parse_block(p));
        }
        return node;
    } else if (tok->type == TOKEN_LOOP) {
        parser_advance(p);
        ASTNode *node = ast_create(NODE_LOOP, line, col);
        
        if (parser_current(p)->type != TOKEN_LBRACE) {
            ast_add_child(node, parser_parse_expression(p));
        }
        ast_add_child(node, parser_parse_block(p));
        return node;
    } else if (tok->type == TOKEN_MATCH) {
        parser_advance(p);
        ASTNode *node = ast_create(NODE_MATCH, line, col);
        ast_add_child(node, parser_parse_expression(p));
        parser_expect(p, TOKEN_LBRACE);
        parser_skip_newlines(p);
        
        while (parser_current(p)->type != TOKEN_RBRACE && 
               parser_current(p)->type != TOKEN_EOF) {
            ast_add_child(node, parser_parse_expression(p));
            parser_expect(p, TOKEN_ARROW);
            ast_add_child(node, parser_parse_expression(p));
            parser_skip_newlines(p);
        }
        
        parser_expect(p, TOKEN_RBRACE);
        parser_skip_newlines(p);
        return node;
    } else if (tok->type == TOKEN_IDENT && parser_peek(p, 1)->type == TOKEN_COLON) {
        ASTNode *node = ast_create(NODE_VAR_DECL, line, col);
        node->name = strdup(tok->value);
        parser_advance(p);
        parser_expect(p, TOKEN_COLON);
        
        Token *type_tok = parser_current(p);
        node->type_name = strdup(type_tok->value);
        parser_advance(p);
        
        if (parser_current(p)->type == TOKEN_ASSIGN) {
            parser_advance(p);
            ast_add_child(node, parser_parse_expression(p));
        }
        if (parser_current(p)->type == TOKEN_SEMICOLON) {
            parser_advance(p);
        }
        return node;
    } else {
        ASTNode *expr = parser_parse_expression(p);
        
        if (parser_current(p)->type == TOKEN_ASSIGN) {
            ASTNode *assign = ast_create(NODE_VAR_ASSIGN, line, col);
            ast_add_child(assign, expr);
            parser_advance(p);
            ast_add_child(assign, parser_parse_expression(p));
            if (parser_current(p)->type == TOKEN_SEMICOLON) {
                parser_advance(p);
            }
            return assign;
        }
        
        ASTNode *stmt = ast_create(NODE_EXPR_STMT, line, col);
        ast_add_child(stmt, expr);
        if (parser_current(p)->type == TOKEN_SEMICOLON) {
            parser_advance(p);
        }
        return stmt;
    }
}

ASTNode *parser_parse_function(Parser *p) {
    int line = parser_current(p)->line;
    parser_expect(p, TOKEN_FN);
    
    ASTNode *node = ast_create(NODE_FUNCTION, line, 0);
    Token *name_tok = parser_current(p);
    node->name = strdup(name_tok->value);
    parser_advance(p);
    
    parser_expect(p, TOKEN_LPAREN);
    
    while (parser_current(p)->type != TOKEN_RPAREN && 
           parser_current(p)->type != TOKEN_EOF) {
        Token *param_name = parser_current(p);
        parser_advance(p);
        parser_expect(p, TOKEN_COLON);
        Token *param_type = parser_current(p);
        
        ASTNode *param = ast_create(NODE_VAR_DECL, param_name->line, param_name->column);
        param->name = strdup(param_name->value);
        param->type_name = strdup(param_type->value);
        ast_add_child(node, param);
        
        parser_advance(p);
        if (parser_current(p)->type == TOKEN_COMMA) {
            parser_advance(p);
        }
    }
    
    parser_expect(p, TOKEN_RPAREN);
    parser_expect(p, TOKEN_COLON);
    
    Token *ret_type = parser_current(p);
    node->type_name = strdup(ret_type->value);
    parser_advance(p);
    
    ASTNode *body = parser_parse_block(p);
    ast_add_child(node, body);
    
    return node;
}

ASTNode *parser_parse_struct(Parser *p) {
    int line = parser_current(p)->line;
    parser_expect(p, TOKEN_STRUCT);
    
    ASTNode *node = ast_create(NODE_STRUCT, line, 0);
    Token *name_tok = parser_current(p);
    node->name = strdup(name_tok->value);
    parser_advance(p);
    
    parser_expect(p, TOKEN_LBRACE);
    parser_skip_newlines(p);
    
    while (parser_current(p)->type != TOKEN_RBRACE && 
           parser_current(p)->type != TOKEN_EOF) {
        Token *field_name = parser_current(p);
        parser_advance(p);
        parser_expect(p, TOKEN_COLON);
        Token *field_type = parser_current(p);
        
        ASTNode *field = ast_create(NODE_VAR_DECL, field_name->line, field_name->column);
        field->name = strdup(field_name->value);
        field->type_name = strdup(field_type->value);
        ast_add_child(node, field);
        
        parser_advance(p);
        parser_skip_newlines(p);
    }
    
    parser_expect(p, TOKEN_RBRACE);
    parser_skip_newlines(p);
    
    return node;
}

ASTNode *parser_parse_enum(Parser *p) {
    int line = parser_current(p)->line;
    parser_expect(p, TOKEN_ENUM);
    
    ASTNode *node = ast_create(NODE_ENUM, line, 0);
    Token *name_tok = parser_current(p);
    node->name = strdup(name_tok->value);
    parser_advance(p);
    
    parser_expect(p, TOKEN_LBRACE);
    parser_skip_newlines(p);
    
    while (parser_current(p)->type != TOKEN_RBRACE && 
           parser_current(p)->type != TOKEN_EOF) {
        Token *variant = parser_current(p);
        ASTNode *var_node = ast_create(NODE_IDENT, variant->line, variant->column);
        var_node->name = strdup(variant->value);
        ast_add_child(node, var_node);
        
        parser_advance(p);
        parser_skip_newlines(p);
    }
    
    parser_expect(p, TOKEN_RBRACE);
    parser_skip_newlines(p);
    
    return node;
}

ASTNode *parser_parse_program(Parser *p) {
    parser_skip_newlines(p);
    ASTNode *program = ast_create(NODE_PROGRAM, 1, 1);
    
    while (parser_current(p)->type != TOKEN_EOF) {
        if (parser_current(p)->type == TOKEN_USE) {
            int line = parser_current(p)->line;
            parser_advance(p);
            Token *module = parser_current(p);
            ASTNode *import = ast_create(NODE_IMPORT, line, 0);
            import->name = strdup(module->value);
            ast_add_child(program, import);
            parser_advance(p);
            if (parser_current(p)->type == TOKEN_SEMICOLON) {
                parser_advance(p);
            }
            parser_skip_newlines(p);
        } else if (parser_current(p)->type == TOKEN_FN) {
            ast_add_child(program, parser_parse_function(p));
        } else if (parser_current(p)->type == TOKEN_STRUCT) {
            ast_add_child(program, parser_parse_struct(p));
        } else if (parser_current(p)->type == TOKEN_ENUM) {
            ast_add_child(program, parser_parse_enum(p));
        } else {
            parser_advance(p);
        }
    }
    
    return program;
}

void parser_free(Parser *p) {
    free(p);
}

/* ============================================================================
 * SEMANTIC ANALYZER
 * ============================================================================ */

typedef struct {
    Scope *global_scope;
    Scope *current_scope;
    ErrorHandler *errors;
    int stack_offset;
    char *current_function;
    char *current_function_return_type;
} SemanticAnalyzer;

SemanticAnalyzer *semantic_analyzer_create(ErrorHandler *errors) {
    SemanticAnalyzer *sa = malloc(sizeof(SemanticAnalyzer));
    sa->global_scope = scope_create(NULL);
    sa->current_scope = sa->global_scope;
    sa->errors = errors;
    sa->stack_offset = 0;
    sa->current_function = NULL;
    sa->current_function_return_type = NULL;
    return sa;
}

char *semantic_analyze_expr(SemanticAnalyzer *sa, ASTNode *expr);
void semantic_analyze_node(SemanticAnalyzer *sa, ASTNode *node);

void semantic_analyze_block(SemanticAnalyzer *sa, ASTNode *block) {
    if (!block || block->type != NODE_BLOCK) return;
    
    Scope *prev_scope = sa->current_scope;
    sa->current_scope = scope_create(prev_scope);
    int prev_offset = sa->stack_offset;
    
    for (size_t i = 0; i < block->child_count; i++) {
        semantic_analyze_node(sa, block->children[i]);
    }
    
    sa->stack_offset = prev_offset;
    scope_free(sa->current_scope);
    sa->current_scope = prev_scope;
}

char *semantic_analyze_expr(SemanticAnalyzer *sa, ASTNode *expr) {
    if (!expr) return strdup("void");
    
    char *result = malloc(64);
    
    switch (expr->type) {
        case NODE_INT_LIT:
            strcpy(result, "i");
            break;
        case NODE_FLOAT_LIT:
            strcpy(result, "f");
            break;
        case NODE_STRING_LIT:
            strcpy(result, "s");
            break;
        case NODE_CHAR_LIT:
            strcpy(result, "c");
            break;
        case NODE_BOOL_LIT:
            strcpy(result, "b");
            break;
        case NODE_IDENT: {
            Symbol *sym = scope_lookup(sa->current_scope, expr->name);
            if (!sym) {
                eh_add_error(sa->errors, "Undefined variable '%s' at line %d:%d",
                             expr->name, expr->line, expr->column);
                strcpy(result, "i");
            } else {
                strcpy(result, sym->type);
            }
            break;
        }
        case NODE_BINARY_OP: {
            char *left_type = semantic_analyze_expr(sa, expr->children[0]);
            char *right_type = semantic_analyze_expr(sa, expr->children[1]);
            
            if (strcmp(expr->value, "+") == 0 || strcmp(expr->value, "-") == 0 ||
                strcmp(expr->value, "*") == 0 || strcmp(expr->value, "/") == 0 ||
                strcmp(expr->value, "%") == 0) {
                
                if (!type_is_numeric(left_type) || !type_is_numeric(right_type)) {
                    eh_add_error(sa->errors, "Arithmetic operator requires numeric types at line %d:%d",
                                 expr->line, expr->column);
                    strcpy(result, "i");
                } else if (!types_equal(left_type, right_type)) {
                    eh_add_error(sa->errors, "Type mismatch in arithmetic: %s vs %s at line %d:%d",
                                 left_type, right_type, expr->line, expr->column);
                    strcpy(result, left_type);
                } else {
                    strcpy(result, left_type);
                }
            } else if (strcmp(expr->value, "==") == 0 || strcmp(expr->value, "!=") == 0 ||
                       strcmp(expr->value, "<") == 0 || strcmp(expr->value, ">") == 0 ||
                       strcmp(expr->value, "<=") == 0 || strcmp(expr->value, ">=") == 0) {
                
                if (!type_is_comparable(left_type) || !type_is_comparable(right_type)) {
                    eh_add_error(sa->errors, "Comparison requires comparable types at line %d:%d",
                                 expr->line, expr->column);
                }
                strcpy(result, "b");
            } else if (strcmp(expr->value, "&&") == 0 || strcmp(expr->value, "||") == 0) {
                if (!types_equal(left_type, "b") || !types_equal(right_type, "b")) {
                    eh_add_error(sa->errors, "Logical operator requires bool operands at line %d:%d",
                                 expr->line, expr->column);
                }
                strcpy(result, "b");
            } else if (strcmp(expr->value, "=") == 0) {
                if (!types_equal(left_type, right_type)) {
                    eh_add_error(sa->errors, "Assignment type mismatch: %s vs %s at line %d:%d",
                                 left_type, right_type, expr->line, expr->column);
                }
                strcpy(result, left_type);
            }
            
            free(left_type);
            free(right_type);
            break;
        }
        case NODE_UNARY_OP: {
            char *operand_type = semantic_analyze_expr(sa, expr->children[0]);
            
            if (strcmp(expr->value, "!") == 0) {
                if (!types_equal(operand_type, "b")) {
                    eh_add_error(sa->errors, "NOT operator requires bool operand at line %d:%d",
                                 expr->line, expr->column);
                }
                strcpy(result, "b");
            } else if (strcmp(expr->value, "-") == 0) {
                if (!type_is_numeric(operand_type)) {
                    eh_add_error(sa->errors, "Unary minus requires numeric type at line %d:%d",
                                 expr->line, expr->column);
                }
                strcpy(result, operand_type);
            }
            
            free(operand_type);
            break;
        }
        case NODE_CALL: {
            Symbol *func_sym = NULL;
            if (expr->children[0]->type == NODE_IDENT) {
                func_sym = scope_lookup(sa->current_scope, expr->children[0]->name);
            }
            
            if (!func_sym || !func_sym->is_function) {
                eh_add_error(sa->errors, "Undefined function '%s' at line %d:%d",
                             expr->children[0]->name, expr->line, expr->column);
                strcpy(result, "i");
            } else {
                strcpy(result, func_sym->type);
            }
            break;
        }
        case NODE_ADDRESSOF:
            semantic_analyze_expr(sa, expr->children[0]);
            strcpy(result, "ptr");
            break;
        case NODE_DEREF: {
            char *ptr_type = semantic_analyze_expr(sa, expr->children[0]);
            if (!types_equal(ptr_type, "ptr")) {
                eh_add_error(sa->errors, "Dereference requires pointer type at line %d:%d",
                             expr->line, expr->column);
            }
            strcpy(result, "i");
            free(ptr_type);
            break;
        }
        case NODE_INDEX:
            semantic_analyze_expr(sa, expr->children[0]);
            semantic_analyze_expr(sa, expr->children[1]);
            strcpy(result, "i");
            break;
        case NODE_MEMBER:
            semantic_analyze_expr(sa, expr->children[0]);
            strcpy(result, "i");
            break;
        default:
            strcpy(result, "i");
            break;
    }
    
    return result;
}

void semantic_analyze_node(SemanticAnalyzer *sa, ASTNode *node) {
    if (!node) return;
    
    switch (node->type) {
        case NODE_FUNCTION: {
            char *prev_func = sa->current_function;
            char *prev_ret = sa->current_function_return_type;
            sa->current_function = node->name;
            sa->current_function_return_type = node->type_name;
            
            Scope *func_scope = scope_create(sa->global_scope);
            Scope *prev_scope = sa->current_scope;
            sa->current_scope = func_scope;
            sa->stack_offset = 0;
            
            int param_offset = 16;
            
            for (size_t i = 0; i < node->child_count - 1; i++) {
                ASTNode *param = node->children[i];
                if (param->type == NODE_VAR_DECL) {
                    scope_define(sa->current_scope, param->name, param->type_name, param_offset, sa->errors);
                    param_offset += 8;
                }
            }
            
            if (node->child_count > 0) {
                semantic_analyze_block(sa, node->children[node->child_count - 1]);
            }
            
            scope_free(sa->current_scope);
            sa->current_scope = prev_scope;
            sa->current_function = prev_func;
            sa->current_function_return_type = prev_ret;
            break;
        }
        case NODE_STRUCT: {
            break;
        }
        case NODE_ENUM: {
            break;
        }
        case NODE_VAR_DECL: {
            if (!type_is_valid(node->type_name)) {
                eh_add_error(sa->errors, "Invalid type '%s' at line %d:%d",
                             node->type_name, node->line, node->column);
            }
            
            if (node->child_count > 0) {
                char *expr_type = semantic_analyze_expr(sa, node->children[0]);
                if (!types_equal(expr_type, node->type_name)) {
                    eh_add_error(sa->errors, "Type mismatch in variable declaration: expected %s, got %s at line %d:%d",
                                 node->type_name, expr_type, node->line, node->column);
                }
                free(expr_type);
            }
            
            sa->stack_offset += 8;
            scope_define(sa->current_scope, node->name, node->type_name, sa->stack_offset, sa->errors);
            break;
        }
        case NODE_VAR_ASSIGN: {
            semantic_analyze_expr(sa, node);
            break;
        }
        case NODE_RETURN: {
            if (node->child_count > 0) {
                char *ret_type = semantic_analyze_expr(sa, node->children[0]);
                if (sa->current_function_return_type && 
                    !types_equal(ret_type, sa->current_function_return_type)) {
                    eh_add_error(sa->errors, "Return type mismatch: expected %s, got %s at line %d:%d",
                                 sa->current_function_return_type, ret_type, node->line, node->column);
                }
                free(ret_type);
            }
            break;
        }
        case NODE_IF: {
            if (node->child_count > 0) {
                char *cond_type = semantic_analyze_expr(sa, node->children[0]);
                if (!types_equal(cond_type, "b")) {
                    eh_add_error(sa->errors, "If condition must be bool, got %s at line %d:%d",
                                 cond_type, node->line, node->column);
                }
                free(cond_type);
            }
            if (node->child_count > 1) {
                semantic_analyze_block(sa, node->children[1]);
            }
            if (node->child_count > 2) {
                semantic_analyze_block(sa, node->children[2]);
            }
            break;
        }
        case NODE_LOOP: {
            if (node->child_count == 2) {
                char *cond_type = semantic_analyze_expr(sa, node->children[0]);
                if (!types_equal(cond_type, "b")) {
                    eh_add_error(sa->errors, "Loop condition must be bool at line %d:%d",
                                 node->line, node->column);
                }
                free(cond_type);
                semantic_analyze_block(sa, node->children[1]);
            } else if (node->child_count == 1) {
                semantic_analyze_block(sa, node->children[0]);
            }
            break;
        }
        case NODE_MATCH: {
            if (node->child_count >= 1) {
                semantic_analyze_expr(sa, node->children[0]);
            }
            for (size_t i = 1; i < node->child_count; i += 2) {
                semantic_analyze_expr(sa, node->children[i]);
                if (i + 1 < node->child_count) {
                    semantic_analyze_expr(sa, node->children[i + 1]);
                }
            }
            break;
        }
        case NODE_BLOCK: {
            semantic_analyze_block(sa, node);
            break;
        }
        case NODE_EXPR_STMT: {
            if (node->child_count > 0) {
                semantic_analyze_expr(sa, node->children[0]);
            }
            break;
        }
        default:
            break;
    }
}

void semantic_analyze(SemanticAnalyzer *sa, ASTNode *program) {
    if (!program || program->type != NODE_PROGRAM) return;
    
    // First pass: collect all top-level definitions
    for (size_t i = 0; i < program->child_count; i++) {
        ASTNode *item = program->children[i];
        if (item->type == NODE_FUNCTION) {
            scope_define(sa->global_scope, item->name, item->type_name, 0, sa->errors);
            Symbol *sym = scope_lookup(sa->global_scope, item->name);
            if (sym) sym->is_function = 1;
        } else if (item->type == NODE_STRUCT) {
            scope_define(sa->global_scope, item->name, "struct", 0, sa->errors);
            Symbol *sym = scope_lookup(sa->global_scope, item->name);
            if (sym) sym->is_struct = 1;
        } else if (item->type == NODE_ENUM) {
            scope_define(sa->global_scope, item->name, "enum", 0, sa->errors);
            Symbol *sym = scope_lookup(sa->global_scope, item->name);
            if (sym) sym->is_enum = 1;
        }
    }
    
    // Second pass: analyze bodies
    for (size_t i = 0; i < program->child_count; i++) {
        semantic_analyze_node(sa, program->children[i]);
    }
}

void semantic_analyzer_free(SemanticAnalyzer *sa) {
    scope_free(sa->global_scope);
    if (sa->current_function) free(sa->current_function);
    if (sa->current_function_return_type) free(sa->current_function_return_type);
    free(sa);
}
