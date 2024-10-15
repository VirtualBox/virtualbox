/* $Id: vbox-greeter.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * vbox-greeter - an own LightDM greeter module supporting auto-logons
 *                controlled by the host.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define GLIB_DISABLE_DEPRECATION_WARNINGS 1 /* g_type_init() is deprecated */
#include <pwd.h>
#include <syslog.h>
#include <stdlib.h>

#include <lightdm.h>
#ifdef VBOX_WITH_FLTK
# include <FL/Fl.H>
# include <FL/fl_ask.H> /* Yes, the casing is correct for 1.3.0 -- d'oh. */
# include <FL/Fl_Box.H>
# include <FL/Fl_Button.H>
# include <FL/fl_draw.H> /* Same as above. */
# include <FL/Fl_Double_Window.H>
# include <FL/Fl_Input.H>
# include <FL/Fl_Menu_Button.H>
# ifdef VBOX_GREETER_WITH_PNG_SUPPORT
#  include <FL/Fl_PNG_Image.H>
#  include <FL/Fl_Shared_Image.H>
# endif
# include <FL/Fl_Secret_Input.H>
#else
# include <cairo-xlib.h>
# include <gtk/gtk.h>
# include <gdk/gdkx.h>
#endif

#include <package-generated.h>
#include "product-generated.h"

#include <iprt/assert.h>
#include <iprt/buildconfig.h>
#include <iprt/env.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/stream.h>
#include <iprt/system.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/time.h>

#include <VBox/log.h>
#include <VBox/VBoxGuestLib.h>

/* The greeter's full name for logging. */
#define VBOX_MODULE_NAME "vbox-lightdm-greeter"

/* UI elements used in this greeter. */
#define VBOX_GREETER_UI_WND_GREETER     "wnd_greeter"

#define VBOX_GREETER_UI_EDT_USER        "edt_username"
#define VBOX_GREETER_UI_EDT_PASSWORD    "edt_password"
#define VBOX_GREETER_UI_BTN_LOGIN       "btn_login"
#define VBOX_GREETER_UI_LBL_INFO        "lbl_info"

/* UI display options. */
/** Show the restart menu entry / button. */
#define VBOX_GREETER_UI_SHOW_RESTART    RT_BIT(0)
/** Show the shutdown menu entry / button. */
#define VBOX_GREETER_UI_SHOW_SHUTDOWN   RT_BIT(1)
/** Show the (customized) top banner.  */
#define VBOX_GREETER_UI_SHOW_BANNER     RT_BIT(2)
/** Enable custom colors */
#define VBOX_GREETER_UI_USE_THEMING     RT_BIT(3)

/** Extracts the 8-bit red component from an uint32_t. */
#define VBOX_RGB_COLOR_RED(uColor)      uColor & 0xFF
/** Extracts the 8-bit green component from an uint32_t. */
#define VBOX_RGB_COLOR_GREEN(uColor)    (uColor >> 8)  & 0xFF
/** Extracts the 8-bit blue component from an uint32_t. */
#define VBOX_RGB_COLOR_BLUE(uColor)     (uColor >> 16) & 0xFF

#include <VBox/log.h>
#ifdef VBOX_WITH_GUEST_PROPS
# include <VBox/HostServices/GuestPropertySvc.h>
#endif

/** The program name (derived from argv[0]). */
char                *g_pszProgName =  (char *)"";
/** For debugging. */
#ifdef DEBUG
 static int          g_iVerbosity = 99;
#else
 static int          g_iVerbosity = 0;
#endif
static bool          g_fRunning   = true;

/** Logging parameters. */
/** @todo Make this configurable later. */
static PRTLOGGER     g_pLoggerRelease = NULL;
static uint32_t      g_cHistory = 10;                   /* Enable log rotation, 10 files. */
static uint32_t      g_uHistoryFileTime = RT_SEC_1DAY;  /* Max 1 day per file. */
static uint64_t      g_uHistoryFileSize = 100 * _1M;    /* Max 100MB per file. */

/**
 * Context structure which contains all needed
 * data within callbacks.
 */
typedef struct VBOXGREETERCTX
{
    /** Pointer to this greeter instance. */
    LightDMGreeter      *pGreeter;
#ifdef VBOX_WITH_FLTK
    Fl_Button           *pBtnLogin;
    Fl_Input            *pEdtUsername;
    Fl_Secret_Input     *pEdtPassword;
    Fl_Box              *pLblInfo;
#else
    /** The GTK builder instance for accessing
     *  the UI elements. */
    GtkBuilder     *pBuilder;
#endif
    /** The timeout (in ms) to wait for credentials. */
    uint32_t        uTimeoutMS;
    /** The starting timestamp (in ms) to calculate
     *  the timeout. */
    uint64_t        uStartMS;
    /** Timestamp of last abort message. */
    uint64_t        uTsAbort;
    /** The HGCM client ID. */
    uint32_t        uClientId;
    /** The credential password. */
    char           *pszPassword;
} VBOXGREETERCTX, *PVBOXGREETERCTX;

static void vboxGreeterError(const char *pszFormat, ...)
{
    va_list va;
    char *buf;
    va_start(va, pszFormat);
    if (RTStrAPrintfV(&buf, pszFormat, va))
    {
        RTLogRelPrintf("%s: error: %s", VBOX_MODULE_NAME, buf);
        RTStrFree(buf);
    }
    va_end(va);
}

static void vboxGreeterLog(const char *pszFormat, ...)
{
    if (g_iVerbosity)
    {
        va_list va;
        char *buf;
        va_start(va, pszFormat);
        if (RTStrAPrintfV(&buf, pszFormat, va))
        {
            /* Only do normal logging in debug mode; could contain
             * sensitive data! */
            RTLogRelPrintf("%s: %s", VBOX_MODULE_NAME, buf);
            RTStrFree(buf);
        }
        va_end(va);
    }
}

/** @tood Move the following two functions to VbglR3 (also see pam_vbox). */
#ifdef VBOX_WITH_GUEST_PROPS

/**
 * Reads a guest property.
 *
 * @return  IPRT status code.
 * @param   hPAM                    PAM handle.
 * @param   uClientID               Guest property service client ID.
 * @param   pszKey                  Key (name) of guest property to read.
 * @param   fReadOnly               Indicates whether this key needs to be
 *                                  checked if it only can be read (and *not* written)
 *                                  by the guest.
 * @param   pszValue                Buffer where to store the key's value.
 * @param   cbValue                 Size of buffer (in bytes).
 * @param   puTimestamp             Timestamp of the value
 *                                  retrieved. Optional.
 */
