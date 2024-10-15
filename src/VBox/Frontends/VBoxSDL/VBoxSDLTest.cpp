/* $Id: VBoxSDLTest.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 *
 * VBox frontends: VBoxSDL (simple frontend based on SDL):
 * VBoxSDL testcases
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifdef _MSC_VER
# pragma warning(push)
# pragma warning(disable:4121)
#endif
#if defined(RT_OS_WINDOWS) /// @todo someone please explain why we don't follow the book!
# define _SDL_main_h
#endif
#include <SDL.h>
#ifdef _MSC_VER
# pragma warning(pop)
#endif

#include <iprt/assert.h>
#include <iprt/env.h>
#include <iprt/initterm.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/time.h>

#include <stdlib.h>
#include <signal.h>

#ifdef VBOX_OPENGL
#include "SDL_opengl.h"
#endif

#ifdef RT_OS_WINDOWS
#define ESC_NORM
#define ESC_BOLD
#else
#define ESC_NORM "\033[m"
#define ESC_BOLD "\033[1m"
#endif

static SDL_Surface  *gSurfVRAM;            /* SDL virtual framebuffer surface */
static void         *gPtrVRAM;             /* allocated virtual framebuffer */
static SDL_Surface  *gScreen;              /* SDL screen surface */
static unsigned long guGuestXRes;          /* virtual framebuffer width */
static unsigned long guGuestYRes;          /* virtual framebuffer height */
static unsigned long guGuestBpp;           /* virtual framebuffer bits per pixel */
static unsigned long guMaxScreenWidth;     /* max screen width SDL allows */
static unsigned long guMaxScreenHeight;    /* max screen height SDL allows */
static int           gfResizable = 1;      /* SDL window is resizable */
static int           gfFullscreen = 0;         /* use fullscreen mode */
#ifdef VBOX_OPENGL
static unsigned long guTextureWidth;       /* width of OpenGL texture */
static unsigned long guTextureHeight;      /* height of OpenGL texture */
static unsigned int  gTexture;
static int           gfOpenGL;             /* use OpenGL as backend */
#endif
static unsigned int  guLoop = 1000;        /* Number of frame redrawings for each test */

static void bench(unsigned long w, unsigned long h, unsigned long bpp);
static void benchExecute(void);
static int  checkSDL(const char *fn, int rc);
static void checkEvents(void);

int
main(int argc, char **argv)
{
    int rc;
    RTR3InitExe(argc, &argv, 0);

    for (int i = 1; i < argc; i++)
    {
#ifdef VBOX_OPENGL
        if (strcmp(argv[i], "-gl") == 0)
        {
            gfOpenGL = 1;
            continue;
        }
#endif
        if (strcmp(argv[i], "-loop") == 0 && ++i < argc)
        {
            guLoop = atoi(argv[i]);
            continue;
        }
        RTPrintf("Unrecognized option '%s'\n", argv[i]);
        return -1;
    }

#ifdef RT_OS_WINDOWS
    /* Default to DirectX if nothing else set. "windib" would be possible.  */
    if (!RTEnvExist("SDL_VIDEODRIVER"))
    {
        _putenv("SDL_VIDEODRIVER=directx");
    }
#endif

#ifdef RT_OS_WINDOWS
    _putenv("SDL_VIDEO_WINDOW_POS=0,0");
#else
    RTEnvSet("SDL_VIDEO_WINDOW_POS", "0,0");
#endif

    rc = SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_NOPARACHUTE);
    if (rc != 0)
    {
        RTPrintf("Error: SDL_InitSubSystem failed with message '%s'\n", SDL_GetError());
        return -1;
    }

    /* output what SDL is capable of */
    const SDL_VideoInfo *videoInfo = SDL_GetVideoInfo();

    if (!videoInfo)
    {
        RTPrintf("No SDL video info available!\n");
        return -1;
    }

    RTPrintf("SDL capabilities:\n");
    RTPrintf("  Hardware surface support:                    %s\n", videoInfo->hw_available ? "yes" : "no");
    RTPrintf("  Window manager available:                    %s\n", videoInfo->wm_available ? "yes" : "no");
    RTPrintf("  Screen to screen blits accelerated:          %s\n", videoInfo->blit_hw ? "yes" : "no");
    RTPrintf("  Screen to screen colorkey blits accelerated: %s\n", videoInfo->blit_hw_CC ? "yes" : "no");
    RTPrintf("  Screen to screen alpha blits accelerated:    %s\n", videoInfo->blit_hw_A ? "yes" : "no");
    RTPrintf("  Memory to screen blits accelerated:          %s\n", videoInfo->blit_sw ? "yes" : "no");
    RTPrintf("  Memory to screen colorkey blits accelerated: %s\n", videoInfo->blit_sw_CC ? "yes" : "no");
    RTPrintf("  Memory to screen alpha blits accelerated:    %s\n", videoInfo->blit_sw_A ? "yes" : "no");
    RTPrintf("  Color fills accelerated:                     %s\n", videoInfo->blit_fill ? "yes" : "no");
    RTPrintf("  Video memory in kilobytes:                   %d\n", videoInfo->video_mem);
    RTPrintf("  Optimal bpp mode:                            %d\n", videoInfo->vfmt->BitsPerPixel);
    char buf[256];
    RTPrintf("Video driver SDL_VIDEODRIVER / active:         %s/%s\n", RTEnvGet("SDL_VIDEODRIVER"),
                                                                       SDL_VideoDriverName(buf, sizeof(buf)));

    RTPrintf("\n"
             "Starting tests. Any key pressed inside the SDL window will abort this\n"
             "program at the end of the current test. Iterations = %u\n", guLoop);

