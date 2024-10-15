/* $Id: UIProgressDialog.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIProgressDialog class implementation.
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
#include <QCloseEvent>
#include <QEventLoop>
#include <QProgressBar>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

/* GUI includes: */
#include "QIDialogButtonBox.h"
#include "QILabel.h"
#include "UIErrorString.h"
#include "UIExtraDataManager.h"
#include "UIMainEventListener.h"
#include "UIModalWindowManager.h"
#include "UIProgressDialog.h"
#include "UIProgressEventHandler.h"
#include "UISpecialControls.h"
#include "UITranslator.h"
#include "UITranslationEventListener.h"
#ifdef VBOX_WS_MAC
# include "VBoxUtils-darwin.h"
#endif

/* COM includes: */
#include "CEventListener.h"
#include "CEventSource.h"
#include "CProgress.h"


const char *UIProgressDialog::m_spcszOpDescTpl = "%1 ... (%2/%3)";

UIProgressDialog::UIProgressDialog(CProgress &comProgress,
                                   const QString &strTitle,
                                   QPixmap *pImage /* = 0 */,
                                   int cMinDuration /* = 2000 */,
                                   QWidget *pParent /* = 0 */)
    : QIDialog(pParent, Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint)
    , m_comProgress(comProgress)
    , m_strTitle(strTitle)
    , m_pImage(pImage)
    , m_cMinDuration(cMinDuration)
    , m_fLegacyHandling(gEDataManager->legacyProgressHandlingRequested())
    , m_pLabelImage(0)
    , m_pLabelDescription(0)
    , m_pProgressBar(0)
    , m_pButtonCancel(0)
    , m_pLabelEta(0)
    , m_cOperations(m_comProgress.GetOperationCount())
    , m_uCurrentOperation(m_comProgress.GetOperation() + 1)
    , m_uCurrentOperationWeight(m_comProgress.GetOperationWeight())
    , m_fCancelEnabled(false)
    , m_fEnded(false)
    , m_pEventHandler(0)
{
    /* Prepare: */
    prepare();
}

UIProgressDialog::~UIProgressDialog()
{
    /* Cleanup: */
    cleanup();
}

void UIProgressDialog::sltRetranslateUI()
{
    m_pButtonCancel->setText(tr("&Cancel"));
    m_pButtonCancel->setToolTip(tr("Cancel the current operation"));
}

int UIProgressDialog::run(int cRefreshInterval)
{
    /* Make sure progress hasn't finished already: */
    if (!m_comProgress.isOk() || m_comProgress.GetCompleted())
    {
        /* Progress completed? */
        if (m_comProgress.isOk())
            return Accepted;
        /* Or aborted? */
        else
            return Rejected;
    }

    /* Start refresh timer (if necessary): */
    int id = 0;
    if (m_fLegacyHandling)
        id = startTimer(cRefreshInterval);

    /* Set busy cursor.
     * We don't do this on the Mac, cause regarding the design rules of
     * Apple there is no busy window behavior. A window should always be
     * responsive and it is in our case (We show the progress dialog bar). */
#ifndef VBOX_WS_MAC
    if (m_fCancelEnabled)
        QApplication::setOverrideCursor(QCursor(Qt::BusyCursor));
    else
        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
#endif /* VBOX_WS_MAC */

    /* Create a local event-loop: */
    {
        /* Guard ourself for the case
         * we destroyed ourself in our event-loop: */
        QPointer<UIProgressDialog> guard = this;

        /* Holds the modal loop, but don't show the window immediately: */
        execute(false);

        /* Are we still valid? */
        if (guard.isNull())
            return Rejected;
    }

    /* Kill refresh timer (if necessary): */
    if (m_fLegacyHandling)
        killTimer(id);

#ifndef VBOX_WS_MAC
    /* Reset the busy cursor */
    QApplication::restoreOverrideCursor();
#endif /* VBOX_WS_MAC */

    return result();
}

void UIProgressDialog::show()
{
    /* We should not show progress-dialog
     * if it was already finalized but not yet closed.
     * This could happens in case of some other
     * modal dialog prevents our event-loop from
     * being exit overlapping 'this'. */
    if (!m_fEnded)
        QIDialog::show();
}

void UIProgressDialog::reject()
{
    if (m_fCancelEnabled)
        sltCancelOperation();
}

void UIProgressDialog::timerEvent(QTimerEvent *)
{
    /* Call the timer event handling delegate: */
    handleTimerEvent();
}

