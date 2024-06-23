#define CBUILD_IMPLEMENTATION
#include "cbuild.h"

void cc(Cmd* cmd) {
    cmd_push_str(cmd, "gcc");
}

void cflags(Cmd* cmd) {
    cmd_push_str(cmd, "-Wall", "-Wextra", "-ggdb", "-O3");
    cmd_push_str(cmd, "-L./tree-sitter/", "-L./tree-sitter-c/");
}

void libs(Cmd* cmd) {
    cmd_push_str(cmd, "-l:libtree-sitter.a", "-l:libtree-sitter-c.a");
}

int main(int argc, char** argv) {
    Cmd cmd = {0};
    build_yourself(&cmd, argc, argv);

    if (need_rebuild("tree-sitter/libtree-sitter.a", STRS("tree-sitter/Makefile"))) {
        cmd_push_str(&cmd, "make", "-C", "tree-sitter/", "all");
        if (!cmd_run_sync(&cmd, true)) return 1;
        cmd.count = 0;
    }

    if (need_rebuild("tree-sitter-c/libtree-sitter-c.a", STRS("tree-sitter-c/Makefile"))) {
        cmd_push_str(&cmd, "make", "-C", "tree-sitter-c/", "all");
        if (!cmd_run_sync(&cmd, true)) return 1;
        cmd.count = 0;
    }

    if (need_rebuild("cffind", STRS("main.c"))) {
        cc(&cmd);
        cflags(&cmd);
        cmd_push_str(&cmd, "-o", "cffind", "main.c");
        libs(&cmd);
        if (!cmd_run_sync(&cmd, true)) return 1;
        cmd.count = 0;
    }

    return 0;
}
