/* $Id: VBoxSDL.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox frontends: VBoxSDL (simple frontend based on SDL):
 * Main code
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_GUI

#include <iprt/stream.h>

#include <VBox/com/com.h>
#include <VBox/com/string.h>
#include <VBox/com/Guid.h>
#include <VBox/com/array.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>

#include <VBox/com/NativeEventQueue.h>
#include <VBox/com/VirtualBox.h>

using namespace com;

#if defined(VBOXSDL_WITH_X11)
# include <VBox/VBoxKeyboard.h>

# include <X11/Xlib.h>
# include <X11/cursorfont.h>      /* for XC_left_ptr */
# if !defined(VBOX_WITHOUT_XCURSOR)
#  include <X11/Xcursor/Xcursor.h>
# endif
# include <unistd.h>
#endif

#include "VBoxSDL.h"

#ifdef _MSC_VER
# pragma warning(push)
# pragma warning(disable: 4121) /* warning C4121: 'SDL_SysWMmsg' : alignment of a member was sensitive to packing*/
#endif
#ifndef RT_OS_DARWIN
# include <SDL_syswm.h>          /* for SDL_GetWMInfo() */
#endif
#ifdef _MSC_VER
# pragma warning(pop)
#endif

#include "Framebuffer.h"
#include "Helper.h"

#include <VBox/types.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/log.h>
#include <VBox/version.h>
#include <VBoxVideo.h>
#include <VBox/com/listeners.h>

#include <iprt/alloca.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/env.h>
#include <iprt/file.h>
#include <iprt/ldr.h>
#include <iprt/initterm.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/uuid.h>

#include <signal.h>

#include <vector>
#include <list>

#include "PasswordInput.h"

/* Xlib would re-define our enums */
#undef True
#undef False


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Enables the rawr[0|3], patm, and casm options. */
#define VBOXSDL_ADVANCED_OPTIONS


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer shape change event data structure */
struct PointerShapeChangeData
{
    PointerShapeChangeData(BOOL aVisible, BOOL aAlpha, ULONG aXHot, ULONG aYHot,
                           ULONG aWidth, ULONG aHeight, ComSafeArrayIn(BYTE,pShape))
        : visible(aVisible), alpha(aAlpha), xHot(aXHot), yHot(aYHot),
          width(aWidth), height(aHeight)
    {
        // make a copy of the shape
        com::SafeArray<BYTE> aShape(ComSafeArrayInArg(pShape));
        size_t cbShapeSize = aShape.size();
        if (cbShapeSize > 0)
        {
            shape.resize(cbShapeSize);
            ::memcpy(shape.raw(), aShape.raw(), cbShapeSize);
        }
    }

    ~PointerShapeChangeData()
    {
    }

    const BOOL visible;
    const BOOL alpha;
    const ULONG xHot;
    const ULONG yHot;
    const ULONG width;
    const ULONG height;
    com::SafeArray<BYTE> shape;
};

enum TitlebarMode
{
    TITLEBAR_NORMAL   = 1,
    TITLEBAR_STARTUP  = 2,
    TITLEBAR_SAVE     = 3,
    TITLEBAR_SNAPSHOT = 4
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static bool    UseAbsoluteMouse(void);
static void    ResetKeys(void);
static void    ProcessKey(SDL_KeyboardEvent *ev);
static void    InputGrabStart(void);
static void    InputGrabEnd(void);
static void    SendMouseEvent(VBoxSDLFB *fb, int dz, int button, int down);
static void    UpdateTitlebar(TitlebarMode mode, uint32_t u32User = 0);
static void    SetPointerShape(const PointerShapeChangeData *data);
static void    HandleGuestCapsChanged(void);
static int     HandleHostKey(const SDL_KeyboardEvent *pEv);
static Uint32  StartupTimer(Uint32 interval, void *param) RT_NOTHROW_PROTO;
static Uint32  ResizeTimer(Uint32 interval, void *param) RT_NOTHROW_PROTO;
static Uint32  QuitTimer(Uint32 interval, void *param) RT_NOTHROW_PROTO;
static int     WaitSDLEvent(SDL_Event *event);
static void    SetFullscreen(bool enable);
static VBoxSDLFB *getFbFromWinId(Uint32 id);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static int gHostKeyMod  = KMOD_RCTRL;
static int gHostKeySym1 = SDLK_RCTRL;
static int gHostKeySym2 = SDLK_UNKNOWN;
static const char *gHostKeyDisabledCombinations = "";
static const char *gpszPidFile;
static BOOL gfGrabbed = FALSE;
static BOOL gfGrabOnMouseClick = TRUE;
static BOOL gfFullscreenResize = FALSE;
static BOOL gfIgnoreNextResize = FALSE;
static BOOL gfAllowFullscreenToggle = TRUE;
static BOOL gfAbsoluteMouseHost = FALSE;
static BOOL gfAbsoluteMouseGuest = FALSE;
static BOOL gfRelativeMouseGuest = TRUE;
static BOOL gfGuestNeedsHostCursor = FALSE;
static BOOL gfOffCursorActive = FALSE;
static BOOL gfGuestNumLockPressed = FALSE;
static BOOL gfGuestCapsLockPressed = FALSE;
static BOOL gfGuestScrollLockPressed = FALSE;
static BOOL gfACPITerm = FALSE;
#if defined(VBOXSDL_WITH_X11) && !defined(VBOX_WITHOUT_XCURSOR)
 static BOOL gfXCursorEnabled = FALSE;
#endif
static int  gcGuestNumLockAdaptions = 2;
static int  gcGuestCapsLockAdaptions = 2;
static uint32_t gmGuestNormalXRes;
static uint32_t gmGuestNormalYRes;

/** modifier keypress status (scancode as index) */
static uint8_t gaModifiersState[256];

static ComPtr<IMachine> gpMachine;
static ComPtr<IConsole> gpConsole;
static ComPtr<IMachineDebugger> gpMachineDebugger;
static ComPtr<IKeyboard> gpKeyboard;
static ComPtr<IMouse> gpMouse;
ComPtr<IDisplay> gpDisplay;
static ComPtr<IVRDEServer> gpVRDEServer;
static ComPtr<IProgress> gpProgress;

static ULONG       gcMonitors = 1;
static ComObjPtr<VBoxSDLFB> gpFramebuffer[64];
static Bstr gaFramebufferId[64];
static SDL_Cursor *gpDefaultCursor = NULL;
static SDL_Cursor *gpOffCursor = NULL;
static SDL_TimerID gSdlResizeTimer = 0;
static SDL_TimerID gSdlQuitTimer = 0;

static RTSEMEVENT g_EventSemSDLEvents;
static volatile int32_t g_cNotifyUpdateEventsPending;

/**
 * Event handler for VirtualBoxClient events
 */
class VBoxSDLClientEventListener
{
public:
    VBoxSDLClientEventListener()
    {
    }

    virtual ~VBoxSDLClientEventListener()
    {
    }

    HRESULT init()
    {
        return S_OK;
    }

    void uninit()
    {
    }

    STDMETHOD(HandleEvent)(VBoxEventType_T aType, IEvent * aEvent)
    {
        switch (aType)
        {
            case VBoxEventType_OnVBoxSVCAvailabilityChanged:
            {
                ComPtr<IVBoxSVCAvailabilityChangedEvent> pVSACEv = aEvent;
                Assert(pVSACEv);
                BOOL fAvailable = FALSE;
                pVSACEv->COMGETTER(Available)(&fAvailable);
                if (!fAvailable)
                {
                    LogRel(("VBoxSDL: VBoxSVC became unavailable, exiting.\n"));
                    RTPrintf("VBoxSVC became unavailable, exiting.\n");
                    /* Send QUIT event to terminate the VM as cleanly as possible
                     * given that VBoxSVC is no longer present. */
                    SDL_Event event = {0};
                    event.type = SDL_QUIT;
                    PushSDLEventForSure(&event);
                }
                break;
            }

            default:
                AssertFailed();
        }

        return S_OK;
    }
};

/**
 * Event handler for VirtualBox (server) events
 */
class VBoxSDLEventListener
{
public:
    VBoxSDLEventListener()
    {
    }

    virtual ~VBoxSDLEventListener()
    {
    }

    HRESULT init()
    {
        return S_OK;
    }

    void uninit()
    {
    }

    STDMETHOD(HandleEvent)(VBoxEventType_T aType, IEvent * aEvent)
    {
        RT_NOREF(aEvent);
        switch (aType)
        {
            case VBoxEventType_OnExtraDataChanged:
                break;
            default:
                AssertFailed();
        }

        return S_OK;
    }
};

/**
 * Event handler for Console events
 */
class VBoxSDLConsoleEventListener
{
public:
    VBoxSDLConsoleEventListener() : m_fIgnorePowerOffEvents(false)
    {
    }

    virtual ~VBoxSDLConsoleEventListener()
    {
    }

    HRESULT init()
    {
        return S_OK;
    }

    void uninit()
    {
    }

    STDMETHOD(HandleEvent)(VBoxEventType_T aType, IEvent * aEvent)
    {
        // likely all this double copy is now excessive, and we can just use existing event object
        /// @todo eliminate it
        switch (aType)
        {
            case VBoxEventType_OnMousePointerShapeChanged:
            {
                ComPtr<IMousePointerShapeChangedEvent> pMPSCEv = aEvent;
                Assert(pMPSCEv);
                PointerShapeChangeData *data;
                BOOL    visible,  alpha;
                ULONG   xHot, yHot, width, height;
                com::SafeArray<BYTE> shape;

                pMPSCEv->COMGETTER(Visible)(&visible);
                pMPSCEv->COMGETTER(Alpha)(&alpha);
                pMPSCEv->COMGETTER(Xhot)(&xHot);
                pMPSCEv->COMGETTER(Yhot)(&yHot);
                pMPSCEv->COMGETTER(Width)(&width);
                pMPSCEv->COMGETTER(Height)(&height);
                pMPSCEv->COMGETTER(Shape)(ComSafeArrayAsOutParam(shape));
                data = new PointerShapeChangeData(visible, alpha, xHot, yHot, width, height,
                                                  ComSafeArrayAsInParam(shape));
                Assert(data);
                if (!data)
                    break;

                SDL_Event event  = {0};
                event.type       = SDL_USEREVENT;
                event.user.type  = SDL_USER_EVENT_POINTER_CHANGE;
                event.user.data1 = data;

                int rc = PushSDLEventForSure(&event);
                if (rc)
                    delete data;

                break;
            }
            case VBoxEventType_OnMouseCapabilityChanged:
            {
                ComPtr<IMouseCapabilityChangedEvent> pMCCEv = aEvent;
                Assert(pMCCEv);
                pMCCEv->COMGETTER(SupportsAbsolute)(&gfAbsoluteMouseGuest);
                pMCCEv->COMGETTER(SupportsRelative)(&gfRelativeMouseGuest);
                pMCCEv->COMGETTER(NeedsHostCursor)(&gfGuestNeedsHostCursor);
                SDL_Event event = {0};
                event.type      = SDL_USEREVENT;
                event.user.type = SDL_USER_EVENT_GUEST_CAP_CHANGED;

                PushSDLEventForSure(&event);
                break;
            }
            case VBoxEventType_OnKeyboardLedsChanged:
            {
                ComPtr<IKeyboardLedsChangedEvent> pCLCEv = aEvent;
                Assert(pCLCEv);
                BOOL fNumLock, fCapsLock, fScrollLock;
                pCLCEv->COMGETTER(NumLock)(&fNumLock);
                pCLCEv->COMGETTER(CapsLock)(&fCapsLock);
                pCLCEv->COMGETTER(ScrollLock)(&fScrollLock);
                /* Don't bother the guest with NumLock scancodes if he doesn't set the NumLock LED */
                if (gfGuestNumLockPressed != fNumLock)
                    gcGuestNumLockAdaptions = 2;
                if (gfGuestCapsLockPressed != fCapsLock)
                    gcGuestCapsLockAdaptions = 2;
                gfGuestNumLockPressed    = fNumLock;
                gfGuestCapsLockPressed   = fCapsLock;
                gfGuestScrollLockPressed = fScrollLock;
                break;
            }

            case VBoxEventType_OnStateChanged:
            {
                ComPtr<IStateChangedEvent> pSCEv = aEvent;
                Assert(pSCEv);
                MachineState_T machineState;
                pSCEv->COMGETTER(State)(&machineState);
                LogFlow(("OnStateChange: machineState = %d (%s)\n", machineState, GetStateName(machineState)));
                SDL_Event event = {0};

                if (     machineState == MachineState_Aborted
                         ||   machineState == MachineState_Teleported
                         ||  (machineState == MachineState_Saved        && !m_fIgnorePowerOffEvents)
                         ||  (machineState == MachineState_AbortedSaved && !m_fIgnorePowerOffEvents)
                         ||  (machineState == MachineState_PoweredOff   && !m_fIgnorePowerOffEvents)
                         )
                {
                    /*
                     * We have to inform the SDL thread that the application has be terminated
                     */
                    event.type      = SDL_USEREVENT;
                    event.user.type = SDL_USER_EVENT_TERMINATE;
                    event.user.code = machineState == MachineState_Aborted
                            ? VBOXSDL_TERM_ABEND
                            : VBOXSDL_TERM_NORMAL;
                }
                else
                {
                    /*
                     * Inform the SDL thread to refresh the titlebar
                     */
                    event.type      = SDL_USEREVENT;
                    event.user.type = SDL_USER_EVENT_UPDATE_TITLEBAR;
                }

                PushSDLEventForSure(&event);
                break;
            }

            case VBoxEventType_OnRuntimeError:
            {
                ComPtr<IRuntimeErrorEvent> pRTEEv = aEvent;
                Assert(pRTEEv);
                BOOL fFatal;

                pRTEEv->COMGETTER(Fatal)(&fFatal);
                MachineState_T machineState;
                gpMachine->COMGETTER(State)(&machineState);
                const char *pszType;
                bool fPaused = machineState == MachineState_Paused;
                if (fFatal)
                    pszType = "FATAL ERROR";
                else if (machineState == MachineState_Paused)
                    pszType = "Non-fatal ERROR";
                else
                    pszType = "WARNING";
                Bstr bstrId, bstrMessage;
                pRTEEv->COMGETTER(Id)(bstrId.asOutParam());
                pRTEEv->COMGETTER(Message)(bstrMessage.asOutParam());
                RTPrintf("\n%s: ** %ls **\n%ls\n%s\n", pszType, bstrId.raw(), bstrMessage.raw(),
                         fPaused ? "The VM was paused. Continue with HostKey + P after you solved the problem.\n" : "");
                break;
            }

            case VBoxEventType_OnCanShowWindow:
            {
                ComPtr<ICanShowWindowEvent> pCSWEv = aEvent;
                Assert(pCSWEv);
#ifdef RT_OS_DARWIN
                /* SDL feature not available on Quartz */
#else
                bool fCanShow = false;
                Uint32 winId = 0;
                VBoxSDLFB *fb = getFbFromWinId(winId);
                if (fb) /* Framebuffer might not be around (yet). */
                {
                    SDL_SysWMinfo info;
                    SDL_VERSION(&info.version);
                    if (SDL_GetWindowWMInfo(fb->getWindow(), &info))
                        fCanShow = true;
                    if (fCanShow)
                        pCSWEv->AddApproval(NULL);
                    else
                        pCSWEv->AddVeto(NULL);
                }
#endif
                break;
            }

            case VBoxEventType_OnShowWindow:
            {
                ComPtr<IShowWindowEvent> pSWEv = aEvent;
                Assert(pSWEv);
                LONG64 winId = 0;
                pSWEv->COMGETTER(WinId)(&winId);
                if (winId != 0)
                    break; /* WinId already set by some other listener. */
#ifndef RT_OS_DARWIN
                SDL_SysWMinfo info;
                SDL_VERSION(&info.version);
                VBoxSDLFB *fb = getFbFromWinId(winId);
                if (SDL_GetWindowWMInfo(fb->getWindow(), &info))
                {
# if defined(VBOXSDL_WITH_X11)
                    pSWEv->COMSETTER(WinId)((LONG64)info.info.x11.window);
# elif defined(RT_OS_WINDOWS)
                    pSWEv->COMSETTER(WinId)((intptr_t)info.info.win.window);
# else /* !RT_OS_WINDOWS */
                    AssertFailed();
# endif
                }
#endif /* !RT_OS_DARWIN */
                break;
            }

            default:
                AssertFailed();
        }
        return S_OK;
    }

