/* $Id: VBoxManageAppliance.cpp 110133 2025-07-07 12:49:50Z brent.paulson@oracle.com $ */
/** @file
 * VBoxManage - The appliance-related commands.
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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
#include <VBox/log.h>
#include <VBox/param.h>

#include <VBox/version.h>

#include <list>
#include <map>

#include <iprt/getopt.h>
#include <iprt/ctype.h>
#include <iprt/path.h>
#include <iprt/file.h>
#include <iprt/err.h>
#include <iprt/zip.h>
#include <iprt/stream.h>
#include <iprt/vfs.h>
#include <iprt/manifest.h>
#include <iprt/crypto/digest.h>
#include <iprt/crypto/x509.h>
#include <iprt/crypto/pkcs7.h>
#include <iprt/crypto/store.h>
#include <iprt/crypto/spc.h>
#include <iprt/crypto/key.h>
#include <iprt/crypto/pkix.h>



#include "VBoxManage.h"
using namespace com;

DECLARE_TRANSLATION_CONTEXT(Appliance);


// funcs
///////////////////////////////////////////////////////////////////////////////

typedef std::map<Utf8Str, Utf8Str> ArgsMap;                 // pairs of strings like "vmname" => "newvmname"
typedef std::map<uint32_t, ArgsMap> ArgsMapsMap;            // map of maps, one for each virtual system, sorted by index

typedef std::map<uint32_t, bool> IgnoresMap;                // pairs of numeric description entry indices
typedef std::map<uint32_t, IgnoresMap> IgnoresMapsMap;      // map of maps, one for each virtual system, sorted by index

static bool findArgValue(Utf8Str &strOut,
                         ArgsMap *pmapArgs,
                         const Utf8Str &strKey)
{
    if (pmapArgs)
    {
        ArgsMap::iterator it;
        it = pmapArgs->find(strKey);
        if (it != pmapArgs->end())
        {
            strOut = it->second;
            pmapArgs->erase(it);
            return true;
        }
    }

    return false;
}

static int parseImportOptions(const char *psz, com::SafeArray<ImportOptions_T> *options)
{
    int vrc = VINF_SUCCESS;
    while (psz && *psz && RT_SUCCESS(vrc))
    {
        size_t len;
        const char *pszComma = strchr(psz, ',');
        if (pszComma)
            len = (size_t)(pszComma - psz);
        else
            len = strlen(psz);
        if (len > 0)
        {
            if (!RTStrNICmp(psz, "KeepAllMACs", len))
                options->push_back(ImportOptions_KeepAllMACs);
            else if (!RTStrNICmp(psz, "KeepNATMACs", len))
                options->push_back(ImportOptions_KeepNATMACs);
            else if (!RTStrNICmp(psz, "ImportToVDI", len))
                options->push_back(ImportOptions_ImportToVDI);
            else
                vrc = VERR_PARSE_ERROR;
        }
        if (pszComma)
            psz += len + 1;
        else
            psz += len;
    }

    return vrc;
}

/**
 * Helper routine to parse the ExtraData Utf8Str for a storage controller's
 * value or channel value.
 *
 * @param   aExtraData    The ExtraData string which can have a format of
 *                        either 'controller=13;channel=3' or '11'.
 * @param   pszKey        The string being looked up, usually either 'controller'
 *                        or 'channel' but can be NULL or empty.
 * @param   puVal         The integer value of the 'controller=' or 'channel='
 *                        key (or the controller number when there is no key) in
 *                        the ExtraData string.
 * @returns COM status code.
 */
static int getStorageControllerDetailsFromStr(const com::Utf8Str &aExtraData, const char *pszKey, uint32_t *puVal)
{
    int vrc;

    if (pszKey && *pszKey)
    {
        size_t posKey = aExtraData.find(pszKey);
        if (posKey == Utf8Str::npos)
            return VERR_INVALID_PARAMETER;
        vrc = RTStrToUInt32Ex(aExtraData.c_str() + posKey + strlen(pszKey), NULL, 0, puVal);
    }
    else
    {
        vrc = RTStrToUInt32Ex(aExtraData.c_str(), NULL, 0, puVal);
    }

    if (vrc == VWRN_NUMBER_TOO_BIG || vrc == VWRN_NEGATIVE_UNSIGNED)
        return VERR_INVALID_PARAMETER;

    return vrc;
}

static bool isStorageControllerType(VirtualSystemDescriptionType_T avsdType)
{
    switch (avsdType)
    {
        case VirtualSystemDescriptionType_HardDiskControllerIDE:
        case VirtualSystemDescriptionType_HardDiskControllerNVMe:
        case VirtualSystemDescriptionType_HardDiskControllerSATA:
        case VirtualSystemDescriptionType_HardDiskControllerSCSI:
        case VirtualSystemDescriptionType_HardDiskControllerSAS:
        case VirtualSystemDescriptionType_HardDiskControllerVirtioSCSI:
            return true;
        default:
            return false;
    }
}

static const RTGETOPTDEF g_aImportApplianceOptions[] =
{
    { "--dry-run",              'n', RTGETOPT_REQ_NOTHING },
    { "-dry-run",               'n', RTGETOPT_REQ_NOTHING },    // deprecated
    { "--dryrun",               'n', RTGETOPT_REQ_NOTHING },
    { "-dryrun",                'n', RTGETOPT_REQ_NOTHING },    // deprecated
    { "--detailed-progress",    'P', RTGETOPT_REQ_NOTHING },
    { "-detailed-progress",     'P', RTGETOPT_REQ_NOTHING },    // deprecated
    { "--vsys",                 's', RTGETOPT_REQ_UINT32 },
    { "-vsys",                  's', RTGETOPT_REQ_UINT32 },     // deprecated
    { "--ostype",               'o', RTGETOPT_REQ_STRING },
    { "-ostype",                'o', RTGETOPT_REQ_STRING },     // deprecated
    { "--vmname",               'V', RTGETOPT_REQ_STRING },
    { "-vmname",                'V', RTGETOPT_REQ_STRING },     // deprecated
    { "--settingsfile",         'S', RTGETOPT_REQ_STRING },
    { "--basefolder",           'p', RTGETOPT_REQ_STRING },
    { "--group",                'g', RTGETOPT_REQ_STRING },
    { "--memory",               'm', RTGETOPT_REQ_STRING },
    { "-memory",                'm', RTGETOPT_REQ_STRING },     // deprecated
    { "--cpus",                 'c', RTGETOPT_REQ_STRING },
    { "--description",          'd', RTGETOPT_REQ_STRING },
    { "--eula",                 'L', RTGETOPT_REQ_STRING },
    { "-eula",                  'L', RTGETOPT_REQ_STRING },     // deprecated
    { "--unit",                 'u', RTGETOPT_REQ_UINT32 },
    { "-unit",                  'u', RTGETOPT_REQ_UINT32 },     // deprecated
    { "--ignore",               'x', RTGETOPT_REQ_NOTHING },
    { "-ignore",                'x', RTGETOPT_REQ_NOTHING },    // deprecated
    { "--scsitype",             'T', RTGETOPT_REQ_UINT32 },
    { "-scsitype",              'T', RTGETOPT_REQ_UINT32 },     // deprecated
    { "--type",                 'T', RTGETOPT_REQ_UINT32 },     // deprecated
    { "-type",                  'T', RTGETOPT_REQ_UINT32 },     // deprecated
    { "--controller",           'C', RTGETOPT_REQ_STRING },
    { "--port",                 'E', RTGETOPT_REQ_STRING },
    { "--disk",                 'D', RTGETOPT_REQ_STRING },
    { "--options",              'O', RTGETOPT_REQ_STRING },

    { "--cloud",                'j', RTGETOPT_REQ_NOTHING},
    { "--cloudprofile",         'k', RTGETOPT_REQ_STRING },
    { "--cloudinstanceid",      'l', RTGETOPT_REQ_STRING },
    { "--cloudbucket",          'B', RTGETOPT_REQ_STRING }
};

typedef enum APPLIANCETYPE
{
    NOT_SET, LOCAL, CLOUD
} APPLIANCETYPE;

