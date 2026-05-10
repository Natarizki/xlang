#ifndef XLANG_H
#define XLANG_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

typedef enum { ARCH_X86_64, ARCH_AARCH64, ARCH_UNKNOWN } Arch;
Arch detect_arch();

typedef enum {
    TOK_EOF, TOK_NEWLINE,
    TOK_INT, TOK_FLOAT, TOK_STRING,
    TOK_IDENT,
    TOK_IF, TOK_OR, TOK_FOR, TOK_WHILE, TOK_REPEAT,
    TOK_MAKE, TOK_GIVE, TOK_MAYBE, TOK_NOPE,
    TOK_IMPORT, TOK_AS, TOK_IN, TOK_CLASS,
    TOK_YES, TOK_NO, TOK_NULL, TOK_BREAK, TOK_SKIP,
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_POWER, TOK_EQ, TOK_NEQ, TOK_LT, TOK_GT, TOK_LTE, TOK_GTE,
    TOK_ASSIGN, TOK_PLUS_EQ, TOK_MINUS_EQ, TOK_STAR_EQ, TOK_SLASH_EQ,
    TOK_LPAREN, TOK_RPAREN, TOK_LBRACK, TOK_RBRACK,
    TOK_LBRACE, TOK_RBRACE, TOK_COMMA, TOK_DOT, TOK_COLON,
} TokenType;

typedef struct {
    TokenType type;
    char     *val;
    int       len;
    int       line;
} Token;

typedef struct {
    Token *tokens;
    int    count, cap;
} TokenList;

typedef enum {
    NODE_PROGRAM, NODE_ASSIGN, NODE_BINOP, NODE_UNOP,
    NODE_INT, NODE_FLOAT, NODE_STRING, NODE_BOOL, NODE_NULL,
    NODE_IDENT, NODE_PRINT, NODE_INPUT,
    NODE_IF, NODE_FOR, NODE_WHILE, NODE_REPEAT,
    NODE_BREAK, NODE_SKIP, NODE_FUNCTION, NODE_CALL,
    NODE_RETURN, NODE_MAYBE, NODE_IMPORT, NODE_CLASS,
    NODE_LIST, NODE_MEMBER,
} NodeType;

typedef struct ASTNode {
    NodeType        type;
    char           *val;
    int             val_len;
    struct ASTNode **children;
    int             nchild;
    int             line;
} ASTNode;

typedef enum { VAR_INT, VAR_FLOAT, VAR_STR, VAR_BOOL, VAR_NULL } VarType;
typedef struct {
    char    name[64];
    VarType type;
    long    ival;
    double  fval;
    char   *sval;
    int     slen;
} Var;

#define MAX_VARS    1024
#define MAX_STRINGS 4096

typedef struct { char *ptr; int len; } StrEntry;

extern Var      vars[MAX_VARS];
extern int      var_count;
extern StrEntry str_pool[MAX_STRINGS];
extern int      str_count;

int      var_find(const char *name);
void     var_set_int(const char *name, long val);
void     var_set_float(const char *name, double val);
void     var_set_str(const char *name, const char *val, int len);
int      pool_add(const char *ptr, int len);

TokenList *lex(const char *src, int len);
void       lex_free(TokenList *tl);
ASTNode   *parse(TokenList *tl);
void       ast_free(ASTNode *n);
int        codegen(ASTNode *ast, const char *outname, Arch arch);
#endif
