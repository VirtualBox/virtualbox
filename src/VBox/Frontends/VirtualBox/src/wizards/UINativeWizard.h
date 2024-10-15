/* $Id: UINativeWizard.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UINativeWizard class declaration.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_wizards_UINativeWizard_h
#define FEQT_INCLUDED_SRC_wizards_UINativeWizard_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* Qt includes: */
#include <QDialog>
#include <QMap>
#include <QPointer>
#include <QSet>

/* GUI includes: */
#include "UIExtraDataDefs.h"
#include "UILibraryDefs.h"

/* Forward declarations: */
class QLabel;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;
class UINativeWizardPage;
class UINotificationCenter;
class UINotificationProgress;

/** Native wizard buttons. */
enum WizardButtonType
{
    WizardButtonType_Invalid,
    WizardButtonType_Help,
    WizardButtonType_Back,
    WizardButtonType_Next,
    WizardButtonType_Cancel,
    WizardButtonType_Max,
};
Q_DECLARE_METATYPE(WizardButtonType);

#ifdef VBOX_WS_MAC
/** QWidget-based QFrame analog with one particular purpose to
  * simulate macOS wizard frame without influencing palette hierarchy. */
class SHARED_LIBRARY_STUFF UIFrame : public QWidget
{
    Q_OBJECT;

public:

    /** Constructs UIFrame passing @a pParent to the base-class. */
    UIFrame(QWidget *pParent);

protected:

    /** Handles paint @a pEvent. */
    virtual void paintEvent(QPaintEvent *pEvent) /* final */;
};
#endif /* VBOX_WS_MAC */

/** QDialog extension with advanced functionality emulating QWizard behavior. */
class SHARED_LIBRARY_STUFF UINativeWizard : public QDialog
{
    Q_OBJECT;

signals:

    /** Notifies listeners about dialog should be closed. */
    void sigClose(WizardType enmType);

public:

    /** Constructs wizard passing @a pParent to the base-class.
      * @param  enmType         Brings the wizard type.
      * @param  strHelpKeyword  Brings the wizard help keyword. */
    UINativeWizard(QWidget *pParent,
                   WizardType enmType,
                   const QString &strHelpKeyword = QString());
    /** Destructs wizard. */
    virtual ~UINativeWizard() RT_OVERRIDE;

    /** Returns local notification-center reference. */
    UINotificationCenter *notificationCenter() const;
    /** Immediately handles notification @a pProgress object. */
    bool handleNotificationProgressNow(UINotificationProgress *pProgress);

    /** Returns wizard button of specified @a enmType. */
    QPushButton *wizardButton(const WizardButtonType &enmType) const;

public slots:

    /** Executes wizard in window modal mode.
      * @note You shouldn't have to override it! */
    virtual int exec() RT_OVERRIDE RT_FINAL;
    /** Shows wizard in non-mode.
      * @note You shouldn't have to override it! */
    virtual void show() /* final */;

protected:

    /** Returns wizard type. */
    WizardType type() const { return m_enmType; }
    /** Returns wizard mode. */
    WizardMode mode() const { return m_enmMode; }
    /** Defines @a strName for wizard button of specified @a enmType. */
    void setWizardButtonName(const WizardButtonType &enmType, const QString &strName);

    /** Defines pixmap @a strName. */
    void setPixmapName(const QString &strName);

    /** Returns whether the page with certain @a iIndex is visible. */
    bool isPageVisible(int iIndex) const;
    /** Defines whether the page with certain @a iIndex is @a fVisible. */
    void setPageVisible(int iIndex, bool fVisible);

    /** Appends wizard @a pPage.
      * @returns assigned page index. */
    int addPage(UINativeWizardPage *pPage);
    /** Populates pages.
      * @note In your subclasses you should add
      *       pages via addPage declared above. */
    virtual void populatePages() = 0;

    /** Handles key-press @a pEvent. */
    virtual void keyPressEvent(QKeyEvent *pEvent) RT_OVERRIDE RT_FINAL;
    /** Handles close @a pEvent. */
    virtual void closeEvent(QCloseEvent *pEvent) RT_OVERRIDE RT_FINAL;

    /** Performs wizard-specific cleanup in case of wizard-mode change
      * such as folder deletion in New VM wizard etc. */
    virtual void cleanWizard() {}

protected slots:

    /** Handles translation event. */
    virtual void sltRetranslateUI();

private slots:

    /** Handles current-page change to page with @a iIndex. */
    void sltCurrentIndexChanged(int iIndex = -1);
    /** Handles page validity changes. */
    void sltCompleteChanged();

    /** Switches to previous page. */
    void sltPrevious();
    /** Switches to next page. */
    void sltNext();

    /** Handle help request*/
    void sltHandleHelpRequest();

private:

    /** Prepares all. */
    void prepare();
    /** Cleanups all. */
    void cleanup();
    /** Inits all. */
    void init();

    /** Performs pages translation. */
    void retranslatePages();

    /** Resizes wizard to golden ratio. */
    void resizeToGoldenRatio();
#ifdef VBOX_WS_MAC
    /** Assigns wizard background. */
    void assignBackground();
#else
    /** Assigns wizard watermark. */
    void assignWatermark();
#endif
    /** Checks if the pages coming after the page with iPageIndex is visible or not. Returns true if
      * page with iPageIndex is the last visible page of the wizard. Returns false otherwise. */
    bool isLastVisiblePage(int iPageIndex) const;

    /** Holds the wizard type. */
    WizardType  m_enmType;
    /** Holds the wizard mode. */
    WizardMode  m_enmMode;
    /** Holds the wizard help keyword. */
    QString     m_strHelpKeyword;
    /** Holds the pixmap name. */
    QString     m_strPixmapName;
    /** Holds the last entered page index. */
    int         m_iLastIndex;
    /** Holds the set of invisible pages. */
    QSet<int>   m_invisiblePages;
    /** Holds whether user has requested to abort wizard. */
    bool        m_fAborted;
    /** Holds whether the dialod had emitted signal to be closed. */
    bool        m_fClosed;

    /** Holds the pixmap label instance. */
    QLabel                               *m_pLabelPixmap;
    /** Holds the right layout instance. */
    QVBoxLayout                          *m_pLayoutRight;
    /** Holds the title label instance. */
    QLabel                               *m_pLabelPageTitle;
    /** Holds the widget-stack instance. */
    QStackedWidget                       *m_pWidgetStack;
    /** Holds button instance map. */
    QMap<WizardButtonType, QPushButton*>  m_buttons;

    /** Holds the local notification-center instance. */
    UINotificationCenter *m_pNotificationCenter;
};

/** Native wizard interface pointer. */
typedef QPointer<UINativeWizard> UINativeWizardPointer;

#endif /* !FEQT_INCLUDED_SRC_wizards_UINativeWizard_h */
