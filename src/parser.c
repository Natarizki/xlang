#include "xlang.h"

static TokenList *tl;
static int pos;

static ASTNode *node_new(NodeType t, const char *val, int len, int line) {
    ASTNode *n = calloc(1, sizeof(*n));
    n->type = t; n->val = val?strndup(val,len):NULL;
    n->val_len = len; n->line = line;
    return n;
}
static void node_add(ASTNode *p, ASTNode *c) {
    p->children = realloc(p->children, sizeof(ASTNode*)*(p->nchild+1));
    p->children[p->nchild++] = c;
}
void ast_free(ASTNode *n) {
    if (!n) return;
    for (int i=0; i<n->nchild; i++) ast_free(n->children[i]);
    free(n->children); free(n->val); free(n);
}

static Token *peek()    { return &tl->tokens[pos]; }
static Token *advance() { Token *t=&tl->tokens[pos]; if(t->type!=TOK_EOF) pos++; return t; }
static void skip_nl()   { while(peek()->type==TOK_NEWLINE) advance(); }
static int  check(TokenType t) { return peek()->type == t; }
static Token *expect(TokenType t) {
    if (!check(t)) { fprintf(stderr,"Parse error line %d: expected %d got %d ('%s')\n",
        peek()->line, t, peek()->type, peek()->val?peek()->val:"?"); }
    return advance();
}

static ASTNode *parse_expr();

static ASTNode *parse_primary() {
    Token *t = peek();
    if (t->type == TOK_STRING) { advance(); return node_new(NODE_STRING, t->val, t->len, t->line); }
    if (t->type == TOK_INT)    { advance(); return node_new(NODE_INT,    t->val, t->len, t->line); }
    if (t->type == TOK_FLOAT)  { advance(); return node_new(NODE_FLOAT,  t->val, t->len, t->line); }
    if (t->type == TOK_YES)    { advance(); return node_new(NODE_BOOL,   "1", 1, t->line); }
    if (t->type == TOK_NO)     { advance(); return node_new(NODE_BOOL,   "0", 1, t->line); }
    if (t->type == TOK_NULL)   { advance(); return node_new(NODE_NULL,   "null", 4, t->line); }
    if (t->type == TOK_IDENT) {
        advance();
        // function call
        if (check(TOK_LPAREN)) {
            advance();
            ASTNode *call = node_new(NODE_CALL, t->val, t->len, t->line);
            while (!check(TOK_RPAREN) && !check(TOK_EOF)) {
                node_add(call, parse_expr());
                if (check(TOK_COMMA)) advance();
            }
            expect(TOK_RPAREN);
            return call;
        }
        return node_new(NODE_IDENT, t->val, t->len, t->line);
    }
    // fallback
    advance();
    return node_new(NODE_NULL, "null", 4, t->line);
}

static int is_binop(TokenType t) {
    return t==TOK_PLUS||t==TOK_MINUS||t==TOK_STAR||t==TOK_SLASH||
           t==TOK_PERCENT||t==TOK_POWER||t==TOK_EQ||t==TOK_NEQ||
           t==TOK_LT||t==TOK_GT||t==TOK_LTE||t==TOK_GTE;
}

static ASTNode *parse_expr() {
    ASTNode *left = parse_primary();
    while (is_binop(peek()->type)) {
        Token *op = advance();
        ASTNode *b = node_new(NODE_BINOP, op->val, op->len, op->line);
        node_add(b, left);
        node_add(b, parse_primary());
        left = b;
    }
    return left;
}

// parse indented block (one or more stmts)
static ASTNode *parse_indented_block();
static ASTNode *parse_stmt();

