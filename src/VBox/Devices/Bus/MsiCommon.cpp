/* $Id: MsiCommon.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * MSI support routines
 *
 * @todo Straighten up this file!!
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

#include "MsiCommon.h"
#include "PciInline.h"
#include "DevPciInternal.h"


DECLINLINE(uint16_t) msiGetMessageControl(PPDMPCIDEV pDev)
{
    uint32_t idxMessageControl = pDev->Int.s.u8MsiCapOffset + VBOX_MSI_CAP_MESSAGE_CONTROL;
#ifdef IN_RING3
    if (pciDevIsPassthrough(pDev) && pDev->Int.s.pfnConfigRead)
    {
        uint32_t u32Value = 0;
        VBOXSTRICTRC rcStrict = pDev->Int.s.pfnConfigRead(pDev->Int.s.CTX_SUFF(pDevIns), pDev, idxMessageControl, 2, &u32Value);
        AssertRCSuccess(VBOXSTRICTRC_VAL(rcStrict));
        return (uint16_t)u32Value;
    }
#endif
    return PCIDevGetWord(pDev, idxMessageControl);
}

DECLINLINE(bool) msiIs64Bit(PPDMPCIDEV pDev)
{
    return pciDevIsMsi64Capable(pDev);
}

/** @todo r=klaus This design assumes that the config space cache is always
 * up to date, which is a wrong assumption for the "emulate passthrough" case
 * where only the callbacks give the correct data. */
DECLINLINE(uint32_t *) msiGetMaskBits(PPDMPCIDEV pDev)
{
    uint8_t iOff = msiIs64Bit(pDev) ? VBOX_MSI_CAP_MASK_BITS_64 : VBOX_MSI_CAP_MASK_BITS_32;
    /* devices may have no masked/pending support */
    if (iOff >= pDev->Int.s.u8MsiCapSize)
        return NULL;
    iOff += pDev->Int.s.u8MsiCapOffset;
    return (uint32_t*)(pDev->abConfig + iOff);
}

/** @todo r=klaus This design assumes that the config space cache is always
 * up to date, which is a wrong assumption for the "emulate passthrough" case
 * where only the callbacks give the correct data. */
DECLINLINE(uint32_t*) msiGetPendingBits(PPDMPCIDEV pDev)
{
    uint8_t iOff = msiIs64Bit(pDev) ? VBOX_MSI_CAP_PENDING_BITS_64 : VBOX_MSI_CAP_PENDING_BITS_32;
    /* devices may have no masked/pending support */
    if (iOff >= pDev->Int.s.u8MsiCapSize)
        return NULL;
    iOff += pDev->Int.s.u8MsiCapOffset;
    return (uint32_t*)(pDev->abConfig + iOff);
}

DECLINLINE(bool) msiIsEnabled(PPDMPCIDEV pDev)
{
    return (msiGetMessageControl(pDev) & VBOX_PCI_MSI_FLAGS_ENABLE) != 0;
}

DECLINLINE(uint8_t) msiGetMme(PPDMPCIDEV pDev)
{
    return (msiGetMessageControl(pDev) & VBOX_PCI_MSI_FLAGS_QSIZE) >> 4;
}

DECLINLINE(RTGCPHYS) msiGetMsiAddress(PPDMPCIDEV pDev)
{
    if (msiIs64Bit(pDev))
    {
        uint32_t lo = PCIDevGetDWord(pDev, pDev->Int.s.u8MsiCapOffset + VBOX_MSI_CAP_MESSAGE_ADDRESS_LO);
        uint32_t hi = PCIDevGetDWord(pDev, pDev->Int.s.u8MsiCapOffset + VBOX_MSI_CAP_MESSAGE_ADDRESS_HI);
        return RT_MAKE_U64(lo, hi);
    }
    return PCIDevGetDWord(pDev, pDev->Int.s.u8MsiCapOffset + VBOX_MSI_CAP_MESSAGE_ADDRESS_32);
}

DECLINLINE(uint32_t) msiGetMsiData(PPDMPCIDEV pDev, int32_t iVector)
{
    int16_t  iOff = msiIs64Bit(pDev) ? VBOX_MSI_CAP_MESSAGE_DATA_64 : VBOX_MSI_CAP_MESSAGE_DATA_32;
    uint16_t lo = PCIDevGetWord(pDev, pDev->Int.s.u8MsiCapOffset + iOff);

    // vector encoding into lower bits of message data
    uint8_t bits = msiGetMme(pDev);
    uint16_t uMask = ((1 << bits) - 1);
    lo &= ~uMask;
    lo |= iVector & uMask;

    return RT_MAKE_U32(lo, 0);
}

#ifdef IN_RING3

