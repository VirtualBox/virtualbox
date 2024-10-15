/* $Id: DevBusMouse.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * BusMouse - Microsoft Bus (parallel) mouse controller device.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_KBD
#include <VBox/vmm/pdmdev.h>
#ifndef IN_RING3
# include <VBox/vmm/pdmapi.h>
#endif
#include <VBox/AssertGuest.h>
#include <VBox/version.h>
#include <iprt/assert.h>
#include <iprt/uuid.h>

/** @page pg_busmouse DevBusMouse - Microsoft Bus Mouse Emulation
 *
 * The Microsoft Bus Mouse was an early mouse sold by Microsoft, originally
 * introduced in 1983. The mouse had a D-shaped 9-pin connector which plugged
 * into a small ISA add-in board.
 *
 * The mouse itself was very simple (compared to a serial mouse) and most of the
 * logic was located on the ISA board. Later, Microsoft sold an InPort mouse,
 * which was also called a "bus mouse", but used a different interface.
 *
 * Microsoft part numbers for the Bus Mouse were 037-099 (100 ppi)
 * and 037-199 (200 ppi).
 *
 * The Bus Mouse adapter included IRQ configuration jumpers (ref. MS article
 * Q12230). The IRQ could be set to one of 2, 3, 4, 5. The typical setting
 * would be IRQ 2 for a PC/XT and IRQ 5 for an AT compatible. Because IRQ 5
 * may conflict with a SoundBlaster or a PCI device, this device defaults to
 * IRQ 3. Note that IRQ 3 is also used by the COM 2 device, not often needed.
 *
 * The ISA adapter was built around an Intel 8255A compatible chip (ref.
 * MS article Q46369). Once enabled, the adapter raises the configured IRQ
 * 30 times per second; the rate is not configurable. The interrupts
 * occur regardless of whether the mouse state has changed or not.
 *
 * To function properly, the 8255A must be programmed as follows:
 *  - Port A: Input. Used to read motion deltas and button states.
 *  - Port B: Output. Not used except for mouse detection.
 *  - Port C: Split. Upper bits set as output, used for control purposes.
 *                   Lower bits set as input, reflecting IRQ state.
 *
 * Detailed information was gleaned from Windows and OS/2 DDK mouse samples.
 */


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The original bus mouse controller is fixed at I/O port 0x23C. */
#define BMS_IO_BASE         0x23C
#define BMS_IO_SIZE         4

/** @name Offsets relative to the I/O base.
 *@{ */
#define BMS_PORT_DATA       0   /**< 8255 Port A. */
#define BMS_PORT_SIG        1   /**< 8255 Port B. */
#define BMS_PORT_CTRL       2   /**< 8255 Port C. */
#define BMS_PORT_INIT       3   /**< 8255 Control Port. */
/** @} */

/** @name Port C bits (control port).
 * @{  */
#define BMS_CTL_INT_DIS     RT_BIT(4)   /**< Disable IRQ (else enabled). */
#define BMS_CTL_SEL_HIGH    RT_BIT(5)   /**< Select hi nibble (else lo). */
#define BMS_CTL_SEL_Y       RT_BIT(6)   /**< Select X to read (else Y). */
#define BMS_CTL_HOLD        RT_BIT(7)   /**< Hold counter (else clear). */
/** @} */

/** @name Port A bits (data port).
 * @{ */
#define BMS_DATA_DELTA      0x0F        /**< Motion delta in lower nibble. */
#define BMS_DATA_B3_UP      RT_BIT(5)   /**< Button 3 (right) is up. */
#define BMS_DATA_B2_UP      RT_BIT(6)   /**< Button 2 (middle) is up. */
#define BMS_DATA_B1_UP      RT_BIT(7)   /**< Button 1 (left) is up. */
/** @} */

/** Convert IRQ level (2/3/4/5) to a bit in the control register. */
#define BMS_IRQ_BIT(a)      (1 << (5 - a))

/** IRQ period, corresponds to approx. 30 Hz. */
#define BMS_IRQ_PERIOD_MS   34

