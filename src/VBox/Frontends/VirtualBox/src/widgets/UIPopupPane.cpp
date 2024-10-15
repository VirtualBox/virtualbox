/* $Id: UIPopupPane.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIPopupPane class implementation.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
#include <QPainter>
#include <QPainterPath>
#include <QTextEdit>

/* GUI includes: */
#include "QIMessageBox.h"
#include "UIAnimationFramework.h"
#include "UIPopupPane.h"
#include "UIPopupPaneMessage.h"
#include "UIPopupPaneDetails.h"
#include "UIPopupPaneButtonPane.h"
#include "UITranslationEventListener.h"

/* Other VBox includes: */
#include <iprt/assert.h>


UIPopupPane::UIPopupPane(QWidget *pParent,
                         const QString &strMessage, const QString &strDetails,
                         const QMap<int, QString> &buttonDescriptions)
    : QWidget(pParent)
    , m_fPolished(false)
    , m_iLayoutMargin(10), m_iLayoutSpacing(5)
    , m_strMessage(strMessage), m_strDetails(strDetails)
    , m_buttonDescriptions(buttonDescriptions)
    , m_fShown(false)
    , m_pShowAnimation(0)
    , m_fCanLooseFocus(!m_buttonDescriptions.isEmpty())
    , m_fFocused(!m_fCanLooseFocus)
    , m_fHovered(m_fFocused)
    , m_iDefaultOpacity(180)
    , m_iHoveredOpacity(250)
    , m_iOpacity(m_fHovered ? m_iHoveredOpacity : m_iDefaultOpacity)
    , m_pMessagePane(0), m_pDetailsPane(0), m_pButtonPane(0)
{
    /* Prepare: */
    prepare();
}

void UIPopupPane::recall()
{
    /* Close popup-pane with *escape* button: */
    done(m_pButtonPane->escapeButton());
}

void UIPopupPane::setMessage(const QString &strMessage)
{
    /* Make sure the message has changed: */
    if (m_strMessage == strMessage)
        return;

    /* Fetch new message: */
    m_strMessage = strMessage;
    m_pMessagePane->setText(m_strMessage);
}

void UIPopupPane::setDetails(const QString &strDetails)
{
    /* Make sure the details has changed: */
    if (m_strDetails == strDetails)
        return;

    /* Fetch new details: */
    m_strDetails = strDetails;
    m_pDetailsPane->setText(prepareDetailsText());
}

void UIPopupPane::setMinimumSizeHint(const QSize &minimumSizeHint)
{
    /* Make sure the size-hint has changed: */
    if (m_minimumSizeHint == minimumSizeHint)
        return;

    /* Fetch new size-hint: */
    m_minimumSizeHint = minimumSizeHint;

    /* Notify parent popup-stack: */
    emit sigSizeHintChanged();
}

void UIPopupPane::layoutContent()
{
    /* Variables: */
    const int iWidth = width();
    const int iHeight = height();
    const QSize buttonPaneMinimumSizeHint = m_pButtonPane->minimumSizeHint();
    const int iButtonPaneMinimumWidth = buttonPaneMinimumSizeHint.width();
    const int iButtonPaneMinimumHeight = buttonPaneMinimumSizeHint.height();
    const int iTextPaneWidth = iWidth - 2 * m_iLayoutMargin - m_iLayoutSpacing - iButtonPaneMinimumWidth;
    const int iTextPaneHeight = m_pMessagePane->minimumSizeHint().height();
    const int iMaximumHeight = qMax(iTextPaneHeight, iButtonPaneMinimumHeight);
    const int iMinimumHeight = qMin(iTextPaneHeight, iButtonPaneMinimumHeight);
    const int iHeightShift = (iMaximumHeight - iMinimumHeight) / 2;
    const bool fTextPaneShifted = iTextPaneHeight < iButtonPaneMinimumHeight;
    const int iTextPaneYOffset = fTextPaneShifted ? m_iLayoutMargin + iHeightShift : m_iLayoutMargin;

    /* Message-pane: */
    m_pMessagePane->move(m_iLayoutMargin, iTextPaneYOffset);
    m_pMessagePane->resize(iTextPaneWidth, iTextPaneHeight);
    m_pMessagePane->layoutContent();

    /* Button-pane: */
    m_pButtonPane->move(m_iLayoutMargin + iTextPaneWidth + m_iLayoutSpacing,
                        m_iLayoutMargin);
    m_pButtonPane->resize(iButtonPaneMinimumWidth,
                          iHeight - m_iLayoutSpacing);

    /* Details-pane: */
    if (m_pDetailsPane->isVisible())
    {
        m_pDetailsPane->move(m_iLayoutMargin,
                             iTextPaneYOffset + iTextPaneHeight + m_iLayoutSpacing);
        m_pDetailsPane->resize(iTextPaneWidth + iButtonPaneMinimumWidth,
                               m_pDetailsPane->minimumSizeHint().height());
        m_pDetailsPane->layoutContent();
    }
}

