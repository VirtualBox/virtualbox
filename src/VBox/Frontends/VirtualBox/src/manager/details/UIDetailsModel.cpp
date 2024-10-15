/* $Id: UIDetailsModel.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDetailsModel class implementation.
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
#include <QAction>
#include <QGraphicsScene>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsView>
#include <QMenu>
#include <QMetaEnum>

/* GUI includes: */
#include "UICommon.h"
#include "UIConverter.h"
#include "UIDesktopWidgetWatchdog.h"
#include "UIDetails.h"
#include "UIDetailsContextMenu.h"
#include "UIDetailsElement.h"
#include "UIDetailsGroup.h"
#include "UIDetailsModel.h"
#include "UIDetailsView.h"
#include "UIExtraDataManager.h"

/* Other VBox includes: */
#include "iprt/assert.h"


/*********************************************************************************************************************************
*   Class UIDetailsModel implementation.                                                                                         *
*********************************************************************************************************************************/

UIDetailsModel::UIDetailsModel(UIDetails *pParent)
    : QObject(pParent)
    , m_pDetails(pParent)
    , m_pScene(0)
    , m_pRoot(0)
    , m_pAnimationCallback(0)
    , m_pContextMenu(0)
{
    prepare();
}

UIDetailsModel::~UIDetailsModel()
{
    cleanup();
}

void UIDetailsModel::init()
{
    /* Install root as event-filter for scene view,
     * we need QEvent::Scroll events from it: */
    root()->installEventFilterHelper(view());
}

QGraphicsScene *UIDetailsModel::scene() const
{
    return m_pScene;
}

UIDetailsView *UIDetailsModel::view() const
{
    return scene() && !scene()->views().isEmpty() ? qobject_cast<UIDetailsView*>(scene()->views().first()) : 0;
}

QGraphicsView *UIDetailsModel::paintDevice() const
{
    return scene() && !scene()->views().isEmpty() ? scene()->views().first() : 0;
}

QGraphicsItem *UIDetailsModel::itemAt(const QPointF &position) const
{
    return scene()->itemAt(position, QTransform());
}

UIDetailsItem *UIDetailsModel::root() const
{
    return m_pRoot;
}

void UIDetailsModel::updateLayout()
{
    /* Initialize variables: */
    AssertPtrReturnVoid(view());
    AssertPtrReturnVoid(root());
    const QSize viewportSize = view()->size();
    const int iViewportWidth = viewportSize.width();
    const int iViewportHeight = root()->minimumSizeHint().toSize().height();

    /* Move root: */
    root()->setPos(0, 0);
    /* Resize root: */
    root()->resize(iViewportWidth, iViewportHeight);
    /* Layout root content: */
    root()->updateLayout();
}

void UIDetailsModel::setItems(const QList<UIVirtualMachineItem*> &items)
{
    m_pRoot->buildGroup(items);
}

void UIDetailsModel::setCategories(const QMap<DetailsElementType, bool> &categories)
{
    m_categories = categories;
    m_pRoot->rebuildGroup();
    m_pContextMenu->updateCategoryStates();

    /* Save categories: */
    gEDataManager->setSelectorWindowDetailsElements(m_categories);
}

void UIDetailsModel::setOptionsGeneral(UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral fOptionsGeneral)
{
    m_fOptionsGeneral = fOptionsGeneral;
    m_pRoot->rebuildGroup();
    m_pContextMenu->updateOptionStates(DetailsElementType_General);

    /* Save general options: */
    const QMetaObject &smo = UIExtraDataMetaDefs::staticMetaObject;
    const int iEnumIndex = smo.indexOfEnumerator("DetailsElementOptionTypeGeneral");
    if (iEnumIndex != -1)
    {
        bool fDefault = true;
        QStringList options;
        const QMetaEnum metaEnum = smo.enumerator(iEnumIndex);
        for (int iKeyIndex = 0; iKeyIndex < metaEnum.keyCount(); ++iKeyIndex)
        {
            /* Prepare current option type: */
            const UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral enmOptionType =
                static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral>(metaEnum.keyToValue(metaEnum.key(iKeyIndex)));
            /* Skip invalid and default types: */
            if (   enmOptionType == UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral_Invalid
                || enmOptionType == UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral_Default)
                continue;
            /* If option type enabled: */
            if (m_fOptionsGeneral & enmOptionType)
            {
                /* Add it to the list: */
                options << gpConverter->toInternalString(enmOptionType);
                /* Make sure item is included by default: */
                if (!(UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral_Default & enmOptionType))
                    fDefault = false;
            }
            /* If option type disabled: */
            else
            {
                /* Make sure item is excluded by default: */
                if (UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral_Default & enmOptionType)
                    fDefault = false;
            }
            /* Save options: */
            if (!fDefault)
                gEDataManager->setVBoxManagerDetailsPaneElementOptions(DetailsElementType_General, options);
            else
                gEDataManager->setVBoxManagerDetailsPaneElementOptions(DetailsElementType_General, QStringList());
        }
    }
}

