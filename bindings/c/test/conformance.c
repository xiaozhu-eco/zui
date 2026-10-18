#if defined(__cplusplus)
#error the C ABI conformance test is compiled as C
#endif

#include "zui_capi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#define zui_pid _getpid
#else
#include <unistd.h>
#define zui_pid getpid
#endif

static int g_fails = 0;

#define CHECK(cond)                                                                          \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                  \
            ++g_fails;                                                                       \
        }                                                                                    \
    } while (0)

static int write_file(const char* path, const char* text) {
    FILE* file = fopen(path, "wb");
    if (file == NULL) {
        return 0;
    }
    fputs(text, file);
    fclose(file);
    return 1;
}

static void temp_name(char* buf, size_t cap, const char* leaf) {
#ifdef _WIN32
    char dir[MAX_PATH];
    DWORD n = GetTempPathA((DWORD)sizeof(dir), dir);
    if (n == 0 || n >= sizeof(dir)) {
        snprintf(buf, cap, "%s", leaf);
        return;
    }
    snprintf(buf, cap, "%s%s", dir, leaf);
#else
    snprintf(buf, cap, "/tmp/%s", leaf);
#endif
}

static size_t put_str(const char* text, char* buf, size_t cap) {
    const size_t n = strlen(text);
    if (buf != NULL && cap > 0) {
        const size_t copy = n < cap ? n : cap - 1;
        if (copy > 0) {
            memcpy(buf, text, copy);
        }
        buf[copy] = '\0';
    }
    return n;
}

static int key_is(const char* key, size_t key_len, const char* literal) {
    const size_t n = strlen(literal);
    return key != NULL && key_len == n && memcmp(key, literal, n) == 0;
}

static int get_is(zui_app* app, const char* id, const char* field, const char* expect) {
    char buf[1024];
    const size_t n = zui_app_get(app, id, field, buf, sizeof(buf));
    return n == strlen(expect) && strcmp(buf, expect) == 0;
}

static int get_has(zui_app* app, const char* id, const char* field, const char* token) {
    char buf[1024];
    zui_app_get(app, id, field, buf, sizeof(buf));
    return strstr(buf, token) != NULL;
}

typedef struct Row {
    const char* id;
    const char* text;
} Row;

static Row g_rows[2];

static size_t host_rows(void* user) {
    (void)user;
    return 2;
}

static size_t host_value(void* user, size_t row, const char* key, size_t key_len, char* buf,
                         size_t cap) {
    (void)user;
    if (row >= 2) {
        return put_str("", buf, cap);
    }
    if (key_is(key, key_len, "id")) {
        return put_str(g_rows[row].id, buf, cap);
    }
    if (key_is(key, key_len, "text")) {
        return put_str(g_rows[row].text, buf, cap);
    }
    return put_str("", buf, cap);
}

static size_t host_key(void* user, size_t row, char* buf, size_t cap) {
    (void)user;
    if (row >= 2) {
        return put_str("", buf, cap);
    }
    return put_str(g_rows[row].id, buf, cap);
}

static int g_hits_a = 0;
static int g_hits_b = 0;

static void on_watch(const char* id, void* user) {
    (void)user;
    if (id != NULL && strcmp(id, "a") == 0) {
        ++g_hits_a;
    }
    if (id != NULL && strcmp(id, "b") == 0) {
        ++g_hits_b;
    }
}

static size_t table_rows(void* user) {
    (void)user;
    return 1;
}

static size_t table_cols(void* user) {
    (void)user;
    return 2;
}

static size_t table_col_key(void* user, size_t column, char* buf, size_t cap) {
    (void)user;
    if (column == 0) {
        return put_str("name", buf, cap);
    }
    if (column == 1) {
        return put_str("qty", buf, cap);
    }
    return put_str("", buf, cap);
}

static size_t table_cell(void* user, size_t row, size_t column, char* buf, size_t cap) {
    (void)user;
    (void)row;
    if (column == 0) {
        return put_str("Ada", buf, cap);
    }
    if (column == 1) {
        return put_str("4", buf, cap);
    }
    return put_str("", buf, cap);
}

static zui_list_model* g_child = NULL;