void UIProgressDialog::closeEvent(QCloseEvent *pEvent)
{
    if (m_fCancelEnabled)
        sltCancelOperation();
    else
        pEvent->ignore();
}

void UIProgressDialog::sltHandleProgressPercentageChange(const QUuid &, const int iPercent)
{
    /* New mode only: */
    AssertReturnVoid(!m_fLegacyHandling);

    /* Update progress: */
    updateProgressState();
    updateProgressPercentage(iPercent);
}

void UIProgressDialog::sltHandleProgressTaskComplete(const QUuid &)
{
    /* New mode only: */
    AssertReturnVoid(!m_fLegacyHandling);

    /* If progress-dialog is not yet ended but progress is aborted or completed: */
    if (!m_fEnded && (!m_comProgress.isOk() || m_comProgress.GetCompleted()))
    {
        /* Set progress to 100%: */
        updateProgressPercentage(100);

        /* Try to close the dialog: */
        closeProgressDialog();
    }
}

void UIProgressDialog::sltHandleWindowStackChange()
{
    /* If progress-dialog is not yet ended but progress is aborted or completed: */
    if (!m_fEnded && (!m_comProgress.isOk() || m_comProgress.GetCompleted()))
    {
        /* Try to close the dialog: */
        closeProgressDialog();
    }
}

void UIProgressDialog::sltCancelOperation()
{
    m_pButtonCancel->setEnabled(false);
    m_comProgress.Cancel();
}

void UIProgressDialog::prepare()
{
    /* Setup dialog: */
    if (m_strTitle.isNull())
        setWindowTitle(m_comProgress.GetDescription());
    else
        setWindowTitle(QString("%1: %2").arg(m_strTitle, m_comProgress.GetDescription()));
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
#ifdef VBOX_WS_MAC
    ::darwinSetHidesAllTitleButtons(this);
#endif

    /* Make sure dialog is handling window stack changes: */
    connect(&windowManager(), &UIModalWindowManager::sigStackChanged,
            this, &UIProgressDialog::sltHandleWindowStackChange);

    /* Prepare: */
    prepareEventHandler();
    prepareWidgets();
}

void UIProgressDialog::prepareEventHandler()
{
    if (!m_fLegacyHandling)
    {
        /* Create CProgress event handler: */
        m_pEventHandler = new UIProgressEventHandler(this, m_comProgress);
        connect(m_pEventHandler, &UIProgressEventHandler::sigProgressPercentageChange,
                this, &UIProgressDialog::sltHandleProgressPercentageChange);
        connect(m_pEventHandler, &UIProgressEventHandler::sigProgressTaskComplete,
                this, &UIProgressDialog::sltHandleProgressTaskComplete);
    }
}

