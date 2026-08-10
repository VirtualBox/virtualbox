/* $Id: vrdpapi.cpp 114912 2026-08-10 12:03:20Z vitali.pelenjow@oracle.com $ */
/** @file
 * VBox Remote Desktop Protocol - Public API functions.
 */

/*
 * Copyright (C) 2006-2026 Oracle and/or its affiliates.
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

#include "vrdpserv.h"
#include "utils.h"

#include <VBox/RemoteDesktop/VRDEOrders.h>

static DECLCALLBACK(void) VRDPDestroy (HVRDESERVER hServer)
{
    VRDPServer *pServer = (VRDPServer *)hServer;

    VRDPAPILOG(("%p\n", pServer));

    if (pServer)
    {
        delete pServer;
    }

    VRDPMemUninit ();
}

static DECLCALLBACK(int) VRDPEnableConnections (HVRDESERVER hServer,
                                                bool fEnable)
{
    int rc = VINF_SUCCESS;

    VRDPServer *pServer = (VRDPServer *)hServer;

    VRDPAPILOG(("%p\n", pServer));

    if (pServer)
    {
        rc = pServer->EnableConnections (fEnable);
    }

    return rc;
}

static DECLCALLBACK(void) VRDPDisconnect (HVRDESERVER hServer,
                                          uint32_t u32ClientId,
                                          bool fReconnect)
{
    VRDPServer *pServer = (VRDPServer *)hServer;

    VRDPAPILOG(("%p\n", pServer));

    if (pServer)
    {
        pServer->Disconnect (u32ClientId, fReconnect);
    }
}

static DECLCALLBACK(void) VRDPResize (HVRDESERVER hServer)
{
    VRDPServer *pServer = (VRDPServer *)hServer;

    VRDPAPILOG(("%p\n", pServer));

    if (pServer)
    {
        pServer->ProcessResize ();
    }
}

static DECLCALLBACK(void) VRDPUpdate (HVRDESERVER hServer,
                                      unsigned uScreenId,
                                      void *pvUpdate,
                                      uint32_t cbUpdate)
{
    VRDPServer *pServer = (VRDPServer *)hServer;

    if (pServer)
    {
#if 0 /* obsolete */
        if (cbUpdate > sizeof (VRDEORDERHDR))
        {
            /* The update includes VRDP order information. */
            VRDPAPILOG(("%p, %d, %d\n", pServer, cbUpdate, sizeof (VRDEORDERHDR)));
            pServer->ProcessOutputUpdate (uScreenId, pvUpdate, cbUpdate);
        }
        else if (cbUpdate == sizeof (VRDEORDERHDR))
#endif
        if (cbUpdate == sizeof (VRDEORDERHDR))
        {
            /* This is just a bitmap update. */
            VRDPAPILOG(("%p, %d, %d (bitmap)\n", pServer, cbUpdate, sizeof (VRDEORDERHDR)));
            VRDEORDERHDR *pHdr = (VRDEORDERHDR *)pvUpdate;
            pServer->ProcessBitmapUpdate (uScreenId, pHdr->x, pHdr->y, pHdr->w, pHdr->h);
        }
        else if (cbUpdate == 0 && pvUpdate == NULL)
        {
            pServer->ProcessUpdateComplete ();
        }
    }
}