DECLINLINE(bool) msiR3BitJustCleared(uint32_t uOldValue, uint32_t uNewValue, uint32_t uMask)
{
    return !!(uOldValue & uMask) && !(uNewValue & uMask);
}

DECLINLINE(bool) msiR3BitJustSet(uint32_t uOldValue, uint32_t uNewValue, uint32_t uMask)
{
    return !(uOldValue & uMask) && !!(uNewValue & uMask);
}

/**
 * PCI config space accessors for MSI registers.
 */
void MsiR3PciConfigWrite(PPDMDEVINS pDevIns, PCPDMPCIHLP pPciHlp, PPDMPCIDEV pDev,
                         uint32_t u32Address, uint32_t val, unsigned len)
{
    int32_t iOff = u32Address - pDev->Int.s.u8MsiCapOffset;
    Assert(iOff >= 0 && (pciDevIsMsiCapable(pDev) && iOff < pDev->Int.s.u8MsiCapSize));

    Log2(("MsiR3PciConfigWrite: %d <- %x (%d)\n", iOff, val, len));

    uint32_t uAddr = u32Address;
    bool f64Bit = msiIs64Bit(pDev);

    for (uint32_t i = 0; i < len; i++)
    {
        uint32_t reg = i + iOff;
        uint8_t u8Val = (uint8_t)val;
        switch (reg)
        {
            case 0: /* Capability ID, ro */
            case 1: /* Next pointer,  ro */
                break;
            case VBOX_MSI_CAP_MESSAGE_CONTROL:
                /* don't change read-only bits: 1-3,7 */
                u8Val &= UINT8_C(~0x8e);
                pDev->abConfig[uAddr] = u8Val | (pDev->abConfig[uAddr] & UINT8_C(0x8e));
                break;
            case VBOX_MSI_CAP_MESSAGE_CONTROL + 1:
                /* don't change read-only bit 8, and reserved 9-15 */
                break;
            default:
                if (pDev->abConfig[uAddr] != u8Val)
                {
                    int32_t maskUpdated = -1;

                    /* If we're enabling masked vector, and have pending messages
                       for this vector, we have to send this message now */
                    if (    !f64Bit
                         && (reg >= VBOX_MSI_CAP_MASK_BITS_32)
                         && (reg < VBOX_MSI_CAP_MASK_BITS_32 + 4)
                        )
                    {
                        maskUpdated = reg - VBOX_MSI_CAP_MASK_BITS_32;
                    }
                    if (    f64Bit
                         && (reg >= VBOX_MSI_CAP_MASK_BITS_64)
                         && (reg < VBOX_MSI_CAP_MASK_BITS_64 + 4)
                       )
                    {
                        maskUpdated = reg - VBOX_MSI_CAP_MASK_BITS_64;
                    }

                    if (maskUpdated != -1 && msiIsEnabled(pDev))
                    {
                        uint32_t*  puPending = msiGetPendingBits(pDev);
                        for (int iBitNum = 0; iBitNum < 8; iBitNum++)
                        {
                            int32_t iBit = 1 << iBitNum;
                            uint32_t uVector = maskUpdated*8 + iBitNum;

                            if (msiR3BitJustCleared(pDev->abConfig[uAddr], u8Val, iBit))
                            {
                                Log(("msi: mask updated bit %d@%x (%d)\n", iBitNum, uAddr, maskUpdated));

                                /* To ensure that we're no longer masked */
                                pDev->abConfig[uAddr] &= ~iBit;
                                if ((*puPending & (1 << uVector)) != 0)
                                {
                                    Log(("msi: notify earlier masked pending vector: %d\n", uVector));
                                    MsiNotify(pDevIns, pPciHlp, pDev, uVector, PDM_IRQ_LEVEL_HIGH, 0 /*uTagSrc*/);
                                }
                            }
                            if (msiR3BitJustSet(pDev->abConfig[uAddr], u8Val, iBit))
                            {
                                Log(("msi: mask vector: %d\n", uVector));
                            }
                        }
                    }

                    pDev->abConfig[uAddr] = u8Val;
                }
        }
        uAddr++;
        val >>= 8;
    }
}

/**
 * Initializes MSI support for the given PCI device.
 */
