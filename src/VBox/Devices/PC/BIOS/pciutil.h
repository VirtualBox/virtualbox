/* $Id: pciutil.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Utility routines for calling the PCI BIOS.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_PC_BIOS_pciutil_h
#define VBOX_INCLUDED_SRC_PC_BIOS_pciutil_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

extern  uint16_t    pci_find_device(uint16_t v_id, uint16_t d_id);
/* Warning: pci_find_classcode destroys the high bits of ECX. */
extern  uint16_t    pci_find_classcode(uint32_t dev_class);
extern  uint32_t    pci_read_config_byte(uint8_t bus, uint8_t dev_fn, uint8_t reg);
extern  uint32_t    pci_read_config_word(uint8_t bus, uint8_t dev_fn, uint8_t reg);
/* Warning: pci_read_config_dword destroys the high bits of ECX. */
extern  uint32_t    pci_read_config_dword(uint8_t bus, uint8_t dev_fn, uint8_t reg);
extern  void        pci_write_config_byte(uint8_t bus, uint8_t dev_fn, uint8_t reg, uint8_t val);
extern  void        pci_write_config_word(uint8_t bus, uint8_t dev_fn, uint8_t reg, uint16_t val);
/* Warning: pci_write_config_dword destroys the high bits of ECX. */
extern  void        pci_write_config_dword(uint8_t bus, uint8_t dev_fn, uint8_t reg, uint32_t val);
extern  uint16_t    pci_find_class_noif(uint16_t dev_class);

#endif /* !VBOX_INCLUDED_SRC_PC_BIOS_pciutil_h */

