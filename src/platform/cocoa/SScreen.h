
#ifndef __DISPLAYMODE_H__
#define __DISPLAYMODE_H__

#include <memory>
#include <vector>
#include <ctypes.h>
#include <string>


struct DisplayModeData;
typedef struct DisplayMode
{
    uint32_t format;              /**< pixel format */
    int w;                      /**< width, in screen coordinates */
    int h;                      /**< height, in screen coordinates */
    int refresh_rate;           /**< refresh rate (or zero for unspecified) */
    std::shared_ptr<DisplayModeData> driverdata;           /**< driver-specific data, initialize to 0 */

    DisplayMode(){
    }

    ~DisplayMode(){
    }
} DisplayMode;

struct DisplayData;
struct VideoDisplay
{
    std::string name;
    int max_display_modes;
    std::vector<DisplayMode> display_modes;

    DisplayMode & GetCurrentMode(){
        return display_modes[current_mode];
    }

    DisplayMode & GetDesktopMode(){
        return display_modes[desktop_mode];
    }
    int desktop_mode;
    int current_mode;

    std::shared_ptr<DisplayData> driverdata;
    VideoDisplay(){
    }
    ~VideoDisplay(){

    }
};

int Cocoa_GetDisplayBounds(VideoDisplay * display, RECT * rect);

uint32_t Cocoa_InitDisplayModes(std::vector<VideoDisplay> * pDisplays);

#endif//__DISPLAYMODE_H__