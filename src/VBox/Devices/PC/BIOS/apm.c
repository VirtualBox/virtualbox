/* $Id: apm.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * APM BIOS support. Implements APM version 1.2.
 */

/*
 * Copyright (C) 2004-2024 Oracle and/or its affiliates.
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

#include <stdint.h>
#include <string.h>
#include "biosint.h"
#include "inlines.h"
#include "VBox/bios.h"

#if DEBUG_APM
#  define BX_DEBUG_APM(...) BX_DEBUG(__VA_ARGS__)
#else
#  define BX_DEBUG_APM(...)
#endif

/* Implemented in assembly. */
extern void apm_pm16_entry(void);
#pragma aux apm_pm16_entry "*"

#if VBOX_BIOS_CPU >= 80386
extern void apm_pm32_entry(void);
#pragma aux apm_pm32_entry "*"
#endif

/* APM function codes. */
enum apm_func {
    APM_CHECK   = 0x00,     /* APM Installation Check */
    APM_RM_CONN = 0x01,     /* APM Real Mode Interface Connect */
    APM_PM_CONN = 0x02,     /* APM Protected Mode 16-bit Interface Connect */
    APM_32_CONN = 0x03,     /* APM Protected Mode 32-bit Interface Connect */
    APM_DISCONN = 0x04,     /* APM Interface Disconnect */
    APM_IDLE    = 0x05,     /* CPU Idle */
    APM_BUSY    = 0x06,     /* CPU Busy */
    APM_SET_PWR = 0x07,     /* Set Power State */
    APM_ENBL_PM = 0x08,     /* Enable/Disable Power Management */
    APM_SET_DFL = 0x09,     /* Restore APM BIOS Power-On Defaults */
    APM_STATUS  = 0x0A,     /* Get Power Status */
    APM_GET_EVT = 0x0B,     /* Get PM Event */
    APM_GET_PWR = 0x0C,     /* Get Power State */
    APM_DEVPM   = 0x0D,     /* Enable/Disable Device Power Management */
    APM_DRV_VER = 0x0E,     /* APM Driver Version */
    APM_ENGAGE  = 0x0F,     /* Engage/Disengage Power Management */
    APM_GET_CAP = 0x10      /* Get Capabilities */
};

enum apm_error {
    APM_ERR_PM_DISABLED = 0x01,     /* Power Management functionality disabled */
    APM_ERR_RM_INUSE    = 0x02,     /* Real mode interface connection already established */
    APM_ERR_NOT_CONN    = 0x03,     /* Interface not connected */
    APM_ERR_PM_16_INUSE = 0x05,     /* 16-bit protected mode interface connection already established */
    APM_ERR_NO_PM_16    = 0x06,     /* 16-bit protected mode interface not supported */
    APM_ERR_PM_32_INUSE = 0x07,     /* 32-bit protected mode interface connection already established */
    APM_ERR_NO_PM_32    = 0x08,     /* 32-bit protected mode interface not supported */
    APM_ERR_BAD_DEV_ID  = 0x09,     /* Unrecognized device ID */
    APM_ERR_INVAL_PARAM = 0x0A,     /* Parameter out of range */
    APM_ERR_NOT_ENGAGED = 0x0B,     /* Interface not engaged */
    APM_ERR_UNSUPPORTED = 0x0C,     /* Function not supported */
    APM_ERR_NO_RSM_TMR  = 0x0D,     /* Resume timer disabled */
    APM_ERR_NO_EVENTS   = 0x80      /* No power management events pending */
};

enum apm_power_state {
    APM_PS_ENABLED      = 0x00,     /* APM enabled */
    APM_PS_STANDBY      = 0x01,     /* Standby */
    APM_PS_SUSPEND      = 0x02,     /* Suspend */
    APM_PS_OFF          = 0x03,     /* Suspend */
};

/// @todo merge with system.c
#define AX      r.gr.u.r16.ax
#define BX      r.gr.u.r16.bx
#define CX      r.gr.u.r16.cx
#define DX      r.gr.u.r16.dx
#define SI      r.gr.u.r16.si
#define DI      r.gr.u.r16.di
#define BP      r.gr.u.r16.bp
#define SP      r.gr.u.r16.sp
#define FLAGS   r.fl.u.r16.flags
#define EAX     r.gr.u.r32.eax
#define EBX     r.gr.u.r32.ebx
#define ECX     r.gr.u.r32.ecx
#define EDX     r.gr.u.r32.edx
#define ES      r.es

#define APM_BIOS_SEG        0xF000      /* Real-mode APM segment. */
#define APM_BIOS_SEG_LEN    0xFFF0      /* Length of APM segment. */

/* The APM BIOS interface uses 32-bit registers *only* in the 32-bit
 * protected mode connect call. Rather than saving/restoring 32-bit
 * registers all the time, simply set the high words of those registers
 * when necessary.
 */
