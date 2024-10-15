/* $Id: UIErrorString.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIErrorString class implementation.
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

/* Qt includes: */
#include <QApplication>
#include <QObject>
#include <QPalette>

/* GUI includes: */
#include "UIErrorString.h"
#include "UITranslator.h"

/* COM includes: */
#include "COMDefs.h"
#include "CProgress.h"
#include "CVirtualBoxErrorInfo.h"


/* static */
QString UIErrorString::formatRC(HRESULT rc)
{
    /** @todo r=bird: Not sure why we set the sign bit 31 bit for warnings.
     *  Maybe to try get the error variant?  It won't really work for S_FALSE and
     *  probably a bunch of others too.  I've modified it on windows to try get
     *  the exact one, the one with the top bit set, or just the value. */
#ifdef RT_OS_WINDOWS
    char szDefine[80];
    if (   !SUCCEEDED_WARNING(rc)
        || (   RTErrWinQueryDefine(rc, szDefine, sizeof(szDefine), true /*fFailIfUnknown*/) == VERR_NOT_FOUND
            && RTErrWinQueryDefine(rc | 0x80000000, szDefine, sizeof(szDefine), true /*fFailIfUnknown*/) == VERR_NOT_FOUND))
        RTErrWinQueryDefine(rc, szDefine, sizeof(szDefine), false /*fFailIfUnknown*/);

    return QString::asprintf("%s", szDefine);
#else
    const char *pszDefine = RTErrCOMGet(SUCCEEDED_WARNING(rc) ? rc | 0x80000000 : rc)->pszDefine;
    Assert(pszDefine);

    return QString(pszDefine);
#endif
}

/* static */
QString UIErrorString::formatRCFull(HRESULT rc)
{
    /** @todo r=bird: See UIErrorString::formatRC for 31th bit discussion. */
    char szHex[32];
    RTStrPrintf(szHex, sizeof(szHex), "%#010x", rc);

#ifdef RT_OS_WINDOWS
    char szDefine[80];
    ssize_t cchRet = RTErrWinQueryDefine(rc, szDefine, sizeof(szDefine), true /*fFailIfUnknown*/);
    if (cchRet == VERR_NOT_FOUND && SUCCEEDED_WARNING(rc))
        cchRet = RTErrWinQueryDefine(rc | 0x80000000, szDefine, sizeof(szDefine), true /*fFailIfUnknown*/);

    if (cchRet != VERR_NOT_FOUND)
        return QString(szDefine).append(" (").append(szHex).append(")");
#else
    const char *pszDefine = RTErrCOMGet(SUCCEEDED_WARNING(rc) ? rc | 0x80000000 : rc)->pszDefine;
    Assert(pszDefine);

    if (strncmp(pszDefine, RT_STR_TUPLE("Unknown ")))
        return QString(pszDefine).append(" (").append(szHex).append(")");
#endif
    return QString(szHex);
}

/* static */
QString UIErrorString::formatErrorInfo(const CProgress &comProgress)
{
    /* Check for API errors first: */
    if (!comProgress.isOk())
        return formatErrorInfo(static_cast<COMBaseWithEI>(comProgress));

    /* For progress errors otherwise: */
    CVirtualBoxErrorInfo comErrorInfo = comProgress.GetErrorInfo();
    /* Handle valid error-info first: */
    if (!comErrorInfo.isNull())
        return formatErrorInfo(comErrorInfo);
    /* Handle NULL error-info otherwise: */
    return QString("<table bgcolor=%1 border=0 cellspacing=5 cellpadding=0 width=100%>"
                   "<tr><td>%2</td><td><tt>%3</tt></td></tr></table>")
                   .arg(QApplication::palette().color(QPalette::Active, QPalette::Window).name(QColor::HexRgb))
                   .arg(QApplication::translate("UIErrorString", "Result&nbsp;Code:", "error info"))
                   .arg(formatRCFull(comProgress.GetResultCode()))
                   .prepend("<!--EOM-->") /* move to details */;
}

/* static */
QString UIErrorString::formatErrorInfo(const COMErrorInfo &comInfo, HRESULT wrapperRC /* = S_OK */)
{
    return QString("<qt>%1</qt>").arg(UIErrorString::errorInfoToString(comInfo, wrapperRC));
}

/* static */
QString UIErrorString::formatErrorInfo(const CVirtualBoxErrorInfo &comInfo)
{
    return formatErrorInfo(COMErrorInfo(comInfo));
}

/* static */
QString UIErrorString::formatErrorInfo(const COMBaseWithEI &comWrapper)
{
    Assert(comWrapper.lastRC() != S_OK);
    return formatErrorInfo(comWrapper.errorInfo(), comWrapper.lastRC());
}

/* static */
QString UIErrorString::formatErrorInfo(const COMResult &comRc)
{
    Assert(comRc.rc() != S_OK);
    return formatErrorInfo(comRc.errorInfo(), comRc.rc());
}

/* static */
QString UIErrorString::simplifiedErrorInfo(const COMErrorInfo &comInfo, HRESULT wrapperRC /* = S_OK */)
{
    return UIErrorString::errorInfoToSimpleString(comInfo, wrapperRC);
}

/* static */
QString UIErrorString::simplifiedErrorInfo(const COMBaseWithEI &comWrapper)
{
    Assert(comWrapper.lastRC() != S_OK);
    return simplifiedErrorInfo(comWrapper.errorInfo(), comWrapper.lastRC());
}

