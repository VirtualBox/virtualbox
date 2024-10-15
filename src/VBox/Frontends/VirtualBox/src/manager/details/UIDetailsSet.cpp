/* $Id: UIDetailsSet.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDetailsSet class implementation.
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
#include <QApplication>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionGraphicsItem>

/* GUI includes: */
#include "UIDetailsElements.h"
#include "UIDetailsModel.h"
#include "UIDetailsSet.h"
#include "UIMedium.h"
#include "UIMediumEnumerator.h"
#include "UIVirtualBoxEventHandler.h"
#include "UIVirtualMachineItemCloud.h"
#include "UIVirtualMachineItemLocal.h"

/* COM includes: */
#include "CUSBController.h"
#include "CUSBDeviceFilters.h"


UIDetailsSet::UIDetailsSet(UIDetailsItem *pParent)
    : UIDetailsItem(pParent)
    , m_pMachineItem(0)
    , m_fFullSet(true)
    , m_fIsLocal(true)
    , m_fHasDetails(false)
    , m_configurationAccessLevel(ConfigurationAccessLevel_Null)
    , m_pBuildStep(0)
    , m_iBackgroundDarknessStart(115)
    , m_iBackgroundDarknessFinal(150)
{
    /* Add set to the parent group: */
    parentItem()->addItem(this);

    /* Prepare set: */
    prepareSet();

    /* Prepare connections: */
    prepareConnections();
}

UIDetailsSet::~UIDetailsSet()
{
    /* Cleanup items: */
    clearItems();

    /* Remove set from the parent group: */
    parentItem()->removeItem(this);
}

void UIDetailsSet::clearSet()
{
    /* Clear passed arguments: */
    m_pMachineItem = 0;
    m_comMachine = CMachine();
    m_comCloudMachine = CCloudMachine();
}

void UIDetailsSet::buildSet(UIVirtualMachineItem *pMachineItem, bool fFullSet, const QMap<DetailsElementType, bool> &settings)
{
    /* Remember passed arguments: */
    m_pMachineItem = pMachineItem;
    m_fIsLocal = m_pMachineItem->itemType() == UIVirtualMachineItemType_Local;
    m_fHasDetails = m_pMachineItem->hasDetails();
    m_fFullSet = fFullSet;
    m_settings = settings;

    /* Prepare a list of types to build: */
    QList<DetailsElementType> types;

    /* Make sure we have details: */
    if (m_fHasDetails)
    {
        /* Special handling wrt item type: */
        switch (m_pMachineItem->itemType())
        {
            case UIVirtualMachineItemType_Local:
            {
                /* Get local machine: */
                m_comMachine = m_pMachineItem->toLocal()->machine();

                /* Compose a list of types to build: */
                if (m_fFullSet)
                    types << DetailsElementType_General << DetailsElementType_System << DetailsElementType_Preview
                          << DetailsElementType_Display << DetailsElementType_Storage << DetailsElementType_Audio
                          << DetailsElementType_Network << DetailsElementType_Serial << DetailsElementType_USB
                          << DetailsElementType_SF << DetailsElementType_UI << DetailsElementType_Description;
                else
                    types << DetailsElementType_General << DetailsElementType_System << DetailsElementType_Preview;

                /* Take into account USB controller restrictions: */
                const CUSBDeviceFilters &filters = m_comMachine.GetUSBDeviceFilters();
                if (filters.isNull() || !m_comMachine.GetUSBProxyAvailable())
                    m_settings.remove(DetailsElementType_USB);

                break;
            }
            case UIVirtualMachineItemType_CloudReal:
            {
                /* Get cloud machine: */
                m_comCloudMachine = m_pMachineItem->toCloud()->machine();

                /* Compose a list of types to build: */
                types << DetailsElementType_General;

                break;
            }
            default:
                break;
        }
    }

    /* Cleanup if new types differs from old: */
    if (m_types != types)
    {
        clearItems();
        m_elements.clear();
        updateGeometry();
    }

    /* Remember new types: */
    m_types = types;

    /* Build or emit fake signal: */
    if (m_fHasDetails)
        rebuildSet();
    else
        emit sigBuildDone();
}

