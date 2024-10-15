/* $Id: iokit.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Main - Darwin IOKit Routines.
 *
 * Because IOKit makes use of COM like interfaces, it does not mix very
 * well with COM/XPCOM and must therefore be isolated from it using a
 * simpler C interface.
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
#define LOG_GROUP LOG_GROUP_MAIN
#ifdef STANDALONE_TESTCASE
# define VBOX_WITH_USB
#endif

#include <mach/mach.h>
#include <Carbon/Carbon.h>
#include <CoreFoundation/CFBase.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOBSD.h>
#include <IOKit/storage/IOStorageDeviceCharacteristics.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <IOKit/storage/IOMedia.h>
#include <IOKit/storage/IOCDMedia.h>
#include <IOKit/scsi/SCSITaskLib.h>
#include <SystemConfiguration/SystemConfiguration.h>
#include <mach/mach_error.h>
#include <sys/param.h>
#include <paths.h>
#ifdef VBOX_WITH_USB
# include <IOKit/usb/IOUSBLib.h>
# include <IOKit/IOCFPlugIn.h>
#endif

#include <VBox/log.h>
#include <VBox/usblib.h>
#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/process.h>
#include <iprt/assert.h>
#include <iprt/system.h>
#include <iprt/thread.h>
#include <iprt/uuid.h>
#ifdef STANDALONE_TESTCASE
# include <iprt/initterm.h>
# include <iprt/stream.h>
#endif

#include "iokit.h"

/* A small hack... */
#ifdef STANDALONE_TESTCASE
# define DarwinFreeUSBDeviceFromIOKit(a) do { } while (0)
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** An attempt at catching reference leaks. */
#define MY_CHECK_CREFS(cRefs)   do { AssertMsg(cRefs < 25, ("%ld\n", cRefs)); NOREF(cRefs); } while (0)

/** Contains the pid of the current client. If 0, the kernel is the current client. */
#define VBOXUSB_CLIENT_KEY  "VBoxUSB-Client"
/** Contains the pid of the filter owner (i.e. the VBoxSVC pid). */
#define VBOXUSB_OWNER_KEY   "VBoxUSB-Owner"
/** The VBoxUSBDevice class name. */
#define VBOXUSBDEVICE_CLASS_NAME "org_virtualbox_VBoxUSBDevice"

/** Define the constant for the IOUSBHostDevice class name added in El Capitan. */
#ifndef kIOUSBHostDeviceClassName
# define kIOUSBHostDeviceClassName "IOUSBHostDevice"
#endif

/** The major darwin version indicating OS X El Captian, used to take care of the USB changes. */
#define VBOX_OSX_EL_CAPTIAN_VER 15


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The IO Master Port. */
static mach_port_t g_MasterPort = MACH_PORT_NULL;
/** Major darwin version as returned by uname -r. */
static uint32_t g_uMajorDarwin = 0;


/**
 * Lazily opens the master port.
 *
 * @returns true if the port is open, false on failure (very unlikely).
 */
static bool darwinOpenMasterPort(void)
{
    if (!g_MasterPort)
    {
        RT_GCC_NO_WARN_DEPRECATED_BEGIN
        kern_return_t krc = IOMasterPort(MACH_PORT_NULL, &g_MasterPort); /* Deprecated since 12.0. */
        RT_GCC_NO_WARN_DEPRECATED_END
        AssertReturn(krc == KERN_SUCCESS, false);

        /* Get the darwin version we are running on. */
        char szVersion[64];
        int vrc = RTSystemQueryOSInfo(RTSYSOSINFO_RELEASE, &szVersion[0], sizeof(szVersion));
        if (RT_SUCCESS(vrc))
        {
            vrc = RTStrToUInt32Ex(&szVersion[0], NULL, 10, &g_uMajorDarwin);
            AssertLogRelMsg(vrc == VINF_SUCCESS || vrc == VWRN_TRAILING_CHARS,
                            ("Failed to convert the major part of the version string '%s' into an integer: %Rrc\n",
                             szVersion, vrc));
        }
        else
            AssertLogRelMsgFailed(("Failed to query the OS release version with %Rrc\n", vrc));
    }
    return true;
}


/**
 * Checks whether the value exists.
 *
 * @returns true / false accordingly.
 * @param   DictRef     The dictionary.
 * @param   KeyStrRef   The key name.
 */
static bool darwinDictIsPresent(CFDictionaryRef DictRef, CFStringRef KeyStrRef)
{
    return !!CFDictionaryGetValue(DictRef, KeyStrRef);
}


/**
 * Gets a boolean value.
 *
 * @returns Success indicator (true/false).
 * @param   DictRef     The dictionary.
 * @param   KeyStrRef   The key name.
 * @param   pf          Where to store the key value.
 */
static bool darwinDictGetBool(CFDictionaryRef DictRef, CFStringRef KeyStrRef, bool *pf)
{
    CFTypeRef BoolRef = CFDictionaryGetValue(DictRef, KeyStrRef);
    if (    BoolRef
        &&  CFGetTypeID(BoolRef) == CFBooleanGetTypeID())
    {
        *pf = CFBooleanGetValue((CFBooleanRef)BoolRef);
        return true;
    }
    *pf = false;
    return false;
}


/**
 * Gets an unsigned 8-bit integer value.
 *
 * @returns Success indicator (true/false).
 * @param   DictRef     The dictionary.
 * @param   KeyStrRef   The key name.
 * @param   pu8         Where to store the key value.
 */
static bool darwinDictGetU8(CFDictionaryRef DictRef, CFStringRef KeyStrRef, uint8_t *pu8)
{
    CFTypeRef ValRef = CFDictionaryGetValue(DictRef, KeyStrRef);
    if (ValRef)
    {
        if (CFNumberGetValue((CFNumberRef)ValRef, kCFNumberSInt8Type, pu8))
            return true;
    }
    *pu8 = 0;
    return false;
}


/**
 * Gets an unsigned 16-bit integer value.
 *
 * @returns Success indicator (true/false).
 * @param   DictRef     The dictionary.
 * @param   KeyStrRef   The key name.
 * @param   pu16        Where to store the key value.
 */
static bool darwinDictGetU16(CFDictionaryRef DictRef, CFStringRef KeyStrRef, uint16_t *pu16)
{
    CFTypeRef ValRef = CFDictionaryGetValue(DictRef, KeyStrRef);
    if (ValRef)
    {
        if (CFNumberGetValue((CFNumberRef)ValRef, kCFNumberSInt16Type, pu16))
            return true;
    }
    *pu16 = 0;
    return false;
}


/**
 * Gets an unsigned 32-bit integer value.
 *
 * @returns Success indicator (true/false).
 * @param   DictRef     The dictionary.
 * @param   KeyStrRef   The key name.
 * @param   pu32        Where to store the key value.
 */
static bool darwinDictGetU32(CFDictionaryRef DictRef, CFStringRef KeyStrRef, uint32_t *pu32)
{
    CFTypeRef ValRef = CFDictionaryGetValue(DictRef, KeyStrRef);
    if (ValRef)
    {
        if (CFNumberGetValue((CFNumberRef)ValRef, kCFNumberSInt32Type, pu32))
            return true;
    }
    *pu32 = 0;
    return false;
}


/**
 * Gets an unsigned 64-bit integer value.
 *
 * @returns Success indicator (true/false).
 * @param   DictRef     The dictionary.
 * @param   KeyStrRef   The key name.
 * @param   pu64        Where to store the key value.
 */
static bool darwinDictGetU64(CFDictionaryRef DictRef, CFStringRef KeyStrRef, uint64_t *pu64)
{
    CFTypeRef ValRef = CFDictionaryGetValue(DictRef, KeyStrRef);
    if (ValRef)
    {
        if (CFNumberGetValue((CFNumberRef)ValRef, kCFNumberSInt64Type, pu64))
            return true;
    }
    *pu64 = 0;
    return false;
}


/**
 * Gets a RTPROCESS value.
 *
 * @returns Success indicator (true/false).
 * @param   DictRef     The dictionary.
 * @param   KeyStrRef   The key name.
 * @param   pProcess    Where to store the key value.
 */
static bool darwinDictGetProcess(CFMutableDictionaryRef DictRef, CFStringRef KeyStrRef, PRTPROCESS pProcess)
{
    switch (sizeof(*pProcess))
    {
        case sizeof(uint16_t):  return darwinDictGetU16(DictRef, KeyStrRef, (uint16_t *)pProcess);
        case sizeof(uint32_t):  return darwinDictGetU32(DictRef, KeyStrRef, (uint32_t *)pProcess);
        case sizeof(uint64_t):  return darwinDictGetU64(DictRef, KeyStrRef, (uint64_t *)pProcess);
        default:
            AssertMsgFailedReturn(("%d\n", sizeof(*pProcess)), false);
    }
}


/**
 * Gets string value, converted to UTF-8 and put in user buffer.
 *
 * @returns Success indicator (true/false).
 * @param   DictRef     The dictionary.
 * @param   KeyStrRef   The key name.
 * @param   psz         The string buffer. On failure this will be an empty string ("").
 * @param   cch         The size of the buffer.
 */
static bool darwinDictGetString(CFDictionaryRef DictRef, CFStringRef KeyStrRef, char *psz, size_t cch)
{
    CFTypeRef ValRef = CFDictionaryGetValue(DictRef, KeyStrRef);
    if (ValRef)
    {
        if (CFStringGetCString((CFStringRef)ValRef, psz, (CFIndex)cch, kCFStringEncodingUTF8))
            return true;
    }
    Assert(cch > 0);
    *psz = '\0';
    return false;
}


/**
 * Gets string value, converted to UTF-8 and put in a IPRT string buffer.
 *
 * @returns Success indicator (true/false).
 * @param   DictRef     The dictionary.
 * @param   KeyStrRef   The key name.
 * @param   ppsz        Where to store the key value. Free with RTStrFree. Set to NULL on failure.
 */
static bool darwinDictDupString(CFDictionaryRef DictRef, CFStringRef KeyStrRef, char **ppsz)
{
    char szBuf[512];
    if (darwinDictGetString(DictRef, KeyStrRef, szBuf, sizeof(szBuf)))
    {
        USBLibPurgeEncoding(szBuf);
        *ppsz = RTStrDup(szBuf);
        if (*ppsz)
            return true;
    }
    *ppsz = NULL;
    return false;
}


/**
 * Gets a byte string (data) of a specific size.
 *
 * @returns Success indicator (true/false).
 * @param   DictRef     The dictionary.
 * @param   KeyStrRef   The key name.
 * @param   pvBuf       The buffer to store the bytes in.
 * @param   cbBuf       The size of the buffer. This must exactly match the data size.
 */
