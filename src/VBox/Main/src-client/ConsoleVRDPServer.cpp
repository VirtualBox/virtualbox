/* $Id: ConsoleVRDPServer.cpp 109303 2025-04-22 11:16:01Z vadim.galitsyn@oracle.com $ */
/** @file
 * VBox Console VRDP helper class.
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

#define LOG_GROUP LOG_GROUP_MAIN_CONSOLE
#include "LoggingNew.h"

#include "ConsoleVRDPServer.h"
#include "ConsoleImpl.h"
#include "DisplayImpl.h"
#ifdef VBOX_WITH_SHARED_CLIPBOARD
# include "GuestShClPrivate.h" /* For (un-)registering the service extension. */
#endif
#include "KeyboardImpl.h"
#include "MouseImpl.h"
#ifdef VBOX_WITH_AUDIO_VRDE
#include "DrvAudioVRDE.h"
#endif
#ifdef VBOX_WITH_EXTPACK
# include "ExtPackManagerImpl.h"
#endif
#include "VMMDev.h"
#ifdef VBOX_WITH_USB_CARDREADER
# include "UsbCardReader.h"
#endif
#include "UsbWebcamInterface.h"

#include "Global.h"
#include "AutoCaller.h"

#include <iprt/asm.h>
#include <iprt/alloca.h>
#include <iprt/ldr.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/cpp/utils.h>

#include <VBox/err.h>
#include <VBox/RemoteDesktop/VRDEOrders.h>
#include <VBox/com/listeners.h>


class VRDPConsoleListener
{
public:
    VRDPConsoleListener()
    {
    }

    virtual ~VRDPConsoleListener()
    {
    }

    HRESULT init(ConsoleVRDPServer *server)
    {
        m_server = server;
        return S_OK;
    }

    void uninit()
    {
    }

    STDMETHOD(HandleEvent)(VBoxEventType_T aType, IEvent * aEvent)
    {
        switch (aType)
        {
            case VBoxEventType_OnMousePointerShapeChanged:
            {
                ComPtr<IMousePointerShapeChangedEvent> mpscev = aEvent;
                Assert(mpscev);
                BOOL    visible,  alpha;
                ULONG   xHot, yHot, width, height;
                com::SafeArray <BYTE> shape;

                mpscev->COMGETTER(Visible)(&visible);
                mpscev->COMGETTER(Alpha)(&alpha);
                mpscev->COMGETTER(Xhot)(&xHot);
                mpscev->COMGETTER(Yhot)(&yHot);
                mpscev->COMGETTER(Width)(&width);
                mpscev->COMGETTER(Height)(&height);
                mpscev->COMGETTER(Shape)(ComSafeArrayAsOutParam(shape));

                m_server->onMousePointerShapeChange(visible, alpha, xHot, yHot, width, height, ComSafeArrayAsInParam(shape));
                break;
            }
            case VBoxEventType_OnMouseCapabilityChanged:
            {
                ComPtr<IMouseCapabilityChangedEvent> mccev = aEvent;
                Assert(mccev);
                if (m_server)
                {
                    BOOL fAbsoluteMouse;
                    mccev->COMGETTER(SupportsAbsolute)(&fAbsoluteMouse);
                    m_server->NotifyAbsoluteMouse(!!fAbsoluteMouse);
                }
                break;
            }
            case VBoxEventType_OnKeyboardLedsChanged:
            {
                ComPtr<IKeyboardLedsChangedEvent> klcev = aEvent;
                Assert(klcev);

                if (m_server)
                {
                    BOOL fNumLock, fCapsLock, fScrollLock;
                    klcev->COMGETTER(NumLock)(&fNumLock);
                    klcev->COMGETTER(CapsLock)(&fCapsLock);
                    klcev->COMGETTER(ScrollLock)(&fScrollLock);
                    m_server->NotifyKeyboardLedsChange(fNumLock, fCapsLock, fScrollLock);
                }
                 break;
            }

            default:
                AssertFailed();
        }

        return S_OK;
    }

private:
    ConsoleVRDPServer *m_server;
};

typedef ListenerImpl<VRDPConsoleListener, ConsoleVRDPServer*> VRDPConsoleListenerImpl;

VBOX_LISTENER_DECLARE(VRDPConsoleListenerImpl)

#ifdef DEBUG_sunlover
#define LOGDUMPPTR Log
void dumpPointer(const uint8_t *pu8Shape, uint32_t width, uint32_t height, bool fXorMaskRGB32)
{
    unsigned i;

    const uint8_t *pu8And = pu8Shape;

    for (i = 0; i < height; i++)
    {
        unsigned j;
        LOGDUMPPTR(("%p: ", pu8And));
        for (j = 0; j < (width + 7) / 8; j++)
        {
            unsigned k;
            for (k = 0; k < 8; k++)
            {
                LOGDUMPPTR(("%d", ((*pu8And) & (1 << (7 - k)))? 1: 0));
            }

            pu8And++;
        }
        LOGDUMPPTR(("\n"));
    }

    if (fXorMaskRGB32)
    {
        uint32_t *pu32Xor = (uint32_t*)(pu8Shape + ((((width + 7) / 8) * height + 3) & ~3));

        for (i = 0; i < height; i++)
        {
            unsigned j;
            LOGDUMPPTR(("%p: ", pu32Xor));
            for (j = 0; j < width; j++)
            {
                LOGDUMPPTR(("%08X", *pu32Xor++));
            }
            LOGDUMPPTR(("\n"));
        }
    }
    else
    {
        /* RDP 24 bit RGB mask. */
        uint8_t *pu8Xor = (uint8_t*)(pu8Shape + ((((width + 7) / 8) * height + 3) & ~3));
        for (i = 0; i < height; i++)
        {
            unsigned j;
            LOGDUMPPTR(("%p: ", pu8Xor));
            for (j = 0; j < width; j++)
            {
                LOGDUMPPTR(("%02X%02X%02X", pu8Xor[2], pu8Xor[1], pu8Xor[0]));
                pu8Xor += 3;
            }
            LOGDUMPPTR(("\n"));
        }
    }
}
#else
#define dumpPointer(a, b, c, d) do {} while (0)
#endif /* DEBUG_sunlover */

static void findTopLeftBorder(const uint8_t *pu8AndMask, const uint8_t *pu8XorMask, uint32_t width,
                              uint32_t height, uint32_t *pxSkip, uint32_t *pySkip)
{
    /*
     * Find the top border of the AND mask. First assign to special value.
     */
    uint32_t ySkipAnd = UINT32_MAX;

    const uint8_t *pu8And = pu8AndMask;
    const uint32_t cbAndRow = (width + 7) / 8;
    const uint8_t maskLastByte = (uint8_t)( 0xFF << (cbAndRow * 8 - width) );

    Assert(cbAndRow > 0);

    unsigned y;
    unsigned x;

    for (y = 0; y < height && ySkipAnd == ~(uint32_t)0; y++, pu8And += cbAndRow)
    {
        /* For each complete byte in the row. */
        for (x = 0; x < cbAndRow - 1; x++)
        {
            if (pu8And[x] != 0xFF)
            {
                ySkipAnd = y;
                break;
            }
        }

        if (ySkipAnd == ~(uint32_t)0)
        {
            /* Last byte. */
            if ((pu8And[cbAndRow - 1] & maskLastByte) != maskLastByte)
            {
                ySkipAnd = y;
            }
        }
    }

    if (ySkipAnd == ~(uint32_t)0)
    {
        ySkipAnd = 0;
    }

    /*
     * Find the left border of the AND mask.
     */
    uint32_t xSkipAnd = UINT32_MAX;

    /* For all bit columns. */
    for (x = 0; x < width && xSkipAnd == ~(uint32_t)0; x++)
    {
        pu8And = pu8AndMask + x/8;       /* Currently checking byte. */
        uint8_t mask = 1 << (7 - x%8); /* Currently checking bit in the byte. */

        for (y = ySkipAnd; y < height; y++, pu8And += cbAndRow)
        {
            if ((*pu8And & mask) == 0)
            {
                xSkipAnd = x;
                break;
            }
        }
    }

    if (xSkipAnd == ~(uint32_t)0)
    {
        xSkipAnd = 0;
    }

    /*
     * Find the XOR mask top border.
     */
    uint32_t ySkipXor = UINT32_MAX;

    uint32_t *pu32XorStart = (uint32_t *)pu8XorMask;

    uint32_t *pu32Xor = pu32XorStart;

    for (y = 0; y < height && ySkipXor == ~(uint32_t)0; y++, pu32Xor += width)
    {
        for (x = 0; x < width; x++)
        {
            if (pu32Xor[x] != 0)
            {
                ySkipXor = y;
                break;
            }
        }
    }

    if (ySkipXor == ~(uint32_t)0)
    {
        ySkipXor = 0;
    }

    /*
     * Find the left border of the XOR mask.
     */
    uint32_t xSkipXor = ~(uint32_t)0;

    /* For all columns. */
    for (x = 0; x < width && xSkipXor == ~(uint32_t)0; x++)
    {
        pu32Xor = pu32XorStart + x;    /* Currently checking dword. */

        for (y = ySkipXor; y < height; y++, pu32Xor += width)
        {
            if (*pu32Xor != 0)
            {
                xSkipXor = x;
                break;
            }
        }
    }

    if (xSkipXor == ~(uint32_t)0)
    {
        xSkipXor = 0;
    }

    *pxSkip = RT_MIN(xSkipAnd, xSkipXor);
    *pySkip = RT_MIN(ySkipAnd, ySkipXor);
}

/* Generate an AND mask for alpha pointers here, because
 * guest driver does not do that correctly for Vista pointers.
 * Similar fix, changing the alpha threshold, could be applied
 * for the guest driver, but then additions reinstall would be
 * necessary, which we try to avoid.
 */
static void mousePointerGenerateANDMask(uint8_t *pu8DstAndMask, int cbDstAndMask, const uint8_t *pu8SrcAlpha, int w, int h)
{
    memset(pu8DstAndMask, 0xFF, cbDstAndMask);

    int y;
    for (y = 0; y < h; y++)
    {
        uint8_t bitmask = 0x80;

        int x;
        for (x = 0; x < w; x++, bitmask >>= 1)
        {
            if (bitmask == 0)
            {
                bitmask = 0x80;
            }

            /* Whether alpha channel value is not transparent enough for the pixel to be seen. */
            if (pu8SrcAlpha[x * 4 + 3] > 0x7f)
            {
                pu8DstAndMask[x / 8] &= ~bitmask;
            }
        }

        /* Point to next source and dest scans. */
        pu8SrcAlpha += w * 4;
        pu8DstAndMask += (w + 7) / 8;
    }
}

void ConsoleVRDPServer::onMousePointerShapeChange(BOOL visible,
                                                  BOOL alpha,
                                                  ULONG xHot,
                                                  ULONG yHot,
                                                  ULONG width,
                                                  ULONG height,
                                                  ComSafeArrayIn(BYTE,inShape))
{
    Log9(("VRDPConsoleListener::OnMousePointerShapeChange: %d, %d, %lux%lu, @%lu,%lu\n",
          visible, alpha, width, height, xHot, yHot));

    com::SafeArray <BYTE> aShape(ComSafeArrayInArg(inShape));
    if (aShape.size() == 0)
    {
        if (!visible)
        {
            MousePointerHide();
        }
    }
    else if (width != 0 && height != 0)
    {
        uint8_t* shape = aShape.raw();

        dumpPointer(shape, width, height, true);

        /* Try the new interface. */
        if (MousePointer(alpha, xHot, yHot, width, height, shape) == VINF_SUCCESS)
        {
            return;
        }

        /* Continue with the old interface. */

        /* Pointer consists of 1 bpp AND and 24 BPP XOR masks.
         * 'shape' AND mask followed by XOR mask.
         * XOR mask contains 32 bit (lsb)BGR0(msb) values.
         *
         * We convert this to RDP color format which consist of
         * one bpp AND mask and 24 BPP (BGR) color XOR image.
         *
         * RDP clients expect 8 aligned width and height of
         * pointer (preferably 32x32).
         *
         * They even contain bugs which do not appear for
         * 32x32 pointers but would appear for a 41x32 one.
         *
         * So set pointer size to 32x32. This can be done safely
         * because most pointers are 32x32.
         */

        int cbDstAndMask = (((width + 7) / 8) * height + 3) & ~3;

        uint8_t *pu8AndMask = shape;
        uint8_t *pu8XorMask = shape + cbDstAndMask;

        if (alpha)
        {
            pu8AndMask = (uint8_t*)alloca(cbDstAndMask);

            mousePointerGenerateANDMask(pu8AndMask, cbDstAndMask, pu8XorMask, width, height);
        }

        /* Windows guest alpha pointers are wider than 32 pixels.
         * Try to find out the top-left border of the pointer and
         * then copy only meaningful bits. All complete top rows
         * and all complete left columns where (AND == 1 && XOR == 0)
         * are skipped. Hot spot is adjusted.
         */
        uint32_t ySkip = 0; /* How many rows to skip at the top. */
        uint32_t xSkip = 0; /* How many columns to skip at the left. */

        findTopLeftBorder(pu8AndMask, pu8XorMask, width, height, &xSkip, &ySkip);

        /* Must not skip the hot spot. */
        xSkip = RT_MIN(xSkip, xHot);
        ySkip = RT_MIN(ySkip, yHot);

        /*
         * Compute size and allocate memory for the pointer.
         */
        const uint32_t dstwidth = 32;
        const uint32_t dstheight = 32;

        VRDECOLORPOINTER *pointer = NULL;

        uint32_t dstmaskwidth = (dstwidth + 7) / 8;

        uint32_t rdpmaskwidth = dstmaskwidth;
        uint32_t rdpmasklen = dstheight * rdpmaskwidth;

        uint32_t rdpdatawidth = dstwidth * 3;
        uint32_t rdpdatalen = dstheight * rdpdatawidth;

        pointer = (VRDECOLORPOINTER *)RTMemTmpAlloc(sizeof(VRDECOLORPOINTER) + rdpmasklen + rdpdatalen);

        if (pointer)
        {
            uint8_t *maskarray = (uint8_t*)pointer + sizeof(VRDECOLORPOINTER);
            uint8_t *dataarray = maskarray + rdpmasklen;

            memset(maskarray, 0xFF, rdpmasklen);
            memset(dataarray, 0x00, rdpdatalen);

            uint32_t srcmaskwidth = (width + 7) / 8;
            uint32_t srcdatawidth = width * 4;

            /* Copy AND mask. */
            uint8_t *src = pu8AndMask + ySkip * srcmaskwidth;
            uint8_t *dst = maskarray + (dstheight - 1) * rdpmaskwidth;

            uint32_t minheight = RT_MIN(height - ySkip, dstheight);
            uint32_t minwidth = RT_MIN(width - xSkip, dstwidth);

            unsigned x, y;

            for (y = 0; y < minheight; y++)
            {
                for (x = 0; x < minwidth; x++)
                {
                    uint32_t byteIndex = (x + xSkip) / 8;
                    uint32_t bitIndex = (x + xSkip) % 8;

                    bool bit = (src[byteIndex] & (1 << (7 - bitIndex))) != 0;

                    if (!bit)
                    {
                        byteIndex = x / 8;
                        bitIndex = x % 8;

                        dst[byteIndex] &= ~(1 << (7 - bitIndex));
                    }
                }

                src += srcmaskwidth;
                dst -= rdpmaskwidth;
            }

            /* Point src to XOR mask */
            src = pu8XorMask + ySkip * srcdatawidth;
            dst = dataarray + (dstheight - 1) * rdpdatawidth;

            for (y = 0; y < minheight ; y++)
            {
                for (x = 0; x < minwidth; x++)
                {
                    memcpy(dst + x * 3, &src[4 * (x + xSkip)], 3);
                }

                src += srcdatawidth;
                dst -= rdpdatawidth;
            }

            pointer->u16HotX = (uint16_t)(xHot - xSkip);
            pointer->u16HotY = (uint16_t)(yHot - ySkip);

            pointer->u16Width = (uint16_t)dstwidth;
            pointer->u16Height = (uint16_t)dstheight;

            pointer->u16MaskLen = (uint16_t)rdpmasklen;
            pointer->u16DataLen = (uint16_t)rdpdatalen;

            dumpPointer((uint8_t*)pointer + sizeof(*pointer), dstwidth, dstheight, false);

            MousePointerUpdate(pointer);

            RTMemTmpFree(pointer);
        }
    }
}


// ConsoleVRDPServer
////////////////////////////////////////////////////////////////////////////////

RTLDRMOD ConsoleVRDPServer::mVRDPLibrary = NIL_RTLDRMOD;

PFNVRDECREATESERVER ConsoleVRDPServer::mpfnVRDECreateServer = NULL;

VRDEENTRYPOINTS_4 ConsoleVRDPServer::mEntryPoints; /* A copy of the server entry points. */
VRDEENTRYPOINTS_4 *ConsoleVRDPServer::mpEntryPoints = NULL;

VRDECALLBACKS_4 ConsoleVRDPServer::mCallbacks =
{
    { VRDE_INTERFACE_VERSION_4, sizeof(VRDECALLBACKS_4) },
    ConsoleVRDPServer::VRDPCallbackQueryProperty,
    ConsoleVRDPServer::VRDPCallbackClientLogon,
    ConsoleVRDPServer::VRDPCallbackClientConnect,
    ConsoleVRDPServer::VRDPCallbackClientDisconnect,
    ConsoleVRDPServer::VRDPCallbackIntercept,
    ConsoleVRDPServer::VRDPCallbackUSB,
    ConsoleVRDPServer::VRDPCallbackClipboard,
    ConsoleVRDPServer::VRDPCallbackFramebufferQuery,
    ConsoleVRDPServer::VRDPCallbackFramebufferLock,
    ConsoleVRDPServer::VRDPCallbackFramebufferUnlock,
    ConsoleVRDPServer::VRDPCallbackInput,
    ConsoleVRDPServer::VRDPCallbackVideoModeHint,
    ConsoleVRDPServer::VRDECallbackAudioIn
};

