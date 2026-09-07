/* $Id: tstHGCMSvc.cpp 115169 2026-09-07 15:16:40Z andreas.loeffler@oracle.com $ */
/** @file
 * HGCM Service Testcase.
 */

/*
 * Copyright (C) 2009-2026 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <VBox/hgcmsvc.h>
#include <VBox/HostServices/Service.h>
#include <iprt/initterm.h>
#include <iprt/test.h>

/** Test the getString member function.  Indirectly tests the getPointer
 * and getBuffer APIs.
 * @param  hTest  an running IPRT test
 * @param  type  the type that the parameter should be set to before
 *                calling getString
 * @param  pcch   the value that the parameter should be set to before
 *                calling getString, and also the address (!) which we
 *                expect getString to return.  Stricter than needed of
 *                course, but I was feeling lazy.
 * @param  cb     the size that the parameter should be set to before
 *                calling getString, and also the size which we expect
 *                getString to return.
 * @param  rcExp  the expected return value of the call to getString.
 */
static void doTestGetString(VBOXHGCMSVCPARM *pParm, RTTEST hTest, uint32_t type,
                            const char *pcch, uint32_t cb, int rcExp)
{
    /* An RTTest API like this, which would print out an additional line
     * of context if a test failed, would be nice.  This is because the
     * line number alone doesn't help much here, given that this is a
     * subroutine called many times. */
    /*
    RTTestContextF(hTest,
                   ("doTestGetString, type=%u, pcch=%p, acp=%u, rcExp=%Rrc",
                    type, pcch, acp, rcExp));
     */
    HGCMSvcSetPv(pParm, (void *)pcch, cb);
    pParm->type = type;  /* in case we don't want VBOX_HGCM_SVC_PARM_PTR */
    const char *pcch2 = NULL;
    uint32_t cb2 = 0;
    int rc = HGCMSvcGetCStr(pParm, &pcch2, &cb2);
    RTTEST_CHECK_RC(hTest, rc, rcExp);
    if (RT_SUCCESS(rcExp))
    {
        RTTEST_CHECK_MSG_RETV(hTest, (pcch2 == pcch),
                              (hTest, "expected %p, got %p", pcch, pcch2));
        RTTEST_CHECK_MSG_RETV(hTest, (cb2 == cb),
                              (hTest, "expected %u, got %u", cb, cb2));
    }
}

/** Run some unit tests on the getString method and indirectly test
 * getPointer and getBuffer as well. */
static void testGetString(VBOXHGCMSVCPARM *pParm, RTTEST hTest)
{
    RTTestSub(hTest, "HGCM string parameter handling");
    doTestGetString(pParm, hTest, VBOX_HGCM_SVC_PARM_32BIT, "test", 3,
                    VERR_INVALID_PARAMETER);
    doTestGetString(pParm, hTest, VBOX_HGCM_SVC_PARM_PTR, "test", 5,
                    VINF_SUCCESS);
    doTestGetString(pParm, hTest, VBOX_HGCM_SVC_PARM_PTR, "test", 3,
                    VERR_BUFFER_OVERFLOW);
    doTestGetString(pParm, hTest, VBOX_HGCM_SVC_PARM_PTR, "test\xf0", 6,
                    VERR_INVALID_UTF8_ENCODING);
    doTestGetString(pParm, hTest, VBOX_HGCM_SVC_PARM_PTR, "test", 0,
                    VERR_INVALID_PARAMETER);
    doTestGetString(pParm, hTest, VBOX_HGCM_SVC_PARM_PTR, (const char *)0x1, 5,
                    VERR_INVALID_PARAMETER);
    RTTestSubDone(hTest);
}

