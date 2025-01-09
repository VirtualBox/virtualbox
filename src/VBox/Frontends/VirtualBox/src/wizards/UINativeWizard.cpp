/* $Id: UINativeWizard.cpp 107666 2025-01-09 15:52:29Z sergey.dubov@oracle.com $ */
/** @file
 * VBox Qt GUI - UINativeWizard class implementation.
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

/* Qt includes: */
#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>
#include <QWindow>

/* GUI includes: */
#include "QIRichTextLabel.h"
#include "UICommon.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIExtraDataManager.h"
#include "UIHelpBrowserDialog.h"
#include "UIIconPool.h"
#include "UINativeWizard.h"
#include "UINativeWizardPage.h"
#include "UINotificationCenter.h"
#include "UIShortcutPool.h"
#include "UITranslationEventListener.h"

#ifdef VBOX_WS_MAC
UIFrame::UIFrame(QWidget *pParent)
    : QWidget(pParent)
{
}

void UIFrame::paintEvent(QPaintEvent *pEvent)
{
    /* Sanity check: */
    AssertPtrReturnVoid(pEvent);

    /* Prepare painter: */
    QPainter painter(this);

    /* Limit painting with incoming rectangle: */
    painter.setClipRect(pEvent->rect());

    /* Check whether we should use Active or Inactive palette: */
    const bool fActive = parentWidget() && parentWidget()->isActiveWindow();

    /* Paint background: */
    QColor backgroundColor = QGuiApplication::palette().color(fActive ? QPalette::Active : QPalette::Inactive, QPalette::Window);
    backgroundColor.setAlpha(100);
    painter.setPen(backgroundColor);
    painter.setBrush(backgroundColor);
    painter.drawRect(rect());

    /* Paint borders: */
    painter.setPen(QGuiApplication::palette().color(fActive ? QPalette::Active : QPalette::Inactive, QPalette::Window).darker(130));
    QLine line1(0,                  0,                      rect().width() - 1, 0);
    QLine line2(rect().width() - 1, 0,                      rect().width() - 1, rect().height() - 1);
    QLine line3(rect().width() - 1, rect().height() - 1, 0,                     rect().height() - 1);
    QLine line4(0,                  rect().height() - 1, 0,                     0);
    painter.drawLine(line1);
    painter.drawLine(line2);
    painter.drawLine(line3);
    painter.drawLine(line4);
}
#endif /* VBOX_WS_MAC */


UINativeWizard::UINativeWizard(QWidget *pParent,
                               WizardType enmType,
                               const QString &strHelpKeyword /* = QString() */)
    : QDialog(pParent, Qt::Window)
    , m_enmType(enmType)
    , m_enmMode(gEDataManager->isSettingsInExpertMode() ? WizardMode_Expert : WizardMode_Basic)
    , m_strHelpKeyword(strHelpKeyword)
    , m_iLastIndex(-1)
    , m_fAborted(true)
    , m_fClosed(false)
    , m_pLabelPixmap(0)
    , m_pLayoutRight(0)
    , m_pLabelPageTitle(0)
    , m_pWidgetStack(0)
    , m_pNotificationCenter(0)
{
    prepare();
}

UINativeWizard::~UINativeWizard()
{
    cleanup();
}

UINotificationCenter *UINativeWizard::notificationCenter() const
{
    return m_pNotificationCenter;
}

bool UINativeWizard::handleNotificationProgressNow(UINotificationProgress *pProgress)
{
    const bool fResult = m_pNotificationCenter->handleNow(pProgress);
    return fResult;
}

QPushButton *UINativeWizard::wizardButton(const WizardButtonType &enmType) const
{
    return m_buttons.value(enmType);
}

int UINativeWizard::exec()
{
    /* Init wizard: */
    init();

    /* Call to base-class: */
    return QDialog::exec();
}

void UINativeWizard::show()
{
    /* Init wizard: */
    init();

    /* Call to base-class: */
    return QDialog::show();
}

void UINativeWizard::setPixmapName(const QString &strName)
{
    m_strPixmapName = strName;
}

bool UINativeWizard::isPageVisible(int iIndex) const
{
    return !m_invisiblePages.contains(iIndex);
}

