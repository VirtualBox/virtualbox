/* $Id: DevPIC.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevPIC - Intel 8259 Programmable Interrupt Controller (PIC) Device.
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
 * -------------------------------------------------------------------
 *
 * This code is based on:
 *
 * QEMU 8259 interrupt controller emulation
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_PIC
#include <VBox/vmm/pdmdev.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/string.h>

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def PIC_LOCK_RET
 * Acquires the PDM lock. This is a NOP if locking is disabled. */
#define PIC_LOCK_RET(a_pDevIns, a_pThisCC, rcBusy) \
    do { \
        int const rcLock = (a_pThisCC)->pPicHlp->pfnLock((a_pDevIns), rcBusy); \
        if (rcLock == VINF_SUCCESS) \
        { /* likely */ } \
        else \
            return rcLock; \
    } while (0)
/** @def PIC_UNLOCK
 * Releases the PDM lock. This is a NOP if locking is disabled. */
#define PIC_UNLOCK(a_pDevIns, a_pThisCC) \
    (a_pThisCC)->pPicHlp->pfnUnlock((a_pDevIns))


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The instance data of one (1) PIC.
 */
typedef struct PICSTATE
{
    uint8_t         last_irr;       /**< edge detection */
    uint8_t         irr;            /**< interrupt request register */
    uint8_t         imr;            /**< interrupt mask register */
    uint8_t         isr;            /**< interrupt service register */
    uint8_t         priority_add;   /**< highest irq priority */
    uint8_t         irq_base;
    uint8_t         read_reg_select;
    uint8_t         poll;
    uint8_t         special_mask;
    uint8_t         init_state;
    uint8_t         auto_eoi;
    uint8_t         rotate_on_auto_eoi;
    uint8_t         special_fully_nested_mode;
    uint8_t         init4;          /**< true if 4 byte init */
    uint8_t         elcr;           /**< PIIX edge/trigger selection*/
    uint8_t         elcr_mask;
    /** The IRQ tags and source IDs for each (tracing purposes). */
    uint32_t        auTags[8];
    /** The PIC index (0 or 1). */
    uint8_t         idxPic;
    uint8_t         abAlignment0[7]; /**< Alignment padding. */
    /** The two I/O ports at 0x20 or 0xa0. */
    IOMIOPORTHANDLE hIoPorts0;
    /** The ELCR I/O port at 0x4d0 or 0x4d1. */
    IOMIOPORTHANDLE hIoPorts1;
} PICSTATE;
AssertCompileMemberAlignment(PICSTATE, hIoPorts0, 8);
/** Pointer to the state of one PIC. */
typedef PICSTATE *PPICSTATE;


/**
 * The shared PIC device instance data.
 */
typedef struct DEVPIC
{
    /** The two interrupt controllers. */
    PICSTATE                aPics[2];
    /** Number of release log entries. Used to prevent flooding. */
    uint32_t                cRelLogEntries;
    uint32_t                u32Padding;
#ifdef VBOX_WITH_STATISTICS
    STAMCOUNTER             StatSetIrqRZ;
    STAMCOUNTER             StatSetIrqR3;
    STAMCOUNTER             StatClearedActiveIRQ2;
    STAMCOUNTER             StatClearedActiveMasterIRQ;
    STAMCOUNTER             StatClearedActiveSlaveIRQ;
#endif
} DEVPIC;
/** Pointer to the shared PIC instance data. */
typedef DEVPIC *PDEVPIC;


/**
 * The PIC device instance data for ring-3.
 */
typedef struct DEVPICR3
{
    /** Pointer to the PIC ring-3 helpers. */
    R3PTRTYPE(PCPDMPICHLP)  pPicHlp;
} DEVPICR3;
/** Pointer to the ring-3 PIC instance data. */
typedef DEVPICR3 *PDEVPICR3;


/**
 * The PIC device instance data for ring-0.
 */
typedef struct DEVPICR0
{
    /** Pointer to the PIC ring-0 helpers. */
    R0PTRTYPE(PCPDMPICHLP)  pPicHlp;
} DEVPICR0;
/** Pointer to the ring-0 PIC instance data. */
typedef DEVPICR0 *PDEVPICR0;


/**
 * The PIC device instance data for raw-mode.
 */
typedef struct DEVPICRC
{
    /** Pointer to the PIC raw-mode helpers. */
    RCPTRTYPE(PCPDMPICHLP)  pPicHlp;
} DEVPICRC;
/** Pointer to the raw-mode PIC instance data. */
typedef DEVPICRC *PDEVPICRC;


/** The PIC instance data for the current context. */
typedef CTX_SUFF(DEVPIC) DEVPICCC;
/** Pointer to the PIC instance data for the current context. */
typedef CTX_SUFF(PDEVPIC) PDEVPICCC;



#ifndef VBOX_DEVICE_STRUCT_TESTCASE /* The rest of the file! */

#ifdef LOG_ENABLED
DECLINLINE(void) DumpPICState(PPICSTATE pPic, const char *pszFn)
{
    Log2(("%s: pic%d: elcr=%x last_irr=%x irr=%x imr=%x isr=%x irq_base=%x\n",
          pszFn, pPic->idxPic, pPic->elcr, pPic->last_irr, pPic->irr, pPic->imr, pPic->isr, pPic->irq_base));
}
#else
# define DumpPICState(pThis, szFn) do { } while (0)
#endif

