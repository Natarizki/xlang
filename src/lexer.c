#include "xlang.h"

static struct { const char *kw; TokenType t; } kws[] = {
    {"if",TOK_IF},{"or",TOK_OR},{"for",TOK_FOR},{"while",TOK_WHILE},
    {"repeat",TOK_REPEAT},{"make",TOK_MAKE},{"give",TOK_GIVE},
    {"maybe",TOK_MAYBE},{"nope",TOK_NOPE},{"import",TOK_IMPORT},
    {"as",TOK_AS},{"in",TOK_IN},{"class",TOK_CLASS},
    {"yes",TOK_YES},{"no",TOK_NO},{"null",TOK_NULL},
    {"break",TOK_BREAK},{"skip",TOK_SKIP},{NULL,0}
};

static TokenList *tl_new() {
    TokenList *t = malloc(sizeof(*t));
    t->cap = 256; t->count = 0;
    t->tokens = malloc(sizeof(Token)*t->cap);
    return t;
}
static void tl_push(TokenList *tl, TokenType type, const char *val, int len, int line) {
    if (tl->count >= tl->cap) {
        tl->cap *= 2;
        tl->tokens = realloc(tl->tokens, sizeof(Token)*tl->cap);
    }
    tl->tokens[tl->count++] = (Token){type, val?strndup(val,len):NULL, len, line};
}
static TokenType kw_check(const char *s, int len) {
    for (int i = 0; kws[i].kw; i++)
        if ((int)strlen(kws[i].kw)==len && memcmp(kws[i].kw,s,len)==0)
            return kws[i].t;
    return TOK_IDENT;
}

TokenList *lex(const char *src, int n) {
    TokenList *tl = tl_new();
    int i=0, line=1;
    while (i < n) {
        char c = src[i];
        // comment
        if (c=='/' && i+1<n && src[i+1]=='/') {
            while (i<n && src[i]!='\n') i++; continue;
        }
        if (c=='\n') { tl_push(tl,TOK_NEWLINE,"\n",1,line); line++; i++; continue; }
        if (c==' '||c=='\t'||c=='\r') { i++; continue; }
        // string
        if (c=='"') {
            int s=++i;
            while (i<n && src[i]!='"') i++;
            tl_push(tl, TOK_STRING, src+s, i-s, line);
            i++; continue;
        }
        // number
        if (isdigit(c)) {
            int s=i; int is_f=0;
            while (i<n && (isdigit(src[i])||src[i]=='.')) { if(src[i]=='.') is_f=1; i++; }
            tl_push(tl, is_f?TOK_FLOAT:TOK_INT, src+s, i-s, line);
            continue;
        }
        // ident/keyword
        if (isalpha(c)||c=='_') {
            int s=i;
            while (i<n && (isalnum(src[i])||src[i]=='_')) i++;
            TokenType t = kw_check(src+s, i-s);
            // special: print/input as idents initially
            tl_push(tl, t, src+s, i-s, line);
            continue;
        }
        // operators
        i++;
        switch(c) {
            case '+': if(i<n&&src[i]=='='){tl_push(tl,TOK_PLUS_EQ,"+=",2,line);i++;}else tl_push(tl,TOK_PLUS,"+",1,line); break;
            case '-': if(i<n&&src[i]=='='){tl_push(tl,TOK_MINUS_EQ,"-=",2,line);i++;}else tl_push(tl,TOK_MINUS,"-",1,line); break;
            case '*': if(i<n&&src[i]=='*'){tl_push(tl,TOK_POWER,"**",2,line);i++;}
                      else if(i<n&&src[i]=='='){tl_push(tl,TOK_STAR_EQ,"*=",2,line);i++;}
                      else tl_push(tl,TOK_STAR,"*",1,line); break;
            case '/': if(i<n&&src[i]=='='){tl_push(tl,TOK_SLASH_EQ,"/=",2,line);i++;}else tl_push(tl,TOK_SLASH,"/",1,line); break;
            case '%': tl_push(tl,TOK_PERCENT,"%",1,line); break;
            case '=': if(i<n&&src[i]=='='){tl_push(tl,TOK_EQ,"==",2,line);i++;}else tl_push(tl,TOK_ASSIGN,"=",1,line); break;
            case '!': if(i<n&&src[i]=='='){tl_push(tl,TOK_NEQ,"!=",2,line);i++;}break;
            case '<': if(i<n&&src[i]=='='){tl_push(tl,TOK_LTE,"<=",2,line);i++;}else tl_push(tl,TOK_LT,"<",1,line); break;
            case '>': if(i<n&&src[i]=='='){tl_push(tl,TOK_GTE,">=",2,line);i++;}else tl_push(tl,TOK_GT,">",1,line); break;
            case '(': tl_push(tl,TOK_LPAREN,"(",1,line); break;
            case ')': tl_push(tl,TOK_RPAREN,")",1,line); break;
            case '[': tl_push(tl,TOK_LBRACK,"[",1,line); break;
            case ']': tl_push(tl,TOK_RBRACK,"]",1,line); break;
            case '{': tl_push(tl,TOK_LBRACE,"{",1,line); break;
            case '}': tl_push(tl,TOK_RBRACE,"}",1,line); break;
            case ',': tl_push(tl,TOK_COMMA,",",1,line); break;
            case '.': tl_push(tl,TOK_DOT,".",1,line); break;
            case ':': tl_push(tl,TOK_COLON,":",1,line); break;
        }
    }
    tl_push(tl, TOK_EOF, NULL, 0, line);
    return tl;
}

void lex_free(TokenList *tl) {
    for (int i=0; i<tl->count; i++) free(tl->tokens[i].val);
    free(tl->tokens); free(tl);
}