static int vbox_read_prop(uint32_t uClientID,
                          const char *pszKey, bool fReadOnly,
                          char *pszValue, size_t cbValue, uint64_t *puTimestamp)
{
    AssertReturn(uClientID, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszKey, VERR_INVALID_POINTER);
    AssertPtrReturn(pszValue, VERR_INVALID_POINTER);
    /* puTimestamp is optional. */

    int rc;

    uint64_t u64Timestamp = 0;
    char *pszValTemp = NULL;
    char *pszFlags = NULL;
    /* The buffer for storing the data and its initial size.  We leave a bit
     * of space here in case the maximum values are raised. */
    void *pvBuf = NULL;
    uint32_t cbBuf = GUEST_PROP_MAX_VALUE_LEN + GUEST_PROP_MAX_FLAGS_LEN + _1K;

    /* Because there is a race condition between our reading the size of a
     * property and the guest updating it, we loop a few times here and
     * hope.  Actually this should never go wrong, as we are generous
     * enough with buffer space. */
    for (unsigned i = 0; i < 10; i++)
    {
        pvBuf = RTMemRealloc(pvBuf, cbBuf);
        if (pvBuf)
        {
            rc = VbglR3GuestPropRead(uClientID, pszKey, pvBuf, cbBuf,
                                     &pszValTemp, &u64Timestamp, &pszFlags,
                                     &cbBuf);
        }
        else
            rc = VERR_NO_MEMORY;

        switch (rc)
        {
            case VERR_BUFFER_OVERFLOW:
            {
                /* Buffer too small, try it with a bigger one next time. */
                cbBuf += _1K;
                continue; /* Try next round. */
            }

            default:
                break;
        }

        /* Everything except VERR_BUFFER_OVERLOW makes us bail out ... */
        break;
    }

    if (RT_SUCCESS(rc))
    {
        /* Check security bits. */
        if (pszFlags)
        {
            if (   fReadOnly
                && !RTStrStr(pszFlags, "RDONLYGUEST"))
            {
                /* If we want a property which is read-only on the guest
                 * and it is *not* marked as such, deny access! */
                rc = VERR_ACCESS_DENIED;
            }
        }
        else /* No flags, no access! */
            rc = VERR_ACCESS_DENIED;

        if (RT_SUCCESS(rc))
        {
            /* If everything went well copy property value to our destination buffer. */
            if (!RTStrPrintf(pszValue, cbValue, "%s", pszValTemp))
                rc = VERR_BUFFER_OVERFLOW;

            if (puTimestamp)
                *puTimestamp = u64Timestamp;
        }
    }

#ifdef DEBUG
    vboxGreeterLog("Read guest property \"%s\"=\"%s\" (Flags: %s, TS: %RU64): %Rrc\n",
                   pszKey, pszValTemp ? pszValTemp : "<None>",
                   pszFlags ? pszFlags : "<None>", u64Timestamp, rc);
#endif

    if (pvBuf)
        RTMemFree(pvBuf);

    return rc;
}

# if 0 /* unused */
/**
 * Waits for a guest property to be changed.
 *
 * @return  IPRT status code.
 * @param   hPAM                    PAM handle.
 * @param   uClientID               Guest property service client ID.
 * @param   pszKey                  Key (name) of guest property to wait for.
 * @param   uTimeoutMS              Timeout (in ms) to wait for the change. Specify
 *                                  RT_INDEFINITE_WAIT to wait indefinitly.
 */
static int vbox_wait_prop(uint32_t uClientID,
                          const char *pszKey, uint32_t uTimeoutMS)
{
    AssertReturn(uClientID, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszKey, VERR_INVALID_POINTER);

    int rc;

    /* The buffer for storing the data and its initial size.  We leave a bit
     * of space here in case the maximum values are raised. */
    void *pvBuf = NULL;
    uint32_t cbBuf = MAX_NAME_LEN + MAX_VALUE_LEN + MAX_FLAGS_LEN + _1K;

    for (int i = 0; i < 10; i++)
    {
        void *pvTmpBuf = RTMemRealloc(pvBuf, cbBuf);
        if (pvTmpBuf)
        {
            char *pszName = NULL;
            char *pszValue = NULL;
            uint64_t u64TimestampOut = 0;
            char *pszFlags = NULL;

            pvBuf = pvTmpBuf;
            rc = VbglR3GuestPropWait(uClientID, pszKey, pvBuf, cbBuf,
                                     0 /* Last timestamp; just wait for next event */, uTimeoutMS,
                                     &pszName, &pszValue, &u64TimestampOut,
                                     &pszFlags, &cbBuf, NULL);
        }
        else
            rc = VERR_NO_MEMORY;

        if (rc == VERR_BUFFER_OVERFLOW)
        {
            /* Buffer too small, try it with a bigger one next time. */
            cbBuf += _1K;
            continue; /* Try next round. */
        }

        /* Everything except VERR_BUFFER_OVERLOW makes us bail out ... */
        break;
    }

    return rc;
}
# endif /* unused */

#endif /* VBOX_WITH_GUEST_PROPS */

/**
 * Checks for credentials provided by the host / HGCM.
 *
 * @return  IPRT status code. VERR_NOT_FOUND if no credentials are available,
 *          VINF_SUCCESS on successful retrieval or another IPRT error.
 * @param   pCtx                    Greeter context.
 */
static int vboxGreeterCheckCreds(PVBOXGREETERCTX pCtx)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    static bool s_fCredsNotFoundMsgShown = false;
    int rc = VbglR3CredentialsQueryAvailability();
    if (RT_FAILURE(rc))
    {
        if (rc != VERR_NOT_FOUND)
            vboxGreeterError("vboxGreeterCheckCreds: could not query for credentials! rc=%Rrc. Aborting\n", rc);
        else if (!s_fCredsNotFoundMsgShown)
        {
            vboxGreeterLog("vboxGreeterCheckCreds: no credentials available\n");
            s_fCredsNotFoundMsgShown = true;
        }
    }
    else
    {
        /** @todo Domain handling needed?  */
        char *pszUsername; /* User name only is kept local. */
        char *pszDomain = NULL;
        rc = VbglR3CredentialsRetrieve(&pszUsername, &pCtx->pszPassword, &pszDomain);
        if (RT_FAILURE(rc))
        {
            vboxGreeterError("vboxGreeterCheckCreds: could not retrieve credentials! rc=%Rrc. Aborting\n", rc);
        }
        else
        {
            vboxGreeterLog("vboxGreeterCheckCreds: credentials retrieved: user=%s, password=%s, domain=%s\n",
                           pszUsername,
#ifdef DEBUG
                           pCtx->pszPassword,
#else
                           "XXX",
#endif
                           pszDomain);
            /* Trigger LightDM authentication with the user name just retrieved. */
            lightdm_greeter_authenticate(pCtx->pGreeter, pszUsername); /* Must be the real user name from host! */

            /* Securely wipe the user name + domain again. */
            VbglR3CredentialsDestroy(pszUsername, NULL /* pszPassword */, pszDomain,
                                     3 /* Three wipe passes */);
        }
    }

#ifdef DEBUG
    vboxGreeterLog("vboxGreeterCheckCreds: returned with rc=%Rrc\n", rc);
#endif
    return rc;
}

/**
 * Called by LightDM when greeter is not needed anymore.
 *
 * @param   signum                  Signal number.
 */
static void cb_sigterm(int signum)
{
    RT_NOREF(signum);

    /* Note: This handler must be reentrant-safe. */
#ifdef VBOX_WITH_FLTK
    g_fRunning = false;
#else
    exit(RTEXITCODE_SUCCESS);
#endif
}

/**
 * Callback for showing a user prompt, issued by the LightDM server.
 *
 * @param pGreeter                    Pointer to this greeter instance.
 * @param pszText                     Text to display.
 * @param enmType                     Type of prompt to display.
 * @param pvData                      Pointer to user-supplied data.
 */
