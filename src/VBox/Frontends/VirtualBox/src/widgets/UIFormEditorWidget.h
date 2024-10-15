/* $Id: UIFormEditorWidget.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIFormEditorWidget class declaration.
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

#ifndef FEQT_INCLUDED_SRC_widgets_UIFormEditorWidget_h
#define FEQT_INCLUDED_SRC_widgets_UIFormEditorWidget_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QPointer>
#include <QWidget>

/* COM includes: */
#include "CFormValue.h"

/* Forward declarations: */
class QHeaderView;
class QITableView;
class UIFormEditorModel;
class UINotificationCenter;
class CForm;
class CVirtualSystemDescriptionForm;

/** QWidget subclass representing model/view Form Editor widget. */
class UIFormEditorWidget : public QWidget
{
    Q_OBJECT;

public:

    /** Constructs Form Editor widget passing @a pParent to the base-class.
      * @param  pNotificationCenter  Brings the notification-center this widget should report to. */
    UIFormEditorWidget(QWidget *pParent = 0, UINotificationCenter *pNotificationCenter = 0);

    /** Returns the notification-center reference. */
    UINotificationCenter *notificationCenter() const { return m_pNotificationCenter; }
    /** Defines the @a pNotificationCenter reference. */
    void setNotificationCenter(UINotificationCenter *pNotificationCenter) { m_pNotificationCenter = pNotificationCenter; }

    /** Returns table-view reference. */
    QITableView *view() const;
    /** Returns horizontal header reference. */
    QHeaderView *horizontalHeader() const;
    /** Returns vertical header reference. */
    QHeaderView *verticalHeader() const;
    /** Defines table-view @a strWhatsThis help text. */
    void setWhatsThis(const QString &strWhatsThis);

    /** Clears form. */
    void clearForm();
    /** Defines form @a values to be edited. */
    void setFormValues(const QVector<CFormValue> &values);
    /** Defines @a comForm to be edited. */
    void setForm(const CForm &comForm);
    /** Defines virtual system description @a comForm to be edited. */
    void setVirtualSystemDescriptionForm(const CVirtualSystemDescriptionForm &comForm);

    /** Makes sure current editor data committed. */
    void makeSureEditorDataCommitted();

private:

    /** Prepares all. */
    void prepare();

    /** Holds the notification-center reference. */
    UINotificationCenter *m_pNotificationCenter;

    /** Holds the table-view instance. */
    QITableView       *m_pTableView;
    /** Holds the table-model instance. */
    UIFormEditorModel *m_pTableModel;
};

/** Safe pointer to Form Editor widget. */
typedef QPointer<UIFormEditorWidget> UIFormEditorWidgetPointer;

#endif /* !FEQT_INCLUDED_SRC_widgets_UIFormEditorWidget_h */