static bool darwinDictGetData(CFDictionaryRef DictRef, CFStringRef KeyStrRef, void *pvBuf, size_t cbBuf)
{
    CFTypeRef ValRef = CFDictionaryGetValue(DictRef, KeyStrRef);
    if (ValRef)
    {
        CFIndex cbActual = CFDataGetLength((CFDataRef)ValRef);
        if (cbActual >= 0 && cbBuf == (size_t)cbActual)
        {
            CFDataGetBytes((CFDataRef)ValRef, CFRangeMake(0, (CFIndex)cbBuf), (uint8_t *)pvBuf);
            return true;
        }
    }
    memset(pvBuf, '\0', cbBuf);
    return false;
}


#if 1 && !defined(STANDALONE_TESTCASE) /* dumping disabled */
# define DARWIN_IOKIT_LOG(a)         Log(a)
# define DARWIN_IOKIT_LOG_FLUSH()    do {} while (0)
# define DARWIN_IOKIT_DUMP_OBJ(o)    do {} while (0)
#else
# if defined(STANDALONE_TESTCASE)
#  include <iprt/stream.h>
#  define DARWIN_IOKIT_LOG(a)       RTPrintf a
#  define DARWIN_IOKIT_LOG_FLUSH()  RTStrmFlush(g_pStdOut)
# else
#  define DARWIN_IOKIT_LOG(a)       RTLogPrintf a
#  define DARWIN_IOKIT_LOG_FLUSH()  RTLogFlush(NULL)
# endif
# define DARWIN_IOKIT_DUMP_OBJ(o)   darwinDumpObj(o)

/**
 * Callback for dumping a dictionary key.
 *
 * @param   pvKey       The key name.
 * @param   pvValue     The key value
 * @param   pvUser      The recursion depth.
 */
static void darwinDumpDictCallback(const void *pvKey, const void *pvValue, void *pvUser)
{
    /* display the key name. */
    char *pszKey = (char *)RTMemTmpAlloc(1024);
    if (!CFStringGetCString((CFStringRef)pvKey, pszKey, 1024, kCFStringEncodingUTF8))
        strcpy(pszKey, "CFStringGetCString failure");
    DARWIN_IOKIT_LOG(("%+*s%s", (int)(uintptr_t)pvUser, "", pszKey));
    RTMemTmpFree(pszKey);

    /* display the value type */
    CFTypeID Type = CFGetTypeID(pvValue);
    DARWIN_IOKIT_LOG((" [%d-", Type));

    /* display the value */
    if (Type == CFDictionaryGetTypeID())
    {
        DARWIN_IOKIT_LOG(("dictionary] =\n"
                     "%-*s{\n", (int)(uintptr_t)pvUser, ""));
        CFDictionaryApplyFunction((CFDictionaryRef)pvValue, darwinDumpDictCallback, (void *)((uintptr_t)pvUser + 4));
        DARWIN_IOKIT_LOG(("%-*s}\n", (int)(uintptr_t)pvUser, ""));
    }
    else if (Type == CFBooleanGetTypeID())
        DARWIN_IOKIT_LOG(("bool] = %s\n", CFBooleanGetValue((CFBooleanRef)pvValue) ? "true" : "false"));
    else if (Type == CFNumberGetTypeID())
    {
        union
        {
            SInt8 s8;
            SInt16 s16;
            SInt32 s32;
            SInt64 s64;
            Float32 rf32;
            Float64 rd64;
            char ch;
            short s;
            int i;
            long l;
            long long ll;
            float rf;
            double rd;
            CFIndex iCF;
        } u;
        RT_ZERO(u);
        CFNumberType NumType = CFNumberGetType((CFNumberRef)pvValue);
        if (CFNumberGetValue((CFNumberRef)pvValue, NumType, &u))
        {
            switch (CFNumberGetType((CFNumberRef)pvValue))
            {
                case kCFNumberSInt8Type:    DARWIN_IOKIT_LOG(("SInt8] = %RI8 (%#RX8)\n", NumType, u.s8, u.s8)); break;
                case kCFNumberSInt16Type:   DARWIN_IOKIT_LOG(("SInt16] = %RI16 (%#RX16)\n", NumType, u.s16, u.s16)); break;
                case kCFNumberSInt32Type:   DARWIN_IOKIT_LOG(("SInt32] = %RI32 (%#RX32)\n", NumType, u.s32, u.s32)); break;
                case kCFNumberSInt64Type:   DARWIN_IOKIT_LOG(("SInt64] = %RI64 (%#RX64)\n", NumType, u.s64, u.s64)); break;
                case kCFNumberFloat32Type:  DARWIN_IOKIT_LOG(("float32] = %#lx\n", NumType, u.l)); break;
                case kCFNumberFloat64Type:  DARWIN_IOKIT_LOG(("float64] = %#llx\n", NumType, u.ll)); break;
                case kCFNumberFloatType:    DARWIN_IOKIT_LOG(("float] = %#lx\n", NumType, u.l)); break;
                case kCFNumberDoubleType:   DARWIN_IOKIT_LOG(("double] = %#llx\n", NumType, u.ll)); break;
                case kCFNumberCharType:     DARWIN_IOKIT_LOG(("char] = %hhd (%hhx)\n", NumType, u.ch, u.ch)); break;
                case kCFNumberShortType:    DARWIN_IOKIT_LOG(("short] = %hd (%hx)\n", NumType, u.s, u.s)); break;
                case kCFNumberIntType:      DARWIN_IOKIT_LOG(("int] = %d (%#x)\n", NumType, u.i, u.i)); break;
                case kCFNumberLongType:     DARWIN_IOKIT_LOG(("long] = %ld (%#lx)\n", NumType, u.l, u.l)); break;
                case kCFNumberLongLongType: DARWIN_IOKIT_LOG(("long long] = %lld (%#llx)\n", NumType, u.ll, u.ll)); break;
                case kCFNumberCFIndexType:  DARWIN_IOKIT_LOG(("CFIndex] = %lld (%#llx)\n", NumType, (long long)u.iCF, (long long)u.iCF)); break;
                    break;
                default:                    DARWIN_IOKIT_LOG(("%d?] = %lld (%llx)\n", NumType, u.ll, u.ll)); break;
            }
        }
        else
            DARWIN_IOKIT_LOG(("number] = CFNumberGetValue failed\n"));
    }
    else if (Type == CFBooleanGetTypeID())
        DARWIN_IOKIT_LOG(("boolean] = %RTbool\n", CFBooleanGetValue((CFBooleanRef)pvValue)));
    else if (Type == CFStringGetTypeID())
    {
        DARWIN_IOKIT_LOG(("string] = "));
        char *pszValue = (char *)RTMemTmpAlloc(16*_1K);
        if (!CFStringGetCString((CFStringRef)pvValue, pszValue, 16*_1K, kCFStringEncodingUTF8))
            strcpy(pszValue, "CFStringGetCString failure");
        DARWIN_IOKIT_LOG(("\"%s\"\n", pszValue));
        RTMemTmpFree(pszValue);
    }
    else if (Type == CFDataGetTypeID())
    {
        CFIndex cb = CFDataGetLength((CFDataRef)pvValue);
        DARWIN_IOKIT_LOG(("%zu bytes] =", (size_t)cb));
        void *pvData = RTMemTmpAlloc(cb + 8);
        CFDataGetBytes((CFDataRef)pvValue, CFRangeMake(0, cb), (uint8_t *)pvData);
        if (!cb)
            DARWIN_IOKIT_LOG((" \n"));
        else if (cb <= 32)
            DARWIN_IOKIT_LOG((" %.*Rhxs\n", cb, pvData));
        else
            DARWIN_IOKIT_LOG(("\n%.*Rhxd\n", cb, pvData));
        RTMemTmpFree(pvData);
    }
    else
        DARWIN_IOKIT_LOG(("??] = %p\n", pvValue));
}


/**
 * Dumps a dictionary to the log.
 *
 * @param   DictRef     The dictionary to dump.
 */
static void darwinDumpDict(CFDictionaryRef DictRef, unsigned cIndents)
{
    CFDictionaryApplyFunction(DictRef, darwinDumpDictCallback, (void *)(uintptr_t)cIndents);
    DARWIN_IOKIT_LOG_FLUSH();
}


/**
 * Dumps an I/O kit registry object and all it children.
 * @param   Object      The object to dump.
 * @param   cIndents    The number of indents to use.
 */
static void darwinDumpObjInt(io_object_t Object, unsigned cIndents)
{
    static io_string_t s_szPath;
    kern_return_t krc = IORegistryEntryGetPath(Object, kIOServicePlane, s_szPath);
    if (krc != KERN_SUCCESS)
        strcpy(s_szPath, "IORegistryEntryGetPath failed");
    DARWIN_IOKIT_LOG(("Dumping %p - %s:\n", (const void *)Object, s_szPath));

    CFMutableDictionaryRef PropsRef = 0;
    krc = IORegistryEntryCreateCFProperties(Object, &PropsRef, kCFAllocatorDefault, kNilOptions);
    if (krc == KERN_SUCCESS)
    {
        darwinDumpDict(PropsRef, cIndents + 4);
        CFRelease(PropsRef);
    }

    /*
     * Children.
     */
    io_iterator_t Children;
    krc = IORegistryEntryGetChildIterator(Object, kIOServicePlane, &Children);
    if (krc == KERN_SUCCESS)
    {
        io_object_t Child;
        while ((Child = IOIteratorNext(Children)) != IO_OBJECT_NULL)
        {
            darwinDumpObjInt(Child, cIndents + 4);
            IOObjectRelease(Child);
        }
        IOObjectRelease(Children);
    }
    else
        DARWIN_IOKIT_LOG(("IORegistryEntryGetChildIterator -> %#x\n", krc));
}

/**
 * Dumps an I/O kit registry object and all it children.
 * @param   Object      The object to dump.
 */
static void darwinDumpObj(io_object_t Object)
{
    darwinDumpObjInt(Object, 0);
}

#endif /* helpers for dumping registry dictionaries */


#ifdef VBOX_WITH_USB

/**
 * Notification data created by DarwinSubscribeUSBNotifications, used by
 * the callbacks and finally freed by DarwinUnsubscribeUSBNotifications.
 */
