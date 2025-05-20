/* $Id: UIActionPool.cpp 168940 2025-05-20 14:00:56Z serkan.bayraktar@oracle.com $ */
/** @file
 * VBox Qt GUI - UIActionPool class implementation.
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
#include <QHelpEvent>
#include <QToolTip>

/* GUI includes: */
#include "UIActionPool.h"
#include "UIActionPoolManager.h"
#include "UIActionPoolRuntime.h"
#include "UIConverter.h"
#include "UIIconPool.h"
#include "UIMessageCenter.h"
#include "UIShortcutPool.h"
#include "UITranslator.h"
#include "UITranslationEventListener.h"
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
# include "UIExtraDataManager.h"
# include "UINetworkRequestManager.h"
# include "UIUpdateManager.h"
#endif /* VBOX_GUI_WITH_NETWORK_MANAGER */


/** Additional Qt event types. */
enum UIEventTypeActionPool
{
    UIEventTypeActionPool_ActivateAction = QEvent::User + 101,
};

/** QEvent extension
  * representing action-activation event. */
class ActivateActionEvent : public QEvent
{
public:

    /** Constructs @a pAction event. */
    ActivateActionEvent(QAction *pAction)
        : QEvent((QEvent::Type)UIEventTypeActionPool_ActivateAction)
        , m_pAction(pAction)
    {}

    /** Returns the action this event corresponds to. */
    QAction *action() const { return m_pAction; }

private:

    /** Holds the action this event corresponds to. */
    QAction *m_pAction;
};


/*********************************************************************************************************************************
*   Class UIMenu implementation.                                                                                                 *
*********************************************************************************************************************************/

UIMenu::UIMenu()
    : m_fShowToolTip(false)
#ifdef VBOX_WS_MAC
    , m_fConsumable(false)
    , m_fConsumed(false)
#endif
{
}

bool UIMenu::event(QEvent *pEvent)
{
    /* Handle particular event-types: */
    switch (pEvent->type())
    {
        /* Tool-tip request handler: */
        case QEvent::ToolTip:
        {
            /* Get current help-event: */
            QHelpEvent *pHelpEvent = static_cast<QHelpEvent*>(pEvent);
            /* Get action which caused help-event: */
            QAction *pAction = actionAt(pHelpEvent->pos());
            /* If action present => show action's tool-tip if needed: */
            if (pAction && m_fShowToolTip)
                QToolTip::showText(pHelpEvent->globalPos(), pAction->toolTip());
            break;
        }
        default:
            break;
    }
    /* Call to base-class: */
    return QMenu::event(pEvent);
}


/*********************************************************************************************************************************
*   Class UIAction implementation.                                                                                               *
*********************************************************************************************************************************/

UIAction::UIAction(UIActionPool *pParent, UIActionType enmType, bool fMachineMenuAction /* = false */)
    : QAction(pParent)
    , m_pActionPool(pParent)
    , m_enmActionPoolType(pParent->type())
    , m_enmType(enmType)
    , m_fMachineMenuAction(fMachineMenuAction)
    , m_iState(0)
    , m_fShortcutHidden(false)
{
    /* By default there is no specific menu role.
     * It will be set explicitly later. */
    setMenuRole(QAction::NoRole);

#ifdef VBOX_WS_MAC
    /* Make sure each action notifies it's parent about hovering: */
    connect(this, &UIAction::hovered,
            static_cast<UIActionPool*>(parent()), &UIActionPool::sltActionHovered);
#endif
}

UIMenu *UIAction::menu() const
{
    return QAction::menu() ? qobject_cast<UIMenu*>(QAction::menu()) : 0;
}

void UIAction::setState(int iState)
{
    m_iState = iState;
    updateIcon();
    retranslateUi();
    handleStateChange();
}

void UIAction::setIcon(int iState, const QIcon &icon)
{
    m_icons.resize(iState + 1);
    m_icons[iState] = icon;
    updateIcon();
}

void UIAction::setIcon(const QIcon &icon)
{
    setIcon(0, icon);
}

void UIAction::setName(const QString &strName)
{
    /* Remember internal name: */
    m_strName = strName;
    /* Update text according new name: */
    updateText();
}

void UIAction::setShortcuts(const QList<QKeySequence> &shortcuts)
{
    /* Only for manager's action-pool: */
    if (m_enmActionPoolType == UIType_ManagerUI)
    {
        /* If primary shortcut should be visible: */
        if (!m_fShortcutHidden)
            /* Call to base-class: */
            QAction::setShortcuts(shortcuts);
        /* Remember shortcuts: */
        m_shortcuts = shortcuts;
    }
    /* Update text according to new primary shortcut: */
    updateText();
}

void UIAction::showShortcut()
{
    m_fShortcutHidden = false;
    if (!m_shortcuts.isEmpty())
        QAction::setShortcuts(m_shortcuts);
}

void UIAction::hideShortcut()
{
    m_fShortcutHidden = true;
    if (!shortcut().isEmpty())
        QAction::setShortcuts(QList<QKeySequence>());
}

QString UIAction::nameInMenu() const
{
    /* Action-name format depends on action-pool type: */
    switch (m_enmActionPoolType)
    {
        /* Unchanged name for Manager UI: */
        case UIType_ManagerUI: return name();
        /* Filtered name for Runtime UI: */
        case UIType_RuntimeUI: return UITranslator::removeAccelMark(name());
    }
    /* Nothing by default: */
    return QString();
}

void UIAction::updateIcon()
{
    QAction::setIcon(m_icons.value(m_iState, m_icons.value(0)));
}

void UIAction::updateText()
{
    /* First of all, action-text depends on action type: */
    switch (m_enmType)
    {
        case UIActionType_Menu:
        {
            /* For menu types it's very easy: */
            setText(nameInMenu());
            break;
        }
        default:
        {
            /* For rest of action types it depends on action-pool type: */
            switch (m_enmActionPoolType)
            {
                /* The same as menu name for Manager UI: */
                case UIType_ManagerUI:
                {
                    setText(nameInMenu());
                    break;
                }
                /* With shortcut appended for Runtime UI: */
                case UIType_RuntimeUI:
                {
                    if (m_fMachineMenuAction)
                        setText(UITranslator::insertKeyToActionText(nameInMenu(),
                                                                    gShortcutPool->shortcut(actionPool(), this).primaryToPortableText()));
                    else
                        setText(nameInMenu());
                    break;
                }
            }
            break;
        }
    }
}

/* static */
QString UIAction::simplifyText(QString strText)
{
    return strText.remove('.').remove('&');
}


/*********************************************************************************************************************************
*   Class UIActionMenu implementation.                                                                                           *
*********************************************************************************************************************************/

UIActionMenu::UIActionMenu(UIActionPool *pParent,
                           const QString &strIcon, const QString &strIconDisabled)
    : UIAction(pParent, UIActionType_Menu)
    , m_pMenu(0)
{
    if (!strIcon.isNull())
        setIcon(UIIconPool::iconSet(strIcon, strIconDisabled));
    prepare();
}

UIActionMenu::UIActionMenu(UIActionPool *pParent,
                           const QString &strIconNormal, const QString &strIconSmall,
                           const QString &strIconNormalDisabled, const QString &strIconSmallDisabled)
    : UIAction(pParent, UIActionType_Menu)
    , m_pMenu(0)
{
    if (!strIconNormal.isNull())
        setIcon(UIIconPool::iconSetFull(strIconNormal, strIconSmall, strIconNormalDisabled, strIconSmallDisabled));
    prepare();
}

UIActionMenu::UIActionMenu(UIActionPool *pParent,
                           const QIcon &icon)
    : UIAction(pParent, UIActionType_Menu)
    , m_pMenu(0)
{
    if (!icon.isNull())
        setIcon(icon);
    prepare();
}

UIActionMenu::~UIActionMenu()
{
    /* Delete menu: */
    delete m_pMenu;
    m_pMenu = 0;
}

void UIActionMenu::setShowToolTip(bool fShowToolTip)
{
    AssertPtrReturnVoid(m_pMenu);
    m_pMenu->setShowToolTip(fShowToolTip);
}

void UIActionMenu::prepare()
{
    /* Prepare menu: */
    m_pMenu = new UIMenu;
    AssertPtrReturnVoid(m_pMenu);
    {
        setMenu(m_pMenu);
        connect(m_pMenu, &UIMenu::aboutToShow,
                actionPool(), &UIActionPool::sltHandleMenuPrepare);
    }
}


/*********************************************************************************************************************************
*   Class UIActionSimple implementation.                                                                                         *
*********************************************************************************************************************************/

UIActionSimple::UIActionSimple(UIActionPool *pParent,
                               bool fMachineMenuAction /* = false */)
    : UIAction(pParent, UIActionType_Simple, fMachineMenuAction)
{
}

UIActionSimple::UIActionSimple(UIActionPool *pParent,
                               const QString &strIcon, const QString &strIconDisabled,
                               bool fMachineMenuAction /* = false */)
    : UIAction(pParent, UIActionType_Simple, fMachineMenuAction)
{
    if (!strIcon.isNull())
        setIcon(UIIconPool::iconSet(strIcon, strIconDisabled));
}

UIActionSimple::UIActionSimple(UIActionPool *pParent,
                               const QString &strIconNormal, const QString &strIconSmall,
                               const QString &strIconNormalDisabled, const QString &strIconSmallDisabled,
                               bool fMachineMenuAction /* = false */)
    : UIAction(pParent, UIActionType_Simple, fMachineMenuAction)
{
    if (!strIconNormal.isNull())
        setIcon(UIIconPool::iconSetFull(strIconNormal, strIconSmall, strIconNormalDisabled, strIconSmallDisabled));
}

UIActionSimple::UIActionSimple(UIActionPool *pParent,
                               const QIcon &icon,
                               bool fMachineMenuAction /* = false */)
    : UIAction(pParent, UIActionType_Simple, fMachineMenuAction)
{
    if (!icon.isNull())
        setIcon(icon);
}


/*********************************************************************************************************************************
*   Class UIActionToggle implementation.                                                                                         *
*********************************************************************************************************************************/

UIActionToggle::UIActionToggle(UIActionPool *pParent,
                               bool fMachineMenuAction /* = false */)
    : UIAction(pParent, UIActionType_Toggle, fMachineMenuAction)
{
    prepare();
}

UIActionToggle::UIActionToggle(UIActionPool *pParent,
                               const QString &strIcon, const QString &strIconDisabled,
                               bool fMachineMenuAction /* = false */)
    : UIAction(pParent, UIActionType_Toggle, fMachineMenuAction)
{
    if (!strIcon.isNull())
        setIcon(UIIconPool::iconSet(strIcon, strIconDisabled));
    prepare();
}

UIActionToggle::UIActionToggle(UIActionPool *pParent,
                               const QString &strIconOn, const QString &strIconOff,
                               const QString &strIconOnDisabled, const QString &strIconOffDisabled,
                               bool fMachineMenuAction /* = false */)
    : UIAction(pParent, UIActionType_Toggle, fMachineMenuAction)
{
    if (!strIconOn.isNull())
        setIcon(UIIconPool::iconSetOnOff(strIconOn, strIconOff, strIconOnDisabled, strIconOffDisabled));
    prepare();
}

UIActionToggle::UIActionToggle(UIActionPool *pParent,
                               const QIcon &icon,
                               bool fMachineMenuAction /* = false */)
    : UIAction(pParent, UIActionType_Toggle, fMachineMenuAction)
{
    if (!icon.isNull())
        setIcon(icon);
    prepare();
}

void UIActionToggle::prepare()
{
    setCheckable(true);
}