DECLCALLBACK(int) ConsoleVRDPServer::VRDPCallbackQueryProperty(void *pvCallback, uint32_t index, void *pvBuffer,
                                                               uint32_t cbBuffer, uint32_t *pcbOut)
{
    ConsoleVRDPServer *server = static_cast<ConsoleVRDPServer*>(pvCallback);

    int vrc = VERR_NOT_SUPPORTED;

    switch (index)
    {
        case VRDE_QP_NETWORK_PORT:
        {
            /* This is obsolete, the VRDE server uses VRDE_QP_NETWORK_PORT_RANGE instead. */
            ULONG port = 0;

            if (cbBuffer >= sizeof(uint32_t))
            {
                *(uint32_t *)pvBuffer = (uint32_t)port;
                vrc = VINF_SUCCESS;
            }
            else
            {
                vrc = VINF_BUFFER_OVERFLOW;
            }

            *pcbOut = sizeof(uint32_t);
        } break;

        case VRDE_QP_NETWORK_ADDRESS:
        {
            com::Bstr bstr;
            server->mConsole->i_getVRDEServer()->GetVRDEProperty(Bstr("TCP/Address").raw(), bstr.asOutParam());

            /* The server expects UTF8. */
            com::Utf8Str address = bstr;

            size_t cbAddress = address.length() + 1;

            if (cbAddress >= 0x10000)
            {
                /* More than 64K seems to be an  invalid address. */
                vrc = VERR_TOO_MUCH_DATA;
                break;
            }

            if ((size_t)cbBuffer >= cbAddress)
            {
                memcpy(pvBuffer, address.c_str(), cbAddress);
                vrc = VINF_SUCCESS;
            }
            else
            {
                vrc = VINF_BUFFER_OVERFLOW;
            }

            *pcbOut = (uint32_t)cbAddress;
        } break;

        case VRDE_QP_NUMBER_MONITORS:
        {
            uint32_t cMonitors = server->mConsole->i_getDisplay()->i_getMonitorCount();

            if (cbBuffer >= sizeof(uint32_t))
            {
                *(uint32_t *)pvBuffer = (uint32_t)cMonitors;
                vrc = VINF_SUCCESS;
            }
            else
            {
                vrc = VINF_BUFFER_OVERFLOW;
            }

            *pcbOut = sizeof(uint32_t);
        } break;

        case VRDE_QP_NETWORK_PORT_RANGE:
        {
            com::Bstr bstr;
            HRESULT hrc = server->mConsole->i_getVRDEServer()->GetVRDEProperty(Bstr("TCP/Ports").raw(), bstr.asOutParam());

            if (hrc != S_OK)
            {
                bstr = "";
            }

            if (bstr == "0")
            {
                bstr = "3389";
            }

            /* The server expects UTF8. */
            com::Utf8Str portRange = bstr;

            size_t cbPortRange = portRange.length() + 1;

            if (cbPortRange >= _64K)
            {
                /* More than 64K seems to be an invalid port range string. */
                vrc = VERR_TOO_MUCH_DATA;
                break;
            }

            if ((size_t)cbBuffer >= cbPortRange)
            {
                memcpy(pvBuffer, portRange.c_str(), cbPortRange);
                vrc = VINF_SUCCESS;
            }
            else
            {
                vrc = VINF_BUFFER_OVERFLOW;
            }

            *pcbOut = (uint32_t)cbPortRange;
        } break;

        case VRDE_QP_VIDEO_CHANNEL:
        {
            com::Bstr bstr;
            HRESULT hrc = server->mConsole->i_getVRDEServer()->GetVRDEProperty(Bstr("VideoChannel/Enabled").raw(),
                                                                             bstr.asOutParam());

            if (hrc != S_OK)
            {
                bstr = "";
            }

            com::Utf8Str value = bstr;

            BOOL fVideoEnabled =    RTStrICmp(value.c_str(), "true") == 0
                                 || RTStrICmp(value.c_str(), "1") == 0;

            if (cbBuffer >= sizeof(uint32_t))
            {
                *(uint32_t *)pvBuffer = (uint32_t)fVideoEnabled;
                vrc = VINF_SUCCESS;
            }
            else
            {
                vrc = VINF_BUFFER_OVERFLOW;
            }

            *pcbOut = sizeof(uint32_t);
        } break;

        case VRDE_QP_VIDEO_CHANNEL_QUALITY:
        {
            com::Bstr bstr;
            HRESULT hrc = server->mConsole->i_getVRDEServer()->GetVRDEProperty(Bstr("VideoChannel/Quality").raw(),
                                                                             bstr.asOutParam());

            if (hrc != S_OK)
            {
                bstr = "";
            }

            com::Utf8Str value = bstr;

            ULONG ulQuality = RTStrToUInt32(value.c_str()); /* This returns 0 on invalid string which is ok. */

            if (cbBuffer >= sizeof(uint32_t))
            {
                *(uint32_t *)pvBuffer = (uint32_t)ulQuality;
                vrc = VINF_SUCCESS;
            }
            else
            {
                vrc = VINF_BUFFER_OVERFLOW;
            }

            *pcbOut = sizeof(uint32_t);
        } break;

        case VRDE_QP_VIDEO_CHANNEL_SUNFLSH:
        {
            ULONG ulSunFlsh = 1;

            com::Bstr bstr;
            HRESULT hrc = server->mConsole->i_machine()->GetExtraData(Bstr("VRDP/SunFlsh").raw(),
                                                                    bstr.asOutParam());
            if (hrc == S_OK && !bstr.isEmpty())
            {
                com::Utf8Str sunFlsh = bstr;
                if (!sunFlsh.isEmpty())
                {
                    ulSunFlsh = sunFlsh.toUInt32();
                }
            }

            if (cbBuffer >= sizeof(uint32_t))
            {
                *(uint32_t *)pvBuffer = (uint32_t)ulSunFlsh;
                vrc = VINF_SUCCESS;
            }
            else
            {
                vrc = VINF_BUFFER_OVERFLOW;
            }

            *pcbOut = sizeof(uint32_t);
        } break;

        case VRDE_QP_FEATURE:
        {
            if (cbBuffer < sizeof(VRDEFEATURE))
            {
                vrc = VERR_INVALID_PARAMETER;
                break;
            }

            size_t cbInfo = cbBuffer - RT_UOFFSETOF(VRDEFEATURE, achInfo);

            VRDEFEATURE *pFeature = (VRDEFEATURE *)pvBuffer;

            size_t cchInfo = 0;
            vrc = RTStrNLenEx(pFeature->achInfo, cbInfo, &cchInfo);

            if (RT_FAILURE(vrc))
            {
                vrc = VERR_INVALID_PARAMETER;
                break;
            }

            Log(("VRDE_QP_FEATURE [%s]\n", pFeature->achInfo));

            com::Bstr bstrValue;

            if (   RTStrICmp(pFeature->achInfo, "Client/DisableDisplay") == 0
                || RTStrICmp(pFeature->achInfo, "Client/DisableInput") == 0
                || RTStrICmp(pFeature->achInfo, "Client/DisableAudio") == 0
                || RTStrICmp(pFeature->achInfo, "Client/DisableUSB") == 0
                || RTStrICmp(pFeature->achInfo, "Client/DisableClipboard") == 0
               )
            {
                /** @todo these features should be per client. */
                NOREF(pFeature->u32ClientId);

                /* These features are mapped to "VRDE/Feature/NAME" extra data. */
                com::Utf8Str extraData("VRDE/Feature/");
                extraData += pFeature->achInfo;

                HRESULT hrc = server->mConsole->i_machine()->GetExtraData(com::Bstr(extraData).raw(),
                                                                          bstrValue.asOutParam());
                if (FAILED(hrc) || bstrValue.isEmpty())
                {
                    /* Also try the old "VRDP/Feature/NAME" */
                    extraData = "VRDP/Feature/";
                    extraData += pFeature->achInfo;

                    hrc = server->mConsole->i_machine()->GetExtraData(com::Bstr(extraData).raw(),
                                                                      bstrValue.asOutParam());
                    if (FAILED(hrc))
                    {
                        vrc = VERR_NOT_SUPPORTED;
                    }
                }
            }
            else if (RTStrNCmp(pFeature->achInfo, "Property/", 9) == 0)
            {
                /* Generic properties. */
                const char *pszPropertyName = &pFeature->achInfo[9];
                HRESULT hrc = server->mConsole->i_getVRDEServer()->GetVRDEProperty(Bstr(pszPropertyName).raw(),
                                                                                   bstrValue.asOutParam());
                if (FAILED(hrc))
                {
                    vrc = VERR_NOT_SUPPORTED;
                }
            }
            else
            {
                vrc = VERR_NOT_SUPPORTED;
            }

            /* Copy the value string to the callers buffer. */
            if (vrc == VINF_SUCCESS)
            {
                com::Utf8Str value = bstrValue;

                size_t cb = value.length() + 1;

                if ((size_t)cbInfo >= cb)
                {
                    memcpy(pFeature->achInfo, value.c_str(), cb);
                }
                else
                {
                    vrc = VINF_BUFFER_OVERFLOW;
                }

                *pcbOut = (uint32_t)cb;
            }
        } break;

        case VRDE_SP_NETWORK_BIND_PORT:
        {
            if (cbBuffer != sizeof(uint32_t))
            {
                vrc = VERR_INVALID_PARAMETER;
                break;
            }

            ULONG port = *(uint32_t *)pvBuffer;

            server->mVRDPBindPort = port;

            vrc = VINF_SUCCESS;

            if (pcbOut)
            {
                *pcbOut = sizeof(uint32_t);
            }

            server->mConsole->i_onVRDEServerInfoChange();
        } break;

        case VRDE_SP_CLIENT_STATUS:
        {
            if (cbBuffer < sizeof(VRDECLIENTSTATUS))
            {
                vrc = VERR_INVALID_PARAMETER;
                break;
            }

            size_t cbStatus = cbBuffer - RT_UOFFSETOF(VRDECLIENTSTATUS, achStatus);

            VRDECLIENTSTATUS *pStatus = (VRDECLIENTSTATUS *)pvBuffer;

            if (cbBuffer < RT_UOFFSETOF(VRDECLIENTSTATUS, achStatus) + pStatus->cbStatus)
            {
                vrc = VERR_INVALID_PARAMETER;
                break;
            }

            size_t cchStatus = 0;
            vrc = RTStrNLenEx(pStatus->achStatus, cbStatus, &cchStatus);

            if (RT_FAILURE(vrc))
            {
                vrc = VERR_INVALID_PARAMETER;
                break;
            }

            Log(("VRDE_SP_CLIENT_STATUS [%s]\n", pStatus->achStatus));

            server->mConsole->i_VRDPClientStatusChange(pStatus->u32ClientId, pStatus->achStatus);

            vrc = VINF_SUCCESS;

            if (pcbOut)
            {
                *pcbOut = cbBuffer;
            }

            server->mConsole->i_onVRDEServerInfoChange();
        } break;

        default:
            break;
    }

    return vrc;
}

DECLCALLBACK(int) ConsoleVRDPServer::VRDPCallbackClientLogon(void *pvCallback, uint32_t u32ClientId, const char *pszUser,
                                                             const char *pszPassword, const char *pszDomain)
{
    ConsoleVRDPServer *server = static_cast<ConsoleVRDPServer*>(pvCallback);

    return server->mConsole->i_VRDPClientLogon(u32ClientId, pszUser, pszPassword, pszDomain);
}

DECLCALLBACK(void) ConsoleVRDPServer::VRDPCallbackClientConnect(void *pvCallback, uint32_t u32ClientId)
{
    ConsoleVRDPServer *pServer = static_cast<ConsoleVRDPServer*>(pvCallback);

    pServer->mConsole->i_VRDPClientConnect(u32ClientId);

    /* Should the server report usage of an interface for each client?
     * Similar to Intercept.
     */
    int c = ASMAtomicIncS32(&pServer->mcClients);
    if (c == 1)
    {
        /* Features which should be enabled only if there is a client. */
        pServer->remote3DRedirect(true);
    }

#ifdef VBOX_WITH_AUDIO_VRDE
    AudioVRDE *pVRDE = pServer->mConsole->i_getAudioVRDE();
    if (pVRDE)
        pVRDE->onVRDEClientConnect(u32ClientId);
#endif
}

DECLCALLBACK(void) ConsoleVRDPServer::VRDPCallbackClientDisconnect(void *pvCallback, uint32_t u32ClientId,
                                                                   uint32_t fu32Intercepted)
{
    ConsoleVRDPServer *pServer = static_cast<ConsoleVRDPServer*>(pvCallback);
    AssertPtrReturnVoid(pServer);

    pServer->mConsole->i_VRDPClientDisconnect(u32ClientId, fu32Intercepted);

    if (ASMAtomicReadU32(&pServer->mu32AudioInputClientId) == u32ClientId)
    {
        LogFunc(("Disconnected client %u\n", u32ClientId));
        ASMAtomicWriteU32(&pServer->mu32AudioInputClientId, 0);

#ifdef VBOX_WITH_AUDIO_VRDE
        AudioVRDE *pVRDE = pServer->mConsole->i_getAudioVRDE();
        if (pVRDE)
        {
            pVRDE->onVRDEInputIntercept(false /* fIntercept */);
            pVRDE->onVRDEClientDisconnect(u32ClientId);
        }
#endif
    }

    int32_t cClients = ASMAtomicDecS32(&pServer->mcClients);
    if (cClients == 0)
    {
        /* Features which should be enabled only if there is a client. */
        pServer->remote3DRedirect(false);
    }
}

DECLCALLBACK(int) ConsoleVRDPServer::VRDPCallbackIntercept(void *pvCallback, uint32_t u32ClientId, uint32_t fu32Intercept,
                                                           void **ppvIntercept)
{
    ConsoleVRDPServer *pServer = static_cast<ConsoleVRDPServer*>(pvCallback);
    AssertPtrReturn(pServer, VERR_INVALID_POINTER);

    LogFlowFunc(("%x\n", fu32Intercept));

    int vrc = VERR_NOT_SUPPORTED;

    switch (fu32Intercept)
    {
        case VRDE_CLIENT_INTERCEPT_AUDIO:
        {
            pServer->mConsole->i_VRDPInterceptAudio(u32ClientId);
            if (ppvIntercept)
            {
                *ppvIntercept = pServer;
            }
            vrc = VINF_SUCCESS;
        } break;

        case VRDE_CLIENT_INTERCEPT_USB:
        {
            pServer->mConsole->i_VRDPInterceptUSB(u32ClientId, ppvIntercept);
            vrc = VINF_SUCCESS;
        } break;

        case VRDE_CLIENT_INTERCEPT_CLIPBOARD:
        {
            pServer->mConsole->i_VRDPInterceptClipboard(u32ClientId);
            if (ppvIntercept)
            {
                *ppvIntercept = pServer;
            }
            vrc = VINF_SUCCESS;
        } break;

        case VRDE_CLIENT_INTERCEPT_AUDIO_INPUT:
        {
            /*
             * This request is processed internally by the ConsoleVRDPServer.
             * Only one client is allowed to intercept audio input.
             */
            if (ASMAtomicCmpXchgU32(&pServer->mu32AudioInputClientId, u32ClientId, 0) == true)
            {
                LogFunc(("Intercepting audio input by client %RU32\n", u32ClientId));

#ifdef VBOX_WITH_AUDIO_VRDE
                AudioVRDE *pVRDE = pServer->mConsole->i_getAudioVRDE();
                if (pVRDE)
                    pVRDE->onVRDEInputIntercept(true /* fIntercept */);
#endif
            }
            else
            {
                Log(("AUDIOIN: ignored client %RU32, active client %RU32\n", u32ClientId, pServer->mu32AudioInputClientId));
                vrc = VERR_NOT_SUPPORTED;
            }
        } break;

        default:
            break;
    }

    return vrc;
}

DECLCALLBACK(int) ConsoleVRDPServer::VRDPCallbackUSB(void *pvCallback, void *pvIntercept, uint32_t u32ClientId,
                                                     uint8_t u8Code, const void *pvRet, uint32_t cbRet)
{
    RT_NOREF(pvCallback);
#ifdef VBOX_WITH_USB
    return USBClientResponseCallback(pvIntercept, u32ClientId, u8Code, pvRet, cbRet);
#else
    RT_NOREF(pvCallback, pvIntercept, u32ClientId, u8Code, pvRet, cbRet);
    return VERR_NOT_SUPPORTED;
#endif
}

DECLCALLBACK(int) ConsoleVRDPServer::VRDPCallbackClipboard(void *pvCallback, void *pvIntercept, uint32_t u32ClientId,
                                                           uint32_t u32Function, uint32_t u32Format,
                                                           const void *pvData, uint32_t cbData)
{
    RT_NOREF(pvCallback);
    return ClipboardCallback(pvIntercept, u32ClientId, u32Function, u32Format, pvData, cbData);
}

DECLCALLBACK(bool) ConsoleVRDPServer::VRDPCallbackFramebufferQuery(void *pvCallback, unsigned uScreenId,
                                                                   VRDEFRAMEBUFFERINFO *pInfo)
{
    ConsoleVRDPServer *server = static_cast<ConsoleVRDPServer*>(pvCallback);

    bool fAvailable = false;

    /* Obtain the new screen bitmap. */
    HRESULT hr = server->mConsole->i_getDisplay()->QuerySourceBitmap(uScreenId, server->maSourceBitmaps[uScreenId].asOutParam());
    if (SUCCEEDED(hr))
    {
        LONG xOrigin = 0;
        LONG yOrigin = 0;
        BYTE *pAddress = NULL;
        ULONG ulWidth = 0;
        ULONG ulHeight = 0;
        ULONG ulBitsPerPixel = 0;
        ULONG ulBytesPerLine = 0;
        BitmapFormat_T bitmapFormat = BitmapFormat_Opaque;

        hr = server->maSourceBitmaps[uScreenId]->QueryBitmapInfo(&pAddress,
                                                                 &ulWidth,
                                                                 &ulHeight,
                                                                 &ulBitsPerPixel,
                                                                 &ulBytesPerLine,
                                                                 &bitmapFormat);

        if (SUCCEEDED(hr))
        {
            ULONG dummy;
            GuestMonitorStatus_T monitorStatus;
            hr = server->mConsole->i_getDisplay()->GetScreenResolution(uScreenId, &dummy, &dummy, &dummy,
                                                                       &xOrigin, &yOrigin, &monitorStatus);

            if (SUCCEEDED(hr))
            {
                /* Now fill the information as requested by the caller. */
                pInfo->pu8Bits = pAddress;
                pInfo->xOrigin = xOrigin;
                pInfo->yOrigin = yOrigin;
                pInfo->cWidth = ulWidth;
                pInfo->cHeight = ulHeight;
                pInfo->cBitsPerPixel = ulBitsPerPixel;
                pInfo->cbLine = ulBytesPerLine;

                fAvailable = true;
            }
        }
    }

    return fAvailable;
}