typedef struct DARWINUSBNOTIFY
{
    /** The notification port.
     * It's shared between the notification callbacks. */
    IONotificationPortRef NotifyPort;
    /** The run loop source for NotifyPort. */
    CFRunLoopSourceRef NotifyRLSrc;
    /** The attach notification iterator. */
    io_iterator_t AttachIterator;
    /** The 2nd attach notification iterator. */
    io_iterator_t AttachIterator2;
    /** The detach notification iterator. */
    io_iterator_t DetachIterator;
} DARWINUSBNOTIFY, *PDARWINUSBNOTIFY;


/**
 * Run thru an iterator.
 *
 * The docs says this is necessary to start getting notifications,
 * so this function is called in the callbacks and right after
 * registering the notification.
 *
 * @param   pIterator   The iterator reference.
 */
static void darwinDrainIterator(io_iterator_t pIterator)
{
    io_object_t Object;
    while ((Object = IOIteratorNext(pIterator)) != IO_OBJECT_NULL)
    {
        DARWIN_IOKIT_DUMP_OBJ(Object);
        IOObjectRelease(Object);
    }
}


/**
 * Callback for the 1st attach notification.
 *
 * @param   pvNotify        Our data.
 * @param   NotifyIterator  The notification iterator.
 */
static void darwinUSBAttachNotification1(void *pvNotify, io_iterator_t NotifyIterator)
{
    DARWIN_IOKIT_LOG(("USB Attach Notification1\n"));
    NOREF(pvNotify); //PDARWINUSBNOTIFY pNotify = (PDARWINUSBNOTIFY)pvNotify;
    darwinDrainIterator(NotifyIterator);
}


/**
 * Callback for the 2nd attach notification.
 *
 * @param   pvNotify        Our data.
 * @param   NotifyIterator  The notification iterator.
 */
static void darwinUSBAttachNotification2(void *pvNotify, io_iterator_t NotifyIterator)
{
    DARWIN_IOKIT_LOG(("USB Attach Notification2\n"));
    NOREF(pvNotify); //PDARWINUSBNOTIFY pNotify = (PDARWINUSBNOTIFY)pvNotify;
    darwinDrainIterator(NotifyIterator);
}


/**
 * Callback for the detach notifications.
 *
 * @param   pvNotify        Our data.
 * @param   NotifyIterator  The notification iterator.
 */
static void darwinUSBDetachNotification(void *pvNotify, io_iterator_t NotifyIterator)
{
    DARWIN_IOKIT_LOG(("USB Detach Notification\n"));
    NOREF(pvNotify); //PDARWINUSBNOTIFY pNotify = (PDARWINUSBNOTIFY)pvNotify;
    darwinDrainIterator(NotifyIterator);
}


/**
 * Subscribes the run loop to USB notification events relevant to
 * device attach/detach.
 *
 * The source mode for these events is defined as VBOX_IOKIT_MODE_STRING
 * so that the caller can listen to events from this mode only and
 * re-evalutate the list of attached devices whenever an event arrives.
 *
 * @returns opaque for passing to the unsubscribe function. If NULL
 *          something unexpectedly failed during subscription.
 */
void *DarwinSubscribeUSBNotifications(void)
{
    AssertReturn(darwinOpenMasterPort(), NULL);

    PDARWINUSBNOTIFY pNotify = (PDARWINUSBNOTIFY)RTMemAllocZ(sizeof(*pNotify));
    AssertReturn(pNotify, NULL);

    /*
     * Create the notification port, bake it into a runloop source which we
     * then add to our run loop.
     */
    pNotify->NotifyPort = IONotificationPortCreate(g_MasterPort);
    Assert(pNotify->NotifyPort);
    if (pNotify->NotifyPort)
    {
        pNotify->NotifyRLSrc = IONotificationPortGetRunLoopSource(pNotify->NotifyPort);
        Assert(pNotify->NotifyRLSrc);
        if (pNotify->NotifyRLSrc)
        {
            CFRunLoopRef RunLoopRef = CFRunLoopGetCurrent();
            CFRetain(RunLoopRef); /* Workaround for crash when cleaning up the TLS / runloop((sub)mode). See @bugref{2807}. */
            CFRunLoopAddSource(RunLoopRef, pNotify->NotifyRLSrc, CFSTR(VBOX_IOKIT_MODE_STRING));

            /*
             * Create the notification callbacks.
             */
            kern_return_t krc = IOServiceAddMatchingNotification(pNotify->NotifyPort,
                                                                 kIOPublishNotification,
                                                                 IOServiceMatching(kIOUSBDeviceClassName),
                                                                 darwinUSBAttachNotification1,
                                                                 pNotify,
                                                                 &pNotify->AttachIterator);
            if (krc == KERN_SUCCESS)
            {
                darwinDrainIterator(pNotify->AttachIterator);
                krc = IOServiceAddMatchingNotification(pNotify->NotifyPort,
                                                       kIOMatchedNotification,
                                                       IOServiceMatching(kIOUSBDeviceClassName),
                                                       darwinUSBAttachNotification2,
                                                       pNotify,
                                                       &pNotify->AttachIterator2);
                if (krc == KERN_SUCCESS)
                {
                    darwinDrainIterator(pNotify->AttachIterator2);
                    krc = IOServiceAddMatchingNotification(pNotify->NotifyPort,
                                                           kIOTerminatedNotification,
                                                           IOServiceMatching(kIOUSBDeviceClassName),
                                                           darwinUSBDetachNotification,
                                                           pNotify,
                                                           &pNotify->DetachIterator);
                    if (krc == KERN_SUCCESS)
                    {
                        darwinDrainIterator(pNotify->DetachIterator);
                        return pNotify;
                    }
                    IOObjectRelease(pNotify->AttachIterator2);
                }
                IOObjectRelease(pNotify->AttachIterator);
            }
            CFRunLoopRemoveSource(RunLoopRef, pNotify->NotifyRLSrc, CFSTR(VBOX_IOKIT_MODE_STRING));
        }
        IONotificationPortDestroy(pNotify->NotifyPort);
    }

    RTMemFree(pNotify);
    return NULL;
}


/**
 * Unsubscribe the run loop from USB notification subscribed to
 * by DarwinSubscribeUSBNotifications.
 *
 * @param   pvOpaque    The return value from DarwinSubscribeUSBNotifications.
 */
void DarwinUnsubscribeUSBNotifications(void *pvOpaque)
{
    PDARWINUSBNOTIFY pNotify = (PDARWINUSBNOTIFY)pvOpaque;
    if (!pNotify)
        return;

    IOObjectRelease(pNotify->AttachIterator);
    pNotify->AttachIterator = IO_OBJECT_NULL;
    IOObjectRelease(pNotify->AttachIterator2);
    pNotify->AttachIterator2 = IO_OBJECT_NULL;
    IOObjectRelease(pNotify->DetachIterator);
    pNotify->DetachIterator = IO_OBJECT_NULL;

    CFRunLoopRemoveSource(CFRunLoopGetCurrent(), pNotify->NotifyRLSrc, CFSTR(VBOX_IOKIT_MODE_STRING));
    IONotificationPortDestroy(pNotify->NotifyPort);
    pNotify->NotifyRLSrc = NULL;
    pNotify->NotifyPort = NULL;

    RTMemFree(pNotify);
}


/**
 * Descends recursively into a IORegistry tree locating the first object of a given class.
 *
 * The search is performed depth first.
 *
 * @returns Object reference if found, NULL if not.
 * @param   Object      The current tree root.
 * @param   pszClass    The name of the class we're looking for.
 * @param   pszNameBuf  A scratch buffer for query the class name in to avoid
 *                      wasting 128 bytes on an io_name_t object for every recursion.
 */
static io_object_t darwinFindObjectByClass(io_object_t Object, const char *pszClass, io_name_t pszNameBuf)
{
    io_iterator_t Children;
    kern_return_t krc = IORegistryEntryGetChildIterator(Object, kIOServicePlane, &Children);
    if (krc != KERN_SUCCESS)
        return IO_OBJECT_NULL;
    io_object_t Child;
    while ((Child = IOIteratorNext(Children)) != IO_OBJECT_NULL)
    {
        krc = IOObjectGetClass(Child, pszNameBuf);
        if (    krc == KERN_SUCCESS
            &&  !strcmp(pszNameBuf, pszClass))
            break;

        io_object_t GrandChild = darwinFindObjectByClass(Child, pszClass, pszNameBuf);
        IOObjectRelease(Child);
        if (GrandChild)
        {
            Child = GrandChild;
            break;
        }
    }
    IOObjectRelease(Children);
    return Child;
}


/**
 * Descends recursively into IOUSBMassStorageClass tree to check whether
 * the MSD is mounted or not.
 *
 * The current heuristic is to look for the IOMedia class.
 *
 * @returns true if mounted, false if not.
 * @param   MSDObj      The IOUSBMassStorageClass object.
 * @param   pszNameBuf  A scratch buffer for query the class name in to avoid
 *                      wasting 128 bytes on an io_name_t object for every recursion.
 */
static bool darwinIsMassStorageInterfaceInUse(io_object_t MSDObj, io_name_t pszNameBuf)
{
    io_object_t MediaObj = darwinFindObjectByClass(MSDObj, kIOMediaClass, pszNameBuf);
    if (MediaObj)
    {
        CFMutableDictionaryRef pProperties;
        kern_return_t krc;
        bool fInUse = true;

        krc = IORegistryEntryCreateCFProperties(MediaObj, &pProperties, kCFAllocatorDefault, kNilOptions);
        if (krc == KERN_SUCCESS)
        {
            CFBooleanRef pBoolValue = (CFBooleanRef)CFDictionaryGetValue(pProperties, CFSTR(kIOMediaOpenKey));
            if (pBoolValue)
                fInUse = CFBooleanGetValue(pBoolValue);

            CFRelease(pProperties);
        }

        /* more checks? */
        IOObjectRelease(MediaObj);
        return fInUse;
    }

    return false;
}


/**
 * Finds the matching IOUSBHostDevice registry entry for the given legacy USB device interface (IOUSBDevice).
 *
 * @returns kern_return_t error code.
 * @param   USBDeviceLegacy    The legacy device I/O Kit object.
 * @param   pUSBDevice         Where to store the IOUSBHostDevice object on success.
 */
