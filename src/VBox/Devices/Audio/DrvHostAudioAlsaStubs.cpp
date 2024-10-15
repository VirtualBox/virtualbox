/* $Id: DrvHostAudioAlsaStubs.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Stubs for libasound.
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

#define LOG_GROUP LOG_GROUP_DRV_HOST_AUDIO
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/ldr.h>
#include <VBox/log.h>
#include <iprt/once.h>

#include <alsa/asoundlib.h>
#include <errno.h>

#include "DrvHostAudioAlsaStubs.h"

#define VBOX_ALSA_LIB "libasound.so.2"

#define PROXY_STUB(function, rettype, signature, shortsig) \
    static rettype (*pfn_ ## function) signature; \
    \
    extern "C" rettype VBox_##function signature; \
    rettype VBox_##function signature \
    { \
        return pfn_ ## function shortsig; \
    }

PROXY_STUB(snd_lib_error_set_handler, int, (snd_lib_error_handler_t handler),
           (handler))
PROXY_STUB(snd_strerror, const char *, (int errnum), (errnum))

PROXY_STUB(snd_device_name_hint, int,
           (int card, const char *iface, void ***hints),
           (card, iface, hints))
PROXY_STUB(snd_device_name_free_hint, int,
           (void **hints),
           (hints))
PROXY_STUB(snd_device_name_get_hint, char *,
           (const void *hint, const char *id),
           (hint, id))

static int fallback_snd_device_name_hint(int card, const char *iface, void ***hints)
{
    RT_NOREF(card, iface);
    *hints = NULL;
    return -ENOSYS;
}

static int   fallback_snd_device_name_free_hint(void **hints)
{
    RT_NOREF(hints);
    return 0;
}

static char *fallback_snd_device_name_get_hint(const void *hint, const char *id)
{
    RT_NOREF(hint, id);
    return NULL;
}

/*
 * PCM
 */

PROXY_STUB(snd_pcm_avail_update, snd_pcm_sframes_t, (snd_pcm_t *pcm), (pcm))
PROXY_STUB(snd_pcm_avail_delay, int,
           (snd_pcm_t *pcm, snd_pcm_sframes_t *availp, snd_pcm_sframes_t *delayp),
           (pcm, availp, delayp))
PROXY_STUB(snd_pcm_close, int, (snd_pcm_t *pcm), (pcm))
PROXY_STUB(snd_pcm_delay, int, (snd_pcm_t *pcm, snd_pcm_sframes_t *delayp), (pcm, delayp))
PROXY_STUB(snd_pcm_nonblock, int, (snd_pcm_t *pcm, int onoff),
           (pcm, onoff))
PROXY_STUB(snd_pcm_drain, int, (snd_pcm_t *pcm),
           (pcm))
PROXY_STUB(snd_pcm_drop, int, (snd_pcm_t *pcm), (pcm))
PROXY_STUB(snd_pcm_open, int,
           (snd_pcm_t **pcm, const char *name, snd_pcm_stream_t stream, int mode),
           (pcm, name, stream, mode))
PROXY_STUB(snd_pcm_prepare, int, (snd_pcm_t *pcm), (pcm))
PROXY_STUB(snd_pcm_readi, snd_pcm_sframes_t,
           (snd_pcm_t *pcm, void *buffer, snd_pcm_uframes_t size),
           (pcm, buffer, size))
PROXY_STUB(snd_pcm_resume, int, (snd_pcm_t *pcm), (pcm))
PROXY_STUB(snd_pcm_set_chmap, int, (snd_pcm_t *pcm, snd_pcm_chmap_t const *map), (pcm, map))
PROXY_STUB(snd_pcm_state, snd_pcm_state_t, (snd_pcm_t *pcm), (pcm))
PROXY_STUB(snd_pcm_state_name, const char *, (snd_pcm_state_t state), (state))
PROXY_STUB(snd_pcm_writei, snd_pcm_sframes_t,
           (snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size),
           (pcm, buffer, size))
PROXY_STUB(snd_pcm_start, int, (snd_pcm_t *pcm), (pcm))