    static const char *GetStateName(MachineState_T machineState)
    {
        switch (machineState)
        {
            case MachineState_Null:                 return "<null>";
            case MachineState_PoweredOff:           return "PoweredOff";
            case MachineState_Saved:                return "Saved";
            case MachineState_Teleported:           return "Teleported";
            case MachineState_Aborted:              return "Aborted";
            case MachineState_AbortedSaved:         return "Aborted-Saved";
            case MachineState_Running:              return "Running";
            case MachineState_Teleporting:          return "Teleporting";
            case MachineState_LiveSnapshotting:     return "LiveSnapshotting";
            case MachineState_Paused:               return "Paused";
            case MachineState_Stuck:                return "GuruMeditation";
            case MachineState_Starting:             return "Starting";
            case MachineState_Stopping:             return "Stopping";
            case MachineState_Saving:               return "Saving";
            case MachineState_Restoring:            return "Restoring";
            case MachineState_TeleportingPausedVM:  return "TeleportingPausedVM";
            case MachineState_TeleportingIn:        return "TeleportingIn";
            case MachineState_RestoringSnapshot:    return "RestoringSnapshot";
            case MachineState_DeletingSnapshot:     return "DeletingSnapshot";
            case MachineState_SettingUp:            return "SettingUp";
            default:                                return "no idea";
        }
    }

    void ignorePowerOffEvents(bool fIgnore)
    {
        m_fIgnorePowerOffEvents = fIgnore;
    }

private:
    bool m_fIgnorePowerOffEvents;
};

typedef ListenerImpl<VBoxSDLClientEventListener>  VBoxSDLClientEventListenerImpl;
typedef ListenerImpl<VBoxSDLEventListener>        VBoxSDLEventListenerImpl;
typedef ListenerImpl<VBoxSDLConsoleEventListener> VBoxSDLConsoleEventListenerImpl;

static void show_usage()
{
    RTPrintf("Usage:\n"
             "  --startvm <uuid|name>    Virtual machine to start, either UUID or name\n"
             "  --separate               Run a separate VM process or attach to a running VM\n"
             "  --hda <file>             Set temporary first hard disk to file\n"
             "  --fda <file>             Set temporary first floppy disk to file\n"
             "  --cdrom <file>           Set temporary CDROM/DVD to file/device ('none' to unmount)\n"
             "  --boot <a|c|d|n>         Set temporary boot device (a = floppy, c = 1st HD, d = DVD, n = network)\n"
             "  --memory <size>          Set temporary memory size in megabytes\n"
             "  --vram <size>            Set temporary size of video memory in megabytes\n"
             "  --fullscreen             Start VM in fullscreen mode\n"
             "  --fullscreenresize       Resize the guest on fullscreen\n"
             "  --fixedmode <w> <h> <bpp> Use a fixed SDL video mode with given width, height and bits per pixel\n"
             "  --nofstoggle             Forbid switching to/from fullscreen mode\n"
             "  --noresize               Make the SDL frame non resizable\n"
             "  --nohostkey              Disable all hostkey combinations\n"
             "  --nohostkeys ...         Disable specific hostkey combinations, see below for valid keys\n"
             "  --nograbonclick          Disable mouse/keyboard grabbing on mouse click w/o additions\n"
             "  --detecthostkey          Get the hostkey identifier and modifier state\n"
             "  --hostkey <key> {<key2>} <mod> Set the host key to the values obtained using --detecthostkey\n"
             "  --termacpi               Send an ACPI power button event when closing the window\n"
             "  --vrdp <ports>           Listen for VRDP connections on one of specified ports (default if not specified)\n"
             "  --discardstate           Discard saved state (if present) and revert to last snapshot (if present)\n"
             "  --settingspw <pw>        Specify the settings password\n"
             "  --settingspwfile <file>  Specify a file containing the settings password\n"
#ifdef VBOXSDL_ADVANCED_OPTIONS
             "  --warpdrive <pct>        Sets the warp driver rate in percent (100 = normal)\n"
#endif
             "\n"
             "Key bindings:\n"
             "  <hostkey> +  f           Switch to full screen / restore to previous view\n"
             "               h           Press ACPI power button\n"
             "               n           Take a snapshot and continue execution\n"
             "               p           Pause / resume execution\n"
             "               q           Power off\n"
             "               r           VM reset\n"
             "               s           Save state and power off\n"
             "              <del>        Send <ctrl><alt><del>\n"
             "       <F1>...<F12>        Send <ctrl><alt><Fx>\n"
#if defined(DEBUG) || defined(VBOX_WITH_STATISTICS)
             "\n"
             "Further key bindings useful for debugging:\n"
             "  LCtrl + Alt + F12        Reset statistics counter\n"
             "  LCtrl + Alt + F11        Dump statistics to logfile\n"
             "  Alt         + F8         Toggle single step mode\n"
             "  LCtrl/RCtrl + F12        Toggle logger\n"
             "  F12                      Write log marker to logfile\n"
#endif
             "\n");
}

static void PrintError(const char *pszName, CBSTR pwszDescr, CBSTR pwszComponent=NULL)
{
    const char *pszFile, *pszFunc, *pszStat;
    char  pszBuffer[1024];
    com::ErrorInfo info;

    RTStrPrintf(pszBuffer, sizeof(pszBuffer), "%ls", pwszDescr);

    RTPrintf("\n%s! Error info:\n", pszName);
    if (   (pszFile = strstr(pszBuffer, "At '"))
        && (pszFunc = strstr(pszBuffer, ") in "))
        && (pszStat = strstr(pszBuffer, "VBox status code: ")))
        RTPrintf("  %.*s  %.*s\n  In%.*s  %s",
                 pszFile-pszBuffer, pszBuffer,
                 pszFunc-pszFile+1, pszFile,
                 pszStat-pszFunc-4, pszFunc+4,
                 pszStat);
    else
        RTPrintf("%s\n", pszBuffer);

    if (pwszComponent)
        RTPrintf("(component %ls).\n", pwszComponent);

    RTPrintf("\n");
}

#ifdef VBOXSDL_WITH_X11
/**
 * Custom signal handler. Currently it is only used to release modifier
 * keys when receiving the USR1 signal. When switching VTs, we might not
 * get release events for Ctrl-Alt and in case a savestate is performed
 * on the new VT, the VM will be saved with modifier keys stuck. This is
 * annoying enough for introducing this hack.
 */
void signal_handler_SIGUSR1(int sig, siginfo_t *info, void *secret)
{
    RT_NOREF(info, secret);

    /* only SIGUSR1 is interesting */
    if (sig == SIGUSR1)
    {
        /* just release the modifiers */
        ResetKeys();
    }
}

/**
 * Custom signal handler for catching exit events.
 */
void signal_handler_SIGINT(int sig)
{
    if (gpszPidFile)
        RTFileDelete(gpszPidFile);
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGSEGV, SIG_DFL);
    kill(getpid(), sig);
}
#endif /* VBOXSDL_WITH_X11 */

/**
 * Returns a stringified version of a keyboard modifier.
 *
 * @returns Stringified version of the keyboard modifier.
 * @param   mod                 Modifier code to return a stringified version for.
 */
static const char *keyModToStr(unsigned mod)
{
    switch (mod)
    {
        RT_CASE_RET_STR(KMOD_NONE);
        RT_CASE_RET_STR(KMOD_LSHIFT);
        RT_CASE_RET_STR(KMOD_RSHIFT);
        RT_CASE_RET_STR(KMOD_LCTRL);
        RT_CASE_RET_STR(KMOD_RCTRL);
        RT_CASE_RET_STR(KMOD_LALT);
        RT_CASE_RET_STR(KMOD_RALT);
        RT_CASE_RET_STR(KMOD_LGUI);
        RT_CASE_RET_STR(KMOD_RGUI);
        RT_CASE_RET_STR(KMOD_NUM);
        RT_CASE_RET_STR(KMOD_CAPS);
        RT_CASE_RET_STR(KMOD_MODE);
        RT_CASE_RET_STR(KMOD_SCROLL);
        default:
            break;
    }

    return "<Unknown>";
}

/**
 * Handles detecting a host key by printing its values to stdout.
 *
 * @returns RTEXITCODE
 */
static RTEXITCODE handleDetectHostKey(void)
{
    RTEXITCODE rcExit  = RTEXITCODE_SUCCESS;

    int rc = SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    if (rc == 0)
    {
        /* We need a window, otherwise we won't get any keypress events. */
        SDL_Window *pWnd = SDL_CreateWindow("VBoxSDL",
                                            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 480, SDL_WINDOW_SHOWN);
        RTPrintf("Please hit one or two function key(s) to get the --hostkey value. ..\n");
        RTPrintf("Press CTRL+C to quit.\n");
        SDL_Event e1;
        while (SDL_WaitEvent(&e1))
        {
            if (    e1.key.keysym.sym == SDLK_c
                && (e1.key.keysym.mod & KMOD_CTRL) != 0)
                break;
            if (e1.type == SDL_QUIT)
                break;
            if (e1.type == SDL_KEYDOWN)
            {
                unsigned const mod = SDL_GetModState() & ~(KMOD_MODE | KMOD_NUM | KMOD_RESERVED);
                RTPrintf("--hostkey %d", e1.key.keysym.sym);
                if (mod)
                    RTPrintf(" %d\n", mod);
                else
                    RTPrintf("\n");

                if (mod)
                    RTPrintf("Host key is '%s' + '%s'\n", keyModToStr(mod), SDL_GetKeyName(e1.key.keysym.sym));
                else
                    RTPrintf("Host key is '%s'\n", SDL_GetKeyName(e1.key.keysym.sym));
            }
        }
        SDL_DestroyWindow(pWnd);
        SDL_Quit();
    }
    else
    {
        RTPrintf("Error: SDL_InitSubSystem failed with message '%s'\n", SDL_GetError());
        rcExit = RTEXITCODE_FAILURE;
    }

    return rcExit;
}

