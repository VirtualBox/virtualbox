/* $Id: VBoxVideo.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Video interface.
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

#ifndef VBOX_INCLUDED_Graphics_VBoxVideo_h
#define VBOX_INCLUDED_Graphics_VBoxVideo_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VBoxVideoIPRT.h"

/* this should be in sync with monitorCount <xsd:maxInclusive value="64"/> in src/VBox/Main/xml/VirtualBox-settings-common.xsd */
#define VBOX_VIDEO_MAX_SCREENS 64

/*
 * The last 4096 bytes of the guest VRAM contains the generic info for all
 * DualView chunks: sizes and offsets of chunks. This is filled by miniport.
 *
 * Last 4096 bytes of each chunk contain chunk specific data: framebuffer info,
 * etc. This is used exclusively by the corresponding instance of a display driver.
 *
 * The VRAM layout:
 *     Last 4096 bytes - Adapter information area.
 *     4096 bytes aligned miniport heap (value specified in the config rouded up).
 *     Slack - what left after dividing the VRAM.
 *     4096 bytes aligned framebuffers:
 *       last 4096 bytes of each framebuffer is the display information area.
 *
 * The Virtual Graphics Adapter information in the guest VRAM is stored by the
 * guest video driver using structures prepended by VBOXVIDEOINFOHDR.
 *
 * When the guest driver writes dword 0 to the VBE_DISPI_INDEX_VBOX_VIDEO
 * the host starts to process the info. The first element at the start of
 * the 4096 bytes region should be normally be a LINK that points to
 * actual information chain. That way the guest driver can have some
 * fixed layout of the information memory block and just rewrite
 * the link to point to relevant memory chain.
 *
 * The processing stops at the END element.
 *
 * The host can access the memory only when the port IO is processed.
 * All data that will be needed later must be copied from these 4096 bytes.
 * But other VRAM can be used by host until the mode is disabled.
 *
 * The guest driver writes dword 0xffffffff to the VBE_DISPI_INDEX_VBOX_VIDEO
 * to disable the mode.
 *
 * VBE_DISPI_INDEX_VBOX_VIDEO is used to read the configuration information
 * from the host and issue commands to the host.
 *
 * The guest writes the VBE_DISPI_INDEX_VBOX_VIDEO index register, the the
 * following operations with the VBE data register can be performed:
 *
 * Operation            Result
 * write 16 bit value   NOP
 * read 16 bit value    count of monitors
 * write 32 bit value   sets the vbox command value and the command processed by the host
 * read 32 bit value    result of the last vbox command is returned
 */

#define VBOX_VIDEO_PRIMARY_SCREEN 0
#define VBOX_VIDEO_NO_SCREEN ~0

/**
 * VBVA command header.
 *
 * @todo Where does this fit in?
 */
typedef struct VBVACMDHDR
{
   /** Coordinates of affected rectangle. */
   int16_t x;
   int16_t y;
   uint16_t w;
   uint16_t h;
} VBVACMDHDR;
AssertCompileSize(VBVACMDHDR, 8);

/** @name VBVA ring defines.
 *
 * The VBVA ring buffer is suitable for transferring large (< 2GB) amount of
 * data. For example big bitmaps which do not fit to the buffer.
 *
 * Guest starts writing to the buffer by initializing a record entry in the
 * aRecords queue. VBVA_F_RECORD_PARTIAL indicates that the record is being
 * written. As data is written to the ring buffer, the guest increases off32End
 * for the record.
 *
 * The host reads the aRecords on flushes and processes all completed records.
 * When host encounters situation when only a partial record presents and
 * cbRecord & ~VBVA_F_RECORD_PARTIAL >= VBVA_RING_BUFFER_SIZE -
 * VBVA_RING_BUFFER_THRESHOLD, the host fetched all record data and updates
 * off32Head. After that on each flush the host continues fetching the data
 * until the record is completed.
 *
 */
#define VBVA_RING_BUFFER_SIZE        (_4M - _1K)
#define VBVA_RING_BUFFER_THRESHOLD   (4 * _1K)

#define VBVA_MAX_RECORDS (64)

#define VBVA_F_MODE_ENABLED         UINT32_C(0x00000001)
#define VBVA_F_MODE_VRDP            UINT32_C(0x00000002)
#define VBVA_F_MODE_VRDP_RESET      UINT32_C(0x00000004)
#define VBVA_F_MODE_VRDP_ORDER_MASK UINT32_C(0x00000008)

#define VBVA_F_STATE_PROCESSING     UINT32_C(0x00010000)

#define VBVA_F_RECORD_PARTIAL       UINT32_C(0x80000000)

/**
 * VBVA record.
 */
typedef struct VBVARECORD
{
    /** The length of the record. Changed by guest. */
    uint32_t cbRecord;
} VBVARECORD;
AssertCompileSize(VBVARECORD, 4);

/* The size of the information. */
/*
 * The minimum HGSMI heap size is PAGE_SIZE (4096 bytes) and is a restriction of the
 * runtime heapsimple API. Use minimum 2 pages here, because the info area also may
 * contain other data (for example HGSMIHOSTFLAGS structure).
 */
#ifndef VBOX_XPDM_MINIPORT
# define VBVA_ADAPTER_INFORMATION_SIZE (64*_1K)
#else
#define VBVA_ADAPTER_INFORMATION_SIZE  (16*_1K)
#define VBVA_DISPLAY_INFORMATION_SIZE  (64*_1K)
#endif
#define VBVA_MIN_BUFFER_SIZE           (64*_1K)


/* The value for port IO to let the adapter to interpret the adapter memory. */
#define VBOX_VIDEO_DISABLE_ADAPTER_MEMORY        0xFFFFFFFF

/* The value for port IO to let the adapter to interpret the adapter memory. */
#define VBOX_VIDEO_INTERPRET_ADAPTER_MEMORY      0x00000000

/* The value for port IO to let the adapter to interpret the display memory.
 * The display number is encoded in low 16 bits.
 */
#define VBOX_VIDEO_INTERPRET_DISPLAY_MEMORY_BASE 0x00010000


/* The end of the information. */
#define VBOX_VIDEO_INFO_TYPE_END          0
/* Instructs the host to fetch the next VBOXVIDEOINFOHDR at the given offset of VRAM. */
#define VBOX_VIDEO_INFO_TYPE_LINK         1
/* Information about a display memory position. */
#define VBOX_VIDEO_INFO_TYPE_DISPLAY      2
/* Information about a screen. */
#define VBOX_VIDEO_INFO_TYPE_SCREEN       3
/* Information about host notifications for the driver. */
#define VBOX_VIDEO_INFO_TYPE_HOST_EVENTS  4
/* Information about non-volatile guest VRAM heap. */
#define VBOX_VIDEO_INFO_TYPE_NV_HEAP      5
/* VBVA enable/disable. */
#define VBOX_VIDEO_INFO_TYPE_VBVA_STATUS  6
/* VBVA flush. */
#define VBOX_VIDEO_INFO_TYPE_VBVA_FLUSH   7
/* Query configuration value. */
#define VBOX_VIDEO_INFO_TYPE_QUERY_CONF32 8


#pragma pack(1)
typedef struct VBOXVIDEOINFOHDR
{
    uint8_t u8Type;
    uint8_t u8Reserved;
    uint16_t u16Length;
} VBOXVIDEOINFOHDR;


typedef struct VBOXVIDEOINFOLINK
{
    /* Relative offset in VRAM */
    int32_t i32Offset;
} VBOXVIDEOINFOLINK;


/* Resides in adapter info memory. Describes a display VRAM chunk. */
typedef struct VBOXVIDEOINFODISPLAY
{
    /* Index of the framebuffer assigned by guest. */
    uint32_t u32Index;

    /* Absolute offset in VRAM of the framebuffer to be displayed on the monitor. */
    uint32_t u32Offset;

    /* The size of the memory that can be used for the screen. */
    uint32_t u32FramebufferSize;

    /* The size of the memory that is used for the Display information.
     * The information is at u32Offset + u32FramebufferSize
     */
    uint32_t u32InformationSize;

} VBOXVIDEOINFODISPLAY;


