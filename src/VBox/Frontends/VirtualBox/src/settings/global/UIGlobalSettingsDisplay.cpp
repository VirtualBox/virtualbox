/* $Id: UIGlobalSettingsDisplay.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIGlobalSettingsDisplay class implementation.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#include "UIDesktopWidgetWatchdog.h"
#include "UIExtraDataManager.h"
#include "UIFontScaleEditor.h"
#include "UIDisplayFeaturesEditor.h"
#include "UIGlobalSettingsDisplay.h"
#include "UIMaximumGuestScreenSizeEditor.h"
#include "UIScaleFactorEditor.h"


/** Global settings: Display page data structure. */
struct UIDataSettingsGlobalDisplay
{
    /** Constructs data. */
    UIDataSettingsGlobalDisplay()
        : m_fActivateHoveredMachineWindow(false)
        , m_fDisableHostScreenSaver(false)
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataSettingsGlobalDisplay &other) const
    {
        return    true
               && (m_guiMaximumGuestScreenSizeValue == other.m_guiMaximumGuestScreenSizeValue)
               && (m_scaleFactors == other.m_scaleFactors)
               && (m_iFontScalingFactor == other.m_iFontScalingFactor)
               && (m_fActivateHoveredMachineWindow == other.m_fActivateHoveredMachineWindow)
               && (m_fDisableHostScreenSaver == other.m_fDisableHostScreenSaver)
                  ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataSettingsGlobalDisplay &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataSettingsGlobalDisplay &other) const { return !equal(other); }

    /** Holds the maximum guest-screen size value. */
    UIMaximumGuestScreenSizeValue  m_guiMaximumGuestScreenSizeValue;
    /** Holds the guest screen scale-factor. */
    QList<double>                  m_scaleFactors;
    /** Holds the font scaling factor. */
    int                            m_iFontScalingFactor;
    /** Holds whether we should automatically activate machine window under the mouse cursor. */
    bool                           m_fActivateHoveredMachineWindow;
    /** Holds whether we should disable host sceen saver on a vm is running. */
    bool                           m_fDisableHostScreenSaver;
};


/*********************************************************************************************************************************
*   Class UIGlobalSettingsDisplay implementation.                                                                                *
*********************************************************************************************************************************/

UIGlobalSettingsDisplay::UIGlobalSettingsDisplay()
    : m_pCache(0)
    , m_pEditorMaximumGuestScreenSize(0)
    , m_pEditorScaleFactor(0)
    , m_pFontScaleEditor(0)
    , m_pEditorDisplayFeatures(0)
{
    prepare();
}

UIGlobalSettingsDisplay::~UIGlobalSettingsDisplay()
{
    cleanup();
}

bool UIGlobalSettingsDisplay::changed() const
{
    return m_pCache ? m_pCache->wasChanged() : false;
}

void UIGlobalSettingsDisplay::loadToCacheFrom(QVariant &data)
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Fetch data to properties: */
    UISettingsPageGlobal::fetchData(data);

    /* Clear cache initially: */
    m_pCache->clear();

    /* Cache old data: */
    UIDataSettingsGlobalDisplay oldData;
    oldData.m_guiMaximumGuestScreenSizeValue = UIMaximumGuestScreenSizeValue(gEDataManager->maxGuestResolutionPolicy(),
                                                                             gEDataManager->maxGuestResolutionForPolicyFixed());
    oldData.m_scaleFactors = gEDataManager->scaleFactors(UIExtraDataManager::GlobalID);
    oldData.m_iFontScalingFactor = gEDataManager->fontScaleFactor();
    oldData.m_fActivateHoveredMachineWindow = gEDataManager->activateHoveredMachineWindow();
#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
    oldData.m_fDisableHostScreenSaver = gEDataManager->disableHostScreenSaver();
#endif
    m_pCache->cacheInitialData(oldData);

    /* Upload properties to data: */
    UISettingsPageGlobal::uploadData(data);
}