RTEXITCODE handleImportAppliance(HandlerArg *arg)
{
    HRESULT hrc = S_OK;
    APPLIANCETYPE enmApplType = NOT_SET;
    Utf8Str strOvfFilename;
    bool fExecute = true;                  // if true, then we actually do the import
    com::SafeArray<ImportOptions_T> options;
    uint32_t ulCurVsys = (uint32_t)-1;
    uint32_t ulCurUnit = (uint32_t)-1;
    // for each --vsys X command, maintain a map of command line items
    // (we'll parse them later after interpreting the OVF, when we can
    // actually check whether they make sense semantically)
    ArgsMapsMap mapArgsMapsPerVsys;
    IgnoresMapsMap mapIgnoresMapsPerVsys;

    int c;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    // start at 0 because main() has hacked both the argc and argv given to us
    RTGetOptInit(&GetState, arg->argc, arg->argv, g_aImportApplianceOptions, RT_ELEMENTS(g_aImportApplianceOptions),
                 0, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 'n':   // --dry-run
                fExecute = false;
                break;

            case 'P':   // --detailed-progress
                g_fDetailedProgress = true;
                break;

            case 's':   // --vsys
                if (enmApplType == NOT_SET)
                    enmApplType = LOCAL;

                if (enmApplType != LOCAL)
                    return errorSyntax(Appliance::tr("Option \"%s\" can't be used together with \"--cloud\" option."),
                                       GetState.pDef->pszLong);
                if (ValueUnion.u32 == (uint32_t)-1)
                    return errorSyntax(Appliance::tr("Value of option \"%s\" is out of range."),
                                       GetState.pDef->pszLong);

                ulCurVsys = ValueUnion.u32;
                ulCurUnit = (uint32_t)-1;
                break;

            case 'o':   // --ostype
                if (enmApplType == NOT_SET)
                    return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --vsys or --cloud option."),
                                       GetState.pDef->pszLong);
                mapArgsMapsPerVsys[ulCurVsys]["ostype"] = ValueUnion.psz;
                break;

            case 'V':   // --vmname
                if (enmApplType == NOT_SET)
                    return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --vsys or --cloud option."),
                                       GetState.pDef->pszLong);
                mapArgsMapsPerVsys[ulCurVsys]["vmname"] = ValueUnion.psz;
                break;

            case 'S':   // --settingsfile
                if (enmApplType != LOCAL)
                    return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --vsys option."),
                                       GetState.pDef->pszLong);
                mapArgsMapsPerVsys[ulCurVsys]["settingsfile"] = ValueUnion.psz;
                break;

            case 'p':   // --basefolder
                if (enmApplType == NOT_SET)
                    return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --vsys or --cloud option."),
                                       GetState.pDef->pszLong);
                mapArgsMapsPerVsys[ulCurVsys]["basefolder"] = ValueUnion.psz;
                break;

            case 'g':   // --group
                if (enmApplType != LOCAL)
                    return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --vsys option."),
                                       GetState.pDef->pszLong);
                mapArgsMapsPerVsys[ulCurVsys]["group"] = ValueUnion.psz;
                break;

            case 'd':   // --description
                if (enmApplType == NOT_SET)
                    return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --vsys or --cloud option."),
                                       GetState.pDef->pszLong);
                mapArgsMapsPerVsys[ulCurVsys]["description"] = ValueUnion.psz;
                break;

            case 'L':   // --eula
                if (enmApplType != LOCAL)
                    return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --vsys option."),
                                       GetState.pDef->pszLong);
                mapArgsMapsPerVsys[ulCurVsys]["eula"] = ValueUnion.psz;
                break;

            case 'm':   // --memory
                if (enmApplType == NOT_SET)
                    return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --vsys or --cloud option."),
                                       GetState.pDef->pszLong);
                mapArgsMapsPerVsys[ulCurVsys]["memory"] = ValueUnion.psz;
                break;

            case 'c':   // --cpus
                if (enmApplType == NOT_SET)
                    return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --vsys or --cloud option."),
                                       GetState.pDef->pszLong);
                mapArgsMapsPerVsys[ulCurVsys]["cpus"] = ValueUnion.psz;
                break;

            case 'u':   // --unit
                if (enmApplType != LOCAL)
                    return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --vsys option."),
                                       GetState.pDef->pszLong);
                if (ValueUnion.u32 == (uint32_t)-1)
                    return errorSyntax(Appliance::tr("Value of option \"%s\" is out of range."),
                                       GetState.pDef->pszLong);

                ulCurUnit = ValueUnion.u32;
                break;

            case 'x':   // --ignore
                if (enmApplType != LOCAL)
                    return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --vsys option."),
                                       GetState.pDef->pszLong);
                if (ulCurUnit == (uint32_t)-1)
                    return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --unit option."),
                                       GetState.pDef->pszLong);
                mapIgnoresMapsPerVsys[ulCurVsys][ulCurUnit] = true;
                break;

            case 'T':   // --scsitype
                if (enmApplType != LOCAL)
                    return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --vsys option."),
                                       GetState.pDef->pszLong);
                if (ulCurUnit == (uint32_t)-1)
                    return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --unit option."),
                                       GetState.pDef->pszLong);
                mapArgsMapsPerVsys[ulCurVsys][Utf8StrFmt("scsitype%u", ulCurUnit)] = ValueUnion.psz;
                break;

            case 'C':   // --controller
                if (enmApplType != LOCAL)
                    return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --vsys option."),
                                       GetState.pDef->pszLong);
                if (ulCurUnit == (uint32_t)-1)
                    return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --unit option."),
                                       GetState.pDef->pszLong);
                mapArgsMapsPerVsys[ulCurVsys][Utf8StrFmt("controller%u", ulCurUnit)] = ValueUnion.psz;
                break;

            case 'E':   // --port
                if (enmApplType != LOCAL)
                    return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --vsys option."),
                                       GetState.pDef->pszLong);
                if (ulCurUnit == (uint32_t)-1)
                    return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --unit option."),
                                       GetState.pDef->pszLong);
                mapArgsMapsPerVsys[ulCurVsys][Utf8StrFmt("port%u", ulCurUnit)] = ValueUnion.psz;
                break;

            case 'D':   // --disk
                if (enmApplType != LOCAL)
                    return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --vsys option."),
                                       GetState.pDef->pszLong);
                if (ulCurUnit == (uint32_t)-1)
                    return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --unit option."),
                                       GetState.pDef->pszLong);
                mapArgsMapsPerVsys[ulCurVsys][Utf8StrFmt("disk%u", ulCurUnit)] = ValueUnion.psz;
                break;

            case 'O':   // --options
                if (RT_FAILURE(parseImportOptions(ValueUnion.psz, &options)))
                    return errorArgument(Appliance::tr("Invalid import options '%s'\n"), ValueUnion.psz);
                break;

                /*--cloud and --vsys are orthogonal, only one must be presented*/
            case 'j':   // --cloud
                if (enmApplType == NOT_SET)
                    enmApplType = CLOUD;

                if (enmApplType != CLOUD)
                    return errorSyntax(Appliance::tr("Option \"%s\" can't be used together with \"--vsys\" option."),
                                       GetState.pDef->pszLong);

                ulCurVsys = 0;
                break;

                /* Cloud export settings */
            case 'k':   // --cloudprofile
                if (enmApplType != CLOUD)
                    return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --cloud option."),
                                       GetState.pDef->pszLong);
                mapArgsMapsPerVsys[ulCurVsys]["cloudprofile"] = ValueUnion.psz;
                break;

            case 'l':   // --cloudinstanceid
                if (enmApplType != CLOUD)
                    return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --cloud option."),
                                       GetState.pDef->pszLong);
                mapArgsMapsPerVsys[ulCurVsys]["cloudinstanceid"] = ValueUnion.psz;
                break;

            case 'B':   // --cloudbucket
                if (enmApplType != CLOUD)
                    return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --cloud option."),
                                       GetState.pDef->pszLong);
                mapArgsMapsPerVsys[ulCurVsys]["cloudbucket"] = ValueUnion.psz;
                break;

            case VINF_GETOPT_NOT_OPTION:
                if (strOvfFilename.isEmpty())
                    strOvfFilename = ValueUnion.psz;
                else
                    return errorSyntax(Appliance::tr("Invalid parameter '%s'"), ValueUnion.psz);
                break;

            default:
                if (c > 0)
                {
                    if (RT_C_IS_PRINT(c))
                        return errorSyntax(Appliance::tr("Invalid option -%c"), c);
                    else
                        return errorSyntax(Appliance::tr("Invalid option case %i"), c);
                }
                else if (c == VERR_GETOPT_UNKNOWN_OPTION)
                    return errorSyntax(Appliance::tr("unknown option: %s\n"), ValueUnion.psz);
                else if (ValueUnion.pDef)
                    return errorSyntax("%s: %Rrs", ValueUnion.pDef->pszLong, c);
                else
                    return errorSyntax(Appliance::tr("error: %Rrs"), c);
        }
    }

    /* Last check after parsing all arguments */
    if (strOvfFilename.isEmpty())
        return errorSyntax(Appliance::tr("Not enough arguments for \"import\" command."));

    if (enmApplType == NOT_SET)
        enmApplType = LOCAL;

    do
    {
        ComPtr<IAppliance> pAppliance;
        CHECK_ERROR_BREAK(arg->virtualBox, CreateAppliance(pAppliance.asOutParam()));
        //in the case of Cloud, append the instance id here because later it's harder to do
        if (enmApplType == CLOUD)
        {
            try
            {
                /* Check presence of cloudprofile and cloudinstanceid in the map.
                 * If there isn't the exception is triggered. It's standard std:map logic.*/
                ArgsMap a = mapArgsMapsPerVsys[ulCurVsys];
                (void)a.at("cloudprofile");
                (void)a.at("cloudinstanceid");
            }
            catch (...)
            {
                return errorSyntax(Appliance::tr("Not enough arguments for import from the Cloud."));
            }

            strOvfFilename.append(mapArgsMapsPerVsys[ulCurVsys]["cloudprofile"]);
            strOvfFilename.append("/");
            strOvfFilename.append(mapArgsMapsPerVsys[ulCurVsys]["cloudinstanceid"]);
        }

        char *pszAbsFilePath;
        if (strOvfFilename.startsWith("S3://", RTCString::CaseInsensitive) ||
            strOvfFilename.startsWith("SunCloud://", RTCString::CaseInsensitive) ||
            strOvfFilename.startsWith("webdav://", RTCString::CaseInsensitive) ||
            strOvfFilename.startsWith("OCI://", RTCString::CaseInsensitive))
            pszAbsFilePath = RTStrDup(strOvfFilename.c_str());
        else
            pszAbsFilePath = RTPathAbsDup(strOvfFilename.c_str());

        ComPtr<IProgress> progressRead;
        CHECK_ERROR_BREAK(pAppliance, Read(Bstr(pszAbsFilePath).raw(),
                                           progressRead.asOutParam()));
        RTStrFree(pszAbsFilePath);

        hrc = showProgress(progressRead);
        CHECK_PROGRESS_ERROR_RET(progressRead, (Appliance::tr("Appliance read failed")), RTEXITCODE_FAILURE);

        Bstr path; /* fetch the path, there is stuff like username/password removed if any */
        CHECK_ERROR_BREAK(pAppliance, COMGETTER(Path)(path.asOutParam()));

        size_t cVirtualSystemDescriptions = 0;
        com::SafeIfaceArray<IVirtualSystemDescription> aVirtualSystemDescriptions;

        if (enmApplType == LOCAL)
        {
            // call interpret(); this can yield both warnings and errors, so we need
            // to tinker with the error info a bit
            RTStrmPrintf(g_pStdErr, Appliance::tr("Interpreting %ls...\n"), path.raw());
            hrc = pAppliance->Interpret();
            com::ErrorInfoKeeper eik;

            /** @todo r=klaus Eliminate this special way of signalling
             * warnings which should be part of the ErrorInfo. */
            com::SafeArray<BSTR> aWarnings;
            if (SUCCEEDED(pAppliance->GetWarnings(ComSafeArrayAsOutParam(aWarnings))))
            {
                size_t cWarnings = aWarnings.size();
                for (unsigned i = 0; i < cWarnings; ++i)
                {
                    Bstr bstrWarning(aWarnings[i]);
                    RTMsgWarning("%ls", bstrWarning.raw());
                }
            }

            eik.restore();
            if (FAILED(hrc))     // during interpret, after printing warnings
            {
                com::GlueHandleComError(pAppliance, "Interpret()", hrc, __FILE__, __LINE__);
                break;
            }

            RTStrmPrintf(g_pStdErr, "OK.\n");

            // fetch all disks
            com::SafeArray<BSTR> retDisks;
            CHECK_ERROR_BREAK(pAppliance,
                              COMGETTER(Disks)(ComSafeArrayAsOutParam(retDisks)));
            if (retDisks.size() > 0)
            {
                RTPrintf(Appliance::tr("Disks:\n"));
                for (unsigned i = 0; i < retDisks.size(); i++)
                    RTPrintf("  %ls\n", retDisks[i]);
                RTPrintf("\n");
            }

            // fetch virtual system descriptions
            CHECK_ERROR_BREAK(pAppliance,
                              COMGETTER(VirtualSystemDescriptions)(ComSafeArrayAsOutParam(aVirtualSystemDescriptions)));

            cVirtualSystemDescriptions = aVirtualSystemDescriptions.size();

            // match command line arguments with virtual system descriptions;
            // this is only to sort out invalid indices at this time
            ArgsMapsMap::const_iterator it;
            for (it = mapArgsMapsPerVsys.begin();
                 it != mapArgsMapsPerVsys.end();
                 ++it)
            {
                uint32_t ulVsys = it->first;
                if (ulVsys >= cVirtualSystemDescriptions)
                    return errorSyntax(Appliance::tr("Invalid index %RI32 with -vsys option; the OVF contains only %zu virtual system(s).",
                                                     "", cVirtualSystemDescriptions),
                                       ulVsys, cVirtualSystemDescriptions);
            }
        }
        else if (enmApplType == CLOUD)
        {
            /* In the Cloud case the call of interpret() isn't needed because there isn't any OVF XML file.
             * All info is got from the Cloud and VSD is filled inside IAppliance::read(). */
            // fetch virtual system descriptions
            CHECK_ERROR_BREAK(pAppliance,
                              COMGETTER(VirtualSystemDescriptions)(ComSafeArrayAsOutParam(aVirtualSystemDescriptions)));

            cVirtualSystemDescriptions = aVirtualSystemDescriptions.size();
        }

        uint32_t cLicensesInTheWay = 0;

        // dump virtual system descriptions and match command-line arguments
        if (cVirtualSystemDescriptions > 0)
        {
            for (unsigned i = 0; i < cVirtualSystemDescriptions; ++i)
            {
                com::SafeArray<VirtualSystemDescriptionType_T> retTypes;
                com::SafeArray<BSTR> aRefs;
                com::SafeArray<BSTR> aOvfValues;
                com::SafeArray<BSTR> aVBoxValues;
                com::SafeArray<BSTR> aExtraConfigValues;
                CHECK_ERROR_BREAK(aVirtualSystemDescriptions[i],
                                  GetDescription(ComSafeArrayAsOutParam(retTypes),
                                                 ComSafeArrayAsOutParam(aRefs),
                                                 ComSafeArrayAsOutParam(aOvfValues),
                                                 ComSafeArrayAsOutParam(aVBoxValues),
                                                 ComSafeArrayAsOutParam(aExtraConfigValues)));

                RTPrintf(Appliance::tr("Virtual system %u:\n"), i);

                // look up the corresponding command line options, if any
                ArgsMap *pmapArgs = NULL;
                ArgsMapsMap::iterator itm = mapArgsMapsPerVsys.find(i);
                if (itm != mapArgsMapsPerVsys.end())
                    pmapArgs = &itm->second;

                // this collects the final values for setFinalValues()
                com::SafeArray<BOOL> aEnabled(retTypes.size());
                com::SafeArray<BSTR> aFinalValues(retTypes.size());

                for (unsigned a = 0; a < retTypes.size(); ++a)
                {
                    VirtualSystemDescriptionType_T t = retTypes[a];

                    Utf8Str strOverride;

                    Bstr bstrFinalValue = aVBoxValues[a];

                    bool fIgnoreThis = mapIgnoresMapsPerVsys[i][a];

                    aEnabled[a] = true;

                    switch (t)
                    {
                        case VirtualSystemDescriptionType_OS:
                            if (findArgValue(strOverride, pmapArgs, "ostype"))
                            {
                                bstrFinalValue = strOverride;
                                RTPrintf(Appliance::tr("%2u: OS type specified with --ostype: \"%ls\"\n"),
                                        a, bstrFinalValue.raw());
                            }
                            else
                                RTPrintf(Appliance::tr("%2u: Suggested OS type: \"%ls\"\n"
                                            "    (change with \"--vsys %u --ostype <type>\"; use \"list ostypes\" to list all possible values)\n"),
                                        a, bstrFinalValue.raw(), i);
                            break;

                        case VirtualSystemDescriptionType_Name:
                            if (findArgValue(strOverride, pmapArgs, "vmname"))
                            {
                                bstrFinalValue = strOverride;
                                RTPrintf(Appliance::tr("%2u: VM name specified with --vmname: \"%ls\"\n"),
                                        a, bstrFinalValue.raw());
                            }
                            else
                                RTPrintf(Appliance::tr("%2u: Suggested VM name \"%ls\"\n"
                                            "    (change with \"--vsys %u --vmname <name>\")\n"),
                                        a, bstrFinalValue.raw(), i);
                            break;

                        case VirtualSystemDescriptionType_Product:
                            RTPrintf(Appliance::tr("%2u: Product (ignored): %ls\n"),
                                     a, aVBoxValues[a]);
                            break;

                        case VirtualSystemDescriptionType_ProductUrl:
                            RTPrintf(Appliance::tr("%2u: ProductUrl (ignored): %ls\n"),
                                     a, aVBoxValues[a]);
                            break;

                        case VirtualSystemDescriptionType_Vendor:
                            RTPrintf(Appliance::tr("%2u: Vendor (ignored): %ls\n"),
                                     a, aVBoxValues[a]);
                            break;

                        case VirtualSystemDescriptionType_VendorUrl:
                            RTPrintf(Appliance::tr("%2u: VendorUrl (ignored): %ls\n"),
                                     a, aVBoxValues[a]);
                            break;

                        case VirtualSystemDescriptionType_Version:
                            RTPrintf(Appliance::tr("%2u: Version (ignored): %ls\n"),
                                     a, aVBoxValues[a]);
                            break;

                        case VirtualSystemDescriptionType_Description:
                            if (findArgValue(strOverride, pmapArgs, "description"))
                            {
                                bstrFinalValue = strOverride;
                                RTPrintf(Appliance::tr("%2u: Description specified with --description: \"%ls\"\n"),
                                        a, bstrFinalValue.raw());
                            }
                            else
                                RTPrintf(Appliance::tr("%2u: Description \"%ls\"\n"
                                            "    (change with \"--vsys %u --description <desc>\")\n"),
                                        a, bstrFinalValue.raw(), i);
                            break;

                        case VirtualSystemDescriptionType_License:
                            ++cLicensesInTheWay;
                            if (findArgValue(strOverride, pmapArgs, "eula"))
                            {
                                if (strOverride == "show")
                                {
                                    RTPrintf(Appliance::tr("%2u: End-user license agreement\n"
                                                "    (accept with \"--vsys %u --eula accept\"):\n"
                                                "\n%ls\n\n"),
                                             a, i, bstrFinalValue.raw());
                                }
                                else if (strOverride == "accept")
                                {
                                    RTPrintf(Appliance::tr("%2u: End-user license agreement (accepted)\n"),
                                             a);
                                    --cLicensesInTheWay;
                                }
                                else
                                    return errorSyntax(Appliance::tr("Argument to --eula must be either \"show\" or \"accept\"."));
                            }
                            else
                                RTPrintf(Appliance::tr("%2u: End-user license agreement\n"
                                            "    (display with \"--vsys %u --eula show\";\n"
                                            "    accept with \"--vsys %u --eula accept\")\n"),
                                        a, i, i);
                            break;

                        case VirtualSystemDescriptionType_CPU:
                            if (findArgValue(strOverride, pmapArgs, "cpus"))
                            {
                                uint32_t cCPUs;
                                if (    strOverride.toInt(cCPUs) == VINF_SUCCESS
                                     && cCPUs >= VMM_MIN_CPU_COUNT
                                     && cCPUs <= VMM_MAX_CPU_COUNT
                                   )
                                {
                                    bstrFinalValue = strOverride;
                                    RTPrintf(Appliance::tr("%2u: No. of CPUs specified with --cpus: %ls\n"),
                                             a, bstrFinalValue.raw());
                                }
                                else
                                    return errorSyntax(Appliance::tr("Argument to --cpus option must be a number greater than %d and less than %d."),
                                                       VMM_MIN_CPU_COUNT - 1, VMM_MAX_CPU_COUNT + 1);
                            }
                            else
                                RTPrintf(Appliance::tr("%2u: Number of CPUs: %ls\n    (change with \"--vsys %u --cpus <n>\")\n"),
                                         a, bstrFinalValue.raw(), i);
                            break;

                        case VirtualSystemDescriptionType_Memory:
                        {
                            if (findArgValue(strOverride, pmapArgs, "memory"))
                            {
                                uint32_t ulMemMB;
                                if (VINF_SUCCESS == strOverride.toInt(ulMemMB))
                                {
                                    /* 'VBoxManage import --memory' size is in megabytes */
                                    RTPrintf(Appliance::tr("%2u: Guest memory specified with --memory: %RU32 MB\n"),
                                             a, ulMemMB);

                                   /* IVirtualSystemDescription guest memory size is in bytes.
                                      It's alway stored in bytes in VSD according to the old internal agreement within the team */
                                    uint64_t ullMemBytes = (uint64_t)ulMemMB * _1M;
                                    strOverride = Utf8StrFmt("%RU64", ullMemBytes);
                                    bstrFinalValue = strOverride;
                                }
                                else
                                    return errorSyntax(Appliance::tr("Argument to --memory option must be a non-negative number."));
                            }
                            else
                            {
                                strOverride = aVBoxValues[a];
                                uint64_t ullMemMB = strOverride.toUInt64() / _1M;
                                RTPrintf(Appliance::tr("%2u: Guest memory: %RU64 MB\n    (change with \"--vsys %u --memory <MB>\")\n"),
                                         a, ullMemMB, i);
                            }
                            break;
                        }

                        case VirtualSystemDescriptionType_HardDiskControllerIDE:
                            if (fIgnoreThis)
                            {
                                RTPrintf(Appliance::tr("%2u: IDE controller, type %ls -- disabled\n"),
                                         a,
                                         aVBoxValues[a]);
                                aEnabled[a] = false;
                            }
                            else
                                RTPrintf(Appliance::tr("%2u: IDE controller, type %ls\n"
                                            "    (disable with \"--vsys %u --unit %u --ignore\")\n"),
                                         a,
                                         aVBoxValues[a],
                                         i, a);
                            break;

                        case VirtualSystemDescriptionType_HardDiskControllerSATA:
                            if (fIgnoreThis)
                            {
                                RTPrintf(Appliance::tr("%2u: SATA controller, type %ls -- disabled\n"),
                                         a,
                                         aVBoxValues[a]);
                                aEnabled[a] = false;
                            }
                            else
                                RTPrintf(Appliance::tr("%2u: SATA controller, type %ls\n"
                                            "    (disable with \"--vsys %u --unit %u --ignore\")\n"),
                                        a,
                                        aVBoxValues[a],
                                        i, a);
                            break;

                        case VirtualSystemDescriptionType_HardDiskControllerSAS:
                            if (fIgnoreThis)
                            {
                                RTPrintf(Appliance::tr("%2u: SAS controller, type %ls -- disabled\n"),
                                         a,
                                         aVBoxValues[a]);
                                aEnabled[a] = false;
                            }
                            else
                                RTPrintf(Appliance::tr("%2u: SAS controller, type %ls\n"
                                            "    (disable with \"--vsys %u --unit %u --ignore\")\n"),
                                        a,
                                        aVBoxValues[a],
                                        i, a);
                            break;

                        case VirtualSystemDescriptionType_HardDiskControllerSCSI:
                            if (fIgnoreThis)
                            {
                                RTPrintf(Appliance::tr("%2u: SCSI controller, type %ls -- disabled\n"),
                                         a,
                                         aVBoxValues[a]);
                                aEnabled[a] = false;
                            }
                            else
                            {
                                Utf8StrFmt strTypeArg("scsitype%u", a);
                                if (findArgValue(strOverride, pmapArgs, strTypeArg))
                                {
                                    bstrFinalValue = strOverride;
                                    RTPrintf(Appliance::tr("%2u: SCSI controller, type set with --unit %u --scsitype: \"%ls\"\n"),
                                            a,
                                            a,
                                            bstrFinalValue.raw());
                                }
                                else
                                    RTPrintf(Appliance::tr("%2u: SCSI controller, type %ls\n"
                                                "    (change with \"--vsys %u --unit %u --scsitype {BusLogic|LsiLogic}\";\n"
                                                "    disable with \"--vsys %u --unit %u --ignore\")\n"),
                                            a,
                                            aVBoxValues[a],
                                            i, a, i, a);
                            }
                            break;

                        case VirtualSystemDescriptionType_HardDiskControllerVirtioSCSI:
                            if (fIgnoreThis)
                            {
                                RTPrintf(Appliance::tr("%2u: VirtioSCSI controller, type %ls -- disabled\n"),
                                         a,
                                         aVBoxValues[a]);
                                aEnabled[a] = false;
                            }
                            else
                                RTPrintf(Appliance::tr("%2u: VirtioSCSI controller, type %ls\n"
                                            "    (disable with \"--vsys %u --unit %u --ignore\")\n"),
                                        a,
                                        aVBoxValues[a],
                                        i, a);
                            break;

                        case VirtualSystemDescriptionType_HardDiskControllerNVMe:
                            if (fIgnoreThis)
                            {
                                RTPrintf(Appliance::tr("%2u: NVMe controller, type %ls -- disabled\n"),
                                         a,
                                         aVBoxValues[a]);
                                aEnabled[a] = false;
                            }
                            else
                                RTPrintf(Appliance::tr("%2u: NVMe controller, type %ls\n"
                                            "    (disable with \"--vsys %u --unit %u --ignore\")\n"),
                                        a,
                                        aVBoxValues[a],
                                        i, a);
                            break;

                        case VirtualSystemDescriptionType_HardDiskImage:
                            if (fIgnoreThis)
                            {
                                RTPrintf(Appliance::tr("%2u: Hard disk image: source image=%ls -- disabled\n"),
                                         a,
                                         aOvfValues[a]);
                                aEnabled[a] = false;
                            }
                            else
                            {
                                Utf8StrFmt strTypeArg("disk%u", a);
                                bool fDiskChanged = false;
                                int vrc;
                                RTCList<ImportOptions_T> optionsList = options.toList();

                                if (findArgValue(strOverride, pmapArgs, strTypeArg))
                                {
                                    if (optionsList.contains(ImportOptions_ImportToVDI))
                                        return errorSyntax(Appliance::tr("Option --ImportToVDI can not be used together with a manually set target path."));
                                    RTUUID uuid;
                                    /* Check if this is a uuid. If so, don't touch. */
                                    vrc = RTUuidFromStr(&uuid, strOverride.c_str());
                                    if (vrc != VINF_SUCCESS)
                                    {
                                        /* Make the path absolute. */
                                        if (!RTPathStartsWithRoot(strOverride.c_str()))
                                        {
                                            char pszPwd[RTPATH_MAX];
                                            vrc = RTPathGetCurrent(pszPwd, RTPATH_MAX);
                                            if (RT_SUCCESS(vrc))
                                                strOverride = Utf8Str(pszPwd).append(RTPATH_SLASH).append(strOverride);
                                        }
                                    }
                                    bstrFinalValue = strOverride;
                                    fDiskChanged = true;
                                }

                                strTypeArg.printf("controller%u", a);
                                bool fControllerChanged = false;
                                uint32_t uTargetController = (uint32_t)-1;
                                VirtualSystemDescriptionType_T vsdControllerType = VirtualSystemDescriptionType_Ignore;
                                Utf8Str strExtraConfigValue;
                                if (findArgValue(strOverride, pmapArgs, strTypeArg))
                                {
                                    vrc = getStorageControllerDetailsFromStr(strOverride, NULL, &uTargetController);
                                    if (RT_FAILURE(vrc))
                                        return errorSyntax(Appliance::tr("Invalid controller value: '%s'"),
                                                           strOverride.c_str());

                                    vsdControllerType = retTypes[uTargetController];
                                    if (!isStorageControllerType(vsdControllerType))
                                        return errorSyntax(Appliance::tr("Invalid storage controller specified: %u"),
                                                           uTargetController);

                                    fControllerChanged = true;
                                }

                                strTypeArg.printf("port%u", a);
                                bool fControllerPortChanged = false;
                                uint32_t uTargetControllerPort = (uint32_t)-1;;
                                if (findArgValue(strOverride, pmapArgs, strTypeArg))
                                {
                                    vrc = getStorageControllerDetailsFromStr(strOverride, NULL, &uTargetControllerPort);
                                    if (RT_FAILURE(vrc))
                                        return errorSyntax(Appliance::tr("Invalid port value: '%s'"),
                                                           strOverride.c_str());

                                    fControllerPortChanged = true;
                                }

                                /*
                                 * aExtraConfigValues[a] has a format of 'controller=12;channel=0' and is set by
                                 * Appliance::interpret() so any parsing errors here aren't due to user-supplied
                                 * values so different error messages here.
                                 */
                                uint32_t uOrigController;
                                Utf8Str strOrigController(Bstr(aExtraConfigValues[a]).raw());
                                vrc = getStorageControllerDetailsFromStr(strOrigController, "controller=", &uOrigController);
                                if (RT_FAILURE(vrc))
                                    return RTMsgErrorExitFailure(Appliance::tr("Failed to extract controller value from ExtraConfig: '%s'"),
                                                                 strOrigController.c_str());

                                uint32_t uOrigControllerPort;
                                vrc = getStorageControllerDetailsFromStr(strOrigController, "channel=", &uOrigControllerPort);
                                if (RT_FAILURE(vrc))
                                    return RTMsgErrorExitFailure(Appliance::tr("Failed to extract channel value from ExtraConfig: '%s'"),
                                                                 strOrigController.c_str());

                                /*
                                 * The 'strExtraConfigValue' string is used to display the storage controller and
                                 * port details for each virtual hard disk using the more accurate 'controller=' and
                                 * 'port=' labels. The aExtraConfigValues[a] string has a format of
                                 * 'controller=%u;channel=%u' from Appliance::interpret() which is required as per
                                 * the API but for consistency and clarity with the CLI options --controller and
                                 * --port we instead use strExtraConfigValue in the output below.
                                 */
                                strExtraConfigValue = Utf8StrFmt("controller=%u;port=%u", uOrigController, uOrigControllerPort);

                                if (fControllerChanged || fControllerPortChanged)
                                {
                                    /*
                                     * Verify that the new combination of controller and controller port is valid.
                                     * cf. StorageController::i_checkPortAndDeviceValid()
                                     */
                                    if (uTargetControllerPort == (uint32_t)-1)
                                        uTargetControllerPort = uOrigControllerPort;
                                    if (uTargetController == (uint32_t)-1)
                                        uTargetController = uOrigController;

                                    if (   uOrigController == uTargetController
                                        && uOrigControllerPort == uTargetControllerPort)
                                        return errorSyntax(Appliance::tr("Device already attached to controller %u at this port (%u) location."),
                                                           uTargetController,
                                                           uTargetControllerPort);

                                    if (vsdControllerType == VirtualSystemDescriptionType_Ignore)
                                        vsdControllerType = retTypes[uOrigController];
                                    if (!isStorageControllerType(vsdControllerType))
                                        return errorSyntax(Appliance::tr("Invalid storage controller specified: %u"),
                                                           uOrigController);

                                    ComPtr<IVirtualBox> pVirtualBox = arg->virtualBox;
                                    ComPtr<ISystemProperties> systemProperties;
                                    CHECK_ERROR(pVirtualBox, COMGETTER(SystemProperties)(systemProperties.asOutParam()));
                                    ULONG maxPorts = 0;
                                    StorageBus_T enmStorageBus = StorageBus_Null;;
                                    switch (vsdControllerType)
                                    {
                                        case VirtualSystemDescriptionType_HardDiskControllerIDE:
                                            enmStorageBus = StorageBus_IDE;
                                           break;
                                        case VirtualSystemDescriptionType_HardDiskControllerSATA:
                                            enmStorageBus = StorageBus_SATA;
                                            break;
                                        case VirtualSystemDescriptionType_HardDiskControllerSCSI:
                                            enmStorageBus = StorageBus_SCSI;
                                            break;
                                        case VirtualSystemDescriptionType_HardDiskControllerSAS:
                                            enmStorageBus = StorageBus_SAS;
                                            break;
                                        case VirtualSystemDescriptionType_HardDiskControllerVirtioSCSI:
                                            enmStorageBus = StorageBus_VirtioSCSI;
                                            break;
                                        case VirtualSystemDescriptionType_HardDiskControllerNVMe:
                                            enmStorageBus = StorageBus_PCIe;
                                            break;
                                        default:  // Not reached since vsdControllerType validated above but silence gcc.
                                            break;
                                    }

                                    PlatformArchitecture_T platformArch = PlatformArchitecture_x86; /** @todo BUGBUG Appliances only handle x86 so far! */

                                    ComPtr<IPlatformProperties> pPlatformProperties;
                                    CHECK_ERROR_RET(pVirtualBox, GetPlatformProperties(platformArch, pPlatformProperties.asOutParam()),
                                                                                       RTEXITCODE_FAILURE);

                                    CHECK_ERROR_RET(pPlatformProperties, GetMaxPortCountForStorageBus(enmStorageBus, &maxPorts),
                                                    RTEXITCODE_FAILURE);
                                    if (uTargetControllerPort >= maxPorts)
                                        return errorSyntax(Appliance::tr("Illegal port value: %u. For %ls controllers the only valid values are 0 to %lu (inclusive)"),
                                                           uTargetControllerPort,
                                                           aVBoxValues[uTargetController],
                                                           maxPorts);

                                    /*
                                     * The 'strOverride' string will be mapped to the strExtraConfigCurrent value in
                                     * VirtualSystemDescription::setFinalValues() which is then used in the appliance
                                     * import routines i_importVBoxMachine()/i_importMachineGeneric() later.  This
                                     * aExtraConfigValues[] array entry must have a format of
                                     * 'controller=<index>;channel=<c>' as per the API documentation.
                                     */
                                    strExtraConfigValue = Utf8StrFmt("controller=%u;port=%u", uTargetController,
                                                                     uTargetControllerPort);
                                    strOverride = Utf8StrFmt("controller=%u;channel=%u", uTargetController,
                                                             uTargetControllerPort);
                                    Bstr bstrExtraConfigValue = strOverride;
                                    bstrExtraConfigValue.detachTo(&aExtraConfigValues[a]);
                                }

                                if (fDiskChanged && !fControllerChanged && !fControllerPortChanged)
                                {
                                    RTPrintf(Appliance::tr("%2u: Hard disk image specified with --disk: source image=%ls, target path=%ls, %s\n"
                                             "    (change controller with \"--vsys %u --unit %u --controller <index>\";\n"
                                             "    change controller port with \"--vsys %u --unit %u --port <n>\")\n"),
                                             a,
                                             aOvfValues[a],
                                             bstrFinalValue.raw(),
                                             strExtraConfigValue.c_str(),
                                             i, a,
                                             i, a);
                                }
                                else if (fDiskChanged && fControllerChanged && !fControllerPortChanged)
                                {
                                    RTPrintf(Appliance::tr("%2u: Hard disk image specified with --disk and --controller: source image=%ls, target path=%ls, %s\n"
                                             "    (change controller port with \"--vsys %u --unit %u --port <n>\")\n"),
                                             a,
                                             aOvfValues[a],
                                             bstrFinalValue.raw(),
                                             strExtraConfigValue.c_str(),
                                             i, a);
                                }
                                else if (fDiskChanged && !fControllerChanged && fControllerPortChanged)
                                {
                                    RTPrintf(Appliance::tr("%2u: Hard disk image specified with --disk and --port: source image=%ls, target path=%ls, %s\n"
                                             "    (change controller with \"--vsys %u --unit %u --controller <index>\")\n"),
                                             a,
                                             aOvfValues[a],
                                             bstrFinalValue.raw(),
                                             strExtraConfigValue.c_str(),
                                             i, a);
                                }
                                else if (!fDiskChanged && fControllerChanged && fControllerPortChanged)
                                {
                                    RTPrintf(Appliance::tr("%2u: Hard disk image specified with --controller and --port: source image=%ls, target path=%ls, %s\n"
                                             "    (change target path with \"--vsys %u --unit %u --disk path\")\n"),
                                             a,
                                             aOvfValues[a],
                                             bstrFinalValue.raw(),
                                             strExtraConfigValue.c_str(),
                                             i, a);
                                }
                                else if (!fDiskChanged && !fControllerChanged && fControllerPortChanged)
                                {
                                    RTPrintf(Appliance::tr("%2u: Hard disk image specified with --port: source image=%ls, target path=%ls, %s\n"
                                             "    (change target path with \"--vsys %u --unit %u --disk path\";\n"
                                             "    change controller with \"--vsys %u --unit %u --controller <index>\")\n"),
                                             a,
                                             aOvfValues[a],
                                             bstrFinalValue.raw(),
                                             strExtraConfigValue.c_str(),
                                             i, a,
                                             i, a);
                                }
                                else if (!fDiskChanged && fControllerChanged && !fControllerPortChanged)
                                {
                                    RTPrintf(Appliance::tr("%2u: Hard disk image specified with --controller: source image=%ls, target path=%ls, %s\n"
                                             "    (change target path with \"--vsys %u --unit %u --disk path\";\n"
                                             "    change controller port with \"--vsys %u --unit %u --port <n>\")\n"),
                                             a,
                                             aOvfValues[a],
                                             bstrFinalValue.raw(),
                                             strExtraConfigValue.c_str(),
                                             i, a,
                                             i, a);
                                }
                                else if (fDiskChanged && fControllerChanged && fControllerPortChanged)
                                {
                                    RTPrintf(Appliance::tr("%2u: Hard disk image specified with --disk and --controller and --port: source image=%ls, target path=%ls, %s\n"),
                                             a,
                                             aOvfValues[a],
                                             bstrFinalValue.raw(),
                                             strExtraConfigValue.c_str());
                                }
                                else
                                {
                                    strOverride = aVBoxValues[a];

                                    /*
                                     * Current solution isn't optimal.
                                     * Better way is to provide API call for function
                                     * Appliance::i_findMediumFormatFromDiskImage()
                                     * and creating one new function which returns
                                     * struct ovf::DiskImage for currently processed disk.
                                     */

                                    /*
                                     * if user wants to convert all imported disks to VDI format
                                     * we need to replace files extensions to "vdi"
                                     * except CD/DVD disks
                                     */
                                    if (optionsList.contains(ImportOptions_ImportToVDI))
                                    {
                                        ComPtr<IVirtualBox> pVirtualBox = arg->virtualBox;
                                        ComPtr<ISystemProperties> systemProperties;
                                        com::SafeIfaceArray<IMediumFormat> mediumFormats;
                                        Bstr bstrFormatName;

                                        CHECK_ERROR(pVirtualBox,
                                                     COMGETTER(SystemProperties)(systemProperties.asOutParam()));

                                        CHECK_ERROR(systemProperties,
                                             COMGETTER(MediumFormats)(ComSafeArrayAsOutParam(mediumFormats)));

                                        /* go through all supported media formats and store files extensions only for RAW */
                                        com::SafeArray<BSTR> extensions;

                                        for (unsigned j = 0; j < mediumFormats.size(); ++j)
                                        {
                                            com::SafeArray<DeviceType_T> deviceType;
                                            ComPtr<IMediumFormat> mediumFormat = mediumFormats[j];
                                            CHECK_ERROR(mediumFormat, COMGETTER(Name)(bstrFormatName.asOutParam()));
                                            Utf8Str strFormatName = Utf8Str(bstrFormatName);

                                            if (strFormatName.compare("RAW", Utf8Str::CaseInsensitive) == 0)
                                            {
                                                /* getting files extensions for "RAW" format */
                                                CHECK_ERROR(mediumFormat,
                                                            DescribeFileExtensions(ComSafeArrayAsOutParam(extensions),
                                                                                   ComSafeArrayAsOutParam(deviceType)));
                                                break;
                                            }
                                        }

                                        /* go through files extensions for RAW format and compare them with
                                         * extension of current file
                                         */
                                        bool fReplace = true;

                                        const char *pszExtension = RTPathSuffix(strOverride.c_str());
                                        if (pszExtension)
                                            pszExtension++;

                                        for (unsigned j = 0; j < extensions.size(); ++j)
                                        {
                                            Bstr bstrExt(extensions[j]);
                                            Utf8Str strExtension(bstrExt);
                                            if(strExtension.compare(pszExtension, Utf8Str::CaseInsensitive) == 0)
                                            {
                                                fReplace = false;
                                                break;
                                            }
                                        }

                                        if (fReplace)
                                        {
                                            strOverride = strOverride.stripSuffix();
                                            strOverride = strOverride.append(".").append("vdi");
                                        }
                                    }

                                    bstrFinalValue = strOverride;

                                    RTPrintf(Appliance::tr("%2u: Hard disk image: source image=%ls, target path=%ls, %s\n"
                                            "    (change target path with \"--vsys %u --unit %u --disk path\";\n"
                                            "    change controller with \"--vsys %u --unit %u --controller <index>\";\n"
                                            "    change controller port with \"--vsys %u --unit %u --port <n>\";\n"
                                            "    disable with \"--vsys %u --unit %u --ignore\")\n"),
                                            a, aOvfValues[a], bstrFinalValue.raw(), strExtraConfigValue.c_str(),
                                            i, a,
                                            i, a,
                                            i, a,
                                            i, a);
                                }
                            }
                            break;

                        case VirtualSystemDescriptionType_CDROM:
                            if (fIgnoreThis)
                            {
                                RTPrintf(Appliance::tr("%2u: CD-ROM -- disabled\n"),
                                         a);
                                aEnabled[a] = false;
                            }
                            else
                                RTPrintf(Appliance::tr("%2u: CD-ROM\n"
                                            "    (disable with \"--vsys %u --unit %u --ignore\")\n"),
                                        a, i, a);
                            break;

                        case VirtualSystemDescriptionType_Floppy:
                            if (fIgnoreThis)
                            {
                                RTPrintf(Appliance::tr("%2u: Floppy -- disabled\n"),
                                         a);
                                aEnabled[a] = false;
                            }
                            else
                                RTPrintf(Appliance::tr("%2u: Floppy\n"
                                            "    (disable with \"--vsys %u --unit %u --ignore\")\n"),
                                        a, i, a);
                            break;

                        case VirtualSystemDescriptionType_NetworkAdapter:
                            RTPrintf(Appliance::tr("%2u: Network adapter: orig %ls, config %ls, extra %ls\n"),   /// @todo implement once we have a plan for the back-end
                                     a,
                                     aOvfValues[a],
                                     aVBoxValues[a],
                                     aExtraConfigValues[a]);
                            break;

                        case VirtualSystemDescriptionType_USBController:
                            if (fIgnoreThis)
                            {
                                RTPrintf(Appliance::tr("%2u: USB controller -- disabled\n"),
                                         a);
                                aEnabled[a] = false;
                            }
                            else
                                RTPrintf(Appliance::tr("%2u: USB controller\n"
                                            "    (disable with \"--vsys %u --unit %u --ignore\")\n"),
                                        a, i, a);
                            break;

                        case VirtualSystemDescriptionType_SoundCard:
                            if (fIgnoreThis)
                            {
                               RTPrintf(Appliance::tr("%2u: Sound card \"%ls\" -- disabled\n"),
                                         a,
                                         aOvfValues[a]);
                                aEnabled[a] = false;
                            }
                            else
                                RTPrintf(Appliance::tr("%2u: Sound card (appliance expects \"%ls\", can change on import)\n"
                                            "    (disable with \"--vsys %u --unit %u --ignore\")\n"),
                                        a,
                                        aOvfValues[a],
                                        i,
                                        a);
                            break;

                        case VirtualSystemDescriptionType_SettingsFile:
                            if (findArgValue(strOverride, pmapArgs, "settingsfile"))
                            {
                                bstrFinalValue = strOverride;
                                RTPrintf(Appliance::tr("%2u: VM settings file name specified with --settingsfile: \"%ls\"\n"),
                                        a, bstrFinalValue.raw());
                            }
                            else
                                RTPrintf(Appliance::tr("%2u: Suggested VM settings file name \"%ls\"\n"
                                            "    (change with \"--vsys %u --settingsfile <filename>\")\n"),
                                        a, bstrFinalValue.raw(), i);
                            break;

                        case VirtualSystemDescriptionType_BaseFolder:
                            if (findArgValue(strOverride, pmapArgs, "basefolder"))
                            {
                                bstrFinalValue = strOverride;
                                RTPrintf(Appliance::tr("%2u: VM base folder specified with --basefolder: \"%ls\"\n"),
                                        a, bstrFinalValue.raw());
                            }
                            else
                                RTPrintf(Appliance::tr("%2u: Suggested VM base folder \"%ls\"\n"
                                            "    (change with \"--vsys %u --basefolder <path>\")\n"),
                                        a, bstrFinalValue.raw(), i);
                            break;

                        case VirtualSystemDescriptionType_PrimaryGroup:
                            if (findArgValue(strOverride, pmapArgs, "group"))
                            {
                                bstrFinalValue = strOverride;
                                RTPrintf(Appliance::tr("%2u: VM group specified with --group: \"%ls\"\n"),
                                        a, bstrFinalValue.raw());
                            }
                            else
                                RTPrintf(Appliance::tr("%2u: Suggested VM group \"%ls\"\n"
                                            "    (change with \"--vsys %u --group <group>\")\n"),
                                        a, bstrFinalValue.raw(), i);
                            break;

                        case VirtualSystemDescriptionType_CloudInstanceShape:
                            RTPrintf(Appliance::tr("%2u: Suggested cloud shape \"%ls\"\n"),
                                    a, bstrFinalValue.raw());
                            break;

                        case VirtualSystemDescriptionType_CloudBucket:
                            if (findArgValue(strOverride, pmapArgs, "cloudbucket"))
                            {
                                bstrFinalValue = strOverride;
                                RTPrintf(Appliance::tr("%2u: Cloud bucket id specified with --cloudbucket: \"%ls\"\n"),
                                        a, bstrFinalValue.raw());
                            }
                            else
                                RTPrintf(Appliance::tr("%2u: Suggested cloud bucket id \"%ls\"\n"
                                            "    (change with \"--cloud %u --cloudbucket <id>\")\n"),
                                        a, bstrFinalValue.raw(), i);
                            break;

                        case VirtualSystemDescriptionType_CloudProfileName:
                            if (findArgValue(strOverride, pmapArgs, "cloudprofile"))
                            {
                                bstrFinalValue = strOverride;
                                RTPrintf(Appliance::tr("%2u: Cloud profile name specified with --cloudprofile: \"%ls\"\n"),
                                        a, bstrFinalValue.raw());
                            }
                            else
                                RTPrintf(Appliance::tr("%2u: Suggested cloud profile name \"%ls\"\n"
                                            "    (change with \"--cloud %u --cloudprofile <id>\")\n"),
                                        a, bstrFinalValue.raw(), i);
                            break;

                        case VirtualSystemDescriptionType_CloudInstanceId:
                            if (findArgValue(strOverride, pmapArgs, "cloudinstanceid"))
                            {
                                bstrFinalValue = strOverride;
                                RTPrintf(Appliance::tr("%2u: Cloud instance id specified with --cloudinstanceid: \"%ls\"\n"),
                                        a, bstrFinalValue.raw());
                            }
                            else
                                RTPrintf(Appliance::tr("%2u: Suggested cloud instance id \"%ls\"\n"
                                            "    (change with \"--cloud %u --cloudinstanceid <id>\")\n"),
                                        a, bstrFinalValue.raw(), i);
                            break;

                        case VirtualSystemDescriptionType_CloudImageId:
                            RTPrintf(Appliance::tr("%2u: Suggested cloud base image id \"%ls\"\n"),
                                    a, bstrFinalValue.raw());
                            break;
                        case VirtualSystemDescriptionType_CloudDomain:
                        case VirtualSystemDescriptionType_CloudBootDiskSize:
                        case VirtualSystemDescriptionType_CloudOCIVCN:
                        case VirtualSystemDescriptionType_CloudPublicIP:
                        case VirtualSystemDescriptionType_CloudOCISubnet:
                        case VirtualSystemDescriptionType_CloudKeepObject:
                        case VirtualSystemDescriptionType_CloudLaunchInstance:
                        case VirtualSystemDescriptionType_CloudInstanceState:
                        case VirtualSystemDescriptionType_CloudImageState:
                        case VirtualSystemDescriptionType_Miscellaneous:
                        case VirtualSystemDescriptionType_CloudInstanceDisplayName:
                        case VirtualSystemDescriptionType_CloudImageDisplayName:
                        case VirtualSystemDescriptionType_CloudOCILaunchMode:
                        case VirtualSystemDescriptionType_CloudPrivateIP:
                        case VirtualSystemDescriptionType_CloudBootVolumeId:
                        case VirtualSystemDescriptionType_CloudOCIVCNCompartment:
                        case VirtualSystemDescriptionType_CloudOCISubnetCompartment:
                        case VirtualSystemDescriptionType_CloudPublicSSHKey:
                        case VirtualSystemDescriptionType_BootingFirmware:
                        case VirtualSystemDescriptionType_CloudInitScriptPath:
                        case VirtualSystemDescriptionType_CloudCompartmentId:
                        case VirtualSystemDescriptionType_CloudShapeCpus:
                        case VirtualSystemDescriptionType_CloudShapeMemory:
                        case VirtualSystemDescriptionType_CloudInstanceMetadata:
                        case VirtualSystemDescriptionType_CloudInstanceFreeFormTags:
                        case VirtualSystemDescriptionType_CloudImageFreeFormTags:
                        case VirtualSystemDescriptionType_NVRAM:
                            /** @todo  VirtualSystemDescriptionType_Miscellaneous? */
                            break;

                        case VirtualSystemDescriptionType_Ignore:
#ifdef VBOX_WITH_XPCOM_CPP_ENUM_HACK
                        case VirtualSystemDescriptionType_32BitHack:
#endif
                            break;
                    }

                    bstrFinalValue.detachTo(&aFinalValues[a]);
                }

                if (fExecute)
                    CHECK_ERROR_BREAK(aVirtualSystemDescriptions[i],
                                      SetFinalValues(ComSafeArrayAsInParam(aEnabled),
                                                     ComSafeArrayAsInParam(aFinalValues),
                                                     ComSafeArrayAsInParam(aExtraConfigValues)));

            } // for (unsigned i = 0; i < cVirtualSystemDescriptions; ++i)

            if (cLicensesInTheWay == 1)
                RTMsgError(Appliance::tr("Cannot import until the license agreement listed above is accepted."));
            else if (cLicensesInTheWay > 1)
                RTMsgError(Appliance::tr("Cannot import until the %c license agreements listed above are accepted."),
                           cLicensesInTheWay);

            if (!cLicensesInTheWay && fExecute)
            {
                // go!
                ComPtr<IProgress> progress;
                CHECK_ERROR_BREAK(pAppliance,
                                  ImportMachines(ComSafeArrayAsInParam(options), progress.asOutParam()));

                hrc = showProgress(progress);
                CHECK_PROGRESS_ERROR_RET(progress, (Appliance::tr("Appliance import failed")), RTEXITCODE_FAILURE);

                if (SUCCEEDED(hrc))
                    RTPrintf(Appliance::tr("Successfully imported the appliance.\n"));
            }
        } // end if (aVirtualSystemDescriptions.size() > 0)
    } while (0);

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static int parseExportOptions(const char *psz, com::SafeArray<ExportOptions_T> *options)
{
    int vrc = VINF_SUCCESS;
    while (psz && *psz && RT_SUCCESS(vrc))
    {
        size_t len;
        const char *pszComma = strchr(psz, ',');
        if (pszComma)
            len = (size_t)(pszComma - psz);
        else
            len = strlen(psz);
        if (len > 0)
        {
            if (!RTStrNICmp(psz, "CreateManifest", len))
                options->push_back(ExportOptions_CreateManifest);
            else if (!RTStrNICmp(psz, "manifest", len))
                options->push_back(ExportOptions_CreateManifest);
            else if (!RTStrNICmp(psz, "ExportDVDImages", len))
                options->push_back(ExportOptions_ExportDVDImages);
            else if (!RTStrNICmp(psz, "iso", len))
                options->push_back(ExportOptions_ExportDVDImages);
            else if (!RTStrNICmp(psz, "StripAllMACs", len))
                options->push_back(ExportOptions_StripAllMACs);
            else if (!RTStrNICmp(psz, "nomacs", len))
                options->push_back(ExportOptions_StripAllMACs);
            else if (!RTStrNICmp(psz, "StripAllNonNATMACs", len))
                options->push_back(ExportOptions_StripAllNonNATMACs);
            else if (!RTStrNICmp(psz, "nomacsbutnat", len))
                options->push_back(ExportOptions_StripAllNonNATMACs);
            else
                vrc = VERR_PARSE_ERROR;
        }
        if (pszComma)
            psz += len + 1;
        else
            psz += len;
    }

    return vrc;
}

