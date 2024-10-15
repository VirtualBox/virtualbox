/* $Id: UsbTestServiceGadgetClassTest.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * UsbTestServ - Remote USB test configuration and execution server, USB gadget class
 *               for the test device.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/asm.h>
#include <iprt/ctype.h>
#include <iprt/dir.h>
#include <iprt/err.h>
#include <iprt/env.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/string.h>
#include <iprt/symlink.h>
#include <iprt/thread.h>

#include <iprt/linux/sysfs.h>

#include "UsbTestServiceGadgetInternal.h"
#include "UsbTestServicePlatform.h"


/*********************************************************************************************************************************
*   Constants And Macros, Structures and Typedefs                                                                                *
*********************************************************************************************************************************/

/** Default configfs mount point. */
#define UTS_GADGET_CLASS_CONFIGFS_MNT_DEF "/sys/kernel/config/usb_gadget"
/** Gadget template name */
#define UTS_GADGET_TEMPLATE_NAME "gadget_test"

/** Default vendor ID which is recognized by the usbtest driver. */
#define UTS_GADGET_TEST_VENDOR_ID_DEF    UINT16_C(0x0525)
/** Default product ID which is recognized by the usbtest driver. */
#define UTS_GADGET_TEST_PRODUCT_ID_DEF   UINT16_C(0xa4a0)
/** Default device class. */
#define UTS_GADGET_TEST_DEVICE_CLASS_DEF UINT8_C(0xff)
/** Default serial number string. */
#define UTS_GADGET_TEST_SERIALNUMBER_DEF "0123456789"
/** Default manufacturer string. */
#define UTS_GADGET_TEST_MANUFACTURER_DEF "Oracle Inc."
/** Default product string. */
#define UTS_GADGET_TEST_PRODUCT_DEF      "USB test device"

/**
 * Internal UTS gadget host instance data.
 */
typedef struct UTSGADGETCLASSINT
{
    /** Gadget template path. */
    char                      *pszGadgetPath;
    /** The UDC this gadget is connected to. */
    char                      *pszUdc;
    /** Bus identifier for the used UDC. */
    uint32_t                   uBusId;
    /** Device identifier. */
    uint32_t                   uDevId;
} UTSGADGETCLASSINT;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

/** Number of already created gadgets, used for the template name. */
static volatile uint32_t g_cGadgets = 0;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Creates a new directory pointed to by the given format string.
 *
 * @returns IPRT status code.
 * @param   pszFormat         The format string.
 * @param   va                The arguments.
 */
static int utsGadgetClassTestDirCreateV(const char *pszFormat, va_list va)
{
    int rc = VINF_SUCCESS;
    char aszPath[RTPATH_MAX + 1];

    size_t cbStr = RTStrPrintfV(&aszPath[0], sizeof(aszPath), pszFormat, va);
    if (cbStr <= sizeof(aszPath) - 1)
        rc = RTDirCreateFullPath(aszPath, 0700);
    else
        rc = VERR_BUFFER_OVERFLOW;

    return rc;
}


/**
 * Creates a new directory pointed to by the given format string.
 *
 * @returns IPRT status code.
 * @param   pszFormat         The format string.
 * @param   ...               The arguments.
 */
static int utsGadgetClassTestDirCreate(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    int rc = utsGadgetClassTestDirCreateV(pszFormat, va);
    va_end(va);
    return rc;
}


/**
 * Removes a directory pointed to by the given format string.
 *
 * @returns IPRT status code.
 * @param   pszFormat         The format string.
 * @param   va                The arguments.
 */
static int utsGadgetClassTestDirRemoveV(const char *pszFormat, va_list va)
{
    int rc = VINF_SUCCESS;
    char aszPath[RTPATH_MAX + 1];

    size_t cbStr = RTStrPrintfV(&aszPath[0], sizeof(aszPath), pszFormat, va);
    if (cbStr <= sizeof(aszPath) - 1)
        rc = RTDirRemove(aszPath);
    else
        rc = VERR_BUFFER_OVERFLOW;

    return rc;
}


/**
 * Removes a directory pointed to by the given format string.
 *
 * @returns IPRT status code.
 * @param   pszFormat         The format string.
 * @param   ...               The arguments.
 */
static int utsGadgetClassTestDirRemove(const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    int rc = utsGadgetClassTestDirRemoveV(pszFormat, va);
    va_end(va);
    return rc;
}


/**
 * Links the given function to the given config.
 *
 * @returns IPRT status code.
 * @param   pClass            The gadget class instance data.
 * @param   pszFunc           The function to link.
 * @param   pszCfg            The configuration which the function will be part of.
 */
static int utsGadgetClassTestLinkFuncToCfg(PUTSGADGETCLASSINT pClass, const char *pszFunc, const char *pszCfg)
{
    int rc = VINF_SUCCESS;
    char aszPathFunc[RTPATH_MAX + 1];
    char aszPathCfg[RTPATH_MAX + 1];

    size_t cbStr = RTStrPrintf(&aszPathFunc[0], sizeof(aszPathFunc), "%s/functions/%s",
                               pClass->pszGadgetPath, pszFunc);
    if (cbStr <= sizeof(aszPathFunc) - 1)
    {
        cbStr = RTStrPrintf(&aszPathCfg[0], sizeof(aszPathCfg), "%s/configs/%s/%s",
                            pClass->pszGadgetPath, pszCfg, pszFunc);
        if (cbStr <= sizeof(aszPathCfg) - 1)
            rc = RTSymlinkCreate(&aszPathCfg[0], &aszPathFunc[0], RTSYMLINKTYPE_DIR, 0);
        else
            rc = VERR_BUFFER_OVERFLOW;
    }
    else
        rc = VERR_BUFFER_OVERFLOW;

    return rc;
}


/**
 * Unlinks the given function from the given configuration.
 *
 * @returns IPRT status code.
 * @param   pClass            The gadget class instance data.
 * @param   pszFunc           The function to unlink.
 * @param   pszCfg            The configuration which the function is currently part of.
 */
static int utsGadgetClassTestUnlinkFuncFromCfg(PUTSGADGETCLASSINT pClass, const char *pszFunc, const char *pszCfg)
{
    int rc = VINF_SUCCESS;
    char aszPath[RTPATH_MAX + 1];
    size_t cbStr = RTStrPrintf(&aszPath[0], sizeof(aszPath), "%s/configs/%s/%s",
                               pClass->pszGadgetPath, pszCfg, pszFunc);
    if (cbStr <= sizeof(aszPath) - 1)
        rc = RTSymlinkDelete(&aszPath[0], 0);
    else
        rc = VERR_BUFFER_OVERFLOW;

    return rc;
}


/**
 * Cleans up any leftover configurations from the gadget class.
 *
 * @param   pClass            The gadget class instance data.
 */
static void utsGadgetClassTestCleanup(PUTSGADGETCLASSINT pClass)
{
    /* Unbind the gadget from the currently assigned UDC first. */
    int rc = RTLinuxSysFsWriteStrFile("", 0, NULL, "%s/UDC", pClass->pszGadgetPath);
    AssertRC(rc);

    /* Delete the symlinks, ignore any errors. */
    utsGadgetClassTestUnlinkFuncFromCfg(pClass, "Loopback.0", "c.2");
    utsGadgetClassTestUnlinkFuncFromCfg(pClass, "SourceSink.0", "c.1");

    /* Delete configuration strings and then the configuration directories. */
    utsGadgetClassTestDirRemove("%s/configs/c.2/strings/0x409", pClass->pszGadgetPath);
    utsGadgetClassTestDirRemove("%s/configs/c.1/strings/0x409", pClass->pszGadgetPath);

    utsGadgetClassTestDirRemove("%s/configs/c.2", pClass->pszGadgetPath);
    utsGadgetClassTestDirRemove("%s/configs/c.1", pClass->pszGadgetPath);

    /* Delete the functions. */
    utsGadgetClassTestDirRemove("%s/functions/Loopback.0", pClass->pszGadgetPath);
    utsGadgetClassTestDirRemove("%s/functions/SourceSink.0", pClass->pszGadgetPath);

    /* Delete the english strings. */
    utsGadgetClassTestDirRemove("%s/strings/0x409", pClass->pszGadgetPath);

    /* Finally delete the gadget template. */
    utsGadgetClassTestDirRemove(pClass->pszGadgetPath);

    /* Release the UDC. */
    if (pClass->pszUdc)
    {
        rc = utsPlatformLnxReleaseUDC(pClass->pszUdc);
        AssertRC(rc);
        RTStrFree(pClass->pszUdc);
    }
}

/**
 * @interface_method_impl{UTSGADGETCLASSIF,pfnInit}
 */