void UIPopupPane::sltMarkAsShown()
{
    /* Mark popup-pane as 'shown': */
    m_fShown = true;
}

void UIPopupPane::sltHandleProposalForSize(QSize newSize)
{
    /* Prepare the width: */
    int iWidth = newSize.width();

    /* Subtract layout margins: */
    iWidth -= 2 * m_iLayoutMargin;
    /* Subtract layout spacing: */
    iWidth -= m_iLayoutSpacing;
    /* Subtract button-pane width: */
    iWidth -= m_pButtonPane->minimumSizeHint().width();

    /* Propose resulting width to the panes: */
    emit sigProposePaneWidth(iWidth);

    /* Prepare the height: */
    int iHeight = newSize.height();
    /* Determine maximum height of the message-pane / button-pane: */
    int iExtraHeight = qMax(m_pMessagePane->expandedSizeHint().height(),
                            m_pButtonPane->minimumSizeHint().height());

    /* Subtract height of the message pane: */
    iHeight -= iExtraHeight;
    /* Subtract layout margins: */
    iHeight -= 2 * m_iLayoutMargin;
    /* Subtract layout spacing: */
    iHeight -= m_iLayoutSpacing;

    /* Propose resulting height to details-pane: */
    emit sigProposeDetailsPaneHeight(iHeight);
}

void UIPopupPane::sltUpdateSizeHint()
{
    /* Calculate minimum width-hint: */
    int iMinimumWidthHint = 0;
    {
        /* Take into account layout: */
        iMinimumWidthHint += 2 * m_iLayoutMargin;
        {
            /* Take into account widgets: */
            iMinimumWidthHint += m_pMessagePane->minimumSizeHint().width();
            iMinimumWidthHint += m_iLayoutSpacing;
            iMinimumWidthHint += m_pButtonPane->minimumSizeHint().width();
        }
    }

    /* Calculate minimum height-hint: */
    int iMinimumHeightHint = 0;
    {
        /* Take into account layout: */
        iMinimumHeightHint += 2 * m_iLayoutMargin;
        iMinimumHeightHint += m_iLayoutSpacing;
        {
            /* Take into account widgets: */
            const int iTextPaneHeight = m_pMessagePane->minimumSizeHint().height();
            const int iButtonBoxHeight = m_pButtonPane->minimumSizeHint().height();
            iMinimumHeightHint += qMax(iTextPaneHeight, iButtonBoxHeight);
            /* Add the height of details-pane only if it is visible: */
            if (m_pDetailsPane->isVisible())
                iMinimumHeightHint += m_pDetailsPane->minimumSizeHint().height();
        }
    }

    /* Compose minimum size-hints: */
    m_hiddenSizeHint = QSize(iMinimumWidthHint, 1);
    m_shownSizeHint = QSize(iMinimumWidthHint, iMinimumHeightHint);
    m_minimumSizeHint = m_fShown ? m_shownSizeHint : m_hiddenSizeHint;

    /* Update 'show/hide' animation: */
    if (m_pShowAnimation)
        m_pShowAnimation->update();

    /* Notify parent popup-stack: */
    emit sigSizeHintChanged();
}