/** Default IRQ setting. */
#define BMS_DEFAULT_IRQ     3

/** The saved state version. */
#define BMS_SAVED_STATE_VERSION     1


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The shared Bus Mouse device state.
 */
typedef struct MouState
{
    /** @name 8255A state
     * @{ */
    uint8_t         port_a;
    uint8_t         port_b;
    uint8_t         port_c;
    uint8_t         ctrl_port;
    uint8_t         cnt_held;   /**< Counters held for reading. */
    uint8_t         held_dx;
    uint8_t         held_dy;
    uint8_t         irq;        /**< The "jumpered" IRQ level. */
    int32_t         irq_toggle_counter;
    /** Timer period in milliseconds. */
    uint32_t        cTimerPeriodMs;
    /** Mouse timer handle. */
    TMTIMERHANDLE   hMouseTimer;
    /** @} */

    /** @name mouse state
     * @{ */
    int32_t         disable_counter;
    int32_t         mouse_dx; /* current values, needed for 'poll' mode */
    int32_t         mouse_dy;
    uint8_t         mouse_enabled;
    uint8_t         mouse_buttons;
    uint8_t         mouse_buttons_reported;
    uint8_t         bAlignment;
    /** @}  */

    /** The I/O ports registration. */
    IOMIOPORTHANDLE hIoPorts;

} MouState, BMSSTATE;
/** Pointer to the shared Bus Mouse device state.  */
typedef BMSSTATE *PBMSSTATE;


/**
 * The ring-3 Bus Mouse device state.
 */
typedef struct BMSSTATER3
{
    /** Pointer to the device instance.
     * @note Only for getting our bearings in an interface method. */
    PPDMDEVINSR3    pDevIns;

    /**
     * Mouse port - LUN#0.
     *
     * @implements  PDMIBASE
     * @implements  PDMIMOUSEPORT
     */
    struct
    {
        /** The base interface for the mouse port. */
        PDMIBASE                            IBase;
        /** The mouse port base interface. */
        PDMIMOUSEPORT                       IPort;

        /** The base interface of the attached mouse driver. */
        R3PTRTYPE(PPDMIBASE)                pDrvBase;
        /** The mouse interface of the attached mouse driver. */
        R3PTRTYPE(PPDMIMOUSECONNECTOR)      pDrv;
    } Mouse;
} BMSSTATER3;
/** Pointer to the ring-3 Bus Mouse device state.  */
typedef BMSSTATER3 *PBMSSTATER3;


#ifndef VBOX_DEVICE_STRUCT_TESTCASE

# ifdef IN_RING3

/**
 * Report a change in status down the driver chain.
 *
 * We want to report the mouse as enabled if and only if the guest is "using"
 * it. That way, other devices (e.g. a PS/2 or USB mouse) can receive mouse
 * events when the bus mouse is disabled. Enabling interrupts constitutes
 * enabling the bus mouse. The mouse is considered disabled if interrupts are
 * disabled for several consecutive mouse timer ticks; this is because the
 * interrupt handler in the guest typically temporarily disables interrupts and
 * we do not want to toggle the enabled/disabled state more often than
 * necessary.
 */
static void bmsR3UpdateDownstreamStatus(PBMSSTATE pThis, PBMSSTATER3 pThisCC)
{
    PPDMIMOUSECONNECTOR pDrv = pThisCC->Mouse.pDrv;
    bool fEnabled = !!pThis->mouse_enabled;
    if (pDrv)   /* pDrv may be NULL if no mouse interface attached. */
        pDrv->pfnReportModes(pDrv, fEnabled, false, false, false);
}

/**
 * Process a mouse event coming from the host.
 */
static void bmsR3MouseEvent(PBMSSTATE pThis, int dx, int dy, int dz, int dw, int buttons_state)
{
    LogRel3(("%s: dx=%d, dy=%d, dz=%d, dw=%d, buttons_state=0x%x\n",
             __PRETTY_FUNCTION__, dx, dy, dz, dw, buttons_state));

    /* Only record X/Y movement and buttons. */
    pThis->mouse_dx += dx;
    pThis->mouse_dy += dy;
    pThis->mouse_buttons = buttons_state;
}

