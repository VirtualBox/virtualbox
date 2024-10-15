/* $Id: misc.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NAT - helpers (declarations/defines).
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

/*
 * This code is based on:
 *
 * Copyright (c) 1995 Danny Gasparovski.
 *
 * Please read the file COPYRIGHT for the
 * terms and conditions of the copyright.
 */

#ifndef _MISC_H_
#define _MISC_H_

#ifdef VBOX_NAT_TST_QUEUE
typedef void *PNATState;
#endif

void slirp_insque  (PNATState, void *, void *);
void slirp_remque  (PNATState, void *);
# ifndef VBOX_NAT_TST_QUEUE
void getouraddr (PNATState);
void fd_nonblock (int);

/* UVM interface */
#define UMA_ALIGN_PTR       (1 << 0)
#define UMA_ZONE_REFCNT     (1 << 1)
#define UMA_ZONE_MAXBUCKET  (1 << 2)
#define UMA_ZONE_ZINIT      (1 << 3)
#define UMA_SLAB_KERNEL     (1 << 4)
#define UMA_ZFLAG_FULL      (1 << 5)

struct uma_zone;
typedef struct uma_zone *uma_zone_t;

typedef void *(*uma_alloc_t)(uma_zone_t, int, u_int8_t *, int);
typedef void (*uma_free_t)(void *, int, u_int8_t);

typedef int (*ctor_t)(PNATState, void *, int, void *, int);
typedef void (*dtor_t)(PNATState, void *, int, void *);
typedef int (*zinit_t)(PNATState, void *, int, int);
typedef void (*zfini_t)(PNATState, void *, int);
uma_zone_t uma_zcreate(PNATState, char *, size_t, ctor_t, dtor_t, zinit_t, zfini_t, int, int);
uma_zone_t uma_zsecond_create(char *, ctor_t, dtor_t, zinit_t, zfini_t, uma_zone_t);
void uma_zone_set_max(uma_zone_t, int);
void uma_zone_set_allocf(uma_zone_t, uma_alloc_t);
void uma_zone_set_freef(uma_zone_t, uma_free_t);

uint32_t *uma_find_refcnt(uma_zone_t, void *);
void *uma_zalloc(uma_zone_t, int);
void *uma_zalloc_arg(uma_zone_t, void *, int);
void uma_zfree(uma_zone_t, void *);
void uma_zfree_arg(uma_zone_t, void *, void *);
int uma_zone_exhausted_nolock(uma_zone_t);
void zone_drain(uma_zone_t);

void slirp_null_arg_free(void *, void *);
void m_fini(PNATState pData);
# else /* VBOX_NAT_TST_QUEUE */
#  define insque slirp_insque
#  define remque slirp_remque
# endif
#endif
