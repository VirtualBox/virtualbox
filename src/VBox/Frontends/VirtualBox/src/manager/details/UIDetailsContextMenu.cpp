/* $Id: UIDetailsContextMenu.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDetailsContextMenu class implementation.
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
#include <QHBoxLayout>
#include <QListWidget>
#include <QMetaEnum>

/* GUI includes: */
#include "UIConverter.h"
#include "UIDetailsContextMenu.h"
#include "UIDetailsModel.h"
#include "UITranslationEventListener.h"

UIDetailsContextMenu::UIDetailsContextMenu(UIDetailsModel *pModel)
    : QWidget(0, Qt::Popup)
    , m_pModel(pModel)
    , m_pListWidgetCategories(0)
    , m_pListWidgetOptions(0)
{
    prepare();
}

void UIDetailsContextMenu::updateCategoryStates()
{
    /* Enumerate all the category items: */
    for (int i = 0; i < m_pListWidgetCategories->count(); ++i)
    {
        QListWidgetItem *pCategoryItem = m_pListWidgetCategories->item(i);
        if (pCategoryItem)
        {
            /* Apply check-state on per-enum basis: */
            const DetailsElementType enmCategoryType = pCategoryItem->data(DataField_Type).value<DetailsElementType>();
            pCategoryItem->setCheckState(m_pModel->categories().contains(enmCategoryType) ? Qt::Checked : Qt::Unchecked);
        }
    }
}

