/* $Id: DrvHostAudioAlsaStubsMangling.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Mangle libasound symbols.
 *
 * This is necessary on hosts which don't support the -fvisibility gcc switch.
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

#ifndef VBOX_INCLUDED_SRC_Audio_DrvHostAudioAlsaStubsMangling_h
#define VBOX_INCLUDED_SRC_Audio_DrvHostAudioAlsaStubsMangling_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#define ALSA_MANGLER(symbol) VBox_##symbol

#define snd_lib_error_set_handler                 ALSA_MANGLER(snd_lib_error_set_handler)
#define snd_strerror                              ALSA_MANGLER(snd_strerror)

#define snd_device_name_hint                      ALSA_MANGLER(snd_device_name_hint)
#define snd_device_name_get_hint                  ALSA_MANGLER(snd_device_name_get_hint)
#define snd_device_name_free_hint                 ALSA_MANGLER(snd_device_name_free_hint)

#define snd_pcm_avail_update                      ALSA_MANGLER(snd_pcm_avail_update)
#define snd_pcm_close                             ALSA_MANGLER(snd_pcm_close)
#define snd_pcm_avail_delay                       ALSA_MANGLER(snd_pcm_avail_delay)
#define snd_pcm_delay                             ALSA_MANGLER(snd_pcm_delay)
#define snd_pcm_drain                             ALSA_MANGLER(snd_pcm_drain)
#define snd_pcm_drop                              ALSA_MANGLER(snd_pcm_drop)
#define snd_pcm_nonblock                          ALSA_MANGLER(snd_pcm_nonblock)
#define snd_pcm_open                              ALSA_MANGLER(snd_pcm_open)
#define snd_pcm_prepare                           ALSA_MANGLER(snd_pcm_prepare)
#define snd_pcm_readi                             ALSA_MANGLER(snd_pcm_readi)
#define snd_pcm_resume                            ALSA_MANGLER(snd_pcm_resume)
#define snd_pcm_set_chmap                         ALSA_MANGLER(snd_pcm_set_chmap)
#define snd_pcm_start                             ALSA_MANGLER(snd_pcm_start)
#define snd_pcm_state                             ALSA_MANGLER(snd_pcm_state)
#define snd_pcm_state_name                        ALSA_MANGLER(snd_pcm_state_name)
#define snd_pcm_writei                            ALSA_MANGLER(snd_pcm_writei)

#define snd_pcm_hw_params                         ALSA_MANGLER(snd_pcm_hw_params)
#define snd_pcm_hw_params_any                     ALSA_MANGLER(snd_pcm_hw_params_any)
#define snd_pcm_hw_params_sizeof                  ALSA_MANGLER(snd_pcm_hw_params_sizeof)
#define snd_pcm_hw_params_get_buffer_size         ALSA_MANGLER(snd_pcm_hw_params_get_buffer_size)
#define snd_pcm_hw_params_get_period_size_min     ALSA_MANGLER(snd_pcm_hw_params_get_period_size_min)
#define snd_pcm_hw_params_set_rate_near           ALSA_MANGLER(snd_pcm_hw_params_set_rate_near)
#define snd_pcm_hw_params_set_access              ALSA_MANGLER(snd_pcm_hw_params_set_access)
#define snd_pcm_hw_params_set_buffer_time_near    ALSA_MANGLER(snd_pcm_hw_params_set_buffer_time_near)
#define snd_pcm_hw_params_set_buffer_size_near    ALSA_MANGLER(snd_pcm_hw_params_set_buffer_size_near)
#define snd_pcm_hw_params_get_buffer_size_min     ALSA_MANGLER(snd_pcm_hw_params_get_buffer_size_min)
#define snd_pcm_hw_params_set_channels_near       ALSA_MANGLER(snd_pcm_hw_params_set_channels_near)
#define snd_pcm_hw_params_set_format              ALSA_MANGLER(snd_pcm_hw_params_set_format)
#define snd_pcm_hw_params_get_period_size         ALSA_MANGLER(snd_pcm_hw_params_get_period_size)
#define snd_pcm_hw_params_set_period_size_near    ALSA_MANGLER(snd_pcm_hw_params_set_period_size_near)
#define snd_pcm_hw_params_set_period_time_near    ALSA_MANGLER(snd_pcm_hw_params_set_period_time_near)

#define snd_pcm_sw_params                         ALSA_MANGLER(snd_pcm_sw_params)
#define snd_pcm_sw_params_current                 ALSA_MANGLER(snd_pcm_sw_params_current)
#define snd_pcm_sw_params_get_start_threshold     ALSA_MANGLER(snd_pcm_sw_params_get_start_threshold)
#define snd_pcm_sw_params_set_avail_min           ALSA_MANGLER(snd_pcm_sw_params_set_avail_min)
#define snd_pcm_sw_params_set_start_threshold     ALSA_MANGLER(snd_pcm_sw_params_set_start_threshold)
#define snd_pcm_sw_params_sizeof                  ALSA_MANGLER(snd_pcm_sw_params_sizeof)

#define snd_mixer_selem_id_sizeof                 ALSA_MANGLER(snd_mixer_selem_id_sizeof)
#define snd_mixer_open                            ALSA_MANGLER(snd_mixer_open)
#define snd_mixer_attach                          ALSA_MANGLER(snd_mixer_attach)
#define snd_mixer_close                           ALSA_MANGLER(snd_mixer_close)
#define snd_mixer_selem_id_set_index              ALSA_MANGLER(snd_mixer_selem_id_set_index)
#define snd_mixer_selem_id_set_name               ALSA_MANGLER(snd_mixer_selem_id_set_name)
#define snd_mixer_selem_set_playback_volume       ALSA_MANGLER(snd_mixer_selem_set_playback_volume)
#define snd_mixer_selem_get_playback_volume_range ALSA_MANGLER(snd_mixer_selem_get_playback_volume_range)
#define snd_mixer_selem_set_capture_volume        ALSA_MANGLER(snd_mixer_selem_set_capture_volume)
#define snd_mixer_selem_get_capture_volume_range  ALSA_MANGLER(snd_mixer_selem_get_capture_volume_range)
#define snd_mixer_selem_register                  ALSA_MANGLER(snd_mixer_selem_register)
#define snd_mixer_load                            ALSA_MANGLER(snd_mixer_load)
#define snd_mixer_find_selem                      ALSA_MANGLER(snd_mixer_find_selem)

#endif /* !VBOX_INCLUDED_SRC_Audio_DrvHostAudioAlsaStubsMangling_h */