/**
 * @callback_method_impl{FNTMTIMERDEV}
 */
static DECLCALLBACK(void) bmsR3TimerCallback(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PBMSSTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PBMSSTATE);
    PBMSSTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PBMSSTATER3);
    uint8_t     irq_bit;
    RT_NOREF(pvUser);
    Assert(hTimer == pThis->hMouseTimer);

    /* Toggle the IRQ line if interrupts are enabled. */
    irq_bit = BMS_IRQ_BIT(pThis->irq);

    if (pThis->port_c & irq_bit)
    {
        if (!(pThis->port_c & BMS_CTL_INT_DIS))
            PDMDevHlpISASetIrq(pDevIns, pThis->irq, PDM_IRQ_LEVEL_LOW);
        pThis->port_c &= ~irq_bit;
    }
    else
    {
        pThis->port_c |= irq_bit;
        if (!(pThis->port_c & BMS_CTL_INT_DIS))
            PDMDevHlpISASetIrq(pDevIns, pThis->irq, PDM_IRQ_LEVEL_HIGH);
    }

    /* Handle enabling/disabling of the mouse interface. */
    if (pThis->port_c & BMS_CTL_INT_DIS)
    {
        if (pThis->disable_counter)
            --pThis->disable_counter;

        if (pThis->disable_counter == 0 && pThis->mouse_enabled)
        {
            pThis->mouse_enabled = false;
            bmsR3UpdateDownstreamStatus(pThis, pThisCC);
        }
    }
    else
    {
        pThis->disable_counter = 8; /* Re-arm the disable countdown. */
        if (!pThis->mouse_enabled)
        {
            pThis->mouse_enabled = true;
            bmsR3UpdateDownstreamStatus(pThis, pThisCC);
        }
    }

    /* Re-arm the timer. */
    PDMDevHlpTimerSetMillies(pDevIns, hTimer, pThis->cTimerPeriodMs);
}

# endif /* IN_RING3 */

static void bmsSetReportedButtons(PBMSSTATE pThis, unsigned fButtons, unsigned fButtonMask)
{
    pThis->mouse_buttons_reported |= (fButtons & fButtonMask);
    pThis->mouse_buttons_reported &= (fButtons | ~fButtonMask);
}

/**
 * Update the internal state after a write to port C.
 */
static void bmsUpdateCtrl(PPDMDEVINS pDevIns, PBMSSTATE pThis)
{
    int32_t     dx, dy;

    /* If the controller is in hold state, transfer data from counters. */
    if (pThis->port_c & BMS_CTL_HOLD)
    {
        if (!pThis->cnt_held)
        {
            pThis->cnt_held = true;
            dx = pThis->mouse_dx < 0 ? RT_MAX(pThis->mouse_dx, -128)
                                     : RT_MIN(pThis->mouse_dx, 127);
            dy = pThis->mouse_dy < 0 ? RT_MAX(pThis->mouse_dy, -128)
                                     : RT_MIN(pThis->mouse_dy, 127);
            pThis->mouse_dx -= dx;
            pThis->mouse_dy -= dy;
            bmsSetReportedButtons(pThis, pThis->mouse_buttons & 0x07, 0x07);

            /* Force type conversion. */
            pThis->held_dx = dx;
            pThis->held_dy = dy;
        }
    }
    else
        pThis->cnt_held = false;

    /* Move the appropriate nibble into port A. */
    if (pThis->cnt_held)
    {
        if (pThis->port_c & BMS_CTL_SEL_Y)
        {
            if (pThis->port_c & BMS_CTL_SEL_HIGH)
                pThis->port_a = pThis->held_dy >> 4;
            else
                pThis->port_a = pThis->held_dy & 0xF;
        }
        else
        {
            if (pThis->port_c & BMS_CTL_SEL_HIGH)
                pThis->port_a = pThis->held_dx >> 4;
            else
                pThis->port_a = pThis->held_dx & 0xF;
        }
        /* And update the button bits. */
        pThis->port_a |= pThis->mouse_buttons & 1 ? 0 : BMS_DATA_B1_UP;
        pThis->port_a |= pThis->mouse_buttons & 2 ? 0 : BMS_DATA_B3_UP;
        pThis->port_a |= pThis->mouse_buttons & 4 ? 0 : BMS_DATA_B2_UP;
    }
    /* Immediately clear the IRQ if necessary. */
    if (pThis->port_c & BMS_CTL_INT_DIS)
    {
        PDMDevHlpISASetIrq(pDevIns, pThis->irq, PDM_IRQ_LEVEL_LOW);
        pThis->port_c &= ~BMS_IRQ_BIT(pThis->irq);
    }
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN}
 */
