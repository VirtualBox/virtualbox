/* $Id: HGSMIChSetup.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Host Guest Shared Memory Interface (HGSMI), Host/Guest shared part.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef VBOX_INCLUDED_Graphics_HGSMIChSetup_h
#define VBOX_INCLUDED_Graphics_HGSMIChSetup_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "HGSMIDefs.h"

/* HGSMI setup and configuration channel commands and data structures. */
/*
 * Tell the host the location of hgsmi_host_flags structure, where the host
 * can write information about pending buffers, etc, and which can be quickly
 * polled by the guest without a need to port IO.
 */
#define HGSMI_CC_HOST_FLAGS_LOCATION 0

typedef struct HGSMIBUFFERLOCATION
{
    HGSMIOFFSET offLocation;
    HGSMISIZE   cbLocation;
} HGSMIBUFFERLOCATION;
AssertCompileSize(HGSMIBUFFERLOCATION, 8);

/* HGSMI setup and configuration data structures. */
/* host->guest commands pending, should be accessed under FIFO lock only */
#define HGSMIHOSTFLAGS_COMMANDS_PENDING    UINT32_C(0x01)
/* IRQ is fired, should be accessed under VGAState::lock only  */
#define HGSMIHOSTFLAGS_IRQ                 UINT32_C(0x02)
#ifdef VBOX_WITH_WDDM
/* one or more guest commands is completed, should be accessed under FIFO lock only */
# define HGSMIHOSTFLAGS_GCOMMAND_COMPLETED UINT32_C(0x04)
#endif
/* vsync interrupt flag, should be accessed under VGAState::lock only */
#define HGSMIHOSTFLAGS_VSYNC               UINT32_C(0x10)
/** monitor hotplug flag, should be accessed under VGAState::lock only */
#define HGSMIHOSTFLAGS_HOTPLUG             UINT32_C(0x20)
/**
 * Cursor capability state change flag, should be accessed under
 * VGAState::lock only.  @see VBVACONF32.
 */
#define HGSMIHOSTFLAGS_CURSOR_CAPABILITIES UINT32_C(0x40)

typedef struct HGSMIHOSTFLAGS
{
    /*
     * Host flags can be accessed and modified in multiple threads
     * concurrently, e.g. CrOpenGL HGCM and GUI threads when completing
     * HGSMI 3D and Video Accel respectively, EMT thread when dealing with
     * HGSMI command processing, etc.
     * Besides settings/cleaning flags atomically, some flags have their
     * own special sync restrictions, see comments for flags above.
     */
    volatile uint32_t u32HostFlags;
    uint32_t au32Reserved[3];
} HGSMIHOSTFLAGS;
AssertCompileSize(HGSMIHOSTFLAGS, 16);

#endif /* !VBOX_INCLUDED_Graphics_HGSMIChSetup_h */