static ASTNode *parse_stmt() {
    skip_nl();
    Token *t = peek();

    // print(...)
    if (t->type == TOK_IDENT && t->val && strcmp(t->val,"print")==0) {
        advance(); expect(TOK_LPAREN);
        ASTNode *p = node_new(NODE_PRINT, "print", 5, t->line);
        while (!check(TOK_RPAREN) && !check(TOK_EOF)) {
            node_add(p, parse_expr());
            if (check(TOK_COMMA)) advance();
        }
        expect(TOK_RPAREN);
        return p;
    }
    // input(...)
    if (t->type == TOK_IDENT && t->val && strcmp(t->val,"input")==0) {
        advance(); expect(TOK_LPAREN);
        ASTNode *inp = node_new(NODE_INPUT, "input", 5, t->line);
        if (!check(TOK_RPAREN)) node_add(inp, parse_expr());
        expect(TOK_RPAREN);
        return inp;
    }
    // if
    if (t->type == TOK_IF) {
        advance();
        ASTNode *ifn = node_new(NODE_IF, "if", 2, t->line);
        node_add(ifn, parse_expr()); // condition
        skip_nl();
        node_add(ifn, parse_indented_block()); // body
        // or / or if
        while (peek()->type == TOK_OR) {
            advance();
            if (peek()->type == TOK_IF) {
                advance();
                node_add(ifn, parse_expr());
                skip_nl();
                node_add(ifn, parse_indented_block());
            } else {
                skip_nl();
                node_add(ifn, parse_indented_block()); // else body
                break;
            }
        }
        return ifn;
    }
    // while
    if (t->type == TOK_WHILE) {
        advance();
        ASTNode *w = node_new(NODE_WHILE, "while", 5, t->line);
        node_add(w, parse_expr());
        skip_nl();
        node_add(w, parse_indented_block());
        return w;
    }
    // repeat N
    if (t->type == TOK_REPEAT) {
        advance();
        ASTNode *r = node_new(NODE_REPEAT, "repeat", 6, t->line);
        node_add(r, parse_expr());
        skip_nl();
        node_add(r, parse_indented_block());
        return r;
    }
    // for item in list
    if (t->type == TOK_FOR) {
        advance();
        ASTNode *f = node_new(NODE_FOR, "for", 3, t->line);
        node_add(f, parse_expr()); // item var
        if (check(TOK_IN)) advance();
        node_add(f, parse_expr()); // iterable
        skip_nl();
        node_add(f, parse_indented_block());
        return f;
    }
    // make fn
    if (t->type == TOK_MAKE) {
        advance();
        Token *name = peek(); advance();
        ASTNode *fn = node_new(NODE_FUNCTION, name->val, name->len, t->line);
        expect(TOK_LPAREN);
        while (!check(TOK_RPAREN) && !check(TOK_EOF)) {
            Token *param = advance();
            node_add(fn, node_new(NODE_IDENT, param->val, param->len, param->line));
            if (check(TOK_COMMA)) advance();
        }
        expect(TOK_RPAREN);
        skip_nl();
        node_add(fn, parse_indented_block());
        return fn;
    }
    // give (return)
    if (t->type == TOK_GIVE) {
        advance();
        ASTNode *ret = node_new(NODE_RETURN, "give", 4, t->line);
        node_add(ret, parse_expr());
        return ret;
    }
    // maybe/nope
    if (t->type == TOK_MAYBE) {
        advance();
        ASTNode *m = node_new(NODE_MAYBE, "maybe", 5, t->line);
        skip_nl();
        node_add(m, parse_indented_block());
        if (check(TOK_NOPE)) {
            advance(); skip_nl();
            node_add(m, parse_indented_block());
        }
        return m;
    }
    // break/skip
    if (t->type == TOK_BREAK) { advance(); return node_new(NODE_BREAK, "break", 5, t->line); }
    if (t->type == TOK_SKIP)  { advance(); return node_new(NODE_SKIP,  "skip",  4, t->line); }
    // import
    if (t->type == TOK_IMPORT) {
        advance();
        Token *mod = advance();
        return node_new(NODE_IMPORT, mod->val, mod->len, t->line);
    }
    // assignment or expr
    if (t->type == TOK_IDENT) {
        advance();
        // += -= *= /=
        if (check(TOK_PLUS_EQ)||check(TOK_MINUS_EQ)||check(TOK_STAR_EQ)||check(TOK_SLASH_EQ)) {
            Token *op = advance();
            ASTNode *a = node_new(NODE_ASSIGN, t->val, t->len, t->line);
            // expand: name op= expr  →  name = name op expr
            ASTNode *b = node_new(NODE_BINOP, op->val, op->len, op->line);
            node_add(b, node_new(NODE_IDENT, t->val, t->len, t->line));
            node_add(b, parse_expr());
            node_add(a, b);
            return a;
        }
        if (check(TOK_ASSIGN)) {
            advance();
            ASTNode *a = node_new(NODE_ASSIGN, t->val, t->len, t->line);
            node_add(a, parse_expr());
            return a;
        }
        // just ident (function call already handled in primary)
        return node_new(NODE_IDENT, t->val, t->len, t->line);
    }
    advance();
    return NULL;
}

static int is_block_end(TokenType t) {
    return t==TOK_EOF || t==TOK_OR || t==TOK_NOPE;
}

static ASTNode *parse_indented_block() {
    ASTNode *blk = node_new(NODE_PROGRAM, "block", 5, peek()->line);
    skip_nl();
    // Parse multiple statements (indented block)
    // Stops at: EOF, or/nope keywords, or back to top-level
    // Simple heuristic: read all consecutive indented stmts
    int count = 0;
    while (!is_block_end(peek()->type) && peek()->type != TOK_EOF) {
        ASTNode *s = parse_stmt();
        if (s) { node_add(blk, s); count++; }
        // Skip newlines between stmts in block
        skip_nl();
        // Stop if we hit a block-ending token
        if (is_block_end(peek()->type)) break;
        // Stop after reasonable block size to avoid eating outer stmts
        // Simple: only read stmts that are "indented" (heuristic: up to 8)
        if (count >= 8) break;
    }
    return blk;
}

ASTNode *parse(TokenList *tokens) {
    tl = tokens; pos = 0;
    ASTNode *prog = node_new(NODE_PROGRAM, "program", 7, 0);
    while (!check(TOK_EOF)) {
        skip_nl();
        if (check(TOK_EOF)) break;
        ASTNode *s = parse_stmt();
        if (s) node_add(prog, s);
    }
    return prog;
}