void UIDetailsModel::setOptionsSystem(UIExtraDataMetaDefs::DetailsElementOptionTypeSystem fOptionsSystem)
{
    m_fOptionsSystem = fOptionsSystem;
    m_pRoot->rebuildGroup();
    m_pContextMenu->updateOptionStates(DetailsElementType_System);

    /* Save system options: */
    const QMetaObject &smo = UIExtraDataMetaDefs::staticMetaObject;
    const int iEnumIndex = smo.indexOfEnumerator("DetailsElementOptionTypeSystem");
    if (iEnumIndex != -1)
    {
        bool fDefault = true;
        QStringList options;
        const QMetaEnum metaEnum = smo.enumerator(iEnumIndex);
        for (int iKeyIndex = 0; iKeyIndex < metaEnum.keyCount(); ++iKeyIndex)
        {
            /* Prepare current option type: */
            const UIExtraDataMetaDefs::DetailsElementOptionTypeSystem enmOptionType =
                static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeSystem>(metaEnum.keyToValue(metaEnum.key(iKeyIndex)));
            /* Skip invalid and default types: */
            if (   enmOptionType == UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_Invalid
                || enmOptionType == UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_Default)
                continue;
            /* If option type enabled: */
            if (m_fOptionsSystem & enmOptionType)
            {
                /* Add it to the list: */
                options << gpConverter->toInternalString(enmOptionType);
                /* Make sure item is included by default: */
                if (!(UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_Default & enmOptionType))
                    fDefault = false;
            }
            /* If option type disabled: */
            else
            {
                /* Make sure item is excluded by default: */
                if (UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_Default & enmOptionType)
                    fDefault = false;
            }
            /* Save options: */
            if (!fDefault)
                gEDataManager->setVBoxManagerDetailsPaneElementOptions(DetailsElementType_System, options);
            else
                gEDataManager->setVBoxManagerDetailsPaneElementOptions(DetailsElementType_System, QStringList());
        }
    }
}

void UIDetailsModel::setOptionsDisplay(UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay fOptionsDisplay)
{
    m_fOptionsDisplay = fOptionsDisplay;
    m_pRoot->rebuildGroup();
    m_pContextMenu->updateOptionStates(DetailsElementType_Display);

    /* Save display options: */
    const QMetaObject &smo = UIExtraDataMetaDefs::staticMetaObject;
    const int iEnumIndex = smo.indexOfEnumerator("DetailsElementOptionTypeDisplay");
    if (iEnumIndex != -1)
    {
        bool fDefault = true;
        QStringList options;
        const QMetaEnum metaEnum = smo.enumerator(iEnumIndex);
        for (int iKeyIndex = 0; iKeyIndex < metaEnum.keyCount(); ++iKeyIndex)
        {
            /* Prepare current option type: */
            const UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay enmOptionType =
                static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay>(metaEnum.keyToValue(metaEnum.key(iKeyIndex)));
            /* Skip invalid and default types: */
            if (   enmOptionType == UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_Invalid
                || enmOptionType == UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_Default)
                continue;
            /* If option type enabled: */
            if (m_fOptionsDisplay & enmOptionType)
            {
                /* Add it to the list: */
                options << gpConverter->toInternalString(enmOptionType);
                /* Make sure item is included by default: */
                if (!(UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_Default & enmOptionType))
                    fDefault = false;
            }
            /* If option type disabled: */
            else
            {
                /* Make sure item is excluded by default: */
                if (UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_Default & enmOptionType)
                    fDefault = false;
            }
            /* Save options: */
            if (!fDefault)
                gEDataManager->setVBoxManagerDetailsPaneElementOptions(DetailsElementType_Display, options);
            else
                gEDataManager->setVBoxManagerDetailsPaneElementOptions(DetailsElementType_Display, QStringList());
        }
    }
}

void UIDetailsModel::setOptionsStorage(UIExtraDataMetaDefs::DetailsElementOptionTypeStorage fOptionsStorage)
{
    m_fOptionsStorage = fOptionsStorage;
    m_pRoot->rebuildGroup();
    m_pContextMenu->updateOptionStates(DetailsElementType_Storage);

    /* Save storage options: */
    const QMetaObject &smo = UIExtraDataMetaDefs::staticMetaObject;
    const int iEnumIndex = smo.indexOfEnumerator("DetailsElementOptionTypeStorage");
    if (iEnumIndex != -1)
    {
        bool fDefault = true;
        QStringList options;
        const QMetaEnum metaEnum = smo.enumerator(iEnumIndex);
        for (int iKeyIndex = 0; iKeyIndex < metaEnum.keyCount(); ++iKeyIndex)
        {
            /* Prepare current option type: */
            const UIExtraDataMetaDefs::DetailsElementOptionTypeStorage enmOptionType =
                static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeStorage>(metaEnum.keyToValue(metaEnum.key(iKeyIndex)));
            /* Skip invalid and default types: */
            if (   enmOptionType == UIExtraDataMetaDefs::DetailsElementOptionTypeStorage_Invalid
                || enmOptionType == UIExtraDataMetaDefs::DetailsElementOptionTypeStorage_Default)
                continue;
            /* If option type enabled: */
            if (m_fOptionsStorage & enmOptionType)
            {
                /* Add it to the list: */
                options << gpConverter->toInternalString(enmOptionType);
                /* Make sure item is included by default: */
                if (!(UIExtraDataMetaDefs::DetailsElementOptionTypeStorage_Default & enmOptionType))
                    fDefault = false;
            }
            /* If option type disabled: */
            else
            {
                /* Make sure item is excluded by default: */
                if (UIExtraDataMetaDefs::DetailsElementOptionTypeStorage_Default & enmOptionType)
                    fDefault = false;
            }
            /* Save options: */
            if (!fDefault)
                gEDataManager->setVBoxManagerDetailsPaneElementOptions(DetailsElementType_Storage, options);
            else
                gEDataManager->setVBoxManagerDetailsPaneElementOptions(DetailsElementType_Storage, QStringList());
        }
    }
}

