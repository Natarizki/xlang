#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#define REGISTRY "https://github.com/Natarizki/xpm-packages/raw/main/packages"
#define PKG_DIR  "%s/.xlang/packages"
#define MAX_PATH 512

static void usage() {
    printf("xpm - X Package Manager v0.1\n\n");
    printf("Usage:\n");
    printf("  xpm install <package>   Install a package\n");
    printf("  xpm remove  <package>   Remove a package\n");
    printf("  xpm list                List installed packages\n");
    printf("  xpm search  <keyword>   Search packages\n");
    printf("  xpm update              Update all packages\n");
    printf("  xpm info    <package>   Show package info\n\n");
    printf("Example:\n");
    printf("  xpm install math\n");
    printf("  xpm install http\n");
}

static void get_pkg_dir(char *out, size_t sz) {
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(out, sz, "%s/.xlang/packages", home);
}

static void mkdir_p(const char *path) {
    char tmp[MAX_PATH];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp+1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static int pkg_exists(const char *name) {
    char path[MAX_PATH];
    get_pkg_dir(path, sizeof(path));
    strncat(path, "/", sizeof(path)-strlen(path)-1);
    strncat(path, name, sizeof(path)-strlen(path)-1);
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int cmd_install(const char *name) {
    printf("[xpm] Installing %s...\n", name);

    if (pkg_exists(name)) {
        printf("[xpm] %s is already installed!\n", name);
        return 0;
    }

    char pkg_dir[MAX_PATH];
    get_pkg_dir(pkg_dir, sizeof(pkg_dir));
    mkdir_p(pkg_dir);

    char dest[MAX_PATH];
    snprintf(dest, sizeof(dest), "%s/%s", pkg_dir, name);
    mkdir_p(dest);

    // Download main.x
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "curl -sSf %s/%s/main.x -o %s/main.x 2>/dev/null || "
        "wget -q %s/%s/main.x -O %s/main.x 2>/dev/null",
        REGISTRY, name, dest,
        REGISTRY, name, dest);

    if (system(cmd) != 0) {
        fprintf(stderr, "[xpm] Failed to download %s!\n", name);
        fprintf(stderr, "[xpm] Package '%s' not found in registry.\n", name);
        // Cleanup empty dir
        rmdir(dest);
        return 1;
    }

    // Check if file actually downloaded
    char main_x[MAX_PATH];
    snprintf(main_x, sizeof(main_x), "%s/main.x", dest);
    struct stat st;
    if (stat(main_x, &st) != 0 || st.st_size == 0) {
        fprintf(stderr, "[xpm] Package '%s' not found in registry.\n", name);
        remove(main_x); rmdir(dest);
        return 1;
    }

    printf("[xpm] %s installed!\n", name);
    printf("[xpm] Use: import %s\n", name);
    return 0;
}

static int cmd_remove(const char *name) {
    printf("[xpm] Removing %s...\n", name);

    if (!pkg_exists(name)) {
        fprintf(stderr, "[xpm] %s is not installed!\n", name);
        return 1;
    }

    char pkg_dir[MAX_PATH];
    get_pkg_dir(pkg_dir, sizeof(pkg_dir));

    char cmd[MAX_PATH*2];
    snprintf(cmd, sizeof(cmd), "rm -rf %s/%s", pkg_dir, name);
    system(cmd);

    printf("[xpm] %s removed!\n", name);
    return 0;
}

static int cmd_list() {
    char pkg_dir[MAX_PATH];
    get_pkg_dir(pkg_dir, sizeof(pkg_dir));

    DIR *d = opendir(pkg_dir);
    if (!d) {
        printf("[xpm] No packages installed.\n");
        return 0;
    }

    printf("[xpm] Installed packages:\n");
    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        printf("  - %s\n", entry->d_name);
        count++;
    }
    closedir(d);

    if (count == 0) printf("  (none)\n");
    else printf("\nTotal: %d package(s)\n", count);
    return 0;
}

static int cmd_search(const char *keyword) {
    printf("[xpm] Searching for '%s'...\n", keyword);
    printf("[xpm] Available packages (registry):\n");

    // Known packages (will be fetched from registry later)
    const char *pkgs[] = {
        "math", "string", "http", "json",
        "os", "io", "net", "crypto",
        "sdl2", "opengl", "sqlite", NULL
    };

    int found = 0;
    for (int i = 0; pkgs[i]; i++) {
        if (!keyword || strstr(pkgs[i], keyword)) {
            printf("  - %s\n", pkgs[i]);
            found++;
        }
    }

    if (!found) printf("  No packages found for '%s'\n", keyword);
    return 0;
}

static int cmd_update() {
    printf("[xpm] Updating all packages...\n");

    char pkg_dir[MAX_PATH];
    get_pkg_dir(pkg_dir, sizeof(pkg_dir));

    DIR *d = opendir(pkg_dir);
    if (!d) { printf("[xpm] No packages to update.\n"); return 0; }

    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        printf("[xpm] Updating %s...\n", entry->d_name);
        cmd_install(entry->d_name);
        count++;
    }
    closedir(d);

    if (count == 0) printf("[xpm] Nothing to update.\n");
    else printf("[xpm] Updated %d package(s)!\n", count);
    return 0;
}

static int cmd_info(const char *name) {
    printf("[xpm] Package: %s\n", name);

    char pkg_dir[MAX_PATH];
    get_pkg_dir(pkg_dir, sizeof(pkg_dir));

    char main_x[MAX_PATH];
    snprintf(main_x, sizeof(main_x), "%s/%s/main.x", pkg_dir, name);

    struct stat st;
    if (stat(main_x, &st) == 0) {
        printf("[xpm] Status:  installed\n");
        printf("[xpm] Size:    %ld bytes\n", st.st_size);
        printf("[xpm] Path:    %s\n", main_x);
    } else {
        printf("[xpm] Status:  not installed\n");
        printf("[xpm] Run: xpm install %s\n", name);
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) { usage(); return 1; }

    const char *cmd = argv[1];

    if (strcmp(cmd, "install") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: xpm install <package>\n"); return 1; }
        return cmd_install(argv[2]);
    }
    if (strcmp(cmd, "remove") == 0 || strcmp(cmd, "uninstall") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: xpm remove <package>\n"); return 1; }
        return cmd_remove(argv[2]);
    }
    if (strcmp(cmd, "list") == 0 || strcmp(cmd, "ls") == 0) {
        return cmd_list();
    }
    if (strcmp(cmd, "search") == 0) {
        return cmd_search(argc >= 3 ? argv[2] : NULL);
    }
    if (strcmp(cmd, "update") == 0) {
        return cmd_update();
    }
    if (strcmp(cmd, "info") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: xpm info <package>\n"); return 1; }
        return cmd_info(argv[2]);
    }

    fprintf(stderr, "Unknown command: %s\n", cmd);
    usage();
    return 1;
}