/* Resides in display info area, describes the current video mode. */
#define VBOX_VIDEO_INFO_SCREEN_F_NONE   0x00
#define VBOX_VIDEO_INFO_SCREEN_F_ACTIVE 0x01

typedef struct VBOXVIDEOINFOSCREEN
{
    /* Physical X origin relative to the primary screen. */
    int32_t xOrigin;

    /* Physical Y origin relative to the primary screen. */
    int32_t yOrigin;

    /* The scan line size in bytes. */
    uint32_t u32LineSize;

    /* Width of the screen. */
    uint16_t u16Width;

    /* Height of the screen. */
    uint16_t u16Height;

    /* Color depth. */
    uint8_t bitsPerPixel;

    /* VBOX_VIDEO_INFO_SCREEN_F_* */
    uint8_t u8Flags;
} VBOXVIDEOINFOSCREEN;

/* The guest initializes the structure to 0. The positions of the structure in the
 * display info area must not be changed, host will update the structure. Guest checks
 * the events and modifies the structure as a response to host.
 */
#define VBOX_VIDEO_INFO_HOST_EVENTS_F_NONE        0x00000000
#define VBOX_VIDEO_INFO_HOST_EVENTS_F_VRDP_RESET  0x00000080

typedef struct VBOXVIDEOINFOHOSTEVENTS
{
    /* Host events. */
    uint32_t fu32Events;
} VBOXVIDEOINFOHOSTEVENTS;

/* Resides in adapter info memory. Describes the non-volatile VRAM heap. */
typedef struct VBOXVIDEOINFONVHEAP
{
    /* Absolute offset in VRAM of the start of the heap. */
    uint32_t u32HeapOffset;

    /* The size of the heap. */
    uint32_t u32HeapSize;

} VBOXVIDEOINFONVHEAP;

/* Display information area. */
typedef struct VBOXVIDEOINFOVBVASTATUS
{
    /* Absolute offset in VRAM of the start of the VBVA QUEUE. 0 to disable VBVA. */
    uint32_t u32QueueOffset;

    /* The size of the VBVA QUEUE. 0 to disable VBVA. */
    uint32_t u32QueueSize;

} VBOXVIDEOINFOVBVASTATUS;

typedef struct VBOXVIDEOINFOVBVAFLUSH
{
    uint32_t u32DataStart;

    uint32_t u32DataEnd;

} VBOXVIDEOINFOVBVAFLUSH;

#define VBOX_VIDEO_QCI32_MONITOR_COUNT       0
#define VBOX_VIDEO_QCI32_OFFSCREEN_HEAP_SIZE 1

typedef struct VBOXVIDEOINFOQUERYCONF32
{
    uint32_t u32Index;

    uint32_t u32Value;

} VBOXVIDEOINFOQUERYCONF32;
#pragma pack()

#ifdef VBOX_WITH_VIDEOHWACCEL
#pragma pack(1)

#define VBOXVHWA_VERSION_MAJ 0
#define VBOXVHWA_VERSION_MIN 0
#define VBOXVHWA_VERSION_BLD 6
#define VBOXVHWA_VERSION_RSV 0

typedef enum
{
    VBOXVHWACMD_TYPE_SURF_CANCREATE = 1,
    VBOXVHWACMD_TYPE_SURF_CREATE,
    VBOXVHWACMD_TYPE_SURF_DESTROY,
    VBOXVHWACMD_TYPE_SURF_LOCK,
    VBOXVHWACMD_TYPE_SURF_UNLOCK,
    VBOXVHWACMD_TYPE_SURF_BLT,
    VBOXVHWACMD_TYPE_SURF_FLIP,
    VBOXVHWACMD_TYPE_SURF_OVERLAY_UPDATE,
    VBOXVHWACMD_TYPE_SURF_OVERLAY_SETPOSITION,
    VBOXVHWACMD_TYPE_SURF_COLORKEY_SET,
    VBOXVHWACMD_TYPE_QUERY_INFO1,
    VBOXVHWACMD_TYPE_QUERY_INFO2,
    VBOXVHWACMD_TYPE_ENABLE,
    VBOXVHWACMD_TYPE_DISABLE,
    VBOXVHWACMD_TYPE_HH_CONSTRUCT,
    VBOXVHWACMD_TYPE_HH_RESET
#ifdef VBOX_WITH_WDDM
    , VBOXVHWACMD_TYPE_SURF_GETINFO
    , VBOXVHWACMD_TYPE_SURF_COLORFILL
#endif
    , VBOXVHWACMD_TYPE_HH_DISABLE
    , VBOXVHWACMD_TYPE_HH_ENABLE
    , VBOXVHWACMD_TYPE_HH_SAVESTATE_SAVEBEGIN
    , VBOXVHWACMD_TYPE_HH_SAVESTATE_SAVEEND
    , VBOXVHWACMD_TYPE_HH_SAVESTATE_SAVEPERFORM
    , VBOXVHWACMD_TYPE_HH_SAVESTATE_LOADPERFORM
} VBOXVHWACMD_TYPE;

/** The command processing was asynch, set by the host to indicate asynch
 * command completion. Must not be cleared once set, the command completion is
 * performed by issuing a host->guest completion command while keeping this
 * flag unchanged */
#define VBOXVHWACMD_FLAG_HG_ASYNCH               UINT32_C(0x00010000)
/** asynch completion is performed by issuing the event */
#define VBOXVHWACMD_FLAG_GH_ASYNCH_EVENT         UINT32_C(0x00000001)
/** issue interrupt on asynch completion */
#define VBOXVHWACMD_FLAG_GH_ASYNCH_IRQ           UINT32_C(0x00000002)
/** Guest does not do any op on completion of this command, the host may copy
 * the command and indicate that it does not need the command anymore
 * by setting the VBOXVHWACMD_FLAG_HG_ASYNCH_RETURNED flag */
#define VBOXVHWACMD_FLAG_GH_ASYNCH_NOCOMPLETION  UINT32_C(0x00000004)
/** the host has copied the VBOXVHWACMD_FLAG_GH_ASYNCH_NOCOMPLETION command and returned it to the guest */
#define VBOXVHWACMD_FLAG_HG_ASYNCH_RETURNED      UINT32_C(0x00020000)
/** this is the host->host cmd, i.e. a configuration command posted by the host to the framebuffer */
#define VBOXVHWACMD_FLAG_HH_CMD                  UINT32_C(0x10000000)

typedef struct VBOXVHWACMD
{
    VBOXVHWACMD_TYPE enmCmd;     /**< command type */
    volatile int32_t rc;         /**< command result */
    int32_t iDisplay;            /**< display index */
    volatile int32_t Flags;      /**< ORed VBOXVHWACMD_FLAG_xxx values */
    uint64_t GuestVBVAReserved1; /**< field internally used by the guest VBVA cmd handling, must NOT be modified by clients */
    uint64_t GuestVBVAReserved2; /**< field internally used by the guest VBVA cmd handling, must NOT be modified by clients */
    volatile uint32_t cRefs;
    int32_t Reserved;
    union
    {
        struct VBOXVHWACMD *pNext;
        uint32_t            offNext;
        uint64_t Data;                  /**< the body is 64-bit aligned */
    } u;
    char body[1];
} VBOXVHWACMD;

#define VBOXVHWACMD_HEADSIZE()                          (RT_OFFSETOF(VBOXVHWACMD, body))
#define VBOXVHWACMD_SIZE_FROMBODYSIZE(a_cbBody)         (VBOXVHWACMD_HEADSIZE() + (a_cbBody))
#define VBOXVHWACMD_SIZE(a_tTypeCmd)                    (VBOXVHWACMD_SIZE_FROMBODYSIZE(sizeof(a_tTypeCmd)))
typedef unsigned int VBOXVHWACMD_LENGTH;
typedef uint64_t VBOXVHWA_SURFHANDLE;
#define VBOXVHWA_SURFHANDLE_INVALID                     UINT64_C(0)
#define VBOXVHWACMD_BODY(a_pHdr, a_TypeBody)            ( (a_TypeBody RT_UNTRUSTED_VOLATILE_HSTGST *)&(a_pHdr)->body[0] )
#if !defined(IN_GUEST) && defined(IN_RING3)
# define VBOXVHWACMD_BODY_HOST_HEAP(a_pHdr, a_TypeBody) ( (a_TypeBody *)&(a_pHdr)->body[0] )
#endif
#define VBOXVHWACMD_HEAD(a_pBody)\
    ( (VBOXVHWACMD RT_UNTRUSTED_VOLATILE_HSTGST *)((uint8_t *)(a_pBody) - RT_OFFSETOF(VBOXVHWACMD, body)))

