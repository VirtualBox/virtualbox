/* $Id: UIVMLogViewerFilterWidget.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIVMLogViewer class declaration.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_logviewer_UIVMLogViewerFilterWidget_h
#define FEQT_INCLUDED_SRC_logviewer_UIVMLogViewerFilterWidget_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
# include <QSet>

/* GUI includes: */
#include "UIVMLogViewerPanel.h"

/* Forward declarations: */
class QAbstractButton;
class QButtonGroup;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QVBoxLayout;
class QIToolButton;
class QRadioButton;
class UIVMFilterLineEdit;


/** QWidget extension
  * providing GUI for filter panel in VM Log Viewer. */
class UIVMLogViewerFilterWidget : public UIVMLogViewerPane
{
    Q_OBJECT;

signals:

    void sigFilterApplied();

public:

    /** Constructs the filter-panel by passing @a pParent to the QWidget base-class constructor.
      * @param  pViewer  Specifies reference to the VM Log-Viewer this filter-panel belongs to. */
    UIVMLogViewerFilterWidget(QWidget *pParent, UIVMLogViewerWidget *pViewer);

public slots:

    /** Applies filter settings and filters the current log-page. */
    void applyFilter();

protected:

    virtual void prepareWidgets() RT_OVERRIDE;
    virtual void prepareConnections() RT_OVERRIDE;

    /** Handles Qt @a pEvent, used for keyboard processing. */
    bool eventFilter(QObject *pObject, QEvent *pEvent) RT_OVERRIDE;
    void showEvent(QShowEvent *pEvent) RT_OVERRIDE;
    void hideEvent(QHideEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Adds the new filter term and reapplies the filter. */
    void sltAddFilterTerm();
    /** Clear all the filter terms and reset the filtering. */
    void sltClearFilterTerms();
    /** Executes the necessary code to handle filter's boolean operator change ('And', 'Or'). */
    void sltOperatorButtonChanged(QAbstractButton *pButton);
    void sltRemoveFilterTerm(const QString &termString);
    void sltRetranslateUI();

private:

    enum FilterOperatorButton{
        AndButton = 0,/* Don't change this value */
        OrButton,
        ButtonEnd
    };

    void prepareRadioButtonGroup(QVBoxLayout *pLayout);

    bool applyFilterTermsToString(const QString& string);
    void filter();
    /** Revert the document to original. */
    void resetFiltering();

    QLabel              *m_pFilterLabel;
    QComboBox           *m_pFilterComboBox;
    QButtonGroup        *m_pButtonGroup;
    QRadioButton        *m_pAndRadioButton;
    QRadioButton        *m_pOrRadioButton;
    QFrame              *m_pRadioButtonContainer;
    QIToolButton        *m_pAddFilterTermButton;
    QSet<QString>        m_filterTermSet;
    FilterOperatorButton m_eFilterOperatorButton;
    UIVMFilterLineEdit  *m_pFilterTermsLineEdit;
    QLabel              *m_pResultLabel;
    int                  m_iUnfilteredLineCount;
    int                  m_iFilteredLineCount;
};

#endif /* !FEQT_INCLUDED_SRC_logviewer_UIVMLogViewerFilterWidget_h */
