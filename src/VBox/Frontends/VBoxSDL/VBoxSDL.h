/* $Id: VBoxSDL.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 *
 * VBox frontends: VBoxSDL (simple frontend based on SDL):
 * Main header
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

#ifndef VBOX_INCLUDED_SRC_VBoxSDL_VBoxSDL_h
#define VBOX_INCLUDED_SRC_VBoxSDL_VBoxSDL_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#ifdef RT_OS_WINDOWS /** @todo check why we need to do this on windows. */
/* convince SDL to not overload main() */
# define _SDL_main_h
#endif

/* include this first so Windows.h get's in before our stuff. */
#ifdef _MSC_VER
# pragma warning(push)
# pragma warning(disable: 4121) /* warning C4121: 'SDL_SysWMmsg' : alignment of a member was sensitive to packing*/
# pragma warning(disable: 4668) /* warning C4668: '__GNUC__' is not defined as a preprocessor macro, replacing with '0' for '#if/#elif' */
#endif
#include <SDL.h>
#ifdef _MSC_VER
# pragma warning(pop)
#endif

/** custom SDL event for display update handling */
#define SDL_USER_EVENT_UPDATERECT         (SDL_USEREVENT + 4)
/** custom SDL event for changing the guest resolution */
#define SDL_USER_EVENT_NOTIFYCHANGE       (SDL_USEREVENT + 5)
/** custom SDL for XPCOM event queue processing */
#define SDL_USER_EVENT_XPCOM_EVENTQUEUE   (SDL_USEREVENT + 6)
/** custom SDL event for updating the titlebar */
#define SDL_USER_EVENT_UPDATE_TITLEBAR    (SDL_USEREVENT + 7)
/** custom SDL user event for terminating the session */
#define SDL_USER_EVENT_TERMINATE          (SDL_USEREVENT + 8)
/** custom SDL user event for pointer shape change request */
#define SDL_USER_EVENT_POINTER_CHANGE     (SDL_USEREVENT + 9)
/** custom SDL user event for a regular timer */
#define SDL_USER_EVENT_TIMER              (SDL_USEREVENT + 10)
/** custom SDL user event for resetting mouse cursor */
#define SDL_USER_EVENT_GUEST_CAP_CHANGED  (SDL_USEREVENT + 11)
/** custom SDL user event for window resize done */
#define SDL_USER_EVENT_WINDOW_RESIZE_DONE (SDL_USEREVENT + 12)


/** The user.code field of the SDL_USER_EVENT_TERMINATE event.
 * @{
 */
/** Normal termination. */
#define VBOXSDL_TERM_NORMAL             0
/** Abnormal termination. */
#define VBOXSDL_TERM_ABEND              1
/** @} */

void ResetVM(void);
void SaveState(void);

#ifdef VBOX_WIN32_UI
int initUI(bool fResizable, int64_t &winId);
int uninitUI(void);
int resizeUI(uint16_t width, uint16_t height);
int setUITitle(char *title);
#endif /* VBOX_WIN32_UI */

#if defined(VBOXSDL_WITH_X11) || defined(RT_OS_DARWIN)
void PushNotifyUpdateEvent(SDL_Event *event);
#endif
int  PushSDLEventForSure(SDL_Event *event);

#ifdef RT_OS_DARWIN
RT_C_DECLS_BEGIN
void *VBoxSDLGetDarwinWindowId(void);
RT_C_DECLS_END
#endif

#endif /* !VBOX_INCLUDED_SRC_VBoxSDL_VBoxSDL_h */
