/* $Id: tstDeviceStructSizeRC.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * tstDeviceStructSizeGC - Generate structure member and size checks from the RC perspective.
 *
 * This is built using the VBoxRc template but linked into a host
 * ring-3 executable, rather hacky.
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


/*
 * Sanity checks.
 */
#ifndef IN_RC
# error Incorrect template!
#endif
#if defined(IN_RING3) || defined(IN_RING0)
# error Incorrect template!
#endif


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define VBOX_DEVICE_STRUCT_TESTCASE
#define VBOX_WITH_HGCM                  /* grumble */

/* Check that important preprocessor macros does not get redefined: */
#include <VBox/cdefs.h>
#include <VBox/log.h>
#ifdef DEBUG
# define VBOX_DEVICE_STRUCT_TESTCASE_CHECK_DEBUG
#else
# undef  VBOX_DEVICE_STRUCT_TESTCASE_CHECK_DEBUG
#endif
#ifdef LOG_ENABLED
# define VBOX_DEVICE_STRUCT_TESTCASE_CHECK_LOG_ENABLED
#else
# undef  VBOX_DEVICE_STRUCT_TESTCASE_CHECK_LOG_ENABLED
#endif
#ifdef VBOX_STRICT
# define VBOX_DEVICE_STRUCT_TESTCASE_CHECK_VBOX_STRICT
#else
# undef  VBOX_DEVICE_STRUCT_TESTCASE_CHECK_VBOX_STRICT
#endif
#ifdef RT_STRICT
# define VBOX_DEVICE_STRUCT_TESTCASE_CHECK_RT_STRICT
#else
# undef  VBOX_DEVICE_STRUCT_TESTCASE_CHECK_RT_STRICT
#endif

/* The structures we're checking: */
#undef LOG_GROUP
#include "../Bus/DevPciInternal.h"
#undef LOG_GROUP
#include "../EFI/DevSmc.cpp"
#undef LOG_GROUP
#include "../Graphics/DevVGA.cpp"
#undef LOG_GROUP
#include "../Input/DevPS2.cpp"
#undef LOG_GROUP
#include "../Input/PS2K.cpp"
#undef LOG_GROUP
#include "../Input/PS2M.cpp"
#undef LOG_GROUP
#include "../Network/DevPCNet.cpp"
#undef LOG_GROUP
#include "../PC/DevACPI.cpp"
#undef LOG_GROUP
#include "../PC/DevPIC.cpp"
#undef LOG_GROUP
#include "../PC/DevPit-i8254.cpp"
#undef LOG_GROUP
#include "../PC/DevRTC.cpp"
# undef LOG_GROUP
# include "../../VMM/VMMR3/APIC.cpp"
#undef LOG_GROUP
#include "../PC/DevIoApic.cpp"
#undef LOG_GROUP
#include "../PC/DevDMA.cpp"
#undef LOG_GROUP
#include "../Storage/DevATA.cpp"
#ifdef VBOX_WITH_USB
# undef LOG_GROUP
# include "../USB/DevOHCI.cpp"
# ifdef VBOX_WITH_EHCI_IMPL
#  undef LOG_GROUP
#  include "../USB/DevEHCI.cpp"
# endif
# ifdef VBOX_WITH_XHCI_IMPL
#  undef LOG_GROUP
#  include "../USB/DevXHCI.cpp"
# endif
#endif
#undef LOG_GROUP
#include "../VMMDev/VMMDev.cpp"
#undef LOG_GROUP
#include "../Parallel/DevParallel.cpp"
#undef LOG_GROUP
#include "../Serial/DevSerial.cpp"
#undef LOG_GROUP
#include "../Serial/DevOxPcie958.cpp"
#undef LOG_GROUP
#include "../Serial/UartCore.h"
#ifdef VBOX_WITH_AHCI
# undef LOG_GROUP
# include "../Storage/DevAHCI.cpp"
#endif
#ifdef VBOX_WITH_E1000
# undef LOG_GROUP
# include "../Network/DevE1000.cpp"
#endif
#ifdef VBOX_WITH_BUSLOGIC
# undef LOG_GROUP
# include "../Storage/DevBusLogic.cpp"
#endif
#ifdef VBOX_WITH_LSILOGIC
# undef LOG_GROUP
# include "../Storage/DevLsiLogicSCSI.cpp"
#endif
#undef LOG_GROUP
#include "../PC/DevHPET.cpp"
#undef LOG_GROUP
#include "../Audio/DevIchAc97.cpp"
#undef LOG_GROUP
#include "../Audio/DevHDA.cpp"
#ifdef VBOX_WITH_NVME_IMPL
# undef LOG_GROUP
# include "../Storage/DevNVMe.cpp"
#endif
#ifdef VBOX_WITH_IOMMU_AMD
# undef LOG_GROUP
# include "../Bus/DevIommuAmd.cpp"
#endif
#ifdef VBOX_WITH_IOMMU_INTEL
# undef LOG_GROUP
# include "../Bus/DevIommuIntel.cpp"
#endif

/* Check that important preprocessor macros didn't get redefined: */
#if defined(DEBUG)       != defined(VBOX_DEVICE_STRUCT_TESTCASE_CHECK_DEBUG)
# error "DEBUG was modified!  This may throw off structure tests."
#endif
#if defined(LOG_ENABLED) != defined(VBOX_DEVICE_STRUCT_TESTCASE_CHECK_LOG_ENABLED)
# error "LOG_ENABLED was modified!  This may throw off structure tests."
#endif
#if defined(RT_STRICT)   != defined(VBOX_DEVICE_STRUCT_TESTCASE_CHECK_RT_STRICT)
# error "RT_STRICT was modified!  This may throw off structure tests."
#endif
#if defined(VBOX_STRICT) != defined(VBOX_DEVICE_STRUCT_TESTCASE_CHECK_VBOX_STRICT)
# error "VBOX_STRICT was modified!  This may throw off structure tests."
#endif


/* we don't use iprt here because we're pretending to be in GC! */
#include <stdio.h>

#define GEN_CHECK_SIZE(s)           printf("    CHECK_SIZE(%s, %d);\n", #s, (int)sizeof(s))
#define GEN_CHECK_OFF(s, m)         printf("    CHECK_OFF(%s, %d, %s);\n", #s, (int)RT_OFFSETOF(s, m), #m)
#define GEN_CHECK_PADDING(s, m, a)  printf("    CHECK_PADDING(%s, %s, %u);\n", #s, #m, (a))

