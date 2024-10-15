/* $Id: UIGlobalSettingsLanguage.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGlobalSettingsLanguage class implementation.
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
#include "UIExtraDataManager.h"
#include "UIGlobalSettingsLanguage.h"
#include "UILanguageSettingsEditor.h"


/** Global settings: Language page data structure. */
struct UIDataSettingsGlobalLanguage
{
    /** Constructs data. */
    UIDataSettingsGlobalLanguage()
        : m_strLanguageId(QString())
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataSettingsGlobalLanguage &other) const
    {
        return    true
               && (m_strLanguageId == other.m_strLanguageId)
                  ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataSettingsGlobalLanguage &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataSettingsGlobalLanguage &other) const { return !equal(other); }

    /** Holds the current language id. */
    QString m_strLanguageId;
};


/*********************************************************************************************************************************
*   Class UIGlobalSettingsLanguage implementation.                                                                               *
*********************************************************************************************************************************/

UIGlobalSettingsLanguage::UIGlobalSettingsLanguage()
    : m_pCache(0)
    , m_pEditorLanguageSettings(0)
{
    prepare();
}

UIGlobalSettingsLanguage::~UIGlobalSettingsLanguage()
{
    cleanup();
}

bool UIGlobalSettingsLanguage::changed() const
{
    return m_pCache ? m_pCache->wasChanged() : false;
}

void UIGlobalSettingsLanguage::loadToCacheFrom(QVariant &data)
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Fetch data to properties: */
    UISettingsPageGlobal::fetchData(data);

    /* Clear cache initially: */
    m_pCache->clear();

    /* Cache old data: */
    UIDataSettingsGlobalLanguage oldData;
    oldData.m_strLanguageId = gEDataManager->languageId();
    m_pCache->cacheInitialData(oldData);

    /* Upload properties to data: */
    UISettingsPageGlobal::uploadData(data);
}

void UIGlobalSettingsLanguage::getFromCache()
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Load old data from cache: */
    const UIDataSettingsGlobalLanguage &oldData = m_pCache->base();
    if (m_pEditorLanguageSettings)
        m_pEditorLanguageSettings->setValue(oldData.m_strLanguageId);
}

void UIGlobalSettingsLanguage::putToCache()
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Prepare new data: */
    UIDataSettingsGlobalLanguage newData = m_pCache->base();

    /* Cache new data: */
    if (m_pEditorLanguageSettings)
        newData.m_strLanguageId = m_pEditorLanguageSettings->value();
    m_pCache->cacheCurrentData(newData);
}

void UIGlobalSettingsLanguage::saveFromCacheTo(QVariant &data)
{
    /* Fetch data to properties: */
    UISettingsPageGlobal::fetchData(data);

    /* Update data and failing state: */
    setFailed(!saveData());

    /* Upload properties to data: */
    UISettingsPageGlobal::uploadData(data);
}

void UIGlobalSettingsLanguage::sltRetranslateUI()
{
}

void UIGlobalSettingsLanguage::prepare()
{
    /* Prepare cache: */
    m_pCache = new UISettingsCacheGlobalLanguage;
    AssertPtrReturnVoid(m_pCache);

    /* Prepare everything: */
    prepareWidgets();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIGlobalSettingsLanguage::prepareWidgets()
{
    /* Prepare main layout: */
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    if (pLayout)
    {
        /* Prepare 'language settings' editor: */
        m_pEditorLanguageSettings = new UILanguageSettingsEditor(this);
        if (m_pEditorLanguageSettings)
        {
            addEditor(m_pEditorLanguageSettings);
            pLayout->addWidget(m_pEditorLanguageSettings);
        }
    }
}

void UIGlobalSettingsLanguage::cleanup()
{
    /* Cleanup cache: */
    delete m_pCache;
    m_pCache = 0;
}

bool UIGlobalSettingsLanguage::saveData()
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
        const UIDataSettingsGlobalLanguage &oldData = m_pCache->base();
        /* Get new data from cache: */
        const UIDataSettingsGlobalLanguage &newData = m_pCache->data();

        /* Save new data from cache: */
        if (   fSuccess
            && newData.m_strLanguageId != oldData.m_strLanguageId)
            /* fSuccess = */ gEDataManager->setLanguageId(newData.m_strLanguageId);
    }
    /* Return result: */
    return fSuccess;
}