static const RTGETOPTDEF g_aExportOptions[] =
{
    { "--output",               'o', RTGETOPT_REQ_STRING },
    { "--legacy09",             'l', RTGETOPT_REQ_NOTHING },
    { "--ovf09",                'l', RTGETOPT_REQ_NOTHING },
    { "--ovf10",                '1', RTGETOPT_REQ_NOTHING },
    { "--ovf20",                '2', RTGETOPT_REQ_NOTHING },
    { "--opc10",                'c', RTGETOPT_REQ_NOTHING },
    { "--manifest",             'm', RTGETOPT_REQ_NOTHING },    // obsoleted by --options
    { "--vsys",                 's', RTGETOPT_REQ_UINT32 },
    { "--vmname",               'V', RTGETOPT_REQ_STRING },
    { "--product",              'p', RTGETOPT_REQ_STRING },
    { "--producturl",           'P', RTGETOPT_REQ_STRING },
    { "--vendor",               'n', RTGETOPT_REQ_STRING },
    { "--vendorurl",            'N', RTGETOPT_REQ_STRING },
    { "--version",              'v', RTGETOPT_REQ_STRING },
    { "--description",          'd', RTGETOPT_REQ_STRING },
    { "--eula",                 'e', RTGETOPT_REQ_STRING },
    { "--eulafile",             'E', RTGETOPT_REQ_STRING },
    { "--options",              'O', RTGETOPT_REQ_STRING },
    { "--cloud",                'C', RTGETOPT_REQ_UINT32 },
    { "--cloudshape",           'S', RTGETOPT_REQ_STRING },
    { "--clouddomain",          'D', RTGETOPT_REQ_STRING },
    { "--clouddisksize",        'R', RTGETOPT_REQ_STRING },
    { "--cloudbucket",          'B', RTGETOPT_REQ_STRING },
    { "--cloudocivcn",          'Q', RTGETOPT_REQ_STRING },
    { "--cloudpublicip",        'A', RTGETOPT_REQ_STRING },
    { "--cloudprofile",         'F', RTGETOPT_REQ_STRING },
    { "--cloudocisubnet",       'T', RTGETOPT_REQ_STRING },
    { "--cloudkeepobject",      'K', RTGETOPT_REQ_STRING },
    { "--cloudlaunchinstance",  'L', RTGETOPT_REQ_STRING },
    { "--cloudlaunchmode",      'M', RTGETOPT_REQ_STRING },
    { "--cloudprivateip",       'i', RTGETOPT_REQ_STRING },
    { "--cloudinitscriptpath",  'I', RTGETOPT_REQ_STRING },
};