void UIDetailsModel::setOptionsAudio(UIExtraDataMetaDefs::DetailsElementOptionTypeAudio fOptionsAudio)
{
    m_fOptionsAudio = fOptionsAudio;
    m_pRoot->rebuildGroup();
    m_pContextMenu->updateOptionStates(DetailsElementType_Audio);

    /* Save audio options: */
    const QMetaObject &smo = UIExtraDataMetaDefs::staticMetaObject;
    const int iEnumIndex = smo.indexOfEnumerator("DetailsElementOptionTypeAudio");
    if (iEnumIndex != -1)
    {
        bool fDefault = true;
        QStringList options;
        const QMetaEnum metaEnum = smo.enumerator(iEnumIndex);
        for (int iKeyIndex = 0; iKeyIndex < metaEnum.keyCount(); ++iKeyIndex)
        {
            /* Prepare current option type: */
            const UIExtraDataMetaDefs::DetailsElementOptionTypeAudio enmOptionType =
                static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeAudio>(metaEnum.keyToValue(metaEnum.key(iKeyIndex)));
            /* Skip invalid and default types: */
            if (   enmOptionType == UIExtraDataMetaDefs::DetailsElementOptionTypeAudio_Invalid
                || enmOptionType == UIExtraDataMetaDefs::DetailsElementOptionTypeAudio_Default)
                continue;
            /* If option type enabled: */
            if (m_fOptionsAudio & enmOptionType)
            {
                /* Add it to the list: */
                options << gpConverter->toInternalString(enmOptionType);
                /* Make sure item is included by default: */
                if (!(UIExtraDataMetaDefs::DetailsElementOptionTypeAudio_Default & enmOptionType))
                    fDefault = false;
            }
            /* If option type disabled: */
            else
            {
                /* Make sure item is excluded by default: */
                if (UIExtraDataMetaDefs::DetailsElementOptionTypeAudio_Default & enmOptionType)
                    fDefault = false;
            }
            /* Save options: */
            if (!fDefault)
                gEDataManager->setVBoxManagerDetailsPaneElementOptions(DetailsElementType_Audio, options);
            else
                gEDataManager->setVBoxManagerDetailsPaneElementOptions(DetailsElementType_Audio, QStringList());
        }
    }
}

void UIDetailsModel::setOptionsNetwork(UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork fOptionsNetwork)
{
    m_fOptionsNetwork = fOptionsNetwork;
    m_pRoot->rebuildGroup();
    m_pContextMenu->updateOptionStates(DetailsElementType_Network);

    /* Save network options: */
    const QMetaObject &smo = UIExtraDataMetaDefs::staticMetaObject;
    const int iEnumIndex = smo.indexOfEnumerator("DetailsElementOptionTypeNetwork");
    if (iEnumIndex != -1)
    {
        bool fDefault = true;
        QStringList options;
        const QMetaEnum metaEnum = smo.enumerator(iEnumIndex);
        for (int iKeyIndex = 0; iKeyIndex < metaEnum.keyCount(); ++iKeyIndex)
        {
            /* Prepare current option type: */
            const UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork enmOptionType =
                static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork>(metaEnum.keyToValue(metaEnum.key(iKeyIndex)));
            /* Skip invalid and default types: */
            if (   enmOptionType == UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_Invalid
                || enmOptionType == UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_Default)
                continue;
            /* If option type enabled: */
            if (m_fOptionsNetwork & enmOptionType)
            {
                /* Add it to the list: */
                options << gpConverter->toInternalString(enmOptionType);
                /* Make sure item is included by default: */
                if (!(UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_Default & enmOptionType))
                    fDefault = false;
            }
            /* If option type disabled: */
            else
            {
                /* Make sure item is excluded by default: */
                if (UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_Default & enmOptionType)
                    fDefault = false;
            }
            /* Save options: */
            if (!fDefault)
                gEDataManager->setVBoxManagerDetailsPaneElementOptions(DetailsElementType_Network, options);
            else
                gEDataManager->setVBoxManagerDetailsPaneElementOptions(DetailsElementType_Network, QStringList());
        }
    }
}

