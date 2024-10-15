/* $Id: UIMachineSettingsUSB.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIMachineSettingsUSB class implementation.
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
#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QHelpEvent>
#include <QLineEdit>
#include <QMenu>
#include <QRadioButton>
#include <QSpacerItem>
#include <QVBoxLayout>

/* GUI includes: */
#include "QITreeWidget.h"
#include "QIWidgetValidator.h"
#include "UIConverter.h"
#include "UIIconPool.h"
#include "UIMachineSettingsUSB.h"
#include "UIErrorString.h"
#include "QIToolBar.h"
#include "UIUSBFiltersEditor.h"
#include "UIUSBSettingsEditor.h"

/* COM includes: */
#include "CExtPack.h"
#include "CExtPackManager.h"
#include "CUSBController.h"
#include "CUSBDeviceFilter.h"
#include "CUSBDeviceFilters.h"


/** Machine settings: USB filter data structure. */
struct UIDataSettingsMachineUSBFilter
{
    /** Constructs data. */
    UIDataSettingsMachineUSBFilter() {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataSettingsMachineUSBFilter &other) const
    {
        return true
               && (m_guiData == other.m_guiData)
               ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataSettingsMachineUSBFilter &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataSettingsMachineUSBFilter &other) const { return !equal(other); }

    /** Holds the USB filter data. */
    UIDataUSBFilter  m_guiData;
};


/** Machine settings: USB page data structure. */
struct UIDataSettingsMachineUSB
{
    /** Constructs data. */
    UIDataSettingsMachineUSB()
        : m_fUSBEnabled(false)
        , m_enmUSBControllerType(KUSBControllerType_Null)
    {}

    /** Returns whether the @a other passed data is equal to this one. */
    bool equal(const UIDataSettingsMachineUSB &other) const
    {
        return true
               && (m_fUSBEnabled == other.m_fUSBEnabled)
               && (m_enmUSBControllerType == other.m_enmUSBControllerType)
               ;
    }

    /** Returns whether the @a other passed data is equal to this one. */
    bool operator==(const UIDataSettingsMachineUSB &other) const { return equal(other); }
    /** Returns whether the @a other passed data is different from this one. */
    bool operator!=(const UIDataSettingsMachineUSB &other) const { return !equal(other); }

    /** Holds whether the USB is enabled. */
    bool                m_fUSBEnabled;
    /** Holds the USB controller type. */
    KUSBControllerType  m_enmUSBControllerType;
};


UIMachineSettingsUSB::UIMachineSettingsUSB()
    : m_pCache(0)
    , m_pEditorUsbSettings(0)
{
    prepare();
}

UIMachineSettingsUSB::~UIMachineSettingsUSB()
{
    cleanup();
}

bool UIMachineSettingsUSB::isUSBEnabled() const
{
    return m_pEditorUsbSettings->isFeatureEnabled();
}

bool UIMachineSettingsUSB::changed() const
{
    return m_pCache ? m_pCache->wasChanged() : false;
}