void UINativeWizard::setPageVisible(int iIndex, bool fVisible)
{
    AssertMsgReturnVoid(iIndex || fVisible, ("Can't hide 1st wizard page!\n"));
    if (fVisible)
        m_invisiblePages.remove(iIndex);
    else
        m_invisiblePages.insert(iIndex);
    /* Update the button labels since the last visible page might have changed. Thus 'Next' <-> 'Finish' might be needed: */
    sltRetranslateUI();
}

int UINativeWizard::addPage(UINativeWizardPage *pPage)
{
    /* Sanity check: */
    AssertPtrReturn(pPage, -1);
    AssertPtrReturn(pPage->layout(), -1);

    /* Adjust page layout: */
    const int iL = 0;
    const int iT = 0;
    const int iR = qApp->style()->pixelMetric(QStyle::PM_LayoutRightMargin);
    const int iB = qApp->style()->pixelMetric(QStyle::PM_LayoutBottomMargin);
    pPage->layout()->setContentsMargins(iL, iT, iR, iB);

    /* Add page to wizard's stack: */
    m_pWidgetStack->blockSignals(true);
    const int iIndex = m_pWidgetStack->addWidget(pPage);
    m_pWidgetStack->blockSignals(false);

    /* Make sure wizard is aware of page validity changes: */
    connect(pPage, &UINativeWizardPage::completeChanged,
            this, &UINativeWizard::sltCompleteChanged);

    /* Returns added page index: */
    return iIndex;
}

void UINativeWizard::sltRetranslateUI()
{
    /* Translate Help button: */
    QPushButton *pButtonHelp = wizardButton(WizardButtonType_Help);
    if (pButtonHelp)
    {
        pButtonHelp->setText(tr("&Help"));
        pButtonHelp->setToolTip(tr("Open corresponding Help topic."));
        pButtonHelp->setShortcut(UIShortcutPool::standardSequence(QKeySequence::HelpContents));
    }

    /* Translate Back button: */
    QPushButton *pButtonBack = wizardButton(WizardButtonType_Back);
    AssertMsgReturnVoid(pButtonBack, ("No Back wizard button found!\n"));
    pButtonBack->setText(tr("&Back"));
    pButtonBack->setToolTip(tr("Go to previous wizard page."));

    /* Translate Next button: */
    QPushButton *pButtonNext = wizardButton(WizardButtonType_Next);
    AssertMsgReturnVoid(pButtonNext, ("No Next wizard button found!\n"));
    if (!isLastVisiblePage(m_pWidgetStack->currentIndex()))
    {
        pButtonNext->setText(tr("&Next"));
        pButtonNext->setToolTip(tr("Go to next wizard page."));
    }
    else
    {
        pButtonNext->setText(tr("&Finish"));
        pButtonNext->setToolTip(tr("Commit all wizard data."));
    }

    /* Translate Cancel button: */
    QPushButton *pButtonCancel = wizardButton(WizardButtonType_Cancel);
    AssertMsgReturnVoid(pButtonCancel, ("No Cancel wizard button found!\n"));
    pButtonCancel->setText(tr("&Cancel"));
    pButtonCancel->setToolTip(tr("Cancel wizard execution."));
}

void UINativeWizard::keyPressEvent(QKeyEvent *pEvent)
{
    // WORKAROUND:
    // In non-modal case we'll have to handle Escape button ourselves.
    // In modal case QDialog does this itself internally by unwinding the event-loop.

    /* Different handling depending on current modality: */
    const Qt::WindowModality enmModality = windowHandle()->modality();

    /* For non-modal case: */
    if (enmModality == Qt::NonModal)
    {
        /* Special pre-processing for some keys: */
        switch (pEvent->key())
        {
            case Qt::Key_Escape:
            {
                close();
                return;
            }
            default:
                break;
        }
    }

    /* Call to base-class: */
    return QDialog::keyPressEvent(pEvent);
}

void UINativeWizard::closeEvent(QCloseEvent *pEvent)
{
    /* Different handling depending on current modality: */
    const Qt::WindowModality enmModality = windowHandle()->modality();

    /* For non-modal case: */
    if (enmModality == Qt::NonModal)
    {
        /* Ignore event initially: */
        pEvent->ignore();

        /* Let the notification-center abort blocking operations: */
        if (m_pNotificationCenter->hasOperationsPending())
            m_pNotificationCenter->abortOperations();
        else
        /* Tell the listener to close us (once): */
        if (!m_fClosed)
        {
            m_fClosed = true;
            if (m_fAborted)
                cleanWizard();
            emit sigClose(m_enmType);
        }

        return;
    }

    /* Call to base-class: */
    QDialog::closeEvent(pEvent);
}

