/* $Id: accelerant.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxVideo Accelerant; Haiku Guest Additions, implementation.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

/*
 * This code is based on:
 *
 * VirtualBox Guest Additions for Haiku.
 * Copyright (c) 2011 Mike Smith <mike@scgtrp.net>
 *                    François Revol <revol@free.fr>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <Accelerant.h>
#include "accelerant.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
AccelerantInfo gInfo;
static engine_token sEngineToken = { 1, 0 /*B_2D_ACCELERATION*/, NULL };

/** @todo r=ramshankar: get rid of this and replace with IPRT logging. */
#define TRACE(x...) do { \
    FILE* logfile = fopen("/var/log/vboxvideo.accelerant.log", "a"); \
    fprintf(logfile, x); \
    fflush(logfile); \
    fsync(fileno(logfile)); \
    fclose(logfile); \
    sync(); \
     } while(0)

class AreaCloner
{
    public:
        AreaCloner()
            : fArea(-1)
        {
        }

        ~AreaCloner()
        {
            if (fArea >= B_OK)
                delete_area(fArea);
        }

        area_id Clone(const char *name, void **_address, uint32 spec, uint32 protection, area_id sourceArea)
        {
            fArea = clone_area(name, _address, spec, protection, sourceArea);
            return fArea;
        }

        status_t InitCheck()
        {
            return fArea < B_OK ? (status_t)fArea : B_OK;
        }

        void Keep()
        {
            fArea = -1;
        }

    private:
        area_id fArea;
};

extern "C"
void* get_accelerant_hook(uint32 feature, void *data)
{
    TRACE("%s\n", __FUNCTION__);
    switch (feature)
    {
        /* General */
        case B_INIT_ACCELERANT:
            return (void *)vboxvideo_init_accelerant;
        case B_UNINIT_ACCELERANT:
            return (void *)vboxvideo_uninit_accelerant;
        case B_CLONE_ACCELERANT:
            return (void *)vboxvideo_clone_accelerant;
        case B_ACCELERANT_CLONE_INFO_SIZE:
            return (void *)vboxvideo_accelerant_clone_info_size;
        case B_GET_ACCELERANT_CLONE_INFO:
            return (void *)vboxvideo_get_accelerant_clone_info;
        case B_GET_ACCELERANT_DEVICE_INFO:
            return (void *)vboxvideo_get_accelerant_device_info;
        case B_ACCELERANT_RETRACE_SEMAPHORE:
            return (void *)vboxvideo_accelerant_retrace_semaphore;

        /* Mode configuration */
        case B_ACCELERANT_MODE_COUNT:
            return (void *)vboxvideo_accelerant_mode_count;
        case B_GET_MODE_LIST:
            return (void *)vboxvideo_get_mode_list;
        case B_SET_DISPLAY_MODE:
            return (void *)vboxvideo_set_display_mode;
        case B_GET_DISPLAY_MODE:
            return (void *)vboxvideo_get_display_mode;
        case B_GET_EDID_INFO:
            return (void *)vboxvideo_get_edid_info;
        case B_GET_FRAME_BUFFER_CONFIG:
            return (void *)vboxvideo_get_frame_buffer_config;
        case B_GET_PIXEL_CLOCK_LIMITS:
            return (void *)vboxvideo_get_pixel_clock_limits;

#if 0
            /* cursor managment */
            case B_SET_CURSOR_SHAPE:
                return (void*)vboxvideo_set_cursor_shape;
            case B_MOVE_CURSOR:
                return (void*)vboxvideo_move_cursor;
            case B_SHOW_CURSOR:
                return (void*)vboxvideo_show_cursor;
#endif

        /* Engine/synchronization */
        case B_ACCELERANT_ENGINE_COUNT:
            return (void *)vboxvideo_accelerant_engine_count;
        case B_ACQUIRE_ENGINE:
            return (void *)vboxvideo_acquire_engine;
        case B_RELEASE_ENGINE:
            return (void *)vboxvideo_release_engine;
        case B_WAIT_ENGINE_IDLE:
            return (void *)vboxvideo_wait_engine_idle;
        case B_GET_SYNC_TOKEN:
            return (void *)vboxvideo_get_sync_token;
        case B_SYNC_TO_TOKEN:
            return (void *)vboxvideo_sync_to_token;
    }

    return NULL;
}

