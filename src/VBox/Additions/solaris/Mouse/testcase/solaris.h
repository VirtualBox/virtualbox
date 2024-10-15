/* $Id: solaris.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuest - Guest Additions Driver for Solaris - testcase stubs.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */

#ifndef GA_INCLUDED_SRC_solaris_Mouse_testcase_solaris_h
#define GA_INCLUDED_SRC_solaris_Mouse_testcase_solaris_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/assert.h>
#include <iprt/string.h>  /* RT_ZERO */
#ifdef RT_OS_WINDOWS
# include <iprt/win/winsock2.h>  /* struct timeval */
#endif
#include <errno.h>
#include <time.h>  /* struct timeval */

/* Overrides */
#define dev_t unsigned

/* Constants */
#define DDI_FAILURE (-1)
#define DDI_SUCCESS (0)

#define MODMAXNAMELEN 32
#define MODMAXLINKINFOLEN 32
#define MODMAXLINK 10

#define MOD_NOAUTOUNLOAD        0x1

#define M_DATA          0x00
#define M_BREAK         0x08
#define M_PASSFP        0x09
#define M_EVENT         0x0a
#define M_SIG           0x0b
#define M_DELAY         0x0c
#define M_CTL           0x0d
#define M_IOCTL         0x0e
#define M_SETOPTS       0x10
#define M_RSE           0x11

#define M_IOCACK        0x81
#define M_IOCNAK        0x82
#define M_PCPROTO       0x83
#define M_PCSIG         0x84
#define M_READ          0x85
#define M_FLUSH         0x86
#define M_STOP          0x87
#define M_START         0x88
#define M_HANGUP        0x89
#define M_ERROR         0x8a
#define M_COPYIN        0x8b
#define M_COPYOUT       0x8c
#define M_IOCDATA       0x8d
#define M_PCRSE         0x8e
#define M_STOPI         0x8f
#define M_STARTI        0x90
#define M_PCEVENT       0x91
#define M_UNHANGUP      0x92
#define M_CMD           0x93

#define BPRI_LO         1
#define BPRI_MED        2
#define BPRI_HI         3

#define FLUSHALL        1
#define FLUSHDATA       0

#define TRANSPARENT     (unsigned int)(-1)

#define FLUSHR          0x01
#define FLUSHW          0x02

#define MSIOC           ('m'<<8)
#define MSIOGETPARMS    (MSIOC|1)
#define MSIOSETPARMS    (MSIOC|2)
#define MSIOBUTTONS     (MSIOC|3)
#define MSIOSRESOLUTION (MSIOC|4)

#define VUIOC           ('v'<<8)
#define VUIDSFORMAT   (VUIOC|1)
#define VUIDGFORMAT   (VUIOC|2)
#define VUID_NATIVE     0
#define VUID_FIRM_EVENT 1

#define VUIDSADDR   (VUIOC|3)
#define VUIDGADDR   (VUIOC|4)

#define VUID_WHEEL_MAX_COUNT    256
#define VUIDGWHEELCOUNT         (VUIOC|15)
#define VUIDGWHEELINFO          (VUIOC|16)
#define VUIDGWHEELSTATE         (VUIOC|17)
#define VUIDSWHEELSTATE         (VUIOC|18)

#define DDI_DEVICE_ATTR_V0      0x0001
#define DDI_DEVICE_ATTR_V1      0x0002

#define  DDI_NEVERSWAP_ACC      0x00
#define  DDI_STRUCTURE_LE_ACC   0x01
#define  DDI_STRUCTURE_BE_ACC   0x02

#define DDI_STRICTORDER_ACC     0x00
#define DDI_UNORDERED_OK_ACC    0x01
#define DDI_MERGING_OK_ACC      0x02
#define DDI_LOADCACHING_OK_ACC  0x03
#define DDI_STORECACHING_OK_ACC 0x04

/** @todo fix this */
#define DDI_DEFAULT_ACC DDI_STRICTORDER_ACC

#define DDI_INTR_CLAIMED        1
#define DDI_INTR_UNCLAIMED      0

#define DDI_INTR_TYPE_FIXED     0x1
#define DDI_INTR_TYPE_MSI       0x2
#define DDI_INTR_TYPE_MSIX      0x4

#define LOC_FIRST_DELTA         32640
#define LOC_X_DELTA             32640
#define LOC_Y_DELTA             32641
#define LOC_LAST_DELTA          32641
#define LOC_FIRST_ABSOLUTE      32642
#define LOC_X_ABSOLUTE          32642
#define LOC_Y_ABSOLUTE          32643
#define LOC_LAST_ABSOLUTE       32643

#define FE_PAIR_NONE            0
#define FE_PAIR_SET             1
#define FE_PAIR_DELTA           2
#define FE_PAIR_ABSOLUTE        3

typedef struct __ldi_handle             *ldi_handle_t;

