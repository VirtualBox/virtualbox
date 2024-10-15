/* $Id: QIDialogContainer.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - Qt extensions: QIDialogContainer class declaration.
 */

/*
 * Copyright (C) 2019-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_extensions_QIDialogContainer_h
#define FEQT_INCLUDED_SRC_extensions_QIDialogContainer_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QDialog>

/* GUI includes: */
#include "UILibraryDefs.h"

/* Forward declarations: */
class QGridLayout;
class QLabel;
class QProgressBar;
class QWidget;
class QIDialogButtonBox;

/** QDialog sub-class used as executable input container for passed widget.
  * Should be used as popup or modal dialog wrapping functionality of the passed widget. */
class SHARED_LIBRARY_STUFF QIDialogContainer : public QDialog
{
    Q_OBJECT;

public:

    /** Constructs QIDialogContainer passing @a pParent & @a enmFlags to the base-class. */
    QIDialogContainer(QWidget *pParent = 0, Qt::WindowFlags enmFlags = Qt::WindowFlags());

    /** Defines containing @a pWidget. */
    void setWidget(QWidget *pWidget);

public slots:

    /** Activates window. */
    void sltActivateWindow() { activateWindow(); }

    /** Sets progress-bar to be @a fHidden. */
    void setProgressBarHidden(bool fHidden);

    /** Sets Ok button to be @a fEnabled. */
    void setOkButtonEnabled(bool fEnabled);

private slots:

    /** Handles translation event. */
    void sltRetranslateUI();

private:

    /** Prepares all. */
    void prepare();

    /** Holds the layout instance. */
    QGridLayout       *m_pLayout;
    /** Holds the widget reference. */
    QWidget           *m_pWidget;
    /** Holds the progress-bar instance. */
    QLabel            *m_pProgressLabel;
    /** Holds the progress-bar instance. */
    QProgressBar      *m_pProgressBar;
    /** Holds the button-box instance. */
    QIDialogButtonBox *m_pButtonBox;
};

#endif /* !FEQT_INCLUDED_SRC_extensions_QIDialogContainer_h */
