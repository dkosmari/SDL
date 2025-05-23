/*
  Simple DirectMedia Layer
  Copyright (C) 2018-2019 Ash Logan <ash@heyquark.com>
  Copyright (C) 2018-2019 Roberto Van Eeden <r.r.qwertyuiop.r.r@gmail.com>
  Copyright (C) 2022 GaryOderNichts <garyodernichts@gmail.com>

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

#if SDL_VIDEO_RENDER_WIIU

#include "../SDL_sysrender.h"
#include "SDL_render_wiiu.h"
#include "../../video/wiiu/SDL_wiiu_swkbd.h"

#include <gx2/registers.h>
#include <gx2/state.h>
#include <gx2/draw.h>
#include <gx2/swap.h>
#include <gx2/display.h>
#include <gx2r/draw.h>

static SDL_bool tvDrcEnabled = SDL_FALSE;

int WIIU_SDL_SetVSync(SDL_Renderer * renderer, const int vsync)
{
    GX2SetSwapInterval(vsync ? 1 : 0);

    if (GX2GetSwapInterval() > 0) {
        renderer->info.flags |= SDL_RENDERER_PRESENTVSYNC;
    } else {
        renderer->info.flags &= ~SDL_RENDERER_PRESENTVSYNC;
    }
    return 0;
}

#define WIIU_FIX_SWKBD_GAMMA

int WIIU_SDL_RenderPresent(SDL_Renderer * renderer)
{
    WIIU_RenderData *data = (WIIU_RenderData *) renderer->driverdata;
    WIIU_TextureData *tdata = (WIIU_TextureData *) data->windowTex.driverdata;
    Uint32 flags = SDL_GetWindowFlags(renderer->window);

    if (WIIU_SWKBD_IsScreenKeyboardShown(NULL, renderer->window)) {
        GX2SetContextState(NULL);
#ifdef WIIU_FIX_SWKBD_GAMMA
        if (tdata->cbuf.surface.format == GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8) {
            GX2SurfaceFormat old_format = tdata->cbuf.surface.format;
            tdata->cbuf.surface.format = GX2_SURFACE_FORMAT_SRGB_R8_G8_B8_A8;
            GX2InitColorBufferRegs(&tdata->cbuf);
            GX2SetColorBuffer(&tdata->cbuf, GX2_RENDER_TARGET_0);
            WIIU_SWKBD_Draw(renderer->window);
            tdata->cbuf.surface.format = old_format;
            GX2InitColorBufferRegs(&tdata->cbuf);
            GX2SetColorBuffer(&tdata->cbuf, GX2_RENDER_TARGET_0);
        } else {
            WIIU_SWKBD_Draw(renderer->window);
        }
#else
        WIIU_SWKBD_Draw(renderer->window);
#endif

    }

    /* Only render to TV if the window is *not* drc-only */
    if (!(flags & SDL_WINDOW_WIIU_GAMEPAD_ONLY)) {
        GX2CopyColorBufferToScanBuffer(&tdata->cbuf, GX2_SCAN_TARGET_TV);
    }

    if (!(flags & SDL_WINDOW_WIIU_TV_ONLY)) {
        GX2CopyColorBufferToScanBuffer(&tdata->cbuf, GX2_SCAN_TARGET_DRC);
    }

    /* Swap buffers */
    GX2SwapScanBuffers();
    GX2Flush();

    /* Restore SDL context state */
    GX2SetContextState(data->ctx);

    /* Notify renderer that the frame is complete */
    WIIU_FrameDone(data);

    /* TV and DRC can now be enabled after the first frame was drawn */
    if (!tvDrcEnabled) {
        GX2SetTVEnable(TRUE);
        GX2SetDRCEnable(TRUE);
        tvDrcEnabled = SDL_TRUE;
    }

    return 0;
}

#endif /* SDL_VIDEO_RENDER_WIIU */