#ifdef VBOX_OPENGL
    RTPrintf("\n========== "ESC_BOLD"OpenGL is %s"ESC_NORM" ==========\n",
             gfOpenGL ? "ON" : "OFF");
#endif
    bench( 640,  480, 16);  bench( 640,  480, 24);  bench( 640,  480, 32);
    bench(1024,  768, 16);  bench(1024,  768, 24);  bench(1024,  768, 32);
    bench(1280, 1024, 16);  bench(1280, 1024, 24);  bench(1280, 1024, 32);

    RTPrintf("\nSuccess!\n");
    return 0;
}

/**
 * Method that does the actual resize of the guest framebuffer and
 * then changes the SDL framebuffer setup.
 */
static void bench(unsigned long w, unsigned long h, unsigned long bpp)
{
    Uint32 Rmask,  Gmask,  Bmask, Amask = 0;
    Uint32 Rsize,  Gsize,  Bsize;
    Uint32 newWidth, newHeight;

    guGuestXRes = w;
    guGuestYRes = h;
    guGuestBpp  = bpp;

    RTPrintf("\n");

    /* a different format we support directly? */
    switch (guGuestBpp)
    {
        case 16:
        {
            Rmask = 0xF800;
            Gmask = 0x07E0;
            Bmask = 0x001F;
            Amask = 0x0000;
            Rsize  = 5;
            Gsize  = 6;
            Bsize  = 5;
            break;
        }

        case 24:
        {
            Rmask = 0x00FF0000;
            Gmask = 0x0000FF00;
            Bmask = 0x000000FF;
            Amask = 0x00000000;
            Rsize  = 8;
            Gsize  = 8;
            Bsize  = 8;
            break;
        }

        default:
            Rmask = 0x00FF0000;
            Gmask = 0x0000FF00;
            Bmask = 0x000000FF;
            Amask = 0x00000000;
            Rsize  = 8;
            Gsize  = 8;
            Bsize  = 8;
            break;
    }

    int sdlFlags = SDL_HWSURFACE | SDL_ASYNCBLIT | SDL_HWACCEL;
#ifdef VBOX_OPENGL
    if (gfOpenGL)
        sdlFlags |= SDL_OPENGL;
#endif
    if (gfResizable)
        sdlFlags |= SDL_RESIZABLE;
    if (gfFullscreen)
        sdlFlags |= SDL_FULLSCREEN;

    /*
     * Now we have to check whether there are video mode restrictions
     */
    SDL_Rect **modes;
    /* Get available fullscreen/hardware modes */
    modes = SDL_ListModes(NULL, sdlFlags);
    if (modes == NULL)
    {
        RTPrintf("Error: SDL_ListModes failed with message '%s'\n", SDL_GetError());
        return;
    }

    /* -1 means that any mode is possible (usually non fullscreen) */
    if (modes != (SDL_Rect **)-1)
    {
        /*
         * according to the SDL documentation, the API guarantees that
         * the modes are sorted from larger to smaller, so we just
         * take the first entry as the maximum.
         */
        guMaxScreenWidth  = modes[0]->w;
        guMaxScreenHeight = modes[0]->h;
    }
    else
    {
        /* no restriction */
        guMaxScreenWidth  = ~0U;
        guMaxScreenHeight = ~0U;
    }

    newWidth  = RT_MIN(guMaxScreenWidth,  guGuestXRes);
    newHeight = RT_MIN(guMaxScreenHeight, guGuestYRes);

    /*
     * Now set the screen resolution and get the surface pointer
     * @todo BPP is not supported!
     */
#ifdef VBOX_OPENGL
    if (gfOpenGL)
    {
        checkSDL("SDL_GL_SetAttribute", SDL_GL_SetAttribute(SDL_GL_RED_SIZE,   Rsize));
        checkSDL("SDL_GL_SetAttribute", SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, Gsize));
        checkSDL("SDL_GL_SetAttribute", SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,  Bsize));
        checkSDL("SDL_GL_SetAttribute", SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0));
    }
