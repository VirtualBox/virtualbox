/* $Id: VBoxBFE.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxBFE - The basic VirtualBox frontend for running VMs without using Main/COM/XPCOM.
 * Mainly serves as a playground for the ARMv8 VMM bringup for now.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_GUI

#ifdef RT_OS_DARWIN
# include <Carbon/Carbon.h>
# undef PVM
#endif

#include <VBox/log.h>
#include <VBox/version.h>
#include <VBox/vmm/vmmr3vtable.h>
#include <VBox/vmm/vmapi.h>
#include <VBox/vmm/pdm.h>
#include <iprt/buildconfig.h>
#include <iprt/ctype.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/semaphore.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/ldr.h>
#include <iprt/mem.h>
#include <iprt/getopt.h>
#include <iprt/env.h>
#include <iprt/errcore.h>
#include <iprt/thread.h>
#include <iprt/uuid.h>

#include <SDL.h>

#include "Display.h"
#include "Framebuffer.h"
#include "Keyboard.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

#define LogError(m,rc) \
    do { \
        Log(("VBoxBFE: ERROR: " m " [rc=0x%08X]\n", rc)); \
        RTPrintf("%s\n", m); \
    } while (0)

#define DTB_ADDR 0x40000000


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

enum TitlebarMode
{
    TITLEBAR_NORMAL   = 1,
    TITLEBAR_STARTUP  = 2,
    TITLEBAR_SAVE     = 3,
    TITLEBAR_SNAPSHOT = 4
};


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static int gHostKeyMod  = KMOD_RCTRL;
static int gHostKeySym1 = SDLK_RCTRL;
static int gHostKeySym2 = SDLK_UNKNOWN;
static bool gfGrabbed = FALSE;
static bool gfGrabOnMouseClick = TRUE;
static bool gfFullscreenResize = FALSE;
static bool gfAllowFullscreenToggle = TRUE;
static bool gfAbsoluteMouseHost = FALSE;
static bool gfAbsoluteMouseGuest = FALSE;
static bool gfRelativeMouseGuest = TRUE;
static bool gfGuestNeedsHostCursor = FALSE;
static bool gfOffCursorActive = FALSE;
static bool gfGuestNumLockPressed = FALSE;
static bool gfGuestCapsLockPressed = FALSE;
static bool gfGuestScrollLockPressed = FALSE;

/** modifier keypress status (scancode as index) */
static uint8_t gaModifiersState[256];

/** flag whether frontend should terminate */
static volatile bool    g_fTerminateFE      = false;
static RTLDRMOD         g_hModVMM           = NIL_RTLDRMOD;
static PCVMMR3VTABLE    g_pVMM              = NULL;
static PVM              g_pVM               = NULL;
static PUVM             g_pUVM              = NULL;
static uint32_t         g_u32MemorySizeMB   = 512;
static VMSTATE          g_enmVmState        = VMSTATE_CREATING;
static const char       *g_pszLoadMem       = NULL;
static const char       *g_pszLoadFlash     = NULL;
static const char       *g_pszLoadDtb       = NULL;
static const char       *g_pszSerialLog     = NULL;
static const char       *g_pszLoadKernel    = NULL;
static const char       *g_pszLoadInitrd    = NULL;
static const char       *g_pszCmdLine       = NULL;
static VMM2USERMETHODS  g_Vmm2UserMethods;
static Display          *g_pDisplay         = NULL;
static Framebuffer      *g_pFramebuffer     = NULL;
static Keyboard         *g_pKeyboard        = NULL;
static bool gfIgnoreNextResize = false;
static uint32_t gcMonitors = 1;
static SDL_TimerID gSdlResizeTimer = 0;

/** @todo currently this is only set but never read. */
static char szError[512];

extern DECL_HIDDEN_DATA(RTSEMEVENT) g_EventSemSDLEvents;
extern DECL_HIDDEN_DATA(volatile int32_t) g_cNotifyUpdateEventsPending;


/* The damned GOTOs forces this to be up here - totally out of place. */
/*
 * Host key handling.
 *
 * The golden rule is that host-key combinations should not be seen
 * by the guest. For instance a CAD should not have any extra RCtrl down
 * and RCtrl up around itself. Nor should a resume be followed by a Ctrl-P
 * that could encourage applications to start printing.
 *
 * We must not confuse the hostkey processing into any release sequences
 * either, the host key is supposed to be explicitly pressing one key.
 *
 * Quick state diagram:
 *
 *            host key down alone
 *  (Normal) ---------------
 *    ^ ^                  |
 *    | |                  v          host combination key down
 *    | |            (Host key down) ----------------
 *    | | host key up v    |                        |
 *    | |--------------    | other key down         v           host combination key down
 *    |                    |                  (host key used) -------------
 *    |                    |                        |      ^              |
 *    |              (not host key)--               |      |---------------
 *    |                    |     |  |               |
 *    |                    |     ---- other         |
 *    |  modifiers = 0     v                        v
 *    -----------------------------------------------
 */
enum HKEYSTATE
{
    /** The initial and most common state, pass keystrokes to the guest.
     * Next state: HKEYSTATE_DOWN
     * Prev state: Any */
    HKEYSTATE_NORMAL = 1,
    /** The first host key was pressed down
     */
    HKEYSTATE_DOWN_1ST,
    /** The second host key was pressed down (if gHostKeySym2 != SDLK_UNKNOWN)
     */
    HKEYSTATE_DOWN_2ND,
    /** The host key has been pressed down.
     * Prev state: HKEYSTATE_NORMAL
     * Next state: HKEYSTATE_NORMAL - host key up, capture toggle.
     * Next state: HKEYSTATE_USED   - host key combination down.
     * Next state: HKEYSTATE_NOT_IT - non-host key combination down.
     */
    HKEYSTATE_DOWN,
    /** A host key combination was pressed.
     * Prev state: HKEYSTATE_DOWN
     * Next state: HKEYSTATE_NORMAL - when modifiers are all 0
     */
    HKEYSTATE_USED,
    /** A non-host key combination was attempted. Send hostkey down to the
     * guest and continue until all modifiers have been released.
     * Prev state: HKEYSTATE_DOWN
     * Next state: HKEYSTATE_NORMAL - when modifiers are all 0
     */
    HKEYSTATE_NOT_IT
} enmHKeyState = HKEYSTATE_NORMAL;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Build the titlebar string
 */
