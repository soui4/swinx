#ifndef __KEYBOARD_H__
#define __KEYBOARD_H__

#include <ctypes.h>

UINT convertKeyCodeToVK(uint16_t keyCode);
uint16_t convertVKToKeyCode(UINT vk);
wchar_t scanCodeToChar(uint16_t scanCode,uint32_t modifierFlags);

class Keyboard { 
public:
    static Keyboard & instance() ;
    SHORT getKeyState(uint8_t vk);
    void setKeyState(uint8_t vk, BYTE state) ;
    void getKeyboardState(PBYTE lpKeyState);

  private:
    Keyboard() ;

    BYTE m_byKeyboardState[256];
};

#endif//__KEYBOARD_H__