static DECLCALLBACK(VBOXSTRICTRC) bmsIoPortRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    RT_NOREF(pvUser);
    if (cb == 1)
    {
        PBMSSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PBMSSTATE);
        uint32_t uValue;

        switch (offPort)
        {
            case BMS_PORT_DATA:
                /* Read port A. */
                uValue = pThis->port_a;
                break;
            case BMS_PORT_SIG:
                /* Read port B. */
                uValue = pThis->port_b;
                break;
            case BMS_PORT_CTRL:
                /* Read port C. */
                uValue = pThis->port_c;
                /* Some Microsoft driver code reads the control port 10,000 times when
                 * determining the IRQ level. This can occur faster than the IRQ line
                 * transitions and the detection fails. To work around this, we force
                 * the IRQ bit to toggle every once in a while.
                 */
                if (pThis->irq_toggle_counter)
                    pThis->irq_toggle_counter--;
                else
                {
                    pThis->irq_toggle_counter = 1000;
                    uValue ^= BMS_IRQ_BIT(pThis->irq);
                }
                break;
            case BMS_PORT_INIT:
                /* Read the 8255A control port. */
                uValue = pThis->ctrl_port;
                break;
            default:
                ASSERT_GUEST_MSG_FAILED(("invalid port %#x\n", offPort));
                uValue = 0xff;
                break;
        }

        *pu32 = uValue;
        Log2(("mouIoPortRead: offPort=%#x+%x cb=%d *pu32=%#x\n", BMS_IO_BASE, offPort, cb, uValue));
        LogRel3(("mouIoPortRead: read port %u: %#04x\n", offPort, uValue));
        return VINF_SUCCESS;
    }
    ASSERT_GUEST_MSG_FAILED(("offPort=%#x cb=%d\n", offPort, cb));
    return VERR_IOM_IOPORT_UNUSED;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT}
 */
static DECLCALLBACK(VBOXSTRICTRC) bmsIoPortWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    RT_NOREF(pvUser);
    if (cb == 1)
    {
        PBMSSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PBMSSTATE);
        LogRel3(("mouIoPortWrite: write port %u: %#04x\n", offPort, u32));

        switch (offPort)
        {
            case BMS_PORT_SIG:
                /* Update port B. */
                pThis->port_b = u32;
                break;
            case BMS_PORT_DATA:
                /* Do nothing, port A is not writable. */
                break;
            case BMS_PORT_INIT:
                pThis->ctrl_port = u32;
                break;
            case BMS_PORT_CTRL:
                /* Update the high nibble of port C. */
                pThis->port_c = (u32 & 0xF0) | (pThis->port_c & 0x0F);
                bmsUpdateCtrl(pDevIns, pThis);
                break;
            default:
                ASSERT_GUEST_MSG_FAILED(("invalid port %#x\n", offPort));
                break;
        }

        Log2(("mouIoPortWrite: offPort=%#x+%u cb=%d u32=%#x\n", BMS_IO_BASE, offPort, cb, u32));
    }
    else
        ASSERT_GUEST_MSG_FAILED(("offPort=%#x cb=%d\n", offPort, cb));
    return VINF_SUCCESS;
}

# ifdef IN_RING3

