/* $Id: ConsoleImplTeleporter.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Console COM Class implementation, The Teleporter Part.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_MAIN_CONSOLE
#include "LoggingNew.h"

#include "ConsoleImpl.h"
#include "ProgressImpl.h"
#include "Global.h"
#include "StringifyEnums.h"

#include "AutoCaller.h"
#include "HashedPw.h"

#include <iprt/asm.h>
#include <iprt/err.h>
#include <iprt/rand.h>
#include <iprt/socket.h>
#include <iprt/tcp.h>
#include <iprt/timer.h>

#include <VBox/vmm/vmapi.h>
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/vmmr3vtable.h>
#include <VBox/err.h>
#include <VBox/version.h>
#include <VBox/com/string.h>
#include "VBox/com/ErrorInfo.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Base class for the teleporter state.
 *
 * These classes are used as advanced structs, not as proper classes.
 */
class TeleporterState
{
public:
    ComPtr<Console>     mptrConsole;
    PUVM                mpUVM;
    PCVMMR3VTABLE       mpVMM;
    ComObjPtr<Progress> mptrProgress;
    Utf8Str             mstrPassword;
    bool const          mfIsSource;

    /** @name stream stuff
     * @{  */
    RTSOCKET            mhSocket;
    uint64_t            moffStream;
    uint32_t            mcbReadBlock;
    bool volatile       mfStopReading;
    bool volatile       mfEndOfStream;
    bool volatile       mfIOError;
    /** @} */

    TeleporterState(Console *pConsole, PUVM pUVM, PCVMMR3VTABLE pVMM, Progress *pProgress, bool fIsSource)
        : mptrConsole(pConsole)
        , mpUVM(pUVM)
        , mpVMM(pVMM)
        , mptrProgress(pProgress)
        , mfIsSource(fIsSource)
        , mhSocket(NIL_RTSOCKET)
        , moffStream(UINT64_MAX / 2)
        , mcbReadBlock(0)
        , mfStopReading(false)
        , mfEndOfStream(false)
        , mfIOError(false)
    {
        pVMM->pfnVMR3RetainUVM(mpUVM);
    }

    ~TeleporterState()
    {
        if (mpVMM)
            mpVMM->pfnVMR3ReleaseUVM(mpUVM);
        mpUVM = NULL;
    }
};


/**
 * Teleporter state used by the source side.
 */
class TeleporterStateSrc : public TeleporterState
{
public:
    Utf8Str             mstrHostname;
    uint32_t            muPort;
    uint32_t            mcMsMaxDowntime;
    MachineState_T      menmOldMachineState;
    bool                mfSuspendedByUs;
    bool                mfUnlockedMedia;

    TeleporterStateSrc(Console *pConsole, PUVM pUVM, PCVMMR3VTABLE pVMM, Progress *pProgress, MachineState_T enmOldMachineState)
        : TeleporterState(pConsole, pUVM, pVMM, pProgress, true /*fIsSource*/)
        , muPort(UINT32_MAX)
        , mcMsMaxDowntime(250)
        , menmOldMachineState(enmOldMachineState)
        , mfSuspendedByUs(false)
        , mfUnlockedMedia(false)
    {
    }
};


/**
 * Teleporter state used by the destination side.
 */
class TeleporterStateTrg : public TeleporterState
{
public:
    IMachine                   *mpMachine;
    IInternalMachineControl    *mpControl;
    PRTTCPSERVER                mhServer;
    PRTTIMERLR                  mphTimerLR;
    bool                        mfLockedMedia;
    int                         mRc;
    Utf8Str                     mErrorText;

    TeleporterStateTrg(Console *pConsole, PUVM pUVM, PCVMMR3VTABLE pVMM, Progress *pProgress,
                       IMachine *pMachine, IInternalMachineControl *pControl,
                       PRTTIMERLR phTimerLR, bool fStartPaused)
        : TeleporterState(pConsole, pUVM, pVMM, pProgress, false /*fIsSource*/)
        , mpMachine(pMachine)
        , mpControl(pControl)
        , mhServer(NULL)
        , mphTimerLR(phTimerLR)
        , mfLockedMedia(false)
        , mRc(VINF_SUCCESS)
        , mErrorText()
    {
        RT_NOREF(fStartPaused); /** @todo figure out why fStartPaused isn't used */
    }
};


/**
 * TCP stream header.
 *
 * This is an extra layer for fixing the problem with figuring out when the SSM
 * stream ends.
 */
typedef struct TELEPORTERTCPHDR
{
    /** Magic value. */
    uint32_t    u32Magic;
    /** The size of the data block following this header.
     * 0 indicates the end of the stream, while UINT32_MAX indicates
     * cancelation. */
    uint32_t    cb;
} TELEPORTERTCPHDR;
/** Magic value for TELEPORTERTCPHDR::u32Magic. (Egberto Gismonti Amin) */
#define TELEPORTERTCPHDR_MAGIC       UINT32_C(0x19471205)
/** The max block size. */
#define TELEPORTERTCPHDR_MAX_SIZE    UINT32_C(0x00fffff8)


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static const char g_szWelcome[] = "VirtualBox-Teleporter-1.0\n";


/**
 * Reads a string from the socket.
 *
 * @returns VBox status code.
 *
 * @param   pState      The teleporter state structure.
 * @param   pszBuf      The output buffer.
 * @param   cchBuf      The size of the output buffer.
 *
 */
static int teleporterTcpReadLine(TeleporterState *pState, char *pszBuf, size_t cchBuf)
{
    char       *pszStart = pszBuf;
    RTSOCKET    hSocket  = pState->mhSocket;

    AssertReturn(cchBuf > 1, VERR_INTERNAL_ERROR);
    *pszBuf = '\0';

    /* dead simple approach. */
    for (;;)
    {
        char ch;
        int vrc = RTTcpRead(hSocket, &ch, sizeof(ch), NULL);
        if (RT_FAILURE(vrc))
        {
            LogRel(("Teleporter: RTTcpRead -> %Rrc while reading string ('%s')\n", vrc, pszStart));
            return vrc;
        }
        if (    ch == '\n'
            ||  ch == '\0')
            return VINF_SUCCESS;
        if (cchBuf <= 1)
        {
            LogRel(("Teleporter: String buffer overflow: '%s'\n", pszStart));
            return VERR_BUFFER_OVERFLOW;
        }
        *pszBuf++ = ch;
        *pszBuf = '\0';
        cchBuf--;
    }
}


/**
 * Reads an ACK or NACK.
 *
 * @returns S_OK on ACK, E_FAIL+setError() on failure or NACK.
 * @param   pState              The teleporter source state.
 * @param   pszWhich            Which ACK is this this?
 * @param   pszNAckMsg          Optional NACK message.
 *
 * @remarks the setError laziness forces this to be a Console member.
 */
HRESULT
Console::i_teleporterSrcReadACK(TeleporterStateSrc *pState, const char *pszWhich, const char *pszNAckMsg /*= NULL*/)
{
    char szMsg[256];
    int vrc = teleporterTcpReadLine(pState, szMsg, sizeof(szMsg));
    if (RT_FAILURE(vrc))
        return setErrorBoth(E_FAIL, vrc, tr("Failed reading ACK(%s): %Rrc"), pszWhich, vrc);

    if (!strcmp(szMsg, "ACK"))
        return S_OK;

    if (!strncmp(szMsg, RT_STR_TUPLE("NACK=")))
    {
        char *pszMsgText = strchr(szMsg, ';');
        if (pszMsgText)
            *pszMsgText++ = '\0';

        int32_t vrc2;
        vrc = RTStrToInt32Full(&szMsg[sizeof("NACK=") - 1], 10, &vrc2);
        if (vrc == VINF_SUCCESS)
        {
            /*
             * Well formed NACK, transform it into an error.
             */
            if (pszNAckMsg)
            {
                LogRel(("Teleporter: %s: NACK=%Rrc (%d)\n", pszWhich, vrc2, vrc2));
                return setError(E_FAIL, pszNAckMsg);
            }

            if (pszMsgText)
            {
                pszMsgText = RTStrStrip(pszMsgText);
                for (size_t off = 0; pszMsgText[off]; off++)
                    if (pszMsgText[off] == '\r')
                        pszMsgText[off] = '\n';

                LogRel(("Teleporter: %s: NACK=%Rrc (%d) - '%s'\n", pszWhich, vrc2, vrc2, pszMsgText));
                if (strlen(pszMsgText) > 4)
                    return setError(E_FAIL, "%s", pszMsgText);
                return setError(E_FAIL, "NACK(%s) - %Rrc (%d) '%s'", pszWhich, vrc2, vrc2, pszMsgText);
            }

            return setError(E_FAIL, "NACK(%s) - %Rrc (%d)", pszWhich, vrc2, vrc2);
        }

        if (pszMsgText)
            pszMsgText[-1] = ';';
    }
    return setError(E_FAIL, tr("%s: Expected ACK or NACK, got '%s'"), pszWhich, szMsg);
}


/**
 * Submitts a command to the destination and waits for the ACK.
 *
 * @returns S_OK on ACKed command, E_FAIL+setError() on failure.
 *
 * @param   pState              The teleporter source state.
 * @param   pszCommand          The command.
 * @param   fWaitForAck         Whether to wait for the ACK.
 *
 * @remarks the setError laziness forces this to be a Console member.
 */
HRESULT Console::i_teleporterSrcSubmitCommand(TeleporterStateSrc *pState, const char *pszCommand, bool fWaitForAck /*= true*/)
{
    int vrc = RTTcpSgWriteL(pState->mhSocket, 2, pszCommand, strlen(pszCommand), "\n", sizeof("\n") - 1);
    if (RT_FAILURE(vrc))
        return setErrorBoth(E_FAIL, vrc, tr("Failed writing command '%s': %Rrc"), pszCommand, vrc);
    if (!fWaitForAck)
        return S_OK;
    return i_teleporterSrcReadACK(pState, pszCommand);
}


/**
 * @copydoc SSMSTRMOPS::pfnWrite
 */
static DECLCALLBACK(int) teleporterTcpOpWrite(void *pvUser, uint64_t offStream, const void *pvBuf, size_t cbToWrite)
{
    RT_NOREF(offStream);
    TeleporterState *pState = (TeleporterState *)pvUser;

    AssertReturn(cbToWrite > 0, VINF_SUCCESS);
    AssertReturn(cbToWrite < UINT32_MAX, VERR_OUT_OF_RANGE);
    AssertReturn(pState->mfIsSource, VERR_INVALID_HANDLE);

    for (;;)
    {
        TELEPORTERTCPHDR Hdr;
        Hdr.u32Magic = TELEPORTERTCPHDR_MAGIC;
        Hdr.cb       = RT_MIN((uint32_t)cbToWrite, TELEPORTERTCPHDR_MAX_SIZE);
        int vrc = RTTcpSgWriteL(pState->mhSocket, 2, &Hdr, sizeof(Hdr), pvBuf, (size_t)Hdr.cb);
        if (RT_FAILURE(vrc))
        {
            LogRel(("Teleporter/TCP: Write error: %Rrc (cb=%#x)\n", vrc, Hdr.cb));
            return vrc;
        }
        pState->moffStream += Hdr.cb;
        if (Hdr.cb == cbToWrite)
            return VINF_SUCCESS;

        /* advance */
        cbToWrite -= Hdr.cb;
        pvBuf = (uint8_t const *)pvBuf + Hdr.cb;
    }
}


/**
 * Selects and poll for close condition.
 *
 * We can use a relatively high poll timeout here since it's only used to get
 * us out of error paths.  In the normal cause of events, we'll get a
 * end-of-stream header.
 *
 * @returns VBox status code.
 *
 * @param   pState          The teleporter state data.
 */
static int teleporterTcpReadSelect(TeleporterState *pState)
{
    int vrc;
    do
    {
        vrc = RTTcpSelectOne(pState->mhSocket, 1000);
        if (RT_FAILURE(vrc) && vrc != VERR_TIMEOUT)
        {
            pState->mfIOError = true;
            LogRel(("Teleporter/TCP: Header select error: %Rrc\n", vrc));
            break;
        }
        if (pState->mfStopReading)
        {
            vrc = VERR_EOF;
            break;
        }
    } while (vrc == VERR_TIMEOUT);
    return vrc;
}


/**
 * @copydoc SSMSTRMOPS::pfnRead
 */
static DECLCALLBACK(int) teleporterTcpOpRead(void *pvUser, uint64_t offStream, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    RT_NOREF(offStream);
    TeleporterState *pState = (TeleporterState *)pvUser;
    AssertReturn(!pState->mfIsSource, VERR_INVALID_HANDLE);

    for (;;)
    {
        int vrc;

        /*
         * Check for various conditions and may have been signalled.
         */
        if (pState->mfEndOfStream)
            return VERR_EOF;
        if (pState->mfStopReading)
            return VERR_EOF;
        if (pState->mfIOError)
            return VERR_IO_GEN_FAILURE;

        /*
         * If there is no more data in the current block, read the next
         * block header.
         */
        if (!pState->mcbReadBlock)
        {
            vrc = teleporterTcpReadSelect(pState);
            if (RT_FAILURE(vrc))
                return vrc;
            TELEPORTERTCPHDR Hdr;
            vrc = RTTcpRead(pState->mhSocket, &Hdr, sizeof(Hdr), NULL);
            if (RT_FAILURE(vrc))
            {
                pState->mfIOError = true;
                LogRel(("Teleporter/TCP: Header read error: %Rrc\n", vrc));
                return vrc;
            }

            if (RT_UNLIKELY(   Hdr.u32Magic != TELEPORTERTCPHDR_MAGIC
                            || Hdr.cb > TELEPORTERTCPHDR_MAX_SIZE
                            || Hdr.cb == 0))
            {
                if (    Hdr.u32Magic == TELEPORTERTCPHDR_MAGIC
                    &&  (   Hdr.cb == 0
                         || Hdr.cb == UINT32_MAX)
                   )
                {
                    pState->mfEndOfStream = true;
                    pState->mcbReadBlock  = 0;
                    return Hdr.cb ? VERR_SSM_CANCELLED : VERR_EOF;
                }
                pState->mfIOError = true;
                LogRel(("Teleporter/TCP: Invalid block: u32Magic=%#x cb=%#x\n", Hdr.u32Magic, Hdr.cb));
                return VERR_IO_GEN_FAILURE;
            }

            pState->mcbReadBlock = Hdr.cb;
            if (pState->mfStopReading)
                return VERR_EOF;
        }

        /*
         * Read more data.
         */
        vrc = teleporterTcpReadSelect(pState);
        if (RT_FAILURE(vrc))
            return vrc;
        uint32_t cb = (uint32_t)RT_MIN(pState->mcbReadBlock, cbToRead);
        vrc = RTTcpRead(pState->mhSocket, pvBuf, cb, pcbRead);
        if (RT_FAILURE(vrc))
        {
            pState->mfIOError = true;
            LogRel(("Teleporter/TCP: Data read error: %Rrc (cb=%#x)\n", vrc, cb));
            return vrc;
        }
        if (pcbRead)
        {
            cb = (uint32_t)*pcbRead;
            pState->moffStream   += cb;
            pState->mcbReadBlock -= cb;
            return VINF_SUCCESS;
        }
        pState->moffStream   += cb;
        pState->mcbReadBlock -= cb;
        if (cbToRead == cb)
            return VINF_SUCCESS;

        /* Advance to the next block. */
        cbToRead -= cb;
        pvBuf = (uint8_t *)pvBuf + cb;
    }
}


/**
 * @copydoc SSMSTRMOPS::pfnSeek
 */
static DECLCALLBACK(int) teleporterTcpOpSeek(void *pvUser, int64_t offSeek, unsigned uMethod, uint64_t *poffActual)
{
    RT_NOREF(pvUser, offSeek, uMethod, poffActual);
    return VERR_NOT_SUPPORTED;
}


/**
 * @copydoc SSMSTRMOPS::pfnTell
 */
static DECLCALLBACK(uint64_t) teleporterTcpOpTell(void *pvUser)
{
    TeleporterState *pState = (TeleporterState *)pvUser;
    return pState->moffStream;
}


/**
 * @copydoc SSMSTRMOPS::pfnSize
 */
static DECLCALLBACK(int) teleporterTcpOpSize(void *pvUser, uint64_t *pcb)
{
    RT_NOREF(pvUser, pcb);
    return VERR_NOT_SUPPORTED;
}


/**
 * @copydoc SSMSTRMOPS::pfnIsOk
 */
static DECLCALLBACK(int) teleporterTcpOpIsOk(void *pvUser)
{
    TeleporterState *pState = (TeleporterState *)pvUser;

    if (pState->mfIsSource)
    {
        /* Poll for incoming NACKs and errors from the other side */
        int vrc = RTTcpSelectOne(pState->mhSocket, 0);
        if (vrc != VERR_TIMEOUT)
        {
            if (RT_SUCCESS(vrc))
            {
                LogRel(("Teleporter/TCP: Incoming data detect by IsOk, assuming it is a cancellation NACK.\n"));
                vrc = VERR_SSM_CANCELLED;
            }
            else
                LogRel(("Teleporter/TCP: RTTcpSelectOne -> %Rrc (IsOk).\n", vrc));
            return vrc;
        }
    }

    return VINF_SUCCESS;
}


/**
 * @copydoc SSMSTRMOPS::pfnClose
 */
static DECLCALLBACK(int) teleporterTcpOpClose(void *pvUser, bool fCancelled)
{
    TeleporterState *pState = (TeleporterState *)pvUser;

    if (pState->mfIsSource)
    {
        TELEPORTERTCPHDR EofHdr;
        EofHdr.u32Magic = TELEPORTERTCPHDR_MAGIC;
        EofHdr.cb       = fCancelled ? UINT32_MAX : 0;
        int vrc = RTTcpWrite(pState->mhSocket, &EofHdr, sizeof(EofHdr));
        if (RT_FAILURE(vrc))
        {
            LogRel(("Teleporter/TCP: EOF Header write error: %Rrc\n", vrc));
            return vrc;
        }
    }
    else
    {
        ASMAtomicWriteBool(&pState->mfStopReading, true);
    }

    return VINF_SUCCESS;
}


/**
 * Method table for a TCP based stream.
 */
static SSMSTRMOPS const g_teleporterTcpOps =
{
    SSMSTRMOPS_VERSION,
    teleporterTcpOpWrite,
    teleporterTcpOpRead,
    teleporterTcpOpSeek,
    teleporterTcpOpTell,
    teleporterTcpOpSize,
    teleporterTcpOpIsOk,
    teleporterTcpOpClose,
    SSMSTRMOPS_VERSION
};


/**
 * Progress cancelation callback.
 */
static void teleporterProgressCancelCallback(void *pvUser)
{
    TeleporterState *pState = (TeleporterState *)pvUser;
    pState->mpVMM->pfnSSMR3Cancel(pState->mpUVM);
    if (!pState->mfIsSource)
    {
        TeleporterStateTrg *pStateTrg = (TeleporterStateTrg *)pState;
        RTTcpServerShutdown(pStateTrg->mhServer);
    }
}

/**
 * @copydoc PFNVMPROGRESS
 */
static DECLCALLBACK(int) teleporterProgressCallback(PUVM pUVM, unsigned uPercent, void *pvUser)
{
    TeleporterState *pState = (TeleporterState *)pvUser;
    if (pState->mptrProgress)
    {
        HRESULT hrc = pState->mptrProgress->SetCurrentOperationProgress(uPercent);
        if (FAILED(hrc))
        {
            /* check if the failure was caused by cancellation. */
            BOOL fCanceled;
            hrc = pState->mptrProgress->COMGETTER(Canceled)(&fCanceled);
            if (SUCCEEDED(hrc) && fCanceled)
            {
                pState->mpVMM->pfnSSMR3Cancel(pState->mpUVM);
                return VERR_SSM_CANCELLED;
            }
        }
    }

    NOREF(pUVM);
    return VINF_SUCCESS;
}


/**
 * @copydoc FNRTTIMERLR
 */
static DECLCALLBACK(void) teleporterDstTimeout(RTTIMERLR hTimerLR, void *pvUser, uint64_t iTick)
{
    RT_NOREF(hTimerLR, iTick);
    /* This is harmless for any open connections. */
    RTTcpServerShutdown((PRTTCPSERVER)pvUser);
}


/**
 * Do the teleporter.
 *
 * @returns VBox status code.
 * @param   pState              The teleporter state.
 */
HRESULT Console::i_teleporterSrc(TeleporterStateSrc *pState)
{
    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    /*
     * Wait for Console::Teleport to change the state.
     */
    { AutoWriteLock autoLock(this COMMA_LOCKVAL_SRC_POS); }

    BOOL fCanceled = TRUE;
    HRESULT hrc = pState->mptrProgress->COMGETTER(Canceled)(&fCanceled);
    if (FAILED(hrc))
        return hrc;
    if (fCanceled)
        return setError(E_FAIL, tr("canceled"));

    /*
     * Try connect to the destination machine, disable Nagle.
     * (Note. The caller cleans up mhSocket, so we can return without worries.)
     */
    int vrc = RTTcpClientConnect(pState->mstrHostname.c_str(), pState->muPort, &pState->mhSocket);
    if (RT_FAILURE(vrc))
        return setErrorBoth(E_FAIL, vrc, tr("Failed to connect to port %u on '%s': %Rrc"),
                            pState->muPort, pState->mstrHostname.c_str(), vrc);
    vrc = RTTcpSetSendCoalescing(pState->mhSocket, false /*fEnable*/);
    AssertRC(vrc);

    /* Read and check the welcome message. */
    char szLine[RT_MAX(128, sizeof(g_szWelcome))];
    RT_ZERO(szLine);
    vrc = RTTcpRead(pState->mhSocket, szLine, sizeof(g_szWelcome) - 1, NULL);
    if (RT_FAILURE(vrc))
        return setErrorBoth(E_FAIL, vrc, tr("Failed to read welcome message: %Rrc"), vrc);
    if (strcmp(szLine, g_szWelcome))
        return setError(E_FAIL, tr("Unexpected welcome %.*Rhxs"), sizeof(g_szWelcome) - 1, szLine);

    /* password */
    pState->mstrPassword.append('\n');
    vrc = RTTcpWrite(pState->mhSocket, pState->mstrPassword.c_str(), pState->mstrPassword.length());
    if (RT_FAILURE(vrc))
        return setErrorBoth(E_FAIL, vrc, tr("Failed to send password: %Rrc"), vrc);

    /* ACK */
    hrc = i_teleporterSrcReadACK(pState, "password", tr("Invalid password"));
    if (FAILED(hrc))
        return hrc;

    /*
     * Start loading the state.
     *
     * Note! The saved state includes vital configuration data which will be
     *       verified against the VM config on the other end.  This is all done
     *       in the first pass, so we should fail pretty promptly on misconfig.
     */
    hrc = i_teleporterSrcSubmitCommand(pState, "load");
    if (FAILED(hrc))
        return hrc;

    RTSocketRetain(pState->mhSocket);
    void *pvUser = static_cast<void *>(static_cast<TeleporterState *>(pState));
    vrc = pState->mpVMM->pfnVMR3Teleport(pState->mpUVM,
                                         pState->mcMsMaxDowntime,
                                         &g_teleporterTcpOps,         pvUser,
                                         teleporterProgressCallback,  pvUser,
                                         &pState->mfSuspendedByUs);
    RTSocketRelease(pState->mhSocket);
    if (RT_FAILURE(vrc))
    {
        if (   vrc == VERR_SSM_CANCELLED
            && RT_SUCCESS(RTTcpSelectOne(pState->mhSocket, 1)))
        {
            hrc = i_teleporterSrcReadACK(pState, "load-complete");
            if (FAILED(hrc))
                return hrc;
        }
        return setErrorBoth(E_FAIL, vrc, "VMR3Teleport -> %Rrc", vrc);
    }

    hrc = i_teleporterSrcReadACK(pState, "load-complete");
    if (FAILED(hrc))
        return hrc;

    /*
     * We're at the point of no return.
     */
    if (FAILED(pState->mptrProgress->NotifyPointOfNoReturn()))
    {
        i_teleporterSrcSubmitCommand(pState, "cancel", false /*fWaitForAck*/);
        return E_FAIL;
    }

    /*
     * Hand over any media which we might be sharing.
     *
     * Note! This is only important on localhost teleportations.
     */
    /** @todo Maybe we should only do this if it's a local teleportation... */
    hrc = mControl->UnlockMedia();
    if (FAILED(hrc))
        return hrc;
    pState->mfUnlockedMedia = true;

    hrc = i_teleporterSrcSubmitCommand(pState, "lock-media");
    if (FAILED(hrc))
        return hrc;

    /*
     * The FINAL step is giving the target instructions how to proceed with the VM.
     */
    if (    vrc == VINF_SSM_LIVE_SUSPENDED
        ||  pState->menmOldMachineState == MachineState_Paused)
        hrc = i_teleporterSrcSubmitCommand(pState, "hand-over-paused");
    else
        hrc = i_teleporterSrcSubmitCommand(pState, "hand-over-resume");
    if (FAILED(hrc))
        return hrc;

    /*
     * teleporterSrcThreadWrapper will do the automatic power off because it
     * has to release the AutoVMCaller.
     */
    return S_OK;
}


/**
 * Static thread method wrapper.
 *
 * @returns VINF_SUCCESS (ignored).
 * @param   hThreadSelf         The thread.
 * @param   pvUser              Pointer to a TeleporterStateSrc instance.
 */
/*static*/ DECLCALLBACK(int)
Console::i_teleporterSrcThreadWrapper(RTTHREAD hThreadSelf, void *pvUser)
{
    RT_NOREF(hThreadSelf);
    TeleporterStateSrc *pState = (TeleporterStateSrc *)pvUser;

    /*
     * Console::teleporterSrc does the work, we just grab onto the VM handle
     * and do the cleanups afterwards.
     */
    SafeVMPtr ptrVM(pState->mptrConsole);
    HRESULT hrc = ptrVM.hrc();

    if (SUCCEEDED(hrc))
        hrc = pState->mptrConsole->i_teleporterSrc(pState);

    /* Close the connection ASAP on so that the other side can complete. */
    if (pState->mhSocket != NIL_RTSOCKET)
    {
        RTTcpClientClose(pState->mhSocket);
        pState->mhSocket = NIL_RTSOCKET;
    }

    /* Aaarg! setMachineState trashes error info on Windows, so we have to
       complete things here on failure instead of right before cleanup. */
    if (FAILED(hrc))
        pState->mptrProgress->i_notifyComplete(hrc);

    /* We can no longer be canceled (success), or it doesn't matter any longer (failure). */
    pState->mptrProgress->i_setCancelCallback(NULL, NULL);

    /*
     * Write lock the console before resetting mptrCancelableProgress and
     * fixing the state.
     */
    AutoWriteLock autoLock(pState->mptrConsole COMMA_LOCKVAL_SRC_POS);
    pState->mptrConsole->mptrCancelableProgress.setNull();

    VMSTATE const        enmVMState      = pState->mpVMM->pfnVMR3GetStateU(pState->mpUVM);
    MachineState_T const enmMachineState = pState->mptrConsole->mMachineState;
    if (SUCCEEDED(hrc))
    {
        /*
         * Automatically shut down the VM on success.
         *
         * Note! We have to release the VM caller object or we'll deadlock in
         *       powerDown.
         */
        AssertLogRelMsg(enmVMState == VMSTATE_SUSPENDED, ("%s\n", pState->mpVMM->pfnVMR3GetStateName(enmVMState)));
        AssertLogRelMsg(enmMachineState == MachineState_TeleportingPausedVM, ("%s\n", ::stringifyMachineState(enmMachineState)));

        ptrVM.release();

        pState->mptrConsole->mVMIsAlreadyPoweringOff = true; /* (Make sure we stick in the TeleportingPausedVM state.) */
        autoLock.release();

        hrc = pState->mptrConsole->i_powerDown();

        autoLock.acquire();
        pState->mptrConsole->mVMIsAlreadyPoweringOff = false;

        pState->mptrProgress->i_notifyComplete(hrc);
    }
    else
    {
        /*
         * Work the state machinery on failure.
         *
         * If the state is no longer 'Teleporting*', some other operation has
         * canceled us and there is nothing we need to do here.  In all other
         * cases, we've failed one way or another.
         */
        if (   enmMachineState == MachineState_Teleporting
            || enmMachineState == MachineState_TeleportingPausedVM
           )
        {
            if (pState->mfUnlockedMedia)
            {
                ErrorInfoKeeper Oak;
                HRESULT hrc2 = pState->mptrConsole->mControl->LockMedia();
                if (FAILED(hrc2))
                {
                    uint64_t StartMS = RTTimeMilliTS();
                    do
                    {
                        RTThreadSleep(2);
                        hrc2 = pState->mptrConsole->mControl->LockMedia();
                    } while (   FAILED(hrc2)
                             && RTTimeMilliTS() - StartMS < 2000);
                }
                if (SUCCEEDED(hrc2))
                    pState->mfUnlockedMedia = true;
                else
                    LogRel(("FATAL ERROR: Failed to re-take the media locks. hrc2=%Rhrc\n", hrc2));
            }

            switch (enmVMState)
            {
                case VMSTATE_RUNNING:
                case VMSTATE_RUNNING_LS:
                case VMSTATE_DEBUGGING:
                case VMSTATE_DEBUGGING_LS:
                case VMSTATE_POWERING_OFF:
                case VMSTATE_POWERING_OFF_LS:
                case VMSTATE_RESETTING:
                case VMSTATE_RESETTING_LS:
                case VMSTATE_SOFT_RESETTING:
                case VMSTATE_SOFT_RESETTING_LS:
                    Assert(!pState->mfSuspendedByUs);
                    Assert(!pState->mfUnlockedMedia);
                    pState->mptrConsole->i_setMachineState(MachineState_Running);
                    break;

                case VMSTATE_GURU_MEDITATION:
                case VMSTATE_GURU_MEDITATION_LS:
                    pState->mptrConsole->i_setMachineState(MachineState_Stuck);
                    break;

                case VMSTATE_FATAL_ERROR:
                case VMSTATE_FATAL_ERROR_LS:
                    pState->mptrConsole->i_setMachineState(MachineState_Paused);
                    break;

                default:
                    AssertMsgFailed(("%s\n", pState->mpVMM->pfnVMR3GetStateName(enmVMState)));
                    RT_FALL_THRU();
                case VMSTATE_SUSPENDED:
                case VMSTATE_SUSPENDED_LS:
                case VMSTATE_SUSPENDING:
                case VMSTATE_SUSPENDING_LS:
                case VMSTATE_SUSPENDING_EXT_LS:
                    if (!pState->mfUnlockedMedia)
                    {
                        pState->mptrConsole->i_setMachineState(MachineState_Paused);
                        if (pState->mfSuspendedByUs)
                        {
                            autoLock.release();
                            int vrc = pState->mpVMM->pfnVMR3Resume(pState->mpUVM, VMRESUMEREASON_TELEPORT_FAILED);
                            AssertLogRelMsgRC(vrc, ("VMR3Resume -> %Rrc\n", vrc));
                            autoLock.acquire();
                        }
                    }
                    else
                    {
                        /* Faking a guru meditation is the best I can think of doing here... */
                        pState->mptrConsole->i_setMachineState(MachineState_Stuck);
                    }
                    break;
            }
        }
    }
    autoLock.release();

    /*
     * Cleanup.
     */
    Assert(pState->mhSocket == NIL_RTSOCKET);
    delete pState;

    return VINF_SUCCESS; /* ignored */
}


/**
 * Start teleporter to the specified target.
 *
 * @returns COM status code.
 *
 * @param   aHostname       The name of the target host.
 * @param   aTcpport        The TCP port number.
 * @param   aPassword       The password.
 * @param   aMaxDowntime    Max allowed "downtime" in milliseconds.
 * @param   aProgress       Where to return the progress object.
 */
HRESULT Console::teleport(const com::Utf8Str &aHostname, ULONG aTcpport, const com::Utf8Str &aPassword,
                          ULONG aMaxDowntime, ComPtr<IProgress> &aProgress)
{
    /*
     * Validate parameters, check+hold object status, write lock the object
     * and validate the state.
     */
    Utf8Str strPassword(aPassword);
    if (!strPassword.isEmpty())
    {
        if (VBoxIsPasswordHashed(&strPassword))
            return setError(E_INVALIDARG, tr("The specified password resembles a hashed password, expected plain text"));
        VBoxHashPassword(&strPassword);
    }

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoWriteLock autoLock(this COMMA_LOCKVAL_SRC_POS);
    LogFlowThisFunc(("mMachineState=%d\n", mMachineState));

    switch (mMachineState)
    {
        case MachineState_Running:
        case MachineState_Paused:
            break;

        default:
            return setError(VBOX_E_INVALID_VM_STATE, tr("Invalid machine state: %s (must be Running or Paused)"),
                            Global::stringifyMachineState(mMachineState));
    }


    /*
     * Create a progress object, spawn a worker thread and change the state.
     * Note! The thread won't start working until we release the lock.
     */
    LogFlowThisFunc(("Initiating TELEPORT request...\n"));

    ComObjPtr<Progress> ptrProgress;
    HRESULT hrc = ptrProgress.createObject();
    if (SUCCEEDED(hrc))
        hrc = ptrProgress->init(static_cast<IConsole *>(this),
                                Bstr(tr("Teleporter")).raw(),
                                TRUE /*aCancelable*/);
    if (FAILED(hrc))
        return hrc;

    TeleporterStateSrc *pState = new TeleporterStateSrc(this, mpUVM, mpVMM, ptrProgress, mMachineState);
    pState->mstrPassword    = strPassword;
    pState->mstrHostname    = aHostname;
    pState->muPort          = aTcpport;
    pState->mcMsMaxDowntime = aMaxDowntime;

    void *pvUser = static_cast<void *>(static_cast<TeleporterState *>(pState));
    ptrProgress->i_setCancelCallback(teleporterProgressCancelCallback, pvUser);

    int vrc = RTThreadCreate(NULL, Console::i_teleporterSrcThreadWrapper, (void *)pState, 0 /*cbStack*/,
                             RTTHREADTYPE_EMULATION, 0 /*fFlags*/, "Teleport");
    if (RT_SUCCESS(vrc))
    {
        if (mMachineState == MachineState_Running)
            hrc = i_setMachineState(MachineState_Teleporting);
        else
            hrc = i_setMachineState(MachineState_TeleportingPausedVM);
        if (SUCCEEDED(hrc))
        {
            ptrProgress.queryInterfaceTo(aProgress.asOutParam());
            mptrCancelableProgress = aProgress;
        }
        else
            ptrProgress->Cancel();
    }
    else
    {
        ptrProgress->i_setCancelCallback(NULL, NULL);
        delete pState;
        hrc = setErrorBoth(E_FAIL, vrc, "RTThreadCreate -> %Rrc", vrc);
    }

    return hrc;
}


/**
 * Creates a TCP server that listens for the source machine and passes control
 * over to Console::teleporterTrgServeConnection().
 *
 * @returns VBox status code.
 * @param   pUVM                The user-mode VM handle
 * @param   pVMM                The VMM vtable.
 * @param   pMachine            The IMachine for the virtual machine.
 * @param   pErrorMsg           Pointer to the error string for VMSetError.
 * @param   fStartPaused        Whether to start it in the Paused (true) or
 *                              Running (false) state,
 * @param   pProgress           Pointer to the progress object.
 * @param   pfPowerOffOnFailure Whether the caller should power off
 *                              the VM on failure.
 *
 * @remarks The caller expects error information to be set on failure.
 * @todo    Check that all the possible failure paths sets error info...
 */
HRESULT Console::i_teleporterTrg(PUVM pUVM, PCVMMR3VTABLE pVMM, IMachine *pMachine, Utf8Str *pErrorMsg, bool fStartPaused,
                                 Progress *pProgress, bool *pfPowerOffOnFailure)
{
    LogThisFunc(("pUVM=%p pVMM=%p pMachine=%p fStartPaused=%RTbool pProgress=%p\n", pUVM, pVMM, pMachine, fStartPaused, pProgress));

    *pfPowerOffOnFailure = true;

    /*
     * Get the config.
     */
    ULONG uPort;
    HRESULT hrc = pMachine->COMGETTER(TeleporterPort)(&uPort);
    if (FAILED(hrc))
        return hrc;
    ULONG const uPortOrg = uPort;

    Bstr bstrAddress;
    hrc = pMachine->COMGETTER(TeleporterAddress)(bstrAddress.asOutParam());
    if (FAILED(hrc))
        return hrc;
    Utf8Str strAddress(bstrAddress);
    const char *pszAddress = strAddress.isEmpty() ? NULL : strAddress.c_str();

    Bstr bstrPassword;
    hrc = pMachine->COMGETTER(TeleporterPassword)(bstrPassword.asOutParam());
    if (FAILED(hrc))
        return hrc;
    Utf8Str strPassword(bstrPassword);
    strPassword.append('\n');           /* To simplify password checking. */

    /*
     * Create the TCP server.
     */
    int vrc = VINF_SUCCESS; /* Shut up MSC */
    PRTTCPSERVER hServer = NULL; /* ditto */
    if (uPort)
        vrc = RTTcpServerCreateEx(pszAddress, uPort, &hServer);
    else
    {
        for (int cTries = 10240; cTries > 0; cTries--)
        {
            uPort = RTRandU32Ex(cTries >= 8192 ? 49152 : 1024, 65534);
            vrc = RTTcpServerCreateEx(pszAddress, uPort, &hServer);
            if (vrc != VERR_NET_ADDRESS_IN_USE)
                break;
        }
        if (RT_SUCCESS(vrc))
        {
            hrc = pMachine->COMSETTER(TeleporterPort)(uPort);
            if (FAILED(hrc))
            {
                RTTcpServerDestroy(hServer);
                return hrc;
            }
        }
    }
    if (RT_FAILURE(vrc))
        return setErrorBoth(E_FAIL, vrc, tr("RTTcpServerCreateEx failed with status %Rrc"), vrc);

    /*
     * Create a one-shot timer for timing out after 5 mins.
     */
    RTTIMERLR hTimerLR;
    vrc = RTTimerLRCreateEx(&hTimerLR, 0 /*ns*/, RTTIMER_FLAGS_CPU_ANY, teleporterDstTimeout, hServer);
    if (RT_SUCCESS(vrc))
    {
        vrc = RTTimerLRStart(hTimerLR, 5*60*UINT64_C(1000000000) /*ns*/);
        if (RT_SUCCESS(vrc))
        {
            /*
             * Do the job, when it returns we're done.
             */
            TeleporterStateTrg theState(this, pUVM, pVMM, pProgress, pMachine, mControl, &hTimerLR, fStartPaused);
            theState.mstrPassword      = strPassword;
            theState.mhServer          = hServer;

            void *pvUser = static_cast<void *>(static_cast<TeleporterState *>(&theState));
            if (pProgress->i_setCancelCallback(teleporterProgressCancelCallback, pvUser))
            {
                LogRel(("Teleporter: Waiting for incoming VM...\n"));
                hrc = pProgress->SetNextOperation(Bstr(tr("Waiting for incoming VM")).raw(), 1);
                if (SUCCEEDED(hrc))
                {
                    vrc = RTTcpServerListen(hServer, Console::i_teleporterTrgServeConnection, &theState);
                    pProgress->i_setCancelCallback(NULL, NULL);

                    if (vrc == VERR_TCP_SERVER_STOP)
                    {
                        vrc = theState.mRc;
                        /* Power off the VM on failure unless the state callback
                           already did that. */
                        *pfPowerOffOnFailure = false;
                        if (RT_SUCCESS(vrc))
                            hrc = S_OK;
                        else
                        {
                            VMSTATE enmVMState = pVMM->pfnVMR3GetStateU(pUVM);
                            if (    enmVMState != VMSTATE_OFF
                                &&  enmVMState != VMSTATE_POWERING_OFF)
                                *pfPowerOffOnFailure = true;

                            /* Set error. */
                            if (pErrorMsg->length())
                                hrc = setError(E_FAIL, "%s", pErrorMsg->c_str());
                            else
                                hrc = setError(E_FAIL, tr("Teleporation failed (%Rrc)"), vrc);
                        }
                    }
                    else if (vrc == VERR_TCP_SERVER_SHUTDOWN)
                    {
                        BOOL fCanceled = TRUE;
                        hrc = pProgress->COMGETTER(Canceled)(&fCanceled);
                        if (FAILED(hrc) || fCanceled)
                            hrc = setError(E_FAIL, tr("Teleporting canceled"));
                        else
                            hrc = setError(E_FAIL, tr("Teleporter timed out waiting for incoming connection"));
                        LogRel(("Teleporter: RTTcpServerListen aborted - %Rrc\n", vrc));
                    }
                    else
                    {
                        hrc = setErrorBoth(E_FAIL, vrc, tr("Unexpected RTTcpServerListen status code %Rrc"), vrc);
                        LogRel(("Teleporter: Unexpected RTTcpServerListen vrc: %Rrc\n", vrc));
                    }
                }
                else
                    LogThisFunc(("SetNextOperation failed, %Rhrc\n", hrc));
            }
            else
            {
                LogThisFunc(("Canceled - check point #1\n"));
                hrc = setError(E_FAIL, tr("Teleporting canceled"));
            }
        }
        else
            hrc = setErrorBoth(E_FAIL, vrc, "RTTimerLRStart -> %Rrc", vrc);

        RTTimerLRDestroy(hTimerLR);
    }
    else
        hrc = setErrorBoth(E_FAIL, vrc, "RTTimerLRCreate -> %Rrc", vrc);
    RTTcpServerDestroy(hServer);

    /*
     * If we change TeleporterPort above, set it back to it's original
     * value before returning.
     */
    if (uPortOrg != uPort)
    {
        ErrorInfoKeeper Eik;
        pMachine->COMSETTER(TeleporterPort)(uPortOrg);
    }

    return hrc;
}


/**
 * Unlock the media.
 *
 * This is used in error paths.
 *
 * @param  pState           The teleporter state.
 */
static void teleporterTrgUnlockMedia(TeleporterStateTrg *pState)
{
    if (pState->mfLockedMedia)
    {
        pState->mpControl->UnlockMedia();
        pState->mfLockedMedia = false;
    }
}


static int teleporterTcpWriteACK(TeleporterStateTrg *pState, bool fAutomaticUnlock = true)
{
    int vrc = RTTcpWrite(pState->mhSocket, "ACK\n", sizeof("ACK\n") - 1);
    if (RT_FAILURE(vrc))
    {
        LogRel(("Teleporter: RTTcpWrite(,ACK,) -> %Rrc\n", vrc));
        if (fAutomaticUnlock)
            teleporterTrgUnlockMedia(pState);
    }
    return vrc;
}


static int teleporterTcpWriteNACK(TeleporterStateTrg *pState, int32_t rc2, const char *pszMsgText = NULL)
{
    /*
     * Unlock media sending the NACK. That way the other doesn't have to spin
     * waiting to regain the locks.
     */
    teleporterTrgUnlockMedia(pState);

    char    szMsg[256];
    size_t  cch;
    if (pszMsgText && *pszMsgText)
    {
        cch = RTStrPrintf(szMsg, sizeof(szMsg), "NACK=%d;%s\n", rc2, pszMsgText);
        for (size_t off = 6; off + 1 < cch; off++)
            if (szMsg[off] == '\n')
                szMsg[off] = '\r';
    }
    else
        cch = RTStrPrintf(szMsg, sizeof(szMsg), "NACK=%d\n", rc2);
    int vrc = RTTcpWrite(pState->mhSocket, szMsg, cch);
    if (RT_FAILURE(vrc))
        LogRel(("Teleporter: RTTcpWrite(,%s,%zu) -> %Rrc\n", szMsg, cch, vrc));
    return vrc;
}


/**
 * @copydoc FNRTTCPSERVE
 *
 * @returns VINF_SUCCESS or VERR_TCP_SERVER_STOP.
 */
/*static*/ DECLCALLBACK(int)
Console::i_teleporterTrgServeConnection(RTSOCKET hSocket, void *pvUser)
{
    TeleporterStateTrg *pState = (TeleporterStateTrg *)pvUser;
    pState->mhSocket = hSocket;

    /*
     * Disable Nagle and say hello.
     */
    int vrc = RTTcpSetSendCoalescing(pState->mhSocket, false /*fEnable*/);
    AssertRC(vrc);
    vrc = RTTcpWrite(hSocket, g_szWelcome, sizeof(g_szWelcome) - 1);
    if (RT_FAILURE(vrc))
    {
        LogRel(("Teleporter: Failed to write welcome message: %Rrc\n", vrc));
        return VINF_SUCCESS;
    }

    /*
     * Password (includes '\n', see i_teleporterTrg).
     */
    const char *pszPassword = pState->mstrPassword.c_str();
    unsigned    off = 0;
    while (pszPassword[off])
    {
        char ch;
        vrc = RTTcpRead(hSocket, &ch, sizeof(ch), NULL);
        if (    RT_FAILURE(vrc)
            ||  pszPassword[off] != ch)
        {
            if (RT_FAILURE(vrc))
                LogRel(("Teleporter: Password read failure (off=%u): %Rrc\n", off, vrc));
            else
            {
                /* Must read the whole password before NACK'ing it. */
                size_t const cchMaxRead = RT_ALIGN_Z(pState->mstrPassword.length() * 3, _1K);
                while (off < cchMaxRead && RT_SUCCESS(vrc) && ch != '\n')
                {
                    vrc = RTTcpRead(hSocket, &ch, sizeof(ch), NULL);
                    off++;
                }
                LogRel(("Teleporter: Invalid password\n"));
            }
            RTThreadSleep(RTRandU32Ex(64, 1024)); /* stagger retries */
            teleporterTcpWriteNACK(pState, VERR_AUTHENTICATION_FAILURE);
            return VINF_SUCCESS;
        }
        off++;
    }
    vrc = teleporterTcpWriteACK(pState);
    if (RT_FAILURE(vrc))
        return VINF_SUCCESS;

    /*
     * Update the progress bar, with peer name if available.
     */
    HRESULT     hrc;
    RTNETADDR   Addr;
    vrc = RTTcpGetPeerAddress(hSocket, &Addr);
    if (RT_SUCCESS(vrc))
    {
        LogRel(("Teleporter: Incoming VM from %RTnaddr!\n", &Addr));
        hrc = pState->mptrProgress->SetNextOperation(BstrFmt(tr("Teleporting VM from %RTnaddr"), &Addr).raw(), 8);
    }
    else
    {
        LogRel(("Teleporter: Incoming VM!\n"));
        hrc = pState->mptrProgress->SetNextOperation(Bstr(tr("Teleporting VM")).raw(), 8);
    }
    AssertMsg(SUCCEEDED(hrc) || hrc == E_FAIL, ("%Rhrc\n", hrc));

    /*
     * Stop the server and cancel the timeout timer.
     *
     * Note! After this point we must return VERR_TCP_SERVER_STOP, while prior
     *       to it we must not return that value!
     */
    RTTcpServerShutdown(pState->mhServer);
    RTTimerLRDestroy(*pState->mphTimerLR);
    *pState->mphTimerLR = NIL_RTTIMERLR;

    /*
     * Command processing loop.
     */
    bool fDone = false;
    for (;;)
    {
        char szCmd[128];
        vrc = teleporterTcpReadLine(pState, szCmd, sizeof(szCmd));
        if (RT_FAILURE(vrc))
            break;

        if (!strcmp(szCmd, "load"))
        {
            vrc = teleporterTcpWriteACK(pState);
            if (RT_FAILURE(vrc))
                break;

            int vrc2 = pState->mpVMM->pfnVMR3AtErrorRegister(pState->mpUVM, Console::i_genericVMSetErrorCallback,
                                                             &pState->mErrorText);
            AssertRC(vrc2);
            RTSocketRetain(pState->mhSocket); /* For concurrent access by I/O thread and EMT. */
            pState->moffStream = 0;

            void *pvUser2 = static_cast<void *>(static_cast<TeleporterState *>(pState));
            vrc = pState->mpVMM->pfnVMR3LoadFromStream(pState->mpUVM,
                                                       &g_teleporterTcpOps, pvUser2,
                                                       teleporterProgressCallback, pvUser2,
                                                       true /*fTeleporting*/);

            RTSocketRelease(pState->mhSocket);
            vrc2 = pState->mpVMM->pfnVMR3AtErrorDeregister(pState->mpUVM, Console::i_genericVMSetErrorCallback, &pState->mErrorText);
            AssertRC(vrc2);

            if (RT_FAILURE(vrc))
            {
                LogRel(("Teleporter: VMR3LoadFromStream -> %Rrc\n", vrc));
                teleporterTcpWriteNACK(pState, vrc, pState->mErrorText.c_str());
                break;
            }

            /* The EOS might not have been read, make sure it is. */
            pState->mfStopReading = false;
            size_t cbRead;
            vrc = teleporterTcpOpRead(pvUser2, pState->moffStream, szCmd, 1, &cbRead);
            if (vrc != VERR_EOF)
            {
                LogRel(("Teleporter: Draining teleporterTcpOpRead -> %Rrc\n", vrc));
                teleporterTcpWriteNACK(pState, vrc);
                break;
            }

            vrc = teleporterTcpWriteACK(pState);
        }
        else if (!strcmp(szCmd, "cancel"))
        {
            /* Don't ACK this. */
            LogRel(("Teleporter: Received cancel command.\n"));
            vrc = VERR_SSM_CANCELLED;
        }
        else if (!strcmp(szCmd, "lock-media"))
        {
            hrc = pState->mpControl->LockMedia();
            if (SUCCEEDED(hrc))
            {
                pState->mfLockedMedia = true;
                vrc = teleporterTcpWriteACK(pState);
            }
            else
            {
                vrc = VERR_FILE_LOCK_FAILED;
                teleporterTcpWriteNACK(pState, vrc);
            }
        }
        else if (   !strcmp(szCmd, "hand-over-resume")
                 || !strcmp(szCmd, "hand-over-paused"))
        {
            /*
             * Point of no return.
             *
             * Note! Since we cannot tell whether a VMR3Resume failure is
             *       destructive for the source or not, we have little choice
             *       but to ACK it first and take any failures locally.
             *
             *       Ideally, we should try resume it first and then ACK (or
             *       NACK) the request since this would reduce latency and
             *       make it possible to recover from some VMR3Resume failures.
             */
            if (   SUCCEEDED(pState->mptrProgress->NotifyPointOfNoReturn())
                && pState->mfLockedMedia)
            {
                vrc = teleporterTcpWriteACK(pState);
                if (RT_SUCCESS(vrc))
                {
                    if (!strcmp(szCmd, "hand-over-resume"))
                        vrc = pState->mpVMM->pfnVMR3Resume(pState->mpUVM, VMRESUMEREASON_TELEPORTED);
                    else
                        pState->mptrConsole->i_setMachineState(MachineState_Paused);
                    fDone = true;
                    break;
                }
            }
            else
            {
                vrc = pState->mfLockedMedia ? VERR_WRONG_ORDER : VERR_SSM_CANCELLED;
                teleporterTcpWriteNACK(pState, vrc);
            }
        }
        else
        {
            LogRel(("Teleporter: Unknown command '%s' (%.*Rhxs)\n", szCmd, strlen(szCmd), szCmd));
            vrc = VERR_NOT_IMPLEMENTED;
            teleporterTcpWriteNACK(pState, vrc);
        }

        if (RT_FAILURE(vrc))
            break;
    }

    if (RT_SUCCESS(vrc) && !fDone)
        vrc = VERR_WRONG_ORDER;
    if (RT_FAILURE(vrc))
        teleporterTrgUnlockMedia(pState);

    pState->mRc = vrc;
    pState->mhSocket = NIL_RTSOCKET;
    LogFlowFunc(("returns mRc=%Rrc\n", vrc));
    return VERR_TCP_SERVER_STOP;
}