void UIPopupPane::sltButtonClicked(int iButtonID)
{
    /* Complete popup with corresponding code: */
    done(iButtonID);
}

void UIPopupPane::prepare()
{
    /* Prepare this: */
    installEventFilter(this);
    /* Prepare background: */
    prepareBackground();
    /* Prepare content: */
    prepareContent();
    /* Prepare animation: */
    prepareAnimation();

    /* Update size-hint: */
    sltUpdateSizeHint();
}

void UIPopupPane::prepareBackground()
{
    /* Prepare palette: */
    QPalette pal = QApplication::palette();
    pal.setColor(QPalette::Window, QApplication::palette().color(QPalette::Window));
    setPalette(pal);
}

void UIPopupPane::prepareContent()
{
    /* Create message-pane: */
    m_pMessagePane = new UIPopupPaneMessage(this, m_strMessage, m_fFocused);
    {
        /* Configure message-pane: */
        connect(this, &UIPopupPane::sigProposePaneWidth, m_pMessagePane, &UIPopupPaneMessage::sltHandleProposalForWidth);
        connect(m_pMessagePane, &UIPopupPaneMessage::sigSizeHintChanged, this, &UIPopupPane::sltUpdateSizeHint);
        m_pMessagePane->installEventFilter(this);
    }

    /* Create button-box: */
    m_pButtonPane = new UIPopupPaneButtonPane(this);
    {
        /* Configure button-box: */
        connect(m_pButtonPane, &UIPopupPaneButtonPane::sigButtonClicked, this, &UIPopupPane::sltButtonClicked);
        m_pButtonPane->installEventFilter(this);
        m_pButtonPane->setButtons(m_buttonDescriptions);
    }

    /* Create details-pane: */
    m_pDetailsPane = new UIPopupPaneDetails(this, prepareDetailsText(), m_fFocused);
    {
        /* Configure details-pane: */
        connect(this, &UIPopupPane::sigProposePaneWidth,         m_pDetailsPane, &UIPopupPaneDetails::sltHandleProposalForWidth);
        connect(this, &UIPopupPane::sigProposeDetailsPaneHeight, m_pDetailsPane, &UIPopupPaneDetails::sltHandleProposalForHeight);
        connect(m_pDetailsPane, &UIPopupPaneDetails::sigSizeHintChanged, this, &UIPopupPane::sltUpdateSizeHint);
        m_pDetailsPane->installEventFilter(this);
    }

    /* Prepare focus rules: */
    setFocusPolicy(Qt::StrongFocus);
    m_pMessagePane->setFocusPolicy(Qt::StrongFocus);
    m_pButtonPane->setFocusPolicy(Qt::StrongFocus);
    m_pDetailsPane->setFocusPolicy(Qt::StrongFocus);
    setFocusProxy(m_pButtonPane);
    m_pMessagePane->setFocusProxy(m_pButtonPane);
    m_pDetailsPane->setFocusProxy(m_pButtonPane);

    /* Translate UI finally: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIPopupPane::sltRetranslateUI);
}

void UIPopupPane::prepareAnimation()
{
    /* Install 'show' animation for 'minimumSizeHint' property: */
    connect(this, SIGNAL(sigToShow()), this, SIGNAL(sigShow()), Qt::QueuedConnection);
    m_pShowAnimation = UIAnimation::installPropertyAnimation(this, "minimumSizeHint", "hiddenSizeHint", "shownSizeHint",
                                                             SIGNAL(sigShow()), SIGNAL(sigHide()));
    connect(m_pShowAnimation, &UIAnimation::sigStateEnteredFinal, this, &UIPopupPane::sltMarkAsShown);

    /* Install 'hover' animation for 'opacity' property: */
    UIAnimation::installPropertyAnimation(this, "opacity", "defaultOpacity", "hoveredOpacity",
                                          SIGNAL(sigHoverEnter()), SIGNAL(sigHoverLeave()), m_fHovered);
}

