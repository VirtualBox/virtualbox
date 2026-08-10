/* $Id: wayland-helper-ipc.cpp 114745 2026-07-21 18:40:35Z knut.osmundsen@oracle.com $ */
/** @file
 * Guest Additions - IPC between VBoxClient and vboxwl tool.
 *
 * @note Obsolete. Compiled with 'kmk VBOX_WITH_WAYLAND_ADDITIONS_LEGACY=1'.
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

#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>

#include <iprt/cdefs.h>
#include <iprt/err.h>
#include <iprt/linux/sysfs.h>
#include <iprt/localipc.h>
#include <iprt/mem.h>
#include <iprt/crc.h>
#include <iprt/env.h>
#include <iprt/process.h>
#include <iprt/asm.h>

#include <VBox/GuestHost/clipboard-helper.h>

#include "VBoxClient.h"
#include "wayland-helper-ipc.h"

int vbcl_wayland_hlp_gtk_ipc_srv_name(const char *pszNamePrefix, char *pszBuf, size_t cbBuf)
{
    /*
     * Validate and prep input.
     */
    AssertPtrReturn(pszNamePrefix, VERR_INVALID_POINTER);
    AssertPtrReturn(pszBuf, VERR_INVALID_POINTER);
    AssertReturn(cbBuf > 0, VERR_INVALID_PARAMETER);
    RT_BZERO(pszBuf, cbBuf);

    /*
     * Do work.
     */
    int rc = RTStrCat(pszBuf, cbBuf, "GtkHlpIpcServer-");
    if (RT_SUCCESS(rc))
        rc = RTStrCat(pszBuf, cbBuf, pszNamePrefix);
    if (RT_SUCCESS(rc))
        rc = RTStrCat(pszBuf, cbBuf, "-");
    if (RT_SUCCESS(rc))
    {
        size_t cchRead;
        char szActiveTTY[128] = {0};
        rc = RTLinuxSysFsReadStrFile(szActiveTTY, sizeof(szActiveTTY) - 1 /* reserve last byte for string termination */,
                                     &cchRead, "class/tty/tty0/active");
        if (RT_SUCCESS(rc))
            rc = RTStrCat(pszBuf, cbBuf, szActiveTTY);
    }
    if (RT_SUCCESS(rc))
        rc = RTStrCat(pszBuf, cbBuf, "-");
    if (RT_SUCCESS(rc))
    {
        struct passwd *pwd = getpwuid(geteuid());
        if (RT_VALID_PTR(pwd))
        {
            if (RT_VALID_PTR(pwd->pw_name))
                rc = RTStrCat(pszBuf, cbBuf, pwd->pw_name);
            else
                rc = VERR_NOT_FOUND;
        }
        else
            rc = VERR_NOT_FOUND;
    }

    return rc;
}

void vbcl::ipc::packet_dump(vbcl::ipc::packet_t *pPacket)
{
    VBClLogVerbose(3, "IPC packet dump:\n");
    VBClLogVerbose(3, "u64Crc    : 0x%x\n", pPacket->u64Crc);
    VBClLogVerbose(3, "uSessionId: %u\n", pPacket->uSessionId);
    VBClLogVerbose(3, "idCmd     : %u\n", pPacket->idCmd);
    VBClLogVerbose(3, "cbData:   : %u\n", pPacket->cbData);
}