static void UpdateTitlebar(TitlebarMode mode, uint32_t u32User = 0)
{
    static char szTitle[1024] = {0};

    /* back up current title */
    char szPrevTitle[1024];
    strcpy(szPrevTitle, szTitle);

    RTStrPrintf(szTitle, sizeof(szTitle), "%s - " VBOX_PRODUCT,
                "<noname>");

    /* which mode are we in? */
    switch (mode)
    {
        case TITLEBAR_NORMAL:
        {
            if (gfGrabbed)
                RTStrPrintf(szTitle + strlen(szTitle), sizeof(szTitle) - strlen(szTitle), " - [Input captured]");
            break;
        }

        case TITLEBAR_STARTUP:
        {
            RTStrPrintf(szTitle + strlen(szTitle), sizeof(szTitle) - strlen(szTitle),
                        " - Starting...");
            /* ignore other states, we could already be in running or aborted state */
            break;
        }

        case TITLEBAR_SAVE:
        {
            AssertMsg(u32User <= 100, ("%d\n", u32User));
            RTStrPrintf(szTitle + strlen(szTitle), sizeof(szTitle) - strlen(szTitle),
                        " - Saving %d%%...", u32User);
            break;
        }

        case TITLEBAR_SNAPSHOT:
        {
            AssertMsg(u32User <= 100, ("%d\n", u32User));
            RTStrPrintf(szTitle + strlen(szTitle), sizeof(szTitle) - strlen(szTitle),
                        " - Taking snapshot %d%%...", u32User);
            break;
        }

        default:
            RTPrintf("Error: Invalid title bar mode %d!\n", mode);
            return;
    }

    /*
     * Don't update if it didn't change.
     */
    if (!strcmp(szTitle, szPrevTitle))
        return;

    /*
     * Set the new title
     */
    g_pFramebuffer->setWindowTitle(szTitle);
}


#ifdef RT_OS_DARWIN
RT_C_DECLS_BEGIN
/* Private interface in 10.3 and later. */
typedef int CGSConnection;
typedef enum
{
    kCGSGlobalHotKeyEnable = 0,
    kCGSGlobalHotKeyDisable,
    kCGSGlobalHotKeyInvalid = -1 /* bird */
} CGSGlobalHotKeyOperatingMode;
extern CGSConnection _CGSDefaultConnection(void);
extern CGError CGSGetGlobalHotKeyOperatingMode(CGSConnection Connection, CGSGlobalHotKeyOperatingMode *enmMode);
extern CGError CGSSetGlobalHotKeyOperatingMode(CGSConnection Connection, CGSGlobalHotKeyOperatingMode enmMode);
RT_C_DECLS_END

/** Keeping track of whether we disabled the hotkeys or not. */
static bool g_fHotKeysDisabled = false;
/** Whether we've connected or not. */
static bool g_fConnectedToCGS = false;
/** Cached connection. */
static CGSConnection g_CGSConnection;

/**
 * Disables or enabled global hot keys.
 */
static void DisableGlobalHotKeys(bool fDisable)
{
    if (!g_fConnectedToCGS)
    {
        g_CGSConnection = _CGSDefaultConnection();
        g_fConnectedToCGS = true;
    }

    /* get current mode. */
    CGSGlobalHotKeyOperatingMode enmMode = kCGSGlobalHotKeyInvalid;
    CGSGetGlobalHotKeyOperatingMode(g_CGSConnection, &enmMode);

    /* calc new mode. */
    if (fDisable)
    {
        if (enmMode != kCGSGlobalHotKeyEnable)
            return;
        enmMode = kCGSGlobalHotKeyDisable;
    }
    else
    {
        if (    enmMode != kCGSGlobalHotKeyDisable
            /*||  !g_fHotKeysDisabled*/)
            return;
        enmMode = kCGSGlobalHotKeyEnable;
    }

    /* try set it and check the actual result. */
    CGSSetGlobalHotKeyOperatingMode(g_CGSConnection, enmMode);
    CGSGlobalHotKeyOperatingMode enmNewMode = kCGSGlobalHotKeyInvalid;
    CGSGetGlobalHotKeyOperatingMode(g_CGSConnection, &enmNewMode);
    if (enmNewMode == enmMode)
        g_fHotKeysDisabled = enmMode == kCGSGlobalHotKeyDisable;
}
#endif /* RT_OS_DARWIN */


/**
 * Start grabbing the mouse.
 */
static void InputGrabStart(void)
{
#ifdef RT_OS_DARWIN
    DisableGlobalHotKeys(true);
#endif
    if (!gfGuestNeedsHostCursor && gfRelativeMouseGuest)
        SDL_ShowCursor(SDL_DISABLE);
    SDL_SetRelativeMouseMode(SDL_TRUE);
    gfGrabbed = TRUE;
    UpdateTitlebar(TITLEBAR_NORMAL);
}

/**
 * End mouse grabbing.
 */
static void InputGrabEnd(void)
{
    SDL_SetRelativeMouseMode(SDL_FALSE);
    if (!gfGuestNeedsHostCursor && gfRelativeMouseGuest)
        SDL_ShowCursor(SDL_ENABLE);
#ifdef RT_OS_DARWIN
    DisableGlobalHotKeys(false);
#endif
    gfGrabbed = FALSE;
    UpdateTitlebar(TITLEBAR_NORMAL);
}



/**
 * Handles a host key down event
 */
static int HandleHostKey(const SDL_KeyboardEvent *pEv)
{
    /*
     * Revalidate the host key modifier
     */
    if ((SDL_GetModState() & ~(KMOD_MODE | KMOD_NUM | KMOD_RESERVED)) != gHostKeyMod)
        return VERR_NOT_SUPPORTED;

    /*
     * What was pressed?
     */
    switch (pEv->keysym.sym)
    {
#if 0
        /* Control-Alt-Delete */
        case SDLK_DELETE:
        {
            //g_pKeyboard->PutCAD();
            break;
        }

        /*
         * Fullscreen / Windowed toggle.
         */
        case SDLK_f:
        {
            if (   strchr(gHostKeyDisabledCombinations, 'f')
                || !gfAllowFullscreenToggle)
                return VERR_NOT_SUPPORTED;

            /*
             * We have to pause/resume the machine during this
             * process because there might be a short moment
             * without a valid framebuffer
             */
            /** @todo */
            //SetFullscreen(!g_pFramebuffer->getFullscreen());

            /*
             * We have switched from/to fullscreen, so request a full
             * screen repaint, just to be sure.
             */
            gpDisplay->InvalidateAndUpdate();
            break;
        }

        /*
         * Pause / Resume toggle.
         */
        case SDLK_p:
        {
            if (strchr(gHostKeyDisabledCombinations, 'p'))
                return VERR_NOT_SUPPORTED;

            /** @todo */
            UpdateTitlebar(TITLEBAR_NORMAL);
            break;
        }

        /*
         * Reset the VM
         */
        case SDLK_r:
        {
            if (strchr(gHostKeyDisabledCombinations, 'r'))
                return VERR_NOT_SUPPORTED;

            ResetVM();
            break;
        }

        /*
         * Terminate the VM
         */
        case SDLK_q:
        {
            if (strchr(gHostKeyDisabledCombinations, 'q'))
                return VERR_NOT_SUPPORTED;

            return VINF_EM_TERMINATE;
        }

        /*
         * Save the machine's state and exit
         */
        case SDLK_s:
        {
            if (strchr(gHostKeyDisabledCombinations, 's'))
                return VERR_NOT_SUPPORTED;

            SaveState();
            return VINF_EM_TERMINATE;
        }

        case SDLK_h:
        {
            if (strchr(gHostKeyDisabledCombinations, 'h'))
                return VERR_NOT_SUPPORTED;

            if (gpConsole)
                gpConsole->PowerButton();
            break;
        }
#endif

        case SDLK_F1: case SDLK_F2: case SDLK_F3:
        case SDLK_F4: case SDLK_F5: case SDLK_F6:
        case SDLK_F7: case SDLK_F8: case SDLK_F9:
        case SDLK_F10: case SDLK_F11: case SDLK_F12:
        {
            /* send Ctrl-Alt-Fx to guest */
            g_pKeyboard->PutUsageCode(0xE0 /*left ctrl*/, 0x07 /*usage code page id*/, FALSE);
            g_pKeyboard->PutUsageCode(0xE2 /*left alt*/, 0x07 /*usage code page id*/, FALSE);
            g_pKeyboard->PutUsageCode(pEv->keysym.sym,  0x07 /*usage code page id*/, FALSE);
            g_pKeyboard->PutUsageCode(pEv->keysym.sym,  0x07 /*usage code page id*/, TRUE);
            g_pKeyboard->PutUsageCode(0xE0 /*left ctrl*/, 0x07 /*usage code page id*/, TRUE);
            g_pKeyboard->PutUsageCode(0xE2 /*left alt*/, 0x07 /*usage code page id*/, TRUE);
            return VINF_SUCCESS;
        }

        /*
         * Not a host key combination.
         * Indicate this by returning false.
         */
        default:
            return VERR_NOT_SUPPORTED;
    }

    return VINF_SUCCESS;
}