static void cb_lightdm_show_prompt(LightDMGreeter *pGreeter,
                                   const gchar *pszText, LightDMPromptType enmType,
                                   gpointer pvData)
{
    vboxGreeterLog("cb_lightdm_show_prompt: text=%s, type=%d\n", pszText, enmType);

    PVBOXGREETERCTX pCtx = (PVBOXGREETERCTX)pvData;
    AssertPtr(pCtx);

    switch (enmType)
    {
        case 1: /* Password. */
        {
            if (pCtx->pszPassword)
            {
                lightdm_greeter_respond(pGreeter, pCtx->pszPassword);
            }
            else
            {
#ifdef VBOX_WITH_FLTK
                AssertPtr(pCtx->pEdtPassword);
                const char *pszPwd = pCtx->pEdtPassword->value();
#else
                GtkEntry *pEdtPwd = GTK_ENTRY(gtk_builder_get_object(pCtx->pBuilder, "edt_password"));
                AssertPtr(pEdtPwd);
                const gchar *pszPwd = gtk_entry_get_text(pEdtPwd);
#endif
                lightdm_greeter_respond(pGreeter, pszPwd);
            }
            break;
        }
        /** @todo Other fields?  */

        default:
            break;
    }

    VbglR3CredentialsDestroy(NULL /* pszUsername */, pCtx->pszPassword, NULL /* pszDomain */,
                             3 /* Three wipe passes */);
    pCtx->pszPassword = NULL;
}

/**
 * Callback for showing a message, issued by the LightDM server.
 *
 * @param pGreeter                    Pointer to this greeter instance.
 * @param pszText                     Text to display.
 * @param enmType                     Type of message to display.
 * @param pvData                      Pointer to user-supplied data.
 */
static void cb_lightdm_show_message(LightDMGreeter *pGreeter,
                                    const gchar *pszText, LightDMPromptType enmType,
                                    gpointer pvData)
{
    RT_NOREF(pGreeter);
    vboxGreeterLog("cb_lightdm_show_message: text=%s, type=%d\n", pszText, enmType);

    PVBOXGREETERCTX pCtx = (PVBOXGREETERCTX)pvData;
    AssertPtrReturnVoid(pCtx);

#ifdef VBOX_WITH_FLTK
    AssertPtr(pCtx->pLblInfo);
    pCtx->pLblInfo->copy_label(pszText);
#else
    GtkLabel *pLblInfo = GTK_LABEL(gtk_builder_get_object(pCtx->pBuilder, "lbl_info"));
    AssertPtr(pLblInfo);
    gtk_label_set_text(pLblInfo, pszText);
#endif
}

/**
 * Callback for authentication completion, issued by the LightDM server.
 *
 * @param pGreeter                    Pointer to this greeter instance.
 */
static void cb_lightdm_auth_complete(LightDMGreeter *pGreeter)
{
    vboxGreeterLog("cb_lightdm_auth_complete\n");

    const gchar *pszUser = lightdm_greeter_get_authentication_user(pGreeter);
    vboxGreeterLog("authenticating user: %s\n", pszUser ? pszUser : "<NULL>");

    if (lightdm_greeter_get_is_authenticated(pGreeter))
    {
        /** @todo Add non-default session support. */
        gchar *pszSession = g_strdup(lightdm_greeter_get_default_session_hint(pGreeter));
        if (pszSession)
        {
            vboxGreeterLog("starting session: %s\n", pszSession);
            GError *pError = NULL;
            if (!lightdm_greeter_start_session_sync(pGreeter, pszSession, &pError))
            {
                vboxGreeterError("unable to start session '%s': %s\n",
                                 pszSession, pError ? pError->message : "Unknown error");
            }
            else
            {
                AssertPtr(pszSession);
                vboxGreeterLog("session '%s' successfully started\n", pszSession);
            }
            if (pError)
                g_error_free(pError);
            g_free(pszSession);
        }
        else
            vboxGreeterError("unable to get default session\n");
    }
    else
        vboxGreeterLog("user not authenticated successfully (yet)\n");
}

/**
 * Callback for clicking on the "Login" button.
 *
 * @param pWidget                     Widget this callback is bound to.
 * @param pvData                      Pointer to user-supplied data.
 */
#ifdef VBOX_WITH_FLTK
void cb_btn_login(Fl_Widget *pWidget, void *pvData)
#else
void cb_btn_login(GtkWidget *pWidget, gpointer pvData)
#endif
{
    PVBOXGREETERCTX pCtx = (PVBOXGREETERCTX)pvData;
    RT_NOREF(pWidget);
    AssertPtr(pCtx);

#ifdef VBOX_WITH_FLTK
    AssertPtr(pCtx->pEdtUsername);
    const char *pszUser = pCtx->pEdtUsername->value();
    AssertPtr(pCtx->pEdtPassword);
    const char *pszPwd = pCtx->pEdtPassword->value();
#else
    GtkEntry *pEdtUser = GTK_ENTRY(gtk_builder_get_object(pCtx->pBuilder, VBOX_GREETER_UI_EDT_USER));
    AssertPtr(pEdtUser);
    const gchar *pszUser = gtk_entry_get_text(pEdtUser);

    GtkEntry *pEdtPwd = GTK_ENTRY(gtk_builder_get_object(pCtx->pBuilder, VBOX_GREETER_UI_EDT_PASSWORD));
    AssertPtr(pEdtPwd);
    const gchar *pszPwd = gtk_entry_get_text(pEdtPwd);
#endif

    /** @todo Add domain handling? */
    vboxGreeterLog("login button pressed: greeter=%p, user=%s, password=%s\n",
                   pCtx->pGreeter,
                   pszUser ? pszUser : "<NONE>",
#ifdef DEBUG
                   pszPwd ? pszPwd : "<NONE>");
#else
                   /* Don't log passwords in release mode! */
                   "XXX");
#endif
    if (strlen(pszUser)) /* Only authenticate if username is given. */
    {
        lightdm_greeter_respond(pCtx->pGreeter, pszPwd);
        lightdm_greeter_authenticate(pCtx->pGreeter, pszUser);
    }
}

/**
 * Callback for clicking on the "Menu" button.
 *
 * @param pWidget                     Widget this callback is bound to.
 * @param pvData                      Pointer to user-supplied data.
 */
#ifdef VBOX_WITH_FLTK
void cb_btn_menu(Fl_Widget *pWidget, void *pvData)
#else
void cb_btn_menu(GtkWidget *pWidget, gpointer pvData)
#endif
{
    RT_NOREF(pWidget, pvData);
    vboxGreeterLog("menu button pressed\n");
}

/**
 * Callback for clicking on the "Restart" button / menu entry.
 *
 * @param pWidget                     Widget this callback is bound to.
 * @param pvData                      Pointer to user-supplied data.
 */
#ifdef VBOX_WITH_FLTK
void cb_btn_restart(Fl_Widget *pWidget, void *pvData)
#else
void cb_btn_restart(GtkWidget *pWidget, gpointer pvData)
#endif
{
    RT_NOREF(pWidget, pvData);
    vboxGreeterLog("restart button pressed\n");

    bool fRestart = true;
#ifdef VBOX_WITH_FLTK
    int rc = fl_choice("Really restart the system?", "Yes", "No", NULL);
    fRestart = rc == 0;
#endif

    if (fRestart)
    {
        vboxGreeterLog("restart requested\n");
#ifndef DEBUG
        lightdm_restart(NULL);
#endif
    }
}