RTEXITCODE handleExportAppliance(HandlerArg *a)
{
    HRESULT hrc = S_OK;

    Utf8Str strOutputFile;
    Utf8Str strOvfFormat("ovf-1.0"); // the default export version
    bool fManifest = false; // the default
    APPLIANCETYPE enmApplType = NOT_SET;
    bool fExportISOImages = false; // the default
    com::SafeArray<ExportOptions_T> options;
    std::list< ComPtr<IMachine> > llMachines;

    uint32_t ulCurVsys = (uint32_t)-1;
    // for each --vsys X command, maintain a map of command line items
    ArgsMapsMap mapArgsMapsPerVsys;
    do
    {
        int c;

        RTGETOPTUNION ValueUnion;
        RTGETOPTSTATE GetState;
        // start at 0 because main() has hacked both the argc and argv given to us
        RTGetOptInit(&GetState, a->argc, a->argv, g_aExportOptions,
                     RT_ELEMENTS(g_aExportOptions), 0, RTGETOPTINIT_FLAGS_NO_STD_OPTS);

        Utf8Str strProductUrl;
        while ((c = RTGetOpt(&GetState, &ValueUnion)))
        {
            switch (c)
            {
                case 'o':   // --output
                    if (strOutputFile.length())
                        return errorSyntax(Appliance::tr("You can only specify --output once."));
                    else
                        strOutputFile = ValueUnion.psz;
                    break;

                case 'l':   // --legacy09/--ovf09
                    strOvfFormat = "ovf-0.9";
                    break;

                case '1':   // --ovf10
                    strOvfFormat = "ovf-1.0";
                    break;

                case '2':   // --ovf20
                    strOvfFormat = "ovf-2.0";
                    break;

                case 'c':   // --opc
                    strOvfFormat = "opc-1.0";
                    break;

//              case 'I':   // --iso
//                  fExportISOImages = true;
//                  break;

                case 'm':   // --manifest
                    fManifest = true;
                    break;

                case 's':   // --vsys
                    if (enmApplType == NOT_SET)
                        enmApplType = LOCAL;

                    if (enmApplType != LOCAL)
                        return errorSyntax(Appliance::tr("Option \"%s\" can't be used together with \"--cloud\" option."),
                                           GetState.pDef->pszLong);
                    if (ValueUnion.u32 == (uint32_t)-1)
                        return errorSyntax(Appliance::tr("Value of option \"%s\" is out of range."),
                                           GetState.pDef->pszLong);

                    ulCurVsys = ValueUnion.u32;
                    break;

                case 'V':   // --vmname
                    if (enmApplType == NOT_SET)
                        return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --vsys or --cloud option."),
                                           GetState.pDef->pszLong);
                    mapArgsMapsPerVsys[ulCurVsys]["vmname"] = ValueUnion.psz;
                    break;

                case 'p':   // --product
                    if (enmApplType != LOCAL)
                        return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --vsys option."),
                                           GetState.pDef->pszLong);
                    mapArgsMapsPerVsys[ulCurVsys]["product"] = ValueUnion.psz;
                    break;

                case 'P':   // --producturl
                    if (enmApplType != LOCAL)
                        return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --vsys option."),
                                           GetState.pDef->pszLong);
                    mapArgsMapsPerVsys[ulCurVsys]["producturl"] = ValueUnion.psz;
                    break;

                case 'n':   // --vendor
                    if (enmApplType != LOCAL)
                        return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --vsys option."),
                                           GetState.pDef->pszLong);
                    mapArgsMapsPerVsys[ulCurVsys]["vendor"] = ValueUnion.psz;
                    break;

                case 'N':   // --vendorurl
                    if (enmApplType != LOCAL)
                        return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --vsys option."),
                                           GetState.pDef->pszLong);
                    mapArgsMapsPerVsys[ulCurVsys]["vendorurl"] = ValueUnion.psz;
                    break;

                case 'v':   // --version
                    if (enmApplType != LOCAL)
                        return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --vsys option."),
                                           GetState.pDef->pszLong);
                    mapArgsMapsPerVsys[ulCurVsys]["version"] = ValueUnion.psz;
                    break;

                case 'd':   // --description
                    if (enmApplType != LOCAL)
                        return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --vsys option."),
                                           GetState.pDef->pszLong);
                    mapArgsMapsPerVsys[ulCurVsys]["description"] = ValueUnion.psz;
                    break;

                case 'e':   // --eula
                    if (enmApplType != LOCAL)
                        return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --vsys option."),
                                           GetState.pDef->pszLong);
                    mapArgsMapsPerVsys[ulCurVsys]["eula"] = ValueUnion.psz;
                    break;

                case 'E':   // --eulafile
                    if (enmApplType != LOCAL)
                        return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --vsys option."),
                                           GetState.pDef->pszLong);
                    mapArgsMapsPerVsys[ulCurVsys]["eulafile"] = ValueUnion.psz;
                    break;

                case 'O':   // --options
                    if (RT_FAILURE(parseExportOptions(ValueUnion.psz, &options)))
                        return errorArgument(Appliance::tr("Invalid export options '%s'\n"), ValueUnion.psz);
                    break;

                    /*--cloud and --vsys are orthogonal, only one must be presented*/
                case 'C':   // --cloud
                    if (enmApplType == NOT_SET)
                        enmApplType = CLOUD;

                    if (enmApplType != CLOUD)
                        return errorSyntax(Appliance::tr("Option \"%s\" can't be used together with \"--vsys\" option."),
                                           GetState.pDef->pszLong);
                    if (ValueUnion.u32 == (uint32_t)-1)
                        return errorSyntax(Appliance::tr("Value of option \"%s\" is out of range."),
                                           GetState.pDef->pszLong);

                    ulCurVsys = ValueUnion.u32;
                    break;

                    /* Cloud export settings */
                case 'S':   // --cloudshape
                    if (enmApplType != CLOUD)
                        return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --cloud option."),
                                           GetState.pDef->pszLong);
                    mapArgsMapsPerVsys[ulCurVsys]["cloudshape"] = ValueUnion.psz;
                    break;

                case 'D':   // --clouddomain
                    if (enmApplType != CLOUD)
                        return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --cloud option."),
                                           GetState.pDef->pszLong);
                    mapArgsMapsPerVsys[ulCurVsys]["clouddomain"] = ValueUnion.psz;
                    break;

                case 'R':   // --clouddisksize
                    if (enmApplType != CLOUD)
                        return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --cloud option."),
                                           GetState.pDef->pszLong);
                    mapArgsMapsPerVsys[ulCurVsys]["clouddisksize"] = ValueUnion.psz;
                    break;

                case 'B':   // --cloudbucket
                    if (enmApplType != CLOUD)
                        return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --cloud option."),
                                           GetState.pDef->pszLong);
                    mapArgsMapsPerVsys[ulCurVsys]["cloudbucket"] = ValueUnion.psz;
                    break;

                case 'Q':   // --cloudocivcn
                    if (enmApplType != CLOUD)
                        return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --cloud option."),
                                           GetState.pDef->pszLong);
                    mapArgsMapsPerVsys[ulCurVsys]["cloudocivcn"] = ValueUnion.psz;
                    break;

                case 'A':   // --cloudpublicip
                    if (enmApplType != CLOUD)
                        return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --cloud option."),
                                           GetState.pDef->pszLong);
                    mapArgsMapsPerVsys[ulCurVsys]["cloudpublicip"] = ValueUnion.psz;
                    break;

                case 'i': /* --cloudprivateip */
                    if (enmApplType != CLOUD)
                        return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --cloud option."),
                                           GetState.pDef->pszLong);
                    mapArgsMapsPerVsys[ulCurVsys]["cloudprivateip"] = ValueUnion.psz;
                    break;

                case 'F':   // --cloudprofile
                    if (enmApplType != CLOUD)
                        return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --cloud option."),
                                           GetState.pDef->pszLong);
                    mapArgsMapsPerVsys[ulCurVsys]["cloudprofile"] = ValueUnion.psz;
                    break;

                case 'T':   // --cloudocisubnet
                    if (enmApplType != CLOUD)
                        return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --cloud option."),
                                           GetState.pDef->pszLong);
                    mapArgsMapsPerVsys[ulCurVsys]["cloudocisubnet"] = ValueUnion.psz;
                    break;

                case 'K':   // --cloudkeepobject
                    if (enmApplType != CLOUD)
                        return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --cloud option."),
                                           GetState.pDef->pszLong);
                    mapArgsMapsPerVsys[ulCurVsys]["cloudkeepobject"] = ValueUnion.psz;
                    break;

                case 'L':   // --cloudlaunchinstance
                    if (enmApplType != CLOUD)
                        return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --cloud option."),
                                           GetState.pDef->pszLong);
                    mapArgsMapsPerVsys[ulCurVsys]["cloudlaunchinstance"] = ValueUnion.psz;
                    break;

                case 'M': /* --cloudlaunchmode */
                    if (enmApplType != CLOUD)
                        return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --cloud option."),
                                           GetState.pDef->pszLong);
                    mapArgsMapsPerVsys[ulCurVsys]["cloudlaunchmode"] = ValueUnion.psz;
                    break;

                case 'I':   // --cloudinitscriptpath
                    if (enmApplType != CLOUD)
                        return errorSyntax(Appliance::tr("Option \"%s\" requires preceding --cloud option."),
                                           GetState.pDef->pszLong);
                    mapArgsMapsPerVsys[ulCurVsys]["cloudinitscriptpath"] = ValueUnion.psz;
                    break;

                case VINF_GETOPT_NOT_OPTION:
                {
                    Utf8Str strMachine(ValueUnion.psz);
                    // must be machine: try UUID or name
                    ComPtr<IMachine> machine;
                    CHECK_ERROR_BREAK(a->virtualBox, FindMachine(Bstr(strMachine).raw(),
                                                                 machine.asOutParam()));
                    if (machine)
                        llMachines.push_back(machine);
                    break;
                }

                default:
                    if (c > 0)
                    {
                        if (RT_C_IS_GRAPH(c))
                            return errorSyntax(Appliance::tr("unhandled option: -%c"), c);
                        else
                            return errorSyntax(Appliance::tr("unhandled option: %i"), c);
                    }
                    else if (c == VERR_GETOPT_UNKNOWN_OPTION)
                        return errorSyntax(Appliance::tr("unknown option: %s"), ValueUnion.psz);
                    else if (ValueUnion.pDef)
                        return errorSyntax("%s: %Rrs", ValueUnion.pDef->pszLong, c);
                    else
                        return errorSyntax("%Rrs", c);
            }

            if (FAILED(hrc))
                break;
        }

        if (FAILED(hrc))
            break;

        if (llMachines.empty())
            return errorSyntax(Appliance::tr("At least one machine must be specified with the export command."));

        /* Last check after parsing all arguments */
        if (strOutputFile.isEmpty())
            return errorSyntax(Appliance::tr("Missing --output argument with export command."));

        if (enmApplType == NOT_SET)
            enmApplType = LOCAL;

        // match command line arguments with the machines count
        // this is only to sort out invalid indices at this time
        ArgsMapsMap::const_iterator it;
        for (it = mapArgsMapsPerVsys.begin();
             it != mapArgsMapsPerVsys.end();
             ++it)
        {
            uint32_t ulVsys = it->first;
            if (ulVsys >= llMachines.size())
                return errorSyntax(Appliance::tr("Invalid index %RI32 with -vsys option; you specified only %zu virtual system(s).",
                                                 "", llMachines.size()),
                                   ulVsys, llMachines.size());
        }

        ComPtr<IAppliance> pAppliance;
        CHECK_ERROR_BREAK(a->virtualBox, CreateAppliance(pAppliance.asOutParam()));

        char *pszAbsFilePath = 0;
        if (strOutputFile.startsWith("S3://", RTCString::CaseInsensitive) ||
            strOutputFile.startsWith("SunCloud://", RTCString::CaseInsensitive) ||
            strOutputFile.startsWith("webdav://", RTCString::CaseInsensitive) ||
            strOutputFile.startsWith("OCI://", RTCString::CaseInsensitive))
            pszAbsFilePath = RTStrDup(strOutputFile.c_str());
        else
            pszAbsFilePath = RTPathAbsDup(strOutputFile.c_str());

        /*
         * The first stage - export machine/s to the Cloud or into the
         * OVA/OVF format on the local host.
         */

        /* VSDList is needed for the second stage where we launch the cloud instances if it was requested by user */
        std::list< ComPtr<IVirtualSystemDescription> > VSDList;
        std::list< ComPtr<IMachine> >::iterator itM;
        uint32_t i=0;
        for (itM = llMachines.begin();
             itM != llMachines.end();
             ++itM, ++i)
        {
            ComPtr<IMachine> pMachine = *itM;
            ComPtr<IVirtualSystemDescription> pVSD;
            CHECK_ERROR_BREAK(pMachine, ExportTo(pAppliance, Bstr(pszAbsFilePath).raw(), pVSD.asOutParam()));

            // Add additional info to the virtual system description if the user wants so
            ArgsMap *pmapArgs = NULL;
            ArgsMapsMap::iterator itm = mapArgsMapsPerVsys.find(i);
            if (itm != mapArgsMapsPerVsys.end())
                pmapArgs = &itm->second;
            if (pmapArgs)
            {
                ArgsMap::iterator itD;
                for (itD = pmapArgs->begin();
                     itD != pmapArgs->end();
                     ++itD)
                {
                    if (itD->first == "vmname")
                    {
                        //remove default value if user has specified new name (default value is set in the ExportTo())
//                      pVSD->RemoveDescriptionByType(VirtualSystemDescriptionType_Name);
                        pVSD->AddDescription(VirtualSystemDescriptionType_Name,
                                             Bstr(itD->second).raw(), NULL);
                    }
                    else if (itD->first == "product")
                        pVSD->AddDescription(VirtualSystemDescriptionType_Product,
                                             Bstr(itD->second).raw(), NULL);
                    else if (itD->first == "producturl")
                        pVSD->AddDescription(VirtualSystemDescriptionType_ProductUrl,
                                             Bstr(itD->second).raw(), NULL);
                    else if (itD->first == "vendor")
                        pVSD->AddDescription(VirtualSystemDescriptionType_Vendor,
                                             Bstr(itD->second).raw(), NULL);
                    else if (itD->first == "vendorurl")
                        pVSD->AddDescription(VirtualSystemDescriptionType_VendorUrl,
                                             Bstr(itD->second).raw(), NULL);
                    else if (itD->first == "version")
                        pVSD->AddDescription(VirtualSystemDescriptionType_Version,
                                             Bstr(itD->second).raw(), NULL);
                    else if (itD->first == "description")
                        pVSD->AddDescription(VirtualSystemDescriptionType_Description,
                                             Bstr(itD->second).raw(), NULL);
                    else if (itD->first == "eula")
                        pVSD->AddDescription(VirtualSystemDescriptionType_License,
                                             Bstr(itD->second).raw(), NULL);
                    else if (itD->first == "eulafile")
                    {
                        Utf8Str strContent;
                        void *pvFile;
                        size_t cbFile;
                        int irc = RTFileReadAll(itD->second.c_str(), &pvFile, &cbFile);
                        if (RT_SUCCESS(irc))
                        {
                            Bstr bstrContent((char*)pvFile, cbFile);
                            pVSD->AddDescription(VirtualSystemDescriptionType_License,
                                                 bstrContent.raw(), NULL);
                            RTFileReadAllFree(pvFile, cbFile);
                        }
                        else
                        {
                            RTMsgError(Appliance::tr("Cannot read license file \"%s\" which should be included in the virtual system %u."),
                                       itD->second.c_str(), i);
                            return RTEXITCODE_FAILURE;
                        }
                    }
                    /* add cloud export settings */
                    else if (itD->first == "cloudshape")
                        pVSD->AddDescription(VirtualSystemDescriptionType_CloudInstanceShape,
                                             Bstr(itD->second).raw(), NULL);
                    else if (itD->first == "clouddomain")
                        pVSD->AddDescription(VirtualSystemDescriptionType_CloudDomain,
                                             Bstr(itD->second).raw(), NULL);
                    else if (itD->first == "clouddisksize")
                        pVSD->AddDescription(VirtualSystemDescriptionType_CloudBootDiskSize,
                                             Bstr(itD->second).raw(), NULL);
                    else if (itD->first == "cloudbucket")
                        pVSD->AddDescription(VirtualSystemDescriptionType_CloudBucket,
                                             Bstr(itD->second).raw(), NULL);
                    else if (itD->first == "cloudocivcn")
                        pVSD->AddDescription(VirtualSystemDescriptionType_CloudOCIVCN,
                                             Bstr(itD->second).raw(), NULL);
                    else if (itD->first == "cloudpublicip")
                        pVSD->AddDescription(VirtualSystemDescriptionType_CloudPublicIP,
                                             Bstr(itD->second).raw(), NULL);
                    else if (itD->first == "cloudprivateip")
                        pVSD->AddDescription(VirtualSystemDescriptionType_CloudPrivateIP,
                                             Bstr(itD->second).raw(), NULL);
                    else if (itD->first == "cloudprofile")
                        pVSD->AddDescription(VirtualSystemDescriptionType_CloudProfileName,
                                             Bstr(itD->second).raw(), NULL);
                    else if (itD->first == "cloudocisubnet")
                        pVSD->AddDescription(VirtualSystemDescriptionType_CloudOCISubnet,
                                             Bstr(itD->second).raw(), NULL);
                    else if (itD->first == "cloudkeepobject")
                        pVSD->AddDescription(VirtualSystemDescriptionType_CloudKeepObject,
                                             Bstr(itD->second).raw(), NULL);
                    else if (itD->first == "cloudlaunchmode")
                        pVSD->AddDescription(VirtualSystemDescriptionType_CloudOCILaunchMode,
                                             Bstr(itD->second).raw(), NULL);
                    else if (itD->first == "cloudlaunchinstance")
                        pVSD->AddDescription(VirtualSystemDescriptionType_CloudLaunchInstance,
                                             Bstr(itD->second).raw(), NULL);
                    else if (itD->first == "cloudinitscriptpath")
                        pVSD->AddDescription(VirtualSystemDescriptionType_CloudInitScriptPath,
                                             Bstr(itD->second).raw(), NULL);

                }
            }

            VSDList.push_back(pVSD);//store vsd for the possible second stage
        }

        if (FAILED(hrc))
            break;

        /* Query required passwords and supply them to the appliance. */
        com::SafeArray<BSTR> aIdentifiers;

        CHECK_ERROR_BREAK(pAppliance, GetPasswordIds(ComSafeArrayAsOutParam(aIdentifiers)));

        if (aIdentifiers.size() > 0)
        {
            com::SafeArray<BSTR> aPasswords(aIdentifiers.size());
            RTPrintf(Appliance::tr("Enter the passwords for the following identifiers to export the apppliance:\n"));
            for (unsigned idxId = 0; idxId < aIdentifiers.size(); idxId++)
            {
                com::Utf8Str strPassword;
                Bstr bstrPassword;
                Bstr bstrId = aIdentifiers[idxId];

                RTEXITCODE rcExit = readPasswordFromConsole(&strPassword, Appliance::tr("Password ID %s:"),
                                                            Utf8Str(bstrId).c_str());
                if (rcExit == RTEXITCODE_FAILURE)
                {
                    RTStrFree(pszAbsFilePath);
                    return rcExit;
                }

                bstrPassword = strPassword;
                bstrPassword.detachTo(&aPasswords[idxId]);
            }

            CHECK_ERROR_BREAK(pAppliance, AddPasswords(ComSafeArrayAsInParam(aIdentifiers),
                                                       ComSafeArrayAsInParam(aPasswords)));
        }

        if (fManifest)
            options.push_back(ExportOptions_CreateManifest);

        if (fExportISOImages)
            options.push_back(ExportOptions_ExportDVDImages);

        ComPtr<IProgress> progress;
        CHECK_ERROR_BREAK(pAppliance, Write(Bstr(strOvfFormat).raw(),
                                            ComSafeArrayAsInParam(options),
                                            Bstr(pszAbsFilePath).raw(),
                                            progress.asOutParam()));
        RTStrFree(pszAbsFilePath);

        hrc = showProgress(progress);
        CHECK_PROGRESS_ERROR_RET(progress, (Appliance::tr("Appliance write failed")), RTEXITCODE_FAILURE);

        if (SUCCEEDED(hrc))
            RTPrintf(Appliance::tr("Successfully exported %d machine(s).\n", "", llMachines.size()), llMachines.size());

        /*
         *  The second stage for the cloud case
         */
        if (enmApplType == CLOUD)
        {
            /* Launch the exported VM if the appropriate flag had been set on the first stage */
            for (std::list< ComPtr<IVirtualSystemDescription> >::iterator itVSD = VSDList.begin();
                 itVSD != VSDList.end();
                 ++itVSD)
            {
                ComPtr<IVirtualSystemDescription> pVSD = *itVSD;

                com::SafeArray<VirtualSystemDescriptionType_T> retTypes;
                com::SafeArray<BSTR> aRefs;
                com::SafeArray<BSTR> aOvfValues;
                com::SafeArray<BSTR> aVBoxValues;
                com::SafeArray<BSTR> aExtraConfigValues;

                CHECK_ERROR_BREAK(pVSD, GetDescriptionByType(VirtualSystemDescriptionType_CloudLaunchInstance,
                                         ComSafeArrayAsOutParam(retTypes),
                                         ComSafeArrayAsOutParam(aRefs),
                                         ComSafeArrayAsOutParam(aOvfValues),
                                         ComSafeArrayAsOutParam(aVBoxValues),
                                         ComSafeArrayAsOutParam(aExtraConfigValues)));

                BSTR const pValue0 = aVBoxValues[0]; /* gcc 13.3.0 gets confused by Bstr(aVBoxValues[0]). */
                Utf8Str flagCloudLaunchInstance(Bstr(pValue0).raw());
                retTypes.setNull(); aRefs.setNull(); aOvfValues.setNull(); aVBoxValues.setNull(); aExtraConfigValues.setNull();

                if (flagCloudLaunchInstance.equals("true"))
                {
                    /* Getting the short provider name */
                    Bstr bstrCloudProviderShortName(strOutputFile.c_str(), strOutputFile.find("://"));

                    ComPtr<IVirtualBox> pVirtualBox = a->virtualBox;
                    ComPtr<ICloudProviderManager> pCloudProviderManager;
                    CHECK_ERROR_BREAK(pVirtualBox, COMGETTER(CloudProviderManager)(pCloudProviderManager.asOutParam()));

                    ComPtr<ICloudProvider> pCloudProvider;
                    CHECK_ERROR_BREAK(pCloudProviderManager,
                                     GetProviderByShortName(bstrCloudProviderShortName.raw(), pCloudProvider.asOutParam()));

                    CHECK_ERROR_BREAK(pVSD, GetDescriptionByType(VirtualSystemDescriptionType_CloudProfileName,
                                             ComSafeArrayAsOutParam(retTypes),
                                             ComSafeArrayAsOutParam(aRefs),
                                             ComSafeArrayAsOutParam(aOvfValues),
                                             ComSafeArrayAsOutParam(aVBoxValues),
                                             ComSafeArrayAsOutParam(aExtraConfigValues)));

                    ComPtr<ICloudProfile> pCloudProfile;
                    CHECK_ERROR_BREAK(pCloudProvider, GetProfileByName(Bstr(aVBoxValues[0]).raw(), pCloudProfile.asOutParam()));
                    retTypes.setNull(); aRefs.setNull(); aOvfValues.setNull(); aVBoxValues.setNull(); aExtraConfigValues.setNull();

                    ComObjPtr<ICloudClient> oCloudClient;
                    CHECK_ERROR_BREAK(pCloudProfile, CreateCloudClient(oCloudClient.asOutParam()));
                    RTPrintf(Appliance::tr("Creating a cloud instance...\n"));

                    ComPtr<IProgress> progress1;
                    CHECK_ERROR_BREAK(oCloudClient, LaunchVM(pVSD, progress1.asOutParam()));
                    hrc = showProgress(progress1);
                    CHECK_PROGRESS_ERROR_RET(progress1, (Appliance::tr("Creating the cloud instance failed")),
                                             RTEXITCODE_FAILURE);

                    if (SUCCEEDED(hrc))
                    {
                        CHECK_ERROR_BREAK(pVSD, GetDescriptionByType(VirtualSystemDescriptionType_CloudInstanceId,
                                                 ComSafeArrayAsOutParam(retTypes),
                                                 ComSafeArrayAsOutParam(aRefs),
                                                 ComSafeArrayAsOutParam(aOvfValues),
                                                 ComSafeArrayAsOutParam(aVBoxValues),
                                                 ComSafeArrayAsOutParam(aExtraConfigValues)));

                        RTPrintf(Appliance::tr("A cloud instance with id '%s' (provider '%s') was created\n"),
                                 Utf8Str(Bstr(aVBoxValues[0]).raw()).c_str(),
                                 Utf8Str(bstrCloudProviderShortName.raw()).c_str());
                        retTypes.setNull(); aRefs.setNull(); aOvfValues.setNull(); aVBoxValues.setNull(); aExtraConfigValues.setNull();
                    }
                }
            }
        }
    } while (0);

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/*********************************************************************************************************************************
*   signova                                                                                                                      *
*********************************************************************************************************************************/