void UIMachineSettingsUSB::loadToCacheFrom(QVariant &data)
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Fetch data to machine: */
    UISettingsPageMachine::fetchData(data);

    /* Clear cache initially: */
    m_pCache->clear();

    /* Prepare old USB data: */
    UIDataSettingsMachineUSB oldUsbData;

    /* Gather old USB data: */
    oldUsbData.m_fUSBEnabled = !m_machine.GetUSBControllers().isEmpty();
    oldUsbData.m_enmUSBControllerType = m_machine.GetUSBControllerCountByType(KUSBControllerType_XHCI) > 0 ? KUSBControllerType_XHCI
                                      : m_machine.GetUSBControllerCountByType(KUSBControllerType_EHCI) > 0 ? KUSBControllerType_EHCI
                                      : m_machine.GetUSBControllerCountByType(KUSBControllerType_OHCI) > 0 ? KUSBControllerType_OHCI
                                      : KUSBControllerType_Null;

    /* Check whether controller is valid: */
    const CUSBDeviceFilters &comFiltersObject = m_machine.GetUSBDeviceFilters();
    if (!comFiltersObject.isNull())
    {
        /* For each filter: */
        const CUSBDeviceFilterVector &filters = comFiltersObject.GetDeviceFilters();
        for (int iFilterIndex = 0; iFilterIndex < filters.size(); ++iFilterIndex)
        {
            /* Prepare old data: */
            UIDataSettingsMachineUSBFilter oldFilterData;

            /* Check whether filter is valid: */
            const CUSBDeviceFilter &filter = filters.at(iFilterIndex);
            if (!filter.isNull())
            {
                /* Gather old data: */
                oldFilterData.m_guiData.m_fActive = filter.GetActive();
                oldFilterData.m_guiData.m_strName = filter.GetName();
                oldFilterData.m_guiData.m_strVendorId = filter.GetVendorId();
                oldFilterData.m_guiData.m_strProductId = filter.GetProductId();
                oldFilterData.m_guiData.m_strRevision = filter.GetRevision();
                oldFilterData.m_guiData.m_strManufacturer = filter.GetManufacturer();
                oldFilterData.m_guiData.m_strProduct = filter.GetProduct();
                oldFilterData.m_guiData.m_strSerialNumber = filter.GetSerialNumber();
                oldFilterData.m_guiData.m_strPort = filter.GetPort();
                const QString strRemote = filter.GetRemote();
                UIRemoteMode enmRemoteMode = UIRemoteMode_Any;
                if (strRemote == "true" || strRemote == "yes" || strRemote == "1")
                    enmRemoteMode = UIRemoteMode_On;
                else if (strRemote == "false" || strRemote == "no" || strRemote == "0")
                    enmRemoteMode = UIRemoteMode_Off;
                oldFilterData.m_guiData.m_enmRemoteMode = enmRemoteMode;
            }

            /* Cache old data: */
            m_pCache->child(iFilterIndex).cacheInitialData(oldFilterData);
        }
    }

    /* Cache old USB data: */
    m_pCache->cacheInitialData(oldUsbData);

    /* Upload machine to data: */
    UISettingsPageMachine::uploadData(data);
}

void UIMachineSettingsUSB::getFromCache()
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Get old USB data from cache: */
    const UIDataSettingsMachineUSB &oldUsbData = m_pCache->base();

    /* Load old USB data from cache: */
    if (m_pEditorUsbSettings)
    {
        m_pEditorUsbSettings->setFeatureEnabled(oldUsbData.m_fUSBEnabled);
        m_pEditorUsbSettings->setUsbControllerType(oldUsbData.m_enmUSBControllerType);

        /* For each filter => load it from cache: */
        QList<UIDataUSBFilter> filters;
        for (int iFilterIndex = 0; iFilterIndex < m_pCache->childCount(); ++iFilterIndex)
        {
            const UIDataSettingsMachineUSBFilter &oldUsbFilterData = m_pCache->child(iFilterIndex).base();
            UIDataUSBFilter filter;
            filter.m_fActive = oldUsbFilterData.m_guiData.m_fActive;
            filter.m_strName = oldUsbFilterData.m_guiData.m_strName;
            filter.m_strVendorId = oldUsbFilterData.m_guiData.m_strVendorId;
            filter.m_strProductId = oldUsbFilterData.m_guiData.m_strProductId;
            filter.m_strRevision = oldUsbFilterData.m_guiData.m_strRevision;
            filter.m_strManufacturer = oldUsbFilterData.m_guiData.m_strManufacturer;
            filter.m_strProduct = oldUsbFilterData.m_guiData.m_strProduct;
            filter.m_strSerialNumber = oldUsbFilterData.m_guiData.m_strSerialNumber;
            filter.m_strPort = oldUsbFilterData.m_guiData.m_strPort;
            filter.m_enmRemoteMode = oldUsbFilterData.m_guiData.m_enmRemoteMode;
            filters << filter;
        }
        m_pEditorUsbSettings->setUsbFilters(filters);
    }

    /* Polish page finally: */
    polishPage();

    /* Revalidate: */
    revalidate();
}