int MsiR3Init(PPDMPCIDEV pDev, PPDMMSIREG pMsiReg)
{
    if (pMsiReg->cMsiVectors == 0)
         return VINF_SUCCESS;

    /* XXX: done in pcirawAnalyzePciCaps() */
    if (pciDevIsPassthrough(pDev))
        return VINF_SUCCESS;

    uint16_t   cVectors    = pMsiReg->cMsiVectors;
    uint8_t    iCapOffset  = pMsiReg->iMsiCapOffset;
    uint8_t    iNextOffset = pMsiReg->iMsiNextOffset;
    bool       f64bit      = pMsiReg->fMsi64bit;
    bool       fNoMasking  = pMsiReg->fMsiNoMasking;
    uint16_t   iFlags      = 0;

    Assert(iCapOffset != 0 && iCapOffset < 0xff && iNextOffset < 0xff);

    if (!fNoMasking)
    {
        int iMmc;

        /* Compute multiple-message capable bitfield */
        for (iMmc = 0; iMmc < 6; iMmc++)
        {
            if ((1 << iMmc) >= cVectors)
                break;
        }

        if ((cVectors > VBOX_MSI_MAX_ENTRIES) || (1 << iMmc) < cVectors)
            return VERR_TOO_MUCH_DATA;

        /* We support per-vector masking */
        iFlags |= VBOX_PCI_MSI_FLAGS_MASKBIT;
        /* How many vectors we're capable of */
        iFlags |= iMmc;
    }

    if (f64bit)
        iFlags |= VBOX_PCI_MSI_FLAGS_64BIT;

    pDev->Int.s.u8MsiCapOffset = iCapOffset;
    pDev->Int.s.u8MsiCapSize   = f64bit ? VBOX_MSI_CAP_SIZE_64 : VBOX_MSI_CAP_SIZE_32;

    PCIDevSetByte(pDev,  iCapOffset + 0, VBOX_PCI_CAP_ID_MSI);
    PCIDevSetByte(pDev,  iCapOffset + 1, iNextOffset); /* next */
    PCIDevSetWord(pDev,  iCapOffset + VBOX_MSI_CAP_MESSAGE_CONTROL, iFlags);

    if (!fNoMasking)
    {
        *msiGetMaskBits(pDev)    = 0;
        *msiGetPendingBits(pDev) = 0;
    }

    pciDevSetMsiCapable(pDev);
    if (f64bit)
        pciDevSetMsi64Capable(pDev);

    return VINF_SUCCESS;
}

#endif /* IN_RING3 */


/**
 * Checks if MSI is enabled for the given PCI device.
 *
 * (Must use MSINotify() for notifications when true.)
 */
bool MsiIsEnabled(PPDMPCIDEV pDev)
{
    return pciDevIsMsiCapable(pDev) && msiIsEnabled(pDev);
}

/**
 * Device notification (aka interrupt).
 */
void MsiNotify(PPDMDEVINS pDevIns, PCPDMPCIHLP pPciHlp, PPDMPCIDEV pDev, int iVector, int iLevel, uint32_t uTagSrc)
{
    AssertMsg(msiIsEnabled(pDev), ("Must be enabled to use that"));

    uint32_t uMask;
    uint32_t *puPending = msiGetPendingBits(pDev);
    if (puPending)
    {
        uint32_t *puMask = msiGetMaskBits(pDev);
        AssertPtr(puMask);
        uMask = *puMask;
        LogFlow(("MsiNotify: %d pending=%x mask=%x\n", iVector, *puPending, uMask));
    }
    else
    {
        uMask = 0;
        LogFlow(("MsiNotify: %d\n", iVector));
    }

    /* We only trigger MSI on level up */
    if ((iLevel & PDM_IRQ_LEVEL_HIGH) == 0)
    {
        /** @todo maybe clear pending interrupts on level down? */
#if 0
        if (puPending)
        {
            *puPending &= ~(1<<iVector);
            LogFlow(("msi: clear pending %d, now %x\n", iVector, *puPending));
        }
#endif
        return;
    }

    if ((uMask & (1<<iVector)) != 0)
    {
        *puPending |= (1<<iVector);
        LogFlow(("msi: %d is masked, mark pending, now %x\n", iVector, *puPending));
        return;
    }

    MSIMSG Msi;
    Msi.Addr.u64 = msiGetMsiAddress(pDev);
    Msi.Data.u32 = msiGetMsiData(pDev, iVector);

    if (puPending)
        *puPending &= ~(1<<iVector);

    PPDMDEVINS pDevInsBus = pPciHlp->pfnGetBusByNo(pDevIns, pDev->Int.s.idxPdmBus);
    Assert(pDevInsBus);
    PDEVPCIBUS pBus = PDMINS_2_DATA(pDevInsBus, PDEVPCIBUS);
    uint16_t const uBusDevFn = PCIBDF_MAKE(pBus->iBus, pDev->uDevFn);

    Assert(pPciHlp->pfnIoApicSendMsi != NULL);
    pPciHlp->pfnIoApicSendMsi(pDevIns, uBusDevFn, &Msi, uTagSrc);
}