bool vbcl::ipc::packet_verify(vbcl::ipc::packet_t *pPacket, size_t cbPacket)
{
    bool fResult = false;

    AssertPtrReturn(pPacket, VERR_INVALID_PARAMETER);
    AssertReturn(cbPacket > sizeof(vbcl::ipc::packet_t), VERR_INVALID_PARAMETER);

    AssertReturn(   pPacket->idCmd > vbcl::ipc::CMD_UNKNOWN
                 && pPacket->idCmd < vbcl::ipc::CMD_MAX, VERR_INVALID_PARAMETER);

    /* Exact size match. */
    if (pPacket->cbData == cbPacket)
    {
        /* CRC check. */
        uint64_t u64Crc = pPacket->u64Crc;
        pPacket->u64Crc = 0;
        if (u64Crc != 0 && RTCrc64(pPacket, pPacket->cbData) == u64Crc)
        {
            /* Verify payload size. */
            size_t cbPayload = 0;

            switch (pPacket->idCmd)
            {
                case vbcl::ipc::VBOX_FORMATS:
                case vbcl::ipc::VBOX_FORMAT:
                    cbPayload = sizeof(vbcl::ipc::data::formats_packet_t);
                    break;

                case vbcl::ipc::VBOX_DATA:
                {
                    vbcl::ipc::data::data_packet_t *pDataEx = (vbcl::ipc::data::data_packet_t *)pPacket;
                    cbPayload = sizeof(vbcl::ipc::data::data_packet_t) + pDataEx->cbData;
                    break;
                }

                default:
                    break;
            }

            if (pPacket->cbData == cbPayload)
                fResult = true;
            else
                VBClLogVerbose(3, "bad cmd size (%u vs %u)\n", pPacket->cbData, cbPayload);
        }
        else
            VBClLogVerbose(3, "bad crc\n");

        /* Restore CRC. */
        pPacket->u64Crc = u64Crc;
    }
    else
        VBClLogVerbose(3, "bad size\n");

    return fResult;
}

int vbcl::ipc::packet_read(uint32_t uSessionId, RTLOCALIPCSESSION hSession, uint32_t msTimeout, void **ppvData)
{
    AssertPtrReturn(ppvData, VERR_INVALID_PARAMETER);

    int rc = RTLocalIpcSessionWaitForData(hSession, msTimeout);
    if (RT_SUCCESS(rc))
    {
        /* Read IPC message header. */
        vbcl::ipc::packet_t Packet = { 0, 0, CMD_UNKNOWN, 0 };
        rc = RTLocalIpcSessionRead(hSession, &Packet, sizeof(Packet), NULL);
        if (RT_SUCCESS(rc))
        {
            bool fCheck = true;
#define MY_CHECK(a_MustBeTrueExpr, a_szMsgFmt, a_MsgArg) \
                if (!(a_MustBeTrueExpr)) \
                { \
                    VBClLogVerbose(3, a_szMsgFmt "\n", a_MsgArg); \
                    fCheck = false; \
                } else do {} while (0)
            MY_CHECK(Packet.u64Crc != 0,                "bad crc: 0x%x", Packet.u64Crc);
            MY_CHECK(Packet.uSessionId == uSessionId,   "bad session id: %u", Packet.uSessionId);
            MY_CHECK(Packet.cbData > sizeof(Packet) && Packet.cbData <= _256M, /** @todo r=bird: what's a sensible max limit here? */
                     "bad cbData: %u", Packet.cbData);
            if (fCheck)
            {
                /* Receive the rest of a message. */
                uint8_t *pbData = (uint8_t *)RTMemAllocZ(Packet.cbData);
                if (pbData)
                {
                    /* Add generic header to the beginning of the output buffer
                     * and receive the rest of the data into it. */
                    memcpy(pbData, &Packet, sizeof(Packet));

                    rc = RTLocalIpcSessionRead(hSession, &pbData[sizeof(Packet)], Packet.cbData - sizeof(Packet), NULL);
                    if (RT_SUCCESS(rc))
                    {
                        if (vbcl::ipc::packet_verify((vbcl::ipc::packet_t *)pbData, Packet.cbData))
                        {
                            /* Now return received data to the caller. */
                            *ppvData = pbData;
                            pbData = NULL;
                        }
                        else
                            rc = VERR_NOT_EQUAL;
                    }
                    RTMemFree(pbData);
                }
                else
                {
                    VBClLogError("Failed to allocate %#x bytes for IPC package!!\n", Packet.cbData);
                    /** @todo r=bird: This is kind of fatal, since the IPC stream will be out of sync now. */
                    rc = VERR_NO_MEMORY;
                }
            }
            else
                rc = VERR_INVALID_PARAMETER;

            if (RT_FAILURE(rc))
                vbcl::ipc::packet_dump(&Packet);
        }
    }

    return rc;
}