static DECLCALLBACK(void) VRDEColorPointer (HVRDESERVER hServer,
                                            const VRDECOLORPOINTER *pPointer)
{
    VRDPServer *pServer = (VRDPServer *)hServer;

    VRDPAPILOG(("%p\n", pServer));

    if (pServer
        && pPointer
        && pPointer->u16Width <= 256
        && pPointer->u16Height <= 256)
    {
        unsigned cPixels = pPointer->u16Width * pPointer->u16Height;

        size_t cbNewPointer = sizeof(VRDEMOUSEPTRDATA) + pPointer->u16MaskLen + cPixels * 4;

        uint8_t *pu8NewPointer = (uint8_t *)VRDPMemAlloc(cbNewPointer);
        if (pu8NewPointer != NULL)
        {
            VRDEMOUSEPTRDATA *pNewPointer = (VRDEMOUSEPTRDATA *)pu8NewPointer;

            pNewPointer->u16HotX    = pPointer->u16HotX;
            pNewPointer->u16HotY    = pPointer->u16HotY;
            pNewPointer->u16Width   = pPointer->u16Width;
            pNewPointer->u16Height  = pPointer->u16Height;
            pNewPointer->u16MaskLen = pPointer->u16MaskLen;
            pNewPointer->u32DataLen = (uint32_t)(cPixels * 4);

            uint16_t x, y;

            /* AND mask. */
            uint8_t *pu8NewMask = pu8NewPointer + sizeof(VRDEMOUSEPTRDATA);

            uint16_t cbLine = (pNewPointer->u16Width + 7 ) / 8;
            uint8_t *pu8Src = (uint8_t *)pPointer + sizeof(VRDECOLORPOINTER);
            uint8_t *pu8Dst = pu8NewMask + (pNewPointer->u16Height - 1) * cbLine;

            for (y = 0; y < pNewPointer->u16Height ; y++)
            {
                memcpy(pu8Dst, pu8Src, cbLine);

                pu8Src += cbLine;
                pu8Dst -= cbLine;
            }

            /* XOR mask */
            uint8_t *pu8NewData = pu8NewMask + pPointer->u16MaskLen;

            cbLine = pNewPointer->u16Width * 4;
            pu8Src = (uint8_t *)pPointer + sizeof(VRDECOLORPOINTER) + pPointer->u16MaskLen;
            pu8Dst = pu8NewData + (pNewPointer->u16Height - 1) * cbLine;

            for (y = 0; y < pNewPointer->u16Height ; y++)
            {
                for (x = 0; x < pNewPointer->u16Width; x++)
                {
                    uint8_t *pu8DstPixel = pu8Dst + x * 4;
                    memcpy(pu8DstPixel, &pu8Src[3 * x], 3);
                    pu8DstPixel[3] = 0;
                }

                pu8Src += pNewPointer->u16Width * 3;
                pu8Dst -= cbLine;
            }

            pServer->ProcessPointer(pNewPointer);

            VRDPMemFree(pu8NewPointer);
        }
    }
}

static DECLCALLBACK(void) VRDPHidePointer (HVRDESERVER hServer)
{
    VRDPServer *pServer = (VRDPServer *)hServer;

    VRDPAPILOG(("%p\n", pServer));

    if (pServer)
    {
        pServer->PostOutput (VRDP_OUTPUT_HIDE_POINTER, 0, NULL, 0);
    }
}

static DECLCALLBACK(void) VRDPAudioSamples (HVRDESERVER hServer,
                                            const void *pvSamples,
                                            uint32_t cSamples,
                                            VRDEAUDIOFORMAT format)
{
    VRDPServer *pServer = (VRDPServer *)hServer;

    VRDPAPILOG(("%p\n", pServer));

    if (pServer)
    {
        pServer->ProcessAudio (pvSamples, cSamples, format);
    }
}

static DECLCALLBACK(void) VRDPAudioVolume (HVRDESERVER hServer,
                                           uint16_t u16Left,
                                           uint16_t u16Right)
{
    VRDPServer *pServer = (VRDPServer *)hServer;

    VRDPAPILOG(("%p\n", pServer));

    if (pServer)
    {
        VRDPSetVolumeStruct data;

        data.left = u16Left;
        data.right = u16Right;

        pServer->PostOutput (VRDP_OUTPUT_SET_VOLUME, 0, &data, sizeof (data));
    }
}

static DECLCALLBACK(void) VRDPUSBRequest (HVRDESERVER hServer,
                                          uint32_t u32ClientId,
                                          void *pvParm,
                                          uint32_t cbParm)
{
    VRDPServer *pServer = (VRDPServer *)hServer;

    VRDPAPILOG(("%p\n", pServer));

    if (pServer)
    {
        pServer->ProcessUSBSend (u32ClientId, pvParm, cbParm);
    }
}

static DECLCALLBACK(void) VRDPClipboard (HVRDESERVER hServer,
                                         uint32_t u32Function,
                                         uint32_t u32Format,
                                         void *pvData,
                                         uint32_t cbData,
                                         uint32_t *pcbActualRead)
{
    VRDPServer *pServer = (VRDPServer *)hServer;

    VRDPAPILOG(("%p\n", pServer));

    if (pServer)
    {
        pServer->ProcessClipboard (u32Function, u32Format, pvData, cbData, pcbActualRead);
    }
}

static DECLCALLBACK(void) VRDPQueryInfo (HVRDESERVER hServer,
                                         uint32_t index,
                                         void *pvBuffer,
                                         uint32_t cbBuffer,
                                         uint32_t *pcbOut)
{
    VRDPServer *pServer = (VRDPServer *)hServer;

    VRDPAPILOG(("%p\n", pServer));

    if (pServer)
    {
        pServer->QueryInfo (index, pvBuffer, cbBuffer, pcbOut);
    }
}