/* set irq level. If an edge is detected, then the IRR is set to 1 */
DECLINLINE(void) pic_set_irq1(PPICSTATE pPic, int irq, int level, uint32_t uTagSrc)
{
    Log(("pic_set_irq1: irq=%d level=%d\n", irq, level));
    int mask = 1 << irq;
    if (pPic->elcr & mask)
    {
        /* level triggered */
        if (level)
        {
            Log2(("pic_set_irq1(ls) irr=%d irrnew=%d\n", pPic->irr, pPic->irr | mask));
            pPic->irr |= mask;
            pPic->last_irr |= mask;
        }
        else
        {
            Log2(("pic_set_irq1(lc) irr=%d irrnew=%d\n", pPic->irr, pPic->irr & ~mask));
            pPic->irr &= ~mask;
            pPic->last_irr &= ~mask;
        }
    }
    else
    {
        /* edge triggered */
        if (level)
        {
            if ((pPic->last_irr & mask) == 0)
            {
                Log2(("pic_set_irq1 irr=%x last_irr=%x\n", pPic->irr | mask, pPic->last_irr));
                pPic->irr |= mask;
            }
            pPic->last_irr |= mask;
        }
        else
        {
            pPic->irr &= ~mask;
            pPic->last_irr &= ~mask;
        }
    }

    /* Save the tag. */
    if (level)
    {
        if (!pPic->auTags[irq])
            pPic->auTags[irq] = uTagSrc;
        else
            pPic->auTags[irq] |= RT_BIT_32(31);
    }

    DumpPICState(pPic, "pic_set_irq1");
}

/* return the highest priority found in mask (highest = smallest
   number). Return 8 if no irq */
DECLINLINE(int) get_priority(PPICSTATE pPic, int mask)
{
    int priority;
    if (mask == 0)
        return 8;
    priority = 0;
    while ((mask & (1 << ((priority + pPic->priority_add) & 7))) == 0)
        priority++;
    return priority;
}

/* return the pic wanted interrupt. return -1 if none */
static int pic_get_irq(PPICSTATE pPic)
{
    int mask, cur_priority, priority;
    Log(("pic_get_irq%d: mask=%x\n", pPic->idxPic, pPic->irr & ~pPic->imr));
    DumpPICState(pPic, "pic_get_irq");

    mask = pPic->irr & ~pPic->imr;
    priority = get_priority(pPic, mask);
    Log(("pic_get_irq: priority=%x\n", priority));
    if (priority == 8)
        return -1;
    /* compute current priority. If special fully nested mode on the
       master, the IRQ coming from the slave is not taken into account
       for the priority computation. */
    mask = pPic->isr;
    if (pPic->special_mask)
        mask &= ~pPic->imr;
    if (pPic->special_fully_nested_mode && pPic->idxPic == 0)
        mask &= ~(1 << 2);
    cur_priority = get_priority(pPic, mask);
    Log(("pic_get_irq%d: cur_priority=%x pending=%d\n", pPic->idxPic,
         cur_priority, (priority == 8) ? -1 : (priority + pPic->priority_add) & 7));
    if (priority < cur_priority)
    {
        /* higher priority found: an irq should be generated */
        return (priority + pPic->priority_add) & 7;
    }
    return -1;
}

/* raise irq to CPU if necessary. must be called every time the active
   irq may change */
static int pic_update_irq(PPDMDEVINS pDevIns, PDEVPIC pThis, PDEVPICCC pThisCC)
{
    int irq2, irq;

    /* first look at slave pic */
    irq2 = pic_get_irq(&pThis->aPics[1]);
    Log(("pic_update_irq irq2=%d\n", irq2));
    if (irq2 >= 0)
    {
        /* if irq request by slave pic, signal master PIC */
        pic_set_irq1(&pThis->aPics[0], 2, 1, pThis->aPics[1].auTags[irq2]);
    }
    else
    {
        /* If not, clear the IR on the master PIC. */
        pic_set_irq1(&pThis->aPics[0], 2, 0, 0 /*uTagSrc*/);
    }
    /* look at requested irq */
    irq = pic_get_irq(&pThis->aPics[0]);
    if (irq >= 0)
    {
        /* If irq 2 is pending on the master pic, then there must be one pending on the slave pic too! Otherwise we'll get
         * spurious slave interrupts in picGetInterrupt.
         */
        if (irq != 2 || irq2 != -1)
        {
            for (int i = 0; i < 2; i++)
                Log(("pic%d: imr=%x irr=%x padd=%d\n", i, pThis->aPics[i].imr, pThis->aPics[i].irr, pThis->aPics[i].priority_add));
            Log(("pic: cpu_interrupt\n"));
            pThisCC->pPicHlp->pfnSetInterruptFF(pDevIns);
        }
        else
        {
            STAM_COUNTER_INC(&pThis->StatClearedActiveIRQ2);
            Log(("pic_update_irq: irq 2 is active, but no interrupt is pending on the slave pic!!\n"));
            /* Clear it here, so lower priority interrupts can still be dispatched. */

            /* if this was the only pending irq, then we must clear the interrupt ff flag */
            pThisCC->pPicHlp->pfnClearInterruptFF(pDevIns);

            /** @todo Is this correct? */
            pThis->aPics[0].irr &= ~(1 << 2);

            /* Call ourselves again just in case other interrupts are pending */
            return pic_update_irq(pDevIns, pThis, pThisCC);
        }
    }
    else
    {
        Log(("pic_update_irq: no interrupt is pending!!\n"));

        /* we must clear the interrupt ff flag */
        pThisCC->pPicHlp->pfnClearInterruptFF(pDevIns);
    }
    return VINF_SUCCESS;
}

