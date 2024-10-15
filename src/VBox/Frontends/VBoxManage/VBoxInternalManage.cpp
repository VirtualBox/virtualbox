/* $Id: VBoxInternalManage.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxManage - The 'internalcommands' command.
 *
 * VBoxInternalManage used to be a second CLI for doing special tricks,
 * not intended for general usage, only for assisting VBox developers.
 * It is now integrated into VBoxManage.
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
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>

#include <VBox/com/VirtualBox.h>

#include <VBox/vd.h>
#include <VBox/sup.h>
#include <VBox/log.h>
#include <VBox/version.h>

#include <iprt/buildconfig.h>
#include <iprt/ctype.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/uuid.h>
#include <iprt/sha.h>

#include "VBoxManage.h"

/* Includes for the raw disk stuff. */
#ifdef RT_OS_WINDOWS
# include <iprt/win/windows.h>
# include <winioctl.h>
#elif defined(RT_OS_LINUX) || defined(RT_OS_DARWIN) \
    || defined(RT_OS_SOLARIS) || defined(RT_OS_FREEBSD)
# include <errno.h>
# include <sys/ioctl.h>
# include <sys/types.h>
# include <sys/stat.h>
# include <fcntl.h>
# include <unistd.h>
#endif
#ifdef RT_OS_LINUX
# include <sys/utsname.h>
# include <linux/hdreg.h>
# include <linux/fs.h>
# include <stdlib.h> /* atoi() */
#endif /* RT_OS_LINUX */
#ifdef RT_OS_DARWIN
# include <sys/disk.h>
#endif /* RT_OS_DARWIN */
#ifdef RT_OS_SOLARIS
# include <stropts.h>
# include <sys/dkio.h>
# include <sys/vtoc.h>
#endif /* RT_OS_SOLARIS */
#ifdef RT_OS_FREEBSD
# include <sys/disk.h>
#endif /* RT_OS_FREEBSD */

using namespace com;


/** Macro for checking whether a partition is of extended type or not. */
#define PARTTYPE_IS_EXTENDED(x) ((x) == 0x05 || (x) == 0x0f || (x) == 0x85)

/** Maximum number of partitions we can deal with.
 * Ridiculously large number, but the memory consumption is rather low so who
 * cares about never using most entries. */
#define HOSTPARTITION_MAX 100

DECLARE_TRANSLATION_CONTEXT(Internal);


typedef struct HOSTPARTITION
{
    /** partition number */
    unsigned        uIndex;
    /** partition number (internal only, windows specific numbering) */
    unsigned        uIndexWin;
    /** partition type */
    unsigned        uType;
    /** CHS/cylinder of the first sector */
    unsigned        uStartCylinder;
    /** CHS/head of the first sector */
    unsigned        uStartHead;
    /** CHS/head of the first sector */
    unsigned        uStartSector;
    /** CHS/cylinder of the last sector */
    unsigned        uEndCylinder;
    /** CHS/head of the last sector */
    unsigned        uEndHead;
    /** CHS/sector of the last sector */
    unsigned        uEndSector;
    /** start sector of this partition relative to the beginning of the hard
     * disk or relative to the beginning of the extended partition table */
    uint64_t        uStart;
    /** numer of sectors of the partition */
    uint64_t        uSize;
    /** start sector of this partition _table_ */
    uint64_t        uPartDataStart;
    /** numer of sectors of this partition _table_ */
    uint64_t        cPartDataSectors;
} HOSTPARTITION, *PHOSTPARTITION;

typedef struct HOSTPARTITIONS
{
    /** partitioning type - MBR or GPT */
    VDISKPARTTYPE   uPartitioningType;
    unsigned        cPartitions;
    HOSTPARTITION   aPartitions[HOSTPARTITION_MAX];
} HOSTPARTITIONS, *PHOSTPARTITIONS;


/** @name Syntax diagram category, i.e. the command.
 * @{ */
typedef enum
{
    USAGE_INVALID = 0,
    USAGE_I_LOADSYMS,
    USAGE_I_LOADMAP,
    USAGE_I_SETHDUUID,
    USAGE_I_LISTPARTITIONS,
    USAGE_I_CREATERAWVMDK,
    USAGE_I_MODINSTALL,
    USAGE_I_MODUNINSTALL,
    USAGE_I_RENAMEVMDK,
    USAGE_I_CONVERTTORAW,
    USAGE_I_CONVERTHD,
    USAGE_I_DUMPHDINFO,
    USAGE_I_DEBUGLOG,
    USAGE_I_SETHDPARENTUUID,
    USAGE_I_PASSWORDHASH,
    USAGE_I_GUESTSTATS,
    USAGE_I_REPAIRHD,
    USAGE_I_ALL
} USAGECATEGORY;
/** @} */


/**
 * Print the usage info.
 */
static void printUsageInternal(USAGECATEGORY enmCommand, PRTSTREAM pStrm)
{
    Assert(enmCommand != USAGE_INVALID);
    RTStrmPrintf(pStrm,
        Internal::tr(
         "Usage: VBoxManage internalcommands <command> [command arguments]\n"
         "\n"
         "Commands:\n"
         "\n"
         "%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s"
         "WARNING: This is a development tool and should only be used to analyse\n"
         "         problems. It is completely unsupported and will change in\n"
         "         incompatible ways without warning.\n"),

        (enmCommand == USAGE_I_LOADMAP || enmCommand == USAGE_I_ALL)
        ? Internal::tr(
           "  loadmap <vmname|uuid> <symfile> <address> [module] [subtrahend] [segment]\n"
           "      This will instruct DBGF to load the given map file\n"
           "      during initialization.  (See also loadmap in the debugger.)\n"
           "\n")
        : "",
        (enmCommand == USAGE_I_LOADSYMS || enmCommand == USAGE_I_ALL)
        ? Internal::tr(
           "  loadsyms <vmname|uuid> <symfile> [delta] [module] [module address]\n"
           "      This will instruct DBGF to load the given symbol file\n"
           "      during initialization.\n"
           "\n")
        : "",
        (enmCommand == USAGE_I_SETHDUUID || enmCommand == USAGE_I_ALL)
        ? Internal::tr(
           "  sethduuid <filepath> [<uuid>]\n"
           "       Assigns a new UUID to the given image file. This way, multiple copies\n"
           "       of a container can be registered.\n"
           "\n")
        : "",
        (enmCommand == USAGE_I_SETHDPARENTUUID || enmCommand == USAGE_I_ALL)
        ? Internal::tr(
           "  sethdparentuuid <filepath> <uuid>\n"
           "       Assigns a new parent UUID to the given image file.\n"
           "\n")
        : "",
        (enmCommand == USAGE_I_DUMPHDINFO || enmCommand == USAGE_I_ALL)
        ? Internal::tr(
           "  dumphdinfo <filepath>\n"
           "       Prints information about the image at the given location.\n"
           "\n")
        : "",
        (enmCommand == USAGE_I_LISTPARTITIONS || enmCommand == USAGE_I_ALL)
        ? Internal::tr(
           "  listpartitions -rawdisk <diskname>\n"
           "       Lists all partitions on <diskname>.\n"
           "\n")
        : "",
        (enmCommand == USAGE_I_CREATERAWVMDK || enmCommand == USAGE_I_ALL)
        ? Internal::tr(
           "  createrawvmdk --filename <filename> --rawdisk <diskname>\n"
           "                [--partitions <list of partition numbers> [--mbr <filename>] ]\n"
           "                [--relative]\n"
           "       Creates a new VMDK image which gives direct access to a physical hard\n"
           "       disk on the host. The entire disk can be presented to the guest or\n"
           "       just specific partitions specified using the --partitions parameter.\n"
           "       If access to individual partitions is granted, then the --mbr parameter\n"
           "       can be used to specify an alternative Master Boot Record (MBR) (note\n"
           "       that the partitioning information in the MBR file is ignored). The\n"
           "       format of the diskname argument for the --rawdisk parameter varies by\n"
           "       platform but can be determined using the command:\n"
           "         VBoxManage list hostdrives\n"
           "       The output lists the available drives and their partitions along with\n"
           "       their partition types and sizes.\n"
           "       On Linux, FreeBSD, and Windows hosts the --relative parameter creates a\n"
           "       VMDK image file which references the specified individual partitions\n"
           "       directly instead of referencing the partitions by their offset from\n"
           "       the start of the physical disk.\n"
           "\n"
           "       Nota Bene: The 'createrawvdk' subcommand is deprecated. The equivalent\n"
           "       functionality is available using the 'VBoxManage createmedium' command\n"
           "       and should be used instead. See 'VBoxManage help createmedium' for\n"
           "       details.\n"
           "\n")
        : "",
        (enmCommand == USAGE_I_RENAMEVMDK || enmCommand == USAGE_I_ALL)
        ? Internal::tr(
           "  renamevmdk -from <filename> -to <filename>\n"
           "       Renames an existing VMDK image, including the base file and all its extents.\n"
           "\n")
        : "",
        (enmCommand == USAGE_I_CONVERTTORAW || enmCommand == USAGE_I_ALL)
#ifdef ENABLE_CONVERT_RAW_TO_STDOUT
        ? Internal::tr(
           "  converttoraw [-format <fileformat>] <filename> <outputfile>|stdout"
           "\n"
           "       Convert image to raw, writing to file or stdout.\n"
           "\n")
#else
        ? Internal::tr(
           "  converttoraw [-format <fileformat>] <filename> <outputfile>"
           "\n"
           "       Convert image to raw, writing to file.\n"
           "\n")
#endif
        : "",
        (enmCommand == USAGE_I_CONVERTHD || enmCommand == USAGE_I_ALL)
        ? Internal::tr(
           "  converthd [-srcformat VDI|VMDK|VHD|RAW]\n"
           "            [-dstformat VDI|VMDK|VHD|RAW]\n"
           "            <inputfile> <outputfile>\n"
           "       converts hard disk images between formats\n"
           "\n")
        : "",
        (enmCommand == USAGE_I_REPAIRHD || enmCommand == USAGE_I_ALL)
        ? Internal::tr(
           "  repairhd [-dry-run]\n"
           "           [-format VDI|VMDK|VHD|...]\n"
           "           <filename>\n"
           "       Tries to repair corrupted disk images\n"
           "\n")
        : "",
#ifdef RT_OS_WINDOWS
        (enmCommand == USAGE_I_MODINSTALL || enmCommand == USAGE_I_ALL)
        ? Internal::tr(
           "  modinstall\n"
           "       Installs the necessary driver for the host OS\n"
           "\n")
        : "",
        (enmCommand == USAGE_I_MODUNINSTALL || enmCommand == USAGE_I_ALL)
        ? Internal::tr(
           "  moduninstall\n"
           "       Deinstalls the driver\n"
           "\n")
        : "",
#else
        "",
        "",
#endif
        (enmCommand == USAGE_I_DEBUGLOG || enmCommand == USAGE_I_ALL)
        ? Internal::tr(
           "  debuglog <vmname|uuid> [--enable|--disable] [--flags todo]\n"
           "           [--groups todo] [--destinations todo]\n"
           "       Controls debug logging.\n"
           "\n")
        : "",
        (enmCommand == USAGE_I_PASSWORDHASH || enmCommand == USAGE_I_ALL)
        ? Internal::tr(
           "  passwordhash <password>\n"
           "       Generates a password hash.\n"
           "\n")
        : "",
        (enmCommand == USAGE_I_GUESTSTATS || enmCommand == USAGE_I_ALL)
        ? Internal::tr(
           "  gueststats <vmname|uuid> [--interval <seconds>]\n"
           "       Obtains and prints internal guest statistics.\n"
           "       Sets the update interval if specified.\n"
           "\n")
        : ""
        );
}