/** Tests copying queued HGCM message parameters to caller-provided storage. */
static void testMessageCopyParms(RTTEST hTest)
{
    RTTestSub(hTest, "HGCM message parameter copying");

    /* Verify that a queued pointer is copied into a matching retrieval buffer. */
    static const uint8_t s_abMessageData[] = { 0x42, 0x43, 0x44, 0x45 };
    VBOXHGCMSVCPARM ParmSrc;
    HGCMSvcSetPv(&ParmSrc, (void *)&s_abMessageData[0], sizeof(s_abMessageData)); /* Queued pointer. */
    HGCM::Message Message(42, 1, &ParmSrc);

    uint8_t abDst[sizeof(s_abMessageData)] = { 0 };
    VBOXHGCMSVCPARM ParmDst;
    HGCMSvcSetPv(&ParmDst, &abDst[0], sizeof(abDst)); /* Retrieval buffer. */
    RTTEST_CHECK_RC(hTest, Message.GetData(42, 1, &ParmDst), VINF_SUCCESS);
    RTTEST_CHECK(hTest, memcmp(&abDst[0], &s_abMessageData[0], sizeof(abDst)) == 0);

    /* Verify that pointer retrieval rejects page-list storage without changing it. */
    static const uint8_t s_abPageData[] = { 0x11, 0x22, 0x33, 0x44,
                                            0x55, 0x66, 0x77, 0x88 };
    uint8_t abPage[sizeof(s_abPageData)];
    memcpy(&abPage[0], &s_abPageData[0], sizeof(abPage));
    void *apvPages[1] = { &abPage[0] };

    VBOXHGCMSVCPARM ParmPages;
    RT_ZERO(ParmPages);
    ParmPages.type              = VBOX_HGCM_SVC_PARM_PAGES; /* Retrieval buffer. */
    ParmPages.u.Pages.cb        = sizeof(abPage);
    ParmPages.u.Pages.cPages    = 1;
    ParmPages.u.Pages.papvPages = &apvPages[0];

    /* Suppress the expected strict guest assertion for the invalid type. */
    bool const fQuiet    = RTAssertSetQuiet(true);
    bool const fMayPanic = RTAssertSetMayPanic(false);
    int const rc = Message.GetData(42, 1, &ParmPages);
    RTAssertSetMayPanic(fMayPanic);
    RTAssertSetQuiet(fQuiet);
    RTTEST_CHECK_RC(hTest, rc, VERR_WRONG_PARAMETER_TYPE);
    RTTEST_CHECK(hTest, ParmPages.type == VBOX_HGCM_SVC_PARM_PAGES); /* Retrieval buffer. */
    RTTEST_CHECK(hTest, ParmPages.u.Pages.cb == sizeof(abPage));
    RTTEST_CHECK(hTest, ParmPages.u.Pages.cPages == 1);
    RTTEST_CHECK(hTest, ParmPages.u.Pages.papvPages == &apvPages[0]);
    RTTEST_CHECK(hTest, apvPages[0] == &abPage[0]);
    RTTEST_CHECK(hTest, memcmp(&abPage[0], &s_abPageData[0], sizeof(abPage)) == 0);

    RTTestSubDone(hTest);
}

/** Tests setting the result of a deferred message-information request. */
static void testClientSetDeferredMsgInfo(RTTEST hTest)
{
    RTTestSub(hTest, "HGCM deferred message information");

    HGCM::Client Client(1);
    VBOXHGCMSVCPARM aParms[2];
    HGCMSvcSetU32(&aParms[0], 0); /* uMsg */
    HGCMSvcSetU32(&aParms[1], 0); /* cParms */
    Client.SetDeferred((VBOXHGCMCALLHANDLE)(uintptr_t)1, 0, RT_ELEMENTS(aParms), aParms);
    RTTEST_CHECK_RC(hTest, Client.SetDeferredMsgInfo(42, 7), VINF_SUCCESS);
    RTTEST_CHECK(hTest, aParms[0].type == VBOX_HGCM_SVC_PARM_32BIT); /* uMsg */
    RTTEST_CHECK(hTest, aParms[0].u.uint32 == 42); /* uMsg */
    RTTEST_CHECK(hTest, aParms[1].type == VBOX_HGCM_SVC_PARM_32BIT); /* cParms */
    RTTEST_CHECK(hTest, aParms[1].u.uint32 == 7); /* cParms */

    void *apvPages[1] = { (void *)(uintptr_t)0x1234 };
    RT_ZERO(aParms[0]);
    aParms[0].type              = VBOX_HGCM_SVC_PARM_PAGES; /* uMsg */
    aParms[0].u.Pages.cb        = 4096;
    aParms[0].u.Pages.cPages    = 1;
    aParms[0].u.Pages.papvPages = &apvPages[0];
    HGCMSvcSetU32(&aParms[1], 0); /* cParms */

    bool const fQuiet     = RTAssertSetQuiet(true);
    bool const fMayPanic  = RTAssertSetMayPanic(false);
    int const rc = Client.SetDeferredMsgInfo(42, 7);
    RTAssertSetMayPanic(fMayPanic);
    RTAssertSetQuiet(fQuiet);
    RTTEST_CHECK_RC(hTest, rc, VERR_WRONG_PARAMETER_TYPE);
    RTTEST_CHECK(hTest, aParms[0].type == VBOX_HGCM_SVC_PARM_PAGES); /* uMsg */
    RTTEST_CHECK(hTest, aParms[0].u.Pages.cb == 4096);
    RTTEST_CHECK(hTest, aParms[0].u.Pages.cPages == 1);
    RTTEST_CHECK(hTest, aParms[0].u.Pages.papvPages == &apvPages[0]);

    RTTestSubDone(hTest);
}

int main()
{
    /*
     * Init the runtime, test and say hello.
     */
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstHGCMSvc", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    /*
     * Run the test.
     */
    VBOXHGCMSVCPARM parm;
    testGetString(&parm, hTest);
    testMessageCopyParms(hTest);
    testClientSetDeferredMsgInfo(hTest);

    /*
     * Summary
     */
    return RTTestSummaryAndDestroy(hTest);
}

