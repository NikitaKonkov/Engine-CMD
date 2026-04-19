// ═══════════════════════════════════════════════════════════════════════════════
// Console_Manager.cpp — Cross-platform console output & cursor control
//
// Windows: Enables ANSI via SetConsoleMode, gets size via
//          GetConsoleScreenBufferInfo, sets title via SetConsoleTitleA.
// Linux:   ANSI works natively, size via ioctl(TIOCGWINSZ),
//          title via ANSI OSC sequence.
//
// All visible output goes through fputs/fprintf to stdout.
// ═══════════════════════════════════════════════════════════════════════════════

#include "Console_Manager.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #include <sys/ioctl.h>
  #include <unistd.h>
#endif

// ─── Internal state ───────────────────────────────────────────────────────────

static int g_ansi_enabled = 0;

// ─── con_init ─────────────────────────────────────────────────────────────────

void con_init(void) {
#if defined(_WIN32)
    // Enable Virtual Terminal Processing so ANSI codes work on Windows 10+
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(h, &mode)) {
            if (SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
                g_ansi_enabled = 1;
            }
        }
    }
#else
    // Linux/macOS terminals support ANSI natively
    g_ansi_enabled = 1;
#endif
}

// ─── con_shutdown ─────────────────────────────────────────────────────────────

void con_shutdown(void) {
    if (g_ansi_enabled) {
        fputs(COL_RESET, stdout);
        con_cursor_show();
    }
    fflush(stdout);
}

// ─── Output ───────────────────────────────────────────────────────────────────

void con_print(const char* text) {
    fputs(text, stdout);
}

void con_println(const char* text) {
    fputs(text, stdout);
    fputc('\n', stdout);
}

void con_printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
}

void con_print_color(const char* color, const char* text) {
    if (g_ansi_enabled) {
        fputs(color, stdout);
        fputs(text, stdout);
        fputs(COL_RESET, stdout);
    } else {
        fputs(text, stdout);
    }
}

void con_println_color(const char* color, const char* text) {
    con_print_color(color, text);
    fputc('\n', stdout);
}

void con_printf_color(const char* color, const char* fmt, ...) {
    char buf[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    con_print_color(color, buf);
}

void con_print_styled(const char* style, const char* color, const char* text) {
    if (g_ansi_enabled) {
        fputs(style, stdout);
        fputs(color, stdout);
        fputs(text, stdout);
        fputs(COL_RESET, stdout);
    } else {
        fputs(text, stdout);
    }
}

// ─── Screen ───────────────────────────────────────────────────────────────────

void con_clear(void) {
#if defined(_WIN32)
    system("cls");
#else
    system("clear");
#endif
}

void con_clear_line(void) {
    if (g_ansi_enabled) {
        fputs("\033[2K", stdout);
        fflush(stdout);
    }
}

// ─── Cursor ───────────────────────────────────────────────────────────────────

void con_move(int row, int col) {
    if (g_ansi_enabled) fprintf(stdout, "\033[%d;%dH", row, col);
}

void con_move_up(int n) {
    if (g_ansi_enabled && n > 0) fprintf(stdout, "\033[%dA", n);
}

void con_move_down(int n) {
    if (g_ansi_enabled && n > 0) fprintf(stdout, "\033[%dB", n);
}

void con_move_left(int n) {
    if (g_ansi_enabled && n > 0) fprintf(stdout, "\033[%dD", n);
}

void con_move_right(int n) {
    if (g_ansi_enabled && n > 0) fprintf(stdout, "\033[%dC", n);
}

void con_cursor_save(void) {
    if (g_ansi_enabled) fputs("\033[s", stdout);
}

void con_cursor_restore(void) {
    if (g_ansi_enabled) fputs("\033[u", stdout);
}

void con_cursor_show(void) {
    if (g_ansi_enabled) fputs("\033[?25h", stdout);
}

void con_cursor_hide(void) {
    if (g_ansi_enabled) fputs("\033[?25l", stdout);
}

// ─── Terminal Info ────────────────────────────────────────────────────────────

void con_get_size(int* width, int* height) {
#if defined(_WIN32)
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(h, &csbi)) {
        *width  = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        *height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    } else {
        *width  = 80;
        *height = 25;
    }
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        *width  = ws.ws_col;
        *height = ws.ws_row;
    } else {
        *width  = 80;
        *height = 25;
    }
#endif
}

void con_set_title(const char* title) {
#if defined(_WIN32)
    SetConsoleTitleA(title);
#else
    // OSC sequence: \033]0;title\007
    fprintf(stdout, "\033]0;%s\007", title);
    fflush(stdout);
#endif
}

int con_ansi_enabled(void) {
    return g_ansi_enabled;
}

// ─── Drawing ──────────────────────────────────────────────────────────────────

void con_draw_box(int x, int y, int w, int h, const char* color) {
    if (w < 2 || h < 1) return;

    // Top border
    con_move(y, x);
    con_print_color(color, "+");
    for (int i = 1; i < w - 1; i++) con_print("-");
    con_print("+");

    // Side borders
    for (int i = 1; i < h - 1; i++) {
        con_move(y + i, x);
        con_print_color(color, "|");
        con_move(y + i, x + w - 1);
        con_print_color(color, "|");
    }

    // Bottom border
    if (h > 1) {
        con_move(y + h - 1, x);
        con_print_color(color, "+");
        for (int i = 1; i < w - 1; i++) con_print("-");
        con_print("+");
    }

    fputs(COL_RESET, stdout);
    fflush(stdout);
}

void con_draw_hline(int x, int y, int len, char ch) {
    con_move(y, x);
    for (int i = 0; i < len; i++) fputc(ch, stdout);
}

void con_draw_vline(int x, int y, int len, char ch) {
    for (int i = 0; i < len; i++) {
        con_move(y + i, x);
        fputc(ch, stdout);
    }
}

void con_fill(int x, int y, int w, int h, char ch, const char* color) {
    if (g_ansi_enabled) fputs(color, stdout);
    for (int row = 0; row < h; row++) {
        con_move(y + row, x);
        for (int col = 0; col < w; col++) fputc(ch, stdout);
    }
    if (g_ansi_enabled) fputs(COL_RESET, stdout);
    fflush(stdout);
}

// ─── Debug overlay ────────────────────────────────────────────────────────────

void con_debug(void) {
    int w, h;
    con_get_size(&w, &h);
    if (w < 4 || h < 4) return;

    // Hide cursor while drawing to avoid flicker
    con_cursor_hide();

    // ── Border (top + bottom rows, left + right columns) ─────────────────────
    fputs(COL_RED, stdout);

    // Top row
    con_move(1, 1);
    for (int c = 0; c < w; c++) fputc('#', stdout);

    // Bottom row
    con_move(h, 1);
    for (int c = 0; c < w; c++) fputc('#', stdout);

    // Left and right columns
    for (int r = 2; r < h; r++) {
        con_move(r, 1);
        fputc('#', stdout);
        con_move(r, w);
        fputc('#', stdout);
    }

    fputs(COL_RESET, stdout);

    // ── Resolution text at (row=2, col=2) ─────────────────────────────────────
    con_move(2, 2);
    fprintf(stdout, COL_BR_RED "[ %d x %d ]" COL_RESET, w, h);

    // ── Center dot ────────────────────────────────────────────────────────────
    int cx = w / 2;
    int cy = h / 2;
    con_move(cy, cx);
    fputs(COL_BR_WHITE "*" COL_RESET, stdout);

    fflush(stdout);
    con_cursor_show();
}
