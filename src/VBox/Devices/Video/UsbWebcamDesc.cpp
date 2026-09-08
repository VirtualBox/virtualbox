/* $Id: UsbWebcamDesc.cpp 115186 2026-09-08 09:34:46Z vitali.pelenjow@oracle.com $ */
/** @file
 * USB Webcam Device Emulation - USB descriptors.
 */

/*
 * Copyright (C) 2013-2026 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_USB_WEBCAM

#include <iprt/mem.h>
#include <iprt/string.h>

#include <iprt/errcore.h>
#include <VBox/param.h>
#include <VBox/vmm/pdmusb.h>

#include "UsbWebcam.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define USBWEBCAM_STR_ID_MANUFACTURER   1
#define USBWEBCAM_STR_ID_PRODUCT        2
#define USBWEBCAM_STR_ID_SERIAL         3

/* Number of supported video formats. */
#define USBWEBCAM_NUM_FORMATS 1


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
#pragma pack(1)
typedef struct WEBCAMDESCVCHEADER
{
    VUSBVDESCVCHEADER Core;
    uint8_t baInterfaceNr[1]; /* One VideoStreaming interface in this Video Interface Collection. */
} WEBCAMDESCVCHEADER;

/* Class-Specific VC Interface Descriptor. */
typedef struct WEBCAMDESCVCCLASS
{
    WEBCAMDESCVCHEADER      Header;
    VUSBVDESCCAMERATERMINAL Camera;
    VUSBVDESCPROCESSINGUNIT ProcessingUnit;
    VUSBVDESCOUTPUTTERMINAL OutputTerminal;
} WEBCAMDESCVCCLASS;

typedef struct WEBCAMDESCVSINPUTHEADER
{
    VUSBVDESCVSINPUTHEADER Core;
    uint8_t bmaControls[USBWEBCAM_NUM_FORMATS];
} WEBCAMDESCVSINPUTHEADER;

/* Class-Specific VS Interface Descriptor. */
typedef struct WEBCAMDESCVSCLASS
{
    WEBCAMDESCVSINPUTHEADER  InputHeader;
    VUSBVDESCVSMJPEGFMT      JpegFormat;
    /* Variable number of frames */
    VUSBVDESCVSMJPEGFRAME    aJpegFrames[1];
    /* VUSBVDESCVSCOLORMATCHING follows. */
} WEBCAMDESCVSCLASS;

typedef struct USBWEBCAMDESCRIPTORCACHE
{
    PDMUSBDESCCACHE     DescCache;

    /* PDMUSBDESCCACHE members */
    VUSBDESCDEVICE        DescDevice;
    VUSBDESCCONFIGEX      aConfigs[1];
    PDMUSBDESCCACHELANG   aLanguages[1];

    /* VUSBDESCCONFIGEX members */
    VUSBINTERFACE         aIfs[USBWEBCAM_NUM_INTERFACES]; /* Streaming and control. */

    /* PDMUSBDESCCACHELANG members */
    PDMUSBDESCCACHESTRING aStrings[3]; /* Number of string descriptors. */

    /* aIfs[0] Video Control interface. */
    VUSBDESCINTERFACEEX   aSettingsVC[1];
    VUSBDESCENDPOINTEX    aEndpointsVC[1];
    VUSBDESCIAD           iadVC;
    VUSBVDESCVCENDPOINT   epClassVC;

    /* aIfs[1] Video Streaming interface. */
    VUSBDESCINTERFACEEX   aSettingsVS[1];
    VUSBDESCENDPOINTEX    aEndpointsVS[1];

    /* Strings. */
    char achManufacturer[256];
    char achProduct[256];
    char achSerial[32];

    /* Class VC interface descriptor follows */

    /* Class VS interface descriptor follows */
} USBWEBCAMDESCRIPTORCACHE;

/* Default device description */
typedef struct WEBCAMFRAMEDESCDEFAULT
{
    VRDEVIDEOINFRAMEDESC frame;
    uint32_t au32Intervals[5];
} WEBCAMFRAMEDESCDEFAULT;

typedef struct WEBCAMDEVICEDESCDEFAULT
{
    VRDEVIDEOINDEVICEDESC desc;
    VRDEVIDEOINFORMATDESC format;
    WEBCAMFRAMEDESCDEFAULT aFrames[8];
} WEBCAMDEVICEDESCDEFAULT;
#pragma pack()


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#define UW_DECL_DEFAULT_FRAME(i, w, h)                                     \
{                                                                          \
    /* .frame */                                                           \
    {                                                                      \
        /* .cbFrame              */ sizeof(WEBCAMFRAMEDESCDEFAULT),        \
        /* .u8FrameId            */ (i),                                   \
        /* .u8FrameFlags         */ VRDE_VIDEOIN_F_FRM_DISCRETE_INTERVALS, \
        /* .u16Width             */ (w),                                   \
        /* .u16Height            */ (h),                                   \
        /* .u32NumFrameIntervals */ 5,                                     \
        /* .u32MinFrameInterval  */ 333333,                                \
        /* .u32MaxFrameInterval  */ 2000000                                \
    },                                                                     \
    /* .au32Intervals[] */ { 333333, 500000, 666666, 1000000, 2000000 }    \
}

static WEBCAMDEVICEDESCDEFAULT g_defaultDeviceDesc =
{
    /* .desc */
    {
        /* .u16ObjectiveFocalLengthMin */ 0,
        /* .u16ObjectiveFocalLengthMax */ 0,
        /* .u16OcularFocalLength       */ 0,
        /* .u16MaxMultiplier           */ 0,
        /* .fu32CameraControls         */ 0,
        /* .fu32ProcessingControls     */ VRDE_VIDEOIN_F_PU_CTRL_BRIGHTNESS,
        /* .fu8DeviceCaps              */ 0,
        /* .u8NumFormats               */ 1,
        /* .cbExt                      */ 0
    },
    /* .format */
    {
        /* .cbFormat      */ sizeof(VRDEVIDEOINFORMATDESC),
        /* .u8FormatId    */ 1,
        /* .u8FormatType  */ VRDE_VIDEOIN_FORMAT_MJPEG,
        /* .u8FormatFlags */ 0,
        /* .u8NumFrames   */ 8,
        /* .u16Reserved   */ 0
    },
    /* .aFrames[] */
    {
        UW_DECL_DEFAULT_FRAME(1,  640, 480),
        UW_DECL_DEFAULT_FRAME(2,  160, 120),
        UW_DECL_DEFAULT_FRAME(3,  320, 240),
        UW_DECL_DEFAULT_FRAME(4,  432, 240),
        UW_DECL_DEFAULT_FRAME(5,  864, 480),
        UW_DECL_DEFAULT_FRAME(6,  960, 720),
        UW_DECL_DEFAULT_FRAME(7, 1280, 720),
        UW_DECL_DEFAULT_FRAME(8, 1920,1080)
    }
};