/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) bmsR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSMHandle)
{
    PBMSSTATE       pThis = PDMDEVINS_2_DATA(pDevIns, PBMSSTATE);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;

    /* 8255A state. */
    pHlp->pfnSSMPutU8(pSSMHandle, pThis->port_a);
    pHlp->pfnSSMPutU8(pSSMHandle, pThis->port_b);
    pHlp->pfnSSMPutU8(pSSMHandle, pThis->port_c);
    pHlp->pfnSSMPutU8(pSSMHandle, pThis->ctrl_port);
    /* Other device state. */
    pHlp->pfnSSMPutU8(pSSMHandle, pThis->cnt_held);
    pHlp->pfnSSMPutU8(pSSMHandle, pThis->held_dx);
    pHlp->pfnSSMPutU8(pSSMHandle, pThis->held_dy);
    pHlp->pfnSSMPutU8(pSSMHandle, pThis->irq);
    pHlp->pfnSSMPutU32(pSSMHandle, pThis->cTimerPeriodMs);
    /* Current mouse state deltas. */
    pHlp->pfnSSMPutS32(pSSMHandle, pThis->mouse_dx);
    pHlp->pfnSSMPutS32(pSSMHandle, pThis->mouse_dy);
    pHlp->pfnSSMPutU8(pSSMHandle, pThis->mouse_buttons_reported);
    /* Timer. */
    return PDMDevHlpTimerSave(pDevIns, pThis->hMouseTimer, pSSMHandle);
}

/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) bmsR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSMHandle, uint32_t uVersion, uint32_t uPass)
{
    PBMSSTATE       pThis = PDMDEVINS_2_DATA(pDevIns, PBMSSTATE);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;

    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);

    if (uVersion > BMS_SAVED_STATE_VERSION)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    /* 8255A state. */
    pHlp->pfnSSMGetU8(pSSMHandle, &pThis->port_a);
    pHlp->pfnSSMGetU8(pSSMHandle, &pThis->port_b);
    pHlp->pfnSSMGetU8(pSSMHandle, &pThis->port_c);
    pHlp->pfnSSMGetU8(pSSMHandle, &pThis->ctrl_port);
    /* Other device state. */
    pHlp->pfnSSMGetU8(pSSMHandle, &pThis->cnt_held);
    pHlp->pfnSSMGetU8(pSSMHandle, &pThis->held_dx);
    pHlp->pfnSSMGetU8(pSSMHandle, &pThis->held_dy);
    pHlp->pfnSSMGetU8(pSSMHandle, &pThis->irq);
    pHlp->pfnSSMGetU32(pSSMHandle, &pThis->cTimerPeriodMs);
    /* Current mouse state deltas. */
    pHlp->pfnSSMGetS32(pSSMHandle, &pThis->mouse_dx);
    pHlp->pfnSSMGetS32(pSSMHandle, &pThis->mouse_dy);
    pHlp->pfnSSMGetU8(pSSMHandle, &pThis->mouse_buttons_reported);
    /* Timer. */
    return PDMDevHlpTimerLoad(pDevIns, pThis->hMouseTimer, pSSMHandle);
}

/**
 * Reset notification.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance data.
 */
static DECLCALLBACK(void) bmsR3Reset(PPDMDEVINS pDevIns)
{
    PBMSSTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PBMSSTATE);
    PBMSSTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PBMSSTATER3);

    /* Reinitialize the timer. */
    pThis->cTimerPeriodMs = BMS_IRQ_PERIOD_MS / 2;
    PDMDevHlpTimerSetMillies(pDevIns, pThis->hMouseTimer, pThis->cTimerPeriodMs);

    /* Clear the device setup. */
    pThis->port_a = pThis->port_b = 0;
    pThis->port_c = BMS_CTL_INT_DIS;    /* Interrupts disabled. */
    pThis->ctrl_port = 0x91;            /* Default 8255A setup. */

    /* Clear motion/button state. */
    pThis->cnt_held = false;
    pThis->mouse_dx = pThis->mouse_dy = 0;
    pThis->mouse_buttons = 0;
    pThis->mouse_buttons_reported = 0;
    pThis->disable_counter = 0;
    pThis->irq_toggle_counter = 1000;

    if (pThis->mouse_enabled)
    {
        pThis->mouse_enabled = false;
        bmsR3UpdateDownstreamStatus(pThis, pThisCC);
    }
}


