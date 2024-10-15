/* $Id: UIShortcutPool.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIShortcutPool class implementation.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#include <QApplication>

/* GUI includes: */
#include "UIActionPool.h"
#include "UIExtraDataManager.h"
#include "UIGlobalSession.h"
#include "UIShortcutPool.h"
#include "UITranslationEventListener.h"

/* Namespaces: */
using namespace UIExtraDataDefs;


/*********************************************************************************************************************************
*   Class UIShortcut implementation.                                                                                             *
*********************************************************************************************************************************/

void UIShortcut::setScope(const QString &strScope)
{
    m_strScope = strScope;
}

const QString &UIShortcut::scope() const
{
    return m_strScope;
}

void UIShortcut::setDescription(const QString &strDescription)
{
    m_strDescription = strDescription;
}

const QString &UIShortcut::description() const
{
    return m_strDescription;
}

void UIShortcut::setSequences(const QList<QKeySequence> &sequences)
{
    m_sequences = sequences;
}

const QList<QKeySequence> &UIShortcut::sequences() const
{
    return m_sequences;
}

void UIShortcut::setDefaultSequence(const QKeySequence &defaultSequence)
{
    m_defaultSequence = defaultSequence;
}

const QKeySequence &UIShortcut::defaultSequence() const
{
    return m_defaultSequence;
}

void UIShortcut::setStandardSequence(const QKeySequence &standardSequence)
{
    m_standardSequence = standardSequence;
}

const QKeySequence &UIShortcut::standardSequence() const
{
    return m_standardSequence;
}

QString UIShortcut::primaryToNativeText() const
{
    return m_sequences.isEmpty() ? QString() : m_sequences.first().toString(QKeySequence::NativeText);
}

QString UIShortcut::primaryToPortableText() const
{
    return m_sequences.isEmpty() ? QString() : m_sequences.first().toString(QKeySequence::PortableText);
}


/*********************************************************************************************************************************
*   Class UIShortcutPool implementation.                                                                                         *
*********************************************************************************************************************************/

/* static */
UIShortcutPool *UIShortcutPool::s_pInstance = 0;
const QString UIShortcutPool::s_strShortcutKeyTemplate = QString("%1/%2");
const QString UIShortcutPool::s_strShortcutKeyTemplateRuntime = s_strShortcutKeyTemplate.arg(GUI_Input_MachineShortcuts);

void UIShortcutPool::create(UIType enmType)
{
    /* Check that instance do NOT exists: */
    if (s_pInstance)
        return;

    /* Create instance: */
    new UIShortcutPool(enmType);

    /* Prepare instance: */
    s_pInstance->prepare();
}

void UIShortcutPool::destroy()
{
    /* Check that instance exists: */
    if (!s_pInstance)
        return;

    /* Cleanup instance: */
    s_pInstance->cleanup();

    /* Delete instance: */
    delete s_pInstance;
}

UIShortcut &UIShortcutPool::shortcut(UIActionPool *pActionPool, UIAction *pAction)
{
    /* Compose shortcut key: */
    const QString strShortcutKey(s_strShortcutKeyTemplate.arg(pActionPool->shortcutsExtraDataID(),
                                                              pAction->shortcutExtraDataID()));
    /* Return existing if any: */
    if (m_shortcuts.contains(strShortcutKey))
        return shortcut(strShortcutKey);
    /* Create and return new one: */
    UIShortcut &newShortcut = m_shortcuts[strShortcutKey];
    newShortcut.setScope(pAction->shortcutScope());
    newShortcut.setDescription(pAction->name());
    const QKeySequence &defaultSequence = pAction->defaultShortcut(pActionPool->type());
    const QKeySequence &standardSequence = pAction->standardShortcut(pActionPool->type());
    newShortcut.setSequences(QList<QKeySequence>() << defaultSequence << standardSequence);
    newShortcut.setDefaultSequence(defaultSequence);
    newShortcut.setStandardSequence(standardSequence);
    return newShortcut;
}

