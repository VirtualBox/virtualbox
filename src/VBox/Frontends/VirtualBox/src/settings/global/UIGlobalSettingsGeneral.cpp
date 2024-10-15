/* $Id: UIGlobalSettingsGeneral.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGlobalSettingsGeneral class implementation.
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
#include <QDir>
#include <QVBoxLayout>

/* GUI includes: */
#include "UIDefaultMachineFolderEditor.h"
#include "UIErrorString.h"
#include "UIGlobalSettingsGeneral.h"
#include "UIVRDEAuthLibraryEditor.h"


/** Global settings: General page data structure. */
struct UIDataSettingsGlobalGeneral
{
    /** Constructs data. */
    UIDataSettingsGlobalGeneral()
        : m_strDefaultMachineFolder(QString())
        , m_strVRDEAuthLibrary(QString())
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataSettingsGlobalGeneral &other) const
    {
        return    true
               && (m_strDefaultMachineFolder == other.m_strDefaultMachineFolder)
               && (m_strVRDEAuthLibrary == other.m_strVRDEAuthLibrary)
                  ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataSettingsGlobalGeneral &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataSettingsGlobalGeneral &other) const { return !equal(other); }

    /** Holds the 'default machine folder' path. */
    QString  m_strDefaultMachineFolder;
    /** Holds the 'VRDE auth library' name. */
    QString  m_strVRDEAuthLibrary;
};


/*********************************************************************************************************************************
*   Class UIGlobalSettingsGeneral implementation.                                                                                *
*********************************************************************************************************************************/

UIGlobalSettingsGeneral::UIGlobalSettingsGeneral()
    : m_pCache(0)
    , m_pEditorDefaultMachineFolder(0)
    , m_pEditorVRDEAuthLibrary(0)
{
    prepare();
}

UIGlobalSettingsGeneral::~UIGlobalSettingsGeneral()
{
    cleanup();
}

bool UIGlobalSettingsGeneral::changed() const
{
    return m_pCache ? m_pCache->wasChanged() : false;
}

void UIGlobalSettingsGeneral::loadToCacheFrom(QVariant &data)
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Fetch data to properties: */
    UISettingsPageGlobal::fetchData(data);

    /* Clear cache initially: */
    m_pCache->clear();

    /* Cache old data: */
    UIDataSettingsGlobalGeneral oldData;
    oldData.m_strDefaultMachineFolder = m_properties.GetDefaultMachineFolder();
    oldData.m_strVRDEAuthLibrary = m_properties.GetVRDEAuthLibrary();
    m_pCache->cacheInitialData(oldData);

    /* Upload properties to data: */
    UISettingsPageGlobal::uploadData(data);
}

void UIGlobalSettingsGeneral::getFromCache()
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Load old data from cache: */
    const UIDataSettingsGlobalGeneral &oldData = m_pCache->base();
    if (m_pEditorDefaultMachineFolder)
        m_pEditorDefaultMachineFolder->setValue(oldData.m_strDefaultMachineFolder);
    if (m_pEditorVRDEAuthLibrary)
        m_pEditorVRDEAuthLibrary->setValue(oldData.m_strVRDEAuthLibrary);

    /* Revalidate: */
    revalidate();
}

void UIGlobalSettingsGeneral::putToCache()
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Prepare new data: */
    UIDataSettingsGlobalGeneral newData = m_pCache->base();

    /* Cache new data: */
    if (m_pEditorDefaultMachineFolder)
        newData.m_strDefaultMachineFolder = m_pEditorDefaultMachineFolder->value();
    if (m_pEditorVRDEAuthLibrary)
        newData.m_strVRDEAuthLibrary = m_pEditorVRDEAuthLibrary->value();
    m_pCache->cacheCurrentData(newData);
}

void UIGlobalSettingsGeneral::saveFromCacheTo(QVariant &data)
{
    /* Fetch data to properties: */
    UISettingsPageGlobal::fetchData(data);

    /* Update data and failing state: */
    setFailed(!saveData());

    /* Upload properties to data: */
    UISettingsPageGlobal::uploadData(data);
}

