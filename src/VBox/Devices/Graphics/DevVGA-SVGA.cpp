/* $Id: DevVGA-SVGA.cpp 108286 2025-02-10 10:47:28Z dmitrii.grigorev@oracle.com $ */
/** @file
 * VMware SVGA device.
 *
 * Logging levels guidelines for this and related files:
 *  - Log() for normal bits.
 *  - LogFlow() for more info.
 *  - Log2 for hex dump of cursor data.
 *  - Log3 for hex dump of shader code.
 *  - Log4 for hex dumps of 3D data.
 *  - Log5 for info about GMR pages.
 *  - Log6 for DX shaders.
 *  - Log7 for SVGA command dump.
 *  - Log8 for content of constant and vertex buffers.
 *  - LogRel for the usual important stuff.
 *  - LogRel2 for cursor.
 *  - LogRel3 for 3D performance data.
 *  - LogRel4 for HW accelerated graphics output.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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


/** @page pg_dev_vmsvga     VMSVGA - VMware SVGA II Device Emulation
 *
 * This device emulation was contributed by trivirt AG.  It offers an
 * alternative to our Bochs based VGA graphics and 3d emulations.  This is
 * valuable for Xorg based guests, as there is driver support shipping with Xorg
 * since it forked from XFree86.
 *
 *
 * @section sec_dev_vmsvga_sdk  The VMware SDK
 *
 * This is officially deprecated now, however it's still quite useful,
 * especially for getting the old features working:
 * http://vmware-svga.sourceforge.net/
 *
 * They currently point developers at the following resources.
 *  - http://cgit.freedesktop.org/xorg/driver/xf86-video-vmware/
 *  - http://cgit.freedesktop.org/mesa/mesa/tree/src/gallium/drivers/svga/
 *  - http://cgit.freedesktop.org/mesa/vmwgfx/
 *
 * @subsection subsec_dev_vmsvga_sdk_results  Test results
 *
 * Test results:
 *  - 2dmark.img:
 *       + todo
 *  - backdoor-tclo.img:
 *       + todo
 *  - blit-cube.img:
 *       + todo
 *  - bunnies.img:
 *       + todo
 *  - cube.img:
 *       + todo
 *  - cubemark.img:
 *       + todo
 *  - dynamic-vertex-stress.img:
 *       + todo
 *  - dynamic-vertex.img:
 *       + todo
 *  - fence-stress.img:
 *       + todo
 *  - gmr-test.img:
 *       + todo
 *  - half-float-test.img:
 *       + todo
 *  - noscreen-cursor.img:
 *       - The CURSOR I/O and FIFO registers are not implemented, so the mouse
 *         cursor doesn't show. (Hacking the GUI a little, would make the cursor
 *         visible though.)
 *       - Cursor animation via the palette doesn't work.
 *       - During debugging, it turns out that the framebuffer content seems to
 *         be halfways ignore or something (memset(fb, 0xcc, lots)).
 *       - Trouble with way to small FIFO and the 256x256 cursor fails. Need to
 *         grow it 0x10 fold (128KB -> 2MB like in WS10).
 *  - null.img:
 *       + todo
 *  - pong.img:
 *       + todo
 *  - presentReadback.img:
 *       + todo
 *  - resolution-set.img:
 *       + todo
 *  - rt-gamma-test.img:
 *       + todo
 *  - screen-annotation.img:
 *       + todo
 *  - screen-cursor.img:
 *       + todo
 *  - screen-dma-coalesce.img:
 *       + todo
 *  - screen-gmr-discontig.img:
 *       + todo
 *  - screen-gmr-remap.img:
 *       + todo
 *  - screen-multimon.img:
 *       + todo
 *  - screen-present-clip.img:
 *       + todo
 *  - screen-render-test.img:
 *       + todo
 *  - screen-simple.img:
 *       + todo
 *  - screen-text.img:
 *       + todo
 *  - simple-shaders.img:
 *       + todo
 *  - simple_blit.img:
 *       + todo
 *  - tiny-2d-updates.img:
 *       + todo
 *  - video-formats.img:
 *       + todo
 *  - video-sync.img:
 *       + todo
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_VMSVGA
#include <VBox/vmm/pdmdev.h>
#include <VBox/version.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/vmm/pgm.h>
#include <VBox/sup.h>

#include <iprt/assert.h>
#include <iprt/semaphore.h>
#include <iprt/uuid.h>
#ifdef IN_RING3
# include <iprt/ctype.h>
# include <iprt/mem.h>
# ifdef VBOX_STRICT
#  include <iprt/time.h>
# endif
#endif

#include <VBox/AssertGuest.h>
#include <VBox/VMMDev.h>
#include <VBoxVideo.h>
#include <VBox/bioslogo.h>

#ifdef LOG_ENABLED
#include "svgadump/svga_dump.h"
#endif

/* should go BEFORE any other DevVGA include to make all DevVGA.h config defines be visible */
#include "DevVGA.h"

/* Should be included after DevVGA.h/DevVGA-SVGA.h to pick all defines. */
#ifdef VBOX_WITH_VMSVGA3D
# include "DevVGA-SVGA3d.h"
# ifdef RT_OS_DARWIN
#  include "DevVGA-SVGA3d-cocoa.h"
# endif
# ifdef RT_OS_LINUX
#  ifdef IN_RING3
#   include "DevVGA-SVGA3d-glLdr.h"
#  endif
# endif
#endif
#ifdef IN_RING3
#include "DevVGA-SVGA-internal.h"
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/**
 * Macro for checking if a fixed FIFO register is valid according to the
 * current FIFO configuration.
 *
 * @returns true / false.
 * @param   a_iIndex        The fifo register index (like SVGA_FIFO_CAPABILITIES).
 * @param   a_offFifoMin    A valid SVGA_FIFO_MIN value.
 */
#define VMSVGA_IS_VALID_FIFO_REG(a_iIndex, a_offFifoMin) ( ((a_iIndex) + 1) * sizeof(uint32_t) <= (a_offFifoMin) )


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#ifdef IN_RING3
# if defined(VMSVGA_USE_FIFO_ACCESS_HANDLER) || defined(DEBUG_FIFO_ACCESS)
static FNPGMPHYSHANDLER vmsvgaR3FifoAccessHandler;
# endif
# ifdef DEBUG_GMR_ACCESS
static FNPGMPHYSHANDLER vmsvgaR3GmrAccessHandler;
# endif
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef IN_RING3

/**
 * SSM descriptor table for the VMSVGAGMRDESCRIPTOR structure.
 */
static SSMFIELD const g_aVMSVGAGMRDESCRIPTORFields[] =
{
    SSMFIELD_ENTRY_GCPHYS(      VMSVGAGMRDESCRIPTOR,     GCPhys),
    SSMFIELD_ENTRY(             VMSVGAGMRDESCRIPTOR,     numPages),
    SSMFIELD_ENTRY_TERM()
};

/**
 * SSM descriptor table for the GMR structure.
 */
static SSMFIELD const g_aGMRFields[] =
{
    SSMFIELD_ENTRY(             GMR, cMaxPages),
    SSMFIELD_ENTRY(             GMR, cbTotal),
    SSMFIELD_ENTRY(             GMR, numDescriptors),
    SSMFIELD_ENTRY_IGN_HCPTR(   GMR, paDesc),
    SSMFIELD_ENTRY_TERM()
};

/**
 * SSM descriptor table for the VMSVGASCREENOBJECT structure.
 */
static SSMFIELD const g_aVMSVGASCREENOBJECTFields[] =
{
    SSMFIELD_ENTRY(             VMSVGASCREENOBJECT, fuScreen),
    SSMFIELD_ENTRY(             VMSVGASCREENOBJECT, idScreen),
    SSMFIELD_ENTRY(             VMSVGASCREENOBJECT, xOrigin),
    SSMFIELD_ENTRY(             VMSVGASCREENOBJECT, yOrigin),
    SSMFIELD_ENTRY(             VMSVGASCREENOBJECT, cWidth),
    SSMFIELD_ENTRY(             VMSVGASCREENOBJECT, cHeight),
    SSMFIELD_ENTRY(             VMSVGASCREENOBJECT, offVRAM),
    SSMFIELD_ENTRY(             VMSVGASCREENOBJECT, cbPitch),
    SSMFIELD_ENTRY(             VMSVGASCREENOBJECT, cBpp),
    SSMFIELD_ENTRY(             VMSVGASCREENOBJECT, fDefined),
    SSMFIELD_ENTRY(             VMSVGASCREENOBJECT, fModified),
    SSMFIELD_ENTRY_VER(         VMSVGASCREENOBJECT, cDpi, VGA_SAVEDSTATE_VERSION_VMSVGA_MIPLEVELS),
    SSMFIELD_ENTRY_TERM()
};

/**
 * SSM descriptor table for the VMSVGAR3STATE structure.
 */
static SSMFIELD const g_aVMSVGAR3STATEFields[] =
{
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, paGMR),
    SSMFIELD_ENTRY(             VMSVGAR3STATE, GMRFB),
    SSMFIELD_ENTRY(             VMSVGAR3STATE, Cursor.fActive),
    SSMFIELD_ENTRY(             VMSVGAR3STATE, Cursor.xHotspot),
    SSMFIELD_ENTRY(             VMSVGAR3STATE, Cursor.yHotspot),
    SSMFIELD_ENTRY(             VMSVGAR3STATE, Cursor.width),
    SSMFIELD_ENTRY(             VMSVGAR3STATE, Cursor.height),
    SSMFIELD_ENTRY(             VMSVGAR3STATE, Cursor.cbData),
    SSMFIELD_ENTRY_IGN_HCPTR(   VMSVGAR3STATE, Cursor.pData),
    SSMFIELD_ENTRY(             VMSVGAR3STATE, colorAnnotation),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, cBusyDelayedEmts),
#ifdef VMSVGA_USE_EMT_HALT_CODE
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, BusyDelayedEmts),
#else
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, hBusyDelayedEmts),
#endif
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatBusyDelayEmts),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dPresentProf),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dDrawPrimitivesProf),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dSurfaceDmaProf),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dBlitSurfaceToScreenProf),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3CmdDefineGmr2),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3CmdDefineGmr2Free),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3CmdDefineGmr2Modify),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3CmdRemapGmr2),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3CmdRemapGmr2Modify),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3CmdInvalidCmd),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3CmdFence),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3CmdUpdate),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3CmdUpdateVerbose),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3CmdDefineCursor),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3CmdDefineAlphaCursor),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3CmdMoveCursor),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3CmdDisplayCursor),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3CmdRectFill),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3CmdRectCopy),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3CmdRectRopCopy),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3CmdEscape),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3CmdDefineScreen),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3CmdDestroyScreen),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3CmdDefineGmrFb),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3CmdBlitGmrFbToScreen),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3CmdBlitScreentoGmrFb),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3CmdAnnotationFill),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3CmdAnnotationCopy),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dSurfaceDefine),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dSurfaceDefineV2),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dSurfaceDestroy),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dSurfaceCopy),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dSurfaceStretchBlt),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dSurfaceDma),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dSurfaceScreen),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dContextDefine),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dContextDestroy),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dSetTransform),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dSetZRange),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dSetRenderState),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dSetRenderTarget),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dSetTextureState),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dSetMaterial),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dSetLightData),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dSetLightEnable),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dSetViewPort),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dSetClipPlane),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dClear),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dPresent),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dPresentReadBack),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dShaderDefine),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dShaderDestroy),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dSetShader),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dSetShaderConst),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dDrawPrimitives),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dSetScissorRect),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dBeginQuery),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dEndQuery),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dWaitForQuery),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dGenerateMipmaps),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dActivateSurface),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3Cmd3dDeactivateSurface),

    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3RegConfigDoneWr),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3RegGmrDescriptorWr),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3RegGmrDescriptorWrErrors),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatR3RegGmrDescriptorWrFree),

    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatFifoCommands),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatFifoErrors),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatFifoUnkCmds),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatFifoTodoTimeout),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatFifoTodoWoken),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatFifoStalls),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatFifoExtendedSleep),
# if defined(VMSVGA_USE_FIFO_ACCESS_HANDLER) || defined(DEBUG_FIFO_ACCESS)
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatFifoAccessHandler),
# endif
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatFifoCursorFetchAgain),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatFifoCursorNoChange),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatFifoCursorPosition),
    SSMFIELD_ENTRY_IGNORE(      VMSVGAR3STATE, StatFifoCursorVisiblity),

    SSMFIELD_ENTRY_TERM()
};

/**
 * SSM descriptor table for the VGAState.svga structure.
 */
static SSMFIELD const g_aVGAStateSVGAFields[] =
{
    SSMFIELD_ENTRY_IGN_GCPHYS(      VMSVGAState, GCPhysFIFO),
    SSMFIELD_ENTRY_IGNORE(          VMSVGAState, cbFIFO),
    SSMFIELD_ENTRY_IGNORE(          VMSVGAState, cbFIFOConfig),
    SSMFIELD_ENTRY(                 VMSVGAState, u32SVGAId),
    SSMFIELD_ENTRY(                 VMSVGAState, fEnabled),
    SSMFIELD_ENTRY(                 VMSVGAState, fConfigured),
    SSMFIELD_ENTRY(                 VMSVGAState, fBusy),
    SSMFIELD_ENTRY(                 VMSVGAState, fTraces),
    SSMFIELD_ENTRY(                 VMSVGAState, u32GuestId),
    SSMFIELD_ENTRY(                 VMSVGAState, cScratchRegion),
    SSMFIELD_ENTRY(                 VMSVGAState, au32ScratchRegion),
    SSMFIELD_ENTRY(                 VMSVGAState, u32IrqStatus),
    SSMFIELD_ENTRY(                 VMSVGAState, u32IrqMask),
    SSMFIELD_ENTRY(                 VMSVGAState, u32PitchLock),
    SSMFIELD_ENTRY(                 VMSVGAState, u32CurrentGMRId),
    SSMFIELD_ENTRY(                 VMSVGAState, u32DeviceCaps),
    SSMFIELD_ENTRY_VER(             VMSVGAState, u32DeviceCaps2, VGA_SAVEDSTATE_VERSION_VMSVGA_REG_CAP2),
    SSMFIELD_ENTRY_VER(             VMSVGAState, u32GuestDriverId, VGA_SAVEDSTATE_VERSION_VMSVGA_REG_CAP2),
    SSMFIELD_ENTRY_VER(             VMSVGAState, u32GuestDriverVer1, VGA_SAVEDSTATE_VERSION_VMSVGA_REG_CAP2),
    SSMFIELD_ENTRY_VER(             VMSVGAState, u32GuestDriverVer2, VGA_SAVEDSTATE_VERSION_VMSVGA_REG_CAP2),
    SSMFIELD_ENTRY_VER(             VMSVGAState, u32GuestDriverVer3, VGA_SAVEDSTATE_VERSION_VMSVGA_REG_CAP2),
    SSMFIELD_ENTRY_VER(             VMSVGAState, u32FenceLast, VGA_SAVEDSTATE_VERSION_VMSVGA_SVGA3),
    SSMFIELD_ENTRY(                 VMSVGAState, u32IndexReg),
    SSMFIELD_ENTRY_IGNORE(          VMSVGAState, hFIFORequestSem),
    SSMFIELD_ENTRY_IGNORE(          VMSVGAState, uLastCursorUpdateCount),
    SSMFIELD_ENTRY_IGNORE(          VMSVGAState, fFIFOThreadSleeping),
    SSMFIELD_ENTRY_VER(             VMSVGAState, fGFBRegisters, VGA_SAVEDSTATE_VERSION_VMSVGA_SCREENS),
    SSMFIELD_ENTRY(                 VMSVGAState, uWidth),
    SSMFIELD_ENTRY(                 VMSVGAState, uHeight),
    SSMFIELD_ENTRY(                 VMSVGAState, uBpp),
    SSMFIELD_ENTRY(                 VMSVGAState, cbScanline),
    SSMFIELD_ENTRY_VER(             VMSVGAState, uScreenOffset, VGA_SAVEDSTATE_VERSION_VMSVGA),
    SSMFIELD_ENTRY_VER(             VMSVGAState, uCursorX, VGA_SAVEDSTATE_VERSION_VMSVGA_CURSOR),
    SSMFIELD_ENTRY_VER(             VMSVGAState, uCursorY, VGA_SAVEDSTATE_VERSION_VMSVGA_CURSOR),
    SSMFIELD_ENTRY_VER(             VMSVGAState, uCursorID, VGA_SAVEDSTATE_VERSION_VMSVGA_CURSOR),
    SSMFIELD_ENTRY_VER(             VMSVGAState, uCursorOn, VGA_SAVEDSTATE_VERSION_VMSVGA_CURSOR),
    SSMFIELD_ENTRY(                 VMSVGAState, u32MaxWidth),
    SSMFIELD_ENTRY(                 VMSVGAState, u32MaxHeight),
    SSMFIELD_ENTRY(                 VMSVGAState, u32ActionFlags),
    SSMFIELD_ENTRY(                 VMSVGAState, f3DEnabled),
    SSMFIELD_ENTRY(                 VMSVGAState, fVRAMTracking),
    SSMFIELD_ENTRY_IGNORE(          VMSVGAState, u8FIFOExtCommand),
    SSMFIELD_ENTRY_IGNORE(          VMSVGAState, fFifoExtCommandWakeup),
    SSMFIELD_ENTRY_IGNORE(          VMSVGAState, cGMR),
    SSMFIELD_ENTRY_VER(             VMSVGAState, au32DevCaps, VGA_SAVEDSTATE_VERSION_VMSVGA_DX),
    SSMFIELD_ENTRY_VER(             VMSVGAState, u32DevCapIndex, VGA_SAVEDSTATE_VERSION_VMSVGA_DX),
    SSMFIELD_ENTRY_VER(             VMSVGAState, u32RegCommandLow, VGA_SAVEDSTATE_VERSION_VMSVGA_DX),
    SSMFIELD_ENTRY_VER(             VMSVGAState, u32RegCommandHigh, VGA_SAVEDSTATE_VERSION_VMSVGA_DX),

    SSMFIELD_ENTRY_TERM()
};
#endif /* IN_RING3 */


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#ifdef IN_RING3
static void vmsvgaR3SetTraces(PPDMDEVINS pDevIns, PVGASTATE pThis, bool fTraces);
static int vmsvgaR3LoadExecFifo(PCPDMDEVHLPR3 pHlp, PVGASTATE pThis, PVGASTATECC pThisCC, PSSMHANDLE pSSM,
                                uint32_t uVersion, uint32_t uPass);
static int vmsvgaR3SaveExecFifo(PCPDMDEVHLPR3 pHlp, PVGASTATECC pThisCC, PSSMHANDLE pSSM);
static void vmsvgaR3CmdBufSubmit(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC, RTGCPHYS GCPhysCB, SVGACBContext CBCtx);
static void vmsvgaR3PowerOnDevice(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC, bool fLoadState);
#endif /* IN_RING3 */


#define SVGA_CASE_ID2STR(idx) case idx: return #idx
#if defined(LOG_ENABLED)
/**
 * Index register string name lookup
 *
 * @returns Index register string or "UNKNOWN"
 * @param   pThis       The shared VGA/VMSVGA state.
 * @param   idxReg      The index register.
 */
static const char *vmsvgaIndexToString(PVGASTATE pThis, uint32_t idxReg)
{
    AssertCompile(SVGA_REG_TOP == 84); /* Ensure that the correct headers are used. */
    switch (idxReg)
    {
        SVGA_CASE_ID2STR(SVGA_REG_ID);
        SVGA_CASE_ID2STR(SVGA_REG_ENABLE);
        SVGA_CASE_ID2STR(SVGA_REG_WIDTH);
        SVGA_CASE_ID2STR(SVGA_REG_HEIGHT);
        SVGA_CASE_ID2STR(SVGA_REG_MAX_WIDTH);
        SVGA_CASE_ID2STR(SVGA_REG_MAX_HEIGHT);
        SVGA_CASE_ID2STR(SVGA_REG_DEPTH);
        SVGA_CASE_ID2STR(SVGA_REG_BITS_PER_PIXEL);       /* Current bpp in the guest */
        SVGA_CASE_ID2STR(SVGA_REG_PSEUDOCOLOR);
        SVGA_CASE_ID2STR(SVGA_REG_RED_MASK);
        SVGA_CASE_ID2STR(SVGA_REG_GREEN_MASK);
        SVGA_CASE_ID2STR(SVGA_REG_BLUE_MASK);
        SVGA_CASE_ID2STR(SVGA_REG_BYTES_PER_LINE);
        SVGA_CASE_ID2STR(SVGA_REG_FB_START);            /* (Deprecated) */
        SVGA_CASE_ID2STR(SVGA_REG_FB_OFFSET);
        SVGA_CASE_ID2STR(SVGA_REG_VRAM_SIZE);
        SVGA_CASE_ID2STR(SVGA_REG_FB_SIZE);

        /* ID 0 implementation only had the above registers, then the palette */
        SVGA_CASE_ID2STR(SVGA_REG_CAPABILITIES);
        SVGA_CASE_ID2STR(SVGA_REG_MEM_START);           /* (Deprecated) */
        SVGA_CASE_ID2STR(SVGA_REG_MEM_SIZE);
        SVGA_CASE_ID2STR(SVGA_REG_CONFIG_DONE);         /* Set when memory area configured */
        SVGA_CASE_ID2STR(SVGA_REG_SYNC);                /* See "FIFO Synchronization Registers" */
        SVGA_CASE_ID2STR(SVGA_REG_BUSY);                /* See "FIFO Synchronization Registers" */
        SVGA_CASE_ID2STR(SVGA_REG_GUEST_ID);            /* Set guest OS identifier */
        SVGA_CASE_ID2STR(SVGA_REG_DEAD);                /* (Deprecated) SVGA_REG_CURSOR_ID. */
        SVGA_CASE_ID2STR(SVGA_REG_CURSOR_X);            /* (Deprecated) */
        SVGA_CASE_ID2STR(SVGA_REG_CURSOR_Y);            /* (Deprecated) */
        SVGA_CASE_ID2STR(SVGA_REG_CURSOR_ON);           /* (Deprecated) */
        SVGA_CASE_ID2STR(SVGA_REG_HOST_BITS_PER_PIXEL); /* (Deprecated) */
        SVGA_CASE_ID2STR(SVGA_REG_SCRATCH_SIZE);        /* Number of scratch registers */
        SVGA_CASE_ID2STR(SVGA_REG_MEM_REGS);            /* Number of FIFO registers */
        SVGA_CASE_ID2STR(SVGA_REG_NUM_DISPLAYS);        /* (Deprecated) */
        SVGA_CASE_ID2STR(SVGA_REG_PITCHLOCK);           /* Fixed pitch for all modes */
        SVGA_CASE_ID2STR(SVGA_REG_IRQMASK);             /* Interrupt mask */

        /* Legacy multi-monitor support */
        SVGA_CASE_ID2STR(SVGA_REG_NUM_GUEST_DISPLAYS); /* Number of guest displays in X/Y direction */
        SVGA_CASE_ID2STR(SVGA_REG_DISPLAY_ID);         /* Display ID for the following display attributes */
        SVGA_CASE_ID2STR(SVGA_REG_DISPLAY_IS_PRIMARY); /* Whether this is a primary display */
        SVGA_CASE_ID2STR(SVGA_REG_DISPLAY_POSITION_X); /* The display position x */
        SVGA_CASE_ID2STR(SVGA_REG_DISPLAY_POSITION_Y); /* The display position y */
        SVGA_CASE_ID2STR(SVGA_REG_DISPLAY_WIDTH);      /* The display's width */
        SVGA_CASE_ID2STR(SVGA_REG_DISPLAY_HEIGHT);     /* The display's height */

        SVGA_CASE_ID2STR(SVGA_REG_GMR_ID);
        SVGA_CASE_ID2STR(SVGA_REG_GMR_DESCRIPTOR);
        SVGA_CASE_ID2STR(SVGA_REG_GMR_MAX_IDS);
        SVGA_CASE_ID2STR(SVGA_REG_GMR_MAX_DESCRIPTOR_LENGTH);

        SVGA_CASE_ID2STR(SVGA_REG_TRACES);             /* Enable trace-based updates even when FIFO is on */
        SVGA_CASE_ID2STR(SVGA_REG_GMRS_MAX_PAGES);     /* Maximum number of 4KB pages for all GMRs */
        SVGA_CASE_ID2STR(SVGA_REG_MEMORY_SIZE);        /* Total dedicated device memory excluding FIFO */
        SVGA_CASE_ID2STR(SVGA_REG_COMMAND_LOW);        /* Lower 32 bits and submits commands */
        SVGA_CASE_ID2STR(SVGA_REG_COMMAND_HIGH);       /* Upper 32 bits of command buffer PA */
        SVGA_CASE_ID2STR(SVGA_REG_MAX_PRIMARY_BOUNDING_BOX_MEM);   /* Max primary memory */
        SVGA_CASE_ID2STR(SVGA_REG_SUGGESTED_GBOBJECT_MEM_SIZE_KB); /* Suggested limit on mob mem */
        SVGA_CASE_ID2STR(SVGA_REG_DEV_CAP);            /* Write dev cap index, read value */
        SVGA_CASE_ID2STR(SVGA_REG_CMD_PREPEND_LOW);
        SVGA_CASE_ID2STR(SVGA_REG_CMD_PREPEND_HIGH);
        SVGA_CASE_ID2STR(SVGA_REG_SCREENTARGET_MAX_WIDTH);
        SVGA_CASE_ID2STR(SVGA_REG_SCREENTARGET_MAX_HEIGHT);
        SVGA_CASE_ID2STR(SVGA_REG_MOB_MAX_SIZE);
        SVGA_CASE_ID2STR(SVGA_REG_BLANK_SCREEN_TARGETS);
        SVGA_CASE_ID2STR(SVGA_REG_CAP2);
        SVGA_CASE_ID2STR(SVGA_REG_DEVEL_CAP);
        SVGA_CASE_ID2STR(SVGA_REG_GUEST_DRIVER_ID);
        SVGA_CASE_ID2STR(SVGA_REG_GUEST_DRIVER_VERSION1);
        SVGA_CASE_ID2STR(SVGA_REG_GUEST_DRIVER_VERSION2);
        SVGA_CASE_ID2STR(SVGA_REG_GUEST_DRIVER_VERSION3);
        SVGA_CASE_ID2STR(SVGA_REG_CURSOR_MOBID);
        SVGA_CASE_ID2STR(SVGA_REG_CURSOR_MAX_BYTE_SIZE);
        SVGA_CASE_ID2STR(SVGA_REG_CURSOR_MAX_DIMENSION);
        SVGA_CASE_ID2STR(SVGA_REG_FIFO_CAPS);
        SVGA_CASE_ID2STR(SVGA_REG_FENCE);
        SVGA_CASE_ID2STR(SVGA_REG_RESERVED1);
        SVGA_CASE_ID2STR(SVGA_REG_RESERVED2);
        SVGA_CASE_ID2STR(SVGA_REG_RESERVED3);
        SVGA_CASE_ID2STR(SVGA_REG_RESERVED4);
        SVGA_CASE_ID2STR(SVGA_REG_RESERVED5);
        SVGA_CASE_ID2STR(SVGA_REG_SCREENDMA);
        SVGA_CASE_ID2STR(SVGA_REG_GBOBJECT_MEM_SIZE_KB);
        SVGA_CASE_ID2STR(SVGA_REG_REGS_START_HIGH32);
        SVGA_CASE_ID2STR(SVGA_REG_REGS_START_LOW32);
        SVGA_CASE_ID2STR(SVGA_REG_FB_START_HIGH32);
        SVGA_CASE_ID2STR(SVGA_REG_FB_START_LOW32);
        SVGA_CASE_ID2STR(SVGA_REG_MSHINT);
        SVGA_CASE_ID2STR(SVGA_REG_IRQ_STATUS);
        SVGA_CASE_ID2STR(SVGA_REG_DIRTY_TRACKING);
        SVGA_CASE_ID2STR(SVGA_REG_TOP);                /* Must be 1 more than the last register */

        default:
            if (idxReg - (uint32_t)SVGA_SCRATCH_BASE < pThis->svga.cScratchRegion)
                return "SVGA_SCRATCH_BASE reg";
            if (idxReg - (uint32_t)SVGA_PALETTE_BASE < (uint32_t)SVGA_NUM_PALETTE_REGS)
                return "SVGA_PALETTE_BASE reg";
            return "UNKNOWN";
    }
}
#endif /* LOG_ENABLED */

#if defined(LOG_ENABLED) || (defined(IN_RING3) && defined(VBOX_WITH_VMSVGA3D))
static const char *vmsvgaDevCapIndexToString(SVGA3dDevCapIndex idxDevCap)
{
    AssertCompile(SVGA3D_DEVCAP_MAX == 260);
    switch (idxDevCap)
    {
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_INVALID);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_3D);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MAX_LIGHTS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MAX_TEXTURES);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MAX_CLIP_PLANES);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_VERTEX_SHADER_VERSION);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_VERTEX_SHADER);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_FRAGMENT_SHADER_VERSION);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_FRAGMENT_SHADER);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MAX_RENDER_TARGETS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_S23E8_TEXTURES);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_S10E5_TEXTURES);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MAX_FIXED_VERTEXBLEND);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_D16_BUFFER_FORMAT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_D24S8_BUFFER_FORMAT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_D24X8_BUFFER_FORMAT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_QUERY_TYPES);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_TEXTURE_GRADIENT_SAMPLING);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MAX_POINT_SIZE);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MAX_SHADER_TEXTURES);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MAX_TEXTURE_WIDTH);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MAX_TEXTURE_HEIGHT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MAX_VOLUME_EXTENT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MAX_TEXTURE_REPEAT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MAX_TEXTURE_ASPECT_RATIO);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MAX_TEXTURE_ANISOTROPY);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MAX_PRIMITIVE_COUNT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MAX_VERTEX_INDEX);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MAX_VERTEX_SHADER_INSTRUCTIONS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MAX_FRAGMENT_SHADER_INSTRUCTIONS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MAX_VERTEX_SHADER_TEMPS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MAX_FRAGMENT_SHADER_TEMPS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_TEXTURE_OPS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_X8R8G8B8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_A8R8G8B8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_A2R10G10B10);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_X1R5G5B5);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_A1R5G5B5);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_A4R4G4B4);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_R5G6B5);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_LUMINANCE16);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_LUMINANCE8_ALPHA8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_ALPHA8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_LUMINANCE8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_Z_D16);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_Z_D24S8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_Z_D24X8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_DXT1);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_DXT2);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_DXT3);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_DXT4);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_DXT5);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_BUMPX8L8V8U8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_A2W10V10U10);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_BUMPU8V8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_Q8W8V8U8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_CxV8U8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_R_S10E5);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_R_S23E8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_RG_S10E5);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_RG_S23E8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_ARGB_S10E5);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_ARGB_S23E8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MISSING62);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MAX_VERTEX_SHADER_TEXTURES);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MAX_SIMULTANEOUS_RENDER_TARGETS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_V16U16);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_G16R16);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_A16B16G16R16);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_UYVY);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_YUY2);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DEAD4); /* SVGA3D_DEVCAP_MULTISAMPLE_NONMASKABLESAMPLES */
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DEAD5); /* SVGA3D_DEVCAP_MULTISAMPLE_MASKABLESAMPLES */
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DEAD7); /* SVGA3D_DEVCAP_ALPHATOCOVERAGE */
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DEAD6); /* SVGA3D_DEVCAP_SUPERSAMPLE */
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_AUTOGENMIPMAPS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_NV12);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DEAD10); /* SVGA3D_DEVCAP_SURFACEFMT_AYUV */
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MAX_CONTEXT_IDS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MAX_SURFACE_IDS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_Z_DF16);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_Z_DF24);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_Z_D24S8_INT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_ATI1);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_ATI2);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DEAD1);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DEAD8); /* SVGA3D_DEVCAP_VIDEO_DECODE */
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DEAD9); /* SVGA3D_DEVCAP_VIDEO_PROCESS */
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_LINE_AA);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_LINE_STIPPLE);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MAX_LINE_WIDTH);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MAX_AA_LINE_WIDTH);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SURFACEFMT_YV12);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DEAD3); /* Old SVGA3D_DEVCAP_LOGICOPS */
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_TS_COLOR_KEY);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DEAD2);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXCONTEXT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DEAD11); /* SVGA3D_DEVCAP_MAX_TEXTURE_ARRAY_SIZE */
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DX_MAX_VERTEXBUFFERS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DX_MAX_CONSTANT_BUFFERS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DX_PROVOKING_VERTEX);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_X8R8G8B8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_A8R8G8B8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R5G6B5);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_X1R5G5B5);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_A1R5G5B5);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_A4R4G4B4);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_Z_D32);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_Z_D16);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_Z_D24S8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_Z_D15S1);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_LUMINANCE8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_LUMINANCE4_ALPHA4);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_LUMINANCE16);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_LUMINANCE8_ALPHA8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_DXT1);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_DXT2);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_DXT3);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_DXT4);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_DXT5);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_BUMPU8V8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_BUMPL6V5U5);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_BUMPX8L8V8U8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_FORMAT_DEAD1);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_ARGB_S10E5);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_ARGB_S23E8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_A2R10G10B10);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_V8U8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_Q8W8V8U8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_CxV8U8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_X8L8V8U8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_A2W10V10U10);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_ALPHA8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R_S10E5);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R_S23E8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_RG_S10E5);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_RG_S23E8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_BUFFER);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_Z_D24X8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_V16U16);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_G16R16);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_A16B16G16R16);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_UYVY);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_YUY2);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_NV12);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_FORMAT_DEAD2); /* SVGA3D_DEVCAP_DXFMT_AYUV */
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R32G32B32A32_TYPELESS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R32G32B32A32_UINT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R32G32B32A32_SINT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R32G32B32_TYPELESS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R32G32B32_FLOAT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R32G32B32_UINT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R32G32B32_SINT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R16G16B16A16_TYPELESS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R16G16B16A16_UINT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R16G16B16A16_SNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R16G16B16A16_SINT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R32G32_TYPELESS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R32G32_UINT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R32G32_SINT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R32G8X24_TYPELESS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_D32_FLOAT_S8X24_UINT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R32_FLOAT_X8X24);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_X32_G8X24_UINT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R10G10B10A2_TYPELESS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R10G10B10A2_UINT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R11G11B10_FLOAT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R8G8B8A8_TYPELESS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R8G8B8A8_UNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R8G8B8A8_UNORM_SRGB);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R8G8B8A8_UINT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R8G8B8A8_SINT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R16G16_TYPELESS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R16G16_UINT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R16G16_SINT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R32_TYPELESS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_D32_FLOAT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R32_UINT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R32_SINT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R24G8_TYPELESS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_D24_UNORM_S8_UINT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R24_UNORM_X8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_X24_G8_UINT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R8G8_TYPELESS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R8G8_UNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R8G8_UINT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R8G8_SINT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R16_TYPELESS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R16_UNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R16_UINT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R16_SNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R16_SINT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R8_TYPELESS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R8_UNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R8_UINT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R8_SNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R8_SINT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_P8);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R9G9B9E5_SHAREDEXP);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R8G8_B8G8_UNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_G8R8_G8B8_UNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_BC1_TYPELESS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_BC1_UNORM_SRGB);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_BC2_TYPELESS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_BC2_UNORM_SRGB);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_BC3_TYPELESS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_BC3_UNORM_SRGB);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_BC4_TYPELESS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_ATI1);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_BC4_SNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_BC5_TYPELESS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_ATI2);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_BC5_SNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R10G10B10_XR_BIAS_A2_UNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_B8G8R8A8_TYPELESS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_B8G8R8A8_UNORM_SRGB);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_B8G8R8X8_TYPELESS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_B8G8R8X8_UNORM_SRGB);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_Z_DF16);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_Z_DF24);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_Z_D24S8_INT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_YV12);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R32G32B32A32_FLOAT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R16G16B16A16_FLOAT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R16G16B16A16_UNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R32G32_FLOAT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R10G10B10A2_UNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R8G8B8A8_SNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R16G16_FLOAT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R16G16_UNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R16G16_SNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R32_FLOAT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R8G8_SNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_R16_FLOAT);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_D16_UNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_A8_UNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_BC1_UNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_BC2_UNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_BC3_UNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_B5G6R5_UNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_B5G5R5A1_UNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_B8G8R8A8_UNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_B8G8R8X8_UNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_BC4_UNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_BC5_UNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SM41);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MULTISAMPLE_2X);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MULTISAMPLE_4X);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MS_FULL_QUALITY);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_LOGICOPS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_LOGIC_BLENDOPS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_RESERVED_1);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_BC6H_TYPELESS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_BC6H_UF16);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_BC6H_SF16);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_BC7_TYPELESS);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_BC7_UNORM);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_DXFMT_BC7_UNORM_SRGB);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_RESERVED_2);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_SM5);
        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MULTISAMPLE_8X);

        SVGA_CASE_ID2STR(SVGA3D_DEVCAP_MAX);

        default:
            break;
    }
    return "UNKNOWN";
}
#endif /* defined(LOG_ENABLED) || (defined(IN_RING3) && defined(VBOX_WITH_VMSVGA3D)) */
#undef SVGA_CASE_ID2STR


#ifdef IN_RING3

/**
 * @interface_method_impl{PDMIDISPLAYPORT,pfnSetViewport}
 */
DECLCALLBACK(void) vmsvgaR3PortSetViewport(PPDMIDISPLAYPORT pInterface, uint32_t idScreen, uint32_t x, uint32_t y, uint32_t cx, uint32_t cy)
{
    PVGASTATECC pThisCC = RT_FROM_MEMBER(pInterface, VGASTATECC, IPort);
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pThisCC->pDevIns, PVGASTATE);

    Log(("vmsvgaPortSetViewPort: screen %d (%d,%d)(%d,%d)\n", idScreen, x, y, cx, cy));
    VMSVGAVIEWPORT const OldViewport = pThis->svga.viewport;

    /** @todo Test how it interacts with multiple screen objects. */
    VMSVGASCREENOBJECT *pScreen = vmsvgaR3GetScreenObject(pThisCC, idScreen);
    uint32_t const uWidth = pScreen ? pScreen->cWidth : 0;
    uint32_t const uHeight = pScreen ? pScreen->cHeight : 0;

    if (x < uWidth)
    {
        pThis->svga.viewport.x      = x;
        pThis->svga.viewport.cx     = RT_MIN(cx, uWidth - x);
        pThis->svga.viewport.xRight = x + pThis->svga.viewport.cx;
    }
    else
    {
        pThis->svga.viewport.x      = uWidth;
        pThis->svga.viewport.cx     = 0;
        pThis->svga.viewport.xRight = uWidth;
    }
    if (y < uHeight)
    {
        pThis->svga.viewport.y       = y;
        pThis->svga.viewport.cy      = RT_MIN(cy, uHeight - y);
        pThis->svga.viewport.yLowWC  = uHeight - y - pThis->svga.viewport.cy;
        pThis->svga.viewport.yHighWC = uHeight - y;
    }
    else
    {
        pThis->svga.viewport.y       = uHeight;
        pThis->svga.viewport.cy      = 0;
        pThis->svga.viewport.yLowWC  = 0;
        pThis->svga.viewport.yHighWC = 0;
    }

# ifdef VBOX_WITH_VMSVGA3D
    /*
     * Now inform the 3D backend.
     */
    if (pThis->svga.f3DEnabled)
        vmsvga3dUpdateHostScreenViewport(pThisCC, idScreen, &OldViewport);
# else
    RT_NOREF(OldViewport);
# endif
}


/**
 * Updating screen information in API
 *
 * @param   pThis       The The shared VGA/VMSVGA instance data.
 * @param   pThisCC     The VGA/VMSVGA state for ring-3.
 */
static void vmsvgaR3VBVAResize(PVGASTATE pThis, PVGASTATECC pThisCC)
{
    int rc;

    PVMSVGAR3STATE pSVGAState = pThisCC->svga.pSvgaR3State;

    for (unsigned iScreen = 0; iScreen < RT_ELEMENTS(pSVGAState->aScreens); ++iScreen)
    {
        VMSVGASCREENOBJECT *pScreen = &pSVGAState->aScreens[iScreen];
        if (!pScreen->fModified)
            continue;

        pScreen->fModified = false;

        VBVAINFOVIEW view;
        RT_ZERO(view);
        view.u32ViewIndex     = pScreen->idScreen;
        // view.u32ViewOffset    = 0;
        view.u32ViewSize      = pThis->vram_size;
        view.u32MaxScreenSize = pThis->vram_size;

        VBVAINFOSCREEN screen;
        RT_ZERO(screen);
        screen.u32ViewIndex   = pScreen->idScreen;

        if (pScreen->fDefined)
        {
            if (   pScreen->cWidth  == VMSVGA_VAL_UNINITIALIZED
                || pScreen->cHeight == VMSVGA_VAL_UNINITIALIZED
                || pScreen->cBpp    == VMSVGA_VAL_UNINITIALIZED)
            {
                Assert(pThis->svga.fGFBRegisters);
                continue;
            }

            screen.i32OriginX      = pScreen->xOrigin;
            screen.i32OriginY      = pScreen->yOrigin;
            screen.u32StartOffset  = pScreen->offVRAM;
            screen.u32LineSize     = pScreen->cbPitch;
            screen.u32Width        = pScreen->cWidth;
            screen.u32Height       = pScreen->cHeight;
            screen.u16BitsPerPixel = pScreen->cBpp;
            if (!(pScreen->fuScreen & SVGA_SCREEN_DEACTIVATE))
                screen.u16Flags    = VBVA_SCREEN_F_ACTIVE;
            if (pScreen->fuScreen & SVGA_SCREEN_BLANKING)
                screen.u16Flags   |= VBVA_SCREEN_F_BLANK2;
        }
        else
        {
            /* Screen is destroyed. */
            screen.u16Flags        = VBVA_SCREEN_F_DISABLED;
        }

        void *pvVRAM = pScreen->pvScreenBitmap ? pScreen->pvScreenBitmap : pThisCC->pbVRam;
        rc = pThisCC->pDrv->pfnVBVAResize(pThisCC->pDrv, &view, &screen, pvVRAM, /*fResetInputMapping=*/ true);
        AssertRC(rc);
    }
}


/**
 * @interface_method_impl{PDMIDISPLAYPORT,pfnReportMonitorPositions}
 *
 * Used to update screen offsets (positions) since appearently vmwgfx fails to
 * pass correct offsets thru FIFO.
 */
DECLCALLBACK(void) vmsvgaR3PortReportMonitorPositions(PPDMIDISPLAYPORT pInterface, uint32_t cPositions, PCRTPOINT paPositions)
{
    PVGASTATECC         pThisCC    = RT_FROM_MEMBER(pInterface, VGASTATECC, IPort);
    PVGASTATE           pThis      = PDMDEVINS_2_DATA(pThisCC->pDevIns, PVGASTATE);
    PVMSVGAR3STATE      pSVGAState = pThisCC->svga.pSvgaR3State;

    AssertReturnVoid(pSVGAState);

    /* We assume cPositions is the # of outputs Xserver reports and paPositions is (-1, -1) for disabled monitors. */
    cPositions = RT_MIN(cPositions, RT_ELEMENTS(pSVGAState->aScreens));
    for (uint32_t i = 0; i < cPositions; ++i)
    {
        if (   pSVGAState->aScreens[i].xOrigin == paPositions[i].x
            && pSVGAState->aScreens[i].yOrigin == paPositions[i].y)
            continue;

        if (paPositions[i].x == -1)
            continue;
        if (paPositions[i].y == -1)
            continue;

        pSVGAState->aScreens[i].xOrigin = paPositions[i].x;
        pSVGAState->aScreens[i].yOrigin = paPositions[i].y;
        pSVGAState->aScreens[i].fModified = true;
    }

    vmsvgaR3VBVAResize(pThis, pThisCC);
}

#endif /* IN_RING3 */

/**
 * Read port register
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The shared VGA/VMSVGA state.
 * @param   idxReg      The register index being read.
 * @param   pu32        Where to store the read value
 */
static int vmsvgaReadPort(PPDMDEVINS pDevIns, PVGASTATE pThis, uint32_t idxReg, uint32_t *pu32)
{
#ifdef IN_RING3
    PVGASTATER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
#endif
    int rc = VINF_SUCCESS;
    *pu32 = 0;

    /* We must adjust the register number if we're in SVGA_ID_0 mode because the PALETTE range moved. */
    if (   idxReg >= SVGA_REG_ID_0_TOP
        && pThis->svga.u32SVGAId == SVGA_ID_0)
    {
        idxReg += SVGA_PALETTE_BASE - SVGA_REG_ID_0_TOP;
        Log(("vmsvgaReadPort: SVGA_ID_0 reg adj %#x -> %#x\n", pThis->svga.u32IndexReg, idxReg));
    }

    switch (idxReg)
    {
        case SVGA_REG_ID:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegIdRd);
            *pu32 = pThis->svga.u32SVGAId;
            break;

        case SVGA_REG_ENABLE:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegEnableRd);
            *pu32 = pThis->svga.fEnabled;
            break;

        case SVGA_REG_WIDTH:
        {
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegWidthRd);
            if (    pThis->svga.fEnabled
                &&  pThis->svga.uWidth != VMSVGA_VAL_UNINITIALIZED)
                *pu32 = pThis->svga.uWidth;
            else
            {
#ifndef IN_RING3
                rc = VINF_IOM_R3_IOPORT_READ;
#else
                *pu32 = pThisCC->pDrv->cx;
#endif
            }
            break;
        }

        case SVGA_REG_HEIGHT:
        {
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegHeightRd);
            if (    pThis->svga.fEnabled
                &&  pThis->svga.uHeight != VMSVGA_VAL_UNINITIALIZED)
                *pu32 = pThis->svga.uHeight;
            else
            {
#ifndef IN_RING3
                rc = VINF_IOM_R3_IOPORT_READ;
#else
                *pu32 = pThisCC->pDrv->cy;
#endif
            }
            break;
        }

        case SVGA_REG_MAX_WIDTH:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegMaxWidthRd);
            *pu32 = pThis->svga.u32MaxWidth;
            break;

        case SVGA_REG_MAX_HEIGHT:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegMaxHeightRd);
            *pu32 = pThis->svga.u32MaxHeight;
            break;

        case SVGA_REG_DEPTH:
            /* This returns the color depth of the current mode. */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegDepthRd);
            switch (pThis->svga.uBpp)
            {
                case 15:
                case 16:
                case 24:
                    *pu32 = pThis->svga.uBpp;
                    break;

                default:
                case 32:
                    *pu32 = 24; /* The upper 8 bits are either alpha bits or not used. */
                    break;
            }
            break;

        case SVGA_REG_HOST_BITS_PER_PIXEL: /* (Deprecated) */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegHostBitsPerPixelRd);
            *pu32 = pThis->svga.uHostBpp;
            break;

        case SVGA_REG_BITS_PER_PIXEL:      /* Current bpp in the guest */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegBitsPerPixelRd);
            *pu32 = pThis->svga.uBpp;
            break;

        case SVGA_REG_PSEUDOCOLOR:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegPsuedoColorRd);
            *pu32 = pThis->svga.uBpp == 8; /* See section 6 "Pseudocolor" in svga_interface.txt. */
            break;

        case SVGA_REG_RED_MASK:
        case SVGA_REG_GREEN_MASK:
        case SVGA_REG_BLUE_MASK:
        {
            uint32_t uBpp;

            if (pThis->svga.fEnabled)
                uBpp = pThis->svga.uBpp;
            else
                uBpp = pThis->svga.uHostBpp;

            uint32_t u32RedMask, u32GreenMask, u32BlueMask;
            switch (uBpp)
            {
                case 8:
                    u32RedMask   = 0x07;
                    u32GreenMask = 0x38;
                    u32BlueMask  = 0xc0;
                    break;

                case 15:
                    u32RedMask   = 0x0000001f;
                    u32GreenMask = 0x000003e0;
                    u32BlueMask  = 0x00007c00;
                    break;

                case 16:
                    u32RedMask   = 0x0000001f;
                    u32GreenMask = 0x000007e0;
                    u32BlueMask  = 0x0000f800;
                    break;

                case 24:
                case 32:
                default:
                    u32RedMask   = 0x00ff0000;
                    u32GreenMask = 0x0000ff00;
                    u32BlueMask  = 0x000000ff;
                    break;
            }
            switch (idxReg)
            {
                case SVGA_REG_RED_MASK:
                    STAM_REL_COUNTER_INC(&pThis->svga.StatRegRedMaskRd);
                    *pu32 = u32RedMask;
                    break;

                case SVGA_REG_GREEN_MASK:
                    STAM_REL_COUNTER_INC(&pThis->svga.StatRegGreenMaskRd);
                    *pu32 = u32GreenMask;
                    break;

                case SVGA_REG_BLUE_MASK:
                    STAM_REL_COUNTER_INC(&pThis->svga.StatRegBlueMaskRd);
                    *pu32 = u32BlueMask;
                    break;
            }
            break;
        }

        case SVGA_REG_BYTES_PER_LINE:
        {
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegBytesPerLineRd);
            if (    pThis->svga.fEnabled
                &&  pThis->svga.cbScanline)
                *pu32 = pThis->svga.cbScanline;
            else
            {
#ifndef IN_RING3
                rc = VINF_IOM_R3_IOPORT_READ;
#else
                *pu32 = pThisCC->pDrv->cbScanline;
#endif
            }
            break;
        }

        case SVGA_REG_VRAM_SIZE:            /* VRAM size */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegVramSizeRd);
            *pu32 = pThis->vram_size;
            break;

        case SVGA_REG_FB_START:             /* Frame buffer physical address. */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegFbStartRd);
            Assert(pThis->GCPhysVRAM <= 0xffffffff);
            *pu32 = pThis->GCPhysVRAM;
            break;

        case SVGA_REG_FB_OFFSET:            /* Offset of the frame buffer in VRAM */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegFbOffsetRd);
            /* Always zero in our case. */
            *pu32 = 0;
            break;

        case SVGA_REG_FB_SIZE:              /* Frame buffer size */
        {
#ifndef IN_RING3
            rc = VINF_IOM_R3_IOPORT_READ;
#else
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegFbSizeRd);

            /* VMWare testcases want at least 4 MB in case the hardware is disabled. */
            if (    pThis->svga.fEnabled
                &&  pThis->svga.uHeight != VMSVGA_VAL_UNINITIALIZED)
            {
                /* Hardware enabled; return real framebuffer size .*/
                *pu32 = (uint32_t)pThis->svga.uHeight * pThis->svga.cbScanline;
            }
            else
                *pu32 = RT_MAX(0x100000, (uint32_t)pThisCC->pDrv->cy * pThisCC->pDrv->cbScanline);

            *pu32 = RT_MIN(pThis->vram_size, *pu32);
            Log(("h=%d w=%d bpp=%d\n", pThisCC->pDrv->cy, pThisCC->pDrv->cx, pThisCC->pDrv->cBits));
#endif
            break;
        }

        case SVGA_REG_CAPABILITIES:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegCapabilitesRd);
            *pu32 = pThis->svga.u32DeviceCaps;
            break;

        case SVGA_REG_MEM_START:           /* FIFO start */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegMemStartRd);
            Assert(pThis->svga.GCPhysFIFO <= 0xffffffff);
            *pu32 = pThis->svga.GCPhysFIFO;
            break;

        case SVGA_REG_MEM_SIZE:            /* FIFO size */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegMemSizeRd);
            *pu32 = pThis->svga.cbFIFO;
            break;

        case SVGA_REG_CONFIG_DONE:         /* Set when memory area configured */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegConfigDoneRd);
            *pu32 = pThis->svga.fConfigured;
            break;

        case SVGA_REG_SYNC:                /* See "FIFO Synchronization Registers" */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegSyncRd);
            *pu32 = 0;
            break;

        case SVGA_REG_BUSY:                /* See "FIFO Synchronization Registers" */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegBusyRd);
            if (pThis->svga.fBusy)
            {
#ifndef IN_RING3
                /* Go to ring-3 and halt the CPU. */
                rc = VINF_IOM_R3_IOPORT_READ;
                RT_NOREF(pDevIns);
                break;
#else /* IN_RING3 */
# if defined(VMSVGA_USE_EMT_HALT_CODE)
                /* The guest is basically doing a HLT via the device here, but with
                   a special wake up condition on FIFO completion. */
                PVMSVGAR3STATE pSVGAState = pThisCC->svga.pSvgaR3State;
                STAM_REL_PROFILE_START(&pSVGAState->StatBusyDelayEmts, EmtDelay);
                VMCPUID     idCpu = PDMDevHlpGetCurrentCpuId(pDevIns);
                VMCPUSET_ATOMIC_ADD(&pSVGAState->BusyDelayedEmts, idCpu);
                ASMAtomicIncU32(&pSVGAState->cBusyDelayedEmts);
                if (pThis->svga.fBusy)
                {
                    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect); /* hack around lock order issue. */
                    rc = PDMDevHlpVMWaitForDeviceReady(pDevIns, idCpu);
                    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
                    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSect, rcLock);
                }
                ASMAtomicDecU32(&pSVGAState->cBusyDelayedEmts);
                VMCPUSET_ATOMIC_DEL(&pSVGAState->BusyDelayedEmts, idCpu);
# else

                /* Delay the EMT a bit so the FIFO and others can get some work done.
                   This used to be a crude 50 ms sleep. The current code tries to be
                   more efficient, but the consept is still very crude. */
                PVMSVGAR3STATE pSVGAState = pThisCC->svga.pSvgaR3State;
                STAM_REL_PROFILE_START(&pSVGAState->StatBusyDelayEmts, EmtDelay);
                RTThreadYield();
                if (pThis->svga.fBusy)
                {
                    uint32_t cRefs = ASMAtomicIncU32(&pSVGAState->cBusyDelayedEmts);

                    if (pThis->svga.fBusy && cRefs == 1)
                        RTSemEventMultiReset(pSVGAState->hBusyDelayedEmts);
                    if (pThis->svga.fBusy)
                    {
                        /** @todo If this code is going to stay, we need to call into the halt/wait
                         *        code in VMEmt.cpp here, otherwise all kind of EMT interaction will
                         *        suffer when the guest is polling on a busy FIFO. */
                        uint64_t uIgnored1, uIgnored2;
                        uint64_t cNsMaxWait = TMVirtualSyncGetNsToDeadline(PDMDevHlpGetVM(pDevIns), &uIgnored1, &uIgnored2);
                        if (cNsMaxWait >= RT_NS_100US)
                            RTSemEventMultiWaitEx(pSVGAState->hBusyDelayedEmts,
                                                  RTSEMWAIT_FLAGS_NANOSECS | RTSEMWAIT_FLAGS_RELATIVE | RTSEMWAIT_FLAGS_NORESUME,
                                                  RT_MIN(cNsMaxWait, RT_NS_10MS));
                    }

                    ASMAtomicDecU32(&pSVGAState->cBusyDelayedEmts);
                }
                STAM_REL_PROFILE_STOP(&pSVGAState->StatBusyDelayEmts, EmtDelay);
# endif
                *pu32 = pThis->svga.fBusy != 0;
#endif /* IN_RING3 */
            }
            else
                *pu32 = false;
            break;

        case SVGA_REG_GUEST_ID:            /* Set guest OS identifier */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegGuestIdRd);
            *pu32 = pThis->svga.u32GuestId;
            break;

        case SVGA_REG_SCRATCH_SIZE:        /* Number of scratch registers */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegScratchSizeRd);
            *pu32 = pThis->svga.cScratchRegion;
            break;

        case SVGA_REG_MEM_REGS:            /* Number of FIFO registers */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegMemRegsRd);
            *pu32 = SVGA_FIFO_NUM_REGS;
            break;

        case SVGA_REG_PITCHLOCK:           /* Fixed pitch for all modes */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegPitchLockRd);
            *pu32 = pThis->svga.u32PitchLock;
            break;

        case SVGA_REG_IRQMASK:             /* Interrupt mask */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegIrqMaskRd);
            *pu32 = pThis->svga.u32IrqMask;
            break;

        /* See "Guest memory regions" below. */
        case SVGA_REG_GMR_ID:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegGmrIdRd);
            *pu32 = pThis->svga.u32CurrentGMRId;
            break;

        case SVGA_REG_GMR_DESCRIPTOR:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegWriteOnlyRd);
            /* Write only */
            *pu32 = 0;
            break;

        case SVGA_REG_GMR_MAX_IDS:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegGmrMaxIdsRd);
            *pu32 = pThis->svga.cGMR;
            break;

        case SVGA_REG_GMR_MAX_DESCRIPTOR_LENGTH:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegGmrMaxDescriptorLengthRd);
            *pu32 = VMSVGA_MAX_GMR_PAGES;
            break;

        case SVGA_REG_TRACES:            /* Enable trace-based updates even when FIFO is on */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegTracesRd);
            *pu32 = pThis->svga.fTraces;
            break;

        case SVGA_REG_GMRS_MAX_PAGES:    /* Maximum number of 4KB pages for all GMRs */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegGmrsMaxPagesRd);
            *pu32 = VMSVGA_MAX_GMR_PAGES;
            break;

        case SVGA_REG_MEMORY_SIZE:       /* Total dedicated device memory excluding FIFO */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegMemorySizeRd);
            *pu32 = VMSVGA_SURFACE_SIZE;
            break;

        case SVGA_REG_TOP:               /* Must be 1 more than the last register */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegTopRd);
            break;

        /* Mouse cursor support. */
        case SVGA_REG_DEAD: /* SVGA_REG_CURSOR_ID */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegCursorIdRd);
            *pu32 = pThis->svga.uCursorID;
            break;

        case SVGA_REG_CURSOR_X:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegCursorXRd);
            *pu32 = pThis->svga.uCursorX;
            break;

        case SVGA_REG_CURSOR_Y:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegCursorYRd);
            *pu32 = pThis->svga.uCursorY;
            break;

        case SVGA_REG_CURSOR_ON:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegCursorOnRd);
            *pu32 = pThis->svga.uCursorOn;
            break;

        /* Legacy multi-monitor support */
        case SVGA_REG_NUM_GUEST_DISPLAYS:/* Number of guest displays in X/Y direction */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegNumGuestDisplaysRd);
            *pu32 = 1;
            break;

        case SVGA_REG_DISPLAY_ID:        /* Display ID for the following display attributes */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegDisplayIdRd);
            *pu32 = 0;
            break;

        case SVGA_REG_DISPLAY_IS_PRIMARY:/* Whether this is a primary display */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegDisplayIsPrimaryRd);
            *pu32 = 0;
            break;

        case SVGA_REG_DISPLAY_POSITION_X:/* The display position x */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegDisplayPositionXRd);
            *pu32 = 0;
            break;

        case SVGA_REG_DISPLAY_POSITION_Y:/* The display position y */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegDisplayPositionYRd);
            *pu32 = 0;
            break;

        case SVGA_REG_DISPLAY_WIDTH:     /* The display's width */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegDisplayWidthRd);
            *pu32 = pThis->svga.uWidth;
            break;

        case SVGA_REG_DISPLAY_HEIGHT:    /* The display's height */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegDisplayHeightRd);
            *pu32 = pThis->svga.uHeight;
            break;

        case SVGA_REG_NUM_DISPLAYS:        /* (Deprecated) */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegNumDisplaysRd);
            /* We must return something sensible here otherwise the Linux driver
               will take a legacy code path without 3d support.  This number also
               limits how many screens Linux guests will allow. */
            *pu32 = pThis->cMonitors;
            break;

        /*
         * SVGA_CAP_GBOBJECTS+ registers.
         */
        case SVGA_REG_COMMAND_LOW:
            /* Lower 32 bits of command buffer physical address. */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegCommandLowRd);
            *pu32 = pThis->svga.u32RegCommandLow;
            break;

        case SVGA_REG_COMMAND_HIGH:
            /* Upper 32 bits of command buffer PA. */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegCommandHighRd);
            *pu32 = pThis->svga.u32RegCommandHigh;
            break;

        case SVGA_REG_MAX_PRIMARY_BOUNDING_BOX_MEM:
            /* Max primary (screen) memory. */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegMaxPrimBBMemRd);
            *pu32 = pThis->vram_size; /** @todo Maybe half VRAM? */
            break;

        case SVGA_REG_SUGGESTED_GBOBJECT_MEM_SIZE_KB:
            /* Suggested limit on mob mem (i.e. size of the guest mapped VRAM in KB) */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegGBMemSizeRd);
            *pu32 = pThis->vram_size / 1024;
            break;

        case SVGA_REG_DEV_CAP:
            /* Write dev cap index, read value */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegDevCapRd);
            if (pThis->svga.u32DevCapIndex < RT_ELEMENTS(pThis->svga.au32DevCaps))
            {
                RT_UNTRUSTED_VALIDATED_FENCE();
                *pu32 = pThis->svga.au32DevCaps[pThis->svga.u32DevCapIndex];
            }
            else
                *pu32 = 0;
            break;

        case SVGA_REG_CMD_PREPEND_LOW:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegCmdPrependLowRd);
            *pu32 = 0; /* Not supported. */
            break;

        case SVGA_REG_CMD_PREPEND_HIGH:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegCmdPrependHighRd);
            *pu32 = 0; /* Not supported. */
            break;

        case SVGA_REG_SCREENTARGET_MAX_WIDTH:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegScrnTgtMaxWidthRd);
            *pu32 = pThis->svga.u32MaxWidth;
            break;

        case SVGA_REG_SCREENTARGET_MAX_HEIGHT:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegScrnTgtMaxHeightRd);
            *pu32 = pThis->svga.u32MaxHeight;
            break;

        case SVGA_REG_MOB_MAX_SIZE:
            /* Essentially the max texture size */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegMobMaxSizeRd);
            *pu32 = _128M; /** @todo Some actual value. Probably the mapped VRAM size. */
            break;

        case SVGA_REG_BLANK_SCREEN_TARGETS:
            /// @todo STAM_REL_COUNTER_INC(&pThis->svga.aStatRegRd[idxReg]);
            *pu32 = 0; /* Not supported. */
            break;

        case SVGA_REG_CAP2:
            *pu32 = pThis->svga.u32DeviceCaps2;
            break;

        case SVGA_REG_DEVEL_CAP:
            *pu32 = 0; /* Not supported. */
            break;

        /*
         * SVGA_REG_GUEST_DRIVER_* registers require SVGA_CAP2_DX2.
         */
        case SVGA_REG_GUEST_DRIVER_ID:
            *pu32 = pThis->svga.u32GuestDriverId;
            break;

        case SVGA_REG_GUEST_DRIVER_VERSION1:
            *pu32 = pThis->svga.u32GuestDriverVer1;
            break;

        case SVGA_REG_GUEST_DRIVER_VERSION2:
            *pu32 = pThis->svga.u32GuestDriverVer2;
            break;

        case SVGA_REG_GUEST_DRIVER_VERSION3:
            *pu32 = pThis->svga.u32GuestDriverVer3;
            break;

        /*
         * SVGA_REG_CURSOR_ registers require SVGA_CAP2_CURSOR_MOB which the device does not support currently.
         */
        case SVGA_REG_CURSOR_MOBID:
            *pu32 = SVGA_ID_INVALID;
            break;

        case SVGA_REG_CURSOR_MAX_BYTE_SIZE:
            *pu32 = 0;
            break;

        case SVGA_REG_CURSOR_MAX_DIMENSION:
            *pu32 = 0;
            break;

        case SVGA_REG_FIFO_CAPS:
        {
            if (pThis->fVmSvga3)
                *pu32 =   SVGA_FIFO_CAP_FENCE
                        | SVGA_FIFO_CAP_PITCHLOCK
                        | SVGA_FIFO_CAP_CURSOR_BYPASS_3
                        | SVGA_FIFO_CAP_RESERVE
                        | SVGA_FIFO_CAP_GMR2
                        | SVGA_FIFO_CAP_3D_HWVERSION_REVISED
                        | SVGA_FIFO_CAP_SCREEN_OBJECT_2;
            else
                *pu32 = 0;
            break;
        }
        case SVGA_REG_FENCE:
        {
            if (pThis->fVmSvga3)
                *pu32 = pThis->svga.u32FenceLast;
            else
                *pu32 = 0;
            break;
        }

        case SVGA_REG_RESERVED1: /* SVGA_REG_RESERVED* correspond to SVGA_REG_CURSOR4_*. Require SVGA_CAP2_EXTRA_REGS. */
        case SVGA_REG_RESERVED2:
        case SVGA_REG_RESERVED3:
        case SVGA_REG_RESERVED4:
        case SVGA_REG_RESERVED5:
        case SVGA_REG_SCREENDMA:
            *pu32 = 0; /* Not supported. */
            break;

        case SVGA_REG_GBOBJECT_MEM_SIZE_KB:
            /** @todo "The maximum amount of guest-backed objects that the device can have resident at a time" */
            *pu32 = _1G / _1K;
            break;

        case SVGA_REG_IRQ_STATUS:
        {
            if (pThis->fVmSvga3)
                *pu32 = pThis->svga.u32IrqStatus;
            else
                *pu32 = 0;
            break;
        }

        default:
        {
            uint32_t offReg;
            if ((offReg = idxReg - SVGA_SCRATCH_BASE) < pThis->svga.cScratchRegion)
            {
                STAM_REL_COUNTER_INC(&pThis->svga.StatRegScratchRd);
                RT_UNTRUSTED_VALIDATED_FENCE();
                *pu32 = pThis->svga.au32ScratchRegion[offReg];
            }
            else if ((offReg = idxReg - SVGA_PALETTE_BASE) < (uint32_t)SVGA_NUM_PALETTE_REGS)
            {
                /* Note! Using last_palette rather than palette here to preserve the VGA one. */
                STAM_REL_COUNTER_INC(&pThis->svga.StatRegPaletteRd);
                RT_UNTRUSTED_VALIDATED_FENCE();
                uint32_t u32 = pThis->last_palette[offReg / 3];
                switch (offReg % 3)
                {
                    case 0: *pu32 = (u32 >> 16) & 0xff; break; /* red */
                    case 1: *pu32 = (u32 >>  8) & 0xff; break; /* green */
                    case 2: *pu32 =  u32        & 0xff; break; /* blue */
                }
            }
            else
            {
#if !defined(IN_RING3) && defined(VBOX_STRICT)
                rc = VINF_IOM_R3_IOPORT_READ;
#else
                STAM_REL_COUNTER_INC(&pThis->svga.StatRegUnknownRd);

                /* Do not assert. The guest might be reading all registers. */
                LogFunc(("Unknown reg=%#x\n", idxReg));
#endif
            }
            break;
        }
    }
    LogFlow(("vmsvgaReadPort index=%s (%d) val=%#x rc=%x\n", vmsvgaIndexToString(pThis, idxReg), idxReg, *pu32, rc));
    return rc;
}

#ifdef IN_RING3
/**
 * Apply the current resolution settings to change the video mode.
 *
 * @returns VBox status code.
 * @param   pThis       The shared VGA state.
 * @param   pThisCC     The ring-3 VGA state.
 */
int vmsvgaR3ChangeMode(PVGASTATE pThis, PVGASTATECC pThisCC)
{
    /* Always do changemode on FIFO thread. */
    Assert(RTThreadSelf() == pThisCC->svga.pFIFOIOThread->Thread);

    PVMSVGAR3STATE pSVGAState = pThisCC->svga.pSvgaR3State;

    pThisCC->pDrv->pfnLFBModeChange(pThisCC->pDrv, true);

    if (pThis->svga.fGFBRegisters)
    {
        /* "For backwards compatibility, when the GFB mode registers (WIDTH,
         * HEIGHT, PITCHLOCK, BITS_PER_PIXEL) are modified, the SVGA device
         * deletes all screens other than screen #0, and redefines screen
         * #0 according to the specified mode. Drivers that use
         * SVGA_CMD_DEFINE_SCREEN should destroy or redefine screen #0."
         */

        VMSVGASCREENOBJECT *pScreen = &pSVGAState->aScreens[0];
        Assert(pScreen->idScreen == 0);

        if (   pScreen->cWidth  == VMSVGA_VAL_UNINITIALIZED
            || pScreen->cHeight == VMSVGA_VAL_UNINITIALIZED
            || pScreen->cBpp    == VMSVGA_VAL_UNINITIALIZED)
        {
            /* Do not apply the change if the guest has not finished updating registers.
             * This is necessary in order to make a full mode change, including freeing
             * pvScreenBitmap buffers for screen 0 if necessary.
             */
            return VINF_SUCCESS;
        }

        /* Remember screen bitmap buffers to be freed. */
        void * apvOldScreenBitmap[RT_ELEMENTS(pSVGAState->aScreens)];
        RT_ZERO(apvOldScreenBitmap);

        pScreen->fDefined  = true;
        pScreen->fModified = true;
        pScreen->fuScreen  = SVGA_SCREEN_MUST_BE_SET | SVGA_SCREEN_IS_PRIMARY;
        pScreen->xOrigin   = 0;
        pScreen->yOrigin   = 0;
        pScreen->offVRAM   = 0;
        pScreen->cbPitch   = pThis->svga.cbScanline;
        pScreen->cWidth    = pThis->svga.uWidth;
        pScreen->cHeight   = pThis->svga.uHeight;
        pScreen->cBpp      = pThis->svga.uBpp;
        pScreen->cDpi      = 0; /* GFB mode does not support dpi. */
        /* GFB mode uses the guest VRAM. The screen bitmap must be deallocated after 'vmsvgaR3VBVAResize'. */
        apvOldScreenBitmap[0] = pScreen->pvScreenBitmap;
        /* Set pvScreenBitmap to zero because if it is not, then vmsvgaR3VBVAResize uses it as VRAM address. */
        pScreen->pvScreenBitmap = 0;

        for (unsigned iScreen = 1; iScreen < RT_ELEMENTS(pSVGAState->aScreens); ++iScreen)
        {
            /* Delete screen. */
            pScreen = &pSVGAState->aScreens[iScreen];
            if (pScreen->fDefined)
            {
                pScreen->fModified = true;
                pScreen->fDefined = false;

#ifdef VBOX_WITH_VMSVGA3D
                if (RT_LIKELY(pThis->svga.f3DEnabled))
                    vmsvga3dDestroyScreen(pThisCC, pScreen);
#endif
                apvOldScreenBitmap[iScreen] = pScreen->pvScreenBitmap;
                pScreen->pvScreenBitmap = 0;
            }
        }

        vmsvgaR3VBVAResize(pThis, pThisCC);

        /* Deallocate screen bitmaps for all screens because GFB mode uses the guest VRAM. */
        for (unsigned iScreen = 0; iScreen < RT_ELEMENTS(apvOldScreenBitmap); ++iScreen)
            RTMemFree(apvOldScreenBitmap[iScreen]);
    }
    else
    {
        /* "If Screen Objects are supported, they can be used to fully
         * replace the functionality provided by the framebuffer registers
         * (SVGA_REG_WIDTH, HEIGHT, etc.) and by SVGA_CAP_DISPLAY_TOPOLOGY."
         */
        pThis->svga.uWidth  = VMSVGA_VAL_UNINITIALIZED;
        pThis->svga.uHeight = VMSVGA_VAL_UNINITIALIZED;
        pThis->svga.uBpp    = pThis->svga.uHostBpp;

        vmsvgaR3VBVAResize(pThis, pThisCC);
    }

    /* Last stuff. For the VGA device screenshot. */
    pThis->last_bpp        = pSVGAState->aScreens[0].cBpp;
    pThis->last_scr_width  = pSVGAState->aScreens[0].cWidth;
    pThis->last_scr_height = pSVGAState->aScreens[0].cHeight;
    pThis->last_width      = pSVGAState->aScreens[0].cWidth;
    pThis->last_height     = pSVGAState->aScreens[0].cHeight;

    /* vmsvgaPortSetViewPort not called after state load; set sensible defaults. */
    if (    pThis->svga.viewport.cx == 0
        &&  pThis->svga.viewport.cy == 0)
    {
        pThis->svga.viewport.cx      = pSVGAState->aScreens[0].cWidth;
        pThis->svga.viewport.xRight  = pSVGAState->aScreens[0].cWidth;
        pThis->svga.viewport.cy      = pSVGAState->aScreens[0].cHeight;
        pThis->svga.viewport.yHighWC = pSVGAState->aScreens[0].cHeight;
        pThis->svga.viewport.yLowWC  = 0;
    }

    return VINF_SUCCESS;
}

int vmsvgaR3UpdateScreen(PVGASTATECC pThisCC, VMSVGASCREENOBJECT *pScreen, int x, int y, int w, int h)
{
    ASSERT_GUEST_LOGREL_MSG_RETURN(w > 0 && h > 0,
                                   ("vmsvgaR3UpdateScreen: screen %d (%d,%d) %dx%d: Invalid height and/or width supplied.\n",
                                   pScreen->idScreen, x, y, w, h),
                                   VERR_INVALID_PARAMETER);

    VBVACMDHDR cmd;
    cmd.x = (int16_t)(pScreen->xOrigin + x);
    cmd.y = (int16_t)(pScreen->yOrigin + y);
    cmd.w = (uint16_t)w;
    cmd.h = (uint16_t)h;

    pThisCC->pDrv->pfnVBVAUpdateBegin(pThisCC->pDrv, pScreen->idScreen);
    pThisCC->pDrv->pfnVBVAUpdateProcess(pThisCC->pDrv, pScreen->idScreen, &cmd, sizeof(cmd));
    pThisCC->pDrv->pfnVBVAUpdateEnd(pThisCC->pDrv, pScreen->idScreen,
                                    pScreen->xOrigin + x, pScreen->yOrigin + y, w, h);

    return VINF_SUCCESS;
}

#endif /* IN_RING3 */
#if defined(IN_RING0) || defined(IN_RING3)

/**
 * Safely updates the SVGA_FIFO_BUSY register (in shared memory).
 *
 * @param   pThis       The shared VGA/VMSVGA instance data.
 * @param   pThisCC     The VGA/VMSVGA state for the current context.
 * @param   fState      The busy state.
 */
DECLINLINE(void) vmsvgaHCSafeFifoBusyRegUpdate(PVGASTATE pThis, PVGASTATECC pThisCC, bool fState)
{
    ASMAtomicWriteU32(&pThisCC->svga.pau32FIFO[SVGA_FIFO_BUSY], fState);

    if (RT_UNLIKELY(fState != (pThis->svga.fBusy != 0)))
    {
        /* Race / unfortunately scheduling. Highly unlikly. */
        uint32_t cLoops = 64;
        do
        {
            ASMNopPause();
            fState = (pThis->svga.fBusy != 0);
            ASMAtomicWriteU32(&pThisCC->svga.pau32FIFO[SVGA_FIFO_BUSY], fState != 0);
        } while (cLoops-- > 0 && fState != (pThis->svga.fBusy != 0));
    }
}


/**
 * Update the scanline pitch in response to the guest changing mode
 * width/bpp.
 *
 * @param   pThis       The shared VGA/VMSVGA state.
 * @param   pThisCC     The VGA/VMSVGA state for the current context.
 */
DECLINLINE(void) vmsvgaHCUpdatePitch(PVGASTATE pThis, PVGASTATECC pThisCC)
{
    uint32_t RT_UNTRUSTED_VOLATILE_GUEST *pFIFO = pThisCC->svga.pau32FIFO;
    uint32_t uFifoPitchLock = pThis->fVmSvga3 ? 0 : pFIFO[SVGA_FIFO_PITCHLOCK];
    uint32_t uRegPitchLock  = pThis->svga.u32PitchLock;
    uint32_t uFifoMin       = pThis->fVmSvga3 ? 0 : pFIFO[SVGA_FIFO_MIN];

    /* The SVGA_FIFO_PITCHLOCK register is only valid if SVGA_FIFO_MIN points past
     * it. If SVGA_FIFO_MIN is small, there may well be data at the SVGA_FIFO_PITCHLOCK
     * location but it has a different meaning.
     */
    if ((uFifoMin / sizeof(uint32_t)) <= SVGA_FIFO_PITCHLOCK)
        uFifoPitchLock = 0;

    /* Sanitize values. */
    if ((uFifoPitchLock < 200) || (uFifoPitchLock > 32768))
        uFifoPitchLock = 0;
    if ((uRegPitchLock  < 200) || (uRegPitchLock  > 32768))
        uRegPitchLock = 0;

    /* Prefer the register value to the FIFO value.*/
    if (uRegPitchLock)
        pThis->svga.cbScanline = uRegPitchLock;
    else if (uFifoPitchLock)
        pThis->svga.cbScanline = uFifoPitchLock;
    else
        pThis->svga.cbScanline = (uint32_t)pThis->svga.uWidth * (RT_ALIGN(pThis->svga.uBpp, 8) / 8);

    if ((uFifoMin / sizeof(uint32_t)) <= SVGA_FIFO_PITCHLOCK)
        pThis->svga.u32PitchLock = pThis->svga.cbScanline;
}

#endif /* IN_RING0 || IN_RING3 */

#ifdef IN_RING3

/**
 * Sends cursor position and visibility information from legacy
 * SVGA registers to the front-end.
 */
static void vmsvgaR3RegUpdateCursor(PVGASTATECC pThisCC, PVGASTATE pThis, uint32_t uCursorOn)
{
    /*
     * Writing the X/Y/ID registers does not trigger changes; only writing the
     * SVGA_REG_CURSOR_ON register does. That minimizes the overhead.
     * We boldly assume that guests aren't stupid and aren't writing the CURSOR_ON
     * register if they don't have to.
     */
    uint32_t x, y, idScreen;
    uint32_t fFlags = VBVA_CURSOR_VALID_DATA;

    x = pThis->svga.uCursorX;
    y = pThis->svga.uCursorY;
    idScreen = SVGA_ID_INVALID; /* The old register interface is single screen only. */

    /* The original values for SVGA_REG_CURSOR_ON were off (0) and on (1); later, the values
     * were extended as follows:
     *
     *   SVGA_CURSOR_ON_HIDE               0
     *   SVGA_CURSOR_ON_SHOW               1
     *   SVGA_CURSOR_ON_REMOVE_FROM_FB     2 - cursor on but not in the framebuffer
     *   SVGA_CURSOR_ON_RESTORE_TO_FB      3 - cursor on, possibly in the framebuffer
     *
     * Since we never draw the cursor into the guest's framebuffer, we do not need to
     * distinguish between the non-zero values but still remember them.
     */
    if (RT_BOOL(pThis->svga.uCursorOn) != RT_BOOL(uCursorOn))
    {
        LogRel2(("vmsvgaR3RegUpdateCursor: uCursorOn %d prev CursorOn %d (%d,%d)\n", uCursorOn, pThis->svga.uCursorOn, x, y));
        pThisCC->pDrv->pfnVBVAMousePointerShape(pThisCC->pDrv, RT_BOOL(uCursorOn), false, 0, 0, 0, 0, NULL);
    }
    pThis->svga.uCursorOn = uCursorOn;
    pThisCC->pDrv->pfnVBVAReportCursorPosition(pThisCC->pDrv, fFlags, idScreen, x, y);
}

#endif /* IN_RING3 */


/**
 * Write port register
 *
 * @returns Strict VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The shared VGA/VMSVGA state.
 * @param   pThisCC     The VGA/VMSVGA state for the current context.
 * @param   idxReg      Rge register index being written.
 * @param   u32         Value to write
 */
static VBOXSTRICTRC vmsvgaWritePort(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC, uint32_t idxReg, uint32_t u32)
{
#ifdef IN_RING3
    PVMSVGAR3STATE pSVGAState = pThisCC->svga.pSvgaR3State;
#endif
    VBOXSTRICTRC rc = VINF_SUCCESS;
    RT_NOREF(pThisCC);

    /* We must adjust the register number if we're in SVGA_ID_0 mode because the PALETTE range moved. */
    if (   idxReg >= SVGA_REG_ID_0_TOP
        && pThis->svga.u32SVGAId == SVGA_ID_0)
    {
        idxReg += SVGA_PALETTE_BASE - SVGA_REG_ID_0_TOP;
        Log(("vmsvgaWritePort: SVGA_ID_0 reg adj %#x -> %#x\n", pThis->svga.u32IndexReg, idxReg));
    }
#ifdef LOG_ENABLED
    if (idxReg != SVGA_REG_DEV_CAP)
        LogFlow(("vmsvgaWritePort index=%s (%d) val=%#x\n", vmsvgaIndexToString(pThis, idxReg), idxReg, u32));
    else
        LogFlow(("vmsvgaWritePort index=%s (%d) val=%s (%d)\n", vmsvgaIndexToString(pThis, idxReg), idxReg, vmsvgaDevCapIndexToString((SVGA3dDevCapIndex)u32), u32));
#endif
    /* Check if the guest uses legacy registers. See vmsvgaR3ChangeMode */
    switch (idxReg)
    {
        case SVGA_REG_WIDTH:
        case SVGA_REG_HEIGHT:
        case SVGA_REG_PITCHLOCK:
        case SVGA_REG_BITS_PER_PIXEL:
            pThis->svga.fGFBRegisters = true;
            break;
        default:
            break;
    }

    switch (idxReg)
    {
        case SVGA_REG_ID:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegIdWr);
            if (   u32 == SVGA_ID_0
                || u32 == SVGA_ID_1
                || u32 == SVGA_ID_2
                || u32 == SVGA_ID_3)
                pThis->svga.u32SVGAId = u32;
            else
                PDMDevHlpDBGFStop(pDevIns, RT_SRC_POS, "Trying to set SVGA_REG_ID to %#x (%d)\n", u32, u32);
            break;

        case SVGA_REG_ENABLE:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegEnableWr);
#ifdef IN_RING3
            if (    (u32 & SVGA_REG_ENABLE_ENABLE)
                &&  pThis->svga.fEnabled == false)
            {
                /* Make a backup copy of the first 512kb in order to save font data etc. */
                /** @todo should probably swap here, rather than copy + zero */
                memcpy(pThisCC->svga.pbVgaFrameBufferR3, pThisCC->pbVRam, VMSVGA_VGA_FB_BACKUP_SIZE);
                memset(pThisCC->pbVRam, 0, VMSVGA_VGA_FB_BACKUP_SIZE);
            }

            pThis->svga.fEnabled = u32;
            if (pThis->svga.fEnabled)
            {
                if (    pThis->svga.uWidth  == VMSVGA_VAL_UNINITIALIZED
                    &&  pThis->svga.uHeight == VMSVGA_VAL_UNINITIALIZED)
                {
                    /* Keep the current mode. */
                    pThis->svga.uWidth  = pThisCC->pDrv->cx;
                    pThis->svga.uHeight = pThisCC->pDrv->cy;
                    pThis->svga.uBpp    = (pThisCC->pDrv->cBits + 7) & ~7;
                    vmsvgaHCUpdatePitch(pThis, pThisCC);
                }

                if (    pThis->svga.uWidth  != VMSVGA_VAL_UNINITIALIZED
                    &&  pThis->svga.uHeight != VMSVGA_VAL_UNINITIALIZED)
                    ASMAtomicOrU32(&pThis->svga.u32ActionFlags, VMSVGA_ACTION_CHANGEMODE);
# ifdef LOG_ENABLED
                if (!pThis->fVmSvga3)
                {
                    uint32_t *pFIFO = pThisCC->svga.pau32FIFO;
                    Log(("configured=%d busy=%d\n", pThis->svga.fConfigured, pFIFO[SVGA_FIFO_BUSY]));
                    Log(("next %x stop %x\n", pFIFO[SVGA_FIFO_NEXT_CMD], pFIFO[SVGA_FIFO_STOP]));
                }
# endif

                /* Disable or enable dirty page tracking according to the current fTraces value. */
                vmsvgaR3SetTraces(pDevIns, pThis, !!pThis->svga.fTraces);

                /* bird: Whatever this is was added to make screenshot work, ask sunlover should explain... */
                for (uint32_t idScreen = 0; idScreen < pThis->cMonitors; ++idScreen)
                    pThisCC->pDrv->pfnVBVAEnable(pThisCC->pDrv, idScreen, NULL /*pHostFlags*/);

                /* Make the cursor visible again as needed. */
                if (pSVGAState->Cursor.fActive)
                    pThisCC->pDrv->pfnVBVAMousePointerShape(pThisCC->pDrv, true /*fVisible*/, false, 0, 0, 0, 0, NULL);
            }
            else
            {
                /* Make sure the cursor is off. */
                if (pSVGAState->Cursor.fActive)
                    pThisCC->pDrv->pfnVBVAMousePointerShape(pThisCC->pDrv, false /*fVisible*/, false, 0, 0, 0, 0, NULL);

                /* Restore the text mode backup. */
                memcpy(pThisCC->pbVRam, pThisCC->svga.pbVgaFrameBufferR3, VMSVGA_VGA_FB_BACKUP_SIZE);

                pThisCC->pDrv->pfnLFBModeChange(pThisCC->pDrv, false);

                /* Enable dirty page tracking again when going into legacy mode. */
                vmsvgaR3SetTraces(pDevIns, pThis, true);

                /* bird: Whatever this is was added to make screenshot work, ask sunlover should explain... */
                for (uint32_t idScreen = 0; idScreen < pThis->cMonitors; ++idScreen)
                    pThisCC->pDrv->pfnVBVADisable(pThisCC->pDrv, idScreen);

                /* Clear the pitch lock. */
                pThis->svga.u32PitchLock = 0;
            }
#else  /* !IN_RING3 */
            rc = VINF_IOM_R3_IOPORT_WRITE;
#endif /* !IN_RING3 */
            break;

        case SVGA_REG_WIDTH:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegWidthWr);
            if (u32 != pThis->svga.uWidth)
            {
                if (u32 <= pThis->svga.u32MaxWidth)
                {
#if defined(IN_RING3) || defined(IN_RING0)
                    pThis->svga.uWidth = u32;
                    vmsvgaHCUpdatePitch(pThis, pThisCC);
                    if (pThis->svga.fEnabled)
                        ASMAtomicOrU32(&pThis->svga.u32ActionFlags, VMSVGA_ACTION_CHANGEMODE);
#else
                    rc = VINF_IOM_R3_IOPORT_WRITE;
#endif
                }
                else
                    Log(("SVGA_REG_WIDTH: New value is out of bounds: %u, max %u\n", u32, pThis->svga.u32MaxWidth));
            }
            /* else: nop */
            break;

        case SVGA_REG_HEIGHT:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegHeightWr);
            if (u32 != pThis->svga.uHeight)
            {
                if (u32 <= pThis->svga.u32MaxHeight)
                {
                    pThis->svga.uHeight = u32;
                    if (pThis->svga.fEnabled)
                        ASMAtomicOrU32(&pThis->svga.u32ActionFlags, VMSVGA_ACTION_CHANGEMODE);
                }
                else
                    Log(("SVGA_REG_HEIGHT: New value is out of bounds: %u, max %u\n", u32, pThis->svga.u32MaxHeight));
            }
            /* else: nop */
            break;

        case SVGA_REG_DEPTH:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegDepthWr);
            /** @todo read-only?? */
            break;

        case SVGA_REG_BITS_PER_PIXEL:      /* Current bpp in the guest */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegBitsPerPixelWr);
            if (pThis->svga.uBpp != u32)
            {
                if (u32 <= 32)
                {
#if defined(IN_RING3) || defined(IN_RING0)
                    pThis->svga.uBpp = u32;
                    vmsvgaHCUpdatePitch(pThis, pThisCC);
                    if (pThis->svga.fEnabled)
                        ASMAtomicOrU32(&pThis->svga.u32ActionFlags, VMSVGA_ACTION_CHANGEMODE);
#else
                    rc = VINF_IOM_R3_IOPORT_WRITE;
#endif
                }
                else
                    Log(("SVGA_REG_BITS_PER_PIXEL: New value is out of bounds: %u, max 32\n", u32));
            }
            /* else: nop */
            break;

        case SVGA_REG_PSEUDOCOLOR:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegPseudoColorWr);
            break;

        case SVGA_REG_CONFIG_DONE:         /* Set when memory area configured */
#ifdef IN_RING3
            STAM_REL_COUNTER_INC(&pSVGAState->StatR3RegConfigDoneWr);
            pThis->svga.fConfigured = u32;
            /* Disabling the FIFO enables tracing (dirty page detection) by default. */
            if (!pThis->svga.fConfigured)
                pThis->svga.fTraces = true;
            vmsvgaR3SetTraces(pDevIns, pThis, !!pThis->svga.fTraces);
#else
            rc = VINF_IOM_R3_IOPORT_WRITE;
#endif
            break;

        case SVGA_REG_SYNC:                /* See "FIFO Synchronization Registers" */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegSyncWr);
            if (    pThis->svga.fEnabled
                &&  pThis->svga.fConfigured)
            {
#if defined(IN_RING3) || defined(IN_RING0)
                Log(("SVGA_REG_SYNC: SVGA_FIFO_BUSY=%d\n", pThisCC->svga.pau32FIFO[SVGA_FIFO_BUSY]));
                /*
                 * The VMSVGA_BUSY_F_EMT_FORCE flag makes sure we will check if the FIFO is empty
                 * at least once; VMSVGA_BUSY_F_FIFO alone does not ensure that.
                 */
                ASMAtomicWriteU32(&pThis->svga.fBusy, VMSVGA_BUSY_F_EMT_FORCE | VMSVGA_BUSY_F_FIFO);
                if (VMSVGA_IS_VALID_FIFO_REG(SVGA_FIFO_BUSY, pThisCC->svga.pau32FIFO[SVGA_FIFO_MIN]))
                    vmsvgaHCSafeFifoBusyRegUpdate(pThis, pThisCC, true);

                /* Kick the FIFO thread to start processing commands again. */
                PDMDevHlpSUPSemEventSignal(pDevIns, pThis->svga.hFIFORequestSem);
#else
                rc = VINF_IOM_R3_IOPORT_WRITE;
#endif
            }
            /* else nothing to do. */
            else
                Log(("Sync ignored enabled=%d configured=%d\n", pThis->svga.fEnabled, pThis->svga.fConfigured));

            break;

        case SVGA_REG_BUSY:                /* See "FIFO Synchronization Registers" (read-only) */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegBusyWr);
            break;

        case SVGA_REG_GUEST_ID:            /* Set guest OS identifier */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegGuestIdWr);
            pThis->svga.u32GuestId = u32;
            break;

        case SVGA_REG_PITCHLOCK:           /* Fixed pitch for all modes */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegPitchLockWr);
            pThis->svga.u32PitchLock = u32;
            /* Should this also update the FIFO pitch lock? Unclear. */
            break;

        case SVGA_REG_IRQMASK:             /* Interrupt mask */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegIrqMaskWr);
            pThis->svga.u32IrqMask = u32;

            /* Irq pending after the above change? */
            if (pThis->svga.u32IrqStatus & u32)
            {
                Log(("SVGA_REG_IRQMASK: Trigger interrupt with status %x\n", pThis->svga.u32IrqStatus));
                PDMDevHlpPCISetIrqNoWait(pDevIns, 0, 1);
            }
            else
                PDMDevHlpPCISetIrqNoWait(pDevIns, 0, 0);
            break;

        /* Mouse cursor support */
        case SVGA_REG_DEAD: /* SVGA_REG_CURSOR_ID */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegCursorIdWr);
            pThis->svga.uCursorID = u32;
            break;

        case SVGA_REG_CURSOR_X:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegCursorXWr);
            pThis->svga.uCursorX = u32;
            break;

        case SVGA_REG_CURSOR_Y:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegCursorYWr);
            pThis->svga.uCursorY = u32;
            break;

        case SVGA_REG_CURSOR_ON:
#ifdef IN_RING3
            /* The cursor is only updated when SVGA_REG_CURSOR_ON is written. */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegCursorOnWr);
            vmsvgaR3RegUpdateCursor(pThisCC, pThis, u32);
#else
            rc = VINF_IOM_R3_IOPORT_WRITE;
#endif
            break;

        /* Legacy multi-monitor support */
        case SVGA_REG_NUM_GUEST_DISPLAYS:/* Number of guest displays in X/Y direction */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegNumGuestDisplaysWr);
            break;
        case SVGA_REG_DISPLAY_ID:        /* Display ID for the following display attributes */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegDisplayIdWr);
            break;
        case SVGA_REG_DISPLAY_IS_PRIMARY:/* Whether this is a primary display */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegDisplayIsPrimaryWr);
            break;
        case SVGA_REG_DISPLAY_POSITION_X:/* The display position x */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegDisplayPositionXWr);
            break;
        case SVGA_REG_DISPLAY_POSITION_Y:/* The display position y */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegDisplayPositionYWr);
            break;
        case SVGA_REG_DISPLAY_WIDTH:     /* The display's width */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegDisplayWidthWr);
            break;
        case SVGA_REG_DISPLAY_HEIGHT:    /* The display's height */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegDisplayHeightWr);
            break;
#ifdef VBOX_WITH_VMSVGA3D
        /* See "Guest memory regions" below. */
        case SVGA_REG_GMR_ID:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegGmrIdWr);
            pThis->svga.u32CurrentGMRId = u32;
            break;

        case SVGA_REG_GMR_DESCRIPTOR:
# ifndef IN_RING3
            rc = VINF_IOM_R3_IOPORT_WRITE;
            break;
# else /* IN_RING3 */
        {
            STAM_REL_COUNTER_INC(&pSVGAState->StatR3RegGmrDescriptorWr);

            /* Validate current GMR id. */
            uint32_t idGMR = pThis->svga.u32CurrentGMRId;
            AssertBreak(idGMR < pThis->svga.cGMR);
            RT_UNTRUSTED_VALIDATED_FENCE();

            /* Free the old GMR if present. */
            vmsvgaR3GmrFree(pThisCC, idGMR);

            /* Just undefine the GMR? */
            RTGCPHYS GCPhys = (RTGCPHYS)u32 << GUEST_PAGE_SHIFT;
            if (GCPhys == 0)
            {
                STAM_REL_COUNTER_INC(&pSVGAState->StatR3RegGmrDescriptorWrFree);
                break;
            }


            /* Never cross a page boundary automatically. */
            const uint32_t          cMaxPages   = RT_MIN(VMSVGA_MAX_GMR_PAGES, UINT32_MAX / X86_PAGE_SIZE);
            uint32_t                cPagesTotal = 0;
            uint32_t                iDesc       = 0;
            PVMSVGAGMRDESCRIPTOR    paDescs     = NULL;
            uint32_t                cLoops      = 0;
            RTGCPHYS                GCPhysBase  = GCPhys;
            while ((GCPhys >> GUEST_PAGE_SHIFT) == (GCPhysBase >> GUEST_PAGE_SHIFT))
            {
                /* Read descriptor. */
                SVGAGuestMemDescriptor desc;
                rc = PDMDevHlpPCIPhysRead(pDevIns, GCPhys, &desc, sizeof(desc));
                AssertRCBreak(VBOXSTRICTRC_VAL(rc));

                if (desc.numPages != 0)
                {
                    AssertBreakStmt(desc.numPages <= cMaxPages, rc = VERR_OUT_OF_RANGE);
                    cPagesTotal += desc.numPages;
                    AssertBreakStmt(cPagesTotal   <= cMaxPages, rc = VERR_OUT_OF_RANGE);

                    if ((iDesc & 15) == 0)
                    {
                        void *pvNew = RTMemRealloc(paDescs, (iDesc + 16) * sizeof(VMSVGAGMRDESCRIPTOR));
                        AssertBreakStmt(pvNew, rc = VERR_NO_MEMORY);
                        paDescs = (PVMSVGAGMRDESCRIPTOR)pvNew;
                    }

                    paDescs[iDesc].GCPhys     = (RTGCPHYS)desc.ppn << GUEST_PAGE_SHIFT;
                    paDescs[iDesc++].numPages = desc.numPages;

                    /* Continue with the next descriptor. */
                    GCPhys += sizeof(desc);
                }
                else if (desc.ppn == 0)
                    break;  /* terminator */
                else /* Pointer to the next physical page of descriptors. */
                    GCPhys = GCPhysBase = (RTGCPHYS)desc.ppn << GUEST_PAGE_SHIFT;

                cLoops++;
                AssertBreakStmt(cLoops < VMSVGA_MAX_GMR_DESC_LOOP_COUNT, rc = VERR_OUT_OF_RANGE);
            }

            AssertStmt(iDesc > 0 || RT_FAILURE_NP(rc), rc = VERR_OUT_OF_RANGE);
            if (RT_SUCCESS(rc))
            {
                /* Commit the GMR. */
                pSVGAState->paGMR[idGMR].paDesc         = paDescs;
                pSVGAState->paGMR[idGMR].numDescriptors = iDesc;
                pSVGAState->paGMR[idGMR].cMaxPages      = cPagesTotal;
                pSVGAState->paGMR[idGMR].cbTotal        = cPagesTotal * GUEST_PAGE_SIZE;
                Assert((pSVGAState->paGMR[idGMR].cbTotal >> GUEST_PAGE_SHIFT) == cPagesTotal);
                Log(("Defined new gmr %x numDescriptors=%d cbTotal=%x (%#x pages)\n",
                     idGMR, iDesc, pSVGAState->paGMR[idGMR].cbTotal, cPagesTotal));
            }
            else
            {
                RTMemFree(paDescs);
                STAM_REL_COUNTER_INC(&pSVGAState->StatR3RegGmrDescriptorWrErrors);
            }
            break;
        }
# endif /* IN_RING3 */
#endif // VBOX_WITH_VMSVGA3D

        case SVGA_REG_TRACES:            /* Enable trace-based updates even when FIFO is on */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegTracesWr);
            if (pThis->svga.fTraces == u32)
                break; /* nothing to do */

#ifdef IN_RING3
            vmsvgaR3SetTraces(pDevIns, pThis, !!u32);
#else
            rc = VINF_IOM_R3_IOPORT_WRITE;
#endif
            break;

        case SVGA_REG_TOP:               /* Must be 1 more than the last register */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegTopWr);
            break;

        case SVGA_REG_NUM_DISPLAYS:        /* (Deprecated) */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegNumDisplaysWr);
            Log(("Write to deprecated register %x - val %x ignored\n", idxReg, u32));
            break;

        /*
         * SVGA_CAP_GBOBJECTS+ registers.
         */
        case SVGA_REG_COMMAND_LOW:
        {
            /* Lower 32 bits of command buffer physical address and submit the command buffer. */
#ifdef IN_RING3
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegCommandLowWr);
            pThis->svga.u32RegCommandLow = u32;

            /* "lower 6 bits are used for the SVGACBContext" */
            RTGCPHYS GCPhysCB = pThis->svga.u32RegCommandHigh;
            GCPhysCB <<= 32;
            GCPhysCB |= pThis->svga.u32RegCommandLow & ~SVGA_CB_CONTEXT_MASK;
            SVGACBContext const CBCtx = (SVGACBContext)(pThis->svga.u32RegCommandLow & SVGA_CB_CONTEXT_MASK);
            vmsvgaR3CmdBufSubmit(pDevIns, pThis, pThisCC, GCPhysCB, CBCtx);
#else
            rc = VINF_IOM_R3_IOPORT_WRITE;
#endif
            break;
        }

        case SVGA_REG_COMMAND_HIGH:
            /* Upper 32 bits of command buffer PA. */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegCommandHighWr);
            pThis->svga.u32RegCommandHigh = u32;
            break;

        case SVGA_REG_DEV_CAP:
            /* Write dev cap index, read value */
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegDevCapWr);
            pThis->svga.u32DevCapIndex = u32;
            break;

        case SVGA_REG_CMD_PREPEND_LOW:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegCmdPrependLowWr);
            /* Not supported. */
            break;

        case SVGA_REG_CMD_PREPEND_HIGH:
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegCmdPrependHighWr);
            /* Not supported. */
            break;

        case SVGA_REG_GUEST_DRIVER_ID:
            if (u32 != SVGA_REG_GUEST_DRIVER_ID_SUBMIT)
                pThis->svga.u32GuestDriverId = u32;
            break;

        case SVGA_REG_GUEST_DRIVER_VERSION1:
            pThis->svga.u32GuestDriverVer1 = u32;
            break;

        case SVGA_REG_GUEST_DRIVER_VERSION2:
            pThis->svga.u32GuestDriverVer2 = u32;
            break;

        case SVGA_REG_GUEST_DRIVER_VERSION3:
            pThis->svga.u32GuestDriverVer3 = u32;
            break;

        case SVGA_REG_CURSOR_MOBID:
            /* Not supported, ignore. See correspondent comments in vmsvgaReadPort. */
            break;

        case SVGA_REG_FB_START:
        case SVGA_REG_MEM_START:
        case SVGA_REG_HOST_BITS_PER_PIXEL:
        case SVGA_REG_MAX_WIDTH:
        case SVGA_REG_MAX_HEIGHT:
        case SVGA_REG_VRAM_SIZE:
        case SVGA_REG_FB_SIZE:
        case SVGA_REG_CAPABILITIES:
        case SVGA_REG_MEM_SIZE:
        case SVGA_REG_SCRATCH_SIZE:        /* Number of scratch registers */
        case SVGA_REG_MEM_REGS:            /* Number of FIFO registers */
        case SVGA_REG_BYTES_PER_LINE:
        case SVGA_REG_FB_OFFSET:
        case SVGA_REG_RED_MASK:
        case SVGA_REG_GREEN_MASK:
        case SVGA_REG_BLUE_MASK:
        case SVGA_REG_GMRS_MAX_PAGES:    /* Maximum number of 4KB pages for all GMRs */
        case SVGA_REG_MEMORY_SIZE:       /* Total dedicated device memory excluding FIFO */
        case SVGA_REG_GMR_MAX_IDS:
        case SVGA_REG_GMR_MAX_DESCRIPTOR_LENGTH:
        case SVGA_REG_MAX_PRIMARY_BOUNDING_BOX_MEM:
        case SVGA_REG_SUGGESTED_GBOBJECT_MEM_SIZE_KB:
        case SVGA_REG_SCREENTARGET_MAX_WIDTH:
        case SVGA_REG_SCREENTARGET_MAX_HEIGHT:
        case SVGA_REG_MOB_MAX_SIZE:
        case SVGA_REG_BLANK_SCREEN_TARGETS:
        case SVGA_REG_CAP2:
        case SVGA_REG_DEVEL_CAP:
        case SVGA_REG_CURSOR_MAX_BYTE_SIZE:
        case SVGA_REG_CURSOR_MAX_DIMENSION:
        case SVGA_REG_FIFO_CAPS:
        case SVGA_REG_FENCE:
        case SVGA_REG_RESERVED1:
        case SVGA_REG_RESERVED2:
        case SVGA_REG_RESERVED3:
        case SVGA_REG_RESERVED4:
        case SVGA_REG_RESERVED5:
        case SVGA_REG_SCREENDMA:
        case SVGA_REG_GBOBJECT_MEM_SIZE_KB:
            /* Read only - ignore. */
            Log(("Write to R/O register %x - val %x ignored\n", idxReg, u32));
            STAM_REL_COUNTER_INC(&pThis->svga.StatRegReadOnlyWr);
            break;

        case SVGA_REG_IRQ_STATUS:
        {
            if (pThis->fVmSvga3)
            {
                LogFlow(("vmsvga3MmioWrite SVGA_IRQSTATUS_PORT %x: status %x -> %x\n", u32, pThis->svga.u32IrqStatus, pThis->svga.u32IrqStatus & ~u32));
                ASMAtomicAndU32(&pThis->svga.u32IrqStatus, ~u32);
                /* Clear the irq in case all events have been cleared. */
                if (!(pThis->svga.u32IrqStatus & pThis->svga.u32IrqMask))
                {
                    Log(("vmsvga3MmioWrite SVGA_IRQSTATUS_PORT: clearing IRQ\n"));
                    PDMDevHlpPCISetIrqNoWait(pDevIns, 0, 0);
                }
            }
            break;
        }

        default:
        {
            uint32_t offReg;
            if ((offReg = idxReg - SVGA_SCRATCH_BASE) < pThis->svga.cScratchRegion)
            {
                RT_UNTRUSTED_VALIDATED_FENCE();
                pThis->svga.au32ScratchRegion[offReg] = u32;
                STAM_REL_COUNTER_INC(&pThis->svga.StatRegScratchWr);
            }
            else if ((offReg = idxReg - SVGA_PALETTE_BASE) < (uint32_t)SVGA_NUM_PALETTE_REGS)
            {
                /* Note! Using last_palette rather than palette here to preserve the VGA one.
                         Btw, see rgb_to_pixel32.  */
                STAM_REL_COUNTER_INC(&pThis->svga.StatRegPaletteWr);
                u32 &= 0xff;
                RT_UNTRUSTED_VALIDATED_FENCE();
                uint32_t uRgb = pThis->last_palette[offReg / 3];
                switch (offReg % 3)
                {
                    case 0: uRgb = (uRgb & UINT32_C(0x0000ffff)) | (u32 << 16); break; /* red */
                    case 1: uRgb = (uRgb & UINT32_C(0x00ff00ff)) | (u32 <<  8); break; /* green */
                    case 2: uRgb = (uRgb & UINT32_C(0x00ffff00)) |  u32       ; break; /* blue */
                }
                pThis->last_palette[offReg / 3] = uRgb;
            }
            else
            {
#if !defined(IN_RING3) && defined(VBOX_STRICT)
                rc = VINF_IOM_R3_IOPORT_WRITE;
#else
                STAM_REL_COUNTER_INC(&pThis->svga.StatRegUnknownWr);
                AssertMsgFailed(("reg=%#x u32=%#x\n", idxReg, u32));
#endif
            }
            break;
        }
    }
    return rc;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN}
 */
DECLCALLBACK(VBOXSTRICTRC) vmsvgaIORead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PVGASTATE   pThis = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    RT_NOREF_PV(pvUser);

    /* Only dword accesses. */
    if (cb == 4)
    {
        switch (offPort)
        {
            case SVGA_INDEX_PORT:
                *pu32 = pThis->svga.u32IndexReg;
                break;

            case SVGA_VALUE_PORT:
            {
                /* Rough index register validation. */
                uint32_t idxReg = pThis->svga.u32IndexReg;
#if !defined(IN_RING3) && defined(VBOX_STRICT)
                ASSERT_GUEST_MSG_RETURN(idxReg < SVGA_SCRATCH_BASE + pThis->svga.cScratchRegion, ("idxReg=%#x\n", idxReg),
                                        VINF_IOM_R3_IOPORT_READ);
#else
                ASSERT_GUEST_MSG_STMT_RETURN(idxReg < SVGA_SCRATCH_BASE + pThis->svga.cScratchRegion, ("idxReg=%#x\n", idxReg),
                                             STAM_REL_COUNTER_INC(&pThis->svga.StatRegUnknownRd),
                                             VINF_SUCCESS);
#endif
                RT_UNTRUSTED_VALIDATED_FENCE();

                return vmsvgaReadPort(pDevIns, pThis, idxReg, pu32);
            }

            case SVGA_BIOS_PORT:
                Log(("Ignoring BIOS port read\n"));
                *pu32 = 0;
                break;

            case SVGA_IRQSTATUS_PORT:
                LogFlow(("vmsvgaIORead: SVGA_IRQSTATUS_PORT %x\n", pThis->svga.u32IrqStatus));
                *pu32 = pThis->svga.u32IrqStatus;
                break;

            default:
                ASSERT_GUEST_MSG_FAILED(("vmsvgaIORead: Unknown register %u was read from.\n", offPort));
                *pu32 = UINT32_MAX;
                break;
        }
    }
    else
    {
        Log(("Ignoring non-dword I/O port read at %x cb=%d\n", offPort, cb));
        *pu32 = UINT32_MAX;
    }
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT}
 */
DECLCALLBACK(VBOXSTRICTRC) vmsvgaIOWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    RT_NOREF_PV(pvUser);

    /* Only dword accesses. */
    if (cb == 4)
        switch (offPort)
        {
            case SVGA_INDEX_PORT:
                pThis->svga.u32IndexReg = u32;
                break;

            case SVGA_VALUE_PORT:
            {
                /* Rough index register validation. */
                uint32_t idxReg = pThis->svga.u32IndexReg;
#if !defined(IN_RING3) && defined(VBOX_STRICT)
                ASSERT_GUEST_MSG_RETURN(idxReg < SVGA_SCRATCH_BASE + pThis->svga.cScratchRegion, ("idxReg=%#x\n", idxReg),
                                        VINF_IOM_R3_IOPORT_WRITE);
#else
                ASSERT_GUEST_MSG_STMT_RETURN(idxReg < SVGA_SCRATCH_BASE + pThis->svga.cScratchRegion, ("idxReg=%#x\n", idxReg),
                                             STAM_REL_COUNTER_INC(&pThis->svga.StatRegUnknownWr),
                                             VINF_SUCCESS);
#endif
                RT_UNTRUSTED_VALIDATED_FENCE();

                return vmsvgaWritePort(pDevIns, pThis, pThisCC, idxReg, u32);
            }

            case SVGA_BIOS_PORT:
                Log(("Ignoring BIOS port write (val=%x)\n", u32));
                break;

            case SVGA_IRQSTATUS_PORT:
                LogFlow(("vmsvgaIOWrite SVGA_IRQSTATUS_PORT %x: status %x -> %x\n", u32, pThis->svga.u32IrqStatus, pThis->svga.u32IrqStatus & ~u32));
                ASMAtomicAndU32(&pThis->svga.u32IrqStatus, ~u32);
                /* Clear the irq in case all events have been cleared. */
                if (!(pThis->svga.u32IrqStatus & pThis->svga.u32IrqMask))
                {
                    Log(("vmsvgaIOWrite SVGA_IRQSTATUS_PORT: clearing IRQ\n"));
                    PDMDevHlpPCISetIrqNoWait(pDevIns, 0, 0);
                }
                break;

            default:
                ASSERT_GUEST_MSG_FAILED(("vmsvgaIOWrite: Unknown register %u was written to, value %#x LB %u.\n", offPort, u32, cb));
                break;
        }
    else
        Log(("Ignoring non-dword write at %x val=%x cb=%d\n", offPort, u32, cb));

    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
DECLCALLBACK(VBOXSTRICTRC) vmsvga3MmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    PVGASTATE   pThis = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    RT_NOREF_PV(pvUser);

    /* Only dword accesses. */
    VBOXSTRICTRC rcStrict;
    if (cb == sizeof(uint32_t))
    {
        rcStrict = vmsvgaReadPort(pDevIns, pThis, (uint32_t)(off / sizeof(uint32_t)), (uint32_t *)pv);
        if (rcStrict == VINF_IOM_R3_IOPORT_READ)
            rcStrict = VINF_IOM_R3_MMIO_READ;
    }
    else
    {
        Log(("Ignoring non-dword I/O port read at %x cb=%d\n", off, cb));
        rcStrict = VINF_IOM_MMIO_UNUSED_00;
    }
    return rcStrict;
}

/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
DECLCALLBACK(VBOXSTRICTRC) vmsvga3MmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    RT_NOREF_PV(pvUser);

    /* Only dword accesses. */
    VBOXSTRICTRC rcStrict;
    if (cb == sizeof(uint32_t))
    {
        rcStrict = vmsvgaWritePort(pDevIns, pThis, pThisCC, (uint32_t)(off / sizeof(uint32_t)), *(uint32_t *)pv);
        if (rcStrict == VINF_IOM_R3_IOPORT_WRITE)
            rcStrict = VINF_IOM_R3_MMIO_WRITE;
    }
    else
    {
        Log(("Ignoring non-dword write at %x cb=%d\n", off, cb));
        rcStrict = VINF_SUCCESS;
    }

    return rcStrict;
}

#ifdef IN_RING3

# ifdef DEBUG_FIFO_ACCESS
/**
 * Handle FIFO memory access.
 * @returns VBox status code.
 * @param   pVM             VM handle.
 * @param   pThis           The shared VGA/VMSVGA instance data.
 * @param   GCPhys          The access physical address.
 * @param   fWriteAccess    Read or write access
 */
static int vmsvgaR3DebugFifoAccess(PVM pVM, PVGASTATE pThis, RTGCPHYS GCPhys, bool fWriteAccess)
{
    RT_NOREF(pVM);
    RTGCPHYS GCPhysOffset = GCPhys - pThis->svga.GCPhysFIFO;
    uint32_t *pFIFO = pThisCC->svga.pau32FIFO;

    switch (GCPhysOffset >> 2)
    {
    case SVGA_FIFO_MIN:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_MIN = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_MAX:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_MAX = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_NEXT_CMD:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_NEXT_CMD = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_STOP:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_STOP = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_CAPABILITIES:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_CAPABILITIES = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_FLAGS:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_FLAGS = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_FENCE:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_FENCE = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_HWVERSION:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_HWVERSION = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_PITCHLOCK:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_PITCHLOCK = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_CURSOR_ON:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_CURSOR_ON = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_CURSOR_X:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_CURSOR_X = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_CURSOR_Y:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_CURSOR_Y = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_CURSOR_COUNT:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_CURSOR_COUNT = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_CURSOR_LAST_UPDATED:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_CURSOR_LAST_UPDATED = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_RESERVED:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_RESERVED = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_CURSOR_SCREEN_ID:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_CURSOR_SCREEN_ID = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_DEAD:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_DEAD = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_HWVERSION_REVISED:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_HWVERSION_REVISED = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_3D:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_3D = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_MAX_LIGHTS:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_MAX_LIGHTS = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_MAX_TEXTURES:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_MAX_TEXTURES = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_MAX_CLIP_PLANES:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_MAX_CLIP_PLANES = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_VERTEX_SHADER_VERSION:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_VERTEX_SHADER_VERSION = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_VERTEX_SHADER:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_VERTEX_SHADER = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_FRAGMENT_SHADER_VERSION:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_FRAGMENT_SHADER_VERSION = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_FRAGMENT_SHADER:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_FRAGMENT_SHADER = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_MAX_RENDER_TARGETS:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_MAX_RENDER_TARGETS = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_S23E8_TEXTURES:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_S23E8_TEXTURES = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_S10E5_TEXTURES:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_S10E5_TEXTURES = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_MAX_FIXED_VERTEXBLEND:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_MAX_FIXED_VERTEXBLEND = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_D16_BUFFER_FORMAT:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_D16_BUFFER_FORMAT = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_D24S8_BUFFER_FORMAT:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_D24S8_BUFFER_FORMAT = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_D24X8_BUFFER_FORMAT:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_D24X8_BUFFER_FORMAT = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_QUERY_TYPES:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_QUERY_TYPES = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_TEXTURE_GRADIENT_SAMPLING:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_TEXTURE_GRADIENT_SAMPLING = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_MAX_POINT_SIZE:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_MAX_POINT_SIZE = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_MAX_SHADER_TEXTURES:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_MAX_SHADER_TEXTURES = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_MAX_TEXTURE_WIDTH:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_MAX_TEXTURE_WIDTH = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_MAX_TEXTURE_HEIGHT:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_MAX_TEXTURE_HEIGHT = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_MAX_VOLUME_EXTENT:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_MAX_VOLUME_EXTENT = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_MAX_TEXTURE_REPEAT:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_MAX_TEXTURE_REPEAT = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_MAX_TEXTURE_ASPECT_RATIO:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_MAX_TEXTURE_ASPECT_RATIO = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_MAX_TEXTURE_ANISOTROPY:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_MAX_TEXTURE_ANISOTROPY = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_MAX_PRIMITIVE_COUNT:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_MAX_PRIMITIVE_COUNT = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_MAX_VERTEX_INDEX:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_MAX_VERTEX_INDEX = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_MAX_VERTEX_SHADER_INSTRUCTIONS:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_MAX_VERTEX_SHADER_INSTRUCTIONS = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_MAX_FRAGMENT_SHADER_INSTRUCTIONS:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_MAX_FRAGMENT_SHADER_INSTRUCTIONS = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_MAX_VERTEX_SHADER_TEMPS:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_MAX_VERTEX_SHADER_TEMPS = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_MAX_FRAGMENT_SHADER_TEMPS:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_MAX_FRAGMENT_SHADER_TEMPS = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_TEXTURE_OPS:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_TEXTURE_OPS = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_X8R8G8B8:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_X8R8G8B8 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_A8R8G8B8:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_A8R8G8B8 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_A2R10G10B10:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_A2R10G10B10 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_X1R5G5B5:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_X1R5G5B5 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_A1R5G5B5:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_A1R5G5B5 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_A4R4G4B4:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_A4R4G4B4 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_R5G6B5:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_R5G6B5 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_LUMINANCE16:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_LUMINANCE16 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_LUMINANCE8_ALPHA8:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_LUMINANCE8_ALPHA8 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_ALPHA8:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_ALPHA8 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_LUMINANCE8:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_LUMINANCE8 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_Z_D16:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_Z_D16 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_Z_D24S8:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_Z_D24S8 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_Z_D24X8:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_Z_D24X8 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_DXT1:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_DXT1 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_DXT2:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_DXT2 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_DXT3:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_DXT3 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_DXT4:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_DXT4 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_DXT5:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_DXT5 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_BUMPX8L8V8U8:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_BUMPX8L8V8U8 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_A2W10V10U10:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_A2W10V10U10 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_BUMPU8V8:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_BUMPU8V8 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_Q8W8V8U8:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_Q8W8V8U8 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_CxV8U8:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_CxV8U8 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_R_S10E5:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_R_S10E5 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_R_S23E8:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_R_S23E8 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_RG_S10E5:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_RG_S10E5 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_RG_S23E8:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_RG_S23E8 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_ARGB_S10E5:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_ARGB_S10E5 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_ARGB_S23E8:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_ARGB_S23E8 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_MAX_VERTEX_SHADER_TEXTURES:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_MAX_VERTEX_SHADER_TEXTURES = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_MAX_SIMULTANEOUS_RENDER_TARGETS:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_MAX_SIMULTANEOUS_RENDER_TARGETS = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_V16U16:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_V16U16 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_G16R16:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_G16R16 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_A16B16G16R16:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_A16B16G16R16 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_UYVY:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_UYVY = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_YUY2:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_YUY2 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_DEAD4: /* SVGA3D_DEVCAP_MULTISAMPLE_NONMASKABLESAMPLES */
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_DEAD4 (SVGA3D_DEVCAP_MULTISAMPLE_NONMASKABLESAMPLES) = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_DEAD5: /* SVGA3D_DEVCAP_MULTISAMPLE_MASKABLESAMPLES */
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_DEAD5 (SVGA3D_DEVCAP_MULTISAMPLE_MASKABLESAMPLES) = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_DEAD7: /* SVGA3D_DEVCAP_ALPHATOCOVERAGE */
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_DEAD7 (SVGA3D_DEVCAP_ALPHATOCOVERAGE) = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_DEAD6: /* SVGA3D_DEVCAP_SUPERSAMPLE */
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_DEAD6 (SVGA3D_DEVCAP_SUPERSAMPLE) = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_AUTOGENMIPMAPS:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_AUTOGENMIPMAPS = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_NV12:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_NV12 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_DEAD10: /* SVGA3D_DEVCAP_SURFACEFMT_AYUV */
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_DEAD10 (SVGA3D_DEVCAP_SURFACEFMT_AYUV) = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_MAX_CONTEXT_IDS:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_MAX_CONTEXT_IDS = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_MAX_SURFACE_IDS:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_MAX_SURFACE_IDS = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_Z_DF16:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_Z_DF16 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_Z_DF24:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_Z_DF24 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_Z_D24S8_INT:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_Z_D24S8_INT = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_ATI1:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_ATI1 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS + SVGA3D_DEVCAP_SURFACEFMT_ATI2:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS SVGA3D_DEVCAP_SURFACEFMT_ATI2 = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_3D_CAPS_LAST:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_3D_CAPS_LAST = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_GUEST_3D_HWVERSION:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_GUEST_3D_HWVERSION = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_FENCE_GOAL:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_FENCE_GOAL = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    case SVGA_FIFO_BUSY:
        Log(("vmsvgaFIFOAccess [0x%x]: %s SVGA_FIFO_BUSY = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", pFIFO[GCPhysOffset >> 2]));
        break;
    default:
        Log(("vmsvgaFIFOAccess [0x%x]: %s access at offset %x = %x\n", GCPhysOffset >> 2, (fWriteAccess) ? "WRITE" : "READ", GCPhysOffset, pFIFO[GCPhysOffset >> 2]));
        break;
    }

    return VINF_EM_RAW_EMULATE_INSTR;
}
# endif /* DEBUG_FIFO_ACCESS */

# if defined(VMSVGA_USE_FIFO_ACCESS_HANDLER) || defined(DEBUG_FIFO_ACCESS)
/**
 * HC access handler for the FIFO.
 *
 * @returns VINF_SUCCESS if the handler have carried out the operation.
 * @returns VINF_PGM_HANDLER_DO_DEFAULT if the caller should carry out the access operation.
 * @param   pVM             VM Handle.
 * @param   pVCpu           The cross context CPU structure for the calling EMT.
 * @param   GCPhys          The physical address the guest is writing to.
 * @param   pvPhys          The HC mapping of that address.
 * @param   pvBuf           What the guest is reading/writing.
 * @param   cbBuf           How much it's reading/writing.
 * @param   enmAccessType   The access type.
 * @param   enmOrigin       Who is making the access.
 * @param   pvUser          User argument.
 */
static DECLCALLBACK(VBOXSTRICTRC)
vmsvgaR3FifoAccessHandler(PVM pVM, PVMCPU pVCpu, RTGCPHYS GCPhys, void *pvPhys, void *pvBuf, size_t cbBuf,
                          PGMACCESSTYPE enmAccessType, PGMACCESSORIGIN enmOrigin, void *pvUser)
{
    NOREF(pVCpu); NOREF(pvPhys); NOREF(pvBuf); NOREF(cbBuf); NOREF(enmOrigin); NOREF(enmAccessType); NOREF(GCPhys);
    PVGASTATE pThis = (PVGASTATE)pvUser;
    AssertPtr(pThis);

# ifdef VMSVGA_USE_FIFO_ACCESS_HANDLER
    /*
     * Wake up the FIFO thread as it might have work to do now.
     */
    int rc = PDMDevHlpSUPSemEventSignal(pDevIns, pThis->svga.hFIFORequestSem);
    AssertLogRelRC(rc);
# endif

# ifdef DEBUG_FIFO_ACCESS
    /*
     * When in debug-fifo-access mode, we do not disable the access handler,
     * but leave it on as we wish to catch all access.
     */
    Assert(GCPhys >= pThis->svga.GCPhysFIFO);
    rc = vmsvgaR3DebugFifoAccess(pVM, pThis, GCPhys, enmAccessType == PGMACCESSTYPE_WRITE);
# elif defined(VMSVGA_USE_FIFO_ACCESS_HANDLER)
    /*
     * Temporarily disable the access handler now that we've kicked the FIFO thread.
     */
    STAM_REL_COUNTER_INC(&pThisCC->svga.pSvgaR3State->StatFifoAccessHandler);
    rc = PGMHandlerPhysicalPageTempOff(pVM, pThis->svga.GCPhysFIFO, pThis->svga.GCPhysFIFO);
# endif
    if (RT_SUCCESS(rc))
        return VINF_PGM_HANDLER_DO_DEFAULT;
    AssertMsg(rc <= VINF_SUCCESS, ("rc=%Rrc\n", rc));
    return rc;
}
# endif /* VMSVGA_USE_FIFO_ACCESS_HANDLER || DEBUG_FIFO_ACCESS */

#endif /* IN_RING3 */

#ifdef DEBUG_GMR_ACCESS
# ifdef IN_RING3

/**
 * HC access handler for GMRs.
 *
 * @returns VINF_SUCCESS if the handler have carried out the operation.
 * @returns VINF_PGM_HANDLER_DO_DEFAULT if the caller should carry out the access operation.
 * @param   pVM             VM Handle.
 * @param   pVCpu           The cross context CPU structure for the calling EMT.
 * @param   GCPhys          The physical address the guest is writing to.
 * @param   pvPhys          The HC mapping of that address.
 * @param   pvBuf           What the guest is reading/writing.
 * @param   cbBuf           How much it's reading/writing.
 * @param   enmAccessType   The access type.
 * @param   enmOrigin       Who is making the access.
 * @param   pvUser          User argument.
 */
static DECLCALLBACK(VBOXSTRICTRC)
vmsvgaR3GmrAccessHandler(PVM pVM, PVMCPU pVCpu, RTGCPHYS GCPhys, void *pvPhys, void *pvBuf, size_t cbBuf,
                         PGMACCESSTYPE enmAccessType, PGMACCESSORIGIN enmOrigin, void *pvUser)
{
    PVGASTATE   pThis = (PVGASTATE)pvUser;
    Assert(pThis);
    PVMSVGAR3STATE pSVGAState = pThisCC->svga.pSvgaR3State;
    NOREF(pVCpu); NOREF(pvPhys); NOREF(pvBuf); NOREF(cbBuf); NOREF(enmAccessType); NOREF(enmOrigin);

    Log(("vmsvgaR3GmrAccessHandler: GMR access to page %RGp\n", GCPhys));

    for (uint32_t i = 0; i < pThis->svga.cGMR; ++i)
    {
        PGMR pGMR = &pSVGAState->paGMR[i];

        if (pGMR->numDescriptors)
        {
            for (uint32_t j = 0; j < pGMR->numDescriptors; j++)
            {
                if (    GCPhys >= pGMR->paDesc[j].GCPhys
                    &&  GCPhys < pGMR->paDesc[j].GCPhys + pGMR->paDesc[j].numPages * GUEST_PAGE_SIZE)
                {
                    /*
                     * Turn off the write handler for this particular page and make it R/W.
                     * Then return telling the caller to restart the guest instruction.
                     */
                    int rc = PGMHandlerPhysicalPageTempOff(pVM, pGMR->paDesc[j].GCPhys, GCPhys);
                    AssertRC(rc);
                    return VINF_PGM_HANDLER_DO_DEFAULT;
                }
            }
        }
    }

    return VINF_PGM_HANDLER_DO_DEFAULT;
}

/** Callback handler for VMR3ReqCallWaitU */
static DECLCALLBACK(int) vmsvgaR3RegisterGmr(PPDMDEVINS pDevIns, uint32_t gmrId)
{
    PVGASTATE       pThis = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVMSVGAR3STATE  pSVGAState = pThisCC->svga.pSvgaR3State;
    PGMR pGMR = &pSVGAState->paGMR[gmrId];
    int rc;

    for (uint32_t i = 0; i < pGMR->numDescriptors; i++)
    {
        rc = PDMDevHlpPGMHandlerPhysicalRegister(pDevIns, pGMR->paDesc[i].GCPhys,
                                                 pGMR->paDesc[i].GCPhys + pGMR->paDesc[i].numPages * GUEST_PAGE_SIZE - 1,
                                                 pThis->svga.hGmrAccessHandlerType, pThis, NIL_RTR0PTR, NIL_RTRCPTR, "VMSVGA GMR");
        AssertRC(rc);
    }
    return VINF_SUCCESS;
}

/** Callback handler for VMR3ReqCallWaitU */
static DECLCALLBACK(int) vmsvgaR3DeregisterGmr(PPDMDEVINS pDevIns, uint32_t gmrId)
{
    PVGASTATE       pThis = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVMSVGAR3STATE  pSVGAState = pThisCC->svga.pSvgaR3State;
    PGMR pGMR = &pSVGAState->paGMR[gmrId];

    for (uint32_t i = 0; i < pGMR->numDescriptors; i++)
    {
        int rc = PDMDevHlpPGMHandlerPhysicalDeregister(pDevIns, pGMR->paDesc[i].GCPhys);
        AssertRC(rc);
    }
    return VINF_SUCCESS;
}

/** Callback handler for VMR3ReqCallWaitU */
static DECLCALLBACK(int) vmsvgaR3ResetGmrHandlers(PVGASTATE pThis)
{
    PVMSVGAR3STATE pSVGAState = pThisCC->svga.pSvgaR3State;

    for (uint32_t i = 0; i < pThis->svga.cGMR; ++i)
    {
        PGMR pGMR = &pSVGAState->paGMR[i];

        if (pGMR->numDescriptors)
        {
            for (uint32_t j = 0; j < pGMR->numDescriptors; j++)
            {
                int rc = PDMDevHlpPGMHandlerPhysicalReset(pDevIns, pGMR->paDesc[j].GCPhys);
                AssertRC(rc);
            }
        }
    }
    return VINF_SUCCESS;
}

# endif /* IN_RING3 */
#endif /* DEBUG_GMR_ACCESS */

/* -=-=-=-=-=- Ring 3 -=-=-=-=-=- */

#ifdef IN_RING3


/*
 *
 * Command buffer submission.
 *
 * Guest submits a buffer by writing to SVGA_REG_COMMAND_LOW register.
 *
 * EMT thread appends a command buffer to the context queue (VMSVGACMDBUFCTX::listSubmitted)
 * and wakes up the FIFO thread.
 *
 * FIFO thread fetches the command buffer from the queue, processes the commands and writes
 * the buffer header back to the guest memory.
 *
 * If buffers are preempted, then the EMT thread removes all buffers from the context queue.
 *
 */


/** Update a command buffer header 'status' and 'errorOffset' fields in the guest memory.
 *
 * @param pDevIns     The device instance.
 * @param GCPhysCB    Guest physical address of the command buffer header.
 * @param status      Command buffer status (SVGA_CB_STATUS_*).
 * @param errorOffset Offset to the first byte of the failing command for SVGA_CB_STATUS_COMMAND_ERROR.
 *                    errorOffset is ignored if the status is not SVGA_CB_STATUS_COMMAND_ERROR.
 * @thread FIFO or EMT.
 */
static void vmsvgaR3CmdBufWriteStatus(PPDMDEVINS pDevIns, RTGCPHYS GCPhysCB, SVGACBStatus status, uint32_t errorOffset)
{
    SVGACBHeader hdr;
    hdr.status = status;
    hdr.errorOffset = errorOffset;
    AssertCompile(   RT_OFFSETOF(SVGACBHeader, status) == 0
                  && RT_OFFSETOF(SVGACBHeader, errorOffset) == 4
                  && RT_OFFSETOF(SVGACBHeader, id) == 8);
    size_t const cbWrite = status == SVGA_CB_STATUS_COMMAND_ERROR
                         ? RT_UOFFSET_AFTER(SVGACBHeader, errorOffset)  /* Both 'status' and 'errorOffset' fields. */
                         : RT_UOFFSET_AFTER(SVGACBHeader, status);      /* Only 'status' field. */
    PDMDevHlpPCIPhysWrite(pDevIns, GCPhysCB, &hdr, cbWrite);
}


/** Raise an IRQ.
 *
 * @param pDevIns     The device instance.
 * @param pThis       The shared VGA/VMSVGA state.
 * @param u32IrqStatus SVGA_IRQFLAG_* bits.
 * @thread FIFO or EMT.
 */
static void vmsvgaR3CmdBufRaiseIRQ(PPDMDEVINS pDevIns, PVGASTATE pThis, uint32_t u32IrqStatus)
{
    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSect, rcLock);

    if (pThis->svga.u32IrqMask & u32IrqStatus)
    {
        LogFunc(("Trigger interrupt with status %#x\n", u32IrqStatus));
        ASMAtomicOrU32(&pThis->svga.u32IrqStatus, u32IrqStatus);
        PDMDevHlpPCISetIrq(pDevIns, 0, 1);
    }

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
}


/** Allocate a command buffer structure.
 *
 * @param pCmdBufCtx  The command buffer context which must allocate the buffer.
 * @return Pointer to the allocated command buffer structure.
 */
static PVMSVGACMDBUF vmsvgaR3CmdBufAlloc(PVMSVGACMDBUFCTX pCmdBufCtx)
{
    if (!pCmdBufCtx)
        return NULL;

    PVMSVGACMDBUF pCmdBuf = (PVMSVGACMDBUF)RTMemAllocZ(sizeof(*pCmdBuf));
    if (pCmdBuf)
    {
        // RT_ZERO(pCmdBuf->nodeBuffer);
        pCmdBuf->pCmdBufCtx = pCmdBufCtx;
        // pCmdBuf->GCPhysCB = 0;
        // RT_ZERO(pCmdBuf->hdr);
        // pCmdBuf->pvCommands = NULL;
    }

    return pCmdBuf;
}


/** Free a command buffer structure.
 *
 * @param pCmdBuf  The command buffer pointer.
 */
static void vmsvgaR3CmdBufFree(PVMSVGACMDBUF pCmdBuf)
{
    if (pCmdBuf)
        RTMemFree(pCmdBuf->pvCommands);
    RTMemFree(pCmdBuf);
}


/** Initialize a command buffer context.
 *
 * @param pCmdBufCtx  The command buffer context.
 */
static void vmsvgaR3CmdBufCtxInit(PVMSVGACMDBUFCTX pCmdBufCtx)
{
    RTListInit(&pCmdBufCtx->listSubmitted);
    pCmdBufCtx->cSubmitted = 0;
}


/** Destroy a command buffer context.
 *
 * @param pCmdBufCtx  The command buffer context pointer.
 */
static void vmsvgaR3CmdBufCtxTerm(PVMSVGACMDBUFCTX pCmdBufCtx)
{
    if (!pCmdBufCtx)
        return;

    if (pCmdBufCtx->listSubmitted.pNext)
    {
        /* If the list has been initialized. */
        PVMSVGACMDBUF pIter, pNext;
        RTListForEachSafe(&pCmdBufCtx->listSubmitted, pIter, pNext, VMSVGACMDBUF, nodeBuffer)
        {
            RTListNodeRemove(&pIter->nodeBuffer);
            --pCmdBufCtx->cSubmitted;
            vmsvgaR3CmdBufFree(pIter);
        }
    }
    Assert(pCmdBufCtx->cSubmitted == 0);
    pCmdBufCtx->cSubmitted = 0;
}


/** Handles SVGA_DC_CMD_START_STOP_CONTEXT command.
 *
 * @param pSvgaR3State VMSVGA R3 state.
 * @param pCmd         The command data.
 * @return SVGACBStatus code.
 * @thread EMT
 */
static SVGACBStatus vmsvgaR3CmdBufDCStartStop(PVMSVGAR3STATE pSvgaR3State, SVGADCCmdStartStop const *pCmd)
{
    /* Create or destroy a regular command buffer context. */
    if (pCmd->context >= RT_ELEMENTS(pSvgaR3State->apCmdBufCtxs))
        return SVGA_CB_STATUS_COMMAND_ERROR;
    RT_UNTRUSTED_VALIDATED_FENCE();

    SVGACBStatus CBStatus = SVGA_CB_STATUS_COMPLETED;

    int rc = RTCritSectEnter(&pSvgaR3State->CritSectCmdBuf);
    AssertRC(rc);
    if (pCmd->enable)
    {
        pSvgaR3State->apCmdBufCtxs[pCmd->context] = (PVMSVGACMDBUFCTX)RTMemAlloc(sizeof(VMSVGACMDBUFCTX));
        if (pSvgaR3State->apCmdBufCtxs[pCmd->context])
            vmsvgaR3CmdBufCtxInit(pSvgaR3State->apCmdBufCtxs[pCmd->context]);
        else
            CBStatus = SVGA_CB_STATUS_QUEUE_FULL;
    }
    else
    {
        vmsvgaR3CmdBufCtxTerm(pSvgaR3State->apCmdBufCtxs[pCmd->context]);
        RTMemFree(pSvgaR3State->apCmdBufCtxs[pCmd->context]);
        pSvgaR3State->apCmdBufCtxs[pCmd->context] = NULL;
    }
    RTCritSectLeave(&pSvgaR3State->CritSectCmdBuf);

    return CBStatus;
}


/** Handles SVGA_DC_CMD_PREEMPT command.
 *
 * @param pDevIns      The device instance.
 * @param pSvgaR3State VMSVGA R3 state.
 * @param pCmd         The command data.
 * @return SVGACBStatus code.
 * @thread EMT
 */
static SVGACBStatus vmsvgaR3CmdBufDCPreempt(PPDMDEVINS pDevIns, PVMSVGAR3STATE pSvgaR3State, SVGADCCmdPreempt const *pCmd)
{
    /* Remove buffers from the processing queue of the specified context. */
    if (pCmd->context >= RT_ELEMENTS(pSvgaR3State->apCmdBufCtxs))
        return SVGA_CB_STATUS_COMMAND_ERROR;
    RT_UNTRUSTED_VALIDATED_FENCE();

    PVMSVGACMDBUFCTX const pCmdBufCtx = pSvgaR3State->apCmdBufCtxs[pCmd->context];
    RTLISTANCHOR listPreempted;

    int rc = RTCritSectEnter(&pSvgaR3State->CritSectCmdBuf);
    AssertRC(rc);
    if (pCmd->ignoreIDZero)
    {
        RTListInit(&listPreempted);

        PVMSVGACMDBUF pIter, pNext;
        RTListForEachSafe(&pCmdBufCtx->listSubmitted, pIter, pNext, VMSVGACMDBUF, nodeBuffer)
        {
            if (pIter->hdr.id == 0)
                continue;

            RTListNodeRemove(&pIter->nodeBuffer);
            --pCmdBufCtx->cSubmitted;
            RTListAppend(&listPreempted, &pIter->nodeBuffer);
        }
    }
    else
    {
        RTListMove(&listPreempted, &pCmdBufCtx->listSubmitted);
        pCmdBufCtx->cSubmitted = 0;
    }
    RTCritSectLeave(&pSvgaR3State->CritSectCmdBuf);

    PVMSVGACMDBUF pIter, pNext;
    RTListForEachSafe(&listPreempted, pIter, pNext, VMSVGACMDBUF, nodeBuffer)
    {
        RTListNodeRemove(&pIter->nodeBuffer);
        vmsvgaR3CmdBufWriteStatus(pDevIns, pIter->GCPhysCB, SVGA_CB_STATUS_PREEMPTED, 0);
        LogFunc(("Preempted %RX64\n", pIter->GCPhysCB));
        vmsvgaR3CmdBufFree(pIter);
    }

    return SVGA_CB_STATUS_COMPLETED;
}


/** @def VMSVGA_INC_CMD_SIZE_BREAK
 * Increments the size of the command cbCmd by a_cbMore.
 * Checks that the command buffer has at least cbCmd bytes. Will break out of the switch if it doesn't.
 * Used by vmsvgaR3CmdBufProcessDC and vmsvgaR3CmdBufProcessCommands.
 */
#define VMSVGA_INC_CMD_SIZE_BREAK(a_cbMore) \
     if (1) { \
          cbCmd += (a_cbMore); \
          ASSERT_GUEST_MSG_STMT_BREAK(cbRemain >= cbCmd, ("size=%#x remain=%#zx\n", cbCmd, (size_t)cbRemain), CBstatus = SVGA_CB_STATUS_COMMAND_ERROR); \
          RT_UNTRUSTED_VALIDATED_FENCE(); \
     } else do {} while (0)


/** Processes Device Context command buffer.
 *
 * @param pDevIns      The device instance.
 * @param pSvgaR3State VMSVGA R3 state.
 * @param pvCommands   Pointer to the command buffer.
 * @param cbCommands   Size of the command buffer.
 * @param poffNextCmd  Where to store the offset of the first unprocessed command.
 * @return SVGACBStatus code.
 * @thread EMT
 */
static SVGACBStatus vmsvgaR3CmdBufProcessDC(PPDMDEVINS pDevIns, PVMSVGAR3STATE pSvgaR3State, void const *pvCommands, uint32_t cbCommands, uint32_t *poffNextCmd)
{
    SVGACBStatus CBstatus = SVGA_CB_STATUS_COMPLETED;

    uint8_t const *pu8Cmd = (uint8_t *)pvCommands;
    uint32_t cbRemain = cbCommands;
    while (cbRemain)
    {
        /* Command identifier is a 32 bit value. */
        if (cbRemain < sizeof(uint32_t))
        {
            CBstatus = SVGA_CB_STATUS_COMMAND_ERROR;
            break;
        }

         /* Fetch the command id. */
        uint32_t const cmdId = *(uint32_t *)pu8Cmd;
        uint32_t cbCmd = sizeof(uint32_t);
        switch (cmdId)
        {
            case SVGA_DC_CMD_NOP:
            {
                /* NOP */
                break;
            }

            case SVGA_DC_CMD_START_STOP_CONTEXT:
            {
                SVGADCCmdStartStop *pCmd = (SVGADCCmdStartStop *)&pu8Cmd[cbCmd];
                VMSVGA_INC_CMD_SIZE_BREAK(sizeof(*pCmd));
                CBstatus = vmsvgaR3CmdBufDCStartStop(pSvgaR3State, pCmd);
                break;
            }

            case SVGA_DC_CMD_PREEMPT:
            {
                SVGADCCmdPreempt *pCmd = (SVGADCCmdPreempt *)&pu8Cmd[cbCmd];
                VMSVGA_INC_CMD_SIZE_BREAK(sizeof(*pCmd));
                CBstatus = vmsvgaR3CmdBufDCPreempt(pDevIns, pSvgaR3State, pCmd);
                break;
            }

            default:
            {
                /* Unsupported command. */
                CBstatus = SVGA_CB_STATUS_COMMAND_ERROR;
                break;
            }
        }

        if (CBstatus != SVGA_CB_STATUS_COMPLETED)
            break;

        pu8Cmd += cbCmd;
        cbRemain -= cbCmd;
    }

    Assert(cbRemain <= cbCommands);
    *poffNextCmd = cbCommands - cbRemain;
    return CBstatus;
}


/** Submits a device context command buffer for synchronous processing.
 *
 * @param pDevIns      The device instance.
 * @param pThisCC      The VGA/VMSVGA state for the current context.
 * @param ppCmdBuf     Pointer to the command buffer pointer.
 *                     The function can set the command buffer pointer to NULL to prevent deallocation by the caller.
 * @param poffNextCmd  Where to store the offset of the first unprocessed command.
 * @return SVGACBStatus code.
 * @thread EMT
 */
static SVGACBStatus vmsvgaR3CmdBufSubmitDC(PPDMDEVINS pDevIns, PVGASTATECC pThisCC, PVMSVGACMDBUF *ppCmdBuf, uint32_t *poffNextCmd)
{
    /* Synchronously process the device context commands. */
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    return vmsvgaR3CmdBufProcessDC(pDevIns, pSvgaR3State, (*ppCmdBuf)->pvCommands, (*ppCmdBuf)->hdr.length, poffNextCmd);
}

/** Submits a command buffer for asynchronous processing by the FIFO thread.
 *
 * @param pDevIns      The device instance.
 * @param pThis        The shared VGA/VMSVGA state.
 * @param pThisCC      The VGA/VMSVGA state for the current context.
 * @param ppCmdBuf     Pointer to the command buffer pointer.
 *                     The function can set the command buffer pointer to NULL to prevent deallocation by the caller.
 * @return SVGACBStatus code.
 * @thread EMT
 */
static SVGACBStatus vmsvgaR3CmdBufSubmitCtx(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC, PVMSVGACMDBUF *ppCmdBuf)
{
    /* Command buffer submission. */
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    SVGACBStatus CBstatus = SVGA_CB_STATUS_NONE;

    PVMSVGACMDBUF const pCmdBuf = *ppCmdBuf;
    PVMSVGACMDBUFCTX const pCmdBufCtx = pCmdBuf->pCmdBufCtx;

    int rc = RTCritSectEnter(&pSvgaR3State->CritSectCmdBuf);
    AssertRC(rc);

    if (RT_LIKELY(pCmdBufCtx->cSubmitted < SVGA_CB_MAX_QUEUED_PER_CONTEXT))
    {
        RTListAppend(&pCmdBufCtx->listSubmitted, &pCmdBuf->nodeBuffer);
        ++pCmdBufCtx->cSubmitted;
        *ppCmdBuf = NULL; /* Consume the buffer. */
        ASMAtomicWriteU32(&pThisCC->svga.pSvgaR3State->fCmdBuf, 1);
    }
    else
        CBstatus = SVGA_CB_STATUS_QUEUE_FULL;

    RTCritSectLeave(&pSvgaR3State->CritSectCmdBuf);

    /* Inform the FIFO thread. */
    if (*ppCmdBuf == NULL)
        PDMDevHlpSUPSemEventSignal(pDevIns, pThis->svga.hFIFORequestSem);

    return CBstatus;
}


/** SVGA_REG_COMMAND_LOW write handler.
 * Submits a command buffer to the FIFO thread or processes a device context command.
 *
 * @param pDevIns     The device instance.
 * @param pThis       The shared VGA/VMSVGA state.
 * @param pThisCC     The VGA/VMSVGA state for the current context.
 * @param GCPhysCB    Guest physical address of the command buffer header.
 * @param CBCtx       Context the command buffer is submitted to.
 * @thread EMT
 */
static void vmsvgaR3CmdBufSubmit(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC, RTGCPHYS GCPhysCB, SVGACBContext CBCtx)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    SVGACBStatus CBstatus = SVGA_CB_STATUS_NONE;
    uint32_t offNextCmd = 0;
    uint32_t fIRQ = 0;

    /* Get the context if the device has the capability. */
    PVMSVGACMDBUFCTX pCmdBufCtx = NULL;
    if (pThis->svga.u32DeviceCaps & SVGA_CAP_COMMAND_BUFFERS)
    {
        if (RT_LIKELY(CBCtx < RT_ELEMENTS(pSvgaR3State->apCmdBufCtxs)))
            pCmdBufCtx = pSvgaR3State->apCmdBufCtxs[CBCtx];
        else if (CBCtx == SVGA_CB_CONTEXT_DEVICE)
            pCmdBufCtx = &pSvgaR3State->CmdBufCtxDC;
        RT_UNTRUSTED_VALIDATED_FENCE();
    }

    /* Allocate a new command buffer. */
    PVMSVGACMDBUF pCmdBuf = vmsvgaR3CmdBufAlloc(pCmdBufCtx);
    if (RT_LIKELY(pCmdBuf))
    {
        pCmdBuf->GCPhysCB = GCPhysCB;

        int rc = PDMDevHlpPCIPhysRead(pDevIns, GCPhysCB, &pCmdBuf->hdr, sizeof(pCmdBuf->hdr));
        if (RT_SUCCESS(rc))
        {
            LogFunc(("status %RX32 errorOffset %RX32 id %RX64 flags %RX32 length %RX32 ptr %RX64 offset %RX32 dxContext %RX32 (%RX32 %RX32 %RX32 %RX32 %RX32 %RX32)\n",
                     pCmdBuf->hdr.status,
                     pCmdBuf->hdr.errorOffset,
                     pCmdBuf->hdr.id,
                     pCmdBuf->hdr.flags,
                     pCmdBuf->hdr.length,
                     pCmdBuf->hdr.ptr.pa,
                     pCmdBuf->hdr.offset,
                     pCmdBuf->hdr.dxContext,
                     pCmdBuf->hdr.mustBeZero[0],
                     pCmdBuf->hdr.mustBeZero[1],
                     pCmdBuf->hdr.mustBeZero[2],
                     pCmdBuf->hdr.mustBeZero[3],
                     pCmdBuf->hdr.mustBeZero[4],
                     pCmdBuf->hdr.mustBeZero[5]));

            /* Verify the command buffer header. */
            if (RT_LIKELY(   pCmdBuf->hdr.status == SVGA_CB_STATUS_NONE
                          && (pCmdBuf->hdr.flags & ~(SVGA_CB_FLAG_NO_IRQ | SVGA_CB_FLAG_DX_CONTEXT)) == 0 /* No unexpected flags. */
                          && pCmdBuf->hdr.length <= SVGA_CB_MAX_SIZE))
            {
                RT_UNTRUSTED_VALIDATED_FENCE();

                /* Read the command buffer content. */
                pCmdBuf->pvCommands = RTMemAlloc(pCmdBuf->hdr.length);
                if (pCmdBuf->pvCommands)
                {
                    RTGCPHYS const GCPhysCmd = (RTGCPHYS)pCmdBuf->hdr.ptr.pa;
                    rc = PDMDevHlpPCIPhysRead(pDevIns, GCPhysCmd, pCmdBuf->pvCommands, pCmdBuf->hdr.length);
                    if (RT_SUCCESS(rc))
                    {
                        /* Submit the buffer. Device context buffers will be processed synchronously. */
                        if (RT_LIKELY(CBCtx < RT_ELEMENTS(pSvgaR3State->apCmdBufCtxs)))
                            /* This usually processes the CB async and sets pCmbBuf to NULL. */
                            CBstatus = vmsvgaR3CmdBufSubmitCtx(pDevIns, pThis, pThisCC, &pCmdBuf);
                        else
                            CBstatus = vmsvgaR3CmdBufSubmitDC(pDevIns, pThisCC, &pCmdBuf, &offNextCmd);
                    }
                    else
                    {
                        ASSERT_GUEST_MSG_FAILED(("Failed to read commands at %RGp\n", GCPhysCmd));
                        CBstatus = SVGA_CB_STATUS_CB_HEADER_ERROR;
                        fIRQ = SVGA_IRQFLAG_ERROR | SVGA_IRQFLAG_COMMAND_BUFFER;
                    }
                }
                else
                {
                    /* No memory for commands. */
                    CBstatus = SVGA_CB_STATUS_QUEUE_FULL;
                }
            }
            else
            {
                ASSERT_GUEST_MSG_FAILED(("Invalid buffer header\n"));
                CBstatus = SVGA_CB_STATUS_CB_HEADER_ERROR;
                fIRQ = SVGA_IRQFLAG_ERROR | SVGA_IRQFLAG_COMMAND_BUFFER;
            }
        }
        else
        {
            LogFunc(("Failed to read buffer header at %RGp\n", GCPhysCB));
            ASSERT_GUEST_FAILED();
            /* Do not attempt to write the status. */
        }

        /* Free the buffer if pfnCmdBufSubmit did not consume it. */
        vmsvgaR3CmdBufFree(pCmdBuf);
    }
    else
    {
        LogFunc(("Can't allocate buffer for context id %#x\n", CBCtx));
        AssertFailed();
        CBstatus = SVGA_CB_STATUS_QUEUE_FULL;
    }

    if (CBstatus != SVGA_CB_STATUS_NONE)
    {
        LogFunc(("Write status %#x, offNextCmd %#x, fIRQ %#x\n", CBstatus, offNextCmd, fIRQ));
        vmsvgaR3CmdBufWriteStatus(pDevIns, GCPhysCB, CBstatus, offNextCmd);
        if (fIRQ)
            vmsvgaR3CmdBufRaiseIRQ(pDevIns, pThis, fIRQ);
    }
}


/** Checks if there are some buffers to be processed.
 *
 * @param pThisCC     The VGA/VMSVGA state for the current context.
 * @return true if buffers must be processed.
 * @thread FIFO
 */
static bool vmsvgaR3CmdBufHasWork(PVGASTATECC pThisCC)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;
    return RT_BOOL(ASMAtomicReadU32(&pSvgaR3State->fCmdBuf));
}


/** Processes a command buffer.
 *
 * @param pDevIns      The device instance.
 * @param pThis        The shared VGA/VMSVGA state.
 * @param pThisCC      The VGA/VMSVGA state for the current context.
 * @param idDXContext  VGPU10 DX context of the commands or SVGA3D_INVALID_ID if they are not for a specific context.
 * @param pvCommands   Pointer to the command buffer.
 * @param cbCommands   Size of the command buffer.
 * @param poffNextCmd  Where to store the offset of the first unprocessed command.
 * @param pu32IrqStatus Where to store SVGA_IRQFLAG_ if the IRQ is generated by the last command in the buffer.
 * @return SVGACBStatus code.
 * @thread FIFO
 */
static SVGACBStatus vmsvgaR3CmdBufProcessCommands(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC, uint32_t idDXContext, void const *pvCommands, uint32_t cbCommands, uint32_t *poffNextCmd, uint32_t *pu32IrqStatus)
{
# ifndef VBOX_WITH_VMSVGA3D
    RT_NOREF(idDXContext);
# endif
    SVGACBStatus CBstatus = SVGA_CB_STATUS_COMPLETED;
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

# ifdef VBOX_WITH_VMSVGA3D
#  ifdef VMSVGA3D_DX
    /* Commands submitted for the SVGA3D_INVALID_ID context do not affect pipeline. So ignore them. */
    if (idDXContext != SVGA3D_INVALID_ID)
    {
        if (pSvgaR3State->idDXContextCurrent != idDXContext)
        {
            LogFlow(("DXCTX: buffer %d->%d\n", pSvgaR3State->idDXContextCurrent, idDXContext));
            vmsvga3dDXSwitchContext(pThisCC, idDXContext);
            pSvgaR3State->idDXContextCurrent = idDXContext;
        }
    }
#  endif
# endif

    uint32_t RT_UNTRUSTED_VOLATILE_GUEST * const pFIFO = pThisCC->svga.pau32FIFO;

    uint8_t const *pu8Cmd = (uint8_t *)pvCommands;
    uint32_t cbRemain = cbCommands;
    while (cbRemain)
    {
        /* Command identifier is a 32 bit value. */
        if (cbRemain < sizeof(uint32_t))
        {
            CBstatus = SVGA_CB_STATUS_COMMAND_ERROR;
            break;
        }

        /* Fetch the command id.
         * 'cmdId' is actually a SVGAFifoCmdId. It is treated as uint32_t in order to avoid a compiler
         * warning. Because we support some obsolete and deprecated commands, which are not included in
         * the SVGAFifoCmdId enum in the VMSVGA headers anymore.
         */
        uint32_t const cmdId = *(uint32_t *)pu8Cmd;
        uint32_t cbCmd = sizeof(uint32_t);

        LogFlowFunc(("[cid=%d] %s %u\n", (int32_t)idDXContext, vmsvgaR3FifoCmdToString(cmdId), cmdId));
# ifdef LOG_ENABLED
#  ifdef VBOX_WITH_VMSVGA3D
        if (   (cmdId >= SVGA_3D_CMD_BASE && cmdId < SVGA_3D_CMD_MAX)
            || (cmdId >= VBSVGA_3D_CMD_BASE && cmdId < VBSVGA_3D_CMD_MAX))
        {
            SVGA3dCmdHeader const *header = (SVGA3dCmdHeader *)pu8Cmd;
            svga_dump_command(cmdId, (uint8_t *)&header[1], header->size);
        }
        else if (cmdId == SVGA_CMD_FENCE)
        {
            Log7(("\tSVGA_CMD_FENCE\n"));
            Log7(("\t\t0x%08x\n", ((uint32_t *)pu8Cmd)[1]));
        }
#  endif
# endif

        /* At the end of the switch cbCmd is equal to the total length of the command including the cmdId.
         * I.e. pu8Cmd + cbCmd must point to the next command.
         * However if CBstatus is set to anything but SVGA_CB_STATUS_COMPLETED in the switch, then
         * the cbCmd value is ignored (and pu8Cmd still points to the failed command).
         */
        /** @todo This code is very similar to the FIFO loop command processing. Think about merging. */
        switch (cmdId)
        {
            case SVGA_CMD_INVALID_CMD:
            {
                /* Nothing to do. */
                STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3CmdInvalidCmd);
                break;
            }

            case SVGA_CMD_FENCE:
            {
                SVGAFifoCmdFence *pCmd = (SVGAFifoCmdFence *)&pu8Cmd[cbCmd];
                VMSVGA_INC_CMD_SIZE_BREAK(sizeof(*pCmd));
                STAM_REL_COUNTER_INC(&pSvgaR3State->StatR3CmdFence);
                Log(("SVGA_CMD_FENCE %#x\n", pCmd->fence));

                if (pThis->fVmSvga3)
                {
                    pThis->svga.u32FenceLast = pCmd->fence;

                    if (pThis->svga.u32IrqMask & SVGA_IRQFLAG_ANY_FENCE)
                    {
                        Log(("any fence irq\n"));
                        *pu32IrqStatus |= SVGA_IRQFLAG_ANY_FENCE;
                    }
                    else if (pThis->svga.u32IrqMask & SVGA_IRQFLAG_FENCE_GOAL)
                    {
                        Log(("fence goal reached irq (fence=%#x)\n", pCmd->fence));
                        *pu32IrqStatus |= SVGA_IRQFLAG_FENCE_GOAL;
                    }
                }
                else
                {
                    uint32_t const offFifoMin = pFIFO[SVGA_FIFO_MIN];
                    if (VMSVGA_IS_VALID_FIFO_REG(SVGA_FIFO_FENCE, offFifoMin))
                    {
                        pFIFO[SVGA_FIFO_FENCE] = pCmd->fence;

                        if (pThis->svga.u32IrqMask & SVGA_IRQFLAG_ANY_FENCE)
                        {
                            Log(("any fence irq\n"));
                            *pu32IrqStatus |= SVGA_IRQFLAG_ANY_FENCE;
                        }
                        else if (    VMSVGA_IS_VALID_FIFO_REG(SVGA_FIFO_FENCE_GOAL, offFifoMin)
                                 &&  (pThis->svga.u32IrqMask & SVGA_IRQFLAG_FENCE_GOAL)
                                 &&  pFIFO[SVGA_FIFO_FENCE_GOAL] == pCmd->fence)
                        {
                            Log(("fence goal reached irq (fence=%#x)\n", pCmd->fence));
                            *pu32IrqStatus |= SVGA_IRQFLAG_FENCE_GOAL;
                        }
                    }
                    else
                        Log(("SVGA_CMD_FENCE is bogus when offFifoMin is %#x!\n", offFifoMin));
                }
                break;
            }

            case SVGA_CMD_UPDATE:
            {
                SVGAFifoCmdUpdate *pCmd = (SVGAFifoCmdUpdate *)&pu8Cmd[cbCmd];
                VMSVGA_INC_CMD_SIZE_BREAK(sizeof(*pCmd));
                vmsvgaR3CmdUpdate(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_UPDATE_VERBOSE:
            {
                SVGAFifoCmdUpdateVerbose *pCmd = (SVGAFifoCmdUpdateVerbose *)&pu8Cmd[cbCmd];
                VMSVGA_INC_CMD_SIZE_BREAK(sizeof(*pCmd));
                vmsvgaR3CmdUpdateVerbose(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_DEFINE_CURSOR:
            {
                /* Followed by bitmap data. */
                SVGAFifoCmdDefineCursor *pCmd = (SVGAFifoCmdDefineCursor *)&pu8Cmd[cbCmd];
                VMSVGA_INC_CMD_SIZE_BREAK(sizeof(*pCmd));

                /* Figure out the size of the bitmap data. */
                ASSERT_GUEST_STMT_BREAK(pCmd->height < 2048 && pCmd->width < 2048, CBstatus = SVGA_CB_STATUS_COMMAND_ERROR);
                ASSERT_GUEST_STMT_BREAK(pCmd->andMaskDepth <= 32, CBstatus = SVGA_CB_STATUS_COMMAND_ERROR);
                ASSERT_GUEST_STMT_BREAK(pCmd->xorMaskDepth <= 32, CBstatus = SVGA_CB_STATUS_COMMAND_ERROR);
                RT_UNTRUSTED_VALIDATED_FENCE();

                uint32_t const cbAndLine = RT_ALIGN_32(pCmd->width * (pCmd->andMaskDepth + (pCmd->andMaskDepth == 15)), 32) / 8;
                uint32_t const cbAndMask = cbAndLine * pCmd->height;
                uint32_t const cbXorLine = RT_ALIGN_32(pCmd->width * (pCmd->xorMaskDepth + (pCmd->xorMaskDepth == 15)), 32) / 8;
                uint32_t const cbXorMask = cbXorLine * pCmd->height;

                VMSVGA_INC_CMD_SIZE_BREAK(cbAndMask + cbXorMask);
                vmsvgaR3CmdDefineCursor(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_DEFINE_ALPHA_CURSOR:
            {
                /* Followed by bitmap data. */
                SVGAFifoCmdDefineAlphaCursor *pCmd = (SVGAFifoCmdDefineAlphaCursor *)&pu8Cmd[cbCmd];
                VMSVGA_INC_CMD_SIZE_BREAK(sizeof(*pCmd));

                /* Figure out the size of the bitmap data. */
                ASSERT_GUEST_STMT_BREAK(pCmd->height < 2048 && pCmd->width < 2048, CBstatus = SVGA_CB_STATUS_COMMAND_ERROR);

                VMSVGA_INC_CMD_SIZE_BREAK(pCmd->width * pCmd->height * sizeof(uint32_t)); /* 32-bit BRGA format */
                vmsvgaR3CmdDefineAlphaCursor(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_MOVE_CURSOR:
            {
                /* Deprecated; there should be no driver which *requires* this command. However, if
                 * we do ecncounter this command, it might be useful to not get the FIFO completely out of
                 * alignment.
                 * May be issued by guest if SVGA_CAP_CURSOR_BYPASS is missing.
                 */
                SVGAFifoCmdMoveCursor *pCmd = (SVGAFifoCmdMoveCursor *)&pu8Cmd[cbCmd];
                VMSVGA_INC_CMD_SIZE_BREAK(sizeof(*pCmd));
                vmsvgaR3CmdMoveCursor(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_DISPLAY_CURSOR:
            {
                /* Deprecated; there should be no driver which *requires* this command. However, if
                 * we do ecncounter this command, it might be useful to not get the FIFO completely out of
                 * alignment.
                 * May be issued by guest if SVGA_CAP_CURSOR_BYPASS is missing.
                 */
                SVGAFifoCmdDisplayCursor *pCmd = (SVGAFifoCmdDisplayCursor *)&pu8Cmd[cbCmd];
                VMSVGA_INC_CMD_SIZE_BREAK(sizeof(*pCmd));
                vmsvgaR3CmdDisplayCursor(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_RECT_FILL:
            {
                SVGAFifoCmdRectFill *pCmd = (SVGAFifoCmdRectFill *)&pu8Cmd[cbCmd];
                VMSVGA_INC_CMD_SIZE_BREAK(sizeof(*pCmd));
                vmsvgaR3CmdRectFill(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_RECT_COPY:
            {
                SVGAFifoCmdRectCopy *pCmd = (SVGAFifoCmdRectCopy *)&pu8Cmd[cbCmd];
                VMSVGA_INC_CMD_SIZE_BREAK(sizeof(*pCmd));
                vmsvgaR3CmdRectCopy(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_RECT_ROP_COPY:
            {
                SVGAFifoCmdRectRopCopy *pCmd = (SVGAFifoCmdRectRopCopy *)&pu8Cmd[cbCmd];
                VMSVGA_INC_CMD_SIZE_BREAK(sizeof(*pCmd));
                vmsvgaR3CmdRectRopCopy(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_ESCAPE:
            {
                /* Followed by 'size' bytes of data. */
                SVGAFifoCmdEscape *pCmd = (SVGAFifoCmdEscape *)&pu8Cmd[cbCmd];
                VMSVGA_INC_CMD_SIZE_BREAK(sizeof(*pCmd));

                ASSERT_GUEST_STMT_BREAK(pCmd->size < pThis->svga.cbFIFO - sizeof(SVGAFifoCmdEscape), CBstatus = SVGA_CB_STATUS_COMMAND_ERROR);
                RT_UNTRUSTED_VALIDATED_FENCE();

                VMSVGA_INC_CMD_SIZE_BREAK(pCmd->size);
                vmsvgaR3CmdEscape(pThis, pThisCC, pCmd);
                break;
            }
# ifdef VBOX_WITH_VMSVGA3D
            case SVGA_CMD_DEFINE_GMR2:
            {
                SVGAFifoCmdDefineGMR2 *pCmd = (SVGAFifoCmdDefineGMR2 *)&pu8Cmd[cbCmd];
                VMSVGA_INC_CMD_SIZE_BREAK(sizeof(*pCmd));
                vmsvgaR3CmdDefineGMR2(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_REMAP_GMR2:
            {
                /* Followed by page descriptors or guest ptr. */
                SVGAFifoCmdRemapGMR2 *pCmd = (SVGAFifoCmdRemapGMR2 *)&pu8Cmd[cbCmd];
                VMSVGA_INC_CMD_SIZE_BREAK(sizeof(*pCmd));

                /* Calculate the size of what comes after next and fetch it. */
                uint32_t cbMore = 0;
                if (pCmd->flags & SVGA_REMAP_GMR2_VIA_GMR)
                    cbMore = sizeof(SVGAGuestPtr);
                else
                {
                    uint32_t const cbPageDesc = (pCmd->flags & SVGA_REMAP_GMR2_PPN64) ? sizeof(uint64_t) : sizeof(uint32_t);
                    if (pCmd->flags & SVGA_REMAP_GMR2_SINGLE_PPN)
                    {
                        cbMore         = cbPageDesc;
                        pCmd->numPages = 1;
                    }
                    else
                    {
                        ASSERT_GUEST_STMT_BREAK(pCmd->numPages <= pThis->svga.cbFIFO / cbPageDesc, CBstatus = SVGA_CB_STATUS_COMMAND_ERROR);
                        cbMore = cbPageDesc * pCmd->numPages;
                    }
                }
                VMSVGA_INC_CMD_SIZE_BREAK(cbMore);
                vmsvgaR3CmdRemapGMR2(pThis, pThisCC, pCmd);
#  ifdef DEBUG_GMR_ACCESS
                VMR3ReqCallWaitU(PDMDevHlpGetUVM(pDevIns), VMCPUID_ANY, (PFNRT)vmsvgaR3RegisterGmr, 2, pDevIns, pCmd->gmrId);
#  endif
                break;
            }
# endif /* VBOX_WITH_VMSVGA3D */
            case SVGA_CMD_DEFINE_SCREEN:
            {
                /* The size of this command is specified by the guest and depends on capabilities. */
                SVGAFifoCmdDefineScreen *pCmd = (SVGAFifoCmdDefineScreen *)&pu8Cmd[cbCmd];
                VMSVGA_INC_CMD_SIZE_BREAK(sizeof(pCmd->screen.structSize));
                ASSERT_GUEST_STMT_BREAK(pCmd->screen.structSize < pThis->svga.cbFIFO, CBstatus = SVGA_CB_STATUS_COMMAND_ERROR);
                RT_UNTRUSTED_VALIDATED_FENCE();

                VMSVGA_INC_CMD_SIZE_BREAK(RT_MAX(sizeof(pCmd->screen.structSize), pCmd->screen.structSize) - sizeof(pCmd->screen.structSize));
                vmsvgaR3CmdDefineScreen(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_DESTROY_SCREEN:
            {
                SVGAFifoCmdDestroyScreen *pCmd = (SVGAFifoCmdDestroyScreen *)&pu8Cmd[cbCmd];
                VMSVGA_INC_CMD_SIZE_BREAK(sizeof(*pCmd));
                vmsvgaR3CmdDestroyScreen(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_DEFINE_GMRFB:
            {
                SVGAFifoCmdDefineGMRFB *pCmd = (SVGAFifoCmdDefineGMRFB *)&pu8Cmd[cbCmd];
                VMSVGA_INC_CMD_SIZE_BREAK(sizeof(*pCmd));
                vmsvgaR3CmdDefineGMRFB(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_BLIT_GMRFB_TO_SCREEN:
            {
                SVGAFifoCmdBlitGMRFBToScreen *pCmd = (SVGAFifoCmdBlitGMRFBToScreen *)&pu8Cmd[cbCmd];
                VMSVGA_INC_CMD_SIZE_BREAK(sizeof(*pCmd));
                vmsvgaR3CmdBlitGMRFBToScreen(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_BLIT_SCREEN_TO_GMRFB:
            {
                SVGAFifoCmdBlitScreenToGMRFB *pCmd = (SVGAFifoCmdBlitScreenToGMRFB *)&pu8Cmd[cbCmd];
                VMSVGA_INC_CMD_SIZE_BREAK(sizeof(*pCmd));
                vmsvgaR3CmdBlitScreenToGMRFB(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_ANNOTATION_FILL:
            {
                SVGAFifoCmdAnnotationFill *pCmd = (SVGAFifoCmdAnnotationFill *)&pu8Cmd[cbCmd];
                VMSVGA_INC_CMD_SIZE_BREAK(sizeof(*pCmd));
                vmsvgaR3CmdAnnotationFill(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_ANNOTATION_COPY:
            {
                SVGAFifoCmdAnnotationCopy *pCmd = (SVGAFifoCmdAnnotationCopy *)&pu8Cmd[cbCmd];
                VMSVGA_INC_CMD_SIZE_BREAK(sizeof(*pCmd));
                vmsvgaR3CmdAnnotationCopy(pThis, pThisCC, pCmd);
                break;
            }

            default:
            {
# ifdef VBOX_WITH_VMSVGA3D
                if (   (cmdId >= SVGA_3D_CMD_BASE && cmdId < SVGA_3D_CMD_MAX)
                    || (   pThis->svga.fVBoxExtensions
                        && (cmdId >= VBSVGA_3D_CMD_BASE && cmdId < VBSVGA_3D_CMD_MAX)))
                {
                    RT_UNTRUSTED_VALIDATED_FENCE();

                    /* All 3d commands start with a common header, which defines the identifier and the size
                     * of the command. The identifier has been already read. Fetch the size.
                     */
                    uint32_t const *pcbMore = (uint32_t const *)&pu8Cmd[cbCmd];
                    VMSVGA_INC_CMD_SIZE_BREAK(sizeof(*pcbMore));
                    VMSVGA_INC_CMD_SIZE_BREAK(*pcbMore);
                    if (RT_LIKELY(pThis->svga.f3DEnabled))
                    { /* likely */ }
                    else
                    {
                        if (pThis->svga.fVMSVGA2dGBO &&
                            (cmdId == SVGA_3D_CMD_SET_OTABLE_BASE64 ||
                            cmdId == SVGA_3D_CMD_DEFINE_GB_MOB64   ||
                            cmdId == SVGA_3D_CMD_DESTROY_GB_MOB    ||
                            cmdId == SVGA_3D_CMD_DEFINE_GB_SURFACE ||
                            cmdId == SVGA_3D_CMD_DESTROY_GB_SURFACE ||
                            cmdId == SVGA_3D_CMD_BIND_GB_SURFACE   ||
                            cmdId == SVGA_3D_CMD_INVALIDATE_GB_SURFACE ||
                            cmdId == SVGA_3D_CMD_DEFINE_GB_SCREENTARGET ||
                            cmdId == SVGA_3D_CMD_DESTROY_GB_SCREENTARGET ||
                            cmdId == SVGA_3D_CMD_BIND_GB_SCREENTARGET ||
                            cmdId == SVGA_3D_CMD_UPDATE_GB_IMAGE      ||
                            cmdId == SVGA_3D_CMD_UPDATE_GB_SCREENTARGET ||
                            cmdId == SVGA_3D_CMD_SURFACE_COPY)
                        )
                        {
                            LogRelMax(8, ("VMSVGA: 3D disabled, but command %d will be processed\n", cmdId));
                        }
                        else
                        {
                            LogRelMax(8, ("VMSVGA: 3D disabled, command %d skipped\n", cmdId));
                            break;
                        }
                    }

                    /* Command data begins after the 32 bit command length. */
                    int rc = vmsvgaR3Process3dCmd(pThis, pThisCC, idDXContext, (SVGAFifo3dCmdId)cmdId, *pcbMore, pcbMore + 1);
                    if (RT_SUCCESS(rc))
                    { /* likely */ }
                    else
                    {
                        CBstatus = SVGA_CB_STATUS_COMMAND_ERROR;
                        break;
                    }
                }
                else
# endif /* VBOX_WITH_VMSVGA3D */
                {
                    /* Unsupported command. */
                    STAM_REL_COUNTER_INC(&pSvgaR3State->StatFifoUnkCmds);
                    ASSERT_GUEST_MSG_FAILED(("cmdId=%d\n", cmdId));
                    LogRelMax(16, ("VMSVGA: unsupported command %d\n", cmdId));
                    CBstatus = SVGA_CB_STATUS_COMMAND_ERROR;
                    break;
                }
            }
        }

        if (CBstatus != SVGA_CB_STATUS_COMPLETED)
            break;

        pu8Cmd += cbCmd;
        cbRemain -= cbCmd;

        /* If this is not the last command in the buffer, then generate IRQ, if required.
         * This avoids a double call to vmsvgaR3CmdBufRaiseIRQ if FENCE is the last command
         * in the buffer (usually the case).
         */
        if (RT_LIKELY(!(cbRemain && *pu32IrqStatus)))
        { /* likely */ }
        else
        {
            vmsvgaR3CmdBufRaiseIRQ(pDevIns, pThis, *pu32IrqStatus);
            *pu32IrqStatus = 0;
        }
    }

    Assert(cbRemain <= cbCommands);
    *poffNextCmd = cbCommands - cbRemain;
    return CBstatus;
}


/** Process command buffers.
 *
 * @param pDevIns     The device instance.
 * @param pThis       The shared VGA/VMSVGA state.
 * @param pThisCC     The VGA/VMSVGA state for the current context.
 * @param pThread     Handle of the FIFO thread.
 * @thread FIFO
 */
static void vmsvgaR3CmdBufProcessBuffers(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC, PPDMTHREAD pThread)
{
    PVMSVGAR3STATE const pSvgaR3State = pThisCC->svga.pSvgaR3State;

    for (;;)
    {
        if (pThread->enmState != PDMTHREADSTATE_RUNNING)
            break;

        /* See if there is a submitted buffer. */
        PVMSVGACMDBUF pCmdBuf = NULL;

        int rc = RTCritSectEnter(&pSvgaR3State->CritSectCmdBuf);
        AssertRC(rc);

        /* It seems that a higher queue index has a higher priority.
         * See SVGACBContext in svga_reg.h from latest vmwgfx Linux driver.
         */
        for (unsigned i = RT_ELEMENTS(pSvgaR3State->apCmdBufCtxs); i > 0; --i)
        {
            PVMSVGACMDBUFCTX pCmdBufCtx = pSvgaR3State->apCmdBufCtxs[i - 1];
            if (pCmdBufCtx)
            {
                pCmdBuf = RTListRemoveFirst(&pCmdBufCtx->listSubmitted, VMSVGACMDBUF, nodeBuffer);
                if (pCmdBuf)
                {
                    Assert(pCmdBufCtx->cSubmitted > 0);
                    --pCmdBufCtx->cSubmitted;
                    break;
                }
            }
        }

        if (!pCmdBuf)
        {
            ASMAtomicWriteU32(&pSvgaR3State->fCmdBuf, 0);
            RTCritSectLeave(&pSvgaR3State->CritSectCmdBuf);
            break;
        }

        RTCritSectLeave(&pSvgaR3State->CritSectCmdBuf);

        SVGACBStatus CBstatus = SVGA_CB_STATUS_NONE;
        uint32_t offNextCmd = 0;
        uint32_t u32IrqStatus = 0;
        uint32_t const idDXContext = RT_BOOL(pCmdBuf->hdr.flags & SVGA_CB_FLAG_DX_CONTEXT)
                                   ? pCmdBuf->hdr.dxContext
                                   : SVGA3D_INVALID_ID;
        /* Process one buffer. */
        CBstatus = vmsvgaR3CmdBufProcessCommands(pDevIns, pThis, pThisCC, idDXContext, pCmdBuf->pvCommands, pCmdBuf->hdr.length, &offNextCmd, &u32IrqStatus);

        if (!RT_BOOL(pCmdBuf->hdr.flags & SVGA_CB_FLAG_NO_IRQ))
            u32IrqStatus |= SVGA_IRQFLAG_COMMAND_BUFFER;
        if (CBstatus == SVGA_CB_STATUS_COMMAND_ERROR)
            u32IrqStatus |= SVGA_IRQFLAG_ERROR;

        vmsvgaR3CmdBufWriteStatus(pDevIns, pCmdBuf->GCPhysCB, CBstatus, offNextCmd);
        if (u32IrqStatus)
            vmsvgaR3CmdBufRaiseIRQ(pDevIns, pThis, u32IrqStatus);

        vmsvgaR3CmdBufFree(pCmdBuf);
    }
}


/**
 * Worker for vmsvgaR3FifoThread that handles an external command.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The shared VGA/VMSVGA instance data.
 * @param   pThisCC         The VGA/VMSVGA state for ring-3.
 */
static void vmsvgaR3FifoHandleExtCmd(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC)
{
    uint8_t uExtCmd = pThis->svga.u8FIFOExtCommand;
    switch (pThis->svga.u8FIFOExtCommand)
    {
        case VMSVGA_FIFO_EXTCMD_RESET:
            Log(("vmsvgaR3FifoLoop: reset the fifo thread.\n"));
            Assert(pThisCC->svga.pvFIFOExtCmdParam == NULL);

            vmsvgaR3ResetScreens(pThis, pThisCC);
# ifdef VBOX_WITH_VMSVGA3D
            /* The 3d subsystem must be reset from the fifo thread. */
            if (pThis->svga.f3DEnabled)
                vmsvga3dReset(pThisCC);
# endif
            vmsvgaR3ResetSvgaState(pThis, pThisCC);
            break;

        case VMSVGA_FIFO_EXTCMD_POWEROFF:
            Log(("vmsvgaR3FifoLoop: power off.\n"));
            Assert(pThisCC->svga.pvFIFOExtCmdParam == NULL);

            /* The screens must be reset on the FIFO thread, because they may use 3D resources. */
            vmsvgaR3ResetScreens(pThis, pThisCC);
            break;

        case VMSVGA_FIFO_EXTCMD_TERMINATE:
            Log(("vmsvgaR3FifoLoop: terminate the fifo thread.\n"));
            Assert(pThisCC->svga.pvFIFOExtCmdParam == NULL);

# ifdef VBOX_WITH_VMSVGA3D
            /* The 3d subsystem must be shut down from the fifo thread. */
            if (pThis->svga.f3DEnabled)
                vmsvga3dTerminate(pThisCC);
# endif
            vmsvgaR3TerminateSvgaState(pThis, pThisCC);
            break;

        case VMSVGA_FIFO_EXTCMD_SAVESTATE:
        {
            Log(("vmsvgaR3FifoLoop: VMSVGA_FIFO_EXTCMD_SAVESTATE.\n"));
            PSSMHANDLE pSSM = (PSSMHANDLE)pThisCC->svga.pvFIFOExtCmdParam;
            AssertLogRelMsgBreak(RT_VALID_PTR(pSSM), ("pSSM=%p\n", pSSM));
            vmsvgaR3SaveExecFifo(pDevIns->pHlpR3, pThisCC, pSSM);
# ifdef VBOX_WITH_VMSVGA3D
            if (pThis->svga.f3DEnabled || pThis->svga.fVMSVGA2dGBO)
            {
                if (vmsvga3dIsLegacyBackend(pThisCC))
                    vmsvga3dSaveExec(pDevIns, pThisCC, pSSM);
#  ifdef VMSVGA3D_DX
                else
                    vmsvga3dDXSaveExec(pDevIns, pThisCC, pSSM);
#  endif
            }
# endif
            break;
        }

        case VMSVGA_FIFO_EXTCMD_LOADSTATE:
        {
            Log(("vmsvgaR3FifoLoop: VMSVGA_FIFO_EXTCMD_LOADSTATE.\n"));
            PVMSVGA_STATE_LOAD pLoadState = (PVMSVGA_STATE_LOAD)pThisCC->svga.pvFIFOExtCmdParam;
            AssertLogRelMsgBreak(RT_VALID_PTR(pLoadState), ("pLoadState=%p\n", pLoadState));
            vmsvgaR3LoadExecFifo(pDevIns->pHlpR3, pThis, pThisCC, pLoadState->pSSM, pLoadState->uVersion, pLoadState->uPass);
# ifdef VBOX_WITH_VMSVGA3D
            if (pThis->svga.f3DEnabled || pThis->svga.fVMSVGA2dGBO)
            {
                /* The following RT_OS_DARWIN code was in vmsvga3dLoadExec and therefore must be executed before each vmsvga3dLoadExec invocation. */
#  ifndef RT_OS_DARWIN /** @todo r=bird: this is normally done on the EMT, so for DARWIN we do that when loading saved state too now. See DevVGA-SVGA.cpp */
                /* Must initialize now as the recreation calls below rely on an initialized 3d subsystem. */
                vmsvgaR3PowerOnDevice(pDevIns, pThis, pThisCC, /*fLoadState=*/ true);
#  endif

                if (vmsvga3dIsLegacyBackend(pThisCC))
                    vmsvga3dLoadExec(pDevIns, pThis, pThisCC, pLoadState->pSSM, pLoadState->uVersion, pLoadState->uPass);
#  ifdef VMSVGA3D_DX
                else
                    vmsvga3dDXLoadExec(pDevIns, pThis, pThisCC, pLoadState->pSSM, pLoadState->uVersion, pLoadState->uPass);
#  endif
            }
# endif
            break;
        }

        case VMSVGA_FIFO_EXTCMD_UPDATE_SURFACE_HEAP_BUFFERS:
        {
# ifdef VBOX_WITH_VMSVGA3D
            uint32_t sid = (uint32_t)(uintptr_t)pThisCC->svga.pvFIFOExtCmdParam;
            Log(("vmsvgaR3FifoLoop: VMSVGA_FIFO_EXTCMD_UPDATE_SURFACE_HEAP_BUFFERS sid=%#x\n", sid));
            vmsvga3dUpdateHeapBuffersForSurfaces(pThisCC, sid);
# endif
            break;
        }


        default:
            AssertLogRelMsgFailed(("uExtCmd=%#x pvFIFOExtCmdParam=%p\n", uExtCmd, pThisCC->svga.pvFIFOExtCmdParam));
            break;
    }

    /*
     * Signal the end of the external command.
     */
    pThisCC->svga.pvFIFOExtCmdParam = NULL;
    pThis->svga.u8FIFOExtCommand  = VMSVGA_FIFO_EXTCMD_NONE;
    ASMMemoryFence(); /* paranoia^2 */
    int rc = RTSemEventSignal(pThisCC->svga.hFIFOExtCmdSem);
    AssertLogRelRC(rc);
}

/**
 * Worker for vmsvgaR3Destruct, vmsvgaR3Reset, vmsvgaR3Save and vmsvgaR3Load for
 * doing a job on the FIFO thread (even when it's officially suspended).
 *
 * @returns VBox status code (fully asserted).
 * @param   pDevIns         The device instance.
 * @param   pThis           The shared VGA/VMSVGA instance data.
 * @param   pThisCC         The VGA/VMSVGA state for ring-3.
 * @param   uExtCmd         The command to execute on the FIFO thread.
 * @param   pvParam         Pointer to command parameters.
 * @param   cMsWait         The time to wait for the command, given in
 *                          milliseconds.
 */
static int vmsvgaR3RunExtCmdOnFifoThread(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC,
                                         uint8_t uExtCmd, void *pvParam, RTMSINTERVAL cMsWait)
{
    Assert(cMsWait >= RT_MS_1SEC * 5);
    AssertLogRelMsg(pThis->svga.u8FIFOExtCommand == VMSVGA_FIFO_EXTCMD_NONE,
                    ("old=%d new=%d\n", pThis->svga.u8FIFOExtCommand, uExtCmd));

    int rc;
    PPDMTHREAD      pThread  = pThisCC->svga.pFIFOIOThread;
    PDMTHREADSTATE  enmState = pThread->enmState;
    if (enmState == PDMTHREADSTATE_SUSPENDED)
    {
        /*
         * The thread is suspended, we have to temporarily wake it up so it can
         * perform the task.
         * (We ASSUME not racing code here, both wrt thread state and ext commands.)
         */
        Log(("vmsvgaR3RunExtCmdOnFifoThread: uExtCmd=%d enmState=SUSPENDED\n", uExtCmd));
        /* Post the request. */
        pThis->svga.fFifoExtCommandWakeup = true;
        pThisCC->svga.pvFIFOExtCmdParam     = pvParam;
        pThis->svga.u8FIFOExtCommand      = uExtCmd;
        ASMMemoryFence(); /* paranoia^3 */

        /* Resume the thread. */
        rc = PDMDevHlpThreadResume(pDevIns, pThread);
        AssertLogRelRC(rc);
        if (RT_SUCCESS(rc))
        {
            /* Wait. Take care in case the semaphore was already posted (same as below). */
            rc = RTSemEventWait(pThisCC->svga.hFIFOExtCmdSem, cMsWait);
            if (   rc == VINF_SUCCESS
                && pThis->svga.u8FIFOExtCommand == uExtCmd)
                rc = RTSemEventWait(pThisCC->svga.hFIFOExtCmdSem, cMsWait);
            AssertLogRelMsg(pThis->svga.u8FIFOExtCommand != uExtCmd || RT_FAILURE_NP(rc),
                            ("%#x %Rrc\n", pThis->svga.u8FIFOExtCommand, rc));

            /* suspend the thread */
            pThis->svga.fFifoExtCommandWakeup = false;
            int rc2 = PDMDevHlpThreadSuspend(pDevIns, pThread);
            AssertLogRelRC(rc2);
            if (RT_FAILURE(rc2) && RT_SUCCESS(rc))
                rc = rc2;
        }
        pThis->svga.fFifoExtCommandWakeup = false;
        pThisCC->svga.pvFIFOExtCmdParam     = NULL;
    }
    else if (enmState == PDMTHREADSTATE_RUNNING)
    {
        /*
         * The thread is running, should only happen during reset and vmsvga3dsfc.
         * We ASSUME not racing code here, both wrt thread state and ext commands.
         */
        Log(("vmsvgaR3RunExtCmdOnFifoThread: uExtCmd=%d enmState=RUNNING\n", uExtCmd));
        Assert(uExtCmd == VMSVGA_FIFO_EXTCMD_RESET || uExtCmd == VMSVGA_FIFO_EXTCMD_UPDATE_SURFACE_HEAP_BUFFERS || uExtCmd == VMSVGA_FIFO_EXTCMD_POWEROFF);

        /* Post the request. */
        pThisCC->svga.pvFIFOExtCmdParam = pvParam;
        pThis->svga.u8FIFOExtCommand  = uExtCmd;
        ASMMemoryFence(); /* paranoia^2 */
        rc = PDMDevHlpSUPSemEventSignal(pDevIns, pThis->svga.hFIFORequestSem);
        AssertLogRelRC(rc);

        /* Wait. Take care in case the semaphore was already posted (same as above). */
        rc = RTSemEventWait(pThisCC->svga.hFIFOExtCmdSem, cMsWait);
        if (   rc == VINF_SUCCESS
            && pThis->svga.u8FIFOExtCommand == uExtCmd)
            rc = RTSemEventWait(pThisCC->svga.hFIFOExtCmdSem, cMsWait); /* it was already posted, retry the wait. */
        AssertLogRelMsg(pThis->svga.u8FIFOExtCommand != uExtCmd || RT_FAILURE_NP(rc),
                        ("%#x %Rrc\n", pThis->svga.u8FIFOExtCommand, rc));

        pThisCC->svga.pvFIFOExtCmdParam = NULL;
    }
    else
    {
        /*
         * Something is wrong with the thread!
         */
        AssertLogRelMsgFailed(("uExtCmd=%d enmState=%d\n", uExtCmd, enmState));
        rc = VERR_INVALID_STATE;
    }
    return rc;
}


/**
 * Marks the FIFO non-busy, notifying any waiting EMTs.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The shared VGA/VMSVGA instance data.
 * @param   pThisCC         The VGA/VMSVGA state for ring-3.
 * @param   pSVGAState      Pointer to the ring-3 only SVGA state data.
 * @param   offFifoMin      The start byte offset of the command FIFO.
 */
static void vmsvgaR3FifoSetNotBusy(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC, PVMSVGAR3STATE pSVGAState, uint32_t offFifoMin)
{
    ASMAtomicAndU32(&pThis->svga.fBusy, ~(VMSVGA_BUSY_F_FIFO | VMSVGA_BUSY_F_EMT_FORCE));
    if (VMSVGA_IS_VALID_FIFO_REG(SVGA_FIFO_BUSY, offFifoMin))
        vmsvgaHCSafeFifoBusyRegUpdate(pThis, pThisCC, pThis->svga.fBusy != 0);

    /* Wake up any waiting EMTs. */
    if (pSVGAState->cBusyDelayedEmts > 0)
    {
# ifdef VMSVGA_USE_EMT_HALT_CODE
        VMCPUID idCpu = VMCpuSetFindLastPresentInternal(&pSVGAState->BusyDelayedEmts);
        if (idCpu != NIL_VMCPUID)
        {
            PDMDevHlpVMNotifyCpuDeviceReady(pDevIns, idCpu);
            while (idCpu-- > 0)
                if (VMCPUSET_IS_PRESENT(&pSVGAState->BusyDelayedEmts, idCpu))
                    PDMDevHlpVMNotifyCpuDeviceReady(pDevIns, idCpu);
        }
# else
        int rc2 = RTSemEventMultiSignal(pSVGAState->hBusyDelayedEmts);
        AssertRC(rc2);
# endif
    }
}

/**
 * Reads (more) payload into the command buffer.
 *
 * @returns pbBounceBuf on success
 * @retval  (void *)1 if the thread was requested to stop.
 * @retval  NULL on FIFO error.
 *
 * @param   cbPayloadReq    The number of bytes of payload requested.
 * @param   pFIFO           The FIFO.
 * @param   offCurrentCmd   The FIFO byte offset of the current command.
 * @param   offFifoMin      The start byte offset of the command FIFO.
 * @param   offFifoMax      The end byte offset of the command FIFO.
 * @param   pbBounceBuf     The bounch buffer. Same size as the entire FIFO, so
 *                          always sufficient size.
 * @param   pcbAlreadyRead  How much payload we've already read into the bounce
 *                          buffer. (We will NEVER re-read anything.)
 * @param   pThread         The calling PDM thread handle.
 * @param   pThis           The shared VGA/VMSVGA instance data.
 * @param   pSVGAState      Pointer to the ring-3 only SVGA state data. For
 *                          statistics collection.
 * @param   pDevIns         The device instance.
 */
static void *vmsvgaR3FifoGetCmdPayload(uint32_t cbPayloadReq, uint32_t RT_UNTRUSTED_VOLATILE_GUEST *pFIFO,
                                       uint32_t offCurrentCmd, uint32_t offFifoMin, uint32_t offFifoMax,
                                       uint8_t *pbBounceBuf, uint32_t *pcbAlreadyRead,
                                       PPDMTHREAD pThread, PVGASTATE pThis, PVMSVGAR3STATE pSVGAState, PPDMDEVINS pDevIns)
{
    Assert(pbBounceBuf);
    Assert(pcbAlreadyRead);
    Assert(offFifoMin < offFifoMax);
    Assert(offCurrentCmd >= offFifoMin && offCurrentCmd < offFifoMax);
    Assert(offFifoMax <= pThis->svga.cbFIFO);

    /*
     * Check if the requested payload size has already been satisfied                                                                                                .
     *                                                                                                                                                       .
     * When called to read more, the caller is responsible for making sure the                                                                               .
     * new command size (cbRequsted) never is smaller than what has already                                                                                  .
     * been read.
     */
    uint32_t cbAlreadyRead = *pcbAlreadyRead;
    if (cbPayloadReq <= cbAlreadyRead)
    {
        AssertLogRelReturn(cbPayloadReq == cbAlreadyRead, NULL);
        return pbBounceBuf;
    }

    /*
     * Commands bigger than the fifo buffer are invalid.
     */
    uint32_t const cbFifoCmd = offFifoMax - offFifoMin;
    AssertMsgReturnStmt(cbPayloadReq <= cbFifoCmd, ("cbPayloadReq=%#x cbFifoCmd=%#x\n", cbPayloadReq, cbFifoCmd),
                        STAM_REL_COUNTER_INC(&pSVGAState->StatFifoErrors),
                        NULL);

    /*
     * Move offCurrentCmd past the command dword.
     */
    offCurrentCmd += sizeof(uint32_t);
    if (offCurrentCmd >= offFifoMax)
        offCurrentCmd = offFifoMin;

    /*
     * Do we have sufficient payload data available already?
     * The host should not read beyond [SVGA_FIFO_NEXT_CMD], therefore '>=' in the condition below.
     */
    uint32_t cbAfter, cbBefore;
    uint32_t offNextCmd = pFIFO[SVGA_FIFO_NEXT_CMD];
    RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();
    if (offNextCmd >= offCurrentCmd)
    {
        if (RT_LIKELY(offNextCmd < offFifoMax))
            cbAfter = offNextCmd - offCurrentCmd;
        else
        {
            STAM_REL_COUNTER_INC(&pSVGAState->StatFifoErrors);
            LogRelMax(16, ("vmsvgaR3FifoGetCmdPayload: Invalid offNextCmd=%#x (offFifoMin=%#x offFifoMax=%#x)\n",
                           offNextCmd, offFifoMin, offFifoMax));
            cbAfter = offFifoMax - offCurrentCmd;
        }
        cbBefore = 0;
    }
    else
    {
        cbAfter  = offFifoMax - offCurrentCmd;
        if (offNextCmd >= offFifoMin)
            cbBefore = offNextCmd - offFifoMin;
        else
        {
            STAM_REL_COUNTER_INC(&pSVGAState->StatFifoErrors);
            LogRelMax(16, ("vmsvgaR3FifoGetCmdPayload: Invalid offNextCmd=%#x (offFifoMin=%#x offFifoMax=%#x)\n",
                           offNextCmd, offFifoMin, offFifoMax));
            cbBefore = 0;
        }
    }
    if (cbAfter + cbBefore < cbPayloadReq)
    {
        /*
         * Insufficient, must wait for it to arrive.
         */
/** @todo Should clear the busy flag here to maybe encourage the guest to wake us up. */
        STAM_REL_PROFILE_START(&pSVGAState->StatFifoStalls, Stall);
        for (uint32_t i = 0;; i++)
        {
            if (pThread->enmState != PDMTHREADSTATE_RUNNING)
            {
                STAM_REL_PROFILE_STOP(&pSVGAState->StatFifoStalls, Stall);
                return (void *)(uintptr_t)1;
            }
            Log(("Guest still copying (%x vs %x) current %x next %x stop %x loop %u; sleep a bit\n",
                 cbPayloadReq, cbAfter + cbBefore, offCurrentCmd, offNextCmd, pFIFO[SVGA_FIFO_STOP], i));

            PDMDevHlpSUPSemEventWaitNoResume(pDevIns, pThis->svga.hFIFORequestSem, i < 16 ? 1 : 2);

            offNextCmd = pFIFO[SVGA_FIFO_NEXT_CMD];
            RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();
            if (offNextCmd >= offCurrentCmd)
            {
                cbAfter = RT_MIN(offNextCmd, offFifoMax) - offCurrentCmd;
                cbBefore = 0;
            }
            else
            {
                cbAfter  = offFifoMax - offCurrentCmd;
                cbBefore = RT_MAX(offNextCmd, offFifoMin) - offFifoMin;
            }

            if (cbAfter + cbBefore >= cbPayloadReq)
                break;
        }
        STAM_REL_PROFILE_STOP(&pSVGAState->StatFifoStalls, Stall);
    }

    /*
     * Copy out the memory and update what pcbAlreadyRead points to.
     */
    if (cbAfter >= cbPayloadReq)
        memcpy(pbBounceBuf + cbAlreadyRead,
               (uint8_t *)pFIFO + offCurrentCmd + cbAlreadyRead,
               cbPayloadReq - cbAlreadyRead);
    else
    {
        LogFlow(("Split data buffer at %x (%u-%u)\n", offCurrentCmd, cbAfter, cbBefore));
        if (cbAlreadyRead < cbAfter)
        {
            memcpy(pbBounceBuf + cbAlreadyRead,
                   (uint8_t *)pFIFO + offCurrentCmd + cbAlreadyRead,
                   cbAfter - cbAlreadyRead);
            cbAlreadyRead = cbAfter;
        }
        memcpy(pbBounceBuf + cbAlreadyRead,
               (uint8_t *)pFIFO + offFifoMin + cbAlreadyRead - cbAfter,
               cbPayloadReq - cbAlreadyRead);
    }
    *pcbAlreadyRead = cbPayloadReq;
    RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();
    return pbBounceBuf;
}


/**
 * Sends cursor position and visibility information from the FIFO to the front-end.
 * @returns SVGA_FIFO_CURSOR_COUNT value used.
 */
static uint32_t
vmsvgaR3FifoUpdateCursor(PVGASTATECC pThisCC, PVMSVGAR3STATE  pSVGAState, uint32_t RT_UNTRUSTED_VOLATILE_GUEST *pFIFO,
                         uint32_t offFifoMin,  uint32_t uCursorUpdateCount,
                         uint32_t *pxLast, uint32_t *pyLast, uint32_t *pfLastVisible)
{
    /*
     * Check if the cursor update counter has changed and try get a stable
     * set of values if it has.  This is race-prone, especially consindering
     * the screen ID, but little we can do about that.
     */
    uint32_t x, y, fVisible, idScreen;
    for (uint32_t i = 0; ; i++)
    {
        x        = pFIFO[SVGA_FIFO_CURSOR_X];
        y        = pFIFO[SVGA_FIFO_CURSOR_Y];
        fVisible = pFIFO[SVGA_FIFO_CURSOR_ON];
        idScreen = VMSVGA_IS_VALID_FIFO_REG(SVGA_FIFO_CURSOR_SCREEN_ID, offFifoMin)
                 ? pFIFO[SVGA_FIFO_CURSOR_SCREEN_ID] : SVGA_ID_INVALID;
        if (   uCursorUpdateCount == pFIFO[SVGA_FIFO_CURSOR_COUNT]
            || i > 3)
            break;
        if (i == 0)
            STAM_REL_COUNTER_INC(&pSVGAState->StatFifoCursorFetchAgain);
        ASMNopPause();
        uCursorUpdateCount = pFIFO[SVGA_FIFO_CURSOR_COUNT];
    }

    /*
     * Check if anything has changed, as calling into pDrv is not light-weight.
     */
    if (   *pxLast == x
        && *pyLast == y
        && (idScreen != SVGA_ID_INVALID || *pfLastVisible == fVisible))
        STAM_REL_COUNTER_INC(&pSVGAState->StatFifoCursorNoChange);
    else
    {
        /*
         * Detected changes.
         *
         * We handle global, not per-screen visibility information by sending
         * pfnVBVAMousePointerShape without shape data.
         */
        *pxLast = x;
        *pyLast = y;
        uint32_t fFlags = VBVA_CURSOR_VALID_DATA;
        if (idScreen != SVGA_ID_INVALID)
            fFlags |= VBVA_CURSOR_SCREEN_RELATIVE;
        else if (*pfLastVisible != fVisible)
        {
            LogRel2(("vmsvgaR3FifoUpdateCursor: fVisible %d fLastVisible %d (%d,%d)\n", fVisible, *pfLastVisible, x, y));
            *pfLastVisible = fVisible;
            pThisCC->pDrv->pfnVBVAMousePointerShape(pThisCC->pDrv, RT_BOOL(fVisible), false, 0, 0, 0, 0, NULL);
            STAM_REL_COUNTER_INC(&pSVGAState->StatFifoCursorVisiblity);
        }
        pThisCC->pDrv->pfnVBVAReportCursorPosition(pThisCC->pDrv, fFlags, idScreen, x, y);
        STAM_REL_COUNTER_INC(&pSVGAState->StatFifoCursorPosition);
    }

    /*
     * Update done.  Signal this to the guest.
     */
    pFIFO[SVGA_FIFO_CURSOR_LAST_UPDATED] = uCursorUpdateCount;

    return uCursorUpdateCount;
}


/**
 * Checks if there is work to be done, either cursor updating or FIFO commands.
 *
 * @returns true if pending work, false if not.
 * @param   pThisCC             The VGA/VMSVGA state for ring-3.
 * @param   uLastCursorCount    The last cursor update counter value.
 */
DECLINLINE(bool) vmsvgaR3FifoHasWork(PVGASTATECC pThisCC, uint32_t uLastCursorCount)
{
    /* If FIFO does not exist than there is nothing to do. Command buffers also require the enabled FIFO. */
    uint32_t RT_UNTRUSTED_VOLATILE_GUEST * const pFIFO = pThisCC->svga.pau32FIFO;
    AssertReturn(pFIFO, false);

    if (vmsvgaR3CmdBufHasWork(pThisCC))
        return true;

    if (pFIFO[SVGA_FIFO_NEXT_CMD] != pFIFO[SVGA_FIFO_STOP])
        return true;

    if (   uLastCursorCount != pFIFO[SVGA_FIFO_CURSOR_COUNT]
        && VMSVGA_IS_VALID_FIFO_REG(SVGA_FIFO_CURSOR_LAST_UPDATED, pFIFO[SVGA_FIFO_MIN]))
        return true;

    return false;
}


/**
 * Called by the VGA refresh timer to wake up the FIFO thread when needed.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The shared VGA/VMSVGA instance data.
 * @param   pThisCC     The VGA/VMSVGA state for ring-3.
 */
void vmsvgaR3FifoWatchdogTimer(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC)
{
    /* Caller already checked pThis->svga.fFIFOThreadSleeping, so we only have
       to recheck it before doing the signalling. */
    if (   (pThis->fVmSvga3 || vmsvgaR3FifoHasWork(pThisCC, ASMAtomicReadU32(&pThis->svga.uLastCursorUpdateCount)))
        && pThis->svga.fFIFOThreadSleeping
        && !ASMAtomicReadBool(&pThis->svga.fBadGuest))
    {
        int rc = PDMDevHlpSUPSemEventSignal(pDevIns, pThis->svga.hFIFORequestSem);
        AssertRC(rc);
        STAM_REL_COUNTER_INC(&pThisCC->svga.pSvgaR3State->StatFifoWatchdogWakeUps);
    }
}


/**
 * Called by the FIFO thread to process pending actions.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The shared VGA/VMSVGA instance data.
 * @param   pThisCC     The VGA/VMSVGA state for ring-3.
 */
static void vmsvgaR3FifoPendingActions(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC)
{
    RT_NOREF(pDevIns);

    /* Currently just mode changes. */
    if (ASMBitTestAndClear(&pThis->svga.u32ActionFlags, VMSVGA_ACTION_CHANGEMODE_BIT))
    {
        vmsvgaR3ChangeMode(pThis, pThisCC);
# ifdef VBOX_WITH_VMSVGA3D
        if (pThis->svga.f3DEnabled && pThisCC->svga.p3dState != NULL)
        {
            /** @todo Implement !f3DEnabled and fVMSVGA2dGBO cases to prevent an occasional blank screens on VM startup */
            vmsvga3dChangeMode(pThisCC);
        }
# endif
    }
}


/*
 * These two macros are put outside vmsvgaR3FifoLoop because doxygen gets confused,
 * even the latest version, and thinks we're documenting vmsvgaR3FifoLoop. Sigh.
 */
/** @def VMSVGAFIFO_GET_CMD_BUFFER_BREAK
 * Macro for shortening calls to vmsvgaR3FifoGetCmdPayload.
 *
 * Will break out of the switch on failure.
 * Will restart and quit the loop if the thread was requested to stop.
 *
 * @param   a_PtrVar        Request variable pointer.
 * @param   a_Type          Request typedef (not pointer) for casting.
 * @param   a_cbPayloadReq  How much payload to fetch.
 * @remarks Accesses a bunch of variables in the current scope!
 */
# define VMSVGAFIFO_GET_CMD_BUFFER_BREAK(a_PtrVar, a_Type, a_cbPayloadReq) \
            if (1) { \
                (a_PtrVar) = (a_Type *)vmsvgaR3FifoGetCmdPayload((a_cbPayloadReq), pFIFO, offCurrentCmd, offFifoMin, offFifoMax, \
                                                                 pbBounceBuf, &cbPayload, pThread, pThis, pSVGAState, pDevIns); \
                if (RT_UNLIKELY((uintptr_t)(a_PtrVar) < 2)) { if ((uintptr_t)(a_PtrVar) == 1) continue; break; } \
                RT_UNTRUSTED_NONVOLATILE_COPY_FENCE(); \
            } else do {} while (0)
/* @def VMSVGAFIFO_GET_MORE_CMD_BUFFER_BREAK
 * Macro for shortening calls to vmsvgaR3FifoGetCmdPayload for refetching the
 * buffer after figuring out the actual command size.
 *
 * Will break out of the switch on failure.
 *
 * @param   a_PtrVar        Request variable pointer.
 * @param   a_Type          Request typedef (not pointer) for casting.
 * @param   a_cbPayloadReq  How much payload to fetch.
 * @remarks Accesses a bunch of variables in the current scope!
 */
# define VMSVGAFIFO_GET_MORE_CMD_BUFFER_BREAK(a_PtrVar, a_Type, a_cbPayloadReq) \
            if (1) { \
                VMSVGAFIFO_GET_CMD_BUFFER_BREAK(a_PtrVar, a_Type, a_cbPayloadReq); \
            } else do {} while (0)

/**
 * @callback_method_impl{PFNPDMTHREADDEV, The async FIFO handling thread.}
 */
static DECLCALLBACK(int) vmsvgaR3FifoLoop(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    PVGASTATE       pThis      = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATER3     pThisCC    = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    PVMSVGAR3STATE  pSVGAState = pThisCC->svga.pSvgaR3State;
    int             rc;

    if (pThread->enmState == PDMTHREADSTATE_INITIALIZING)
        return VINF_SUCCESS;

    /*
     * Special mode where we only execute an external command and the go back
     * to being suspended.  Currently, all ext cmds ends up here, with the reset
     * one also being eligble for runtime execution further down as well.
     */
    if (pThis->svga.fFifoExtCommandWakeup)
    {
        vmsvgaR3FifoHandleExtCmd(pDevIns, pThis, pThisCC);
        while (pThread->enmState == PDMTHREADSTATE_RUNNING)
            if (pThis->svga.u8FIFOExtCommand == VMSVGA_FIFO_EXTCMD_NONE)
                PDMDevHlpSUPSemEventWaitNoResume(pDevIns, pThis->svga.hFIFORequestSem, RT_MS_1MIN);
            else
                vmsvgaR3FifoHandleExtCmd(pDevIns, pThis, pThisCC);
        return VINF_SUCCESS;
    }


    /*
     * Signal the semaphore to make sure we don't wait for 250ms after a
     * suspend & resume scenario (see vmsvgaR3FifoGetCmdPayload).
     */
    PDMDevHlpSUPSemEventSignal(pDevIns, pThis->svga.hFIFORequestSem);

    /*
     * Allocate a bounce buffer for command we get from the FIFO.
     * (All code must return via the end of the function to free this buffer.)
     */
    uint8_t *pbBounceBuf = (uint8_t *)RTMemAllocZ(pThis->svga.cbFIFO);
    AssertReturn(pbBounceBuf, VERR_NO_MEMORY);

    /*
     * Polling/sleep interval config.
     *
     * We wait for an a short interval if the guest has recently given us work
     * to do, but the interval increases the longer we're kept idle.  Once we've
     * reached the refresh timer interval, we'll switch to extended waits,
     * depending on it or the guest to kick us into action when needed.
     *
     * Should the refresh time go fishing, we'll just continue increasing the
     * sleep length till we reaches the 250 ms max after about 16 seconds.
     */
    RTMSINTERVAL const  cMsMinSleep           = 16;
    RTMSINTERVAL const  cMsIncSleep           = 2;
    RTMSINTERVAL const  cMsMaxSleep           = 250;
    RTMSINTERVAL const  cMsExtendedSleep      = 15 * RT_MS_1SEC; /* Regular paranoia dictates that this cannot be indefinite. */
    RTMSINTERVAL        cMsSleep              = cMsMaxSleep;

    /*
     * Cursor update state (SVGA_FIFO_CAP_CURSOR_BYPASS_3).
     *
     * Initialize with values that will detect an update from the guest.
     * Make sure that if the guest never updates the cursor position, then the device does not report it.
     * The guest has to change the value of uLastCursorUpdateCount, when the cursor position is actually updated.
     * xLastCursor, yLastCursor and fLastCursorVisible are set to report the first update.
     */
    uint32_t RT_UNTRUSTED_VOLATILE_GUEST * const pFIFO = pThisCC->svga.pau32FIFO;
    pThis->svga.uLastCursorUpdateCount = pFIFO[SVGA_FIFO_CURSOR_COUNT];
    uint32_t xLastCursor        = ~pFIFO[SVGA_FIFO_CURSOR_X];
    uint32_t yLastCursor        = ~pFIFO[SVGA_FIFO_CURSOR_Y];
    uint32_t fLastCursorVisible = ~pFIFO[SVGA_FIFO_CURSOR_ON];

    /*
     * The FIFO loop.
     */
    LogFlow(("vmsvgaR3FifoLoop: started loop\n"));
    bool fBadOrDisabledFifo = ASMAtomicReadBool(&pThis->svga.fBadGuest);
    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
# if defined(RT_OS_DARWIN) && defined(VBOX_WITH_VMSVGA3D)
        /*
         * Should service the run loop every so often.
         */
        if (pThis->svga.f3DEnabled)
            vmsvga3dCocoaServiceRunLoop();
# endif

        /* First check any pending actions. */
        vmsvgaR3FifoPendingActions(pDevIns, pThis, pThisCC);

        /*
         * Unless there's already work pending, go to sleep for a short while.
         * (See polling/sleep interval config above.)
         */
        if (   fBadOrDisabledFifo
            || !vmsvgaR3FifoHasWork(pThisCC, pThis->svga.uLastCursorUpdateCount))
        {
            ASMAtomicWriteBool(&pThis->svga.fFIFOThreadSleeping, true);
            Assert(pThis->cMilliesRefreshInterval > 0);
            if (cMsSleep < pThis->cMilliesRefreshInterval)
                rc = PDMDevHlpSUPSemEventWaitNoResume(pDevIns, pThis->svga.hFIFORequestSem, cMsSleep);
            else
            {
# ifdef VMSVGA_USE_FIFO_ACCESS_HANDLER
                int rc2 = PDMDevHlpPGMHandlerPhysicalReset(pDevIns, pThis->svga.GCPhysFIFO);
                AssertRC(rc2); /* No break. Racing EMTs unmapping and remapping the region. */
# endif
                if (   !fBadOrDisabledFifo
                    && vmsvgaR3FifoHasWork(pThisCC, pThis->svga.uLastCursorUpdateCount))
                    rc = VINF_SUCCESS;
                else
                {
                    STAM_REL_PROFILE_START(&pSVGAState->StatFifoExtendedSleep, Acc);
                    rc = PDMDevHlpSUPSemEventWaitNoResume(pDevIns, pThis->svga.hFIFORequestSem, cMsExtendedSleep);
                    STAM_REL_PROFILE_STOP(&pSVGAState->StatFifoExtendedSleep, Acc);
                }
            }
            ASMAtomicWriteBool(&pThis->svga.fFIFOThreadSleeping, false);
            AssertBreak(RT_SUCCESS(rc) || rc == VERR_TIMEOUT || rc == VERR_INTERRUPTED);
            if (pThread->enmState != PDMTHREADSTATE_RUNNING)
            {
                LogFlow(("vmsvgaR3FifoLoop: thread state %x\n", pThread->enmState));
                break;
            }
        }
        else
            rc = VINF_SUCCESS;
        fBadOrDisabledFifo = ASMAtomicReadBool(&pThis->svga.fBadGuest);
        if (rc == VERR_TIMEOUT)
        {
            if (!vmsvgaR3FifoHasWork(pThisCC, pThis->svga.uLastCursorUpdateCount))
            {
                cMsSleep = RT_MIN(cMsSleep + cMsIncSleep, cMsMaxSleep);
                continue;
            }
            STAM_REL_COUNTER_INC(&pSVGAState->StatFifoTodoTimeout);

            Log(("vmsvgaR3FifoLoop: timeout\n"));
        }
        else if (vmsvgaR3FifoHasWork(pThisCC, pThis->svga.uLastCursorUpdateCount))
            STAM_REL_COUNTER_INC(&pSVGAState->StatFifoTodoWoken);
        cMsSleep = cMsMinSleep;

        Log(("vmsvgaR3FifoLoop: enabled=%d configured=%d busy=%d\n", pThis->svga.fEnabled, pThis->svga.fConfigured, pFIFO[SVGA_FIFO_BUSY]));
        Log(("vmsvgaR3FifoLoop: min  %x max  %x\n", pFIFO[SVGA_FIFO_MIN], pFIFO[SVGA_FIFO_MAX]));
        Log(("vmsvgaR3FifoLoop: next %x stop %x\n", pFIFO[SVGA_FIFO_NEXT_CMD], pFIFO[SVGA_FIFO_STOP]));

        /*
         * Handle external commands (currently only reset).
         */
        if (pThis->svga.u8FIFOExtCommand != VMSVGA_FIFO_EXTCMD_NONE)
        {
            vmsvgaR3FifoHandleExtCmd(pDevIns, pThis, pThisCC);
            continue;
        }

        /*
         * If guest misbehaves, then do nothing.
         */
        if (ASMAtomicReadBool(&pThis->svga.fBadGuest))
        {
            vmsvgaR3FifoSetNotBusy(pDevIns, pThis, pThisCC, pSVGAState, pFIFO[SVGA_FIFO_MIN]);
            cMsSleep = cMsExtendedSleep;
            LogRelMax(1, ("VMSVGA: FIFO processing stopped because of the guest misbehavior\n"));
            continue;
        }

        /*
         * The device must be enabled and configured.
         */
        if (   !pThis->svga.fEnabled
            || !pThis->svga.fConfigured)
        {
            vmsvgaR3FifoSetNotBusy(pDevIns, pThis, pThisCC, pSVGAState, pFIFO[SVGA_FIFO_MIN]);
            fBadOrDisabledFifo = true;
            cMsSleep           = cMsMaxSleep; /* cheat */
            continue;
        }

        /*
         * Get and check the min/max values.  We ASSUME that they will remain
         * unchanged while we process requests.  A further ASSUMPTION is that
         * the guest won't mess with SVGA_FIFO_NEXT_CMD while we're busy, so
         * we don't read it back while in the loop.
         */
        uint32_t const offFifoMin    = pFIFO[SVGA_FIFO_MIN];
        uint32_t const offFifoMax    = pFIFO[SVGA_FIFO_MAX];
        uint32_t       offCurrentCmd = pFIFO[SVGA_FIFO_STOP];
        RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();
        if (RT_UNLIKELY(   !VMSVGA_IS_VALID_FIFO_REG(SVGA_FIFO_STOP, offFifoMin)
                        || offFifoMax <= offFifoMin
                        || offFifoMax > pThis->svga.cbFIFO
                        || (offFifoMax & 3) != 0
                        || (offFifoMin & 3) != 0
                        || offCurrentCmd < offFifoMin
                        || offCurrentCmd > offFifoMax))
        {
            STAM_REL_COUNTER_INC(&pSVGAState->StatFifoErrors);
            LogRelMax(8, ("vmsvgaR3FifoLoop: Bad fifo: min=%#x stop=%#x max=%#x\n", offFifoMin, offCurrentCmd, offFifoMax));
            vmsvgaR3FifoSetNotBusy(pDevIns, pThis, pThisCC, pSVGAState, offFifoMin);
            fBadOrDisabledFifo = true;
            continue;
        }
        RT_UNTRUSTED_VALIDATED_FENCE();
        if (RT_UNLIKELY(offCurrentCmd & 3))
        {
            STAM_REL_COUNTER_INC(&pSVGAState->StatFifoErrors);
            LogRelMax(8, ("vmsvgaR3FifoLoop: Misaligned offCurrentCmd=%#x?\n", offCurrentCmd));
            offCurrentCmd &= ~UINT32_C(3);
        }

        /*
         * Update the cursor position before we start on the FIFO commands.
         */
        /** @todo do we need to check whether the guest disabled the SVGA_FIFO_CAP_CURSOR_BYPASS_3 capability here? */
        if (VMSVGA_IS_VALID_FIFO_REG(SVGA_FIFO_CURSOR_LAST_UPDATED, offFifoMin))
        {
            uint32_t const uCursorUpdateCount = pFIFO[SVGA_FIFO_CURSOR_COUNT];
            if (uCursorUpdateCount == pThis->svga.uLastCursorUpdateCount)
            { /* halfways likely */ }
            else
            {
                uint32_t const uNewCount = vmsvgaR3FifoUpdateCursor(pThisCC, pSVGAState, pFIFO, offFifoMin, uCursorUpdateCount,
                                                                    &xLastCursor, &yLastCursor, &fLastCursorVisible);
                ASMAtomicWriteU32(&pThis->svga.uLastCursorUpdateCount, uNewCount);
            }
        }

        /*
         * Mark the FIFO as busy.
         */
        ASMAtomicWriteU32(&pThis->svga.fBusy, VMSVGA_BUSY_F_FIFO);  // Clears VMSVGA_BUSY_F_EMT_FORCE!
        if (VMSVGA_IS_VALID_FIFO_REG(SVGA_FIFO_BUSY, offFifoMin))
            ASMAtomicWriteU32(&pFIFO[SVGA_FIFO_BUSY], true);

        /*
         * Process all submitted command buffers.
         */
        vmsvgaR3CmdBufProcessBuffers(pDevIns, pThis, pThisCC, pThread);

        /*
         * Execute all queued FIFO commands.
         * Quit if pending external command or changes in the thread state.
         */
        bool fDone = false;
        while (   !(fDone = (pFIFO[SVGA_FIFO_NEXT_CMD] == offCurrentCmd))
               && pThread->enmState == PDMTHREADSTATE_RUNNING)
        {
            uint32_t cbPayload = 0;
            uint32_t u32IrqStatus = 0;

            Assert(offCurrentCmd < offFifoMax && offCurrentCmd >= offFifoMin);

            /* First check any pending actions. */
            vmsvgaR3FifoPendingActions(pDevIns, pThis, pThisCC);

            /* Check for pending external commands (reset). */
            if (pThis->svga.u8FIFOExtCommand != VMSVGA_FIFO_EXTCMD_NONE)
                break;

            /*
             * Process the command.
             */
            /* 'enmCmdId' is actually a SVGAFifoCmdId. It is treated as uint32_t in order to avoid a compiler
             * warning. Because we implement some obsolete and deprecated commands, which are not included in
             * the SVGAFifoCmdId enum in the VMSVGA headers anymore.
             */
            uint32_t const enmCmdId = pFIFO[offCurrentCmd / sizeof(uint32_t)];
            RT_UNTRUSTED_NONVOLATILE_COPY_FENCE();
            LogFlow(("vmsvgaR3FifoLoop: FIFO command (iCmd=0x%x) %s %d\n",
                     offCurrentCmd / sizeof(uint32_t), vmsvgaR3FifoCmdToString(enmCmdId), enmCmdId));
            switch (enmCmdId)
            {
            case SVGA_CMD_INVALID_CMD:
                /* Nothing to do. */
                STAM_REL_COUNTER_INC(&pSVGAState->StatR3CmdInvalidCmd);
                break;

            case SVGA_CMD_FENCE:
            {
                SVGAFifoCmdFence *pCmdFence;
                VMSVGAFIFO_GET_CMD_BUFFER_BREAK(pCmdFence, SVGAFifoCmdFence, sizeof(*pCmdFence));
                STAM_REL_COUNTER_INC(&pSVGAState->StatR3CmdFence);
                if (VMSVGA_IS_VALID_FIFO_REG(SVGA_FIFO_FENCE, offFifoMin))
                {
                    Log(("vmsvgaR3FifoLoop: SVGA_CMD_FENCE %#x\n", pCmdFence->fence));
                    pFIFO[SVGA_FIFO_FENCE] = pCmdFence->fence;

                    if (pThis->svga.u32IrqMask & SVGA_IRQFLAG_ANY_FENCE)
                    {
                        Log(("vmsvgaR3FifoLoop: any fence irq\n"));
                        u32IrqStatus |= SVGA_IRQFLAG_ANY_FENCE;
                    }
                    else
                    if (    VMSVGA_IS_VALID_FIFO_REG(SVGA_FIFO_FENCE_GOAL, offFifoMin)
                        &&  (pThis->svga.u32IrqMask & SVGA_IRQFLAG_FENCE_GOAL)
                        &&  pFIFO[SVGA_FIFO_FENCE_GOAL] == pCmdFence->fence)
                    {
                        Log(("vmsvgaR3FifoLoop: fence goal reached irq (fence=%#x)\n", pCmdFence->fence));
                        u32IrqStatus |= SVGA_IRQFLAG_FENCE_GOAL;
                    }
                }
                else
                    Log(("SVGA_CMD_FENCE is bogus when offFifoMin is %#x!\n", offFifoMin));
                break;
            }

            case SVGA_CMD_UPDATE:
            {
                SVGAFifoCmdUpdate *pCmd;
                VMSVGAFIFO_GET_CMD_BUFFER_BREAK(pCmd, SVGAFifoCmdUpdate, sizeof(*pCmd));
                vmsvgaR3CmdUpdate(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_UPDATE_VERBOSE:
            {
                SVGAFifoCmdUpdateVerbose *pCmd;
                VMSVGAFIFO_GET_CMD_BUFFER_BREAK(pCmd, SVGAFifoCmdUpdateVerbose, sizeof(*pCmd));
                vmsvgaR3CmdUpdateVerbose(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_DEFINE_CURSOR:
            {
                /* Followed by bitmap data. */
                SVGAFifoCmdDefineCursor *pCmd;
                VMSVGAFIFO_GET_CMD_BUFFER_BREAK(pCmd, SVGAFifoCmdDefineCursor, sizeof(*pCmd));

                /* Figure out the size of the bitmap data. */
                ASSERT_GUEST_BREAK(pCmd->height < 2048 && pCmd->width < 2048);
                ASSERT_GUEST_BREAK(pCmd->andMaskDepth <= 32);
                ASSERT_GUEST_BREAK(pCmd->xorMaskDepth <= 32);
                RT_UNTRUSTED_VALIDATED_FENCE();

                uint32_t const cbAndLine = RT_ALIGN_32(pCmd->width * (pCmd->andMaskDepth + (pCmd->andMaskDepth == 15)), 32) / 8;
                uint32_t const cbAndMask = cbAndLine * pCmd->height;
                uint32_t const cbXorLine = RT_ALIGN_32(pCmd->width * (pCmd->xorMaskDepth + (pCmd->xorMaskDepth == 15)), 32) / 8;
                uint32_t const cbXorMask = cbXorLine * pCmd->height;

                uint32_t const cbCmd = sizeof(SVGAFifoCmdDefineCursor) + cbAndMask + cbXorMask;
                VMSVGAFIFO_GET_MORE_CMD_BUFFER_BREAK(pCmd, SVGAFifoCmdDefineCursor, cbCmd);
                vmsvgaR3CmdDefineCursor(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_DEFINE_ALPHA_CURSOR:
            {
                /* Followed by bitmap data. */
                SVGAFifoCmdDefineAlphaCursor *pCmd;
                VMSVGAFIFO_GET_CMD_BUFFER_BREAK(pCmd, SVGAFifoCmdDefineAlphaCursor, sizeof(*pCmd));

                /* Figure out the size of the bitmap data. */
                ASSERT_GUEST_BREAK(pCmd->height < 2048 && pCmd->width < 2048);

                uint32_t const cbCmd = sizeof(SVGAFifoCmdDefineAlphaCursor) + pCmd->width * pCmd->height * sizeof(uint32_t) /* 32-bit BRGA format */;
                VMSVGAFIFO_GET_MORE_CMD_BUFFER_BREAK(pCmd, SVGAFifoCmdDefineAlphaCursor, cbCmd);
                vmsvgaR3CmdDefineAlphaCursor(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_MOVE_CURSOR:
            {
                /* Deprecated; there should be no driver which *requires* this command. However, if
                 * we do ecncounter this command, it might be useful to not get the FIFO completely out of
                 * alignment.
                 * May be issued by guest if SVGA_CAP_CURSOR_BYPASS is missing.
                 */
                SVGAFifoCmdMoveCursor *pCmd;
                VMSVGAFIFO_GET_CMD_BUFFER_BREAK(pCmd, SVGAFifoCmdMoveCursor, sizeof(*pCmd));
                vmsvgaR3CmdMoveCursor(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_DISPLAY_CURSOR:
            {
                /* Deprecated; there should be no driver which *requires* this command. However, if
                 * we do ecncounter this command, it might be useful to not get the FIFO completely out of
                 * alignment.
                 * May be issued by guest if SVGA_CAP_CURSOR_BYPASS is missing.
                 */
                SVGAFifoCmdDisplayCursor *pCmd;
                VMSVGAFIFO_GET_CMD_BUFFER_BREAK(pCmd, SVGAFifoCmdDisplayCursor, sizeof(*pCmd));
                vmsvgaR3CmdDisplayCursor(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_RECT_FILL:
            {
                SVGAFifoCmdRectFill *pCmd;
                VMSVGAFIFO_GET_CMD_BUFFER_BREAK(pCmd, SVGAFifoCmdRectFill, sizeof(*pCmd));
                vmsvgaR3CmdRectFill(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_RECT_COPY:
            {
                SVGAFifoCmdRectCopy *pCmd;
                VMSVGAFIFO_GET_CMD_BUFFER_BREAK(pCmd, SVGAFifoCmdRectCopy, sizeof(*pCmd));
                vmsvgaR3CmdRectCopy(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_RECT_ROP_COPY:
            {
                SVGAFifoCmdRectRopCopy *pCmd;
                VMSVGAFIFO_GET_CMD_BUFFER_BREAK(pCmd, SVGAFifoCmdRectRopCopy, sizeof(*pCmd));
                vmsvgaR3CmdRectRopCopy(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_ESCAPE:
            {
                /* Followed by 'size' bytes of data. */
                SVGAFifoCmdEscape *pCmd;
                VMSVGAFIFO_GET_CMD_BUFFER_BREAK(pCmd, SVGAFifoCmdEscape, sizeof(*pCmd));

                ASSERT_GUEST_BREAK(pCmd->size < pThis->svga.cbFIFO - sizeof(SVGAFifoCmdEscape));
                RT_UNTRUSTED_VALIDATED_FENCE();

                uint32_t const cbCmd = sizeof(SVGAFifoCmdEscape) + pCmd->size;
                VMSVGAFIFO_GET_MORE_CMD_BUFFER_BREAK(pCmd, SVGAFifoCmdEscape, cbCmd);
                vmsvgaR3CmdEscape(pThis, pThisCC, pCmd);
                break;
            }
# ifdef VBOX_WITH_VMSVGA3D
            case SVGA_CMD_DEFINE_GMR2:
            {
                SVGAFifoCmdDefineGMR2 *pCmd;
                VMSVGAFIFO_GET_CMD_BUFFER_BREAK(pCmd, SVGAFifoCmdDefineGMR2, sizeof(*pCmd));
                vmsvgaR3CmdDefineGMR2(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_REMAP_GMR2:
            {
                /* Followed by page descriptors or guest ptr. */
                SVGAFifoCmdRemapGMR2 *pCmd;
                VMSVGAFIFO_GET_CMD_BUFFER_BREAK(pCmd, SVGAFifoCmdRemapGMR2, sizeof(*pCmd));

                /* Calculate the size of what comes after next and fetch it. */
                uint32_t cbCmd = sizeof(SVGAFifoCmdRemapGMR2);
                if (pCmd->flags & SVGA_REMAP_GMR2_VIA_GMR)
                    cbCmd += sizeof(SVGAGuestPtr);
                else
                {
                    uint32_t const cbPageDesc = (pCmd->flags & SVGA_REMAP_GMR2_PPN64) ? sizeof(uint64_t) : sizeof(uint32_t);
                    if (pCmd->flags & SVGA_REMAP_GMR2_SINGLE_PPN)
                    {
                        cbCmd         += cbPageDesc;
                        pCmd->numPages = 1;
                    }
                    else
                    {
                        ASSERT_GUEST_BREAK(pCmd->numPages <= pThis->svga.cbFIFO / cbPageDesc);
                        cbCmd += cbPageDesc * pCmd->numPages;
                    }
                }
                VMSVGAFIFO_GET_MORE_CMD_BUFFER_BREAK(pCmd, SVGAFifoCmdRemapGMR2, cbCmd);
                vmsvgaR3CmdRemapGMR2(pThis, pThisCC, pCmd);
#  ifdef DEBUG_GMR_ACCESS
                VMR3ReqCallWaitU(PDMDevHlpGetUVM(pDevIns), VMCPUID_ANY, (PFNRT)vmsvgaR3RegisterGmr, 2, pDevIns, pCmd->gmrId);
#  endif
                break;
            }
# endif // VBOX_WITH_VMSVGA3D
            case SVGA_CMD_DEFINE_SCREEN:
            {
                /* The size of this command is specified by the guest and depends on capabilities. */
                Assert(pFIFO[SVGA_FIFO_CAPABILITIES] & SVGA_FIFO_CAP_SCREEN_OBJECT_2);

                SVGAFifoCmdDefineScreen *pCmd;
                VMSVGAFIFO_GET_CMD_BUFFER_BREAK(pCmd, SVGAFifoCmdDefineScreen, sizeof(pCmd->screen.structSize));
                AssertBreak(pCmd->screen.structSize < pThis->svga.cbFIFO);
                RT_UNTRUSTED_VALIDATED_FENCE();

                RT_BZERO(&pCmd->screen.id, sizeof(*pCmd) - RT_OFFSETOF(SVGAFifoCmdDefineScreen, screen.id));
                VMSVGAFIFO_GET_MORE_CMD_BUFFER_BREAK(pCmd, SVGAFifoCmdDefineScreen, RT_MAX(sizeof(pCmd->screen.structSize), pCmd->screen.structSize));
                vmsvgaR3CmdDefineScreen(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_DESTROY_SCREEN:
            {
                SVGAFifoCmdDestroyScreen *pCmd;
                VMSVGAFIFO_GET_CMD_BUFFER_BREAK(pCmd, SVGAFifoCmdDestroyScreen, sizeof(*pCmd));
                vmsvgaR3CmdDestroyScreen(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_DEFINE_GMRFB:
            {
                SVGAFifoCmdDefineGMRFB *pCmd;
                VMSVGAFIFO_GET_CMD_BUFFER_BREAK(pCmd, SVGAFifoCmdDefineGMRFB, sizeof(*pCmd));
                vmsvgaR3CmdDefineGMRFB(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_BLIT_GMRFB_TO_SCREEN:
            {
                SVGAFifoCmdBlitGMRFBToScreen *pCmd;
                VMSVGAFIFO_GET_CMD_BUFFER_BREAK(pCmd, SVGAFifoCmdBlitGMRFBToScreen, sizeof(*pCmd));
                vmsvgaR3CmdBlitGMRFBToScreen(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_BLIT_SCREEN_TO_GMRFB:
            {
                SVGAFifoCmdBlitScreenToGMRFB *pCmd;
                VMSVGAFIFO_GET_CMD_BUFFER_BREAK(pCmd, SVGAFifoCmdBlitScreenToGMRFB, sizeof(*pCmd));
                vmsvgaR3CmdBlitScreenToGMRFB(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_ANNOTATION_FILL:
            {
                SVGAFifoCmdAnnotationFill *pCmd;
                VMSVGAFIFO_GET_CMD_BUFFER_BREAK(pCmd, SVGAFifoCmdAnnotationFill, sizeof(*pCmd));
                vmsvgaR3CmdAnnotationFill(pThis, pThisCC, pCmd);
                break;
            }

            case SVGA_CMD_ANNOTATION_COPY:
            {
                SVGAFifoCmdAnnotationCopy *pCmd;
                VMSVGAFIFO_GET_CMD_BUFFER_BREAK(pCmd, SVGAFifoCmdAnnotationCopy, sizeof(*pCmd));
                vmsvgaR3CmdAnnotationCopy(pThis, pThisCC, pCmd);
                break;
            }

            default:
# ifdef VBOX_WITH_VMSVGA3D
                if (   ((int)enmCmdId >= SVGA_3D_CMD_BASE && (int)enmCmdId <  SVGA_3D_CMD_MAX)
                    || (   pThis->svga.fVBoxExtensions
                        && ((int)enmCmdId >= VBSVGA_3D_CMD_BASE && (int)enmCmdId < VBSVGA_3D_CMD_MAX)))
                {
                    RT_UNTRUSTED_VALIDATED_FENCE();

                    /* All 3d commands start with a common header, which defines the identifier and the size
                     * of the command. The identifier has been already read from FIFO. Fetch the size.
                     */
                    uint32_t *pcbCmd;
                    VMSVGAFIFO_GET_CMD_BUFFER_BREAK(pcbCmd, uint32_t, sizeof(*pcbCmd));
                    uint32_t const cbCmd = *pcbCmd;
                    AssertBreak(cbCmd < pThis->svga.cbFIFO);
                    uint32_t *pu32Cmd;
                    VMSVGAFIFO_GET_MORE_CMD_BUFFER_BREAK(pu32Cmd, uint32_t, sizeof(*pcbCmd) + cbCmd);
                    pu32Cmd++; /* Skip the command size. */

                    if (RT_LIKELY(pThis->svga.f3DEnabled))
                    { /* likely */ }
                    else
                    {
                        LogRelMax(8, ("VMSVGA: 3D disabled, command %d skipped\n", enmCmdId));
                        break;
                    }

                    vmsvgaR3Process3dCmd(pThis, pThisCC, SVGA3D_INVALID_ID, (SVGAFifo3dCmdId)enmCmdId, cbCmd, pu32Cmd);
                }
                else
# endif // VBOX_WITH_VMSVGA3D
                {
                    STAM_REL_COUNTER_INC(&pSVGAState->StatFifoUnkCmds);
                    AssertMsgFailed(("enmCmdId=%d\n", enmCmdId));
                    LogRelMax(16, ("VMSVGA: unsupported command %d\n", enmCmdId));
                }
            }

            /* Go to the next slot */
            Assert(cbPayload + sizeof(uint32_t) <= offFifoMax - offFifoMin);
            offCurrentCmd += RT_ALIGN_32(cbPayload + sizeof(uint32_t), sizeof(uint32_t));
            if (offCurrentCmd >= offFifoMax)
            {
                offCurrentCmd -= offFifoMax - offFifoMin;
                Assert(offCurrentCmd >= offFifoMin);
                Assert(offCurrentCmd <  offFifoMax);
            }
            ASMAtomicWriteU32(&pFIFO[SVGA_FIFO_STOP], offCurrentCmd);
            STAM_REL_COUNTER_INC(&pSVGAState->StatFifoCommands);

            /*
             * Raise IRQ if required.  Must enter the critical section here
             * before making final decisions here, otherwise cubebench and
             * others may end up waiting forever.
             */
            if (   u32IrqStatus
                || (pThis->svga.u32IrqMask & SVGA_IRQFLAG_FIFO_PROGRESS))
            {
                int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
                PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSect, rcLock);

                /* FIFO progress might trigger an interrupt. */
                if (pThis->svga.u32IrqMask & SVGA_IRQFLAG_FIFO_PROGRESS)
                {
                    Log(("vmsvgaR3FifoLoop: fifo progress irq\n"));
                    u32IrqStatus |= SVGA_IRQFLAG_FIFO_PROGRESS;
                }

                /* Unmasked IRQ pending? */
                if (pThis->svga.u32IrqMask & u32IrqStatus)
                {
                    Log(("vmsvgaR3FifoLoop: Trigger interrupt with status %x\n", u32IrqStatus));
                    ASMAtomicOrU32(&pThis->svga.u32IrqStatus, u32IrqStatus);
                    PDMDevHlpPCISetIrq(pDevIns, 0, 1);
                }

                PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
            }
        }

        /* If really done, clear the busy flag. */
        if (fDone)
        {
            Log(("vmsvgaR3FifoLoop: emptied the FIFO next=%x stop=%x\n", pFIFO[SVGA_FIFO_NEXT_CMD], offCurrentCmd));
            vmsvgaR3FifoSetNotBusy(pDevIns, pThis, pThisCC, pSVGAState, offFifoMin);
        }
    }

    /*
     * Free the bounce buffer. (There are no returns above!)
     */
    RTMemFree(pbBounceBuf);

    return VINF_SUCCESS;
}

#undef VMSVGAFIFO_GET_MORE_CMD_BUFFER_BREAK
#undef VMSVGAFIFO_GET_CMD_BUFFER_BREAK

/**
 * @callback_method_impl{PFNPDMTHREADDEV, The async FIFO handling thread.}
 */
static DECLCALLBACK(int) vmsvgaR3CmdBufLoop(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    PVGASTATE       pThis      = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATER3     pThisCC    = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    PVMSVGAR3STATE  pSVGAState = pThisCC->svga.pSvgaR3State;
    int             rc;

    if (pThread->enmState == PDMTHREADSTATE_INITIALIZING)
        return VINF_SUCCESS;

    /*
     * Special mode where we only execute an external command and the go back
     * to being suspended.  Currently, all ext cmds ends up here, with the reset
     * one also being eligble for runtime execution further down as well.
     */
    if (pThis->svga.fFifoExtCommandWakeup)
    {
        vmsvgaR3FifoHandleExtCmd(pDevIns, pThis, pThisCC);
        while (pThread->enmState == PDMTHREADSTATE_RUNNING)
            if (pThis->svga.u8FIFOExtCommand == VMSVGA_FIFO_EXTCMD_NONE)
                PDMDevHlpSUPSemEventWaitNoResume(pDevIns, pThis->svga.hFIFORequestSem, RT_MS_1MIN);
            else
                vmsvgaR3FifoHandleExtCmd(pDevIns, pThis, pThisCC);
        return VINF_SUCCESS;
    }


    /*
     * Signal the semaphore to make sure we don't wait for 250ms after a
     * suspend & resume scenario (see vmsvgaR3FifoGetCmdPayload).
     */
    PDMDevHlpSUPSemEventSignal(pDevIns, pThis->svga.hFIFORequestSem);

    /*
     * Polling/sleep interval config.
     *
     * We wait for an a short interval if the guest has recently given us work
     * to do, but the interval increases the longer we're kept idle.  Once we've
     * reached the refresh timer interval, we'll switch to extended waits,
     * depending on it or the guest to kick us into action when needed.
     *
     * Should the refresh time go fishing, we'll just continue increasing the
     * sleep length till we reaches the 250 ms max after about 16 seconds.
     */
    RTMSINTERVAL const  cMsMinSleep           = 16;
    RTMSINTERVAL const  cMsMaxSleep           = 250;
    RTMSINTERVAL const  cMsExtendedSleep      = 15 * RT_MS_1SEC; /* Regular paranoia dictates that this cannot be indefinite. */
    RTMSINTERVAL        cMsSleep              = cMsMaxSleep;

    /*
     * The FIFO loop.
     */
    LogFlow(("vmsvgaR3CmdBufLoop: started loop\n"));
    bool fBadOrDisabledFifo = ASMAtomicReadBool(&pThis->svga.fBadGuest);
    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        /* First check any pending actions. */
        vmsvgaR3FifoPendingActions(pDevIns, pThis, pThisCC);

        /*
         * Unless there's already work pending, go to sleep for a short while.
         * (See polling/sleep interval config above.)
         */
        ASMAtomicWriteBool(&pThis->svga.fFIFOThreadSleeping, true);
        Assert(pThis->cMilliesRefreshInterval > 0);
        if (cMsSleep < pThis->cMilliesRefreshInterval)
            rc = PDMDevHlpSUPSemEventWaitNoResume(pDevIns, pThis->svga.hFIFORequestSem, cMsSleep);
        else
        {
            STAM_REL_PROFILE_START(&pSVGAState->StatFifoExtendedSleep, Acc);
            rc = PDMDevHlpSUPSemEventWaitNoResume(pDevIns, pThis->svga.hFIFORequestSem, cMsExtendedSleep);
            STAM_REL_PROFILE_STOP(&pSVGAState->StatFifoExtendedSleep, Acc);
        }
        ASMAtomicWriteBool(&pThis->svga.fFIFOThreadSleeping, false);
        AssertBreak(RT_SUCCESS(rc) || rc == VERR_TIMEOUT || rc == VERR_INTERRUPTED);
        if (pThread->enmState != PDMTHREADSTATE_RUNNING)
        {
            LogFlow(("vmsvgaR3CmdBufLoop: thread state %x\n", pThread->enmState));
            break;
        }

        fBadOrDisabledFifo = ASMAtomicReadBool(&pThis->svga.fBadGuest);
        cMsSleep = cMsMinSleep;

        Log(("vmsvgaR3CmdBufLoop: enabled=%d configured=%d\n", pThis->svga.fEnabled, pThis->svga.fConfigured));

        /*
         * Handle external commands (currently only reset).
         */
        if (pThis->svga.u8FIFOExtCommand != VMSVGA_FIFO_EXTCMD_NONE)
        {
            vmsvgaR3FifoHandleExtCmd(pDevIns, pThis, pThisCC);
            continue;
        }

        /*
         * If guest misbehaves, then do nothing.
         */
        if (ASMAtomicReadBool(&pThis->svga.fBadGuest))
        {
            cMsSleep = cMsExtendedSleep;
            LogRelMax(1, ("VMSVGA: FIFO processing stopped because of the guest misbehavior\n"));
            continue;
        }

        /*
         * The device must be enabled and configured.
         */
        if (   !pThis->svga.fEnabled
            || !pThis->svga.fConfigured)
        {
            fBadOrDisabledFifo = true;
            cMsSleep           = cMsMaxSleep; /* cheat */
            continue;
        }

        /*
         * Process all submitted command buffers.
         */
        vmsvgaR3CmdBufProcessBuffers(pDevIns, pThis, pThisCC, pThread);
    }

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{PFNPDMTHREADWAKEUPDEV,
 * Unblock the FIFO I/O thread so it can respond to a state change.}
 */
static DECLCALLBACK(int) vmsvgaR3FifoLoopWakeUp(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    RT_NOREF(pDevIns);
    PVGASTATE pThis = (PVGASTATE)pThread->pvUser;
    Log(("vmsvgaR3FifoLoopWakeUp\n"));
    return PDMDevHlpSUPSemEventSignal(pDevIns, pThis->svga.hFIFORequestSem);
}

/**
 * Enables or disables dirty page tracking for the framebuffer
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The shared VGA/VMSVGA instance data.
 * @param   fTraces         Enable/disable traces
 */
static void vmsvgaR3SetTraces(PPDMDEVINS pDevIns, PVGASTATE pThis, bool fTraces)
{
    if (    (!pThis->svga.fConfigured || !pThis->svga.fEnabled)
        &&  !fTraces)
    {
        //Assert(pThis->svga.fTraces);
        Log(("vmsvgaR3SetTraces: *not* allowed to disable dirty page tracking when the device is in legacy mode.\n"));
        return;
    }

    pThis->svga.fTraces = fTraces;
    if (pThis->svga.fTraces)
    {
        unsigned cbFrameBuffer = pThis->vram_size;

        Log(("vmsvgaR3SetTraces: enable dirty page handling for the frame buffer only (%x bytes)\n", 0));
        /** @todo How does this work with screens? */
        if (pThis->svga.uHeight != VMSVGA_VAL_UNINITIALIZED)
        {
# if 0 //ifndef DEBUG_bird /* BB-10.3.1 triggers this as it initializes everything to zero. Better just ignore it. */
            Assert(pThis->svga.cbScanline);
# endif
            /* Hardware enabled; return real framebuffer size .*/
            cbFrameBuffer = (uint32_t)pThis->svga.uHeight * pThis->svga.cbScanline;
            cbFrameBuffer = RT_ALIGN(cbFrameBuffer, GUEST_PAGE_SIZE);
        }

        if (!pThis->svga.fVRAMTracking)
        {
            Log(("vmsvgaR3SetTraces: enable frame buffer dirty page tracking. (%x bytes; vram %x)\n", cbFrameBuffer, pThis->vram_size));
            vgaR3RegisterVRAMHandler(pDevIns, pThis, cbFrameBuffer);
            pThis->svga.fVRAMTracking = true;
        }
    }
    else
    {
        if (pThis->svga.fVRAMTracking)
        {
            Log(("vmsvgaR3SetTraces: disable frame buffer dirty page tracking\n"));
            vgaR3UnregisterVRAMHandler(pDevIns, pThis);
            pThis->svga.fVRAMTracking = false;
        }
    }
}

/**
 * @callback_method_impl{FNPCIIOREGIONMAP}
 */
DECLCALLBACK(int) vmsvgaR3PciIORegionFifoMapUnmap(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t iRegion,
                                                  RTGCPHYS GCPhysAddress, RTGCPHYS cb, PCIADDRESSSPACE enmType)
{
    PVGASTATE   pThis = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    int         rc;
    RT_NOREF(pPciDev);
    Assert(pPciDev == pDevIns->apPciDevs[0]);

    Log(("vmsvgaR3PciIORegionFifoMapUnmap: iRegion=%d GCPhysAddress=%RGp cb=%RGp enmType=%d\n", iRegion, GCPhysAddress, cb, enmType));
    AssertReturn(   iRegion == pThis->pciRegions.iFIFO
                 && (   enmType == PCI_ADDRESS_SPACE_MEM
                     || (enmType == PCI_ADDRESS_SPACE_MEM_PREFETCH /* got wrong in 6.1.0RC1 */ && pThis->fStateLoaded))
                 , VERR_INTERNAL_ERROR);
    if (GCPhysAddress != NIL_RTGCPHYS)
    {
        /*
         * Mapping the FIFO RAM.
         */
        AssertLogRelMsg(cb == pThis->svga.cbFIFO, ("cb=%#RGp cbFIFO=%#x\n", cb, pThis->svga.cbFIFO));
        rc = PDMDevHlpMmio2Map(pDevIns, pThis->hMmio2VmSvgaFifo, GCPhysAddress);
        AssertRC(rc);

# if defined(VMSVGA_USE_FIFO_ACCESS_HANDLER) || defined(DEBUG_FIFO_ACCESS)
        if (RT_SUCCESS(rc))
        {
            rc = PDMDevHlpPGMHandlerPhysicalRegister(pDevIns, GCPhysAddress,
#  ifdef DEBUG_FIFO_ACCESS
                                                     GCPhysAddress + (pThis->svga.cbFIFO - 1),
#  else
                                                     GCPhysAddress + GUEST_PAGE_SIZE - 1,
#  endif
                                                     pThis->svga.hFifoAccessHandlerType, pThis, NIL_RTR0PTR, NIL_RTRCPTR,
                                                     "VMSVGA FIFO");
            AssertRC(rc);
        }
# endif
        if (RT_SUCCESS(rc))
        {
            pThis->svga.GCPhysFIFO = GCPhysAddress;
            Log(("vmsvgaR3IORegionMap: GCPhysFIFO=%RGp cbFIFO=%#x\n", GCPhysAddress, pThis->svga.cbFIFO));
        }
        rc = VINF_PCI_MAPPING_DONE; /* caller only cares about this status, so it is okay that we overwrite errors here. */
    }
    else
    {
        Assert(pThis->svga.GCPhysFIFO);
# if defined(VMSVGA_USE_FIFO_ACCESS_HANDLER) || defined(DEBUG_FIFO_ACCESS)
        rc = PDMDevHlpPGMHandlerPhysicalDeregister(pDevIns, pThis->svga.GCPhysFIFO);
        AssertRC(rc);
# else
        rc = VINF_SUCCESS;
# endif
        pThis->svga.GCPhysFIFO = 0;
    }
    return rc;
}

# ifdef VBOX_WITH_VMSVGA3D

/**
 * Used by vmsvga3dInfoSurfaceWorker to make the FIFO thread to save one or all
 * surfaces to VMSVGA3DMIPMAPLEVEL::pSurfaceData heap buffers.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The The shared VGA/VMSVGA instance data.
 * @param   pThisCC     The VGA/VMSVGA state for ring-3.
 * @param   sid         Either UINT32_MAX or the ID of a specific surface.  If
 *                      UINT32_MAX is used, all surfaces are processed.
 */
void vmsvgaR33dSurfaceUpdateHeapBuffersOnFifoThread(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC, uint32_t sid)
{
    vmsvgaR3RunExtCmdOnFifoThread(pDevIns, pThis, pThisCC, VMSVGA_FIFO_EXTCMD_UPDATE_SURFACE_HEAP_BUFFERS, (void *)(uintptr_t)sid,
                                  sid == UINT32_MAX ? 10 * RT_MS_1SEC : RT_MS_1MIN);
}


/**
 * @callback_method_impl{FNDBGFHANDLERDEV, "vmsvga3dsfc"}
 */
static DECLCALLBACK(void) vmsvgaR3Info3dSurface(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    /* There might be a specific surface ID at the start of the
       arguments, if not show all surfaces. */
    uint32_t sid = UINT32_MAX;
    if (pszArgs)
        pszArgs = RTStrStripL(pszArgs);
    if (pszArgs && RT_C_IS_DIGIT(*pszArgs))
        sid = RTStrToUInt32(pszArgs);

    /* Verbose or terse display, we default to verbose. */
    bool fVerbose = true;
    if (RTStrIStr(pszArgs, "terse"))
        fVerbose = false;

    /* The size of the ascii art (x direction, y is 3/4 of x). */
    uint32_t cxAscii = 80;
    if (RTStrIStr(pszArgs, "gigantic"))
        cxAscii = 300;
    else if (RTStrIStr(pszArgs, "huge"))
        cxAscii = 180;
    else if (RTStrIStr(pszArgs, "big"))
        cxAscii = 132;
    else if (RTStrIStr(pszArgs, "normal"))
        cxAscii = 80;
    else if (RTStrIStr(pszArgs, "medium"))
        cxAscii = 64;
    else if (RTStrIStr(pszArgs, "small"))
        cxAscii = 48;
    else if (RTStrIStr(pszArgs, "tiny"))
        cxAscii = 24;

    /* Y invert the image when producing the ASCII art. */
    bool fInvY = false;
    if (RTStrIStr(pszArgs, "invy"))
        fInvY = true;

    vmsvga3dInfoSurfaceWorker(pDevIns, PDMDEVINS_2_DATA(pDevIns, PVGASTATE), PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC),
                              pHlp, sid, fVerbose, cxAscii, fInvY, NULL);
}


/**
 * @callback_method_impl{FNDBGFHANDLERDEV, "vmsvga3dsurf"}
 */
static DECLCALLBACK(void) vmsvgaR3Info3dSurfaceBmp(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    /* pszArg = "sid[>dir]"
     * Writes %dir%/info-S-sidI.bmp, where S - sequential bitmap number, I - decimal surface id.
     */
    char *pszBitmapPath = NULL;
    uint32_t sid = UINT32_MAX;
    if (pszArgs)
        pszArgs = RTStrStripL(pszArgs);
    if (pszArgs && RT_C_IS_DIGIT(*pszArgs))
        RTStrToUInt32Ex(pszArgs, &pszBitmapPath, 0, &sid);
    if (   pszBitmapPath
        && *pszBitmapPath == '>')
        ++pszBitmapPath;

    const bool fVerbose = true;
    const uint32_t cxAscii = 0; /* No ASCII */
    const bool fInvY = false;   /* Do not invert. */
    vmsvga3dInfoSurfaceWorker(pDevIns, PDMDEVINS_2_DATA(pDevIns, PVGASTATE), PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC),
                              pHlp, sid, fVerbose, cxAscii, fInvY, pszBitmapPath);
}

/**
 * @callback_method_impl{FNDBGFHANDLERDEV, "vmsvga3dctx"}
 */
static DECLCALLBACK(void) vmsvgaR3Info3dContext(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    /* There might be a specific surface ID at the start of the
       arguments, if not show all contexts. */
    uint32_t sid = UINT32_MAX;
    if (pszArgs)
        pszArgs = RTStrStripL(pszArgs);
    if (pszArgs && RT_C_IS_DIGIT(*pszArgs))
        sid = RTStrToUInt32(pszArgs);

    /* Verbose or terse display, we default to verbose. */
    bool fVerbose = true;
    if (RTStrIStr(pszArgs, "terse"))
        fVerbose = false;

    vmsvga3dInfoContextWorker(PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC), pHlp, sid, fVerbose);
}
# endif /* VBOX_WITH_VMSVGA3D */

/**
 * @callback_method_impl{FNDBGFHANDLERDEV, "vmsvga"}
 */
static DECLCALLBACK(void) vmsvgaR3Info(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PVGASTATE       pThis      = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC     pThisCC    = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    PVMSVGAR3STATE  pSVGAState = pThisCC->svga.pSvgaR3State;
    uint32_t RT_UNTRUSTED_VOLATILE_GUEST *pFIFO = pThisCC->svga.pau32FIFO;
    RT_NOREF(pszArgs);

    pHlp->pfnPrintf(pHlp, "Extension enabled:  %RTbool\n", pThis->svga.fEnabled);
    pHlp->pfnPrintf(pHlp, "Configured:         %RTbool\n", pThis->svga.fConfigured);
    pHlp->pfnPrintf(pHlp, "Base I/O port:      %#x\n",
                    pThis->hIoPortVmSvga != NIL_IOMIOPORTHANDLE
                    ? PDMDevHlpIoPortGetMappingAddress(pDevIns, pThis->hIoPortVmSvga) : UINT32_MAX);
    pHlp->pfnPrintf(pHlp, "FIFO address:       %RGp\n", pThis->svga.GCPhysFIFO);
    pHlp->pfnPrintf(pHlp, "FIFO size:          %u (%#x)\n", pThis->svga.cbFIFO, pThis->svga.cbFIFO);
    pHlp->pfnPrintf(pHlp, "FIFO external cmd:  %#x\n", pThis->svga.u8FIFOExtCommand);
    pHlp->pfnPrintf(pHlp, "FIFO extcmd wakeup: %u\n", pThis->svga.fFifoExtCommandWakeup);
    if (pFIFO)
        pHlp->pfnPrintf(pHlp, "FIFO min/max:       %u/%u\n", pFIFO[SVGA_FIFO_MIN], pFIFO[SVGA_FIFO_MAX]);
    pHlp->pfnPrintf(pHlp, "Busy:               %#x\n", pThis->svga.fBusy);
    pHlp->pfnPrintf(pHlp, "Traces:             %RTbool (effective: %RTbool)\n", pThis->svga.fTraces, pThis->svga.fVRAMTracking);
    pHlp->pfnPrintf(pHlp, "Guest ID:           %#x (%d)\n", pThis->svga.u32GuestId, pThis->svga.u32GuestId);
    pHlp->pfnPrintf(pHlp, "IRQ status:         %#x\n", pThis->svga.u32IrqStatus);
    pHlp->pfnPrintf(pHlp, "IRQ mask:           %#x\n", pThis->svga.u32IrqMask);
    pHlp->pfnPrintf(pHlp, "Pitch lock:         %#x (FIFO:%#x)\n", pThis->svga.u32PitchLock, pFIFO ? pFIFO[SVGA_FIFO_PITCHLOCK] : 0);
    pHlp->pfnPrintf(pHlp, "Current GMR ID:     %#x\n", pThis->svga.u32CurrentGMRId);
    pHlp->pfnPrintf(pHlp, "Device Capabilites: %#x\n", pThis->svga.u32DeviceCaps);
    pHlp->pfnPrintf(pHlp, "Device Cap2:        %#x\n", pThis->svga.u32DeviceCaps2);
    pHlp->pfnPrintf(pHlp, "Guest driver id:    %#x\n", pThis->svga.u32GuestDriverId);
    pHlp->pfnPrintf(pHlp, "Guest driver ver1:  %#x\n", pThis->svga.u32GuestDriverVer1);
    pHlp->pfnPrintf(pHlp, "Guest driver ver2:  %#x\n", pThis->svga.u32GuestDriverVer2);
    pHlp->pfnPrintf(pHlp, "Guest driver ver3:  %#x\n", pThis->svga.u32GuestDriverVer3);
    pHlp->pfnPrintf(pHlp, "Index reg:          %#x\n", pThis->svga.u32IndexReg);
    pHlp->pfnPrintf(pHlp, "Action flags:       %#x\n", pThis->svga.u32ActionFlags);
    pHlp->pfnPrintf(pHlp, "Max display size:   %ux%u\n", pThis->svga.u32MaxWidth, pThis->svga.u32MaxHeight);
    pHlp->pfnPrintf(pHlp, "Display size:       %ux%u %ubpp\n", pThis->svga.uWidth, pThis->svga.uHeight, pThis->svga.uBpp);
    pHlp->pfnPrintf(pHlp, "Scanline:           %u (%#x)\n", pThis->svga.cbScanline, pThis->svga.cbScanline);
    pHlp->pfnPrintf(pHlp, "Viewport position:  %ux%u\n", pThis->svga.viewport.x, pThis->svga.viewport.y);
    pHlp->pfnPrintf(pHlp, "Viewport size:      %ux%u\n", pThis->svga.viewport.cx, pThis->svga.viewport.cy);

    pHlp->pfnPrintf(pHlp, "Cursor active:      %RTbool\n", pSVGAState->Cursor.fActive);
    pHlp->pfnPrintf(pHlp, "Cursor hotspot:     %ux%u\n", pSVGAState->Cursor.xHotspot, pSVGAState->Cursor.yHotspot);
    pHlp->pfnPrintf(pHlp, "Cursor size:        %ux%u\n", pSVGAState->Cursor.width, pSVGAState->Cursor.height);
    pHlp->pfnPrintf(pHlp, "Cursor byte size:   %u (%#x)\n", pSVGAState->Cursor.cbData, pSVGAState->Cursor.cbData);
    if (pFIFO)
    {
        pHlp->pfnPrintf(pHlp, "FIFO cursor:        state %u, screen %d\n", pFIFO[SVGA_FIFO_CURSOR_ON], pFIFO[SVGA_FIFO_CURSOR_SCREEN_ID]);
        pHlp->pfnPrintf(pHlp, "FIFO cursor at:     %u,%u\n", pFIFO[SVGA_FIFO_CURSOR_X], pFIFO[SVGA_FIFO_CURSOR_Y]);
    }
    pHlp->pfnPrintf(pHlp, "Legacy cursor:      ID %u, state %u\n", pThis->svga.uCursorID, pThis->svga.uCursorOn);
    pHlp->pfnPrintf(pHlp, "Legacy cursor at:   %u,%u\n", pThis->svga.uCursorX, pThis->svga.uCursorY);

# ifdef VBOX_WITH_VMSVGA3D
    pHlp->pfnPrintf(pHlp, "3D enabled:         %RTbool\n", pThis->svga.f3DEnabled);
# endif
    if (pThisCC->pDrv)
    {
        pHlp->pfnPrintf(pHlp, "Driver mode:        %ux%u %ubpp\n", pThisCC->pDrv->cx, pThisCC->pDrv->cy, pThisCC->pDrv->cBits);
        pHlp->pfnPrintf(pHlp, "Driver pitch:       %u (%#x)\n", pThisCC->pDrv->cbScanline, pThisCC->pDrv->cbScanline);
    }

    /* Dump screen information. */
    for (unsigned iScreen = 0; iScreen < RT_ELEMENTS(pSVGAState->aScreens); ++iScreen)
    {
        VMSVGASCREENOBJECT *pScreen = vmsvgaR3GetScreenObject(pThisCC, iScreen);
        if (pScreen)
        {
            pHlp->pfnPrintf(pHlp, "Screen %u defined (ID %u):\n", iScreen, pScreen->idScreen);
            pHlp->pfnPrintf(pHlp, "  %u x %u x %ubpp @ %u, %u\n", pScreen->cWidth, pScreen->cHeight,
                            pScreen->cBpp, pScreen->xOrigin, pScreen->yOrigin);
            pHlp->pfnPrintf(pHlp, "  Pitch %u bytes, VRAM offset %X\n", pScreen->cbPitch, pScreen->offVRAM);
            pHlp->pfnPrintf(pHlp, "  Flags %X", pScreen->fuScreen);
            if (pScreen->fuScreen != SVGA_SCREEN_MUST_BE_SET)
            {
                pHlp->pfnPrintf(pHlp, " (");
                if (pScreen->fuScreen & SVGA_SCREEN_IS_PRIMARY)
                    pHlp->pfnPrintf(pHlp, " IS_PRIMARY");
                if (pScreen->fuScreen & SVGA_SCREEN_FULLSCREEN_HINT)
                    pHlp->pfnPrintf(pHlp, " FULLSCREEN_HINT");
                if (pScreen->fuScreen & SVGA_SCREEN_DEACTIVATE)
                    pHlp->pfnPrintf(pHlp, " DEACTIVATE");
                if (pScreen->fuScreen & SVGA_SCREEN_BLANKING)
                    pHlp->pfnPrintf(pHlp, " BLANKING");
                pHlp->pfnPrintf(pHlp, " )");
            }
            pHlp->pfnPrintf(pHlp, ", %smodified\n", pScreen->fModified ? "" : "not ");
        }
    }

}

static int vmsvgaR3LoadBufCtx(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC, PSSMHANDLE pSSM, PVMSVGACMDBUFCTX pBufCtx, SVGACBContext CBCtx)
{
    PCPDMDEVHLPR3 pHlp = pDevIns->pHlpR3;
    PVMSVGAR3STATE pSvgaR3State = pThisCC->svga.pSvgaR3State;

    uint32_t cSubmitted;
    int rc = pHlp->pfnSSMGetU32(pSSM, &cSubmitted);
    AssertLogRelRCReturn(rc, rc);

    for (uint32_t i = 0; i < cSubmitted; ++i)
    {
        PVMSVGACMDBUF pCmdBuf = vmsvgaR3CmdBufAlloc(pBufCtx);
        AssertPtrReturn(pCmdBuf, VERR_NO_MEMORY);

        pHlp->pfnSSMGetGCPhys(pSSM, &pCmdBuf->GCPhysCB);

        uint32_t u32;
        rc = pHlp->pfnSSMGetU32(pSSM, &u32);
        AssertRCReturn(rc, rc);
        AssertReturn(u32 == sizeof(SVGACBHeader), VERR_INVALID_STATE);
        pHlp->pfnSSMGetMem(pSSM, &pCmdBuf->hdr, sizeof(SVGACBHeader));

        rc = pHlp->pfnSSMGetU32(pSSM, &u32);
        AssertRCReturn(rc, rc);
        AssertReturn(u32 == pCmdBuf->hdr.length, VERR_INVALID_STATE);

        if (pCmdBuf->hdr.length)
        {
            pCmdBuf->pvCommands = RTMemAlloc(pCmdBuf->hdr.length);
            AssertPtrReturn(pCmdBuf->pvCommands, VERR_NO_MEMORY);

            rc = pHlp->pfnSSMGetMem(pSSM, pCmdBuf->pvCommands, pCmdBuf->hdr.length);
            AssertRCReturn(rc, rc);
        }

        if (RT_LIKELY(CBCtx < RT_ELEMENTS(pSvgaR3State->apCmdBufCtxs)))
        {
            vmsvgaR3CmdBufSubmitCtx(pDevIns, pThis, pThisCC, &pCmdBuf);
        }
        else
        {
            uint32_t offNextCmd = 0;
            vmsvgaR3CmdBufSubmitDC(pDevIns, pThisCC, &pCmdBuf, &offNextCmd);
        }

        /* Free the buffer if CmdBufSubmit* did not consume it. */
        vmsvgaR3CmdBufFree(pCmdBuf);
    }
    return rc;
}

static int vmsvgaR3LoadCommandBuffers(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC, PSSMHANDLE pSSM)
{
    PCPDMDEVHLPR3 pHlp = pDevIns->pHlpR3;
    PVMSVGAR3STATE pSvgaR3State = pThisCC->svga.pSvgaR3State;

    bool f;
    uint32_t u32;

    /* Device context command buffers. */
    int rc = vmsvgaR3LoadBufCtx(pDevIns, pThis, pThisCC, pSSM, &pSvgaR3State->CmdBufCtxDC, SVGA_CB_CONTEXT_MAX);
    AssertLogRelRCReturn(rc, rc);

    /* DX contexts command buffers. */
    uint32_t cBufCtx;
    rc = pHlp->pfnSSMGetU32(pSSM, &cBufCtx);
    AssertLogRelRCReturn(rc, rc);
    AssertReturn(cBufCtx == RT_ELEMENTS(pSvgaR3State->apCmdBufCtxs), VERR_INVALID_STATE);
    for (uint32_t j = 0; j < cBufCtx; ++j)
    {
        rc = pHlp->pfnSSMGetBool(pSSM, &f);
        AssertLogRelRCReturn(rc, rc);
        if (f)
        {
            pSvgaR3State->apCmdBufCtxs[j] = (PVMSVGACMDBUFCTX)RTMemAlloc(sizeof(VMSVGACMDBUFCTX));
            AssertPtrReturn(pSvgaR3State->apCmdBufCtxs[j], VERR_NO_MEMORY);
            vmsvgaR3CmdBufCtxInit(pSvgaR3State->apCmdBufCtxs[j]);

            rc = vmsvgaR3LoadBufCtx(pDevIns, pThis, pThisCC, pSSM, pSvgaR3State->apCmdBufCtxs[j], (SVGACBContext)j);
            AssertLogRelRCReturn(rc, rc);
        }
    }

    rc = pHlp->pfnSSMGetU32(pSSM, &u32);
    pSvgaR3State->fCmdBuf = u32;
    return rc;
}

static int vmsvgaR3LoadGbo(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, VMSVGAGBO *pGbo)
{
    PCPDMDEVHLPR3 pHlp = pDevIns->pHlpR3;

    int rc;
    pHlp->pfnSSMGetU32(pSSM, &pGbo->fGboFlags);
    pHlp->pfnSSMGetU32(pSSM, &pGbo->cTotalPages);
    pHlp->pfnSSMGetU32(pSSM, &pGbo->cbTotal);
    rc = pHlp->pfnSSMGetU32(pSSM, &pGbo->cDescriptors);
    AssertRCReturn(rc, rc);

    if (pGbo->cDescriptors)
    {
        pGbo->paDescriptors = (PVMSVGAGBODESCRIPTOR)RTMemAllocZ(pGbo->cDescriptors * sizeof(VMSVGAGBODESCRIPTOR));
        AssertPtrReturn(pGbo->paDescriptors, VERR_NO_MEMORY);
    }

    for (uint32_t iDesc = 0; iDesc < pGbo->cDescriptors; ++iDesc)
    {
        PVMSVGAGBODESCRIPTOR pDesc = &pGbo->paDescriptors[iDesc];
        pHlp->pfnSSMGetGCPhys(pSSM, &pDesc->GCPhys);
        rc = pHlp->pfnSSMGetU64(pSSM, &pDesc->cPages);
    }

    if (pGbo->fGboFlags & VMSVGAGBO_F_HOST_BACKED)
    {
        pGbo->pvHost = RTMemAlloc(pGbo->cbTotal);
        AssertPtrReturn(pGbo->pvHost, VERR_NO_MEMORY);
        rc = pHlp->pfnSSMGetMem(pSSM, pGbo->pvHost, pGbo->cbTotal);
    }

    return rc;
}

/**
 * Portion of VMSVGA state which must be loaded oin the FIFO thread.
 */
static int vmsvgaR3LoadExecFifo(PCPDMDEVHLPR3 pHlp, PVGASTATE pThis, PVGASTATECC pThisCC,
                                PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    RT_NOREF(uPass);

    PVMSVGAR3STATE  pSVGAState = pThisCC->svga.pSvgaR3State;
    int rc;

    if (uVersion >= VGA_SAVEDSTATE_VERSION_VMSVGA_SCREENS)
    {
        uint32_t cScreens = 0;
        rc = pHlp->pfnSSMGetU32(pSSM, &cScreens);
        AssertRCReturn(rc, rc);
        AssertLogRelMsgReturn(cScreens <= _64K, /* big enough */
                              ("cScreens=%#x\n", cScreens),
                              VERR_SSM_DATA_UNIT_FORMAT_CHANGED);

        for (uint32_t i = 0; i < cScreens; ++i)
        {
            VMSVGASCREENOBJECT screen;
            RT_ZERO(screen);

            rc = pHlp->pfnSSMGetStructEx(pSSM, &screen, sizeof(screen), 0, g_aVMSVGASCREENOBJECTFields, NULL);
            AssertLogRelRCReturn(rc, rc);

            if (screen.idScreen < RT_ELEMENTS(pSVGAState->aScreens))
            {
                VMSVGASCREENOBJECT *pScreen = &pSVGAState->aScreens[screen.idScreen];
                *pScreen = screen;
                pScreen->fModified = true;

                if (uVersion >= VGA_SAVEDSTATE_VERSION_VMSVGA_DX)
                {
                    uint32_t u32;
                    pHlp->pfnSSMGetU32(pSSM, &u32); /* Size of screen bitmap. */
                    AssertLogRelRCReturn(rc, rc);
                    if (u32)
                    {
                        pScreen->pvScreenBitmap = RTMemAlloc(u32);
                        AssertPtrReturn(pScreen->pvScreenBitmap, VERR_NO_MEMORY);

                        pHlp->pfnSSMGetMem(pSSM, pScreen->pvScreenBitmap, u32);
                    }
                }
            }
            else
            {
                LogRel(("VGA: ignored screen object %d\n", screen.idScreen));
            }
        }
    }
    else
    {
        /* Try to setup at least the first screen. */
        VMSVGASCREENOBJECT *pScreen = &pSVGAState->aScreens[0];
        Assert(pScreen->idScreen == 0);
        pScreen->fDefined  = true;
        pScreen->fModified = true;
        pScreen->fuScreen  = SVGA_SCREEN_MUST_BE_SET | SVGA_SCREEN_IS_PRIMARY;
        pScreen->xOrigin   = 0;
        pScreen->yOrigin   = 0;
        pScreen->offVRAM   = pThis->svga.uScreenOffset;
        pScreen->cbPitch   = pThis->svga.cbScanline;
        pScreen->cWidth    = pThis->svga.uWidth;
        pScreen->cHeight   = pThis->svga.uHeight;
        pScreen->cBpp      = pThis->svga.uBpp;
    }

    return VINF_SUCCESS;
}

/**
 * @copydoc FNSSMDEVLOADEXEC
 */
int vmsvgaR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    RT_NOREF(uPass);
    PVGASTATE       pThis      = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC     pThisCC    = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    PVMSVGAR3STATE  pSVGAState = pThisCC->svga.pSvgaR3State;
    PCPDMDEVHLPR3   pHlp       = pDevIns->pHlpR3;
    int             rc;

    /* Load our part of the VGAState */
    rc = pHlp->pfnSSMGetStructEx(pSSM, &pThis->svga, sizeof(pThis->svga), 0, g_aVGAStateSVGAFields, NULL);
    AssertRCReturn(rc, rc);

    if (pThis->svga.fVMSVGA2dGBO)
    {
        if (pThis->svga.u32DeviceCaps & SVGA_CAP_GBOBJECTS)
        {
            LogRel(("VGA: VMSVGA2dGBO enabled in VM config and SVGA_CAP_GBOBJECTS is present in Caps. 3D state should be loaded.\n"));
        }
        else
        {
            LogRel(("VGA: VMSVGA2dGBO enabled in VM config but SVGA_CAP_GBOBJECTS is NOT present in Caps, so fVMSVGA2dGBO should be forced to 0\n"));
            pThis->svga.fVMSVGA2dGBO = false;
        }
    }

    /* Load the VGA framebuffer. */
    AssertCompile(VMSVGA_VGA_FB_BACKUP_SIZE >= _32K);
    uint32_t cbVgaFramebuffer = _32K;
    if (uVersion >= VGA_SAVEDSTATE_VERSION_VMSVGA_VGA_FB_FIX)
    {
        rc = pHlp->pfnSSMGetU32(pSSM, &cbVgaFramebuffer);
        AssertRCReturn(rc, rc);
        AssertLogRelMsgReturn(cbVgaFramebuffer <= _4M && cbVgaFramebuffer >= _32K && RT_IS_POWER_OF_TWO(cbVgaFramebuffer),
                              ("cbVgaFramebuffer=%#x - expected 32KB..4MB, power of two\n", cbVgaFramebuffer),
                              VERR_SSM_DATA_UNIT_FORMAT_CHANGED);
        AssertCompile(VMSVGA_VGA_FB_BACKUP_SIZE <= _4M);
        AssertCompile(RT_IS_POWER_OF_TWO(VMSVGA_VGA_FB_BACKUP_SIZE));
    }
    rc = pHlp->pfnSSMGetMem(pSSM, pThisCC->svga.pbVgaFrameBufferR3, RT_MIN(cbVgaFramebuffer, VMSVGA_VGA_FB_BACKUP_SIZE));
    AssertRCReturn(rc, rc);
    if (cbVgaFramebuffer > VMSVGA_VGA_FB_BACKUP_SIZE)
        pHlp->pfnSSMSkip(pSSM, cbVgaFramebuffer - VMSVGA_VGA_FB_BACKUP_SIZE);
    else if (cbVgaFramebuffer < VMSVGA_VGA_FB_BACKUP_SIZE)
        RT_BZERO(&pThisCC->svga.pbVgaFrameBufferR3[cbVgaFramebuffer], VMSVGA_VGA_FB_BACKUP_SIZE - cbVgaFramebuffer);

    /* Load the VMSVGA state. */
    rc = pHlp->pfnSSMGetStructEx(pSSM, pSVGAState, sizeof(*pSVGAState), 0, g_aVMSVGAR3STATEFields, NULL);
    AssertRCReturn(rc, rc);

    /* Load the active cursor bitmaps. */
    if (pSVGAState->Cursor.fActive)
    {
        pSVGAState->Cursor.pData = RTMemAlloc(pSVGAState->Cursor.cbData);
        AssertReturn(pSVGAState->Cursor.pData, VERR_NO_MEMORY);

        rc = pHlp->pfnSSMGetMem(pSSM, pSVGAState->Cursor.pData, pSVGAState->Cursor.cbData);
        AssertRCReturn(rc, rc);
    }

    /* Load the GMR state. */
    uint32_t cGMR = 256; /* Hardcoded in previous saved state versions. */
    if (uVersion >= VGA_SAVEDSTATE_VERSION_VMSVGA_GMR_COUNT)
    {
        rc = pHlp->pfnSSMGetU32(pSSM, &cGMR);
        AssertRCReturn(rc, rc);
        /* Numbers of GMRs was never less than 256. 1MB is a large arbitrary limit. */
        AssertLogRelMsgReturn(cGMR <= _1M && cGMR >= 256,
                              ("cGMR=%#x - expected 256B..1MB\n", cGMR),
                              VERR_SSM_DATA_UNIT_FORMAT_CHANGED);
    }

    if (pThis->svga.cGMR != cGMR)
    {
        /* Reallocate GMR array. */
        Assert(pSVGAState->paGMR != NULL);
        RTMemFree(pSVGAState->paGMR);
        pSVGAState->paGMR = (PGMR)RTMemAllocZ(cGMR * sizeof(GMR));
        AssertReturn(pSVGAState->paGMR, VERR_NO_MEMORY);
        pThis->svga.cGMR = cGMR;
    }

    for (uint32_t i = 0; i < cGMR; ++i)
    {
        PGMR pGMR = &pSVGAState->paGMR[i];

        rc = pHlp->pfnSSMGetStructEx(pSSM, pGMR, sizeof(*pGMR), 0, g_aGMRFields, NULL);
        AssertRCReturn(rc, rc);

        if (pGMR->numDescriptors)
        {
            Assert(pGMR->cMaxPages || pGMR->cbTotal);
            pGMR->paDesc = (PVMSVGAGMRDESCRIPTOR)RTMemAllocZ(pGMR->numDescriptors * sizeof(VMSVGAGMRDESCRIPTOR));
            AssertReturn(pGMR->paDesc, VERR_NO_MEMORY);

            for (uint32_t j = 0; j < pGMR->numDescriptors; ++j)
            {
                rc = pHlp->pfnSSMGetStructEx(pSSM, &pGMR->paDesc[j], sizeof(pGMR->paDesc[j]), 0, g_aVMSVGAGMRDESCRIPTORFields, NULL);
                AssertRCReturn(rc, rc);
            }
        }
    }

    if (uVersion >= VGA_SAVEDSTATE_VERSION_VMSVGA_DX)
    {
        bool f;
        uint32_t u32;

        if (uVersion >= VGA_SAVEDSTATE_VERSION_VMSVGA_DX_CMDBUF)
        {
            /* Command buffers are saved independently from VGPU10. */
            rc = pHlp->pfnSSMGetBool(pSSM, &f);
            AssertLogRelRCReturn(rc, rc);
            if (f)
            {
                rc = vmsvgaR3LoadCommandBuffers(pDevIns, pThis, pThisCC, pSSM);
                AssertLogRelRCReturn(rc, rc);
            }
        }

        rc = pHlp->pfnSSMGetBool(pSSM, &f);
        AssertLogRelRCReturn(rc, rc);
        pThis->fVMSVGA10 = f;

        if (pThis->fVMSVGA10)
        {
            if (uVersion < VGA_SAVEDSTATE_VERSION_VMSVGA_DX_CMDBUF)
            {
                rc = vmsvgaR3LoadCommandBuffers(pDevIns, pThis, pThisCC, pSSM);
                AssertLogRelRCReturn(rc, rc);
            }

            /*
             * OTables GBOs.
             */
            rc = pHlp->pfnSSMGetU32(pSSM, &u32);
            AssertLogRelRCReturn(rc, rc);
            AssertReturn(u32 == SVGA_OTABLE_MAX, VERR_INVALID_STATE);
            for (int i = 0; i < SVGA_OTABLE_MAX; ++i)
            {
                VMSVGAGBO *pGbo = &pSVGAState->aGboOTables[i];
                rc = vmsvgaR3LoadGbo(pDevIns, pSSM, pGbo);
                AssertRCReturn(rc, rc);
            }

            /*
             * MOBs.
             */
            for (;;)
            {
                rc = pHlp->pfnSSMGetU32(pSSM, &u32); /* MOB id. */
                AssertRCReturn(rc, rc);
                if (u32 == SVGA_ID_INVALID)
                    break;

                PVMSVGAMOB pMob = (PVMSVGAMOB)RTMemAllocZ(sizeof(*pMob));
                AssertPtrReturn(pMob, VERR_NO_MEMORY);

                rc = vmsvgaR3LoadGbo(pDevIns, pSSM, &pMob->Gbo);
                AssertRCReturn(rc, rc);

                pMob->Core.Key = u32;
                if (RTAvlU32Insert(&pSVGAState->MOBTree, &pMob->Core))
                    RTListPrepend(&pSVGAState->MOBLRUList, &pMob->nodeLRU);
                else
                    AssertFailedReturn(VERR_NO_MEMORY);
            }

# ifdef VMSVGA3D_DX
            if (pThis->svga.f3DEnabled)
            {
                pHlp->pfnSSMGetU32(pSSM, &pSVGAState->idDXContextCurrent);
            }
# endif
        }
    }

#  ifdef RT_OS_DARWIN  /** @todo r=bird: this is normally done on the EMT, so for DARWIN we do that when loading saved state too now. See DevVGA-SVGA3d-shared.h. */
    vmsvgaR3PowerOnDevice(pDevIns, pThis, pThisCC, /*fLoadState=*/ true);
#  endif

    VMSVGA_STATE_LOAD LoadState;
    LoadState.pSSM     = pSSM;
    LoadState.uVersion = uVersion;
    LoadState.uPass    = uPass;
    rc = vmsvgaR3RunExtCmdOnFifoThread(pDevIns, pThis, pThisCC, VMSVGA_FIFO_EXTCMD_LOADSTATE, &LoadState, RT_INDEFINITE_WAIT);
    AssertLogRelRCReturn(rc, rc);

    return VINF_SUCCESS;
}

/**
 * Reinit the video mode after the state has been loaded.
 */
int vmsvgaR3LoadDone(PPDMDEVINS pDevIns)
{
    PVGASTATE       pThis      = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC     pThisCC    = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    PVMSVGAR3STATE  pSVGAState = pThisCC->svga.pSvgaR3State;

    /* VMSVGA is working via VBVA interface, therefore it needs to be
     * enabled on saved state restore. See @bugref{10071#c7}. */
    if (pThis->svga.fEnabled)
    {
        for (uint32_t idScreen = 0; idScreen < pThis->cMonitors; ++idScreen)
            pThisCC->pDrv->pfnVBVAEnable(pThisCC->pDrv, idScreen, NULL /*pHostFlags*/);
    }

    /* Set the active cursor. */
    if (pSVGAState->Cursor.fActive)
    {
        /* We don't store the alpha flag, but we can take a guess that if
         * the old register interface was used, the cursor was B&W.
         */
        bool    fAlpha = pThis->svga.uCursorOn ? false : true;

        int rc = pThisCC->pDrv->pfnVBVAMousePointerShape(pThisCC->pDrv,
                                                         true /*fVisible*/,
                                                         fAlpha,
                                                         pSVGAState->Cursor.xHotspot,
                                                         pSVGAState->Cursor.yHotspot,
                                                         pSVGAState->Cursor.width,
                                                         pSVGAState->Cursor.height,
                                                         pSVGAState->Cursor.pData);
        AssertRC(rc);

        if (pThis->svga.uCursorOn)
            pThisCC->pDrv->pfnVBVAReportCursorPosition(pThisCC->pDrv, VBVA_CURSOR_VALID_DATA, SVGA_ID_INVALID, pThis->svga.uCursorX, pThis->svga.uCursorY);
    }

    /* If the VRAM handler should not be registered, we have to explicitly
     * unregister it here!
     */
    if (!pThis->svga.fVRAMTracking)
    {
        vgaR3UnregisterVRAMHandler(pDevIns, pThis);
    }

    /* Let the FIFO thread deal with changing the mode. */
    ASMAtomicOrU32(&pThis->svga.u32ActionFlags, VMSVGA_ACTION_CHANGEMODE);

    return VINF_SUCCESS;
}

static int vmsvgaR3SaveBufCtx(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, PVMSVGACMDBUFCTX pBufCtx)
{
    PCPDMDEVHLPR3 pHlp = pDevIns->pHlpR3;

    int rc = pHlp->pfnSSMPutU32(pSSM, pBufCtx->cSubmitted);
    AssertLogRelRCReturn(rc, rc);
    if (pBufCtx->cSubmitted)
    {
        PVMSVGACMDBUF pIter;
        RTListForEach(&pBufCtx->listSubmitted, pIter, VMSVGACMDBUF, nodeBuffer)
        {
            pHlp->pfnSSMPutGCPhys(pSSM, pIter->GCPhysCB);
            pHlp->pfnSSMPutU32(pSSM, sizeof(SVGACBHeader));
            pHlp->pfnSSMPutMem(pSSM, &pIter->hdr, sizeof(SVGACBHeader));
            pHlp->pfnSSMPutU32(pSSM, pIter->hdr.length);
            if (pIter->hdr.length)
                rc = pHlp->pfnSSMPutMem(pSSM, pIter->pvCommands, pIter->hdr.length);
            AssertLogRelRCReturn(rc, rc);
        }
    }
    return rc;
}

static int vmsvgaR3SaveGbo(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, VMSVGAGBO *pGbo)
{
    PCPDMDEVHLPR3 pHlp = pDevIns->pHlpR3;

    int rc;
    pHlp->pfnSSMPutU32(pSSM, pGbo->fGboFlags);
    pHlp->pfnSSMPutU32(pSSM, pGbo->cTotalPages);
    pHlp->pfnSSMPutU32(pSSM, pGbo->cbTotal);
    rc =  pHlp->pfnSSMPutU32(pSSM, pGbo->cDescriptors);
    for (uint32_t iDesc = 0; iDesc < pGbo->cDescriptors; ++iDesc)
    {
        PVMSVGAGBODESCRIPTOR pDesc = &pGbo->paDescriptors[iDesc];
        pHlp->pfnSSMPutGCPhys(pSSM, pDesc->GCPhys);
        rc = pHlp->pfnSSMPutU64(pSSM, pDesc->cPages);
    }
    if (pGbo->fGboFlags & VMSVGAGBO_F_HOST_BACKED)
        rc = pHlp->pfnSSMPutMem(pSSM, pGbo->pvHost, pGbo->cbTotal);
    return rc;
}

/**
 * Portion of SVGA state which must be saved in the FIFO thread.
 */
static int vmsvgaR3SaveExecFifo(PCPDMDEVHLPR3 pHlp, PVGASTATECC pThisCC, PSSMHANDLE pSSM)
{
    PVMSVGAR3STATE  pSVGAState = pThisCC->svga.pSvgaR3State;
    int             rc;

    /* Save the screen objects. */
    /* Count defined screen object. */
    uint32_t cScreens = 0;
    for (uint32_t i = 0; i < RT_ELEMENTS(pSVGAState->aScreens); ++i)
    {
         if (pSVGAState->aScreens[i].fDefined)
             ++cScreens;
    }

    rc = pHlp->pfnSSMPutU32(pSSM, cScreens);
    AssertLogRelRCReturn(rc, rc);

    for (uint32_t i = 0; i < RT_ELEMENTS(pSVGAState->aScreens); ++i)
    {
        VMSVGASCREENOBJECT *pScreen = &pSVGAState->aScreens[i];
        if (!pScreen->fDefined)
            continue;

        rc = pHlp->pfnSSMPutStructEx(pSSM, pScreen, sizeof(*pScreen), 0, g_aVMSVGASCREENOBJECTFields, NULL);
        AssertLogRelRCReturn(rc, rc);

        /*
         * VGA_SAVEDSTATE_VERSION_VMSVGA_DX
         */
        if (pScreen->pvScreenBitmap)
        {
            uint32_t const cbScreenBitmap = pScreen->cHeight * pScreen->cbPitch;
            pHlp->pfnSSMPutU32(pSSM, cbScreenBitmap);
            pHlp->pfnSSMPutMem(pSSM, pScreen->pvScreenBitmap, cbScreenBitmap);
        }
        else
            pHlp->pfnSSMPutU32(pSSM, 0);
    }
    return VINF_SUCCESS;
}

/**
 * @copydoc FNSSMDEVSAVEEXEC
 */
int vmsvgaR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PVGASTATE       pThis      = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC     pThisCC    = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    PVMSVGAR3STATE  pSVGAState = pThisCC->svga.pSvgaR3State;
    PCPDMDEVHLPR3   pHlp       = pDevIns->pHlpR3;
    int             rc;

    /* Save our part of the VGAState */
    rc = pHlp->pfnSSMPutStructEx(pSSM, &pThis->svga, sizeof(pThis->svga), 0, g_aVGAStateSVGAFields, NULL);
    AssertLogRelRCReturn(rc, rc);

    /* Save the framebuffer backup. */
    rc = pHlp->pfnSSMPutU32(pSSM, VMSVGA_VGA_FB_BACKUP_SIZE);
    rc = pHlp->pfnSSMPutMem(pSSM, pThisCC->svga.pbVgaFrameBufferR3, VMSVGA_VGA_FB_BACKUP_SIZE);
    AssertLogRelRCReturn(rc, rc);

    /* Save the VMSVGA state. */
    rc = pHlp->pfnSSMPutStructEx(pSSM, pSVGAState, sizeof(*pSVGAState), 0, g_aVMSVGAR3STATEFields, NULL);
    AssertLogRelRCReturn(rc, rc);

    /* Save the active cursor bitmaps. */
    if (pSVGAState->Cursor.fActive)
    {
        rc = pHlp->pfnSSMPutMem(pSSM, pSVGAState->Cursor.pData, pSVGAState->Cursor.cbData);
        AssertLogRelRCReturn(rc, rc);
    }

    /* Save the GMR state */
    rc = pHlp->pfnSSMPutU32(pSSM, pThis->svga.cGMR);
    AssertLogRelRCReturn(rc, rc);
    for (uint32_t i = 0; i < pThis->svga.cGMR; ++i)
    {
        PGMR pGMR = &pSVGAState->paGMR[i];

        rc = pHlp->pfnSSMPutStructEx(pSSM, pGMR, sizeof(*pGMR), 0, g_aGMRFields, NULL);
        AssertLogRelRCReturn(rc, rc);

        for (uint32_t j = 0; j < pGMR->numDescriptors; ++j)
        {
            rc = pHlp->pfnSSMPutStructEx(pSSM, &pGMR->paDesc[j], sizeof(pGMR->paDesc[j]), 0, g_aVMSVGAGMRDESCRIPTORFields, NULL);
            AssertLogRelRCReturn(rc, rc);
        }
    }

    /*
     * VGA_SAVEDSTATE_VERSION_VMSVGA_DX+
     */
    if (pThis->svga.u32DeviceCaps & SVGA_CAP_COMMAND_BUFFERS)
    {
        rc = pHlp->pfnSSMPutBool(pSSM, true);
        AssertLogRelRCReturn(rc, rc);

        /* Device context command buffers. */
        rc = vmsvgaR3SaveBufCtx(pDevIns, pSSM, &pSVGAState->CmdBufCtxDC);
        AssertRCReturn(rc, rc);

        /* DX contexts command buffers. */
        rc = pHlp->pfnSSMPutU32(pSSM, RT_ELEMENTS(pSVGAState->apCmdBufCtxs));
        AssertLogRelRCReturn(rc, rc);
        for (unsigned i = 0; i < RT_ELEMENTS(pSVGAState->apCmdBufCtxs); ++i)
        {
            if (pSVGAState->apCmdBufCtxs[i])
            {
                pHlp->pfnSSMPutBool(pSSM, true);
                rc = vmsvgaR3SaveBufCtx(pDevIns, pSSM, pSVGAState->apCmdBufCtxs[i]);
                AssertRCReturn(rc, rc);
            }
            else
                pHlp->pfnSSMPutBool(pSSM, false);
        }

        rc = pHlp->pfnSSMPutU32(pSSM, pSVGAState->fCmdBuf);
        AssertRCReturn(rc, rc);
    }
    else
    {
        rc = pHlp->pfnSSMPutBool(pSSM, false);
        AssertLogRelRCReturn(rc, rc);
    }

    rc = pHlp->pfnSSMPutBool(pSSM, pThis->fVMSVGA10);
    AssertLogRelRCReturn(rc, rc);

    if (pThis->fVMSVGA10)
    {
        /*
         * OTables GBOs.
         */
        pHlp->pfnSSMPutU32(pSSM, SVGA_OTABLE_MAX);
        for (int i = 0; i < SVGA_OTABLE_MAX; ++i)
        {
            VMSVGAGBO *pGbo = &pSVGAState->aGboOTables[i];
            rc = vmsvgaR3SaveGbo(pDevIns, pSSM, pGbo);
            AssertRCReturn(rc, rc);
        }

        /*
         * MOBs.
         */
        PVMSVGAMOB pIter;
        RTListForEach(&pSVGAState->MOBLRUList, pIter, VMSVGAMOB, nodeLRU)
        {
            pHlp->pfnSSMPutU32(pSSM, pIter->Core.Key); /* MOB id. */
            rc = vmsvgaR3SaveGbo(pDevIns, pSSM, &pIter->Gbo);
            AssertRCReturn(rc, rc);
        }

        pHlp->pfnSSMPutU32(pSSM, SVGA_ID_INVALID); /* End marker. */

# ifdef VMSVGA3D_DX
        if (pThis->svga.f3DEnabled)
        {
            pHlp->pfnSSMPutU32(pSSM, pSVGAState->idDXContextCurrent);
        }
# endif
    }

    /*
     * Must save some state (3D in particular) in the FIFO thread.
     */
    rc = vmsvgaR3RunExtCmdOnFifoThread(pDevIns, pThis, pThisCC, VMSVGA_FIFO_EXTCMD_SAVESTATE, pSSM, RT_INDEFINITE_WAIT);
    AssertLogRelRCReturn(rc, rc);

    return VINF_SUCCESS;
}

/**
 * Destructor for PVMSVGAR3STATE structure. The structure is not deallocated.
 *
 * @param   pThis          The shared VGA/VMSVGA instance data.
 * @param   pThisCC        The device context.
 */
static void vmsvgaR3StateTerm(PVGASTATE pThis, PVGASTATECC pThisCC)
{
    PVMSVGAR3STATE pSVGAState = pThisCC->svga.pSvgaR3State;

# ifndef VMSVGA_USE_EMT_HALT_CODE
    if (pSVGAState->hBusyDelayedEmts != NIL_RTSEMEVENTMULTI)
    {
        RTSemEventMultiDestroy(pSVGAState->hBusyDelayedEmts);
        pSVGAState->hBusyDelayedEmts = NIL_RTSEMEVENT;
    }
# endif

    if (pSVGAState->Cursor.fActive)
    {
        RTMemFreeZ(pSVGAState->Cursor.pData, pSVGAState->Cursor.cbData);
        pSVGAState->Cursor.pData = NULL;
        pSVGAState->Cursor.fActive = false;
    }

    if (pSVGAState->paGMR)
    {
        for (unsigned i = 0; i < pThis->svga.cGMR; ++i)
            if (pSVGAState->paGMR[i].paDesc)
                RTMemFree(pSVGAState->paGMR[i].paDesc);

        RTMemFree(pSVGAState->paGMR);
        pSVGAState->paGMR = NULL;
    }

    if (RTCritSectIsInitialized(&pSVGAState->CritSectCmdBuf))
    {
        RTCritSectEnter(&pSVGAState->CritSectCmdBuf);
        for (unsigned i = 0; i < RT_ELEMENTS(pSVGAState->apCmdBufCtxs); ++i)
        {
            vmsvgaR3CmdBufCtxTerm(pSVGAState->apCmdBufCtxs[i]);
            RTMemFree(pSVGAState->apCmdBufCtxs[i]);
            pSVGAState->apCmdBufCtxs[i] = NULL;
        }
        vmsvgaR3CmdBufCtxTerm(&pSVGAState->CmdBufCtxDC);
        RTCritSectLeave(&pSVGAState->CritSectCmdBuf);
        RTCritSectDelete(&pSVGAState->CritSectCmdBuf);
    }
}

/**
 * Constructor for PVMSVGAR3STATE structure.
 *
 * @returns VBox status code.
 * @param   pDevIns        The PDM device instance.
 * @param   pThis          The shared VGA/VMSVGA instance data.
 * @param   pSVGAState     Pointer to the structure. It is already allocated.
 */
static int vmsvgaR3StateInit(PPDMDEVINS pDevIns, PVGASTATE pThis, PVMSVGAR3STATE pSVGAState)
{
    int rc = VINF_SUCCESS;

    pSVGAState->pDevIns = pDevIns;

    pSVGAState->paGMR = (PGMR)RTMemAllocZ(pThis->svga.cGMR * sizeof(GMR));
    AssertReturn(pSVGAState->paGMR, VERR_NO_MEMORY);

# ifndef VMSVGA_USE_EMT_HALT_CODE
    /* Create semaphore for delaying EMTs wait for the FIFO to stop being busy. */
    rc = RTSemEventMultiCreate(&pSVGAState->hBusyDelayedEmts);
    AssertRCReturn(rc, rc);
# endif

    rc = RTCritSectInit(&pSVGAState->CritSectCmdBuf);
    AssertRCReturn(rc, rc);

    /* Init screen ids which are constant and allow to use a pointer to aScreens element and know its index. */
    for (uint32_t i = 0; i < RT_ELEMENTS(pSVGAState->aScreens); ++i)
        pSVGAState->aScreens[i].idScreen = i;

    vmsvgaR3CmdBufCtxInit(&pSVGAState->CmdBufCtxDC);

    RTListInit(&pSVGAState->MOBLRUList);
# ifdef VBOX_WITH_VMSVGA3D
#  ifdef VMSVGA3D_DX
    pSVGAState->idDXContextCurrent = SVGA3D_INVALID_ID;
#  endif
# endif
    return rc;
}

# ifdef VBOX_WITH_VMSVGA3D
static void vmsvga3dR3Free3dInterfaces(PVGASTATECC pThisCC)
{
    PVMSVGAR3STATE pSVGAState = pThisCC->svga.pSvgaR3State;

    RTMemFree(pSVGAState->pFuncsMap);
    pSVGAState->pFuncsMap = NULL;
    RTMemFree(pSVGAState->pFuncsGBO);
    pSVGAState->pFuncsGBO = NULL;
    RTMemFree(pSVGAState->pFuncsDX);
    pSVGAState->pFuncsDX = NULL;
    RTMemFree(pSVGAState->pFuncsDXVideo);
    pSVGAState->pFuncsDXVideo = NULL;
    RTMemFree(pSVGAState->pFuncsVGPU9);
    pSVGAState->pFuncsVGPU9 = NULL;
    RTMemFree(pSVGAState->pFuncs3D);
    pSVGAState->pFuncs3D = NULL;
}

/* This structure is used only by vmsvgaR3Init3dInterfaces */
typedef struct VMSVGA3DINTERFACE
{
    char const *pcszName;
    uint32_t cbFuncs;
    void **ppvFuncs;
} VMSVGA3DINTERFACE;

extern VMSVGA3DBACKENDDESC const g_BackendLegacy;
#if defined(VMSVGA3D_DX_BACKEND)
extern VMSVGA3DBACKENDDESC const g_BackendDX;
#endif

/**
 * Initializes the optional host 3D backend interfaces.
 *
 * @returns VBox status code.
 * @param   pThisCC   The VGA/VMSVGA state for ring-3.
 */
static int vmsvgaR3Init3dInterfaces(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC)
{
#ifndef VMSVGA3D_DX
    RT_NOREF(pThis);
#endif

    PVMSVGAR3STATE pSVGAState = pThisCC->svga.pSvgaR3State;

#define ENTRY_3D_INTERFACE(a_Name, a_Field) { VMSVGA3D_BACKEND_INTERFACE_NAME_##a_Name, sizeof(VMSVGA3DBACKENDFUNCS##a_Name), (void **)&pSVGAState->a_Field }
    VMSVGA3DINTERFACE a3dInterface[] =
    {
        ENTRY_3D_INTERFACE(3D,    pFuncs3D),
        ENTRY_3D_INTERFACE(VGPU9, pFuncsVGPU9),
        ENTRY_3D_INTERFACE(DX,    pFuncsDX),
        ENTRY_3D_INTERFACE(DXVIDEO, pFuncsDXVideo),
        ENTRY_3D_INTERFACE(MAP,   pFuncsMap),
        ENTRY_3D_INTERFACE(GBO,   pFuncsGBO),
    };
#undef ENTRY_3D_INTERFACE

    VMSVGA3DBACKENDDESC const *pBackend = NULL;
#if defined(VMSVGA3D_DX_BACKEND)
    if (pThis->fVMSVGA10)
        pBackend = &g_BackendDX;
    else
#endif
        pBackend = &g_BackendLegacy;

    int rc = VINF_SUCCESS;
    for (uint32_t i = 0; i < RT_ELEMENTS(a3dInterface); ++i)
    {
        VMSVGA3DINTERFACE *p = &a3dInterface[i];

        if (pThis->svga.fVMSVGA2dGBO &&
            RTStrCmp(p->pcszName, VMSVGA3D_BACKEND_INTERFACE_NAME_MAP) &&
            RTStrCmp(p->pcszName, VMSVGA3D_BACKEND_INTERFACE_NAME_GBO))
        {
            continue;
        }

        int rc2 = pBackend->pfnQueryInterface(pThisCC, p->pcszName, NULL, p->cbFuncs);
        if (RT_SUCCESS(rc2))
        {
            *p->ppvFuncs = RTMemAllocZ(p->cbFuncs);
            AssertBreakStmt(*p->ppvFuncs, rc = VERR_NO_MEMORY);

            pBackend->pfnQueryInterface(pThisCC, p->pcszName, *p->ppvFuncs, p->cbFuncs);
        }
    }

    if (RT_SUCCESS(rc))
    {
        rc = vmsvga3dInit(pDevIns, pThis, pThisCC);
        if (RT_SUCCESS(rc))
            return VINF_SUCCESS;
    }

    vmsvga3dR3Free3dInterfaces(pThisCC);
    return rc;
}
# endif /* VBOX_WITH_VMSVGA3D */

/**
 * Compute the host capabilities: device and FIFO.
 *
 * Depends on 3D backend initialization.
 *
 * @param   pThis     The shared VGA/VMSVGA instance data.
 * @param   pThisCC   The VGA/VMSVGA state for ring-3.
 * @param   pu32DeviceCaps Device capabilities (SVGA_CAP_*).
 * @param   pu32DeviceCaps2 Device capabilities (SVGA_CAP2_*).
 * @param   pu32FIFOCaps FIFO capabilities (SVGA_FIFO_CAPS_*).
 */
static void vmsvgaR3GetCaps(PVGASTATE pThis, PVGASTATECC pThisCC, uint32_t *pu32DeviceCaps, uint32_t *pu32DeviceCaps2, uint32_t *pu32FIFOCaps)
{
#ifndef VBOX_WITH_VMSVGA3D
    RT_NOREF(pThisCC);
#endif

    /* Device caps. */
    *pu32DeviceCaps = SVGA_CAP_GMR
                    | SVGA_CAP_GMR2
                    | SVGA_CAP_CURSOR
                    | SVGA_CAP_CURSOR_BYPASS
                    | SVGA_CAP_CURSOR_BYPASS_2
                    | SVGA_CAP_EXTENDED_FIFO
                    | SVGA_CAP_IRQMASK
                    | SVGA_CAP_PITCHLOCK
                    | SVGA_CAP_RECT_COPY
                    | SVGA_CAP_TRACES
                    | SVGA_CAP_SCREEN_OBJECT_2
                    | SVGA_CAP_ALPHA_CURSOR;

    *pu32DeviceCaps |= SVGA_CAP_COMMAND_BUFFERS   /* Enable register based command buffer submission. */
                    ;

    *pu32DeviceCaps2 = SVGA_CAP2_NONE;

    /* VGPU10 capabilities. */
    if (pThis->fVMSVGA10)
    {
# ifdef VBOX_WITH_VMSVGA3D
        if (pThisCC->svga.pSvgaR3State->pFuncsGBO)
           *pu32DeviceCaps |= SVGA_CAP_GBOBJECTS; /* Enable guest-backed objects and surfaces. */
        if (pThisCC->svga.pSvgaR3State->pFuncsDX)
        {
           *pu32DeviceCaps |= SVGA_CAP_DX                 /* DX commands, and command buffers in a mob. */
                           |  SVGA_CAP_CAP2_REGISTER      /* Extended capabilities. */
                           ;

           if (*pu32DeviceCaps & SVGA_CAP_CAP2_REGISTER)
               *pu32DeviceCaps2 |= SVGA_CAP2_GROW_OTABLE  /* "Allow the GrowOTable/DXGrowCOTable commands" */
                                |  SVGA_CAP2_INTRA_SURFACE_COPY /* "IntraSurfaceCopy command" */
                                |  SVGA_CAP2_DX2          /* Shader Model 4.1.
                                                           * "Allow the DefineGBSurface_v3, WholeSurfaceCopy, WriteZeroSurface, and
                                                           * HintZeroSurface commands, and the SVGA_REG_GUEST_DRIVER_ID register."
                                                           */
                                |  SVGA_CAP2_GB_MEMSIZE_2 /* "Allow the SVGA_REG_GBOBJECT_MEM_SIZE_KB register" */
                                |  SVGA_CAP2_OTABLE_PTDEPTH_2
                                |  SVGA_CAP2_DX3          /* Shader Model 5.
                                                           * DefineGBSurface_v4, etc
                                                           */
                                ;
        }
# endif
    }

# ifdef VBOX_WITH_VMSVGA3D
    if (pThisCC->svga.pSvgaR3State->pFuncs3D)
        *pu32DeviceCaps |= SVGA_CAP_3D;
# endif

    /* FIFO capabilities. */
    if (!pThis->fVmSvga3)
        *pu32FIFOCaps = SVGA_FIFO_CAP_FENCE
                      | SVGA_FIFO_CAP_PITCHLOCK
                      | SVGA_FIFO_CAP_CURSOR_BYPASS_3
                      | SVGA_FIFO_CAP_RESERVE
                      | SVGA_FIFO_CAP_GMR2
                      | SVGA_FIFO_CAP_3D_HWVERSION_REVISED
                      | SVGA_FIFO_CAP_SCREEN_OBJECT_2;
}

/** Initialize the FIFO on power on and reset.
 *
 * @param   pThis     The shared VGA/VMSVGA instance data.
 * @param   pThisCC   The VGA/VMSVGA state for ring-3.
 */
static void vmsvgaR3InitFIFO(PVGASTATE pThis, PVGASTATECC pThisCC)
{
    RT_BZERO(pThisCC->svga.pau32FIFO, pThis->svga.cbFIFO);

    /* Valid with SVGA_FIFO_CAP_SCREEN_OBJECT_2 */
    pThisCC->svga.pau32FIFO[SVGA_FIFO_CURSOR_SCREEN_ID] = SVGA_ID_INVALID;
}

# ifdef VBOX_WITH_VMSVGA3D
/**
 * Tweak the host 3D capabilities (pThis->svga.au32DevCaps).
 *
 * @returns VBox status code.
 * @param   pThis     The shared VGA/VMSVGA instance data.
 * @param   pThisCC   The VGA/VMSVGA state for ring-3.
 */
static void vmsvgaR3Censor3DCaps(PVGASTATE pThis, PVGASTATECC pThisCC)
{
    RT_NOREF(pThisCC);

    /*
     * Hide extended VBoxSVGA capabilities if they are not enabled.
     */
    if (!pThis->svga.fVBoxExtensions)
        pThis->svga.au32DevCaps[SVGA3D_DEVCAP_3D] &= VBSVGA3D_CAP_3D;

    /*
     * D3D11 does not support multisampling for a number of formats:
     * https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/format-support-for-direct3d-11-1-feature-level-hardware
     * "Format support for Direct3D Feature Level 11.1 hardware"
     * Implementations on non-Windows hosts may report such support.
     * Windows 11 guest actually checks this.
     */
    static const uint32_t aDevCapNoMsaa[] =
    {
        SVGA3D_DEVCAP_DXFMT_R32_FLOAT_X8X24,
        SVGA3D_DEVCAP_DXFMT_X32_G8X24_UINT,
        SVGA3D_DEVCAP_DXFMT_R10G10B10_XR_BIAS_A2_UNORM,
        SVGA3D_DEVCAP_DXFMT_R24_UNORM_X8,
        SVGA3D_DEVCAP_DXFMT_X24_G8_UINT,
        SVGA3D_DEVCAP_DXFMT_R9G9B9E5_SHAREDEXP,
        SVGA3D_DEVCAP_DXFMT_R8G8_B8G8_UNORM,
        SVGA3D_DEVCAP_DXFMT_G8R8_G8B8_UNORM,
        SVGA3D_DEVCAP_DXFMT_BC1_TYPELESS,
        SVGA3D_DEVCAP_DXFMT_BC1_UNORM,
        SVGA3D_DEVCAP_DXFMT_BC1_UNORM_SRGB,
        SVGA3D_DEVCAP_DXFMT_BC2_TYPELESS,
        SVGA3D_DEVCAP_DXFMT_BC2_UNORM,
        SVGA3D_DEVCAP_DXFMT_BC2_UNORM_SRGB,
        SVGA3D_DEVCAP_DXFMT_BC3_TYPELESS,
        SVGA3D_DEVCAP_DXFMT_BC3_UNORM,
        SVGA3D_DEVCAP_DXFMT_BC3_UNORM_SRGB,
        SVGA3D_DEVCAP_DXFMT_BC4_TYPELESS,
        SVGA3D_DEVCAP_DXFMT_BC4_UNORM,
        SVGA3D_DEVCAP_DXFMT_BC4_SNORM,
        SVGA3D_DEVCAP_DXFMT_BC5_TYPELESS,
        SVGA3D_DEVCAP_DXFMT_BC5_UNORM,
        SVGA3D_DEVCAP_DXFMT_BC5_SNORM,
        SVGA3D_DEVCAP_DXFMT_BC6H_TYPELESS,
        SVGA3D_DEVCAP_DXFMT_BC6H_UF16,
        SVGA3D_DEVCAP_DXFMT_BC6H_SF16,
        SVGA3D_DEVCAP_DXFMT_BC7_TYPELESS,
        SVGA3D_DEVCAP_DXFMT_BC7_UNORM,
        SVGA3D_DEVCAP_DXFMT_BC7_UNORM_SRGB,
        SVGA3D_DEVCAP_DXFMT_NV12,
        SVGA3D_DEVCAP_DXFMT_YUY2,
        SVGA3D_DEVCAP_DXFMT_P8
    };

    for (unsigned i = 0; i < RT_ELEMENTS(aDevCapNoMsaa); ++i)
        pThis->svga.au32DevCaps[aDevCapNoMsaa[i]] &= ~SVGA3D_DXFMT_MULTISAMPLE;

    /*
     * Formats belonging to the same group must have the same multisample capability.
     */
    static const uint32_t aDevCapR32G32B32A32[] =
    {
        SVGA3D_DEVCAP_DXFMT_R32G32B32A32_TYPELESS,
        SVGA3D_DEVCAP_DXFMT_R32G32B32A32_UINT,
        SVGA3D_DEVCAP_DXFMT_R32G32B32A32_SINT,
        SVGA3D_DEVCAP_DXFMT_R32G32B32A32_FLOAT
    };

    static const uint32_t aDevCapR32G32B32[] =
    {
        SVGA3D_DEVCAP_DXFMT_R32G32B32_TYPELESS,
        SVGA3D_DEVCAP_DXFMT_R32G32B32_FLOAT,
        SVGA3D_DEVCAP_DXFMT_R32G32B32_UINT,
        SVGA3D_DEVCAP_DXFMT_R32G32B32_SINT
    };

    static const uint32_t aDevCapR16G16B16A16[] =
    {
        SVGA3D_DEVCAP_DXFMT_R16G16B16A16_TYPELESS,
        SVGA3D_DEVCAP_DXFMT_R16G16B16A16_UINT,
        SVGA3D_DEVCAP_DXFMT_R16G16B16A16_SNORM,
        SVGA3D_DEVCAP_DXFMT_R16G16B16A16_SINT,
        SVGA3D_DEVCAP_DXFMT_R16G16B16A16_FLOAT,
        SVGA3D_DEVCAP_DXFMT_R16G16B16A16_UNORM
    };

    static const uint32_t aDevCapR32G32[] =
    {
        SVGA3D_DEVCAP_DXFMT_R32G32_TYPELESS,
        SVGA3D_DEVCAP_DXFMT_R32G32_UINT,
        SVGA3D_DEVCAP_DXFMT_R32G32_SINT,
        SVGA3D_DEVCAP_DXFMT_R32G32_FLOAT
    };

    static const uint32_t aDevCapR32G8X24[] =
    {
        SVGA3D_DEVCAP_DXFMT_R32G8X24_TYPELESS,
        SVGA3D_DEVCAP_DXFMT_D32_FLOAT_S8X24_UINT
    };

    static const uint32_t aDevCapR10G10B10A2[] =
    {
        SVGA3D_DEVCAP_DXFMT_R10G10B10A2_TYPELESS,
        SVGA3D_DEVCAP_DXFMT_R10G10B10A2_UINT,
        SVGA3D_DEVCAP_DXFMT_R10G10B10A2_UNORM
    };

    static const uint32_t aDevCapR8G8B8A8[] =
    {
        SVGA3D_DEVCAP_DXFMT_R8G8B8A8_TYPELESS,
        SVGA3D_DEVCAP_DXFMT_R8G8B8A8_UNORM,
        SVGA3D_DEVCAP_DXFMT_R8G8B8A8_UNORM_SRGB,
        SVGA3D_DEVCAP_DXFMT_R8G8B8A8_UINT,
        SVGA3D_DEVCAP_DXFMT_R8G8B8A8_SINT,
        SVGA3D_DEVCAP_DXFMT_R8G8B8A8_SNORM
    };

    static const uint32_t aDevCapR16G16[] =
    {
        SVGA3D_DEVCAP_DXFMT_R16G16_TYPELESS,
        SVGA3D_DEVCAP_DXFMT_R16G16_UINT,
        SVGA3D_DEVCAP_DXFMT_R16G16_SINT,
        SVGA3D_DEVCAP_DXFMT_R16G16_FLOAT,
        SVGA3D_DEVCAP_DXFMT_R16G16_UNORM,
        SVGA3D_DEVCAP_DXFMT_R16G16_SNORM
    };

    static const uint32_t aDevCapR32[] =
    {
        SVGA3D_DEVCAP_DXFMT_R32_TYPELESS,
        SVGA3D_DEVCAP_DXFMT_D32_FLOAT,
        SVGA3D_DEVCAP_DXFMT_R32_UINT,
        SVGA3D_DEVCAP_DXFMT_R32_SINT,
        SVGA3D_DEVCAP_DXFMT_R32_FLOAT
    };

    static const uint32_t aDevCapR24G8[] =
    {
        SVGA3D_DEVCAP_DXFMT_R24G8_TYPELESS,
        SVGA3D_DEVCAP_DXFMT_D24_UNORM_S8_UINT
    };

    static const uint32_t aDevCapR8G8[] =
    {
        SVGA3D_DEVCAP_DXFMT_R8G8_TYPELESS,
        SVGA3D_DEVCAP_DXFMT_R8G8_UNORM,
        SVGA3D_DEVCAP_DXFMT_R8G8_UINT,
        SVGA3D_DEVCAP_DXFMT_R8G8_SINT,
        SVGA3D_DEVCAP_DXFMT_R8G8_SNORM
    };

    static const uint32_t aDevCapR16[] =
    {
        SVGA3D_DEVCAP_DXFMT_R16_TYPELESS,
        SVGA3D_DEVCAP_DXFMT_R16_UNORM,
        SVGA3D_DEVCAP_DXFMT_R16_UINT,
        SVGA3D_DEVCAP_DXFMT_R16_SNORM,
        SVGA3D_DEVCAP_DXFMT_R16_SINT,
        SVGA3D_DEVCAP_DXFMT_R16_FLOAT,
        SVGA3D_DEVCAP_DXFMT_D16_UNORM
    };

    static const uint32_t aDevCapR8[] =
    {
        SVGA3D_DEVCAP_DXFMT_R8_TYPELESS,
        SVGA3D_DEVCAP_DXFMT_R8_UNORM,
        SVGA3D_DEVCAP_DXFMT_R8_UINT,
        SVGA3D_DEVCAP_DXFMT_R8_SNORM,
        SVGA3D_DEVCAP_DXFMT_R8_SINT
    };

    static const uint32_t aDevCapB8G8R8A8[] =
    {
        SVGA3D_DEVCAP_DXFMT_B8G8R8A8_TYPELESS,
        SVGA3D_DEVCAP_DXFMT_B8G8R8A8_UNORM_SRGB,
        SVGA3D_DEVCAP_DXFMT_B8G8R8A8_UNORM
    };

    static const uint32_t aDevCapB8G8R8X8[] =
    {
        SVGA3D_DEVCAP_DXFMT_B8G8R8X8_TYPELESS,
        SVGA3D_DEVCAP_DXFMT_B8G8R8X8_UNORM_SRGB,
        SVGA3D_DEVCAP_DXFMT_B8G8R8X8_UNORM
    };

    typedef struct _FormatGroup
    {
        uint32_t cFormats;
        uint32_t const *pau32DevCaps;
        char const *szGroupName;
    } FormatGroup;

    #define FORMAT_GROUP_ENTRY(aFormat) { RT_ELEMENTS(aDevCap##aFormat), aDevCap##aFormat, #aFormat }
    static const FormatGroup aFormatGroup[] =
    {
        FORMAT_GROUP_ENTRY(R32G32B32A32),
        FORMAT_GROUP_ENTRY(R32G32B32),
        FORMAT_GROUP_ENTRY(R16G16B16A16),
        FORMAT_GROUP_ENTRY(R32G32),
        FORMAT_GROUP_ENTRY(R32G8X24),
        FORMAT_GROUP_ENTRY(R10G10B10A2),
        FORMAT_GROUP_ENTRY(R8G8B8A8),
        FORMAT_GROUP_ENTRY(R16G16),
        FORMAT_GROUP_ENTRY(R32),
        FORMAT_GROUP_ENTRY(R24G8),
        FORMAT_GROUP_ENTRY(R8G8),
        FORMAT_GROUP_ENTRY(R16),
        FORMAT_GROUP_ENTRY(R8),
        FORMAT_GROUP_ENTRY(B8G8R8A8),
        FORMAT_GROUP_ENTRY(B8G8R8X8)
    };
    #undef FORMAT_GROUP_ENTRY

    for (unsigned iGroup = 0; iGroup < RT_ELEMENTS(aFormatGroup); ++iGroup)
    {
        FormatGroup const *pGroup = &aFormatGroup[iGroup];

        /* Verify that all formats have the same MSAA capability. */
        uint32_t const fMSAA = pThis->svga.au32DevCaps[pGroup->pau32DevCaps[0]] & SVGA3D_DXFMT_MULTISAMPLE;
        for (unsigned i = 1; i < pGroup->cFormats; ++i)
        {
            if (fMSAA != (pThis->svga.au32DevCaps[pGroup->pau32DevCaps[i]] & SVGA3D_DXFMT_MULTISAMPLE))
            {
                /* If different MSAA capabilities have been detected. then disable MSAA for the group. */
                LogRel(("VMSVGA3d: disabling MSAA for %s\n", pGroup->szGroupName));
                for (unsigned j = 0; j < pGroup->cFormats; ++j)
                    pThis->svga.au32DevCaps[pGroup->pau32DevCaps[j]] &= ~SVGA3D_DXFMT_MULTISAMPLE;
                break;
            }
        }
    }
}

/**
 * Initializes the host 3D capabilities (pThis->svga.au32DevCaps).
 *
 * @returns VBox status code.
 * @param   pThis     The shared VGA/VMSVGA instance data.
 * @param   pThisCC   The VGA/VMSVGA state for ring-3.
 */
static void vmsvgaR3Init3DCaps(PVGASTATE pThis, PVGASTATECC pThisCC)
{
    /* Query the capabilities and store them in the pThis->svga.au32DevCaps array. */

    uint32_t au32FailedCapsBitmap[(RT_ELEMENTS(pThis->svga.au32DevCaps) + 31) / 32];
    RT_ZERO(au32FailedCapsBitmap);

    if (!pThis->svga.fVMSVGA2dGBO)
    {
        for (unsigned i = 0; i <= RT_ELEMENTS(pThis->svga.au32DevCaps); ++i)
        {
            uint32_t val = 0;
            int rc = vmsvga3dQueryCaps(pThisCC, (SVGA3dDevCapIndex)i, &val);
            if (RT_SUCCESS(rc))
                pThis->svga.au32DevCaps[i] = val;
            else
            {
                ASMBitSet(au32FailedCapsBitmap, i);
                pThis->svga.au32DevCaps[i] = 0;
            }
        }

        vmsvgaR3Censor3DCaps(pThis, pThisCC);
    }
    else
    {
        /* These max values are used by vmwgfx.ko only to validate a virtual displays layout. */
        pThis->svga.au32DevCaps[SVGA3D_DEVCAP_MAX_TEXTURE_WIDTH]  = 16384;
        pThis->svga.au32DevCaps[SVGA3D_DEVCAP_MAX_TEXTURE_HEIGHT] = 16384;
        pThis->svga.au32DevCaps[SVGA3D_DEVCAP_DXFMT_X8R8G8B8] = SVGA3D_DXFMT_SUPPORTED | SVGA3D_DXFMT_COLOR_RENDERTARGET;
    }

    bool const fSavedBuffering = RTLogRelSetBuffering(true);

    for (unsigned i = 0; i < RT_ELEMENTS(pThis->svga.au32DevCaps); ++i)
    {
        /* LogRel the capability value. */
        uint32_t const val = pThis->svga.au32DevCaps[i];
        if (i < SVGA3D_DEVCAP_MAX)
        {
            char const *pszDevCapName = &vmsvgaDevCapIndexToString((SVGA3dDevCapIndex)i)[sizeof("SVGA3D_DEVCAP")];
            if (!ASMBitTest(au32FailedCapsBitmap, i))
            {
                if (   i == SVGA3D_DEVCAP_MAX_POINT_SIZE
                    || i == SVGA3D_DEVCAP_MAX_LINE_WIDTH
                    || i == SVGA3D_DEVCAP_MAX_AA_LINE_WIDTH)
                {
                    float const fval = *(float *)&val;
                    LogRel(("VMSVGA3d: cap[%u]=" FLOAT_FMT_STR " {%s}\n", i, FLOAT_FMT_ARGS(fval), pszDevCapName));
                }
                else
                    LogRel(("VMSVGA3d: cap[%u]=%#010x {%s}\n", i, val, pszDevCapName));
            }
            else
                LogRel(("VMSVGA3d: cap[%u]=%#010x -{%s}\n", i, val, pszDevCapName));
        }
        else
            LogRel(("VMSVGA3d: new cap[%u]=%#010x%s\n", i, val, ASMBitTest(au32FailedCapsBitmap, i) ? " -" : ""));
    }

    RTLogRelSetBuffering(fSavedBuffering);
}


/**
 * Write the host 3D capabilities to FIFO memory.
 * Must be called after vmsvgaR3Init3DCaps.
 *
 * @returns VBox status code.
 * @param   pThis     The shared VGA/VMSVGA instance data.
 * @param   pThisCC   The VGA/VMSVGA state for ring-3.
 */
static void vmsvgaR3InitFIFO3D(PVGASTATE pThis, PVGASTATECC pThisCC)
{
    /* 3d hardware version; latest and greatest */
    pThisCC->svga.pau32FIFO[SVGA_FIFO_3D_HWVERSION_REVISED] = SVGA3D_HWVERSION_CURRENT;
    pThisCC->svga.pau32FIFO[SVGA_FIFO_3D_HWVERSION]         = SVGA3D_HWVERSION_CURRENT;

    /* Fill out 3d capabilities up to SVGA3D_DEVCAP_SURFACEFMT_ATI2 in the FIFO memory.
     * SVGA3D_DEVCAP_SURFACEFMT_ATI2 is the last capabiltiy for pre-SVGA_CAP_GBOBJECTS hardware.
     * If the VMSVGA device supports SVGA_CAP_GBOBJECTS capability, then the guest has to use SVGA_REG_DEV_CAP
     * register to query the devcaps. Older guests will still try to read the devcaps from FIFO.
     */
    SVGA3dCapsRecord *pCaps;
    SVGA3dCapPair    *pData;

    pCaps = (SVGA3dCapsRecord *)&pThisCC->svga.pau32FIFO[SVGA_FIFO_3D_CAPS];
    pCaps->header.type   = SVGA3DCAPS_RECORD_DEVCAPS;
    pData = (SVGA3dCapPair *)&pCaps->data;

    AssertCompile(SVGA3D_DEVCAP_DEAD1 == SVGA3D_DEVCAP_SURFACEFMT_ATI2 + 1);
    for (unsigned i = 0; i < SVGA3D_DEVCAP_DEAD1; ++i)
    {
        pData[i][0] = i;
        pData[i][1] = pThis->svga.au32DevCaps[i];
    }
    pCaps->header.length = (sizeof(pCaps->header) + SVGA3D_DEVCAP_DEAD1 * sizeof(SVGA3dCapPair)) / sizeof(uint32_t);
    pCaps = (SVGA3dCapsRecord *)((uint32_t *)pCaps + pCaps->header.length);

    /* Mark end of record array (a zero word). */
    pCaps->header.length = 0;
}
# endif /* VBOX_WITH_VMSVGA3D */

/**
 * Resets the SVGA hardware state
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 */
int vmsvgaR3Reset(PPDMDEVINS pDevIns)
{
    PVGASTATE       pThis      = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC     pThisCC    = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    PVMSVGAR3STATE  pSVGAState = pThisCC->svga.pSvgaR3State;

    /* Reset before init? */
    if (!pSVGAState)
        return VINF_SUCCESS;

    Log(("vmsvgaR3Reset\n"));

    if (!pThis->fVmSvga3)
    {
        /* Reset the FIFO processing as well as the 3d state (if we have one). */
        pThisCC->svga.pau32FIFO[SVGA_FIFO_NEXT_CMD] = pThisCC->svga.pau32FIFO[SVGA_FIFO_STOP] = 0; /** @todo should probably let the FIFO thread do this ... */
    }

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect); /* Hack around lock order issue. FIFO thread might take the lock. */

    int rc = vmsvgaR3RunExtCmdOnFifoThread(pDevIns, pThis, pThisCC, VMSVGA_FIFO_EXTCMD_RESET, NULL /*pvParam*/, 60000 /*ms*/);
    AssertLogRelRC(rc);

    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSect, rcLock);

    /* Reset other stuff. */
    pThis->svga.cScratchRegion = VMSVGA_SCRATCH_SIZE;
    RT_ZERO(pThis->svga.au32ScratchRegion);

    ASMAtomicWriteBool(&pThis->svga.fBadGuest, false);

    vmsvgaR3StateTerm(pThis, pThisCC);
    vmsvgaR3StateInit(pDevIns, pThis, pThisCC->svga.pSvgaR3State);

    RT_BZERO(pThisCC->svga.pbVgaFrameBufferR3, VMSVGA_VGA_FB_BACKUP_SIZE);

    if (!pThis->fVmSvga3)
        vmsvgaR3InitFIFO(pThis, pThisCC);

    /* Initialize FIFO and register capabilities. */
    vmsvgaR3GetCaps(pThis, pThisCC, &pThis->svga.u32DeviceCaps, &pThis->svga.u32DeviceCaps2, &pThisCC->svga.pau32FIFO[SVGA_FIFO_CAPABILITIES]);

# ifdef VBOX_WITH_VMSVGA3D
    if (pThis->svga.f3DEnabled)
    {
        vmsvgaR3Init3DCaps(pThis, pThisCC);
        if (!pThis->fVmSvga3)
            vmsvgaR3InitFIFO3D(pThis, pThisCC);
    }
# endif

    /* VRAM tracking is enabled by default during bootup. */
    pThis->svga.fVRAMTracking = true;
    pThis->svga.fEnabled      = false;

    /* Invalidate current settings. */
    pThis->svga.uWidth       = VMSVGA_VAL_UNINITIALIZED;
    pThis->svga.uHeight      = VMSVGA_VAL_UNINITIALIZED;
    pThis->svga.uBpp         = pThis->svga.uHostBpp;
    pThis->svga.cbScanline   = 0;
    pThis->svga.u32PitchLock = 0;

    return rc;
}

/**
 * Cleans up the SVGA hardware state
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 */
int vmsvgaR3Destruct(PPDMDEVINS pDevIns)
{
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);

    /*
     * Ask the FIFO thread to terminate the 3d state and then terminate it.
     */
    if (pThisCC->svga.pFIFOIOThread)
    {
        int rc = vmsvgaR3RunExtCmdOnFifoThread(pDevIns, pThis, pThisCC,  VMSVGA_FIFO_EXTCMD_TERMINATE,
                                               NULL /*pvParam*/, 30000 /*ms*/);
        AssertLogRelRC(rc);

        rc = PDMDevHlpThreadDestroy(pDevIns, pThisCC->svga.pFIFOIOThread, NULL);
        AssertLogRelRC(rc);
        pThisCC->svga.pFIFOIOThread = NULL;
    }

    /*
     * Destroy the special SVGA state.
     */
    if (pThisCC->svga.pSvgaR3State)
    {
        vmsvgaR3StateTerm(pThis, pThisCC);

# ifdef VBOX_WITH_VMSVGA3D
        vmsvga3dR3Free3dInterfaces(pThisCC);
# endif

        RTMemFree(pThisCC->svga.pSvgaR3State);
        pThisCC->svga.pSvgaR3State = NULL;
    }

    /*
     * Free our resources residing in the VGA state.
     */
    if (pThisCC->svga.pbVgaFrameBufferR3)
    {
        RTMemFree(pThisCC->svga.pbVgaFrameBufferR3);
        pThisCC->svga.pbVgaFrameBufferR3 = NULL;
    }
    if (pThisCC->svga.hFIFOExtCmdSem != NIL_RTSEMEVENT)
    {
        RTSemEventDestroy(pThisCC->svga.hFIFOExtCmdSem);
        pThisCC->svga.hFIFOExtCmdSem = NIL_RTSEMEVENT;
    }
    if (pThis->svga.hFIFORequestSem != NIL_SUPSEMEVENT)
    {
        PDMDevHlpSUPSemEventClose(pDevIns, pThis->svga.hFIFORequestSem);
        pThis->svga.hFIFORequestSem = NIL_SUPSEMEVENT;
    }

    return VINF_SUCCESS;
}

static DECLCALLBACK(size_t) vmsvga3dFloatFormat(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                                                const char *pszType, void const *pvValue,
                                                int cchWidth, int cchPrecision, unsigned fFlags, void *pvUser)
{
    RT_NOREF(pszType, cchWidth, cchPrecision, fFlags, pvUser);
    double const v = *(double *)&pvValue;
    return RTStrFormat(pfnOutput, pvArgOutput, NULL, 0, FLOAT_FMT_STR, FLOAT_FMT_ARGS(v));
}

/**
 * Initialize the SVGA hardware state
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 */
int vmsvgaR3Init(PPDMDEVINS pDevIns)
{
    PVGASTATE       pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);
    PVMSVGAR3STATE  pSVGAState;
    int             rc;

    rc = RTStrFormatTypeRegister("float", vmsvga3dFloatFormat, NULL);
    AssertMsgReturn(RT_SUCCESS(rc) || rc == VERR_ALREADY_EXISTS, ("%Rrc\n", rc), rc);

    pThis->svga.cScratchRegion = VMSVGA_SCRATCH_SIZE;
    memset(pThis->svga.au32ScratchRegion, 0, sizeof(pThis->svga.au32ScratchRegion));

    pThis->svga.cGMR = VMSVGA_MAX_GMR_IDS;

    /* Necessary for creating a backup of the text mode frame buffer when switching into svga mode. */
    pThisCC->svga.pbVgaFrameBufferR3 = (uint8_t *)RTMemAllocZ(VMSVGA_VGA_FB_BACKUP_SIZE);
    AssertReturn(pThisCC->svga.pbVgaFrameBufferR3, VERR_NO_MEMORY);

    /* Create event semaphore. */
    rc = PDMDevHlpSUPSemEventCreate(pDevIns, &pThis->svga.hFIFORequestSem);
    AssertRCReturn(rc, rc);

    /* Create event semaphore. */
    rc = RTSemEventCreate(&pThisCC->svga.hFIFOExtCmdSem);
    AssertRCReturn(rc, rc);

    pThisCC->svga.pSvgaR3State = (PVMSVGAR3STATE)RTMemAllocZ(sizeof(VMSVGAR3STATE));
    AssertReturn(pThisCC->svga.pSvgaR3State, VERR_NO_MEMORY);

    rc = vmsvgaR3StateInit(pDevIns, pThis, pThisCC->svga.pSvgaR3State);
    AssertMsgRCReturn(rc, ("Failed to create pSvgaR3State.\n"), rc);

    pSVGAState = pThisCC->svga.pSvgaR3State;

    /* VRAM tracking is enabled by default during bootup. */
    pThis->svga.fVRAMTracking = true;

    /* Set up the host bpp. This value is as a default for the programmable
     * bpp value. On old implementations, SVGA_REG_HOST_BITS_PER_PIXEL did not
     * exist and SVGA_REG_BITS_PER_PIXEL was read-only, returning what was later
     * separated as SVGA_REG_HOST_BITS_PER_PIXEL.
     *
     * NB: The driver cBits value is currently constant for the lifetime of the
     * VM. If that changes, the host bpp logic might need revisiting.
     */
    pThis->svga.uHostBpp = (pThisCC->pDrv->cBits + 7) & ~7;

    /* Invalidate current settings. */
    pThis->svga.uWidth     = VMSVGA_VAL_UNINITIALIZED;
    pThis->svga.uHeight    = VMSVGA_VAL_UNINITIALIZED;
    pThis->svga.uBpp       = pThis->svga.uHostBpp;
    pThis->svga.cbScanline = 0;

    pThis->svga.u32MaxWidth  = VBE_DISPI_MAX_XRES;
    pThis->svga.u32MaxHeight = VBE_DISPI_MAX_YRES;
    while (pThis->svga.u32MaxWidth * pThis->svga.u32MaxHeight * 4 /* 32 bpp */ > pThis->vram_size)
    {
        pThis->svga.u32MaxWidth  -= 256;
        pThis->svga.u32MaxHeight -= 256;
    }
    Log(("VMSVGA: Maximum size (%d,%d)\n", pThis->svga.u32MaxWidth, pThis->svga.u32MaxHeight));

    /* VBox commands and capabilities are always enabled for VirtualBox PCI device. */
    pThis->svga.fVBoxExtensions = !pThis->fVMSVGAPciId;

# ifdef DEBUG_GMR_ACCESS
    /* Register the GMR access handler type. */
    rc = PDMDevHlpPGMHandlerPhysicalTypeRegister(pDevIns, PGMPHYSHANDLERKIND_WRITE, vmsvgaR3GmrAccessHandler,
                                                 "VMSVGA GMR", &pThis->svga.hGmrAccessHandlerType);
    AssertRCReturn(rc, rc);
# endif

# if defined(VMSVGA_USE_FIFO_ACCESS_HANDLER) || defined(DEBUG_FIFO_ACCESS)
    /* Register the FIFO access handler type.  In addition to debugging FIFO
       access, this is also used to facilitate extended fifo thread sleeps. */
    rc = PDMDevHlpPGMHandlerPhysicalTypeRegister(pDevIns,
#  ifdef DEBUG_FIFO_ACCESS
                                                 PGMPHYSHANDLERKIND_ALL,
#  else
                                                 PGMPHYSHANDLERKIND_WRITE,
#  endif
                                                 vmsvgaR3FifoAccessHandler,
                                                 "VMSVGA FIFO", &pThis->svga.hFifoAccessHandlerType);
    AssertRCReturn(rc, rc);
# endif

    /* Create the async IO thread. */
    if (pThis->fVmSvga3)
    {
        /*
         * For SVGA 3 we use a different command processing loop because the standard FIFO loop would get riddled with
         * if (pThis->fVmsvga3) otherwise
         */
        rc = PDMDevHlpThreadCreate(pDevIns, &pThisCC->svga.pFIFOIOThread, pThis, vmsvgaR3CmdBufLoop, vmsvgaR3FifoLoopWakeUp, 0,
                                   RTTHREADTYPE_IO, "VMSVGA CMD");
    }
    else
        rc = PDMDevHlpThreadCreate(pDevIns, &pThisCC->svga.pFIFOIOThread, pThis, vmsvgaR3FifoLoop, vmsvgaR3FifoLoopWakeUp, 0,
                                   RTTHREADTYPE_IO, "VMSVGA FIFO");
    if (RT_FAILURE(rc))
    {
        AssertMsgFailed(("%s: Async IO Thread creation for FIFO handling failed rc=%d\n", __FUNCTION__, rc));
        return rc;
    }

    /*
     * Statistics.
     */
# define REG_CNT(a_pvSample, a_pszName, a_pszDesc) \
        PDMDevHlpSTAMRegister(pDevIns, (a_pvSample), STAMTYPE_COUNTER, a_pszName, STAMUNIT_OCCURENCES, a_pszDesc)
# define REG_PRF(a_pvSample, a_pszName, a_pszDesc) \
        PDMDevHlpSTAMRegister(pDevIns, (a_pvSample), STAMTYPE_PROFILE, a_pszName, STAMUNIT_TICKS_PER_CALL, a_pszDesc)
# ifdef VBOX_WITH_STATISTICS
    REG_PRF(&pSVGAState->StatR3Cmd3dDrawPrimitivesProf,   "VMSVGA/Cmd/3dDrawPrimitivesProf",       "Profiling of SVGA_3D_CMD_DRAW_PRIMITIVES.");
    REG_PRF(&pSVGAState->StatR3Cmd3dPresentProf,          "VMSVGA/Cmd/3dPresentProfBoth",          "Profiling of SVGA_3D_CMD_PRESENT and SVGA_3D_CMD_PRESENT_READBACK.");
    REG_PRF(&pSVGAState->StatR3Cmd3dSurfaceDmaProf,       "VMSVGA/Cmd/3dSurfaceDmaProf",           "Profiling of SVGA_3D_CMD_SURFACE_DMA.");
# endif
    REG_PRF(&pSVGAState->StatR3Cmd3dBlitSurfaceToScreenProf, "VMSVGA/Cmd/3dBlitSurfaceToScreenProf", "Profiling of SVGA_3D_CMD_BLIT_SURFACE_TO_SCREEN.");
    REG_CNT(&pSVGAState->StatR3Cmd3dActivateSurface,      "VMSVGA/Cmd/3dActivateSurface",          "SVGA_3D_CMD_ACTIVATE_SURFACE");
    REG_CNT(&pSVGAState->StatR3Cmd3dBeginQuery,           "VMSVGA/Cmd/3dBeginQuery",               "SVGA_3D_CMD_BEGIN_QUERY");
    REG_CNT(&pSVGAState->StatR3Cmd3dClear,                "VMSVGA/Cmd/3dClear",                    "SVGA_3D_CMD_CLEAR");
    REG_CNT(&pSVGAState->StatR3Cmd3dContextDefine,        "VMSVGA/Cmd/3dContextDefine",            "SVGA_3D_CMD_CONTEXT_DEFINE");
    REG_CNT(&pSVGAState->StatR3Cmd3dContextDestroy,       "VMSVGA/Cmd/3dContextDestroy",           "SVGA_3D_CMD_CONTEXT_DESTROY");
    REG_CNT(&pSVGAState->StatR3Cmd3dDeactivateSurface,    "VMSVGA/Cmd/3dDeactivateSurface",        "SVGA_3D_CMD_DEACTIVATE_SURFACE");
    REG_CNT(&pSVGAState->StatR3Cmd3dDrawPrimitives,       "VMSVGA/Cmd/3dDrawPrimitives",           "SVGA_3D_CMD_DRAW_PRIMITIVES");
    REG_CNT(&pSVGAState->StatR3Cmd3dEndQuery,             "VMSVGA/Cmd/3dEndQuery",                 "SVGA_3D_CMD_END_QUERY");
    REG_CNT(&pSVGAState->StatR3Cmd3dGenerateMipmaps,      "VMSVGA/Cmd/3dGenerateMipmaps",          "SVGA_3D_CMD_GENERATE_MIPMAPS");
    REG_CNT(&pSVGAState->StatR3Cmd3dPresent,              "VMSVGA/Cmd/3dPresent",                  "SVGA_3D_CMD_PRESENT");
    REG_CNT(&pSVGAState->StatR3Cmd3dPresentReadBack,      "VMSVGA/Cmd/3dPresentReadBack",          "SVGA_3D_CMD_PRESENT_READBACK");
    REG_CNT(&pSVGAState->StatR3Cmd3dSetClipPlane,         "VMSVGA/Cmd/3dSetClipPlane",             "SVGA_3D_CMD_SETCLIPPLANE");
    REG_CNT(&pSVGAState->StatR3Cmd3dSetLightData,         "VMSVGA/Cmd/3dSetLightData",             "SVGA_3D_CMD_SETLIGHTDATA");
    REG_CNT(&pSVGAState->StatR3Cmd3dSetLightEnable,       "VMSVGA/Cmd/3dSetLightEnable",           "SVGA_3D_CMD_SETLIGHTENABLE");
    REG_CNT(&pSVGAState->StatR3Cmd3dSetMaterial,          "VMSVGA/Cmd/3dSetMaterial",              "SVGA_3D_CMD_SETMATERIAL");
    REG_CNT(&pSVGAState->StatR3Cmd3dSetRenderState,       "VMSVGA/Cmd/3dSetRenderState",           "SVGA_3D_CMD_SETRENDERSTATE");
    REG_CNT(&pSVGAState->StatR3Cmd3dSetRenderTarget,      "VMSVGA/Cmd/3dSetRenderTarget",          "SVGA_3D_CMD_SETRENDERTARGET");
    REG_CNT(&pSVGAState->StatR3Cmd3dSetScissorRect,       "VMSVGA/Cmd/3dSetScissorRect",           "SVGA_3D_CMD_SETSCISSORRECT");
    REG_CNT(&pSVGAState->StatR3Cmd3dSetShader,            "VMSVGA/Cmd/3dSetShader",                "SVGA_3D_CMD_SET_SHADER");
    REG_CNT(&pSVGAState->StatR3Cmd3dSetShaderConst,       "VMSVGA/Cmd/3dSetShaderConst",           "SVGA_3D_CMD_SET_SHADER_CONST");
    REG_CNT(&pSVGAState->StatR3Cmd3dSetTextureState,      "VMSVGA/Cmd/3dSetTextureState",          "SVGA_3D_CMD_SETTEXTURESTATE");
    REG_CNT(&pSVGAState->StatR3Cmd3dSetTransform,         "VMSVGA/Cmd/3dSetTransform",             "SVGA_3D_CMD_SETTRANSFORM");
    REG_CNT(&pSVGAState->StatR3Cmd3dSetViewPort,          "VMSVGA/Cmd/3dSetViewPort",              "SVGA_3D_CMD_SETVIEWPORT");
    REG_CNT(&pSVGAState->StatR3Cmd3dSetZRange,            "VMSVGA/Cmd/3dSetZRange",                "SVGA_3D_CMD_SETZRANGE");
    REG_CNT(&pSVGAState->StatR3Cmd3dShaderDefine,         "VMSVGA/Cmd/3dShaderDefine",             "SVGA_3D_CMD_SHADER_DEFINE");
    REG_CNT(&pSVGAState->StatR3Cmd3dShaderDestroy,        "VMSVGA/Cmd/3dShaderDestroy",            "SVGA_3D_CMD_SHADER_DESTROY");
    REG_CNT(&pSVGAState->StatR3Cmd3dSurfaceCopy,          "VMSVGA/Cmd/3dSurfaceCopy",              "SVGA_3D_CMD_SURFACE_COPY");
    REG_CNT(&pSVGAState->StatR3Cmd3dSurfaceDefine,        "VMSVGA/Cmd/3dSurfaceDefine",            "SVGA_3D_CMD_SURFACE_DEFINE");
    REG_CNT(&pSVGAState->StatR3Cmd3dSurfaceDefineV2,      "VMSVGA/Cmd/3dSurfaceDefineV2",          "SVGA_3D_CMD_SURFACE_DEFINE_V2");
    REG_CNT(&pSVGAState->StatR3Cmd3dSurfaceDestroy,       "VMSVGA/Cmd/3dSurfaceDestroy",           "SVGA_3D_CMD_SURFACE_DESTROY");
    REG_CNT(&pSVGAState->StatR3Cmd3dSurfaceDma,           "VMSVGA/Cmd/3dSurfaceDma",               "SVGA_3D_CMD_SURFACE_DMA");
    REG_CNT(&pSVGAState->StatR3Cmd3dSurfaceScreen,        "VMSVGA/Cmd/3dSurfaceScreen",            "SVGA_3D_CMD_SURFACE_SCREEN");
    REG_CNT(&pSVGAState->StatR3Cmd3dSurfaceStretchBlt,    "VMSVGA/Cmd/3dSurfaceStretchBlt",        "SVGA_3D_CMD_SURFACE_STRETCHBLT");
    REG_CNT(&pSVGAState->StatR3Cmd3dWaitForQuery,         "VMSVGA/Cmd/3dWaitForQuery",             "SVGA_3D_CMD_WAIT_FOR_QUERY");
    REG_CNT(&pSVGAState->StatR3CmdAnnotationCopy,         "VMSVGA/Cmd/AnnotationCopy",             "SVGA_CMD_ANNOTATION_COPY");
    REG_CNT(&pSVGAState->StatR3CmdAnnotationFill,         "VMSVGA/Cmd/AnnotationFill",             "SVGA_CMD_ANNOTATION_FILL");
    REG_CNT(&pSVGAState->StatR3CmdBlitGmrFbToScreen,      "VMSVGA/Cmd/BlitGmrFbToScreen",          "SVGA_CMD_BLIT_GMRFB_TO_SCREEN");
    REG_CNT(&pSVGAState->StatR3CmdBlitScreentoGmrFb,      "VMSVGA/Cmd/BlitScreentoGmrFb",          "SVGA_CMD_BLIT_SCREEN_TO_GMRFB");
    REG_CNT(&pSVGAState->StatR3CmdDefineAlphaCursor,      "VMSVGA/Cmd/DefineAlphaCursor",          "SVGA_CMD_DEFINE_ALPHA_CURSOR");
    REG_CNT(&pSVGAState->StatR3CmdDefineCursor,           "VMSVGA/Cmd/DefineCursor",               "SVGA_CMD_DEFINE_CURSOR");
    REG_CNT(&pSVGAState->StatR3CmdMoveCursor,             "VMSVGA/Cmd/MoveCursor",                 "SVGA_CMD_MOVE_CURSOR");
    REG_CNT(&pSVGAState->StatR3CmdDisplayCursor,          "VMSVGA/Cmd/DisplayCursor",              "SVGA_CMD_DISPLAY_CURSOR");
    REG_CNT(&pSVGAState->StatR3CmdRectFill,               "VMSVGA/Cmd/RectFill",                   "SVGA_CMD_RECT_FILL");
    REG_CNT(&pSVGAState->StatR3CmdRectCopy,               "VMSVGA/Cmd/RectCopy",                   "SVGA_CMD_RECT_COPY");
    REG_CNT(&pSVGAState->StatR3CmdRectRopCopy,            "VMSVGA/Cmd/RectRopCopy",                "SVGA_CMD_RECT_ROP_COPY");
    REG_CNT(&pSVGAState->StatR3CmdDefineGmr2,             "VMSVGA/Cmd/DefineGmr2",                 "SVGA_CMD_DEFINE_GMR2");
    REG_CNT(&pSVGAState->StatR3CmdDefineGmr2Free,         "VMSVGA/Cmd/DefineGmr2/Free",            "Number of SVGA_CMD_DEFINE_GMR2 commands that only frees.");
    REG_CNT(&pSVGAState->StatR3CmdDefineGmr2Modify,       "VMSVGA/Cmd/DefineGmr2/Modify",          "Number of SVGA_CMD_DEFINE_GMR2 commands that redefines a non-free GMR.");
    REG_CNT(&pSVGAState->StatR3CmdDefineGmrFb,            "VMSVGA/Cmd/DefineGmrFb",                "SVGA_CMD_DEFINE_GMRFB");
    REG_CNT(&pSVGAState->StatR3CmdDefineScreen,           "VMSVGA/Cmd/DefineScreen",               "SVGA_CMD_DEFINE_SCREEN");
    REG_CNT(&pSVGAState->StatR3CmdDestroyScreen,          "VMSVGA/Cmd/DestroyScreen",              "SVGA_CMD_DESTROY_SCREEN");
    REG_CNT(&pSVGAState->StatR3CmdEscape,                 "VMSVGA/Cmd/Escape",                     "SVGA_CMD_ESCAPE");
    REG_CNT(&pSVGAState->StatR3CmdFence,                  "VMSVGA/Cmd/Fence",                      "SVGA_CMD_FENCE");
    REG_CNT(&pSVGAState->StatR3CmdInvalidCmd,             "VMSVGA/Cmd/InvalidCmd",                 "SVGA_CMD_INVALID_CMD");
    REG_CNT(&pSVGAState->StatR3CmdRemapGmr2,              "VMSVGA/Cmd/RemapGmr2",                  "SVGA_CMD_REMAP_GMR2");
    REG_CNT(&pSVGAState->StatR3CmdRemapGmr2Modify,        "VMSVGA/Cmd/RemapGmr2/Modify",           "Number of SVGA_CMD_REMAP_GMR2 commands that modifies rather than complete the definition of a GMR.");
    REG_CNT(&pSVGAState->StatR3CmdUpdate,                 "VMSVGA/Cmd/Update",                     "SVGA_CMD_UPDATE");
    REG_CNT(&pSVGAState->StatR3CmdUpdateVerbose,          "VMSVGA/Cmd/UpdateVerbose",              "SVGA_CMD_UPDATE_VERBOSE");

    REG_CNT(&pSVGAState->StatR3RegConfigDoneWr,           "VMSVGA/Reg/ConfigDoneWrite",            "SVGA_REG_CONFIG_DONE writes");
    REG_CNT(&pSVGAState->StatR3RegGmrDescriptorWr,        "VMSVGA/Reg/GmrDescriptorWrite",         "SVGA_REG_GMR_DESCRIPTOR writes");
    REG_CNT(&pSVGAState->StatR3RegGmrDescriptorWrErrors,  "VMSVGA/Reg/GmrDescriptorWrite/Errors",  "Number of erroneous SVGA_REG_GMR_DESCRIPTOR commands.");
    REG_CNT(&pSVGAState->StatR3RegGmrDescriptorWrFree,    "VMSVGA/Reg/GmrDescriptorWrite/Free",    "Number of SVGA_REG_GMR_DESCRIPTOR commands only freeing the GMR.");
    REG_CNT(&pThis->svga.StatRegBitsPerPixelWr,           "VMSVGA/Reg/BitsPerPixelWrite",          "SVGA_REG_BITS_PER_PIXEL writes.");
    REG_CNT(&pThis->svga.StatRegBusyWr,                   "VMSVGA/Reg/BusyWrite",                  "SVGA_REG_BUSY writes.");
    REG_CNT(&pThis->svga.StatRegCursorXWr,                "VMSVGA/Reg/CursorXWrite",               "SVGA_REG_CURSOR_X writes.");
    REG_CNT(&pThis->svga.StatRegCursorYWr,                "VMSVGA/Reg/CursorYWrite",               "SVGA_REG_CURSOR_Y writes.");
    REG_CNT(&pThis->svga.StatRegCursorIdWr,               "VMSVGA/Reg/CursorIdWrite",              "SVGA_REG_DEAD (SVGA_REG_CURSOR_ID) writes.");
    REG_CNT(&pThis->svga.StatRegCursorOnWr,               "VMSVGA/Reg/CursorOnWrite",              "SVGA_REG_CURSOR_ON writes.");
    REG_CNT(&pThis->svga.StatRegDepthWr,                  "VMSVGA/Reg/DepthWrite",                 "SVGA_REG_DEPTH writes.");
    REG_CNT(&pThis->svga.StatRegDisplayHeightWr,          "VMSVGA/Reg/DisplayHeightWrite",         "SVGA_REG_DISPLAY_HEIGHT writes.");
    REG_CNT(&pThis->svga.StatRegDisplayIdWr,              "VMSVGA/Reg/DisplayIdWrite",             "SVGA_REG_DISPLAY_ID writes.");
    REG_CNT(&pThis->svga.StatRegDisplayIsPrimaryWr,       "VMSVGA/Reg/DisplayIsPrimaryWrite",      "SVGA_REG_DISPLAY_IS_PRIMARY writes.");
    REG_CNT(&pThis->svga.StatRegDisplayPositionXWr,       "VMSVGA/Reg/DisplayPositionXWrite",      "SVGA_REG_DISPLAY_POSITION_X writes.");
    REG_CNT(&pThis->svga.StatRegDisplayPositionYWr,       "VMSVGA/Reg/DisplayPositionYWrite",      "SVGA_REG_DISPLAY_POSITION_Y writes.");
    REG_CNT(&pThis->svga.StatRegDisplayWidthWr,           "VMSVGA/Reg/DisplayWidthWrite",          "SVGA_REG_DISPLAY_WIDTH writes.");
    REG_CNT(&pThis->svga.StatRegEnableWr,                 "VMSVGA/Reg/EnableWrite",                "SVGA_REG_ENABLE writes.");
    REG_CNT(&pThis->svga.StatRegGmrIdWr,                  "VMSVGA/Reg/GmrIdWrite",                 "SVGA_REG_GMR_ID writes.");
    REG_CNT(&pThis->svga.StatRegGuestIdWr,                "VMSVGA/Reg/GuestIdWrite",               "SVGA_REG_GUEST_ID writes.");
    REG_CNT(&pThis->svga.StatRegHeightWr,                 "VMSVGA/Reg/HeightWrite",                "SVGA_REG_HEIGHT writes.");
    REG_CNT(&pThis->svga.StatRegIdWr,                     "VMSVGA/Reg/IdWrite",                    "SVGA_REG_ID writes.");
    REG_CNT(&pThis->svga.StatRegIrqMaskWr,                "VMSVGA/Reg/IrqMaskWrite",               "SVGA_REG_IRQMASK writes.");
    REG_CNT(&pThis->svga.StatRegNumDisplaysWr,            "VMSVGA/Reg/NumDisplaysWrite",           "SVGA_REG_NUM_DISPLAYS writes.");
    REG_CNT(&pThis->svga.StatRegNumGuestDisplaysWr,       "VMSVGA/Reg/NumGuestDisplaysWrite",      "SVGA_REG_NUM_GUEST_DISPLAYS writes.");
    REG_CNT(&pThis->svga.StatRegPaletteWr,                "VMSVGA/Reg/PaletteWrite",               "SVGA_PALETTE_XXXX writes.");
    REG_CNT(&pThis->svga.StatRegPitchLockWr,              "VMSVGA/Reg/PitchLockWrite",             "SVGA_REG_PITCHLOCK writes.");
    REG_CNT(&pThis->svga.StatRegPseudoColorWr,            "VMSVGA/Reg/PseudoColorWrite",           "SVGA_REG_PSEUDOCOLOR writes.");
    REG_CNT(&pThis->svga.StatRegReadOnlyWr,               "VMSVGA/Reg/ReadOnlyWrite",              "Read-only SVGA_REG_XXXX writes.");
    REG_CNT(&pThis->svga.StatRegScratchWr,                "VMSVGA/Reg/ScratchWrite",               "SVGA_REG_SCRATCH_XXXX writes.");
    REG_CNT(&pThis->svga.StatRegSyncWr,                   "VMSVGA/Reg/SyncWrite",                  "SVGA_REG_SYNC writes.");
    REG_CNT(&pThis->svga.StatRegTopWr,                    "VMSVGA/Reg/TopWrite",                   "SVGA_REG_TOP writes.");
    REG_CNT(&pThis->svga.StatRegTracesWr,                 "VMSVGA/Reg/TracesWrite",                "SVGA_REG_TRACES writes.");
    REG_CNT(&pThis->svga.StatRegUnknownWr,                "VMSVGA/Reg/UnknownWrite",               "Writes to unknown register.");
    REG_CNT(&pThis->svga.StatRegWidthWr,                  "VMSVGA/Reg/WidthWrite",                 "SVGA_REG_WIDTH writes.");
    REG_CNT(&pThis->svga.StatRegCommandLowWr,             "VMSVGA/Reg/CommandLowWrite",            "SVGA_REG_COMMAND_LOW writes.");
    REG_CNT(&pThis->svga.StatRegCommandHighWr,            "VMSVGA/Reg/CommandHighWrite",           "SVGA_REG_COMMAND_HIGH writes.");
    REG_CNT(&pThis->svga.StatRegDevCapWr,                 "VMSVGA/Reg/DevCapWrite",                "SVGA_REG_DEV_CAP writes.");
    REG_CNT(&pThis->svga.StatRegCmdPrependLowWr,          "VMSVGA/Reg/CmdPrependLowWrite",         "SVGA_REG_CMD_PREPEND_LOW writes.");
    REG_CNT(&pThis->svga.StatRegCmdPrependHighWr,         "VMSVGA/Reg/CmdPrependHighWrite",        "SVGA_REG_CMD_PREPEND_HIGH writes.");

    REG_CNT(&pThis->svga.StatRegBitsPerPixelRd,           "VMSVGA/Reg/BitsPerPixelRead",           "SVGA_REG_BITS_PER_PIXEL reads.");
    REG_CNT(&pThis->svga.StatRegBlueMaskRd,               "VMSVGA/Reg/BlueMaskRead",               "SVGA_REG_BLUE_MASK reads.");
    REG_CNT(&pThis->svga.StatRegBusyRd,                   "VMSVGA/Reg/BusyRead",                   "SVGA_REG_BUSY reads.");
    REG_CNT(&pThis->svga.StatRegBytesPerLineRd,           "VMSVGA/Reg/BytesPerLineRead",           "SVGA_REG_BYTES_PER_LINE reads.");
    REG_CNT(&pThis->svga.StatRegCapabilitesRd,            "VMSVGA/Reg/CapabilitesRead",            "SVGA_REG_CAPABILITIES reads.");
    REG_CNT(&pThis->svga.StatRegConfigDoneRd,             "VMSVGA/Reg/ConfigDoneRead",             "SVGA_REG_CONFIG_DONE reads.");
    REG_CNT(&pThis->svga.StatRegCursorXRd,                "VMSVGA/Reg/CursorXRead",                "SVGA_REG_CURSOR_X reads.");
    REG_CNT(&pThis->svga.StatRegCursorYRd,                "VMSVGA/Reg/CursorYRead",                "SVGA_REG_CURSOR_Y reads.");
    REG_CNT(&pThis->svga.StatRegCursorIdRd,               "VMSVGA/Reg/CursorIdRead",               "SVGA_REG_DEAD (SVGA_REG_CURSOR_ID) reads.");
    REG_CNT(&pThis->svga.StatRegCursorOnRd,               "VMSVGA/Reg/CursorOnRead",               "SVGA_REG_CURSOR_ON reads.");
    REG_CNT(&pThis->svga.StatRegDepthRd,                  "VMSVGA/Reg/DepthRead",                  "SVGA_REG_DEPTH reads.");
    REG_CNT(&pThis->svga.StatRegDisplayHeightRd,          "VMSVGA/Reg/DisplayHeightRead",          "SVGA_REG_DISPLAY_HEIGHT reads.");
    REG_CNT(&pThis->svga.StatRegDisplayIdRd,              "VMSVGA/Reg/DisplayIdRead",              "SVGA_REG_DISPLAY_ID reads.");
    REG_CNT(&pThis->svga.StatRegDisplayIsPrimaryRd,       "VMSVGA/Reg/DisplayIsPrimaryRead",       "SVGA_REG_DISPLAY_IS_PRIMARY reads.");
    REG_CNT(&pThis->svga.StatRegDisplayPositionXRd,       "VMSVGA/Reg/DisplayPositionXRead",       "SVGA_REG_DISPLAY_POSITION_X reads.");
    REG_CNT(&pThis->svga.StatRegDisplayPositionYRd,       "VMSVGA/Reg/DisplayPositionYRead",       "SVGA_REG_DISPLAY_POSITION_Y reads.");
    REG_CNT(&pThis->svga.StatRegDisplayWidthRd,           "VMSVGA/Reg/DisplayWidthRead",           "SVGA_REG_DISPLAY_WIDTH reads.");
    REG_CNT(&pThis->svga.StatRegEnableRd,                 "VMSVGA/Reg/EnableRead",                 "SVGA_REG_ENABLE reads.");
    REG_CNT(&pThis->svga.StatRegFbOffsetRd,               "VMSVGA/Reg/FbOffsetRead",               "SVGA_REG_FB_OFFSET reads.");
    REG_CNT(&pThis->svga.StatRegFbSizeRd,                 "VMSVGA/Reg/FbSizeRead",                 "SVGA_REG_FB_SIZE reads.");
    REG_CNT(&pThis->svga.StatRegFbStartRd,                "VMSVGA/Reg/FbStartRead",                "SVGA_REG_FB_START reads.");
    REG_CNT(&pThis->svga.StatRegGmrIdRd,                  "VMSVGA/Reg/GmrIdRead",                  "SVGA_REG_GMR_ID reads.");
    REG_CNT(&pThis->svga.StatRegGmrMaxDescriptorLengthRd, "VMSVGA/Reg/GmrMaxDescriptorLengthRead", "SVGA_REG_GMR_MAX_DESCRIPTOR_LENGTH reads.");
    REG_CNT(&pThis->svga.StatRegGmrMaxIdsRd,              "VMSVGA/Reg/GmrMaxIdsRead",              "SVGA_REG_GMR_MAX_IDS reads.");
    REG_CNT(&pThis->svga.StatRegGmrsMaxPagesRd,           "VMSVGA/Reg/GmrsMaxPagesRead",           "SVGA_REG_GMRS_MAX_PAGES reads.");
    REG_CNT(&pThis->svga.StatRegGreenMaskRd,              "VMSVGA/Reg/GreenMaskRead",              "SVGA_REG_GREEN_MASK reads.");
    REG_CNT(&pThis->svga.StatRegGuestIdRd,                "VMSVGA/Reg/GuestIdRead",                "SVGA_REG_GUEST_ID reads.");
    REG_CNT(&pThis->svga.StatRegHeightRd,                 "VMSVGA/Reg/HeightRead",                 "SVGA_REG_HEIGHT reads.");
    REG_CNT(&pThis->svga.StatRegHostBitsPerPixelRd,       "VMSVGA/Reg/HostBitsPerPixelRead",       "SVGA_REG_HOST_BITS_PER_PIXEL reads.");
    REG_CNT(&pThis->svga.StatRegIdRd,                     "VMSVGA/Reg/IdRead",                     "SVGA_REG_ID reads.");
    REG_CNT(&pThis->svga.StatRegIrqMaskRd,                "VMSVGA/Reg/IrqMaskRead",                "SVGA_REG_IRQ_MASK reads.");
    REG_CNT(&pThis->svga.StatRegMaxHeightRd,              "VMSVGA/Reg/MaxHeightRead",              "SVGA_REG_MAX_HEIGHT reads.");
    REG_CNT(&pThis->svga.StatRegMaxWidthRd,               "VMSVGA/Reg/MaxWidthRead",               "SVGA_REG_MAX_WIDTH reads.");
    REG_CNT(&pThis->svga.StatRegMemorySizeRd,             "VMSVGA/Reg/MemorySizeRead",             "SVGA_REG_MEMORY_SIZE reads.");
    REG_CNT(&pThis->svga.StatRegMemRegsRd,                "VMSVGA/Reg/MemRegsRead",                "SVGA_REG_MEM_REGS reads.");
    REG_CNT(&pThis->svga.StatRegMemSizeRd,                "VMSVGA/Reg/MemSizeRead",                "SVGA_REG_MEM_SIZE reads.");
    REG_CNT(&pThis->svga.StatRegMemStartRd,               "VMSVGA/Reg/MemStartRead",               "SVGA_REG_MEM_START reads.");
    REG_CNT(&pThis->svga.StatRegNumDisplaysRd,            "VMSVGA/Reg/NumDisplaysRead",            "SVGA_REG_NUM_DISPLAYS reads.");
    REG_CNT(&pThis->svga.StatRegNumGuestDisplaysRd,       "VMSVGA/Reg/NumGuestDisplaysRead",       "SVGA_REG_NUM_GUEST_DISPLAYS reads.");
    REG_CNT(&pThis->svga.StatRegPaletteRd,                "VMSVGA/Reg/PaletteRead",                "SVGA_REG_PLAETTE_XXXX reads.");
    REG_CNT(&pThis->svga.StatRegPitchLockRd,              "VMSVGA/Reg/PitchLockRead",              "SVGA_REG_PITCHLOCK reads.");
    REG_CNT(&pThis->svga.StatRegPsuedoColorRd,            "VMSVGA/Reg/PsuedoColorRead",            "SVGA_REG_PSEUDOCOLOR reads.");
    REG_CNT(&pThis->svga.StatRegRedMaskRd,                "VMSVGA/Reg/RedMaskRead",                "SVGA_REG_RED_MASK reads.");
    REG_CNT(&pThis->svga.StatRegScratchRd,                "VMSVGA/Reg/ScratchRead",                "SVGA_REG_SCRATCH reads.");
    REG_CNT(&pThis->svga.StatRegScratchSizeRd,            "VMSVGA/Reg/ScratchSizeRead",            "SVGA_REG_SCRATCH_SIZE reads.");
    REG_CNT(&pThis->svga.StatRegSyncRd,                   "VMSVGA/Reg/SyncRead",                   "SVGA_REG_SYNC reads.");
    REG_CNT(&pThis->svga.StatRegTopRd,                    "VMSVGA/Reg/TopRead",                    "SVGA_REG_TOP reads.");
    REG_CNT(&pThis->svga.StatRegTracesRd,                 "VMSVGA/Reg/TracesRead",                 "SVGA_REG_TRACES reads.");
    REG_CNT(&pThis->svga.StatRegUnknownRd,                "VMSVGA/Reg/UnknownRead",                "SVGA_REG_UNKNOWN reads.");
    REG_CNT(&pThis->svga.StatRegVramSizeRd,               "VMSVGA/Reg/VramSizeRead",               "SVGA_REG_VRAM_SIZE reads.");
    REG_CNT(&pThis->svga.StatRegWidthRd,                  "VMSVGA/Reg/WidthRead",                  "SVGA_REG_WIDTH reads.");
    REG_CNT(&pThis->svga.StatRegWriteOnlyRd,              "VMSVGA/Reg/WriteOnlyRead",              "Write-only SVGA_REG_XXXX reads.");
    REG_CNT(&pThis->svga.StatRegCommandLowRd,             "VMSVGA/Reg/CommandLowRead",             "SVGA_REG_COMMAND_LOW reads.");
    REG_CNT(&pThis->svga.StatRegCommandHighRd,            "VMSVGA/Reg/CommandHighRead",            "SVGA_REG_COMMAND_HIGH reads.");
    REG_CNT(&pThis->svga.StatRegMaxPrimBBMemRd,           "VMSVGA/Reg/MaxPrimBBMemRead",           "SVGA_REG_MAX_PRIMARY_BOUNDING_BOX_MEM reads.");
    REG_CNT(&pThis->svga.StatRegGBMemSizeRd,              "VMSVGA/Reg/GBMemSizeRead",              "SVGA_REG_SUGGESTED_GBOBJECT_MEM_SIZE_KB reads.");
    REG_CNT(&pThis->svga.StatRegDevCapRd,                 "VMSVGA/Reg/DevCapRead",                 "SVGA_REG_DEV_CAP reads.");
    REG_CNT(&pThis->svga.StatRegCmdPrependLowRd,          "VMSVGA/Reg/CmdPrependLowRead",          "SVGA_REG_CMD_PREPEND_LOW reads.");
    REG_CNT(&pThis->svga.StatRegCmdPrependHighRd,         "VMSVGA/Reg/CmdPrependHighRead",         "SVGA_REG_CMD_PREPEND_HIGH reads.");
    REG_CNT(&pThis->svga.StatRegScrnTgtMaxWidthRd,        "VMSVGA/Reg/ScrnTgtMaxWidthRead",        "SVGA_REG_SCREENTARGET_MAX_WIDTH reads.");
    REG_CNT(&pThis->svga.StatRegScrnTgtMaxHeightRd,       "VMSVGA/Reg/ScrnTgtMaxHeightRead",       "SVGA_REG_SCREENTARGET_MAX_HEIGHT reads.");
    REG_CNT(&pThis->svga.StatRegMobMaxSizeRd,             "VMSVGA/Reg/MobMaxSizeRead",             "SVGA_REG_MOB_MAX_SIZE reads.");

    REG_PRF(&pSVGAState->StatBusyDelayEmts,               "VMSVGA/EmtDelayOnBusyFifo",             "Time we've delayed EMTs because of busy FIFO thread.");
    REG_CNT(&pSVGAState->StatFifoCommands,                "VMSVGA/FifoCommands",                   "FIFO command counter.");
    REG_CNT(&pSVGAState->StatFifoErrors,                  "VMSVGA/FifoErrors",                     "FIFO error counter.");
    REG_CNT(&pSVGAState->StatFifoUnkCmds,                 "VMSVGA/FifoUnknownCommands",            "FIFO unknown command counter.");
    REG_CNT(&pSVGAState->StatFifoTodoTimeout,             "VMSVGA/FifoTodoTimeout",                "Number of times we discovered pending work after a wait timeout.");
    REG_CNT(&pSVGAState->StatFifoTodoWoken,               "VMSVGA/FifoTodoWoken",                  "Number of times we discovered pending work after being woken up.");
    REG_PRF(&pSVGAState->StatFifoStalls,                  "VMSVGA/FifoStalls",                     "Profiling of FIFO stalls (waiting for guest to finish copying data).");
    REG_PRF(&pSVGAState->StatFifoExtendedSleep,           "VMSVGA/FifoExtendedSleep",              "Profiling FIFO sleeps relying on the refresh timer and/or access handler.");
# if defined(VMSVGA_USE_FIFO_ACCESS_HANDLER) || defined(DEBUG_FIFO_ACCESS)
    REG_CNT(&pSVGAState->StatFifoAccessHandler,           "VMSVGA/FifoAccessHandler",              "Number of times the FIFO access handler triggered.");
# endif
    REG_CNT(&pSVGAState->StatFifoCursorFetchAgain,        "VMSVGA/FifoCursorFetchAgain",           "Times the cursor update counter changed while reading.");
    REG_CNT(&pSVGAState->StatFifoCursorNoChange,          "VMSVGA/FifoCursorNoChange",             "No cursor position change event though the update counter was modified.");
    REG_CNT(&pSVGAState->StatFifoCursorPosition,          "VMSVGA/FifoCursorPosition",             "Cursor position and visibility changes.");
    REG_CNT(&pSVGAState->StatFifoCursorVisiblity,         "VMSVGA/FifoCursorVisiblity",            "Cursor visibility changes.");
    REG_CNT(&pSVGAState->StatFifoWatchdogWakeUps,         "VMSVGA/FifoWatchdogWakeUps",            "Number of times the FIFO refresh poller/watchdog woke up the FIFO thread.");

# undef REG_CNT
# undef REG_PRF

    /*
     * Info handlers.
     */
    PDMDevHlpDBGFInfoRegister(pDevIns, "vmsvga", "Basic VMSVGA device state details", vmsvgaR3Info);
# ifdef VBOX_WITH_VMSVGA3D
    PDMDevHlpDBGFInfoRegister(pDevIns, "vmsvga3dctx", "VMSVGA 3d context details. Accepts 'terse'.", vmsvgaR3Info3dContext);
    PDMDevHlpDBGFInfoRegister(pDevIns, "vmsvga3dsfc",
                              "VMSVGA 3d surface details. "
                              "Accepts 'terse', 'invy', and one of 'tiny', 'medium', 'normal', 'big', 'huge', or 'gigantic'.",
                              vmsvgaR3Info3dSurface);
    PDMDevHlpDBGFInfoRegister(pDevIns, "vmsvga3dsurf",
                              "VMSVGA 3d surface details and bitmap: "
                              "sid[>dir]",
                              vmsvgaR3Info3dSurfaceBmp);
# endif

    return VINF_SUCCESS;
}

/* Initialize 3D backend, set device capabilities and call pfnPowerOn callback of 3D backend.
 *
 * @param   pDevIns    The device instance.
 * @param   pThis      The shared VGA/VMSVGA instance data.
 * @param   pThisCC    The VGA/VMSVGA state for ring-3.
 * @param   fLoadState Whether saved state is being loaded.
 */
static void vmsvgaR3PowerOnDevice(PPDMDEVINS pDevIns, PVGASTATE pThis, PVGASTATECC pThisCC, bool fLoadState)
{
# ifdef VBOX_WITH_VMSVGA3D
    if (pThis->svga.f3DEnabled || pThis->svga.fVMSVGA2dGBO)
    {
        /* Load a 3D backend. */
        int rc = vmsvgaR3Init3dInterfaces(pDevIns, pThis, pThisCC);
        if (RT_FAILURE(rc))
        {
            LogRel(("VMSVGA3d: 3D support disabled! (vmsvga3dInit -> %Rrc)\n", rc));
            pThis->svga.f3DEnabled = false;
        }
    }
# endif

# if defined(VBOX_WITH_VMSVGA3D) && defined(RT_OS_LINUX)
    if (pThis->svga.f3DEnabled)
    {
        /* The FIFO thread may use X API for accelerated screen output. */
        /* This must be done after backend initialization by vmsvgaR3Init3dInterfaces,
         * because it dynamically resolves XInitThreads.
         */
        XInitThreads();
    }
# endif

    if (!fLoadState)
    {
        if (!pThis->fVmSvga3)
            vmsvgaR3InitFIFO(pThis, pThisCC);
        vmsvgaR3GetCaps(pThis, pThisCC, &pThis->svga.u32DeviceCaps, &pThis->svga.u32DeviceCaps2, &pThisCC->svga.pau32FIFO[SVGA_FIFO_CAPABILITIES]);
    }
# ifdef DEBUG
    else
    {
        /* If saved state is being loaded then FIFO and caps are already restored. */
        uint32_t u32DeviceCaps = 0;
        uint32_t u32DeviceCaps2 = 0;
        uint32_t u32FIFOCaps = 0;
        vmsvgaR3GetCaps(pThis, pThisCC, &u32DeviceCaps, &u32DeviceCaps2, &u32FIFOCaps);

        /* Capabilities should not change normally.
         * However the saved state might have a subset of currently implemented caps.
         */
        Assert(   (pThis->svga.u32DeviceCaps & u32DeviceCaps) == pThis->svga.u32DeviceCaps
               && (pThis->svga.u32DeviceCaps2 & u32DeviceCaps2) == pThis->svga.u32DeviceCaps2
               && (   pThis->fVmSvga3
                   || (pThisCC->svga.pau32FIFO[SVGA_FIFO_CAPABILITIES] & u32FIFOCaps) == pThisCC->svga.pau32FIFO[SVGA_FIFO_CAPABILITIES]));
    }
#endif

# ifdef VBOX_WITH_VMSVGA3D
    if (pThis->svga.f3DEnabled || pThis->svga.fVMSVGA2dGBO)
    {
        int rc = VINF_SUCCESS;

        PVMSVGAR3STATE pSVGAState = pThisCC->svga.pSvgaR3State;
        if (pSVGAState->pFuncs3D)
            rc = pSVGAState->pFuncs3D->pfnPowerOn(pDevIns, pThis, pThisCC);

        if (RT_SUCCESS(rc))
        {
            /* Initialize 3D capabilities. */
            vmsvgaR3Init3DCaps(pThis, pThisCC);
            if (!pThis->fVmSvga3)
                vmsvgaR3InitFIFO3D(pThis, pThisCC);
        }
        else
        {
            LogRel(("VMSVGA3d: 3D support disabled! (vmsvga3dPowerOn -> %Rrc)\n", rc));
            pThis->svga.f3DEnabled = false;
        }
    }
# else  /* !VBOX_WITH_VMSVGA3D */
    RT_NOREF(pDevIns);
# endif /* !VBOX_WITH_VMSVGA3D */
}


/**
 * Power On notification.
 *
 * @param   pDevIns     The device instance data.
 *
 * @remarks Caller enters the device critical section.
 */
DECLCALLBACK(void) vmsvgaR3PowerOn(PPDMDEVINS pDevIns)
{
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);

    vmsvgaR3PowerOnDevice(pDevIns, pThis, pThisCC, /*fLoadState=*/ false);
}

/**
 * Power Off notification.
 *
 * @param   pDevIns     The device instance data.
 *
 * @remarks Caller enters the device critical section.
 */
DECLCALLBACK(void) vmsvgaR3PowerOff(PPDMDEVINS pDevIns)
{
    PVGASTATE   pThis   = PDMDEVINS_2_DATA(pDevIns, PVGASTATE);
    PVGASTATECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVGASTATECC);

    /*
     * Notify the FIFO thread.
     */
    if (pThisCC->svga.pFIFOIOThread)
    {
        /* Hack around a deadlock:
         * - the caller holds the device critsect;
         * - FIFO thread may attempt to enter the critsect too (when raising an IRQ).
         */
        PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);

        int rc = vmsvgaR3RunExtCmdOnFifoThread(pDevIns, pThis, pThisCC,  VMSVGA_FIFO_EXTCMD_POWEROFF,
                                               NULL /*pvParam*/, 30000 /*ms*/);
        AssertLogRelRC(rc);

        int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
        PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSect, rcLock);
    }
}

#endif /* IN_RING3 */
