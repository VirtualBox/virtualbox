/* $Id: tstPalmOne.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * USB PalmOne testcase
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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/usbdevice_fs.h>


/** @name USB Control message recipient codes (from spec)
 * @{ */
#define VUSB_TO_DEVICE          0x0
#define VUSB_TO_INTERFACE       0x1
#define VUSB_TO_ENDPOINT        0x2
#define VUSB_TO_OTHER           0x3
#define VUSB_RECIP_MASK         0x1f
/** @} */

/** @name USB control pipe setup packet structure (from spec)
 * @{ */
#define VUSB_REQ_SHIFT          (5)
#define VUSB_REQ_STANDARD       (0x0 << VUSB_REQ_SHIFT)
#define VUSB_REQ_CLASS          (0x1 << VUSB_REQ_SHIFT)
#define VUSB_REQ_VENDOR         (0x2 << VUSB_REQ_SHIFT)
#define VUSB_REQ_RESERVED       (0x3 << VUSB_REQ_SHIFT)
#define VUSB_REQ_MASK           (0x3 << VUSB_REQ_SHIFT)
/** @} */

#define VUSB_DIR_TO_HOST        0x80
typedef struct vusb_setup
{
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} VUSBSETUP, *PVUSBSETUP;
typedef const VUSBSETUP *PCVUSBSETUP;


int g_fd;

int bitch(const char *pszMsg)
{
    printf("failure: %s: %d %s\n", pszMsg, errno, strerror(errno));
    return 1;
}

void hex(const void *pv, ssize_t cb, const char *pszWhat)
{
    printf("%s: cb=%d\n", pszWhat, cb);
    unsigned char *pb = (unsigned char *)pv;
    int cch = 0;
    int off = 0;
    int cchPrecision = 16;
    while (off < cb)
    {
        int i;
        printf("%s%0*x %04x:", off ? "\n" : "", sizeof(pb) * 2, (uintptr_t)pb, off);

        for (i = 0; i < cchPrecision && off + i < cb; i++)
            printf(off + i < cb ? !(i & 7) && i ? "-%02x" : " %02x" : "   ", pb[i]);
        while (i++ < cchPrecision)
            printf("   ");
        printf(" ");
        for (i = 0; i < cchPrecision && off + i < cb; i++)
        {
            uint8_t u8 = pb[i];
            fputc(u8 < 127 && u8 >= 32 ? u8 : '.', stdout);
        }

        /* next */
        pb += cchPrecision;
        off += cchPrecision;
    }
    printf("\n");
}

int doioctl(int iCmd, void *pvData, const char *pszWho)
{
    int rc;
    do
    {
        errno = 0;
        rc = ioctl(g_fd, iCmd, pvData);

    } while (rc && errno == EAGAIN);
    if (rc)
        printf("doioctl: %s: iCmd=%#x errno=%d %s\n", pszWho, iCmd, errno, strerror(errno));
    else
        printf("doioctl: %s: iCmd=%#x ok\n", pszWho, iCmd);
    return rc;
}

int dobulk(int EndPt, void *pvBuf, size_t cbBuf, const char *pszWho)
{
#if 0
    struct usbdevfs_urb KUrb = {0};
    KUrb.type = USBDEVFS_URB_TYPE_BULK;
    KUrb.endpoint = EndPt;
    KUrb.buffer = pvBuf;
    KUrb.buffer_length = cbBuf;
    KUrb.actual_length = 0; //cbBuf
    KUrb.flags = 0; /* ISO_ASAP/SHORT_NOT_OK */
    if (!doioctl(USBDEVFS_SUBMITURB, &KUrb, pszWho))
    {
        struct usbdevfs_urb *pKUrb = NULL;
        if (!doioctl(USBDEVFS_REAPURB, &pKUrb, pszWho)
            && pKUrb == &KUrb)
            return KUrb.actual_length;
    }
    return -1;
#else
    struct usbdevfs_bulktransfer BulkMsg = {0};

    BulkMsg.ep = EndPt;
    BulkMsg.timeout = 1000;
    BulkMsg.len = cbBuf;
    BulkMsg.data = pvBuf;
    int rc = doioctl(USBDEVFS_BULK, &BulkMsg, pszWho);
//    printf("rc=%d BulkMsg.len=%d cbBuf=%d\n", rc, BulkMsg.len, cbBuf);
    if (rc >= 0)
        return rc;
    return -1;
#endif
}

