/* $Id: UIGlobalSettingsInput.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGlobalSettingsInput class implementation.
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

/* Qt includes: */
#include <QVBoxLayout>

/* GUI includes: */
#include "UIAutoCaptureKeyboardEditor.h"
#include "UIExtraDataManager.h"
#include "UIGlobalSettingsInput.h"
#include "UIHostComboEditor.h"
#include "UIShortcutConfigurationEditor.h"
#include "UIShortcutPool.h"
#include "UITranslator.h"

/** Global settings: Input page data structure. */
struct UIDataSettingsGlobalInput
{
    /** Constructs cache. */
    UIDataSettingsGlobalInput()
        : m_fAutoCapture(false)
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataSettingsGlobalInput &other) const
    {
        return    true
               && (m_shortcuts == other.m_shortcuts)
               && (m_fAutoCapture == other.m_fAutoCapture)
                  ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataSettingsGlobalInput &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataSettingsGlobalInput &other) const { return !equal(other); }

    /** Holds the shortcut configuration list. */
    UIShortcutConfigurationList  m_shortcuts;
    /** Holds whether the keyboard auto-capture is enabled. */
    bool                         m_fAutoCapture;
};


/*********************************************************************************************************************************
*   Class UIGlobalSettingsInput implementation.                                                                                  *
*********************************************************************************************************************************/

UIGlobalSettingsInput::UIGlobalSettingsInput()
    : m_pCache(0)
    , m_pEditorShortcutConfiguration(0)
    , m_pEditorAutoCaptureKeyboard(0)
{
    prepare();
}

UIGlobalSettingsInput::~UIGlobalSettingsInput()
{
    cleanup();
}

bool UIGlobalSettingsInput::changed() const
{
    return m_pCache ? m_pCache->wasChanged() : false;
}

void UIGlobalSettingsInput::loadToCacheFrom(QVariant &data)
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Fetch data to properties: */
    UISettingsPageGlobal::fetchData(data);

    /* Clear cache initially: */
    m_pCache->clear();

    /* Cache old data: */
    UIDataSettingsGlobalInput oldData;
    UIShortcutConfigurationList list;
    list << UIShortcutConfigurationItem(UIHostCombo::hostComboCacheKey(),
                                        QString(),
                                        tr("Host Key Combo"),
                                        gEDataManager->hostKeyCombination(),
                                        QString());
    const QMap<QString, UIShortcut> &shortcuts = gShortcutPool->shortcuts();
    const QList<QString> shortcutKeys = shortcuts.keys();
    foreach (const QString &strShortcutKey, shortcutKeys)
    {
        const UIShortcut &shortcut = shortcuts.value(strShortcutKey);
        list << UIShortcutConfigurationItem(strShortcutKey,
                                            shortcut.scope(),
                                            UITranslator::removeAccelMark(shortcut.description()),
                                            shortcut.primaryToNativeText(),
                                            shortcut.defaultSequence().toString(QKeySequence::NativeText));
    }
    oldData.m_shortcuts = list;
    oldData.m_fAutoCapture = gEDataManager->autoCaptureEnabled();
    m_pCache->cacheInitialData(oldData);

    /* Upload properties to data: */
    UISettingsPageGlobal::uploadData(data);
}

void UIGlobalSettingsInput::getFromCache()
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Load old data from cache: */
    const UIDataSettingsGlobalInput &oldData = m_pCache->base();
    if (m_pEditorShortcutConfiguration)
        m_pEditorShortcutConfiguration->load(oldData.m_shortcuts);
    if (m_pEditorAutoCaptureKeyboard)
        m_pEditorAutoCaptureKeyboard->setValue(oldData.m_fAutoCapture);

    /* Revalidate: */
    revalidate();
}

void UIGlobalSettingsInput::putToCache()
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Prepare new data: */
    UIDataSettingsGlobalInput newData = m_pCache->base();

    /* Cache new data: */
    if (m_pEditorShortcutConfiguration)
        m_pEditorShortcutConfiguration->save(newData.m_shortcuts);
    if (m_pEditorAutoCaptureKeyboard)
        newData.m_fAutoCapture = m_pEditorAutoCaptureKeyboard->value();
    m_pCache->cacheCurrentData(newData);
}

void UIGlobalSettingsInput::saveFromCacheTo(QVariant &data)
{
    /* Fetch data to properties: */
    UISettingsPageGlobal::fetchData(data);

    /* Update data and failing state: */
    setFailed(!saveData());

    /* Upload properties to data: */
    UISettingsPageGlobal::uploadData(data);
}