/**
 * Releases any modifier keys that are currently in pressed state.
 */
static void ResetKeys(void)
{
    int i;

    if (!g_pKeyboard)
        return;

    for(i = 0; i < 256; i++)
    {
        if (gaModifiersState[i])
        {
            if (i & 0x80)
                g_pKeyboard->PutScancode(0xe0);
            g_pKeyboard->PutScancode(i | 0x80);
            gaModifiersState[i] = 0;
        }
    }
}

/**
 * Keyboard event handler.
 *
 * @param ev SDL keyboard event.
 */
static void ProcessKey(SDL_KeyboardEvent *ev)
{
    /* According to SDL2/SDL_scancodes.h ev->keysym.sym stores scancodes which are
    * based on USB usage page standard. This is what we can directly pass to
    * IKeyboard::putUsageCode. */
    g_pKeyboard->PutUsageCode(SDL_GetScancodeFromKey(ev->keysym.sym), 0x07 /*usage code page id*/, ev->type == SDL_KEYUP ? TRUE : FALSE);
}


/**
 * Wait for the next SDL event. Don't use SDL_WaitEvent since this function
 * calls SDL_Delay(10) if the event queue is empty.
 */
static int WaitSDLEvent(SDL_Event *event)
{
    for (;;)
    {
        int rc = SDL_PollEvent(event);
        if (rc == 1)
            return 1;
        /* Immediately wake up if new SDL events are available. This does not
         * work for internal SDL events. Don't wait more than 10ms. */
        RTSemEventWait(g_EventSemSDLEvents, 10);
    }
}


/**
 * Timer callback function to check if resizing is finished
 */
static Uint32 ResizeTimer(Uint32 interval, void *param) RT_NOTHROW_DEF
{
    RT_NOREF(interval, param);

    /* post message so the window is actually resized */
    SDL_Event event = {0};
    event.type      = SDL_USEREVENT;
    event.user.type = SDL_USER_EVENT_WINDOW_RESIZE_DONE;
    PushSDLEventForSure(&event);
    /* one-shot */
    return 0;
}


/**
 * VM state callback function. Called by the VMM
 * using its state machine states.
 *
 * Primarily used to handle VM initiated power off, suspend and state saving,
 * but also for doing termination completed work (VMSTATE_TERMINATE).
 *
 * In general this function is called in the context of the EMT.
 *
 * @todo g_enmVmState is set to VMSTATE_RUNNING before all devices have received power on events
 *       this can prematurely allow the main thread to enter the event loop
 *
 * @param   pVM         The VM handle.
 * @param   enmState    The new state.
 * @param   enmOldState The old state.
 * @param   pvUser      The user argument.
 */
static DECLCALLBACK(void) vboxbfeVmStateChangeCallback(PUVM pUVM, PCVMMR3VTABLE pVMM, VMSTATE enmState, VMSTATE enmOldState, void *pvUser)
{
    RT_NOREF(pUVM, pVMM, enmOldState, pvUser);
    LogFlow(("vmstateChangeCallback: changing state from %d to %d\n", enmOldState, enmState));
    g_enmVmState = enmState;

    switch (enmState)
    {
        /*
         * The VM has terminated
         */
        case VMSTATE_OFF:
        {
            break;
        }

        /*
         * The VM has been completely destroyed.
         *
         * Note: This state change can happen at two points:
         *       1) At the end of VMR3Destroy() if it was not called from EMT.
         *       2) At the end of vmR3EmulationThread if VMR3Destroy() was called by EMT.
         */
        case VMSTATE_TERMINATED:
        {
            break;
        }

        default: /* shut up gcc */
            break;
    }
}


/**
 * VM error callback function. Called by the various VM components.
 *
 * @param   pVM         The VM handle.
 * @param   pvUser      The user argument.
 * @param   vrc         VBox status code.
 * @param   pszFormat   Error message format string.
 * @param   args        Error message arguments.
 * @thread EMT.
 */
DECLCALLBACK(void) vboxbfeSetVMErrorCallback(PUVM pUVM, void *pvUser, int vrc, RT_SRC_POS_DECL, const char *pszFormat, va_list args)
{
    RT_NOREF(pUVM, pvUser, pszFile, iLine, pszFunction);

    /** @todo accessing shared resource without any kind of synchronization */
    if (RT_SUCCESS(vrc))
        szError[0] = '\0';
    else
    {
        va_list va2;
        va_copy(va2, args); /* Have to make a copy here or GCC will break. */
        RTStrPrintf(szError, sizeof(szError),
                    "%N!\nVBox status code: %d (%Rrc)", pszFormat, &va2, vrc, vrc);
        RTPrintf("%s\n", szError);
        va_end(va2);
    }
}


/**
 * VM Runtime error callback function. Called by the various VM components.
 *
 * @param   pVM         The VM handle.
 * @param   pvUser      The user argument.
 * @param   fFlags      The action flags. See VMSETRTERR_FLAGS_*.
 * @param   pszErrorId  Error ID string.
 * @param   pszFormat   Error message format string.
 * @param   va          Error message arguments.
 * @thread EMT.
 */