DECLCALLBACK(void) ConsoleVRDPServer::VRDPCallbackFramebufferLock(void *pvCallback, unsigned uScreenId)
{
    NOREF(pvCallback);
    NOREF(uScreenId);
    /* Do nothing */
}

DECLCALLBACK(void) ConsoleVRDPServer::VRDPCallbackFramebufferUnlock(void *pvCallback, unsigned uScreenId)
{
    NOREF(pvCallback);
    NOREF(uScreenId);
    /* Do nothing */
}

static void fixKbdLockStatus(VRDPInputSynch *pInputSynch, IKeyboard *pKeyboard)
{
    if (   pInputSynch->cGuestNumLockAdaptions
        && (pInputSynch->fGuestNumLock != pInputSynch->fClientNumLock))
    {
        pInputSynch->cGuestNumLockAdaptions--;
        pKeyboard->PutScancode(0x45);
        pKeyboard->PutScancode(0x45 | 0x80);
    }
    if (   pInputSynch->cGuestCapsLockAdaptions
        && (pInputSynch->fGuestCapsLock != pInputSynch->fClientCapsLock))
    {
        pInputSynch->cGuestCapsLockAdaptions--;
        pKeyboard->PutScancode(0x3a);
        pKeyboard->PutScancode(0x3a | 0x80);
    }
}

DECLCALLBACK(void) ConsoleVRDPServer::VRDPCallbackInput(void *pvCallback, int type, const void *pvInput, unsigned cbInput)
{
    ConsoleVRDPServer *server = static_cast<ConsoleVRDPServer*>(pvCallback);
    Console *pConsole = server->mConsole;

    switch (type)
    {
        case VRDE_INPUT_SCANCODE:
        {
            if (cbInput == sizeof(VRDEINPUTSCANCODE))
            {
                IKeyboard *pKeyboard = pConsole->i_getKeyboard();

                const VRDEINPUTSCANCODE *pInputScancode = (VRDEINPUTSCANCODE *)pvInput;

                /* Track lock keys. */
                if (pInputScancode->uScancode == 0x45)
                {
                    server->m_InputSynch.fClientNumLock = !server->m_InputSynch.fClientNumLock;
                }
                else if (pInputScancode->uScancode == 0x3a)
                {
                    server->m_InputSynch.fClientCapsLock = !server->m_InputSynch.fClientCapsLock;
                }
                else if (pInputScancode->uScancode == 0x46)
                {
                    server->m_InputSynch.fClientScrollLock = !server->m_InputSynch.fClientScrollLock;
                }
                else if ((pInputScancode->uScancode & 0x80) == 0)
                {
                    /* Key pressed. */
                    fixKbdLockStatus(&server->m_InputSynch, pKeyboard);
                }

                pKeyboard->PutScancode((LONG)pInputScancode->uScancode);
            }
        } break;

        case VRDE_INPUT_POINT:
        {
            if (cbInput == sizeof(VRDEINPUTPOINT))
            {
                const VRDEINPUTPOINT *pInputPoint = (VRDEINPUTPOINT *)pvInput;

                int mouseButtons = 0;
                int iWheel = 0;

                if (pInputPoint->uButtons & VRDE_INPUT_POINT_BUTTON1)
                {
                    mouseButtons |= MouseButtonState_LeftButton;
                }
                if (pInputPoint->uButtons & VRDE_INPUT_POINT_BUTTON2)
                {
                    mouseButtons |= MouseButtonState_RightButton;
                }
                if (pInputPoint->uButtons & VRDE_INPUT_POINT_BUTTON3)
                {
                    mouseButtons |= MouseButtonState_MiddleButton;
                }
                if (pInputPoint->uButtons & VRDE_INPUT_POINT_WHEEL_UP)
                {
                    mouseButtons |= MouseButtonState_WheelUp;
                    iWheel = -1;
                }
                if (pInputPoint->uButtons & VRDE_INPUT_POINT_WHEEL_DOWN)
                {
                    mouseButtons |= MouseButtonState_WheelDown;
                    iWheel = 1;
                }

                if (server->m_fGuestWantsAbsolute)
                {
                    pConsole->i_getMouse()->PutMouseEventAbsolute(pInputPoint->x + 1, pInputPoint->y + 1, iWheel,
                                                                  0 /* Horizontal wheel */, mouseButtons);
                } else
                {
                    pConsole->i_getMouse()->PutMouseEvent(pInputPoint->x - server->m_mousex,
                                                          pInputPoint->y - server->m_mousey,
                                                          iWheel, 0 /* Horizontal wheel */, mouseButtons);
                    server->m_mousex = pInputPoint->x;
                    server->m_mousey = pInputPoint->y;
                }
            }
        } break;

        case VRDE_INPUT_CAD:
        {
            pConsole->i_getKeyboard()->PutCAD();
        } break;

        case VRDE_INPUT_RESET:
        {
            pConsole->Reset();
        } break;

        case VRDE_INPUT_SYNCH:
        {
            if (cbInput == sizeof(VRDEINPUTSYNCH))
            {
                IKeyboard *pKeyboard = pConsole->i_getKeyboard();

                const VRDEINPUTSYNCH *pInputSynch = (VRDEINPUTSYNCH *)pvInput;

                server->m_InputSynch.fClientNumLock    = (pInputSynch->uLockStatus & VRDE_INPUT_SYNCH_NUMLOCK) != 0;
                server->m_InputSynch.fClientCapsLock   = (pInputSynch->uLockStatus & VRDE_INPUT_SYNCH_CAPITAL) != 0;
                server->m_InputSynch.fClientScrollLock = (pInputSynch->uLockStatus & VRDE_INPUT_SYNCH_SCROLL) != 0;

                /* The client initiated synchronization. Always make the guest to reflect the client state.
                 * Than means, when the guest changes the state itself, it is forced to return to the client
                 * state.
                 */
                if (server->m_InputSynch.fClientNumLock != server->m_InputSynch.fGuestNumLock)
                {
                    server->m_InputSynch.cGuestNumLockAdaptions = 2;
                }

                if (server->m_InputSynch.fClientCapsLock != server->m_InputSynch.fGuestCapsLock)
                {
                    server->m_InputSynch.cGuestCapsLockAdaptions = 2;
                }

                fixKbdLockStatus(&server->m_InputSynch, pKeyboard);
            }
        } break;

        default:
            break;
    }
}

DECLCALLBACK(void) ConsoleVRDPServer::VRDPCallbackVideoModeHint(void *pvCallback, unsigned cWidth, unsigned cHeight,
                                                                unsigned cBitsPerPixel, unsigned uScreenId)
{
    ConsoleVRDPServer *server = static_cast<ConsoleVRDPServer*>(pvCallback);

    server->mConsole->i_getDisplay()->SetVideoModeHint(uScreenId, TRUE /*=enabled*/,
                                                       FALSE /*=changeOrigin*/, 0/*=OriginX*/, 0/*=OriginY*/,
                                                       cWidth, cHeight, cBitsPerPixel, TRUE /*=notify*/);
}

DECLCALLBACK(void) ConsoleVRDPServer::VRDECallbackAudioIn(void *pvCallback,
                                                          void *pvCtx,
                                                          uint32_t u32ClientId,
                                                          uint32_t u32Event,
                                                          const void *pvData,
                                                          uint32_t cbData)
{
    RT_NOREF(u32ClientId);
    ConsoleVRDPServer *pServer = static_cast<ConsoleVRDPServer*>(pvCallback);
    AssertPtrReturnVoid(pServer);

#ifdef VBOX_WITH_AUDIO_VRDE
    AudioVRDE *pVRDE = pServer->mConsole->i_getAudioVRDE();
    if (!pVRDE) /* Nothing to do, bail out early. */
        return;

    switch (u32Event)
    {
        case VRDE_AUDIOIN_BEGIN:
        {
            pVRDE->onVRDEInputBegin(pvCtx, (PVRDEAUDIOINBEGIN)pvData);
            break;
        }

        case VRDE_AUDIOIN_DATA:
            pVRDE->onVRDEInputData(pvCtx, pvData, cbData);
            break;

        case VRDE_AUDIOIN_END:
            pVRDE->onVRDEInputEnd(pvCtx);
            break;

        default:
            break;
    }
#else
    RT_NOREF(pvCtx, u32Event, pvData, cbData);
#endif /* VBOX_WITH_AUDIO_VRDE */
}

ConsoleVRDPServer::ConsoleVRDPServer(Console *console)
{
    mConsole = console;

    int vrc = RTCritSectInit(&mCritSect);
    AssertRC(vrc);

    mcClipboardRefs = 0;
    mpfnClipboardCallback = NULL;
#ifdef VBOX_WITH_USB
    mUSBBackends.pHead = NULL;
    mUSBBackends.pTail = NULL;

    mUSBBackends.thread = NIL_RTTHREAD;
    mUSBBackends.fThreadRunning = false;
    mUSBBackends.event = 0;
#endif

    mhServer = 0;
    mServerInterfaceVersion = 0;

    mcInResize = 0;

    m_fGuestWantsAbsolute = false;
    m_mousex = 0;
    m_mousey = 0;

    m_InputSynch.cGuestNumLockAdaptions = 2;
    m_InputSynch.cGuestCapsLockAdaptions = 2;

    m_InputSynch.fGuestNumLock    = false;
    m_InputSynch.fGuestCapsLock   = false;
    m_InputSynch.fGuestScrollLock = false;

    m_InputSynch.fClientNumLock    = false;
    m_InputSynch.fClientCapsLock   = false;
    m_InputSynch.fClientScrollLock = false;

    {
        ComPtr<IEventSource> es;
        console->COMGETTER(EventSource)(es.asOutParam());
        ComObjPtr<VRDPConsoleListenerImpl> aConsoleListener;
        aConsoleListener.createObject();
        aConsoleListener->init(new VRDPConsoleListener(), this);
        mConsoleListener = aConsoleListener;
        com::SafeArray <VBoxEventType_T> eventTypes;
        eventTypes.push_back(VBoxEventType_OnMousePointerShapeChanged);
        eventTypes.push_back(VBoxEventType_OnMouseCapabilityChanged);
        eventTypes.push_back(VBoxEventType_OnKeyboardLedsChanged);
        es->RegisterListener(mConsoleListener, ComSafeArrayAsInParam(eventTypes), true);
    }

    mVRDPBindPort = -1;

#ifndef VBOX_WITH_VRDEAUTH_IN_VBOXSVC
    RT_ZERO(mAuthLibCtx);
#endif

    mu32AudioInputClientId = 0;
    mcClients = 0;

    /*
     * Optional interfaces.
     */
    m_fInterfaceImage = false;
    RT_ZERO(m_interfaceImage);
    RT_ZERO(m_interfaceCallbacksImage);
    RT_ZERO(m_interfaceMousePtr);
    RT_ZERO(m_interfaceSCard);
    RT_ZERO(m_interfaceCallbacksSCard);
    RT_ZERO(m_interfaceTSMF);
    RT_ZERO(m_interfaceCallbacksTSMF);
    RT_ZERO(m_interfaceVideoIn);
    RT_ZERO(m_interfaceCallbacksVideoIn);
    RT_ZERO(m_interfaceInput);
    RT_ZERO(m_interfaceCallbacksInput);

    vrc = RTCritSectInit(&mTSMFLock);
    AssertRC(vrc);

    mEmWebcam = new EmWebcam(this);
    AssertPtr(mEmWebcam);
}

ConsoleVRDPServer::~ConsoleVRDPServer()
{
    Stop();

    if (mConsoleListener)
    {
        ComPtr<IEventSource> es;
        mConsole->COMGETTER(EventSource)(es.asOutParam());
        es->UnregisterListener(mConsoleListener);
        mConsoleListener.setNull();
    }

    unsigned i;
    for (i = 0; i < RT_ELEMENTS(maSourceBitmaps); i++)
    {
        maSourceBitmaps[i].setNull();
    }

    if (mEmWebcam)
    {
        delete mEmWebcam;
        mEmWebcam = NULL;
    }

    if (RTCritSectIsInitialized(&mCritSect))
    {
        RTCritSectDelete(&mCritSect);
        RT_ZERO(mCritSect);
    }

    if (RTCritSectIsInitialized(&mTSMFLock))
    {
        RTCritSectDelete(&mTSMFLock);
        RT_ZERO(mTSMFLock);
    }
}

