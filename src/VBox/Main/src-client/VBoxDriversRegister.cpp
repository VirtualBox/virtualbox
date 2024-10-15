/* $Id: VBoxDriversRegister.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 *
 * Main driver registration.
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
#define LOG_GROUP LOG_GROUP_MAIN
#include "LoggingNew.h"

#include "MouseImpl.h"
#include "KeyboardImpl.h"
#include "DisplayImpl.h"
#include "VMMDev.h"
#include "NvramStoreImpl.h"
#ifdef VBOX_WITH_AUDIO_VRDE
# include "DrvAudioVRDE.h"
#endif
#ifdef VBOX_WITH_AUDIO_RECORDING
# include "DrvAudioRec.h"
#endif
#include "UsbWebcamInterface.h"
#ifdef VBOX_WITH_USB_CARDREADER
# include "UsbCardReader.h"
#endif
#include "ConsoleImpl.h"
#ifdef VBOX_WITH_PCI_PASSTHROUGH
# include "PCIRawDevImpl.h"
#endif
#ifdef VBOX_WITH_VIRT_ARMV8
# include "ResourceStoreImpl.h"
#endif

#include <VBox/vmm/pdmdrv.h>
#include <VBox/version.h>


/**
 * Register the main drivers.
 *
 * @returns VBox status code.
 * @param   pCallbacks      Pointer to the callback table.
 * @param   u32Version      VBox version number.
 */
extern "C" DECLEXPORT(int) VBoxDriversRegister(PCPDMDRVREGCB pCallbacks, uint32_t u32Version)
{
    LogFlow(("VBoxDriversRegister: u32Version=%#x\n", u32Version));
    AssertReleaseMsg(u32Version == VBOX_VERSION, ("u32Version=%#x VBOX_VERSION=%#x\n", u32Version, VBOX_VERSION));

    int vrc = pCallbacks->pfnRegister(pCallbacks, &Mouse::DrvReg);
    if (RT_FAILURE(vrc))
        return vrc;

    vrc = pCallbacks->pfnRegister(pCallbacks, &Keyboard::DrvReg);
    if (RT_FAILURE(vrc))
        return vrc;

    vrc = pCallbacks->pfnRegister(pCallbacks, &Display::DrvReg);
    if (RT_FAILURE(vrc))
        return vrc;

    vrc = pCallbacks->pfnRegister(pCallbacks, &VMMDev::DrvReg);
    if (RT_FAILURE(vrc))
        return vrc;
#ifdef VBOX_WITH_AUDIO_VRDE
    vrc = pCallbacks->pfnRegister(pCallbacks, &AudioVRDE::DrvReg);
    if (RT_FAILURE(vrc))
        return vrc;
#endif
#ifdef VBOX_WITH_AUDIO_RECORDING
    vrc = pCallbacks->pfnRegister(pCallbacks, &AudioVideoRec::DrvReg);
    if (RT_FAILURE(vrc))
        return vrc;
#endif

    vrc = pCallbacks->pfnRegister(pCallbacks, &EmWebcam::DrvReg);
    if (RT_FAILURE(vrc))
        return vrc;

#ifdef VBOX_WITH_USB_CARDREADER
    vrc = pCallbacks->pfnRegister(pCallbacks, &UsbCardReader::DrvReg);
    if (RT_FAILURE(vrc))
        return vrc;
#endif

    vrc = pCallbacks->pfnRegister(pCallbacks, &Console::DrvStatusReg);
    if (RT_FAILURE(vrc))
        return vrc;

#ifdef VBOX_WITH_PCI_PASSTHROUGH
    vrc = pCallbacks->pfnRegister(pCallbacks, &PCIRawDev::DrvReg);
    if (RT_FAILURE(vrc))
        return vrc;
#endif

    vrc = pCallbacks->pfnRegister(pCallbacks, &NvramStore::DrvReg);
    if (RT_FAILURE(vrc))
        return vrc;

#ifdef VBOX_WITH_VIRT_ARMV8
    vrc = pCallbacks->pfnRegister(pCallbacks, &ResourceStore::DrvReg);
    if (RT_FAILURE(vrc))
        return vrc;
#endif

    return VINF_SUCCESS;
}
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