/** Menu action extension, used as 'Application' menu class. */
class UIActionMenuApplication : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuApplication(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {
#ifdef VBOX_WS_MAC
        menu()->setConsumable(true);
#endif
        retranslateUi();
    }

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::MenuType_Application;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::MenuType_Application);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->isAllowedInMenuBar(UIExtraDataMetaDefs::MenuType_Application);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
#ifdef VBOX_WS_MAC
        setName(QApplication::translate("UIActionPool", "&VirtualBox"));
#else
        setName(QApplication::translate("UIActionPool", "&File"));
#endif
    }
};

/** Simple action extension, used as 'Close' action class. */
class UIActionSimplePerformClose : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimplePerformClose(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/exit_16px.png", ":/exit_16px.png", true)
    {
        setMenuRole(QAction::QuitRole);
    }

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::MenuApplicationActionType_Close;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::MenuApplicationActionType_Close);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->isAllowedInMenuApplication(UIExtraDataMetaDefs::MenuApplicationActionType_Close);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("Close");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType enmActionPoolType) const RT_OVERRIDE
    {
        switch (enmActionPoolType)
        {
            case UIType_ManagerUI: break;
            case UIType_RuntimeUI: return QKeySequence("Q");
        }
        return QKeySequence();
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Close..."));
        setStatusTip(QApplication::translate("UIActionPool", "Close the virtual machine"));
    }
};

#ifdef VBOX_WS_MAC
/** Menu action extension, used as 'Window' menu class. */
class UIActionMenuWindow : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuWindow(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::MenuType_Window;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::MenuType_Window);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->isAllowedInMenuBar(UIExtraDataMetaDefs::MenuType_Window);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Window"));
    }
};

/** Simple action extension, used as 'Minimize' action class. */
class UIActionSimpleMinimize : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleMinimize(UIActionPool *pParent)
        : UIActionSimple(pParent)
    {}

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::MenuWindowActionType_Minimize;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::MenuWindowActionType_Minimize);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->isAllowedInMenuWindow(UIExtraDataMetaDefs::MenuWindowActionType_Minimize);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("Minimize");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Minimize"));
        setStatusTip(QApplication::translate("UIActionPool", "Minimize active window"));
    }
};
#endif /* VBOX_WS_MAC */

/** Menu action extension, used as 'Help' menu class. */
class UIActionMenuHelp : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuHelp(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {
        retranslateUi();
    }

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::MenuType_Help;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::MenuType_Help);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->isAllowedInMenuBar(UIExtraDataMetaDefs::MenuType_Help);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Help"));
    }
};

/** Simple action extension, used as 'Contents' action class. */
class UIActionSimpleContents : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleContents(UIActionPool *pParent)
        : UIActionSimple(pParent, UIIconPool::defaultIcon(UIIconPool::UIDefaultIconType_DialogHelp), true)
    {
        retranslateUi();
    }

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::MenuHelpActionType_Contents;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::MenuHelpActionType_Contents);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->isAllowedInMenuHelp(UIExtraDataMetaDefs::MenuHelpActionType_Contents);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("Help");
    }

    /** Returns standard shortcut. */
    virtual QKeySequence standardShortcut(UIType enmActionPoolType) const RT_OVERRIDE
    {
        switch (enmActionPoolType)
        {
            case UIType_ManagerUI: return UIShortcutPool::standardSequence(QKeySequence::HelpContents);
            case UIType_RuntimeUI: break;
        }
        return QKeySequence();
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Contents..."));
        setStatusTip(QApplication::translate("UIActionPool", "Show help contents"));
    }
};

/** Simple action extension, used as 'Online Documentation' action class. */
class UIActionSimpleOnlineDocumentation : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleOnlineDocumentation(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/site_oracle_16px.png", ":/site_oracle_16px.png", true)
    {
        retranslateUi();
    }

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::MenuHelpActionType_OnlineDocumentation;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::MenuHelpActionType_OnlineDocumentation);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->isAllowedInMenuHelp(UIExtraDataMetaDefs::MenuHelpActionType_OnlineDocumentation);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("OnlineDocumentation");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Online Documentation..."));
        setStatusTip(QApplication::translate("UIActionPool", "Open the browser and go to the VirtualBox user guide"));
    }
};

/** Simple action extension, used as 'Web Site' action class. */
class UIActionSimpleWebSite : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleWebSite(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/site_16px.png", ":/site_16px.png", true)
    {
        retranslateUi();
    }

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::MenuHelpActionType_WebSite;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::MenuHelpActionType_WebSite);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->isAllowedInMenuHelp(UIExtraDataMetaDefs::MenuHelpActionType_WebSite);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("Web");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&VirtualBox Web Site..."));
        setStatusTip(QApplication::translate("UIActionPool", "Open the browser and go to the VirtualBox product web site"));
    }
};

/** Simple action extension, used as 'Bug Tracker' action class. */
class UIActionSimpleBugTracker : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleBugTracker(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/site_bugtracker_16px.png", ":/site_bugtracker_16px.png", true)
    {
        retranslateUi();
    }

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::MenuHelpActionType_BugTracker;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::MenuHelpActionType_BugTracker);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->isAllowedInMenuHelp(UIExtraDataMetaDefs::MenuHelpActionType_BugTracker);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("BugTracker");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&VirtualBox Bug Tracker..."));
        setStatusTip(QApplication::translate("UIActionPool", "Open the browser and go to the VirtualBox product bug tracker"));
    }
};

/** Simple action extension, used as 'Forums' action class. */
class UIActionSimpleForums : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleForums(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/site_forum_16px.png", ":/site_forum_16px.png", true)
    {
        retranslateUi();
    }

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::MenuHelpActionType_Forums;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::MenuHelpActionType_Forums);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->isAllowedInMenuHelp(UIExtraDataMetaDefs::MenuHelpActionType_Forums);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("Forums");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&VirtualBox Forums..."));
        setStatusTip(QApplication::translate("UIActionPool", "Open the browser and go to the VirtualBox product forums"));
    }
};

/** Simple action extension, used as 'Oracle' action class. */
class UIActionSimpleOracle : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleOracle(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/site_oracle_16px.png", ":/site_oracle_16px.png", true)
    {
        retranslateUi();
    }

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::MenuHelpActionType_Oracle;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::MenuHelpActionType_Oracle);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->isAllowedInMenuHelp(UIExtraDataMetaDefs::MenuHelpActionType_Oracle);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("Oracle");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Oracle Web Site..."));
        setStatusTip(QApplication::translate("UIActionPool", "Open the browser and go to the Oracle web site"));
    }
};

/** Simple action extension, used as 'Reset Warnings' action class. */
class UIActionSimpleResetWarnings : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleResetWarnings(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/reset_warnings_16px.png", ":/reset_warnings_16px.png", true)
    {
        setMenuRole(QAction::ApplicationSpecificRole);
        retranslateUi();
    }

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::MenuApplicationActionType_ResetWarnings;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::MenuApplicationActionType_ResetWarnings);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->isAllowedInMenuApplication(UIExtraDataMetaDefs::MenuApplicationActionType_ResetWarnings);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ResetWarnings");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Reset All Warnings"));
        setStatusTip(QApplication::translate("UIActionPool", "Go back to showing all suppressed warnings and messages"));
    }
};

#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
/** Simple action extension, used as 'Check for Updates' action class. */
class UIActionSimpleCheckForUpdates : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleCheckForUpdates(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/refresh_16px.png", ":/refresh_disabled_16px.png", true)
    {
        setMenuRole(QAction::ApplicationSpecificRole);
        retranslateUi();
    }

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::MenuApplicationActionType_CheckForUpdates;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::MenuApplicationActionType_CheckForUpdates);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->isAllowedInMenuApplication(UIExtraDataMetaDefs::MenuApplicationActionType_CheckForUpdates);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("Update");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "C&heck for Updates..."));
        setStatusTip(QApplication::translate("UIActionPool", "Check for a new VirtualBox version"));
    }
};
#endif /* VBOX_GUI_WITH_NETWORK_MANAGER */

/** Simple action extension, used as 'About' action class. */
class UIActionSimpleAbout : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleAbout(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/about_16px.png", ":/about_16px.png", true)
    {
        setMenuRole(QAction::AboutRole);
        retranslateUi();
    }

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
#ifdef VBOX_WS_MAC
        return UIExtraDataMetaDefs::MenuApplicationActionType_About;
#else
        return UIExtraDataMetaDefs::MenuHelpActionType_About;
#endif
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
#ifdef VBOX_WS_MAC
        return gpConverter->toInternalString(UIExtraDataMetaDefs::MenuApplicationActionType_About);
#else
        return gpConverter->toInternalString(UIExtraDataMetaDefs::MenuHelpActionType_About);
#endif
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
#ifdef VBOX_WS_MAC
        return actionPool()->isAllowedInMenuApplication(UIExtraDataMetaDefs::MenuApplicationActionType_About);
#else
        return actionPool()->isAllowedInMenuHelp(UIExtraDataMetaDefs::MenuHelpActionType_About);
#endif
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("About");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&About VirtualBox..."));
        setStatusTip(QApplication::translate("UIActionPool", "Display a window with product information"));
    }
};

/** Simple action extension, used as 'Preferences' action class. */
class UIActionSimplePreferences : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimplePreferences(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/global_settings_32px.png", ":/global_settings_16px.png",
                         ":/global_settings_disabled_32px.png", ":/global_settings_disabled_16px.png",
                         true)
    {
        setMenuRole(QAction::PreferencesRole);
        retranslateUi();
    }