static DECLCALLBACK(void) VRDPRedirect (HVRDESERVER hServer,
                                        uint32_t u32ClientId,
                                        const char *pszServer,
                                        const char *pszUser,
                                        const char *pszDomain,
                                        const char *pszPassword,
                                        uint32_t u32SessionId,
                                        const char *pszCookie)
{
    VRDPServer *pServer = (VRDPServer *)hServer;

    VRDPAPILOG(("%p\n", pServer));

    if (pServer)
    {
        pServer->Redirect (u32ClientId,
                           pszServer,
                           pszUser,
                           pszDomain,
                           pszPassword,
                           u32SessionId,
                           pszCookie);
    }
}

static DECLCALLBACK(void) VRDEAudioInOpen (HVRDESERVER hServer,
                                           void *pvCtx,
                                           uint32_t u32ClientId,
                                           VRDEAUDIOFORMAT audioFormat,
                                           uint32_t u32SamplesPerBlock)
{
    VRDPServer *pServer = (VRDPServer *)hServer;

    VRDPAPILOG(("%p\n", pServer));

    if (pServer)
    {
        pServer->ProcessAudioInOpen (pvCtx, u32ClientId, audioFormat, u32SamplesPerBlock);
    }
}

static DECLCALLBACK(void) VRDEAudioInClose (HVRDESERVER hServer,
                                            uint32_t u32ClientId)
{
    VRDPServer *pServer = (VRDPServer *)hServer;

    VRDPAPILOG(("%p\n", pServer));

    if (pServer)
    {
        pServer->ProcessAudioInClose (u32ClientId);
    }
}

static DECLCALLBACK(int) VRDEGetInterface (HVRDESERVER hServer,
                                           const char *pszId,
                                           VRDEINTERFACEHDR *pInterface,
                                           const VRDEINTERFACEHDR *pCallbacks,
                                           void *pvContext)
{
    int rc = VERR_NOT_SUPPORTED;

    VRDPServer *pServer = (VRDPServer *)hServer;

    VRDPAPILOG(("%p\n", pServer));

    if (pServer)
    {
        rc = pServer->GetInterface(pszId, pInterface, pCallbacks, pvContext);
    }

    return rc;
}

static VRDEENTRYPOINTS_1 gEntryPoints_1 =
{
    { VRDE_INTERFACE_VERSION_1, sizeof (VRDEENTRYPOINTS_1) },

    VRDPDestroy,
    VRDPEnableConnections,
    VRDPDisconnect,
    VRDPResize,
    VRDPUpdate,
    VRDEColorPointer,
    VRDPHidePointer,
    VRDPAudioSamples,
    VRDPAudioVolume,
    VRDPUSBRequest,
    VRDPClipboard,
    VRDPQueryInfo
};

static VRDEENTRYPOINTS_2 gEntryPoints_2 =
{
    { VRDE_INTERFACE_VERSION_2, sizeof (VRDEENTRYPOINTS_2) },

    VRDPDestroy,
    VRDPEnableConnections,
    VRDPDisconnect,
    VRDPResize,
    VRDPUpdate,
    VRDEColorPointer,
    VRDPHidePointer,
    VRDPAudioSamples,
    VRDPAudioVolume,
    VRDPUSBRequest,
    VRDPClipboard,
    VRDPQueryInfo,
    VRDPRedirect
};

static VRDEENTRYPOINTS_3 gEntryPoints_3 =
{
    { VRDE_INTERFACE_VERSION_3, sizeof (VRDEENTRYPOINTS_3) },

    VRDPDestroy,
    VRDPEnableConnections,
    VRDPDisconnect,
    VRDPResize,
    VRDPUpdate,
    VRDEColorPointer,
    VRDPHidePointer,
    VRDPAudioSamples,
    VRDPAudioVolume,
    VRDPUSBRequest,
    VRDPClipboard,
    VRDPQueryInfo,
    VRDPRedirect,

    VRDEAudioInOpen,
    VRDEAudioInClose
};

