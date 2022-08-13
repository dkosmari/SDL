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

#include <whb/gfx.h>
#include <gx2/registers.h>
#include <gx2/state.h>
#include <gx2/draw.h>
#include <gx2r/buffer.h>
#include <gx2r/draw.h>

#define SCREEN_WIDTH    1280
#define SCREEN_HEIGHT   720

void WIIU_SDL_RenderPresent(SDL_Renderer * renderer)
{
    WIIU_RenderData *data = (WIIU_RenderData *) renderer->driverdata;
    WIIU_TextureData *tdata = (WIIU_TextureData *) data->windowTex.driverdata;
    Uint32 flags = SDL_GetWindowFlags(renderer->window);
    int win_x, win_y, win_w, win_h;

    if (renderer->info.flags & SDL_RENDERER_PRESENTVSYNC) {
    /*  NOTE watch libwhb's source to ensure this call only does vsync */
        WHBGfxBeginRender();
    }

    /* Calculate and save positions TODO: make use of the window position */
    if (flags & SDL_WINDOW_FULLSCREEN) {
        win_x = 0;
        win_y = 0;
        win_w = SCREEN_WIDTH;
        win_h = SCREEN_HEIGHT;
    } else {
        /* Center */
        SDL_GetWindowSize(renderer->window, &win_w, &win_h);
        win_x = (SCREEN_WIDTH - win_w) / 2;
        win_y = (SCREEN_HEIGHT - win_h) / 2;
    }

    /* Only render to TV if the window is *not* drc-only */
    if (!(flags & SDL_WINDOW_WIIU_GAMEPAD_ONLY)) {
        WHBGfxBeginRenderTV();
        GX2CopySurface(&tdata->texture.surface, 0, 0, &WHBGfxGetTVColourBuffer()->surface, 0, 0);
        GX2SetContextState(data->ctx);
        WHBGfxFinishRenderTV();
    }

    if (!(flags & SDL_WINDOW_WIIU_TV_ONLY)) {
        WHBGfxBeginRenderDRC();
        GX2CopySurface(&tdata->texture.surface, 0, 0, &WHBGfxGetDRCColourBuffer()->surface, 0, 0);
        GX2SetContextState(data->ctx);
        WHBGfxFinishRenderDRC();
    }

    WHBGfxFinishRender();

    /* Free the list of render and draw data */
    WIIU_FreeRenderData(data);
    WIIU_TextureDoneRendering(data);

    /* Restore SDL context state */
    GX2SetContextState(data->ctx);
}

#endif /* SDL_VIDEO_RENDER_WIIU */
