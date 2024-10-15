/* $Id: UIGlobalSettingsProxy.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGlobalSettingsProxy class implementation.
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
#include <QVBoxLayout>

/* GUI includes: */
#include "UIGlobalSettingsProxy.h"
#include "UIErrorString.h"
#include "UIExtraDataManager.h"
#include "UIProxyFeaturesEditor.h"

/* COM includes: */
#include "CSystemProperties.h"


/** Global settings: Proxy page data structure. */
struct UIDataSettingsGlobalProxy
{
    /** Constructs data. */
    UIDataSettingsGlobalProxy()
        : m_enmProxyMode(KProxyMode_System)
        , m_strProxyHost(QString())
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataSettingsGlobalProxy &other) const
    {
        return    true
               && (m_enmProxyMode == other.m_enmProxyMode)
               && (m_strProxyHost == other.m_strProxyHost)
                  ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataSettingsGlobalProxy &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataSettingsGlobalProxy &other) const { return !equal(other); }

    /** Holds the proxy mode. */
    KProxyMode  m_enmProxyMode;
    /** Holds the proxy host. */
    QString     m_strProxyHost;
};


/*********************************************************************************************************************************
*   Class UIGlobalSettingsProxy implementation.                                                                                  *
*********************************************************************************************************************************/

UIGlobalSettingsProxy::UIGlobalSettingsProxy()
    : m_pCache(0)
{
    prepare();
}

UIGlobalSettingsProxy::~UIGlobalSettingsProxy()
{
    cleanup();
}

bool UIGlobalSettingsProxy::changed() const
{
    return m_pCache ? m_pCache->wasChanged() : false;
}

void UIGlobalSettingsProxy::loadToCacheFrom(QVariant &data)
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Fetch data to properties: */
    UISettingsPageGlobal::fetchData(data);

    /* Clear cache initially: */
    m_pCache->clear();

    /* Cache old data: */
    UIDataSettingsGlobalProxy oldData;
    oldData.m_enmProxyMode = m_properties.GetProxyMode();
    oldData.m_strProxyHost = m_properties.GetProxyURL();
    m_pCache->cacheInitialData(oldData);

    /* Upload properties to data: */
    UISettingsPageGlobal::uploadData(data);
}

void UIGlobalSettingsProxy::getFromCache()
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Load old data from cache: */
    const UIDataSettingsGlobalProxy &oldData = m_pCache->base();
    if (m_pEditorProxyFeatures)
    {
        m_pEditorProxyFeatures->setProxyMode(oldData.m_enmProxyMode);
        m_pEditorProxyFeatures->setProxyHost(oldData.m_strProxyHost);
    }

    /* Revalidate: */
    revalidate();
}

void UIGlobalSettingsProxy::putToCache()
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Prepare new data: */
    UIDataSettingsGlobalProxy newData = m_pCache->base();

    /* Cache new data: */
    if (m_pEditorProxyFeatures)
    {
        newData.m_enmProxyMode = m_pEditorProxyFeatures->proxyMode();
        newData.m_strProxyHost = m_pEditorProxyFeatures->proxyHost();
    }
    m_pCache->cacheCurrentData(newData);
}

void UIGlobalSettingsProxy::saveFromCacheTo(QVariant &data)
{
    /* Fetch data to properties: */
    UISettingsPageGlobal::fetchData(data);

    /* Update data and failing state: */
    setFailed(!saveData());

    /* Upload properties to data: */
    UISettingsPageGlobal::uploadData(data);
}

bool UIGlobalSettingsProxy::validate(QList<UIValidationMessage> &messages)
{
    /* Pass if proxy is disabled: */
    if (m_pEditorProxyFeatures->proxyMode() != KProxyMode_Manual)
        return true;

    /* Pass by default: */
    bool fPass = true;

    /* Prepare message: */
    UIValidationMessage message;

    /* Check for URL presence: */
    if (m_pEditorProxyFeatures->proxyHost().trimmed().isEmpty())
    {
        message.second << tr("No proxy URL is currently specified.");
        fPass = false;
    }

    else

    /* Check for URL validness: */
    if (!QUrl(m_pEditorProxyFeatures->proxyHost().trimmed()).isValid())
    {
        message.second << tr("Invalid proxy URL is currently specified.");
        fPass = true;
    }

    else

    /* Check for password presence: */
    if (!QUrl(m_pEditorProxyFeatures->proxyHost().trimmed()).password().isEmpty())
    {
        message.second << tr("You have provided a proxy password. "
                             "Please be aware that the password will be saved in plain text. "
                             "You may wish to configure a system-wide proxy instead and not "
                             "store application-specific settings.");
        fPass = true;
    }

    /* Serialize message: */
    if (!message.second.isEmpty())
        messages << message;

    /* Return result: */
    return fPass;
}

void UIGlobalSettingsProxy::sltRetranslateUI()
{
}

void UIGlobalSettingsProxy::prepare()
{
    /* Prepare cache: */
    m_pCache = new UISettingsCacheGlobalProxy;
    AssertPtrReturnVoid(m_pCache);

    /* Prepare everything: */
    prepareWidgets();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIGlobalSettingsProxy::prepareWidgets()
{
    /* Prepare main layout: */
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    if (pLayout)
    {
        /* Prepare 'proxy features' editor: */
        m_pEditorProxyFeatures = new UIProxyFeaturesEditor(this);
        if (m_pEditorProxyFeatures)
        {
            connect(m_pEditorProxyFeatures, &UIProxyFeaturesEditor::sigProxyModeChanged,
                    this, &UIGlobalSettingsProxy::revalidate);
            connect(m_pEditorProxyFeatures, &UIProxyFeaturesEditor::sigProxyHostChanged,
                    this, &UIGlobalSettingsProxy::revalidate);
            addEditor(m_pEditorProxyFeatures);
            pLayout->addWidget(m_pEditorProxyFeatures);
        }

        /* Add stretch to the end: */
        pLayout->addStretch();
    }
}

void UIGlobalSettingsProxy::cleanup()
{
    /* Cleanup cache: */
    delete m_pCache;
    m_pCache = 0;
}

bool UIGlobalSettingsProxy::saveData()
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
        const UIDataSettingsGlobalProxy &oldData = m_pCache->base();
        /* Get new data from cache: */
        const UIDataSettingsGlobalProxy &newData = m_pCache->data();

        /* Save new data from cache: */
        if (   fSuccess
            && newData.m_enmProxyMode != oldData.m_enmProxyMode)
        {
            m_properties.SetProxyMode(newData.m_enmProxyMode);
            fSuccess &= m_properties.isOk();
        }
        if (   fSuccess
            && newData.m_strProxyHost != oldData.m_strProxyHost)
        {
            m_properties.SetProxyURL(newData.m_strProxyHost);
            fSuccess &= m_properties.isOk();
        }

        /* Drop the old extra data setting if still around: */
        if (   fSuccess
            && !gEDataManager->proxySettings().isEmpty())
            /* fSuccess = */ gEDataManager->setProxySettings(QString());

        /* Show error message if necessary: */
        if (!fSuccess)
            notifyOperationProgressError(UIErrorString::formatErrorInfo(m_properties));
    }
    /* Return result: */
    return fSuccess;
}