static kern_return_t darwinGetUSBHostDeviceFromLegacyDevice(io_object_t USBDeviceLegacy, io_object_t *pUSBDevice)
{
    kern_return_t krc = KERN_SUCCESS;
    uint64_t uIoRegEntryId = 0;

    *pUSBDevice = 0;

    /* Get the registry entry ID to match against. */
    krc = IORegistryEntryGetRegistryEntryID(USBDeviceLegacy, &uIoRegEntryId);
    if (krc != KERN_SUCCESS)
        return krc;

    /*
     * Create a matching dictionary for searching for USB Devices in the IOKit.
     */
    CFMutableDictionaryRef RefMatchingDict = IOServiceMatching(kIOUSBHostDeviceClassName);
    AssertReturn(RefMatchingDict, KERN_FAILURE);

    /*
     * Perform the search and get a collection of USB Device back.
     */
    io_iterator_t USBDevices = IO_OBJECT_NULL;
    IOReturn irc = IOServiceGetMatchingServices(g_MasterPort, RefMatchingDict, &USBDevices);
    AssertMsgReturn(irc == kIOReturnSuccess, ("irc=%d\n", irc), KERN_FAILURE);
    RefMatchingDict = NULL; /* the reference is consumed by IOServiceGetMatchingServices. */

    /*
     * Walk the devices and check for the matching alternate registry entry ID.
     */
    io_object_t USBDevice;
    while ((USBDevice = IOIteratorNext(USBDevices)) != IO_OBJECT_NULL)
    {
        DARWIN_IOKIT_DUMP_OBJ(USBDevice);

        CFMutableDictionaryRef PropsRef = 0;
        krc = IORegistryEntryCreateCFProperties(USBDevice, &PropsRef, kCFAllocatorDefault, kNilOptions);
        if (krc == KERN_SUCCESS)
        {
            uint64_t uAltRegId = 0;
            if (   darwinDictGetU64(PropsRef,  CFSTR("AppleUSBAlternateServiceRegistryID"), &uAltRegId)
                && uAltRegId == uIoRegEntryId)
            {
                *pUSBDevice = USBDevice;
                CFRelease(PropsRef);
                break;
            }

            CFRelease(PropsRef);
        }
        IOObjectRelease(USBDevice);
    }
    IOObjectRelease(USBDevices);

    return krc;
}


static bool darwinUSBDeviceIsGrabbedDetermineState(PUSBDEVICE pCur, io_object_t USBDevice)
{
    /*
     * Iterate the interfaces (among the children of the IOUSBDevice object).
     */
    io_iterator_t Interfaces;
    kern_return_t krc = IORegistryEntryGetChildIterator(USBDevice, kIOServicePlane, &Interfaces);
    if (krc != KERN_SUCCESS)
        return false;

    bool fHaveOwner = false;
    RTPROCESS Owner = NIL_RTPROCESS;
    bool fHaveClient = false;
    RTPROCESS Client = NIL_RTPROCESS;
    io_object_t Interface;
    while ((Interface = IOIteratorNext(Interfaces)) != IO_OBJECT_NULL)
    {
        io_name_t szName;
        krc = IOObjectGetClass(Interface, szName);
        if (    krc == KERN_SUCCESS
            &&  !strcmp(szName, VBOXUSBDEVICE_CLASS_NAME))
        {
            CFMutableDictionaryRef PropsRef = 0;
            krc = IORegistryEntryCreateCFProperties(Interface, &PropsRef, kCFAllocatorDefault, kNilOptions);
            if (krc == KERN_SUCCESS)
            {
                fHaveOwner = darwinDictGetProcess(PropsRef, CFSTR(VBOXUSB_OWNER_KEY), &Owner);
                fHaveClient = darwinDictGetProcess(PropsRef, CFSTR(VBOXUSB_CLIENT_KEY), &Client);
                CFRelease(PropsRef);
            }
        }

        IOObjectRelease(Interface);
    }
    IOObjectRelease(Interfaces);

    /*
     * Calc the status.
     */
    if (fHaveOwner)
    {
        if (Owner == RTProcSelf())
            pCur->enmState = !fHaveClient || Client == NIL_RTPROCESS || !Client
                           ? USBDEVICESTATE_HELD_BY_PROXY
                           : USBDEVICESTATE_USED_BY_GUEST;
        else
            pCur->enmState = USBDEVICESTATE_USED_BY_HOST;
    }

    return fHaveOwner;
}


/**
 * Worker for determining the USB device state for devices which are not captured by the VBoxUSB driver
 * Works for both, IOUSBDevice (legacy on release >= El Capitan) and IOUSBHostDevice (available on >= El Capitan).
 *
 * @param   pCur      The USB device data.
 * @param   USBDevice I/O Kit USB device object (either IOUSBDevice or IOUSBHostDevice).
 */
static void darwinDetermineUSBDeviceStateWorker(PUSBDEVICE pCur, io_object_t USBDevice)
{
    /*
     * Iterate the interfaces (among the children of the IOUSBDevice object).
     */
    io_iterator_t Interfaces;
    kern_return_t krc = IORegistryEntryGetChildIterator(USBDevice, kIOServicePlane, &Interfaces);
    if (krc != KERN_SUCCESS)
        return;

    bool fUserClientOnly = true; RT_NOREF(fUserClientOnly); /* Shut up Clang 13 (-Wunused-but-set-variable). */
    bool fConfigured = false;    RT_NOREF(fConfigured);
    bool fInUse = false;
    bool fSeizable = true;
    io_object_t Interface;
    while ((Interface = IOIteratorNext(Interfaces)) != IO_OBJECT_NULL)
    {
        io_name_t szName;
        krc = IOObjectGetClass(Interface, szName);
        if (    krc == KERN_SUCCESS
            &&  (   !strcmp(szName, "IOUSBInterface")
                 || !strcmp(szName, "IOUSBHostInterface")))
        {
            fConfigured = true;

            /*
             * Iterate the interface children looking for stuff other than
             * IOUSBUserClientInit objects.
             */
            io_iterator_t Children1;
            krc = IORegistryEntryGetChildIterator(Interface, kIOServicePlane, &Children1);
            if (krc == KERN_SUCCESS)
            {
                io_object_t Child1;
                while ((Child1 = IOIteratorNext(Children1)) != IO_OBJECT_NULL)
                {
                    krc = IOObjectGetClass(Child1, szName);
                    if (    krc == KERN_SUCCESS
                        &&  strcmp(szName, "IOUSBUserClientInit"))
                    {
                        fUserClientOnly = false;

                        if (   !strcmp(szName, "IOUSBMassStorageClass")
                            || !strcmp(szName, "IOUSBMassStorageInterfaceNub"))
                        {
                            /* Only permit capturing MSDs that aren't mounted, at least
                               until the GUI starts poping up warnings about data loss
                               and such when capturing a busy device. */
                            fSeizable = false;
                            fInUse |= darwinIsMassStorageInterfaceInUse(Child1, szName);
                        }
                        else if (!strcmp(szName, "IOUSBHIDDriver")
                              || !strcmp(szName, "AppleHIDMouse")
                              /** @todo more? */)
                        {
                            /* For now, just assume that all HID devices are inaccessible
                               because of the greedy HID service. */
                            fSeizable = false;
                            fInUse = true;
                        }
                        else
                            fInUse = true;
                    }
                    IOObjectRelease(Child1);
                }
                IOObjectRelease(Children1);
            }
        }

        IOObjectRelease(Interface);
    }
    IOObjectRelease(Interfaces);

    /*
     * Calc the status.
     */
    if (!fInUse)
        pCur->enmState = USBDEVICESTATE_UNUSED;
    else
        pCur->enmState = fSeizable
                       ? USBDEVICESTATE_USED_BY_HOST_CAPTURABLE
                       : USBDEVICESTATE_USED_BY_HOST;
}


/**
 * Worker function for DarwinGetUSBDevices() that tries to figure out
 * what state the device is in and set enmState.
 *
 * This is mostly a matter of distinguishing between devices that nobody
 * uses, devices that can be seized and devices that cannot be grabbed.
 *
 * @param   pCur        The USB device data.
 * @param   USBDevice   The USB device object.
 * @param   PropsRef    The USB device properties.
 */
static void darwinDeterminUSBDeviceState(PUSBDEVICE pCur, io_object_t USBDevice, CFMutableDictionaryRef /* PropsRef */)
{

    if (!darwinUSBDeviceIsGrabbedDetermineState(pCur, USBDevice))
    {
        /*
         * The USB stack was completely reworked on El Capitan and the IOUSBDevice and IOUSBInterface
         * are deprecated and don't return the information required for the additional checks below.
         * We also can't directly make use of the new classes (IOUSBHostDevice and IOUSBHostInterface)
         * because VBoxUSB only exposes the legacy interfaces. Trying to use the new classes results in errors
         * because the I/O Kit USB library wants to use the new interfaces. The result is us losing the device
         * form the list when VBoxUSB has attached to the USB device.
         *
         * To make the checks below work we have to get hold of the IOUSBHostDevice and IOUSBHostInterface
         * instances for the current device. Fortunately the IOUSBHostDevice instance contains a
         * "AppleUSBAlternateServiceRegistryID" which points to the legacy class instance for the same device.
         * So just iterate over the list of IOUSBHostDevice instances and check whether the
         * AppleUSBAlternateServiceRegistryID property matches with the legacy instance.
         *
         * The upside is that we can keep VBoxUSB untouched and still compatible with older OS X releases.
         */
        if (g_uMajorDarwin >= VBOX_OSX_EL_CAPTIAN_VER)
        {
            io_object_t IOUSBDeviceNew = IO_OBJECT_NULL;
            kern_return_t krc = darwinGetUSBHostDeviceFromLegacyDevice(USBDevice, &IOUSBDeviceNew);
            if (   krc == KERN_SUCCESS
                && IOUSBDeviceNew != IO_OBJECT_NULL)
            {
                darwinDetermineUSBDeviceStateWorker(pCur, IOUSBDeviceNew);
                IOObjectRelease(IOUSBDeviceNew);
            }
        }
        else
            darwinDetermineUSBDeviceStateWorker(pCur, USBDevice);
    }
}


/**
 * Enumerate the USB devices returning a FIFO of them.
 *
 * @returns Pointer to the head.
 *          USBProxyService::freeDevice is expected to free each of the list elements.
 */
