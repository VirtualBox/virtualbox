/* $Id: DrvACPI.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DrvACPI - ACPI Host Driver.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DRV_ACPI

#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>
#endif

#include <VBox/vmm/pdmdrv.h>
#include <VBox/log.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/uuid.h>

#ifdef RT_OS_LINUX
# include <iprt/critsect.h>
# include <iprt/dir.h>
# include <iprt/semaphore.h>
# include <iprt/stream.h>
#endif

#ifdef RT_OS_DARWIN
# include <Carbon/Carbon.h>
# include <IOKit/ps/IOPowerSources.h>
# include <IOKit/ps/IOPSKeys.h>
# undef PVM                             /* This still messed up in the 10.9 SDK. Sigh. */
#endif

#ifdef RT_OS_FREEBSD
# include <sys/ioctl.h>
# include <dev/acpica/acpiio.h>
# include <sys/types.h>
# include <sys/sysctl.h>
# include <stdio.h>
# include <errno.h>
# include <fcntl.h>
# include <unistd.h>
#endif

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * ACPI driver instance data.
 *
 * @implements  PDMIACPICONNECTOR
 */
typedef struct DRVACPI
{
    /** The ACPI interface. */
    PDMIACPICONNECTOR   IACPIConnector;
    /** The ACPI port interface. */
    PPDMIACPIPORT       pPort;
    /** Pointer to the driver instance. */
    PPDMDRVINS          pDrvIns;

#ifdef RT_OS_LINUX
    /** The current power source. */
    PDMACPIPOWERSOURCE  enmPowerSource;
    /** true = one or more batteries preset, false = no battery present. */
    bool                fBatteryPresent;
    /** No need to RTThreadPoke the poller when set.  */
    bool volatile       fDontPokePoller;
    /** Remaining battery capacity. */
    PDMACPIBATCAPACITY  enmBatteryRemainingCapacity;
    /** Battery state. */
    PDMACPIBATSTATE     enmBatteryState;
    /** Preset battery charging/discharging rate. */
    uint32_t            u32BatteryPresentRate;
    /** The poller thread. */
    PPDMTHREAD          pPollerThread;
    /** Synchronize access to the above fields.
     * XXX A spinlock is probably cheaper ... */
    RTCRITSECT          CritSect;
    /** Event semaphore the poller thread is sleeping on. */
    RTSEMEVENT          hPollerSleepEvent;
#endif

} DRVACPI, *PDRVACPI;


/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) drvACPIQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVACPI   pThis = PDMINS_2_DATA(pDrvIns, PDRVACPI);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIACPICONNECTOR, &pThis->IACPIConnector);
    return NULL;
}

/**
 * Get the current power source of the host system.
 *
 * @returns status code
 * @param   pInterface   Pointer to the interface structure containing the called function pointer.
 * @param   pPowerSource Pointer to the power source result variable.
 */