bool UIGlobalSettingsInput::validate(QList<UIValidationMessage> &messages)
{
    /* Pass by default: */
    bool fPass = true;

    /* Check VirtualBox Manager page for unique shortcuts: */
    if (!m_pEditorShortcutConfiguration->isShortcutsUniqueManager())
    {
        UIValidationMessage message;
        message.first = UITranslator::removeAccelMark(m_pEditorShortcutConfiguration->tabNameManager());
        message.second << tr("Some items have the same shortcuts assigned.");
        messages << message;
        fPass = false;
    }

    /* Check Virtual Runtime page for unique shortcuts: */
    if (!m_pEditorShortcutConfiguration->isShortcutsUniqueRuntime())
    {
        UIValidationMessage message;
        message.first = UITranslator::removeAccelMark(m_pEditorShortcutConfiguration->tabNameRuntime());
        message.second << tr("Some items have the same shortcuts assigned.");
        messages << message;
        fPass = false;
    }

    /* Return result: */
    return fPass;
}

void UIGlobalSettingsInput::sltRetranslateUI()
{
}

void UIGlobalSettingsInput::prepare()
{
    /* Prepare cache: */
    m_pCache = new UISettingsCacheGlobalInput;
    AssertPtrReturnVoid(m_pCache);

    /* Prepare everything: */
    prepareWidgets();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIGlobalSettingsInput::prepareWidgets()
{
    /* Prepare main layout: */
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    if (pLayout)
    {
        /* Prepare 'shortcut configuration' editor: */
        m_pEditorShortcutConfiguration = new UIShortcutConfigurationEditor(this);
        if (m_pEditorShortcutConfiguration)
        {
            connect(m_pEditorShortcutConfiguration, &UIShortcutConfigurationEditor::sigValueChanged,
                    this, &UIGlobalSettingsInput::revalidate);
            addEditor(m_pEditorShortcutConfiguration);
            pLayout->addWidget(m_pEditorShortcutConfiguration);
        }

        /* Prepare 'auto capture keyboard' editor: */
        m_pEditorAutoCaptureKeyboard = new UIAutoCaptureKeyboardEditor(this);
        if (m_pEditorAutoCaptureKeyboard)
        {
            addEditor(m_pEditorAutoCaptureKeyboard);
            pLayout->addWidget(m_pEditorAutoCaptureKeyboard);
        }
    }
}

void UIGlobalSettingsInput::cleanup()
{
    /* Cleanup cache: */
    delete m_pCache;
    m_pCache = 0;
}

bool UIGlobalSettingsInput::saveData()
{
    /* Sanity check: */
    if (!m_pCache)
        return false;

    /* Prepare result: */
    bool fSuccess = true;
    /* Save settings from cache: */
    if (   fSuccess
        && m_pCache->wasChanged())
    {
        /* Get old data from cache: */
        const UIDataSettingsGlobalInput &oldData = m_pCache->base();
        /* Get new data from cache: */
        const UIDataSettingsGlobalInput &newData = m_pCache->data();

        /* Save new host-combo shortcut from cache: */
        if (fSuccess)
        {
            const UIShortcutConfigurationItem fakeHostComboItem(UIHostCombo::hostComboCacheKey(), QString(), QString(), QString(), QString());
            const int iHostComboItemBase = UIShortcutSearchFunctor<UIShortcutConfigurationItem>()(oldData.m_shortcuts, fakeHostComboItem);
            const int iHostComboItemData = UIShortcutSearchFunctor<UIShortcutConfigurationItem>()(newData.m_shortcuts, fakeHostComboItem);
            const QString strHostComboBase = iHostComboItemBase != -1 ? oldData.m_shortcuts.at(iHostComboItemBase).currentSequence() : QString();
            const QString strHostComboData = iHostComboItemData != -1 ? newData.m_shortcuts.at(iHostComboItemData).currentSequence() : QString();
            if (strHostComboData != strHostComboBase)
                /* fSuccess = */ gEDataManager->setHostKeyCombination(strHostComboData);
        }

        /* Save other new shortcuts from cache: */
        if (fSuccess)
        {
            QMap<QString, QString> sequencesBase;
            QMap<QString, QString> sequencesData;
            foreach (const UIShortcutConfigurationItem &item, oldData.m_shortcuts)
                sequencesBase.insert(item.key(), item.currentSequence());
            foreach (const UIShortcutConfigurationItem &item, newData.m_shortcuts)
                sequencesData.insert(item.key(), item.currentSequence());
            if (sequencesData != sequencesBase)
                /* fSuccess = */ gShortcutPool->setOverrides(sequencesData);
        }

        /* Save other new things from cache: */
        if (   fSuccess
            && newData.m_fAutoCapture != oldData.m_fAutoCapture)
            /* fSuccess = */ gEDataManager->setAutoCaptureEnabled(newData.m_fAutoCapture);
    }
    /* Return result: */
    return fSuccess;
}