DECLCALLBACK(void) vboxbfeSetVMRuntimeErrorCallback(PUVM pUVM, void *pvUser, uint32_t fFlags,
                                                    const char *pszErrorId, const char *pszFormat, va_list va)
{
    RT_NOREF(pUVM, pvUser);

    va_list va2;
    va_copy(va2, va); /* Have to make a copy here or GCC/AMD64 will break. */
    RTPrintf("%s: %s!\n%N!\n",
             fFlags & VMSETRTERR_FLAGS_FATAL ? "Error" : "Warning",
             pszErrorId, pszFormat, &va2);
    RTStrmFlush(g_pStdErr);
    va_end(va2);
}


/**
 * Register the main drivers.
 *
 * @returns VBox status code.
 * @param   pCallbacks      Pointer to the callback table.
 * @param   u32Version      VBox version number.
 */
DECLCALLBACK(int) VBoxDriversRegister(PCPDMDRVREGCB pCallbacks, uint32_t u32Version)
{
    LogFlow(("VBoxDriversRegister: u32Version=%#x\n", u32Version));
    AssertReleaseMsg(u32Version == VBOX_VERSION, ("u32Version=%#x VBOX_VERSION=%#x\n", u32Version, VBOX_VERSION));

    int vrc = pCallbacks->pfnRegister(pCallbacks, &Display::DrvReg);
    if (RT_FAILURE(vrc))
        return vrc;

    vrc = pCallbacks->pfnRegister(pCallbacks, &Keyboard::DrvReg);
    if (RT_FAILURE(vrc))
        return vrc;

    return VINF_SUCCESS;
}


/**
 * Constructs the VMM configuration tree.
 *
 * @returns VBox status code.
 * @param   pVM     VM handle.
 */
