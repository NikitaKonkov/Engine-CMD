#if !defined(INPUT_HPP)
#define INPUT_HPP

#include <windows.h>
#include <stdbool.h>
#include <stdio.h>

// Special Keys
#define VK_ESCAPE        0x1B  // Escape key
#define VK_TAB           0x09  // Tab key
#define VK_CAPITAL       0x14  // Caps Lock key
#define VK_SHIFT         0x10  // Shift key
#define VK_CONTROL       0x11  // Control key
#define VK_MENU          0x12  // Alt key
#define VK_SPACE         0x20  // Spacebar
#define VK_RETURN        0x0D  // Enter key
#define VK_BACK          0x08  // Backspace key

// Arrow Keys
#define VK_LEFT          0x25  // Left arrow key
#define VK_UP            0x26  // Up arrow key
#define VK_RIGHT         0x27  // Right arrow key
#define VK_DOWN          0x28  // Down arrow key

// Function Keys
#define VK_F1            0x70  // F1 key
#define VK_F2            0x71  // F2 key
#define VK_F3            0x72  // F3 key
#define VK_F4            0x73  // F4 key
#define VK_F5            0x74  // F5 key
#define VK_F6            0x75  // F6 key
#define VK_F7            0x76  // F7 key
#define VK_F8            0x77  // F8 key
#define VK_F9            0x78  // F9 key
#define VK_F10           0x79  // F10 key
#define VK_F11           0x7A  // F11 key
#define VK_F12           0x7B  // F12 key

// Number Keys (Top Row)
#define VK_0             0x30  // '0' key
#define VK_1             0x31  // '1' key
#define VK_2             0x32  // '2' key
#define VK_3             0x33  // '3' key
#define VK_4             0x34  // '4' key
#define VK_5             0x35  // '5' key
#define VK_6             0x36  // '6' key
#define VK_7             0x37  // '7' key
#define VK_8             0x38  // '8' key
#define VK_9             0x39  // '9' key

// Alphabet Keys
#define VK_A             0x41  // 'A' key
#define VK_B             0x42  // 'B' key
#define VK_C             0x43  // 'C' key
#define VK_D             0x44  // 'D' key
#define VK_E             0x45  // 'E' key
#define VK_F             0x46  // 'F' key
#define VK_G             0x47  // 'G' key
#define VK_H             0x48  // 'H' key
#define VK_I             0x49  // 'I' key
#define VK_J             0x4A  // 'J' key
#define VK_K             0x4B  // 'K' key
#define VK_L             0x4C  // 'L' key
#define VK_M             0x4D  // 'M' key
#define VK_N             0x4E  // 'N' key
#define VK_O             0x4F  // 'O' key
#define VK_P             0x50  // 'P' key
#define VK_Q             0x51  // 'Q' key
#define VK_R             0x52  // 'R' key
#define VK_S             0x53  // 'S' key
#define VK_T             0x54  // 'T' key
#define VK_U             0x55  // 'U' key
#define VK_V             0x56  // 'V' key
#define VK_W             0x57  // 'W' key
#define VK_X             0x58  // 'X' key
#define VK_Y             0x59  // 'Y' key
#define VK_Z             0x5A  // 'Z' key

// Numpad Keys
#define VK_NUMPAD0       0x60  // Numpad '0' key
#define VK_NUMPAD1       0x61  // Numpad '1' key
#define VK_NUMPAD2       0x62  // Numpad '2' key
#define VK_NUMPAD3       0x63  // Numpad '3' key
#define VK_NUMPAD4       0x64  // Numpad '4' key
#define VK_NUMPAD5       0x65  // Numpad '5' key
#define VK_NUMPAD6       0x66  // Numpad '6' key
#define VK_NUMPAD7       0x67  // Numpad '7' key
#define VK_NUMPAD8       0x68  // Numpad '8' key
#define VK_NUMPAD9       0x69  // Numpad '9' key
#define VK_MULTIPLY      0x6A  // Numpad '*' key
#define VK_ADD           0x6B  // Numpad '+' key
#define VK_SEPARATOR     0x6C  // Numpad separator key
#define VK_SUBTRACT      0x6D  // Numpad '-' key
#define VK_DECIMAL       0x6E  // Numpad '.' key
#define VK_DIVIDE        0x6F  // Numpad '/' key

// Miscellaneous Keys
#define VK_INSERT        0x2D  // Insert key
#define VK_DELETE        0x2E  // Delete key
#define VK_HOME          0x24  // Home key
#define VK_END           0x23  // End key
#define VK_PAGE_UP       0x21  // Page Up key
#define VK_PAGE_DOWN     0x22  // Page Down key
#define VK_PRINT         0x2A  // Print key
#define VK_SNAPSHOT      0x2C  // Print Screen key
#define VK_PAUSE         0x13  // Pause key
#define VK_SCROLL        0x91  // Scroll Lock key

// Mouse Button Keys
#define VK_LBUTTON       0x01  // Left mouse button
#define VK_RBUTTON       0x02  // Right mouse button
#define VK_MBUTTON       0x04  // Middle mouse button


// Input Manager Class
class InputManager {
private:
    static POINT lastMousePos;
    
public:
    // Keyboard Methods
    bool GetPressedKeys(int count, ...);
    bool GetKeyLSB(int key);
    bool GetKeyMSB(int key);
    void PrintPressedKeys();
    void PressVirtualKeys(int count, ...);

    // Mouse Methods
    void GetMousePosition(int *x, int *y);
    void PrintMousePosition();
    void SetMousePosition(int x, int y);
    bool GetMouseButtonState(int button);
    void PrintMouseButtons();
    bool IsMouseMoved();
};

#endif // INPUT_HPP