void UIGlobalSettingsDisplay::getFromCache()
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Load old data from cache: */
    const UIDataSettingsGlobalDisplay &oldData = m_pCache->base();
    if (m_pEditorMaximumGuestScreenSize)
        m_pEditorMaximumGuestScreenSize->setValue(oldData.m_guiMaximumGuestScreenSizeValue);
    if (m_pEditorScaleFactor)
    {
        m_pEditorScaleFactor->setScaleFactors(oldData.m_scaleFactors);
        m_pEditorScaleFactor->setMonitorCount(UIDesktopWidgetWatchdog::screenCount());
    }
    if (m_pFontScaleEditor)
        m_pFontScaleEditor->setFontScaleFactor(oldData.m_iFontScalingFactor);
    if (m_pEditorDisplayFeatures)
    {
        m_pEditorDisplayFeatures->setActivateOnMouseHover(oldData.m_fActivateHoveredMachineWindow);
        m_pEditorDisplayFeatures->setDisableHostScreenSaver(oldData.m_fDisableHostScreenSaver);
    }
}

void UIGlobalSettingsDisplay::putToCache()
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Prepare new data: */
    UIDataSettingsGlobalDisplay newData = m_pCache->base();

    /* Cache new data: */
    if (m_pEditorMaximumGuestScreenSize)
        newData.m_guiMaximumGuestScreenSizeValue = m_pEditorMaximumGuestScreenSize->value();
    if (m_pEditorScaleFactor)
        newData.m_scaleFactors = m_pEditorScaleFactor->scaleFactors();
    if (m_pFontScaleEditor)
        newData.m_iFontScalingFactor = m_pFontScaleEditor->fontScaleFactor();
    if (m_pEditorDisplayFeatures)
    {
        newData.m_fActivateHoveredMachineWindow = m_pEditorDisplayFeatures->activateOnMouseHover();
        newData.m_fDisableHostScreenSaver = m_pEditorDisplayFeatures->disableHostScreenSaver();
    }
    m_pCache->cacheCurrentData(newData);
}

void UIGlobalSettingsDisplay::saveFromCacheTo(QVariant &data)
{
    /* Fetch data to properties: */
    UISettingsPageGlobal::fetchData(data);

    /* Update data and failing state: */
    setFailed(!saveData());

    /* Upload properties to data: */
    UISettingsPageGlobal::uploadData(data);
}

void UIGlobalSettingsDisplay::sltRetranslateUI()
{
    updateMinimumLayoutHint();
}

void UIGlobalSettingsDisplay::handleFilterChange()
{
    updateMinimumLayoutHint();
}

