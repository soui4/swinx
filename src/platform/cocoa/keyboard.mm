
#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#include "keyboard.h"
#include "winuser.h"
#include <stdint.h>
#include <stdio.h>

typedef struct {
    const char *keyName;
    int keyCode; // macOS keyCode
    int vkCode;  // Windows VK value
} KeyMapping;

KeyMapping keyMappings[] = {
    {"Backspace", 51, 0x08},
    {"Tab", 48, 0x09},
    {"Space", 49, 0x20},
    {"0", 29, 0x30},
    {"1", 18, 0x31},
    {"2", 19, 0x32},
    {"3", 20, 0x33},
    {"4", 21, 0x34},
    {"5", 23, 0x35},
    {"6", 22, 0x36},
    {"7", 26, 0x37},
    {"8", 28, 0x38},
    {"9", 25, 0x39},
    {"A", 0, 0x41},
    {"S", 1, 0x53},
    {"D", 2, 0x44},
    {"F", 3, 0x46},
    {"H", 4, 0x48},
    {"G", 5, 0x47},
    {"Z", 6, 0x5A},
    {"X", 7, 0x58},
    {"C", 8, 0x43},
    {"V", 9, 0x56},
    {"B", 11, 0x42},
    {"Q", 12, 0x51},
    {"W", 13, 0x57},
    {"E", 14, 0x45},
    {"R", 15, 0x52},
    {"T", 17, 0x54},
    {"Y", 16, 0x59},
    {"U", 32, 0x55},
    {"I", 34, 0x49},
    {"O", 35, 0x4F},
    {"P", 25, 0x50},
    {"L", 37, 0x4C},
    {"K", 40, 0x4B},
    {"J", 38, 0x4A},
    {"N", 45, 0x4E},
    {"M", 46, 0x4D},
    {"Enter", 36, 0x0D},
    {"Delete", 51, 0x2E},
    {"Insert", 114, 0x2D},
    {"Up Arrow", 126, 0x26},
    {"Down Arrow", 125, 0x28},
    {"Left Arrow", 123, 0x25},
    {"Right Arrow", 124, 0x27},
    {"Shift", 56, 0x10},
    {"Control", 59, 0x11},
    {"Option", 58, 0x12},
    {"Command", 55, 0x5B},
    {"Caps Lock", 57, 0x14},
    {"F1", 122, 0x70},
    {"F2", 120, 0x71},
    {"F3", 99, 0x72},
    {"F4", 118, 0x73},
    {"F5", 96, 0x74},
    {"F6", 97, 0x75},
    {"F7", 98, 0x76},
    {"F8", 100, 0x77},
    {"F9", 101, 0x78},
    {"F10", 109, 0x79},
    {"F11", 103, 0x7A},
    {"F12", 111, 0x7B},
    {"Numpad 0", 83, 0x60},
    {"Numpad 1", 84, 0x61},
    {"Numpad 2", 85, 0x62},
    {"Numpad 3", 86, 0x63},
    {"Numpad 4", 87, 0x64},
    {"Numpad 5", 88, 0x65},
    {"Numpad 6", 89, 0x66},
    {"Numpad 7", 91, 0x67},
    {"Numpad 8", 92, 0x68},
    {"Numpad 9", 93, 0x69},
    {"Numpad +", 69, 0x6B},
    {"Numpad -", 78, 0x6D},
    {"Numpad *", 67, 0x6A},
    {"Numpad /", 75, 0x6F},
    {"Numpad .", 82, 0x6E},
    {"Numpad Enter", 76, 0x0D},
    {"Page Up", 116, 0x22},
    {"Page Down", 121, 0x22},
    {"Home", 115, 0x24},
    {"End", 119, 0x23}
};

static const size_t keyCodeMapSize = ARRAYSIZE(keyMappings);

UINT convertKeyCodeToVK(uint16_t keyCode){
    for (size_t i = 0; i < keyCodeMapSize; i++) {
        if (keyMappings[i].keyCode == keyCode) {
            return keyMappings[i].vkCode;
        }
    }
    return 0;
}

uint16_t convertVKToKeyCode(UINT vk){
    for (size_t i = 0; i < keyCodeMapSize; i++) {
        if (keyMappings[i].vkCode == vk) {
            return keyMappings[i].keyCode;
        }
    }
    return 0;
}


wchar_t scanCodeToChar(uint16_t scanCode,uint32_t modifierFlags) {
    @autoreleasepool {

    TISInputSourceRef keyboardLayout = TISCopyCurrentKeyboardLayoutInputSource();
    CFDataRef layoutData = (CFDataRef)TISGetInputSourceProperty(keyboardLayout, kTISPropertyUnicodeKeyLayoutData);
    if (!layoutData) {
        return 0;
    }

    const UCKeyboardLayout *keyboardLayoutPtr = (const UCKeyboardLayout *)CFDataGetBytePtr(layoutData);
    UInt32 deadKeyState = 0;
    UniCharCount maxStringLength = 4;
    UniCharCount actualStringLength = 0;
    UniChar unicodeString[maxStringLength];

    OSStatus status = UCKeyTranslate(
        keyboardLayoutPtr,
        scanCode,
        kUCKeyActionDown,
        modifierFlags>>8, 
        LMGetKbdType(),
        kUCKeyTranslateNoDeadKeysMask,
        &deadKeyState,
        maxStringLength,
        &actualStringLength,
        unicodeString
    );

    CFRelease(keyboardLayout);
    
    if (status != noErr || actualStringLength == 0) {
        return 0;
    }

    if (actualStringLength == 1) {
        return (wchar_t)unicodeString[0];
    } else if (actualStringLength >= 2) {
        if ((unicodeString[0] & 0xFC00) == 0xD800 && (unicodeString[1] & 0xFC00) == 0xDC00) {
            UInt32 high = unicodeString[0];
            UInt32 low = unicodeString[1];
            UInt32 codePoint = 0x10000 + ((high & 0x3FF) << 10) + (low & 0x3FF);
            return (wchar_t)codePoint;
        } else {
            return (wchar_t)unicodeString[0];
        }
    }
    return 0;
    }
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