/* -=-=-=-=-=- Mouse: IBase  -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) bmsR3Base_QueryMouseInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PBMSSTATER3 pThisCC = RT_FROM_MEMBER(pInterface, BMSSTATER3, Mouse.IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThisCC->Mouse.IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMOUSEPORT, &pThisCC->Mouse.IPort);
    return NULL;
}


/* -=-=-=-=-=- Mouse: IMousePort  -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMIMOUSEPORT,pfnPutEvent}
 */
static DECLCALLBACK(int) bmsR3MousePort_PutEvent(PPDMIMOUSEPORT pInterface, int32_t dx,
                                                 int32_t dy, int32_t dz, int32_t dw,
                                                 uint32_t fButtons)
{
    PBMSSTATER3 pThisCC = RT_FROM_MEMBER(pInterface, BMSSTATER3, Mouse.IPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PBMSSTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PBMSSTATE);
    int rc = PDMDevHlpCritSectEnter(pDevIns, pDevIns->CTX_SUFF(pCritSectRo), VERR_SEM_BUSY);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, pDevIns->CTX_SUFF(pCritSectRo), rc);

    bmsR3MouseEvent(pThis, dx, dy, dz, dw, fButtons);

    PDMDevHlpCritSectLeave(pDevIns, pDevIns->CTX_SUFF(pCritSectRo));
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIMOUSEPORT,pfnPutEventAbs}
 */
static DECLCALLBACK(int) bmsR3MousePort_PutEventAbs(PPDMIMOUSEPORT pInterface, uint32_t x, uint32_t y,
                                                    int32_t dz, int32_t dw, uint32_t fButtons)
{
    RT_NOREF(pInterface, x, y, dz, dw, fButtons);
    AssertFailedReturn(VERR_NOT_SUPPORTED);
}

/**
 * @interface_method_impl{PDMIMOUSEPORT,pfnPutEventMultiTouch}
 */
static DECLCALLBACK(int) bmsR3MousePort_PutEventMultiTouch(PPDMIMOUSEPORT pInterface, uint8_t cContacts,
                                                           const uint64_t *pau64Contacts, uint32_t u32ScanTime)
{
    RT_NOREF(pInterface, cContacts, pau64Contacts, u32ScanTime);
    AssertFailedReturn(VERR_NOT_SUPPORTED);
}

/* -=-=-=-=-=- setup code -=-=-=-=-=- */


/**
 * @interface_method_impl{PDMDEVREGR3,pfnAttach}
 */
static DECLCALLBACK(int) bmsR3Attach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PBMSSTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PBMSSTATER3);
    int         rc;

    AssertMsgReturn(fFlags & PDM_TACH_FLAGS_NOT_HOT_PLUG,
                    ("Bus mouse device does not support hotplugging\n"),
                    VERR_INVALID_PARAMETER);

    switch (iLUN)
    {
        /* LUN #0: mouse */
        case 0:
            rc = PDMDevHlpDriverAttach(pDevIns, iLUN, &pThisCC->Mouse.IBase, &pThisCC->Mouse.pDrvBase, "Bus Mouse Port");
            if (RT_SUCCESS(rc))
            {
                pThisCC->Mouse.pDrv = PDMIBASE_QUERY_INTERFACE(pThisCC->Mouse.pDrvBase, PDMIMOUSECONNECTOR);
                if (!pThisCC->Mouse.pDrv)
                {
                    AssertLogRelMsgFailed(("LUN #0 doesn't have a mouse interface! rc=%Rrc\n", rc));
                    rc = VERR_PDM_MISSING_INTERFACE;
                }
            }
            else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
            {
                LogRel(("%s/%d: Warning: no driver attached to LUN #0!\n", pDevIns->pReg->szName, pDevIns->iInstance));
                rc = VINF_SUCCESS;
            }
            else
                AssertLogRelMsgFailed(("Failed to attach LUN #0! rc=%Rrc\n", rc));
            break;

        default:
            AssertMsgFailed(("Invalid LUN #%d\n", iLUN));
            return VERR_PDM_NO_SUCH_LUN;
    }

    return rc;
}


