/* $Id: UIPopupStackViewport.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIPopupStackViewport class implementation.
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

/* GUI includes: */
#include "UIPopupPane.h"
#include "UIPopupStackViewport.h"

/* Other VBox includes: */
#include <iprt/assert.h>


UIPopupStackViewport::UIPopupStackViewport()
    : m_iLayoutMargin(1)
    , m_iLayoutSpacing(1)
{
}

bool UIPopupStackViewport::exists(const QString &strID) const
{
    /* Is there already popup-pane with the same ID? */
    return m_panes.contains(strID);
}

void UIPopupStackViewport::createPopupPane(const QString &strID,
                                           const QString &strMessage, const QString &strDetails,
                                           const QMap<int, QString> &buttonDescriptions)
{
    /* Make sure there is no such popup-pane already: */
    if (m_panes.contains(strID))
    {
        AssertMsgFailed(("Popup-pane already exists!"));
        return;
    }

    /* Create new popup-pane: */
    UIPopupPane *pPopupPane = m_panes[strID] = new UIPopupPane(this,
                                                                        strMessage, strDetails,
                                                                        buttonDescriptions);

    /* Attach popup-pane connection: */
    connect(this, &UIPopupStackViewport::sigProposePopupPaneSize, pPopupPane, &UIPopupPane::sltHandleProposalForSize);
    connect(pPopupPane, &UIPopupPane::sigSizeHintChanged, this, &UIPopupStackViewport::sltAdjustGeometry);
    connect(pPopupPane, &UIPopupPane::sigDone, this, &UIPopupStackViewport::sltPopupPaneDone);

    /* Show popup-pane: */
    pPopupPane->show();
}

void UIPopupStackViewport::updatePopupPane(const QString &strID,
                                           const QString &strMessage, const QString &strDetails)
{
    /* Make sure there is such popup-pane already: */
    if (!m_panes.contains(strID))
    {
        AssertMsgFailed(("Popup-pane doesn't exists!"));
        return;
    }

    /* Get existing popup-pane: */
    UIPopupPane *pPopupPane = m_panes[strID];

    /* Update message and details: */
    pPopupPane->setMessage(strMessage);
    pPopupPane->setDetails(strDetails);
}

void UIPopupStackViewport::recallPopupPane(const QString &strID)
{
    /* Make sure there is such popup-pane already: */
    if (!m_panes.contains(strID))
    {
        AssertMsgFailed(("Popup-pane doesn't exists!"));
        return;
    }

    /* Get existing popup-pane: */
    UIPopupPane *pPopupPane = m_panes[strID];

    /* Recall popup-pane: */
    pPopupPane->recall();
}

void UIPopupStackViewport::sltHandleProposalForSize(QSize newSize)
{
    /* Subtract layout margins: */
    newSize.setWidth(newSize.width() - 2 * m_iLayoutMargin);
    newSize.setHeight(newSize.height() - 2 * m_iLayoutMargin);

    /* Propagate resulting size to popups: */
    emit sigProposePopupPaneSize(newSize);
}

void UIPopupStackViewport::sltAdjustGeometry()
{
    /* Update size-hint: */
    updateSizeHint();

    /* Layout content: */
    layoutContent();

    /* Notify parent popup-stack: */
    emit sigSizeHintChanged();
}

void UIPopupStackViewport::sltPopupPaneDone(int iResultCode)
{
    /* Make sure the sender is the popup-pane: */
    UIPopupPane *pPopupPane = qobject_cast<UIPopupPane*>(sender());
    if (!pPopupPane)
    {
        AssertMsgFailed(("Should be called by popup-pane only!"));
        return;
    }

    /* Make sure the popup-pane still exists: */
    const QString strID(m_panes.key(pPopupPane, QString()));
    if (strID.isNull())
    {
        AssertMsgFailed(("Popup-pane already destroyed!"));
        return;
    }

    /* Notify listeners about popup-pane removal: */
    emit sigPopupPaneDone(strID, iResultCode);

    /* Delete popup-pane asyncronously.
     * To avoid issues with events which already posted: */
    m_panes.remove(strID);
    pPopupPane->deleteLater();

    /* Notify listeners about popup-pane removed: */
    emit sigPopupPaneRemoved(strID);

    /* Adjust geometry: */
    sltAdjustGeometry();

    /* Make sure this stack still contains popup-panes: */
    if (!m_panes.isEmpty())
        return;

    /* Notify listeners about popup-stack: */
    emit sigPopupPanesRemoved();
}

void UIPopupStackViewport::updateSizeHint()
{
    /* Calculate minimum width-hint: */
    int iMinimumWidthHint = 0;
    {
        /* Take into account all the panes: */
        foreach (UIPopupPane *pPane, m_panes)
            iMinimumWidthHint = qMax(iMinimumWidthHint, pPane->minimumSizeHint().width());

        /* And two margins finally: */
        iMinimumWidthHint += 2 * m_iLayoutMargin;
    }

    /* Calculate minimum height-hint: */
    int iMinimumHeightHint = 0;
    {
        /* Take into account all the panes: */
        foreach (UIPopupPane *pPane, m_panes)
            iMinimumHeightHint += pPane->minimumSizeHint().height();

        /* Take into account all the spacings, if any: */
        if (!m_panes.isEmpty())
            iMinimumHeightHint += (m_panes.size() - 1) * m_iLayoutSpacing;

        /* And two margins finally: */
        iMinimumHeightHint += 2 * m_iLayoutMargin;
    }

    /* Compose minimum size-hint: */
    m_minimumSizeHint = QSize(iMinimumWidthHint, iMinimumHeightHint);
}

void UIPopupStackViewport::layoutContent()
{
    /* Get attributes: */
    int iX = m_iLayoutMargin;
    int iY = m_iLayoutMargin;

    /* Layout every pane we have: */
    foreach (UIPopupPane *pPane, m_panes)
    {
        /* Get pane attributes: */
        QSize paneSize = pPane->minimumSizeHint();
        const int iPaneWidth = paneSize.width();
        const int iPaneHeight = paneSize.height();
        /* Adjust geometry for the pane: */
        pPane->setGeometry(iX, iY, iPaneWidth, iPaneHeight);
        pPane->layoutContent();
        /* Increment placeholder: */
        iY += (iPaneHeight + m_iLayoutSpacing);
    }
}