void UIDetailsSet::updateLayout()
{
    /* Prepare variables: */
    const int iMargin = data(SetData_Margin).toInt();
    const int iSpacing = data(SetData_Spacing).toInt();
    const int iMaximumWidth = geometry().width();
    UIDetailsElement *pPreviewElement = element(DetailsElementType_Preview);
    const bool fPreviewVisible = pPreviewElement && pPreviewElement->isVisible();
    const int iPreviewWidth = fPreviewVisible ? pPreviewElement->minimumWidthHint() : 0;
    const int iPreviewHeight = fPreviewVisible ? pPreviewElement->minimumHeightHint() : 0;
    int iVerticalIndent = iMargin;
    int iPreviewGroupHeight = 0;
    bool fPreviewGroupUnfinished = fPreviewVisible;
    QList<UIDetailsElement*> listPreviewGroup;
    m_listPreviewGroup.clear();
    m_listOutsideGroup.clear();

    /* Layout all the items but Preview: */
    foreach (UIDetailsItem *pItem, items())
    {
        /* Make sure item exists: */
        AssertPtrReturnVoid(pItem);
        /* Skip item if hidden: */
        if (!pItem->isVisible())
            continue;

        /* Acquire element type: */
        UIDetailsElement *pElement = pItem->toElement();
        AssertPtrReturnVoid(pElement);
        const DetailsElementType enmElementType = pElement->elementType();
        /* Skip Preview element: */
        if (enmElementType == DetailsElementType_Preview)
            continue;

        /* Calculate element size: */
        QSizeF elementSize;

        /* If we haven't finished filling Preview group: */
        if (fPreviewGroupUnfinished)
        {
            /* For Preview group we have limited element width: */
            elementSize.setWidth(iMaximumWidth - (iSpacing + iPreviewWidth));
            /* Resize element to width to get corresponding height: */
            pElement->resize(elementSize.width(), pElement->geometry().height());
            /* Now we can get element height based on width above: */
            elementSize.setHeight(pElement->minimumHeightHint());
            /* Resize element to height based on width above: */
            pElement->resize(pElement->geometry().width(), elementSize.height());

            /* Calculate remaining vertical space: */
            const int iRemainingSpace = (iPreviewHeight + iSpacing) - iPreviewGroupHeight;

            /* If last element height is at least two times taller than the remaining space: */
            if (elementSize.height() / 2 > iRemainingSpace)
            {
                /* We should stop filling Preview group now: */
                fPreviewGroupUnfinished = false;

                /* Advance indent only if there is remaining space at all: */
                if (iRemainingSpace > 0)
                    iVerticalIndent += iRemainingSpace;
            }
            /* Otherwise last element can still be inserted to Preview group: */
            else
            {
                /* Advance Preview group height: */
                iPreviewGroupHeight += (elementSize.height() + iSpacing);
                /* Append last Preview group element: */
                listPreviewGroup << pElement;
                m_listPreviewGroup << enmElementType;
            }
        }

        /* If we have finished filling Preview group: */
        if (!fPreviewGroupUnfinished)
        {
            /* Calculate element width: */
            elementSize.setWidth(iMaximumWidth);
            /* Resize element to width to get corresponding height: */
            pElement->resize(elementSize.width(), pElement->geometry().height());
            /* Now we can get element height based on width above: */
            elementSize.setHeight(pElement->minimumHeightHint());
            /* Resize element to height based on width above: */
            pElement->resize(pElement->geometry().width(), elementSize.height());
            /* Append last Outside group element: */
            m_listOutsideGroup << enmElementType;
        }

        /* Move element: */
        pElement->setPos(0, iVerticalIndent);
        /* Layout element content: */
        pElement->updateLayout();

        /* Advance indent: */
        iVerticalIndent += (elementSize.height() + iSpacing);
    }

    /* Make sure last opened Preview group item, if exists, consumes rest of vertical space: */
    if (!listPreviewGroup.isEmpty())
    {
        /* Calculate remaining vertical space: */
        const int iRemainingSpace = (iPreviewHeight + iSpacing) - iPreviewGroupHeight;
        if (iRemainingSpace > 0)
        {
            /* Look for last opened element: */
            int iLastOpenedElement = -1;
            foreach (UIDetailsElement *pElement, listPreviewGroup)
                if (pElement->isOpened())
                    iLastOpenedElement = listPreviewGroup.indexOf(pElement);

            /* If at least one is opened: */
            if (iLastOpenedElement != -1)
            {
                /* Resize element to width to get corresponding height: */
                UIDetailsElement *pFoundOne = listPreviewGroup.at(iLastOpenedElement);
                pFoundOne->resize(pFoundOne->geometry().width(), pFoundOne->geometry().height() + iRemainingSpace);

                /* Adjust subsequent element positions: */
                for (int i = iLastOpenedElement + 1; i < listPreviewGroup.size(); ++i)
                {
                    UIDetailsElement *pIteratedOne = listPreviewGroup.at(i);
                    pIteratedOne->setPos(pIteratedOne->geometry().x(), pIteratedOne->geometry().y() + iRemainingSpace);
                }

                /* Layout element content: */
                pFoundOne->updateLayout();
            }
        }
    }

    /* If Preview exists: */
    if (fPreviewVisible)
    {
        /* Align it to the right corner if there is at least one element in the Preview group.
         * Otherwise we can put it to the left corner to be able to take whole the space. */
        if (!listPreviewGroup.isEmpty())
            pPreviewElement->setPos(iMaximumWidth - iPreviewWidth, iMargin);
        else
            pPreviewElement->setPos(0, iMargin);

        /* Resize it to it's size if there is at least one element in the Preview group.
         * Otherwise we can take whole the horizontal space we have. */
        int iWidth = iPreviewWidth;
        int iHeight = iPreviewHeight;
        if (listPreviewGroup.isEmpty())
            iWidth = iMaximumWidth;
        if (!pPreviewElement->isAnimationRunning() && !pPreviewElement->isClosed())
            iHeight += iPreviewGroupHeight - (iPreviewHeight + iSpacing);
        pPreviewElement->resize(iWidth, iHeight);

        /* Layout element content: */
        pPreviewElement->updateLayout();
    }

    /* Set layout update procedure cause hints to be invalidated,
     * so we have to update geometry to recalculate them: */
    updateGeometry();
}

