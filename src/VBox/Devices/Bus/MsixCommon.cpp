/* $Id: MsixCommon.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * MSI-X support routines
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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


#define LOG_GROUP LOG_GROUP_DEV_PCI
#define PDMPCIDEV_INCLUDE_PRIVATE  /* Hack to get pdmpcidevint.h included at the right point. */
#include <VBox/pci.h>
#include <VBox/msi.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/log.h>
#include <VBox/vmm/mm.h>
#include <VBox/AssertGuest.h>

#include <iprt/assert.h>

#include "MsiCommon.h"
#include "DevPciInternal.h"
#include "PciInline.h"

typedef struct
{
    uint32_t  u32MsgAddressLo;
    uint32_t  u32MsgAddressHi;
    uint32_t  u32MsgData;
    uint32_t  u32VectorControl;
} MsixTableRecord;
AssertCompileSize(MsixTableRecord, VBOX_MSIX_ENTRY_SIZE);


/** @todo use accessors so that raw PCI devices work correctly with MSI-X. */
DECLINLINE(uint16_t)  msixGetMessageControl(PPDMPCIDEV pDev)
{
    return PCIDevGetWord(pDev, pDev->Int.s.u8MsixCapOffset + VBOX_MSIX_CAP_MESSAGE_CONTROL);
}

DECLINLINE(bool)      msixIsEnabled(PPDMPCIDEV pDev)
{
    return (msixGetMessageControl(pDev) & VBOX_PCI_MSIX_FLAGS_ENABLE) != 0;
}

DECLINLINE(bool)      msixIsMasked(PPDMPCIDEV pDev)
{
    return (msixGetMessageControl(pDev) & VBOX_PCI_MSIX_FLAGS_FUNCMASK) != 0;
}

#ifdef IN_RING3
DECLINLINE(uint16_t)  msixTableSize(PPDMPCIDEV pDev)
{
    return (msixGetMessageControl(pDev) & 0x3ff) + 1;
}
#endif

DECLINLINE(uint8_t *) msixGetPageOffset(PPDMPCIDEV pDev, uint32_t off)
{
    return &pDev->abMsixState[off];
}

DECLINLINE(MsixTableRecord *) msixGetVectorRecord(PPDMPCIDEV pDev, uint32_t iVector)
{
    return (MsixTableRecord *)msixGetPageOffset(pDev, iVector * VBOX_MSIX_ENTRY_SIZE);
}

DECLINLINE(RTGCPHYS)  msixGetMsiAddress(PPDMPCIDEV pDev, uint32_t iVector)
{
    MsixTableRecord *pRec = msixGetVectorRecord(pDev, iVector);
    return RT_MAKE_U64(pRec->u32MsgAddressLo & ~UINT32_C(0x3), pRec->u32MsgAddressHi);
}

DECLINLINE(uint32_t)  msixGetMsiData(PPDMPCIDEV pDev, uint32_t iVector)
{
    return msixGetVectorRecord(pDev, iVector)->u32MsgData;
}

DECLINLINE(uint32_t)  msixIsVectorMasked(PPDMPCIDEV pDev, uint32_t iVector)
{
    return (msixGetVectorRecord(pDev, iVector)->u32VectorControl & 0x1) != 0;
}

DECLINLINE(uint8_t *) msixPendingByte(PPDMPCIDEV pDev, uint32_t iVector)
{
    return msixGetPageOffset(pDev, pDev->Int.s.offMsixPba + iVector / 8);
}

DECLINLINE(void)      msixSetPending(PPDMPCIDEV pDev, uint32_t iVector)
{
    *msixPendingByte(pDev, iVector) |= (1 << (iVector & 0x7));
}

DECLINLINE(void)      msixClearPending(PPDMPCIDEV pDev, uint32_t iVector)
{
    *msixPendingByte(pDev, iVector) &= ~(1 << (iVector & 0x7));
}

#ifdef IN_RING3

DECLINLINE(bool)      msixR3IsPending(PPDMPCIDEV pDev, uint32_t iVector)
{
    return (*msixPendingByte(pDev, iVector) & (1 << (iVector & 0x7))) != 0;
}

static void msixR3CheckPendingVector(PPDMDEVINS pDevIns, PCPDMPCIHLP pPciHlp, PPDMPCIDEV pDev, uint32_t iVector)
{
    if (msixR3IsPending(pDev, iVector) && !msixIsVectorMasked(pDev, iVector))
        MsixNotify(pDevIns, pPciHlp, pDev, iVector, 1 /* iLevel */, 0 /*uTagSrc*/);
}