/**
 * Print a usage synopsis and the syntax error message.
 * @returns RTEXITCODE_SYNTAX.
 */
static RTEXITCODE errorSyntaxInternal(USAGECATEGORY enmCommand, const char *pszFormat, ...)
{
    va_list args;
    showLogo(g_pStdErr); // show logo even if suppressed

    printUsageInternal(enmCommand, g_pStdErr);

    va_start(args, pszFormat);
    RTStrmPrintf(g_pStdErr, Internal::tr("\nSyntax error: %N\n"), pszFormat, &args);
    va_end(args);
    return RTEXITCODE_SYNTAX;
}


/**
 * errorSyntaxInternal for RTGetOpt users.
 *
 * @returns RTEXITCODE_SYNTAX.
 *
 * @param   enmCommand      The command.
 * @param   vrc             The RTGetOpt return code.
 * @param   pValueUnion     The value union.
 */
static RTEXITCODE errorGetOptInternal(USAGECATEGORY enmCommand, int vrc, union RTGETOPTUNION const *pValueUnion)
{
    /*
     * Check if it is an unhandled standard option.
     */
    if (vrc == 'V')
    {
        RTPrintf("%sr%d\n", VBOX_VERSION_STRING, RTBldCfgRevision());
        return RTEXITCODE_SUCCESS;
    }

    if (vrc == 'h')
    {
        showLogo(g_pStdErr);
        printUsageInternal(enmCommand, g_pStdOut);
        return RTEXITCODE_SUCCESS;
    }

    /*
     * General failure.
     */
    showLogo(g_pStdErr); // show logo even if suppressed

    printUsageInternal(enmCommand, g_pStdErr);

    if (vrc == VINF_GETOPT_NOT_OPTION)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, Internal::tr("Invalid parameter '%s'"), pValueUnion->psz);
    if (vrc > 0)
    {
        if (RT_C_IS_PRINT(vrc))
            return RTMsgErrorExit(RTEXITCODE_SYNTAX, Internal::tr("Invalid option -%c"), vrc);
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, Internal::tr("Invalid option case %i"), vrc);
    }
    if (vrc == VERR_GETOPT_UNKNOWN_OPTION)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, Internal::tr("Unknown option: %s"), pValueUnion->psz);
    if (vrc == VERR_GETOPT_INVALID_ARGUMENT_FORMAT)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, Internal::tr("Invalid argument format: %s"), pValueUnion->psz);
    if (pValueUnion->pDef)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, "%s: %Rrs", pValueUnion->pDef->pszLong, vrc);
    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "%Rrs", vrc);
}


/**
 * Externally visible wrapper around printUsageInternal() to dump the
 * complete usage text.
 *
 * @param   pStrm           The stream to dump the usage text to.
 */
DECLHIDDEN(void) printUsageInternalCmds(PRTSTREAM pStrm)
{
    printUsageInternal(USAGE_I_ALL, pStrm);
}


/** @todo this is no longer necessary, we can enumerate extra data */
/**
 * Finds a new unique key name.
 *
 * I don't think this is 100% race condition proof, but we assumes
 * the user is not trying to push this point.
 *
 * @returns Result from the insert.
 * @param   pMachine        The Machine object.
 * @param   pszKeyBase      The base key.
 * @param   rKey            Reference to the string object in which we will return the key.
 */
static HRESULT NewUniqueKey(ComPtr<IMachine> pMachine, const char *pszKeyBase, Utf8Str &rKey)
{
    Bstr KeyBase(pszKeyBase);
    Bstr Keys;
    HRESULT hrc = pMachine->GetExtraData(KeyBase.raw(), Keys.asOutParam());
    if (FAILED(hrc))
        return hrc;

    /* if there are no keys, it's simple. */
    if (Keys.isEmpty())
    {
        rKey = "1";
        return pMachine->SetExtraData(KeyBase.raw(), Bstr(rKey).raw());
    }

    /* find a unique number - brute force rulez. */
    Utf8Str KeysUtf8(Keys);
    const char *pszKeys = RTStrStripL(KeysUtf8.c_str());
    for (unsigned i = 1; i < 1000000; i++)
    {
        char szKey[32];
        size_t cchKey = RTStrPrintf(szKey, sizeof(szKey), "%#x", i);
        const char *psz = strstr(pszKeys, szKey);
        while (psz)
        {
            if (    (   psz == pszKeys
                     || psz[-1] == ' ')
                &&  (   psz[cchKey] == ' '
                     || !psz[cchKey])
               )
                break;
            psz = strstr(psz + cchKey, szKey);
        }
        if (!psz)
        {
            rKey = szKey;
            Utf8StrFmt NewKeysUtf8("%s %s", pszKeys, szKey);
            return pMachine->SetExtraData(KeyBase.raw(),
                                          Bstr(NewKeysUtf8).raw());
        }
    }
    RTMsgError(Internal::tr("Cannot find unique key for '%s'!"), pszKeyBase);
    return E_FAIL;
}


#if 0
/**
 * Remove a key.
 *
 * I don't think this isn't 100% race condition proof, but we assumes
 * the user is not trying to push this point.
 *
 * @returns Result from the insert.
 * @param   pMachine    The machine object.
 * @param   pszKeyBase  The base key.
 * @param   pszKey      The key to remove.
 */
static HRESULT RemoveKey(ComPtr<IMachine> pMachine, const char *pszKeyBase, const char *pszKey)
{
    Bstr Keys;
    HRESULT hrc = pMachine->GetExtraData(Bstr(pszKeyBase), Keys.asOutParam());
    if (FAILED(hrc))
        return hrc;

    /* if there are no keys, it's simple. */
    if (Keys.isEmpty())
        return S_OK;

    char *pszKeys;
    int vrc = RTUtf16ToUtf8(Keys.raw(), &pszKeys);
    if (RT_SUCCESS(vrc))
    {
        /* locate it */
        size_t cchKey = strlen(pszKey);
        char *psz = strstr(pszKeys, pszKey);
        while (psz)
        {
            if (    (   psz == pszKeys
                     || psz[-1] == ' ')
                &&  (   psz[cchKey] == ' '
                     || !psz[cchKey])
               )
                break;
            psz = strstr(psz + cchKey, pszKey);
        }
        if (psz)
        {
            /* remove it */
            char *pszNext = RTStrStripL(psz + cchKey);
            if (*pszNext)
                memmove(psz, pszNext, strlen(pszNext) + 1);
            else
                *psz = '\0';
            psz = RTStrStrip(pszKeys);

            /* update */
            hrc = pMachine->SetExtraData(Bstr(pszKeyBase), Bstr(psz));
        }

        RTStrFree(pszKeys);
        return hrc;
    }
    else
        RTMsgError(Internal::tr("Failed to delete key '%s' from '%s',  string conversion error %Rrc!"),
                   pszKey,  pszKeyBase, vrc);

    return E_FAIL;
}
#endif


/**
 * Sets a key value, does necessary error bitching.
 *
 * @returns COM status code.
 * @param   pMachine        The Machine object.
 * @param   pszKeyBase      The key base.
 * @param   pszKey          The key.
 * @param   pszAttribute    The attribute name.
 * @param   pszValue        The string value.
 */
static HRESULT SetString(ComPtr<IMachine> pMachine, const char *pszKeyBase, const char *pszKey, const char *pszAttribute, const char *pszValue)
{
    HRESULT hrc = pMachine->SetExtraData(BstrFmt("%s/%s/%s", pszKeyBase,
                                                 pszKey, pszAttribute).raw(),
                                         Bstr(pszValue).raw());
    if (FAILED(hrc))
        RTMsgError(Internal::tr("Failed to set '%s/%s/%s' to '%s'! hrc=%#x"),
                   pszKeyBase, pszKey, pszAttribute, pszValue, hrc);
    return hrc;
}


/**
 * Sets a key value, does necessary error bitching.
 *
 * @returns COM status code.
 * @param   pMachine        The Machine object.
 * @param   pszKeyBase      The key base.
 * @param   pszKey          The key.
 * @param   pszAttribute    The attribute name.
 * @param   u64Value        The value.
 */
static HRESULT SetUInt64(ComPtr<IMachine> pMachine, const char *pszKeyBase, const char *pszKey, const char *pszAttribute, uint64_t u64Value)
{
    char szValue[64];
    RTStrPrintf(szValue, sizeof(szValue), "%#RX64", u64Value);
    return SetString(pMachine, pszKeyBase, pszKey, pszAttribute, szValue);
}


/**
 * Sets a key value, does necessary error bitching.
 *
 * @returns COM status code.
 * @param   pMachine        The Machine object.
 * @param   pszKeyBase      The key base.
 * @param   pszKey          The key.
 * @param   pszAttribute    The attribute name.
 * @param   i64Value        The value.
 */
static HRESULT SetInt64(ComPtr<IMachine> pMachine, const char *pszKeyBase, const char *pszKey, const char *pszAttribute, int64_t i64Value)
{
    char szValue[64];
    RTStrPrintf(szValue, sizeof(szValue), "%RI64", i64Value);
    return SetString(pMachine, pszKeyBase, pszKey, pszAttribute, szValue);
}