int ConsoleVRDPServer::Launch(void)
{
    LogFlowThisFunc(("\n"));

    IVRDEServer *server = mConsole->i_getVRDEServer();
    AssertReturn(server, VERR_INTERNAL_ERROR_2);

    /*
     * Check if VRDE is enabled.
     */
    BOOL fEnabled;
    HRESULT hrc = server->COMGETTER(Enabled)(&fEnabled);
    AssertComRCReturn(hrc, Global::vboxStatusCodeFromCOM(hrc));
    if (!fEnabled)
        return VINF_SUCCESS;

    /*
     * Check that a VRDE extension pack name is set and resolve it into a
     * library path.
     */
    Bstr bstrExtPack;
    hrc = server->COMGETTER(VRDEExtPack)(bstrExtPack.asOutParam());
    if (FAILED(hrc))
        return Global::vboxStatusCodeFromCOM(hrc);
    if (bstrExtPack.isEmpty())
        return VINF_NOT_SUPPORTED;

    Utf8Str         strExtPack(bstrExtPack);
    Utf8Str         strVrdeLibrary;
    int             vrc = VINF_SUCCESS;
    if (strExtPack.equals(VBOXVRDP_KLUDGE_EXTPACK_NAME))
        strVrdeLibrary = "VBoxVRDP";
    else
    {
#ifdef VBOX_WITH_EXTPACK
        ExtPackManager *pExtPackMgr = mConsole->i_getExtPackManager();
        vrc = pExtPackMgr->i_getVrdeLibraryPathForExtPack(&strExtPack, &strVrdeLibrary);
#else
        vrc = VERR_FILE_NOT_FOUND;
#endif
    }
    if (RT_SUCCESS(vrc))
    {
        /*
         * Load the VRDE library and start the server, if it is enabled.
         */
        vrc = loadVRDPLibrary(strVrdeLibrary.c_str());
        if (RT_SUCCESS(vrc))
        {
            VRDEENTRYPOINTS_4 *pEntryPoints4;
            vrc = mpfnVRDECreateServer(&mCallbacks.header, this, (VRDEINTERFACEHDR **)&pEntryPoints4, &mhServer);

            if (RT_SUCCESS(vrc))
            {
                mServerInterfaceVersion = 4;
                mEntryPoints = *pEntryPoints4;
                mpEntryPoints = &mEntryPoints;
            }
            else if (vrc == VERR_VERSION_MISMATCH)
            {
                /* An older version of VRDE is installed, try version 3. */
                VRDEENTRYPOINTS_3 *pEntryPoints3;

                static VRDECALLBACKS_3 sCallbacks3 =
                {
                    { VRDE_INTERFACE_VERSION_3, sizeof(VRDECALLBACKS_3) },
                    ConsoleVRDPServer::VRDPCallbackQueryProperty,
                    ConsoleVRDPServer::VRDPCallbackClientLogon,
                    ConsoleVRDPServer::VRDPCallbackClientConnect,
                    ConsoleVRDPServer::VRDPCallbackClientDisconnect,
                    ConsoleVRDPServer::VRDPCallbackIntercept,
                    ConsoleVRDPServer::VRDPCallbackUSB,
                    ConsoleVRDPServer::VRDPCallbackClipboard,
                    ConsoleVRDPServer::VRDPCallbackFramebufferQuery,
                    ConsoleVRDPServer::VRDPCallbackFramebufferLock,
                    ConsoleVRDPServer::VRDPCallbackFramebufferUnlock,
                    ConsoleVRDPServer::VRDPCallbackInput,
                    ConsoleVRDPServer::VRDPCallbackVideoModeHint,
                    ConsoleVRDPServer::VRDECallbackAudioIn
                };

                vrc = mpfnVRDECreateServer(&sCallbacks3.header, this, (VRDEINTERFACEHDR **)&pEntryPoints3, &mhServer);
                if (RT_SUCCESS(vrc))
                {
                    mServerInterfaceVersion = 3;
                    mEntryPoints.header = pEntryPoints3->header;
                    mEntryPoints.VRDEDestroy = pEntryPoints3->VRDEDestroy;
                    mEntryPoints.VRDEEnableConnections = pEntryPoints3->VRDEEnableConnections;
                    mEntryPoints.VRDEDisconnect = pEntryPoints3->VRDEDisconnect;
                    mEntryPoints.VRDEResize = pEntryPoints3->VRDEResize;
                    mEntryPoints.VRDEUpdate = pEntryPoints3->VRDEUpdate;
                    mEntryPoints.VRDEColorPointer = pEntryPoints3->VRDEColorPointer;
                    mEntryPoints.VRDEHidePointer = pEntryPoints3->VRDEHidePointer;
                    mEntryPoints.VRDEAudioSamples = pEntryPoints3->VRDEAudioSamples;
                    mEntryPoints.VRDEAudioVolume = pEntryPoints3->VRDEAudioVolume;
                    mEntryPoints.VRDEUSBRequest = pEntryPoints3->VRDEUSBRequest;
                    mEntryPoints.VRDEClipboard = pEntryPoints3->VRDEClipboard;
                    mEntryPoints.VRDEQueryInfo = pEntryPoints3->VRDEQueryInfo;
                    mEntryPoints.VRDERedirect = pEntryPoints3->VRDERedirect;
                    mEntryPoints.VRDEAudioInOpen = pEntryPoints3->VRDEAudioInOpen;
                    mEntryPoints.VRDEAudioInClose = pEntryPoints3->VRDEAudioInClose;
                    mEntryPoints.VRDEGetInterface = NULL;
                    mpEntryPoints = &mEntryPoints;
                }
                else if (vrc == VERR_VERSION_MISMATCH)
                {
                    /* An older version of VRDE is installed, try version 1. */
                    VRDEENTRYPOINTS_1 *pEntryPoints1;

                    static VRDECALLBACKS_1 sCallbacks1 =
                    {
                        { VRDE_INTERFACE_VERSION_1, sizeof(VRDECALLBACKS_1) },
                        ConsoleVRDPServer::VRDPCallbackQueryProperty,
                        ConsoleVRDPServer::VRDPCallbackClientLogon,
                        ConsoleVRDPServer::VRDPCallbackClientConnect,
                        ConsoleVRDPServer::VRDPCallbackClientDisconnect,
                        ConsoleVRDPServer::VRDPCallbackIntercept,
                        ConsoleVRDPServer::VRDPCallbackUSB,
                        ConsoleVRDPServer::VRDPCallbackClipboard,
                        ConsoleVRDPServer::VRDPCallbackFramebufferQuery,
                        ConsoleVRDPServer::VRDPCallbackFramebufferLock,
                        ConsoleVRDPServer::VRDPCallbackFramebufferUnlock,
                        ConsoleVRDPServer::VRDPCallbackInput,
                        ConsoleVRDPServer::VRDPCallbackVideoModeHint
                    };

                    vrc = mpfnVRDECreateServer(&sCallbacks1.header, this, (VRDEINTERFACEHDR **)&pEntryPoints1, &mhServer);
                    if (RT_SUCCESS(vrc))
                    {
                        mServerInterfaceVersion = 1;
                        mEntryPoints.header = pEntryPoints1->header;
                        mEntryPoints.VRDEDestroy = pEntryPoints1->VRDEDestroy;
                        mEntryPoints.VRDEEnableConnections = pEntryPoints1->VRDEEnableConnections;
                        mEntryPoints.VRDEDisconnect = pEntryPoints1->VRDEDisconnect;
                        mEntryPoints.VRDEResize = pEntryPoints1->VRDEResize;
                        mEntryPoints.VRDEUpdate = pEntryPoints1->VRDEUpdate;
                        mEntryPoints.VRDEColorPointer = pEntryPoints1->VRDEColorPointer;
                        mEntryPoints.VRDEHidePointer = pEntryPoints1->VRDEHidePointer;
                        mEntryPoints.VRDEAudioSamples = pEntryPoints1->VRDEAudioSamples;
                        mEntryPoints.VRDEAudioVolume = pEntryPoints1->VRDEAudioVolume;
                        mEntryPoints.VRDEUSBRequest = pEntryPoints1->VRDEUSBRequest;
                        mEntryPoints.VRDEClipboard = pEntryPoints1->VRDEClipboard;
                        mEntryPoints.VRDEQueryInfo = pEntryPoints1->VRDEQueryInfo;
                        mEntryPoints.VRDERedirect = NULL;
                        mEntryPoints.VRDEAudioInOpen = NULL;
                        mEntryPoints.VRDEAudioInClose = NULL;
                        mEntryPoints.VRDEGetInterface = NULL;
                        mpEntryPoints = &mEntryPoints;
                    }
                }
            }

            if (RT_SUCCESS(vrc))
            {
                LogRel(("VRDE: loaded version %d of the server.\n", mServerInterfaceVersion));

                if (mServerInterfaceVersion >= 4)
                {
                    /* The server supports optional interfaces. */
                    Assert(mpEntryPoints->VRDEGetInterface != NULL);

                    /* Image interface. */
                    m_interfaceImage.header.u64Version = 1;
                    m_interfaceImage.header.u64Size = sizeof(m_interfaceImage);

                    m_interfaceCallbacksImage.header.u64Version = 1;
                    m_interfaceCallbacksImage.header.u64Size = sizeof(m_interfaceCallbacksImage);
                    m_interfaceCallbacksImage.VRDEImageCbNotify = VRDEImageCbNotify;

                    vrc = mpEntryPoints->VRDEGetInterface(mhServer,
                                                          VRDE_IMAGE_INTERFACE_NAME,
                                                          &m_interfaceImage.header,
                                                          &m_interfaceCallbacksImage.header,
                                                          this);
                    if (RT_SUCCESS(vrc))
                    {
                        LogRel(("VRDE: [%s]\n", VRDE_IMAGE_INTERFACE_NAME));
                        m_fInterfaceImage = true;
                    }

                    /* Mouse pointer interface. */
                    m_interfaceMousePtr.header.u64Version = 1;
                    m_interfaceMousePtr.header.u64Size = sizeof(m_interfaceMousePtr);

                    vrc = mpEntryPoints->VRDEGetInterface(mhServer,
                                                          VRDE_MOUSEPTR_INTERFACE_NAME,
                                                          &m_interfaceMousePtr.header,
                                                          NULL,
                                                          this);
                    if (RT_SUCCESS(vrc))
                    {
                        LogRel(("VRDE: [%s]\n", VRDE_MOUSEPTR_INTERFACE_NAME));
                    }
                    else
                    {
                        RT_ZERO(m_interfaceMousePtr);
                    }

                    /* Smartcard interface. */
                    m_interfaceSCard.header.u64Version = 1;
                    m_interfaceSCard.header.u64Size = sizeof(m_interfaceSCard);

                    m_interfaceCallbacksSCard.header.u64Version = 1;
                    m_interfaceCallbacksSCard.header.u64Size = sizeof(m_interfaceCallbacksSCard);
                    m_interfaceCallbacksSCard.VRDESCardCbNotify = VRDESCardCbNotify;
                    m_interfaceCallbacksSCard.VRDESCardCbResponse = VRDESCardCbResponse;

                    vrc = mpEntryPoints->VRDEGetInterface(mhServer,
                                                          VRDE_SCARD_INTERFACE_NAME,
                                                          &m_interfaceSCard.header,
                                                          &m_interfaceCallbacksSCard.header,
                                                          this);
                    if (RT_SUCCESS(vrc))
                    {
                        LogRel(("VRDE: [%s]\n", VRDE_SCARD_INTERFACE_NAME));
                    }
                    else
                    {
                        RT_ZERO(m_interfaceSCard);
                    }

                    /* Raw TSMF interface. */
                    m_interfaceTSMF.header.u64Version = 1;
                    m_interfaceTSMF.header.u64Size = sizeof(m_interfaceTSMF);

                    m_interfaceCallbacksTSMF.header.u64Version = 1;
                    m_interfaceCallbacksTSMF.header.u64Size = sizeof(m_interfaceCallbacksTSMF);
                    m_interfaceCallbacksTSMF.VRDETSMFCbNotify = VRDETSMFCbNotify;

                    vrc = mpEntryPoints->VRDEGetInterface(mhServer,
                                                          VRDE_TSMF_INTERFACE_NAME,
                                                          &m_interfaceTSMF.header,
                                                          &m_interfaceCallbacksTSMF.header,
                                                          this);
                    if (RT_SUCCESS(vrc))
                    {
                        LogRel(("VRDE: [%s]\n", VRDE_TSMF_INTERFACE_NAME));
                    }
                    else
                    {
                        RT_ZERO(m_interfaceTSMF);
                    }

                    /* VideoIn interface. */
                    m_interfaceVideoIn.header.u64Version = 1;
                    m_interfaceVideoIn.header.u64Size = sizeof(m_interfaceVideoIn);

                    m_interfaceCallbacksVideoIn.header.u64Version = 1;
                    m_interfaceCallbacksVideoIn.header.u64Size = sizeof(m_interfaceCallbacksVideoIn);
                    m_interfaceCallbacksVideoIn.VRDECallbackVideoInNotify = VRDECallbackVideoInNotify;
                    m_interfaceCallbacksVideoIn.VRDECallbackVideoInDeviceDesc = VRDECallbackVideoInDeviceDesc;
                    m_interfaceCallbacksVideoIn.VRDECallbackVideoInControl = VRDECallbackVideoInControl;
                    m_interfaceCallbacksVideoIn.VRDECallbackVideoInFrame = VRDECallbackVideoInFrame;

                    vrc = mpEntryPoints->VRDEGetInterface(mhServer,
                                                          VRDE_VIDEOIN_INTERFACE_NAME,
                                                          &m_interfaceVideoIn.header,
                                                          &m_interfaceCallbacksVideoIn.header,
                                                          this);
                    if (RT_SUCCESS(vrc))
                    {
                        LogRel(("VRDE: [%s]\n", VRDE_VIDEOIN_INTERFACE_NAME));
                    }
                    else
                    {
                        RT_ZERO(m_interfaceVideoIn);
                    }

                    /* Input interface. */
                    m_interfaceInput.header.u64Version = 1;
                    m_interfaceInput.header.u64Size = sizeof(m_interfaceInput);

                    m_interfaceCallbacksInput.header.u64Version = 1;
                    m_interfaceCallbacksInput.header.u64Size = sizeof(m_interfaceCallbacksInput);
                    m_interfaceCallbacksInput.VRDECallbackInputSetup = VRDECallbackInputSetup;
                    m_interfaceCallbacksInput.VRDECallbackInputEvent = VRDECallbackInputEvent;

                    vrc = mpEntryPoints->VRDEGetInterface(mhServer,
                                                          VRDE_INPUT_INTERFACE_NAME,
                                                          &m_interfaceInput.header,
                                                          &m_interfaceCallbacksInput.header,
                                                          this);
                    if (RT_SUCCESS(vrc))
                    {
                        LogRel(("VRDE: [%s]\n", VRDE_INPUT_INTERFACE_NAME));
                    }
                    else
                    {
                        RT_ZERO(m_interfaceInput);
                    }

                    /* Since these interfaces are optional, it is always a success here. */
                    vrc = VINF_SUCCESS;
                }
#ifdef VBOX_WITH_USB
                remoteUSBThreadStart();
#endif

                /*
                 * Re-init the server current state, which is usually obtained from events.
                 */
                fetchCurrentState();
            }
            else
            {
                if (vrc != VERR_NET_ADDRESS_IN_USE)
                    LogRel(("VRDE: Could not start the server vrc = %Rrc\n", vrc));
                /* Don't unload the lib, because it prevents us trying again or
                   because there may be other users? */
            }
        }
    }

    return vrc;
}

void ConsoleVRDPServer::fetchCurrentState(void)
{
    ComPtr<IMousePointerShape> mps;
    mConsole->i_getMouse()->COMGETTER(PointerShape)(mps.asOutParam());
    if (!mps.isNull())
    {
        BOOL  visible, alpha;
        ULONG hotX, hotY, width, height;
        com::SafeArray <BYTE> shape;

        mps->COMGETTER(Visible)(&visible);
        mps->COMGETTER(Alpha)(&alpha);
        mps->COMGETTER(HotX)(&hotX);
        mps->COMGETTER(HotY)(&hotY);
        mps->COMGETTER(Width)(&width);
        mps->COMGETTER(Height)(&height);
        mps->COMGETTER(Shape)(ComSafeArrayAsOutParam(shape));

        onMousePointerShapeChange(visible, alpha, hotX, hotY, width, height, ComSafeArrayAsInParam(shape));
    }
}

#if 0 /** @todo Chromium got removed (see @bugref{9529}) and this is not available for VMSVGA yet. */
typedef struct H3DORInstance
{
    ConsoleVRDPServer *pThis;
    HVRDEIMAGE hImageBitmap;
    int32_t x;
    int32_t y;
    uint32_t w;
    uint32_t h;
    bool fCreated;
    bool fFallback;
    bool fTopDown;
} H3DORInstance;

#define H3DORLOG Log

/* static */ DECLCALLBACK(void) ConsoleVRDPServer::H3DORBegin(const void *pvContext, void **ppvInstance,
                                                              const char *pszFormat)
{
    H3DORLOG(("H3DORBegin: ctx %p [%s]\n", pvContext, pszFormat));

    H3DORInstance *p = (H3DORInstance *)RTMemAlloc(sizeof(H3DORInstance));

    if (p)
    {
        p->pThis = (ConsoleVRDPServer *)pvContext;
        p->hImageBitmap = NULL;
        p->x = 0;
        p->y = 0;
        p->w = 0;
        p->h = 0;
        p->fCreated = false;
        p->fFallback = false;

        /* Host 3D service passes the actual format of data in this redirect instance.
         * That is what will be in the H3DORFrame's parameters pvData and cbData.
         */
        if (RTStrICmp(pszFormat, H3DOR_FMT_RGBA_TOPDOWN) == 0)
        {
            /* Accept it. */
            p->fTopDown = true;
        }
        else if (RTStrICmp(pszFormat, H3DOR_FMT_RGBA) == 0)
        {
            /* Accept it. */
            p->fTopDown = false;
        }
        else
        {
            RTMemFree(p);
            p = NULL;
        }
    }

    H3DORLOG(("H3DORBegin: ins %p\n", p));

    /* Caller checks this for NULL. */
    *ppvInstance = p;
}

/* static */ DECLCALLBACK(void) ConsoleVRDPServer::H3DORGeometry(void *pvInstance,
                                                                 int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    H3DORLOG(("H3DORGeometry: ins %p %d,%d %dx%d\n", pvInstance, x, y, w, h));

    H3DORInstance *p = (H3DORInstance *)pvInstance;
    AssertPtrReturnVoid(p);
    AssertPtrReturnVoid(p->pThis);

    /** @todo find out what to do if size changes to 0x0 from non zero */
    if (w == 0 || h == 0)
    {
        /* Do nothing. */
        return;
    }

    RTRECT rect;
    rect.xLeft = x;
    rect.yTop = y;
    rect.xRight = x + w;
    rect.yBottom = y + h;

    if (p->hImageBitmap)
    {
        /* An image handle has been already created,
         * check if it has the same size as the reported geometry.
         */
        if (   p->x == x
            && p->y == y
            && p->w == w
            && p->h == h)
        {
            H3DORLOG(("H3DORGeometry: geometry not changed\n"));
            /* Do nothing. Continue using the existing handle. */
        }
        else
        {
            int vrc = p->fFallback
                    ? VERR_NOT_SUPPORTED /* Try to go out of fallback mode. */
                    : p->pThis->m_interfaceImage.VRDEImageGeometrySet(p->hImageBitmap, &rect);
            if (RT_SUCCESS(vrc))
            {
                p->x = x;
                p->y = y;
                p->w = w;
                p->h = h;
            }
            else
            {
                /* The handle must be recreated. Delete existing handle here. */
                p->pThis->m_interfaceImage.VRDEImageHandleClose(p->hImageBitmap);
                p->hImageBitmap = NULL;
            }
        }
    }

    if (!p->hImageBitmap)
    {
        /* Create a new bitmap handle. */
        uint32_t u32ScreenId = 0; /** @todo clip to corresponding screens.
                                   * Clipping can be done here or in VRDP server.
                                   * If VRDP does clipping, then uScreenId parameter
                                   * is not necessary and coords must be global.
                                   * (have to check which coords are used in opengl service).
                                   * Since all VRDE API uses a ScreenId,
                                   * the clipping must be done here in ConsoleVRDPServer
                                   */
        uint32_t fu32CompletionFlags = 0;
        p->fFallback = false;
        int vrc = p->pThis->m_interfaceImage.VRDEImageHandleCreate(p->pThis->mhServer,
                                                                   &p->hImageBitmap,
                                                                   p,
                                                                   u32ScreenId,
                                                                   VRDE_IMAGE_F_CREATE_CONTENT_3D
                                                                   | VRDE_IMAGE_F_CREATE_WINDOW,
                                                                   &rect,
                                                                   VRDE_IMAGE_FMT_ID_BITMAP_BGRA8,
                                                                   NULL,
                                                                   0,
                                                                   &fu32CompletionFlags);
        if (RT_FAILURE(vrc))
        {
            /* No support for a 3D + WINDOW. Try bitmap updates. */
            H3DORLOG(("H3DORGeometry: Fallback to bitmaps\n"));
            fu32CompletionFlags = 0;
            p->fFallback = true;
            vrc = p->pThis->m_interfaceImage.VRDEImageHandleCreate(p->pThis->mhServer,
                                                                   &p->hImageBitmap,
                                                                   p,
                                                                   u32ScreenId,
                                                                   0,
                                                                   &rect,
                                                                   VRDE_IMAGE_FMT_ID_BITMAP_BGRA8,
                                                                   NULL,
                                                                   0,
                                                                   &fu32CompletionFlags);
        }

        H3DORLOG(("H3DORGeometry: Image handle create %Rrc, flags 0x%RX32\n", vrc, fu32CompletionFlags));

        if (RT_SUCCESS(vrc))
        {
            p->x = x;
            p->y = y;
            p->w = w;
            p->h = h;

            if ((fu32CompletionFlags & VRDE_IMAGE_F_COMPLETE_ASYNC) == 0)
            {
                p->fCreated = true;
            }
        }
        else
        {
            p->hImageBitmap = NULL;
            p->w = 0;
            p->h = 0;
        }
    }

    H3DORLOG(("H3DORGeometry: ins %p completed\n", pvInstance));
}

/* static */ DECLCALLBACK(void) ConsoleVRDPServer::H3DORVisibleRegion(void *pvInstance,
                                                                      uint32_t cRects, const RTRECT *paRects)
{
    H3DORLOG(("H3DORVisibleRegion: ins %p %d\n", pvInstance, cRects));

    H3DORInstance *p = (H3DORInstance *)pvInstance;
    AssertPtrReturnVoid(p);
    AssertPtrReturnVoid(p->pThis);

    if (cRects == 0)
    {
        /* Complete image is visible. */
        RTRECT rect;
        rect.xLeft = p->x;
        rect.yTop = p->y;
        rect.xRight = p->x + p->w;
        rect.yBottom = p->y + p->h;
        p->pThis->m_interfaceImage.VRDEImageRegionSet (p->hImageBitmap,
                                                       1,
                                                       &rect);
    }
    else
    {
        p->pThis->m_interfaceImage.VRDEImageRegionSet (p->hImageBitmap,
                                                       cRects,
                                                       paRects);
    }

    H3DORLOG(("H3DORVisibleRegion: ins %p completed\n", pvInstance));
}