/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
static DECLCALLBACK(VBOXSTRICTRC) msixR3MMIORead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    PPDMPCIDEV pPciDev = (PPDMPCIDEV)pvUser;
    RT_NOREF(pDevIns);

    /* Validate IOM behaviour. */
    Assert(cb == 4);
    Assert((off & 3) == 0);

    /* Do the read if it's within the MSI state. */
    ASSERT_GUEST_MSG_RETURN(off + cb <= pPciDev->Int.s.cbMsixRegion, ("Out of bounds access for the MSI-X region\n"),
                            VINF_IOM_MMIO_UNUSED_FF);
    *(uint32_t *)pv = *(uint32_t *)&pPciDev->abMsixState[off];

    LogFlowFunc(("off=%RGp cb=%d -> %#010RX32\n", off, cb, *(uint32_t *)pv));
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) msixR3MMIOWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PPDMPCIDEV pPciDev = (PPDMPCIDEV)pvUser;
    LogFlowFunc(("off=%RGp cb=%d %#010RX32\n", off, cb, *(uint32_t *)pv));

    /* Validate IOM behaviour. */
    Assert(cb == 4);
    Assert((off & 3) == 0);

    /* Do the write if it's within the MSI state. */
    ASSERT_GUEST_MSG_RETURN(off + cb <= pPciDev->Int.s.offMsixPba, ("Trying to write to PBA\n"),
                            VINF_SUCCESS);
    *(uint32_t *)&pPciDev->abMsixState[off] = *(uint32_t *)pv;

    /* (See MsixR3Init the setting up of pvPciBusPtrR3.) */
    msixR3CheckPendingVector(pDevIns, (PCPDMPCIHLP)pPciDev->Int.s.pvPciBusPtrR3, pPciDev, off / VBOX_MSIX_ENTRY_SIZE);
    return VINF_SUCCESS;
}

/**
 * Initalizes MSI-X support for the given PCI device.
 */
int MsixR3Init(PCPDMPCIHLP pPciHlp, PPDMPCIDEV pDev, PPDMMSIREG pMsiReg)
{
    if (pMsiReg->cMsixVectors == 0)
        return VINF_SUCCESS;

     /* We cannot init MSI-X on raw devices yet. */
    Assert(!pciDevIsPassthrough(pDev));

    uint16_t   cVectors    = pMsiReg->cMsixVectors;
    uint8_t    iCapOffset  = pMsiReg->iMsixCapOffset;
    uint8_t    iNextOffset = pMsiReg->iMsixNextOffset;
    uint8_t    iBar        = pMsiReg->iMsixBar;

    AssertMsgReturn(cVectors <= VBOX_MSIX_MAX_ENTRIES, ("Too many MSI-X vectors: %d\n", cVectors), VERR_TOO_MUCH_DATA);
    AssertMsgReturn(iBar <= 5, ("Using wrong BAR for MSI-X: %d\n", iBar), VERR_INVALID_PARAMETER);
    Assert(iCapOffset != 0 && iCapOffset < 0xff && iNextOffset < 0xff);

    uint16_t cbPba = cVectors / 8;
    if (cVectors % 8)
        cbPba++;
    uint16_t cbMsixRegion = RT_ALIGN_T(cVectors * sizeof(MsixTableRecord) + cbPba, _4K, uint16_t);
    AssertLogRelMsgReturn(cbMsixRegion <= pDev->cbMsixState,
                          ("%#x vs %#x (%s)\n", cbMsixRegion, pDev->cbMsixState, pDev->pszNameR3),
                          VERR_MISMATCH);

    /* If device is passthrough, BAR is registered using common mechanism. */
    if (!pciDevIsPassthrough(pDev))
    {
        /** @todo r=bird: This used to be IOMMMIO_FLAGS_READ_PASSTHRU |
         * IOMMMIO_FLAGS_WRITE_PASSTHRU with the callbacks asserting and
         * returning VERR_INTERNAL_ERROR on non-dword reads.  That is of
         * course certifiable insane behaviour.  So, instead I've changed it
         * so the callbacks only see dword reads and writes.  I'm not at all
         * sure about the read-missing behaviour, but it seems like a good
         * idea for now. */
        /** @todo r=bird: Shouldn't we at least handle writes in ring-0?   */
        int rc = PDMDevHlpPCIIORegionCreateMmio(pDev->Int.s.CTX_SUFF(pDevIns), iBar, cbMsixRegion, PCI_ADDRESS_SPACE_MEM,
                                                msixR3MMIOWrite, msixR3MMIORead, pDev,
                                                IOMMMIO_FLAGS_READ_DWORD | IOMMMIO_FLAGS_WRITE_DWORD_READ_MISSING,
                                                "MSI-X tables", &pDev->Int.s.hMmioMsix);
        AssertRCReturn(rc, rc);
    }

    uint16_t offTable = 0;
    uint16_t offPBA   = cVectors * sizeof(MsixTableRecord);

    pDev->Int.s.u8MsixCapOffset = iCapOffset;
    pDev->Int.s.u8MsixCapSize   = VBOX_MSIX_CAP_SIZE;
    pDev->Int.s.cbMsixRegion    = cbMsixRegion;
    pDev->Int.s.offMsixPba      = offPBA;

    /* R3 PCI helper */
    pDev->Int.s.pvPciBusPtrR3   = pPciHlp;

    PCIDevSetByte(pDev,  iCapOffset + 0, VBOX_PCI_CAP_ID_MSIX);
    PCIDevSetByte(pDev,  iCapOffset + 1, iNextOffset); /* next */
    PCIDevSetWord(pDev,  iCapOffset + VBOX_MSIX_CAP_MESSAGE_CONTROL, cVectors - 1);

    PCIDevSetDWord(pDev,  iCapOffset + VBOX_MSIX_TABLE_BIROFFSET, offTable | iBar);
    PCIDevSetDWord(pDev,  iCapOffset + VBOX_MSIX_PBA_BIROFFSET,   offPBA   | iBar);

    pciDevSetMsixCapable(pDev);

    return VINF_SUCCESS;
}