void UIDetailsModel::setOptionsSerial(UIExtraDataMetaDefs::DetailsElementOptionTypeSerial fOptionsSerial)
{
    m_fOptionsSerial = fOptionsSerial;
    m_pRoot->rebuildGroup();
    m_pContextMenu->updateOptionStates(DetailsElementType_Serial);

    /* Save serial options: */
    const QMetaObject &smo = UIExtraDataMetaDefs::staticMetaObject;
    const int iEnumIndex = smo.indexOfEnumerator("DetailsElementOptionTypeSerial");
    if (iEnumIndex != -1)
    {
        bool fDefault = true;
        QStringList options;
        const QMetaEnum metaEnum = smo.enumerator(iEnumIndex);
        for (int iKeyIndex = 0; iKeyIndex < metaEnum.keyCount(); ++iKeyIndex)
        {
            /* Prepare current option type: */
            const UIExtraDataMetaDefs::DetailsElementOptionTypeSerial enmOptionType =
                static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeSerial>(metaEnum.keyToValue(metaEnum.key(iKeyIndex)));
            /* Skip invalid and default types: */
            if (   enmOptionType == UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_Invalid
                || enmOptionType == UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_Default)
                continue;
            /* If option type enabled: */
            if (m_fOptionsSerial & enmOptionType)
            {
                /* Add it to the list: */
                options << gpConverter->toInternalString(enmOptionType);
                /* Make sure item is included by default: */
                if (!(UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_Default & enmOptionType))
                    fDefault = false;
            }
            /* If option type disabled: */
            else
            {
                /* Make sure item is excluded by default: */
                if (UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_Default & enmOptionType)
                    fDefault = false;
            }
            /* Save options: */
            if (!fDefault)
                gEDataManager->setVBoxManagerDetailsPaneElementOptions(DetailsElementType_Serial, options);
            else
                gEDataManager->setVBoxManagerDetailsPaneElementOptions(DetailsElementType_Serial, QStringList());
        }
    }
}

void UIDetailsModel::setOptionsUsb(UIExtraDataMetaDefs::DetailsElementOptionTypeUsb fOptionsUsb)
{
    m_fOptionsUsb = fOptionsUsb;
    m_pRoot->rebuildGroup();
    m_pContextMenu->updateOptionStates(DetailsElementType_USB);

    /* Save USB options: */
    const QMetaObject &smo = UIExtraDataMetaDefs::staticMetaObject;
    const int iEnumIndex = smo.indexOfEnumerator("DetailsElementOptionTypeUsb");
    if (iEnumIndex != -1)
    {
        bool fDefault = true;
        QStringList options;
        const QMetaEnum metaEnum = smo.enumerator(iEnumIndex);
        for (int iKeyIndex = 0; iKeyIndex < metaEnum.keyCount(); ++iKeyIndex)
        {
            /* Prepare current option type: */
            const UIExtraDataMetaDefs::DetailsElementOptionTypeUsb enmOptionType =
                static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeUsb>(metaEnum.keyToValue(metaEnum.key(iKeyIndex)));
            /* Skip invalid and default types: */
            if (   enmOptionType == UIExtraDataMetaDefs::DetailsElementOptionTypeUsb_Invalid
                || enmOptionType == UIExtraDataMetaDefs::DetailsElementOptionTypeUsb_Default)
                continue;
            /* If option type enabled: */
            if (m_fOptionsUsb & enmOptionType)
            {
                /* Add it to the list: */
                options << gpConverter->toInternalString(enmOptionType);
                /* Make sure item is included by default: */
                if (!(UIExtraDataMetaDefs::DetailsElementOptionTypeUsb_Default & enmOptionType))
                    fDefault = false;
            }
            /* If option type disabled: */
            else
            {
                /* Make sure item is excluded by default: */
                if (UIExtraDataMetaDefs::DetailsElementOptionTypeUsb_Default & enmOptionType)
                    fDefault = false;
            }
            /* Save options: */
            if (!fDefault)
                gEDataManager->setVBoxManagerDetailsPaneElementOptions(DetailsElementType_USB, options);
            else
                gEDataManager->setVBoxManagerDetailsPaneElementOptions(DetailsElementType_USB, QStringList());
        }
    }
}

void UIDetailsModel::setOptionsSharedFolders(UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders fOptionsSharedFolders)
{
    m_fOptionsSharedFolders = fOptionsSharedFolders;
    m_pRoot->rebuildGroup();
    m_pContextMenu->updateOptionStates(DetailsElementType_SF);

    /* Save shared folders options: */
    const QMetaObject &smo = UIExtraDataMetaDefs::staticMetaObject;
    const int iEnumIndex = smo.indexOfEnumerator("DetailsElementOptionTypeSharedFolders");
    if (iEnumIndex != -1)
    {
        bool fDefault = true;
        QStringList options;
        const QMetaEnum metaEnum = smo.enumerator(iEnumIndex);
        for (int iKeyIndex = 0; iKeyIndex < metaEnum.keyCount(); ++iKeyIndex)
        {
            /* Prepare current option type: */
            const UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders enmOptionType =
                static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders>(metaEnum.keyToValue(metaEnum.key(iKeyIndex)));
            /* Skip invalid and default types: */
            if (   enmOptionType == UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders_Invalid
                || enmOptionType == UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders_Default)
                continue;
            /* If option type enabled: */
            if (m_fOptionsSharedFolders & enmOptionType)
            {
                /* Add it to the list: */
                options << gpConverter->toInternalString(enmOptionType);
                /* Make sure item is included by default: */
                if (!(UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders_Default & enmOptionType))
                    fDefault = false;
            }
            /* If option type disabled: */
            else
            {
                /* Make sure item is excluded by default: */
                if (UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders_Default & enmOptionType)
                    fDefault = false;
            }
            /* Save options: */
            if (!fDefault)
                gEDataManager->setVBoxManagerDetailsPaneElementOptions(DetailsElementType_SF, options);
            else
                gEDataManager->setVBoxManagerDetailsPaneElementOptions(DetailsElementType_SF, QStringList());
        }
    }
}