/**
 * @interface_method_impl{PDMDEVREGR3,pfnDetach}
 */
static DECLCALLBACK(void) bmsR3Detach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
#  if 0
    /*
     * Reset the interfaces and update the controller state.
     */
    PBMSSTATE   pThis = PDMDEVINS_2_DATA(pDevIns, PBMSSTATE);
    switch (iLUN)
    {
        /* LUN #0: mouse */
        case 0:
            pThis->Mouse.pDrv = NULL;
            pThis->Mouse.pDrvBase = NULL;
            break;

        default:
            AssertMsgFailed(("Invalid LUN #%d\n", iLUN));
            break;
    }
#  else
    RT_NOREF(pDevIns, iLUN, fFlags);
#  endif
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) bmsR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PBMSSTATE       pThis   = PDMDEVINS_2_DATA(pDevIns, PBMSSTATE);
    PBMSSTATER3     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PBMSSTATER3);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    int             rc;
    RT_NOREF(iInstance);

    Assert(iInstance == 0);

    /*
     * Validate and read the configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "IRQ", "");

    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "IRQ", &pThis->irq, BMS_DEFAULT_IRQ);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Failed to query \"IRQ\" from the config"));
    if (pThis->irq < 2 || pThis->irq > 5)
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Invalid \"IRQ\" config setting"));

    Log(("busmouse: IRQ=%u fRCEnabled=%RTbool fR0Enabled=%RTbool\n", pThis->irq, pDevIns->fRCEnabled, pDevIns->fR0Enabled));

    /*
     * Initialize the interfaces.
     */
    pThisCC->pDevIns                            = pDevIns;
    pThisCC->Mouse.IBase.pfnQueryInterface      = bmsR3Base_QueryMouseInterface;
    pThisCC->Mouse.IPort.pfnPutEvent            = bmsR3MousePort_PutEvent;
    pThisCC->Mouse.IPort.pfnPutEventAbs         = bmsR3MousePort_PutEventAbs;
    pThisCC->Mouse.IPort.pfnPutEventTouchScreen = bmsR3MousePort_PutEventMultiTouch;
    pThisCC->Mouse.IPort.pfnPutEventTouchPad    = bmsR3MousePort_PutEventMultiTouch;

    /*
     * Create the interrupt timer.
     */
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL, bmsR3TimerCallback, pThis,
                              TMTIMER_FLAGS_DEFAULT_CRIT_SECT | TMTIMER_FLAGS_NO_RING0, "Bus Mouse", &pThis->hMouseTimer);
    AssertRCReturn(rc, rc);

    /*
     * Register I/O ports.
     */
    static const IOMIOPORTDESC s_aDescs[] =
    {
        { "DATA", "DATA", NULL, NULL },
        { "SIG",  "SIG",  NULL, NULL },
        { "CTRL", "CTRL", NULL, NULL },
        { "INIT", "INIT", NULL, NULL },
        { NULL,   NULL,   NULL, NULL }
    };
    rc = PDMDevHlpIoPortCreateAndMap(pDevIns, BMS_IO_BASE, BMS_IO_SIZE, bmsIoPortWrite, bmsIoPortRead,
                                     "Bus Mouse", s_aDescs, &pThis->hIoPorts);
    AssertRCReturn(rc, rc);

    /*
     * Register saved state.
     */
    rc = PDMDevHlpSSMRegister(pDevIns, BMS_SAVED_STATE_VERSION, sizeof(*pThis), bmsR3SaveExec, bmsR3LoadExec);
    AssertRCReturn(rc, rc);

    /*
     * Attach to the mouse driver.
     */
    rc = bmsR3Attach(pDevIns, 0, PDM_TACH_FLAGS_NOT_HOT_PLUG);
    AssertRCReturn(rc, rc);

    /*
     * Initialize the device state.
     */
    bmsR3Reset(pDevIns);

    return VINF_SUCCESS;
}