typedef struct VBOXVHWA_RECTL
{
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
} VBOXVHWA_RECTL;

typedef struct VBOXVHWA_COLORKEY
{
    uint32_t low;
    uint32_t high;
} VBOXVHWA_COLORKEY;

typedef struct VBOXVHWA_PIXELFORMAT
{
    uint32_t flags;
    uint32_t fourCC;
    union
    {
        uint32_t rgbBitCount;
        uint32_t yuvBitCount;
    } c;

    union
    {
        uint32_t rgbRBitMask;
        uint32_t yuvYBitMask;
    } m1;

    union
    {
        uint32_t rgbGBitMask;
        uint32_t yuvUBitMask;
    } m2;

    union
    {
        uint32_t rgbBBitMask;
        uint32_t yuvVBitMask;
    } m3;

    union
    {
        uint32_t rgbABitMask;
    } m4;

    uint32_t Reserved;
} VBOXVHWA_PIXELFORMAT;

typedef struct VBOXVHWA_SURFACEDESC
{
    uint32_t flags;
    uint32_t height;
    uint32_t width;
    uint32_t pitch;
    uint32_t sizeX;
    uint32_t sizeY;
    uint32_t cBackBuffers;
    uint32_t Reserved;
    VBOXVHWA_COLORKEY DstOverlayCK;
    VBOXVHWA_COLORKEY DstBltCK;
    VBOXVHWA_COLORKEY SrcOverlayCK;
    VBOXVHWA_COLORKEY SrcBltCK;
    VBOXVHWA_PIXELFORMAT PixelFormat;
    uint32_t surfCaps;
    uint32_t Reserved2;
    VBOXVHWA_SURFHANDLE hSurf;
    uint64_t offSurface;
} VBOXVHWA_SURFACEDESC;

typedef struct VBOXVHWA_BLTFX
{
    uint32_t flags;
    uint32_t rop;
    uint32_t rotationOp;
    uint32_t rotation;
    uint32_t fillColor;
    uint32_t Reserved;
    VBOXVHWA_COLORKEY DstCK;
    VBOXVHWA_COLORKEY SrcCK;
} VBOXVHWA_BLTFX;

typedef struct VBOXVHWA_OVERLAYFX
{
    uint32_t flags;
    uint32_t Reserved1;
    uint32_t fxFlags;
    uint32_t Reserved2;
    VBOXVHWA_COLORKEY DstCK;
    VBOXVHWA_COLORKEY SrcCK;
} VBOXVHWA_OVERLAYFX;

#define VBOXVHWA_CAPS_BLT               0x00000040
#define VBOXVHWA_CAPS_BLTCOLORFILL      0x04000000
#define VBOXVHWA_CAPS_BLTFOURCC         0x00000100
#define VBOXVHWA_CAPS_BLTSTRETCH        0x00000200
#define VBOXVHWA_CAPS_BLTQUEUE          0x00000080

#define VBOXVHWA_CAPS_OVERLAY           0x00000800
#define VBOXVHWA_CAPS_OVERLAYFOURCC     0x00002000
#define VBOXVHWA_CAPS_OVERLAYSTRETCH    0x00004000
#define VBOXVHWA_CAPS_OVERLAYCANTCLIP   0x00001000

#define VBOXVHWA_CAPS_COLORKEY          0x00400000
#define VBOXVHWA_CAPS_COLORKEYHWASSIST  0x01000000

#define VBOXVHWA_SCAPS_BACKBUFFER       0x00000004
#define VBOXVHWA_SCAPS_COMPLEX          0x00000008
#define VBOXVHWA_SCAPS_FLIP             0x00000010
#define VBOXVHWA_SCAPS_FRONTBUFFER      0x00000020
#define VBOXVHWA_SCAPS_OFFSCREENPLAIN   0x00000040
#define VBOXVHWA_SCAPS_OVERLAY          0x00000080
#define VBOXVHWA_SCAPS_PRIMARYSURFACE   0x00000200
#define VBOXVHWA_SCAPS_SYSTEMMEMORY     0x00000800
#define VBOXVHWA_SCAPS_VIDEOMEMORY      0x00004000
#define VBOXVHWA_SCAPS_VISIBLE          0x00008000
#define VBOXVHWA_SCAPS_LOCALVIDMEM      0x10000000

#define VBOXVHWA_PF_PALETTEINDEXED8     0x00000020
#define VBOXVHWA_PF_RGB                 0x00000040
#define VBOXVHWA_PF_RGBTOYUV            0x00000100
#define VBOXVHWA_PF_YUV                 0x00000200
#define VBOXVHWA_PF_FOURCC              0x00000004

#define VBOXVHWA_LOCK_DISCARDCONTENTS   0x00002000

#define VBOXVHWA_CFG_ENABLED            0x00000001

#define VBOXVHWA_SD_BACKBUFFERCOUNT     0x00000020
#define VBOXVHWA_SD_CAPS                0x00000001
#define VBOXVHWA_SD_CKDESTBLT           0x00004000
#define VBOXVHWA_SD_CKDESTOVERLAY       0x00002000
#define VBOXVHWA_SD_CKSRCBLT            0x00010000
#define VBOXVHWA_SD_CKSRCOVERLAY        0x00008000
#define VBOXVHWA_SD_HEIGHT              0x00000002
#define VBOXVHWA_SD_PITCH               0x00000008
#define VBOXVHWA_SD_PIXELFORMAT         0x00001000
/*#define VBOXVHWA_SD_REFRESHRATE       0x00040000*/
#define VBOXVHWA_SD_WIDTH               0x00000004

#define VBOXVHWA_CKEYCAPS_DESTBLT                  0x00000001
#define VBOXVHWA_CKEYCAPS_DESTBLTCLRSPACE          0x00000002
#define VBOXVHWA_CKEYCAPS_DESTBLTCLRSPACEYUV       0x00000004
#define VBOXVHWA_CKEYCAPS_DESTBLTYUV               0x00000008
#define VBOXVHWA_CKEYCAPS_DESTOVERLAY              0x00000010
#define VBOXVHWA_CKEYCAPS_DESTOVERLAYCLRSPACE      0x00000020
#define VBOXVHWA_CKEYCAPS_DESTOVERLAYCLRSPACEYUV   0x00000040
#define VBOXVHWA_CKEYCAPS_DESTOVERLAYONEACTIVE     0x00000080
#define VBOXVHWA_CKEYCAPS_DESTOVERLAYYUV           0x00000100
#define VBOXVHWA_CKEYCAPS_SRCBLT                   0x00000200
#define VBOXVHWA_CKEYCAPS_SRCBLTCLRSPACE           0x00000400
#define VBOXVHWA_CKEYCAPS_SRCBLTCLRSPACEYUV        0x00000800
#define VBOXVHWA_CKEYCAPS_SRCBLTYUV                0x00001000
#define VBOXVHWA_CKEYCAPS_SRCOVERLAY               0x00002000
#define VBOXVHWA_CKEYCAPS_SRCOVERLAYCLRSPACE       0x00004000
#define VBOXVHWA_CKEYCAPS_SRCOVERLAYCLRSPACEYUV    0x00008000
#define VBOXVHWA_CKEYCAPS_SRCOVERLAYONEACTIVE      0x00010000
#define VBOXVHWA_CKEYCAPS_SRCOVERLAYYUV            0x00020000
#define VBOXVHWA_CKEYCAPS_NOCOSTOVERLAY            0x00040000