static int fallback_snd_pcm_avail_delay(snd_pcm_t *pcm, snd_pcm_sframes_t *availp, snd_pcm_sframes_t *delayp)
{
    *availp = pfn_snd_pcm_avail_update(pcm);
    int ret = pfn_snd_pcm_delay(pcm, delayp);
    if (ret >= 0 && *availp < 0)
        ret = (int)*availp;
    return ret;
}

static int fallback_snd_pcm_set_chmap(snd_pcm_t *pcm, snd_pcm_chmap_t const *map)
{
    RT_NOREF(pcm, map);
    return 0;
}

/*
 * HW
 */

PROXY_STUB(snd_pcm_hw_params, int,
           (snd_pcm_t *pcm, snd_pcm_hw_params_t *params),
           (pcm, params))
PROXY_STUB(snd_pcm_hw_params_any, int,
           (snd_pcm_t *pcm, snd_pcm_hw_params_t *params),
           (pcm, params))
PROXY_STUB(snd_pcm_hw_params_get_buffer_size, int,
           (const snd_pcm_hw_params_t *params, snd_pcm_uframes_t *val),
           (params, val))
PROXY_STUB(snd_pcm_hw_params_get_buffer_size_min, int,
           (const snd_pcm_hw_params_t *params, snd_pcm_uframes_t *val),
           (params, val))
PROXY_STUB(snd_pcm_hw_params_get_period_size, int,
           (const snd_pcm_hw_params_t *params, snd_pcm_uframes_t *frames, int *dir),
           (params, frames, dir))
PROXY_STUB(snd_pcm_hw_params_get_period_size_min, int,
           (const snd_pcm_hw_params_t *params, snd_pcm_uframes_t *frames, int *dir),
           (params, frames, dir))
PROXY_STUB(snd_pcm_hw_params_set_rate_near, int,
           (snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int *val, int *dir),
           (pcm, params, val, dir))
PROXY_STUB(snd_pcm_hw_params_set_access, int,
           (snd_pcm_t *pcm, snd_pcm_hw_params_t *params, snd_pcm_access_t _access),
           (pcm, params, _access))
PROXY_STUB(snd_pcm_hw_params_set_buffer_time_near, int,
           (snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int *val, int *dir),
           (pcm, params, val, dir))
PROXY_STUB(snd_pcm_hw_params_set_buffer_size_near, int,
           (snd_pcm_t *pcm, snd_pcm_hw_params_t *params, snd_pcm_uframes_t *val),
           (pcm, params, val))
PROXY_STUB(snd_pcm_hw_params_set_channels_near, int,
           (snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int *val),
           (pcm, params, val))
PROXY_STUB(snd_pcm_hw_params_set_period_size_near, int,
           (snd_pcm_t *pcm, snd_pcm_hw_params_t *params, snd_pcm_uframes_t *val, int *dir),
           (pcm, params, val, dir))
PROXY_STUB(snd_pcm_hw_params_set_period_time_near, int,
           (snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int *val, int *dir),
           (pcm, params, val, dir))
PROXY_STUB(snd_pcm_hw_params_sizeof, size_t, (void), ())
PROXY_STUB(snd_pcm_hw_params_set_format, int,
           (snd_pcm_t *pcm, snd_pcm_hw_params_t *params, snd_pcm_format_t val),
           (pcm, params, val))

/*
 * SW
 */

PROXY_STUB(snd_pcm_sw_params, int,
           (snd_pcm_t *pcm, snd_pcm_sw_params_t *params),
           (pcm, params))
PROXY_STUB(snd_pcm_sw_params_current, int,
           (snd_pcm_t *pcm, snd_pcm_sw_params_t *params),
           (pcm, params))
PROXY_STUB(snd_pcm_sw_params_get_start_threshold, int,
           (const snd_pcm_sw_params_t *params, snd_pcm_uframes_t *val),
           (params, val))
PROXY_STUB(snd_pcm_sw_params_set_avail_min, int,
           (snd_pcm_t *pcm, snd_pcm_sw_params_t *params, snd_pcm_uframes_t val),
           (pcm, params, val))