/* static */ DECLCALLBACK(void) ConsoleVRDPServer::H3DORFrame(void *pvInstance,
                                                              void *pvData, uint32_t cbData)
{
    H3DORLOG(("H3DORFrame: ins %p %p %d\n", pvInstance, pvData, cbData));

    H3DORInstance *p = (H3DORInstance *)pvInstance;
    AssertPtrReturnVoid(p);
    AssertPtrReturnVoid(p->pThis);

    /* Currently only a topdown BGR0 bitmap format is supported. */
    VRDEIMAGEBITMAP image;

    image.cWidth = p->w;
    image.cHeight = p->h;
    image.pvData = pvData;
    image.cbData = cbData;
    image.pvScanLine0 = (uint8_t *)pvData + (p->h - 1) * p->w * 4;
    image.iScanDelta = 4 * p->w;
    if (p->fTopDown)
    {
        image.iScanDelta = -image.iScanDelta;
    }

    p->pThis->m_interfaceImage.VRDEImageUpdate (p->hImageBitmap,
                                                p->x,
                                                p->y,
                                                p->w,
                                                p->h,
                                                &image,
                                                sizeof(VRDEIMAGEBITMAP));

    H3DORLOG(("H3DORFrame: ins %p completed\n", pvInstance));
}

/* static */ DECLCALLBACK(void) ConsoleVRDPServer::H3DOREnd(void *pvInstance)
{
    H3DORLOG(("H3DOREnd: ins %p\n", pvInstance));

    H3DORInstance *p = (H3DORInstance *)pvInstance;
    AssertPtrReturnVoid(p);
    AssertPtrReturnVoid(p->pThis);

    p->pThis->m_interfaceImage.VRDEImageHandleClose(p->hImageBitmap);

    RT_ZERO(*p);
    RTMemFree(p);

    H3DORLOG(("H3DOREnd: ins %p completed\n", pvInstance));
}

/* static */ DECLCALLBACK(int) ConsoleVRDPServer::H3DORContextProperty(const void *pvContext, uint32_t index,
                                                                       void *pvBuffer, uint32_t cbBuffer, uint32_t *pcbOut)
{
    RT_NOREF(pvContext, pvBuffer);
    int vrc = VINF_SUCCESS;

    H3DORLOG(("H3DORContextProperty: index %d\n", index));

    if (index == H3DOR_PROP_FORMATS)
    {
        /* Return a comma separated list of supported formats. */
        uint32_t cbOut =   (uint32_t)strlen(H3DOR_FMT_RGBA_TOPDOWN) + 1
                         + (uint32_t)strlen(H3DOR_FMT_RGBA) + 1;
        if (cbOut <= cbBuffer)
        {
            char *pch = (char *)pvBuffer;
            memcpy(pch, H3DOR_FMT_RGBA_TOPDOWN, strlen(H3DOR_FMT_RGBA_TOPDOWN));
            pch += strlen(H3DOR_FMT_RGBA_TOPDOWN);
            *pch++ = ',';
            memcpy(pch, H3DOR_FMT_RGBA, strlen(H3DOR_FMT_RGBA));
            pch += strlen(H3DOR_FMT_RGBA);
            *pch++ = '\0';
        }
        else
        {
            vrc = VERR_BUFFER_OVERFLOW;
        }
        *pcbOut = cbOut;
    }
    else
    {
        vrc = VERR_NOT_SUPPORTED;
    }

    H3DORLOG(("H3DORContextProperty: %Rrc\n", vrc));
    return vrc;
}
#endif

void ConsoleVRDPServer::remote3DRedirect(bool fEnable)
{
    if (!m_fInterfaceImage)
    {
        /* No redirect without corresponding interface. */
        return;
    }

    /* Check if 3D redirection has been enabled. It is enabled by default. */
    com::Bstr bstr;
    HRESULT hrc = mConsole->i_getVRDEServer()->GetVRDEProperty(Bstr("H3DRedirect/Enabled").raw(), bstr.asOutParam());

    com::Utf8Str value = hrc == S_OK? bstr: "";

    bool fAllowed =    RTStrICmp(value.c_str(), "true") == 0
                    || RTStrICmp(value.c_str(), "1") == 0
                    || value.c_str()[0] == 0;

    if (!fAllowed && fEnable)
    {
        return;
    }

#if 0 /** @todo Implement again for VMSVGA. */
    /* Tell the host 3D service to redirect output using the ConsoleVRDPServer callbacks. */
    H3DOUTPUTREDIRECT outputRedirect =
    {
        this,
        H3DORBegin,
        H3DORGeometry,
        H3DORVisibleRegion,
        H3DORFrame,
        H3DOREnd,
        H3DORContextProperty
    };

    if (!fEnable)
    {
        /* This will tell the service to disable rediection. */
        RT_ZERO(outputRedirect);
    }
#endif

    return;
}

/* static */ DECLCALLBACK(int) ConsoleVRDPServer::VRDEImageCbNotify (void *pvContext,
                                                                     void *pvUser,
                                                                     HVRDEIMAGE hVideo,
                                                                     uint32_t u32Id,
                                                                     void *pvData,
                                                                     uint32_t cbData)
{
    RT_NOREF(hVideo);
    Log(("H3DOR: VRDEImageCbNotify: pvContext %p, pvUser %p, hVideo %p, u32Id %u, pvData %p, cbData %d\n",
              pvContext, pvUser, hVideo, u32Id, pvData, cbData));

    ConsoleVRDPServer *pServer = static_cast<ConsoleVRDPServer*>(pvContext); NOREF(pServer);

#if 0 /** @todo Implement again for VMSVGA. */
    H3DORInstance *p = (H3DORInstance *)pvUser;
    Assert(p);
    Assert(p->pThis);
    Assert(p->pThis == pServer);

    if (u32Id == VRDE_IMAGE_NOTIFY_HANDLE_CREATE)
    {
        if (cbData != sizeof(uint32_t))
        {
            AssertFailed();
            return VERR_INVALID_PARAMETER;
        }

        uint32_t u32StreamId = *(uint32_t *)pvData;
        Log(("H3DOR: VRDE_IMAGE_NOTIFY_HANDLE_CREATE u32StreamId %d\n",
                  u32StreamId));

        if (u32StreamId != 0)
        {
            p->fCreated = true; /// @todo not needed?
        }
        else
        {
           /* The stream has not been created. */
        }
    }
#else
    RT_NOREF(pvUser, u32Id, pvData, cbData);
#endif

    return VINF_SUCCESS;
}

#undef H3DORLOG

/* static */ DECLCALLBACK(int) ConsoleVRDPServer::VRDESCardCbNotify(void *pvContext,
                                                                    uint32_t u32Id,
                                                                    void *pvData,
                                                                    uint32_t cbData)
{
#ifdef VBOX_WITH_USB_CARDREADER
    ConsoleVRDPServer *pThis = static_cast<ConsoleVRDPServer*>(pvContext);
    UsbCardReader *pReader = pThis->mConsole->i_getUsbCardReader();
    return pReader->VRDENotify(u32Id, pvData, cbData);
#else
    NOREF(pvContext);
    NOREF(u32Id);
    NOREF(pvData);
    NOREF(cbData);
    return VERR_NOT_SUPPORTED;
#endif
}

/* static */ DECLCALLBACK(int) ConsoleVRDPServer::VRDESCardCbResponse(void *pvContext,
                                                                      int vrcRequest,
                                                                      void *pvUser,
                                                                      uint32_t u32Function,
                                                                      void *pvData,
                                                                      uint32_t cbData)
{
#ifdef VBOX_WITH_USB_CARDREADER
    ConsoleVRDPServer *pThis = static_cast<ConsoleVRDPServer*>(pvContext);
    UsbCardReader *pReader = pThis->mConsole->i_getUsbCardReader();
    return pReader->VRDEResponse(vrcRequest, pvUser, u32Function, pvData, cbData);
#else
    NOREF(pvContext);
    NOREF(vrcRequest);
    NOREF(pvUser);
    NOREF(u32Function);
    NOREF(pvData);
    NOREF(cbData);
    return VERR_NOT_SUPPORTED;
#endif
}

int ConsoleVRDPServer::SCardRequest(void *pvUser, uint32_t u32Function, const void *pvData, uint32_t cbData)
{
    int vrc = VINF_SUCCESS;

    if (mhServer && mpEntryPoints && m_interfaceSCard.VRDESCardRequest)
    {
        vrc = m_interfaceSCard.VRDESCardRequest(mhServer, pvUser, u32Function, pvData, cbData);
    }
    else
    {
        vrc = VERR_NOT_SUPPORTED;
    }

    return vrc;
}


struct TSMFHOSTCHCTX;
struct TSMFVRDPCTX;

typedef struct TSMFHOSTCHCTX
{
    ConsoleVRDPServer *pThis;

    struct TSMFVRDPCTX *pVRDPCtx; /* NULL if no corresponding host channel context. */

    void *pvDataReceived;
    uint32_t cbDataReceived;
    uint32_t cbDataAllocated;
} TSMFHOSTCHCTX;

typedef struct TSMFVRDPCTX
{
    ConsoleVRDPServer *pThis;

    VBOXHOSTCHANNELCALLBACKS *pCallbacks;
    void *pvCallbacks;

    TSMFHOSTCHCTX *pHostChCtx; /* NULL if no corresponding host channel context. */

    uint32_t u32ChannelHandle;
} TSMFVRDPCTX;

static int tsmfContextsAlloc(TSMFHOSTCHCTX **ppHostChCtx, TSMFVRDPCTX **ppVRDPCtx)
{
    TSMFHOSTCHCTX *pHostChCtx = (TSMFHOSTCHCTX *)RTMemAllocZ(sizeof(TSMFHOSTCHCTX));
    if (!pHostChCtx)
    {
        return VERR_NO_MEMORY;
    }

    TSMFVRDPCTX *pVRDPCtx = (TSMFVRDPCTX *)RTMemAllocZ(sizeof(TSMFVRDPCTX));
    if (!pVRDPCtx)
    {
        RTMemFree(pHostChCtx);
        return VERR_NO_MEMORY;
    }

    *ppHostChCtx = pHostChCtx;
    *ppVRDPCtx = pVRDPCtx;
    return VINF_SUCCESS;
}

int ConsoleVRDPServer::tsmfLock(void)
{
    int vrc = RTCritSectEnter(&mTSMFLock);
    AssertRC(vrc);
    return vrc;
}

void ConsoleVRDPServer::tsmfUnlock(void)
{
    RTCritSectLeave(&mTSMFLock);
}

/* static */ DECLCALLBACK(int) ConsoleVRDPServer::tsmfHostChannelAttach(void *pvProvider,
                                                                        void **ppvChannel,
                                                                        uint32_t u32Flags,
                                                                        VBOXHOSTCHANNELCALLBACKS *pCallbacks,
                                                                        void *pvCallbacks)
{
    LogFlowFunc(("\n"));

    ConsoleVRDPServer *pThis = static_cast<ConsoleVRDPServer*>(pvProvider);

    /* Create 2 context structures: for the VRDP server and for the host service. */
    TSMFHOSTCHCTX *pHostChCtx = NULL;
    TSMFVRDPCTX *pVRDPCtx = NULL;

    int vrc = tsmfContextsAlloc(&pHostChCtx, &pVRDPCtx);
    if (RT_FAILURE(vrc))
    {
        return vrc;
    }

    pHostChCtx->pThis = pThis;
    pHostChCtx->pVRDPCtx = pVRDPCtx;

    pVRDPCtx->pThis = pThis;
    pVRDPCtx->pCallbacks = pCallbacks;
    pVRDPCtx->pvCallbacks = pvCallbacks;
    pVRDPCtx->pHostChCtx = pHostChCtx;

    vrc = pThis->m_interfaceTSMF.VRDETSMFChannelCreate(pThis->mhServer, pVRDPCtx, u32Flags);

    if (RT_SUCCESS(vrc))
    {
        /** @todo contexts should be in a list for accounting. */
        *ppvChannel = pHostChCtx;
    }
    else
    {
        RTMemFree(pHostChCtx);
        RTMemFree(pVRDPCtx);
    }

    return vrc;
}

/* static */ DECLCALLBACK(void) ConsoleVRDPServer::tsmfHostChannelDetach(void *pvChannel)
{
    LogFlowFunc(("\n"));

    TSMFHOSTCHCTX *pHostChCtx = (TSMFHOSTCHCTX *)pvChannel;
    ConsoleVRDPServer *pThis = pHostChCtx->pThis;

    int vrc = pThis->tsmfLock();
    if (RT_SUCCESS(vrc))
    {
        bool fClose = false;
        uint32_t u32ChannelHandle = 0;

        if (pHostChCtx->pVRDPCtx)
        {
            /* There is still a VRDP context for this channel. */
            pHostChCtx->pVRDPCtx->pHostChCtx = NULL;
            u32ChannelHandle = pHostChCtx->pVRDPCtx->u32ChannelHandle;
            fClose = true;
        }

        pThis->tsmfUnlock();

        RTMemFree(pHostChCtx);

        if (fClose)
        {
            LogFlowFunc(("Closing VRDE channel %d.\n", u32ChannelHandle));
            pThis->m_interfaceTSMF.VRDETSMFChannelClose(pThis->mhServer, u32ChannelHandle);
        }
        else
        {
            LogFlowFunc(("No VRDE channel.\n"));
        }
    }
}

/* static */ DECLCALLBACK(int) ConsoleVRDPServer::tsmfHostChannelSend(void *pvChannel,
                                                                      const void *pvData,
                                                                      uint32_t cbData)
{
    LogFlowFunc(("cbData %d\n", cbData));

    TSMFHOSTCHCTX *pHostChCtx = (TSMFHOSTCHCTX *)pvChannel;
    ConsoleVRDPServer *pThis = pHostChCtx->pThis;

    int vrc = pThis->tsmfLock();
    if (RT_SUCCESS(vrc))
    {
        bool fSend = false;
        uint32_t u32ChannelHandle = 0;

        if (pHostChCtx->pVRDPCtx)
        {
            u32ChannelHandle = pHostChCtx->pVRDPCtx->u32ChannelHandle;
            fSend = true;
        }

        pThis->tsmfUnlock();

        if (fSend)
        {
            LogFlowFunc(("Send to VRDE channel %d.\n", u32ChannelHandle));
            vrc = pThis->m_interfaceTSMF.VRDETSMFChannelSend(pThis->mhServer, u32ChannelHandle,
                                                             pvData, cbData);
        }
    }

    return vrc;
}

/* static */ DECLCALLBACK(int) ConsoleVRDPServer::tsmfHostChannelRecv(void *pvChannel,
                                                                      void *pvData,
                                                                      uint32_t cbData,
                                                                      uint32_t *pcbReceived,
                                                                      uint32_t *pcbRemaining)
{
    LogFlowFunc(("cbData %d\n", cbData));

    TSMFHOSTCHCTX *pHostChCtx = (TSMFHOSTCHCTX *)pvChannel;
    ConsoleVRDPServer *pThis = pHostChCtx->pThis;

    int vrc = pThis->tsmfLock();
    if (RT_SUCCESS(vrc))
    {
        uint32_t cbToCopy = RT_MIN(cbData, pHostChCtx->cbDataReceived);
        uint32_t cbRemaining = pHostChCtx->cbDataReceived - cbToCopy;

        LogFlowFunc(("cbToCopy %d, cbRemaining %d\n", cbToCopy, cbRemaining));

        if (cbToCopy != 0)
        {
            memcpy(pvData, pHostChCtx->pvDataReceived, cbToCopy);

            if (cbRemaining != 0)
            {
                memmove(pHostChCtx->pvDataReceived, (uint8_t *)pHostChCtx->pvDataReceived + cbToCopy, cbRemaining);
            }

            pHostChCtx->cbDataReceived = cbRemaining;
        }

        pThis->tsmfUnlock();

        *pcbRemaining = cbRemaining;
        *pcbReceived = cbToCopy;
    }

    return vrc;
}

/* static */ DECLCALLBACK(int) ConsoleVRDPServer::tsmfHostChannelControl(void *pvChannel,
                                                                         uint32_t u32Code,
                                                                         const void *pvParm,
                                                                         uint32_t cbParm,
                                                                         const void *pvData,
                                                                         uint32_t cbData,
                                                                         uint32_t *pcbDataReturned)
{
    RT_NOREF(pvParm, cbParm, pvData, cbData);
    LogFlowFunc(("u32Code %u\n", u32Code));

    if (!pvChannel)
    {
        /* Special case, the provider must answer rather than a channel instance. */
        if (u32Code == VBOX_HOST_CHANNEL_CTRL_EXISTS)
        {
            *pcbDataReturned = 0;
            return VINF_SUCCESS;
        }

        return VERR_NOT_IMPLEMENTED;
    }

    /* Channels do not support this. */
    return VERR_NOT_IMPLEMENTED;
}


void ConsoleVRDPServer::setupTSMF(void)
{
    if (m_interfaceTSMF.header.u64Size == 0)
    {
        return;
    }

    /* Register with the host channel service. */
    VBOXHOSTCHANNELINTERFACE hostChannelInterface =
    {
        this,
        tsmfHostChannelAttach,
        tsmfHostChannelDetach,
        tsmfHostChannelSend,
        tsmfHostChannelRecv,
        tsmfHostChannelControl
    };

    VBoxHostChannelHostRegister parms;

    static char szProviderName[] = "/vrde/tsmf";

    parms.name.type = VBOX_HGCM_SVC_PARM_PTR;
    parms.name.u.pointer.addr = &szProviderName[0];
    parms.name.u.pointer.size = sizeof(szProviderName);

    parms.iface.type = VBOX_HGCM_SVC_PARM_PTR;
    parms.iface.u.pointer.addr = &hostChannelInterface;
    parms.iface.u.pointer.size = sizeof(hostChannelInterface);

    VMMDev *pVMMDev = mConsole->i_getVMMDev();

    if (!pVMMDev)
    {
        AssertMsgFailed(("setupTSMF no vmmdev\n"));
        return;
    }

    int vrc = pVMMDev->hgcmHostCall("VBoxHostChannel",
                                    VBOX_HOST_CHANNEL_HOST_FN_REGISTER,
                                    2,
                                    &parms.name);

    if (!RT_SUCCESS(vrc))
    {
        Log(("VBOX_HOST_CHANNEL_HOST_FN_REGISTER failed with %Rrc\n", vrc));
        return;
    }

    LogRel(("VRDE: Enabled TSMF channel.\n"));

    return;
}