int vbcl::ipc::packet_write(RTLOCALIPCSESSION hSession, vbcl::ipc::packet_t *pPacket)
{
    int rc;

    AssertPtrReturn(pPacket, VERR_INVALID_PARAMETER);

    pPacket->u64Crc = 0;
    pPacket->u64Crc = RTCrc64(pPacket, pPacket->cbData);

    Assert(pPacket->u64Crc);

    if (vbcl::ipc::packet_verify(pPacket, pPacket->cbData))
    {
        rc = RTLocalIpcSessionWrite(hSession, (void *)pPacket, pPacket->cbData);
        if (RT_SUCCESS(rc))
            rc = RTLocalIpcSessionFlush(hSession);
    }
    else
    {
        vbcl::ipc::packet_dump(pPacket);
        rc = VERR_NOT_EQUAL;
    }

    return rc;
}

int vbcl::ipc::data::DataIpc::send_formats(uint32_t uSessionId, RTLOCALIPCSESSION hIpcSession)
{
    SHCLFORMATS fFormats;
    int rc = VINF_SUCCESS;

    vbcl::ipc::data::formats_packet_t Packet;
    RT_ZERO(Packet);
    Packet.Hdr.u64Crc = 0;
    Packet.Hdr.uSessionId = uSessionId;
    Packet.Hdr.idCmd = VBOX_FORMATS;
    Packet.Hdr.cbData = sizeof(Packet);

    fFormats = m_fFmts.wait();
    if (fFormats != m_fFmts.defaults())
    {
        Packet.fFormats = fFormats;
        rc = vbcl::ipc::packet_write(hIpcSession, &Packet.Hdr);
    }
    else
        rc = VERR_TIMEOUT;

    VBClLogVerbose(3, "%s: send_formats [sid=%u, fmts=0x%x], rc=%Rrc\n",
                   m_fServer ? "server" : "client", uSessionId, fFormats, rc);

    return rc;
}

int vbcl::ipc::data::DataIpc::recv_formats(uint32_t uSessionId, RTLOCALIPCSESSION hIpcSession)
{
    int rc;
    vbcl::ipc::data::formats_packet_t *pPacket;
    vbcl::ipc::command_t idCmd = CMD_UNKNOWN;
    SHCLFORMATS fFormats = VBOX_SHCL_FMT_NONE;

    rc = vbcl::ipc::packet_read(uSessionId, hIpcSession, m_fFmts.timeout(), (void **)&pPacket);
    if (RT_SUCCESS(rc))
    {
        if (   pPacket->Hdr.idCmd == VBOX_FORMATS
            && vbcl::ipc::packet_verify(&pPacket->Hdr, pPacket->Hdr.cbData))
        {
            fFormats = pPacket->fFormats;
            idCmd = pPacket->Hdr.idCmd;
            m_fFmts.set(fFormats);
        }
        else
            rc = VERR_WRONG_ORDER;

        RTMemFree(pPacket);
    }

    VBClLogVerbose(3, "%s: recv_formats [sid=%u, cmd=0x%x, fmts=0x%x], rc=%Rrc\n",
                   m_fServer ? "server" : "client", uSessionId, idCmd, fFormats, rc);

    return rc;
}

int vbcl::ipc::data::DataIpc::send_format(uint32_t uSessionId, RTLOCALIPCSESSION hIpcSession)
{
    vbcl::ipc::data::formats_packet_t Packet;
    SHCLFORMAT uFormat;
    int rc = VINF_SUCCESS;

    RT_ZERO(Packet);

    Packet.Hdr.u64Crc = 0;
    Packet.Hdr.uSessionId = uSessionId;
    Packet.Hdr.idCmd = VBOX_FORMAT;
    Packet.Hdr.cbData = sizeof(Packet);

    uFormat = m_uFmt.wait();
    if (uFormat != m_uFmt.defaults())
    {
        Packet.fFormats = uFormat;
        rc = vbcl::ipc::packet_write(hIpcSession, &Packet.Hdr);
    }
    else
        rc = VERR_TIMEOUT;

    VBClLogVerbose(3, "%s: send_format [sid=%u, fmt=0x%x], rc=%Rrc\n",
                   m_fServer ? "server" : "client", uSessionId, uFormat, rc);

    return rc;
}