PROXY_STUB(snd_pcm_sw_params_set_start_threshold, int,
           (snd_pcm_t *pcm, snd_pcm_sw_params_t *params, snd_pcm_uframes_t val),
           (pcm, params, val))
PROXY_STUB(snd_pcm_sw_params_sizeof, size_t, (void), ())

/*
 * Mixer
 */

PROXY_STUB(snd_mixer_selem_id_sizeof, size_t,
           (void), ())
PROXY_STUB(snd_mixer_open, int,
           (snd_mixer_t **mixer, int mode),
           (mixer, mode))
PROXY_STUB(snd_mixer_attach, int,
           (snd_mixer_t *mixer, const char *name),
           (mixer, name))
PROXY_STUB(snd_mixer_close, int,
           (snd_mixer_t *mixer),
           (mixer))
PROXY_STUB(snd_mixer_selem_id_set_index, void,
           (snd_mixer_selem_id_t *obj, unsigned int val),
           (obj, val))
PROXY_STUB(snd_mixer_selem_id_set_name, void,
           (snd_mixer_selem_id_t *obj, const char *val),
           (obj, val))
PROXY_STUB(snd_mixer_selem_set_playback_volume, int,
           (snd_mixer_elem_t *elem, snd_mixer_selem_channel_id_t channel, long value),
           (elem, channel, value))
PROXY_STUB(snd_mixer_selem_get_playback_volume_range, int,
           (snd_mixer_elem_t *elem, long *min, long *max),
           (elem, min, max))
PROXY_STUB(snd_mixer_selem_set_capture_volume, int,
           (snd_mixer_elem_t *elem, snd_mixer_selem_channel_id_t channel, long value),
           (elem, channel, value))
PROXY_STUB(snd_mixer_selem_get_capture_volume_range, int,
           (snd_mixer_elem_t *elem, long *min, long *max),
           (elem, min, max))
PROXY_STUB(snd_mixer_selem_register, int,
           (snd_mixer_t *mixer, snd_mixer_selem_regopt *options, snd_mixer_class_t **classp),
           (mixer, options, classp))
PROXY_STUB(snd_mixer_load, int,
           (snd_mixer_t *mixer),
           (mixer))
PROXY_STUB(snd_mixer_find_selem, snd_mixer_elem_t *,
           (snd_mixer_t *mixer, const snd_mixer_selem_id_t *id),
           (mixer, id))

typedef struct
{
    const char *name;
    void (**pfn)(void);
    void (*pfnFallback)(void);
} SHARED_FUNC;