/** entry point */
extern "C"
DECLEXPORT(int) TrustedMain(int argc, char **argv, char **envp)
{
    RT_NOREF(envp);
#ifdef RT_OS_WINDOWS
    /* As we run with the WINDOWS subsystem, we need to either attach to or create an own console
     * to get any stdout / stderr output. */
    bool fAllocConsole = IsDebuggerPresent();
    if (!fAllocConsole)
    {
        if (!AttachConsole(ATTACH_PARENT_PROCESS))
            fAllocConsole = true;
    }

    if (fAllocConsole)
    {
        if (!AllocConsole())
            MessageBox(GetDesktopWindow(), L"Unable to attach to or allocate a console!", L"VBoxSDL", MB_OK | MB_ICONERROR);
        /* Continue running. */
    }

    RTFILE hStdIn;
    RTFileFromNative(&hStdIn,  (RTHCINTPTR)GetStdHandle(STD_INPUT_HANDLE));
    /** @todo Closing of standard handles not support via IPRT (yet). */
    RTStrmOpenFileHandle(hStdIn, "r", 0, &g_pStdIn);

    RTFILE hStdOut;
    RTFileFromNative(&hStdOut,  (RTHCINTPTR)GetStdHandle(STD_OUTPUT_HANDLE));
    /** @todo Closing of standard handles not support via IPRT (yet). */
    RTStrmOpenFileHandle(hStdOut, "wt", 0, &g_pStdOut);

    RTFILE hStdErr;
    RTFileFromNative(&hStdErr,  (RTHCINTPTR)GetStdHandle(STD_ERROR_HANDLE));
    RTStrmOpenFileHandle(hStdErr, "wt", 0, &g_pStdErr);

    if (!fAllocConsole) /* When attaching to the parent console, make sure we start on a fresh line. */
        RTPrintf("\n");

    ATL::CComModule _Module; /* Required internally by ATL (constructor records instance in global variable). */
#endif /* RT_OS_WINDOWS */

#ifdef Q_WS_X11
    if (!XInitThreads())
        return 1;
#endif
#ifdef VBOXSDL_WITH_X11
    /*
     * Lock keys on SDL behave different from normal keys: A KeyPress event is generated
     * if the lock mode gets active and a keyRelease event is generated if the lock mode
     * gets inactive, that is KeyPress and KeyRelease are sent when pressing the lock key
     * to change the mode. The current lock mode is reflected in SDL_GetModState().
     *
     * Debian patched libSDL to make the lock keys behave like normal keys
     * generating a KeyPress/KeyRelease event if the lock key was
     * pressed/released.  With the new behaviour, the lock status is not
     * reflected in the mod status anymore, but the user can request the old
     * behaviour by setting an environment variable.  To confuse matters further
     * version 1.2.14 (fortunately including the Debian packaged versions)
     * adopted the Debian behaviour officially, but inverted the meaning of the
     * environment variable to select the new behaviour, keeping the old as the
     * default.  We disable the new behaviour to ensure a defined environment
     * and work around the missing KeyPress/KeyRelease events in ProcessKeys().
     */
    {
#if 0
        const SDL_version *pVersion = SDL_Linked_Version();
        if (  SDL_VERSIONNUM(pVersion->major, pVersion->minor, pVersion->patch)
            < SDL_VERSIONNUM(1, 2, 14))
            RTEnvSet("SDL_DISABLE_LOCK_KEYS", "1");
#endif
    }
#endif

    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;

    /*
     * the hostkey detection mode is unrelated to VM processing, so handle it before
     * we initialize anything COM related
     */
    if (argc == 2 && (   !strcmp(argv[1], "-detecthostkey")
                      || !strcmp(argv[1], "--detecthostkey")))
    {
        rcExit = handleDetectHostKey();
#ifdef RT_OS_WINDOWS
        FreeConsole(); /* Detach or destroy (from) console. */
#endif
        return rcExit;
    }

    /** @todo r=andy This function is waaaaaay to long, uses goto's and leaks stuff. Use RTGetOpt handling. */

    HRESULT hrc;
    int vrc;
    Guid uuidVM;
    char *vmName = NULL;
    bool fSeparate = false;
    DeviceType_T bootDevice = DeviceType_Null;
    uint32_t memorySize = 0;
    uint32_t vramSize = 0;
    ComPtr<IEventListener> pVBoxClientListener;
    ComPtr<IEventListener> pVBoxListener;
    ComObjPtr<VBoxSDLConsoleEventListenerImpl> pConsoleListener;

    bool fFullscreen = false;
    bool fResizable = true;
#ifdef USE_XPCOM_QUEUE_THREAD
    bool fXPCOMEventThreadSignaled = false;
#endif
    const char *pcszHdaFile   = NULL;
    const char *pcszCdromFile = NULL;
    const char *pcszFdaFile   = NULL;
    const char *pszPortVRDP = NULL;
    bool fDiscardState = false;
    const char *pcszSettingsPw = NULL;
    const char *pcszSettingsPwFile = NULL;
#ifdef VBOXSDL_ADVANCED_OPTIONS
    uint32_t u32WarpDrive = 0;
#endif
#ifdef VBOX_WIN32_UI
    bool fWin32UI = true;
    int64_t winId = 0;
#endif
    bool fShowSDLConfig    = false;
    uint32_t fixedWidth    = ~(uint32_t)0;
    uint32_t fixedHeight   = ~(uint32_t)0;
    uint32_t fixedBPP      = ~(uint32_t)0;
    uint32_t uResizeWidth  = ~(uint32_t)0;
    uint32_t uResizeHeight = ~(uint32_t)0;

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
    /** The host key down event which we have been hiding from the guest.
     * Used when going from HKEYSTATE_DOWN to HKEYSTATE_NOT_IT. */
    SDL_Event EvHKeyDown1;
    SDL_Event EvHKeyDown2;

    LogFlow(("SDL GUI started\n"));
    RTPrintf(VBOX_PRODUCT " SDL GUI version %s\n"
             "Copyright (C) 2005-" VBOX_C_YEAR " " VBOX_VENDOR "\n",
             VBOX_VERSION_STRING);

    // less than one parameter is not possible
    if (argc < 2)
    {
        show_usage();
        return 1;
    }

    // command line argument parsing stuff
    for (int curArg = 1; curArg < argc; curArg++)
    {
        if (   !strcmp(argv[curArg], "--vm")
            || !strcmp(argv[curArg], "-vm")
            || !strcmp(argv[curArg], "--startvm")
            || !strcmp(argv[curArg], "-startvm")
            || !strcmp(argv[curArg], "-s")
            )
        {
            if (++curArg >= argc)
            {
                RTPrintf("Error: VM not specified (UUID or name)!\n");
                return 1;
            }
            // first check if a UUID was supplied
            uuidVM = argv[curArg];

            if (!uuidVM.isValid())
            {
                LogFlow(("invalid UUID format, assuming it's a VM name\n"));
                vmName = argv[curArg];
            }
            else if (uuidVM.isZero())
            {
                RTPrintf("Error: UUID argument is zero!\n");
                return 1;
            }
        }
        else if (   !strcmp(argv[curArg], "--separate")
                 || !strcmp(argv[curArg], "-separate"))
        {
            fSeparate = true;
        }
        else if (   !strcmp(argv[curArg], "--comment")
                 || !strcmp(argv[curArg], "-comment"))
        {
            if (++curArg >= argc)
            {
                RTPrintf("Error: missing argument for comment!\n");
                return 1;
            }
        }
        else if (   !strcmp(argv[curArg], "--boot")
                 || !strcmp(argv[curArg], "-boot"))
        {
            if (++curArg >= argc)
            {
                RTPrintf("Error: missing argument for boot drive!\n");
                return 1;
            }
            switch (argv[curArg][0])
            {
                case 'a':
                {
                    bootDevice = DeviceType_Floppy;
                    break;
                }

                case 'c':
                {
                    bootDevice = DeviceType_HardDisk;
                    break;
                }

                case 'd':
                {
                    bootDevice = DeviceType_DVD;
                    break;
                }

                case 'n':
                {
                    bootDevice = DeviceType_Network;
                    break;
                }

                default:
                {
                    RTPrintf("Error: wrong argument for boot drive!\n");
                    return 1;
                }
            }
        }
        else if (   !strcmp(argv[curArg], "--detecthostkey")
                 || !strcmp(argv[curArg], "-detecthostkey"))
        {
            RTPrintf("Error: please specify \"%s\" without any additional parameters!\n",
                     argv[curArg]);
            return 1;
        }
        else if (   !strcmp(argv[curArg], "--memory")
                 || !strcmp(argv[curArg], "-memory")
                 || !strcmp(argv[curArg], "-m"))
        {
            if (++curArg >= argc)
            {
                RTPrintf("Error: missing argument for memory size!\n");
                return 1;
            }
            memorySize = atoi(argv[curArg]);
        }
        else if (   !strcmp(argv[curArg], "--vram")
                 || !strcmp(argv[curArg], "-vram"))
        {
            if (++curArg >= argc)
            {
                RTPrintf("Error: missing argument for vram size!\n");
                return 1;
            }
            vramSize = atoi(argv[curArg]);
        }
        else if (   !strcmp(argv[curArg], "--fullscreen")
                 || !strcmp(argv[curArg], "-fullscreen"))
        {
            fFullscreen = true;
        }
        else if (   !strcmp(argv[curArg], "--fullscreenresize")
                 || !strcmp(argv[curArg], "-fullscreenresize"))
        {
            gfFullscreenResize = true;
#ifdef VBOXSDL_WITH_X11
            RTEnvSet("SDL_VIDEO_X11_VIDMODE", "0");
#endif
        }
        else if (   !strcmp(argv[curArg], "--fixedmode")
                 || !strcmp(argv[curArg], "-fixedmode"))
        {
            /* three parameters follow */
            if (curArg + 3 >= argc)
            {
                RTPrintf("Error: missing arguments for fixed video mode!\n");
                return 1;
            }
            fixedWidth  = atoi(argv[++curArg]);
            fixedHeight = atoi(argv[++curArg]);
            fixedBPP    = atoi(argv[++curArg]);
        }
        else if (   !strcmp(argv[curArg], "--nofstoggle")
                 || !strcmp(argv[curArg], "-nofstoggle"))
        {
            gfAllowFullscreenToggle = FALSE;
        }
        else if (   !strcmp(argv[curArg], "--noresize")
                 || !strcmp(argv[curArg], "-noresize"))
        {
            fResizable = false;
        }
        else if (   !strcmp(argv[curArg], "--nohostkey")
                 || !strcmp(argv[curArg], "-nohostkey"))
        {
            gHostKeyMod  = 0;
            gHostKeySym1 = 0;
        }
        else if (   !strcmp(argv[curArg], "--nohostkeys")
                 || !strcmp(argv[curArg], "-nohostkeys"))
        {
            if (++curArg >= argc)
            {
                RTPrintf("Error: missing a string of disabled hostkey combinations\n");
                return 1;
            }
            gHostKeyDisabledCombinations = argv[curArg];
            size_t cch = strlen(gHostKeyDisabledCombinations);
            for (size_t i = 0; i < cch; i++)
            {
                if (!strchr("fhnpqrs", gHostKeyDisabledCombinations[i]))
                {
                    RTPrintf("Error: <hostkey> + '%c' is not a valid combination\n",
                             gHostKeyDisabledCombinations[i]);
                    return 1;
                }
            }
        }
        else if (   !strcmp(argv[curArg], "--nograbonclick")
                 || !strcmp(argv[curArg], "-nograbonclick"))
        {
            gfGrabOnMouseClick = FALSE;
        }
        else if (   !strcmp(argv[curArg], "--termacpi")
                 || !strcmp(argv[curArg], "-termacpi"))
        {
            gfACPITerm = TRUE;
        }
        else if (   !strcmp(argv[curArg], "--pidfile")
                 || !strcmp(argv[curArg], "-pidfile"))
        {
            if (++curArg >= argc)
            {
                RTPrintf("Error: missing file name for --pidfile!\n");
                return 1;
            }
            gpszPidFile = argv[curArg];
        }
        else if (   !strcmp(argv[curArg], "--hda")
                 || !strcmp(argv[curArg], "-hda"))
        {
            if (++curArg >= argc)
            {
                RTPrintf("Error: missing file name for first hard disk!\n");
                return 1;
            }
            /* resolve it. */
            if (RTPathExists(argv[curArg]))
                pcszHdaFile = RTPathRealDup(argv[curArg]);
            if (!pcszHdaFile)
            {
                RTPrintf("Error: The path to the specified harddisk, '%s', could not be resolved.\n", argv[curArg]);
                return 1;
            }
        }
        else if (   !strcmp(argv[curArg], "--fda")
                 || !strcmp(argv[curArg], "-fda"))
        {
            if (++curArg >= argc)
            {
                RTPrintf("Error: missing file/device name for first floppy disk!\n");
                return 1;
            }
            /* resolve it. */
            if (RTPathExists(argv[curArg]))
                pcszFdaFile = RTPathRealDup(argv[curArg]);
            if (!pcszFdaFile)
            {
                RTPrintf("Error: The path to the specified floppy disk, '%s', could not be resolved.\n", argv[curArg]);
                return 1;
            }
        }
        else if (   !strcmp(argv[curArg], "--cdrom")
                 || !strcmp(argv[curArg], "-cdrom"))
        {
            if (++curArg >= argc)
            {
                RTPrintf("Error: missing file/device name for cdrom!\n");
                return 1;
            }
            /* resolve it. */
            if (RTPathExists(argv[curArg]))
                pcszCdromFile = RTPathRealDup(argv[curArg]);
            if (!pcszCdromFile)
            {
                RTPrintf("Error: The path to the specified cdrom, '%s', could not be resolved.\n", argv[curArg]);
                return 1;
            }
        }
        else if (   !strcmp(argv[curArg], "--vrdp")
                 || !strcmp(argv[curArg], "-vrdp"))
        {
            // start with the standard VRDP port
            pszPortVRDP = "0";

            // is there another argument
            if (argc > (curArg + 1))
            {
                curArg++;
                pszPortVRDP = argv[curArg];
                LogFlow(("Using non standard VRDP port %s\n", pszPortVRDP));
            }
        }
        else if (   !strcmp(argv[curArg], "--discardstate")
                 || !strcmp(argv[curArg], "-discardstate"))
        {
            fDiscardState = true;
        }
        else if (!strcmp(argv[curArg], "--settingspw"))
        {
            if (++curArg >= argc)
            {
                RTPrintf("Error: missing password");
                return 1;
            }
            pcszSettingsPw = argv[curArg];
        }
        else if (!strcmp(argv[curArg], "--settingspwfile"))
        {
            if (++curArg >= argc)
            {
                RTPrintf("Error: missing password file\n");
                return 1;
            }
            pcszSettingsPwFile = argv[curArg];
        }
#ifdef VBOXSDL_ADVANCED_OPTIONS
        else if (   !strcmp(argv[curArg], "--warpdrive")
                 || !strcmp(argv[curArg], "-warpdrive"))
        {
            if (++curArg >= argc)
            {
                RTPrintf("Error: missing the rate value for the --warpdrive option!\n");
                return 1;
            }
            u32WarpDrive = RTStrToUInt32(argv[curArg]);
            if (u32WarpDrive < 2 || u32WarpDrive > 20000)
            {
                RTPrintf("Error: the warp drive rate is restricted to [2..20000]. (%d)\n", u32WarpDrive);
                return 1;
            }
        }
#endif /* VBOXSDL_ADVANCED_OPTIONS */
#ifdef VBOX_WIN32_UI
        else if (   !strcmp(argv[curArg], "--win32ui")
                 || !strcmp(argv[curArg], "-win32ui"))
            fWin32UI = true;
#endif
        else if (   !strcmp(argv[curArg], "--showsdlconfig")
                 || !strcmp(argv[curArg], "-showsdlconfig"))
            fShowSDLConfig = true;
        else if (   !strcmp(argv[curArg], "--hostkey")
                 || !strcmp(argv[curArg], "-hostkey"))
        {
            if (++curArg + 1 >= argc)
            {
                RTPrintf("Error: not enough arguments for host keys!\n");
                return 1;
            }
            gHostKeySym1 = atoi(argv[curArg++]);
            if (curArg + 1 < argc && (argv[curArg+1][0] == '0' || atoi(argv[curArg+1]) > 0))
            {
                /* two-key sequence as host key specified */
                gHostKeySym2 = atoi(argv[curArg++]);
            }
            gHostKeyMod = atoi(argv[curArg]);
        }
        /* just show the help screen */
        else
        {
            if (   strcmp(argv[curArg], "-h")
                && strcmp(argv[curArg], "-help")
                && strcmp(argv[curArg], "--help"))
                RTPrintf("Error: unrecognized switch '%s'\n", argv[curArg]);
            show_usage();
            return 1;
        }
    }

    hrc = com::Initialize();
#ifdef VBOX_WITH_XPCOM
    if (hrc == NS_ERROR_FILE_ACCESS_DENIED)
    {
        char szHome[RTPATH_MAX] = "";
        com::GetVBoxUserHomeDirectory(szHome, sizeof(szHome));
        RTPrintf("Failed to initialize COM because the global settings directory '%s' is not accessible!\n", szHome);
        return 1;
    }
#endif
    if (FAILED(hrc))
    {
        RTPrintf("Error: COM initialization failed (rc=%Rhrc)!\n", hrc);
        return 1;
    }

    /* NOTE: do not convert the following scope to a "do {} while (0);", as
     * this would make it all too tempting to use "break;" incorrectly - it
     * would skip over the cleanup. */
    {
    // scopes all the stuff till shutdown
    ////////////////////////////////////////////////////////////////////////////

    ComPtr<IVirtualBoxClient> pVirtualBoxClient;
    ComPtr<IVirtualBox> pVirtualBox;
    ComPtr<ISession> pSession;
    bool sessionOpened = false;
    NativeEventQueue* eventQ = com::NativeEventQueue::getMainEventQueue();

    ComPtr<IMachine> pMachine;
    ComPtr<IGraphicsAdapter> pGraphicsAdapter;

    hrc = pVirtualBoxClient.createInprocObject(CLSID_VirtualBoxClient);
    if (FAILED(hrc))
    {
        com::ErrorInfo info;
        if (info.isFullAvailable())
            PrintError("Failed to create VirtualBoxClient object",
                       info.getText().raw(), info.getComponent().raw());
        else
            RTPrintf("Failed to create VirtualBoxClient object! No error information available (rc=%Rhrc).\n", hrc);
        goto leave;
    }

    hrc = pVirtualBoxClient->COMGETTER(VirtualBox)(pVirtualBox.asOutParam());
    if (FAILED(hrc))
    {
        RTPrintf("Failed to get VirtualBox object (rc=%Rhrc)!\n", hrc);
        goto leave;
    }
    hrc = pVirtualBoxClient->COMGETTER(Session)(pSession.asOutParam());
    if (FAILED(hrc))
    {
        RTPrintf("Failed to get session object (rc=%Rhrc)!\n", hrc);
        goto leave;
    }

    if (pcszSettingsPw)
    {
        CHECK_ERROR(pVirtualBox, SetSettingsSecret(Bstr(pcszSettingsPw).raw()));
        if (FAILED(hrc))
            goto leave;
    }
    else if (pcszSettingsPwFile)
    {
        rcExit = settingsPasswordFile(pVirtualBox, pcszSettingsPwFile);
        if (rcExit != RTEXITCODE_SUCCESS)
            goto leave;
    }

    /*
     * Do we have a UUID?
     */
    if (uuidVM.isValid())
    {
        hrc = pVirtualBox->FindMachine(uuidVM.toUtf16().raw(), pMachine.asOutParam());
        if (FAILED(hrc) || !pMachine)
        {
            RTPrintf("Error: machine with the given ID not found!\n");
            goto leave;
        }
    }
    else if (vmName)
    {
        /*
         * Do we have a name but no UUID?
         */
        hrc = pVirtualBox->FindMachine(Bstr(vmName).raw(), pMachine.asOutParam());
        if ((hrc == S_OK) && pMachine)
        {
            Bstr bstrId;
            pMachine->COMGETTER(Id)(bstrId.asOutParam());
            uuidVM = Guid(bstrId);
        }
        else
        {
            RTPrintf("Error: machine with the given name not found!\n");
            RTPrintf("Check if this VM has been corrupted and is now inaccessible.");
            goto leave;
        }
    }

    /* create SDL event semaphore */
    vrc = RTSemEventCreate(&g_EventSemSDLEvents);
    AssertReleaseRC(vrc);

    hrc = pVirtualBoxClient->CheckMachineError(pMachine);
    if (FAILED(hrc))
    {
        com::ErrorInfo info;
        if (info.isFullAvailable())
            PrintError("The VM has errors",
                       info.getText().raw(), info.getComponent().raw());
        else
            RTPrintf("Failed to check for VM errors! No error information available (rc=%Rhrc).\n", hrc);
        goto leave;
    }

    if (fSeparate)
    {
        MachineState_T machineState = MachineState_Null;
        pMachine->COMGETTER(State)(&machineState);
        if (   machineState == MachineState_Running
            || machineState == MachineState_Teleporting
            || machineState == MachineState_LiveSnapshotting
            || machineState == MachineState_Paused
            || machineState == MachineState_TeleportingPausedVM
           )
        {
            RTPrintf("VM is already running.\n");
        }
        else
        {
            ComPtr<IProgress> progress;
            hrc = pMachine->LaunchVMProcess(pSession, Bstr("headless").raw(), ComSafeArrayNullInParam(), progress.asOutParam());
            if (SUCCEEDED(hrc) && !progress.isNull())
            {
                RTPrintf("Waiting for VM to power on...\n");
                hrc = progress->WaitForCompletion(-1);
                if (SUCCEEDED(hrc))
                {
                    BOOL completed = true;
                    hrc = progress->COMGETTER(Completed)(&completed);
                    if (SUCCEEDED(hrc))
                    {
                        LONG iRc;
                        hrc = progress->COMGETTER(ResultCode)(&iRc);
                        if (SUCCEEDED(hrc))
                        {
                            if (FAILED(iRc))
                            {
                                ProgressErrorInfo info(progress);
                                com::GluePrintErrorInfo(info);
                            }
                            else
                            {
                                RTPrintf("VM has been successfully started.\n");
                                /* LaunchVMProcess obtains a shared lock on the machine.
                                 * Unlock it here, because the lock will be obtained below
                                 * in the common code path as for already running VM.
                                 */
                                pSession->UnlockMachine();
                            }
                        }
                    }
                }
            }
        }
        if (FAILED(hrc))
        {
            RTPrintf("Error: failed to power up VM! No error text available.\n");
            goto leave;
        }

        hrc = pMachine->LockMachine(pSession, LockType_Shared);
    }
    else
    {
        pSession->COMSETTER(Name)(Bstr("GUI/SDL").raw());
        hrc = pMachine->LockMachine(pSession, LockType_VM);
    }

    if (FAILED(hrc))
    {
        com::ErrorInfo info;
        if (info.isFullAvailable())
            PrintError("Could not open VirtualBox session",
                       info.getText().raw(), info.getComponent().raw());
        goto leave;
    }
    if (!pSession)
    {
        RTPrintf("Could not open VirtualBox session!\n");
        goto leave;
    }
    sessionOpened = true;
    // get the mutable VM we're dealing with
    pSession->COMGETTER(Machine)(gpMachine.asOutParam());
    if (!gpMachine)
    {
        com::ErrorInfo info;
        if (info.isFullAvailable())
            PrintError("Cannot start VM!",
                       info.getText().raw(), info.getComponent().raw());
        else
            RTPrintf("Error: given machine not found!\n");
        goto leave;
    }

    // get the VM console
    pSession->COMGETTER(Console)(gpConsole.asOutParam());
    if (!gpConsole)
    {
        RTPrintf("Given console not found!\n");
        goto leave;
    }

    /*
     * Are we supposed to use a different hard disk file?
     */
    if (pcszHdaFile)
    {
        ComPtr<IMedium> pMedium;

        /*
         * Strategy: if any registered hard disk points to the same file,
         * assign it. If not, register a new image and assign it to the VM.
         */
        Bstr bstrHdaFile(pcszHdaFile);
        pVirtualBox->OpenMedium(bstrHdaFile.raw(), DeviceType_HardDisk,
                                AccessMode_ReadWrite, FALSE /* fForceNewUuid */,
                                pMedium.asOutParam());
        if (!pMedium)
        {
            /* we've not found the image */
            RTPrintf("Adding hard disk '%s'...\n", pcszHdaFile);
            pVirtualBox->OpenMedium(bstrHdaFile.raw(), DeviceType_HardDisk,
                                    AccessMode_ReadWrite, FALSE /* fForceNewUuid */,
                                    pMedium.asOutParam());
        }
        /* do we have the right image now? */
        if (pMedium)
        {
            Bstr bstrSCName;

            /* get the first IDE controller to attach the harddisk to
             * and if there is none, add one temporarily */
            {
                ComPtr<IStorageController> pStorageCtl;
                com::SafeIfaceArray<IStorageController> aStorageControllers;
                CHECK_ERROR(gpMachine, COMGETTER(StorageControllers)(ComSafeArrayAsOutParam(aStorageControllers)));
                for (size_t i = 0; i < aStorageControllers.size(); ++ i)
                {
                    StorageBus_T storageBus = StorageBus_Null;

                    CHECK_ERROR(aStorageControllers[i], COMGETTER(Bus)(&storageBus));
                    if (storageBus == StorageBus_IDE)
                    {
                        pStorageCtl = aStorageControllers[i];
                        break;
                    }
                }

                if (pStorageCtl)
                {
                    CHECK_ERROR(pStorageCtl, COMGETTER(Name)(bstrSCName.asOutParam()));
                    gpMachine->DetachDevice(bstrSCName.raw(), 0, 0);
                }
                else
                {
                    bstrSCName = "IDE Controller";
                    CHECK_ERROR(gpMachine, AddStorageController(bstrSCName.raw(),
                                                                StorageBus_IDE,
                                                                pStorageCtl.asOutParam()));
                }
            }

            CHECK_ERROR(gpMachine, AttachDevice(bstrSCName.raw(), 0, 0,
                                                DeviceType_HardDisk, pMedium));
            /// @todo why is this attachment saved?
        }
        else
        {
            RTPrintf("Error: failed to mount the specified hard disk image!\n");
            goto leave;
        }
    }

    /*
     * Mount a floppy if requested.
     */
    if (pcszFdaFile)
    do
    {
        ComPtr<IMedium> pMedium;

        /* unmount? */
        if (!strcmp(pcszFdaFile, "none"))
        {
            /* nothing to do, NULL object will cause unmount */
        }
        else
        {
            Bstr bstrFdaFile(pcszFdaFile);

            /* Assume it's a host drive name */
            ComPtr<IHost> pHost;
            CHECK_ERROR_BREAK(pVirtualBox, COMGETTER(Host)(pHost.asOutParam()));
            hrc = pHost->FindHostFloppyDrive(bstrFdaFile.raw(),
                                            pMedium.asOutParam());
            if (FAILED(hrc))
            {
                /* try to find an existing one */
                hrc = pVirtualBox->OpenMedium(bstrFdaFile.raw(),
                                             DeviceType_Floppy,
                                             AccessMode_ReadWrite,
                                             FALSE /* fForceNewUuid */,
                                             pMedium.asOutParam());
                if (FAILED(hrc))
                {
                    /* try to add to the list */
                    RTPrintf("Adding floppy image '%s'...\n", pcszFdaFile);
                    CHECK_ERROR_BREAK(pVirtualBox,
                                      OpenMedium(bstrFdaFile.raw(),
                                                 DeviceType_Floppy,
                                                 AccessMode_ReadWrite,
                                                 FALSE /* fForceNewUuid */,
                                                 pMedium.asOutParam()));
                }
            }
        }

        Bstr bstrSCName;

        /* get the first floppy controller to attach the floppy to
         * and if there is none, add one temporarily */
        {
            ComPtr<IStorageController> pStorageCtl;
            com::SafeIfaceArray<IStorageController> aStorageControllers;
            CHECK_ERROR(gpMachine, COMGETTER(StorageControllers)(ComSafeArrayAsOutParam(aStorageControllers)));
            for (size_t i = 0; i < aStorageControllers.size(); ++ i)
            {
                StorageBus_T storageBus = StorageBus_Null;

                CHECK_ERROR(aStorageControllers[i], COMGETTER(Bus)(&storageBus));
                if (storageBus == StorageBus_Floppy)
                {
                    pStorageCtl = aStorageControllers[i];
                    break;
                }
            }

            if (pStorageCtl)
            {
                CHECK_ERROR(pStorageCtl, COMGETTER(Name)(bstrSCName.asOutParam()));
                gpMachine->DetachDevice(bstrSCName.raw(), 0, 0);
            }
            else
            {
                bstrSCName = "Floppy Controller";
                CHECK_ERROR(gpMachine, AddStorageController(bstrSCName.raw(),
                                                            StorageBus_Floppy,
                                                            pStorageCtl.asOutParam()));
            }
        }

        CHECK_ERROR(gpMachine, AttachDevice(bstrSCName.raw(), 0, 0,
                                            DeviceType_Floppy, pMedium));
    }
    while (0);
    if (FAILED(hrc))
        goto leave;

    /*
     * Mount a CD-ROM if requested.
     */
    if (pcszCdromFile)
    do
    {
        ComPtr<IMedium> pMedium;

        /* unmount? */
        if (!strcmp(pcszCdromFile, "none"))
        {
            /* nothing to do, NULL object will cause unmount */
        }
        else
        {
            Bstr bstrCdromFile(pcszCdromFile);

            /* Assume it's a host drive name */
            ComPtr<IHost> pHost;
            CHECK_ERROR_BREAK(pVirtualBox, COMGETTER(Host)(pHost.asOutParam()));
            hrc = pHost->FindHostDVDDrive(bstrCdromFile.raw(), pMedium.asOutParam());
            if (FAILED(hrc))
            {
                /* try to find an existing one */
                hrc = pVirtualBox->OpenMedium(bstrCdromFile.raw(),
                                            DeviceType_DVD,
                                            AccessMode_ReadWrite,
                                            FALSE /* fForceNewUuid */,
                                            pMedium.asOutParam());
                if (FAILED(hrc))
                {
                    /* try to add to the list */
                    RTPrintf("Adding ISO image '%s'...\n", pcszCdromFile);
                    CHECK_ERROR_BREAK(pVirtualBox,
                                      OpenMedium(bstrCdromFile.raw(),
                                                 DeviceType_DVD,
                                                 AccessMode_ReadWrite,
                                                 FALSE /* fForceNewUuid */,
                                                 pMedium.asOutParam()));
                }
            }
        }

        Bstr bstrSCName;

        /* get the first IDE controller to attach the DVD drive to
         * and if there is none, add one temporarily */
        {
            ComPtr<IStorageController> pStorageCtl;
            com::SafeIfaceArray<IStorageController> aStorageControllers;
            CHECK_ERROR(gpMachine, COMGETTER(StorageControllers)(ComSafeArrayAsOutParam(aStorageControllers)));
            for (size_t i = 0; i < aStorageControllers.size(); ++ i)
            {
                StorageBus_T storageBus = StorageBus_Null;

                CHECK_ERROR(aStorageControllers[i], COMGETTER(Bus)(&storageBus));
                if (storageBus == StorageBus_IDE)
                {
                    pStorageCtl = aStorageControllers[i];
                    break;
                }
            }

            if (pStorageCtl)
            {
                CHECK_ERROR(pStorageCtl, COMGETTER(Name)(bstrSCName.asOutParam()));
                gpMachine->DetachDevice(bstrSCName.raw(), 1, 0);
            }
            else
            {
                bstrSCName = "IDE Controller";
                CHECK_ERROR(gpMachine, AddStorageController(bstrSCName.raw(),
                                                            StorageBus_IDE,
                                                            pStorageCtl.asOutParam()));
            }
        }

        CHECK_ERROR(gpMachine, AttachDevice(bstrSCName.raw(), 1, 0,
                                            DeviceType_DVD, pMedium));
    }
    while (0);
    if (FAILED(hrc))
        goto leave;

    if (fDiscardState)
    {
        /*
         * If the machine is currently saved,
         * discard the saved state first.
         */
        MachineState_T machineState;
        gpMachine->COMGETTER(State)(&machineState);
        if (machineState == MachineState_Saved || machineState == MachineState_AbortedSaved)
        {
            CHECK_ERROR(gpMachine, DiscardSavedState(true /* fDeleteFile */));
        }
        /*
         * If there are snapshots, discard the current state,
         * i.e. revert to the last snapshot.
         */
        ULONG cSnapshots;
        gpMachine->COMGETTER(SnapshotCount)(&cSnapshots);
        if (cSnapshots)
        {
            gpProgress = NULL;

            ComPtr<ISnapshot> pCurrentSnapshot;
            CHECK_ERROR(gpMachine, COMGETTER(CurrentSnapshot)(pCurrentSnapshot.asOutParam()));
            if (FAILED(hrc))
                goto leave;

            CHECK_ERROR(gpMachine, RestoreSnapshot(pCurrentSnapshot, gpProgress.asOutParam()));
            hrc = gpProgress->WaitForCompletion(-1);
        }
    }

    // get the machine debugger (does not have to be there)
    gpConsole->COMGETTER(Debugger)(gpMachineDebugger.asOutParam());
    if (gpMachineDebugger)
    {
        Log(("Machine debugger available!\n"));
    }
    gpConsole->COMGETTER(Display)(gpDisplay.asOutParam());
    if (!gpDisplay)
    {
        RTPrintf("Error: could not get display object!\n");
        goto leave;
    }

    // set the boot drive
    if (bootDevice != DeviceType_Null)
    {
        hrc = gpMachine->SetBootOrder(1, bootDevice);
        if (hrc != S_OK)
        {
            RTPrintf("Error: could not set boot device, using default.\n");
        }
    }

    // set the memory size if not default
    if (memorySize)
    {
        hrc = gpMachine->COMSETTER(MemorySize)(memorySize);
        if (hrc != S_OK)
        {
            ULONG ramSize = 0;
            gpMachine->COMGETTER(MemorySize)(&ramSize);
            RTPrintf("Error: could not set memory size, using current setting of %d MBytes\n", ramSize);
        }
    }

    hrc = gpMachine->COMGETTER(GraphicsAdapter)(pGraphicsAdapter.asOutParam());
    if (hrc != S_OK)
    {
        RTPrintf("Error: could not get graphics adapter object\n");
        goto leave;
    }

    if (vramSize)
    {
        hrc = pGraphicsAdapter->COMSETTER(VRAMSize)(vramSize);
        if (hrc != S_OK)
        {
            pGraphicsAdapter->COMGETTER(VRAMSize)((ULONG*)&vramSize);
            RTPrintf("Error: could not set VRAM size, using current setting of %d MBytes\n", vramSize);
        }
    }

    // we're always able to process absolute mouse events and we prefer that
    gfAbsoluteMouseHost = TRUE;

#ifdef VBOX_WIN32_UI
    if (fWin32UI)
    {
        /* initialize the Win32 user interface inside which SDL will be embedded */
        if (initUI(fResizable, winId))
            return 1;
    }
#endif

    /* static initialization of the SDL stuff */
    if (!VBoxSDLFB::init(fShowSDLConfig))
        goto leave;

    pGraphicsAdapter->COMGETTER(MonitorCount)(&gcMonitors);
    if (gcMonitors > 64)
        gcMonitors = 64;

    for (unsigned i = 0; i < gcMonitors; i++)
    {
        // create our SDL framebuffer instance
        gpFramebuffer[i].createObject();
        hrc = gpFramebuffer[i]->init(i, fFullscreen, fResizable, fShowSDLConfig, false,
                                    fixedWidth, fixedHeight, fixedBPP, fSeparate);
        if (FAILED(hrc))
        {
            RTPrintf("Error: could not create framebuffer object!\n");
            goto leave;
        }
    }

#ifdef VBOX_WIN32_UI
    gpFramebuffer[0]->setWinId(winId);
#endif

    for (unsigned i = 0; i < gcMonitors; i++)
    {
        if (!gpFramebuffer[i]->initialized())
            goto leave;
        gpFramebuffer[i]->AddRef();
        if (fFullscreen)
            SetFullscreen(true);
    }

#ifdef VBOXSDL_WITH_X11
    /* NOTE1: We still want Ctrl-C to work, so we undo the SDL redirections.
     * NOTE2: We have to remove the PidFile if this file exists. */
    signal(SIGINT,  signal_handler_SIGINT);
    signal(SIGQUIT, signal_handler_SIGINT);
    signal(SIGSEGV, signal_handler_SIGINT);
#endif


    for (ULONG i = 0; i < gcMonitors; i++)
    {
        // register our framebuffer
        hrc = gpDisplay->AttachFramebuffer(i, gpFramebuffer[i], gaFramebufferId[i].asOutParam());
        if (FAILED(hrc))
        {
            RTPrintf("Error: could not register framebuffer object!\n");
            goto leave;
        }
        ULONG dummy;
        LONG xOrigin, yOrigin;
        GuestMonitorStatus_T monitorStatus;
        hrc = gpDisplay->GetScreenResolution(i, &dummy, &dummy, &dummy, &xOrigin, &yOrigin, &monitorStatus);
        gpFramebuffer[i]->setOrigin(xOrigin, yOrigin);
    }

    {
        // register listener for VirtualBoxClient events
        ComPtr<IEventSource> pES;
        CHECK_ERROR(pVirtualBoxClient, COMGETTER(EventSource)(pES.asOutParam()));
        ComObjPtr<VBoxSDLClientEventListenerImpl> listener;
        listener.createObject();
        listener->init(new VBoxSDLClientEventListener());
        pVBoxClientListener = listener;
        com::SafeArray<VBoxEventType_T> eventTypes;
        eventTypes.push_back(VBoxEventType_OnVBoxSVCAvailabilityChanged);
        CHECK_ERROR(pES, RegisterListener(pVBoxClientListener, ComSafeArrayAsInParam(eventTypes), true));
    }

    {
        // register listener for VirtualBox (server) events
        ComPtr<IEventSource> pES;
        CHECK_ERROR(pVirtualBox, COMGETTER(EventSource)(pES.asOutParam()));
        ComObjPtr<VBoxSDLEventListenerImpl> listener;
        listener.createObject();
        listener->init(new VBoxSDLEventListener());
        pVBoxListener = listener;
        com::SafeArray<VBoxEventType_T> eventTypes;
        eventTypes.push_back(VBoxEventType_OnExtraDataChanged);
        CHECK_ERROR(pES, RegisterListener(pVBoxListener, ComSafeArrayAsInParam(eventTypes), true));
    }

    {
        // register listener for Console events
        ComPtr<IEventSource> pES;
        CHECK_ERROR(gpConsole, COMGETTER(EventSource)(pES.asOutParam()));
        pConsoleListener.createObject();
        pConsoleListener->init(new VBoxSDLConsoleEventListener());
        com::SafeArray<VBoxEventType_T> eventTypes;
        eventTypes.push_back(VBoxEventType_OnMousePointerShapeChanged);
        eventTypes.push_back(VBoxEventType_OnMouseCapabilityChanged);
        eventTypes.push_back(VBoxEventType_OnKeyboardLedsChanged);
        eventTypes.push_back(VBoxEventType_OnStateChanged);
        eventTypes.push_back(VBoxEventType_OnRuntimeError);
        eventTypes.push_back(VBoxEventType_OnCanShowWindow);
        eventTypes.push_back(VBoxEventType_OnShowWindow);
        CHECK_ERROR(pES, RegisterListener(pConsoleListener, ComSafeArrayAsInParam(eventTypes), true));
        // until we've tried to to start the VM, ignore power off events
        pConsoleListener->getWrapped()->ignorePowerOffEvents(true);
    }

    if (pszPortVRDP)
    {
        hrc = gpMachine->COMGETTER(VRDEServer)(gpVRDEServer.asOutParam());
        AssertMsg((hrc == S_OK) && gpVRDEServer, ("Could not get VRDP Server! rc = 0x%x\n", hrc));
        if (gpVRDEServer)
        {
            // has a non standard VRDP port been requested?
            if (strcmp(pszPortVRDP, "0"))
            {
                hrc = gpVRDEServer->SetVRDEProperty(Bstr("TCP/Ports").raw(), Bstr(pszPortVRDP).raw());
                if (hrc != S_OK)
                {
                    RTPrintf("Error: could not set VRDP port! rc = 0x%x\n", hrc);
                    goto leave;
                }
            }
            // now enable VRDP
            hrc = gpVRDEServer->COMSETTER(Enabled)(TRUE);
            if (hrc != S_OK)
            {
                RTPrintf("Error: could not enable VRDP server! rc = 0x%x\n", hrc);
                goto leave;
            }
        }
    }

    hrc = E_FAIL;
#ifdef VBOXSDL_ADVANCED_OPTIONS
    if (u32WarpDrive != 0)
    {
        if (!gpMachineDebugger)
        {
            RTPrintf("Error: No debugger object; --warpdrive %d cannot be executed!\n", u32WarpDrive);
            goto leave;
        }
        gpMachineDebugger->COMSETTER(VirtualTimeRate)(u32WarpDrive);
    }
#endif /* VBOXSDL_ADVANCED_OPTIONS */

    /* start with something in the titlebar */
    UpdateTitlebar(TITLEBAR_NORMAL);

    /* memorize the default cursor */
    gpDefaultCursor = SDL_GetCursor();
    /*
     * Register our user signal handler.
     */
#ifdef VBOXSDL_WITH_X11
    struct sigaction sa;
    sa.sa_sigaction = signal_handler_SIGUSR1;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sigaction(SIGUSR1, &sa, NULL);
#endif /* VBOXSDL_WITH_X11 */

    /*
     * Start the VM execution thread. This has to be done
     * asynchronously as powering up can take some time
     * (accessing devices such as the host DVD drive). In
     * the meantime, we have to service the SDL event loop.
     */
    SDL_Event event;

    if (!fSeparate)
    {
        LogFlow(("Powering up the VM...\n"));
        hrc = gpConsole->PowerUp(gpProgress.asOutParam());
        if (hrc != S_OK)
        {
            com::ErrorInfo info(gpConsole, COM_IIDOF(IConsole));
            if (info.isBasicAvailable())
                PrintError("Failed to power up VM", info.getText().raw());
            else
                RTPrintf("Error: failed to power up VM! No error text available.\n");
            goto leave;
        }
    }

#ifdef USE_XPCOM_QUEUE_THREAD
    /*
     * Before we starting to do stuff, we have to launch the XPCOM
     * event queue thread. It will wait for events and send messages
     * to the SDL thread. After having done this, we should fairly
     * quickly start to process the SDL event queue as an XPCOM
     * event storm might arrive. Stupid SDL has a ridiculously small
     * event queue buffer!
     */
    startXPCOMEventQueueThread(eventQ->getSelectFD());
#endif /* USE_XPCOM_QUEUE_THREAD */

    /* termination flag */
    bool fTerminateDuringStartup;
    fTerminateDuringStartup = false;

    LogRel(("VBoxSDL: NUM lock initially %s, CAPS lock initially %s\n",
            !!(SDL_GetModState() & KMOD_NUM)  ? "ON" : "OFF",
            !!(SDL_GetModState() & KMOD_CAPS) ? "ON" : "OFF"));

    /* start regular timer so we don't starve in the event loop */
    SDL_TimerID sdlTimer;
    sdlTimer = SDL_AddTimer(100, StartupTimer, NULL);

    /* loop until the powerup processing is done */
    MachineState_T machineState;
    do
    {
        hrc = gpMachine->COMGETTER(State)(&machineState);
        if (    hrc == S_OK
            &&  (   machineState == MachineState_Starting
                 || machineState == MachineState_Restoring
                 || machineState == MachineState_TeleportingIn
                )
            )
        {
            /*
             * wait for the next event. This is uncritical as
             * power up guarantees to change the machine state
             * to either running or aborted and a machine state
             * change will send us an event. However, we have to
             * service the XPCOM event queue!
             */
#ifdef USE_XPCOM_QUEUE_THREAD
            if (!fXPCOMEventThreadSignaled)
            {
                signalXPCOMEventQueueThread();
                fXPCOMEventThreadSignaled = true;
            }
#endif
            /*
             * Wait for SDL events.
             */
            if (WaitSDLEvent(&event))
            {
                switch (event.type)
                {
                    /*
                     * Timer event. Used to have the titlebar updated.
                     */
                    case SDL_USER_EVENT_TIMER:
                    {
                        /*
                         * Update the title bar.
                         */
                        UpdateTitlebar(TITLEBAR_STARTUP);
                        break;
                    }

                    /*
                     * User specific framebuffer change event.
                     */
                    case SDL_USER_EVENT_NOTIFYCHANGE:
                    {
                        LogFlow(("SDL_USER_EVENT_NOTIFYCHANGE\n"));
                        LONG xOrigin, yOrigin;
                        gpFramebuffer[event.user.code]->notifyChange(event.user.code);
                        /* update xOrigin, yOrigin -> mouse */
                        ULONG dummy;
                        GuestMonitorStatus_T monitorStatus;
                        hrc = gpDisplay->GetScreenResolution(event.user.code, &dummy, &dummy, &dummy, &xOrigin, &yOrigin, &monitorStatus);
                        gpFramebuffer[event.user.code]->setOrigin(xOrigin, yOrigin);
                        break;
                    }

#ifdef USE_XPCOM_QUEUE_THREAD
                    /*
                     * User specific XPCOM event queue event
                     */
                    case SDL_USER_EVENT_XPCOM_EVENTQUEUE:
                    {
                        LogFlow(("SDL_USER_EVENT_XPCOM_EVENTQUEUE: processing XPCOM event queue...\n"));
                        eventQ->processEventQueue(0);
                        signalXPCOMEventQueueThread();
                        break;
                    }
#endif /* USE_XPCOM_QUEUE_THREAD */

                    /*
                     * Termination event from the on state change callback.
                     */
                    case SDL_USER_EVENT_TERMINATE:
                    {
                        if (event.user.code != VBOXSDL_TERM_NORMAL)
                        {
                            com::ProgressErrorInfo info(gpProgress);
                            if (info.isBasicAvailable())
                                PrintError("Failed to power up VM", info.getText().raw());
                            else
                                RTPrintf("Error: failed to power up VM! No error text available.\n");
                        }
                        fTerminateDuringStartup = true;
                        break;
                    }

                    default:
                    {
                        Log8(("VBoxSDL: Unknown SDL event %d (pre)\n", event.type));
                        break;
                    }
                }

            }
        }
        eventQ->processEventQueue(0);
    } while (   hrc == S_OK
             && (   machineState == MachineState_Starting
                 || machineState == MachineState_Restoring
                 || machineState == MachineState_TeleportingIn
                )
            );

    /* kill the timer again */
    SDL_RemoveTimer(sdlTimer);
    sdlTimer = 0;

    /* are we supposed to terminate the process? */
    if (fTerminateDuringStartup)
        goto leave;

    /* did the power up succeed? */
    if (machineState != MachineState_Running)
    {
        com::ProgressErrorInfo info(gpProgress);
        if (info.isBasicAvailable())
            PrintError("Failed to power up VM", info.getText().raw());
        else
            RTPrintf("Error: failed to power up VM! No error text available (rc = 0x%x state = %d)\n", hrc, machineState);
        goto leave;
    }

    // accept power off events from now on because we're running
    // note that there's a possible race condition here...
    pConsoleListener->getWrapped()->ignorePowerOffEvents(false);

    hrc = gpConsole->COMGETTER(Keyboard)(gpKeyboard.asOutParam());
    if (!gpKeyboard)
    {
        RTPrintf("Error: could not get keyboard object!\n");
        goto leave;
    }
    gpConsole->COMGETTER(Mouse)(gpMouse.asOutParam());
    if (!gpMouse)
    {
        RTPrintf("Error: could not get mouse object!\n");
        goto leave;
    }

    if (fSeparate && gpMouse)
    {
        LogFlow(("Fetching mouse caps\n"));

        /* Fetch current mouse status, etc */
        gpMouse->COMGETTER(AbsoluteSupported)(&gfAbsoluteMouseGuest);
        gpMouse->COMGETTER(RelativeSupported)(&gfRelativeMouseGuest);
        gpMouse->COMGETTER(NeedsHostCursor)(&gfGuestNeedsHostCursor);

        HandleGuestCapsChanged();

        ComPtr<IMousePointerShape> mps;
        gpMouse->COMGETTER(PointerShape)(mps.asOutParam());
        if (!mps.isNull())
        {
            BOOL  visible,  alpha;
            ULONG hotX, hotY, width, height;
            com::SafeArray <BYTE> shape;

            mps->COMGETTER(Visible)(&visible);
            mps->COMGETTER(Alpha)(&alpha);
            mps->COMGETTER(HotX)(&hotX);
            mps->COMGETTER(HotY)(&hotY);
            mps->COMGETTER(Width)(&width);
            mps->COMGETTER(Height)(&height);
            mps->COMGETTER(Shape)(ComSafeArrayAsOutParam(shape));

            if (shape.size() > 0)
            {
                PointerShapeChangeData data(visible, alpha, hotX, hotY, width, height,
                                            ComSafeArrayAsInParam(shape));
                SetPointerShape(&data);
            }
        }
    }

    UpdateTitlebar(TITLEBAR_NORMAL);

    /*
     * Create PID file.
     */
    if (gpszPidFile)
    {
        char szBuf[32];
        const char *pcszLf = "\n";
        RTFILE PidFile;
        RTFileOpen(&PidFile, gpszPidFile, RTFILE_O_WRITE | RTFILE_O_CREATE_REPLACE | RTFILE_O_DENY_NONE);
        RTStrFormatNumber(szBuf, RTProcSelf(), 10, 0, 0, 0);
        RTFileWrite(PidFile, szBuf, strlen(szBuf), NULL);
        RTFileWrite(PidFile, pcszLf, strlen(pcszLf), NULL);
        RTFileClose(PidFile);
    }

    /*
     * Main event loop
     */
#ifdef USE_XPCOM_QUEUE_THREAD
    if (!fXPCOMEventThreadSignaled)
    {
        signalXPCOMEventQueueThread();
    }
#endif
    LogFlow(("VBoxSDL: Entering big event loop\n"));
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
                        VBoxSDLFB *fb = getFbFromWinId(event.window.windowID);
                        if (fb)
                            fb->repaint();
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
                        if (gpDisplay)
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
                if (!gfACPITerm || gSdlQuitTimer)
                    goto leave;
                if (gpConsole)
                    gpConsole->PowerButton();
                gSdlQuitTimer = SDL_AddTimer(1000, QuitTimer, NULL);
                break;
            }

            /*
             * The mouse has moved
             */
            case SDL_MOUSEMOTION:
            {
                if (gfGrabbed || UseAbsoluteMouse())
                {
                    VBoxSDLFB *fb = getFbFromWinId(event.motion.windowID);
                    if (fb)
                        SendMouseEvent(fb, 0, 0, 0);
                }
                break;
            }

            case SDL_MOUSEWHEEL:
            {
                VBoxSDLFB *fb = getFbFromWinId(event.button.windowID);
                if (fb)
                    SendMouseEvent(fb, -1 * event.wheel.y, 0, 0);
                break;
            }
            /*
             * A mouse button has been clicked or released.
             */
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
            {
                SDL_MouseButtonEvent *bev = &event.button;
                /* don't grab on mouse click if we have guest additions */
                if (!gfGrabbed && !UseAbsoluteMouse() && gfGrabOnMouseClick)
                {
                    if (event.type == SDL_MOUSEBUTTONDOWN && (bev->state & SDL_BUTTON_LMASK))
                    {
                        /* start grabbing all events */
                        InputGrabStart();
                    }
                }
                else if (gfGrabbed || UseAbsoluteMouse())
                {
                    /* end host key combination (CTRL+MouseButton) */
                    switch (enmHKeyState)
                    {
                        case HKEYSTATE_DOWN_1ST:
                        case HKEYSTATE_DOWN_2ND:
                            enmHKeyState = HKEYSTATE_NOT_IT;
                            ProcessKey(&EvHKeyDown1.key);
                            /* ugly hack: small delay to ensure that the key event is
                             * actually handled _prior_ to the mouse click event */
                            RTThreadSleep(20);
                            break;
                        case HKEYSTATE_DOWN:
                            enmHKeyState = HKEYSTATE_NOT_IT;
                            ProcessKey(&EvHKeyDown1.key);
                            if (gHostKeySym2 != SDLK_UNKNOWN)
                                ProcessKey(&EvHKeyDown2.key);
                            /* ugly hack: small delay to ensure that the key event is
                             * actually handled _prior_ to the mouse click event */
                            RTThreadSleep(20);
                            break;
                        default:
                            break;
                    }

                    VBoxSDLFB *fb;
                    fb = getFbFromWinId(event.button.windowID);
                    if (fb)
                        SendMouseEvent(fb, 0 /*wheel vertical movement*/, event.type == SDL_MOUSEBUTTONDOWN, bev->button);
                }
                break;
            }

#if 0
            /*
             * The window has gained or lost focus.
             */
            case SDL_ACTIVEEVENT: /** @todo Needs to be also fixed with SDL2? Check! */
            {
                /*
                 * There is a strange behaviour in SDL when running without a window
                 * manager: When SDL_WM_GrabInput(SDL_GRAB_ON) is called we receive two
                 * consecutive events SDL_ACTIVEEVENTs (input lost, input gained).
                 * Asking SDL_GetAppState() seems the better choice.
                 */
                if (gfGrabbed && (SDL_GetAppState() & SDL_APPINPUTFOCUS) == 0)
                {
                    /*
                     * another window has stolen the (keyboard) input focus
                     */
                    InputGrabEnd();
                }
                break;
            }

            /*
             * The SDL window was resized.
             * For SDL2 this is done in SDL_WINDOWEVENT.
             */
            case SDL_VIDEORESIZE:
            {
                if (gpDisplay)
                {
                    if (gfIgnoreNextResize)
                    {
                        gfIgnoreNextResize = FALSE;
                        break;
                    }
                    uResizeWidth  = event.resize.w;
                    uResizeHeight = event.resize.h;
                    if (gSdlResizeTimer)
                        SDL_RemoveTimer(gSdlResizeTimer);
                    gSdlResizeTimer = SDL_AddTimer(300, ResizeTimer, NULL);
                }
                break;
            }
#endif
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

                Assert(gpFramebuffer[event.user.code]);
                gpFramebuffer[event.user.code]->update(x, y, w, h, true /* fGuestRelative */);
                break;
            }

            /*
             * User event: Window resize done
             */
            case SDL_USER_EVENT_WINDOW_RESIZE_DONE:
            {
                /**
                 * @todo This is a workaround for synchronization problems between EMT and the
                 *       SDL main thread. It can happen that the SDL thread already starts a
                 *       new resize operation while the EMT is still busy with the old one
                 *       leading to a deadlock. Therefore we call SetVideoModeHint only once
                 *       when the mouse button was released.
                 */
                /* communicate the resize event to the guest */
                gpDisplay->SetVideoModeHint(0 /*=display*/, true /*=enabled*/, false /*=changeOrigin*/,
                                            0 /*=originX*/, 0 /*=originY*/,
                                            uResizeWidth, uResizeHeight, 0 /*=don't change bpp*/, true /*=notify*/);
                break;

            }

            /*
             * User specific framebuffer change event.
             */
            case SDL_USER_EVENT_NOTIFYCHANGE:
            {
                LogFlow(("SDL_USER_EVENT_NOTIFYCHANGE\n"));
                LONG xOrigin, yOrigin;
                gpFramebuffer[event.user.code]->notifyChange(event.user.code);
                /* update xOrigin, yOrigin -> mouse */
                ULONG dummy;
                GuestMonitorStatus_T monitorStatus;
                hrc = gpDisplay->GetScreenResolution(event.user.code, &dummy, &dummy, &dummy, &xOrigin, &yOrigin, &monitorStatus);
                gpFramebuffer[event.user.code]->setOrigin(xOrigin, yOrigin);
                break;
            }