/**
 * Set the an IRQ.
 *
 * @param   pDevIns         Device instance of the PICs.
 * @param   iIrq            IRQ number to set.
 * @param   iLevel          IRQ level.
 * @param   uTagSrc         The IRQ tag and source ID (for tracing).
 */
static DECLCALLBACK(void) picSetIrq(PPDMDEVINS pDevIns, int iIrq, int iLevel, uint32_t uTagSrc)
{
    PDEVPIC     pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVPIC);
    PDEVPICCC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVPICCC);
    AssertMsgReturnVoid(iIrq < 16, ("iIrq=%d\n", iIrq));

    Log(("picSetIrq %d %d\n", iIrq, iLevel));
    DumpPICState(&pThis->aPics[0], "picSetIrq");
    DumpPICState(&pThis->aPics[1], "picSetIrq");
    STAM_COUNTER_INC(&pThis->CTX_SUFF_Z(StatSetIrq));
    if ((iLevel & PDM_IRQ_LEVEL_FLIP_FLOP) == PDM_IRQ_LEVEL_FLIP_FLOP)
    {
        /* A flip-flop lowers the IRQ line and immediately raises it, so
         * that a rising edge is guaranteed to occur. Note that the IRQ
         * line must be held high for a while to avoid spurious interrupts.
         */
        pic_set_irq1(&RT_SAFE_SUBSCRIPT(pThis->aPics, iIrq >> 3), iIrq & 7, 0, uTagSrc);
        pic_update_irq(pDevIns, pThis, pThisCC);
    }
    pic_set_irq1(&RT_SAFE_SUBSCRIPT(pThis->aPics, iIrq >> 3), iIrq & 7, iLevel & PDM_IRQ_LEVEL_HIGH, uTagSrc);
    pic_update_irq(pDevIns, pThis, pThisCC);
}


/* acknowledge interrupt 'irq' */
DECLINLINE(void) pic_intack(PPICSTATE pPic, int irq)
{
    if (pPic->auto_eoi)
    {
        if (pPic->rotate_on_auto_eoi)
            pPic->priority_add = (irq + 1) & 7;
    }
    else
        pPic->isr |= (1 << irq);

    /* We don't clear a level sensitive interrupt here */
    if (!(pPic->elcr & (1 << irq)))
    {
        Log2(("pic_intack: irr=%x irrnew=%x\n", pPic->irr, pPic->irr & ~(1 << irq)));
        pPic->irr &= ~(1 << irq);
    }
}


/**
 * Get a pending interrupt.
 *
 * @returns Pending interrupt number.
 * @param   pDevIns         Device instance of the PICs.
 * @param   puTagSrc        Where to return the IRQ tag and source ID.
 */
static DECLCALLBACK(int) picGetInterrupt(PPDMDEVINS pDevIns, uint32_t *puTagSrc)
{
    PDEVPIC     pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVPIC);
    PDEVPICCC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVPICCC);
    int         irq;
    int         irq2;
    int         intno;

    /* read the irq from the PIC */
    DumpPICState(&pThis->aPics[0], "picGetInterrupt");
    DumpPICState(&pThis->aPics[1], "picGetInterrupt");

    irq = pic_get_irq(&pThis->aPics[0]);
    if (irq >= 0)
    {
        pic_intack(&pThis->aPics[0], irq);
        if (irq == 2)
        {
            irq2 = pic_get_irq(&pThis->aPics[1]);
            if (irq2 >= 0)
                pic_intack(&pThis->aPics[1], irq2);
            else
            {
                /* Interrupt went away or is now masked. */
                Log(("picGetInterrupt: spurious IRQ on slave controller, converted to IRQ15\n"));
                irq2 = 7;
            }
            intno = pThis->aPics[1].irq_base + irq2;
            *puTagSrc = pThis->aPics[0].auTags[irq2];
            pThis->aPics[0].auTags[irq2] = 0;
            Log2(("picGetInterrupt1: %x base=%x irq=%x uTagSrc=%#x\n", intno, pThis->aPics[1].irq_base, irq2, *puTagSrc));
            irq = irq2 + 8;
        }
        else
        {
            intno = pThis->aPics[0].irq_base + irq;
            *puTagSrc = pThis->aPics[0].auTags[irq];
            pThis->aPics[0].auTags[irq] = 0;
            Log2(("picGetInterrupt0: %x base=%x irq=%x uTagSrc=%#x\n", intno, pThis->aPics[0].irq_base, irq, *puTagSrc));
        }
    }
    else
    {
        /* Interrupt went away or is now masked. */
        Log(("picGetInterrupt: spurious IRQ on master controller, converted to IRQ7\n"));
        irq = 7;
        intno = pThis->aPics[0].irq_base + irq;
        *puTagSrc = 0;
    }
    pic_update_irq(pDevIns, pThis, pThisCC);

    Log(("picGetInterrupt: 0x%02x pending 0:%d 1:%d\n", intno, pic_get_irq(&pThis->aPics[0]), pic_get_irq(&pThis->aPics[1])));

    return intno;
}