UIShortcut &UIShortcutPool::shortcut(const QString &strPoolID, const QString &strActionID)
{
    /* Return if present, autocreate if necessary: */
    return shortcut(s_strShortcutKeyTemplate.arg(strPoolID, strActionID));
}

void UIShortcutPool::setOverrides(const QMap<QString, QString> &overrides)
{
    /* Iterate over all the overrides: */
    const QList<QString> shortcutKeys = overrides.keys();
    foreach (const QString &strShortcutKey, shortcutKeys)
    {
        /* Make no changes if there is no such shortcut: */
        if (!m_shortcuts.contains(strShortcutKey))
            continue;
        /* Assign overridden sequences to the shortcut: */
        m_shortcuts[strShortcutKey].setSequences(QList<QKeySequence>() << overrides[strShortcutKey]);
    }
    /* Save overrides: */
    saveOverrides();
}

void UIShortcutPool::applyShortcuts(UIActionPool *pActionPool)
{
    /* For each the action of the passed action-pool: */
    foreach (UIAction *pAction, pActionPool->actions())
    {
        /* Skip menu actions: */
        if (pAction->type() == UIActionType_Menu)
            continue;

        /* Compose shortcut key: */
        const QString strShortcutKey = s_strShortcutKeyTemplate.arg(pActionPool->shortcutsExtraDataID(),
                                                                    pAction->shortcutExtraDataID());
        /* If shortcut key is already known: */
        if (m_shortcuts.contains(strShortcutKey))
        {
            /* Get corresponding shortcut: */
            UIShortcut &existingShortcut = m_shortcuts[strShortcutKey];
            /* Copy the scope from the action to the shortcut: */
            existingShortcut.setScope(pAction->shortcutScope());
            /* Copy the description from the action to the shortcut: */
            existingShortcut.setDescription(pAction->name());
            /* Copy the sequences from the shortcut to the action: */
            pAction->setShortcuts(existingShortcut.sequences());
            pAction->retranslateUi();
            /* Copy default and standard sequences from the action to the shortcut: */
            if (pActionPool->type() == m_enmType)
            {
                existingShortcut.setDefaultSequence(pAction->defaultShortcut(pActionPool->type()));
                existingShortcut.setStandardSequence(pAction->standardShortcut(pActionPool->type()));
            }
        }
        /* If shortcut key is NOT known yet: */
        else
        {
            /* Create corresponding shortcut: */
            UIShortcut &newShortcut = m_shortcuts[strShortcutKey];
            /* Copy the action's default sequence to both the shortcut & the action: */
            const QKeySequence &defaultSequence = pAction->defaultShortcut(pActionPool->type());
            const QKeySequence &standardSequence = pAction->standardShortcut(pActionPool->type());
            newShortcut.setSequences(QList<QKeySequence>() << defaultSequence << standardSequence);
            newShortcut.setDefaultSequence(defaultSequence);
            newShortcut.setStandardSequence(standardSequence);
            pAction->setShortcuts(newShortcut.sequences());
            pAction->retranslateUi();
            /* Copy the description from the action to the shortcut: */
            newShortcut.setScope(pAction->shortcutScope());
            newShortcut.setDescription(pAction->name());
        }
    }
}

/* static */
QKeySequence UIShortcutPool::standardSequence(QKeySequence::StandardKey enmKey)
{
    /* We have some overrides: */
    switch (enmKey)
    {
#ifdef VBOX_WS_MAC
        /* So called Apple default sequence for HelpContents action (CMD+?)
         * is no more the default one on macOS for many years.
         * Instead they have redesigned this shortcut to open
         * system-wide Help menu with native search field.
         * But the Contents action has no shortcut anymore.
         * We could leave it empty or make it CMD+/ instead. */
        case QKeySequence::HelpContents:
            return QKeySequence("Ctrl+/");
#endif
        default:
            break;
    }

    /* Use QKeySequence constructor by default: */
    return QKeySequence(enmKey);
}

