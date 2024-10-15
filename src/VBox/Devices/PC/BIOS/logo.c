/* $Id: logo.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Stuff for drawing the BIOS logo.
 */

/*
 * Copyright (C) 2004-2024 Oracle and/or its affiliates.
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

#include <stdint.h>
#include "biosint.h"
#include "inlines.h"
#include "ebda.h"

#define WAIT_HZ              64
#define WAIT_MS              16

#define F12_SCAN_CODE        0x86
#define F12_WAIT_TIME        (3 * WAIT_HZ)   /* 3 seconds. Used only if logo disabled. */

#include <VBox/bioslogo.h>

/**
 * Set video mode (VGA).
 * @param   mode    New video mode.
 */
void set_mode(uint8_t mode);
#pragma aux set_mode =      \
    "mov    ah, 0"          \
    "int    10h"            \
    parm [al] modify [ax] nomemory;


/**
 * Set VESA video mode.
 * @param   mode    New video mode.
 */
uint16_t vesa_set_mode(uint16_t mode);
#pragma aux vesa_set_mode = \
    "mov    ax, 4F02h"      \
    "int    10h"            \
    parm [bx] modify [ax] nomemory;

/**
 * Get current VESA video mode.
 * @param   mode    New video mode.
 */
uint16_t vesa_get_mode(uint16_t __far *mode);
#pragma aux vesa_get_mode = \
    "mov    ax, 4F03h"      \
    "int    10h"            \
    "mov    es:[di], bx"    \
    parm [es di] modify [ax bx] nomemory;


/**
 * Set custom video mode.
 * @param   xres    Requested width
 * @param   yres    Requested height
 * @param   bpp     Requested bits per pixel
 */
uint16_t custom_set_mode(uint16_t xres, uint16_t yres, uint8_t bpp);
#pragma aux custom_set_mode = \
    "mov    ax, 5642h"      \
    "mov    bl, 0"          \
    "int    10h"            \
    parm [cx] [dx] [bh] modify [ax] nomemory;

/**
 * Check for keystroke.
 * @returns    True if keystroke available, False if not.
 */
/// @todo INT 16h should already be returning the right value in al; could also use setz
uint8_t check_for_keystroke(void);
#pragma aux check_for_keystroke =   \
    "mov    ax, 100h"               \
    "int    16h"                    \
    "jz     no_key"                 \
    "mov    al, 1"                  \
    "jmp    done"                   \
    "no_key:"                       \
    "xor    al, al"                 \
    "done:"                         \
    modify [ax] nomemory;


/**
 * Get keystroke.
 * @returns    BIOS scan code.
 */
uint8_t get_keystroke(void);
#pragma aux get_keystroke = \
    "xor    ax, ax"         \
    "int    16h"            \
    "xchg   ah, al"         \
    modify [ax] nomemory;


/// @todo This whole business with reprogramming the PIT is rather suspect.
// The BIOS already has waiting facilities in INT 15h (fn 83h, 86h) which
// should be utilized instead.

// Set the timer to 16ms ticks (64K / (Hz / (PIT_HZ / 64K)) = count).
void wait_init(void);
#pragma aux wait_init = \
    "mov    al, 34h"    \
    "out    43h, al"    \
    "mov    al, 0D3h"   \
    "out    40h, al"    \
    "mov    al, 048h"   \
    "out    40h, al"    \
    modify [ax] nomemory;

/// @todo using this private interface is not great
extern void rtc_post(void);
#pragma aux rtc_post "*";

/* Restore the timer to the default 18.2Hz. Reinitialize the tick
 * and rollover counts since we've screwed them up by running the
 * timer at WAIT_HZ for a while.
 */
void wait_uninit(void);
#if VBOX_BIOS_CPU >= 80386
# pragma aux wait_uninit =   \
    ".386"                  \
    "mov    al, 34h"        \
    "out    43h, al"        \
    "xor    ax, ax"         \
    "out    40h, al"        \
    "out    40h, al"        \
    "pushad"                \
    "push   ds"             \
    "mov    ds, ax"         \
    "call   rtc_post"       \
    "pop    ds"             \
    "popad"                 \
    modify [ax] nomemory;