static DECLCALLBACK(int) drvACPIQueryPowerSource(PPDMIACPICONNECTOR pInterface,
                                                 PDMACPIPOWERSOURCE *pPowerSource)
{
#if defined(RT_OS_WINDOWS)
    RT_NOREF(pInterface);
    SYSTEM_POWER_STATUS powerStatus;
    if (GetSystemPowerStatus(&powerStatus))
    {
        /* running on battery? */
        if (    powerStatus.ACLineStatus == 0   /* Offline */
             || powerStatus.ACLineStatus == 255 /* Unknown */
             && (powerStatus.BatteryFlag & 15)  /* high | low | critical | charging */
           ) /** @todo why is 'charging' included in the flag test?  Add parenthesis around the right bits so the code is clearer. */
        {
            *pPowerSource = PDM_ACPI_POWER_SOURCE_BATTERY;
        }
        /* running on AC link? */
        else if (powerStatus.ACLineStatus == 1)
        {
            *pPowerSource = PDM_ACPI_POWER_SOURCE_OUTLET;
        }
        else
        /* what the hell we're running on? */
        {
            *pPowerSource = PDM_ACPI_POWER_SOURCE_UNKNOWN;
        }
    }
    else
    {
        AssertMsgFailed(("Could not determine system power status, error: 0x%x\n",
                         GetLastError()));
        *pPowerSource = PDM_ACPI_POWER_SOURCE_UNKNOWN;
    }

#elif defined (RT_OS_LINUX)
    PDRVACPI pThis = RT_FROM_MEMBER(pInterface, DRVACPI, IACPIConnector);
    RTCritSectEnter(&pThis->CritSect);
    *pPowerSource = pThis->enmPowerSource;
    RTCritSectLeave(&pThis->CritSect);

#elif defined (RT_OS_DARWIN)
    RT_NOREF(pInterface);
    *pPowerSource = PDM_ACPI_POWER_SOURCE_UNKNOWN;

    CFTypeRef pBlob = IOPSCopyPowerSourcesInfo();
    CFArrayRef pSources = IOPSCopyPowerSourcesList(pBlob);

    CFDictionaryRef pSource = NULL;
    const void *psValue;
    bool fResult;

    if (CFArrayGetCount(pSources) > 0)
    {
        for (int i = 0; i < CFArrayGetCount(pSources); ++i)
        {
            pSource = IOPSGetPowerSourceDescription(pBlob, CFArrayGetValueAtIndex(pSources, i));
            /* If the source is empty skip over to the next one. */
            if(!pSource)
                continue;
            /* Skip all power sources which are currently not present like a
             * second battery. */
            if (CFDictionaryGetValue(pSource, CFSTR(kIOPSIsPresentKey)) == kCFBooleanFalse)
                continue;
            /* Only internal power types are of interest. */
            fResult = CFDictionaryGetValueIfPresent(pSource, CFSTR(kIOPSTransportTypeKey), &psValue);
            if (   fResult
                && CFStringCompare((CFStringRef)psValue, CFSTR(kIOPSInternalType), 0) == kCFCompareEqualTo)
            {
                /* Check which power source we are connect on. */
                fResult = CFDictionaryGetValueIfPresent(pSource, CFSTR(kIOPSPowerSourceStateKey), &psValue);
                if (   fResult
                    && CFStringCompare((CFStringRef)psValue, CFSTR(kIOPSACPowerValue), 0) == kCFCompareEqualTo)
                    *pPowerSource = PDM_ACPI_POWER_SOURCE_OUTLET;
                else if (   fResult
                         && CFStringCompare((CFStringRef)psValue, CFSTR(kIOPSBatteryPowerValue), 0) == kCFCompareEqualTo)
                    *pPowerSource = PDM_ACPI_POWER_SOURCE_BATTERY;
            }
        }
    }
    CFRelease(pBlob);
    CFRelease(pSources);

#elif defined(RT_OS_FREEBSD)
    RT_NOREF(pInterface);
    int fAcLine = 0;
    size_t cbParameter = sizeof(fAcLine);

    int rc = sysctlbyname("hw.acpi.acline", &fAcLine, &cbParameter, NULL, 0);

    if (!rc)
    {
        if (fAcLine == 1)
            *pPowerSource = PDM_ACPI_POWER_SOURCE_OUTLET;
        else if (fAcLine == 0)
            *pPowerSource = PDM_ACPI_POWER_SOURCE_BATTERY;
        else
            *pPowerSource = PDM_ACPI_POWER_SOURCE_UNKNOWN;
    }
    else
    {
        AssertMsg(errno == ENOENT, ("rc=%d (%s)\n", rc, strerror(errno)));
        *pPowerSource = PDM_ACPI_POWER_SOURCE_OUTLET;
    }
#else /* !RT_OS_FREEBSD either - what could this be? */
    RT_NOREF(pInterface);
    *pPowerSource = PDM_ACPI_POWER_SOURCE_OUTLET;

#endif /* !RT_OS_FREEBSD */
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIACPICONNECTOR,pfnQueryBatteryStatus}
 */