/**
 * Callback for clicking on the "Shutdown" button / menu entry.
 *
 * @param pWidget                     Widget this callback is bound to.
 * @param pvData                      Pointer to user-supplied data.
 */
#ifdef VBOX_WITH_FLTK
void cb_btn_shutdown(Fl_Widget *pWidget, void *pvData)
#else
void cb_btn_shutdown(GtkWidget *pWidget, gpointer pvData)
#endif
{
    RT_NOREF(pWidget, pvData);
    vboxGreeterLog("shutdown button pressed\n");

    bool fShutdown = true;
#ifdef VBOX_WITH_FLTK
    int rc = fl_choice("Really shutdown the system?", "Yes", "No", NULL);
    fShutdown = rc == 0;
#endif

    if (fShutdown)
    {
        vboxGreeterLog("shutdown requested\n");
#ifndef DEBUG
        lightdm_shutdown(NULL);
#endif
    }
}

#ifdef VBOX_WITH_FLTK
void cb_edt_username(Fl_Widget *pWidget, void *pvData)
#else
void cb_edt_username(GtkWidget *pWidget, gpointer pvData)
#endif
{
    RT_NOREF(pWidget);
    vboxGreeterLog("cb_edt_username called\n");

    PVBOXGREETERCTX pCtx = (PVBOXGREETERCTX)pvData;
    AssertPtr(pCtx);
#ifdef VBOX_WITH_FLTK
    AssertPtr(pCtx->pEdtPassword);
    Fl::focus(pCtx->pEdtPassword);
#endif
}

#ifdef VBOX_WITH_FLTK
void cb_edt_password(Fl_Widget *pWidget, void *pvData)
#else
void cb_edt_password(GtkWidget *pWidget, gpointer pvData)
#endif
{
    RT_NOREF(pWidget, pvData);
    vboxGreeterLog("cb_edt_password called\n");

    PVBOXGREETERCTX pCtx = (PVBOXGREETERCTX)pvData;
    AssertPtr(pCtx);
#ifdef VBOX_WITH_FLTK
    AssertPtr(pCtx->pBtnLogin);
    cb_btn_login(pCtx->pBtnLogin, pvData);
#endif
}

/**
 * Callback for the timer event which is checking for new credentials
 * from the host.
 *
 * @param pvData                      Pointer to user-supplied data.
 */
#ifdef VBOX_WITH_FLTK
static void cb_check_creds(void *pvData)
#else
static gboolean cb_check_creds(gpointer pvData)
#endif
{
    PVBOXGREETERCTX pCtx = (PVBOXGREETERCTX)pvData;
    AssertPtr(pCtx);

#ifdef DEBUG
    vboxGreeterLog("cb_check_creds called, clientId=%RU32, timeoutMS=%RU32\n",
                   pCtx->uClientId, pCtx->uTimeoutMS);
#endif

    int rc = VINF_SUCCESS;

#ifdef VBOX_WITH_GUEST_PROPS
    bool fAbort = false;
    char szVal[255];
    if (pCtx->uClientId)
    {
        uint64_t tsAbort;
        rc = vbox_read_prop(pCtx->uClientId,
                            "/VirtualBox/GuestAdd/PAM/CredsWaitAbort",
                            true /* Read-only on guest */,
                            szVal, sizeof(szVal), &tsAbort);
        switch (rc)
        {
            case VINF_SUCCESS:
# ifdef DEBUG
                vboxGreeterLog("cb_check_creds: tsAbort %RU64 <-> %RU64\n",
                               pCtx->uTsAbort, tsAbort);
# endif
                if (tsAbort != pCtx->uTsAbort)
                    fAbort = true; /* Timestamps differs, abort. */
                pCtx->uTsAbort = tsAbort;
                break;

            case VERR_TOO_MUCH_DATA:
                vboxGreeterError("cb_check_creds: temporarily unable to get abort notification\n");
                break;

            case VERR_NOT_FOUND:
                /* Value not found, continue checking for credentials. */
                break;

            default:
                vboxGreeterError("cb_check_creds: the abort notification request failed with rc=%Rrc\n", rc);
                fAbort = true; /* Abort on error. */
                break;
        }
    }

    if (fAbort)
    {
        /* Get optional message. */
        szVal[0] = '\0';
        int rc2 = vbox_read_prop(pCtx->uClientId,
                                 "/VirtualBox/GuestAdd/PAM/CredsMsgWaitAbort",
                                 true /* Read-only on guest */,
                                 szVal, sizeof(szVal), NULL /* Timestamp. */);
        if (   RT_FAILURE(rc2)
            && rc2 != VERR_NOT_FOUND)
            vboxGreeterError("cb_check_creds: getting wait abort message failed with rc=%Rrc\n", rc2);
# ifdef VBOX_WITH_FLTK
        AssertPtr(pCtx->pLblInfo);
        pCtx->pLblInfo->copy_label(szVal);
# else /* !VBOX_WITH_FLTK */
        GtkLabel *pLblInfo = GTK_LABEL(gtk_builder_get_object(pCtx->pBuilder, VBOX_GREETER_UI_LBL_INFO));
        AssertPtr(pLblInfo);
        gtk_label_set_text(pLblInfo, szVal);
# endif /* !VBOX_WITH_FLTK */
        vboxGreeterLog("cb_check_creds: got notification from host to abort waiting\n");
    }
    else
    {
#endif /* VBOX_WITH_GUEST_PROPS */
        rc = vboxGreeterCheckCreds(pCtx);
        if (RT_SUCCESS(rc))
        {
            /* Credentials retrieved. */
        }
        else if (rc == VERR_NOT_FOUND)
        {
            /* No credentials found, but try next round (if there's
             * time left for) ... */
        }
#ifdef VBOX_WITH_GUEST_PROPS
    }
#endif /* VBOX_WITH_GUEST_PROPS */

    if (rc == VERR_NOT_FOUND) /* No credential found this round. */
    {
        /* Calculate timeout value left after process has been started.  */
        uint64_t u64Elapsed = RTTimeMilliTS() - pCtx->uStartMS;
        /* Is it time to bail out? */
        if (pCtx->uTimeoutMS < u64Elapsed)
        {
#ifdef VBOX_WITH_GUEST_PROPS
            szVal[0] = '\0';
            int rc2 = vbox_read_prop(pCtx->uClientId,
                                     "/VirtualBox/GuestAdd/PAM/CredsMsgWaitTimeout",
                                     true /* Read-only on guest */,
                                     szVal, sizeof(szVal), NULL /* Timestamp. */);
            if (   RT_FAILURE(rc2)
                && rc2 != VERR_NOT_FOUND)
                vboxGreeterError("cb_check_creds: getting wait timeout message failed with rc=%Rrc\n", rc2);
# ifdef VBOX_WITH_FLTK
            AssertPtr(pCtx->pLblInfo);
            pCtx->pLblInfo->copy_label(szVal);
# else
            GtkLabel *pLblInfo = GTK_LABEL(gtk_builder_get_object(pCtx->pBuilder, VBOX_GREETER_UI_LBL_INFO));
            AssertPtr(pLblInfo);
            gtk_label_set_text(pLblInfo, szVal);
# endif
#endif /* VBOX_WITH_GUEST_PROPS */
            vboxGreeterLog("cb_check_creds: no credentials retrieved within time (%RU32ms), giving up\n",
                           pCtx->uTimeoutMS);
            rc = VERR_TIMEOUT;
        }
    }

#ifdef DEBUG
    vboxGreeterLog("cb_check_creds returned with rc=%Rrc\n", rc);
#endif

    /* At the moment we only allow *one* shot from the host,
     * so setting credentials in a second attempt won't be possible
     * intentionally. */

    if (rc == VERR_NOT_FOUND)
#ifdef VBOX_WITH_FLTK
        Fl::repeat_timeout(0.5 /* 500 ms */, cb_check_creds, pvData);
#else
        return TRUE; /* No credentials found, do another round. */

    return FALSE; /* Remove timer source on every other error / status. */
#endif
}

