/* $Id: UINetworkRequest.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UINetworkRequest class implementation.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#include <QVariant>

/* GUI includes: */
#include "UINetworkRequest.h"
#include "UINetworkRequestManager.h"

/* Other VBox includes: */
#include "iprt/assert.h"


UINetworkRequest::UINetworkRequest(UINetworkRequestType enmType,
                                   const QList<QUrl> &urls,
                                   const QString &strTarget,
                                   const UserDictionary &requestHeaders)
    : m_enmType(enmType)
    , m_urls(urls)
    , m_strTarget(strTarget)
    , m_requestHeaders(requestHeaders)
    , m_iUrlIndex(-1)
    , m_fRunning(false)
{
    prepare();
}

UINetworkRequest::~UINetworkRequest()
{
    cleanup();
}

void UINetworkRequest::sltHandleNetworkReplyProgress(qint64 iReceived, qint64 iTotal)
{
    /* Notify network-request listeners: */
    emit sigProgress(iReceived, iTotal);
}

void UINetworkRequest::sltHandleNetworkReplyFinish()
{
    /* Mark network-reply as non-running: */
    m_fRunning = false;

    /* Make sure network-reply still valid: */
    if (!m_pReply)
        return;

    /* If network-reply has no errors: */
    if (m_pReply->error() == UINetworkReply::NoError)
    {
        /* Notify network-request listeners: */
        emit sigFinished();
    }
    /* If network-request was canceled: */
    else if (m_pReply->error() == UINetworkReply::OperationCanceledError)
    {
        /* Notify network-request listeners: */
        emit sigCanceled();
    }
    /* If some other error occured: */
    else
    {
        /* Check if we are able to handle error: */
        bool fErrorHandled = false;

        /* Handle redirection: */
        switch (m_pReply->error())
        {
            case UINetworkReply::ContentReSendError:
            {
                /* Check whether redirection link was acquired: */
                const QString strRedirect = m_pReply->header(UINetworkReply::LocationHeader).toString();
                if (!strRedirect.isEmpty())
                {
                    /* Cleanup current network-reply first: */
                    cleanupNetworkReply();

                    /* Choose redirect-source as current url: */
                    m_url = strRedirect;

                    /* Create new network-reply finally: */
                    prepareNetworkReply();

                    /* Mark this error handled: */
                    fErrorHandled = true;
                }
                break;
            }
            default:
                break;
        }

        /* If error still unhandled: */
        if (!fErrorHandled)
        {
            /* Check if we have other urls in queue: */
            if (m_iUrlIndex < m_urls.size() - 1)
            {
                /* Cleanup current network-reply first: */
                cleanupNetworkReply();

                /* Choose next url as current: */
                ++m_iUrlIndex;
                m_url = m_urls.at(m_iUrlIndex);

                /* Create new network-reply finally: */
                prepareNetworkReply();
            }
            else
            {
                /* Notify network-request listeners: */
                emit sigFailed(m_pReply->errorString());
            }
        }
    }
}

void UINetworkRequest::sltCancel()
{
    /* Abort network-reply if present: */
    if (m_pReply)
    {
        if (m_fRunning)
            m_pReply->abort();
        else
            emit sigCanceled();
    }
}

void UINetworkRequest::prepare()
{
    /* Choose first url as current: */
    m_iUrlIndex = 0;
    m_url = m_urls.at(m_iUrlIndex);

    /* Prepare network-reply: */
    prepareNetworkReply();
}

void UINetworkRequest::prepareNetworkReply()
{
    /* Create network-reply: */
    m_pReply = new UINetworkReply(m_enmType, m_url, m_strTarget, m_requestHeaders);
    AssertPtrReturnVoid(m_pReply.data());
    {
        /* Prepare network-reply: */
        connect(m_pReply.data(), &UINetworkReply::downloadProgress,
                this, &UINetworkRequest::sltHandleNetworkReplyProgress);
        connect(m_pReply.data(), &UINetworkReply::finished,
                this, &UINetworkRequest::sltHandleNetworkReplyFinish);

        /* Mark network-reply as running: */
        m_fRunning = true;

        /* Notify network-request listeners: */
        emit sigStarted();
    }
}

void UINetworkRequest::cleanupNetworkReply()
{
    /* Destroy network-reply: */
    AssertPtrReturnVoid(m_pReply.data());
    m_pReply->disconnect();
    m_pReply->deleteLater();
    m_pReply = 0;
}

void UINetworkRequest::cleanup()
{
    /* Cleanup network-reply: */
    cleanupNetworkReply();
}
