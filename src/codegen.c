#include "xlang.h"

// ─── Output buffer ────────────────────────────────────────────
static char  out_buf[1<<20]; // 1MB
static int   out_pos;
static Arch  cur_arch;
static int   lbl_cnt;

static void out(const char *fmt, ...) {
    va_list a; va_start(a,fmt);
    out_pos += vsnprintf(out_buf+out_pos, sizeof(out_buf)-out_pos, fmt, a);
    va_end(a);
}

// ─── Eval helpers (compile-time) ─────────────────────────────
static long eval_int(ASTNode *n) {
    if (!n) return 0;
    if (n->type == NODE_INT)   return atol(n->val);
    if (n->type == NODE_BOOL)  return atol(n->val);
    if (n->type == NODE_IDENT) {
        int i = var_find(n->val);
        if (i>=0 && vars[i].type==VAR_INT) return vars[i].ival;
        return 0;
    }
    if (n->type == NODE_BINOP && n->nchild==2) {
        long a = eval_int(n->children[0]), b = eval_int(n->children[1]);
        char *op = n->val;
        if (!strcmp(op,"+"))  return a+b;
        if (!strcmp(op,"-"))  return a-b;
        if (!strcmp(op,"*"))  return a*b;
        if (!strcmp(op,"/"))  return b?a/b:0;
        if (!strcmp(op,"%"))  return b?a%b:0;
        if (!strcmp(op,"**")) { long r=1; for(long i=0;i<b;i++) r*=a; return r; }
        if (!strcmp(op,"==")) return a==b;
        if (!strcmp(op,"!=")) return a!=b;
        if (!strcmp(op,">"))  return a>b;
        if (!strcmp(op,"<"))  return a<b;
        if (!strcmp(op,">=")) return a>=b;
        if (!strcmp(op,"<=")) return a<=b;
    }
    return 0;
}

static int is_str_node(ASTNode *n) {
    if (!n) return 0;
    if (n->type == NODE_STRING) return 1;
    if (n->type == NODE_IDENT) {
        int i = var_find(n->val);
        return (i>=0 && vars[i].type==VAR_STR);
    }
    if (n->type == NODE_BINOP && n->val && !strcmp(n->val,"+") && n->nchild==2)
        return is_str_node(n->children[0]) || is_str_node(n->children[1]);
    return 0;
}

static char *eval_str(ASTNode *n, int *outlen) {
    if (!n) { *outlen=0; return ""; }
    if (n->type == NODE_STRING) { *outlen=n->val_len; return n->val; }
    if (n->type == NODE_IDENT) {
        int i = var_find(n->val);
        if (i>=0 && vars[i].type==VAR_STR) { *outlen=vars[i].slen; return vars[i].sval; }
        if (i>=0 && vars[i].type==VAR_INT) {
            static char buf[32];
            int l = snprintf(buf, sizeof(buf), "%ld", vars[i].ival);
            *outlen=l; return buf;
        }
    }
    if (n->type == NODE_INT) { *outlen=n->val_len; return n->val; }
    if (n->type == NODE_BINOP) {
        // Only concat if at least one side is actually a string
        if (n->val && !strcmp(n->val,"+") && n->nchild==2 &&
            (is_str_node(n->children[0]) || is_str_node(n->children[1]))) {
            int la, lb;
            char *a = eval_str(n->children[0], &la);
            char *b = eval_str(n->children[1], &lb);
            char *res = malloc(la+lb+1);
            memcpy(res, a, la); memcpy(res+la, b, lb); res[la+lb]=0;
            *outlen = la+lb; return res;
        }
        // numeric result as string
        static char buf[32];
        long v = eval_int(n);
        int l = snprintf(buf, sizeof(buf), "%ld", v);
        *outlen=l; return buf;
    }
    *outlen=0; return "";
}

