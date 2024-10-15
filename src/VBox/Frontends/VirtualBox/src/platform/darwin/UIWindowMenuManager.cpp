/* $Id: UIWindowMenuManager.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIWindowMenuManager class implementation.
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

/* Qt includes: */
#include <QActionGroup>
#include <QApplication>
#include <QMenu>

/* GUI includes: */
#include "UITranslationEventListener.h"
#include "UIWindowMenuManager.h"

/* Other VBox includes: */
#include <iprt/assert.h>


/** QObject extension
  * used as Mac OS X 'Window' menu helper. */
class UIMenuHelper : public QObject
{
    Q_OBJECT;

public:

    /** Constructs menu-helper on the basis of passed @a windows. */
    UIMenuHelper(const QList<QWidget*> &windows);
    /** Destructs menu-helper. */
    virtual ~UIMenuHelper() RT_OVERRIDE;

    /** Returns 'Window' menu. */
    QMenu *menu() const { return m_pWindowMenu; }

    /** Adds @a pWindow into 'Window' menu. */
    QAction *addWindow(QWidget *pWindow);
    /** Removes @a pWindow from 'Window' menu. */
    void removeWindow(QWidget *pWindow);

    /** Handles translation event. */
    virtual void retranslateUi();

    /** Updates toggle action states according to passed @a pActiveWindow. */
    void updateStatus(QWidget *pActiveWindow);

private slots:

    /** Handles request to minimize active-window. */
    void sltMinimizeActiveWindow();

    /** Handles request to raise sender window. */
    void sltRaiseSender();

private:

    /** Holds the 'Window' menu instance. */
    QMenu                     *m_pWindowMenu;
    /** Holds the action group instance. */
    QActionGroup              *m_pGroup;
    /** Holds the 'Minimize' action instance. */
    QAction                   *m_pMinimizeAction;
    /** Holds the hash of the registered menu-helper instances. */
    QHash<QWidget*, QAction*>  m_windows;
};


/*********************************************************************************************************************************
*   Class UIMenuHelper implementation.                                                                                           *
*********************************************************************************************************************************/

UIMenuHelper::UIMenuHelper(const QList<QWidget*> &windows)
{
    /* Prepare 'Window' menu: */
    m_pWindowMenu = new QMenu;

    /* Prepare action group: */
    m_pGroup = new QActionGroup(this);
    m_pGroup->setExclusive(true);

    /* Prepare 'Minimize' action: */
    m_pMinimizeAction = new QAction(this);
    m_pWindowMenu->addAction(m_pMinimizeAction);
    connect(m_pMinimizeAction, SIGNAL(triggered(bool)),
            this, SLOT(sltMinimizeActiveWindow()));

    /* Make sure all already available windows are
     * properly registered within this menu: */
    for (int i = 0; i < windows.size(); ++i)
        addWindow(windows.at(i));

    /* Apply language settings: */
    retranslateUi();
}

UIMenuHelper::~UIMenuHelper()
{
    /* Cleanup 'Window' menu: */
    delete m_pWindowMenu;

    /* Cleanup actions: */
    qDeleteAll(m_windows);
}

QAction *UIMenuHelper::addWindow(QWidget *pWindow)
{
    QAction *pAction = 0;
    if (   pWindow
        && !m_windows.contains(pWindow))
    {
        if (m_windows.size() < 2)
            m_pWindowMenu->addSeparator();

        /* The main window always first: */
        pAction = new QAction(this);
        pAction->setText(pWindow->windowTitle());
        pAction->setMenuRole(QAction::NoRole);
        pAction->setData(QVariant::fromValue(pWindow));
        pAction->setCheckable(true);

        /* The first registered one is always
         * considered as the main window: */
        if (m_windows.size() == 0)
            pAction->setShortcut(QKeySequence("Ctrl+0"));
        m_pGroup->addAction(pAction);
        connect(pAction, SIGNAL(triggered(bool)),
                this, SLOT(sltRaiseSender()));
        m_pWindowMenu->addAction(pAction);
        m_windows[pWindow] = pAction;
    }
    return pAction;
}

void UIMenuHelper::removeWindow(QWidget *pWindow)
{
    if (m_windows.contains(pWindow))
    {
        delete m_windows.value(pWindow);
        m_windows.remove(pWindow);
    }
}

void UIMenuHelper::retranslateUi()
{
    /* Translate menu: */
    m_pWindowMenu->setTitle(QApplication::translate("UIActionPool", "&Window"));

    /* Translate menu 'Minimize' action: */
    m_pMinimizeAction->setText(QApplication::translate("UIActionPool", "&Minimize"));
    m_pMinimizeAction->setShortcut(QKeySequence("Ctrl+M"));

    /* Translate other menu-actions: */
    foreach (QAction *pAction, m_windows.values())
    {
        /* Get corresponding window from action's data: */
        QWidget *pWindow = pAction->data().value<QWidget*>();
        /* Use the window's title as the action's text: */
        pAction->setText(pWindow->windowTitle());
    }
}

void UIMenuHelper::updateStatus(QWidget *pActiveWindow)
{
    /* 'Minimize' action is enabled if there is active-window: */
    m_pMinimizeAction->setEnabled(pActiveWindow != 0);

    /* If there is active-window: */
    if (pActiveWindow)
    {
        /* Toggle corresponding action on: */
        if (m_windows.contains(pActiveWindow))
            m_windows.value(pActiveWindow)->setChecked(true);
    }
    /* If there is no active-window: */
    else
    {
        /* Make sure corresponding action toggled off: */
        if (QAction *pChecked = m_pGroup->checkedAction())
            pChecked->setChecked(false);
    }
}