PUSBDEVICE DarwinGetUSBDevices(void)
{
    AssertReturn(darwinOpenMasterPort(), NULL);
    //DARWIN_IOKIT_LOG(("DarwinGetUSBDevices\n"));

    /*
     * Create a matching dictionary for searching for USB Devices in the IOKit.
     */
    CFMutableDictionaryRef RefMatchingDict = IOServiceMatching(kIOUSBDeviceClassName);
    AssertReturn(RefMatchingDict, NULL);

    /*
     * Perform the search and get a collection of USB Device back.
     */
    io_iterator_t USBDevices = IO_OBJECT_NULL;
    IOReturn irc = IOServiceGetMatchingServices(g_MasterPort, RefMatchingDict, &USBDevices);
    AssertMsgReturn(irc == kIOReturnSuccess, ("irc=%d\n", irc), NULL);
    RefMatchingDict = NULL; /* the reference is consumed by IOServiceGetMatchingServices. */

    /*
     * Enumerate the USB Devices.
     */
    PUSBDEVICE pHead = NULL;
    PUSBDEVICE pTail = NULL;
    io_object_t USBDevice;
    while ((USBDevice = IOIteratorNext(USBDevices)) != IO_OBJECT_NULL)
    {
        DARWIN_IOKIT_DUMP_OBJ(USBDevice);

        /*
         * Query the device properties from the registry.
         *
         * We could alternatively use the device and such, but that will be
         * slower and we would have to resort to the registry for the three
         * string anyway.
         */
        CFMutableDictionaryRef PropsRef = 0;
        kern_return_t krc = IORegistryEntryCreateCFProperties(USBDevice, &PropsRef, kCFAllocatorDefault, kNilOptions);
        if (krc == KERN_SUCCESS)
        {
            bool fOk = false;
            PUSBDEVICE pCur = (PUSBDEVICE)RTMemAllocZ(sizeof(*pCur));
            do /* loop for breaking out of on failure. */
            {
                AssertBreak(pCur);

                /*
                 * Mandatory
                 */
                pCur->bcdUSB = 0;                                           /* we've no idea. */
                pCur->enmState = USBDEVICESTATE_USED_BY_HOST_CAPTURABLE;    /* just a default, we'll try harder in a bit. */

                /* Skip hubs. On 10.11 beta 3, the root hub simulations does not have a USBDeviceClass property, so
                   simply ignore failures to retrieve it. */
                if (!darwinDictGetU8(PropsRef,         CFSTR(kUSBDeviceClass),          &pCur->bDeviceClass))
                {
#ifdef VBOX_STRICT
                    char szTmp[80];
                    Assert(   darwinDictGetString(PropsRef, CFSTR("IOClassNameOverride"), szTmp, sizeof(szTmp))
                           && strcmp(szTmp, "IOUSBRootHubDevice") == 0);
#endif
                    break;
                }
                if (pCur->bDeviceClass == 0x09 /* hub, find a define! */)
                    break;
                AssertBreak(darwinDictGetU8(PropsRef,  CFSTR(kUSBDeviceSubClass),       &pCur->bDeviceSubClass));
                AssertBreak(darwinDictGetU8(PropsRef,  CFSTR(kUSBDeviceProtocol),       &pCur->bDeviceProtocol));
                AssertBreak(darwinDictGetU16(PropsRef, CFSTR(kUSBVendorID),             &pCur->idVendor));
                AssertBreak(darwinDictGetU16(PropsRef, CFSTR(kUSBProductID),            &pCur->idProduct));
                AssertBreak(darwinDictGetU16(PropsRef, CFSTR(kUSBDeviceReleaseNumber),  &pCur->bcdDevice));
                uint32_t u32LocationId;
                AssertBreak(darwinDictGetU32(PropsRef, CFSTR(kUSBDevicePropertyLocationID), &u32LocationId));
                uint64_t u64SessionId;
                AssertBreak(darwinDictGetU64(PropsRef, CFSTR("sessionID"), &u64SessionId));
                char szAddress[64];
                RTStrPrintf(szAddress, sizeof(szAddress), "p=0x%04RX16;v=0x%04RX16;s=0x%016RX64;l=0x%08RX32",
                            pCur->idProduct, pCur->idVendor, u64SessionId, u32LocationId);
                pCur->pszAddress = RTStrDup(szAddress);
                AssertBreak(pCur->pszAddress);
                pCur->bBus = u32LocationId >> 24;
                darwinDictGetU8(PropsRef, CFSTR("PortNum"), &pCur->bPort); /* Not present in 10.11 beta 3, so ignore failure. (Is set to zero.) */
                uint8_t bSpeed;
                AssertBreak(darwinDictGetU8(PropsRef, CFSTR(kUSBDevicePropertySpeed), &bSpeed));
                Assert(bSpeed <= 4);
                pCur->enmSpeed = bSpeed == 4 ? USBDEVICESPEED_SUPER
                               : bSpeed == 3 ? USBDEVICESPEED_SUPER
                               : bSpeed == 2 ? USBDEVICESPEED_HIGH
                               : bSpeed == 1 ? USBDEVICESPEED_FULL
                               : bSpeed == 0 ? USBDEVICESPEED_LOW
                                             : USBDEVICESPEED_UNKNOWN;

                /*
                 * Optional.
                 * There are some nameless device in the iMac, apply names to them.
                 */
                darwinDictDupString(PropsRef, CFSTR("USB Vendor Name"),     (char **)&pCur->pszManufacturer);
                if (    !pCur->pszManufacturer
                    &&  pCur->idVendor == kIOUSBVendorIDAppleComputer)
                    pCur->pszManufacturer = RTStrDup("Apple Computer, Inc.");
                darwinDictDupString(PropsRef, CFSTR("USB Product Name"),    (char **)&pCur->pszProduct);
                if (    !pCur->pszProduct
                    &&  pCur->bDeviceClass == 224 /* Wireless */
                    &&  pCur->bDeviceSubClass == 1 /* Radio Frequency */
                    &&  pCur->bDeviceProtocol == 1 /* Bluetooth */)
                    pCur->pszProduct = RTStrDup("Bluetooth");
                darwinDictDupString(PropsRef, CFSTR("USB Serial Number"),   (char **)&pCur->pszSerialNumber);

                pCur->pszBackend = RTStrDup("host");
                AssertBreak(pCur->pszBackend);

#if 0           /* leave the remainder as zero for now. */
                /*
                 * Create a plugin interface for the service and query its USB Device interface.
                 */
                SInt32 Score = 0;
                IOCFPlugInInterface **ppPlugInInterface = NULL;
                irc = IOCreatePlugInInterfaceForService(USBDevice, kIOUSBDeviceUserClientTypeID,
                                                        kIOCFPlugInInterfaceID, &ppPlugInInterface, &Score);
                if (irc == kIOReturnSuccess)
                {
                    IOUSBDeviceInterface245 **ppUSBDevI = NULL;
                    HRESULT hrc = (*ppPlugInInterface)->QueryInterface(ppPlugInInterface,
                                                                       CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID245),
                                                                       (LPVOID *)&ppUSBDevI);
                    irc = IODestroyPlugInInterface(ppPlugInInterface); Assert(irc == kIOReturnSuccess);
                    ppPlugInInterface = NULL;
                    if (hrc == S_OK)
                    {
                        /** @todo enumerate configurations and interfaces if we actually need them. */
                        //IOReturn (*GetNumberOfConfigurations)(void *self, UInt8 *numConfig);
                        //IOReturn (*GetConfigurationDescriptorPtr)(void *self, UInt8 configIndex, IOUSBConfigurationDescriptorPtr *desc);
                        //IOReturn (*CreateInterfaceIterator)(void *self, IOUSBFindInterfaceRequest *req, io_iterator_t *iter);
                    }
                    long cReft = (*ppUSBDeviceInterface)->Release(ppUSBDeviceInterface); MY_CHECK_CREFS(cRefs);
                }
#endif
                /*
                 * Try determine the state.
                 */
                darwinDeterminUSBDeviceState(pCur, USBDevice, PropsRef);

                /*
                 * We're good. Link the device.
                 */
                pCur->pPrev = pTail;
                if (pTail)
                    pTail = pTail->pNext = pCur;
                else
                    pTail = pHead = pCur;
                fOk = true;
            } while (0);

            /* cleanup on failure / skipped device. */
            if (!fOk && pCur)
                DarwinFreeUSBDeviceFromIOKit(pCur);

            CFRelease(PropsRef);
        }
        else
            AssertMsgFailed(("krc=%#x\n", krc));

        IOObjectRelease(USBDevice);
    }

    IOObjectRelease(USBDevices);
    //DARWIN_IOKIT_LOG_FLUSH();

    /*
     * Some post processing. There are a couple of things we have to
     * make 100% sure about, and that is that the (Apple) keyboard
     * and mouse most likely to be in use by the user aren't available
     * for capturing. If there is no Apple mouse or keyboard we'll
     * take the first one from another vendor.
     */
    /* As it turns out, the HID service will take all keyboards and mice
       and we're not currently able to seize them. */
    PUSBDEVICE pMouse = NULL;
    PUSBDEVICE pKeyboard = NULL;
    for (PUSBDEVICE pCur = pHead; pCur; pCur = pCur->pNext)
        if (pCur->idVendor == kIOUSBVendorIDAppleComputer)
        {
            /*
             * This test is a bit rough, should check device class/protocol but
             * we don't have interface info yet so that might be a bit tricky.
             */
            if (    (   !pKeyboard
                     || pKeyboard->idVendor != kIOUSBVendorIDAppleComputer)
                &&  pCur->pszProduct
                &&  strstr(pCur->pszProduct, " Keyboard"))
                pKeyboard = pCur;
            else if (    (   !pMouse
                          || pMouse->idVendor != kIOUSBVendorIDAppleComputer)
                     &&  pCur->pszProduct
                     &&  strstr(pCur->pszProduct, " Mouse")
                )
                pMouse = pCur;
        }
        else if (!pKeyboard || !pMouse)
        {
            if (    pCur->bDeviceClass == 3         /* HID */
                &&  pCur->bDeviceProtocol == 1      /* Keyboard */)
                pKeyboard = pCur;
            else if (   pCur->bDeviceClass == 3     /* HID */
                     && pCur->bDeviceProtocol == 2  /* Mouse */)
                pMouse = pCur;
            /** @todo examin interfaces */
        }

    if (pKeyboard)
        pKeyboard->enmState = USBDEVICESTATE_USED_BY_HOST;
    if (pMouse)
        pMouse->enmState = USBDEVICESTATE_USED_BY_HOST;

    return pHead;
}

#endif /* VBOX_WITH_USB */


/**
 * Enumerate the CD, DVD and BlueRay drives returning a FIFO of device name strings.
 *
 * @returns Pointer to the head.
 *          The caller is responsible for calling RTMemFree() on each of the nodes.
 */
