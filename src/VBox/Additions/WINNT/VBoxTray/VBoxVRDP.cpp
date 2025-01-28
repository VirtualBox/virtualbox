/* $Id: VBoxVRDP.cpp 108086 2025-01-28 09:22:12Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxVRDP - VBox VRDP connection notification
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

#include <iprt/assert.h>
#include <iprt/ldr.h>
#include <VBox/log.h>

/* 0x0501 for SPI_SETDROPSHADOW */
#define _WIN32_WINNT 0x0501
#include <iprt/win/windows.h>

#include <VBox/VMMDev.h> /* for VMMDEV_EVENT_VRDP and VRDP_EXPERIENCE_LEVEL_XXX */

#include "VBoxTray.h"
#include "VBoxHelpers.h"
#include "VBoxVRDP.h"



/* The guest receives VRDP_ACTIVE/VRDP_INACTIVE notifications.
 *
 * When VRDP_ACTIVE is received, the guest asks host about the experience level.
 * The experience level is an integer value, different values disable some GUI effects.
 *
 * On VRDP_INACTIVE the original values are restored.
 *
 * Note: that this is not controlled from the client, that is a per VM settings.
 *
 * Note: theming is disabled separately by EnableTheming.
 */

#define VBOX_SPI_STRING   0
#define VBOX_SPI_BOOL_PTR 1
#define VBOX_SPI_BOOL     2
#define VBOX_SPI_PTR      3

static ANIMATIONINFO animationInfoDisable =
{
    sizeof (ANIMATIONINFO),
    FALSE
};

typedef struct VBOXVRDPEXPPARAM
{
    const char *name;
    UINT  uActionSet;
    UINT  uActionGet;
    uint32_t level;                    /* The parameter remain enabled at this or higher level. */
    int   type;
    void  *pvDisable;
    UINT  cbSavedValue;
    char  achSavedValue[2 * MAX_PATH]; /* Large enough to save the bitmap path. */
} VBOXVRDPEXPPARAM, *PVBOXVRDPEXPPARAM;

typedef struct VBOXVRDPCONTEXT
{
    const VBOXTRAYSVCENV *pEnv;

    uint32_t level;
    BOOL fSavedThemeEnabled;

    RTLDRMOD hModUxTheme;

    HRESULT (* pfnEnableTheming)(BOOL fEnable);
    BOOL (* pfnIsThemeActive)(VOID);
} VBOXVRDPCONTEXT, *PVBOXVRDPCONTEXT;

static VBOXVRDPCONTEXT g_Ctx = { 0 };

#define SPI_(l, a) #a, SPI_SET##a, SPI_GET##a, VRDP_EXPERIENCE_LEVEL_##l

static VBOXVRDPEXPPARAM s_aSPIParams[] =
{
    { SPI_(MEDIUM, DESKWALLPAPER),           VBOX_SPI_STRING,   (void *)"" },
    { SPI_(FULL,   DROPSHADOW),              VBOX_SPI_BOOL_PTR,       },
    { SPI_(HIGH,   FONTSMOOTHING),           VBOX_SPI_BOOL,           },
    { SPI_(FULL,   MENUFADE),                VBOX_SPI_BOOL_PTR,       },
    { SPI_(FULL,   COMBOBOXANIMATION),       VBOX_SPI_BOOL_PTR,       },
    { SPI_(FULL,   CURSORSHADOW),            VBOX_SPI_BOOL_PTR,       },
    { SPI_(HIGH,   GRADIENTCAPTIONS),        VBOX_SPI_BOOL_PTR,       },
    { SPI_(FULL,   LISTBOXSMOOTHSCROLLING),  VBOX_SPI_BOOL_PTR,       },
    { SPI_(FULL,   MENUANIMATION),           VBOX_SPI_BOOL_PTR,       },
    { SPI_(FULL,   SELECTIONFADE),           VBOX_SPI_BOOL_PTR,       },
    { SPI_(FULL,   TOOLTIPANIMATION),        VBOX_SPI_BOOL_PTR,       },
    { SPI_(FULL,   ANIMATION),               VBOX_SPI_PTR,      &animationInfoDisable, sizeof (ANIMATIONINFO) },
    { SPI_(MEDIUM, DRAGFULLWINDOWS),         VBOX_SPI_BOOL,           }
};

