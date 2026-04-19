# Console Manager – Advanced Windows Console Output Control

## Overview

The **ConsoleManager** provides comprehensive Windows console control with:
- **ANSI Color Support** – Full color palette with background colors and text styles
- **Cursor Management** – Precise positioning, movement, and visibility control  
- **ASCII Graphics** – Box drawing, lines, and area filling capabilities
- **Console Configuration** – Window title, size detection, and mode management
- **Cross-Platform ANSI** – Automatic detection with graceful fallback for legacy terminals
- **Performance Optimized** – Direct Windows Console API usage with minimal overhead

---

## Quick Start

```cpp
#include "console.hpp"

ConsoleManager console;
console.PrintColoredLine(COLOR_BRIGHT_GREEN, "Hello, World!");
console.DrawBox(10, 5, 30, 8, COLOR_BRIGHT_BLUE);
```

## API Reference

### Text Output
```cpp
void Print(const char* text);                                    // Print without newline
void PrintLine(const char* text);                               // Print with newline  
void PrintFormatted(const char* format, ...);                   // Printf-style formatting
void PrintColored(const char* color, const char* text);         // Colored text
void PrintColoredLine(const char* color, const char* text);     // Colored text + newline
void PrintStyledText(const char* style, const char* color, const char* text); // Style + color
```

### Screen & Cursor Control
```cpp
void ClearScreen();                        // Clear screen, cursor to (1,1)
void ClearLine();                          // Clear current line
void MoveCursor(int row, int col);         // Move to position (1-based)
void MoveCursorUp/Down/Left/Right(int n);  // Relative movement
void SaveCursorPosition();                 // Save current position
void RestoreCursorPosition();              // Restore saved position
void HideCursor() / ShowCursor();          // Cursor visibility
```

### Drawing & Graphics  
```cpp
void DrawBox(int x, int y, int width, int height, const char* color = COLOR_WHITE);
void DrawHorizontalLine(int x, int y, int length, char character = '-');
void DrawVerticalLine(int x, int y, int length, char character = '|');
void FillArea(int x, int y, int width, int height, char character = ' ', const char* color = COLOR_WHITE);
```

### Console Management
```cpp
void SetTitle(const char* title);              // Set window title
void GetConsoleSize(int* width, int* height);  // Get dimensions
bool IsANSIEnabled();                          // Check ANSI support
void EnableANSI() / DisableANSI();             // Toggle ANSI mode
```

## Color & Style Constants

### Colors
```cpp
COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_YELLOW,
COLOR_BLUE, COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE

// Bright variants
COLOR_BRIGHT_BLACK, COLOR_BRIGHT_RED, COLOR_BRIGHT_GREEN, COLOR_BRIGHT_YELLOW,
COLOR_BRIGHT_BLUE, COLOR_BRIGHT_MAGENTA, COLOR_BRIGHT_CYAN, COLOR_BRIGHT_WHITE

// Background colors
BG_BLACK, BG_RED, BG_GREEN, BG_YELLOW, BG_BLUE, BG_MAGENTA, BG_CYAN, BG_WHITE
```

### Text Styles
```cpp
STYLE_BOLD, STYLE_DIM, STYLE_ITALIC, STYLE_UNDERLINE, 
STYLE_BLINK, STYLE_REVERSE, STYLE_STRIKETHROUGH
```

## Usage Examples

### Basic Output
```cpp
ConsoleManager console;

console.Print("Hello ");
console.PrintLine("World!");
console.PrintFormatted("Score: %d\n", 1250);
console.PrintColoredLine(COLOR_BRIGHT_GREEN, "SUCCESS!");
```

### Advanced Styling  
```cpp
// Combined style and color
console.PrintStyledText(STYLE_BOLD, COLOR_BRIGHT_RED, "ERROR");

// Manual style combinations
console.Print(STYLE_BOLD STYLE_UNDERLINE);
console.PrintColored(COLOR_BRIGHT_CYAN, "Important Message");
console.Print(COLOR_RESET);
```

### Game Interface Example
```cpp
void DrawGameUI(ConsoleManager& console, int health, int score) {
    console.ClearScreen();
    console.SetTitle("ASCIILATOR v1.0");
    
    // Header
    console.DrawHorizontalLine(1, 1, 80, '=');
    console.MoveCursor(2, 35);
    console.PrintStyledText(STYLE_BOLD, COLOR_BRIGHT_CYAN, "GAME STATUS");
    
    // Health bar
    console.MoveCursor(4, 5);
    console.Print("Health: [");
    const char* healthColor = (health > 50) ? COLOR_BRIGHT_GREEN : COLOR_BRIGHT_RED;
    for(int i = 0; i < health/5; i++) {
        console.PrintColored(healthColor, "█");
    }
    console.PrintFormatted("] %d%%", health);
    
    // Game area
    console.DrawBox(5, 6, 70, 20, COLOR_BRIGHT_BLUE);
}
```

### Animation & Effects
```cpp
void LoadingAnimation(ConsoleManager& console) {
    console.MoveCursor(10, 10);
    console.Print("Loading: [");
    
    for(int i = 0; i < 20; i++) {
        console.PrintColored(COLOR_BRIGHT_GREEN, "█");
        Sleep(100);
    }
    console.Print("] Complete!");
}
```

## Architecture & Implementation

### File Structure
- `console.hpp` – ConsoleManager class declaration with ANSI constants
- `console.cpp` – Complete Windows Console API implementation
- `CONSOLE.md` – Documentation and usage guide

### Key Features
- **Direct WinAPI Integration** – Uses Windows Console API for optimal performance
- **ANSI Auto-Detection** – Enables modern terminal features when available
- **Zero Dependencies** – Only requires standard Windows headers
- **Memory Efficient** – No dynamic allocation, stack-based operations only
- **Thread Safe** – Safe for multi-threaded applications
- **Graceful Degradation** – Falls back to basic text on legacy systems

### Performance Characteristics
- **Minimal Overhead** – Direct console writes bypass stdio buffering
- **Optimized Drawing** – Batch operations for complex graphics
- **Smart Caching** – Console state cached to avoid redundant API calls
- **Efficient Color Handling** – ANSI codes sent only when supported

---

## Integration Guide

### Basic Setup
```cpp
#include "console.hpp"

int main() {
    ConsoleManager console;
    console.PrintColoredLine(COLOR_BRIGHT_GREEN, "Ready to go!");
    return 0;
}
```

### Best Practices
- Create one `ConsoleManager` instance per application
- Use `HideCursor()` during animations for smoother visuals  
- Call `ShowCursor()` before program exit for clean terminal state
- Check `IsANSIEnabled()` for feature detection in cross-platform code
- Use `SaveCursorPosition()` / `RestoreCursorPosition()` for complex layouts

### Error Handling
The ConsoleManager handles all error conditions gracefully:
- Invalid console handles default to safe no-op behavior
- Unsupported ANSI features automatically fall back to plain text
- Out-of-bounds cursor movements are safely ignored
- All functions are designed to never crash or throw exceptions

---

This console management system provides professional-grade Windows console control with modern ANSI support and automatic compatibility handling.