void UIPopupPane::sltRetranslateUI()
{
    /* Translate tool-tips: */
    retranslateToolTips();
}

void UIPopupPane::retranslateToolTips()
{
    /* Translate pane & message-pane tool-tips: */
    if (m_fFocused)
    {
        setToolTip(QString());
        m_pMessagePane->setToolTip(QString());
    }
    else
    {
        setToolTip(QApplication::translate("UIPopupCenter", "Click for full details"));
        m_pMessagePane->setToolTip(QApplication::translate("UIPopupCenter", "Click for full details"));
    }
}

bool UIPopupPane::eventFilter(QObject *pObject, QEvent *pEvent)
{
    /* Handle events for allowed widgets only: */
    if (   pObject != this
        && pObject != m_pMessagePane
        && pObject != m_pButtonPane
        && pObject != m_pDetailsPane)
        return QWidget::eventFilter(pObject, pEvent);

    /* Depending on event-type: */
    switch (pEvent->type())
    {
        /* Something is hovered: */
        case QEvent::HoverEnter:
        case QEvent::Enter:
        {
            /* Hover pane if not yet hovered: */
            if (!m_fHovered)
            {
                m_fHovered = true;
                emit sigHoverEnter();
            }
            break;
        }
        /* Nothing is hovered: */
        case QEvent::Leave:
        {
            /* Unhover pane if hovered but not focused: */
            if (pObject == this && m_fHovered && !m_fFocused)
            {
                m_fHovered = false;
                emit sigHoverLeave();
            }
            break;
        }
        /* Pane is clicked with mouse: */
        case QEvent::MouseButtonPress:
        {
            /* Focus pane if not focused: */
            if (!m_fFocused)
            {
                m_fFocused = true;
                emit sigFocusEnter();
                /* Hover pane if not hovered: */
                if (!m_fHovered)
                {
                    m_fHovered = true;
                    emit sigHoverEnter();
                }
                /* Translate tool-tips: */
                retranslateToolTips();
            }
            break;
        }
        /* Pane is unfocused: */
        case QEvent::FocusOut:
        {
            /* Unhocus pane if focused: */
            if (m_fCanLooseFocus && m_fFocused)
            {
                m_fFocused = false;
                emit sigFocusLeave();
                /* Unhover pane if hovered: */
                if (m_fHovered)
                {
                    m_fHovered = false;
                    emit sigHoverLeave();
                }
                /* Translate tool-tips: */
                retranslateToolTips();
            }
            break;
        }
        /* Default case: */
        default: break;
    }

    /* Call to base-class: */
    return QWidget::eventFilter(pObject, pEvent);
}

void UIPopupPane::showEvent(QShowEvent *pEvent)
{
    /* Call to base-class: */
    QWidget::showEvent(pEvent);

    /* Polish border: */
    if (m_fPolished)
        return;
    m_fPolished = true;

    /* Call to polish event: */
    polishEvent(pEvent);
}

void UIPopupPane::polishEvent(QShowEvent *)
{
    /* Focus if marked as 'focused': */
    if (m_fFocused)
        setFocus();

    /* Emit signal to start *show* animation: */
    emit sigToShow();
}

void UIPopupPane::paintEvent(QPaintEvent *)
{
    /* Compose painting rectangle,
     * Shifts are required for the antialiasing support: */
    const QRect rect(1, 1, width() - 2, height() - 2);

    /* Create painter: */
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    /* Configure clipping: */
    configureClipping(rect, painter);

    /* Paint background: */
    paintBackground(rect, painter);

    /* Paint frame: */
    paintFrame(painter);
}