#undef UW_DECL_DEFAULT_FRAME

static int uwcDeviceDescFindFormat(const VRDEVIDEOINFORMATDESC **ppFormatFound,
                                   uint32_t *pcbFormatFound,
                                   const VRDEVIDEOINDEVICEDESC *pDeviceDesc,
                                   uint32_t cbDeviceDesc,
                                   uint8_t u8FormatType)
{
    int rc = VINF_SUCCESS;
    const VRDEVIDEOINFORMATDESC *pFormatFound = NULL;
    uint32_t cbFormatFound = 0;

    const uint8_t *pu8Data = (uint8_t *)pDeviceDesc;
    const uint8_t *pu8End = pu8Data + cbDeviceDesc;

    /* Verify the data and find the format description. */
    if (   (uintptr_t)pu8End - (uintptr_t)pu8Data < (uintptr_t)sizeof(VRDEVIDEOINDEVICEDESC)
        || (uintptr_t)pu8End - (uintptr_t)pu8Data < (uintptr_t)(sizeof(VRDEVIDEOINDEVICEDESC) + pDeviceDesc->cbExt))
    {
        UWLOG(("DEVICEDESC: %d\n", pu8End - pu8Data));
        rc = VERR_INVALID_PARAMETER;
    }
    else
    {
        UWLOG(("Focal %d-%d/%d mult %d CT 0x%08X PU 0x%08X caps 0x%02X formats %d cbExt %d\n",
               pDeviceDesc->u16ObjectiveFocalLengthMin,
               pDeviceDesc->u16ObjectiveFocalLengthMax,
               pDeviceDesc->u16OcularFocalLength,
               pDeviceDesc->u16MaxMultiplier,
               pDeviceDesc->fu32CameraControls,
               pDeviceDesc->fu32ProcessingControls,
               pDeviceDesc->fu8DeviceCaps,
               pDeviceDesc->u8NumFormats,
               pDeviceDesc->cbExt));

        pu8Data += sizeof(VRDEVIDEOINDEVICEDESC);

        /** @todo check ext */
        pu8Data += pDeviceDesc->cbExt;

        /* Scan the entire description and verify it. */
        uint8_t idxFormat;
        for (idxFormat = 0; idxFormat < pDeviceDesc->u8NumFormats && RT_SUCCESS(rc); idxFormat++)
        {
            const VRDEVIDEOINFORMATDESC *pFormat = (const VRDEVIDEOINFORMATDESC *)pu8Data;
            if (   (uintptr_t)pu8End - (uintptr_t)pu8Data < (uintptr_t)sizeof(VRDEVIDEOINFORMATDESC)
                || (uintptr_t)pu8End - (uintptr_t)pu8Data < (uintptr_t)pFormat->cbFormat)
            {
                UWLOG(("Format[%d]: %d\n", idxFormat, pu8End - pu8Data));
                rc = VERR_INVALID_PARAMETER;
                break;
            }

            UWLOG(("Format[%d]: cb %d, id %d, type 0x%02X, flags 0x%02X, frames %d, reserved 0x%04X\n",
                   idxFormat,
                   pFormat->cbFormat,
                   pFormat->u8FormatId,
                   pFormat->u8FormatType,
                   pFormat->u8FormatFlags,
                   pFormat->u8NumFrames,
                   pFormat->u16Reserved));

            /* Check if this is a format which is requested. */
            if (!pFormatFound)
            {
                if (pFormat->u8FormatType == u8FormatType)
                {
                    pFormatFound = pFormat;
                    cbFormatFound = pFormat->cbFormat;
                }
            }

            pu8Data += pFormat->cbFormat;

            uint8_t idxFrame;
            for (idxFrame = 0; idxFrame < pFormat->u8NumFrames; idxFrame++)
            {
                const VRDEVIDEOINFRAMEDESC *pFrame = (const VRDEVIDEOINFRAMEDESC *)pu8Data;
                if (   (uintptr_t)pu8End - (uintptr_t)pu8Data < (uintptr_t)sizeof(VRDEVIDEOINFRAMEDESC)
                    || (uintptr_t)pu8End - (uintptr_t)pu8Data < (uintptr_t)pFrame->cbFrame)
                {
                    UWLOG(("Format[%d]: Frame[%d]: %d\n", idxFormat, idxFrame, pu8End - pu8Data));
                    rc = VERR_INVALID_PARAMETER;
                    break;
                }

                /** @todo Verify pFrame content. */
                pu8Data += pFrame->cbFrame;

                if (pFormat == pFormatFound)
                    cbFormatFound += pFrame->cbFrame;
            }
        }
    }

    if (RT_SUCCESS(rc))
    {
        if (pFormatFound)
        {
            UWLOG(("Found format: id %d\n", pFormatFound->u8FormatId));
            *ppFormatFound = pFormatFound;
            *pcbFormatFound = cbFormatFound;
        }
        else
        {
            rc = VERR_NOT_FOUND;
        }
    }

    UWLOGFLEAVERC((rc));
    return rc;
}


