/* $Id: UISettingsSerializer.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UISettingsSerializer class implementation.
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
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <QVBoxLayout>

/* GUI includes: */
#include "QILabel.h"
#include "UIIconPool.h"
#include "UIMessageCenter.h"
#include "UISettingsPage.h"
#include "UISettingsSerializer.h"
#include "UITranslationEventListener.h"


/*********************************************************************************************************************************
*   Class UISettingsSerializer implementation.                                                                                   *
*********************************************************************************************************************************/

UISettingsSerializer::UISettingsSerializer(QObject *pParent, SerializationDirection enmDirection,
                                           const QVariant &data, const UISettingsPageList &pages)
    : QThread(pParent)
    , m_enmDirection(enmDirection)
    , m_data(data)
    , m_fSavingComplete(m_enmDirection == Load)
    , m_iIdOfHighPriorityPage(-1)
{
    /* Copy the page(s) from incoming list to our map: */
    foreach (UISettingsPage *pPage, pages)
        m_pages.insert(pPage->id(), pPage);

    /* Handling internal signals, they are also redirected in their handlers: */
    connect(this, &UISettingsSerializer::sigNotifyAboutPageProcessed, this, &UISettingsSerializer::sltHandleProcessedPage, Qt::QueuedConnection);
    connect(this, &UISettingsSerializer::sigNotifyAboutPagesProcessed, this, &UISettingsSerializer::sltHandleProcessedPages, Qt::QueuedConnection);

    /* Redirecting unhandled internal signals: */
    connect(this, &UISettingsSerializer::finished, this, &UISettingsSerializer::sigNotifyAboutProcessFinished, Qt::QueuedConnection);
}

UISettingsSerializer::~UISettingsSerializer()
{
    /* If serializer is being destructed by it's parent,
     * thread could still be running, we have to wait
     * for it to be finished! */
    if (isRunning())
        wait();
}

void UISettingsSerializer::raisePriorityOfPage(int iPageId)
{
    /* If that page is present and was not processed already =>
     * we should remember which page should be processed next: */
    if (m_pages.contains(iPageId) && !(m_pages[iPageId]->processed()))
        m_iIdOfHighPriorityPage = iPageId;
}

void UISettingsSerializer::start(Priority priority /* = InheritPriority */)
{
    /* Notify listeners about we are starting: */
    emit sigNotifyAboutProcessStarted();

    /* If serializer saves settings: */
    if (m_enmDirection == Save)
    {
        /* We should update internal page cache first: */
        foreach (UISettingsPage *pPage, m_pages.values())
            pPage->putToCache();
    }

    /* Start async serializing thread: */
    QThread::start(priority);
}

void UISettingsSerializer::sltHandleProcessedPage(int iPageId)
{
    /* Make sure such page present: */
    AssertReturnVoid(m_pages.contains(iPageId));

    /* Get the page being processed: */
    UISettingsPage *pSettingsPage = m_pages.value(iPageId);

    /* If serializer loads settings: */
    if (m_enmDirection == Load)
    {
        /* We should fetch internal page cache: */
        pSettingsPage->setValidatorBlocked(true);
        pSettingsPage->getFromCache();
        pSettingsPage->setValidatorBlocked(false);
    }

    /* Add processed page into corresponding map: */
    m_pagesDone.insert(iPageId, pSettingsPage);

    /* Notify listeners about process reached n%: */
    const int iValue = 100 * m_pagesDone.size() / m_pages.size();
    emit sigNotifyAboutProcessProgressChanged(iValue);
}

void UISettingsSerializer::sltHandleProcessedPages()
{
    /* If serializer saves settings: */
    if (m_enmDirection == Save)
    {
        /* We should flag GUI thread to unlock itself: */
        if (!m_fSavingComplete)
            m_fSavingComplete = true;
    }
    /* If serializer loads settings: */
    else
    {
        /* We have to do initial validation finally: */
        foreach (UISettingsPage *pPage, m_pages.values())
            pPage->revalidate();
    }

    /* Notify listeners about process reached 100%: */
    emit sigNotifyAboutProcessProgressChanged(100);
}

