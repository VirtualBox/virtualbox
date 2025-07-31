/* $Id: tstRTXml.cpp 110190 2025-07-10 11:50:58Z andreas.loeffler@oracle.com $ */
/** @file
 * IPRT Testcase - XML reading / writing.
 */

/*
 * Copyright (C) 2025 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#include <iprt/errcore.h>
#include <iprt/initterm.h>
#include <iprt/path.h>
#include <iprt/test.h>
#include <iprt/cpp/xml.h>

void testReadWriteSimple(void)
{
    static struct TestReadWriteAttrSimple
    {
        const char *szName;
        const char *szValWrite;
        const char *szValRead;
    } aValues[] =
    {
        { "val0", "bar"   , "bar"    },
        //{ "val1", "Ã"     , "Ã"      },
        //{ "val1", "fÃƒÆ" , "f&#xC3;&#xFFFD;" },
        { "val2", "&#xC3;", "&amp;#xC3;" }
    };
    char szFileDst[RTPATH_MAX] = { 0 };
    RTTESTI_CHECK_RC_OK(RTPathTemp(szFileDst, sizeof(szFileDst)));
    RTTESTI_CHECK_RC_OK(RTPathAppend(szFileDst, sizeof(szFileDst), "tstRTXml-Write.xml"));

    {
        try
        {
            xml::Document Doc;
            xml::XmlFileWriter Writer(Doc);

            xml::ElementNode *pElRoot = Doc.createRootElement("RTTstXml");
            xml::ElementNode *pElNode;

            for (size_t i = 0; i < RT_ELEMENTS(aValues); i++)
            {
                 pElNode = pElRoot->createChild("test"); pElNode->setAttribute(aValues[i].szName, aValues[i].szValWrite);
            }

            Writer.write(szFileDst, false /*fSafe*/);
        }
        catch (const std::exception &err)
        {
            RTTestIFailed("Writing file failed: %s\n", err.what());
        }
    }

    {
        try
        {
            xml::Document Doc;
            xml::XmlFileParser Parser;
            Parser.read(szFileDst, Doc);
        }
        catch (const std::exception &err)
        {
            RTTestIFailed("Parsing file failed: %s\n", err.what());
        }
    }
}

int main(void)
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTXml", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    testReadWriteSimple();

    return RTTestSummaryAndDestroy(hTest);
}
