/* $Id: UIToolsView.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIToolsView class declaration.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_manager_tools_UIToolsView_h
#define FEQT_INCLUDED_SRC_manager_tools_UIToolsView_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "QIGraphicsView.h"

/* Forward declarations: */
class UITools;

/** QIGraphicsView extension used as VM Tools-pane view. */
class UIToolsView : public QIGraphicsView
{
    Q_OBJECT;

signals:

    /** Notifies listeners about resize. */
    void sigResized();

public:

    /** Constructs a Tools-view passing @a pParent to the base-class.
      * @param  pParent  Brings the Tools-container to embed into. */
    UIToolsView(UITools *pParent);

    /** @name General stuff.
      * @{ */
        /** Returns the Tools reference. */
        UITools *tools() const { return m_pTools; }
    /** @} */

public slots:

    /** @name General stuff.
      * @{ */
        /** Handles focus change to @a pFocusItem. */
        void sltFocusChanged();
    /** @} */

    /** @name Layout stuff.
      * @{ */
        /** Handles minimum width @a iHint change. */
        void sltMinimumWidthHintChanged(int iHint);
        /** Handles minimum height @a iHint change. */
        void sltMinimumHeightHintChanged(int iHint);
    /** @} */

protected:

    /** @name Event handling stuff.
      * @{ */
        /** Handles resize @a pEvent. */
        virtual void resizeEvent(QResizeEvent *pEvent) RT_OVERRIDE;
    /** @} */

private slots:

    /** @name Event handling stuff.
      * @{ */
       /** Handles translation event. */
       void sltRetranslateUI();
    /** @} */

private:

    /** @name Prepare/Cleanup cascade.
      * @{ */
        /** Prepares all. */
        void prepare();
        /** Prepares palette. */
        void preparePalette();
    /** @} */

    /** @name General stuff.
      * @{ */
        /** Updates scene rectangle. */
        void updateSceneRect();
    /** @} */

    /** @name General stuff.
      * @{ */
        /** Holds the Tools-pane reference. */
        UITools *m_pTools;
    /** @} */

    /** @name Layout stuff.
      * @{ */
        /** Holds the minimum width hint. */
        int m_iMinimumWidthHint;
        /** Holds the minimum height hint. */
        int m_iMinimumHeightHint;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_manager_tools_UIToolsView_h */
