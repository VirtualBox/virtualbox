/* $Id: VBoxFB.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxFB - Linux Direct Framebuffer Frontend.
 *
 * @note This code has not been tested in a long time, so expect bugs if it
 *       even compiles.  It is not part of any regular VirtualBox build.
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

#include "VBoxFB.h"
#include "Framebuffer.h"
#include <getopt.h>
#include <VBox/param.h>
#include <iprt/path.h>
#include <VBox/version.h>

/**
 * Globals
 */
uint32_t g_useFixedVideoMode = 0;
int g_scaleGuest = 0;
videoMode g_fixedVideoMode = {0};
int32_t g_initialVideoMode = -1;

void showusage()
{
    printf("\nThe following parameters are supported:\n"
            "--startvm uuid       start VM with UUID 'uuid'\n"
            "--fixedres WxHxBPP   always use fixed host resolution\n"
            "--listhostmodes      display list of supported host display modes and exit\n"
            "--scale              scale guest video mode to host video mode\n"
            "--nodirectblit       disable direct blitting, use intermediate framebuffer\n"
            "--showlabel          show VM name on top of the VM display\n");
}

/** entry point */
int main(int argc, char *argv[])
{
    const char *uuid = NULL;
    int c;
    int listHostModes = 0;
    int quit = 0;
    const struct option options[] =
    {
        { "help",          no_argument,       NULL, 'h' },
        { "startvm",       required_argument, NULL, 's' },
        { "fixedres",      required_argument, NULL, 'f' },
        { "listhostmodes", no_argument,       NULL, 'l' },
        { "scale",         no_argument,       NULL, 'c' }
    };

    printf("VirtualBox DirectFB GUI built %s %s\n"
           "Copyright (C) 2004-" VBOX_C_YEAR " " VBOX_VENDOR "\n"
           "Copyright (C) 2004-2005 secunet Security Networks AG\n", __DATE__, __TIME__);

    fputs("\nWARNING! Unmaintained code.\nWARNING! Needs fixing & debugging!\n\n", stdout);

    for (;;)
    {
        c = getopt_long(argc, argv, "s:", options, NULL);
        if (c == -1)
            break;
        switch (c)
        {
            case 'h':
            {
                showusage();
                exit(0);
            }
            case 's':
            {
#if 0
                // UUID as string, parse it
                RTUUID buuid;
                if (!RT_SUCCESS(RTUuidFromStr((PRTUUID)&buuid, optarg)))
                {
                    printf("Error, invalid UUID format given!\n");
                    showusage();
                    exit(-1);
                }
#endif
                uuid = optarg;
                break;
            }
            case 'f':
            {
                if (sscanf(optarg, "%ux%ux%u", &g_fixedVideoMode.width, &g_fixedVideoMode.height,
                           &g_fixedVideoMode.bpp) != 3)
                {
                    printf("Error, invalid resolution argument!\n");
                    showusage();
                    exit(-1);
                }
                g_useFixedVideoMode = 1;
                break;
            }
            case 'l':
            {
                listHostModes = 1;
                break;
            }
            case 'c':
            {
                g_scaleGuest = 1;
                break;
            }
            default:
                break;
        }
    }

    // check if we got a UUID
    if (!uuid)
    {
        printf("Error, no UUID given!\n");
        showusage();
        exit(-1);
    }


    /*
     * XPCOM setup
     */

    nsresult rc;
    /*
     * Note that we scope all nsCOMPtr variables in order to have all XPCOM
     * objects automatically released before we call NS_ShutdownXPCOM at the
     * end. This is an XPCOM requirement.
     */
    {
#if 0
        nsCOMPtr<nsIServiceManager> serviceManager;
        rc = NS_InitXPCOM2(getter_AddRefs(serviceManager), nsnull, nsnull);
        if (NS_FAILED(rc))
        {
            printf("Error: XPCOM could not be initialized! rc=0x%x\n", rc);
            exit(-1);
        }

        // register our component
        nsCOMPtr<nsIComponentRegistrar> registrar = do_QueryInterface(serviceManager);
        if (!registrar)
        {
            printf("Error: could not query nsIComponentRegistrar interface!\n");
            exit(-1);
        }
        registrar->AutoRegister(nsnull);

        /*
         * Make sure the main event queue is created. This event queue is
         * responsible for dispatching incoming XPCOM IPC messages. The main
         * thread should run this event queue's loop during lengthy non-XPCOM
         * operations to ensure messages from the VirtualBox server and other
         * XPCOM IPC clients are processed. This use case doesn't perform such
         * operations so it doesn't run the event loop.
         */
        nsCOMPtr<nsIEventQueue> eventQ;
        rc = NS_GetMainEventQ(getter_AddRefs(eventQ));
        if (NS_FAILED(rc))
        {
            printf("Error: could not get main event queue! rc=%08X\n", rc);
            return -1;
        }
#else
        rc = com::Initialize();
        if (NS_FAILED(rc))
        {
            printf("Error: XPCOM could not be initialized! rc=0x%x\n", rc);
            exit(-1);
        }
#endif

        /*
         * Now XPCOM is ready and we can start to do real work.
         * IVirtualBox is the root interface of VirtualBox and will be
         * retrieved from the XPCOM component manager. We use the
         * XPCOM provided smart pointer nsCOMPtr for all objects because
         * that's very convenient and removes the need deal with reference
         * counting and freeing.
         */
        nsCOMPtr<nsIComponentManager> manager;
        rc = NS_GetComponentManager(getter_AddRefs(manager));
        if (NS_FAILED(rc))
        {
            printf("Error: could not get component manager! rc=%08X\n", rc);
            exit(-1);
        }

        nsCOMPtr<IVirtualBox> virtualBox;
        rc = manager->CreateInstanceByContractID(NS_VIRTUALBOX_CONTRACTID,
                                                 nsnull,
                                                 NS_GET_IID(IVirtualBox),
                                                 getter_AddRefs(virtualBox));
        if (NS_FAILED(rc))
        {
            printf("Error, could not instantiate object! rc=0x%x\n", rc);
            exit(-1);
        }

        nsCOMPtr<ISession> session;
        rc = manager->CreateInstance(CLSID_Session,
                                     nsnull,
                                     NS_GET_IID(ISession),
                                     getter_AddRefs(session));
        if (NS_FAILED(rc))
        {
            printf("Error: could not instantiate Session object! rc = %08X\n", rc);
            exit(-1);
        }

        // find the VM
        nsCOMPtr<IMachine> machine;
        rc = virtualBox->FindMachine(NS_ConvertUTF8toUTF16(uuid).get(), getter_AddRefs(machine));
        if (rc != S_OK || !machine)
        {
            printf("Error: given machine not found!\n");
            return RTEXITCODE_FAILURE;
        }

        // open session for this VM
        rc = machine->LockMachine(session, LockType_VM);
        if (NS_FAILED(rc))
        {
            printf("Error: given machine not found!\n");
            exit(-1);
        }
        session->GetMachine(getter_AddRefs(machine));
        if (!machine)
        {
            printf("Error: given machine not found!\n");
            exit(-1);
        }
        nsCOMPtr<IConsole> console;
        session->GetConsole(getter_AddRefs(console));
        if (!console)
        {
            printf("Error: cannot get console!\n");
            exit(-1);
        }

        nsCOMPtr<IDisplay> display;
        console->GetDisplay(getter_AddRefs(display));
        if (!display)
        {
            printf("Error: could not get display object!\n");
            exit(-1);
        }

        nsCOMPtr<IKeyboard> keyboard;
        nsCOMPtr<IMouse> mouse;
        VBoxDirectFB *frameBuffer = NULL;

        /*
         * Init DirectFB
         */
        IDirectFB *dfb = NULL;
        IDirectFBSurface *surface = NULL;
        IDirectFBInputDevice *dfbKeyboard = NULL;
        IDirectFBInputDevice *dfbMouse = NULL;
        IDirectFBEventBuffer *dfbEventBuffer = NULL;
        DFBSurfaceDescription dsc;
        int screen_width, screen_height;

        DFBCHECK(DirectFBInit(&argc, &argv));
        DFBCHECK(DirectFBCreate(&dfb));
        DFBCHECK(dfb->SetCooperativeLevel(dfb, DFSCL_FULLSCREEN));
        // populate our structure of supported video modes
        DFBCHECK(dfb->EnumVideoModes(dfb, enumVideoModesHandler, NULL));

        if (listHostModes)
        {
            printf("*****************************************************\n");
            printf("Number of available host video modes: %u\n", g_numVideoModes);
            for (uint32_t i = 0; i < g_numVideoModes; i++)
            {
                printf("Mode %u: xres = %u, yres = %u, bpp = %u\n", i,
                       g_videoModes[i].width, g_videoModes[i].height, g_videoModes[i].bpp);
            }
            printf("Note: display modes with bpp < have been filtered out\n");
            printf("*****************************************************\n");
            goto Leave;
        }

        if (g_useFixedVideoMode)
        {
            int32_t bestVideoMode = getBestVideoMode(g_fixedVideoMode.width,
                                                     g_fixedVideoMode.height,
                                                     g_fixedVideoMode.bpp);
            // validate the fixed mode
            if (   bestVideoMode == -1
                || g_fixedVideoMode.width  != g_videoModes[bestVideoMode].width
                || g_fixedVideoMode.height != g_videoModes[bestVideoMode].height
                || g_fixedVideoMode.bpp    != g_videoModes[bestVideoMode].bpp)
            {
                printf("Error: the specified fixed video mode is not available!\n");
                exit(-1);
            }
        } else
        {
            g_initialVideoMode = getBestVideoMode(640, 480, 16);
            if (g_initialVideoMode == -1)
            {
                printf("Error: initial video mode 640x480x16 is not available!\n");
                exit(-1);
            }
        }

        dsc.flags = DSDESC_CAPS;
        dsc.caps = DSCAPS_PRIMARY;
        DFBCHECK(dfb->CreateSurface(dfb, &dsc, &surface));
        DFBCHECK(surface->Clear(surface, 0, 0, 0, 0));
        DFBCHECK(surface->GetSize(surface, &screen_width, &screen_height));
        DFBCHECK(dfb->GetInputDevice(dfb, DIDID_KEYBOARD, &dfbKeyboard));
        DFBCHECK(dfbKeyboard->CreateEventBuffer(dfbKeyboard, &dfbEventBuffer));
        DFBCHECK(dfb->GetInputDevice(dfb, DIDID_MOUSE, &dfbMouse));
        DFBCHECK(dfbMouse->AttachEventBuffer(dfbMouse, dfbEventBuffer));


        if (g_useFixedVideoMode)
        {
            printf("Information: setting video mode to %ux%ux%u\n", g_fixedVideoMode.width,
                   g_fixedVideoMode.height, g_fixedVideoMode.bpp);
            DFBCHECK(dfb->SetVideoMode(dfb, g_fixedVideoMode.width,
                                       g_fixedVideoMode.height, g_fixedVideoMode.bpp));
        } else
        {
            printf("Information: starting with default video mode %ux%ux%u\n",
                   g_videoModes[g_initialVideoMode].width, g_videoModes[g_initialVideoMode].height,
                   g_videoModes[g_initialVideoMode].bpp);
            DFBCHECK(dfb->SetVideoMode(dfb,
                                       g_videoModes[g_initialVideoMode].width,
                                       g_videoModes[g_initialVideoMode].height,
                                       g_videoModes[g_initialVideoMode].bpp));
        }

        // register our framebuffer
        frameBuffer = new VBoxDirectFB(dfb, surface);
        PRUnichar *pwszFrameBufferUuid = NULL;
        display->AttachFramebuffer(0, frameBuffer, &pwszFrameBufferUuid);

        /*
         * Start the VM execution thread
         */
        console->PowerUp(NULL);

        console->GetKeyboard(getter_AddRefs(keyboard));
        console->GetMouse(getter_AddRefs(mouse));

        /*
         * Main event loop
         */
        #define MAX_KEYEVENTS 10
        PRInt32 keyEvents[MAX_KEYEVENTS];
        int numKeyEvents;

        while (!quit)
        {
            DFBInputEvent event;

            numKeyEvents = 0;
            DFBCHECK(dfbEventBuffer->WaitForEvent(dfbEventBuffer));
            while (dfbEventBuffer->GetEvent(dfbEventBuffer, DFB_EVENT(&event)) == DFB_OK)
            {
                int mouseXDelta = 0;
                int mouseYDelta = 0;
                int mouseZDelta = 0;
                switch (event.type)
                {
                    #define QUEUEEXT() keyEvents[numKeyEvents++] = 0xe0
                    #define QUEUEKEY(scan) keyEvents[numKeyEvents++] = scan | (event.type == DIET_KEYRELEASE ? 0x80 : 0x00)
                    #define QUEUEKEYRAW(scan) keyEvents[numKeyEvents++] = scan
                    case DIET_KEYPRESS:
                    case DIET_KEYRELEASE:
                    {
                        // @@@AH development hack to get out of it!
                        if ((event.key_id == DIKI_ESCAPE) && (event.modifiers & (DIMM_CONTROL | DIMM_ALT)))
                            quit = 1;

                        if (numKeyEvents < MAX_KEYEVENTS)
                        {
                            //printf("%s: key_code: 0x%x\n", event.type == DIET_KEYPRESS ? "DIET_KEYPRESS" : "DIET_KEYRELEASE", event.key_code);
                            switch ((uint32_t)event.key_id)
                            {
                                case DIKI_CONTROL_R:
                                    QUEUEEXT();
                                    QUEUEKEY(0x1d);
                                    break;
                                case DIKI_INSERT:
                                    QUEUEEXT();
                                    QUEUEKEY(0x52);
                                    break;
                                case DIKI_DELETE:
                                    QUEUEEXT();
                                    QUEUEKEY(0x53);
                                    break;
                                case DIKI_HOME:
                                    QUEUEEXT();
                                    QUEUEKEY(0x47);
                                    break;
                                case DIKI_END:
                                    QUEUEEXT();
                                    QUEUEKEY(0x4f);
                                    break;
                                case DIKI_PAGE_UP:
                                    QUEUEEXT();
                                    QUEUEKEY(0x49);
                                    break;
                                case DIKI_PAGE_DOWN:
                                    QUEUEEXT();
                                    QUEUEKEY(0x51);
                                    break;
                                case DIKI_LEFT:
                                    QUEUEEXT();
                                    QUEUEKEY(0x4b);
                                    break;
                                case DIKI_RIGHT:
                                    QUEUEEXT();
                                    QUEUEKEY(0x4d);
                                    break;
                                case DIKI_UP:
                                    QUEUEEXT();
                                    QUEUEKEY(0x48);
                                    break;
                                case DIKI_DOWN:
                                    QUEUEEXT();
                                    QUEUEKEY(0x50);
                                    break;
                                case DIKI_KP_DIV:
                                    QUEUEEXT();
                                    QUEUEKEY(0x35);
                                    break;
                                case DIKI_KP_ENTER:
                                    QUEUEEXT();
                                    QUEUEKEY(0x1c);
                                    break;
                                case DIKI_PRINT:
                                    // the break code is inverted!
                                    if (event.type == DIET_KEYPRESS)
                                    {
                                        QUEUEEXT();
                                        QUEUEKEY(0x2a);
                                        QUEUEEXT();
                                        QUEUEKEY(0x37);
                                    } else
                                    {
                                        QUEUEEXT();
                                        QUEUEKEY(0x37);
                                        QUEUEEXT();
                                        QUEUEKEY(0x2a);
                                    }
                                    break;
                                case DIKI_PAUSE:
                                    // This is a super weird key. No break code and a 6 byte
                                    // combination.
                                    if (event.type == DIET_KEYPRESS)
                                    {
                                        QUEUEKEY(0xe1);
                                        QUEUEKEY(0x1d);
                                        QUEUEKEY(0x45);
                                        QUEUEKEY(0xe1);
                                        QUEUEKEY(0x9d);
                                        QUEUEKEY(0xc5);
                                    }
                                    break;
                                case DIKI_META_L:
                                    // the left Windows logo is a bit different
                                    if (event.type == DIET_KEYPRESS)
                                    {
                                        QUEUEEXT();
                                        QUEUEKEYRAW(0x1f);
                                    } else
                                    {
                                        QUEUEEXT();
                                        QUEUEKEYRAW(0xf0);
                                        QUEUEKEYRAW(0x1f);
                                    }
                                    break;
                                case DIKI_META_R:
                                    // the right Windows logo is a bit different
                                    if (event.type == DIET_KEYPRESS)
                                    {
                                        QUEUEEXT();
                                        QUEUEKEYRAW(0x27);
                                    } else
                                    {
                                        QUEUEEXT();
                                        QUEUEKEYRAW(0xf0);
                                        QUEUEKEYRAW(0x27);
                                    }
                                    break;
                                case DIKI_SUPER_R:
                                    // the popup menu is a bit different
                                    if (event.type == DIET_KEYPRESS)
                                    {
                                        QUEUEEXT();
                                        QUEUEKEYRAW(0x2f);
                                    } else
                                    {
                                        QUEUEEXT();
                                        QUEUEKEYRAW(0xf0);
                                        QUEUEKEYRAW(0x2f);
                                    }
                                    break;

                                default:
                                    // check if we got a hardware scancode
                                    if (event.key_code != -1)
                                    {
                                        // take the scancode from DirectFB as is
                                        QUEUEKEY(event.key_code);
                                    } else
                                    {
                                        // XXX need extra handling!
                                    }
                            }
                        }
                        break;
                    }
                    #undef QUEUEEXT
                    #undef QUEUEKEY
                    #undef QUEUEKEYRAW

                    case DIET_AXISMOTION:
                    {
                        switch (event.axis)
                        {
                            case DIAI_X:
                                mouseXDelta += event.axisrel;
                                break;
                            case DIAI_Y:
                                mouseYDelta += event.axisrel;
                                break;
                            case DIAI_Z:
                                mouseZDelta += event.axisrel;
                                break;
                            default:
                                break;
                        }
                        // fall through
                    }
                    case DIET_BUTTONPRESS:
                        // fall through;
                    case DIET_BUTTONRELEASE:
                    {
                        int buttonState = 0;
                        if (event.buttons & DIBM_LEFT)
                            buttonState |= MouseButtonState::LeftButton;
                        if (event.buttons & DIBM_RIGHT)
                            buttonState |= MouseButtonState::RightButton;
                        if (event.buttons & DIBM_MIDDLE)
                            buttonState |= MouseButtonState::MiddleButton;
                        mouse->PutMouseEvent(mouseXDelta, mouseYDelta, mouseZDelta, 0, buttonState);
                        break;
                    }
                    default:
                        break;
                }
            }
            // did we get any keyboard events?
            if (numKeyEvents > 0)
            {
                uint32_t codesStored;
                if (numKeyEvents > 1)
                {
                    keyboard->PutScancodes(numKeyEvents, keyEvents,
                                           &codesStored);
                } else
                {
                    keyboard->PutScancode(keyEvents[0]);
                }
            }
        }
        {
            nsCOMPtr<IProgress> progress;
            console->PowerDown(getter_AddRefs(progress));
            progress->WaitForCompletion(-1);
        }
    }

Leave:
    /*
     * Perform the standard XPCOM shutdown procedure.
     */
    NS_ShutdownXPCOM(nsnull);

    return 0;
}
