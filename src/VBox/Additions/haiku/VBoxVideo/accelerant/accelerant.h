/* $Id: accelerant.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxVideo Accelerant; Haiku Guest Additions, header.
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

#ifndef GA_INCLUDED_SRC_haiku_VBoxVideo_accelerant_accelerant_h
#define GA_INCLUDED_SRC_haiku_VBoxVideo_accelerant_accelerant_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <Accelerant.h>
#include "../common/VBoxVideo_common.h"

struct AccelerantInfo
{
    /** @todo doxygen document these fields  */
    int deviceFD;
    bool isClone;

    SharedInfo *sharedInfo;
    area_id sharedInfoArea;
};
extern AccelerantInfo gInfo;

/* General */
status_t vboxvideo_init_accelerant(int fd);
ssize_t vboxvideo_accelerant_clone_info_size(void);
void vboxvideo_get_accelerant_clone_info(void *data);
status_t vboxvideo_clone_accelerant(void *data);
void vboxvideo_uninit_accelerant(void);
status_t vboxvideo_get_accelerant_device_info(accelerant_device_info *adi);
sem_id vboxvideo_accelerant_retrace_semaphore(void);

/* Modes & constraints */
uint32 vboxvideo_accelerant_mode_count(void);
status_t vboxvideo_get_mode_list(display_mode *dm);
status_t vboxvideo_set_display_mode(display_mode *modeToSet);
status_t vboxvideo_get_display_mode(display_mode *currentMode);
status_t vboxvideo_get_edid_info(void *info, size_t size, uint32 *_version);
status_t vboxvideo_get_frame_buffer_config(frame_buffer_config *config);
status_t vboxvideo_get_pixel_clock_limits(display_mode *dm, uint32 *low, uint32 *high);

/* Cursor */
status_t vboxvideo_set_cursor_shape(uint16 width, uint16 height, uint16 hotX, uint16 hotY, uint8 *andMask, uint8 *xorMask);
void vboxvideo_move_cursor(uint16 x, uint16 y);
void vboxvideo_show_cursor(bool is_visible);

/* Accelerant engine */
uint32 vboxvideo_accelerant_engine_count(void);
status_t vboxvideo_acquire_engine(uint32 capabilities, uint32 maxWait, sync_token *st, engine_token **et);
status_t vboxvideo_release_engine(engine_token *et, sync_token *st);
void vboxvideo_wait_engine_idle(void);
status_t vboxvideo_get_sync_token(engine_token *et, sync_token *st);
status_t vboxvideo_sync_to_token(sync_token *st);

/* 2D acceleration */
void vboxvideo_screen_to_screen_blit(engine_token *et, blit_params *list, uint32 count);
void vboxvideo_fill_rectangle(engine_token *et, uint32 color, fill_rect_params *list, uint32 count);
void vboxvideo_invert_rectangle(engine_token *et, fill_rect_params *list, uint32 count);
void vboxvideo_fill_span(engine_token *et, uint32 color, uint16 *list, uint32 count);

#endif /* !GA_INCLUDED_SRC_haiku_VBoxVideo_accelerant_accelerant_h */