status_t vboxvideo_init_common(int fd, bool cloned)
{
    unlink("/var/log/vboxvideo.accelerant.log"); // clear old log - next TRACE() will recreate it
    TRACE("%s\n", __FUNCTION__);

    gInfo.deviceFD = fd;
    gInfo.isClone = cloned;
    gInfo.sharedInfo = NULL;
    gInfo.sharedInfoArea = -1;

    area_id sharedArea;
    if (ioctl(gInfo.deviceFD, VBOXVIDEO_GET_PRIVATE_DATA, &sharedArea, sizeof(area_id)) != 0)
    {
        TRACE("ioctl failed\n");
        return B_ERROR;
    }

    AreaCloner sharedCloner;
    gInfo.sharedInfoArea = sharedCloner.Clone("vboxvideo shared info", (void **)&gInfo.sharedInfo, B_ANY_ADDRESS,
                                              B_READ_AREA | B_WRITE_AREA, sharedArea);
    status_t status = sharedCloner.InitCheck();
    if (status < B_OK)
    {
        TRACE("InitCheck failed (%s)\n", strerror(status));
        return status;
    }
    sharedCloner.Keep();

    return B_OK;
}


status_t vboxvideo_init_accelerant(int fd)
{
    return vboxvideo_init_common(fd, false);
}


ssize_t vboxvideo_accelerant_clone_info_size(void)
{
    TRACE("%s\n", __FUNCTION__);
    return B_PATH_NAME_LENGTH;
}


void vboxvideo_get_accelerant_clone_info(void *data)
{
    TRACE("%s\n", __FUNCTION__);
    ioctl(gInfo.deviceFD, VBOXVIDEO_GET_DEVICE_NAME, data, B_PATH_NAME_LENGTH);
}


status_t vboxvideo_clone_accelerant(void *data)
{
    TRACE("%s\n", __FUNCTION__);

    /* Create full device name */
    char path[MAXPATHLEN];
    strcpy(path, "/dev/");
    strcat(path, (const char *)data);

    int fd = open(path, B_READ_WRITE);
    if (fd < 0)
        return errno;

    return vboxvideo_init_common(fd, true);
}


void vboxvideo_uninit_accelerant(void)
{
    delete_area(gInfo.sharedInfoArea);
    gInfo.sharedInfo = NULL;
    gInfo.sharedInfoArea = -1;

    if (gInfo.isClone)
        close(gInfo.deviceFD);

    TRACE("%s\n", __FUNCTION__);
}


status_t vboxvideo_get_accelerant_device_info(accelerant_device_info *adi)
{
    TRACE("%s\n", __FUNCTION__);
    adi->version = B_ACCELERANT_VERSION;
    strcpy(adi->name, "Virtual display");
    strcpy(adi->chipset, "VirtualBox Graphics Adapter");
    strcpy(adi->serial_no, "9001");
    return B_OK;
}


sem_id vboxvideo_accelerant_retrace_semaphore(void)
{
    TRACE("%s\n", __FUNCTION__);
    return -1;
}


// modes & constraints
uint32 vboxvideo_accelerant_mode_count(void)
{
    TRACE("%s\n", __FUNCTION__);
    return 1;
}


status_t vboxvideo_get_mode_list(display_mode *dm)
{
    /// @todo return some standard modes here
    TRACE("%s\n", __FUNCTION__);
    return vboxvideo_get_display_mode(dm);
}


status_t vboxvideo_set_display_mode(display_mode *modeToSet)
{
    TRACE("%s\n", __FUNCTION__);
    TRACE("trying to set mode %dx%d\n", modeToSet->timing.h_display, modeToSet->timing.v_display);
    return ioctl(gInfo.deviceFD, VBOXVIDEO_SET_DISPLAY_MODE, modeToSet, sizeof(display_mode));
}


status_t vboxvideo_get_display_mode(display_mode *currentMode)
{
    TRACE("%s\n", __FUNCTION__);
    *currentMode = gInfo.sharedInfo->currentMode;
    TRACE("current mode is %dx%d\n", currentMode->timing.h_display, currentMode->timing.v_display);
    return B_OK;
}


