/** @file
 * PDM - Pluggable Device Manager, GICv3 Interface.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_vmm_pdmgic_h
#define VBOX_INCLUDED_vmm_pdmgic_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/types.h>
#include <iprt/assertcompile.h>
struct PDMDEVREGCB;

/** @defgroup grp_pdm_gic    The local GIC PDM API
 * @ingroup grp_pdm
 * @{
 */

RT_C_DECLS_BEGIN

/**
 * The type of PDM GIC backend.
 */
typedef enum PDMGICBACKENDTYPE
{
    /** None/Invalid PDM GIC backend. */
    PDMGICBACKENDTYPE_NONE = 0,
    /** VirtualBox backend. */
    PDMGICBACKENDTYPE_VBOX,
    /** KVM backend. */
    PDMGICBACKENDTYPE_KVM,
    /** Hyper-V backend. */
    PDMGICBACKENDTYPE_HYPERV,
    /** Hypervisor.Framework backend. */
    PDMGICBACKENDTYPE_HVF,
    /** End of valid PDM GIC backend values. */
    PDMGICBACKENDTYPE_END,
    /** The usual 32-bit paranoia. */
    PDMGICBACKENDTYPE_32BIT_HACK = 0x7fffffff
} PDMGICBACKENDTYPE;

/**
 * PDM GIC backend ring-3 API.
 */
typedef struct PDMGICBACKENDR3
{
    /**
     * Reads a GIC system register.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu       The cross context virtual CPU structure.
     * @param   u32Reg      The system register being read.
     * @param   pu64Value   Where to store the read value.
     */
    DECLR3CALLBACKMEMBER(VBOXSTRICTRC, pfnReadSysReg, (PVMCPUCC pVCpu, uint32_t u32Reg, uint64_t *pu64Value));

    /**
     * Writes an GIC system register.
     *
     * @returns Strict VBox status code.
     * @param   pVCpu       The cross context virtual CPU structure.
     * @param   u32Reg      The system register being written (IPRT system  register identifier).
     * @param   u64Value    The value to write.
     */
    DECLR3CALLBACKMEMBER(VBOXSTRICTRC, pfnWriteSysReg, (PVMCPUCC pVCpu, uint32_t u32Reg, uint64_t u64Value));

    /**
     * Sets the specified shared peripheral interrupt.
     *
     * @returns VBox status code.
     * @param   pVM         The cross context virtual machine structure.
     * @param   uSpiIntId   The SPI ID (minus GIC_INTID_RANGE_SPI_START) to
     *                      assert/de-assert.
     * @param   fAsserted   Flag whether to mark the interrupt as asserted/de-asserted.
     */
    DECLR3CALLBACKMEMBER(int, pfnSetSpi, (PVMCC pVM, uint32_t uSpiIntId, bool fAsserted));

    /**
     * Sets the specified private peripheral interrupt.
     *
     * @returns VBox status code.
     * @param   pVCpu       The cross context virtual CPU structure.
     * @param   uPpiIntId   The PPI ID (minus GIC_INTID_RANGE_PPI_START) to
     *                      assert/de-assert.
     * @param   fAsserted   Flag whether to mark the interrupt as asserted/de-asserted.
     */
    DECLR3CALLBACKMEMBER(int, pfnSetPpi, (PVMCPUCC pVCpu, uint32_t uPpiIntId, bool fAsserted));

    /**
     * Sends an MSI to the GIC ITS.
     *
     * @returns VBox status code.
     * @param   pVM         The cross context virtual machine structure.
     * @param   uBusDevFn   The bus:device:function of the device initiating the MSI.
     *                      Cannot be NIL_PCIBDF.
     * @param   pMsi        The MSI to send.
     * @param   uTagSrc     The IRQ tag and source (for tracing).
     */
    DECLR3CALLBACKMEMBER(int, pfnSendMsi, (PVMCC pVM, PCIBDF uBusDevFn, PCMSIMSG pMsi, uint32_t uTagSrc));

    /** @name Reserved for future (MBZ).
     * @{ */
    DECLR3CALLBACKMEMBER(int, pfnReserved5, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved6, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved7, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved8, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved9, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved10, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved11, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved12, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved13, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved14, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved15, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved16, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved17, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved18, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved19, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved20, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved21, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved22, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved23, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved24, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved25, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved26, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved27, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved28, (void));
    DECLR3CALLBACKMEMBER(int, pfnReserved29, (void));
    /** @} */
} PDMGICBACKENDR3;
/** Pointer to ring-3 GIC backend. */
typedef R3PTRTYPE(struct PDMGICBACKENDR3 *) PPDMGICBACKENDR3;
/** Const pointer to ring-3 GIC backend. */
typedef R3PTRTYPE(const struct PDMGICBACKENDR3 *) PCPDMGICBACKENDR3;
AssertCompileSizeAlignment(PDMGICBACKENDR3, 8);