int send_bulk(int EndPt, void *pvBuf, size_t cbBuf)
{
    return dobulk(EndPt, pvBuf, cbBuf, "send_bulk");
}

int recv_bulk(int EndPt, void *pvBuf, size_t cbBuf)
{
    int cb = dobulk(EndPt | 0x80, pvBuf, cbBuf, "recv_bulk");
    if (cb > 0)
        printf("cb=%d\n", cb);
    return cb;
}

int doctrl(uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint16_t wLength,
           void *pvBuf, const char *pszWho)
{
#if 0
    struct usbdevfs_urb KUrb = {0};
    KUrb.type = USBDEVFS_URB_TYPE_BULK;
    KUrb.endpoint = EndPt;
    KUrb.buffer = pvBuf;
    KUrb.buffer_length = cbBuf;
    KUrb.actual_length = 0; //cbBuf
    KUrb.flags = 0; /* ISO_ASAP/SHORT_NOT_OK */
    if (!doioctl(USBDEVFS_SUBMITURB, &KUrb, pszWho))
    {
        struct usbdevfs_urb *pKUrb = NULL;
        if (!doioctl(USBDEVFS_REAPURB, &pKUrb, pszWho)
            && pKUrb == &KUrb)
            return KUrb.actual_length;
    }
    return -1;
#else
    struct usbdevfs_ctrltransfer CtrlMsg = {0};

    CtrlMsg.bRequestType = bmRequestType;
    CtrlMsg.bRequest = bRequest;
    CtrlMsg.wValue = wValue;
    CtrlMsg.wLength = wLength;
    CtrlMsg.timeout = 1000;
    CtrlMsg.data = pvBuf;

    int rc = doioctl(USBDEVFS_CONTROL, &CtrlMsg, pszWho);
    printf("rc=%d CtrlMsg.wLength=%d\n", rc, CtrlMsg.wLength);
    if (rc >= 0)
        return rc;
    return -1;
#endif
}

static int claim_if(int iIf)
{
    return doioctl(USBDEVFS_CLAIMINTERFACE, &iIf, "claim_if");
}

static int usb_set_connected(int ifnum, int conn)
{
    struct usbdevfs_ioctl io;
    io.ifno = ifnum;
    io.ioctl_code = (conn) ? USBDEVFS_CONNECT : USBDEVFS_DISCONNECT;
    io.data = NULL;
    return doioctl(USBDEVFS_IOCTL, &io, "set_connected");
}

static int set_config(int iCfg)
{
    return doioctl(USBDEVFS_SETCONFIGURATION, &iCfg, "set_config");
}

static int set_interface(int iIf, int iAlt)
{
    struct usbdevfs_setinterface SetIf = {0};
    SetIf.interface  = iIf;
    SetIf.altsetting = iAlt;
    return doioctl(USBDEVFS_SETINTERFACE, &SetIf, "set_interface");
}

/* can be exploited to check if there is an active config. */
static int reset_ep(int EndPt)
{
    return doioctl(USBDEVFS_RESETEP, &EndPt, "reset_ep");
}


static void msd()
{
#if 1
    unsigned InEndPt = 1;
    unsigned OutEndPt = 1;
#else
    unsigned InEndPt = 1;
    unsigned OutEndPt = 2;
#endif
    unsigned char abBuf[512];
    int i;

//    set_config(1); - the culprit
    set_interface(0, 0);

#if 0
    /* Send an Get Max LUN request */
    abBuf[0] = 0;
    if (doctrl(VUSB_DIR_TO_HOST | VUSB_REQ_CLASS | VUSB_TO_INTERFACE,
               0xfe /* max lun */, 0, 1, 1, abBuf, "get max lun") >= 0)
        printf("max luns: %d\n",  abBuf[0]);
#endif

    for (i = 0; i < 3; i++)
    {
        printf("i=%d\n", i);

        /* Send an INQUIRY command to ep 2 */
        memset(abBuf, 0, sizeof(abBuf));
        memcpy(abBuf, "USBC", 4);
        *(uint32_t *)(&abBuf[4]) = 0x12330984 ;
        //abBuf[8]    = 0x08;
        abBuf[8]    = 0x24;
        abBuf[0xc]  = 0x80;
        abBuf[0xe]  = 0x06; /* cmd length */
        abBuf[0x0f] = 0x12; /* cmd - INQUIRY */
        abBuf[0x13] = 0x24;

        hex(abBuf, 31, "intquery req");
        if (send_bulk(OutEndPt, abBuf, 31) < 0)
            return;
        //usleep(15000);

        /* read result */
        memset(abBuf, 0, sizeof(abBuf));
    //printf("recv..\n");
        int cb = recv_bulk(InEndPt, abBuf, 36);
        hex(abBuf, cb, "inquiry result");

        /* sense? */
        memset(abBuf, 0, sizeof(abBuf));
        cb = recv_bulk(InEndPt, abBuf, 36);
        hex(abBuf, cb, "inquiry sense?");
        usleep(150000);
    }
}

