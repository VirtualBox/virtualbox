/* $Id: DrvHostAudioPulseAudioStubs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Stubs for libpulse.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DRV_HOST_AUDIO
#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/ldr.h>
#include <VBox/log.h>
#include <iprt/once.h>

#include <pulse/pulseaudio.h>

#include "DrvHostAudioPulseAudioStubs.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define VBOX_PULSE_LIB "libpulse.so.0"

#define PROXY_STUB(function, rettype, signature, shortsig) \
    static rettype (*g_pfn_ ## function) signature; \
    \
    extern "C" rettype VBox_##function signature; \
    rettype VBox_##function signature \
    { \
        return g_pfn_ ## function shortsig; \
    }

#define PROXY_STUB_VOID(function, signature, shortsig) \
    static void (*g_pfn_ ## function) signature; \
    \
    extern "C" void VBox_##function signature; \
    void VBox_##function signature \
    { \
        g_pfn_ ## function shortsig; \
    }

PROXY_STUB     (pa_bytes_per_second, size_t,
                (const pa_sample_spec *spec),
                (spec))
PROXY_STUB     (pa_bytes_to_usec, pa_usec_t,
                (uint64_t l, const pa_sample_spec *spec),
                (l, spec))
PROXY_STUB     (pa_channel_map_init_auto, pa_channel_map*,
                (pa_channel_map *m, unsigned channels, pa_channel_map_def_t def),
                (m, channels, def))

PROXY_STUB     (pa_context_connect, int,
                (pa_context *c, const char *server, pa_context_flags_t flags,
                 const pa_spawn_api *api),
                (c, server, flags, api))
PROXY_STUB_VOID(pa_context_disconnect,
                (pa_context *c),
                (c))
PROXY_STUB     (pa_context_get_server_info, pa_operation*,
                (pa_context *c, pa_server_info_cb_t cb, void *userdata),
                (c, cb, userdata))
PROXY_STUB     (pa_context_get_sink_info_by_name, pa_operation*,
                (pa_context *c, const char *name, pa_sink_info_cb_t cb, void *userdata),
                (c, name, cb, userdata))
PROXY_STUB     (pa_context_get_sink_info_list, pa_operation *,
                (pa_context *c, pa_sink_info_cb_t cb, void *userdata),
                (c, cb, userdata))
PROXY_STUB     (pa_context_get_source_info_by_name, pa_operation*,
                (pa_context *c, const char *name, pa_source_info_cb_t cb, void *userdata),
                (c, name, cb, userdata))
PROXY_STUB     (pa_context_get_source_info_list, pa_operation *,
                (pa_context *c, pa_source_info_cb_t cb, void *userdata),
                (c, cb, userdata))
PROXY_STUB     (pa_context_get_state, pa_context_state_t,
                (pa_context *c),
                (c))
PROXY_STUB_VOID(pa_context_unref,
                (pa_context *c),
                (c))
PROXY_STUB     (pa_context_errno, int,
                (pa_context *c),
                (c))
PROXY_STUB     (pa_context_new, pa_context*,
                (pa_mainloop_api *mainloop, const char *name),
                (mainloop, name))
PROXY_STUB_VOID(pa_context_set_state_callback,
                (pa_context *c, pa_context_notify_cb_t cb, void *userdata),
                (c, cb, userdata))

PROXY_STUB     (pa_frame_size, size_t,
                (const pa_sample_spec *spec),
                (spec))
PROXY_STUB     (pa_get_library_version, const char *, (void), ())
PROXY_STUB_VOID(pa_operation_unref,
                (pa_operation *o),
                (o))
PROXY_STUB     (pa_operation_get_state, pa_operation_state_t,
                (pa_operation *o),
                (o))
PROXY_STUB_VOID(pa_operation_cancel,
                (pa_operation *o),
                (o))

PROXY_STUB     (pa_rtclock_now, pa_usec_t,
                (void),
                ())
PROXY_STUB     (pa_sample_format_to_string, const char*,
                (pa_sample_format_t f),
                (f))
PROXY_STUB     (pa_sample_spec_valid, int,
                (const pa_sample_spec *spec),
                (spec))
PROXY_STUB     (pa_strerror, const char*,
                (int error),
                (error))

#if PA_PROTOCOL_VERSION >= 16
PROXY_STUB     (pa_stream_connect_playback, int,
                (pa_stream *s, const char *dev, const pa_buffer_attr *attr,
                 pa_stream_flags_t flags, const pa_cvolume *volume, pa_stream *sync_stream),
                (s, dev, attr, flags, volume, sync_stream))
#else
PROXY_STUB     (pa_stream_connect_playback, int,
                (pa_stream *s, const char *dev, const pa_buffer_attr *attr,
                 pa_stream_flags_t flags, pa_cvolume *volume, pa_stream *sync_stream),
                (s, dev, attr, flags, volume, sync_stream))
#endif
PROXY_STUB     (pa_stream_connect_record, int,
                (pa_stream *s, const char *dev, const pa_buffer_attr *attr,
                pa_stream_flags_t flags),
                (s, dev, attr, flags))
PROXY_STUB     (pa_stream_disconnect, int,
                (pa_stream *s),
                (s))
PROXY_STUB     (pa_stream_get_sample_spec, const pa_sample_spec*,
                (pa_stream *s),
                (s))
PROXY_STUB_VOID(pa_stream_set_latency_update_callback,
                (pa_stream *p, pa_stream_notify_cb_t cb, void *userdata),
                (p, cb, userdata))
PROXY_STUB     (pa_stream_write, int,
                (pa_stream *p, const void *data, size_t bytes, pa_free_cb_t free_cb,
                 int64_t offset, pa_seek_mode_t seek),
                (p, data, bytes, free_cb, offset, seek))
PROXY_STUB_VOID(pa_stream_unref,
                (pa_stream *s),
                (s))
PROXY_STUB     (pa_stream_get_state, pa_stream_state_t,
                (pa_stream *p),
                (p))
PROXY_STUB     (pa_stream_get_latency, int,
                (pa_stream *s, pa_usec_t *r_usec, int *negative),
                (s, r_usec, negative))
PROXY_STUB     (pa_stream_get_timing_info, pa_timing_info*,
                (pa_stream *s),
                (s))
PROXY_STUB     (pa_stream_readable_size, size_t,
                (pa_stream *p),
                (p))
PROXY_STUB      (pa_stream_set_buffer_attr, pa_operation *,
                (pa_stream *s, const pa_buffer_attr *attr, pa_stream_success_cb_t cb, void *userdata),
                (s, attr, cb, userdata))
PROXY_STUB_VOID(pa_stream_set_state_callback,
                (pa_stream *s, pa_stream_notify_cb_t cb, void *userdata),
                (s, cb, userdata))
PROXY_STUB_VOID(pa_stream_set_underflow_callback,
                (pa_stream *s, pa_stream_notify_cb_t cb, void *userdata),
                (s, cb, userdata))
PROXY_STUB_VOID(pa_stream_set_overflow_callback,
                (pa_stream *s, pa_stream_notify_cb_t cb, void *userdata),
                (s, cb, userdata))
PROXY_STUB_VOID(pa_stream_set_write_callback,
                (pa_stream *s, pa_stream_request_cb_t cb, void *userdata),
                (s, cb, userdata))
PROXY_STUB     (pa_stream_flush, pa_operation*,
                (pa_stream *s, pa_stream_success_cb_t cb, void *userdata),
                (s, cb, userdata))
PROXY_STUB     (pa_stream_drain, pa_operation*,
                (pa_stream *s, pa_stream_success_cb_t cb, void *userdata),
                (s, cb, userdata))
PROXY_STUB     (pa_stream_trigger, pa_operation*,
                (pa_stream *s, pa_stream_success_cb_t cb, void *userdata),
                (s, cb, userdata))
PROXY_STUB     (pa_stream_new, pa_stream*,
                (pa_context *c, const char *name, const pa_sample_spec *ss,
                 const pa_channel_map *map),
                (c, name, ss, map))
PROXY_STUB     (pa_stream_get_buffer_attr, const pa_buffer_attr*,
                (pa_stream *s),
                (s))
PROXY_STUB     (pa_stream_peek, int,
                (pa_stream *p, const void **data, size_t *bytes),
                (p, data, bytes))
PROXY_STUB     (pa_stream_cork, pa_operation*,
                (pa_stream *s, int b, pa_stream_success_cb_t cb, void *userdata),
                (s, b, cb, userdata))
PROXY_STUB     (pa_stream_drop, int,
                (pa_stream *p),
                (p))
PROXY_STUB     (pa_stream_writable_size, size_t,
                (pa_stream *p),
                (p))

PROXY_STUB_VOID(pa_threaded_mainloop_stop,
                (pa_threaded_mainloop *m),
                (m))
PROXY_STUB     (pa_threaded_mainloop_get_api, pa_mainloop_api*,
                (pa_threaded_mainloop *m),
                (m))
PROXY_STUB_VOID(pa_threaded_mainloop_free,
                (pa_threaded_mainloop* m),
                (m))
PROXY_STUB_VOID(pa_threaded_mainloop_signal,
                (pa_threaded_mainloop *m, int wait_for_accept),
                (m, wait_for_accept))
PROXY_STUB_VOID(pa_threaded_mainloop_unlock,
                (pa_threaded_mainloop *m),
                (m))
PROXY_STUB     (pa_threaded_mainloop_new, pa_threaded_mainloop *,
                (void),
                ())
PROXY_STUB_VOID(pa_threaded_mainloop_wait,
                (pa_threaded_mainloop *m),
                (m))
PROXY_STUB     (pa_threaded_mainloop_start, int,
                (pa_threaded_mainloop *m),
                (m))
PROXY_STUB_VOID(pa_threaded_mainloop_lock,
                (pa_threaded_mainloop *m),
                (m))

PROXY_STUB     (pa_usec_to_bytes, size_t,
                (pa_usec_t t, const pa_sample_spec *spec),
                (t, spec))

#define FUNC_ENTRY(function) { #function , (void (**)(void)) & g_pfn_ ## function }
static struct
{
    const char *pszName;
    void     (**pfn)(void);
} const g_aImportedFunctions[] =
{
    FUNC_ENTRY(pa_bytes_per_second),
    FUNC_ENTRY(pa_bytes_to_usec),
    FUNC_ENTRY(pa_channel_map_init_auto),

    FUNC_ENTRY(pa_context_connect),
    FUNC_ENTRY(pa_context_disconnect),
    FUNC_ENTRY(pa_context_get_server_info),
    FUNC_ENTRY(pa_context_get_sink_info_by_name),
    FUNC_ENTRY(pa_context_get_sink_info_list),
    FUNC_ENTRY(pa_context_get_source_info_by_name),
    FUNC_ENTRY(pa_context_get_source_info_list),
    FUNC_ENTRY(pa_context_get_state),
    FUNC_ENTRY(pa_context_unref),
    FUNC_ENTRY(pa_context_errno),
    FUNC_ENTRY(pa_context_new),
    FUNC_ENTRY(pa_context_set_state_callback),

    FUNC_ENTRY(pa_frame_size),
    FUNC_ENTRY(pa_get_library_version),
    FUNC_ENTRY(pa_operation_unref),
    FUNC_ENTRY(pa_operation_get_state),
    FUNC_ENTRY(pa_operation_cancel),
    FUNC_ENTRY(pa_rtclock_now),
    FUNC_ENTRY(pa_sample_format_to_string),
    FUNC_ENTRY(pa_sample_spec_valid),
    FUNC_ENTRY(pa_strerror),

    FUNC_ENTRY(pa_stream_connect_playback),
    FUNC_ENTRY(pa_stream_connect_record),
    FUNC_ENTRY(pa_stream_disconnect),
    FUNC_ENTRY(pa_stream_get_sample_spec),
    FUNC_ENTRY(pa_stream_set_latency_update_callback),
    FUNC_ENTRY(pa_stream_write),
    FUNC_ENTRY(pa_stream_unref),
    FUNC_ENTRY(pa_stream_get_state),
    FUNC_ENTRY(pa_stream_get_latency),
    FUNC_ENTRY(pa_stream_get_timing_info),
    FUNC_ENTRY(pa_stream_readable_size),
    FUNC_ENTRY(pa_stream_set_buffer_attr),
    FUNC_ENTRY(pa_stream_set_state_callback),
    FUNC_ENTRY(pa_stream_set_underflow_callback),
    FUNC_ENTRY(pa_stream_set_overflow_callback),
    FUNC_ENTRY(pa_stream_set_write_callback),
    FUNC_ENTRY(pa_stream_flush),
    FUNC_ENTRY(pa_stream_drain),
    FUNC_ENTRY(pa_stream_trigger),
    FUNC_ENTRY(pa_stream_new),
    FUNC_ENTRY(pa_stream_get_buffer_attr),
    FUNC_ENTRY(pa_stream_peek),
    FUNC_ENTRY(pa_stream_cork),
    FUNC_ENTRY(pa_stream_drop),
    FUNC_ENTRY(pa_stream_writable_size),

    FUNC_ENTRY(pa_threaded_mainloop_stop),
    FUNC_ENTRY(pa_threaded_mainloop_get_api),
    FUNC_ENTRY(pa_threaded_mainloop_free),
    FUNC_ENTRY(pa_threaded_mainloop_signal),
    FUNC_ENTRY(pa_threaded_mainloop_unlock),
    FUNC_ENTRY(pa_threaded_mainloop_new),
    FUNC_ENTRY(pa_threaded_mainloop_wait),
    FUNC_ENTRY(pa_threaded_mainloop_start),
    FUNC_ENTRY(pa_threaded_mainloop_lock),

    FUNC_ENTRY(pa_usec_to_bytes)
};
#undef FUNC_ENTRY

/** Init once.   */
static RTONCE g_PulseAudioLibInitOnce = RTONCE_INITIALIZER;