void UINativeWizard::sltCurrentIndexChanged(int iIndex /* = -1 */)
{
    /* Update translation: */
    sltRetranslateUI();

    /* Sanity check: */
    AssertPtrReturnVoid(m_pWidgetStack);

    /* -1 means current one page: */
    if (iIndex == -1)
        iIndex = m_pWidgetStack->currentIndex();

    /* Disable/enable Back button: */
    QPushButton *pButtonBack = wizardButton(WizardButtonType_Back);
    AssertMsgReturnVoid(pButtonBack, ("No Back wizard button found!\n"));
    pButtonBack->setEnabled(iIndex > 0);

    /* Initialize corresponding page: */
    UINativeWizardPage *pPage = qobject_cast<UINativeWizardPage*>(m_pWidgetStack->widget(iIndex));
    AssertPtrReturnVoid(pPage);
    m_pLabelPageTitle->setText(pPage->title());
    if (iIndex > m_iLastIndex)
        pPage->initializePage();

    /* Disable/enable Next button: */
    QPushButton *pButtonNext = wizardButton(WizardButtonType_Next);
    AssertMsgReturnVoid(pButtonNext, ("No Next wizard button found!\n"));
    pButtonNext->setEnabled(pPage->isComplete());

    /* Update last index: */
    m_iLastIndex = iIndex;
}

void UINativeWizard::sltCompleteChanged()
{
    /* Make sure sender is current widget: */
    QWidget *pSender = qobject_cast<QWidget*>(sender());
    if (pSender != m_pWidgetStack->currentWidget())
        return;

    /* Allow Next button only if current page is complete: */
    UINativeWizardPage *pPage = qobject_cast<UINativeWizardPage*>(pSender);
    QPushButton *pButtonNext = wizardButton(WizardButtonType_Next);
    AssertMsgReturnVoid(pButtonNext, ("No Next wizard button found!\n"));
    pButtonNext->setEnabled(pPage->isComplete());
}

void UINativeWizard::sltPrevious()
{
    /* For all allowed pages besides the 1st one we going backward: */
    bool fPreviousFound = false;
    int iIteratedIndex = m_pWidgetStack->currentIndex();
    while (!fPreviousFound && iIteratedIndex > 0)
        if (isPageVisible(--iIteratedIndex))
            fPreviousFound = true;
    if (fPreviousFound)
        m_pWidgetStack->setCurrentIndex(iIteratedIndex);
}

void UINativeWizard::sltNext()
{
    /* Look for Next button: */
    QPushButton *pButtonNext = wizardButton(WizardButtonType_Next);
    AssertMsgReturnVoid(pButtonNext, ("No Next wizard button found!\n"));

    /* Validate page before going forward: */
    AssertReturnVoid(m_pWidgetStack->currentIndex() < m_pWidgetStack->count());
    UINativeWizardPage *pPage = qobject_cast<UINativeWizardPage*>(m_pWidgetStack->currentWidget());
    AssertPtrReturnVoid(pPage);
    pButtonNext->setEnabled(false);
    const bool fIsPageValid = pPage->validatePage();
    pButtonNext->setEnabled(true);
    if (!fIsPageValid)
        return;

    /* For all allowed pages besides the last one we going forward: */
    bool fNextFound = false;
    int iIteratedIndex = m_pWidgetStack->currentIndex();
    while (!fNextFound && iIteratedIndex < m_pWidgetStack->count() - 1)
        if (isPageVisible(++iIteratedIndex))
            fNextFound = true;
    if (fNextFound)
        m_pWidgetStack->setCurrentIndex(iIteratedIndex);
    /* For last one we just accept the wizard: */
    else
    {
        /* Different handling depending on current modality: */
        if (windowHandle()->modality() == Qt::NonModal)
        {
            m_fAborted = false;
            close();
        }
        else
            accept();
    }
}

