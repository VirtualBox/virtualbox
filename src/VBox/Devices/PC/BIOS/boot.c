/* $Id: boot.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PC BIOS - ???
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
 * --------------------------------------------------------------------
 *
 * This code is based on:
 *
 *  ROM BIOS for use with Bochs/Plex86/QEMU emulation environment
 *
 *  Copyright (C) 2002  MandrakeSoft S.A.
 *
 *    MandrakeSoft S.A.
 *    43, rue d'Aboukir
 *    75002 Paris - France
 *    http://www.linux-mandrake.com/
 *    http://www.mandrakesoft.com/
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

/*
 * Oracle LGPL Disclaimer: For the avoidance of doubt, except that if any license choice
 * other than GPL or LGPL is available it will apply instead, Oracle elects to use only
 * the Lesser General Public License version 2.1 (LGPLv2) at this time for any software where
 * a choice of LGPL license versions is made available with the language indicating
 * that LGPLv2 or any later version may be used, or where a choice of which version
 * of the LGPL is applied is otherwise unspecified.
 */


#include <stdint.h>
#include <string.h>
#include "inlines.h"
#include "biosint.h"
#include "ebda.h"

/* Sanity check the LAN boot segment definition. */
#if VBOX_LANBOOT_SEG < 0xA000
#error VBOX_LANBOOT_SEG incorrect!
#endif

/* PnP header used with LAN boot ROMs. */
typedef struct {
    uint32_t    sig;
    uint8_t     revision;
    uint8_t     length;
    uint16_t    next_s;
    uint8_t     pad1;
    uint8_t     checksum;
    uint32_t    dev_id;
    uint16_t    mfg_string;
    uint16_t    prod_string;
    uint8_t     base_class;
    uint8_t     subclass;
    uint8_t     interface;
    uint8_t     dev_ind;
    uint16_t    boot_code;
    uint16_t    dv;
    uint16_t    bev;
    uint16_t    pad2;
    uint16_t    sriv;
} pnp_exp_t;


int read_boot_sec(uint8_t bootdrv, uint16_t segment);
#pragma aux read_boot_sec = \
    "mov ax,0201h"          \
    "mov dh,0"              \
    "mov cx,1"              \
    "xor bx,bx"             \
    "int 13h"               \
    "mov ax,0"              \
    "sbb ax,0"              \
    parm [dl] [es] modify [ax bx cx dx];

//--------------------------------------------------------------------------
// print_boot_device
//   displays the boot device
//--------------------------------------------------------------------------

static const char drivetypes[][10]={"Floppy","Hard Disk","CD-ROM","LAN"};

/// @todo pass inputs as bit flags rather than bytes?
void print_boot_device(uint8_t cdboot, uint8_t lanboot, uint8_t drive)
{
    int     i;

    // cdboot contains 0 if lan/floppy/harddisk, 1 otherwise
    // lanboot contains 0 if floppy/harddisk, 1 otherwise
    // drive contains real/emulated boot drive

    if(cdboot)i=2;                    // CD-Rom
    else if(lanboot)i=3;              // LAN
    else if((drive&0x0080)==0x00)i=0; // Floppy
    else if((drive&0x0080)==0x80)i=1; // Hard drive
    else return;

    BX_INFO("Booting from %s...\n",drivetypes[i]);
}

//--------------------------------------------------------------------------
// print_boot_failure
//   displays the reason why boot failed
//--------------------------------------------------------------------------
/// @todo pass inputs as bit flags rather than bytes?
void print_boot_failure(uint8_t cdboot, uint8_t lanboot, uint8_t drive,
                        uint8_t reason, uint8_t lastdrive)
{
    uint16_t    drivenum = drive&0x7f;

    // cdboot: 1 if boot from cd, 0 otherwise
    // lanboot: 1 if boot from lan, 0 otherwise
    // drive : drive number
    // reason: 0 signature check failed, 1 read error
    // lastdrive: 1 boot drive is the last one in boot sequence

    if (cdboot)
        BX_INFO("Boot from %s failed\n",drivetypes[2]);
    else if (lanboot)
        BX_INFO("Boot from %s failed\n",drivetypes[3]);
    else if (drive & 0x80)
        BX_INFO("Boot from %s %d failed\n", drivetypes[1],drivenum);
    else
        BX_INFO("Boot from %s %d failed\n", drivetypes[0],drivenum);

    if (lastdrive==1) {
        if (reason==0)
            BX_INFO_CON("No bootable medium found!\n");
        else
            BX_INFO_CON("Could not read from the boot medium!\n");
        BX_INFO_CON("Please insert a bootable medium and reboot.\n");
    }
}