static int usbWebcamBuildVSClassDescriptor(uint32_t u32MinFrameInterval, const VRDEVIDEOINFORMATDESC *pFormatMJPEG, uint32_t cbFormatMJPEG,
                                           uint8_t *pu8Desc, uint32_t cbDesc, uint32_t *pcbDesc)
{
    UWLOG(("cbDesc %d\n", cbDesc));

    const uint8_t *pu8Src = (const uint8_t *)pFormatMJPEG;
    const uint8_t *pu8SrcEnd = pu8Src + cbFormatMJPEG;
    uint8_t *pu8Dst = pu8Desc;
    uint8_t *pu8DstEnd = pu8Dst + cbDesc;

    /* VS class descriptor consists of:
     *   WEBCAMDESCVSINPUTHEADER  InputHeader;
     *   VUSBVDESCVSMJPEGFMT      JpegFormat;
     *   WEBCAMDESCVSMJPEGFRAME   aJpegFrames[number of frames];
     *   VUSBVDESCVSCOLORMATCHING ColorMatching;
     */

    AssertReturn((uintptr_t)pu8SrcEnd - (uintptr_t)pu8Src >= (uintptr_t)sizeof(VRDEVIDEOINFORMATDESC), VERR_INVALID_PARAMETER);
    AssertReturn((uintptr_t)pu8SrcEnd - (uintptr_t)pu8Src >= (uintptr_t)pFormatMJPEG->cbFormat, VERR_INVALID_PARAMETER);

    AssertReturn((uintptr_t)pu8DstEnd - (uintptr_t)pu8Dst >= (uintptr_t)sizeof(WEBCAMDESCVSINPUTHEADER), VERR_INVALID_PARAMETER);
    WEBCAMDESCVSINPUTHEADER *pInputHeader = (WEBCAMDESCVSINPUTHEADER *)pu8Dst;

    pInputHeader->Core.bLength             = sizeof(WEBCAMDESCVSINPUTHEADER);
    pInputHeader->Core.bDescriptorType     = VUSB_V_CS_INTERFACE;
    pInputHeader->Core.bDescriptorSubType  = VUSB_V_VS_HEADER;
    pInputHeader->Core.bNumFormats         = 1; /* JPEG only. */
    pInputHeader->Core.wTotalLenght        = 0; /* Updated later. */
    pInputHeader->Core.bEndpointAddress    = 0x82;
    pInputHeader->Core.bmInfo              = 0x01; /* Dynamic format change. */
    pInputHeader->Core.bTerminalLink       = 3; /* output terminal */
    pInputHeader->Core.bStillCaptureMethod = 0; /** @todo method 3? */
    pInputHeader->Core.bTriggerSupport     = 0; /** @todo */
    pInputHeader->Core.bTriggerUsage       = 0; /** @todo */
    pInputHeader->Core.bControlSize        = sizeof(uint8_t); /* Size of each bmaControls element. */
    RT_ZERO(pInputHeader->bmaControls);

    pu8Dst += sizeof(WEBCAMDESCVSINPUTHEADER);

    AssertReturn((uintptr_t)pu8DstEnd - (uintptr_t)pu8Dst >= (uintptr_t)sizeof(VUSBVDESCVSMJPEGFMT), VERR_INVALID_PARAMETER);
    VUSBVDESCVSMJPEGFMT *pJpegFormat = (VUSBVDESCVSMJPEGFMT *)pu8Dst;

    pJpegFormat->bLength                = sizeof(VUSBVDESCVSMJPEGFMT);
    pJpegFormat->bDescriptorType        = VUSB_V_CS_INTERFACE;
    pJpegFormat->bDescriptorSubType     = VUSB_V_VS_FORMAT_MJPEG;
    pJpegFormat->bFormatIndex           = 1;
    pJpegFormat->bNumFrameDescriptors   = pFormatMJPEG->u8NumFrames;
    pJpegFormat->bmFlags                = 0x01; /* MJPEG has fixed size samples. */
    pJpegFormat->bDefaultFrameIndex     = 1;
    pJpegFormat->bAspectRatioX          = 0;    /* Non-interlaced stream - not required. */
    pJpegFormat->bAspectRatioY          = 0;    /* Non-interlaced stream - not required. */
    pJpegFormat->bmInterlaceFlags       = 0x00; /* Non-interlaced stream */
    pJpegFormat->bCopyProtect           = 0;

    pu8Dst += sizeof(VUSBVDESCVSMJPEGFMT);

    pu8Src += pFormatMJPEG->cbFormat;

    uint8_t i;
    for (i = 1; i <= pJpegFormat->bNumFrameDescriptors; i++)
    {
        AssertReturn((uintptr_t)pu8SrcEnd - (uintptr_t)pu8Src >= (uintptr_t)sizeof(VRDEVIDEOINFRAMEDESC), VERR_INVALID_PARAMETER);
        const VRDEVIDEOINFRAMEDESC *pFrameSrc = (const VRDEVIDEOINFRAMEDESC *)pu8Src;

        if (pFrameSrc->u32NumFrameIntervals == 0)
        {
            return VERR_INVALID_PARAMETER;
        }

        AssertReturn((uintptr_t)pu8DstEnd - (uintptr_t)pu8Dst >= (uintptr_t)sizeof(VUSBVDESCVSMJPEGFRAME), VERR_INVALID_PARAMETER);
        VUSBVDESCVSMJPEGFRAME *pFrameDst = (VUSBVDESCVSMJPEGFRAME *)pu8Dst;

        pFrameDst->bLength                = 0; /* Updated later */
        pFrameDst->bDescriptorType        = VUSB_V_CS_INTERFACE;
        pFrameDst->bDescriptorSubType     = VUSB_V_VS_FRAME_JPEG;
        pFrameDst->bFrameIndex            = i;
        pFrameDst->bmCapabilities         = 0x00; /* Not fixed frame rate. No still image. */
        pFrameDst->wWidth                 = pFrameSrc->u16Width;
        pFrameDst->wHeight                = pFrameSrc->u16Height;
        pFrameDst->dwMinBitRate           = 0; /* Updated later */
        pFrameDst->dwMaxBitRate           = 0; /* Updated later */
        /* UVC1.1: "dwMaxVideoFrameSize field of the Video Probe and
         *          Commit control replaces this descriptor field."
         * Logitech webcam uses a half of uncompressed 32bpp bitmap.
         */
        pFrameDst->dwMaxVideoFrameBufSize = (uint32_t)pFrameDst->wWidth * (uint32_t)pFrameDst->wHeight * 4 / 2;
        pFrameDst->dwDefaultFrameInterval = pFrameSrc->u32MinFrameInterval;
        pFrameDst->bFrameIntervalType     = (pFrameSrc->u8FrameFlags & VRDE_VIDEOIN_F_FRM_DISCRETE_INTERVALS)?
                                                (uint8_t)pFrameSrc->u32NumFrameIntervals:
                                                0;

        pu8Dst += sizeof(VUSBVDESCVSMJPEGFRAME);
        pu8Src += sizeof(VRDEVIDEOINFRAMEDESC);

        uint16_t cbField;

        /* Intervals. */
        uint32_t *pau32IntervalsDst = (uint32_t *)pu8Dst;

        if (pFrameSrc->u8FrameFlags & VRDE_VIDEOIN_F_FRM_DISCRETE_INTERVALS)
        {
            /* Discrete frame intervals. */
            if (pFrameSrc->u8FrameFlags & VRDE_VIDEOIN_F_FRM_SIZE_OF_FIELDS)
            {
                AssertReturn((uintptr_t)pu8SrcEnd - (uintptr_t)pu8Src >= (uintptr_t)sizeof(uint16_t), VERR_INVALID_PARAMETER);
                cbField = *(uint16_t *)pu8Src;
                pu8Src += sizeof(uint16_t);
                AssertReturn(cbField / sizeof(uint32_t) >= pFrameSrc->u32NumFrameIntervals, VERR_INVALID_PARAMETER);
            }
            else
            {
                cbField = (uint16_t)(pFrameSrc->u32NumFrameIntervals * sizeof(uint32_t));
            }
            AssertReturn((uintptr_t)pu8SrcEnd - (uintptr_t)pu8Src >= (uintptr_t)cbField, VERR_INVALID_PARAMETER);

            UWLOG(("%u discrete intervals\n", pFrameSrc->u32NumFrameIntervals));

            AssertReturn((uintptr_t)pu8DstEnd - (uintptr_t)pu8Dst >= (uintptr_t)pFrameSrc->u32NumFrameIntervals * sizeof(uint32_t), VERR_INVALID_PARAMETER);

            /* Sort pau32IntervalsDst, shortest interval first, as required by UVC spec. */
            const uint32_t *pau32IntervalsSrc = (const uint32_t *)pu8Src;
            uint32_t j;
            for (j = 0; j < pFrameSrc->u32NumFrameIntervals; j++)
            {
                /* Dst is a sorted array, find the index where the src interval should be inserted. */
                uint32_t iDst;
                for (iDst = 0; iDst < j; iDst++)
                {
                    if (pau32IntervalsDst[iDst] > pau32IntervalsSrc[j])
                    {
                        break;
                    }
                }
                if (iDst < j)
                {
                    /* Make place for new element by shifting existing ones. */
                    memmove(&pau32IntervalsDst[iDst + 1], &pau32IntervalsDst[iDst], sizeof(uint32_t) * (j - iDst));
                }
                pau32IntervalsDst[iDst] = pau32IntervalsSrc[j];
            }

            /* Remove too short intervals. */
            for (j = 0; j < pFrameSrc->u32NumFrameIntervals; j++)
            {
                if (pau32IntervalsDst[j] >= u32MinFrameInterval)
                {
                    break;
                }
            }

            /* j is the index if the shortest allowed interval. */
            if (j == pFrameSrc->u32NumFrameIntervals)
            {
                /* Actually all intervals are too short. Keep longest interval anyway. */
                pFrameDst->bFrameIntervalType = 1;
                pau32IntervalsDst[0] = pau32IntervalsDst[pFrameSrc->u32NumFrameIntervals - 1];
            }
            else
            {
                pFrameDst->bFrameIntervalType = (uint8_t)(pFrameSrc->u32NumFrameIntervals - j);
                if (j != 0)
                {
                    /* Need to remove intervals [0;j-1]*/
                    memmove(&pau32IntervalsDst[0],
                            &pau32IntervalsDst[j],
                            sizeof(uint32_t) * pFrameDst->bFrameIntervalType);
                }

            }

            if (UWLOGENABLED())
            {
                UWLOG(("Intervals: "));
                for (j = 0; j < pFrameDst->bFrameIntervalType; j++)
                {
                    UWLOG_NO_FN((" %d", pau32IntervalsDst[j]));
                }
                UWLOG_NO_FN(("\n"));
            }

            pu8Dst += pFrameDst->bFrameIntervalType * sizeof(uint32_t);
            pu8Src += cbField;
        }
        else
        {
            /* Continuous frame intervals. */
            AssertReturn((uintptr_t)pu8DstEnd - (uintptr_t)pu8Dst >= (uintptr_t)3 * sizeof(uint32_t), VERR_INVALID_PARAMETER);
            pau32IntervalsDst[0] = pFrameSrc->u32MinFrameInterval; /* dwMinFrameInterval */
            pau32IntervalsDst[1] = pFrameSrc->u32MaxFrameInterval; /* dwMaxFrameInterval */

            uint32_t dwFrameIntervalStep = pFrameSrc->u32MaxFrameInterval - pFrameSrc->u32MinFrameInterval;
            if (pFrameSrc->u32NumFrameIntervals > 2)
            {
                dwFrameIntervalStep /= pFrameSrc->u32NumFrameIntervals - 1;
            }

            pau32IntervalsDst[2] = dwFrameIntervalStep; /* dwFrameIntervalStep */

            pu8Dst += 3 * sizeof(uint32_t);
        }

        /* Bit rates, default values are like in a Logitech webcam. */
        pFrameDst->dwMinBitRate = pFrameDst->dwMaxVideoFrameBufSize * 8;
        if (pFrameSrc->u32MaxFrameInterval != 0)
        {
            pFrameDst->dwMinBitRate *= (10000000 / pFrameSrc->u32MaxFrameInterval);
        }
        pFrameDst->dwMaxBitRate = pFrameDst->dwMaxVideoFrameBufSize * 8;
        if (pFrameSrc->u32MinFrameInterval != 0)
        {
            pFrameDst->dwMaxBitRate *= (10000000 / pFrameSrc->u32MinFrameInterval);
        }

        if (pFrameSrc->u8FrameFlags & VRDE_VIDEOIN_F_FRM_BITRATE)
        {
            if (pFrameSrc->u8FrameFlags & VRDE_VIDEOIN_F_FRM_SIZE_OF_FIELDS)
            {
                AssertReturn((uintptr_t)pu8SrcEnd - (uintptr_t)pu8Src >= (uintptr_t)sizeof(uint16_t), VERR_INVALID_PARAMETER);
                cbField = *(uint16_t *)pu8Src;
                pu8Src += sizeof(uint16_t);
                AssertReturn(cbField / sizeof(uint32_t) >= 2, VERR_INVALID_PARAMETER);
            }
            else
            {
                cbField = (uint16_t)(2 * sizeof(uint32_t));
            }
            AssertReturn((uintptr_t)pu8SrcEnd - (uintptr_t)pu8Src >= (uintptr_t)cbField, VERR_INVALID_PARAMETER);

            const uint32_t *pau32BitRatesSrc = (const uint32_t *)pu8Src;
            pFrameDst->dwMinBitRate = pau32BitRatesSrc[0];
            pFrameDst->dwMaxBitRate = pau32BitRatesSrc[1];
            pu8Src += cbField;
        }

        pFrameDst->bLength = (uint8_t)((uintptr_t)pu8Dst - (uintptr_t)pFrameDst);

        pu8Src = (uint8_t *)pFrameSrc + pFrameSrc->cbFrame;
    }

    AssertReturn((uintptr_t)pu8DstEnd - (uintptr_t)pu8Dst >= (uintptr_t)sizeof(VUSBVDESCVSCOLORMATCHING), VERR_INVALID_PARAMETER);
    VUSBVDESCVSCOLORMATCHING *pColorMatching = (VUSBVDESCVSCOLORMATCHING *)pu8Dst;

    pColorMatching->bLength                  = sizeof(VUSBVDESCVSCOLORMATCHING);
    pColorMatching->bDescriptorType          = VUSB_V_CS_INTERFACE;
    pColorMatching->bDescriptorSubType       = VUSB_V_VS_COLORFORMAT;
    pColorMatching->bColorPrimaries          = 1; /* BT.709, sRGB (default) */
    pColorMatching->bTransferCharacteristics = 1; /* BT.709 (default) */
    pColorMatching->bMatrixCoefficients      = 4; /* SMPTE 170M (BT.601, default) */

    pu8Dst += sizeof(VUSBVDESCVSCOLORMATCHING);

    AssertReturn((uintptr_t)pu8DstEnd - (uintptr_t)pu8Dst <= (uintptr_t)UINT16_MAX, VERR_INVALID_PARAMETER);
    pInputHeader->Core.wTotalLenght = (uint16_t)(pu8Dst - pu8Desc);

    UWLOG(("Built descriptor %d bytes\n%.*Rhxd\n",
           pInputHeader->Core.wTotalLenght, pInputHeader->Core.wTotalLenght, pu8Desc));

    *pcbDesc = pInputHeader->Core.wTotalLenght;

    return VINF_SUCCESS;
}