/** @todo these defines must be in a header, which is used by guest component as well. */
#define VBOX_TSMF_HCH_CREATE_ACCEPTED (VBOX_HOST_CHANNEL_EVENT_USER + 0)
#define VBOX_TSMF_HCH_CREATE_DECLINED (VBOX_HOST_CHANNEL_EVENT_USER + 1)
#define VBOX_TSMF_HCH_DISCONNECTED    (VBOX_HOST_CHANNEL_EVENT_USER + 2)

/* static */ DECLCALLBACK(void) ConsoleVRDPServer::VRDETSMFCbNotify(void *pvContext,
                                                                    uint32_t u32Notification,
                                                                    void *pvChannel,
                                                                    const void *pvParm,
                                                                    uint32_t cbParm)
{
    RT_NOREF(cbParm);
    int vrc = VINF_SUCCESS;

    ConsoleVRDPServer *pThis = static_cast<ConsoleVRDPServer*>(pvContext);

    TSMFVRDPCTX *pVRDPCtx = (TSMFVRDPCTX *)pvChannel;

    Assert(pVRDPCtx->pThis == pThis);

    if (pVRDPCtx->pCallbacks == NULL)
    {
        LogFlowFunc(("tsmfHostChannel: Channel disconnected. Skipping.\n"));
        return;
    }

    switch (u32Notification)
    {
        case VRDE_TSMF_N_CREATE_ACCEPTED:
        {
            VRDETSMFNOTIFYCREATEACCEPTED *p = (VRDETSMFNOTIFYCREATEACCEPTED *)pvParm;
            Assert(cbParm == sizeof(VRDETSMFNOTIFYCREATEACCEPTED));

            LogFlowFunc(("tsmfHostChannel: VRDE_TSMF_N_CREATE_ACCEPTED(%p): p->u32ChannelHandle %d\n",
                          pVRDPCtx, p->u32ChannelHandle));

            pVRDPCtx->u32ChannelHandle = p->u32ChannelHandle;

            pVRDPCtx->pCallbacks->HostChannelCallbackEvent(pVRDPCtx->pvCallbacks, pVRDPCtx->pHostChCtx,
                                                           VBOX_TSMF_HCH_CREATE_ACCEPTED,
                                                           NULL, 0);
        } break;

        case VRDE_TSMF_N_CREATE_DECLINED:
        {
            LogFlowFunc(("tsmfHostChannel: VRDE_TSMF_N_CREATE_DECLINED(%p)\n", pVRDPCtx));

            pVRDPCtx->pCallbacks->HostChannelCallbackEvent(pVRDPCtx->pvCallbacks, pVRDPCtx->pHostChCtx,
                                                           VBOX_TSMF_HCH_CREATE_DECLINED,
                                                           NULL, 0);
        } break;

        case VRDE_TSMF_N_DATA:
        {
            /* Save the data in the intermediate buffer and send the event. */
            VRDETSMFNOTIFYDATA *p = (VRDETSMFNOTIFYDATA *)pvParm;
            Assert(cbParm == sizeof(VRDETSMFNOTIFYDATA));

            LogFlowFunc(("tsmfHostChannel: VRDE_TSMF_N_DATA(%p): p->cbData %d\n", pVRDPCtx, p->cbData));

            VBOXHOSTCHANNELEVENTRECV ev;
            ev.u32SizeAvailable = 0;

            vrc = pThis->tsmfLock();

            if (RT_SUCCESS(vrc))
            {
                TSMFHOSTCHCTX *pHostChCtx = pVRDPCtx->pHostChCtx;

                if (pHostChCtx)
                {
                    if (pHostChCtx->pvDataReceived)
                    {
                        uint32_t cbAlloc = p->cbData + pHostChCtx->cbDataReceived;
                        pHostChCtx->pvDataReceived = RTMemRealloc(pHostChCtx->pvDataReceived, cbAlloc);
                        memcpy((uint8_t *)pHostChCtx->pvDataReceived + pHostChCtx->cbDataReceived, p->pvData, p->cbData);

                        pHostChCtx->cbDataReceived += p->cbData;
                        pHostChCtx->cbDataAllocated = cbAlloc;
                    }
                    else
                    {
                        pHostChCtx->pvDataReceived = RTMemAlloc(p->cbData);
                        memcpy(pHostChCtx->pvDataReceived, p->pvData, p->cbData);

                        pHostChCtx->cbDataReceived = p->cbData;
                        pHostChCtx->cbDataAllocated = p->cbData;
                    }

                    ev.u32SizeAvailable = p->cbData;
                }
                else
                {
                    LogFlowFunc(("tsmfHostChannel: VRDE_TSMF_N_DATA: no host channel. Skipping\n"));
                }

                pThis->tsmfUnlock();
            }

            pVRDPCtx->pCallbacks->HostChannelCallbackEvent(pVRDPCtx->pvCallbacks, pVRDPCtx->pHostChCtx,
                                                           VBOX_HOST_CHANNEL_EVENT_RECV,
                                                           &ev, sizeof(ev));
        } break;

        case VRDE_TSMF_N_DISCONNECTED:
        {
            LogFlowFunc(("tsmfHostChannel: VRDE_TSMF_N_DISCONNECTED(%p)\n", pVRDPCtx));

            pVRDPCtx->pCallbacks->HostChannelCallbackEvent(pVRDPCtx->pvCallbacks, pVRDPCtx->pHostChCtx,
                                                           VBOX_TSMF_HCH_DISCONNECTED,
                                                           NULL, 0);

            /* The callback context will not be used anymore. */
            pVRDPCtx->pCallbacks->HostChannelCallbackDeleted(pVRDPCtx->pvCallbacks, pVRDPCtx->pHostChCtx);
            pVRDPCtx->pCallbacks = NULL;
            pVRDPCtx->pvCallbacks = NULL;

            vrc = pThis->tsmfLock();
            if (RT_SUCCESS(vrc))
            {
                if (pVRDPCtx->pHostChCtx)
                {
                    /* There is still a host channel context for this channel. */
                    pVRDPCtx->pHostChCtx->pVRDPCtx = NULL;
                }

                pThis->tsmfUnlock();

                RT_ZERO(*pVRDPCtx);
                RTMemFree(pVRDPCtx);
            }
        } break;

        default:
        {
            AssertFailed();
        } break;
    }
}

/* static */ DECLCALLBACK(void) ConsoleVRDPServer::VRDECallbackVideoInNotify(void *pvCallback,
                                                                       uint32_t u32Id,
                                                                       const void *pvData,
                                                                       uint32_t cbData)
{
    ConsoleVRDPServer *pThis = static_cast<ConsoleVRDPServer*>(pvCallback);
    if (pThis->mEmWebcam)
    {
        pThis->mEmWebcam->EmWebcamCbNotify(u32Id, pvData, cbData);
    }
}

/* static */ DECLCALLBACK(void) ConsoleVRDPServer::VRDECallbackVideoInDeviceDesc(void *pvCallback,
                                                                                 int vrcRequest,
                                                                                 void *pDeviceCtx,
                                                                                 void *pvUser,
                                                                                 const VRDEVIDEOINDEVICEDESC *pDeviceDesc,
                                                                                 uint32_t cbDevice)
{
    ConsoleVRDPServer *pThis = static_cast<ConsoleVRDPServer*>(pvCallback);
    if (pThis->mEmWebcam)
    {
        pThis->mEmWebcam->EmWebcamCbDeviceDesc(vrcRequest, pDeviceCtx, pvUser, pDeviceDesc, cbDevice);
    }
}

/* static */ DECLCALLBACK(void) ConsoleVRDPServer::VRDECallbackVideoInControl(void *pvCallback,
                                                                              int vrcRequest,
                                                                              void *pDeviceCtx,
                                                                              void *pvUser,
                                                                              const VRDEVIDEOINCTRLHDR *pControl,
                                                                              uint32_t cbControl)
{
    ConsoleVRDPServer *pThis = static_cast<ConsoleVRDPServer*>(pvCallback);
    if (pThis->mEmWebcam)
    {
        pThis->mEmWebcam->EmWebcamCbControl(vrcRequest, pDeviceCtx, pvUser, pControl, cbControl);
    }
}

/* static */ DECLCALLBACK(void) ConsoleVRDPServer::VRDECallbackVideoInFrame(void *pvCallback,
                                                                            int vrcRequest,
                                                                            void *pDeviceCtx,
                                                                            const VRDEVIDEOINPAYLOADHDR *pFrame,
                                                                            uint32_t cbFrame)
{
    ConsoleVRDPServer *pThis = static_cast<ConsoleVRDPServer*>(pvCallback);
    if (pThis->mEmWebcam)
    {
        pThis->mEmWebcam->EmWebcamCbFrame(vrcRequest, pDeviceCtx, pFrame, cbFrame);
    }
}

int ConsoleVRDPServer::VideoInDeviceAttach(const VRDEVIDEOINDEVICEHANDLE *pDeviceHandle, void *pvDeviceCtx)
{
    int vrc;

    if (mhServer && mpEntryPoints && m_interfaceVideoIn.VRDEVideoInDeviceAttach)
    {
        vrc = m_interfaceVideoIn.VRDEVideoInDeviceAttach(mhServer, pDeviceHandle, pvDeviceCtx);
    }
    else
    {
        vrc = VERR_NOT_SUPPORTED;
    }

    return vrc;
}

int ConsoleVRDPServer::VideoInDeviceDetach(const VRDEVIDEOINDEVICEHANDLE *pDeviceHandle)
{
    int vrc;

    if (mhServer && mpEntryPoints && m_interfaceVideoIn.VRDEVideoInDeviceDetach)
    {
        vrc = m_interfaceVideoIn.VRDEVideoInDeviceDetach(mhServer, pDeviceHandle);
    }
    else
    {
        vrc = VERR_NOT_SUPPORTED;
    }

    return vrc;
}

int ConsoleVRDPServer::VideoInGetDeviceDesc(void *pvUser, const VRDEVIDEOINDEVICEHANDLE *pDeviceHandle)
{
    int vrc;

    if (mhServer && mpEntryPoints && m_interfaceVideoIn.VRDEVideoInGetDeviceDesc)
    {
        vrc = m_interfaceVideoIn.VRDEVideoInGetDeviceDesc(mhServer, pvUser, pDeviceHandle);
    }
    else
    {
        vrc = VERR_NOT_SUPPORTED;
    }

    return vrc;
}

int ConsoleVRDPServer::VideoInControl(void *pvUser, const VRDEVIDEOINDEVICEHANDLE *pDeviceHandle,
                                      const VRDEVIDEOINCTRLHDR *pReq, uint32_t cbReq)
{
    int vrc;

    if (mhServer && mpEntryPoints && m_interfaceVideoIn.VRDEVideoInControl)
    {
        vrc = m_interfaceVideoIn.VRDEVideoInControl(mhServer, pvUser, pDeviceHandle, pReq, cbReq);
    }
    else
    {
        vrc = VERR_NOT_SUPPORTED;
    }

    return vrc;
}


/* static */ DECLCALLBACK(void) ConsoleVRDPServer::VRDECallbackInputSetup(void *pvCallback,
                                                                          int vrcRequest,
                                                                          uint32_t u32Method,
                                                                          const void *pvResult,
                                                                          uint32_t cbResult)
{
    NOREF(pvCallback);
    NOREF(vrcRequest);
    NOREF(u32Method);
    NOREF(pvResult);
    NOREF(cbResult);
}

/* static */ DECLCALLBACK(void) ConsoleVRDPServer::VRDECallbackInputEvent(void *pvCallback,
                                                                          uint32_t u32Method,
                                                                          const void *pvEvent,
                                                                          uint32_t cbEvent)
{
    ConsoleVRDPServer *pThis = static_cast<ConsoleVRDPServer*>(pvCallback);

    if (u32Method == VRDE_INPUT_METHOD_TOUCH)
    {
        if (cbEvent >= sizeof(VRDEINPUTHEADER))
        {
            VRDEINPUTHEADER *pHeader = (VRDEINPUTHEADER *)pvEvent;

            if (pHeader->u16EventId == VRDEINPUT_EVENTID_TOUCH)
            {
                IMouse *pMouse = pThis->mConsole->i_getMouse();

                VRDEINPUT_TOUCH_EVENT_PDU *p = (VRDEINPUT_TOUCH_EVENT_PDU *)pHeader;

                uint16_t iFrame;
                for (iFrame = 0; iFrame < p->u16FrameCount; iFrame++)
                {
                    VRDEINPUT_TOUCH_FRAME *pFrame = &p->aFrames[iFrame];

                    com::SafeArray<LONG64> aContacts(pFrame->u16ContactCount);

                    uint16_t iContact;
                    for (iContact = 0; iContact < pFrame->u16ContactCount; iContact++)
                    {
                        VRDEINPUT_CONTACT_DATA *pContact = &pFrame->aContacts[iContact];

                        int16_t x = (int16_t)(pContact->i32X + 1);
                        int16_t y = (int16_t)(pContact->i32Y + 1);
                        uint8_t contactId = pContact->u8ContactId;
                        uint8_t contactState = TouchContactState_None;

                        if (pContact->u32ContactFlags & VRDEINPUT_CONTACT_FLAG_INRANGE)
                        {
                            contactState |= TouchContactState_InRange;
                        }
                        if (pContact->u32ContactFlags & VRDEINPUT_CONTACT_FLAG_INCONTACT)
                        {
                            contactState |= TouchContactState_InContact;
                        }

                        aContacts[iContact] = RT_MAKE_U64_FROM_U16((uint16_t)x,
                                                                   (uint16_t)y,
                                                                   RT_MAKE_U16(contactId, contactState),
                                                                   0);
                    }

                    if (pFrame->u64FrameOffset == 0)
                    {
                        pThis->mu64TouchInputTimestampMCS = 0;
                    }
                    else
                    {
                        pThis->mu64TouchInputTimestampMCS += pFrame->u64FrameOffset;
                    }

                    pMouse->PutEventMultiTouch(pFrame->u16ContactCount,
                                               ComSafeArrayAsInParam(aContacts),
                                               true /* isTouchScreen */,
                                               (ULONG)(pThis->mu64TouchInputTimestampMCS / 1000)); /* Micro->milliseconds. */
                }
            }
            else if (pHeader->u16EventId == VRDEINPUT_EVENTID_DISMISS_HOVERING_CONTACT)
            {
                /** @todo */
            }
            else
            {
                AssertMsgFailed(("EventId %d\n", pHeader->u16EventId));
            }
        }
    }
}


void ConsoleVRDPServer::EnableConnections(void)
{
    if (mpEntryPoints && mhServer)
    {
        mpEntryPoints->VRDEEnableConnections(mhServer, true);

        /* Setup the generic TSMF channel. */
        setupTSMF();
    }
}

void ConsoleVRDPServer::DisconnectClient(uint32_t u32ClientId, bool fReconnect)
{
    if (mpEntryPoints && mhServer)
    {
        mpEntryPoints->VRDEDisconnect(mhServer, u32ClientId, fReconnect);
    }
}

int ConsoleVRDPServer::MousePointer(BOOL alpha,
                                    ULONG xHot,
                                    ULONG yHot,
                                    ULONG width,
                                    ULONG height,
                                    const uint8_t *pu8Shape)
{
    int vrc = VINF_SUCCESS;

    if (mhServer && mpEntryPoints && m_interfaceMousePtr.VRDEMousePtr)
    {
        size_t cbMask = (((width + 7) / 8) * height + 3) & ~3;
        size_t cbData = width * height * 4;

        size_t cbDstMask = alpha? 0: cbMask;

        size_t cbPointer = sizeof(VRDEMOUSEPTRDATA) + cbDstMask + cbData;
        uint8_t *pu8Pointer = (uint8_t *)RTMemAlloc(cbPointer);
        if (pu8Pointer != NULL)
        {
            VRDEMOUSEPTRDATA *pPointer = (VRDEMOUSEPTRDATA *)pu8Pointer;

            pPointer->u16HotX    = (uint16_t)xHot;
            pPointer->u16HotY    = (uint16_t)yHot;
            pPointer->u16Width   = (uint16_t)width;
            pPointer->u16Height  = (uint16_t)height;
            pPointer->u16MaskLen = (uint16_t)cbDstMask;
            pPointer->u32DataLen = (uint32_t)cbData;

            /* AND mask. */
            uint8_t *pu8Mask = pu8Pointer + sizeof(VRDEMOUSEPTRDATA);
            if (cbDstMask)
            {
                memcpy(pu8Mask, pu8Shape, cbDstMask);
            }

            /* XOR mask */
            uint8_t *pu8Data = pu8Mask + pPointer->u16MaskLen;
            memcpy(pu8Data, pu8Shape + cbMask, cbData);

            m_interfaceMousePtr.VRDEMousePtr(mhServer, pPointer);

            RTMemFree(pu8Pointer);
        }
        else
        {
            vrc = VERR_NO_MEMORY;
        }
    }
    else
    {
        vrc = VERR_NOT_SUPPORTED;
    }

    return vrc;
}

void ConsoleVRDPServer::MousePointerUpdate(const VRDECOLORPOINTER *pPointer)
{
    if (mpEntryPoints && mhServer)
    {
        mpEntryPoints->VRDEColorPointer(mhServer, pPointer);
    }
}

void ConsoleVRDPServer::MousePointerHide(void)
{
    if (mpEntryPoints && mhServer)
    {
        mpEntryPoints->VRDEHidePointer(mhServer);
    }
}

