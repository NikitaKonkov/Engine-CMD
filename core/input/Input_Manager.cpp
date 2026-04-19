// ═══════════════════════════════════════════════════════════════════════════════
// Input_Manager.cpp — Cross-platform keyboard & mouse input
//
// Windows: WinAPI (GetAsyncKeyState, GetCursorPos, SetCursorPos, SendInput)
// Linux:   X11 (XQueryKeymap, XQueryPointer, XWarpPointer) +
//          XTest (XTestFakeKeyEvent, XTestFakeButtonEvent)
//
// Key codes: We use VK_ constants (Windows-compatible values) everywhere.
// On Linux the .cpp maps VK → X11 KeySym → KeyCode internally.
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

// ── Detect X11 availability at compile time ─────────────────────────────────
// If X11 headers are not found, fall back to terminal (termios) input.
// To force terminal mode even with X11 installed, define INPUT_NO_X11.
// Check X11/X.h too — some systems (Termux) ship Xlib.h but lack transitive deps.
#elif !defined(INPUT_NO_X11) && __has_include(<X11/Xlib.h>) && __has_include(<X11/X.h>)
  #define INPUT_USE_X11
  #include <X11/Xlib.h>
  #include <X11/keysym.h>
  #include <X11/extensions/XTest.h>

  // Lazy-init X11 display — opened once, never closed (lives for process life)
  static Display* g_display = nullptr;
  static Display* get_display(void) {
      if (!g_display) {
          g_display = XOpenDisplay(nullptr);
          if (!g_display) {
              fprintf(stderr, "[Input] Failed to open X11 display\n");
          }
      }
      return g_display;
  }

  // ── VK → X11 KeySym mapping ────────────────────────────────────────────────
  static KeySym vk_to_keysym(int vk) {
      if (vk >= 0x41 && vk <= 0x5A) return XK_a + (vk - 0x41);
      if (vk >= 0x30 && vk <= 0x39) return XK_0 + (vk - 0x30);
      if (vk >= 0x60 && vk <= 0x69) return XK_KP_0 + (vk - 0x60);
      if (vk >= 0x70 && vk <= 0x7B) return XK_F1 + (vk - 0x70);
      switch (vk) {
          case 0x1B: return XK_Escape;
          case 0x09: return XK_Tab;
          case 0x14: return XK_Caps_Lock;
          case 0x10: return XK_Shift_L;
          case 0x11: return XK_Control_L;
          case 0x12: return XK_Alt_L;
          case 0x20: return XK_space;
          case 0x0D: return XK_Return;
          case 0x08: return XK_BackSpace;
          case 0x25: return XK_Left;
          case 0x26: return XK_Up;
          case 0x27: return XK_Right;
          case 0x28: return XK_Down;
          case 0x2D: return XK_Insert;
          case 0x2E: return XK_Delete;
          case 0x24: return XK_Home;
          case 0x23: return XK_End;
          case 0x21: return XK_Page_Up;
          case 0x22: return XK_Page_Down;
          case 0x2C: return XK_Print;
          case 0x13: return XK_Pause;
          case 0x91: return XK_Scroll_Lock;
          case 0x6A: return XK_KP_Multiply;
          case 0x6B: return XK_KP_Add;
          case 0x6C: return XK_KP_Separator;
          case 0x6D: return XK_KP_Subtract;
          case 0x6E: return XK_KP_Decimal;
          case 0x6F: return XK_KP_Divide;
          default:   return NoSymbol;
      }
  }

  static unsigned int vk_to_x11_button(int vk) {
      switch (vk) {
          case 0x01: return 1;
          case 0x02: return 3;
          case 0x04: return 2;
          default:   return 0;
      }
  }

#else
  // ── Terminal (termios) fallback — works on Termux, SSH, headless Linux ────
  #define INPUT_USE_TERMIOS
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
#elif defined(INPUT_USE_X11)
    Display* dpy = get_display();
    if (!dpy) return 0;
    KeySym ks = vk_to_keysym(vk);
    if (ks == NoSymbol) return 0;
    KeyCode kc = XKeysymToKeycode(dpy, ks);
    if (kc == 0) return 0;
    char keys[32];
    XQueryKeymap(dpy, keys);
    return (keys[kc / 8] >> (kc % 8)) & 1;
#else
    term_poll_input();
    if (vk < 0 || vk > 255) return 0;
    return g_term_keys[vk];
#endif
}

// ─── Keyboard: key pressed (edge detect) ─────────────────────────────────────
// Windows: GetAsyncKeyState LSB toggles each time the key transitions.
// Linux:   We track previous state manually.