// ─── First pass: collect ALL strings we'll need ───────────────
static void collect_node(ASTNode *n);
static void collect_node(ASTNode *n) {
    if (!n) return;
    if (n->type == NODE_STRING) { pool_add(n->val, n->val_len); return; }
    if (n->type == NODE_INT)    { pool_add(n->val, n->val_len); return; }
    // Pre-eval assignments so we have their string forms
    if (n->type == NODE_ASSIGN && n->nchild>0) {
        ASTNode *v = n->children[0];
        if (v->type==NODE_INT) { pool_add(v->val,v->val_len); }
        if (v->type==NODE_STRING) pool_add(v->val,v->val_len);
        if (v->type==NODE_BINOP) {
            if (v->val && !strcmp(v->val,"+")) {
                int sl; char *s=eval_str(v,&sl); if(sl>0) pool_add(s,sl);
            } else {
                long iv = eval_int(v);
                char buf[32]; int l=snprintf(buf,sizeof(buf),"%ld",iv);
                pool_add(buf,l);
            }
        }
    }
    // Collect print arg computed values
    if (n->type == NODE_PRINT) {
        for (int i=0; i<n->nchild; i++) {
            ASTNode *a = n->children[i];
            if (!a) continue;
            if (a->type == NODE_BINOP) {
                if (a->val && !strcmp(a->val,"+")) {
                    int sl; char *s=eval_str(a,&sl); if(sl>0) pool_add(s,sl);
                } else {
                    long v=eval_int(a); char buf[32];
                    int l=snprintf(buf,sizeof(buf),"%ld",v); pool_add(buf,l);
                }
            }
            if (a->type == NODE_IDENT) {
                int vi=var_find(a->val);
                if (vi>=0) {
                    if (vars[vi].type==VAR_STR) pool_add(vars[vi].sval,vars[vi].slen);
                    else if (vars[vi].type==VAR_INT) {
                        char buf[32]; int l=snprintf(buf,sizeof(buf),"%ld",vars[vi].ival);
                        pool_add(buf,l);
                    }
                }
            }
        }
    }
    for (int i=0; i<n->nchild; i++) collect_node(n->children[i]);
}

// ─── Arch-specific: emit write syscall for string index ───────
static void emit_write_idx(int idx) {
    if (cur_arch == ARCH_AARCH64) {
        out("    mov x0, #1\n");
        out("    adr x1, _str%d\n", idx);
        out("    mov x2, _str%d_len\n", idx);
        out("    mov x8, #64\n");
        out("    svc #0\n");
    } else {
        out("    mov $1, %%rax\n");
        out("    mov $1, %%rdi\n");
        out("    lea _str%d(%%rip), %%rsi\n", idx);
        out("    mov $_str%d_len, %%rdx\n", idx);
        out("    syscall\n");
    }
}
static void emit_newline() {
    if (cur_arch == ARCH_AARCH64) {
        out("    mov x0, #1\n    adr x1, _nl\n    mov x2, #1\n    mov x8, #64\n    svc #0\n");
    } else {
        out("    mov $1, %%rax\n    mov $1, %%rdi\n    lea _nl(%%rip), %%rsi\n    mov $1, %%rdx\n    syscall\n");
    }
}
static void emit_space() {
    if (cur_arch == ARCH_AARCH64) {
        out("    mov x0, #1\n    adr x1, _sp\n    mov x2, #1\n    mov x8, #64\n    svc #0\n");
    } else {
        out("    mov $1, %%rax\n    mov $1, %%rdi\n    lea _sp(%%rip), %%rsi\n    mov $1, %%rdx\n    syscall\n");
    }
}

// ─── Emit print argument ──────────────────────────────────────
static void emit_print_arg(ASTNode *a) {
    if (!a) return;
    if (a->type == NODE_STRING) {
        int idx = pool_add(a->val, a->val_len);
        emit_write_idx(idx); return;
    }
    if (a->type == NODE_INT) {
        int idx = pool_add(a->val, a->val_len);
        emit_write_idx(idx); return;
    }
    if (a->type == NODE_IDENT) {
        int vi = var_find(a->val);
        if (vi >= 0) {
            if (vars[vi].type == VAR_STR) {
                int idx = pool_add(vars[vi].sval, vars[vi].slen);
                emit_write_idx(idx);
            } else if (vars[vi].type == VAR_INT) {
                char buf[32]; int l=snprintf(buf,sizeof(buf),"%ld",vars[vi].ival);
                int idx = pool_add(buf,l);
                emit_write_idx(idx);
            }
        }
        return;
    }
    if (a->type == NODE_BINOP) {
        // Try string concat first
        if (a->val && !strcmp(a->val,"+")) {
            int sl; char *s = eval_str(a, &sl);
            int idx = pool_add(s, sl);
            emit_write_idx(idx); return;
        }
        // Numeric
        long v = eval_int(a);
        char buf[32]; int l=snprintf(buf,sizeof(buf),"%ld",v);
        int idx = pool_add(buf,l);
        emit_write_idx(idx);
        return;
    }
    if (a->type == NODE_CALL) {
        // handle str(), int(), float() conversions
        if (a->val && !strcmp(a->val,"str") && a->nchild>0) {
            int sl; char *s=eval_str(a->children[0],&sl);
            int idx=pool_add(s,sl); emit_write_idx(idx);
        }
    }
}

