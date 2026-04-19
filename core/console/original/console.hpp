#if !defined(CONSOLE_HPP)
#define CONSOLE_HPP

#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// ANSI Color Codes
#define COLOR_RESET      "\033[0m"
#define COLOR_BLACK      "\033[0;30m"
#define COLOR_RED        "\033[0;31m"
#define COLOR_GREEN      "\033[0;32m"
#define COLOR_YELLOW     "\033[0;33m"
#define COLOR_BLUE       "\033[0;34m"
#define COLOR_MAGENTA    "\033[0;35m"
#define COLOR_CYAN       "\033[0;36m"
#define COLOR_WHITE      "\033[0;37m"

// ANSI Bright Color Codes
#define COLOR_BRIGHT_BLACK   "\033[1;30m"
#define COLOR_BRIGHT_RED     "\033[1;31m"
#define COLOR_BRIGHT_GREEN   "\033[1;32m"
#define COLOR_BRIGHT_YELLOW  "\033[1;33m"
#define COLOR_BRIGHT_BLUE    "\033[1;34m"
#define COLOR_BRIGHT_MAGENTA "\033[1;35m"
#define COLOR_BRIGHT_CYAN    "\033[1;36m"
#define COLOR_BRIGHT_WHITE   "\033[1;37m"

// ANSI Background Color Codes
#define BG_BLACK         "\033[40m"
#define BG_RED           "\033[41m"
#define BG_GREEN         "\033[42m"
#define BG_YELLOW        "\033[43m"
#define BG_BLUE          "\033[44m"
#define BG_MAGENTA       "\033[45m"
#define BG_CYAN          "\033[46m"
#define BG_WHITE         "\033[47m"

// ANSI Text Styles
#define STYLE_BOLD       "\033[1m"
#define STYLE_DIM        "\033[2m"
#define STYLE_ITALIC     "\033[3m"
#define STYLE_UNDERLINE  "\033[4m"
#define STYLE_BLINK      "\033[5m"
#define STYLE_REVERSE    "\033[7m"
#define STYLE_STRIKETHROUGH "\033[9m"

// Console Manager Class
class ConsoleManager {
private:
    HANDLE hConsole;
    bool ansiEnabled;
    
public:
    // Constructor and Destructor
    ConsoleManager();
    ~ConsoleManager();
    
    // Basic Display Methods
    void Print(const char* text);
    void PrintLine(const char* text);
    void PrintFormatted(const char* format, ...);
    void PrintFormattedColored(const char* color, const char* format, ...);
    void PrintFormattedColoredLine(const char* color, const char* format, ...);
    void PrintColored(const char* color, const char* text);
    void PrintColoredLine(const char* color, const char* text);
    void PrintStyledText(const char* style, const char* color, const char* text);
    
    // Cursor Control Methods
    void ClearScreen();
    void ClearLine();
    void MoveCursor(int row, int col);
    void MoveCursorUp(int lines);
    void MoveCursorDown(int lines);
    void MoveCursorLeft(int chars);
    void MoveCursorRight(int chars);
    void SaveCursorPosition();
    void RestoreCursorPosition();
    void HideCursor();
    void ShowCursor();
    
    // Utility Methods
    void EnableANSI();
    void DisableANSI();
    bool IsANSIEnabled();
    void GetConsoleSize(int* width, int* height);
    void SetTitle(const char* title);
    
    // Advanced Display Methods
    void DrawBox(int x, int y, int width, int height, const char* color = COLOR_WHITE);
    void DrawHorizontalLine(int x, int y, int length, char character = '-');
    void DrawVerticalLine(int x, int y, int length, char character = '|');
    void FillArea(int x, int y, int width, int height, char character = ' ', const char* color = COLOR_WHITE);
};

#endif // CONSOLE_HPP