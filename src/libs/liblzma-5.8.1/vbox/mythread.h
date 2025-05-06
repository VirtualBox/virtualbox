/* $Id: mythread.h 109441 2025-05-06 17:40:59Z samantha.scholz@oracle.com $ */
/** @file
 * mythread.h - Thread implementation based on IPRT
 */

/*
 * Copyright (C) 2023 Oracle and/or its affiliates.
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

#ifndef MYTHREAD_H
#define MYTHREAD_H

#include "sysdefs.h"

#if defined(VBOX)
# define MYTHREAD_ENABLED 1
#else
# error "VBOX is not defined!"
#endif


#ifdef MYTHREAD_ENABLED

////////////////////////////////////////
// Shared between all threading types //
////////////////////////////////////////

// Locks a mutex for a duration of a block.
//
// Perform mythread_mutex_lock(&mutex) in the beginning of a block
// and mythread_mutex_unlock(&mutex) at the end of the block. "break"
// may be used to unlock the mutex and jump out of the block.
// mythread_sync blocks may be nested.
//
// Example:
//
//     mythread_sync(mutex) {
//         foo();
//         if (some_error)
//             break; // Skips bar()
//         bar();
//     }
//
// At least GCC optimizes the loops completely away so it doesn't slow
// things down at all compared to plain mythread_mutex_lock(&mutex)
// and mythread_mutex_unlock(&mutex) calls.
//
#define mythread_sync(mutex) mythread_sync_helper1(mutex, __LINE__)
#define mythread_sync_helper1(mutex, line) mythread_sync_helper2(mutex, line)
#define mythread_sync_helper2(mutex, line) \
	for (unsigned int mythread_i_ ## line = 0; \
			mythread_i_ ## line \
				? (mythread_mutex_unlock(&(mutex)), 0) \
				: (mythread_mutex_lock(&(mutex)), 1); \
			mythread_i_ ## line = 1) \
		for (unsigned int mythread_j_ ## line = 0; \
				!mythread_j_ ## line; \
				mythread_j_ ## line = 1)
#endif


#include <iprt/thread.h>
#include <iprt/critsect.h>
#include <iprt/condvar.h>
#include <iprt/once.h>

#define MYTHREAD_RET_TYPE DECLCALLBACK(int)
#define MYTHREAD_RET_VALUE VINF_SUCCESS

typedef RTTHREAD mythread;
typedef RTCRITSECT mythread_mutex;
typedef RTCONDVAR mythread_cond;

typedef struct {
	// Tick count (milliseconds) in the beginning of the timeout.
	// NOTE: This is 32 bits so it wraps around after 49.7 days.
	// Multi-day timeouts may not work as expected.
	uint32_t start;

	// Length of the timeout in milliseconds. The timeout expires
	// when the current tick count minus "start" is equal or greater
	// than "timeout".
	uint32_t timeout;
} mythread_condtime;


#define mythread_once(func) \
	do { \
		static RTONCE s_Once = RTONCE_INITIALIZER; \
		RTOnce(&s_Once, (PFNRTONCE)func, NULL /*pvUser*/); \
	} while (0)

// mythread_sigmask() isn't available on Windows. Even a dummy version would
// make no sense because the other POSIX signal functions are missing anyway.


static inline int
mythread_create(mythread *thread, PFNRTTHREAD pfnThread, void *arg)
{
	RTThreadCreate(thread, pfnThread, arg, 0 /*cbStack*/, RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "VBox-LZMA");
	return 0;
}

static inline int
mythread_join(mythread thread)
{
	int rc = RTThreadWait(thread, RT_INDEFINITE_WAIT, NULL/*prc*/);
	if (RT_FAILURE(rc))
		rc = -1;

	return rc;
}


static inline int
mythread_mutex_init(mythread_mutex *mutex)
{
	RTCritSectInit(mutex);
	return 0;
}

static inline void
mythread_mutex_destroy(mythread_mutex *mutex)
{
	RTCritSectDelete(mutex);
}

static inline void
mythread_mutex_lock(mythread_mutex *mutex)
{
	RTCritSectEnter(mutex);
}

static inline void
mythread_mutex_unlock(mythread_mutex *mutex)
{
	RTCritSectLeave(mutex);
}


static inline int
mythread_cond_init(mythread_cond *cond)
{
	RTCondVarCreate(cond);
	return 0;
}

static inline void
mythread_cond_destroy(mythread_cond *cond)
{
	RTCondVarDestroy(*cond);
}

static inline void
mythread_cond_signal(mythread_cond *cond)
{
	RTCondVarSignal(*cond);
}

static inline void
mythread_cond_wait(mythread_cond *cond, mythread_mutex *mutex)
{
	RTCondVarCritSectWait(*cond, mutex, RT_INDEFINITE_WAIT);
}

static inline int
mythread_cond_timedwait(mythread_cond *cond, mythread_mutex *mutex,
		const mythread_condtime *condtime)
{
	int rc = RTCondVarCritSectWait(*cond, mutex, condtime->timeout);
	if (rc == VERR_TIMEOUT)
		return -1;
	return 0;
}

static inline void
mythread_condtime_set(mythread_condtime *condtime, const mythread_cond *cond,
		uint32_t timeout)
{
	(void)cond;
	//condtime->start = RTTimeSystemMilliTS();
	condtime->timeout = timeout;
}

#endif
