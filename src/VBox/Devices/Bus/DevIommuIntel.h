/* $Id: DevIommuIntel.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevIommuIntel - I/O Memory Management Unit (Intel), header shared with the IOMMU, ACPI, chipset/firmware code.
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_Bus_DevIommuIntel_h
#define VBOX_INCLUDED_SRC_Bus_DevIommuIntel_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/** Intel vendor ID for the DMAR unit. */
#define DMAR_PCI_VENDOR_ID                          0x8086
/** VirtualBox DMAR unit's device ID. */
#define DMAR_PCI_DEVICE_ID                          0xc0de
/** VirtualBox DMAR unit's device revision ID. */
#define DMAR_PCI_REVISION_ID                        0x01

/** Feature/capability flags exposed to the guest. */
#define DMAR_ACPI_DMAR_FLAGS                        ACPI_DMAR_F_INTR_REMAP

/** The MMIO base address of the DMAR unit (taken from real hardware). */
#define DMAR_MMIO_BASE_PHYSADDR                     UINT64_C(0xfed90000)
/** The size of the MMIO region (in bytes). */
#define DMAR_MMIO_SIZE                              4096

#endif /* !VBOX_INCLUDED_SRC_Bus_DevIommuIntel_h */