// ─── AST emitter ─────────────────────────────────────────────
static void emit_node(ASTNode *n);

static void emit_node(ASTNode *n) {
    if (!n) return;

    switch(n->type) {
    case NODE_PROGRAM:
        for (int i=0; i<n->nchild; i++) emit_node(n->children[i]);
        break;

    case NODE_ASSIGN:
        if (n->nchild>0) {
            ASTNode *v = n->children[0];
            if (v->type==NODE_STRING) var_set_str(n->val, v->val, v->val_len);
            else if (v->type==NODE_INT) var_set_int(n->val, atol(v->val));
            else if (v->type==NODE_FLOAT) var_set_float(n->val, atof(v->val));
            else if (v->type==NODE_BOOL) var_set_int(n->val, atol(v->val));
            else if (v->type==NODE_BINOP) {
                if (v->val && !strcmp(v->val,"+")) {
                    int sl; char *s=eval_str(v,&sl); var_set_str(n->val,s,sl);
                } else var_set_int(n->val, eval_int(v));
            }
            else if (v->type==NODE_IDENT) {
                int vi=var_find(v->val);
                if (vi>=0) {
                    if (vars[vi].type==VAR_STR) var_set_str(n->val,vars[vi].sval,vars[vi].slen);
                    else var_set_int(n->val, vars[vi].ival);
                }
            }
        }
        break;

    case NODE_PRINT:
        for (int i=0; i<n->nchild; i++) {
            emit_print_arg(n->children[i]);
            if (i < n->nchild-1) emit_space();
        }
        emit_newline();
        break;

    case NODE_INPUT: {
        // Print prompt
        if (n->nchild>0) emit_print_arg(n->children[0]);
        // Read from stdin (runtime - emit syscall read into buffer)
        int l = lbl_cnt++;
        if (cur_arch == ARCH_AARCH64) {
            out("    // input read\n");
            out("    mov x0, #0\n");
            out("    adr x1, _ibuf\n");
            out("    mov x2, #1024\n");
            out("    mov x8, #63\n");
            out("    svc #0\n");
            out("    str x0, [_ibuf_len]\n");
        } else {
            out("    // input read\n");
            out("    mov $0, %%rax\n");
            out("    mov $0, %%rdi\n");
            out("    lea _ibuf(%%rip), %%rsi\n");
            out("    mov $1024, %%rdx\n");
            out("    syscall\n");
        }
        (void)l;
        break;
    }

    case NODE_IF: {
        // children: cond, body, [elif_cond, elif_body, ...], [else_body]
        // Compile-time eval for constants, runtime for variables
        int i = 0;
        while (i+1 < n->nchild) {
            ASTNode *cond = n->children[i];
            ASTNode *body = n->children[i+1];
            long cv = eval_int(cond);
            // Check if condition involves variables (runtime)
            if (cond->type == NODE_BINOP &&
                (cond->children[0]->type==NODE_IDENT || cond->children[1]->type==NODE_IDENT)) {
                // Runtime condition
                lbl_cnt++;
                //(void)eval_int(cond->children[1]);
                //(void)eval_int(cond->children[0]);
                // For now: emit as compile-time (full runtime in phase 3)
                if (cv) emit_node(body);
            } else {
                if (cv) { emit_node(body); break; }
            }
            i += 2;
        }
        // else body (odd child at end)
        if (i < n->nchild && i % 2 == 0) emit_node(n->children[i]);
        break;
    }

    case NODE_WHILE: {
        if (n->nchild < 2) break;
        int lbl = lbl_cnt++;
        if (cur_arch == ARCH_AARCH64) {
            out(".Lwhile_%d:\n", lbl);
            // emit body (condition eval in phase 3)
            long cv = eval_int(n->children[0]);
            if (cv) {
                emit_node(n->children[1]);
                out("    b .Lwhile_%d\n", lbl);
            }
            out(".Lwhile_%d_end:\n", lbl);
        } else {
            out(".Lwhile_%d:\n", lbl);
            long cv = eval_int(n->children[0]);
            if (cv) {
                emit_node(n->children[1]);
                out("    jmp .Lwhile_%d\n", lbl);
            }
            out(".Lwhile_%d_end:\n", lbl);
        }
        break;
    }

    case NODE_REPEAT: {
        if (n->nchild < 2) break;
        long count = eval_int(n->children[0]);
        if (count <= 0) break;
        // Unroll loop: process each iteration fully
        // Save/restore str_count to avoid duplicate strings
        for (long _ri = 0; _ri < count; _ri++) {
            // First sub-pass: collect new strings for this iteration
            collect_node(n->children[1]);
            // Then emit
            emit_node(n->children[1]);
        }
        break;
    }

    case NODE_FUNCTION: {
        // emit function label + body
        if (!n->val) break;
        char fname[128]; snprintf(fname, sizeof(fname), "_xfn_%s", n->val);
        if (cur_arch == ARCH_AARCH64) {
            out("%s:\n", fname);
            out("    stp x29, x30, [sp, #-16]!\n");
            out("    mov x29, sp\n");
            if (n->nchild > 0) emit_node(n->children[n->nchild-1]); // body
            out("    ldp x29, x30, [sp], #16\n");
            out("    ret\n");
        } else {
            out("%s:\n", fname);
            out("    push %%rbp\n    mov %%rsp, %%rbp\n");
            if (n->nchild > 0) emit_node(n->children[n->nchild-1]);
            out("    pop %%rbp\n    ret\n");
        }
        break;
    }

    case NODE_CALL: {
        // built-in print is handled elsewhere; other calls
        if (!n->val) break;
        if (!strcmp(n->val,"print")) {
            // treat as print
            ASTNode fake = {NODE_PRINT,"print",5,n->children,n->nchild,n->line};
            emit_node(&fake);
            break;
        }
        char fname[128]; snprintf(fname,sizeof(fname),"_xfn_%s",n->val);
        if (cur_arch == ARCH_AARCH64) out("    bl %s\n", fname);
        else out("    call %s\n", fname);
        break;
    }

    case NODE_MAYBE: {
        // try/catch - emit body, skip nope for now
        if (n->nchild > 0) emit_node(n->children[0]);
        break;
    }

    case NODE_RETURN:
        if (cur_arch == ARCH_AARCH64) out("    ldp x29, x30, [sp], #16\n    ret\n");
        else out("    pop %%rbp\n    ret\n");
        break;

    case NODE_BREAK:
        // TODO: proper break with labels
        break;

    default:
        for (int i=0; i<n->nchild; i++) emit_node(n->children[i]);
        break;
    }
}