static int uwcBuildClassDescVS(uint32_t u32MinFrameInterval,
                               const VRDEVIDEOINFORMATDESC *pFormatMJPEG,
                               uint32_t cbFormatMJPEG,
                               void **ppvClassVC,
                               uint32_t *pcbClassVC)
{
    int rc= VINF_SUCCESS;

    uint32_t cbDescAlloc = 64*_1K;
    uint8_t *pu8DescAlloc = (uint8_t *)RTMemAllocZ(cbDescAlloc);

    if (pu8DescAlloc)
    {
        uint32_t cbDesc = 0;
        rc = usbWebcamBuildVSClassDescriptor(u32MinFrameInterval, pFormatMJPEG, cbFormatMJPEG, pu8DescAlloc, cbDescAlloc, &cbDesc);

        if (RT_SUCCESS(rc))
        {
            void *pv = RTMemRealloc(pu8DescAlloc, cbDesc);
            if (pv)
            {
                pu8DescAlloc = (uint8_t *)pv;

                *ppvClassVC = pu8DescAlloc;
                *pcbClassVC = cbDesc;
            }
            else
            {
                rc = VERR_NO_MEMORY;
            }
        }

        if (RT_FAILURE(rc))
        {
            RTMemFree(pu8DescAlloc);
        }
    }
    else
    {
        rc = VERR_NO_MEMORY;
    }

    return rc;
}


