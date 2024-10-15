/* $Id: VBoxManageSnapshot.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxManage - The 'snapshot' command.
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
#include <VBox/com/array.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>

#include <VBox/com/VirtualBox.h>

#include <iprt/getopt.h>
#include <iprt/stream.h>
#include <iprt/time.h>

#include "VBoxManage.h"
using namespace com;

DECLARE_TRANSLATION_CONTEXT(Snapshot);

/**
 * Helper function used with "VBoxManage snapshot ... dump". Gets called to find the
 * snapshot in the machine's snapshot tree that uses a particular diff image child of
 * a medium.
 * Horribly inefficient since we keep re-querying the snapshots tree for each image,
 * but this is for quick debugging only.
 * @param pMedium
 * @param pThisSnapshot
 * @param pCurrentSnapshot
 * @param uMediumLevel
 * @param uSnapshotLevel
 * @return
 */
static bool FindAndPrintSnapshotUsingMedium(ComPtr<IMedium> &pMedium,
                                            ComPtr<ISnapshot> &pThisSnapshot,
                                            ComPtr<ISnapshot> &pCurrentSnapshot,
                                            uint32_t uMediumLevel,
                                            uint32_t uSnapshotLevel)
{
    HRESULT hrc;

    do
    {
        // get snapshot machine so we can figure out which diff image this created
        ComPtr<IMachine> pSnapshotMachine;
        CHECK_ERROR_BREAK(pThisSnapshot, COMGETTER(Machine)(pSnapshotMachine.asOutParam()));

        // get media attachments
        SafeIfaceArray<IMediumAttachment> aAttachments;
        CHECK_ERROR_BREAK(pSnapshotMachine, COMGETTER(MediumAttachments)(ComSafeArrayAsOutParam(aAttachments)));

        for (uint32_t i = 0;
             i < aAttachments.size();
             ++i)
        {
            ComPtr<IMediumAttachment> pAttach(aAttachments[i]);
            DeviceType_T type;
            CHECK_ERROR_BREAK(pAttach, COMGETTER(Type)(&type));
            if (type == DeviceType_HardDisk)
            {
                ComPtr<IMedium> pMediumInSnapshot;
                CHECK_ERROR_BREAK(pAttach, COMGETTER(Medium)(pMediumInSnapshot.asOutParam()));

                if (pMediumInSnapshot == pMedium)
                {
                    // get snapshot name
                    Bstr bstrSnapshotName;
                    CHECK_ERROR_BREAK(pThisSnapshot, COMGETTER(Name)(bstrSnapshotName.asOutParam()));

                    RTPrintf("%*s  \"%ls\"%s\n",
                             50 + uSnapshotLevel * 2, "",            // indent
                             bstrSnapshotName.raw(),
                             (pThisSnapshot == pCurrentSnapshot) ? " (CURSNAP)" : "");
                    return true;        // found
                }
            }
        }

        // not found: then recurse into child snapshots
        SafeIfaceArray<ISnapshot> aSnapshots;
        CHECK_ERROR_BREAK(pThisSnapshot, COMGETTER(Children)(ComSafeArrayAsOutParam(aSnapshots)));

        for (uint32_t i = 0;
            i < aSnapshots.size();
            ++i)
        {
            ComPtr<ISnapshot> pChild(aSnapshots[i]);
            if (FindAndPrintSnapshotUsingMedium(pMedium,
                                                pChild,
                                                pCurrentSnapshot,
                                                uMediumLevel,
                                                uSnapshotLevel + 1))
                // found:
                break;
        }
    } while (0);

    return false;
}

/**
 * Helper function used with "VBoxManage snapshot ... dump". Called from DumpSnapshot()
 * for each hard disk attachment found in a virtual machine. This then writes out the
 * root (base) medium for that hard disk attachment and recurses into the children
 * tree of that medium, correlating it with the snapshots of the machine.
 * @param pCurrentStateMedium constant, the medium listed in the current machine data (latest diff image).
 * @param pMedium variant, initially the base medium, then a child of the base medium when recursing.
 * @param pRootSnapshot constant, the root snapshot of the machine, if any; this then looks into the child snapshots.
 * @param pCurrentSnapshot constant, the machine's current snapshot (so we can mark it in the output).
 * @param uLevel variant, the recursion level for output indentation.
 */
static void DumpMediumWithChildren(ComPtr<IMedium> &pCurrentStateMedium,
                                   ComPtr<IMedium> &pMedium,
                                   ComPtr<ISnapshot> &pRootSnapshot,
                                   ComPtr<ISnapshot> &pCurrentSnapshot,
                                   uint32_t uLevel)
{
    HRESULT hrc;
    do
    {
        // print this medium
        Bstr bstrMediumName;
        CHECK_ERROR_BREAK(pMedium, COMGETTER(Name)(bstrMediumName.asOutParam()));
        RTPrintf("%*s  \"%ls\"%s\n",
                 uLevel * 2, "",            // indent
                 bstrMediumName.raw(),
                 (pCurrentStateMedium == pMedium) ? " (CURSTATE)" : "");

        // find and print the snapshot that uses this particular medium (diff image)
        FindAndPrintSnapshotUsingMedium(pMedium, pRootSnapshot, pCurrentSnapshot, uLevel, 0);

        // recurse into children
        SafeIfaceArray<IMedium> aChildren;
        CHECK_ERROR_BREAK(pMedium, COMGETTER(Children)(ComSafeArrayAsOutParam(aChildren)));
        for (uint32_t i = 0;
             i < aChildren.size();
             ++i)
        {
            ComPtr<IMedium> pChild(aChildren[i]);
            DumpMediumWithChildren(pCurrentStateMedium, pChild, pRootSnapshot, pCurrentSnapshot, uLevel + 1);
        }
    } while (0);
}


/**
 * Handles the 'snapshot myvm list' sub-command.
 * @returns Exit code.
 * @param   pArgs           The handler argument package.
 * @param   pMachine        Reference to the VM (locked) we're operating on.
 */
static RTEXITCODE handleSnapshotList(HandlerArg *pArgs, ComPtr<IMachine> &pMachine)
{
    static const RTGETOPTDEF g_aOptions[] =
    {
        { "--details",          'D', RTGETOPT_REQ_NOTHING },
        { "--machinereadable",  'M', RTGETOPT_REQ_NOTHING },
    };

    VMINFO_DETAILS enmDetails = VMINFO_STANDARD;

    int c;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, pArgs->argc, pArgs->argv, g_aOptions, RT_ELEMENTS(g_aOptions), 2 /*iArg*/, 0 /*fFlags*/);
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 'D':   enmDetails = VMINFO_FULL; break;
            case 'M':   enmDetails = VMINFO_MACHINEREADABLE; break;
            default:    return errorGetOpt(c, &ValueUnion);
        }
    }

    ComPtr<ISnapshot> pSnapshot;
    HRESULT hrc = pMachine->FindSnapshot(Bstr().raw(), pSnapshot.asOutParam());
    if (FAILED(hrc))
    {
        RTPrintf(Snapshot::tr("This machine does not have any snapshots\n"));
        return RTEXITCODE_FAILURE;
    }
    if (pSnapshot)
    {
        ComPtr<ISnapshot> pCurrentSnapshot;
        CHECK_ERROR2I_RET(pMachine, COMGETTER(CurrentSnapshot)(pCurrentSnapshot.asOutParam()), RTEXITCODE_FAILURE);
        hrc = showSnapshots(pSnapshot, pCurrentSnapshot, enmDetails);
        if (FAILED(hrc))
            return RTEXITCODE_FAILURE;
    }
    return RTEXITCODE_SUCCESS;
}

/**
 * Implementation for "VBoxManage snapshot ... dump". This goes thru the machine's
 * medium attachments and calls DumpMediumWithChildren() for each hard disk medium found,
 * which then dumps the parent/child tree of that medium together with the corresponding
 * snapshots.
 * @param pMachine Machine to dump snapshots for.
 */
static void DumpSnapshot(ComPtr<IMachine> &pMachine)
{
    HRESULT hrc;

    do
    {
        // get root snapshot
        ComPtr<ISnapshot> pSnapshot;
        CHECK_ERROR_BREAK(pMachine, FindSnapshot(Bstr("").raw(), pSnapshot.asOutParam()));

        // get current snapshot
        ComPtr<ISnapshot> pCurrentSnapshot;
        CHECK_ERROR_BREAK(pMachine, COMGETTER(CurrentSnapshot)(pCurrentSnapshot.asOutParam()));

        // get media attachments
        SafeIfaceArray<IMediumAttachment> aAttachments;
        CHECK_ERROR_BREAK(pMachine, COMGETTER(MediumAttachments)(ComSafeArrayAsOutParam(aAttachments)));
        for (uint32_t i = 0;
             i < aAttachments.size();
             ++i)
        {
            ComPtr<IMediumAttachment> pAttach(aAttachments[i]);
            DeviceType_T type;
            CHECK_ERROR_BREAK(pAttach, COMGETTER(Type)(&type));
            if (type == DeviceType_HardDisk)
            {
                ComPtr<IMedium> pCurrentStateMedium;
                CHECK_ERROR_BREAK(pAttach, COMGETTER(Medium)(pCurrentStateMedium.asOutParam()));

                ComPtr<IMedium> pBaseMedium;
                CHECK_ERROR_BREAK(pCurrentStateMedium, COMGETTER(Base)(pBaseMedium.asOutParam()));

                Bstr bstrBaseMediumName;
                CHECK_ERROR_BREAK(pBaseMedium, COMGETTER(Name)(bstrBaseMediumName.asOutParam()));

                RTPrintf(Snapshot::tr("[%RI32] Images and snapshots for medium \"%ls\"\n"), i, bstrBaseMediumName.raw());

                DumpMediumWithChildren(pCurrentStateMedium,
                                       pBaseMedium,
                                       pSnapshot,
                                       pCurrentSnapshot,
                                       0);
            }
        }
    } while (0);
}

typedef enum SnapshotUniqueFlags
{
    SnapshotUniqueFlags_Null = 0,
    SnapshotUniqueFlags_Number = RT_BIT(1),
    SnapshotUniqueFlags_Timestamp = RT_BIT(2),
    SnapshotUniqueFlags_Space = RT_BIT(16),
    SnapshotUniqueFlags_Force = RT_BIT(30)
} SnapshotUniqueFlags;

static int parseSnapshotUniqueFlags(const char *psz, SnapshotUniqueFlags *pUnique)
{
    int vrc = VINF_SUCCESS;
    unsigned uUnique = 0;
    while (psz && *psz && RT_SUCCESS(vrc))
    {
        size_t len;
        const char *pszComma = strchr(psz, ',');
        if (pszComma)
            len = pszComma - psz;
        else
            len = strlen(psz);
        if (len > 0)
        {
            if (!RTStrNICmp(psz, "number", len))
                uUnique |= SnapshotUniqueFlags_Number;
            else if (!RTStrNICmp(psz, "timestamp", len))
                uUnique |= SnapshotUniqueFlags_Timestamp;
            else if (!RTStrNICmp(psz, "space", len))
                uUnique |= SnapshotUniqueFlags_Space;
            else if (!RTStrNICmp(psz, "force", len))
                uUnique |= SnapshotUniqueFlags_Force;
            else
                vrc = VERR_PARSE_ERROR;
        }
        if (pszComma)
            psz += len + 1;
        else
            psz += len;
    }

    if (RT_SUCCESS(vrc))
        *pUnique = (SnapshotUniqueFlags)uUnique;
    return vrc;
}

/**
 * Implementation for all VBoxManage snapshot ... subcommands.
 * @param a
 * @return
 */
RTEXITCODE handleSnapshot(HandlerArg *a)
{
    HRESULT hrc;

/** @todo r=bird: sub-standard command line parsing here!
 *
 * 'VBoxManage snapshot empty take --help' takes a snapshot rather than display
 * help as you would expect.
 *
 */

    /* we need at least a VM and a command */
    if (a->argc < 2)
        return errorSyntax(Snapshot::tr("Not enough parameters"));

    /* the first argument must be the VM */
    Bstr bstrMachine(a->argv[0]);
    ComPtr<IMachine> pMachine;
    CHECK_ERROR(a->virtualBox, FindMachine(bstrMachine.raw(),
                                           pMachine.asOutParam()));
    if (!pMachine)
        return RTEXITCODE_FAILURE;

    /* we have to open a session for this task (new or shared) */
    CHECK_ERROR_RET(pMachine, LockMachine(a->session, LockType_Shared), RTEXITCODE_FAILURE);
    do
    {
        /* replace the (read-only) IMachine object by a writable one */
        ComPtr<IMachine> sessionMachine;
        CHECK_ERROR_BREAK(a->session, COMGETTER(Machine)(sessionMachine.asOutParam()));

        /* switch based on the command */
        bool fDelete = false,
             fRestore = false,
             fRestoreCurrent = false;

        if (!strcmp(a->argv[1], "take"))
        {
            setCurrentSubcommand(HELP_SCOPE_SNAPSHOT_TAKE);

            /* there must be a name */
            if (a->argc < 3)
            {
                errorSyntax(Snapshot::tr("Missing snapshot name"));
                hrc = E_FAIL;
                break;
            }
            Bstr name(a->argv[2]);

            /* parse the optional arguments */
            Bstr desc;
            bool fPause = true; /* default is NO live snapshot */
            SnapshotUniqueFlags enmUnique = SnapshotUniqueFlags_Null;
            static const RTGETOPTDEF s_aTakeOptions[] =
            {
                { "--description", 'd', RTGETOPT_REQ_STRING },
                { "-description",  'd', RTGETOPT_REQ_STRING },
                { "-desc",         'd', RTGETOPT_REQ_STRING },
                { "--pause",       'p', RTGETOPT_REQ_NOTHING },
                { "--live",        'l', RTGETOPT_REQ_NOTHING },
                { "--uniquename",  'u', RTGETOPT_REQ_STRING }
            };
            RTGETOPTSTATE GetOptState;
            RTGetOptInit(&GetOptState, a->argc, a->argv, s_aTakeOptions, RT_ELEMENTS(s_aTakeOptions),
                         3, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
            int ch;
            RTGETOPTUNION Value;
            int vrc;
            while (   SUCCEEDED(hrc)
                   && (ch = RTGetOpt(&GetOptState, &Value)))
            {
                switch (ch)
                {
                    case 'p':
                        fPause = true;
                        break;

                    case 'l':
                        fPause = false;
                        break;

                    case 'd':
                        desc = Value.psz;
                        break;

                    case 'u':
                        vrc = parseSnapshotUniqueFlags(Value.psz, &enmUnique);
                        if (RT_FAILURE(vrc))
                            return errorArgument(Snapshot::tr("Invalid unique name description '%s'"), Value.psz);
                        break;

                    default:
                        errorGetOpt(ch, &Value);
                        hrc = E_FAIL;
                        break;
                }
            }
            if (FAILED(hrc))
                break;

            if (enmUnique & (SnapshotUniqueFlags_Number | SnapshotUniqueFlags_Timestamp))
            {
                ComPtr<ISnapshot> pSnapshot;
                hrc = sessionMachine->FindSnapshot(name.raw(),
                                                   pSnapshot.asOutParam());
                if (SUCCEEDED(hrc) || (enmUnique & SnapshotUniqueFlags_Force))
                {
                    /* there is a duplicate, need to create a unique name */
                    uint32_t count = 0;
                    RTTIMESPEC now;

                    if (enmUnique & SnapshotUniqueFlags_Number)
                    {
                        if (enmUnique & SnapshotUniqueFlags_Force)
                            count = 1;
                        else
                            count = 2;
                        RTTimeSpecSetNano(&now, 0); /* Shut up MSC */
                    }
                    else
                        RTTimeNow(&now);

                    while (count < 500)
                    {
                        Utf8Str suffix;
                        if (enmUnique & SnapshotUniqueFlags_Number)
                            suffix = Utf8StrFmt("%u", count);
                        else
                        {
                            RTTIMESPEC nowplus = now;
                            RTTimeSpecAddSeconds(&nowplus, count);
                            RTTIME stamp;
                            RTTimeExplode(&stamp, &nowplus);
                            suffix = Utf8StrFmt("%04u-%02u-%02uT%02u:%02u:%02uZ", stamp.i32Year, stamp.u8Month, stamp.u8MonthDay, stamp.u8Hour, stamp.u8Minute, stamp.u8Second);
                        }
                        Bstr tryName = name;
                        if (enmUnique & SnapshotUniqueFlags_Space)
                            tryName = BstrFmt("%ls %s", name.raw(), suffix.c_str());
                        else
                            tryName = BstrFmt("%ls%s", name.raw(), suffix.c_str());
                        count++;
                        hrc = sessionMachine->FindSnapshot(tryName.raw(),
                                                           pSnapshot.asOutParam());
                        if (FAILED(hrc))
                        {
                            name = tryName;
                            break;
                        }
                    }
                    if (SUCCEEDED(hrc))
                    {
                        errorArgument(Snapshot::tr("Failed to generate a unique snapshot name"));
                        hrc = E_FAIL;
                        break;
                    }
                }
                hrc = S_OK;
            }

            ComPtr<IProgress> progress;
            Bstr snapId;
            CHECK_ERROR_BREAK(sessionMachine, TakeSnapshot(name.raw(), desc.raw(),
                                                           fPause, snapId.asOutParam(),
                                                           progress.asOutParam()));

            hrc = showProgress(progress);
            if (SUCCEEDED(hrc))
                RTPrintf(Snapshot::tr("Snapshot taken. UUID: %ls\n"), snapId.raw());
            else
                CHECK_PROGRESS_ERROR(progress, (Snapshot::tr("Failed to take snapshot")));
        }
        else if (    (fDelete = !strcmp(a->argv[1], "delete"))
                  || (fRestore = !strcmp(a->argv[1], "restore"))
                  || (fRestoreCurrent = !strcmp(a->argv[1], "restorecurrent"))
                )
        {
            setCurrentSubcommand(fDelete    ? HELP_SCOPE_SNAPSHOT_DELETE
                                 : fRestore ? HELP_SCOPE_SNAPSHOT_RESTORE
                                            : HELP_SCOPE_SNAPSHOT_RESTORECURRENT);

            if (fRestoreCurrent)
            {
                if (a->argc > 2)
                {
                    errorSyntax(Snapshot::tr("Too many arguments"));
                    hrc = E_FAIL;
                    break;
                }
            }
            /* exactly one parameter: snapshot name */
            else if (a->argc != 3)
            {
                errorSyntax(Snapshot::tr("Expecting snapshot name only"));
                hrc = E_FAIL;
                break;
            }

            ComPtr<ISnapshot> pSnapshot;

            if (fRestoreCurrent)
            {
                CHECK_ERROR_BREAK(sessionMachine, COMGETTER(CurrentSnapshot)(pSnapshot.asOutParam()));
                if (pSnapshot.isNull())
                {
                    RTPrintf(Snapshot::tr("This machine does not have any snapshots\n"));
                    return RTEXITCODE_FAILURE;
                }
            }
            else
            {
                // restore or delete snapshot: then resolve cmd line argument to snapshot instance
                CHECK_ERROR_BREAK(sessionMachine, FindSnapshot(Bstr(a->argv[2]).raw(),
                                                               pSnapshot.asOutParam()));
            }

            Bstr bstrSnapGuid;
            CHECK_ERROR_BREAK(pSnapshot, COMGETTER(Id)(bstrSnapGuid.asOutParam()));

            Bstr bstrSnapName;
            CHECK_ERROR_BREAK(pSnapshot, COMGETTER(Name)(bstrSnapName.asOutParam()));

            ComPtr<IProgress> pProgress;

            RTPrintf(Snapshot::tr("%s snapshot '%ls' (%ls)\n"),
                     fDelete ? Snapshot::tr("Deleting") : Snapshot::tr("Restoring"), bstrSnapName.raw(), bstrSnapGuid.raw());

            if (fDelete)
            {
                CHECK_ERROR_BREAK(sessionMachine, DeleteSnapshot(bstrSnapGuid.raw(),
                                                                 pProgress.asOutParam()));
            }
            else
            {
                // restore or restore current
                CHECK_ERROR_BREAK(sessionMachine, RestoreSnapshot(pSnapshot, pProgress.asOutParam()));
            }

            hrc = showProgress(pProgress);
            CHECK_PROGRESS_ERROR(pProgress, (Snapshot::tr("Snapshot operation failed")));
        }
        else if (!strcmp(a->argv[1], "edit"))
        {
            setCurrentSubcommand(HELP_SCOPE_SNAPSHOT_EDIT);
            if (a->argc < 3)
            {
                errorSyntax(Snapshot::tr("Missing snapshot name"));
                hrc = E_FAIL;
                break;
            }

            /* Parse the optional arguments, allowing more freedom than the
             * synopsis explains. Can rename multiple snapshots and so on. */
            ComPtr<ISnapshot> pSnapshot;
            static const RTGETOPTDEF s_aEditOptions[] =
            {
                { "--current",     'c', RTGETOPT_REQ_NOTHING },
                { "-current",      'c', RTGETOPT_REQ_NOTHING },
                { "--name",        'n', RTGETOPT_REQ_STRING },
                { "-name",         'n', RTGETOPT_REQ_STRING },
                { "-newname",      'n', RTGETOPT_REQ_STRING },
                { "--description", 'd', RTGETOPT_REQ_STRING },
                { "-description",  'd', RTGETOPT_REQ_STRING },
                { "-desc",         'd', RTGETOPT_REQ_STRING }
            };
            RTGETOPTSTATE GetOptState;
            RTGetOptInit(&GetOptState, a->argc, a->argv, s_aEditOptions, RT_ELEMENTS(s_aEditOptions),
                         2, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
            int ch;
            RTGETOPTUNION Value;
            while (   SUCCEEDED(hrc)
                   && (ch = RTGetOpt(&GetOptState, &Value)))
            {
                switch (ch)
                {
                    case 'c':
                        CHECK_ERROR_BREAK(sessionMachine, COMGETTER(CurrentSnapshot)(pSnapshot.asOutParam()));
                        if (pSnapshot.isNull())
                        {
                            RTPrintf(Snapshot::tr("This machine does not have any snapshots\n"));
                            return RTEXITCODE_FAILURE;
                        }
                        break;

                    case 'n':
                        CHECK_ERROR_BREAK(pSnapshot, COMSETTER(Name)(Bstr(Value.psz).raw()));
                        break;

                    case 'd':
                        CHECK_ERROR_BREAK(pSnapshot, COMSETTER(Description)(Bstr(Value.psz).raw()));
                        break;

                    case VINF_GETOPT_NOT_OPTION:
                        CHECK_ERROR_BREAK(sessionMachine, FindSnapshot(Bstr(Value.psz).raw(), pSnapshot.asOutParam()));
                        break;

                    default:
                        errorGetOpt(ch, &Value);
                        hrc = E_FAIL;
                        break;
                }
            }

            if (FAILED(hrc))
                break;
        }
        else if (!strcmp(a->argv[1], "showvminfo"))
        {
            setCurrentSubcommand(HELP_SCOPE_SNAPSHOT_SHOWVMINFO);

            /* exactly one parameter: snapshot name */
            if (a->argc != 3)
            {
                errorSyntax(Snapshot::tr("Expecting snapshot name only"));
                hrc = E_FAIL;
                break;
            }

            ComPtr<ISnapshot> pSnapshot;

            CHECK_ERROR_BREAK(sessionMachine, FindSnapshot(Bstr(a->argv[2]).raw(),
                                                           pSnapshot.asOutParam()));

            /* get the machine of the given snapshot */
            ComPtr<IMachine> pMachine2;
            pSnapshot->COMGETTER(Machine)(pMachine2.asOutParam());
            showVMInfo(a->virtualBox, pMachine2, NULL, VMINFO_NONE);
        }
        else if (!strcmp(a->argv[1], "list"))
        {
            setCurrentSubcommand(HELP_SCOPE_SNAPSHOT_LIST);
            hrc = handleSnapshotList(a, sessionMachine) == RTEXITCODE_SUCCESS ? S_OK : E_FAIL;
        }
        else if (!strcmp(a->argv[1], "dump"))          // undocumented parameter to debug snapshot info
            DumpSnapshot(sessionMachine);
        else
        {
            errorSyntax(Snapshot::tr("Invalid parameter '%s'"), a->argv[1]);
            hrc = E_FAIL;
        }
    } while (0);

    a->session->UnlockMachine();

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}
