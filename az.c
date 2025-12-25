/*
 * AZ Editor v1.1 - A minimal terminal text editor for Windows
 * Features: Dark theme, directory sidebar, mouse support, intuitive motions
 * Compile: gcc az.c -o az.exe -O2
 */

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <direct.h>

#define AZ_VERSION "1.1"
#define TAB_SIZE 4
#define MAX_LINES 50000
#define MAX_LINE_LEN 4096
#define SIDEBAR_WIDTH 30
#define STATUS_HEIGHT 2
#define MAX_DIR_ENTRIES 1000
#define MAX_UNDO 100

/* Editor modes */
typedef enum {
    MODE_NORMAL,
    MODE_INSERT,
    MODE_COMMAND,
    MODE_SEARCH,
    MODE_SIDEBAR
} EditorMode;

/* Directory entry for sidebar */
typedef struct {
    char name[260];
    int is_dir;
} DirEntry;

/* Undo state */
typedef struct {
    char **lines;
    int num_lines;
    int cx, cy;
} UndoState;

/* Selection */
typedef struct {
    int active;
    int start_x, start_y;
    int end_x, end_y;
} Selection;

/* Editor state */
typedef struct {
    char **lines;
    int num_lines;
    int cx, cy;
    int row_offset;
    int col_offset;
    int screen_rows;
    int screen_cols;
    char filename[512];
    int modified;
    EditorMode mode;
    EditorMode prev_mode;
    char status_msg[256];
    char command_buf[256];
    int command_len;
    char search_buf[256];
    int search_len;
    
    int sidebar_visible;
    DirEntry dir_entries[MAX_DIR_ENTRIES];
    int num_entries;
    int sidebar_scroll;
    int sidebar_cursor;
    char current_dir[512];
    
    char *clipboard;
    Selection sel;
    
    UndoState undo_stack[MAX_UNDO];
    int undo_count;
    int undo_pos;
    
    HANDLE hStdout;
    HANDLE hStdin;
    HANDLE hBuffer;
    DWORD orig_in_mode;
    DWORD orig_out_mode;
    
    /* Double buffer */
    CHAR_INFO *buffer;
    int buf_size;
    
    int dirty;
} Editor;

Editor E;

/* Colors - Dark theme */
#define CLR_DEFAULT     (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)
#define CLR_YELLOW      (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY)
#define CLR_CYAN        (FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY)
#define CLR_GREEN       (FOREGROUND_GREEN | FOREGROUND_INTENSITY)
#define CLR_MAGENTA     (FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY)
#define CLR_WHITE       (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY)
#define CLR_GRAY        (FOREGROUND_INTENSITY)
#define CLR_RED         (FOREGROUND_RED | FOREGROUND_INTENSITY)

#define BG_BLACK        0
#define BG_BLUE         (BACKGROUND_BLUE)
#define BG_CYAN         (BACKGROUND_GREEN | BACKGROUND_BLUE)
#define BG_GRAY         (BACKGROUND_INTENSITY)
#define BG_SELECT       (BACKGROUND_BLUE | BACKGROUND_INTENSITY)

/* Function prototypes */
void editor_init(void);
void editor_free(void);
void editor_open(const char *filename);
void editor_save(void);
void editor_draw(void);
void editor_process_key(void);
void editor_set_status(const char *fmt, ...);
void sidebar_load_dir(const char *path);
void push_undo(void);
void pop_undo(void);

/* Buffer drawing functions */
void buf_clear(void) {
    for (int i = 0; i < E.buf_size; i++) {
        E.buffer[i].Char.AsciiChar = ' ';
        E.buffer[i].Attributes = CLR_DEFAULT | BG_BLACK;
    }
}

void buf_set(int x, int y, char c, WORD attr) {
    if (x < 0 || x >= E.screen_cols || y < 0 || y >= E.screen_rows + STATUS_HEIGHT) return;
    int idx = y * E.screen_cols + x;
    E.buffer[idx].Char.AsciiChar = c;
    E.buffer[idx].Attributes = attr;
}

void buf_write(int x, int y, const char *str, WORD attr) {
    int len = strlen(str);
    for (int i = 0; i < len && x + i < E.screen_cols; i++) {
        buf_set(x + i, y, str[i], attr);
    }
}

void buf_fill_line(int y, char c, WORD attr) {
    for (int x = 0; x < E.screen_cols; x++) {
        buf_set(x, y, c, attr);
    }
}

void buf_flush(void) {
    COORD bufSize = { (SHORT)E.screen_cols, (SHORT)(E.screen_rows + STATUS_HEIGHT) };
    COORD bufCoord = { 0, 0 };
    SMALL_RECT region = { 0, 0, (SHORT)(E.screen_cols - 1), (SHORT)(E.screen_rows + STATUS_HEIGHT - 1) };
    WriteConsoleOutput(E.hStdout, E.buffer, bufSize, bufCoord, &region);
}

void set_cursor(int x, int y) {
    COORD pos = { (SHORT)x, (SHORT)y };
    SetConsoleCursorPosition(E.hStdout, pos);
}

/* Selection helpers */
int is_selected(int x, int y) {
    if (!E.sel.active) return 0;
    
    int sy = E.sel.start_y, sx = E.sel.start_x;
    int ey = E.sel.end_y, ex = E.sel.end_x;
    
    /* Normalize selection */
    if (sy > ey || (sy == ey && sx > ex)) {
        int ty = sy, tx = sx;
        sy = ey; sx = ex;
        ey = ty; ex = tx;
    }
    
    if (y < sy || y > ey) return 0;
    if (y == sy && y == ey) return x >= sx && x <= ex;
    if (y == sy) return x >= sx;
    if (y == ey) return x <= ex;
    return 1;
}