void UIMachineSettingsUSB::putToCache()
{
    /* Sanity check: */
    if (!m_pCache)
        return;

    /* Prepare new USB data: */
    UIDataSettingsMachineUSB newUsbData;

    /* Gather new USB data: */
    if (m_pEditorUsbSettings)
    {
        newUsbData.m_fUSBEnabled = m_pEditorUsbSettings->isFeatureEnabled();
        newUsbData.m_enmUSBControllerType = newUsbData.m_fUSBEnabled
                                          ? m_pEditorUsbSettings->usbControllerType()
                                          : KUSBControllerType_Null;
        /* For each filter => save it to cache: */
        const QList<UIDataUSBFilter> filters = m_pEditorUsbSettings->usbFilters();
        for (int iFilterIndex = 0; iFilterIndex < filters.size(); ++iFilterIndex)
        {
            /* Gather and cache new data: */
            UIDataUSBFilter filter = filters.at(iFilterIndex);
            UIDataSettingsMachineUSBFilter newUsbFilterData;
            newUsbFilterData.m_guiData.m_fActive = filter.m_fActive;
            newUsbFilterData.m_guiData.m_strName = filter.m_strName;
            newUsbFilterData.m_guiData.m_strVendorId = filter.m_strVendorId;
            newUsbFilterData.m_guiData.m_strProductId = filter.m_strProductId;
            newUsbFilterData.m_guiData.m_strRevision = filter.m_strRevision;
            newUsbFilterData.m_guiData.m_strManufacturer = filter.m_strManufacturer;
            newUsbFilterData.m_guiData.m_strProduct = filter.m_strProduct;
            newUsbFilterData.m_guiData.m_strSerialNumber = filter.m_strSerialNumber;
            newUsbFilterData.m_guiData.m_strPort = filter.m_strPort;
            newUsbFilterData.m_guiData.m_enmRemoteMode = filter.m_enmRemoteMode;
            m_pCache->child(iFilterIndex).cacheCurrentData(newUsbFilterData);
        }
    }

    /* Cache new USB data: */
    m_pCache->cacheCurrentData(newUsbData);
}

void UIMachineSettingsUSB::saveFromCacheTo(QVariant &data)
{
    /* Fetch data to machine: */
    UISettingsPageMachine::fetchData(data);

    /* Update data and failing state: */
    setFailed(!saveData());

    /* Upload machine to data: */
    UISettingsPageMachine::uploadData(data);
}

bool UIMachineSettingsUSB::validate(QList<UIValidationMessage> &messages)
{
    Q_UNUSED(messages);

    /* Pass by default: */
    bool fPass = true;

    /* Return result: */
    return fPass;
}

void UIMachineSettingsUSB::setOrderAfter(QWidget *pWidget)
{
    setTabOrder(pWidget, m_pEditorUsbSettings);
}

void UIMachineSettingsUSB::sltRetranslateUI()
{
}

void UIMachineSettingsUSB::polishPage()
{
    /* Polish USB page availability: */
    m_pEditorUsbSettings->setFeatureAvailable(isMachineOffline());
    m_pEditorUsbSettings->setUsbControllerOptionAvailable(isMachineOffline());
    m_pEditorUsbSettings->setUsbFiltersOptionAvailable(isMachineInValidMode());
}

void UIMachineSettingsUSB::prepare()
{
    /* Prepare cache: */
    m_pCache = new UISettingsCacheMachineUSB;
    AssertPtrReturnVoid(m_pCache);

    /* Prepare everything: */
    prepareWidgets();
    prepareConnections();

    /* Apply language settings: */
    sltRetranslateUI();
}

void UIMachineSettingsUSB::prepareWidgets()
{
    /* Prepare main layout: */
    QVBoxLayout *pLayout = new QVBoxLayout(this);
    if (pLayout)
    {
        /* Prepare settings editor: */
        m_pEditorUsbSettings = new UIUSBSettingsEditor(this);
        if (m_pEditorUsbSettings)
        {
            addEditor(m_pEditorUsbSettings);
            pLayout->addWidget(m_pEditorUsbSettings);
        }
    }
}