#define VBOXVHWA_BLT_COLORFILL                      0x00000400
#define VBOXVHWA_BLT_DDFX                           0x00000800
#define VBOXVHWA_BLT_EXTENDED_FLAGS                 0x40000000
#define VBOXVHWA_BLT_EXTENDED_LINEAR_CONTENT        0x00000004
#define VBOXVHWA_BLT_EXTENDED_PRESENTATION_STRETCHFACTOR 0x00000010
#define VBOXVHWA_BLT_KEYDESTOVERRIDE                0x00004000
#define VBOXVHWA_BLT_KEYSRCOVERRIDE                 0x00010000
#define VBOXVHWA_BLT_LAST_PRESENTATION              0x20000000
#define VBOXVHWA_BLT_PRESENTATION                   0x10000000
#define VBOXVHWA_BLT_ROP                            0x00020000


#define VBOXVHWA_OVER_DDFX                          0x00080000
#define VBOXVHWA_OVER_HIDE                          0x00000200
#define VBOXVHWA_OVER_KEYDEST                       0x00000400
#define VBOXVHWA_OVER_KEYDESTOVERRIDE               0x00000800
#define VBOXVHWA_OVER_KEYSRC                        0x00001000
#define VBOXVHWA_OVER_KEYSRCOVERRIDE                0x00002000
#define VBOXVHWA_OVER_SHOW                          0x00004000

#define VBOXVHWA_CKEY_COLORSPACE                    0x00000001
#define VBOXVHWA_CKEY_DESTBLT                       0x00000002
#define VBOXVHWA_CKEY_DESTOVERLAY                   0x00000004
#define VBOXVHWA_CKEY_SRCBLT                        0x00000008
#define VBOXVHWA_CKEY_SRCOVERLAY                    0x00000010

#define VBOXVHWA_BLT_ARITHSTRETCHY                  0x00000001
#define VBOXVHWA_BLT_MIRRORLEFTRIGHT                0x00000002
#define VBOXVHWA_BLT_MIRRORUPDOWN                   0x00000004

#define VBOXVHWA_OVERFX_ARITHSTRETCHY               0x00000001
#define VBOXVHWA_OVERFX_MIRRORLEFTRIGHT             0x00000002
#define VBOXVHWA_OVERFX_MIRRORUPDOWN                0x00000004

#define VBOXVHWA_CAPS2_CANRENDERWINDOWED            0x00080000
#define VBOXVHWA_CAPS2_WIDESURFACES                 0x00001000
#define VBOXVHWA_CAPS2_COPYFOURCC                   0x00008000
/*#define VBOXVHWA_CAPS2_FLIPINTERVAL                 0x00200000*/
/*#define VBOXVHWA_CAPS2_FLIPNOVSYNC                  0x00400000*/


#define VBOXVHWA_OFFSET64_VOID        (UINT64_MAX)

typedef struct VBOXVHWA_VERSION
{
    uint32_t maj;
    uint32_t min;
    uint32_t bld;
    uint32_t reserved;
} VBOXVHWA_VERSION;

#define VBOXVHWA_VERSION_INIT(_pv) do { \
        (_pv)->maj = VBOXVHWA_VERSION_MAJ; \
        (_pv)->min = VBOXVHWA_VERSION_MIN; \
        (_pv)->bld = VBOXVHWA_VERSION_BLD; \
        (_pv)->reserved = VBOXVHWA_VERSION_RSV; \
        } while(0)

typedef struct VBOXVHWACMD_QUERYINFO1
{
    union
    {
        struct
        {
            VBOXVHWA_VERSION guestVersion;
        } in;

        struct
        {
            uint32_t cfgFlags;
            uint32_t caps;

            uint32_t caps2;
            uint32_t colorKeyCaps;

            uint32_t stretchCaps;
            uint32_t surfaceCaps;

            uint32_t numOverlays;
            uint32_t curOverlays;

            uint32_t numFourCC;
            uint32_t reserved;
        } out;
    } u;
} VBOXVHWACMD_QUERYINFO1;

typedef struct VBOXVHWACMD_QUERYINFO2
{
    uint32_t numFourCC;
    uint32_t FourCC[1];
} VBOXVHWACMD_QUERYINFO2;

#define VBOXVHWAINFO2_SIZE(_cFourCC) RT_UOFFSETOF_DYN(VBOXVHWACMD_QUERYINFO2, FourCC[_cFourCC])

typedef struct VBOXVHWACMD_SURF_CANCREATE
{
    VBOXVHWA_SURFACEDESC SurfInfo;
    union
    {
        struct
        {
            uint32_t bIsDifferentPixelFormat;
            uint32_t Reserved;
        } in;

        struct
        {
            int32_t ErrInfo;
        } out;
    } u;
} VBOXVHWACMD_SURF_CANCREATE;

typedef struct VBOXVHWACMD_SURF_CREATE
{
    VBOXVHWA_SURFACEDESC SurfInfo;
} VBOXVHWACMD_SURF_CREATE;

#ifdef VBOX_WITH_WDDM
typedef struct VBOXVHWACMD_SURF_GETINFO
{
    VBOXVHWA_SURFACEDESC SurfInfo;
} VBOXVHWACMD_SURF_GETINFO;
#endif

typedef struct VBOXVHWACMD_SURF_DESTROY
{
    union
    {
        struct
        {
            VBOXVHWA_SURFHANDLE hSurf;
        } in;
    } u;
} VBOXVHWACMD_SURF_DESTROY;

typedef struct VBOXVHWACMD_SURF_LOCK
{
    union
    {
        struct
        {
            VBOXVHWA_SURFHANDLE hSurf;
            uint64_t offSurface;
            uint32_t flags;
            uint32_t rectValid;
            VBOXVHWA_RECTL rect;
        } in;
    } u;
} VBOXVHWACMD_SURF_LOCK;

typedef struct VBOXVHWACMD_SURF_UNLOCK
{
    union
    {
        struct
        {
            VBOXVHWA_SURFHANDLE hSurf;
            uint32_t xUpdatedMemValid;
            uint32_t reserved;
            VBOXVHWA_RECTL xUpdatedMemRect;
        } in;
    } u;
} VBOXVHWACMD_SURF_UNLOCK;

typedef struct VBOXVHWACMD_SURF_BLT
{
    uint64_t DstGuestSurfInfo;
    uint64_t SrcGuestSurfInfo;
    union
    {
        struct
        {
            VBOXVHWA_SURFHANDLE hDstSurf;
            uint64_t offDstSurface;
            VBOXVHWA_RECTL dstRect;
            VBOXVHWA_SURFHANDLE hSrcSurf;
            uint64_t offSrcSurface;
            VBOXVHWA_RECTL srcRect;
            uint32_t flags;
            uint32_t xUpdatedSrcMemValid;
            VBOXVHWA_BLTFX desc;
            VBOXVHWA_RECTL xUpdatedSrcMemRect;
        } in;
    } u;
} VBOXVHWACMD_SURF_BLT;

#ifdef VBOX_WITH_WDDM
typedef struct VBOXVHWACMD_SURF_COLORFILL
{
    union
    {
        struct
        {
            VBOXVHWA_SURFHANDLE hSurf;
            uint64_t offSurface;
            uint32_t u32Reserved;
            uint32_t cRects;
            VBOXVHWA_RECTL aRects[1];
        } in;
    } u;
} VBOXVHWACMD_SURF_COLORFILL;
#endif

typedef struct VBOXVHWACMD_SURF_FLIP
{
    uint64_t TargGuestSurfInfo;
    uint64_t CurrGuestSurfInfo;
    union
    {
        struct
        {
            VBOXVHWA_SURFHANDLE hTargSurf;
            uint64_t offTargSurface;
            VBOXVHWA_SURFHANDLE hCurrSurf;
            uint64_t offCurrSurface;
            uint32_t flags;
            uint32_t xUpdatedTargMemValid;
            VBOXVHWA_RECTL xUpdatedTargMemRect;
        } in;
    } u;
} VBOXVHWACMD_SURF_FLIP;