/**
 * Reads the OVA and saves the manifest and signed status.
 *
 * @returns VBox status code (fully messaged).
 * @param   pszOva              The name of the OVA.
 * @param   iVerbosity          The noise level.
 * @param   fReSign             Whether it is acceptable to have an existing signature
 *                              in the OVA or not.
 * @param   phVfsFssOva         Where to return the OVA file system stream handle.
 *                              This has been opened for updating and we're positioned
 *                              at the end of the stream.
 * @param   pStrManifestName    Where to return the manifest name.
 * @param   phVfsManifest       Where to return the manifest file handle (copy in mem).
 * @param   phVfsOldSignature   Where to return the handle to the old signature object.
 *
 * @note    Caller must clean up return values on failure too!
 */
static int openOvaAndGetManifestAndOldSignature(const char *pszOva, unsigned iVerbosity, bool fReSign,
                                                PRTVFSFSSTREAM phVfsFssOva, Utf8Str *pStrManifestName,
                                                PRTVFSFILE phVfsManifest, PRTVFSOBJ phVfsOldSignature)
{
    /*
     * Clear return values.
     */
    *phVfsFssOva       = NIL_RTVFSFSSTREAM;
    pStrManifestName->setNull();
    *phVfsManifest     = NIL_RTVFSFILE;
    *phVfsOldSignature = NIL_RTVFSOBJ;

    /*
     * Open the file as a tar file system stream.
     */
    RTVFSFILE hVfsFileOva;
    int vrc = RTVfsFileOpenNormal(pszOva, RTFILE_O_OPEN | RTFILE_O_READWRITE | RTFILE_O_DENY_WRITE, &hVfsFileOva);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExitFailure(Appliance::tr("Failed to open OVA '%s' for updating: %Rrc"), pszOva, vrc);

    RTVFSFSSTREAM hVfsFssOva;
    vrc = RTZipTarFsStreamForFile(hVfsFileOva, RTZIPTARFORMAT_DEFAULT, RTZIPTAR_C_UPDATE, &hVfsFssOva);
    RTVfsFileRelease(hVfsFileOva);
    if (RT_FAILURE(vrc))
        return RTMsgErrorExitFailure(Appliance::tr("Failed to open OVA '%s' as a TAR file: %Rrc"), pszOva, vrc);
    *phVfsFssOva = hVfsFssOva;

    /*
     * Scan the objects in the stream and locate the manifest and any existing cert file.
     */
    if (iVerbosity >= 2)
        RTMsgInfo(Appliance::tr("Scanning OVA '%s' for a manifest and signature..."), pszOva);
    char *pszSignatureName = NULL;
    for (;;)
    {
        /*
         * Retrive the next object.
         */
        char           *pszName;
        RTVFSOBJTYPE    enmType;
        RTVFSOBJ        hVfsObj;
        vrc = RTVfsFsStrmNext(hVfsFssOva, &pszName, &enmType, &hVfsObj);
        if (RT_FAILURE(vrc))
        {
            if (vrc == VERR_EOF)
                vrc = VINF_SUCCESS;
            else
                RTMsgError(Appliance::tr("RTVfsFsStrmNext returned %Rrc"), vrc);
            break;
        }

        if (iVerbosity > 2)
            RTMsgInfo("  %s %s\n", RTVfsTypeName(enmType), pszName);

        /*
         * Should we process this entry?
         */
        const char *pszSuffix = RTPathSuffix(pszName);
        if (   pszSuffix
            && RTStrICmpAscii(pszSuffix, ".mf") == 0
            && (enmType == RTVFSOBJTYPE_IO_STREAM || enmType == RTVFSOBJTYPE_FILE))
        {
            if (*phVfsManifest != NIL_RTVFSFILE)
                vrc = RTMsgErrorRc(VERR_DUPLICATE, Appliance::tr("OVA contains multiple manifests! first: %s  second: %s"),
                                   pStrManifestName->c_str(), pszName);
            else if (pszSignatureName)
                vrc = RTMsgErrorRc(VERR_WRONG_ORDER,
                                   Appliance::tr("Unsupported OVA file ordering! Signature file ('%s') as succeeded by '%s'."),
                                   pszSignatureName, pszName);
            else
            {
                if (iVerbosity >= 2)
                    RTMsgInfo(Appliance::tr("Found manifest file: %s"), pszName);
                vrc = pStrManifestName->assignNoThrow(pszName);
                if (RT_SUCCESS(vrc))
                {
                    RTVFSIOSTREAM hVfsIos = RTVfsObjToIoStream(hVfsObj);
                    Assert(hVfsIos != NIL_RTVFSIOSTREAM);
                    vrc = RTVfsMemorizeIoStreamAsFile(hVfsIos, RTFILE_O_READ, phVfsManifest);
                    RTVfsIoStrmRelease(hVfsIos);     /* consumes stream handle.  */
                    if (RT_FAILURE(vrc))
                        vrc = RTMsgErrorRc(VERR_DUPLICATE, Appliance::tr("Failed to memorize the manifest: %Rrc"), vrc);
                }
                else
                    RTMsgError(Appliance::tr("Out of memory!"));
            }
        }
        else if (   pszSuffix
                 && RTStrICmpAscii(pszSuffix, ".cert") == 0
                 && (enmType == RTVFSOBJTYPE_IO_STREAM || enmType == RTVFSOBJTYPE_FILE))
        {
            if (*phVfsOldSignature != NIL_RTVFSOBJ)
                vrc = RTMsgErrorRc(VERR_WRONG_ORDER, Appliance::tr("Multiple signature files! (%s)"), pszName);
            else
            {
                if (iVerbosity >= 2)
                    RTMsgInfo(Appliance::tr("Found existing signature file: %s"), pszName);
                pszSignatureName   = pszName;
                *phVfsOldSignature = hVfsObj;
                pszName = NULL;
                hVfsObj = NIL_RTVFSOBJ;
            }
        }
        else if (pszSignatureName)
            vrc = RTMsgErrorRc(VERR_WRONG_ORDER,
                               Appliance::tr("Unsupported OVA file ordering! Signature file ('%s') as succeeded by '%s'."),
                               pszSignatureName, pszName);

        /*
         * Release the current object and string.
         */
        RTVfsObjRelease(hVfsObj);
        RTStrFree(pszName);
        if (RT_FAILURE(vrc))
            break;
    }

    /*
     * Complain if no manifest.
     */
    if (RT_SUCCESS(vrc) && *phVfsManifest == NIL_RTVFSFILE)
        vrc = RTMsgErrorRc(VERR_NOT_FOUND, Appliance::tr("The OVA contains no manifest and cannot be signed!"));
    else if (RT_SUCCESS(vrc) && *phVfsOldSignature != NIL_RTVFSOBJ && !fReSign)
        vrc = RTMsgErrorRc(VERR_ALREADY_EXISTS,
                           Appliance::tr("The OVA is already signed ('%s')! (Use the --force option to force re-signing it.)"),
                           pszSignatureName);

    RTStrFree(pszSignatureName);
    return vrc;
}