/** @callback_method_impl{FNRTONCE} */
static DECLCALLBACK(int32_t) drvHostAudioPulseLibInitOnce(void *pvUser)
{
    RT_NOREF(pvUser);
    LogFlowFunc(("\n"));

    RTLDRMOD hMod = NIL_RTLDRMOD;
    int rc = RTLdrLoadSystemEx(VBOX_PULSE_LIB, RTLDRLOAD_FLAGS_NO_UNLOAD, &hMod);
    if (RT_SUCCESS(rc))
    {
        for (unsigned i = 0; i < RT_ELEMENTS(g_aImportedFunctions); i++)
        {
            rc = RTLdrGetSymbol(hMod, g_aImportedFunctions[i].pszName, (void **)g_aImportedFunctions[i].pfn);
            if (RT_FAILURE(rc))
            {
                LogRelFunc(("Failed to resolve function #%u: '%s' (%Rrc)\n", i, g_aImportedFunctions[i].pszName, rc));
                break;
            }
        }

        RTLdrClose(hMod);
    }
    else
        LogRelFunc(("Failed to load library %s: %Rrc\n", VBOX_PULSE_LIB, rc));
    return rc;
}

/**
 * Try to dynamically load the PulseAudio libraries.
 *
 * @returns VBox status code.
 */
int audioLoadPulseLib(void)
{
    LogFlowFunc(("\n"));
    return RTOnce(&g_PulseAudioLibInitOnce, drvHostAudioPulseLibInitOnce, NULL);
}

