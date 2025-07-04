/* $Id: wayland-helper-ipc.h 108999 2025-03-28 20:17:09Z vadim.galitsyn@oracle.com $ */
/** @file
 * Guest Additions - Definitions for IPC between VBoxClient and vboxwl tool.
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

#ifndef GA_INCLUDED_SRC_x11_VBoxClient_wayland_helper_ipc_h
#define GA_INCLUDED_SRC_x11_VBoxClient_wayland_helper_ipc_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

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
#include <iprt/time.h>
#include <iprt/nocrt/new>

#include <VBox/GuestHost/clipboard-helper.h>

#include "VBoxClient.h"
#include "wayland-helper.h"

/** Path to Gtk helper tool which raises popup window and gets
 * access to Wayland clipboard. */
#ifndef VBOXWL_PATH
# define VBOXWL_PATH                    "/usr/bin/vboxwl"
#endif
/** Limit maximum log verbosity level for popup tool. */
#define VBOXWL_VERBOSITY_MAX            (5)

/** IPC server socket name prefixes. */
#define VBOXWL_SRV_NAME_PREFIX_CLIP     "clip"
#define VBOXWL_SRV_NAME_PREFIX_DND      "dnd"

/** Arguments to vboxwl tool. */
#define VBOXWL_ARG_CLIP_HG_COPY         "--clip-hg-copy"
#define VBOXWL_ARG_CLIP_GH_ANNOUNCE     "--clip-gh-announce"
#define VBOXWL_ARG_CLIP_GH_COPY         "--clip-gh-copy"
#define VBOXWL_ARG_DND_GH               "--dnd-gh"
#define VBOXWL_ARG_DND_HG               "--dnd-hg"
#define VBOXWL_ARG_SESSION_ID           "--session-id"

/** Time in milliseconds to wait for IPC socket events. */
#define VBOX_GTKIPC_RX_TIMEOUT_MS       (VBCL_WAYLAND_DATA_WAIT_TIMEOUT_MS)

namespace vbcl
{
    namespace ipc
    {
        /** List of commands which VBoxClient and vboxwl use for IPC communication. */
        typedef enum
        {
            /** Initializer. */
            CMD_UNKNOWN = 0,
            /** Send or receive list of clipboard formats which
             *  host or guest announces. */
            VBOX_FORMATS,
            /** Send or receive a clipboard format which host
             *  or guest requests. */
            VBOX_FORMAT,
            /** Send or receive clipboard data in given format. */
            VBOX_DATA,
            /** Termination of commands list. */
            CMD_MAX
        } command_t;

        /** IPC command flow direction: from VBoxClient to vboxwl. */
        const bool FLOW_DIRECTION_CLIENT = false;
        /** IPC command flow direction: from vboxwl to VBoxClient. */
        const bool FLOW_DIRECTION_SERVER = true;

        /** IPC flow entry. */
        typedef struct
        {
            /** Command ID. */
            command_t enmCmd;
            /** Flow direction. */
            bool fDirection;
        } flow_t;

        /** IPC command header. */
        typedef struct
        {
            /** IPC command packet checksum, includes header and payload. */
            uint64_t u64Crc;
            /** IPC session ID. Generated by VBoxClient instance and
             * provided to vboxwl tool as a command line argument. Needs to be
             * checked by both parties when data is received over IPC. */
            uint32_t uSessionId;
            /** IPC command identificator (opaque). */
            command_t idCmd;
            /** Size of payload data. */
            uint64_t cbData;

        } packet_t;

        /**
         * Log IPC packet content.
         *
         * @param   pPacket     Pointer to IPC packet data.
         */
        void packet_dump(vbcl::ipc::packet_t *pPacket);

        /**
         * Verify IPC packet integrity.
         *
         * @returns True if packet integrity check passed, False otherwise.
         * @param   pPacket     Pointer to buffer which contains raw IPC packet data.
         * @param   cbPacket    Size of buffer.
         */
        bool packet_verify(vbcl::ipc::packet_t *pPacket, size_t cbPacket);

        /**
         * Read entire packet from IPC socket.
         *
         * @returns IPRT status code. In case if success, output IPC packet
         *          is validated and its fields, such as packet size, can be trusted.
         * @param   uSessionId      IPC session ID.
         * @param   hSession        IPC session handle.
         * @param   msTimeout       Read operation timeout in milliseconds.
         * @param   ppvData         Output buffer structured as validated
         *                          IPC packet (contains size inside).
         */
        int packet_read(uint32_t uSessionId, RTLOCALIPCSESSION hSession, uint32_t msTimeout, void **ppvData);

        /**
         * Write entire IPC packet into IPC socket.
         *
         * @returns IPRT status code.
         * @param   hSession        IPC session handle.
         * @param   pPacket         IPC packet.
         */
        int packet_write(RTLOCALIPCSESSION hSession, vbcl::ipc::packet_t *pPacket);

        namespace data
        {
            /** Payload for IPC commands VBOX_FORMATS and VBOX_FORMAT. */
            typedef struct
            {
                /** IPC command header. */
                vbcl::ipc::packet_t Hdr;
                /** Clipboard formats bitmask. */
                SHCLFORMATS fFormats;
            } formats_packet_t;