void UIDetailsContextMenu::updateOptionStates(DetailsElementType enmRequiredCategoryType /* = DetailsElementType_Invalid */)
{
    /* First make sure we really have category item selected: */
    QListWidgetItem *pCategoryItem = m_pListWidgetCategories->currentItem();
    if (!pCategoryItem)
        return;

    /* Then acquire category type and check if it is suitable: */
    const DetailsElementType enmCategoryType = pCategoryItem->data(DataField_Type).value<DetailsElementType>();
    if (enmRequiredCategoryType == DetailsElementType_Invalid)
        enmRequiredCategoryType = enmCategoryType;
    if (enmCategoryType != enmRequiredCategoryType)
        return;

    /* Handle known category types: */
    switch (enmRequiredCategoryType)
    {
        case DetailsElementType_General:
        {
            /* Enumerate all the option items: */
            for (int i = 0; i < m_pListWidgetOptions->count(); ++i)
            {
                QListWidgetItem *pOptionItem = m_pListWidgetOptions->item(i);
                if (pOptionItem)
                {
                    /* Apply check-state on per-enum basis: */
                    const UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral enmOptionType =
                        pOptionItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral>();
                    pOptionItem->setCheckState(m_pModel->optionsGeneral() & enmOptionType ? Qt::Checked : Qt::Unchecked);
                }
            }
            break;
        }
        case DetailsElementType_System:
        {
            /* Enumerate all the option items: */
            for (int i = 0; i < m_pListWidgetOptions->count(); ++i)
            {
                QListWidgetItem *pOptionItem = m_pListWidgetOptions->item(i);
                if (pOptionItem)
                {
                    /* Apply check-state on per-enum basis: */
                    const UIExtraDataMetaDefs::DetailsElementOptionTypeSystem enmOptionType =
                        pOptionItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeSystem>();
                    pOptionItem->setCheckState(m_pModel->optionsSystem() & enmOptionType ? Qt::Checked : Qt::Unchecked);
                }
            }
            break;
        }
        case DetailsElementType_Display:
        {
            /* Enumerate all the option items: */
            for (int i = 0; i < m_pListWidgetOptions->count(); ++i)
            {
                QListWidgetItem *pOptionItem = m_pListWidgetOptions->item(i);
                if (pOptionItem)
                {
                    /* Apply check-state on per-enum basis: */
                    const UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay enmOptionType =
                        pOptionItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay>();
                    pOptionItem->setCheckState(m_pModel->optionsDisplay() & enmOptionType ? Qt::Checked : Qt::Unchecked);
                }
            }
            break;
        }
        case DetailsElementType_Storage:
        {
            /* Enumerate all the option items: */
            for (int i = 0; i < m_pListWidgetOptions->count(); ++i)
            {
                QListWidgetItem *pOptionItem = m_pListWidgetOptions->item(i);
                if (pOptionItem)
                {
                    /* Apply check-state on per-enum basis: */
                    const UIExtraDataMetaDefs::DetailsElementOptionTypeStorage enmOptionType =
                        pOptionItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeStorage>();
                    pOptionItem->setCheckState(m_pModel->optionsStorage() & enmOptionType ? Qt::Checked : Qt::Unchecked);
                }
            }
            break;
        }
        case DetailsElementType_Audio:
        {
            /* Enumerate all the option items: */
            for (int i = 0; i < m_pListWidgetOptions->count(); ++i)
            {
                QListWidgetItem *pOptionItem = m_pListWidgetOptions->item(i);
                if (pOptionItem)
                {
                    /* Apply check-state on per-enum basis: */
                    const UIExtraDataMetaDefs::DetailsElementOptionTypeAudio enmOptionType =
                        pOptionItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeAudio>();
                    pOptionItem->setCheckState(m_pModel->optionsAudio() & enmOptionType ? Qt::Checked : Qt::Unchecked);
                }
            }
            break;
        }
        case DetailsElementType_Network:
        {
            /* Enumerate all the option items: */
            for (int i = 0; i < m_pListWidgetOptions->count(); ++i)
            {
                QListWidgetItem *pOptionItem = m_pListWidgetOptions->item(i);
                if (pOptionItem)
                {
                    /* Apply check-state on per-enum basis: */
                    const UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork enmOptionType =
                        pOptionItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork>();
                    pOptionItem->setCheckState(m_pModel->optionsNetwork() & enmOptionType ? Qt::Checked : Qt::Unchecked);
                }
            }
            break;
        }
        case DetailsElementType_Serial:
        {
            /* Enumerate all the option items: */
            for (int i = 0; i < m_pListWidgetOptions->count(); ++i)
            {
                QListWidgetItem *pOptionItem = m_pListWidgetOptions->item(i);
                if (pOptionItem)
                {
                    /* Apply check-state on per-enum basis: */
                    const UIExtraDataMetaDefs::DetailsElementOptionTypeSerial enmOptionType =
                        pOptionItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeSerial>();
                    pOptionItem->setCheckState(m_pModel->optionsSerial() & enmOptionType ? Qt::Checked : Qt::Unchecked);
                }
            }
            break;
        }
        case DetailsElementType_USB:
        {
            /* Enumerate all the option items: */
            for (int i = 0; i < m_pListWidgetOptions->count(); ++i)
            {
                QListWidgetItem *pOptionItem = m_pListWidgetOptions->item(i);
                if (pOptionItem)
                {
                    /* Apply check-state on per-enum basis: */
                    const UIExtraDataMetaDefs::DetailsElementOptionTypeUsb enmOptionType =
                        pOptionItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeUsb>();
                    pOptionItem->setCheckState(m_pModel->optionsUsb() & enmOptionType ? Qt::Checked : Qt::Unchecked);
                }
            }
            break;
        }
        case DetailsElementType_SF:
        {
            /* Enumerate all the option items: */
            for (int i = 0; i < m_pListWidgetOptions->count(); ++i)
            {
                QListWidgetItem *pOptionItem = m_pListWidgetOptions->item(i);
                if (pOptionItem)
                {
                    /* Apply check-state on per-enum basis: */
                    const UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders enmOptionType =
                        pOptionItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders>();
                    pOptionItem->setCheckState(m_pModel->optionsSharedFolders() & enmOptionType ? Qt::Checked : Qt::Unchecked);
                }
            }
            break;
        }
        case DetailsElementType_UI:
        {
            /* Enumerate all the option items: */
            for (int i = 0; i < m_pListWidgetOptions->count(); ++i)
            {
                QListWidgetItem *pOptionItem = m_pListWidgetOptions->item(i);
                if (pOptionItem)
                {
                    /* Apply check-state on per-enum basis: */
                    const UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface enmOptionType =
                        pOptionItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface>();
                    pOptionItem->setCheckState(m_pModel->optionsUserInterface() & enmOptionType ? Qt::Checked : Qt::Unchecked);
                }
            }
            break;
        }
        case DetailsElementType_Description:
        {
            /* Enumerate all the option items: */
            for (int i = 0; i < m_pListWidgetOptions->count(); ++i)
            {
                QListWidgetItem *pOptionItem = m_pListWidgetOptions->item(i);
                if (pOptionItem)
                {
                    /* Apply check-state on per-enum basis: */
                    const UIExtraDataMetaDefs::DetailsElementOptionTypeDescription enmOptionType =
                        pOptionItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeDescription>();
                    pOptionItem->setCheckState(m_pModel->optionsDescription() & enmOptionType ? Qt::Checked : Qt::Unchecked);
                }
            }
            break;
        }
        default:
            break;
    }
}