void clear_selection(void) {
    E.sel.active = 0;
}

/* Undo system */
void push_undo(void) {
    /* Free oldest if full */
    if (E.undo_count >= MAX_UNDO) {
        UndoState *old = &E.undo_stack[0];
        if (old->lines) {
            for (int i = 0; i < old->num_lines; i++) free(old->lines[i]);
            free(old->lines);
        }
        memmove(&E.undo_stack[0], &E.undo_stack[1], sizeof(UndoState) * (MAX_UNDO - 1));
        E.undo_count--;
    }
    
    /* Clear any redo states */
    for (int i = E.undo_pos; i < E.undo_count; i++) {
        UndoState *s = &E.undo_stack[i];
        if (s->lines) {
            for (int j = 0; j < s->num_lines; j++) free(s->lines[j]);
            free(s->lines);
            s->lines = NULL;
        }
    }
    E.undo_count = E.undo_pos;
    
    /* Save current state */
    UndoState *state = &E.undo_stack[E.undo_count];
    state->lines = malloc(sizeof(char*) * E.num_lines);
    state->num_lines = E.num_lines;
    state->cx = E.cx;
    state->cy = E.cy;
    for (int i = 0; i < E.num_lines; i++) {
        state->lines[i] = _strdup(E.lines[i]);
    }
    E.undo_count++;
    E.undo_pos = E.undo_count;
}

void pop_undo(void) {
    if (E.undo_pos <= 0) {
        editor_set_status("Nothing to undo");
        return;
    }
    
    E.undo_pos--;
    UndoState *state = &E.undo_stack[E.undo_pos];
    
    /* Free current */
    for (int i = 0; i < E.num_lines; i++) free(E.lines[i]);
    free(E.lines);
    
    /* Restore */
    E.lines = malloc(sizeof(char*) * state->num_lines);
    E.num_lines = state->num_lines;
    for (int i = 0; i < E.num_lines; i++) {
        E.lines[i] = _strdup(state->lines[i]);
    }
    E.cx = state->cx;
    E.cy = state->cy;
    
    if (E.cy >= E.num_lines) E.cy = E.num_lines - 1;
    if (E.cx > (int)strlen(E.lines[E.cy])) E.cx = strlen(E.lines[E.cy]);
    
    E.dirty = 1;
    editor_set_status("Undo");
}

void editor_init(void) {
    memset(&E, 0, sizeof(E));
    
    E.mode = MODE_NORMAL;
    _getcwd(E.current_dir, sizeof(E.current_dir));
    
    /* Console setup */
    E.hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    E.hStdin = GetStdHandle(STD_INPUT_HANDLE);
    
    GetConsoleMode(E.hStdin, &E.orig_in_mode);
    GetConsoleMode(E.hStdout, &E.orig_out_mode);
    
    SetConsoleMode(E.hStdin, ENABLE_EXTENDED_FLAGS | ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT);
    SetConsoleMode(E.hStdout, E.orig_out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    
    /* Get screen size */
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(E.hStdout, &csbi);
    E.screen_cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    E.screen_rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1 - STATUS_HEIGHT;
    
    /* Allocate double buffer */
    E.buf_size = E.screen_cols * (E.screen_rows + STATUS_HEIGHT);
    E.buffer = malloc(sizeof(CHAR_INFO) * E.buf_size);
    
    /* Hide cursor blink during refresh */
    CONSOLE_CURSOR_INFO cci = { 25, TRUE };
    SetConsoleCursorInfo(E.hStdout, &cci);
    
    /* Create empty buffer */
    E.lines = malloc(sizeof(char*));
    E.lines[0] = _strdup("");
    E.num_lines = 1;
    E.dirty = 1;
}

void editor_free(void) {
    for (int i = 0; i < E.num_lines; i++) free(E.lines[i]);
    free(E.lines);
    if (E.clipboard) free(E.clipboard);
    if (E.buffer) free(E.buffer);
    
    for (int i = 0; i < E.undo_count; i++) {
        if (E.undo_stack[i].lines) {
            for (int j = 0; j < E.undo_stack[i].num_lines; j++)
                free(E.undo_stack[i].lines[j]);
            free(E.undo_stack[i].lines);
        }
    }
    
    SetConsoleMode(E.hStdin, E.orig_in_mode);
    SetConsoleMode(E.hStdout, E.orig_out_mode);
    
    /* Clear screen on exit */
    COORD pos = {0, 0};
    DWORD written;
    FillConsoleOutputCharacter(E.hStdout, ' ', E.screen_cols * (E.screen_rows + STATUS_HEIGHT), pos, &written);
    FillConsoleOutputAttribute(E.hStdout, CLR_DEFAULT, E.screen_cols * (E.screen_rows + STATUS_HEIGHT), pos, &written);
    SetConsoleCursorPosition(E.hStdout, pos);
}

void editor_open(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        strncpy(E.filename, filename, sizeof(E.filename) - 1);
        editor_set_status("New file: %s", filename);
        return;
    }
    
    strncpy(E.filename, filename, sizeof(E.filename) - 1);
    
    for (int i = 0; i < E.num_lines; i++) free(E.lines[i]);
    free(E.lines);
    E.lines = NULL;
    E.num_lines = 0;
    
    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), fp)) {
        int len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        
        E.lines = realloc(E.lines, sizeof(char*) * (E.num_lines + 1));
        E.lines[E.num_lines++] = _strdup(line);
    }
    fclose(fp);
    
    if (E.num_lines == 0) {
        E.lines = malloc(sizeof(char*));
        E.lines[0] = _strdup("");
        E.num_lines = 1;
    }
    
    E.cy = E.cx = 0;
    E.modified = 0;
    E.dirty = 1;
    clear_selection();
    
    /* Clear undo stack for new file */
    for (int i = 0; i < E.undo_count; i++) {
        if (E.undo_stack[i].lines) {
            for (int j = 0; j < E.undo_stack[i].num_lines; j++)
                free(E.undo_stack[i].lines[j]);
            free(E.undo_stack[i].lines);
            E.undo_stack[i].lines = NULL;
        }
    }
    E.undo_count = E.undo_pos = 0;
    
    editor_set_status("Opened: %s (%d lines)", filename, E.num_lines);
}