/* static */
QString UIErrorString::errorInfoToString(const COMErrorInfo &comInfo, HRESULT wrapperRC)
{
    /* Compose complex details string with internal <!--EOM--> delimiter to
     * make it possible to split string into info & details parts which will
     * be used separately in QIMessageBox. */
    QString strFormatted;

    /* Check if details text is NOT empty: */
    const QString strDetailsInfo = comInfo.text();
    if (!strDetailsInfo.isEmpty())
    {
        /* Check if details text written in English (latin1) and translated: */
        if (   strDetailsInfo == QString::fromLatin1(strDetailsInfo.toLatin1())
            && strDetailsInfo != QObject::tr(strDetailsInfo.toLatin1().constData()))
            strFormatted += QString("<p>%1.</p>").arg(UITranslator::emphasize(QObject::tr(strDetailsInfo.toLatin1().constData())));
        else
            strFormatted += QString("<p>%1.</p>").arg(UITranslator::emphasize(strDetailsInfo));
    }

    strFormatted += QString("<!--EOM--><table bgcolor=%1 border=0 cellspacing=5 cellpadding=0 width=100%>")
                            .arg(QApplication::palette().color(QPalette::Active, QPalette::Window).name(QColor::HexRgb));

    bool fHaveResultCode = false;

    if (comInfo.isBasicAvailable())
    {
#ifdef VBOX_WS_WIN
        fHaveResultCode = comInfo.isFullAvailable();
        bool fHaveComponent = true;
        bool fHaveInterfaceID = true;
#else /* !VBOX_WS_WIN */
        fHaveResultCode = true;
        bool fHaveComponent = comInfo.isFullAvailable();
        bool fHaveInterfaceID = comInfo.isFullAvailable();
#endif

        if (fHaveResultCode)
        {
            strFormatted += QString("<tr><td>%1</td><td><tt>%2</tt></td></tr>")
                .arg(QApplication::translate("UIErrorString", "Result&nbsp;Code:", "error info"))
                .arg(formatRCFull(comInfo.resultCode()));
        }

        if (comInfo.resultDetail() != 0)
            strFormatted += QString("<tr><td>%1</td><td><tt>%2 (0x%3)</tt></td></tr>")
                .arg(QApplication::translate("UIErrorString", "Result&nbsp;Detail:", "error info"))
                .arg(QString::number(int(comInfo.resultDetail()))) /* arg(int()) ends up as unsigned, thus the ::number crap. */
                .arg(comInfo.resultDetail(), 0, 16);

        if (fHaveComponent)
            strFormatted += QString("<tr><td>%1</td><td>%2</td></tr>")
                .arg(QApplication::translate("UIErrorString", "Component:", "error info"), comInfo.component());

        if (fHaveInterfaceID)
        {
            QString s = comInfo.interfaceID().toString();
            if (!comInfo.interfaceName().isEmpty())
                s = comInfo.interfaceName() + ' ' + s;
            strFormatted += QString("<tr><td>%1</td><td>%2</td></tr>")
                .arg(QApplication::translate("UIErrorString", "Interface:", "error info"), s);
        }

        if (!comInfo.calleeIID().isNull() && comInfo.calleeIID() != comInfo.interfaceID())
        {
            QString s = comInfo.calleeIID().toString();
            if (!comInfo.calleeName().isEmpty())
                s = comInfo.calleeName() + ' ' + s;
            strFormatted += QString("<tr><td>%1</td><td>%2</td></tr>")
                .arg(QApplication::translate("UIErrorString", "Callee:", "error info"), s);
        }
    }

    if (   FAILED(wrapperRC)
        && (!fHaveResultCode || wrapperRC != comInfo.resultCode()))
    {
        strFormatted += QString("<tr><td>%1</td><td><tt>%2</tt></td></tr>")
            .arg(QApplication::translate("UIErrorString", "Callee&nbsp;RC:", "error info"))
            .arg(formatRCFull(wrapperRC));
    }

    strFormatted += "</table>";

    if (comInfo.next())
        strFormatted = strFormatted + "<!--EOP-->" + errorInfoToString(*comInfo.next());

    return strFormatted;
}

/* static */
QString UIErrorString::errorInfoToSimpleString(const COMErrorInfo &comInfo, HRESULT wrapperRC /* = S_OK */)
{
    /* Compose complex details string with text and status code: */
    QString strFormatted;

    /* Check if details text is NOT empty: */
    const QString strDetailsInfo = comInfo.text();
    if (!strDetailsInfo.isEmpty())
        strFormatted += strDetailsInfo;

    /* Check if we have result code: */
    bool fHaveResultCode = false;

    if (comInfo.isBasicAvailable())
    {
#ifdef VBOX_WS_WIN
        fHaveResultCode = comInfo.isFullAvailable();
#else
        fHaveResultCode = true;
#endif

        if (fHaveResultCode)
            strFormatted += "; " + QString("Result Code: ") + formatRCFull(comInfo.resultCode());
    }

    if (   FAILED(wrapperRC)
        && (!fHaveResultCode || wrapperRC != comInfo.resultCode()))
        strFormatted += "; " + QString("Callee RC: ") + formatRCFull(wrapperRC);

    /* Check if we have next error queued: */
    if (comInfo.next())
        strFormatted += "; " + errorInfoToSimpleString(*comInfo.next());

    return strFormatted;
}