void UIDetailsSet::sltBuildStep(const QUuid &uStepId, int iStepNumber)
{
    /* Cleanup build-step: */
    delete m_pBuildStep;
    m_pBuildStep = 0;

    /* Is step id valid? */
    if (uStepId != m_uSetId)
        return;

    /* Step number feats the bounds: */
    if (iStepNumber >= 0 && iStepNumber < m_types.size())
    {
        /* Load details settings: */
        const DetailsElementType enmElementType = m_types.at(iStepNumber);
        /* Should the element be visible? */
        bool fVisible = m_settings.contains(enmElementType);
        /* Should the element be opened? */
        bool fOpen = fVisible && m_settings[enmElementType];

        /* Check if element is present already: */
        UIDetailsElement *pElement = element(enmElementType);
        if (pElement && fOpen)
            pElement->open(false);
        /* Create element if necessary: */
        bool fJustCreated = false;
        if (!pElement)
        {
            fJustCreated = true;
            pElement = createElement(enmElementType, fOpen);
        }

        /* Show element if necessary: */
        if (fVisible && !pElement->isVisible())
        {
            /* Show the element: */
            pElement->show();
            /* Recursively update size-hint: */
            pElement->updateGeometry();
            /* Update layout: */
            model()->updateLayout();
        }
        /* Hide element if necessary: */
        else if (!fVisible && pElement->isVisible())
        {
            /* Hide the element: */
            pElement->hide();
            /* Recursively update size-hint: */
            updateGeometry();
            /* Update layout: */
            model()->updateLayout();
        }
        /* Update model if necessary: */
        else if (fJustCreated)
            model()->updateLayout();

        /* For visible element: */
        if (pElement->isVisible())
        {
            /* Create next build-step: */
            m_pBuildStep = new UIPrepareStep(this, pElement, uStepId, iStepNumber + 1);

            /* Build element: */
            pElement->updateAppearance();
        }
        /* For invisible element: */
        else
        {
            /* Just build next step: */
            sltBuildStep(uStepId, iStepNumber + 1);
        }
    }
    /* Step number out of bounds: */
    else
    {
        /* Update model: */
        model()->updateLayout();
        /* Repaint all the items: */
        foreach (UIDetailsItem *pItem, items())
            pItem->update();
        /* Notify listener about build done: */
        emit sigBuildDone();
    }
}

