/* $Id: tstHGCMSvc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * HGCM Service Testcase.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

    /*
     * Summary
     */
    return RTTestSummaryAndDestroy(hTest);
}