void UINativeWizard::sltHandleHelpRequest()
{
    UIHelpBrowserDialog::findManualFileAndShow(uiCommon().helpKeyword(this));
}

void UINativeWizard::prepare()
{
    /* Prepare main layout: */
    QVBoxLayout *pLayoutMain = new QVBoxLayout(this);
    if (pLayoutMain)
    {
        /* No need for margins and spacings between sub-layouts: */
        pLayoutMain->setContentsMargins(0, 0, 0, 0);
        pLayoutMain->setSpacing(0);

        /* Prepare upper layout: */
        QHBoxLayout *pLayoutUpper = new QHBoxLayout;
        if (pLayoutUpper)
        {
#ifdef VBOX_WS_MAC
            /* No need for bottom margin on macOS, reseting others to default: */
            const int iL = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin);
            const int iT = qApp->style()->pixelMetric(QStyle::PM_LayoutTopMargin);
            const int iR = qApp->style()->pixelMetric(QStyle::PM_LayoutRightMargin);
            pLayoutUpper->setContentsMargins(iL, iT, iR, 0);
#endif /* VBOX_WS_MAC */
            /* Reset spacing to default, it was flawed by parent inheritance: */
            const int iSpacing = qApp->style()->pixelMetric(QStyle::PM_LayoutHorizontalSpacing);
            pLayoutUpper->setSpacing(iSpacing);

            /* Prepare pixmap label: */
            m_pLabelPixmap = new QLabel(this);
            if (m_pLabelPixmap)
            {
                m_pLabelPixmap->setAlignment(Qt::AlignTop);
#ifdef VBOX_WS_MAC
                /* On macOS this label contains background, which isn't a part of layout, moving manually: */
                m_pLabelPixmap->move(0, 0);
                /* Spacer to make look&feel native on macOS: */
                QSpacerItem *pSpacer = new QSpacerItem(200, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
                pLayoutUpper->addItem(pSpacer);
#else /* !VBOX_WS_MAC */
                /* Just add label into layout on other platforms: */
                pLayoutUpper->addWidget(m_pLabelPixmap);
#endif /* !VBOX_WS_MAC */
            }

            /* Prepare right layout: */
            m_pLayoutRight = new QVBoxLayout;
            if (m_pLayoutRight)
            {
                /* Prepare page title label: */
                m_pLabelPageTitle = new QLabel(this);
                if (m_pLabelPageTitle)
                {
                    /* Title should have big/fat font: */
                    QFont labelFont = m_pLabelPageTitle->font();
                    labelFont.setBold(true);
                    labelFont.setPointSize(labelFont.pointSize() + 4);
                    m_pLabelPageTitle->setFont(labelFont);

                    m_pLayoutRight->addWidget(m_pLabelPageTitle);
                }

#ifdef VBOX_WS_MAC
                /* Prepare frame around widget-stack on macOS for nativity purposes: */
                UIFrame *pFrame = new UIFrame(this);
                if (pFrame)
                {
                    /* Prepare frame layout: */
                    QVBoxLayout *pLayoutFrame = new QVBoxLayout(pFrame);
                    if (pLayoutFrame)
                    {
                        /* Prepare widget-stack: */
                        m_pWidgetStack = new QStackedWidget(pFrame);
                        if (m_pWidgetStack)
                        {
                            connect(m_pWidgetStack, &QStackedWidget::currentChanged, this, &UINativeWizard::sltCurrentIndexChanged);
                            pLayoutFrame->addWidget(m_pWidgetStack);
                        }
                    }

                    /* Add to layout: */
                    m_pLayoutRight->addWidget(pFrame);
                }
#else /* !VBOX_WS_MAC */
                /* Prepare widget-stack directly on other platforms: */
                m_pWidgetStack = new QStackedWidget(this);
                if (m_pWidgetStack)
                {
                    connect(m_pWidgetStack, &QStackedWidget::currentChanged, this, &UINativeWizard::sltCurrentIndexChanged);
                    m_pLayoutRight->addWidget(m_pWidgetStack);
                }
#endif /* !VBOX_WS_MAC */

                /* Add to layout: */
                pLayoutUpper->addLayout(m_pLayoutRight);
            }

            /* Add to layout: */
            pLayoutMain->addLayout(pLayoutUpper, 1);
        }

        /* Prepare bottom widget: */
        QWidget *pWidgetBottom = new QWidget(this);
        if (pWidgetBottom)
        {
#ifndef VBOX_WS_MAC
            /* Adjust palette a bit on Windows/X11 for native purposes: */
            pWidgetBottom->setAutoFillBackground(true);
            QPalette pal = QGuiApplication::palette();
            pal.setColor(QPalette::Active, QPalette::Window, pal.color(QPalette::Active, QPalette::Window).darker(110));
            pal.setColor(QPalette::Inactive, QPalette::Window, pal.color(QPalette::Inactive, QPalette::Window).darker(110));
            pWidgetBottom->setPalette(pal);
#endif /* !VBOX_WS_MAC */

            /* Prepare bottom layout: */
            QHBoxLayout *pLayoutBottom = new QHBoxLayout(pWidgetBottom);
            if (pLayoutBottom)
            {
                /* Reset margins to default, they were flawed by parent inheritance: */
                const int iL = qApp->style()->pixelMetric(QStyle::PM_LayoutLeftMargin);
                const int iT = qApp->style()->pixelMetric(QStyle::PM_LayoutTopMargin);
                const int iR = qApp->style()->pixelMetric(QStyle::PM_LayoutRightMargin);
                const int iB = qApp->style()->pixelMetric(QStyle::PM_LayoutBottomMargin);
                pLayoutBottom->setContentsMargins(iL, iT, iR, iB);

                // WORKAROUND:
                // Prepare dialog button-box? Huh, no .. QWizard has different opinion.
                // So we are hardcoding order, same on all platforms, which is the case.
                for (int i = WizardButtonType_Invalid + 1; i < WizardButtonType_Max; ++i)
                {
                    const WizardButtonType enmType = (WizardButtonType)i;
                    /* Create Help button only if help keyword is set.
                     * Create other buttons in any case: */
                    if (enmType != WizardButtonType_Help || !m_strHelpKeyword.isEmpty())
                        m_buttons[enmType] = new QPushButton(pWidgetBottom);
                    QPushButton *pButton = wizardButton(enmType);
                    if (pButton)
                        pLayoutBottom->addWidget(pButton);
                    if (enmType == WizardButtonType_Help)
                        pLayoutBottom->addStretch(1);
                    if (   pButton
                        && enmType == WizardButtonType_Next)
                        pButton->setDefault(true);
                }
                /* Connect buttons: */
                if (wizardButton(WizardButtonType_Help))
                {
                    connect(wizardButton(WizardButtonType_Help), &QPushButton::clicked,
                            this, &UINativeWizard::sltHandleHelpRequest);
                    uiCommon().setHelpKeyword(this, m_strHelpKeyword);
                }
                connect(wizardButton(WizardButtonType_Back), &QPushButton::clicked,
                        this, &UINativeWizard::sltPrevious);
                connect(wizardButton(WizardButtonType_Next), &QPushButton::clicked,
                        this, &UINativeWizard::sltNext);
                connect(wizardButton(WizardButtonType_Cancel), &QPushButton::clicked,
                        this, &UINativeWizard::close);
            }

            /* Add to layout: */
            pLayoutMain->addWidget(pWidgetBottom);
        }
    }

    /* Prepare local notification-center: */
    m_pNotificationCenter = new UINotificationCenter(this);
    if (m_pNotificationCenter)
        connect(m_pNotificationCenter, &UINotificationCenter::sigOperationsAborted,
                this, &UINativeWizard::close, Qt::QueuedConnection);

    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UINativeWizard::sltRetranslateUI);
}