void UIDetailsContextMenu::sltRetranslateUI()
{
    retranslateCategories();
    retranslateOptions();
    adjustListWidgets();
}

void UIDetailsContextMenu::retranslateCategories()
{
    /* Enumerate all the category items: */
    for (int i = 0; i < m_pListWidgetCategories->count(); ++i)
    {
        QListWidgetItem *pCategoryItem = m_pListWidgetCategories->item(i);
        if (pCategoryItem)
        {
            /* We can translate this thing on per-enum basis: */
            const DetailsElementType enmCategoryType = pCategoryItem->data(DataField_Type).value<DetailsElementType>();
            pCategoryItem->setText(gpConverter->toString(enmCategoryType));
        }
    }
}

void UIDetailsContextMenu::retranslateOptions()
{
    /* Acquire currently selected category item: */
    QListWidgetItem *pCategoryItem = m_pListWidgetCategories->currentItem();
    if (!pCategoryItem)
        return;

    /* Populate currently selected category options: */
    const DetailsElementType enmCategoryType = pCategoryItem->data(DataField_Type).value<DetailsElementType>();
    switch (enmCategoryType)
    {
        case DetailsElementType_General:
        {
            /* Enumerate all the option items: */
            for (int i = 0; i < m_pListWidgetOptions->count(); ++i)
            {
                QListWidgetItem *pOptionItem = m_pListWidgetOptions->item(i);
                if (pOptionItem)
                {
                    /* We can translate this thing on per-enum basis: */
                    const UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral enmOptionType =
                        pOptionItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral>();
                    pOptionItem->setText(gpConverter->toString(enmOptionType));
                }
            }
            break;
        }
        case DetailsElementType_System:
        {
            /* Enumerate all the option items: */
            for (int i = 0; i < m_pListWidgetOptions->count(); ++i)
            {
                QListWidgetItem *pOptionItem = m_pListWidgetOptions->item(i);
                if (pOptionItem)
                {
                    /* We can translate this thing on per-enum basis: */
                    const UIExtraDataMetaDefs::DetailsElementOptionTypeSystem enmOptionType =
                        pOptionItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeSystem>();
                    pOptionItem->setText(gpConverter->toString(enmOptionType));
                }
            }
            break;
        }
        case DetailsElementType_Display:
        {
            /* Enumerate all the option items: */
            for (int i = 0; i < m_pListWidgetOptions->count(); ++i)
            {
                QListWidgetItem *pOptionItem = m_pListWidgetOptions->item(i);
                if (pOptionItem)
                {
                    /* We can translate this thing on per-enum basis: */
                    const UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay enmOptionType =
                        pOptionItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay>();
                    pOptionItem->setText(gpConverter->toString(enmOptionType));
                }
            }
            break;
        }
        case DetailsElementType_Storage:
        {
            /* Enumerate all the option items: */
            for (int i = 0; i < m_pListWidgetOptions->count(); ++i)
            {
                QListWidgetItem *pOptionItem = m_pListWidgetOptions->item(i);
                if (pOptionItem)
                {
                    /* We can translate this thing on per-enum basis: */
                    const UIExtraDataMetaDefs::DetailsElementOptionTypeStorage enmOptionType =
                        pOptionItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeStorage>();
                    pOptionItem->setText(gpConverter->toString(enmOptionType));
                }
            }
            break;
        }
        case DetailsElementType_Audio:
        {
            /* Enumerate all the option items: */
            for (int i = 0; i < m_pListWidgetOptions->count(); ++i)
            {
                QListWidgetItem *pOptionItem = m_pListWidgetOptions->item(i);
                if (pOptionItem)
                {
                    /* We can translate this thing on per-enum basis: */
                    const UIExtraDataMetaDefs::DetailsElementOptionTypeAudio enmOptionType =
                        pOptionItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeAudio>();
                    pOptionItem->setText(gpConverter->toString(enmOptionType));
                }
            }
            break;
        }
        case DetailsElementType_Network:
        {
            /* Enumerate all the option items: */
            for (int i = 0; i < m_pListWidgetOptions->count(); ++i)
            {
                QListWidgetItem *pOptionItem = m_pListWidgetOptions->item(i);
                if (pOptionItem)
                {
                    /* We can translate this thing on per-enum basis: */
                    const UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork enmOptionType =
                        pOptionItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork>();
                    pOptionItem->setText(gpConverter->toString(enmOptionType));
                }
            }
            break;
        }
        case DetailsElementType_Serial:
        {
            /* Enumerate all the option items: */
            for (int i = 0; i < m_pListWidgetOptions->count(); ++i)
            {
                QListWidgetItem *pOptionItem = m_pListWidgetOptions->item(i);
                if (pOptionItem)
                {
                    /* We can translate this thing on per-enum basis: */
                    const UIExtraDataMetaDefs::DetailsElementOptionTypeSerial enmOptionType =
                        pOptionItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeSerial>();
                    pOptionItem->setText(gpConverter->toString(enmOptionType));
                }
            }
            break;
        }
        case DetailsElementType_USB:
        {
            /* Enumerate all the option items: */
            for (int i = 0; i < m_pListWidgetOptions->count(); ++i)
            {
                QListWidgetItem *pOptionItem = m_pListWidgetOptions->item(i);
                if (pOptionItem)
                {
                    /* We can translate this thing on per-enum basis: */
                    const UIExtraDataMetaDefs::DetailsElementOptionTypeUsb enmOptionType =
                        pOptionItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeUsb>();
                    pOptionItem->setText(gpConverter->toString(enmOptionType));
                }
            }
            break;
        }
        case DetailsElementType_SF:
        {
            /* Enumerate all the option items: */
            for (int i = 0; i < m_pListWidgetOptions->count(); ++i)
            {
                QListWidgetItem *pOptionItem = m_pListWidgetOptions->item(i);
                if (pOptionItem)
                {
                    /* We can translate this thing on per-enum basis: */
                    const UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders enmOptionType =
                        pOptionItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders>();
                    pOptionItem->setText(gpConverter->toString(enmOptionType));
                }
            }
            break;
        }
        case DetailsElementType_UI:
        {
            /* Enumerate all the option items: */
            for (int i = 0; i < m_pListWidgetOptions->count(); ++i)
            {
                QListWidgetItem *pOptionItem = m_pListWidgetOptions->item(i);
                if (pOptionItem)
                {
                    /* We can translate this thing on per-enum basis: */
                    const UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface enmOptionType =
                        pOptionItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface>();
                    pOptionItem->setText(gpConverter->toString(enmOptionType));
                }
            }
            break;
        }
        case DetailsElementType_Description:
        {
            /* Enumerate all the option items: */
            for (int i = 0; i < m_pListWidgetOptions->count(); ++i)
            {
                QListWidgetItem *pOptionItem = m_pListWidgetOptions->item(i);
                if (pOptionItem)
                {
                    /* We can translate this thing on per-enum basis: */
                    const UIExtraDataMetaDefs::DetailsElementOptionTypeDescription enmOptionType =
                        pOptionItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeDescription>();
                    pOptionItem->setText(gpConverter->toString(enmOptionType));
                }
            }
            break;
        }
        default:
            break;
    }
}

