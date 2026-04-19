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
  #include <sys/time.h>

  // Async-style key state: each key has a timestamp of the last byte received.
  // A key is considered "held" if its timestamp is within KEY_HOLD_MS of now.
  // Terminal key repeat sends at ~30ms intervals, so 120ms catches gaps.
  #define KEY_HOLD_MS 120

  static long long g_term_key_time[256] = {0};  // last-seen timestamp per VK (ms)
  static int g_term_init = 0;
  static struct termios g_orig_termios;

  static long long term_now_ms(void) {
      struct timeval tv;
      gettimeofday(&tv, NULL);
      return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
  }

  static void term_mark_key(int vk) {
      if (vk >= 0 && vk <= 255)
          g_term_key_time[vk] = term_now_ms();
  }

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

  // Drain stdin and stamp key times. Called once per frame via input_poll().
  static void term_poll_input(void) {
      term_input_init();

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
                              case 'A': term_mark_key(0x26); break; // Up
                              case 'B': term_mark_key(0x28); break; // Down
                              case 'C': term_mark_key(0x27); break; // Right
                              case 'D': term_mark_key(0x25); break; // Left
                              case 'H': term_mark_key(0x24); break; // Home
                              case 'F': term_mark_key(0x23); break; // End
                              case '5': term_mark_key(0x21); i++; break; // PgUp (5~)
                              case '6': term_mark_key(0x22); i++; break; // PgDn (6~)
                              case '2': term_mark_key(0x2D); i++; break; // Insert (2~)
                              case '3': term_mark_key(0x2E); i++; break; // Delete (3~)
                          }
                          i += 3;
                      } else {
                          i += 2;
                      }
                  } else {
                      // Bare escape = ESC key
                      term_mark_key(0x1B);
                      i++;
                  }
              } else {
                  unsigned char c = buf[i];
                  if (c == '\r' || c == '\n')       term_mark_key(0x0D); // Enter
                  else if (c == '\t')               term_mark_key(0x09); // Tab
                  else if (c == 127 || c == '\b')   term_mark_key(0x08); // Backspace
                  else if (c == ' ')                term_mark_key(0x20); // Space
                  else if (c >= 'a' && c <= 'z')    term_mark_key(0x41 + (c - 'a'));
                  else if (c >= 'A' && c <= 'Z')    term_mark_key(0x41 + (c - 'A'));
                  else if (c >= '0' && c <= '9')    term_mark_key(0x30 + (c - '0'));
                  else if (c == 3)                  term_mark_key(0x1B); // Ctrl+C → ESC
                  i++;
              }
          }
      }
  }
#endif

// ─── Polling ─────────────────────────────────────────────────────────────────

void input_poll(void) {
#if defined(_WIN32)
    // No-op: Windows uses GetAsyncKeyState which is real-time
#else
    term_poll_input();
#endif
}

// ─── Keyboard: key held ──────────────────────────────────────────────────────

int input_key_held(int vk) {
#if defined(_WIN32)
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
    if (vk < 0 || vk > 255) return 0;
    // Key is "held" if last byte arrived within KEY_HOLD_MS
    long long age = term_now_ms() - g_term_key_time[vk];
    return (g_term_key_time[vk] != 0 && age < KEY_HOLD_MS);
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