void UIMachineSettingsUSB::prepareConnections()
{
    /* Configure validation connections: */
    connect(m_pEditorUsbSettings, &UIUSBSettingsEditor::sigValueChanged,
            this, &UIMachineSettingsUSB::revalidate);
}

void UIMachineSettingsUSB::cleanup()
{
    /* Cleanup cache: */
    delete m_pCache;
    m_pCache = 0;
}

bool UIMachineSettingsUSB::saveData()
{
    /* Sanity check: */
    if (!m_pCache)
        return false;

    /* Prepare result: */
    bool fSuccess = true;
    /* Save USB settings from cache: */
    if (fSuccess && isMachineInValidMode() && m_pCache->wasChanged())
    {
        /* Get new USB data from cache: */
        const UIDataSettingsMachineUSB &newUsbData = m_pCache->data();

        /* Save USB data: */
        if (fSuccess && isMachineOffline())
        {
            /* Remove USB controllers: */
            if (!newUsbData.m_fUSBEnabled)
                fSuccess = removeUSBControllers();

            else

            /* Create/update USB controllers: */
            if (newUsbData.m_fUSBEnabled)
                fSuccess = createUSBControllers(newUsbData.m_enmUSBControllerType);
        }

        /* Save USB filters data: */
        if (fSuccess)
        {
            /* Make sure filters object really exists: */
            CUSBDeviceFilters comFiltersObject = m_machine.GetUSBDeviceFilters();
            fSuccess = m_machine.isOk() && comFiltersObject.isNotNull();

            /* Show error message if necessary: */
            if (!fSuccess)
                notifyOperationProgressError(UIErrorString::formatErrorInfo(m_machine));
            else
            {
                /* For each filter data set: */
                int iOperationPosition = 0;
                for (int iFilterIndex = 0; fSuccess && iFilterIndex < m_pCache->childCount(); ++iFilterIndex)
                {
                    /* Check if USB filter data was changed: */
                    const UISettingsCacheMachineUSBFilter &filterCache = m_pCache->child(iFilterIndex);

                    /* Remove filter marked for 'remove' or 'update': */
                    if (fSuccess && (filterCache.wasRemoved() || filterCache.wasUpdated()))
                    {
                        fSuccess = removeUSBFilter(comFiltersObject, iOperationPosition);
                        if (fSuccess && filterCache.wasRemoved())
                            --iOperationPosition;
                    }

                    /* Create filter marked for 'create' or 'update': */
                    if (fSuccess && (filterCache.wasCreated() || filterCache.wasUpdated()))
                        fSuccess = createUSBFilter(comFiltersObject, iOperationPosition, filterCache.data());

                    /* Advance operation position: */
                    ++iOperationPosition;
                }
            }
        }
    }
    /* Return result: */
    return fSuccess;
}

bool UIMachineSettingsUSB::removeUSBControllers(const QSet<KUSBControllerType> &types /* = QSet<KUSBControllerType>() */)
{
    /* Prepare result: */
    bool fSuccess = true;
    /* Remove controllers: */
    if (fSuccess && isMachineOffline())
    {
        /* Get controllers for further activities: */
        const CUSBControllerVector &controllers = m_machine.GetUSBControllers();
        fSuccess = m_machine.isOk();

        /* Show error message if necessary: */
        if (!fSuccess)
            notifyOperationProgressError(UIErrorString::formatErrorInfo(m_machine));

        /* For each controller: */
        for (int iControllerIndex = 0; fSuccess && iControllerIndex < controllers.size(); ++iControllerIndex)
        {
            /* Get current controller: */
            const CUSBController &comController = controllers.at(iControllerIndex);

            /* Get controller type for further activities: */
            KUSBControllerType enmType = KUSBControllerType_Null;
            if (fSuccess)
            {
                enmType = comController.GetType();
                fSuccess = comController.isOk();
            }
            /* Get controller name for further activities: */
            QString strName;
            if (fSuccess)
            {
                strName = comController.GetName();
                fSuccess = comController.isOk();
            }

            /* Show error message if necessary: */
            if (!fSuccess)
                notifyOperationProgressError(UIErrorString::formatErrorInfo(comController));
            else
            {
                /* Pass only if requested types were not defined or contains the one we found: */
                if (!types.isEmpty() && !types.contains(enmType))
                    continue;

                /* Remove controller: */
                if (fSuccess)
                {
                    m_machine.RemoveUSBController(comController.GetName());
                    fSuccess = m_machine.isOk();
                }

                /* Show error message if necessary: */
                if (!fSuccess)
                    notifyOperationProgressError(UIErrorString::formatErrorInfo(m_machine));
            }
        }
    }
    /* Return result: */
    return fSuccess;
}