/**
 * Release logger callback.
 *
 * @return  IPRT status code.
 * @param   pLoggerRelease
 * @param   enmPhase
 * @param   pfnLog
 */
static DECLCALLBACK(void) vboxGreeterLogHeaderFooter(PRTLOGGER pLoggerRelease, RTLOGPHASE enmPhase, PFNRTLOGPHASEMSG pfnLog)
{
    /* Some introductory information. */
    static RTTIMESPEC s_TimeSpec;
    char szTmp[256];
    if (enmPhase == RTLOGPHASE_BEGIN)
        RTTimeNow(&s_TimeSpec);
    RTTimeSpecToString(&s_TimeSpec, szTmp, sizeof(szTmp));

    switch (enmPhase)
    {
        case RTLOGPHASE_BEGIN:
        {
            pfnLog(pLoggerRelease,
                   "vbox-greeter %s r%s (verbosity: %d) %s (%s %s) release log\n"
                   "Log opened %s\n",
                   RTBldCfgVersion(), RTBldCfgRevisionStr(), g_iVerbosity, VBOX_BUILD_TARGET,
                   __DATE__, __TIME__, szTmp);

            int vrc = RTSystemQueryOSInfo(RTSYSOSINFO_PRODUCT, szTmp, sizeof(szTmp));
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pLoggerRelease, "OS Product: %s\n", szTmp);
            vrc = RTSystemQueryOSInfo(RTSYSOSINFO_RELEASE, szTmp, sizeof(szTmp));
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pLoggerRelease, "OS Release: %s\n", szTmp);
            vrc = RTSystemQueryOSInfo(RTSYSOSINFO_VERSION, szTmp, sizeof(szTmp));
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pLoggerRelease, "OS Version: %s\n", szTmp);
            if (RT_SUCCESS(vrc) || vrc == VERR_BUFFER_OVERFLOW)
                pfnLog(pLoggerRelease, "OS Service Pack: %s\n", szTmp);

            /* the package type is interesting for Linux distributions */
            char szExecName[RTPATH_MAX];
            char *pszExecName = RTProcGetExecutablePath(szExecName, sizeof(szExecName));
            pfnLog(pLoggerRelease,
                   "Executable: %s\n"
                   "Process ID: %u\n"
                   "Package type: %s"
#ifdef VBOX_OSE
                   " (OSE)"
#endif
                   "\n",
                   pszExecName ? pszExecName : "unknown",
                   RTProcSelf(),
                   VBOX_PACKAGE_STRING);
            break;
        }

        case RTLOGPHASE_PREROTATE:
            pfnLog(pLoggerRelease, "Log rotated - Log started %s\n", szTmp);
            break;

        case RTLOGPHASE_POSTROTATE:
            pfnLog(pLoggerRelease, "Log continuation - Log started %s\n", szTmp);
            break;

        case RTLOGPHASE_END:
            pfnLog(pLoggerRelease, "End of log file - Log started %s\n", szTmp);
            break;

        default:
            /* nothing */;
    }
}

/**
 * Creates the default release logger outputting to the specified file.
 *
 * @return  IPRT status code.
 * @param   pszLogFile              Filename for log output.  Optional.
 */
static int vboxGreeterLogCreate(const char *pszLogFile)
{
    /* Create release logger (stdout + file). */
    static const char * const s_apszGroups[] = VBOX_LOGGROUP_NAMES;
    RTUINT fFlags = RTLOGFLAGS_PREFIX_THREAD | RTLOGFLAGS_PREFIX_TIME_PROG;
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    fFlags |= RTLOGFLAGS_USECRLF;
#endif
    int rc = RTLogCreateEx(&g_pLoggerRelease, "VBOXGREETER_RELEASE_LOG", fFlags, "all",
                           RT_ELEMENTS(s_apszGroups), s_apszGroups, UINT32_MAX /*cMaxEntriesPerGroup*/,
                           0 /*cBufDescs*/, NULL /*paBufDescs*/, RTLOGDEST_STDOUT,
                           vboxGreeterLogHeaderFooter, g_cHistory, g_uHistoryFileSize, g_uHistoryFileTime,
                           NULL /*pOutputIf*/, NULL /*pvOutputIfUser*/,
                           NULL /*pErrInfo*/, pszLogFile);
    if (RT_SUCCESS(rc))
    {
        /* register this logger as the release logger */
        RTLogRelSetDefaultInstance(g_pLoggerRelease);

        /* Explicitly flush the log in case of VBOXGREETER_RELEASE_LOG_FLAGS=buffered. */
        RTLogFlush(g_pLoggerRelease);
    }

    return rc;
}

static void vboxGreeterLogDestroy(void)
{
    RTLogDestroy(RTLogRelSetDefaultInstance(NULL));
}

static int vboxGreeterUsage(void)
{
    RTPrintf("Usage:\n"
             " %-12s [-h|-?|--help] [-F|--logfile <file>]\n"
             "              [-v|--verbose] [-V|--version]\n", g_pszProgName);

    RTPrintf("\n"
             " Copyright (C) 2012-" VBOX_C_YEAR " " VBOX_VENDOR "\n");

    return RTEXITCODE_SYNTAX;
}

int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);
    g_pszProgName = RTPathFilename(argv[0]);

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--logfile", 'F', RTGETOPT_REQ_STRING },
        { "--verbose", 'v', RTGETOPT_REQ_NOTHING },
        { "--version", 'V', RTGETOPT_REQ_NOTHING }
    };

    char szLogFile[RTPATH_MAX + 128] = "";

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv,
                 s_aOptions, RT_ELEMENTS(s_aOptions),
                 1 /*iFirst*/, RTGETOPTINIT_FLAGS_OPTS_FIRST);

    while (   (ch = RTGetOpt(&GetState, &ValueUnion))
           && RT_SUCCESS(rc))
    {
        /* For options that require an argument, ValueUnion has received the value. */
        switch (ch)
        {
            case 'F':
                if (!RTStrPrintf(szLogFile, sizeof(szLogFile), "%s", ValueUnion.psz))
                    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to get prepare log file name");
                break;

            case 'h':
            case '?':
                return vboxGreeterUsage();

            case 'v': /* Raise verbosity. */
                g_iVerbosity++;
                break;

            case 'V': /* Print version and exit. */
                RTPrintf("%sr%s\n", RTBldCfgVersion(), RTBldCfgRevisionStr());
                return RTEXITCODE_SUCCESS;
                break; /* Never reached. */

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }

    if (RT_FAILURE(rc))
        return RTEXITCODE_SYNTAX;

    rc = VbglR3InitUser();
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to init Vbgl (%Rrc)", rc);

    rc = vboxGreeterLogCreate(strlen(szLogFile) ? szLogFile : NULL);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to create release log (%s, %Rrc)",
                              strlen(szLogFile) ? szLogFile : "<None>", rc);

    vboxGreeterLog("init\n");

    signal(SIGTERM, cb_sigterm);

    /** @todo This function already is too long. Move code into
     *        functions. */

    VBOXGREETERCTX ctx;
    RT_ZERO(ctx);

    /* UI parameters. */
    uint32_t uBgColor = 0; /* The background color. */
    uint32_t uLogonDlgHdrColor = 0;
    uint32_t uLogonDlgBgColor = 0; /* The greeter's dialog color. */
    uint32_t uLogonDlgBtnColor = 0; /* The greeter's button color. */