void UISettingsSerializer::run()
{
    /* Initialize COM for other thread: */
    COMBase::InitializeCOM(false);

    /* Mark all the pages initially as NOT processed: */
    foreach (UISettingsPage *pPage, m_pages.values())
        pPage->setProcessed(false);

    /* Iterate over the all left settings pages: */
    UISettingsPageMap pages(m_pages);
    while (!pages.empty())
    {
        /* Get required page pointer, protect map by mutex while getting pointer: */
        UISettingsPage *pPage = m_iIdOfHighPriorityPage != -1 && pages.contains(m_iIdOfHighPriorityPage) ?
                                pages.value(m_iIdOfHighPriorityPage) : *pages.begin();
        /* Reset request of high priority: */
        if (m_iIdOfHighPriorityPage != -1)
            m_iIdOfHighPriorityPage = -1;
        /* Process this page if its enabled: */
        connect(pPage, &UISettingsPage::sigOperationProgressChange,
                this, &UISettingsSerializer::sigOperationProgressChange);
        connect(pPage, &UISettingsPage::sigOperationProgressError,
                this, &UISettingsSerializer::sigOperationProgressError);
        if (pPage->isEnabled())
        {
            if (m_enmDirection == Load)
                pPage->loadToCacheFrom(m_data);
            if (m_enmDirection == Save)
                pPage->saveFromCacheTo(m_data);
        }
        /* Remember what page was processed: */
        disconnect(pPage, &UISettingsPage::sigOperationProgressChange,
                   this, &UISettingsSerializer::sigOperationProgressChange);
        disconnect(pPage, &UISettingsPage::sigOperationProgressError,
                   this, &UISettingsSerializer::sigOperationProgressError);
        pPage->setProcessed(true);
        /* Remove processed page from our map: */
        pages.remove(pPage->id());
        /* Notify listeners about page was processed: */
        emit sigNotifyAboutPageProcessed(pPage->id());
        /* If serializer saves settings => wake up GUI thread: */
        if (m_enmDirection == Save)
            m_condition.wakeAll();
        /* Break further processing if page had failed: */
        if (pPage->failed())
            break;
    }
    /* Notify listeners about all pages were processed: */
    emit sigNotifyAboutPagesProcessed();
    /* If serializer saves settings => wake up GUI thread: */
    if (m_enmDirection == Save)
        m_condition.wakeAll();

    /* Deinitialize COM for other thread: */
    COMBase::CleanupCOM();
}


/*********************************************************************************************************************************
*   Class UISettingsSerializerProgress implementation.                                                                           *
*********************************************************************************************************************************/

QString UISettingsSerializerProgress::s_strProgressDescriptionTemplate = QString("<compact elipsis=\"middle\">%1 (%2/%3)</compact>");

UISettingsSerializerProgress::UISettingsSerializerProgress(QWidget *pParent,
                                                           UISettingsSerializer::SerializationDirection enmDirection,
                                                           const QVariant &data,
                                                           const UISettingsPageList &pages)
    : QIDialog(pParent)
    , m_enmDirection(enmDirection)
    , m_data(data)
    , m_pages(pages)
    , m_pSerializer(0)
    , m_pLabelOperationProgress(0)
    , m_pBarOperationProgress(0)
    , m_pLabelSubOperationProgress(0)
    , m_pBarSubOperationProgress(0)
    , m_fClean(true)
{
    /* Prepare: */
    prepare();
    /* Translate: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UISettingsSerializerProgress::sltRetranslateUI);
}

int UISettingsSerializerProgress::exec()
{
    /* Ask for process start: */
    emit sigAskForProcessStart();

    /* Call to base-class: */
    return QIDialog::exec();
}

QVariant &UISettingsSerializerProgress::data()
{
    AssertPtrReturn(m_pSerializer, m_data);
    return m_pSerializer->data();
}

