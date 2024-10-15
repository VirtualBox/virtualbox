/* $Id: tstGuestCtrlContextID.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Context ID makeup/extraction test cases.
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

#define LOG_ENABLED
#define LOG_GROUP LOG_GROUP_MAIN
#include <VBox/log.h>

#include "../include/GuestCtrlImplPrivate.h"

using namespace com;

#include <iprt/env.h>
#include <iprt/rand.h>
#include <iprt/stream.h>
#include <iprt/test.h>

int main()
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstGuestCtrlContextID", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    RTTestIPrintf(RTTESTLVL_DEBUG, "Initializing COM...\n");
    HRESULT hrc = com::Initialize();
    if (FAILED(hrc))
    {
        RTTestFailed(hTest, "Failed to initialize COM (%Rhrc)!\n", hrc);
        return RTEXITCODE_FAILURE;
    }

    /* Don't let the assertions trigger here
     * -- we rely on the return values in the test(s) below. */
    RTAssertSetQuiet(true);

#if 0
    for (int t = 0; t < 4 && !RTTestErrorCount(hTest); t++)
    {
        uint32_t uSession = RTRandU32Ex(0, VBOX_GUESTCTRL_MAX_SESSIONS - 1);
        uint32_t uFilter = VBOX_GUESTCTRL_CONTEXTID_MAKE_SESSION(uSession);
        RTTestIPrintf(RTTESTLVL_INFO, "Session: %RU32, Filter: %x\n", uSession, uFilter);

        uint32_t uSession2 = RTRandU32Ex(0, VBOX_GUESTCTRL_MAX_SESSIONS - 1);
        uint32_t uCID = VBOX_GUESTCTRL_CONTEXTID_MAKE(uSession2,
                                                      RTRandU32Ex(0, VBOX_GUESTCTRL_MAX_OBJECTS - 1),
                                                      RTRandU32Ex(0, VBOX_GUESTCTRL_MAX_CONTEXTS - 1));
        RTTestIPrintf(RTTESTLVL_INFO, "CID: %x (Session: %d), Masked: %x\n",
                      uCID, VBOX_GUESTCTRL_CONTEXTID_GET_SESSION(uCID), uCID & uFilter);
        if ((uCID & uFilter) == uCID)
        {
            RTTestIPrintf(RTTESTLVL_INFO, "=========== Masking works: %x vs. %x\n",
                          uCID & uFilter, uFilter);
        }
    }
#endif

    uint32_t uContextMax = UINT32_MAX;
    RTTestIPrintf(RTTESTLVL_DEBUG, "Max context is: %RU32\n", uContextMax);

    /* Do 4048 tests total. */
    for (int t = 0; t < 4048 && !RTTestErrorCount(hTest); t++)
    {
        /* VBOX_GUESTCTRL_MAX_* includes 0 as an object, so subtract one. */
        uint32_t s = RTRandU32Ex(0, VBOX_GUESTCTRL_MAX_SESSIONS - 1);
        uint32_t p = RTRandU32Ex(0, VBOX_GUESTCTRL_MAX_OBJECTS - 1);
        uint32_t c = RTRandU32Ex(0, VBOX_GUESTCTRL_MAX_CONTEXTS - 1);

        uint64_t uContextID = VBOX_GUESTCTRL_CONTEXTID_MAKE(s, p, c);
        RTTestIPrintf(RTTESTLVL_DEBUG, "ContextID (%d,%d,%d) = %RU32\n", s, p, c, uContextID);
        if (s != VBOX_GUESTCTRL_CONTEXTID_GET_SESSION(uContextID))
        {
            RTTestFailed(hTest, "%d,%d,%d: Session is %d, expected %d -> %RU64\n",
                         s, p, c, VBOX_GUESTCTRL_CONTEXTID_GET_SESSION(uContextID), s, uContextID);
        }
        else if (p != VBOX_GUESTCTRL_CONTEXTID_GET_OBJECT(uContextID))
        {
            RTTestFailed(hTest, "%d,%d,%d: Object is %d, expected %d -> %RU64\n",
                         s, p, c, VBOX_GUESTCTRL_CONTEXTID_GET_OBJECT(uContextID), p, uContextID);
        }
        if (c != VBOX_GUESTCTRL_CONTEXTID_GET_COUNT(uContextID))
        {
            RTTestFailed(hTest, "%d,%d,%d: Count is %d, expected %d -> %RU64\n",
                         s, p, c, VBOX_GUESTCTRL_CONTEXTID_GET_COUNT(uContextID), c, uContextID);
        }
        if (uContextID > UINT32_MAX)
        {
            RTTestFailed(hTest, "%d,%d,%d: Value overflow; does not fit anymore: %RU64\n",
                         s, p, c, uContextID);
        }
    }

    RTTestIPrintf(RTTESTLVL_DEBUG, "Shutting down COM...\n");
    com::Shutdown();

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