typedef enum
{
    DDI_INFO_DEVT2DEVINFO = 0,
    DDI_INFO_DEVT2INSTANCE = 1
} ddi_info_cmd_t;

typedef enum
{
    DDI_ATTACH = 0,
    DDI_RESUME = 1,
    DDI_PM_RESUME = 2
} ddi_attach_cmd_t;

typedef enum
{
    DDI_DETACH = 0,
    DDI_SUSPEND = 1,
    DDI_PM_SUSPEND = 2,
    DDI_HOTPLUG_DETACH = 3
} ddi_detach_cmd_t;

/* Simple types */

typedef struct cred *cred_t;
typedef struct dev_info *dev_info_t;
typedef struct __ddi_acc_handle * ddi_acc_handle_t;
typedef struct __ddi_intr_handle *ddi_intr_handle_t;
typedef struct mutex *kmutex_t;
typedef unsigned int uint_t;
typedef unsigned short ushort_t;
typedef unsigned char uchar_t;

/* Structures */

struct modspecific_info {
    char    msi_linkinfo[MODMAXLINKINFOLEN];
    int     msi_p0;
};

struct modinfo {
    int                mi_info;
    int                mi_state;
    int                mi_id;
    int                mi_nextid;
    char              *mi_base;  /* Was caddr_t. */
    size_t             mi_size;
    int                mi_rev;
    int                mi_loadcnt;
    char               mi_name[MODMAXNAMELEN];
    struct modspecific_info mi_msinfo[MODMAXLINK];
};

typedef struct queue
{
    struct    qinit   *q_qinfo;
    struct    msgb    *q_first;
    struct    msgb    *q_last;
    struct    queue   *q_next;
    void              *q_ptr;
    size_t             q_count;
    uint_t             q_flag;
    ssize_t            q_minpsz;
    ssize_t            q_maxpsz;
    size_t             q_hiwat;
    size_t             q_lowat;
} queue_t;

typedef struct msgb
{
    struct msgb     *b_next;
    struct msgb     *b_prev;
    struct msgb     *b_cont;
    unsigned char   *b_rptr;
    unsigned char   *b_wptr;
    struct datab    *b_datap;
    unsigned char   b_band;
    unsigned short  b_flag;
} mblk_t;

typedef struct datab
{
    unsigned char     *db_base;
    unsigned char     *db_lim;
    unsigned char      db_ref;
    unsigned char      db_type;
} dblk_t;

struct iocblk
{
    int         ioc_cmd;
    cred_t      *ioc_cr;
    uint_t      ioc_id;
    uint_t      ioc_flag;
    size_t      ioc_count;
    int         ioc_rval;
    int         ioc_error;
#if defined(RT_ARCH_AMD64)  /* Actually this should be LP64. */
    int         dummy;  /* For simplicity, to ensure the structure size matches
                           struct copyreq. */
#endif
};

struct copyreq
{
    int       cq_cmd;
    cred_t   *cq_cr;
    uint_t    cq_id;
    uint_t    cq_flag;
    mblk_t   *cq_private;
    char     *cq_addr;  /* Was caddr_t. */
    size_t    cq_size;
};

struct copyresp
{
    int      cp_cmd;
    cred_t   *cp_cr;
    uint_t   cp_id;
    uint_t   cp_flag;
    mblk_t   *cp_private;
    char     *cp_rval;  /* Was caddr_t. */
};

typedef struct modctl
{
    /* ... */
    char mod_loadflags;
    /* ... */
} modctl_t;

typedef struct {
    int     jitter_thresh;
    int     speed_law;
    int     speed_limit;
} Ms_parms;

typedef struct {
    int     height;
    int     width;
} Ms_screen_resolution;

typedef struct  vuid_addr_probe {
    short   base;
    union
    {
        short   next;
        short   current;
    } data;
} Vuid_addr_probe;

typedef struct ddi_device_acc_attr
{
    ushort_t devacc_attr_version;
    uchar_t devacc_attr_endian_flags;
    uchar_t devacc_attr_dataorder;
    uchar_t devacc_attr_access;
} ddi_device_acc_attr_t;

typedef struct firm_event
{
    ushort_t        id;
    uchar_t         pair_type;
    uchar_t         pair;
    int             value;
    struct timeval time;
} Firm_event;

/* Prototypes */

#define _init vboxguestSolarisInit
extern int vboxguestSolarisInit(void);
#define _fini vboxguestSolarisFini
extern int vboxguestSolarisFini(void);
#define _info vboxguestSolarisInfo
extern int vboxguestSolarisInfo(struct modinfo *pModInfo);

/* Simple API stubs */