void UIProgressDialog::prepareWidgets()
{
    /* Create main layout: */
    QHBoxLayout *pMainLayout = new QHBoxLayout(this);
    AssertPtrReturnVoid(pMainLayout);
    {
        /* Configure layout: */
#ifdef VBOX_WS_MAC
        if (m_pImage)
            pMainLayout->setContentsMargins(30, 15, 30, 15);
        else
            pMainLayout->setContentsMargins(6, 6, 6, 6);
#endif

        /* If there is image: */
        if (m_pImage)
        {
            /* Create image label: */
            m_pLabelImage = new QLabel;
            AssertPtrReturnVoid(m_pLabelImage);
            {
                /* Configure label: */
                m_pLabelImage->setPixmap(*m_pImage);

                /* Add into layout: */
                pMainLayout->addWidget(m_pLabelImage);
            }
        }

        /* Create description layout: */
        QVBoxLayout *pDescriptionLayout = new QVBoxLayout;
        AssertPtrReturnVoid(pDescriptionLayout);
        {
            /* Configure layout: */
            pDescriptionLayout->setContentsMargins(0, 0, 0, 0);

            /* Add stretch: */
            pDescriptionLayout->addStretch(1);

            /* Create description label: */
            m_pLabelDescription = new QILabel;
            AssertPtrReturnVoid(m_pLabelDescription);
            {
                /* Configure label: */
                if (m_cOperations > 1)
                    m_pLabelDescription->setText(QString(m_spcszOpDescTpl)
                                                 .arg(m_comProgress.GetOperationDescription())
                                                 .arg(m_uCurrentOperation).arg(m_cOperations));
                else
                    m_pLabelDescription->setText(QString("%1 ...")
                                                 .arg(m_comProgress.GetOperationDescription()));

                /* Add into layout: */
                pDescriptionLayout->addWidget(m_pLabelDescription, 0, Qt::AlignHCenter);
            }

            /* Create proggress layout: */
            QHBoxLayout *pProgressLayout = new QHBoxLayout;
            AssertPtrReturnVoid(pProgressLayout);
            {
                /* Configure layout: */
                pProgressLayout->setContentsMargins(0, 0, 0, 0);

                /* Create progress-bar: */
                m_pProgressBar = new QProgressBar;
                AssertPtrReturnVoid(m_pProgressBar);
                {
                    /* Configure progress-bar: */
                    // WORKAROUND:
                    // Based on agreement implemented in r131088 and r131090,
                    // if progress has just one operation with weight equal to 1,
                    // we should make it "infinite" by setting maximum to minimum.
                    // But be aware that this can and will be overridden by
                    // updated progress percentage if it's changing.
                    if (m_cOperations == 1 && m_uCurrentOperationWeight == 1)
                        m_pProgressBar->setMaximum(0);
                    else
                        m_pProgressBar->setMaximum(100);
                    m_pProgressBar->setValue(0);

                    /* Add into layout: */
                    pProgressLayout->addWidget(m_pProgressBar, 0, Qt::AlignVCenter);
                }

                /* Create cancel button: */
                m_pButtonCancel = new UIMiniCancelButton;
                AssertPtrReturnVoid(m_pButtonCancel);
                {
                    /* Configure cancel button: */
                    m_fCancelEnabled = m_comProgress.GetCancelable();
                    m_pButtonCancel->setEnabled(m_fCancelEnabled);
                    m_pButtonCancel->setFocusPolicy(Qt::ClickFocus);
                    connect(m_pButtonCancel, &UIMiniCancelButton::clicked, this, &UIProgressDialog::sltCancelOperation);

                    /* Add into layout: */
                    pProgressLayout->addWidget(m_pButtonCancel, 0, Qt::AlignVCenter);
                }

                /* Add into layout: */
                pDescriptionLayout->addLayout(pProgressLayout);
            }

            /* Create estimation label: */
            m_pLabelEta = new QILabel;
            {
                /* Add into layout: */
                pDescriptionLayout->addWidget(m_pLabelEta, 0, Qt::AlignLeft | Qt::AlignVCenter);
            }

            /* Add stretch: */
            pDescriptionLayout->addStretch(1);

            /* Add into layout: */
            pMainLayout->addLayout(pDescriptionLayout);
        }
    }

    /* Translate finally: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
        this, &UIProgressDialog::sltRetranslateUI);

    /* The progress dialog will be shown automatically after
     * the duration is over if progress is not finished yet. */
    QTimer::singleShot(m_cMinDuration, this, SLOT(show()));
}

void UIProgressDialog::cleanupWidgets()
{
    /* Nothing for now. */
}

void UIProgressDialog::cleanupEventHandler()
{
    if (!m_fLegacyHandling)
    {
        /* Destroy CProgress event handler: */
        delete m_pEventHandler;
        m_pEventHandler = 0;
    }
}

void UIProgressDialog::cleanup()
{
    /* Wait for CProgress to complete: */
    m_comProgress.WaitForCompletion(-1);

    /* Call the timer event handling delegate: */
    if (m_fLegacyHandling)
        handleTimerEvent();

    /* Cleanup: */
    cleanupEventHandler();
    cleanupWidgets();
}