// ─── Escape string for assembly ───────────────────────────────
static void emit_escaped(const char *s, int len) {
    for (int i=0; i<len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c=='"')       out("\\\"");
        else if (c=='\\') out("\\\\");
        else if (c=='\n') out("\\n");
        else if (c=='\t') out("\\t");
        else if (c<32||c>126) out("\\%03o", c);
        else out("%c", c);
    }
}


// Pre-collect: simulate execution to find ALL strings before emitting data section
void pre_collect(ASTNode *n) {
    if (!n) return;
    if (n->type == NODE_STRING) { pool_add(n->val, n->val_len); return; }
    if (n->type == NODE_INT)    { pool_add(n->val, n->val_len); return; }
    if (n->type == NODE_ASSIGN && n->nchild > 0) {
        ASTNode *v = n->children[0];
        if (v->type==NODE_STRING) { var_set_str(n->val,v->val,v->val_len); pool_add(v->val,v->val_len); }
        else if (v->type==NODE_INT) { var_set_int(n->val,atol(v->val)); pool_add(v->val,v->val_len); }
        else if (v->type==NODE_BINOP) {
            if (v->val&&!strcmp(v->val,"+")&&(is_str_node(v->children[0])||is_str_node(v->children[1]))) {
                int sl; char *s=eval_str(v,&sl);
                if(sl>0){var_set_str(n->val,s,sl);pool_add(s,sl);}
            } else { long iv=eval_int(v); var_set_int(n->val,iv); char buf[32]; int l=snprintf(buf,sizeof(buf),"%ld",iv); pool_add(buf,l); }
        } else if (v->type==NODE_IDENT) {
            int vi=var_find(v->val);
            if(vi>=0){if(vars[vi].type==VAR_STR)var_set_str(n->val,vars[vi].sval,vars[vi].slen);else var_set_int(n->val,vars[vi].ival);}
        }
        return;
    }
    if (n->type == NODE_PRINT) {
        for (int i=0; i<n->nchild; i++) {
            ASTNode *a = n->children[i]; if (!a) continue;
            if (a->type==NODE_STRING) pool_add(a->val,a->val_len);
            else if (a->type==NODE_INT) pool_add(a->val,a->val_len);
            else if (a->type==NODE_BINOP) {
                if (a->val&&!strcmp(a->val,"+")&&(is_str_node(a->children[0])||is_str_node(a->children[1]))){int sl;char *s=eval_str(a,&sl);if(sl>0)pool_add(s,sl);}
                else{long v=eval_int(a);char buf[32];int l=snprintf(buf,sizeof(buf),"%ld",v);pool_add(buf,l);}
            } else if (a->type==NODE_IDENT) {
                int vi=var_find(a->val);
                if(vi>=0){if(vars[vi].type==VAR_STR)pool_add(vars[vi].sval,vars[vi].slen);else{char buf[32];int l=snprintf(buf,sizeof(buf),"%ld",vars[vi].ival);pool_add(buf,l);}}
            }
        }
        return;
    }
    if (n->type == NODE_REPEAT && n->nchild >= 2) {
        long count = eval_int(n->children[0]);
        for (long i=0; i<count && i<1000; i++) pre_collect(n->children[1]);
        return;
    }
    for (int i=0; i<n->nchild; i++) pre_collect(n->children[i]);
}