static void pic_reset(PPICSTATE pPic)
{
    pPic->last_irr                  = 0;
    pPic->irr                       = 0;
    pPic->imr                       = 0;
    pPic->isr                       = 0;
    pPic->priority_add              = 0;
    pPic->irq_base                  = 0;
    pPic->read_reg_select           = 0;
    pPic->poll                      = 0;
    pPic->special_mask              = 0;
    pPic->init_state                = 0;
    pPic->auto_eoi                  = 0;
    pPic->rotate_on_auto_eoi        = 0;
    pPic->special_fully_nested_mode = 0;
    pPic->init4                     = 0;
    //pPic->elcr                    - not cleared;
    //pPic->elcr_mask               - not cleared;
    RT_ZERO(pPic->auTags);
}


static VBOXSTRICTRC pic_ioport_write(PPDMDEVINS pDevIns, PDEVPIC pThis, PDEVPICCC pThisCC, PPICSTATE pPic,
                                     uint32_t addr, uint32_t val)
{
    VBOXSTRICTRC rc = VINF_SUCCESS;
    int          irq;

    Log(("pic_write/%zu: addr=0x%02x val=0x%02x\n", pPic - pThis->aPics, addr, val));
    addr &= 1;
    if (addr == 0)
    {
        if (val & 0x10)
        {
            /* init */
            pic_reset(pPic);
            /* deassert a pending interrupt */
            pThisCC->pPicHlp->pfnClearInterruptFF(pDevIns);

            pPic->init_state = 1;
            pPic->init4 = val & 1;
            if (!(val & 0x0a))
            { /* likely */ }
            else if (pThis->cRelLogEntries++ < 64)
            {
                if (val & 0x02)
                    LogRel(("PIC: Single mode not supported, ignored.\n"));
                if (val & 0x08)
                    LogRel(("PIC: Level sensitive IRQ setting ignored.\n"));
            }
        }
        else if (val & 0x08)
        {
            if (val & 0x04)
                pPic->poll = 1;
            if (val & 0x02)
                pPic->read_reg_select = val & 1;
            if (val & 0x40)
                pPic->special_mask = (val >> 5) & 1;
        }
        else
        {
            int cmd = val >> 5;
            switch (cmd)
            {
                case 0:
                case 4:
                    pPic->rotate_on_auto_eoi = cmd >> 2;
                    break;
                case 1: /* end of interrupt */
                case 5:
                {
                    int priority = get_priority(pPic, pPic->isr);
                    if (priority != 8) {
                        irq = (priority + pPic->priority_add) & 7;
                        Log(("pic_write: EOI prio=%d irq=%d\n", priority, irq));
                        pPic->isr &= ~(1 << irq);
                        if (cmd == 5)
                            pPic->priority_add = (irq + 1) & 7;
                        rc = pic_update_irq(pDevIns, pThis, pThisCC);
                        Assert(rc == VINF_SUCCESS);
                        DumpPICState(pPic, "eoi");
                    }
                    break;
                }
                case 3:
                {
                    irq = val & 7;
                    Log(("pic_write: EOI2 for irq %d\n", irq));
                    pPic->isr &= ~(1 << irq);
                    rc = pic_update_irq(pDevIns, pThis, pThisCC);
                    Assert(rc == VINF_SUCCESS);
                    DumpPICState(pPic, "eoi2");
                    break;
                }
                case 6:
                {
                    pPic->priority_add = (val + 1) & 7;
                    Log(("pic_write: lowest priority %d (highest %d)\n", val & 7, pPic->priority_add));
                    rc = pic_update_irq(pDevIns, pThis, pThisCC);
                    Assert(rc == VINF_SUCCESS);
                    break;
                }
                case 7:
                {
                    irq = val & 7;
                    Log(("pic_write: EOI3 for irq %d\n", irq));
                    pPic->isr &= ~(1 << irq);
                    pPic->priority_add = (irq + 1) & 7;
                    rc = pic_update_irq(pDevIns, pThis, pThisCC);
                    Assert(rc == VINF_SUCCESS);
                    DumpPICState(pPic, "eoi3");
                    break;
                }
                default:
                    /* no operation */
                    break;
            }
        }
    }
    else
    {
        switch (pPic->init_state)
        {
            case 0:
                /* normal mode */
                pPic->imr = val;
                rc = pic_update_irq(pDevIns, pThis, pThisCC);
                Assert(rc == VINF_SUCCESS);
                break;
            case 1:
                pPic->irq_base = val & 0xf8;
                pPic->init_state = 2;
                Log(("pic_write: set irq base to %x\n", pPic->irq_base));
                break;
            case 2:
                if (pPic->init4)
                    pPic->init_state = 3;
                else
                    pPic->init_state = 0;
                break;
            case 3:
                pPic->special_fully_nested_mode = (val >> 4) & 1;
                pPic->auto_eoi = (val >> 1) & 1;
                pPic->init_state = 0;
                Log(("pic_write: special_fully_nested_mode=%d auto_eoi=%d\n", pPic->special_fully_nested_mode, pPic->auto_eoi));
                break;
        }
    }
    return rc;
}