static size_t child_rows(void* user) {
    (void)user;
    return 1;
}

static size_t child_value(void* user, size_t row, const char* key, size_t key_len, char* buf,
                          size_t cap) {
    (void)user;
    (void)row;
    if (key_is(key, key_len, "text")) {
        return put_str("a", buf, cap);
    }
    return put_str("", buf, cap);
}

static size_t board_rows(void* user) {
    (void)user;
    return 1;
}

static size_t board_value(void* user, size_t row, const char* key, size_t key_len, char* buf,
                          size_t cap) {
    (void)user;
    (void)row;
    if (key_is(key, key_len, "name")) {
        return put_str("todo", buf, cap);
    }
    return put_str("", buf, cap);
}

static void board_children(void* user, size_t row, zui_list_visit_fn visit, void* visit_user) {
    (void)user;
    if (row == 0 && g_child != NULL && visit != NULL) {
        visit(g_child, visit_user);
    }
}

static void on_timer(void* user) { (void)user; }

static void test_layout(void) {
    zui_capi_layout layout;
    memset(&layout, 0, sizeof(layout));
    layout.struct_size = (uint32_t)sizeof(layout);
    zui_capi_fill_layout(&layout);
    CHECK(zui_capi_abi_version() == ZUI_CAPI_ABI_VERSION);
    CHECK(layout.abi_version == ZUI_CAPI_ABI_VERSION);
    CHECK(layout.event == (uint32_t)sizeof(zui_event));
    CHECK(layout.list_vtable == (uint32_t)sizeof(zui_list_vtable));
    CHECK(layout.table_vtable == (uint32_t)sizeof(zui_table_vtable));
    CHECK(layout.file_dialog == (uint32_t)sizeof(zui_file_dialog));
    CHECK(layout.element_box == (uint32_t)sizeof(zui_element_box));
    CHECK(layout.gamepad == (uint32_t)sizeof(zui_gamepad));
    CHECK(layout.pointer == (uint32_t)sizeof(zui_pointer));
}

static void test_null(void) {
    zui_app_destroy(NULL);
    CHECK(zui_app_load(NULL, "missing.html") == 0);
    CHECK(zui_app_get(NULL, "n", "text", NULL, 0) == 0);
    zui_app_bind(NULL, "on_save", NULL, NULL);
    CHECK(zui_app_run(NULL) == 1);
    CHECK(zui_app_pump_frame(NULL) == 0);
}

static void test_document(zui_app* app, const char* page, const char* fragment) {
    CHECK(zui_app_load(app, "zui-capi-no-such-file.html") == 0);
    CHECK(zui_app_load(app, page) == 1);
    CHECK(zui_app_set(app, "missing", "text", "x") == 0);
    CHECK(zui_app_set(app, "n", "style", "color:red") == 0);
    CHECK(zui_app_set(app, "n", "text", "Ready") == 1);
    CHECK(get_is(app, "n", "text", "Ready"));
    CHECK(zui_app_set(app, "n", "visible", "false") == 1);
    CHECK(get_is(app, "n", "visible", "false"));
    CHECK(zui_app_set(app, "n", "data-kind", "row") == 1);
    CHECK(get_is(app, "n", "data-kind", "row"));
    CHECK(zui_app_set(app, "n", "aria-label", "Name") == 1);
    CHECK(get_is(app, "n", "aria-label", "Name"));
    CHECK(get_has(app, "title", "class", "base"));
    CHECK(zui_app_add_class(app, "title", "error") == 1);
    CHECK(get_has(app, "title", "class", "error"));
    CHECK(zui_app_remove_class(app, "title", "error") == 1);
    CHECK(!get_has(app, "title", "class", "error"));
    CHECK(zui_app_set_theme(app, "dawn") == 1);
    CHECK(get_has(app, "page", "class", "theme-dawn"));
    CHECK(zui_app_set_title(app, "") == 0);
    CHECK(zui_app_set_title(app, "C API") == 1);
    CHECK(zui_app_set_window_size(app, 0, 10) == 0);
    CHECK(zui_app_set_window_size(app, 800, 600) == 1);

    CHECK(write_file(fragment, "<p id=\"frag\">hi</p>\n"));
    CHECK(zui_app_load_fragment_into(app, fragment, "slot") == 1);
    CHECK(get_is(app, "frag", "text", "hi"));
    // Replace is the slot's whole content: the appended node leaves even though the replacement
    // fragment does not name it.
    CHECK(write_file(fragment, "<p id=\"next\">next</p>\n"));
    CHECK(zui_app_replace_fragment(app, fragment, "slot") == 1);
    CHECK(get_is(app, "next", "text", "next"));
    CHECK(get_is(app, "frag", "text", ""));
    CHECK(zui_app_unload_fragment(app, "next") == 1);
    CHECK(get_is(app, "next", "text", ""));

    zui_element_box box;
    memset(&box, 0, sizeof(box));
    box.struct_size = (uint32_t)sizeof(box);
    CHECK(zui_app_measure(app, "missing", &box) == 0);

    const uint64_t timer = zui_app_set_timeout(app, on_timer, NULL, 1000);
    CHECK(timer != 0);
    zui_app_clear_timer(app, timer);
    zui_app_defer(app, on_timer, NULL);
    zui_app_bind(app, "on_save", NULL, NULL);
    zui_app_unbind(app, "on_save");
}

