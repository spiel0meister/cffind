#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>

#include "da.h"

#include "tree-sitter/lib/include/tree_sitter/api.h"
#include "tree-sitter-c/bindings/c/tree-sitter-c.h"

#define FDEF_QUERY "(declaration type: (_) @return_type declarator: (function_declarator parameters: (parameter_list ((parameter_declaration type: (_) @param_type) . (\",\" . (parameter_declaration type: (_) @param_type))*)?))) @def"

typedef struct {
    const char* start;
    size_t len;
}StringView;

typedef struct {
    StringView* items;
    size_t count;
    size_t capacity;
}ParamTypes;

typedef struct {
    const char* file;
    size_t line;

    StringView return_type;
    StringView def;
    ParamTypes param_types;
}FuncDef;

typedef struct {
    FuncDef* items;
    size_t count;
    size_t capacity;
}FuncDefs;

bool get_defs(FuncDefs* defs, const char* file, const char* source) {
    size_t source_len = strlen(source);

    TSParser* parser = ts_parser_new();
    assert(parser != NULL);
    assert(ts_parser_set_language(parser, tree_sitter_c()));

    TSTree* tree = ts_parser_parse_string(parser, NULL, source, source_len);
    assert(tree != NULL);
    TSNode root = ts_tree_root_node(tree);

    uint32_t error_offset;
    TSQueryError error_type;
    TSQuery* query = ts_query_new(ts_parser_language(parser), FDEF_QUERY, strlen(FDEF_QUERY), &error_offset, &error_type);
    assert(query != NULL);
    TSQueryCursor* cursor = ts_query_cursor_new();
    assert(cursor != NULL);

    ts_query_cursor_exec(cursor, query, root);
    TSQueryMatch match;
    while (ts_query_cursor_next_match(cursor, &match)) {
        FuncDef def = { .file = file };
        for (size_t i = 0; i < match.capture_count; ++i) {
            const TSQueryCapture* capture = &match.captures[i];
            uint32_t len;
            const char* capture_name = ts_query_capture_name_for_id(query, capture->index, &len);

            uint32_t start = ts_node_start_byte(capture->node);
            uint32_t end = ts_node_end_byte(capture->node);
            StringView sv = { source + start, end - start };

            if (strcmp(capture_name, "param_type") == 0) {
                da_append(&def.param_types, sv);
            } else if (strcmp(capture_name, "def") == 0) {
                TSPoint point = ts_node_start_point(capture->node);
                def.def = sv;
                def.line = point.row;
            } else if (strcmp(capture_name, "return_type") == 0) {
                def.return_type = sv;
            } else {
                assert(0);
            }

        }

        da_append(defs, def);
    }

    ts_query_cursor_delete(cursor);
    ts_query_delete(query);
    ts_parser_delete(parser);

    return true;
}

FuncDef parse_query(const char* query_source) {
    FuncDef def = {0};

    bool paren_opened = false;
    bool paren_closed = false;
    size_t query_source_len = strlen(query_source);
    for (size_t i = 0; i < query_source_len; ++i) {
        char c = query_source[i];
        const char* buf_start = query_source + i;

        if (isspace(c)) continue;

        if (c == ',') continue;

        if (c == '(') {
            if (paren_opened) {
                fprintf(stderr, "Unexpected %c\n", c);
                exit(1);
            }
            paren_opened = true;
            continue;
        }

        if (c == ')') {
            if (paren_closed) {
                fprintf(stderr, "Unexpected %c\n", c);
                exit(1);
            }
            paren_closed = true;
            continue;
        }

        if (c == '*') {
            StringView sv = { buf_start, 1 };
            
            if (def.return_type.len == 0) {
                def.return_type = sv;
            } else {
                da_append(&def.param_types, sv);
            }
            
            continue;
        }
        
        if (!isalpha(c)) {
            fprintf(stderr, "Unexpected %c\n", c);
            exit(1);
        }

        size_t buf_len = 0;
        while (isalpha(c)) {
            buf_len++;
            c = query_source[++i];
        }
        i--;

        StringView sv = { buf_start, buf_len };

        if (def.return_type.len == 0) {
            def.return_type = sv;
        } else {
            da_append(&def.param_types, sv);
        }
    }

    return def;
}

#ifdef DEBUG
static print_cache(size_t* cache, size_t an, size_t bn) {
    for (size_t i = 1; i <= an; ++i) {
        for (size_t j = 1; j <= bn; ++j) {
            printf("%ld ", CACHE_AT(i, j));
        }
        printf("\n");
    }
}
#endif // DEBUG