bool UIMachineSettingsUSB::createUSBControllers(KUSBControllerType enmType)
{
    /* Prepare result: */
    bool fSuccess = true;
    /* Add controllers: */
    if (fSuccess && isMachineOffline())
    {
        /* Get each controller count for further activities: */
        ULONG cOhciCtls = 0;
        if (fSuccess)
        {
            cOhciCtls = m_machine.GetUSBControllerCountByType(KUSBControllerType_OHCI);
            fSuccess = m_machine.isOk();
        }
        ULONG cEhciCtls = 0;
        if (fSuccess)
        {
            cEhciCtls = m_machine.GetUSBControllerCountByType(KUSBControllerType_EHCI);
            fSuccess = m_machine.isOk();
        }
        ULONG cXhciCtls = 0;
        if (fSuccess)
        {
            cXhciCtls = m_machine.GetUSBControllerCountByType(KUSBControllerType_XHCI);
            fSuccess = m_machine.isOk();
        }

        /* Show error message if necessary: */
        if (!fSuccess)
            notifyOperationProgressError(UIErrorString::formatErrorInfo(m_machine));
        else
        {
            /* For requested controller type: */
            switch (enmType)
            {
                case KUSBControllerType_OHCI:
                {
                    /* Remove excessive controllers: */
                    if (cXhciCtls || cEhciCtls)
                        fSuccess = removeUSBControllers(QSet<KUSBControllerType>()
                                                        << KUSBControllerType_XHCI
                                                        << KUSBControllerType_EHCI);

                    /* Add required controller: */
                    if (fSuccess && !cOhciCtls)
                    {
                        m_machine.AddUSBController("OHCI", KUSBControllerType_OHCI);
                        fSuccess = m_machine.isOk();

                        /* Show error message if necessary: */
                        if (!fSuccess)
                            notifyOperationProgressError(UIErrorString::formatErrorInfo(m_machine));
                    }

                    break;
                }
                case KUSBControllerType_EHCI:
                {
                    /* Remove excessive controllers: */
                    if (cXhciCtls)
                        fSuccess = removeUSBControllers(QSet<KUSBControllerType>()
                                                        << KUSBControllerType_XHCI);

                    /* Add required controllers: */
                    if (fSuccess)
                    {
                        if (fSuccess && !cOhciCtls)
                        {
                            m_machine.AddUSBController("OHCI", KUSBControllerType_OHCI);
                            fSuccess = m_machine.isOk();
                        }
                        if (fSuccess && !cEhciCtls)
                        {
                            m_machine.AddUSBController("EHCI", KUSBControllerType_EHCI);
                            fSuccess = m_machine.isOk();
                        }

                        /* Show error message if necessary: */
                        if (!fSuccess)
                            notifyOperationProgressError(UIErrorString::formatErrorInfo(m_machine));
                    }

                    break;
                }
                case KUSBControllerType_XHCI:
                {
                    /* Remove excessive controllers: */
                    if (cEhciCtls || cOhciCtls)
                        fSuccess = removeUSBControllers(QSet<KUSBControllerType>()
                                                        << KUSBControllerType_EHCI
                                                        << KUSBControllerType_OHCI);

                    /* Add required controller: */
                    if (fSuccess && !cXhciCtls)
                    {
                        m_machine.AddUSBController("xHCI", KUSBControllerType_XHCI);
                        fSuccess = m_machine.isOk();

                        /* Show error message if necessary: */
                        if (!fSuccess)
                            notifyOperationProgressError(UIErrorString::formatErrorInfo(m_machine));
                    }

                    break;
                }
                default:
                    break;
            }
        }
    }
    /* Return result: */
    return fSuccess;
}

