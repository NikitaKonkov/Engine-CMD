#include "input.hpp"

////////////////////// Get the state of multiple keys; returns true if all specified keys are pressed
bool InputManager::GetPressedKeys(int count, ...) {
    va_list args;
    va_start(args, count);

    for (int i = 0; i < count; i++) {
        int key = va_arg(args, int);
        if (!(GetAsyncKeyState(key) & 0x8000)) {
            va_end(args);
            return false;
        }
    }
    va_end(args);
    return true;
}

////////////////////// Check if a key was pressed since the last call
bool InputManager::GetKeyLSB(int key) {
    SHORT keyState = GetAsyncKeyState(key);
    return (keyState & 0x0001) != 0;
}

////////////////////// Check if a key is currently pressed (most significant bit)
bool InputManager::GetKeyMSB(int key) {
    SHORT keyState = GetAsyncKeyState(key);
    return (keyState & 0x8000) != 0;
}

////////////////////// Print all currently pressed keys to the console
void InputManager::PrintPressedKeys() {
    printf("Pressed Keys: ");
    for (int key = 8; key <= 255; key++) {
        if (GetAsyncKeyState(key) & 0x8000) {
            printf("%c ", key);
        }
    }
    printf("\n");
}

////////////////////// Simulate key presses for specified virtual keys
void InputManager::PressVirtualKeys(int count, ...) {
    va_list args;
    va_start(args, count);

    INPUT inputs[256] = {0};
    int inputIndex = 0;

    for (int i = 0; i < count; i++) {
        int key = va_arg(args, int);
        inputs[inputIndex].type = INPUT_KEYBOARD;
        inputs[inputIndex].ki.wVk = (WORD)key;
        inputIndex++;
    }

    va_end(args);
    SendInput(inputIndex, inputs, sizeof(INPUT));
}

////////////////////// Get current mouse position
void InputManager::GetMousePosition(int *x, int *y) {
    POINT cursorPos;
    if (GetCursorPos(&cursorPos)) {
        *x = cursorPos.x;
        *y = cursorPos.y;
    } else {
        *x = -1;
        *y = -1;
    }
}

////////////////////// Print current mouse position to the console
void InputManager::PrintMousePosition() {
    POINT cursorPos;
    if (GetCursorPos(&cursorPos)) {
        printf("Mouse Position: X = %ld, Y = %ld\n", cursorPos.x, cursorPos.y);
    } else {
        printf("Unable to get mouse position.\n");
    }
}

////////////////////// Set mouse position to specified coordinates
void InputManager::SetMousePosition(int x, int y) {
    SetCursorPos(x, y);
}

////////////////////// Get state of mouse buttons (left, right, middle)
bool InputManager::GetMouseButtonState(int button) {
    return (GetAsyncKeyState(button) & 0x8000) != 0;
}

////////////////////// Print current mouse button states
void InputManager::PrintMouseButtons() {
    printf("Mouse Buttons: ");
    if (GetMouseButtonState(VK_LBUTTON)) printf("LEFT ");
    if (GetMouseButtonState(VK_RBUTTON)) printf("RIGHT ");
    if (GetMouseButtonState(VK_MBUTTON)) printf("MIDDLE ");
    printf("\n");
}

// Previous mouse position for movement detection
POINT InputManager::lastMousePos = {0, 0};

////////////////////// Check if mouse moved since last call
bool InputManager::IsMouseMoved() {
    POINT currentPos;
    GetCursorPos(&currentPos);
    bool moved = (currentPos.x != lastMousePos.x || currentPos.y != lastMousePos.y);
    lastMousePos = currentPos;
    return moved;
}