#undef SPI_

static void vboxExperienceSet(uint32_t uLevel)
{
    for (size_t i = 0; i < RT_ELEMENTS(s_aSPIParams); i++)
    {
        PVBOXVRDPEXPPARAM pParam = &s_aSPIParams[i];
        if (pParam->level > uLevel)
        {
            /*
             * The parameter has to be disabled.
             */
            LogFlowFunc(("Saving %s\n", pParam->name));

            /* Save the current value. */
            switch (pParam->type)
            {
                case VBOX_SPI_STRING:
                {
                    /* The 2nd parameter is size in characters of the buffer.
                     * The 3rd parameter points to the buffer.
                     */
                    SystemParametersInfo (pParam->uActionGet,
                                          MAX_PATH,
                                          pParam->achSavedValue,
                                          0);
                } break;

                case VBOX_SPI_BOOL:
                case VBOX_SPI_BOOL_PTR:
                {
                    /* The 3rd parameter points to BOOL. */
                    SystemParametersInfo (pParam->uActionGet,
                                          0,
                                          pParam->achSavedValue,
                                          0);
                } break;

                case VBOX_SPI_PTR:
                {
                    /* The 3rd parameter points to the structure.
                     * The cbSize member of this structure must be set.
                     * The uiParam parameter must alos be set.
                     */
                    if (pParam->cbSavedValue > sizeof (pParam->achSavedValue))
                    {
                        LogFlowFunc(("Not enough space %d > %d\n", pParam->cbSavedValue, sizeof (pParam->achSavedValue)));
                        break;
                    }

                    *(UINT *)&pParam->achSavedValue[0] = pParam->cbSavedValue;

                    SystemParametersInfo (s_aSPIParams[i].uActionGet,
                                          s_aSPIParams[i].cbSavedValue,
                                          s_aSPIParams[i].achSavedValue,
                                          0);
                } break;

                default:
                    break;
            }

            LogFlowFunc(("Disabling %s\n", pParam->name));

            /* Disable the feature. */
            switch (pParam->type)
            {
                case VBOX_SPI_STRING:
                {
                    /* The 3rd parameter points to the string. */
                    SystemParametersInfo (pParam->uActionSet,
                                          0,
                                          pParam->pvDisable,
                                          SPIF_SENDCHANGE);
                } break;

                case VBOX_SPI_BOOL:
                {
                    /* The 2nd parameter is BOOL. */
                    SystemParametersInfo (pParam->uActionSet,
                                          FALSE,
                                          NULL,
                                          SPIF_SENDCHANGE);
                } break;

                case VBOX_SPI_BOOL_PTR:
                {
                    /* The 3rd parameter is NULL to disable. */
                    SystemParametersInfo (pParam->uActionSet,
                                          0,
                                          NULL,
                                          SPIF_SENDCHANGE);
                } break;

                case VBOX_SPI_PTR:
                {
                    /* The 3rd parameter points to the structure. */
                    SystemParametersInfo (pParam->uActionSet,
                                          0,
                                          pParam->pvDisable,
                                          SPIF_SENDCHANGE);
                } break;

                default:
                    break;
            }
        }
    }
}