void set_ebx_hi(uint16_t val);
#pragma aux set_ebx_hi =    \
    ".386"                  \
    "shl    ebx, 16"        \
    parm [bx] modify exact [bx] nomemory;

void set_esi_hi(uint16_t val);
#pragma aux set_esi_hi =    \
    ".386"                  \
    "shl    esi, 16"        \
    parm [si] modify exact [si] nomemory;


/* The APM handler has unique requirements. It must be callable from real and
 * protected mode, both 16-bit and 32-bit. In protected mode, the caller must
 * ensure that appropriate selectors are available; these only cover the BIOS
 * code and data, hence the BIOS Data Area or EBDA cannot be accessed. CMOS is
 * a good place to store information which needs to be accessible from several
 * different contexts.
 *
 * Note that the 32-bit protected-mode handler only needs to thunk down to the
 * 16-bit code. There's no need for separate 16-bit and 32-bit implementation.
 */

/* Wrapper to avoid unnecessary inlining. */
void apm_out_str(const char *s)
{
    if (*s)
        out_ctrl_str_asm(VBOX_BIOS_SHUTDOWN_PORT, s);
}

void BIOSCALL apm_function(sys_regs_t r)
{
    BX_DEBUG_APM("APM: AX=%04X BX=%04X CX=%04X\n", AX, BX, CX);

    CLEAR_CF();         /* Boldly expect success. */
    switch (GET_AL()) {
    case APM_CHECK:
        AX = 0x0102;    /* Version 1.2 */
        BX = 0x504D;    /* 'PM' */
        CX = 3;         /* Bits 0/1: 16-bit/32-bit PM interface */
        break;
    case APM_RM_CONN:
        /// @todo validate device ID
        /// @todo validate current connection state
        /// @todo change connection state
        break;
#if VBOX_BIOS_CPU >= 80286
    case APM_PM_CONN:
        /// @todo validate device ID
        /// @todo validate current connection state
        /// @todo change connection state
        AX = APM_BIOS_SEG;              /* 16-bit PM code segment (RM segment base). */
        BX = (uint16_t)apm_pm16_entry;  /* 16-bit PM entry point offset. */
        CX = APM_BIOS_SEG;              /* 16-bit data segment. */
        SI = APM_BIOS_SEG_LEN;          /* 16-bit PM code segment length. */
        DI = APM_BIOS_SEG_LEN;          /* Data segment length. */
        break;
#endif
#if VBOX_BIOS_CPU >= 80386
    case APM_32_CONN:
        /// @todo validate device ID
        /// @todo validate current connection state
        /// @todo change connection state
        AX = APM_BIOS_SEG;              /* 32-bit PM code segment (RM segment base). */
        BX = (uint16_t)apm_pm32_entry;  /* 32-bit entry point offset. */
        CX = APM_BIOS_SEG;              /* 16-bit code segment. */
        DX = APM_BIOS_SEG;              /* 16-bit data segment. */
        SI = APM_BIOS_SEG_LEN;          /* 32-bit code segment length. */
        DI = APM_BIOS_SEG_LEN;          /* Data segment length. */
        set_ebx_hi(0);
        set_esi_hi(APM_BIOS_SEG_LEN);   /* 16-bit code segment length. */
        break;
#endif
    case APM_IDLE:
        int_enable();   /* Simply halt the CPU with interrupts enabled. */
        halt();
        break;
    case APM_SET_PWR:
        /// @todo validate device ID
        /// @todo validate current connection state
        switch (CX) {
        case APM_PS_STANDBY:
            apm_out_str("Standby");
            break;
        case APM_PS_SUSPEND:
            apm_out_str("Suspend");
            break;
        case APM_PS_OFF:
            apm_out_str("Shutdown");    /* Should not return. */
            break;
        default:
            SET_AH(APM_ERR_INVAL_PARAM);
            SET_CF();
        }
        break;
    case APM_DRV_VER:
        AX = 0x0102;    /// @todo Not right - must take driver version into account!
        break;
    case APM_DISCONN:
        /// @todo actually perform a disconnect...
    case APM_BUSY:      /* Nothing to do as APM Idle doesn't slow CPU clock. */
        break;
    case APM_STATUS:
        /* We do not attempt to report battery status. */
        BX = 0x01FF;    /* AC line power, battery unknown. */
        CX = 0x80FF;    /* No battery. */
        DX = 0xFFFF;    /* No idea about remaining battery life. */
        break;
    case APM_GET_EVT:
        /// @todo error should be different if interface not connected + engaged
        SET_AH(APM_ERR_NO_EVENTS);  /* PM events don't happen. */
        SET_CF();
        break;
    default:
        BX_INFO("APM: Unsupported function AX=%04X BX=%04X called\n", AX, BX);
        SET_AH(APM_ERR_UNSUPPORTED);
        SET_CF();
    }
}
