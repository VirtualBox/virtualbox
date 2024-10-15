/* $Id: PciInline.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PCI - The PCI Controller And Devices, inline device helpers.
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

#ifndef VBOX_INCLUDED_SRC_Bus_PciInline_h
#define VBOX_INCLUDED_SRC_Bus_PciInline_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

DECLINLINE(void) pciDevSetPci2PciBridge(PPDMPCIDEV pDev)
{
    pDev->Int.s.fFlags |= PCIDEV_FLAG_PCI_TO_PCI_BRIDGE;
}

DECLINLINE(bool) pciDevIsPci2PciBridge(PPDMPCIDEV pDev)
{
    return (pDev->Int.s.fFlags & PCIDEV_FLAG_PCI_TO_PCI_BRIDGE) != 0;
}

DECLINLINE(void) pciDevSetPciExpress(PPDMPCIDEV pDev)
{
    pDev->Int.s.fFlags |= PCIDEV_FLAG_PCI_EXPRESS_DEVICE;
}

DECLINLINE(bool) pciDevIsPciExpress(PPDMPCIDEV pDev)
{
    return (pDev->Int.s.fFlags & PCIDEV_FLAG_PCI_EXPRESS_DEVICE) != 0;
}

DECLINLINE(void) pciDevSetMsiCapable(PPDMPCIDEV pDev)
{
    pDev->Int.s.fFlags |= PCIDEV_FLAG_MSI_CAPABLE;
}

DECLINLINE(void) pciDevClearMsiCapable(PPDMPCIDEV pDev)
{
    pDev->Int.s.fFlags &= ~PCIDEV_FLAG_MSI_CAPABLE;
}

DECLINLINE(bool) pciDevIsMsiCapable(PPDMPCIDEV pDev)
{
    return (pDev->Int.s.fFlags & PCIDEV_FLAG_MSI_CAPABLE) != 0;
}

DECLINLINE(void) pciDevSetMsi64Capable(PPDMPCIDEV pDev)
{
    pDev->Int.s.fFlags |= PCIDEV_FLAG_MSI64_CAPABLE;
}

DECLINLINE(void) pciDevClearMsi64Capable(PPDMPCIDEV pDev)
{
    pDev->Int.s.fFlags &= ~PCIDEV_FLAG_MSI64_CAPABLE;
}

DECLINLINE(bool) pciDevIsMsi64Capable(PPDMPCIDEV pDev)
{
    return (pDev->Int.s.fFlags & PCIDEV_FLAG_MSI64_CAPABLE) != 0;
}

DECLINLINE(void) pciDevSetMsixCapable(PPDMPCIDEV pDev)
{
    pDev->Int.s.fFlags |= PCIDEV_FLAG_MSIX_CAPABLE;
}

DECLINLINE(void) pciDevClearMsixCapable(PPDMPCIDEV pDev)
{
    pDev->Int.s.fFlags &= ~PCIDEV_FLAG_MSIX_CAPABLE;
}

DECLINLINE(bool) pciDevIsMsixCapable(PPDMPCIDEV pDev)
{
    return (pDev->Int.s.fFlags & PCIDEV_FLAG_MSIX_CAPABLE) != 0;
}

DECLINLINE(void) pciDevSetPassthrough(PPDMPCIDEV pDev)
{
    pDev->Int.s.fFlags |= PCIDEV_FLAG_PASSTHROUGH;
}

DECLINLINE(void) pciDevClearPassthrough(PPDMPCIDEV pDev)
{
    pDev->Int.s.fFlags &= ~PCIDEV_FLAG_PASSTHROUGH;
}

DECLINLINE(bool) pciDevIsPassthrough(PPDMPCIDEV pDev)
{
    return (pDev->Int.s.fFlags & PCIDEV_FLAG_PASSTHROUGH) != 0;
}

#endif /* !VBOX_INCLUDED_SRC_Bus_PciInline_h */