void UIShortcutPool::sltRetranslateUI()
{
    /* Translate own defaults: */
    m_shortcuts[s_strShortcutKeyTemplateRuntime.arg("PopupMenu")]
        .setDescription(QApplication::translate("UIActionPool", "Popup Menu"));
}

void UIShortcutPool::sltReloadSelectorShortcuts()
{
    /* Clear selector shortcuts first: */
    const QList<QString> shortcutKeyList = m_shortcuts.keys();
    foreach (const QString &strShortcutKey, shortcutKeyList)
        if (strShortcutKey.startsWith(GUI_Input_SelectorShortcuts))
            m_shortcuts.remove(strShortcutKey);

    /* Load selector defaults: */
    loadDefaultsFor(GUI_Input_SelectorShortcuts);
    /* Load selector overrides: */
    loadOverridesFor(GUI_Input_SelectorShortcuts);

    /* Notify manager shortcuts reloaded: */
    emit sigManagerShortcutsReloaded();
}

void UIShortcutPool::sltReloadMachineShortcuts()
{
    /* Clear machine shortcuts first: */
    const QList<QString> shortcutKeyList = m_shortcuts.keys();
    foreach (const QString &strShortcutKey, shortcutKeyList)
        if (strShortcutKey.startsWith(GUI_Input_MachineShortcuts))
            m_shortcuts.remove(strShortcutKey);

    /* Load machine defaults: */
    loadDefaultsFor(GUI_Input_MachineShortcuts);
    /* Load machine overrides: */
    loadOverridesFor(GUI_Input_MachineShortcuts);

    /* Notify runtime shortcuts reloaded: */
    emit sigRuntimeShortcutsReloaded();
}

UIShortcutPool::UIShortcutPool(UIType enmType)
    : m_enmType(enmType)
{
    /* Prepare instance: */
    if (!s_pInstance)
        s_pInstance = this;
}

UIShortcutPool::~UIShortcutPool()
{
    /* Cleanup instance: */
    if (s_pInstance == this)
        s_pInstance = 0;
}

void UIShortcutPool::prepare()
{
    /* Load defaults: */
    loadDefaults();
    /* Load overrides: */
    loadOverrides();
    /* Prepare connections: */
    prepareConnections();
}

void UIShortcutPool::prepareConnections()
{
    /* Connect to extra-data signals: */
    connect(gEDataManager, &UIExtraDataManager::sigSelectorUIShortcutChange,
            this, &UIShortcutPool::sltReloadSelectorShortcuts);
    connect(gEDataManager, &UIExtraDataManager::sigRuntimeUIShortcutChange,
            this, &UIShortcutPool::sltReloadMachineShortcuts);
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIShortcutPool::sltRetranslateUI);
}

void UIShortcutPool::loadDefaults()
{
    /* Load selector defaults: */
    loadDefaultsFor(GUI_Input_SelectorShortcuts);
    /* Load machine defaults: */
    loadDefaultsFor(GUI_Input_MachineShortcuts);
}

void UIShortcutPool::loadDefaultsFor(const QString &strPoolExtraDataID)
{
    /* Default shortcuts for Selector UI: */
    if (strPoolExtraDataID == GUI_Input_SelectorShortcuts)
    {
        /* Nothing for now.. */
    }
    /* Default shortcuts for Runtime UI: */
    else if (strPoolExtraDataID == GUI_Input_MachineShortcuts)
    {
        /* Default shortcut for the Runtime Popup Menu: */
        m_shortcuts.insert(s_strShortcutKeyTemplateRuntime.arg("PopupMenu"),
                           UIShortcut(QString(),
                                      QApplication::translate("UIActionPool", "Popup Menu"),
                                      QList<QKeySequence>() << QString("Home"),
                                      QString("Home"),
                                      QString()));
    }
}

void UIShortcutPool::loadOverrides()
{
    /* Load selector overrides: */
    loadOverridesFor(GUI_Input_SelectorShortcuts);
    /* Load machine overrides: */
    loadOverridesFor(GUI_Input_MachineShortcuts);
}

