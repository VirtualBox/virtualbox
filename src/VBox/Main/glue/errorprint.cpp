/* $Id: errorprint.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */

/** @file
 * MS COM / XPCOM Abstraction Layer:
 * Error info print helpers. This implements the shared code from the macros from errorprint.h.
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


#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>
#include <VBox/log.h>

#include <ProgressImpl.h>

#include <iprt/stream.h>
#include <iprt/message.h>
#include <iprt/path.h>

namespace com
{

void GluePrintErrorInfo(const com::ErrorInfo &info)
{
    bool haveResultCode = false;
#if defined (RT_OS_WIN)
    haveResultCode = info.isFullAvailable();
    bool haveComponent = true;
    bool haveInterfaceID = true;
#else /* defined (RT_OS_WIN) */
    haveResultCode = true;
    bool haveComponent = info.isFullAvailable();
    bool haveInterfaceID = info.isFullAvailable();
#endif

    try
    {
        HRESULT hrc = S_OK;
        Utf8Str str;
        RTCList<Utf8Str> comp;

        Bstr bstrDetailsText = info.getText();
        if (!bstrDetailsText.isEmpty())
            str = Utf8StrFmt("%ls\n",
                             bstrDetailsText.raw());
        if (haveResultCode)
        {
            hrc = info.getResultCode();
            comp.append(Utf8StrFmt("code %Rhrc (0x%RX32)", hrc, hrc));
        }
        if (haveComponent)
            comp.append(Utf8StrFmt("component %ls",
                                   info.getComponent().raw()));
        if (haveInterfaceID)
            comp.append(Utf8StrFmt("interface %ls",
                                   info.getInterfaceName().raw()));
        if (!info.getCalleeName().isEmpty())
            comp.append(Utf8StrFmt("callee %ls",
                                   info.getCalleeName().raw()));

        if (comp.size() > 0)
        {
            str += "Details: ";
            for (size_t i = 0; i < comp.size() - 1; ++i)
                str += comp.at(i) + ", ";
            str += comp.last();
            str += "\n";
        }

        // print and log
        if (FAILED(hrc))
        {
            RTMsgError("%s", str.c_str());
            Log(("ERROR: %s", str.c_str()));
        }
        else
        {
            RTMsgWarning("%s", str.c_str());
            Log(("WARNING: %s", str.c_str()));
        }
    }
    catch (std::bad_alloc &)
    {
        RTMsgError("std::bad_alloc in GluePrintErrorInfo!");
        Log(("ERROR: std::bad_alloc in GluePrintErrorInfo!\n"));
    }
}

void GluePrintErrorContext(const char *pcszContext, const char *pcszSourceFile, uint32_t ulLine, bool fWarning /* = false */)
{
    // pcszSourceFile comes from __FILE__ macro, which always contains the full path,
    // which we don't want to see printed:
    // print and log
    const char *pszFilenameOnly = RTPathFilename(pcszSourceFile);
    if (!fWarning)
    {
        RTMsgError("Context: \"%s\" at line %d of file %s\n", pcszContext, ulLine, pszFilenameOnly);
        Log(("ERROR: Context: \"%s\" at line %d of file %s\n", pcszContext, ulLine, pszFilenameOnly));
    }
    else
    {
        RTMsgWarning("Context: \"%s\" at line %d of file %s\n", pcszContext, ulLine, pszFilenameOnly);
        Log(("WARNING: Context: \"%s\" at line %d of file %s\n", pcszContext, ulLine, pszFilenameOnly));
    }
}

void GluePrintRCMessage(HRESULT hrc)
{
    // print and log
    if (FAILED(hrc))
    {
        RTMsgError("Code %Rhra (extended info not available)\n", hrc);
        Log(("ERROR: Code %Rhra (extended info not available)\n", hrc));
    }
    else
    {
        RTMsgWarning("Code %Rhra (extended info not available)\n", hrc);
        Log(("WARNING: Code %Rhra (extended info not available)\n", hrc));
    }
}

static void glueHandleComErrorInternal(com::ErrorInfo &info,
                                       const char *pcszContext,
                                       HRESULT hrc,
                                       const char *pcszSourceFile,
                                       uint32_t ulLine)
{
    if (info.isFullAvailable() || info.isBasicAvailable())
    {
        const com::ErrorInfo *pInfo = &info;
        do
        {
            GluePrintErrorInfo(*pInfo);

            /* Use hrc for figuring out if there were just warnings. */
            HRESULT hrc2 = pInfo->getResultCode();
            if (   (SUCCEEDED_WARNING(hrc) && FAILED(hrc2))
                || (SUCCEEDED(hrc) && (FAILED(hrc2) || SUCCEEDED_WARNING(hrc2))))
                hrc = hrc2;

            pInfo = pInfo->getNext();
            /* If there is more than one error, separate them visually. */
            if (pInfo)
            {
                /* If there are several errors then at least basic error
                 * information must be available, otherwise something went
                 * horribly wrong. */
                Assert(pInfo->isFullAvailable() || pInfo->isBasicAvailable());

                RTMsgError("--------\n");
            }
        } while (pInfo);
    }
    else
        GluePrintRCMessage(hrc);

    if (pcszContext != NULL || pcszSourceFile != NULL)
        GluePrintErrorContext(pcszContext, pcszSourceFile, ulLine, SUCCEEDED_WARNING(hrc));
}

void GlueHandleComError(ComPtr<IUnknown> iface,
                        const char *pcszContext,
                        HRESULT hrc,
                        const char *pcszSourceFile,
                        uint32_t ulLine)
{
    /* If we have full error info, print something nice, and start with the
     * actual error message. */
    com::ErrorInfo info(iface, COM_IIDOF(IUnknown));

    glueHandleComErrorInternal(info,
                               pcszContext,
                               hrc,
                               pcszSourceFile,
                               ulLine);

}

void GlueHandleComErrorNoCtx(ComPtr<IUnknown> iface, HRESULT hrc)
{
    GlueHandleComError(iface, NULL, hrc, NULL, 0);
}

void GlueHandleComErrorProgress(ComPtr<IProgress> progress,
                                const char *pcszContext,
                                HRESULT hrc,
                                const char *pcszSourceFile,
                                uint32_t ulLine)
{
    /* Get the error info out of the progress object. */
    ProgressErrorInfo ei(progress);

    glueHandleComErrorInternal(ei,
                               pcszContext,
                               hrc,
                               pcszSourceFile,
                               ulLine);
}

} /* namespace com */