void UINativeWizard::cleanup()
{
    /* Cleanup local notification-center: */
    delete m_pNotificationCenter;
    m_pNotificationCenter = 0;
}

void UINativeWizard::init()
{
    /* Populate pages: */
    populatePages();

    /* Translate wizard: */
    sltRetranslateUI();
    /* Translate wizard pages: */
    retranslatePages();

    /* Resize wizard to 'golden ratio': */
    resizeToGoldenRatio();

    /* Make sure current page initialized: */
    sltCurrentIndexChanged();
}

void UINativeWizard::retranslatePages()
{
    /* Translate all the pages: */
    for (int i = 0; i < m_pWidgetStack->count(); ++i)
        qobject_cast<UINativeWizardPage*>(m_pWidgetStack->widget(i))->retranslate();
}

void UINativeWizard::resizeToGoldenRatio()
{
    /* Standard top margin: */
    const int iT = qApp->style()->pixelMetric(QStyle::PM_LayoutTopMargin);
    m_pLayoutRight->setContentsMargins(0, iT, 0, 0);
    /* Show title label for Basic mode case: */
    m_pLabelPageTitle->setVisible(m_enmMode == WizardMode_Basic);
#ifndef VBOX_WS_MAC
    /* Hide/show pixmap label on Windows/X11 only, on macOS it's in the background: */
    m_pLabelPixmap->setVisible(!m_strPixmapName.isEmpty());
#endif /* !VBOX_WS_MAC */

    /* For wizard in Basic mode: */
    if (m_enmMode == WizardMode_Basic)
    {
        /* Temporary hide all the QIRichTextLabel(s) to exclude
         * influence onto m_pWidgetStack minimum size-hint below: */
        foreach (QIRichTextLabel *pLabel, findChildren<QIRichTextLabel*>())
            pLabel->hide();
        /* Gather suitable dimensions: */
        const int iStepWidth = 100;
        const int iMinWidth = qMax(100, m_pWidgetStack->minimumSizeHint().width());
        const int iMaxWidth = qMax(iMinWidth, gpDesktop->availableGeometry(this).width() * 3 / 4);
        /* Show all the QIRichTextLabel(s) again, they were hidden above: */
        foreach (QIRichTextLabel *pLabel, findChildren<QIRichTextLabel*>())
            pLabel->show();
        /* Now look for a golden ratio: */
        int iCurrentWidth = iMinWidth;
        do
        {
            /* Assign current QIRichTextLabel(s) width: */
            foreach (QIRichTextLabel *pLabel, findChildren<QIRichTextLabel*>())
                pLabel->setMinimumTextWidth(iCurrentWidth);

            /* Calculate current ratio: */
            const QSize msh = m_pWidgetStack->minimumSizeHint();
            int iWidth = msh.width();
            int iHeight = msh.height();
#ifndef VBOX_WS_MAC
            /* Advance width for standard watermark width: */
            if (!m_strPixmapName.isEmpty())
                iWidth += 145;
            /* Advance height for spacing & title height: */
            if (m_pLayoutRight)
            {
                int iL, iT, iR, iB;
                m_pLayoutRight->getContentsMargins(&iL, &iT, &iR, &iB);
                iHeight += iT + m_pLayoutRight->spacing() + iB;
            }
            if (m_pLabelPageTitle)
                iHeight += m_pLabelPageTitle->minimumSizeHint().height();
#endif /* !VBOX_WS_MAC */
            const double dRatio = (double)iWidth / iHeight;
            if (dRatio > 1.6)
                break;

            /* Advance current width: */
            iCurrentWidth += iStepWidth;
        }
        while (iCurrentWidth < iMaxWidth);
    }

#ifdef VBOX_WS_MAC
    /* Assign background finally: */
    if (!m_strPixmapName.isEmpty())
        assignBackground();
#else
    /* Assign watermark finally: */
    if (!m_strPixmapName.isEmpty())
        assignWatermark();
#endif /* !VBOX_WS_MAC */

    /* Make sure layouts are freshly updated & activated: */
    foreach (QLayout *pLayout, findChildren<QLayout*>())
    {
        pLayout->update();
        pLayout->activate();
    }
    QCoreApplication::sendPostedEvents(0, QEvent::LayoutRequest);

    /* Resize to minimum size-hint: */
    resize(minimumSizeHint());
}