void UIShortcutPool::loadOverridesFor(const QString &strPoolExtraDataID)
{
    /* Compose shortcut key template: */
    const QString strShortcutKeyTemplate(s_strShortcutKeyTemplate.arg(strPoolExtraDataID));
    /* Iterate over all the overrides: */
    const QStringList overrides = gEDataManager->shortcutOverrides(strPoolExtraDataID);
    foreach (const QString &strKeyValuePair, overrides)
    {
        /* Make sure override structure is valid: */
        int iDelimiterPosition = strKeyValuePair.indexOf('=');
        if (iDelimiterPosition < 0)
            continue;

        /* Get shortcut ID/sequence: */
        QString strShortcutExtraDataID = strKeyValuePair.left(iDelimiterPosition);
        const QString strShortcutSequence = strKeyValuePair.right(strKeyValuePair.length() - iDelimiterPosition - 1);

        // Hack for handling "Save" as "SaveState":
        if (strShortcutExtraDataID == "Save")
            strShortcutExtraDataID = "SaveState";

        /* Compose corresponding shortcut key: */
        const QString strShortcutKey(strShortcutKeyTemplate.arg(strShortcutExtraDataID));
        /* Modify map with composed key/value: */
        if (!m_shortcuts.contains(strShortcutKey))
            m_shortcuts.insert(strShortcutKey,
                               UIShortcut(QString(),
                                          QString(),
                                          QList<QKeySequence>() << strShortcutSequence,
                                          QString(),
                                          QString()));
        else
        {
            /* Get corresponding value: */
            UIShortcut &shortcut = m_shortcuts[strShortcutKey];
            /* Check if corresponding shortcut overridden by value: */
            if (shortcut.primaryToPortableText().compare(strShortcutSequence, Qt::CaseInsensitive) != 0)
            {
                /* Shortcut unassigned? */
                if (strShortcutSequence.compare("None", Qt::CaseInsensitive) == 0)
                    shortcut.setSequences(QList<QKeySequence>());
                /* Or reassigned? */
                else
                    shortcut.setSequences(QList<QKeySequence>() << strShortcutSequence);
            }
        }
    }
}

void UIShortcutPool::saveOverrides()
{
    /* Load selector overrides: */
    saveOverridesFor(GUI_Input_SelectorShortcuts);
    /* Load machine overrides: */
    saveOverridesFor(GUI_Input_MachineShortcuts);
}

void UIShortcutPool::saveOverridesFor(const QString &strPoolExtraDataID)
{
    /* Compose shortcut prefix: */
    const QString strShortcutPrefix(s_strShortcutKeyTemplate.arg(strPoolExtraDataID, QString()));
    /* Populate the list of all the known overrides: */
    QStringList overrides;
    const QList<QString> shortcutKeys = m_shortcuts.keys();
    foreach (const QString &strShortcutKey, shortcutKeys)
    {
        /* Check if the key starts from the proper prefix: */
        if (!strShortcutKey.startsWith(strShortcutPrefix))
            continue;
        /* Get corresponding shortcut: */
        const UIShortcut &shortcut = m_shortcuts[strShortcutKey];
        /* Check if the sequence for that shortcut differs from default or standard: */
        if (   shortcut.sequences().contains(shortcut.defaultSequence())
            || (   !shortcut.standardSequence().isEmpty()
                && shortcut.sequences().contains(shortcut.standardSequence())))
            continue;
        /* Add the shortcut sequence into overrides list: */
        overrides << QString("%1=%2").arg(QString(strShortcutKey).remove(strShortcutPrefix),
                                          shortcut.primaryToPortableText());
    }
    /* Save overrides into the extra-data: */
    gpGlobalSession->virtualBox().SetExtraDataStringList(strPoolExtraDataID, overrides);
}

UIShortcut &UIShortcutPool::shortcut(const QString &strShortcutKey)
{
    return m_shortcuts[strShortcutKey];
}
