/* $Id: USBProxyDevice.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * USBProxy - USB device proxy.
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
#define LOG_GROUP LOG_GROUP_DRV_USBPROXY
#include <VBox/usb.h>
#include <VBox/usbfilter.h>
#include <VBox/vmm/pdm.h>
#include <VBox/err.h>
#include <iprt/alloc.h>
#include <iprt/string.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include "USBProxyDevice.h"
#include "VUSBInternal.h"
#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** A dummy name used early during the construction phase to avoid log crashes. */
static char g_szDummyName[] = "proxy xxxx:yyyy";

/**
 * Array of supported proxy backends.
 */
static PCUSBPROXYBACK g_aUsbProxies[] =
{
    &g_USBProxyDeviceHost,
    &g_USBProxyDeviceVRDP,
    &g_USBProxyDeviceUsbIp
};

/* Synchronously obtain a standard USB descriptor for a device, used in order
 * to grab configuration descriptors when we first add the device
 */
static void *GetStdDescSync(PUSBPROXYDEV pProxyDev, uint8_t iDescType, uint8_t iIdx, uint16_t LangId, uint16_t cbHint)
{
#define GET_DESC_RETRIES 6
    int cRetries = 0;
    uint16_t cbInitialHint = cbHint;

    LogFlow(("GetStdDescSync: pProxyDev=%s, iDescType=%d, iIdx=%d, LangId=%04X, cbHint=%u\n", pProxyDev->pUsbIns->pszName, iDescType, iIdx, LangId, cbHint));
    for (;;)
    {
        /*
         * Setup a MSG URB, queue and reap it.
         */
        int rc = VINF_SUCCESS;
        VUSBURB Urb;
        AssertCompile(RT_SIZEOFMEMB(VUSBURB, abData) >= _4K);
        RT_ZERO(Urb);
        Urb.u32Magic      = VUSBURB_MAGIC;
        Urb.enmState      = VUSBURBSTATE_IN_FLIGHT;
        Urb.pszDesc       = (char*)"URB sync";
        Urb.DstAddress    = 0;
        Urb.EndPt         = 0;
        Urb.enmType       = VUSBXFERTYPE_MSG;
        Urb.enmDir        = VUSBDIRECTION_IN;
        Urb.fShortNotOk   = false;
        Urb.enmStatus     = VUSBSTATUS_INVALID;
        cbHint = RT_MIN(cbHint, sizeof(Urb.abData) - sizeof(VUSBSETUP));
        Urb.cbData = cbHint + sizeof(VUSBSETUP);

        PVUSBSETUP pSetup = (PVUSBSETUP)Urb.abData;
        pSetup->bmRequestType = VUSB_DIR_TO_HOST | VUSB_REQ_STANDARD | VUSB_TO_DEVICE;
        pSetup->bRequest = VUSB_REQ_GET_DESCRIPTOR;
        pSetup->wValue = (iDescType << 8) | iIdx;
        pSetup->wIndex = LangId;
        pSetup->wLength = cbHint;

        uint8_t *pbDesc = (uint8_t *)(pSetup + 1);
        uint32_t cbDesc = 0;
        PVUSBURB pUrbReaped = NULL;

        rc = pProxyDev->pOps->pfnUrbQueue(pProxyDev, &Urb);
        if (RT_FAILURE(rc))
        {
            Log(("GetStdDescSync: pfnUrbQueue failed, rc=%d\n", rc));
            goto err;
        }

        /* Don't wait forever, it's just a simple request that should
           return immediately. Since we're executing in the EMT thread
           it's important not to get stuck here. (Some of the builtin
           iMac devices may refuse to respond for instance.) */
        pUrbReaped = pProxyDev->pOps->pfnUrbReap(pProxyDev, 5000 /* ms */);
        if (!pUrbReaped)
        {
            Log(("GetStdDescSync: pfnUrbReap returned NULL, cancel and re-reap\n"));
            rc = pProxyDev->pOps->pfnUrbCancel(pProxyDev, &Urb);
            AssertRC(rc);
            /** @todo This breaks the comment above... */
            pUrbReaped = pProxyDev->pOps->pfnUrbReap(pProxyDev, RT_INDEFINITE_WAIT);
        }
        if (pUrbReaped != &Urb)
        {
            Log(("GetStdDescSync: pfnUrbReap failed, pUrbReaped=%p\n", pUrbReaped));
            goto err;
        }

        if (Urb.enmStatus != VUSBSTATUS_OK)
        {
            Log(("GetStdDescSync: Urb.enmStatus=%d\n", Urb.enmStatus));
            goto err;
        }

        /*
         * Check the length, config descriptors have total_length field
         */
        if (iDescType == VUSB_DT_CONFIG)
        {
            if (Urb.cbData < sizeof(VUSBSETUP) + 4)
            {
                Log(("GetStdDescSync: Urb.cbData=%#x (min 4)\n", Urb.cbData));
                goto err;
            }
            cbDesc = RT_LE2H_U16(((uint16_t *)pbDesc)[1]);
        }
        else
        {
            if (Urb.cbData < sizeof(VUSBSETUP) + 1)
            {
                Log(("GetStdDescSync: Urb.cbData=%#x (min 1)\n", Urb.cbData));
                goto err;
            }
            cbDesc = ((uint8_t *)pbDesc)[0];
        }

        Log(("GetStdDescSync: got Urb.cbData=%u, cbDesc=%u cbHint=%u\n", Urb.cbData, cbDesc, cbHint));

        if (    Urb.cbData == cbHint + sizeof(VUSBSETUP)
            &&  cbDesc > Urb.cbData - sizeof(VUSBSETUP))
        {
            cbHint = cbDesc;
            Log(("GetStdDescSync: Part descriptor, Urb.cbData=%u, cbDesc=%u cbHint=%u\n", Urb.cbData, cbDesc, cbHint));

            if (cbHint > sizeof(Urb.abData))
            {
                Log(("GetStdDescSync: cbHint=%u, Urb.abData=%u, retrying immediately\n", cbHint, sizeof(Urb.abData)));
                /* Not an error, go again without incrementing retry count or delaying. */
                continue;
            }

            goto err;
        }

        if (cbDesc > Urb.cbData - sizeof(VUSBSETUP))
        {
            Log(("GetStdDescSync: Descriptor length too short, cbDesc=%u, Urb.cbData=%u\n", cbDesc, Urb.cbData));
            goto err;
        }

        if (   cbInitialHint != cbHint
            && (   cbDesc != cbHint
                || Urb.cbData < cbInitialHint) )
        {
            Log(("GetStdDescSync: Descriptor length incorrect, cbDesc=%u, Urb.cbData=%u, cbHint=%u\n", cbDesc, Urb.cbData, cbHint));
            goto err;
        }

#ifdef LOG_ENABLED
        vusbUrbTrace(&Urb, "GetStdDescSync", true);
#endif

        /*
         * Fine, we got everything return a heap duplicate of the descriptor.
         */
        return RTMemDup(pbDesc, cbDesc);

err:
        cRetries++;
        if (cRetries < GET_DESC_RETRIES)
        {
            Log(("GetStdDescSync: Retrying %u/%u\n", cRetries, GET_DESC_RETRIES));
            RTThreadSleep(100);
            continue;
        }
        else
        {
            Log(("GetStdDescSync: Retries exceeded %u/%u. Giving up.\n", cRetries, GET_DESC_RETRIES));
            break;
        }
    }

    return NULL;
}

