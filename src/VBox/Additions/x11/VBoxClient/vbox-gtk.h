/* $Id: vbox-gtk.h 108217 2025-02-05 12:08:25Z vadim.galitsyn@oracle.com $ */
/** @file
 * Guest Additions - Definitions from Gtk 3.24.38 and Glib 2.17 libraries.
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

/* GTK - The GIMP Toolkit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Modified by the GTK+ Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GTK+ Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/.
 */

/* GLIB - Library of useful routines for C programming
 * Copyright (C) 1995-1997  Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Modified by the GLib Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GLib Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GLib at ftp://ftp.gtk.org/pub/gtk/.
 */

/*
 * Oracle LGPL Disclaimer: For the avoidance of doubt, except that if any license choice
 * other than GPL or LGPL is available it will apply instead, Oracle elects to use only
 * the Lesser General Public License version 2.1 (LGPLv2) at this time for any software where
 * a choice of LGPL license versions is made available with the language indicating
 * that LGPLv2 or any later version may be used, or where a choice of which version
 * of the LGPL is applied is otherwise unspecified.
 */

#ifndef GA_INCLUDED_SRC_x11_VBoxClient_vbox_gtk_h
#define GA_INCLUDED_SRC_x11_VBoxClient_vbox_gtk_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

extern "C"
{


/**
 * Gtk definitions.
 */


#ifndef FALSE
#define FALSE (0)
#endif

#ifndef TRUE
#define TRUE (!FALSE)
#endif

#define G_SOURCE_REMOVE                                         FALSE

#define GUINT_TO_POINTER(u)                                     ((gpointer) (gulong) (u))
#define _GDK_MAKE_ATOM(val)                                     ((GdkAtom)GUINT_TO_POINTER(val))

#define GDK_NONE                                                _GDK_MAKE_ATOM (0)

#define GDK_SELECTION_CLIPBOARD                                 _GDK_MAKE_ATOM (69)

#define _G_TYPE_CIC(ip, gt, ct)                                 ((ct*) (void *) ip)
#define G_TYPE_CHECK_INSTANCE_CAST(instance, g_type, c_type)    (_G_TYPE_CIC ((instance), (g_type), c_type))

#define G_CALLBACK(f)                                           ((GCallback) (f))
#define g_signal_connect(instance, detailed_signal, c_handler, data) \
     g_signal_connect_data ((instance), (detailed_signal), (c_handler), (data), NULL, (GConnectFlags) 0)

#define g_signal_connect_after(instance, detailed_signal, c_handler, data) \
     g_signal_connect_data ((instance), (detailed_signal), (c_handler), (data), NULL, G_CONNECT_AFTER)

#define GTK_TYPE_WINDOW                                         (gtk_window_get_type ())
#define GTK_WINDOW(obj)                                         (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_TYPE_WINDOW, GtkWindow))

#define GTK_TYPE_CONTAINER                                      (gtk_container_get_type ())
#define GTK_CONTAINER(obj)                                      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_TYPE_CONTAINER, GtkContainer))

#define GTK_TYPE_BOX                                            (gtk_box_get_type ())
#define GTK_BOX(obj)                                            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_TYPE_BOX, GtkBox))

#define GTK_TYPE_WIDGET                                         (gtk_widget_get_type ())
#define GTK_WIDGET(widget)                                      (G_TYPE_CHECK_INSTANCE_CAST ((widget), GTK_TYPE_WIDGET, GtkWidget))

#define G_TYPE_APPLICATION                                      (g_application_get_type ())
#define G_APPLICATION(inst)                                     (G_TYPE_CHECK_INSTANCE_CAST ((inst), G_TYPE_APPLICATION, GApplication))

typedef enum
{
    G_APPLICATION_FLAGS_NONE = 0,
    G_APPLICATION_DEFAULT_FLAGS = 0,
    G_APPLICATION_IS_SERVICE  =          (1 << 0),
    G_APPLICATION_IS_LAUNCHER =          (1 << 1),

    G_APPLICATION_HANDLES_OPEN =         (1 << 2),
    G_APPLICATION_HANDLES_COMMAND_LINE = (1 << 3),
    G_APPLICATION_SEND_ENVIRONMENT    =  (1 << 4),

    G_APPLICATION_NON_UNIQUE =           (1 << 5),

    G_APPLICATION_CAN_OVERRIDE_APP_ID =  (1 << 6),
    G_APPLICATION_ALLOW_REPLACEMENT   =  (1 << 7),
    G_APPLICATION_REPLACE             =  (1 << 8)
} GApplicationFlags;

typedef enum
{
    GTK_WINDOW_TOPLEVEL,
    GTK_WINDOW_POPUP
} GtkWindowType;

typedef enum
{
    GDK_NOTHING       = -1,
    GDK_DELETE        = 0,
    GDK_DESTROY       = 1,
    GDK_EXPOSE        = 2,
    GDK_MOTION_NOTIFY = 3,
    GDK_BUTTON_PRESS  = 4,
    GDK_2BUTTON_PRESS = 5,
    GDK_DOUBLE_BUTTON_PRESS = GDK_2BUTTON_PRESS,
    GDK_3BUTTON_PRESS = 6,
    GDK_TRIPLE_BUTTON_PRESS = GDK_3BUTTON_PRESS,
    GDK_BUTTON_RELEASE    = 7,
    GDK_KEY_PRESS     = 8,
    GDK_KEY_RELEASE   = 9,
    GDK_ENTER_NOTIFY  = 10,
    GDK_LEAVE_NOTIFY  = 11,
    GDK_FOCUS_CHANGE  = 12,
    GDK_CONFIGURE     = 13,
    GDK_MAP       = 14,
    GDK_UNMAP     = 15,
    GDK_PROPERTY_NOTIFY   = 16,
    GDK_SELECTION_CLEAR   = 17,
    GDK_SELECTION_REQUEST = 18,
    GDK_SELECTION_NOTIFY  = 19,
    GDK_PROXIMITY_IN  = 20,
    GDK_PROXIMITY_OUT = 21,
    GDK_DRAG_ENTER        = 22,
    GDK_DRAG_LEAVE        = 23,
    GDK_DRAG_MOTION       = 24,
    GDK_DRAG_STATUS       = 25,
    GDK_DROP_START        = 26,
    GDK_DROP_FINISHED     = 27,
    GDK_CLIENT_EVENT  = 28,
    GDK_VISIBILITY_NOTIFY = 29,
    GDK_SCROLL            = 31,
    GDK_WINDOW_STATE      = 32,
    GDK_SETTING           = 33,
    GDK_OWNER_CHANGE      = 34,
    GDK_GRAB_BROKEN       = 35,
    GDK_DAMAGE            = 36,
    GDK_TOUCH_BEGIN       = 37,
    GDK_TOUCH_UPDATE      = 38,
    GDK_TOUCH_END         = 39,
    GDK_TOUCH_CANCEL      = 40,
    GDK_TOUCHPAD_SWIPE    = 41,
    GDK_TOUCHPAD_PINCH    = 42,
    GDK_PAD_BUTTON_PRESS  = 43,
    GDK_PAD_BUTTON_RELEASE = 44,
    GDK_PAD_RING          = 45,
    GDK_PAD_STRIP         = 46,
    GDK_PAD_GROUP_MODE    = 47,
    GDK_EVENT_LAST        /* helper variable for decls */
} GdkEventType;

typedef enum
{
    GDK_WINDOW_STATE_WITHDRAWN        = 1 << 0,
    GDK_WINDOW_STATE_ICONIFIED        = 1 << 1,
    GDK_WINDOW_STATE_MAXIMIZED        = 1 << 2,
    GDK_WINDOW_STATE_STICKY           = 1 << 3,
    GDK_WINDOW_STATE_FULLSCREEN       = 1 << 4,
    GDK_WINDOW_STATE_ABOVE            = 1 << 5,
    GDK_WINDOW_STATE_BELOW            = 1 << 6,
    GDK_WINDOW_STATE_FOCUSED          = 1 << 7,
    GDK_WINDOW_STATE_TILED            = 1 << 8,
    GDK_WINDOW_STATE_TOP_TILED        = 1 << 9,
    GDK_WINDOW_STATE_TOP_RESIZABLE    = 1 << 10,
    GDK_WINDOW_STATE_RIGHT_TILED      = 1 << 11,
    GDK_WINDOW_STATE_RIGHT_RESIZABLE  = 1 << 12,
    GDK_WINDOW_STATE_BOTTOM_TILED     = 1 << 13,
    GDK_WINDOW_STATE_BOTTOM_RESIZABLE = 1 << 14,
    GDK_WINDOW_STATE_LEFT_TILED       = 1 << 15,
    GDK_WINDOW_STATE_LEFT_RESIZABLE   = 1 << 16
} GdkWindowState;

typedef enum
{
    GTK_ORIENTATION_HORIZONTAL,
    GTK_ORIENTATION_VERTICAL
} GtkOrientation;

typedef enum
{
    G_CONNECT_DEFAULT = 0,
    G_CONNECT_AFTER   = 1 << 0,
    G_CONNECT_SWAPPED = 1 << 1
} GConnectFlags;

typedef enum
{
    GTK_DIR_TAB_FORWARD,
    GTK_DIR_TAB_BACKWARD,
    GTK_DIR_UP,
    GTK_DIR_DOWN,
    GTK_DIR_LEFT,
    GTK_DIR_RIGHT
} GtkDirectionType;

typedef struct _GdkWindow GdkWindow;
typedef char gint8;

struct _GdkEventWindowState
{
    GdkEventType type;
    GdkWindow *window;
    gint8 send_event;
    GdkWindowState changed_mask;
    GdkWindowState new_window_state;
};

typedef struct _GList GList;
typedef struct _GdkAtom *GdkAtom;
typedef struct _GtkTargetList GtkTargetList;
typedef struct _GtkTargetEntry GtkTargetEntry;
typedef struct _GtkClipboard GtkClipboard;
typedef struct _GtkSelectionData GtkSelectionData;
typedef struct _GtkWindow GtkWindow;
typedef struct _GClosure GClosure;
typedef struct _GdkEventWindowState GdkEventWindowState;
typedef struct _GtkContainer GtkContainer;
typedef struct _GtkBox GtkBox;
typedef struct _GtkButton GtkButton;
typedef struct _GtkApplication GtkApplication;
typedef struct _GApplication GApplication;
typedef union  _GdkEvent GdkEvent;

typedef struct _GtkWidget GtkWidget;
typedef int gint;
typedef gint gboolean;
typedef char gchar;
typedef unsigned char guchar;
typedef unsigned int guint;
typedef unsigned long gulong;
typedef void* gpointer;
typedef double gdouble;
typedef gulong GType;

typedef gboolean (*GSourceFunc) (gpointer user_data);
typedef void (*GtkClipboardGetFunc) (GtkClipboard *clipboard, GtkSelectionData *selection_data, guint info, gpointer user_data_or_owner);
typedef void (*GtkClipboardClearFunc)(GtkClipboard *clipboard, gpointer user_data_or_owner);
typedef void (*GCallback) (void);
typedef void (*GClosureNotify) (gpointer data, GClosure *closure);
typedef void (*GtkClipboardReceivedFunc) (GtkClipboard *clipboard, GtkSelectionData *selection_data, gpointer data);


extern GdkAtom gdk_atom_intern(const gchar *atom_name, gboolean only_if_exists);
extern gchar* gdk_atom_name(GdkAtom atom);

extern guint gdk_threads_add_timeout(guint interval, GSourceFunc function, gpointer data);

extern GtkApplication *gtk_application_new(const gchar *application_id, GApplicationFlags flags);
extern GtkWidget *gtk_application_window_new(GtkApplication *application);

extern GType gtk_box_get_type(void);
extern GtkWidget* gtk_box_new(GtkOrientation orientation, gint spacing);
extern void gtk_box_pack_start(GtkBox* box, GtkWidget* child, gboolean expand, gboolean fill,
                               guint padding);
extern GtkWidget* gtk_button_new(void);

extern GtkClipboard *gtk_clipboard_get(GdkAtom selection);
extern void gtk_clipboard_request_contents(GtkClipboard *clipboard, GdkAtom target,
                                           GtkClipboardReceivedFunc callback, gpointer user_data);
extern gboolean gtk_clipboard_set_with_data(GtkClipboard* clipboard, const GtkTargetEntry* targets,
                                            guint n_targets, GtkClipboardGetFunc get_func,
                                            GtkClipboardClearFunc clear_func, gpointer user_data);
extern void gtk_clipboard_store(GtkClipboard* clipboard);
extern gboolean gtk_clipboard_wait_for_targets(GtkClipboard *clipboard, GdkAtom **targets, gint *n_targets);
extern void gtk_container_add(GtkContainer* container, GtkWidget* widget);
extern GType gtk_container_get_type(void);

extern void gtk_init(int *argc, char ***argv);
extern void gtk_main(void);
extern void gtk_main_quit(void);

extern guchar *gtk_selection_data_get_data(const GtkSelectionData* selection_data);
extern GdkAtom gtk_selection_data_get_data_type(const GtkSelectionData *selection_data);
extern const guchar *gtk_selection_data_get_data_with_length(const GtkSelectionData *selection_data, gint *length);
extern GdkAtom gtk_selection_data_get_target(const GtkSelectionData* selection_data);
extern void gtk_selection_data_set(GtkSelectionData* selection_data, GdkAtom type, gint format, const guchar* data, gint length);

extern void gtk_target_list_add(GtkTargetList *list, GdkAtom target, guint flags, guint info);
extern GtkTargetList *gtk_target_list_new(const GtkTargetEntry *targets, guint ntargets);
extern void gtk_target_list_unref(GtkTargetList  *list);

extern void gtk_target_table_free(GtkTargetEntry* targets, gint n_targets);
extern GtkTargetEntry *gtk_target_table_new_from_list(GtkTargetList* list, gint* n_targets);

extern void gtk_widget_destroy(GtkWidget* widget);
extern GType gtk_widget_get_type(void);
extern void gtk_widget_set_opacity(GtkWidget* widget, double opacity);
extern void gtk_widget_show_all(GtkWidget* widget);

extern GType gtk_window_get_type(void);
extern void gtk_window_iconify(GtkWindow* window);
extern void gtk_window_present(GtkWindow* window);
extern void gtk_window_resize(GtkWindow* window, gint width, gint height);
extern void gtk_window_set_default_size(GtkWindow* window, gint width, gint height);


/**
 * Glib definitions.
 */


extern GType g_application_get_type(void);
extern gulong g_signal_connect_data(gpointer instance, const gchar *detailed_signal,
                                    GCallback c_handler, gpointer data,
                                    GClosureNotify destroy_data, GConnectFlags connect_flags);
extern void g_free(gpointer mem);

extern int g_application_run(GApplication *application, int argc, char **argv);
extern GApplication *g_application_get_default(void);
extern void g_application_quit(GApplication *application);
extern void g_object_unref(gpointer object);

} /* extern "C" */

#endif /* !GA_INCLUDED_SRC_x11_VBoxClient_vbox_gtk_h */