typedef struct VBOXVHWACMD_SURF_COLORKEY_SET
{
    union
    {
        struct
        {
            VBOXVHWA_SURFHANDLE hSurf;
            uint64_t offSurface;
            VBOXVHWA_COLORKEY CKey;
            uint32_t flags;
            uint32_t reserved;
        } in;
    } u;
} VBOXVHWACMD_SURF_COLORKEY_SET;

#define VBOXVHWACMD_SURF_OVERLAY_UPDATE_F_SRCMEMRECT 0x00000001
#define VBOXVHWACMD_SURF_OVERLAY_UPDATE_F_DSTMEMRECT 0x00000002

typedef struct VBOXVHWACMD_SURF_OVERLAY_UPDATE
{
    union
    {
        struct
        {
            VBOXVHWA_SURFHANDLE hDstSurf;
            uint64_t offDstSurface;
            VBOXVHWA_RECTL dstRect;
            VBOXVHWA_SURFHANDLE hSrcSurf;
            uint64_t offSrcSurface;
            VBOXVHWA_RECTL srcRect;
            uint32_t flags;
            uint32_t xFlags;
            VBOXVHWA_OVERLAYFX desc;
            VBOXVHWA_RECTL xUpdatedSrcMemRect;
            VBOXVHWA_RECTL xUpdatedDstMemRect;
        } in;
    } u;
}VBOXVHWACMD_SURF_OVERLAY_UPDATE;

typedef struct VBOXVHWACMD_SURF_OVERLAY_SETPOSITION
{
    union
    {
        struct
        {
            VBOXVHWA_SURFHANDLE hDstSurf;
            uint64_t offDstSurface;
            VBOXVHWA_SURFHANDLE hSrcSurf;
            uint64_t offSrcSurface;
            uint32_t xPos;
            uint32_t yPos;
            uint32_t flags;
            uint32_t reserved;
        } in;
    } u;
} VBOXVHWACMD_SURF_OVERLAY_SETPOSITION;

typedef struct VBOXVHWACMD_HH_CONSTRUCT
{
    void    *pVM;
    /* VRAM info for the backend to be able to properly translate VRAM offsets */
    void    *pvVRAM;
    uint32_t cbVRAM;
} VBOXVHWACMD_HH_CONSTRUCT;

typedef struct VBOXVHWACMD_HH_SAVESTATE_SAVEPERFORM
{
    struct SSMHANDLE * pSSM;
} VBOXVHWACMD_HH_SAVESTATE_SAVEPERFORM;

typedef struct VBOXVHWACMD_HH_SAVESTATE_LOADPERFORM
{
    struct SSMHANDLE * pSSM;
} VBOXVHWACMD_HH_SAVESTATE_LOADPERFORM;

typedef DECLCALLBACKTYPE(void, FNVBOXVHWA_HH_CALLBACK,(void *));
typedef FNVBOXVHWA_HH_CALLBACK *PFNVBOXVHWA_HH_CALLBACK;

#define VBOXVHWA_HH_CALLBACK_SET(_pCmd, _pfn, _parg) \
    do { \
        (_pCmd)->GuestVBVAReserved1 = (uint64_t)(uintptr_t)(_pfn); \
        (_pCmd)->GuestVBVAReserved2 = (uint64_t)(uintptr_t)(_parg); \
    }while(0)

#define VBOXVHWA_HH_CALLBACK_GET(_pCmd) ((PFNVBOXVHWA_HH_CALLBACK)(_pCmd)->GuestVBVAReserved1)
#define VBOXVHWA_HH_CALLBACK_GET_ARG(_pCmd) ((void*)(_pCmd)->GuestVBVAReserved2)

#pragma pack()
#endif /* #ifdef VBOX_WITH_VIDEOHWACCEL */

/* All structures are without alignment. */
#pragma pack(1)

typedef struct VBVAHOSTFLAGS
{
    uint32_t u32HostEvents;
    uint32_t u32SupportedOrders;
} VBVAHOSTFLAGS;

typedef struct VBVABUFFER
{
    VBVAHOSTFLAGS hostFlags;

    /* The offset where the data start in the buffer. */
    uint32_t off32Data;
    /* The offset where next data must be placed in the buffer. */
    uint32_t off32Free;

    /* The queue of record descriptions. */
    VBVARECORD aRecords[VBVA_MAX_RECORDS];
    uint32_t indexRecordFirst;
    uint32_t indexRecordFree;

    /* Space to leave free in the buffer when large partial records are transferred. */
    uint32_t cbPartialWriteThreshold;

    uint32_t cbData;
    uint8_t  au8Data[1]; /* variable size for the rest of the VBVABUFFER area in VRAM. */
} VBVABUFFER;

#define VBVA_MAX_RECORD_SIZE (128*_1M)

/* guest->host commands */
#define VBVA_QUERY_CONF32 1
#define VBVA_SET_CONF32   2
#define VBVA_INFO_VIEW    3
#define VBVA_INFO_HEAP    4
#define VBVA_FLUSH        5
#define VBVA_INFO_SCREEN  6
/** Enables or disables VBVA.  Enabling VBVA without disabling it before
 * causes a complete screen update. */
#define VBVA_ENABLE       7
#define VBVA_MOUSE_POINTER_SHAPE 8
#ifdef VBOX_WITH_VIDEOHWACCEL
# define VBVA_VHWA_CMD    9
#endif /* # ifdef VBOX_WITH_VIDEOHWACCEL */
#ifdef VBOX_WITH_VDMA
# define VBVA_VDMA_CTL   10 /* setup G<->H DMA channel info */
# define VBVA_VDMA_CMD    11 /* G->H DMA command             */
#endif
#define VBVA_INFO_CAPS   12 /* informs host about HGSMI caps. see VBVACAPS below */
#define VBVA_SCANLINE_CFG    13 /* configures scanline, see VBVASCANLINECFG below */
#define VBVA_SCANLINE_INFO   14 /* requests scanline info, see VBVASCANLINEINFO below */
#define VBVA_CMDVBVA_SUBMIT  16 /* inform host about VBVA Command submission */
#define VBVA_CMDVBVA_FLUSH   17 /* inform host about VBVA Command submission */
#define VBVA_CMDVBVA_CTL     18 /* G->H DMA command             */
#define VBVA_QUERY_MODE_HINTS 19 /* Query most recent mode hints sent. */
/** Report the guest virtual desktop position and size for mapping host and
 * guest pointer positions. */
#define VBVA_REPORT_INPUT_MAPPING 20
/** Report the guest cursor position and query the host position. */
#define VBVA_CURSOR_POSITION 21

/* host->guest commands */
#define VBVAHG_EVENT              1
#define VBVAHG_DISPLAY_CUSTOM     2
#ifdef VBOX_WITH_VDMA
#define VBVAHG_SHGSMI_COMPLETION  3
#endif

#ifdef VBOX_WITH_VIDEOHWACCEL
#define VBVAHG_DCUSTOM_VHWA_CMDCOMPLETE 1
#pragma pack(1)
typedef struct VBVAHOSTCMDVHWACMDCOMPLETE
{
    uint32_t offCmd;
}VBVAHOSTCMDVHWACMDCOMPLETE;
#pragma pack()
#endif /* # ifdef VBOX_WITH_VIDEOHWACCEL */

#pragma pack(1)
typedef enum
{
    VBVAHOSTCMD_OP_EVENT = 1,
    VBVAHOSTCMD_OP_CUSTOM
}VBVAHOSTCMD_OP_TYPE;

typedef struct VBVAHOSTCMDEVENT
{
    uint64_t pEvent;
}VBVAHOSTCMDEVENT;


typedef struct VBVAHOSTCMD
{
    /* destination ID if >=0 specifies display index, otherwize the command is directed to the miniport */
    int32_t iDstID;
    int32_t customOpCode;
    union
    {
        struct VBVAHOSTCMD *pNext;
        uint32_t             offNext;
        uint64_t Data; /* the body is 64-bit aligned */
    } u;
    char body[1];
} VBVAHOSTCMD;

