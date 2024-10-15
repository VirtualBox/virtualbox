/* $Id: UIGlobalSettingsInterface.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGlobalSettingsInterface class implementation.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include <QVariant>

/* GUI includes: */
#include "UIColorThemeEditor.h"
#include "UIExtraDataManager.h"
#include "UIGlobalSettingsInterface.h"


/** Global settings: User Interface page data structure. */
struct UIDataSettingsGlobalInterface
{
    /** Constructs data. */
    UIDataSettingsGlobalInterface()
        : m_enmColorTheme(UIColorThemeType_Auto)
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataSettingsGlobalInterface &other) const
    {
        return    true
               && (m_enmColorTheme == other.m_enmColorTheme)
                  ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataSettingsGlobalInterface &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataSettingsGlobalInterface &other) const { return !equal(other); }

    /** Holds the color-theme. */
    UIColorThemeType  m_enmColorTheme;
};


/*********************************************************************************************************************************
*   Class UIGlobalSettingsInterface implementation.                                                                              *
*********************************************************************************************************************************/

UIGlobalSettingsInterface::UIGlobalSettingsInterface()
    : m_pCache(0)
    , m_pEditorColorTheme(0)
{
    prepare();
}

UIGlobalSettingsInterface::~UIGlobalSettingsInterface()
{
    cleanup();
}

bool UIGlobalSettingsInterface::changed() const
{
    return m_pCache ? m_pCache->wasChanged() : false;
}

void UIGlobalSettingsInterface::loadToCacheFrom(QVariant &data)
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Fetch data to properties: */
    UISettingsPageGlobal::fetchData(data);

    /* Clear cache initially: */
    m_pCache->clear();

    /* Cache old data: */
    UIDataSettingsGlobalInterface oldData;
    oldData.m_enmColorTheme = gEDataManager->colorTheme();
    m_pCache->cacheInitialData(oldData);

    /* Upload properties to data: */
    UISettingsPageGlobal::uploadData(data);
}

void UIGlobalSettingsInterface::getFromCache()
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Load old data from cache: */
    const UIDataSettingsGlobalInterface &oldData = m_pCache->base();
    if (m_pEditorColorTheme)
        m_pEditorColorTheme->setValue(oldData.m_enmColorTheme);

    /* Revalidate: */
    revalidate();
}

void UIGlobalSettingsInterface::putToCache()
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Prepare new data: */
    UIDataSettingsGlobalInterface newData;

    /* Cache new data: */
    if (m_pEditorColorTheme)
        newData.m_enmColorTheme = m_pEditorColorTheme->value();
    m_pCache->cacheCurrentData(newData);
}

void UIGlobalSettingsInterface::saveFromCacheTo(QVariant &data)
{
    /* Fetch data to properties: */
    UISettingsPageGlobal::fetchData(data);

    /* Update data and failing state: */
    setFailed(!saveData());

    /* Upload properties to data: */
    UISettingsPageGlobal::uploadData(data);
}

void UIGlobalSettingsInterface::sltRetranslateUI()
{
}

void UIGlobalSettingsInterface::prepare()
{
    /* Prepare cache: */
    m_pCache = new UISettingsCacheGlobalInterface;
    AssertPtrReturnVoid(m_pCache);

    /* Prepare everything: */
    prepareWidgets();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIGlobalSettingsInterface::prepareWidgets()
{
    /* Prepare main layout: */
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    if (pLayout)
    {
        /* Prepare 'color-theme' editor: */
        m_pEditorColorTheme = new UIColorThemeEditor(this);
        if (m_pEditorColorTheme)
        {
            addEditor(m_pEditorColorTheme);
            pLayout->addWidget(m_pEditorColorTheme);
        }

        /* Add stretch to the end: */
        pLayout->addStretch();
    }
}

void UIGlobalSettingsInterface::cleanup()
{
    /* Cleanup cache: */
    delete m_pCache;
    m_pCache = 0;
}

bool UIGlobalSettingsInterface::saveData()
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
        const UIDataSettingsGlobalInterface &oldData = m_pCache->base();
        /* Get new data from cache: */
        const UIDataSettingsGlobalInterface &newData = m_pCache->data();

        /* Save 'color-theme': */
        if (   fSuccess
            && newData.m_enmColorTheme != oldData.m_enmColorTheme)
            /* fSuccess = */ gEDataManager->setColorTheme(newData.m_enmColorTheme);
    }
    /* Return result: */
    return fSuccess;
}