int main()
{
    /* misc */
    GEN_CHECK_SIZE(PDMDEVINS);
    GEN_CHECK_OFF(PDMDEVINS, Internal);
    GEN_CHECK_OFF(PDMDEVINS, pReg);
    GEN_CHECK_OFF(PDMDEVINS, pCfg);
    GEN_CHECK_OFF(PDMDEVINS, iInstance);
    GEN_CHECK_OFF(PDMDEVINS, IBase);
    GEN_CHECK_OFF(PDMDEVINS, pHlpR3);
    GEN_CHECK_OFF(PDMDEVINS, pHlpR0);
    GEN_CHECK_OFF(PDMDEVINS, pHlpRC);
    GEN_CHECK_OFF(PDMDEVINS, pvInstanceDataR3);
    GEN_CHECK_OFF(PDMDEVINS, pvInstanceDataR0);
    GEN_CHECK_OFF(PDMDEVINS, pvInstanceDataRC);
    GEN_CHECK_OFF(PDMDEVINS, achInstanceData);

    /* PDMPCIDEV */
    GEN_CHECK_SIZE(PDMPCIDEV);
    GEN_CHECK_SIZE(PDMPCIDEVINT);
    GEN_CHECK_SIZE(PCIIOREGION); /** @todo fix name of PCIIOREGION */
    GEN_CHECK_OFF(PDMPCIDEV, abConfig);
    GEN_CHECK_OFF(PDMPCIDEV, uDevFn);
    GEN_CHECK_OFF(PDMPCIDEV, pszNameR3);
    GEN_CHECK_OFF(PDMPCIDEV, pfnRegionLoadChangeHookR3);
    GEN_CHECK_OFF(PDMPCIDEV, Int);
    GEN_CHECK_OFF(PDMPCIDEV, Int.s.aIORegions);
    GEN_CHECK_OFF(PDMPCIDEV, Int.s.aIORegions[1]);
    GEN_CHECK_OFF(PDMPCIDEV, Int.s.aIORegions[VBOX_PCI_NUM_REGIONS - 1]);
    GEN_CHECK_OFF(PDMPCIDEV, Int.s.aIORegions[0].addr);
    GEN_CHECK_OFF(PDMPCIDEV, Int.s.aIORegions[0].size);
    GEN_CHECK_OFF(PDMPCIDEV, Int.s.aIORegions[0].type);
    GEN_CHECK_OFF(PDMPCIDEV, Int.s.aIORegions[0].padding);
    GEN_CHECK_OFF(PDMPCIDEV, Int.s.pBusR3);
    GEN_CHECK_OFF(PDMPCIDEV, Int.s.pBusR0);
    GEN_CHECK_OFF(PDMPCIDEV, Int.s.pBusRC);
    GEN_CHECK_OFF(PDMPCIDEV, Int.s.pfnConfigRead);
    GEN_CHECK_OFF(PDMPCIDEV, Int.s.pfnConfigWrite);
    GEN_CHECK_OFF(PDMPCIDEV, Int.s.fFlags);
    GEN_CHECK_OFF(PDMPCIDEV, Int.s.uIrqPinState);
    GEN_CHECK_OFF(PDMPCIDEV, Int.s.pfnBridgeConfigRead);
    GEN_CHECK_OFF(PDMPCIDEV, Int.s.pfnBridgeConfigWrite);
    GEN_CHECK_PADDING(PDMPCIDEV, Int, 8);

    /* DevPciInternal.h */
    GEN_CHECK_SIZE(DEVPCIBUS);
    GEN_CHECK_OFF(DEVPCIBUS, iBus);
    GEN_CHECK_OFF(DEVPCIBUS, iDevSearch);
    GEN_CHECK_OFF(DEVPCIBUS, cBridges);
    GEN_CHECK_OFF(DEVPCIBUS, apDevices);
    GEN_CHECK_OFF(DEVPCIBUS, apDevices[1]);
    GEN_CHECK_OFF(DEVPCIBUS, papBridgesR3);
    GEN_CHECK_OFF(DEVPCIBUS, PciDev);
    GEN_CHECK_SIZE(PIIX3ISABRIDGE);
    GEN_CHECK_SIZE(DEVPCIROOT);
    GEN_CHECK_OFF(DEVPCIROOT, PciBus);
    GEN_CHECK_OFF(DEVPCIROOT, fUseIoApic);
    GEN_CHECK_OFF(DEVPCIROOT, u64PciConfigMMioAddress);
    GEN_CHECK_OFF(DEVPCIROOT, u64PciConfigMMioLength);
    GEN_CHECK_OFF(DEVPCIROOT, auPciApicIrqLevels);
    GEN_CHECK_OFF(DEVPCIROOT, auPciApicIrqLevels[1]);
    GEN_CHECK_OFF(DEVPCIROOT, uConfigReg);
    GEN_CHECK_OFF(DEVPCIROOT, Piix3.iAcpiIrqLevel);
    GEN_CHECK_OFF(DEVPCIROOT, Piix3.iAcpiIrq);
    GEN_CHECK_OFF(DEVPCIROOT, Piix3.auPciLegacyIrqLevels);
    GEN_CHECK_OFF(DEVPCIROOT, Piix3.auPciLegacyIrqLevels[1]);
    GEN_CHECK_OFF(DEVPCIROOT, Piix3.PIIX3State);
    GEN_CHECK_OFF(DEVPCIROOT, uPciBiosBus);
    GEN_CHECK_OFF(DEVPCIROOT, uPciBiosIo);
    GEN_CHECK_OFF(DEVPCIROOT, uPciBiosMmio);
    GEN_CHECK_OFF(DEVPCIROOT, uPciBiosMmio64);

    /* EFI/DevSMC.cpp */
    GEN_CHECK_SIZE(DEVSMC);
    GEN_CHECK_OFF(DEVSMC, bCmd);
    GEN_CHECK_OFF(DEVSMC, offKey);
    GEN_CHECK_OFF(DEVSMC, offValue);
    GEN_CHECK_OFF(DEVSMC, cKeys);
    GEN_CHECK_OFF(DEVSMC, CurKey);
    GEN_CHECK_OFF(DEVSMC, u);
    GEN_CHECK_OFF(DEVSMC, u.s);
    GEN_CHECK_OFF(DEVSMC, u.s.bState);
    GEN_CHECK_OFF(DEVSMC, u.s.bStatusCode);
    GEN_CHECK_OFF(DEVSMC, u.s.bStatusCode);
    GEN_CHECK_OFF(DEVSMC, szOsk0And1);
    GEN_CHECK_OFF(DEVSMC, bDollaryNumber);
    GEN_CHECK_OFF(DEVSMC, bShutdownReason);
    GEN_CHECK_OFF(DEVSMC, bNinjaActionTimerJob);

    /* DevVGA.cpp */
    GEN_CHECK_SIZE(VGASTATE);
    GEN_CHECK_OFF(VGASTATE, vram_ptrR3);
    GEN_CHECK_OFF(VGASTATE, get_bpp);
    GEN_CHECK_OFF(VGASTATE, get_offsets);
    GEN_CHECK_OFF(VGASTATE, get_resolution);
    GEN_CHECK_OFF(VGASTATE, rgb_to_pixel);
    GEN_CHECK_OFF(VGASTATE, cursor_invalidate);
    GEN_CHECK_OFF(VGASTATE, cursor_draw_line);
    GEN_CHECK_OFF(VGASTATE, vram_size);
    GEN_CHECK_OFF(VGASTATE, latch);
    GEN_CHECK_OFF(VGASTATE, sr_index);
    GEN_CHECK_OFF(VGASTATE, sr);
    GEN_CHECK_OFF(VGASTATE, sr[1]);
    GEN_CHECK_OFF(VGASTATE, gr_index);
    GEN_CHECK_OFF(VGASTATE, gr);
    GEN_CHECK_OFF(VGASTATE, gr[1]);
    GEN_CHECK_OFF(VGASTATE, ar_index);
    GEN_CHECK_OFF(VGASTATE, ar);
    GEN_CHECK_OFF(VGASTATE, ar[1]);
    GEN_CHECK_OFF(VGASTATE, ar_flip_flop);
    GEN_CHECK_OFF(VGASTATE, cr_index);
    GEN_CHECK_OFF(VGASTATE, cr);
    GEN_CHECK_OFF(VGASTATE, cr[1]);
    GEN_CHECK_OFF(VGASTATE, msr);
    GEN_CHECK_OFF(VGASTATE, msr);
    GEN_CHECK_OFF(VGASTATE, fcr);
    GEN_CHECK_OFF(VGASTATE, st00);
    GEN_CHECK_OFF(VGASTATE, st01);
    GEN_CHECK_OFF(VGASTATE, dac_state);
    GEN_CHECK_OFF(VGASTATE, dac_sub_index);
    GEN_CHECK_OFF(VGASTATE, dac_read_index);
    GEN_CHECK_OFF(VGASTATE, dac_write_index);
    GEN_CHECK_OFF(VGASTATE, dac_cache);
    GEN_CHECK_OFF(VGASTATE, dac_cache[1]);
    GEN_CHECK_OFF(VGASTATE, palette);
    GEN_CHECK_OFF(VGASTATE, palette[1]);
    GEN_CHECK_OFF(VGASTATE, bank_offset);
#ifdef CONFIG_BOCHS_VBE
    GEN_CHECK_OFF(VGASTATE, vbe_index);
    GEN_CHECK_OFF(VGASTATE, vbe_regs);
    GEN_CHECK_OFF(VGASTATE, vbe_regs[1]);
    GEN_CHECK_OFF(VGASTATE, vbe_regs[VBE_DISPI_INDEX_NB - 1]);
    GEN_CHECK_OFF(VGASTATE, vbe_start_addr);
    GEN_CHECK_OFF(VGASTATE, vbe_line_offset);
    GEN_CHECK_OFF(VGASTATE, vbe_bank_max);
#endif
    GEN_CHECK_OFF(VGASTATE, font_offsets);
    GEN_CHECK_OFF(VGASTATE, font_offsets[1]);
    GEN_CHECK_OFF(VGASTATE, graphic_mode);
    GEN_CHECK_OFF(VGASTATE, shift_control);
    GEN_CHECK_OFF(VGASTATE, double_scan);
    GEN_CHECK_OFF(VGASTATE, line_offset);
    GEN_CHECK_OFF(VGASTATE, line_compare);
    GEN_CHECK_OFF(VGASTATE, start_addr);
    GEN_CHECK_OFF(VGASTATE, plane_updated);
    GEN_CHECK_OFF(VGASTATE, last_cw);
    GEN_CHECK_OFF(VGASTATE, last_ch);
    GEN_CHECK_OFF(VGASTATE, last_width);
    GEN_CHECK_OFF(VGASTATE, last_height);
    GEN_CHECK_OFF(VGASTATE, last_scr_width);
    GEN_CHECK_OFF(VGASTATE, last_scr_height);
    GEN_CHECK_OFF(VGASTATE, last_bpp);
    GEN_CHECK_OFF(VGASTATE, cursor_start);
    GEN_CHECK_OFF(VGASTATE, cursor_end);
    GEN_CHECK_OFF(VGASTATE, cursor_offset);
    GEN_CHECK_OFF(VGASTATE, invalidated_y_table);
    GEN_CHECK_OFF(VGASTATE, invalidated_y_table[1]);
    GEN_CHECK_OFF(VGASTATE, invalidated_y_table[(VGA_MAX_HEIGHT / 32) - 1]);
    GEN_CHECK_OFF(VGASTATE, last_palette);
    GEN_CHECK_OFF(VGASTATE, last_palette[1]);
    GEN_CHECK_OFF(VGASTATE, last_ch_attr);
    GEN_CHECK_OFF(VGASTATE, last_ch_attr[CH_ATTR_SIZE - 1]);
    GEN_CHECK_OFF(VGASTATE, u32Marker);
    GEN_CHECK_OFF(VGASTATE, pDevInsRC);
    GEN_CHECK_OFF(VGASTATE, vram_ptrRC);
    GEN_CHECK_OFF(VGASTATE, pDevInsR3);
#ifdef VBOX_WITH_HGSMI
    GEN_CHECK_OFF(VGASTATE, pHGSMI);
#endif
#ifdef VBOX_WITH_VDMA
    GEN_CHECK_OFF(VGASTATE, pVdma);
#endif
    GEN_CHECK_OFF(VGASTATE, IBase);
    GEN_CHECK_OFF(VGASTATE, IPort);
#if defined(VBOX_WITH_HGSMI) && defined(VBOX_WITH_VIDEOHWACCEL)
    GEN_CHECK_OFF(VGASTATE, IVBVACallbacks);
#endif
    GEN_CHECK_OFF(VGASTATE, pDrvBase);
    GEN_CHECK_OFF(VGASTATE, pDrv);
    GEN_CHECK_OFF(VGASTATE, RefreshTimer);
    GEN_CHECK_OFF(VGASTATE, pDevInsR0);
#ifdef VBOX_WITH_VMSVGA
    GEN_CHECK_OFF(VGASTATE, svga.pFIFOR3);
    GEN_CHECK_OFF(VGASTATE, svga.pFIFOR0);
    GEN_CHECK_OFF(VGASTATE, svga.pSvgaR3State);
    GEN_CHECK_OFF(VGASTATE, svga.p3dState);
    GEN_CHECK_OFF(VGASTATE, svga.pbVgaFrameBufferR3);
    GEN_CHECK_OFF(VGASTATE, svga.GCPhysFIFO);
    GEN_CHECK_OFF(VGASTATE, svga.cbFIFO);
    GEN_CHECK_OFF(VGASTATE, svga.cbFIFOConfig);
    GEN_CHECK_OFF(VGASTATE, svga.u32SVGAId);
    GEN_CHECK_OFF(VGASTATE, svga.fConfigured);
    GEN_CHECK_OFF(VGASTATE, svga.fBusy);
    GEN_CHECK_OFF(VGASTATE, svga.fTraces);
    GEN_CHECK_OFF(VGASTATE, svga.u32GuestId);
    GEN_CHECK_OFF(VGASTATE, svga.cScratchRegion);
    GEN_CHECK_OFF(VGASTATE, svga.u32IrqStatus);
    GEN_CHECK_OFF(VGASTATE, svga.u32IrqMask);
    GEN_CHECK_OFF(VGASTATE, svga.u32PitchLock);
    GEN_CHECK_OFF(VGASTATE, svga.u32CurrentGMRId);
    GEN_CHECK_OFF(VGASTATE, svga.u32RegCaps);
    GEN_CHECK_OFF(VGASTATE, svga.u32IndexReg);
    GEN_CHECK_OFF(VGASTATE, svga.hFIFORequestSem);
    GEN_CHECK_OFF(VGASTATE, svga.hFIFOExtCmdSem);
    GEN_CHECK_OFF(VGASTATE, svga.pFIFOIOThread);
    GEN_CHECK_OFF(VGASTATE, svga.uWidth);
    GEN_CHECK_OFF(VGASTATE, svga.uHeight);
    GEN_CHECK_OFF(VGASTATE, svga.uBpp);
    GEN_CHECK_OFF(VGASTATE, svga.cbScanline);
    GEN_CHECK_OFF(VGASTATE, svga.u32MaxWidth);
    GEN_CHECK_OFF(VGASTATE, svga.u32MaxHeight);
    GEN_CHECK_OFF(VGASTATE, svga.viewport);
    GEN_CHECK_OFF(VGASTATE, svga.u32ActionFlags);
    GEN_CHECK_OFF(VGASTATE, svga.f3DEnabled);
    GEN_CHECK_OFF(VGASTATE, svga.fVRAMTracking);
    GEN_CHECK_OFF(VGASTATE, svga.u8FIFOExtCommand);
    GEN_CHECK_OFF(VGASTATE, svga.fFifoExtCommandWakeup);
    GEN_CHECK_OFF(VGASTATE, svga.au32ScratchRegion);
    GEN_CHECK_OFF(VGASTATE, svga.StatRegBitsPerPixelWr);
    GEN_CHECK_OFF(VGASTATE, svga.StatRegWriteOnlyRd);
#endif
    GEN_CHECK_OFF(VGASTATE, cMonitors);
    GEN_CHECK_OFF(VGASTATE, cMilliesRefreshInterval);
    GEN_CHECK_OFF(VGASTATE, au32DirtyBitmap);
    GEN_CHECK_OFF(VGASTATE, au32DirtyBitmap[1]);
    GEN_CHECK_OFF(VGASTATE, au32DirtyBitmap[(VGA_VRAM_MAX / GUEST_PAGE_SIZE / 32) - 1]);
    GEN_CHECK_OFF(VGASTATE, fHasDirtyBits);
    GEN_CHECK_OFF(VGASTATE, fLFBUpdated);
    GEN_CHECK_OFF(VGASTATE, fGCEnabled);
    GEN_CHECK_OFF(VGASTATE, fR0Enabled);
    GEN_CHECK_OFF(VGASTATE, fRemappedVGA);
    GEN_CHECK_OFF(VGASTATE, fRenderVRAM);
    GEN_CHECK_OFF(VGASTATE, GCPhysVRAM);
    GEN_CHECK_OFF(VGASTATE, CritSect);
    GEN_CHECK_OFF(VGASTATE, Dev);
    GEN_CHECK_OFF(VGASTATE, StatRZMemoryRead);
    GEN_CHECK_OFF(VGASTATE, StatR3MemoryRead);
    GEN_CHECK_OFF(VGASTATE, StatRZMemoryWrite);
    GEN_CHECK_OFF(VGASTATE, StatR3MemoryWrite);
#ifdef VBE_BYTEWISE_IO
    GEN_CHECK_OFF(VGASTATE, fReadVBEData);
    GEN_CHECK_OFF(VGASTATE, fWriteVBEData);
    GEN_CHECK_OFF(VGASTATE, fReadVBEIndex);
    GEN_CHECK_OFF(VGASTATE, fWriteVBEIndex);
    GEN_CHECK_OFF(VGASTATE, cbWriteVBEData);
    GEN_CHECK_OFF(VGASTATE, cbWriteVBEIndex);
# ifdef VBE_NEW_DYN_LIST
    GEN_CHECK_OFF(VGASTATE, cbWriteVBEExtraAddress);
# endif
#endif
#ifdef VBE_NEW_DYN_LIST
    GEN_CHECK_OFF(VGASTATE, pbVBEExtraData);
    GEN_CHECK_OFF(VGASTATE, cbVBEExtraData);
    GEN_CHECK_OFF(VGASTATE, u16VBEExtraAddress);
#endif
    GEN_CHECK_OFF(VGASTATE, pbLogo);
    GEN_CHECK_OFF(VGASTATE, pszLogoFile);
    GEN_CHECK_OFF(VGASTATE, pbLogoBitmap);
    GEN_CHECK_OFF(VGASTATE, offLogoData);
    GEN_CHECK_OFF(VGASTATE, cbLogo);
    GEN_CHECK_OFF(VGASTATE, LogoCommand);
    GEN_CHECK_OFF(VGASTATE, cxLogo);
    GEN_CHECK_OFF(VGASTATE, cyLogo);
    GEN_CHECK_OFF(VGASTATE, cLogoPlanes);
    GEN_CHECK_OFF(VGASTATE, cLogoBits);
    GEN_CHECK_OFF(VGASTATE, LogoCompression);
    GEN_CHECK_OFF(VGASTATE, cLogoUsedColors);
    GEN_CHECK_OFF(VGASTATE, cLogoPalEntries);
    GEN_CHECK_OFF(VGASTATE, fLogoClearScreen);
    GEN_CHECK_OFF(VGASTATE, au32LogoPalette);
    GEN_CHECK_OFF(VGASTATE, pbVgaBios);
    GEN_CHECK_OFF(VGASTATE, cbVgaBios);
    GEN_CHECK_OFF(VGASTATE, pszVgaBiosFile);
#ifdef VBOX_WITH_HGSMI
    GEN_CHECK_OFF(VGASTATE, IOPortBase);
#endif
#ifdef VBOX_WITH_WDDM
    GEN_CHECK_OFF(VGASTATE, fGuestCaps);
#endif

    /* Input/pckbd.c */
    GEN_CHECK_SIZE(KBDSTATE);
    GEN_CHECK_OFF(KBDSTATE, write_cmd);
    GEN_CHECK_OFF(KBDSTATE, status);
    GEN_CHECK_OFF(KBDSTATE, mode);
    GEN_CHECK_OFF(KBDSTATE, pDevInsR3);
    GEN_CHECK_OFF(KBDSTATE, pDevInsR0);
    GEN_CHECK_OFF(KBDSTATE, pDevInsRC);
    GEN_CHECK_SIZE(KbdKeyQ);
    GEN_CHECK_OFF(KbdCmdQ, rpos);
    GEN_CHECK_OFF(KbdCmdQ, wpos);
    GEN_CHECK_OFF(KbdCmdQ, cUsed);
    GEN_CHECK_OFF(KbdCmdQ, cSize);
    GEN_CHECK_OFF(KbdCmdQ, abQueue);
    GEN_CHECK_SIZE(KbdCmdQ);
    /* Input/PS2K.c */
    GEN_CHECK_SIZE(PS2K);
    GEN_CHECK_OFF(PS2K, fScanning);
    GEN_CHECK_OFF(PS2K, fNumLockOn);
    GEN_CHECK_OFF(PS2K, u8ScanSet);
    GEN_CHECK_OFF(PS2K, u8TypematicCfg);
    GEN_CHECK_OFF(PS2K, enmTypematicState);
    GEN_CHECK_OFF(PS2K, keyQ);
    GEN_CHECK_OFF(PS2K, cmdQ);
    GEN_CHECK_OFF(PS2K, uTypematicDelay);
    GEN_CHECK_OFF(PS2K, fThrottleActive);
    GEN_CHECK_OFF(PS2K, pKbdDelayTimerRC);
    GEN_CHECK_OFF(PS2K, pKbdDelayTimerR3);
    GEN_CHECK_OFF(PS2K, pKbdDelayTimerR0);
    GEN_CHECK_OFF(PS2K, pKbdTypematicTimerRC);
    GEN_CHECK_OFF(PS2K, pKbdTypematicTimerR3);
    GEN_CHECK_OFF(PS2K, pKbdTypematicTimerR0);
    GEN_CHECK_OFF(PS2K, pThrottleTimerRC);
    GEN_CHECK_OFF(PS2K, pThrottleTimerR3);
    GEN_CHECK_OFF(PS2K, pThrottleTimerR0);
    GEN_CHECK_OFF(PS2K, pCritSectR3);
    GEN_CHECK_OFF(PS2K, Keyboard.IBase);
    GEN_CHECK_OFF(PS2K, Keyboard.IPort);
    GEN_CHECK_OFF(PS2K, Keyboard.pDrvBase);
    GEN_CHECK_OFF(PS2K, Keyboard.pDrv);
    /* Input/PS2M.c */
    GEN_CHECK_SIZE(PS2M);
    GEN_CHECK_OFF(PS2M, u8State);
    GEN_CHECK_OFF(PS2M, u8SampleRate);
    GEN_CHECK_OFF(PS2M, u8Resolution);
    GEN_CHECK_OFF(PS2M, u8CurrCmd);
    GEN_CHECK_OFF(PS2M, fThrottleActive);
    GEN_CHECK_OFF(PS2M, fDelayReset);
    GEN_CHECK_OFF(PS2M, enmMode);
    GEN_CHECK_OFF(PS2M, enmProtocol);
    GEN_CHECK_OFF(PS2M, enmKnockState);
    GEN_CHECK_OFF(PS2M, evtQ);
    GEN_CHECK_OFF(PS2M, cmdQ);
    GEN_CHECK_OFF(PS2M, iAccumX);
    GEN_CHECK_OFF(PS2M, fAccumB);
    GEN_CHECK_OFF(PS2M, fCurrB);
    GEN_CHECK_OFF(PS2M, uThrottleDelay);
    GEN_CHECK_OFF(PS2M, pCritSectR3);
    GEN_CHECK_OFF(PS2M, pDelayTimerR3);
    GEN_CHECK_OFF(PS2M, pThrottleTimerR3);
    GEN_CHECK_OFF(PS2M, pDelayTimerRC);
    GEN_CHECK_OFF(PS2M, pThrottleTimerRC);
    GEN_CHECK_OFF(PS2M, pDelayTimerR0);
    GEN_CHECK_OFF(PS2M, pThrottleTimerR0);
    GEN_CHECK_OFF(PS2M, Mouse.IBase);
    GEN_CHECK_OFF(PS2M, Mouse.IPort);
    GEN_CHECK_OFF(PS2M, Mouse.pDrvBase);
    GEN_CHECK_OFF(PS2M, Mouse.pDrv);

    /* Network/DevPCNet.cpp */
    GEN_CHECK_SIZE(PCNETSTATE);
    GEN_CHECK_OFF(PCNETSTATE, PciDev);
#ifndef PCNET_NO_POLLING
    GEN_CHECK_OFF(PCNETSTATE, pTimerPollR3);
    GEN_CHECK_OFF(PCNETSTATE, pTimerPollR0);
    GEN_CHECK_OFF(PCNETSTATE, pTimerPollRC);
#endif
    GEN_CHECK_OFF(PCNETSTATE, pTimerSoftIntR3);
    GEN_CHECK_OFF(PCNETSTATE, pTimerSoftIntR0);
    GEN_CHECK_OFF(PCNETSTATE, pTimerSoftIntRC);
    GEN_CHECK_OFF(PCNETSTATE, u32RAP);
    GEN_CHECK_OFF(PCNETSTATE, iISR);
    GEN_CHECK_OFF(PCNETSTATE, u32Lnkst);
    GEN_CHECK_OFF(PCNETSTATE, GCRDRA);
    GEN_CHECK_OFF(PCNETSTATE, GCTDRA);
    GEN_CHECK_OFF(PCNETSTATE, aPROM);
    GEN_CHECK_OFF(PCNETSTATE, aPROM[1]);
    GEN_CHECK_OFF(PCNETSTATE, aCSR);
    GEN_CHECK_OFF(PCNETSTATE, aCSR[1]);
    GEN_CHECK_OFF(PCNETSTATE, aCSR[CSR_MAX_REG - 1]);
    GEN_CHECK_OFF(PCNETSTATE, aBCR);
    GEN_CHECK_OFF(PCNETSTATE, aBCR[1]);
    GEN_CHECK_OFF(PCNETSTATE, aBCR[BCR_MAX_RAP - 1]);
    GEN_CHECK_OFF(PCNETSTATE, aMII);
    GEN_CHECK_OFF(PCNETSTATE, aMII[1]);
    GEN_CHECK_OFF(PCNETSTATE, aMII[MII_MAX_REG - 1]);
    GEN_CHECK_OFF(PCNETSTATE, u16CSR0LastSeenByGuest);
    GEN_CHECK_OFF(PCNETSTATE, u64LastPoll);
    GEN_CHECK_OFF(PCNETSTATE, abLoopBuf);
    GEN_CHECK_OFF(PCNETSTATE, abRecvBuf);
    GEN_CHECK_OFF(PCNETSTATE, iLog2DescSize);
    GEN_CHECK_OFF(PCNETSTATE, GCUpperPhys);
    GEN_CHECK_OFF(PCNETSTATE, pXmitQueueR3);
    GEN_CHECK_OFF(PCNETSTATE, pXmitQueueR0);
    GEN_CHECK_OFF(PCNETSTATE, pXmitQueueRC);
    GEN_CHECK_OFF(PCNETSTATE, pCanRxQueueR3);
    GEN_CHECK_OFF(PCNETSTATE, pCanRxQueueR0);
    GEN_CHECK_OFF(PCNETSTATE, pCanRxQueueRC);
    GEN_CHECK_OFF(PCNETSTATE, pTimerRestore);
    GEN_CHECK_OFF(PCNETSTATE, pDevInsR3);
    GEN_CHECK_OFF(PCNETSTATE, pDevInsR0);
    GEN_CHECK_OFF(PCNETSTATE, pDevInsRC);
    GEN_CHECK_OFF(PCNETSTATE, pDrvR3);
    GEN_CHECK_OFF(PCNETSTATE, pDrvBase);
    GEN_CHECK_OFF(PCNETSTATE, IBase);
    GEN_CHECK_OFF(PCNETSTATE, INetworkDown);
    GEN_CHECK_OFF(PCNETSTATE, INetworkConfig);
    GEN_CHECK_OFF(PCNETSTATE, MMIOBase);
    GEN_CHECK_OFF(PCNETSTATE, IOPortBase);
    GEN_CHECK_OFF(PCNETSTATE, fLinkUp);
    GEN_CHECK_OFF(PCNETSTATE, fLinkTempDown);
    GEN_CHECK_OFF(PCNETSTATE, cLinkDownReported);
    GEN_CHECK_OFF(PCNETSTATE, MacConfigured);
    GEN_CHECK_OFF(PCNETSTATE, Led);
    GEN_CHECK_OFF(PCNETSTATE, ILeds);
    GEN_CHECK_OFF(PCNETSTATE, pLedsConnector);
    GEN_CHECK_OFF(PCNETSTATE, CritSect);
#ifdef PCNET_NO_POLLING
    GEN_CHECK_OFF(PCNETSTATE, TDRAPhysOld);
    GEN_CHECK_OFF(PCNETSTATE, cbTDRAOld);
    GEN_CHECK_OFF(PCNETSTATE, RDRAPhysOld);
    GEN_CHECK_OFF(PCNETSTATE, cbRDRAOld);
    GEN_CHECK_OFF(PCNETSTATE, pfnEMInterpretInstructionGC
    GEN_CHECK_OFF(PCNETSTATE, pfnEMInterpretInstructionR0
#endif
    GEN_CHECK_OFF(PCNETSTATE, fGCEnabled);
    GEN_CHECK_OFF(PCNETSTATE, fR0Enabled);
    GEN_CHECK_OFF(PCNETSTATE, uDevType);
    GEN_CHECK_OFF(PCNETSTATE, StatReceiveBytes);
    GEN_CHECK_OFF(PCNETSTATE, StatTransmitBytes);
#ifdef VBOX_WITH_STATISTICS
    GEN_CHECK_OFF(PCNETSTATE, StatMMIOReadR3);
    GEN_CHECK_OFF(PCNETSTATE, StatMMIOReadRZ);
    GEN_CHECK_OFF(PCNETSTATE, StatMIIReads);
# ifdef PCNET_NO_POLLING
    GEN_CHECK_OFF(PCNETSTATE, StatRCVRingWrite);
    GEN_CHECK_OFF(PCNETSTATE, StatRingWriteOutsideRangeR3);
# endif
#endif

    /* PC/DevACPI.cpp */
    GEN_CHECK_SIZE(ACPIState);
    GEN_CHECK_OFF(ACPIState, dev);
    GEN_CHECK_OFF(ACPIState, pm1a_en);
    GEN_CHECK_OFF(ACPIState, pm1a_sts);
    GEN_CHECK_OFF(ACPIState, pm1a_ctl);
    GEN_CHECK_OFF(ACPIState, u64PmTimerInitial);
    GEN_CHECK_OFF(ACPIState, pPmTimerR3);
    GEN_CHECK_OFF(ACPIState, pPmTimerR0);
    GEN_CHECK_OFF(ACPIState, pPmTimerRC);
    GEN_CHECK_OFF(ACPIState, uPmTimerVal);
    GEN_CHECK_OFF(ACPIState, gpe0_en);
    GEN_CHECK_OFF(ACPIState, gpe0_sts);
    GEN_CHECK_OFF(ACPIState, uBatteryIndex);
    GEN_CHECK_OFF(ACPIState, au8BatteryInfo);
    GEN_CHECK_OFF(ACPIState, uSystemInfoIndex);
    GEN_CHECK_OFF(ACPIState, u64RamSize);
    GEN_CHECK_OFF(ACPIState, uSleepState);
    GEN_CHECK_OFF(ACPIState, au8RSDPPage);
    GEN_CHECK_OFF(ACPIState, u8IndexShift);
    GEN_CHECK_OFF(ACPIState, u8UseIOApic);
    GEN_CHECK_OFF(ACPIState, fUseFdc);
    GEN_CHECK_OFF(ACPIState, fUseHpet);
    GEN_CHECK_OFF(ACPIState, fUseSmc);
    GEN_CHECK_OFF(ACPIState, CpuSetAttached);
    GEN_CHECK_OFF(ACPIState, idCpuLockCheck);
    GEN_CHECK_OFF(ACPIState, CpuSetLocked);
    GEN_CHECK_OFF(ACPIState, u32CpuEventType);
    GEN_CHECK_OFF(ACPIState, u32CpuEvent);
    GEN_CHECK_OFF(ACPIState, fCpuHotPlug);
    GEN_CHECK_OFF(ACPIState, u32NicPciAddress);
    GEN_CHECK_OFF(ACPIState, u32HbcPciAddress);
    GEN_CHECK_OFF(ACPIState, u32IommuPciAddress);
    GEN_CHECK_OFF(ACPIState, u32SbIoApicPciAddress);
    GEN_CHECK_OFF(ACPIState, u64PciConfigMMioAddress);
    GEN_CHECK_OFF(ACPIState, IBase);
    GEN_CHECK_OFF(ACPIState, IACPIPort);
    GEN_CHECK_OFF(ACPIState, pDevInsR3);
    GEN_CHECK_OFF(ACPIState, pDevInsR0);
    GEN_CHECK_OFF(ACPIState, pDrvBase);
    GEN_CHECK_OFF(ACPIState, pDrv);
    GEN_CHECK_OFF(ACPIState, u16SMBusSlvDat);

    /* PC/DevPIC.cpp */
    GEN_CHECK_SIZE(PICSTATE);
    GEN_CHECK_OFF(PICSTATE, last_irr);
    GEN_CHECK_OFF(PICSTATE, irr);
    GEN_CHECK_OFF(PICSTATE, imr);
    GEN_CHECK_OFF(PICSTATE, isr);
    GEN_CHECK_OFF(PICSTATE, priority_add);
    GEN_CHECK_OFF(PICSTATE, irq_base);
    GEN_CHECK_OFF(PICSTATE, read_reg_select);
    GEN_CHECK_OFF(PICSTATE, poll);
    GEN_CHECK_OFF(PICSTATE, special_mask);
    GEN_CHECK_OFF(PICSTATE, init_state);
    GEN_CHECK_OFF(PICSTATE, auto_eoi);
    GEN_CHECK_OFF(PICSTATE, rotate_on_auto_eoi);
    GEN_CHECK_OFF(PICSTATE, special_fully_nested_mode);
    GEN_CHECK_OFF(PICSTATE, init4);
    GEN_CHECK_OFF(PICSTATE, elcr);
    GEN_CHECK_OFF(PICSTATE, elcr_mask);
    GEN_CHECK_OFF(PICSTATE, pDevInsR3);
    GEN_CHECK_OFF(PICSTATE, pDevInsR0);
    GEN_CHECK_OFF(PICSTATE, pDevInsRC);
    GEN_CHECK_OFF(PICSTATE, idxPic);
    GEN_CHECK_OFF(PICSTATE, auTags);

    GEN_CHECK_SIZE(DEVPIC);
    GEN_CHECK_OFF(DEVPIC, aPics);
    GEN_CHECK_OFF(DEVPIC, aPics[1]);
    GEN_CHECK_OFF(DEVPIC, pDevInsR3);
    GEN_CHECK_OFF(DEVPIC, pDevInsR0);
    GEN_CHECK_OFF(DEVPIC, pDevInsRC);
    GEN_CHECK_OFF(DEVPIC, pPicHlpR3);
    GEN_CHECK_OFF(DEVPIC, pPicHlpR0);
    GEN_CHECK_OFF(DEVPIC, pPicHlpRC);
#ifdef VBOX_WITH_STATISTICS
    GEN_CHECK_OFF(DEVPIC, StatSetIrqGC);
    GEN_CHECK_OFF(DEVPIC, StatClearedActiveSlaveIRQ);
#endif

    /* PC/DevPit-i8254.cpp */
    GEN_CHECK_SIZE(DMAState);
    GEN_CHECK_OFF(DMAState, DMAC);
    GEN_CHECK_OFF(DMAState, DMAC[0].ChState[0]);
    GEN_CHECK_OFF(DMAState, DMAC[0].ChState[1]);
    GEN_CHECK_OFF(DMAState, DMAC[0].u8Mask);
    GEN_CHECK_OFF(DMAState, DMAC[1].ChState[0]);
    GEN_CHECK_OFF(DMAState, DMAC[1].ChState[1]);
    GEN_CHECK_OFF(DMAState, DMAC[1].u8Mask);

    /* PC/DevPit-i8254.cpp */
    GEN_CHECK_SIZE(PITCHANNEL);
    GEN_CHECK_OFF(PITCHANNEL, pPitR3);
    GEN_CHECK_OFF(PITCHANNEL, pTimerR3);
    GEN_CHECK_OFF(PITCHANNEL, pPitR0);
    GEN_CHECK_OFF(PITCHANNEL, pTimerR0);
    GEN_CHECK_OFF(PITCHANNEL, pPitRC);
    GEN_CHECK_OFF(PITCHANNEL, pTimerRC);
    GEN_CHECK_OFF(PITCHANNEL, u64ReloadTS);
    GEN_CHECK_OFF(PITCHANNEL, u64NextTS);
    GEN_CHECK_OFF(PITCHANNEL, count_load_time);
    GEN_CHECK_OFF(PITCHANNEL, next_transition_time);
    GEN_CHECK_OFF(PITCHANNEL, irq);
    GEN_CHECK_OFF(PITCHANNEL, cRelLogEntries);
    GEN_CHECK_OFF(PITCHANNEL, count);
    GEN_CHECK_OFF(PITCHANNEL, latched_count);
    GEN_CHECK_OFF(PITCHANNEL, count_latched);
    GEN_CHECK_OFF(PITCHANNEL, status_latched);
    GEN_CHECK_OFF(PITCHANNEL, status);
    GEN_CHECK_OFF(PITCHANNEL, read_state);
    GEN_CHECK_OFF(PITCHANNEL, write_state);
    GEN_CHECK_OFF(PITCHANNEL, write_latch);
    GEN_CHECK_OFF(PITCHANNEL, rw_mode);
    GEN_CHECK_OFF(PITCHANNEL, mode);
    GEN_CHECK_OFF(PITCHANNEL, bcd);
    GEN_CHECK_OFF(PITCHANNEL, gate);
    GEN_CHECK_SIZE(PITSTATE);
    GEN_CHECK_OFF(PITSTATE, channels);
    GEN_CHECK_OFF(PITSTATE, channels[1]);
    GEN_CHECK_OFF(PITSTATE, speaker_data_on);
//    GEN_CHECK_OFF(PITSTATE, dummy_refresh_clock);
    GEN_CHECK_OFF(PITSTATE, IOPortBaseCfg);
    GEN_CHECK_OFF(PITSTATE, fSpeakerCfg);
    GEN_CHECK_OFF(PITSTATE, pDevIns);
    GEN_CHECK_OFF(PITSTATE, StatPITIrq);
    GEN_CHECK_OFF(PITSTATE, StatPITHandler);

    /* PC/DevRTC.cpp */
    GEN_CHECK_SIZE(RTCSTATE);
    GEN_CHECK_OFF(RTCSTATE, cmos_data);
    GEN_CHECK_OFF(RTCSTATE, cmos_data[1]);
    GEN_CHECK_OFF(RTCSTATE, cmos_index);
    GEN_CHECK_OFF(RTCSTATE, current_tm);
    GEN_CHECK_OFF(RTCSTATE, current_tm.tm_sec);
    GEN_CHECK_OFF(RTCSTATE, current_tm.tm_min);
    GEN_CHECK_OFF(RTCSTATE, current_tm.tm_hour);
    GEN_CHECK_OFF(RTCSTATE, current_tm.tm_mday);
    GEN_CHECK_OFF(RTCSTATE, current_tm.tm_mon);
    GEN_CHECK_OFF(RTCSTATE, current_tm.tm_year);
    GEN_CHECK_OFF(RTCSTATE, current_tm.tm_wday);
    GEN_CHECK_OFF(RTCSTATE, current_tm.tm_yday);
    GEN_CHECK_OFF(RTCSTATE, irq);
    GEN_CHECK_OFF(RTCSTATE, fUTC);
    GEN_CHECK_OFF(RTCSTATE, IOPortBase);
    GEN_CHECK_OFF(RTCSTATE, pPeriodicTimerR0);
    GEN_CHECK_OFF(RTCSTATE, pPeriodicTimerR3);
    GEN_CHECK_OFF(RTCSTATE, pPeriodicTimerRC);
    GEN_CHECK_OFF(RTCSTATE, next_periodic_time);
    GEN_CHECK_OFF(RTCSTATE, next_second_time);
    GEN_CHECK_OFF(RTCSTATE, pSecondTimerR0);
    GEN_CHECK_OFF(RTCSTATE, pSecondTimerR3);
    GEN_CHECK_OFF(RTCSTATE, pSecondTimerRC);
    GEN_CHECK_OFF(RTCSTATE, pSecondTimer2R0);
    GEN_CHECK_OFF(RTCSTATE, pSecondTimer2R3);
    GEN_CHECK_OFF(RTCSTATE, pSecondTimer2RC);
    GEN_CHECK_OFF(RTCSTATE, pDevInsR0);
    GEN_CHECK_OFF(RTCSTATE, pDevInsR3);
    GEN_CHECK_OFF(RTCSTATE, pDevInsRC);
    GEN_CHECK_OFF(RTCSTATE, RtcReg);
    GEN_CHECK_OFF(RTCSTATE, pRtcHlpR3);
    GEN_CHECK_OFF(RTCSTATE, cRelLogEntries);
    GEN_CHECK_OFF(RTCSTATE, CurLogPeriod);
    GEN_CHECK_OFF(RTCSTATE, CurHintPeriod);

    GEN_CHECK_SIZE(APIC);
    GEN_CHECK_OFF(APIC, pApicDevR0);
    GEN_CHECK_OFF(APIC, pApicDevR3);
    GEN_CHECK_OFF(APIC, pApicDevRC);
    GEN_CHECK_OFF(APIC, HCPhysApicPib);
    GEN_CHECK_OFF(APIC, pvApicPibR0);
    GEN_CHECK_OFF(APIC, pvApicPibR3);
    GEN_CHECK_OFF(APIC, pvApicPibRC);
    GEN_CHECK_OFF(APIC, cbApicPib);
    GEN_CHECK_OFF(APIC, fVirtApicRegsEnabled);
    GEN_CHECK_OFF(APIC, fPostedIntrsEnabled);
    GEN_CHECK_OFF(APIC, fSupportsTscDeadline);
    GEN_CHECK_OFF(APIC, fIoApicPresent);
    GEN_CHECK_OFF(APIC, fRZEnabled);
    GEN_CHECK_OFF(APIC, enmMaxMode);

    GEN_CHECK_SIZE(APICCPU);
    GEN_CHECK_OFF(APICCPU, pvApicPageR0);
    GEN_CHECK_OFF(APICCPU, pvApicPageR3);
    GEN_CHECK_OFF(APICCPU, pvApicPageRC);
    GEN_CHECK_OFF(APICCPU, cbApicPage);
    GEN_CHECK_OFF(APICCPU, uEsrInternal);
    GEN_CHECK_OFF(APICCPU, uApicBaseMsr);
    GEN_CHECK_OFF(APICCPU, HCPhysApicPib);
    GEN_CHECK_OFF(APICCPU, pvApicPibR0);
    GEN_CHECK_OFF(APICCPU, pvApicPibR3);
    GEN_CHECK_OFF(APICCPU, pvApicPibRC);
    GEN_CHECK_OFF(APICCPU, ApicPibLevel);
    GEN_CHECK_OFF(APICCPU, pTimerR0);
    GEN_CHECK_OFF(APICCPU, pTimerR3);
    GEN_CHECK_OFF(APICCPU, pTimerRC);
    GEN_CHECK_OFF(APICCPU, TimerCritSect);
    GEN_CHECK_OFF(APICCPU, u64TimerInitial);
    GEN_CHECK_OFF(APICCPU, uHintedTimerInitialCount);
    GEN_CHECK_OFF(APICCPU, uHintedTimerShift);
# ifdef VBOX_WITH_STATISTICS
    GEN_CHECK_OFF(APICCPU, StatMmioReadRZ);
    GEN_CHECK_OFF(APICCPU, StatMmioReadR3);
    GEN_CHECK_OFF(APICCPU, StatMmioWriteRZ);
    GEN_CHECK_OFF(APICCPU, StatMmioWriteR3);
    GEN_CHECK_OFF(APICCPU, StatMsrReadRZ);
    GEN_CHECK_OFF(APICCPU, StatMsrReadR3);
    GEN_CHECK_OFF(APICCPU, StatMsrWriteRZ);
    GEN_CHECK_OFF(APICCPU, StatMsrWriteR3);
    GEN_CHECK_OFF(APICCPU, StatUpdatePendingIntrs);
    GEN_CHECK_OFF(APICCPU, StatPostIntr);
    GEN_CHECK_OFF(APICCPU, StatPostIntrAlreadyPending);
    GEN_CHECK_OFF(APICCPU, StatTimerCallback);
    GEN_CHECK_OFF(APICCPU, StatTprWrite);
    GEN_CHECK_OFF(APICCPU, StatTprRead);
    GEN_CHECK_OFF(APICCPU, StatEoiWrite);
    GEN_CHECK_OFF(APICCPU, StatMaskedByTpr);
    GEN_CHECK_OFF(APICCPU, StatMaskedByPpr);
    GEN_CHECK_OFF(APICCPU, StatTimerIcrWrite);
    GEN_CHECK_OFF(APICCPU, StatIcrLoWrite);
    GEN_CHECK_OFF(APICCPU, StatIcrHiWrite);
    GEN_CHECK_OFF(APICCPU, StatIcrFullWrite);
# endif /* VBOX_WITH_STATISTICS */

    /* PC/DevIoApic.cpp */
    GEN_CHECK_SIZE(IOAPIC);
    GEN_CHECK_OFF(IOAPIC, pDevInsR3);
    GEN_CHECK_OFF(IOAPIC, pIoApicHlpR3);
    GEN_CHECK_OFF(IOAPIC, pDevInsR0);
    GEN_CHECK_OFF(IOAPIC, pIoApicHlpR0);
    GEN_CHECK_OFF(IOAPIC, pDevInsRC);
    GEN_CHECK_OFF(IOAPIC, pIoApicHlpRC);
    GEN_CHECK_OFF(IOAPIC, u8Id);
    GEN_CHECK_OFF(IOAPIC, u8Index);
    GEN_CHECK_OFF(IOAPIC, cCpus);
    GEN_CHECK_OFF(IOAPIC, au64RedirTable);
    GEN_CHECK_OFF(IOAPIC, uIrr);
# ifdef VBOX_WITH_STATISTICS
    GEN_CHECK_OFF(IOAPIC, StatMmioReadRZ);
    GEN_CHECK_OFF(IOAPIC, StatMmioReadR3);
    GEN_CHECK_OFF(IOAPIC, StatMmioWriteRZ);
    GEN_CHECK_OFF(IOAPIC, StatMmioWriteR3);
    GEN_CHECK_OFF(IOAPIC, StatSetIrqRZ);
    GEN_CHECK_OFF(IOAPIC, StatSetIrqR3);
    GEN_CHECK_OFF(IOAPIC, StatSetEoiRZ);
    GEN_CHECK_OFF(IOAPIC, StatSetEoiR3);
# endif

    /* Storage/DevATA.cpp */
    GEN_CHECK_SIZE(BMDMAState);
    GEN_CHECK_OFF(BMDMAState, u8Cmd);
    GEN_CHECK_OFF(BMDMAState, u8Status);
    GEN_CHECK_OFF(BMDMAState, GCPhysAddr);
    GEN_CHECK_SIZE(BMDMADesc);
    GEN_CHECK_OFF(BMDMADesc, GCPhysBuffer);
    GEN_CHECK_OFF(BMDMADesc, cbBuffer);
    GEN_CHECK_SIZE(ATADevState);
    GEN_CHECK_OFF(ATADevState, fLBA48);
    GEN_CHECK_OFF(ATADevState, fATAPI);
    GEN_CHECK_OFF(ATADevState, fIrqPending);
    GEN_CHECK_OFF(ATADevState, cMultSectors);
    GEN_CHECK_OFF(ATADevState, cbSector);
    GEN_CHECK_OFF(ATADevState, PCHSGeometry.cCylinders);
    GEN_CHECK_OFF(ATADevState, PCHSGeometry.cHeads);
    GEN_CHECK_OFF(ATADevState, PCHSGeometry.cSectors);
    GEN_CHECK_OFF(ATADevState, cSectorsPerIRQ);
    GEN_CHECK_OFF(ATADevState, cTotalSectors);
    GEN_CHECK_OFF(ATADevState, uATARegFeature);
    GEN_CHECK_OFF(ATADevState, uATARegFeatureHOB);
    GEN_CHECK_OFF(ATADevState, uATARegError);
    GEN_CHECK_OFF(ATADevState, uATARegNSector);
    GEN_CHECK_OFF(ATADevState, uATARegNSectorHOB);
    GEN_CHECK_OFF(ATADevState, uATARegSector);
    GEN_CHECK_OFF(ATADevState, uATARegSectorHOB);
    GEN_CHECK_OFF(ATADevState, uATARegLCyl);
    GEN_CHECK_OFF(ATADevState, uATARegLCylHOB);
    GEN_CHECK_OFF(ATADevState, uATARegHCyl);
    GEN_CHECK_OFF(ATADevState, uATARegHCylHOB);
    GEN_CHECK_OFF(ATADevState, uATARegSelect);
    GEN_CHECK_OFF(ATADevState, uATARegStatus);
    GEN_CHECK_OFF(ATADevState, uATARegCommand);
    GEN_CHECK_OFF(ATADevState, uATARegDevCtl);
    GEN_CHECK_OFF(ATADevState, uATATransferMode);
    GEN_CHECK_OFF(ATADevState, uTxDir);
    GEN_CHECK_OFF(ATADevState, iBeginTransfer);
    GEN_CHECK_OFF(ATADevState, iSourceSink);
    GEN_CHECK_OFF(ATADevState, fDMA);
    GEN_CHECK_OFF(ATADevState, fATAPITransfer);
    GEN_CHECK_OFF(ATADevState, cbTotalTransfer);
    GEN_CHECK_OFF(ATADevState, cbElementaryTransfer);
    GEN_CHECK_OFF(ATADevState, iIOBufferCur);
    GEN_CHECK_OFF(ATADevState, iIOBufferEnd);
    GEN_CHECK_OFF(ATADevState, iIOBufferPIODataStart);
    GEN_CHECK_OFF(ATADevState, iIOBufferPIODataEnd);
    GEN_CHECK_OFF(ATADevState, iATAPILBA);
    GEN_CHECK_OFF(ATADevState, cbATAPISector);
    GEN_CHECK_OFF(ATADevState, aATAPICmd);
    GEN_CHECK_OFF(ATADevState, aATAPICmd[ATAPI_PACKET_SIZE - 1]);
    GEN_CHECK_OFF(ATADevState, abATAPISense);
    GEN_CHECK_OFF(ATADevState, abATAPISense[ATAPI_SENSE_SIZE - 1]);
    GEN_CHECK_OFF(ATADevState, cNotifiedMediaChange);
    GEN_CHECK_OFF(ATADevState, MediaEventStatus);
    GEN_CHECK_OFF(ATADevState, MediaTrackType);
    GEN_CHECK_OFF(ATADevState, Led);
    GEN_CHECK_OFF(ATADevState, cbIOBuffer);
    GEN_CHECK_OFF(ATADevState, pbIOBufferR3);
    GEN_CHECK_OFF(ATADevState, pbIOBufferR0);
    GEN_CHECK_OFF(ATADevState, pbIOBufferRC);
    GEN_CHECK_OFF(ATADevState, StatATADMA);
    GEN_CHECK_OFF(ATADevState, StatATAPIO);
    GEN_CHECK_OFF(ATADevState, StatATAPIDMA);
    GEN_CHECK_OFF(ATADevState, StatATAPIPIO);
    GEN_CHECK_OFF(ATADevState, StatReads);
    GEN_CHECK_OFF(ATADevState, StatBytesRead);
    GEN_CHECK_OFF(ATADevState, StatWrites);
    GEN_CHECK_OFF(ATADevState, StatBytesWritten);
    GEN_CHECK_OFF(ATADevState, StatFlushes);
    GEN_CHECK_OFF(ATADevState, fATAPIPassthrough);
    GEN_CHECK_OFF(ATADevState, fOverwriteInquiry);
    GEN_CHECK_OFF(ATADevState, cErrors);
    GEN_CHECK_OFF(ATADevState, pDrvBase);
    GEN_CHECK_OFF(ATADevState, pDrvMedia);
    GEN_CHECK_OFF(ATADevState, pDrvMount);
    GEN_CHECK_OFF(ATADevState, IBase);
    GEN_CHECK_OFF(ATADevState, IPort);
    GEN_CHECK_OFF(ATADevState, IMountNotify);
    GEN_CHECK_OFF(ATADevState, iLUN);
    GEN_CHECK_OFF(ATADevState, pDevInsR3);
    GEN_CHECK_OFF(ATADevState, pDevInsR0);
    GEN_CHECK_OFF(ATADevState, pDevInsRC);
    GEN_CHECK_OFF(ATADevState, pControllerR3);
    GEN_CHECK_OFF(ATADevState, pControllerR0);
    GEN_CHECK_OFF(ATADevState, pControllerRC);
    GEN_CHECK_OFF(ATADevState, szSerialNumber);
    GEN_CHECK_OFF(ATADevState, szSerialNumber[ATA_SERIAL_NUMBER_LENGTH]);
    GEN_CHECK_OFF(ATADevState, szFirmwareRevision);
    GEN_CHECK_OFF(ATADevState, szFirmwareRevision[ATA_FIRMWARE_REVISION_LENGTH]);
    GEN_CHECK_OFF(ATADevState, szModelNumber);
    GEN_CHECK_OFF(ATADevState, szModelNumber[ATA_MODEL_NUMBER_LENGTH]);
    GEN_CHECK_OFF(ATADevState, szInquiryVendorId);
    GEN_CHECK_OFF(ATADevState, szInquiryVendorId[SCSI_INQUIRY_VENDOR_ID_LENGTH]);
    GEN_CHECK_OFF(ATADevState, szInquiryProductId);
    GEN_CHECK_OFF(ATADevState, szInquiryProductId[SCSI_INQUIRY_PRODUCT_ID_LENGTH]);
    GEN_CHECK_OFF(ATADevState, szInquiryRevision);
    GEN_CHECK_OFF(ATADevState, szInquiryRevision[SCSI_INQUIRY_REVISION_LENGTH]);
    GEN_CHECK_OFF(ATADevState, pTrackList);
    GEN_CHECK_SIZE(ATATransferRequest);
    GEN_CHECK_OFF(ATATransferRequest, iIf);
    GEN_CHECK_OFF(ATATransferRequest, iBeginTransfer);
    GEN_CHECK_OFF(ATATransferRequest, iSourceSink);
    GEN_CHECK_OFF(ATATransferRequest, cbTotalTransfer);
    GEN_CHECK_OFF(ATATransferRequest, uTxDir);
    GEN_CHECK_SIZE(ATAAbortRequest);
    GEN_CHECK_OFF(ATAAbortRequest, iIf);
    GEN_CHECK_OFF(ATAAbortRequest, fResetDrive);
    GEN_CHECK_SIZE(ATARequest);
    GEN_CHECK_OFF(ATARequest, ReqType);
    GEN_CHECK_OFF(ATARequest, u);
    GEN_CHECK_OFF(ATARequest, u.t);
    GEN_CHECK_OFF(ATARequest, u.a);
    GEN_CHECK_SIZE(ATACONTROLLER);
    GEN_CHECK_OFF(ATACONTROLLER, IOPortBase1);
    GEN_CHECK_OFF(ATACONTROLLER, IOPortBase2);
    GEN_CHECK_OFF(ATACONTROLLER, irq);
    GEN_CHECK_OFF(ATACONTROLLER, lock);
    GEN_CHECK_OFF(ATACONTROLLER, iSelectedIf);
    GEN_CHECK_OFF(ATACONTROLLER, iAIOIf);
    GEN_CHECK_OFF(ATACONTROLLER, uAsyncIOState);
    GEN_CHECK_OFF(ATACONTROLLER, fChainedTransfer);
    GEN_CHECK_OFF(ATACONTROLLER, fReset);
    GEN_CHECK_OFF(ATACONTROLLER, fRedo);
    GEN_CHECK_OFF(ATACONTROLLER, fRedoIdle);
    GEN_CHECK_OFF(ATACONTROLLER, fRedoDMALastDesc);
    GEN_CHECK_OFF(ATACONTROLLER, BmDma);
    GEN_CHECK_OFF(ATACONTROLLER, GCPhysFirstDMADesc);
    GEN_CHECK_OFF(ATACONTROLLER, GCPhysLastDMADesc);
    GEN_CHECK_OFF(ATACONTROLLER, GCPhysRedoDMABuffer);
    GEN_CHECK_OFF(ATACONTROLLER, cbRedoDMABuffer);
    GEN_CHECK_OFF(ATACONTROLLER, aIfs);
    GEN_CHECK_OFF(ATACONTROLLER, aIfs[1]);
    GEN_CHECK_OFF(ATACONTROLLER, pDevInsR3);
    GEN_CHECK_OFF(ATACONTROLLER, pDevInsR0);
    GEN_CHECK_OFF(ATACONTROLLER, pDevInsRC);
    GEN_CHECK_OFF(ATACONTROLLER, fShutdown);
    GEN_CHECK_OFF(ATACONTROLLER, AsyncIOThread);
    GEN_CHECK_OFF(ATACONTROLLER, hAsyncIOSem);
    GEN_CHECK_OFF(ATACONTROLLER, aAsyncIORequests[4]);
    GEN_CHECK_OFF(ATACONTROLLER, AsyncIOReqHead);
    GEN_CHECK_OFF(ATACONTROLLER, AsyncIOReqTail);
    GEN_CHECK_OFF(ATACONTROLLER, AsyncIORequestLock);
    GEN_CHECK_OFF(ATACONTROLLER, SuspendIOSem);
    GEN_CHECK_OFF(ATACONTROLLER, fSignalIdle);
    GEN_CHECK_OFF(ATACONTROLLER, DelayIRQMillies);
    GEN_CHECK_OFF(ATACONTROLLER, u64ResetTime);
    GEN_CHECK_OFF(ATACONTROLLER, StatAsyncOps);
    GEN_CHECK_OFF(ATACONTROLLER, StatAsyncMinWait);
    GEN_CHECK_OFF(ATACONTROLLER, StatAsyncMaxWait);
    GEN_CHECK_OFF(ATACONTROLLER, StatAsyncTimeUS);
    GEN_CHECK_OFF(ATACONTROLLER, StatAsyncTime);
    GEN_CHECK_OFF(ATACONTROLLER, StatLockWait);
    GEN_CHECK_SIZE(PCIATAState);
    GEN_CHECK_OFF(PCIATAState, dev);
    GEN_CHECK_OFF(PCIATAState, aCts);
    GEN_CHECK_OFF(PCIATAState, aCts[1]);
    GEN_CHECK_OFF(PCIATAState, pDevIns);
    GEN_CHECK_OFF(PCIATAState, IBase);
    GEN_CHECK_OFF(PCIATAState, ILeds);
    GEN_CHECK_OFF(PCIATAState, pLedsConnector);
    GEN_CHECK_OFF(PCIATAState, fRCEnabled);
    GEN_CHECK_OFF(PCIATAState, fR0Enabled);

#ifdef VBOX_WITH_USB
    /* USB/DevOHCI.cpp */
    GEN_CHECK_SIZE(OHCIHUBPORT);
    GEN_CHECK_OFF(OHCIHUBPORT, fReg);
    GEN_CHECK_OFF(OHCIHUBPORT, pDev);

    GEN_CHECK_SIZE(OHCIROOTHUB);
    GEN_CHECK_OFF(OHCIROOTHUB, pIBase);
    GEN_CHECK_OFF(OHCIROOTHUB, pIRhConn);
    GEN_CHECK_OFF(OHCIROOTHUB, pIDev);
    GEN_CHECK_OFF(OHCIROOTHUB, IBase);
    GEN_CHECK_OFF(OHCIROOTHUB, IRhPort);
    GEN_CHECK_OFF(OHCIROOTHUB, status);
    GEN_CHECK_OFF(OHCIROOTHUB, desc_a);
    GEN_CHECK_OFF(OHCIROOTHUB, desc_b);
    GEN_CHECK_OFF(OHCIROOTHUB, aPorts);
    GEN_CHECK_OFF(OHCIROOTHUB, aPorts[1]);
    GEN_CHECK_OFF(OHCIROOTHUB, aPorts[OHCI_NDP_MAX - 1]);
    GEN_CHECK_OFF(OHCIROOTHUB, pOhci);

    GEN_CHECK_SIZE(OHCI);
    GEN_CHECK_OFF(OHCI, PciDev);
    GEN_CHECK_OFF(OHCI, MMIOBase);
    GEN_CHECK_OFF(OHCI, pEndOfFrameTimerR3);
    GEN_CHECK_OFF(OHCI, pEndOfFrameTimerR0);
    GEN_CHECK_OFF(OHCI, pEndOfFrameTimerRC);
    GEN_CHECK_OFF(OHCI, pDevInsR3);
    GEN_CHECK_OFF(OHCI, pDevInsR0);
    GEN_CHECK_OFF(OHCI, pDevInsRC);
    GEN_CHECK_OFF(OHCI, SofTime);
    //GEN_CHECK_OFF(OHCI, dqic:3);
    //GEN_CHECK_OFF(OHCI, fno:1);
    GEN_CHECK_OFF(OHCI, RootHub);
    GEN_CHECK_OFF(OHCI, ctl);
    GEN_CHECK_OFF(OHCI, status);
    GEN_CHECK_OFF(OHCI, intr_status);
    GEN_CHECK_OFF(OHCI, intr);
    GEN_CHECK_OFF(OHCI, hcca);
    GEN_CHECK_OFF(OHCI, per_cur);
    GEN_CHECK_OFF(OHCI, ctrl_cur);
    GEN_CHECK_OFF(OHCI, ctrl_head);
    GEN_CHECK_OFF(OHCI, bulk_cur);
    GEN_CHECK_OFF(OHCI, bulk_head);
    GEN_CHECK_OFF(OHCI, done);
    //GEN_CHECK_OFF(OHCI, fsmps:15);
    //GEN_CHECK_OFF(OHCI, fit:1);
    //GEN_CHECK_OFF(OHCI, fi:14);
    //GEN_CHECK_OFF(OHCI, frt:1);
    GEN_CHECK_OFF(OHCI, HcFmNumber);
    GEN_CHECK_OFF(OHCI, pstart);
    GEN_CHECK_OFF(OHCI, cTicksPerFrame);
    GEN_CHECK_OFF(OHCI, cTicksPerUsbTick);
    GEN_CHECK_OFF(OHCI, cInFlight);
    GEN_CHECK_OFF(OHCI, aInFlight);
    GEN_CHECK_OFF(OHCI, aInFlight[0].GCPhysTD);
    GEN_CHECK_OFF(OHCI, aInFlight[0].pUrb);
    GEN_CHECK_OFF(OHCI, aInFlight[1]);
    GEN_CHECK_OFF(OHCI, cInDoneQueue);
    GEN_CHECK_OFF(OHCI, aInDoneQueue);
    GEN_CHECK_OFF(OHCI, aInDoneQueue[0].GCPhysTD);
    GEN_CHECK_OFF(OHCI, aInDoneQueue[1]);
    GEN_CHECK_OFF(OHCI, u32FmDoneQueueTail);
    GEN_CHECK_OFF(OHCI, pLoad);
# ifdef VBOX_WITH_STATISTICS
    GEN_CHECK_OFF(OHCI, StatCanceledIsocUrbs);
    GEN_CHECK_OFF(OHCI, StatCanceledGenUrbs);
    GEN_CHECK_OFF(OHCI, StatDroppedUrbs);
    GEN_CHECK_OFF(OHCI, StatTimer);
# endif
    GEN_CHECK_OFF(OHCI, CsIrq);
    GEN_CHECK_OFF(OHCI, CritSect);

# ifdef VBOX_WITH_EHCI_IMPL
    /* USB/DevEHCI.cpp */
    GEN_CHECK_SIZE(EHCIHUBPORT);
    GEN_CHECK_OFF(EHCIHUBPORT, fReg);
    GEN_CHECK_OFF(EHCIHUBPORT, pDev);

    GEN_CHECK_SIZE(EHCIROOTHUB);
    GEN_CHECK_OFF(EHCIROOTHUB, pIBase);
    GEN_CHECK_OFF(EHCIROOTHUB, pIRhConn);
    GEN_CHECK_OFF(EHCIROOTHUB, pIDev);
    GEN_CHECK_OFF(EHCIROOTHUB, IBase);
    GEN_CHECK_OFF(EHCIROOTHUB, IRhPort);
    GEN_CHECK_OFF(EHCIROOTHUB, Led);
    GEN_CHECK_OFF(EHCIROOTHUB, ILeds);
    GEN_CHECK_OFF(EHCIROOTHUB, pLedsConnector);
    GEN_CHECK_OFF(EHCIROOTHUB, aPorts);
    GEN_CHECK_OFF(EHCIROOTHUB, aPorts[1]);
    GEN_CHECK_OFF(EHCIROOTHUB, aPorts[EHCI_NDP_MAX - 1]);
    GEN_CHECK_OFF(EHCIROOTHUB, pEhci);

    GEN_CHECK_SIZE(EHCI);
    GEN_CHECK_OFF(EHCI, PciDev);
    GEN_CHECK_OFF(EHCI, MMIOBase);
    GEN_CHECK_OFF(EHCI, pDevInsR3);
    GEN_CHECK_OFF(EHCI, pDevInsR0);
    GEN_CHECK_OFF(EHCI, pDevInsRC);
    GEN_CHECK_OFF(EHCI, MMIOBase);
    GEN_CHECK_OFF(EHCI, SofTime);
    GEN_CHECK_OFF(EHCI, RootHub);
    GEN_CHECK_OFF(EHCI, cap_length);
    GEN_CHECK_OFF(EHCI, hci_version);
    GEN_CHECK_OFF(EHCI, hcs_params);
    GEN_CHECK_OFF(EHCI, hcc_params);
    GEN_CHECK_OFF(EHCI, cmd);
    GEN_CHECK_OFF(EHCI, intr_status);
    GEN_CHECK_OFF(EHCI, intr);
    GEN_CHECK_OFF(EHCI, frame_idx);
    GEN_CHECK_OFF(EHCI, ds_segment);
    GEN_CHECK_OFF(EHCI, periodic_list_base);
    GEN_CHECK_OFF(EHCI, async_list_base);
    GEN_CHECK_OFF(EHCI, config);
    GEN_CHECK_OFF(EHCI, uIrqInterval);
    GEN_CHECK_OFF(EHCI, HcFmNumber);
    GEN_CHECK_OFF(EHCI, uFramesPerTimerCall);
    GEN_CHECK_OFF(EHCI, cTicksPerFrame);
    GEN_CHECK_OFF(EHCI, cTicksPerUsbTick);
    GEN_CHECK_OFF(EHCI, cInFlight);
    GEN_CHECK_OFF(EHCI, aInFlight);
    GEN_CHECK_OFF(EHCI, aInFlight[0].GCPhysTD);
    GEN_CHECK_OFF(EHCI, aInFlight[0].pUrb);
    GEN_CHECK_OFF(EHCI, aInFlight[1]);
    GEN_CHECK_OFF(EHCI, aInFlight[256]);
    GEN_CHECK_OFF(EHCI, pLoad);
    GEN_CHECK_OFF(EHCI, fAsyncTraversalTimerActive);
#  ifdef VBOX_WITH_STATISTICS
    GEN_CHECK_OFF(EHCI, StatCanceledIsocUrbs);
    GEN_CHECK_OFF(EHCI, StatCanceledGenUrbs);
    GEN_CHECK_OFF(EHCI, StatDroppedUrbs);
#  endif
    GEN_CHECK_OFF(EHCI, u64TimerHz);
    GEN_CHECK_OFF(EHCI, cIdleCycles);
    GEN_CHECK_OFF(EHCI, uFrameRate);
    GEN_CHECK_OFF(EHCI, fIdle);
    GEN_CHECK_OFF(EHCI, hThreadFrame);
    GEN_CHECK_OFF(EHCI, hSemEventFrame);
    GEN_CHECK_OFF(EHCI, fBusStarted);
    GEN_CHECK_OFF(EHCI, CsIrq);
    GEN_CHECK_OFF(EHCI, uFrameRateDefault);
    GEN_CHECK_OFF(EHCI, nsWait);
    GEN_CHECK_OFF(EHCI, CritSect);
# endif /* VBOX_WITH_EHCI_IMPL */

# ifdef VBOX_WITH_XHCI_IMPL
    /* USB/DevXHCI.cpp */
    GEN_CHECK_SIZE(XHCIHUBPORT);
    GEN_CHECK_OFF(XHCIHUBPORT, portsc);
    GEN_CHECK_OFF(XHCIHUBPORT, portpm);
    GEN_CHECK_OFF(XHCIHUBPORT, portli);
    GEN_CHECK_OFF(XHCIHUBPORT, pDev);

    GEN_CHECK_SIZE(XHCIROOTHUB);
    GEN_CHECK_OFF(XHCIROOTHUB, pIBase);
    GEN_CHECK_OFF(XHCIROOTHUB, pIRhConn);
    GEN_CHECK_OFF(XHCIROOTHUB, pIDev);
    GEN_CHECK_OFF(XHCIROOTHUB, IBase);
    GEN_CHECK_OFF(XHCIROOTHUB, IRhPort);
    GEN_CHECK_OFF(XHCIROOTHUB, Led);
    GEN_CHECK_OFF(XHCIROOTHUB, cPortsImpl);
    GEN_CHECK_OFF(XHCIROOTHUB, pXhci);

    GEN_CHECK_SIZE(XHCIINTRPTR);
    GEN_CHECK_OFF(XHCIINTRPTR, iman);
    GEN_CHECK_OFF(XHCIINTRPTR, imod);
    GEN_CHECK_OFF(XHCIINTRPTR, erstba);
    GEN_CHECK_OFF(XHCIINTRPTR, erdp);
    GEN_CHECK_OFF(XHCIINTRPTR, erep);
    GEN_CHECK_OFF(XHCIINTRPTR, erst_idx);
    GEN_CHECK_OFF(XHCIINTRPTR, trb_count);
    GEN_CHECK_OFF(XHCIINTRPTR, evtr_pcs);
    GEN_CHECK_OFF(XHCIINTRPTR, ipe);

    GEN_CHECK_SIZE(XHCI);
    GEN_CHECK_OFF(XHCI, PciDev);
    GEN_CHECK_OFF(XHCI, pDevInsR3);
    GEN_CHECK_OFF(XHCI, pDevInsR0);
    GEN_CHECK_OFF(XHCI, pDevInsRC);
    GEN_CHECK_OFF(XHCI, pNotifierQueueR3);
    GEN_CHECK_OFF(XHCI, pNotifierQueueR0);
    GEN_CHECK_OFF(XHCI, pNotifierQueueRC);
    GEN_CHECK_OFF(XHCI, pWrapTimerR3);
    GEN_CHECK_OFF(XHCI, pWrapTimerR0);
    GEN_CHECK_OFF(XHCI, pWrapTimerRC);
    GEN_CHECK_OFF(XHCI, pWorkerThread);
    GEN_CHECK_OFF(XHCI, pSupDrvSession);
    GEN_CHECK_OFF(XHCI, hEvtProcess);
    GEN_CHECK_OFF(XHCI, fWrkThreadSleeping);
    GEN_CHECK_OFF(XHCI, u32TasksNew);
    GEN_CHECK_OFF(XHCI, ILeds);
    GEN_CHECK_OFF(XHCI, pLedsConnector);
    GEN_CHECK_OFF(XHCI, MMIOBase);
    GEN_CHECK_OFF(XHCI, RootHub2);
    GEN_CHECK_OFF(XHCI, RootHub3);
    GEN_CHECK_OFF(XHCI, aPorts);
    GEN_CHECK_OFF(XHCI, aPorts[1]);
    GEN_CHECK_OFF(XHCI, aPorts[XHCI_NDP_MAX - 1]);
    GEN_CHECK_OFF(XHCI, cap_length);
    GEN_CHECK_OFF(XHCI, hci_version);
    GEN_CHECK_OFF(XHCI, hcs_params3);
    GEN_CHECK_OFF(XHCI, hcc_params);
    GEN_CHECK_OFF(XHCI, dbell_off);
    GEN_CHECK_OFF(XHCI, rts_off);
    GEN_CHECK_OFF(XHCI, cmd);
    GEN_CHECK_OFF(XHCI, status);
    GEN_CHECK_OFF(XHCI, dnctrl);
    GEN_CHECK_OFF(XHCI, config);
    GEN_CHECK_OFF(XHCI, crcr);
    GEN_CHECK_OFF(XHCI, dcbaap);
    GEN_CHECK_OFF(XHCI, abExtCap);
    GEN_CHECK_OFF(XHCI, cbExtCap);
    GEN_CHECK_OFF(XHCI, cmdr_dqp);
    GEN_CHECK_OFF(XHCI, cmdr_ccs);
    GEN_CHECK_OFF(XHCI, aSlotState);
    GEN_CHECK_OFF(XHCI, aBellsRung);
    GEN_CHECK_OFF(XHCI, pLoad);
#  ifdef VBOX_WITH_STATISTICS
    GEN_CHECK_OFF(XHCI, StatErrorIsocUrbs);
    GEN_CHECK_OFF(XHCI, StatErrorIsocPkts);
    GEN_CHECK_OFF(XHCI, StatEventsWritten);
    GEN_CHECK_OFF(XHCI, StatEventsDropped);
    GEN_CHECK_OFF(XHCI, StatIntrsPending);
    GEN_CHECK_OFF(XHCI, StatIntrsSet);
    GEN_CHECK_OFF(XHCI, StatIntrsNotSet);
    GEN_CHECK_OFF(XHCI, StatIntrsCleared);
#  endif
# endif /* VBOX_WITH_XHCI_IMPL */
#endif /* VBOX_WITH_USB */

    /* Parallel/DevParallel.cpp */
    GEN_CHECK_SIZE(PARALLELPORT);
    GEN_CHECK_OFF(PARALLELPORT, pDevInsR3);
    GEN_CHECK_OFF(PARALLELPORT, pDevInsR0);
    GEN_CHECK_OFF(PARALLELPORT, pDevInsRC);
    GEN_CHECK_OFF(PARALLELPORT, IBase);
    GEN_CHECK_OFF(PARALLELPORT, IHostParallelPort);
    GEN_CHECK_OFF(PARALLELPORT, pDrvHostParallelConnector);
    GEN_CHECK_OFF(PARALLELPORT, fGCEnabled);
    GEN_CHECK_OFF(PARALLELPORT, fR0Enabled);
    GEN_CHECK_OFF(PARALLELPORT, fEppTimeout);
    GEN_CHECK_OFF(PARALLELPORT, IOBase);
    GEN_CHECK_OFF(PARALLELPORT, iIrq);
    GEN_CHECK_OFF(PARALLELPORT, regData);
    GEN_CHECK_OFF(PARALLELPORT, regStatus);
    GEN_CHECK_OFF(PARALLELPORT, regControl);
    GEN_CHECK_OFF(PARALLELPORT, regEppAddr);
    GEN_CHECK_OFF(PARALLELPORT, regEppData);
#if 0
    GEN_CHECK_OFF(PARALLELPORT, reg_ecp_ecr);
    GEN_CHECK_OFF(PARALLELPORT, reg_ecp_base_plus_400h);
    GEN_CHECK_OFF(PARALLELPORT, reg_ecp_config_b);
    GEN_CHECK_OFF(PARALLELPORT, ecp_fifo);
    GEN_CHECK_OFF(PARALLELPORT, ecp_fifo[1]);
    GEN_CHECK_OFF(PARALLELPORT, act_fifo_pos_write);
    GEN_CHECK_OFF(PARALLELPORT, act_fifo_pos_read);
#endif

    /* Serial/UartCore.cpp */
    GEN_CHECK_SIZE(UARTCORE);
    GEN_CHECK_OFF(UARTCORE, CritSect);
    GEN_CHECK_OFF(UARTCORE, pDevInsR3);
    GEN_CHECK_OFF(UARTCORE, pDevInsR0);
    GEN_CHECK_OFF(UARTCORE, pDevInsRC);
    GEN_CHECK_OFF(UARTCORE, iLUN);
    GEN_CHECK_OFF(UARTCORE, IBase);
    GEN_CHECK_OFF(UARTCORE, ISerialPort);
    GEN_CHECK_OFF(UARTCORE, pDrvBase);
    GEN_CHECK_OFF(UARTCORE, pDrvSerial);
    GEN_CHECK_OFF(UARTCORE, fFlags);
    GEN_CHECK_OFF(UARTCORE, enmType);
    GEN_CHECK_OFF(UARTCORE, pTimerRcvFifoTimeoutR3);
    GEN_CHECK_OFF(UARTCORE, pTimerTxUnconnectedR3);
    GEN_CHECK_OFF(UARTCORE, pfnUartIrqReqR3);
    GEN_CHECK_OFF(UARTCORE, pTimerRcvFifoTimeoutR0);
    GEN_CHECK_OFF(UARTCORE, pTimerTxUnconnectedR0);
    GEN_CHECK_OFF(UARTCORE, pfnUartIrqReqR0);
    GEN_CHECK_OFF(UARTCORE, pTimerRcvFifoTimeoutRC);
    GEN_CHECK_OFF(UARTCORE, pTimerTxUnconnectedRC);
    GEN_CHECK_OFF(UARTCORE, pfnUartIrqReqRC);
    GEN_CHECK_OFF(UARTCORE, uRegDivisor);
    GEN_CHECK_OFF(UARTCORE, uRegRbr);
    GEN_CHECK_OFF(UARTCORE, uRegThr);
    GEN_CHECK_OFF(UARTCORE, uRegIer);
    GEN_CHECK_OFF(UARTCORE, uRegIir);
    GEN_CHECK_OFF(UARTCORE, uRegFcr);
    GEN_CHECK_OFF(UARTCORE, uRegLcr);
    GEN_CHECK_OFF(UARTCORE, uRegMcr);
    GEN_CHECK_OFF(UARTCORE, uRegLsr);
    GEN_CHECK_OFF(UARTCORE, uRegMsr);
    GEN_CHECK_OFF(UARTCORE, uRegScr);
    GEN_CHECK_OFF(UARTCORE, fIrqCtiPending);
    GEN_CHECK_OFF(UARTCORE, fThreEmptyPending);
    GEN_CHECK_OFF(UARTCORE, FifoXmit);
    GEN_CHECK_OFF(UARTCORE, FifoRecv);
    GEN_CHECK_OFF(UARTCORE, cSymbolXferTicks);
    GEN_CHECK_OFF(UARTCORE, cbAvailRdr);

    /* Serial/DevSerial.cpp */
    GEN_CHECK_SIZE(DEVSERIAL);
    GEN_CHECK_OFF(DEVSERIAL, pDevInsR3);
    GEN_CHECK_OFF(DEVSERIAL, pDevInsR0);
    GEN_CHECK_OFF(DEVSERIAL, pDevInsRC);
    GEN_CHECK_OFF(DEVSERIAL, fR0Enabled);
    GEN_CHECK_OFF(DEVSERIAL, fRCEnabled);
    GEN_CHECK_OFF(DEVSERIAL, uIrq);
    GEN_CHECK_OFF(DEVSERIAL, PortBase);
    GEN_CHECK_OFF(DEVSERIAL, UartCore);

    /* Serial/DevOxPcie958.cpp */
    GEN_CHECK_SIZE(DEVOX958);
    GEN_CHECK_OFF(DEVOX958, PciDev);
    GEN_CHECK_OFF(DEVOX958, pDevInsR3);
    GEN_CHECK_OFF(DEVOX958, pDevInsR0);
    GEN_CHECK_OFF(DEVOX958, pDevInsRC);
    GEN_CHECK_OFF(DEVOX958, fR0Enabled);
    GEN_CHECK_OFF(DEVOX958, fRCEnabled);
    GEN_CHECK_OFF(DEVOX958, u32RegIrqStsGlob);
    GEN_CHECK_OFF(DEVOX958, u32RegIrqEnGlob);
    GEN_CHECK_OFF(DEVOX958, u32RegIrqEnWake);
    GEN_CHECK_OFF(DEVOX958, cUarts);
    GEN_CHECK_OFF(DEVOX958, GCPhysMMIO);
    GEN_CHECK_OFF(DEVOX958, aUarts);
    GEN_CHECK_OFF(DEVOX958, aUarts[OX958_UARTS_MAX - 1]);

#ifdef VBOX_WITH_AHCI
    /* Storage/DevAHCI.cpp */

    GEN_CHECK_SIZE(AHCIPort);
    GEN_CHECK_OFF(AHCIPort, pDevInsR3);
    GEN_CHECK_OFF(AHCIPort, pDevInsR0);
    GEN_CHECK_OFF(AHCIPort, pDevInsRC);
    GEN_CHECK_OFF(AHCIPort, pAhciR3);
    GEN_CHECK_OFF(AHCIPort, pAhciR0);
    GEN_CHECK_OFF(AHCIPort, pAhciRC);
    GEN_CHECK_OFF(AHCIPort, regCLB);
    GEN_CHECK_OFF(AHCIPort, regCLBU);
    GEN_CHECK_OFF(AHCIPort, regFB);
    GEN_CHECK_OFF(AHCIPort, regFBU);
    GEN_CHECK_OFF(AHCIPort, regIS);
    GEN_CHECK_OFF(AHCIPort, regIE);
    GEN_CHECK_OFF(AHCIPort, regCMD);
    GEN_CHECK_OFF(AHCIPort, regTFD);
    GEN_CHECK_OFF(AHCIPort, regSIG);
    GEN_CHECK_OFF(AHCIPort, regSSTS);
    GEN_CHECK_OFF(AHCIPort, regSCTL);
    GEN_CHECK_OFF(AHCIPort, regSERR);
    GEN_CHECK_OFF(AHCIPort, regSACT);
    GEN_CHECK_OFF(AHCIPort, regCI);
    GEN_CHECK_OFF(AHCIPort, cTasksActive);
    GEN_CHECK_OFF(AHCIPort, GCPhysAddrClb);
    GEN_CHECK_OFF(AHCIPort, GCPhysAddrFb);
    GEN_CHECK_OFF(AHCIPort, fPoweredOn);
    GEN_CHECK_OFF(AHCIPort, fSpunUp);
    GEN_CHECK_OFF(AHCIPort, fFirstD2HFisSent);
    GEN_CHECK_OFF(AHCIPort, fATAPI);
    GEN_CHECK_OFF(AHCIPort, fPortReset);
    GEN_CHECK_OFF(AHCIPort, fTrimEnabled);
    GEN_CHECK_OFF(AHCIPort, fResetDevice);
    GEN_CHECK_OFF(AHCIPort, fHotpluggable);
    GEN_CHECK_OFF(AHCIPort, fRedo);
    GEN_CHECK_OFF(AHCIPort, fWrkThreadSleeping);
    GEN_CHECK_OFF(AHCIPort, cTotalSectors);
    GEN_CHECK_OFF(AHCIPort, cbSector);
    GEN_CHECK_OFF(AHCIPort, cMultSectors);
    GEN_CHECK_OFF(AHCIPort, uATATransferMode);
    GEN_CHECK_OFF(AHCIPort, abATAPISense);
    GEN_CHECK_OFF(AHCIPort, cLogSectorsPerPhysicalExp);
    GEN_CHECK_OFF(AHCIPort, iLUN);
    GEN_CHECK_OFF(AHCIPort, u32TasksFinished);
    GEN_CHECK_OFF(AHCIPort, u32QueuedTasksFinished);
    GEN_CHECK_OFF(AHCIPort, u32TasksNew);
    GEN_CHECK_OFF(AHCIPort, u32TasksRedo);
    GEN_CHECK_OFF(AHCIPort, u32CurrentCommandSlot);
    GEN_CHECK_OFF(AHCIPort, pDrvBase);
    GEN_CHECK_OFF(AHCIPort, pDrvMedia);
    GEN_CHECK_OFF(AHCIPort, pDrvMediaEx);
    GEN_CHECK_OFF(AHCIPort, pszDesc);
    GEN_CHECK_OFF(AHCIPort, IBase);
    GEN_CHECK_OFF(AHCIPort, IPort);
    GEN_CHECK_OFF(AHCIPort, IMediaExPort);
    GEN_CHECK_OFF(AHCIPort, PCHSGeometry);
    GEN_CHECK_OFF(AHCIPort, Led);
    GEN_CHECK_OFF(AHCIPort, pAsyncIOThread);

    GEN_CHECK_OFF(AHCIPort, pTaskErr);
    GEN_CHECK_OFF(AHCIPort, hEvtProcess);
    GEN_CHECK_OFF(AHCIPort, szSerialNumber);
    GEN_CHECK_OFF(AHCIPort, szSerialNumber[AHCI_SERIAL_NUMBER_LENGTH]); /* One additional byte for the termination.*/
    GEN_CHECK_OFF(AHCIPort, szFirmwareRevision);
    GEN_CHECK_OFF(AHCIPort, szFirmwareRevision[AHCI_FIRMWARE_REVISION_LENGTH]); /* One additional byte for the termination.*/
    GEN_CHECK_OFF(AHCIPort, szModelNumber);
    GEN_CHECK_OFF(AHCIPort, szModelNumber[AHCI_MODEL_NUMBER_LENGTH]); /* One additional byte for the termination.*/
    GEN_CHECK_OFF(AHCIPort, szInquiryVendorId);
    GEN_CHECK_OFF(AHCIPort, szInquiryVendorId[AHCI_ATAPI_INQUIRY_VENDOR_ID_LENGTH]); /* One additional byte for the termination.*/
    GEN_CHECK_OFF(AHCIPort, szInquiryProductId);
    GEN_CHECK_OFF(AHCIPort, szInquiryProductId[AHCI_ATAPI_INQUIRY_PRODUCT_ID_LENGTH]); /* One additional byte for the termination.*/
    GEN_CHECK_OFF(AHCIPort, szInquiryRevision);
    GEN_CHECK_OFF(AHCIPort, szInquiryRevision[AHCI_ATAPI_INQUIRY_REVISION_LENGTH]); /* One additional byte for the termination.*/
    GEN_CHECK_OFF(AHCIPort, cErrors);
    GEN_CHECK_OFF(AHCIPort, fRedo);

    GEN_CHECK_SIZE(AHCI);
    GEN_CHECK_OFF(AHCI, dev);
    GEN_CHECK_OFF(AHCI, pDevInsR3);
    GEN_CHECK_OFF(AHCI, pDevInsR0);
    GEN_CHECK_OFF(AHCI, pDevInsRC);
    GEN_CHECK_OFF(AHCI, IBase);
    GEN_CHECK_OFF(AHCI, ILeds);
    GEN_CHECK_OFF(AHCI, pLedsConnector);
    GEN_CHECK_OFF(AHCI, MMIOBase);
    GEN_CHECK_OFF(AHCI, regHbaCap);
    GEN_CHECK_OFF(AHCI, regHbaCtrl);
    GEN_CHECK_OFF(AHCI, regHbaIs);
    GEN_CHECK_OFF(AHCI, regHbaPi);
    GEN_CHECK_OFF(AHCI, regHbaVs);
    GEN_CHECK_OFF(AHCI, regHbaCccCtl);
    GEN_CHECK_OFF(AHCI, regHbaCccPorts);
    GEN_CHECK_OFF(AHCI, regIdx);
    GEN_CHECK_OFF(AHCI, pHbaCccTimerR3);
    GEN_CHECK_OFF(AHCI, pHbaCccTimerR0);
    GEN_CHECK_OFF(AHCI, pHbaCccTimerRC);
    GEN_CHECK_OFF(AHCI, pNotifierQueueR3);
    GEN_CHECK_OFF(AHCI, pNotifierQueueR0);
    GEN_CHECK_OFF(AHCI, pNotifierQueueRC);
    GEN_CHECK_OFF(AHCI, uCccPortNr);
    GEN_CHECK_OFF(AHCI, uCccTimeout);
    GEN_CHECK_OFF(AHCI, uCccNr);
    GEN_CHECK_OFF(AHCI, uCccCurrentNr);
    GEN_CHECK_OFF(AHCI, ahciPort);
    GEN_CHECK_OFF(AHCI, ahciPort[AHCI_MAX_NR_PORTS_IMPL-1]);
    GEN_CHECK_OFF(AHCI, lock);
    GEN_CHECK_OFF(AHCI, u32PortsInterrupted);
    GEN_CHECK_OFF(AHCI, cThreadsActive);
    GEN_CHECK_OFF(AHCI, fReset);
    GEN_CHECK_OFF(AHCI, f64BitAddr);
    GEN_CHECK_OFF(AHCI, fGCEnabled);
    GEN_CHECK_OFF(AHCI, fR0Enabled);
    GEN_CHECK_OFF(AHCI, fSignalIdle);
    GEN_CHECK_OFF(AHCI, fBootable);
    GEN_CHECK_OFF(AHCI, fLegacyPortResetMethod);
    GEN_CHECK_OFF(AHCI, cPortsImpl);
    GEN_CHECK_OFF(AHCI, cCmdSlotsAvail);
    GEN_CHECK_OFF(AHCI, f8ByteMMIO4BytesWrittenSuccessfully);
    GEN_CHECK_OFF(AHCI, pSupDrvSession);
#endif /* VBOX_WITH_AHCI */

#ifdef VBOX_WITH_E1000
    GEN_CHECK_SIZE(EEPROM93C46);
    GEN_CHECK_OFF(EEPROM93C46, m_eState);
    GEN_CHECK_OFF(EEPROM93C46, m_au16Data);
    GEN_CHECK_OFF(EEPROM93C46, m_fWriteEnabled);
    GEN_CHECK_OFF(EEPROM93C46, m_u16Word);
    GEN_CHECK_OFF(EEPROM93C46, m_u16Mask);
    GEN_CHECK_OFF(EEPROM93C46, m_u16Addr);
    GEN_CHECK_OFF(EEPROM93C46, m_u32InternalWires);
    GEN_CHECK_OFF(EEPROM93C46, m_eOp);

    GEN_CHECK_SIZE(E1KSTATE);
    GEN_CHECK_OFF(E1KSTATE, IBase);
    GEN_CHECK_OFF(E1KSTATE, INetworkDown);
    GEN_CHECK_OFF(E1KSTATE, INetworkConfig);
    GEN_CHECK_OFF(E1KSTATE, ILeds);
    GEN_CHECK_OFF(E1KSTATE, pDrvBase);
    GEN_CHECK_OFF(E1KSTATE, pDrvR3);
    GEN_CHECK_OFF(E1KSTATE, pDrvR0);
    GEN_CHECK_OFF(E1KSTATE, pDrvRC);
    GEN_CHECK_OFF(E1KSTATE, pLedsConnector);
    GEN_CHECK_OFF(E1KSTATE, pDevInsR3);
    GEN_CHECK_OFF(E1KSTATE, pDevInsR0);
    GEN_CHECK_OFF(E1KSTATE, pDevInsRC);
    GEN_CHECK_OFF(E1KSTATE, pTxQueueR3);
    GEN_CHECK_OFF(E1KSTATE, pTxQueueR0);
    GEN_CHECK_OFF(E1KSTATE, pTxQueueRC);
    GEN_CHECK_OFF(E1KSTATE, pCanRxQueueR3);
    GEN_CHECK_OFF(E1KSTATE, pCanRxQueueR0);
    GEN_CHECK_OFF(E1KSTATE, pCanRxQueueRC);
    GEN_CHECK_OFF(E1KSTATE, pRIDTimerR3);
    GEN_CHECK_OFF(E1KSTATE, pRIDTimerR0);
    GEN_CHECK_OFF(E1KSTATE, pRIDTimerRC);
    GEN_CHECK_OFF(E1KSTATE, pRADTimerR3);
    GEN_CHECK_OFF(E1KSTATE, pRADTimerR0);
    GEN_CHECK_OFF(E1KSTATE, pRADTimerRC);
    GEN_CHECK_OFF(E1KSTATE, pTIDTimerR3);
    GEN_CHECK_OFF(E1KSTATE, pTIDTimerR0);
    GEN_CHECK_OFF(E1KSTATE, pTIDTimerRC);
    GEN_CHECK_OFF(E1KSTATE, pTADTimerR3);
    GEN_CHECK_OFF(E1KSTATE, pTADTimerR0);
    GEN_CHECK_OFF(E1KSTATE, pTADTimerRC);
    GEN_CHECK_OFF(E1KSTATE, pIntTimerR3);
    GEN_CHECK_OFF(E1KSTATE, pIntTimerR0);
    GEN_CHECK_OFF(E1KSTATE, pIntTimerRC);
    GEN_CHECK_OFF(E1KSTATE, pLUTimerR3);
    GEN_CHECK_OFF(E1KSTATE, pLUTimerR0);
    GEN_CHECK_OFF(E1KSTATE, pLUTimerRC);
    GEN_CHECK_OFF(E1KSTATE, cs);
# ifndef E1K_GLOBAL_MUTEX
    GEN_CHECK_OFF(E1KSTATE, csRx);
# endif
    GEN_CHECK_OFF(E1KSTATE, addrMMReg);
    GEN_CHECK_OFF(E1KSTATE, macConfigured);
    GEN_CHECK_OFF(E1KSTATE, IOPortBase);
    GEN_CHECK_OFF(E1KSTATE, pciDevice);
    GEN_CHECK_OFF(E1KSTATE, u64AckedAt);
    GEN_CHECK_OFF(E1KSTATE, fIntRaised);
    GEN_CHECK_OFF(E1KSTATE, fCableConnected);
    GEN_CHECK_OFF(E1KSTATE, fR0Enabled);
    GEN_CHECK_OFF(E1KSTATE, fRCEnabled);
    GEN_CHECK_OFF(E1KSTATE, auRegs[E1K_NUM_OF_32BIT_REGS]);
    GEN_CHECK_OFF(E1KSTATE, led);
    GEN_CHECK_OFF(E1KSTATE, u32PktNo);
    GEN_CHECK_OFF(E1KSTATE, uSelectedReg);
    GEN_CHECK_OFF(E1KSTATE, auMTA[128]);
    GEN_CHECK_OFF(E1KSTATE, aRecAddr);
    GEN_CHECK_OFF(E1KSTATE, auVFTA[128]);
    GEN_CHECK_OFF(E1KSTATE, u16RxBSize);
    GEN_CHECK_OFF(E1KSTATE, fLocked);
    GEN_CHECK_OFF(E1KSTATE, fDelayInts);
    GEN_CHECK_OFF(E1KSTATE, fIntMaskUsed);
    GEN_CHECK_OFF(E1KSTATE, fMaybeOutOfSpace);
    GEN_CHECK_OFF(E1KSTATE, hEventMoreRxDescAvail);
    GEN_CHECK_OFF(E1KSTATE, contextTSE);
    GEN_CHECK_OFF(E1KSTATE, contextNormal);
# ifdef E1K_WITH_TXD_CACHE
    GEN_CHECK_OFF(E1KSTATE, aTxDescriptors);
    GEN_CHECK_OFF(E1KSTATE, nTxDFetched);
    GEN_CHECK_OFF(E1KSTATE, iTxDCurrent);
    GEN_CHECK_OFF(E1KSTATE, fGSO);
    GEN_CHECK_OFF(E1KSTATE, cbTxAlloc);
# endif
    GEN_CHECK_OFF(E1KSTATE, GsoCtx);
    GEN_CHECK_OFF(E1KSTATE, uTxFallback);
    GEN_CHECK_OFF(E1KSTATE, fVTag);
    GEN_CHECK_OFF(E1KSTATE, u16VTagTCI);
    GEN_CHECK_OFF(E1KSTATE, aTxPacketFallback[E1K_MAX_TX_PKT_SIZE]);
    GEN_CHECK_OFF(E1KSTATE, u16TxPktLen);
    GEN_CHECK_OFF(E1KSTATE, fIPcsum);
    GEN_CHECK_OFF(E1KSTATE, fTCPcsum);
    GEN_CHECK_OFF(E1KSTATE, u32PayRemain);
    GEN_CHECK_OFF(E1KSTATE, u16HdrRemain);
    GEN_CHECK_OFF(E1KSTATE, u16SavedFlags);
    GEN_CHECK_OFF(E1KSTATE, u32SavedCsum);
    GEN_CHECK_OFF(E1KSTATE, eeprom);
    GEN_CHECK_OFF(E1KSTATE, phy);
    GEN_CHECK_OFF(E1KSTATE, StatReceiveBytes);
#endif /* VBOX_WITH_E1000 */

    /* VMMDev*.cpp/h */
    GEN_CHECK_SIZE(VMMDEV);
    GEN_CHECK_OFF(VMMDEV, PciDev);
    GEN_CHECK_OFF(VMMDEV, CritSect);
    GEN_CHECK_OFF(VMMDEV, hypervisorSize);
    GEN_CHECK_OFF(VMMDEV, mouseCapabilities);
    GEN_CHECK_OFF(VMMDEV, mouseXAbs);
    GEN_CHECK_OFF(VMMDEV, mouseYAbs);
    GEN_CHECK_OFF(VMMDEV, fHostCursorRequested);
    GEN_CHECK_OFF(VMMDEV, pDevInsR3);
    GEN_CHECK_OFF(VMMDEV, pDevInsR0);
    GEN_CHECK_OFF(VMMDEV, pDevInsRC);
    GEN_CHECK_OFF(VMMDEV, IBase);
    GEN_CHECK_OFF(VMMDEV, IPort);
#ifdef VBOX_WITH_HGCM
    GEN_CHECK_OFF(VMMDEV, IHGCMPort);
#endif
    GEN_CHECK_OFF(VMMDEV, pDrvBase);
    GEN_CHECK_OFF(VMMDEV, pDrv);
#ifdef VBOX_WITH_HGCM
    GEN_CHECK_OFF(VMMDEV, pHGCMDrv);
#endif
    GEN_CHECK_OFF(VMMDEV, szMsg);
    GEN_CHECK_OFF(VMMDEV, iMsg);
    GEN_CHECK_OFF(VMMDEV, irq);
    GEN_CHECK_OFF(VMMDEV, u32HostEventFlags);
    GEN_CHECK_OFF(VMMDEV, u32GuestFilterMask);
    GEN_CHECK_OFF(VMMDEV, u32NewGuestFilterMask);
    GEN_CHECK_OFF(VMMDEV, fNewGuestFilterMask);
    GEN_CHECK_OFF(VMMDEV, GCPhysVMMDevRAM);
    GEN_CHECK_OFF(VMMDEV, pVMMDevRAMR3);
    GEN_CHECK_OFF(VMMDEV, pVMMDevRAMR0);
    GEN_CHECK_OFF(VMMDEV, pVMMDevRAMRC);
    GEN_CHECK_OFF(VMMDEV, pVMMDevHeapR3);
    GEN_CHECK_OFF(VMMDEV, GCPhysVMMDevHeap);
    GEN_CHECK_OFF(VMMDEV, guestInfo);
    GEN_CHECK_OFF(VMMDEV, guestCaps);
    GEN_CHECK_OFF(VMMDEV, fu32AdditionsOk);
    GEN_CHECK_OFF(VMMDEV, u32VideoAccelEnabled);
    GEN_CHECK_OFF(VMMDEV, displayChangeData);
    GEN_CHECK_OFF(VMMDEV, pCredentials);
    GEN_CHECK_OFF(VMMDEV, cMbMemoryBalloon);
    GEN_CHECK_OFF(VMMDEV, cMbMemoryBalloonLast);
    GEN_CHECK_OFF(VMMDEV, cbGuestRAM);
    GEN_CHECK_OFF(VMMDEV, idSession);
    GEN_CHECK_OFF(VMMDEV, u32StatIntervalSize);
    GEN_CHECK_OFF(VMMDEV, u32LastStatIntervalSize);
    GEN_CHECK_OFF(VMMDEV, fLastSeamlessEnabled),
    GEN_CHECK_OFF(VMMDEV, fSeamlessEnabled);
    GEN_CHECK_OFF(VMMDEV, fVRDPEnabled);
    GEN_CHECK_OFF(VMMDEV, uVRDPExperienceLevel);
#ifdef VMMDEV_WITH_ALT_TIMESYNC
    GEN_CHECK_OFF(VMMDEV, hostTime);
    GEN_CHECK_OFF(VMMDEV, fTimesyncBackdoorLo);
#endif
    GEN_CHECK_OFF(VMMDEV, fGetHostTimeDisabled);
    GEN_CHECK_OFF(VMMDEV, fBackdoorLogDisabled);
    GEN_CHECK_OFF(VMMDEV, fKeepCredentials);
    GEN_CHECK_OFF(VMMDEV, fHeapEnabled);
#ifdef VBOX_WITH_HGCM
    GEN_CHECK_OFF(VMMDEV, listHGCMCmd);
    GEN_CHECK_OFF(VMMDEV, critsectHGCMCmdList);
    GEN_CHECK_OFF(VMMDEV, u32HGCMEnabled);
#endif
    GEN_CHECK_OFF(VMMDEV, SharedFolders);
    GEN_CHECK_OFF(VMMDEV, SharedFolders.Led);
    GEN_CHECK_OFF(VMMDEV, SharedFolders.ILeds);
    GEN_CHECK_OFF(VMMDEV, SharedFolders.pLedsConnector);
    GEN_CHECK_OFF(VMMDEV, fCpuHotPlugEventsEnabled);
    GEN_CHECK_OFF(VMMDEV, enmCpuHotPlugEvent);
    GEN_CHECK_OFF(VMMDEV, idCpuCore);
    GEN_CHECK_OFF(VMMDEV, idCpuPackage);
    GEN_CHECK_OFF(VMMDEV, StatMemBalloonChunks);
    GEN_CHECK_OFF(VMMDEV, fRZEnabled);
    GEN_CHECK_OFF(VMMDEV, fTestingEnabled);
    GEN_CHECK_OFF(VMMDEV, fTestingMMIO);
    GEN_CHECK_OFF(VMMDEV, u32TestingHighTimestamp);
    GEN_CHECK_OFF(VMMDEV, u32TestingCmd);
    GEN_CHECK_OFF(VMMDEV, offTestingData);
    GEN_CHECK_OFF(VMMDEV, TestingData);
    GEN_CHECK_OFF(VMMDEV, TestingData.Value.u64Value);
    GEN_CHECK_OFF(VMMDEV, TestingData.Value.u32Unit);
    GEN_CHECK_OFF(VMMDEV, TestingData.Value.szName);
    GEN_CHECK_OFF(VMMDEV, nsLastHeartbeatTS);
    GEN_CHECK_OFF(VMMDEV, fFlatlined);
    GEN_CHECK_OFF(VMMDEV, fHeartbeatActive);
    GEN_CHECK_OFF(VMMDEV, cNsHeartbeatInterval);
    GEN_CHECK_OFF(VMMDEV, cNsHeartbeatTimeout);
    GEN_CHECK_OFF(VMMDEV, pFlatlinedTimer);

#ifdef VBOX_WITH_BUSLOGIC
    GEN_CHECK_SIZE(BUSLOGICDEVICE);
    GEN_CHECK_OFF(BUSLOGICDEVICE, pBusLogicR3);
    GEN_CHECK_OFF(BUSLOGICDEVICE, pBusLogicR0);
    GEN_CHECK_OFF(BUSLOGICDEVICE, pBusLogicRC);
    GEN_CHECK_OFF(BUSLOGICDEVICE, fPresent);
    GEN_CHECK_OFF(BUSLOGICDEVICE, iLUN);
    GEN_CHECK_OFF(BUSLOGICDEVICE, IBase);
    GEN_CHECK_OFF(BUSLOGICDEVICE, IMediaPort);
    GEN_CHECK_OFF(BUSLOGICDEVICE, IMediaExPort);
    GEN_CHECK_OFF(BUSLOGICDEVICE, ILed);
    GEN_CHECK_OFF(BUSLOGICDEVICE, pDrvBase);
    GEN_CHECK_OFF(BUSLOGICDEVICE, pDrvMedia);
    GEN_CHECK_OFF(BUSLOGICDEVICE, pDrvMediaEx);
    GEN_CHECK_OFF(BUSLOGICDEVICE, Led);
    GEN_CHECK_OFF(BUSLOGICDEVICE, cOutstandingRequests);

    GEN_CHECK_SIZE(BUSLOGIC);
    GEN_CHECK_OFF(BUSLOGIC, dev);
    GEN_CHECK_OFF(BUSLOGIC, pDevInsR3);
    GEN_CHECK_OFF(BUSLOGIC, pDevInsR0);
    GEN_CHECK_OFF(BUSLOGIC, pDevInsRC);
    GEN_CHECK_OFF(BUSLOGIC, IOPortBase);
    GEN_CHECK_OFF(BUSLOGIC, MMIOBase);
    GEN_CHECK_OFF(BUSLOGIC, regStatus);
    GEN_CHECK_OFF(BUSLOGIC, regInterrupt);
    GEN_CHECK_OFF(BUSLOGIC, regGeometry);
    GEN_CHECK_OFF(BUSLOGIC, LocalRam);
    GEN_CHECK_OFF(BUSLOGIC, uOperationCode);
    GEN_CHECK_OFF(BUSLOGIC, aCommandBuffer);
    GEN_CHECK_OFF(BUSLOGIC, aCommandBuffer[BUSLOGIC_COMMAND_SIZE_MAX]);
    GEN_CHECK_OFF(BUSLOGIC, iParameter);
    GEN_CHECK_OFF(BUSLOGIC, cbCommandParametersLeft);
    GEN_CHECK_OFF(BUSLOGIC, fUseLocalRam);
    GEN_CHECK_OFF(BUSLOGIC, aReplyBuffer);
    GEN_CHECK_OFF(BUSLOGIC, aReplyBuffer[BUSLOGIC_REPLY_SIZE_MAX]);
    GEN_CHECK_OFF(BUSLOGIC, iReply);
    GEN_CHECK_OFF(BUSLOGIC, cbReplyParametersLeft);
    GEN_CHECK_OFF(BUSLOGIC, fIRQEnabled);
    GEN_CHECK_OFF(BUSLOGIC, cMailbox);
    GEN_CHECK_OFF(BUSLOGIC, GCPhysAddrMailboxOutgoingBase);
    GEN_CHECK_OFF(BUSLOGIC, uMailboxOutgoingPositionCurrent);
    GEN_CHECK_OFF(BUSLOGIC, cMailboxesReady);
    GEN_CHECK_OFF(BUSLOGIC, fNotificationSent);
    GEN_CHECK_OFF(BUSLOGIC, GCPhysAddrMailboxIncomingBase);
    GEN_CHECK_OFF(BUSLOGIC, uMailboxIncomingPositionCurrent);
    GEN_CHECK_OFF(BUSLOGIC, fStrictRoundRobinMode);
    GEN_CHECK_OFF(BUSLOGIC, fExtendedLunCCBFormat);
    GEN_CHECK_OFF(BUSLOGIC, pNotifierQueueR3);
    GEN_CHECK_OFF(BUSLOGIC, pNotifierQueueR0);
    GEN_CHECK_OFF(BUSLOGIC, pNotifierQueueRC);
    GEN_CHECK_OFF(BUSLOGIC, CritSectIntr);
    GEN_CHECK_OFF(BUSLOGIC, aDeviceStates);
    GEN_CHECK_OFF(BUSLOGIC, aDeviceStates[BUSLOGIC_MAX_DEVICES-1]);
    GEN_CHECK_OFF(BUSLOGIC, IBase);
    GEN_CHECK_OFF(BUSLOGIC, ILeds);
    GEN_CHECK_OFF(BUSLOGIC, pLedsConnector);
    GEN_CHECK_OFF(BUSLOGIC, pMediaNotify);
    GEN_CHECK_OFF(BUSLOGIC, fSignalIdle);
    GEN_CHECK_OFF(BUSLOGIC, fWrkThreadSleeping);
    GEN_CHECK_OFF(BUSLOGIC, pSupDrvSession);
    GEN_CHECK_OFF(BUSLOGIC, hEvtProcess);
    GEN_CHECK_OFF(BUSLOGIC, paGCPhysAddrCCBRedo);
    GEN_CHECK_OFF(BUSLOGIC, cReqsRedo);
# ifdef LOG_ENABLED
    GEN_CHECK_OFF(BUSLOGIC, cInMailboxesReady);
# endif
#endif /* VBOX_WITH_BUSLOGIC */

#ifdef VBOX_WITH_LSILOGIC
    GEN_CHECK_SIZE(LSILOGICSCSI);
    GEN_CHECK_OFF(LSILOGICSCSI, PciDev);
    GEN_CHECK_OFF(LSILOGICSCSI, pDevInsR3);
    GEN_CHECK_OFF(LSILOGICSCSI, pDevInsR0);
    GEN_CHECK_OFF(LSILOGICSCSI, pDevInsRC);
    GEN_CHECK_OFF(LSILOGICSCSI, fGCEnabled);
    GEN_CHECK_OFF(LSILOGICSCSI, fR0Enabled);
    GEN_CHECK_OFF(LSILOGICSCSI, enmState);
    GEN_CHECK_OFF(LSILOGICSCSI, enmWhoInit);
    GEN_CHECK_OFF(LSILOGICSCSI, enmDoorbellState);
    GEN_CHECK_OFF(LSILOGICSCSI, fDiagnosticEnabled);
    GEN_CHECK_OFF(LSILOGICSCSI, fNotificationSent);
    GEN_CHECK_OFF(LSILOGICSCSI, fEventNotificationEnabled);
    GEN_CHECK_OFF(LSILOGICSCSI, fDiagRegsEnabled);
    GEN_CHECK_OFF(LSILOGICSCSI, pNotificationQueueR3);
    GEN_CHECK_OFF(LSILOGICSCSI, pNotificationQueueR0);
    GEN_CHECK_OFF(LSILOGICSCSI, pNotificationQueueRC);
    GEN_CHECK_OFF(LSILOGICSCSI, cDeviceStates);
    GEN_CHECK_OFF(LSILOGICSCSI, paDeviceStates);
    GEN_CHECK_OFF(LSILOGICSCSI, GCPhysMMIOBase);
    GEN_CHECK_OFF(LSILOGICSCSI, IOPortBase);
    GEN_CHECK_OFF(LSILOGICSCSI, uInterruptMask);
    GEN_CHECK_OFF(LSILOGICSCSI, uInterruptStatus);
    GEN_CHECK_OFF(LSILOGICSCSI, aMessage);
    GEN_CHECK_OFF(LSILOGICSCSI, aMessage[sizeof(MptConfigurationRequest)-1]);
    GEN_CHECK_OFF(LSILOGICSCSI, iMessage);
    GEN_CHECK_OFF(LSILOGICSCSI, cMessage);
    GEN_CHECK_OFF(LSILOGICSCSI, ReplyBuffer);
    GEN_CHECK_OFF(LSILOGICSCSI, uNextReplyEntryRead);
    GEN_CHECK_OFF(LSILOGICSCSI, cReplySize);
    GEN_CHECK_OFF(LSILOGICSCSI, u16IOCFaultCode);
    GEN_CHECK_OFF(LSILOGICSCSI, u32HostMFAHighAddr);
    GEN_CHECK_OFF(LSILOGICSCSI, u32SenseBufferHighAddr);
    GEN_CHECK_OFF(LSILOGICSCSI, cMaxDevices);
    GEN_CHECK_OFF(LSILOGICSCSI, cMaxBuses);
    GEN_CHECK_OFF(LSILOGICSCSI, cbReplyFrame);
    GEN_CHECK_OFF(LSILOGICSCSI, iDiagnosticAccess);
    GEN_CHECK_OFF(LSILOGICSCSI, cReplyQueueEntries);
    GEN_CHECK_OFF(LSILOGICSCSI, cRequestQueueEntries);
    GEN_CHECK_OFF(LSILOGICSCSI, ReplyPostQueueCritSect);
    GEN_CHECK_OFF(LSILOGICSCSI, ReplyFreeQueueCritSect);
    GEN_CHECK_OFF(LSILOGICSCSI, RequestQueueCritSect);
    GEN_CHECK_OFF(LSILOGICSCSI, ReplyFreeQueueWriteCritSect);
    GEN_CHECK_OFF(LSILOGICSCSI, pReplyFreeQueueBaseR3);
    GEN_CHECK_OFF(LSILOGICSCSI, pReplyPostQueueBaseR3);
    GEN_CHECK_OFF(LSILOGICSCSI, pRequestQueueBaseR3);
    GEN_CHECK_OFF(LSILOGICSCSI, pReplyFreeQueueBaseR0);
    GEN_CHECK_OFF(LSILOGICSCSI, pReplyPostQueueBaseR0);
    GEN_CHECK_OFF(LSILOGICSCSI, pRequestQueueBaseR0);
    GEN_CHECK_OFF(LSILOGICSCSI, pReplyFreeQueueBaseRC);
    GEN_CHECK_OFF(LSILOGICSCSI, pReplyPostQueueBaseRC);
    GEN_CHECK_OFF(LSILOGICSCSI, pRequestQueueBaseRC);
    GEN_CHECK_OFF(LSILOGICSCSI, uReplyFreeQueueNextEntryFreeWrite);
    GEN_CHECK_OFF(LSILOGICSCSI, uReplyFreeQueueNextAddressRead);
    GEN_CHECK_OFF(LSILOGICSCSI, uReplyPostQueueNextEntryFreeWrite);
    GEN_CHECK_OFF(LSILOGICSCSI, uReplyPostQueueNextAddressRead);
    GEN_CHECK_OFF(LSILOGICSCSI, uRequestQueueNextEntryFreeWrite);
    GEN_CHECK_OFF(LSILOGICSCSI, uRequestQueueNextAddressRead);
    GEN_CHECK_OFF(LSILOGICSCSI, u16NextHandle);
    GEN_CHECK_OFF(LSILOGICSCSI, enmCtrlType);
    GEN_CHECK_OFF(LSILOGICSCSI, IBase);
    GEN_CHECK_OFF(LSILOGICSCSI, ILeds);
    GEN_CHECK_OFF(LSILOGICSCSI, pLedsConnector);
    GEN_CHECK_OFF(LSILOGICSCSI, pMediaNotify);
    GEN_CHECK_OFF(LSILOGICSCSI, pConfigurationPages);
    GEN_CHECK_OFF(LSILOGICSCSI, fSignalIdle);
    GEN_CHECK_OFF(LSILOGICSCSI, fRedo);
    GEN_CHECK_OFF(LSILOGICSCSI, fWrkThreadSleeping);
    GEN_CHECK_OFF(LSILOGICSCSI, pTasksRedoHead);
    GEN_CHECK_OFF(LSILOGICSCSI, u32DiagMemAddr);
    GEN_CHECK_OFF(LSILOGICSCSI, cbMemRegns);
    GEN_CHECK_OFF(LSILOGICSCSI, ListMemRegns);
    GEN_CHECK_OFF(LSILOGICSCSI, pSupDrvSession);
    GEN_CHECK_OFF(LSILOGICSCSI, pThreadWrk);
    GEN_CHECK_OFF(LSILOGICSCSI, hEvtProcess);
#endif /* VBOX_WITH_LSILOGIC */

    GEN_CHECK_SIZE(HPET);
    GEN_CHECK_OFF(HPET, pDevInsR3);
    GEN_CHECK_OFF(HPET, pDevInsR0);
    GEN_CHECK_OFF(HPET, pDevInsRC);
    GEN_CHECK_OFF(HPET, u64HpetOffset);
    GEN_CHECK_OFF(HPET, u32Capabilities);
    GEN_CHECK_OFF(HPET, u32Period);
    GEN_CHECK_OFF(HPET, u64HpetConfig);
    GEN_CHECK_OFF(HPET, u64Isr);
    GEN_CHECK_OFF(HPET, u64HpetCounter);
    GEN_CHECK_OFF(HPET, CritSect);
    GEN_CHECK_OFF(HPET, fIch9);

    GEN_CHECK_SIZE(HPETTIMER);
    GEN_CHECK_OFF(HPETTIMER, pTimerR3);
    GEN_CHECK_OFF(HPETTIMER, pHpetR3);
    GEN_CHECK_OFF(HPETTIMER, pTimerR0);
    GEN_CHECK_OFF(HPETTIMER, pHpetR0);
    GEN_CHECK_OFF(HPETTIMER, pTimerRC);
    GEN_CHECK_OFF(HPETTIMER, pHpetRC);
    GEN_CHECK_OFF(HPETTIMER, idxTimer);
    GEN_CHECK_OFF(HPETTIMER, u64Config);
    GEN_CHECK_OFF(HPETTIMER, u64Cmp);
    GEN_CHECK_OFF(HPETTIMER, u64Fsb);
    GEN_CHECK_OFF(HPETTIMER, u64Period);
    GEN_CHECK_OFF(HPETTIMER, u8Wrap);

    GEN_CHECK_SIZE(AC97DRIVER);
    GEN_CHECK_OFF(AC97DRIVER, Node);
    GEN_CHECK_OFF(AC97DRIVER, pAC97State);
    GEN_CHECK_OFF(AC97DRIVER, fFlags);
    GEN_CHECK_OFF(AC97DRIVER, uLUN);
    GEN_CHECK_OFF(AC97DRIVER, fAttached);
    GEN_CHECK_OFF(AC97DRIVER, pConnector);
    GEN_CHECK_OFF(AC97DRIVER, LineIn);
    GEN_CHECK_OFF(AC97DRIVER, MicIn);
    GEN_CHECK_OFF(AC97DRIVER, Out);

    GEN_CHECK_SIZE(AC97STATE);
    GEN_CHECK_OFF(AC97STATE, CritSect);
    GEN_CHECK_OFF(AC97STATE, pDevInsR3);
    GEN_CHECK_OFF(AC97STATE, pDevInsR0);
    GEN_CHECK_OFF(AC97STATE, pDevInsRC);
    GEN_CHECK_OFF(AC97STATE, fRZEnabled);
    GEN_CHECK_OFF(AC97STATE, glob_cnt);
    GEN_CHECK_OFF(AC97STATE, glob_sta);
    GEN_CHECK_OFF(AC97STATE, cas);
    GEN_CHECK_OFF(AC97STATE, last_samp);
    GEN_CHECK_OFF(AC97STATE, mixer_data);
    GEN_CHECK_OFF(AC97STATE, aStreams);
    GEN_CHECK_OFF(AC97STATE, uTimerHz);
    GEN_CHECK_OFF(AC97STATE, pTimerRC);
    GEN_CHECK_OFF(AC97STATE, pTimerR3);
    GEN_CHECK_OFF(AC97STATE, pTimerR0);
#ifdef VBOX_WITH_STATISTICS
    GEN_CHECK_OFF(AC97STATE, StatTimer);
    GEN_CHECK_OFF(AC97STATE, StatIn);
    GEN_CHECK_OFF(AC97STATE, StatOut);
    GEN_CHECK_OFF(AC97STATE, StatBytesRead);
    GEN_CHECK_OFF(AC97STATE, StatBytesWritten);
#endif
    GEN_CHECK_OFF(AC97STATE, lstDrv);
    GEN_CHECK_OFF(AC97STATE, pMixer);
    GEN_CHECK_OFF(AC97STATE, pSinkOut);
    GEN_CHECK_OFF(AC97STATE, pSinkLineIn);
    GEN_CHECK_OFF(AC97STATE, pSinkMicIn);
    GEN_CHECK_OFF(AC97STATE, silence);
    GEN_CHECK_OFF(AC97STATE, bup_flag);
    GEN_CHECK_OFF(AC97STATE, IBase);
    GEN_CHECK_OFF(AC97STATE, IOPortBase);
    GEN_CHECK_OFF(AC97STATE, uCodecModel);

    GEN_CHECK_SIZE(AC97STREAM);
    GEN_CHECK_OFF(AC97STREAM, pAC97State);

    GEN_CHECK_SIZE(AC97STREAMSTATE);
    GEN_CHECK_OFF(AC97STREAMSTATE, CritSect);
    GEN_CHECK_OFF(AC97STREAMSTATE, pCircBuf);
    GEN_CHECK_OFF(AC97STREAMSTATE, tsTransferLast);

    GEN_CHECK_SIZE(HDADRIVERSTREAM);
    GEN_CHECK_OFF(HDADRIVERSTREAM, pMixStrm);

    GEN_CHECK_SIZE(HDADRIVER);
    GEN_CHECK_OFF(HDADRIVER, Node);
    GEN_CHECK_OFF(HDADRIVER, pHDAState);
    GEN_CHECK_OFF(HDADRIVER, fFlags);
    GEN_CHECK_OFF(HDADRIVER, uLUN);
    GEN_CHECK_OFF(HDADRIVER, fAttached);
    GEN_CHECK_OFF(HDADRIVER, pConnector);
    GEN_CHECK_OFF(HDADRIVER, LineIn);
#ifdef VBOX_WITH_HDA_MIC_IN
    GEN_CHECK_OFF(HDADRIVER, MicIn);
#endif
    GEN_CHECK_OFF(HDADRIVER, Front);
#ifdef VBOX_WITH_HDA_51_SURROUND
    GEN_CHECK_OFF(HDADRIVER, CenterLFE);
    GEN_CHECK_OFF(HDADRIVER, Rear);
#endif

    GEN_CHECK_SIZE(HDABDLESTATE);
    GEN_CHECK_OFF(HDABDLESTATE, u32BDLIndex);
    GEN_CHECK_OFF(HDABDLESTATE, cbBelowFIFOW);
    GEN_CHECK_OFF(HDABDLESTATE, u32BufOff);

    GEN_CHECK_SIZE(HDABDLEDESC);
    GEN_CHECK_OFF(HDABDLEDESC, u64BufAddr);
    GEN_CHECK_OFF(HDABDLEDESC, u32BufSize);
    GEN_CHECK_OFF(HDABDLEDESC, fFlags);

    GEN_CHECK_SIZE(HDABDLE);
    GEN_CHECK_OFF(HDABDLE, Desc);
    GEN_CHECK_OFF(HDABDLE, State);

    GEN_CHECK_SIZE(HDASTREAMMAP);

    GEN_CHECK_SIZE(HDASTREAMSTATE);
    GEN_CHECK_OFF(HDASTREAMSTATE, uCurBDLE);
    GEN_CHECK_OFF(HDASTREAMSTATE, fInReset);
    GEN_CHECK_OFF(HDASTREAMSTATE, Mapping);
    GEN_CHECK_OFF(HDASTREAMSTATE, BDLE);
    GEN_CHECK_OFF(HDASTREAMSTATE, pCircBuf);

    GEN_CHECK_SIZE(HDASTREAMDBGINFORT);

    GEN_CHECK_SIZE(HDASTREAMDBGINFO);
    GEN_CHECK_OFF(HDASTREAMDBGINFO, Runtime);

    GEN_CHECK_SIZE(HDASTREAM);
    GEN_CHECK_OFF(HDASTREAM, u8SD);
    GEN_CHECK_OFF(HDASTREAM, u64BDLBase);
    GEN_CHECK_OFF(HDASTREAM, u16FMT);
    GEN_CHECK_OFF(HDASTREAM, u16FIFOS);
    GEN_CHECK_OFF(HDASTREAM, u16LVI);
    GEN_CHECK_OFF(HDASTREAM, State);
    GEN_CHECK_OFF(HDASTREAM, Dbg);
    GEN_CHECK_OFF(HDASTREAM, CritSect);

    GEN_CHECK_SIZE(HDASTATE);
    GEN_CHECK_OFF(HDASTATE, PciDev);
    GEN_CHECK_OFF(HDASTATE, pDevInsR3);
    GEN_CHECK_OFF(HDASTATE, pDevInsR0);
    GEN_CHECK_OFF(HDASTATE, pDevInsRC);
    GEN_CHECK_OFF(HDASTATE, IBase);
    GEN_CHECK_OFF(HDASTATE, MMIOBaseAddr);
    GEN_CHECK_OFF(HDASTATE, au32Regs[0]);
    GEN_CHECK_OFF(HDASTATE, au32Regs[HDA_NUM_REGS]);
    GEN_CHECK_OFF(HDASTATE, aStreams);
    GEN_CHECK_OFF(HDASTATE, aTags);
    GEN_CHECK_OFF(HDASTATE, u64CORBBase);
    GEN_CHECK_OFF(HDASTATE, u64RIRBBase);
    GEN_CHECK_OFF(HDASTATE, u64DPBase);
    GEN_CHECK_OFF(HDASTATE, pu32CorbBuf);
    GEN_CHECK_OFF(HDASTATE, cbCorbBuf);
    GEN_CHECK_OFF(HDASTATE, pu64RirbBuf);
    GEN_CHECK_OFF(HDASTATE, cbRirbBuf);
    GEN_CHECK_OFF(HDASTATE, fRZEnabled);
#ifdef VBOX_WITH_STATISTICS
# ifndef VBOX_WITH_AUDIO_CALLBACKS
    GEN_CHECK_OFF(HDASTATE, StatTimer);
# endif
    GEN_CHECK_OFF(HDASTATE, StatBytesRead);
    GEN_CHECK_OFF(HDASTATE, StatBytesWritten);
#endif /* VBOX_WITH_STATISTICS */
    GEN_CHECK_OFF(HDASTATE, pCodec);
    GEN_CHECK_OFF(HDASTATE, lstDrv);
    GEN_CHECK_OFF(HDASTATE, pMixer);
    GEN_CHECK_OFF(HDASTATE, SinkFront);
#ifdef VBOX_WITH_HDA_51_SURROUND
    GEN_CHECK_OFF(HDASTATE, SinkCenterLFE);
    GEN_CHECK_OFF(HDASTATE, SinkRear);
#endif
    GEN_CHECK_OFF(HDASTATE, SinkLineIn);
#ifdef VBOX_WITH_HDA_MIC_IN
    GEN_CHECK_OFF(HDASTATE, SinkMicIn);
#endif
    GEN_CHECK_OFF(HDASTATE, u64WalClk);
    GEN_CHECK_OFF(HDASTATE, u16RespIntCnt);
    GEN_CHECK_OFF(HDASTATE, cPosAdjustFrames);
    GEN_CHECK_OFF(HDASTATE, u8IRQL);

#ifdef VBOX_WITH_NVME_IMPL
    GEN_CHECK_SIZE(NVMEQUEUEHDR);
    GEN_CHECK_OFF(NVMEQUEUEHDR, u16Id);
    GEN_CHECK_OFF(NVMEQUEUEHDR, cEntries);
    GEN_CHECK_OFF(NVMEQUEUEHDR, enmState);
    GEN_CHECK_OFF(NVMEQUEUEHDR, GCPhysBase);
    GEN_CHECK_OFF(NVMEQUEUEHDR, cbEntry);
    GEN_CHECK_OFF(NVMEQUEUEHDR, idxHead);
    GEN_CHECK_OFF(NVMEQUEUEHDR, idxTail);
    GEN_CHECK_OFF(NVMEQUEUEHDR, fPhysCont);
    GEN_CHECK_OFF(NVMEQUEUEHDR, enmType);

    GEN_CHECK_SIZE(NVMEQUEUESUBM);
    GEN_CHECK_OFF(NVMEQUEUESUBM, Hdr);
    GEN_CHECK_OFF(NVMEQUEUESUBM, u16CompletionQueueId);
    GEN_CHECK_OFF(NVMEQUEUESUBM, enmPriority);
    GEN_CHECK_OFF(NVMEQUEUESUBM, hEvtProcess);
    GEN_CHECK_OFF(NVMEQUEUESUBM, pWrkThrdR3);
    GEN_CHECK_OFF(NVMEQUEUESUBM, NdLstWrkThrdAssgnd);
    GEN_CHECK_OFF(NVMEQUEUESUBM, cReqsActive);

    GEN_CHECK_SIZE(NVMEQUEUECOMP);
    GEN_CHECK_OFF(NVMEQUEUECOMP, Hdr);
    GEN_CHECK_OFF(NVMEQUEUECOMP, fIntrEnabled);
    GEN_CHECK_OFF(NVMEQUEUECOMP, u32IntrVec);
    GEN_CHECK_OFF(NVMEQUEUECOMP, cSubmQueuesRef);
    GEN_CHECK_OFF(NVMEQUEUECOMP, cWaiters);
    GEN_CHECK_OFF(NVMEQUEUECOMP, LstCompletionsWaiting);
    GEN_CHECK_OFF(NVMEQUEUECOMP, hMtx);

    GEN_CHECK_SIZE(NVME);
    GEN_CHECK_OFF(NVME, PciDev);
    GEN_CHECK_OFF(NVME, pDevInsR3);
    GEN_CHECK_OFF(NVME, pDevInsR0);
    GEN_CHECK_OFF(NVME, pDevInsRC);
    GEN_CHECK_OFF(NVME, IBase);
    GEN_CHECK_OFF(NVME, ILeds);
    GEN_CHECK_OFF(NVME, pLedsConnector);
    GEN_CHECK_OFF(NVME, pSupDrvSession);
    GEN_CHECK_OFF(NVME, GCPhysMMIO);
    GEN_CHECK_OFF(NVME, IOPortBase);
    GEN_CHECK_OFF(NVME, cQueuesSubmMax);
    GEN_CHECK_OFF(NVME, cQueuesCompMax);
    GEN_CHECK_OFF(NVME, cQueueEntriesMax);
    GEN_CHECK_OFF(NVME, cTimeoutMax);
    GEN_CHECK_OFF(NVME, cWrkThrdsMax);
    GEN_CHECK_OFF(NVME, cCompQueuesWaitersMax);
    GEN_CHECK_OFF(NVME, cNamespaces);
    GEN_CHECK_OFF(NVME, szSerialNumber);
    GEN_CHECK_OFF(NVME, szModelNumber);
    GEN_CHECK_OFF(NVME, szFirmwareRevision);
    GEN_CHECK_OFF(NVME, fRCEnabled);
    GEN_CHECK_OFF(NVME, fR0Enabled);
    GEN_CHECK_OFF(NVME, enmState);
    GEN_CHECK_OFF(NVME, u32IntrMask);
    GEN_CHECK_OFF(NVME, aIntrVecs);
    GEN_CHECK_OFF(NVME, u32IoCompletionQueueEntrySize);
    GEN_CHECK_OFF(NVME, u32IoSubmissionQueueEntrySize);
    GEN_CHECK_OFF(NVME, uShutdwnNotifierLast);
    GEN_CHECK_OFF(NVME, uAmsSet);
    GEN_CHECK_OFF(NVME, uMpsSet);
    GEN_CHECK_OFF(NVME, uCssSet);
    GEN_CHECK_OFF(NVME, u32RegIdx);
    GEN_CHECK_OFF(NVME, cbPage);
    GEN_CHECK_OFF(NVME, paQueuesSubmR3);
    GEN_CHECK_OFF(NVME, paQueuesCompR3);
    GEN_CHECK_OFF(NVME, paQueuesSubmR0);
    GEN_CHECK_OFF(NVME, paQueuesCompR0);
    GEN_CHECK_OFF(NVME, paQueuesSubmRC);
    GEN_CHECK_OFF(NVME, paQueuesCompRC);
    GEN_CHECK_OFF(NVME, pvCtrlMemBufR3);
    GEN_CHECK_OFF(NVME, GCPhysCtrlMemBuf);
    GEN_CHECK_OFF(NVME, cbCtrlMemBuf);
    GEN_CHECK_OFF(NVME, u32CtrlMemBufSz);
    GEN_CHECK_OFF(NVME, pWakeQueueR3);
    GEN_CHECK_OFF(NVME, pWakeQueueR0);
    GEN_CHECK_OFF(NVME, pWakeQueueRC);
    GEN_CHECK_OFF(NVME, cAsyncEvtReqsMax);
    GEN_CHECK_OFF(NVME, CritSectAsyncEvtReqs);
    GEN_CHECK_OFF(NVME, paAsyncEvtReqCids);
    GEN_CHECK_OFF(NVME, paNamespaces);
    GEN_CHECK_OFF(NVME, cWrkThrdsCur);
    GEN_CHECK_OFF(NVME, cWrkThrdsActive);
    GEN_CHECK_OFF(NVME, LstWrkThrds);
    GEN_CHECK_OFF(NVME, CritSectWrkThrds);
    GEN_CHECK_OFF(NVME, fSignalIdle);
# ifdef VBOX_WITH_STATISTICS
    GEN_CHECK_OFF(NVME, aStatMemXfer[0]);
    GEN_CHECK_OFF(NVME, aStatMemXfer[NVME_CMBSZ_SUPP_BIT_IDX_MAX]);
# endif
#endif

#ifdef VBOX_WITH_IOMMU_AMD
# ifdef IOMMU_WITH_DTE_CACHE
    GEN_CHECK_SIZE(DTECACHE);
# endif
# ifdef IOMMU_WITH_DTE_CACHE
    GEN_CHECK_SIZE(IRTECACHE);
# endif
    GEN_CHECK_SIZE(IOMMU);
    GEN_CHECK_OFF(IOMMU, idxIommu);
    GEN_CHECK_OFF(IOMMU, u32Magic);
    GEN_CHECK_OFF(IOMMU, hMmio);
    GEN_CHECK_OFF(IOMMU, hEvtCmdThread);
    GEN_CHECK_OFF(IOMMU, fCmdThreadSignaled);
# ifdef IOMMU_WITH_DTE_CACHE
    GEN_CHECK_OFF(IOMMU, CritSectCache);
    GEN_CHECK_OFF(IOMMU, aDeviceIds);
    GEN_CHECK_OFF(IOMMU, aDteCache);
# endif
# ifdef IOMMU_WITH_IRTE_CACHE
    GEN_CHECK_OFF(IOMMU, aIrteCache);
# endif
    GEN_CHECK_OFF(IOMMU, IommuBar);
    GEN_CHECK_OFF(IOMMU, aDevTabBaseAddrs);
    GEN_CHECK_OFF(IOMMU, CmdBufBaseAddr);
    GEN_CHECK_OFF(IOMMU, EvtLogBaseAddr);
    GEN_CHECK_OFF(IOMMU, Ctrl);
    GEN_CHECK_OFF(IOMMU, ExclRangeBaseAddr);
    GEN_CHECK_OFF(IOMMU, ExclRangeLimit);
    GEN_CHECK_OFF(IOMMU, ExtFeat);
    GEN_CHECK_OFF(IOMMU, PprLogBaseAddr);
    GEN_CHECK_OFF(IOMMU, HwEvtHi);
    GEN_CHECK_OFF(IOMMU, HwEvtLo);
    GEN_CHECK_OFF(IOMMU, HwEvtStatus);
    GEN_CHECK_OFF(IOMMU, GALogBaseAddr);
    GEN_CHECK_OFF(IOMMU, GALogTailAddr);
    GEN_CHECK_OFF(IOMMU, PprLogBBaseAddr);
    GEN_CHECK_OFF(IOMMU, EvtLogBBaseAddr);
    GEN_CHECK_OFF(IOMMU, DevSpecificFeat);
    GEN_CHECK_OFF(IOMMU, DevSpecificCtrl);
    GEN_CHECK_OFF(IOMMU, DevSpecificStatus);
    GEN_CHECK_OFF(IOMMU, MiscInfo);
    GEN_CHECK_OFF(IOMMU, PerfOptCtrl);
    GEN_CHECK_OFF(IOMMU, XtGenIntrCtrl);
    GEN_CHECK_OFF(IOMMU, XtPprIntrCtrl);
    GEN_CHECK_OFF(IOMMU, XtGALogIntrCtrl);
    GEN_CHECK_OFF(IOMMU, aMarcApers);
    GEN_CHECK_OFF(IOMMU, RsvdReg);
    GEN_CHECK_OFF(IOMMU, CmdBufHeadPtr);
    GEN_CHECK_OFF(IOMMU, CmdBufTailPtr);
    GEN_CHECK_OFF(IOMMU, EvtLogHeadPtr);
    GEN_CHECK_OFF(IOMMU, EvtLogTailPtr);
    GEN_CHECK_OFF(IOMMU, Status);
    GEN_CHECK_OFF(IOMMU, PprLogHeadPtr);
    GEN_CHECK_OFF(IOMMU, PprLogTailPtr);
    GEN_CHECK_OFF(IOMMU, GALogHeadPtr);
    GEN_CHECK_OFF(IOMMU, GALogTailPtr);
    GEN_CHECK_OFF(IOMMU, PprLogBHeadPtr);
    GEN_CHECK_OFF(IOMMU, PprLogBTailPtr);
    GEN_CHECK_OFF(IOMMU, EvtLogBHeadPtr);
    GEN_CHECK_OFF(IOMMU, EvtLogBTailPtr);
    GEN_CHECK_OFF(IOMMU, PprLogAutoResp);
    GEN_CHECK_OFF(IOMMU, PprLogOverflowEarly);
    GEN_CHECK_OFF(IOMMU, PprLogBOverflowEarly);
# ifdef VBOX_WITH_STATISTICS
    GEN_CHECK_OFF(IOMMU, StatMmioReadR3);
    GEN_CHECK_OFF(IOMMU, StatMmioReadRZ);
    GEN_CHECK_OFF(IOMMU, StatMmioWriteR3);
    GEN_CHECK_OFF(IOMMU, StatMmioWriteRZ);
    GEN_CHECK_OFF(IOMMU, StatMsiRemapR3);
    GEN_CHECK_OFF(IOMMU, StatMsiRemapRZ);
    GEN_CHECK_OFF(IOMMU, StatMemReadR3);
    GEN_CHECK_OFF(IOMMU, StatMemReadRZ);
    GEN_CHECK_OFF(IOMMU, StatMemWriteR3);
    GEN_CHECK_OFF(IOMMU, StatMemWriteRZ);
    GEN_CHECK_OFF(IOMMU, StatMemBulkReadR3);
    GEN_CHECK_OFF(IOMMU, StatMemBulkReadRZ);
    GEN_CHECK_OFF(IOMMU, StatMemBulkWriteR3);
    GEN_CHECK_OFF(IOMMU, StatMemBulkWriteRZ);
    GEN_CHECK_OFF(IOMMU, StatCmd);
    GEN_CHECK_OFF(IOMMU, StatCmdCompWait);
    GEN_CHECK_OFF(IOMMU, StatCmdInvDte);
    GEN_CHECK_OFF(IOMMU, StatCmdInvIommuPages);
    GEN_CHECK_OFF(IOMMU, StatCmdInvIotlbPages);
    GEN_CHECK_OFF(IOMMU, StatCmdInvIntrTable);
    GEN_CHECK_OFF(IOMMU, StatCmdPrefIommuPages);
    GEN_CHECK_OFF(IOMMU, StatCmdCompletePprReq);
    GEN_CHECK_OFF(IOMMU, StatCmdInvIommuAll);
    GEN_CHECK_OFF(IOMMU, StatIotlbeCached);
    GEN_CHECK_OFF(IOMMU, StatIotlbeLazyEvictReuse);
    GEN_CHECK_OFF(IOMMU, StatProfDteLookup);
    GEN_CHECK_OFF(IOMMU, StatProfIotlbeLookup);
    GEN_CHECK_OFF(IOMMU, StatProfIrteLookup);
    GEN_CHECK_OFF(IOMMU, StatProfIrteCacheLookup);
    GEN_CHECK_OFF(IOMMU, StatAccessCacheHit);
    GEN_CHECK_OFF(IOMMU, StatAccessCacheHitFull);
    GEN_CHECK_OFF(IOMMU, StatAccessCacheMiss);
    GEN_CHECK_OFF(IOMMU, StatAccessCacheNonContig);
    GEN_CHECK_OFF(IOMMU, StatAccessCachePermDenied);
    GEN_CHECK_OFF(IOMMU, StatAccessDteNonContig);
    GEN_CHECK_OFF(IOMMU, StatAccessDtePermDenied);
    GEN_CHECK_OFF(IOMMU, StatIntrCacheHit);
    GEN_CHECK_OFF(IOMMU, StatIntrCacheMiss);
# endif
#endif

#ifdef VBOX_WITH_IOMMU_INTEL
    GEN_CHECK_SIZE(DMAR);
    GEN_CHECK_OFF(DMAR, idxIommu);
    GEN_CHECK_OFF(DMAR, abRegs0);
    GEN_CHECK_OFF(DMAR, abRegs1);
    GEN_CHECK_OFF(DMAR, uIrtaReg);
    GEN_CHECK_OFF(DMAR, uRtaddrReg);
    GEN_CHECK_OFF(DMAR, uVerReg);
    GEN_CHECK_OFF(DMAR, fCapReg);
    GEN_CHECK_OFF(DMAR, fExtCapReg);
    GEN_CHECK_OFF(DMAR, fHawBaseMask);
    GEN_CHECK_OFF(DMAR, fMgawInvMask);
    GEN_CHECK_OFF(DMAR, cMaxPagingLevel);
    GEN_CHECK_OFF(DMAR, fPermValidMask);
    GEN_CHECK_OFF(DMAR, hEvtInvQueue);
    GEN_CHECK_OFF(DMAR, enmDiag);
    GEN_CHECK_OFF(DMAR, hMmio);
# ifdef VBOX_WITH_STATISTICS
    GEN_CHECK_OFF(DMAR, StatMmioReadR3);
    GEN_CHECK_OFF(DMAR, StatMmioReadRZ);
    GEN_CHECK_OFF(DMAR, StatMmioWriteR3);
    GEN_CHECK_OFF(DMAR, StatMmioWriteRZ);
    GEN_CHECK_OFF(DMAR, StatMsiRemapCfiR3);
    GEN_CHECK_OFF(DMAR, StatMsiRemapCfiRZ);
    GEN_CHECK_OFF(DMAR, StatMsiRemapRfiR3);
    GEN_CHECK_OFF(DMAR, StatMsiRemapRfiRZ);
    GEN_CHECK_OFF(DMAR, StatMemReadR3);
    GEN_CHECK_OFF(DMAR, StatMemReadRZ);
    GEN_CHECK_OFF(DMAR, StatMemWriteR3);
    GEN_CHECK_OFF(DMAR, StatMemWriteRZ);
    GEN_CHECK_OFF(DMAR, StatMemBulkReadR3);
    GEN_CHECK_OFF(DMAR, StatMemBulkReadRZ);
    GEN_CHECK_OFF(DMAR, StatMemBulkWriteR3);
    GEN_CHECK_OFF(DMAR, StatMemBulkWriteRZ);
    GEN_CHECK_OFF(DMAR, StatCcInvDsc);
    GEN_CHECK_OFF(DMAR, StatIotlbInvDsc);
    GEN_CHECK_OFF(DMAR, StatDevtlbInvDsc);
    GEN_CHECK_OFF(DMAR, StatIecInvDsc);
    GEN_CHECK_OFF(DMAR, StatInvWaitDsc);
    GEN_CHECK_OFF(DMAR, StatPasidIotlbInvDsc);
    GEN_CHECK_OFF(DMAR, StatPasidCacheInvDsc);
    GEN_CHECK_OFF(DMAR, StatPasidDevtlbInvDsc);
# endif

    GEN_CHECK_SIZE(DMARIOPAGE);
    GEN_CHECK_OFF(DMARIOPAGE, GCPhysBase);
    GEN_CHECK_OFF(DMARIOPAGE, cShift);
    GEN_CHECK_OFF(DMARIOPAGE, fPerm);

    GEN_CHECK_SIZE(DMARIOADDRRANGE);
    GEN_CHECK_OFF(DMARIOADDRRANGE, uAddr);
    GEN_CHECK_OFF(DMARIOADDRRANGE, cb);
    GEN_CHECK_OFF(DMARIOADDRRANGE, fPerm);

    GEN_CHECK_SIZE(DMARMEMREQIN);
    GEN_CHECK_OFF(DMARMEMREQIN, AddrRange);
    GEN_CHECK_OFF(DMARMEMREQIN, idDevice);
    GEN_CHECK_OFF(DMARMEMREQIN, Pasid);
    GEN_CHECK_OFF(DMARMEMREQIN, enmAddrType);
    GEN_CHECK_OFF(DMARMEMREQIN, enmReqType);

    GEN_CHECK_SIZE(DMARMEMREQOUT);
    GEN_CHECK_OFF(DMARMEMREQOUT, AddrRange);
    GEN_CHECK_OFF(DMARMEMREQOUT, idDomain);

    GEN_CHECK_SIZE(DMARMEMREQAUX);
    GEN_CHECK_OFF(DMARMEMREQAUX, fTtm
    GEN_CHECK_OFF(DMARMEMREQAUX, fFpd);
    GEN_CHECK_OFF(DMARMEMREQAUX, cPagingLevel);
    GEN_CHECK_OFF(DMARMEMREQAUX, GCPhysFlPt);
    GEN_CHECK_OFF(DMARMEMREQAUX, GCPhysSlPt);

    GEN_CHECK_SIZE(DMARMEMREQREMAP);
    GEN_CHECK_OFF(DMARMEMREQREMAP, In);
    GEN_CHECK_OFF(DMARMEMREQREMAP, Aux);
    GEN_CHECK_OFF(DMARMEMREQREMAP, Out);
#endif

    return (0);
}
