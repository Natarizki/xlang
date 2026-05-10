#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE 4096
#define MAX_LINES 65536

typedef enum {
    LANG_UNKNOWN,
    LANG_PYTHON,
    LANG_JAVASCRIPT,
    LANG_TYPESCRIPT,
    LANG_C,
    LANG_CPP,
    LANG_GO,
    LANG_RUST,
} Lang;

// ─── Detect language from extension ──────────────────────────
Lang detect_lang(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return LANG_UNKNOWN;
    if (!strcmp(ext, ".py"))  return LANG_PYTHON;
    if (!strcmp(ext, ".js"))  return LANG_JAVASCRIPT;
    if (!strcmp(ext, ".ts"))  return LANG_TYPESCRIPT;
    if (!strcmp(ext, ".c"))   return LANG_C;
    if (!strcmp(ext, ".cpp") || !strcmp(ext, ".cc")) return LANG_CPP;
    if (!strcmp(ext, ".go"))  return LANG_GO;
    if (!strcmp(ext, ".rs"))  return LANG_RUST;
    return LANG_UNKNOWN;
}

// ─── String helpers ───────────────────────────────────────────
static void trim(char *s) {
    int start = 0;
    while (s[start] == ' ' || s[start] == '\t') start++;
    memmove(s, s+start, strlen(s)-start+1);
    int end = strlen(s)-1;
    while (end >= 0 && (s[end]=='\n'||s[end]=='\r'||s[end]==' ')) s[end--]=0;
}