static uint32_t pic_poll_read(PPDMDEVINS pDevIns, PDEVPIC pThis, PDEVPICCC pThisCC, PPICSTATE pPic, uint32_t addr1)
{
    int ret = pic_get_irq(pPic);
    if (ret >= 0)
    {
        if (addr1 >> 7)
        {
            Log2(("pic_poll_read: clear slave irq (isr)\n"));
            pThis->aPics[0].isr &= ~(1 << 2);
            pThis->aPics[0].irr &= ~(1 << 2);
        }
        Log2(("pic_poll_read: clear irq %d (isr)\n", ret));
        pPic->irr &= ~(1 << ret);
        pPic->isr &= ~(1 << ret);
        if (addr1 >> 7 || ret != 2)
            pic_update_irq(pDevIns, pThis, pThisCC);
    }
    else
    {
        ret = 0;
        pic_update_irq(pDevIns, pThis, pThisCC);
    }

    return ret;
}


static uint32_t pic_ioport_read(PPDMDEVINS pDevIns, PDEVPIC pThis, PDEVPICCC pThisCC, PPICSTATE pPic, uint32_t addr1, int *pRC)
{
    unsigned int addr;
    int ret;

    *pRC = VINF_SUCCESS;

    addr = addr1;
    addr &= 1;
    if (pPic->poll)
    {
        ret = pic_poll_read(pDevIns, pThis, pThisCC, pPic, addr1);
        pPic->poll = 0;
    }
    else
    {
        if (addr == 0)
        {
            if (pPic->read_reg_select)
                ret = pPic->isr;
            else
                ret = pPic->irr;
        }
        else
            ret = pPic->imr;
    }
    Log(("pic_read: addr=0x%02x val=0x%02x\n", addr1, ret));
    return ret;
}



/* -=-=-=-=-=- I/O ports -=-=-=-=-=- */

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN}
 */
static DECLCALLBACK(VBOXSTRICTRC) picIOPortRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PDEVPIC     pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVPIC);
    PDEVPICCC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVPICCC);
    uint32_t    iPic    = (uint32_t)(uintptr_t)pvUser;
    int         rc;

    Assert(iPic == 0 || iPic == 1);
    if (cb == 1)
    {
        PIC_LOCK_RET(pDevIns, pThisCC, VINF_IOM_R3_IOPORT_READ);
        *pu32 = pic_ioport_read(pDevIns, pThis, pThisCC, &RT_SAFE_SUBSCRIPT(pThis->aPics, iPic), offPort, &rc);
        PIC_UNLOCK(pDevIns, pThisCC);
        return rc;
    }
    else if (cb == 2)
    {
        uint8_t     u8Lo, u8Hi = 0;
        /* Manually split access. Probably not 100% accurate! */
        PIC_LOCK_RET(pDevIns, pThisCC, VINF_IOM_R3_IOPORT_READ);
        u8Lo = pic_ioport_read(pDevIns, pThis, pThisCC, &RT_SAFE_SUBSCRIPT(pThis->aPics, iPic), offPort, &rc);
        Assert(rc == VINF_SUCCESS);
        if (!(offPort & 1))
            u8Hi = pic_ioport_read(pDevIns, pThis, pThisCC, &RT_SAFE_SUBSCRIPT(pThis->aPics, iPic), offPort + 1, &rc);
        PIC_UNLOCK(pDevIns, pThisCC);
        *pu32 = RT_MAKE_U16(u8Lo, u8Hi);
        return rc;
    }
    return VERR_IOM_IOPORT_UNUSED;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT}
 */
static DECLCALLBACK(VBOXSTRICTRC) picIOPortWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PDEVPIC     pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVPIC);
    PDEVPICCC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVPICCC);
    uint32_t    iPic    = (uint32_t)(uintptr_t)pvUser;
    VBOXSTRICTRC rc;

    Assert(iPic == 0 || iPic == 1);

    if (cb == 1)
    {
        PIC_LOCK_RET(pDevIns, pThisCC, VINF_IOM_R3_IOPORT_WRITE);
        rc = pic_ioport_write(pDevIns, pThis, pThisCC, &RT_SAFE_SUBSCRIPT(pThis->aPics, iPic), offPort, u32);
        PIC_UNLOCK(pDevIns, pThisCC);
        return rc;
    }
    else if (cb == 2)
    {
        PIC_LOCK_RET(pDevIns, pThisCC, VINF_IOM_R3_IOPORT_WRITE);
        /* Manually split access. Probably not 100% accurate! */
        rc = pic_ioport_write(pDevIns, pThis, pThisCC, &RT_SAFE_SUBSCRIPT(pThis->aPics, iPic), offPort, RT_LOBYTE(u32));
        if (RT_SUCCESS(rc) && !(offPort & 1))
            rc = pic_ioport_write(pDevIns, pThis, pThisCC, &RT_SAFE_SUBSCRIPT(pThis->aPics, iPic), offPort + 1, RT_HIBYTE(u32));
        PIC_UNLOCK(pDevIns, pThisCC);
        return rc;
    }
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWIN, ELCR}
 */
