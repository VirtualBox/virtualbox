/* $Id: UINetworkRequestManager.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UINetworkRequestManager stuff implementation.
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
#include <QUrl>

/* GUI includes: */
#include "UINetworkCustomer.h"
#include "UINetworkRequest.h"
#include "UINetworkRequestManager.h"

/* Other VBox includes: */
#include "iprt/assert.h"


/* static */
UINetworkRequestManager *UINetworkRequestManager::s_pInstance = 0;

/* static */
void UINetworkRequestManager::create()
{
    AssertReturnVoid(!s_pInstance);
    new UINetworkRequestManager;
}

/* static */
void UINetworkRequestManager::destroy()
{
    AssertPtrReturnVoid(s_pInstance);
    delete s_pInstance;
}

/* static */
UINetworkRequestManager *UINetworkRequestManager::instance()
{
    return s_pInstance;
}

QUuid UINetworkRequestManager::createNetworkRequest(UINetworkRequestType enmType,
                                                    const QList<QUrl> &urls,
                                                    const QString &strTarget,
                                                    const UserDictionary &requestHeaders,
                                                    UINetworkCustomer *pCustomer)
{
    /* Create network-request: */
    UINetworkRequest *pNetworkRequest = new UINetworkRequest(enmType, urls, strTarget, requestHeaders);
    if (pNetworkRequest)
    {
        /* Configure request listeners: */
        connect(pNetworkRequest, &UINetworkRequest::sigProgress,
                this, &UINetworkRequestManager::sltHandleNetworkRequestProgress);
        connect(pNetworkRequest, &UINetworkRequest::sigCanceled,
                this, &UINetworkRequestManager::sltHandleNetworkRequestCancel);
        connect(pNetworkRequest, &UINetworkRequest::sigFinished,
                this, &UINetworkRequestManager::sltHandleNetworkRequestFinish);
        connect(pNetworkRequest, &UINetworkRequest::sigFailed,
                this, &UINetworkRequestManager::sltHandleNetworkRequestFailure);

        /* [Re]generate ID until unique: */
        QUuid uId = QUuid::createUuid();
        while (m_requests.contains(uId))
            uId = QUuid::createUuid();

        /* Add request&customer to map: */
        m_requests.insert(uId, pNetworkRequest);
        m_customers.insert(uId, pCustomer);
        connect(pCustomer, &UINetworkCustomer::sigBeingDestroyed,
                this, &UINetworkRequestManager::sltHandleNetworkCustomerBeingDestroyed,
                Qt::UniqueConnection);

        /* Return ID: */
        return uId;
    }

    /* Null ID by default: */
    return QUuid();
}

void UINetworkRequestManager::cancelNetworkRequest(const QUuid &uId)
{
    /* Cleanup request: */
    UINetworkRequest *pNetworkRequest = m_requests.value(uId);
    if (pNetworkRequest)
        pNetworkRequest->sltCancel();
}

UINetworkRequestManager::UINetworkRequestManager()
{
    s_pInstance = this;
    prepare();
}

UINetworkRequestManager::~UINetworkRequestManager()
{
    cleanup();
    s_pInstance = 0;
}

void UINetworkRequestManager::sltHandleNetworkRequestProgress(qint64 iReceived, qint64 iTotal)
{
    /* Make sure we have this request registered: */
    UINetworkRequest *pNetworkRequest = qobject_cast<UINetworkRequest*>(sender());
    AssertPtrReturnVoid(pNetworkRequest);
    const QUuid uId = m_requests.key(pNetworkRequest);
    AssertReturnVoid(!uId.isNull());

    /* Delegate request to customer: */
    UINetworkCustomer *pNetworkCustomer = m_customers.value(uId);
    if (pNetworkCustomer)
        pNetworkCustomer->processNetworkReplyProgress(iReceived, iTotal);
}

void UINetworkRequestManager::sltHandleNetworkRequestFailure(const QString &strError)
{
    /* Make sure we have this request registered: */
    UINetworkRequest *pNetworkRequest = qobject_cast<UINetworkRequest*>(sender());
    AssertPtrReturnVoid(pNetworkRequest);
    const QUuid uId = m_requests.key(pNetworkRequest);
    AssertReturnVoid(!uId.isNull());

    /* Delegate request to customer: */
    UINetworkCustomer *pNetworkCustomer = m_customers.value(uId);
    if (pNetworkCustomer)
        pNetworkCustomer->processNetworkReplyFailed(strError);

    /* Cleanup request: */
    cleanupNetworkRequest(uId);
}

void UINetworkRequestManager::sltHandleNetworkRequestCancel()
{
    /* Make sure we have this request registered: */
    UINetworkRequest *pNetworkRequest = qobject_cast<UINetworkRequest*>(sender());
    AssertPtrReturnVoid(pNetworkRequest);
    const QUuid uId = m_requests.key(pNetworkRequest);
    AssertReturnVoid(!uId.isNull());

    /* Delegate request to customer: */
    UINetworkCustomer *pNetworkCustomer = m_customers.value(uId);
    if (pNetworkCustomer)
        pNetworkCustomer->processNetworkReplyCanceled(pNetworkRequest->reply());

    /* Cleanup request: */
    cleanupNetworkRequest(uId);
}

void UINetworkRequestManager::sltHandleNetworkRequestFinish()
{
    /* Make sure we have this request registered: */
    UINetworkRequest *pNetworkRequest = qobject_cast<UINetworkRequest*>(sender());
    AssertPtrReturnVoid(pNetworkRequest);
    const QUuid uId = m_requests.key(pNetworkRequest);
    AssertReturnVoid(!uId.isNull());

    /* Delegate request to customer: */
    UINetworkCustomer *pNetworkCustomer = m_customers.value(uId);
    if (pNetworkCustomer)
        pNetworkCustomer->processNetworkReplyFinished(pNetworkRequest->reply());

    /* Cleanup request: */
    cleanupNetworkRequest(uId);
}

void UINetworkRequestManager::sltHandleNetworkCustomerBeingDestroyed(UINetworkCustomer *pNetworkCustomer)
{
    /* Make sure customer was and still registered: */
    const QList<QUuid> ids = m_customers.keys(pNetworkCustomer);
    AssertReturnVoid(!ids.isEmpty());
    /* Unregister it: */
    foreach (const QUuid &uId, ids)
        m_customers.remove(uId);
}

void UINetworkRequestManager::prepare()
{
    // nothing for now
}

void UINetworkRequestManager::cleanupNetworkRequest(const QUuid &uId)
{
    delete m_requests.value(uId);
    m_requests.remove(uId);
}

void UINetworkRequestManager::cleanupNetworkRequests()
{
    foreach (const QUuid &uId, m_requests.keys())
        cleanupNetworkRequest(uId);
}

void UINetworkRequestManager::cleanup()
{
    cleanupNetworkRequests();
}