            /** Payload for IPC command VBOX_DATA. */
            typedef struct
            {
                /* IPC command header. */
                vbcl::ipc::packet_t Hdr;
                /** Clipboard buffer format. */
                SHCLFORMAT uFmt;
                /** Size of clipboard buffer data. */
                uint64_t cbData;
            } data_packet_t;

            /**
             * IPC commands flow is described as a table. Each entry of
             * such table contains command ID and flow direction. Both
             * sides, VBoxClient and vboxwl, go through exactly the same
             * command flow table sequentially and depending on a role
             * (client or server) either send or wait for data and receive
             * it. When last table entry is reached by both sides
             * (simultaneously) it means that IPC session is completed.
             * If error occurs on either side in the middle of the flow,
             * IPC session is aborted.
             */

            /** IPC flow description (clipboard): Copy clipboard from host to guest. */
            const flow_t HGCopyFlow[4] =
            {
                { VBOX_FORMATS, FLOW_DIRECTION_CLIENT },
                { VBOX_FORMAT,  FLOW_DIRECTION_SERVER },
                { VBOX_DATA,    FLOW_DIRECTION_CLIENT },
                { CMD_MAX,      false }
            };

            /** IPC flow description (clipboard): Copy clipboard from guest to host. */
            const flow_t GHCopyFlow[3] =
            {
                { VBOX_FORMAT,  FLOW_DIRECTION_CLIENT },
                { VBOX_DATA,    FLOW_DIRECTION_SERVER },
                { CMD_MAX,      false }
            };

            /** IPC flow description (clipboard): Announce guest's clipboard to the host
             *  and copy it to the host in format selected by host. */
            const flow_t GHAnnounceAndCopyFlow[4] =
            {
                { VBOX_FORMATS, FLOW_DIRECTION_SERVER },
                { VBOX_FORMAT,  FLOW_DIRECTION_CLIENT },
                { VBOX_DATA,    FLOW_DIRECTION_SERVER },
                { CMD_MAX,      false }
            };

            /** IPC flow description (DnD): DnD operation started inside guest and
             *  guest reports DnD content mime-type list. Host side picks up one
             *  of the formats and requests data in this format. Guest sends
             *  data in requested format. */
            const flow_t GHDragFlow[4] =
            {
                { VBOX_FORMATS, FLOW_DIRECTION_SERVER },
                { VBOX_FORMAT,  FLOW_DIRECTION_CLIENT },
                { VBOX_DATA,    FLOW_DIRECTION_SERVER },
                { CMD_MAX,      false }
            };

            /** IPC flow description (DnD): DnD operation started on host and
             *  host reports DnD content mime-type list. Guest side picks up one
             *  of the formats and requests data in this format. Host sends
             *  data in requested format. */
            const flow_t HGDragFlow[4] =
            {
                { VBOX_FORMATS, FLOW_DIRECTION_CLIENT },
                { VBOX_FORMAT,  FLOW_DIRECTION_SERVER },
                { VBOX_DATA,    FLOW_DIRECTION_CLIENT },
                { CMD_MAX,      false }
            };

            class DataIpc
            {
                public:

#ifdef RT_NEED_NEW_AND_DELETE
                    RTMEM_IMPLEMENT_NEW_AND_DELETE();
#endif
                    DataIpc()
                    {}

                    /**
                     * Initialize memory.
                     *
                     * @param   fServer     Specify IPC role; if True, server role
                     *                      is assigned (set by VBoxClient),
                     *                      otherwise client role is assigned (vboxwl).
                     * @param   uSessionId  Unique IPC session ID (randomly generated
                     *                      by server).
                     */
                    void init(bool fServer, uint32_t uSessionId)
                    {
                        m_fFmts.init(VBOX_SHCL_FMT_NONE, VBCL_WAYLAND_VALUE_WAIT_TIMEOUT_MS);
                        m_uFmt.init(VBOX_SHCL_FMT_NONE, VBCL_WAYLAND_VALUE_WAIT_TIMEOUT_MS);
                        m_pvDataBuf.init(0, VBCL_WAYLAND_DATA_WAIT_TIMEOUT_MS);
                        m_cbDataBuf.init(0, VBCL_WAYLAND_DATA_WAIT_TIMEOUT_MS);
                        m_fServer = fServer;
                        m_uSessionId = uSessionId;
                    }

                    /**
                     * Reset IPC session data and free allocated resources.
                     */
                    void reset()
                    {
                        void *pvData = (void *)m_pvDataBuf.reset();
                        if (RT_VALID_PTR(pvData))
                            RTMemFree(pvData);

                        m_fFmts.reset();
                        m_uFmt.reset();
                        m_cbDataBuf.reset();
                    }

