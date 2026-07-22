#include "platform_api.h"
#include <stdlib.h>

struct PlatformAPI g_platformAPI = {
    .version = PLATFORM_API_VERSION,
    .clipboard = { 0},
    .window = {0},
    .ime = {0},
};

BOOL PlatformAPI_Init(struct PlatformAPI *api)
{
    if (!api || api->version != PLATFORM_API_VERSION)
        return FALSE;
    g_platformAPI = *api;
    return TRUE;
}

void PlatformAPI_Deinit(void)
{
    memset(&g_platformAPI, 0, sizeof(g_platformAPI));
}