void editor_save(void) {
    if (E.filename[0] == '\0') {
        editor_set_status("No filename! Use :w <filename>");
        return;
    }
    
    FILE *fp = fopen(E.filename, "w");
    if (!fp) {
        editor_set_status("Error: Cannot save file!");
        return;
    }
    
    int bytes = 0;
    for (int i = 0; i < E.num_lines; i++) {
        fprintf(fp, "%s\n", E.lines[i]);
        bytes += strlen(E.lines[i]) + 1;
    }
    fclose(fp);
    
    E.modified = 0;
    E.dirty = 1;
    editor_set_status("Saved: %s (%d bytes)", E.filename, bytes);
}

void editor_set_status(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.status_msg, sizeof(E.status_msg), fmt, ap);
    va_end(ap);
    E.dirty = 1;
}

void editor_insert_char(int c) {
    push_undo();
    char *line = E.lines[E.cy];
    int len = strlen(line);
    E.lines[E.cy] = realloc(line, len + 2);
    line = E.lines[E.cy];
    memmove(&line[E.cx + 1], &line[E.cx], len - E.cx + 1);
    line[E.cx++] = c;
    E.modified = 1;
    E.dirty = 1;
}

void editor_delete_char(void) {
    if (E.cy == E.num_lines) return;
    if (E.cx == 0 && E.cy == 0) return;
    
    push_undo();
    char *line = E.lines[E.cy];
    int len = strlen(line);
    
    if (E.cx > 0) {
        memmove(&line[E.cx - 1], &line[E.cx], len - E.cx + 1);
        E.cx--;
    } else {
        char *prev = E.lines[E.cy - 1];
        int prev_len = strlen(prev);
        E.lines[E.cy - 1] = realloc(prev, prev_len + len + 1);
        strcat(E.lines[E.cy - 1], line);
        free(E.lines[E.cy]);
        memmove(&E.lines[E.cy], &E.lines[E.cy + 1], sizeof(char*) * (E.num_lines - E.cy - 1));
        E.num_lines--;
        E.cy--;
        E.cx = prev_len;
    }
    E.modified = 1;
    E.dirty = 1;
}

void editor_insert_newline(void) {
    push_undo();
    char *line = E.lines[E.cy];
    char *new_line = _strdup(&line[E.cx]);
    line[E.cx] = '\0';
    
    E.lines = realloc(E.lines, sizeof(char*) * (E.num_lines + 1));
    memmove(&E.lines[E.cy + 2], &E.lines[E.cy + 1], sizeof(char*) * (E.num_lines - E.cy - 1));
    E.lines[E.cy + 1] = new_line;
    E.num_lines++;
    E.cy++;
    E.cx = 0;
    E.modified = 1;
    E.dirty = 1;
}

void editor_delete_line(void) {
    if (E.num_lines <= 1) {
        push_undo();
        free(E.lines[0]);
        E.lines[0] = _strdup("");
        E.cx = 0;
        E.modified = 1;
        E.dirty = 1;
        return;
    }
    
    push_undo();
    if (E.clipboard) free(E.clipboard);
    E.clipboard = _strdup(E.lines[E.cy]);
    
    free(E.lines[E.cy]);
    memmove(&E.lines[E.cy], &E.lines[E.cy + 1], sizeof(char*) * (E.num_lines - E.cy - 1));
    E.num_lines--;
    
    if (E.cy >= E.num_lines) E.cy = E.num_lines - 1;
    if (E.cx > (int)strlen(E.lines[E.cy])) E.cx = strlen(E.lines[E.cy]);
    E.modified = 1;
    E.dirty = 1;
}

void editor_copy_line(void) {
    if (E.clipboard) free(E.clipboard);
    E.clipboard = _strdup(E.lines[E.cy]);
    editor_set_status("Line yanked");
}

void editor_paste(void) {
    if (!E.clipboard) {
        editor_set_status("Nothing to paste");
        return;
    }
    push_undo();
    E.lines = realloc(E.lines, sizeof(char*) * (E.num_lines + 1));
    memmove(&E.lines[E.cy + 2], &E.lines[E.cy + 1], sizeof(char*) * (E.num_lines - E.cy - 1));
    E.lines[E.cy + 1] = _strdup(E.clipboard);
    E.num_lines++;
    E.cy++;
    E.cx = 0;
    E.modified = 1;
    E.dirty = 1;
    editor_set_status("Pasted");
}

void editor_scroll(void) {
    int editor_width = E.screen_cols - (E.sidebar_visible ? SIDEBAR_WIDTH : 0) - 6;
    
    if (E.cy < E.row_offset) E.row_offset = E.cy;
    if (E.cy >= E.row_offset + E.screen_rows) E.row_offset = E.cy - E.screen_rows + 1;
    if (E.cx < E.col_offset) E.col_offset = E.cx;
    if (E.cx >= E.col_offset + editor_width) E.col_offset = E.cx - editor_width + 1;
}