#define cmn_err(...) do {} while(0)
#define mod_remove(...) 0
#define mod_info(...) 0
#define RTR0Init(...) VINF_SUCCESS
#define RTR0Term(...) do {} while(0)
#define RTR0AssertPanicSystem(...) do {} while(0)
#define RTLogCreate(...) VINF_SUCCESS
#define RTLogRelSetDefaultInstance(...) do {} while(0)
#define RTLogDestroy(...) do {} while(0)
#if 0
#define VBoxGuestCreateKernelSession(...) VINF_SUCCESS
#define VBoxGuestCreateUserSession(...) VINF_SUCCESS
#define VBoxGuestCloseSession(...) do {} while(0)
#define VBoxGuestInitDevExt(...) VINF_SUCCESS
#define VBoxGuestDeleteDevExt(...) do {} while(0)
#define VBoxGuestCommonIOCtl(...) VINF_SUCCESS
#define VBoxGuestCommonISR(...) true
#define VbglR0GRAlloc(...) VINF_SUCCESS
#define VbglR0GRPerform(...) VINF_SUCCESS
#define VbglR0GRFree(...) do {} while(0)
#endif
#define VbglR0InitClient(...) VINF_SUCCESS
#define vbglDriverOpen(...) VINF_SUCCESS
#define vbglDriverClose(...) do {} while(0)
#define vbglDriverIOCtl(...) VINF_SUCCESS
#define qprocson(...) do {} while(0)
#define qprocsoff(...) do {} while(0)
#define flushq(...) do {} while(0)
#define putnext(...) do {} while(0)
#define ddi_get_instance(...) 0
#define pci_config_setup(...) DDI_SUCCESS
#define pci_config_teardown(...) do {} while(0)
#define ddi_regs_map_setup(...) DDI_SUCCESS
#define ddi_regs_map_free(...) do {} while(0)
#define ddi_dev_regsize(...) DDI_SUCCESS
#define ddi_create_minor_node(...) DDI_SUCCESS
#define ddi_remove_minor_node(...) do {} while(0)
#define ddi_intr_get_supported_types(...) DDI_SUCCESS
#define ddi_intr_get_nintrs(...) DDI_SUCCESS
#define ddi_intr_get_navail(...) DDI_SUCCESS
#define ddi_intr_alloc(...) DDI_SUCCESS
#define ddi_intr_free(...) do {} while(0)
#define ddi_intr_get_pri(...) DDI_SUCCESS
#define ddi_intr_enable(...) DDI_SUCCESS
#define ddi_intr_disable(...) DDI_SUCCESS
#define ddi_intr_add_handler(...) DDI_SUCCESS
#define ddi_intr_remove_handler(...) DDI_SUCCESS
#define mutex_init(...) do {} while(0)
#define mutex_destroy(...) do {} while(0)
#define mutex_enter(...) do {} while(0)
#define mutex_exit(...) do {} while(0)
#define uniqtime32(...) do {} while(0)
#define canput(...) true
#define putbq(...) do {} while(0)

/* Externally defined helpers. */

/** Flags set in the struct mblk b_flag member for verification purposes.
 * @{ */
/** miocpullup was called for this message. */
#define F_TEST_PULLUP 1
/** @} */

extern void miocack(queue_t *pWriteQueue, mblk_t *pMBlk, int cbData, int rc);
extern void miocnak(queue_t *pWriteQueue, mblk_t *pMBlk, int cbData, int iErr);
extern int miocpullup(mblk_t *pMBlk, size_t cbMsg);
extern void mcopyin(mblk_t *pMBlk, void *pvState, size_t cbData, void *pvUser);
extern void mcopyout(mblk_t *pMBlk, void *pvState, size_t cbData, void *pvUser,
                     mblk_t *pMBlkData);
extern void qreply(queue_t *pQueue, mblk_t *pMBlk);
extern mblk_t *allocb(size_t cb, uint_t cPrio);
extern void freemsg(mblk_t *pMsg);

/* API stubs with simple logic */

static modctl_t s_ModCtl;
static void **s_pvLinkage;

static inline modctl_t *mod_getctl(void **linkage)
{
    s_pvLinkage = linkage;
    return s_pvLinkage ? &s_ModCtl : NULL;
}

#define mod_install(linkage) (s_pvLinkage && ((linkage) == s_pvLinkage) ? 0 : EINVAL)
#define QREADR          0x00000010
#define         OTHERQ(q)      ((q)->q_flag & QREADR ? (q) + 1 : (q) - 1)
#define         WR(q)          ((q)->q_flag & QREADR ? (q) + 1 : (q))
#define         RD(q)          ((q)->q_flag & QREADR ? (q) : (q) - 1)


/* Basic initialisation of a queue structure pair for testing. */
static inline void doInitQueues(queue_t aQueues[2])
{
    aQueues[0].q_flag = QREADR;
}

static inline dev_t makedevice(unsigned cMajor, unsigned cMinor)
{
    return cMajor * 4096 + cMinor;
}

static inline unsigned getmajor(dev_t device)
{
    return device / 4096;
}

/* API stubs with controllable logic */

#endif /* !GA_INCLUDED_SRC_solaris_Mouse_testcase_solaris_h */