static void test_models(zui_app* app) {
    g_rows[0].id = "a";
    g_rows[0].text = "one";
    g_rows[1].id = "b";
    g_rows[1].text = "two";
    zui_list_vtable list;
    memset(&list, 0, sizeof(list));
    list.struct_size = (uint32_t)sizeof(list);
    list.row_count = host_rows;
    list.value = host_value;
    list.row_key = host_key;
    zui_list_model* model = zui_list_model_create(&list, NULL);
    CHECK(model != NULL);
    CHECK(zui_app_set_model(app, "missing", model) == 0);
    CHECK(zui_app_set_model(app, "host", model) == 1);
    CHECK(get_is(app, "a", "text", "one"));
    CHECK(get_is(app, "b", "text", "two"));

    g_hits_a = 0;
    g_hits_b = 0;
    const uint64_t watch_a = zui_app_watch(app, "a", on_watch, NULL);
    const uint64_t watch_b = zui_app_watch(app, "b", on_watch, NULL);
    CHECK(watch_a != 0);
    CHECK(watch_b != 0);
    CHECK(zui_app_set_model(app, "host", model) == 1);
    CHECK(g_hits_a == 0);
    CHECK(g_hits_b == 0);
    g_rows[0].text = "one-b";
    CHECK(zui_app_set_model(app, "host", model) == 1);
    CHECK(g_hits_a == 1);
    CHECK(g_hits_b == 0);
    CHECK(get_is(app, "a", "text", "one-b"));
    zui_app_unwatch(app, watch_a);
    g_rows[0].text = "one-c";
    CHECK(zui_app_set_model(app, "host", model) == 1);
    CHECK(g_hits_a == 1);
    CHECK(zui_app_set(app, "b", "data-mark", "yes") == 1);
    CHECK(g_hits_b == 1);
    CHECK(get_is(app, "b", "data-mark", "yes"));
    zui_list_model_destroy(model);

    zui_table_vtable table;
    memset(&table, 0, sizeof(table));
    table.struct_size = (uint32_t)sizeof(table);
    table.row_count = table_rows;
    table.column_count = table_cols;
    table.column_key = table_col_key;
    table.cell = table_cell;
    zui_list_model* grid = zui_table_model_create(&table, NULL);
    CHECK(grid != NULL);
    CHECK(zui_app_set_model(app, "grid", grid) == 1);
    CHECK(get_is(app, "Ada", "text", "4"));
    zui_list_model_destroy(grid);

    zui_list_vtable child_vt;
    memset(&child_vt, 0, sizeof(child_vt));
    child_vt.struct_size = (uint32_t)sizeof(child_vt);
    child_vt.row_count = child_rows;
    child_vt.value = child_value;
    g_child = zui_list_model_create(&child_vt, NULL);
    CHECK(g_child != NULL);
    zui_list_vtable board;
    memset(&board, 0, sizeof(board));
    board.struct_size = (uint32_t)sizeof(board);
    board.row_count = board_rows;
    board.value = board_value;
    board.with_children = board_children;
    zui_list_model* columns = zui_list_model_create(&board, NULL);
    CHECK(columns != NULL);
    CHECK(zui_app_set_model(app, "board", columns) == 1);
    CHECK(get_has(app, "col-todo", "class", "col"));
    CHECK(get_is(app, "card-a", "text", "a"));
    zui_list_model_destroy(columns);
    zui_list_model_destroy(g_child);
    g_child = NULL;
}