void UIGlobalSettingsDisplay::prepare()
{
    /* Prepare cache: */
    m_pCache = new UISettingsCacheGlobalDisplay;
    AssertPtrReturnVoid(m_pCache);

    /* Prepare everything: */
    prepareWidgets();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIGlobalSettingsDisplay::prepareWidgets()
{
    /* Prepare main layout: */
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    if (pLayout)
    {
        /* Prepare 'maximum guest screen size' editor: */
        m_pEditorMaximumGuestScreenSize = new UIMaximumGuestScreenSizeEditor(this);
        if (m_pEditorMaximumGuestScreenSize)
        {
            addEditor(m_pEditorMaximumGuestScreenSize);
            pLayout->addWidget(m_pEditorMaximumGuestScreenSize);
        }

        /* Prepare 'scale-factor' editor: */
        m_pEditorScaleFactor = new UIScaleFactorEditor(this);
        if (m_pEditorScaleFactor)
        {
            addEditor(m_pEditorScaleFactor);
            pLayout->addWidget(m_pEditorScaleFactor);
        }

        /* Prepare 'font scale' editor: */
        m_pFontScaleEditor = new UIFontScaleEditor(this);
        if (m_pFontScaleEditor)
        {
            addEditor(m_pFontScaleEditor);
            pLayout->addWidget(m_pFontScaleEditor);
        }

        /* Prepare 'display features' editor: */
        m_pEditorDisplayFeatures = new UIDisplayFeaturesEditor(this);
        if (m_pEditorDisplayFeatures)
        {
            addEditor(m_pEditorDisplayFeatures);
            pLayout->addWidget(m_pEditorDisplayFeatures);
        }

        /* Add stretch to the end: */
        pLayout->addStretch();
    }
}

void UIGlobalSettingsDisplay::cleanup()
{
    /* Cleanup cache: */
    delete m_pCache;
    m_pCache = 0;
}

bool UIGlobalSettingsDisplay::saveData()
{
    /* Sanity check: */
    if (!m_pCache)
        return false;

    /* Prepare result: */
    bool fSuccess = true;
    /* Save display settings from cache: */
    if (   fSuccess
        && m_pCache->wasChanged())
    {
        /* Get old data from cache: */
        const UIDataSettingsGlobalDisplay &oldData = m_pCache->base();
        /* Get new data from cache: */
        const UIDataSettingsGlobalDisplay &newData = m_pCache->data();

        /* Save maximum guest screen size and policy: */
        if (   fSuccess
            && newData.m_guiMaximumGuestScreenSizeValue != oldData.m_guiMaximumGuestScreenSizeValue)
            /* fSuccess = */ gEDataManager->setMaxGuestScreenResolution(newData.m_guiMaximumGuestScreenSizeValue.m_enmPolicy,
                                                                        newData.m_guiMaximumGuestScreenSizeValue.m_size);
        /* Save guest-screen scale-factor: */
        if (   fSuccess
            && newData.m_scaleFactors != oldData.m_scaleFactors)
            /* fSuccess = */ gEDataManager->setScaleFactors(newData.m_scaleFactors, UIExtraDataManager::GlobalID);
        /* Save font scale factor: */
        if (   fSuccess
            && newData.m_iFontScalingFactor != oldData.m_iFontScalingFactor)
            /* fSuccess = */ gEDataManager->setFontScaleFactor(newData.m_iFontScalingFactor);
        /* Save whether hovered machine-window should be activated automatically: */
        if (   fSuccess
            && newData.m_fActivateHoveredMachineWindow != oldData.m_fActivateHoveredMachineWindow)
            /* fSuccess = */ gEDataManager->setActivateHoveredMachineWindow(newData.m_fActivateHoveredMachineWindow);
#if defined(VBOX_WS_WIN) || defined(VBOX_WS_NIX)
        /* Save whether the host screen saver is to be disable when a vm is running: */
        if (   fSuccess
            && newData.m_fDisableHostScreenSaver != oldData.m_fDisableHostScreenSaver)
            /* fSuccess = */ gEDataManager->setDisableHostScreenSaver(newData.m_fDisableHostScreenSaver);
#endif /* VBOX_WS_WIN || VBOX_WS_NIX */
    }
    /* Return result: */
    return fSuccess;
}

void UIGlobalSettingsDisplay::updateMinimumLayoutHint()
{
    /* These editors have own labels, but we want them to be properly layouted according to each other: */
    int iMinimumLayoutHint = 0;
    if (m_pEditorMaximumGuestScreenSize && !m_pEditorMaximumGuestScreenSize->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorMaximumGuestScreenSize->minimumLabelHorizontalHint());
    if (m_pEditorScaleFactor && !m_pEditorScaleFactor->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorScaleFactor->minimumLabelHorizontalHint());
    if (m_pFontScaleEditor && !m_pFontScaleEditor->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pFontScaleEditor->minimumLabelHorizontalHint());
    if (m_pEditorDisplayFeatures && !m_pEditorDisplayFeatures->isHidden())
        iMinimumLayoutHint = qMax(iMinimumLayoutHint, m_pEditorDisplayFeatures->minimumLabelHorizontalHint());
    if (m_pEditorMaximumGuestScreenSize)
        m_pEditorMaximumGuestScreenSize->setMinimumLayoutIndent(iMinimumLayoutHint);
    if (m_pEditorScaleFactor)
        m_pEditorScaleFactor->setMinimumLayoutIndent(iMinimumLayoutHint);
    if (m_pFontScaleEditor)
        m_pFontScaleEditor->setMinimumLayoutIndent(iMinimumLayoutHint);
    if (m_pEditorDisplayFeatures)
        m_pEditorDisplayFeatures->setMinimumLayoutIndent(iMinimumLayoutHint);
}
