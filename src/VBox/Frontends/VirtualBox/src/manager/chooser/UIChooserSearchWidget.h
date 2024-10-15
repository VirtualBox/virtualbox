/* $Id: UIChooserSearchWidget.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIChooserSearchWidget class declaration.
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

#ifndef FEQT_INCLUDED_SRC_manager_chooser_UIChooserSearchWidget_h
#define FEQT_INCLUDED_SRC_manager_chooser_UIChooserSearchWidget_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QWidget>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QHBoxLayout;
class QILineEdit;
class QIToolButton;
class UISearchLineEdit;

/** QWidget extension used as virtual machine search widget in the VM Chooser-pane. */
class UIChooserSearchWidget : public QWidget
{
    Q_OBJECT;

signals:

    void sigRedoSearch(const QString &strSearchTerm, int iSearchFlags);
    /** Is being signalled as next/prev tool buttons are pressed. @a fIsNext is true
      * for the next and false for the previous case. */
    void sigScrollToMatch(bool fIsNext);
    /** Is used for signalling show/hide event from this to parent. */
    void sigToggleVisibility(bool fIsVisible);

public:

    UIChooserSearchWidget(QWidget *pParent);
    /** Forward @a iMatchCount to UISearchLineEdit. */
    void setMatchCount(int iMatchCount);
    /** Forward @a iScrollToIndex to UISearchLineEdit. */
    void setScroolToIndex(int iScrollToIndex);
    /** Appends the @a strSearchText to the current (if any) search text. */
    void appendToSearchString(const QString &strSearchText);
    /** Repeats the last search again. */
    void redoSearch();

protected:

    virtual void showEvent(QShowEvent *pEvent) RT_OVERRIDE;
    virtual void hideEvent(QHideEvent *pEvent) RT_OVERRIDE;
    virtual bool eventFilter(QObject *pWatched, QEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Emits sigRedoSearch thuse causes a re-search. */
    void sltHandleSearchTermChange(const QString &strSearchTerm);
    void sltHandleScroolToButtonClick();
    /** Emits sigToggleVisibility, */
    void sltHandleCloseButtonClick();
    void sltRetranslateUI();

private:

    void prepareWidgets();
    void prepareConnections();

    /** @name Member widgets.
      * @{ */
        UISearchLineEdit  *m_pLineEdit;
        QHBoxLayout       *m_pMainLayout;
        QIToolButton      *m_pScrollToNextMatchButton;
        QIToolButton      *m_pScrollToPreviousMatchButton;
        QIToolButton      *m_pCloseButton;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_manager_chooser_UIChooserSearchWidget_h */