static int g_perm_seen = -1;

static void on_permission(zui_permission_state state, void* user) {
    int* out = (int*)user;
    *out = (int)state;
}

static void test_host_without_window(zui_app* app) {
    int status = -1;
    char buf[8];
    zui_app_clipboard_text(app, &status, buf, sizeof(buf));
    CHECK(status == ZUI_CLIPBOARD_UNSUPPORTED);
    CHECK(zui_app_set_clipboard_text(app, "x") == 0);
    CHECK(zui_app_permission_state(app, ZUI_PERMISSION_CAMERA) == ZUI_PERMISSION_UNSUPPORTED);
    CHECK(zui_app_permission_state(app, ZUI_PERMISSION_STORAGE_WRITE) ==
          ZUI_PERMISSION_UNSUPPORTED);
    g_perm_seen = -1;
    zui_app_request_permission(app, ZUI_PERMISSION_MANAGE_STORAGE, on_permission, &g_perm_seen);
    CHECK(g_perm_seen == ZUI_PERMISSION_UNSUPPORTED);
    CHECK(zui_app_open_uri(app, "javascript:alert(1)") == 0);
    CHECK(zui_app_focus(app, "missing") == 0);
    CHECK(zui_app_scroll_into_view(app, "missing", ZUI_SCROLL_START) == 0);
    CHECK(zui_app_minimize(app) == 0);
    zui_gamepad pad;
    memset(&pad, 0, sizeof(pad));
    pad.struct_size = (uint32_t)sizeof(pad);
    zui_app_gamepad(app, 0, &pad);
    CHECK(pad.connected == 0);
    zui_display display;
    memset(&display, 0, sizeof(display));
    display.struct_size = (uint32_t)sizeof(display);
    zui_app_display(app, &display);
    CHECK(display.known == 0);
    CHECK(zui_app_pump_frame(app) == 0);
}

int main(void) {
    char page[512];
    char fragment[512];
    char leaf[64];
    snprintf(leaf, sizeof(leaf), "zui-capi-%d.html", zui_pid());
    temp_name(page, sizeof(page), leaf);
    snprintf(leaf, sizeof(leaf), "zui-capi-frag-%d.html", zui_pid());
    temp_name(fragment, sizeof(fragment), leaf);

    test_layout();
    test_null();

    const char* html =
        "<!DOCTYPE html><html id=\"page\"><body>"
        "<h1 id=\"title\" class=\"base\">Title</h1>"
        "<p id=\"n\">n</p><div id=\"slot\"></div>"
        "<ul id=\"host\"><template><li id=\"{{id}}\">{{text}}</li></template></ul>"
        "<ul id=\"grid\"><template><li id=\"{{name}}\">{{qty}}</li></template></ul>"
        "<div id=\"board\"><template><section class=\"col\" id=\"col-{{name}}\">"
        "<h3>{{name}}</h3><ul class=\"cards\"><template>"
        "<li class=\"card\" id=\"card-{{text}}\">{{text}}</li>"
        "</template></ul></section></template></div>"
        "</body></html>\n";
    CHECK(write_file(page, html));

    zui_app* app = zui_app_create(0, NULL);
    CHECK(app != NULL);
    if (app != NULL) {
        test_document(app, page, fragment);
        test_models(app);
        test_host_without_window(app);
        zui_app_destroy(app);
    }
    remove(page);
    remove(fragment);
    if (g_fails != 0) {
        fprintf(stderr, "%d check(s) failed\n", g_fails);
        return 1;
    }
    printf("capi conformance ok\n");
    return 0;
}
