/* $Id: QIArrowSplitter.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIArrowSplitter class implementation.
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
#include <QHBoxLayout>
#include <QStyle>
#include <QTextEdit>

/* GUI includes: */
#include "QIArrowSplitter.h"
#include "QIArrowButtonPress.h"
#include "QIArrowButtonSwitch.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIIconPool.h"
#include "UITranslationEventListener.h"

/* Other VBox includes: */
#include "iprt/assert.h"


/** QTextEdit extension
  * taking into account text-document size-hint.
  * @note Used with QIMessageBox class only.
  * @todo Should be moved/renamed accordingly. */
class QIDetailsBrowser : public QTextEdit
{
    Q_OBJECT;

public:

    /** Constructs details-browser passing @a pParent to the base-class. */
    QIDetailsBrowser(QWidget *pParent = 0);

    /** Returns minimum size-hint. */
    QSize minimumSizeHint() const RT_OVERRIDE RT_FINAL;
    /** Returns size-hint. */
    QSize sizeHint() const RT_OVERRIDE RT_FINAL;

    /** Update scroll-bars. */
    void updateScrollBars();
};


/*********************************************************************************************************************************
*   Class QIDetailsBrowser implementation.                                                                                       *
*********************************************************************************************************************************/

QIDetailsBrowser::QIDetailsBrowser(QWidget *pParent /* = 0 */)
    : QTextEdit(pParent)
{
    /* Prepare: */
    setReadOnly(true);
}

QSize QIDetailsBrowser::minimumSizeHint() const
{
    /* Get document size as the basis: */
    QSize documentSize = document()->size().toSize();
    /* But only document ideal-width can advice wise width: */
    const int iDocumentIdealWidth = (int)document()->idealWidth();
    /* Moreover we should take document margins into account: */
    const int iDocumentMargin = (int)document()->documentMargin();

    /* Compose minimum size-hint on the basis of values above: */
    documentSize.setWidth(iDocumentIdealWidth + iDocumentMargin);
    documentSize.setHeight(documentSize.height() + iDocumentMargin);

    /* Get 40% of the screen-area to limit the resulting hint: */
    const QSize screenGeometryDot4 = gpDesktop->screenGeometry(this).size() * .4;

    /* Calculate minimum size-hint which is document-size limited by screen-area: */
    QSize mSizeHint = documentSize.boundedTo(screenGeometryDot4);

    /* If there is not enough of vertical space: */
    if (mSizeHint.height() < documentSize.height())
    {
        /* We should also take into account vertical scroll-bar extent: */
        int iExtent = QApplication::style()->pixelMetric(QStyle::PM_ScrollBarExtent);
        mSizeHint.setWidth(mSizeHint.width() + iExtent);
    }

    /* Always bound cached hint by 40% of current screen-area: */
    return mSizeHint;
}

QSize QIDetailsBrowser::sizeHint() const
{
    /* Return minimum size-hint: */
    return minimumSizeHint();
}

void QIDetailsBrowser::updateScrollBars()
{
    /* Some Qt issue prevents scroll-bars from update.. */
    Qt::ScrollBarPolicy horizontalPolicy = horizontalScrollBarPolicy();
    Qt::ScrollBarPolicy verticalPolicy = verticalScrollBarPolicy();
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(horizontalPolicy);
    setVerticalScrollBarPolicy(verticalPolicy);
}


/*********************************************************************************************************************************
*   Class QIArrowSplitter implementation.                                                                                        *
*********************************************************************************************************************************/

QIArrowSplitter::QIArrowSplitter(QWidget *pParent /* = 0 */)
    : QWidget(pParent)
    , m_pMainLayout(0)
    , m_pSwitchButton(0)
    , m_pBackButton(0)
    , m_pNextButton(0)
    , m_pDetailsBrowser(0)
    , m_iDetailsIndex(-1)
{
    /* Prepare: */
    prepare();
}