void UIDetailsModel::setOptionsUserInterface(UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface fOptionsUserInterface)
{
    m_fOptionsUserInterface = fOptionsUserInterface;
    m_pRoot->rebuildGroup();
    m_pContextMenu->updateOptionStates(DetailsElementType_UI);

    /* Save user interface options: */
    const QMetaObject &smo = UIExtraDataMetaDefs::staticMetaObject;
    const int iEnumIndex = smo.indexOfEnumerator("DetailsElementOptionTypeUserInterface");
    if (iEnumIndex != -1)
    {
        bool fDefault = true;
        QStringList options;
        const QMetaEnum metaEnum = smo.enumerator(iEnumIndex);
        for (int iKeyIndex = 0; iKeyIndex < metaEnum.keyCount(); ++iKeyIndex)
        {
            /* Prepare current option type: */
            const UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface enmOptionType =
                static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface>(metaEnum.keyToValue(metaEnum.key(iKeyIndex)));
            /* Skip invalid and default types: */
            if (   enmOptionType == UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_Invalid
                || enmOptionType == UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_Default)
                continue;
            /* If option type enabled: */
            if (m_fOptionsUserInterface & enmOptionType)
            {
                /* Add it to the list: */
                options << gpConverter->toInternalString(enmOptionType);
                /* Make sure item is included by default: */
                if (!(UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_Default & enmOptionType))
                    fDefault = false;
            }
            /* If option type disabled: */
            else
            {
                /* Make sure item is excluded by default: */
                if (UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_Default & enmOptionType)
                    fDefault = false;
            }
            /* Save options: */
            if (!fDefault)
                gEDataManager->setVBoxManagerDetailsPaneElementOptions(DetailsElementType_UI, options);
            else
                gEDataManager->setVBoxManagerDetailsPaneElementOptions(DetailsElementType_UI, QStringList());
        }
    }
}

void UIDetailsModel::setOptionsDescription(UIExtraDataMetaDefs::DetailsElementOptionTypeDescription fOptionsDescription)
{
    m_fOptionsDescription = fOptionsDescription;
    m_pRoot->rebuildGroup();
    m_pContextMenu->updateOptionStates(DetailsElementType_Description);

    /* Save description options: */
    const QMetaObject &smo = UIExtraDataMetaDefs::staticMetaObject;
    const int iEnumIndex = smo.indexOfEnumerator("DetailsElementOptionTypeDescription");
    if (iEnumIndex != -1)
    {
        bool fDefault = true;
        QStringList options;
        const QMetaEnum metaEnum = smo.enumerator(iEnumIndex);
        for (int iKeyIndex = 0; iKeyIndex < metaEnum.keyCount(); ++iKeyIndex)
        {
            /* Prepare current option type: */
            const UIExtraDataMetaDefs::DetailsElementOptionTypeDescription enmOptionType =
                static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeDescription>(metaEnum.keyToValue(metaEnum.key(iKeyIndex)));
            /* Skip invalid and default types: */
            if (   enmOptionType == UIExtraDataMetaDefs::DetailsElementOptionTypeDescription_Invalid
                || enmOptionType == UIExtraDataMetaDefs::DetailsElementOptionTypeDescription_Default)
                continue;
            /* If option type enabled: */
            if (m_fOptionsDescription & enmOptionType)
            {
                /* Add it to the list: */
                options << gpConverter->toInternalString(enmOptionType);
                /* Make sure item is included by default: */
                if (!(UIExtraDataMetaDefs::DetailsElementOptionTypeDescription_Default & enmOptionType))
                    fDefault = false;
            }
            /* If option type disabled: */
            else
            {
                /* Make sure item is excluded by default: */
                if (UIExtraDataMetaDefs::DetailsElementOptionTypeDescription_Default & enmOptionType)
                    fDefault = false;
            }
            /* Save options: */
            if (!fDefault)
                gEDataManager->setVBoxManagerDetailsPaneElementOptions(DetailsElementType_Description, options);
            else
                gEDataManager->setVBoxManagerDetailsPaneElementOptions(DetailsElementType_Description, QStringList());
        }
    }
}

void UIDetailsModel::sltHandleViewResize()
{
    updateLayout();
}

void UIDetailsModel::sltHandleToggleStarted()
{
    m_pRoot->stopBuildingGroup();
}

void UIDetailsModel::sltHandleToggleFinished()
{
    m_pRoot->rebuildGroup();
}

void UIDetailsModel::sltHandleExtraDataCategoriesChange()
{
    loadDetailsCategories();
    m_pContextMenu->updateCategoryStates();
    m_pRoot->rebuildGroup();
}

void UIDetailsModel::sltHandleExtraDataOptionsChange(DetailsElementType enmType)
{
    loadDetailsOptions(enmType);
    m_pContextMenu->updateOptionStates(enmType);
    m_pRoot->rebuildGroup();
}