static VRDEENTRYPOINTS_4 gEntryPoints_4 =
{
    { VRDE_INTERFACE_VERSION_4, sizeof (VRDEENTRYPOINTS_4) },

    VRDPDestroy,
    VRDPEnableConnections,
    VRDPDisconnect,
    VRDPResize,
    VRDPUpdate,
    VRDEColorPointer,
    VRDPHidePointer,
    VRDPAudioSamples,
    VRDPAudioVolume,
    VRDPUSBRequest,
    VRDPClipboard,
    VRDPQueryInfo,
    VRDPRedirect,

    VRDEAudioInOpen,
    VRDEAudioInClose,

    VRDEGetInterface
};


DECLEXPORT(int) VRDECreateServer (const VRDEINTERFACEHDR *pCallbacks,
                                    void *pvCallback,
                                    VRDEINTERFACEHDR **ppEntryPoints,
                                    HVRDESERVER *phServer)
{
    int rc = VINF_SUCCESS;

    VRDEINTERFACEHDR *pEntryPoints = NULL;
    VRDPServer *pServer = NULL;

    VRDPMemInit ();

    VRDPAPILOG(("StartServer\n"));

    if (   pCallbacks->u64Version == VRDE_INTERFACE_VERSION_1
        && pCallbacks->u64Size == sizeof (VRDECALLBACKS_1))
    {
        /* Interface version 1 is requested. */
        pServer = new VRDPServer (1, pCallbacks, pvCallback);

        if (!pServer)
        {
            rc = VERR_NO_MEMORY;
        }
        else
        {
            pEntryPoints = &gEntryPoints_1.header;
        }
    }
    else if (   pCallbacks->u64Version == VRDE_INTERFACE_VERSION_2
             && pCallbacks->u64Size == sizeof (VRDECALLBACKS_2))
    {
        /* Interface version 2 is requested.
         * Note: since the VRDECALLBACKS_2 is the same as VRDECALLBACKS_1,
         *       we can use the same VRDPServer implementation for both.
         */
        pServer = new VRDPServer (2, pCallbacks, pvCallback);

        if (!pServer)
        {
            rc = VERR_NO_MEMORY;
        }
        else
        {
            pEntryPoints = &gEntryPoints_2.header;
        }
    }
    else if (   pCallbacks->u64Version == VRDE_INTERFACE_VERSION_3
             && pCallbacks->u64Size == sizeof (VRDECALLBACKS_3))
    {
        /* Interface version 3 is requested.
         */
        pServer = new VRDPServer (3, pCallbacks, pvCallback);

        if (!pServer)
        {
            rc = VERR_NO_MEMORY;
        }
        else
        {
            pEntryPoints = &gEntryPoints_3.header;
        }
    }
    else if (   pCallbacks->u64Version == VRDE_INTERFACE_VERSION_4
             && pCallbacks->u64Size == sizeof (VRDECALLBACKS_4))
    {
        /* Interface version 4 is requested.
         */
        pServer = new VRDPServer (4, pCallbacks, pvCallback);

        if (!pServer)
        {
            rc = VERR_NO_MEMORY;
        }
        else
        {
            pEntryPoints = &gEntryPoints_4.header;
        }
    }
    else
    {
        /* The requested interface version is not supported. */
        rc = VERR_VERSION_MISMATCH;
    }

    if (RT_SUCCESS(rc))
    {
        rc = pServer->Start ();

        if (RT_SUCCESS(rc))
        {
            *ppEntryPoints = pEntryPoints;
            *phServer = (HVRDESERVER)pServer;
        }
        else
        {
            delete pServer;
            pServer = NULL;
        }
    }

    if (RT_FAILURE(rc))
    {
        VRDPMemUninit ();
    }

    VRDPAPILOG(("StartServer: pServer = %p, rc = %Rrc\n", pServer, rc));

    return rc;
}

static const char * const gapszProperties[] =
{
    "TCP/Ports",
    "TCP/Address",
    "VideoChannel/Enabled",
    "VideoChannel/Quality",
    "VideoChannel/DownscaleProtection",
    "Client/DisableDisplay",
    "Client/DisableInput",
    "Client/DisableAudio",
    "Client/DisableUSB",
    "Client/DisableClipboard",
    "Client/DisableUpstreamAudio",
    "Client/DisableRDPDR",
    "H3DRedirect/Enabled",
    "Security/Method",
    "Security/ServerCertificate",
    "Security/ServerPrivateKey",
    "Security/CACertificate",
/** @todo    "Security/CertificateProvider", */
    "Audio/RateCorrectionMode",
    "Audio/LogPath",
    NULL
};

DECLEXPORT(const char * const *) VRDESupportedProperties (void)
{
    return gapszProperties;
}