bool UIGlobalSettingsGeneral::validate(QList<UIValidationMessage> &messages)
{
    /* Pass by default: */
    bool fPass = true;

    /* Prepare message: */
    UIValidationMessage message;

    /* Check for the folder presence: */
    if (   m_pEditorDefaultMachineFolder
        && !QDir(m_pEditorDefaultMachineFolder->value()).exists())
    {
        message.second << tr("Default machine folder is missing.");
        fPass = false;
    }

    /* Serialize message: */
    if (!message.second.isEmpty())
        messages << message;

    /* Return result: */
    return fPass;
}

void UIGlobalSettingsGeneral::sltRetranslateUI()
{
    updateMinimumLayoutHint();
}

void UIGlobalSettingsGeneral::handleFilterChange()
{
    updateMinimumLayoutHint();
}

void UIGlobalSettingsGeneral::prepare()
{
    /* Prepare cache: */
    m_pCache = new UISettingsCacheGlobalGeneral;
    AssertPtrReturnVoid(m_pCache);

    /* Prepare everything: */
    prepareWidgets();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIGlobalSettingsGeneral::prepareWidgets()
{
    /* Prepare main layout: */
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    if (pLayout)
    {
        /* Prepare 'default machine folder' editor: */
        m_pEditorDefaultMachineFolder = new UIDefaultMachineFolderEditor(this);
        if (m_pEditorDefaultMachineFolder)
        {
            addEditor(m_pEditorDefaultMachineFolder);
            pLayout->addWidget(m_pEditorDefaultMachineFolder);
            connect(m_pEditorDefaultMachineFolder, &UIDefaultMachineFolderEditor::sigPathChanged,
                    this, &UIGlobalSettingsGeneral::revalidate);
        }

        /* Prepare 'VRDE auth library' editor: */
        m_pEditorVRDEAuthLibrary = new UIVRDEAuthLibraryEditor(this);
        if (m_pEditorVRDEAuthLibrary)
        {
            addEditor(m_pEditorVRDEAuthLibrary);
            pLayout->addWidget(m_pEditorVRDEAuthLibrary);
        }

        /* Add stretch to the end: */
        pLayout->addStretch();
    }
}

void UIGlobalSettingsGeneral::cleanup()
{
    /* Cleanup cache: */
    delete m_pCache;
    m_pCache = 0;
}

bool UIGlobalSettingsGeneral::saveData()
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
        const UIDataSettingsGlobalGeneral &oldData = m_pCache->base();
        /* Get new data from cache: */
        const UIDataSettingsGlobalGeneral &newData = m_pCache->data();

        /* Save 'default machine folder': */
        if (   fSuccess
            && newData.m_strDefaultMachineFolder != oldData.m_strDefaultMachineFolder)
        {
            m_properties.SetDefaultMachineFolder(newData.m_strDefaultMachineFolder);
            fSuccess = m_properties.isOk();
        }
        /* Save 'VRDE auth library': */
        if (   fSuccess
            && newData.m_strVRDEAuthLibrary != oldData.m_strVRDEAuthLibrary)
        {
            m_properties.SetVRDEAuthLibrary(newData.m_strVRDEAuthLibrary);
            fSuccess = m_properties.isOk();
        }

        /* Show error message if necessary: */
        if (!fSuccess)
            notifyOperationProgressError(UIErrorString::formatErrorInfo(m_properties));
    }
    /* Return result: */
    return fSuccess;
}

void UIGlobalSettingsGeneral::updateMinimumLayoutHint()
{
    /* These editors have own labels, but we want them to be properly layouted according to each other: */
    int iMinimumLayoutHint = 0;
    if (m_pEditorDefaultMachineFolder && !m_pEditorDefaultMachineFolder->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorDefaultMachineFolder->minimumLabelHorizontalHint());
    if (m_pEditorVRDEAuthLibrary && !m_pEditorVRDEAuthLibrary->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorVRDEAuthLibrary->minimumLabelHorizontalHint());
    if (m_pEditorDefaultMachineFolder)
        m_pEditorDefaultMachineFolder->setMinimumLayoutIndent(iMinimumLayoutHint);
    if (m_pEditorVRDEAuthLibrary)
        m_pEditorVRDEAuthLibrary->setMinimumLayoutIndent(iMinimumLayoutHint);
}