size_t levenshtein(const char* a, size_t an, const char* b, size_t bn) {
    if (an == 0) return bn;
    if (bn == 0) return an;

    size_t* cache = calloc((an + 1) * (bn + 1), sizeof(size_t));
#define CACHE_AT(i, j) cache[(i) * bn + (j)]

    for (size_t j = 0; j <= bn; ++j) {
        CACHE_AT(0, j) = j;
    }

    for (size_t i = 0; i <= an; ++i) {
        CACHE_AT(i, 0) = i;
    }

    for (size_t i = 1; i <= an; ++i) {
        for (size_t j = 1; j <= bn; ++j) {
            size_t cost = a[i] == b[j]? 0 : 1;
            CACHE_AT(i, j) = CACHE_AT(i - 1, j) + 1;
            if (CACHE_AT(i, j - 1) + 1 < CACHE_AT(i, j)) CACHE_AT(i, j) = CACHE_AT(i, j - 1) + 1;
            if (CACHE_AT(i - 1, j - 1) + cost < CACHE_AT(i, j)) CACHE_AT(i, j) = CACHE_AT(i - 1, j - 1) + cost;
        }
    }

    size_t value = cache[an * bn + bn];
    free(cache);

    return value;
}

char* read_entire_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) return NULL;

    fseek(f, 0, SEEK_END);
    size_t len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char* out = malloc(sizeof(char) * (len + 1));
    fread(out, 1, len, f);
    out[len] = 0;

    fclose(f);
    return out;
}

typedef struct {
    FuncDef* def;
    size_t dist;
}FuncDefDist;

FuncDefDist query_exec(FuncDef* def, FuncDef query) {
    size_t dist;
    if (query.return_type.len == 1 && query.return_type.start[0] == '*') dist = 0;
    else dist = levenshtein(def->return_type.start, def->return_type.len, query.return_type.start, query.return_type.len);

    for (size_t i = 0; i < def->param_types.count && i < query.param_types.count; ++i) {
        if (query.param_types.items[i].len == 1 && query.param_types.items[i].start[0] == '*') continue;
        dist += levenshtein(def->param_types.items[i].start, def->param_types.items[i].len, query.param_types.items[i].start, query.param_types.items[i].len);
    }

    for (size_t i = query.param_types.count; i < def->param_types.count; ++i) {
        dist += def->param_types.items[i].len;
    }

    for (size_t i = def->param_types.count; i < query.param_types.count; ++i) {
        if (query.param_types.items[i].len == 1 && query.param_types.items[i].start[0] == '*') continue;
        dist += query.param_types.items[i].len;
    }

    return (FuncDefDist) { def, dist };
}

int compare_funcdefdist(const void* va, const void* vb) {
    const FuncDefDist* a = va;
    const FuncDefDist* b = vb;

    return a->dist - b->dist;
}

typedef struct {
    FuncDefDist* items;
    size_t count;
    size_t capacity;
}FuncDefDists;

typedef struct {
    char** items;
    size_t count;
    size_t capacity;
}FilePaths;

const char* pop_argv(int* argc, char*** argv) {
    const char* arg = **argv;
    (*argv)++;
    (*argc)--;
    return arg;
}

void usage(FILE* sink, const char* program) {
    fprintf(sink, "Usage: %s <QUERY> <FILE...>\n", program);
    fprintf(sink, "Flags:\n");
    fprintf(sink, "    --all - Print ALL found defenitions, not top 15\n");
}

int main(int argc, char* argv[argc]) {
    const char* program = pop_argv(&argc, &argv);

    bool print_all = false;
    FilePaths source_files = {0};
    const char* query = NULL;

    while (argc > 0) {
        const char* arg = pop_argv(&argc, &argv);
        if (arg[0] == '-') {
            if (strcmp(arg, "--all") == 0) print_all = true;
            else if (strcmp(arg, "-h") == 0) {
                usage(stdout, program);
                exit(0);
            } else {
                fprintf(stderr, "Unknown flag: %s\n", arg);
                usage(stderr, program);
                exit(1);
            }

            continue;
        }

        if (query == NULL) {
            query = arg;
        } else {
            da_append(&source_files, (char*)arg);
        }
    }

    if (source_files.count == 0) {
        fprintf(stderr, "No source files provided\n");
        usage(stderr, program);
        exit(1);
    }

    if (query == NULL) {
        fprintf(stderr, "No query provided\n");
        usage(stderr, program);
        exit(1);
    }

    FuncDefs defs = {0};
    FuncDef query_compiled = parse_query(query); 

    da_foreach(&source_files, char*, source_file) {
        char* source = read_entire_file(*source_file);

        if (source == NULL) {
            fprintf(stderr, "Error loading %s: %s\n", *source_file, strerror(errno));
            continue;
        }

        get_defs(&defs, *source_file, source);
    }

    FuncDefDists def_dists = {0};

    da_foreach(&defs, FuncDef, def) {
        FuncDefDist dist = query_exec(def, query_compiled);
        da_append(&def_dists, dist);
    }

    qsort(def_dists.items, def_dists.count, sizeof(def_dists.items[0]), compare_funcdefdist);

    for (size_t i = 0; (i < 15 || print_all) && i < def_dists.count; ++i) {
        printf("Found: %s:%ld: '%.*s'\n", def_dists.items[i].def->file, def_dists.items[i].def->line, (int)def_dists.items[i].def->def.len, def_dists.items[i].def->def.start);
    }

    da_free(&def_dists);
    da_free(&defs);
    return 0;
}