# else /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) bmsRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PBMSSTATE   pThis = PDMDEVINS_2_DATA(pDevIns, PBMSSTATE);

    int rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPorts, bmsIoPortWrite, bmsIoPortRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}

# endif /* !IN_RING3 */


/**
 * The device registration structure.
 */
static const PDMDEVREG g_DeviceBusMouse =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "busmouse",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS /** @todo | PDM_DEVREG_FLAGS_RZ */ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_INPUT,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(BMSSTATE),
    /* .cbInstanceCC = */           CTX_EXPR(sizeof(BMSSTATER3), 0, 0),
    /* .cbInstanceRC = */           0,
    /* .cMaxPciDevices = */         0,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Microsoft Bus Mouse controller. LUN #0 is the mouse connector.",
# if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           bmsR3Construct,
    /* .pfnDestruct = */            NULL,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               bmsR3Reset,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              bmsR3Attach,
    /* .pfnDetach = */              bmsR3Detach,
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
# elif defined(IN_RING0)
    /* .pfnEarlyConstruct = */      NULL,
    /* .pfnConstruct = */           bmsRZConstruct,
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
# elif defined(IN_RC)
    /* .pfnConstruct = */           bmsRZConstruct,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
# else
#  error "Not in IN_RING3, IN_RING0 or IN_RC!"
# endif
    /* .u32VersionEnd = */          PDM_DEVREG_VERSION
};

# ifdef VBOX_IN_EXTPACK_R3

/**
 * @callback_method_impl{FNPDMVBOXDEVICESREGISTER}
 */
extern "C" DECLEXPORT(int) VBoxDevicesRegister(PPDMDEVREGCB pCallbacks, uint32_t u32Version)
{
    AssertLogRelMsgReturn(u32Version >= VBOX_VERSION,
                          ("u32Version=%#x VBOX_VERSION=%#x\n", u32Version, VBOX_VERSION),
                          VERR_EXTPACK_VBOX_VERSION_MISMATCH);
    AssertLogRelMsgReturn(pCallbacks->u32Version == PDM_DEVREG_CB_VERSION,
                          ("pCallbacks->u32Version=%#x PDM_DEVREG_CB_VERSION=%#x\n", pCallbacks->u32Version, PDM_DEVREG_CB_VERSION),
                          VERR_VERSION_MISMATCH);

    return pCallbacks->pfnRegister(pCallbacks, &g_DeviceBusMouse);
}

# else  /* !VBOX_IN_EXTPACK_R3 */

/** Pointer to the ring-0 device registrations for the Bus Mouse. */
static PCPDMDEVREGR0 g_apDevRegs[] =
{
    &g_DeviceBusMouse,
};

/** Module device registration record for the Bus Mouse. */
static PDMDEVMODREGR0 g_ModDevReg =
{
    /* .u32Version = */ PDM_DEVMODREGR0_VERSION,
    /* .cDevRegs = */   RT_ELEMENTS(g_apDevRegs),
    /* .papDevRegs = */ &g_apDevRegs[0],
    /* .hMod = */       NULL,
    /* .ListEntry = */  { NULL, NULL },
};

DECLEXPORT(int)  ModuleInit(void *hMod)
{
    LogFlow(("VBoxBusMouseRZ/ModuleInit: %p\n", hMod));
    return PDMR0DeviceRegisterModule(hMod, &g_ModDevReg);
}

DECLEXPORT(void) ModuleTerm(void *hMod)
{
    LogFlow(("VBoxBusMouseRZ/ModuleTerm: %p\n", hMod));
    PDMR0DeviceDeregisterModule(hMod, &g_ModDevReg);
}

# endif  /* !VBOX_IN_EXTPACK_R3 */

#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */

