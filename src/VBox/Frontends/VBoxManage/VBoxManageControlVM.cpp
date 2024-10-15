/* $Id: VBoxManageControlVM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxManage - Implementation of the controlvm command.
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
#include <VBox/com/com.h>
#include <VBox/com/string.h>
#include <VBox/com/Guid.h>
#include <VBox/com/array.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>
#include <VBox/com/VirtualBox.h>

#include <iprt/ctype.h>
#include <iprt/getopt.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/uuid.h>
#include <iprt/file.h>
#include <VBox/log.h>

#include "VBoxManage.h"
#include "VBoxManageUtils.h"

#include <list>

DECLARE_TRANSLATION_CONTEXT(ControlVM);

/**
 * Parses a number.
 *
 * @returns Valid number on success.
 * @returns 0 if invalid number. All necessary bitching has been done.
 * @param   psz     Pointer to the nic number.
 */
static unsigned parseNum(const char *psz, unsigned cMaxNum, const char *name)
{
    uint32_t u32;
    char *pszNext;
    int vrc = RTStrToUInt32Ex(psz, &pszNext, 10, &u32);
    if (    RT_SUCCESS(vrc)
        &&  *pszNext == '\0'
        &&  u32 >= 1
        &&  u32 <= cMaxNum)
        return (unsigned)u32;
    errorArgument(ControlVM::tr("Invalid %s number '%s'."), name, psz);
    return 0;
}

#define KBDCHARDEF_MOD_NONE  0x00
#define KBDCHARDEF_MOD_SHIFT 0x01

typedef struct KBDCHARDEF
{
    uint8_t u8Scancode;
    uint8_t u8Modifiers;
} KBDCHARDEF;

static const KBDCHARDEF g_aASCIIChars[0x80] =
{
    /* 0x00 ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x01 ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x02 ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x03 ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x04 ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x05 ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x06 ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x07 ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x08 ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x09 ' ' */ {0x0f, KBDCHARDEF_MOD_NONE},
    /* 0x0A ' ' */ {0x1c, KBDCHARDEF_MOD_NONE},
    /* 0x0B ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x0C ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x0D ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x0E ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x0F ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x10 ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x11 ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x12 ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x13 ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x14 ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x15 ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x16 ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x17 ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x18 ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x19 ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x1A ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x1B ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x1C ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x1D ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x1E ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x1F ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
    /* 0x20 ' ' */ {0x39, KBDCHARDEF_MOD_NONE},
    /* 0x21 '!' */ {0x02, KBDCHARDEF_MOD_SHIFT},
    /* 0x22 '"' */ {0x28, KBDCHARDEF_MOD_SHIFT},
    /* 0x23 '#' */ {0x04, KBDCHARDEF_MOD_SHIFT},
    /* 0x24 '$' */ {0x05, KBDCHARDEF_MOD_SHIFT},
    /* 0x25 '%' */ {0x06, KBDCHARDEF_MOD_SHIFT},
    /* 0x26 '&' */ {0x08, KBDCHARDEF_MOD_SHIFT},
    /* 0x27 ''' */ {0x28, KBDCHARDEF_MOD_NONE},
    /* 0x28 '(' */ {0x0a, KBDCHARDEF_MOD_SHIFT},
    /* 0x29 ')' */ {0x0b, KBDCHARDEF_MOD_SHIFT},
    /* 0x2A '*' */ {0x09, KBDCHARDEF_MOD_SHIFT},
    /* 0x2B '+' */ {0x0d, KBDCHARDEF_MOD_SHIFT},
    /* 0x2C ',' */ {0x33, KBDCHARDEF_MOD_NONE},
    /* 0x2D '-' */ {0x0c, KBDCHARDEF_MOD_NONE},
    /* 0x2E '.' */ {0x34, KBDCHARDEF_MOD_NONE},
    /* 0x2F '/' */ {0x35, KBDCHARDEF_MOD_NONE},
    /* 0x30 '0' */ {0x0b, KBDCHARDEF_MOD_NONE},
    /* 0x31 '1' */ {0x02, KBDCHARDEF_MOD_NONE},
    /* 0x32 '2' */ {0x03, KBDCHARDEF_MOD_NONE},
    /* 0x33 '3' */ {0x04, KBDCHARDEF_MOD_NONE},
    /* 0x34 '4' */ {0x05, KBDCHARDEF_MOD_NONE},
    /* 0x35 '5' */ {0x06, KBDCHARDEF_MOD_NONE},
    /* 0x36 '6' */ {0x07, KBDCHARDEF_MOD_NONE},
    /* 0x37 '7' */ {0x08, KBDCHARDEF_MOD_NONE},
    /* 0x38 '8' */ {0x09, KBDCHARDEF_MOD_NONE},
    /* 0x39 '9' */ {0x0a, KBDCHARDEF_MOD_NONE},
    /* 0x3A ':' */ {0x27, KBDCHARDEF_MOD_SHIFT},
    /* 0x3B ';' */ {0x27, KBDCHARDEF_MOD_NONE},
    /* 0x3C '<' */ {0x33, KBDCHARDEF_MOD_SHIFT},
    /* 0x3D '=' */ {0x0d, KBDCHARDEF_MOD_NONE},
    /* 0x3E '>' */ {0x34, KBDCHARDEF_MOD_SHIFT},
    /* 0x3F '?' */ {0x35, KBDCHARDEF_MOD_SHIFT},
    /* 0x40 '@' */ {0x03, KBDCHARDEF_MOD_SHIFT},
    /* 0x41 'A' */ {0x1e, KBDCHARDEF_MOD_SHIFT},
    /* 0x42 'B' */ {0x30, KBDCHARDEF_MOD_SHIFT},
    /* 0x43 'C' */ {0x2e, KBDCHARDEF_MOD_SHIFT},
    /* 0x44 'D' */ {0x20, KBDCHARDEF_MOD_SHIFT},
    /* 0x45 'E' */ {0x12, KBDCHARDEF_MOD_SHIFT},
    /* 0x46 'F' */ {0x21, KBDCHARDEF_MOD_SHIFT},
    /* 0x47 'G' */ {0x22, KBDCHARDEF_MOD_SHIFT},
    /* 0x48 'H' */ {0x23, KBDCHARDEF_MOD_SHIFT},
    /* 0x49 'I' */ {0x17, KBDCHARDEF_MOD_SHIFT},
    /* 0x4A 'J' */ {0x24, KBDCHARDEF_MOD_SHIFT},
    /* 0x4B 'K' */ {0x25, KBDCHARDEF_MOD_SHIFT},
    /* 0x4C 'L' */ {0x26, KBDCHARDEF_MOD_SHIFT},
    /* 0x4D 'M' */ {0x32, KBDCHARDEF_MOD_SHIFT},
    /* 0x4E 'N' */ {0x31, KBDCHARDEF_MOD_SHIFT},
    /* 0x4F 'O' */ {0x18, KBDCHARDEF_MOD_SHIFT},
    /* 0x50 'P' */ {0x19, KBDCHARDEF_MOD_SHIFT},
    /* 0x51 'Q' */ {0x10, KBDCHARDEF_MOD_SHIFT},
    /* 0x52 'R' */ {0x13, KBDCHARDEF_MOD_SHIFT},
    /* 0x53 'S' */ {0x1f, KBDCHARDEF_MOD_SHIFT},
    /* 0x54 'T' */ {0x14, KBDCHARDEF_MOD_SHIFT},
    /* 0x55 'U' */ {0x16, KBDCHARDEF_MOD_SHIFT},
    /* 0x56 'V' */ {0x2f, KBDCHARDEF_MOD_SHIFT},
    /* 0x57 'W' */ {0x11, KBDCHARDEF_MOD_SHIFT},
    /* 0x58 'X' */ {0x2d, KBDCHARDEF_MOD_SHIFT},
    /* 0x59 'Y' */ {0x15, KBDCHARDEF_MOD_SHIFT},
    /* 0x5A 'Z' */ {0x2c, KBDCHARDEF_MOD_SHIFT},
    /* 0x5B '[' */ {0x1a, KBDCHARDEF_MOD_NONE},
    /* 0x5C '\' */ {0x2b, KBDCHARDEF_MOD_NONE},
    /* 0x5D ']' */ {0x1b, KBDCHARDEF_MOD_NONE},
    /* 0x5E '^' */ {0x07, KBDCHARDEF_MOD_SHIFT},
    /* 0x5F '_' */ {0x0c, KBDCHARDEF_MOD_SHIFT},
    /* 0x60 '`' */ {0x28, KBDCHARDEF_MOD_NONE},
    /* 0x61 'a' */ {0x1e, KBDCHARDEF_MOD_NONE},
    /* 0x62 'b' */ {0x30, KBDCHARDEF_MOD_NONE},
    /* 0x63 'c' */ {0x2e, KBDCHARDEF_MOD_NONE},
    /* 0x64 'd' */ {0x20, KBDCHARDEF_MOD_NONE},
    /* 0x65 'e' */ {0x12, KBDCHARDEF_MOD_NONE},
    /* 0x66 'f' */ {0x21, KBDCHARDEF_MOD_NONE},
    /* 0x67 'g' */ {0x22, KBDCHARDEF_MOD_NONE},
    /* 0x68 'h' */ {0x23, KBDCHARDEF_MOD_NONE},
    /* 0x69 'i' */ {0x17, KBDCHARDEF_MOD_NONE},
    /* 0x6A 'j' */ {0x24, KBDCHARDEF_MOD_NONE},
    /* 0x6B 'k' */ {0x25, KBDCHARDEF_MOD_NONE},
    /* 0x6C 'l' */ {0x26, KBDCHARDEF_MOD_NONE},
    /* 0x6D 'm' */ {0x32, KBDCHARDEF_MOD_NONE},
    /* 0x6E 'n' */ {0x31, KBDCHARDEF_MOD_NONE},
    /* 0x6F 'o' */ {0x18, KBDCHARDEF_MOD_NONE},
    /* 0x70 'p' */ {0x19, KBDCHARDEF_MOD_NONE},
    /* 0x71 'q' */ {0x10, KBDCHARDEF_MOD_NONE},
    /* 0x72 'r' */ {0x13, KBDCHARDEF_MOD_NONE},
    /* 0x73 's' */ {0x1f, KBDCHARDEF_MOD_NONE},
    /* 0x74 't' */ {0x14, KBDCHARDEF_MOD_NONE},
    /* 0x75 'u' */ {0x16, KBDCHARDEF_MOD_NONE},
    /* 0x76 'v' */ {0x2f, KBDCHARDEF_MOD_NONE},
    /* 0x77 'w' */ {0x11, KBDCHARDEF_MOD_NONE},
    /* 0x78 'x' */ {0x2d, KBDCHARDEF_MOD_NONE},
    /* 0x79 'y' */ {0x15, KBDCHARDEF_MOD_NONE},
    /* 0x7A 'z' */ {0x2c, KBDCHARDEF_MOD_NONE},
    /* 0x7B '{' */ {0x1a, KBDCHARDEF_MOD_SHIFT},
    /* 0x7C '|' */ {0x2b, KBDCHARDEF_MOD_SHIFT},
    /* 0x7D '}' */ {0x1b, KBDCHARDEF_MOD_SHIFT},
    /* 0x7E '~' */ {0x29, KBDCHARDEF_MOD_SHIFT},
    /* 0x7F ' ' */ {0x00, KBDCHARDEF_MOD_NONE},
};

static HRESULT keyboardPutScancodes(IKeyboard *pKeyboard, const std::list<LONG> &llScancodes)
{
    /* Send scancodes to the VM. */
    com::SafeArray<LONG> saScancodes(llScancodes);

    HRESULT hrc = S_OK;
    size_t i;
    for (i = 0; i < saScancodes.size(); ++i)
    {
        hrc = pKeyboard->PutScancode(saScancodes[i]);
        if (FAILED(hrc))
        {
            RTMsgError(ControlVM::tr("Failed to send a scancode."));
            break;
        }

        RTThreadSleep(10); /* "Typing" too fast causes lost characters. */
    }

    return hrc;
}

static void keyboardCharsToScancodes(const char *pch, size_t cchMax, std::list<LONG> &llScancodes, bool *pfShift)
{
    size_t cchProcessed = 0;
    const char *p = pch;
    while (cchProcessed < cchMax)
    {
        ++cchProcessed;
        const uint8_t c = (uint8_t)*p++;
        if (c < RT_ELEMENTS(g_aASCIIChars))
        {
            const KBDCHARDEF *d = &g_aASCIIChars[c];
            if (d->u8Scancode)
            {
                const bool fNeedShift = RT_BOOL(d->u8Modifiers & KBDCHARDEF_MOD_SHIFT);
                if (*pfShift != fNeedShift)
                {
                    *pfShift = fNeedShift;
                    /* Press or release the SHIFT key. */
                    llScancodes.push_back(0x2a | (fNeedShift? 0x00: 0x80));
                }

                llScancodes.push_back(d->u8Scancode);
                llScancodes.push_back(d->u8Scancode | 0x80);
            }
        }
    }
}

static HRESULT keyboardPutString(IKeyboard *pKeyboard, int argc, char **argv)
{
    std::list<LONG> llScancodes;
    bool fShift = false;

    /* Convert command line string(s) to the en-us keyboard scancodes. */
    int i;
    for (i = 1 + 1; i < argc; ++i)
    {
        if (!llScancodes.empty())
        {
            /* Insert a SPACE before the next string. */
            llScancodes.push_back(0x39);
            llScancodes.push_back(0x39 | 0x80);
        }

        keyboardCharsToScancodes(argv[i], strlen(argv[i]), llScancodes, &fShift);
    }

    /* Release SHIFT if pressed. */
    if (fShift)
        llScancodes.push_back(0x2a | 0x80);

    return keyboardPutScancodes(pKeyboard, llScancodes);
}

static HRESULT keyboardPutFile(IKeyboard *pKeyboard, const char *pszFilename)
{
    std::list<LONG> llScancodes;
    bool fShift = false;

    RTFILE File = NIL_RTFILE;
    int vrc = RTFileOpen(&File, pszFilename, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
    if (RT_SUCCESS(vrc))
    {
        uint64_t cbFile = 0;
        vrc = RTFileQuerySize(File, &cbFile);
        if (RT_SUCCESS(vrc))
        {
            const uint64_t cbFileMax = _64K;
            if (cbFile <= cbFileMax)
            {
                const size_t cbBuffer = _4K;
                char *pchBuf = (char *)RTMemAlloc(cbBuffer);
                if (pchBuf)
                {
                    size_t cbRemaining = (size_t)cbFile;
                    while (cbRemaining > 0)
                    {
                        const size_t cbToRead = cbRemaining > cbBuffer ? cbBuffer : cbRemaining;

                        size_t cbRead = 0;
                        vrc = RTFileRead(File, pchBuf, cbToRead, &cbRead);
                        if (RT_FAILURE(vrc) || cbRead == 0)
                            break;

                        keyboardCharsToScancodes(pchBuf, cbRead, llScancodes, &fShift);
                        cbRemaining -= cbRead;
                    }

                    RTMemFree(pchBuf);
                }
                else
                    RTMsgError(ControlVM::tr("Out of memory allocating %d bytes.", "", cbBuffer), cbBuffer);
            }
            else
                RTMsgError(ControlVM::tr("File size %RI64 is greater than %RI64: '%s'."), cbFile, cbFileMax, pszFilename);
        }
        else
            RTMsgError(ControlVM::tr("Cannot get size of file '%s': %Rrc."), pszFilename, vrc);

        RTFileClose(File);
    }
    else
        RTMsgError(ControlVM::tr("Cannot open file '%s': %Rrc."), pszFilename, vrc);

    /* Release SHIFT if pressed. */
    if (fShift)
        llScancodes.push_back(0x2a | 0x80);

    return keyboardPutScancodes(pKeyboard, llScancodes);
}


RTEXITCODE handleControlVM(HandlerArg *a)
{
    using namespace com;
    bool fNeedsSaving = false;
    HRESULT hrc;

    if (a->argc < 2)
        return errorSyntax(ControlVM::tr("Not enough parameters."));

    /* try to find the given machine */
    ComPtr<IMachine> machine;
    CHECK_ERROR(a->virtualBox, FindMachine(Bstr(a->argv[0]).raw(),
                                           machine.asOutParam()));
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    /* open a session for the VM */
    CHECK_ERROR_RET(machine, LockMachine(a->session, LockType_Shared), RTEXITCODE_FAILURE);

    ComPtr<IConsole> console;
    ComPtr<IMachine> sessionMachine;

    do
    {
        /* get the associated console */
        CHECK_ERROR_BREAK(a->session, COMGETTER(Console)(console.asOutParam()));
        if (!console)
            return RTMsgErrorExit(RTEXITCODE_FAILURE, ControlVM::tr("Machine '%s' is not currently running."), a->argv[0]);

        /* ... and session machine */
        CHECK_ERROR_BREAK(a->session, COMGETTER(Machine)(sessionMachine.asOutParam()));

        /* which command? */
        if (!strcmp(a->argv[1], "pause"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_PAUSE);
            CHECK_ERROR_BREAK(console, Pause());
        }
        else if (!strcmp(a->argv[1], "resume"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_RESUME);
            CHECK_ERROR_BREAK(console, Resume());
        }
        else if (!strcmp(a->argv[1], "reset"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_RESET);
            CHECK_ERROR_BREAK(console, Reset());
        }
        else if (!strcmp(a->argv[1], "unplugcpu"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_UNPLUGCPU);
            if (a->argc <= 1 + 1)
            {
                errorSyntax(ControlVM::tr("Missing argument to '%s'."), a->argv[1]);
                hrc = E_FAIL;
                break;
            }

            unsigned n = parseNum(a->argv[2], 32, "CPU");

            CHECK_ERROR_BREAK(sessionMachine, HotUnplugCPU(n));
        }
        else if (!strcmp(a->argv[1], "plugcpu"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_PLUGCPU);
            if (a->argc <= 1 + 1)
            {
                errorSyntax(ControlVM::tr("Missing argument to '%s'."), a->argv[1]);
                hrc = E_FAIL;
                break;
            }

            unsigned n = parseNum(a->argv[2], 32, "CPU");

            CHECK_ERROR_BREAK(sessionMachine, HotPlugCPU(n));
        }
        else if (!strcmp(a->argv[1], "cpuexecutioncap"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_CPUEXECUTIONCAP);
            if (a->argc <= 1 + 1)
            {
                errorSyntax(ControlVM::tr("Missing argument to '%s'."), a->argv[1]);
                hrc = E_FAIL;
                break;
            }

            unsigned n = parseNum(a->argv[2], 100, "ExecutionCap");

            CHECK_ERROR_BREAK(sessionMachine, COMSETTER(CPUExecutionCap)(n));
        }
        else if (!strcmp(a->argv[1], "audioin"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_AUDIOIN);

            ComPtr<IAudioSettings> audioSettings;
            CHECK_ERROR_BREAK(sessionMachine, COMGETTER(AudioSettings)(audioSettings.asOutParam()));
            ComPtr<IAudioAdapter> adapter;
            CHECK_ERROR_BREAK(audioSettings, COMGETTER(Adapter)(adapter.asOutParam()));
            if (adapter)
            {
                bool fEnabled;
                if (RT_FAILURE(parseBool(a->argv[2], &fEnabled)))
                {
                    errorSyntax(ControlVM::tr("Invalid value '%s'."), a->argv[2]);
                    hrc = E_FAIL;
                    break;
                }
                CHECK_ERROR_RET(adapter, COMSETTER(EnabledIn)(fEnabled), RTEXITCODE_FAILURE);
                fNeedsSaving = true;
            }
            else
            {
                errorSyntax(ControlVM::tr("Audio adapter not enabled in VM configuration."));
                hrc = E_FAIL;
                break;
            }
        }
        else if (!strcmp(a->argv[1], "audioout"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_AUDIOOUT);

            ComPtr<IAudioSettings> audioSettings;
            CHECK_ERROR_BREAK(sessionMachine, COMGETTER(AudioSettings)(audioSettings.asOutParam()));
            ComPtr<IAudioAdapter> adapter;
            CHECK_ERROR_BREAK(audioSettings, COMGETTER(Adapter)(adapter.asOutParam()));
            if (adapter)
            {
                bool fEnabled;
                if (RT_FAILURE(parseBool(a->argv[2], &fEnabled)))
                {
                    errorSyntax(ControlVM::tr("Invalid value '%s'."), a->argv[2]);
                    hrc = E_FAIL;
                    break;
                }
                CHECK_ERROR_RET(adapter, COMSETTER(EnabledOut)(fEnabled), RTEXITCODE_FAILURE);
                fNeedsSaving = true;
            }
            else
            {
                errorSyntax(ControlVM::tr("Audio adapter not enabled in VM configuration."));
                hrc = E_FAIL;
                break;
            }
        }
#ifdef VBOX_WITH_SHARED_CLIPBOARD
        else if (!strcmp(a->argv[1], "clipboard"))
        {
            if (a->argc <= 1 + 1)
            {
                errorArgument(ControlVM::tr("Missing argument to '%s'."), a->argv[1]);
                hrc = E_FAIL;
                break;
            }

            ClipboardMode_T mode = ClipboardMode_Disabled; /* Shut up MSC */
            if (!strcmp(a->argv[2], "mode"))
            {
                setCurrentSubcommand(HELP_SCOPE_CONTROLVM_CLIPBOARD_MODE);
                if (a->argc <= 1 + 2)
                {
                    errorSyntax(ControlVM::tr("Missing argument to '%s %s'."), a->argv[1], a->argv[2]);
                    hrc = E_FAIL;
                    break;
                }

                if (!strcmp(a->argv[3], "disabled"))
                    mode = ClipboardMode_Disabled;
                else if (!strcmp(a->argv[3], "hosttoguest"))
                    mode = ClipboardMode_HostToGuest;
                else if (!strcmp(a->argv[3], "guesttohost"))
                    mode = ClipboardMode_GuestToHost;
                else if (!strcmp(a->argv[3], "bidirectional"))
                    mode = ClipboardMode_Bidirectional;
                else
                {
                    errorSyntax(ControlVM::tr("Invalid '%s %s' argument '%s'."), a->argv[1], a->argv[2], a->argv[3]);
                    hrc = E_FAIL;
                    break;
                }

                CHECK_ERROR_BREAK(sessionMachine, COMSETTER(ClipboardMode)(mode));
                if (SUCCEEDED(hrc))
                    fNeedsSaving = true;
            }
# ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
            else if (!strcmp(a->argv[2], "filetransfers"))
            {
                setCurrentSubcommand(HELP_SCOPE_CONTROLVM_CLIPBOARD_FILETRANSFERS);
                if (a->argc <= 1 + 2)
                {
                    errorSyntax(ControlVM::tr("Missing argument to '%s %s'."), a->argv[1], a->argv[2]);
                    hrc = E_FAIL;
                    break;
                }

                bool fEnabled;
                if (RT_FAILURE(parseBool(a->argv[3], &fEnabled)))
                {
                    errorSyntax(ControlVM::tr("Invalid '%s %s' argument '%s'."), a->argv[1], a->argv[2], a->argv[3]);
                    hrc = E_FAIL;
                    break;
                }

                CHECK_ERROR_BREAK(sessionMachine, COMSETTER(ClipboardFileTransfersEnabled)(fEnabled));
                fNeedsSaving = true;
            }
# endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
            else
            {
                errorArgument(ControlVM::tr("Invalid '%s' argument '%s'."), a->argv[1], a->argv[2]);
                hrc = E_FAIL;
                break;
            }
        }
#endif /* VBOX_WITH_SHARED_CLIPBOARD */
        else if (!strcmp(a->argv[1], "draganddrop"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_DRAGANDDROP);
            if (a->argc <= 1 + 1)
            {
                errorSyntax(ControlVM::tr("Missing argument to '%s'."), a->argv[1]);
                hrc = E_FAIL;
                break;
            }

            DnDMode_T mode = DnDMode_Disabled; /* Shup up MSC. */
            if (!strcmp(a->argv[2], "disabled"))
                mode = DnDMode_Disabled;
            else if (!strcmp(a->argv[2], "hosttoguest"))
                mode = DnDMode_HostToGuest;
            else if (!strcmp(a->argv[2], "guesttohost"))
                mode = DnDMode_GuestToHost;
            else if (!strcmp(a->argv[2], "bidirectional"))
                mode = DnDMode_Bidirectional;
            else
            {
                errorSyntax(ControlVM::tr("Invalid '%s' argument '%s'."), a->argv[1], a->argv[2]);
                hrc = E_FAIL;
            }
            if (SUCCEEDED(hrc))
            {
                CHECK_ERROR_BREAK(sessionMachine, COMSETTER(DnDMode)(mode));
                if (SUCCEEDED(hrc))
                    fNeedsSaving = true;
            }
        }
        else if (!strcmp(a->argv[1], "poweroff"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_POWEROFF);
            ComPtr<IProgress> progress;
            CHECK_ERROR_BREAK(console, PowerDown(progress.asOutParam()));

            hrc = showProgress(progress);
            CHECK_PROGRESS_ERROR(progress, (ControlVM::tr("Failed to power off machine.")));
        }
        else if (!strcmp(a->argv[1], "savestate"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_SAVESTATE);
            /* first pause so we don't trigger a live save which needs more time/resources */
            bool fPaused = false;
            hrc = console->Pause();
            if (FAILED(hrc))
            {
                bool fError = true;
                if (hrc == VBOX_E_INVALID_VM_STATE)
                {
                    /* check if we are already paused */
                    MachineState_T machineState;
                    CHECK_ERROR_BREAK(console, COMGETTER(State)(&machineState));
                    /* the error code was lost by the previous instruction */
                    hrc = VBOX_E_INVALID_VM_STATE;
                    if (machineState != MachineState_Paused)
                    {
                        RTMsgError(ControlVM::tr("Machine in invalid state %d -- %s."),
                                   machineState, machineStateToName(machineState, false));
                    }
                    else
                    {
                        fError = false;
                        fPaused = true;
                    }
                }
                if (fError)
                    break;
            }

            ComPtr<IProgress> progress;
            CHECK_ERROR(sessionMachine, SaveState(progress.asOutParam()));
            if (FAILED(hrc))
            {
                if (!fPaused)
                    console->Resume();
                break;
            }

            hrc = showProgress(progress);
            CHECK_PROGRESS_ERROR(progress, (ControlVM::tr("Failed to save machine state.")));
            if (FAILED(hrc))
            {
                if (!fPaused)
                    console->Resume();
            }
        }
        else if (!strcmp(a->argv[1], "acpipowerbutton"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_ACPIPOWERBUTTON);
            CHECK_ERROR_BREAK(console, PowerButton());
        }
        else if (!strcmp(a->argv[1], "acpisleepbutton"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_ACPISLEEPBUTTON);
            CHECK_ERROR_BREAK(console, SleepButton());
        }
#ifdef VBOX_WITH_GUEST_CONTROL
        else if (   !strcmp(a->argv[1], "reboot")
                 || !strcmp(a->argv[1], "shutdown")) /* With shutdown we mean gracefully powering off the VM by letting the guest OS do its thing. */
        {
            const bool fReboot = !strcmp(a->argv[1], "reboot");
            if (fReboot)
                setCurrentSubcommand(HELP_SCOPE_CONTROLVM_REBOOT);
            else
                setCurrentSubcommand(HELP_SCOPE_CONTROLVM_SHUTDOWN);

            ComPtr<IGuest> pGuest;
            CHECK_ERROR_BREAK(console, COMGETTER(Guest)(pGuest.asOutParam()));
            if (!pGuest)
            {
                RTMsgError(ControlVM::tr("Guest not running."));
                hrc = E_FAIL;
                break;
            }

            com::SafeArray<GuestShutdownFlag_T> aShutdownFlags;
            if (fReboot)
                aShutdownFlags.push_back(GuestShutdownFlag_Reboot);
            else
                aShutdownFlags.push_back(GuestShutdownFlag_PowerOff);

            if (   a->argc >= 3
                && !strcmp(a->argv[2], "--force"))
                aShutdownFlags.push_back(GuestShutdownFlag_Force);

            CHECK_ERROR(pGuest, Shutdown(ComSafeArrayAsInParam(aShutdownFlags)));
            if (FAILED(hrc))
            {
                if (hrc == VBOX_E_NOT_SUPPORTED)
                {
                    if (fReboot)
                        RTMsgError(ControlVM::tr("Current installed Guest Additions don't support rebooting the guest."));
                    else
                        RTMsgError(ControlVM::tr("Current installed Guest Additions don't support shutting down the guest."));
                }
            }
        }
#endif
        else if (!strcmp(a->argv[1], "keyboardputscancode"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_KEYBOARDPUTSCANCODE);
            ComPtr<IKeyboard> pKeyboard;
            CHECK_ERROR_BREAK(console, COMGETTER(Keyboard)(pKeyboard.asOutParam()));
            if (!pKeyboard)
            {
                RTMsgError(ControlVM::tr("Guest not running."));
                hrc = E_FAIL;
                break;
            }

            if (a->argc <= 1 + 1)
            {
                errorSyntax(ControlVM::tr("Missing argument to '%s'. Expected IBM PC AT set 2 keyboard scancode(s)."),
                              a->argv[1]);
                hrc = E_FAIL;
                break;
            }

            std::list<LONG> llScancodes;

            /* Process the command line. */
            int i;
            for (i = 1 + 1; i < a->argc; i++)
            {
                if (   RT_C_IS_XDIGIT (a->argv[i][0])
                    && RT_C_IS_XDIGIT (a->argv[i][1])
                    && a->argv[i][2] == 0)
                {
                    uint8_t u8Scancode;
                    int vrc = RTStrToUInt8Ex(a->argv[i], NULL, 16, &u8Scancode);
                    if (RT_FAILURE (vrc))
                    {
                        RTMsgError(ControlVM::tr("Converting '%s' returned %Rrc!"), a->argv[i], vrc);
                        hrc = E_FAIL;
                        break;
                    }

                    llScancodes.push_back(u8Scancode);
                }
                else
                {
                    RTMsgError(ControlVM::tr("'%s' is not a hex byte!"), a->argv[i]);
                    hrc = E_FAIL;
                    break;
                }
            }

            if (FAILED(hrc))
                break;

            hrc = keyboardPutScancodes(pKeyboard, llScancodes);
        }
        else if (!strcmp(a->argv[1], "keyboardputstring"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_KEYBOARDPUTSTRING);
            ComPtr<IKeyboard> pKeyboard;
            CHECK_ERROR_BREAK(console, COMGETTER(Keyboard)(pKeyboard.asOutParam()));
            if (!pKeyboard)
            {
                RTMsgError(ControlVM::tr("Guest not running."));
                hrc = E_FAIL;
                break;
            }

            if (a->argc <= 1 + 1)
            {
                errorSyntax(ControlVM::tr("Missing argument to '%s'. Expected ASCII string(s)."), a->argv[1]);
                hrc = E_FAIL;
                break;
            }

            hrc = keyboardPutString(pKeyboard, a->argc, a->argv);
        }
        else if (!strcmp(a->argv[1], "keyboardputfile"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_KEYBOARDPUTFILE);
            ComPtr<IKeyboard> pKeyboard;
            CHECK_ERROR_BREAK(console, COMGETTER(Keyboard)(pKeyboard.asOutParam()));
            if (!pKeyboard)
            {
                RTMsgError(ControlVM::tr("Guest not running."));
                hrc = E_FAIL;
                break;
            }

            if (a->argc <= 1 + 1)
            {
                errorSyntax(ControlVM::tr("Missing argument to '%s'."), a->argv[1]);
                hrc = E_FAIL;
                break;
            }

            hrc = keyboardPutFile(pKeyboard, a->argv[2]);
        }
        else if (!strncmp(a->argv[1], "setlinkstate", 12))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_SETLINKSTATE);
            /* Get the number of network adapters */
            ULONG NetworkAdapterCount = getMaxNics(sessionMachine);
            unsigned n = parseNum(&a->argv[1][12], NetworkAdapterCount, "NIC");
            if (!n)
            {
                hrc = E_FAIL;
                break;
            }
            if (a->argc <= 1 + 1)
            {
                errorSyntax(ControlVM::tr("Missing argument to '%s'."), a->argv[1]);
                hrc = E_FAIL;
                break;
            }
            /* get the corresponding network adapter */
            ComPtr<INetworkAdapter> adapter;
            CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(n - 1, adapter.asOutParam()));
            if (adapter)
            {
                bool fEnabled;
                if (RT_FAILURE(parseBool(a->argv[2], &fEnabled)))
                {
                    errorSyntax(ControlVM::tr("Invalid link state '%s'."), a->argv[2]);
                    hrc = E_FAIL;
                    break;
                }
                CHECK_ERROR_BREAK(adapter, COMSETTER(CableConnected)(fEnabled));
                fNeedsSaving = true;
            }
        }
        /* here the order in which strncmp is called is important
         * cause nictracefile can be very well compared with
         * nictrace and nic and thus everything will always fail
         * if the order is changed
         */
        else if (!strncmp(a->argv[1], "nictracefile", 12))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_NICTRACEFILE);
            /* Get the number of network adapters */
            ULONG NetworkAdapterCount = getMaxNics(sessionMachine);
            unsigned n = parseNum(&a->argv[1][12], NetworkAdapterCount, "NIC");
            if (!n)
            {
                hrc = E_FAIL;
                break;
            }
            if (a->argc <= 2)
            {
                errorSyntax(ControlVM::tr("Missing argument to '%s'."), a->argv[1]);
                hrc = E_FAIL;
                break;
            }

            /* get the corresponding network adapter */
            ComPtr<INetworkAdapter> adapter;
            CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(n - 1, adapter.asOutParam()));
            if (adapter)
            {
                BOOL fEnabled;
                adapter->COMGETTER(Enabled)(&fEnabled);
                if (fEnabled)
                {
                    if (a->argv[2])
                    {
                        CHECK_ERROR_RET(adapter, COMSETTER(TraceFile)(Bstr(a->argv[2]).raw()), RTEXITCODE_FAILURE);
                    }
                    else
                    {
                        errorSyntax(ControlVM::tr("Filename not specified for NIC %lu."), n);
                        hrc = E_FAIL;
                        break;
                    }
                    if (SUCCEEDED(hrc))
                        fNeedsSaving = true;
                }
                else
                    RTMsgError(ControlVM::tr("The NIC %d is currently disabled and thus its tracefile can't be changed."), n);
            }
        }
        else if (!strncmp(a->argv[1], "nictrace", 8))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_NICTRACE);
            /* Get the number of network adapters */
            ULONG NetworkAdapterCount = getMaxNics(sessionMachine);
            unsigned n = parseNum(&a->argv[1][8], NetworkAdapterCount, "NIC");
            if (!n)
            {
                hrc = E_FAIL;
                break;
            }
            if (a->argc <= 2)
            {
                errorSyntax(ControlVM::tr("Missing argument to '%s'."), a->argv[1]);
                hrc = E_FAIL;
                break;
            }

            /* get the corresponding network adapter */
            ComPtr<INetworkAdapter> adapter;
            CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(n - 1, adapter.asOutParam()));
            if (adapter)
            {
                BOOL fEnabled;
                adapter->COMGETTER(Enabled)(&fEnabled);
                if (fEnabled)
                {
                    bool fTraceEnabled;
                    if (RT_FAILURE(parseBool(a->argv[2], &fTraceEnabled)))
                    {
                        errorSyntax(ControlVM::tr("Invalid nictrace%lu argument '%s'."), n, a->argv[2]);
                        hrc = E_FAIL;
                        break;
                    }
                    CHECK_ERROR_RET(adapter, COMSETTER(TraceEnabled)(fTraceEnabled), RTEXITCODE_FAILURE);
                    fNeedsSaving = true;
                }
                else
                    RTMsgError(ControlVM::tr("The NIC %d is currently disabled and thus its trace flag can't be changed."), n);
            }
        }
        else if(   a->argc > 2
                && !strncmp(a->argv[1], "natpf", 5))
        {
            /* Get the number of network adapters */
            ULONG NetworkAdapterCount = getMaxNics(sessionMachine);
            unsigned n = parseNum(&a->argv[1][5], NetworkAdapterCount, "NIC");
            if (!n)
            {
                hrc = E_FAIL;
                break;
            }
            if (a->argc <= 2)
            {
                errorArgument(ControlVM::tr("Missing argument to '%s'."), a->argv[1]);
                hrc = E_FAIL;
                break;
            }

            /* get the corresponding network adapter */
            ComPtr<INetworkAdapter> adapter;
            CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(n - 1, adapter.asOutParam()));
            if (!adapter)
            {
                hrc = E_FAIL;
                break;
            }
            ComPtr<INATEngine> engine;
            CHECK_ERROR(adapter, COMGETTER(NATEngine)(engine.asOutParam()));
            if (!engine)
            {
                hrc = E_FAIL;
                break;
            }

            if (!strcmp(a->argv[2], "delete"))
            {
                setCurrentSubcommand(HELP_SCOPE_CONTROLVM_NATPF_DELETE);
                if (a->argc >= 3)
                    CHECK_ERROR(engine, RemoveRedirect(Bstr(a->argv[3]).raw()));
            }
            else
            {
                setCurrentSubcommand(HELP_SCOPE_CONTROLVM_NATPF);
#define ITERATE_TO_NEXT_TERM(ch)                                                        \
    do {                                                                                \
        while (*ch != ',')                                                              \
        {                                                                               \
            if (*ch == 0)                                                               \
            {                                                                           \
                return errorSyntax(ControlVM::tr("Missing or invalid argument to '%s'."), \
                                   a->argv[1]);                                         \
            }                                                                           \
            ch++;                                                                       \
        }                                                                               \
        *ch = '\0';                                                                     \
        ch++;                                                                           \
    } while(0)

                char *strName;
                char *strProto;
                char *strHostIp;
                char *strHostPort;
                char *strGuestIp;
                char *strGuestPort;
                char *strRaw = RTStrDup(a->argv[2]);
                char *ch = strRaw;
                strName = RTStrStrip(ch);
                ITERATE_TO_NEXT_TERM(ch);
                strProto = RTStrStrip(ch);
                ITERATE_TO_NEXT_TERM(ch);
                strHostIp = RTStrStrip(ch);
                ITERATE_TO_NEXT_TERM(ch);
                strHostPort = RTStrStrip(ch);
                ITERATE_TO_NEXT_TERM(ch);
                strGuestIp = RTStrStrip(ch);
                ITERATE_TO_NEXT_TERM(ch);
                strGuestPort = RTStrStrip(ch);
                NATProtocol_T proto;
                if (RTStrICmp(strProto, "udp") == 0)
                    proto = NATProtocol_UDP;
                else if (RTStrICmp(strProto, "tcp") == 0)
                    proto = NATProtocol_TCP;
                else
                {
                    return errorSyntax(ControlVM::tr("Wrong rule proto '%s' specified -- only 'udp' and 'tcp' are allowed."),
                                       strProto);
                }
                CHECK_ERROR(engine, AddRedirect(Bstr(strName).raw(), proto, Bstr(strHostIp).raw(),
                        RTStrToUInt16(strHostPort), Bstr(strGuestIp).raw(), RTStrToUInt16(strGuestPort)));
#undef ITERATE_TO_NEXT_TERM
            }
            if (SUCCEEDED(hrc))
                fNeedsSaving = true;
        }
        else if (!strncmp(a->argv[1], "nicproperty", 11))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_NICPROPERTY);
            /* Get the number of network adapters */
            ULONG NetworkAdapterCount = getMaxNics(sessionMachine);
            unsigned n = parseNum(&a->argv[1][11], NetworkAdapterCount, "NIC");
            if (!n)
            {
                hrc = E_FAIL;
                break;
            }
            if (a->argc <= 2)
            {
                errorSyntax(ControlVM::tr("Missing argument to '%s'."), a->argv[1]);
                hrc = E_FAIL;
                break;
            }

            /* get the corresponding network adapter */
            ComPtr<INetworkAdapter> adapter;
            CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(n - 1, adapter.asOutParam()));
            if (adapter)
            {
                BOOL fEnabled;
                adapter->COMGETTER(Enabled)(&fEnabled);
                if (fEnabled)
                {
                    /* Parse 'name=value' */
                    char *pszProperty = RTStrDup(a->argv[2]);
                    if (pszProperty)
                    {
                        char *pDelimiter = strchr(pszProperty, '=');
                        if (pDelimiter)
                        {
                            *pDelimiter = '\0';

                            Bstr bstrName = pszProperty;
                            Bstr bstrValue = &pDelimiter[1];
                            CHECK_ERROR(adapter, SetProperty(bstrName.raw(), bstrValue.raw()));
                            if (SUCCEEDED(hrc))
                                fNeedsSaving = true;
                        }
                        else
                        {
                            errorSyntax(ControlVM::tr("Invalid nicproperty%d argument '%s'."), n, a->argv[2]);
                            hrc = E_FAIL;
                        }
                        RTStrFree(pszProperty);
                    }
                    else
                    {
                        RTMsgError(ControlVM::tr("Failed to allocate memory for nicproperty%d '%s'."),
                                     n, a->argv[2]);
                        hrc = E_FAIL;
                    }
                    if (FAILED(hrc))
                        break;
                }
                else
                    RTMsgError(ControlVM::tr("The NIC %d is currently disabled and thus its properties can't be changed."), n);
            }
        }
        else if (!strncmp(a->argv[1], "nicpromisc", 10))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_NICPROMISC);
            /* Get the number of network adapters */
            ULONG NetworkAdapterCount = getMaxNics(sessionMachine);
            unsigned n = parseNum(&a->argv[1][10], NetworkAdapterCount, "NIC");
            if (!n)
            {
                hrc = E_FAIL;
                break;
            }
            if (a->argc <= 2)
            {
                errorSyntax(ControlVM::tr("Missing argument to '%s'."), a->argv[1]);
                hrc = E_FAIL;
                break;
            }

            /* get the corresponding network adapter */
            ComPtr<INetworkAdapter> adapter;
            CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(n - 1, adapter.asOutParam()));
            if (adapter)
            {
                BOOL fEnabled;
                adapter->COMGETTER(Enabled)(&fEnabled);
                if (fEnabled)
                {
                    NetworkAdapterPromiscModePolicy_T enmPromiscModePolicy;
                    if (!strcmp(a->argv[2], "deny"))
                        enmPromiscModePolicy = NetworkAdapterPromiscModePolicy_Deny;
                    else if (  !strcmp(a->argv[2], "allow-vms")
                            || !strcmp(a->argv[2], "allow-network"))
                        enmPromiscModePolicy = NetworkAdapterPromiscModePolicy_AllowNetwork;
                    else if (!strcmp(a->argv[2], "allow-all"))
                        enmPromiscModePolicy = NetworkAdapterPromiscModePolicy_AllowAll;
                    else
                    {
                        errorSyntax(ControlVM::tr("Unknown promiscuous mode policy '%s'."), a->argv[2]);
                        hrc = E_INVALIDARG;
                        break;
                    }

                    CHECK_ERROR(adapter, COMSETTER(PromiscModePolicy)(enmPromiscModePolicy));
                    if (SUCCEEDED(hrc))
                        fNeedsSaving = true;
                }
                else
                    RTMsgError(ControlVM::tr("The NIC %d is currently disabled and thus its promiscuous mode can't be changed."), n);
            }
        }
        else if (!strncmp(a->argv[1], "nic", 3))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_NIC);
            /* Get the number of network adapters */
            ULONG NetworkAdapterCount = getMaxNics(sessionMachine);
            unsigned n = parseNum(&a->argv[1][3], NetworkAdapterCount, "NIC");
            if (!n)
            {
                hrc = E_FAIL;
                break;
            }
            if (a->argc <= 2)
            {
                errorSyntax(ControlVM::tr("Missing argument to '%s'."), a->argv[1]);
                hrc = E_FAIL;
                break;
            }

            /* get the corresponding network adapter */
            ComPtr<INetworkAdapter> adapter;
            CHECK_ERROR_BREAK(sessionMachine, GetNetworkAdapter(n - 1, adapter.asOutParam()));
            if (adapter)
            {
                BOOL fEnabled;
                adapter->COMGETTER(Enabled)(&fEnabled);
                if (fEnabled)
                {
                    if (!strcmp(a->argv[2], "null"))
                    {
                        CHECK_ERROR_RET(adapter, COMSETTER(AttachmentType)(NetworkAttachmentType_Null), RTEXITCODE_FAILURE);
                    }
                    else if (!strcmp(a->argv[2], "nat"))
                    {
                        if (a->argc == 4)
                            CHECK_ERROR_RET(adapter, COMSETTER(NATNetwork)(Bstr(a->argv[3]).raw()), RTEXITCODE_FAILURE);
                        CHECK_ERROR_RET(adapter, COMSETTER(AttachmentType)(NetworkAttachmentType_NAT), RTEXITCODE_FAILURE);
                    }
                    else if (  !strcmp(a->argv[2], "bridged")
                            || !strcmp(a->argv[2], "hostif")) /* backward compatibility */
                    {
                        if (a->argc <= 3)
                        {
                            errorSyntax(ControlVM::tr("Missing argument to '%s'."), a->argv[2]);
                            hrc = E_FAIL;
                            break;
                        }
                        CHECK_ERROR_RET(adapter, COMSETTER(BridgedInterface)(Bstr(a->argv[3]).raw()), RTEXITCODE_FAILURE);
                        verifyHostNetworkInterfaceName(a->virtualBox, a->argv[3], HostNetworkInterfaceType_Bridged);
                        CHECK_ERROR_RET(adapter, COMSETTER(AttachmentType)(NetworkAttachmentType_Bridged), RTEXITCODE_FAILURE);
                    }
                    else if (!strcmp(a->argv[2], "intnet"))
                    {
                        if (a->argc <= 3)
                        {
                            errorSyntax(ControlVM::tr("Missing argument to '%s'."), a->argv[2]);
                            hrc = E_FAIL;
                            break;
                        }
                        CHECK_ERROR_RET(adapter, COMSETTER(InternalNetwork)(Bstr(a->argv[3]).raw()), RTEXITCODE_FAILURE);
                        CHECK_ERROR_RET(adapter, COMSETTER(AttachmentType)(NetworkAttachmentType_Internal), RTEXITCODE_FAILURE);
                    }
#if defined(VBOX_WITH_NETFLT)
                    else if (!strcmp(a->argv[2], "hostonly"))
                    {
                        if (a->argc <= 3)
                        {
                            errorSyntax(ControlVM::tr("Missing argument to '%s'."), a->argv[2]);
                            hrc = E_FAIL;
                            break;
                        }
                        CHECK_ERROR_RET(adapter, COMSETTER(HostOnlyInterface)(Bstr(a->argv[3]).raw()), RTEXITCODE_FAILURE);
                        verifyHostNetworkInterfaceName(a->virtualBox, a->argv[3], HostNetworkInterfaceType_HostOnly);
                        CHECK_ERROR_RET(adapter, COMSETTER(AttachmentType)(NetworkAttachmentType_HostOnly), RTEXITCODE_FAILURE);
                    }
#endif
                    else if (!strcmp(a->argv[2], "generic"))
                    {
                        if (a->argc <= 3)
                        {
                            errorSyntax(ControlVM::tr("Missing argument to '%s'."), a->argv[2]);
                            hrc = E_FAIL;
                            break;
                        }
                        CHECK_ERROR_RET(adapter, COMSETTER(GenericDriver)(Bstr(a->argv[3]).raw()), RTEXITCODE_FAILURE);
                        CHECK_ERROR_RET(adapter, COMSETTER(AttachmentType)(NetworkAttachmentType_Generic), RTEXITCODE_FAILURE);
                    }
                    else if (!strcmp(a->argv[2], "natnetwork"))
                    {
                        if (a->argc <= 3)
                        {
                            errorSyntax(ControlVM::tr("Missing argument to '%s'."), a->argv[2]);
                            hrc = E_FAIL;
                            break;
                        }
                        CHECK_ERROR_RET(adapter, COMSETTER(NATNetwork)(Bstr(a->argv[3]).raw()), RTEXITCODE_FAILURE);
                        CHECK_ERROR_RET(adapter, COMSETTER(AttachmentType)(NetworkAttachmentType_NATNetwork), RTEXITCODE_FAILURE);
                    }
                    else
                    {
                        errorSyntax(ControlVM::tr("Invalid type '%s' specfied for NIC %lu."), a->argv[2], n);
                        hrc = E_FAIL;
                        break;
                    }
                    if (SUCCEEDED(hrc))
                        fNeedsSaving = true;
                }
                else
                    RTMsgError(ControlVM::tr("The NIC %d is currently disabled and thus its attachment type can't be changed."), n);
            }
        }
        else if (   !strcmp(a->argv[1], "vrde")
                 || !strcmp(a->argv[1], "vrdp"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_VRDE);
            if (!strcmp(a->argv[1], "vrdp"))
                RTMsgWarning(ControlVM::tr("'vrdp' is deprecated. Use 'vrde'."));

            if (a->argc <= 1 + 1)
            {
                errorSyntax(ControlVM::tr("Missing argument to '%s'."), a->argv[1]);
                hrc = E_FAIL;
                break;
            }
            ComPtr<IVRDEServer> vrdeServer;
            sessionMachine->COMGETTER(VRDEServer)(vrdeServer.asOutParam());
            ASSERT(vrdeServer);
            if (vrdeServer)
            {
                bool fEnabled;
                if (RT_FAILURE(parseBool(a->argv[2], &fEnabled)))
                {
                    errorSyntax(ControlVM::tr("Invalid remote desktop server state '%s'."), a->argv[2]);
                    hrc = E_FAIL;
                    break;
                }
                CHECK_ERROR_BREAK(vrdeServer, COMSETTER(Enabled)(fEnabled));
                fNeedsSaving = true;
            }
        }
        else if (   !strcmp(a->argv[1], "vrdeport")
                 || !strcmp(a->argv[1], "vrdpport"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_VRDEPORT);
            if (!strcmp(a->argv[1], "vrdpport"))
                RTMsgWarning(ControlVM::tr("'vrdpport' is deprecated. Use 'vrdeport'."));

            if (a->argc <= 1 + 1)
            {
                errorSyntax(ControlVM::tr("Missing argument to '%s'."), a->argv[1]);
                hrc = E_FAIL;
                break;
            }

            ComPtr<IVRDEServer> vrdeServer;
            sessionMachine->COMGETTER(VRDEServer)(vrdeServer.asOutParam());
            ASSERT(vrdeServer);
            if (vrdeServer)
            {
                Bstr ports;

                if (!strcmp(a->argv[2], "default"))
                    ports = "0";
                else
                    ports = a->argv[2];

                CHECK_ERROR_BREAK(vrdeServer, SetVRDEProperty(Bstr("TCP/Ports").raw(), ports.raw()));
                if (SUCCEEDED(hrc))
                    fNeedsSaving = true;
            }
        }
        else if (   !strcmp(a->argv[1], "vrdevideochannelquality")
                 || !strcmp(a->argv[1], "vrdpvideochannelquality"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_VRDEVIDEOCHANNELQUALITY);
            if (!strcmp(a->argv[1], "vrdpvideochannelquality"))
                RTMsgWarning(ControlVM::tr("'vrdpvideochannelquality' is deprecated. Use 'vrdevideochannelquality'."));

            if (a->argc <= 1 + 1)
            {
                errorSyntax(ControlVM::tr("Missing argument to '%s'."), a->argv[1]);
                hrc = E_FAIL;
                break;
            }
            ComPtr<IVRDEServer> vrdeServer;
            sessionMachine->COMGETTER(VRDEServer)(vrdeServer.asOutParam());
            ASSERT(vrdeServer);
            if (vrdeServer)
            {
                Bstr value = a->argv[2];

                CHECK_ERROR(vrdeServer, SetVRDEProperty(Bstr("VideoChannel/Quality").raw(), value.raw()));
                if (SUCCEEDED(hrc))
                    fNeedsSaving = true;
            }
        }
        else if (!strcmp(a->argv[1], "vrdeproperty"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_VRDEPROPERTY);
            if (a->argc <= 1 + 1)
            {
                errorSyntax(ControlVM::tr("Missing argument to '%s'."), a->argv[1]);
                hrc = E_FAIL;
                break;
            }
            ComPtr<IVRDEServer> vrdeServer;
            sessionMachine->COMGETTER(VRDEServer)(vrdeServer.asOutParam());
            ASSERT(vrdeServer);
            if (vrdeServer)
            {
                /* Parse 'name=value' */
                char *pszProperty = RTStrDup(a->argv[2]);
                if (pszProperty)
                {
                    char *pDelimiter = strchr(pszProperty, '=');
                    if (pDelimiter)
                    {
                        *pDelimiter = '\0';

                        Bstr bstrName = pszProperty;
                        Bstr bstrValue = &pDelimiter[1];
                        CHECK_ERROR(vrdeServer, SetVRDEProperty(bstrName.raw(), bstrValue.raw()));
                        if (SUCCEEDED(hrc))
                            fNeedsSaving = true;
                    }
                    else
                    {
                        errorSyntax(ControlVM::tr("Invalid vrdeproperty argument '%s'."), a->argv[2]);
                        hrc = E_FAIL;
                    }
                    RTStrFree(pszProperty);
                }
                else
                {
                    RTMsgError(ControlVM::tr("Failed to allocate memory for VRDE property '%s'."),
                                 a->argv[2]);
                    hrc = E_FAIL;
                }
            }
            if (FAILED(hrc))
            {
                break;
            }
        }
        else if (   !strcmp(a->argv[1], "usbattach")
                 || !strcmp(a->argv[1], "usbdetach"))
        {
            bool attach = !strcmp(a->argv[1], "usbattach");
            if (attach)
                setCurrentSubcommand(HELP_SCOPE_CONTROLVM_USBATTACH);
            else
                setCurrentSubcommand(HELP_SCOPE_CONTROLVM_USBDETACH);

            if (a->argc < 3)
            {
                errorSyntax(ControlVM::tr("Not enough parameters."));
                hrc = E_FAIL;
                break;
            }
            else if (a->argc == 4 || a->argc > 5)
            {
                errorSyntax(ControlVM::tr("Wrong number of arguments."));
                hrc = E_FAIL;
                break;
            }

            Bstr usbId = a->argv[2];
            Bstr captureFilename;

            if (a->argc == 5)
            {
                if (!strcmp(a->argv[3], "--capturefile"))
                    captureFilename = a->argv[4];
                else
                {
                    errorSyntax(ControlVM::tr("Invalid parameter '%s'."), a->argv[3]);
                    hrc = E_FAIL;
                    break;
                }
            }

            Guid guid(usbId);
            if (!guid.isValid())
            {
                // assume address
                if (attach)
                {
                    ComPtr<IHost> host;
                    CHECK_ERROR_BREAK(a->virtualBox, COMGETTER(Host)(host.asOutParam()));
                    SafeIfaceArray <IHostUSBDevice> coll;
                    CHECK_ERROR_BREAK(host, COMGETTER(USBDevices)(ComSafeArrayAsOutParam(coll)));
                    ComPtr<IHostUSBDevice> dev;
                    CHECK_ERROR_BREAK(host, FindUSBDeviceByAddress(Bstr(a->argv[2]).raw(),
                                                                   dev.asOutParam()));
                    CHECK_ERROR_BREAK(dev, COMGETTER(Id)(usbId.asOutParam()));
                }
                else
                {
                    SafeIfaceArray <IUSBDevice> coll;
                    CHECK_ERROR_BREAK(console, COMGETTER(USBDevices)(ComSafeArrayAsOutParam(coll)));
                    ComPtr<IUSBDevice> dev;
                    CHECK_ERROR_BREAK(console, FindUSBDeviceByAddress(Bstr(a->argv[2]).raw(),
                                                                      dev.asOutParam()));
                    CHECK_ERROR_BREAK(dev, COMGETTER(Id)(usbId.asOutParam()));
                }
            }
            else if (guid.isZero())
            {
                errorSyntax(ControlVM::tr("Zero UUID argument '%s'."), a->argv[2]);
                hrc = E_FAIL;
                break;
            }

            if (attach)
                CHECK_ERROR_BREAK(console, AttachUSBDevice(usbId.raw(), captureFilename.raw()));
            else
            {
                ComPtr<IUSBDevice> dev;
                CHECK_ERROR_BREAK(console, DetachUSBDevice(usbId.raw(),
                                                           dev.asOutParam()));
            }
        }
        else if (!strcmp(a->argv[1], "setvideomodehint"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_SETVIDEOMODEHINT);
            if (a->argc != 5 && a->argc != 6 && a->argc != 7 && a->argc != 9)
            {
                errorSyntax(ControlVM::tr("Incorrect number of parameters."));
                hrc = E_FAIL;
                break;
            }
            bool fEnabled = true;
            uint32_t uXRes = RTStrToUInt32(a->argv[2]);
            uint32_t uYRes = RTStrToUInt32(a->argv[3]);
            uint32_t uBpp  = RTStrToUInt32(a->argv[4]);
            uint32_t uDisplayIdx = 0;
            bool fChangeOrigin = false;
            int32_t iOriginX = 0;
            int32_t iOriginY = 0;
            if (a->argc >= 6)
                uDisplayIdx = RTStrToUInt32(a->argv[5]);
            if (a->argc >= 7)
            {
                if (RT_FAILURE(parseBool(a->argv[6], &fEnabled)))
                {
                    errorSyntax(ControlVM::tr("Either \"yes\" or \"no\" is expected."));
                    hrc = E_FAIL;
                    break;
                }
            }
            if (a->argc == 9)
            {
                iOriginX = RTStrToInt32(a->argv[7]);
                iOriginY = RTStrToInt32(a->argv[8]);
                fChangeOrigin = true;
            }

            ComPtr<IDisplay> pDisplay;
            CHECK_ERROR_BREAK(console, COMGETTER(Display)(pDisplay.asOutParam()));
            if (!pDisplay)
            {
                RTMsgError(ControlVM::tr("Guest not running."));
                hrc = E_FAIL;
                break;
            }
            CHECK_ERROR_BREAK(pDisplay, SetVideoModeHint(uDisplayIdx, fEnabled,
                                                         fChangeOrigin, iOriginX, iOriginY,
                                                         uXRes, uYRes, uBpp, true));
        }
        else if (!strcmp(a->argv[1], "setscreenlayout"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_SETSCREENLAYOUT);
            if (a->argc < 4)
            {
                errorSyntax(ControlVM::tr("Incorrect number of parameters."));
                hrc = E_FAIL;
                break;
            }

            ComPtr<IDisplay> pDisplay;
            CHECK_ERROR_BREAK(console, COMGETTER(Display)(pDisplay.asOutParam()));
            if (!pDisplay)
            {
                RTMsgError(ControlVM::tr("Guest not running."));
                hrc = E_FAIL;
                break;
            }

            com::SafeIfaceArray<IGuestScreenInfo> aGuestScreenInfos;

            /* Parse "<display> on|primary <xorigin> <yorigin> <xres> <yres> <bpp> | off" sequences. */
            int argc = a->argc - 2;
            char **argv = &a->argv[2];
            while (argc >= 2)
            {
                ULONG aDisplay = RTStrToUInt32(argv[0]);
                BOOL aPrimary = FALSE;

                GuestMonitorStatus_T aStatus;
                if (RTStrICmp(argv[1], "primary") == 0)
                {
                    aStatus = GuestMonitorStatus_Enabled;
                    aPrimary = TRUE;
                }
                else if (RTStrICmp(argv[1], "on") == 0)
                    aStatus = GuestMonitorStatus_Enabled;
                else if (RTStrICmp(argv[1], "off") == 0)
                    aStatus = GuestMonitorStatus_Disabled;
                else
                {
                    errorSyntax(ControlVM::tr("Display status must be <on> or <off>."));
                    hrc = E_FAIL;
                    break;
                }

                BOOL aChangeOrigin = FALSE;
                LONG aOriginX = 0;
                LONG aOriginY = 0;
                ULONG aWidth = 0;
                ULONG aHeight = 0;
                ULONG aBitsPerPixel = 0;
                if (aStatus == GuestMonitorStatus_Enabled)
                {
                    if (argc < 7)
                    {
                        errorSyntax(ControlVM::tr("Incorrect number of parameters."));
                        hrc = E_FAIL;
                        break;
                    }

                    aChangeOrigin = TRUE;
                    aOriginX      = RTStrToUInt32(argv[2]);
                    aOriginY      = RTStrToUInt32(argv[3]);
                    aWidth        = RTStrToUInt32(argv[4]);
                    aHeight       = RTStrToUInt32(argv[5]);
                    aBitsPerPixel = RTStrToUInt32(argv[6]);

                    argc -= 7;
                    argv += 7;
                }
                else
                {
                    argc -= 2;
                    argv += 2;
                }

                ComPtr<IGuestScreenInfo> pInfo;
                CHECK_ERROR_BREAK(pDisplay, CreateGuestScreenInfo(aDisplay, aStatus, aPrimary, aChangeOrigin,
                                                                  aOriginX, aOriginY, aWidth, aHeight, aBitsPerPixel,
                                                                  pInfo.asOutParam()));
                aGuestScreenInfos.push_back(pInfo);
            }

            if (FAILED(hrc))
                break;

            CHECK_ERROR_BREAK(pDisplay, SetScreenLayout(ScreenLayoutMode_Apply, ComSafeArrayAsInParam(aGuestScreenInfos)));
        }
        else if (!strcmp(a->argv[1], "setcredentials"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_SETCREDENTIALS);
            bool fAllowLocalLogon = true;
            if (   a->argc == 7
                || (   a->argc == 8
                    && (   !strcmp(a->argv[3], "-p")
                        || !strcmp(a->argv[3], "--passwordfile"))))
            {
                if (   strcmp(a->argv[5 + (a->argc - 7)], "--allowlocallogon")
                    && strcmp(a->argv[5 + (a->argc - 7)], "-allowlocallogon"))
                {
                    errorSyntax(ControlVM::tr("Invalid parameter '%s'."), a->argv[5]);
                    hrc = E_FAIL;
                    break;
                }
                if (!strcmp(a->argv[6 + (a->argc - 7)], "no"))
                    fAllowLocalLogon = false;
            }
            else if (   a->argc != 5
                     && (   a->argc != 6
                         || (   strcmp(a->argv[3], "-p")
                             && strcmp(a->argv[3], "--passwordfile"))))
            {
                errorSyntax(ControlVM::tr("Incorrect number of parameters."));
                hrc = E_FAIL;
                break;
            }
            Utf8Str passwd, domain;
            if (a->argc == 5 || a->argc == 7)
            {
                passwd = a->argv[3];
                domain = a->argv[4];
            }
            else
            {
                RTEXITCODE rcExit = readPasswordFile(a->argv[4], &passwd);
                if (rcExit != RTEXITCODE_SUCCESS)
                {
                    hrc = E_FAIL;
                    break;
                }
                domain = a->argv[5];
            }

            ComPtr<IGuest> pGuest;
            CHECK_ERROR_BREAK(console, COMGETTER(Guest)(pGuest.asOutParam()));
            if (!pGuest)
            {
                RTMsgError(ControlVM::tr("Guest not running."));
                hrc = E_FAIL;
                break;
            }
            CHECK_ERROR_BREAK(pGuest, SetCredentials(Bstr(a->argv[2]).raw(),
                                                     Bstr(passwd).raw(),
                                                     Bstr(domain).raw(),
                                                     fAllowLocalLogon));
        }
        else if (!strcmp(a->argv[1], "guestmemoryballoon"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_GUESTMEMORYBALLOON);
            if (a->argc != 3)
            {
                errorSyntax(ControlVM::tr("Incorrect number of parameters."));
                hrc = E_FAIL;
                break;
            }
            uint32_t uVal;
            int vrc;
            vrc = RTStrToUInt32Ex(a->argv[2], NULL, 0, &uVal);
            if (vrc != VINF_SUCCESS)
            {
                errorSyntax(ControlVM::tr("Error parsing guest memory balloon size '%s'."), a->argv[2]);
                hrc = E_FAIL;
                break;
            }
            /* guest is running; update IGuest */
            ComPtr<IGuest> pGuest;
            hrc = console->COMGETTER(Guest)(pGuest.asOutParam());
            if (SUCCEEDED(hrc))
            {
                if (!pGuest)
                {
                    RTMsgError(ControlVM::tr("Guest not running."));
                    hrc = E_FAIL;
                    break;
                }
                CHECK_ERROR(pGuest, COMSETTER(MemoryBalloonSize)(uVal));
            }
        }
        else if (!strcmp(a->argv[1], "teleport"))
        {
            Bstr        bstrHostname;
            uint32_t    uMaxDowntime = 250 /*ms*/;
            uint32_t    uPort        = UINT32_MAX;
            uint32_t    cMsTimeout   = 0;
            Utf8Str     strPassword;
            static const RTGETOPTDEF s_aTeleportOptions[] =
            {
                { "--host",              'h', RTGETOPT_REQ_STRING }, /** @todo RTGETOPT_FLAG_MANDATORY */
                { "--maxdowntime",       'd', RTGETOPT_REQ_UINT32 },
                { "--port",              'P', RTGETOPT_REQ_UINT32 }, /** @todo RTGETOPT_FLAG_MANDATORY */
                { "--passwordfile",      'p', RTGETOPT_REQ_STRING },
                { "--password",          'W', RTGETOPT_REQ_STRING },
                { "--timeout",           't', RTGETOPT_REQ_UINT32 },
                { "--detailed-progress", 'D', RTGETOPT_REQ_NOTHING }
            };
            RTGETOPTSTATE GetOptState;
            RTGetOptInit(&GetOptState, a->argc, a->argv, s_aTeleportOptions, RT_ELEMENTS(s_aTeleportOptions), 2, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_TELEPORT);
            int ch;
            RTGETOPTUNION Value;
            while (   SUCCEEDED(hrc)
                   && (ch = RTGetOpt(&GetOptState, &Value)))
            {
                switch (ch)
                {
                    case 'h': bstrHostname  = Value.psz; break;
                    case 'd': uMaxDowntime  = Value.u32; break;
                    case 'D': g_fDetailedProgress = true; break;
                    case 'P': uPort         = Value.u32; break;
                    case 'p':
                    {
                        RTEXITCODE rcExit = readPasswordFile(Value.psz, &strPassword);
                        if (rcExit != RTEXITCODE_SUCCESS)
                            hrc = E_FAIL;
                        break;
                    }
                    case 'W': strPassword   = Value.psz; break;
                    case 't': cMsTimeout    = Value.u32; break;
                    default:
                        errorGetOpt(ch, &Value);
                        hrc = E_FAIL;
                        break;
                }
            }
            if (FAILED(hrc))
                break;

            ComPtr<IProgress> progress;
            CHECK_ERROR_BREAK(console, Teleport(bstrHostname.raw(), uPort,
                                                Bstr(strPassword).raw(),
                                                uMaxDowntime,
                                                progress.asOutParam()));

            if (cMsTimeout)
            {
                hrc = progress->COMSETTER(Timeout)(cMsTimeout);
                if (FAILED(hrc) && hrc != VBOX_E_INVALID_OBJECT_STATE)
                    CHECK_ERROR_BREAK(progress, COMSETTER(Timeout)(cMsTimeout)); /* lazyness */
            }

            hrc = showProgress(progress);
            CHECK_PROGRESS_ERROR(progress, (ControlVM::tr("Teleportation failed")));
        }
        else if (!strcmp(a->argv[1], "screenshotpng"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_SCREENSHOTPNG);
            if (a->argc <= 2 || a->argc > 4)
            {
                errorSyntax(ControlVM::tr("Incorrect number of parameters."));
                hrc = E_FAIL;
                break;
            }
            int vrc;
            uint32_t iScreen = 0;
            if (a->argc == 4)
            {
                vrc = RTStrToUInt32Ex(a->argv[3], NULL, 0, &iScreen);
                if (vrc != VINF_SUCCESS)
                {
                    errorSyntax(ControlVM::tr("Error parsing display number '%s'."), a->argv[3]);
                    hrc = E_FAIL;
                    break;
                }
            }
            ComPtr<IDisplay> pDisplay;
            CHECK_ERROR_BREAK(console, COMGETTER(Display)(pDisplay.asOutParam()));
            if (!pDisplay)
            {
                RTMsgError(ControlVM::tr("Guest not running."));
                hrc = E_FAIL;
                break;
            }
            ULONG width, height, bpp;
            LONG xOrigin, yOrigin;
            GuestMonitorStatus_T monitorStatus;
            CHECK_ERROR_BREAK(pDisplay, GetScreenResolution(iScreen, &width, &height, &bpp, &xOrigin, &yOrigin, &monitorStatus));
            com::SafeArray<BYTE> saScreenshot;
            CHECK_ERROR_BREAK(pDisplay, TakeScreenShotToArray(iScreen, width, height, BitmapFormat_PNG, ComSafeArrayAsOutParam(saScreenshot)));
            RTFILE pngFile = NIL_RTFILE;
            vrc = RTFileOpen(&pngFile, a->argv[2], RTFILE_O_OPEN_CREATE | RTFILE_O_WRITE | RTFILE_O_TRUNCATE | RTFILE_O_DENY_ALL);
            if (RT_FAILURE(vrc))
            {
                RTMsgError(ControlVM::tr("Failed to create file '%s' (%Rrc)."), a->argv[2], vrc);
                hrc = E_FAIL;
                break;
            }
            vrc = RTFileWrite(pngFile, saScreenshot.raw(), saScreenshot.size(), NULL);
            if (RT_FAILURE(vrc))
            {
                RTMsgError(ControlVM::tr("Failed to write screenshot to file '%s' (%Rrc)."), a->argv[2], vrc);
                hrc = E_FAIL;
            }
            RTFileClose(pngFile);
        }
#ifdef VBOX_WITH_RECORDING
        else if (   !strcmp(a->argv[1], "recording")
                 || !strcmp(a->argv[1], "videocap") /* legacy command */)
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_RECORDING);

            if (!strcmp(a->argv[1], "videocap"))
                RTMsgWarning(ControlVM::tr("Sub command 'videocap' is deprecated -- please use 'recording' instead ."));

            if (a->argc < 3)
            {
                errorSyntax(ControlVM::tr("Incorrect number of parameters."));
                hrc = E_FAIL;
                break;
            }

            ComPtr<IRecordingSettings> recordingSettings;
            CHECK_ERROR_BREAK(sessionMachine, COMGETTER(RecordingSettings)(recordingSettings.asOutParam()));

            SafeIfaceArray <IRecordingScreenSettings> saRecordingScreenScreens;
            CHECK_ERROR_BREAK(recordingSettings, COMGETTER(Screens)(ComSafeArrayAsOutParam(saRecordingScreenScreens)));

            ComPtr<IGraphicsAdapter> pGraphicsAdapter;
            CHECK_ERROR_BREAK(sessionMachine, COMGETTER(GraphicsAdapter)(pGraphicsAdapter.asOutParam()));

            /* Note: For now all screens have the same configuration. */

            bool fEnabled;
            if (RT_SUCCESS(parseBool(a->argv[2], &fEnabled)))
            {
                CHECK_ERROR_RET(recordingSettings, COMSETTER(Enabled)(fEnabled), RTEXITCODE_FAILURE);

                if (fEnabled)
                    RTPrintf(ControlVM::tr("Recording enabled. Use 'start' to start recording.\n"));
            }
            else if (!strcmp(a->argv[2], "start"))
            {
                bool fWait = false;
                if (a->argc >= 4 && !strcmp(a->argv[3], "--wait"))
                    fWait = true;

                ComPtr<IProgress> progress;
                CHECK_ERROR_BREAK(recordingSettings, Start(progress.asOutParam()));

                if (fWait)
                {
                    hrc = showProgress(progress, SHOW_PROGRESS_OPS);
                    CHECK_PROGRESS_ERROR(progress, (ControlVM::tr("Recording failed.")));
                }
                else
                    RTPrintf(ControlVM::tr("Recording started (detacted).\n"));
            }
            else if (!strcmp(a->argv[2], "stop"))
            {
                ComPtr<IProgress> progress;
                CHECK_ERROR_BREAK(recordingSettings, COMGETTER(Progress)(progress.asOutParam()));
                CHECK_ERROR_BREAK(progress, Cancel());
            }
            else if (!strcmp(a->argv[2], "attach"))
            {
                ComPtr<IProgress> progress;
                CHECK_ERROR_BREAK(recordingSettings, COMGETTER(Progress)(progress.asOutParam()));
                hrc = showProgress(progress, SHOW_PROGRESS_OPS);
            }
            else if (!strcmp(a->argv[2], "screens"))
            {
                ULONG cMonitors = 64;
                CHECK_ERROR_BREAK(pGraphicsAdapter, COMGETTER(MonitorCount)(&cMonitors));
                com::SafeArray<BOOL> saScreens(cMonitors);
                if (a->argc != 4)
                {
                    errorSyntax(ControlVM::tr("Incorrect number of parameters."));
                    hrc = E_FAIL;
                    break;
                }
                if (RT_FAILURE(parseScreens(a->argv[3], &saScreens)))
                {
                    errorSyntax(ControlVM::tr("Error parsing list of screen IDs '%s'."), a->argv[3]);
                    hrc = E_FAIL;
                    break;
                }

                for (size_t i = 0; i < saRecordingScreenScreens.size(); ++i)
                    CHECK_ERROR_BREAK(saRecordingScreenScreens[i], COMSETTER(Enabled)(saScreens[i]));
            }
            else if (!strcmp(a->argv[2], "filename"))
            {
                if (a->argc != 4)
                {
                    errorSyntax(ControlVM::tr("Incorrect number of parameters."));
                    hrc = E_FAIL;
                    break;
                }

                for (size_t i = 0; i < saRecordingScreenScreens.size(); ++i)
                    CHECK_ERROR_BREAK(saRecordingScreenScreens[i], COMSETTER(Filename)(Bstr(a->argv[3]).raw()));
            }
            else if (   !strcmp(a->argv[2], "videores")
                     || !strcmp(a->argv[2], "videoresolution"))
            {
                if (a->argc != 5)
                {
                    errorSyntax(ControlVM::tr("Incorrect number of parameters."));
                    hrc = E_FAIL;
                    break;
                }

                uint32_t uWidth;
                int vrc = RTStrToUInt32Ex(a->argv[3], NULL, 0, &uWidth);
                if (RT_FAILURE(vrc))
                {
                    errorSyntax(ControlVM::tr("Error parsing video width '%s'."), a->argv[3]);
                    hrc = E_FAIL;
                    break;
                }

                uint32_t uHeight;
                vrc = RTStrToUInt32Ex(a->argv[4], NULL, 0, &uHeight);
                if (RT_FAILURE(vrc))
                {
                    errorSyntax(ControlVM::tr("Error parsing video height '%s'."), a->argv[4]);
                    hrc = E_FAIL;
                    break;
                }

                for (size_t i = 0; i < saRecordingScreenScreens.size(); ++i)
                {
                    CHECK_ERROR_BREAK(saRecordingScreenScreens[i], COMSETTER(VideoWidth)(uWidth));
                    CHECK_ERROR_BREAK(saRecordingScreenScreens[i], COMSETTER(VideoHeight)(uHeight));
                }
            }
            else if (!strcmp(a->argv[2], "videorate"))
            {
                if (a->argc != 4)
                {
                    errorSyntax(ControlVM::tr("Incorrect number of parameters."));
                    hrc = E_FAIL;
                    break;
                }

                uint32_t uRate;
                int vrc = RTStrToUInt32Ex(a->argv[3], NULL, 0, &uRate);
                if (RT_FAILURE(vrc))
                {
                    errorSyntax(ControlVM::tr("Error parsing video rate '%s'."), a->argv[3]);
                    hrc = E_FAIL;
                    break;
                }

                for (size_t i = 0; i < saRecordingScreenScreens.size(); ++i)
                    CHECK_ERROR_BREAK(saRecordingScreenScreens[i], COMSETTER(VideoRate)(uRate));
            }
            else if (!strcmp(a->argv[2], "videofps"))
            {
                if (a->argc != 4)
                {
                    errorSyntax(ControlVM::tr("Incorrect number of parameters."));
                    hrc = E_FAIL;
                    break;
                }

                uint32_t uFPS;
                int vrc = RTStrToUInt32Ex(a->argv[3], NULL, 0, &uFPS);
                if (RT_FAILURE(vrc))
                {
                    errorSyntax(ControlVM::tr("Error parsing video FPS '%s'."), a->argv[3]);
                    hrc = E_FAIL;
                    break;
                }

                for (size_t i = 0; i < saRecordingScreenScreens.size(); ++i)
                    CHECK_ERROR_BREAK(saRecordingScreenScreens[i], COMSETTER(VideoFPS)(uFPS));
            }
            else if (!strcmp(a->argv[2], "maxtime"))
            {
                if (a->argc != 4)
                {
                    errorSyntax(ControlVM::tr("Incorrect number of parameters."));
                    hrc = E_FAIL;
                    break;
                }

                uint32_t uMaxTime;
                int vrc = RTStrToUInt32Ex(a->argv[3], NULL, 0, &uMaxTime);
                if (RT_FAILURE(vrc))
                {
                    errorSyntax(ControlVM::tr("Error parsing maximum time '%s'."), a->argv[3]);
                    hrc = E_FAIL;
                    break;
                }

                for (size_t i = 0; i < saRecordingScreenScreens.size(); ++i)
                    CHECK_ERROR_BREAK(saRecordingScreenScreens[i], COMSETTER(MaxTime)(uMaxTime));
            }
            else if (!strcmp(a->argv[2], "maxfilesize"))
            {
                if (a->argc != 4)
                {
                    errorSyntax(ControlVM::tr("Incorrect number of parameters."));
                    hrc = E_FAIL;
                    break;
                }

                uint32_t uMaxFileSize;
                int vrc = RTStrToUInt32Ex(a->argv[3], NULL, 0, &uMaxFileSize);
                if (RT_FAILURE(vrc))
                {
                    errorSyntax(ControlVM::tr("Error parsing maximum file size '%s'."), a->argv[3]);
                    hrc = E_FAIL;
                    break;
                }

                for (size_t i = 0; i < saRecordingScreenScreens.size(); ++i)
                    CHECK_ERROR_BREAK(saRecordingScreenScreens[i], COMSETTER(MaxFileSize)(uMaxFileSize));
            }
            else if (!strcmp(a->argv[2], "opts"))
            {
                if (a->argc != 4)
                {
                    errorSyntax(ControlVM::tr("Incorrect number of parameters."));
                    hrc = E_FAIL;
                    break;
                }

                for (size_t i = 0; i < saRecordingScreenScreens.size(); ++i)
                    CHECK_ERROR_BREAK(saRecordingScreenScreens[i], COMSETTER(Options)(Bstr(a->argv[3]).raw()));
            }
        }
#endif /* VBOX_WITH_RECORDING */
        else if (!strcmp(a->argv[1], "webcam"))
        {
            if (a->argc < 3)
            {
                errorArgument(ControlVM::tr("Missing argument to '%s'."), a->argv[1]);
                hrc = E_FAIL;
                break;
            }

            ComPtr<IEmulatedUSB> pEmulatedUSB;
            CHECK_ERROR_BREAK(console, COMGETTER(EmulatedUSB)(pEmulatedUSB.asOutParam()));
            if (!pEmulatedUSB)
            {
                RTMsgError(ControlVM::tr("Guest not running."));
                hrc = E_FAIL;
                break;
            }

            if (!strcmp(a->argv[2], "attach"))
            {
                setCurrentSubcommand(HELP_SCOPE_CONTROLVM_WEBCAM_ATTACH);
                Bstr path("");
                if (a->argc >= 4)
                    path = a->argv[3];
                Bstr settings("");
                if (a->argc >= 5)
                    settings = a->argv[4];
                CHECK_ERROR_BREAK(pEmulatedUSB, WebcamAttach(path.raw(), settings.raw()));
            }
            else if (!strcmp(a->argv[2], "detach"))
            {
                setCurrentSubcommand(HELP_SCOPE_CONTROLVM_WEBCAM_DETACH);
                Bstr path("");
                if (a->argc >= 4)
                    path = a->argv[3];
                CHECK_ERROR_BREAK(pEmulatedUSB, WebcamDetach(path.raw()));
            }
            else if (!strcmp(a->argv[2], "list"))
            {
                setCurrentSubcommand(HELP_SCOPE_CONTROLVM_WEBCAM_LIST);
                com::SafeArray <BSTR> webcams;
                CHECK_ERROR_BREAK(pEmulatedUSB, COMGETTER(Webcams)(ComSafeArrayAsOutParam(webcams)));
                for (size_t i = 0; i < webcams.size(); ++i)
                {
                    RTPrintf("%ls\n", webcams[i][0]? webcams[i]: Bstr("default").raw());
                }
            }
            else
            {
                errorArgument(ControlVM::tr("Invalid argument to '%s'."), a->argv[1]);
                hrc = E_FAIL;
                break;
            }
        }
        else if (!strcmp(a->argv[1], "addencpassword"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_ADDENCPASSWORD);
            if (   a->argc != 4
                && a->argc != 6)
            {
                errorSyntax(ControlVM::tr("Incorrect number of parameters."));
                break;
            }

            BOOL fRemoveOnSuspend = FALSE;
            if (a->argc == 6)
            {
                if (   strcmp(a->argv[4], "--removeonsuspend")
                    || (   strcmp(a->argv[5], "yes")
                        && strcmp(a->argv[5], "no")))
                {
                    errorSyntax(ControlVM::tr("Invalid parameters."));
                    break;
                }
                if (!strcmp(a->argv[5], "yes"))
                    fRemoveOnSuspend = TRUE;
            }

            Bstr bstrPwId(a->argv[2]);
            Utf8Str strPassword;

            if (!RTStrCmp(a->argv[3], "-"))
            {
                /* Get password from console. */
                RTEXITCODE rcExit = readPasswordFromConsole(&strPassword, ControlVM::tr("Enter password:"));
                if (rcExit == RTEXITCODE_FAILURE)
                    break;
            }
            else
            {
                RTEXITCODE rcExit = readPasswordFile(a->argv[3], &strPassword);
                if (rcExit == RTEXITCODE_FAILURE)
                {
                    RTMsgError(ControlVM::tr("Failed to read new password from file."));
                    break;
                }
            }

            CHECK_ERROR_BREAK(console, AddEncryptionPassword(bstrPwId.raw(), Bstr(strPassword).raw(), fRemoveOnSuspend));
        }
        else if (!strcmp(a->argv[1], "removeencpassword"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_REMOVEENCPASSWORD);
            if (a->argc != 3)
            {
                errorSyntax(ControlVM::tr("Incorrect number of parameters."));
                break;
            }
            Bstr bstrPwId(a->argv[2]);
            CHECK_ERROR_BREAK(console, RemoveEncryptionPassword(bstrPwId.raw()));
        }
        /** @todo r=bird: 'removeallencpasswords' is very much unreadable carp. Use
         *        dashes as word separators to make it less fishy. */
        else if (!strcmp(a->argv[1], "removeallencpasswords"))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_REMOVEALLENCPASSWORDS);
            CHECK_ERROR_BREAK(console, ClearAllEncryptionPasswords());
        }
        else if (!strncmp(a->argv[1], "changeuartmode", 14))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_CHANGEUARTMODE);
            unsigned n = parseNum(&a->argv[1][14], 4, "UART");
            if (!n)
            {
                hrc = E_FAIL;
                break;
            }
            if (a->argc < 3)
            {
                errorSyntax(ControlVM::tr("Missing argument to '%s'."), a->argv[1]);
                hrc = E_FAIL;
                break;
            }

            ComPtr<ISerialPort> uart;

            CHECK_ERROR_BREAK(sessionMachine, GetSerialPort(n - 1, uart.asOutParam()));
            ASSERT(uart);

            if (!RTStrICmp(a->argv[2], "disconnected"))
            {
                if (a->argc != 3)
                {
                    errorSyntax(ControlVM::tr("Incorrect arguments to '%s'."), a->argv[1]);
                    hrc = E_FAIL;
                    break;
                }
                CHECK_ERROR(uart, COMSETTER(HostMode)(PortMode_Disconnected));
            }
            else if (   !RTStrICmp(a->argv[2], "server")
                     || !RTStrICmp(a->argv[2], "client")
                     || !RTStrICmp(a->argv[2], "tcpserver")
                     || !RTStrICmp(a->argv[2], "tcpclient")
                     || !RTStrICmp(a->argv[2], "file"))
            {
                const char *pszMode = a->argv[2];
                if (a->argc != 4)
                {
                    errorSyntax(ControlVM::tr("Incorrect arguments to '%s'."), a->argv[1]);
                    hrc = E_FAIL;
                    break;
                }

                CHECK_ERROR(uart, COMSETTER(Path)(Bstr(a->argv[3]).raw()));

                /*
                 * Change to disconnected first to get changes in just a parameter causing
                 * the correct changes later on.
                 */
                CHECK_ERROR(uart, COMSETTER(HostMode)(PortMode_Disconnected));
                if (!RTStrICmp(pszMode, "server"))
                {
                    CHECK_ERROR(uart, COMSETTER(Server)(TRUE));
                    CHECK_ERROR(uart, COMSETTER(HostMode)(PortMode_HostPipe));
                }
                else if (!RTStrICmp(pszMode, "client"))
                {
                    CHECK_ERROR(uart, COMSETTER(Server)(FALSE));
                    CHECK_ERROR(uart, COMSETTER(HostMode)(PortMode_HostPipe));
                }
                else if (!RTStrICmp(pszMode, "tcpserver"))
                {
                    CHECK_ERROR(uart, COMSETTER(Server)(TRUE));
                    CHECK_ERROR(uart, COMSETTER(HostMode)(PortMode_TCP));
                }
                else if (!RTStrICmp(pszMode, "tcpclient"))
                {
                    CHECK_ERROR(uart, COMSETTER(Server)(FALSE));
                    CHECK_ERROR(uart, COMSETTER(HostMode)(PortMode_TCP));
                }
                else if (!RTStrICmp(pszMode, "file"))
                {
                    CHECK_ERROR(uart, COMSETTER(HostMode)(PortMode_RawFile));
                }
            }
            else
            {
                if (a->argc != 3)
                {
                    errorSyntax(ControlVM::tr("Incorrect arguments to '%s'."), a->argv[1]);
                    hrc = E_FAIL;
                    break;
                }
                CHECK_ERROR(uart, COMSETTER(Path)(Bstr(a->argv[2]).raw()));
                CHECK_ERROR(uart, COMSETTER(HostMode)(PortMode_HostDevice));
            }
        }
        else if (!strncmp(a->argv[1], "vm-process-priority", 14))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_VM_PROCESS_PRIORITY);
            if (a->argc != 3)
            {
                errorSyntax(ControlVM::tr("Incorrect arguments to '%s'."), a->argv[1]);
                hrc = E_FAIL;
                break;
            }
            VMProcPriority_T enmPriority = nameToVMProcPriority(a->argv[2]);
            if (enmPriority == VMProcPriority_Invalid)
            {
                errorSyntax(ControlVM::tr("Invalid vm-process-priority '%s'."), a->argv[2]);
                hrc = E_FAIL;
            }
            else
            {
                CHECK_ERROR(sessionMachine, COMSETTER(VMProcessPriority)(enmPriority));
            }
            break;
        }
        else if (!strncmp(a->argv[1], "autostart-enabled", 17))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_AUTOSTART_ENABLED);
            if (a->argc != 3)
            {
                errorSyntax(ControlVM::tr("Incorrect arguments to '%s'."), a->argv[1]);
                hrc = E_FAIL;
                break;
            }
            bool fEnabled;
            if (RT_FAILURE(parseBool(a->argv[2], &fEnabled)))
            {
                errorSyntax(ControlVM::tr("Invalid value '%s'."), a->argv[2]);
                hrc = E_FAIL;
                break;
            }
            CHECK_ERROR(sessionMachine, COMSETTER(AutostartEnabled)(TRUE));
            fNeedsSaving = true;
            break;
        }
        else if (!strncmp(a->argv[1], "autostart-delay", 15))
        {
            setCurrentSubcommand(HELP_SCOPE_CONTROLVM_AUTOSTART_DELAY);
            if (a->argc != 3)
            {
                errorSyntax(ControlVM::tr("Incorrect arguments to '%s'."), a->argv[1]);
                hrc = E_FAIL;
                break;
            }
            uint32_t u32;
            char *pszNext;
            int vrc = RTStrToUInt32Ex(a->argv[2], &pszNext, 10, &u32);
            if (RT_FAILURE(vrc) || *pszNext != '\0')
            {
                errorSyntax(ControlVM::tr("Invalid autostart delay number '%s'."), a->argv[2]);
                hrc = E_FAIL;
                break;
            }
            CHECK_ERROR(sessionMachine, COMSETTER(AutostartDelay)(u32));
            if (SUCCEEDED(hrc))
                fNeedsSaving = true;
            break;
        }
        else
        {
            errorSyntax(ControlVM::tr("Invalid parameter '%s'."), a->argv[1]);
            hrc = E_FAIL;
        }
    } while (0);

    /* The client has to trigger saving the state explicitely. */
    if (fNeedsSaving)
        CHECK_ERROR(sessionMachine, SaveSettings());

    a->session->UnlockMachine();

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}