int vbcl::ipc::data::DataIpc::recv_format(uint32_t uSessionId, RTLOCALIPCSESSION hIpcSession)
{
    int rc;
    vbcl::ipc::data::formats_packet_t *pPacket;
    vbcl::ipc::command_t idCmd = CMD_UNKNOWN;
    SHCLFORMATS uFormat = VBOX_SHCL_FMT_NONE;

    rc = vbcl::ipc::packet_read(uSessionId, hIpcSession, m_uFmt.timeout(), (void **)&pPacket);
    if (RT_SUCCESS(rc))
    {
        if (   pPacket->Hdr.idCmd == VBOX_FORMAT
            && vbcl::ipc::packet_verify(&pPacket->Hdr, pPacket->Hdr.cbData))
        {
            uFormat = pPacket->fFormats;
            idCmd = pPacket->Hdr.idCmd;
            m_uFmt.set(uFormat);
        }
        else
            rc = VERR_WRONG_ORDER;

        RTMemFree(pPacket);
    }

    VBClLogVerbose(3, "%s: recv_format [sid=%u, cmd=0x%x, fmts=0x%x], rc=%Rrc\n",
                   m_fServer ? "server" : "client", uSessionId, idCmd, uFormat, rc);

    return rc;
}

int vbcl::ipc::data::DataIpc::send_data(uint32_t uSessionId, RTLOCALIPCSESSION hIpcSession)
{
    vbcl::ipc::data::data_packet_t *pPacket;
    int rc = VINF_SUCCESS;

    void *pvData;
    uint32_t cbData;

    cbData = m_cbDataBuf.wait();
    pvData = (void *)m_pvDataBuf.wait();
    if (   cbData != m_cbDataBuf.defaults()
        && pvData != (void *)m_pvDataBuf.defaults())
    {
        pPacket = (vbcl::ipc::data::data_packet_t *)RTMemAllocZ(sizeof(vbcl::ipc::data::data_packet_t) + cbData);
        if (RT_VALID_PTR(pPacket))
        {
            pPacket->Hdr.u64Crc = 0;
            pPacket->Hdr.uSessionId = uSessionId;
            pPacket->Hdr.idCmd = VBOX_DATA;
            pPacket->Hdr.cbData = sizeof(vbcl::ipc::data::data_packet_t) + cbData;
            pPacket->cbData = cbData;

            memcpy((uint8_t *)pPacket + sizeof(vbcl::ipc::data::data_packet_t), pvData, cbData);
            rc = vbcl::ipc::packet_write(hIpcSession, &pPacket->Hdr);
            RTMemFree(pPacket);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
        rc = VERR_TIMEOUT;

    VBClLogVerbose(3, "%s: send_data [sid=%u, cbData=%u], rc=%Rrc\n",
                   m_fServer ? "server" : "client", uSessionId, cbData, rc);

    return rc;
}

int vbcl::ipc::data::DataIpc::recv_data(uint32_t uSessionId, RTLOCALIPCSESSION hIpcSession)
{
    int rc;
    vbcl::ipc::data::data_packet_t *pPacket;
    vbcl::ipc::command_t idCmd = CMD_UNKNOWN;
    uint32_t cbData = 0;

    rc = vbcl::ipc::packet_read(uSessionId, hIpcSession, m_pvDataBuf.timeout(), (void **)&pPacket);
    if (RT_SUCCESS(rc))
    {
        if (   pPacket->Hdr.idCmd == VBOX_DATA
            && vbcl::ipc::packet_verify(&pPacket->Hdr, pPacket->Hdr.cbData))
        {
            void *pvData = RTMemAllocZ(pPacket->cbData);
            idCmd = pPacket->Hdr.idCmd;
            if (RT_VALID_PTR(pvData))
            {
                memcpy(pvData, (uint8_t *)pPacket + sizeof(vbcl::ipc::data::data_packet_t), pPacket->cbData);
                m_pvDataBuf.set((uint64_t)pvData);
                cbData = pPacket->cbData;
                m_cbDataBuf.set(cbData);
            }
            else
                rc = VERR_NO_MEMORY;
        }
        else
            rc = VERR_WRONG_ORDER;

        RTMemFree(pPacket);
    }

    VBClLogVerbose(3, "%s: recv_data [sid=%u, cmd=0x%x, cbData=%u], rc=%Rrc\n",
                   m_fServer ? "server" : "client", uSessionId, idCmd, cbData, rc);

    return rc;
}