PDARWINDVD DarwinGetDVDDrives(void)
{
    AssertReturn(darwinOpenMasterPort(), NULL);

    /*
     * Create a matching dictionary for searching for CD, DVD and BlueRay services in the IOKit.
     *
     * The idea is to find all the devices which are of class IOCDBlockStorageDevice.
     * CD devices are represented by IOCDBlockStorageDevice class itself, while DVD and BlueRay ones
     * have it as a parent class.
     */
    CFMutableDictionaryRef RefMatchingDict = IOServiceMatching("IOCDBlockStorageDevice");
    AssertReturn(RefMatchingDict, NULL);

    /*
     * Perform the search and get a collection of DVD services.
     */
    io_iterator_t DVDServices = IO_OBJECT_NULL;
    IOReturn irc = IOServiceGetMatchingServices(g_MasterPort, RefMatchingDict, &DVDServices);
    AssertMsgReturn(irc == kIOReturnSuccess, ("irc=%d\n", irc), NULL);
    RefMatchingDict = NULL; /* the reference is consumed by IOServiceGetMatchingServices. */

    /*
     * Enumerate the matching services.
     * (This enumeration must be identical to the one performed in DrvHostBase.cpp.)
     */
    PDARWINDVD pHead = NULL;
    PDARWINDVD pTail = NULL;
    unsigned i = 0;
    io_object_t DVDService;
    while ((DVDService = IOIteratorNext(DVDServices)) != IO_OBJECT_NULL)
    {
        DARWIN_IOKIT_DUMP_OBJ(DVDService);

        /*
         * Get the properties we use to identify the DVD drive.
         *
         * While there is a (weird 12 byte) GUID, it isn't persistent
         * across boots. So, we have to use a combination of the
         * vendor name and product name properties with an optional
         * sequence number for identification.
         */
        CFMutableDictionaryRef PropsRef = 0;
        kern_return_t krc = IORegistryEntryCreateCFProperties(DVDService, &PropsRef, kCFAllocatorDefault, kNilOptions);
        if (krc == KERN_SUCCESS)
        {
            /* Get the Device Characteristics dictionary. */
            CFDictionaryRef DevCharRef = (CFDictionaryRef)CFDictionaryGetValue(PropsRef, CFSTR(kIOPropertyDeviceCharacteristicsKey));
            if (DevCharRef)
            {
                /* The vendor name. */
                char szVendor[128];
                char *pszVendor = &szVendor[0];
                CFTypeRef ValueRef = CFDictionaryGetValue(DevCharRef, CFSTR(kIOPropertyVendorNameKey));
                if (    ValueRef
                    &&  CFGetTypeID(ValueRef) == CFStringGetTypeID()
                    &&  CFStringGetCString((CFStringRef)ValueRef, szVendor, sizeof(szVendor), kCFStringEncodingUTF8))
                    pszVendor = RTStrStrip(szVendor);
                else
                    *pszVendor = '\0';

                /* The product name. */
                char szProduct[128];
                char *pszProduct = &szProduct[0];
                ValueRef = CFDictionaryGetValue(DevCharRef, CFSTR(kIOPropertyProductNameKey));
                if (    ValueRef
                    &&  CFGetTypeID(ValueRef) == CFStringGetTypeID()
                    &&  CFStringGetCString((CFStringRef)ValueRef, szProduct, sizeof(szProduct), kCFStringEncodingUTF8))
                    pszProduct = RTStrStrip(szProduct);
                else
                    *pszProduct = '\0';

                /* Construct the name and check for duplicates. */
                char szName[256 + 32];
                if (*pszVendor || *pszProduct)
                {
                    if (*pszVendor && *pszProduct)
                        RTStrPrintf(szName, sizeof(szName), "%s %s", pszVendor, pszProduct);
                    else
                        strcpy(szName, *pszVendor ? pszVendor : pszProduct);

                    for (PDARWINDVD pCur = pHead; pCur; pCur = pCur->pNext)
                    {
                        if (!strcmp(szName, pCur->szName))
                        {
                            if (*pszVendor && *pszProduct)
                                RTStrPrintf(szName, sizeof(szName), "%s %s (#%u)", pszVendor, pszProduct, i);
                            else
                                RTStrPrintf(szName, sizeof(szName), "%s (#%u)", *pszVendor ? pszVendor : pszProduct, i);
                            break;
                        }
                    }
                }
                else
                    RTStrPrintf(szName, sizeof(szName), "(#%u)", i);

                /* Create the device. */
                size_t cbName = strlen(szName) + 1;
                PDARWINDVD pNew = (PDARWINDVD)RTMemAlloc(RT_UOFFSETOF_DYN(DARWINDVD, szName[cbName]));
                if (pNew)
                {
                    pNew->pNext = NULL;
                    memcpy(pNew->szName, szName, cbName);
                    if (pTail)
                        pTail = pTail->pNext = pNew;
                    else
                        pTail = pHead = pNew;
                }
            }
            CFRelease(PropsRef);
        }
        else
            AssertMsgFailed(("krc=%#x\n", krc));

        IOObjectRelease(DVDService);
        i++;
    }

    IOObjectRelease(DVDServices);

    return pHead;
}


/**
 * Enumerate the fixed drives (HDDs, SSD, ++) returning a FIFO of device paths
 * strings and model strings separated by ':'.
 *
 * @returns Pointer to the head.
 *          The caller is responsible for calling RTMemFree() on each of the nodes.
 */
PDARWINFIXEDDRIVE DarwinGetFixedDrives(void)
{
    AssertReturn(darwinOpenMasterPort(), NULL);

    /*
     * Create a matching dictionary for searching drives in the IOKit.
     *
     * The idea is to find all the IOMedia objects with "Whole"="True" which identify the disks but
     * not partitions.
     */
    CFMutableDictionaryRef RefMatchingDict = IOServiceMatching("IOMedia");
    AssertReturn(RefMatchingDict, NULL);
    CFDictionaryAddValue(RefMatchingDict, CFSTR(kIOMediaWholeKey), kCFBooleanTrue);

    /*
     * Perform the search and get a collection of IOMedia objects.
     */
    io_iterator_t MediaServices = IO_OBJECT_NULL;
    IOReturn irc = IOServiceGetMatchingServices(g_MasterPort, RefMatchingDict, &MediaServices);
    AssertMsgReturn(irc == kIOReturnSuccess, ("irc=%d\n", irc), NULL);
    RefMatchingDict = NULL; /* the reference is consumed by IOServiceGetMatchingServices. */

    /*
     * Enumerate the matching services.
     * (This enumeration must be identical to the one performed in DrvHostBase.cpp.)
     */
    PDARWINFIXEDDRIVE pHead = NULL;
    PDARWINFIXEDDRIVE pTail = NULL;
    io_object_t MediaService;
    while ((MediaService = IOIteratorNext(MediaServices)) != IO_OBJECT_NULL)
    {
        DARWIN_IOKIT_DUMP_OBJ(MediaService);

        /*
         * Find the IOMedia parents having the IOBlockStorageDevice type and check they have "device-type" = "Generic".
         * If the IOMedia object hasn't IOBlockStorageDevices with such device-type in parents the one is not general
         * disk but either CDROM-like device or some another device which has no interest for the function.
         */

        /*
         * Just avoid parents enumeration if the IOMedia is IOCDMedia, i.e. CDROM-like disk
         */
        if (IOObjectConformsTo(MediaService, kIOCDMediaClass))
        {
            IOObjectRelease(MediaService);
            continue;
        }

        bool fIsGenericStorage = false;
        io_registry_entry_t ChildEntry = MediaService;
        io_registry_entry_t ParentEntry = IO_OBJECT_NULL;
        kern_return_t krc = KERN_SUCCESS;
        while (   !fIsGenericStorage
               && (krc = IORegistryEntryGetParentEntry(ChildEntry, kIOServicePlane, &ParentEntry)) == KERN_SUCCESS)
        {
            if (!IOObjectIsEqualTo(ChildEntry, MediaService))
                IOObjectRelease(ChildEntry);

            DARWIN_IOKIT_DUMP_OBJ(ParentEntry);
            if (IOObjectConformsTo(ParentEntry, kIOBlockStorageDeviceClass))
            {
                CFTypeRef DeviceTypeValueRef = IORegistryEntryCreateCFProperty(ParentEntry,
                                                                               CFSTR("device-type"),
                                                                               kCFAllocatorDefault, 0);
                if (   DeviceTypeValueRef
                    && CFGetTypeID(DeviceTypeValueRef) == CFStringGetTypeID()
                    && CFStringCompare((CFStringRef)DeviceTypeValueRef, CFSTR("Generic"),
                                       kCFCompareCaseInsensitive) == kCFCompareEqualTo)
                    fIsGenericStorage = true;

                if (DeviceTypeValueRef != NULL)
                    CFRelease(DeviceTypeValueRef);
            }
            ChildEntry = ParentEntry;
        }
        if (ChildEntry != IO_OBJECT_NULL && !IOObjectIsEqualTo(ChildEntry, MediaService))
            IOObjectRelease(ChildEntry);

        if (!fIsGenericStorage)
        {
            IOObjectRelease(MediaService);
            continue;
        }

        CFTypeRef DeviceName;
        DeviceName = IORegistryEntryCreateCFProperty(MediaService,
                                                     CFSTR(kIOBSDNameKey),
                                                     kCFAllocatorDefault,0);
        if (DeviceName)
        {
            char szDeviceFilePath[MAXPATHLEN];
            strcpy(szDeviceFilePath, _PATH_DEV);
            size_t cchPathSize = strlen(szDeviceFilePath);
            if (CFStringGetCString((CFStringRef)DeviceName,
                                   &szDeviceFilePath[cchPathSize],
                                   (CFIndex)(sizeof(szDeviceFilePath) - cchPathSize),
                                   kCFStringEncodingUTF8))
            {
                PDARWINFIXEDDRIVE pDuplicate = pHead;
                while (pDuplicate && strcmp(szDeviceFilePath, pDuplicate->szName) != 0)
                    pDuplicate = pDuplicate->pNext;
                if (pDuplicate == NULL)
                {
                    /* Get model for the IOMedia object.
                     *
                     * Due to vendor and product property names are different and
                     * depend on interface and device type, the best way to get a drive
                     * model is get IORegistry name for the IOMedia object. Usually,
                     * it takes "<vendor> <product> <revision> Media" form. Noticed,
                     * such naming are used by only IOMedia objects having
                     * "Whole" = True and "BSDName" properties set.
                     */
                    io_name_t szEntryName = { 0 };
                    if ((krc = IORegistryEntryGetName(MediaService, szEntryName)) == KERN_SUCCESS)
                    {
                        /* remove " Media" from the end of the name */
                        char *pszMedia = strrchr(szEntryName, ' ');
                        if (   pszMedia != NULL
                            && (uintptr_t)pszMedia < (uintptr_t)&szEntryName[sizeof(szEntryName)]
                            && strcmp(pszMedia, " Media") == 0)
                        {
                            *pszMedia = '\0';
                            RTStrPurgeEncoding(szEntryName);
                        }
                    }
                    /* Create the device path and model name in form "/device/path:model". */
                    cchPathSize = strlen(szDeviceFilePath);
                    size_t const cchModelSize = strlen(szEntryName);
                    size_t const cbExtra = cchPathSize + 1 + cchModelSize + !!cchModelSize;
                    PDARWINFIXEDDRIVE pNew = (PDARWINFIXEDDRIVE)RTMemAlloc(RT_UOFFSETOF_DYN(DARWINFIXEDDRIVE, szName[cbExtra]));
                    if (pNew)
                    {
                        pNew->pNext = NULL;
                        memcpy(pNew->szName, szDeviceFilePath, cchPathSize + 1);
                        pNew->pszModel = NULL;
                        if (cchModelSize)
                            pNew->pszModel = (const char *)memcpy(&pNew->szName[cchPathSize + 1], szEntryName, cchModelSize + 1);

                        if (pTail)
                            pTail = pTail->pNext = pNew;
                        else
                            pTail = pHead = pNew;
                    }
                }
            }
            CFRelease(DeviceName);
        }
        IOObjectRelease(MediaService);
    }
    IOObjectRelease(MediaServices);

    return pHead;
}