static DECLCALLBACK(int) utsGadgetClassTestInit(PUTSGADGETCLASSINT pClass, PCUTSGADGETCFGITEM paCfg)
{
    int rc = VINF_SUCCESS;

    if (RTLinuxSysFsExists(UTS_GADGET_CLASS_CONFIGFS_MNT_DEF))
    {
        /* Create the gadget template */
        unsigned idx = ASMAtomicIncU32(&g_cGadgets);

        int rcStr = RTStrAPrintf(&pClass->pszGadgetPath, "%s/%s%u", UTS_GADGET_CLASS_CONFIGFS_MNT_DEF,
                                 UTS_GADGET_TEMPLATE_NAME, idx);
        if (rcStr == -1)
            return VERR_NO_STR_MEMORY;

        rc = utsGadgetClassTestDirCreate(pClass->pszGadgetPath);
        if (RT_SUCCESS(rc))
        {
            uint16_t idVendor = 0;
            uint16_t idProduct = 0;
            uint8_t  bDeviceClass = 0;
            char *pszSerial = NULL;
            char *pszManufacturer = NULL;
            char *pszProduct = NULL;
            bool fSuperSpeed = false;

            /* Get basic device config. */
            rc = utsGadgetCfgQueryU16Def(paCfg,        "Gadget/idVendor",     &idVendor,        UTS_GADGET_TEST_VENDOR_ID_DEF);
            if (RT_SUCCESS(rc))
                rc = utsGadgetCfgQueryU16Def(paCfg,    "Gadget/idProduct",    &idProduct,       UTS_GADGET_TEST_PRODUCT_ID_DEF);
            if (RT_SUCCESS(rc))
                rc = utsGadgetCfgQueryU8Def(paCfg,     "Gadget/bDeviceClass", &bDeviceClass,    UTS_GADGET_TEST_DEVICE_CLASS_DEF);
            if (RT_SUCCESS(rc))
                rc = utsGadgetCfgQueryStringDef(paCfg, "Gadget/SerialNumber", &pszSerial,       UTS_GADGET_TEST_SERIALNUMBER_DEF);
            if (RT_SUCCESS(rc))
                rc = utsGadgetCfgQueryStringDef(paCfg, "Gadget/Manufacturer", &pszManufacturer, UTS_GADGET_TEST_MANUFACTURER_DEF);
            if (RT_SUCCESS(rc))
                rc = utsGadgetCfgQueryStringDef(paCfg, "Gadget/Product",      &pszProduct,      UTS_GADGET_TEST_PRODUCT_DEF);
            if (RT_SUCCESS(rc))
                rc = utsGadgetCfgQueryBoolDef(paCfg,   "Gadget/SuperSpeed",   &fSuperSpeed,     false);

            if (RT_SUCCESS(rc))
            {
                /* Write basic attributes. */
                rc = RTLinuxSysFsWriteU16File(16, idVendor, "%s/idVendor", pClass->pszGadgetPath);
                if (RT_SUCCESS(rc))
                    rc = RTLinuxSysFsWriteU16File(16, idProduct, "%s/idProduct", pClass->pszGadgetPath);
                if (RT_SUCCESS(rc))
                    rc = RTLinuxSysFsWriteU16File(16, bDeviceClass, "%s/bDeviceClass", pClass->pszGadgetPath);

                /* Create english language strings. */
                if (RT_SUCCESS(rc))
                    rc = utsGadgetClassTestDirCreate("%s/strings/0x409", pClass->pszGadgetPath);
                if (RT_SUCCESS(rc))
                    rc = RTLinuxSysFsWriteStrFile(pszSerial, 0, NULL, "%s/strings/0x409/serialnumber", pClass->pszGadgetPath);
                if (RT_SUCCESS(rc))
                    rc = RTLinuxSysFsWriteStrFile(pszManufacturer, 0, NULL, "%s/strings/0x409/manufacturer", pClass->pszGadgetPath);
                if (RT_SUCCESS(rc))
                    rc = RTLinuxSysFsWriteStrFile(pszProduct, 0, NULL, "%s/strings/0x409/product", pClass->pszGadgetPath);

                /* Create the gadget functions. */
                if (RT_SUCCESS(rc))
                    rc = utsGadgetClassTestDirCreate("%s/functions/SourceSink.0", pClass->pszGadgetPath);
                if (RT_SUCCESS(rc))
                    rc = utsGadgetClassTestDirCreate("%s/functions/Loopback.0", pClass->pszGadgetPath);

                /* Create the device configs. */
                if (RT_SUCCESS(rc))
                    rc = utsGadgetClassTestDirCreate("%s/configs/c.1", pClass->pszGadgetPath);
                if (RT_SUCCESS(rc))
                    rc = utsGadgetClassTestDirCreate("%s/configs/c.2", pClass->pszGadgetPath);

                /* Write configuration strings. */
                if (RT_SUCCESS(rc))
                    rc = utsGadgetClassTestDirCreate("%s/configs/c.1/strings/0x409", pClass->pszGadgetPath);
                if (RT_SUCCESS(rc))
                    rc = utsGadgetClassTestDirCreate("%s/configs/c.2/strings/0x409", pClass->pszGadgetPath);
                if (RT_SUCCESS(rc))
                    rc = RTLinuxSysFsWriteStrFile("source and sink data", 0, NULL, "%s/configs/c.1/strings/0x409/configuration", pClass->pszGadgetPath);
                if (RT_SUCCESS(rc))
                    rc = RTLinuxSysFsWriteStrFile("loop input to output", 0, NULL, "%s/configs/c.2/strings/0x409/configuration", pClass->pszGadgetPath);

                /* Link the functions into the configurations. */
                if (RT_SUCCESS(rc))
                    rc = utsGadgetClassTestLinkFuncToCfg(pClass, "SourceSink.0", "c.1");
                if (RT_SUCCESS(rc))
                    rc = utsGadgetClassTestLinkFuncToCfg(pClass, "Loopback.0", "c.2");

                /* Finally enable the gadget by attaching it to a UDC. */
                if (RT_SUCCESS(rc))
                {
                    pClass->pszUdc = NULL;

                    rc = utsPlatformLnxAcquireUDC(fSuperSpeed, &pClass->pszUdc, &pClass->uBusId);
                    if (RT_SUCCESS(rc))
                        rc = RTLinuxSysFsWriteStrFile(pClass->pszUdc, 0, NULL, "%s/UDC", pClass->pszGadgetPath);
                    if (RT_SUCCESS(rc))
                        RTThreadSleep(500); /* Fudge: Sleep a bit to give the device a chance to appear on the host so binding succeeds. */
                }
            }

            if (pszSerial)
                RTStrFree(pszSerial);
            if (pszManufacturer)
                RTStrFree(pszManufacturer);
            if (pszProduct)
                RTStrFree(pszProduct);
        }
    }
    else
        rc = VERR_NOT_FOUND;

    if (RT_FAILURE(rc))
        utsGadgetClassTestCleanup(pClass);

    return rc;
}