static DECLCALLBACK(int) vboxbfeConfigConstructor(PUVM pUVM, PVM pVM, PCVMMR3VTABLE pVMM, void *pvConsole)
{
    int rcAll = VINF_SUCCESS;
    int rc;

    RT_NOREF(pvConsole);
    g_pUVM = pUVM;

#define UPDATE_RC() do { if (RT_FAILURE(rc) && RT_SUCCESS(rcAll)) rcAll = rc; } while (0)

    /*
     * Root values.
     */
    PCFGMNODE pRoot = pVMM->pfnCFGMR3GetRoot(pVM);
    rc = pVMM->pfnCFGMR3InsertString(pRoot,  "Name",           "Default VM");                   UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pRoot, "TimerMillies",   1000);                           UPDATE_RC();

    /*
     * Memory setup.
     */
    PCFGMNODE pMem = NULL;
    rc = pVMM->pfnCFGMR3InsertNode(pRoot, "MM", &pMem);                                         UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pMem, "MemRegions", &pMem);                                  UPDATE_RC();

    PCFGMNODE pMemRegion = NULL;
    rc = pVMM->pfnCFGMR3InsertNode(pMem, "Flash", &pMemRegion);                                 UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pMemRegion, "GCPhysStart",   0);                          UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pMemRegion, "Size", 64 * _1M);                            UPDATE_RC();
    if (g_pszLoadMem)
    {
        rc = pVMM->pfnCFGMR3InsertString(pMemRegion, "PrepopulateFromFile", g_pszLoadMem);      UPDATE_RC();
    }

    rc = pVMM->pfnCFGMR3InsertNode(pMem, "Conventional", &pMemRegion);                          UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pMemRegion, "GCPhysStart",   DTB_ADDR);                   UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pMemRegion, "Size", (uint64_t)g_u32MemorySizeMB * _1M);   UPDATE_RC();
    if (g_pszLoadDtb)
    {
        rc = pVMM->pfnCFGMR3InsertString(pMemRegion, "PrepopulateFromFile", g_pszLoadDtb);      UPDATE_RC();

    }

    /*
     * PDM.
     */
    rc = pVMM->pfnPDMR3DrvStaticRegistration(pVM, VBoxDriversRegister);                         UPDATE_RC();

    /*
     * Devices
     */
    PCFGMNODE pDevices = NULL;
    PCFGMNODE pDev = NULL;          /* /Devices/Dev/ */
    PCFGMNODE pInst = NULL;         /* /Devices/Dev/0/ */
    PCFGMNODE pCfg = NULL;          /* /Devices/Dev/.../Config/ */
    PCFGMNODE pLunL0 = NULL;        /* /Devices/Dev/0/LUN#0/ */
    PCFGMNODE pLunL1 = NULL;        /* /Devices/Dev/0/LUN#0/AttachedDriver/ */
    PCFGMNODE pLunL1Cfg = NULL;     /* /Devices/Dev/0/LUN#0/AttachedDriver/Config */

    rc = pVMM->pfnCFGMR3InsertNode(pRoot, "Devices", &pDevices);                             UPDATE_RC();

    rc = pVMM->pfnCFGMR3InsertNode(pDevices, "gic",          &pDev);                         UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pDev,     "0",            &pInst);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pInst, "Trusted",      1);                             UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pInst,    "Config",        &pCfg);                        UPDATE_RC();

    rc = pVMM->pfnCFGMR3InsertInteger(pCfg,  "DistributorMmioBase",       0x08000000);       UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pCfg,  "RedistributorMmioBase",     0x080a0000);       UPDATE_RC();


    rc = pVMM->pfnCFGMR3InsertNode(pDevices, "qemu-fw-cfg",   &pDev);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pDev,     "0",            &pInst);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pInst,    "Config",        &pCfg);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pCfg,  "MmioSize",       4096);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pCfg,  "MmioBase", 0x09020000);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pCfg,  "DmaEnabled",        1);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pCfg,  "QemuRamfbSupport",  1);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pInst,    "LUN#0",           &pLunL0);                    UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertString(pLunL0, "Driver",          "MainDisplay");              UPDATE_RC();
    if (g_pszLoadKernel)
    {
        rc = pVMM->pfnCFGMR3InsertString(pCfg,  "KernelImage",  g_pszLoadKernel);            UPDATE_RC();
    }
    if (g_pszLoadInitrd)
    {
        rc = pVMM->pfnCFGMR3InsertString(pCfg,  "InitrdImage",  g_pszLoadInitrd);            UPDATE_RC();
    }
    if (g_pszCmdLine)
    {
        rc = pVMM->pfnCFGMR3InsertString(pCfg,  "CmdLine",  g_pszCmdLine);                   UPDATE_RC();
    }


    rc = pVMM->pfnCFGMR3InsertNode(pDevices, "flash-cfi",         &pDev);                    UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pDev,     "0",            &pInst);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pInst,    "Config",        &pCfg);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pCfg,  "BaseAddress", 64 * _1M);                       UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pCfg,  "Size",        64 * _1M);                       UPDATE_RC();
    if (g_pszLoadFlash)
    {
        rc = pVMM->pfnCFGMR3InsertString(pCfg, "FlashFile", g_pszLoadFlash);                 UPDATE_RC();
    }

    rc = pVMM->pfnCFGMR3InsertNode(pDevices, "arm-pl011",     &pDev);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pDev,     "0",            &pInst);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pInst,    "Config",        &pCfg);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pCfg,  "Irq",               1);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pCfg,  "MmioBase", 0x09000000);                        UPDATE_RC();

    if (g_pszSerialLog)
    {
        rc = pVMM->pfnCFGMR3InsertNode(pInst,    "LUN#0",           &pLunL0);                UPDATE_RC();
        rc = pVMM->pfnCFGMR3InsertString(pLunL0, "Driver",          "Char");                 UPDATE_RC();
        rc = pVMM->pfnCFGMR3InsertNode(pLunL0,   "AttachedDriver",  &pLunL1);                UPDATE_RC();
        rc = pVMM->pfnCFGMR3InsertString(pLunL1, "Driver",          "RawFile");              UPDATE_RC();
        rc = pVMM->pfnCFGMR3InsertNode(pLunL1,    "Config",         &pLunL1Cfg);             UPDATE_RC();
        rc = pVMM->pfnCFGMR3InsertString(pLunL1Cfg, "Location",     g_pszSerialLog);         UPDATE_RC();
    }

    rc = pVMM->pfnCFGMR3InsertNode(pDevices, "arm-pl031-rtc", &pDev);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pDev,     "0",            &pInst);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pInst,    "Config",        &pCfg);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pCfg,  "Irq",               2);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pCfg,  "MmioBase", 0x09010000);                        UPDATE_RC();

    rc = pVMM->pfnCFGMR3InsertNode(pDevices, "arm-pl061-gpio",&pDev);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pDev,     "0",            &pInst);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pInst,    "Config",        &pCfg);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pCfg,  "Irq",               7);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pCfg,  "MmioBase", 0x09030000);                        UPDATE_RC();

    rc = pVMM->pfnCFGMR3InsertNode(pDevices, "pci-generic-ecam",  &pDev);                    UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pDev,     "0",            &pInst);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pInst,    "Config",        &pCfg);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pCfg,  "MmioEcamBase",   0x3f000000);                  UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pCfg,  "MmioEcamLength", 0x01000000);                  UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pCfg,  "MmioPioBase",    0x3eff0000);                  UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pCfg,  "MmioPioSize",    0x0000ffff);                  UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pCfg,  "IntPinA",        3);                           UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pCfg,  "IntPinB",        4);                           UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pCfg,  "IntPinC",        5);                           UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pCfg,  "IntPinD",        6);                           UPDATE_RC();

    rc = pVMM->pfnCFGMR3InsertNode(pDevices, "usb-xhci",      &pDev);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pDev,     "0",            &pInst);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pInst, "Trusted",           1);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pInst, "PCIBusNo",          0);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pInst, "PCIDeviceNo",       2);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pInst, "PCIFunctionNo",     0);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pInst,    "Config",        &pCfg);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pInst,    "LUN#0",       &pLunL0);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertString(pLunL0, "Driver","VUSBRootHub");                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pInst,    "LUN#1",       &pLunL0);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertString(pLunL0, "Driver","VUSBRootHub");                        UPDATE_RC();

    rc = pVMM->pfnCFGMR3InsertNode(pDevices, "e1000",         &pDev);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pDev,     "0",            &pInst);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pInst, "Trusted",           1);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pInst, "PCIBusNo",          0);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pInst, "PCIDeviceNo",       1);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pInst, "PCIFunctionNo",     0);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pInst,    "Config",        &pCfg);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pCfg,  "CableConnected",    1);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pCfg,  "LineSpeed",         0);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pCfg,  "AdapterType",       0);                        UPDATE_RC();

    const char *pszMac = "080027ede92c";
    Assert(strlen(pszMac) == 12);
    RTMAC Mac;
    RT_ZERO(Mac);
    char *pMac = (char*)&Mac;
    for (uint32_t i = 0; i < 6; ++i)
    {
        int c1 = *pszMac++ - '0';
        if (c1 > 9)
            c1 -= 7;
        int c2 = *pszMac++ - '0';
        if (c2 > 9)
            c2 -= 7;
        *pMac++ = (char)(((c1 & 0x0f) << 4) | (c2 & 0x0f));
    }
    rc = pVMM->pfnCFGMR3InsertBytes(pCfg,    "MAC",   &Mac, sizeof(Mac));                   UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pInst,    "LUN#0",       &pLunL0);                       UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertString(pLunL0, "Driver",        "NAT");                       UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pLunL0,    "Config",  &pLunL1Cfg);                       UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertString(pLunL1Cfg, "Network", "10.0.2.0/24");                  UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertString(pLunL1Cfg, "TFTPPrefix", "/Users/vbox/Library/VirtualBox/TFTP");                  UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertString(pLunL1Cfg, "BootFile", "default.pxe");                 UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pLunL1Cfg,  "AliasMode",         0);                  UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pLunL1Cfg,  "DNSProxy",          0);                  UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pLunL1Cfg,  "LocalhostReachable", 1);                 UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pLunL1Cfg,  "PassDomain",         1);                 UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pLunL1Cfg,  "UseHostResolver",    0);                 UPDATE_RC();

    PCFGMNODE pUsb = NULL;
    rc = pVMM->pfnCFGMR3InsertNode(pRoot,    "USB",           &pUsb);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pUsb,     "Msd",           &pDev);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pDev,     "0",            &pInst);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pInst, "Trusted",           1);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pInst,    "Config",        &pCfg);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pInst,    "LUN#0",       &pLunL0);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertString(pLunL0, "Driver",       "SCSI");                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pLunL0,   "AttachedDriver",  &pLunL1);                    UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertString(pLunL1, "Driver",          "VD");                       UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pLunL1,    "Config",         &pLunL1Cfg);                 UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertString(pLunL1Cfg,  "Path", "/Users/vbox/rootfs.img");          UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertString(pLunL1Cfg,  "Type", "HardDisk");                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertString(pLunL1Cfg,  "Format", "Raw");                           UPDATE_RC();

    rc = pVMM->pfnCFGMR3InsertNode(pUsb,     "HidKeyboard",   &pDev);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pDev,     "0",            &pInst);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertInteger(pInst, "Trusted",           1);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pInst,    "Config",        &pCfg);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pInst,    "LUN#0",       &pLunL0);                        UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertString(pLunL0, "Driver",       "KeyboardQueue");               UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertNode(pLunL0,   "AttachedDriver",  &pLunL1);                    UPDATE_RC();
    rc = pVMM->pfnCFGMR3InsertString(pLunL1, "Driver",          "MainKeyboard");             UPDATE_RC();

#undef UPDATE_RC
#undef UPDATE_RC

    pVMM->pfnVMR3AtRuntimeErrorRegister (pUVM, vboxbfeSetVMRuntimeErrorCallback, NULL);

    return rc;
}


/**
 * Loads the VMM if needed.
 *
 * @returns VBox status code.
 * @param   pszVmmMod       The VMM module to load.
 */
