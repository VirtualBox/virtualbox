/* $Id: UIStatusBarEditor.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIStatusBarEditor class declaration.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_settings_editors_UIStatusBarEditor_h
#define FEQT_INCLUDED_SRC_settings_editors_UIStatusBarEditor_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QList>
#include <QMap>
#include <QUuid>

/* GUI includes: */
#include "UIEditor.h"
#include "UIExtraDataDefs.h"
#include "UISlidingToolBar.h"

/* Forward declarations: */
class QCheckBox;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QHBoxLayout;
class QPaintEvent;
class QString;
class QWidget;
class QIToolButton;
class UIMachineWindow;
class UIStatusBarEditorButton;

/** UISlidingToolBar sub-class
  * providing user with possibility to edit status-bar layout. */
class SHARED_LIBRARY_STUFF UIStatusBarEditorWindow : public UISlidingToolBar
{
    Q_OBJECT;

public:

    /** Constructs sliding toolbar passing @a pParent to the base-class. */
    UIStatusBarEditorWindow(UIMachineWindow *pParent);
};

/** UIEditor sub-class
  * used as status-bar editor widget. */
class SHARED_LIBRARY_STUFF UIStatusBarEditorWidget : public UIEditor
{
    Q_OBJECT;

signals:

    /** Notifies about Cancel button click. */
    void sigCancelClicked();

public:

    /** Constructs status-bar editor widget passing @a pParent to the base-class.
      * @param  fStartedFromVMSettings  Brings whether 'this' is a part of VM settings.
      * @param  uMachineID              Brings the machine ID to be used by the editor. */
    UIStatusBarEditorWidget(QWidget *pParent,
                            bool fStartedFromVMSettings = true,
                            const QUuid &uMachineID = QUuid());

    /** Returns the machine ID instance. */
    const QUuid &machineID() const { return m_uMachineID; }
    /** Defines the @a uMachineID instance. */
    void setMachineID(const QUuid &uMachineID);

    /** Returns whether the status-bar enabled. */
    bool isStatusBarEnabled() const;
    /** Defines whether the status-bar @a fEnabled. */
    void setStatusBarEnabled(bool fEnabled);

    /** Returns status-bar indicator restrictions. */
    const QList<IndicatorType> &statusBarIndicatorRestrictions() const { return m_restrictions; }
    /** Returns status-bar indicator order. */
    const QList<IndicatorType> &statusBarIndicatorOrder() const { return m_order; }
    /** Defines status-bar indicator @a restrictions and @a order. */
    void setStatusBarConfiguration(const QList<IndicatorType> &restrictions, const QList<IndicatorType> &order);

protected:

    /** Handles paint @a pEvent. */
    virtual void paintEvent(QPaintEvent *pEvent) RT_OVERRIDE;

    /** Handles drag-enter @a pEvent. */
    virtual void dragEnterEvent(QDragEnterEvent *pEvent) RT_OVERRIDE;
    /** Handles drag-move @a pEvent. */
    virtual void dragMoveEvent(QDragMoveEvent *pEvent) RT_OVERRIDE;
    /** Handles drag-leave @a pEvent. */
    virtual void dragLeaveEvent(QDragLeaveEvent *pEvent) RT_OVERRIDE;
    /** Handles drop @a pEvent. */
    virtual void dropEvent(QDropEvent *pEvent) RT_OVERRIDE;

private slots:

    /** Handles configuration change. */
    void sltHandleConfigurationChange(const QUuid &uMachineID);

    /** Handles button click. */
    void sltHandleButtonClick();

    /** Handles drag object destroy. */
    void sltHandleDragObjectDestroy();

    /** Handles translation event. */
    virtual void sltRetranslateUI() RT_OVERRIDE RT_FINAL;

private:

    /** Prepares all. */
    void prepare();
    /** Prepares status-buttons. */
    void prepareStatusButtons();
    /** Prepares status-button of certain @a enmType. */
    void prepareStatusButton(IndicatorType enmType);

    /** Returns position for passed @a enmType. */
    int position(IndicatorType enmType) const;

    /** @name General
      * @{ */
        /** Holds whether 'this' is prepared. */
        bool     m_fPrepared;
        /** Holds whether 'this' is a part of VM settings. */
        bool     m_fStartedFromVMSettings;
        /** Holds the machine ID instance. */
        QUuid  m_uMachineID;
    /** @} */

    /** @name Contents
      * @{ */
        /** Holds the main-layout instance. */
        QHBoxLayout                                   *m_pMainLayout;
        /** Holds the button-layout instance. */
        QHBoxLayout                                   *m_pButtonLayout;
        /** Holds the close-button instance. */
        QIToolButton                                  *m_pButtonClose;
        /** Holds the enable-checkbox instance. */
        QCheckBox                                     *m_pCheckBoxEnable;
        /** Holds status-bar buttons. */
        QMap<IndicatorType, UIStatusBarEditorButton*>  m_buttons;
    /** @} */

    /** @name Contents: Restrictions
      * @{ */
        /** Holds the cached status-bar button restrictions. */
        QList<IndicatorType>  m_restrictions;
    /** @} */

    /** @name Contents: Order
      * @{ */
        /** Holds the cached status-bar button order. */
        QList<IndicatorType>     m_order;
        /** Holds the token-button to drop dragged-button nearby. */
        UIStatusBarEditorButton *m_pButtonDropToken;
        /** Holds whether dragged-button should be dropped <b>after</b> the token-button. */
        bool                     m_fDropAfterTokenButton;
    /** @} */
};

#endif /* !FEQT_INCLUDED_SRC_settings_editors_UIStatusBarEditor_h */
