// ═══════════════════════════════════════════════════════════════════════════════
// Input_Manager.cpp — Cross-platform keyboard & mouse input
//
// Windows: WinAPI (GetAsyncKeyState, GetCursorPos, SetCursorPos, SendInput)
// Linux:   Terminal (termios raw mode, non-blocking stdin)
//
// Key codes: We use VK_ constants (Windows-compatible values) everywhere.
// On Linux the .cpp maps terminal bytes / escape sequences to VK codes.
// ═══════════════════════════════════════════════════════════════════════════════

#include "Input_Manager.hpp"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// ─── Platform headers ─────────────────────────────────────────────────────────

#if defined(_WIN32)
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>

  // ── Mouse-wheel hook (background thread + WH_MOUSE_LL) ────────────────────
  static volatile LONG g_wheel_accum   = 0;
  static HHOOK         g_mouse_hook    = NULL;
  static HANDLE        g_hook_thread   = NULL;
  static volatile LONG g_hook_ready    = 0;

  static LRESULT CALLBACK wheel_hook_proc(int nCode, WPARAM wParam, LPARAM lParam) {
      if (nCode >= 0 && wParam == WM_MOUSEWHEEL) {
          MSLLHOOKSTRUCT* info = (MSLLHOOKSTRUCT*)lParam;
          short delta = (short)HIWORD(info->mouseData);
          InterlockedExchangeAdd(&g_wheel_accum, (LONG)delta);
      }
      return CallNextHookEx(g_mouse_hook, nCode, wParam, lParam);
  }

  static DWORD WINAPI wheel_thread_proc(LPVOID) {
      g_mouse_hook = SetWindowsHookExW(WH_MOUSE_LL, wheel_hook_proc, NULL, 0);
      InterlockedExchange(&g_hook_ready, 1);
      MSG msg;
      while (GetMessage(&msg, NULL, 0, 0)) {
          TranslateMessage(&msg);
          DispatchMessage(&msg);
      }
      if (g_mouse_hook) UnhookWindowsHookEx(g_mouse_hook);
      return 0;
  }

  static void ensure_wheel_hook(void) {
      if (!g_hook_thread) {
          g_hook_thread = CreateThread(NULL, 0, wheel_thread_proc, NULL, 0, NULL);
          while (!g_hook_ready) Sleep(1);
      }
  }