bool UINativeWizard::isLastVisiblePage(int iPageIndex) const
{
    if (!m_pWidgetStack)
        return false;
    if (iPageIndex == -1)
        return false;
    /* The page itself is not visible: */
    if (m_invisiblePages.contains(iPageIndex))
        return false;
    bool fLastVisible = true;
    /* Look at the page coming after the page with @p iPageIndex and check if they are visible: */
    for (int i = iPageIndex + 1; i < m_pWidgetStack->count(); ++i)
    {
        if (!m_invisiblePages.contains(i))
        {
            fLastVisible = false;
            break;
        }
    }
    return fLastVisible;
}

#ifdef VBOX_WS_MAC
void UINativeWizard::assignBackground()
{
    /* Load pixmap to icon first, this will gather HiDPI pixmaps as well: */
    const QIcon icon = UIIconPool::iconSet(m_strPixmapName);

    /* Acquire pixmap of required size and scale (on basis of parent-widget's device pixel ratio): */
    const QSize standardSize(620, 440);
    const qreal fDevicePixelRatio = parentWidget() && parentWidget()->windowHandle() ? parentWidget()->windowHandle()->devicePixelRatio() : 1;
    const QPixmap pixmapOld = icon.pixmap(standardSize, fDevicePixelRatio);

    /* Assign background finally: */
    m_pLabelPixmap->setPixmap(pixmapOld);
    m_pLabelPixmap->resize(m_pLabelPixmap->minimumSizeHint());
}

