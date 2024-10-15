/* $Id: UIDetailsView.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDetailsView class declaration.
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

#ifndef FEQT_INCLUDED_SRC_manager_details_UIDetailsView_h
#define FEQT_INCLUDED_SRC_manager_details_UIDetailsView_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "QIGraphicsView.h"

/* Forward declarations: */
class UIDetails;

/* Graphics details-view: */
class UIDetailsView : public QIGraphicsView
{
    Q_OBJECT;

signals:

    /** Notifies listeners about resize. */
    void sigResized();

public:

    /** Constructs a details-view passing @a pParent to the base-class.
      * @param  pParent  Brings the details container to embed into. */
    UIDetailsView(UIDetails *pParent);

    /** Returns the details reference. */
    UIDetails *details() const { return m_pDetails; }

public slots:

    /** Handles minimum width @a iHint change. */
    void sltMinimumWidthHintChanged(int iHint);

protected:

    /** Handles resize @a pEvent. */
    virtual void resizeEvent(QResizeEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Updates palette. */
    void sltUpdatePalette() { preparePalette(); }

    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** Prepares all. */
    void prepare();
    /** Prepares this. */
    void prepareThis();
    /** Prepares palette. */
    void preparePalette();

    /** Updates scene rectangle. */
    void updateSceneRect();

    /** Holds the details reference. */
    UIDetails *m_pDetails;

    /** Updates scene rectangle. */
    int m_iMinimumWidthHint;
};

#endif /* !FEQT_INCLUDED_SRC_manager_details_UIDetailsView_h */