// ─── Main codegen entry ───────────────────────────────────────
int codegen(ASTNode *ast, const char *outname, Arch arch) {
    cur_arch  = arch;
    out_pos   = 0;
    lbl_cnt   = 0;
    var_count = 0;
    str_count = 0;

    // Pre-collect pass: simulate execution to collect ALL strings
    // including those generated inside loops
    pre_collect(ast);
    // Reset var table for actual emit
    var_count = 0;

    // ── Data section ──
    if (arch == ARCH_AARCH64) {
        out("// X Compiler v0.2 - Phase 2 (AArch64)\n");
        out(".global _start\n\n");
        out(".section .data\n");
        out("_nl: .ascii \"\\n\"\n");
        out("_sp: .ascii \" \"\n");
        out(".section .bss\n");
        out("_ibuf: .space 1024\n");
        out("_ibuf_len: .space 8\n");
        out(".section .data\n");
    } else {
        out("# X Compiler v0.2 - Phase 2 (x86_64)\n");
        out(".global _start\n\n");
        out(".section .data\n");
        out("_nl: .ascii \"\\n\"\n");
        out("_sp: .ascii \" \"\n");
        out(".section .bss\n");
        out("_ibuf: .space 1024\n");
        out(".section .data\n");
    }

    // Emit string pool
    for (int i=0; i<str_count; i++) {
        out("_str%d: .ascii \"", i);
        emit_escaped(str_pool[i].ptr, str_pool[i].len);
        out("\"\n");
        if (arch == ARCH_AARCH64)
            out("_str%d_len = . - _str%d\n", i, i);
        else
            out(".set _str%d_len, . - _str%d\n", i, i);
    }

    // ── Text section ──
    out("\n.section .text\n_start:\n");

    // emit AST
    emit_node(ast);

    // ── Exit ──
    if (arch == ARCH_AARCH64) {
        out("\n    mov x0, #0\n    mov x8, #93\n    svc #0\n");
    } else {
        out("\n    mov $60, %%rax\n    xor %%rdi, %%rdi\n    syscall\n");
    }

    // ── Write assembly to temp file, assemble, link ──
    char asm_f[256], obj_f[256], cmd[512];
    const char *tmp = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
    // Use basename of outname for temp files
    const char *base = strrchr(outname, '/');
    base = base ? base+1 : outname;
    snprintf(asm_f, sizeof(asm_f), "%s/x_%s.s", tmp, base);
    snprintf(obj_f, sizeof(obj_f), "%s/x_%s.o", tmp, base);

    FILE *f = fopen(asm_f, "w");
    if (!f) { fprintf(stderr,"Cannot write %s\n",asm_f); return 0; }
    fwrite(out_buf, 1, out_pos, f);
    fclose(f);

    snprintf(cmd, sizeof(cmd), "as %s -o %s", asm_f, obj_f);
    printf("[X] Assembling...\n");
    if (system(cmd) != 0) { fprintf(stderr,"as failed\n"); return 0; }

    snprintf(cmd, sizeof(cmd), "ld %s -o %s", obj_f, outname);
    printf("[X] Linking...\n");
    if (system(cmd) != 0) { fprintf(stderr,"ld failed\n"); return 0; }

    snprintf(cmd, sizeof(cmd), "chmod +x %s", outname);
    system(cmd);

    remove(asm_f); remove(obj_f);
    return 1;
}
