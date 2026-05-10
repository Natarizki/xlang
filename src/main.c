#include "xlang.h"

Var      vars[MAX_VARS];
int      var_count = 0;
StrEntry str_pool[MAX_STRINGS];
int      str_count = 0;

Arch detect_arch() {
    FILE *f = popen("uname -m", "r");
    if (!f) return ARCH_UNKNOWN;
    char buf[32]; if(!fgets(buf, sizeof(buf), f)){} pclose(f);
    if (strstr(buf, "aarch64") || strstr(buf, "arm64")) return ARCH_AARCH64;
    if (strstr(buf, "x86_64")) return ARCH_X86_64;
    return ARCH_UNKNOWN;
}

int var_find(const char *name) {
    for (int i = 0; i < var_count; i++)
        if (strcmp(vars[i].name, name) == 0) return i;
    return -1;
}
void var_set_int(const char *name, long val) {
    int i = var_find(name);
    if (i < 0) { i = var_count++; strncpy(vars[i].name, name, 63); }
    vars[i].type = VAR_INT; vars[i].ival = val;
}
void var_set_float(const char *name, double val) {
    int i = var_find(name);
    if (i < 0) { i = var_count++; strncpy(vars[i].name, name, 63); }
    vars[i].type = VAR_FLOAT; vars[i].fval = val;
}
void var_set_str(const char *name, const char *val, int len) {
    int i = var_find(name);
    if (i < 0) { i = var_count++; strncpy(vars[i].name, name, 63); }
    vars[i].type = VAR_STR;
    vars[i].sval = strndup(val, len);
    vars[i].slen = len;
}
int pool_add(const char *ptr, int len) {
    for (int i = 0; i < str_count; i++)
        if (str_pool[i].len == len && memcmp(str_pool[i].ptr, ptr, len) == 0) return i;
    str_pool[str_count].ptr = strndup(ptr, len);
    str_pool[str_count].len = len;
    return str_count++;
}

static void usage(const char *p) {
    fprintf(stderr, "X Compiler v0.2\nUsage: %s <file.x> -o <output>\n", p);
}

int main(int argc, char *argv[]) {
    if (argc < 2) { usage(argv[0]); return 1; }

    const char *infile = argv[1];
    const char *outname = "out";
    for (int i = 2; i < argc; i++)
        if (strcmp(argv[i], "-o") == 0 && i+1 < argc)
            outname = argv[++i];

    // Read source
    FILE *f = fopen(infile, "r");
    if (!f) { fprintf(stderr, "Cannot open: %s\n", infile); return 1; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    char *src = malloc(sz+1); if(!fread(src, 1, sz, f)){} src[sz]=0; fclose(f);

    Arch arch = detect_arch();
    printf("[X] Compiling %s -> %s (%s)\n", infile, outname,
        arch == ARCH_AARCH64 ? "AArch64" : "x86_64");

    TokenList *tl = lex(src, sz);
    ASTNode   *ast = parse(tl);
    int ok = codegen(ast, outname, arch);

    ast_free(ast); lex_free(tl); free(src);
    if (ok) printf("[X] Done! Run: ./%s\n", outname);
    return ok ? 0 : 1;
}