/**
 * Frees a descriptor returned by GetStdDescSync().
 */
static void free_desc(void *pvDesc)
{
    RTMemFree(pvDesc);
}

/**
 * Get and a device descriptor and byteswap it appropriately.
 */
static bool usbProxyGetDeviceDesc(PUSBPROXYDEV pProxyDev, PVUSBDESCDEVICE pOut)
{
    /*
     * Get the descriptor from the device.
     */
    PVUSBDESCDEVICE pIn = (PVUSBDESCDEVICE)GetStdDescSync(pProxyDev, VUSB_DT_DEVICE, 0, 0, VUSB_DT_DEVICE_MIN_LEN);
    if (!pIn)
    {
        Log(("usbProxyGetDeviceDesc: pProxyDev=%s: GetStdDescSync failed\n", pProxyDev->pUsbIns->pszName));
        return false;
    }
    if (pIn->bLength < VUSB_DT_DEVICE_MIN_LEN)
    {
        Log(("usb-proxy: pProxyDev=%s: Corrupted device descriptor. bLength=%d\n", pProxyDev->pUsbIns->pszName, pIn->bLength));
        return false;
    }

    /*
     * Convert it.
     */
    pOut->bLength            = VUSB_DT_DEVICE_MIN_LEN;
    pOut->bDescriptorType    = VUSB_DT_DEVICE;
    pOut->bcdUSB             = RT_LE2H_U16(pIn->bcdUSB);
    pOut->bDeviceClass       = pIn->bDeviceClass;
    pOut->bDeviceSubClass    = pIn->bDeviceSubClass;
    pOut->bDeviceProtocol    = pIn->bDeviceProtocol;
    pOut->bMaxPacketSize0    = pIn->bMaxPacketSize0;
    pOut->idVendor           = RT_LE2H_U16(pIn->idVendor);
    pOut->idProduct          = RT_LE2H_U16(pIn->idProduct);
    pOut->bcdDevice          = RT_LE2H_U16(pIn->bcdDevice);
    pOut->iManufacturer      = pIn->iManufacturer;
    pOut->iProduct           = pIn->iProduct;
    pOut->iSerialNumber      = pIn->iSerialNumber;
    pOut->bNumConfigurations = pIn->bNumConfigurations;

    free_desc(pIn);
    return true;
}

/**
 * Count the numbers and types of each kind of descriptor that we need to
 * copy out of the config descriptor
 */
struct desc_counts
{
    size_t num_ed, num_id, num_if;
    /** bitmap (128 bits) */
    uint32_t idmap[4];
};

static int count_descriptors(struct desc_counts *cnt, uint8_t *buf, size_t len)
{
    PVUSBDESCCONFIG cfg;
    uint8_t *tmp, *end;
    uint32_t i, x;

    memset(cnt, 0, sizeof(*cnt));

    end = buf + len;

    cfg = (PVUSBDESCCONFIG)buf;
    if ( cfg->bLength < VUSB_DT_CONFIG_MIN_LEN )
        return 0;
    if ( cfg->bLength > len )
        return 0;

    for (tmp = buf + cfg->bLength; ((tmp + 1) < end) && *tmp; tmp += *tmp)
    {
        uint8_t type;
        uint32_t ifnum;
        PVUSBDESCINTERFACE id;
        PVUSBDESCENDPOINT ed;

        type = *(tmp + 1);

        switch ( type ) {
        case VUSB_DT_INTERFACE:
            id = (PVUSBDESCINTERFACE)tmp;
            if ( id->bLength < VUSB_DT_INTERFACE_MIN_LEN )
                return 0;
            cnt->num_id++;
            ifnum = id->bInterfaceNumber;
            cnt->idmap[ifnum >> 6] |= (1 << (ifnum & 0x1f));
            break;
        case VUSB_DT_ENDPOINT:
            ed = (PVUSBDESCENDPOINT)tmp;
            if ( ed->bLength < VUSB_DT_ENDPOINT_MIN_LEN )
                return 0;
            cnt->num_ed++;
            break;
        default:
            break;
        }
    }

    /* count interfaces */
    for(i=0; i < RT_ELEMENTS(cnt->idmap); i++)
        for(x=1; x; x<<=1)
            if ( cnt->idmap[i] & x )
                cnt->num_if++;

    return 1;
}

/* Given the pointer to a configuration/interface/endpoint descriptor, find any following
 * non-standard (vendor or class) descriptors.
 */
static const void *collect_stray_bits(uint8_t *this_desc, uint8_t *end, uint16_t *cbExtra)
{
    uint8_t *tmp, *buf;
    uint8_t type;

    Assert(*(this_desc + 1) == VUSB_DT_INTERFACE || *(this_desc + 1) == VUSB_DT_ENDPOINT || *(this_desc + 1) == VUSB_DT_CONFIG);
    buf = this_desc;

    /* Skip the current configuration/interface/endpoint descriptor. */
    buf += *(uint8_t *)buf;

    /* Loop until we find another descriptor we understand. */
    for (tmp = buf; ((tmp + 1) < end) && *tmp; tmp += *tmp)
    {
        type = *(tmp + 1);
        if (type == VUSB_DT_INTERFACE || type == VUSB_DT_ENDPOINT)
            break;
    }
    *cbExtra = tmp - buf;
    if (*cbExtra)
        return buf;
    else
        return NULL;
}

/* Setup a vusb_interface structure given some preallocated structures
 * to use, (we counted them already)
 */
static int copy_interface(PVUSBINTERFACE pIf, uint8_t ifnum,
                          PVUSBDESCINTERFACEEX *id, PVUSBDESCENDPOINTEX *ed,
                          uint8_t *buf, size_t len)
{
    PVUSBDESCINTERFACEEX cur_if = NULL;
    uint32_t altmap[4] = {0,};
    uint8_t *tmp, *end = buf + len;
    uint8_t alt;
    int state;
    size_t num_ep = 0;

    buf += *(uint8_t *)buf;

    pIf->cSettings = 0;
    pIf->paSettings = NULL;

    for (tmp = buf, state = 0; ((tmp + 1) < end) && *tmp; tmp += *tmp)
    {
        uint8_t type;
        PVUSBDESCINTERFACE ifd;
        PVUSBDESCENDPOINT epd;
        PVUSBDESCENDPOINTEX cur_ep;

        type = tmp[1];

        switch ( type ) {
        case VUSB_DT_INTERFACE:
            state = 0;
            ifd = (PVUSBDESCINTERFACE)tmp;

            /* Ignoring this interface */
            if ( ifd->bInterfaceNumber != ifnum )
                break;

            /* Check we didn't see this alternate setting already
             * because that will break stuff
             */
            alt = ifd->bAlternateSetting;
            if ( altmap[alt >> 6] & (1 << (alt & 0x1f)) )
                return 0;
            altmap[alt >> 6] |= (1 << (alt & 0x1f));

            cur_if = *id;
            (*id)++;
            if ( pIf->cSettings == 0 )
                pIf->paSettings = cur_if;

            memcpy(cur_if, ifd, sizeof(cur_if->Core));

            /* Point to additional interface descriptor bytes, if any. */
            AssertCompile(sizeof(cur_if->Core) == VUSB_DT_INTERFACE_MIN_LEN);
            if (cur_if->Core.bLength - VUSB_DT_INTERFACE_MIN_LEN > 0)
                cur_if->pvMore = tmp + VUSB_DT_INTERFACE_MIN_LEN;
            else
                cur_if->pvMore = NULL;

            cur_if->pvClass = collect_stray_bits(tmp, end, &cur_if->cbClass);

            pIf->cSettings++;

            state = 1;
            num_ep = 0;
            break;
        case VUSB_DT_ENDPOINT:
            if ( state == 0 )
                break;

            epd = (PVUSBDESCENDPOINT)tmp;

            cur_ep = *ed;
            (*ed)++;

            if ( num_ep == 0 )
                cur_if->paEndpoints = cur_ep;

            if ( num_ep > cur_if->Core.bNumEndpoints )
                return 0;

            memcpy(cur_ep, epd, sizeof(cur_ep->Core));

            /* Point to additional endpoint descriptor bytes, if any. */
            AssertCompile(sizeof(cur_ep->Core) == VUSB_DT_ENDPOINT_MIN_LEN);
            if (cur_ep->Core.bLength - VUSB_DT_ENDPOINT_MIN_LEN > 0)
                cur_ep->pvMore = tmp + VUSB_DT_ENDPOINT_MIN_LEN;
            else
                cur_ep->pvMore = NULL;

            cur_ep->pvClass = collect_stray_bits(tmp, end, &cur_ep->cbClass);

            cur_ep->Core.wMaxPacketSize = RT_LE2H_U16(cur_ep->Core.wMaxPacketSize);

            num_ep++;
            break;
        default:
            /* Skip unknown descriptors. */
            break;
        }
    }

    return 1;
}

/**
 * Copy all of a devices config descriptors, this is needed so that the USB
 * core layer knows all about how to map the different functions on to the
 * virtual USB bus.
 */
static bool copy_config(PUSBPROXYDEV pProxyDev, uint8_t idx, PVUSBDESCCONFIGEX out)
{
    PVUSBDESCCONFIG cfg;
    PVUSBINTERFACE pIf;
    PVUSBDESCINTERFACEEX ifd;
    PVUSBDESCENDPOINTEX epd;
    struct desc_counts cnt;
    void *descs;
    size_t tot_len;
    size_t cbIface;
    uint32_t i, x;
    uint8_t *tmp, *end;

    descs = GetStdDescSync(pProxyDev, VUSB_DT_CONFIG, idx, 0, VUSB_DT_CONFIG_MIN_LEN);
    if ( descs == NULL ) {
        Log(("copy_config: GetStdDescSync failed\n"));
        return false;
    }

    cfg = (PVUSBDESCCONFIG)descs;
    tot_len = RT_LE2H_U16(cfg->wTotalLength);

    if ( !count_descriptors(&cnt, (uint8_t *)descs, tot_len) ) {
        Log(("copy_config: count_descriptors failed\n"));
        goto err;
    }

    if ( cfg->bNumInterfaces != cnt.num_if )
        Log(("usb-proxy: config%u: bNumInterfaces %u != %u\n",
            idx, cfg->bNumInterfaces, cnt.num_if));

    Log(("usb-proxy: config%u: %u bytes id=%u ed=%u if=%u\n",
        idx, tot_len, cnt.num_id, cnt.num_ed, cnt.num_if));

    cbIface = cnt.num_if * sizeof(VUSBINTERFACE)
           + cnt.num_id * sizeof(VUSBDESCINTERFACEEX)
           + cnt.num_ed * sizeof(VUSBDESCENDPOINTEX);
    out->paIfs = (PCVUSBINTERFACE)RTMemAllocZ(cbIface);
    if ( out->paIfs == NULL ) {
        free_desc(descs);
        return false;
    }

    /* Stash a pointer to the raw config descriptor; we may need bits of it later.  */
    out->pvOriginal = descs;

    pIf = (PVUSBINTERFACE)out->paIfs;
    ifd = (PVUSBDESCINTERFACEEX)&pIf[cnt.num_if];
    epd = (PVUSBDESCENDPOINTEX)&ifd[cnt.num_id];

    out->Core.bLength = cfg->bLength;
    out->Core.bDescriptorType = cfg->bDescriptorType;
    out->Core.wTotalLength = 0; /* Auto Calculated */
    out->Core.bNumInterfaces = (uint8_t)cnt.num_if;
    out->Core.bConfigurationValue = cfg->bConfigurationValue;
    out->Core.iConfiguration = cfg->iConfiguration;
    out->Core.bmAttributes = cfg->bmAttributes;
    out->Core.MaxPower = cfg->MaxPower;

    tmp = (uint8_t *)out->pvOriginal;
    end = tmp + tot_len;

    /* Point to additional configuration descriptor bytes, if any. */
    AssertCompile(sizeof(out->Core) == VUSB_DT_CONFIG_MIN_LEN);
    if (out->Core.bLength - VUSB_DT_CONFIG_MIN_LEN > 0)
        out->pvMore = tmp + VUSB_DT_CONFIG_MIN_LEN;
    else
        out->pvMore = NULL;

    /* Typically there might be an interface association descriptor here. */
    out->pvClass = collect_stray_bits(tmp, end, &out->cbClass);

    for(i=0; i < 4; i++)
        for(x=0; x < 32; x++)
            if ( cnt.idmap[i] & (1 << x) )
                if ( !copy_interface(pIf++, (i << 6) | x, &ifd, &epd, (uint8_t *)out->pvOriginal, tot_len) ) {
                    Log(("copy_interface(%d,,) failed\n", pIf - 1));
                    goto err;
                }

    return true;
err:
    Log(("usb-proxy: config%u: Corrupted configuration descriptor\n", idx));
    free_desc(descs);
    return false;
}


/**
 * Edit out masked interface descriptors.
 *
 * @param   pProxyDev   The proxy device
 */