void UIPopupPane::configureClipping(const QRect &rect, QPainter &painter)
{
    /* Configure clipping: */
    QPainterPath path;
    int iDiameter = 6;
    QSizeF arcSize(2 * iDiameter, 2 * iDiameter);
    path.moveTo(rect.x() + iDiameter, rect.y());
    path.arcTo(QRectF(path.currentPosition(), arcSize).translated(-iDiameter, 0), 90, 90);
    path.lineTo(path.currentPosition().x(), rect.y() + rect.height() - iDiameter);
    path.arcTo(QRectF(path.currentPosition(), arcSize).translated(0, -iDiameter), 180, 90);
    path.lineTo(rect.x() + rect.width() - iDiameter, path.currentPosition().y());
    path.arcTo(QRectF(path.currentPosition(), arcSize).translated(-iDiameter, -2 * iDiameter), 270, 90);
    path.lineTo(path.currentPosition().x(), rect.y() + iDiameter);
    path.arcTo(QRectF(path.currentPosition(), arcSize).translated(-2 * iDiameter, -iDiameter), 0, 90);
    path.closeSubpath();
    painter.setClipPath(path);
}

void UIPopupPane::paintBackground(const QRect &rect, QPainter &painter)
{
    /* Paint background: */
    QColor currentColor(palette().color(QPalette::Window));
    QColor newColor1(currentColor.red(), currentColor.green(), currentColor.blue(), opacity());
    QColor newColor2 = newColor1.darker(115);
    QLinearGradient headerGradient(rect.topLeft(), rect.bottomLeft());
    headerGradient.setColorAt(0, newColor1);
    headerGradient.setColorAt(1, newColor2);
    painter.fillRect(rect, headerGradient);
}

void UIPopupPane::paintFrame(QPainter &painter)
{
    /* Paint frame: */
    QColor currentColor(palette().color(QPalette::Window).darker(150));
    QPainterPath path = painter.clipPath();
    painter.setClipping(false);
    painter.strokePath(path, currentColor);
}

void UIPopupPane::done(int iResultCode)
{
    /* Notify listeners: */
    emit sigDone(iResultCode);
}

QString UIPopupPane::prepareDetailsText() const
{
    if (m_strDetails.isEmpty())
        return QString();

    QStringPairList aDetailsList;
    prepareDetailsList(aDetailsList);
    if (aDetailsList.isEmpty())
        return QString();

    if (aDetailsList.size() == 1)
        return tr("<p><b>Details:</b>") + m_strDetails + "</p>";

    QString strResultText;
    for (int iListIdx = 0; iListIdx < aDetailsList.size(); ++iListIdx)
    {
        strResultText += tr("<p><b>Details:</b> (%1 of %2)").arg(iListIdx + 1).arg(aDetailsList.size());
        const QString strFirstPart = aDetailsList.at(iListIdx).first;
        const QString strSecondPart = aDetailsList.at(iListIdx).second;
        if (strFirstPart.isEmpty())
            strResultText += strSecondPart + "</p>";
        else
            strResultText += QString("%1<br>%2").arg(strFirstPart, strSecondPart) + "</p>";
    }
    return strResultText;
}

void UIPopupPane::prepareDetailsList(QStringPairList &aDetailsList) const
{
    if (m_strDetails.isEmpty())
        return;

    /* Split details into paragraphs: */
    const QStringList aParagraphs(m_strDetails.split("<!--EOP-->", Qt::SkipEmptyParts));
    /* Make sure details-text has at least one paragraph: */
    AssertReturnVoid(!aParagraphs.isEmpty());

    /* Enumerate all the paragraphs: */
    foreach (const QString &strParagraph, aParagraphs)
    {
        /* Split each paragraph into pairs: */
        const QStringList aParts(strParagraph.split("<!--EOM-->", Qt::KeepEmptyParts));
        /* Make sure each paragraph consist of 2 parts: */
        AssertReturnVoid(aParts.size() == 2);
        /* Append each pair into details-list: */
        aDetailsList << QStringPair(aParts.at(0), aParts.at(1));
    }
}
