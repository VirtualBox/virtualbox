/* $Id: tstVBoxNatGuestSideInternal.h 114884 2026-08-07 07:22:52Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxNetSlirpNAT guest-side testcase hooks.
 */

/*
 * Copyright (C) 2026 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_testcase_tstVBoxNatGuestSideInternal_h
#define VBOX_INCLUDED_SRC_testcase_tstVBoxNatGuestSideInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/net.h>
#include <iprt/types.h>

RT_C_DECLS_BEGIN

/** Opaque test instance owning one restricted libslirp datapath. */
typedef struct VBOXNETSLIRPNATTEST *PVBOXNETSLIRPNATTEST;

/**
 * Callback for an Ethernet frame emitted toward the guest.
 *
 * The frame is valid only for the duration of the callback.  The callback is
 * made synchronously by VBoxNetSlirpNATTestInput or VBoxNetSlirpNATTestPoll;
 * no callback may occur after either function returns.
 *
 * @returns VINF_SUCCESS on success, or an IPRT error to propagate to the
 *          currently executing test-hook call.
 * @param   pvFrame Frame bytes emitted toward the guest.
 * @param   cbFrame Number of valid bytes in @a pvFrame.
 * @param   pvUser  User pointer supplied to VBoxNetSlirpNATTestCreate.
 */
typedef DECLCALLBACKTYPE(int, FNVBOXNETSLIRPNATTESTOUTPUT,(const void *pvFrame, size_t cbFrame, void *pvUser));
/** Pointer to a guest-output callback. */
typedef FNVBOXNETSLIRPNATTESTOUTPUT *PFNVBOXNETSLIRPNATTESTOUTPUT;

/** Deterministic IPv4 configuration for the test-only NAT datapath wrapper. */
typedef struct VBOXNETSLIRPNATTESTCFG
{
    /** IPv4 network address in IPRT/network representation. */
    RTNETADDRIPV4 IPv4Network;
    /** IPv4 netmask in IPRT/network representation. */
    RTNETADDRIPV4 IPv4Netmask;
    /** Guest-visible gateway address. */
    RTNETADDRIPV4 IPv4Host;
    /** First address in the DHCP pool. */
    RTNETADDRIPV4 IPv4DhcpStart;
    /** Guest-visible DNS proxy address advertised by DHCP. */
    RTNETADDRIPV4 IPv4Nameserver;
    /** Whether libslirp DHCP is enabled. */
    bool          fDhcp;
    /** Whether the libslirp DNS proxy is enabled. */
    bool          fDns;
    /** Whether host-network access is disabled.  Tests require true. */
    bool          fRestricted;
} VBOXNETSLIRPNATTESTCFG;
/** Pointer to a NAT testcase configuration. */
typedef VBOXNETSLIRPNATTESTCFG *PVBOXNETSLIRPNATTESTCFG;
/** Pointer to a const NAT testcase configuration. */
typedef const VBOXNETSLIRPNATTESTCFG *PCVBOXNETSLIRPNATTESTCFG;

/**
 * Create one guest-side test context (without COM).
 *
 * The implementation must validate that @a pCfg requests a restricted IPv4
 * network and leave frame transport to @a pfnOutput. It intentionally does
 * not instantiate the COM VBoxNetSlirpNAT service; its configuration is
 * locked down for gest packet tests.  The returned handle is
 * single-thread affine: Input and Poll must not be called concurrently.
 *
 * @returns VINF_SUCCESS on success, or an IPRT/VBox error.
 * @param   pCfg         NAT configuration.
 * @param   pfnOutput    Required callback for frames sent toward the guest.
 * @param   pvOutputUser User pointer passed to @a pfnOutput.
 * @param   phNat        Where to return the opaque test handle.  Set to NULL
 *                       on failure.
 */
DECLHIDDEN(int) VBoxNetSlirpNATTestCreate(PCVBOXNETSLIRPNATTESTCFG pCfg,
                                          PFNVBOXNETSLIRPNATTESTOUTPUT pfnOutput,
                                          void *pvOutputUser, PVBOXNETSLIRPNATTEST *phNat);

/**
 * Destroy a test instance.
 *
 * The implementation must cancel timers, release libslirp state, and guarantee
 * that no output callback remains in flight when it returns.
 *
 * @param   hNat Test instance.  NULL is accepted as a no-op.
 */
DECLHIDDEN(void) VBoxNetSlirpNATTestDestroy(PVBOXNETSLIRPNATTEST hNat);

/**
 * Inject one complete guest Ethernet frame through the same validation and
 * slirp-input path used by VBoxNetSlirpNAT::processFrame.
 *
 * Immediate output must be delivered before this function returns.  Like
 * VBoxNetSlirpNAT::processFrame, frames smaller than an Ethernet header or
 * larger than 1522 bytes are dropped successfully.  NULL input, zero size, or
 * input larger than 64 KiB returns VERR_INVALID_PARAMETER.
 *
 * @returns VINF_SUCCESS when accepted or deliberately dropped, an error from
 *          argument validation/libslirp, or an output-callback error.
 * @param   hNat    Test instance.
 * @param   pvFrame Guest Ethernet frame bytes.
 * @param   cbFrame Number of bytes in @a pvFrame.
 */
DECLHIDDEN(int) VBoxNetSlirpNATTestInput(PVBOXNETSLIRPNATTEST hNat,
                                         const void *pvFrame, size_t cbFrame);

/**
 * Progress production libslirp timers and pending socket work for at most the
 * requested interval.
 *
 * This call must not access the unrestricted host network when fRestricted is
 * set.  Output callbacks are completed before the function returns.
 *
 * @returns VINF_SUCCESS, an IPRT/VBox poll error, or an output-callback error.
 * @param   hNat   Test instance.
 * @param   cMsMax Maximum poll duration in milliseconds.
 */
DECLHIDDEN(int) VBoxNetSlirpNATTestPoll(PVBOXNETSLIRPNATTEST hNat, uint32_t cMsMax);

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_testcase_tstVBoxNatGuestSideInternal_h */