/**
 * Identical to the 'loadsyms' command.
 */
static RTEXITCODE CmdLoadSyms(int argc, char **argv, ComPtr<IVirtualBox> aVirtualBox, ComPtr<ISession> aSession)
{
    RT_NOREF(aSession);
    HRESULT hrc;

    /*
     * Get the VM
     */
    ComPtr<IMachine> machine;
    CHECK_ERROR_RET(aVirtualBox, FindMachine(Bstr(argv[0]).raw(),
                                             machine.asOutParam()), RTEXITCODE_FAILURE);

    /*
     * Parse the command.
     */
    const char *pszFilename;
    int64_t     offDelta = 0;
    const char *pszModule = NULL;
    uint64_t    ModuleAddress = UINT64_MAX;
    uint64_t    ModuleSize = 0;

    /* filename */
    if (argc < 2)
        return errorArgument(Internal::tr("Missing the filename argument!\n"));
    pszFilename = argv[1];

    /* offDelta */
    if (argc >= 3)
    {
        int vrc = RTStrToInt64Ex(argv[2], NULL, 0, &offDelta);
        if (RT_FAILURE(vrc))
            return errorArgument(argv[0], Internal::tr("Failed to read delta '%s', vrc=%Rrc\n"), argv[2], vrc);
    }

    /* pszModule */
    if (argc >= 4)
        pszModule = argv[3];

    /* ModuleAddress */
    if (argc >= 5)
    {
        int vrc = RTStrToUInt64Ex(argv[4], NULL, 0, &ModuleAddress);
        if (RT_FAILURE(vrc))
            return errorArgument(argv[0], Internal::tr("Failed to read module address '%s', vrc=%Rrc\n"), argv[4], vrc);
    }

    /* ModuleSize */
    if (argc >= 6)
    {
        int vrc = RTStrToUInt64Ex(argv[5], NULL, 0, &ModuleSize);
        if (RT_FAILURE(vrc))
            return errorArgument(argv[0], Internal::tr("Failed to read module size '%s', vrc=%Rrc\n"), argv[5], vrc);
    }

    /*
     * Add extra data.
     */
    Utf8Str KeyStr;
    hrc = NewUniqueKey(machine, "VBoxInternal/DBGF/loadsyms", KeyStr);
    if (SUCCEEDED(hrc))
        hrc = SetString(machine, "VBoxInternal/DBGF/loadsyms", KeyStr.c_str(), "Filename", pszFilename);
    if (SUCCEEDED(hrc) && argc >= 3)
        hrc = SetInt64(machine, "VBoxInternal/DBGF/loadsyms", KeyStr.c_str(), "Delta", offDelta);
    if (SUCCEEDED(hrc) && argc >= 4)
        hrc = SetString(machine, "VBoxInternal/DBGF/loadsyms", KeyStr.c_str(), "Module", pszModule);
    if (SUCCEEDED(hrc) && argc >= 5)
        hrc = SetUInt64(machine, "VBoxInternal/DBGF/loadsyms", KeyStr.c_str(), "ModuleAddress", ModuleAddress);
    if (SUCCEEDED(hrc) && argc >= 6)
        hrc = SetUInt64(machine, "VBoxInternal/DBGF/loadsyms", KeyStr.c_str(), "ModuleSize", ModuleSize);

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/**
 * Identical to the 'loadmap' command.
 */
static RTEXITCODE CmdLoadMap(int argc, char **argv, ComPtr<IVirtualBox> aVirtualBox, ComPtr<ISession> aSession)
{
    RT_NOREF(aSession);
    HRESULT hrc;

    /*
     * Get the VM
     */
    ComPtr<IMachine> machine;
    CHECK_ERROR_RET(aVirtualBox, FindMachine(Bstr(argv[0]).raw(),
                                             machine.asOutParam()), RTEXITCODE_FAILURE);

    /*
     * Parse the command.
     */
    const char *pszFilename;
    uint64_t    ModuleAddress = UINT64_MAX;
    const char *pszModule = NULL;
    uint64_t    offSubtrahend = 0;
    uint32_t    iSeg = UINT32_MAX;

    /* filename */
    if (argc < 2)
        return errorArgument(Internal::tr("Missing the filename argument!\n"));
    pszFilename = argv[1];

    /* address */
    if (argc < 3)
        return errorArgument(Internal::tr("Missing the module address argument!\n"));
    int vrc = RTStrToUInt64Ex(argv[2], NULL, 0, &ModuleAddress);
    if (RT_FAILURE(vrc))
        return errorArgument(argv[0], Internal::tr("Failed to read module address '%s', vrc=%Rrc\n"), argv[2], vrc);

    /* name (optional) */
    if (argc > 3)
        pszModule = argv[3];

    /* subtrahend (optional) */
    if (argc > 4)
    {
        vrc = RTStrToUInt64Ex(argv[4], NULL, 0, &offSubtrahend);
        if (RT_FAILURE(vrc))
            return errorArgument(argv[0], Internal::tr("Failed to read subtrahend '%s', vrc=%Rrc\n"), argv[4], vrc);
    }

    /* segment (optional) */
    if (argc > 5)
    {
        vrc = RTStrToUInt32Ex(argv[5], NULL, 0, &iSeg);
        if (RT_FAILURE(vrc))
            return errorArgument(argv[0], Internal::tr("Failed to read segment number '%s', vrc=%Rrc\n"), argv[5], vrc);
    }

    /*
     * Add extra data.
     */
    Utf8Str KeyStr;
    hrc = NewUniqueKey(machine, "VBoxInternal/DBGF/loadmap", KeyStr);
    if (SUCCEEDED(hrc))
        hrc = SetString(machine, "VBoxInternal/DBGF/loadmap", KeyStr.c_str(), "Filename", pszFilename);
    if (SUCCEEDED(hrc))
        hrc = SetUInt64(machine, "VBoxInternal/DBGF/loadmap", KeyStr.c_str(), "Address", ModuleAddress);
    if (SUCCEEDED(hrc) && pszModule != NULL)
        hrc = SetString(machine, "VBoxInternal/DBGF/loadmap", KeyStr.c_str(), "Name", pszModule);
    if (SUCCEEDED(hrc) && offSubtrahend != 0)
        hrc = SetUInt64(machine, "VBoxInternal/DBGF/loadmap", KeyStr.c_str(), "Subtrahend", offSubtrahend);
    if (SUCCEEDED(hrc) && iSeg != UINT32_MAX)
        hrc = SetUInt64(machine, "VBoxInternal/DBGF/loadmap", KeyStr.c_str(), "Segment", iSeg);

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


static DECLCALLBACK(void) handleVDError(void *pvUser, int vrc, RT_SRC_POS_DECL, const char *pszFormat, va_list va)
{
    RT_NOREF(pvUser);
    RTMsgErrorV(pszFormat, va);
    RTMsgError(Internal::tr("Error code %Rrc at %s(%u) in function %s"), vrc, RT_SRC_POS_ARGS);
}

static DECLCALLBACK(int) handleVDMessage(void *pvUser, const char *pszFormat, va_list va)
{
    NOREF(pvUser);
    return RTPrintfV(pszFormat, va);
}

static RTEXITCODE CmdSetHDUUID(int argc, char **argv, ComPtr<IVirtualBox> aVirtualBox, ComPtr<ISession> aSession)
{
    RT_NOREF(aVirtualBox, aSession);
    Guid uuid;
    RTUUID rtuuid;
    enum eUuidType {
        HDUUID,
        HDPARENTUUID
    } uuidType;

    if (!strcmp(argv[0], "sethduuid"))
    {
        uuidType = HDUUID;
        if (argc != 3 && argc != 2)
            return errorSyntaxInternal(USAGE_I_SETHDUUID, Internal::tr("Not enough parameters"));
        /* if specified, take UUID, otherwise generate a new one */
        if (argc == 3)
        {
            if (RT_FAILURE(RTUuidFromStr(&rtuuid, argv[2])))
                return errorSyntaxInternal(USAGE_I_SETHDUUID, Internal::tr("Invalid UUID parameter"));
            uuid = argv[2];
        } else
            uuid.create();
    }
    else if (!strcmp(argv[0], "sethdparentuuid"))
    {
        uuidType = HDPARENTUUID;
        if (argc != 3)
            return errorSyntaxInternal(USAGE_I_SETHDPARENTUUID, Internal::tr("Not enough parameters"));
        if (RT_FAILURE(RTUuidFromStr(&rtuuid, argv[2])))
            return errorSyntaxInternal(USAGE_I_SETHDPARENTUUID, Internal::tr("Invalid UUID parameter"));
        uuid = argv[2];
    }
    else
        return errorSyntaxInternal(USAGE_I_SETHDUUID, Internal::tr("Invalid invocation"));

    /* just try it */
    char *pszFormat = NULL;
    VDTYPE enmType = VDTYPE_INVALID;
    int vrc = VDGetFormat(NULL /* pVDIfsDisk */, NULL /* pVDIfsImage */, argv[1], VDTYPE_INVALID, &pszFormat, &enmType);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Internal::tr("Format autodetect failed: %Rrc"), vrc);

    PVDISK pDisk = NULL;

    PVDINTERFACE     pVDIfs = NULL;
    VDINTERFACEERROR vdInterfaceError;
    vdInterfaceError.pfnError     = handleVDError;
    vdInterfaceError.pfnMessage   = handleVDMessage;

    vrc = VDInterfaceAdd(&vdInterfaceError.Core, "VBoxManage_IError", VDINTERFACETYPE_ERROR,
                         NULL, sizeof(VDINTERFACEERROR), &pVDIfs);
    AssertRC(vrc);

    vrc = VDCreate(pVDIfs, enmType, &pDisk);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Internal::tr("Cannot create the virtual disk container: %Rrc"), vrc);

    /* Open the image */
    vrc = VDOpen(pDisk, pszFormat, argv[1], VD_OPEN_FLAGS_NORMAL | VD_OPEN_FLAGS_INFO, NULL);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Internal::tr("Cannot open the image: %Rrc"), vrc);

    if (uuidType == HDUUID)
      vrc = VDSetUuid(pDisk, VD_LAST_IMAGE, uuid.raw());
    else
      vrc = VDSetParentUuid(pDisk, VD_LAST_IMAGE, uuid.raw());
    if (RT_FAILURE(vrc))
        RTMsgError(Internal::tr("Cannot set a new UUID: %Rrc"), vrc);
    else
        RTPrintf(Internal::tr("UUID changed to: %s\n"), uuid.toString().c_str());

    VDCloseAll(pDisk);

    return RT_SUCCESS(vrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


static RTEXITCODE CmdDumpHDInfo(int argc, char **argv, ComPtr<IVirtualBox> aVirtualBox, ComPtr<ISession> aSession)
{
    RT_NOREF(aVirtualBox, aSession);

    /* we need exactly one parameter: the image file */
    if (argc != 1)
    {
        return errorSyntaxInternal(USAGE_I_DUMPHDINFO, Internal::tr("Not enough parameters"));
    }

    /* just try it */
    char *pszFormat = NULL;
    VDTYPE enmType = VDTYPE_INVALID;
    int vrc = VDGetFormat(NULL /* pVDIfsDisk */, NULL /* pVDIfsImage */, argv[0], VDTYPE_INVALID, &pszFormat, &enmType);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Internal::tr("Format autodetect failed: %Rrc"), vrc);

    PVDISK pDisk = NULL;

    PVDINTERFACE     pVDIfs = NULL;
    VDINTERFACEERROR vdInterfaceError;
    vdInterfaceError.pfnError     = handleVDError;
    vdInterfaceError.pfnMessage   = handleVDMessage;

    vrc = VDInterfaceAdd(&vdInterfaceError.Core, "VBoxManage_IError", VDINTERFACETYPE_ERROR,
                         NULL, sizeof(VDINTERFACEERROR), &pVDIfs);
    AssertRC(vrc);

    vrc = VDCreate(pVDIfs, enmType, &pDisk);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Internal::tr("Cannot create the virtual disk container: %Rrc"), vrc);

    /* Open the image */
    vrc = VDOpen(pDisk, pszFormat, argv[0], VD_OPEN_FLAGS_READONLY | VD_OPEN_FLAGS_INFO, NULL);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Internal::tr("Cannot open the image: %Rrc"), vrc);

    VDDumpImages(pDisk);

    VDCloseAll(pDisk);

    return RT_SUCCESS(vrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static int partRead(RTFILE File, PHOSTPARTITIONS pPart)
{
    uint8_t aBuffer[512];
    uint8_t partitionTableHeader[512];
    uint32_t sector_size = 512;
    uint64_t lastUsableLBA = 0;

    VDISKPARTTYPE partitioningType;

    pPart->cPartitions = 0;
    memset(pPart->aPartitions, '\0', sizeof(pPart->aPartitions));

    int vrc = RTFileReadAt(File, 0, &aBuffer, sizeof(aBuffer), NULL);
    if (RT_FAILURE(vrc))
        return vrc;

    if (aBuffer[450] == 0xEE)/* check the sign of the GPT disk*/
    {
        partitioningType = VDISKPARTTYPE_GPT;
        pPart->uPartitioningType = VDISKPARTTYPE_GPT;//partitioningType;

        if (aBuffer[510] != 0x55 || aBuffer[511] != 0xaa)
            return VERR_INVALID_PARAMETER;

        vrc = RTFileReadAt(File, sector_size, &partitionTableHeader, sector_size, NULL);
        if (RT_SUCCESS(vrc))
        {
            /** @todo r=bird: This is a 64-bit magic value, right... */
            const char *l_ppth = (char *)partitionTableHeader;
            if (strncmp(l_ppth, "EFI PART", 8))
                return VERR_INVALID_PARAMETER;

            /** @todo check GPT Version */

            /** @todo r=bird: C have this handy concept called structures which
             *        greatly simplify data access...  (Someone is really lazy here!) */
#if 0 /* unused */
            uint64_t firstUsableLBA     = RT_MAKE_U64_FROM_U8(partitionTableHeader[40],
                                                              partitionTableHeader[41],
                                                              partitionTableHeader[42],
                                                              partitionTableHeader[43],
                                                              partitionTableHeader[44],
                                                              partitionTableHeader[45],
                                                              partitionTableHeader[46],
                                                              partitionTableHeader[47]
                                                              );
#endif
            lastUsableLBA               = RT_MAKE_U64_FROM_U8(partitionTableHeader[48],
                                                              partitionTableHeader[49],
                                                              partitionTableHeader[50],
                                                              partitionTableHeader[51],
                                                              partitionTableHeader[52],
                                                              partitionTableHeader[53],
                                                              partitionTableHeader[54],
                                                              partitionTableHeader[55]
                                                              );
            uint32_t partitionsNumber   = RT_MAKE_U32_FROM_U8(partitionTableHeader[80],
                                                              partitionTableHeader[81],
                                                              partitionTableHeader[82],
                                                              partitionTableHeader[83]
                                                              );
            uint32_t partitionEntrySize = RT_MAKE_U32_FROM_U8(partitionTableHeader[84],
                                                              partitionTableHeader[85],
                                                              partitionTableHeader[86],
                                                              partitionTableHeader[87]
                                                              );

            uint32_t currentEntry = 0;

            if (partitionEntrySize * partitionsNumber > 4 * _1M)
            {
                RTMsgError(Internal::tr("The GPT header seems corrupt because it contains too many entries"));
                return VERR_INVALID_PARAMETER;
            }

            uint8_t *pbPartTable = (uint8_t *)RTMemAllocZ(RT_ALIGN_Z(partitionEntrySize * partitionsNumber, 512));
            if (!pbPartTable)
            {
                RTMsgError(Internal::tr("Allocating memory for the GPT partitions entries failed"));
                return VERR_NO_MEMORY;
            }

            /* partition entries begin from LBA2 */
            /** @todo r=aeichner: Reading from LBA 2 is not always correct, the header will contain the starting LBA. */
            vrc = RTFileReadAt(File, 1024, pbPartTable, RT_ALIGN_Z(partitionEntrySize * partitionsNumber, 512), NULL);
            if (RT_FAILURE(vrc))
            {
                RTMsgError(Internal::tr("Reading the partition table failed"));
                RTMemFree(pbPartTable);
                return vrc;
            }

            while (currentEntry < partitionsNumber)
            {
                uint8_t *partitionEntry = pbPartTable + currentEntry * partitionEntrySize;

                uint64_t start = RT_MAKE_U64_FROM_U8(partitionEntry[32], partitionEntry[33], partitionEntry[34], partitionEntry[35],
                                                     partitionEntry[36], partitionEntry[37], partitionEntry[38], partitionEntry[39]);
                uint64_t end = RT_MAKE_U64_FROM_U8(partitionEntry[40], partitionEntry[41], partitionEntry[42], partitionEntry[43],
                                                   partitionEntry[44], partitionEntry[45], partitionEntry[46], partitionEntry[47]);

                PHOSTPARTITION pCP = &pPart->aPartitions[pPart->cPartitions++];
                pCP->uIndex = currentEntry + 1;
                pCP->uIndexWin = currentEntry + 1;
                pCP->uType = 0;
                pCP->uStartCylinder = 0;
                pCP->uStartHead = 0;
                pCP->uStartSector = 0;
                pCP->uEndCylinder = 0;
                pCP->uEndHead = 0;
                pCP->uEndSector = 0;
                pCP->uPartDataStart = 0;    /* will be filled out later properly. */
                pCP->cPartDataSectors = 0;
                if (start==0 || end==0)
                {
                    pCP->uIndex = 0;
                    pCP->uIndexWin = 0;
                    --pPart->cPartitions;
                    break;
                }
                else
                {
                    pCP->uStart = start;
                    pCP->uSize = (end +1) - start;/*+1 LBA because the last address is included*/
                }

                ++currentEntry;
            }

            RTMemFree(pbPartTable);
        }
    }
    else
    {
        partitioningType = VDISKPARTTYPE_MBR;
        pPart->uPartitioningType = VDISKPARTTYPE_MBR;//partitioningType;

        if (aBuffer[510] != 0x55 || aBuffer[511] != 0xaa)
            return VERR_INVALID_PARAMETER;

        unsigned uExtended = (unsigned)-1;
        unsigned uIndexWin = 1;

        for (unsigned i = 0; i < 4; i++)
        {
            uint8_t *p = &aBuffer[0x1be + i * 16];
            if (p[4] == 0)
                continue;
            PHOSTPARTITION pCP = &pPart->aPartitions[pPart->cPartitions++];
            pCP->uIndex = i + 1;
            pCP->uType = p[4];
            pCP->uStartCylinder = (uint32_t)p[3] + ((uint32_t)(p[2] & 0xc0) << 2);
            pCP->uStartHead = p[1];
            pCP->uStartSector = p[2] & 0x3f;
            pCP->uEndCylinder = (uint32_t)p[7] + ((uint32_t)(p[6] & 0xc0) << 2);
            pCP->uEndHead = p[5];
            pCP->uEndSector = p[6] & 0x3f;
            pCP->uStart = RT_MAKE_U32_FROM_U8(p[8], p[9], p[10], p[11]);
            pCP->uSize = RT_MAKE_U32_FROM_U8(p[12], p[13], p[14], p[15]);
            pCP->uPartDataStart = 0;    /* will be filled out later properly. */
            pCP->cPartDataSectors = 0;

            if (PARTTYPE_IS_EXTENDED(p[4]))
            {
                if (uExtended == (unsigned)-1)
                {
                    uExtended = (unsigned)(pCP - pPart->aPartitions);
                    pCP->uIndexWin = 0;
                }
                else
                {
                    RTMsgError(Internal::tr("More than one extended partition"));
                    return VERR_INVALID_PARAMETER;
                }
            }
            else
            {
                pCP->uIndexWin = uIndexWin;
                uIndexWin++;
            }
        }

        if (uExtended != (unsigned)-1)
        {
            unsigned uIndex = 5;
            uint64_t uStart = pPart->aPartitions[uExtended].uStart;
            uint64_t uOffset = 0;
            if (!uStart)
            {
                RTMsgError(Internal::tr("Inconsistency for logical partition start"));
                return VERR_INVALID_PARAMETER;
            }

            do
            {
                vrc = RTFileReadAt(File, (uStart + uOffset) * 512, &aBuffer, sizeof(aBuffer), NULL);
                if (RT_FAILURE(vrc))
                    return vrc;

                if (aBuffer[510] != 0x55 || aBuffer[511] != 0xaa)
                {
                    RTMsgError(Internal::tr("Logical partition without magic"));
                    return VERR_INVALID_PARAMETER;
                }
                uint8_t *p = &aBuffer[0x1be];

                if (p[4] == 0)
                {
                    RTMsgError(Internal::tr("Logical partition with type 0 encountered"));
                    return VERR_INVALID_PARAMETER;
                }

                PHOSTPARTITION pCP = &pPart->aPartitions[pPart->cPartitions++];
                pCP->uIndex = uIndex;
                pCP->uIndexWin = uIndexWin;
                pCP->uType = p[4];
                pCP->uStartCylinder = (uint32_t)p[3] + ((uint32_t)(p[2] & 0xc0) << 2);
                pCP->uStartHead = p[1];
                pCP->uStartSector = p[2] & 0x3f;
                pCP->uEndCylinder = (uint32_t)p[7] + ((uint32_t)(p[6] & 0xc0) << 2);
                pCP->uEndHead = p[5];
                pCP->uEndSector = p[6] & 0x3f;
                uint32_t uStartOffset = RT_MAKE_U32_FROM_U8(p[8], p[9], p[10], p[11]);
                if (!uStartOffset)
                {
                    RTMsgError(Internal::tr("Invalid partition start offset"));
                    return VERR_INVALID_PARAMETER;
                }
                pCP->uStart = uStart + uOffset + uStartOffset;
                pCP->uSize = RT_MAKE_U32_FROM_U8(p[12], p[13], p[14], p[15]);
                /* Fill out partitioning location info for EBR. */
                pCP->uPartDataStart = uStart + uOffset;
                pCP->cPartDataSectors = uStartOffset;
                p += 16;
                if (p[4] == 0)
                    uExtended = (unsigned)-1;
                else if (PARTTYPE_IS_EXTENDED(p[4]))
                {
                    uExtended = uIndex;
                    uIndex++;
                    uIndexWin++;
                    uOffset = RT_MAKE_U32_FROM_U8(p[8], p[9], p[10], p[11]);
                }
                else
                {
                    RTMsgError(Internal::tr("Logical partition chain broken"));
                    return VERR_INVALID_PARAMETER;
                }
            } while (uExtended != (unsigned)-1);
        }
    }


    /* Sort partitions in ascending order of start sector, plus a trivial
     * bit of consistency checking. */
    for (unsigned i = 0; i < pPart->cPartitions-1; i++)
    {
        unsigned uMinIdx = i;
        uint64_t uMinVal = pPart->aPartitions[i].uStart;
        for (unsigned j = i + 1; j < pPart->cPartitions; j++)
        {
            if (pPart->aPartitions[j].uStart < uMinVal)
            {
                uMinIdx = j;
                uMinVal = pPart->aPartitions[j].uStart;
            }
            else if (pPart->aPartitions[j].uStart == uMinVal)
            {
                RTMsgError(Internal::tr("Two partitions start at the same place"));
                return VERR_INVALID_PARAMETER;
            }
            else if (pPart->aPartitions[j].uStart == 0)
            {
                RTMsgError(Internal::tr("Partition starts at sector 0"));
                return VERR_INVALID_PARAMETER;
            }
        }
        if (uMinIdx != i)
        {
            /* Swap entries at index i and uMinIdx. */
            memcpy(&pPart->aPartitions[pPart->cPartitions],
                   &pPart->aPartitions[i], sizeof(HOSTPARTITION));
            memcpy(&pPart->aPartitions[i],
                   &pPart->aPartitions[uMinIdx], sizeof(HOSTPARTITION));
            memcpy(&pPart->aPartitions[uMinIdx],
                   &pPart->aPartitions[pPart->cPartitions], sizeof(HOSTPARTITION));
        }
    }

    /* Fill out partitioning location info for MBR or GPT. */
    pPart->aPartitions[0].uPartDataStart = 0;
    pPart->aPartitions[0].cPartDataSectors = pPart->aPartitions[0].uStart;

    /* Fill out partitioning location info for backup GPT. */
    if (partitioningType == VDISKPARTTYPE_GPT)
    {
        pPart->aPartitions[pPart->cPartitions-1].uPartDataStart = lastUsableLBA+1;
        pPart->aPartitions[pPart->cPartitions-1].cPartDataSectors = 33;

        /* Now do a some partition table consistency checking, to reject the most
         * obvious garbage which can lead to trouble later. */
        uint64_t uPrevEnd = 0;
        for (unsigned i = 0; i < pPart->cPartitions; i++)
        {
            if (pPart->aPartitions[i].cPartDataSectors)
                uPrevEnd = pPart->aPartitions[i].uPartDataStart + pPart->aPartitions[i].cPartDataSectors;
            if (pPart->aPartitions[i].uStart < uPrevEnd &&
                pPart->cPartitions-1 != i)
            {
                RTMsgError(Internal::tr("Overlapping GPT partitions"));
                return VERR_INVALID_PARAMETER;
            }
        }
    }
    else
    {
        /* Now do a some partition table consistency checking, to reject the most
         * obvious garbage which can lead to trouble later. */
        uint64_t uPrevEnd = 0;
        for (unsigned i = 0; i < pPart->cPartitions; i++)
        {
            if (pPart->aPartitions[i].cPartDataSectors)
                uPrevEnd = pPart->aPartitions[i].uPartDataStart + pPart->aPartitions[i].cPartDataSectors;
            if (pPart->aPartitions[i].uStart < uPrevEnd)
            {
                RTMsgError(Internal::tr("Overlapping MBR partitions"));
                return VERR_INVALID_PARAMETER;
            }
            if (!PARTTYPE_IS_EXTENDED(pPart->aPartitions[i].uType))
                uPrevEnd = pPart->aPartitions[i].uStart + pPart->aPartitions[i].uSize;
        }
    }

    return VINF_SUCCESS;
}

static RTEXITCODE CmdListPartitions(int argc, char **argv, ComPtr<IVirtualBox> aVirtualBox, ComPtr<ISession> aSession)
{
    RT_NOREF(aVirtualBox, aSession);
    Utf8Str rawdisk;

    /* let's have a closer look at the arguments */
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "-rawdisk") == 0)
        {
            if (argc <= i + 1)
            {
                return errorArgument(Internal::tr("Missing argument to '%s'"), argv[i]);
            }
            i++;
            rawdisk = argv[i];
        }
        else
        {
            return errorSyntaxInternal(USAGE_I_LISTPARTITIONS, Internal::tr("Invalid parameter '%s'"), argv[i]);
        }
    }

    if (rawdisk.isEmpty())
        return errorSyntaxInternal(USAGE_I_LISTPARTITIONS, Internal::tr("Mandatory parameter -rawdisk missing"));

    RTFILE hRawFile;
    int vrc = RTFileOpen(&hRawFile, rawdisk.c_str(), RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Internal::tr("Cannot open the raw disk: %Rrc"), vrc);

    HOSTPARTITIONS partitions;
    vrc = partRead(hRawFile, &partitions);
    /* Don't bail out on errors, print the table and return the result code. */

    RTPrintf(Internal::tr("Number  Type   StartCHS       EndCHS      Size (MiB)  Start (Sect)\n"));
    for (unsigned i = 0; i < partitions.cPartitions; i++)
    {
        /* Don't show the extended partition, otherwise users might think they
         * can add it to the list of partitions for raw partition access. */
        if (PARTTYPE_IS_EXTENDED(partitions.aPartitions[i].uType))
            continue;

        RTPrintf("%-7u %#04x  %-4u/%-3u/%-2u  %-4u/%-3u/%-2u    %10llu   %10llu\n",
                 partitions.aPartitions[i].uIndex,
                 partitions.aPartitions[i].uType,
                 partitions.aPartitions[i].uStartCylinder,
                 partitions.aPartitions[i].uStartHead,
                 partitions.aPartitions[i].uStartSector,
                 partitions.aPartitions[i].uEndCylinder,
                 partitions.aPartitions[i].uEndHead,
                 partitions.aPartitions[i].uEndSector,
                 partitions.aPartitions[i].uSize / 2048,
                 partitions.aPartitions[i].uStart);
    }

    return RT_SUCCESS(vrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static const RTGETOPTDEF g_aCreateRawVMDKOptions[] =
{
    { "--filename",     'f', RTGETOPT_REQ_STRING },
    { "-filename",      'f', RTGETOPT_REQ_STRING },
    { "--rawdisk",      'd', RTGETOPT_REQ_STRING },
    { "-rawdisk",       'd', RTGETOPT_REQ_STRING },
    { "--partitions",   'p', RTGETOPT_REQ_STRING },
    { "-partitions",    'p', RTGETOPT_REQ_STRING },
    { "--mbr",          'm', RTGETOPT_REQ_STRING },
    { "-mbr",           'm', RTGETOPT_REQ_STRING },
#if defined(RT_OS_LINUX) || defined(RT_OS_FREEBSD) || defined(RT_OS_WINDOWS)
    { "--relative",     'r', RTGETOPT_REQ_NOTHING },
    { "-relative",      'r', RTGETOPT_REQ_NOTHING },
#endif /* RT_OS_LINUX || RT_OS_FREEBSD || RT_OS_WINDOWS */
};

static RTEXITCODE CmdCreateRawVMDK(int argc, char **argv, HandlerArg *a)
{
    const char *pszFilename = NULL;
    const char *pszRawdisk = NULL;
    const char *pszPartitions = NULL;
    const char *pszMbr = NULL;
    bool fRelative = false;
    int c;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, g_aCreateRawVMDKOptions, RT_ELEMENTS(g_aCreateRawVMDKOptions), 0, 0);
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 'f':   // --filename
                pszFilename = ValueUnion.psz;
                break;

            case 'd':   // --rawdisk
                pszRawdisk = ValueUnion.psz;
                break;

            case 'p':   // --partitions
                pszPartitions = ValueUnion.psz;
                break;

            case 'm':   // --mbr
                pszMbr = ValueUnion.psz;
                break;
#if defined(RT_OS_LINUX) || defined(RT_OS_FREEBSD) || defined(RT_OS_WINDOWS)
            case 'r':   // --relative
                fRelative = true;
                break;
#endif /* RT_OS_LINUX || RT_OS_FREEBSD || RT_OS_WINDOWS */

            default:
                return errorGetOptInternal(USAGE_I_CREATERAWVMDK, c, &ValueUnion);
        }
    }

    if (!pszFilename || !*pszFilename)
        return errorSyntaxInternal(USAGE_I_CREATERAWVMDK, Internal::tr("Mandatory parameter --filename missing"));
    if (!pszRawdisk || !*pszRawdisk)
        return errorSyntaxInternal(USAGE_I_CREATERAWVMDK, Internal::tr("Mandatory parameter --rawdisk missing"));
    if (!pszPartitions && pszMbr)
        return errorSyntaxInternal(USAGE_I_CREATERAWVMDK,
                           Internal::tr("The parameter --mbr is only valid when the parameter -partitions is also present"));

    /* Construct the equivalent 'VBoxManage createmedium disk --variant RawDisk ...' command line. */
    size_t cMaxArgs = 9; /* all possible 'createmedium' args based on the 'createrawvmdk' options + 1 for NULL */
    char **papszNewArgv = (char **)RTMemAllocZ(sizeof(papszNewArgv[0]) * cMaxArgs);
    if (!papszNewArgv)
            return RTMsgErrorExit(RTEXITCODE_FAILURE, Internal::tr("Failed to allocate memory for argument array"));
    int cArgs = 0;

    papszNewArgv[cArgs++] = RTStrDup("disk");
    papszNewArgv[cArgs++] = RTStrDup("--variant=RawDisk");
    papszNewArgv[cArgs++] = RTStrDup("--format=VMDK");

    for (int i = 0; i < cArgs; i++)
        if (!papszNewArgv[i])
            return RTMsgErrorExit(RTEXITCODE_FAILURE, Internal::tr("Failed to allocate memory for argument array"));

    if (   RTStrAPrintf(&papszNewArgv[cArgs++], "--filename=%s", pszFilename) == -1
        || RTStrAPrintf(&papszNewArgv[cArgs++], "--property=RawDrive=%s", pszRawdisk) == -1
        || (pszPartitions && (RTStrAPrintf(&papszNewArgv[cArgs++], "--property=Partitions=%s", pszPartitions) == -1))
        || (pszMbr && (RTStrAPrintf(&papszNewArgv[cArgs++], "--property-filename=%s", pszMbr) == -1))
        || (fRelative && (RTStrAPrintf(&papszNewArgv[cArgs++], "--property=Relative=%d", fRelative) == -1)))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Internal::tr("Failed to allocate memory for argument array"));

    papszNewArgv[cArgs] = NULL;

    RTStrmPrintf(g_pStdErr,
                 Internal::tr("\nThe 'createrawvdk' subcommand is deprecated.  The equivalent functionality is\n"
                              "available using the 'VBoxManage createmedium' command and should be used\n"
                              "instead.  See 'VBoxManage help createmedium' for details.\n\n"));

    a->argc = cArgs;
    a->argv = papszNewArgv;
    RTEXITCODE rcExit = handleCreateMedium(a);

    for (int i = 0; i < cArgs; i++)
        RTStrFree(papszNewArgv[i]);
    RTMemFree(papszNewArgv);

    return rcExit;
}