static int uwcBuildClassDescVC(void **ppvClassVC, uint32_t *pcbClassVC)
{
    uint32_t cbClass = sizeof(WEBCAMDESCVCCLASS);
    WEBCAMDESCVCCLASS *p = (WEBCAMDESCVCCLASS *)RTMemAllocZ(cbClass);
    if (!p)
    {
        return VERR_NO_MEMORY;
    }

    p->Header.Core.bLength            = sizeof(WEBCAMDESCVCHEADER); /* Includes baInterfaceNr. */
    p->Header.Core.bDescriptorType    = VUSB_V_CS_INTERFACE;
    p->Header.Core.bDescriptorSubType = VUSB_V_VC_HEADER;
    p->Header.Core.bcdUVC             = 0x0100;
    p->Header.Core.wTotalLength       = sizeof(WEBCAMDESCVCCLASS);
    p->Header.Core.dwClockFrequency   = 1000000; /* Deprecated. */
    p->Header.Core.bInCollection      = RT_ELEMENTS(p->Header.baInterfaceNr);
    p->Header.baInterfaceNr[0]        = 1; /* Interface Number of the VideoStreaming interface. */

    p->Camera.Core.bLength             = sizeof(VUSBVDESCCAMERATERMINAL);
    p->Camera.Core.bDescriptorType     = VUSB_V_CS_INTERFACE;
    p->Camera.Core.bDescriptorSubType  = VUSB_V_VC_INPUT_TERMINAL;
    p->Camera.Core.bTerminalID         = 1;
    p->Camera.Core.wTerminalType       = VUSB_V_ITT_CAMERA;
    p->Camera.Core.bAssocTerminal      = 0;
    p->Camera.Core.iTerminal           = 0;
    p->Camera.wObjectiveFocalLengthMin = 0; /* Not supported. */
    p->Camera.wObjectiveFocalLengthMax = 0; /* Not supported. */
    p->Camera.wOcularFocalLength       = 0; /* Not supported. */
    p->Camera.bControlSize             = sizeof(p->Camera.bmControls);
    /** @todo custom */
    p->Camera.bmControls[0]            = 0; /* Supported controls VUSB_V_CT_CTRL_* */
    p->Camera.bmControls[1]            = 0;
    p->Camera.bmControls[2]            = 0;

    p->ProcessingUnit.bLength            = sizeof(VUSBVDESCPROCESSINGUNIT);
    p->ProcessingUnit.bDescriptorType    = VUSB_V_CS_INTERFACE;
    p->ProcessingUnit.bDescriptorSubType = VUSB_V_VC_PROCESSING_UNIT;
    p->ProcessingUnit.bUnitID            = 2;
    p->ProcessingUnit.bSourceID          = 1; /* From Camera */
    p->ProcessingUnit.wMaxMultiplier     = 0; /* Not supported. */
    p->ProcessingUnit.bControlSize       = sizeof(p->ProcessingUnit.bmControls);
    p->ProcessingUnit.bmControls[0]      = VUSB_V_PU_CTRL_BRIGHTNESS;
    p->ProcessingUnit.bmControls[1]      = 0;
    p->ProcessingUnit.bmControls[2]      = 0;
    p->ProcessingUnit.iProcessing        = 0;

    p->OutputTerminal.bLength = sizeof(VUSBVDESCOUTPUTTERMINAL);
    p->OutputTerminal.bDescriptorType = VUSB_V_CS_INTERFACE;
    p->OutputTerminal.bDescriptorSubType = VUSB_V_VC_OUTPUT_TERMINAL;
    p->OutputTerminal.bTerminalID = 3;
    p->OutputTerminal.wTerminalType = VUSB_V_TT_STREAMING;
    p->OutputTerminal.bAssocTerminal = 0;
    p->OutputTerminal.bSourceID = 2; /* From Processing Unit */
    p->OutputTerminal.iTerminal = 0;

    *ppvClassVC = p;
    *pcbClassVC = cbClass;
    return VINF_SUCCESS;
}