void UIDetailsContextMenu::sltCategoryItemEntered(QListWidgetItem *pItem)
{
    /* Choose hovered item as current one: */
    m_pListWidgetCategories->setCurrentItem(pItem);
}

void UIDetailsContextMenu::sltCategoryItemClicked(QListWidgetItem *pItem)
{
    /* Notify listeners: */
    const DetailsElementType enmCategoryType = pItem->data(DataField_Type).value<DetailsElementType>();

    /* Toggle element visibility status: */
    QMap<DetailsElementType, bool> categories = m_pModel->categories();
    if (categories.contains(enmCategoryType))
        categories.remove(enmCategoryType);
    else
        categories[enmCategoryType] = true;
    m_pModel->setCategories(categories);
}

void UIDetailsContextMenu::sltCategoryItemChanged(QListWidgetItem *, QListWidgetItem *)
{
    /* Update options list: */
    populateOptions();
    updateOptionStates();
    retranslateOptions();
}

void UIDetailsContextMenu::sltOptionItemEntered(QListWidgetItem *pItem)
{
    /* Choose hovered item as current one: */
    m_pListWidgetOptions->setCurrentItem(pItem);
}

void UIDetailsContextMenu::sltOptionItemClicked(QListWidgetItem *pItem)
{
    /* First make sure we really have category item selected: */
    QListWidgetItem *pCategoryItem = m_pListWidgetCategories->currentItem();
    if (!pCategoryItem)
        return;

    /* Then acquire category type: */
    const DetailsElementType enmCategoryType = pCategoryItem->data(DataField_Type).value<DetailsElementType>();

    /* Handle known category types: */
    switch (enmCategoryType)
    {
        case DetailsElementType_General:
        {
            /* Toggle element visibility status: */
            const UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral enmOptionType =
                pItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral>();
            m_pModel->setOptionsGeneral(static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral>(m_pModel->optionsGeneral() ^ enmOptionType));
            break;
        }
        case DetailsElementType_System:
        {
            /* Toggle element visibility status: */
            const UIExtraDataMetaDefs::DetailsElementOptionTypeSystem enmOptionType =
                pItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeSystem>();
            m_pModel->setOptionsSystem(static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeSystem>(m_pModel->optionsSystem() ^ enmOptionType));
            break;
        }
        case DetailsElementType_Display:
        {
            /* Toggle element visibility status: */
            const UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay enmOptionType =
                pItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay>();
            m_pModel->setOptionsDisplay(static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay>(m_pModel->optionsDisplay() ^ enmOptionType));
            break;
        }
        case DetailsElementType_Storage:
        {
            /* Toggle element visibility status: */
            const UIExtraDataMetaDefs::DetailsElementOptionTypeStorage enmOptionType =
                pItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeStorage>();
            m_pModel->setOptionsStorage(static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeStorage>(m_pModel->optionsStorage() ^ enmOptionType));
            break;
        }
        case DetailsElementType_Audio:
        {
            /* Toggle element visibility status: */
            const UIExtraDataMetaDefs::DetailsElementOptionTypeAudio enmOptionType =
                pItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeAudio>();
            m_pModel->setOptionsAudio(static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeAudio>(m_pModel->optionsAudio() ^ enmOptionType));
            break;
        }
        case DetailsElementType_Network:
        {
            /* Toggle element visibility status: */
            const UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork enmOptionType =
                pItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork>();
            m_pModel->setOptionsNetwork(static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork>(m_pModel->optionsNetwork() ^ enmOptionType));
            break;
        }
        case DetailsElementType_Serial:
        {
            /* Toggle element visibility status: */
            const UIExtraDataMetaDefs::DetailsElementOptionTypeSerial enmOptionType =
                pItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeSerial>();
            m_pModel->setOptionsSerial(static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeSerial>(m_pModel->optionsSerial() ^ enmOptionType));
            break;
        }
        case DetailsElementType_USB:
        {
            /* Toggle element visibility status: */
            const UIExtraDataMetaDefs::DetailsElementOptionTypeUsb enmOptionType =
                pItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeUsb>();
            m_pModel->setOptionsUsb(static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeUsb>(m_pModel->optionsUsb() ^ enmOptionType));
            break;
        }
        case DetailsElementType_SF:
        {
            /* Toggle element visibility status: */
            const UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders enmOptionType =
                pItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders>();
            m_pModel->setOptionsSharedFolders(static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders>(m_pModel->optionsSharedFolders() ^ enmOptionType));
            break;
        }
        case DetailsElementType_UI:
        {
            /* Toggle element visibility status: */
            const UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface enmOptionType =
                pItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface>();
            m_pModel->setOptionsUserInterface(static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface>(m_pModel->optionsUserInterface() ^ enmOptionType));
            break;
        }
        case DetailsElementType_Description:
        {
            /* Toggle element visibility status: */
            const UIExtraDataMetaDefs::DetailsElementOptionTypeDescription enmOptionType =
                pItem->data(DataField_Type).value<UIExtraDataMetaDefs::DetailsElementOptionTypeDescription>();
            m_pModel->setOptionsDescription(static_cast<UIExtraDataMetaDefs::DetailsElementOptionTypeDescription>(m_pModel->optionsDescription() ^ enmOptionType));
            break;
        }
        default:
            break;
    }
}