static DECLCALLBACK(VBOXSTRICTRC) picIOPortElcrRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    if (cb == 1)
    {
        PDEVPICCC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVPICCC);
        PPICSTATE   pPic    = (PPICSTATE)pvUser;
        PIC_LOCK_RET(pDevIns, pThisCC, VINF_IOM_R3_IOPORT_READ);
        *pu32 = pPic->elcr;
        PIC_UNLOCK(pDevIns, pThisCC);
        return VINF_SUCCESS;
    }
    RT_NOREF(offPort);
    return VERR_IOM_IOPORT_UNUSED;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT, ELCR}
 */
static DECLCALLBACK(VBOXSTRICTRC) picIOPortElcrWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    if (cb == 1)
    {
        PDEVPICCC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVPICCC);
        PPICSTATE   pPic    = (PPICSTATE)pvUser;
        PIC_LOCK_RET(pDevIns, pThisCC, VINF_IOM_R3_IOPORT_WRITE);
        pPic->elcr = u32 & pPic->elcr_mask;
        PIC_UNLOCK(pDevIns, pThisCC);
    }
    RT_NOREF(offPort);
    return VINF_SUCCESS;
}


#ifdef IN_RING3

/**
 * @callback_method_impl{FNDBGFHANDLERDEV}
 */
static DECLCALLBACK(void) picR3Info(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PDEVPIC pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPIC);
    NOREF(pszArgs);

    /*
     * Show info.
     */
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aPics); i++)
    {
        PPICSTATE pPic = &pThis->aPics[i];

        pHlp->pfnPrintf(pHlp, "PIC%d:\n", i);
        pHlp->pfnPrintf(pHlp, " IMR :%02x ISR   :%02x IRR   :%02x LIRR:%02x\n",
                        pPic->imr, pPic->isr, pPic->irr, pPic->last_irr);
        pHlp->pfnPrintf(pHlp, " Base:%02x PriAdd:%02x RegSel:%02x\n",
                        pPic->irq_base, pPic->priority_add, pPic->read_reg_select);
        pHlp->pfnPrintf(pHlp, " Poll:%02x SpMask:%02x IState:%02x\n",
                        pPic->poll, pPic->special_mask, pPic->init_state);
        pHlp->pfnPrintf(pHlp, " AEOI:%02x Rotate:%02x FNest :%02x Ini4:%02x\n",
                        pPic->auto_eoi, pPic->rotate_on_auto_eoi,
                        pPic->special_fully_nested_mode, pPic->init4);
        pHlp->pfnPrintf(pHlp, " ELCR:%02x ELMask:%02x\n", pPic->elcr, pPic->elcr_mask);
    }
}


/* -=-=-=-=-=- Saved State -=-=-=-=-=- */

/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) picR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PDEVPIC         pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPIC);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;

    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aPics); i++)
    {
        pHlp->pfnSSMPutU8(pSSM, pThis->aPics[i].last_irr);
        pHlp->pfnSSMPutU8(pSSM, pThis->aPics[i].irr);
        pHlp->pfnSSMPutU8(pSSM, pThis->aPics[i].imr);
        pHlp->pfnSSMPutU8(pSSM, pThis->aPics[i].isr);
        pHlp->pfnSSMPutU8(pSSM, pThis->aPics[i].priority_add);
        pHlp->pfnSSMPutU8(pSSM, pThis->aPics[i].irq_base);
        pHlp->pfnSSMPutU8(pSSM, pThis->aPics[i].read_reg_select);
        pHlp->pfnSSMPutU8(pSSM, pThis->aPics[i].poll);
        pHlp->pfnSSMPutU8(pSSM, pThis->aPics[i].special_mask);
        pHlp->pfnSSMPutU8(pSSM, pThis->aPics[i].init_state);
        pHlp->pfnSSMPutU8(pSSM, pThis->aPics[i].auto_eoi);
        pHlp->pfnSSMPutU8(pSSM, pThis->aPics[i].rotate_on_auto_eoi);
        pHlp->pfnSSMPutU8(pSSM, pThis->aPics[i].special_fully_nested_mode);
        pHlp->pfnSSMPutU8(pSSM, pThis->aPics[i].init4);
        pHlp->pfnSSMPutU8(pSSM, pThis->aPics[i].elcr);
    }
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) picR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PDEVPIC         pThis = PDMDEVINS_2_DATA(pDevIns, PDEVPIC);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;

    if (uVersion != 1)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);

    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aPics); i++)
    {
        pHlp->pfnSSMGetU8(pSSM, &pThis->aPics[i].last_irr);
        pHlp->pfnSSMGetU8(pSSM, &pThis->aPics[i].irr);
        pHlp->pfnSSMGetU8(pSSM, &pThis->aPics[i].imr);
        pHlp->pfnSSMGetU8(pSSM, &pThis->aPics[i].isr);
        pHlp->pfnSSMGetU8(pSSM, &pThis->aPics[i].priority_add);
        pHlp->pfnSSMGetU8(pSSM, &pThis->aPics[i].irq_base);
        pHlp->pfnSSMGetU8(pSSM, &pThis->aPics[i].read_reg_select);
        pHlp->pfnSSMGetU8(pSSM, &pThis->aPics[i].poll);
        pHlp->pfnSSMGetU8(pSSM, &pThis->aPics[i].special_mask);
        pHlp->pfnSSMGetU8(pSSM, &pThis->aPics[i].init_state);
        pHlp->pfnSSMGetU8(pSSM, &pThis->aPics[i].auto_eoi);
        pHlp->pfnSSMGetU8(pSSM, &pThis->aPics[i].rotate_on_auto_eoi);
        pHlp->pfnSSMGetU8(pSSM, &pThis->aPics[i].special_fully_nested_mode);
        pHlp->pfnSSMGetU8(pSSM, &pThis->aPics[i].init4);
        pHlp->pfnSSMGetU8(pSSM, &pThis->aPics[i].elcr);
    }

    /* Note! PDM will restore the VMCPU_FF_INTERRUPT_PIC state. */
    return VINF_SUCCESS;
}