#else
# pragma aux wait_uninit = \
    "mov    al, 34h" \
    "out    43h, al" \
    "xor    ax, ax" \
    "out    40h, al" \
    "out    40h, al" \
    "push   bp" \
    "push   ds" \
    "mov    ds, ax" \
    "call   rtc_post" \
    "pop    ds" \
    "pop    bp" \
    modify [ax bx cx dx si di];
#endif


/**
 * Waits (sleeps) for the given number of ticks.
 * Checks for keystroke.
 *
 * @returns BIOS scan code if available, 0 if not.
 * @param   ticks       Number of ticks to sleep.
 * @param   stop_on_key Whether to stop immediately upon keypress.
 */
uint8_t wait(uint16_t ticks, uint8_t stop_on_key)
{
    long        ticks_to_wait, delta;
    uint16_t    old_flags;
    uint32_t    prev_ticks, t;
    uint8_t     scan_code = 0;

    /*
     * We may or may not be called with interrupts disabled. For the duration
     * of this function, interrupts must be enabled.
     */
    old_flags = int_query();
    int_enable();

    /*
     * The 0:046c wraps around at 'midnight' according to a 18.2Hz clock.
     * We also have to be careful about interrupt storms.
     */
    ticks_to_wait = ticks;
    prev_ticks = read_dword(0x0, 0x46c);
    do
    {
        halt();
        t = read_dword(0x0, 0x46c);
        if (t > prev_ticks)
        {
            delta = t - prev_ticks;     /* The temp var is required or bcc screws up. */
            ticks_to_wait -= delta;
        }
        else if (t < prev_ticks)
            ticks_to_wait -= t;         /* wrapped */
        prev_ticks = t;

        if (check_for_keystroke())
        {
            scan_code = get_keystroke();
            bios_printf(BIOS_PRINTF_INFO, "Key pressed: %x\n", scan_code);
            if (stop_on_key)
                return scan_code;
        }
    } while (ticks_to_wait > 0);
    int_restore(old_flags);
    return scan_code;
}

uint8_t read_logo_byte(uint8_t offset)
{
    outw(LOGO_IO_PORT, LOGO_CMD_SET_OFFSET | offset);
    return inb(LOGO_IO_PORT);
}

uint16_t read_logo_word(uint8_t offset)
{
    outw(LOGO_IO_PORT, LOGO_CMD_SET_OFFSET | offset);
    return inw(LOGO_IO_PORT);
}

// Hide cursor, clear screen and move cursor to starting position
void clear_screen(void);
#pragma aux clear_screen =  \
    "mov    ax, 100h"       \
    "mov    cx, 1000h"      \
    "int    10h"            \
    "mov    ax, 700h"       \
    "mov    bh, 7"          \
    "xor    cx, cx"         \
    "mov    dx, 184Fh"      \
    "int    10h"            \
    "mov    ax, 200h"       \
    "xor    bx, bx"         \
    "xor    dx, dx"         \
    "int    10h"            \
    modify [ax bx cx dx] nomemory;