/**
 * Enumerate the ethernet capable network devices returning a FIFO of them.
 *
 * @returns Pointer to the head.
 */
PDARWINETHERNIC DarwinGetEthernetControllers(void)
{
    AssertReturn(darwinOpenMasterPort(), NULL);

    /*
     * Create a matching dictionary for searching for ethernet controller
     * services in the IOKit.
     *
     * For some really stupid reason I don't get all the controllers if I look for
     * objects that are instances of IOEthernetController or its descendants (only
     * get the  AirPort on my mac pro). But fortunately using IOEthernetInterface
     * seems to work. Weird s**t!
     */
    //CFMutableDictionaryRef RefMatchingDict = IOServiceMatching("IOEthernetController"); - this doesn't work :-(
    CFMutableDictionaryRef RefMatchingDict = IOServiceMatching("IOEthernetInterface");
    AssertReturn(RefMatchingDict, NULL);

    /*
     * Perform the search and get a collection of ethernet controller services.
     */
    io_iterator_t EtherIfServices = IO_OBJECT_NULL;
    IOReturn irc = IOServiceGetMatchingServices(g_MasterPort, RefMatchingDict, &EtherIfServices);
    AssertMsgReturn(irc == kIOReturnSuccess, ("irc=%d\n", irc), NULL);
    RefMatchingDict = NULL; /* the reference is consumed by IOServiceGetMatchingServices. */

    /*
     * Get a copy of the current network interfaces from the system configuration service.
     * We'll use this for looking up the proper interface names.
     */
    CFArrayRef IfsRef = SCNetworkInterfaceCopyAll();
    CFIndex cIfs = IfsRef ? CFArrayGetCount(IfsRef) : 0;

    /*
     * Get the current preferences and make a copy of the network services so we
     * can look up the right interface names. The IfsRef is just for fallback.
     */
    CFArrayRef ServicesRef = NULL;
    CFIndex cServices = 0;
    SCPreferencesRef PrefsRef = SCPreferencesCreate(kCFAllocatorDefault, CFSTR("org.virtualbox.VBoxSVC"), NULL);
    if (PrefsRef)
    {
        SCNetworkSetRef SetRef = SCNetworkSetCopyCurrent(PrefsRef);
        CFRelease(PrefsRef);
        if (SetRef)
        {
            ServicesRef = SCNetworkSetCopyServices(SetRef);
            CFRelease(SetRef);
            cServices = ServicesRef ? CFArrayGetCount(ServicesRef) : 0;
        }
    }

    /*
     * Enumerate the ethernet controller services.
     */
    PDARWINETHERNIC pHead = NULL;
    PDARWINETHERNIC pTail = NULL;
    io_object_t EtherIfService;
    while ((EtherIfService = IOIteratorNext(EtherIfServices)) != IO_OBJECT_NULL)
    {
        /*
         * Dig up the parent, meaning the IOEthernetController.
         */
        io_object_t EtherNICService;
        kern_return_t krc = IORegistryEntryGetParentEntry(EtherIfService, kIOServicePlane, &EtherNICService);
        /*krc = IORegistryEntryGetChildEntry(EtherNICService, kIOServicePlane, &EtherIfService); */
        if (krc == KERN_SUCCESS)
        {
            DARWIN_IOKIT_DUMP_OBJ(EtherNICService);
            /*
             * Get the properties we use to identify and name the Ethernet NIC.
             * We need the both the IOEthernetController and it's IONetworkInterface child.
             */
            CFMutableDictionaryRef PropsRef = 0;
            krc = IORegistryEntryCreateCFProperties(EtherNICService, &PropsRef, kCFAllocatorDefault, kNilOptions);
            if (krc == KERN_SUCCESS)
            {
                CFMutableDictionaryRef IfPropsRef = 0;
                krc = IORegistryEntryCreateCFProperties(EtherIfService, &IfPropsRef, kCFAllocatorDefault, kNilOptions);
                if (krc == KERN_SUCCESS)
                {
                    /*
                     * Gather the required data.
                     * We'll create a UUID from the MAC address and the BSD name.
                     */
                    char szTmp[256];
                    do
                    {
                        /* Check if airport (a bit heuristical - it's com.apple.driver.AirPortBrcm43xx here). */
                        darwinDictGetString(PropsRef, CFSTR("CFBundleIdentifier"), szTmp, sizeof(szTmp));
                        bool fWireless;
                        bool fAirPort = fWireless = strstr(szTmp, ".AirPort") != NULL;

                        /* Check if it's USB. */
                        darwinDictGetString(PropsRef, CFSTR("IOProviderClass"), szTmp, sizeof(szTmp));
                        bool fUSB = strstr(szTmp, "USB") != NULL;


                        /* Is it builtin? */
                        bool fBuiltin;
                        darwinDictGetBool(IfPropsRef, CFSTR("IOBuiltin"), &fBuiltin);

                        /* Is it the primary interface  */
                        bool fPrimaryIf;
                        darwinDictGetBool(IfPropsRef, CFSTR("IOPrimaryInterface"), &fPrimaryIf);

                        /* Get the MAC address. */
                        RTMAC Mac;
                        AssertBreak(darwinDictGetData(PropsRef, CFSTR("IOMACAddress"), &Mac, sizeof(Mac)));

                        /* The BSD Name from the interface dictionary.  No assert here as the belkin USB-C gadget
                           does not always end up with a BSD name, typically requiring replugging. */
                        char szBSDName[RT_SIZEOFMEMB(DARWINETHERNIC, szBSDName)];
                        if (RT_UNLIKELY(!darwinDictGetString(IfPropsRef, CFSTR("BSD Name"), szBSDName, sizeof(szBSDName))))
                        {
                            LogRelMax(32, ("DarwinGetEthernetControllers: Warning! Failed to get 'BSD Name'; provider class %s\n", szTmp));
                            break;
                        }

                        /* Check if it's really wireless. */
                        if (    darwinDictIsPresent(IfPropsRef, CFSTR("IO80211CountryCode"))
                            ||  darwinDictIsPresent(IfPropsRef, CFSTR("IO80211DriverVersion"))
                            ||  darwinDictIsPresent(IfPropsRef, CFSTR("IO80211HardwareVersion"))
                            ||  darwinDictIsPresent(IfPropsRef, CFSTR("IO80211Locale")))
                            fWireless = true;
                        else
                            fAirPort = fWireless = false;

                        /** @todo IOPacketFilters / IONetworkFilterGroup?  */
                        /*
                         * Create the interface name.
                         *
                         * Note! The ConsoleImpl2.cpp code ASSUMES things about the name. It is also
                         *       stored in the VM config files. (really bright idea)
                         */
                        strcpy(szTmp, szBSDName);
                        char *psz = strchr(szTmp, '\0');
                        *psz++ = ':';
                        *psz++ = ' ';
                        size_t cchLeft = sizeof(szTmp) - (size_t)(psz - &szTmp[0]) - (sizeof(" (Wireless)") - 1);
                        bool fFound = false;
                        CFIndex i;

                        /* look it up among the current services */
                        for (i = 0; i < cServices; i++)
                        {
                            SCNetworkServiceRef ServiceRef = (SCNetworkServiceRef)CFArrayGetValueAtIndex(ServicesRef, i);
                            SCNetworkInterfaceRef IfRef = SCNetworkServiceGetInterface(ServiceRef);
                            if (IfRef)
                            {
                                CFStringRef BSDNameRef = SCNetworkInterfaceGetBSDName(IfRef);
                                if (     BSDNameRef
                                    &&   CFStringGetCString(BSDNameRef, psz, (CFIndex)cchLeft, kCFStringEncodingUTF8)
                                    &&  !strcmp(psz, szBSDName))
                                {
                                    CFStringRef ServiceNameRef = SCNetworkServiceGetName(ServiceRef);
                                    if (    ServiceNameRef
                                        &&  CFStringGetCString(ServiceNameRef, psz, (CFIndex)cchLeft, kCFStringEncodingUTF8))
                                    {
                                        fFound = true;
                                        break;
                                    }
                                }
                            }
                        }
                        /* Look it up in the interface list. */
                        if (!fFound)
                            for (i = 0; i < cIfs; i++)
                            {
                                SCNetworkInterfaceRef IfRef = (SCNetworkInterfaceRef)CFArrayGetValueAtIndex(IfsRef, i);
                                CFStringRef BSDNameRef = SCNetworkInterfaceGetBSDName(IfRef);
                                if (     BSDNameRef
                                    &&   CFStringGetCString(BSDNameRef, psz, (CFIndex)cchLeft, kCFStringEncodingUTF8)
                                    &&  !strcmp(psz, szBSDName))
                                {
                                    CFStringRef DisplayNameRef = SCNetworkInterfaceGetLocalizedDisplayName(IfRef);
                                    if (    DisplayNameRef
                                        &&  CFStringGetCString(DisplayNameRef, psz, (CFIndex)cchLeft, kCFStringEncodingUTF8))
                                    {
                                        fFound = true;
                                        break;
                                    }
                                }
                            }
                        /* Generate a half plausible name if we for some silly reason didn't find the interface. */
                        if (!fFound)
                            RTStrPrintf(szTmp, sizeof(szTmp), "%s: %s%s(?)",
                                        szBSDName,
                                        fUSB ? "USB " : "",
                                        fWireless ? fAirPort ? "AirPort " : "Wireless" : "Ethernet");
                        /* If we did find it and it's wireless but without "AirPort" or "Wireless", fix it */
                        else if (   fWireless
                                 && !strstr(psz, "AirPort")
                                 && !strstr(psz, "Wireless"))
                            strcat(szTmp, fAirPort ? " (AirPort)" : " (Wireless)");

                        /*
                         * Create the list entry.
                         */
                        DARWIN_IOKIT_LOG(("Found: if=%s mac=%.6Rhxs fWireless=%RTbool fAirPort=%RTbool fBuiltin=%RTbool fPrimaryIf=%RTbool fUSB=%RTbool\n",
                                          szBSDName, &Mac, fWireless, fAirPort, fBuiltin, fPrimaryIf, fUSB));

                        size_t cchName = strlen(szTmp);
                        PDARWINETHERNIC pNew = (PDARWINETHERNIC)RTMemAlloc(RT_UOFFSETOF_DYN(DARWINETHERNIC, szName[cchName + 1]));
                        if (pNew)
                        {
                            strncpy(pNew->szBSDName, szBSDName, sizeof(pNew->szBSDName)); /* the '\0' padding is intentional! */

                            RTUuidClear(&pNew->Uuid);
                            memcpy(&pNew->Uuid, pNew->szBSDName, RT_MIN(sizeof(pNew->szBSDName), sizeof(pNew->Uuid)));
                            pNew->Uuid.Gen.u8ClockSeqHiAndReserved = (pNew->Uuid.Gen.u8ClockSeqHiAndReserved & 0x3f) | 0x80;
                            pNew->Uuid.Gen.u16TimeHiAndVersion = (pNew->Uuid.Gen.u16TimeHiAndVersion & 0x0fff) | 0x4000;
                            pNew->Uuid.Gen.au8Node[0] = Mac.au8[0];
                            pNew->Uuid.Gen.au8Node[1] = Mac.au8[1];
                            pNew->Uuid.Gen.au8Node[2] = Mac.au8[2];
                            pNew->Uuid.Gen.au8Node[3] = Mac.au8[3];
                            pNew->Uuid.Gen.au8Node[4] = Mac.au8[4];
                            pNew->Uuid.Gen.au8Node[5] = Mac.au8[5];

                            pNew->Mac = Mac;
                            pNew->fWireless = fWireless;
                            pNew->fAirPort = fAirPort;
                            pNew->fBuiltin = fBuiltin;
                            pNew->fUSB = fUSB;
                            pNew->fPrimaryIf = fPrimaryIf;
                            memcpy(pNew->szName, szTmp, cchName + 1);

                            /*
                             * Link it into the list, keep the list sorted by fPrimaryIf and the BSD name.
                             */
                            if (pTail)
                            {
                                PDARWINETHERNIC pPrev = pTail;
                                if (strcmp(pNew->szBSDName, pPrev->szBSDName) < 0)
                                {
                                    pPrev = NULL;
                                    for (PDARWINETHERNIC pCur = pHead; pCur; pPrev = pCur, pCur = pCur->pNext)
                                        if (    (int)pNew->fPrimaryIf - (int)pCur->fPrimaryIf > 0
                                            ||  (   (int)pNew->fPrimaryIf - (int)pCur->fPrimaryIf == 0
                                                 && strcmp(pNew->szBSDName, pCur->szBSDName) >= 0))
                                            break;
                                }
                                if (pPrev)
                                {
                                    /* tail or in list. */
                                    pNew->pNext = pPrev->pNext;
                                    pPrev->pNext = pNew;
                                    if (pPrev == pTail)
                                        pTail = pNew;
                                }
                                else
                                {
                                    /* head */
                                    pNew->pNext = pHead;
                                    pHead = pNew;
                                }
                            }
                            else
                            {
                                /* empty list */
                                pNew->pNext = NULL;
                                pTail = pHead = pNew;
                            }
                        }
                    } while (0);

                    CFRelease(IfPropsRef);
                }
                CFRelease(PropsRef);
            }
            IOObjectRelease(EtherNICService);
        }
        else
            AssertMsgFailed(("krc=%#x\n", krc));
        IOObjectRelease(EtherIfService);
    }

    IOObjectRelease(EtherIfServices);
    if (ServicesRef)
        CFRelease(ServicesRef);
    if (IfsRef)
        CFRelease(IfsRef);
    return pHead;
}