#define VBVAHOSTCMD_SIZE(a_cb)                  (sizeof(VBVAHOSTCMD) + (a_cb))
#define VBVAHOSTCMD_BODY(a_pCmd, a_TypeBody)    ((a_TypeBody RT_UNTRUSTED_VOLATILE_HSTGST *)&(a_pCmd)->body[0])
#define VBVAHOSTCMD_HDR(a_pBody) \
    ( (VBVAHOSTCMD RT_UNTRUSTED_VOLATILE_HSTGST *)( (uint8_t *)(a_pBody) - RT_OFFSETOF(VBVAHOSTCMD, body)) )
#define VBVAHOSTCMD_HDRSIZE                     (RT_OFFSETOF(VBVAHOSTCMD, body))

#pragma pack()

/* VBVACONF32::u32Index */
#define VBOX_VBVA_CONF32_MONITOR_COUNT  0
#define VBOX_VBVA_CONF32_HOST_HEAP_SIZE 1
/** Returns VINF_SUCCESS if the host can report mode hints via VBVA.
 * Set value to VERR_NOT_SUPPORTED before calling. */
#define VBOX_VBVA_CONF32_MODE_HINT_REPORTING  2
/** Returns VINF_SUCCESS if the host can report guest cursor enabled status via
 * VBVA.  Set value to VERR_NOT_SUPPORTED before calling. */
#define VBOX_VBVA_CONF32_GUEST_CURSOR_REPORTING  3
/** Returns the currently available host cursor capabilities.  Available if
 * VBVACONF32::VBOX_VBVA_CONF32_GUEST_CURSOR_REPORTING returns success.
 * @see VMMDevReqMouseStatus::mouseFeatures. */
#define VBOX_VBVA_CONF32_CURSOR_CAPABILITIES  4
/** Returns the supported flags in VBVAINFOSCREEN::u8Flags. */
#define VBOX_VBVA_CONF32_SCREEN_FLAGS 5
/** Returns the max size of VBVA record. */
#define VBOX_VBVA_CONF32_MAX_RECORD_SIZE 6

typedef struct VBVACONF32
{
    uint32_t u32Index;
    uint32_t u32Value;
} VBVACONF32;

/** Reserved for historical reasons. */
#define VBOX_VBVA_CURSOR_CAPABILITY_RESERVED0  RT_BIT(0)
/** Guest cursor capability: can the host show a hardware cursor at the host
 * pointer location? */
#define VBOX_VBVA_CURSOR_CAPABILITY_HARDWARE   RT_BIT(1)
/** Reserved for historical reasons. */
#define VBOX_VBVA_CURSOR_CAPABILITY_RESERVED2  RT_BIT(2)
/** Reserved for historical reasons.  Must always be unset. */
#define VBOX_VBVA_CURSOR_CAPABILITY_RESERVED3  RT_BIT(3)
/** Reserved for historical reasons. */
#define VBOX_VBVA_CURSOR_CAPABILITY_RESERVED4  RT_BIT(4)
/** Reserved for historical reasons. */
#define VBOX_VBVA_CURSOR_CAPABILITY_RESERVED5  RT_BIT(5)

typedef struct VBVAINFOVIEW
{
    /* Index of the screen, assigned by the guest. */
    uint32_t u32ViewIndex;

    /* The screen offset in VRAM, the framebuffer starts here. */
    uint32_t u32ViewOffset;

    /* The size of the VRAM memory that can be used for the view. */
    uint32_t u32ViewSize;

    /* The recommended maximum size of the VRAM memory for the screen. */
    uint32_t u32MaxScreenSize;
} VBVAINFOVIEW;

typedef struct VBVAINFOHEAP
{
    /* Absolute offset in VRAM of the start of the heap. */
    uint32_t u32HeapOffset;

    /* The size of the heap. */
    uint32_t u32HeapSize;

} VBVAINFOHEAP;

typedef struct VBVAFLUSH
{
    uint32_t u32Reserved;

} VBVAFLUSH;

typedef struct VBVACMDVBVASUBMIT
{
    uint32_t u32Reserved;
} VBVACMDVBVASUBMIT;

/* flush is requested because due to guest command buffer overflow */
#define VBVACMDVBVAFLUSH_F_GUEST_BUFFER_OVERFLOW 1

typedef struct VBVACMDVBVAFLUSH
{
    uint32_t u32Flags;
} VBVACMDVBVAFLUSH;


/* VBVAINFOSCREEN::u8Flags */
#define VBVA_SCREEN_F_NONE     0x0000
#define VBVA_SCREEN_F_ACTIVE   0x0001
/** The virtual monitor has been disabled by the guest and should be removed
 * by the host and ignored for purposes of pointer position calculation. */
#define VBVA_SCREEN_F_DISABLED 0x0002
/** The virtual monitor has been blanked by the guest and should be blacked
 * out by the host using width, height, etc values from the VBVAINFOSCREEN request. */
#define VBVA_SCREEN_F_BLANK    0x0004
/** The virtual monitor has been blanked by the guest and should be blacked
 * out by the host using the previous mode values for width. height, etc. */
#define VBVA_SCREEN_F_BLANK2   0x0008

typedef struct VBVAINFOSCREEN
{
    /* Which view contains the screen. */
    uint32_t u32ViewIndex;

    /* Physical X origin relative to the primary screen. */
    int32_t i32OriginX;

    /* Physical Y origin relative to the primary screen. */
    int32_t i32OriginY;

    /* Offset of visible framebuffer relative to the framebuffer start. */
    uint32_t u32StartOffset;

    /* The scan line size in bytes. */
    uint32_t u32LineSize;

    /* Width of the screen. */
    uint32_t u32Width;

    /* Height of the screen. */
    uint32_t u32Height;

    /* Color depth. */
    uint16_t u16BitsPerPixel;

    /* VBVA_SCREEN_F_* */
    uint16_t u16Flags;
} VBVAINFOSCREEN;


/* VBVAENABLE::u32Flags */
#define VBVA_F_NONE    0x00000000
#define VBVA_F_ENABLE  0x00000001
#define VBVA_F_DISABLE 0x00000002
/* extended VBVA to be used with WDDM */
#define VBVA_F_EXTENDED 0x00000004
/* vbva offset is absolute VRAM offset */
#define VBVA_F_ABSOFFSET 0x00000008

typedef struct VBVAENABLE
{
    uint32_t u32Flags;
    uint32_t u32Offset;
    int32_t  i32Result;
} VBVAENABLE;

typedef struct VBVAENABLE_EX
{
    VBVAENABLE Base;
    uint32_t u32ScreenId;
} VBVAENABLE_EX;


typedef struct VBVAMOUSEPOINTERSHAPE
{
    /* The host result. */
    int32_t i32Result;

    /* VBOX_MOUSE_POINTER_* bit flags. */
    uint32_t fu32Flags;

    /* X coordinate of the hot spot. */
    uint32_t u32HotX;

    /* Y coordinate of the hot spot. */
    uint32_t u32HotY;

    /* Width of the pointer in pixels. */
    uint32_t u32Width;

    /* Height of the pointer in scanlines. */
    uint32_t u32Height;

    /* Pointer data.
     *
     ****
     * The data consists of 1 bpp AND mask followed by 32 bpp XOR (color) mask.
     *
     * For pointers without alpha channel the XOR mask pixels are 32 bit values: (lsb)BGR0(msb).
     * For pointers with alpha channel the XOR mask consists of (lsb)BGRA(msb) 32 bit values.
     *
     * Guest driver must create the AND mask for pointers with alpha channel, so if host does not
     * support alpha, the pointer could be displayed as a normal color pointer. The AND mask can
     * be constructed from alpha values. For example alpha value >= 0xf0 means bit 0 in the AND mask.
     *
     * The AND mask is 1 bpp bitmap with byte aligned scanlines. Size of AND mask,
     * therefore, is cbAnd = (width + 7) / 8 * height. The padding bits at the
     * end of any scanline are undefined.
     *
     * The XOR mask follows the AND mask on the next 4 bytes aligned offset:
     * uint8_t *pXor = pAnd + (cbAnd + 3) & ~3
     * Bytes in the gap between the AND and the XOR mask are undefined.
     * XOR mask scanlines have no gap between them and size of XOR mask is:
     * cXor = width * 4 * height.
     ****
     *
     * Preallocate 4 bytes for accessing actual data as p->au8Data.
     */
    uint8_t au8Data[4];

} VBVAMOUSEPOINTERSHAPE;

