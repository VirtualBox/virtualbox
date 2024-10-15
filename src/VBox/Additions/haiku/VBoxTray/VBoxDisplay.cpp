/* $Id: VBoxDisplay.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxDisplayService, Haiku Guest Additions, implementation.
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

#include <stdio.h>
#include <stdlib.h>
#include <new>
#include <DataIO.h>
#include <Message.h>
#include <TranslationUtils.h>
#include <TranslatorFormats.h>
#include <TranslatorRoster.h>
#include <String.h>

#include "VBoxGuestApplication.h"
#include "VBoxDisplay.h"
#include <VBoxGuestInternal.h>
#include "../VBoxVideo/common/VBoxVideo_common.h"

#include <iprt/mem.h>
#include <VBox/log.h>

#ifdef DEBUG_ramshankar
# undef Log
# define Log(x) printf x
# undef LogRel
# define LogRel(x) printf x
# undef LogRelFlowFunc
# define LogRelFlowFunc(x) printf x
#endif

VBoxDisplayService::VBoxDisplayService()
     : BHandler("VBoxDisplayService"),
       fClientId(-1),
       fServiceThreadID(-1),
       fExiting(false),
       fScreen(B_MAIN_SCREEN_ID)
{
}


VBoxDisplayService::~VBoxDisplayService()
{
}


void VBoxDisplayService::Start()
{
    status_t err;
    err = fServiceThreadID = spawn_thread(_ServiceThreadNub, "VBoxDisplayService", B_NORMAL_PRIORITY, this);
    if (err >= B_OK)
        resume_thread(fServiceThreadID);
    else
        LogRel(("VBoxDisplayService: Error starting service thread: %s\n", strerror(err)));
}


void VBoxDisplayService::MessageReceived(BMessage *message)
{
    if (message->what == B_QUIT_REQUESTED)
        fExiting = true;
    else
        BHandler::MessageReceived(message);
}


status_t VBoxDisplayService::_ServiceThreadNub(void *_this)
{
    VBoxDisplayService *service = (VBoxDisplayService *)_this;
    return service->_ServiceThread();
}


status_t VBoxDisplayService::_ServiceThread()
{
    LogFlow(("VBoxDisplayService::_ServiceThread"));

    VbglR3CtlFilterMask(VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST, 0);
    VbglR3SetGuestCaps(VMMDEV_GUEST_SUPPORTS_GRAPHICS, 0);
    for (;;)
    {
        uint32_t events;
        int rc = VbglR3WaitEvent(VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST, 5000, &events);
        if (   rc == VERR_TIMEOUT
            || rc == VERR_INTERRUPTED)
            continue;

        if (RT_SUCCESS(rc))
        {
            uint32_t cx, cy, cBits, iDisplay;
            int rc2 = VbglR3GetDisplayChangeRequest(&cx, &cy, &cBits, &iDisplay, NULL, NULL, NULL, NULL, true);
            LogFlow(("rc2=%d screen %d size changed (%d, %d, %d)\n", rc2, iDisplay, cx, cy, cBits));

            if (RT_SUCCESS(rc2))
            {
                display_mode mode;
                fScreen.GetMode(&mode);
                if (cBits == 0)
                    cBits = get_depth_for_color_space(mode.space);

                mode.timing.h_display = cx;
                mode.timing.v_display = cy;
                mode.space            = get_color_space_for_depth(cBits);
                mode.virtual_width    = cx;
                mode.virtual_height   = cy;

                /*= {
                    {0, cx, 0, 0, cBits * cx / 8, cy, 0, 0, cBits * cy / 8, 0},
                    get_color_space_for_depth(cBits),
                    cx, cy, 0, 0, 0
                };*/

                fScreen.SetMode(&mode, false);
            }
        }
        else
            fExiting = true;

        LogFlow(("processed host event rc = %d\n", rc));
        if (fExiting)
            break;
    }
    return 0;
}