bool UIDetailsModel::eventFilter(QObject *pObject, QEvent *pEvent)
{
    /* Handle allowed context-menu events: */
    if (pObject == scene() && pEvent->type() == QEvent::GraphicsSceneContextMenu)
        return processContextMenuEvent(static_cast<QGraphicsSceneContextMenuEvent*>(pEvent));

    /* Call to base-class: */
    return QObject::eventFilter(pObject, pEvent);
}

void UIDetailsModel::sltToggleElements(DetailsElementType type, bool fToggled)
{
    /* Make sure it is not started yet: */
    if (m_pAnimationCallback)
        return;

    /* Prepare/configure animation callback: */
    m_pAnimationCallback = new UIDetailsElementAnimationCallback(this, type, fToggled);
    connect(m_pAnimationCallback, &UIDetailsElementAnimationCallback::sigAllAnimationFinished,
            this, &UIDetailsModel::sltToggleAnimationFinished, Qt::QueuedConnection);
    /* For each the set of the group: */
    foreach (UIDetailsItem *pSetItem, m_pRoot->items())
    {
        /* For each the element of the set: */
        foreach (UIDetailsItem *pElementItem, pSetItem->items())
        {
            /* Get each element: */
            UIDetailsElement *pElement = pElementItem->toElement();
            /* Check if this element is of required type: */
            if (pElement->elementType() == type)
            {
                if (fToggled && pElement->isClosed())
                {
                    m_pAnimationCallback->addNotifier(pElement);
                    pElement->open();
                }
                else if (!fToggled && pElement->isOpened())
                {
                    m_pAnimationCallback->addNotifier(pElement);
                    pElement->close();
                }
            }
        }
    }
    /* Update layout: */
    updateLayout();
}

void UIDetailsModel::sltDetachCOM()
{
    /* Clear model of unwanted machine-items: */
    setItems(QList<UIVirtualMachineItem*>());
}

void UIDetailsModel::sltToggleAnimationFinished(DetailsElementType enmType, bool fToggled)
{
    /* Cleanup animation callback: */
    delete m_pAnimationCallback;
    m_pAnimationCallback = 0;

    /* Mark animation finished: */
    foreach (UIDetailsItem *pSetItem, m_pRoot->items())
    {
        foreach (UIDetailsItem *pElementItem, pSetItem->items())
        {
            UIDetailsElement *pElement = pElementItem->toElement();
            if (pElement->elementType() == enmType)
                pElement->markAnimationFinished();
        }
    }
    /* Update layout: */
    updateLayout();

    /* Update element open/close status: */
    if (m_categories.contains(enmType))
    {
        m_categories[enmType] = fToggled;
        gEDataManager->setSelectorWindowDetailsElements(m_categories);
    }
}

void UIDetailsModel::prepare()
{
    /* Install cleanup handler: */
    connect(&uiCommon(), &UICommon::sigAskToDetachCOM,
            this, &UIDetailsModel::sltDetachCOM);

    /* Prepare scene: */
    m_pScene = new QGraphicsScene(this);
    if (m_pScene)
        m_pScene->installEventFilter(this);

    /* Prepare root item: */
    m_pRoot = new UIDetailsGroup(scene());

    /* Prepare context-menu: */
    m_pContextMenu = new UIDetailsContextMenu(this);

    /* Load stuff: */
    loadDetailsCategories();
    loadDetailsOptions();
}

void UIDetailsModel::loadDetailsCategories()
{
    m_categories = gEDataManager->selectorWindowDetailsElements();
    m_pContextMenu->updateCategoryStates();
}