#endif /* IN_RING3 */

/**
 * Checks if MSI-X is enabled for the tiven PCI device.
 *
 * (Must use MSIXNotify() for notifications when true.)
 */
bool MsixIsEnabled(PPDMPCIDEV pDev)
{
    return pciDevIsMsixCapable(pDev) && msixIsEnabled(pDev);
}

/**
 * Device notification (aka interrupt).
 */
void MsixNotify(PPDMDEVINS pDevIns, PCPDMPCIHLP pPciHlp, PPDMPCIDEV pDev, int iVector, int iLevel, uint32_t uTagSrc)
{
    AssertMsg(msixIsEnabled(pDev), ("Must be enabled to use that"));

    Assert(pPciHlp->pfnIoApicSendMsi != NULL);

    /* We only trigger MSI-X on level up */
    if ((iLevel & PDM_IRQ_LEVEL_HIGH) == 0)
    {
        return;
    }

    // if this vector is somehow disabled
    if (msixIsMasked(pDev) || msixIsVectorMasked(pDev, iVector))
    {
        // mark pending bit
        msixSetPending(pDev, iVector);
        return;
    }

    // clear pending bit
    msixClearPending(pDev, iVector);

    MSIMSG Msi;
    Msi.Addr.u64 = msixGetMsiAddress(pDev, iVector);
    Msi.Data.u32 = msixGetMsiData(pDev, iVector);

    PPDMDEVINS pDevInsBus = pPciHlp->pfnGetBusByNo(pDevIns, pDev->Int.s.idxPdmBus);
    Assert(pDevInsBus);
    PDEVPCIBUS pBus = PDMINS_2_DATA(pDevInsBus, PDEVPCIBUS);
    uint16_t const uBusDevFn = PCIBDF_MAKE(pBus->iBus, pDev->uDevFn);

    pPciHlp->pfnIoApicSendMsi(pDevIns, uBusDevFn, &Msi, uTagSrc);
}

#ifdef IN_RING3

DECLINLINE(bool) msixR3BitJustCleared(uint32_t uOldValue, uint32_t uNewValue, uint32_t uMask)
{
    return !!(uOldValue & uMask) && !(uNewValue & uMask);
}


static void msixR3CheckPendingVectors(PPDMDEVINS pDevIns, PCPDMPCIHLP pPciHlp, PPDMPCIDEV pDev)
{
    for (uint32_t i = 0; i < msixTableSize(pDev); i++)
        msixR3CheckPendingVector(pDevIns, pPciHlp, pDev, i);
}

/**
 * PCI config space accessors for MSI-X.
 */
void MsixR3PciConfigWrite(PPDMDEVINS pDevIns, PCPDMPCIHLP pPciHlp, PPDMPCIDEV pDev, uint32_t u32Address, uint32_t val, unsigned len)
{
    int32_t iOff = u32Address - pDev->Int.s.u8MsixCapOffset;
    Assert(iOff >= 0 && (pciDevIsMsixCapable(pDev) && iOff < pDev->Int.s.u8MsixCapSize));

    Log2(("MsixR3PciConfigWrite: %d <- %x (%d)\n", iOff, val, len));

    uint32_t uAddr = u32Address;
    uint8_t u8NewVal;
    bool fJustEnabled = false;

    for (uint32_t i = 0; i < len; i++)
    {
        uint32_t reg = i + iOff;
        uint8_t u8Val = (uint8_t)val;
        switch (reg)
        {
            case 0: /* Capability ID, ro */
            case 1: /* Next pointer,  ro */
                break;
            case VBOX_MSIX_CAP_MESSAGE_CONTROL:
                /* don't change read-only bits: 0-7 */
                break;
            case VBOX_MSIX_CAP_MESSAGE_CONTROL + 1:
            {
                /* don't change read-only bits 8-13 */
                u8NewVal = (u8Val & UINT8_C(~0x3f)) | (pDev->abConfig[uAddr] & UINT8_C(0x3f));
                /* If just enabled globally - check pending vectors */
                fJustEnabled |= msixR3BitJustCleared(pDev->abConfig[uAddr], u8NewVal, VBOX_PCI_MSIX_FLAGS_ENABLE >> 8);
                fJustEnabled |= msixR3BitJustCleared(pDev->abConfig[uAddr], u8NewVal, VBOX_PCI_MSIX_FLAGS_FUNCMASK >> 8);
                pDev->abConfig[uAddr] = u8NewVal;
                break;
        }
            default:
                /* other fields read-only too */
                break;
        }
        uAddr++;
        val >>= 8;
    }

    if (fJustEnabled)
        msixR3CheckPendingVectors(pDevIns, pPciHlp, pDev);
}

#endif /* IN_RING3 */