// ── Terminal (termios) — works on Linux, Termux, SSH, headless ──────────────
#else
  #include <termios.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <sys/select.h>
  #include <sys/ioctl.h>

  // Key state table: tracks which VK codes are currently "held"
  // We poll stdin each time input_key_held is called, draining all
  // available bytes and mapping escape sequences to VK codes.
  static int g_term_keys[256] = {0};   // 1 = held this frame
  static int g_term_init = 0;
  static struct termios g_orig_termios;

  static void term_input_init(void) {
      if (g_term_init) return;
      struct termios raw;
      tcgetattr(STDIN_FILENO, &g_orig_termios);
      raw = g_orig_termios;
      raw.c_lflag &= ~(ICANON | ECHO);  // raw mode, no echo
      raw.c_cc[VMIN]  = 0;               // non-blocking
      raw.c_cc[VTIME] = 0;
      tcsetattr(STDIN_FILENO, TCSANOW, &raw);
      // Also set non-blocking on stdin fd
      int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
      fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
      g_term_init = 1;
  }

  // Drain stdin and update key state. Called at start of each input query.
  static void term_poll_input(void) {
      term_input_init();
      // Clear all keys (terminal can't detect key-up, so keys are "held"
      // only for the frame in which their byte arrives)
      memset(g_term_keys, 0, sizeof(g_term_keys));

      unsigned char buf[64];
      int n;
      while ((n = (int)read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
          int i = 0;
          while (i < n) {
              if (buf[i] == 0x1B) {
                  // Escape sequence
                  if (i + 1 < n && buf[i+1] == '[') {
                      // CSI sequence
                      if (i + 2 < n) {
                          switch (buf[i+2]) {
                              case 'A': g_term_keys[0x26] = 1; break; // Up
                              case 'B': g_term_keys[0x28] = 1; break; // Down
                              case 'C': g_term_keys[0x27] = 1; break; // Right
                              case 'D': g_term_keys[0x25] = 1; break; // Left
                              case 'H': g_term_keys[0x24] = 1; break; // Home
                              case 'F': g_term_keys[0x23] = 1; break; // End
                              case '5': g_term_keys[0x21] = 1; i++; break; // PgUp (5~)
                              case '6': g_term_keys[0x22] = 1; i++; break; // PgDn (6~)
                              case '2': g_term_keys[0x2D] = 1; i++; break; // Insert (2~)
                              case '3': g_term_keys[0x2E] = 1; i++; break; // Delete (3~)
                          }
                          i += 3;
                      } else {
                          i += 2;
                      }
                  } else {
                      // Bare escape = ESC key
                      g_term_keys[0x1B] = 1;
                      i++;
                  }
              } else {
                  unsigned char c = buf[i];
                  if (c == '\r' || c == '\n')       g_term_keys[0x0D] = 1; // Enter
                  else if (c == '\t')               g_term_keys[0x09] = 1; // Tab
                  else if (c == 127 || c == '\b')   g_term_keys[0x08] = 1; // Backspace
                  else if (c == ' ')                g_term_keys[0x20] = 1; // Space
                  else if (c >= 'a' && c <= 'z')    g_term_keys[0x41 + (c - 'a')] = 1;
                  else if (c >= 'A' && c <= 'Z')    g_term_keys[0x41 + (c - 'A')] = 1;
                  else if (c >= '0' && c <= '9')    g_term_keys[0x30 + (c - '0')] = 1;
                  else if (c == 3)                  g_term_keys[0x1B] = 1; // Ctrl+C → ESC
                  i++;
              }
          }
      }
  }
#endif

// ─── Keyboard: key held ──────────────────────────────────────────────────────

int input_key_held(int vk) {
#if defined(_WIN32)
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
    term_poll_input();
    if (vk < 0 || vk > 255) return 0;
    return g_term_keys[vk];
#endif
}

// ─── Keyboard: key pressed (edge detect) ─────────────────────────────────────
// Windows: GetAsyncKeyState LSB toggles each time the key transitions.
// Linux:   We track previous state manually.

int input_key_pressed(int vk) {
#if defined(_WIN32)
    return (GetAsyncKeyState(vk) & 0x0001) != 0;
#else
    // In terminal mode, key_held already represents a single-frame press
    return input_key_held(vk);
#endif
}

// ─── Keyboard: multiple keys held ────────────────────────────────────────────

int input_keys_held(int count, ...) {
    va_list args;
    va_start(args, count);
    for (int i = 0; i < count; i++) {
        int vk = va_arg(args, int);
        if (!input_key_held(vk)) {
            va_end(args);
            return 0;
        }
    }
    va_end(args);
    return 1;
}

// ─── Keyboard: simulate key press ────────────────────────────────────────────

void input_key_send(int vk) {
#if defined(_WIN32)
    INPUT inputs[2] = {};
    inputs[0].type       = INPUT_KEYBOARD;
    inputs[0].ki.wVk     = (WORD)vk;
    inputs[1].type       = INPUT_KEYBOARD;
    inputs[1].ki.wVk     = (WORD)vk;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
#else
    (void)vk;
#endif
}

void input_keys_send(int count, ...) {
    va_list args;

#if defined(_WIN32)
    va_start(args, count);
    INPUT inputs[512] = {};
    int idx = 0;
    for (int i = 0; i < count && idx < 256; i++) {
        int vk = va_arg(args, int);
        inputs[idx].type   = INPUT_KEYBOARD;
        inputs[idx].ki.wVk = (WORD)vk;
        idx++;
    }
    va_end(args);
    va_start(args, count);
    for (int i = 0; i < count && idx < 512; i++) {
        int vk = va_arg(args, int);
        inputs[idx].type       = INPUT_KEYBOARD;
        inputs[idx].ki.wVk     = (WORD)vk;
        inputs[idx].ki.dwFlags = KEYEVENTF_KEYUP;
        idx++;
    }
    va_end(args);
    SendInput(idx, inputs, sizeof(INPUT));
#else
    va_start(args, count);
    for (int i = 0; i < count; i++) (void)va_arg(args, int);
    va_end(args);
#endif
}

// ─── Mouse: get position ─────────────────────────────────────────────────────

void input_mouse_get(int* x, int* y) {
#if defined(_WIN32)
    POINT p;
    if (GetCursorPos(&p)) {
        *x = p.x;
        *y = p.y;
    } else {
        *x = -1;
        *y = -1;
    }
#else
    *x = -1;
    *y = -1;
#endif
}

// ─── Mouse: set position ─────────────────────────────────────────────────────

void input_mouse_set(int x, int y) {
#if defined(_WIN32)
    SetCursorPos(x, y);
#else
    (void)x; (void)y;
#endif
}

// ─── Mouse: button held ──────────────────────────────────────────────────────

int input_mouse_held(int button) {
#if defined(_WIN32)
    return (GetAsyncKeyState(button) & 0x8000) != 0;
#else
    (void)button;
    return 0;
#endif
}

// ─── Mouse: moved since last check ──────────────────────────────────────────

static int g_last_mx = -1;
static int g_last_my = -1;

int input_mouse_moved(void) {
    int cx, cy;
    input_mouse_get(&cx, &cy);
    int moved = (cx != g_last_mx || cy != g_last_my);
    g_last_mx = cx;
    g_last_my = cy;
    return moved;
}

// ─── Mouse: wheel delta (accumulated, resets on read) ────────────────────────

int input_mouse_wheel(void) {
#if defined(_WIN32)
    ensure_wheel_hook();
    return (int)InterlockedExchange(&g_wheel_accum, 0);
#else
    return 0;
#endif
}

// ─── Debug: print held keys ──────────────────────────────────────────────────

void input_print_keys(void) {
    printf("Held keys:");
    // Check printable range + common special keys
    for (int vk = 0x08; vk <= 0xFF; vk++) {
        if (input_key_held(vk)) {
            // Try to print a readable name for common keys
            if (vk >= 0x41 && vk <= 0x5A)      printf(" %c", vk);        // A-Z
            else if (vk >= 0x30 && vk <= 0x39)  printf(" %c", vk);        // 0-9
            else if (vk == 0x20) printf(" SPACE");
            else if (vk == 0x0D) printf(" ENTER");
            else if (vk == 0x1B) printf(" ESC");
            else if (vk == 0x09) printf(" TAB");
            else if (vk == 0x10) printf(" SHIFT");
            else if (vk == 0x11) printf(" CTRL");
            else if (vk == 0x12) printf(" ALT");
            else if (vk == 0x08) printf(" BKSP");
            else if (vk == 0x25) printf(" LEFT");
            else if (vk == 0x26) printf(" UP");
            else if (vk == 0x27) printf(" RIGHT");
            else if (vk == 0x28) printf(" DOWN");
            else if (vk >= 0x70 && vk <= 0x7B) printf(" F%d", vk - 0x6F);
            else printf(" 0x%02X", vk);
        }
    }
    printf("\n");
}

// ─── Debug: print mouse state ────────────────────────────────────────────────

void input_print_mouse(void) {
    int x, y;
    input_mouse_get(&x, &y);
    printf("Mouse: (%d, %d)", x, y);
    if (input_mouse_held(VK_LBUTTON_)) printf(" [LEFT]");
    if (input_mouse_held(VK_RBUTTON_)) printf(" [RIGHT]");
    if (input_mouse_held(VK_MBUTTON_)) printf(" [MIDDLE]");
    printf("\n");
}