/* -=-=-=-=-=- PDMDEVREG -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void)  picR3Reset(PPDMDEVINS pDevIns)
{
    PDEVPIC     pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVPIC);
    PDEVPICCC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVPICCC);
    unsigned    i;
    LogFlow(("picR3Reset:\n"));
    pThisCC->pPicHlp->pfnLock(pDevIns, VERR_INTERNAL_ERROR);

    for (i = 0; i < RT_ELEMENTS(pThis->aPics); i++)
        pic_reset(&pThis->aPics[i]);

    PIC_UNLOCK(pDevIns, pThisCC);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnRelocate}
 */
static DECLCALLBACK(void) picR3Relocate(PPDMDEVINS pDevIns, RTGCINTPTR offDelta)
{
    PDEVPICRC pThisRC = PDMINS_2_DATA_RC(pDevIns, PDEVPICRC);
    pThisRC->pPicHlp += offDelta;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int)  picR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PDEVPIC         pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVPIC);
    PDEVPICCC       pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVPICCC);
    int             rc;
    RT_NOREF(iInstance, pCfg);

    Assert(iInstance == 0);

    /*
     * Validate and read configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "", "");
    Log(("DevPIC: fRCEnabled=%RTbool fR0Enabled=%RTbool\n", pDevIns->fRCEnabled, pDevIns->fR0Enabled));

    /*
     * Init the data.
     */
    Assert(RT_ELEMENTS(pThis->aPics) == 2);
    pThis->aPics[0].elcr_mask = 0xf8;
    pThis->aPics[1].elcr_mask = 0xde;
    pThis->aPics[0].idxPic    = 0;
    pThis->aPics[1].idxPic    = 1;
    pThis->cRelLogEntries     = 0;

    /*
     * Register us as the PIC with PDM.
     */
    PDMPICREG PicReg;
    PicReg.u32Version           = PDM_PICREG_VERSION;
    PicReg.pfnSetIrq            = picSetIrq;
    PicReg.pfnGetInterrupt      = picGetInterrupt;
    PicReg.u32TheEnd            = PDM_PICREG_VERSION;
    rc = PDMDevHlpPICRegister(pDevIns, &PicReg, &pThisCC->pPicHlp);
    AssertLogRelMsgRCReturn(rc, ("PDMDevHlpPICRegister -> %Rrc\n", rc), rc);
    AssertReturn(pThisCC->pPicHlp->u32Version == PDM_PICHLP_VERSION, VERR_VERSION_MISMATCH);
    AssertReturn(pThisCC->pPicHlp->u32TheEnd  == PDM_PICHLP_VERSION, VERR_VERSION_MISMATCH);

    /*
     * Since the PIC helper interface provides access to the PDM lock,
     * we need no device level critical section.
     */
    rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    /*
     * Register I/O ports and save state.
     */
    rc = PDMDevHlpIoPortCreateUAndMap(pDevIns, 0x20 /*uPort*/, 2 /*cPorts*/, picIOPortWrite, picIOPortRead, (void *)0,
                                      "i8259 PIC #0", NULL /*paExtDesc*/, &pThis->aPics[0].hIoPorts0);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpIoPortCreateUAndMap(pDevIns, 0xa0 /*uPort*/, 2 /*cPorts*/, picIOPortWrite, picIOPortRead, (void *)1,
                                      "i8259 PIC #1", NULL /*paExtDesc*/, &pThis->aPics[1].hIoPorts0);
    AssertRCReturn(rc, rc);


    rc = PDMDevHlpIoPortCreateUAndMap(pDevIns, 0x4d0 /*uPort*/, 1 /*cPorts*/, picIOPortElcrWrite, picIOPortElcrRead,
                                      &pThis->aPics[0], "i8259 PIC #0 - elcr", NULL /*paExtDesc*/, &pThis->aPics[0].hIoPorts1);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpIoPortCreateUAndMap(pDevIns, 0x4d1 /*uPort*/, 1 /*cPorts*/, picIOPortElcrWrite, picIOPortElcrRead,
                                      &pThis->aPics[1], "i8259 PIC #1 - elcr", NULL /*paExtDesc*/, &pThis->aPics[1].hIoPorts1);
    AssertRCReturn(rc, rc);

    /*
     * Saved state.
     */
    rc = PDMDevHlpSSMRegister(pDevIns, 1 /* uVersion */, sizeof(*pThis), picR3SaveExec, picR3LoadExec);
    AssertRCReturn(rc, rc);

    /*
     * Register the info item.
     */
    PDMDevHlpDBGFInfoRegister(pDevIns, "pic", "PIC info.", picR3Info);

    /*
     * Initialize the device state.
     */
    picR3Reset(pDevIns);

