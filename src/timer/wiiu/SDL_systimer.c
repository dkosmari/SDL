/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2018 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"

#ifdef SDL_TIMER_WIIU

#include "SDL_thread.h"
#include "SDL_timer.h"
#include "SDL_error.h"
#include "../SDL_timer_c.h"
#include <coreinit/thread.h>
#include <coreinit/systeminfo.h>
#include <coreinit/time.h>

static OSTime start;
static SDL_bool ticks_started = SDL_FALSE;

void
SDL_TicksInit(void)
{
    if (ticks_started) {
        return;
    }
    ticks_started = SDL_TRUE;
    start = OSGetSystemTime();
}

void
SDL_TicksQuit(void)
{
    ticks_started = SDL_FALSE;
}

Uint64
SDL_GetTicks64(void)
{
    OSTime now;

    if (!ticks_started) {
        SDL_TicksInit();
    }

    now = OSGetSystemTime();
    return (Uint64)OSTicksToMilliseconds(now - start);
}

Uint64
SDL_GetPerformanceCounter(void)
{
    return OSGetTime();
}

Uint64
SDL_GetPerformanceFrequency(void)
{
    return OSTimerClockSpeed;
}

void
SDL_Delay(Uint32 ms)
{
   OSSleepTicks(OSMillisecondsToTicks(ms));
}

#endif /* SDL_TIMER_WIIU */

/* vim: ts=4 sw=4
 */
