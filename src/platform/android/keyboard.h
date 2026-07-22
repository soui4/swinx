#ifndef _ANDROID_KEYBOARD_H_
#define _ANDROID_KEYBOARD_H_

#include <windows.h>

UINT AndroidVKToKeyCode(UINT vk);
UINT AndroidKeyCodeToVK(UINT keyCode);

#endif // _ANDROID_KEYBOARD_H_