int vboxbfeLoadVMM(const char *pszVmmMod)
{
    Assert(!g_pVMM);

    RTERRINFOSTATIC ErrInfo;
    RTLDRMOD        hModVMM = NIL_RTLDRMOD;
    int vrc = SUPR3HardenedLdrLoadAppPriv(pszVmmMod, &hModVMM, RTLDRLOAD_FLAGS_LOCAL, RTErrInfoInitStatic(&ErrInfo));
    if (RT_SUCCESS(vrc))
    {
        PFNVMMGETVTABLE pfnGetVTable = NULL;
        vrc = RTLdrGetSymbol(hModVMM, VMMR3VTABLE_GETTER_NAME, (void **)&pfnGetVTable);
        if (pfnGetVTable)
        {
            PCVMMR3VTABLE pVMM = pfnGetVTable();
            if (pVMM)
            {
                if (VMMR3VTABLE_IS_COMPATIBLE(pVMM->uMagicVersion))
                {
                    if (pVMM->uMagicVersion == pVMM->uMagicVersionEnd)
                    {
                        g_hModVMM = hModVMM;
                        g_pVMM    = pVMM;
                        LogFunc(("mhLdrVMM=%p phVMM=%p uMagicVersion=%#RX64\n", hModVMM, pVMM, pVMM->uMagicVersion));
                        return VINF_SUCCESS;
                    }

                    LogRel(("Bogus VMM vtable: uMagicVersion=%#RX64 uMagicVersionEnd=%#RX64",
                            pVMM->uMagicVersion, pVMM->uMagicVersionEnd));
                }
                else
                    LogRel(("Incompatible of bogus VMM version magic: %#RX64", pVMM->uMagicVersion));
            }
            else
                LogRel(("pfnGetVTable return NULL!"));
        }
        else
            LogRel(("Failed to locate symbol '%s' in VBoxVMM: %Rrc", VMMR3VTABLE_GETTER_NAME, vrc));
        RTLdrClose(hModVMM);
    }
    else
        LogRel(("Failed to load VBoxVMM: %#RTeic", &ErrInfo.Core));

    return vrc;
}


/**
 * @interface_method_impl{VMM2USERMETHODS,pfnQueryGenericObject}
 */
static DECLCALLBACK(void *) vboxbfeVmm2User_QueryGenericObject(PCVMM2USERMETHODS pThis, PUVM pUVM, PCRTUUID pUuid)
{
    RT_NOREF(pThis, pUVM);

    if (!RTUuidCompareStr(pUuid, DISPLAY_OID))
        return g_pDisplay;

    if (!RTUuidCompareStr(pUuid, KEYBOARD_OID))
        return g_pKeyboard;

    return NULL;
}


/** VM asynchronous operations thread */
DECLCALLBACK(int) vboxbfeVMPowerUpThread(RTTHREAD hThread, void *pvUser)
{
    RT_NOREF(hThread, pvUser);

    int rc = VINF_SUCCESS;
    int rc2;

#if 0
    /*
     * Setup the release log instance in current directory.
     */
    if (g_fReleaseLog)
    {
        static const char * const s_apszGroups[] = VBOX_LOGGROUP_NAMES;
        static char s_szError[RTPATH_MAX + 128] = "";
        PRTLOGGER pLogger;
        rc2 = RTLogCreateEx(&pLogger, RTLOGFLAGS_PREFIX_TIME_PROG, "all",
                            "VBOX_RELEASE_LOG", RT_ELEMENTS(s_apszGroups), s_apszGroups, RTLOGDEST_FILE,
                            NULL /* pfnBeginEnd */, 0 /* cHistory */, 0 /* cbHistoryFileMax */, 0 /* uHistoryTimeMax */,
                            s_szError, sizeof(s_szError), "./VBoxBFE.log");
        if (RT_SUCCESS(rc2))
        {
            /* some introductory information */
            RTTIMESPEC TimeSpec;
            char szNowUct[64];
            RTTimeSpecToString(RTTimeNow(&TimeSpec), szNowUct, sizeof(szNowUct));
            RTLogRelLogger(pLogger, 0, ~0U,
                           "VBoxBFE %s (%s %s) release log\n"
                           "Log opened %s\n",
                           VBOX_VERSION_STRING, __DATE__, __TIME__,
                           szNowUct);

            /* register this logger as the release logger */
            RTLogRelSetDefaultInstance(pLogger);
        }
        else
            RTPrintf("Could not open release log (%s)\n", s_szError);
    }
#endif

    /*
     * Start VM (also from saved state) and track progress
     */
    LogFlow(("VMPowerUp\n"));

    g_Vmm2UserMethods.u32Magic                         = VMM2USERMETHODS_MAGIC;
    g_Vmm2UserMethods.u32Version                       = VMM2USERMETHODS_VERSION;
    g_Vmm2UserMethods.pfnSaveState                     = NULL;
    g_Vmm2UserMethods.pfnNotifyEmtInit                 = NULL;
    g_Vmm2UserMethods.pfnNotifyEmtTerm                 = NULL;
    g_Vmm2UserMethods.pfnNotifyPdmtInit                = NULL;
    g_Vmm2UserMethods.pfnNotifyPdmtTerm                = NULL;
    g_Vmm2UserMethods.pfnNotifyResetTurnedIntoPowerOff = NULL;
    g_Vmm2UserMethods.pfnQueryGenericObject            = vboxbfeVmm2User_QueryGenericObject;
    g_Vmm2UserMethods.u32EndMagic                      = VMM2USERMETHODS_MAGIC;

    /*
     * Create empty VM.
     */
    rc = g_pVMM->pfnVMR3Create(1, &g_Vmm2UserMethods, 0 /*fFlags*/, vboxbfeSetVMErrorCallback, NULL, vboxbfeConfigConstructor, NULL, &g_pVM, NULL);
    if (RT_FAILURE(rc))
    {
        RTPrintf("Error: VM creation failed with %Rrc.\n", rc);
        goto failure;
    }


    /*
     * Register VM state change handler
     */
    rc = g_pVMM->pfnVMR3AtStateRegister(g_pUVM, vboxbfeVmStateChangeCallback, NULL);
    if (RT_FAILURE(rc))
    {
        RTPrintf("Error: VMR3AtStateRegister failed with %Rrc.\n", rc);
        goto failure;
    }

    /*
     * Power on the VM (i.e. start executing).
     */
    if (RT_SUCCESS(rc))
    {
#if 0
        if (   g_fRestoreState
            && g_pszStateFile
            && *g_pszStateFile
            && RTPathExists(g_pszStateFile))
        {
            startProgressInfo("Restoring");
            rc = VMR3LoadFromFile(gpVM, g_pszStateFile, callProgressInfo, (uintptr_t)NULL);
            endProgressInfo();
            if (RT_SUCCESS(rc))
            {
                rc = VMR3Resume(gpVM);
                AssertRC(rc);
            }
            else
                AssertMsgFailed(("VMR3LoadFromFile failed, rc=%Rrc\n", rc));
        }
        else
#endif
        {
            rc = g_pVMM->pfnVMR3PowerOn(g_pUVM);
            if (RT_FAILURE(rc))
                AssertMsgFailed(("VMR3PowerOn failed, rc=%Rrc\n", rc));
        }
    }

    /*
     * On failure destroy the VM.
     */
    if (RT_FAILURE(rc))
        goto failure;

    return VINF_SUCCESS;

failure:
    if (g_pVM)
    {
        rc2 = g_pVMM->pfnVMR3Destroy(g_pUVM);
        AssertRC(rc2);
        g_pVM = NULL;
    }
    g_enmVmState = VMSTATE_TERMINATED;

    return VINF_SUCCESS;
}