/**
 * @interface_method_impl{UTSGADGETCLASSIF,pfnTerm}
 */
static DECLCALLBACK(void) utsGadgetClassTestTerm(PUTSGADGETCLASSINT pClass)
{
    utsGadgetClassTestCleanup(pClass);

    if (pClass->pszGadgetPath)
        RTStrFree(pClass->pszGadgetPath);
}


/**
 * @interface_method_impl{UTSGADGETCLASSIF,pfnGetBusId}
 */
static DECLCALLBACK(uint32_t) utsGadgetClassTestGetBusId(PUTSGADGETCLASSINT pClass)
{
    return pClass->uBusId;
}


/**
 * @interface_method_impl{UTSGADGETCLASSIF,pfnConnect}
 */
static DECLCALLBACK(int) utsGadgetClassTestConnect(PUTSGADGETCLASSINT pClass)
{
    int rc = RTLinuxSysFsWriteStrFile("connect", 0, NULL, "/sys/class/udc/%s/soft_connect", pClass->pszUdc);
    if (RT_SUCCESS(rc))
        RTThreadSleep(500); /* Fudge: Sleep a bit to give the device a chance to appear on the host so binding succeeds. */

    return rc;
}


/**
 * @interface_method_impl{UTSGADGETCLASSIF,pfnDisconnect}
 */
static DECLCALLBACK(int) utsGadgetClassTestDisconnect(PUTSGADGETCLASSINT pClass)
{
    return RTLinuxSysFsWriteStrFile("disconnect", 0, NULL, "/sys/class/udc/%s/soft_connect", pClass->pszUdc);}



/**
 * The gadget host interface callback table.
 */
const UTSGADGETCLASSIF g_UtsGadgetClassTest =
{
    /** enmType */
    UTSGADGETCLASS_TEST,
    /** pszDesc */
    "UTS test device gadget class",
    /** cbIf */
    sizeof(UTSGADGETCLASSINT),
    /** pfnInit */
    utsGadgetClassTestInit,
    /** pfnTerm */
    utsGadgetClassTestTerm,
    /** pfnGetBusId */
    utsGadgetClassTestGetBusId,
    /** pfnConnect */
    utsGadgetClassTestConnect,
    /** pfnDisconnect. */
    utsGadgetClassTestDisconnect
};