#ifdef VBOX_GREETER_WITH_PNG_SUPPORT
    char szBannerPath[RTPATH_MAX];
#endif

    /* By default most UI elements are shown. */
    uint32_t uOptsUI = VBOX_GREETER_UI_SHOW_RESTART
                     | VBOX_GREETER_UI_SHOW_SHUTDOWN;
#ifdef VBOX_WITH_GUEST_PROPS
    uint32_t uClientId = 0;
    rc = VbglR3GuestPropConnect(&uClientId);
    if (RT_SUCCESS(rc))
    {
        vboxGreeterLog("clientId=%RU32\n", uClientId);

        ctx.uClientId = uClientId;

        char szVal[256];
        int rc2 = vbox_read_prop(uClientId,
                                 "/VirtualBox/GuestAdd/Greeter/HideRestart",
                                 true /* Read-only on guest */,
                                 szVal, sizeof(szVal), NULL /* Timestamp. */);
        if (   RT_SUCCESS(rc2)
            && !RTStrICmp(szVal, "1"))
        {
            uOptsUI &= ~VBOX_GREETER_UI_SHOW_RESTART;
        }

        rc2 = vbox_read_prop(uClientId,
                             "/VirtualBox/GuestAdd/Greeter/HideShutdown",
                             true /* Read-only on guest */,
                             szVal, sizeof(szVal), NULL /* Timestamp. */);
        if (   RT_SUCCESS(rc2)
            && !RTStrICmp(szVal, "1"))
        {
            uOptsUI &= ~VBOX_GREETER_UI_SHOW_SHUTDOWN;
        }

# ifdef VBOX_GREETER_WITH_PNG_SUPPORT
        /* Load the banner. */
        rc2 = vbox_read_prop(uClientId,
                             "/VirtualBox/GuestAdd/Greeter/BannerPath",
                             true /* Read-only on guest */,
                             szBannerPath, sizeof(szBannerPath), NULL /* Timestamp. */);
        if (RT_SUCCESS(rc2))
        {
            if (RTFileExists(szBannerPath))
            {
                vboxGreeterLog("showing banner from '%s'\n", szBannerPath);
                uOptsUI |= VBOX_GREETER_UI_SHOW_BANNER;
            }
            else
                vboxGreeterLog("warning: unable to find banner at '%s', skipping\n", szBannerPath);
        }
# endif /* VBOX_GREETER_WITH_PNG_SUPPORT */

        /* Use theming?. */
        rc2 = vbox_read_prop(uClientId,
                             "/VirtualBox/GuestAdd/Greeter/UseTheming",
                             true /* Read-only on guest */,
                             szVal, sizeof(szVal), NULL /* Timestamp. */);
        if (   RT_SUCCESS(rc2)
            && !RTStrICmp(szVal, "1"))
        {
            vboxGreeterLog("custom theming enabled\n");
                           uOptsUI |= VBOX_GREETER_UI_USE_THEMING;
        }

        if (uOptsUI & VBOX_GREETER_UI_USE_THEMING)
        {
            /* Get background color. */
            rc2 = vbox_read_prop(uClientId,
                                 "/VirtualBox/GuestAdd/Greeter/Theme/BackgroundColor",
                                 true /* Read-only on guest */,
                                 szVal, sizeof(szVal), NULL /* Timestamp. */);
            if (RT_SUCCESS(rc2))
            {
                uBgColor = strtol(szVal, NULL,
                                  /* Change conversion base when having a 0x prefix. */
                                  RTStrStr(szVal, "0x") == szVal ? 0 : 16);
            }

            /* Logon dialog. */

            /* Get header color. */
            rc2 = vbox_read_prop(uClientId,
                                 "/VirtualBox/GuestAdd/Greeter/Theme/LogonDialog/HeaderColor",
                                 true /* Read-only on guest */,
                                 szVal, sizeof(szVal), NULL /* Timestamp. */);
            if (RT_SUCCESS(rc2))
            {
                uLogonDlgHdrColor = strtol(szVal, NULL,
                                           /* Change conversion base when having a 0x prefix. */
                                           RTStrStr(szVal, "0x") == szVal ? 0 : 16);
            }

            /* Get dialog color. */
            rc2 = vbox_read_prop(uClientId,
                                 "/VirtualBox/GuestAdd/Greeter/Theme/LogonDialog/BackgroundColor",
                                 true /* Read-only on guest */,
                                 szVal, sizeof(szVal), NULL /* Timestamp. */);
            if (RT_SUCCESS(rc2))
            {
                uLogonDlgBgColor = strtol(szVal, NULL,
                                          /* Change conversion base when having a 0x prefix. */
                                          RTStrStr(szVal, "0x") == szVal ? 0 : 16);
            }

            /* Get button color. */
            rc2 = vbox_read_prop(uClientId,
                                 "/VirtualBox/GuestAdd/Greeter/Theme/LogonDialog/ButtonColor",
                                 true /* Read-only on guest */,
                                 szVal, sizeof(szVal), NULL /* Timestamp. */);
            if (RT_SUCCESS(rc2))
            {
                uLogonDlgBtnColor = strtol(szVal, NULL,
                                           /* Change conversion base when having a 0x prefix. */
                                           RTStrStr(szVal, "0x") == szVal ? 0 : 16);
            }
        }
    }
    else
        vboxGreeterError("unable to connect to guest property service, rc=%Rrc\n", rc);
#endif
    vboxGreeterLog("UI options are: %RU32\n", uOptsUI);

#ifdef VBOX_WITH_FLTK
    int rc2 = Fl::scheme("plastic");
    if (!rc2)
        vboxGreeterLog("warning: unable to set visual scheme\n");

    Fl::visual(FL_DOUBLE | FL_INDEX);
    Fl_Double_Window *pWndMain = new Fl_Double_Window(Fl::w(), Fl::h(), "VirtualBox Guest Additions");
    AssertPtr(pWndMain);
    if (uOptsUI & VBOX_GREETER_UI_USE_THEMING)
        pWndMain->color(fl_rgb_color(VBOX_RGB_COLOR_RED(uBgColor),
                                     VBOX_RGB_COLOR_GREEN(uBgColor),
                                     VBOX_RGB_COLOR_BLUE(uBgColor)));
    else /* Default colors. */
        pWndMain->color(fl_rgb_color(0x73, 0x7F, 0x8C));

    Fl_Double_Window *pWndGreeter = new Fl_Double_Window(500, 350);
    AssertPtr(pWndGreeter);
    pWndGreeter->set_modal();
    if (uOptsUI & VBOX_GREETER_UI_USE_THEMING)
        pWndGreeter->color(fl_rgb_color(VBOX_RGB_COLOR_RED(uLogonDlgBgColor),
                                        VBOX_RGB_COLOR_GREEN(uLogonDlgBgColor),
                                        VBOX_RGB_COLOR_BLUE(uLogonDlgBgColor)));
    else /* Default colors. */
        pWndGreeter->color(fl_rgb_color(255, 255, 255));

    uint32_t uOffsetX = 130;
    /**
     * For now we're using a simple Y offset for moving all elements
     * down if a banner needs to be shown on top of the greeter. Not
     * very clean but does the job. Use some more layouting stuff
     * when this gets more complex.
     */
    uint32_t uOffsetY = 80;