static void vboxExperienceRestore(uint32_t uLevel)
{
    int i;
    for (i = 0; i < RT_ELEMENTS(s_aSPIParams); i++)
    {
        PVBOXVRDPEXPPARAM pParam = &s_aSPIParams[i];
        if (pParam->level > uLevel)
        {
            LogFlowFunc(("Restoring %s\n", pParam->name));

            /* Restore the feature. */
            switch (pParam->type)
            {
                case VBOX_SPI_STRING:
                {
                    /* The 3rd parameter points to the string. */
                    SystemParametersInfo (pParam->uActionSet,
                                          0,
                                          pParam->achSavedValue,
                                          SPIF_SENDCHANGE);
                } break;

                case VBOX_SPI_BOOL:
                {
                    /* The 2nd parameter is BOOL. */
                    SystemParametersInfo (pParam->uActionSet,
                                          *(BOOL *)&pParam->achSavedValue[0],
                                          NULL,
                                          SPIF_SENDCHANGE);
                } break;

                case VBOX_SPI_BOOL_PTR:
                {
                    /* The 3rd parameter is NULL to disable. */
                    BOOL fSaved = *(BOOL *)&pParam->achSavedValue[0];

                    SystemParametersInfo (pParam->uActionSet,
                                          0,
                                          fSaved? &fSaved: NULL,
                                          SPIF_SENDCHANGE);
                } break;

                case VBOX_SPI_PTR:
                {
                    /* The 3rd parameter points to the structure. */
                    SystemParametersInfo (pParam->uActionSet,
                                          0,
                                          pParam->achSavedValue,
                                          SPIF_SENDCHANGE);
                } break;

                default:
                    break;
            }
        }
    }
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnPreInit}
 */
static DECLCALLBACK(int) vbtrVRDPPreInit(void)
{
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnOption}
 */
static DECLCALLBACK(int) vbtrVRDPOption(const char **ppszShort, int argc, char **argv, int *pi)
{
    RT_NOREF(ppszShort, argc, argv, pi);

    return -1;
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnInit}
 */
static DECLCALLBACK(int) vbtrVRDPInit(const PVBOXTRAYSVCENV pEnv, void **ppInstance)
{
    AssertPtrReturn(pEnv, VERR_INVALID_POINTER);
    AssertPtrReturn(ppInstance, VERR_INVALID_POINTER);

    LogFlowFuncEnter();

    PVBOXVRDPCONTEXT pCtx = &g_Ctx; /* Only one instance at the moment. */
    AssertPtr(pCtx);

    pCtx->pEnv               = pEnv;
    pCtx->level              = VRDP_EXPERIENCE_LEVEL_FULL;
    pCtx->fSavedThemeEnabled = FALSE;

    int rc = RTLdrLoadSystem("UxTheme.dll", false /*fNoUnload*/, &g_Ctx.hModUxTheme);
    if (RT_SUCCESS(rc))
    {
        *(PFNRT *)&pCtx->pfnEnableTheming = RTLdrGetFunction(g_Ctx.hModUxTheme, "EnableTheming");
        *(PFNRT *)&pCtx->pfnIsThemeActive = RTLdrGetFunction(g_Ctx.hModUxTheme, "IsThemeActive");

        *ppInstance = &g_Ctx;
    }
    else
    {
        g_Ctx.hModUxTheme = NIL_RTLDRMOD;
        g_Ctx.pfnEnableTheming = NULL;
        g_Ctx.pfnIsThemeActive = NULL;
    }

    /* Tell the caller that the service does not work but it is OK to continue. */
    if (RT_FAILURE(rc))
        rc = VERR_NOT_SUPPORTED;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnDestroy}
 */
static DECLCALLBACK(void) VBoxVRDPDestroy(void *pInstance)
{
    AssertPtrReturnVoid(pInstance);

    LogFlowFuncEnter();

    PVBOXVRDPCONTEXT pCtx = (PVBOXVRDPCONTEXT)pInstance;

    vboxExperienceRestore (pCtx->level);
    if (pCtx->hModUxTheme != NIL_RTLDRMOD)
    {
        RTLdrClose(g_Ctx.hModUxTheme);
        pCtx->hModUxTheme = NIL_RTLDRMOD;
    }

    return;
}

/**
 * @interface_method_impl{VBOXTRAYSVCDESC,pfnWorker}
 */