#define ELEMENT(function) { #function , (void (**)(void)) & pfn_ ## function, NULL }
#define ELEMENT_FALLBACK(function) { #function , (void (**)(void)) & pfn_ ## function, (void (*)(void))fallback_ ## function }
static SHARED_FUNC SharedFuncs[] =
{
    ELEMENT(snd_lib_error_set_handler),
    ELEMENT(snd_strerror),

    ELEMENT_FALLBACK(snd_device_name_hint),
    ELEMENT_FALLBACK(snd_device_name_get_hint),
    ELEMENT_FALLBACK(snd_device_name_free_hint),

    ELEMENT(snd_pcm_avail_update),
    ELEMENT_FALLBACK(snd_pcm_avail_delay),
    ELEMENT(snd_pcm_close),
    ELEMENT(snd_pcm_delay),
    ELEMENT(snd_pcm_drain),
    ELEMENT(snd_pcm_drop),
    ELEMENT(snd_pcm_nonblock),
    ELEMENT(snd_pcm_open),
    ELEMENT(snd_pcm_prepare),
    ELEMENT(snd_pcm_resume),
    ELEMENT_FALLBACK(snd_pcm_set_chmap),
    ELEMENT(snd_pcm_state),
    ELEMENT(snd_pcm_state_name),

    ELEMENT(snd_pcm_readi),
    ELEMENT(snd_pcm_start),
    ELEMENT(snd_pcm_writei),

    ELEMENT(snd_pcm_hw_params),
    ELEMENT(snd_pcm_hw_params_any),
    ELEMENT(snd_pcm_hw_params_sizeof),
    ELEMENT(snd_pcm_hw_params_get_buffer_size),
    ELEMENT(snd_pcm_hw_params_get_buffer_size_min),
    ELEMENT(snd_pcm_hw_params_get_period_size_min),
    ELEMENT(snd_pcm_hw_params_set_access),
    ELEMENT(snd_pcm_hw_params_set_buffer_size_near),
    ELEMENT(snd_pcm_hw_params_set_buffer_time_near),
    ELEMENT(snd_pcm_hw_params_set_channels_near),
    ELEMENT(snd_pcm_hw_params_set_format),
    ELEMENT(snd_pcm_hw_params_get_period_size),
    ELEMENT(snd_pcm_hw_params_set_period_size_near),
    ELEMENT(snd_pcm_hw_params_set_period_time_near),
    ELEMENT(snd_pcm_hw_params_set_rate_near),

    ELEMENT(snd_pcm_sw_params),
    ELEMENT(snd_pcm_sw_params_current),
    ELEMENT(snd_pcm_sw_params_get_start_threshold),
    ELEMENT(snd_pcm_sw_params_set_avail_min),
    ELEMENT(snd_pcm_sw_params_set_start_threshold),
    ELEMENT(snd_pcm_sw_params_sizeof),

    ELEMENT(snd_mixer_selem_id_sizeof),
    ELEMENT(snd_mixer_open),
    ELEMENT(snd_mixer_attach),
    ELEMENT(snd_mixer_close),
    ELEMENT(snd_mixer_selem_id_set_index),
    ELEMENT(snd_mixer_selem_id_set_name),
    ELEMENT(snd_mixer_selem_set_playback_volume),
    ELEMENT(snd_mixer_selem_get_playback_volume_range),
    ELEMENT(snd_mixer_selem_set_capture_volume),
    ELEMENT(snd_mixer_selem_get_capture_volume_range),
    ELEMENT(snd_mixer_selem_register),
    ELEMENT(snd_mixer_load),
    ELEMENT(snd_mixer_find_selem),

};
#undef ELEMENT

/** Init once. */
static RTONCE g_AlsaLibInitOnce = RTONCE_INITIALIZER;


/** @callback_method_impl{FNRTONCE} */
static DECLCALLBACK(int32_t) drvHostAudioAlsaLibInitOnce(void *pvUser)
{
    RT_NOREF(pvUser);
    LogFlowFunc(("\n"));

    RTLDRMOD hMod = NIL_RTLDRMOD;
    int rc = RTLdrLoadSystemEx(VBOX_ALSA_LIB, RTLDRLOAD_FLAGS_NO_UNLOAD, &hMod);
    if (RT_SUCCESS(rc))
    {
        for (uintptr_t i = 0; i < RT_ELEMENTS(SharedFuncs); i++)
        {
            rc = RTLdrGetSymbol(hMod, SharedFuncs[i].name, (void **)SharedFuncs[i].pfn);
            if (RT_SUCCESS(rc))
            { /* likely */ }
            else if (SharedFuncs[i].pfnFallback && rc == VERR_SYMBOL_NOT_FOUND)
                *SharedFuncs[i].pfn = SharedFuncs[i].pfnFallback;
            else
            {
                LogRelFunc(("Failed to load library %s: Getting symbol %s failed: %Rrc\n", VBOX_ALSA_LIB, SharedFuncs[i].name, rc));
                return rc;
            }
        }

        RTLdrClose(hMod);
    }
    else
        LogRelFunc(("Failed to load library %s (%Rrc)\n", VBOX_ALSA_LIB, rc));
    return rc;
}


/**
 * Try to dynamically load the ALSA libraries.
 *
 * @returns VBox status code.
 */
int audioLoadAlsaLib(void)
{
    LogFlowFunc(("\n"));
    return RTOnce(&g_AlsaLibInitOnce, drvHostAudioAlsaLibInitOnce, NULL);
}

