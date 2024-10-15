/* $Id: DrvHostAudioAlsaStubs.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
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

#ifndef VBOX_INCLUDED_SRC_Audio_DrvHostAudioAlsaStubs_h
#define VBOX_INCLUDED_SRC_Audio_DrvHostAudioAlsaStubs_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <alsa/version.h>

#define VBOX_ALSA_MAKE_VER(a,b,c)   ( ((a) << 24) | ((b) << 16) | (c) )
#define VBOX_ALSA_VER               VBOX_ALSA_MAKE_VER(SND_LIB_MAJOR, SND_LIB_MINOR, SND_LIB_SUBMINOR)

RT_C_DECLS_BEGIN
extern int audioLoadAlsaLib(void);

#if VBOX_ALSA_VER < VBOX_ALSA_MAKE_VER(1,0,18)   /* added in 1.0.18 */
extern int  snd_pcm_avail_delay(snd_pcm_t *, snd_pcm_sframes_t *, snd_pcm_sframes_t *);
#endif

#if VBOX_ALSA_VER < VBOX_ALSA_MAKE_VER(1,0,14)  /* added in 1.0.14a */
extern int  snd_device_name_hint(int, const char *, void ***);
extern int  snd_device_name_free_hint(void **);
extern char *snd_device_name_get_hint(const void *, const char *);
#endif

#if VBOX_ALSA_VER < VBOX_ALSA_MAKE_VER(1,0,27)  /* added in 1.0.27 */
enum snd_pcm_chmap_position { SND_CHMAP_UNKNOWN = 0, SND_CHMAP_NA, SND_CHMAP_MONO, SND_CHMAP_FL, SND_CHMAP_FR,
    SND_CHMAP_RL,  SND_CHMAP_RR,   SND_CHMAP_FC,   SND_CHMAP_LFE,  SND_CHMAP_SL,  SND_CHMAP_SR,  SND_CHMAP_RC,
    SND_CHMAP_FLC, SND_CHMAP_FRC,  SND_CHMAP_RLC,  SND_CHMAP_RRC,  SND_CHMAP_FLW, SND_CHMAP_FRW, SND_CHMAP_FLH,
    SND_CHMAP_FCH, SND_CHMAP_FRH,  SND_CHMAP_TC,   SND_CHMAP_TFL,  SND_CHMAP_TFR, SND_CHMAP_TFC, SND_CHMAP_TRL,
    SND_CHMAP_TRR, SND_CHMAP_TRC,  SND_CHMAP_TFLC, SND_CHMAP_TFRC, SND_CHMAP_TSL, SND_CHMAP_TSR, SND_CHMAP_LLFE,
    SND_CHMAP_RLFE, SND_CHMAP_BC,  SND_CHMAP_BLC,  SND_CHMAP_BRC };
typedef struct snd_pcm_chmap
{
    unsigned int channels;
    RT_GCC_EXTENSION
    unsigned int pos[RT_FLEXIBLE_ARRAY_IN_NESTED_UNION];
} snd_pcm_chmap_t;
extern int snd_pcm_set_chmap(snd_pcm_t *, snd_pcm_chmap_t const *);
#endif

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_Audio_DrvHostAudioAlsaStubs_h */

