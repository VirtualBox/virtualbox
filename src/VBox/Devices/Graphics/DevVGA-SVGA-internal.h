/* $Id: DevVGA-SVGA-internal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VMWare SVGA device - internal header for DevVGA-SVGA* source files.
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

#ifndef VBOX_INCLUDED_SRC_Graphics_DevVGA_SVGA_internal_h
#define VBOX_INCLUDED_SRC_Graphics_DevVGA_SVGA_internal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/*
 * Assert sane compilation environment.
 */
#ifndef IN_RING3
# error "DevVGA-SVGA-internal.h is only for ring-3 code"
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * 64-bit GMR descriptor.
 */
typedef struct
{
   RTGCPHYS GCPhys;
   uint64_t numPages;
} VMSVGAGMRDESCRIPTOR, *PVMSVGAGMRDESCRIPTOR;

/**
 * GMR slot
 */
typedef struct
{
    uint32_t                cMaxPages;
    uint32_t                cbTotal;
    uint32_t                numDescriptors;
    PVMSVGAGMRDESCRIPTOR    paDesc;
} GMR, *PGMR;


typedef struct VMSVGACMDBUF *PVMSVGACMDBUF;
typedef struct VMSVGACMDBUFCTX *PVMSVGACMDBUFCTX;

/* Command buffer. */
typedef struct VMSVGACMDBUF
{
    RTLISTNODE nodeBuffer;
    /* Context of the buffer. */
    PVMSVGACMDBUFCTX pCmdBufCtx;
    /* PA of the buffer. */
    RTGCPHYS GCPhysCB;
    /* A copy of the buffer header. */
    SVGACBHeader hdr;
    /* A copy of the commands. Size of the memory buffer is hdr.length */
    void *pvCommands;
} VMSVGACMDBUF;

/* Command buffer context. */
typedef struct VMSVGACMDBUFCTX
{
    /* Buffers submitted to processing for the FIFO thread. */
    RTLISTANCHOR listSubmitted;
    /* How many buffers in the queue. */
    uint32_t cSubmitted;
} VMSVGACMDBUFCTX;

/**
 * Internal SVGA ring-3 only state.
 */