void palm(void)
{
//    set_config(1); //skip this
//    reset_ep(6);

    /* This seems to be some kind of 'identify device' request. */
    uint8_t abVendor[0x14] = {0};
    int cb = doctrl(VUSB_DIR_TO_HOST | VUSB_REQ_VENDOR | VUSB_TO_ENDPOINT,
                    0x04, 0, 0, 0x14, abVendor, "vendor req");
    hex(abVendor, cb, "palm vendor req");

    /* read from ep 6. */
    uint8_t abBuf[512];
    memset(abBuf, 0, sizeof(abBuf));
    cb = recv_bulk(6, abBuf, 6);
    hex(abBuf, cb, "bulk 1");

    /* read from ep 6. */
    memset(abBuf, 0, sizeof(abBuf));
    cb = recv_bulk(6, abBuf, 22);
    hex(abBuf, cb, "bulk 2");

#if 0
    /* write to ep 6 */
    memset(abBuf, 0, sizeof(abBuf));
    abBuf[0] = 1;
    abBuf[1] = 2;
    abBuf[5] = 0x32;
    if (send_bulk(7, abBuf, 6) < 0)
        return;

    memset(abBuf, 0, sizeof(abBuf));
    abBuf[0] = 0x12;
    abBuf[1] = 1;
    if (send_bulk(7, abBuf, 8) < 0)
        return;

    memset(abBuf, 0, sizeof(abBuf));
    abBuf[1] = 0x20;
    abBuf[6] = 0x24;
    if (send_bulk(7, abBuf, 6) < 0)
        return;

    memset(abBuf, 0, sizeof(abBuf));
    abBuf[0] = 0xff;
    abBuf[1] = 0xff;
    abBuf[2] = 0xff;
    abBuf[3] = 0xff;
    abBuf[4] = 0x3c;
    abBuf[6] = 0x3c;
    abBuf[0x0f] = 1;
    abBuf[0x11] = 0x10;
    abBuf[0x15] = 0x10;
    abBuf[0x18] = 0x3c;
    abBuf[0x1a] = 0x3c;
    if (send_bulk(7, abBuf, 0x24) < 0)
        return;

    /* read from ep 6 */
    memset(abBuf, 0, sizeof(abBuf));
    cb = recv_bulk(6, abBuf, 64);
    hex(abBuf, cb, "bulk 3");

    /* read from ep 6. */
    memset(abBuf, 0, sizeof(abBuf));
    cb = recv_bulk(6, abBuf, 50);
    hex(abBuf, cb, "bulk 4");
#endif
}

int reset(void)
{
    int i = 0;
    printf("resetting...\n");
    return doioctl(USBDEVFS_RESET, &i, "reset");
}

int main(int argc, char **argv)
{
    g_fd = open(argv[1], O_RDWR);
    if (errno == ENOENT && g_fd < 0)
    {
        int i;
        for (i = 0; i < 120; i++)
        {
            g_fd = open(argv[1], O_RDWR);
            if (g_fd >= 0)
                break;
            printf("."); fflush(stdout);
            usleep(500000);
        }
        printf("\n");
    }
    if (g_fd < 0)
        return bitch("open");

//    reset();
//    set_config(0);
//    set_config(1);

    usb_set_connected(0, 1);
    claim_if(0);

#if 0
    msd();
#else
    palm();
#endif
    return 0;
}
