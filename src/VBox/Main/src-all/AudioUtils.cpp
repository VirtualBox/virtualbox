/* $Id: AudioUtils.cpp 110356 2025-07-22 17:20:38Z andreas.loeffler@oracle.com $ */
/** @file
 * VirtualBox audio utility functions for Main.
 */

/*
 * Copyright (C) 2025 Oracle and/or its affiliates.
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

#include <iprt/ldr.h>
#include <iprt/process.h>
#include <iprt/system.h>

#include "AudioUtils.h"


/**
 * Returns the AudioDriverType_* which should be used by default on this
 * host platform. On Linux, this will check at runtime whether PulseAudio
 * or ALSA are actually supported on the first call.
 *
 * When more than one supported audio stack is available, choose the most suited
 * (probably newest in most cases) one.
 *
 * @return Default audio driver type for this host platform.
 */
AudioDriverType_T VBoxAudioGetDefaultDriver(void)
{
#if defined(RT_OS_WINDOWS)
    if (RTSystemGetNtVersion() >= RTSYSTEM_MAKE_NT_VERSION(6,1,0))
        return AudioDriverType_WAS;
    return AudioDriverType_DirectSound;

#elif defined(RT_OS_LINUX)
    /* On Linux, we need to check at runtime what's actually supported.
     * Descending precedence. */
    static RTCLockMtx s_mtx;
    static AudioDriverType_T s_enmLinuxDriver = AudioDriverType_Null;
    RTCLock lock(s_mtx);
    if (s_enmLinuxDriver == AudioDriverType_Null) /* Already determined from a former run? */
    {
# ifdef VBOX_WITH_AUDIO_PULSE
        /* Check for the pulse library & that the PulseAudio daemon is running. */
        if (   (   RTProcIsRunningByName("pulseaudio")
                /* We also use the PulseAudio backend when we find pipewire-pulse running, which
                 * acts as a PulseAudio-compatible daemon for Pipewire-enabled applications. See @ticketref{21575} */
                || RTProcIsRunningByName("pipewire-pulse"))
            && RTLdrIsLoadable("libpulse.so.0"))
        {
            s_enmLinuxDriver = AudioDriverType_Pulse;
        }
#endif /* VBOX_WITH_AUDIO_PULSE */

# ifdef VBOX_WITH_AUDIO_ALSA
        if (s_enmLinuxDriver == AudioDriverType_Null)
        {
            /* Check if we can load the ALSA library */
            if (RTLdrIsLoadable("libasound.so.2"))
                s_enmLinuxDriver = AudioDriverType_ALSA;
        }
# endif /* VBOX_WITH_AUDIO_ALSA */

# ifdef VBOX_WITH_AUDIO_OSS
        if (s_enmLinuxDriver == AudioDriverType_Null)
            s_enmLinuxDriver = AudioDriverType_OSS;
# endif /* VBOX_WITH_AUDIO_OSS */
    }
    return s_enmLinuxDriver;

#elif defined(RT_OS_DARWIN)
    return AudioDriverType_CoreAudio;

#elif defined(RT_OS_OS2)
    return AudioDriverType_MMPM;

#else /* All other platforms. */
# ifdef VBOX_WITH_AUDIO_OSS
    return AudioDriverType_OSS;
# else
    /* Return NULL driver as a fallback if nothing of the above is available. */
    return AudioDriverType_Null;
# endif
#endif
}