static int startswith(const char *s, const char *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static void replace_str(char *s, const char *old, const char *new, char *out, int outsz) {
    char *p = strstr(s, old);
    if (!p) { strncpy(out, s, outsz); return; }
    int before = p - s;
    snprintf(out, outsz, "%.*s%s%s", before, s, new, p + strlen(old));
}

// ─── Python → X ───────────────────────────────────────────────
static void convert_python(FILE *in, FILE *out) {
    char line[MAX_LINE];
    fprintf(out, "// Converted from Python by X converter\n\n");

    while (fgets(line, sizeof(line), in)) {
        char orig[MAX_LINE]; strcpy(orig, line);
        trim(line);

        // Skip empty
        if (strlen(line) == 0) { fprintf(out, "\n"); continue; }

        // Comments
        if (line[0] == '#') {
            fprintf(out, "//%s\n", line+1);
            continue;
        }

        // def → make
        if (startswith(line, "def ")) {
            char *paren = strchr(line, '(');
            char *colon = strrchr(line, ':');
            if (paren && colon) *colon = 0;
            fprintf(out, "make %s\n", line+4);
            continue;
        }

        // return → give
        if (startswith(line, "return ")) {
            fprintf(out, "    give %s\n", line+7);
            continue;
        }

        // print() stays print()
        if (startswith(line, "print(")) {
            fprintf(out, "%s\n", line);
            continue;
        }

        // if/elif/else
        if (startswith(line, "if ")) {
            char cond[MAX_LINE]; strcpy(cond, line+3);
            char *colon = strrchr(cond, ':');
            if (colon) *colon = 0;
            // Replace Python ops
            char tmp[MAX_LINE];
            replace_str(cond, " and ", " and ", tmp, sizeof(tmp));
            replace_str(tmp, " or ", " or ", cond, sizeof(cond));
            fprintf(out, "if %s\n", cond);
            continue;
        }
        if (startswith(line, "elif ")) {
            char cond[MAX_LINE]; strcpy(cond, line+5);
            char *colon = strrchr(cond, ':');
            if (colon) *colon = 0;
            fprintf(out, "or if %s\n", cond);
            continue;
        }
        if (startswith(line, "else:")) {
            fprintf(out, "or\n");
            continue;
        }

        // for x in y:
        if (startswith(line, "for ")) {
            char body[MAX_LINE]; strcpy(body, line+4);
            char *colon = strrchr(body, ':');
            if (colon) *colon = 0;
            fprintf(out, "for %s\n", body);
            continue;
        }

        // while
        if (startswith(line, "while ")) {
            char cond[MAX_LINE]; strcpy(cond, line+6);
            char *colon = strrchr(cond, ':');
            if (colon) *colon = 0;
            fprintf(out, "while %s\n", cond);
            continue;
        }

        // import
        if (startswith(line, "import ")) {
            fprintf(out, "import %s\n", line+7);
            continue;
        }
        if (startswith(line, "from ")) {
            // from x import y → import x
            char *imp = strstr(line, " import ");
            if (imp) {
                char mod[128]; 
                strncpy(mod, line+5, imp-(line+5));
                mod[imp-(line+5)] = 0;
                fprintf(out, "import %s\n", mod);
            }
            continue;
        }

        // True/False/None
        char converted[MAX_LINE]; strcpy(converted, line);
        char tmp[MAX_LINE];
        replace_str(converted, "True",  "yes",  tmp, sizeof(tmp)); strcpy(converted, tmp);
        replace_str(converted, "False", "no",   tmp, sizeof(tmp)); strcpy(converted, tmp);
        replace_str(converted, "None",  "null", tmp, sizeof(tmp)); strcpy(converted, tmp);
        // and/or/not stay same in X

        // pass → skip
        if (!strcmp(converted, "pass")) { fprintf(out, "    skip\n"); continue; }

        // break stays
        if (!strcmp(converted, "break")) { fprintf(out, "    break\n"); continue; }

        // class
        if (startswith(converted, "class ")) {
            char *colon = strrchr(converted, ':');
            if (colon) *colon = 0;
            // Remove parent class (class Foo(Bar) → make class Foo)
            char *paren = strchr(converted+6, '(');
            if (paren) *paren = 0;
            fprintf(out, "make class %s\n", converted+6);
            continue;
        }

        // __init__ → make init
        if (strstr(converted, "def __init__")) {
            char *paren = strchr(converted, '(');
            char *colon = strrchr(converted, ':');
            if (paren && colon) *colon = 0;
            fprintf(out, "    make init%s\n", paren ? paren : "()");
            continue;
        }

        // self. → self.
        fprintf(out, "%s\n", converted);
    }
}

// ─── JavaScript/TypeScript → X ────────────────────────────────
static void convert_js(FILE *in, FILE *out, int is_ts) {
    char line[MAX_LINE];
    fprintf(out, "// Converted from %s by X converter\n\n",
        is_ts ? "TypeScript" : "JavaScript");

    while (fgets(line, sizeof(line), in)) {
        trim(line);
        if (!strlen(line)) { fprintf(out, "\n"); continue; }

        // Comments
        if (startswith(line, "//")) { fprintf(out, "%s\n", line); continue; }
        if (startswith(line, "/*")) { fprintf(out, "%s\n", line); continue; }

        // function → make
        if (startswith(line, "function ")) {
            char *paren = strchr(line, '(');
            char *brace = strrchr(line, '{');
            if (brace) *brace = 0;
            fprintf(out, "make %s\n", line+9);
            continue;
        }

        // const/let/var → remove type, keep assignment
        if (startswith(line, "const ") || startswith(line, "let ") || startswith(line, "var ")) {
            char *eq = strchr(line, '=');
            char *semi = strrchr(line, ';');
            if (semi) *semi = 0;
            if (eq) {
                // Find var name
                char *start = line;
                while (*start && *start != ' ') start++;
                while (*start == ' ') start++;
                // Remove TypeScript type annotation
                char *colon = strchr(start, ':');
                if (colon && colon < eq) *colon = 0;
                fprintf(out, "%s\n", start);
            }
            continue;
        }

        // return → give
        if (startswith(line, "return ")) {
            char val[MAX_LINE]; strcpy(val, line+7);
            char *semi = strrchr(val, ';');
            if (semi) *semi = 0;
            fprintf(out, "    give %s\n", val);
            continue;
        }

        // console.log → print
        if (strstr(line, "console.log(")) {
            char *start = strstr(line, "console.log(");
            char *semi = strrchr(line, ';');
            if (semi) *semi = 0;
            // Replace console.log( with print(
            char tmp[MAX_LINE];
            replace_str(line, "console.log(", "print(", tmp, sizeof(tmp));
            fprintf(out, "%s\n", tmp);
            continue;
        }

        // if/else
        if (startswith(line, "if (") || startswith(line, "if(")) {
            char cond[MAX_LINE];
            char *start = strchr(line, '(');
            if (start) {
                strcpy(cond, start+1);
                char *end = strrchr(cond, ')');
                if (end) *end = 0;
                char *brace = strrchr(cond, '{');
                if (brace) *brace = 0;
                // Replace === with ==, !== with !=
                char tmp[MAX_LINE];
                replace_str(cond, "===", "==", tmp, sizeof(tmp));
                replace_str(tmp, "!==", "!=", cond, sizeof(cond));
                fprintf(out, "if %s\n", cond);
            }
            continue;
        }
        if (startswith(line, "} else {") || !strcmp(line, "else {")) {
            fprintf(out, "or\n"); continue;
        }
        if (startswith(line, "} else if")) {
            char *paren = strchr(line, '(');
            char *close = strrchr(line, ')');
            if (paren && close) {
                *close = 0;
                fprintf(out, "or if %s\n", paren+1);
            }
            continue;
        }

        // for loops
        if (startswith(line, "for (") || startswith(line, "for(")) {
            // for (let x of arr) → for x in arr
            if (strstr(line, " of ")) {
                char *of = strstr(line, " of ");
                char *paren = strchr(line, '(');
                if (paren && of) {
                    char var[64], arr[128];
                    // extract variable
                    char *vstart = paren+1;
                    while (*vstart==' '||*vstart=='l'||*vstart=='e'||*vstart=='t'||*vstart==' ') vstart++;
                    strncpy(var, vstart, of-vstart); var[of-vstart]=0;
                    // extract array
                    char *astart = of+4;
                    strcpy(arr, astart);
                    char *end = strrchr(arr, ')');
                    if (end) *end=0;
                    fprintf(out, "for %s in %s\n", var, arr);
                }
            }
            continue;
        }

        // while
        if (startswith(line, "while (") || startswith(line, "while(")) {
            char *paren = strchr(line, '(');
            char *close = strrchr(line, ')');
            if (paren && close) {
                *close = 0;
                fprintf(out, "while %s\n", paren+1);
            }
            continue;
        }

        // true/false/null
        char converted[MAX_LINE]; strcpy(converted, line);
        char tmp[MAX_LINE];
        replace_str(converted, "true",      "yes",  tmp, sizeof(tmp)); strcpy(converted, tmp);
        replace_str(converted, "false",     "no",   tmp, sizeof(tmp)); strcpy(converted, tmp);
        replace_str(converted, "null",      "null", tmp, sizeof(tmp)); strcpy(converted, tmp);
        replace_str(converted, "undefined", "null", tmp, sizeof(tmp)); strcpy(converted, tmp);

        // Remove semicolons and braces
        char *semi = strrchr(converted, ';');
        if (semi && *(semi+1)==0) *semi=0;
        if (!strcmp(converted, "{") || !strcmp(converted, "}")) continue;

        if (strlen(converted)) fprintf(out, "%s\n", converted);
    }
}

// ─── C/C++ → X ────────────────────────────────────────────────
static void convert_c(FILE *in, FILE *out, int is_cpp) {
    char line[MAX_LINE];
    fprintf(out, "// Converted from %s by X converter\n\n", is_cpp ? "C++" : "C");

    while (fgets(line, sizeof(line), in)) {
        trim(line);
        if (!strlen(line)) { fprintf(out, "\n"); continue; }

        // Comments
        if (startswith(line, "//") || startswith(line, "/*")) {
            fprintf(out, "%s\n", line); continue;
        }

        // #include → import
        if (startswith(line, "#include")) {
            char *lt = strchr(line, '<');
            char *qt = strchr(line, '"');
            if (lt) {
                char *gt = strchr(lt, '>');
                if (gt) { *gt=0; fprintf(out, "import %s\n", lt+1); }
            } else if (qt) {
                char *qt2 = strchr(qt+1, '"');
                if (qt2) { *qt2=0; fprintf(out, "import %s\n", qt+1); }
            }
            continue;
        }

        // #define → const
        if (startswith(line, "#define ")) {
            char *rest = line+8;
            char *space = strchr(rest, ' ');
            if (space) {
                *space = 0;
                fprintf(out, "%s = %s\n", rest, space+1);
            }
            continue;
        }

        // printf → print
        if (strstr(line, "printf(")) {
            char tmp[MAX_LINE];
            replace_str(line, "printf(", "print(", tmp, sizeof(tmp));
            char *semi = strrchr(tmp, ';');
            if (semi) *semi=0;
            fprintf(out, "%s\n", tmp);
            continue;
        }

        // return
        if (startswith(line, "return ")) {
            char val[MAX_LINE]; strcpy(val, line+7);
            char *semi = strrchr(val, ';');
            if (semi) *semi=0;
            fprintf(out, "    give %s\n", val);
            continue;
        }

        // if/else
        if (startswith(line, "if (") || startswith(line, "if(")) {
            char *paren = strchr(line, '(');
            char *close = strrchr(line, ')');
            if (paren && close) {
                char cond[MAX_LINE];
                strncpy(cond, paren+1, close-paren-1);
                cond[close-paren-1]=0;
                fprintf(out, "if %s\n", cond);
            }
            continue;
        }
        if (startswith(line, "} else {") || !strcmp(line,"else {")) {
            fprintf(out, "or\n"); continue;
        }

        // for/while
        if (startswith(line, "for (") || startswith(line, "for(")) {
            // Simple: skip C-style for loops (too complex to convert)
            fprintf(out, "// TODO: convert for loop\n");
            continue;
        }
        if (startswith(line, "while (") || startswith(line, "while(")) {
            char *paren = strchr(line, '(');
            char *close = strrchr(line, ')');
            if (paren && close) {
                *close=0;
                fprintf(out, "while %s\n", paren+1);
            }
            continue;
        }

        // Remove type declarations (int x = ...) → x = ...
        const char *types[] = {"int ","float ","double ","char ","long ","short ",
                                "bool ","string ","void ","auto ","uint8_t ","uint32_t ",
                                "uint64_t ","int64_t ","size_t ", NULL};
        int found_type = 0;
        for (int i=0; types[i]; i++) {
            if (startswith(line, types[i])) {
                char *rest = line + strlen(types[i]);
                // Remove pointer notation
                while (*rest == '*' || *rest == ' ') rest++;
                char *semi = strrchr(rest, ';');
                if (semi) *semi=0;
                char *brace = strrchr(rest, '{');
                if (brace) *brace=0;
                // Check if it's a function definition
                char *paren = strchr(rest, '(');
                if (paren) fprintf(out, "make %s\n", rest);
                else fprintf(out, "%s\n", rest);
                found_type = 1;
                break;
            }
        }
        if (found_type) continue;

        // Skip braces
        if (!strcmp(line,"{") || !strcmp(line,"}") || !strcmp(line,"};")) continue;

        // Remove semicolons
        char converted[MAX_LINE]; strcpy(converted, line);
        char *semi = strrchr(converted, ';');
        if (semi && *(semi+1)==0) *semi=0;

        if (strlen(converted)) fprintf(out, "%s\n", converted);
    }
}

// ─── Go → X ───────────────────────────────────────────────────
static void convert_go(FILE *in, FILE *out) {
    char line[MAX_LINE];
    fprintf(out, "// Converted from Go by X converter\n\n");

    while (fgets(line, sizeof(line), in)) {
        trim(line);
        if (!strlen(line)) { fprintf(out, "\n"); continue; }

        if (startswith(line, "//")) { fprintf(out, "%s\n", line); continue; }
        if (!strcmp(line, "package main")) continue;

        // import
        if (startswith(line, "import ")) {
            char *qt = strchr(line, '"');
            if (qt) {
                char *qt2 = strchr(qt+1, '"');
                if (qt2) { *qt2=0; fprintf(out, "import %s\n", qt+1); }
            }
            continue;
        }

        // func → make
        if (startswith(line, "func ")) {
            char *paren = strchr(line, '(');
            char *brace = strrchr(line, '{');
            if (brace) *brace=0;
            fprintf(out, "make %s\n", line+5);
            continue;
        }

        // fmt.Println/fmt.Printf → print
        if (strstr(line, "fmt.Println(") || strstr(line, "fmt.Printf(") || strstr(line, "fmt.Print(")) {
            char tmp[MAX_LINE];
            replace_str(line, "fmt.Println(", "print(", tmp, sizeof(tmp));
            replace_str(tmp, "fmt.Printf(", "print(", line, sizeof(line));
            replace_str(line, "fmt.Print(", "print(", tmp, sizeof(tmp));
            fprintf(out, "%s\n", tmp);
            continue;
        }

        // return → give
        if (startswith(line, "return ")) {
            fprintf(out, "    give %s\n", line+7); continue;
        }

        // var/const declarations
        if (startswith(line, "var ") || startswith(line, "const ")) {
            char *start = strchr(line, ' ')+1;
            char *space = strchr(start, ' ');
            if (space) *space=0;
            fprintf(out, "%s = null\n", start);
            continue;
        }

        // := → =
        char converted[MAX_LINE]; strcpy(converted, line);
        char tmp[MAX_LINE];
        replace_str(converted, ":=", "=", tmp, sizeof(tmp));
        strcpy(converted, tmp);

        // if/for/else
        if (startswith(converted, "if ")) {
            char *brace = strrchr(converted, '{');
            if (brace) *brace=0;
            fprintf(out, "if %s\n", converted+3); continue;
        }
        if (startswith(converted, "} else {")) {
            fprintf(out, "or\n"); continue;
        }
        if (startswith(converted, "for ")) {
            char *brace = strrchr(converted, '{');
            if (brace) *brace=0;
            // for range → for x in
            if (strstr(converted, "range ")) {
                char *range = strstr(converted, "range ");
                fprintf(out, "for item in %s\n", range+6);
            } else {
                fprintf(out, "// TODO: convert for loop\n");
            }
            continue;
        }

        if (!strcmp(converted,"{") || !strcmp(converted,"}")) continue;
        if (strlen(converted)) fprintf(out, "%s\n", converted);
    }
}

// ─── Rust → X ─────────────────────────────────────────────────
static void convert_rust(FILE *in, FILE *out) {
    char line[MAX_LINE];
    fprintf(out, "// Converted from Rust by X converter\n\n");

    while (fgets(line, sizeof(line), in)) {
        trim(line);
        if (!strlen(line)) { fprintf(out, "\n"); continue; }
        if (startswith(line, "//")) { fprintf(out, "%s\n", line); continue; }

        // use → import
        if (startswith(line, "use ")) {
            char *semi = strrchr(line, ';');
            if (semi) *semi=0;
            fprintf(out, "import %s\n", line+4); continue;
        }

        // fn → make
        if (startswith(line, "fn ")) {
            char *paren = strchr(line, '(');
            char *brace = strrchr(line, '{');
            if (brace) *brace=0;
            // Remove return type (-> Type)
            char *arrow = strstr(line, " -> ");
            if (arrow) *arrow=0;
            fprintf(out, "make %s\n", line+3); continue;
        }

        // println!/print! → print
        if (strstr(line, "println!(") || strstr(line, "print!(")) {
            char tmp[MAX_LINE];
            replace_str(line, "println!(", "print(", tmp, sizeof(tmp));
            replace_str(tmp, "print!(", "print(", line, sizeof(line));
            char *semi = strrchr(line, ';');
            if (semi) *semi=0;
            fprintf(out, "%s\n", line); continue;
        }

        // let → remove
        if (startswith(line, "let ") || startswith(line, "let mut ")) {
            char *rest = line + (startswith(line,"let mut ") ? 8 : 4);
            // Remove type annotation
            char *colon = strchr(rest, ':');
            char *eq = strchr(rest, '=');
            if (colon && eq && colon < eq) {
                char name[64]; strncpy(name, rest, colon-rest); name[colon-rest]=0;
                char *semi=strrchr(eq,';'); if(semi)*semi=0;
                fprintf(out, "%s %s\n", name, eq); continue;
            }
            char *semi=strrchr(rest,';'); if(semi)*semi=0;
            fprintf(out, "%s\n", rest); continue;
        }

        // return → give
        if (startswith(line, "return ")) {
            char val[MAX_LINE]; strcpy(val, line+7);
            char *semi=strrchr(val,';'); if(semi)*semi=0;
            fprintf(out, "    give %s\n", val); continue;
        }

        // true/false
        char converted[MAX_LINE]; strcpy(converted, line);
        char tmp[MAX_LINE];
        replace_str(converted, "true",  "yes", tmp, sizeof(tmp)); strcpy(converted, tmp);
        replace_str(converted, "false", "no",  tmp, sizeof(tmp)); strcpy(converted, tmp);

        char *semi=strrchr(converted,';'); if(semi&&*(semi+1)==0)*semi=0;
        if (!strcmp(converted,"{") || !strcmp(converted,"}")) continue;
        if (strlen(converted)) fprintf(out, "%s\n", converted);
    }
}

// ─── Main ─────────────────────────────────────────────────────
int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("X Converter v0.1\n");
        printf("Usage: xconv <input> <output.x>\n");
        printf("Supports: .py .js .ts .c .cpp .go .rs\n");
        return 1;
    }

    const char *infile  = argv[1];
    const char *outfile = argv[2];

    Lang lang = detect_lang(infile);
    if (lang == LANG_UNKNOWN) {
        fprintf(stderr, "Unknown file type: %s\n", infile);
        return 1;
    }

    FILE *in = fopen(infile, "r");
    if (!in) { fprintf(stderr, "Cannot open: %s\n", infile); return 1; }

    FILE *out = fopen(outfile, "w");
    if (!out) { fprintf(stderr, "Cannot write: %s\n", outfile); fclose(in); return 1; }

    const char *lang_names[] = {"?","Python","JavaScript","TypeScript","C","C++","Go","Rust"};
    printf("[xconv] Converting %s (%s) → %s\n", infile, lang_names[lang], outfile);

    switch (lang) {
        case LANG_PYTHON:     convert_python(in, out); break;
        case LANG_JAVASCRIPT: convert_js(in, out, 0);  break;
        case LANG_TYPESCRIPT: convert_js(in, out, 1);  break;
        case LANG_C:          convert_c(in, out, 0);   break;
        case LANG_CPP:        convert_c(in, out, 1);   break;
        case LANG_GO:         convert_go(in, out);     break;
        case LANG_RUST:       convert_rust(in, out);   break;
        default: break;
    }

    fclose(in); fclose(out);
    printf("[xconv] Done! Output: %s\n", outfile);
    return 0;
}