static RTEXITCODE CmdRenameVMDK(int argc, char **argv, ComPtr<IVirtualBox> aVirtualBox, ComPtr<ISession> aSession)
{
    RT_NOREF(aVirtualBox, aSession);
    Utf8Str src;
    Utf8Str dst;
    /* Parse the arguments. */
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "-from") == 0)
        {
            if (argc <= i + 1)
            {
                return errorArgument(Internal::tr("Missing argument to '%s'"), argv[i]);
            }
            i++;
            src = argv[i];
        }
        else if (strcmp(argv[i], "-to") == 0)
        {
            if (argc <= i + 1)
            {
                return errorArgument(Internal::tr("Missing argument to '%s'"), argv[i]);
            }
            i++;
            dst = argv[i];
        }
        else
        {
            return errorSyntaxInternal(USAGE_I_RENAMEVMDK, Internal::tr("Invalid parameter '%s'"), argv[i]);
        }
    }

    if (src.isEmpty())
        return errorSyntaxInternal(USAGE_I_RENAMEVMDK, Internal::tr("Mandatory parameter -from missing"));
    if (dst.isEmpty())
        return errorSyntaxInternal(USAGE_I_RENAMEVMDK, Internal::tr("Mandatory parameter -to missing"));

    PVDISK pDisk = NULL;

    PVDINTERFACE     pVDIfs = NULL;
    VDINTERFACEERROR vdInterfaceError;
    vdInterfaceError.pfnError     = handleVDError;
    vdInterfaceError.pfnMessage   = handleVDMessage;

    int vrc = VDInterfaceAdd(&vdInterfaceError.Core, "VBoxManage_IError", VDINTERFACETYPE_ERROR,
                             NULL, sizeof(VDINTERFACEERROR), &pVDIfs);
    AssertRC(vrc);

    vrc = VDCreate(pVDIfs, VDTYPE_HDD, &pDisk);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Internal::tr("Cannot create the virtual disk container: %Rrc"), vrc);

    vrc = VDOpen(pDisk, "VMDK", src.c_str(), VD_OPEN_FLAGS_NORMAL, NULL);
    if (RT_SUCCESS(vrc))
    {
        vrc = VDCopy(pDisk, 0, pDisk, "VMDK", dst.c_str(), true, 0,
                     VD_IMAGE_FLAGS_NONE, NULL, VD_OPEN_FLAGS_NORMAL,
                     NULL, NULL, NULL);
        if (RT_FAILURE(vrc))
            RTMsgError(Internal::tr("Cannot rename the image: %Rrc"), vrc);
    }
    else
        RTMsgError(Internal::tr("Cannot create the source image: %Rrc"), vrc);
    VDCloseAll(pDisk);
    return RT_SUCCESS(vrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static RTEXITCODE CmdConvertToRaw(int argc, char **argv, ComPtr<IVirtualBox> aVirtualBox, ComPtr<ISession> aSession)
{
    RT_NOREF(aVirtualBox, aSession);
    Utf8Str srcformat;
    Utf8Str src;
    Utf8Str dst;
    bool fWriteToStdOut = false;

    /* Parse the arguments. */
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "-format") == 0)
        {
            if (argc <= i + 1)
            {
                return errorArgument(Internal::tr("Missing argument to '%s'"), argv[i]);
            }
            i++;
            srcformat = argv[i];
        }
        else if (src.isEmpty())
        {
            src = argv[i];
        }
        else if (dst.isEmpty())
        {
            dst = argv[i];
#ifdef ENABLE_CONVERT_RAW_TO_STDOUT
            if (!strcmp(argv[i], "stdout"))
                fWriteToStdOut = true;
#endif /* ENABLE_CONVERT_RAW_TO_STDOUT */
        }
        else
        {
            return errorSyntaxInternal(USAGE_I_CONVERTTORAW, Internal::tr("Invalid parameter '%s'"), argv[i]);
        }
    }

    if (src.isEmpty())
        return errorSyntaxInternal(USAGE_I_CONVERTTORAW, Internal::tr("Mandatory filename parameter missing"));
    if (dst.isEmpty())
        return errorSyntaxInternal(USAGE_I_CONVERTTORAW, Internal::tr("Mandatory outputfile parameter missing"));

    PVDISK pDisk = NULL;

    PVDINTERFACE     pVDIfs = NULL;
    VDINTERFACEERROR vdInterfaceError;
    vdInterfaceError.pfnError     = handleVDError;
    vdInterfaceError.pfnMessage   = handleVDMessage;

    int vrc = VDInterfaceAdd(&vdInterfaceError.Core, "VBoxManage_IError", VDINTERFACETYPE_ERROR,
                             NULL, sizeof(VDINTERFACEERROR), &pVDIfs);
    AssertRC(vrc);

    /** @todo Support convert to raw for floppy and DVD images too. */
    vrc = VDCreate(pVDIfs, VDTYPE_HDD, &pDisk);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Internal::tr("Cannot create the virtual disk container: %Rrc"), vrc);

    /* Open raw output file. */
    RTFILE outFile;
    vrc = VINF_SUCCESS;
    if (fWriteToStdOut)
        vrc = RTFileFromNative(&outFile, 1);
    else
        vrc = RTFileOpen(&outFile, dst.c_str(), RTFILE_O_WRITE | RTFILE_O_CREATE | RTFILE_O_DENY_ALL);
    if (RT_FAILURE(vrc))
    {
        VDCloseAll(pDisk);
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Internal::tr("Cannot create destination file \"%s\": %Rrc"),
                              dst.c_str(), vrc);
    }

    if (srcformat.isEmpty())
    {
        char *pszFormat = NULL;
        VDTYPE enmType = VDTYPE_INVALID;
        vrc = VDGetFormat(NULL /* pVDIfsDisk */, NULL /* pVDIfsImage */,
                          src.c_str(), VDTYPE_INVALID, &pszFormat, &enmType);
        if (RT_FAILURE(vrc) || enmType != VDTYPE_HDD)
        {
            VDCloseAll(pDisk);
            if (!fWriteToStdOut)
            {
                RTFileClose(outFile);
                RTFileDelete(dst.c_str());
            }
            if (RT_FAILURE(vrc))
                RTMsgError(Internal::tr("No file format specified and autodetect failed - please specify format: %Rrc"),
                           vrc);
            else
                RTMsgError(Internal::tr("Only converting harddisk images is supported"));
            return RTEXITCODE_FAILURE;
        }
        srcformat = pszFormat;
        RTStrFree(pszFormat);
    }
    vrc = VDOpen(pDisk, srcformat.c_str(), src.c_str(), VD_OPEN_FLAGS_READONLY, NULL);
    if (RT_FAILURE(vrc))
    {
        VDCloseAll(pDisk);
        if (!fWriteToStdOut)
        {
            RTFileClose(outFile);
            RTFileDelete(dst.c_str());
        }
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Internal::tr("Cannot open the source image: %Rrc"), vrc);
    }

    uint64_t cbSize = VDGetSize(pDisk, VD_LAST_IMAGE);
    uint64_t offFile = 0;
