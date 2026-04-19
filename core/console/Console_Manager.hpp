#if !defined(CONSOLE_MANAGER_HPP)
#define CONSOLE_MANAGER_HPP

// ═══════════════════════════════════════════════════════════════════════════════
// Console_Manager.hpp — Cross-platform console output & cursor control
//
// All output uses ANSI escape codes which work natively on:
//   Linux/macOS: every terminal
//   Windows 10+: after enabling Virtual Terminal Processing (done in con_init)
//
// No platform headers leak into this file.
// ═══════════════════════════════════════════════════════════════════════════════

// ─── ANSI Color Codes ─────────────────────────────────────────────────────────

// Foreground
#define COL_RESET        "\033[0m"
#define COL_BLACK        "\033[0;30m"
#define COL_RED          "\033[0;31m"
#define COL_GREEN        "\033[0;32m"
#define COL_YELLOW       "\033[0;33m"
#define COL_BLUE         "\033[0;34m"
#define COL_MAGENTA      "\033[0;35m"
#define COL_CYAN         "\033[0;36m"
#define COL_WHITE        "\033[0;37m"

// Bright foreground
#define COL_BR_BLACK     "\033[1;30m"
#define COL_BR_RED       "\033[1;31m"
#define COL_BR_GREEN     "\033[1;32m"
#define COL_BR_YELLOW    "\033[1;33m"
#define COL_BR_BLUE      "\033[1;34m"
#define COL_BR_MAGENTA   "\033[1;35m"
#define COL_BR_CYAN      "\033[1;36m"
#define COL_BR_WHITE     "\033[1;37m"

// Background
#define BG_BLACK         "\033[40m"
#define BG_RED           "\033[41m"
#define BG_GREEN         "\033[42m"
#define BG_YELLOW        "\033[43m"
#define BG_BLUE          "\033[44m"
#define BG_MAGENTA       "\033[45m"
#define BG_CYAN          "\033[46m"
#define BG_WHITE         "\033[47m"

// Styles
#define STY_BOLD         "\033[1m"
#define STY_DIM          "\033[2m"
#define STY_ITALIC       "\033[3m"
#define STY_UNDERLINE    "\033[4m"
#define STY_BLINK        "\033[5m"
#define STY_REVERSE      "\033[7m"
#define STY_STRIKE       "\033[9m"

// ─── API: Lifecycle ───────────────────────────────────────────────────────────

// Initialize console (enables ANSI on Windows). Call once at startup.
void con_init(void);

// Restore defaults (show cursor, reset colors). Call at shutdown.
void con_shutdown(void);

// ─── API: Output ──────────────────────────────────────────────────────────────

// Print raw text (no newline).
void con_print(const char* text);

// Print text + newline.
void con_println(const char* text);

// Formatted print (printf-style, no newline).
void con_printf(const char* fmt, ...);

// Print with color prefix + reset suffix.
void con_print_color(const char* color, const char* text);

// Print with color + newline.
void con_println_color(const char* color, const char* text);

// Formatted print with color.
void con_printf_color(const char* color, const char* fmt, ...);

// Print with style + color + reset.
void con_print_styled(const char* style, const char* color, const char* text);

// ─── API: Screen ─────────────────────────────────────────────────────────────

// Clear entire screen and move cursor to (1,1).
void con_clear(void);

// Clear current line.
void con_clear_line(void);

// ─── API: Cursor ─────────────────────────────────────────────────────────────

// Move cursor to (row, col). 1-based.
void con_move(int row, int col);

// Move cursor relative.
void con_move_up(int n);
void con_move_down(int n);
void con_move_left(int n);
void con_move_right(int n);

// Save / restore cursor position.
void con_cursor_save(void);
void con_cursor_restore(void);

// Show / hide cursor.
void con_cursor_show(void);
void con_cursor_hide(void);

// ─── API: Terminal Info ──────────────────────────────────────────────────────

// Get terminal size in characters.
void con_get_size(int* width, int* height);

// Set terminal window title.
void con_set_title(const char* title);

// Is ANSI enabled? (Always true on Linux, may fail on very old Windows.)
int con_ansi_enabled(void);

// ─── API: Drawing ────────────────────────────────────────────────────────────

// Draw an ASCII box at (x, y) with given size.
void con_draw_box(int x, int y, int w, int h, const char* color);

// Draw horizontal line at (x, y) with length.
void con_draw_hline(int x, int y, int len, char ch);

// Draw vertical line at (x, y) with length.
void con_draw_vline(int x, int y, int len, char ch);

// Fill rectangular area with a character + color.
void con_fill(int x, int y, int w, int h, char ch, const char* color);

// Debug overlay: draws a red border, resolution at (2,2), dot at center.
// Call in a loop — re-queries terminal size each call so it responds to resize.
void con_debug(void);

#endif // CONSOLE_MANAGER_HPP