#else

void UINativeWizard::assignWatermark()
{
    /* Load pixmap to icon first, this will gather HiDPI pixmaps as well: */
    const QIcon icon = UIIconPool::iconSet(m_strPixmapName);

    /* Acquire pixmap of required size and scale (on basis of parent-widget's device pixel ratio): */
    const QSize standardSize(145, 290);
    const qreal fDevicePixelRatio = parentWidget() && parentWidget()->windowHandle() ? parentWidget()->windowHandle()->devicePixelRatio() : 1;
    const QPixmap pixmapOld = icon.pixmap(standardSize, fDevicePixelRatio);

    /* Convert watermark to image which allows to manage pixel data directly: */
    const QImage imageOld = pixmapOld.toImage();
    /* Use the right-top watermark pixel as frame color: */
    const QRgb rgbFrame = imageOld.pixel(imageOld.width() - 1, 0);

    /* Compose desired height up to pixmap device pixel ratio: */
    int iL, iT, iR, iB;
    m_pLayoutRight->getContentsMargins(&iL, &iT, &iR, &iB);
    const int iSpacing = iT + m_pLayoutRight->spacing() + iB;
    const int iTitleHeight = m_pLabelPageTitle->minimumSizeHint().height();
    const int iStackHeight = m_pWidgetStack->minimumSizeHint().height();
    const int iDesiredHeight = (iTitleHeight + iSpacing + iStackHeight) * pixmapOld.devicePixelRatio();
    /* Create final image on the basis of incoming, applying the rules: */
    QImage imageNew(imageOld.width(), qMax(imageOld.height(), iDesiredHeight), imageOld.format());
    for (int y = 0; y < imageNew.height(); ++y)
    {
        for (int x = 0; x < imageNew.width(); ++x)
        {
            /* Border rule: */
            if (x == imageNew.width() - 1)
                imageNew.setPixel(x, y, rgbFrame);
            /* Horizontal extension rule - use last used color: */
            else if (x >= imageOld.width() && y < imageOld.height())
                imageNew.setPixel(x, y, imageOld.pixel(imageOld.width() - 1, y));
            /* Vertical extension rule - use last used color: */
            else if (y >= imageOld.height() && x < imageOld.width())
                imageNew.setPixel(x, y, imageOld.pixel(x, imageOld.height() - 1));
            /* Common extension rule - use last used color: */
            else if (x >= imageOld.width() && y >= imageOld.height())
                imageNew.setPixel(x, y, imageOld.pixel(imageOld.width() - 1, imageOld.height() - 1));
            /* Else just copy color: */
            else
                imageNew.setPixel(x, y, imageOld.pixel(x, y));
        }
    }

    /* Convert processed image to pixmap: */
    QPixmap pixmapNew = QPixmap::fromImage(imageNew);
    /* For HiDPI support parent-widget's device pixel ratio is to be taken into account: */
    double dRatio = 1.0;
    if (   parentWidget()
        && parentWidget()->window()
        && parentWidget()->window()->windowHandle())
        dRatio = parentWidget()->window()->windowHandle()->devicePixelRatio();
    pixmapNew.setDevicePixelRatio(dRatio);
    /* Assign watermark finally: */
    m_pLabelPixmap->setPixmap(pixmapNew);
}

#endif /* !VBOX_WS_MAC */
