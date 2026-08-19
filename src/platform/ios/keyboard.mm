#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#include "keyboard.h"
#include "winuser.h"
#include <stdint.h>
#include <stdio.h>

// iOS 键盘映射基于 UIKeyboardHIDUsage（USB HID Usage Page 0x07）。
// UIPress.keyCode 即 UIKeyboardHIDUsage，这里将其映射到 Windows VK 码。

typedef struct {
    uint16_t hidUsage;  // UIKeyboardHIDUsage
    int vkCode;         // Windows VK
} KeyMapping;

static KeyMapping keyMappings[] = {
    {0x04, 0x41}, // A
    {0x05, 0x42}, // B
    {0x06, 0x43}, // C
    {0x07, 0x44}, // D
    {0x08, 0x45}, // E
    {0x09, 0x46}, // F
    {0x0A, 0x47}, // G
    {0x0B, 0x48}, // H
    {0x0C, 0x49}, // I
    {0x0D, 0x4A}, // J
    {0x0E, 0x4B}, // K
    {0x0F, 0x4C}, // L
    {0x10, 0x4D}, // M
    {0x11, 0x4E}, // N
    {0x12, 0x4F}, // O
    {0x13, 0x50}, // P
    {0x14, 0x51}, // Q
    {0x15, 0x52}, // R
    {0x16, 0x53}, // S
    {0x17, 0x54}, // T
    {0x18, 0x55}, // U
    {0x19, 0x56}, // V
    {0x1A, 0x57}, // W
    {0x1B, 0x58}, // X
    {0x1C, 0x59}, // Y
    {0x1D, 0x5A}, // Z
    {0x1E, 0x31}, // 1
    {0x1F, 0x32}, // 2
    {0x20, 0x33}, // 3
    {0x21, 0x34}, // 4
    {0x22, 0x35}, // 5
    {0x23, 0x36}, // 6
    {0x24, 0x37}, // 7
    {0x25, 0x38}, // 8
    {0x26, 0x39}, // 9
    {0x27, 0x30}, // 0
    {0x28, 0x0D}, // Return/Enter
    {0x29, 0x1B}, // Escape
    {0x2A, 0x08}, // Delete/Backspace
    {0x2B, 0x09}, // Tab
    {0x2C, 0x20}, // Spacebar
    {0x2D, 0xBD}, // Hyphen -> VK_OEM_MINUS
    {0x2E, 0xBB}, // EqualSign -> VK_OEM_PLUS
    {0x2F, 0xDB}, // OpenBracket -> VK_OEM_4
    {0x30, 0xDD}, // CloseBracket -> VK_OEM_6
    {0x31, 0xDC}, // Backslash -> VK_OEM_5
    {0x33, 0xBA}, // Semicolon -> VK_OEM_1
    {0x34, 0xDE}, // Quote -> VK_OEM_7
    {0x35, 0xC0}, // GraveAccent -> VK_OEM_3
    {0x36, 0xBC}, // Comma -> VK_OEM_COMMA
    {0x37, 0xBE}, // Period -> VK_OEM_PERIOD
    {0x38, 0xBF}, // Slash -> VK_OEM_2
    {0x39, 0x14}, // CapsLock
    {0x3A, 0x70}, // F1
    {0x3B, 0x71}, // F2
    {0x3C, 0x72}, // F3
    {0x3D, 0x73}, // F4
    {0x3E, 0x74}, // F5
    {0x3F, 0x75}, // F6
    {0x40, 0x76}, // F7
    {0x41, 0x77}, // F8
    {0x42, 0x78}, // F9
    {0x43, 0x79}, // F10
    {0x44, 0x7A}, // F11
    {0x45, 0x7B}, // F12
    {0x46, 0x2C}, // PrintScreen -> VK_SNAPSHOT
    {0x47, 0x91}, // ScrollLock
    {0x48, 0x13}, // Pause -> VK_PAUSE
    {0x49, 0x2D}, // Insert
    {0x4A, 0x24}, // Home
    {0x4B, 0x21}, // PageUp
    {0x4C, 0x2E}, // DeleteForward
    {0x4D, 0x23}, // End
    {0x4E, 0x22}, // PageDown
    {0x4F, 0x27}, // RightArrow
    {0x50, 0x25}, // LeftArrow
    {0x51, 0x28}, // DownArrow
    {0x52, 0x26}, // UpArrow
    {0x53, 0x90}, // KeypadNumLock -> VK_NUMLOCK
    {0x54, 0x6F}, // KeypadSlash -> VK_DIVIDE
    {0x55, 0x6A}, // KeypadAsterisk -> VK_MULTIPLY
    {0x56, 0x6D}, // KeypadHyphen -> VK_SUBTRACT
    {0x57, 0x6B}, // KeypadPlus -> VK_ADD
    {0x58, 0x0D}, // KeypadEnter
    {0x59, 0x61}, // Keypad1
    {0x5A, 0x62}, // Keypad2
    {0x5B, 0x63}, // Keypad3
    {0x5C, 0x64}, // Keypad4
    {0x5D, 0x65}, // Keypad5
    {0x5E, 0x66}, // Keypad6
    {0x5F, 0x67}, // Keypad7
    {0x60, 0x68}, // Keypad8
    {0x61, 0x69}, // Keypad9
    {0x62, 0x60}, // Keypad0
    {0x63, 0x6E}, // KeypadPeriod -> VK_DECIMAL
    {0x65, 0x5D}, // Application -> VK_APPS (Menu key)
    {0xE0, 0xA2}, // LeftControl -> VK_LCONTROL
    {0xE1, 0xA0}, // LeftShift -> VK_LSHIFT
    {0xE2, 0xA4}, // LeftAlt -> VK_LMENU
    {0xE3, 0x5B}, // LeftGUI -> VK_LWIN
    {0xE4, 0xA3}, // RightControl -> VK_RCONTROL
    {0xE5, 0xA1}, // RightShift -> VK_RSHIFT
    {0xE6, 0xA5}, // RightAlt -> VK_RMENU
    {0xE7, 0x5C}, // RightGUI -> VK_RWIN
};