void UIMenuHelper::sltMinimizeActiveWindow()
{
    if (QWidget *pActiveWindow = qApp->activeWindow())
        pActiveWindow->showMinimized();
}

void UIMenuHelper::sltRaiseSender()
{
    AssertReturnVoid(sender());
    if (QAction *pAction = qobject_cast<QAction*>(sender()))
    {
        if (QWidget *pWidget = pAction->data().value<QWidget*>())
        {
            pWidget->show();
            pWidget->raise();
            pWidget->activateWindow();
        }
    }
}


/*********************************************************************************************************************************
*   Class UIWindowMenuManager implementation.                                                                                    *
*********************************************************************************************************************************/

/* static */
UIWindowMenuManager* UIWindowMenuManager::s_pInstance = 0;

/* static */
void UIWindowMenuManager::create()
{
    /* Make sure 'Window' menu Manager is not created: */
    AssertReturnVoid(!s_pInstance);

    /* Create 'Window' menu Manager: */
    new UIWindowMenuManager;
}

/* static */
void UIWindowMenuManager::destroy()
{
    /* Make sure 'Window' menu Manager is created: */
    AssertPtrReturnVoid(s_pInstance);

    /* Delete 'Window' menu Manager: */
    delete s_pInstance;
}

QMenu *UIWindowMenuManager::createMenu(QWidget *pWindow)
{
    /* Create helper: */
    UIMenuHelper *pHelper = new UIMenuHelper(m_windows);
    /* Register it: */
    m_helpers[pWindow] = pHelper;

    /* Return menu of created helper: */
    return pHelper->menu();
}

void UIWindowMenuManager::destroyMenu(QWidget *pWindow)
{
    /* If window is registered: */
    if (m_helpers.contains(pWindow))
    {
        /* Delete helper: */
        delete m_helpers.value(pWindow);
        /* Unregister it: */
        m_helpers.remove(pWindow);
    }
}

void UIWindowMenuManager::addWindow(QWidget *pWindow)
{
    /* Register window: */
    m_windows.append(pWindow);
    /* Add window to all menus we have: */
    foreach (UIMenuHelper *pHelper, m_helpers.values())
        pHelper->addWindow(pWindow);
}

void UIWindowMenuManager::removeWindow(QWidget *pWindow)
{
    /* Remove window from all menus we have: */
    foreach (UIMenuHelper *pHelper, m_helpers.values())
        pHelper->removeWindow(pWindow);
    /* Unregister window: */
    m_windows.removeAll(pWindow);
}

void UIWindowMenuManager::sltRetranslateUI()
{
    /* Translate all the helpers: */
    foreach (UIMenuHelper *pHelper, m_helpers.values())
        pHelper->retranslateUi();
}

UIWindowMenuManager::UIWindowMenuManager()
{
    /* Assign instance: */
    s_pInstance = this;

    /* Install global event-filter: */
    qApp->installEventFilter(this);
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIWindowMenuManager::sltRetranslateUI);
}

UIWindowMenuManager::~UIWindowMenuManager()
{
    /* Cleanup all helpers: */
    qDeleteAll(m_helpers);

    /* Unassign instance: */
    s_pInstance = 0;
}

bool UIWindowMenuManager::eventFilter(QObject *pObject, QEvent *pEvent)
{
    /* Acquire event type: */
    const QEvent::Type type = pEvent->type();

#ifdef VBOX_OSE /// @todo Do we still need it?
    // WORKAROUND:
    // Stupid Qt: Qt doesn't check if a window is minimized when a command is
    // executed. This leads to strange behaviour. The minimized window is
    // partly restored, but not usable. As a workaround we raise the parent
    // window before we let execute the command.
    // Note: fixed in our local Qt build since 4.7.0.
    if (pObject && type == QEvent::Show)
    {
        QWidget *pWidget = qobject_cast<QWidget*>(pObject);
        if (   pWidget
            && pWidget->parentWidget()
            && pWidget->parentWidget()->isMinimized())
        {
            pWidget->parentWidget()->show();
            pWidget->parentWidget()->raise();
            pWidget->parentWidget()->activateWindow();
        }
    }
#endif /* VBOX_OSE */

    /* We need to track several events which leads to different window
     * activation and change the menu items in that case. */
    if (   type == QEvent::ActivationChange
        || type == QEvent::WindowActivate
        || type == QEvent::WindowDeactivate
        || type == QEvent::WindowStateChange
        || type == QEvent::Show
        || type == QEvent::Close
        || type == QEvent::Hide)
    {
        QWidget *pActiveWindow = qApp->activeWindow();
        foreach (UIMenuHelper *pHelper, m_helpers.values())
            pHelper->updateStatus(pActiveWindow);
    }

    /* Besides our own retranslation, we should also retranslate
     * everything on any registered widget title change event: */
    if (pObject && type == QEvent::WindowTitleChange)
    {
        QWidget *pWidget = qobject_cast<QWidget*>(pObject);
        if (pWidget && m_helpers.contains(pWidget))
            sltRetranslateUI();
    }

    /* Call to base-class: */
    return QObject::eventFilter(pObject, pEvent);
}


#include "UIWindowMenuManager.moc"