void UIDetailsSet::paint(QPainter *pPainter, const QStyleOptionGraphicsItem *pOptions, QWidget *)
{
    /* Paint background: */
    paintBackground(pPainter, pOptions);
}

QString UIDetailsSet::description() const
{
    return tr("Contains the details of virtual machine '%1'").arg(m_pMachineItem->name());
}

void UIDetailsSet::addItem(UIDetailsItem *pItem)
{
    switch (pItem->type())
    {
        case UIDetailsItemType_Element:
        {
            UIDetailsElement *pElement = pItem->toElement();
            DetailsElementType type = pElement->elementType();
            AssertMsg(!m_elements.contains(type), ("Element already added!"));
            m_elements.insert(type, pItem);
            break;
        }
        default:
        {
            AssertMsgFailed(("Invalid item type!"));
            break;
        }
    }
}

void UIDetailsSet::removeItem(UIDetailsItem *pItem)
{
    switch (pItem->type())
    {
        case UIDetailsItemType_Element:
        {
            UIDetailsElement *pElement = pItem->toElement();
            DetailsElementType type = pElement->elementType();
            AssertMsg(m_elements.contains(type), ("Element do not present (type = %d)!", (int)type));
            m_elements.remove(type);
            break;
        }
        default:
        {
            AssertMsgFailed(("Invalid item type!"));
            break;
        }
    }
}

QList<UIDetailsItem*> UIDetailsSet::items(UIDetailsItemType enmType /* = UIDetailsItemType_Element */) const
{
    switch (enmType)
    {
        case UIDetailsItemType_Element: return m_elements.values();
        case UIDetailsItemType_Any: return items(UIDetailsItemType_Element);
        default: AssertMsgFailed(("Invalid item type!")); break;
    }
    return QList<UIDetailsItem*>();
}

bool UIDetailsSet::hasItems(UIDetailsItemType enmType /* = UIDetailsItemType_Element */) const
{
    switch (enmType)
    {
        case UIDetailsItemType_Element: return !m_elements.isEmpty();
        case UIDetailsItemType_Any: return hasItems(UIDetailsItemType_Element);
        default: AssertMsgFailed(("Invalid item type!")); break;
    }
    return false;
}

void UIDetailsSet::clearItems(UIDetailsItemType enmType /* = UIDetailsItemType_Element */)
{
    switch (enmType)
    {
        case UIDetailsItemType_Element:
        {
            foreach (int iKey, m_elements.keys())
                delete m_elements[iKey];
            AssertMsg(m_elements.isEmpty(), ("Set items cleanup failed!"));
            break;
        }
        case UIDetailsItemType_Any:
        {
            clearItems(UIDetailsItemType_Element);
            break;
        }
        default:
        {
            AssertMsgFailed(("Invalid item type!"));
            break;
        }
    }
}

UIDetailsElement *UIDetailsSet::element(DetailsElementType enmElementType) const
{
    UIDetailsItem *pItem = m_elements.value(enmElementType, 0);
    if (pItem)
        return pItem->toElement();
    return 0;
}

int UIDetailsSet::minimumWidthHint() const
{
    /* Zero if has no details: */
    if (!hasDetails())
        return 0;

    /* Prepare variables: */
    const int iSpacing = data(SetData_Spacing).toInt();
    int iMinimumWidthHintPreview = 0;
    int iMinimumWidthHintInGroup = 0;
    int iMinimumWidthHintOutGroup = 0;

    /* Take into account all the elements: */
    foreach (UIDetailsItem *pItem, items())
    {
        /* Make sure item exists: */
        AssertPtrReturn(pItem, 0);
        /* Skip item if hidden: */
        if (!pItem->isVisible())
            continue;

        /* Acquire element type: */
        UIDetailsElement *pElement = pItem->toElement();
        AssertPtrReturn(pElement, 0);
        DetailsElementType enmElementType = pElement->elementType();

        /* Calculate corresponding hints: */
        if (enmElementType == DetailsElementType_Preview)
            iMinimumWidthHintPreview = pItem->minimumWidthHint();
        else
        {
            if (m_listPreviewGroup.contains(enmElementType))
                iMinimumWidthHintInGroup = qMax(iMinimumWidthHintInGroup, pItem->minimumWidthHint());
            else if (m_listOutsideGroup.contains(enmElementType))
                iMinimumWidthHintOutGroup = qMax(iMinimumWidthHintOutGroup, pItem->minimumWidthHint());
        }
    }

    /* Append minimum width of Preview and Preview group: */
    int iMinimumWidthHint = 0;
    if (iMinimumWidthHintPreview)
        iMinimumWidthHint += iMinimumWidthHintPreview;
    if (iMinimumWidthHintInGroup)
        iMinimumWidthHint += iMinimumWidthHintInGroup;
    if (iMinimumWidthHintPreview && iMinimumWidthHintInGroup)
        iMinimumWidthHint += iSpacing;
    /* Compare with minimum width of Outside group: */
    iMinimumWidthHint = qMax(iMinimumWidthHint, iMinimumWidthHintOutGroup);
    /* Return result: */
    return iMinimumWidthHint;
}