# ifdef VBOX_GREETER_WITH_PNG_SUPPORT
    fl_register_images();

    /** @todo Add basic image type detection based on file
     *        extension. */

    Fl_PNG_Image *pImgBanner = NULL;
    if (uOptsUI & VBOX_GREETER_UI_SHOW_BANNER)
    {
        pImgBanner = new Fl_PNG_Image(szBannerPath);
        AssertPtr(pImgBanner);

        /** @todo Make the banner size configurable via guest
         *        properties. For now it's hardcoded to 460 x 90px. */
        Fl_Box *pBoxBanner = new Fl_Box(20, uOffsetY, 460, 90, "");
        AssertPtr(pBoxBanner);
        pBoxBanner->image(pImgBanner);

        uOffsetY = 120;
    }
# endif

    Fl_Box *pLblHeader = new Fl_Box(FL_NO_BOX, 242, uOffsetY, 300, 20,
                                    "Desktop Login");
    AssertPtr(pLblHeader);

    /** Note to use an own font:
     *  Fl_Font myfnt = FL_FREE_FONT + 1;
     *  Fl::set_font(myfnt, "MyFont"); */
    Fl_Font fntHeader = FL_FREE_FONT;
    Fl::set_font(fntHeader, "Courier");

    pLblHeader->align(FL_ALIGN_LEFT);
    pLblHeader->labelfont(FL_BOLD);
    pLblHeader->labelsize(24);
    if (uOptsUI & VBOX_GREETER_UI_USE_THEMING)
        pLblHeader->labelcolor(fl_rgb_color(VBOX_RGB_COLOR_RED(uLogonDlgHdrColor),
                                            VBOX_RGB_COLOR_GREEN(uLogonDlgHdrColor),
                                            VBOX_RGB_COLOR_BLUE(uLogonDlgHdrColor)));
    else /* Default color. */
        pLblHeader->labelcolor(fl_rgb_color(0x51, 0x5F, 0x77));
    uOffsetY += 40;

    /** @todo Add basic NLS support. */

    Fl_Input *pEdtUsername = new Fl_Input(uOffsetX, uOffsetY,
                                          300, 20, "User Name");
    AssertPtr(pEdtUsername);
    pEdtUsername->callback(cb_edt_username, &ctx);
    pEdtUsername->when(FL_WHEN_ENTER_KEY_ALWAYS);
    Fl::focus(pEdtUsername);
    ctx.pEdtUsername = pEdtUsername;

    Fl_Secret_Input *pEdtPassword = new Fl_Secret_Input(uOffsetX, uOffsetY + 40,
                                                        300, 20, "Password");
    AssertPtr(pEdtPassword);
    pEdtPassword->callback(cb_edt_password, &ctx);
    pEdtPassword->when(FL_WHEN_ENTER_KEY_ALWAYS);
    ctx.pEdtPassword = pEdtPassword;

    Fl_Button *pBtnLogin = new Fl_Button(uOffsetX, uOffsetY + 70,
                                         100, 40, "Log In");
    AssertPtr(pBtnLogin);
    pBtnLogin->callback(cb_btn_login, &ctx);
    if (uOptsUI & VBOX_GREETER_UI_USE_THEMING)
        pBtnLogin->color(fl_rgb_color(VBOX_RGB_COLOR_RED(uLogonDlgBtnColor),
                                      VBOX_RGB_COLOR_GREEN(uLogonDlgBtnColor),
                                      VBOX_RGB_COLOR_BLUE(uLogonDlgBtnColor)));
    else /* Default color. */
        pBtnLogin->color(fl_rgb_color(255, 255, 255));
    ctx.pBtnLogin = pBtnLogin;

    Fl_Menu_Button *pBtnMenu = new Fl_Menu_Button(uOffsetX + 120, uOffsetY + 70,
                                                  100, 40, "Options");
    AssertPtr(pBtnMenu);
    pBtnMenu->callback(cb_btn_menu, &ctx);
    if (uOptsUI & VBOX_GREETER_UI_USE_THEMING)
        pBtnMenu->color(fl_rgb_color(VBOX_RGB_COLOR_RED(uLogonDlgBtnColor),
                                     VBOX_RGB_COLOR_GREEN(uLogonDlgBtnColor),
                                     VBOX_RGB_COLOR_BLUE(uLogonDlgBtnColor)));
    else /* Default color. */
        pBtnMenu->color(fl_rgb_color(255, 255, 255));

    if (uOptsUI & VBOX_GREETER_UI_SHOW_RESTART)
        pBtnMenu->add("Restart", "" /* Shortcut */, cb_btn_restart, &ctx, 0 /* Flags */);
    if (uOptsUI & VBOX_GREETER_UI_SHOW_SHUTDOWN)
        pBtnMenu->add("Shutdown", "" /* Shortcut */, cb_btn_shutdown, &ctx, 0 /* Flags */);

    char szLabel[255];
    RTStrPrintf(szLabel, sizeof(szLabel), "Oracle VirtualBox Guest Additions %sr%s",
                RTBldCfgVersion(), RTBldCfgRevisionStr());
    Fl_Box *pLblInfo = new Fl_Box(FL_NO_BOX , 50, uOffsetY + 150,
                                  400, 20, szLabel);
    AssertPtr(pLblInfo);
    ctx.pLblInfo = pLblInfo;

    pWndGreeter->end();
    pWndGreeter->position((Fl::w() - pWndGreeter->w()) / 2,
                          (Fl::h() - pWndGreeter->h()) / 2);

    pWndMain->fullscreen();
    pWndMain->show(argc, argv);
    pWndMain->end();

    pWndGreeter->show();