#ifdef USE_XPCOM_QUEUE_THREAD
            /*
             * User specific XPCOM event queue event
             */
            case SDL_USER_EVENT_XPCOM_EVENTQUEUE:
            {
                LogFlow(("SDL_USER_EVENT_XPCOM_EVENTQUEUE: processing XPCOM event queue...\n"));
                eventQ->processEventQueue(0);
                signalXPCOMEventQueueThread();
                break;
            }
#endif /* USE_XPCOM_QUEUE_THREAD */

            /*
             * User specific update title bar notification event
             */
            case SDL_USER_EVENT_UPDATE_TITLEBAR:
            {
                UpdateTitlebar(TITLEBAR_NORMAL);
                break;
            }

            /*
             * User specific termination event
             */
            case SDL_USER_EVENT_TERMINATE:
            {
                if (event.user.code != VBOXSDL_TERM_NORMAL)
                    RTPrintf("Error: VM terminated abnormally!\n");
                goto leave;
            }
            /*
             * User specific pointer shape change event
             */
            case SDL_USER_EVENT_POINTER_CHANGE:
            {
                PointerShapeChangeData *data = (PointerShapeChangeData *)event.user.data1;
                SetPointerShape (data);
                delete data;
                break;
            }

            /*
             * User specific guest capabilities changed
             */
            case SDL_USER_EVENT_GUEST_CAP_CHANGED:
            {
                HandleGuestCapsChanged();
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
    if (gpszPidFile)
        RTFileDelete(gpszPidFile);

    LogFlow(("leaving...\n"));
#if defined(VBOX_WITH_XPCOM) && !defined(RT_OS_DARWIN) && !defined(RT_OS_OS2)
    /* make sure the XPCOM event queue thread doesn't do anything harmful */
    terminateXPCOMQueueThread();
#endif /* VBOX_WITH_XPCOM */

    if (gpVRDEServer)
        hrc = gpVRDEServer->COMSETTER(Enabled)(FALSE);

    /*
     * Get the machine state.
     */
    if (gpMachine)
        gpMachine->COMGETTER(State)(&machineState);
    else
        machineState = MachineState_Aborted;

    if (!fSeparate)
    {
        /*
         * Turn off the VM if it's running
         */
        if (   gpConsole
            && (   machineState == MachineState_Running
                || machineState == MachineState_Teleporting
                || machineState == MachineState_LiveSnapshotting
                /** @todo power off paused VMs too? */
               )
           )
        do
        {
            pConsoleListener->getWrapped()->ignorePowerOffEvents(true);
            ComPtr<IProgress> pProgress;
            CHECK_ERROR_BREAK(gpConsole, PowerDown(pProgress.asOutParam()));
            CHECK_ERROR_BREAK(pProgress, WaitForCompletion(-1));
            BOOL completed;
            CHECK_ERROR_BREAK(pProgress, COMGETTER(Completed)(&completed));
            ASSERT(completed);
            LONG hrc2;
            CHECK_ERROR_BREAK(pProgress, COMGETTER(ResultCode)(&hrc2));
            if (FAILED(hrc2))
            {
                com::ErrorInfo info;
                if (info.isFullAvailable())
                    PrintError("Failed to power down VM",
                               info.getText().raw(), info.getComponent().raw());
                else
                    RTPrintf("Failed to power down virtual machine! No error information available (rc=%Rhrc).\n", hrc2);
                break;
            }
        } while (0);
    }

    /* unregister Console listener */
    if (pConsoleListener)
    {
        ComPtr<IEventSource> pES;
        CHECK_ERROR(gpConsole, COMGETTER(EventSource)(pES.asOutParam()));
        if (!pES.isNull())
            CHECK_ERROR(pES, UnregisterListener(pConsoleListener));
        pConsoleListener.setNull();
    }

    /*
     * Now we discard all settings so that our changes will
     * not be flushed to the permanent configuration
     */
    if (   gpMachine
        && machineState != MachineState_Saved
        && machineState != MachineState_AbortedSaved)
    {
        hrc = gpMachine->DiscardSettings();
        AssertMsg(SUCCEEDED(hrc), ("DiscardSettings %Rhrc, machineState %d\n", hrc, machineState));
    }

    /* close the session */
    if (sessionOpened)
    {
        hrc = pSession->UnlockMachine();
        AssertComRC(hrc);
    }

    LogFlow(("Releasing mouse, keyboard, remote desktop server, display, console...\n"));
    if (gpDisplay)
    {
        for (unsigned i = 0; i < gcMonitors; i++)
            gpDisplay->DetachFramebuffer(i, gaFramebufferId[i].raw());
    }

    gpMouse = NULL;
    gpKeyboard = NULL;
    gpVRDEServer = NULL;
    gpDisplay = NULL;
    gpConsole = NULL;
    gpMachineDebugger = NULL;
    gpProgress = NULL;
    // we can only uninitialize SDL here because it is not threadsafe

    for (unsigned i = 0; i < gcMonitors; i++)
    {
        if (gpFramebuffer[i])
        {
            LogFlow(("Releasing framebuffer...\n"));
            gpFramebuffer[i]->Release();
            gpFramebuffer[i] = NULL;
        }
    }

    VBoxSDLFB::uninit();

    /* VirtualBox (server) listener unregistration. */
    if (pVBoxListener)
    {
        ComPtr<IEventSource> pES;
        CHECK_ERROR(pVirtualBox, COMGETTER(EventSource)(pES.asOutParam()));
        if (!pES.isNull())
            CHECK_ERROR(pES, UnregisterListener(pVBoxListener));
        pVBoxListener.setNull();
    }

    /* VirtualBoxClient listener unregistration. */
    if (pVBoxClientListener)
    {
        ComPtr<IEventSource> pES;
        CHECK_ERROR(pVirtualBoxClient, COMGETTER(EventSource)(pES.asOutParam()));
        if (!pES.isNull())
            CHECK_ERROR(pES, UnregisterListener(pVBoxClientListener));
        pVBoxClientListener.setNull();
    }

    LogFlow(("Releasing machine, session...\n"));
    gpMachine = NULL;
    pSession = NULL;
    LogFlow(("Releasing VirtualBox object...\n"));
    pVirtualBox = NULL;
    LogFlow(("Releasing VirtualBoxClient object...\n"));
    pVirtualBoxClient = NULL;

    // end "all-stuff" scope
    ////////////////////////////////////////////////////////////////////////////
    }

    /* Must be before com::Shutdown() */
    LogFlow(("Uninitializing COM...\n"));
    com::Shutdown();

    LogFlow(("Returning from main()!\n"));
    RTLogFlush(NULL);