static DECLCALLBACK(int) drvACPIQueryBatteryStatus(PPDMIACPICONNECTOR pInterface, bool *pfPresent,
                                                   PPDMACPIBATCAPACITY penmRemainingCapacity,
                                                   PPDMACPIBATSTATE penmBatteryState,
                                                   uint32_t *pu32PresentRate)
{
    /* default return values for all architectures */
    *pfPresent              = false;        /* no battery present */
    *penmBatteryState       = PDM_ACPI_BAT_STATE_CHARGED;
    *penmRemainingCapacity  = PDM_ACPI_BAT_CAPACITY_UNKNOWN;
    *pu32PresentRate        = UINT32_MAX;   /* present rate is unknown */

#if defined(RT_OS_WINDOWS)
    RT_NOREF(pInterface);
    SYSTEM_POWER_STATUS powerStatus;
    if (GetSystemPowerStatus(&powerStatus))
    {
        /* 128 means no battery present */
        *pfPresent = !(powerStatus.BatteryFlag & 128);
        /* just forward the value directly */
        *penmRemainingCapacity = (PDMACPIBATCAPACITY)powerStatus.BatteryLifePercent;
        /* we assume that we are discharging the battery if we are not on-line and
         * not charge the battery */
        uint32_t uBs = PDM_ACPI_BAT_STATE_CHARGED;
        if (powerStatus.BatteryFlag & 8)
            uBs = PDM_ACPI_BAT_STATE_CHARGING;
        else if (powerStatus.ACLineStatus == 0 || powerStatus.ACLineStatus == 255)
            uBs = PDM_ACPI_BAT_STATE_DISCHARGING;
        if (powerStatus.BatteryFlag & 4)
            uBs |= PDM_ACPI_BAT_STATE_CRITICAL;
        *penmBatteryState = (PDMACPIBATSTATE)uBs;
        /* on Windows it is difficult to request the present charging/discharging rate */
    }
    else
    {
        AssertMsgFailed(("Could not determine system power status, error: 0x%x\n",
                    GetLastError()));
    }

#elif defined(RT_OS_LINUX)
    PDRVACPI pThis = RT_FROM_MEMBER(pInterface, DRVACPI, IACPIConnector);
    RTCritSectEnter(&pThis->CritSect);
    *pfPresent = pThis->fBatteryPresent;
    *penmRemainingCapacity = pThis->enmBatteryRemainingCapacity;
    *penmBatteryState = pThis->enmBatteryState;
    *pu32PresentRate = pThis->u32BatteryPresentRate;
    RTCritSectLeave(&pThis->CritSect);

#elif defined(RT_OS_DARWIN)
    RT_NOREF(pInterface);
    CFTypeRef pBlob = IOPSCopyPowerSourcesInfo();
    CFArrayRef pSources = IOPSCopyPowerSourcesList(pBlob);

    CFDictionaryRef pSource = NULL;
    const void *psValue;
    bool fResult;

    if (CFArrayGetCount(pSources) > 0)
    {
        for (int i = 0; i < CFArrayGetCount(pSources); ++i)
        {
            pSource = IOPSGetPowerSourceDescription(pBlob, CFArrayGetValueAtIndex(pSources, i));
            /* If the source is empty skip over to the next one. */
            if(!pSource)
                continue;
            /* Skip all power sources which are currently not present like a
             * second battery. */
            if (CFDictionaryGetValue(pSource, CFSTR(kIOPSIsPresentKey)) == kCFBooleanFalse)
                continue;
            /* Only internal power types are of interest. */
            fResult = CFDictionaryGetValueIfPresent(pSource, CFSTR(kIOPSTransportTypeKey), &psValue);
            if (   fResult
                    && CFStringCompare((CFStringRef)psValue, CFSTR(kIOPSInternalType), 0) == kCFCompareEqualTo)
            {
                PDMACPIPOWERSOURCE powerSource = PDM_ACPI_POWER_SOURCE_UNKNOWN;
                /* First check which power source we are connect on. */
                fResult = CFDictionaryGetValueIfPresent(pSource, CFSTR(kIOPSPowerSourceStateKey), &psValue);
                if (   fResult
                        && CFStringCompare((CFStringRef)psValue, CFSTR(kIOPSACPowerValue), 0) == kCFCompareEqualTo)
                    powerSource = PDM_ACPI_POWER_SOURCE_OUTLET;
                else if (   fResult
                        && CFStringCompare((CFStringRef)psValue, CFSTR(kIOPSBatteryPowerValue), 0) == kCFCompareEqualTo)
                    powerSource = PDM_ACPI_POWER_SOURCE_BATTERY;

                /* At this point the power source is present. */
                *pfPresent = true;
                *penmBatteryState = PDM_ACPI_BAT_STATE_CHARGED;

                int curCapacity = 0;
                int maxCapacity = 1;
                float remCapacity = 0.0f;

                /* Fetch the current capacity value of the power source */
                fResult = CFDictionaryGetValueIfPresent(pSource, CFSTR(kIOPSCurrentCapacityKey), &psValue);
                if (fResult)
                    CFNumberGetValue((CFNumberRef)psValue, kCFNumberSInt32Type, &curCapacity);
                /* Fetch the maximum capacity value of the power source */
                fResult = CFDictionaryGetValueIfPresent(pSource, CFSTR(kIOPSMaxCapacityKey), &psValue);
                if (fResult)
                    CFNumberGetValue((CFNumberRef)psValue, kCFNumberSInt32Type, &maxCapacity);

                /* Calculate the remaining capacity in percent */
                remCapacity = ((float)curCapacity/(float)maxCapacity * PDM_ACPI_BAT_CAPACITY_MAX);
                *penmRemainingCapacity = (PDMACPIBATCAPACITY)remCapacity;

                if (powerSource == PDM_ACPI_POWER_SOURCE_BATTERY)
                {
                    /* If we are on battery power we are discharging in every
                     * case */
                    *penmBatteryState = PDM_ACPI_BAT_STATE_DISCHARGING;
                    int timeToEmpty = -1;
                    /* Get the time till the battery source will be empty */
                    fResult = CFDictionaryGetValueIfPresent(pSource, CFSTR(kIOPSTimeToEmptyKey), &psValue);
                    if (fResult)
                        CFNumberGetValue((CFNumberRef)psValue, kCFNumberSInt32Type, &timeToEmpty);
                    if (timeToEmpty != -1)
                        /* 0...1000 */
                        *pu32PresentRate = (uint32_t)roundf((remCapacity / ((float)timeToEmpty/60.0)) * 10.0);
                }

                if (   powerSource == PDM_ACPI_POWER_SOURCE_OUTLET
                        && CFDictionaryGetValueIfPresent(pSource, CFSTR(kIOPSIsChargingKey), &psValue))
                {
                    /* We are running on an AC power source, but we also have a
                     * battery power source present. */
                    if (CFBooleanGetValue((CFBooleanRef)psValue) > 0)
                    {
                        /* This means charging. */
                        *penmBatteryState = PDM_ACPI_BAT_STATE_CHARGING;
                        int timeToFull = -1;
                        /* Get the time till the battery source will be charged */
                        fResult = CFDictionaryGetValueIfPresent(pSource, CFSTR(kIOPSTimeToFullChargeKey), &psValue);
                        if (fResult)
                            CFNumberGetValue((CFNumberRef)psValue, kCFNumberSInt32Type, &timeToFull);
                        if (timeToFull != -1)
                            /* 0...1000 */
                            *pu32PresentRate = (uint32_t)roundf((100.0-(float)remCapacity) / ((float)timeToFull/60.0)) * 10.0;
                    }
                }

                /* Check for critical */
                int criticalValue = 20;
                fResult = CFDictionaryGetValueIfPresent(pSource, CFSTR(kIOPSDeadWarnLevelKey), &psValue);
                if (fResult)
                    CFNumberGetValue((CFNumberRef)psValue, kCFNumberSInt32Type, &criticalValue);
                if (remCapacity < criticalValue)
                    *penmBatteryState = (PDMACPIBATSTATE)(*penmBatteryState | PDM_ACPI_BAT_STATE_CRITICAL);
            }
        }
    }
    CFRelease(pBlob);
    CFRelease(pSources);

#elif defined(RT_OS_FREEBSD)
    RT_NOREF(pInterface);
    /* We try to use /dev/acpi first and if that fails use the sysctls. */
    bool fSuccess = true;
    int FileAcpi = 0;
    int rc = 0;

    FileAcpi = open("/dev/acpi", O_RDONLY);
    if (FileAcpi != -1)
    {
        bool fMilliWatt;
        union acpi_battery_ioctl_arg BatteryIo;

        memset(&BatteryIo, 0, sizeof(BatteryIo));
        BatteryIo.unit = 0; /* Always use the first battery. */

        /* Determine the power units first. */
        if (ioctl(FileAcpi, ACPIIO_BATT_GET_BIF, &BatteryIo) == -1)
            fSuccess = false;
        else
        {
            if (BatteryIo.bif.units == ACPI_BIF_UNITS_MW)
                fMilliWatt = true;
            else
                fMilliWatt = false; /* mA */

            BatteryIo.unit = 0;
            if (ioctl(FileAcpi, ACPIIO_BATT_GET_BATTINFO, &BatteryIo) == -1)
                fSuccess = false;
            else
            {
                if ((BatteryIo.battinfo.state & ACPI_BATT_STAT_NOT_PRESENT) == ACPI_BATT_STAT_NOT_PRESENT)
                    *pfPresent = false;
                else
                {
                    *pfPresent = true;

                    if (BatteryIo.battinfo.state & ACPI_BATT_STAT_DISCHARG)
                        *penmBatteryState = PDM_ACPI_BAT_STATE_DISCHARGING;
                    else if (BatteryIo.battinfo.state & ACPI_BATT_STAT_CHARGING)
                        *penmBatteryState = PDM_ACPI_BAT_STATE_CHARGING;
                    else
                        *penmBatteryState = PDM_ACPI_BAT_STATE_CHARGED;

                    if (BatteryIo.battinfo.state & ACPI_BATT_STAT_CRITICAL)
                        *penmBatteryState = (PDMACPIBATSTATE)(*penmBatteryState | PDM_ACPI_BAT_STATE_CRITICAL);
                }

                if (BatteryIo.battinfo.cap != -1)
                    *penmRemainingCapacity = (PDMACPIBATCAPACITY)BatteryIo.battinfo.cap;

                BatteryIo.unit = 0;
                if (ioctl(FileAcpi, ACPIIO_BATT_GET_BST, &BatteryIo) == 0)
                {
                    /* The rate can be either mW or mA but the ACPI device wants mW. */
                    if (BatteryIo.bst.rate != 0xffffffff)
                    {
                        if (fMilliWatt)
                            *pu32PresentRate = BatteryIo.bst.rate;
                        else if (BatteryIo.bst.volt != 0xffffffff)
                        {
                            /*
                             * The rate is in mA so we have to convert it.
                             * The current power rate can be calculated with P = U * I
                             */
                            *pu32PresentRate = (uint32_t)(  (  ((float)BatteryIo.bst.volt/1000.0)
                                        * ((float)BatteryIo.bst.rate/1000.0))
                                    * 1000.0);
                        }
                    }
                }
            }
        }

        close(FileAcpi);
    }
    else
        fSuccess = false;

    if (!fSuccess)
    {
        int fBatteryState = 0;
        size_t cbParameter = sizeof(fBatteryState);

        rc = sysctlbyname("hw.acpi.battery.state", &fBatteryState, &cbParameter, NULL, 0);
        if (!rc)
        {
            if ((fBatteryState & ACPI_BATT_STAT_NOT_PRESENT) == ACPI_BATT_STAT_NOT_PRESENT)
                *pfPresent = false;
            else
            {
                *pfPresent = true;

                if (fBatteryState & ACPI_BATT_STAT_DISCHARG)
                    *penmBatteryState = PDM_ACPI_BAT_STATE_DISCHARGING;
                else if (fBatteryState & ACPI_BATT_STAT_CHARGING)
                    *penmBatteryState = PDM_ACPI_BAT_STATE_CHARGING;
                else
                    *penmBatteryState = PDM_ACPI_BAT_STATE_CHARGED;

                if (fBatteryState & ACPI_BATT_STAT_CRITICAL)
                    *penmBatteryState = (PDMACPIBATSTATE)(*penmBatteryState | PDM_ACPI_BAT_STATE_CRITICAL);

                /* Get battery level. */
                int curCapacity = 0;
                cbParameter = sizeof(curCapacity);
                rc = sysctlbyname("hw.acpi.battery.life", &curCapacity, &cbParameter, NULL, 0);
                if (!rc && curCapacity >= 0)
                    *penmRemainingCapacity = (PDMACPIBATCAPACITY)curCapacity;

                /* The rate can't be determined with sysctls. */
            }
        }
    }

#endif /* RT_OS_FREEBSD */

    return VINF_SUCCESS;
}

#ifdef RT_OS_LINUX
/**
 * Poller thread for /proc/acpi status files.
 *
 * Reading these files takes ages (several seconds) on some hosts, therefore
 * start this thread. The termination of this thread may take some seconds
 * on such a hosts!
 *
 * @param   pDrvIns     The driver instance data.
 * @param   pThread     The thread.
 */
static DECLCALLBACK(int) drvACPIPoller(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    PDRVACPI pThis = PDMINS_2_DATA(pDrvIns, PDRVACPI);

    if (pThread->enmState == PDMTHREADSTATE_INITIALIZING)
        return VINF_SUCCESS;

    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        ASMAtomicWriteBool(&pThis->fDontPokePoller, false);

        PDMACPIPOWERSOURCE enmPowerSource = PDM_ACPI_POWER_SOURCE_UNKNOWN;
        PRTSTREAM  pStrmStatus;
        PRTSTREAM  pStrmType;
        RTDIR      hDir = NIL_RTDIR;
        RTDIRENTRY DirEntry;
        char       szLine[1024];
        bool       fBatteryPresent = false;     /* one or more batteries present */
        bool       fCharging = false;           /* one or more batteries charging */
        bool       fDischarging = false;        /* one or more batteries discharging */
        bool       fCritical = false;           /* one or more batteries in critical state */
        bool       fDataChanged;                /* if battery status data changed during last poll */
        int32_t    maxCapacityTotal = 0;        /* total capacity of all batteries */
        int32_t    currentCapacityTotal = 0;    /* total current capacity of all batteries */
        int32_t    presentRateTotal = 0;        /* total present (dis)charging rate of all batts */
        PDMACPIBATCAPACITY enmBatteryRemainingCapacity; /* total remaining capacity of vbox batt */
        uint32_t u32BatteryPresentRate;         /* total present (dis)charging rate of vbox batt */

        int rc = RTDirOpen(&hDir, "/sys/class/power_supply/");
        if (RT_SUCCESS(rc))
        {
            /*
             * The new /sys interface introduced with Linux 2.6.25.
             */
            while (pThread->enmState == PDMTHREADSTATE_RUNNING)
            {
                rc = RTDirRead(hDir, &DirEntry, NULL);
                if (RT_FAILURE(rc))
                    break;
                if (   strcmp(DirEntry.szName, ".") == 0
                    || strcmp(DirEntry.szName, "..") == 0)
                    continue;
#define POWER_OPEN(s, n) RTStrmOpenF("r", s, "/sys/class/power_supply/%s/" n, DirEntry.szName)
                rc = POWER_OPEN(&pStrmType, "type");
                if (RT_FAILURE(rc))
                    continue;
                rc = RTStrmGetLine(pStrmType, szLine, sizeof(szLine));
                if (RT_SUCCESS(rc))
                {
                    if (strcmp(szLine, "Mains") == 0)
                    {
                        /* AC adapter */
                        rc = POWER_OPEN(&pStrmStatus, "online");
                        if (RT_SUCCESS(rc))
                        {
                            rc = RTStrmGetLine(pStrmStatus, szLine, sizeof(szLine));
                            if (   RT_SUCCESS(rc)
                                && strcmp(szLine, "1") == 0)
                                enmPowerSource = PDM_ACPI_POWER_SOURCE_OUTLET;
                            else
                                enmPowerSource = PDM_ACPI_POWER_SOURCE_BATTERY;
                            RTStrmClose(pStrmStatus);
                        }
                    }
                    else if (strcmp(szLine, "Battery") == 0)
                    {
                        /* Battery */
                        rc = POWER_OPEN(&pStrmStatus, "present");
                        if (RT_SUCCESS(rc))
                        {
                            rc = RTStrmGetLine(pStrmStatus, szLine, sizeof(szLine));
                            RTStrmClose(pStrmStatus);
                            if (   RT_SUCCESS(rc)
                                && strcmp(szLine, "1") == 0)
                            {
                                fBatteryPresent = true;
                                rc = RTStrmOpenF("r", &pStrmStatus,
                                                 "/sys/class/power_supply/%s/status", DirEntry.szName);
                                if (RT_SUCCESS(rc))
                                {
                                    rc = RTStrmGetLine(pStrmStatus, szLine, sizeof(szLine));
                                    if (RT_SUCCESS(rc))
                                    {
                                        if (strcmp(szLine, "Discharging") == 0)
                                            fDischarging = true;
                                        else if (strcmp(szLine, "Charging") == 0)
                                            fCharging = true;
                                    }
                                    RTStrmClose(pStrmStatus);
                                }
                                rc = POWER_OPEN(&pStrmStatus, "capacity_level");
                                if (RT_SUCCESS(rc))
                                {
                                    rc = RTStrmGetLine(pStrmStatus, szLine, sizeof(szLine));
                                    if (   RT_SUCCESS(rc)
                                        && strcmp(szLine, "Critical") == 0)
                                        fCritical = true;
                                    RTStrmClose(pStrmStatus);
                                }
                                rc = POWER_OPEN(&pStrmStatus, "energy_full");
                                if (RT_FAILURE(rc))
                                    rc = POWER_OPEN(&pStrmStatus, "charge_full");
                                if (RT_SUCCESS(rc))
                                {
                                    rc = RTStrmGetLine(pStrmStatus, szLine, sizeof(szLine));
                                    if (RT_SUCCESS(rc))
                                    {
                                        int32_t maxCapacity = 0;
                                        rc = RTStrToInt32Full(szLine, 0, &maxCapacity);
                                        if (   RT_SUCCESS(rc)
                                            && maxCapacity > 0)
                                            maxCapacityTotal += maxCapacity;
                                    }
                                    RTStrmClose(pStrmStatus);
                                }
                                rc = POWER_OPEN(&pStrmStatus, "energy_now");
                                if (RT_FAILURE(rc))
                                    rc = POWER_OPEN(&pStrmStatus, "charge_now");
                                if (RT_SUCCESS(rc))
                                {
                                    rc = RTStrmGetLine(pStrmStatus, szLine, sizeof(szLine));
                                    if (RT_SUCCESS(rc))
                                    {
                                        int32_t currentCapacity = 0;
                                        rc = RTStrToInt32Full(szLine, 0, &currentCapacity);
                                        if (   RT_SUCCESS(rc)
                                            && currentCapacity > 0)
                                            currentCapacityTotal += currentCapacity;
                                    }
                                    RTStrmClose(pStrmStatus);
                                }
                                rc = POWER_OPEN(&pStrmStatus, "power_now");
                                if (RT_FAILURE(rc))
                                    rc = POWER_OPEN(&pStrmStatus, "current_now");
                                if (RT_SUCCESS(rc))
                                {
                                    rc = RTStrmGetLine(pStrmStatus, szLine, sizeof(szLine));
                                    if (RT_SUCCESS(rc))
                                    {
                                        int32_t presentRate = 0;
                                        rc = RTStrToInt32Full(szLine, 0, &presentRate);
                                        if (   RT_SUCCESS(rc)
                                            && presentRate > 0)
                                        {
                                            if (fDischarging)
                                                presentRateTotal -= presentRate;
                                            else
                                                presentRateTotal += presentRate;
                                        }
                                    }
                                    RTStrmClose(pStrmStatus);
                                }
                            }
                        }
                    }
                }
                RTStrmClose(pStrmType);
#undef POWER_OPEN
            }
            RTDirClose(hDir);
        }
        else /* !/sys */
        {
            /*
             * The old /proc/acpi interface
             */
            /*
             * Read the status of the powerline-adapter.
             */
            rc = RTDirOpen(&hDir, "/proc/acpi/ac_adapter/");
            if (RT_SUCCESS(rc))
            {
#define POWER_OPEN(s, n) RTStrmOpenF("r", s, "/proc/acpi/ac_adapter/%s/" n, DirEntry.szName)
                while (pThread->enmState == PDMTHREADSTATE_RUNNING)
                {
                    rc = RTDirRead(hDir, &DirEntry, NULL);
                    if (RT_FAILURE(rc))
                        break;
                    if (   strcmp(DirEntry.szName, ".") == 0
                        || strcmp(DirEntry.szName, "..") == 0)
                        continue;
                    rc = POWER_OPEN(&pStrmStatus, "status");
                    if (RT_FAILURE(rc))
                        rc = POWER_OPEN(&pStrmStatus, "state");
                    if (RT_SUCCESS(rc))
                    {
                        while (pThread->enmState == PDMTHREADSTATE_RUNNING)
                        {
                            rc = RTStrmGetLine(pStrmStatus, szLine, sizeof(szLine));
                            if (RT_FAILURE(rc))
                                break;
                            if (   strstr(szLine, "Status:") != NULL
                                || strstr(szLine, "state:") != NULL)
                            {
                                if (strstr(szLine, "on-line") != NULL)
                                    enmPowerSource = PDM_ACPI_POWER_SOURCE_OUTLET;
                                else
                                    enmPowerSource = PDM_ACPI_POWER_SOURCE_BATTERY;
                                break;
                            }
                        }
                        RTStrmClose(pStrmStatus);
                        break;
                    }
                }
                RTDirClose(hDir);
#undef POWER_OPEN
            }

            /*
             * Read the status of all batteries and collect it into one.
             */
            rc = RTDirOpen(&hDir, "/proc/acpi/battery/");
            if (RT_SUCCESS(rc))
            {
#define POWER_OPEN(s, n) RTStrmOpenF("r", s, "/proc/acpi/battery/%s/" n, DirEntry.szName)
                bool fThisBatteryPresent = false;
                bool fThisDischarging = false;

                while (pThread->enmState == PDMTHREADSTATE_RUNNING)
                {
                    rc = RTDirRead(hDir, &DirEntry, NULL);
                    if (RT_FAILURE(rc))
                        break;
                    if (   strcmp(DirEntry.szName, ".") == 0
                        || strcmp(DirEntry.szName, "..") == 0)
                        continue;

                    rc = POWER_OPEN(&pStrmStatus, "status");
                    /* there is a 2nd variant of that file */
                    if (RT_FAILURE(rc))
                        rc = POWER_OPEN(&pStrmStatus, "state");
                    if (RT_FAILURE(rc))
                        continue;

                    PRTSTREAM pStrmInfo;
                    rc = POWER_OPEN(&pStrmInfo, "info");
                    if (RT_FAILURE(rc))
                    {
                        RTStrmClose(pStrmStatus);
                        continue;
                    }

                    /* get 'present' status from the info file */
                    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
                    {
                        rc = RTStrmGetLine(pStrmInfo, szLine, sizeof(szLine));
                        if (RT_FAILURE(rc))
                            break;
                        if (strstr(szLine, "present:") != NULL)
                        {
                            if (strstr(szLine, "yes") != NULL)
                            {
                                fThisBatteryPresent = true;
                                break;
                            }
                        }
                    }

                    if (fThisBatteryPresent)
                    {
                        fBatteryPresent = true;
                        RTStrmRewind(pStrmInfo);

                        /* get the maximum capacity from the info file */
                        while (pThread->enmState == PDMTHREADSTATE_RUNNING)
                        {
                            rc = RTStrmGetLine(pStrmInfo, szLine, sizeof(szLine));
                            if (RT_FAILURE(rc))
                                break;
                            if (strstr(szLine, "last full capacity:") != NULL)
                            {
                                char *psz;
                                int32_t maxCapacity = 0;
                                rc = RTStrToInt32Ex(RTStrStripL(&szLine[19]), &psz, 0, &maxCapacity);
                                if (RT_FAILURE(rc))
                                    maxCapacity = 0;
                                maxCapacityTotal += maxCapacity;
                                break;
                            }
                        }

                        /* get the current capacity/state from the status file */
                        int32_t presentRate = 0;
                        bool fGotRemainingCapacity = false;
                        bool fGotBatteryState = false;
                        bool fGotCapacityState = false;
                        bool fGotPresentRate = false;
                        while (  (   !fGotRemainingCapacity
                                  || !fGotBatteryState
                                  || !fGotCapacityState
                                  || !fGotPresentRate)
                               && pThread->enmState == PDMTHREADSTATE_RUNNING)
                        {
                            rc = RTStrmGetLine(pStrmStatus, szLine, sizeof(szLine));
                            if (RT_FAILURE(rc))
                                break;
                            if (strstr(szLine, "remaining capacity:") != NULL)
                            {
                                char *psz;
                                int32_t currentCapacity = 0;
                                rc = RTStrToInt32Ex(RTStrStripL(&szLine[19]), &psz, 0, &currentCapacity);
                                if (   RT_SUCCESS(rc)
                                    && currentCapacity > 0)
                                    currentCapacityTotal += currentCapacity;
                                fGotRemainingCapacity = true;
                            }
                            else if (strstr(szLine, "charging state:") != NULL)
                            {
                                if (strstr(szLine + 15, "discharging") != NULL)
                                {
                                    fDischarging = true;
                                    fThisDischarging = true;
                                }
                                else if (strstr(szLine + 15, "charging") != NULL)
                                    fCharging = true;
                                fGotBatteryState = true;
                            }
                            else if (strstr(szLine, "capacity state:") != NULL)
                            {
                                if (strstr(szLine + 15, "critical") != NULL)
                                    fCritical = true;
                                fGotCapacityState = true;
                            }
                            if (strstr(szLine, "present rate:") != NULL)
                            {
                                char *psz;
                                rc = RTStrToInt32Ex(RTStrStripL(&szLine[13]), &psz, 0, &presentRate);
                                if (RT_FAILURE(rc))
                                    presentRate = 0;
                                fGotPresentRate = true;
                            }
                        }
                        if (fThisDischarging)
                            presentRateTotal -= presentRate;
                        else
                            presentRateTotal += presentRate;
                    }
                    RTStrmClose(pStrmStatus);
                    RTStrmClose(pStrmInfo);
                }
                RTDirClose(hDir);
#undef POWER_OPEN
            }
        } /* /proc/acpi */

        /* atomic update of the state */
        RTCritSectEnter(&pThis->CritSect);

        /* charging/discharging bits are mutual exclusive */
        uint32_t uBs = PDM_ACPI_BAT_STATE_CHARGED;
        if (fDischarging)
            uBs = PDM_ACPI_BAT_STATE_DISCHARGING;
        else if (fCharging)
            uBs = PDM_ACPI_BAT_STATE_CHARGING;
        if (fCritical)
            uBs |= PDM_ACPI_BAT_STATE_CRITICAL;

        if (maxCapacityTotal > 0 && currentCapacityTotal > 0)
        {
            if (presentRateTotal < 0)
                presentRateTotal = -presentRateTotal;

            /* calculate the percentage */

            enmBatteryRemainingCapacity =
                                 (PDMACPIBATCAPACITY)( (  (float)currentCapacityTotal
                                                        / (float)maxCapacityTotal)
                                                      * PDM_ACPI_BAT_CAPACITY_MAX);
            u32BatteryPresentRate =
                                 (uint32_t)((  (float)presentRateTotal
                                             / (float)maxCapacityTotal) * 1000);
        }
        else
        {
            /* unknown capacity / state */
            enmBatteryRemainingCapacity = PDM_ACPI_BAT_CAPACITY_UNKNOWN;
            u32BatteryPresentRate = ~0;
        }

        if (   pThis->enmPowerSource  == enmPowerSource
            && pThis->fBatteryPresent == fBatteryPresent
            && pThis->enmBatteryState == (PDMACPIBATSTATE) uBs
            && pThis->enmBatteryRemainingCapacity == enmBatteryRemainingCapacity
            && pThis->u32BatteryPresentRate == u32BatteryPresentRate)
        {
            fDataChanged = false;
        }
        else
        {
            fDataChanged = true;

            pThis->enmPowerSource = enmPowerSource;
            pThis->fBatteryPresent = fBatteryPresent;
            pThis->enmBatteryState = (PDMACPIBATSTATE)uBs;
            pThis->enmBatteryRemainingCapacity = enmBatteryRemainingCapacity;
            pThis->u32BatteryPresentRate = u32BatteryPresentRate;
        }

        RTCritSectLeave(&pThis->CritSect);

        if (fDataChanged)
            pThis->pPort->pfnBatteryStatusChangeEvent(pThis->pPort);

        /* wait a bit (e.g. Ubuntu/GNOME polls every 30 seconds) */
        ASMAtomicWriteBool(&pThis->fDontPokePoller, true);
        rc = RTSemEventWait(pThis->hPollerSleepEvent, 20000);
    }

    return VINF_SUCCESS;
}