typedef struct VMSVGAR3STATE
{
    PPDMDEVINS              pDevIns; /* Stored here to use with PDMDevHlp* */
    GMR                    *paGMR; // [VMSVGAState::cGMR]
    struct
    {
        SVGAGuestPtr RT_UNTRUSTED_GUEST         ptr;
        uint32_t RT_UNTRUSTED_GUEST             bytesPerLine;
        SVGAGMRImageFormat RT_UNTRUSTED_GUEST   format;
    } GMRFB;
    struct
    {
        bool                fActive;
        uint32_t            xHotspot;
        uint32_t            yHotspot;
        uint32_t            width;
        uint32_t            height;
        uint32_t            cbData;
        void               *pData;
    } Cursor;
    SVGAColorBGRX           colorAnnotation;

# ifdef VMSVGA_USE_EMT_HALT_CODE
    /** Number of EMTs in BusyDelayedEmts (quicker than scanning the set). */
    uint32_t volatile       cBusyDelayedEmts;
    /** Set of EMTs that are   */
    VMCPUSET                BusyDelayedEmts;
# else
    /** Number of EMTs waiting on hBusyDelayedEmts. */
    uint32_t volatile       cBusyDelayedEmts;
    /** Semaphore that EMTs wait on when reading SVGA_REG_BUSY and the FIFO is
     *  busy (ugly).  */
    RTSEMEVENTMULTI         hBusyDelayedEmts;
# endif

    /** Information about screens. */
    VMSVGASCREENOBJECT      aScreens[64];

    /** Command buffer contexts. */
    PVMSVGACMDBUFCTX        apCmdBufCtxs[SVGA_CB_CONTEXT_MAX];
    /** The special Device Context for synchronous commands. */
    VMSVGACMDBUFCTX         CmdBufCtxDC;
    /** Flag which indicates that there are buffers to be processed. */
    uint32_t volatile       fCmdBuf;
    /** Critical section for accessing the command buffer data. */
    RTCRITSECT              CritSectCmdBuf;

    /** Object Tables: MOBs, etc. see SVGA_OTABLE_* */
    VMSVGAGBO               aGboOTables[SVGA_OTABLE_MAX];

    /** Tree of guest's Memory OBjects. Key is mobid. */
    AVLU32TREE              MOBTree;
    /** Least Recently Used list of MOBs.
     * To unmap older MOBs when the guest exceeds SVGA_REG_SUGGESTED_GBOBJECT_MEM_SIZE_KB (SVGA_REG_GBOBJECT_MEM_SIZE_KB) value. */
    RTLISTANCHOR            MOBLRUList;

# ifdef VBOX_WITH_VMSVGA3D
#  ifdef VMSVGA3D_DX
    /** DX context of the currently processed command buffer */
    uint32_t                 idDXContextCurrent;
    uint32_t                 u32Reserved;
#  endif
    VMSVGA3DBACKENDFUNCS3D  *pFuncs3D;
    VMSVGA3DBACKENDFUNCSVGPU9 *pFuncsVGPU9;
    VMSVGA3DBACKENDFUNCSMAP *pFuncsMap;
    VMSVGA3DBACKENDFUNCSGBO *pFuncsGBO;
    VMSVGA3DBACKENDFUNCSDX  *pFuncsDX;
    VMSVGA3DBACKENDFUNCSDXVIDEO *pFuncsDXVideo;
# endif

    /** Tracks how much time we waste reading SVGA_REG_BUSY with a busy FIFO. */
    STAMPROFILE             StatBusyDelayEmts;

    STAMPROFILE             StatR3Cmd3dPresentProf;
    STAMPROFILE             StatR3Cmd3dDrawPrimitivesProf;
    STAMPROFILE             StatR3Cmd3dSurfaceDmaProf;
    STAMPROFILE             StatR3Cmd3dBlitSurfaceToScreenProf;
    STAMCOUNTER             StatR3CmdDefineGmr2;
    STAMCOUNTER             StatR3CmdDefineGmr2Free;
    STAMCOUNTER             StatR3CmdDefineGmr2Modify;
    STAMCOUNTER             StatR3CmdRemapGmr2;
    STAMCOUNTER             StatR3CmdRemapGmr2Modify;
    STAMCOUNTER             StatR3CmdInvalidCmd;
    STAMCOUNTER             StatR3CmdFence;
    STAMCOUNTER             StatR3CmdUpdate;
    STAMCOUNTER             StatR3CmdUpdateVerbose;
    STAMCOUNTER             StatR3CmdDefineCursor;
    STAMCOUNTER             StatR3CmdDefineAlphaCursor;
    STAMCOUNTER             StatR3CmdMoveCursor;
    STAMCOUNTER             StatR3CmdDisplayCursor;
    STAMCOUNTER             StatR3CmdRectFill;
    STAMCOUNTER             StatR3CmdRectCopy;
    STAMCOUNTER             StatR3CmdRectRopCopy;
    STAMCOUNTER             StatR3CmdEscape;
    STAMCOUNTER             StatR3CmdDefineScreen;
    STAMCOUNTER             StatR3CmdDestroyScreen;
    STAMCOUNTER             StatR3CmdDefineGmrFb;
    STAMCOUNTER             StatR3CmdBlitGmrFbToScreen;
    STAMCOUNTER             StatR3CmdBlitScreentoGmrFb;
    STAMCOUNTER             StatR3CmdAnnotationFill;
    STAMCOUNTER             StatR3CmdAnnotationCopy;
    STAMCOUNTER             StatR3Cmd3dSurfaceDefine;
    STAMCOUNTER             StatR3Cmd3dSurfaceDefineV2;
    STAMCOUNTER             StatR3Cmd3dSurfaceDestroy;
    STAMCOUNTER             StatR3Cmd3dSurfaceCopy;
    STAMCOUNTER             StatR3Cmd3dSurfaceStretchBlt;
    STAMCOUNTER             StatR3Cmd3dSurfaceDma;
    STAMCOUNTER             StatR3Cmd3dSurfaceScreen;
    STAMCOUNTER             StatR3Cmd3dContextDefine;
    STAMCOUNTER             StatR3Cmd3dContextDestroy;
    STAMCOUNTER             StatR3Cmd3dSetTransform;
    STAMCOUNTER             StatR3Cmd3dSetZRange;
    STAMCOUNTER             StatR3Cmd3dSetRenderState;
    STAMCOUNTER             StatR3Cmd3dSetRenderTarget;
    STAMCOUNTER             StatR3Cmd3dSetTextureState;
    STAMCOUNTER             StatR3Cmd3dSetMaterial;
    STAMCOUNTER             StatR3Cmd3dSetLightData;
    STAMCOUNTER             StatR3Cmd3dSetLightEnable;
    STAMCOUNTER             StatR3Cmd3dSetViewPort;
    STAMCOUNTER             StatR3Cmd3dSetClipPlane;
    STAMCOUNTER             StatR3Cmd3dClear;
    STAMCOUNTER             StatR3Cmd3dPresent;
    STAMCOUNTER             StatR3Cmd3dPresentReadBack;
    STAMCOUNTER             StatR3Cmd3dShaderDefine;
    STAMCOUNTER             StatR3Cmd3dShaderDestroy;
    STAMCOUNTER             StatR3Cmd3dSetShader;
    STAMCOUNTER             StatR3Cmd3dSetShaderConst;
    STAMCOUNTER             StatR3Cmd3dDrawPrimitives;
    STAMCOUNTER             StatR3Cmd3dSetScissorRect;
    STAMCOUNTER             StatR3Cmd3dBeginQuery;
    STAMCOUNTER             StatR3Cmd3dEndQuery;
    STAMCOUNTER             StatR3Cmd3dWaitForQuery;
    STAMCOUNTER             StatR3Cmd3dGenerateMipmaps;
    STAMCOUNTER             StatR3Cmd3dActivateSurface;
    STAMCOUNTER             StatR3Cmd3dDeactivateSurface;

    STAMCOUNTER             StatR3RegConfigDoneWr;
    STAMCOUNTER             StatR3RegGmrDescriptorWr;
    STAMCOUNTER             StatR3RegGmrDescriptorWrErrors;
    STAMCOUNTER             StatR3RegGmrDescriptorWrFree;

    STAMCOUNTER             StatFifoCommands;
    STAMCOUNTER             StatFifoErrors;
    STAMCOUNTER             StatFifoUnkCmds;
    STAMCOUNTER             StatFifoTodoTimeout;
    STAMCOUNTER             StatFifoTodoWoken;
    STAMPROFILE             StatFifoStalls;
    STAMPROFILE             StatFifoExtendedSleep;
# ifdef VMSVGA_USE_FIFO_ACCESS_HANDLER
    STAMCOUNTER             StatFifoAccessHandler;
# endif
    STAMCOUNTER             StatFifoCursorFetchAgain;
    STAMCOUNTER             StatFifoCursorNoChange;
    STAMCOUNTER             StatFifoCursorPosition;
    STAMCOUNTER             StatFifoCursorVisiblity;
    STAMCOUNTER             StatFifoWatchdogWakeUps;
} VMSVGAR3STATE, *PVMSVGAR3STATE;