/** @name VBVAMOUSEPOINTERSHAPE::fu32Flags
 * @note The VBOX_MOUSE_POINTER_* flags are used in the guest video driver,
 *       values must be <= 0x8000 and must not be changed. (try make more sense
 *       of this, please).
 * @{
 */
/** pointer is visible */
#define VBOX_MOUSE_POINTER_VISIBLE (0x0001)
/** pointer has alpha channel */
#define VBOX_MOUSE_POINTER_ALPHA   (0x0002)
/** pointerData contains new pointer shape */
#define VBOX_MOUSE_POINTER_SHAPE   (0x0004)
/** @} */

/* the guest driver can handle asynch guest cmd completion by reading the command offset from io port */
#define VBVACAPS_COMPLETEGCMD_BY_IOREAD 0x00000001
/* the guest driver can handle video adapter IRQs */
#define VBVACAPS_IRQ                    0x00000002
/** The guest can read video mode hints sent via VBVA. */
#define VBVACAPS_VIDEO_MODE_HINTS       0x00000004
/** The guest can switch to a software cursor on demand. */
#define VBVACAPS_DISABLE_CURSOR_INTEGRATION 0x00000008
/** The guest does not depend on host handling the VBE registers. */
#define VBVACAPS_USE_VBVA_ONLY 0x00000010
typedef struct VBVACAPS
{
    int32_t rc;
    uint32_t fCaps;
} VBVACAPS;

/* makes graphics device generate IRQ on VSYNC */
#define VBVASCANLINECFG_ENABLE_VSYNC_IRQ        0x00000001
/* guest driver may request the current scanline */
#define VBVASCANLINECFG_ENABLE_SCANLINE_INFO    0x00000002
/* request the current refresh period, returned in u32RefreshPeriodMs */
#define VBVASCANLINECFG_QUERY_REFRESH_PERIOD    0x00000004
/* set new refresh period specified in u32RefreshPeriodMs.
 * if used with VBVASCANLINECFG_QUERY_REFRESH_PERIOD,
 * u32RefreshPeriodMs is set to the previous refresh period on return */
#define VBVASCANLINECFG_SET_REFRESH_PERIOD      0x00000008

typedef struct VBVASCANLINECFG
{
    int32_t rc;
    uint32_t fFlags;
    uint32_t u32RefreshPeriodMs;
    uint32_t u32Reserved;
} VBVASCANLINECFG;

typedef struct VBVASCANLINEINFO
{
    int32_t rc;
    uint32_t u32ScreenId;
    uint32_t u32InVBlank;
    uint32_t u32ScanLine;
} VBVASCANLINEINFO;

/** Query the most recent mode hints received from the host. */
typedef struct VBVAQUERYMODEHINTS
{
    /** The maximum number of screens to return hints for. */
    uint16_t cHintsQueried;
    /** The size of the mode hint structures directly following this one. */
    uint16_t cbHintStructureGuest;
    /** The return code for the operation.  Initialise to VERR_NOT_SUPPORTED. */
    int32_t  rc;
} VBVAQUERYMODEHINTS;

/** Structure in which a mode hint is returned.  The guest allocates an array
 *  of these immediately after the VBVAQUERYMODEHINTS structure.  To accomodate
 *  future extensions, the VBVAQUERYMODEHINTS structure specifies the size of
 *  the VBVAMODEHINT structures allocated by the guest, and the host only fills
 *  out structure elements which fit into that size.  The host should fill any
 *  unused members (e.g. dx, dy) or structure space on the end with ~0.  The
 *  whole structure can legally be set to ~0 to skip a screen. */
typedef struct VBVAMODEHINT
{
    uint32_t magic;
    uint32_t cx;
    uint32_t cy;
    uint32_t cBPP;  /* Which has never been used... */
    uint32_t cDisplay;
    uint32_t dx;  /**< X offset into the virtual frame-buffer. */
    uint32_t dy;  /**< Y offset into the virtual frame-buffer. */
    uint32_t fEnabled;  /* Not fFlags.  Add new members for new flags. */
} VBVAMODEHINT;

#define VBVAMODEHINT_MAGIC UINT32_C(0x0801add9)

/** Report the rectangle relative to which absolute pointer events should be
 *  expressed.  This information remains valid until the next VBVA resize event
 *  for any screen, at which time it is reset to the bounding rectangle of all
 *  virtual screens and must be re-set.
 *  @see VBVA_REPORT_INPUT_MAPPING. */
typedef struct VBVAREPORTINPUTMAPPING
{
    int32_t x;    /**< Upper left X co-ordinate relative to the first screen. */
    int32_t y;    /**< Upper left Y co-ordinate relative to the first screen. */
    uint32_t cx;  /**< Rectangle width. */
    uint32_t cy;  /**< Rectangle height. */
} VBVAREPORTINPUTMAPPING;

/** Report the guest cursor position and query the host one.  The host may wish
 *  to use the guest information to re-position its own cursor, particularly
 *  when the cursor is captured and the guest does not support switching to a
 *  software cursor.  After every mode switch the guest must signal that it
 *  supports sending position information by sending an event with
 *  @a fReportPosition set to false.
 *  @see VBVA_CURSOR_POSITION */
typedef struct VBVACURSORPOSITION
{
    uint32_t fReportPosition;  /**< Are we reporting a position? */
    uint32_t x;                /**< Guest cursor X position */
    uint32_t y;                /**< Guest cursor Y position */
} VBVACURSORPOSITION;

#pragma pack()

typedef uint64_t VBOXVIDEOOFFSET;

#define VBOXVIDEOOFFSET_VOID ((VBOXVIDEOOFFSET)~0)

#pragma pack(1)

/*
 * VBOXSHGSMI made on top HGSMI and allows receiving notifications
 * about G->H command completion
 */
/* SHGSMI command header */
typedef struct VBOXSHGSMIHEADER
{
    uint64_t pvNext;    /*<- completion processing queue */
    uint32_t fFlags;    /*<- see VBOXSHGSMI_FLAG_XXX Flags */
    uint32_t cRefs;     /*<- command referece count */
    uint64_t u64Info1;  /*<- contents depends on the fFlags value */
    uint64_t u64Info2;  /*<- contents depends on the fFlags value */
} VBOXSHGSMIHEADER, *PVBOXSHGSMIHEADER;

typedef enum
{
    VBOXVDMACMD_TYPE_UNDEFINED         = 0,
    VBOXVDMACMD_TYPE_DMA_PRESENT_BLT   = 1,
    VBOXVDMACMD_TYPE_DMA_BPB_TRANSFER,
    VBOXVDMACMD_TYPE_DMA_BPB_FILL,
    VBOXVDMACMD_TYPE_DMA_PRESENT_SHADOW2PRIMARY,
    VBOXVDMACMD_TYPE_DMA_PRESENT_CLRFILL,
    VBOXVDMACMD_TYPE_DMA_PRESENT_FLIP,
    VBOXVDMACMD_TYPE_DMA_NOP,
    VBOXVDMACMD_TYPE_CHROMIUM_CMD, /* chromium cmd */
    VBOXVDMACMD_TYPE_DMA_BPB_TRANSFER_VRAMSYS,
    VBOXVDMACMD_TYPE_CHILD_STATUS_IRQ /* make the device notify child (monitor) state change IRQ */
} VBOXVDMACMD_TYPE;

#pragma pack()

/* the command processing was asynch, set by the host to indicate asynch command completion
 * must not be cleared once set, the command completion is performed by issuing a host->guest completion command
 * while keeping this flag unchanged */
#define VBOXSHGSMI_FLAG_HG_ASYNCH               0x00010000
#if 0
/* if set     - asynch completion is performed by issuing the event,
 * if cleared - asynch completion is performed by calling a callback */