void print_detected_harddisks(void)
{
    uint16_t    ebda_seg=read_word(0x0040,0x000E);
    uint8_t     hd_count;
    uint8_t     hd_curr = 0;
    uint8_t     ide_ctrl_printed = 0;
    uint8_t     sata_ctrl_printed = 0;
    uint8_t     scsi_ctrl_printed = 0;
    uint8_t     device;

    hd_count = read_byte(ebda_seg, (uint16_t)&EbdaData->bdisk.hdcount);

    for (hd_curr = 0; hd_curr < hd_count; hd_curr++)
    {
        device = read_byte(ebda_seg, (uint16_t)&EbdaData->bdisk.hdidmap[hd_curr]);

#ifdef VBOX_WITH_AHCI
        if (VBOX_IS_AHCI_DEVICE(device))
        {
            if (sata_ctrl_printed == 0)
            {
                printf("\n\n  AHCI controller:");
                sata_ctrl_printed = 1;
            }

            printf("\n    %d) Hard disk", hd_curr+1);

        }
        else
#endif
#ifdef VBOX_WITH_SCSI
        if (VBOX_IS_SCSI_DEVICE(device))
        {
            if (scsi_ctrl_printed == 0)
            {
                printf("\n\n  SCSI controller:");
                scsi_ctrl_printed = 1;
            }

            printf("\n    %d) Hard disk", hd_curr+1);

        }
        else
#endif
        {

            if ((device < 4) && (ide_ctrl_printed == 0))
            {
                printf("  IDE controller:");
                ide_ctrl_printed = 1;
            }
            else if ((device >= 4) && (sata_ctrl_printed == 0))
            {
                printf("\n\nAHCI controller:\n");
                sata_ctrl_printed = 1;
            }

            printf("\n    %d) ", hd_curr+1);

            /*
             * If actual_device is bigger than or equal 4
             * this is the next controller and
             * the positions start at the beginning.
             */
            if (device >= 4)
                device -= 4;

            if (device / 2)
                printf("Secondary ");
            else
                printf("Primary ");

            if (device % 2)
                printf("Slave");
            else
                printf("Master");
        }
    }

    if (   (ide_ctrl_printed == 0)
        && (sata_ctrl_printed == 0)
        && (scsi_ctrl_printed == 0))
        printf("No hard disks found");

    printf("\n");
}

uint8_t get_boot_drive(uint8_t scode)
{
    uint16_t    ebda_seg=read_word(0x0040,0x000E);

    /* Check that the scan code is in the range of detected hard disks. */
    uint8_t     hd_count = read_byte(ebda_seg, (uint16_t)&EbdaData->bdisk.hdcount);

    /* The key '1' has scancode 0x02 which represents the first disk */
    scode -= 2;

    if (scode < hd_count)
        return scode;

    /* Scancode is higher than number of available devices */
    return 0xff;
}