                    /**
                     * Process IPC flow from start to finish.
                     *
                     * @returns IPRT status code.
                     * @param   pFlow           Pointer to selected IPC flow.
                     * @param   hIpcSession     IPC connection handle.
                     */
                    int flow(const flow_t *pFlow, RTLOCALIPCSESSION hIpcSession)
                    {
                        int idx = 0;
                        int rc = VINF_SUCCESS;

                        while (   RT_SUCCESS(rc)
                               && pFlow[idx].enmCmd != CMD_MAX)
                        {
                            rc = select_fn(pFlow[idx].enmCmd, pFlow[idx].fDirection, hIpcSession);
                            idx++;
                        }

                        return rc;
                    }

                    /** IPC session internal data. */
                    Waitable<volatile SHCLFORMATS> m_fFmts;
                    Waitable<volatile SHCLFORMAT> m_uFmt;
                    Waitable<volatile uint64_t> m_pvDataBuf;
                    Waitable<volatile uint32_t> m_cbDataBuf;

                protected:

                    /**
                     * Send available clipboard formats over IPC.
                     *
                     * @returns IPRT status code.
                     * @param   uSessionId      IPC session ID.
                     * @param   hIpcSession     IPC connection handle.
                     */
                    int send_formats(uint32_t uSessionId, RTLOCALIPCSESSION hIpcSession);

                    /**
                     * Receive available clipboard formats over IPC.
                     *
                     * @returns IPRT status code.
                     * @param   uSessionId      IPC session ID.
                     * @param   hIpcSession     IPC connection handle.
                     */
                    int recv_formats(uint32_t uSessionId, RTLOCALIPCSESSION hIpcSession);

                    /**
                     * Send requested clipboard format over IPC.
                     *
                     * @returns IPRT status code.
                     * @param   uSessionId      IPC session ID.
                     * @param   hIpcSession     IPC connection handle.
                     */
                    int send_format(uint32_t uSessionId, RTLOCALIPCSESSION hIpcSession);

                    /**
                     * Receive requested clipboard format over IPC.
                     *
                     * @returns IPRT status code.
                     * @param   uSessionId      IPC session ID.
                     * @param   hIpcSession     IPC connection handle.
                     */
                    int recv_format(uint32_t uSessionId, RTLOCALIPCSESSION hIpcSession);

                    /**
                     * Send clipboard buffer over IPC.
                     *
                     * @returns IPRT status code.
                     * @param   uSessionId      IPC session ID.
                     * @param   hIpcSession     IPC connection handle.
                     */
                    int send_data(uint32_t uSessionId, RTLOCALIPCSESSION hIpcSession);

                    /**
                     * Receive clipboard buffer over IPC.
                     *
                     * @returns IPRT status code.
                     * @param   uSessionId      IPC session ID.
                     * @param   hIpcSession     IPC connection handle.
                     */
                    int recv_data(uint32_t uSessionId, RTLOCALIPCSESSION hIpcSession);

                    /**
                     * Take one step flow action.
                     *
                     * Taking into account given command and IPC role, either
                     * send or receive data from IPC socket.
                     *
                     * @returns IPRT status code.
                     * @param   enmCmd          IPC command ID.
                     * @param   fDirection      IPC role.
                     * @param   hIpcSession     IPC connection handle.
                     */
                    int select_fn(command_t enmCmd, bool fDirection, RTLOCALIPCSESSION hIpcSession)
                    {
                        int rc = VERR_INVALID_PARAMETER;
                        bool fShouldSend;

                        if (m_fServer)
                            fShouldSend = (fDirection == FLOW_DIRECTION_CLIENT);
                        else
                            fShouldSend = (fDirection == FLOW_DIRECTION_SERVER);

                        switch(enmCmd)
                        {
                            case VBOX_FORMATS:
                            {
                                if (fShouldSend)
                                    rc = send_formats(m_uSessionId, hIpcSession);
                                else
                                    rc = recv_formats(m_uSessionId, hIpcSession);
                                break;
                            }

                            case VBOX_FORMAT:
                            {
                                if (fShouldSend)
                                    rc = send_format(m_uSessionId, hIpcSession);
                                else
                                    rc = recv_format(m_uSessionId, hIpcSession);
                                break;
                            }

                            case VBOX_DATA:
                            {
                                if (fShouldSend)
                                    rc = send_data(m_uSessionId, hIpcSession);
                                else
                                    rc = recv_data(m_uSessionId, hIpcSession);
                                break;
                            }

                            default:
                                break;
                        }

                        return rc;
                    }

                    bool m_fServer;
                    uint32_t m_uSessionId;
            };
        }
    }
}

/**
 * Helper function to get Gtk helper IPC server name.
 *
 * This function should be used by both IPC server and client code
 * in order to connect one to another. Output string will be in
 * format: GtkHlpIpcServer-&lt;prefix&gt;--&lt;active tty&gt;-&lt;user name&gt;.
 *
 * @returns     IPRT status code.
 * @param       szNamePrefix    Name prefix.
 * @param       szBuf           Where to store generated name string.
 * @param       cbBuf           Size of buffer.
 */
RTDECL(int) vbcl_wayland_hlp_gtk_ipc_srv_name(const char *szNamePrefix, char *szBuf, size_t cbBuf);

#endif /* !GA_INCLUDED_SRC_x11_VBoxClient_wayland_helper_ipc_h */