#define VBOXSHGSMI_FLAG_GH_ASYNCH_EVENT         0x00000001
#endif
/* issue interrupt on asynch completion, used for critical G->H commands,
 * i.e. for completion of which guest is waiting. */
#define VBOXSHGSMI_FLAG_GH_ASYNCH_IRQ           0x00000002
/* guest does not do any op on completion of this command,
 * the host may copy the command and indicate that it does not need the command anymore
 * by not setting VBOXSHGSMI_FLAG_HG_ASYNCH */
#define VBOXSHGSMI_FLAG_GH_ASYNCH_NOCOMPLETION  0x00000004
/* guest requires the command to be processed asynchronously,
 * not setting VBOXSHGSMI_FLAG_HG_ASYNCH by the host in this case is treated as command failure */
#define VBOXSHGSMI_FLAG_GH_ASYNCH_FORCE         0x00000008
/* force IRQ on cmd completion */
#define VBOXSHGSMI_FLAG_GH_ASYNCH_IRQ_FORCE     0x00000010
/* an IRQ-level callback is associated with the command */
#define VBOXSHGSMI_FLAG_GH_ASYNCH_CALLBACK_IRQ  0x00000020
/* guest expects this command to be completed synchronously */
#define VBOXSHGSMI_FLAG_GH_SYNCH                0x00000040


DECLINLINE(uint8_t RT_UNTRUSTED_VOLATILE_GUEST *)
VBoxSHGSMIBufferData(const VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_GUEST *pHeader)
{
    return (uint8_t RT_UNTRUSTED_VOLATILE_GUEST *)pHeader + sizeof(VBOXSHGSMIHEADER);
}

#define VBoxSHGSMIBufferHeaderSize() (sizeof(VBOXSHGSMIHEADER))

DECLINLINE(VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_GUEST *) VBoxSHGSMIBufferHeader(const void RT_UNTRUSTED_VOLATILE_GUEST *pvData)
{
    return (VBOXSHGSMIHEADER RT_UNTRUSTED_VOLATILE_GUEST *)((uintptr_t)pvData - sizeof(VBOXSHGSMIHEADER));
}

#ifdef VBOX_WITH_VDMA
# pragma pack(1)

/* VDMA - Video DMA */

/* VDMA Control API */
/* VBOXVDMA_CTL::u32Flags */
typedef enum
{
    VBOXVDMA_CTL_TYPE_NONE = 0,
    VBOXVDMA_CTL_TYPE_ENABLE,
    VBOXVDMA_CTL_TYPE_DISABLE,
    VBOXVDMA_CTL_TYPE_FLUSH,
    VBOXVDMA_CTL_TYPE_WATCHDOG,
    VBOXVDMA_CTL_TYPE_END
} VBOXVDMA_CTL_TYPE;

typedef struct VBOXVDMA_CTL
{
    VBOXVDMA_CTL_TYPE enmCtl;
    uint32_t u32Offset;
    int32_t  i32Result;
} VBOXVDMA_CTL;

/* VBOXVDMACBUF_DR::phBuf specifies offset in VRAM */
#define VBOXVDMACBUF_FLAG_BUF_VRAM_OFFSET 0x00000001
/* command buffer follows the VBOXVDMACBUF_DR in VRAM, VBOXVDMACBUF_DR::phBuf is ignored */
#define VBOXVDMACBUF_FLAG_BUF_FOLLOWS_DR  0x00000002

/**
 * We can not submit the DMA command via VRAM since we do not have control over
 * DMA command buffer [de]allocation, i.e. we only control the buffer contents.
 * In other words the system may call one of our callbacks to fill a command buffer
 * with the necessary commands and then discard the buffer w/o any notification.
 *
 * We have only DMA command buffer physical address at submission time.
 *
 * so the only way is to */
typedef struct VBOXVDMACBUF_DR
{
    uint16_t fFlags;
    uint16_t cbBuf;
    /* RT_SUCCESS()     - on success
     * VERR_INTERRUPTED - on preemption
     * VERR_xxx         - on error */
    int32_t  rc;
    union
    {
        uint64_t phBuf;
        VBOXVIDEOOFFSET offVramBuf;
    } Location;
    uint64_t aGuestData[7];
} VBOXVDMACBUF_DR, *PVBOXVDMACBUF_DR;

#define VBOXVDMACBUF_DR_TAIL(a_pCmd, a_TailType)       \
    ( (a_TailType      RT_UNTRUSTED_VOLATILE_HSTGST *)( ((uint8_t*)(a_pCmd)) + sizeof(VBOXVDMACBUF_DR)) )
#define VBOXVDMACBUF_DR_FROM_TAIL(a_pCmd) \
    ( (VBOXVDMACBUF_DR RT_UNTRUSTED_VOLATILE_HSTGST *)( ((uint8_t*)(a_pCmd)) - sizeof(VBOXVDMACBUF_DR)) )

typedef struct VBOXVDMACMD
{
    VBOXVDMACMD_TYPE enmType;
    uint32_t u32CmdSpecific;
} VBOXVDMACMD;

#define VBOXVDMACMD_HEADER_SIZE()                   sizeof(VBOXVDMACMD)
#define VBOXVDMACMD_SIZE_FROMBODYSIZE(_s)           ((uint32_t)(VBOXVDMACMD_HEADER_SIZE() + (_s)))
#define VBOXVDMACMD_SIZE(_t)                        (VBOXVDMACMD_SIZE_FROMBODYSIZE(sizeof(_t)))
#define VBOXVDMACMD_BODY(a_pCmd, a_TypeBody)        \
    ( (a_TypeBody RT_UNTRUSTED_VOLATILE_HSTGST  *)( ((uint8_t *)(a_pCmd)) + VBOXVDMACMD_HEADER_SIZE()) )
#define VBOXVDMACMD_BODY_SIZE(_s)                   ( (_s) - VBOXVDMACMD_HEADER_SIZE() )
#define VBOXVDMACMD_FROM_BODY(a_pBody)                \
    ( (VBOXVDMACMD RT_UNTRUSTED_VOLATILE_HSTGST *)( ((uint8_t *)(a_pBody)) - VBOXVDMACMD_HEADER_SIZE()) )
#define VBOXVDMACMD_BODY_FIELD_OFFSET(_ot, _t, _f)  ( (_ot)(uintptr_t)( VBOXVDMACMD_BODY(0, uint8_t) + RT_UOFFSETOF_DYN(_t, _f) ) )

# pragma pack()
#endif /* #ifdef VBOX_WITH_VDMA */


#define VBOXVDMA_CHILD_STATUS_F_CONNECTED    0x01
#define VBOXVDMA_CHILD_STATUS_F_DISCONNECTED 0x02
#define VBOXVDMA_CHILD_STATUS_F_ROTATED      0x04

typedef struct VBOXVDMA_CHILD_STATUS
{
    uint32_t iChild;
    uint8_t  fFlags;
    uint8_t  u8RotationAngle;
    uint16_t u16Reserved;
} VBOXVDMA_CHILD_STATUS, *PVBOXVDMA_CHILD_STATUS;

/* apply the aInfos are applied to all targets, the iTarget is ignored */
#define VBOXVDMACMD_CHILD_STATUS_IRQ_F_APPLY_TO_ALL 0x00000001

typedef struct VBOXVDMACMD_CHILD_STATUS_IRQ
{
    uint32_t cInfos;
    uint32_t fFlags;
    VBOXVDMA_CHILD_STATUS aInfos[1];
} VBOXVDMACMD_CHILD_STATUS_IRQ, *PVBOXVDMACMD_CHILD_STATUS_IRQ;

#define VBOXCMDVBVA_SCREENMAP_SIZE(_elType) ((VBOX_VIDEO_MAX_SCREENS + sizeof (_elType) - 1) / sizeof (_elType))
#define VBOXCMDVBVA_SCREENMAP_DECL(_elType, _name) _elType _name[VBOXCMDVBVA_SCREENMAP_SIZE(_elType)]

#endif /* !VBOX_INCLUDED_Graphics_VBoxVideo_h */