void UIDetailsContextMenu::prepare()
{
    /* Create main layout: */
    QHBoxLayout *pMainLayout = new QHBoxLayout(this);
    if (pMainLayout)
    {
        pMainLayout->setContentsMargins(0, 0, 0, 0);
        pMainLayout->setSpacing(1);

        /* Create list of categories: */
        m_pListWidgetCategories = new QListWidget(this);
        if (m_pListWidgetCategories)
        {
            m_pListWidgetCategories->setMouseTracking(true);
            m_pListWidgetCategories->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
            connect(m_pListWidgetCategories, &QListWidget::itemEntered,
                    this, &UIDetailsContextMenu::sltCategoryItemEntered);
            connect(m_pListWidgetCategories, &QListWidget::itemClicked,
                    this, &UIDetailsContextMenu::sltCategoryItemClicked);
            connect(m_pListWidgetCategories, &QListWidget::currentItemChanged,
                    this, &UIDetailsContextMenu::sltCategoryItemChanged);
            pMainLayout->addWidget(m_pListWidgetCategories);
        }

        /* Create list of options: */
        m_pListWidgetOptions = new QListWidget(this);
        if (m_pListWidgetOptions)
        {
            m_pListWidgetOptions->setMouseTracking(true);
            m_pListWidgetOptions->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
            connect(m_pListWidgetOptions, &QListWidget::itemEntered,
                    this, &UIDetailsContextMenu::sltOptionItemEntered);
            connect(m_pListWidgetOptions, &QListWidget::itemClicked,
                    this, &UIDetailsContextMenu::sltOptionItemClicked);
            pMainLayout->addWidget(m_pListWidgetOptions);
        }
    }

    /* Prepare lists: */
    populateCategories();
    populateOptions();
    /* Apply language settings: */
    sltRetranslateUI();
    connect(&translationEventListener(), &UITranslationEventListener::sigRetranslateUI,
            this, &UIDetailsContextMenu::sltRetranslateUI);
}