void UISettingsSerializerProgress::prepare()
{
    /* Configure self: */
    setWindowModality(Qt::WindowModal);
    setWindowTitle(parentWidget()->windowTitle());
    connect(this, &UISettingsSerializerProgress::sigAskForProcessStart,
            this, &UISettingsSerializerProgress::sltStartProcess, Qt::QueuedConnection);

    /* Create serializer: */
    m_pSerializer = new UISettingsSerializer(this, m_enmDirection, m_data, m_pages);
    AssertPtrReturnVoid(m_pSerializer);
    {
        /* Install progress handler: */
        connect(m_pSerializer, &UISettingsSerializer::sigNotifyAboutProcessProgressChanged,
                this, &UISettingsSerializerProgress::sltHandleProcessProgressChange);
        connect(m_pSerializer, &UISettingsSerializer::sigOperationProgressChange,
                this, &UISettingsSerializerProgress::sltHandleOperationProgressChange);
        connect(m_pSerializer, &UISettingsSerializer::sigOperationProgressError,
                this, &UISettingsSerializerProgress::sltHandleOperationProgressError);
    }

    /* Create layout: */
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    AssertPtrReturnVoid(pLayout);
    {
        /* Create top layout: */
        QHBoxLayout *pLayoutTop = new QHBoxLayout;
        AssertPtrReturnVoid(pLayoutTop);
        {
            /* Create pixmap layout: */
            QVBoxLayout *pLayoutPixmap = new QVBoxLayout;
            AssertPtrReturnVoid(pLayoutPixmap);
            {
                /* Create pixmap label: */
                QLabel *pLabelPixmap = new QLabel;
                AssertPtrReturnVoid(pLabelPixmap);
                {
                    /* Configure label: */
                    const QIcon icon = UIIconPool::iconSet(":/progress_settings_90px.png");
                    pLabelPixmap->setPixmap(icon.pixmap(icon.availableSizes().value(0, QSize(90, 90))));
                    /* Add label into layout: */
                    pLayoutPixmap->addWidget(pLabelPixmap);
                }
                /* Add stretch: */
                pLayoutPixmap->addStretch();
                /* Add layout into parent: */
                pLayoutTop->addLayout(pLayoutPixmap);
            }
            /* Create progress layout: */
            QVBoxLayout *pLayoutProgress = new QVBoxLayout;
            AssertPtrReturnVoid(pLayoutProgress);
            {
                /* Create operation progress label: */
                m_pLabelOperationProgress = new QLabel;
                AssertPtrReturnVoid(m_pLabelOperationProgress);
                {
                    /* Add label into layout: */
                    pLayoutProgress->addWidget(m_pLabelOperationProgress);
                }
                /* Create operation progress bar: */
                m_pBarOperationProgress = new QProgressBar;
                AssertPtrReturnVoid(m_pBarOperationProgress);
                {
                    /* Configure progress bar: */
                    m_pBarOperationProgress->setMinimumWidth(300);
                    m_pBarOperationProgress->setMaximum(100);
                    m_pBarOperationProgress->setMinimum(0);
                    m_pBarOperationProgress->setValue(0);
                    /* Add bar into layout: */
                    pLayoutProgress->addWidget(m_pBarOperationProgress);
                }
                /* Create sub-operation progress label: */
                m_pLabelSubOperationProgress = new QILabel;
                AssertPtrReturnVoid(m_pLabelSubOperationProgress);
                {
                    /* Configure label: */
                    m_pLabelSubOperationProgress->hide();
                    m_pLabelSubOperationProgress->setSizePolicy(QSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed));
                    /* Add label into layout: */
                    pLayoutProgress->addWidget(m_pLabelSubOperationProgress);
                }
                /* Create sub-operation progress bar: */
                m_pBarSubOperationProgress = new QProgressBar;
                AssertPtrReturnVoid(m_pBarSubOperationProgress);
                {
                    /* Configure progress bar: */
                    m_pBarSubOperationProgress->hide();
                    m_pBarSubOperationProgress->setMinimumWidth(300);
                    m_pBarSubOperationProgress->setMaximum(100);
                    m_pBarSubOperationProgress->setMinimum(0);
                    m_pBarSubOperationProgress->setValue(0);
                    /* Add bar into layout: */
                    pLayoutProgress->addWidget(m_pBarSubOperationProgress);
                }
                /* Add stretch: */
                pLayoutProgress->addStretch();
                /* Add layout into parent: */
                pLayoutTop->addLayout(pLayoutProgress);
            }
            /* Add layout into parent: */
            pLayout->addLayout(pLayoutTop);
        }
    }
}

void UISettingsSerializerProgress::sltRetranslateUI()
{
    /* Translate operation progress label: */
    AssertPtrReturnVoid(m_pLabelOperationProgress);
    switch (m_pSerializer->direction())
    {
        case UISettingsSerializer::Load: m_pLabelOperationProgress->setText(tr("Loading Settings...")); break;
        case UISettingsSerializer::Save: m_pLabelOperationProgress->setText(tr("Saving Settings...")); break;
    }
}

void UISettingsSerializerProgress::closeEvent(QCloseEvent *pEvent)
{
    /* No need to close the dialog: */
    pEvent->ignore();
}

void UISettingsSerializerProgress::reject()
{
    /* No need to reject the dialog. */
}

void UISettingsSerializerProgress::sltStartProcess()
{
    /* Start the serializer: */
    m_pSerializer->start();
}

void UISettingsSerializerProgress::sltHandleProcessProgressChange(int iValue)
{
    /* Update the operation progress-bar with incoming value: */
    AssertPtrReturnVoid(m_pBarOperationProgress);
    m_pBarOperationProgress->setValue(iValue);
    /* Hide the progress-dialog upon reaching the 100% progress: */
    if (iValue == m_pBarOperationProgress->maximum())
        hide();
}

void UISettingsSerializerProgress::sltHandleOperationProgressChange(ulong iOperations, QString strOperation,
                                                                    ulong iOperation, ulong iPercent)
{
    /* Update the sub-operation progress label and bar: */
    AssertPtrReturnVoid(m_pLabelSubOperationProgress);
    AssertPtrReturnVoid(m_pBarSubOperationProgress);
    m_pLabelSubOperationProgress->show();
    m_pBarSubOperationProgress->show();
    m_pLabelSubOperationProgress->setText(s_strProgressDescriptionTemplate.arg(strOperation).arg(iOperation).arg(iOperations));
    m_pBarSubOperationProgress->setValue(iPercent);
}

void UISettingsSerializerProgress::sltHandleOperationProgressError(QString strErrorInfo)
{
    /* Mark the serialization process dirty: */
    m_fClean = false;

    /* Show the error message: */
    msgCenter().cannotSaveSettings(strErrorInfo, this);
}