/*
 * Fill backend-independent part of the descriptor cache.
 * pDC points to a buffer enough for sizeof(USBWEBCAMDESCRIPTORCACHE) + cbClassVC + cbClassVS
 */
static void uwcFillDescriptorCache(USBWEBCAMDESCRIPTORCACHE *pDC,
                                   const void *pvClassVC, uint32_t cbClassVC,
                                   const void *pvClassVS, uint32_t cbClassVS)
{
    /*
     * Video Control Interface
     */
    pDC->epClassVC.bLength            = sizeof(VUSBVDESCVCENDPOINT);
    pDC->epClassVC.bDescriptorType    = VUSB_V_CS_ENDPOINT;
    pDC->epClassVC.bDescriptorSubType = VUSB_V_EP_INTERRUPT;
    pDC->epClassVC.wMaxTransferSize   = 16;

    pDC->aEndpointsVC[0].Core.bLength          = sizeof(VUSBDESCENDPOINT);
    pDC->aEndpointsVC[0].Core.bDescriptorType  = VUSB_DT_ENDPOINT;
    pDC->aEndpointsVC[0].Core.bEndpointAddress = 0x81; /* IN endpoint 1 */
    pDC->aEndpointsVC[0].Core.bmAttributes     = 0x03; /* EP_INTERRUPT */
    pDC->aEndpointsVC[0].Core.wMaxPacketSize   = 16;
    pDC->aEndpointsVC[0].Core.bInterval        = 0x40; /* Poll every 64ms, full speed EP. */
    pDC->aEndpointsVC[0].pvMore                = NULL;
    pDC->aEndpointsVC[0].pvClass               = &pDC->epClassVC;
    pDC->aEndpointsVC[0].cbClass               = sizeof(VUSBVDESCVCENDPOINT);

    pDC->aSettingsVC[0].Core.bLength            = sizeof(VUSBDESCINTERFACE);
    pDC->aSettingsVC[0].Core.bDescriptorType    = VUSB_DT_INTERFACE;
    pDC->aSettingsVC[0].Core.bInterfaceNumber   = 0; /* Interface index */
    pDC->aSettingsVC[0].Core.bAlternateSetting  = 0; /* Setting index */
    pDC->aSettingsVC[0].Core.bNumEndpoints      = RT_ELEMENTS(pDC->aEndpointsVC);
    pDC->aSettingsVC[0].Core.bInterfaceClass    = VUSB_CC_VIDEO;
    pDC->aSettingsVC[0].Core.bInterfaceSubClass = VUSB_V_SC_VIDEOCONTROL;
    pDC->aSettingsVC[0].Core.bInterfaceProtocol = VUSB_PC_PROTOCOL_UNDEFINED;
    pDC->aSettingsVC[0].Core.iInterface         = USBWEBCAM_STR_ID_PRODUCT;
    pDC->aSettingsVC[0].pvMore                  = NULL;
    pDC->aSettingsVC[0].pvClass                 = (uint8_t *)pDC + sizeof(USBWEBCAMDESCRIPTORCACHE);
    pDC->aSettingsVC[0].cbClass                 = cbClassVC;
    pDC->aSettingsVC[0].paEndpoints             = &pDC->aEndpointsVC[0];
    pDC->aSettingsVC[0].pIAD                    = &pDC->iadVC;
    pDC->aSettingsVC[0].cbIAD                   = sizeof(VUSBDESCIAD);
    memcpy((void *)pDC->aSettingsVC[0].pvClass, pvClassVC, cbClassVC);

    /* UVC1.1: 2.4.1 Video Interface Collection
     * UVC1.1: "If the VideoControl interface is part of a Video Interface Collection,
     *         the iFunction field in the IAD and the iInterface field in the Standard
     *         VC interface descriptor for this Video Interface Collection must be equal."
     */
    pDC->iadVC.bLength           = sizeof(VUSBDESCIAD);
    pDC->iadVC.bDescriptorType   = VUSB_DT_INTERFACE_ASSOCIATION;
    pDC->iadVC.bFirstInterface   = 0; /* bInterfaceNumber of the VC interface */
    pDC->iadVC.bInterfaceCount   = 2; /* Control + Streaming */
    pDC->iadVC.bFunctionClass    = VUSB_CC_VIDEO;
    pDC->iadVC.bFunctionSubClass = VUSB_V_SC_VIDEO_INTERFACE_COLLECTION;
    pDC->iadVC.bFunctionProtocol = VUSB_PC_PROTOCOL_UNDEFINED;
    pDC->iadVC.iFunction         = USBWEBCAM_STR_ID_PRODUCT;

    /*
     * Video Streaming interface
     */
    pDC->aEndpointsVS[0].Core.bLength          = sizeof(VUSBDESCENDPOINT);
    pDC->aEndpointsVS[0].Core.bDescriptorType  = VUSB_DT_ENDPOINT;
    pDC->aEndpointsVS[0].Core.bEndpointAddress = 0x82; /* IN endpoint 2 */
    pDC->aEndpointsVS[0].Core.bmAttributes     = 0x02; /* EP_BULK */
    pDC->aEndpointsVS[0].Core.wMaxPacketSize   = 64;
    pDC->aEndpointsVS[0].Core.bInterval        = 0;
    pDC->aEndpointsVS[0].pvMore                = NULL;
    pDC->aEndpointsVS[0].pvClass               = NULL;
    pDC->aEndpointsVS[0].cbClass               = 0;

    /* UVC1.1: 2.4.3 VideoStreaming Interface
     * UVC1.1: "A VideoStreaming interface containing a bulk endpoint for streaming shall support
     *          only alternate setting zero."
     */
    pDC->aSettingsVS[0].Core.bLength            = sizeof(VUSBDESCINTERFACE);
    pDC->aSettingsVS[0].Core.bDescriptorType    = VUSB_DT_INTERFACE;
    pDC->aSettingsVS[0].Core.bInterfaceNumber   = 1; /* Interface index */
    pDC->aSettingsVS[0].Core.bAlternateSetting  = 0; /* Setting index */
    pDC->aSettingsVS[0].Core.bNumEndpoints      = RT_ELEMENTS(pDC->aEndpointsVS);
    pDC->aSettingsVS[0].Core.bInterfaceClass    = VUSB_CC_VIDEO;
    pDC->aSettingsVS[0].Core.bInterfaceSubClass = VUSB_V_SC_VIDEOSTREAMING;
    pDC->aSettingsVS[0].Core.bInterfaceProtocol = VUSB_PC_PROTOCOL_UNDEFINED;
    pDC->aSettingsVS[0].Core.iInterface         = 0;
    pDC->aSettingsVS[0].pvMore                  = NULL;
    pDC->aSettingsVS[0].pvClass                 = (uint8_t *)pDC->aSettingsVC[0].pvClass + cbClassVC;
    pDC->aSettingsVS[0].cbClass                 = cbClassVS;
    pDC->aSettingsVS[0].paEndpoints             = &pDC->aEndpointsVS[0];
    pDC->aSettingsVS[0].pIAD                    = NULL;
    pDC->aSettingsVS[0].cbIAD                   = 0;
    memcpy((void *)pDC->aSettingsVS[0].pvClass, pvClassVS, cbClassVS);

    /*
     * Descriptor cache
     */
    pDC->aIfs[0].paSettings = &pDC->aSettingsVC[0];
    pDC->aIfs[0].cSettings  = RT_ELEMENTS(pDC->aSettingsVC);
    pDC->aIfs[1].paSettings = &pDC->aSettingsVS[0];
    pDC->aIfs[1].cSettings  = RT_ELEMENTS(pDC->aSettingsVS);

    pDC->aStrings[0].idx = USBWEBCAM_STR_ID_MANUFACTURER;
    pDC->aStrings[0].psz = &pDC->achManufacturer[0];
    pDC->aStrings[1].idx = USBWEBCAM_STR_ID_PRODUCT;
    pDC->aStrings[1].psz = &pDC->achProduct[0];
    pDC->aStrings[2].idx = USBWEBCAM_STR_ID_SERIAL;
    pDC->aStrings[2].psz = &pDC->achSerial[0];

    pDC->DescDevice.bLength            = sizeof(VUSBDESCDEVICE);
    pDC->DescDevice.bDescriptorType    = VUSB_DT_DEVICE;
    pDC->DescDevice.bcdUSB             = 0x0200;
    pDC->DescDevice.bDeviceClass       = 0xEF; /* Miscellaneous device class */
    pDC->DescDevice.bDeviceSubClass    = 0x02; /* Common Class */
    pDC->DescDevice.bDeviceProtocol    = 0x01; /* Interface Association Descriptor */
    pDC->DescDevice.bMaxPacketSize0    = 0x10; /* Control endpoint packet-size */
    pDC->DescDevice.idVendor           = VBOX_PCI_VENDORID;
    pDC->DescDevice.idProduct          = 0x0030; /* Video camera product Id */
    pDC->DescDevice.bcdDevice          = 0x0100; /* Release: 1.0 */
    pDC->DescDevice.iManufacturer      = USBWEBCAM_STR_ID_MANUFACTURER;
    pDC->DescDevice.iProduct           = USBWEBCAM_STR_ID_PRODUCT;
    pDC->DescDevice.iSerialNumber      = USBWEBCAM_STR_ID_SERIAL;
    pDC->DescDevice.bNumConfigurations = RT_ELEMENTS(pDC->aConfigs);

    pDC->aConfigs[0].Core.bLength             = sizeof(VUSBDESCCONFIG);
    pDC->aConfigs[0].Core.bDescriptorType     = VUSB_DT_CONFIG;
    pDC->aConfigs[0].Core.wTotalLength        = 0; /* Recalculated by VUSB. */
    pDC->aConfigs[0].Core.bNumInterfaces      = 2; /* Control + Streaming. */
    pDC->aConfigs[0].Core.bConfigurationValue = 1; /* ID of this configuration */
    pDC->aConfigs[0].Core.iConfiguration      = 0; /* Unused. */
    pDC->aConfigs[0].Core.bmAttributes        = 0x80; /* Bus-powered device, no remote wakeup capability */
    pDC->aConfigs[0].Core.MaxPower            = 0xFA; /* Value from spec example */
    pDC->aConfigs[0].pvMore     = NULL;
    pDC->aConfigs[0].pvClass    = NULL;
    pDC->aConfigs[0].cbClass    = 0;
    pDC->aConfigs[0].paIfs      = &pDC->aIfs[0];
    pDC->aConfigs[0].pvOriginal = NULL;

    pDC->aLanguages[0].idLang    = 0x0409;
    pDC->aLanguages[0].cStrings  = RT_ELEMENTS(pDC->aStrings);
    pDC->aLanguages[0].paStrings = &pDC->aStrings[0];

    pDC->DescCache.pDevice                      = &pDC->DescDevice;
    pDC->DescCache.paConfigs                    = &pDC->aConfigs[0];
    pDC->DescCache.paLanguages                  = &pDC->aLanguages[0];
    pDC->DescCache.cLanguages                   = RT_ELEMENTS(pDC->aLanguages);
    pDC->DescCache.fUseCachedDescriptors        = true;
    pDC->DescCache.fUseCachedStringsDescriptors = true;

    return;
}