//--------------------------------------------------------------------------
// print_cdromboot_failure
//   displays the reason why boot failed
//--------------------------------------------------------------------------
void print_cdromboot_failure(uint16_t code)
{
    BX_INFO("CDROM boot failure code : %04x\n",code);
    return;
}


#define BOOT_CHK_WORDS  8

/* Check if the first n words of a boot sector
 * are identical. Only consider the boot sector
 * valid if they're not.
 */
static int valid_bootsect(uint16_t __far *boot)
{
    int         i;
    uint16_t    word1;

    word1 = boot[0];
    for (i = 1; i < BOOT_CHK_WORDS; ++i) {
        if (word1 != boot[i])
            break;
    }

    return i != BOOT_CHK_WORDS;
}

// returns bootsegment in ax, drive in bl
uint32_t BIOSCALL int19_function(uint8_t bseqnr)
{
    /// @todo common code for getting the EBDA segment
    uint16_t    ebda_seg=read_word(0x0040,0x000E);
    uint16_t    bootseq;
    uint8_t     bootdrv;
    uint8_t     bootcd;
    uint8_t     bootlan;
    uint8_t     bootchk;
    uint16_t    bootseg;
    uint16_t    status;
    uint8_t     lastdrive=0;

    // if BX_ELTORITO_BOOT is not defined, old behavior
    //   check bit 5 in CMOS reg 0x2d.  load either 0x00 or 0x80 into DL
    //   in preparation for the initial INT 13h (0=floppy A:, 0x80=C:)
    //     0: system boot sequence, first drive C: then A:
    //     1: system boot sequence, first drive A: then C:
    // else BX_ELTORITO_BOOT is defined
    //   CMOS regs 0x3D and 0x38 contain the boot sequence:
    //     CMOS reg 0x3D & 0x0f : 1st boot device
    //     CMOS reg 0x3D & 0xf0 : 2nd boot device
    //     CMOS reg 0x38 & 0xf0 : 3rd boot device
    //     CMOS reg 0x3C & 0x0f : 4th boot device
    //   boot device codes:
    //     0x00 : not defined
    //     0x01 : first floppy
    //     0x02 : first harddrive
    //     0x03 : first cdrom
    //     0x04 : local area network
    //     else : boot failure

    // Get the boot sequence
#if BX_ELTORITO_BOOT
    bootseq=inb_cmos(0x3d);
    bootseq|=((inb_cmos(0x38) & 0xf0) << 4);
    bootseq|=((inb_cmos(0x3c) & 0x0f) << 12);
    if (read_byte(ebda_seg, (uint16_t)&EbdaData->uForceBootDevice))
        bootseq = read_byte(ebda_seg, (uint16_t)&EbdaData->uForceBootDevice);
    /* Boot delay hack. */
    if (bseqnr == 1)
        delay_boot((inb_cmos(0x3c) & 0xf0) >> 4); /* Implemented in logo.c */

    if (bseqnr==2) bootseq >>= 4;
    if (bseqnr==3) bootseq >>= 8;
    if (bseqnr==4) bootseq >>= 12;
    if (bootseq<0x10) lastdrive = 1;
    bootdrv=0x00; bootcd=0;
    bootlan=0;
    BX_INFO("Boot : bseqnr=%d, bootseq=%x\r\n",bseqnr, bootseq);

    switch(bootseq & 0x0f) {
    case 0x01:
        bootdrv=0x00;
        bootcd=0;
        break;
    case 0x02:
    {
        // Get the Boot drive.
        uint8_t boot_drive = read_byte(ebda_seg, (uint16_t)&EbdaData->uForceBootDrive);

        bootdrv = boot_drive + 0x80;
        bootcd=0;
        break;
    }
    case 0x03:
        bootdrv=0x00;
        bootcd=1;
        break;
    case 0x04: bootlan=1; break;
    default:   return 0x00000000;
    }
#else
    bootseq=inb_cmos(0x2d);

    if (bseqnr==2) {
        bootseq ^= 0x20;
        lastdrive = 1;
    }
    bootdrv=0x00; bootcd=0;
    if((bootseq&0x20)==0) bootdrv=0x80;
#endif // BX_ELTORITO_BOOT

#if BX_ELTORITO_BOOT
    // We have to boot from cd
    if (bootcd != 0) {
        status = cdrom_boot();

        // If failure
        if ( (status & 0x00ff) !=0 ) {
            print_cdromboot_failure(status);
            print_boot_failure(bootcd, bootlan, bootdrv, 1, lastdrive);
            return 0x00000000;
        }

        bootseg = read_word(ebda_seg,(uint16_t)&EbdaData->cdemu.load_segment);
        bootdrv = (uint8_t)(status>>8);
    }

#endif // BX_ELTORITO_BOOT

    // Check for boot from LAN first
    if (bootlan == 1) {
        uint8_t __far   *fplan;

        fplan = MK_FP(VBOX_LANBOOT_SEG, 0);
        if (*(uint16_t __far *)fplan == 0xaa55) {
            pnp_exp_t __far *pnps;
            uint32_t        manuf;
            void    (__far  *netboot_entry)(void);

            // This is NOT a generic PnP implementation, but an Etherboot-specific hack.
            pnps = (void __far *)(fplan + *(uint16_t __far *)(fplan + 0x1a));
            if (pnps->sig == 0x506e5024/* '$PnP' */) {
                // Found PnP signature
                manuf = *(uint32_t __far *)(fplan + pnps->mfg_string);
                if (manuf == 0x65687445/* 'Ethe' */) {
                    // Found Etherboot ROM
                    print_boot_device(bootcd, bootlan, bootdrv);
                    netboot_entry = (void __far *)(fplan + 6);
                    netboot_entry();
                }
                else
                {
                    //Found Normal Pnp ROM
                    print_boot_device(bootcd, bootlan, bootdrv);
                    int_enable();   /* Disabled as we were invoked via INT instruction. */
                    netboot_entry = (void __far *)(fplan + pnps->bev);
                    netboot_entry();
                }
            }
        }

        // boot from LAN will not return if successful.
        print_boot_failure(bootcd, bootlan, bootdrv, 1, lastdrive);
        return 0x00000000;
    }

    // We have to boot from harddisk or floppy
    if (bootcd == 0 && bootlan == 0) {
        bootseg=0x07c0;

        status = read_boot_sec(bootdrv,bootseg);
        if (status != 0) {
            print_boot_failure(bootcd, bootlan, bootdrv, 1, lastdrive);
            return 0x00000000;
        }
    }

    // There is *no* requirement whatsoever for a valid floppy boot sector
    // to have a 55AAh signature. UNIX boot floppies typically have no such
    // signature. In general, it is impossible to tell a valid bootsector
    // from an invalid one.
    // NB: It is somewhat common for failed OS installs to have the
    // 0x55AA signature and a valid partition table but zeros in the
    // rest of the boot sector. We check the first few words; if identical,
    // the boot sector is extremely unlikely to be valid.
    if (bootdrv != 0) bootchk = 0;
    else bootchk = 1; /* disable 0x55AA signature check on drive A: */

#if BX_ELTORITO_BOOT
    // if boot from cd, no signature check
    if (bootcd != 0)
        bootchk = 1;
#endif // BX_ELTORITO_BOOT

    if (!valid_bootsect(MK_FP(bootseg,0))
      || (bootchk == 0 && read_word(bootseg,0x1fe) != 0xaa55))
    {
        print_boot_failure(bootcd, bootlan, bootdrv, 0, lastdrive);
        return 0x00000000;
    }

#if BX_ELTORITO_BOOT
    // Print out the boot string
    print_boot_device(bootcd, bootlan, bootdrv);
#else // BX_ELTORITO_BOOT
    print_boot_device(0, bootlan, bootdrv);
#endif // BX_ELTORITO_BOOT

    // return the boot segment
    return (((uint32_t)bootdrv) << 16) + bootseg;
}