#if !defined(_WIN32) && defined(INPUT_USE_X11)
static char g_prev_keys[32] = {0};
#endif

int input_key_pressed(int vk) {
#if defined(_WIN32)
    return (GetAsyncKeyState(vk) & 0x0001) != 0;
#elif defined(INPUT_USE_X11)
    Display* dpy = get_display();
    if (!dpy) return 0;
    KeySym ks = vk_to_keysym(vk);
    if (ks == NoSymbol) return 0;
    KeyCode kc = XKeysymToKeycode(dpy, ks);
    if (kc == 0) return 0;

    char keys[32];
    XQueryKeymap(dpy, keys);
    int now  = (keys[kc / 8] >> (kc % 8)) & 1;
    int prev = (g_prev_keys[kc / 8] >> (kc % 8)) & 1;

    if (now)
        g_prev_keys[kc / 8] |= (1 << (kc % 8));
    else
        g_prev_keys[kc / 8] &= ~(1 << (kc % 8));

    return (now && !prev);
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
#elif defined(INPUT_USE_X11)
    Display* dpy = get_display();
    if (!dpy) return;
    KeySym ks = vk_to_keysym(vk);
    if (ks == NoSymbol) return;
    KeyCode kc = XKeysymToKeycode(dpy, ks);
    if (kc == 0) return;
    XTestFakeKeyEvent(dpy, kc, True, 0);
    XTestFakeKeyEvent(dpy, kc, False, 0);
    XFlush(dpy);
#else
    (void)vk; // Not supported in terminal mode
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
#elif defined(INPUT_USE_X11)
    Display* dpy = get_display();
    if (!dpy) return;
    KeyCode codes[256];
    va_start(args, count);
    int n = 0;
    for (int i = 0; i < count && n < 256; i++) {
        int vk = va_arg(args, int);
        KeySym ks = vk_to_keysym(vk);
        if (ks == NoSymbol) continue;
        KeyCode kc = XKeysymToKeycode(dpy, ks);
        if (kc == 0) continue;
        codes[n++] = kc;
    }
    va_end(args);
    for (int i = 0; i < n; i++) XTestFakeKeyEvent(dpy, codes[i], True, 0);
    for (int i = 0; i < n; i++) XTestFakeKeyEvent(dpy, codes[i], False, 0);
    XFlush(dpy);
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
#elif defined(INPUT_USE_X11)
    Display* dpy = get_display();
    if (!dpy) { *x = -1; *y = -1; return; }
    Window root = DefaultRootWindow(dpy);
    Window child;
    int root_x, root_y, win_x, win_y;
    unsigned int mask;
    XQueryPointer(dpy, root, &root, &child, &root_x, &root_y, &win_x, &win_y, &mask);
    *x = root_x;
    *y = root_y;
#else
    *x = -1;
    *y = -1;
#endif
}

// ─── Mouse: set position ─────────────────────────────────────────────────────

void input_mouse_set(int x, int y) {
#if defined(_WIN32)
    SetCursorPos(x, y);
#elif defined(INPUT_USE_X11)
    Display* dpy = get_display();
    if (!dpy) return;
    Window root = DefaultRootWindow(dpy);
    XWarpPointer(dpy, None, root, 0, 0, 0, 0, x, y);
    XFlush(dpy);
#else
    (void)x; (void)y;
#endif
}

// ─── Mouse: button held ──────────────────────────────────────────────────────

int input_mouse_held(int button) {
#if defined(_WIN32)
    return (GetAsyncKeyState(button) & 0x8000) != 0;
#elif defined(INPUT_USE_X11)
    Display* dpy = get_display();
    if (!dpy) return 0;
    Window root = DefaultRootWindow(dpy);
    Window child;
    int rx, ry, wx, wy;
    unsigned int mask;
    XQueryPointer(dpy, root, &root, &child, &rx, &ry, &wx, &wy, &mask);
    unsigned int btn = vk_to_x11_button(button);
    if (btn == 1) return (mask & Button1Mask) != 0;
    if (btn == 2) return (mask & Button2Mask) != 0;
    if (btn == 3) return (mask & Button3Mask) != 0;
    return 0;
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
#elif defined(INPUT_USE_X11)
    Display* dpy = get_display();
    if (!dpy) return 0;
    int delta = 0;
    XEvent ev;
    while (XCheckMaskEvent(dpy, ButtonPressMask, &ev)) {
        if (ev.xbutton.button == 4)      delta += 120;
        else if (ev.xbutton.button == 5) delta -= 120;
    }
    return delta;
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