/**
 * PDM GIC backend ring-0 API.
 */
typedef struct PDMGICBACKENDR0
{
    /** @name Reserved for future (MBZ).
     * @{ */
    DECLR0CALLBACKMEMBER(int, pfnReserved0, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved1, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved2, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved3, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved4, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved5, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved6, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved7, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved8, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved9, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved10, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved11, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved12, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved13, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved14, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved15, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved16, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved17, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved18, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved19, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved20, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved21, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved22, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved23, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved24, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved25, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved26, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved27, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved28, (void));
    DECLR0CALLBACKMEMBER(int, pfnReserved29, (void));
    /** @} */
} PDMGICBACKENDR0;
/** Pointer to ring-0 GIC backend. */
typedef R0PTRTYPE(struct PDMGICBACKENDR0 *) PPDMGICBACKENDR0;
/** Const pointer to ring-0 GIC backend. */
typedef R0PTRTYPE(const struct PDMGICBACKENDR0 *) PCPDMGICBACKENDR0;
AssertCompileSizeAlignment(PDMGICBACKENDR0, 8);

/**
 * PDM GIC backend RC API.
 */
typedef struct PDMGICBACKENDRC
{
    /** @name Reserved for future (MBZ).
     * @{ */
    DECLRCCALLBACKMEMBER(int, pfnReserved0, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved1, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved2, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved3, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved4, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved5, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved6, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved7, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved8, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved9, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved10, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved11, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved12, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved13, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved14, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved15, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved16, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved17, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved18, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved19, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved20, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved21, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved22, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved23, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved24, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved25, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved26, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved27, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved28, (void));
    DECLRCCALLBACKMEMBER(int, pfnReserved29, (void));
    /** @} */
} PDMGICBACKENDRC;
/** Pointer to raw-mode context GIC backend. */
typedef RCPTRTYPE(struct PDMGICBACKENDRC *) PPDMGICBACKENDRC;
/** Const pointer to raw-mode context GIC backend. */
typedef RCPTRTYPE(const struct PDMGICBACKENDRC *) PCPDMGICBACKENDRC;
AssertCompileSizeAlignment(PDMGICBACKENDRC, 8);
AssertCompile(sizeof(PDMGICBACKENDR3) == sizeof(PDMGICBACKENDR0));

/** @typedef PDMGICBACKENDR3
 * A current context PDM GIC backend. */
/** @typedef PPDMGICBACKENDR3
 * Pointer to a current context PDM GIC backend. */
/** @typedef PCPDMGICBACKENDR3
 * Pointer to a const current context PDM GIC backend. */
#if defined(IN_RING3) || defined(DOXYGEN_RUNNING)
typedef PDMGICBACKENDR3        PDMGICBACKEND;
typedef PPDMGICBACKENDR3       PPDMGICBACKEND;
typedef PCPDMGICBACKENDR3      PCPDMGICBACKEND;
#elif defined(IN_RING0)
typedef PDMGICBACKENDR0        PDMGICBACKEND;
typedef PPDMGICBACKENDR0       PPDMGICBACKEND;
typedef PCPDMGICBACKENDR0      PCPDMGICBACKEND;
#elif defined(IN_RC)
typedef PDMGICBACKENDRC        PDMGICBACKEND;
typedef PPDMGICBACKENDRC       PPDMGICBACKEND;
typedef PCPDMGICBACKENDRC      PCPDMGICBACKEND;
#else
# error "Not IN_RING3, IN_RING0 or IN_RC"
#endif

VMM_INT_DECL(int)           PDMGicRegisterBackend(PVMCC pVM, PDMGICBACKENDTYPE enmBackendType, PCPDMGICBACKEND pBackend);

VMM_INT_DECL(VBOXSTRICTRC)  PDMGicReadSysReg(PVMCPUCC pVCpu, uint32_t u32Reg, uint64_t *pu64Value);
VMM_INT_DECL(VBOXSTRICTRC)  PDMGicWriteSysReg(PVMCPUCC pVCpu, uint32_t u32Reg, uint64_t u64Value);
VMM_INT_DECL(int)           PDMGicSetSpi(PVMCC pVM, uint32_t uSpiIntId, bool fAsserted);
VMM_INT_DECL(int)           PDMGicSetPpi(PVMCPUCC pVCpu, uint32_t uIntId, bool fAsserted);

RT_C_DECLS_END

/** @} */

#endif /* !VBOX_INCLUDED_vmm_pdmgic_h */