void sidebar_load_dir(const char *path) {
    E.num_entries = 0;
    E.sidebar_cursor = 0;
    E.sidebar_scroll = 0;
    strncpy(E.current_dir, path, sizeof(E.current_dir) - 1);
    
    strcpy(E.dir_entries[E.num_entries].name, "..");
    E.dir_entries[E.num_entries++].is_dir = 1;
    
    char search_path[520];
    snprintf(search_path, sizeof(search_path), "%s\\*", path);
    
    WIN32_FIND_DATA ffd;
    HANDLE hFind = FindFirstFile(search_path, &ffd);
    
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0) continue;
            if (E.num_entries >= MAX_DIR_ENTRIES) break;
            
            strncpy(E.dir_entries[E.num_entries].name, ffd.cFileName, 259);
            E.dir_entries[E.num_entries].is_dir = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            E.num_entries++;
        } while (FindNextFile(hFind, &ffd));
        FindClose(hFind);
    }
    E.dirty = 1;
}

void sidebar_open_selected(void) {
    if (E.sidebar_cursor >= E.num_entries) return;
    
    char path[1024];
    if (strcmp(E.dir_entries[E.sidebar_cursor].name, "..") == 0) {
        char *last_sep = strrchr(E.current_dir, '\\');
        if (!last_sep) last_sep = strrchr(E.current_dir, '/');
        if (last_sep && last_sep != E.current_dir) {
            char parent[512];
            strncpy(parent, E.current_dir, last_sep - E.current_dir);
            parent[last_sep - E.current_dir] = '\0';
            sidebar_load_dir(parent);
        }
    } else {
        snprintf(path, sizeof(path), "%s\\%s", E.current_dir, E.dir_entries[E.sidebar_cursor].name);
        if (E.dir_entries[E.sidebar_cursor].is_dir) {
            sidebar_load_dir(path);
        } else {
            editor_open(path);
            E.mode = MODE_NORMAL;
        }
    }
}

void sidebar_draw(void) {
    if (!E.sidebar_visible) return;
    
    for (int y = 0; y < E.screen_rows; y++) {
        int idx = y + E.sidebar_scroll;
        WORD attr = CLR_CYAN | BG_BLACK;
        
        if (idx < E.num_entries) {
            if (idx == E.sidebar_cursor) {
                attr = (E.mode == MODE_SIDEBAR) ? (CLR_WHITE | BG_CYAN) : (CLR_CYAN | BG_GRAY);
            }
            
            char display[SIDEBAR_WIDTH];
            memset(display, ' ', SIDEBAR_WIDTH - 1);
            display[SIDEBAR_WIDTH - 1] = '\0';
            
            char temp[SIDEBAR_WIDTH];
            if (E.dir_entries[idx].is_dir) {
                snprintf(temp, SIDEBAR_WIDTH - 3, " [%s]", E.dir_entries[idx].name);
            } else {
                snprintf(temp, SIDEBAR_WIDTH - 3, "  %s", E.dir_entries[idx].name);
            }
            int len = strlen(temp);
            if (len > SIDEBAR_WIDTH - 2) len = SIDEBAR_WIDTH - 2;
            memcpy(display, temp, len);
            
            buf_write(0, y, display, attr);
        } else {
            for (int x = 0; x < SIDEBAR_WIDTH - 1; x++)
                buf_set(x, y, ' ', CLR_CYAN | BG_BLACK);
        }
        buf_set(SIDEBAR_WIDTH - 1, y, '|', CLR_YELLOW | BG_BLACK);
    }
}

void editor_draw(void) {
    buf_clear();
    
    int start_col = E.sidebar_visible ? SIDEBAR_WIDTH : 0;
    int editor_width = E.screen_cols - start_col - 6;
    
    /* Draw text area */
    for (int y = 0; y < E.screen_rows; y++) {
        int file_row = y + E.row_offset;
        
        if (file_row < E.num_lines) {
            /* Line numbers */
            char linenum[8];
            snprintf(linenum, sizeof(linenum), "%5d ", file_row + 1);
            WORD ln_attr = (file_row == E.cy) ? (CLR_YELLOW | BG_BLUE) : (CLR_YELLOW | BG_BLACK);
            buf_write(start_col, y, linenum, ln_attr);
            
            /* Line content */
            char *line = E.lines[file_row];
            int len = strlen(line);
            int is_current = (file_row == E.cy);
            WORD base_attr = is_current ? (CLR_WHITE | BG_BLUE) : (CLR_DEFAULT | BG_BLACK);
            
            for (int i = 0; i < editor_width; i++) {
                int file_col = i + E.col_offset;
                char c = (file_col < len) ? line[file_col] : ' ';
                WORD attr = base_attr;
                
                if (is_selected(file_col, file_row)) {
                    attr = CLR_WHITE | BG_SELECT;
                }
                
                buf_set(start_col + 6 + i, y, c, attr);
            }
        } else {
            buf_write(start_col, y, "    ~ ", CLR_GRAY | BG_BLACK);
        }
    }
    
    /* Sidebar */
    sidebar_draw();
    
    /* Status bar */
    buf_fill_line(E.screen_rows, ' ', CLR_WHITE | BG_GRAY);
    
    const char *mode_str = "NORMAL";
    WORD mode_color = CLR_GREEN;
    switch (E.mode) {
        case MODE_INSERT: mode_str = "INSERT"; mode_color = CLR_YELLOW; break;
        case MODE_COMMAND: mode_str = "COMMAND"; mode_color = CLR_CYAN; break;
        case MODE_SEARCH: mode_str = "SEARCH"; mode_color = CLR_MAGENTA; break;
        case MODE_SIDEBAR: mode_str = "BROWSE"; mode_color = CLR_CYAN; break;
        default: break;
    }
    
    char status[256];
    snprintf(status, sizeof(status), " [%s] %s%s | Ln %d, Col %d | %d lines",
             mode_str,
             E.filename[0] ? E.filename : "[No Name]",
             E.modified ? " [+]" : "",
             E.cy + 1, E.cx + 1, E.num_lines);
    buf_write(0, E.screen_rows, status, CLR_WHITE | BG_GRAY);
    
    /* Message line */
    buf_fill_line(E.screen_rows + 1, ' ', CLR_DEFAULT | BG_BLACK);
    
    if (E.mode == MODE_COMMAND) {
        char cmd[260];
        snprintf(cmd, sizeof(cmd), ":%s", E.command_buf);
        buf_write(0, E.screen_rows + 1, cmd, CLR_GREEN | BG_BLACK);
    } else if (E.mode == MODE_SEARCH) {
        char srch[260];
        snprintf(srch, sizeof(srch), "/%s", E.search_buf);
        buf_write(0, E.screen_rows + 1, srch, CLR_CYAN | BG_BLACK);
    } else {
        buf_write(1, E.screen_rows + 1, E.status_msg, CLR_DEFAULT | BG_BLACK);
    }
    
    buf_flush();
    
    /* Position cursor */
    int cursor_y = E.cy - E.row_offset;
    int cursor_x = E.cx - E.col_offset + start_col + 6;
    
    if (E.mode == MODE_COMMAND) {
        set_cursor(E.command_len + 1, E.screen_rows + 1);
    } else if (E.mode == MODE_SEARCH) {
        set_cursor(E.search_len + 1, E.screen_rows + 1);
    } else if (E.mode == MODE_SIDEBAR) {
        set_cursor(1, E.sidebar_cursor - E.sidebar_scroll);
    } else {
        set_cursor(cursor_x, cursor_y);
    }
}

void editor_move_cursor(int key, int is_vk) {
    char *line = (E.cy < E.num_lines) ? E.lines[E.cy] : NULL;
    int len = line ? strlen(line) : 0;
    
    if (is_vk) {
        /* Virtual key codes */
        switch (key) {
            case VK_LEFT:
                if (E.cx > 0) E.cx--;
                break;
            case VK_RIGHT:
                if (E.cx < len) E.cx++;
                break;
            case VK_UP:
                if (E.cy > 0) E.cy--;
                break;
            case VK_DOWN:
                if (E.cy < E.num_lines - 1) E.cy++;
                break;
            case VK_HOME:
                E.cx = 0;
                break;
            case VK_END:
                E.cx = len;
                break;
            case VK_PRIOR:
                E.cy -= E.screen_rows;
                if (E.cy < 0) E.cy = 0;
                break;
            case VK_NEXT:
                E.cy += E.screen_rows;
                if (E.cy >= E.num_lines) E.cy = E.num_lines - 1;
                break;
        }
    } else {
        /* Character keys */
        switch (key) {
            case 'h':
                if (E.cx > 0) E.cx--;
                break;
            case 'l':
                if (E.cx < len) E.cx++;
                break;
            case 'k':
                if (E.cy > 0) E.cy--;
                break;
            case 'j':
                if (E.cy < E.num_lines - 1) E.cy++;
                break;
            case '0':
                E.cx = 0;
                break;
            case '$':
                E.cx = len;
                break;
        }
    }
    
    /* Snap to line end */
    line = (E.cy < E.num_lines) ? E.lines[E.cy] : NULL;
    len = line ? strlen(line) : 0;
    if (E.cx > len) E.cx = len;
    E.dirty = 1;
}

void editor_word_forward(void) {
    char *line = E.lines[E.cy];
    int len = strlen(line);
    
    while (E.cx < len && !isspace(line[E.cx])) E.cx++;
    while (E.cx < len && isspace(line[E.cx])) E.cx++;
    
    if (E.cx >= len && E.cy < E.num_lines - 1) {
        E.cy++;
        E.cx = 0;
        line = E.lines[E.cy];
        while (E.cx < (int)strlen(line) && isspace(line[E.cx])) E.cx++;
    }
    E.dirty = 1;
}

void editor_word_backward(void) {
    char *line = E.lines[E.cy];
    
    if (E.cx == 0 && E.cy > 0) {
        E.cy--;
        E.cx = strlen(E.lines[E.cy]);
        line = E.lines[E.cy];
    }
    
    if (E.cx > 0) E.cx--;
    while (E.cx > 0 && isspace(line[E.cx])) E.cx--;
    while (E.cx > 0 && !isspace(line[E.cx - 1])) E.cx--;
    E.dirty = 1;
}

void editor_search(void) {
    if (E.search_len == 0) return;
    
    int found = 0;
    int start_y = E.cy, start_x = E.cx + 1;
    
    for (int y = start_y; y < E.num_lines && !found; y++) {
        char *match = strstr(E.lines[y] + (y == start_y ? start_x : 0), E.search_buf);
        if (match) {
            E.cy = y;
            E.cx = match - E.lines[y];
            found = 1;
        }
    }
    
    if (!found) {
        for (int y = 0; y <= start_y && !found; y++) {
            char *match = strstr(E.lines[y], E.search_buf);
            if (match && (y < start_y || (match - E.lines[y]) < start_x)) {
                E.cy = y;
                E.cx = match - E.lines[y];
                found = 1;
            }
        }
    }
    
    editor_set_status(found ? "Found: '%s'" : "Not found: '%s'", E.search_buf);
}

void editor_process_command(void) {
    char *cmd = E.command_buf;
    
    /* Trim leading spaces */
    while (*cmd == ' ') cmd++;
    
    if (strcmp(cmd, "q") == 0) {
        if (E.modified) {
            editor_set_status("Unsaved changes! Use :q! to force quit or :w to save");
        } else {
            editor_free();
            exit(0);
        }
    } else if (strcmp(cmd, "q!") == 0) {
        editor_free();
        exit(0);
    } else if (strcmp(cmd, "w") == 0) {
        editor_save();
    } else if (strncmp(cmd, "w ", 2) == 0) {
        char *fname = cmd + 2;
        while (*fname == ' ') fname++;
        strncpy(E.filename, fname, sizeof(E.filename) - 1);
        editor_save();
    } else if (strcmp(cmd, "wq") == 0 || strcmp(cmd, "x") == 0) {
        editor_save();
        editor_free();
        exit(0);
    } else if (strncmp(cmd, "e ", 2) == 0) {
        char *fname = cmd + 2;
        while (*fname == ' ') fname++;
        editor_open(fname);
    } else if (strcmp(cmd, "help") == 0) {
        editor_set_status("h/j/k/l:move i:insert :w:save :q:quit Tab:sidebar Enter:open");
    } else if (cmd[0] >= '0' && cmd[0] <= '9') {
        /* Go to line number */
        int line = atoi(cmd) - 1;
        if (line >= 0 && line < E.num_lines) {
            E.cy = line;
            E.cx = 0;
            editor_set_status("Line %d", line + 1);
        }
    } else if (strlen(cmd) > 0) {
        editor_set_status("Unknown command: %s", cmd);
    }
}

void editor_process_mouse(MOUSE_EVENT_RECORD *event) {
    int x = event->dwMousePosition.X;
    int y = event->dwMousePosition.Y;
    int start_col = E.sidebar_visible ? SIDEBAR_WIDTH : 0;
    
    /* Left click */
    if (event->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) {
        if (E.sidebar_visible && x < SIDEBAR_WIDTH && y < E.screen_rows) {
            /* Click in sidebar */
            int idx = y + E.sidebar_scroll;
            if (idx < E.num_entries) {
                E.sidebar_cursor = idx;
                E.mode = MODE_SIDEBAR;
                E.dirty = 1;
            }
        } else if (y < E.screen_rows && x >= start_col + 6) {
            /* Click in editor */
            int click_y = y + E.row_offset;
            int click_x = x - start_col - 6 + E.col_offset;
            
            if (click_y < E.num_lines) {
                E.cy = click_y;
                int len = strlen(E.lines[E.cy]);
                E.cx = (click_x < len) ? click_x : len;
                if (E.cx < 0) E.cx = 0;
                
                /* Start selection */
                E.sel.active = 1;
                E.sel.start_x = E.sel.end_x = E.cx;
                E.sel.start_y = E.sel.end_y = E.cy;
                
                if (E.mode == MODE_SIDEBAR) E.mode = MODE_NORMAL;
                E.dirty = 1;
            }
        }
    }
    
    /* Mouse drag for selection */
    if ((event->dwEventFlags & MOUSE_MOVED) && (event->dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED)) {
        if (E.sel.active && y < E.screen_rows && x >= start_col + 6) {
            int drag_y = y + E.row_offset;
            int drag_x = x - start_col - 6 + E.col_offset;
            
            if (drag_y < E.num_lines && drag_y >= 0) {
                E.sel.end_y = drag_y;
                int len = strlen(E.lines[drag_y]);
                E.sel.end_x = (drag_x < len) ? drag_x : len;
                if (E.sel.end_x < 0) E.sel.end_x = 0;
                E.cy = E.sel.end_y;
                E.cx = E.sel.end_x;
                E.dirty = 1;
            }
        }
    }
    
    /* Release - finalize selection */
    if (event->dwButtonState == 0 && E.sel.active) {
        if (E.sel.start_x == E.sel.end_x && E.sel.start_y == E.sel.end_y) {
            E.sel.active = 0;  /* No actual selection */
        }
    }
    
    /* Double click in sidebar - open */
    if ((event->dwEventFlags & DOUBLE_CLICK) && E.sidebar_visible && x < SIDEBAR_WIDTH) {
        sidebar_open_selected();
    }
    
    /* Right click in sidebar - open */
    if (event->dwButtonState & RIGHTMOST_BUTTON_PRESSED) {
        if (E.sidebar_visible && x < SIDEBAR_WIDTH) {
            sidebar_open_selected();
        }
    }
    
    /* Mouse wheel */
    if (event->dwEventFlags & MOUSE_WHEELED) {
        int delta = (short)HIWORD(event->dwButtonState);
        if (delta > 0) {
            E.row_offset -= 3;
            if (E.row_offset < 0) E.row_offset = 0;
        } else {
            E.row_offset += 3;
            int max = E.num_lines - E.screen_rows;
            if (max < 0) max = 0;
            if (E.row_offset > max) E.row_offset = max;
        }
        E.dirty = 1;
    }
}

void editor_process_key(void) {
    INPUT_RECORD ir;
    DWORD count;
    
    if (!ReadConsoleInput(E.hStdin, &ir, 1, &count)) return;
    
    if (ir.EventType == WINDOW_BUFFER_SIZE_EVENT) {
        E.screen_cols = ir.Event.WindowBufferSizeEvent.dwSize.X;
        E.screen_rows = ir.Event.WindowBufferSizeEvent.dwSize.Y - STATUS_HEIGHT;
        
        free(E.buffer);
        E.buf_size = E.screen_cols * (E.screen_rows + STATUS_HEIGHT);
        E.buffer = malloc(sizeof(CHAR_INFO) * E.buf_size);
        E.dirty = 1;
        return;
    }
    
    if (ir.EventType == MOUSE_EVENT) {
        editor_process_mouse(&ir.Event.MouseEvent);
        return;
    }
    
    if (ir.EventType != KEY_EVENT || !ir.Event.KeyEvent.bKeyDown) return;
    
    KEY_EVENT_RECORD *key = &ir.Event.KeyEvent;
    int c = key->uChar.AsciiChar;
    int vk = key->wVirtualKeyCode;
    DWORD ctrl = key->dwControlKeyState;
    int is_ctrl = (ctrl & LEFT_CTRL_PRESSED) || (ctrl & RIGHT_CTRL_PRESSED);
    
    /* Clear selection on movement unless shift held */
    if (!(ctrl & SHIFT_PRESSED) && E.sel.active) {
        if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN ||
            c == 'h' || c == 'j' || c == 'k' || c == 'l') {
            clear_selection();
        }
    }
    
    switch (E.mode) {
        case MODE_SIDEBAR:
            if (vk == VK_ESCAPE || vk == VK_TAB) {
                E.mode = MODE_NORMAL;
                E.dirty = 1;
            } else if (vk == VK_UP || c == 'k') {
                if (E.sidebar_cursor > 0) {
                    E.sidebar_cursor--;
                    if (E.sidebar_cursor < E.sidebar_scroll)
                        E.sidebar_scroll = E.sidebar_cursor;
                    E.dirty = 1;
                }
            } else if (vk == VK_DOWN || c == 'j') {
                if (E.sidebar_cursor < E.num_entries - 1) {
                    E.sidebar_cursor++;
                    if (E.sidebar_cursor >= E.sidebar_scroll + E.screen_rows)
                        E.sidebar_scroll = E.sidebar_cursor - E.screen_rows + 1;
                    E.dirty = 1;
                }
            } else if (vk == VK_RETURN || c == 'l') {
                sidebar_open_selected();
            } else if (c == 'h') {
                /* Go to parent */
                E.sidebar_cursor = 0;
                sidebar_open_selected();
            } else if (c == 'q') {
                E.sidebar_visible = 0;
                E.mode = MODE_NORMAL;
                E.dirty = 1;
            }
            break;
            
        case MODE_NORMAL:
            if (c == 'i') {
                E.mode = MODE_INSERT;
                editor_set_status("-- INSERT --");
            } else if (c == 'a') {
                int len = strlen(E.lines[E.cy]);
                if (E.cx < len) E.cx++;
                E.mode = MODE_INSERT;
                editor_set_status("-- INSERT --");
            } else if (c == 'A') {
                E.cx = strlen(E.lines[E.cy]);
                E.mode = MODE_INSERT;
                editor_set_status("-- INSERT --");
            } else if (c == 'I') {
                E.cx = 0;
                E.mode = MODE_INSERT;
                editor_set_status("-- INSERT --");
            } else if (c == 'o') {
                E.cx = strlen(E.lines[E.cy]);
                editor_insert_newline();
                E.mode = MODE_INSERT;
                editor_set_status("-- INSERT --");
            } else if (c == 'O') {
                E.cx = 0;
                editor_insert_newline();
                E.cy--;
                E.mode = MODE_INSERT;
                editor_set_status("-- INSERT --");
            } else if (c == ':') {
                E.mode = MODE_COMMAND;
                E.command_buf[0] = '\0';
                E.command_len = 0;
                E.dirty = 1;
            } else if (c == '/') {
                E.mode = MODE_SEARCH;
                E.search_buf[0] = '\0';
                E.search_len = 0;
                E.dirty = 1;
            } else if (c == 'n') {
                editor_search();
            } else if (c == 'h' || vk == VK_LEFT) {
                editor_move_cursor('h', 0);
            } else if (c == 'j' || vk == VK_DOWN) {
                editor_move_cursor('j', 0);
            } else if (c == 'k' || vk == VK_UP) {
                editor_move_cursor('k', 0);
            } else if (c == 'l' || vk == VK_RIGHT) {
                editor_move_cursor('l', 0);
            } else if (c == '0' || vk == VK_HOME) {
                editor_move_cursor('0', 0);
            } else if (c == '$' || vk == VK_END) {
                editor_move_cursor('$', 0);
            } else if (vk == VK_PRIOR) {
                editor_move_cursor(VK_PRIOR, 1);
            } else if (vk == VK_NEXT) {
                editor_move_cursor(VK_NEXT, 1);
            } else if (c == 'w') {
                editor_word_forward();
            } else if (c == 'b') {
                editor_word_backward();
            } else if (c == 'g') {
                INPUT_RECORD ir2;
                if (ReadConsoleInput(E.hStdin, &ir2, 1, &count)) {
                    if (ir2.EventType == KEY_EVENT && ir2.Event.KeyEvent.bKeyDown) {
                        if (ir2.Event.KeyEvent.uChar.AsciiChar == 'g') {
                            E.cy = 0; E.cx = 0;
                            E.dirty = 1;
                        }
                    }
                }
            } else if (c == 'G') {
                E.cy = E.num_lines - 1;
                E.cx = 0;
                E.dirty = 1;
            } else if (c == 'x') {
                int len = strlen(E.lines[E.cy]);
                if (E.cx < len) {
                    push_undo();
                    char *line = E.lines[E.cy];
                    memmove(&line[E.cx], &line[E.cx + 1], len - E.cx);
                    E.modified = 1;
                    E.dirty = 1;
                }
            } else if (c == 'd') {
                INPUT_RECORD ir2;
                if (ReadConsoleInput(E.hStdin, &ir2, 1, &count)) {
                    if (ir2.EventType == KEY_EVENT && ir2.Event.KeyEvent.bKeyDown) {
                        if (ir2.Event.KeyEvent.uChar.AsciiChar == 'd') {
                            editor_delete_line();
                        }
                    }
                }
            } else if (c == 'y') {
                INPUT_RECORD ir2;
                if (ReadConsoleInput(E.hStdin, &ir2, 1, &count)) {
                    if (ir2.EventType == KEY_EVENT && ir2.Event.KeyEvent.bKeyDown) {
                        if (ir2.Event.KeyEvent.uChar.AsciiChar == 'y') {
                            editor_copy_line();
                        }
                    }
                }
            } else if (c == 'p') {
                editor_paste();
            } else if (c == 'u') {
                pop_undo();
            } else if (vk == VK_TAB) {
                E.sidebar_visible = !E.sidebar_visible;
                if (E.sidebar_visible) {
                    sidebar_load_dir(E.current_dir);
                    E.mode = MODE_SIDEBAR;
                }
                E.dirty = 1;
            } else if (is_ctrl && (vk == 'S' || c == 19)) {
                editor_save();
            } else if (is_ctrl && (vk == 'Q' || c == 17)) {
                if (!E.modified) {
                    editor_free();
                    exit(0);
                }
            }
            break;
            
        case MODE_INSERT:
            if (vk == VK_ESCAPE) {
                E.mode = MODE_NORMAL;
                if (E.cx > 0) E.cx--;
                editor_set_status("");
            } else if (vk == VK_BACK) {
                editor_delete_char();
            } else if (vk == VK_DELETE) {
                int len = strlen(E.lines[E.cy]);
                if (E.cx < len) {
                    push_undo();
                    char *line = E.lines[E.cy];
                    memmove(&line[E.cx], &line[E.cx + 1], len - E.cx);
                    E.modified = 1;
                    E.dirty = 1;
                }
            } else if (vk == VK_RETURN) {
                editor_insert_newline();
            } else if (vk == VK_TAB) {
                for (int i = 0; i < TAB_SIZE; i++) editor_insert_char(' ');
            } else if (vk == VK_LEFT) {
                editor_move_cursor(VK_LEFT, 1);
            } else if (vk == VK_RIGHT) {
                editor_move_cursor(VK_RIGHT, 1);
            } else if (vk == VK_UP) {
                editor_move_cursor(VK_UP, 1);
            } else if (vk == VK_DOWN) {
                editor_move_cursor(VK_DOWN, 1);
            } else if (vk == VK_HOME) {
                editor_move_cursor(VK_HOME, 1);
            } else if (vk == VK_END) {
                editor_move_cursor(VK_END, 1);
            } else if (vk == VK_PRIOR) {
                editor_move_cursor(VK_PRIOR, 1);
            } else if (vk == VK_NEXT) {
                editor_move_cursor(VK_NEXT, 1);
            } else if (c >= 32 && c < 127) {
                editor_insert_char(c);
            }
            break;
            
        case MODE_COMMAND:
            if (vk == VK_ESCAPE) {
                E.mode = MODE_NORMAL;
                editor_set_status("");
            } else if (vk == VK_RETURN) {
                E.mode = MODE_NORMAL;
                editor_process_command();
            } else if (vk == VK_BACK) {
                if (E.command_len > 0) {
                    E.command_buf[--E.command_len] = '\0';
                    E.dirty = 1;
                } else {
                    E.mode = MODE_NORMAL;
                    editor_set_status("");
                }
            } else if (c >= 32 && c < 127 && E.command_len < 255) {
                E.command_buf[E.command_len++] = c;
                E.command_buf[E.command_len] = '\0';
                E.dirty = 1;
            }
            break;
            
        case MODE_SEARCH:
            if (vk == VK_ESCAPE) {
                E.mode = MODE_NORMAL;
                editor_set_status("");
            } else if (vk == VK_RETURN) {
                E.mode = MODE_NORMAL;
                editor_search();
            } else if (vk == VK_BACK) {
                if (E.search_len > 0) {
                    E.search_buf[--E.search_len] = '\0';
                    E.dirty = 1;
                } else {
                    E.mode = MODE_NORMAL;
                    editor_set_status("");
                }
            } else if (c >= 32 && c < 127 && E.search_len < 255) {
                E.search_buf[E.search_len++] = c;
                E.search_buf[E.search_len] = '\0';
                E.dirty = 1;
            }
            break;
    }
}

