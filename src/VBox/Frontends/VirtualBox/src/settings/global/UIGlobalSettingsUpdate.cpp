/* $Id: UIGlobalSettingsUpdate.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGlobalSettingsUpdate class implementation.
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
#include "UIGlobalSettingsUpdate.h"
#include "UIUpdateSettingsEditor.h"


/** Global settings: Update page data structure. */
struct UIDataSettingsGlobalUpdate
{
    /** Constructs data. */
    UIDataSettingsGlobalUpdate()
        : m_guiUpdateData(VBoxUpdateData())
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataSettingsGlobalUpdate &other) const
    {
        return    true
               && (m_guiUpdateData == other.m_guiUpdateData)
                  ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataSettingsGlobalUpdate &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataSettingsGlobalUpdate &other) const { return !equal(other); }

    /** Holds VBox update data. */
    VBoxUpdateData  m_guiUpdateData;
};


/*********************************************************************************************************************************
*   Class UIGlobalSettingsUpdate implementation.                                                                                 *
*********************************************************************************************************************************/

UIGlobalSettingsUpdate::UIGlobalSettingsUpdate()
    : m_pCache(0)
    , m_pEditorUpdateSettings(0)
{
    prepare();
}

UIGlobalSettingsUpdate::~UIGlobalSettingsUpdate()
{
    cleanup();
}

bool UIGlobalSettingsUpdate::changed() const
{
    return m_pCache ? m_pCache->wasChanged() : false;
}

void UIGlobalSettingsUpdate::loadToCacheFrom(QVariant &data)
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Fetch data to properties: */
    UISettingsPageGlobal::fetchData(data);

    /* Clear cache initially: */
    m_pCache->clear();

    /* Cache old data: */
    UIDataSettingsGlobalUpdate oldData;
    VBoxUpdateData guiUpdateData;
    /* Load old data from host: */
    guiUpdateData.load(m_host);
    oldData.m_guiUpdateData = guiUpdateData;
    m_pCache->cacheInitialData(oldData);

    /* Upload properties to data: */
    UISettingsPageGlobal::uploadData(data);
}

void UIGlobalSettingsUpdate::getFromCache()
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Load old data from cache: */
    const UIDataSettingsGlobalUpdate &oldData = m_pCache->base();
    if (m_pEditorUpdateSettings)
        m_pEditorUpdateSettings->setValue(oldData.m_guiUpdateData);
}

void UIGlobalSettingsUpdate::putToCache()
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Prepare new data: */
    UIDataSettingsGlobalUpdate newData = m_pCache->base();

    /* Cache new data: */
    if (m_pEditorUpdateSettings)
        newData.m_guiUpdateData = m_pEditorUpdateSettings->value();
    m_pCache->cacheCurrentData(newData);
}

void UIGlobalSettingsUpdate::saveFromCacheTo(QVariant &data)
{
    /* Fetch data to properties: */
    UISettingsPageGlobal::fetchData(data);

    /* Update data and failing state: */
    setFailed(!saveData());

    /* Upload properties to data: */
    UISettingsPageGlobal::uploadData(data);
}

void UIGlobalSettingsUpdate::sltRetranslateUI()
{
}

void UIGlobalSettingsUpdate::prepare()
{
    /* Prepare cache: */
    m_pCache = new UISettingsCacheGlobalUpdate;
    AssertPtrReturnVoid(m_pCache);

    /* Prepare everything: */
    prepareWidgets();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIGlobalSettingsUpdate::prepareWidgets()
{
    /* Prepare main layout: */
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    if (pLayout)
    {
        /* Prepare 'update settings' editor: */
        m_pEditorUpdateSettings = new UIUpdateSettingsEditor(this);
        if (m_pEditorUpdateSettings)
        {
            addEditor(m_pEditorUpdateSettings);
            pLayout->addWidget(m_pEditorUpdateSettings);
        }

        /* Add stretch to the end: */
        pLayout->addStretch();
    }
}

void UIGlobalSettingsUpdate::cleanup()
{
    /* Cleanup cache: */
    delete m_pCache;
    m_pCache = 0;
}

bool UIGlobalSettingsUpdate::saveData()
{
    /* Sanity check: */
    if (!m_pCache)
        return false;

    /* Prepare result: */
    bool fSuccess = true;
    /* Save update settings from cache: */
    if (   fSuccess
        && m_pCache->wasChanged())
    {
        /* Get old data from cache: */
        const UIDataSettingsGlobalUpdate &oldData = m_pCache->base();
        /* Get new data from cache: */
        const UIDataSettingsGlobalUpdate &newData = m_pCache->data();

        /* Save new data from cache: */
        if (   fSuccess
            && newData != oldData)
        {
            /* We still prefer data to be saved to extra-data as well, for backward compartibility: */
            /* fSuccess = */ gEDataManager->setApplicationUpdateData(newData.m_guiUpdateData.data());
            /* Save new data to host finally: */
            const VBoxUpdateData guiUpdateData = newData.m_guiUpdateData;
            fSuccess = guiUpdateData.save(m_host);
        }
    }
    /* Return result: */
    return fSuccess;
}