static void uwcParseExt(USBWEBCAMDESCRIPTORCACHE *pDC, const VRDEVIDEOINDEVICEDESC *pDeviceDesc)
{
    VRDEVIDEOINDEVICEFIELDHDR *pFieldHdr;

    const uint8_t *pu8Ext = (uint8_t *)&pDeviceDesc[1];
    uint16_t cbExtLeft = pDeviceDesc->cbExt;
    if (cbExtLeft < sizeof(VRDEVIDEOINDEVICEEXT))
    {
        return;
    }

    VRDEVIDEOINDEVICEEXT *pExt = (VRDEVIDEOINDEVICEEXT *)pu8Ext;

    pu8Ext += sizeof(VRDEVIDEOINDEVICEEXT);
    cbExtLeft -= sizeof(VRDEVIDEOINDEVICEEXT);

    /* Parse fields in the right order starting from bit 0. */
    if (pExt->fu32Fields & VRDE_VIDEOIN_F_DEV_EXT_NAME)
    {
        pFieldHdr = (VRDEVIDEOINDEVICEFIELDHDR *)pu8Ext;
        if (   cbExtLeft < sizeof(VRDEVIDEOINDEVICEFIELDHDR)
            || cbExtLeft < pFieldHdr->cbField)
        {
            return;
        }

        RTStrCat(pDC->achProduct, sizeof(pDC->achProduct), " - ");
        RTStrCat(pDC->achProduct, sizeof(pDC->achProduct), (char *)&pu8Ext[sizeof(VRDEVIDEOINDEVICEFIELDHDR)]);
        pu8Ext += pFieldHdr->cbField;
    }
    if (pExt->fu32Fields & VRDE_VIDEOIN_F_DEV_EXT_SERIAL)
    {
        pFieldHdr = (VRDEVIDEOINDEVICEFIELDHDR *)pu8Ext;
        if (   cbExtLeft < sizeof(VRDEVIDEOINDEVICEFIELDHDR)
            || cbExtLeft < pFieldHdr->cbField)
        {
            return;
        }

        RTStrCopy(pDC->achSerial, sizeof(pDC->achSerial), (char *)&pu8Ext[sizeof(VRDEVIDEOINDEVICEFIELDHDR)]);
        pu8Ext += pFieldHdr->cbField;
    }
}