void show_help(void) {
    printf("\n");
    printf("  AZ Editor v%s - A minimal terminal text editor\n\n", AZ_VERSION);
    printf("  Usage: az [filename]\n\n");
    printf("  Navigation:  h/j/k/l or arrows, w/b words, 0/$ line, gg/G file\n");
    printf("  Editing:     i insert, a append, o newline, x delete, dd cut, yy copy, p paste\n");
    printf("  Commands:    :w save, :q quit, :wq save+quit, :e file, /<text> search\n");
    printf("  Other:       Tab sidebar, u undo, Ctrl+S save, Ctrl+Q quit\n\n");
}

int main(int argc, char *argv[]) {
    if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        show_help();
        return 0;
    }
    
    if (argc > 1 && strcmp(argv[1], "--version") == 0) {
        printf("AZ Editor v%s\n", AZ_VERSION);
        return 0;
    }
    
    editor_init();
    
    if (argc > 1) {
        editor_open(argv[1]);
    } else {
        editor_set_status("AZ Editor v%s | :help | Tab: sidebar | i: insert", AZ_VERSION);
    }
    
    while (1) {
        editor_scroll();
        if (E.dirty) {
            editor_draw();
            E.dirty = 0;
        }
        editor_process_key();
    }
    
    editor_free();
    return 0;
}
