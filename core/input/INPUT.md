# Input System – C++ InputManager for Keyboard & Mouse (Win32 VK)

## Overview

This module provides a small C++ wrapper around Win32 input APIs:
- Keyboard input querying using virtual key codes (GetAsyncKeyState)
- Mouse position read/write and mouse button state
- Simple virtual-key injection (keydown events)
- Debug helpers to print current key/mouse states
- Clean separation: InputManager only queries/simulates input; your app owns the logic

File locations:
- `core/input/input.hpp` – public API and VK definitions used by this project
- `core/input/input.cpp` – implementation

---

## Public API (class InputManager)

```cpp
class InputManager {
public:
    // Keyboard
    bool GetPressedKeys(int count, ...);   // true if ALL provided VKs are currently down (MSB)
    bool GetKeyLSB(int key);               // true if key was pressed since last query (LSB)
    bool GetKeyMSB(int key);               // true if key is currently down (MSB)
    void PrintPressedKeys();               // print all currently down keys (ASCII only)
    void PressVirtualKeys(int count, ...); // inject keydown events for provided VKs

    // Mouse
    void GetMousePosition(int* x, int* y);
    void PrintMousePosition();
    void SetMousePosition(int x, int y);
    bool GetMouseButtonState(int button);  // button is VK_LBUTTON/VK_RBUTTON/VK_MBUTTON
    void PrintMouseButtons();
    bool IsMouseMoved();                   // true if cursor moved since previous call
};
```

Notes on semantics:
- MSB/LSB come from Win32 GetAsyncKeyState:
  - MSB (0x8000) indicates the key is currently held down.
  - LSB (0x0001) indicates the key was pressed since the last call to GetAsyncKeyState for that key.
- PressVirtualKeys sends keydown only (no automatic keyup). If you need key release, extend it to emit KEYEVENTF_KEYUP for each key.

---

## Virtual Key Codes

Virtual key constants are available in `input.hpp` (mirroring common Win32 VKs):

- Special: VK_ESCAPE, VK_TAB, VK_CAPITAL, VK_SHIFT, VK_CONTROL, VK_MENU, VK_SPACE, VK_RETURN, VK_BACK
- Arrows: VK_LEFT, VK_UP, VK_RIGHT, VK_DOWN
- Function: VK_F1 … VK_F12
- Digits: VK_0 … VK_9
- Letters: VK_A … VK_Z
- Numpad: VK_NUMPAD0 … VK_NUMPAD9, VK_MULTIPLY, VK_ADD, VK_SEPARATOR, VK_SUBTRACT, VK_DECIMAL, VK_DIVIDE
- Misc: VK_INSERT, VK_DELETE, VK_HOME, VK_END, VK_PAGE_UP, VK_PAGE_DOWN, VK_PRINT, VK_SNAPSHOT, VK_PAUSE, VK_SCROLL
- Mouse buttons: VK_LBUTTON, VK_RBUTTON, VK_MBUTTON

You can also pass literal characters (e.g., 'A', 'e') to functions that accept keys.

---

## Usage Examples (C++)

```cpp
#include "core/input/input.hpp"

InputManager input;

// Keyboard: movement with WASD (MSB == held)
if (input.GetKeyMSB(VK_W)) move_forward();
if (input.GetKeyMSB(VK_A)) move_left();

// Combination: Ctrl + C
if (input.GetPressedKeys(2, VK_CONTROL, VK_C)) copy_to_clipboard();

// Single press (LSB == pressed since last query)
if (input.GetKeyLSB(VK_SPACE)) jump();

// Debug: print currently down ASCII keys
input.PrintPressedKeys();

// Inject keydown events (no release)
input.PressVirtualKeys(5, 'H', 'e', 'l', 'l', 'o');
input.PressVirtualKeys(2, VK_MENU, VK_TAB); // Alt+Tab (keydown only)

// Mouse position
int mx, my;
input.GetMousePosition(&mx, &my);
input.PrintMousePosition();
input.SetMousePosition(960, 540); // center

// Mouse buttons
if (input.GetMouseButtonState(VK_LBUTTON)) handle_left_click_hold();
input.PrintMouseButtons();

// Mouse moved since last check
if (input.IsMouseMoved()) on_mouse_move(mx, my);
```

---

## Behavior & Implementation Notes

- GetPressedKeys uses varargs; pass the number of virtual keys followed by each VK as int.
- PrintPressedKeys prints characters for currently-down keys in the range 8..255; non-printable/VK-only keys may not render meaningfully.
- IsMouseMoved stores the last cursor position internally (static state) and returns true if the position changed since the previous call.
- Thread safety: InputManager keeps a static last cursor position; if you access from multiple threads, serialize calls around IsMouseMoved.

### Extending key injection to include keyup

Current PressVirtualKeys only injects keydown events. To simulate a full press, add a corresponding KEYEVENTF_KEYUP for each key after the keydown.

---

## Summary

Use InputManager for lightweight polling of keyboard/mouse state via Win32. It supports key combinations, LSB/MSB checks, mouse position and buttons, printing helpers, and simple keydown injection. Integrate it into your main loop and keep your game/app logic separate from input querying.