/**
 * Continues where openOvaAndGetManifestAndOldSignature() left off and writes
 * the signature file to the OVA.
 *
 * When @a hVfsOldSignature isn't NIL, the old signature it represent will be
 * replaced.  The open function has already made sure there isn't anything
 * following the .cert file in that case.
 */
static int updateTheOvaSignature(RTVFSFSSTREAM hVfsFssOva, const char *pszOva, const char *pszSignatureName,
                                 RTVFSFILE hVfsFileSignature, RTVFSOBJ hVfsOldSignature, unsigned iVerbosity)
{
    if (iVerbosity > 1)
        RTMsgInfo(Appliance::tr("Writing '%s' to the OVA..."), pszSignatureName);

    /*
     * Truncate the file at the old signature, if present.
     */
    int vrc;
    if (hVfsOldSignature != NIL_RTVFSOBJ)
    {
        vrc = RTZipTarFsStreamTruncate(hVfsFssOva, hVfsOldSignature, false /*fAfter*/);
        if (RT_FAILURE(vrc))
            return RTMsgErrorRc(vrc, Appliance::tr("RTZipTarFsStreamTruncate failed on '%s': %Rrc"), pszOva, vrc);
    }

    /*
     * Append the signature file.  We have to rewind it first or
     * we'll end up with VERR_EOF, probably not a great idea...
     */
    vrc = RTVfsFileSeek(hVfsFileSignature, 0, RTFILE_SEEK_BEGIN, NULL);
    if (RT_FAILURE(vrc))
        return RTMsgErrorRc(vrc, Appliance::tr("RTVfsFileSeek(hVfsFileSignature) failed: %Rrc"), vrc);

    RTVFSOBJ hVfsObj = RTVfsObjFromFile(hVfsFileSignature);
    vrc = RTVfsFsStrmAdd(hVfsFssOva, pszSignatureName, hVfsObj, 0 /*fFlags*/);
    RTVfsObjRelease(hVfsObj);
    if (RT_FAILURE(vrc))
        return RTMsgErrorRc(vrc, Appliance::tr("RTVfsFsStrmAdd('%s') failed on '%s': %Rrc"), pszSignatureName, pszOva, vrc);

    /*
     * Terminate the file system stream.
     */
    vrc = RTVfsFsStrmEnd(hVfsFssOva);
    if (RT_FAILURE(vrc))
        return RTMsgErrorRc(vrc, Appliance::tr("RTVfsFsStrmEnd failed on '%s': %Rrc"), pszOva, vrc);

    return VINF_SUCCESS;
}


/**
 * Worker for doCheckPkcs7Signature.
 */
static int doCheckPkcs7SignatureWorker(PRTCRPKCS7CONTENTINFO pContentInfo, void const *pvManifest, size_t cbManifest,
                                       unsigned iVerbosity, const char *pszTag, PRTERRINFOSTATIC pErrInfo)
{
    int vrc;

    /*
     * It must be signedData.
     */
    if (RTCrPkcs7ContentInfo_IsSignedData(pContentInfo))
    {
        PRTCRPKCS7SIGNEDDATA pSignedData = pContentInfo->u.pSignedData;

        /*
         * Inside the signedData there must be just 'data'.
         */
        if (!strcmp(pSignedData->ContentInfo.ContentType.szObjId, RTCR_PKCS7_DATA_OID))
        {
            /*
             * Check that things add up.
             */
            vrc = RTCrPkcs7SignedData_CheckSanity(pSignedData,
                                                  RTCRPKCS7SIGNEDDATA_SANITY_F_ONLY_KNOWN_HASH
                                                  | RTCRPKCS7SIGNEDDATA_SANITY_F_SIGNING_CERT_PRESENT,
                                                  RTErrInfoInitStatic(pErrInfo), "SD");
            if (RT_SUCCESS(vrc))
            {
                if (iVerbosity > 2 && pszTag == NULL)
                    RTMsgInfo(Appliance::tr("  Successfully decoded the PKCS#7/CMS signature..."));

                /*
                 * Check that we can verify the signed data, but skip certificate validate as
                 * we probably don't necessarily have the correct root certs handy here.
                 */
                RTTIMESPEC Now;
                vrc = RTCrPkcs7VerifySignedDataWithExternalData(pContentInfo, RTCRPKCS7VERIFY_SD_F_TRUST_ALL_CERTS,
                                                                NIL_RTCRSTORE /*hAdditionalCerts*/,
                                                                NIL_RTCRSTORE /*hTrustedCerts*/,
                                                                RTTimeNow(&Now),
                                                                NULL /*pfnVerifyCert*/, NULL /*pvUser*/,
                                                                pvManifest, cbManifest, RTErrInfoInitStatic(pErrInfo));
                if (RT_SUCCESS(vrc))
                {
                    if (iVerbosity > 1 && pszTag != NULL)
                        RTMsgInfo(Appliance::tr("  Successfully verified the PKCS#7/CMS signature"));
                }
                else
                    vrc = RTMsgErrorRc(vrc, Appliance::tr("Failed to verify the PKCS#7/CMS signature: %Rrc%RTeim"),
                                       vrc, &pErrInfo->Core);
            }
            else
                RTMsgError(Appliance::tr("RTCrPkcs7SignedData_CheckSanity failed on PKCS#7/CMS signature: %Rrc%RTeim"),
                           vrc, &pErrInfo->Core);

        }
        else
            vrc = RTMsgErrorRc(VERR_WRONG_TYPE, Appliance::tr("PKCS#7/CMS signature inner ContentType isn't 'data' but: %s"),
                               pSignedData->ContentInfo.ContentType.szObjId);
    }
    else
        vrc = RTMsgErrorRc(VERR_WRONG_TYPE, Appliance::tr("PKCS#7/CMD signature is not 'signedData': %s"),
                           pContentInfo->ContentType.szObjId);
    return vrc;
}

/**
 * For testing the decoding side.
 */
static int doCheckPkcs7Signature(void const *pvSignature, size_t cbSignature, PCRTCRX509CERTIFICATE pCertificate,
                                 RTCRSTORE hIntermediateCerts, void const *pvManifest, size_t cbManifest,
                                 unsigned iVerbosity, PRTERRINFOSTATIC pErrInfo)
{
    RT_NOREF(pCertificate, hIntermediateCerts);

    RTASN1CURSORPRIMARY PrimaryCursor;
    RTAsn1CursorInitPrimary(&PrimaryCursor, pvSignature, (uint32_t)cbSignature, RTErrInfoInitStatic(pErrInfo),
                            &g_RTAsn1DefaultAllocator, 0, "Signature");

    RTCRPKCS7CONTENTINFO ContentInfo;
    RT_ZERO(ContentInfo);
    int vrc = RTCrPkcs7ContentInfo_DecodeAsn1(&PrimaryCursor.Cursor, 0, &ContentInfo, "CI");
    if (RT_SUCCESS(vrc))
    {
        if (iVerbosity > 5)
            RTAsn1Dump(&ContentInfo.SeqCore.Asn1Core, 0 /*fFlags*/, 0 /*uLevel*/, RTStrmDumpPrintfV, g_pStdOut);

        vrc = doCheckPkcs7SignatureWorker(&ContentInfo, pvManifest, cbManifest, iVerbosity, NULL, pErrInfo);
        if (RT_SUCCESS(vrc))
        {
            /*
             * Clone it and repeat.  This is to catch IPRT paths assuming
             * that encoded data is always on hand.
             */
            RTCRPKCS7CONTENTINFO ContentInfo2;
            vrc = RTCrPkcs7ContentInfo_Clone(&ContentInfo2, &ContentInfo, &g_RTAsn1DefaultAllocator);
            if (RT_SUCCESS(vrc))
            {
                vrc = doCheckPkcs7SignatureWorker(&ContentInfo2, pvManifest, cbManifest, iVerbosity, "cloned", pErrInfo);
                RTCrPkcs7ContentInfo_Delete(&ContentInfo2);
            }
            else
                vrc = RTMsgErrorRc(vrc, Appliance::tr("RTCrPkcs7ContentInfo_Clone failed: %Rrc"), vrc);
        }
    }
    else
        RTMsgError(Appliance::tr("RTCrPkcs7ContentInfo_DecodeAsn1 failed to decode PKCS#7/CMS signature: %Rrc%RTemi"),
                   vrc, &pErrInfo->Core);

    RTCrPkcs7ContentInfo_Delete(&ContentInfo);
    return vrc;
}


/**
 * Creates a PKCS\#7 signature and appends it to the signature file in PEM
 * format.
 */
static int doAddPkcs7Signature(PCRTCRX509CERTIFICATE pCertificate, RTCRKEY hPrivateKey, RTDIGESTTYPE enmDigestType,
                               unsigned cIntermediateCerts, const char **papszIntermediateCerts, RTVFSFILE hVfsFileManifest,
                               unsigned iVerbosity, PRTERRINFOSTATIC pErrInfo, RTVFSFILE hVfsFileSignature)
{
    /*
     * Add a blank line, just for good measure.
     */
    int vrc = RTVfsFileWrite(hVfsFileSignature, "\n", 1, NULL);
    if (RT_FAILURE(vrc))
        return RTMsgErrorRc(vrc, "RTVfsFileWrite/signature: %Rrc", vrc);

    /*
     * Read the manifest into a single memory block.
     */
    uint64_t cbManifest;
    vrc = RTVfsFileQuerySize(hVfsFileManifest, &cbManifest);
    if (RT_FAILURE(vrc))
        return RTMsgErrorRc(vrc, "RTVfsFileQuerySize/manifest: %Rrc", vrc);
    if (cbManifest > _4M)
        return RTMsgErrorRc(VERR_OUT_OF_RANGE, Appliance::tr("Manifest is too big: %#RX64 bytes, max 4MiB", "", cbManifest),
                            cbManifest);

    void *pvManifest = RTMemAllocZ(cbManifest + 1);
    if (!pvManifest)
        return RTMsgErrorRc(VERR_NO_MEMORY, Appliance::tr("Out of memory!"));

    vrc = RTVfsFileReadAt(hVfsFileManifest, 0, pvManifest, (size_t)cbManifest, NULL);
    if (RT_SUCCESS(vrc))
    {
        /*
         * Load intermediate certificates.
         */
        RTCRSTORE hIntermediateCerts = NIL_RTCRSTORE;
        if (cIntermediateCerts)
        {
            vrc = RTCrStoreCreateInMem(&hIntermediateCerts, cIntermediateCerts);
            if (RT_SUCCESS(vrc))
            {
                for (unsigned i = 0; i < cIntermediateCerts; i++)
                {
                    const char *pszFile = papszIntermediateCerts[i];
                    vrc = RTCrStoreCertAddFromFile(hIntermediateCerts, 0 /*fFlags*/, pszFile, &pErrInfo->Core);
                    if (RT_FAILURE(vrc))
                    {
                        RTMsgError(Appliance::tr("RTCrStoreCertAddFromFile failed on '%s': %Rrc%#RTeim"),
                                   pszFile, vrc, &pErrInfo->Core);
                        break;
                    }
                }
            }
            else
                RTMsgError(Appliance::tr("RTCrStoreCreateInMem failed: %Rrc"), vrc);
        }
        if (RT_SUCCESS(vrc))
        {
            /*
             * Do a dry run to determin the size of the signed data.
             */
            size_t cbResult = 0;
            vrc = RTCrPkcs7SimpleSignSignedData(RTCRPKCS7SIGN_SD_F_DEATCHED | RTCRPKCS7SIGN_SD_F_NO_SMIME_CAP,
                                                pCertificate, hPrivateKey, pvManifest, (size_t)cbManifest, enmDigestType,
                                                hIntermediateCerts, NULL /*pAdditionalAuthenticatedAttribs*/,
                                                NULL /*pvResult*/, &cbResult, RTErrInfoInitStatic(pErrInfo));
            if (vrc == VERR_BUFFER_OVERFLOW)
            {
                /*
                 * Allocate a buffer of the right size and do the real run.
                 */
                void *pvResult = RTMemAllocZ(cbResult);
                if (pvResult)
                {
                    vrc = RTCrPkcs7SimpleSignSignedData(RTCRPKCS7SIGN_SD_F_DEATCHED | RTCRPKCS7SIGN_SD_F_NO_SMIME_CAP,
                                                        pCertificate, hPrivateKey, pvManifest, (size_t)cbManifest, enmDigestType,
                                                        hIntermediateCerts, NULL /*pAdditionalAuthenticatedAttribs*/,
                                                        pvResult, &cbResult, RTErrInfoInitStatic(pErrInfo));
                    if (RT_SUCCESS(vrc))
                    {
                        /*
                         * Add it to the signature file in PEM format.
                         */
                        vrc = (int)RTCrPemWriteBlobToVfsFile(hVfsFileSignature, pvResult, cbResult, "CMS");
                        if (RT_SUCCESS(vrc))
                        {
                            if (iVerbosity > 1)
                                RTMsgInfo(Appliance::tr("Created PKCS#7/CMS signature: %zu bytes, %s.", "", cbResult),
                                          cbResult, RTCrDigestTypeToName(enmDigestType));
                            if (enmDigestType == RTDIGESTTYPE_SHA1)
                                RTMsgWarning(Appliance::tr("Using SHA-1 instead of SHA-3 for the PKCS#7/CMS signature."));

                            /*
                             * Try decode and verify the signature.
                             */
                            vrc = doCheckPkcs7Signature(pvResult, cbResult, pCertificate, hIntermediateCerts,
                                                        pvManifest, (size_t)cbManifest, iVerbosity, pErrInfo);
                        }
                        else
                            RTMsgError(Appliance::tr("RTCrPemWriteBlobToVfsFile failed: %Rrc"), vrc);
                    }
                    RTMemFree(pvResult);
                }
                else
                    vrc = RTMsgErrorRc(VERR_NO_MEMORY, Appliance::tr("Out of memory!"));
            }
            else
                RTMsgError(Appliance::tr("RTCrPkcs7SimpleSignSignedData failed: %Rrc%#RTeim"), vrc, &pErrInfo->Core);
        }
    }
    else
        RTMsgError(Appliance::tr("RTVfsFileReadAt failed: %Rrc"), vrc);
    RTMemFree(pvManifest);
    return vrc;
}


/**
 * Performs the OVA signing, producing an in-memory cert-file.
 */