status_t vboxvideo_get_edid_info(void *info, size_t size, uint32 *_version)
{
    TRACE("%s\n", __FUNCTION__);

    /* Copied from the X11 implementation: */
    static const uint8 edid_data[128] = {
        0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, /* header */
        0x58, 0x58, /* manufacturer (VBX) */
        0x00, 0x00, /* product code */
        0x00, 0x00, 0x00, 0x00, /* serial number goes here */
        0x01, /* week of manufacture */
        0x00, /* year of manufacture */
        0x01, 0x03, /* EDID version */
        0x80, /* capabilities - digital */
        0x00, /* horiz. res in cm, zero for projectors */
        0x00, /* vert. res in cm */
        0x78, /* display gamma (120 == 2.2).  Should we ask the host for this? */
        0xEE, /* features (standby, suspend, off, RGB, standard colour space,
                * preferred timing mode) */
        0xEE, 0x91, 0xA3, 0x54, 0x4C, 0x99, 0x26, 0x0F, 0x50, 0x54,
        /* chromaticity for standard colour space - should we ask the host? */
        0x00, 0x00, 0x00, /* no default timings */
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        0x01, 0x01, 0x01, 0x01, /* no standard timings */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* descriptor block 1 goes here */
        0x00, 0x00, 0x00, 0xFD, 0x00, /* descriptor block 2, monitor ranges */
        0x00, 0xC8, 0x00, 0xC8, 0x64, 0x00, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20, /* 0-200Hz vertical, 0-200KHz horizontal, 1000MHz pixel clock */
        0x00, 0x00, 0x00, 0xFC, 0x00, /* descriptor block 3, monitor name */
        'V', 'B', 'O', 'X', ' ', 'm', 'o', 'n', 'i', 't', 'o', 'r', '\n',
        0x00, 0x00, 0x00, 0x10, 0x00, /* descriptor block 4: dummy data */
        0x0A, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
        0x20,
        0x00, /* number of extensions */
        0x00 /* checksum goes here */
    };

    if (size < 128)
        return B_BUFFER_OVERFLOW;

    *_version = 1; /* EDID_VERSION_1 */
    memcpy(info, edid_data, 128);
    return B_OK;
}


status_t vboxvideo_get_frame_buffer_config(frame_buffer_config *config)
{
    TRACE("%s\n", __FUNCTION__);
    config->frame_buffer = gInfo.sharedInfo->framebuffer;
    config->frame_buffer_dma = NULL;
    config->bytes_per_row = get_depth_for_color_space(gInfo.sharedInfo->currentMode.space)
                            * gInfo.sharedInfo->currentMode.timing.h_display / 8;
    return B_OK;
}


status_t vboxvideo_get_pixel_clock_limits(display_mode *dm, uint32 *low, uint32 *high)
{
    TRACE("%s\n", __FUNCTION__);
    // irrelevant for virtual monitors
    *low = 0;
    *high = 9001;
    return B_OK;
}


/* Cursor */
status_t vboxvideo_set_cursor_shape(uint16 width, uint16 height, uint16 hotX, uint16 hotY, uint8 *andMask, uint8 *xorMask)
{
    TRACE("%s\n", __FUNCTION__);
    // VBoxHGSMIUpdatePointerShape
    return B_UNSUPPORTED;
}


void vboxvideo_move_cursor(uint16 x, uint16 y)
{
    TRACE("%s\n", __FUNCTION__);
}


void vboxvideo_show_cursor(bool is_visible)
{
    TRACE("%s\n", __FUNCTION__);
}


/* Accelerant engine */
uint32 vboxvideo_accelerant_engine_count(void)
{
    TRACE("%s\n", __FUNCTION__);
    return 1;
}

status_t vboxvideo_acquire_engine(uint32 capabilities, uint32 maxWait, sync_token *st, engine_token **et)
{
    TRACE("%s\n", __FUNCTION__);
    *et = &sEngineToken;
    return B_OK;
}


status_t vboxvideo_release_engine(engine_token *et, sync_token *st)
{
    TRACE("%s\n", __FUNCTION__);
    if (st != NULL)
        st->engine_id = et->engine_id;

    return B_OK;
}


void vboxvideo_wait_engine_idle(void)
{
    TRACE("%s\n", __FUNCTION__);
}


status_t vboxvideo_get_sync_token(engine_token *et, sync_token *st)
{
    TRACE("%s\n", __FUNCTION__);
    return B_OK;
}


status_t vboxvideo_sync_to_token(sync_token *st)
{
    TRACE("%s\n", __FUNCTION__);
    return B_OK;
}


/* 2D acceleration */
void vboxvideo_screen_to_screen_blit(engine_token *et, blit_params *list, uint32 count)
{
    TRACE("%s\n", __FUNCTION__);
}


void vboxvideo_fill_rectangle(engine_token *et, uint32 color, fill_rect_params *list, uint32 count)
{
    TRACE("%s\n", __FUNCTION__);
}


void vboxvideo_invert_rectangle(engine_token *et, fill_rect_params *list, uint32 count)
{
    TRACE("%s\n", __FUNCTION__);
}


void vboxvideo_fill_span(engine_token *et, uint32 color, uint16 *list, uint32 count)
{
    TRACE("%s\n", __FUNCTION__);
}