void UIDetailsModel::loadDetailsOptions(DetailsElementType enmType /* = DetailsElementType_Invalid */)
{
    /* We will handle DetailsElementType_Invalid as a request to load everything. */

    if (enmType == DetailsElementType_General || enmType == DetailsElementType_Invalid)
    {
        m_fOptionsGeneral = UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral_Invalid;
        foreach (const QString &strOption, gEDataManager->vboxManagerDetailsPaneElementOptions(DetailsElementType_General))
        {
            const UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral enmOption =
                gpConverter->fromInternalString<UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral>(strOption);
            if (enmOption != UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral_Invalid)
                m_fOptionsGeneral = static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral>(m_fOptionsGeneral | enmOption);
        }
        if (m_fOptionsGeneral == UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral_Invalid)
            m_fOptionsGeneral = UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral_Default;
    }

    if (enmType == DetailsElementType_System || enmType == DetailsElementType_Invalid)
    {
        m_fOptionsSystem = UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_Invalid;
        foreach (const QString &strOption, gEDataManager->vboxManagerDetailsPaneElementOptions(DetailsElementType_System))
        {
            const UIExtraDataMetaDefs::DetailsElementOptionTypeSystem enmOption =
                gpConverter->fromInternalString<UIExtraDataMetaDefs::DetailsElementOptionTypeSystem>(strOption);
            if (enmOption != UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_Invalid)
                m_fOptionsSystem = static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeSystem>(m_fOptionsSystem | enmOption);
        }
        if (m_fOptionsSystem == UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_Invalid)
            m_fOptionsSystem = UIExtraDataMetaDefs::DetailsElementOptionTypeSystem_Default;
    }

    if (enmType == DetailsElementType_Display || enmType == DetailsElementType_Invalid)
    {
        m_fOptionsDisplay = UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_Invalid;
        foreach (const QString &strOption, gEDataManager->vboxManagerDetailsPaneElementOptions(DetailsElementType_Display))
        {
            const UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay enmOption =
                gpConverter->fromInternalString<UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay>(strOption);
            if (enmOption != UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_Invalid)
                m_fOptionsDisplay = static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay>(m_fOptionsDisplay | enmOption);
        }
        if (m_fOptionsDisplay == UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_Invalid)
            m_fOptionsDisplay = UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay_Default;
    }

    if (enmType == DetailsElementType_Storage || enmType == DetailsElementType_Invalid)
    {
        m_fOptionsStorage = UIExtraDataMetaDefs::DetailsElementOptionTypeStorage_Invalid;
        foreach (const QString &strOption, gEDataManager->vboxManagerDetailsPaneElementOptions(DetailsElementType_Storage))
        {
            const UIExtraDataMetaDefs::DetailsElementOptionTypeStorage enmOption =
                gpConverter->fromInternalString<UIExtraDataMetaDefs::DetailsElementOptionTypeStorage>(strOption);
            if (enmOption != UIExtraDataMetaDefs::DetailsElementOptionTypeStorage_Invalid)
                m_fOptionsStorage = static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeStorage>(m_fOptionsStorage | enmOption);
        }
        if (m_fOptionsStorage == UIExtraDataMetaDefs::DetailsElementOptionTypeStorage_Invalid)
            m_fOptionsStorage = UIExtraDataMetaDefs::DetailsElementOptionTypeStorage_Default;
    }

    if (enmType == DetailsElementType_Audio || enmType == DetailsElementType_Invalid)
    {
        m_fOptionsAudio = UIExtraDataMetaDefs::DetailsElementOptionTypeAudio_Invalid;
        foreach (const QString &strOption, gEDataManager->vboxManagerDetailsPaneElementOptions(DetailsElementType_Audio))
        {
            const UIExtraDataMetaDefs::DetailsElementOptionTypeAudio enmOption =
                gpConverter->fromInternalString<UIExtraDataMetaDefs::DetailsElementOptionTypeAudio>(strOption);
            if (enmOption != UIExtraDataMetaDefs::DetailsElementOptionTypeAudio_Invalid)
                m_fOptionsAudio = static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeAudio>(m_fOptionsAudio | enmOption);
        }
        if (m_fOptionsAudio == UIExtraDataMetaDefs::DetailsElementOptionTypeAudio_Invalid)
            m_fOptionsAudio = UIExtraDataMetaDefs::DetailsElementOptionTypeAudio_Default;
    }

    if (enmType == DetailsElementType_Network || enmType == DetailsElementType_Invalid)
    {
        m_fOptionsNetwork = UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_Invalid;
        foreach (const QString &strOption, gEDataManager->vboxManagerDetailsPaneElementOptions(DetailsElementType_Network))
        {
            const UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork enmOption =
                gpConverter->fromInternalString<UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork>(strOption);
            if (enmOption != UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_Invalid)
                m_fOptionsNetwork = static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork>(m_fOptionsNetwork | enmOption);
        }
        if (m_fOptionsNetwork == UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_Invalid)
            m_fOptionsNetwork = UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork_Default;
    }

    if (enmType == DetailsElementType_Serial || enmType == DetailsElementType_Invalid)
    {
        m_fOptionsSerial = UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_Invalid;
        foreach (const QString &strOption, gEDataManager->vboxManagerDetailsPaneElementOptions(DetailsElementType_Serial))
        {
            const UIExtraDataMetaDefs::DetailsElementOptionTypeSerial enmOption =
                gpConverter->fromInternalString<UIExtraDataMetaDefs::DetailsElementOptionTypeSerial>(strOption);
            if (enmOption != UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_Invalid)
                m_fOptionsSerial = static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeSerial>(m_fOptionsSerial | enmOption);
        }
        if (m_fOptionsSerial == UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_Invalid)
            m_fOptionsSerial = UIExtraDataMetaDefs::DetailsElementOptionTypeSerial_Default;
    }

    if (enmType == DetailsElementType_USB || enmType == DetailsElementType_Invalid)
    {
        m_fOptionsUsb = UIExtraDataMetaDefs::DetailsElementOptionTypeUsb_Invalid;
        foreach (const QString &strOption, gEDataManager->vboxManagerDetailsPaneElementOptions(DetailsElementType_USB))
        {
            const UIExtraDataMetaDefs::DetailsElementOptionTypeUsb enmOption =
                gpConverter->fromInternalString<UIExtraDataMetaDefs::DetailsElementOptionTypeUsb>(strOption);
            if (enmOption != UIExtraDataMetaDefs::DetailsElementOptionTypeUsb_Invalid)
                m_fOptionsUsb = static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeUsb>(m_fOptionsUsb | enmOption);
        }
        if (m_fOptionsUsb == UIExtraDataMetaDefs::DetailsElementOptionTypeUsb_Invalid)
            m_fOptionsUsb = UIExtraDataMetaDefs::DetailsElementOptionTypeUsb_Default;
    }

    if (enmType == DetailsElementType_SF || enmType == DetailsElementType_Invalid)
    {
        m_fOptionsSharedFolders = UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders_Invalid;
        foreach (const QString &strOption, gEDataManager->vboxManagerDetailsPaneElementOptions(DetailsElementType_SF))
        {
            const UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders enmOption =
                gpConverter->fromInternalString<UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders>(strOption);
            if (enmOption != UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders_Invalid)
                m_fOptionsSharedFolders = static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders>(m_fOptionsSharedFolders | enmOption);
        }
        if (m_fOptionsSharedFolders == UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders_Invalid)
            m_fOptionsSharedFolders = UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders_Default;
    }

    if (enmType == DetailsElementType_UI || enmType == DetailsElementType_Invalid)
    {
        m_fOptionsUserInterface = UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_Invalid;
        foreach (const QString &strOption, gEDataManager->vboxManagerDetailsPaneElementOptions(DetailsElementType_UI))
        {
            const UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface enmOption =
                gpConverter->fromInternalString<UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface>(strOption);
            if (enmOption != UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_Invalid)
                m_fOptionsUserInterface = static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface>(m_fOptionsUserInterface | enmOption);
        }
        if (m_fOptionsUserInterface == UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_Invalid)
            m_fOptionsUserInterface = UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_Default;
    }

    if (enmType == DetailsElementType_Description || enmType == DetailsElementType_Invalid)
    {
        m_fOptionsDescription = UIExtraDataMetaDefs::DetailsElementOptionTypeDescription_Invalid;
        foreach (const QString &strOption, gEDataManager->vboxManagerDetailsPaneElementOptions(DetailsElementType_Description))
        {
            const UIExtraDataMetaDefs::DetailsElementOptionTypeDescription enmOption =
                gpConverter->fromInternalString<UIExtraDataMetaDefs::DetailsElementOptionTypeDescription>(strOption);
            if (enmOption != UIExtraDataMetaDefs::DetailsElementOptionTypeDescription_Invalid)
                m_fOptionsDescription = static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeDescription>(m_fOptionsDescription | enmOption);
        }
        if (m_fOptionsDescription == UIExtraDataMetaDefs::DetailsElementOptionTypeDescription_Invalid)
            m_fOptionsDescription = UIExtraDataMetaDefs::DetailsElementOptionTypeDescription_Default;
    }

    m_pContextMenu->updateOptionStates();
}