static DECLCALLBACK(int) vbtrVRDPWorker(void *pvInstance, bool volatile *pfShutdown)
{
    AssertPtrReturn(pvInstance, VERR_INVALID_POINTER);
    PVBOXVRDPCONTEXT pCtx = (PVBOXVRDPCONTEXT)pvInstance;

    LogFlowFuncEnter();

    /*
     * Tell the control thread that it can continue spawning services.
     */
    RTThreadUserSignal(RTThreadSelf());

    int rc = VbglR3CtlFilterMask(VMMDEV_EVENT_VRDP, 0 /*fNot*/);
    if (RT_FAILURE(rc))
    {
        LogRel(("VbglR3CtlFilterMask(VMMDEV_EVENT_VRDP, 0) failed with %Rrc, exiting...\n"));
        return rc;
    }

    for (;;)
    {
        /*
         * Wait for the event, checking the shutdown flag both before and after the call.
         */
        if (*pfShutdown)
        {
            rc = VINF_SUCCESS;
            break;
        }

        uint32_t fEvent = 0;
        rc = VbglR3WaitEvent(VMMDEV_EVENT_VRDP, 5000 /*ms*/, &fEvent);

        if (*pfShutdown)
        {
            rc = VINF_SUCCESS;
            break;
        }

        if (RT_SUCCESS(rc))
        {
            /* did we get the right event? */
            if (fEvent & VMMDEV_EVENT_VRDP)
            {
                bool     fActive = false;
                uint32_t uExperienceLevel = 0;
                rc = VbglR3VrdpGetChangeRequest(&fActive, &uExperienceLevel);
                if (RT_SUCCESS(rc))
                {
                    LogFlowFunc(("u8VRDPActive = %d, level %d\n", fActive, uExperienceLevel));

                    if (fActive)
                    {
                        pCtx->level = uExperienceLevel;
                        vboxExperienceSet (pCtx->level);

                        if (pCtx->level == VRDP_EXPERIENCE_LEVEL_ZERO
                            && pCtx->pfnEnableTheming
                            && pCtx->pfnIsThemeActive)
                        {
                            pCtx->fSavedThemeEnabled = pCtx->pfnIsThemeActive ();

                            LogFlowFunc(("pCtx->fSavedThemeEnabled = %d\n", pCtx->fSavedThemeEnabled));

                            if (pCtx->fSavedThemeEnabled)
                            {
                                pCtx->pfnEnableTheming (FALSE);
                            }
                        }
                    }
                    else
                    {
                        if (pCtx->level == VRDP_EXPERIENCE_LEVEL_ZERO
                            && pCtx->pfnEnableTheming
                            && pCtx->pfnIsThemeActive)
                        {
                            if (pCtx->fSavedThemeEnabled)
                            {
                                /** @todo the call returns S_OK but theming remains disabled. */
                                HRESULT hrc = pCtx->pfnEnableTheming (TRUE);
                                LogFlowFunc(("enabling theme rc = 0x%08X\n", hrc)); NOREF(hrc);
                                pCtx->fSavedThemeEnabled = FALSE;
                            }
                        }

                        vboxExperienceRestore (pCtx->level);

                        pCtx->level = VRDP_EXPERIENCE_LEVEL_FULL;
                    }
                }
                else
                {
                    /* sleep a bit to not eat too much CPU in case the above call always fails */
                    RTThreadSleep(10);
                }
            }
        }
        /* sleep a bit to not eat too much CPU in case the above call always fails */
        else
            RTThreadSleep(50);
    }

    int rc2 = VbglR3CtlFilterMask(0 /*fOr*/, VMMDEV_EVENT_VRDP);
    if (RT_FAILURE(rc2))
        LogRel(("VbglR3CtlFilterMask(0 /*fOr*/, VMMDEV_EVENT_VRDP) failed with %Rrc\n", rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * The service description.
 */
VBOXTRAYSVCDESC g_SvcDescVRDP =
{
    /* pszName. */
    "VRDP",
    /* pszDescription. */
    "VRDP Connection Notification",
    /* pszUsage. */
    NULL,
    /* pszOptions. */
    NULL,
    /* methods */
    vbtrVRDPPreInit,
    vbtrVRDPOption,
    vbtrVRDPInit,
    vbtrVRDPWorker,
    NULL /* pfnStop */,
    VBoxVRDPDestroy
};