QSize QIArrowSplitter::minimumSizeHint() const
{
    /* Get minimum size-hints: */
    const QSize switchButtonHint = m_pSwitchButton->minimumSizeHint();
    const QSize backButtonHint = m_pBackButton->minimumSizeHint();
    const QSize nextButtonHint = m_pNextButton->minimumSizeHint();
    const QSize detailsBrowserHint = m_pDetailsBrowser->minimumSizeHint();

    /* Calculate width-hint: */
    int iWidthHint = 0;
    iWidthHint += switchButtonHint.width();
    iWidthHint += 100 /* button spacing */;
    iWidthHint += backButtonHint.width();
    iWidthHint += nextButtonHint.width();
    iWidthHint = qMax(iWidthHint, detailsBrowserHint.width());

    /* Calculate height-hint: */
    int iHeightHint = 0;
    iHeightHint = qMax(iHeightHint, switchButtonHint.height());
    iHeightHint = qMax(iHeightHint, backButtonHint.height());
    iHeightHint = qMax(iHeightHint, nextButtonHint.height());
    if (m_pDetailsBrowser->isVisible())
        iHeightHint += m_pMainLayout->spacing() + detailsBrowserHint.height();

    /* Return result: */
    return QSize(iWidthHint, iHeightHint);
}

void QIArrowSplitter::setName(const QString &strName)
{
    /* Assign name for the switch-button: */
    m_pSwitchButton->setText(strName);
    /* Update size-hints: */
    sltUpdateSizeHints();
}

void QIArrowSplitter::setDetails(const QStringPairList &details)
{
    /* Assign new details: */
    m_details = details;
    /* Reset the details-list index: */
    m_iDetailsIndex = m_details.isEmpty() ? -1 : 0;
    /* Update navigation-buttons visibility: */
    sltUpdateNavigationButtonsVisibility();
    /* Update details-browser visibility: */
    sltUpdateDetailsBrowserVisibility();
    /* Update details: */
    updateDetails();
}

void QIArrowSplitter::sltUpdateSizeHints()
{
    /* Let parent layout know our size-hint changed: */
    updateGeometry();
    /* Notify parent about our size-hint changed: */
    emit sigSizeHintChange();
    /* Update details-browser scroll-bars: */
    m_pDetailsBrowser->updateScrollBars();
}

void QIArrowSplitter::sltUpdateNavigationButtonsVisibility()
{
    /* Depending on switch-button state: */
    const bool fExpanded = m_pSwitchButton->isExpanded();
    /* Update back/next button visibility: */
    m_pBackButton->setVisible(m_details.size() > 1 && fExpanded);
    m_pNextButton->setVisible(m_details.size() > 1 && fExpanded);
}

void QIArrowSplitter::sltUpdateDetailsBrowserVisibility()
{
    /* Update details-browser visibility according switch-button state: */
    m_pDetailsBrowser->setVisible(m_details.size() > 0 && m_pSwitchButton->isExpanded());
    /* Update size-hints: */
    sltUpdateSizeHints();
}

void QIArrowSplitter::sltSwitchDetailsPageBack()
{
    /* Make sure details-page index feats the bounds: */
    AssertReturnVoid(m_iDetailsIndex > 0);
    /* Decrease details-list index: */
    --m_iDetailsIndex;
    /* Update details: */
    updateDetails();
}

void QIArrowSplitter::sltSwitchDetailsPageNext()
{
    /* Make sure details-page index feats the bounds: */
    AssertReturnVoid(m_iDetailsIndex < m_details.size() - 1);
    /* Increase details-list index: */
    ++m_iDetailsIndex;
    /* Update details: */
    updateDetails();
}

void QIArrowSplitter::sltRetranslateUI()
{
    /* Update details: */
    updateDetails();
}