#define RAW_BUFFER_SIZE _128K
    size_t cbBuf = RAW_BUFFER_SIZE;
    void *pvBuf = RTMemAlloc(cbBuf);
    if (pvBuf)
    {
        RTStrmPrintf(g_pStdErr, Internal::tr("Converting image \"%s\" with size %RU64 bytes (%RU64MB) to raw...\n", "", cbSize),
                     src.c_str(), cbSize, (cbSize + _1M - 1) / _1M);
        while (offFile < cbSize)
        {
            size_t cb = (size_t)RT_MIN(cbSize - offFile, cbBuf);
            vrc = VDRead(pDisk, offFile, pvBuf, cb);
            if (RT_FAILURE(vrc))
                break;
            vrc = RTFileWrite(outFile, pvBuf, cb, NULL);
            if (RT_FAILURE(vrc))
                break;
            offFile += cb;
        }
        RTMemFree(pvBuf);
        if (RT_FAILURE(vrc))
        {
            VDCloseAll(pDisk);
            if (!fWriteToStdOut)
            {
                RTFileClose(outFile);
                RTFileDelete(dst.c_str());
            }
            return RTMsgErrorExit(RTEXITCODE_FAILURE, Internal::tr("Cannot copy image data: %Rrc"), vrc);
        }
    }
    else
    {
        vrc = VERR_NO_MEMORY;
        VDCloseAll(pDisk);
        if (!fWriteToStdOut)
        {
            RTFileClose(outFile);
            RTFileDelete(dst.c_str());
        }
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Internal::tr("Out of memory allocating read buffer"));
    }

    if (!fWriteToStdOut)
        RTFileClose(outFile);
    VDCloseAll(pDisk);
    return RTEXITCODE_SUCCESS;
}

static RTEXITCODE CmdConvertHardDisk(int argc, char **argv, ComPtr<IVirtualBox> aVirtualBox, ComPtr<ISession> aSession)
{
    RT_NOREF(aVirtualBox, aSession);
    Utf8Str srcformat;
    Utf8Str dstformat;
    Utf8Str src;
    Utf8Str dst;
    int vrc;
    PVDISK pSrcDisk = NULL;
    PVDISK pDstDisk = NULL;
    VDTYPE enmSrcType = VDTYPE_INVALID;

    /* Parse the arguments. */
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "-srcformat") == 0)
        {
            if (argc <= i + 1)
            {
                return errorArgument(Internal::tr("Missing argument to '%s'"), argv[i]);
            }
            i++;
            srcformat = argv[i];
        }
        else if (strcmp(argv[i], "-dstformat") == 0)
        {
            if (argc <= i + 1)
            {
                return errorArgument(Internal::tr("Missing argument to '%s'"), argv[i]);
            }
            i++;
            dstformat = argv[i];
        }
        else if (src.isEmpty())
        {
            src = argv[i];
        }
        else if (dst.isEmpty())
        {
            dst = argv[i];
        }
        else
        {
            return errorSyntaxInternal(USAGE_I_CONVERTHD, Internal::tr("Invalid parameter '%s'"), argv[i]);
        }
    }

    if (src.isEmpty())
        return errorSyntaxInternal(USAGE_I_CONVERTHD, Internal::tr("Mandatory input image parameter missing"));
    if (dst.isEmpty())
        return errorSyntaxInternal(USAGE_I_CONVERTHD, Internal::tr("Mandatory output image parameter missing"));


    PVDINTERFACE     pVDIfs = NULL;
    VDINTERFACEERROR vdInterfaceError;
    vdInterfaceError.pfnError     = handleVDError;
    vdInterfaceError.pfnMessage   = handleVDMessage;

    vrc = VDInterfaceAdd(&vdInterfaceError.Core, "VBoxManage_IError", VDINTERFACETYPE_ERROR,
                         NULL, sizeof(VDINTERFACEERROR), &pVDIfs);
    AssertRC(vrc);

    do
    {
        /* Try to determine input image format */
        if (srcformat.isEmpty())
        {
            char *pszFormat = NULL;
            vrc = VDGetFormat(NULL /* pVDIfsDisk */, NULL /* pVDIfsImage */,
                              src.c_str(), VDTYPE_HDD, &pszFormat, &enmSrcType);
            if (RT_FAILURE(vrc))
            {
                RTMsgError(Internal::tr("No file format specified and autodetect failed - please specify format: %Rrc"),
                           vrc);
                break;
            }
            srcformat = pszFormat;
            RTStrFree(pszFormat);
        }

        vrc = VDCreate(pVDIfs, enmSrcType, &pSrcDisk);
        if (RT_FAILURE(vrc))
        {
            RTMsgError(Internal::tr("Cannot create the source virtual disk container: %Rrc"), vrc);
            break;
        }

        /* Open the input image */
        vrc = VDOpen(pSrcDisk, srcformat.c_str(), src.c_str(), VD_OPEN_FLAGS_READONLY, NULL);
        if (RT_FAILURE(vrc))
        {
            RTMsgError(Internal::tr("Cannot open the source image: %Rrc"), vrc);
            break;
        }

        /* Output format defaults to VDI */
        if (dstformat.isEmpty())
            dstformat = "VDI";

        vrc = VDCreate(pVDIfs, enmSrcType, &pDstDisk);
        if (RT_FAILURE(vrc))
        {
            RTMsgError(Internal::tr("Cannot create the destination virtual disk container: %Rrc"), vrc);
            break;
        }

        uint64_t cbSize = VDGetSize(pSrcDisk, VD_LAST_IMAGE);
        RTStrmPrintf(g_pStdErr, Internal::tr("Converting image \"%s\" with size %RU64 bytes (%RU64MB)...\n", "", cbSize),
                     src.c_str(), cbSize, (cbSize + _1M - 1) / _1M);

        /* Create the output image */
        vrc = VDCopy(pSrcDisk, VD_LAST_IMAGE, pDstDisk, dstformat.c_str(),
                     dst.c_str(), false, 0, VD_VMDK_IMAGE_FLAGS_STREAM_OPTIMIZED,
                     NULL, VD_OPEN_FLAGS_NORMAL, NULL, NULL, NULL);
        if (RT_FAILURE(vrc))
        {
            RTMsgError(Internal::tr("Cannot copy the image: %Rrc"), vrc);
            break;
        }
    }
    while (0);
    if (pDstDisk)
        VDCloseAll(pDstDisk);
    if (pSrcDisk)
        VDCloseAll(pSrcDisk);

    return RT_SUCCESS(vrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

/**
 * Tries to repair a corrupted hard disk image.
 *
 * @returns VBox status code
 */
static RTEXITCODE CmdRepairHardDisk(int argc, char **argv, ComPtr<IVirtualBox> aVirtualBox, ComPtr<ISession> aSession)
{
    RT_NOREF(aVirtualBox, aSession);
    Utf8Str image;
    Utf8Str format;
    int vrc;
    bool fDryRun = false;

    /* Parse the arguments. */
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(argv[i], "-dry-run") == 0)
        {
            fDryRun = true;
        }
        else if (strcmp(argv[i], "-format") == 0)
        {
            if (argc <= i + 1)
            {
                return errorArgument(Internal::tr("Missing argument to '%s'"), argv[i]);
            }
            i++;
            format = argv[i];
        }
        else if (image.isEmpty())
        {
            image = argv[i];
        }
        else
        {
            return errorSyntaxInternal(USAGE_I_REPAIRHD, Internal::tr("Invalid parameter '%s'"), argv[i]);
        }
    }

    if (image.isEmpty())
        return errorSyntaxInternal(USAGE_I_REPAIRHD, Internal::tr("Mandatory input image parameter missing"));

    PVDINTERFACE     pVDIfs = NULL;
    VDINTERFACEERROR vdInterfaceError;
    vdInterfaceError.pfnError     = handleVDError;
    vdInterfaceError.pfnMessage   = handleVDMessage;

    vrc = VDInterfaceAdd(&vdInterfaceError.Core, "VBoxManage_IError", VDINTERFACETYPE_ERROR,
                         NULL, sizeof(VDINTERFACEERROR), &pVDIfs);
    AssertRC(vrc);

    do
    {
        /* Try to determine input image format */
        if (format.isEmpty())
        {
            char *pszFormat = NULL;
            VDTYPE enmSrcType = VDTYPE_INVALID;

            vrc = VDGetFormat(NULL /* pVDIfsDisk */, NULL /* pVDIfsImage */,
                              image.c_str(), VDTYPE_HDD, &pszFormat, &enmSrcType);
            if (RT_FAILURE(vrc) && (vrc != VERR_VD_IMAGE_CORRUPTED))
            {
                RTMsgError(Internal::tr("No file format specified and autodetect failed - please specify format: %Rrc"),
                           vrc);
                break;
            }
            format = pszFormat;
            RTStrFree(pszFormat);
        }

        uint32_t fFlags = 0;
        if (fDryRun)
            fFlags |= VD_REPAIR_DRY_RUN;

        vrc = VDRepair(pVDIfs, NULL, image.c_str(), format.c_str(), fFlags);
    }
    while (0);

    return RT_SUCCESS(vrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

/**
 * Unloads the necessary driver.
 *
 * @returns VBox status code
 */
static RTEXITCODE CmdModUninstall(void)
{
    int vrc = SUPR3Uninstall();
    if (RT_SUCCESS(vrc) || vrc == VERR_NOT_IMPLEMENTED)
        return RTEXITCODE_SUCCESS;
    return RTEXITCODE_FAILURE;
}

/**
 * Loads the necessary driver.
 *
 * @returns VBox status code
 */
static RTEXITCODE CmdModInstall(void)
{
    int vrc = SUPR3Install();
    if (RT_SUCCESS(vrc) || vrc == VERR_NOT_IMPLEMENTED)
        return RTEXITCODE_SUCCESS;
    return RTEXITCODE_FAILURE;
}

static RTEXITCODE CmdDebugLog(int argc, char **argv, ComPtr<IVirtualBox> aVirtualBox, ComPtr<ISession> aSession)
{
    /*
     * The first parameter is the name or UUID of a VM with a direct session
     * that we wish to open.
     */
    if (argc < 1)
        return errorSyntaxInternal(USAGE_I_DEBUGLOG, Internal::tr("Missing VM name/UUID"));

    ComPtr<IMachine> ptrMachine;
    HRESULT hrc;
    CHECK_ERROR_RET(aVirtualBox, FindMachine(Bstr(argv[0]).raw(),
                                             ptrMachine.asOutParam()), RTEXITCODE_FAILURE);

    CHECK_ERROR_RET(ptrMachine, LockMachine(aSession, LockType_Shared), RTEXITCODE_FAILURE);

    /*
     * Get the debugger interface.
     */
    ComPtr<IConsole> ptrConsole;
    CHECK_ERROR_RET(aSession, COMGETTER(Console)(ptrConsole.asOutParam()), RTEXITCODE_FAILURE);

    ComPtr<IMachineDebugger> ptrDebugger;
    CHECK_ERROR_RET(ptrConsole, COMGETTER(Debugger)(ptrDebugger.asOutParam()), RTEXITCODE_FAILURE);

    /*
     * Parse the command.
     */
    bool                fEnablePresent = false;
    bool                fEnable        = false;
    bool                fFlagsPresent  = false;
    RTCString    strFlags;
    bool                fGroupsPresent = false;
    RTCString    strGroups;
    bool                fDestsPresent  = false;
    RTCString    strDests;

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--disable",      'E', RTGETOPT_REQ_NOTHING },
        { "--enable",       'e', RTGETOPT_REQ_NOTHING },
        { "--flags",        'f', RTGETOPT_REQ_STRING  },
        { "--groups",       'g', RTGETOPT_REQ_STRING  },
        { "--destinations", 'd', RTGETOPT_REQ_STRING  }
    };

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'e':
                fEnablePresent = true;
                fEnable = true;
                break;

            case 'E':
                fEnablePresent = true;
                fEnable = false;
                break;

            case 'f':
                fFlagsPresent = true;
                if (*ValueUnion.psz)
                {
                    if (strFlags.isNotEmpty())
                        strFlags.append(' ');
                    strFlags.append(ValueUnion.psz);
                }
                break;

            case 'g':
                fGroupsPresent = true;
                if (*ValueUnion.psz)
                {
                    if (strGroups.isNotEmpty())
                        strGroups.append(' ');
                    strGroups.append(ValueUnion.psz);
                }
                break;

            case 'd':
                fDestsPresent = true;
                if (*ValueUnion.psz)
                {
                    if (strDests.isNotEmpty())
                        strDests.append(' ');
                    strDests.append(ValueUnion.psz);
                }
                break;

            default:
                return errorGetOptInternal(USAGE_I_DEBUGLOG, ch, &ValueUnion);
        }
    }

    /*
     * Do the job.
     */
    if (fEnablePresent && !fEnable)
        CHECK_ERROR_RET(ptrDebugger, COMSETTER(LogEnabled)(FALSE), RTEXITCODE_FAILURE);

    /** @todo flags, groups destination. */
    if (fFlagsPresent || fGroupsPresent || fDestsPresent)
        RTMsgWarning(Internal::tr("One or more of the requested features are not implemented! Feel free to do this."));

    if (fEnablePresent && fEnable)
        CHECK_ERROR_RET(ptrDebugger, COMSETTER(LogEnabled)(TRUE), RTEXITCODE_FAILURE);
    return RTEXITCODE_SUCCESS;
}

/**
 * Generate a SHA-256 password hash
 */
static RTEXITCODE CmdGeneratePasswordHash(int argc, char **argv, ComPtr<IVirtualBox> aVirtualBox, ComPtr<ISession> aSession)
{
    RT_NOREF(aVirtualBox, aSession);

    /* one parameter, the password to hash */
    if (argc != 1)
        return errorSyntaxInternal(USAGE_I_PASSWORDHASH, Internal::tr("password to hash required"));

    uint8_t abDigest[RTSHA256_HASH_SIZE];
    RTSha256(argv[0], strlen(argv[0]), abDigest);
    char pszDigest[RTSHA256_DIGEST_LEN + 1];
    RTSha256ToString(abDigest, pszDigest, sizeof(pszDigest));
    RTPrintf(Internal::tr("Password hash: %s\n"), pszDigest);

    return RTEXITCODE_SUCCESS;
}

/**
 * Print internal guest statistics or
 * set internal guest statistics update interval if specified
 */
static RTEXITCODE CmdGuestStats(int argc, char **argv, ComPtr<IVirtualBox> aVirtualBox, ComPtr<ISession> aSession)
{
    /* one parameter, guest name */
    if (argc < 1)
        return errorSyntaxInternal(USAGE_I_GUESTSTATS, Internal::tr("Missing VM name/UUID"));

    /*
     * Parse the command.
     */
    ULONG aUpdateInterval = 0;

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--interval", 'i', RTGETOPT_REQ_UINT32  }
    };

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case 'i':
                aUpdateInterval = ValueUnion.u32;
                break;

            default:
                return errorGetOptInternal(USAGE_I_GUESTSTATS, ch, &ValueUnion);
        }
    }

    if (argc > 1 && aUpdateInterval == 0)
        return errorSyntaxInternal(USAGE_I_GUESTSTATS, Internal::tr("Invalid update interval specified"));

    RTPrintf(Internal::tr("argc=%d interval=%u\n"), argc, aUpdateInterval);

    ComPtr<IMachine> ptrMachine;
    HRESULT hrc;
    CHECK_ERROR_RET(aVirtualBox, FindMachine(Bstr(argv[0]).raw(),
                                             ptrMachine.asOutParam()), RTEXITCODE_FAILURE);

    CHECK_ERROR_RET(ptrMachine, LockMachine(aSession, LockType_Shared), RTEXITCODE_FAILURE);

    /*
     * Get the guest interface.
     */
    ComPtr<IConsole> ptrConsole;
    CHECK_ERROR_RET(aSession, COMGETTER(Console)(ptrConsole.asOutParam()), RTEXITCODE_FAILURE);

    ComPtr<IGuest> ptrGuest;
    CHECK_ERROR_RET(ptrConsole, COMGETTER(Guest)(ptrGuest.asOutParam()), RTEXITCODE_FAILURE);

    if (aUpdateInterval)
        CHECK_ERROR_RET(ptrGuest, COMSETTER(StatisticsUpdateInterval)(aUpdateInterval), RTEXITCODE_FAILURE);
    else
    {
        ULONG mCpuUser, mCpuKernel, mCpuIdle;
        ULONG mMemTotal, mMemFree, mMemBalloon, mMemShared, mMemCache, mPageTotal;
        ULONG ulMemAllocTotal, ulMemFreeTotal, ulMemBalloonTotal, ulMemSharedTotal;

        CHECK_ERROR_RET(ptrGuest, InternalGetStatistics(&mCpuUser, &mCpuKernel, &mCpuIdle,
                                                        &mMemTotal, &mMemFree, &mMemBalloon, &mMemShared, &mMemCache,
                                                        &mPageTotal, &ulMemAllocTotal, &ulMemFreeTotal,
                                                        &ulMemBalloonTotal, &ulMemSharedTotal),
                        RTEXITCODE_FAILURE);
        RTPrintf("mCpuUser=%u mCpuKernel=%u mCpuIdle=%u\n"
                 "mMemTotal=%u mMemFree=%u mMemBalloon=%u mMemShared=%u mMemCache=%u\n"
                 "mPageTotal=%u ulMemAllocTotal=%u ulMemFreeTotal=%u ulMemBalloonTotal=%u ulMemSharedTotal=%u\n",
                 mCpuUser, mCpuKernel, mCpuIdle,
                 mMemTotal, mMemFree, mMemBalloon, mMemShared, mMemCache,
                 mPageTotal, ulMemAllocTotal, ulMemFreeTotal, ulMemBalloonTotal, ulMemSharedTotal);

    }

    return RTEXITCODE_SUCCESS;
}