/*********************************************************************************************************************************
*   Functions                                                                                                                    *
*********************************************************************************************************************************/
#ifdef DEBUG_GMR_ACCESS
DECLCALLBACK(int) vmsvgaR3ResetGmrHandlers(PVGASTATE pThis);
DECLCALLBACK(int) vmsvgaR3DeregisterGmr(PPDMDEVINS pDevIns, uint32_t gmrId);
#endif

int vmsvgaR3DestroyScreen(PVGASTATE pThis, PVGASTATECC pThisCC, VMSVGASCREENOBJECT *pScreen);

void vmsvgaR3ResetScreens(PVGASTATE pThis, PVGASTATECC pThisCC);
void vmsvgaR3ResetSvgaState(PVGASTATE pThis, PVGASTATECC pThisCC);

void vmsvgaR3TerminateSvgaState(PVGASTATE pThis, PVGASTATECC pThisCC);

int vmsvgaR3ChangeMode(PVGASTATE pThis, PVGASTATECC pThisCC);
int vmsvgaR3UpdateScreen(PVGASTATECC pThisCC, VMSVGASCREENOBJECT *pScreen, int x, int y, int w, int h);

int vmsvgaR3GmrTransfer(PVGASTATE pThis, PVGASTATECC pThisCC, const SVGA3dTransferType enmTransferType,
                        uint8_t *pbHstBuf, uint32_t cbHstBuf, uint32_t offHst, int32_t cbHstPitch,
                        SVGAGuestPtr gstPtr, uint32_t offGst, int32_t cbGstPitch,
                        uint32_t cbWidth, uint32_t cHeight);