static void usbProxyDevEditOutMaskedIfs(PUSBPROXYDEV pProxyDev)
{
    unsigned cRemoved = 0;

    PVUSBDESCCONFIGEX paCfgs = pProxyDev->paCfgDescs;
    for (unsigned iCfg = 0; iCfg < pProxyDev->DevDesc.bNumConfigurations; iCfg++)
    {
        PVUSBINTERFACE paIfs = (PVUSBINTERFACE)paCfgs[iCfg].paIfs;
        for (unsigned iIf = 0; iIf < paCfgs[iCfg].Core.bNumInterfaces; iIf++)
            for (uint32_t iAlt = 0; iAlt < paIfs[iIf].cSettings; iAlt++)
                if (    paIfs[iIf].paSettings[iAlt].Core.bInterfaceNumber < 32
                    &&  ((1 << paIfs[iIf].paSettings[iAlt].Core.bInterfaceNumber) & pProxyDev->fMaskedIfs))
                {
                    Log(("usb-proxy: removing interface #%d (iIf=%d iAlt=%d) on config #%d (iCfg=%d)\n",
                         paIfs[iIf].paSettings[iAlt].Core.bInterfaceNumber, iIf, iAlt, paCfgs[iCfg].Core.bConfigurationValue, iCfg));
                    cRemoved++;

                    paCfgs[iCfg].Core.bNumInterfaces--;
                    unsigned cToCopy = paCfgs[iCfg].Core.bNumInterfaces - iIf;
                    if (cToCopy)
                        memmove(&paIfs[iIf], &paIfs[iIf + 1], sizeof(paIfs[0]) * cToCopy);
                    memset(&paIfs[iIf + cToCopy], '\0', sizeof(paIfs[0]));
                    break;
                }
    }

    Log(("usb-proxy: edited out %d interface(s).\n", cRemoved));
}


/**
 * @interface_method_impl{PDMUSBREG,pfnUsbReset}
 *
 * USB Device Proxy: Call OS specific code to reset the device.
 */
static DECLCALLBACK(int) usbProxyDevReset(PPDMUSBINS pUsbIns, bool fResetOnLinux)
{
    PUSBPROXYDEV pProxyDev = PDMINS_2_DATA(pUsbIns, PUSBPROXYDEV);

    if (pProxyDev->fMaskedIfs)
    {
        Log(("usbProxyDevReset: pProxyDev=%s - ignoring reset request fMaskedIfs=%#x\n", pUsbIns->pszName, pProxyDev->fMaskedIfs));
        return VINF_SUCCESS;
    }
    LogFlow(("usbProxyDevReset: pProxyDev=%s\n", pUsbIns->pszName));
    return pProxyDev->pOps->pfnReset(pProxyDev, fResetOnLinux);
}


/**
 * @interface_method_impl{PDMUSBREG,pfnUsbGetDescriptorCache}
 */
static DECLCALLBACK(PCPDMUSBDESCCACHE) usbProxyDevGetDescriptorCache(PPDMUSBINS pUsbIns)
{
    PUSBPROXYDEV pThis = PDMINS_2_DATA(pUsbIns, PUSBPROXYDEV);
    return &pThis->DescCache;
}


/**
 * @interface_method_impl{PDMUSBREG,pfnUsbSetConfiguration}
 *
 * USB Device Proxy: Release claimed interfaces, tell the OS+device about the config change, claim the new interfaces.
 */