/**
 * Wrapper for handling internal commands
 */
RTEXITCODE handleInternalCommands(HandlerArg *a)
{
    /* at least a command is required */
    if (a->argc < 1)
        return errorSyntaxInternal(USAGE_I_ALL, Internal::tr("Command missing"));

    /*
     * The 'string switch' on command name.
     */
    const char *pszCmd = a->argv[0];
    if (!strcmp(pszCmd, "loadmap"))
        return CmdLoadMap(a->argc - 1, &a->argv[1], a->virtualBox, a->session);
    if (!strcmp(pszCmd, "loadsyms"))
        return CmdLoadSyms(a->argc - 1, &a->argv[1], a->virtualBox, a->session);
    //if (!strcmp(pszCmd, "unloadsyms"))
    //    return CmdUnloadSyms(argc - 1, &a->argv[1]);
    if (!strcmp(pszCmd, "sethduuid") || !strcmp(pszCmd, "sethdparentuuid"))
        return CmdSetHDUUID(a->argc, &a->argv[0], a->virtualBox, a->session);
    if (!strcmp(pszCmd, "dumphdinfo"))
        return CmdDumpHDInfo(a->argc - 1, &a->argv[1], a->virtualBox, a->session);
    if (!strcmp(pszCmd, "listpartitions"))
        return CmdListPartitions(a->argc - 1, &a->argv[1], a->virtualBox, a->session);
    if (!strcmp(pszCmd, "createrawvmdk"))
        return CmdCreateRawVMDK(a->argc - 1, &a->argv[1], a);
    if (!strcmp(pszCmd, "renamevmdk"))
        return CmdRenameVMDK(a->argc - 1, &a->argv[1], a->virtualBox, a->session);
    if (!strcmp(pszCmd, "converttoraw"))
        return CmdConvertToRaw(a->argc - 1, &a->argv[1], a->virtualBox, a->session);
    if (!strcmp(pszCmd, "converthd"))
        return CmdConvertHardDisk(a->argc - 1, &a->argv[1], a->virtualBox, a->session);
    if (!strcmp(pszCmd, "modinstall"))
        return CmdModInstall();
    if (!strcmp(pszCmd, "moduninstall"))
        return CmdModUninstall();
    if (!strcmp(pszCmd, "debuglog"))
        return CmdDebugLog(a->argc - 1, &a->argv[1], a->virtualBox, a->session);
    if (!strcmp(pszCmd, "passwordhash"))
        return CmdGeneratePasswordHash(a->argc - 1, &a->argv[1], a->virtualBox, a->session);
    if (!strcmp(pszCmd, "gueststats"))
        return CmdGuestStats(a->argc - 1, &a->argv[1], a->virtualBox, a->session);
    if (!strcmp(pszCmd, "repairhd"))
        return CmdRepairHardDisk(a->argc - 1, &a->argv[1], a->virtualBox, a->session);

    /* default: */
    return errorSyntaxInternal(USAGE_I_ALL, Internal::tr("Invalid command '%s'"), a->argv[0]);
}