static DECLCALLBACK(int) drvACPIPollerWakeup(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    PDRVACPI pThis = PDMINS_2_DATA(pDrvIns, PDRVACPI);

    RTSemEventSignal(pThis->hPollerSleepEvent);
    if (!ASMAtomicReadBool(&pThis->fDontPokePoller))
        RTThreadPoke(pThread->Thread);
    return VINF_SUCCESS;
}
#endif /* RT_OS_LINUX */


/**
 * Destruct a driver instance.
 *
 * Most VM resources are freed by the VM. This callback is provided so that any non-VM
 * resources can be freed correctly.
 *
 * @param   pDrvIns     The driver instance data.
 */
static DECLCALLBACK(void) drvACPIDestruct(PPDMDRVINS pDrvIns)
{
    LogFlow(("drvACPIDestruct\n"));
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);

#ifdef RT_OS_LINUX
    PDRVACPI pThis = PDMINS_2_DATA(pDrvIns, PDRVACPI);
    if (pThis->hPollerSleepEvent != NIL_RTSEMEVENT)
    {
        RTSemEventDestroy(pThis->hPollerSleepEvent);
        pThis->hPollerSleepEvent = NIL_RTSEMEVENT;
    }
    RTCritSectDelete(&pThis->CritSect);
#endif
}