void vmsvgaR3GmrFree(PVGASTATECC pThisCC, uint32_t idGMR);

void vmsvgaR3CmdUpdate(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdUpdate const *pCmd);
void vmsvgaR3CmdUpdateVerbose(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdUpdateVerbose const *pCmd);
void vmsvgaR3CmdRectFill(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdRectFill const *pCmd);
void vmsvgaR3CmdRectCopy(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdRectCopy const *pCmd);
void vmsvgaR3CmdRectRopCopy(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdRectRopCopy const *pCmd);
void vmsvgaR3CmdDisplayCursor(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdDisplayCursor const *pCmd);
void vmsvgaR3CmdMoveCursor(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdMoveCursor const *pCmd);
void vmsvgaR3CmdDefineCursor(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdDefineCursor const *pCmd);
void vmsvgaR3CmdDefineAlphaCursor(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdDefineAlphaCursor const *pCmd);
void vmsvgaR3CmdEscape(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdEscape const *pCmd);
void vmsvgaR3CmdDefineScreen(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdDefineScreen const *pCmd);
void vmsvgaR3CmdDestroyScreen(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdDestroyScreen const *pCmd);
void vmsvgaR3CmdDefineGMRFB(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdDefineGMRFB const *pCmd);
void vmsvgaR3CmdBlitGMRFBToScreen(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdBlitGMRFBToScreen const *pCmd);
void vmsvgaR3CmdBlitScreenToGMRFB(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdBlitScreenToGMRFB const *pCmd);
void vmsvgaR3CmdAnnotationFill(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdAnnotationFill const *pCmd);
void vmsvgaR3CmdAnnotationCopy(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdAnnotationCopy const *pCmd);

#ifdef VBOX_WITH_VMSVGA3D
void vmsvgaR3CmdDefineGMR2(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdDefineGMR2 const *pCmd);
void vmsvgaR3CmdRemapGMR2(PVGASTATE pThis, PVGASTATECC pThisCC, SVGAFifoCmdRemapGMR2 const *pCmd);
int vmsvgaR3Process3dCmd(PVGASTATE pThis, PVGASTATECC pThisCC, uint32_t idDXContext, SVGAFifo3dCmdId enmCmdId, uint32_t cbCmd, void const *pvCmd);
#endif

#if defined(LOG_ENABLED) || defined(VBOX_STRICT)
const char *vmsvgaR3FifoCmdToString(uint32_t u32Cmd);
#endif


#endif /* !VBOX_INCLUDED_SRC_Graphics_DevVGA_SVGA_internal_h */