void UIProgressDialog::updateProgressState()
{
    /* Mark progress canceled if so: */
    if (m_comProgress.GetCanceled())
        m_pLabelEta->setText(tr("Canceling..."));
    /* Update the progress dialog otherwise: */
    else
    {
        /* Update ETA: */
        const long iNewTime = m_comProgress.GetTimeRemaining();
        long iSeconds;
        long iMinutes;
        long iHours;
        long iDays;

        iSeconds  = iNewTime < 0 ? 0 : iNewTime;
        iMinutes  = iSeconds / 60;
        iSeconds -= iMinutes * 60;
        iHours    = iMinutes / 60;
        iMinutes -= iHours   * 60;
        iDays     = iHours   / 24;
        iHours   -= iDays    * 24;

        const QString strDays = UITranslator::daysToString(iDays);
        const QString strHours = UITranslator::hoursToString(iHours);
        const QString strMinutes = UITranslator::minutesToString(iMinutes);
        const QString strSeconds = UITranslator::secondsToString(iSeconds);

        const QString strTwoComp = tr("%1, %2 remaining", "You may wish to translate this more like \"Time remaining: %1, %2\"");
        const QString strOneComp = tr("%1 remaining", "You may wish to translate this more like \"Time remaining: %1\"");

        if      (iDays > 1 && iHours > 0)
            m_pLabelEta->setText(strTwoComp.arg(strDays).arg(strHours));
        else if (iDays > 1)
            m_pLabelEta->setText(strOneComp.arg(strDays));
        else if (iDays > 0 && iHours > 0)
            m_pLabelEta->setText(strTwoComp.arg(strDays).arg(strHours));
        else if (iDays > 0 && iMinutes > 5)
            m_pLabelEta->setText(strTwoComp.arg(strDays).arg(strMinutes));
        else if (iDays > 0)
            m_pLabelEta->setText(strOneComp.arg(strDays));
        else if (iHours > 2)
            m_pLabelEta->setText(strOneComp.arg(strHours));
        else if (iHours > 0 && iMinutes > 0)
            m_pLabelEta->setText(strTwoComp.arg(strHours).arg(strMinutes));
        else if (iHours > 0)
            m_pLabelEta->setText(strOneComp.arg(strHours));
        else if (iMinutes > 2)
            m_pLabelEta->setText(strOneComp.arg(strMinutes));
        else if (iMinutes > 0 && iSeconds > 5)
            m_pLabelEta->setText(strTwoComp.arg(strMinutes).arg(strSeconds));
        else if (iMinutes > 0)
            m_pLabelEta->setText(strOneComp.arg(strMinutes));
        else if (iSeconds > 5)
            m_pLabelEta->setText(strOneComp.arg(strSeconds));
        else if (iSeconds > 0)
            m_pLabelEta->setText(tr("A few seconds remaining"));
        else
            m_pLabelEta->clear();

        /* Then operation text (if changed): */
        ulong uNewOp = m_comProgress.GetOperation() + 1;
        if (uNewOp != m_uCurrentOperation)
        {
            m_uCurrentOperation = uNewOp;
            m_uCurrentOperationWeight = m_comProgress.GetOperationWeight();
            m_pLabelDescription->setText(QString(m_spcszOpDescTpl)
                                       .arg(m_comProgress.GetOperationDescription())
                                       .arg(m_uCurrentOperation).arg(m_cOperations));
        }

        /* Then cancel button: */
        m_fCancelEnabled = m_comProgress.GetCancelable();
        m_pButtonCancel->setEnabled(m_fCancelEnabled);
    }
}

void UIProgressDialog::updateProgressPercentage(int iPercent /* = -1 */)
{
    /* Handle default call: */
    if (iPercent == -1)
        iPercent = m_comProgress.GetPercent();

    /* Make sure percentage is reflected properly
     * if progress was "infinite" initially: */
    if (   m_pProgressBar->maximum() == 0
        && iPercent > 0 && iPercent < 100)
        m_pProgressBar->setMaximum(100);

    /* Update operation percentage: */
    m_pProgressBar->setValue(iPercent);

    /* Notify listeners about the operation progress update: */
    emit sigProgressChange(m_cOperations, m_comProgress.GetOperationDescription(),
                           m_comProgress.GetOperation() + 1, iPercent);
}

void UIProgressDialog::closeProgressDialog()
{
    /* If window is on the top of the stack: */
    if (windowManager().isWindowOnTheTopOfTheModalWindowStack(this))
    {
        /* Progress completed? */
        if (m_comProgress.isOk())
            done(Accepted);
        /* Or aborted? */
        else
            done(Rejected);

        /* Mark progress-dialog finished: */
        m_fEnded = true;
    }
}

void UIProgressDialog::handleTimerEvent()
{
    /* Old mode only: */
    AssertReturnVoid(m_fLegacyHandling);

    /* If progress-dialog is ended: */
    if (m_fEnded)
    {
        // WORKAROUND:
        // We should hide progress-dialog if it was already ended but not yet closed.  This could happen
        // in case if some other modal dialog prevents our event-loop from being exit overlapping 'this'.
        /* If window is on the top of the stack and still shown: */
        if (!isHidden() && windowManager().isWindowOnTheTopOfTheModalWindowStack(this))
            hide();

        return;
    }

    /* If progress-dialog is not yet ended but progress is aborted or completed: */
    if (!m_comProgress.isOk() || m_comProgress.GetCompleted())
    {
        /* Set progress to 100%: */
        updateProgressPercentage(100);

        /* Try to close the dialog: */
        return closeProgressDialog();
    }

    /* Update progress: */
    updateProgressState();
    updateProgressPercentage();
}