void ConsoleVRDPServer::Stop(void)
{
    AssertPtr(this); /** @todo r=bird: there are(/was) some odd cases where this buster was invalid on
                      * linux. Just remove this when it's 100% sure that problem has been fixed. */

#ifdef VBOX_WITH_USB
    remoteUSBThreadStop();
#endif /* VBOX_WITH_USB */

    if (mhServer)
    {
        HVRDESERVER hServer = mhServer;

        /* Reset the handle to avoid further calls to the server. */
        mhServer = 0;

        /* Workaround for VM process hangs on termination.
         *
         * Make sure that the server is not currently processing a resize.
         * mhServer 0 will not allow to enter the server again.
         * Wait until any current resize returns from the server.
         */
        if (mcInResize)
        {
            LogRel(("VRDP: waiting for resize %d\n", mcInResize));

            int i = 0;
            while (mcInResize && ++i < 100)
            {
                RTThreadSleep(10);
            }
        }

        if (mpEntryPoints && hServer)
        {
            mpEntryPoints->VRDEDestroy(hServer);
        }
    }

#ifndef VBOX_WITH_VRDEAUTH_IN_VBOXSVC
    AuthLibUnload(&mAuthLibCtx);
#endif
}

/* Worker thread for Remote USB. The thread polls the clients for
 * the list of attached USB devices.
 * The thread is also responsible for attaching/detaching devices
 * to/from the VM.
 *
 * It is expected that attaching/detaching is not a frequent operation.
 *
 * The thread is always running when the VRDP server is active.
 *
 * The thread scans backends and requests the device list every 2 seconds.
 *
 * When device list is available, the thread calls the Console to process it.
 *
 */
#define VRDP_DEVICE_LIST_PERIOD_MS (2000)

#ifdef VBOX_WITH_USB
static DECLCALLBACK(int) threadRemoteUSB(RTTHREAD self, void *pvUser)
{
    ConsoleVRDPServer *pOwner = (ConsoleVRDPServer *)pvUser;

    LogFlow(("Console::threadRemoteUSB: start. owner = %p.\n", pOwner));

    pOwner->notifyRemoteUSBThreadRunning(self);

    while (pOwner->isRemoteUSBThreadRunning())
    {
        RemoteUSBBackend *pRemoteUSBBackend = NULL;

        while ((pRemoteUSBBackend = pOwner->usbBackendGetNext(pRemoteUSBBackend)) != NULL)
        {
            pRemoteUSBBackend->PollRemoteDevices();
        }

        pOwner->waitRemoteUSBThreadEvent(VRDP_DEVICE_LIST_PERIOD_MS);

        LogFlow(("Console::threadRemoteUSB: iteration. owner = %p.\n", pOwner));
    }

    return VINF_SUCCESS;
}

void ConsoleVRDPServer::notifyRemoteUSBThreadRunning(RTTHREAD thread)
{
    mUSBBackends.thread = thread;
    mUSBBackends.fThreadRunning = true;
    int vrc = RTThreadUserSignal(thread);
    AssertRC(vrc);
}

bool ConsoleVRDPServer::isRemoteUSBThreadRunning(void)
{
    return mUSBBackends.fThreadRunning;
}

void ConsoleVRDPServer::waitRemoteUSBThreadEvent(RTMSINTERVAL cMillies)
{
    int vrc = RTSemEventWait(mUSBBackends.event, cMillies);
    Assert(RT_SUCCESS(vrc) || vrc == VERR_TIMEOUT);
    NOREF(vrc);
}

void ConsoleVRDPServer::remoteUSBThreadStart(void)
{
    int vrc = RTSemEventCreate(&mUSBBackends.event);

    if (RT_FAILURE(vrc))
    {
        AssertFailed();
        mUSBBackends.event = 0;
    }

    if (RT_SUCCESS(vrc))
    {
        vrc = RTThreadCreate(&mUSBBackends.thread, threadRemoteUSB, this, 65536,
                             RTTHREADTYPE_VRDP_IO, RTTHREADFLAGS_WAITABLE, "remote usb");
    }

    if (RT_FAILURE(vrc))
    {
        LogRel(("Warning: could not start the remote USB thread, vrc = %Rrc!!!\n", vrc));
        mUSBBackends.thread = NIL_RTTHREAD;
    }
    else
    {
        /* Wait until the thread is ready. */
        vrc = RTThreadUserWait(mUSBBackends.thread, 60000);
        AssertRC(vrc);
        Assert (mUSBBackends.fThreadRunning || RT_FAILURE(vrc));
    }
}

void ConsoleVRDPServer::remoteUSBThreadStop(void)
{
    mUSBBackends.fThreadRunning = false;

    if (mUSBBackends.thread != NIL_RTTHREAD)
    {
        Assert (mUSBBackends.event != 0);

        RTSemEventSignal(mUSBBackends.event);

        int vrc = RTThreadWait(mUSBBackends.thread, 60000, NULL);
        AssertRC(vrc);

        mUSBBackends.thread = NIL_RTTHREAD;
    }

    if (mUSBBackends.event)
    {
        RTSemEventDestroy(mUSBBackends.event);
        mUSBBackends.event = 0;
    }
}
#endif /* VBOX_WITH_USB */

AuthResult ConsoleVRDPServer::Authenticate(const Guid &uuid, AuthGuestJudgement guestJudgement,
                                           const char *pszUser, const char *pszPassword, const char *pszDomain,
                                           uint32_t u32ClientId)
{
    LogFlowFunc(("uuid = %RTuuid, guestJudgement = %d, pszUser = %s, pszPassword = %s, pszDomain = %s, u32ClientId = %d\n",
                 uuid.raw(), guestJudgement, pszUser, pszPassword, pszDomain, u32ClientId));

    AuthResult result = AuthResultAccessDenied;

#ifdef VBOX_WITH_VRDEAUTH_IN_VBOXSVC
    try
    {
        /* Init auth parameters. Order is important. */
        SafeArray<BSTR> authParams;
        Bstr("VRDEAUTH"          ).detachTo(authParams.appendedRaw());
        Bstr(uuid.toUtf16()      ).detachTo(authParams.appendedRaw());
        BstrFmt("%u", guestJudgement).detachTo(authParams.appendedRaw());
        Bstr(pszUser             ).detachTo(authParams.appendedRaw());
        Bstr(pszPassword         ).detachTo(authParams.appendedRaw());
        Bstr(pszDomain           ).detachTo(authParams.appendedRaw());
        BstrFmt("%u", u32ClientId).detachTo(authParams.appendedRaw());

        Bstr authResult;
        HRESULT hr = mConsole->mControl->AuthenticateExternal(ComSafeArrayAsInParam(authParams),
                                                              authResult.asOutParam());
        LogFlowFunc(("%Rhrc [%ls]\n", hr, authResult.raw()));

        size_t cbPassword = RTUtf16Len((PRTUTF16)authParams[4]) * sizeof(RTUTF16);
        if (cbPassword)
            RTMemWipeThoroughly(authParams[4], cbPassword, 10 /* cPasses */);

        if (SUCCEEDED(hr) && authResult == "granted")
            result = AuthResultAccessGranted;
    }
    catch (std::bad_alloc &)
    {
    }
#else
    /*
     * Called only from VRDP input thread. So thread safety is not required.
     */

    if (!mAuthLibCtx.hAuthLibrary)
    {
        /* Load the external authentication library. */
        Bstr authLibrary;
        mConsole->i_getVRDEServer()->COMGETTER(AuthLibrary)(authLibrary.asOutParam());

        Utf8Str filename = authLibrary;

        int vrc = AuthLibLoad(&mAuthLibCtx, filename.c_str());
        if (RT_FAILURE(vrc))
        {
            mConsole->setErrorBoth(E_FAIL, vrc, tr("Could not load the external authentication library '%s' (%Rrc)"),
                                   filename.c_str(), vrc);
            return AuthResultAccessDenied;
        }
    }

    result = AuthLibAuthenticate(&mAuthLibCtx,
                                 uuid.raw(), guestJudgement,
                                 pszUser, pszPassword, pszDomain,
                                 u32ClientId);
#endif /* !VBOX_WITH_VRDEAUTH_IN_VBOXSVC */

    switch (result)
    {
        case AuthResultAccessDenied:
            LogRel(("AUTH: external authentication module returned 'access denied'\n"));
            break;
        case AuthResultAccessGranted:
            LogRel(("AUTH: external authentication module returned 'access granted'\n"));
            break;
        case AuthResultDelegateToGuest:
            LogRel(("AUTH: external authentication module returned 'delegate request to guest'\n"));
            break;
        default:
            LogRel(("AUTH: external authentication module returned incorrect return code %d\n", result));
            result = AuthResultAccessDenied;
    }

    LogFlowFunc(("result = %d\n", result));

    return result;
}

void ConsoleVRDPServer::AuthDisconnect(const Guid &uuid, uint32_t u32ClientId)
{
    LogFlow(("ConsoleVRDPServer::AuthDisconnect: uuid = %RTuuid, u32ClientId = %d\n",
             uuid.raw(), u32ClientId));

#ifdef VBOX_WITH_VRDEAUTH_IN_VBOXSVC
    try
    {
        /* Init auth parameters. Order is important. */
        SafeArray<BSTR> authParams;
        Bstr("VRDEAUTHDISCONNECT").detachTo(authParams.appendedRaw());
        Bstr(uuid.toUtf16()      ).detachTo(authParams.appendedRaw());
        BstrFmt("%u", u32ClientId).detachTo(authParams.appendedRaw());

        Bstr authResult;
        HRESULT hrc = mConsole->mControl->AuthenticateExternal(ComSafeArrayAsInParam(authParams),
                                                               authResult.asOutParam());
        LogFlowFunc(("%Rhrc [%ls]\n", hrc, authResult.raw())); NOREF(hrc);
    }
    catch (std::bad_alloc &)
    {
    }
#else
    AuthLibDisconnect(&mAuthLibCtx, uuid.raw(), u32ClientId);
#endif /* !VBOX_WITH_VRDEAUTH_IN_VBOXSVC */
}

int ConsoleVRDPServer::lockConsoleVRDPServer(void)
{
    int vrc = RTCritSectEnter(&mCritSect);
    AssertRC(vrc);
    return vrc;
}

void ConsoleVRDPServer::unlockConsoleVRDPServer(void)
{
    RTCritSectLeave(&mCritSect);
}

DECLCALLBACK(int) ConsoleVRDPServer::ClipboardCallback(void *pvCallback,
                                                       uint32_t u32ClientId,
                                                       uint32_t u32Function,
                                                       uint32_t u32Format,
                                                       const void *pvData,
                                                       uint32_t cbData)
{
    LogFlowFunc(("pvCallback = %p, u32ClientId = %d, u32Function = %d, u32Format = 0x%08X, pvData = %p, cbData = %d\n",
                 pvCallback, u32ClientId, u32Function, u32Format, pvData, cbData));

    int vrc = VINF_SUCCESS;

    ConsoleVRDPServer *pServer = static_cast <ConsoleVRDPServer *>(pvCallback);

    RT_NOREF(u32ClientId);

    switch (u32Function)
    {
        case VRDE_CLIPBOARD_FUNCTION_FORMAT_ANNOUNCE:
        {
            if (pServer->mpfnClipboardCallback)
            {
                vrc = pServer->mpfnClipboardCallback(VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_HOST,
                                                     u32Format,
                                                     (void *)pvData,
                                                     cbData);
            }
        } break;

        case VRDE_CLIPBOARD_FUNCTION_DATA_READ:
        {
            if (pServer->mpfnClipboardCallback)
            {
                vrc = pServer->mpfnClipboardCallback(VBOX_CLIPBOARD_EXT_FN_DATA_READ,
                                                     u32Format,
                                                     (void *)pvData,
                                                     cbData);
            }
        } break;

        default:
        {
            vrc = VERR_NOT_SUPPORTED;
        } break;
    }

    return vrc;
}

/**
 * Service extension callback called by GuestShCl::hgcmDispatcher().
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_SUPPORTED if the extension didn't handle the requested function. This will invoke the regular backend then.
 * @param   pvExtension         Pointer to service extension.
 * @param   u32Function         Callback HGCM message ID.
 * @param   pvParms             Pointer to optional data provided for a particular message. Optional.
 * @param   cbParms             Size (in bytes) of \a pvParms.
 */
/* static */
DECLCALLBACK(int) ConsoleVRDPServer::ClipboardServiceExtension(void *pvExtension,
                                                               uint32_t u32Function, void *pvParms, uint32_t cbParms)
{
    RT_NOREF(cbParms);
    LogFlowFunc(("pvExtension = %p, u32Function = %d, pvParms = %p, cbParms = %d\n",
                 pvExtension, u32Function, pvParms, cbParms));

    int vrc = VINF_SUCCESS;

    ConsoleVRDPServer *pServer = static_cast <ConsoleVRDPServer *>(pvExtension);
    AssertPtrReturn(pServer, VERR_INVALID_POINTER);

    SHCLEXTPARMS *pParms = (SHCLEXTPARMS *)pvParms;
    AssertPtrReturn(pParms, VERR_INVALID_POINTER);

    switch (u32Function)
    {
        case VBOX_CLIPBOARD_EXT_FN_SET_CALLBACK:
        {
            pServer->mpfnClipboardCallback = pParms->u.SetCallback.pfnCallback;
        } break;

        case VBOX_CLIPBOARD_EXT_FN_FORMAT_REPORT_TO_HOST:
        {
            /* The guest announces clipboard formats to the host. This must be delivered to all clients. */
            if (mpEntryPoints && pServer->mhServer)
            {
                mpEntryPoints->VRDEClipboard(pServer->mhServer,
                                             VRDE_CLIPBOARD_FUNCTION_FORMAT_ANNOUNCE,
                                             pParms->u.ReportFormats.uFormats,
                                             NULL,
                                             0,
                                             NULL);
            }
        } break;

        case VBOX_CLIPBOARD_EXT_FN_DATA_READ:
        {
            /* The clipboard service expects that the pvData buffer will be filled
             * with clipboard data. The server returns the data from the client that
             * announced the requested format most recently.
             */
            if (mpEntryPoints && pServer->mhServer)
            {
                mpEntryPoints->VRDEClipboard(pServer->mhServer,
                                             VRDE_CLIPBOARD_FUNCTION_DATA_READ,
                                             pParms->u.ReadWriteData.uFormat,
                                             pParms->u.ReadWriteData.pvData,
                                             pParms->u.ReadWriteData.cbData,
                                             &pParms->u.ReadWriteData.cbData);
            }
        } break;

        case VBOX_CLIPBOARD_EXT_FN_DATA_WRITE:
        {
            if (mpEntryPoints && pServer->mhServer)
            {
                mpEntryPoints->VRDEClipboard(pServer->mhServer,
                                             VRDE_CLIPBOARD_FUNCTION_DATA_WRITE,
                                             pParms->u.ReadWriteData.uFormat,
                                             pParms->u.ReadWriteData.pvData,
                                             pParms->u.ReadWriteData.cbData,
                                             NULL);
            }
        } break;

        default:
            vrc = VERR_NOT_SUPPORTED;
    }

    return vrc;
}

void ConsoleVRDPServer::ClipboardCreate(uint32_t u32ClientId)
{
    RT_NOREF(u32ClientId);

    int vrc = lockConsoleVRDPServer();
    if (RT_SUCCESS(vrc))
    {
        if (mcClipboardRefs == 0)
        {
#ifdef VBOX_WITH_SHARED_CLIPBOARD
            vrc = GuestShClInst()->RegisterServiceExtension(ClipboardServiceExtension, this /* pvExtension */);
            AssertRC(vrc);
#endif /* VBOX_WITH_SHARED_CLIPBOARD */
        }

        mcClipboardRefs++;
        unlockConsoleVRDPServer();
    }
}

void ConsoleVRDPServer::ClipboardDelete(uint32_t u32ClientId)
{
    RT_NOREF(u32ClientId);

    int vrc = lockConsoleVRDPServer();
    if (RT_SUCCESS(vrc))
    {
        Assert(mcClipboardRefs);
        if (mcClipboardRefs > 0)
        {
            mcClipboardRefs--;

            if (mcClipboardRefs == 0)
            {
#ifdef VBOX_WITH_SHARED_CLIPBOARD
                GuestShClInst()->UnregisterServiceExtension(ClipboardServiceExtension);
#endif /* VBOX_WITH_SHARED_CLIPBOARD */
            }
        }

        unlockConsoleVRDPServer();
    }
}

/* That is called on INPUT thread of the VRDP server.
 * The ConsoleVRDPServer keeps a list of created backend instances.
 */
void ConsoleVRDPServer::USBBackendCreate(uint32_t u32ClientId, void **ppvIntercept)
{
#ifdef VBOX_WITH_USB
    LogFlow(("ConsoleVRDPServer::USBBackendCreate: u32ClientId = %d\n", u32ClientId));

    /* Create a new instance of the USB backend for the new client. */
    RemoteUSBBackend *pRemoteUSBBackend = new RemoteUSBBackend(mConsole, this, u32ClientId);

    if (pRemoteUSBBackend)
    {
        pRemoteUSBBackend->AddRef(); /* 'Release' called in USBBackendDelete. */

        /* Append the new instance in the list. */
        int vrc = lockConsoleVRDPServer();

        if (RT_SUCCESS(vrc))
        {
            pRemoteUSBBackend->pNext = mUSBBackends.pHead;
            if (mUSBBackends.pHead)
            {
                mUSBBackends.pHead->pPrev = pRemoteUSBBackend;
            }
            else
            {
                mUSBBackends.pTail = pRemoteUSBBackend;
            }

            mUSBBackends.pHead = pRemoteUSBBackend;

            unlockConsoleVRDPServer();

            if (ppvIntercept)
            {
                *ppvIntercept = pRemoteUSBBackend;
            }
        }

        if (RT_FAILURE(vrc))
        {
            pRemoteUSBBackend->Release();
        }
    }
#else
    RT_NOREF(u32ClientId, ppvIntercept);
#endif /* VBOX_WITH_USB */
}

void ConsoleVRDPServer::USBBackendDelete(uint32_t u32ClientId)
{
#ifdef VBOX_WITH_USB
    LogFlow(("ConsoleVRDPServer::USBBackendDelete: u32ClientId = %d\n", u32ClientId));

    RemoteUSBBackend *pRemoteUSBBackend = NULL;

    /* Find the instance. */
    int vrc = lockConsoleVRDPServer();

    if (RT_SUCCESS(vrc))
    {
        pRemoteUSBBackend = usbBackendFind(u32ClientId);

        if (pRemoteUSBBackend)
        {
            /* Notify that it will be deleted. */
            pRemoteUSBBackend->NotifyDelete();
        }

        unlockConsoleVRDPServer();
    }

    if (pRemoteUSBBackend)
    {
        /* Here the instance has been excluded from the list and can be dereferenced. */
        pRemoteUSBBackend->Release();
    }
#else
    RT_NOREF(u32ClientId);
#endif
}