# ifdef VBOX_WITH_STATISTICS
    /*
     * Statistics.
     */
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatSetIrqRZ, STAMTYPE_COUNTER, "SetIrqRZ", STAMUNIT_OCCURENCES, "Number of PIC SetIrq calls in ring-0/raw-mode.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatSetIrqR3, STAMTYPE_COUNTER, "SetIrqR3", STAMUNIT_OCCURENCES, "Number of PIC SetIrq calls in ring-3.");

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatClearedActiveIRQ2,       STAMTYPE_COUNTER, "Masked/ActiveIRQ2",   STAMUNIT_OCCURENCES, "Number of cleared irq 2.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatClearedActiveMasterIRQ,  STAMTYPE_COUNTER, "Masked/ActiveMaster", STAMUNIT_OCCURENCES, "Number of cleared master irqs.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatClearedActiveSlaveIRQ,   STAMTYPE_COUNTER, "Masked/ActiveSlave",  STAMUNIT_OCCURENCES, "Number of cleared slave irqs.");
# endif

    return VINF_SUCCESS;
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) picRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PDEVPIC   pThis   = PDMDEVINS_2_DATA(pDevIns, PDEVPIC);
    PDEVPICCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PDEVPICCC);

    /* NOP the critsect: */
    int rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    /* Set up the PIC callbacks: */
    PDMPICREG PicReg;
    PicReg.u32Version           = PDM_PICREG_VERSION;
    PicReg.pfnSetIrq            = picSetIrq;
    PicReg.pfnGetInterrupt      = picGetInterrupt;
    PicReg.u32TheEnd            = PDM_PICREG_VERSION;
    rc = PDMDevHlpPICSetUpContext(pDevIns, &PicReg, &pThisCC->pPicHlp);
    AssertLogRelMsgRCReturn(rc, ("PDMDevHlpPICSetUpContext -> %Rrc\n", rc), rc);
    AssertPtrReturn(pThisCC->pPicHlp, VERR_INTERNAL_ERROR_3);
    AssertReturn(pThisCC->pPicHlp->u32Version == PDM_PICHLP_VERSION, VERR_VERSION_MISMATCH);
    AssertReturn(pThisCC->pPicHlp->u32TheEnd  == PDM_PICHLP_VERSION, VERR_VERSION_MISMATCH);

    /* I/O port callbacks: */
    Assert(RT_ELEMENTS(pThis->aPics) == 2);
    rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->aPics[0].hIoPorts0, picIOPortWrite, picIOPortRead, (void *)0);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->aPics[1].hIoPorts0, picIOPortWrite, picIOPortRead, (void *)1);
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->aPics[0].hIoPorts1, picIOPortElcrWrite, picIOPortElcrRead, &pThis->aPics[0]);
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->aPics[1].hIoPorts1, picIOPortElcrWrite, picIOPortElcrRead, &pThis->aPics[1]);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */

/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceI8259 =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "i8259",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE
                                    | PDM_DEVREG_FLAGS_REQUIRE_R0 | PDM_DEVREG_FLAGS_REQUIRE_RC,
    /* .fClass = */                 PDM_DEVREG_CLASS_PIC,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(DEVPIC),
    /* .cbInstanceCC = */           sizeof(DEVPICCC),
    /* .cbInstanceRC = */           sizeof(DEVPICRC),
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Intel 8259 Programmable Interrupt Controller (PIC) Device.",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           picR3Construct,
    /* .pfnDestruct = */            NULL,
    /* .pfnRelocate = */            picR3Relocate,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               picR3Reset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              NULL,
    /* .pfnDetach = */              NULL,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        NULL,
    /* .pfnPowerOff = */            NULL,
    /* .pfnSoftReset = */           NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RING0)
    /* .pfnEarlyConstruct = */      NULL,
    /* .pfnConstruct = */           picRZConstruct,
    /* .pfnDestruct = */            NULL,
    /* .pfnFinalDestruct = */       NULL,
    /* .pfnRequest = */             NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RC)
    /* .pfnConstruct = */           picRZConstruct,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#else
# error "Not in IN_RING3, IN_RING0 or IN_RC!"
#endif
    /* .u32VersionEnd = */          PDM_DEVREG_VERSION
};

#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */

