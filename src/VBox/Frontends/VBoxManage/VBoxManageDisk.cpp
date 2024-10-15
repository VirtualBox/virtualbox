/* $Id: VBoxManageDisk.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxManage - The disk/medium related commands.
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
#include <VBox/com/array.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>
#include <VBox/com/VirtualBox.h>

#include <iprt/asm.h>
#include <iprt/base64.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/param.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/ctype.h>
#include <iprt/getopt.h>
#include <VBox/log.h>
#include <VBox/vd.h>

#include <list>

#include "VBoxManage.h"
using namespace com;

/** Medium category. */
typedef enum MEDIUMCATEGORY
{
    MEDIUMCATEGORY_NONE = 0,
    MEDIUMCATEGORY_DISK,
    MEDIUMCATEGORY_DVD,
    MEDIUMCATEGORY_FLOPPY
} MEDIUMCATEGORY;

DECLARE_TRANSLATION_CONTEXT(Disk);

// funcs
///////////////////////////////////////////////////////////////////////////////


static DECLCALLBACK(void) handleVDError(void *pvUser, int vrc, RT_SRC_POS_DECL, const char *pszFormat, va_list va)
{
    RT_NOREF(pvUser);
    RTMsgErrorV(pszFormat, va);
    RTMsgError(Disk::tr("Error code %Rrc at %s(%u) in function %s"), vrc, RT_SRC_POS_ARGS);
}

static int parseMediumVariant(const char *psz, MediumVariant_T *pMediumVariant)
{
    int vrc = VINF_SUCCESS;
    unsigned uMediumVariant = (unsigned)*pMediumVariant;
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
            // Parsing is intentionally inconsistent: "standard" resets the
            // variant, whereas the other flags are cumulative.
            if (!RTStrNICmp(psz, "standard", len))
                uMediumVariant = MediumVariant_Standard;
            else if (   !RTStrNICmp(psz, "fixed", len)
                     || !RTStrNICmp(psz, "static", len))
                uMediumVariant |= MediumVariant_Fixed;
            else if (!RTStrNICmp(psz, "Diff", len))
                uMediumVariant |= MediumVariant_Diff;
            else if (!RTStrNICmp(psz, "split2g", len))
                uMediumVariant |= MediumVariant_VmdkSplit2G;
            else if (   !RTStrNICmp(psz, "stream", len)
                     || !RTStrNICmp(psz, "streamoptimized", len))
                uMediumVariant |= MediumVariant_VmdkStreamOptimized;
            else if (!RTStrNICmp(psz, "esx", len))
                uMediumVariant |= MediumVariant_VmdkESX;
            else if (!RTStrNICmp(psz, "formatted", len))
                uMediumVariant |= MediumVariant_Formatted;
            else if (   !RTStrNICmp(psz, "raw", len)
                     || !RTStrNICmp(psz, "rawdisk", len))
                uMediumVariant |= MediumVariant_VmdkRawDisk;
            else
                vrc = VERR_PARSE_ERROR;
        }
        if (pszComma)
            psz += len + 1;
        else
            psz += len;
    }

    if (RT_SUCCESS(vrc))
        *pMediumVariant = (MediumVariant_T)uMediumVariant;
    return vrc;
}

int parseMediumType(const char *psz, MediumType_T *penmMediumType)
{
    int vrc = VINF_SUCCESS;
    MediumType_T enmMediumType = MediumType_Normal;
    if (!RTStrICmp(psz, "normal"))
        enmMediumType = MediumType_Normal;
    else if (!RTStrICmp(psz, "immutable"))
        enmMediumType = MediumType_Immutable;
    else if (!RTStrICmp(psz, "writethrough"))
        enmMediumType = MediumType_Writethrough;
    else if (!RTStrICmp(psz, "shareable"))
        enmMediumType = MediumType_Shareable;
    else if (!RTStrICmp(psz, "readonly"))
        enmMediumType = MediumType_Readonly;
    else if (!RTStrICmp(psz, "multiattach"))
        enmMediumType = MediumType_MultiAttach;
    else
        vrc = VERR_PARSE_ERROR;

    if (RT_SUCCESS(vrc))
        *penmMediumType = enmMediumType;
    return vrc;
}

/** @todo move this into getopt, as getting bool values is generic */
int parseBool(const char *psz, bool *pb)
{
    int vrc = VINF_SUCCESS;
    if (    !RTStrICmp(psz, "on")
        ||  !RTStrICmp(psz, "yes")
        ||  !RTStrICmp(psz, "true")
        ||  !RTStrCmp(psz, "1")
        ||  !RTStrICmp(psz, "enable")
        ||  !RTStrICmp(psz, "enabled"))
        *pb = true;
    else if (   !RTStrICmp(psz, "off")
             || !RTStrICmp(psz, "no")
             || !RTStrICmp(psz, "false")
             || !RTStrCmp(psz, "0")
             || !RTStrICmp(psz, "disable")
             || !RTStrICmp(psz, "disabled"))
        *pb = false;
    else
        vrc = VERR_PARSE_ERROR;

    return vrc;
}

HRESULT openMedium(HandlerArg *a, const char *pszFilenameOrUuid,
                   DeviceType_T enmDevType, AccessMode_T enmAccessMode,
                   ComPtr<IMedium> &pMedium, bool fForceNewUuidOnOpen,
                   bool fSilent)
{
    HRESULT hrc;
    Guid id(pszFilenameOrUuid);
    char szFilenameAbs[RTPATH_MAX] = "";

    /* If it is no UUID, convert the filename to an absolute one. */
    if (!id.isValid())
    {
        int irc = RTPathAbs(pszFilenameOrUuid, szFilenameAbs, sizeof(szFilenameAbs));
        if (RT_FAILURE(irc))
        {
            if (!fSilent)
                RTMsgError(Disk::tr("Cannot convert filename \"%s\" to absolute path"), pszFilenameOrUuid);
            return E_FAIL;
        }
        pszFilenameOrUuid = szFilenameAbs;
    }

    if (!fSilent)
        CHECK_ERROR(a->virtualBox, OpenMedium(Bstr(pszFilenameOrUuid).raw(),
                                              enmDevType,
                                              enmAccessMode,
                                              fForceNewUuidOnOpen,
                                              pMedium.asOutParam()));
    else
        hrc = a->virtualBox->OpenMedium(Bstr(pszFilenameOrUuid).raw(),
                                        enmDevType,
                                        enmAccessMode,
                                        fForceNewUuidOnOpen,
                                        pMedium.asOutParam());

    return hrc;
}

static HRESULT createMedium(HandlerArg *a, const char *pszFormat,
                            const char *pszFilename, DeviceType_T enmDevType,
                            AccessMode_T enmAccessMode, ComPtr<IMedium> &pMedium)
{
    HRESULT hrc;
    char szFilenameAbs[RTPATH_MAX] = "";

    /** @todo laziness shortcut. should really check the MediumFormatCapabilities */
    if (RTStrICmp(pszFormat, "iSCSI"))
    {
        int irc = RTPathAbs(pszFilename, szFilenameAbs, sizeof(szFilenameAbs));
        if (RT_FAILURE(irc))
        {
            RTMsgError(Disk::tr("Cannot convert filename \"%s\" to absolute path"), pszFilename);
            return E_FAIL;
        }
        pszFilename = szFilenameAbs;
    }

    CHECK_ERROR(a->virtualBox, CreateMedium(Bstr(pszFormat).raw(),
                                            Bstr(pszFilename).raw(),
                                            enmAccessMode,
                                            enmDevType,
                                            pMedium.asOutParam()));
    return hrc;
}

static const RTGETOPTDEF g_aCreateMediumOptions[] =
{
    { "disk",           'H', RTGETOPT_REQ_NOTHING },
    { "dvd",            'D', RTGETOPT_REQ_NOTHING },
    { "floppy",         'L', RTGETOPT_REQ_NOTHING },
    { "--filename",     'f', RTGETOPT_REQ_STRING },
    { "-filename",      'f', RTGETOPT_REQ_STRING },     // deprecated
    { "--diffparent",   'd', RTGETOPT_REQ_STRING },
    { "--size",         's', RTGETOPT_REQ_UINT64 },
    { "-size",          's', RTGETOPT_REQ_UINT64 },     // deprecated
    { "--sizebyte",     'S', RTGETOPT_REQ_UINT64 },
    { "--format",       'o', RTGETOPT_REQ_STRING },
    { "-format",        'o', RTGETOPT_REQ_STRING },     // deprecated
    { "--static",       'F', RTGETOPT_REQ_NOTHING },
    { "-static",        'F', RTGETOPT_REQ_NOTHING },    // deprecated
    { "--variant",      'm', RTGETOPT_REQ_STRING },
    { "-variant",       'm', RTGETOPT_REQ_STRING },     // deprecated
    { "--property",     'p', RTGETOPT_REQ_STRING },
    { "--property-file",'P', RTGETOPT_REQ_STRING },
};

class MediumProperty
{
public:
    const char *m_pszKey;
    const char *m_pszValue; /**< Can be binary too. */
    size_t      m_cbValue;
    char       *m_pszFreeValue;
    MediumProperty() : m_pszKey(NULL), m_pszValue(NULL), m_cbValue(0), m_pszFreeValue(NULL) { }
    MediumProperty(MediumProperty const &a_rThat)
        : m_pszKey(a_rThat.m_pszKey)
        , m_pszValue(a_rThat.m_pszValue)
        , m_cbValue(a_rThat.m_cbValue)
        , m_pszFreeValue(NULL)
    {
        Assert(a_rThat.m_pszFreeValue == NULL); /* not expected here! */
    }
    ~MediumProperty()
    {
        RTMemFree(m_pszFreeValue);
        m_pszFreeValue = NULL;
    }

private:
    MediumProperty &operator=(MediumProperty const &a_rThat)
    {
        m_pszKey = a_rThat.m_pszKey;
        m_pszValue = a_rThat.m_pszValue;
        m_cbValue = a_rThat.m_cbValue;
        m_pszFreeValue = a_rThat.m_pszFreeValue;
        if (a_rThat.m_pszFreeValue != NULL)
        {
            m_pszFreeValue = (char *)RTMemDup(m_pszValue, m_cbValue + 1);
            if (!m_pszFreeValue)
            {
                RTMsgError(Disk::tr("Out of memory copying '%s'"), m_pszValue);
                throw std::bad_alloc();
            }
        }
        return *this;
    }
};

RTEXITCODE handleCreateMedium(HandlerArg *a)
{
    std::list<MediumProperty> lstProperties;

    HRESULT hrc;
    int vrc;
    const char *filename = NULL;
    const char *diffparent = NULL;
    uint64_t size = 0;
    enum
    {
        CMD_NONE,
        CMD_DISK,
        CMD_DVD,
        CMD_FLOPPY
    } cmd = CMD_NONE;
    const char *format = NULL;
    bool fBase = true;
    MediumVariant_T enmMediumVariant = MediumVariant_Standard;

    int c;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    // start at 0 because main() has hacked both the argc and argv given to us
    RTGetOptInit(&GetState, a->argc, a->argv, g_aCreateMediumOptions, RT_ELEMENTS(g_aCreateMediumOptions),
                 0, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 'H':   // disk
                if (cmd != CMD_NONE)
                    return errorSyntax(Disk::tr("Only one command can be specified: '%s'"), ValueUnion.psz);
                cmd = CMD_DISK;
                break;

            case 'D':   // DVD
                if (cmd != CMD_NONE)
                    return errorSyntax(Disk::tr("Only one command can be specified: '%s'"), ValueUnion.psz);
                cmd = CMD_DVD;
                break;

            case 'L':   // floppy
                if (cmd != CMD_NONE)
                    return errorSyntax(Disk::tr("Only one command can be specified: '%s'"), ValueUnion.psz);
                cmd = CMD_FLOPPY;
                break;

            case 'f':   // --filename
                filename = ValueUnion.psz;
                break;

            case 'd':   // --diffparent
                diffparent = ValueUnion.psz;
                fBase = false;
                break;

            case 's':   // --size
                size = ValueUnion.u64 * _1M;
                break;

            case 'S':   // --sizebyte
                size = ValueUnion.u64;
                break;

            case 'o':   // --format
                format = ValueUnion.psz;
                break;

            case 'p':   // --property
            case 'P':   // --property-file
            {
                /* allocate property kvp, parse, and append to end of singly linked list */
                char *pszValue = (char *)strchr(ValueUnion.psz, '=');
                if (!pszValue)
                    return RTMsgErrorExitFailure(Disk::tr("Invalid key value pair: No '='."));

                lstProperties.push_back(MediumProperty());
                MediumProperty &rNewProp = lstProperties.back();
                *pszValue++ = '\0';       /* Warning! Modifies argument string. */
                rNewProp.m_pszKey = ValueUnion.psz;
                if (c == 'p')
                {
                    rNewProp.m_pszValue = pszValue;
                    rNewProp.m_cbValue  = strlen(pszValue);
                }
                else // 'P'
                {
                    RTFILE hValueFile = NIL_RTFILE;
                    vrc = RTFileOpen(&hValueFile, pszValue, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
                    if (RT_FAILURE(vrc))
                        return RTMsgErrorExitFailure(Disk::tr("Cannot open replacement value file '%s': %Rrc"), pszValue, vrc);

                    uint64_t cbValue = 0;
                    vrc = RTFileQuerySize(hValueFile, &cbValue);
                    if (RT_SUCCESS(vrc))
                    {
                        if (cbValue <= _16M)
                        {
                            rNewProp.m_cbValue  = (size_t)cbValue;
                            rNewProp.m_pszValue = rNewProp.m_pszFreeValue = (char *)RTMemAlloc(rNewProp.m_cbValue + 1);
                            if (rNewProp.m_pszFreeValue)
                            {
                                vrc = RTFileReadAt(hValueFile, 0, rNewProp.m_pszFreeValue, cbValue, NULL);
                                if (RT_SUCCESS(vrc))
                                    rNewProp.m_pszFreeValue[rNewProp.m_cbValue] = '\0';
                                else
                                    RTMsgError(Disk::tr("Error reading replacement MBR file '%s': %Rrc"), pszValue, vrc);
                            }
                            else
                                vrc = RTMsgErrorRc(VERR_NO_MEMORY, Disk::tr("Out of memory reading '%s': %Rrc"), pszValue, vrc);
                        }
                        else
                            vrc = RTMsgErrorRc(VERR_OUT_OF_RANGE,
                                               Disk::tr("Replacement value file '%s' is to big: %Rhcb, max 16MiB"),
                                               pszValue, cbValue);
                    }
                    else
                        RTMsgError(Disk::tr("Cannot get the size of the value file '%s': %Rrc"), pszValue, vrc);
                    RTFileClose(hValueFile);
                    if (RT_FAILURE(vrc))
                        return RTEXITCODE_FAILURE;
                }
                break;
            }

            case 'F':   // --static ("fixed"/"flat")
            {
                unsigned uMediumVariant = (unsigned)enmMediumVariant;
                uMediumVariant |= MediumVariant_Fixed;
                enmMediumVariant = (MediumVariant_T)uMediumVariant;
                break;
            }

            case 'm':   // --variant
                vrc = parseMediumVariant(ValueUnion.psz, &enmMediumVariant);
                if (RT_FAILURE(vrc))
                    return errorArgument(Disk::tr("Invalid medium variant '%s'"), ValueUnion.psz);
                break;

            case VINF_GETOPT_NOT_OPTION:
                return errorSyntax(Disk::tr("Invalid parameter '%s'"), ValueUnion.psz);

            default:
                if (c > 0)
                {
                    if (RT_C_IS_PRINT(c))
                        return errorSyntax(Disk::tr("Invalid option -%c"), c);
                    else
                        return errorSyntax(Disk::tr("Invalid option case %i"), c);
                }
                else if (c == VERR_GETOPT_UNKNOWN_OPTION)
                    return errorSyntax(Disk::tr("unknown option: %s\n"), ValueUnion.psz);
                else if (ValueUnion.pDef)
                    return errorSyntax("%s: %Rrs", ValueUnion.pDef->pszLong, c);
                else
                    return errorSyntax(Disk::tr("error: %Rrs"), c);
        }
    }

    /* check the outcome */
    if (cmd == CMD_NONE)
        cmd = CMD_DISK;
    ComPtr<IMedium> pParentMedium;
    if (fBase)
    {
        if (!filename || !*filename)
            return errorSyntax(Disk::tr("Parameter --filename is required"));
        if ((enmMediumVariant & MediumVariant_VmdkRawDisk) == 0 && size == 0)
            return errorSyntax(Disk::tr("Parameter --size is required"));
        if (!format || !*format)
        {
            if (cmd == CMD_DISK)
                format = "VDI";
            else if (cmd == CMD_DVD || cmd == CMD_FLOPPY)
            {
                format = "RAW";
                unsigned uMediumVariant = (unsigned)enmMediumVariant;
                uMediumVariant |= MediumVariant_Fixed;
                enmMediumVariant = (MediumVariant_T)uMediumVariant;
            }
        }
        if ((enmMediumVariant & MediumVariant_VmdkRawDisk) && strcmp(format, "VMDK"))
            return errorSyntax(Disk::tr("Variant 'Rawdisk' requires '--format=VMDK'"));
    }
    else
    {
        if (   !filename
            || !*filename)
            return errorSyntax(Disk::tr("Parameter --filename is required"));
        size = 0;
        if (cmd != CMD_DISK)
            return errorSyntax(Disk::tr("Creating a differencing medium is only supported for hard disks"));
        enmMediumVariant = MediumVariant_Diff;
        if (!format || !*format)
        {
            const char *pszExt = RTPathSuffix(filename);
            /* Skip over . if there is an extension. */
            if (pszExt)
                pszExt++;
            if (!pszExt || !*pszExt)
                format = "VDI";
            else
                format = pszExt;
        }
        hrc = openMedium(a, diffparent, DeviceType_HardDisk,
                         AccessMode_ReadWrite, pParentMedium,
                         false /* fForceNewUuidOnOpen */, false /* fSilent */);
        if (FAILED(hrc))
            return RTEXITCODE_FAILURE;
        if (pParentMedium.isNull())
            return RTMsgErrorExit(RTEXITCODE_FAILURE, Disk::tr("Invalid parent hard disk reference, avoiding crash"));
        MediumState_T state;
        CHECK_ERROR(pParentMedium, COMGETTER(State)(&state));
        if (FAILED(hrc))
            return RTEXITCODE_FAILURE;
        if (state == MediumState_Inaccessible)
        {
            CHECK_ERROR(pParentMedium, RefreshState(&state));
            if (FAILED(hrc))
                return RTEXITCODE_FAILURE;
        }
    }
    /* check for filename extension */
    /** @todo use IMediumFormat to cover all extensions generically */
    Utf8Str strName(filename);
    if (!RTPathHasSuffix(strName.c_str()))
    {
        Utf8Str strFormat(format);
        if (cmd == CMD_DISK)
        {
            if (strFormat.compare("vmdk", RTCString::CaseInsensitive) == 0)
                strName.append(".vmdk");
            else if (strFormat.compare("vhd", RTCString::CaseInsensitive) == 0)
                strName.append(".vhd");
            else
                strName.append(".vdi");
        }
        else if (cmd == CMD_DVD)
            strName.append(".iso");
        else if (cmd == CMD_FLOPPY)
            strName.append(".img");
        filename = strName.c_str();
    }

    ComPtr<IMedium> pMedium;
    if (cmd == CMD_DISK)
        hrc = createMedium(a, format, filename, DeviceType_HardDisk,
                           AccessMode_ReadWrite, pMedium);
    else if (cmd == CMD_DVD)
        hrc = createMedium(a, format, filename, DeviceType_DVD,
                           AccessMode_ReadOnly, pMedium);
    else if (cmd == CMD_FLOPPY)
        hrc = createMedium(a, format, filename, DeviceType_Floppy,
                           AccessMode_ReadWrite, pMedium);
    else
        hrc = E_INVALIDARG; /* cannot happen but make gcc happy */


    if (SUCCEEDED(hrc) && pMedium)
    {
        if (lstProperties.size() > 0)
        {
            ComPtr<IMediumFormat> pMediumFormat;
            CHECK_ERROR2I_RET(pMedium, COMGETTER(MediumFormat)(pMediumFormat.asOutParam()), RTEXITCODE_FAILURE);
            com::SafeArray<BSTR> propertyNames;
            com::SafeArray<BSTR> propertyDescriptions;
            com::SafeArray<DataType_T> propertyTypes;
            com::SafeArray<ULONG> propertyFlags;
            com::SafeArray<BSTR> propertyDefaults;
            CHECK_ERROR2I_RET(pMediumFormat,
                              DescribeProperties(ComSafeArrayAsOutParam(propertyNames),
                                                 ComSafeArrayAsOutParam(propertyDescriptions),
                                                 ComSafeArrayAsOutParam(propertyTypes),
                                                 ComSafeArrayAsOutParam(propertyFlags),
                                                 ComSafeArrayAsOutParam(propertyDefaults)),
                              RTEXITCODE_FAILURE);

            for (std::list<MediumProperty>::iterator it = lstProperties.begin();
                 it != lstProperties.end();
                 ++it)
            {
                const char * const pszKey = it->m_pszKey;
                bool fBinary = true;
                bool fPropertyFound = false;
                for (size_t i = 0; i < propertyNames.size(); ++i)
                    if (RTUtf16CmpUtf8(propertyNames[i], pszKey) == 0)
                    {
                        fBinary = propertyTypes[i] == DataType_Int8;
                        fPropertyFound = true;
                        break;
                    }
                if (!fPropertyFound)
                    return RTMsgErrorExit(RTEXITCODE_FAILURE,
                                          Disk::tr("Property '%s' was not found in the list of medium properties for the requested medium format (%s)."),
                                          pszKey, format);
                if (!fBinary)
                    CHECK_ERROR2I_RET(pMedium, SetProperty(Bstr(pszKey).raw(), Bstr(it->m_pszValue).raw()),
                                      RTEXITCODE_FAILURE);
                else
                {
                    com::Bstr bstrBase64Value;
                    hrc = bstrBase64Value.base64Encode(it->m_pszValue, it->m_cbValue);
                    if (FAILED(hrc))
                        return RTMsgErrorExit(RTEXITCODE_FAILURE, Disk::tr("Base64 encoding of the property %s failed. (%Rhrc)"),
                                              pszKey, hrc);
                    CHECK_ERROR2I_RET(pMedium, SetProperty(Bstr(pszKey).raw(), bstrBase64Value.raw()), RTEXITCODE_FAILURE);
                }
            }
        }

        ComPtr<IProgress> pProgress;
        com::SafeArray<MediumVariant_T> l_variants(sizeof(MediumVariant_T)*8);

        for (ULONG i = 0; i < l_variants.size(); ++i)
        {
            ULONG temp = enmMediumVariant;
            temp &= 1<<i;
            l_variants [i] = (MediumVariant_T)temp;
        }

        if (fBase)
            CHECK_ERROR(pMedium, CreateBaseStorage(size, ComSafeArrayAsInParam(l_variants), pProgress.asOutParam()));
        else
            CHECK_ERROR(pParentMedium, CreateDiffStorage(pMedium, ComSafeArrayAsInParam(l_variants), pProgress.asOutParam()));
        if (SUCCEEDED(hrc) && pProgress)
        {
            hrc = showProgress(pProgress);
            CHECK_PROGRESS_ERROR(pProgress, (Disk::tr("Failed to create medium")));
        }
    }

    if (SUCCEEDED(hrc) && pMedium)
    {
        Bstr uuid;
        CHECK_ERROR(pMedium, COMGETTER(Id)(uuid.asOutParam()));
        RTPrintf(Disk::tr("Medium created. UUID: %s\n"), Utf8Str(uuid).c_str());

        //CHECK_ERROR(pMedium, Close());
    }
    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static const RTGETOPTDEF g_aModifyMediumOptions[] =
{
    { "disk",           'H', RTGETOPT_REQ_NOTHING },
    { "dvd",            'D', RTGETOPT_REQ_NOTHING },
    { "floppy",         'L', RTGETOPT_REQ_NOTHING },
    { "--type",         't', RTGETOPT_REQ_STRING },
    { "-type",          't', RTGETOPT_REQ_STRING },     // deprecated
    { "settype",        't', RTGETOPT_REQ_STRING },     // deprecated
    { "--autoreset",    'z', RTGETOPT_REQ_STRING },
    { "-autoreset",     'z', RTGETOPT_REQ_STRING },     // deprecated
    { "autoreset",      'z', RTGETOPT_REQ_STRING },     // deprecated
    { "--property",     'p', RTGETOPT_REQ_STRING },
    { "--compact",      'c', RTGETOPT_REQ_NOTHING },
    { "-compact",       'c', RTGETOPT_REQ_NOTHING },    // deprecated
    { "compact",        'c', RTGETOPT_REQ_NOTHING },    // deprecated
    { "--resize",       'r', RTGETOPT_REQ_UINT64 },
    { "--resizebyte",   'R', RTGETOPT_REQ_UINT64 },
    { "--move",         'm', RTGETOPT_REQ_STRING },
    { "--setlocation",  'l', RTGETOPT_REQ_STRING },
    { "--description",  'd', RTGETOPT_REQ_STRING }
};

RTEXITCODE handleModifyMedium(HandlerArg *a)
{
    HRESULT hrc;
    int vrc;
    enum {
        CMD_NONE,
        CMD_DISK,
        CMD_DVD,
        CMD_FLOPPY
    } cmd = CMD_NONE;
    ComPtr<IMedium> pMedium;
    MediumType_T enmMediumType = MediumType_Normal; /* Shut up MSC */
    bool AutoReset = false;
    SafeArray<BSTR> mediumPropNames;
    SafeArray<BSTR> mediumPropValues;
    bool fModifyMediumType = false;
    bool fModifyAutoReset = false;
    bool fModifyProperties = false;
    bool fModifyCompact = false;
    bool fModifyResize = false;
    bool fModifyResizeMB = false;
    bool fMoveMedium = false;
    bool fModifyDescription = false;
    bool fSetNewLocation = false;
    uint64_t cbResize = 0;
    const char *pszFilenameOrUuid = NULL;
    char *pszNewLocation = NULL;

    int c;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    // start at 0 because main() has hacked both the argc and argv given to us
    RTGetOptInit(&GetState, a->argc, a->argv, g_aModifyMediumOptions, RT_ELEMENTS(g_aModifyMediumOptions),
                 0, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 'H':   // disk
                if (cmd != CMD_NONE)
                    return errorSyntax(Disk::tr("Only one command can be specified: '%s'"), ValueUnion.psz);
                cmd = CMD_DISK;
                break;

            case 'D':   // DVD
                if (cmd != CMD_NONE)
                    return errorSyntax(Disk::tr("Only one command can be specified: '%s'"), ValueUnion.psz);
                cmd = CMD_DVD;
                break;

            case 'L':   // floppy
                if (cmd != CMD_NONE)
                    return errorSyntax(Disk::tr("Only one command can be specified: '%s'"), ValueUnion.psz);
                cmd = CMD_FLOPPY;
                break;

            case 't':   // --type
                vrc = parseMediumType(ValueUnion.psz, &enmMediumType);
                if (RT_FAILURE(vrc))
                    return errorArgument(Disk::tr("Invalid medium type '%s'"), ValueUnion.psz);
                fModifyMediumType = true;
                break;

            case 'z':   // --autoreset
                vrc = parseBool(ValueUnion.psz, &AutoReset);
                if (RT_FAILURE(vrc))
                    return errorArgument(Disk::tr("Invalid autoreset parameter '%s'"), ValueUnion.psz);
                fModifyAutoReset = true;
                break;

            case 'p':   // --property
            {
                /* Parse 'name=value' */
                char *pszProperty = RTStrDup(ValueUnion.psz);
                if (pszProperty)
                {
                    char *pDelimiter = strchr(pszProperty, '=');
                    if (pDelimiter)
                    {
                        *pDelimiter = '\0';

                        Bstr bstrName(pszProperty);
                        Bstr bstrValue(&pDelimiter[1]);
                        bstrName.detachTo(mediumPropNames.appendedRaw());
                        bstrValue.detachTo(mediumPropValues.appendedRaw());
                        fModifyProperties = true;
                    }
                    else
                    {
                        errorArgument(Disk::tr("Invalid --property argument '%s'"), ValueUnion.psz);
                        hrc = E_FAIL;
                    }
                    RTStrFree(pszProperty);
                }
                else
                {
                    RTStrmPrintf(g_pStdErr, Disk::tr("Error: Failed to allocate memory for medium property '%s'\n"),
                                 ValueUnion.psz);
                    hrc = E_FAIL;
                }
                break;
            }

            case 'c':   // --compact
                fModifyCompact = true;
                break;

            case 'r':   // --resize
                cbResize = ValueUnion.u64 * _1M;
                fModifyResize = true;
                fModifyResizeMB = true; // do sanity check!
                break;

            case 'R':   // --resizebyte
                cbResize = ValueUnion.u64;
                fModifyResize = true;
                break;

            case 'm':   // --move
                /* Get a new location  */
                pszNewLocation = RTPathAbsDup(ValueUnion.psz);
                fMoveMedium = true;
                break;

            case 'l':   // --setlocation
                /* Get a new location  */
                pszNewLocation = RTPathAbsDup(ValueUnion.psz);
                fSetNewLocation = true;
                break;

            case 'd':   // --description
                /* Get a new description  */
                pszNewLocation = RTStrDup(ValueUnion.psz);
                fModifyDescription = true;
                break;

            case VINF_GETOPT_NOT_OPTION:
                if (!pszFilenameOrUuid)
                    pszFilenameOrUuid = ValueUnion.psz;
                else
                    return errorSyntax(Disk::tr("Invalid parameter '%s'"), ValueUnion.psz);
                break;

            default:
                if (c > 0)
                {
                    if (RT_C_IS_PRINT(c))
                        return errorSyntax(Disk::tr("Invalid option -%c"), c);
                    else
                        return errorSyntax(Disk::tr("Invalid option case %i"), c);
                }
                else if (c == VERR_GETOPT_UNKNOWN_OPTION)
                    return errorSyntax(Disk::tr("unknown option: %s\n"), ValueUnion.psz);
                else if (ValueUnion.pDef)
                    return errorSyntax("%s: %Rrs", ValueUnion.pDef->pszLong, c);
                else
                    return errorSyntax(Disk::tr("error: %Rrs"), c);
        }
    }

    if (cmd == CMD_NONE)
        cmd = CMD_DISK;

    if (!pszFilenameOrUuid)
        return errorSyntax(Disk::tr("Medium name or UUID required"));

    if (!fModifyMediumType
        && !fModifyAutoReset
        && !fModifyProperties
        && !fModifyCompact
        && !fModifyResize
        && !fMoveMedium
        && !fSetNewLocation
        && !fModifyDescription
        )
        return errorSyntax(Disk::tr("No operation specified"));

    /* Always open the medium if necessary, there is no other way. */
    if (cmd == CMD_DISK)
        hrc = openMedium(a, pszFilenameOrUuid, DeviceType_HardDisk,
                         AccessMode_ReadWrite, pMedium,
                         false /* fForceNewUuidOnOpen */, false /* fSilent */);
    else if (cmd == CMD_DVD)
        hrc = openMedium(a, pszFilenameOrUuid, DeviceType_DVD,
                         AccessMode_ReadOnly, pMedium,
                         false /* fForceNewUuidOnOpen */, false /* fSilent */);
    else if (cmd == CMD_FLOPPY)
        hrc = openMedium(a, pszFilenameOrUuid, DeviceType_Floppy,
                         AccessMode_ReadWrite, pMedium,
                         false /* fForceNewUuidOnOpen */, false /* fSilent */);
    else
        hrc = E_INVALIDARG; /* cannot happen but make gcc happy */
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;
    if (pMedium.isNull())
    {
        RTMsgError(Disk::tr("Invalid medium reference, avoiding crash"));
        return RTEXITCODE_FAILURE;
    }

    if (   fModifyResize
        && fModifyResizeMB)
    {
        // Sanity check
        //
        // In general users should know what they do but in this case users have no
        // alternative to VBoxManage. If happens that one wants to resize the disk
        // and uses --resize and does not consider that this parameter expects the
        // new medium size in MB not Byte. If the operation is started and then
        // aborted by the user, the result is most likely a medium which doesn't
        // work anymore.
        MediumState_T state;
        pMedium->RefreshState(&state);
        LONG64 logicalSize;
        pMedium->COMGETTER(LogicalSize)(&logicalSize);
        if (cbResize > (uint64_t)logicalSize * 1000)
        {
            RTMsgError(Disk::tr("Error: Attempt to resize the medium from %RU64.%RU64 MB to %RU64.%RU64 MB. Use --resizebyte if this is intended!\n"),
                       logicalSize / _1M, (logicalSize % _1M) / (_1M / 10), cbResize / _1M, (cbResize % _1M) / (_1M / 10));
            return RTEXITCODE_FAILURE;
        }
    }

    if (fModifyMediumType)
    {
        MediumType_T enmCurrMediumType;
        CHECK_ERROR(pMedium, COMGETTER(Type)(&enmCurrMediumType));

        if (enmCurrMediumType != enmMediumType)
            CHECK_ERROR(pMedium, COMSETTER(Type)(enmMediumType));
    }

    if (fModifyAutoReset)
    {
        CHECK_ERROR(pMedium, COMSETTER(AutoReset)(AutoReset));
    }

    if (fModifyProperties)
    {
        CHECK_ERROR(pMedium, SetProperties(ComSafeArrayAsInParam(mediumPropNames), ComSafeArrayAsInParam(mediumPropValues)));
    }

    if (fModifyCompact)
    {
        ComPtr<IProgress> pProgress;
        CHECK_ERROR(pMedium, Compact(pProgress.asOutParam()));
        if (SUCCEEDED(hrc))
            hrc = showProgress(pProgress);
        if (FAILED(hrc))
        {
            if (hrc == E_NOTIMPL)
                RTMsgError(Disk::tr("Compact medium operation is not implemented!"));
            else if (hrc == VBOX_E_NOT_SUPPORTED)
                RTMsgError(Disk::tr("Compact medium operation for this format is not implemented yet!"));
            else if (!pProgress.isNull())
                CHECK_PROGRESS_ERROR(pProgress, (Disk::tr("Failed to compact medium")));
            else
                RTMsgError(Disk::tr("Failed to compact medium!"));
        }
    }

    if (fModifyResize)
    {
        ComPtr<IProgress> pProgress;
        CHECK_ERROR(pMedium, Resize(cbResize, pProgress.asOutParam()));
        if (SUCCEEDED(hrc))
            hrc = showProgress(pProgress);
        if (FAILED(hrc))
        {
            if (!pProgress.isNull())
                CHECK_PROGRESS_ERROR(pProgress, (Disk::tr("Failed to resize medium")));
            else if (hrc == E_NOTIMPL)
                RTMsgError(Disk::tr("Resize medium operation is not implemented!"));
            else if (hrc == VBOX_E_NOT_SUPPORTED)
                RTMsgError(Disk::tr("Resize medium operation for this format is not implemented yet!"));
            else
                RTMsgError(Disk::tr("Failed to resize medium!"));
        }
    }

    if (fMoveMedium)
    {
        do
        {
            ComPtr<IProgress> pProgress;
            Utf8Str strLocation(pszNewLocation);
            RTStrFree(pszNewLocation);
            CHECK_ERROR(pMedium, MoveTo(Bstr(strLocation).raw(), pProgress.asOutParam()));

            if (SUCCEEDED(hrc) && !pProgress.isNull())
            {
                hrc = showProgress(pProgress);
                CHECK_PROGRESS_ERROR(pProgress, (Disk::tr("Failed to move medium")));
            }

            Bstr uuid;
            CHECK_ERROR_BREAK(pMedium, COMGETTER(Id)(uuid.asOutParam()));

            RTPrintf(Disk::tr("Move medium with UUID %s finished\n"), Utf8Str(uuid).c_str());
        }
        while (0);
    }

    if (fSetNewLocation)
    {
        Utf8Str strLocation(pszNewLocation);
        RTStrFree(pszNewLocation);
        CHECK_ERROR(pMedium, COMSETTER(Location)(Bstr(strLocation).raw()));

        Bstr uuid;
        CHECK_ERROR(pMedium, COMGETTER(Id)(uuid.asOutParam()));
        RTPrintf(Disk::tr("Set new location of medium with UUID %s finished\n"), Utf8Str(uuid).c_str());
    }

    if (fModifyDescription)
    {
        CHECK_ERROR(pMedium, COMSETTER(Description)(Bstr(pszNewLocation).raw()));

        RTPrintf(Disk::tr("Medium description has been changed.\n"));
    }

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static const RTGETOPTDEF g_aCloneMediumOptions[] =
{
    { "disk",           'd', RTGETOPT_REQ_NOTHING },
    { "dvd",            'D', RTGETOPT_REQ_NOTHING },
    { "floppy",         'f', RTGETOPT_REQ_NOTHING },
    { "--format",       'o', RTGETOPT_REQ_STRING },
    { "-format",        'o', RTGETOPT_REQ_STRING },
    { "--static",       'F', RTGETOPT_REQ_NOTHING },
    { "-static",        'F', RTGETOPT_REQ_NOTHING },
    { "--existing",     'E', RTGETOPT_REQ_NOTHING },
    { "--variant",      'm', RTGETOPT_REQ_STRING },
    { "-variant",       'm', RTGETOPT_REQ_STRING },
    { "--resize",       'r', RTGETOPT_REQ_UINT64 },
};

RTEXITCODE handleCloneMedium(HandlerArg *a)
{
    HRESULT hrc;
    int vrc;
    enum {
        CMD_NONE,
        CMD_DISK,
        CMD_DVD,
        CMD_FLOPPY
    } cmd = CMD_NONE;
    const char *pszSrc = NULL;
    const char *pszDst = NULL;
    Bstr format;
    MediumVariant_T enmMediumVariant = MediumVariant_Standard;
    bool fExisting = false;
    bool fNeedResize = false;
    uint64_t cbResize = 0;

    int c;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    // start at 0 because main() has hacked both the argc and argv given to us
    RTGetOptInit(&GetState, a->argc, a->argv, g_aCloneMediumOptions, RT_ELEMENTS(g_aCloneMediumOptions),
                 0, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 'd':   // disk
                if (cmd != CMD_NONE)
                    return errorSyntax(Disk::tr("Only one command can be specified: '%s'"), ValueUnion.psz);
                cmd = CMD_DISK;
                break;

            case 'D':   // DVD
                if (cmd != CMD_NONE)
                    return errorSyntax(Disk::tr("Only one command can be specified: '%s'"), ValueUnion.psz);
                cmd = CMD_DVD;
                break;

            case 'f':   // floppy
                if (cmd != CMD_NONE)
                    return errorSyntax(Disk::tr("Only one command can be specified: '%s'"), ValueUnion.psz);
                cmd = CMD_FLOPPY;
                break;

            case 'o':   // --format
                format = ValueUnion.psz;
                break;

            case 'F':   // --static
            {
                unsigned uMediumVariant = (unsigned)enmMediumVariant;
                uMediumVariant |= MediumVariant_Fixed;
                enmMediumVariant = (MediumVariant_T)uMediumVariant;
                break;
            }

            case 'E':   // --existing
                fExisting = true;
                break;

            case 'm':   // --variant
                vrc = parseMediumVariant(ValueUnion.psz, &enmMediumVariant);
                if (RT_FAILURE(vrc))
                    return errorArgument(Disk::tr("Invalid medium variant '%s'"), ValueUnion.psz);
                break;

            case 'r':   // --resize
                fNeedResize = true;
                cbResize = ValueUnion.u64 * _1M;
                break;

            case VINF_GETOPT_NOT_OPTION:
                if (!pszSrc)
                    pszSrc = ValueUnion.psz;
                else if (!pszDst)
                    pszDst = ValueUnion.psz;
                else
                    return errorSyntax(Disk::tr("Invalid parameter '%s'"), ValueUnion.psz);
                break;

            default:
                if (c > 0)
                {
                    if (RT_C_IS_GRAPH(c))
                        return errorSyntax(Disk::tr("unhandled option: -%c"), c);
                    else
                        return errorSyntax(Disk::tr("unhandled option: %i"), c);
                }
                else if (c == VERR_GETOPT_UNKNOWN_OPTION)
                    return errorSyntax(Disk::tr("unknown option: %s"), ValueUnion.psz);
                else if (ValueUnion.pDef)
                    return errorSyntax("%s: %Rrs", ValueUnion.pDef->pszLong, c);
                else
                    return errorSyntax(Disk::tr("error: %Rrs"), c);
        }
    }

    if (cmd == CMD_NONE)
        cmd = CMD_DISK;
    if (!pszSrc)
        return errorSyntax(Disk::tr("Mandatory UUID or input file parameter missing"));
    if (!pszDst)
        return errorSyntax(Disk::tr("Mandatory output file parameter missing"));
    if (fExisting && (!format.isEmpty() || enmMediumVariant != MediumVariant_Standard))
        return errorSyntax(Disk::tr("Specified options which cannot be used with --existing"));

    ComPtr<IMedium> pSrcMedium;
    ComPtr<IMedium> pDstMedium;

    if (cmd == CMD_DISK)
        hrc = openMedium(a, pszSrc, DeviceType_HardDisk, AccessMode_ReadOnly, pSrcMedium,
                         false /* fForceNewUuidOnOpen */, false /* fSilent */);
    else if (cmd == CMD_DVD)
        hrc = openMedium(a, pszSrc, DeviceType_DVD, AccessMode_ReadOnly, pSrcMedium,
                         false /* fForceNewUuidOnOpen */, false /* fSilent */);
    else if (cmd == CMD_FLOPPY)
        hrc = openMedium(a, pszSrc, DeviceType_Floppy, AccessMode_ReadOnly, pSrcMedium,
                         false /* fForceNewUuidOnOpen */, false /* fSilent */);
    else
        hrc = E_INVALIDARG; /* cannot happen but make gcc happy */
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    do
    {
        /* open/create destination medium */
        if (fExisting)
        {
            if (cmd == CMD_DISK)
                hrc = openMedium(a, pszDst, DeviceType_HardDisk, AccessMode_ReadWrite, pDstMedium,
                                 false /* fForceNewUuidOnOpen */, false /* fSilent */);
            else if (cmd == CMD_DVD)
                hrc = openMedium(a, pszDst, DeviceType_DVD, AccessMode_ReadOnly, pDstMedium,
                                 false /* fForceNewUuidOnOpen */, false /* fSilent */);
            else if (cmd == CMD_FLOPPY)
                hrc = openMedium(a, pszDst, DeviceType_Floppy, AccessMode_ReadWrite, pDstMedium,
                                 false /* fForceNewUuidOnOpen */, false /* fSilent */);
            if (FAILED(hrc))
                break;

            /* Perform accessibility check now. */
            MediumState_T state;
            CHECK_ERROR_BREAK(pDstMedium, RefreshState(&state));
            CHECK_ERROR_BREAK(pDstMedium, COMGETTER(Format)(format.asOutParam()));
        }
        else
        {
            /*
             * In case the format is unspecified check that the source medium supports
             * image creation and use the same format for the destination image.
             * Use the default image format if it is not supported.
             */
            if (format.isEmpty())
            {
                ComPtr<IMediumFormat> pMediumFmt;
                com::SafeArray<MediumFormatCapabilities_T> l_caps;
                CHECK_ERROR_BREAK(pSrcMedium, COMGETTER(MediumFormat)(pMediumFmt.asOutParam()));
                CHECK_ERROR_BREAK(pMediumFmt, COMGETTER(Capabilities)(ComSafeArrayAsOutParam(l_caps)));
                ULONG caps=0;
                for (size_t i = 0; i < l_caps.size(); i++)
                    caps |= l_caps[i];
                if (caps & (  MediumFormatCapabilities_CreateDynamic
                            | MediumFormatCapabilities_CreateFixed))
                    CHECK_ERROR_BREAK(pMediumFmt, COMGETTER(Id)(format.asOutParam()));
            }
            Utf8Str strFormat(format);
            if (cmd == CMD_DISK)
                hrc = createMedium(a, strFormat.c_str(), pszDst, DeviceType_HardDisk,
                                   AccessMode_ReadWrite, pDstMedium);
            else if (cmd == CMD_DVD)
                hrc = createMedium(a, strFormat.c_str(), pszDst, DeviceType_DVD,
                                   AccessMode_ReadOnly,  pDstMedium);
            else if (cmd == CMD_FLOPPY)
                hrc = createMedium(a, strFormat.c_str(), pszDst, DeviceType_Floppy,
                                   AccessMode_ReadWrite, pDstMedium);
            if (FAILED(hrc))
                break;
        }

        ComPtr<IProgress> pProgress;
        com::SafeArray<MediumVariant_T> l_variants(sizeof(MediumVariant_T)*8);

        for (ULONG i = 0; i < l_variants.size(); ++i)
        {
            ULONG temp = enmMediumVariant;
            temp &= 1<<i;
            l_variants [i] = (MediumVariant_T)temp;
        }

        if (fNeedResize)
        {
            CHECK_ERROR_BREAK(pSrcMedium, ResizeAndCloneTo(pDstMedium, cbResize, ComSafeArrayAsInParam(l_variants), NULL, pProgress.asOutParam()));
        }
        else
        {
            CHECK_ERROR_BREAK(pSrcMedium, CloneTo(pDstMedium, ComSafeArrayAsInParam(l_variants), NULL, pProgress.asOutParam()));
        }


        hrc = showProgress(pProgress);
        CHECK_PROGRESS_ERROR_BREAK(pProgress, (Disk::tr("Failed to clone medium")));

        Bstr uuid;
        CHECK_ERROR_BREAK(pDstMedium, COMGETTER(Id)(uuid.asOutParam()));

        RTPrintf(Disk::tr("Clone medium created in format '%ls'. UUID: %s\n"),
                 format.raw(), Utf8Str(uuid).c_str());
    }
    while (0);

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static const RTGETOPTDEF g_aConvertFromRawHardDiskOptions[] =
{
    { "--format",       'o', RTGETOPT_REQ_STRING },
    { "-format",        'o', RTGETOPT_REQ_STRING },
    { "--static",       'F', RTGETOPT_REQ_NOTHING },
    { "-static",        'F', RTGETOPT_REQ_NOTHING },
    { "--variant",      'm', RTGETOPT_REQ_STRING },
    { "-variant",       'm', RTGETOPT_REQ_STRING },
    { "--uuid",         'u', RTGETOPT_REQ_STRING },
};

RTEXITCODE handleConvertFromRaw(HandlerArg *a)
{
    bool fReadFromStdIn = false;
    const char *format = "VDI";
    const char *srcfilename = NULL;
    const char *dstfilename = NULL;
    const char *filesize = NULL;
    unsigned uImageFlags = VD_IMAGE_FLAGS_NONE;
    void *pvBuf = NULL;
    RTUUID uuid;
    PCRTUUID pUuid = NULL;

    int c;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    // start at 0 because main() has hacked both the argc and argv given to us
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv,
                           g_aConvertFromRawHardDiskOptions, RT_ELEMENTS(g_aConvertFromRawHardDiskOptions),
                           0, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 'u':   // --uuid
                if (RT_FAILURE(RTUuidFromStr(&uuid, ValueUnion.psz)))
                    return errorSyntax(Disk::tr("Invalid UUID '%s'"), ValueUnion.psz);
                pUuid = &uuid;
                break;
            case 'o':   // --format
                format = ValueUnion.psz;
                break;

            case 'm':   // --variant
            {
                MediumVariant_T enmMediumVariant = MediumVariant_Standard;
                vrc = parseMediumVariant(ValueUnion.psz, &enmMediumVariant);
                if (RT_FAILURE(vrc))
                    return errorArgument(Disk::tr("Invalid medium variant '%s'"), ValueUnion.psz);
                /// @todo cleaner solution than assuming 1:1 mapping?
                uImageFlags = (unsigned)enmMediumVariant;
                break;
            }
            case VINF_GETOPT_NOT_OPTION:
                if (!srcfilename)
                {
                    srcfilename = ValueUnion.psz;
                    fReadFromStdIn = !strcmp(srcfilename, "stdin");
                }
                else if (!dstfilename)
                    dstfilename = ValueUnion.psz;
                else if (fReadFromStdIn && !filesize)
                    filesize = ValueUnion.psz;
                else
                    return errorSyntax(Disk::tr("Invalid parameter '%s'"), ValueUnion.psz);
                break;

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    if (!srcfilename || !dstfilename || (fReadFromStdIn && !filesize))
        return errorSyntax(Disk::tr("Incorrect number of parameters"));
    RTStrmPrintf(g_pStdErr, Disk::tr("Converting from raw image file=\"%s\" to file=\"%s\"...\n"),
                 srcfilename, dstfilename);

    PVDISK pDisk = NULL;

    PVDINTERFACE     pVDIfs = NULL;
    VDINTERFACEERROR vdInterfaceError;
    vdInterfaceError.pfnError     = handleVDError;
    vdInterfaceError.pfnMessage   = NULL;

    vrc = VDInterfaceAdd(&vdInterfaceError.Core, "VBoxManage_IError", VDINTERFACETYPE_ERROR,
                         NULL, sizeof(VDINTERFACEERROR), &pVDIfs);
    AssertRC(vrc);

    /* open raw image file. */
    RTFILE File;
    if (fReadFromStdIn)
        vrc = RTFileFromNative(&File, RTFILE_NATIVE_STDIN);
    else
        vrc = RTFileOpen(&File, srcfilename, RTFILE_O_READ | RTFILE_O_OPEN | RTFILE_O_DENY_WRITE);
    if (RT_FAILURE(vrc))
    {
        RTMsgError(Disk::tr("Cannot open file \"%s\": %Rrc"), srcfilename, vrc);
        goto out;
    }

    uint64_t cbFile;
    /* get image size. */
    if (fReadFromStdIn)
        cbFile = RTStrToUInt64(filesize);
    else
        vrc = RTFileQuerySize(File, &cbFile);
    if (RT_FAILURE(vrc))
    {
        RTMsgError(Disk::tr("Cannot get image size for file \"%s\": %Rrc"), srcfilename, vrc);
        goto out;
    }

    RTStrmPrintf(g_pStdErr, Disk::tr("Creating %s image with size %RU64 bytes (%RU64MB)...\n", "", cbFile),
                 (uImageFlags & VD_IMAGE_FLAGS_FIXED) ? Disk::tr("fixed", "adjective") : Disk::tr("dynamic", "adjective"),
                 cbFile, (cbFile + _1M - 1) / _1M);
    char pszComment[256];
    RTStrPrintf(pszComment, sizeof(pszComment), Disk::tr("Converted image from %s"), srcfilename);
    vrc = VDCreate(pVDIfs, VDTYPE_HDD, &pDisk);
    if (RT_FAILURE(vrc))
    {
        RTMsgError(Disk::tr("Cannot create the virtual disk container: %Rrc"), vrc);
        goto out;
    }

    Assert(RT_MIN(cbFile / 512 / 16 / 63, 16383) -
           (unsigned int)RT_MIN(cbFile / 512 / 16 / 63, 16383) == 0);
    VDGEOMETRY PCHS, LCHS;
    PCHS.cCylinders = (unsigned int)RT_MIN(cbFile / 512 / 16 / 63, 16383);
    PCHS.cHeads = 16;
    PCHS.cSectors = 63;
    LCHS.cCylinders = 0;
    LCHS.cHeads = 0;
    LCHS.cSectors = 0;
    vrc = VDCreateBase(pDisk, format, dstfilename, cbFile,
                       uImageFlags, pszComment, &PCHS, &LCHS, pUuid,
                       VD_OPEN_FLAGS_NORMAL, NULL, NULL);
    if (RT_FAILURE(vrc))
    {
        RTMsgError(Disk::tr("Cannot create the disk image \"%s\": %Rrc"), dstfilename, vrc);
        goto out;
    }

    size_t cbBuffer;
    cbBuffer = _1M;
    pvBuf = RTMemAlloc(cbBuffer);
    if (!pvBuf)
    {
        vrc = VERR_NO_MEMORY;
        RTMsgError(Disk::tr("Out of memory allocating buffers for image \"%s\": %Rrc"), dstfilename, vrc);
        goto out;
    }

    uint64_t offFile;
    offFile = 0;
    while (offFile < cbFile)
    {
        size_t cbRead;
        size_t cbToRead;
        cbRead = 0;
        cbToRead = cbFile - offFile >= (uint64_t)cbBuffer ?
                            cbBuffer : (size_t)(cbFile - offFile);
        vrc = RTFileRead(File, pvBuf, cbToRead, &cbRead);
        if (RT_FAILURE(vrc) || !cbRead)
            break;
        vrc = VDWrite(pDisk, offFile, pvBuf, cbRead);
        if (RT_FAILURE(vrc))
        {
            RTMsgError(Disk::tr("Failed to write to disk image \"%s\": %Rrc"), dstfilename, vrc);
            goto out;
        }
        offFile += cbRead;
    }

out:
    if (pvBuf)
        RTMemFree(pvBuf);
    if (pDisk)
        VDClose(pDisk, RT_FAILURE(vrc));
    if (File != NIL_RTFILE)
        RTFileClose(File);

    return RT_SUCCESS(vrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

HRESULT showMediumInfo(const ComPtr<IVirtualBox> &pVirtualBox,
                       const ComPtr<IMedium> &pMedium,
                       const char *pszParentUUID,
                       bool fOptLong)
{
    HRESULT hrc = S_OK;
    do
    {
        Bstr uuid;
        pMedium->COMGETTER(Id)(uuid.asOutParam());
        RTPrintf("UUID:           %ls\n", uuid.raw());
        if (pszParentUUID)
            RTPrintf(Disk::tr("Parent UUID:    %s\n"), pszParentUUID);

        /* check for accessibility */
        MediumState_T enmState;
        CHECK_ERROR_BREAK(pMedium, RefreshState(&enmState));
        const char *pszState = Disk::tr("unknown");
        switch (enmState)
        {
            case MediumState_NotCreated:
                pszState = Disk::tr("not created");
                break;
            case MediumState_Created:
                pszState = Disk::tr("created");
                break;
            case MediumState_LockedRead:
                pszState = Disk::tr("locked read");
                break;
            case MediumState_LockedWrite:
                pszState = Disk::tr("locked write");
                break;
            case MediumState_Inaccessible:
                pszState = Disk::tr("inaccessible");
                break;
            case MediumState_Creating:
                pszState = Disk::tr("creating");
                break;
            case MediumState_Deleting:
                pszState = Disk::tr("deleting");
                break;
#ifdef VBOX_WITH_XPCOM_CPP_ENUM_HACK
            case MediumState_32BitHack: break; /* Shut up compiler warnings. */
#endif
        }
        RTPrintf(Disk::tr("State:          %s\n"), pszState);

        if (fOptLong && enmState == MediumState_Inaccessible)
        {
            Bstr err;
            CHECK_ERROR_BREAK(pMedium, COMGETTER(LastAccessError)(err.asOutParam()));
            RTPrintf(Disk::tr("Access Error:   %ls\n"), err.raw());
        }

        if (fOptLong)
        {
            Bstr description;
            pMedium->COMGETTER(Description)(description.asOutParam());
            if (!description.isEmpty())
                RTPrintf(Disk::tr("Description:    %ls\n"), description.raw());
        }

        MediumType_T type;
        pMedium->COMGETTER(Type)(&type);
        const char *typeStr = Disk::tr("unknown");
        switch (type)
        {
            case MediumType_Normal:
                if (pszParentUUID && Guid(pszParentUUID).isValid())
                    typeStr = Disk::tr("normal (differencing)");
                else
                    typeStr = Disk::tr("normal (base)");
                break;
            case MediumType_Immutable:
                typeStr = Disk::tr("immutable");
                break;
            case MediumType_Writethrough:
                typeStr = Disk::tr("writethrough");
                break;
            case MediumType_Shareable:
                typeStr = Disk::tr("shareable");
                break;
            case MediumType_Readonly:
                typeStr = Disk::tr("readonly");
                break;
            case MediumType_MultiAttach:
                typeStr = Disk::tr("multiattach");
                break;
#ifdef VBOX_WITH_XPCOM_CPP_ENUM_HACK
            case MediumType_32BitHack: break; /* Shut up compiler warnings. */
#endif
        }
        RTPrintf(Disk::tr("Type:           %s\n"), typeStr);

        /* print out information specific for differencing media */
        if (fOptLong && pszParentUUID && Guid(pszParentUUID).isValid())
        {
            BOOL autoReset = FALSE;
            pMedium->COMGETTER(AutoReset)(&autoReset);
            RTPrintf(Disk::tr("Auto-Reset:     %s\n"), autoReset ? Disk::tr("on") : Disk::tr("off"));
        }

        Bstr loc;
        pMedium->COMGETTER(Location)(loc.asOutParam());
        RTPrintf(Disk::tr("Location:       %ls\n"), loc.raw());

        Bstr format;
        pMedium->COMGETTER(Format)(format.asOutParam());
        RTPrintf(Disk::tr("Storage format: %ls\n"), format.raw());

        if (fOptLong)
        {
            com::SafeArray<MediumVariant_T> safeArray_variant;

            pMedium->COMGETTER(Variant)(ComSafeArrayAsOutParam(safeArray_variant));
            ULONG variant=0;
            for (size_t i = 0; i < safeArray_variant.size(); i++)
                variant |= safeArray_variant[i];

            const char *variantStr = Disk::tr("unknown");
            switch (variant & ~(MediumVariant_Fixed | MediumVariant_Diff))
            {
                case MediumVariant_VmdkSplit2G:
                    variantStr = Disk::tr("split2G");
                    break;
                case MediumVariant_VmdkStreamOptimized:
                    variantStr = Disk::tr("streamOptimized");
                    break;
                case MediumVariant_VmdkESX:
                    variantStr = Disk::tr("ESX");
                    break;
                case MediumVariant_Standard:
                    variantStr = Disk::tr("default");
                    break;
            }
            const char *variantTypeStr = Disk::tr("dynamic");
            if (variant & MediumVariant_Fixed)
                variantTypeStr = Disk::tr("fixed");
            else if (variant & MediumVariant_Diff)
                variantTypeStr = Disk::tr("differencing");
            RTPrintf(Disk::tr("Format variant: %s %s\n"), variantTypeStr, variantStr);
        }

        LONG64 logicalSize;
        pMedium->COMGETTER(LogicalSize)(&logicalSize);
        RTPrintf(Disk::tr("Capacity:       %lld MBytes\n"), logicalSize >> 20);
        if (fOptLong)
        {
            LONG64 actualSize;
            pMedium->COMGETTER(Size)(&actualSize);
            RTPrintf(Disk::tr("Size on disk:   %lld MBytes\n"), actualSize >> 20);
        }

        Bstr strCipher;
        Bstr strPasswordId;
        HRESULT hrc2 = pMedium->GetEncryptionSettings(strCipher.asOutParam(), strPasswordId.asOutParam());
        if (SUCCEEDED(hrc2))
        {
            RTPrintf(Disk::tr("Encryption:     enabled\n"));
            if (fOptLong)
            {
                RTPrintf(Disk::tr("Cipher:         %ls\n"), strCipher.raw());
                RTPrintf(Disk::tr("Password ID:    %ls\n"), strPasswordId.raw());
            }
        }
        else
            RTPrintf(Disk::tr("Encryption:     disabled\n"));

        if (fOptLong)
        {
            com::SafeArray<BSTR> names;
            com::SafeArray<BSTR> values;
            pMedium->GetProperties(Bstr().raw(), ComSafeArrayAsOutParam(names), ComSafeArrayAsOutParam(values));
            size_t cNames = names.size();
            size_t cValues = values.size();
            bool fFirst = true;
            for (size_t i = 0; i < cNames; i++)
            {
                Bstr value;
                if (i < cValues)
                    value = values[i];
                RTPrintf("%s%ls=%ls\n",
                         fFirst ? Disk::tr("Property:       ") : "                ",
                         names[i], value.raw());
                fFirst = false;
            }
        }

        if (fOptLong)
        {
            bool fFirst = true;
            com::SafeArray<BSTR> machineIds;
            pMedium->COMGETTER(MachineIds)(ComSafeArrayAsOutParam(machineIds));
            for (size_t i = 0; i < machineIds.size(); i++)
            {
                ComPtr<IMachine> pMachine;
                CHECK_ERROR(pVirtualBox, FindMachine(machineIds[i], pMachine.asOutParam()));
                if (pMachine)
                {
                    Bstr name;
                    pMachine->COMGETTER(Name)(name.asOutParam());
                    pMachine->COMGETTER(Id)(uuid.asOutParam());
                    RTPrintf("%s%ls (UUID: %ls)",
                             fFirst ? Disk::tr("In use by VMs:  ") : "                ",
                             name.raw(), machineIds[i]);
                    fFirst = false;
                    com::SafeArray<BSTR> snapshotIds;
                    pMedium->GetSnapshotIds(machineIds[i],
                                            ComSafeArrayAsOutParam(snapshotIds));
                    for (size_t j = 0; j < snapshotIds.size(); j++)
                    {
                        ComPtr<ISnapshot> pSnapshot;
                        pMachine->FindSnapshot(snapshotIds[j], pSnapshot.asOutParam());
                        if (pSnapshot)
                        {
                            Bstr snapshotName;
                            pSnapshot->COMGETTER(Name)(snapshotName.asOutParam());
                            RTPrintf(" [%ls (UUID: %ls)]", snapshotName.raw(), snapshotIds[j]);
                        }
                    }
                    RTPrintf("\n");
                }
            }
        }

        if (fOptLong)
        {
            com::SafeIfaceArray<IMedium> children;
            pMedium->COMGETTER(Children)(ComSafeArrayAsOutParam(children));
            bool fFirst = true;
            for (size_t i = 0; i < children.size(); i++)
            {
                ComPtr<IMedium> pChild(children[i]);
                if (pChild)
                {
                    Bstr childUUID;
                    pChild->COMGETTER(Id)(childUUID.asOutParam());
                    RTPrintf("%s%ls\n",
                             fFirst ? Disk::tr("Child UUIDs:    ") : "                ",
                             childUUID.raw());
                    fFirst = false;
                }
            }
        }
    }
    while (0);

    return hrc;
}

static const RTGETOPTDEF g_aShowMediumInfoOptions[] =
{
    { "disk",           'd', RTGETOPT_REQ_NOTHING },
    { "dvd",            'D', RTGETOPT_REQ_NOTHING },
    { "floppy",         'f', RTGETOPT_REQ_NOTHING },
};

RTEXITCODE handleShowMediumInfo(HandlerArg *a)
{
    enum {
        CMD_NONE,
        CMD_DISK,
        CMD_DVD,
        CMD_FLOPPY
    } cmd = CMD_NONE;
    const char *pszFilenameOrUuid = NULL;

    int c;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    // start at 0 because main() has hacked both the argc and argv given to us
    RTGetOptInit(&GetState, a->argc, a->argv, g_aShowMediumInfoOptions, RT_ELEMENTS(g_aShowMediumInfoOptions),
                 0, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 'd':   // disk
                if (cmd != CMD_NONE)
                    return errorSyntax(Disk::tr("Only one command can be specified: '%s'"), ValueUnion.psz);
                cmd = CMD_DISK;
                break;

            case 'D':   // DVD
                if (cmd != CMD_NONE)
                    return errorSyntax(Disk::tr("Only one command can be specified: '%s'"), ValueUnion.psz);
                cmd = CMD_DVD;
                break;

            case 'f':   // floppy
                if (cmd != CMD_NONE)
                    return errorSyntax(Disk::tr("Only one command can be specified: '%s'"), ValueUnion.psz);
                cmd = CMD_FLOPPY;
                break;

            case VINF_GETOPT_NOT_OPTION:
                if (!pszFilenameOrUuid)
                    pszFilenameOrUuid = ValueUnion.psz;
                else
                    return errorSyntax(Disk::tr("Invalid parameter '%s'"), ValueUnion.psz);
                break;

            default:
                if (c > 0)
                {
                    if (RT_C_IS_PRINT(c))
                        return errorSyntax(Disk::tr("Invalid option -%c"), c);
                    else
                        return errorSyntax(Disk::tr("Invalid option case %i"), c);
                }
                else if (c == VERR_GETOPT_UNKNOWN_OPTION)
                    return errorSyntax(Disk::tr("unknown option: %s\n"), ValueUnion.psz);
                else if (ValueUnion.pDef)
                    return errorSyntax("%s: %Rrs", ValueUnion.pDef->pszLong, c);
                else
                    return errorSyntax(Disk::tr("error: %Rrs"), c);
        }
    }

    if (cmd == CMD_NONE)
        cmd = CMD_DISK;

    /* check for required options */
    if (!pszFilenameOrUuid)
        return errorSyntax(Disk::tr("Medium name or UUID required"));

    HRESULT hrc = S_OK; /* Prevents warning. */

    ComPtr<IMedium> pMedium;
    if (cmd == CMD_DISK)
        hrc = openMedium(a, pszFilenameOrUuid, DeviceType_HardDisk,
                         AccessMode_ReadOnly, pMedium,
                         false /* fForceNewUuidOnOpen */, false /* fSilent */);
    else if (cmd == CMD_DVD)
        hrc = openMedium(a, pszFilenameOrUuid, DeviceType_DVD,
                         AccessMode_ReadOnly, pMedium,
                         false /* fForceNewUuidOnOpen */, false /* fSilent */);
    else if (cmd == CMD_FLOPPY)
        hrc = openMedium(a, pszFilenameOrUuid, DeviceType_Floppy,
                         AccessMode_ReadOnly, pMedium,
                         false /* fForceNewUuidOnOpen */, false /* fSilent */);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    Utf8Str strParentUUID(Disk::tr("base"));
    ComPtr<IMedium> pParent;
    pMedium->COMGETTER(Parent)(pParent.asOutParam());
    if (!pParent.isNull())
    {
        Bstr bstrParentUUID;
        pParent->COMGETTER(Id)(bstrParentUUID.asOutParam());
        strParentUUID = bstrParentUUID;
    }

    hrc = showMediumInfo(a->virtualBox, pMedium, strParentUUID.c_str(), true);

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static const RTGETOPTDEF g_aCloseMediumOptions[] =
{
    { "disk",           'd', RTGETOPT_REQ_NOTHING },
    { "dvd",            'D', RTGETOPT_REQ_NOTHING },
    { "floppy",         'f', RTGETOPT_REQ_NOTHING },
    { "--delete",       'r', RTGETOPT_REQ_NOTHING },
};

RTEXITCODE handleCloseMedium(HandlerArg *a)
{
    HRESULT hrc = S_OK;
    enum {
        CMD_NONE,
        CMD_DISK,
        CMD_DVD,
        CMD_FLOPPY
    } cmd = CMD_NONE;
    const char *pszFilenameOrUuid = NULL;
    bool fDelete = false;

    int c;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    // start at 0 because main() has hacked both the argc and argv given to us
    RTGetOptInit(&GetState, a->argc, a->argv, g_aCloseMediumOptions, RT_ELEMENTS(g_aCloseMediumOptions),
                 0, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 'd':   // disk
                if (cmd != CMD_NONE)
                    return errorSyntax(Disk::tr("Only one command can be specified: '%s'"), ValueUnion.psz);
                cmd = CMD_DISK;
                break;

            case 'D':   // DVD
                if (cmd != CMD_NONE)
                    return errorSyntax(Disk::tr("Only one command can be specified: '%s'"), ValueUnion.psz);
                cmd = CMD_DVD;
                break;

            case 'f':   // floppy
                if (cmd != CMD_NONE)
                    return errorSyntax(Disk::tr("Only one command can be specified: '%s'"), ValueUnion.psz);
                cmd = CMD_FLOPPY;
                break;

            case 'r':   // --delete
                fDelete = true;
                break;

            case VINF_GETOPT_NOT_OPTION:
                if (!pszFilenameOrUuid)
                    pszFilenameOrUuid = ValueUnion.psz;
                else
                    return errorSyntax(Disk::tr("Invalid parameter '%s'"), ValueUnion.psz);
                break;

            default:
                if (c > 0)
                {
                    if (RT_C_IS_PRINT(c))
                        return errorSyntax(Disk::tr("Invalid option -%c"), c);
                    else
                        return errorSyntax(Disk::tr("Invalid option case %i"), c);
                }
                else if (c == VERR_GETOPT_UNKNOWN_OPTION)
                    return errorSyntax(Disk::tr("unknown option: %s\n"), ValueUnion.psz);
                else if (ValueUnion.pDef)
                    return errorSyntax("%s: %Rrs", ValueUnion.pDef->pszLong, c);
                else
                    return errorSyntax(Disk::tr("error: %Rrs"), c);
        }
    }

    /* check for required options */
    if (cmd == CMD_NONE)
        cmd = CMD_DISK;
    if (!pszFilenameOrUuid)
        return errorSyntax(Disk::tr("Medium name or UUID required"));

    ComPtr<IMedium> pMedium;
    if (cmd == CMD_DISK)
        hrc = openMedium(a, pszFilenameOrUuid, DeviceType_HardDisk,
                         AccessMode_ReadWrite, pMedium,
                         false /* fForceNewUuidOnOpen */, false /* fSilent */);
    else if (cmd == CMD_DVD)
        hrc = openMedium(a, pszFilenameOrUuid, DeviceType_DVD,
                         AccessMode_ReadOnly, pMedium,
                         false /* fForceNewUuidOnOpen */, false /* fSilent */);
    else if (cmd == CMD_FLOPPY)
        hrc = openMedium(a, pszFilenameOrUuid, DeviceType_Floppy,
                         AccessMode_ReadWrite, pMedium,
                         false /* fForceNewUuidOnOpen */, false /* fSilent */);

    if (SUCCEEDED(hrc) && pMedium)
    {
        if (fDelete)
        {
            ComPtr<IProgress> pProgress;
            CHECK_ERROR(pMedium, DeleteStorage(pProgress.asOutParam()));
            if (SUCCEEDED(hrc))
            {
                hrc = showProgress(pProgress);
                CHECK_PROGRESS_ERROR(pProgress, (Disk::tr("Failed to delete medium")));
            }
            else
                RTMsgError(Disk::tr("Failed to delete medium. Error code %Rhrc"), hrc);
        }
        CHECK_ERROR(pMedium, Close());
    }

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

RTEXITCODE handleMediumProperty(HandlerArg *a)
{
    HRESULT hrc = S_OK;
    const char *pszCmd = NULL;
    enum {
        CMD_NONE,
        CMD_DISK,
        CMD_DVD,
        CMD_FLOPPY
    } cmd = CMD_NONE;
    const char *pszAction = NULL;
    const char *pszFilenameOrUuid = NULL;
    const char *pszProperty = NULL;
    ComPtr<IMedium> pMedium;

    pszCmd = (a->argc > 0) ? a->argv[0] : "";
    if (   !RTStrICmp(pszCmd, "disk")
        || !RTStrICmp(pszCmd, "dvd")
        || !RTStrICmp(pszCmd, "floppy"))
    {
        if (!RTStrICmp(pszCmd, "disk"))
            cmd = CMD_DISK;
        else if (!RTStrICmp(pszCmd, "dvd"))
            cmd = CMD_DVD;
        else if (!RTStrICmp(pszCmd, "floppy"))
            cmd = CMD_FLOPPY;
        else
        {
            AssertMsgFailed((Disk::tr("unexpected parameter %s\n"), pszCmd));
            cmd = CMD_DISK;
        }
        a->argv++;
        a->argc--;
    }
    else
    {
        pszCmd = NULL;
        cmd = CMD_DISK;
    }

    if (a->argc == 0)
        return errorSyntax(Disk::tr("Missing action"));

    pszAction = a->argv[0];
    if (   RTStrICmp(pszAction, "set")
        && RTStrICmp(pszAction, "get")
        && RTStrICmp(pszAction, "delete"))
        return errorSyntax(Disk::tr("Invalid action given: %s"), pszAction);

    if (   (   !RTStrICmp(pszAction, "set")
            && a->argc != 4)
        || (   RTStrICmp(pszAction, "set")
            && a->argc != 3))
        return errorSyntax(Disk::tr("Invalid number of arguments given for action: %s"), pszAction);

    pszFilenameOrUuid = a->argv[1];
    pszProperty       = a->argv[2];

    if (cmd == CMD_DISK)
        hrc = openMedium(a, pszFilenameOrUuid, DeviceType_HardDisk,
                         AccessMode_ReadWrite, pMedium,
                         false /* fForceNewUuidOnOpen */, false /* fSilent */);
    else if (cmd == CMD_DVD)
        hrc = openMedium(a, pszFilenameOrUuid, DeviceType_DVD,
                         AccessMode_ReadOnly, pMedium,
                         false /* fForceNewUuidOnOpen */, false /* fSilent */);
    else if (cmd == CMD_FLOPPY)
        hrc = openMedium(a, pszFilenameOrUuid, DeviceType_Floppy,
                         AccessMode_ReadWrite, pMedium,
                         false /* fForceNewUuidOnOpen */, false /* fSilent */);
    if (SUCCEEDED(hrc) && !pMedium.isNull())
    {
        if (!RTStrICmp(pszAction, "set"))
        {
            const char *pszValue = a->argv[3];
            CHECK_ERROR(pMedium, SetProperty(Bstr(pszProperty).raw(), Bstr(pszValue).raw()));
        }
        else if (!RTStrICmp(pszAction, "get"))
        {
            /*
             * Trigger a call to Medium::i_queryInfo()->VDOpen()->pfnOpen() to
             * open the virtual device and populate its properties for
             * Medium::getProperty() to retrieve.
             */
            MediumState_T state;
            CHECK_ERROR(pMedium, RefreshState(&state));

            Bstr strVal;
            CHECK_ERROR(pMedium, GetProperty(Bstr(pszProperty).raw(), strVal.asOutParam()));
            if (SUCCEEDED(hrc))
                RTPrintf("%s=%ls\n", pszProperty, strVal.raw());
        }
        else if (!RTStrICmp(pszAction, "delete"))
        {
            CHECK_ERROR(pMedium, SetProperty(Bstr(pszProperty).raw(), Bstr().raw()));
            /** @todo */
        }
    }

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static const RTGETOPTDEF g_aEncryptMediumOptions[] =
{
    { "--newpassword",   'n', RTGETOPT_REQ_STRING },
    { "--oldpassword",   'o', RTGETOPT_REQ_STRING },
    { "--cipher",        'c', RTGETOPT_REQ_STRING },
    { "--newpasswordid", 'i', RTGETOPT_REQ_STRING }
};

RTEXITCODE handleEncryptMedium(HandlerArg *a)
{
    HRESULT hrc;
    ComPtr<IMedium> hardDisk;
    const char *pszPasswordNew = NULL;
    const char *pszPasswordOld = NULL;
    const char *pszCipher = NULL;
    const char *pszFilenameOrUuid = NULL;
    const char *pszNewPasswordId = NULL;
    Utf8Str strPasswordNew;
    Utf8Str strPasswordOld;

    int c;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    // start at 0 because main() has hacked both the argc and argv given to us
    RTGetOptInit(&GetState, a->argc, a->argv, g_aEncryptMediumOptions, RT_ELEMENTS(g_aEncryptMediumOptions),
                 0, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 'n':   // --newpassword
                pszPasswordNew = ValueUnion.psz;
                break;

            case 'o':   // --oldpassword
                pszPasswordOld = ValueUnion.psz;
                break;

            case 'c':   // --cipher
                pszCipher = ValueUnion.psz;
                break;

            case 'i':   // --newpasswordid
                pszNewPasswordId = ValueUnion.psz;
                break;

            case VINF_GETOPT_NOT_OPTION:
                if (!pszFilenameOrUuid)
                    pszFilenameOrUuid = ValueUnion.psz;
                else
                    return errorSyntax(Disk::tr("Invalid parameter '%s'"), ValueUnion.psz);
                break;

            default:
                if (c > 0)
                {
                    if (RT_C_IS_PRINT(c))
                        return errorSyntax(Disk::tr("Invalid option -%c"), c);
                    else
                        return errorSyntax(Disk::tr("Invalid option case %i"), c);
                }
                else if (c == VERR_GETOPT_UNKNOWN_OPTION)
                    return errorSyntax(Disk::tr("unknown option: %s\n"), ValueUnion.psz);
                else if (ValueUnion.pDef)
                    return errorSyntax("%s: %Rrs", ValueUnion.pDef->pszLong, c);
                else
                    return errorSyntax(Disk::tr("error: %Rrs"), c);
        }
    }

    if (!pszFilenameOrUuid)
        return errorSyntax(Disk::tr("Disk name or UUID required"));

    if (!pszPasswordNew && !pszPasswordOld)
        return errorSyntax(Disk::tr("No password specified"));

    if (   (pszPasswordNew && !pszNewPasswordId)
        || (!pszPasswordNew && pszNewPasswordId))
        return errorSyntax(Disk::tr("A new password must always have a valid identifier set at the same time"));

    if (pszPasswordNew)
    {
        if (!RTStrCmp(pszPasswordNew, "-"))
        {
            /* Get password from console. */
            RTEXITCODE rcExit = readPasswordFromConsole(&strPasswordNew, Disk::tr("Enter new password:"));
            if (rcExit == RTEXITCODE_FAILURE)
                return rcExit;
        }
        else
        {
            RTEXITCODE rcExit = readPasswordFile(pszPasswordNew, &strPasswordNew);
            if (rcExit == RTEXITCODE_FAILURE)
            {
                RTMsgError(Disk::tr("Failed to read new password from file"));
                return rcExit;
            }
        }
    }

    if (pszPasswordOld)
    {
        if (!RTStrCmp(pszPasswordOld, "-"))
        {
            /* Get password from console. */
            RTEXITCODE rcExit = readPasswordFromConsole(&strPasswordOld, Disk::tr("Enter old password:"));
            if (rcExit == RTEXITCODE_FAILURE)
                return rcExit;
        }
        else
        {
            RTEXITCODE rcExit = readPasswordFile(pszPasswordOld, &strPasswordOld);
            if (rcExit == RTEXITCODE_FAILURE)
            {
                RTMsgError(Disk::tr("Failed to read old password from file"));
                return rcExit;
            }
        }
    }

    /* Always open the medium if necessary, there is no other way. */
    hrc = openMedium(a, pszFilenameOrUuid, DeviceType_HardDisk,
                     AccessMode_ReadWrite, hardDisk,
                     false /* fForceNewUuidOnOpen */, false /* fSilent */);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;
    if (hardDisk.isNull())
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Disk::tr("Invalid hard disk reference, avoiding crash"));

    ComPtr<IProgress> progress;
    CHECK_ERROR(hardDisk, ChangeEncryption(Bstr(strPasswordOld).raw(), Bstr(pszCipher).raw(),
                                           Bstr(strPasswordNew).raw(), Bstr(pszNewPasswordId).raw(),
                                           progress.asOutParam()));
    if (SUCCEEDED(hrc))
        hrc = showProgress(progress);
    if (FAILED(hrc))
    {
        if (hrc == E_NOTIMPL)
            RTMsgError(Disk::tr("Encrypt hard disk operation is not implemented!"));
        else if (hrc == VBOX_E_NOT_SUPPORTED)
            RTMsgError(Disk::tr("Encrypt hard disk operation for this cipher is not implemented yet!"));
        else if (!progress.isNull())
            CHECK_PROGRESS_ERROR(progress, (Disk::tr("Failed to encrypt hard disk")));
        else
            RTMsgError(Disk::tr("Failed to encrypt hard disk!"));
    }

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

RTEXITCODE handleCheckMediumPassword(HandlerArg *a)
{
    HRESULT hrc;
    ComPtr<IMedium> hardDisk;
    const char *pszFilenameOrUuid = NULL;
    Utf8Str strPassword;

    if (a->argc != 2)
        return errorSyntax(Disk::tr("Invalid number of arguments: %d"), a->argc);

    pszFilenameOrUuid = a->argv[0];

    if (!RTStrCmp(a->argv[1], "-"))
    {
        /* Get password from console. */
        RTEXITCODE rcExit = readPasswordFromConsole(&strPassword, Disk::tr("Enter password:"));
        if (rcExit == RTEXITCODE_FAILURE)
            return rcExit;
    }
    else
    {
        RTEXITCODE rcExit = readPasswordFile(a->argv[1], &strPassword);
        if (rcExit == RTEXITCODE_FAILURE)
        {
            RTMsgError(Disk::tr("Failed to read password from file"));
            return rcExit;
        }
    }

    /* Always open the medium if necessary, there is no other way. */
    hrc = openMedium(a, pszFilenameOrUuid, DeviceType_HardDisk,
                     AccessMode_ReadWrite, hardDisk,
                     false /* fForceNewUuidOnOpen */, false /* fSilent */);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;
    if (hardDisk.isNull())
        return RTMsgErrorExit(RTEXITCODE_FAILURE, Disk::tr("Invalid hard disk reference, avoiding crash"));

    CHECK_ERROR(hardDisk, CheckEncryptionPassword(Bstr(strPassword).raw()));
    if (SUCCEEDED(hrc))
        RTPrintf(Disk::tr("The given password is correct\n"));
    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/*********************************************************************************************************************************
*   The mediumio command                                                                                                         *
*********************************************************************************************************************************/

/**
 * Common MediumIO options.
 */
typedef struct MEDIUMIOCOMMONOPT
{
    const char     *pszFilenameOrUuid;
    DeviceType_T    enmDeviceType;
    const char     *pszPasswordFile;
} MEDIUMIOCOMMONOPT;
typedef MEDIUMIOCOMMONOPT *PMEDIUMIOCOMMONOPT;
typedef MEDIUMIOCOMMONOPT const *PCMEDIUMIOCOMMONOPT;

/* For RTGETOPTDEF array initializer. */
#define MEDIUMIOCOMMONOPT_DEFS() \
    { "--disk",          'd', RTGETOPT_REQ_STRING }, \
    { "--harddisk",      'd', RTGETOPT_REQ_STRING }, \
    { "disk",            'd', RTGETOPT_REQ_STRING }, \
    { "harddisk",        'd', RTGETOPT_REQ_STRING }, \
    { "--dvd",           'D', RTGETOPT_REQ_STRING }, \
    { "--iso",           'D', RTGETOPT_REQ_STRING }, \
    { "dvd",             'D', RTGETOPT_REQ_STRING }, \
    { "iso",             'D', RTGETOPT_REQ_STRING }, \
    { "--floppy",        'f', RTGETOPT_REQ_STRING }, \
    { "floppy",          'f', RTGETOPT_REQ_STRING }, \
    { "--password-file", 'P', RTGETOPT_REQ_STRING }

/* For option switch. */
#define MEDIUMIOCOMMONOPT_CASES(a_pCommonOpts) \
    case 'd': \
        (a_pCommonOpts)->enmDeviceType     = DeviceType_HardDisk; \
        (a_pCommonOpts)->pszFilenameOrUuid = ValueUnion.psz; \
        break; \
    case 'D': \
        (a_pCommonOpts)->enmDeviceType     = DeviceType_DVD; \
        (a_pCommonOpts)->pszFilenameOrUuid = ValueUnion.psz; \
        break; \
    case 'f': \
        (a_pCommonOpts)->enmDeviceType     = DeviceType_Floppy; \
        (a_pCommonOpts)->pszFilenameOrUuid = ValueUnion.psz; \
        break; \
    case 'P': \
        (a_pCommonOpts)->pszPasswordFile = ValueUnion.psz; \
        break


/**
 * Worker for mediumio operations that returns a IMediumIO for the specified
 * medium.
 *
 * @returns Exit code.
 * @param   pHandler        The handler state structure (for IVirtualBox).
 * @param   pCommonOpts     Common mediumio options.
 * @param   fWritable       Whether to open writable (true) or read only
 *                          (false).
 * @param   rPtrMediumIO    Where to return the IMediumIO pointer.
 * @param   pcbMedium       Where to return the meidum size. Optional.
 */
static RTEXITCODE mediumIOOpenMediumForIO(HandlerArg *pHandler, PCMEDIUMIOCOMMONOPT pCommonOpts, bool fWritable,
                                          ComPtr<IMediumIO> &rPtrMediumIO, uint64_t *pcbMedium = NULL)
{
    /* Clear returns. */
    if (pcbMedium)
        *pcbMedium = 0;
    rPtrMediumIO.setNull();

    /*
     * Make sure a medium was specified already.
     */
    if (pCommonOpts->enmDeviceType == DeviceType_Null)
        return errorSyntax(Disk::tr("No medium specified!"));

    /*
     * Read the password.
     */
    Bstr bstrPassword;
    if (pCommonOpts->pszPasswordFile)
    {
        Utf8Str strPassword;
        RTEXITCODE rcExit;
        if (pCommonOpts->pszPasswordFile[0] == '-' && pCommonOpts->pszPasswordFile[1] == '\0')
            rcExit = readPasswordFromConsole(&strPassword, Disk::tr("Enter encryption password:"));
        else
            rcExit = readPasswordFile(pCommonOpts->pszPasswordFile, &strPassword);
        if (rcExit != RTEXITCODE_SUCCESS)
            return rcExit;
        bstrPassword = strPassword;
        strPassword.assign(strPassword.length(), '*');
    }

    /*
     * Open the medium and then get I/O access to it.
     */
    ComPtr<IMedium> ptrMedium;
    HRESULT hrc = openMedium(pHandler, pCommonOpts->pszFilenameOrUuid, pCommonOpts->enmDeviceType,
                             fWritable ? AccessMode_ReadWrite : AccessMode_ReadOnly,
                             ptrMedium, false /* fForceNewUuidOnOpen */, false /* fSilent */);
    if (SUCCEEDED(hrc))
    {
        CHECK_ERROR2I_STMT(ptrMedium, OpenForIO(fWritable, bstrPassword.raw(), rPtrMediumIO.asOutParam()), hrc = hrcCheck);

        /*
         * If the size is requested get it after we've opened it.
         */
        if (pcbMedium && SUCCEEDED(hrc))
        {
            LONG64 cbLogical = 0;
            CHECK_ERROR2I_STMT(ptrMedium, COMGETTER(LogicalSize)(&cbLogical), hrc = hrcCheck);
            *pcbMedium = cbLogical;
            if (!SUCCEEDED(hrc))
                rPtrMediumIO.setNull();
        }
    }

    if (bstrPassword.isNotEmpty())
        memset(bstrPassword.mutableRaw(), '*', bstrPassword.length() * sizeof(RTUTF16));
    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/**
 * mediumio formatfat
 */
static RTEXITCODE handleMediumIOFormatFat(HandlerArg *a, int iFirst, PMEDIUMIOCOMMONOPT pCommonOpts)
{
    /*
     * Parse the options.
     */
    bool fQuick = false;
    static const RTGETOPTDEF s_aOptions[] =
    {
        MEDIUMIOCOMMONOPT_DEFS(),
        { "--quick",  'q', RTGETOPT_REQ_NOTHING },
    };

    RTGETOPTSTATE GetState;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);
    RTGETOPTUNION ValueUnion;
    while ((vrc = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (vrc)
        {
            MEDIUMIOCOMMONOPT_CASES(pCommonOpts);

            case 'q':
                fQuick = true;
                break;

            default:
                return errorGetOpt(vrc, &ValueUnion);
        }
    }

    /*
     * Open the medium for I/O and format it.
     */
    ComPtr<IMediumIO> ptrMediumIO;
    RTEXITCODE rcExit = mediumIOOpenMediumForIO(a, pCommonOpts, true /*fWritable*/, ptrMediumIO);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    CHECK_ERROR2I_RET(ptrMediumIO, FormatFAT(fQuick), RTEXITCODE_FAILURE);
    return RTEXITCODE_SUCCESS;
}

/**
 * mediumio cat
 */
static RTEXITCODE handleMediumIOCat(HandlerArg *a, int iFirst, PMEDIUMIOCOMMONOPT pCommonOpts)
{
    /*
     * Parse the options.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        MEDIUMIOCOMMONOPT_DEFS(),
        { "--hex",      'H', RTGETOPT_REQ_NOTHING },
        { "--offset",   'o', RTGETOPT_REQ_UINT64  },
        { "--output",   'O', RTGETOPT_REQ_STRING  },
        { "--size",     's', RTGETOPT_REQ_UINT64  },
    };
    bool        fHex      = false;
    uint64_t    off       = 0;
    const char *pszOutput = NULL;
    uint64_t    cb        = UINT64_MAX;

    RTGETOPTSTATE GetState;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);
    RTGETOPTUNION ValueUnion;
    while ((vrc = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (vrc)
        {
            MEDIUMIOCOMMONOPT_CASES(pCommonOpts);

            case 'H':
                fHex = true;
                break;

            case 'o':
                off = ValueUnion.u64;
                break;

            case 'O':
                pszOutput = ValueUnion.psz;
                break;

            case 's':
                cb = ValueUnion.u64;
                break;

            default:
                return errorGetOpt(vrc, &ValueUnion);
        }
    }

    /*
     * Open the medium for I/O.
     */
    ComPtr<IMediumIO>   ptrMediumIO;
    uint64_t            cbMedium;
    RTEXITCODE rcExit = mediumIOOpenMediumForIO(a, pCommonOpts, false /*fWritable*/, ptrMediumIO, &cbMedium);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        /*
         * Do we have an output file or do we write to stdout?
         */
        PRTSTREAM pOut = NULL;
        if (pszOutput && (pszOutput[0] != '-' || pszOutput[1] != '\0'))
        {
            vrc = RTStrmOpen(pszOutput, fHex ? "wt" : "wb", &pOut);
            if (RT_FAILURE(vrc))
                rcExit = RTMsgErrorExitFailure(Disk::tr("Error opening '%s' for writing: %Rrc"), pszOutput, vrc);
        }
        else
        {
            pOut = g_pStdOut;
            if (!fHex)
                RTStrmSetMode(pOut, true, -1);
        }

        if (rcExit == RTEXITCODE_SUCCESS)
        {
            /*
             * Adjust 'cb' now that we've got the medium size.
             */
            if (off >= cbMedium)
            {
                RTMsgWarning(Disk::tr("Specified offset (%#RX64) is beyond the end of the medium (%#RX64)"), off, cbMedium);
                cb = 0;
            }
            else if (   cb > cbMedium
                     || cb + off > cbMedium)
                cb = cbMedium - off;

            /*
             * Hex dump preps.  (The duplication detection is making ASSUMPTIONS about
             * all the reads being a multiple of cchWidth, except for the final one.)
             */
            char           abHexBuf[16]   = { 0 };
            size_t         cbHexBuf       = 0;
            unsigned const cchWidth       = RT_ELEMENTS(abHexBuf);
            uint64_t const offEndDupCheck = cb - cchWidth;
            uint64_t       cDuplicates    = 0;

            /*
             * Do the reading.
             */
            while (cb > 0)
            {
                char szLine[32 + cchWidth * 4 + 32];

                /* Do the reading. */
                uint32_t const  cbToRead = (uint32_t)RT_MIN(cb, _128K);
                SafeArray<BYTE> SafeArrayBuf;
                HRESULT hrc = ptrMediumIO->Read(off, cbToRead, ComSafeArrayAsOutParam(SafeArrayBuf));
                if (FAILED(hrc))
                {
                    RTStrPrintf(szLine, sizeof(szLine), Disk::tr("Read(%zu bytes at %#RX64)", "", cbToRead), cbToRead, off);
                    com::GlueHandleComError(ptrMediumIO, szLine, hrc, __FILE__, __LINE__);
                    break;
                }

                /* Output the data. */
                size_t const cbReturned = SafeArrayBuf.size();
                if (cbReturned)
                {
                    BYTE const *pbBuf = SafeArrayBuf.raw();
                    if (!fHex)
                        vrc = RTStrmWrite(pOut, pbBuf, cbReturned);
                    else
                    {
                        /* hexdump -C */
                        vrc = VINF_SUCCESS;
                        uint64_t        offHex    = off;
                        uint64_t const  offHexEnd = off + cbReturned;
                        while (offHex < offHexEnd)
                        {
                            if (   offHex >= offEndDupCheck
                                || cbHexBuf == 0
                                || memcmp(pbBuf, abHexBuf, cchWidth) != 0
                                || (   cDuplicates == 0
                                    && (   offHex + cchWidth >= offEndDupCheck
                                        || memcmp(pbBuf + cchWidth, pbBuf, cchWidth) != 0)) )
                            {
                                if (cDuplicates > 0)
                                {
                                    RTStrmPrintf(pOut, Disk::tr("**********  <ditto x %RU64>\n"), cDuplicates);
                                    cDuplicates = 0;
                                }

                                size_t   cch = RTStrPrintf(szLine, sizeof(szLine), "%012RX64:", offHex);
                                unsigned i;
                                for (i = 0; i < cchWidth && offHex + i < offHexEnd; i++)
                                {
                                    static const char s_szHexDigits[17] = "0123456789abcdef";
                                    szLine[cch++] = (i & 7) || i == 0 ? ' ' : '-';
                                    uint8_t const u8 = pbBuf[i];
                                    szLine[cch++] = s_szHexDigits[u8 >> 4];
                                    szLine[cch++] = s_szHexDigits[u8 & 0xf];
                                }
                                while (i++ < cchWidth)
                                {
                                    szLine[cch++] = ' ';
                                    szLine[cch++] = ' ';
                                    szLine[cch++] = ' ';
                                }
                                szLine[cch++] = ' ';

                                for (i = 0; i < cchWidth && offHex + i < offHexEnd; i++)
                                {
                                    uint8_t const u8 = pbBuf[i];
                                    szLine[cch++] = u8 < 127 && u8 >= 32 ? u8 : '.';
                                }
                                szLine[cch++] = '\n';
                                szLine[cch]   = '\0';

                                vrc = RTStrmWrite(pOut, szLine, cch);
                                if (RT_FAILURE(vrc))
                                    break;


                                /* copy bytes over to the duplication detection buffer. */
                                cbHexBuf = (size_t)RT_MIN(cchWidth, offHexEnd - offHex);
                                memcpy(abHexBuf, pbBuf, cbHexBuf);
                            }
                            else
                                cDuplicates++;

                            /* Advance to next line. */
                            pbBuf  += cchWidth;
                            offHex += cchWidth;
                        }
                    }
                    if (RT_FAILURE(vrc))
                    {
                        rcExit = RTMsgErrorExitFailure(Disk::tr("Error writing to '%s': %Rrc"), pszOutput, vrc);
                        break;
                    }
                }

                /* Advance. */
                if (cbReturned != cbToRead)
                {
                    rcExit = RTMsgErrorExitFailure(Disk::tr("Expected read() at offset %RU64 (%#RX64) to return %#zx bytes, only got %#zx!\n",
                                                            "", cbReturned),
                                                   off, off, cbReturned, cbToRead);
                    break;
                }
                off += cbReturned;
                cb  -= cbReturned;
            }

            /*
             * Close output.
             */
            if (pOut != g_pStdOut)
            {
                vrc = RTStrmClose(pOut);
                if (RT_FAILURE(vrc))
                    rcExit = RTMsgErrorExitFailure(Disk::tr("Error closing '%s': %Rrc"), pszOutput, vrc);
            }
            else if (!fHex)
                RTStrmSetMode(pOut, false, -1);
        }
    }
    return rcExit;
}

/**
 * mediumio stream
 */
static RTEXITCODE handleMediumIOStream(HandlerArg *a, int iFirst, PMEDIUMIOCOMMONOPT pCommonOpts)
{
    /*
     * Parse the options.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        MEDIUMIOCOMMONOPT_DEFS(),
        { "--output",   'O', RTGETOPT_REQ_STRING },
        { "--format",   'F', RTGETOPT_REQ_STRING },
        { "--variant",  'v', RTGETOPT_REQ_STRING }
    };
    const char *pszOutput = NULL;
    MediumVariant_T enmMediumVariant = MediumVariant_Standard;
    Bstr strFormat;

    RTGETOPTSTATE GetState;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);
    RTGETOPTUNION ValueUnion;
    while ((vrc = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (vrc)
        {
            MEDIUMIOCOMMONOPT_CASES(pCommonOpts);

            case 'O':
                pszOutput = ValueUnion.psz;
                break;
            case 'F':
                strFormat = ValueUnion.psz;
                break;
            case 'v':   // --variant
            {
                vrc = parseMediumVariant(ValueUnion.psz, &enmMediumVariant);
                if (RT_FAILURE(vrc))
                    return errorArgument(Disk::tr("Invalid medium variant '%s'"), ValueUnion.psz);
                break;
            }

            default:
                return errorGetOpt(vrc, &ValueUnion);
        }
    }

    /*
     * Open the medium for I/O.
     */
    ComPtr<IMediumIO>   ptrMediumIO;
    uint64_t            cbMedium;
    RTEXITCODE rcExit = mediumIOOpenMediumForIO(a, pCommonOpts, false /*fWritable*/, ptrMediumIO, &cbMedium);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        /*
         * Do we have an output file or do we write to stdout?
         */
        PRTSTREAM pOut = NULL;
        if (pszOutput && (pszOutput[0] != '-' || pszOutput[1] != '\0'))
        {
            vrc = RTStrmOpen(pszOutput, "wb", &pOut);
            if (RT_FAILURE(vrc))
                rcExit = RTMsgErrorExitFailure(Disk::tr("Error opening '%s' for writing: %Rrc"), pszOutput, vrc);
        }
        else
        {
            pOut = g_pStdOut;
            RTStrmSetMode(pOut, true, -1);
        }

        if (rcExit == RTEXITCODE_SUCCESS)
        {
            ComPtr<IDataStream> ptrDataStream;
            ComPtr<IProgress> ptrProgress;

            com::SafeArray<MediumVariant_T> l_variants(sizeof(MediumVariant_T)*8);

            for (ULONG i = 0; i < l_variants.size(); ++i)
            {
                ULONG temp = enmMediumVariant;
                temp &= 1<<i;
                l_variants [i] = (MediumVariant_T)temp;
            }

            HRESULT hrc = ptrMediumIO->ConvertToStream(strFormat.raw(), ComSafeArrayAsInParam(l_variants), 10 * _1M, ptrDataStream.asOutParam(), ptrProgress.asOutParam());
            if (hrc == S_OK)
            {
                /* Read until we reached the end of the stream. */
                for (;;)
                {
                    SafeArray<BYTE> SafeArrayBuf;

                    hrc = ptrDataStream->Read(_64K, 0 /*Infinite wait*/, ComSafeArrayAsOutParam(SafeArrayBuf));
                    if (   FAILED(hrc)
                        || SafeArrayBuf.size() == 0)
                        break;

                    /* Output the data. */
                    size_t const cbReturned = SafeArrayBuf.size();
                    if (cbReturned)
                    {
                        BYTE const *pbBuf = SafeArrayBuf.raw();
                        vrc = RTStrmWrite(pOut, pbBuf, cbReturned);
                        if (RT_FAILURE(vrc))
                        {
                            rcExit = RTMsgErrorExitFailure(Disk::tr("Error writing to '%s': %Rrc"), pszOutput, vrc);
                            break;
                        }
                    }

                    /** @todo Check progress. */
                }
            }
            else
            {
                com::GlueHandleComError(ptrMediumIO, "ConvertToStream()", hrc, __FILE__, __LINE__);
                rcExit = RTEXITCODE_FAILURE;
            }

            /*
             * Close output.
             */
            if (pOut != g_pStdOut)
            {
                vrc = RTStrmClose(pOut);
                if (RT_FAILURE(vrc))
                    rcExit = RTMsgErrorExitFailure(Disk::tr("Error closing '%s': %Rrc"), pszOutput, vrc);
            }
            else
                RTStrmSetMode(pOut, false, -1);
        }
    }
    return rcExit;
}


RTEXITCODE handleMediumIO(HandlerArg *a)
{
    /*
     * Parse image-option and sub-command.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        MEDIUMIOCOMMONOPT_DEFS(),
        /* sub-commands */
        { "formatfat",  1000, RTGETOPT_REQ_NOTHING },
        { "cat",        1001, RTGETOPT_REQ_NOTHING },
        { "stream",     1002, RTGETOPT_REQ_NOTHING },
    };
    MEDIUMIOCOMMONOPT   CommonOpts = { NULL, DeviceType_Null, NULL };

    RTGETOPTSTATE       GetState;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);
    RTGETOPTUNION       ValueUnion;
    while ((vrc = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (vrc)
        {
            MEDIUMIOCOMMONOPT_CASES(&CommonOpts);

            /* Sub-commands: */
            case 1000:
                setCurrentSubcommand(HELP_SCOPE_MEDIUMIO_FORMATFAT);
                return handleMediumIOFormatFat(a, GetState.iNext, &CommonOpts);
            case 1001:
                setCurrentSubcommand(HELP_SCOPE_MEDIUMIO_CAT);
                return handleMediumIOCat(a, GetState.iNext, &CommonOpts);
            case 1002:
                setCurrentSubcommand(HELP_SCOPE_MEDIUMIO_STREAM);
                return handleMediumIOStream(a, GetState.iNext, &CommonOpts);

            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);

            default:
                return errorGetOpt(vrc, &ValueUnion);
        }
    }
    return errorNoSubcommand();
}