#ifdef RT_OS_WINDOWS
    FreeConsole(); /* Detach or destroy (from) console. */
#endif

    return FAILED(hrc) ? 1 : 0;
}

#ifndef VBOX_WITH_HARDENING
/**
 * Main entry point
 */
int main(int argc, char **argv)
{
#ifdef Q_WS_X11
    if (!XInitThreads())
        return 1;
#endif
    /*
     * Before we do *anything*, we initialize the runtime.
     */
    int rc = RTR3InitExe(argc, &argv, RTR3INIT_FLAGS_TRY_SUPLIB);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    return TrustedMain(argc, argv, NULL);
}
#endif /* !VBOX_WITH_HARDENING */


/**
 * Returns whether the absolute mouse is in use, i.e. both host
 * and guest have opted to enable it.
 *
 * @returns bool Flag whether the absolute mouse is in use
 */
static bool UseAbsoluteMouse(void)
{
    return (gfAbsoluteMouseHost && gfAbsoluteMouseGuest);
}


/**
 * Releases any modifier keys that are currently in pressed state.
 */
static void ResetKeys(void)
{
    int i;

    if (!gpKeyboard)
        return;

    for(i = 0; i < 256; i++)
    {
        if (gaModifiersState[i])
        {
            if (i & 0x80)
                gpKeyboard->PutScancode(0xe0);
            gpKeyboard->PutScancode(i | 0x80);
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
    gpKeyboard->PutUsageCode(SDL_GetScancodeFromKey(ev->keysym.sym), 0x07 /*usage code page id*/, ev->type == SDL_KEYUP ? TRUE : FALSE);
}

#ifdef RT_OS_DARWIN
#include <Carbon/Carbon.h>
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
 * Query mouse position and button state from SDL and send to the VM
 *
 * @param dz  Relative mouse wheel movement
 */
static void SendMouseEvent(VBoxSDLFB *fb, int dz, int down, int button)
{
    int  x, y, state, buttons;
    bool abs;

    if (!fb)
    {
        SDL_GetMouseState(&x, &y);
        RTPrintf("MouseEvent: Cannot find fb mouse = %d,%d\n", x, y);
        return;
    }

    /*
     * If supported and we're not in grabbed mode, we'll use the absolute mouse.
     * If we are in grabbed mode and the guest is not able to draw the mouse cursor
     * itself, or can't handle relative reporting, we have to use absolute
     * coordinates, otherwise the host cursor and
     * the coordinates the guest thinks the mouse is at could get out-of-sync. From
     * the SDL mailing list:
     *
     * "The event processing is usually asynchronous and so somewhat delayed, and
     * SDL_GetMouseState is returning the immediate mouse state. So at the time you
     * call SDL_GetMouseState, the "button" is already up."
     */
    abs =    (UseAbsoluteMouse() && !gfGrabbed)
          || gfGuestNeedsHostCursor
          || !gfRelativeMouseGuest;

    /* only used if abs == TRUE */
    int  xOrigin = fb->getOriginX();
    int  yOrigin = fb->getOriginY();
    int  xMin = fb->getXOffset() + xOrigin;
    int  yMin = fb->getYOffset() + yOrigin;
    int  xMax = xMin + (int)fb->getGuestXRes();
    int  yMax = yMin + (int)fb->getGuestYRes();

    state = abs ? SDL_GetMouseState(&x, &y)
                : SDL_GetRelativeMouseState(&x, &y);

    /*
     * process buttons
     */
    buttons = 0;
    if (state & SDL_BUTTON(SDL_BUTTON_LEFT))
        buttons |= MouseButtonState_LeftButton;
    if (state & SDL_BUTTON(SDL_BUTTON_RIGHT))
        buttons |= MouseButtonState_RightButton;
    if (state & SDL_BUTTON(SDL_BUTTON_MIDDLE))
        buttons |= MouseButtonState_MiddleButton;

    if (abs)
    {
        x += xOrigin;
        y += yOrigin;

        /*
         * Check if the mouse event is inside the guest area. This solves the
         * following problem: Some guests switch off the VBox hardware mouse
         * cursor and draw the mouse cursor itself instead. Moving the mouse
         * outside the guest area then leads to annoying mouse hangs if we
         * don't pass mouse motion events into the guest.
         */
        if (x < xMin || y < yMin || x > xMax || y > yMax)
        {
            /*
             * Cursor outside of valid guest area (outside window or in secure
             * label area. Don't allow any mouse button press.
             */
            button   = 0;

            /*
             * Release any pressed button.
             */
#if 0
            /* disabled on customers request */
            buttons &= ~(MouseButtonState_LeftButton   |
                         MouseButtonState_MiddleButton |
                         MouseButtonState_RightButton);
#endif

            /*
             * Prevent negative coordinates.
             */
            if (x < xMin) x = xMin;
            if (x > xMax) x = xMax;
            if (y < yMin) y = yMin;
            if (y > yMax) y = yMax;

            if (!gpOffCursor)
            {
                gpOffCursor       = SDL_GetCursor();    /* Cursor image */
                gfOffCursorActive = SDL_ShowCursor(-1); /* enabled / disabled */
                SDL_SetCursor(gpDefaultCursor);
                SDL_ShowCursor(SDL_ENABLE);
            }
        }
        else
        {
            if (gpOffCursor)
            {
                /*
                 * We just entered the valid guest area. Restore the guest mouse
                 * cursor.
                 */
                SDL_SetCursor(gpOffCursor);
                SDL_ShowCursor(gfOffCursorActive ? SDL_ENABLE : SDL_DISABLE);
                gpOffCursor = NULL;
            }
        }
    }

    /*
     * Button was pressed but that press is not reflected in the button state?
     */
    if (down && !(state & SDL_BUTTON(button)))
    {
        /*
         * It can happen that a mouse up event follows a mouse down event immediately
         * and we see the events when the bit in the button state is already cleared
         * again. In that case we simulate the mouse down event.
         */
        int tmp_button = 0;
        switch (button)
        {
            case SDL_BUTTON_LEFT:   tmp_button = MouseButtonState_LeftButton;   break;
            case SDL_BUTTON_MIDDLE: tmp_button = MouseButtonState_MiddleButton; break;
            case SDL_BUTTON_RIGHT:  tmp_button = MouseButtonState_RightButton;  break;
        }

        if (abs)
        {
            /**
             * @todo
             * PutMouseEventAbsolute() expects x and y starting from 1,1.
             * should we do the increment internally in PutMouseEventAbsolute()
             * or state it in PutMouseEventAbsolute() docs?
             */
            gpMouse->PutMouseEventAbsolute(x + 1 - xMin + xOrigin,
                                           y + 1 - yMin + yOrigin,
                                           dz, 0 /* horizontal scroll wheel */,
                                           buttons | tmp_button);
        }
        else
        {
            gpMouse->PutMouseEvent(0, 0, dz,
                                   0 /* horizontal scroll wheel */,
                                   buttons | tmp_button);
        }
    }

    // now send the mouse event
    if (abs)
    {
        /**
         * @todo
         * PutMouseEventAbsolute() expects x and y starting from 1,1.
         * should we do the increment internally in PutMouseEventAbsolute()
         * or state it in PutMouseEventAbsolute() docs?
         */
        gpMouse->PutMouseEventAbsolute(x + 1 - xMin + xOrigin,
                                       y + 1 - yMin + yOrigin,
                                       dz, 0 /* Horizontal wheel */, buttons);
    }
    else
    {
        gpMouse->PutMouseEvent(x, y, dz, 0 /* Horizontal wheel */, buttons);
    }
}

/**
 * Resets the VM
 */
void ResetVM(void)
{
    if (gpConsole)
        gpConsole->Reset();
}

/**
 * Initiates a saved state and updates the titlebar with progress information
 */
void SaveState(void)
{
    ResetKeys();
    RTThreadYield();
    if (gfGrabbed)
        InputGrabEnd();
    RTThreadYield();
    UpdateTitlebar(TITLEBAR_SAVE);
    gpProgress = NULL;
    HRESULT hrc = gpMachine->SaveState(gpProgress.asOutParam());
    if (FAILED(hrc))
    {
        RTPrintf("Error saving state! rc=%Rhrc\n", hrc);
        return;
    }
    Assert(gpProgress);

    /*
     * Wait for the operation to be completed and work
     * the title bar in the mean while.
     */
    ULONG    cPercent = 0;
#ifndef RT_OS_DARWIN /* don't break the other guys yet. */
    for (;;)
    {
        BOOL fCompleted = false;
        hrc = gpProgress->COMGETTER(Completed)(&fCompleted);
        if (FAILED(hrc) || fCompleted)
            break;
        ULONG cPercentNow;
        hrc = gpProgress->COMGETTER(Percent)(&cPercentNow);
        if (FAILED(hrc))
            break;
        if (cPercentNow != cPercent)
        {
            UpdateTitlebar(TITLEBAR_SAVE, cPercent);
            cPercent = cPercentNow;
        }

        /* wait */
        hrc = gpProgress->WaitForCompletion(100);
        if (FAILED(hrc))
            break;
        /// @todo process gui events.
    }

#else /* new loop which processes GUI events while saving. */

    /* start regular timer so we don't starve in the event loop */
    SDL_TimerID sdlTimer;
    sdlTimer = SDL_AddTimer(100, StartupTimer, NULL);

    for (;;)
    {
        /*
         * Check for completion.
         */
        BOOL fCompleted = false;
        hrc = gpProgress->COMGETTER(Completed)(&fCompleted);
        if (FAILED(hrc) || fCompleted)
            break;
        ULONG cPercentNow;
        hrc = gpProgress->COMGETTER(Percent)(&cPercentNow);
        if (FAILED(hrc))
            break;
        if (cPercentNow != cPercent)
        {
            UpdateTitlebar(TITLEBAR_SAVE, cPercent);
            cPercent = cPercentNow;
        }

        /*
         * Wait for and process GUI a event.
         * This is necessary for XPCOM IPC and for updating the
         * title bar on the Mac.
         */
        SDL_Event event;
        if (WaitSDLEvent(&event))
        {
            switch (event.type)
            {
                /*
                 * Timer event preventing us from getting stuck.
                 */
                case SDL_USER_EVENT_TIMER:
                    break;

#ifdef USE_XPCOM_QUEUE_THREAD
                /*
                 * User specific XPCOM event queue event
                 */
                case SDL_USER_EVENT_XPCOM_EVENTQUEUE:
                {
                    LogFlow(("SDL_USER_EVENT_XPCOM_EVENTQUEUE: processing XPCOM event queue...\n"));
                    eventQ->ProcessPendingEvents();
                    signalXPCOMEventQueueThread();
                    break;
                }
#endif /* USE_XPCOM_QUEUE_THREAD */


                /*
                 * Ignore all other events.
                 */
                case SDL_USER_EVENT_NOTIFYCHANGE:
                case SDL_USER_EVENT_TERMINATE:
                default:
                    break;
            }
        }
    }

    /* kill the timer */
    SDL_RemoveTimer(sdlTimer);
    sdlTimer = 0;

#endif /* RT_OS_DARWIN */

    /*
     * What's the result of the operation?
     */
    LONG lrc;
    hrc = gpProgress->COMGETTER(ResultCode)(&lrc);
    if (FAILED(hrc))
        lrc = ~0;
    if (!lrc)
    {
        UpdateTitlebar(TITLEBAR_SAVE, 100);
        RTThreadYield();
        RTPrintf("Saved the state successfully.\n");
    }
    else
        RTPrintf("Error saving state, lrc=%d (%#x)\n", lrc, lrc);
}

/**
 * Build the titlebar string
 */
static void UpdateTitlebar(TitlebarMode mode, uint32_t u32User)
{
    static char szTitle[1024] = {0};

    /* back up current title */
    char szPrevTitle[1024];
    strcpy(szPrevTitle, szTitle);

    Bstr bstrName;
    gpMachine->COMGETTER(Name)(bstrName.asOutParam());

    RTStrPrintf(szTitle, sizeof(szTitle), "%s - " VBOX_PRODUCT,
                !bstrName.isEmpty() ? Utf8Str(bstrName).c_str() : "<noname>");

    /* which mode are we in? */
    switch (mode)
    {
        case TITLEBAR_NORMAL:
        {
            MachineState_T machineState;
            gpMachine->COMGETTER(State)(&machineState);
            if (machineState == MachineState_Paused)
                RTStrPrintf(szTitle + strlen(szTitle), sizeof(szTitle) - strlen(szTitle), " - [Paused]");

            if (gfGrabbed)
                RTStrPrintf(szTitle + strlen(szTitle), sizeof(szTitle) - strlen(szTitle), " - [Input captured]");

#if defined(DEBUG) || defined(VBOX_WITH_STATISTICS)
            // do we have a debugger interface
            if (gpMachineDebugger)
            {
                // query the machine state
                BOOL singlestepEnabled = FALSE;
                BOOL logEnabled = FALSE;
                VMExecutionEngine_T enmExecEngine = VMExecutionEngine_NotSet;
                ULONG virtualTimeRate = 100;
                gpMachineDebugger->COMGETTER(LogEnabled)(&logEnabled);
                gpMachineDebugger->COMGETTER(SingleStep)(&singlestepEnabled);
                gpMachineDebugger->COMGETTER(ExecutionEngine)(&enmExecEngine);
                gpMachineDebugger->COMGETTER(VirtualTimeRate)(&virtualTimeRate);
                RTStrPrintf(szTitle + strlen(szTitle), sizeof(szTitle) - strlen(szTitle),
                            " [STEP=%d LOG=%d EXEC=%s",
                            singlestepEnabled == TRUE, logEnabled == TRUE,
                              enmExecEngine == VMExecutionEngine_NotSet      ? "NotSet"
                            : enmExecEngine == VMExecutionEngine_Default     ? "Default"
                            : enmExecEngine == VMExecutionEngine_HwVirt      ? "HM"
                            : enmExecEngine == VMExecutionEngine_NativeApi   ? "NEM"
                            : enmExecEngine == VMExecutionEngine_Interpreter ? "Interpreter"
                            : enmExecEngine == VMExecutionEngine_Recompiler  ? "Recompiler" : "UNK");
                char *psz = strchr(szTitle, '\0');
                if (virtualTimeRate != 100)
                    RTStrPrintf(psz, &szTitle[sizeof(szTitle)] - psz, " WD=%d%%]", virtualTimeRate);
                else
                    RTStrPrintf(psz, &szTitle[sizeof(szTitle)] - psz, "]");
            }
#endif /* DEBUG || VBOX_WITH_STATISTICS */
            break;
        }

        case TITLEBAR_STARTUP:
        {
            /*
             * Format it.
             */
            MachineState_T machineState;
            gpMachine->COMGETTER(State)(&machineState);
            if (machineState == MachineState_Starting)
                RTStrPrintf(szTitle + strlen(szTitle), sizeof(szTitle) - strlen(szTitle),
                            " - Starting...");
            else if (machineState == MachineState_Restoring)
            {
                ULONG cPercentNow;
                HRESULT hrc = gpProgress->COMGETTER(Percent)(&cPercentNow);
                if (SUCCEEDED(hrc))
                    RTStrPrintf(szTitle + strlen(szTitle), sizeof(szTitle) - strlen(szTitle),
                                " - Restoring %d%%...", (int)cPercentNow);
                else
                    RTStrPrintf(szTitle + strlen(szTitle), sizeof(szTitle) - strlen(szTitle),
                                " - Restoring...");
            }
            else if (machineState == MachineState_TeleportingIn)
            {
                ULONG cPercentNow;
                HRESULT hrc = gpProgress->COMGETTER(Percent)(&cPercentNow);
                if (SUCCEEDED(hrc))
                    RTStrPrintf(szTitle + strlen(szTitle), sizeof(szTitle) - strlen(szTitle),
                                " - Teleporting %d%%...", (int)cPercentNow);
                else
                    RTStrPrintf(szTitle + strlen(szTitle), sizeof(szTitle) - strlen(szTitle),
                                " - Teleporting...");
            }
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
#ifdef VBOX_WIN32_UI
    setUITitle(szTitle);
#else
    for (unsigned i = 0; i < gcMonitors; i++)
        gpFramebuffer[i]->setWindowTitle(szTitle);
#endif
}

#if 0
static void vbox_show_shape(unsigned short w, unsigned short h,
                            uint32_t bg, const uint8_t *image)
{
    size_t x, y;
    unsigned short pitch;
    const uint32_t *color;
    const uint8_t *mask;
    size_t size_mask;

    mask = image;
    pitch = (w + 7) / 8;
    size_mask = (pitch * h + 3) & ~3;

    color = (const uint32_t *)(image + size_mask);

    printf("show_shape %dx%d pitch %d size mask %d\n",
           w, h, pitch, size_mask);
    for (y = 0; y < h; ++y, mask += pitch, color += w)
    {
        for (x = 0; x < w; ++x) {
            if (mask[x / 8] & (1 << (7 - (x % 8))))
                printf(" ");
            else
            {
                uint32_t c = color[x];
                if (c == bg)
                    printf("Y");
                else
                    printf("X");
            }
        }
        printf("\n");
    }
}
#endif

/**
 *  Sets the pointer shape according to parameters.
 *  Must be called only from the main SDL thread.
 */
static void SetPointerShape(const PointerShapeChangeData *data)
{
    /*
     * don't allow to change the pointer shape if we are outside the valid
     * guest area. In that case set standard mouse pointer is set and should
     * not get overridden.
     */
    if (gpOffCursor)
        return;

    if (data->shape.size() > 0)
    {
        bool ok = false;

#if defined(RT_OS_WINDOWS) || (defined(VBOXSDL_WITH_X11) && !defined(VBOX_WITHOUT_XCURSOR))
        AssertReturnVoid(data->height); /* Prevent division by zero. */
        uint32_t const  andMaskSize = (data->width + 7) / 8 * data->height;
        uint32_t const  srcShapePtrScan = data->width * 4;

        uint8_t  const *shape = data->shape.raw();
        uint8_t  const *srcAndMaskPtr = shape;
        uint8_t  const *srcShapePtr = shape + ((andMaskSize + 3) & ~3);
#endif

#if 0
        /* pointer debugging code */
        // vbox_show_shape(data->width, data->height, 0, data->shape);
        uint32_t shapeSize = ((((data->width + 7) / 8) * data->height + 3) & ~3) + data->width * 4 * data->height;
        printf("visible: %d\n", data->visible);
        printf("width = %d\n", data->width);
        printf("height = %d\n", data->height);
        printf("alpha = %d\n", data->alpha);
        printf("xhot = %d\n", data->xHot);
        printf("yhot = %d\n", data->yHot);
        printf("uint8_t pointerdata[] = { ");
        for (uint32_t i = 0; i < shapeSize; i++)
        {
            printf("0x%x, ", data->shape[i]);
        }
        printf("};\n");
#endif

#if defined(RT_OS_WINDOWS)
        BITMAPV5HEADER bi;
        HBITMAP hBitmap;
        void *lpBits;

        ::ZeroMemory(&bi, sizeof(BITMAPV5HEADER));
        bi.bV5Size = sizeof(BITMAPV5HEADER);
        bi.bV5Width = data->width;
        bi.bV5Height = -(LONG)data->height;
        bi.bV5Planes = 1;
        bi.bV5BitCount = 32;
        bi.bV5Compression = BI_BITFIELDS;
        // specify a supported 32 BPP alpha format for Windows XP
        bi.bV5RedMask   = 0x00FF0000;
        bi.bV5GreenMask = 0x0000FF00;
        bi.bV5BlueMask  = 0x000000FF;
        if (data->alpha)
            bi.bV5AlphaMask = 0xFF000000;
        else
            bi.bV5AlphaMask = 0;

        HDC hdc = ::GetDC(NULL);

        // create the DIB section with an alpha channel
        hBitmap = ::CreateDIBSection(hdc, (BITMAPINFO *)&bi, DIB_RGB_COLORS,
                                     (void **)&lpBits, NULL, (DWORD)0);

        ::ReleaseDC(NULL, hdc);

        HBITMAP hMonoBitmap = NULL;
        if (data->alpha)
        {
            // create an empty mask bitmap
            hMonoBitmap = ::CreateBitmap(data->width, data->height, 1, 1, NULL);
        }
        else
        {
            /* Word aligned AND mask. Will be allocated and created if necessary. */
            uint8_t *pu8AndMaskWordAligned = NULL;

            /* Width in bytes of the original AND mask scan line. */
            uint32_t cbAndMaskScan = (data->width + 7) / 8;

            if (cbAndMaskScan & 1)
            {
                /* Original AND mask is not word aligned. */

                /* Allocate memory for aligned AND mask. */
                pu8AndMaskWordAligned = (uint8_t *)RTMemTmpAllocZ((cbAndMaskScan + 1) * data->height);

                Assert(pu8AndMaskWordAligned);

                if (pu8AndMaskWordAligned)
                {
                    /* According to MSDN the padding bits must be 0.
                     * Compute the bit mask to set padding bits to 0 in the last byte of original AND mask.
                     */
                    uint32_t u32PaddingBits = cbAndMaskScan * 8  - data->width;
                    Assert(u32PaddingBits < 8);
                    uint8_t u8LastBytesPaddingMask = (uint8_t)(0xFF << u32PaddingBits);

                    Log(("u8LastBytesPaddingMask = %02X, aligned w = %d, width = %d, cbAndMaskScan = %d\n",
                          u8LastBytesPaddingMask, (cbAndMaskScan + 1) * 8, data->width, cbAndMaskScan));

                    uint8_t *src = (uint8_t *)srcAndMaskPtr;
                    uint8_t *dst = pu8AndMaskWordAligned;

                    unsigned i;
                    for (i = 0; i < data->height; i++)
                    {
                        memcpy(dst, src, cbAndMaskScan);

                        dst[cbAndMaskScan - 1] &= u8LastBytesPaddingMask;

                        src += cbAndMaskScan;
                        dst += cbAndMaskScan + 1;
                    }
                }
            }

            // create the AND mask bitmap
            hMonoBitmap = ::CreateBitmap(data->width, data->height, 1, 1,
                                         pu8AndMaskWordAligned? pu8AndMaskWordAligned: srcAndMaskPtr);

            if (pu8AndMaskWordAligned)
            {
                RTMemTmpFree(pu8AndMaskWordAligned);
            }
        }

        Assert(hBitmap);
        Assert(hMonoBitmap);
        if (hBitmap && hMonoBitmap)
        {
            DWORD *dstShapePtr = (DWORD *)lpBits;

            for (uint32_t y = 0; y < data->height; y ++)
            {
                memcpy(dstShapePtr, srcShapePtr, srcShapePtrScan);
                srcShapePtr += srcShapePtrScan;
                dstShapePtr += data->width;
            }
        }

        if (hMonoBitmap)
            ::DeleteObject(hMonoBitmap);
        if (hBitmap)
            ::DeleteObject(hBitmap);

#elif defined(VBOXSDL_WITH_X11) && !defined(VBOX_WITHOUT_XCURSOR)

        if (gfXCursorEnabled)
        {
            XcursorImage *img = XcursorImageCreate(data->width, data->height);
            Assert(img);
            if (img)
            {
                img->xhot = data->xHot;
                img->yhot = data->yHot;

                XcursorPixel *dstShapePtr = img->pixels;

                for (uint32_t y = 0; y < data->height; y ++)
                {
                    memcpy(dstShapePtr, srcShapePtr, srcShapePtrScan);

                    if (!data->alpha)
                    {
                        // convert AND mask to the alpha channel
                        uint8_t byte = 0;
                        for (uint32_t x = 0; x < data->width; x ++)
                        {
                            if (!(x % 8))
                                byte = *(srcAndMaskPtr ++);
                            else
                                byte <<= 1;

                            if (byte & 0x80)
                            {
                                // Linux doesn't support inverted pixels (XOR ops,
                                // to be exact) in cursor shapes, so we detect such
                                // pixels and always replace them with black ones to
                                // make them visible at least over light colors
                                if (dstShapePtr [x] & 0x00FFFFFF)
                                    dstShapePtr [x] = 0xFF000000;
                                else
                                    dstShapePtr [x] = 0x00000000;
                            }
                            else
                                dstShapePtr [x] |= 0xFF000000;
                        }
                    }

                    srcShapePtr += srcShapePtrScan;
                    dstShapePtr += data->width;
                }
            }
            XcursorImageDestroy(img);
        }

#endif /* VBOXSDL_WITH_X11 && !VBOX_WITHOUT_XCURSOR */

        if (!ok)
        {
            SDL_SetCursor(gpDefaultCursor);
            SDL_ShowCursor(SDL_ENABLE);
        }
    }
    else
    {
        if (data->visible)
            SDL_ShowCursor(SDL_ENABLE);
        else if (gfAbsoluteMouseGuest)
            /* Don't disable the cursor if the guest additions are not active (anymore) */
            SDL_ShowCursor(SDL_DISABLE);
    }
}

/**
 * Handle changed mouse capabilities
 */
static void HandleGuestCapsChanged(void)
{
    if (!gfAbsoluteMouseGuest)
    {
        // Cursor could be overwritten by the guest tools
        SDL_SetCursor(gpDefaultCursor);
        SDL_ShowCursor(SDL_ENABLE);
        gpOffCursor = NULL;
    }
    if (gpMouse && UseAbsoluteMouse())
    {
        // Actually switch to absolute coordinates
        if (gfGrabbed)
            InputGrabEnd();
        gpMouse->PutMouseEventAbsolute(-1, -1, 0, 0, 0);
    }
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
        /* Control-Alt-Delete */
        case SDLK_DELETE:
        {
            gpKeyboard->PutCAD();
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
            MachineState_T machineState;
            gpMachine->COMGETTER(State)(&machineState);
            bool fPauseIt = machineState == MachineState_Running
                         || machineState == MachineState_Teleporting
                         || machineState == MachineState_LiveSnapshotting;
            if (fPauseIt)
                gpConsole->Pause();
            SetFullscreen(!gpFramebuffer[0]->getFullscreen());
            if (fPauseIt)
                gpConsole->Resume();

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

            MachineState_T machineState;
            gpMachine->COMGETTER(State)(&machineState);
            if (   machineState == MachineState_Running
                || machineState == MachineState_Teleporting
                || machineState == MachineState_LiveSnapshotting
               )
            {
                if (gfGrabbed)
                    InputGrabEnd();
                gpConsole->Pause();
            }
            else if (machineState == MachineState_Paused)
            {
                gpConsole->Resume();
            }
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

        /*
         * Perform an online snapshot. Continue operation.
         */
        case SDLK_n:
        {
            if (strchr(gHostKeyDisabledCombinations, 'n'))
                return VERR_NOT_SUPPORTED;

            RTThreadYield();
            ULONG cSnapshots = 0;
            gpMachine->COMGETTER(SnapshotCount)(&cSnapshots);
            char pszSnapshotName[20];
            RTStrPrintf(pszSnapshotName, sizeof(pszSnapshotName), "Snapshot %d", cSnapshots + 1);
            gpProgress = NULL;
            HRESULT hrc;
            Bstr snapId;
            CHECK_ERROR(gpMachine, TakeSnapshot(Bstr(pszSnapshotName).raw(),
                                                Bstr("Taken by VBoxSDL").raw(),
                                                TRUE, snapId.asOutParam(),
                                                gpProgress.asOutParam()));
            if (FAILED(hrc))
            {
                RTPrintf("Error taking snapshot! rc=%Rhrc\n", hrc);
                /* continue operation */
                return VINF_SUCCESS;
            }
            /*
             * Wait for the operation to be completed and work
             * the title bar in the mean while.
             */
            ULONG    cPercent = 0;
            for (;;)
            {
                BOOL fCompleted = false;
                hrc = gpProgress->COMGETTER(Completed)(&fCompleted);
                if (FAILED(hrc) || fCompleted)
                    break;
                ULONG cPercentNow;
                hrc = gpProgress->COMGETTER(Percent)(&cPercentNow);
                if (FAILED(hrc))
                    break;
                if (cPercentNow != cPercent)
                {
                    UpdateTitlebar(TITLEBAR_SNAPSHOT, cPercent);
                    cPercent = cPercentNow;
                }

                /* wait */
                hrc = gpProgress->WaitForCompletion(100);
                if (FAILED(hrc))
                    break;
                /// @todo process gui events.
            }

            /* continue operation */
            return VINF_SUCCESS;
        }

        case SDLK_F1: case SDLK_F2: case SDLK_F3:
        case SDLK_F4: case SDLK_F5: case SDLK_F6:
        case SDLK_F7: case SDLK_F8: case SDLK_F9:
        case SDLK_F10: case SDLK_F11: case SDLK_F12:
        {
            /* send Ctrl-Alt-Fx to guest */
            gpKeyboard->PutUsageCode(0xE0 /*left ctrl*/, 0x07 /*usage code page id*/, FALSE);
            gpKeyboard->PutUsageCode(0xE2 /*left alt*/, 0x07 /*usage code page id*/, FALSE);
            gpKeyboard->PutUsageCode(pEv->keysym.sym,  0x07 /*usage code page id*/, FALSE);
            gpKeyboard->PutUsageCode(pEv->keysym.sym,  0x07 /*usage code page id*/, TRUE);
            gpKeyboard->PutUsageCode(0xE0 /*left ctrl*/, 0x07 /*usage code page id*/, TRUE);
            gpKeyboard->PutUsageCode(0xE2 /*left alt*/, 0x07 /*usage code page id*/, TRUE);
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
 * Timer callback function for startup processing
 */
static Uint32 StartupTimer(Uint32 interval, void *param) RT_NOTHROW_DEF
{
    RT_NOREF(param);

    /* post message so we can do something in the startup loop */
    SDL_Event event = {0};
    event.type      = SDL_USEREVENT;
    event.user.type = SDL_USER_EVENT_TIMER;
    SDL_PushEvent(&event);
    RTSemEventSignal(g_EventSemSDLEvents);
    return interval;
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
 * Timer callback function to check if an ACPI power button event was handled by the guest.
 */
static Uint32 QuitTimer(Uint32 interval, void *param) RT_NOTHROW_DEF
{
    RT_NOREF(interval, param);

    BOOL fHandled = FALSE;

    gSdlQuitTimer = 0;
    if (gpConsole)
    {
        int rc = gpConsole->GetPowerButtonHandled(&fHandled);
        LogRel(("QuitTimer: rc=%d handled=%d\n", rc, fHandled));
        if (RT_FAILURE(rc) || !fHandled)
        {
            /* event was not handled, power down the guest */
            gfACPITerm = FALSE;
            SDL_Event event = {0};
            event.type = SDL_QUIT;
            PushSDLEventForSure(&event);
        }
    }
    /* one-shot */
    return 0;
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
        {
#ifdef USE_XPCOM_QUEUE_THREAD
            if (event->type == SDL_USER_EVENT_XPCOM_EVENTQUEUE)
                consumedXPCOMUserEvent();
#endif
            return 1;
        }
        /* Immediately wake up if new SDL events are available. This does not
         * work for internal SDL events. Don't wait more than 10ms. */
        RTSemEventWait(g_EventSemSDLEvents, 10);
    }
}

/**
 * Ensure that an SDL event is really enqueued. Try multiple times if necessary.
 */
int PushSDLEventForSure(SDL_Event *event)
{
    int ntries = 10;
    for (; ntries > 0; ntries--)
    {
        int rc = SDL_PushEvent(event);
        RTSemEventSignal(g_EventSemSDLEvents);
        if (rc == 1)
            return 0;
        Log(("PushSDLEventForSure: waiting for 2ms (rc = %d)\n", rc));
        RTThreadSleep(2);
    }
    LogRel(("WARNING: Failed to enqueue SDL event %d.%d!\n",
            event->type, event->type == SDL_USEREVENT ? event->user.type : 0));
    return -1;
}

#if defined(VBOXSDL_WITH_X11) || defined(RT_OS_DARWIN)
/**
 * Special SDL_PushEvent function for NotifyUpdate events. These events may occur in bursts
 * so make sure they don't flood the SDL event queue.
 */
void PushNotifyUpdateEvent(SDL_Event *event)
{
    int rc = SDL_PushEvent(event);
    bool fSuccess = (rc == 1);

    RTSemEventSignal(g_EventSemSDLEvents);
    AssertMsg(fSuccess, ("SDL_PushEvent returned SDL error\n"));
    /* A global counter is faster than SDL_PeepEvents() */
    if (fSuccess)
        ASMAtomicIncS32(&g_cNotifyUpdateEventsPending);
    /* In order to not flood the SDL event queue, yield the CPU or (if there are already many
     * events queued) even sleep */
    if (g_cNotifyUpdateEventsPending > 96)
    {
        /* Too many NotifyUpdate events, sleep for a small amount to give the main thread time
         * to handle these events. The SDL queue can hold up to 128 events. */
        Log(("PushNotifyUpdateEvent: Sleep 1ms\n"));
        RTThreadSleep(1);
    }
    else
        RTThreadYield();
}
#endif /* VBOXSDL_WITH_X11 */

/**
 *
 */
static void SetFullscreen(bool enable)
{
    if (enable == gpFramebuffer[0]->getFullscreen())
        return;

    if (!gfFullscreenResize)
    {
        /*
         * The old/default way: SDL will resize the host to fit the guest screen resolution.
         */
        gpFramebuffer[0]->setFullscreen(enable);
    }
    else
    {
        /*
         * The alternate way: Switch to fullscreen with the host screen resolution and adapt
         * the guest screen resolution to the host window geometry.
         */
        uint32_t NewWidth = 0, NewHeight = 0;
        if (enable)
        {
            /* switch to fullscreen */
            gmGuestNormalXRes = gpFramebuffer[0]->getGuestXRes();
            gmGuestNormalYRes = gpFramebuffer[0]->getGuestYRes();
            gpFramebuffer[0]->getFullscreenGeometry(&NewWidth, &NewHeight);
        }
        else
        {
            /* switch back to saved geometry */
            NewWidth  = gmGuestNormalXRes;
            NewHeight = gmGuestNormalYRes;
        }
        if (NewWidth != 0 && NewHeight != 0)
        {
            gpFramebuffer[0]->setFullscreen(enable);
            gfIgnoreNextResize = TRUE;
            gpDisplay->SetVideoModeHint(0 /*=display*/, true /*=enabled*/,
                                        false /*=changeOrigin*/, 0 /*=originX*/, 0 /*=originY*/,
                                        NewWidth, NewHeight, 0 /*don't change bpp*/, true /*=notify*/);
        }
    }
}

static VBoxSDLFB *getFbFromWinId(Uint32 id)
{
    for (unsigned i = 0; i < gcMonitors; i++)
        if (gpFramebuffer[i]->hasWindow(id))
            return gpFramebuffer[i];

    return NULL;
}
