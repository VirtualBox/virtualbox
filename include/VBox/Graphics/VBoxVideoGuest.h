/* $Id: VBoxVideoGuest.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Host Guest Shared Memory Interface (HGSMI) - OS-independent guest structures.
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

#ifndef VBOX_INCLUDED_Graphics_VBoxVideoGuest_h
#define VBOX_INCLUDED_Graphics_VBoxVideoGuest_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VBoxVideoIPRT.h"
#include "HGSMIBase.h"
#include "VBoxVideo.h"

RT_C_DECLS_BEGIN

/**
 * Structure grouping the context needed for sending graphics acceleration
 * information to the host via VBVA.  Each screen has its own VBVA buffer.
 */
typedef struct VBVABUFFERCONTEXT
{
    /** Offset of the buffer in the VRAM section for the screen */
    uint32_t    offVRAMBuffer;
    /** Length of the buffer in bytes */
    uint32_t    cbBuffer;
    /** This flag is set if we wrote to the buffer faster than the host could
     * read it. */
    bool        fHwBufferOverflow;
    /** The VBVA record that we are currently preparing for the host, NULL if
     * none. */
    struct VBVARECORD *pRecord;
    /** Pointer to the VBVA buffer mapped into the current address space.  Will
     * be NULL if VBVA is not enabled. */
    struct VBVABUFFER *pVBVA;
} VBVABUFFERCONTEXT, *PVBVABUFFERCONTEXT;

/** @name Base HGSMI APIs
 * @{ */

DECLHIDDEN(bool)     VBoxHGSMIIsSupported(void);
DECLHIDDEN(void)     VBoxHGSMIGetBaseMappingInfo(uint32_t cbVRAM,
                                                 uint32_t *poffVRAMBaseMapping,
                                                 uint32_t *pcbMapping,
                                                 uint32_t *poffGuestHeapMemory,
                                                 uint32_t *pcbGuestHeapMemory,
                                                 uint32_t *poffHostFlags);
DECLHIDDEN(int)      VBoxHGSMIReportFlagsLocation(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                                  HGSMIOFFSET offLocation);
DECLHIDDEN(int)      VBoxHGSMISendCapsInfo(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                           uint32_t fCaps);
DECLHIDDEN(void)     VBoxHGSMIGetHostAreaMapping(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                                 uint32_t cbVRAM,
                                                 uint32_t offVRAMBaseMapping,
                                                 uint32_t *poffVRAMHostArea,
                                                 uint32_t *pcbHostArea);
DECLHIDDEN(int)      VBoxHGSMISendHostCtxInfo(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                              HGSMIOFFSET offVRAMFlagsLocation,
                                              uint32_t fCaps,
                                              uint32_t offVRAMHostArea,
                                              uint32_t cbHostArea);
DECLHIDDEN(int)      VBoxQueryConfHGSMI(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                        uint32_t u32Index, uint32_t *pulValue);
DECLHIDDEN(int)      VBoxQueryConfHGSMIDef(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                           uint32_t u32Index, uint32_t u32DefValue, uint32_t *pulValue);
DECLHIDDEN(int)      VBoxHGSMIUpdatePointerShape(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                                 uint32_t fFlags,
                                                 uint32_t cHotX,
                                                 uint32_t cHotY,
                                                 uint32_t cWidth,
                                                 uint32_t cHeight,
                                                 uint8_t *pPixels,
                                                 uint32_t cbLength);
DECLHIDDEN(int)      VBoxHGSMIReportCursorPosition(PHGSMIGUESTCOMMANDCONTEXT pCtx, bool fReportPosition, uint32_t x, uint32_t y,
                                                   uint32_t *pxHost, uint32_t *pyHost);

/** @}  */

/** @name VBVA APIs
 * @{ */
DECLHIDDEN(bool) VBoxVBVAEnable(PVBVABUFFERCONTEXT pCtx,
                                PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx,
                                struct VBVABUFFER *pVBVA, int32_t cScreen);
DECLHIDDEN(void) VBoxVBVADisable(PVBVABUFFERCONTEXT pCtx,
                                 PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx,
                                 int32_t cScreen);