#else
    NOREF(Rsize); NOREF(Gsize); NOREF(Bsize);
#endif

    RTPrintf("Testing " ESC_BOLD "%ldx%ld@%ld" ESC_NORM "\n", guGuestXRes, guGuestYRes, guGuestBpp);

    gScreen = SDL_SetVideoMode(newWidth, newHeight, 0, sdlFlags);
    if (!gScreen)
    {
        RTPrintf("SDL_SetVideoMode failed (%s)\n", SDL_GetError());
        return;
    }

    /* first free the current surface */
    if (gSurfVRAM)
    {
        SDL_FreeSurface(gSurfVRAM);
        gSurfVRAM = NULL;
    }
    if (gPtrVRAM)
    {
        free(gPtrVRAM);
        gPtrVRAM = NULL;
    }

    if (gScreen->format->BitsPerPixel != guGuestBpp)
    {
        /* Create a source surface from guest VRAM. */
        int bytes_per_pixel = (guGuestBpp + 7) / 8;
        gPtrVRAM  = malloc(guGuestXRes * guGuestYRes * bytes_per_pixel);
        gSurfVRAM = SDL_CreateRGBSurfaceFrom(gPtrVRAM, guGuestXRes, guGuestYRes, guGuestBpp,
                                             bytes_per_pixel * guGuestXRes,
                                             Rmask, Gmask, Bmask, Amask);
    }
    else
    {
        /* Create a software surface for which SDL allocates the RAM */
        gSurfVRAM = SDL_CreateRGBSurface(SDL_SWSURFACE, guGuestXRes, guGuestYRes, guGuestBpp,
                                         Rmask, Gmask, Bmask, Amask);
    }

    if (!gSurfVRAM)
    {
        RTPrintf("Failed to allocate surface %ldx%ld@%ld\n",
                guGuestXRes, guGuestYRes, guGuestBpp);
        return;
    }

    RTPrintf("  gScreen=%dx%d@%d (surface: %s)\n",
            gScreen->w, gScreen->h, gScreen->format->BitsPerPixel,
             (gScreen->flags & SDL_HWSURFACE) == 0 ? "software" : "hardware");

    SDL_Rect rect = { 0, 0, (Uint16)guGuestXRes, (Uint16)guGuestYRes };
    checkSDL("SDL_FillRect",
              SDL_FillRect(gSurfVRAM, &rect,
                           SDL_MapRGB(gSurfVRAM->format, 0x5F, 0x6F, 0x1F)));