/*
 * Public functions.
 */
void usbWebcamDescriptorsFree(PPDMUSBDESCCACHE pDesc)
{
    RTMemFree(pDesc);
}

int usbWebcamDescriptorsBuild(PPDMUSBDESCCACHE *ppDescCache,
                              int iInstance,
                              const VRDEVIDEOINDEVICEDESC *pDeviceDesc,
                              uint32_t cbDeviceDesc,
                              uint32_t u32MinFrameInterval)
{
    RT_NOREF1(iInstance);
    if (pDeviceDesc == NULL)
    {
        pDeviceDesc = &g_defaultDeviceDesc.desc;
        cbDeviceDesc = sizeof(g_defaultDeviceDesc);
    }

    const VRDEVIDEOINFORMATDESC *pFormatMJPEG = NULL;
    uint32_t cbFormatMJPEG = 0;
    int rc = uwcDeviceDescFindFormat(&pFormatMJPEG, &cbFormatMJPEG, pDeviceDesc, cbDeviceDesc, VRDE_VIDEOIN_FORMAT_MJPEG);

    if (RT_SUCCESS(rc))
    {
        /* Build class specific descriptors. Then allocate and fill the descriptor cache. */
        void *pvClassVC = NULL;
        uint32_t cbClassVC = 0;
        void *pvClassVS = NULL;
        uint32_t cbClassVS = 0;

        rc = uwcBuildClassDescVC(&pvClassVC, &cbClassVC);
        if (RT_SUCCESS(rc))
        {
            rc = uwcBuildClassDescVS(u32MinFrameInterval, pFormatMJPEG, cbFormatMJPEG, &pvClassVS, &cbClassVS);
        }

        if (RT_SUCCESS(rc))
        {
            /* Buffer for USB descriptor cache. */
            uint32_t cbDC = sizeof(USBWEBCAMDESCRIPTORCACHE) + cbClassVC + cbClassVS;
            uint8_t *pu8DC = (uint8_t *)RTMemAllocZ(cbDC);

            if (pu8DC)
            {
                USBWEBCAMDESCRIPTORCACHE *pDC = (USBWEBCAMDESCRIPTORCACHE *)pu8DC;
                uwcFillDescriptorCache(pDC, pvClassVC, cbClassVC, pvClassVS, cbClassVS);

                /* Set default values. */
                RTStrCopy(pDC->achManufacturer, sizeof(pDC->achManufacturer), "VirtualBox");
                RTStrCopy(pDC->achProduct,      sizeof(pDC->achProduct),      "VirtualBox Webcam");
                RTStrCopy(pDC->achSerial,       sizeof(pDC->achSerial),       "00000011");

                /* Update descriptors using optional values. */
                uwcParseExt(pDC, pDeviceDesc);

                UWLOG(("\nProduct: %s\nSerial: %s\n", pDC->achProduct, pDC->achSerial));

                *ppDescCache = &pDC->DescCache;
            }
            else
            {
                rc = VERR_NO_MEMORY;
            }
        }

        RTMemFree(pvClassVS);
        RTMemFree(pvClassVC);
    }

    return rc;
}

const VUSBVDESCVSMJPEGFRAME *usbWebcamDescriptorsFindFrame(const PPDMUSBDESCCACHE pDescCache,
                                                           uint8_t bFrameIndex)
{
    USBWEBCAMDESCRIPTORCACHE *pDC = RT_FROM_MEMBER(pDescCache, USBWEBCAMDESCRIPTORCACHE, DescCache);
    WEBCAMDESCVSCLASS *pClass = (WEBCAMDESCVSCLASS *)pDC->aSettingsVS[0].pvClass;

    const VUSBVDESCVSMJPEGFRAME *pDescFrame = NULL;
    const VUSBVDESCVSMJPEGFRAME *pIter;
    uint8_t *pu8 = (uint8_t *)&pClass->aJpegFrames[0];
    int i;
    for (i = 0; i < pClass->JpegFormat.bNumFrameDescriptors; i++)
    {
        pIter = (VUSBVDESCVSMJPEGFRAME *)pu8;

        if (pIter->bFrameIndex == bFrameIndex)
        {
            pDescFrame = pIter;
            UWLOG(("Found %d frame at %d\n", bFrameIndex, i));
            break;
        }

        pu8 += pIter->bLength;
    }

    return pDescFrame;
}

const VUSBVDESCVSMJPEGFRAME *usbWebcamDescriptorsSuggestFrame(const PPDMUSBDESCCACHE pDescCache,
                                                              uint16_t u16Width, uint16_t u16Height)
{
    USBWEBCAMDESCRIPTORCACHE *pDC = RT_FROM_MEMBER(pDescCache, USBWEBCAMDESCRIPTORCACHE, DescCache);
    WEBCAMDESCVSCLASS *pClass = (WEBCAMDESCVSCLASS *)pDC->aSettingsVS[0].pvClass;

    const VUSBVDESCVSMJPEGFRAME *pSuggestedFrame = NULL;
    const VUSBVDESCVSMJPEGFRAME *pIter;
    uint8_t *pu8 = (uint8_t *)&pClass->aJpegFrames[0];
    int i;
    for (i = 0; i < pClass->JpegFormat.bNumFrameDescriptors; i++)
    {
        pIter = (VUSBVDESCVSMJPEGFRAME *)pu8;

        if (   pIter->wWidth >= u16Width
            && pIter->wHeight >= u16Height)
        {
            pSuggestedFrame = pIter;
            break;
        }

        pu8 += pIter->bLength;
    }

    return pSuggestedFrame;
}

uint8_t usbWebcamDescriptorsFrameCount(const PPDMUSBDESCCACHE pDescCache)
{
    USBWEBCAMDESCRIPTORCACHE *pDC = RT_FROM_MEMBER(pDescCache, USBWEBCAMDESCRIPTORCACHE, DescCache);
    WEBCAMDESCVSCLASS *pClass = (WEBCAMDESCVSCLASS *)pDC->aSettingsVS[0].pvClass;
    return pClass->JpegFormat.bNumFrameDescriptors;
}