#else /* !VBOX_WITH_FLTK */
    gtk_init(&argc, &argv);

    /* Set default cursor */
    gdk_window_set_cursor(gdk_get_default_root_window(), gdk_cursor_new(GDK_LEFT_PTR));

    GError *pError = NULL;
    GtkBuilder *pBuilder = gtk_builder_new();
    AssertPtr(pBuilder);
    if (!gtk_builder_add_from_file(pBuilder, "/usr/share/xgreeters/vbox-greeter.ui", &pError))
    {
        AssertPtr(pError);
        vboxGreeterError("unable to load UI: %s", pError->message);
        return RTEXITCODE_FAILURE;
    }

    GtkWindow *pWndGreeter = GTK_WINDOW(gtk_builder_get_object(pBuilder, VBOX_GREETER_UI_WND_GREETER));
    AssertPtr(pWndGreeter);
    GtkButton *pBtnLogin = GTK_BUTTON(gtk_builder_get_object(pBuilder, VBOX_GREETER_UI_BTN_LOGIN));
    AssertPtr(pBtnLogin);
    GtkLabel *pLblInfo = GTK_LABEL(gtk_builder_get_object(pBuilder, VBOX_GREETER_UI_LBL_INFO));
    AssertPtr(pLblInfo);

    ctx.pBuilder = pBuilder;

    g_signal_connect(G_OBJECT(pBtnLogin), "clicked", G_CALLBACK(cb_btn_login), &ctx);

    GdkRectangle rectScreen;
    gdk_screen_get_monitor_geometry(gdk_screen_get_default(), gdk_screen_get_primary_monitor(gdk_screen_get_default()), &rectScreen);
    vboxGreeterLog("monitor (default) is %dx%d\n", rectScreen.width, rectScreen.height);

    gint iWndX, iWndY;
    gtk_window_get_default_size(pWndGreeter, &iWndX, &iWndY);
    vboxGreeterLog("greeter is %dx%d\n", iWndX, iWndY);

    gtk_window_move(pWndGreeter,
                    (rectScreen.width / 2)  - (iWndX / 2),
                    (rectScreen.height / 2) - (iWndY / 2));
    gtk_widget_show(GTK_WIDGET(pWndGreeter));

    g_clear_error(&pError);
#endif /* !VBOX_WITH_FLTK */

    /* GType is needed in any case (for LightDM), whether we
     * use GTK3 or not. */
    g_type_init();

    GMainLoop *pMainLoop = g_main_loop_new(NULL, FALSE /* Not yet running */);
    AssertPtr(pMainLoop); NOREF(pMainLoop);

    LightDMGreeter *pGreeter = lightdm_greeter_new();
    AssertPtr(pGreeter);

    g_signal_connect(pGreeter, "show-prompt", G_CALLBACK(cb_lightdm_show_prompt), &ctx);
    g_signal_connect(pGreeter, "show-message", G_CALLBACK(cb_lightdm_show_message), &ctx);
    g_signal_connect(pGreeter, "authentication-complete", G_CALLBACK(cb_lightdm_auth_complete), &ctx);

    ctx.pGreeter = pGreeter;

    if (!lightdm_greeter_connect_sync(pGreeter, NULL))
    {
        vboxGreeterError("unable to connect to LightDM server, aborting\n");
        return RTEXITCODE_FAILURE;
    }

    vboxGreeterLog("connected to LightDM server\n");

#ifdef VBOX_WITH_GUEST_PROPS
    bool fCheckCreds = false;
    if (uClientId) /* Connected to guest property service? */
    {
        char szVal[256];
        rc2 = vbox_read_prop(uClientId,
                             "/VirtualBox/GuestAdd/PAM/CredsWait",
                             true /* Read-only on guest */,
                            szVal, sizeof(szVal), NULL /* Timestamp. */);
        if (RT_SUCCESS(rc2))
        {
            uint32_t uTimeoutMS = RT_INDEFINITE_WAIT; /* Wait infinite by default. */
            rc2 = vbox_read_prop(uClientId,
                                 "/VirtualBox/GuestAdd/PAM/CredsWaitTimeout",
                                 true /* Read-only on guest */,
                                 szVal, sizeof(szVal), NULL /* Timestamp. */);
            if (RT_SUCCESS(rc2))
            {
                uTimeoutMS = RTStrToUInt32(szVal);
                if (!uTimeoutMS)
                {
                    vboxGreeterError("pam_vbox_authenticate: invalid waiting timeout value specified, defaulting to infinite timeout\n");
                    uTimeoutMS = RT_INDEFINITE_WAIT;
                }
                else
                    uTimeoutMS = uTimeoutMS * 1000; /* Make ms out of s. */
            }

            ctx.uTimeoutMS = uTimeoutMS;

            rc2 = vbox_read_prop(uClientId,
                                 "/VirtualBox/GuestAdd/PAM/CredsMsgWaiting",
                                 true /* Read-only on guest */,
                                 szVal, sizeof(szVal), NULL /* Timestamp. */);
            if (RT_SUCCESS(rc2))
            {
# ifdef VBOX_WITH_FLTK
                Assert(pLblInfo);
                pLblInfo->copy_label(szVal);
# else
                gtk_label_set_text(pLblInfo, szVal);
# endif
            }

            /* Get initial timestamp so that we can compare the time
             * whether the value has been changed or not in our event callback. */
            vbox_read_prop(uClientId,
                           "/VirtualBox/GuestAdd/PAM/CredsWaitAbort",
                           true /* Read-only on guest */,
                           szVal, sizeof(szVal), &ctx.uTsAbort);

            if (RT_SUCCESS(rc))
            {
                /* Before we actuall wait for credentials just make sure we didn't already get credentials
                 * set so that we can skip waiting for them ... */
                rc2 = vboxGreeterCheckCreds(&ctx);
                if (rc2 == VERR_NOT_FOUND)
                {
                    /* Get current time stamp to later calculate rest of timeout left. */
                    ctx.uStartMS = RTTimeMilliTS();

                    fCheckCreds = true;
                }
            }
        }

        /* Start the timer to check credentials availability. */
        if (fCheckCreds)
        {
            vboxGreeterLog("No credentials available on startup, starting to check periodically ...\n");
# ifdef VBOX_WITH_FLTK
            Fl::add_timeout(0.5 /* 500 ms */, cb_check_creds, &ctx);
# else
            g_timeout_add(500 /* ms */, (GSourceFunc)cb_check_creds, &ctx);
# endif
        }
    }
#endif /* VBOX_WITH_GUEST_PROPS */

#ifdef VBOX_WITH_FLTK
    /*
     * Do own GDK main loop processing because FLTK also needs
     * to have the chance of processing its events.
     */
    GMainContext *pMainCtx = g_main_context_default();
    AssertPtr(pMainCtx);

    while (g_fRunning)
    {
        g_main_context_iteration(pMainCtx,
                                 FALSE /* No blocking */);
        Fl::check();
        RTThreadSleep(10); /* Wait a bit, don't hog the CPU too much. */
    }

    g_main_context_unref(pMainCtx);

# ifdef VBOX_GREETER_WITH_PNG_SUPPORT
    if (pImgBanner)
    {
        delete pImgBanner; /* Call destructor to free bitmap data. */
        pImgBanner = NULL;
    }
# endif /* VBOX_GREETER_WITH_PNG_SUPPORT */
#else /* !VBOX_WITH_FLTK */
    gtk_main();
    /** @todo Never reached so far. LightDM sends a SIGTERM. */
#endif /* !VBOX_WITH_FLTK */

    vboxGreeterLog("terminating\n");

#ifdef VBOX_WITH_GUEST_PROPS
    if (uClientId)
    {
        rc2 = VbglR3GuestPropDisconnect(uClientId);
        AssertRC(rc2);
    }
#endif /* VBOX_WITH_GUEST_PROPS */

    VbglR3Term();

    RTEXITCODE rcExit = RT_SUCCESS(rc)
                      ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;

    vboxGreeterLog("terminated with exit code %ld (rc=%Rrc)\n",
                   rcExit, rc);

    vboxGreeterLogDestroy();

    return rcExit;
}

#ifdef DEBUG
DECLEXPORT(void) RTAssertMsg1Weak(const char *pszExpr, unsigned uLine, const char *pszFile, const char *pszFunction)
{
    RTAssertMsg1(pszExpr, uLine, pszFile, pszFunction);
}
#endif