#ifdef VBOX_OPENGL
    if (gfOpenGL)
    {
        int r, g, b, d, o;
        SDL_GL_GetAttribute(SDL_GL_RED_SIZE,     &r);
        SDL_GL_GetAttribute(SDL_GL_GREEN_SIZE,   &g);
        SDL_GL_GetAttribute(SDL_GL_BLUE_SIZE,    &b);
        SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE,   &d);
        SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER, &o);
        RTPrintf("  OpenGL ctxt red=%d, green=%d, blue=%d, depth=%d, dbl=%d", r, g, b, d, o);

        glEnable(GL_TEXTURE_2D);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glGenTextures(1, &gTexture);
        glBindTexture(GL_TEXTURE_2D, gTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

        for (guTextureWidth  = 32; guTextureWidth  < newWidth;  guTextureWidth  <<= 1)
            ;
        for (guTextureHeight = 32; guTextureHeight < newHeight; guTextureHeight <<= 1)
            ;
        RTPrintf(", tex %ldx%ld\n", guTextureWidth, guTextureHeight);

        switch (guGuestBpp)
        {
            case 16: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5, guTextureWidth, guTextureHeight, 0,
                                  GL_RGB,  GL_UNSIGNED_SHORT_5_6_5, 0);
                     break;
            case 24: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,  guTextureWidth, guTextureHeight, 0,
                                  GL_BGR,  GL_UNSIGNED_BYTE, 0);
                     break;
            case 32: glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, guTextureWidth, guTextureHeight, 0,
                                  GL_BGRA, GL_UNSIGNED_BYTE, 0);
                     break;
            default: RTPrintf("guGuestBpp=%d?\n", guGuestBpp);
                     return;
        }

        glViewport(0, 0, newWidth, newHeight);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, newWidth, newHeight, 0.0, -1.0, 1.0);
    }
#endif

    checkEvents();
    benchExecute();

#ifdef VBOX_OPENGL
    if (gfOpenGL)
    {
        glDeleteTextures(1, &gTexture);
    }
#endif
}

static void benchExecute()
{
    SDL_Rect rect = { 0, 0, (Uint16)guGuestXRes, (Uint16)guGuestYRes };
    RTTIMESPEC t1, t2;

    RTTimeNow(&t1);
    for (unsigned i=0; i<guLoop; i++)
    {
#ifdef VBOX_OPENGL
        if (!gfOpenGL)
        {
#endif
            /* SDL backend */
            checkSDL("SDL_BlitSurface", SDL_BlitSurface(gSurfVRAM, &rect, gScreen, &rect));
            if ((gScreen->flags & SDL_HWSURFACE) == 0)
                SDL_UpdateRect(gScreen, rect.x, rect.y, rect.w, rect.h);
#ifdef VBOX_OPENGL
        }
        else
        {
            /* OpenGL backend */
            glBindTexture(GL_TEXTURE_2D, gTexture);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, rect.x);
            glPixelStorei(GL_UNPACK_SKIP_ROWS,   rect.y);
            glPixelStorei(GL_UNPACK_ROW_LENGTH,  gSurfVRAM->pitch / gSurfVRAM->format->BytesPerPixel);
            switch (gSurfVRAM->format->BitsPerPixel)
            {
                case 16: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rect.w, rect.h,
                                         GL_RGB, GL_UNSIGNED_SHORT_5_6_5, gSurfVRAM->pixels);
                         break;
                case 24: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rect.w, rect.h,
                                         GL_BGR, GL_UNSIGNED_BYTE, gSurfVRAM->pixels);
                         break;
                case 32: glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, rect.w, rect.h,
                                         GL_BGRA, GL_UNSIGNED_BYTE, gSurfVRAM->pixels);
                         break;
                default: RTPrintf("BitsPerPixel=%d?\n", gSurfVRAM->format->BitsPerPixel);
                         return;
            }
            GLfloat tx = (GLfloat)((float)rect.w) / guTextureWidth;
            GLfloat ty = (GLfloat)((float)rect.h) / guTextureHeight;
            glBegin(GL_QUADS);
            glColor4f(1.0, 1.0, 1.0, 1.0);
            glTexCoord2f(0.0, 0.0);  glVertex2i(rect.x,          rect.y         );
            glTexCoord2f(0.0,  ty);  glVertex2i(rect.x,          rect.y + rect.h);
            glTexCoord2f(tx,   ty);  glVertex2i(rect.x + rect.w, rect.y + rect.h);
            glTexCoord2f(tx,  0.0);  glVertex2i(rect.x + rect.w, rect.y         );
            glEnd();
            glFlush();
        }
#endif
    }
    RTTimeNow(&t2);
    int64_t ms = RTTimeSpecGetMilli(&t2) - RTTimeSpecGetMilli(&t1);
    printf("  %.1fms/frame\n", (double)ms / guLoop);
}

static int checkSDL(const char *fn, int rc)
{
    if (rc == -1)
        RTPrintf("" ESC_BOLD "%s() failed:" ESC_NORM " '%s'\n", fn, SDL_GetError());

    return rc;
}

static void checkEvents(void)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
            case SDL_KEYDOWN:
                RTPrintf("\nKey pressed, exiting ...\n");
                exit(-1);
                break;
        }
    }
}