/**
 * Construct an ACPI driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
static DECLCALLBACK(int) drvACPIConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF(pCfg, fFlags);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVACPI pThis = PDMINS_2_DATA(pDrvIns, PDRVACPI);
    int rc = VINF_SUCCESS;

    /*
     * Init the static parts.
     */
    pThis->pDrvIns                              = pDrvIns;
#ifdef RT_OS_LINUX
    pThis->hPollerSleepEvent                    = NIL_RTSEMEVENT;
#endif
    /* IBase */
    pDrvIns->IBase.pfnQueryInterface            = drvACPIQueryInterface;
    /* IACPIConnector */
    pThis->IACPIConnector.pfnQueryPowerSource   = drvACPIQueryPowerSource;
    pThis->IACPIConnector.pfnQueryBatteryStatus = drvACPIQueryBatteryStatus;

    /*
     * Validate the config.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns, "", "");

    /*
     * Check that no-one is attached to us.
     */
    AssertMsgReturn(PDMDrvHlpNoAttach(pDrvIns) == VERR_PDM_NO_ATTACHED_DRIVER,
                    ("Configuration error: Not possible to attach anything to this driver!\n"),
                    VERR_PDM_DRVINS_NO_ATTACH);

    /*
     * Query the ACPI port interface.
     */
    pThis->pPort = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMIACPIPORT);
    if (!pThis->pPort)
    {
        AssertMsgFailed(("Configuration error: the above device/driver didn't export the ACPI port interface!\n"));
        return VERR_PDM_MISSING_INTERFACE_ABOVE;
    }

#ifdef RT_OS_LINUX
    /*
     * Start the poller thread.
     */
    rc = PDMDrvHlpThreadCreate(pDrvIns, &pThis->pPollerThread, pThis, drvACPIPoller,
                               drvACPIPollerWakeup, 0, RTTHREADTYPE_INFREQUENT_POLLER, "ACPI Poller");
    if (RT_FAILURE(rc))
        return rc;

    rc = RTCritSectInit(&pThis->CritSect);
    if (RT_FAILURE(rc))
        return rc;

    rc = RTSemEventCreate(&pThis->hPollerSleepEvent);
#endif

    return rc;
}


/**
 * ACPI driver registration record.
 */
const PDMDRVREG g_DrvACPI =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "ACPIHost",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "ACPI Host Driver",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_ACPI,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVACPI),
    /* pfnConstruct */
    drvACPIConstruct,
    /* pfnDestruct */
    drvACPIDestruct,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    NULL,
    /* pfnResume */
    NULL,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    NULL,
    /* pfnSoftReset */
    NULL,
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};
