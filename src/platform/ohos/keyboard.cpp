#include "keyboard.h"
#include <winuser.h>

UINT OhosKeyCodeToVK(UINT keyCode)
{
    if (keyCode >= 2000 && keyCode <= 2009)
        return '0' + (keyCode - 2000);
    if (keyCode >= 2017 && keyCode <= 2042)
        return 'A' + (keyCode - 2017);
    if (keyCode >= 2090 && keyCode <= 2101)
        return VK_F1 + (keyCode - 2090);

    switch (keyCode)
    {
    case 1:
    case 2081:
        return VK_HOME;
    case 2:
        return VK_ESCAPE;
    case 2012:
        return VK_UP;
    case 2013:
        return VK_DOWN;
    case 2014:
        return VK_LEFT;
    case 2015:
        return VK_RIGHT;
    case 2043:
        return VK_OEM_COMMA;
    case 2044:
        return VK_OEM_PERIOD;
    case 2045:
    case 2046:
        return VK_MENU;
    case 2047:
    case 2048:
        return VK_SHIFT;
    case 2049:
        return VK_TAB;
    case 2050:
        return VK_SPACE;
    case 2054:
        return VK_RETURN;
    case 2055:
        return VK_BACK;
    case 2056:
        return VK_OEM_3;
    case 2057:
        return VK_OEM_MINUS;
    case 2058:
    case 2066:
        return VK_OEM_PLUS;
    case 2059:
        return VK_OEM_4;
    case 2060:
        return VK_OEM_6;
    case 2061:
        return VK_OEM_5;
    case 2062:
        return VK_OEM_1;
    case 2063:
        return VK_OEM_7;
    case 2064:
        return VK_OEM_2;
    case 2068:
        return VK_PRIOR;
    case 2069:
        return VK_NEXT;
    case 2070:
        return VK_ESCAPE;
    case 2071:
        return VK_DELETE;
    case 2072:
    case 2073:
        return VK_CONTROL;
    case 2074:
        return VK_CAPITAL;
    case 2082:
        return VK_END;
    case 2083:
        return VK_INSERT;
    default:
        return keyCode;
    }
}

UINT OhosVKToKeyCode(UINT vk)
{
    if (vk >= '0' && vk <= '9')
        return 2000 + (vk - '0');
    if (vk >= 'A' && vk <= 'Z')
        return 2017 + (vk - 'A');
    if (vk >= VK_F1 && vk <= VK_F12)
        return 2090 + (vk - VK_F1);

    switch (vk)
    {
    case VK_HOME:
        return 2081;
    case VK_ESCAPE:
        return 2070;
    case VK_UP:
        return 2012;
    case VK_DOWN:
        return 2013;
    case VK_LEFT:
        return 2014;
    case VK_RIGHT:
        return 2015;
    case VK_OEM_COMMA:
        return 2043;
    case VK_OEM_PERIOD:
        return 2044;
    case VK_MENU:
        return 2045;
    case VK_SHIFT:
        return 2047;
    case VK_TAB:
        return 2049;
    case VK_SPACE:
        return 2050;
    case VK_RETURN:
        return 2054;
    case VK_BACK:
        return 2055;
    case VK_OEM_3:
        return 2056;
    case VK_OEM_MINUS:
        return 2057;
    case VK_OEM_PLUS:
        return 2058;
    case VK_OEM_4:
        return 2059;
    case VK_OEM_6:
        return 2060;
    case VK_OEM_5:
        return 2061;
    case VK_OEM_1:
        return 2062;
    case VK_OEM_7:
        return 2063;
    case VK_OEM_2:
        return 2064;
    case VK_PRIOR:
        return 2068;
    case VK_NEXT:
        return 2069;
    case VK_DELETE:
        return 2071;
    case VK_CONTROL:
        return 2072;
    case VK_CAPITAL:
        return 2074;
    case VK_END:
        return 2082;
    case VK_INSERT:
        return 2083;
    default:
        return vk;
    }
}