protected:

    /** Returns action extra-data ID. */
    virtual int extraDataID() const RT_OVERRIDE
    {
        return UIExtraDataMetaDefs::MenuApplicationActionType_Preferences;
    }
    /** Returns action extra-data key. */
    virtual QString extraDataKey() const RT_OVERRIDE
    {
        return gpConverter->toInternalString(UIExtraDataMetaDefs::MenuApplicationActionType_Preferences);
    }
    /** Returns whether action is allowed. */
    virtual bool isAllowed() const RT_OVERRIDE
    {
        return actionPool()->isAllowedInMenuApplication(UIExtraDataMetaDefs::MenuApplicationActionType_Preferences);
    }

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("Preferences");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType enmActionPoolType) const RT_OVERRIDE
    {
        switch (enmActionPoolType)
        {
            case UIType_ManagerUI: return QKeySequence("Ctrl+G");
            case UIType_RuntimeUI: break;
        }
        return QKeySequence();
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Preferences...", "global preferences window"));
        setStatusTip(QApplication::translate("UIActionPool", "Display the global preferences window"));
        setToolTip(  QApplication::translate("UIActionPool", "Display Global Preferences")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Menu action extension, used as 'Log' menu class. */
class UIActionMenuSelectorLog : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuSelectorLog(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Log"));
    }
};

/** Simple action extension, used as 'Toggle Pane Find' action class. */
class UIActionMenuSelectorLogTogglePaneFind : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuSelectorLogTogglePaneFind(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
        setIcon(UIIconPool::iconSetFull(":/log_viewer_find_32px.png",          ":/log_viewer_find_16px.png",
                                        ":/log_viewer_find_disabled_32px.png", ":/log_viewer_find_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToggleLogFind");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+F");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Find"));
        setShortcutScope(QApplication::translate("UIActionPool", "Log Viewer"));
        setStatusTip(QApplication::translate("UIActionPool", "Open pane with searching options"));
        setToolTip(  QApplication::translate("UIActionPool", "Open Find Pane")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Toggle Pane Filter' action class. */
class UIActionMenuSelectorLogTogglePaneFilter : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuSelectorLogTogglePaneFilter(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
        setIcon(UIIconPool::iconSetFull(":/log_viewer_filter_32px.png",          ":/log_viewer_filter_16px.png",
                                        ":/log_viewer_filter_disabled_32px.png", ":/log_viewer_filter_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToggleLogFilter");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+T");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Filter"));
        setShortcutScope(QApplication::translate("UIActionPool", "Log Viewer"));
        setStatusTip(QApplication::translate("UIActionPool", "Open pane with filtering options"));
        setToolTip(  QApplication::translate("UIActionPool", "Open Filter Pane")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Toggle Pane Bookmark' action class. */
class UIActionMenuSelectorLogTogglePaneBookmark : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuSelectorLogTogglePaneBookmark(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
        setIcon(UIIconPool::iconSetFull(":/log_viewer_bookmark_32px.png",          ":/log_viewer_bookmark_16px.png",
                                        ":/log_viewer_bookmark_disabled_32px.png", ":/log_viewer_bookmark_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToggleLogBookmark");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+D");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Bookmark"));
        setShortcutScope(QApplication::translate("UIActionPool", "Log Viewer"));
        setStatusTip(QApplication::translate("UIActionPool", "Open pane with bookmarking options"));
        setToolTip(  QApplication::translate("UIActionPool", "Open Bookmark Pane")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Toggle Pane Preferences' action class. */
class UIActionMenuSelectorLogTogglePanePreferences : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuSelectorLogTogglePanePreferences(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
        setIcon(UIIconPool::iconSetFull(":/log_viewer_options_32px.png",          ":/log_viewer_options_16px.png",
                                        ":/log_viewer_options_disabled_32px.png", ":/log_viewer_options_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToggleLogPreferences");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+P");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Preferences"));
        setShortcutScope(QApplication::translate("UIActionPool", "Log Viewer"));
        setStatusTip(QApplication::translate("UIActionPool", "Open pane with log viewer preferences"));
        setToolTip(  QApplication::translate("UIActionPool", "Open Preferences Pane")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Refresh' action class. */
class UIActionMenuSelectorLogPerformRefresh : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuSelectorLogPerformRefresh(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/log_viewer_refresh_32px.png", ":/log_viewer_refresh_16px.png",
                         ":/log_viewer_refresh_disabled_32px.png", ":/log_viewer_refresh_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("RefreshLog");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+R");
    }

    /** Returns standard shortcut. */
    virtual QKeySequence standardShortcut(UIType) const RT_OVERRIDE
    {
        return actionPool()->isTemporary() ? QKeySequence() : QKeySequence(QKeySequence::Refresh);
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Refresh"));
        setShortcutScope(QApplication::translate("UIActionPool", "Log Viewer"));
        setStatusTip(QApplication::translate("UIActionPool", "Refresh the currently viewed log"));
        setToolTip(  QApplication::translate("UIActionPool", "Refresh Viewed Log")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Reload' action class. */
class UIActionMenuSelectorLogPerformReload : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuSelectorLogPerformReload(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/log_viewer_refresh_32px.png", ":/log_viewer_refresh_16px.png",
                         ":/log_viewer_refresh_disabled_32px.png", ":/log_viewer_refresh_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ReloadAllLogs");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Reload"));
        setShortcutScope(QApplication::translate("UIActionPool", "Log Viewer"));
        setStatusTip(QApplication::translate("UIActionPool", "Reread all the log files and refresh pages"));
        setToolTip(  QApplication::translate("UIActionPool", "Reload Log Files")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Save' action class. */
class UIActionMenuSelectorLogPerformSave : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuSelectorLogPerformSave(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/log_viewer_save_32px.png", ":/log_viewer_save_16px.png",
                         ":/log_viewer_save_disabled_32px.png", ":/log_viewer_save_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("SaveLog");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("Ctrl+Shift+S");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Save..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Log Viewer"));
        setStatusTip(QApplication::translate("UIActionPool", "Save selected virtual machine log"));
        setToolTip(  QApplication::translate("UIActionPool", "Save Virtual Machine Log")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Menu action extension, used as 'File Manager' menu class. */
class UIActionMenuFileManager : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuFileManager(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "File Manager"));
    }
};

class UIActionMenuFileManagerHostSubmenu : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuFileManagerHostSubmenu(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Host"));
    }
};

class UIActionMenuFileManagerGuestSubmenu : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuFileManagerGuestSubmenu(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Guest"));
    }
};

/** Simple action extension, used as 'Copy to Guest' in file manager action class. */
class UIActionMenuFileManagerCopyToGuest : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuFileManagerCopyToGuest(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/file_manager_copy_to_guest_24px.png", ":/file_manager_copy_to_guest_16px.png",
                         ":/file_manager_copy_to_guest_disabled_24px.png", ":/file_manager_copy_to_guest_disabled_16px.png"){}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("FileManagerCopyToGuest");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Copy to guest"));
        setShortcutScope(QApplication::translate("UIActionPool", "File Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Copy the selected object(s) from host to guest"));
        setToolTip(  QApplication::translate("UIActionPool", "Copy from Host to Guest")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Copy to Host' in file manager action class. */
class UIActionMenuFileManagerCopyToHost : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuFileManagerCopyToHost(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/file_manager_copy_to_host_24px.png", ":/file_manager_copy_to_host_16px.png",
                         ":/file_manager_copy_to_host_disabled_24px.png", ":/file_manager_copy_to_host_disabled_16px.png"){}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("FileManagerCopyToHost");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Copy to host"));
        setShortcutScope(QApplication::translate("UIActionPool", "File Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Copy the selected object(s) from guest to host"));
        setToolTip(  QApplication::translate("UIActionPool", "Copy from Guest to Host")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Toggle action extension, used to toggle 'File Manager Preferences' panel in file manager. */
class UIActionMenuFileManagerPreferences : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuFileManagerPreferences(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
        setIcon(UIIconPool::iconSetFull(":/file_manager_options_32px.png",          ":/file_manager_options_16px.png",
                                        ":/file_manager_options_disabled_32px.png", ":/file_manager_options_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToggleFileManagerOptionsPanel");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Preferences"));
        setShortcutScope(QApplication::translate("UIActionPool", "File Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Open pane with file manager preferences"));
        setToolTip(  QApplication::translate("UIActionPool", "Open Preferences Pane")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Toggle action extension, used to toggle 'File Manager Log' panel in file manager. */
class UIActionMenuFileManagerLog : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuFileManagerLog(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
        setIcon(UIIconPool::iconSetFull(":/file_manager_log_32px.png",          ":/file_manager_log_16px.png",
                                        ":/file_manager_log_disabled_32px.png", ":/file_manager_log_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToggleFileManagerLogPanel");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Log"));
        setShortcutScope(QApplication::translate("UIActionPool", "File Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Open pane with file manager log"));
        setToolTip(  QApplication::translate("UIActionPool", "Open Log Pane")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Toggle action extension, used to toggle 'File Manager Operations' panel in file manager. */
class UIActionMenuFileManagerOperations : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuFileManagerOperations(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
        setIcon(UIIconPool::iconSetFull(":/file_manager_operations_32px.png",          ":/file_manager_operations_16px.png",
                                        ":/file_manager_operations_disabled_32px.png", ":/file_manager_operations_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToggleFileManagerOperationsPanel");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Operations"));
        setShortcutScope(QApplication::translate("UIActionPool", "File Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Open pane with file manager operations"));
        setToolTip(  QApplication::translate("UIActionPool", "Open Operations Pane")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Toggle action extension, used to toggle 'File Manager Guest Session' panel in file manager. */
class UIActionMenuFileManagerGuestSession : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuFileManagerGuestSession(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
        setIcon(UIIconPool::iconSetFull(":/file_manager_session_32px.png",          ":/file_manager_session_16px.png",
                                        ":/file_manager_session_disabled_32px.png", ":/file_manager_session_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToggleFileManagerGuestSessionPanel");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Session"));
        setShortcutScope(QApplication::translate("UIActionPool", "File Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Toggle guest session panel of the file manager"));
        setToolTip(  QApplication::translate("UIActionPool", "Toggle Guest Session Panel")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform GoUp' in file manager action class. */
class UIActionMenuFileManagerGoUp : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuFileManagerGoUp(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/file_manager_go_up_24px.png", ":/file_manager_go_up_16px.png",
                         ":/file_manager_go_up_disabled_24px.png", ":/file_manager_go_up_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("FileManagerGoUp");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Go Up"));
        setShortcutScope(QApplication::translate("UIActionPool", "File Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Go one level up to parent folder"));
        setToolTip(  QApplication::translate("UIActionPool", "Go One Level Up")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform GoHome' in file manager action class. */
class UIActionMenuFileManagerGoHome : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuFileManagerGoHome(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/file_manager_go_home_24px.png", ":/file_manager_go_home_16px.png",
                         ":/file_manager_go_home_disabled_24px.png", ":/file_manager_go_home_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("FileManagerGoHome");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Go Home"));
        setShortcutScope(QApplication::translate("UIActionPool", "File Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Go to home folder"));
        setToolTip(  QApplication::translate("UIActionPool", "Go to Home Folder")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform GoForward' in file manager action class. */
class UIActionMenuFileManagerGoForward : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuFileManagerGoForward(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/file_manager_go_forward_24px.png", ":/file_manager_go_forward_16px.png",
                         ":/file_manager_go_forward_disabled_24px.png", ":/file_manager_go_forward_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("FileManagerGoForward");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Go Forward"));
        setShortcutScope(QApplication::translate("UIActionPool", "File Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Go forward"));
        setToolTip(  QApplication::translate("UIActionPool", "Go Forward")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform GoBackward' in file manager action class. */
class UIActionMenuFileManagerGoBackward : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuFileManagerGoBackward(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/file_manager_go_backward_24px.png", ":/file_manager_go_backward_16px.png",
                         ":/file_manager_go_backward_disabled_24px.png", ":/file_manager_go_backward_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("FileManagerGoBackward");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Go Backward"));
        setShortcutScope(QApplication::translate("UIActionPool", "File Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Go backward"));
        setToolTip(  QApplication::translate("UIActionPool", "Go Backward")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Delete' in file manager action class. */
class UIActionMenuFileManagerDelete : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuFileManagerDelete(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/file_manager_delete_24px.png", ":/file_manager_delete_16px.png",
                         ":/file_manager_delete_disabled_24px.png", ":/file_manager_delete_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("FileManagerDelete");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Delete"));
        setShortcutScope(QApplication::translate("UIActionPool", "File Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Delete selected file object(s)"));
        setToolTip(  QApplication::translate("UIActionPool", "Delete Selected Object(s)")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Refresh' in file manager action class. */
class UIActionMenuFileManagerRefresh : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuFileManagerRefresh(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/file_manager_refresh_24px.png", ":/file_manager_refresh_16px.png",
                         ":/file_manager_refresh_disabled_24px.png", ":/file_manager_refresh_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("FileManagerRefresh");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Refresh"));
        setShortcutScope(QApplication::translate("UIActionPool", "File Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Refresh"));
        setToolTip(  QApplication::translate("UIActionPool", "Refresh Contents")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Rename' in file manager action class. */
class UIActionMenuFileManagerRename : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuFileManagerRename(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/file_manager_rename_24px.png", ":/file_manager_rename_16px.png",
                         ":/file_manager_rename_disabled_24px.png", ":/file_manager_rename_disabled_16px.png"){}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("FileManagerRename");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Rename"));
        setShortcutScope(QApplication::translate("UIActionPool", "File Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Rename selected file object"));
        setToolTip(  QApplication::translate("UIActionPool", "Rename Selected Object")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Rename' in file manager action class. */
class UIActionMenuFileManagerCreateNewDirectory : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuFileManagerCreateNewDirectory(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/file_manager_new_directory_24px.png", ":/file_manager_new_directory_16px.png",
                         ":/file_manager_new_directory_disabled_24px.png", ":/file_manager_new_directory_disabled_16px.png"){}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("FileManagerCreateNewDirectory");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Create New Directory"));
        setShortcutScope(QApplication::translate("UIActionPool", "File Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Create New Directory"));
        setToolTip(  QApplication::translate("UIActionPool", "Create New Directory")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Copy' in file manager action class. */
class UIActionMenuFileManagerCopy : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuFileManagerCopy(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/file_manager_copy_24px.png", ":/file_manager_copy_16px.png",
                         ":/file_manager_copy_disabled_24px.png", ":/file_manager_copy_disabled_16px.png"){}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("FileManagerCopy");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Copy"));
        setShortcutScope(QApplication::translate("UIActionPool", "File Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Copy selected file object(s)"));
        setToolTip(  QApplication::translate("UIActionPool", "Copy Selected Object(s)")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Cut' in file manager action class. */
class UIActionMenuFileManagerCut : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuFileManagerCut(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/file_manager_cut_24px.png", ":/file_manager_cut_16px.png",
                         ":/file_manager_cut_disabled_24px.png", ":/file_manager_cut_disabled_16px.png"){}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("FileManagerCut");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Cut"));
        setShortcutScope(QApplication::translate("UIActionPool", "File Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Cut selected file object(s)"));
        setToolTip(  QApplication::translate("UIActionPool", "Cut Selected Object(s)")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform Paste' in file manager action class. */
class UIActionMenuFileManagerPaste : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuFileManagerPaste(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/file_manager_paste_24px.png", ":/file_manager_paste_16px.png",
                         ":/file_manager_paste_disabled_24px.png", ":/file_manager_paste_disabled_16px.png"){}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("FileManagerPaste");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Paste"));
        setShortcutScope(QApplication::translate("UIActionPool", "File Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Paste copied/cut file object(s)"));
        setToolTip(  QApplication::translate("UIActionPool", "Paste Copied/Cut Object(s)")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Select All' in file manager action class. */
class UIActionMenuFileManagerSelectAll : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuFileManagerSelectAll(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/file_manager_select_all_24px.png", ":/file_manager_select_all_16px.png",
                         ":/file_manager_select_all_disabled_24px.png", ":/file_manager_select_all_disabled_16px.png"){}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("FileManagerSelectAll");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Select All"));
        setShortcutScope(QApplication::translate("UIActionPool", "File Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Select all files objects"));
        setToolTip(  QApplication::translate("UIActionPool", "Select All Objects")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Invert Selection' in file manager action class. */
class UIActionMenuFileManagerInvertSelection : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuFileManagerInvertSelection(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/file_manager_invert_selection_24px.png", ":/file_manager_invert_selection_16px.png",
                         ":/file_manager_invert_selection_disabled_24px.png", ":/file_manager_invert_selection_disabled_16px.png"){}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("FileManagerInvertSelection");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Invert Selection"));
        setShortcutScope(QApplication::translate("UIActionPool", "File Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Invert the current selection"));
        setToolTip(  QApplication::translate("UIActionPool", "Invert Current Selection")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Show Properties' in file manager action class. */
class UIActionMenuFileManagerShowProperties : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuFileManagerShowProperties(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/file_manager_properties_24px.png", ":/file_manager_properties_16px.png",
                         ":/file_manager_properties_disabled_24px.png", ":/file_manager_properties_disabled_16px.png"){}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("FileManagerShowProperties");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Show Properties"));
        setShortcutScope(QApplication::translate("UIActionPool", "File Manager"));
        setStatusTip(QApplication::translate("UIActionPool", "Show the properties of currently selected file object(s)"));
        setToolTip(  QApplication::translate("UIActionPool", "Show Properties of Current Object(s)")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Menu action extension, used as 'VISO Creator' menu class. */
class UIActionMenuVISOCreator : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuVISOCreator(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "VISO Creator"));
    }
};

/** Toggle action extension, used to toggle 'VISO Creator Preferences' pane in VISO Creator. */
class UIActionToggleVISOCreatorPreferences : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionToggleVISOCreatorPreferences(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
        setIcon(UIIconPool::iconSetFull(":/file_manager_options_32px.png",
                                        ":/%file_manager_options_16px.png",
                                        ":/file_manager_options_disabled_32px.png",
                                        ":/file_manager_options_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToggleVISOCreatorPreferences");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Preferences"));
        setShortcutScope(QApplication::translate("UIActionPool", "VISO Creator"));
        setStatusTip(QApplication::translate("UIActionPool", "Open pane with VISO Creator preferences"));
        setToolTip(QApplication::translate("UIActionPool", "Open Preferences Pane")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

class UIActionSimpleVISOCreatorAdd : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleVISOCreatorAdd(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/file_manager_copy_to_guest_24px.png",
                         ":/file_manager_copy_to_guest_16px.png",
                         ":/file_manager_copy_to_guest_disabled_24px.png",
                         ":/file_manager_copy_to_guest_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("VISOAddItem");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Add"));
        setShortcutScope(QApplication::translate("UIActionPool", "VISO Creator"));
        setStatusTip(QApplication::translate("UIActionPool", "Add selected item(s) to VISO"));
        setToolTip(QApplication::translate("UIActionPool", "Add Item(s) to VISO")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

class UIActionSimpleVISOCreatorRemove : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleVISOCreatorRemove(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/file_manager_delete_24px.png",
                         ":/file_manager_delete_16px.png",
                         ":/file_manager_delete_disabled_24px.png",
                         ":/file_manager_delete_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("VISORemoveItem");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Remove"));
        setShortcutScope(QApplication::translate("UIActionPool", "VISO Creator"));
        setStatusTip(QApplication::translate("UIActionPool", "Remove selected item(s) from VISO"));
        setToolTip(QApplication::translate("UIActionPool", "Remove Selected Item(s) From VISO")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

class UIActionSimpleVISOCreatorRestore : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleVISOCreatorRestore(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/file_manager_restore_24px.png",
                         ":/file_manager_restore_16px.png",
                         ":/file_manager_restore_disabled_24px.png",
                         ":/file_manager_restore_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("VISORestoreItem");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Restore"));
        setShortcutScope(QApplication::translate("UIActionPool", "VISO Creator"));
        setStatusTip(QApplication::translate("UIActionPool", "Restore selected item(s)"));
        setToolTip(QApplication::translate("UIActionPool", "Restore Selected Item(s)")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

class UIActionSimpleVISOCreatorCreateNewDirectory : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleVISOCreatorCreateNewDirectory(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/file_manager_new_directory_24px.png",
                         ":/file_manager_new_directory_16px.png",
                         ":/file_manager_new_directory_disabled_24px.png",
                         ":/file_manager_new_directory_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("VISONewDirectory");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&New Directory"));
        setShortcutScope(QApplication::translate("UIActionPool", "VISO Creator"));
        setStatusTip(QApplication::translate("UIActionPool", "Create a new directory under the current location"));
        setToolTip(QApplication::translate("UIActionPool", "Create New Directory")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

class UIActionSimpleVISOCreatorRename : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleVISOCreatorRename(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/file_manager_rename_24px.png",
                         ":/file_manager_rename_16px.png",
                         ":/file_manager_rename_disabled_24px.png",
                         ":/file_manager_rename_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("VISORenameItem");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Rename"));
        setShortcutScope(QApplication::translate("UIActionPool", "VISO Creator"));
        setStatusTip(QApplication::translate("UIActionPool", "Rename the selected object"));
        setToolTip(QApplication::translate("UIActionPool", "Rename Selected VISO File Object")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

class UIActionSimpleVISOCreatorReset : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleVISOCreatorReset(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/cd_remove_16px.png", ":/cd_remove_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("VISOReset");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "R&eset"));
        setShortcutScope(QApplication::translate("UIActionPool", "VISO Creator"));
        setStatusTip(QApplication::translate("UIActionPool", "Reset the VISO content"));
        setToolTip(QApplication::translate("UIActionPool", "Reset the VISO Content")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

class UIActionSimpleVISOCreatorOpen : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleVISOCreatorOpen(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/cd_open_16px.png", ":/cd_open_32px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("VISOOpen");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Open"));
        setShortcutScope(QApplication::translate("UIActionPool", "VISO Creator"));
        setStatusTip(QApplication::translate("UIActionPool", "Open the VISO content"));
        setToolTip(QApplication::translate("UIActionPool", "Open the VISO Content")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

class UIActionSimpleVISOCreatorSaveAs : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleVISOCreatorSaveAs(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/cd_write_16px.png", ":/cd_write_32px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("VISOSaveAs");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Save As"));
        setShortcutScope(QApplication::translate("UIActionPool", "VISO Creator"));
        setStatusTip(QApplication::translate("UIActionPool", "Save the VISO content"));
        setToolTip(QApplication::translate("UIActionPool", "Save the VISO Content")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

class UIActionSimpleVISOCreatorImportISO : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleVISOCreatorImportISO(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/cd_add_16px.png", ":/cd_add_32px.png", ":/cd_add_disabled_16px.png", ":/cd_add_disabled_32px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ISOImport");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Import ISO"));
        setShortcutScope(QApplication::translate("UIActionPool", "VISO Creator"));
        setStatusTip(QApplication::translate("UIActionPool", "Import ISO into the VISO content"));
        setToolTip(QApplication::translate("UIActionPool", "Import Selected ISO Into the VISO Content")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

class UIActionSimpleVISOCreatorRemoveISO : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleVISOCreatorRemoveISO(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/cd_remove_16px.png", ":/cd_remove_32px.png", ":/cd_remove_disabled_16px.png", ":/cd_remove_disabled_32px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ISORemove");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Remove ISO"));
        setShortcutScope(QApplication::translate("UIActionPool", "VISO Creator"));
        setStatusTip(QApplication::translate("UIActionPool", "Remove the imported ISO from the VISO content"));
        setToolTip(QApplication::translate("UIActionPool", "Remove the Imported ISO From the VISO Content")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform GoUp' in VISO creator action class. */
class UIActionSimpleVISOCreatorGoUp : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleVISOCreatorGoUp(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/file_manager_go_up_24px.png", ":/file_manager_go_up_16px.png",
                         ":/file_manager_go_up_disabled_24px.png", ":/file_manager_go_up_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("VISOCreatorGoUp");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Go Up"));
        setShortcutScope(QApplication::translate("UIActionPool", "VISO Creator"));
        setStatusTip(QApplication::translate("UIActionPool", "Go one level up to parent folder"));
        setToolTip(  QApplication::translate("UIActionPool", "Go One Level Up")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform GoHome' in VISO creator action class. */
class UIActionSimpleVISOCreatorGoHome : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleVISOCreatorGoHome(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/file_manager_go_home_24px.png", ":/file_manager_go_home_16px.png",
                         ":/file_manager_go_home_disabled_24px.png", ":/file_manager_go_home_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("VISOCreatorGoHome");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Go Home"));
        setShortcutScope(QApplication::translate("UIActionPool", "VISO Creator"));
        setStatusTip(QApplication::translate("UIActionPool", "Go to home folder"));
        setToolTip(  QApplication::translate("UIActionPool", "Go to Home Folder")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform GoForward' in VISO creator action class. */
class UIActionSimpleVISOCreatorGoForward : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleVISOCreatorGoForward(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/file_manager_go_forward_24px.png", ":/file_manager_go_forward_16px.png",
                         ":/file_manager_go_forward_disabled_24px.png", ":/file_manager_go_forward_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("VISOCreatorGoForward");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Go Forward"));
        setShortcutScope(QApplication::translate("UIActionPool", "VISO Creator"));
        setStatusTip(QApplication::translate("UIActionPool", "Go forward"));
        setToolTip(  QApplication::translate("UIActionPool", "Go Forward")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Perform GoBackward' in VISO creator action class. */
class UIActionSimpleVISOCreatorGoBackward : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionSimpleVISOCreatorGoBackward(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/file_manager_go_backward_24px.png", ":/file_manager_go_backward_16px.png",
                         ":/file_manager_go_backward_disabled_24px.png", ":/file_manager_go_backward_disabled_16px.png")
    {}

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("VISOCreatorGoBackward");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "Go Backward"));
        setShortcutScope(QApplication::translate("UIActionPool", "VISO Creator"));
        setStatusTip(QApplication::translate("UIActionPool", "Go backward"));
        setToolTip(  QApplication::translate("UIActionPool", "Go Backward")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Menu action extension, used as 'Menu Selector' menu class. */
class UIActionMenuMediumSelector : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuMediumSelector(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Medium Selector"));
    }
};

/** Simple action extension, used as 'Add' action class. */
class UIActionMenuMediumSelectorAddHD  : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuMediumSelectorAddHD(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/hd_add_32px.png",  ":/hd_add_16px.png",
                         ":/hd_add_disabled_32px.png", ":/hd_add_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("MediumSelectorAddHD");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Add..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Medium Selector"));
        setStatusTip(QApplication::translate("UIActionPool", "Add existing disk image file"));
        setToolTip(  QApplication::translate("UIActionPool", "Add Disk Image File")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Add' action class. */
class UIActionMenuMediumSelectorAddCD  : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuMediumSelectorAddCD(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/cd_add_32px.png",  ":/cd_add_16px.png",
                         ":/cd_add_disabled_32px.png", ":/cd_add_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("MediumSelectorAddCD");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Add..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Medium Selector"));
        setStatusTip(QApplication::translate("UIActionPool", "Add existing disk image file"));
        setToolTip(  QApplication::translate("UIActionPool", "Add Disk Image File")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'EditCD' action class. */
class UIActionMenuMediumSelectorEditCD  : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuMediumSelectorEditCD(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/cd_edit_32px.png",  ":/cd_edit_16px.png",
                         ":/cd_edit_disabled_32px.png", ":/cd_edit_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("MediumSelectorEditCD");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Edit..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Medium Selector"));
        setStatusTip(QApplication::translate("UIActionPool", "Edit existing disk image file"));
        setToolTip(  QApplication::translate("UIActionPool", "Edit Disk Image File")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Add' action class. */
class UIActionMenuMediumSelectorAddFD  : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuMediumSelectorAddFD(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/fd_add_32px.png",  ":/fd_add_16px.png",
                         ":/fd_add_disabled_32px.png", ":/fd_add_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("MediumSelectorAddFD");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Add..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Medium Selector"));
        setStatusTip(QApplication::translate("UIActionPool", "Add existing disk image file"));
        setToolTip(  QApplication::translate("UIActionPool", "Add Disk Image File")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Create' action class. */
class UIActionMenuMediumSelectorCreateHD  : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuMediumSelectorCreateHD(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/hd_create_32px.png",  ":/hd_create_16px.png",
                         ":/hd_create_disabled_32px.png", ":/hd_create_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("MediumSelectorCreateHD");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Create..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Medium Selector"));
        setStatusTip(QApplication::translate("UIActionPool", "Create a new disk image file"));
        setToolTip(  QApplication::translate("UIActionPool", "Create Disk Image File")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Create' action class. */
class UIActionMenuMediumSelectorCreateCD  : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuMediumSelectorCreateCD(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/cd_create_32px.png",  ":/cd_create_16px.png",
                         ":/cd_create_disabled_32px.png", ":/cd_create_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("MediumSelectorCreateCD");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Create..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Medium Selector"));
        setStatusTip(QApplication::translate("UIActionPool", "Create a new disk image file"));
        setToolTip(  QApplication::translate("UIActionPool", "Create Disk Image File")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Create' action class. */
class UIActionMenuMediumSelectorCreateFD  : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuMediumSelectorCreateFD(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/fd_create_32px.png",  ":/fd_create_16px.png",
                         ":/fd_create_disabled_32px.png", ":/fd_create_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("MediumSelectorCreateFD");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Create..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Medium Selector"));
        setStatusTip(QApplication::translate("UIActionPool", "Create a new disk image file"));
        setToolTip(  QApplication::translate("UIActionPool", "Create Disk Image File")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Create' action class. */
class UIActionMenuMediumSelectorRefresh  : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuMediumSelectorRefresh(UIActionPool *pParent)
        : UIActionSimple(pParent, ":/refresh_32px.png",  ":/refresh_16px.png",
                         ":/refresh_disabled_32px.png", ":/refresh_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("MediumSelectorRefresh");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Refresh..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Medium Selector"));
        setStatusTip(QApplication::translate("UIActionPool", "Refresh disk images"));
        setToolTip(  QApplication::translate("UIActionPool", "Refresh Disk Images")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Menu action extension, used as 'Activity' menu class. */
class UIActionMenuSelectorActivity : public UIActionMenu
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuSelectorActivity(UIActionPool *pParent)
        : UIActionMenu(pParent)
    {}

protected:

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Activity"));
    }
};

/** Simple action extension, used as 'Perform Export' action class. */
class UIActionMenuSelectorActivityPerformExport : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuSelectorActivityPerformExport(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/performance_monitor_export_32px.png", ":/performance_monitor_export_16px.png",
                         ":/performance_monitor_export_disabled_32px.png", ":/performance_monitor_export_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("VMActivityMonitorExportCharts");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Export..."));
        setShortcutScope(QApplication::translate("UIActionPool", "VM Activity Monitor"));
        setStatusTip(QApplication::translate("UIActionPool", "Export the chart data into a text file"));
        setToolTip(  QApplication::translate("UIActionPool", "Export Data to File")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'To VM Activity Overview' action class. */
class UIActionMenuSelectorActivityToVMActivityOverview : public UIActionSimple
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuSelectorActivityToVMActivityOverview(UIActionPool *pParent)
        : UIActionSimple(pParent,
                         ":/resources_monitor_24px.png", ":/resource_monitor_16px.png",
                         ":/resource_monitor_disabled_24px.png", ":/resource_monitor_disabled_16px.png")
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToVMActivityOverview");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Activity Overview..."));
        setShortcutScope(QApplication::translate("UIActionPool", "Activity Monitor"));
        setStatusTip(QApplication::translate("UIActionPool", "Navigate to the vm activity overview"));
        setToolTip(  QApplication::translate("UIActionPool", "Navigate to VM Activity Overview")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};

/** Simple action extension, used as 'Toggle Pane Preferences' action class. */
class UIActionMenuActivityPreferences : public UIActionToggle
{
    Q_OBJECT;

public:

    /** Constructs action passing @a pParent to the base-class. */
    UIActionMenuActivityPreferences(UIActionPool *pParent)
        : UIActionToggle(pParent)
    {
        setShortcutContext(Qt::WidgetWithChildrenShortcut);
        setIcon(UIIconPool::iconSetFull(":/performance_monitor_preferences_32px.png",          ":/performance_monitor_preferences_16px.png",
                                        ":/performance_monitor_preferences_disabled_32px.png", ":/performance_monitor_preferences_disabled_16px.png"));
    }

protected:

    /** Returns shortcut extra-data ID. */
    virtual QString shortcutExtraDataID() const RT_OVERRIDE
    {
        return QString("ToggleActivityMonitorPreferences");
    }

    /** Returns default shortcut. */
    virtual QKeySequence defaultShortcut(UIType) const RT_OVERRIDE
    {
        return QKeySequence("");
    }

    /** Handles translation event. */
    virtual void retranslateUi() RT_OVERRIDE
    {
        setName(QApplication::translate("UIActionPool", "&Preferences"));
        setShortcutScope(QApplication::translate("UIActionPool", "Activity Monitor"));
        setStatusTip(QApplication::translate("UIActionPool", "Open pane with activity monitor preferences"));
        setToolTip(  QApplication::translate("UIActionPool", "Open Preferences Pane")
                   + (shortcut().isEmpty() ? QString() : QString(" (%1)").arg(shortcut().toString())));
    }
};


/*********************************************************************************************************************************
*   Class UIActionPool implementation.                                                                                           *
*********************************************************************************************************************************/

/* static */
UIActionPool *UIActionPool::create(UIType enmType)
{
    UIActionPool *pActionPool = 0;
    switch (enmType)
    {
        case UIType_ManagerUI: pActionPool = new UIActionPoolManager; break;
        case UIType_RuntimeUI: pActionPool = new UIActionPoolRuntime; break;
        default: AssertFailedReturn(0);
    }
    AssertPtrReturn(pActionPool, 0);
    pActionPool->prepare();
    return pActionPool;
}

/* static */
void UIActionPool::destroy(UIActionPool *pActionPool)
{
    AssertPtrReturnVoid(pActionPool);
    pActionPool->cleanup();
    delete pActionPool;
}

/* static */
void UIActionPool::createTemporary(UIType enmType)
{
    UIActionPool *pActionPool = 0;
    switch (enmType)
    {
        case UIType_ManagerUI: pActionPool = new UIActionPoolManager(true); break;
        case UIType_RuntimeUI: pActionPool = new UIActionPoolRuntime(true); break;
        default: AssertFailedReturnVoid();
    }
    AssertPtrReturnVoid(pActionPool);
    pActionPool->prepare();
    pActionPool->cleanup();
    delete pActionPool;
}

UIActionPoolManager *UIActionPool::toManager()
{
    return qobject_cast<UIActionPoolManager*>(this);
}

UIActionPoolRuntime *UIActionPool::toRuntime()
{
    return qobject_cast<UIActionPoolRuntime*>(this);
}

UIAction *UIActionPool::action(int iIndex) const
{
    AssertReturn(m_pool.contains(iIndex), 0);
    return m_pool.value(iIndex);
}

QList<UIAction*> UIActionPool::actions() const
{
    return m_pool.values();
}

QActionGroup *UIActionPool::actionGroup(int iIndex) const
{
    AssertReturn(m_groupPool.contains(iIndex), 0);
    return m_groupPool.value(iIndex);
}

bool UIActionPool::isAllowedInMenuBar(UIExtraDataMetaDefs::MenuType enmType) const
{
    foreach (const UIExtraDataMetaDefs::MenuType &enmRestriction, m_restrictedMenus.values())
        if (enmRestriction & enmType)
            return false;
    return true;
}

void UIActionPool::setRestrictionForMenuBar(UIActionRestrictionLevel enmLevel, UIExtraDataMetaDefs::MenuType enmRestriction)
{
    m_restrictedMenus[enmLevel] = enmRestriction;
    updateMenus();
}

bool UIActionPool::isAllowedInMenuApplication(UIExtraDataMetaDefs::MenuApplicationActionType enmType) const
{
    foreach (const UIExtraDataMetaDefs::MenuApplicationActionType &enmRestriction, m_restrictedActionsMenuApplication.values())
        if (enmRestriction & enmType)
            return false;
    return true;
}

void UIActionPool::setRestrictionForMenuApplication(UIActionRestrictionLevel enmLevel, UIExtraDataMetaDefs::MenuApplicationActionType enmRestriction)
{
    m_restrictedActionsMenuApplication[enmLevel] = enmRestriction;
    m_invalidations << UIActionIndex_M_Application;
}

#ifdef VBOX_WS_MAC
bool UIActionPool::isAllowedInMenuWindow(UIExtraDataMetaDefs::MenuWindowActionType enmType) const
{
    foreach (const UIExtraDataMetaDefs::MenuWindowActionType &enmRestriction, m_restrictedActionsMenuWindow.values())
        if (enmRestriction & enmType)
            return false;
    return true;
}

void UIActionPool::setRestrictionForMenuWindow(UIActionRestrictionLevel enmLevel, UIExtraDataMetaDefs::MenuWindowActionType enmRestriction)
{
    m_restrictedActionsMenuWindow[enmLevel] = enmRestriction;
    m_invalidations << UIActionIndex_M_Window;
}
#endif /* VBOX_WS_MAC */

bool UIActionPool::isAllowedInMenuHelp(UIExtraDataMetaDefs::MenuHelpActionType enmType) const
{
    foreach (const UIExtraDataMetaDefs::MenuHelpActionType &enmRestriction, m_restrictedActionsMenuHelp.values())
        if (enmRestriction & enmType)
            return false;
    return true;
}

void UIActionPool::setRestrictionForMenuHelp(UIActionRestrictionLevel enmLevel, UIExtraDataMetaDefs::MenuHelpActionType enmRestriction)
{
    m_restrictedActionsMenuHelp[enmLevel] = enmRestriction;
    m_invalidations << UIActionIndex_Menu_Help;
}

bool UIActionPool::processHotKey(const QKeySequence &key)
{
    /* Iterate through the whole list of keys: */
    foreach (const int &iKey, m_pool.keys())
    {
        /* Get current action: */
        UIAction *pAction = m_pool.value(iKey);
        /* Skip menus/separators: */
        if (pAction->type() == UIActionType_Menu)
            continue;
        /* Get the hot-key of the current action: */
        const QString strHotKey = gShortcutPool->shortcut(this, pAction).primaryToPortableText();
        if (pAction->isEnabled() && pAction->isAllowed() && !strHotKey.isEmpty())
        {
            if (key.matches(QKeySequence(strHotKey)) == QKeySequence::ExactMatch)
            {
                /* We asynchronously post a special event instead of calling
                 * pAction->trigger() directly, to let key presses and
                 * releases be processed correctly by Qt first.
                 * Note: we assume that nobody will delete the menu item
                 * corresponding to the key sequence, so that the pointer to
                 * menu data posted along with the event will remain valid in
                 * the event handler, at least until the main window is closed. */
                QApplication::postEvent(this, new ActivateActionEvent(pAction));
                return true;
            }
        }
    }
    return false;
}

void UIActionPool::sltHandleMenuPrepare()
{
    /* Make sure menu is valid: */
    AssertPtrReturnVoid(sender());
    UIMenu *pMenu = qobject_cast<UIMenu*>(sender());
    AssertPtrReturnVoid(pMenu);
    /* Make sure action is valid: */
    AssertPtrReturnVoid(pMenu->menuAction());
    UIAction *pAction = qobject_cast<UIAction*>(pMenu->menuAction());
    AssertPtrReturnVoid(pAction);

    /* Determine action index: */
    const int iIndex = m_pool.key(pAction);

    /* Update menu if necessary: */
    updateMenu(iIndex);

    /* Notify listeners about menu prepared: */
    emit sigNotifyAboutMenuPrepare(iIndex, pMenu);
}

#ifdef VBOX_WS_MAC
void UIActionPool::sltActionHovered()
{
    /* Acquire sender action: */
    UIAction *pAction = qobject_cast<UIAction*>(sender());
    AssertPtrReturnVoid(pAction);
    //printf("Action hovered: {%s}\n", pAction->name().toUtf8().constData());

    /* Notify listener about action hevering: */
    emit sigActionHovered(pAction);
}
#endif /* VBOX_WS_MAC */

UIActionPool::UIActionPool(UIType enmType, bool fTemporary /* = false */)
    : m_enmType(enmType)
    , m_fTemporary(fTemporary)
{
}

void UIActionPool::preparePool()
{
    /* Create 'Application' actions: */
    m_pool[UIActionIndex_M_Application] = new UIActionMenuApplication(this);
#ifdef VBOX_WS_MAC
    m_pool[UIActionIndex_M_Application_S_About] = new UIActionSimpleAbout(this);
#endif
    m_pool[UIActionIndex_M_Application_S_Preferences] = new UIActionSimplePreferences(this);
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
    m_pool[UIActionIndex_M_Application_S_CheckForUpdates] = new UIActionSimpleCheckForUpdates(this);
#endif
    m_pool[UIActionIndex_M_Application_S_ResetWarnings] = new UIActionSimpleResetWarnings(this);
    m_pool[UIActionIndex_M_Application_S_Close] = new UIActionSimplePerformClose(this);

#ifdef VBOX_WS_MAC
    /* Create 'Window' actions: */
    m_pool[UIActionIndex_M_Window] = new UIActionMenuWindow(this);
    m_pool[UIActionIndex_M_Window_S_Minimize] = new UIActionSimpleMinimize(this);
#endif

    /* Create 'Help' actions: */
    m_pool[UIActionIndex_Menu_Help] = new UIActionMenuHelp(this);
    m_pool[UIActionIndex_Simple_Contents] = new UIActionSimpleContents(this);
    m_pool[UIActionIndex_Simple_OnlineDocumentation] = new UIActionSimpleOnlineDocumentation(this);
    m_pool[UIActionIndex_Simple_WebSite] = new UIActionSimpleWebSite(this);
    m_pool[UIActionIndex_Simple_BugTracker] = new UIActionSimpleBugTracker(this);
    m_pool[UIActionIndex_Simple_Forums] = new UIActionSimpleForums(this);
    m_pool[UIActionIndex_Simple_Oracle] = new UIActionSimpleOracle(this);
#ifndef VBOX_WS_MAC
    m_pool[UIActionIndex_Simple_About] = new UIActionSimpleAbout(this);
#endif

    /* Create 'Log Viewer' actions: */
    m_pool[UIActionIndex_M_LogWindow] = new UIActionMenuSelectorLog(this);
    m_pool[UIActionIndex_M_Log] = new UIActionMenuSelectorLog(this);
    m_pool[UIActionIndex_M_Log_T_Find] = new UIActionMenuSelectorLogTogglePaneFind(this);
    m_pool[UIActionIndex_M_Log_T_Filter] = new UIActionMenuSelectorLogTogglePaneFilter(this);
    m_pool[UIActionIndex_M_Log_T_Bookmark] = new UIActionMenuSelectorLogTogglePaneBookmark(this);
    m_pool[UIActionIndex_M_Log_T_Preferences] = new UIActionMenuSelectorLogTogglePanePreferences(this);
    m_pool[UIActionIndex_M_Log_S_Refresh] = new UIActionMenuSelectorLogPerformRefresh(this);
    m_pool[UIActionIndex_M_Log_S_Reload] = new UIActionMenuSelectorLogPerformReload(this);
    m_pool[UIActionIndex_M_Log_S_Save] = new UIActionMenuSelectorLogPerformSave(this);

    /* Create 'Performance Monitor' actions: */
    m_pool[UIActionIndex_M_Activity] = new UIActionMenuSelectorActivity(this);
    m_pool[UIActionIndex_M_Activity_S_Export] = new UIActionMenuSelectorActivityPerformExport(this);
    m_pool[UIActionIndex_M_Activity_S_ToVMActivityOverview] = new UIActionMenuSelectorActivityToVMActivityOverview(this);
    m_pool[UIActionIndex_M_Activity_T_Preferences] = new UIActionMenuActivityPreferences(this);

    /* Create 'File Manager' actions: */
    m_pool[UIActionIndex_M_FileManager] = new UIActionMenuFileManager(this);
    m_pool[UIActionIndex_M_FileManager_M_HostSubmenu] = new UIActionMenuFileManagerHostSubmenu(this);
    m_pool[UIActionIndex_M_FileManager_M_GuestSubmenu] = new UIActionMenuFileManagerGuestSubmenu(this);
    m_pool[UIActionIndex_M_FileManager_S_CopyToGuest] = new  UIActionMenuFileManagerCopyToGuest(this);
    m_pool[UIActionIndex_M_FileManager_S_CopyToHost] = new  UIActionMenuFileManagerCopyToHost(this);
    m_pool[UIActionIndex_M_FileManager_T_Preferences] = new UIActionMenuFileManagerPreferences(this);
    m_pool[UIActionIndex_M_FileManager_T_Log] = new UIActionMenuFileManagerLog(this);
    m_pool[UIActionIndex_M_FileManager_T_Operations] = new UIActionMenuFileManagerOperations(this);
    m_pool[UIActionIndex_M_FileManager_T_GuestSession] = new UIActionMenuFileManagerGuestSession(this);
    m_pool[UIActionIndex_M_FileManager_S_Host_GoUp] = new UIActionMenuFileManagerGoUp(this);
    m_pool[UIActionIndex_M_FileManager_S_Guest_GoUp] = new UIActionMenuFileManagerGoUp(this);
    m_pool[UIActionIndex_M_FileManager_S_Host_GoHome] = new UIActionMenuFileManagerGoHome(this);
    m_pool[UIActionIndex_M_FileManager_S_Guest_GoHome] = new UIActionMenuFileManagerGoHome(this);
    m_pool[UIActionIndex_M_FileManager_S_Host_GoForward] = new UIActionMenuFileManagerGoForward(this);
    m_pool[UIActionIndex_M_FileManager_S_Guest_GoForward] = new UIActionMenuFileManagerGoForward(this);
    m_pool[UIActionIndex_M_FileManager_S_Host_GoBackward] = new UIActionMenuFileManagerGoBackward(this);
    m_pool[UIActionIndex_M_FileManager_S_Guest_GoBackward] = new UIActionMenuFileManagerGoBackward(this);
    m_pool[UIActionIndex_M_FileManager_S_Host_Refresh] = new UIActionMenuFileManagerRefresh(this);
    m_pool[UIActionIndex_M_FileManager_S_Guest_Refresh] = new UIActionMenuFileManagerRefresh(this);
    m_pool[UIActionIndex_M_FileManager_S_Host_Delete] = new UIActionMenuFileManagerDelete(this);
    m_pool[UIActionIndex_M_FileManager_S_Guest_Delete] = new UIActionMenuFileManagerDelete(this);
    m_pool[UIActionIndex_M_FileManager_S_Host_Rename] = new UIActionMenuFileManagerRename(this);
    m_pool[UIActionIndex_M_FileManager_S_Guest_Rename] = new UIActionMenuFileManagerRename(this);
    m_pool[UIActionIndex_M_FileManager_S_Host_CreateNewDirectory] = new UIActionMenuFileManagerCreateNewDirectory(this);
    m_pool[UIActionIndex_M_FileManager_S_Guest_CreateNewDirectory] = new UIActionMenuFileManagerCreateNewDirectory(this);
    m_pool[UIActionIndex_M_FileManager_S_Host_Copy] = new UIActionMenuFileManagerCopy(this);
    m_pool[UIActionIndex_M_FileManager_S_Guest_Copy] = new UIActionMenuFileManagerCopy(this);
    m_pool[UIActionIndex_M_FileManager_S_Host_Cut] = new UIActionMenuFileManagerCut(this);
    m_pool[UIActionIndex_M_FileManager_S_Guest_Cut] = new UIActionMenuFileManagerCut(this);
    m_pool[UIActionIndex_M_FileManager_S_Host_Paste] = new UIActionMenuFileManagerPaste(this);
    m_pool[UIActionIndex_M_FileManager_S_Guest_Paste] = new UIActionMenuFileManagerPaste(this);
    m_pool[UIActionIndex_M_FileManager_S_Host_SelectAll] = new UIActionMenuFileManagerSelectAll(this);
    m_pool[UIActionIndex_M_FileManager_S_Guest_SelectAll] = new UIActionMenuFileManagerSelectAll(this);
    m_pool[UIActionIndex_M_FileManager_S_Host_InvertSelection] = new UIActionMenuFileManagerInvertSelection(this);
    m_pool[UIActionIndex_M_FileManager_S_Guest_InvertSelection] = new UIActionMenuFileManagerInvertSelection(this);
    m_pool[UIActionIndex_M_FileManager_S_Host_ShowProperties] = new UIActionMenuFileManagerShowProperties(this);
    m_pool[UIActionIndex_M_FileManager_S_Guest_ShowProperties] = new UIActionMenuFileManagerShowProperties(this);

    /* Create VISO Creator actions: */
    m_pool[UIActionIndex_M_VISOCreator] = new UIActionMenuVISOCreator(this);
    m_pool[UIActionIndex_M_VISOCreator_TogglePreferences] = new UIActionToggleVISOCreatorPreferences(this);
    m_pool[UIActionIndex_M_VISOCreator_Add] = new UIActionSimpleVISOCreatorAdd(this);
    m_pool[UIActionIndex_M_VISOCreator_Remove] = new UIActionSimpleVISOCreatorRemove(this);
    m_pool[UIActionIndex_M_VISOCreator_Restore] = new UIActionSimpleVISOCreatorRestore(this);
    m_pool[UIActionIndex_M_VISOCreator_CreateNewDirectory] = new UIActionSimpleVISOCreatorCreateNewDirectory(this);
    m_pool[UIActionIndex_M_VISOCreator_Rename] = new UIActionSimpleVISOCreatorRename(this);
    m_pool[UIActionIndex_M_VISOCreator_Reset] = new UIActionSimpleVISOCreatorReset(this);
    m_pool[UIActionIndex_M_VISOCreator_Open] = new UIActionSimpleVISOCreatorOpen(this);
    m_pool[UIActionIndex_M_VISOCreator_SaveAs] = new UIActionSimpleVISOCreatorSaveAs(this);
    m_pool[UIActionIndex_M_VISOCreator_ImportISO] = new UIActionSimpleVISOCreatorImportISO(this);
    m_pool[UIActionIndex_M_VISOCreator_RemoveISO] = new UIActionSimpleVISOCreatorRemoveISO(this);
    m_pool[UIActionIndex_M_VISOCreator_VisoContent_GoHome] = new UIActionSimpleVISOCreatorGoHome(this);
    m_pool[UIActionIndex_M_VISOCreator_VisoContent_GoUp] = new UIActionSimpleVISOCreatorGoUp(this);
    m_pool[UIActionIndex_M_VISOCreator_VisoContent_GoForward] = new UIActionSimpleVISOCreatorGoForward(this);
    m_pool[UIActionIndex_M_VISOCreator_VisoContent_GoBackward] = new UIActionSimpleVISOCreatorGoBackward(this);
    m_pool[UIActionIndex_M_VISOCreator_Host_GoHome] = new UIActionSimpleVISOCreatorGoHome(this);
    m_pool[UIActionIndex_M_VISOCreator_Host_GoUp] = new UIActionSimpleVISOCreatorGoUp(this);
    m_pool[UIActionIndex_M_VISOCreator_Host_GoForward] = new UIActionSimpleVISOCreatorGoForward(this);
    m_pool[UIActionIndex_M_VISOCreator_Host_GoBackward] = new UIActionSimpleVISOCreatorGoBackward(this);

    /* Medium Selector actions: */
    m_pool[UIActionIndex_M_MediumSelector] = new UIActionMenuMediumSelector(this);
    m_pool[UIActionIndex_M_MediumSelector_AddHD] = new UIActionMenuMediumSelectorAddHD(this);
    m_pool[UIActionIndex_M_MediumSelector_AddCD] = new UIActionMenuMediumSelectorAddCD(this);
    m_pool[UIActionIndex_M_MediumSelector_AddFD] = new UIActionMenuMediumSelectorAddFD(this);
    m_pool[UIActionIndex_M_MediumSelector_CreateHD] = new UIActionMenuMediumSelectorCreateHD(this);
    m_pool[UIActionIndex_M_MediumSelector_CreateCD] = new UIActionMenuMediumSelectorCreateCD(this);
    m_pool[UIActionIndex_M_MediumSelector_CreateFD] = new UIActionMenuMediumSelectorCreateFD(this);
    m_pool[UIActionIndex_M_MediumSelector_EditCD] = new UIActionMenuMediumSelectorEditCD(this);
    m_pool[UIActionIndex_M_MediumSelector_Refresh] = new UIActionMenuMediumSelectorRefresh(this);

    /* Prepare update-handlers for known menus: */
#ifdef VBOX_WS_MAC
    m_menuUpdateHandlers[UIActionIndex_M_Application].ptf = &UIActionPool::updateMenuApplication;
    m_menuUpdateHandlers[UIActionIndex_M_Window].ptf = &UIActionPool::updateMenuWindow;
#endif
    m_menuUpdateHandlers[UIActionIndex_Menu_Help].ptf = &UIActionPool::updateMenuHelp;
    m_menuUpdateHandlers[UIActionIndex_M_LogWindow].ptf = &UIActionPool::updateMenuLogViewerWindow;
    m_menuUpdateHandlers[UIActionIndex_M_Log].ptf = &UIActionPool::updateMenuLogViewer;
    m_menuUpdateHandlers[UIActionIndex_M_Activity].ptf = &UIActionPool::updateMenuVMActivityMonitor;
    m_menuUpdateHandlers[UIActionIndex_M_FileManager].ptf = &UIActionPool::updateMenuFileManager;

    /* Invalidate all known menus: */
    const QList<int> updateHandlerKeys = m_menuUpdateHandlers.keys();
    m_invalidations.unite(QSet<int>(updateHandlerKeys.begin(), updateHandlerKeys.end()));

    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIActionPool::sltRetranslateUI);
}

void UIActionPool::prepareConnections()
{
    /* 'Application' menu connections: */
#ifdef VBOX_WS_MAC
    connect(action(UIActionIndex_M_Application_S_About), &UIAction::triggered,
            &msgCenter(), &UIMessageCenter::sltShowHelpAboutDialog, Qt::UniqueConnection);
#endif
#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
    connect(action(UIActionIndex_M_Application_S_CheckForUpdates), &UIAction::triggered,
            gUpdateManager, &UIUpdateManager::sltForceCheck, Qt::UniqueConnection);
#endif
    connect(action(UIActionIndex_M_Application_S_ResetWarnings), &UIAction::triggered,
            &msgCenter(), &UIMessageCenter::sltResetSuppressedMessages, Qt::UniqueConnection);

    /* 'Help' menu connections. Note that connections for UIActionIndex_Simple_Contents
     * are done in manager and runtime UIs separately in their respective classes: */
    connect(action(UIActionIndex_Simple_OnlineDocumentation), &UIAction::triggered,
            &msgCenter(), &UIMessageCenter::sltShowOnlineDocumentation, Qt::UniqueConnection);
    connect(action(UIActionIndex_Simple_WebSite), &UIAction::triggered,
            &msgCenter(), &UIMessageCenter::sltShowHelpWebDialog, Qt::UniqueConnection);
    connect(action(UIActionIndex_Simple_BugTracker), &UIAction::triggered,
            &msgCenter(), &UIMessageCenter::sltShowBugTracker, Qt::UniqueConnection);
    connect(action(UIActionIndex_Simple_Forums), &UIAction::triggered,
            &msgCenter(), &UIMessageCenter::sltShowForums, Qt::UniqueConnection);
    connect(action(UIActionIndex_Simple_Oracle), &UIAction::triggered,
            &msgCenter(), &UIMessageCenter::sltShowOracle, Qt::UniqueConnection);
#ifndef VBOX_WS_MAC
    connect(action(UIActionIndex_Simple_About), &UIAction::triggered,
            &msgCenter(), &UIMessageCenter::sltShowHelpAboutDialog, Qt::UniqueConnection);
#endif
}

void UIActionPool::cleanupConnections()
{
    /* Nothing for now.. */
}

void UIActionPool::cleanupPool()
{
    qDeleteAll(m_groupPool);
    qDeleteAll(m_pool);
}

void UIActionPool::updateConfiguration()
{
    /* Recache common action restrictions: */
    // Nothing here for now..

#ifdef VBOX_GUI_WITH_NETWORK_MANAGER
    /* Recache update action restrictions: */
    bool fUpdateAllowed = gEDataManager->applicationUpdateEnabled();
    if (!fUpdateAllowed)
    {
        m_restrictedActionsMenuApplication[UIActionRestrictionLevel_Base] = (UIExtraDataMetaDefs::MenuApplicationActionType)
            (m_restrictedActionsMenuApplication[UIActionRestrictionLevel_Base] | UIExtraDataMetaDefs::MenuApplicationActionType_CheckForUpdates);
    }
#endif /* VBOX_GUI_WITH_NETWORK_MANAGER */

    /* Update menus: */
    updateMenus();
}

void UIActionPool::updateMenu(int iIndex)
{
    /* Make sure index belongs to this class: */
    AssertReturnVoid(iIndex < UIActionIndex_Max);

    /* If menu with such index is invalidated
     * and there is update-handler => handle it here: */
    if (   m_invalidations.contains(iIndex)
        && m_menuUpdateHandlers.contains(iIndex))
        (this->*(m_menuUpdateHandlers.value(iIndex).ptf))();
}

void UIActionPool::updateShortcuts()
{
    gShortcutPool->applyShortcuts(this);
}

bool UIActionPool::event(QEvent *pEvent)
{
    /* Depending on event-type: */
    switch ((UIEventTypeActionPool)pEvent->type())
    {
        case UIEventTypeActionPool_ActivateAction:
        {
            /* Process specific event: */
            ActivateActionEvent *pActionEvent = static_cast<ActivateActionEvent*>(pEvent);
            pActionEvent->action()->trigger();
            pEvent->accept();
            return true;
        }
        default:
            break;
    }
    /* Pass to the base-class: */
    return QObject::event(pEvent);
}

void UIActionPool::sltRetranslateUI()
{
    /* Translate all the actions: */
    foreach (const int iActionPoolKey, m_pool.keys())
        m_pool[iActionPoolKey]->retranslateUi();
    /* Update shortcuts: */
    updateShortcuts();
}

bool UIActionPool::addAction(UIMenu *pMenu, UIAction *pAction, bool fReallyAdd /* = true */)
{
    /* Sanity check: */
    AssertPtrReturn(pMenu, false);
    AssertPtrReturn(pAction, false);

    /* Check if action is allowed: */
    const bool fIsActionAllowed = pAction->isAllowed();

#ifdef VBOX_WS_MAC
    /* Check if menu is consumable: */
    const bool fIsMenuConsumable = pMenu->isConsumable();
    /* Check if menu is NOT yet consumed: */
    const bool fIsMenuConsumed = pMenu->isConsumed();
#endif

    /* Make this action visible
     * depending on clearance state. */
    pAction->setVisible(fIsActionAllowed);

#ifdef VBOX_WS_MAC
    /* If menu is consumable: */
    if (fIsMenuConsumable)
    {
        /* Add action only if menu was not yet consumed: */
        if (!fIsMenuConsumed)
            pMenu->addAction(pAction);
    }
    /* If menu is NOT consumable: */
    else
#endif
    {
        /* Add action only if is allowed: */
        if (fIsActionAllowed && fReallyAdd)
            pMenu->addAction(pAction);
    }

    /* Return if action is allowed: */
    return fIsActionAllowed;
}

bool UIActionPool::addMenu(QList<QMenu*> &menuList, UIAction *pAction, bool fReallyAdd /* = true */)
{
    /* Check if action is allowed: */
    const bool fIsActionAllowed = pAction->isAllowed();

    /* Get action's menu: */
    UIMenu *pMenu = pAction->menu();

#ifdef VBOX_WS_MAC
    /* Check if menu is consumable: */
    const bool fIsMenuConsumable = pMenu->isConsumable();
    /* Check if menu is NOT yet consumed: */
    const bool fIsMenuConsumed = pMenu->isConsumed();
#endif

    /* Make this action visible
     * depending on clearance state. */
    pAction->setVisible(   fIsActionAllowed
#ifdef VBOX_WS_MAC
                        || fIsMenuConsumable
#endif
                        );

#ifdef VBOX_WS_MAC
    /* If menu is consumable: */
    if (fIsMenuConsumable)
    {
        /* Add action's menu only if menu was not yet consumed: */
        if (!fIsMenuConsumed)
            menuList << pMenu;
    }
    /* If menu is NOT consumable: */
    else
#endif
    {
        /* Add action only if is allowed: */
        if (fIsActionAllowed && fReallyAdd)
            menuList << pMenu;
    }

    /* Return if action is allowed: */
    return fIsActionAllowed;
}

void UIActionPool::updateMenuApplication()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndex_M_Application)->menu();
    AssertPtrReturnVoid(pMenu);
#ifdef VBOX_WS_MAC
    AssertReturnVoid(pMenu->isConsumable());
#endif
    /* Clear contents: */
#ifdef VBOX_WS_MAC
    if (!pMenu->isConsumed())
#endif
        pMenu->clear();

    /* Separator: */
    bool fSeparator = false;

#ifdef VBOX_WS_MAC
    /* 'About' action: */
    fSeparator = addAction(pMenu, action(UIActionIndex_M_Application_S_About)) || fSeparator;
#endif

    /* 'Preferences' action: */
    fSeparator = addAction(pMenu, action(UIActionIndex_M_Application_S_Preferences)) || fSeparator;

#ifndef VBOX_WS_MAC
    /* Separator: */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }
#endif

    /* 'Reset Warnings' action: */
    fSeparator = addAction(pMenu, action(UIActionIndex_M_Application_S_ResetWarnings)) || fSeparator;

#ifndef VBOX_WS_MAC
    /* Separator: */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }
#endif

    /* 'Close' action: */
    fSeparator = addAction(pMenu, action(UIActionIndex_M_Application_S_Close)) || fSeparator;

    /* Remove if fSeparator is used: */
    Q_UNUSED(fSeparator);

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndex_M_Application);
}

#ifdef VBOX_WS_MAC
void UIActionPool::updateMenuWindow()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndex_M_Window)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* Separator: */
    bool fSeparator = false;

    /* 'Minimize' action: */
    fSeparator = addAction(pMenu, action(UIActionIndex_M_Window_S_Minimize)) || fSeparator;

    /* Separator: */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* This menu always remains invalid.. */
}
#endif /* VBOX_WS_MAC */

void UIActionPool::updateMenuHelp()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndex_Menu_Help)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* Separator? */
    bool fSeparator = false;

    /* 'Contents' action: */
    fSeparator = addAction(pMenu, action(UIActionIndex_Simple_Contents)) || fSeparator;
    /* 'Online Documentation' action: */
    fSeparator = addAction(pMenu, action(UIActionIndex_Simple_OnlineDocumentation)) || fSeparator;
    /* 'Web Site' action: */
    fSeparator = addAction(pMenu, action(UIActionIndex_Simple_WebSite)) || fSeparator;
    /* 'Bug Tracker' action: */
    fSeparator = addAction(pMenu, action(UIActionIndex_Simple_BugTracker)) || fSeparator;
    /* 'Forums' action: */
    fSeparator = addAction(pMenu, action(UIActionIndex_Simple_Forums)) || fSeparator;
    /* 'Oracle' action: */
    fSeparator = addAction(pMenu, action(UIActionIndex_Simple_Oracle)) || fSeparator;

#ifndef VBOX_WS_MAC
    /* Separator? */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* 'About' action: */
    fSeparator = addAction(pMenu, action(UIActionIndex_Simple_About)) || fSeparator;
#endif

    /* Remove if fSeparator is used: */
    Q_UNUSED(fSeparator);

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndex_Menu_Help);
}

void UIActionPool::updateMenuLogViewerWindow()
{
    /* Update corresponding menu: */
    updateMenuLogViewerWrapper(action(UIActionIndex_M_LogWindow)->menu());

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndex_M_LogWindow);
}

void UIActionPool::updateMenuLogViewer()
{
    /* Update corresponding menu: */
    updateMenuLogViewerWrapper(action(UIActionIndex_M_Log)->menu());

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndex_M_Log);
}

void UIActionPool::updateMenuLogViewerWrapper(UIMenu *pMenu)
{
    /* Clear contents: */
    pMenu->clear();

    /* Separator? */
    bool fSeparator = false;

    /* 'Save' action: */
    fSeparator = addAction(pMenu, action(UIActionIndex_M_Log_S_Save)) || fSeparator;

    /* Separator? */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* 'Find' action: */
    fSeparator = addAction(pMenu, action(UIActionIndex_M_Log_T_Find)) || fSeparator;
    /* 'Filter' action: */
    fSeparator = addAction(pMenu, action(UIActionIndex_M_Log_T_Filter)) || fSeparator;
    /* 'Bookmarks' action: */
    fSeparator = addAction(pMenu, action(UIActionIndex_M_Log_T_Bookmark)) || fSeparator;
    /* 'Preferences' action: */
    fSeparator = addAction(pMenu, action(UIActionIndex_M_Log_T_Preferences)) || fSeparator;

    /* Separator? */
    if (fSeparator)
    {
        pMenu->addSeparator();
        fSeparator = false;
    }

    /* 'Refresh' action: */
    fSeparator = addAction(pMenu, action(UIActionIndex_M_Log_S_Refresh)) || fSeparator;
    fSeparator = addAction(pMenu, action(UIActionIndex_M_Log_S_Reload)) || fSeparator;

    /* Remove if fSeparator is used: */
    Q_UNUSED(fSeparator);
}

void UIActionPool::updateMenuVMActivityMonitor()
{
    /* Get corresponding menu: */
    UIMenu *pMenu = action(UIActionIndex_M_Activity)->menu();
    AssertPtrReturnVoid(pMenu);
    /* Clear contents: */
    pMenu->clear();

    /* 'Export' and 'Switch to VM Activity Overview" actions: */
    pMenu->addAction(action(UIActionIndex_M_Activity_S_Export));
    pMenu->addAction(action(UIActionIndex_M_Activity_S_ToVMActivityOverview));
    /* 'Preferences' action: */
    pMenu->addAction(action(UIActionIndex_M_Activity_T_Preferences));

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndex_M_Activity);
}

void UIActionPool::updateMenuFileManager()
{
    updateMenuFileManagerWrapper(action(UIActionIndex_M_FileManager)->menu());

    /* Mark menu as valid: */
    m_invalidations.remove(UIActionIndex_M_FileManager);
}

void UIActionPool::updateMenuFileManagerWrapper(UIMenu *pMenu)
{
    addAction(pMenu, action(UIActionIndex_M_FileManager_T_Preferences));
    addAction(pMenu, action(UIActionIndex_M_FileManager_T_Operations));
    addAction(pMenu, action(UIActionIndex_M_FileManager_T_Log));

    addAction(pMenu, action(UIActionIndex_M_FileManager_M_HostSubmenu));
    addAction(pMenu, action(UIActionIndex_M_FileManager_M_GuestSubmenu));

    UIMenu *pHostSubmenu = action(UIActionIndex_M_FileManager_M_HostSubmenu)->menu();
    if (pHostSubmenu)
    {
        addAction(pHostSubmenu, action(UIActionIndex_M_FileManager_S_Host_GoUp));
        addAction(pHostSubmenu, action(UIActionIndex_M_FileManager_S_Host_GoHome));
        addAction(pHostSubmenu, action(UIActionIndex_M_FileManager_S_Host_Refresh));
        addAction(pHostSubmenu, action(UIActionIndex_M_FileManager_S_Host_Delete));
        addAction(pHostSubmenu, action(UIActionIndex_M_FileManager_S_Host_Rename));
        addAction(pHostSubmenu, action(UIActionIndex_M_FileManager_S_Host_CreateNewDirectory));
        addAction(pHostSubmenu, action(UIActionIndex_M_FileManager_S_Host_Copy));
        addAction(pHostSubmenu, action(UIActionIndex_M_FileManager_S_Host_Cut));
        addAction(pHostSubmenu, action(UIActionIndex_M_FileManager_S_Host_Paste));
        addAction(pHostSubmenu, action(UIActionIndex_M_FileManager_S_Host_SelectAll));
        addAction(pHostSubmenu, action(UIActionIndex_M_FileManager_S_Host_InvertSelection));
        addAction(pHostSubmenu, action(UIActionIndex_M_FileManager_S_Host_ShowProperties));
    }

    UIMenu *pGuestSubmenu = action(UIActionIndex_M_FileManager_M_GuestSubmenu)->menu();
    if (pGuestSubmenu)
    {
        addAction(pGuestSubmenu, action(UIActionIndex_M_FileManager_S_Host_GoUp));
        addAction(pGuestSubmenu, action(UIActionIndex_M_FileManager_S_Guest_GoHome));
        addAction(pGuestSubmenu, action(UIActionIndex_M_FileManager_S_Guest_Refresh));
        addAction(pGuestSubmenu, action(UIActionIndex_M_FileManager_S_Guest_Delete));
        addAction(pGuestSubmenu, action(UIActionIndex_M_FileManager_S_Guest_Rename));
        addAction(pGuestSubmenu, action(UIActionIndex_M_FileManager_S_Guest_CreateNewDirectory));
        addAction(pGuestSubmenu, action(UIActionIndex_M_FileManager_S_Guest_Copy));
        addAction(pGuestSubmenu, action(UIActionIndex_M_FileManager_S_Guest_Cut));
        addAction(pGuestSubmenu, action(UIActionIndex_M_FileManager_S_Guest_Paste));
        addAction(pGuestSubmenu, action(UIActionIndex_M_FileManager_S_Guest_SelectAll));
        addAction(pGuestSubmenu, action(UIActionIndex_M_FileManager_S_Guest_InvertSelection));
        addAction(pGuestSubmenu, action(UIActionIndex_M_FileManager_S_Guest_ShowProperties));
    }
}

void UIActionPool::prepare()
{
    /* Prepare pool: */
    preparePool();
    /* Prepare connections: */
    prepareConnections();

    /* Update configuration: */
    updateConfiguration();
    /* Update shortcuts: */
    updateShortcuts();
}

void UIActionPool::cleanup()
{
    /* Cleanup connections: */
    cleanupConnections();
    /* Cleanup pool: */
    cleanupPool();
}


#include "UIActionPool.moc"