void UIDetailsModel::cleanup()
{
    /* Cleanup context-menu: */
    delete m_pContextMenu;
    m_pContextMenu = 0;

    /* Cleanup root item: */
    delete m_pRoot;
    m_pRoot = 0;

    /* Cleanup scene: */
    delete m_pScene;
    m_pScene = 0;
}

bool UIDetailsModel::processContextMenuEvent(QGraphicsSceneContextMenuEvent *pEvent)
{
    /* Pass preview context menu instead: */
    if (QGraphicsItem *pItem = itemAt(pEvent->scenePos()))
        if (pItem->type() == UIDetailsItemType_Preview)
            return false;

    /* Adjust the menu then show it: */
    const QRect availableGeo = gpDesktop->availableGeometry(pEvent->screenPos());
    QRect geo(pEvent->screenPos(), m_pContextMenu->minimumSizeHint());
    if (geo.topRight().x() > availableGeo.topRight().x())
        geo.adjust(availableGeo.topRight().x() - geo.topRight().x(), 0,
                   availableGeo.topRight().x() - geo.topRight().x(), 0);
    if (geo.bottomLeft().y() > availableGeo.bottomLeft().y())
        geo.adjust(0, availableGeo.bottomLeft().y() - geo.bottomLeft().y(),
                   0, availableGeo.bottomLeft().y() - geo.bottomLeft().y());
    m_pContextMenu->resize(geo.size());
    m_pContextMenu->move(geo.topLeft());
    m_pContextMenu->show();

    /* Filter: */
    return true;
}


/*********************************************************************************************************************************
*   Class UIDetailsElementAnimationCallback implementation.                                                                      *
*********************************************************************************************************************************/

UIDetailsElementAnimationCallback::UIDetailsElementAnimationCallback(QObject *pParent, DetailsElementType enmType, bool fToggled)
    : QObject(pParent)
    , m_enmType(enmType)
    , m_fToggled(fToggled)
{
}

void UIDetailsElementAnimationCallback::addNotifier(UIDetailsElement *pItem)
{
    /* Connect notifier: */
    connect(pItem, &UIDetailsElement::sigToggleElementFinished,
            this, &UIDetailsElementAnimationCallback::sltAnimationFinished);
    /* Remember notifier: */
    m_notifiers << pItem;
}

void UIDetailsElementAnimationCallback::sltAnimationFinished()
{
    /* Determine notifier: */
    UIDetailsElement *pItem = qobject_cast<UIDetailsElement*>(sender());
    /* Disconnect notifier: */
    disconnect(pItem, &UIDetailsElement::sigToggleElementFinished,
               this, &UIDetailsElementAnimationCallback::sltAnimationFinished);
    /* Remove notifier: */
    m_notifiers.removeAll(pItem);
    /* Check if we finished: */
    if (m_notifiers.isEmpty())
        emit sigAllAnimationFinished(m_enmType, m_fToggled);
}