int UIDetailsSet::minimumHeightHint() const
{
    /* Zero if has no details: */
    if (!hasDetails())
        return 0;

    /* Prepare variables: */
    const int iMargin = data(SetData_Margin).toInt();
    const int iSpacing = data(SetData_Spacing).toInt();
    int iMinimumHeightHintPreview = 0;
    int iMinimumHeightHintInGroup = 0;
    int iMinimumHeightHintOutGroup = 0;

    /* Take into account all the elements: */
    foreach (UIDetailsItem *pItem, items())
    {
        /* Make sure item exists: */
        AssertPtrReturn(pItem, 0);
        /* Skip item if hidden: */
        if (!pItem->isVisible())
            continue;

        /* Acquire element type: */
        UIDetailsElement *pElement = pItem->toElement();
        AssertPtrReturn(pElement, 0);
        DetailsElementType enmElementType = pElement->elementType();

        /* Calculate corresponding hints: */
        if (enmElementType == DetailsElementType_Preview)
            iMinimumHeightHintPreview += pItem->minimumHeightHint();
        else
        {
            if (m_listPreviewGroup.contains(enmElementType))
                iMinimumHeightHintInGroup += (pItem->minimumHeightHint() + iSpacing);
            else if (m_listOutsideGroup.contains(enmElementType))
                iMinimumHeightHintOutGroup += (pItem->minimumHeightHint() + iSpacing);
        }
    }
    /* Minus last spacing: */
    if (iMinimumHeightHintInGroup > 0)
        iMinimumHeightHintInGroup -= iSpacing;
    if (iMinimumHeightHintOutGroup > 0)
        iMinimumHeightHintOutGroup -= iSpacing;

    /* Append minimum height of Preview and Preview group: */
    int iMinimumHeightHint = qMax(iMinimumHeightHintPreview, iMinimumHeightHintInGroup);
    /* Add spacing if necessary: */
    if (!m_listPreviewGroup.isEmpty() && !m_listOutsideGroup.isEmpty())
        iMinimumHeightHint += iSpacing;
    /* Add Outside group height if necessary: */
    if (!m_listOutsideGroup.isEmpty())
        iMinimumHeightHint += iMinimumHeightHintOutGroup;
    /* And two margins finally: */
    iMinimumHeightHint += 2 * iMargin;
    /* Return result: */
    return iMinimumHeightHint;
}

void UIDetailsSet::sltMachineStateChange(const QUuid &uId)
{
    /* For local VMs only: */
    if (!m_fIsLocal)
        return;

    /* Make sure VM is set: */
    if (m_comMachine.isNull())
        return;

    /* Is this our VM changed? */
    if (m_comMachine.GetId() != uId)
        return;

    /* Update appearance: */
    rebuildSet();
}

void UIDetailsSet::sltMachineAttributesChange(const QUuid &uId)
{
    /* For local VMs only: */
    if (!m_fIsLocal)
        return;

    /* Make sure VM is set: */
    if (m_comMachine.isNull())
        return;

    /* Is this our VM changed? */
    if (m_comMachine.GetId() != uId)
        return;

    /* Update appearance: */
    rebuildSet();
}

void UIDetailsSet::sltMediumEnumerated(const QUuid &uId)
{
    /* For local VMs only: */
    if (!m_fIsLocal)
        return;

    /* Make sure VM is set: */
    if (m_comMachine.isNull())
        return;

    /* Is this our medium changed? */
    const UIMedium guiMedium = gpMediumEnumerator->medium(uId);
    if (   guiMedium.isNull()
        || !guiMedium.machineIds().contains(m_comMachine.GetId()))
        return;

    /* Update appearance: */
    rebuildSet();
}