void *ConsoleVRDPServer::USBBackendRequestPointer(uint32_t u32ClientId, const Guid *pGuid)
{
#ifdef VBOX_WITH_USB
    RemoteUSBBackend *pRemoteUSBBackend = NULL;

    /* Find the instance. */
    int vrc = lockConsoleVRDPServer();

    if (RT_SUCCESS(vrc))
    {
        pRemoteUSBBackend = usbBackendFind(u32ClientId);

        if (pRemoteUSBBackend)
        {
            /* Inform the backend instance that it is referenced by the Guid. */
            bool fAdded = pRemoteUSBBackend->addUUID(pGuid);

            if (fAdded)
            {
                /* Reference the instance because its pointer is being taken. */
                pRemoteUSBBackend->AddRef(); /* 'Release' is called in USBBackendReleasePointer. */
            }
            else
            {
                pRemoteUSBBackend = NULL;
            }
        }

        unlockConsoleVRDPServer();
    }

    if (pRemoteUSBBackend)
    {
        return pRemoteUSBBackend->GetBackendCallbackPointer();
    }
#else
    RT_NOREF(u32ClientId, pGuid);
#endif
    return NULL;
}

void ConsoleVRDPServer::USBBackendReleasePointer(const Guid *pGuid)
{
#ifdef VBOX_WITH_USB
    RemoteUSBBackend *pRemoteUSBBackend = NULL;

    /* Find the instance. */
    int vrc = lockConsoleVRDPServer();

    if (RT_SUCCESS(vrc))
    {
        pRemoteUSBBackend = usbBackendFindByUUID(pGuid);

        if (pRemoteUSBBackend)
        {
            pRemoteUSBBackend->removeUUID(pGuid);
        }

        unlockConsoleVRDPServer();

        if (pRemoteUSBBackend)
        {
            pRemoteUSBBackend->Release();
        }
    }
#else
    RT_NOREF(pGuid);
#endif
}

RemoteUSBBackend *ConsoleVRDPServer::usbBackendGetNext(RemoteUSBBackend *pRemoteUSBBackend)
{
    LogFlow(("ConsoleVRDPServer::usbBackendGetNext: pBackend = %p\n", pRemoteUSBBackend));

    RemoteUSBBackend *pNextRemoteUSBBackend = NULL;
#ifdef VBOX_WITH_USB

    int vrc = lockConsoleVRDPServer();

    if (RT_SUCCESS(vrc))
    {
        if (pRemoteUSBBackend == NULL)
        {
            /* The first backend in the list is requested. */
            pNextRemoteUSBBackend = mUSBBackends.pHead;
        }
        else
        {
            /* Get pointer to the next backend. */
            pNextRemoteUSBBackend = (RemoteUSBBackend *)pRemoteUSBBackend->pNext;
        }

        if (pNextRemoteUSBBackend)
        {
            pNextRemoteUSBBackend->AddRef();
        }

        unlockConsoleVRDPServer();

        if (pRemoteUSBBackend)
        {
            pRemoteUSBBackend->Release();
        }
    }
#endif

    return pNextRemoteUSBBackend;
}

#ifdef VBOX_WITH_USB
/* Internal method. Called under the ConsoleVRDPServerLock. */
RemoteUSBBackend *ConsoleVRDPServer::usbBackendFind(uint32_t u32ClientId)
{
    RemoteUSBBackend *pRemoteUSBBackend = mUSBBackends.pHead;

    while (pRemoteUSBBackend)
    {
        if (pRemoteUSBBackend->ClientId() == u32ClientId)
        {
            break;
        }

        pRemoteUSBBackend = (RemoteUSBBackend *)pRemoteUSBBackend->pNext;
    }

    return pRemoteUSBBackend;
}

/* Internal method. Called under the ConsoleVRDPServerLock. */
RemoteUSBBackend *ConsoleVRDPServer::usbBackendFindByUUID(const Guid *pGuid)
{
    RemoteUSBBackend *pRemoteUSBBackend = mUSBBackends.pHead;

    while (pRemoteUSBBackend)
    {
        if (pRemoteUSBBackend->findUUID(pGuid))
        {
            break;
        }

        pRemoteUSBBackend = (RemoteUSBBackend *)pRemoteUSBBackend->pNext;
    }

    return pRemoteUSBBackend;
}
#endif

/* Internal method. Called by the backend destructor. */
void ConsoleVRDPServer::usbBackendRemoveFromList(RemoteUSBBackend *pRemoteUSBBackend)
{
#ifdef VBOX_WITH_USB
    int vrc = lockConsoleVRDPServer();
    AssertRC(vrc);

    /* Exclude the found instance from the list. */
    if (pRemoteUSBBackend->pNext)
    {
        pRemoteUSBBackend->pNext->pPrev = pRemoteUSBBackend->pPrev;
    }
    else
    {
        mUSBBackends.pTail = (RemoteUSBBackend *)pRemoteUSBBackend->pPrev;
    }

    if (pRemoteUSBBackend->pPrev)
    {
        pRemoteUSBBackend->pPrev->pNext = pRemoteUSBBackend->pNext;
    }
    else
    {
        mUSBBackends.pHead = (RemoteUSBBackend *)pRemoteUSBBackend->pNext;
    }

    pRemoteUSBBackend->pNext = pRemoteUSBBackend->pPrev = NULL;

    unlockConsoleVRDPServer();
#else
    RT_NOREF(pRemoteUSBBackend);
#endif
}


void ConsoleVRDPServer::SendUpdate(unsigned uScreenId, void *pvUpdate, uint32_t cbUpdate) const
{
    if (mpEntryPoints && mhServer)
    {
        mpEntryPoints->VRDEUpdate(mhServer, uScreenId, pvUpdate, cbUpdate);
    }
}

void ConsoleVRDPServer::SendResize(void)
{
    if (mpEntryPoints && mhServer)
    {
        ++mcInResize;
        mpEntryPoints->VRDEResize(mhServer);
        --mcInResize;
    }
}

void ConsoleVRDPServer::SendUpdateBitmap(unsigned uScreenId, uint32_t x, uint32_t y, uint32_t w, uint32_t h) const
{
    VRDEORDERHDR update;
    update.x = (uint16_t)x;
    update.y = (uint16_t)y;
    update.w = (uint16_t)w;
    update.h = (uint16_t)h;
    if (mpEntryPoints && mhServer)
    {
        mpEntryPoints->VRDEUpdate(mhServer, uScreenId, &update, sizeof(update));
    }
}

void ConsoleVRDPServer::SendAudioSamples(void const *pvSamples, uint32_t cSamples, VRDEAUDIOFORMAT format) const
{
    if (mpEntryPoints && mhServer)
    {
        mpEntryPoints->VRDEAudioSamples(mhServer, pvSamples, cSamples, format);
    }
}

void ConsoleVRDPServer::SendAudioVolume(uint16_t left, uint16_t right) const
{
    if (mpEntryPoints && mhServer)
    {
        mpEntryPoints->VRDEAudioVolume(mhServer, left, right);
    }
}

void ConsoleVRDPServer::SendUSBRequest(uint32_t u32ClientId, void *pvParms, uint32_t cbParms) const
{
    if (mpEntryPoints && mhServer)
    {
        mpEntryPoints->VRDEUSBRequest(mhServer, u32ClientId, pvParms, cbParms);
    }
}

int ConsoleVRDPServer::SendAudioInputBegin(void **ppvUserCtx,
                                           void *pvContext,
                                           uint32_t cSamples,
                                           uint32_t iSampleHz,
                                           uint32_t cChannels,
                                           uint32_t cBits)
{
    if (   mhServer
        && mpEntryPoints && mpEntryPoints->VRDEAudioInOpen)
    {
        uint32_t u32ClientId = ASMAtomicReadU32(&mu32AudioInputClientId);
        if (u32ClientId != 0) /* 0 would mean broadcast to all clients. */
        {
            VRDEAUDIOFORMAT audioFormat = VRDE_AUDIO_FMT_MAKE(iSampleHz, cChannels, cBits, 0);
            mpEntryPoints->VRDEAudioInOpen(mhServer,
                                           pvContext,
                                           u32ClientId,
                                           audioFormat,
                                           cSamples);
            if (ppvUserCtx)
                *ppvUserCtx = NULL; /* This is the ConsoleVRDPServer context.
                                     * Currently not used because only one client is allowed to
                                     * do audio input and the client ID is saved by the ConsoleVRDPServer.
                                     */
            return VINF_SUCCESS;
        }
    }

    /*
     * Not supported or no client connected.
     */
    return VERR_NOT_SUPPORTED;
}

void ConsoleVRDPServer::SendAudioInputEnd(void *pvUserCtx)
{
    RT_NOREF(pvUserCtx);
    if (mpEntryPoints && mhServer && mpEntryPoints->VRDEAudioInClose)
    {
        uint32_t u32ClientId = ASMAtomicReadU32(&mu32AudioInputClientId);
        if (u32ClientId != 0) /* 0 would mean broadcast to all clients. */
        {
            mpEntryPoints->VRDEAudioInClose(mhServer, u32ClientId);
        }
    }
}

void ConsoleVRDPServer::QueryInfo(uint32_t index, void *pvBuffer, uint32_t cbBuffer, uint32_t *pcbOut) const
{
    if (index == VRDE_QI_PORT)
    {
        uint32_t cbOut = sizeof(int32_t);

        if (cbBuffer >= cbOut)
        {
            *pcbOut = cbOut;
            *(int32_t *)pvBuffer = (int32_t)mVRDPBindPort;
        }
    }
    else if (mpEntryPoints && mhServer)
    {
        mpEntryPoints->VRDEQueryInfo(mhServer, index, pvBuffer, cbBuffer, pcbOut);
    }
}

/* static */ int ConsoleVRDPServer::loadVRDPLibrary(const char *pszLibraryName)
{
    int vrc = VINF_SUCCESS;

    if (mVRDPLibrary == NIL_RTLDRMOD)
    {
        RTERRINFOSTATIC ErrInfo;
        RTErrInfoInitStatic(&ErrInfo);

        if (RTPathHavePath(pszLibraryName))
            vrc = SUPR3HardenedLdrLoadPlugIn(pszLibraryName, &mVRDPLibrary, &ErrInfo.Core);
        else
            vrc = SUPR3HardenedLdrLoadAppPriv(pszLibraryName, &mVRDPLibrary, RTLDRLOAD_FLAGS_LOCAL, &ErrInfo.Core);
        if (RT_SUCCESS(vrc))
        {
            struct SymbolEntry
            {
                const char *name;
                void **ppfn;
            };

            #define DEFSYMENTRY(a) { #a, (void**)&mpfn##a }

            static const struct SymbolEntry s_aSymbols[] =
            {
                DEFSYMENTRY(VRDECreateServer)
            };

            #undef DEFSYMENTRY

            for (unsigned i = 0; i < RT_ELEMENTS(s_aSymbols); i++)
            {
                vrc = RTLdrGetSymbol(mVRDPLibrary, s_aSymbols[i].name, s_aSymbols[i].ppfn);

                if (RT_FAILURE(vrc))
                {
                    LogRel(("VRDE: Error resolving symbol '%s', vrc %Rrc.\n", s_aSymbols[i].name, vrc));
                    break;
                }
            }
        }
        else
        {
            if (RTErrInfoIsSet(&ErrInfo.Core))
                LogRel(("VRDE: Error loading the library '%s': %s (%Rrc)\n", pszLibraryName, ErrInfo.Core.pszMsg, vrc));
            else
                LogRel(("VRDE: Error loading the library '%s' vrc = %Rrc.\n", pszLibraryName, vrc));

            mVRDPLibrary = NIL_RTLDRMOD;
        }
    }

    if (RT_FAILURE(vrc))
    {
        if (mVRDPLibrary != NIL_RTLDRMOD)
        {
            RTLdrClose(mVRDPLibrary);
            mVRDPLibrary = NIL_RTLDRMOD;
        }
    }

    return vrc;
}

/*
 * IVRDEServerInfo implementation.
 */
// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

VRDEServerInfo::VRDEServerInfo()
    : mParent(NULL)
{
}

VRDEServerInfo::~VRDEServerInfo()
{
}


HRESULT VRDEServerInfo::FinalConstruct()
{
    return BaseFinalConstruct();
}

void VRDEServerInfo::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

// public methods only for internal purposes
/////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the guest object.
 */
HRESULT VRDEServerInfo::init(Console *aParent)
{
    LogFlowThisFunc(("aParent=%p\n", aParent));

    ComAssertRet(aParent, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    unconst(mParent) = aParent;

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}

/**
 *  Uninitializes the instance and sets the ready flag to FALSE.
 *  Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void VRDEServerInfo::uninit()
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    unconst(mParent) = NULL;
}

// IVRDEServerInfo properties
/////////////////////////////////////////////////////////////////////////////

#define IMPL_GETTER_BOOL(_aType, _aName, _aIndex)                         \
    HRESULT VRDEServerInfo::get##_aName(_aType *a##_aName)                \
    {                                                                     \
        /** @todo Not sure if a AutoReadLock would be sufficient. */       \
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);                  \
                                                                          \
        uint32_t value;                                                   \
        uint32_t cbOut = 0;                                               \
                                                                          \
        mParent->i_consoleVRDPServer()->QueryInfo                         \
            (_aIndex, &value, sizeof(value), &cbOut);                     \
                                                                          \
        *a##_aName = cbOut? !!value: FALSE;                               \
                                                                          \
        return S_OK;                                                      \
    }                                                                     \
    extern void IMPL_GETTER_BOOL_DUMMY(void)

#define IMPL_GETTER_SCALAR(_aType, _aName, _aIndex, _aValueMask)          \
    HRESULT VRDEServerInfo::get##_aName(_aType *a##_aName)                \
    {                                                                     \
        /** @todo Not sure if a AutoReadLock would be sufficient. */       \
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);                  \
                                                                          \
        _aType value;                                                     \
        uint32_t cbOut = 0;                                               \
                                                                          \
        mParent->i_consoleVRDPServer()->QueryInfo                         \
            (_aIndex, &value, sizeof(value), &cbOut);                     \
                                                                          \
        if (_aValueMask) value &= (_aValueMask);                          \
        *a##_aName = cbOut? value: 0;                                     \
                                                                          \
        return S_OK;                                                      \
    }                                                                     \
    extern void IMPL_GETTER_SCALAR_DUMMY(void)

#define IMPL_GETTER_UTF8STR(_aType, _aName, _aIndex)                      \
    HRESULT VRDEServerInfo::get##_aName(_aType &a##_aName)                \
    {                                                                     \
        /** @todo Not sure if a AutoReadLock would be sufficient. */       \
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);                  \
                                                                          \
        uint32_t cbOut = 0;                                               \
                                                                          \
        mParent->i_consoleVRDPServer()->QueryInfo                         \
            (_aIndex, NULL, 0, &cbOut);                                   \
                                                                          \
        if (cbOut == 0)                                                   \
        {                                                                 \
            a##_aName = Utf8Str::Empty;                                   \
            return S_OK;                                                  \
        }                                                                 \
                                                                          \
        char *pchBuffer = (char *)RTMemTmpAlloc(cbOut);                   \
                                                                          \
        if (!pchBuffer)                                                   \
        {                                                                 \
            Log(("VRDEServerInfo::"                                       \
                 #_aName                                                  \
                 ": Failed to allocate memory %d bytes\n", cbOut));       \
            return E_OUTOFMEMORY;                                         \
        }                                                                 \
                                                                          \
        mParent->i_consoleVRDPServer()->QueryInfo                         \
            (_aIndex, pchBuffer, cbOut, &cbOut);                          \
                                                                          \
        a##_aName = pchBuffer;                                            \
                                                                          \
        RTMemTmpFree(pchBuffer);                                          \
                                                                          \
        return S_OK;                                                      \
    }                                                                     \
    extern void IMPL_GETTER_BSTR_DUMMY(void)

IMPL_GETTER_BOOL   (BOOL,    Active,             VRDE_QI_ACTIVE);
IMPL_GETTER_SCALAR (LONG,    Port,               VRDE_QI_PORT,                  0);
IMPL_GETTER_SCALAR (ULONG,   NumberOfClients,    VRDE_QI_NUMBER_OF_CLIENTS,     0);
IMPL_GETTER_SCALAR (LONG64,  BeginTime,          VRDE_QI_BEGIN_TIME,            0);
IMPL_GETTER_SCALAR (LONG64,  EndTime,            VRDE_QI_END_TIME,              0);
IMPL_GETTER_SCALAR (LONG64,  BytesSent,          VRDE_QI_BYTES_SENT,            INT64_MAX);
IMPL_GETTER_SCALAR (LONG64,  BytesSentTotal,     VRDE_QI_BYTES_SENT_TOTAL,      INT64_MAX);
IMPL_GETTER_SCALAR (LONG64,  BytesReceived,      VRDE_QI_BYTES_RECEIVED,        INT64_MAX);
IMPL_GETTER_SCALAR (LONG64,  BytesReceivedTotal, VRDE_QI_BYTES_RECEIVED_TOTAL,  INT64_MAX);
IMPL_GETTER_UTF8STR(Utf8Str, User,               VRDE_QI_USER);
IMPL_GETTER_UTF8STR(Utf8Str, Domain,             VRDE_QI_DOMAIN);
IMPL_GETTER_UTF8STR(Utf8Str, ClientName,         VRDE_QI_CLIENT_NAME);
IMPL_GETTER_UTF8STR(Utf8Str, ClientIP,           VRDE_QI_CLIENT_IP);
IMPL_GETTER_SCALAR (ULONG,   ClientVersion,      VRDE_QI_CLIENT_VERSION,        0);
IMPL_GETTER_SCALAR (ULONG,   EncryptionStyle,    VRDE_QI_ENCRYPTION_STYLE,      0);

#undef IMPL_GETTER_UTF8STR
#undef IMPL_GETTER_SCALAR
#undef IMPL_GETTER_BOOL
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