void show_logo(void)
{
    uint16_t    ebda_seg = read_word(0x0040,0x000E);
    uint8_t     f12_pressed = 0;
    uint8_t     scode;
    uint16_t    tmp, i;

    LOGOHDR     *logo_hdr = 0;
    uint8_t     is_fade_in, is_fade_out, uBootMenu;
    uint16_t    logo_time;
    uint16_t    old_mode;


    // Set PIT to 64hz.
    wait_init();

    // Get main signature
    tmp = read_logo_word((uint8_t)&logo_hdr->u16Signature);
    if (tmp != 0x66BB)
        goto done;

    // If there is no VBE, just skip this
    if (vesa_get_mode(&old_mode) != 0x004f )
        goto done;

    // Get options
    is_fade_in  = read_logo_byte((uint8_t)&logo_hdr->fu8FadeIn);
    is_fade_out = read_logo_byte((uint8_t)&logo_hdr->fu8FadeOut);
    logo_time   = read_logo_word((uint8_t)&logo_hdr->u16LogoMillies);
    uBootMenu   = read_logo_byte((uint8_t)&logo_hdr->fu8ShowBootMenu);

    // Is Logo disabled?
    if (!is_fade_in && !is_fade_out && !logo_time)
        goto done;

    /* Set video mode using private video BIOS interface. */
    tmp = custom_set_mode(640, 480, 32);
    /* If custom mode set failed, fall back to VBE. */
    if (tmp != 0x4F)
        vesa_set_mode(0x142);

    if (is_fade_in)
    {
        for (i = 0; i <= LOGO_SHOW_STEPS; i++)
        {
            outw(LOGO_IO_PORT, LOGO_CMD_SHOW_BMP | i);
            scode = wait(16 / WAIT_MS, 0);
            if (scode == F12_SCAN_CODE)
            {
                f12_pressed = 1;
                break;
            }
        }
    }
    else
        outw(LOGO_IO_PORT, LOGO_CMD_SHOW_BMP | LOGO_SHOW_STEPS);

    // Wait (interval in milliseconds)
    if (!f12_pressed)
    {
        scode = wait(logo_time / WAIT_MS, 1);
        if (scode == F12_SCAN_CODE)
            f12_pressed = 1;
    }

    // Fade out (only if F12 was not pressed)
    if (is_fade_out && !f12_pressed)
    {
        for (i = LOGO_SHOW_STEPS; i > 0 ; i--)
        {
            outw(LOGO_IO_PORT, LOGO_CMD_SHOW_BMP | i);
            scode = wait(16 / WAIT_MS, 0);
            if (scode == F12_SCAN_CODE)
            {
                f12_pressed = 1;
                break;
            }
        }
    }
    else if (!f12_pressed)
        outw(LOGO_IO_PORT, LOGO_CMD_SHOW_BMP | 0);

done:
    // Clear forced boot drive setting.
    write_byte(ebda_seg, (uint16_t)&EbdaData->uForceBootDevice, 0);

    // Don't restore previous video mode
    // The default text mode should be set up. (defect @bugref{1235})
    set_mode(0x0003);

    // If Setup menu enabled
    if (uBootMenu)
    {
        // If the graphics logo disabled
        if (!is_fade_in && !is_fade_out && !logo_time)
        {
            if (uBootMenu == 2)
                printf("Press F12 to select boot device.\n");

            // if the user has pressed F12 don't wait here
            if (!f12_pressed)
            {
                // Wait for timeout or keystroke
                scode = wait(F12_WAIT_TIME, 1);
                if (scode == F12_SCAN_CODE)
                    f12_pressed = 1;
            }
        }

        // If F12 pressed, show boot menu
        if (f12_pressed)
        {
            uint8_t boot_device = 0;
            uint8_t boot_drive = 0;

            clear_screen();

            // Show menu. Note that some versions of bcc freak out if we split these strings.
            printf("\nVirtualBox temporary boot device selection\n\nDetected Hard disks:\n\n");
            print_detected_harddisks();
            printf("\nOther boot devices:\n f) Floppy\n c) CD-ROM\n l) LAN\n\n b) Continue booting\n");



            // Wait for keystroke
            for (;;)
            {
                do
                {
                    scode = wait(WAIT_HZ, 1);
                } while (scode == 0);

                if (scode == 0x30)
                {
                    // 'b' ... continue
                    break;
                }

                // Check if hard disk was selected
                if ((scode >= 0x02) && (scode <= 0x09))
                {
                    boot_drive = get_boot_drive(scode);

                    /*
                     * 0xff indicates that there is no mapping
                     * from the scan code to a hard drive.
                     * Wait for next keystroke.
                     */
                    if (boot_drive == 0xff)
                        continue;

                    write_byte(ebda_seg, (uint16_t)&EbdaData->uForceBootDrive, boot_drive);
                    boot_device = 0x02;
                    break;
                }

                switch (scode)
                {
                    case 0x21:
                        // Floppy
                        boot_device = 0x01;
                        break;
                    case 0x2e:
                        // CD-ROM
                        boot_device = 0x03;
                        break;
                    case 0x26:
                        // LAN
                        boot_device = 0x04;
                        break;
                }

                if (boot_device != 0)
                    break;
            }

            write_byte(ebda_seg, (uint16_t)&EbdaData->uForceBootDevice, boot_device);

            // Switch to text mode. Clears screen and enables cursor again.
            set_mode(0x0003);
        }
    }

    // Restore PIT ticks
    wait_uninit();

    return;
}


void delay_boot(uint16_t secs)
{
    uint16_t    i;

    if (!secs)
        return;

    // Set PIT to 1ms ticks
    wait_init();

    printf("Delaying boot for %d seconds:", secs);
    for (i = secs; i > 0; i--)
    {
        printf(" %d", i);
        wait(WAIT_HZ, 0);
    }
    printf("\n");
    // Restore PIT ticks
    wait_uninit();
}