void UIDetailsContextMenu::populateCategories()
{
    /* Clear category list initially: */
    m_pListWidgetCategories->clear();

    /* Enumerate all the known categories: */
    for (int i = DetailsElementType_Invalid + 1; i < DetailsElementType_Max; ++i)
    {
        /* Prepare current category type: */
        const DetailsElementType enmCategoryType = static_cast<DetailsElementType>(i);
        /* And create list-widget item of it: */
        QListWidgetItem *pCategoryItem = createCategoryItem(gpConverter->toIcon(enmCategoryType));
        if (pCategoryItem)
        {
            pCategoryItem->setData(DataField_Type, QVariant::fromValue(enmCategoryType));
            pCategoryItem->setCheckState(Qt::Unchecked);
        }
    }
}

void UIDetailsContextMenu::populateOptions()
{
    /* Clear option list initially: */
    m_pListWidgetOptions->clear();

    /* Acquire currently selected category item: */
    QListWidgetItem *pCategoryItem = m_pListWidgetCategories->currentItem();
    if (!pCategoryItem)
        return;

    /* We will use that one for all the options fetching: */
    const QMetaObject &smo = UIExtraDataMetaDefs::staticMetaObject;

    /* Populate currently selected category options: */
    const DetailsElementType enmCategoryType = pCategoryItem->data(DataField_Type).value<DetailsElementType>();
    switch (enmCategoryType)
    {
        case DetailsElementType_General:
        {
            /* Enumerate all the known options: */
            const int iEnumIndex = smo.indexOfEnumerator("DetailsElementOptionTypeGeneral");
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
                /* And create list-widget item of it: */
                QListWidgetItem *pOptionItem = createOptionItem();
                if (pOptionItem)
                {
                    pOptionItem->setData(DataField_Type, QVariant::fromValue(enmOptionType));
                    pOptionItem->setData(DataField_Name, gpConverter->toInternalString(enmOptionType));
                    pOptionItem->setCheckState(Qt::Unchecked);
                }
            }
            break;
        }
        case DetailsElementType_System:
        {
            /* Enumerate all the known options: */
            const int iEnumIndex = smo.indexOfEnumerator("DetailsElementOptionTypeSystem");
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
                /* And create list-widget item of it: */
                QListWidgetItem *pOptionItem = createOptionItem();
                if (pOptionItem)
                {
                    pOptionItem->setData(DataField_Type, QVariant::fromValue(enmOptionType));
                    pOptionItem->setData(DataField_Name, gpConverter->toInternalString(enmOptionType));
                    pOptionItem->setCheckState(Qt::Unchecked);
                }
            }
            break;
        }
        case DetailsElementType_Display:
        {
            /* Enumerate all the known options: */
            const int iEnumIndex = smo.indexOfEnumerator("DetailsElementOptionTypeDisplay");
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
                /* And create list-widget item of it: */
                QListWidgetItem *pOptionItem = createOptionItem();
                if (pOptionItem)
                {
                    pOptionItem->setData(DataField_Type, QVariant::fromValue(enmOptionType));
                    pOptionItem->setData(DataField_Name, gpConverter->toInternalString(enmOptionType));
                    pOptionItem->setCheckState(Qt::Unchecked);
                }
            }
            break;
        }
        case DetailsElementType_Storage:
        {
            /* Enumerate all the known options: */
            const int iEnumIndex = smo.indexOfEnumerator("DetailsElementOptionTypeStorage");
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
                /* And create list-widget item of it: */
                QListWidgetItem *pOptionItem = createOptionItem();
                if (pOptionItem)
                {
                    pOptionItem->setData(DataField_Type, QVariant::fromValue(enmOptionType));
                    pOptionItem->setData(DataField_Name, gpConverter->toInternalString(enmOptionType));
                    pOptionItem->setCheckState(Qt::Unchecked);
                }
            }
            break;
        }
        case DetailsElementType_Audio:
        {
            /* Enumerate all the known options: */
            const int iEnumIndex = smo.indexOfEnumerator("DetailsElementOptionTypeAudio");
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
#ifndef VBOX_WITH_AUDIO_INOUT_INFO
                /* Skip audio input/output stuff if not allowed: */
                if (enmOptionType == UIExtraDataMetaDefs::DetailsElementOptionTypeAudio_IO)
                    continue;
#endif /* VBOX_WITH_AUDIO_INOUT_INFO */
                /* And create list-widget item of it: */
                QListWidgetItem *pOptionItem = createOptionItem();
                if (pOptionItem)
                {
                    pOptionItem->setData(DataField_Type, QVariant::fromValue(enmOptionType));
                    pOptionItem->setData(DataField_Name, gpConverter->toInternalString(enmOptionType));
                    pOptionItem->setCheckState(Qt::Unchecked);
                }
            }
            break;
        }
        case DetailsElementType_Network:
        {
            /* Enumerate all the known options: */
            const int iEnumIndex = smo.indexOfEnumerator("DetailsElementOptionTypeNetwork");
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
                /* And create list-widget item of it: */
                QListWidgetItem *pOptionItem = createOptionItem();
                if (pOptionItem)
                {
                    pOptionItem->setData(DataField_Type, QVariant::fromValue(enmOptionType));
                    pOptionItem->setData(DataField_Name, gpConverter->toInternalString(enmOptionType));
                    pOptionItem->setCheckState(Qt::Unchecked);
                }
            }
            break;
        }
        case DetailsElementType_Serial:
        {
            /* Enumerate all the known options: */
            const int iEnumIndex = smo.indexOfEnumerator("DetailsElementOptionTypeSerial");
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
                /* And create list-widget item of it: */
                QListWidgetItem *pOptionItem = createOptionItem();
                if (pOptionItem)
                {
                    pOptionItem->setData(DataField_Type, QVariant::fromValue(enmOptionType));
                    pOptionItem->setData(DataField_Name, gpConverter->toInternalString(enmOptionType));
                    pOptionItem->setCheckState(Qt::Unchecked);
                }
            }
            break;
        }
        case DetailsElementType_USB:
        {
            /* Enumerate all the known options: */
            const int iEnumIndex = smo.indexOfEnumerator("DetailsElementOptionTypeUsb");
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
                /* And create list-widget item of it: */
                QListWidgetItem *pOptionItem = createOptionItem();
                if (pOptionItem)
                {
                    pOptionItem->setData(DataField_Type, QVariant::fromValue(enmOptionType));
                    pOptionItem->setData(DataField_Name, gpConverter->toInternalString(enmOptionType));
                    pOptionItem->setCheckState(Qt::Unchecked);
                }
            }
            break;
        }
        case DetailsElementType_SF:
        {
            /* Enumerate all the known options: */
            const int iEnumIndex = smo.indexOfEnumerator("DetailsElementOptionTypeSharedFolders");
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
                /* And create list-widget item of it: */
                QListWidgetItem *pOptionItem = createOptionItem();
                if (pOptionItem)
                {
                    pOptionItem->setData(DataField_Type, QVariant::fromValue(enmOptionType));
                    pOptionItem->setData(DataField_Name, gpConverter->toInternalString(enmOptionType));
                    pOptionItem->setCheckState(Qt::Unchecked);
                }
            }
            break;
        }
        case DetailsElementType_UI:
        {
            /* Enumerate all the known options: */
            const int iEnumIndex = smo.indexOfEnumerator("DetailsElementOptionTypeUserInterface");
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
#ifdef VBOX_WS_MAC
                /* Skip menu-bar and mini-toolbar types on macOS: */
                if (   enmOptionType == UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_MenuBar
                    || enmOptionType == UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface_MiniToolbar)
                    continue;
#endif /* VBOX_WS_MAC */
                /* And create list-widget item of it: */
                QListWidgetItem *pOptionItem = createOptionItem();
                if (pOptionItem)
                {
                    pOptionItem->setData(DataField_Type, QVariant::fromValue(enmOptionType));
                    pOptionItem->setData(DataField_Name, gpConverter->toInternalString(enmOptionType));
                    pOptionItem->setCheckState(Qt::Unchecked);
                }
            }
            break;
        }
        case DetailsElementType_Description:
        {
            /* Enumerate all the known options: */
            const int iEnumIndex = smo.indexOfEnumerator("DetailsElementOptionTypeDescription");
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
                /* And create list-widget item of it: */
                QListWidgetItem *pOptionItem = createOptionItem();
                if (pOptionItem)
                {
                    pOptionItem->setData(DataField_Type, QVariant::fromValue(enmOptionType));
                    pOptionItem->setData(DataField_Name, gpConverter->toInternalString(enmOptionType));
                    pOptionItem->setCheckState(Qt::Unchecked);
                }
            }
            break;
        }
        default:
            break;
    }
}

void UIDetailsContextMenu::adjustListWidgets()
{
    /* Include frame width: */
    int iW = 2 * m_pListWidgetCategories->frameWidth();
    int iH = iW;

    /* Include size hints: */
    iW += m_pListWidgetCategories->sizeHintForColumn(0);
    iH += m_pListWidgetCategories->sizeHintForRow(0) * m_pListWidgetCategories->count();

    /* Category list size is constant, options list size is vague: */
    m_pListWidgetCategories->setFixedSize(QSize(iW * 1.3, iH));
    m_pListWidgetOptions->setFixedSize(QSize(iW * 1.3, iH));
}

QListWidgetItem *UIDetailsContextMenu::createCategoryItem(const QIcon &icon)
{
    QListWidgetItem *pItem = new QListWidgetItem(icon, QString(), m_pListWidgetCategories);
    if (pItem)
        m_pListWidgetCategories->addItem(pItem);
    return pItem;
}

QListWidgetItem *UIDetailsContextMenu::createOptionItem()
{
    QListWidgetItem *pItem = new QListWidgetItem(QString(), m_pListWidgetOptions);
    if (pItem)
        m_pListWidgetOptions->addItem(pItem);
    return pItem;
}