void UIDetailsSet::prepareSet()
{
    /* Setup size-policy: */
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
}

void UIDetailsSet::prepareConnections()
{
    /* Global-events connections: */
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMachineStateChange, this, &UIDetailsSet::sltMachineStateChange);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigMachineDataChange, this, &UIDetailsSet::sltMachineAttributesChange);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigSessionStateChange, this, &UIDetailsSet::sltMachineAttributesChange);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigSnapshotTake, this, &UIDetailsSet::sltMachineAttributesChange);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigSnapshotDelete, this, &UIDetailsSet::sltMachineAttributesChange);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigSnapshotChange, this, &UIDetailsSet::sltMachineAttributesChange);
    connect(gVBoxEvents, &UIVirtualBoxEventHandler::sigSnapshotRestore, this, &UIDetailsSet::sltMachineAttributesChange);

    /* Meidum-enumeration connections: */
    connect(gpMediumEnumerator, &UIMediumEnumerator::sigMediumEnumerated, this, &UIDetailsSet::sltMediumEnumerated);
}

QVariant UIDetailsSet::data(int iKey) const
{
    /* Provide other members with required data: */
    switch (iKey)
    {
        /* Layout hints: */
        case SetData_Margin: return 1;
        case SetData_Spacing: return 1;
        /* Default: */
        default: break;
    }
    return QVariant();
}

void UIDetailsSet::rebuildSet()
{
    /* Make sure we have details: */
    if (!m_fHasDetails)
        return;

    /* Recache properties: */
    m_configurationAccessLevel = m_pMachineItem->configurationAccessLevel();

    /* Cleanup build-step: */
    delete m_pBuildStep;
    m_pBuildStep = 0;

    /* Generate new set-id: */
    m_uSetId = QUuid::createUuid();

    /* Request to build first step: */
    emit sigBuildStep(m_uSetId, 0);
}

UIDetailsElement *UIDetailsSet::createElement(DetailsElementType enmElementType, bool fOpen)
{
    /* Element factory: */
    switch (enmElementType)
    {
        case DetailsElementType_General:     return new UIDetailsElementGeneral(this, fOpen);
        case DetailsElementType_System:      return new UIDetailsElementSystem(this, fOpen);
        case DetailsElementType_Preview:     return new UIDetailsElementPreview(this, fOpen);
        case DetailsElementType_Display:     return new UIDetailsElementDisplay(this, fOpen);
        case DetailsElementType_Storage:     return new UIDetailsElementStorage(this, fOpen);
        case DetailsElementType_Audio:       return new UIDetailsElementAudio(this, fOpen);
        case DetailsElementType_Network:     return new UIDetailsElementNetwork(this, fOpen);
        case DetailsElementType_Serial:      return new UIDetailsElementSerial(this, fOpen);
        case DetailsElementType_USB:         return new UIDetailsElementUSB(this, fOpen);
        case DetailsElementType_SF:          return new UIDetailsElementSF(this, fOpen);
        case DetailsElementType_UI:          return new UIDetailsElementUI(this, fOpen);
        case DetailsElementType_Description: return new UIDetailsElementDescription(this, fOpen);
        default:                             AssertFailed(); break; /* Shut up, MSC! */
    }
    return 0;
}

void UIDetailsSet::paintBackground(QPainter *pPainter, const QStyleOptionGraphicsItem *pOptions) const
{
    /* Save painter: */
    pPainter->save();

    /* Prepare variables: */
    const QRect optionRect = pOptions->rect;

    /* Acquire background color: */
    const QColor backgroundColor = QApplication::palette().color(QPalette::Active, QPalette::Window);

    /* Paint default background: */
    QColor bcTone1 = backgroundColor.darker(m_iBackgroundDarknessStart);
    QColor bcTone2 = backgroundColor.darker(m_iBackgroundDarknessFinal);
    QLinearGradient gradientDefault(optionRect.topLeft(), optionRect.bottomRight());
    gradientDefault.setColorAt(0, bcTone1);
    gradientDefault.setColorAt(1, bcTone2);
    pPainter->fillRect(optionRect, gradientDefault);

    /* Restore painter: */
    pPainter->restore();
}