void QIArrowSplitter::prepare()
{
    /* Create main-layout: */
    m_pMainLayout = new QVBoxLayout(this);
    AssertPtrReturnVoid(m_pMainLayout);
    {
        /* Configure main-layout: */
        m_pMainLayout->setContentsMargins(0, 0, 0, 0);
#ifdef VBOX_WS_MAC
        m_pMainLayout->setSpacing(5);
#else
        m_pMainLayout->setSpacing(qApp->style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing) / 2);
#endif
        /* Create button-layout: */
        QHBoxLayout *pButtonLayout = new QHBoxLayout;
        AssertPtrReturnVoid(pButtonLayout);
        {
            /* Determine icon metric: */
            const QStyle *pStyle = QApplication::style();
            const int iIconMetric = (int)(pStyle->pixelMetric(QStyle::PM_SmallIconSize) * .625);
            /* Configure button-layout: */
            pButtonLayout->setContentsMargins(0, 0, 0, 0);
            pButtonLayout->setSpacing(0);
            /* Create switch-button: */
            m_pSwitchButton = new QIArrowButtonSwitch;
            AssertPtrReturnVoid(m_pSwitchButton);
            {
                /* Configure switch-button: */
                m_pSwitchButton->setIconSize(QSize(iIconMetric, iIconMetric));
                m_pSwitchButton->setIcons(UIIconPool::iconSet(":/arrow_right_10px.png"),
                                          UIIconPool::iconSet(":/arrow_down_10px.png"));
                connect(m_pSwitchButton, &QIArrowButtonSwitch::sigClicked, this, &QIArrowSplitter::sltUpdateNavigationButtonsVisibility);
                connect(m_pSwitchButton, &QIArrowButtonSwitch::sigClicked, this, &QIArrowSplitter::sltUpdateDetailsBrowserVisibility);

                /* Add switch-button into button-layout: */
                pButtonLayout->addWidget(m_pSwitchButton);
            }
            /* Add stretch: */
            pButtonLayout->addStretch();
            /* Create back-button: */
            m_pBackButton = new QIArrowButtonPress(QIArrowButtonPress::ButtonType_Back);
            AssertPtrReturnVoid(m_pBackButton);
            {
                /* Configure back-button: */
                m_pBackButton->setIconSize(QSize(iIconMetric, iIconMetric));
                m_pBackButton->setIcon(UIIconPool::iconSet(":/arrow_left_10px.png"));
                connect(m_pBackButton, &QIArrowButtonPress::sigClicked, this, &QIArrowSplitter::sltSwitchDetailsPageBack);

                /* Add back-button into button-layout: */
                pButtonLayout->addWidget(m_pBackButton);
            }
            /* Create next-button: */
            m_pNextButton = new QIArrowButtonPress(QIArrowButtonPress::ButtonType_Next);
            AssertPtrReturnVoid(m_pNextButton);
            {
                /* Configure next-button: */
                m_pNextButton->setIconSize(QSize(iIconMetric, iIconMetric));
                m_pNextButton->setIcon(UIIconPool::iconSet(":/arrow_right_10px.png"));
                connect(m_pNextButton, &QIArrowButtonPress::sigClicked, this, &QIArrowSplitter::sltSwitchDetailsPageNext);

                /* Add next-button into button-layout: */
                pButtonLayout->addWidget(m_pNextButton);
            }
            /* Add button layout into main-layout: */
            m_pMainLayout->addLayout(pButtonLayout);
            /* Update navigation-buttons visibility: */
            sltUpdateNavigationButtonsVisibility();
        }
        /* Create details-browser: */
        m_pDetailsBrowser = new QIDetailsBrowser;
        AssertPtrReturnVoid(m_pDetailsBrowser);
        {
            /* Add details-browser into main-layout: */
            m_pMainLayout->addWidget(m_pDetailsBrowser);
            /* Update details-browser visibility: */
            sltUpdateDetailsBrowserVisibility();
            /* Update details: */
            updateDetails();
        }
    }

    /* Apply size-policy finally: */
    setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding);
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &QIArrowSplitter::sltRetranslateUI);
}

void QIArrowSplitter::updateDetails()
{
    /* If details are empty: */
    if (m_details.isEmpty())
    {
        /* Make sure details-list index is invalid: */
        AssertReturnVoid(m_iDetailsIndex == -1);

        /* Reset name: */
        setName(QString());
    }
    /* If details are NOT empty: */
    else
    {
        /* Make sure details-list index feats the bounds: */
        AssertReturnVoid(m_iDetailsIndex >= 0 && m_iDetailsIndex < m_details.size());

        /* Single page: */
        if (m_details.size() == 1)
        {
            setName(tr("&Details"));
            m_pBackButton->setEnabled(false);
            m_pNextButton->setEnabled(false);
        }
        /* Multi-paging: */
        else if (m_details.size() > 1)
        {
            setName(tr("&Details (%1 of %2)").arg(m_iDetailsIndex + 1).arg(m_details.size()));
            m_pBackButton->setEnabled(m_iDetailsIndex > 0);
            m_pNextButton->setEnabled(m_iDetailsIndex < m_details.size() - 1);
        }

        /* Update details-browser: */
        const QString strFirstPart = m_details[m_iDetailsIndex].first;
        const QString strSecondPart = m_details[m_iDetailsIndex].second;
        if (strFirstPart.isEmpty())
            m_pDetailsBrowser->setText(strSecondPart);
        else
            m_pDetailsBrowser->setText(QString("%1<br>%2").arg(strFirstPart, strSecondPart));
    }
    /* Update size-hints: */
    sltUpdateSizeHints();
}


#include "QIArrowSplitter.moc"