#ifdef STANDALONE_TESTCASE
/**
 * This file can optionally be compiled into a testcase, this is the main function.
 * To build:
 *      g++ -I ../../../../include -D IN_RING3 iokit.cpp   ../../../../out/darwin.x86/debug/lib/RuntimeR3.a  ../../../../out/darwin.x86/debug/lib/SUPR3.a  ../../../../out/darwin.x86/debug/lib/RuntimeR3.a ../../../../out/darwin.x86/debug/lib/VBox-kStuff.a  ../../../../out/darwin.x86/debug/lib/RuntimeR3.a -framework CoreFoundation -framework IOKit -framework SystemConfiguration -liconv -D STANDALONE_TESTCASE -o iokit -g && ./iokit
 */
int main(int argc, char **argv)
{
    RTR3InitExe(argc, &argv, 0);

    if (1)
    {
        /*
         * Network preferences.
         */
        RTPrintf("Preferences: Network Services\n");
        SCPreferencesRef PrefsRef = SCPreferencesCreate(kCFAllocatorDefault, CFSTR("org.virtualbox.VBoxSVC"), NULL);
        if (PrefsRef)
        {
            CFDictionaryRef  NetworkServiceRef = (CFDictionaryRef)SCPreferencesGetValue(PrefsRef, kSCPrefNetworkServices);
            darwinDumpDict(NetworkServiceRef, 4);
            CFRelease(PrefsRef);
        }
    }

    if (1)
    {
        /*
         * Network services interfaces in the current config.
         */
        RTPrintf("Preferences: Network Service Interfaces\n");
        SCPreferencesRef PrefsRef = SCPreferencesCreate(kCFAllocatorDefault, CFSTR("org.virtualbox.VBoxSVC"), NULL);
        if (PrefsRef)
        {
            SCNetworkSetRef SetRef = SCNetworkSetCopyCurrent(PrefsRef);
            if (SetRef)
            {
                CFArrayRef ServicesRef = SCNetworkSetCopyServices(SetRef);
                CFIndex cServices = CFArrayGetCount(ServicesRef);
                for (CFIndex i = 0; i < cServices; i++)
                {
                    SCNetworkServiceRef ServiceRef = (SCNetworkServiceRef)CFArrayGetValueAtIndex(ServicesRef, i);
                    char szServiceName[128] = {0};
                    CFStringGetCString(SCNetworkServiceGetName(ServiceRef), szServiceName, sizeof(szServiceName), kCFStringEncodingUTF8);

                    SCNetworkInterfaceRef IfRef = SCNetworkServiceGetInterface(ServiceRef);
                    char szBSDName[16] = {0};
                    if (SCNetworkInterfaceGetBSDName(IfRef))
                        CFStringGetCString(SCNetworkInterfaceGetBSDName(IfRef), szBSDName, sizeof(szBSDName), kCFStringEncodingUTF8);
                    char szDisplayName[128] = {0};
                    if (SCNetworkInterfaceGetLocalizedDisplayName(IfRef))
                        CFStringGetCString(SCNetworkInterfaceGetLocalizedDisplayName(IfRef), szDisplayName, sizeof(szDisplayName), kCFStringEncodingUTF8);

                    RTPrintf(" #%u ServiceName=\"%s\" IfBSDName=\"%s\" IfDisplayName=\"%s\"\n",
                             i, szServiceName, szBSDName, szDisplayName);
                }

                CFRelease(ServicesRef);
                CFRelease(SetRef);
            }

            CFRelease(PrefsRef);
        }
    }

    if (1)
    {
        /*
         * Network interfaces.
         */
        RTPrintf("Preferences: Network Interfaces\n");
        CFArrayRef IfsRef = SCNetworkInterfaceCopyAll();
        if (IfsRef)
        {
            CFIndex cIfs = CFArrayGetCount(IfsRef);
            for (CFIndex i = 0; i < cIfs; i++)
            {
                SCNetworkInterfaceRef IfRef = (SCNetworkInterfaceRef)CFArrayGetValueAtIndex(IfsRef, i);
                char szBSDName[16] = {0};
                if (SCNetworkInterfaceGetBSDName(IfRef))
                    CFStringGetCString(SCNetworkInterfaceGetBSDName(IfRef), szBSDName, sizeof(szBSDName), kCFStringEncodingUTF8);
                char szDisplayName[128] = {0};
                if (SCNetworkInterfaceGetLocalizedDisplayName(IfRef))
                    CFStringGetCString(SCNetworkInterfaceGetLocalizedDisplayName(IfRef), szDisplayName, sizeof(szDisplayName), kCFStringEncodingUTF8);
                RTPrintf(" #%u BSDName=\"%s\" DisplayName=\"%s\"\n",
                         i, szBSDName, szDisplayName);
            }

            CFRelease(IfsRef);
        }
    }

    if (1)
    {
        /*
         * Get and display the ethernet controllers.
         */
        RTPrintf("Ethernet controllers:\n");
        PDARWINETHERNIC pEtherNICs = DarwinGetEthernetControllers();
        for (PDARWINETHERNIC pCur = pEtherNICs; pCur; pCur = pCur->pNext)
        {
            RTPrintf("%s\n", pCur->szName);
            RTPrintf("    szBSDName=%s\n", pCur->szBSDName);
            RTPrintf("         UUID=%RTuuid\n", &pCur->Uuid);
            RTPrintf("          Mac=%.6Rhxs\n", &pCur->Mac);
            RTPrintf("    fWireless=%RTbool\n", pCur->fWireless);
            RTPrintf("     fAirPort=%RTbool\n", pCur->fAirPort);
            RTPrintf("     fBuiltin=%RTbool\n", pCur->fBuiltin);
            RTPrintf("         fUSB=%RTbool\n", pCur->fUSB);
            RTPrintf("   fPrimaryIf=%RTbool\n", pCur->fPrimaryIf);
        }
    }


    return 0;
}
#endif
