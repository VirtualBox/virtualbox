/* $Id: QIArrowSplitter.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIArrowSplitter class declaration.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QIArrowSplitter_h
#define FEQT_INCLUDED_SRC_extensions_QIArrowSplitter_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QVBoxLayout;
class QIArrowButtonSwitch;
class QIArrowButtonPress;
class QIDetailsBrowser;

/* Type definitions: */
typedef QPair<QString, QString> QStringPair;
typedef QList<QStringPair> QStringPairList;

/** QWidget extension
  * allowing to toggle visibility for any other child widget. */
class SHARED_LIBRARY_STUFF QIArrowSplitter : public QWidget
{
    Q_OBJECT;

signals:

    /** Notifies listeners about size-hint change. */
    void sigSizeHintChange();

public:

    /** Constructs arrow splitter passing @a pParent to the base-class. */
    QIArrowSplitter(QWidget *pParent = 0);

    /** Returns minimum size-hint. */
    QSize minimumSizeHint() const RT_OVERRIDE;

    /** Defines the @a strName for the switch-button. */
    void setName(const QString &strName);

    /** Returns splitter details. */
    const QStringPairList& details() const { return m_details; }
    /** Defines splitter @a details. */
    void setDetails(const QStringPairList &details);

public slots:

    /** Updates size-hints. */
    void sltUpdateSizeHints();

    /** Updates navigation-buttons visibility. */
    void sltUpdateNavigationButtonsVisibility();
    /** Updates details-browser visibility. */
    void sltUpdateDetailsBrowserVisibility();

    /** Navigates through details-list backward. */
    void sltSwitchDetailsPageBack();
    /** Navigates through details-list forward. */
    void sltSwitchDetailsPageNext();

private slots:

    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** Prepares all. */
    void prepare();

    /** Updates details. */
    void updateDetails();

    /** Holds the main-layout instance. */
    QVBoxLayout *m_pMainLayout;

    /** Holds the switch-button instance. */
    QIArrowButtonSwitch *m_pSwitchButton;
    /** Holds the back-button instance. */
    QIArrowButtonPress  *m_pBackButton;
    /** Holds the next-button instance. */
    QIArrowButtonPress  *m_pNextButton;

    /** Holds the details-browser. */
    QIDetailsBrowser *m_pDetailsBrowser;
    /** Holds details-list. */
    QStringPairList   m_details;
    /** Holds details-list index. */
    int               m_iDetailsIndex;
};

#endif /* !FEQT_INCLUDED_SRC_extensions_QIArrowSplitter_h */