static int doTheOvaSigning(PRTCRX509CERTIFICATE pCertificate, RTCRKEY hPrivateKey, RTDIGESTTYPE enmDigestType,
                           const char *pszManifestName, RTVFSFILE hVfsFileManifest,
                           bool fPkcs7, unsigned cIntermediateCerts, const char **papszIntermediateCerts, unsigned iVerbosity,
                           PRTERRINFOSTATIC pErrInfo, PRTVFSFILE phVfsFileSignature)
{
    /*
     * Determine the digest types, preferring SHA-256 for the OVA signature
     * and SHA-512 for the PKCS#7/CMS one.  Try use different hashes for the two.
     */
    if (enmDigestType == RTDIGESTTYPE_UNKNOWN)
    {
        if (RTCrPkixCanCertHandleDigestType(pCertificate, RTDIGESTTYPE_SHA256, NULL))
            enmDigestType = RTDIGESTTYPE_SHA256;
        else
            enmDigestType = RTDIGESTTYPE_SHA1;
    }

    /* Try SHA-3 for better diversity, only fall back on SHA1 if the private
       key doesn't have enough bits (we skip SHA2 as it has the same variants
       and key size requirements as SHA-3). */
    RTDIGESTTYPE enmPkcs7DigestType;
    if (RTCrPkixCanCertHandleDigestType(pCertificate, RTDIGESTTYPE_SHA3_512, NULL))
        enmPkcs7DigestType = RTDIGESTTYPE_SHA3_512;
    else if (RTCrPkixCanCertHandleDigestType(pCertificate, RTDIGESTTYPE_SHA3_384, NULL))
        enmPkcs7DigestType = RTDIGESTTYPE_SHA3_384;
    else if (RTCrPkixCanCertHandleDigestType(pCertificate, RTDIGESTTYPE_SHA3_256, NULL))
        enmPkcs7DigestType = RTDIGESTTYPE_SHA3_256;
    else if (RTCrPkixCanCertHandleDigestType(pCertificate, RTDIGESTTYPE_SHA3_224, NULL))
        enmPkcs7DigestType = RTDIGESTTYPE_SHA3_224;
    else
        enmPkcs7DigestType = RTDIGESTTYPE_SHA1;

    /*
     * Figure the string name for the .cert file.
     */
    const char *pszDigestType;
    switch (enmDigestType)
    {
        case RTDIGESTTYPE_SHA1:         pszDigestType = "SHA1"; break;
        case RTDIGESTTYPE_SHA256:       pszDigestType = "SHA256"; break;
        case RTDIGESTTYPE_SHA224:       pszDigestType = "SHA224"; break;
        case RTDIGESTTYPE_SHA512:       pszDigestType = "SHA512"; break;
        default:
            return RTMsgErrorRc(VERR_INVALID_PARAMETER,
                                Appliance::tr("Unsupported digest type: %s"), RTCrDigestTypeToName(enmDigestType));
    }

    /*
     * Digest the manifest file.
     */
    RTCRDIGEST hDigest = NIL_RTCRDIGEST;
    int vrc = RTCrDigestCreateByType(&hDigest, enmDigestType);
    if (RT_FAILURE(vrc))
        return RTMsgErrorRc(vrc, Appliance::tr("Failed to create digest for %s: %Rrc"), RTCrDigestTypeToName(enmDigestType), vrc);

    vrc = RTCrDigestUpdateFromVfsFile(hDigest, hVfsFileManifest, true /*fRewindFile*/);
    if (RT_SUCCESS(vrc))
        vrc = RTCrDigestFinal(hDigest, NULL, 0);
    if (RT_SUCCESS(vrc))
    {
        /*
         * Sign the digest.  Two passes, first to figure the signature size, the
         * second to do the actual signing.
         */
        PCRTASN1OBJID const   pAlgorithm  = &pCertificate->TbsCertificate.SubjectPublicKeyInfo.Algorithm.Algorithm;
        PCRTASN1DYNTYPE const pAlgoParams = &pCertificate->TbsCertificate.SubjectPublicKeyInfo.Algorithm.Parameters;
        size_t cbSignature = 0;
        vrc = RTCrPkixPubKeySignDigest(pAlgorithm, hPrivateKey, pAlgoParams, hDigest, 0 /*fFlags*/,
                                       NULL /*pvSignature*/, &cbSignature, RTErrInfoInitStatic(pErrInfo));
        if (vrc == VERR_BUFFER_OVERFLOW)
        {
            void *pvSignature = RTMemAllocZ(cbSignature);
            if (pvSignature)
            {
                vrc = RTCrPkixPubKeySignDigest(pAlgorithm, hPrivateKey, pAlgoParams, hDigest, 0,
                                               pvSignature, &cbSignature, RTErrInfoInitStatic(pErrInfo));
                if (RT_SUCCESS(vrc))
                {
                    if (iVerbosity > 1)
                        RTMsgInfo(Appliance::tr("Created OVA signature: %zu bytes, %s", "", cbSignature), cbSignature,
                                  RTCrDigestTypeToName(enmDigestType));

                    /*
                     * Verify the signature using the certificate to make sure we've
                     * been given the right private key.
                     */
                    vrc = RTCrPkixPubKeyVerifySignedDigestByCertPubKeyInfo(&pCertificate->TbsCertificate.SubjectPublicKeyInfo,
                                                                           pvSignature, cbSignature, hDigest,
                                                                           RTErrInfoInitStatic(pErrInfo));
                    if (RT_SUCCESS(vrc))
                    {
                        if (iVerbosity > 2)
                            RTMsgInfo(Appliance::tr("  Successfully decoded and verified the OVA signature.\n"));

                        /*
                         * Create the output file.
                         */
                        RTVFSFILE hVfsFileSignature;
                        vrc = RTVfsMemFileCreate(NIL_RTVFSIOSTREAM, _8K, &hVfsFileSignature);
                        if (RT_SUCCESS(vrc))
                        {
                            vrc = (int)RTVfsFilePrintf(hVfsFileSignature, "%s(%s) = %#.*Rhxs\n\n",
                                                       pszDigestType, pszManifestName, cbSignature, pvSignature);
                            if (RT_SUCCESS(vrc))
                            {
                                vrc = (int)RTCrX509Certificate_WriteToVfsFile(hVfsFileSignature, pCertificate,
                                                                              RTErrInfoInitStatic(pErrInfo));
                                if (RT_SUCCESS(vrc))
                                {
                                    if (fPkcs7)
                                        vrc = doAddPkcs7Signature(pCertificate, hPrivateKey, enmPkcs7DigestType,
                                                                  cIntermediateCerts, papszIntermediateCerts, hVfsFileManifest,
                                                                  iVerbosity, pErrInfo, hVfsFileSignature);
                                    if (RT_SUCCESS(vrc))
                                    {
                                        /*
                                         * Success.
                                         */
                                        *phVfsFileSignature = hVfsFileSignature;
                                        hVfsFileSignature = NIL_RTVFSFILE;
                                    }
                                }
                                else
                                    RTMsgError(Appliance::tr("Failed to write certificate to signature file: %Rrc%#RTeim"),
                                               vrc, &pErrInfo->Core);
                            }
                            else
                                RTMsgError(Appliance::tr("Failed to produce signature file: %Rrc"), vrc);
                            RTVfsFileRelease(hVfsFileSignature);
                        }
                        else
                            RTMsgError(Appliance::tr("RTVfsMemFileCreate failed: %Rrc"), vrc);
                    }
                    else
                        RTMsgError(Appliance::tr("Encountered a problem when validating the signature we just created: %Rrc%#RTeim\n"
                                        "Please make sure the certificate and private key matches."),
                                   vrc, &pErrInfo->Core);
                }
                else
                    RTMsgError(Appliance::tr("2nd RTCrPkixPubKeySignDigest call failed: %Rrc%#RTeim"), vrc, pErrInfo->Core);
                RTMemFree(pvSignature);
            }
            else
                vrc = RTMsgErrorRc(VERR_NO_MEMORY, Appliance::tr("Out of memory!"));
        }
        else
            RTMsgError(Appliance::tr("RTCrPkixPubKeySignDigest failed: %Rrc%#RTeim"), vrc, pErrInfo->Core);
    }
    else
        RTMsgError(Appliance::tr("Failed to create digest %s: %Rrc"), RTCrDigestTypeToName(enmDigestType), vrc);
    RTCrDigestRelease(hDigest);
    return vrc;
}


/**
 * Handles the 'ovasign' command.
 */
RTEXITCODE handleSignAppliance(HandlerArg *arg)
{
    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--certificate",              'c', RTGETOPT_REQ_STRING },
        { "--private-key",              'k', RTGETOPT_REQ_STRING },
        { "--private-key-password",     'p', RTGETOPT_REQ_STRING },
        { "--private-key-password-file",'P', RTGETOPT_REQ_STRING },
        { "--digest-type",              'd', RTGETOPT_REQ_STRING },
        { "--pkcs7",                    '7', RTGETOPT_REQ_NOTHING },
        { "--cms",                      '7', RTGETOPT_REQ_NOTHING },
        { "--no-pkcs7",                 'n', RTGETOPT_REQ_NOTHING },
        { "--no-cms",                   'n', RTGETOPT_REQ_NOTHING },
        { "--intermediate-cert-file",   'i', RTGETOPT_REQ_STRING },
        { "--force",                    'f', RTGETOPT_REQ_NOTHING },
        { "--verbose",                  'v', RTGETOPT_REQ_NOTHING },
        { "--quiet",                    'q', RTGETOPT_REQ_NOTHING },
        { "--dry-run",                  'D', RTGETOPT_REQ_NOTHING },
    };

    RTGETOPTSTATE GetState;
    int vrc = RTGetOptInit(&GetState, arg->argc, arg->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    const char     *pszOva              = NULL;
    const char     *pszCertificate      = NULL;
    const char     *pszPrivateKey       = NULL;
    Utf8Str         strPrivateKeyPassword;
    RTDIGESTTYPE    enmDigestType       = RTDIGESTTYPE_UNKNOWN;
    bool            fPkcs7              = true;
    unsigned        cIntermediateCerts  = 0;
    const char     *apszIntermediateCerts[32];
    bool            fReSign             = false;
    unsigned        iVerbosity          = 1;
    bool            fDryRun             = false;

    int c;
    RTGETOPTUNION ValueUnion;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'c':
                pszCertificate = ValueUnion.psz;
                break;

            case 'k':
                pszPrivateKey = ValueUnion.psz;
                break;

            case 'p':
                if (strPrivateKeyPassword.isNotEmpty())
                    RTMsgWarning(Appliance::tr("Password is given more than once."));
                strPrivateKeyPassword = ValueUnion.psz;
                break;

            case 'P':
            {
                if (strPrivateKeyPassword.isNotEmpty())
                    RTMsgWarning(Appliance::tr("Password is given more than once."));
                RTEXITCODE rcExit = readPasswordFile(ValueUnion.psz, &strPrivateKeyPassword);
                if (rcExit == RTEXITCODE_SUCCESS)
                    break;
                return rcExit;
            }

            case 'd':
                if (   RTStrICmp(ValueUnion.psz, "sha1") == 0
                    || RTStrICmp(ValueUnion.psz, "sha-1") == 0)
                    enmDigestType = RTDIGESTTYPE_SHA1;
                else if (   RTStrICmp(ValueUnion.psz, "sha256") == 0
                         || RTStrICmp(ValueUnion.psz, "sha-256") == 0)
                    enmDigestType = RTDIGESTTYPE_SHA256;
                else if (   RTStrICmp(ValueUnion.psz, "sha512") == 0
                         || RTStrICmp(ValueUnion.psz, "sha-512") == 0)
                    enmDigestType = RTDIGESTTYPE_SHA512;
                else
                    return RTMsgErrorExitFailure(Appliance::tr("Unknown digest type: %s"), ValueUnion.psz);
                break;

            case '7':
                fPkcs7 = true;
                break;

            case 'n':
                fPkcs7 = false;
                break;

            case 'i':
                if (cIntermediateCerts >= RT_ELEMENTS(apszIntermediateCerts))
                    return RTMsgErrorExitFailure(Appliance::tr("Too many intermediate certificates: max %zu"),
                                                 RT_ELEMENTS(apszIntermediateCerts));
                apszIntermediateCerts[cIntermediateCerts++] = ValueUnion.psz;
                fPkcs7 = true;
                break;

            case 'f':
                fReSign = true;
                break;

            case 'v':
                iVerbosity++;
                break;

            case 'q':
                iVerbosity = 0;
                break;

            case 'D':
                fDryRun = true;
                break;

            case VINF_GETOPT_NOT_OPTION:
                if (!pszOva)
                {
                    pszOva = ValueUnion.psz;
                    break;
                }
                RT_FALL_THRU();
            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    /* Required paramaters: */
    if (!pszOva || !*pszOva)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, Appliance::tr("No OVA file was specified!"));
    if (!pszCertificate || !*pszCertificate)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, Appliance::tr("No signing certificate (--certificate=<file>) was specified!"));
    if (!pszPrivateKey || !*pszPrivateKey)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX, Appliance::tr("No signing private key (--private-key=<file>) was specified!"));

    /* Check that input files exists before we commence: */
    if (!RTFileExists(pszOva))
        return RTMsgErrorExitFailure(Appliance::tr("The specified OVA file was not found: %s"), pszOva);
    if (!RTFileExists(pszCertificate))
        return RTMsgErrorExitFailure(Appliance::tr("The specified certificate file was not found: %s"), pszCertificate);
    if (!RTFileExists(pszPrivateKey))
        return RTMsgErrorExitFailure(Appliance::tr("The specified private key file was not found: %s"), pszPrivateKey);

    /*
     * Open the OVA, read the manifest and look for any existing signature.
     */
    RTVFSFSSTREAM   hVfsFssOva       = NIL_RTVFSFSSTREAM;
    RTVFSOBJ        hVfsOldSignature = NIL_RTVFSOBJ;
    RTVFSFILE       hVfsFileManifest = NIL_RTVFSFILE;
    Utf8Str         strManifestName;
    vrc = openOvaAndGetManifestAndOldSignature(pszOva, iVerbosity, fReSign,
                                               &hVfsFssOva, &strManifestName, &hVfsFileManifest, &hVfsOldSignature);
    if (RT_SUCCESS(vrc))
    {
        /*
         * Read the certificate and private key.
         */
        RTERRINFOSTATIC     ErrInfo;
        RTCRX509CERTIFICATE Certificate;
        vrc = RTCrX509Certificate_ReadFromFile(&Certificate, pszCertificate, 0, &g_RTAsn1DefaultAllocator,
                                               RTErrInfoInitStatic(&ErrInfo));
        if (RT_FAILURE(vrc))
            return RTMsgErrorExitFailure(Appliance::tr("Error reading certificate from '%s': %Rrc%#RTeim"),
                                         pszCertificate, vrc, &ErrInfo.Core);

        RTCRKEY hPrivateKey = NIL_RTCRKEY;
        vrc = RTCrKeyCreateFromFile(&hPrivateKey, 0 /*fFlags*/, pszPrivateKey, strPrivateKeyPassword.c_str(),
                                    RTErrInfoInitStatic(&ErrInfo));
        if (RT_SUCCESS(vrc))
        {
            if (iVerbosity > 1)
                RTMsgInfo(Appliance::tr("Successfully read the certificate and private key."));

            /*
             * Do the signing and create the signature file.
             */
            RTVFSFILE hVfsFileSignature = NIL_RTVFSFILE;
            vrc = doTheOvaSigning(&Certificate, hPrivateKey, enmDigestType, strManifestName.c_str(), hVfsFileManifest,
                                  fPkcs7, cIntermediateCerts, apszIntermediateCerts, iVerbosity, &ErrInfo, &hVfsFileSignature);

            /*
             * Construct the signature filename:
             */
            if (RT_SUCCESS(vrc))
            {
                Utf8Str strSignatureName;
                vrc = strSignatureName.assignNoThrow(strManifestName);
                if (RT_SUCCESS(vrc))
                    vrc = strSignatureName.stripSuffix().appendNoThrow(".cert");
                if (RT_SUCCESS(vrc) && !fDryRun)
                {
                    /*
                     * Update the OVA.
                     */
                    vrc = updateTheOvaSignature(hVfsFssOva, pszOva, strSignatureName.c_str(),
                                                hVfsFileSignature, hVfsOldSignature, iVerbosity);
                    if (RT_SUCCESS(vrc) && iVerbosity > 0)
                        RTMsgInfo(Appliance::tr("Successfully signed '%s'."), pszOva);
                }
            }
            RTCrKeyRelease(hPrivateKey);
        }
        else
            RTPrintf(Appliance::tr("Error reading the private key from %s: %Rrc%#RTeim"), pszPrivateKey, vrc, &ErrInfo.Core);
        RTCrX509Certificate_Delete(&Certificate);
    }

    RTVfsObjRelease(hVfsOldSignature);
    RTVfsFileRelease(hVfsFileManifest);
    RTVfsFsStrmRelease(hVfsFssOva);

    return RT_SUCCESS(vrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}