static const size_t keyCodeMapSize = ARRAYSIZE(keyMappings);

UINT convertKeyCodeToVK(uint16_t keyCode){
    for (size_t i = 0; i < keyCodeMapSize; i++) {
        if (keyMappings[i].hidUsage == keyCode) {
            return keyMappings[i].vkCode;
        }
    }
    return 0;
}

uint16_t convertVKToKeyCode(UINT vk){
    for (size_t i = 0; i < keyCodeMapSize; i++) {
        if (keyMappings[i].vkCode == vk) {
            return keyMappings[i].hidUsage;
        }
    }
    return 0;
}

int GetLocal(char *lpLCData, int cchData){
    @autoreleasepool {
        CFLocaleRef localeRef = CFLocaleCopyCurrent();
        if (localeRef)
        {
            CFStringRef localeID = CFLocaleGetIdentifier(localeRef);
            char buffer[256] = {0};
            if (CFStringGetCString(localeID, buffer, sizeof(buffer), kCFStringEncodingUTF8))
            {
                int len = (int)strlen(buffer);
                CFRelease(localeRef);
                if (len < cchData)
                {
                    strncpy(lpLCData, buffer, cchData - 1);
                    lpLCData[cchData - 1] = '\0';
                    return len + 1;
                }
            }
            CFRelease(localeRef);
        }
        const char* locale = getenv("LANG");
        if (!locale)
            return 0;
        int len = (int)strlen(locale);
        if (len < cchData)
        {
            strncpy(lpLCData, locale, cchData - 1);
            lpLCData[cchData - 1] = '\0';
            return len + 1;
        }
        return 0;
    }
}

// iOS 无 Carbon UCKeyTranslate，字符输入通过 UITextInput 的 insertText 传递，
// 这里返回 0，TranslateMessage 不产生 WM_CHAR，改由 WM_IME_CHAR 处理。
wchar_t scanCodeToChar(uint16_t scanCode, uint32_t modifierFlags) {
    return 0;
}

Keyboard & Keyboard::instance() {
    static Keyboard inst;
    return inst;
}

Keyboard::Keyboard() {
    memset(m_byKeyboardState, 0, sizeof(m_byKeyboardState));
}
SHORT Keyboard::getKeyState(uint8_t vk) {
    BYTE  st = m_byKeyboardState[vk];
    return ((st & 0x80) << 8) | (st & 0x01);
}
void Keyboard::setKeyState(uint8_t vk, BYTE state) {
    m_byKeyboardState[vk] = state;
}

void Keyboard::getKeyboardState(PBYTE lpKeyState) {
  memcpy(lpKeyState, m_byKeyboardState, sizeof(m_byKeyboardState));
}