static DECLCALLBACK(int) usbProxyDevSetConfiguration(PPDMUSBINS pUsbIns, uint8_t bConfigurationValue,
                                                     const void *pvOldCfgDesc, const void *pvOldIfState, const void *pvNewCfgDesc)
{
    PUSBPROXYDEV pProxyDev = PDMINS_2_DATA(pUsbIns, PUSBPROXYDEV);
    LogFlow(("usbProxyDevSetConfiguration: pProxyDev=%s iActiveCfg=%d bConfigurationValue=%d\n",
             pUsbIns->pszName, pProxyDev->iActiveCfg, bConfigurationValue));

    /*
     * Release the current config.
     */
    if (pvOldCfgDesc)
    {
        PCVUSBDESCCONFIGEX pOldCfgDesc = (PCVUSBDESCCONFIGEX)pvOldCfgDesc;
        PCVUSBINTERFACESTATE pOldIfState = (PCVUSBINTERFACESTATE)pvOldIfState;
        for (unsigned i = 0; i < pOldCfgDesc->Core.bNumInterfaces; i++)
            if (pOldIfState[i].pCurIfDesc)
                pProxyDev->pOps->pfnReleaseInterface(pProxyDev, pOldIfState[i].pCurIfDesc->Core.bInterfaceNumber);
    }

    /*
     * Do the actual SET_CONFIGURE.
     * The mess here is because most backends will already have selected a
     * configuration and there are a bunch of devices which will freak out
     * if we do SET_CONFIGURE twice with the same value. (PalmOne, TrekStor USB-StickGO, ..)
     *
     * After open and reset the backend should use the members iActiveCfg and cIgnoreSetConfigs
     * to indicate the new configuration state and what to do on the next SET_CONFIGURATION call.
     */
    if (    pProxyDev->iActiveCfg != bConfigurationValue
        ||  (   bConfigurationValue == 0
             && pProxyDev->iActiveCfg != -1         /* this test doesn't make sense, we know it's 0 */
             && pProxyDev->cIgnoreSetConfigs >= 2)
        ||  !pProxyDev->cIgnoreSetConfigs)
    {
        pProxyDev->cIgnoreSetConfigs = 0;
        int rc = pProxyDev->pOps->pfnSetConfig(pProxyDev, bConfigurationValue);
        if (RT_FAILURE(rc))
        {
            pProxyDev->iActiveCfg = -1;
            return rc;
        }
        pProxyDev->iActiveCfg = bConfigurationValue;
    }
    else if (pProxyDev->cIgnoreSetConfigs > 0)
        pProxyDev->cIgnoreSetConfigs--;

    /*
     * Claim the interfaces.
     */
    PCVUSBDESCCONFIGEX pNewCfgDesc = (PCVUSBDESCCONFIGEX)pvNewCfgDesc;
    Assert(pNewCfgDesc->Core.bConfigurationValue == bConfigurationValue);
    for (unsigned iIf = 0; iIf < pNewCfgDesc->Core.bNumInterfaces; iIf++)
    {
        PCVUSBINTERFACE pIf = &pNewCfgDesc->paIfs[iIf];
        for (uint32_t iAlt = 0; iAlt < pIf->cSettings; iAlt++)
        {
            if (pIf->paSettings[iAlt].Core.bAlternateSetting != 0)
                continue;
            pProxyDev->pOps->pfnClaimInterface(pProxyDev, pIf->paSettings[iAlt].Core.bInterfaceNumber);
            /* ignore failures - the backend deals with that and does the necessary logging. */
            break;
        }
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMUSBREG,pfnUsbSetInterface}
 *
 * USB Device Proxy: Call OS specific code to select alternate interface settings.
 */
static DECLCALLBACK(int) usbProxyDevSetInterface(PPDMUSBINS pUsbIns, uint8_t bInterfaceNumber, uint8_t bAlternateSetting)
{
    PUSBPROXYDEV pProxyDev = PDMINS_2_DATA(pUsbIns, PUSBPROXYDEV);
    LogFlow(("usbProxyDevSetInterface: pProxyDev=%s bInterfaceNumber=%d bAlternateSetting=%d\n",
             pUsbIns->pszName, bInterfaceNumber, bAlternateSetting));

    return pProxyDev->pOps->pfnSetInterface(pProxyDev, bInterfaceNumber, bAlternateSetting);
}


/**
 * @interface_method_impl{PDMUSBREG,pfnUsbClearHaltedEndpoint}
 *
 * USB Device Proxy: Call OS specific code to clear the endpoint.
 */
static DECLCALLBACK(int) usbProxyDevClearHaltedEndpoint(PPDMUSBINS pUsbIns, unsigned uEndpoint)
{
    PUSBPROXYDEV pProxyDev = PDMINS_2_DATA(pUsbIns, PUSBPROXYDEV);
    LogFlow(("usbProxyDevClearHaltedEndpoint: pProxyDev=%s uEndpoint=%u\n",
             pUsbIns->pszName, uEndpoint));

    return pProxyDev->pOps->pfnClearHaltedEndpoint(pProxyDev, uEndpoint);
}


/**
 * @interface_method_impl{PDMUSBREG,pfnUrbQueue}
 *
 * USB Device Proxy: Call OS specific code.
 */
static DECLCALLBACK(int) usbProxyDevUrbQueue(PPDMUSBINS pUsbIns, PVUSBURB pUrb)
{
    int rc = VINF_SUCCESS;
    PUSBPROXYDEV pProxyDev = PDMINS_2_DATA(pUsbIns, PUSBPROXYDEV);
    rc = pProxyDev->pOps->pfnUrbQueue(pProxyDev, pUrb);
    if (RT_FAILURE(rc))
        return pProxyDev->fDetached
             ? VERR_VUSB_DEVICE_NOT_ATTACHED
             : VERR_VUSB_FAILED_TO_QUEUE_URB;
    return rc;
}


/**
 * @interface_method_impl{PDMUSBREG,pfnUrbCancel}
 *
 * USB Device Proxy: Call OS specific code.
 */
static DECLCALLBACK(int) usbProxyDevUrbCancel(PPDMUSBINS pUsbIns, PVUSBURB pUrb)
{
    PUSBPROXYDEV pProxyDev = PDMINS_2_DATA(pUsbIns, PUSBPROXYDEV);
    return pProxyDev->pOps->pfnUrbCancel(pProxyDev, pUrb);
}


/**
 * @interface_method_impl{PDMUSBREG,pfnUrbReap}
 *
 * USB Device Proxy: Call OS specific code.
 */
static DECLCALLBACK(PVUSBURB) usbProxyDevUrbReap(PPDMUSBINS pUsbIns, RTMSINTERVAL cMillies)
{
    PUSBPROXYDEV pProxyDev = PDMINS_2_DATA(pUsbIns, PUSBPROXYDEV);
    PVUSBURB pUrb = pProxyDev->pOps->pfnUrbReap(pProxyDev, cMillies);
    if (    pUrb
        &&  pUrb->enmState == VUSBURBSTATE_CANCELLED
        &&  pUrb->enmStatus == VUSBSTATUS_OK)
        pUrb->enmStatus = VUSBSTATUS_DNR;
    return pUrb;
}


/**
 * @interface_method_impl{PDMUSBREG,pfnWakeup}
 *
 * USB Device Proxy: Call OS specific code.
 */
static DECLCALLBACK(int) usbProxyDevWakeup(PPDMUSBINS pUsbIns)
{
    PUSBPROXYDEV pProxyDev = PDMINS_2_DATA(pUsbIns, PUSBPROXYDEV);

    return pProxyDev->pOps->pfnWakeup(pProxyDev);
}


/** @interface_method_impl{PDMUSBREG,pfnDestruct} */
static DECLCALLBACK(void) usbProxyDestruct(PPDMUSBINS pUsbIns)
{
    PDMUSB_CHECK_VERSIONS_RETURN_VOID(pUsbIns);
    PUSBPROXYDEV pThis = PDMINS_2_DATA(pUsbIns, PUSBPROXYDEV);
    Log(("usbProxyDestruct: destroying pProxyDev=%s\n", pUsbIns->pszName));

    /* close it. */
    if (pThis->fOpened)
    {
        pThis->pOps->pfnClose(pThis);
        pThis->fOpened = false;
    }

    /* free the config descriptors. */
    if (pThis->paCfgDescs)
    {
        for (unsigned i = 0; i < pThis->DevDesc.bNumConfigurations; i++)
        {
            RTMemFree((void *)pThis->paCfgDescs[i].paIfs);
            RTMemFree((void *)pThis->paCfgDescs[i].pvOriginal);
        }
        RTMemFree(pThis->paCfgDescs);
        pThis->paCfgDescs = NULL;
    }

    /* free dev */
    if (&g_szDummyName[0] != pUsbIns->pszName)
        RTStrFree(pUsbIns->pszName);
    pUsbIns->pszName = NULL;

    if (pThis->pvInstanceDataR3)
        RTMemFree(pThis->pvInstanceDataR3);
}


/**
 * Helper function used by usbProxyConstruct when
 * reading a filter from CFG.
 *
 * @returns VBox status code.
 * @param   pFilter         The filter.
 * @param   enmFieldIdx     The filter field indext.
 * @param   pHlp            The USB helper callback table.
 * @param   pNode           The CFGM node.
 * @param   pszExact        The exact value name.
 * @param   pszExpr         The expression value name.
 */
static int usbProxyQueryNum(PUSBFILTER pFilter, USBFILTERIDX enmFieldIdx,
                            PCPDMUSBHLP pHlp, PCFGMNODE pNode,
                            const char *pszExact, const char *pszExpr)
{
    char szTmp[256];

    /* try exact first */
    uint16_t u16;
    int rc = pHlp->pfnCFGMQueryU16(pNode, pszExact, &u16);
    if (RT_SUCCESS(rc))
    {
        rc = USBFilterSetNumExact(pFilter, enmFieldIdx, u16, true);
        AssertRCReturn(rc, rc);

        /* make sure only the exact attribute is present. */
        rc = pHlp->pfnCFGMQueryString(pNode, pszExpr, szTmp, sizeof(szTmp));
        if (RT_UNLIKELY(rc != VERR_CFGM_VALUE_NOT_FOUND))
        {
            szTmp[0] = '\0';
            pHlp->pfnCFGMGetName(pNode, szTmp, sizeof(szTmp));
            LogRel(("usbProxyConstruct: %s: Both %s and %s are present!\n", szTmp, pszExact, pszExpr));
            return VERR_INVALID_PARAMETER;
        }
        return VINF_SUCCESS;
    }
    if (RT_UNLIKELY(rc != VERR_CFGM_VALUE_NOT_FOUND))
    {
        szTmp[0] = '\0';
        pHlp->pfnCFGMGetName(pNode, szTmp, sizeof(szTmp));
        LogRel(("usbProxyConstruct: %s: %s query failed, rc=%Rrc\n", szTmp, pszExact, rc));
        return rc;
    }

    /* expression? */
    rc = pHlp->pfnCFGMQueryString(pNode, pszExpr, szTmp, sizeof(szTmp));
    if (RT_SUCCESS(rc))
    {
        rc = USBFilterSetNumExpression(pFilter, enmFieldIdx, szTmp, true);
        AssertRCReturn(rc, rc);
        return VINF_SUCCESS;
    }
    if (RT_UNLIKELY(rc != VERR_CFGM_VALUE_NOT_FOUND))
    {
        szTmp[0] = '\0';
        pHlp->pfnCFGMGetName(pNode, szTmp, sizeof(szTmp));
        LogRel(("usbProxyConstruct: %s: %s query failed, rc=%Rrc\n", szTmp, pszExpr, rc));
        return rc;
    }

    return VINF_SUCCESS;
}


/** @interface_method_impl{PDMUSBREG,pfnConstruct} */
static DECLCALLBACK(int) usbProxyConstruct(PPDMUSBINS pUsbIns, int iInstance, PCFGMNODE pCfg, PCFGMNODE pCfgGlobal)
{
    PDMUSB_CHECK_VERSIONS_RETURN(pUsbIns);
    RT_NOREF(iInstance);
    PUSBPROXYDEV    pThis = PDMINS_2_DATA(pUsbIns, PUSBPROXYDEV);
    PCPDMUSBHLP     pHlp  = pUsbIns->pHlpR3;

    LogFlow(("usbProxyConstruct: pUsbIns=%p iInstance=%d\n", pUsbIns, iInstance));

    /*
     * Initialize the instance data.
     */
    pThis->pUsbIns = pUsbIns;
    pThis->pUsbIns->pszName = g_szDummyName;
    pThis->iActiveCfg = -1;
    pThis->fMaskedIfs = 0;
    pThis->fOpened = false;
    pThis->fInited = false;

    /*
     * Read the basic configuration.
     */
    char szAddress[1024];
    int rc = pHlp->pfnCFGMQueryString(pCfg, "Address", szAddress, sizeof(szAddress));
    AssertRCReturn(rc, rc);

    char szBackend[64];
    rc = pHlp->pfnCFGMQueryString(pCfg, "Backend", szBackend, sizeof(szBackend));
    AssertRCReturn(rc, rc);

    /*
     * Select backend and open the device.
     */
    rc = VERR_NOT_FOUND;
    for (unsigned i = 0; i < RT_ELEMENTS(g_aUsbProxies); i++)
    {
        if (!RTStrICmp(szBackend, g_aUsbProxies[i]->pszName))
        {
            pThis->pOps = g_aUsbProxies[i];
            rc = VINF_SUCCESS;
            break;
        }
    }
    if (RT_FAILURE(rc))
        return PDMUSB_SET_ERROR(pUsbIns, rc, N_("USBProxy: Failed to find backend"));

    pThis->pvInstanceDataR3 = RTMemAllocZ(pThis->pOps->cbBackend);
    if (!pThis->pvInstanceDataR3)
        return PDMUSB_SET_ERROR(pUsbIns, VERR_NO_MEMORY, N_("USBProxy: can't allocate memory for host backend"));

    rc = pThis->pOps->pfnOpen(pThis, szAddress);
    if (RT_FAILURE(rc))
    {
        LogRel(("usbProxyConstruct: Failed to open '%s', rc=%Rrc\n", szAddress, rc));
        return rc;
    }
    pThis->fOpened = true;

    /*
     * Get the device descriptor and format the device name (for logging).
     */
    if (!usbProxyGetDeviceDesc(pThis, &pThis->DevDesc))
    {
        Log(("usbProxyConstruct: usbProxyGetDeviceDesc failed\n"));
        return VERR_READ_ERROR;
    }

    RTStrAPrintf(&pUsbIns->pszName, "%p[proxy %04x:%04x]", pThis, pThis->DevDesc.idVendor, pThis->DevDesc.idProduct); /** @todo append the user comment */
    AssertReturn(pUsbIns->pszName,  VERR_NO_MEMORY);

    /*
     * Get config descriptors.
     */
    size_t cbConfigs = pThis->DevDesc.bNumConfigurations * sizeof(pThis->paCfgDescs[0]);
    pThis->paCfgDescs = (PVUSBDESCCONFIGEX)RTMemAllocZ(cbConfigs);
    AssertReturn(pThis->paCfgDescs, VERR_NO_MEMORY);

    unsigned i;
    for (i = 0; i < pThis->DevDesc.bNumConfigurations; i++)
        if (!copy_config(pThis, i, (PVUSBDESCCONFIGEX)&pThis->paCfgDescs[i]))
            break;
    if (i < pThis->DevDesc.bNumConfigurations)
    {
        Log(("usbProxyConstruct: copy_config failed, i=%d\n", i));
        return VERR_READ_ERROR;
    }

    /*
     * Pickup best matching global configuration for this device.
     * The global configuration is organized like this:
     *
     *  GlobalConfig/Whatever/
     *                       |- idVendor  = 300
     *                       |- idProduct = 300
     *                       - Config/
     *
     * The first level contains filter attributes which we stuff into a USBFILTER
     * structure and match against the device info that's available. The highest
     * ranked match is will be used. If nothing is found, the values will be
     * queried from the GlobalConfig node (simplifies code and might actually
     * be useful).
     */
    PCFGMNODE pCfgGlobalDev = pCfgGlobal;
    PCFGMNODE pCur = pHlp->pfnCFGMGetFirstChild(pCfgGlobal);
    if (pCur)
    {
        /*
         * Create a device filter from the device configuration
         * descriptor ++. No strings currently.
         */
        USBFILTER Device;
        USBFilterInit(&Device, USBFILTERTYPE_CAPTURE);
        rc = USBFilterSetNumExact(&Device, USBFILTERIDX_VENDOR_ID,          pThis->DevDesc.idVendor, true); AssertRC(rc);
        rc = USBFilterSetNumExact(&Device, USBFILTERIDX_PRODUCT_ID,         pThis->DevDesc.idProduct, true); AssertRC(rc);
        rc = USBFilterSetNumExact(&Device, USBFILTERIDX_DEVICE_REV,         pThis->DevDesc.bcdDevice, true); AssertRC(rc);
        rc = USBFilterSetNumExact(&Device, USBFILTERIDX_DEVICE_CLASS,       pThis->DevDesc.bDeviceClass, true); AssertRC(rc);
        rc = USBFilterSetNumExact(&Device, USBFILTERIDX_DEVICE_SUB_CLASS,   pThis->DevDesc.bDeviceSubClass, true); AssertRC(rc);
        rc = USBFilterSetNumExact(&Device, USBFILTERIDX_DEVICE_PROTOCOL,    pThis->DevDesc.bDeviceProtocol, true); AssertRC(rc);
        /** @todo manufacturer, product and serial strings */

        int iBestMatchRate = -1;
        PCFGMNODE pBestMatch = NULL;
        for (pCur = pHlp->pfnCFGMGetFirstChild(pCfgGlobal); pCur; pCur = pHlp->pfnCFGMGetNextChild(pCur))
        {
            /*
             * Construct a filter from the attributes in the node.
             */
            USBFILTER Filter;
            USBFilterInit(&Filter, USBFILTERTYPE_CAPTURE);

            /* numeric */
            if (    RT_FAILURE(usbProxyQueryNum(&Filter, USBFILTERIDX_VENDOR_ID,        pHlp, pCur, "idVendor",        "idVendorExpr"))
                ||  RT_FAILURE(usbProxyQueryNum(&Filter, USBFILTERIDX_PRODUCT_ID,       pHlp, pCur, "idProduct",       "idProcutExpr"))
                ||  RT_FAILURE(usbProxyQueryNum(&Filter, USBFILTERIDX_DEVICE_REV,       pHlp, pCur, "bcdDevice",       "bcdDeviceExpr"))
                ||  RT_FAILURE(usbProxyQueryNum(&Filter, USBFILTERIDX_DEVICE_CLASS,     pHlp, pCur, "bDeviceClass",    "bDeviceClassExpr"))
                ||  RT_FAILURE(usbProxyQueryNum(&Filter, USBFILTERIDX_DEVICE_SUB_CLASS, pHlp, pCur, "bDeviceSubClass", "bDeviceSubClassExpr"))
                ||  RT_FAILURE(usbProxyQueryNum(&Filter, USBFILTERIDX_DEVICE_PROTOCOL,  pHlp, pCur, "bDeviceProtocol", "bDeviceProtocolExpr")))
                continue; /* skip it */

            /* strings */
            /** @todo manufacturer, product and serial strings */

            /* ignore unknown config values, but not without bitching. */
            if (!pHlp->pfnCFGMAreValuesValid(pCur,
                                             "idVendor\0idVendorExpr\0"
                                             "idProduct\0idProductExpr\0"
                                             "bcdDevice\0bcdDeviceExpr\0"
                                             "bDeviceClass\0bDeviceClassExpr\0"
                                             "bDeviceSubClass\0bDeviceSubClassExpr\0"
                                             "bDeviceProtocol\0bDeviceProtocolExpr\0"))
                LogRel(("usbProxyConstruct: Unknown value(s) in config filter (ignored)!\n"));

            /*
             * Try match it and on match see if it has is a higher rate hit
             * than the previous match. Quit if its a 100% match.
             */
            int iRate = USBFilterMatchRated(&Filter, &Device);
            if (iRate > iBestMatchRate)
            {
                pBestMatch = pCur;
                iBestMatchRate = iRate;
                if (iRate >= 100)
                    break;
            }
        }
        if (pBestMatch)
            pCfgGlobalDev = pHlp->pfnCFGMGetChild(pBestMatch, "Config");
        if (pCfgGlobalDev)
            pCfgGlobalDev = pCfgGlobal;
    }

    /*
     * Query the rest of the configuration using the global as fallback.
     */
    rc = pHlp->pfnCFGMQueryU32(pCfg, "MaskedIfs", &pThis->fMaskedIfs);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        rc = pHlp->pfnCFGMQueryU32(pCfgGlobalDev, "MaskedIfs", &pThis->fMaskedIfs);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        pThis->fMaskedIfs = 0;
    else
        AssertRCReturn(rc, rc);

    bool fForce11Device;
    rc = pHlp->pfnCFGMQueryBool(pCfg, "Force11Device", &fForce11Device);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        rc = pHlp->pfnCFGMQueryBool(pCfgGlobalDev, "Force11Device", &fForce11Device);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        fForce11Device = false;
    else
        AssertRCReturn(rc, rc);

    bool fForce11PacketSize;
    rc = pHlp->pfnCFGMQueryBool(pCfg, "Force11PacketSize", &fForce11PacketSize);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        rc = pHlp->pfnCFGMQueryBool(pCfgGlobalDev, "Force11PacketSize", &fForce11PacketSize);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        fForce11PacketSize = false;
    else
        AssertRCReturn(rc, rc);

    bool fEditAudioSyncEp;
    rc = pHlp->pfnCFGMQueryBool(pCfg, "EditAudioSyncEp", &fEditAudioSyncEp);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        rc = pHlp->pfnCFGMQueryBool(pCfgGlobalDev, "EditAudioSyncEp", &fEditAudioSyncEp);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        fEditAudioSyncEp = true;    /* NB: On by default! */
    else
        AssertRCReturn(rc, rc);

    bool fEditRemoteWake;
    rc = pHlp->pfnCFGMQueryBool(pCfg, "EditRemoteWake", &fEditRemoteWake);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        rc = pHlp->pfnCFGMQueryBool(pCfgGlobalDev, "EditRemoteWake", &fEditRemoteWake);
    if (rc == VERR_CFGM_VALUE_NOT_FOUND)
        fEditRemoteWake = true;    /* NB: On by default! */
    else
        AssertRCReturn(rc, rc);

    /*
     * If we're masking interfaces, edit the descriptors.
     */
    bool fEdited = pThis->fMaskedIfs != 0;
    if (pThis->fMaskedIfs)
        usbProxyDevEditOutMaskedIfs(pThis);

    /*
     * Do 2.0 -> 1.1 device edits if requested to do so.
     */
    if (    fForce11PacketSize
        &&  pThis->DevDesc.bcdUSB >= 0x0200)
    {
        PVUSBDESCCONFIGEX paCfgs = pThis->paCfgDescs;
        for (unsigned iCfg = 0; iCfg < pThis->DevDesc.bNumConfigurations; iCfg++)
        {
            PVUSBINTERFACE paIfs = (PVUSBINTERFACE)paCfgs[iCfg].paIfs;
            for (unsigned iIf = 0; iIf < paCfgs[iCfg].Core.bNumInterfaces; iIf++)
                for (uint32_t iAlt = 0; iAlt < paIfs[iIf].cSettings; iAlt++)
                {
                    /*
                     * USB 1.1 defines the max for control, interrupt and bulk to be 64 bytes.
                     * While isochronous has a max of 1023 bytes.
                     */
                    PVUSBDESCENDPOINTEX paEps = (PVUSBDESCENDPOINTEX)paIfs[iIf].paSettings[iAlt].paEndpoints;
                    if (!paEps)
                        continue;

                    for (unsigned iEp = 0; iEp < paIfs[iIf].paSettings[iAlt].Core.bNumEndpoints; iEp++)
                    {
                        const uint16_t cbMax = (paEps[iEp].Core.bmAttributes & 3) == 1 /* isoc */
                                             ? 1023
                                             : 64;
                        if (paEps[iEp].Core.wMaxPacketSize > cbMax)
                        {
                            Log(("usb-proxy: pProxyDev=%s correcting wMaxPacketSize from %#x to %#x (mainly for vista)\n",
                                 pUsbIns->pszName, paEps[iEp].Core.wMaxPacketSize, cbMax));
                            paEps[iEp].Core.wMaxPacketSize = cbMax;
                            fEdited = true;
                        }
                    }
                }
        }
    }

    if (    fForce11Device
        &&  pThis->DevDesc.bcdUSB == 0x0200)
    {
        /*
         * Discourages windows from helping you find a 2.0 port.
         */
        Log(("usb-proxy: %s correcting USB version 2.0 to 1.1 (to avoid Windows warning)\n", pUsbIns->pszName));
        pThis->DevDesc.bcdUSB = 0x110;
        fEdited = true;
    }


    /*
     * Turn asynchronous audio endpoints into synchronous ones, see @bugref{8769}
     */
    if (fEditAudioSyncEp)
    {
        PVUSBDESCCONFIGEX paCfgs = pThis->paCfgDescs;
        for (unsigned iCfg = 0; iCfg < pThis->DevDesc.bNumConfigurations; iCfg++)
        {
            PVUSBINTERFACE paIfs = (PVUSBINTERFACE)paCfgs[iCfg].paIfs;
            for (unsigned iIf = 0; iIf < paCfgs[iCfg].Core.bNumInterfaces; iIf++)
                for (uint32_t iAlt = 0; iAlt < paIfs[iIf].cSettings; iAlt++)
                {
                    /* If not an audio class interface, skip. */
                    if (paIfs[iIf].paSettings[iAlt].Core.bInterfaceClass != 1)
                        continue;

                    /* If not a streaming interface, skip. */
                    if (paIfs[iIf].paSettings[iAlt].Core.bInterfaceSubClass != 2)
                        continue;

                    PVUSBDESCENDPOINTEX paEps = (PVUSBDESCENDPOINTEX)paIfs[iIf].paSettings[iAlt].paEndpoints;
                    if (!paEps)
                        continue;

                    for (unsigned iEp = 0; iEp < paIfs[iIf].paSettings[iAlt].Core.bNumEndpoints; iEp++)
                    {
                        /* isoch/asynch/data*/
                        if ((paEps[iEp].Core.bmAttributes == 5) && (paEps[iEp].Core.bLength == 9))
                        {
                            uint8_t *pbExtra = (uint8_t *)paEps[iEp].pvMore;    /* unconst*/
                            if (pbExtra[1] == 0)
                                continue;   /* If bSynchAddress is zero, leave the descriptor alone. */

                            Log(("usb-proxy: pProxyDev=%s async audio with bmAttr=%02X [%02X, %02X] on EP %02X\n",
                                 pUsbIns->pszName, paEps[iEp].Core.bmAttributes, pbExtra[0], pbExtra[1], paEps[iEp].Core.bEndpointAddress));
                            paEps[iEp].Core.bmAttributes = 0xD; /* isoch/synch/data*/
                            pbExtra[1] = 0; /* Clear bSynchAddress. */
                            fEdited = true;
                            LogRel(("VUSB: Modified '%s' async audio endpoint 0x%02x\n", pUsbIns->pszName, paEps[iEp].Core.bEndpointAddress));
                        }
                    }
                }
        }
    }

    /*
     * Disable remote wakeup capability, see @bugref{9839}. This is done on
     * a device/configuration level, no need to dig too deep through the descriptors.
     * On most backends, we can't perform a real selective suspend, and more importantly
     * can't receive a remote wake notification. If a guest suspends the device and waits
     * for a remote wake, the device is effectively dead.
     */
    if (fEditRemoteWake)
    {
        PVUSBDESCCONFIGEX paCfgs = pThis->paCfgDescs;
        for (unsigned iCfg = 0; iCfg < pThis->DevDesc.bNumConfigurations; iCfg++)
        {
            Log(("usb-proxy: pProxyDev=%s configuration %d with bmAttr=%02X\n",
                 pUsbIns->pszName, paCfgs[iCfg].Core.bmAttributes, iCfg));
            if (paCfgs[iCfg].Core.bmAttributes & RT_BIT(5))
            {
                 paCfgs[iCfg].Core.bmAttributes = paCfgs[iCfg].Core.bmAttributes & ~RT_BIT(5); /* Remote wakeup. */
                 fEdited = true;
                 LogRel(("VUSB: Disabled '%s' remote wakeup for configuration %d\n", pUsbIns->pszName, iCfg));
            }
        }
    }

    /*
     * Init the PDM/VUSB descriptor cache.
     */
    pThis->DescCache.pDevice = &pThis->DevDesc;
    pThis->DescCache.paConfigs = pThis->paCfgDescs;
    pThis->DescCache.paLanguages = NULL;
    pThis->DescCache.cLanguages = 0;
    pThis->DescCache.fUseCachedDescriptors = fEdited;
    pThis->DescCache.fUseCachedStringsDescriptors = false;

    /*
     * Call the backend if it wishes to do some more initializing
     * after we've read the config and descriptors.
     */
    if (pThis->pOps->pfnInit)
    {
        rc = pThis->pOps->pfnInit(pThis);
        if (RT_FAILURE(rc))
            return rc;
    }
    pThis->fInited = true;

    /*
     * We're good!
     */
    Log(("usb-proxy: created pProxyDev=%s address '%s' fMaskedIfs=%#x (rc=%Rrc)\n",
         pUsbIns->pszName, szAddress, pThis->fMaskedIfs, rc));
    return VINF_SUCCESS;
}


/**
 * The USB proxy device registration record.
 */
const PDMUSBREG g_UsbDevProxy =
{
    /* u32Version */
    PDM_USBREG_VERSION,
    /* szName */
    "USBProxy",
    /* pszDescription */
    "USB Proxy Device.",
    /* fFlags */
    0,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(USBPROXYDEV),
    /* pfnConstruct */
    usbProxyConstruct,
    /* pfnDestruct */
    usbProxyDestruct,
    /* pfnVMInitComplete */
    NULL,
    /* pfnVMPowerOn */
    NULL,
    /* pfnVMReset */
    NULL,
    /* pfnVMSuspend */
    NULL,
    /* pfnVMResume */
    NULL,
    /* pfnVMPowerOff */
    NULL,
    /* pfnHotPlugged */
    NULL,
    /* pfnHotUnplugged */
    NULL,
    /* pfnDriverAttach */
    NULL,
    /* pfnDriverDetach */
    NULL,
    /* pfnQueryInterface */
    NULL,
    /* pfnUsbReset */
    usbProxyDevReset,
    /* pfnUsbGetDescriptorCache */
    usbProxyDevGetDescriptorCache,
    /* pfnUsbSetConfiguration */
    usbProxyDevSetConfiguration,
    /* pfnUsbSetInterface */
    usbProxyDevSetInterface,
    /* pfnUsbClearHaltedEndpoint */
    usbProxyDevClearHaltedEndpoint,
    /* pfnUrbNew */
    NULL,
    /* pfnUrbQueue */
    usbProxyDevUrbQueue,
    /* pfnUrbCancel */
    usbProxyDevUrbCancel,
    /* pfnUrbReap */
    usbProxyDevUrbReap,
    /* pfnWakeup */
    usbProxyDevWakeup,

    /* u32TheEnd */
    PDM_USBREG_VERSION
};