DECLHIDDEN(bool) VBoxVBVABufferBeginUpdate(PVBVABUFFERCONTEXT pCtx,
                                           PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx);
DECLHIDDEN(void) VBoxVBVABufferEndUpdate(PVBVABUFFERCONTEXT pCtx);
DECLHIDDEN(bool) VBoxVBVAWrite(PVBVABUFFERCONTEXT pCtx,
                               PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx,
                               const void *pv, uint32_t cb);
DECLHIDDEN(bool) VBoxVBVAOrderSupported(PVBVABUFFERCONTEXT pCtx, unsigned code);
DECLHIDDEN(void) VBoxVBVASetupBufferContext(PVBVABUFFERCONTEXT pCtx,
                                            uint32_t offVRAMBuffer,
                                            uint32_t cbBuffer);

/** @}  */

/** @name Modesetting APIs
 * @{ */

DECLHIDDEN(uint32_t) VBoxHGSMIGetMonitorCount(PHGSMIGUESTCOMMANDCONTEXT pCtx);
DECLHIDDEN(bool)     VBoxVGACfgAvailable(void);
DECLHIDDEN(bool)     VBoxVGACfgQuery(uint16_t u16Id, uint32_t *pu32Value, uint32_t u32DefValue);
DECLHIDDEN(uint32_t) VBoxVideoGetVRAMSize(void);
DECLHIDDEN(bool)     VBoxVideoAnyWidthAllowed(void);
DECLHIDDEN(uint16_t) VBoxHGSMIGetScreenFlags(PHGSMIGUESTCOMMANDCONTEXT pCtx);

struct VBVAINFOVIEW;
/**
 * Callback funtion called from @a VBoxHGSMISendViewInfo to initialise
 * the @a VBVAINFOVIEW structure for each screen.
 *
 * @returns  iprt status code
 * @param  pvData  context data for the callback, passed to @a
 *                 VBoxHGSMISendViewInfo along with the callback
 * @param  pInfo   array of @a VBVAINFOVIEW structures to be filled in
 * @todo  explicitly pass the array size
 */
typedef DECLCALLBACKTYPE(int, FNHGSMIFILLVIEWINFO,(void *pvData, struct VBVAINFOVIEW *pInfo, uint32_t cViews));
/** Pointer to a FNHGSMIFILLVIEWINFO callback */
typedef FNHGSMIFILLVIEWINFO *PFNHGSMIFILLVIEWINFO;

DECLHIDDEN(int)      VBoxHGSMISendViewInfo(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                           uint32_t u32Count,
                                           PFNHGSMIFILLVIEWINFO pfnFill,
                                           void *pvData);
DECLHIDDEN(void)     VBoxVideoSetModeRegisters(uint16_t cWidth, uint16_t cHeight,
                                               uint16_t cVirtWidth, uint16_t cBPP,
                                               uint16_t fFlags,
                                               uint16_t cx, uint16_t cy);
DECLHIDDEN(bool)     VBoxVideoGetModeRegisters(uint16_t *pcWidth,
                                               uint16_t *pcHeight,
                                               uint16_t *pcVirtWidth,
                                               uint16_t *pcBPP,
                                               uint16_t *pfFlags);
DECLHIDDEN(void)     VBoxVideoDisableVBE(void);
DECLHIDDEN(void)     VBoxHGSMIProcessDisplayInfo(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                                 uint32_t cDisplay,
                                                 int32_t  cOriginX,
                                                 int32_t  cOriginY,
                                                 uint32_t offStart,
                                                 uint32_t cbPitch,
                                                 uint32_t cWidth,
                                                 uint32_t cHeight,
                                                 uint16_t cBPP,
                                                 uint16_t fFlags);
DECLHIDDEN(int)      VBoxHGSMIUpdateInputMapping(PHGSMIGUESTCOMMANDCONTEXT pCtx, int32_t  cOriginX, int32_t  cOriginY,
                                                 uint32_t cWidth, uint32_t cHeight);
DECLHIDDEN(int) VBoxHGSMIGetModeHints(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                      unsigned cScreens, VBVAMODEHINT *paHints);

/** @}  */

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_Graphics_VBoxVideoGuest_h */