bool UIMachineSettingsUSB::removeUSBFilter(CUSBDeviceFilters &comFiltersObject, int iPosition)
{
    /* Prepare result: */
    bool fSuccess = true;
    /* Remove filter: */
    if (fSuccess)
    {
        /* Remove filter: */
        comFiltersObject.RemoveDeviceFilter(iPosition);
        fSuccess = comFiltersObject.isOk();

        /* Show error message if necessary: */
        if (!fSuccess)
            notifyOperationProgressError(UIErrorString::formatErrorInfo(comFiltersObject));
    }
    /* Return result: */
    return fSuccess;
}

bool UIMachineSettingsUSB::createUSBFilter(CUSBDeviceFilters &comFiltersObject, int iPosition, const UIDataSettingsMachineUSBFilter &filterData)
{
    /* Prepare result: */
    bool fSuccess = true;
    /* Add filter: */
    if (fSuccess)
    {
        /* Create filter: */
        CUSBDeviceFilter comFilter = comFiltersObject.CreateDeviceFilter(filterData.m_guiData.m_strName);
        fSuccess = comFiltersObject.isOk() && comFilter.isNotNull();

        /* Show error message if necessary: */
        if (!fSuccess)
            notifyOperationProgressError(UIErrorString::formatErrorInfo(comFiltersObject));
        else
        {
            /* Save whether filter is active: */
            if (fSuccess)
            {
                comFilter.SetActive(filterData.m_guiData.m_fActive);
                fSuccess = comFilter.isOk();
            }
            /* Save filter Vendor ID: */
            if (fSuccess)
            {
                comFilter.SetVendorId(filterData.m_guiData.m_strVendorId);
                fSuccess = comFilter.isOk();
            }
            /* Save filter Product ID: */
            if (fSuccess)
            {
                comFilter.SetProductId(filterData.m_guiData.m_strProductId);
                fSuccess = comFilter.isOk();
            }
            /* Save filter revision: */
            if (fSuccess)
            {
                comFilter.SetRevision(filterData.m_guiData.m_strRevision);
                fSuccess = comFilter.isOk();
            }
            /* Save filter manufacturer: */
            if (fSuccess)
            {
                comFilter.SetManufacturer(filterData.m_guiData.m_strManufacturer);
                fSuccess = comFilter.isOk();
            }
            /* Save filter product: */
            if (fSuccess)
            {
                comFilter.SetProduct(filterData.m_guiData.m_strProduct);
                fSuccess = comFilter.isOk();
            }
            /* Save filter serial number: */
            if (fSuccess)
            {
                comFilter.SetSerialNumber(filterData.m_guiData.m_strSerialNumber);
                fSuccess = comFilter.isOk();
            }
            /* Save filter port: */
            if (fSuccess)
            {
                comFilter.SetPort(filterData.m_guiData.m_strPort);
                fSuccess = comFilter.isOk();
            }
            /* Save filter remote mode: */
            if (fSuccess)
            {
                QString strRemote;
                switch (filterData.m_guiData.m_enmRemoteMode)
                {
                    case UIRemoteMode_On: strRemote = "1"; break;
                    case UIRemoteMode_Off: strRemote = "0"; break;
                    default: break;
                }
                comFilter.SetRemote(strRemote);
                fSuccess = comFilter.isOk();
            }

            /* Show error message if necessary: */
            if (!fSuccess)
                notifyOperationProgressError(UIErrorString::formatErrorInfo(comFilter));
            else
            {
                /* Insert filter onto corresponding position: */
                comFiltersObject.InsertDeviceFilter(iPosition, comFilter);
                fSuccess = comFiltersObject.isOk();

                /* Show error message if necessary: */
                if (!fSuccess)
                    notifyOperationProgressError(UIErrorString::formatErrorInfo(comFiltersObject));
            }
        }
    }
    /* Return result: */
    return fSuccess;
}