static void show_usage()
{
    RTPrintf("Usage:\n"
             "   --start-paused     Start the VM in paused state\n"
             "\n");
}

/**
 *  Entry point.
 */
extern "C" DECLEXPORT(int) TrustedMain(int argc, char **argv, char **envp)
{
    RT_NOREF(envp);
    unsigned fPaused = 0;

    LogFlow(("VBoxBFE STARTED.\n"));
    RTPrintf(VBOX_PRODUCT " Basic Interface " VBOX_VERSION_STRING "\n"
             "Copyright (C) 2023-" VBOX_C_YEAR " " VBOX_VENDOR "\n\n");

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--start-paused",       'p', 0                   },
        { "--memory-size-mib",    'm', RTGETOPT_REQ_UINT32 },
        { "--load-file-into-ram", 'l', RTGETOPT_REQ_STRING },
        { "--load-flash",         'f', RTGETOPT_REQ_STRING },
        { "--load-dtb",           'd', RTGETOPT_REQ_STRING },
        { "--load-vmm",           'v', RTGETOPT_REQ_STRING },
        { "--load-kernel",        'k', RTGETOPT_REQ_STRING },
        { "--load-initrd",        'i', RTGETOPT_REQ_STRING },
        { "--cmd-line",           'c', RTGETOPT_REQ_STRING },
        { "--serial-log",         's', RTGETOPT_REQ_STRING },
    };

    const char *pszVmmMod = "VBoxVMM";

    /* Parse the config. */
    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0 /* fFlags */);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch(ch)
        {
            case 'p':
                fPaused = true;
                break;
            case 'm':
                g_u32MemorySizeMB = ValueUnion.u32;
                break;
            case 'l':
                g_pszLoadMem = ValueUnion.psz;
                break;
            case 'f':
                g_pszLoadFlash = ValueUnion.psz;
                break;
            case 'd':
                g_pszLoadDtb = ValueUnion.psz;
                break;
            case 'v':
                pszVmmMod = ValueUnion.psz;
                break;
            case 'k':
                g_pszLoadKernel = ValueUnion.psz;
                break;
            case 'i':
                g_pszLoadInitrd = ValueUnion.psz;
                break;
            case 'c':
                g_pszCmdLine = ValueUnion.psz;
                break;
            case 's':
                g_pszSerialLog = ValueUnion.psz;
                break;
            case 'h':
                show_usage();
                return 0;
            case 'V':
                RTPrintf("%sr%s\n", RTBldCfgVersion(), RTBldCfgRevisionStr());
                return 0;
            default:
                ch = RTGetOptPrintError(ch, &ValueUnion);
                show_usage();
                return ch;
        }
    }

    /* static initialization of the SDL stuff */
    if (!Framebuffer::init(true /*fShowSDLConfig*/))
        return RTEXITCODE_FAILURE;

    g_pKeyboard = new Keyboard();
    g_pDisplay = new Display();
    g_pFramebuffer = new Framebuffer(g_pDisplay, 0, false /*fFullscreen*/, false /*fResizable*/, true /*fShowSDLConfig*/, false,
                                     ~0, ~0, ~0, false /*fSeparate*/);
    g_pDisplay->SetFramebuffer(0, g_pFramebuffer);

    int vrc = vboxbfeLoadVMM(pszVmmMod);
    if (RT_FAILURE(vrc))
        return RTEXITCODE_FAILURE;

    /*
     * Start the VM execution thread. This has to be done
     * asynchronously as powering up can take some time
     * (accessing devices such as the host DVD drive). In
     * the meantime, we have to service the SDL event loop.
     */

    RTTHREAD thread;
    vrc = RTThreadCreate(&thread, vboxbfeVMPowerUpThread, 0, 0, RTTHREADTYPE_MAIN_WORKER, 0, "PowerUp");
    if (RT_FAILURE(vrc))
    {
        RTPrintf("Error: Thread creation failed with %d\n", vrc);
        return RTEXITCODE_FAILURE;
    }


    /* loop until the powerup processing is done */
    do
    {
        RTThreadSleep(1000);
    }
    while (   g_enmVmState == VMSTATE_CREATING
           || g_enmVmState == VMSTATE_LOADING);

    LogFlow(("VBoxSDL: Entering big event loop\n"));
    SDL_Event event;
    /** The host key down event which we have been hiding from the guest.
     * Used when going from HKEYSTATE_DOWN to HKEYSTATE_NOT_IT. */
    SDL_Event EvHKeyDown1;
    SDL_Event EvHKeyDown2;

    uint32_t uResizeWidth  = ~(uint32_t)0;
    uint32_t uResizeHeight = ~(uint32_t)0;

    while (WaitSDLEvent(&event))
    {
        switch (event.type)
        {
            /*
             * The screen needs to be repainted.
             */
            case SDL_WINDOWEVENT:
            {
                switch (event.window.event)
                {
                    case SDL_WINDOWEVENT_EXPOSED:
                    {
                        g_pFramebuffer->repaint();
                        break;
                    }
                    case SDL_WINDOWEVENT_FOCUS_GAINED:
                    {
                        break;
                    }
                    case SDL_WINDOWEVENT_FOCUS_LOST:
                    {
                        break;
                    }
                    case SDL_WINDOWEVENT_RESIZED:
                    {
                        if (g_pDisplay)
                        {
                            if (gfIgnoreNextResize)
                            {
                                gfIgnoreNextResize = FALSE;
                                break;
                            }
                            uResizeWidth  = event.window.data1;
                            uResizeHeight = event.window.data2;
                            if (gSdlResizeTimer)
                                SDL_RemoveTimer(gSdlResizeTimer);
                            gSdlResizeTimer = SDL_AddTimer(300, ResizeTimer, NULL);
                        }
                        break;
                    }
                    default:
                        break;
                }
                break;
            }

            /*
             * Keyboard events.
             */
            case SDL_KEYDOWN:
            case SDL_KEYUP:
            {
                SDL_Keycode ksym = event.key.keysym.sym;
                switch (enmHKeyState)
                {
                    case HKEYSTATE_NORMAL:
                    {
                        if (   event.type == SDL_KEYDOWN
                            && ksym != SDLK_UNKNOWN
                            && (ksym == gHostKeySym1 || ksym == gHostKeySym2))
                        {
                            EvHKeyDown1  = event;
                            enmHKeyState = ksym == gHostKeySym1 ? HKEYSTATE_DOWN_1ST
                                                                : HKEYSTATE_DOWN_2ND;
                            break;
                        }
                        ProcessKey(&event.key);
                        break;
                    }

                    case HKEYSTATE_DOWN_1ST:
                    case HKEYSTATE_DOWN_2ND:
                    {
                        if (gHostKeySym2 != SDLK_UNKNOWN)
                        {
                            if (   event.type == SDL_KEYDOWN
                                && ksym != SDLK_UNKNOWN
                                && (   (enmHKeyState == HKEYSTATE_DOWN_1ST && ksym == gHostKeySym2)
                                    || (enmHKeyState == HKEYSTATE_DOWN_2ND && ksym == gHostKeySym1)))
                            {
                                EvHKeyDown2  = event;
                                enmHKeyState = HKEYSTATE_DOWN;
                                break;
                            }
                            enmHKeyState = event.type == SDL_KEYUP ? HKEYSTATE_NORMAL
                                                                 : HKEYSTATE_NOT_IT;
                            ProcessKey(&EvHKeyDown1.key);
                            /* ugly hack: Some guests (e.g. mstsc.exe on Windows XP)
                             * expect a small delay between two key events. 5ms work
                             * reliable here so use 10ms to be on the safe side. A
                             * better but more complicated fix would be to introduce
                             * a new state and don't wait here. */
                            RTThreadSleep(10);
                            ProcessKey(&event.key);
                            break;
                        }
                    }
                    RT_FALL_THRU();

                    case HKEYSTATE_DOWN:
                    {
                        if (event.type == SDL_KEYDOWN)
                        {
                            /* potential host key combination, try execute it */
                            int irc = HandleHostKey(&event.key);
                            if (irc == VINF_SUCCESS)
                            {
                                enmHKeyState = HKEYSTATE_USED;
                                break;
                            }
                            if (RT_SUCCESS(irc))
                                goto leave;
                        }
                        else /* SDL_KEYUP */
                        {
                            if (   ksym != SDLK_UNKNOWN
                                && (ksym == gHostKeySym1 || ksym == gHostKeySym2))
                            {
                                /* toggle grabbing state */
                                if (!gfGrabbed)
                                    InputGrabStart();
                                else
                                    InputGrabEnd();

                                /* SDL doesn't always reset the keystates, correct it */
                                ResetKeys();
                                enmHKeyState = HKEYSTATE_NORMAL;
                                break;
                            }
                        }

                        /* not host key */
                        enmHKeyState = HKEYSTATE_NOT_IT;
                        ProcessKey(&EvHKeyDown1.key);
                        /* see the comment for the 2-key case above */
                        RTThreadSleep(10);
                        if (gHostKeySym2 != SDLK_UNKNOWN)
                        {
                            ProcessKey(&EvHKeyDown2.key);
                            /* see the comment for the 2-key case above */
                            RTThreadSleep(10);
                        }
                        ProcessKey(&event.key);
                        break;
                    }

                    case HKEYSTATE_USED:
                    {
                        if ((SDL_GetModState() & ~(KMOD_MODE | KMOD_NUM | KMOD_RESERVED)) == 0)
                            enmHKeyState = HKEYSTATE_NORMAL;
                        if (event.type == SDL_KEYDOWN)
                        {
                            int irc = HandleHostKey(&event.key);
                            if (RT_SUCCESS(irc) && irc != VINF_SUCCESS)
                                goto leave;
                        }
                        break;
                    }

                    default:
                        AssertMsgFailed(("enmHKeyState=%d\n", enmHKeyState));
                        RT_FALL_THRU();
                    case HKEYSTATE_NOT_IT:
                    {
                        if ((SDL_GetModState() & ~(KMOD_MODE | KMOD_NUM | KMOD_RESERVED)) == 0)
                            enmHKeyState = HKEYSTATE_NORMAL;
                        ProcessKey(&event.key);
                        break;
                    }
                } /* state switch */
                break;
            }

            /*
             * The window was closed.
             */
            case SDL_QUIT:
            {
                /** @todo */
                break;
            }

            /*
             * User specific update event.
             */
            /** @todo use a common user event handler so that SDL_PeepEvents() won't
             * possibly remove other events in the queue!
             */
            case SDL_USER_EVENT_UPDATERECT:
            {
                /*
                 * Decode event parameters.
                 */
                ASMAtomicDecS32(&g_cNotifyUpdateEventsPending);

                SDL_Rect *pUpdateRect = (SDL_Rect *)event.user.data1;
                AssertPtrBreak(pUpdateRect);

                int const x = pUpdateRect->x;
                int const y = pUpdateRect->y;
                int const w = pUpdateRect->w;
                int const h = pUpdateRect->h;

                RTMemFree(event.user.data1);

                Log3Func(("SDL_USER_EVENT_UPDATERECT: x=%d y=%d, w=%d, h=%d\n", x, y, w, h));

                Assert(g_pFramebuffer);
                g_pFramebuffer->update(x, y, w, h, true /* fGuestRelative */);
                break;
            }

            /*
             * User event: Window resize done
             */
            case SDL_USER_EVENT_WINDOW_RESIZE_DONE:
            {
                /* communicate the resize event to the guest */
                //g_pDisplay->SetVideoModeHint(0 /*=display*/, true /*=enabled*/, false /*=changeOrigin*/,
                //                             0 /*=originX*/, 0 /*=originY*/,
                //                             uResizeWidth, uResizeHeight, 0 /*=don't change bpp*/, true /*=notify*/);
                break;

            }

            /*
             * User specific framebuffer change event.
             */
            case SDL_USER_EVENT_NOTIFYCHANGE:
            {
                LogFlow(("SDL_USER_EVENT_NOTIFYCHANGE\n"));
                g_pFramebuffer->notifyChange(event.user.code);
                break;
            }

            /*
             * User specific termination event
             */
            case SDL_USER_EVENT_TERMINATE:
            {
                if (event.user.code != VBOXSDL_TERM_NORMAL)
                    RTPrintf("Error: VM terminated abnormally!\n");
                break;
            }

            default:
            {
                Log8(("unknown SDL event %d\n", event.type));
                break;
            }
        }
    }

leave:
    LogRel(("VBoxBFE: exiting\n"));
    return RT_SUCCESS(vrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


#ifndef VBOX_WITH_HARDENING
/**
 * Main entry point.
 */
int main(int argc, char **argv, char **envp)
{
    int rc = RTR3InitExe(argc, &argv, RTR3INIT_FLAGS_TRY_SUPLIB);
    if (RT_SUCCESS(rc))
        return TrustedMain(argc, argv, envp);
    RTPrintf("VBoxBFE: Runtime initialization failed: %Rrc - %Rrf\n", rc, rc);
    return RTEXITCODE_FAILURE;
}
#endif /* !VBOX_WITH_HARDENING */
