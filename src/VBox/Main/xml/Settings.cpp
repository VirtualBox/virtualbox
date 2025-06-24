/* $Id: Settings.cpp 109948 2025-06-24 19:59:46Z jack.doherty@oracle.com $ */
/** @file
 * Settings File Manipulation API.
 *
 * Two classes, MainConfigFile and MachineConfigFile, represent the VirtualBox.xml and
 * machine XML files. They share a common ancestor class, ConfigFileBase, which shares
 * functionality such as talking to the XML back-end classes and settings version management.
 *
 * The code can read all VirtualBox settings files version 1.3 and higher. That version was
 * written by VirtualBox 2.0. It can write settings version 1.7 (used by VirtualBox 2.2 and
 * 3.0) and 1.9 (used by VirtualBox 3.1) and newer ones obviously.
 *
 * The settings versions enum is defined in src/VBox/Main/idl/VirtualBox.xidl. To introduce
 * a new settings version (should be necessary at most once per VirtualBox major release,
 * if at all), add a new SettingsVersion value to that enum and grep for the previously
 * highest value to see which code in here needs adjusting.
 *
 * Certainly ConfigFileBase::ConfigFileBase() will. Change VBOX_XML_VERSION below as well.
 * VBOX_XML_VERSION does not have to be changed if the settings for a default VM do not
 * touch newly introduced attributes or tags. It has the benefit that older VirtualBox
 * versions do not trigger their "newer" code path.
 *
 * Once a new settings version has been added, these are the rules for introducing a new
 * setting: If an XML element or attribute or value is introduced that was not present in
 * previous versions, then settings version checks need to be introduced. See the
 * SettingsVersion enumeration in src/VBox/Main/idl/VirtualBox.xidl for details about which
 * version was used when.
 *
 * The settings versions checks are necessary because since version 3.1, VirtualBox no longer
 * automatically converts XML settings files but only if necessary, that is, if settings are
 * present that the old format does not support. If we write an element or attribute to a
 * settings file of an older version, then an old VirtualBox (before 3.1) will attempt to
 * validate it with XML schema, and that will certainly fail.
 *
 * So, to introduce a new setting:
 *
 *   1) Make sure the constructor of corresponding settings structure has a proper default.
 *
 *   2) In the settings reader method, try to read the setting; if it's there, great, if not,
 *      the default value will have been set by the constructor. The rule is to be tolerant
 *      here.
 *
 *   3) In MachineConfigFile::bumpSettingsVersionIfNeeded(), check if the new setting has
 *      a non-default value (i.e. that differs from the constructor). If so, bump the
 *      settings version to the current version so the settings writer (4) can write out
 *      the non-default value properly.
 *
 *      So far a corresponding method for MainConfigFile has not been necessary since there
 *      have been no incompatible changes yet.
 *
 *   4) In the settings writer method, write the setting _only_ if the current settings
 *      version (stored in m->sv) is high enough. That is, for VirtualBox 4.0, write it
 *      only if (m->sv >= SettingsVersion_v1_11).
 *
 *   5) You _must_ update xml/VirtualBox-settings.xsd to contain the new tags and attributes.
 *      Check that settings file from before and after your change are validating properly.
 *      Use "kmk testvalidsettings", it should not find any files which don't validate.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN
#include "VBox/com/string.h"
#include "VBox/settings.h"
#include <iprt/base64.h>
#include <iprt/cpp/lock.h>
#include <iprt/cpp/utils.h>
#include <iprt/cpp/xml.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/ldr.h>
#include <iprt/process.h>
#include <iprt/stream.h>
#ifdef RT_OS_WINDOWS
# include <iprt/system.h> /* For RTSystemGetNtVersion() / RTSYSTEM_MAKE_NT_VERSION. */
#endif
#include <iprt/uri.h>

// Guest Properties validation.
#include "VBox/HostServices/GuestPropertySvc.h"

// generated header
#include "SchemaDefs.h"

#include "HashedPw.h"
#include "LoggingNew.h"

using namespace com;
using namespace settings;

////////////////////////////////////////////////////////////////////////////////
//
// Defines
//
////////////////////////////////////////////////////////////////////////////////

/** VirtualBox XML settings namespace */
#define VBOX_XML_NAMESPACE      "http://www.virtualbox.org/"

/** VirtualBox XML schema location (relative URI) */
#define VBOX_XML_SCHEMA "VirtualBox-settings.xsd"

/** VirtualBox XML settings version number substring ("x.y")  */
#define VBOX_XML_VERSION        "1.12"

/** VirtualBox OVF settings import default version number substring ("x.y").
 *
 * Think twice before changing this, as all VirtualBox versions before 5.1
 * wrote the settings version when exporting, but totally ignored it on
 * importing (while it should have been a mandatory attribute), so 3rd party
 * software out there creates OVF files with the VirtualBox specific settings
 * but lacking the version attribute. This shouldn't happen any more, but
 * breaking existing OVF files isn't nice. */
#define VBOX_XML_IMPORT_VERSION "1.15"

/** VirtualBox XML settings version platform substring */
#if defined (RT_OS_DARWIN)
#   define VBOX_XML_PLATFORM     "macosx"
#elif defined (RT_OS_FREEBSD)
#   define VBOX_XML_PLATFORM     "freebsd"
#elif defined (RT_OS_LINUX)
#   define VBOX_XML_PLATFORM     "linux"
#elif defined (RT_OS_NETBSD)
#   define VBOX_XML_PLATFORM     "netbsd"
#elif defined (RT_OS_OPENBSD)
#   define VBOX_XML_PLATFORM     "openbsd"
#elif defined (RT_OS_OS2)
#   define VBOX_XML_PLATFORM     "os2"
#elif defined (RT_OS_SOLARIS)
#   define VBOX_XML_PLATFORM     "solaris"
#elif defined (RT_OS_WINDOWS)
#   define VBOX_XML_PLATFORM     "windows"
#else
#   error Unsupported platform!
#endif

/** VirtualBox XML settings full version string ("x.y-platform") */
#define VBOX_XML_VERSION_FULL   VBOX_XML_VERSION "-" VBOX_XML_PLATFORM

/** VirtualBox OVF import default settings full version string ("x.y-platform") */
#define VBOX_XML_IMPORT_VERSION_FULL   VBOX_XML_IMPORT_VERSION "-" VBOX_XML_PLATFORM

////////////////////////////////////////////////////////////////////////////////
//
// Internal data
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Opaque data structore for ConfigFileBase (only declared
 * in header, defined only here).
 */

struct ConfigFileBase::Data
{
    Data()
        : pDoc(NULL),
          pelmRoot(NULL),
          sv(SettingsVersion_Null),
          svRead(SettingsVersion_Null)
    {}

    ~Data()
    {
        cleanup();
    }

    RTCString               strFilename;
    bool                    fFileExists;

    xml::Document           *pDoc;
    xml::ElementNode        *pelmRoot;

    com::Utf8Str            strSettingsVersionFull;     // e.g. "1.7-linux"
    SettingsVersion_T       sv;                         // e.g. SettingsVersion_v1_7

    SettingsVersion_T       svRead;                     // settings version that the original file had when it was read,
                                                        // or SettingsVersion_Null if none

    void copyFrom(const Data &d)
    {
        strFilename = d.strFilename;
        fFileExists = d.fFileExists;
        strSettingsVersionFull = d.strSettingsVersionFull;
        sv = d.sv;
        svRead = d.svRead;
    }

    void cleanup()
    {
        if (pDoc)
        {
            delete pDoc;
            pDoc = NULL;
            pelmRoot = NULL;
        }
    }
};

/**
 * Private exception class (not in the header file) that makes
 * throwing xml::LogicError instances easier. That class is public
 * and should be caught by client code.
 */
class settings::ConfigFileError : public xml::LogicError
{
public:
    ConfigFileError(const ConfigFileBase *file,
                    const xml::Node *pNode,
                    const char *pcszFormat, ...)
        : xml::LogicError()
    {
        va_list args;
        va_start(args, pcszFormat);
        Utf8Str strWhat(pcszFormat, args);
        va_end(args);

        Utf8Str strLine;
        if (pNode)
            strLine = Utf8StrFmt(" (line %RU32)", pNode->getLineNumber());

        const char *pcsz = strLine.c_str();
        Utf8StrFmt str(N_("Error in %s%s -- %s"),
                       file->m->strFilename.c_str(),
                       (pcsz) ? pcsz : "",
                       strWhat.c_str());

        setWhat(str.c_str());
    }
};

////////////////////////////////////////////////////////////////////////////////
//
// ConfigFileBase
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Constructor. Allocates the XML internals, parses the XML file if
 * pstrFilename is != NULL and reads the settings version from it.
 * @param pstrFilename
 */
ConfigFileBase::ConfigFileBase(const com::Utf8Str *pstrFilename)
    : m(new Data)
{
    m->fFileExists = false;

    if (pstrFilename)
    {
        try
        {
            // reading existing settings file:
            m->strFilename = *pstrFilename;

            xml::XmlFileParser parser;
            m->pDoc = new xml::Document;
            parser.read(*pstrFilename,
                        *m->pDoc);

            m->fFileExists = true;

            m->pelmRoot = m->pDoc->getRootElement();
            if (!m->pelmRoot || !m->pelmRoot->nameEquals("VirtualBox"))
                throw ConfigFileError(this, m->pelmRoot, N_("Root element in VirtualBox settings files must be \"VirtualBox\""));

            if (!(m->pelmRoot->getAttributeValue("version", m->strSettingsVersionFull)))
                throw ConfigFileError(this, m->pelmRoot, N_("Required VirtualBox/@version attribute is missing"));

            LogRel(("Loading settings file \"%s\" with version \"%s\"\n", m->strFilename.c_str(), m->strSettingsVersionFull.c_str()));

            m->sv = parseVersion(m->strSettingsVersionFull, m->pelmRoot);

            // remember the settings version we read in case it gets upgraded later,
            // so we know when to make backups
            m->svRead = m->sv;
        }
        catch(...)
        {
            /*
             * The destructor is not called when an exception is thrown in the constructor,
             * so we have to do the cleanup here.
             */
            delete m;
            m = NULL;
            throw;
        }
    }
    else
    {
        // creating new settings file:
        m->strSettingsVersionFull = VBOX_XML_VERSION_FULL;
        m->sv = SettingsVersion_v1_12;
    }
}

ConfigFileBase::ConfigFileBase(const ConfigFileBase &other)
    : m(new Data)
{
    copyBaseFrom(other);
    m->strFilename = "";
    m->fFileExists = false;
}

/**
 * Clean up.
 */
ConfigFileBase::~ConfigFileBase()
{
    if (m)
    {
        delete m;
        m = NULL;
    }
}

/**
 * Helper function to convert a MediaType enum value into string from.
 * @param t
 */
/*static*/
const char *ConfigFileBase::stringifyMediaType(MediaType t)
{
    switch (t)
    {
        case HardDisk:
            return "hard disk";
        case DVDImage:
            return "DVD";
        case FloppyImage:
            return "floppy";
        default:
            AssertMsgFailed(("media type %d\n", t));
            return "UNKNOWN";
    }
}

/**
 * Helper function that parses a full version number.
 *
 * Allow future versions but fail if file is older than 1.6. Throws on errors.
 * @returns settings version
 * @param strVersion
 * @param pElm
 */
SettingsVersion_T ConfigFileBase::parseVersion(const Utf8Str &strVersion, const xml::ElementNode *pElm)
{
    SettingsVersion_T sv = SettingsVersion_Null;
    if (strVersion.length() > 3)
    {
        const char *pcsz = strVersion.c_str();

        uint32_t uMajor = 0;
        char ch;
        while (   (ch = *pcsz)
               && RT_C_IS_DIGIT(ch) )
        {
            uMajor *= 10;
            uMajor += (uint32_t)(ch - '0');
            ++pcsz;
        }

        uint32_t uMinor = 0;
        if (ch == '.')
        {
            pcsz++;
            while (   (ch = *pcsz)
                   && RT_C_IS_DIGIT(ch))
            {
                uMinor *= 10;
                uMinor += (ULONG)(ch - '0');
                ++pcsz;
            }
        }

        if (uMajor == 1)
        {
            if (uMinor == 3)
                sv = SettingsVersion_v1_3;
            else if (uMinor == 4)
                sv = SettingsVersion_v1_4;
            else if (uMinor == 5)
                sv = SettingsVersion_v1_5;
            else if (uMinor == 6)
                sv = SettingsVersion_v1_6;
            else if (uMinor == 7)
                sv = SettingsVersion_v1_7;
            else if (uMinor == 8)
                sv = SettingsVersion_v1_8;
            else if (uMinor == 9)
                sv = SettingsVersion_v1_9;
            else if (uMinor == 10)
                sv = SettingsVersion_v1_10;
            else if (uMinor == 11)
                sv = SettingsVersion_v1_11;
            else if (uMinor == 12)
                sv = SettingsVersion_v1_12;
            else if (uMinor == 13)
                sv = SettingsVersion_v1_13;
            else if (uMinor == 14)
                sv = SettingsVersion_v1_14;
            else if (uMinor == 15)
                sv = SettingsVersion_v1_15;
            else if (uMinor == 16)
                sv = SettingsVersion_v1_16;
            else if (uMinor == 17)
                sv = SettingsVersion_v1_17;
            else if (uMinor == 18)
                sv = SettingsVersion_v1_18;
            else if (uMinor == 19)
                sv = SettingsVersion_v1_19;
            else if (uMinor == 20)
                sv = SettingsVersion_v1_20;
            else if (uMinor > 20)
                sv = SettingsVersion_Future;
        }
        else if (uMajor > 1)
            sv = SettingsVersion_Future;

        Log(("Parsed settings version %d.%d to enum value %d\n", uMajor, uMinor, sv));
    }

    if (sv == SettingsVersion_Null)
        throw ConfigFileError(this, pElm, N_("Cannot handle settings version '%s'"), strVersion.c_str());

    return sv;
}

/**
 * Helper function that parses a UUID in string form into
 * a com::Guid item. Accepts UUIDs both with and without
 * "{}" brackets. Throws on errors.
 * @param guid
 * @param strUUID
 * @param pElm
 */
void ConfigFileBase::parseUUID(Guid &guid,
                               const Utf8Str &strUUID,
                               const xml::ElementNode *pElm) const
{
    guid = strUUID.c_str();
    if (guid.isZero())
        throw ConfigFileError(this, pElm, N_("UUID \"%s\" has zero format"), strUUID.c_str());
    else if (!guid.isValid())
        throw ConfigFileError(this, pElm, N_("UUID \"%s\" has invalid format"), strUUID.c_str());
}

/**
 * Parses the given string in str and attempts to treat it as an ISO
 * date/time stamp to put into timestamp. Throws on errors.
 * @param timestamp
 * @param str
 * @param pElm
 */
void ConfigFileBase::parseTimestamp(RTTIMESPEC &timestamp,
                                    const com::Utf8Str &str,
                                    const xml::ElementNode *pElm) const
{
    const char *pcsz = str.c_str();
        //  yyyy-mm-ddThh:mm:ss
        // "2009-07-10T11:54:03Z"
        //  01234567890123456789
        //            1
    if (str.length() > 19)
    {
        // timezone must either be unspecified or 'Z' for UTC
        if (    (pcsz[19])
             && (pcsz[19] != 'Z')
           )
            throw ConfigFileError(this, pElm, N_("Cannot handle ISO timestamp '%s': is not UTC date"), str.c_str());

        int32_t yyyy;
        uint32_t mm, dd, hh, min, secs;
        if (    (pcsz[4] == '-')
             && (pcsz[7] == '-')
             && (pcsz[10] == 'T')
             && (pcsz[13] == ':')
             && (pcsz[16] == ':')
           )
        {
            int vrc;
            if (    (RT_SUCCESS(vrc = RTStrToInt32Ex(pcsz, NULL, 0, &yyyy)))
                        // could theoretically be negative but let's assume that nobody
                        // created virtual machines before the Christian era
                 && (RT_SUCCESS(vrc = RTStrToUInt32Ex(pcsz + 5, NULL, 0, &mm)))
                 && (RT_SUCCESS(vrc = RTStrToUInt32Ex(pcsz + 8, NULL, 0, &dd)))
                 && (RT_SUCCESS(vrc = RTStrToUInt32Ex(pcsz + 11, NULL, 0, &hh)))
                 && (RT_SUCCESS(vrc = RTStrToUInt32Ex(pcsz + 14, NULL, 0, &min)))
                 && (RT_SUCCESS(vrc = RTStrToUInt32Ex(pcsz + 17, NULL, 0, &secs)))
               )
            {
                RTTIME time =
                {
                    yyyy,
                    (uint8_t)mm,
                    0,
                    0,
                    (uint8_t)dd,
                    (uint8_t)hh,
                    (uint8_t)min,
                    (uint8_t)secs,
                    0,
                    RTTIME_FLAGS_TYPE_UTC,
                    0
                };
                if (RTTimeNormalize(&time))
                    if (RTTimeImplode(&timestamp, &time))
                        return;
            }

            throw ConfigFileError(this, pElm, N_("Cannot parse ISO timestamp '%s': runtime error, %Rra"), str.c_str(), vrc);
        }

        throw ConfigFileError(this, pElm, N_("Cannot parse ISO timestamp '%s': invalid format"), str.c_str());
    }
}

/**
 * Helper function that parses a Base64 formatted string into a binary blob.
 * @param binary
 * @param str
 * @param pElm
 */
void ConfigFileBase::parseBase64(IconBlob &binary,
                                 const Utf8Str &str,
                                 const xml::ElementNode *pElm) const
{
#define DECODE_STR_MAX _1M
    const char* psz = str.c_str();
    ssize_t cbOut = RTBase64DecodedSize(psz, NULL);
    if (cbOut > DECODE_STR_MAX)
        throw ConfigFileError(this, pElm, N_("Base64 encoded data too long (%d > %d)"), cbOut, DECODE_STR_MAX);
    else if (cbOut < 0)
        throw ConfigFileError(this, pElm, N_("Base64 encoded data '%s' invalid"), psz);
    binary.resize((size_t)cbOut);
    int vrc = VINF_SUCCESS;
    if (cbOut)
        vrc = RTBase64Decode(psz, &binary.front(), (size_t)cbOut, NULL, NULL);
    if (RT_FAILURE(vrc))
    {
        binary.resize(0);
        throw ConfigFileError(this, pElm, N_("Base64 encoded data could not be decoded (%Rrc)"), vrc);
    }
}

/**
 * Helper to create a string for a RTTIMESPEC for writing out ISO timestamps.
 * @param stamp
 * @return
 */
com::Utf8Str ConfigFileBase::stringifyTimestamp(const RTTIMESPEC &stamp) const
{
    RTTIME time;
    if (!RTTimeExplode(&time, &stamp))
        throw ConfigFileError(this, NULL, N_("Timespec %lld ms is invalid"), RTTimeSpecGetMilli(&stamp));

    return Utf8StrFmt("%04u-%02u-%02uT%02u:%02u:%02uZ",
                      time.i32Year, time.u8Month, time.u8MonthDay,
                      time.u8Hour, time.u8Minute, time.u8Second);
}

/**
 * Helper to create a base64 encoded string out of a binary blob.
 * @param str
 * @param binary
 * @throws std::bad_alloc and ConfigFileError
 */
void ConfigFileBase::toBase64(com::Utf8Str &str, const IconBlob &binary) const
{
    size_t cb = binary.size();
    if (cb > 0)
    {
        size_t cchOut = RTBase64EncodedLength(cb);
        str.reserve(cchOut + 1);
        int vrc = RTBase64Encode(&binary.front(), cb, str.mutableRaw(), str.capacity(), NULL);
        if (RT_FAILURE(vrc))
            throw ConfigFileError(this, NULL, N_("Failed to convert binary data to base64 format (%Rrc)"), vrc);
        str.jolt();
    }
}

/**
 * Helper method to read in an ExtraData subtree and stores its contents
 * in the given map of extradata items. Used for both main and machine
 * extradata (MainConfigFile and MachineConfigFile).
 * @param elmExtraData
 * @param map
 */
void ConfigFileBase::readExtraData(const xml::ElementNode &elmExtraData,
                                   StringsMap &map)
{
    xml::NodesLoop nlLevel4(elmExtraData);
    const xml::ElementNode *pelmExtraDataItem;
    while ((pelmExtraDataItem = nlLevel4.forAllNodes()))
    {
        if (pelmExtraDataItem->nameEquals("ExtraDataItem"))
        {
            // <ExtraDataItem name="GUI/LastWindowPostion" value="97,88,981,858"/>
            Utf8Str strName, strValue;
            if (   pelmExtraDataItem->getAttributeValue("name", strName)
                && pelmExtraDataItem->getAttributeValue("value", strValue) )
                map[strName] = strValue;
            else
                throw ConfigFileError(this, pelmExtraDataItem, N_("Required ExtraDataItem/@name or @value attribute is missing"));
        }
    }
}

/**
 * Reads \<USBDeviceFilter\> entries from under the given elmDeviceFilters node and
 * stores them in the given linklist. This is in ConfigFileBase because it's used
 * from both MainConfigFile (for host filters) and MachineConfigFile (for machine
 * filters).
 * @param elmDeviceFilters
 * @param ll
 */
void ConfigFileBase::readUSBDeviceFilters(const xml::ElementNode &elmDeviceFilters,
                                          USBDeviceFiltersList &ll)
{
    xml::NodesLoop nl1(elmDeviceFilters, "DeviceFilter");
    const xml::ElementNode *pelmLevel4Child;
    while ((pelmLevel4Child = nl1.forAllNodes()))
    {
        USBDeviceFilter flt;
        flt.action = USBDeviceFilterAction_Ignore;
        Utf8Str strAction;
        if (   pelmLevel4Child->getAttributeValue("name", flt.strName)
            && pelmLevel4Child->getAttributeValue("active", flt.fActive))
        {
            if (!pelmLevel4Child->getAttributeValue("vendorId", flt.strVendorId))
                pelmLevel4Child->getAttributeValue("vendorid", flt.strVendorId);            // used before 1.3
            if (!pelmLevel4Child->getAttributeValue("productId", flt.strProductId))
                pelmLevel4Child->getAttributeValue("productid", flt.strProductId);          // used before 1.3
            pelmLevel4Child->getAttributeValue("revision", flt.strRevision);
            pelmLevel4Child->getAttributeValue("manufacturer", flt.strManufacturer);
            pelmLevel4Child->getAttributeValue("product", flt.strProduct);
            if (!pelmLevel4Child->getAttributeValue("serialNumber", flt.strSerialNumber))
                pelmLevel4Child->getAttributeValue("serialnumber", flt.strSerialNumber);    // used before 1.3
            pelmLevel4Child->getAttributeValue("port", flt.strPort);

            // the next 2 are irrelevant for host USB objects
            pelmLevel4Child->getAttributeValue("remote", flt.strRemote);
            pelmLevel4Child->getAttributeValue("maskedInterfaces", flt.ulMaskedInterfaces);

            // action is only used with host USB objects
            if (pelmLevel4Child->getAttributeValue("action", strAction))
            {
                if (strAction == "Ignore")
                    flt.action = USBDeviceFilterAction_Ignore;
                else if (strAction == "Hold")
                    flt.action = USBDeviceFilterAction_Hold;
                else
                    throw ConfigFileError(this, pelmLevel4Child, N_("Invalid value '%s' in DeviceFilter/@action attribute"), strAction.c_str());
            }

            ll.push_back(flt);
        }
    }
}

/**
 * Reads a media registry entry from the main VirtualBox.xml file.
 *
 * Whereas the current media registry code is fairly straightforward, it was quite a mess
 * with settings format before 1.4 (VirtualBox 2.0 used settings format 1.3). The elements
 * in the media registry were much more inconsistent, and different elements were used
 * depending on the type of device and image.
 *
 * @param t
 * @param elmMedium
 * @param med
 */
void ConfigFileBase::readMediumOne(MediaType t,
                                   const xml::ElementNode &elmMedium,
                                   Medium &med)
{
    // <HardDisk uuid="{5471ecdb-1ddb-4012-a801-6d98e226868b}" location="/mnt/innotek-unix/vdis/Windows XP.vdi" format="VDI" type="Normal">

    Utf8Str strUUID;
    if (!elmMedium.getAttributeValue("uuid", strUUID))
        throw ConfigFileError(this, &elmMedium, N_("Required %s/@uuid attribute is missing"), elmMedium.getName());

    parseUUID(med.uuid, strUUID, &elmMedium);

    bool fNeedsLocation = true;

    if (t == HardDisk)
    {
        if (m->sv < SettingsVersion_v1_4)
        {
            // here the system is:
            //         <HardDisk uuid="{....}" type="normal">
            //           <VirtualDiskImage filePath="/path/to/xxx.vdi"/>
            //         </HardDisk>

            fNeedsLocation = false;
            bool fNeedsFilePath = true;
            const xml::ElementNode *pelmImage;
            if ((pelmImage = elmMedium.findChildElement("VirtualDiskImage")))
                med.strFormat = "VDI";
            else if ((pelmImage = elmMedium.findChildElement("VMDKImage")))
                med.strFormat = "VMDK";
            else if ((pelmImage = elmMedium.findChildElement("VHDImage")))
                med.strFormat = "VHD";
            else if ((pelmImage = elmMedium.findChildElement("ISCSIHardDisk")))
            {
                med.strFormat = "iSCSI";

                fNeedsFilePath = false;
                // location is special here: current settings specify an "iscsi://user@server:port/target/lun"
                // string for the location and also have several disk properties for these, whereas this used
                // to be hidden in several sub-elements before 1.4, so compose a location string and set up
                // the properties:
                med.strLocation = "iscsi://";
                Utf8Str strUser, strServer, strPort, strTarget, strLun;
                if (pelmImage->getAttributeValue("userName", strUser))
                {
                    med.strLocation.append(strUser);
                    med.strLocation.append("@");
                }
                Utf8Str strServerAndPort;
                if (pelmImage->getAttributeValue("server", strServer))
                {
                    strServerAndPort = strServer;
                }
                if (pelmImage->getAttributeValue("port", strPort))
                {
                    if (strServerAndPort.length())
                        strServerAndPort.append(":");
                    strServerAndPort.append(strPort);
                }
                med.strLocation.append(strServerAndPort);
                if (pelmImage->getAttributeValue("target", strTarget))
                {
                    med.strLocation.append("/");
                    med.strLocation.append(strTarget);
                }
                if (pelmImage->getAttributeValue("lun", strLun))
                {
                    med.strLocation.append("/");
                    med.strLocation.append(strLun);
                }

                if (strServer.length() && strPort.length())
                    med.properties["TargetAddress"] = strServerAndPort;
                if (strTarget.length())
                    med.properties["TargetName"] = strTarget;
                if (strUser.length())
                    med.properties["InitiatorUsername"] = strUser;
                Utf8Str strPassword;
                if (pelmImage->getAttributeValue("password", strPassword))
                    med.properties["InitiatorSecret"] = strPassword;
                if (strLun.length())
                    med.properties["LUN"] = strLun;
            }
            else if ((pelmImage = elmMedium.findChildElement("CustomHardDisk")))
            {
                fNeedsFilePath = false;
                fNeedsLocation = true;
                    // also requires @format attribute, which will be queried below
            }
            else
                throw ConfigFileError(this, &elmMedium, N_("Required %s/VirtualDiskImage element is missing"), elmMedium.getName());

            if (fNeedsFilePath)
            {
                if (!(pelmImage->getAttributeValuePath("filePath", med.strLocation)))
                    throw ConfigFileError(this, &elmMedium, N_("Required %s/@filePath attribute is missing"), elmMedium.getName());
            }
        }

        if (med.strFormat.isEmpty())        // not set with 1.4 format above, or 1.4 Custom format?
            if (!elmMedium.getAttributeValue("format", med.strFormat))
                throw ConfigFileError(this, &elmMedium, N_("Required %s/@format attribute is missing"), elmMedium.getName());

        if (!elmMedium.getAttributeValue("autoReset", med.fAutoReset))
            med.fAutoReset = false;

        Utf8Str strType;
        if (elmMedium.getAttributeValue("type", strType))
        {
            // pre-1.4 used lower case, so make this case-insensitive
            strType.toUpper();
            if (strType == "NORMAL")
                med.hdType = MediumType_Normal;
            else if (strType == "IMMUTABLE")
                med.hdType = MediumType_Immutable;
            else if (strType == "WRITETHROUGH")
                med.hdType = MediumType_Writethrough;
            else if (strType == "SHAREABLE")
                med.hdType = MediumType_Shareable;
            else if (strType == "READONLY")
                med.hdType = MediumType_Readonly;
            else if (strType == "MULTIATTACH")
                med.hdType = MediumType_MultiAttach;
            else
                throw ConfigFileError(this, &elmMedium, N_("HardDisk/@type attribute must be one of Normal, Immutable, Writethrough, Shareable, Readonly or MultiAttach"));
        }
    }
    else
    {
        if (m->sv < SettingsVersion_v1_4)
        {
            // DVD and floppy images before 1.4 had "src" attribute instead of "location"
            if (!elmMedium.getAttributeValue("src", med.strLocation))
                throw ConfigFileError(this, &elmMedium, N_("Required %s/@src attribute is missing"), elmMedium.getName());

            fNeedsLocation = false;
        }

        if (!elmMedium.getAttributeValue("format", med.strFormat))
        {
            // DVD and floppy images before 1.11 had no format attribute. assign the default.
            med.strFormat = "RAW";
        }

        if (t == DVDImage)
            med.hdType = MediumType_Readonly;
        else if (t == FloppyImage)
            med.hdType = MediumType_Writethrough;
    }

    if (fNeedsLocation)
        // current files and 1.4 CustomHardDisk elements must have a location attribute
        if (!elmMedium.getAttributeValue("location", med.strLocation))
            throw ConfigFileError(this, &elmMedium, N_("Required %s/@location attribute is missing"), elmMedium.getName());

    // 3.2 builds added Description as an attribute, read it silently
    // and write it back as an element starting with 5.1.26
    elmMedium.getAttributeValue("Description", med.strDescription);

    xml::NodesLoop nlMediumChildren(elmMedium);
    const xml::ElementNode *pelmMediumChild;
    while ((pelmMediumChild = nlMediumChildren.forAllNodes()))
    {
        if (pelmMediumChild->nameEquals("Description"))
            med.strDescription = pelmMediumChild->getValue();
        else if (pelmMediumChild->nameEquals("Property"))
        {
            // handle medium properties
            Utf8Str strPropName, strPropValue;
            if (   pelmMediumChild->getAttributeValue("name", strPropName)
                && pelmMediumChild->getAttributeValue("value", strPropValue) )
                med.properties[strPropName] = strPropValue;
            else
                throw ConfigFileError(this, pelmMediumChild, N_("Required HardDisk/Property/@name or @value attribute is missing"));
        }
    }
}

/**
 * Reads a media registry entry from the main VirtualBox.xml file and
 * likewise for all children where applicable.
 *
 * @param t
 * @param elmMedium
 * @param med
 */
void ConfigFileBase::readMedium(MediaType t,
                                const xml::ElementNode &elmMedium,
                                Medium &med)
{
    std::list<const xml::ElementNode *> llElementsTodo;
    llElementsTodo.push_back(&elmMedium);
    std::list<Medium *> llSettingsTodo;
    llSettingsTodo.push_back(&med);
    std::list<uint32_t> llDepthsTodo;
    llDepthsTodo.push_back(1);

    while (llElementsTodo.size() > 0)
    {
        const xml::ElementNode *pElement = llElementsTodo.front();
        llElementsTodo.pop_front();
        Medium *pMed = llSettingsTodo.front();
        llSettingsTodo.pop_front();
        uint32_t depth = llDepthsTodo.front();
        llDepthsTodo.pop_front();

        if (depth > SETTINGS_MEDIUM_DEPTH_MAX)
            throw ConfigFileError(this, pElement, N_("Maximum medium tree depth of %u exceeded"), SETTINGS_MEDIUM_DEPTH_MAX);

        readMediumOne(t, *pElement, *pMed);

        if (t != HardDisk)
            return;

        // load all children
        xml::NodesLoop nl2(*pElement, m->sv >= SettingsVersion_v1_4 ? "HardDisk" : "DiffHardDisk");
        const xml::ElementNode *pelmHDChild;
        while ((pelmHDChild = nl2.forAllNodes()))
        {
            llElementsTodo.push_back(pelmHDChild);
            pMed->llChildren.push_back(Medium::Empty);
            llSettingsTodo.push_back(&pMed->llChildren.back());
            llDepthsTodo.push_back(depth + 1);
        }
    }
}

/**
 * Reads in the entire \<MediaRegistry\> chunk and stores its media in the lists
 * of the given MediaRegistry structure.
 *
 * This is used in both MainConfigFile and MachineConfigFile since starting with
 * VirtualBox 4.0, we can have media registries in both.
 *
 * For pre-1.4 files, this gets called with the \<DiskRegistry\> chunk instead.
 *
 * @param   elmMediaRegistry
 * @param   mr
 */
void ConfigFileBase::readMediaRegistry(const xml::ElementNode &elmMediaRegistry,
                                       MediaRegistry &mr)
{
    xml::NodesLoop nl1(elmMediaRegistry);
    const xml::ElementNode *pelmChild1;
    while ((pelmChild1 = nl1.forAllNodes()))
    {
        MediaType t = Error;
        if (pelmChild1->nameEquals("HardDisks"))
            t = HardDisk;
        else if (pelmChild1->nameEquals("DVDImages"))
            t = DVDImage;
        else if (pelmChild1->nameEquals("FloppyImages"))
            t = FloppyImage;
        else
            continue;

        xml::NodesLoop nl2(*pelmChild1);
        const xml::ElementNode *pelmMedium;
        while ((pelmMedium = nl2.forAllNodes()))
        {
            if (   t == HardDisk
                && (pelmMedium->nameEquals("HardDisk")))
            {
                mr.llHardDisks.push_back(Medium::Empty);
                readMedium(t, *pelmMedium, mr.llHardDisks.back());
            }
            else if (   t == DVDImage
                     && (pelmMedium->nameEquals("Image")))
            {
                mr.llDvdImages.push_back(Medium::Empty);
                readMedium(t, *pelmMedium, mr.llDvdImages.back());
            }
            else if (   t == FloppyImage
                     && (pelmMedium->nameEquals("Image")))
            {
                mr.llFloppyImages.push_back(Medium::Empty);
                readMedium(t, *pelmMedium, mr.llFloppyImages.back());
            }
        }
    }
}

/**
 * This is common version for reading NAT port forward rule in per-_machine's_adapter_ and
 * per-network approaches.
 * Note: this function doesn't in fill given list from xml::ElementNodesList, because there is conflicting
 * declaration in ovmfreader.h.
 */
void ConfigFileBase::readNATForwardRulesMap(const xml::ElementNode &elmParent, NATRulesMap &mapRules)
{
    xml::ElementNodesList plstRules;
    elmParent.getChildElements(plstRules, "Forwarding");
    for (xml::ElementNodesList::iterator pf = plstRules.begin(); pf != plstRules.end(); ++pf)
    {
        NATRule rule;
        uint32_t port = 0;
        (*pf)->getAttributeValue("name", rule.strName);
        (*pf)->getAttributeValue("proto", (uint32_t&)rule.proto);
        (*pf)->getAttributeValue("hostip", rule.strHostIP);
        (*pf)->getAttributeValue("hostport", port);
        rule.u16HostPort = (uint16_t)port;
        (*pf)->getAttributeValue("guestip", rule.strGuestIP);
        (*pf)->getAttributeValue("guestport", port);
        rule.u16GuestPort = (uint16_t)port;
        mapRules.insert(std::make_pair(rule.strName, rule));
    }
}

void ConfigFileBase::readNATLoopbacks(const xml::ElementNode &elmParent, NATLoopbackOffsetList &llLoopbacks)
{
    xml::ElementNodesList plstLoopbacks;
    elmParent.getChildElements(plstLoopbacks, "Loopback4");
    for (xml::ElementNodesList::iterator lo = plstLoopbacks.begin();
         lo != plstLoopbacks.end(); ++lo)
    {
        NATHostLoopbackOffset loopback;
        (*lo)->getAttributeValue("address", loopback.strLoopbackHostAddress);
        (*lo)->getAttributeValue("offset", (uint32_t&)loopback.u32Offset);
        llLoopbacks.push_back(loopback);
    }
}


/**
 * Adds a "version" attribute to the given XML element with the
 * VirtualBox settings version (e.g. "1.10-linux"). Used by
 * the XML format for the root element and by the OVF export
 * for the vbox:Machine element.
 * @param elm
 */
void ConfigFileBase::setVersionAttribute(xml::ElementNode &elm)
{
    const char *pcszVersion = NULL;
    switch (m->sv)
    {
        case SettingsVersion_v1_8:
            pcszVersion = "1.8";
            break;

        case SettingsVersion_v1_9:
            pcszVersion = "1.9";
            break;

        case SettingsVersion_v1_10:
            pcszVersion = "1.10";
            break;

        case SettingsVersion_v1_11:
            pcszVersion = "1.11";
            break;

        case SettingsVersion_v1_12:
            pcszVersion = "1.12";
            break;

        case SettingsVersion_v1_13:
            pcszVersion = "1.13";
            break;

        case SettingsVersion_v1_14:
            pcszVersion = "1.14";
            break;

        case SettingsVersion_v1_15:
            pcszVersion = "1.15";
            break;

        case SettingsVersion_v1_16:
            pcszVersion = "1.16";
            break;

        case SettingsVersion_v1_17:
            pcszVersion = "1.17";
            break;

        case SettingsVersion_v1_18:
            pcszVersion = "1.18";
            break;

        case SettingsVersion_v1_19:
            pcszVersion = "1.19";
            break;

        case SettingsVersion_v1_20:
            pcszVersion = "1.20";
            break;

        default:
            // catch human error: the assertion below will trigger in debug
            // or dbgopt builds, so hopefully this will get noticed sooner in
            // the future, because it's easy to forget top update something.
            AssertMsg(m->sv <= SettingsVersion_v1_7, ("Settings.cpp: unexpected settings version %d, unhandled future version?\n", m->sv));
            // silently upgrade if this is less than 1.7 because that's the oldest we can write
            if (m->sv <= SettingsVersion_v1_7)
            {
                pcszVersion = "1.7";
                m->sv = SettingsVersion_v1_7;
            }
            else
            {
                // This is reached for SettingsVersion_Future and forgotten
                // settings version after SettingsVersion_v1_7, which should
                // not happen (see assertion above). Set the version to the
                // latest known version, to minimize loss of information, but
                // as we can't predict the future we have to use some format
                // we know, and latest should be the best choice. Note that
                // for "forgotten settings" this may not be the best choice,
                // but as it's an omission of someone who changed this file
                // it's the only generic possibility.
                pcszVersion = "1.20";
                m->sv = SettingsVersion_v1_20;
            }
            break;
    }

    m->strSettingsVersionFull = Utf8StrFmt("%s-%s",
                                           pcszVersion,
                                           VBOX_XML_PLATFORM);  // e.g. "linux"
    elm.setAttribute("version", m->strSettingsVersionFull);
}


/**
 * Creates a special backup file in case there is a version
 * bump, so that it is possible to go back to the previous
 * state. This is done only once (not for every settings
 * version bump), when the settings version is newer than
 * the version read from the config file. Must be called
 * before ConfigFileBase::createStubDocument, because that
 * method may alter information which this method needs.
 */
void ConfigFileBase::specialBackupIfFirstBump()
{
    // Since this gets called before the XML document is actually written out,
    // this is where we must check whether we're upgrading the settings version
    // and need to make a backup, so the user can go back to an earlier
    // VirtualBox version and recover his old settings files.
    if (    (m->svRead != SettingsVersion_Null)     // old file exists?
         && (m->svRead < m->sv)                     // we're upgrading?
       )
    {
        // compose new filename: strip off trailing ".xml"/".vbox"
        Utf8Str strFilenameNew;
        Utf8Str strExt = ".xml";
        if (m->strFilename.endsWith(".xml"))
            strFilenameNew = m->strFilename.substr(0, m->strFilename.length() - 4);
        else if (m->strFilename.endsWith(".vbox"))
        {
            strFilenameNew = m->strFilename.substr(0, m->strFilename.length() - 5);
            strExt = ".vbox";
        }

        // and append something like "-1.3-linux.xml"
        strFilenameNew.append("-");
        strFilenameNew.append(m->strSettingsVersionFull);       // e.g. "1.3-linux"
        strFilenameNew.append(strExt);                          // .xml for main config, .vbox for machine config

        // Copying the file cannot be avoided, as doing tricks with renaming
        // causes trouble on OS X with aliases (which follow the rename), and
        // on all platforms there is a risk of "losing" the VM config when
        // running out of space, as a rename here couldn't be rolled back.
        // Ignoring all errors besides running out of space is intentional, as
        // we don't want to do anything if the file already exists.
        int vrc = RTFileCopy(m->strFilename.c_str(), strFilenameNew.c_str());
        if (RT_UNLIKELY(vrc == VERR_DISK_FULL))
            throw ConfigFileError(this, NULL, N_("Cannot create settings backup file when upgrading to a newer settings format"));

        // do this only once
        m->svRead = SettingsVersion_Null;
    }
}

/**
 * Creates a new stub xml::Document in the m->pDoc member with the
 * root "VirtualBox" element set up. This is used by both
 * MainConfigFile and MachineConfigFile at the beginning of writing
 * out their XML.
 *
 * Before calling this, it is the responsibility of the caller to
 * set the "sv" member to the required settings version that is to
 * be written. For newly created files, the settings version will be
 * recent (1.12 or later if necessary); for files read in from disk
 * earlier, it will be the settings version indicated in the file.
 * However, this method will silently make sure that the settings
 * version is always at least 1.7 and change it if necessary, since
 * there is no write support for earlier settings versions.
 */
void ConfigFileBase::createStubDocument()
{
    Assert(m->pDoc == NULL);
    m->pDoc = new xml::Document;

    m->pelmRoot = m->pDoc->createRootElement("VirtualBox",
                                             "\n"
                                             "** DO NOT EDIT THIS FILE.\n"
                                             "** If you make changes to this file while any VirtualBox related application\n"
                                             "** is running, your changes will be overwritten later, without taking effect.\n"
                                             "** Use VBoxManage or the VirtualBox Manager GUI to make changes.\n"
);
    m->pelmRoot->setAttribute("xmlns", VBOX_XML_NAMESPACE);
    // Have the code for producing a proper schema reference. Not used by most
    // tools, so don't bother doing it. The schema is not on the server anyway.
#ifdef VBOX_WITH_SETTINGS_SCHEMA
    m->pelmRoot->setAttribute("xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance");
    m->pelmRoot->setAttribute("xsi:schemaLocation", VBOX_XML_NAMESPACE " " VBOX_XML_SCHEMA);
#endif

    // add settings version attribute to root element, update m->strSettingsVersionFull
    setVersionAttribute(*m->pelmRoot);

    LogRel(("Saving settings file \"%s\" with version \"%s\"\n", m->strFilename.c_str(), m->strSettingsVersionFull.c_str()));
}

/**
 * Creates an \<ExtraData\> node under the given parent element with
 * \<ExtraDataItem\> childern according to the contents of the given
 * map.
 *
 * This is in ConfigFileBase because it's used in both MainConfigFile
 * and MachineConfigFile, which both can have extradata.
 *
 * @param elmParent
 * @param me
 */
void ConfigFileBase::buildExtraData(xml::ElementNode &elmParent,
                                    const StringsMap &me)
{
    if (me.size())
    {
        xml::ElementNode *pelmExtraData = elmParent.createChild("ExtraData");
        for (StringsMap::const_iterator it = me.begin();
             it != me.end();
             ++it)
        {
            const Utf8Str &strName = it->first;
            const Utf8Str &strValue = it->second;
            xml::ElementNode *pelmThis = pelmExtraData->createChild("ExtraDataItem");
            pelmThis->setAttribute("name", strName);
            pelmThis->setAttribute("value", strValue);
        }
    }
}

/**
 * Creates \<DeviceFilter\> nodes under the given parent element according to
 * the contents of the given USBDeviceFiltersList. This is in ConfigFileBase
 * because it's used in both MainConfigFile (for host filters) and
 * MachineConfigFile (for machine filters).
 *
 * If fHostMode is true, this means that we're supposed to write filters
 * for the IHost interface (respect "action", omit "strRemote" and
 * "ulMaskedInterfaces" in struct USBDeviceFilter).
 *
 * @param elmParent
 * @param ll
 * @param fHostMode
 */
void ConfigFileBase::buildUSBDeviceFilters(xml::ElementNode &elmParent,
                                           const USBDeviceFiltersList &ll,
                                           bool fHostMode)
{
    for (USBDeviceFiltersList::const_iterator it = ll.begin();
         it != ll.end();
         ++it)
    {
        const USBDeviceFilter &flt = *it;
        xml::ElementNode *pelmFilter = elmParent.createChild("DeviceFilter");
        pelmFilter->setAttribute("name", flt.strName);
        pelmFilter->setAttribute("active", flt.fActive);
        if (flt.strVendorId.length())
            pelmFilter->setAttribute("vendorId", flt.strVendorId);
        if (flt.strProductId.length())
            pelmFilter->setAttribute("productId", flt.strProductId);
        if (flt.strRevision.length())
            pelmFilter->setAttribute("revision", flt.strRevision);
        if (flt.strManufacturer.length())
            pelmFilter->setAttribute("manufacturer", flt.strManufacturer);
        if (flt.strProduct.length())
            pelmFilter->setAttribute("product", flt.strProduct);
        if (flt.strSerialNumber.length())
            pelmFilter->setAttribute("serialNumber", flt.strSerialNumber);
        if (flt.strPort.length())
            pelmFilter->setAttribute("port", flt.strPort);

        if (fHostMode)
        {
            const char *pcsz =
                (flt.action == USBDeviceFilterAction_Ignore) ? "Ignore"
                : /*(flt.action == USBDeviceFilterAction_Hold) ?*/ "Hold";
            pelmFilter->setAttribute("action", pcsz);
        }
        else
        {
            if (flt.strRemote.length())
                pelmFilter->setAttribute("remote", flt.strRemote);
            if (flt.ulMaskedInterfaces)
                pelmFilter->setAttribute("maskedInterfaces", flt.ulMaskedInterfaces);
        }
    }
}

/**
 * Creates a single \<HardDisk\> element for the given Medium structure
 * and all child hard disks underneath. Called from MainConfigFile::write().
 *
 * @param t
 * @param elmMedium
 * @param med
 */
void ConfigFileBase::buildMedium(MediaType t,
                                 xml::ElementNode &elmMedium,
                                 const Medium &med)
{
    std::list<const Medium *> llSettingsTodo;
    llSettingsTodo.push_back(&med);
    std::list<xml::ElementNode *> llElementsTodo;
    llElementsTodo.push_back(&elmMedium);
    std::list<uint32_t> llDepthsTodo;
    llDepthsTodo.push_back(1);

    while (llSettingsTodo.size() > 0)
    {
        const Medium *pMed = llSettingsTodo.front();
        llSettingsTodo.pop_front();
        xml::ElementNode *pElement = llElementsTodo.front();
        llElementsTodo.pop_front();
        uint32_t depth = llDepthsTodo.front();
        llDepthsTodo.pop_front();

        if (depth > SETTINGS_MEDIUM_DEPTH_MAX)
            throw ConfigFileError(this, pElement, N_("Maximum medium tree depth of %u exceeded"), SETTINGS_MEDIUM_DEPTH_MAX);

        xml::ElementNode *pelmMedium;

        if (t == HardDisk)
            pelmMedium = pElement->createChild("HardDisk");
        else
            pelmMedium = pElement->createChild("Image");

        pelmMedium->setAttribute("uuid", pMed->uuid.toStringCurly());

        pelmMedium->setAttributePath("location", pMed->strLocation);

        if (t == HardDisk || RTStrICmp(pMed->strFormat.c_str(), "RAW"))
            pelmMedium->setAttribute("format", pMed->strFormat);
        if (   t == HardDisk
            && pMed->fAutoReset)
            pelmMedium->setAttribute("autoReset", pMed->fAutoReset);
        if (pMed->strDescription.length())
            pelmMedium->createChild("Description")->addContent(pMed->strDescription);

        for (StringsMap::const_iterator it = pMed->properties.begin();
             it != pMed->properties.end();
             ++it)
        {
            xml::ElementNode *pelmProp = pelmMedium->createChild("Property");
            pelmProp->setAttribute("name", it->first);
            pelmProp->setAttribute("value", it->second);
        }

        // only for base hard disks, save the type
        if (depth == 1)
        {
            // no need to save the usual DVD/floppy medium types
            if (   (   t != DVDImage
                    || (   pMed->hdType != MediumType_Writethrough // shouldn't happen
                        && pMed->hdType != MediumType_Readonly))
                && (   t != FloppyImage
                    || pMed->hdType != MediumType_Writethrough))
            {
                const char *pcszType =
                    pMed->hdType == MediumType_Normal ? "Normal" :
                    pMed->hdType == MediumType_Immutable ? "Immutable" :
                    pMed->hdType == MediumType_Writethrough ? "Writethrough" :
                    pMed->hdType == MediumType_Shareable ? "Shareable" :
                    pMed->hdType == MediumType_Readonly ? "Readonly" :
                    pMed->hdType == MediumType_MultiAttach ? "MultiAttach" :
                    "INVALID";
                pelmMedium->setAttribute("type", pcszType);
            }
        }

        /* save all children */
        MediaList::const_iterator itBegin = pMed->llChildren.begin();
        MediaList::const_iterator itEnd = pMed->llChildren.end();
        for (MediaList::const_iterator it = itBegin; it != itEnd; ++it)
        {
            llSettingsTodo.push_back(&*it);
            llElementsTodo.push_back(pelmMedium);
            llDepthsTodo.push_back(depth + 1);
        }
    }
}

/**
 * Creates a \<MediaRegistry\> node under the given parent and writes out all
 * hard disks and DVD and floppy images from the lists in the given MediaRegistry
 * structure under it.
 *
 * This is used in both MainConfigFile and MachineConfigFile since starting with
 * VirtualBox 4.0, we can have media registries in both.
 *
 * @param elmParent
 * @param mr
 */
void ConfigFileBase::buildMediaRegistry(xml::ElementNode &elmParent,
                                        const MediaRegistry &mr)
{
    if (mr.llHardDisks.size() == 0 && mr.llDvdImages.size() == 0 && mr.llFloppyImages.size() == 0)
        return;

    xml::ElementNode *pelmMediaRegistry = elmParent.createChild("MediaRegistry");

    if (mr.llHardDisks.size())
    {
        xml::ElementNode *pelmHardDisks = pelmMediaRegistry->createChild("HardDisks");
        for (MediaList::const_iterator it = mr.llHardDisks.begin();
             it != mr.llHardDisks.end();
             ++it)
        {
            buildMedium(HardDisk, *pelmHardDisks, *it);
        }
    }

    if (mr.llDvdImages.size())
    {
        xml::ElementNode *pelmDVDImages = pelmMediaRegistry->createChild("DVDImages");
        for (MediaList::const_iterator it = mr.llDvdImages.begin();
             it != mr.llDvdImages.end();
             ++it)
        {
            buildMedium(DVDImage, *pelmDVDImages, *it);
        }
    }

    if (mr.llFloppyImages.size())
    {
        xml::ElementNode *pelmFloppyImages = pelmMediaRegistry->createChild("FloppyImages");
        for (MediaList::const_iterator it = mr.llFloppyImages.begin();
             it != mr.llFloppyImages.end();
             ++it)
        {
            buildMedium(FloppyImage, *pelmFloppyImages, *it);
        }
    }
}

/**
 * Serialize NAT port-forwarding rules in parent container.
 * Note: it's responsibility of caller to create parent of the list tag.
 * because this method used for serializing per-_mahine's_adapter_ and per-network approaches.
 */
void ConfigFileBase::buildNATForwardRulesMap(xml::ElementNode &elmParent, const NATRulesMap &mapRules)
{
    for (NATRulesMap::const_iterator r = mapRules.begin();
         r != mapRules.end(); ++r)
    {
        xml::ElementNode *pelmPF;
        pelmPF = elmParent.createChild("Forwarding");
        const NATRule &nr = r->second;
        if (nr.strName.length())
            pelmPF->setAttribute("name", nr.strName);
        pelmPF->setAttribute("proto", nr.proto);
        if (nr.strHostIP.length())
            pelmPF->setAttribute("hostip", nr.strHostIP);
        if (nr.u16HostPort)
            pelmPF->setAttribute("hostport", nr.u16HostPort);
        if (nr.strGuestIP.length())
            pelmPF->setAttribute("guestip", nr.strGuestIP);
        if (nr.u16GuestPort)
            pelmPF->setAttribute("guestport", nr.u16GuestPort);
    }
}


void ConfigFileBase::buildNATLoopbacks(xml::ElementNode &elmParent, const NATLoopbackOffsetList &natLoopbackOffsetList)
{
    for (NATLoopbackOffsetList::const_iterator lo = natLoopbackOffsetList.begin();
         lo != natLoopbackOffsetList.end(); ++lo)
    {
        xml::ElementNode *pelmLo;
        pelmLo = elmParent.createChild("Loopback4");
        pelmLo->setAttribute("address", (*lo).strLoopbackHostAddress);
        pelmLo->setAttribute("offset", (*lo).u32Offset);
    }
}

/**
 * Cleans up memory allocated by the internal XML parser. To be called by
 * descendant classes when they're done analyzing the DOM tree to discard it.
 */
void ConfigFileBase::clearDocument()
{
    m->cleanup();
}

/**
 * Returns true only if the underlying config file exists on disk;
 * either because the file has been loaded from disk, or it's been written
 * to disk, or both.
 * @return
 */
bool ConfigFileBase::fileExists()
{
    return m->fFileExists;
}

/**
 * Returns the settings file version
 *
 * @returns Settings file version enum.
 */
SettingsVersion_T ConfigFileBase::getSettingsVersion()
{
    return m->sv;
}


/**
 * Copies the base variables from another instance. Used by Machine::saveSettings
 * so that the settings version does not get lost when a copy of the Machine settings
 * file is made to see if settings have actually changed.
 * @param b
 */
void ConfigFileBase::copyBaseFrom(const ConfigFileBase &b)
{
    m->copyFrom(*b.m);
}

////////////////////////////////////////////////////////////////////////////////
//
// Structures shared between Machine XML and VirtualBox.xml
//
////////////////////////////////////////////////////////////////////////////////


/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
USBDeviceFilter::USBDeviceFilter() :
    fActive(false),
    action(USBDeviceFilterAction_Null),
    ulMaskedInterfaces(0)
{
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool USBDeviceFilter::operator==(const USBDeviceFilter &u) const
{
    return (this == &u)
        || (   strName           == u.strName
            && fActive           == u.fActive
            && strVendorId       == u.strVendorId
            && strProductId      == u.strProductId
            && strRevision       == u.strRevision
            && strManufacturer   == u.strManufacturer
            && strProduct        == u.strProduct
            && strSerialNumber   == u.strSerialNumber
            && strPort           == u.strPort
            && action            == u.action
            && strRemote         == u.strRemote
            && ulMaskedInterfaces == u.ulMaskedInterfaces);
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
settings::Medium::Medium() :
    fAutoReset(false),
    hdType(MediumType_Normal)
{
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool settings::Medium::operator==(const settings::Medium &m) const
{
    return (this == &m)
        || (   uuid == m.uuid
            && strLocation == m.strLocation
            && strDescription == m.strDescription
            && strFormat == m.strFormat
            && fAutoReset == m.fAutoReset
            && properties == m.properties
            && hdType == m.hdType
            && llChildren == m.llChildren);         // this is deep and recurses
}

const struct settings::Medium settings::Medium::Empty; /* default ctor is OK */

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool MediaRegistry::operator==(const MediaRegistry &m) const
{
    return (this == &m)
        || (   llHardDisks == m.llHardDisks
            && llDvdImages == m.llDvdImages
            && llFloppyImages == m.llFloppyImages);
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
NATRule::NATRule() :
    proto(NATProtocol_TCP),
    u16HostPort(0),
    u16GuestPort(0)
{
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool NATRule::operator==(const NATRule &r) const
{
    return (this == &r)
        || (   strName == r.strName
            && proto == r.proto
            && u16HostPort == r.u16HostPort
            && strHostIP == r.strHostIP
            && u16GuestPort == r.u16GuestPort
            && strGuestIP == r.strGuestIP);
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
NATHostLoopbackOffset::NATHostLoopbackOffset() :
    u32Offset(0)
{
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool NATHostLoopbackOffset::operator==(const NATHostLoopbackOffset &o) const
{
    return (this == &o)
        || (   strLoopbackHostAddress == o.strLoopbackHostAddress
            && u32Offset == o.u32Offset);
}


////////////////////////////////////////////////////////////////////////////////
//
// VirtualBox.xml structures
//
////////////////////////////////////////////////////////////////////////////////

PlatformProperties::PlatformProperties()
    : fExclusiveHwVirt(true)
{
#if defined(RT_OS_DARWIN) || defined(RT_OS_WINDOWS) || defined(RT_OS_SOLARIS)
    fExclusiveHwVirt = false; /** BUGBUG Does this apply to MacOS on ARM as well? */
#endif
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
SystemProperties::SystemProperties()
    : uProxyMode(ProxyMode_System)
    , uLogHistoryCount(3)
{
}

#ifdef VBOX_WITH_UPDATE_AGENT
UpdateAgent::UpdateAgent()
   : fEnabled(false)
   , enmChannel(UpdateChannel_Stable)
   , uCheckFreqSeconds(RT_SEC_1DAY)
   , uCheckCount(0)
{
}
#endif /* VBOX_WITH_UPDATE_AGENT */

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
DhcpOptValue::DhcpOptValue()
    : strValue()
    , enmEncoding(DHCPOptionEncoding_Normal)
{
}

/**
 * Non-standard constructor.
 */
DhcpOptValue::DhcpOptValue(const com::Utf8Str &aText, DHCPOptionEncoding_T aEncoding)
    : strValue(aText)
    , enmEncoding(aEncoding)
{
}

/**
 * Default constructor.
 */
DHCPGroupCondition::DHCPGroupCondition()
    : fInclusive(true)
    , enmType(DHCPGroupConditionType_MAC)
    , strValue()
{
}

/**
 * Default constructor.
 */
DHCPConfig::DHCPConfig()
    : mapOptions()
    , secMinLeaseTime(0)
    , secDefaultLeaseTime(0)
    , secMaxLeaseTime(0)
{
}

/**
 * Default constructor.
 */
DHCPGroupConfig::DHCPGroupConfig()
    : DHCPConfig()
    , strName()
    , vecConditions()
{
}

/**
 * Default constructor.
 */
DHCPIndividualConfig::DHCPIndividualConfig()
    : DHCPConfig()
    , strMACAddress()
    , strVMName()
    , uSlot(0)
{
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
DHCPServer::DHCPServer()
    : fEnabled(false)
{
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
NATNetwork::NATNetwork() :
    fEnabled(true),
    fIPv6Enabled(false),
    fAdvertiseDefaultIPv6Route(false),
    fNeedDhcpServer(true),
    u32HostLoopback6Offset(0)
{
}

#ifdef VBOX_WITH_VMNET
/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
HostOnlyNetwork::HostOnlyNetwork() :
    strNetworkMask("255.255.255.0"),
    strIPLower("192.168.56.1"),
    strIPUpper("192.168.56.199"),
    fEnabled(true)
{
    uuid.create();
}
#endif /* VBOX_WITH_VMNET */

#ifdef VBOX_WITH_CLOUD_NET
/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
CloudNetwork::CloudNetwork() :
    strProviderShortName("OCI"),
    strProfileName("Default"),
    fEnabled(true)
{
}
#endif /* VBOX_WITH_CLOUD_NET */



////////////////////////////////////////////////////////////////////////////////
//
// MainConfigFile
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Reads one \<MachineEntry\> from the main VirtualBox.xml file.
 * @param elmMachineRegistry
 */
void MainConfigFile::readMachineRegistry(const xml::ElementNode &elmMachineRegistry)
{
    // <MachineEntry uuid="{ xxx }" src="   xxx "/>
    xml::NodesLoop nl1(elmMachineRegistry);
    const xml::ElementNode *pelmChild1;
    while ((pelmChild1 = nl1.forAllNodes()))
    {
        if (pelmChild1->nameEquals("MachineEntry"))
        {
            MachineRegistryEntry mre;
            Utf8Str strUUID;
            if (   pelmChild1->getAttributeValue("uuid", strUUID)
                && pelmChild1->getAttributeValue("src", mre.strSettingsFile) )
            {
                parseUUID(mre.uuid, strUUID, pelmChild1);
                llMachines.push_back(mre);
            }
            else
                throw ConfigFileError(this, pelmChild1, N_("Required MachineEntry/@uuid or @src attribute is missing"));
        }
    }
}

/**
 * Builds the XML tree for the DHCP servers.
 */
void MainConfigFile::buildDHCPServers(xml::ElementNode &elmDHCPServers, DHCPServersList const &ll)
{
    for (DHCPServersList::const_iterator it = ll.begin(); it != ll.end(); ++it)
    {
        const DHCPServer &srv = *it;
        xml::ElementNode *pElmThis = elmDHCPServers.createChild("DHCPServer");

        pElmThis->setAttribute("networkName", srv.strNetworkName);
        pElmThis->setAttribute("IPAddress", srv.strIPAddress);
        DhcpOptConstIterator itOpt = srv.globalConfig.mapOptions.find(DHCPOption_SubnetMask);
        if (itOpt != srv.globalConfig.mapOptions.end())
            pElmThis->setAttribute("networkMask", itOpt->second.strValue);
        pElmThis->setAttribute("lowerIP", srv.strIPLower);
        pElmThis->setAttribute("upperIP", srv.strIPUpper);
        pElmThis->setAttribute("enabled", (srv.fEnabled) ? 1 : 0);        // too bad we chose 1 vs. 0 here

        /* We don't want duplicate validation check of networkMask here*/
        if (srv.globalConfig.mapOptions.size() > (itOpt != srv.globalConfig.mapOptions.end() ? 1U : 0U))
        {
            xml::ElementNode *pElmOptions = pElmThis->createChild("Options");
            buildDHCPOptions(*pElmOptions, srv.globalConfig, true);
        }

        for (DHCPGroupConfigVec::const_iterator itGroup = srv.vecGroupConfigs.begin();
             itGroup != srv.vecGroupConfigs.end(); ++itGroup)
        {
            DHCPGroupConfig const &rGroupConfig = *itGroup;

            xml::ElementNode *pElmGroup = pElmThis->createChild("Group");
            pElmGroup->setAttribute("name", rGroupConfig.strName);
            buildDHCPOptions(*pElmGroup, rGroupConfig, false);

            for (DHCPGroupConditionVec::const_iterator itCond = rGroupConfig.vecConditions.begin();
                 itCond != rGroupConfig.vecConditions.end(); ++itCond)
            {
                xml::ElementNode *pElmCondition = pElmGroup->createChild("Condition");
                pElmCondition->setAttribute("inclusive", itCond->fInclusive);
                pElmCondition->setAttribute("type", (int32_t)itCond->enmType);
                pElmCondition->setAttribute("value", itCond->strValue);
            }
        }

        for (DHCPIndividualConfigMap::const_iterator itHost = srv.mapIndividualConfigs.begin();
             itHost != srv.mapIndividualConfigs.end(); ++itHost)
        {
            DHCPIndividualConfig const &rIndividualConfig = itHost->second;

            xml::ElementNode *pElmConfig = pElmThis->createChild("Config");
            if (rIndividualConfig.strMACAddress.isNotEmpty())
                pElmConfig->setAttribute("MACAddress", rIndividualConfig.strMACAddress);
            if (rIndividualConfig.strVMName.isNotEmpty())
                pElmConfig->setAttribute("vm-name", rIndividualConfig.strVMName);
            if (rIndividualConfig.uSlot != 0 || rIndividualConfig.strVMName.isNotEmpty())
                pElmConfig->setAttribute("slot", rIndividualConfig.uSlot);
            if (rIndividualConfig.strFixedAddress.isNotEmpty())
                pElmConfig->setAttribute("fixedAddress", rIndividualConfig.strFixedAddress);
            buildDHCPOptions(*pElmConfig, rIndividualConfig, false);
        }
     }
}

/**
 * Worker for buildDHCPServers() that builds Options or Config element trees.
 */
void MainConfigFile::buildDHCPOptions(xml::ElementNode &elmOptions, DHCPConfig const &rConfig, bool fSkipSubnetMask)
{
    /* Generic (and optional) attributes on the Options or Config element: */
    if (rConfig.secMinLeaseTime > 0)
        elmOptions.setAttribute("secMinLeaseTime", rConfig.secMinLeaseTime);
    if (rConfig.secDefaultLeaseTime > 0)
        elmOptions.setAttribute("secDefaultLeaseTime", rConfig.secDefaultLeaseTime);
    if (rConfig.secMaxLeaseTime > 0)
        elmOptions.setAttribute("secMaxLeaseTime", rConfig.secMaxLeaseTime);
    if (rConfig.strForcedOptions.isNotEmpty())
        elmOptions.setAttribute("forcedOptions", rConfig.strForcedOptions);
    if (rConfig.strSuppressedOptions.isNotEmpty())
        elmOptions.setAttribute("suppressedOptions", rConfig.strSuppressedOptions);

    /* The DHCP options are <Option> child elements: */
    for (DhcpOptConstIterator it = rConfig.mapOptions.begin(); it != rConfig.mapOptions.end(); ++it)
        if (it->first != DHCPOption_SubnetMask || !fSkipSubnetMask)
        {
            xml::ElementNode *pElmOption = elmOptions.createChild("Option");
            pElmOption->setAttribute("name", it->first);
            pElmOption->setAttribute("value", it->second.strValue);
            if (it->second.enmEncoding != DHCPOptionEncoding_Normal)
                pElmOption->setAttribute("encoding", (int32_t)it->second.enmEncoding);
        }
}

/**
 * Reads in the \<DHCPServers\> chunk.
 * @param elmDHCPServers
 */
void MainConfigFile::readDHCPServers(const xml::ElementNode &elmDHCPServers)
{
    xml::NodesLoop nl1(elmDHCPServers);
    const xml::ElementNode *pelmServer;
    while ((pelmServer = nl1.forAllNodes()))
    {
        if (pelmServer->nameEquals("DHCPServer"))
        {
            DHCPServer srv;
            if (   pelmServer->getAttributeValue("networkName", srv.strNetworkName)
                && pelmServer->getAttributeValue("IPAddress", srv.strIPAddress)
                && pelmServer->getAttributeValue("networkMask", srv.globalConfig.mapOptions[DHCPOption_SubnetMask].strValue)
                && pelmServer->getAttributeValue("lowerIP", srv.strIPLower)
                && pelmServer->getAttributeValue("upperIP", srv.strIPUpper)
                && pelmServer->getAttributeValue("enabled", srv.fEnabled) )
            {
                /* Global options: */
                const xml::ElementNode *pElmOptions;
                xml::NodesLoop          nlOptions(*pelmServer, "Options");
                while ((pElmOptions = nlOptions.forAllNodes()) != NULL) /** @todo this loop makes no sense, there can only be one \<Options\> child. */
                    readDHCPOptions(srv.globalConfig, *pElmOptions, true /*fIgnoreSubnetMask*/);

                /* Group configurations: */
                xml::NodesLoop nlGroup(*pelmServer, "Group");
                const xml::ElementNode *pElmGroup;
                size_t i = 0;
                while ((pElmGroup = nlGroup.forAllNodes()) != NULL)
                {
                    srv.vecGroupConfigs.push_back(DHCPGroupConfig());
                    DHCPGroupConfig &rGroupConfig = srv.vecGroupConfigs.back();

                    if (!pElmGroup->getAttributeValue("name", rGroupConfig.strName))
                        rGroupConfig.strName.printf("Unamed Group #%u", ++i);

                    readDHCPOptions(rGroupConfig, *pElmGroup, false /*fIgnoreSubnetMask*/);

                    xml::NodesLoop nlCondition(*pElmGroup, "Condition");
                    const xml::ElementNode *pElmCondition;
                    while ((pElmCondition = nlCondition.forAllNodes()) != NULL)
                    {
                        rGroupConfig.vecConditions.push_back(DHCPGroupCondition());
                        DHCPGroupCondition &rGroupCondition = rGroupConfig.vecConditions.back();

                        if (!pElmCondition->getAttributeValue("inclusive", rGroupCondition.fInclusive))
                            rGroupCondition.fInclusive = true;

                        int32_t iType;
                        if (!pElmCondition->getAttributeValue("type", iType))
                            iType = DHCPGroupConditionType_MAC;
                        rGroupCondition.enmType = (DHCPGroupConditionType_T)iType;

                        pElmCondition->getAttributeValue("value", rGroupCondition.strValue);
                    }
                }

                /* host specific configuration: */
                xml::NodesLoop nlConfig(*pelmServer, "Config");
                const xml::ElementNode *pElmConfig;
                while ((pElmConfig = nlConfig.forAllNodes()) != NULL)
                {
                    com::Utf8Str strMACAddress;
                    if (!pElmConfig->getAttributeValue("MACAddress", strMACAddress))
                        strMACAddress.setNull();

                    com::Utf8Str strVMName;
                    if (!pElmConfig->getAttributeValue("vm-name", strVMName))
                        strVMName.setNull();

                    uint32_t uSlot;
                    if (!pElmConfig->getAttributeValue("slot", uSlot))
                        uSlot = 0;

                    com::Utf8Str strKey;
                    if (strVMName.isNotEmpty())
                        strKey.printf("%s/%u", strVMName.c_str(), uSlot);
                    else
                        strKey.printf("%s/%u", strMACAddress.c_str(), uSlot);

                    DHCPIndividualConfig &rIndividualConfig = srv.mapIndividualConfigs[strKey];
                    rIndividualConfig.strMACAddress = strMACAddress;
                    rIndividualConfig.strVMName     = strVMName;
                    rIndividualConfig.uSlot         = uSlot;
                    pElmConfig->getAttributeValue("fixedAddress", rIndividualConfig.strFixedAddress);

                    readDHCPOptions(rIndividualConfig, *pElmConfig, false /*fIgnoreSubnetMask*/);
                }

                llDhcpServers.push_back(srv);
            }
            else
                throw ConfigFileError(this, pelmServer, N_("Required DHCPServer/@networkName, @IPAddress, @networkMask, @lowerIP, @upperIP or @enabled attribute is missing"));
        }
    }
}

/**
 * Worker for readDHCPServers that reads a configuration, either global,
 * group or host (VM+NIC) specific.
 */
void MainConfigFile::readDHCPOptions(DHCPConfig &rConfig, const xml::ElementNode &elmConfig, bool fIgnoreSubnetMask)
{
    /* Generic (and optional) attributes on the Options or Config element: */
    if (!elmConfig.getAttributeValue("secMinLeaseTime", rConfig.secMinLeaseTime))
        rConfig.secMinLeaseTime = 0;
    if (!elmConfig.getAttributeValue("secDefaultLeaseTime", rConfig.secDefaultLeaseTime))
        rConfig.secDefaultLeaseTime = 0;
    if (!elmConfig.getAttributeValue("secMaxLeaseTime", rConfig.secMaxLeaseTime))
        rConfig.secMaxLeaseTime = 0;
    if (!elmConfig.getAttributeValue("forcedOptions", rConfig.strForcedOptions))
        rConfig.strSuppressedOptions.setNull();
    if (!elmConfig.getAttributeValue("suppressedOptions", rConfig.strSuppressedOptions))
        rConfig.strSuppressedOptions.setNull();

    /* The DHCP options are <Option> child elements: */
    xml::NodesLoop          nl2(elmConfig, "Option");
    const xml::ElementNode *pElmOption;
    while ((pElmOption = nl2.forAllNodes()) != NULL)
    {
        int32_t iOptName;
        if (!pElmOption->getAttributeValue("name", iOptName))
            continue;
        DHCPOption_T OptName = (DHCPOption_T)iOptName;
        if (OptName == DHCPOption_SubnetMask && fIgnoreSubnetMask)
            continue;

        com::Utf8Str strValue;
        pElmOption->getAttributeValue("value", strValue);

        int32_t iOptEnc;
        if (!pElmOption->getAttributeValue("encoding", iOptEnc))
            iOptEnc = DHCPOptionEncoding_Normal;

        rConfig.mapOptions[OptName] = DhcpOptValue(strValue, (DHCPOptionEncoding_T)iOptEnc);
    } /* end of forall("Option") */

}

/**
 * Reads in the \<NATNetworks\> chunk.
 * @param elmNATNetworks
 */
void MainConfigFile::readNATNetworks(const xml::ElementNode &elmNATNetworks)
{
    xml::NodesLoop nl1(elmNATNetworks);
    const xml::ElementNode *pelmNet;
    while ((pelmNet = nl1.forAllNodes()))
    {
        if (pelmNet->nameEquals("NATNetwork"))
        {
            NATNetwork net;
            if (   pelmNet->getAttributeValue("networkName", net.strNetworkName)
                && pelmNet->getAttributeValue("enabled", net.fEnabled)
                && pelmNet->getAttributeValue("network", net.strIPv4NetworkCidr)
                && pelmNet->getAttributeValue("ipv6", net.fIPv6Enabled)
                && pelmNet->getAttributeValue("ipv6prefix", net.strIPv6Prefix)
                && pelmNet->getAttributeValue("advertiseDefaultIPv6Route", net.fAdvertiseDefaultIPv6Route)
                && pelmNet->getAttributeValue("needDhcp", net.fNeedDhcpServer) )
            {
                pelmNet->getAttributeValue("loopback6", net.u32HostLoopback6Offset);
                const xml::ElementNode *pelmMappings;
                if ((pelmMappings = pelmNet->findChildElement("Mappings")))
                    readNATLoopbacks(*pelmMappings, net.llHostLoopbackOffsetList);

                const xml::ElementNode *pelmPortForwardRules4;
                if ((pelmPortForwardRules4 = pelmNet->findChildElement("PortForwarding4")))
                    readNATForwardRulesMap(*pelmPortForwardRules4,
                                           net.mapPortForwardRules4);

                const xml::ElementNode *pelmPortForwardRules6;
                if ((pelmPortForwardRules6 = pelmNet->findChildElement("PortForwarding6")))
                    readNATForwardRulesMap(*pelmPortForwardRules6,
                                           net.mapPortForwardRules6);

                llNATNetworks.push_back(net);
            }
            else
                throw ConfigFileError(this, pelmNet, N_("Required NATNetwork/@networkName, @gateway, @network,@advertiseDefaultIpv6Route , @needDhcp or @enabled attribute is missing"));
        }
    }
}

#ifdef VBOX_WITH_VMNET
/**
 * Reads in the \<HostOnlyNetworks\> chunk.
 * @param elmHostOnlyNetworks
 */
void MainConfigFile::readHostOnlyNetworks(const xml::ElementNode &elmHostOnlyNetworks)
{
    xml::NodesLoop nl1(elmHostOnlyNetworks);
    const xml::ElementNode *pelmNet;
    while ((pelmNet = nl1.forAllNodes()))
    {
        if (pelmNet->nameEquals("HostOnlyNetwork"))
        {
            HostOnlyNetwork net;
            Utf8Str strID;
            if (   pelmNet->getAttributeValue("name", net.strNetworkName)
                && pelmNet->getAttributeValue("mask", net.strNetworkMask)
                && pelmNet->getAttributeValue("ipLower", net.strIPLower)
                && pelmNet->getAttributeValue("ipUpper", net.strIPUpper)
                && pelmNet->getAttributeValue("id", strID)
                && pelmNet->getAttributeValue("enabled", net.fEnabled) )
            {
                parseUUID(net.uuid, strID, pelmNet);
                llHostOnlyNetworks.push_back(net);
            }
            else
                throw ConfigFileError(this, pelmNet, N_("Required HostOnlyNetwork/@name, @mask, @ipLower, @ipUpper, @id or @enabled attribute is missing"));
        }
    }
}
#endif /* VBOX_WITH_VMNET */

#ifdef VBOX_WITH_CLOUD_NET
/**
 * Reads in the \<CloudNetworks\> chunk.
 * @param elmCloudNetworks
 */
void MainConfigFile::readCloudNetworks(const xml::ElementNode &elmCloudNetworks)
{
    xml::NodesLoop nl1(elmCloudNetworks);
    const xml::ElementNode *pelmNet;
    while ((pelmNet = nl1.forAllNodes()))
    {
        if (pelmNet->nameEquals("CloudNetwork"))
        {
            CloudNetwork net;
            if (   pelmNet->getAttributeValue("name", net.strNetworkName)
                && pelmNet->getAttributeValue("provider", net.strProviderShortName)
                && pelmNet->getAttributeValue("profile", net.strProfileName)
                && pelmNet->getAttributeValue("id", net.strNetworkId)
                && pelmNet->getAttributeValue("enabled", net.fEnabled) )
            {
                llCloudNetworks.push_back(net);
            }
            else
                throw ConfigFileError(this, pelmNet, N_("Required CloudNetwork/@name, @provider, @profile, @id or @enabled attribute is missing"));
        }
    }
}
#endif /* VBOX_WITH_CLOUD_NET */

/**
 * Creates \<USBDeviceSource\> nodes under the given parent element according to
 * the contents of the given USBDeviceSourcesList.
 *
 * @param elmParent
 * @param ll
 */
void MainConfigFile::buildUSBDeviceSources(xml::ElementNode &elmParent,
                                           const USBDeviceSourcesList &ll)
{
    for (USBDeviceSourcesList::const_iterator it = ll.begin();
         it != ll.end();
         ++it)
    {
        const USBDeviceSource &src = *it;
        xml::ElementNode *pelmSource = elmParent.createChild("USBDeviceSource");
        pelmSource->setAttribute("name", src.strName);
        pelmSource->setAttribute("backend", src.strBackend);
        pelmSource->setAttribute("address", src.strAddress);

        /* Write the properties. */
        for (StringsMap::const_iterator itProp = src.properties.begin();
             itProp != src.properties.end();
             ++itProp)
        {
            xml::ElementNode *pelmProp = pelmSource->createChild("Property");
            pelmProp->setAttribute("name", itProp->first);
            pelmProp->setAttribute("value", itProp->second);
        }
    }
}

/**
 * Reads \<USBDeviceFilter\> entries from under the given elmDeviceFilters node and
 * stores them in the given linklist. This is in ConfigFileBase because it's used
 * from both MainConfigFile (for host filters) and MachineConfigFile (for machine
 * filters).
 * @param elmDeviceSources
 * @param ll
 */
void MainConfigFile::readUSBDeviceSources(const xml::ElementNode &elmDeviceSources,
                                          USBDeviceSourcesList &ll)
{
    xml::NodesLoop nl1(elmDeviceSources, "USBDeviceSource");
    const xml::ElementNode *pelmChild;
    while ((pelmChild = nl1.forAllNodes()))
    {
        USBDeviceSource src;

        if (   pelmChild->getAttributeValue("name", src.strName)
            && pelmChild->getAttributeValue("backend", src.strBackend)
            && pelmChild->getAttributeValue("address", src.strAddress))
        {
            // handle medium properties
            xml::NodesLoop nl2(*pelmChild, "Property");
            const xml::ElementNode *pelmSrcChild;
            while ((pelmSrcChild = nl2.forAllNodes()))
            {
                Utf8Str strPropName, strPropValue;
                if (   pelmSrcChild->getAttributeValue("name", strPropName)
                    && pelmSrcChild->getAttributeValue("value", strPropValue) )
                    src.properties[strPropName] = strPropValue;
                else
                    throw ConfigFileError(this, pelmSrcChild, N_("Required USBDeviceSource/Property/@name or @value attribute is missing"));
            }

            ll.push_back(src);
        }
    }
}

/**
 * Converts old style Proxy settings from ExtraData/UI section.
 *
 * Saves proxy settings directly to systemProperties structure.
 *
 * @returns true if conversion was successfull, false if not.
 * @param strUIProxySettings The GUI settings string to convert.
 */
bool MainConfigFile::convertGuiProxySettings(const com::Utf8Str &strUIProxySettings)
{
    /*
     * Possible variants:
     *    - "ProxyAuto,proxyserver.url,1080,authDisabled,,"
     *    - "ProxyDisabled,proxyserver.url,1080,authDisabled,,"
     *    - "ProxyEnabled,proxyserver.url,1080,authDisabled,,"
     *
     * Note! We only need to bother with the first three fields as the last
     *       three was never really used or ever actually passed to the HTTP
     *       client code.
     */
    /* First field: The proxy mode. */
    const char *psz = RTStrStripL(strUIProxySettings.c_str());
    static const struct { const char *psz; size_t cch; ProxyMode_T enmMode; } s_aModes[] =
    {
        { RT_STR_TUPLE("ProxyAuto"),     ProxyMode_System },
        { RT_STR_TUPLE("ProxyDisabled"), ProxyMode_NoProxy },
        { RT_STR_TUPLE("ProxyEnabled"),  ProxyMode_Manual },
    };
    for (size_t i = 0; i < RT_ELEMENTS(s_aModes); i++)
        if (RTStrNICmpAscii(psz, s_aModes[i].psz, s_aModes[i].cch) == 0)
        {
            systemProperties.uProxyMode = s_aModes[i].enmMode;
            psz = RTStrStripL(psz + s_aModes[i].cch);
            if (*psz == ',')
            {
                /* Second field: The proxy host, possibly fully fledged proxy URL. */
                psz = RTStrStripL(psz + 1);
                if (*psz != '\0' && *psz != ',')
                {
                    const char *pszEnd  = strchr(psz, ',');
                    size_t      cchHost = pszEnd ? (size_t)(pszEnd - psz) : strlen(psz);
                    while (cchHost > 0 && RT_C_IS_SPACE(psz[cchHost - 1]))
                        cchHost--;
                    systemProperties.strProxyUrl.assign(psz, cchHost);
                    if (systemProperties.strProxyUrl.find("://") == RTCString::npos)
                        systemProperties.strProxyUrl.replace(0, 0, "http://");

                    /* Third field: The proxy port. Defaulted to 1080 for all proxies.
                                    The new settings has type specific default ports.  */
                    uint16_t uPort = 1080;
                    if (pszEnd)
                    {
                        int vrc = RTStrToUInt16Ex(RTStrStripL(pszEnd + 1), NULL, 10, &uPort);
                        if (RT_FAILURE(vrc))
                            uPort = 1080;
                    }
                    RTURIPARSED Parsed;
                    int vrc = RTUriParse(systemProperties.strProxyUrl.c_str(), &Parsed);
                    if (RT_SUCCESS(vrc))
                    {
                        if (Parsed.uAuthorityPort == UINT32_MAX)
                            systemProperties.strProxyUrl.appendPrintf(systemProperties.strProxyUrl.endsWith(":")
                                                                      ? "%u" : ":%u", uPort);
                    }
                    else
                    {
                        LogRelFunc(("Dropping invalid proxy URL for %u: %s\n",
                                    systemProperties.uProxyMode, systemProperties.strProxyUrl.c_str()));
                        systemProperties.strProxyUrl.setNull();
                    }
                }
                /* else: don't bother with the rest if we haven't got a host. */
            }
            if (   systemProperties.strProxyUrl.isEmpty()
                && systemProperties.uProxyMode == ProxyMode_Manual)
            {
                systemProperties.uProxyMode = ProxyMode_System;
                return false;
            }
            return true;
        }
    LogRelFunc(("Unknown proxy type: %s\n", psz));
    return false;
}

/**
 * Constructor.
 *
 * If pstrFilename is != NULL, this reads the given settings file into the member
 * variables and various substructures and lists. Otherwise, the member variables
 * are initialized with default values.
 *
 * Throws variants of xml::Error for I/O, XML and logical content errors, which
 * the caller should catch; if this constructor does not throw, then the member
 * variables contain meaningful values (either from the file or defaults).
 *
 * @param pstrFilename
 */
MainConfigFile::MainConfigFile(const Utf8Str *pstrFilename)
    : ConfigFileBase(pstrFilename)
{
    if (pstrFilename)
    {
        // the ConfigFileBase constructor has loaded the XML file, so now
        // we need only analyze what is in there
        xml::NodesLoop nlRootChildren(*m->pelmRoot);
        const xml::ElementNode *pelmRootChild;
        bool fCopyProxySettingsFromExtraData = false;
        while ((pelmRootChild = nlRootChildren.forAllNodes()))
        {
            if (pelmRootChild->nameEquals("Global"))
            {
                xml::NodesLoop nlGlobalChildren(*pelmRootChild);
                const xml::ElementNode *pelmGlobalChild;
                while ((pelmGlobalChild = nlGlobalChildren.forAllNodes()))
                {
                    if (pelmGlobalChild->nameEquals("SystemProperties"))
                    {
                        pelmGlobalChild->getAttributeValue("defaultMachineFolder", systemProperties.strDefaultMachineFolder);
                        pelmGlobalChild->getAttributeValue("LoggingLevel", systemProperties.strLoggingLevel);
                        pelmGlobalChild->getAttributeValue("defaultHardDiskFormat", systemProperties.strDefaultHardDiskFormat);
                        if (!pelmGlobalChild->getAttributeValue("VRDEAuthLibrary", systemProperties.strVRDEAuthLibrary))
                            // pre-1.11 used @remoteDisplayAuthLibrary instead
                            pelmGlobalChild->getAttributeValue("remoteDisplayAuthLibrary", systemProperties.strVRDEAuthLibrary);
                        pelmGlobalChild->getAttributeValue("webServiceAuthLibrary", systemProperties.strWebServiceAuthLibrary);
                        pelmGlobalChild->getAttributeValue("defaultVRDEExtPack", systemProperties.strDefaultVRDEExtPack);
                        pelmGlobalChild->getAttributeValue("defaultCryptoExtPack", systemProperties.strDefaultCryptoExtPack);
                        pelmGlobalChild->getAttributeValue("LogHistoryCount", systemProperties.uLogHistoryCount);
                        pelmGlobalChild->getAttributeValue("autostartDatabasePath", systemProperties.strAutostartDatabasePath);
                        pelmGlobalChild->getAttributeValue("defaultFrontend", systemProperties.strDefaultFrontend);
                        if (m->sv < SettingsVersion_v1_20) /* exclusiveHwVirt was part of SystemProperties for < v1.20. */
                            pelmGlobalChild->getAttributeValue("exclusiveHwVirt", platformProperties.fExclusiveHwVirt);
                        if (!pelmGlobalChild->getAttributeValue("proxyMode", systemProperties.uProxyMode))
                            fCopyProxySettingsFromExtraData = true;
                        pelmGlobalChild->getAttributeValue("proxyUrl", systemProperties.strProxyUrl);
                        pelmGlobalChild->getAttributeValue("LanguageId", systemProperties.strLanguageId);
                    }
                    if (   pelmGlobalChild->nameEquals("PlatformProperties")
                        && m->sv >= SettingsVersion_v1_20)
                    {
                        /* Since settings v1.20 exclusiveHwVirt is part of PlatformProperties. */
                        pelmGlobalChild->getAttributeValue("exclusiveHwVirt", platformProperties.fExclusiveHwVirt);
                    }
#ifdef VBOX_WITH_UPDATE_AGENT
                    else if (pelmGlobalChild->nameEquals("Updates"))
                    {
                        /* We keep the updates configuration as part of the host for now, as the API exposes the IHost::updateHost attribute,
                         * but use an own "Updates" branch in the XML for better structurizing stuff in the future. */
                        UpdateAgent &updateHost = host.updateHost;

                        xml::NodesLoop nlLevel4(*pelmGlobalChild);
                        const xml::ElementNode *pelmLevel4Child;
                        while ((pelmLevel4Child = nlLevel4.forAllNodes()))
                        {
                            if (pelmLevel4Child->nameEquals("Host"))
                            {
                                pelmLevel4Child->getAttributeValue("enabled", updateHost.fEnabled);
                                pelmLevel4Child->getAttributeValue("channel", (uint32_t&)updateHost.enmChannel);
                                pelmLevel4Child->getAttributeValue("checkFreqSec", updateHost.uCheckFreqSeconds);
                                pelmLevel4Child->getAttributeValue("repoUrl", updateHost.strRepoUrl);
                                pelmLevel4Child->getAttributeValue("lastCheckDate", updateHost.strLastCheckDate);
                                pelmLevel4Child->getAttributeValue("checkCount", updateHost.uCheckCount);
                            }
                            /** @todo Add update settings for ExtPack and Guest Additions here later. See @bugref{7983}. */
                        }

                        /* Global enabled switch for updates. Currently bound to host updates, as this is the only update we have so far. */
                        pelmGlobalChild->getAttributeValue("enabled", updateHost.fEnabled);
                    }
#endif
                    else if (pelmGlobalChild->nameEquals("ExtraData"))
                        readExtraData(*pelmGlobalChild, mapExtraDataItems);
                    else if (pelmGlobalChild->nameEquals("MachineRegistry"))
                        readMachineRegistry(*pelmGlobalChild);
                    else if (    (pelmGlobalChild->nameEquals("MediaRegistry"))
                              || (    (m->sv < SettingsVersion_v1_4)
                                   && (pelmGlobalChild->nameEquals("DiskRegistry"))
                                 )
                            )
                        readMediaRegistry(*pelmGlobalChild, mediaRegistry);
                    else if (pelmGlobalChild->nameEquals("NetserviceRegistry"))
                    {
                        xml::NodesLoop nlLevel4(*pelmGlobalChild);
                        const xml::ElementNode *pelmLevel4Child;
                        while ((pelmLevel4Child = nlLevel4.forAllNodes()))
                        {
                            if (pelmLevel4Child->nameEquals("DHCPServers"))
                                readDHCPServers(*pelmLevel4Child);
                            if (pelmLevel4Child->nameEquals("NATNetworks"))
                                readNATNetworks(*pelmLevel4Child);
#ifdef VBOX_WITH_VMNET
                            if (pelmLevel4Child->nameEquals("HostOnlyNetworks"))
                                readHostOnlyNetworks(*pelmLevel4Child);
#endif /* VBOX_WITH_VMNET */
#ifdef VBOX_WITH_CLOUD_NET
                            if (pelmLevel4Child->nameEquals("CloudNetworks"))
                                readCloudNetworks(*pelmLevel4Child);
#endif /* VBOX_WITH_CLOUD_NET */
                        }
                    }
                    else if (pelmGlobalChild->nameEquals("USBDeviceFilters"))
                        readUSBDeviceFilters(*pelmGlobalChild, host.llUSBDeviceFilters);
                    else if (pelmGlobalChild->nameEquals("USBDeviceSources"))
                        readUSBDeviceSources(*pelmGlobalChild, host.llUSBDeviceSources);
                }
            } // end if (pelmRootChild->nameEquals("Global"))
        }

        if (fCopyProxySettingsFromExtraData)
            for (StringsMap::const_iterator it = mapExtraDataItems.begin(); it != mapExtraDataItems.end(); ++it)
                if (it->first.equals("GUI/ProxySettings"))
                {
                    convertGuiProxySettings(it->second);
                    break;
                }

        clearDocument();
    }

    // DHCP servers were introduced with settings version 1.7; if we're loading
    // from an older version OR this is a fresh install, then add one DHCP server
    // with default settings
    if (    (!llDhcpServers.size())
         && (    (!pstrFilename)                    // empty VirtualBox.xml file
              || (m->sv < SettingsVersion_v1_7)     // upgrading from before 1.7
            )
       )
    {
        DHCPServer srv;
#ifdef RT_OS_WINDOWS
        srv.strNetworkName = "HostInterfaceNetworking-VirtualBox Host-Only Ethernet Adapter";
#else
        srv.strNetworkName = "HostInterfaceNetworking-vboxnet0";
#endif
        srv.strIPAddress = "192.168.56.100";
        srv.globalConfig.mapOptions[DHCPOption_SubnetMask] = DhcpOptValue("255.255.255.0");
        srv.strIPLower = "192.168.56.101";
        srv.strIPUpper = "192.168.56.254";
        srv.fEnabled = true;
        llDhcpServers.push_back(srv);
    }
}

void MainConfigFile::bumpSettingsVersionIfNeeded()
{
#ifdef VBOX_WITH_VMNET
    if (m->sv < SettingsVersion_v1_19)
    {
        // VirtualBox 7.0 adds support for host-only networks.
        if (!llHostOnlyNetworks.empty())
            m->sv = SettingsVersion_v1_19;
    }
#endif /* VBOX_WITH_VMNET */
#ifdef VBOX_WITH_CLOUD_NET
    if (m->sv < SettingsVersion_v1_18)
    {
        // VirtualBox 6.1 adds support for cloud networks.
        if (!llCloudNetworks.empty())
            m->sv = SettingsVersion_v1_18;
    }
#endif /* VBOX_WITH_CLOUD_NET */

    if (m->sv < SettingsVersion_v1_16)
    {
        // VirtualBox 5.1 add support for additional USB device sources.
        if (!host.llUSBDeviceSources.empty())
            m->sv = SettingsVersion_v1_16;
    }

    if (m->sv < SettingsVersion_v1_14)
    {
        // VirtualBox 4.3 adds NAT networks.
        if (   !llNATNetworks.empty())
            m->sv = SettingsVersion_v1_14;
    }
}


/**
 * Called from the IVirtualBox interface to write out VirtualBox.xml. This
 * builds an XML DOM tree and writes it out to disk.
 */
void MainConfigFile::write(const com::Utf8Str strFilename)
{
    bumpSettingsVersionIfNeeded();

    m->strFilename = strFilename;
    specialBackupIfFirstBump();
    createStubDocument();

    xml::ElementNode *pelmGlobal = m->pelmRoot->createChild("Global");

    buildExtraData(*pelmGlobal, mapExtraDataItems);

    xml::ElementNode *pelmMachineRegistry = pelmGlobal->createChild("MachineRegistry");
    for (MachinesRegistry::const_iterator it = llMachines.begin();
         it != llMachines.end();
         ++it)
    {
        // <MachineEntry uuid="{5f102a55-a51b-48e3-b45a-b28d33469488}" src="/mnt/innotek-unix/vbox-machines/Windows 5.1 XP 1 (Office 2003)/Windows 5.1 XP 1 (Office 2003).xml"/>
        const MachineRegistryEntry &mre = *it;
        xml::ElementNode *pelmMachineEntry = pelmMachineRegistry->createChild("MachineEntry");
        pelmMachineEntry->setAttribute("uuid", mre.uuid.toStringCurly());
        pelmMachineEntry->setAttribute("src", mre.strSettingsFile);
    }

    buildMediaRegistry(*pelmGlobal, mediaRegistry);

    xml::ElementNode *pelmNetServiceRegistry = pelmGlobal->createChild("NetserviceRegistry"); /** @todo r=bird: wrong capitalization of NetServiceRegistry. sigh. */
    buildDHCPServers(*pelmNetServiceRegistry->createChild("DHCPServers"), llDhcpServers);

    xml::ElementNode *pelmNATNetworks;
    /* don't create entry if no NAT networks are registered. */
    if (!llNATNetworks.empty())
    {
        pelmNATNetworks = pelmNetServiceRegistry->createChild("NATNetworks");
        for (NATNetworksList::const_iterator it = llNATNetworks.begin();
             it != llNATNetworks.end();
             ++it)
        {
            const NATNetwork &n = *it;
            xml::ElementNode *pelmThis = pelmNATNetworks->createChild("NATNetwork");
            pelmThis->setAttribute("networkName", n.strNetworkName);
            pelmThis->setAttribute("network", n.strIPv4NetworkCidr);
            pelmThis->setAttribute("ipv6", n.fIPv6Enabled ? 1 : 0);
            pelmThis->setAttribute("ipv6prefix", n.strIPv6Prefix);
            pelmThis->setAttribute("advertiseDefaultIPv6Route", (n.fAdvertiseDefaultIPv6Route)? 1 : 0);
            pelmThis->setAttribute("needDhcp", (n.fNeedDhcpServer) ? 1 : 0);
            pelmThis->setAttribute("enabled", (n.fEnabled) ? 1 : 0);        // too bad we chose 1 vs. 0 here
            if (n.mapPortForwardRules4.size())
            {
                xml::ElementNode *pelmPf4 = pelmThis->createChild("PortForwarding4");
                buildNATForwardRulesMap(*pelmPf4, n.mapPortForwardRules4);
            }
            if (n.mapPortForwardRules6.size())
            {
                xml::ElementNode *pelmPf6 = pelmThis->createChild("PortForwarding6");
                buildNATForwardRulesMap(*pelmPf6, n.mapPortForwardRules6);
            }

            if (n.llHostLoopbackOffsetList.size())
            {
                xml::ElementNode *pelmMappings = pelmThis->createChild("Mappings");
                buildNATLoopbacks(*pelmMappings, n.llHostLoopbackOffsetList);

            }
        }
    }

#ifdef VBOX_WITH_VMNET
    xml::ElementNode *pelmHostOnlyNetworks;
    /* don't create entry if no HostOnly networks are registered. */
    if (!llHostOnlyNetworks.empty())
    {
        pelmHostOnlyNetworks = pelmNetServiceRegistry->createChild("HostOnlyNetworks");
        for (HostOnlyNetworksList::const_iterator it = llHostOnlyNetworks.begin();
             it != llHostOnlyNetworks.end();
             ++it)
        {
            const HostOnlyNetwork &n = *it;
            xml::ElementNode *pelmThis = pelmHostOnlyNetworks->createChild("HostOnlyNetwork");
            pelmThis->setAttribute("name", n.strNetworkName);
            pelmThis->setAttribute("mask", n.strNetworkMask);
            pelmThis->setAttribute("ipLower", n.strIPLower);
            pelmThis->setAttribute("ipUpper", n.strIPUpper);
            pelmThis->setAttribute("id", n.uuid.toStringCurly());
            pelmThis->setAttribute("enabled", (n.fEnabled) ? 1 : 0);        // too bad we chose 1 vs. 0 here
        }
    }
#endif /* VBOX_WITH_VMNET */
#ifdef VBOX_WITH_CLOUD_NET
    xml::ElementNode *pelmCloudNetworks;
    /* don't create entry if no cloud networks are registered. */
    if (!llCloudNetworks.empty())
    {
        pelmCloudNetworks = pelmNetServiceRegistry->createChild("CloudNetworks");
        for (CloudNetworksList::const_iterator it = llCloudNetworks.begin();
             it != llCloudNetworks.end();
             ++it)
        {
            const CloudNetwork &n = *it;
            xml::ElementNode *pelmThis = pelmCloudNetworks->createChild("CloudNetwork");
            pelmThis->setAttribute("name", n.strNetworkName);
            pelmThis->setAttribute("provider", n.strProviderShortName);
            pelmThis->setAttribute("profile", n.strProfileName);
            pelmThis->setAttribute("id", n.strNetworkId);
            pelmThis->setAttribute("enabled", (n.fEnabled) ? 1 : 0);        // too bad we chose 1 vs. 0 here
        }
    }
#endif /* VBOX_WITH_CLOUD_NET */

#ifdef VBOX_WITH_UPDATE_AGENT
    /* We keep the updates configuration as part of the host for now, as the API exposes the IHost::updateHost attribute,
     * but use an own "Updates" branch in the XML for better structurizing stuff in the future. */
    UpdateAgent &updateHost = host.updateHost;

    xml::ElementNode *pelmUpdates = pelmGlobal->createChild("Updates");
    /* Global enabled switch for updates. Currently bound to host updates, as this is the only update we have so far. */
    pelmUpdates->setAttribute("enabled", updateHost.fEnabled);

    xml::ElementNode *pelmUpdateHost = pelmUpdates->createChild("Host");
    pelmUpdateHost->setAttribute("enabled", updateHost.fEnabled);
    pelmUpdateHost->setAttribute("channel", (int32_t)updateHost.enmChannel);
    pelmUpdateHost->setAttribute("checkFreqSec", updateHost.uCheckFreqSeconds);
    if (updateHost.strRepoUrl.length())
        pelmUpdateHost->setAttribute("repoUrl", updateHost.strRepoUrl);
    if (updateHost.strLastCheckDate.length())
        pelmUpdateHost->setAttribute("lastCheckDate", updateHost.strLastCheckDate);
    pelmUpdateHost->setAttribute("checkCount", updateHost.uCheckCount);
    /** @todo Add update settings for ExtPack and Guest Additions here later. See @bugref{7983}. */
#endif

    xml::ElementNode *pelmSysProps = pelmGlobal->createChild("SystemProperties");
    if (systemProperties.strDefaultMachineFolder.length())
        pelmSysProps->setAttribute("defaultMachineFolder", systemProperties.strDefaultMachineFolder);
    if (systemProperties.strLoggingLevel.length())
        pelmSysProps->setAttribute("LoggingLevel", systemProperties.strLoggingLevel);
    if (systemProperties.strDefaultHardDiskFormat.length())
        pelmSysProps->setAttribute("defaultHardDiskFormat", systemProperties.strDefaultHardDiskFormat);
    if (systemProperties.strVRDEAuthLibrary.length())
        pelmSysProps->setAttribute("VRDEAuthLibrary", systemProperties.strVRDEAuthLibrary);
    if (systemProperties.strWebServiceAuthLibrary.length())
        pelmSysProps->setAttribute("webServiceAuthLibrary", systemProperties.strWebServiceAuthLibrary);
    if (systemProperties.strDefaultVRDEExtPack.length())
        pelmSysProps->setAttribute("defaultVRDEExtPack", systemProperties.strDefaultVRDEExtPack);
    if (systemProperties.strDefaultCryptoExtPack.length())
        pelmSysProps->setAttribute("defaultCryptoExtPack", systemProperties.strDefaultCryptoExtPack);
    pelmSysProps->setAttribute("LogHistoryCount", systemProperties.uLogHistoryCount);
    if (systemProperties.strAutostartDatabasePath.length())
        pelmSysProps->setAttribute("autostartDatabasePath", systemProperties.strAutostartDatabasePath);
    if (systemProperties.strDefaultFrontend.length())
        pelmSysProps->setAttribute("defaultFrontend", systemProperties.strDefaultFrontend);
    if (systemProperties.strProxyUrl.length())
        pelmSysProps->setAttribute("proxyUrl", systemProperties.strProxyUrl);
    pelmSysProps->setAttribute("proxyMode", systemProperties.uProxyMode);
    if (m->sv >= SettingsVersion_v1_20) /* Since settings v1.20 exclusiveHwVirt is part of PlatformProperties. */
    {
        xml::ElementNode *pelmPlatProps = pelmGlobal->createChild("PlatformProperties");
        pelmPlatProps->setAttribute("exclusiveHwVirt", platformProperties.fExclusiveHwVirt);
    }
    else
        pelmSysProps->setAttribute("exclusiveHwVirt", platformProperties.fExclusiveHwVirt);
    if (systemProperties.strLanguageId.isNotEmpty())
        pelmSysProps->setAttribute("LanguageId", systemProperties.strLanguageId);

    buildUSBDeviceFilters(*pelmGlobal->createChild("USBDeviceFilters"),
                          host.llUSBDeviceFilters,
                          true);               // fHostMode

    if (!host.llUSBDeviceSources.empty())
        buildUSBDeviceSources(*pelmGlobal->createChild("USBDeviceSources"),
                              host.llUSBDeviceSources);

    // now go write the XML
    xml::XmlFileWriter writer(*m->pDoc);
    writer.write(m->strFilename.c_str(), true /*fSafe*/);

    m->fFileExists = true;

    clearDocument();
    LogRel(("Finished saving settings file \"%s\"\n", m->strFilename.c_str()));
}

////////////////////////////////////////////////////////////////////////////////
//
// Machine XML structures
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
VRDESettings::VRDESettings() :
    fEnabled(true), // default for old VMs, for new ones it's false
    authType(AuthType_Null),
    ulAuthTimeout(5000),
    fAllowMultiConnection(false),
    fReuseSingleConnection(false)
{
}

/**
 * Check if all settings have default values.
 */
bool VRDESettings::areDefaultSettings(SettingsVersion_T sv) const
{
    return (sv < SettingsVersion_v1_16 ? fEnabled : !fEnabled)
        && authType == AuthType_Null
        && (ulAuthTimeout == 5000 || ulAuthTimeout == 0)
        && strAuthLibrary.isEmpty()
        && !fAllowMultiConnection
        && !fReuseSingleConnection
        && strVrdeExtPack.isEmpty()
        && mapProperties.size() == 0;
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool VRDESettings::operator==(const VRDESettings& v) const
{
    return (this == &v)
        || (   fEnabled                  == v.fEnabled
            && authType                  == v.authType
            && ulAuthTimeout             == v.ulAuthTimeout
            && strAuthLibrary            == v.strAuthLibrary
            && fAllowMultiConnection     == v.fAllowMultiConnection
            && fReuseSingleConnection    == v.fReuseSingleConnection
            && strVrdeExtPack            == v.strVrdeExtPack
            && mapProperties             == v.mapProperties);
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
FirmwareSettings::FirmwareSettings() :
    firmwareType(FirmwareType_BIOS),
    fACPIEnabled(true),
    fIOAPICEnabled(false),
    fLogoFadeIn(true),
    fLogoFadeOut(true),
    fPXEDebugEnabled(false),
    fSmbiosUuidLittleEndian(true),
    fAutoSerialNumGen(true),
    ulLogoDisplayTime(0),
    enmBootMenuMode(FirmwareBootMenuMode_MessageAndMenu),
    apicMode(APICMode_APIC),
    llTimeOffset(0)
{
}

/**
 * Returns if all settings have default values.
 *
 * @returns \c true if all settings have default values, \c false if not.
 * @param   enmCPUArch              CPU architecture to use for checking the default values for.
 */
bool FirmwareSettings::areDefaultSettings(CPUArchitecture_T enmCPUArch) const
{
    switch (enmCPUArch)
    {
        case CPUArchitecture_x86:
            RT_FALL_THROUGH();
        case CPUArchitecture_AMD64:
            return
                   firmwareType == FirmwareType_BIOS
                && !fIOAPICEnabled
                && fLogoFadeIn
                && fLogoFadeOut
                && !fPXEDebugEnabled
                && !fSmbiosUuidLittleEndian
                && !fAutoSerialNumGen
                && ulLogoDisplayTime == 0
                && enmBootMenuMode == FirmwareBootMenuMode_MessageAndMenu
                && apicMode == APICMode_APIC
                && llTimeOffset == 0
                && strLogoImagePath.isEmpty();

        case CPUArchitecture_ARMv8_32:
            RT_FALL_THROUGH();
        case CPUArchitecture_ARMv8_64:
            return
                   (    enmCPUArch == CPUArchitecture_ARMv8_32
                    ? firmwareType == FirmwareType_EFI32
                    : firmwareType == FirmwareType_EFI64)
                && !fIOAPICEnabled
                && fLogoFadeIn
                && fLogoFadeOut
                && !fPXEDebugEnabled
                && !fSmbiosUuidLittleEndian
                && !fAutoSerialNumGen
                && ulLogoDisplayTime == 0
                && enmBootMenuMode == FirmwareBootMenuMode_MessageAndMenu
                && apicMode == APICMode_APIC
                && llTimeOffset == 0
                && strLogoImagePath.isEmpty();
            break;

        default:
            break;
    }

    AssertFailedReturn(false);
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool FirmwareSettings::operator==(const FirmwareSettings &d) const
{
    return (this == &d)
        || (   firmwareType            == d.firmwareType
            && fACPIEnabled            == d.fACPIEnabled
            && fIOAPICEnabled          == d.fIOAPICEnabled
            && fLogoFadeIn             == d.fLogoFadeIn
            && fLogoFadeOut            == d.fLogoFadeOut
            && fPXEDebugEnabled        == d.fPXEDebugEnabled
            && fSmbiosUuidLittleEndian == d.fSmbiosUuidLittleEndian
            && fAutoSerialNumGen       == d.fAutoSerialNumGen
            && ulLogoDisplayTime       == d.ulLogoDisplayTime
            && enmBootMenuMode        == d.enmBootMenuMode
            && apicMode                == d.apicMode
            && llTimeOffset            == d.llTimeOffset
            && strLogoImagePath        == d.strLogoImagePath);
}

RecordingScreen::RecordingScreen(uint32_t a_idScreen /* = UINT32_MAX */)
    : idScreen(a_idScreen)
{
    applyDefaults();
}

RecordingScreen::~RecordingScreen()
{

}

/**
 * Returns the default options string for screen recording settings.
 *
 * @returns Default options string for a given screen.
 */
/* static */
const char *RecordingScreen::getDefaultOptions(void)
{
    /* Note: Needs to be kept in sync with FE/Qt's UIMachineSettingsDisplay::putToCache()! */
    return "vc_enabled=true,ac_enabled=false,ac_profile=med";
}

/**
 * Returns a recording settings feature map from a given string.
 *
 * @returns VBox status code.
 * @param   strFeatures         String of features to convert.
 * @param   featureMap          Where to return the converted features on success.
 */
/* static */
int RecordingScreen::featuresFromString(const com::Utf8Str &strFeatures, RecordingFeatureMap &featureMap)
{
    featureMap.clear();

    RTCList<RTCString> lstFeatures = strFeatures.split(" ");
    for (size_t i = 0; i < lstFeatures.size(); i++)
    {
        if (lstFeatures.at(i).compare("video", RTCString::CaseInsensitive) == 0)
            featureMap[RecordingFeature_Video] = true;
        else if (lstFeatures.at(i).compare("audio", RTCString::CaseInsensitive) == 0)
            featureMap[RecordingFeature_Audio] = true;
        /* ignore everything else */
    }

    return VINF_SUCCESS;
}

/**
 * Converts a feature map to a serializable string.
 *
 * @param   featureMap          Feature map to convert.
 * @param   strFeatures         Where to return the features converted as a string.
 */
/* static */
void RecordingScreen::featuresToString(const RecordingFeatureMap &featureMap, com::Utf8Str &strFeatures)
{
    strFeatures = "";

    RecordingFeatureMap::const_iterator itFeature = featureMap.begin();
    while (itFeature != featureMap.end())
    {
        if (itFeature->first == RecordingFeature_Video && itFeature->second)
            strFeatures += "video ";
        if (itFeature->first == RecordingFeature_Audio && itFeature->second)
            strFeatures += "audio ";
        ++itFeature;
    }
    strFeatures.strip();
}

/**
 * Returns a recording settings audio codec from a given string.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_SUPPORTED if audio codec is invalid or not supported.
 * @param   strCodec            String that contains the codec name.
 * @param   enmCodec            Where to return the audio codec on success.
 *
 * @note    An empty string will return "none" (no codec).
 */
/* static */
int RecordingScreen::audioCodecFromString(const com::Utf8Str &strCodec, RecordingAudioCodec_T &enmCodec)
{
    if (   RTStrIStr(strCodec.c_str(), "none")
        || strCodec.isEmpty())
    {
        enmCodec = RecordingAudioCodec_None;
        return VINF_SUCCESS;
    }
    else if (RTStrIStr(strCodec.c_str(), "wav"))
    {
        enmCodec = RecordingAudioCodec_WavPCM;
        return VINF_SUCCESS;
    }
    else if (RTStrIStr(strCodec.c_str(), "mp3"))
    {
        enmCodec = RecordingAudioCodec_MP3;
        return VINF_SUCCESS;
    }
    else if (RTStrIStr(strCodec.c_str(), "opus"))
    {
        enmCodec = RecordingAudioCodec_Opus;
        return VINF_SUCCESS;
    }
    else if (RTStrIStr(strCodec.c_str(), "vorbis"))
    {
        enmCodec = RecordingAudioCodec_OggVorbis;
        return VINF_SUCCESS;
    }

    AssertFailedReturn(VERR_NOT_SUPPORTED);
}

/**
 * Converts an audio codec to a serializable string.
 *
 * @param   enmCodec            Codec to convert to a string.
 * @param   strCodec            Where to return the audio codec converted as a string.
 */
/* static */
void RecordingScreen::audioCodecToString(const RecordingAudioCodec_T &enmCodec, com::Utf8Str &strCodec)
{
    switch (enmCodec)
    {
        case RecordingAudioCodec_None:      strCodec = "none";   return;
        case RecordingAudioCodec_WavPCM:    strCodec = "wav";    return;
        case RecordingAudioCodec_MP3:       strCodec = "mp3";    return;
        case RecordingAudioCodec_Opus:      strCodec = "opus";   return;
        case RecordingAudioCodec_OggVorbis: strCodec = "vorbis"; return;
        default:                            AssertFailedReturnVoid();
    }
}

/**
 * Returns a recording settings video codec from a given string.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_SUPPORTED if video codec is invalid or not supported.
 * @param   strCodec            String that contains the codec name.
 * @param   enmCodec            Where to return the video codec on success.
 *
 * @note    An empty string will return "none" (no codec).
 */
/* static */
int RecordingScreen::videoCodecFromString(const com::Utf8Str &strCodec, RecordingVideoCodec_T &enmCodec)
{
    if (   RTStrIStr(strCodec.c_str(), "none")
        || strCodec.isEmpty())
    {
        enmCodec = RecordingVideoCodec_None;
        return VINF_SUCCESS;
    }
    else if (RTStrIStr(strCodec.c_str(), "MJPEG"))
    {
        enmCodec = RecordingVideoCodec_MJPEG;
        return VINF_SUCCESS;
    }
    else if (RTStrIStr(strCodec.c_str(), "H262"))
    {
        enmCodec = RecordingVideoCodec_H262;
        return VINF_SUCCESS;
    }
    else if (RTStrIStr(strCodec.c_str(), "H264"))
    {
        enmCodec = RecordingVideoCodec_H264;
        return VINF_SUCCESS;
    }
    else if (RTStrIStr(strCodec.c_str(), "H265"))
    {
        enmCodec = RecordingVideoCodec_H265;
        return VINF_SUCCESS;
    }
    else if (RTStrIStr(strCodec.c_str(), "H266"))
    {
        enmCodec = RecordingVideoCodec_H266;
        return VINF_SUCCESS;
    }
    else if (RTStrIStr(strCodec.c_str(), "VP8"))
    {
        enmCodec = RecordingVideoCodec_VP8;
        return VINF_SUCCESS;
    }
    else if (RTStrIStr(strCodec.c_str(), "VP9"))
    {
        enmCodec = RecordingVideoCodec_VP9;
        return VINF_SUCCESS;
    }
    else if (RTStrIStr(strCodec.c_str(), "AV1"))
    {
        enmCodec = RecordingVideoCodec_AV1;
        return VINF_SUCCESS;
    }
    else if (RTStrIStr(strCodec.c_str(), "other"))
    {
        enmCodec = RecordingVideoCodec_Other;
        return VINF_SUCCESS;
    }

    AssertFailedReturn(VERR_NOT_SUPPORTED);
}

/**
 * Converts a video codec to a serializable string.
 *
 * @param   enmCodec            Codec to convert to a string.
 * @param   strCodec            Where to return the video codec converted as a string.
 */
/* static */
void RecordingScreen::videoCodecToString(const RecordingVideoCodec_T &enmCodec, com::Utf8Str &strCodec)
{
    switch (enmCodec)
    {
        case RecordingVideoCodec_None:  strCodec = "none";  return;
        case RecordingVideoCodec_MJPEG: strCodec = "MJPEG"; return;
        case RecordingVideoCodec_H262:  strCodec = "H262";  return;
        case RecordingVideoCodec_H264:  strCodec = "H264";  return;
        case RecordingVideoCodec_H265:  strCodec = "H265";  return;
        case RecordingVideoCodec_H266:  strCodec = "H266";  return;
        case RecordingVideoCodec_VP8:   strCodec = "VP8";   return;
        case RecordingVideoCodec_VP9:   strCodec = "VP9";   return;
        case RecordingVideoCodec_AV1:   strCodec = "AV1";   return;
        case RecordingVideoCodec_Other: strCodec = "other"; return;
        default:                        AssertFailedReturnVoid();
    }
}

/**
 * Applies the default settings.
 */
void RecordingScreen::applyDefaults(void)
{
    /*
     * Set sensible defaults.
     */

    /*
     * Enable screen 0 by default.
     * Otherwise enabling recording without any screen enabled at all makes no sense.
     *
     * Note: When tweaking this, make sure to also alter RecordingScreenSettings::areDefaultSettings().
     */
    fEnabled             = idScreen == 0 ? true : false;;
    enmDest              = RecordingDestination_File;
    ulMaxTimeS           = 0;
    strOptions           = RecordingScreen::getDefaultOptions();
    File.ulMaxSizeMB     = 0;
    File.strName         = "";
    Video.enmCodec       = RecordingVideoCodec_VP8;
    Video.enmDeadline    = RecordingCodecDeadline_Default;
    Video.enmRateCtlMode = RecordingRateControlMode_VBR;
    Video.enmScalingMode = RecordingVideoScalingMode_None;
    Video.ulWidth        = 1024;
    Video.ulHeight       = 768;
    Video.ulRate         = 512;
    Video.ulFPS          = 25;
#ifdef VBOX_WITH_AUDIO_RECORDING
# if   defined(VBOX_WITH_LIBVORBIS)
    Audio.enmCodec       = RecordingAudioCodec_OggVorbis;
# else
    Audio.enmCodec       = RecordingAudioCodec_None;
# endif
#else
    Audio.enmCodec       = RecordingAudioCodec_None;
#endif /* VBOX_WITH_RECORDING */
    Audio.enmDeadline    = RecordingCodecDeadline_Default;
    Audio.enmRateCtlMode = RecordingRateControlMode_VBR;
    Audio.cBits          = 16;
    Audio.cChannels      = 2;
    Audio.uHz            = 22050;

    featureMap[RecordingFeature_Video] = true;
    featureMap[RecordingFeature_Audio] = false; /** @todo Audio is not yet enabled by default. */
}

/**
 * Check if all settings have default values.
 *
 * @returns @c true if default, @c false if not.
 */
bool RecordingScreen::areDefaultSettings(void) const
{
    return    (   fEnabled                                    == false
               /* Screen 0 is special: There we ALWAYS enable recording by default. */
               || (   idScreen                                == 0
                   && fEnabled                                == true)
              )
           && enmDest                                         == RecordingDestination_File
           && ulMaxTimeS                                      == 0
           && strOptions                                      == RecordingScreen::getDefaultOptions()
           && File.ulMaxSizeMB                                == 0
           && File.strName                                    == ""
           && Video.enmCodec                                  == RecordingVideoCodec_VP8
           && Video.enmDeadline                               == RecordingCodecDeadline_Default
           && Video.enmRateCtlMode                            == RecordingRateControlMode_VBR
           && Video.enmScalingMode                            == RecordingVideoScalingMode_None
           && Video.ulWidth                                   == 1024
           && Video.ulHeight                                  == 768
           && Video.ulRate                                    == 512
           && Video.ulFPS                                     == 25
#ifdef VBOX_WITH_AUDIO_RECORDING
# if   defined(VBOX_WITH_LIBVORBIS)
           && Audio.enmCodec                                  == RecordingAudioCodec_OggVorbis
# else
           && Audio.enmCodec                                  == RecordingAudioCodec_None
# endif
#else
           && Audio.enmCodec                                  == RecordingAudioCodec_None
#endif /* VBOX_WITH_AUDIO_RECORDING */
           && Audio.enmDeadline                               == RecordingCodecDeadline_Default
           && Audio.enmRateCtlMode                            == RecordingRateControlMode_VBR
           && Audio.cBits                                     == 16
           && Audio.cChannels                                 == 2
           && Audio.uHz                                       == 22050
           && featureMap.find(RecordingFeature_Video)->second == true
           && featureMap.find(RecordingFeature_Audio)->second == false;
}

/**
 * Returns if a certain recording feature is enabled or not.
 *
 * @returns @c true if the feature is enabled, @c false if not.
 * @param   enmFeature          Feature to check.
 */
bool RecordingScreen::isFeatureEnabled(RecordingFeature_T enmFeature) const
{
    RecordingFeatureMap::const_iterator itFeature = featureMap.find(enmFeature);
    if (itFeature != featureMap.end())
        return itFeature->second;

    return false;
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool RecordingScreen::operator==(const RecordingScreen &d) const
{
    return    fEnabled             == d.fEnabled
           && enmDest              == d.enmDest
           && featureMap           == d.featureMap
           && ulMaxTimeS           == d.ulMaxTimeS
           && strOptions           == d.strOptions
           && File.strName         == d.File.strName
           && File.ulMaxSizeMB     == d.File.ulMaxSizeMB
           && Video.enmCodec       == d.Video.enmCodec
           && Video.enmDeadline    == d.Video.enmDeadline
           && Video.enmRateCtlMode == d.Video.enmRateCtlMode
           && Video.enmScalingMode == d.Video.enmScalingMode
           && Video.ulWidth        == d.Video.ulWidth
           && Video.ulHeight       == d.Video.ulHeight
           && Video.ulRate         == d.Video.ulRate
           && Video.ulFPS          == d.Video.ulFPS
           && Audio.enmCodec       == d.Audio.enmCodec
           && Audio.enmDeadline    == d.Audio.enmDeadline
           && Audio.enmRateCtlMode == d.Audio.enmRateCtlMode
           && Audio.cBits          == d.Audio.cBits
           && Audio.cChannels      == d.Audio.cChannels
           && Audio.uHz            == d.Audio.uHz
           && featureMap           == d.featureMap;
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
RecordingCommon::RecordingCommon()
{
    applyDefaults();
}

/**
 * Applies the default settings.
 */
void RecordingCommon::applyDefaults(void)
{
    fEnabled = false;
}

/**
 * Check if all settings have default values.
 */
bool RecordingCommon::areDefaultSettings(void) const
{
    return fEnabled == false;
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool RecordingCommon::operator==(const RecordingCommon &d) const
{
    if (this == &d)
        return true;

    return fEnabled == d.fEnabled;
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
Recording::Recording()
{
    applyDefaults();
}

/**
 * Applies the default settings.
 */
void Recording::applyDefaults(void)
{
    common.applyDefaults();

    mapScreens.clear();

    try
    {
        /* Always add screen 0 to the default configuration. */
        RecordingScreen screenSettings(0 /* Screen ID */);

        mapScreens[0 /* Screen ID */] = screenSettings;
    }
    catch (std::bad_alloc &)
    {
        AssertFailed();
    }
}

/**
 * Check if all settings have default values.
 */
bool Recording::areDefaultSettings(void) const
{
    AssertReturn(mapScreens.size() >= 1, false); /* The first screen always must be present. */

    if (!common.areDefaultSettings())
        return false;

    RecordingScreenSettingsMap::const_iterator itScreen = mapScreens.begin();
    while (itScreen != mapScreens.end())
    {
        if (!itScreen->second.areDefaultSettings())
            return false;
        ++itScreen;
    }

    return true;
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool Recording::operator==(const Recording &that) const
{
    if (this == &that) /* If pointers match, take a shortcut. */
        return true;

    if (common == that.common)
    {
        /* Too lazy for a != operator. */
    }
    else
        return false;

    if (mapScreens.size() != that.mapScreens.size())
        return false;

    RecordingScreenSettingsMap::const_iterator itScreen     = mapScreens.begin();
    RecordingScreenSettingsMap::const_iterator itScreenThat = that.mapScreens.begin();
    while (   itScreen     != mapScreens.end()
           && itScreenThat != that.mapScreens.end())
    {
        if (itScreen->second == itScreenThat->second)
        {
            /* Nothing to do in here (yet). */
        }
        else
            return false;

        ++itScreen;
        ++itScreenThat;
    }

    return true;
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
GraphicsAdapter::GraphicsAdapter() :
    graphicsControllerType(GraphicsControllerType_VBoxVGA),
    ulVRAMSizeMB(8),
    cMonitors(1),
    fAccelerate3D(false),
    fAccelerate2DVideo(false)
{
}

/**
 * Check if all settings have default values.
 */
bool GraphicsAdapter::areDefaultSettings() const
{
    return graphicsControllerType == GraphicsControllerType_VBoxVGA
        && ulVRAMSizeMB == 8
        && cMonitors <= 1
        && !fAccelerate3D
        && !fAccelerate2DVideo;
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool GraphicsAdapter::operator==(const GraphicsAdapter &g) const
{
    return (this == &g)
        || (   graphicsControllerType         == g.graphicsControllerType
            && ulVRAMSizeMB                   == g.ulVRAMSizeMB
            && cMonitors                      == g.cMonitors
            && fAccelerate3D                  == g.fAccelerate3D
            && fAccelerate2DVideo             == g.fAccelerate2DVideo);
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
TpmSettings::TpmSettings() :
    tpmType(TpmType_None)
{
}

/**
 * Check if all settings have default values.
 */
bool TpmSettings::areDefaultSettings() const
{
    return    tpmType     == TpmType_None
           && strLocation.isEmpty();
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool TpmSettings::operator==(const TpmSettings &g) const
{
    return (this == &g)
        || (   tpmType         == g.tpmType
            && strLocation     == g.strLocation);
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
NvramSettings::NvramSettings()
{
}

/**
 * Check if all settings have default values.
 */
bool NvramSettings::areDefaultSettings() const
{
    return     strNvramPath.isEmpty()
            && strKeyId.isEmpty()
            && strKeyStore.isEmpty();
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool NvramSettings::operator==(const NvramSettings &g) const
{
    return (this == &g)
        || (strNvramPath    == g.strNvramPath)
        || (strKeyId        == g.strKeyId)
        || (strKeyStore     == g.strKeyStore);
}


/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
USBController::USBController() :
    enmType(USBControllerType_Null)
{
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool USBController::operator==(const USBController &u) const
{
    return (this == &u)
        || (   strName == u.strName
            && enmType == u.enmType);
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
USB::USB()
{
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool USB::operator==(const USB &u) const
{
    return (this == &u)
        || (   llUSBControllers == u.llUSBControllers
            && llDeviceFilters  == u.llDeviceFilters);
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
NAT::NAT() :
    u32Mtu(0),
    u32SockRcv(0),
    u32SockSnd(0),
    u32TcpRcv(0),
    u32TcpSnd(0),
    fDNSPassDomain(true), /* historically this value is true */
    fDNSProxy(false),
    fDNSUseHostResolver(false),
    fAliasLog(false),
    fAliasProxyOnly(false),
    fAliasUseSamePorts(false),
    fLocalhostReachable(true) /* Historically this value is true. */
{
}

/**
 * Check if all DNS settings have default values.
 */
bool NAT::areDNSDefaultSettings() const
{
    return fDNSPassDomain && !fDNSProxy && !fDNSUseHostResolver;
}

/**
 * Check if all Alias settings have default values.
 */
bool NAT::areAliasDefaultSettings() const
{
    return !fAliasLog && !fAliasProxyOnly && !fAliasUseSamePorts;
}

/**
 * Check if all TFTP settings have default values.
 */
bool NAT::areTFTPDefaultSettings() const
{
    return strTFTPPrefix.isEmpty()
        && strTFTPBootFile.isEmpty()
        && strTFTPNextServer.isEmpty();
}

/**
 * Check whether the localhost-reachable setting is the default for the given settings version.
 */
bool NAT::areLocalhostReachableDefaultSettings(SettingsVersion_T sv) const
{
    return    (   fLocalhostReachable
                && sv < SettingsVersion_v1_19)
           || (   !fLocalhostReachable
                && sv >= SettingsVersion_v1_19);
}

/**
 * Check if all settings have default values.
 */
bool NAT::areDefaultSettings(SettingsVersion_T sv) const
{
    /*
     * Before settings version 1.19 localhost was reachable by default
     * when using NAT which was changed with version 1.19+, see @bugref{9896}
     * for more information.
     */
    return strNetwork.isEmpty()
        && strBindIP.isEmpty()
        && u32Mtu == 0
        && u32SockRcv == 0
        && u32SockSnd == 0
        && u32TcpRcv == 0
        && u32TcpSnd == 0
        && areDNSDefaultSettings()
        && areAliasDefaultSettings()
        && areTFTPDefaultSettings()
        && mapRules.size() == 0
        && areLocalhostReachableDefaultSettings(sv);
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool NAT::operator==(const NAT &n) const
{
   return (this == &n)
       || (   strNetwork           == n.strNetwork
            && strBindIP           == n.strBindIP
            && u32Mtu              == n.u32Mtu
            && u32SockRcv          == n.u32SockRcv
            && u32SockSnd          == n.u32SockSnd
            && u32TcpSnd           == n.u32TcpSnd
            && u32TcpRcv           == n.u32TcpRcv
            && strTFTPPrefix       == n.strTFTPPrefix
            && strTFTPBootFile     == n.strTFTPBootFile
            && strTFTPNextServer   == n.strTFTPNextServer
            && fDNSPassDomain      == n.fDNSPassDomain
            && fDNSProxy           == n.fDNSProxy
            && fDNSUseHostResolver == n.fDNSUseHostResolver
            && fAliasLog           == n.fAliasLog
            && fAliasProxyOnly     == n.fAliasProxyOnly
            && fAliasUseSamePorts  == n.fAliasUseSamePorts
            && fLocalhostReachable == n.fLocalhostReachable
            && mapRules            == n.mapRules);
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
NetworkAdapter::NetworkAdapter() :
    ulSlot(0),
    type(NetworkAdapterType_Am79C970A), // default for old VMs, for new ones it's Am79C973
    fEnabled(false),
    fCableConnected(false), // default for old VMs, for new ones it's true
    ulLineSpeed(0),
    enmPromiscModePolicy(NetworkAdapterPromiscModePolicy_Deny),
    fTraceEnabled(false),
    mode(NetworkAttachmentType_Null),
    ulBootPriority(0)
{
}

/**
 * Check if all Generic Driver settings have default values.
 */
bool NetworkAdapter::areGenericDriverDefaultSettings() const
{
    return strGenericDriver.isEmpty()
        && genericProperties.size() == 0;
}

/**
 * Check if all settings have default values.
 */
bool NetworkAdapter::areDefaultSettings(SettingsVersion_T sv) const
{
    // 5.0 and earlier had a default of fCableConnected=false, which doesn't
    // make a lot of sense (but it's a fact). Later versions don't save the
    // setting if it's at the default value and thus must get it right.
    return !fEnabled
        && strMACAddress.isEmpty()
        && (   (sv >= SettingsVersion_v1_16 && fCableConnected && type == NetworkAdapterType_Am79C973)
            || (sv < SettingsVersion_v1_16 && !fCableConnected && type == NetworkAdapterType_Am79C970A))
        && ulLineSpeed == 0
        && enmPromiscModePolicy == NetworkAdapterPromiscModePolicy_Deny
        && mode == NetworkAttachmentType_Null
        && nat.areDefaultSettings(sv)
        && strBridgedName.isEmpty()
        && strInternalNetworkName.isEmpty()
#ifdef VBOX_WITH_VMNET
        && strHostOnlyNetworkName.isEmpty()
#endif /* VBOX_WITH_VMNET */
#ifdef VBOX_WITH_CLOUD_NET
        && strCloudNetworkName.isEmpty()
#endif /* VBOX_WITH_CLOUD_NET */
        && strHostOnlyName.isEmpty()
        && areGenericDriverDefaultSettings()
        && strNATNetworkName.isEmpty();
}

/**
 * Special check if settings of the non-current attachment type have default values.
 */
bool NetworkAdapter::areDisabledDefaultSettings(SettingsVersion_T sv) const
{
    return (mode != NetworkAttachmentType_NAT ? nat.areDefaultSettings(sv) : true)
        && (mode != NetworkAttachmentType_Bridged ? strBridgedName.isEmpty() : true)
        && (mode != NetworkAttachmentType_Internal ? strInternalNetworkName.isEmpty() : true)
#ifdef VBOX_WITH_VMNET
        && (mode != NetworkAttachmentType_HostOnlyNetwork ? strHostOnlyNetworkName.isEmpty() : true)
#endif /* VBOX_WITH_VMNET */
#ifdef VBOX_WITH_CLOUD_NET
        && (mode != NetworkAttachmentType_Cloud ? strCloudNetworkName.isEmpty() : true)
#endif /* VBOX_WITH_CLOUD_NET */
        && (mode != NetworkAttachmentType_HostOnly ? strHostOnlyName.isEmpty() : true)
        && (mode != NetworkAttachmentType_Generic ? areGenericDriverDefaultSettings() : true)
        && (mode != NetworkAttachmentType_NATNetwork ? strNATNetworkName.isEmpty() : true);
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool NetworkAdapter::operator==(const NetworkAdapter &n) const
{
    return (this == &n)
        || (   ulSlot                == n.ulSlot
            && type                  == n.type
            && fEnabled              == n.fEnabled
            && strMACAddress         == n.strMACAddress
            && fCableConnected       == n.fCableConnected
            && ulLineSpeed           == n.ulLineSpeed
            && enmPromiscModePolicy  == n.enmPromiscModePolicy
            && fTraceEnabled         == n.fTraceEnabled
            && strTraceFile          == n.strTraceFile
            && mode                  == n.mode
            && nat                   == n.nat
            && strBridgedName        == n.strBridgedName
            && strHostOnlyName       == n.strHostOnlyName
#ifdef VBOX_WITH_VMNET
            && strHostOnlyNetworkName == n.strHostOnlyNetworkName
#endif /* VBOX_WITH_VMNET */
            && strInternalNetworkName == n.strInternalNetworkName
#ifdef VBOX_WITH_CLOUD_NET
            && strCloudNetworkName   == n.strCloudNetworkName
#endif /* VBOX_WITH_CLOUD_NET */
            && strGenericDriver      == n.strGenericDriver
            && genericProperties     == n.genericProperties
            && ulBootPriority        == n.ulBootPriority
            && strBandwidthGroup     == n.strBandwidthGroup);
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
SerialPort::SerialPort() :
    ulSlot(0),
    fEnabled(false),
    ulIOAddress(0x3f8),
    ulIRQ(4),
    portMode(PortMode_Disconnected),
    fServer(false),
    uartType(UartType_U16550A)
{
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool SerialPort::operator==(const SerialPort &s) const
{
    return (this == &s)
        || (   ulSlot            == s.ulSlot
            && fEnabled          == s.fEnabled
            && ulIOAddress       == s.ulIOAddress
            && ulIRQ             == s.ulIRQ
            && portMode          == s.portMode
            && strPath           == s.strPath
            && fServer           == s.fServer
            && uartType          == s.uartType);
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
ParallelPort::ParallelPort() :
    ulSlot(0),
    fEnabled(false),
    ulIOBase(0x378),
    ulIRQ(7)
{
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool ParallelPort::operator==(const ParallelPort &s) const
{
    return (this == &s)
        || (   ulSlot            == s.ulSlot
            && fEnabled          == s.fEnabled
            && ulIOBase          == s.ulIOBase
            && ulIRQ             == s.ulIRQ
            && strPath           == s.strPath);
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
AudioAdapter::AudioAdapter() :
    fEnabled(true), // default for old VMs, for new ones it's false
    fEnabledIn(true), // default for old VMs, for new ones it's false
    fEnabledOut(true), // default for old VMs, for new ones it's false
    controllerType(AudioControllerType_AC97),
    codecType(AudioCodecType_STAC9700),
    driverType(AudioDriverType_Null)
{
}

/**
 * Check if all settings have default values.
 */
bool AudioAdapter::areDefaultSettings(SettingsVersion_T sv) const
{
    return (sv < SettingsVersion_v1_16 ? false : !fEnabled)
        && (sv <= SettingsVersion_v1_16 ? fEnabledIn : !fEnabledIn)
        && (sv <= SettingsVersion_v1_16 ? fEnabledOut : !fEnabledOut)
        && fEnabledOut == true
        && controllerType == AudioControllerType_AC97
        && codecType == AudioCodecType_STAC9700
        && properties.size() == 0;
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool AudioAdapter::operator==(const AudioAdapter &a) const
{
    return (this == &a)
        || (   fEnabled        == a.fEnabled
            && fEnabledIn      == a.fEnabledIn
            && fEnabledOut     == a.fEnabledOut
            && controllerType  == a.controllerType
            && codecType       == a.codecType
            && driverType      == a.driverType
            && properties      == a.properties);
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
SharedFolder::SharedFolder() :
    fWritable(false),
    fAutoMount(false),
    enmSymlinkPolicy(SymlinkPolicy_None)
{
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool SharedFolder::operator==(const SharedFolder &g) const
{
    return (this == &g)
        || (   strName           == g.strName
            && strHostPath       == g.strHostPath
            && fWritable         == g.fWritable
            && fAutoMount        == g.fAutoMount
            && strAutoMountPoint == g.strAutoMountPoint
            && enmSymlinkPolicy  == g.enmSymlinkPolicy);
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
GuestProperty::GuestProperty() :
    timestamp(0)
{
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool GuestProperty::operator==(const GuestProperty &g) const
{
    return (this == &g)
        || (   strName           == g.strName
            && strValue          == g.strValue
            && timestamp         == g.timestamp
            && strFlags          == g.strFlags);
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
CpuIdLeafX86::CpuIdLeafX86() :
    idx(UINT32_MAX),
    idxSub(0),
    uEax(0),
    uEbx(0),
    uEcx(0),
    uEdx(0)
{
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool CpuIdLeafX86::operator==(const CpuIdLeafX86 &c) const
{
    return (this == &c)
        || (   idx      == c.idx
            && idxSub   == c.idxSub
            && uEax     == c.uEax
            && uEbx     == c.uEbx
            && uEcx     == c.uEcx
            && uEdx     == c.uEdx);
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
Cpu::Cpu() :
    ulId(UINT32_MAX)
{
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool Cpu::operator==(const Cpu &c) const
{
    return (this == &c)
        || (ulId      == c.ulId);
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
BandwidthGroup::BandwidthGroup() :
    cMaxBytesPerSec(0),
    enmType(BandwidthGroupType_Null)
{
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool BandwidthGroup::operator==(const BandwidthGroup &i) const
{
    return (this == &i)
        || (   strName      == i.strName
            && cMaxBytesPerSec == i.cMaxBytesPerSec
            && enmType      == i.enmType);
}

/**
 * IOSettings constructor.
 */
IOSettings::IOSettings() :
    fIOCacheEnabled(true),
    ulIOCacheSize(5)
{
}

/**
 * Check if all IO Cache settings have default values.
 */
bool IOSettings::areIOCacheDefaultSettings() const
{
    return fIOCacheEnabled
        && ulIOCacheSize == 5;
}

/**
 * Check if all settings have default values.
 */
bool IOSettings::areDefaultSettings() const
{
    return areIOCacheDefaultSettings()
        && llBandwidthGroups.size() == 0;
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool IOSettings::operator==(const IOSettings &i) const
{
    return (this == &i)
        || (   fIOCacheEnabled   == i.fIOCacheEnabled
            && ulIOCacheSize     == i.ulIOCacheSize
            && llBandwidthGroups == i.llBandwidthGroups);
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
HostPCIDeviceAttachment::HostPCIDeviceAttachment() :
    uHostAddress(0),
    uGuestAddress(0)
{
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool HostPCIDeviceAttachment::operator==(const HostPCIDeviceAttachment &a) const
{
    return (this == &a)
        || (   uHostAddress   == a.uHostAddress
            && uGuestAddress  == a.uGuestAddress
            && strDeviceName  == a.strDeviceName);
}

#ifdef VBOX_WITH_VIRT_ARMV8
PlatformARM::PlatformARM()
{
}

bool PlatformARM::operator==(const PlatformARM& h) const
{
    RT_NOREF(h);
    return true;
}
#endif /* VBOX_WITH_VIRT_ARMV8 */

PlatformX86::PlatformX86() :
    fPAE(false),
    fAPIC(true),
    fX2APIC(false),
    fHPETEnabled(false),
    enmLongMode(HC_ARCH_BITS == 64 ? PlatformX86::LongMode_Enabled : PlatformX86::LongMode_Disabled),
    fTripleFaultReset(false),
    fIBPBOnVMExit(false),
    fIBPBOnVMEntry(false),
    fSpecCtrl(false),
    fSpecCtrlByHost(false),
    fL1DFlushOnSched(true),
    fL1DFlushOnVMEntry(false),
    fMDSClearOnSched(true),
    fMDSClearOnVMEntry(false),
    fHWVirtEx(true),
    fHWVirtExNestedPaging(true),
    fHWVirtExVPID(true),
    fHWVirtExUX(true),
    fHWVirtExForce(false),
    fHWVirtExUseNativeApi(false),
    fHWVirtExVirtVmsaveVmload(true),
    fNestedHWVirt(false)
{
    /* The default value for PAE depends on the host:
     * - 64 bits host -> always true
     * - 32 bits host -> true for Windows & Darwin (masked off if the host cpu doesn't support it anyway)
     */
#if HC_ARCH_BITS == 64 || defined(RT_OS_WINDOWS) || defined(RT_OS_DARWIN)
    fPAE = true;
#endif

    /* The default value of large page supports depends on the host:
     * - 64 bits host -> true, unless it's Linux (pending further prediction work due to excessively expensive large page allocations)
     * - 32 bits host -> false
     */
#if HC_ARCH_BITS == 64 && !defined(RT_OS_LINUX)
    fHWVirtExLargePages = true; /** @todo BUGBUG Does this apply for ARM as well? */
#else
    /* Not supported on 32 bits hosts. */
    fHWVirtExLargePages = false;
#endif
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Platform::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool PlatformX86::operator==(const PlatformX86& h) const
{
    return (this == &h)
        || (   fPAE                         == h.fPAE
            && fAPIC                        == h.fAPIC
            && fX2APIC                      == h.fX2APIC
            && fHPETEnabled                 == h.fHPETEnabled
            && enmLongMode                  == h.enmLongMode
            && llCpuIdLeafs                 == h.llCpuIdLeafs
            && fTripleFaultReset            == h.fTripleFaultReset
            && fIBPBOnVMExit                == h.fIBPBOnVMExit
            && fIBPBOnVMEntry               == h.fIBPBOnVMEntry
            && fSpecCtrl                    == h.fSpecCtrl
            && fSpecCtrlByHost              == h.fSpecCtrlByHost
            && fL1DFlushOnSched             == h.fL1DFlushOnSched
            && fL1DFlushOnVMEntry           == h.fL1DFlushOnVMEntry
            && fMDSClearOnSched             == h.fMDSClearOnSched
            && fMDSClearOnVMEntry           == h.fMDSClearOnVMEntry
            && fHWVirtEx                    == h.fHWVirtEx
            && fHWVirtExNestedPaging        == h.fHWVirtExNestedPaging
            && fHWVirtExLargePages          == h.fHWVirtExLargePages
            && fHWVirtExVPID                == h.fHWVirtExVPID
            && fHWVirtExUX                  == h.fHWVirtExUX
            && fHWVirtExForce               == h.fHWVirtExForce
            && fHWVirtExUseNativeApi        == h.fHWVirtExUseNativeApi
            && fHWVirtExVirtVmsaveVmload    == h.fHWVirtExVirtVmsaveVmload
            && fNestedHWVirt                == h.fNestedHWVirt);
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
Platform::Platform() :
    architectureType(PlatformArchitecture_x86), /* We default to x86 here, as this is what we ever had so far. */
    chipsetType(ChipsetType_PIIX3),
    iommuType(IommuType_None),
    fRTCUseUTC(false)
{
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Platform::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool Platform::operator==(const Platform& h) const
{
    bool fRc = (this == &h)
            || (   architectureType == h.architectureType
                && chipsetType      == h.chipsetType
                && iommuType        == h.iommuType
                && fRTCUseUTC       == h.fRTCUseUTC);
    if (fRc)
    {
        switch (architectureType)
        {
            case PlatformArchitecture_x86:
                return x86 == h.x86;
#ifdef VBOX_WITH_VIRT_ARMV8
            case PlatformArchitecture_ARM:
                return arm == h.arm;
#endif
            default:
                AssertFailedReturn(false);
        }
    }

    return fRc;
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
Hardware::Hardware() :
    strVersion("1"),
    cCPUs(1),
    fCpuHotPlug(false),
    ulCpuExecutionCap(100),
    uCpuIdPortabilityLevel(0),
    strCpuProfile("host"),
    ulMemorySizeMB((uint32_t)-1),
    pointingHIDType(PointingHIDType_PS2Mouse),
    keyboardHIDType(KeyboardHIDType_PS2Keyboard),
    paravirtProvider(ParavirtProvider_Legacy), // default for old VMs, for new ones it's ParavirtProvider_Default
    strParavirtDebug(""),
    fEmulatedUSBCardReader(false),
    clipboardMode(ClipboardMode_Disabled),
    fClipboardFileTransfersEnabled(false),
    dndMode(DnDMode_Disabled),
    ulMemoryBalloonSize(0),
    fPageFusionEnabled(false)
{
    mapBootOrder[0] = DeviceType_Floppy;
    mapBootOrder[1] = DeviceType_DVD;
    mapBootOrder[2] = DeviceType_HardDisk;
}

/**
 * Check if all Paravirt settings have default values.
 */
bool Hardware::areParavirtDefaultSettings(SettingsVersion_T sv) const
{
    // 5.0 didn't save the paravirt settings if it is ParavirtProvider_Legacy,
    // so this default must be kept. Later versions don't save the setting if
    // it's at the default value.
    return (   (sv >= SettingsVersion_v1_16 && paravirtProvider == ParavirtProvider_Default)
            || (sv < SettingsVersion_v1_16 && paravirtProvider == ParavirtProvider_Legacy))
        && strParavirtDebug.isEmpty();
}

/**
 * Check if all Boot Order settings have default values.
 */
bool Hardware::areBootOrderDefaultSettings() const
{
    BootOrderMap::const_iterator it0 = mapBootOrder.find(0);
    BootOrderMap::const_iterator it1 = mapBootOrder.find(1);
    BootOrderMap::const_iterator it2 = mapBootOrder.find(2);
    BootOrderMap::const_iterator it3 = mapBootOrder.find(3);
    return    (   mapBootOrder.size() == 3
               || (   mapBootOrder.size() == 4
                   && (it3 != mapBootOrder.end() && it3->second == DeviceType_Null)))
           && (it0 != mapBootOrder.end() && it0->second == DeviceType_Floppy)
           && (it1 != mapBootOrder.end() && it1->second == DeviceType_DVD)
           && (it2 != mapBootOrder.end() && it2->second == DeviceType_HardDisk);
}

/**
 * Check if all Network Adapter settings have default values.
 */
bool Hardware::areAllNetworkAdaptersDefaultSettings(SettingsVersion_T sv) const
{
    for (NetworkAdaptersList::const_iterator it = llNetworkAdapters.begin();
         it != llNetworkAdapters.end();
         ++it)
    {
        if (!it->areDefaultSettings(sv))
            return false;
    }
    return true;
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool Hardware::operator==(const Hardware& h) const
{
    return (this == &h)
        || (   strVersion                     == h.strVersion
            && uuid                           == h.uuid
            && cCPUs                          == h.cCPUs
            && fCpuHotPlug                    == h.fCpuHotPlug
            && ulCpuExecutionCap              == h.ulCpuExecutionCap
            && uCpuIdPortabilityLevel         == h.uCpuIdPortabilityLevel
            && strCpuProfile                  == h.strCpuProfile
            && llCpus                         == h.llCpus
            && ulMemorySizeMB                 == h.ulMemorySizeMB
            && mapBootOrder                   == h.mapBootOrder
            && pointingHIDType                == h.pointingHIDType
            && keyboardHIDType                == h.keyboardHIDType
            && paravirtProvider               == h.paravirtProvider
            && strParavirtDebug               == h.strParavirtDebug
            && fEmulatedUSBCardReader         == h.fEmulatedUSBCardReader
            && vrdeSettings                   == h.vrdeSettings
            && platformSettings               == h.platformSettings
            && firmwareSettings               == h.firmwareSettings
            && nvramSettings                  == h.nvramSettings
            && graphicsAdapter                == h.graphicsAdapter
            && usbSettings                    == h.usbSettings
            && tpmSettings                    == h.tpmSettings
            && llNetworkAdapters              == h.llNetworkAdapters
            && llSerialPorts                  == h.llSerialPorts
            && llParallelPorts                == h.llParallelPorts
            && audioAdapter                   == h.audioAdapter
            && storage                        == h.storage
            && llSharedFolders                == h.llSharedFolders
            && clipboardMode                  == h.clipboardMode
            && fClipboardFileTransfersEnabled == h.fClipboardFileTransfersEnabled
            && dndMode                        == h.dndMode
            && ulMemoryBalloonSize            == h.ulMemoryBalloonSize
            && fPageFusionEnabled             == h.fPageFusionEnabled
            && llGuestProperties              == h.llGuestProperties
            && ioSettings                     == h.ioSettings
            && pciAttachments                 == h.pciAttachments
            && strDefaultFrontend             == h.strDefaultFrontend);
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
AttachedDevice::AttachedDevice() :
    deviceType(DeviceType_Null),
    fPassThrough(false),
    fTempEject(false),
    fNonRotational(false),
    fDiscard(false),
    fHotPluggable(false),
    lPort(0),
    lDevice(0)
{
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool AttachedDevice::operator==(const AttachedDevice &a) const
{
    return (this == &a)
        || (   deviceType                == a.deviceType
            && fPassThrough              == a.fPassThrough
            && fTempEject                == a.fTempEject
            && fNonRotational            == a.fNonRotational
            && fDiscard                  == a.fDiscard
            && fHotPluggable             == a.fHotPluggable
            && lPort                     == a.lPort
            && lDevice                   == a.lDevice
            && uuid                      == a.uuid
            && strHostDriveSrc           == a.strHostDriveSrc
            && strBwGroup                == a.strBwGroup);
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
StorageController::StorageController() :
    storageBus(StorageBus_IDE),
    controllerType(StorageControllerType_PIIX3),
    ulPortCount(2),
    ulInstance(0),
    fUseHostIOCache(true),
    fBootable(true)
{
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool StorageController::operator==(const StorageController &s) const
{
    return (this == &s)
        || (   strName                   == s.strName
            && storageBus                == s.storageBus
            && controllerType            == s.controllerType
            && ulPortCount               == s.ulPortCount
            && ulInstance                == s.ulInstance
            && fUseHostIOCache           == s.fUseHostIOCache
            && llAttachedDevices         == s.llAttachedDevices);
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool Storage::operator==(const Storage &s) const
{
    return (this == &s)
        || (llStorageControllers == s.llStorageControllers);            // deep compare
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
Debugging::Debugging() :
    fTracingEnabled(false),
    fAllowTracingToAccessVM(false),
    strTracingConfig(),
    enmDbgProvider(GuestDebugProvider_None),
    enmIoProvider(GuestDebugIoProvider_None),
    strAddress(),
    ulPort(0)
{
}

/**
 * Check if all settings have default values.
 */
bool Debugging::areDefaultSettings() const
{
    return !fTracingEnabled
        && !fAllowTracingToAccessVM
        && strTracingConfig.isEmpty()
        && enmDbgProvider == GuestDebugProvider_None
        && enmIoProvider == GuestDebugIoProvider_None
        && strAddress.isEmpty()
        && ulPort == 0;
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool Debugging::operator==(const Debugging &d) const
{
    return (this == &d)
        || (   fTracingEnabled          == d.fTracingEnabled
            && fAllowTracingToAccessVM  == d.fAllowTracingToAccessVM
            && strTracingConfig         == d.strTracingConfig
            && enmDbgProvider           == d.enmDbgProvider
            && enmIoProvider            == d.enmIoProvider
            && strAddress               == d.strAddress
            && ulPort                   == d.ulPort);
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
Autostart::Autostart() :
    fAutostartEnabled(false),
    uAutostartDelay(0),
    enmAutostopType(AutostopType_Disabled)
{
}

/**
 * Check if all settings have default values.
 */
bool Autostart::areDefaultSettings() const
{
    return !fAutostartEnabled
        && !uAutostartDelay
        && enmAutostopType == AutostopType_Disabled;
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool Autostart::operator==(const Autostart &a) const
{
    return (this == &a)
        || (   fAutostartEnabled == a.fAutostartEnabled
            && uAutostartDelay   == a.uAutostartDelay
            && enmAutostopType   == a.enmAutostopType);
}

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
Snapshot::Snapshot()
{
    RTTimeSpecSetNano(&timestamp, 0);
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool Snapshot::operator==(const Snapshot &s) const
{
    return (this == &s)
        || (   uuid                  == s.uuid
            && strName               == s.strName
            && strDescription        == s.strDescription
            && RTTimeSpecIsEqual(&timestamp, &s.timestamp)
            && strStateFile          == s.strStateFile
            && hardware              == s.hardware                  // deep compare
            && recordingSettings     == s.recordingSettings         // deep compare
            && llChildSnapshots      == s.llChildSnapshots          // deep compare
            && debugging             == s.debugging
            && autostart             == s.autostart);
}

const struct Snapshot settings::Snapshot::Empty; /* default ctor is OK */

/**
 * Constructor. Needs to set sane defaults which stand the test of time.
 */
MachineUserData::MachineUserData() :
    fDirectoryIncludesUUID(false),
    fNameSync(true),
    fTeleporterEnabled(false),
    uTeleporterPort(0),
    enmVMPriority(VMProcPriority_Default),
    enmExecEngine(VMExecutionEngine_Default)
{
    llGroups.push_back("/");
}

/**
 * Comparison operator. This gets called from MachineConfigFile::operator==,
 * which in turn gets called from Machine::saveSettings to figure out whether
 * machine settings have really changed and thus need to be written out to disk.
 */
bool MachineUserData::operator==(const MachineUserData &c) const
{
    return (this == &c)
        || (   strName                    == c.strName
            && fDirectoryIncludesUUID     == c.fDirectoryIncludesUUID
            && fNameSync                  == c.fNameSync
            && strDescription             == c.strDescription
            && llGroups                   == c.llGroups
            && strOsType                  == c.strOsType
            && strSnapshotFolder          == c.strSnapshotFolder
            && fTeleporterEnabled         == c.fTeleporterEnabled
            && uTeleporterPort            == c.uTeleporterPort
            && strTeleporterAddress       == c.strTeleporterAddress
            && strTeleporterPassword      == c.strTeleporterPassword
            && ovIcon                     == c.ovIcon
            && enmVMPriority              == c.enmVMPriority
            && enmExecEngine              == c.enmExecEngine);
}


////////////////////////////////////////////////////////////////////////////////
//
// MachineConfigFile
//
////////////////////////////////////////////////////////////////////////////////

/**
 * Constructor.
 *
 * If pstrFilename is != NULL, this reads the given settings file into the member
 * variables and various substructures and lists. Otherwise, the member variables
 * are initialized with default values.
 *
 * Throws variants of xml::Error for I/O, XML and logical content errors, which
 * the caller should catch; if this constructor does not throw, then the member
 * variables contain meaningful values (either from the file or defaults).
 *
 * @param pstrFilename
 * @param pCryptoIf             Pointer to the cryptographic interface, required for an encrypted machine config.
 * @param pszPassword           The password to use for an encrypted machine config.
 */
MachineConfigFile::MachineConfigFile(const Utf8Str *pstrFilename, PCVBOXCRYPTOIF pCryptoIf, const char *pszPassword)
    : ConfigFileBase(pstrFilename),
      enmParseState(ParseState_NotParsed),
      fCurrentStateModified(true),
      fAborted(false)
{
    RTTimeNow(&timeLastStateChange);

    if (pstrFilename)
    {
        // the ConfigFileBase constructor has loaded the XML file, so now
        // we need only analyze what is in there

        xml::NodesLoop nlRootChildren(*m->pelmRoot);
        const xml::ElementNode *pelmRootChild;
        while ((pelmRootChild = nlRootChildren.forAllNodes()))
        {
            if (pelmRootChild->nameEquals("MachineEncrypted"))
                readMachineEncrypted(*pelmRootChild, pCryptoIf, pszPassword);
            if (pelmRootChild->nameEquals("Machine"))
                readMachine(*pelmRootChild);
        }

        // clean up memory allocated by XML engine
        clearDocument();

        if (enmParseState == ParseState_NotParsed)
            enmParseState = ParseState_Parsed;
    }
}

/**
 * Public routine which returns true if this machine config file can have its
 * own media registry (which is true for settings version v1.11 and higher,
 * i.e. files created by VirtualBox 4.0 and higher).
 * @return
 */
bool MachineConfigFile::canHaveOwnMediaRegistry() const
{
    return (m->sv >= SettingsVersion_v1_11);
}

/**
 * Public routine which copies encryption settings. Used by Machine::saveSettings
 * so that the encryption settings do not get lost when a copy of the Machine settings
 * file is made to see if settings have actually changed.
 * @param other
 */
void MachineConfigFile::copyEncryptionSettingsFrom(const MachineConfigFile &other)
{
    strKeyId    = other.strKeyId;
    strKeyStore = other.strKeyStore;
}

/**
 * Public routine which allows for importing machine XML from an external DOM tree.
 * Use this after having called the constructor with a NULL argument.
 *
 * This is used by the OVF code if a <vbox:Machine> element has been encountered
 * in an OVF VirtualSystem element.
 *
 * @param elmMachine
 */
void MachineConfigFile::importMachineXML(const xml::ElementNode &elmMachine)
{
    // Ideally the version should be mandatory, but since VirtualBox didn't
    // care about it until 5.1 came with different defaults, there are OVF
    // files created by magicians (not using VirtualBox, which always wrote it)
    // which lack this information. Let's hope that they learn to add the
    // version when they switch to the newer settings style/defaults of 5.1.
    if (!(elmMachine.getAttributeValue("version", m->strSettingsVersionFull)))
        m->strSettingsVersionFull = VBOX_XML_IMPORT_VERSION_FULL;

    LogRel(("Import settings with version \"%s\"\n", m->strSettingsVersionFull.c_str()));

    m->sv = parseVersion(m->strSettingsVersionFull, &elmMachine);

    // remember the settings version we read in case it gets upgraded later,
    // so we know when to make backups
    m->svRead = m->sv;

    readMachine(elmMachine);
}

/**
 * Comparison operator. This gets called from Machine::saveSettings to figure out
 * whether machine settings have really changed and thus need to be written out to disk.
 *
 * Even though this is called operator==, this does NOT compare all fields; the "equals"
 * should be understood as "has the same machine config as". The following fields are
 * NOT compared:
 *      -- settings versions and file names inherited from ConfigFileBase;
 *      -- fCurrentStateModified because that is considered separately in Machine::saveSettings!!
 *
 * The "deep" comparisons marked below will invoke the operator== functions of the
 * structs defined in this file, which may in turn go into comparing lists of
 * other structures. As a result, invoking this can be expensive, but it's
 * less expensive than writing out XML to disk.
 */
bool MachineConfigFile::operator==(const MachineConfigFile &c) const
{
    return (this == &c)
        || (   uuid                       == c.uuid
            && machineUserData            == c.machineUserData
            && strStateFile               == c.strStateFile
            && uuidCurrentSnapshot        == c.uuidCurrentSnapshot
            // skip fCurrentStateModified!
            && RTTimeSpecIsEqual(&timeLastStateChange, &c.timeLastStateChange)
            && fAborted                   == c.fAborted
            && hardwareMachine            == c.hardwareMachine      // this one's deep
            && mediaRegistry              == c.mediaRegistry        // this one's deep
            // skip mapExtraDataItems! there is no old state available as it's always forced
            && llFirstSnapshot            == c.llFirstSnapshot     // this one's deep
            && recordingSettings          == c.recordingSettings   // this one's deep
            && strKeyId                   == c.strKeyId
            && strKeyStore                == c.strKeyStore
            && strStateKeyId              == c.strStateKeyId
            && strStateKeyStore           == c.strStateKeyStore
            && strLogKeyId                == c.strLogKeyId
            && strLogKeyStore             == c.strLogKeyStore);
}

/**
 * Called from MachineConfigFile::readHardware() to read CPU information.
 *
 * @param elmCpu
 * @param ll
 */
void MachineConfigFile::readCpuTree(const xml::ElementNode &elmCpu,
                                    CpuList &ll)
{
    xml::NodesLoop nl1(elmCpu, "Cpu");
    const xml::ElementNode *pelmCpu;
    while ((pelmCpu = nl1.forAllNodes()))
    {
        Cpu cpu;

        if (!pelmCpu->getAttributeValue("id", cpu.ulId))
            throw ConfigFileError(this, pelmCpu, N_("Required Cpu/@id attribute is missing"));

        ll.push_back(cpu);
    }
}

/**
 * Called from MachineConfigFile::readPlatformX86() to read x86 CPUID information.
 *
 * @param elmCpuid
 * @param ll
 */
void MachineConfigFile::readCpuIdTreeX86(const xml::ElementNode &elmCpuid,
                                         CpuIdLeafsX86List &ll)
{
    xml::NodesLoop nl1(elmCpuid, "CpuIdLeaf");
    const xml::ElementNode *pelmCpuIdLeaf;
    while ((pelmCpuIdLeaf = nl1.forAllNodes()))
    {
        CpuIdLeafX86 leaf;

        if (!pelmCpuIdLeaf->getAttributeValue("id", leaf.idx))
            throw ConfigFileError(this, pelmCpuIdLeaf, N_("Required CpuIdLeaf/@id attribute is missing"));

        if (!pelmCpuIdLeaf->getAttributeValue("subleaf", leaf.idxSub))
            leaf.idxSub = 0;
        pelmCpuIdLeaf->getAttributeValue("eax", leaf.uEax);
        pelmCpuIdLeaf->getAttributeValue("ebx", leaf.uEbx);
        pelmCpuIdLeaf->getAttributeValue("ecx", leaf.uEcx);
        pelmCpuIdLeaf->getAttributeValue("edx", leaf.uEdx);

        ll.push_back(leaf);
    }
}

/**
 * Called from MachineConfigFile::readHardware() to network information.
 * @param elmNetwork
 * @param ll
 */
void MachineConfigFile::readNetworkAdapters(const xml::ElementNode &elmNetwork,
                                            NetworkAdaptersList &ll)
{
    xml::NodesLoop nl1(elmNetwork, "Adapter");
    const xml::ElementNode *pelmAdapter;
    while ((pelmAdapter = nl1.forAllNodes()))
    {
        NetworkAdapter nic;

        if (m->sv >= SettingsVersion_v1_16)
        {
            /* Starting with VirtualBox 5.1 the default is cable connected and
             * PCnet-FAST III. Needs to match NetworkAdapter.areDefaultSettings(). */
            nic.fCableConnected = true;
            nic.type = NetworkAdapterType_Am79C973;
        }

        if (!pelmAdapter->getAttributeValue("slot", nic.ulSlot))
            throw ConfigFileError(this, pelmAdapter, N_("Required Adapter/@slot attribute is missing"));

        Utf8Str strTemp;
        if (pelmAdapter->getAttributeValue("type", strTemp))
        {
            if (strTemp == "Am79C970A")
                nic.type = NetworkAdapterType_Am79C970A;
            else if (strTemp == "Am79C973")
                nic.type = NetworkAdapterType_Am79C973;
            else if (strTemp == "Am79C960")
                nic.type = NetworkAdapterType_Am79C960;
            else if (strTemp == "82540EM")
                nic.type = NetworkAdapterType_I82540EM;
            else if (strTemp == "82543GC")
                nic.type = NetworkAdapterType_I82543GC;
            else if (strTemp == "82545EM")
                nic.type = NetworkAdapterType_I82545EM;
            else if (strTemp == "virtio")
                nic.type = NetworkAdapterType_Virtio;
            else if (strTemp == "NE1000")
                nic.type = NetworkAdapterType_NE1000;
            else if (strTemp == "NE2000")
                nic.type = NetworkAdapterType_NE2000;
            else if (strTemp == "WD8003")
                nic.type = NetworkAdapterType_WD8003;
            else if (strTemp == "WD8013")
                nic.type = NetworkAdapterType_WD8013;
            else if (strTemp == "3C503")
                nic.type = NetworkAdapterType_ELNK2;
            else if (strTemp == "3C501")
                nic.type = NetworkAdapterType_ELNK1;
            else
                throw ConfigFileError(this, pelmAdapter, N_("Invalid value '%s' in Adapter/@type attribute"), strTemp.c_str());
        }

        pelmAdapter->getAttributeValue("enabled", nic.fEnabled);
        pelmAdapter->getAttributeValue("MACAddress", nic.strMACAddress);
        pelmAdapter->getAttributeValue("cable", nic.fCableConnected);
        pelmAdapter->getAttributeValue("speed", nic.ulLineSpeed);

        if (pelmAdapter->getAttributeValue("promiscuousModePolicy", strTemp))
        {
            if (strTemp == "Deny")
                nic.enmPromiscModePolicy = NetworkAdapterPromiscModePolicy_Deny;
            else if (strTemp == "AllowNetwork")
                nic.enmPromiscModePolicy = NetworkAdapterPromiscModePolicy_AllowNetwork;
            else if (strTemp == "AllowAll")
                nic.enmPromiscModePolicy = NetworkAdapterPromiscModePolicy_AllowAll;
            else
                throw ConfigFileError(this, pelmAdapter,
                                      N_("Invalid value '%s' in Adapter/@promiscuousModePolicy attribute"), strTemp.c_str());
        }

        pelmAdapter->getAttributeValue("trace", nic.fTraceEnabled);
        pelmAdapter->getAttributeValue("tracefile", nic.strTraceFile);
        pelmAdapter->getAttributeValue("bootPriority", nic.ulBootPriority);
        pelmAdapter->getAttributeValue("bandwidthGroup", nic.strBandwidthGroup);

        xml::ElementNodesList llNetworkModes;
        pelmAdapter->getChildElements(llNetworkModes);
        xml::ElementNodesList::iterator it;
        /* We should have only active mode descriptor and disabled modes set */
        if (llNetworkModes.size() > 2)
        {
            throw ConfigFileError(this, pelmAdapter, N_("Invalid number of modes ('%d') attached to Adapter attribute"), llNetworkModes.size());
        }
        for (it = llNetworkModes.begin(); it != llNetworkModes.end(); ++it)
        {
            const xml::ElementNode *pelmNode = *it;
            if (pelmNode->nameEquals("DisabledModes"))
            {
                xml::ElementNodesList llDisabledNetworkModes;
                xml::ElementNodesList::iterator itDisabled;
                pelmNode->getChildElements(llDisabledNetworkModes);
                /* run over disabled list and load settings */
                for (itDisabled = llDisabledNetworkModes.begin();
                     itDisabled != llDisabledNetworkModes.end(); ++itDisabled)
                {
                    const xml::ElementNode *pelmDisabledNode = *itDisabled;
                    readAttachedNetworkMode(*pelmDisabledNode, false, nic);
                }
            }
            else
                readAttachedNetworkMode(*pelmNode, true, nic);
        }
        // else: default is NetworkAttachmentType_Null

        ll.push_back(nic);
    }
}

void MachineConfigFile::readAttachedNetworkMode(const xml::ElementNode &elmMode, bool fEnabled, NetworkAdapter &nic)
{
    NetworkAttachmentType_T enmAttachmentType = NetworkAttachmentType_Null;

    if (elmMode.nameEquals("NAT"))
    {
        enmAttachmentType = NetworkAttachmentType_NAT;

        elmMode.getAttributeValue("network", nic.nat.strNetwork);
        elmMode.getAttributeValue("hostip", nic.nat.strBindIP);
        elmMode.getAttributeValue("mtu", nic.nat.u32Mtu);
        elmMode.getAttributeValue("sockrcv", nic.nat.u32SockRcv);
        elmMode.getAttributeValue("socksnd", nic.nat.u32SockSnd);
        elmMode.getAttributeValue("tcprcv", nic.nat.u32TcpRcv);
        elmMode.getAttributeValue("tcpsnd", nic.nat.u32TcpSnd);
        elmMode.getAttributeValue("localhost-reachable", nic.nat.fLocalhostReachable);
        const xml::ElementNode *pelmDNS;
        if ((pelmDNS = elmMode.findChildElement("DNS")))
        {
            pelmDNS->getAttributeValue("pass-domain", nic.nat.fDNSPassDomain);
            pelmDNS->getAttributeValue("use-proxy", nic.nat.fDNSProxy);
            pelmDNS->getAttributeValue("use-host-resolver", nic.nat.fDNSUseHostResolver);
        }
        const xml::ElementNode *pelmAlias;
        if ((pelmAlias = elmMode.findChildElement("Alias")))
        {
            pelmAlias->getAttributeValue("logging", nic.nat.fAliasLog);
            pelmAlias->getAttributeValue("proxy-only", nic.nat.fAliasProxyOnly);
            pelmAlias->getAttributeValue("use-same-ports", nic.nat.fAliasUseSamePorts);
        }
        const xml::ElementNode *pelmTFTP;
        if ((pelmTFTP = elmMode.findChildElement("TFTP")))
        {
            pelmTFTP->getAttributeValue("prefix", nic.nat.strTFTPPrefix);
            pelmTFTP->getAttributeValue("boot-file", nic.nat.strTFTPBootFile);
            pelmTFTP->getAttributeValue("next-server", nic.nat.strTFTPNextServer);
        }

        readNATForwardRulesMap(elmMode, nic.nat.mapRules);
    }
    else if (   elmMode.nameEquals("HostInterface")
             || elmMode.nameEquals("BridgedInterface"))
    {
        enmAttachmentType = NetworkAttachmentType_Bridged;

        // optional network name, cannot be required or we have trouble with
        // settings which are saved before configuring the network name
        elmMode.getAttributeValue("name", nic.strBridgedName);
    }
    else if (elmMode.nameEquals("InternalNetwork"))
    {
        enmAttachmentType = NetworkAttachmentType_Internal;

        // optional network name, cannot be required or we have trouble with
        // settings which are saved before configuring the network name
        elmMode.getAttributeValue("name", nic.strInternalNetworkName);
    }
    else if (elmMode.nameEquals("HostOnlyInterface"))
    {
        enmAttachmentType = NetworkAttachmentType_HostOnly;

        // optional network name, cannot be required or we have trouble with
        // settings which are saved before configuring the network name
        elmMode.getAttributeValue("name", nic.strHostOnlyName);
    }
#ifdef VBOX_WITH_VMNET
    else if (elmMode.nameEquals("HostOnlyNetwork"))
    {
        enmAttachmentType = NetworkAttachmentType_HostOnlyNetwork;

        // optional network name, cannot be required or we have trouble with
        // settings which are saved before configuring the network name
        elmMode.getAttributeValue("name", nic.strHostOnlyNetworkName);
    }
#endif /* VBOX_WITH_VMNET */
    else if (elmMode.nameEquals("GenericInterface"))
    {
        enmAttachmentType = NetworkAttachmentType_Generic;

        elmMode.getAttributeValue("driver", nic.strGenericDriver);  // optional network attachment driver

        // get all properties
        xml::NodesLoop nl(elmMode);
        const xml::ElementNode *pelmModeChild;
        while ((pelmModeChild = nl.forAllNodes()))
        {
            if (pelmModeChild->nameEquals("Property"))
            {
                Utf8Str strPropName, strPropValue;
                if (   pelmModeChild->getAttributeValue("name", strPropName)
                    && pelmModeChild->getAttributeValue("value", strPropValue) )
                    nic.genericProperties[strPropName] = strPropValue;
                else
                    throw ConfigFileError(this, pelmModeChild, N_("Required GenericInterface/Property/@name or @value attribute is missing"));
            }
        }
    }
    else if (elmMode.nameEquals("NATNetwork"))
    {
        enmAttachmentType = NetworkAttachmentType_NATNetwork;

        // optional network name, cannot be required or we have trouble with
        // settings which are saved before configuring the network name
        elmMode.getAttributeValue("name", nic.strNATNetworkName);
    }
    else if (elmMode.nameEquals("VDE"))
    {
        // inofficial hack (VDE networking was never part of the official
        // settings, so it's not mentioned in VirtualBox-settings.xsd)
        enmAttachmentType = NetworkAttachmentType_Generic;

        com::Utf8Str strVDEName;
        elmMode.getAttributeValue("network", strVDEName);   // optional network name
        nic.strGenericDriver = "VDE";
        nic.genericProperties["network"] = strVDEName;
    }
#ifdef VBOX_WITH_VMNET
    else if (elmMode.nameEquals("HostOnlyNetwork"))
    {
        enmAttachmentType = NetworkAttachmentType_HostOnly;

        // optional network name, cannot be required or we have trouble with
        // settings which are saved before configuring the network name
        elmMode.getAttributeValue("name", nic.strHostOnlyNetworkName);
    }
#endif /* VBOX_WITH_VMNET */
#ifdef VBOX_WITH_CLOUD_NET
    else if (elmMode.nameEquals("CloudNetwork"))
    {
        enmAttachmentType = NetworkAttachmentType_Cloud;

        // optional network name, cannot be required or we have trouble with
        // settings which are saved before configuring the network name
        elmMode.getAttributeValue("name", nic.strCloudNetworkName);
    }
#endif /* VBOX_WITH_CLOUD_NET */

    if (fEnabled && enmAttachmentType != NetworkAttachmentType_Null)
        nic.mode = enmAttachmentType;
}

/**
 * Called from MachineConfigFile::readHardware() to read serial port information.
 * @param elmUART
 * @param ll
 */
void MachineConfigFile::readSerialPorts(const xml::ElementNode &elmUART,
                                        SerialPortsList &ll)
{
    xml::NodesLoop nl1(elmUART, "Port");
    const xml::ElementNode *pelmPort;
    while ((pelmPort = nl1.forAllNodes()))
    {
        SerialPort port;
        if (!pelmPort->getAttributeValue("slot", port.ulSlot))
            throw ConfigFileError(this, pelmPort, N_("Required UART/Port/@slot attribute is missing"));

        // slot must be unique
        for (SerialPortsList::const_iterator it = ll.begin();
             it != ll.end();
             ++it)
            if ((*it).ulSlot == port.ulSlot)
                throw ConfigFileError(this, pelmPort, N_("Invalid value %RU32 in UART/Port/@slot attribute: value is not unique"), port.ulSlot);

        if (!pelmPort->getAttributeValue("enabled", port.fEnabled))
            throw ConfigFileError(this, pelmPort, N_("Required UART/Port/@enabled attribute is missing"));
        if (m->sv >= SettingsVersion_v1_20) /* IOBase was changed to IOAddress since settings v1.20. */
        {
            if (!pelmPort->getAttributeValue("IOAddress", port.ulIOAddress))
               throw ConfigFileError(this, pelmPort, N_("Required UART/Port/@IOAddress attribute is missing"));
        }
        else /* Settings < v1.20. */
        {
            if (!pelmPort->getAttributeValue("IOBase", port.ulIOAddress))
               throw ConfigFileError(this, pelmPort, N_("Required UART/Port/@IOBase attribute is missing"));
        }
        if (!pelmPort->getAttributeValue("IRQ", port.ulIRQ))
            throw ConfigFileError(this, pelmPort, N_("Required UART/Port/@IRQ attribute is missing"));

        Utf8Str strPortMode;
        if (!pelmPort->getAttributeValue("hostMode", strPortMode))
            throw ConfigFileError(this, pelmPort, N_("Required UART/Port/@hostMode attribute is missing"));
        if (strPortMode == "RawFile")
            port.portMode = PortMode_RawFile;
        else if (strPortMode == "HostPipe")
            port.portMode = PortMode_HostPipe;
        else if (strPortMode == "HostDevice")
            port.portMode = PortMode_HostDevice;
        else if (strPortMode == "Disconnected")
            port.portMode = PortMode_Disconnected;
        else if (strPortMode == "TCP")
            port.portMode = PortMode_TCP;
        else
            throw ConfigFileError(this, pelmPort, N_("Invalid value '%s' in UART/Port/@hostMode attribute"), strPortMode.c_str());

        pelmPort->getAttributeValue("path", port.strPath);
        pelmPort->getAttributeValue("server", port.fServer);

        Utf8Str strUartType;
        if (pelmPort->getAttributeValue("uartType", strUartType))
        {
            if (strUartType == "16450")
                port.uartType = UartType_U16450;
            else if (strUartType == "16550A")
                port.uartType = UartType_U16550A;
            else if (strUartType == "16750")
                port.uartType = UartType_U16750;
            else
                throw ConfigFileError(this, pelmPort, N_("Invalid value '%s' in UART/Port/@uartType attribute"), strUartType.c_str());
        }

        ll.push_back(port);
    }
}

/**
 * Called from MachineConfigFile::readHardware() to read parallel port information.
 * @param elmLPT
 * @param ll
 */
void MachineConfigFile::readParallelPorts(const xml::ElementNode &elmLPT,
                                          ParallelPortsList &ll)
{
    xml::NodesLoop nl1(elmLPT, "Port");
    const xml::ElementNode *pelmPort;
    while ((pelmPort = nl1.forAllNodes()))
    {
        ParallelPort port;
        if (!pelmPort->getAttributeValue("slot", port.ulSlot))
            throw ConfigFileError(this, pelmPort, N_("Required LPT/Port/@slot attribute is missing"));

        // slot must be unique
        for (ParallelPortsList::const_iterator it = ll.begin();
             it != ll.end();
             ++it)
            if ((*it).ulSlot == port.ulSlot)
                throw ConfigFileError(this, pelmPort, N_("Invalid value %RU32 in LPT/Port/@slot attribute: value is not unique"), port.ulSlot);

        if (!pelmPort->getAttributeValue("enabled", port.fEnabled))
            throw ConfigFileError(this, pelmPort, N_("Required LPT/Port/@enabled attribute is missing"));
        if (!pelmPort->getAttributeValue("IOBase", port.ulIOBase))
            throw ConfigFileError(this, pelmPort, N_("Required LPT/Port/@IOBase attribute is missing"));
        if (!pelmPort->getAttributeValue("IRQ", port.ulIRQ))
            throw ConfigFileError(this, pelmPort, N_("Required LPT/Port/@IRQ attribute is missing"));

        pelmPort->getAttributeValue("path", port.strPath);

        ll.push_back(port);
    }
}

/**
 * Called from MachineConfigFile::readHardware() to read audio adapter information
 * and maybe fix driver information depending on the current host hardware.
 *
 * @param elmAudioAdapter "AudioAdapter" XML element.
 * @param aa
 */
void MachineConfigFile::readAudioAdapter(const xml::ElementNode &elmAudioAdapter,
                                         AudioAdapter &aa)
{
    if (m->sv >= SettingsVersion_v1_15)
    {
        // get all properties
        xml::NodesLoop nl1(elmAudioAdapter, "Property");
        const xml::ElementNode *pelmModeChild;
        while ((pelmModeChild = nl1.forAllNodes()))
        {
            Utf8Str strPropName, strPropValue;
            if (   pelmModeChild->getAttributeValue("name", strPropName)
                && pelmModeChild->getAttributeValue("value", strPropValue) )
                aa.properties[strPropName] = strPropValue;
            else
                throw ConfigFileError(this, pelmModeChild, N_("Required AudioAdapter/Property/@name or @value attribute "
                                                              "is missing"));
        }
    }

    elmAudioAdapter.getAttributeValue("enabled", aa.fEnabled);
    elmAudioAdapter.getAttributeValue("enabledIn", aa.fEnabledIn);
    elmAudioAdapter.getAttributeValue("enabledOut", aa.fEnabledOut);

    Utf8Str strTemp;
    if (elmAudioAdapter.getAttributeValue("controller", strTemp))
    {
        if (strTemp == "SB16")
            aa.controllerType = AudioControllerType_SB16;
        else if (strTemp == "AC97")
            aa.controllerType = AudioControllerType_AC97;
        else if (strTemp == "HDA")
            aa.controllerType = AudioControllerType_HDA;
        else if (strTemp == "Virtio-Sound")
            aa.controllerType = AudioControllerType_VirtioSound;
        else
            throw ConfigFileError(this, &elmAudioAdapter, N_("Invalid value '%s' in AudioAdapter/@controller attribute"), strTemp.c_str());
    }

    if (elmAudioAdapter.getAttributeValue("codec", strTemp))
    {
        if (strTemp == "SB16")
            aa.codecType = AudioCodecType_SB16;
        else if (strTemp == "STAC9700")
            aa.codecType = AudioCodecType_STAC9700;
        else if (strTemp == "AD1980")
            aa.codecType = AudioCodecType_AD1980;
        else if (strTemp == "STAC9221")
            aa.codecType = AudioCodecType_STAC9221;
        else
            throw ConfigFileError(this, &elmAudioAdapter, N_("Invalid value '%s' in AudioAdapter/@codec attribute"), strTemp.c_str());
    }
    else
    {
        /* No codec attribute provided; use defaults. */
        switch (aa.controllerType)
        {
            case AudioControllerType_AC97:
                aa.codecType = AudioCodecType_STAC9700;
                break;
            case AudioControllerType_SB16:
                aa.codecType = AudioCodecType_SB16;
                break;
            case AudioControllerType_HDA:
                aa.codecType = AudioCodecType_STAC9221;
                break;
            case AudioControllerType_VirtioSound:
                aa.codecType = AudioCodecType_Null;
                break;
            default:
                Assert(false);  /* We just checked the controller type above. */
        }
    }

    if (elmAudioAdapter.getAttributeValue("driver", strTemp))
    {
        // settings before 1.3 used lower case so make sure this is case-insensitive
        strTemp.toUpper();
        if (strTemp == "DEFAULT") /* Keep this to be backwards compatible for settings < r152556. */
            aa.driverType = AudioDriverType_Default;
        else if (strTemp == "NULL")
            aa.driverType = AudioDriverType_Null;
        else if (strTemp == "WAS")
            aa.driverType = AudioDriverType_WAS;
        else if (strTemp == "WINMM")
            aa.driverType = AudioDriverType_WinMM;
        else if ( (strTemp == "DIRECTSOUND") || (strTemp == "DSOUND") )
            aa.driverType = AudioDriverType_DirectSound;
        else if (strTemp == "SOLAUDIO") /* Deprecated -- Solaris will use OSS by default now. */
            aa.driverType = AudioDriverType_SolAudio;
        else if (strTemp == "ALSA")
            aa.driverType = AudioDriverType_ALSA;
        else if (strTemp == "PULSE")
            aa.driverType = AudioDriverType_Pulse;
        else if (strTemp == "OSS")
            aa.driverType = AudioDriverType_OSS;
        else if (strTemp == "COREAUDIO")
            aa.driverType = AudioDriverType_CoreAudio;
        else if (strTemp == "MMPM") /* Deprecated; only kept for backwards compatibility. */
            aa.driverType = AudioDriverType_MMPM;
        else
        {
            /* Be nice when loading the settings on downgraded versions: In case the selected backend isn't available / known
             * to this version, fall back to the default backend, telling the user in the release log. See @bugref{10051c7}. */
            LogRel(("WARNING: Invalid value '%s' in AudioAdapter/@driver attribute found; falling back to default audio backend\n",
                    strTemp.c_str()));
            aa.driverType = AudioDriverType_Default;
        }

        /* When loading settings >= 1.19 (VBox 7.0), the attribute "useDefault" will determine if the VM should use
         * the OS' default audio driver or not. This additional attribute is necessary in order to be backwards compatible
         * with older VBox versions. */
        bool fUseDefault = false;
        if (   elmAudioAdapter.getAttributeValue("useDefault", &fUseDefault) /* Overrides "driver" above (if set). */
            && fUseDefault)
            aa.driverType = AudioDriverType_Default;

        // now check if this is actually supported on the current host platform;
        // people might be opening a file created on a Windows host, and that
        // VM should still start on a Linux host
        if (!isAudioDriverAllowedOnThisHost(aa.driverType))
            aa.driverType = getHostDefaultAudioDriver();
    }
}

/**
 * Called from MachineConfigFile::readHardware() to read guest property information.
 * @param elmGuestProperties
 * @param hw
 */
void MachineConfigFile::readGuestProperties(const xml::ElementNode &elmGuestProperties,
                                            Hardware &hw)
{
    xml::NodesLoop nl1(elmGuestProperties, "GuestProperty");
    const xml::ElementNode *pelmProp;
    while ((pelmProp = nl1.forAllNodes()))
    {
        GuestProperty prop;

        pelmProp->getAttributeValue("name", prop.strName);
        pelmProp->getAttributeValue("value", prop.strValue);

        pelmProp->getAttributeValue("timestamp", prop.timestamp);
        pelmProp->getAttributeValue("flags", prop.strFlags);

        /* Check guest property 'name' and 'value' for correctness before
         * placing it to local cache. */

        int vrc = GuestPropValidateName(prop.strName.c_str(), prop.strName.length() + 1  /* '\0' */);
        if (RT_FAILURE(vrc))
        {
            LogRel(("WARNING: Guest property with invalid name (%s) present in VM configuration file. Guest property will be dropped.\n",
                    prop.strName.c_str()));
            continue;
        }

        vrc = GuestPropValidateValue(prop.strValue.c_str(), prop.strValue.length() + 1  /* '\0' */);
        if (vrc == VERR_TOO_MUCH_DATA)
        {
            LogRel(("WARNING: Guest property '%s' present in VM configuration file and has too long value. Guest property value will be truncated.\n",
                    prop.strName.c_str()));

            /* In order to pass validation, guest property value length (including '\0') in bytes
             * should be less than GUEST_PROP_MAX_VALUE_LEN. Chop it down to an appropriate length. */
            prop.strValue.truncate(GUEST_PROP_MAX_VALUE_LEN - 1 /*terminator*/);
        }
        else if (RT_FAILURE(vrc))
        {
            LogRel(("WARNING: Guest property '%s' present in VM configuration file and has invalid value. Guest property will be dropped.\n",
                    prop.strName.c_str()));
            continue;
        }

        hw.llGuestProperties.push_back(prop);
    }
}

/**
 * Helper function to read attributes that are common to \<SATAController\> (pre-1.7)
 * and \<StorageController\>.
 * @param elmStorageController
 * @param sctl
 */
void MachineConfigFile::readStorageControllerAttributes(const xml::ElementNode &elmStorageController,
                                                        StorageController &sctl)
{
    elmStorageController.getAttributeValue("PortCount", sctl.ulPortCount);
    elmStorageController.getAttributeValue("useHostIOCache", sctl.fUseHostIOCache);
}

/**
 * Reads the x86 CPUID tree.
 *
 * For settings >= v1.20 these were stored under the "Platform/x86/CPU" node.
 * For settings <  v1.20 these were stored under the "Hardware/CPU" node.
 *
 * @param elmPlatformOrHardware     Platform or Hardware node to read from.
 * @param platX86                   Where to store the platform settings.
 */
void MachineConfigFile::readPlatformCPUIDTreeX86(const xml::ElementNode &elmPlatformOrHardware,
                                                 PlatformX86 &platX86)
{
    const xml::ElementNode *pelmCPUChild;
    if ((pelmCPUChild = elmPlatformOrHardware.findChildElement("CpuIdTree")))
        readCpuIdTreeX86(*pelmCPUChild, platX86.llCpuIdLeafs);
}

/**
 * Reads the x86 platform settings.
 *
 * For settings >= v1.20 these were stored under the "Platform/x86" node.
 * For settings <  v1.20 these were stored under the "Hardware" node.
 *
 * @param elmPlatformX86OrHardware  Platform/x86 or Hardware node to read from.
 * @param platX86                   Where to store the x86 platform settings.
 */
void MachineConfigFile::readPlatformX86(const xml::ElementNode &elmPlatformX86OrHardware,
                                        PlatformX86 &platX86)
{
    xml::NodesLoop nl1(elmPlatformX86OrHardware);

    const xml::ElementNode *pelChild;
    while ((pelChild = nl1.forAllNodes()))
    {
        if (pelChild->nameEquals("HPET"))
        {
            pelChild->getAttributeValue("enabled", platX86.fHPETEnabled);
        }
        else if (pelChild->nameEquals("CPU"))
        {
            const xml::ElementNode *pelmCPUChild;
            if ((pelmCPUChild = pelChild->findChildElement("HardwareVirtEx")))
                pelmCPUChild->getAttributeValue("enabled", platX86.fHWVirtEx);
            if ((pelmCPUChild = pelChild->findChildElement("HardwareVirtExNestedPaging")))
                pelmCPUChild->getAttributeValue("enabled", platX86.fHWVirtExNestedPaging);
            if ((pelmCPUChild = pelChild->findChildElement("HardwareVirtExLargePages")))
                pelmCPUChild->getAttributeValue("enabled", platX86.fHWVirtExLargePages);
            if ((pelmCPUChild = pelChild->findChildElement("HardwareVirtExVPID")))
                pelmCPUChild->getAttributeValue("enabled", platX86.fHWVirtExVPID);
            if ((pelmCPUChild = pelChild->findChildElement("HardwareVirtExUX")))
                pelmCPUChild->getAttributeValue("enabled", platX86.fHWVirtExUX);
            if ((pelmCPUChild = pelChild->findChildElement("HardwareVirtForce")))
                pelmCPUChild->getAttributeValue("enabled", platX86.fHWVirtExForce);
            if ((pelmCPUChild = pelChild->findChildElement("HardwareVirtExUseNativeApi")))
                pelmCPUChild->getAttributeValue("enabled", platX86.fHWVirtExUseNativeApi);
            if ((pelmCPUChild = pelChild->findChildElement("HardwareVirtExVirtVmsaveVmload")))
                pelmCPUChild->getAttributeValue("enabled", platX86.fHWVirtExVirtVmsaveVmload);

            if (!(pelmCPUChild = pelChild->findChildElement("PAE")))
            {
                /* The default for pre 3.1 was false, so we must respect that. */
                if (m->sv < SettingsVersion_v1_9)
                    platX86.fPAE = false;
            }
            else
                pelmCPUChild->getAttributeValue("enabled", platX86.fPAE);

            bool fLongMode;
            if (   (pelmCPUChild = pelChild->findChildElement("LongMode"))
                && pelmCPUChild->getAttributeValue("enabled", fLongMode) )
                platX86.enmLongMode = fLongMode ? PlatformX86::LongMode_Enabled : PlatformX86::LongMode_Disabled;
            else
                platX86.enmLongMode = PlatformX86::LongMode_Legacy;

            if ((pelmCPUChild = pelChild->findChildElement("TripleFaultReset")))
                pelmCPUChild->getAttributeValue("enabled", platX86.fTripleFaultReset);
            if ((pelmCPUChild = pelChild->findChildElement("APIC")))
                pelmCPUChild->getAttributeValue("enabled", platX86.fAPIC);
            if ((pelmCPUChild = pelChild->findChildElement("X2APIC")))
                pelmCPUChild->getAttributeValue("enabled", platX86.fX2APIC);

            if ((pelmCPUChild = pelChild->findChildElement("IBPBOn")))
            {
                pelmCPUChild->getAttributeValue("vmexit", platX86.fIBPBOnVMExit);
                pelmCPUChild->getAttributeValue("vmentry", platX86.fIBPBOnVMEntry);
            }
            if ((pelmCPUChild = pelChild->findChildElement("SpecCtrl")))
                pelmCPUChild->getAttributeValue("enabled", platX86.fSpecCtrl);
            if ((pelmCPUChild = pelChild->findChildElement("SpecCtrlByHost")))
                pelmCPUChild->getAttributeValue("enabled", platX86.fSpecCtrlByHost);
            if ((pelmCPUChild = pelChild->findChildElement("L1DFlushOn")))
            {
                pelmCPUChild->getAttributeValue("scheduling", platX86.fL1DFlushOnSched);
                pelmCPUChild->getAttributeValue("vmentry", platX86.fL1DFlushOnVMEntry);
            }
            if ((pelmCPUChild = pelChild->findChildElement("MDSClearOn")))
            {
                pelmCPUChild->getAttributeValue("scheduling", platX86.fMDSClearOnSched);
                pelmCPUChild->getAttributeValue("vmentry", platX86.fMDSClearOnVMEntry);
            }
            if ((pelmCPUChild = pelChild->findChildElement("NestedHWVirt")))
                pelmCPUChild->getAttributeValue("enabled", platX86.fNestedHWVirt);

            readPlatformCPUIDTreeX86(*pelChild, platX86);
        }
    }
}

/**
 * Reads the platform settings.
 *
 * For settings >= v1.20 (>= VirtualBox 7.1) these were stored under the "Platform" node.
 * For settings <  v1.20 (<= VirtualBox 7.0) these were stored under the "Hardware" node.
 *
 * @param elmPlatformOrHardware     Platform or Hardware node to read from.
 * @param hw                        Where to store the hardware settings.
 * @param plat                      Where to store the platform settings.
 */
void MachineConfigFile::readPlatform(const xml::ElementNode &elmPlatformOrHardware,
                                     Hardware &hw, Platform &plat)
{
    /*
     * Platform-generic stuff.
     */
    xml::NodesLoop nl1(elmPlatformOrHardware);
    const xml::ElementNode *pelmChild;
    while ((pelmChild = nl1.forAllNodes()))
    {
        if (pelmChild->nameEquals("CPU"))
        {
            if (!pelmChild->getAttributeValue("count", hw.cCPUs))
            {
                // pre-1.5 variant; not sure if this actually exists in the wild anywhere
                const xml::ElementNode *pelmCPUChild;
                if ((pelmCPUChild = pelmChild->findChildElement("CPUCount")))
                    pelmCPUChild->getAttributeValue("count", hw.cCPUs);
            }

            pelmChild->getAttributeValue("hotplug", hw.fCpuHotPlug);
            pelmChild->getAttributeValue("executionCap", hw.ulCpuExecutionCap);

            const xml::ElementNode *pelmCPUChild;
            if (hw.fCpuHotPlug)
            {
                if ((pelmCPUChild = pelmChild->findChildElement("CpuTree")))
                    readCpuTree(*pelmCPUChild, hw.llCpus);
            }

            if ((pelmCPUChild = pelmChild->findChildElement("SyntheticCpu")))
            {
                bool fSyntheticCpu = false;
                pelmCPUChild->getAttributeValue("enabled", fSyntheticCpu);
                hw.uCpuIdPortabilityLevel = fSyntheticCpu ? 1 : 0;
            }
            pelmChild->getAttributeValue("CpuIdPortabilityLevel", hw.uCpuIdPortabilityLevel);
            pelmChild->getAttributeValue("CpuProfile", hw.strCpuProfile);
        }
        else if (pelmChild->nameEquals("Chipset"))
        {
            Utf8Str strChipsetType;
            if (pelmChild->getAttributeValue("type", strChipsetType))
            {
                if (strChipsetType == "PIIX3")
                    plat.chipsetType = ChipsetType_PIIX3;
                else if (strChipsetType == "ICH9")
                    plat.chipsetType = ChipsetType_ICH9;
                else if (strChipsetType == "ARMv8Virtual")
                    plat.chipsetType = ChipsetType_ARMv8Virtual;
                else
                    throw ConfigFileError(this,
                                          pelmChild,
                                          N_("Invalid value '%s' in Chipset/@type"),
                                          strChipsetType.c_str());
            }
        }
        else if (pelmChild->nameEquals("Iommu"))
        {
            Utf8Str strIommuType;
            if (pelmChild->getAttributeValue("type", strIommuType))
            {
                if (strIommuType == "None")
                    plat.iommuType = IommuType_None;
                else if (strIommuType == "Automatic")
                    plat.iommuType = IommuType_Automatic;
                else if (strIommuType == "AMD")
                    plat.iommuType = IommuType_AMD;
                else if (strIommuType == "Intel")
                    plat.iommuType = IommuType_Intel;
                else
                    throw ConfigFileError(this,
                                          pelmChild,
                                          N_("Invalid value '%s' in Iommu/@type"),
                                          strIommuType.c_str());
            }
        }
        else if (pelmChild->nameEquals("RTC"))
        {
            Utf8Str strLocalOrUTC;
            plat.fRTCUseUTC =    pelmChild->getAttributeValue("localOrUTC", strLocalOrUTC)
                              && strLocalOrUTC == "UTC";
        }
    }

    if (m->sv >= SettingsVersion_v1_20) /* Settings v1.20 introduced platform architecture support. */
    {
        Utf8Str strArch;
        if (elmPlatformOrHardware.getAttributeValue("architecture", strArch))
        {
            if (strArch.equalsIgnoreCase("x86"))
                plat.architectureType = PlatformArchitecture_x86;
            else if (strArch.equalsIgnoreCase("ARM"))
                plat.architectureType = PlatformArchitecture_ARM;
            else
                throw ConfigFileError(this,
                                      &elmPlatformOrHardware,
                                      N_("Platform/@architecture '%s' invalid"), strArch.c_str());
        }
        else
            throw ConfigFileError(this, &elmPlatformOrHardware, N_("Platform/@architecture missing"));
    }

    /*
     * Platform-specific stuff.
     */
    switch (plat.architectureType)
    {
        case PlatformArchitecture_x86:
        {
            const xml::ElementNode *pelmPlatformX86OrHardware;
            if (m->sv >= SettingsVersion_v1_20) /* Settings v1.20 introduced platform support with a x86 sub node. */
            {
                pelmPlatformX86OrHardware = elmPlatformOrHardware.findChildElement("x86");
                if (!pelmPlatformX86OrHardware)
                    throw ConfigFileError(this, &elmPlatformOrHardware, N_("Required Platform/@x86 element is missing"));
            }
            else /* For settings < v1.20 we stored everyhing in the Hardware node. */
                pelmPlatformX86OrHardware = &elmPlatformOrHardware;

            readPlatformX86(*pelmPlatformX86OrHardware, plat.x86);
            break;
        }

#ifdef VBOX_WITH_VIRT_ARMV8
        case PlatformArchitecture_ARM:
        {
            /* Nothing here yet -- add ARM-specific stuff as soon as we have it. */
            break;
        }
#endif
        default:
            break;
    }
}

/**
 * Reads in a \<Hardware\> block and stores it in the given structure. Used
 * both directly from readMachine and from readSnapshot, since snapshots
 * have their own hardware sections.
 *
 * For legacy pre-1.7 settings we also need a storage structure because
 * the IDE and SATA controllers used to be defined under \<Hardware\>.
 *
 * @param elmHardware           Hardware node to read from.
 * @param hw                    Where to store the hardware settings.
 */
void MachineConfigFile::readHardware(const xml::ElementNode &elmHardware,
                                     Hardware &hw)
{
    if (m->sv >= SettingsVersion_v1_16)
    {
        /* Starting with VirtualBox 5.1 the default is Default, before it was
         * Legacy. This needs to matched by areParavirtDefaultSettings(). */
        hw.paravirtProvider = ParavirtProvider_Default;
        /* The new default is disabled, before it was enabled by default. */
        hw.vrdeSettings.fEnabled = false;
        /* The new default is disabled, before it was enabled by default. */
        hw.audioAdapter.fEnabled = false;
    }

    if (m->sv >= SettingsVersion_v1_17)
    {
        /* Starting with VirtualBox 5.2 the default is disabled, before it was
         * enabled. This needs to matched by AudioAdapter::areDefaultSettings(). */
        hw.audioAdapter.fEnabledIn = false;
        /* The new default is disabled, before it was enabled by default. */
        hw.audioAdapter.fEnabledOut = false;
    }

    if (!elmHardware.getAttributeValue("version", hw.strVersion))
    {
        /* KLUDGE ALERT!  For a while during the 3.1 development this was not
           written because it was thought to have a default value of "2".  For
           sv <= 1.3 it defaults to "1" because the attribute didn't exist,
           while for 1.4+ it is sort of mandatory.  Now, the buggy XML writer
           code only wrote 1.7 and later.  So, if it's a 1.7+ XML file and it's
           missing the hardware version, then it probably should be "2" instead
           of "1". */
        if (m->sv < SettingsVersion_v1_7)
            hw.strVersion = "1";
        else
            hw.strVersion = "2";
    }
    Utf8Str strUUID;
    if (elmHardware.getAttributeValue("uuid", strUUID))
        parseUUID(hw.uuid, strUUID, &elmHardware);

    xml::NodesLoop nl1(elmHardware);
    const xml::ElementNode *pelmHwChild;
    while ((pelmHwChild = nl1.forAllNodes()))
    {
        if (pelmHwChild->nameEquals("Memory"))
        {
            pelmHwChild->getAttributeValue("RAMSize", hw.ulMemorySizeMB);
            pelmHwChild->getAttributeValue("PageFusion", hw.fPageFusionEnabled);
        }
        else if (pelmHwChild->nameEquals("HID"))
        {
            Utf8Str strHIDType;
            if (pelmHwChild->getAttributeValue("Keyboard", strHIDType))
            {
                if (strHIDType == "None")
                    hw.keyboardHIDType = KeyboardHIDType_None;
                else if (strHIDType == "USBKeyboard")
                    hw.keyboardHIDType = KeyboardHIDType_USBKeyboard;
                else if (strHIDType == "PS2Keyboard")
                    hw.keyboardHIDType = KeyboardHIDType_PS2Keyboard;
                else if (strHIDType == "ComboKeyboard")
                    hw.keyboardHIDType = KeyboardHIDType_ComboKeyboard;
                else
                    throw ConfigFileError(this,
                                          pelmHwChild,
                                          N_("Invalid value '%s' in HID/Keyboard/@type"),
                                          strHIDType.c_str());
            }
            if (pelmHwChild->getAttributeValue("Pointing", strHIDType))
            {
                 if (strHIDType == "None")
                    hw.pointingHIDType = PointingHIDType_None;
                else if (strHIDType == "USBMouse")
                    hw.pointingHIDType = PointingHIDType_USBMouse;
                else if (strHIDType == "USBTablet")
                    hw.pointingHIDType = PointingHIDType_USBTablet;
                else if (strHIDType == "PS2Mouse")
                    hw.pointingHIDType = PointingHIDType_PS2Mouse;
                else if (strHIDType == "ComboMouse")
                    hw.pointingHIDType = PointingHIDType_ComboMouse;
                else if (strHIDType == "USBMultiTouch")
                    hw.pointingHIDType = PointingHIDType_USBMultiTouch;
                else if (strHIDType == "USBMTScreenPlusPad")
                    hw.pointingHIDType = PointingHIDType_USBMultiTouchScreenPlusPad;
                else
                    throw ConfigFileError(this,
                                          pelmHwChild,
                                          N_("Invalid value '%s' in HID/Pointing/@type"),
                                          strHIDType.c_str());
            }
        }
        else if (pelmHwChild->nameEquals("Paravirt"))
        {
            Utf8Str strProvider;
            if (pelmHwChild->getAttributeValue("provider", strProvider))
            {
                if (strProvider == "None")
                    hw.paravirtProvider = ParavirtProvider_None;
                else if (strProvider == "Default")
                    hw.paravirtProvider = ParavirtProvider_Default;
                else if (strProvider == "Legacy")
                    hw.paravirtProvider = ParavirtProvider_Legacy;
                else if (strProvider == "Minimal")
                    hw.paravirtProvider = ParavirtProvider_Minimal;
                else if (strProvider == "HyperV")
                    hw.paravirtProvider = ParavirtProvider_HyperV;
                else if (strProvider == "KVM")
                    hw.paravirtProvider = ParavirtProvider_KVM;
                else
                    throw ConfigFileError(this,
                                          pelmHwChild,
                                          N_("Invalid value '%s' in Paravirt/@provider attribute"),
                                          strProvider.c_str());
            }

            pelmHwChild->getAttributeValue("debug", hw.strParavirtDebug);
        }
        else if (pelmHwChild->nameEquals("Boot"))
        {
            hw.mapBootOrder.clear();

            xml::NodesLoop nl2(*pelmHwChild, "Order");
            const xml::ElementNode *pelmOrder;
            while ((pelmOrder = nl2.forAllNodes()))
            {
                uint32_t ulPos;
                Utf8Str strDevice;
                if (!pelmOrder->getAttributeValue("position", ulPos))
                    throw ConfigFileError(this, pelmOrder, N_("Required Boot/Order/@position attribute is missing"));

                if (    ulPos < 1
                     || ulPos > SchemaDefs::MaxBootPosition
                   )
                    throw ConfigFileError(this,
                                          pelmOrder,
                                          N_("Invalid value '%RU32' in Boot/Order/@position: must be greater than 0 and less than %RU32"),
                                          ulPos,
                                          SchemaDefs::MaxBootPosition + 1);
                // XML is 1-based but internal data is 0-based
                --ulPos;

                if (hw.mapBootOrder.find(ulPos) != hw.mapBootOrder.end())
                    throw ConfigFileError(this, pelmOrder, N_("Invalid value '%RU32' in Boot/Order/@position: value is not unique"), ulPos);

                if (!pelmOrder->getAttributeValue("device", strDevice))
                    throw ConfigFileError(this, pelmOrder, N_("Required Boot/Order/@device attribute is missing"));

                DeviceType_T type;
                if (strDevice == "None")
                    type = DeviceType_Null;
                else if (strDevice == "Floppy")
                    type = DeviceType_Floppy;
                else if (strDevice == "DVD")
                    type = DeviceType_DVD;
                else if (strDevice == "HardDisk")
                    type = DeviceType_HardDisk;
                else if (strDevice == "Network")
                    type = DeviceType_Network;
                else
                    throw ConfigFileError(this, pelmOrder, N_("Invalid value '%s' in Boot/Order/@device attribute"), strDevice.c_str());
                hw.mapBootOrder[ulPos] = type;
            }
        }
        else if (pelmHwChild->nameEquals("Display"))
        {
            Utf8Str strGraphicsControllerType;
            if (!pelmHwChild->getAttributeValue("controller", strGraphicsControllerType))
                hw.graphicsAdapter.graphicsControllerType = GraphicsControllerType_VBoxVGA;
            else
            {
                strGraphicsControllerType.toUpper();
                GraphicsControllerType_T type;
                if (strGraphicsControllerType == "VBOXVGA")
                    type = GraphicsControllerType_VBoxVGA;
                else if (strGraphicsControllerType == "VMSVGA")
                    type = GraphicsControllerType_VMSVGA;
                else if (strGraphicsControllerType == "VBOXSVGA")
                    type = GraphicsControllerType_VBoxSVGA;
                else if (strGraphicsControllerType == "QEMURAMFB")
                    type = GraphicsControllerType_QemuRamFB;
                else if (strGraphicsControllerType == "NONE")
                    type = GraphicsControllerType_Null;
                else
                    throw ConfigFileError(this, pelmHwChild, N_("Invalid value '%s' in Display/@controller attribute"), strGraphicsControllerType.c_str());
                hw.graphicsAdapter.graphicsControllerType = type;
            }
            pelmHwChild->getAttributeValue("VRAMSize", hw.graphicsAdapter.ulVRAMSizeMB);
            if (!pelmHwChild->getAttributeValue("monitorCount", hw.graphicsAdapter.cMonitors))
                pelmHwChild->getAttributeValue("MonitorCount", hw.graphicsAdapter.cMonitors);       // pre-v1.5 variant
            if (!pelmHwChild->getAttributeValue("accelerate3D", hw.graphicsAdapter.fAccelerate3D))
                pelmHwChild->getAttributeValue("Accelerate3D", hw.graphicsAdapter.fAccelerate3D);   // pre-v1.5 variant
            pelmHwChild->getAttributeValue("accelerate2DVideo", hw.graphicsAdapter.fAccelerate2DVideo);
        }
        else if (pelmHwChild->nameEquals("RemoteDisplay"))
        {
            pelmHwChild->getAttributeValue("enabled", hw.vrdeSettings.fEnabled);

            Utf8Str str;
            if (pelmHwChild->getAttributeValue("port", str))
                hw.vrdeSettings.mapProperties["TCP/Ports"] = str;
            if (pelmHwChild->getAttributeValue("netAddress", str))
                hw.vrdeSettings.mapProperties["TCP/Address"] = str;

            Utf8Str strAuthType;
            if (pelmHwChild->getAttributeValue("authType", strAuthType))
            {
                // settings before 1.3 used lower case so make sure this is case-insensitive
                strAuthType.toUpper();
                if (strAuthType == "NULL")
                    hw.vrdeSettings.authType = AuthType_Null;
                else if (strAuthType == "GUEST")
                    hw.vrdeSettings.authType = AuthType_Guest;
                else if (strAuthType == "EXTERNAL")
                    hw.vrdeSettings.authType = AuthType_External;
                else
                    throw ConfigFileError(this, pelmHwChild, N_("Invalid value '%s' in RemoteDisplay/@authType attribute"), strAuthType.c_str());
            }

            pelmHwChild->getAttributeValue("authLibrary", hw.vrdeSettings.strAuthLibrary);
            pelmHwChild->getAttributeValue("authTimeout", hw.vrdeSettings.ulAuthTimeout);
            pelmHwChild->getAttributeValue("allowMultiConnection", hw.vrdeSettings.fAllowMultiConnection);
            pelmHwChild->getAttributeValue("reuseSingleConnection", hw.vrdeSettings.fReuseSingleConnection);

            /* 3.2 and 4.0 betas, 4.0 has this information in VRDEProperties. */
            const xml::ElementNode *pelmVideoChannel;
            if ((pelmVideoChannel = pelmHwChild->findChildElement("VideoChannel")))
            {
                bool fVideoChannel = false;
                pelmVideoChannel->getAttributeValue("enabled", fVideoChannel);
                hw.vrdeSettings.mapProperties["VideoChannel/Enabled"] = fVideoChannel? "true": "false";

                uint32_t ulVideoChannelQuality = 75;
                pelmVideoChannel->getAttributeValue("quality", ulVideoChannelQuality);
                ulVideoChannelQuality = RT_CLAMP(ulVideoChannelQuality, 10, 100);
                char *pszBuffer = NULL;
                if (RTStrAPrintf(&pszBuffer, "%d", ulVideoChannelQuality) >= 0)
                {
                    hw.vrdeSettings.mapProperties["VideoChannel/Quality"] = pszBuffer;
                    RTStrFree(pszBuffer);
                }
                else
                    hw.vrdeSettings.mapProperties["VideoChannel/Quality"] = "75";
            }
            pelmHwChild->getAttributeValue("VRDEExtPack", hw.vrdeSettings.strVrdeExtPack);

            const xml::ElementNode *pelmProperties = pelmHwChild->findChildElement("VRDEProperties");
            if (pelmProperties != NULL)
            {
                xml::NodesLoop nl(*pelmProperties);
                const xml::ElementNode *pelmProperty;
                while ((pelmProperty = nl.forAllNodes()))
                {
                    if (pelmProperty->nameEquals("Property"))
                    {
                        /* <Property name="TCP/Ports" value="3000-3002"/> */
                        Utf8Str strName, strValue;
                        if (   pelmProperty->getAttributeValue("name", strName)
                            && pelmProperty->getAttributeValue("value", strValue))
                            hw.vrdeSettings.mapProperties[strName] = strValue;
                        else
                            throw ConfigFileError(this, pelmProperty, N_("Required VRDE Property/@name or @value attribute is missing"));
                    }
                }
            }
        }
        /* Since v1.20 (VBox 7.1) we store BIOS / UEFI settings inside the "Firmware" element.
         * Older versions (< v1.20) stored those settings in a "BIOS" element (see down below) for that. See also down below. */
        else if (   pelmHwChild->nameEquals("BIOS")
                 || pelmHwChild->nameEquals("Firmware"))
        {
            /* For settings <  v1.20 we already has an element "Firmware", which only contained the firmware type. The rest lived in the "BIOS" element.
             * For settings >= v1.20 we moved the "BIOS" element attributes also to "Firmware". */
            if (pelmHwChild->nameEquals("Firmware"))
            {
                Utf8Str strFirmwareType;
                if (pelmHwChild->getAttributeValue("type", strFirmwareType))
                {
                    if (    (strFirmwareType == "BIOS")
                         || (strFirmwareType == "1")                // some trunk builds used the number here
                       )
                        hw.firmwareSettings.firmwareType = FirmwareType_BIOS;
                    else if (    (strFirmwareType == "EFI")
                              || (strFirmwareType == "2")           // some trunk builds used the number here
                            )
                        hw.firmwareSettings.firmwareType = FirmwareType_EFI;
                    else if (    strFirmwareType == "EFI32")
                        hw.firmwareSettings.firmwareType = FirmwareType_EFI32;
                    else if (    strFirmwareType == "EFI64")
                        hw.firmwareSettings.firmwareType = FirmwareType_EFI64;
                    else if (    strFirmwareType == "EFIDUAL")
                        hw.firmwareSettings.firmwareType = FirmwareType_EFIDUAL;
                    else
                        throw ConfigFileError(this,
                                              pelmHwChild,
                                              N_("Invalid value '%s' in Firmware/@type"),
                                              strFirmwareType.c_str());
                }
            }

            /* Read the firmware settings either from the "Firmware" or "BIOS" elements, depending on the settings version. */
            if (   (   pelmHwChild->nameEquals("Firmware")
                    && m->sv >= SettingsVersion_v1_20)
                || (   pelmHwChild->nameEquals("BIOS")
                    && m->sv < SettingsVersion_v1_20)
               )
            {
                const xml::ElementNode *pelmFirmwareOrBIOSChild;
                if ((pelmFirmwareOrBIOSChild = pelmHwChild->findChildElement("ACPI")))
                    pelmFirmwareOrBIOSChild->getAttributeValue("enabled", hw.firmwareSettings.fACPIEnabled);
                if ((pelmFirmwareOrBIOSChild = pelmHwChild->findChildElement("IOAPIC")))
                    pelmFirmwareOrBIOSChild->getAttributeValue("enabled", hw.firmwareSettings.fIOAPICEnabled);
                if ((pelmFirmwareOrBIOSChild = pelmHwChild->findChildElement("APIC")))
                {
                    Utf8Str strAPIC;
                    if (pelmFirmwareOrBIOSChild->getAttributeValue("mode", strAPIC))
                    {
                        strAPIC.toUpper();
                        if (strAPIC == "DISABLED")
                            hw.firmwareSettings.apicMode = APICMode_Disabled;
                        else if (strAPIC == "APIC")
                            hw.firmwareSettings.apicMode = APICMode_APIC;
                        else if (strAPIC == "X2APIC")
                            hw.firmwareSettings.apicMode = APICMode_X2APIC;
                        else
                            throw ConfigFileError(this, pelmFirmwareOrBIOSChild, N_("Invalid value '%s' in APIC/@mode attribute"), strAPIC.c_str());
                    }
                }
                if ((pelmFirmwareOrBIOSChild = pelmHwChild->findChildElement("Logo")))
                {
                    pelmFirmwareOrBIOSChild->getAttributeValue("fadeIn", hw.firmwareSettings.fLogoFadeIn);
                    pelmFirmwareOrBIOSChild->getAttributeValue("fadeOut", hw.firmwareSettings.fLogoFadeOut);
                    pelmFirmwareOrBIOSChild->getAttributeValue("displayTime", hw.firmwareSettings.ulLogoDisplayTime);
                    pelmFirmwareOrBIOSChild->getAttributeValue("imagePath", hw.firmwareSettings.strLogoImagePath);
                }
                if ((pelmFirmwareOrBIOSChild = pelmHwChild->findChildElement("BootMenu")))
                {
                    Utf8Str strBootMenuMode;
                    if (pelmFirmwareOrBIOSChild->getAttributeValue("mode", strBootMenuMode))
                    {
                        // settings before 1.3 used lower case so make sure this is case-insensitive
                        strBootMenuMode.toUpper();
                        if (strBootMenuMode == "DISABLED")
                            hw.firmwareSettings.enmBootMenuMode = FirmwareBootMenuMode_Disabled;
                        else if (strBootMenuMode == "MENUONLY")
                            hw.firmwareSettings.enmBootMenuMode = FirmwareBootMenuMode_MenuOnly;
                        else if (strBootMenuMode == "MESSAGEANDMENU")
                            hw.firmwareSettings.enmBootMenuMode = FirmwareBootMenuMode_MessageAndMenu;
                        else
                            throw ConfigFileError(this, pelmFirmwareOrBIOSChild, N_("Invalid value '%s' in BootMenu/@mode attribute"), strBootMenuMode.c_str());
                    }
                }
                if ((pelmFirmwareOrBIOSChild = pelmHwChild->findChildElement("PXEDebug")))
                    pelmFirmwareOrBIOSChild->getAttributeValue("enabled", hw.firmwareSettings.fPXEDebugEnabled);
                if ((pelmFirmwareOrBIOSChild = pelmHwChild->findChildElement("TimeOffset")))
                    pelmFirmwareOrBIOSChild->getAttributeValue("value", hw.firmwareSettings.llTimeOffset);
                if ((pelmFirmwareOrBIOSChild = pelmHwChild->findChildElement("NVRAM")))
                {
                    pelmFirmwareOrBIOSChild->getAttributeValue("path", hw.nvramSettings.strNvramPath);
                    if (m->sv >= SettingsVersion_v1_19)
                    {
                        pelmFirmwareOrBIOSChild->getAttributeValue("keyId", hw.nvramSettings.strKeyId);
                        pelmFirmwareOrBIOSChild->getAttributeValue("keyStore", hw.nvramSettings.strKeyStore);
                    }
                }
                if ((pelmFirmwareOrBIOSChild = pelmHwChild->findChildElement("SmbiosUuidLittleEndian")))
                    pelmFirmwareOrBIOSChild->getAttributeValue("enabled", hw.firmwareSettings.fSmbiosUuidLittleEndian);
                else
                    hw.firmwareSettings.fSmbiosUuidLittleEndian = false; /* Default for existing VMs. */

                if ((pelmFirmwareOrBIOSChild = pelmHwChild->findChildElement("AutoSerialNumGen")))
                    pelmFirmwareOrBIOSChild->getAttributeValue("enabled", hw.firmwareSettings.fAutoSerialNumGen);
                else
                    hw.firmwareSettings.fAutoSerialNumGen = false; /* Default for existing VMs. */

                // legacy BIOS/IDEController (pre 1.7)
                if (    (m->sv < SettingsVersion_v1_7)
                     && (pelmFirmwareOrBIOSChild = pelmHwChild->findChildElement("IDEController"))
                   )
                {
                    StorageController sctl;
                    sctl.strName = "IDE Controller";
                    sctl.storageBus = StorageBus_IDE;

                    Utf8Str strType;
                    if (pelmFirmwareOrBIOSChild->getAttributeValue("type", strType))
                    {
                        if (strType == "PIIX3")
                            sctl.controllerType = StorageControllerType_PIIX3;
                        else if (strType == "PIIX4")
                            sctl.controllerType = StorageControllerType_PIIX4;
                        else if (strType == "ICH6")
                            sctl.controllerType = StorageControllerType_ICH6;
                        else
                            throw ConfigFileError(this, pelmFirmwareOrBIOSChild, N_("Invalid value '%s' for IDEController/@type attribute"), strType.c_str());
                    }
                    sctl.ulPortCount = 2;
                    hw.storage.llStorageControllers.push_back(sctl);
                }
            }
        }
        else if (pelmHwChild->nameEquals("TrustedPlatformModule"))
        {
            Utf8Str strTpmType;
            if (pelmHwChild->getAttributeValue("type", strTpmType))
            {
                if (strTpmType == "None")
                    hw.tpmSettings.tpmType = TpmType_None;
                else if (strTpmType == "v1_2")
                    hw.tpmSettings.tpmType = TpmType_v1_2;
                else if (strTpmType == "v2_0")
                    hw.tpmSettings.tpmType = TpmType_v2_0;
                else if (strTpmType == "Host")
                    hw.tpmSettings.tpmType = TpmType_Host;
                else if (strTpmType == "Swtpm")
                    hw.tpmSettings.tpmType = TpmType_Swtpm;
                else
                    throw ConfigFileError(this,
                                          pelmHwChild,
                                          N_("Invalid value '%s' in TrustedPlatformModule/@type"),
                                          strTpmType.c_str());
            }

            pelmHwChild->getAttributeValue("location", hw.tpmSettings.strLocation);
        }
        else if (   (m->sv <= SettingsVersion_v1_14)
                 && pelmHwChild->nameEquals("USBController"))
        {
            bool fEnabled = false;

            pelmHwChild->getAttributeValue("enabled", fEnabled);
            if (fEnabled)
            {
                /* Create OHCI controller with default name. */
                USBController ctrl;

                ctrl.strName = "OHCI";
                ctrl.enmType = USBControllerType_OHCI;
                hw.usbSettings.llUSBControllers.push_back(ctrl);
            }

            pelmHwChild->getAttributeValue("enabledEhci", fEnabled);
            if (fEnabled)
            {
                /* Create OHCI controller with default name. */
                USBController ctrl;

                ctrl.strName = "EHCI";
                ctrl.enmType = USBControllerType_EHCI;
                hw.usbSettings.llUSBControllers.push_back(ctrl);
            }

            readUSBDeviceFilters(*pelmHwChild,
                                 hw.usbSettings.llDeviceFilters);
        }
        else if (pelmHwChild->nameEquals("USB"))
        {
            const xml::ElementNode *pelmUSBChild;

            if ((pelmUSBChild = pelmHwChild->findChildElement("Controllers")))
            {
                xml::NodesLoop nl2(*pelmUSBChild, "Controller");
                const xml::ElementNode *pelmCtrl;

                while ((pelmCtrl = nl2.forAllNodes()))
                {
                    USBController ctrl;
                    com::Utf8Str strCtrlType;

                    pelmCtrl->getAttributeValue("name", ctrl.strName);

                    if (pelmCtrl->getAttributeValue("type", strCtrlType))
                    {
                        if (strCtrlType == "OHCI")
                            ctrl.enmType = USBControllerType_OHCI;
                        else if (strCtrlType == "EHCI")
                            ctrl.enmType = USBControllerType_EHCI;
                        else if (strCtrlType == "XHCI")
                            ctrl.enmType = USBControllerType_XHCI;
                        else
                            throw ConfigFileError(this, pelmCtrl, N_("Invalid value '%s' for Controller/@type attribute"), strCtrlType.c_str());
                    }

                    hw.usbSettings.llUSBControllers.push_back(ctrl);
                }
            }

            if ((pelmUSBChild = pelmHwChild->findChildElement("DeviceFilters")))
                readUSBDeviceFilters(*pelmUSBChild, hw.usbSettings.llDeviceFilters);
        }
        else if (   m->sv < SettingsVersion_v1_7
                 && pelmHwChild->nameEquals("SATAController"))
        {
            bool f;
            if (   pelmHwChild->getAttributeValue("enabled", f)
                && f)
            {
                StorageController sctl;
                sctl.strName = "SATA Controller";
                sctl.storageBus = StorageBus_SATA;
                sctl.controllerType = StorageControllerType_IntelAhci;

                readStorageControllerAttributes(*pelmHwChild, sctl);

                hw.storage.llStorageControllers.push_back(sctl);
            }
        }
        else if (pelmHwChild->nameEquals("Network"))
            readNetworkAdapters(*pelmHwChild, hw.llNetworkAdapters);
        else if (    pelmHwChild->nameEquals("UART")
                  || pelmHwChild->nameEquals("Uart")      // used before 1.3
                )
            readSerialPorts(*pelmHwChild, hw.llSerialPorts);
        else if (    pelmHwChild->nameEquals("LPT")
                  || pelmHwChild->nameEquals("Lpt")       // used before 1.3
                )
            readParallelPorts(*pelmHwChild, hw.llParallelPorts);
        else if (pelmHwChild->nameEquals("AudioAdapter"))
            readAudioAdapter(*pelmHwChild, hw.audioAdapter);
        else if (pelmHwChild->nameEquals("SharedFolders"))
        {
            xml::NodesLoop nl2(*pelmHwChild, "SharedFolder");
            const xml::ElementNode *pelmFolder;
            while ((pelmFolder = nl2.forAllNodes()))
            {
                SharedFolder sf;
                pelmFolder->getAttributeValue("name", sf.strName);
                pelmFolder->getAttributeValue("hostPath", sf.strHostPath);
                pelmFolder->getAttributeValue("writable", sf.fWritable);
                pelmFolder->getAttributeValue("autoMount", sf.fAutoMount);
                pelmFolder->getAttributeValue("autoMountPoint", sf.strAutoMountPoint);

                Utf8Str strTemp;
                if (pelmFolder->getAttributeValue("symlinkPolicy", strTemp))
                {
                    if (strTemp == "forbidden")
                        sf.enmSymlinkPolicy = SymlinkPolicy_Forbidden;
                    else if (strTemp == "subtree")
                        sf.enmSymlinkPolicy = SymlinkPolicy_AllowedInShareSubtree;
                    else if (strTemp == "relative")
                        sf.enmSymlinkPolicy = SymlinkPolicy_AllowedToRelativeTargets;
                    else if (strTemp == "any")
                        sf.enmSymlinkPolicy = SymlinkPolicy_AllowedToAnyTarget;
                    else
                        throw ConfigFileError(this,
                                              pelmHwChild,
                                              N_("Invalid value '%s' in SharedFolder/@symlinkPolicy attribute"),
                                              strTemp.c_str());
                }
                hw.llSharedFolders.push_back(sf);
            }
        }
        else if (pelmHwChild->nameEquals("Clipboard"))
        {
            Utf8Str strTemp;
            if (pelmHwChild->getAttributeValue("mode", strTemp))
            {
                if (strTemp == "Disabled")
                    hw.clipboardMode = ClipboardMode_Disabled;
                else if (strTemp == "HostToGuest")
                    hw.clipboardMode = ClipboardMode_HostToGuest;
                else if (strTemp == "GuestToHost")
                    hw.clipboardMode = ClipboardMode_GuestToHost;
                else if (strTemp == "Bidirectional")
                    hw.clipboardMode = ClipboardMode_Bidirectional;
                else
                    throw ConfigFileError(this, pelmHwChild, N_("Invalid value '%s' in Clipboard/@mode attribute"), strTemp.c_str());
            }

            pelmHwChild->getAttributeValue("fileTransfersEnabled", hw.fClipboardFileTransfersEnabled);
        }
        else if (pelmHwChild->nameEquals("DragAndDrop"))
        {
            Utf8Str strTemp;
            if (pelmHwChild->getAttributeValue("mode", strTemp))
            {
                if (strTemp == "Disabled")
                    hw.dndMode = DnDMode_Disabled;
                else if (strTemp == "HostToGuest")
                    hw.dndMode = DnDMode_HostToGuest;
                else if (strTemp == "GuestToHost")
                    hw.dndMode = DnDMode_GuestToHost;
                else if (strTemp == "Bidirectional")
                    hw.dndMode = DnDMode_Bidirectional;
                else
                    throw ConfigFileError(this, pelmHwChild, N_("Invalid value '%s' in DragAndDrop/@mode attribute"), strTemp.c_str());
            }
        }
        else if (pelmHwChild->nameEquals("Guest"))
        {
            if (!pelmHwChild->getAttributeValue("memoryBalloonSize", hw.ulMemoryBalloonSize))
                pelmHwChild->getAttributeValue("MemoryBalloonSize", hw.ulMemoryBalloonSize);            // used before 1.3
        }
        else if (pelmHwChild->nameEquals("GuestProperties"))
            readGuestProperties(*pelmHwChild, hw);
        else if (pelmHwChild->nameEquals("IO"))
        {
            const xml::ElementNode *pelmBwGroups;
            const xml::ElementNode *pelmIOChild;

            if ((pelmIOChild = pelmHwChild->findChildElement("IoCache")))
            {
                pelmIOChild->getAttributeValue("enabled", hw.ioSettings.fIOCacheEnabled);
                pelmIOChild->getAttributeValue("size", hw.ioSettings.ulIOCacheSize);
            }

            if ((pelmBwGroups = pelmHwChild->findChildElement("BandwidthGroups")))
            {
                xml::NodesLoop nl2(*pelmBwGroups, "BandwidthGroup");
                const xml::ElementNode *pelmBandwidthGroup;
                while ((pelmBandwidthGroup = nl2.forAllNodes()))
                {
                    BandwidthGroup gr;
                    Utf8Str strTemp;

                    pelmBandwidthGroup->getAttributeValue("name", gr.strName);

                    if (pelmBandwidthGroup->getAttributeValue("type", strTemp))
                    {
                        if (strTemp == "Disk")
                            gr.enmType = BandwidthGroupType_Disk;
                        else if (strTemp == "Network")
                            gr.enmType = BandwidthGroupType_Network;
                        else
                            throw ConfigFileError(this, pelmBandwidthGroup, N_("Invalid value '%s' in BandwidthGroup/@type attribute"), strTemp.c_str());
                    }
                    else
                        throw ConfigFileError(this, pelmBandwidthGroup, N_("Missing BandwidthGroup/@type attribute"));

                    if (!pelmBandwidthGroup->getAttributeValue("maxBytesPerSec", gr.cMaxBytesPerSec))
                    {
                        pelmBandwidthGroup->getAttributeValue("maxMbPerSec", gr.cMaxBytesPerSec);
                        gr.cMaxBytesPerSec *= _1M;
                    }
                    hw.ioSettings.llBandwidthGroups.push_back(gr);
                }
            }
        }
        else if (pelmHwChild->nameEquals("HostPci"))
        {
            const xml::ElementNode *pelmDevices;

            if ((pelmDevices = pelmHwChild->findChildElement("Devices")))
            {
                xml::NodesLoop nl2(*pelmDevices, "Device");
                const xml::ElementNode *pelmDevice;
                while ((pelmDevice = nl2.forAllNodes()))
                {
                    HostPCIDeviceAttachment hpda;

                    if (!pelmDevice->getAttributeValue("host", hpda.uHostAddress))
                         throw ConfigFileError(this, pelmDevice, N_("Missing Device/@host attribute"));

                    if (!pelmDevice->getAttributeValue("guest", hpda.uGuestAddress))
                         throw ConfigFileError(this, pelmDevice, N_("Missing Device/@guest attribute"));

                    /* name is optional */
                    pelmDevice->getAttributeValue("name", hpda.strDeviceName);

                    hw.pciAttachments.push_back(hpda);
                }
            }
        }
        else if (pelmHwChild->nameEquals("EmulatedUSB"))
        {
            const xml::ElementNode *pelmCardReader;

            if ((pelmCardReader = pelmHwChild->findChildElement("CardReader")))
            {
                pelmCardReader->getAttributeValue("enabled", hw.fEmulatedUSBCardReader);
            }
        }
        else if (pelmHwChild->nameEquals("Frontend"))
        {
            const xml::ElementNode *pelmDefault;

            if ((pelmDefault = pelmHwChild->findChildElement("Default")))
            {
                pelmDefault->getAttributeValue("type", hw.strDefaultFrontend);
            }
        }
        else if (pelmHwChild->nameEquals("StorageControllers"))
            readStorageControllers(*pelmHwChild, hw.storage);
    }

    /* We read the platform settings here, which were part of the "Hardware" node for settings < 1.2.0.
     * For newer settings (>= v1.20) we read the platform settings outside readHardware(). */
    if (m->sv < SettingsVersion_v1_20)
        readPlatform(elmHardware, hw, hw.platformSettings);

    if (hw.ulMemorySizeMB == (uint32_t)-1)
        throw ConfigFileError(this, &elmHardware, N_("Required Memory/@RAMSize element/attribute is missing"));
}

/**
 * This gets called instead of readStorageControllers() for legacy pre-1.7 settings
 * files which have a \<HardDiskAttachments\> node and storage controller settings
 * hidden in the \<Hardware\> settings. We set the StorageControllers fields just the
 * same, just from different sources.
 * @param elmHardDiskAttachments  \<HardDiskAttachments\> XML node.
 * @param strg
 */
void MachineConfigFile::readHardDiskAttachments_pre1_7(const xml::ElementNode &elmHardDiskAttachments,
                                                       Storage &strg)
{
    StorageController *pIDEController = NULL;
    StorageController *pSATAController = NULL;

    for (StorageControllersList::iterator it = strg.llStorageControllers.begin();
         it != strg.llStorageControllers.end();
         ++it)
    {
        StorageController &s = *it;
        if (s.storageBus == StorageBus_IDE)
            pIDEController = &s;
        else if (s.storageBus == StorageBus_SATA)
            pSATAController = &s;
    }

    xml::NodesLoop nl1(elmHardDiskAttachments, "HardDiskAttachment");
    const xml::ElementNode *pelmAttachment;
    while ((pelmAttachment = nl1.forAllNodes()))
    {
        AttachedDevice att;
        Utf8Str strUUID, strBus;

        if (!pelmAttachment->getAttributeValue("hardDisk", strUUID))
            throw ConfigFileError(this, pelmAttachment, N_("Required HardDiskAttachment/@hardDisk attribute is missing"));
        parseUUID(att.uuid, strUUID, pelmAttachment);

        if (!pelmAttachment->getAttributeValue("bus", strBus))
            throw ConfigFileError(this, pelmAttachment, N_("Required HardDiskAttachment/@bus attribute is missing"));
        // pre-1.7 'channel' is now port
        if (!pelmAttachment->getAttributeValue("channel", att.lPort))
            throw ConfigFileError(this, pelmAttachment, N_("Required HardDiskAttachment/@channel attribute is missing"));
        // pre-1.7 'device' is still device
        if (!pelmAttachment->getAttributeValue("device", att.lDevice))
            throw ConfigFileError(this, pelmAttachment, N_("Required HardDiskAttachment/@device attribute is missing"));

        att.deviceType = DeviceType_HardDisk;

        if (strBus == "IDE")
        {
            if (!pIDEController)
                throw ConfigFileError(this, pelmAttachment, N_("HardDiskAttachment/@bus is 'IDE' but cannot find IDE controller"));
            pIDEController->llAttachedDevices.push_back(att);
        }
        else if (strBus == "SATA")
        {
            if (!pSATAController)
                throw ConfigFileError(this, pelmAttachment, N_("HardDiskAttachment/@bus is 'SATA' but cannot find SATA controller"));
            pSATAController->llAttachedDevices.push_back(att);
        }
        else
            throw ConfigFileError(this, pelmAttachment, N_("HardDiskAttachment/@bus attribute has illegal value '%s'"), strBus.c_str());
    }
}

/**
 * Reads in a \<StorageControllers\> block and stores it in the given Storage structure.
 * Used both directly from readMachine and from readSnapshot, since snapshots
 * have their own storage controllers sections.
 *
 * This is only called for settings version 1.7 and above; see readHardDiskAttachments_pre1_7()
 * for earlier versions.
 *
 * @param   elmStorageControllers
 * @param   strg
 */
void MachineConfigFile::readStorageControllers(const xml::ElementNode &elmStorageControllers,
                                               Storage &strg)
{
    xml::NodesLoop nlStorageControllers(elmStorageControllers, "StorageController");
    const xml::ElementNode *pelmController;
    while ((pelmController = nlStorageControllers.forAllNodes()))
    {
        StorageController sctl;

        if (!pelmController->getAttributeValue("name", sctl.strName))
            throw ConfigFileError(this, pelmController, N_("Required StorageController/@name attribute is missing"));
        //  canonicalize storage controller names for configs in the switchover
        //  period.
        if (m->sv < SettingsVersion_v1_9)
        {
            if (sctl.strName == "IDE")
                sctl.strName = "IDE Controller";
            else if (sctl.strName == "SATA")
                sctl.strName = "SATA Controller";
            else if (sctl.strName == "SCSI")
                sctl.strName = "SCSI Controller";
        }

        pelmController->getAttributeValue("Instance", sctl.ulInstance);
            // default from constructor is 0

        pelmController->getAttributeValue("Bootable", sctl.fBootable);
            // default from constructor is true which is true
            // for settings below version 1.11 because they allowed only
            // one controller per type.

        Utf8Str strType;
        if (!pelmController->getAttributeValue("type", strType))
            throw ConfigFileError(this, pelmController, N_("Required StorageController/@type attribute is missing"));

        if (strType == "AHCI")
        {
            sctl.storageBus = StorageBus_SATA;
            sctl.controllerType = StorageControllerType_IntelAhci;
        }
        else if (strType == "LsiLogic")
        {
            sctl.storageBus = StorageBus_SCSI;
            sctl.controllerType = StorageControllerType_LsiLogic;
        }
        else if (strType == "BusLogic")
        {
            sctl.storageBus = StorageBus_SCSI;
            sctl.controllerType = StorageControllerType_BusLogic;
        }
        else if (strType == "PIIX3")
        {
            sctl.storageBus = StorageBus_IDE;
            sctl.controllerType = StorageControllerType_PIIX3;
        }
        else if (strType == "PIIX4")
        {
            sctl.storageBus = StorageBus_IDE;
            sctl.controllerType = StorageControllerType_PIIX4;
        }
        else if (strType == "ICH6")
        {
            sctl.storageBus = StorageBus_IDE;
            sctl.controllerType = StorageControllerType_ICH6;
        }
        else if (   (m->sv >= SettingsVersion_v1_9)
                 && (strType == "I82078")
                )
        {
            sctl.storageBus = StorageBus_Floppy;
            sctl.controllerType = StorageControllerType_I82078;
        }
        else if (strType == "LsiLogicSas")
        {
            sctl.storageBus = StorageBus_SAS;
            sctl.controllerType = StorageControllerType_LsiLogicSas;
        }
        else if (strType == "USB")
        {
            sctl.storageBus = StorageBus_USB;
            sctl.controllerType = StorageControllerType_USB;
        }
        else if (strType == "NVMe")
        {
            sctl.storageBus = StorageBus_PCIe;
            sctl.controllerType = StorageControllerType_NVMe;
        }
        else if (strType == "VirtioSCSI")
        {
            sctl.storageBus = StorageBus_VirtioSCSI;
            sctl.controllerType = StorageControllerType_VirtioSCSI;
        }
        else
            throw ConfigFileError(this, pelmController, N_("Invalid value '%s' for StorageController/@type attribute"), strType.c_str());

        readStorageControllerAttributes(*pelmController, sctl);

        xml::NodesLoop nlAttached(*pelmController, "AttachedDevice");
        const xml::ElementNode *pelmAttached;
        while ((pelmAttached = nlAttached.forAllNodes()))
        {
            AttachedDevice att;
            Utf8Str strTemp;
            pelmAttached->getAttributeValue("type", strTemp);

            att.fDiscard = false;
            att.fNonRotational = false;
            att.fHotPluggable = false;
            att.fPassThrough = false;

            if (strTemp == "HardDisk")
            {
                att.deviceType = DeviceType_HardDisk;
                pelmAttached->getAttributeValue("nonrotational", att.fNonRotational);
                pelmAttached->getAttributeValue("discard", att.fDiscard);
            }
            else if (m->sv >= SettingsVersion_v1_9)
            {
                // starting with 1.9 we list DVD and floppy drive info + attachments under <StorageControllers>
                if (strTemp == "DVD")
                {
                    att.deviceType = DeviceType_DVD;
                    pelmAttached->getAttributeValue("passthrough", att.fPassThrough);
                    pelmAttached->getAttributeValue("tempeject", att.fTempEject);
                }
                else if (strTemp == "Floppy")
                    att.deviceType = DeviceType_Floppy;
            }

            if (att.deviceType != DeviceType_Null)
            {
                const xml::ElementNode *pelmImage;
                // all types can have images attached, but for HardDisk it's required
                if (!(pelmImage = pelmAttached->findChildElement("Image")))
                {
                    if (att.deviceType == DeviceType_HardDisk)
                        throw ConfigFileError(this, pelmImage, N_("Required AttachedDevice/Image element is missing"));
                    else
                    {
                        // DVDs and floppies can also have <HostDrive> instead of <Image>
                        const xml::ElementNode *pelmHostDrive;
                        if ((pelmHostDrive = pelmAttached->findChildElement("HostDrive")))
                            if (!pelmHostDrive->getAttributeValue("src", att.strHostDriveSrc))
                                throw ConfigFileError(this, pelmHostDrive, N_("Required AttachedDevice/HostDrive/@src attribute is missing"));
                    }
                }
                else
                {
                    if (!pelmImage->getAttributeValue("uuid", strTemp))
                        throw ConfigFileError(this, pelmImage, N_("Required AttachedDevice/Image/@uuid attribute is missing"));
                    parseUUID(att.uuid, strTemp, pelmImage);
                }

                if (!pelmAttached->getAttributeValue("port", att.lPort))
                    throw ConfigFileError(this, pelmImage, N_("Required AttachedDevice/@port attribute is missing"));
                if (!pelmAttached->getAttributeValue("device", att.lDevice))
                    throw ConfigFileError(this, pelmImage, N_("Required AttachedDevice/@device attribute is missing"));

                /* AHCI controller ports are hotpluggable by default, keep compatibility with existing settings. */
                if (m->sv >= SettingsVersion_v1_15)
                    pelmAttached->getAttributeValue("hotpluggable", att.fHotPluggable);
                else if (sctl.controllerType == StorageControllerType_IntelAhci)
                    att.fHotPluggable = true;

                pelmAttached->getAttributeValue("bandwidthGroup", att.strBwGroup);
                sctl.llAttachedDevices.push_back(att);
            }
        }

        strg.llStorageControllers.push_back(sctl);
    }
}

/**
 * This gets called for legacy pre-1.9 settings files after having parsed the
 * \<Hardware\> and \<StorageControllers\> sections to parse \<Hardware\> once more
 * for the \<DVDDrive\> and \<FloppyDrive\> sections.
 *
 * Before settings version 1.9, DVD and floppy drives were specified separately
 * under \<Hardware\>; we then need this extra loop to make sure the storage
 * controller structs are already set up so we can add stuff to them.
 *
 * @param elmHardware
 * @param strg
 */
void MachineConfigFile::readDVDAndFloppies_pre1_9(const xml::ElementNode &elmHardware,
                                                  Storage &strg)
{
    xml::NodesLoop nl1(elmHardware);
    const xml::ElementNode *pelmHwChild;
    while ((pelmHwChild = nl1.forAllNodes()))
    {
        if (pelmHwChild->nameEquals("DVDDrive"))
        {
            // create a DVD "attached device" and attach it to the existing IDE controller
            AttachedDevice att;
            att.deviceType = DeviceType_DVD;
            // legacy DVD drive is always secondary master (port 1, device 0)
            att.lPort = 1;
            att.lDevice = 0;
            pelmHwChild->getAttributeValue("passthrough", att.fPassThrough);
            pelmHwChild->getAttributeValue("tempeject", att.fTempEject);

            const xml::ElementNode *pDriveChild;
            Utf8Str strTmp;
            if (   (pDriveChild = pelmHwChild->findChildElement("Image")) != NULL
                && pDriveChild->getAttributeValue("uuid", strTmp))
                parseUUID(att.uuid, strTmp, pDriveChild);
            else if ((pDriveChild = pelmHwChild->findChildElement("HostDrive")))
                pDriveChild->getAttributeValue("src", att.strHostDriveSrc);

            // find the IDE controller and attach the DVD drive
            bool fFound = false;
            for (StorageControllersList::iterator it = strg.llStorageControllers.begin();
                 it != strg.llStorageControllers.end();
                 ++it)
            {
                StorageController &sctl = *it;
                if (sctl.storageBus == StorageBus_IDE)
                {
                    sctl.llAttachedDevices.push_back(att);
                    fFound = true;
                    break;
                }
            }

            if (!fFound)
                throw ConfigFileError(this, pelmHwChild, N_("Internal error: found DVD drive but IDE controller does not exist"));
                        // shouldn't happen because pre-1.9 settings files always had at least one IDE controller in the settings
                        // which should have gotten parsed in <StorageControllers> before this got called
        }
        else if (pelmHwChild->nameEquals("FloppyDrive"))
        {
            bool fEnabled;
            if (   pelmHwChild->getAttributeValue("enabled", fEnabled)
                && fEnabled)
            {
                // create a new floppy controller and attach a floppy "attached device"
                StorageController sctl;
                sctl.strName = "Floppy Controller";
                sctl.storageBus = StorageBus_Floppy;
                sctl.controllerType = StorageControllerType_I82078;
                sctl.ulPortCount = 1;

                AttachedDevice att;
                att.deviceType = DeviceType_Floppy;
                att.lPort = 0;
                att.lDevice = 0;

                const xml::ElementNode *pDriveChild;
                Utf8Str strTmp;
                if (   (pDriveChild = pelmHwChild->findChildElement("Image"))
                    && pDriveChild->getAttributeValue("uuid", strTmp) )
                    parseUUID(att.uuid, strTmp, pDriveChild);
                else if ((pDriveChild = pelmHwChild->findChildElement("HostDrive")))
                    pDriveChild->getAttributeValue("src", att.strHostDriveSrc);

                // store attachment with controller
                sctl.llAttachedDevices.push_back(att);
                // store controller with storage
                strg.llStorageControllers.push_back(sctl);
            }
        }
    }
}

/**
 * Called for reading the \<Teleporter\> element under \<Machine\>.
 */
void MachineConfigFile::readTeleporter(const xml::ElementNode &elmTeleporter,
                                       MachineUserData &userData)
{
    elmTeleporter.getAttributeValue("enabled", userData.fTeleporterEnabled);
    elmTeleporter.getAttributeValue("port", userData.uTeleporterPort);
    elmTeleporter.getAttributeValue("address", userData.strTeleporterAddress);
    elmTeleporter.getAttributeValue("password", userData.strTeleporterPassword);

    if (   userData.strTeleporterPassword.isNotEmpty()
        && !VBoxIsPasswordHashed(&userData.strTeleporterPassword))
        VBoxHashPassword(&userData.strTeleporterPassword);
}

/**
 * Called for reading the \<Debugging\> element under \<Machine\> or \<Snapshot\>.
 */
void MachineConfigFile::readDebugging(const xml::ElementNode &elmDebugging, Debugging &dbg)
{
    if (m->sv < SettingsVersion_v1_13)
        return;

    const xml::ElementNode *pelmTracing = elmDebugging.findChildElement("Tracing");
    if (pelmTracing)
    {
        pelmTracing->getAttributeValue("enabled", dbg.fTracingEnabled);
        pelmTracing->getAttributeValue("allowTracingToAccessVM", dbg.fAllowTracingToAccessVM);
        pelmTracing->getAttributeValue("config", dbg.strTracingConfig);
    }

    const xml::ElementNode *pelmGuestDebug = elmDebugging.findChildElement("GuestDebug");
    if (pelmGuestDebug)
    {
        Utf8Str strTmp;
        pelmGuestDebug->getAttributeValue("provider", strTmp);
        if (strTmp == "None")
            dbg.enmDbgProvider = GuestDebugProvider_None;
        else if (strTmp == "GDB")
            dbg.enmDbgProvider = GuestDebugProvider_GDB;
        else if (strTmp == "KD")
            dbg.enmDbgProvider = GuestDebugProvider_KD;
        else
            throw ConfigFileError(this, pelmGuestDebug, N_("Invalid value '%s' for GuestDebug/@provider attribute"), strTmp.c_str());

        pelmGuestDebug->getAttributeValue("io", strTmp);
        if (strTmp == "None")
            dbg.enmIoProvider = GuestDebugIoProvider_None;
        else if (strTmp == "TCP")
            dbg.enmIoProvider = GuestDebugIoProvider_TCP;
        else if (strTmp == "UDP")
            dbg.enmIoProvider = GuestDebugIoProvider_UDP;
        else if (strTmp == "IPC")
            dbg.enmIoProvider = GuestDebugIoProvider_IPC;
        else
            throw ConfigFileError(this, pelmGuestDebug, N_("Invalid value '%s' for GuestDebug/@io attribute"), strTmp.c_str());

        pelmGuestDebug->getAttributeValue("address", dbg.strAddress);
        pelmGuestDebug->getAttributeValue("port", dbg.ulPort);
    }
}

/**
 * Called for reading the \<Autostart\> element under \<Machine\> or \<Snapshot\>.
 */
void MachineConfigFile::readAutostart(const xml::ElementNode &elmAutostart, Autostart &autostrt)
{
    Utf8Str strAutostop;

    if (m->sv < SettingsVersion_v1_13)
        return;

    elmAutostart.getAttributeValue("enabled", autostrt.fAutostartEnabled);
    elmAutostart.getAttributeValue("delay", autostrt.uAutostartDelay);
    elmAutostart.getAttributeValue("autostop", strAutostop);
    if (strAutostop == "Disabled")
        autostrt.enmAutostopType = AutostopType_Disabled;
    else if (strAutostop == "SaveState")
        autostrt.enmAutostopType = AutostopType_SaveState;
    else if (strAutostop == "PowerOff")
        autostrt.enmAutostopType = AutostopType_PowerOff;
    else if (strAutostop == "AcpiShutdown")
        autostrt.enmAutostopType = AutostopType_AcpiShutdown;
    else
        throw ConfigFileError(this, &elmAutostart, N_("Invalid value '%s' for Autostart/@autostop attribute"), strAutostop.c_str());
}

/**
 * Called for reading the \<VideoCapture\> element under \<Machine|Hardware\>,
 * or \<Recording\> under \<Machine\>,
 */
void MachineConfigFile::readRecordingSettings(const xml::ElementNode &elmRecording, uint32_t cMonitors, Recording &Settings)
{
    if (cMonitors > 64)
        throw ConfigFileError(this, &elmRecording, N_("Invalid monitor count given"));

    elmRecording.getAttributeValue("enabled", Settings.common.fEnabled);

    /* Note: Since settings 1.19 the recording settings have a dedicated XML branch "Recording" outside of "Hardware". */
    if (m->sv >= SettingsVersion_v1_19 /* VBox >= 7.0 */)
    {
        uint32_t cScreens = 0;
        elmRecording.getAttributeValue("screens",   cScreens);

        xml::ElementNodesList plstScreens;
        elmRecording.getChildElements(plstScreens, "Screen");

        /* Sanity checks. */
        if (cScreens != plstScreens.size())
            throw ConfigFileError(this, &elmRecording, N_("Recording/@screens attribute does not match stored screen objects"));
        if (cScreens > 64)
            throw ConfigFileError(this, &elmRecording, N_("Recording/@screens attribute is invalid"));

        for (xml::ElementNodesList::iterator itScreen  = plstScreens.begin();
                                             itScreen != plstScreens.end();
                                           ++itScreen)
        {
            /* The screen's stored ID is the monitor ID and also the key for the map. */
            uint32_t idxScreen;
            (*itScreen)->getAttributeValue("id",        idxScreen);

            RecordingScreen &screenSettings = Settings.mapScreens[idxScreen];

            (*itScreen)->getAttributeValue("enabled",   screenSettings.fEnabled);
            Utf8Str strTemp;
            (*itScreen)->getAttributeValue("featuresEnabled", strTemp);
            RecordingScreen::featuresFromString(strTemp, screenSettings.featureMap);
            (*itScreen)->getAttributeValue("maxTimeS",  screenSettings.ulMaxTimeS);
            (*itScreen)->getAttributeValue("options",   screenSettings.strOptions);
            (*itScreen)->getAttributeValue("dest",      (uint32_t &)screenSettings.enmDest);
            if (screenSettings.enmDest == RecordingDestination_File)
                (*itScreen)->getAttributeValuePath("file",  screenSettings.File.strName);
            else
                throw ConfigFileError(this, (*itScreen),
                                      N_("Not supported Recording/@dest attribute '%#x'"), screenSettings.enmDest);
            (*itScreen)->getAttributeValue("maxSizeMB", screenSettings.File.ulMaxSizeMB);
            if ((*itScreen)->getAttributeValue("videoCodec", strTemp)) /* Stick with default if not set. */
                RecordingScreen::videoCodecFromString(strTemp, screenSettings.Video.enmCodec);
            (*itScreen)->getAttributeValue("videoDeadline", (uint32_t &)screenSettings.Video.enmDeadline);
            (*itScreen)->getAttributeValue("videoRateCtlMode", (uint32_t &)screenSettings.Video.enmRateCtlMode);
            (*itScreen)->getAttributeValue("videoScalingMode", (uint32_t &)screenSettings.Video.enmScalingMode);
            (*itScreen)->getAttributeValue("horzRes",       screenSettings.Video.ulWidth);
            (*itScreen)->getAttributeValue("vertRes",       screenSettings.Video.ulHeight);
            (*itScreen)->getAttributeValue("rateKbps",      screenSettings.Video.ulRate);
            (*itScreen)->getAttributeValue("fps",           screenSettings.Video.ulFPS);

            if ((*itScreen)->getAttributeValue("audioCodec", strTemp)) /* Stick with default if not set. */
                RecordingScreen::audioCodecFromString(strTemp, screenSettings.Audio.enmCodec);
            (*itScreen)->getAttributeValue("audioDeadline", (uint32_t &)screenSettings.Audio.enmDeadline);
            (*itScreen)->getAttributeValue("audioRateCtlMode", (uint32_t &)screenSettings.Audio.enmRateCtlMode);
            (*itScreen)->getAttributeValue("audioHz",       (uint32_t &)screenSettings.Audio.uHz);
            (*itScreen)->getAttributeValue("audioBits",     (uint32_t &)screenSettings.Audio.cBits);
            (*itScreen)->getAttributeValue("audioChannels", (uint32_t &)screenSettings.Audio.cChannels);
        }
    }
    else if (   m->sv >= SettingsVersion_v1_14
             && m->sv <  SettingsVersion_v1_19 /* VBox < 7.0 */)
    {
        /* For settings < 1.19 (< VBox 7.0) we only support one recording configuration, that is,
         * all screens have the same configuration. So load/save to/from screen 0. */
        RecordingScreen &screen0 = Settings.mapScreens[0];

        elmRecording.getAttributeValue("maxTime",   screen0.ulMaxTimeS);
        elmRecording.getAttributeValue("options",   screen0.strOptions);
        elmRecording.getAttributeValuePath("file",  screen0.File.strName);
        elmRecording.getAttributeValue("maxSize",   screen0.File.ulMaxSizeMB);
        elmRecording.getAttributeValue("horzRes",   screen0.Video.ulWidth);
        elmRecording.getAttributeValue("vertRes",   screen0.Video.ulHeight);
        elmRecording.getAttributeValue("rate",      screen0.Video.ulRate);
        elmRecording.getAttributeValue("fps",       screen0.Video.ulFPS);

        /* Convert the enabled screens to the former uint64_t bit array and vice versa. */
        uint64_t uScreensBitmap = 0;
        elmRecording.getAttributeValue("screens",   uScreensBitmap);

        /* Note: For settings < 1.19 the "screens" attribute is a bit field for all screens
         *       which are ENABLED for recording. The settings for recording are for all the same though. */
        for (unsigned i = 0; i < cMonitors; i++)
        {
            /* Apply settings of screen 0 to screen i and enable it. */
            Settings.mapScreens[i] = screen0;

            /* Screen i enabled? */
            Settings.mapScreens[i].idScreen = i;
            Settings.mapScreens[i].fEnabled = RT_BOOL(uScreensBitmap & RT_BIT_64(i));
        }
    }
}

/**
 * Called for reading the \<Groups\> element under \<Machine\>.
 */
void MachineConfigFile::readGroups(const xml::ElementNode &elmGroups, StringsList &llGroups)
{
    llGroups.clear();
    if (m->sv < SettingsVersion_v1_13)
    {
        llGroups.push_back("/");
        return;
    }

    xml::NodesLoop nlGroups(elmGroups);
    const xml::ElementNode *pelmGroup;
    while ((pelmGroup = nlGroups.forAllNodes()))
    {
        if (pelmGroup->nameEquals("Group"))
        {
            Utf8Str strGroup;
            if (!pelmGroup->getAttributeValue("name", strGroup))
                throw ConfigFileError(this, pelmGroup, N_("Required Group/@name attribute is missing"));
            llGroups.push_back(strGroup);
        }
    }
}

/**
 * Called initially for the \<Snapshot\> element under \<Machine\>, if present,
 * to store the snapshot's data into the given Snapshot structure (which is
 * then the one in the Machine struct). This might process further elements
 * of the snapshot tree if a \<Snapshots\> (plural) element is found in the
 * snapshot, which should contain a list of child snapshots; such lists are
 * maintained in the Snapshot structure.
 *
 * @param curSnapshotUuid
 * @param elmSnapshot
 * @param snap
 * @returns true if curSnapshotUuid is in this snapshot subtree, otherwise false
 */
bool MachineConfigFile::readSnapshot(const Guid &curSnapshotUuid,
                                     const xml::ElementNode &elmSnapshot,
                                     Snapshot &snap)
{
    std::list<const xml::ElementNode *> llElementsTodo;
    llElementsTodo.push_back(&elmSnapshot);
    std::list<Snapshot *> llSettingsTodo;
    llSettingsTodo.push_back(&snap);
    std::list<uint32_t> llDepthsTodo;
    llDepthsTodo.push_back(1);

    bool foundCurrentSnapshot = false;

    while (llElementsTodo.size() > 0)
    {
        const xml::ElementNode *pElement = llElementsTodo.front();
        llElementsTodo.pop_front();
        Snapshot *pSnap = llSettingsTodo.front();
        llSettingsTodo.pop_front();
        uint32_t depth = llDepthsTodo.front();
        llDepthsTodo.pop_front();

        if (depth > SETTINGS_SNAPSHOT_DEPTH_MAX)
            throw ConfigFileError(this, pElement, N_("Maximum snapshot tree depth of %u exceeded"), SETTINGS_SNAPSHOT_DEPTH_MAX);

        Utf8Str strTemp;
        if (!pElement->getAttributeValue("uuid", strTemp))
            throw ConfigFileError(this, pElement, N_("Required Snapshot/@uuid attribute is missing"));
        parseUUID(pSnap->uuid, strTemp, pElement);
        foundCurrentSnapshot |= (pSnap->uuid == curSnapshotUuid);

        if (!pElement->getAttributeValue("name", pSnap->strName))
            throw ConfigFileError(this, pElement, N_("Required Snapshot/@name attribute is missing"));

        // 3.1 dev builds added Description as an attribute, read it silently
        // and write it back as an element
        pElement->getAttributeValue("Description", pSnap->strDescription);

        if (!pElement->getAttributeValue("timeStamp", strTemp))
            throw ConfigFileError(this, pElement, N_("Required Snapshot/@timeStamp attribute is missing"));
        parseTimestamp(pSnap->timestamp, strTemp, pElement);

        pElement->getAttributeValuePath("stateFile", pSnap->strStateFile);      // online snapshots only

        /* We read the platform settings here for newer settings (>= v1.20).
         * For older settings (< v1.20) we read the platform settings in readHardware(). */
        if (m->sv >= SettingsVersion_v1_20)
        {
            const xml::ElementNode *pelmPlatform;
            if (!(pelmPlatform = pElement->findChildElement("Platform")))
                throw ConfigFileError(this, pElement, N_("Required Snapshot/@Platform element is missing"));
            readPlatform(*pelmPlatform, pSnap->hardware, pSnap->hardware.platformSettings);
        }

        // parse Hardware before the other elements because other things depend on it
        const xml::ElementNode *pelmHardware;
        if (!(pelmHardware = pElement->findChildElement("Hardware")))
            throw ConfigFileError(this, pElement, N_("Required Snapshot/@Hardware element is missing"));
        readHardware(*pelmHardware, pSnap->hardware);

        const xml::ElementNode *pelmSnapshots = NULL;

        xml::NodesLoop nlSnapshotChildren(*pElement);
        const xml::ElementNode *pelmSnapshotChild;
        while ((pelmSnapshotChild = nlSnapshotChildren.forAllNodes()))
        {
            if (pelmSnapshotChild->nameEquals("Description"))
                pSnap->strDescription = pelmSnapshotChild->getValue();
            else if (   m->sv < SettingsVersion_v1_7
                    && pelmSnapshotChild->nameEquals("HardDiskAttachments"))
                readHardDiskAttachments_pre1_7(*pelmSnapshotChild, pSnap->hardware.storage);
            else if (   m->sv >= SettingsVersion_v1_7
                    && pelmSnapshotChild->nameEquals("StorageControllers"))
                readStorageControllers(*pelmSnapshotChild, pSnap->hardware.storage);
            else if (pelmSnapshotChild->nameEquals("Snapshots"))
            {
                if (pelmSnapshots)
                    throw ConfigFileError(this, pelmSnapshotChild, N_("Just a single Snapshots element is allowed"));
                pelmSnapshots = pelmSnapshotChild;
            }
        }

        if (m->sv < SettingsVersion_v1_9)
            // go through Hardware once more to repair the settings controller structures
            // with data from old DVDDrive and FloppyDrive elements
            readDVDAndFloppies_pre1_9(*pelmHardware, pSnap->hardware.storage);

        const xml::ElementNode *pelmDebugging = elmSnapshot.findChildElement("Debugging"); /** @todo r=andy Shouldn't this be pElement instead of elmSnapshot? Re-visit this! */
        if (pelmDebugging)
            readDebugging(*pelmDebugging, pSnap->debugging);
        const xml::ElementNode *pelmAutostart = elmSnapshot.findChildElement("Autostart"); /** @todo r=andy Ditto. */
        if (pelmAutostart)
            readAutostart(*pelmAutostart, pSnap->autostart);
        if (m->sv < SettingsVersion_v1_19)
        {
            const xml::ElementNode *pelmVideoCapture = pElement->findChildElement("VideoCapture");
            if (pelmVideoCapture)
                readRecordingSettings(*pelmVideoCapture, pSnap->hardware.graphicsAdapter.cMonitors, pSnap->recordingSettings);
        }
        else /* >= VBox 7.0 */
        {
            const xml::ElementNode *pelmRecording = pElement->findChildElement("Recording");
            if (pelmRecording)
                readRecordingSettings(*pelmRecording, pSnap->hardware.graphicsAdapter.cMonitors, pSnap->recordingSettings);
        }
        // note: Groups exist only for Machine, not for Snapshot

        // process all child snapshots
        if (pelmSnapshots)
        {
            xml::NodesLoop nlChildSnapshots(*pelmSnapshots);
            const xml::ElementNode *pelmChildSnapshot;
            while ((pelmChildSnapshot = nlChildSnapshots.forAllNodes()))
            {
                if (pelmChildSnapshot->nameEquals("Snapshot"))
                {
                    llElementsTodo.push_back(pelmChildSnapshot);
                    pSnap->llChildSnapshots.push_back(Snapshot::Empty);
                    llSettingsTodo.push_back(&pSnap->llChildSnapshots.back());
                    llDepthsTodo.push_back(depth + 1);
                }
            }
        }
    }

    return foundCurrentSnapshot;
}

static struct
{
    const char *pcszOld;
    const char *pcszNew;
} const g_aConvertGuestOSTypesPre1_5[] =
{
    { "unknown",        GUEST_OS_ID_STR_X86("Other") },
    { "dos",            GUEST_OS_ID_STR_X86("DOS") },
    { "win31",          GUEST_OS_ID_STR_X86("Windows31") },
    { "win95",          GUEST_OS_ID_STR_X86("Windows95") },
    { "win98",          GUEST_OS_ID_STR_X86("Windows98") },
    { "winme",          GUEST_OS_ID_STR_X86("WindowsMe") },
    { "winnt4",         GUEST_OS_ID_STR_X86("WindowsNT4") },
    { "win2k",          GUEST_OS_ID_STR_X86("Windows2000") },
    { "winxp",          GUEST_OS_ID_STR_X86("WindowsXP") },
    { "win2k3",         GUEST_OS_ID_STR_X86("Windows2003") },
    { "winvista",       GUEST_OS_ID_STR_X86("WindowsVista") },
    { "win2k8",         GUEST_OS_ID_STR_X86("Windows2008") },
    { "os2warp3",       GUEST_OS_ID_STR_X86("OS2Warp3") },
    { "os2warp4",       GUEST_OS_ID_STR_X86("OS2Warp4") },
    { "os2warp45",      GUEST_OS_ID_STR_X86("OS2Warp45") },
    { "ecs",            GUEST_OS_ID_STR_X86("OS2eCS") },
    { "linux22",        GUEST_OS_ID_STR_X86("Linux22") },
    { "linux24",        GUEST_OS_ID_STR_X86("Linux24") },
    { "linux26",        GUEST_OS_ID_STR_X86("Linux26") },
    { "archlinux",      GUEST_OS_ID_STR_X86("ArchLinux") },
    { "debian",         GUEST_OS_ID_STR_X86("Debian") },
    { "opensuse",       GUEST_OS_ID_STR_X86("OpenSUSE") },
    { "fedoracore",     GUEST_OS_ID_STR_X86("Fedora") },
    { "gentoo",         GUEST_OS_ID_STR_X86("Gentoo") },
    { "mandriva",       GUEST_OS_ID_STR_X86("Mandriva") },
    { "redhat",         GUEST_OS_ID_STR_X86("RedHat") },
    { "ubuntu",         GUEST_OS_ID_STR_X86("Ubuntu") },
    { "xandros",        GUEST_OS_ID_STR_X86("Xandros") },
    { "freebsd",        GUEST_OS_ID_STR_X86("FreeBSD") },
    { "openbsd",        GUEST_OS_ID_STR_X86("OpenBSD") },
    { "netbsd",         GUEST_OS_ID_STR_X86("NetBSD") },
    { "netware",        GUEST_OS_ID_STR_X86("Netware") },
    { "solaris",        GUEST_OS_ID_STR_X86("Solaris") },
    { "opensolaris",    GUEST_OS_ID_STR_X86("OpenSolaris") },
    { "l4",             GUEST_OS_ID_STR_X86("L4") },
};

/* static */
void MachineConfigFile::convertGuestOSTypeFromPre1_5(Utf8Str &str)
{
    for (size_t idx = 0; idx < RT_ELEMENTS(g_aConvertGuestOSTypesPre1_5); ++idx)
        if (str == g_aConvertGuestOSTypesPre1_5[idx].pcszOld)
        {
            str = g_aConvertGuestOSTypesPre1_5[idx].pcszNew;
            break;
        }
}

/**
 * Worker function that converts the suffix of a guest OS type ID.
 *
 * @param   a_rstrOsType        Guest OS type ID to convert.
 * @param   a_pszToReplace      Suffix to replace.
 * @param   a_pszReplacement    What to replace the suffix with.
 */
/* static */
void MachineConfigFile::convertGuestOSTypeSuffix(com::Utf8Str &a_rstrOsType,
                                                 const char *a_pszToReplace, const char *a_pszReplacement)
{
    size_t const cchToReplace = strlen(a_pszToReplace);
    if (a_rstrOsType.endsWith(a_pszToReplace, cchToReplace))
        a_rstrOsType.replace(a_rstrOsType.length() - cchToReplace, cchToReplace, a_pszReplacement);
}

#ifdef GUEST_OS_ID_STYLE_PARTIAL_CLEANUP

/**
 * Converts guest OS type IDs to be compatible with settings >= v1.20.
 *
 * @param   str                 Guest OS type ID to convert.
 *
 * @note    Settings < v1.20 require converting some guest OS type IDs, so that the rest of Main can recognize them.
 *          We always work with the latest guest OS type ID internally.
 *
 *          However, we never write back those modified guest OS type IDs for settings < v1.20, as this would break
 *          compatibility with older VBox versions.
 */
/* static */
void MachineConfigFile::convertGuestOSTypeFromPre1_20(Utf8Str &str)
{
    convertGuestOSTypeSuffix(str, "_64", "_x64");
}

/**
 * Converts guest OS type IDs to be compatible with settings < v1.20.
 *
 * @param   str                 Guest OS type ID to convert.
 *
 * @note    For settings < v1.20 we have to make sure that we replace the new guest OS type ID suffix "_x64"
 *          with "_64" so that we don't break loading (valid) settings for old(er) VBox versions, which don't
 *          know about the new suffix.
 */
/* static */
void MachineConfigFile::convertGuestOSTypeToPre1_20(Utf8Str &str)
{
    convertGuestOSTypeSuffix(str, "_x64", "_64");
}

#else  /* !GUEST_OS_ID_STYLE_PARTIAL_CLEANUP */

/**
 * Undoes effects of the 'OSTYPEID_64' to 'OSTYPEID_x64' renaming attempt from
 * the VBox v7.1 development cycle on @a a_rstrOsType.
 *
 * @see     @bugref{10384#c18}
 */
/* static */
void MachineConfigFile::convertGuestOSTypeFromDev1_20(Utf8Str &a_rstrOsType)
{
    convertGuestOSTypeSuffix(a_rstrOsType, "_x64", "_64");
}

#endif /* !GUEST_OS_ID_STYLE_PARTIAL_CLEANUP */


/**
 * Called from the constructor to actually read in the \<Machine\> element
 * of a machine config file.
 * @param elmMachine
 */
void MachineConfigFile::readMachine(const xml::ElementNode &elmMachine)
{
    Utf8Str strUUID;
    if (   elmMachine.getAttributeValue("uuid", strUUID)
        && elmMachine.getAttributeValue("name", machineUserData.strName))
    {
        parseUUID(uuid, strUUID, &elmMachine);

        elmMachine.getAttributeValue("directoryIncludesUUID", machineUserData.fDirectoryIncludesUUID);
        elmMachine.getAttributeValue("nameSync", machineUserData.fNameSync);

        Utf8Str str;
        elmMachine.getAttributeValue("Description", machineUserData.strDescription);
        elmMachine.getAttributeValue("OSType", machineUserData.strOsType);
        if (m->sv < SettingsVersion_v1_5)
            convertGuestOSTypeFromPre1_5(machineUserData.strOsType);
#ifdef GUEST_OS_ID_STYLE_PARTIAL_CLEANUP
        if (m->sv <= SettingsVersion_v1_19)
            convertGuestOSTypeFromPre1_20(machineUserData.strOsType);
#else
        if (m->sv == SettingsVersion_v1_20)
            convertGuestOSTypeFromDev1_20(machineUserData.strOsType);
#endif
        elmMachine.getAttributeValue("stateKeyId", strStateKeyId);
        elmMachine.getAttributeValue("stateKeyStore", strStateKeyStore);
        elmMachine.getAttributeValuePath("stateFile", strStateFile);

        elmMachine.getAttributeValue("logKeyId", strLogKeyId);
        elmMachine.getAttributeValue("logKeyStore", strLogKeyStore);

        if (elmMachine.getAttributeValue("currentSnapshot", str))
            parseUUID(uuidCurrentSnapshot, str, &elmMachine);

        elmMachine.getAttributeValuePath("snapshotFolder", machineUserData.strSnapshotFolder);

        if (!elmMachine.getAttributeValue("currentStateModified", fCurrentStateModified))
            fCurrentStateModified = true;
        if (elmMachine.getAttributeValue("lastStateChange", str))
            parseTimestamp(timeLastStateChange, str, &elmMachine);
            // constructor has called RTTimeNow(&timeLastStateChange) before
        if (elmMachine.getAttributeValue("aborted", fAborted))
            fAborted = true;

        {
            Utf8Str strVMPriority;
            if (elmMachine.getAttributeValue("processPriority", strVMPriority))
            {
                if (strVMPriority == "Flat")
                    machineUserData.enmVMPriority = VMProcPriority_Flat;
                else if (strVMPriority == "Low")
                    machineUserData.enmVMPriority = VMProcPriority_Low;
                else if (strVMPriority == "Normal")
                    machineUserData.enmVMPriority = VMProcPriority_Normal;
                else if (strVMPriority == "High")
                    machineUserData.enmVMPriority = VMProcPriority_High;
                else
                    machineUserData.enmVMPriority = VMProcPriority_Default;
            }
        }

        {
            Utf8Str strExecEngine;
            if (elmMachine.getAttributeValue("executionEngine", strExecEngine))
            {
                if (strExecEngine == "HwVirt")
                    machineUserData.enmExecEngine = VMExecutionEngine_HwVirt;
                else if (strExecEngine == "NativeApi")
                    machineUserData.enmExecEngine = VMExecutionEngine_NativeApi;
                else if (strExecEngine == "Interpreter")
                    machineUserData.enmExecEngine = VMExecutionEngine_Interpreter;
                else if (strExecEngine == "Recompiler")
                    machineUserData.enmExecEngine = VMExecutionEngine_Recompiler;
                else
                    machineUserData.enmExecEngine = VMExecutionEngine_Default;
            }
        }

        str.setNull();
        elmMachine.getAttributeValue("icon", str);
        parseBase64(machineUserData.ovIcon, str, &elmMachine);

        /* We read the platform settings here for newer settings (>= v1.20).
         * For older settings (< v1.20) we read the platform settings in readHardware(). */
        if (m->sv >= SettingsVersion_v1_20)
        {
            const xml::ElementNode *pelmPlatform;
            if (!(pelmPlatform = elmMachine.findChildElement("Platform")))
                throw ConfigFileError(this, &elmMachine, N_("Required Machine/@Platform element is missing"));
            readPlatform(*pelmPlatform, hardwareMachine, hardwareMachine.platformSettings);
        }

        // parse Hardware before the other elements because other things depend on it
        const xml::ElementNode *pelmHardware;
        if (!(pelmHardware = elmMachine.findChildElement("Hardware")))
            throw ConfigFileError(this, &elmMachine, N_("Required Machine/@Hardware element is missing"));
        readHardware(*pelmHardware, hardwareMachine);

        xml::NodesLoop nlRootChildren(elmMachine);
        const xml::ElementNode *pelmMachineChild;
        while ((pelmMachineChild = nlRootChildren.forAllNodes()))
        {
            if (pelmMachineChild->nameEquals("ExtraData"))
                readExtraData(*pelmMachineChild,
                              mapExtraDataItems);
            else if (    (m->sv < SettingsVersion_v1_7)
                      && (pelmMachineChild->nameEquals("HardDiskAttachments"))
                    )
                readHardDiskAttachments_pre1_7(*pelmMachineChild, hardwareMachine.storage);
            else if (    (m->sv >= SettingsVersion_v1_7)
                      && (pelmMachineChild->nameEquals("StorageControllers"))
                    )
                readStorageControllers(*pelmMachineChild, hardwareMachine.storage);
            else if (pelmMachineChild->nameEquals("Snapshot"))
            {
                if (uuidCurrentSnapshot.isZero())
                    throw ConfigFileError(this, &elmMachine, N_("Snapshots present but required Machine/@currentSnapshot attribute is missing"));
                bool foundCurrentSnapshot = false;
                // Work directly with the target list, because otherwise
                // the entire snapshot settings tree will need to be copied,
                // and the usual STL implementation needs a lot of stack space.
                llFirstSnapshot.push_back(Snapshot::Empty);
                // this will also read all child snapshots
                foundCurrentSnapshot = readSnapshot(uuidCurrentSnapshot, *pelmMachineChild, llFirstSnapshot.back());
                if (!foundCurrentSnapshot)
                    throw ConfigFileError(this, &elmMachine, N_("Snapshots present but none matches the UUID in the Machine/@currentSnapshot attribute"));
            }
            else if (pelmMachineChild->nameEquals("Description"))
                machineUserData.strDescription = pelmMachineChild->getValue();
            else if (pelmMachineChild->nameEquals("Teleporter"))
                readTeleporter(*pelmMachineChild, machineUserData);
            else if (pelmMachineChild->nameEquals("MediaRegistry"))
                readMediaRegistry(*pelmMachineChild, mediaRegistry);
            else if (pelmMachineChild->nameEquals("Debugging"))
                readDebugging(*pelmMachineChild, debugging);
            else if (pelmMachineChild->nameEquals("Autostart"))
                readAutostart(*pelmMachineChild, autostart);
            else if (pelmMachineChild->nameEquals("Groups"))
                readGroups(*pelmMachineChild, machineUserData.llGroups);

            if (   m->sv >= SettingsVersion_v1_14
                && m->sv <  SettingsVersion_v1_19
                && pelmMachineChild->nameEquals("VideoCapture"))   /* For settings >= 1.14 (< VBox 7.0). */
                readRecordingSettings(*pelmMachineChild, hardwareMachine.graphicsAdapter.cMonitors, recordingSettings);
            else if (   m->sv >= SettingsVersion_v1_19
                     && pelmMachineChild->nameEquals("Recording")) /* Only exists for settings >= 1.19 (VBox 7.0). */
                readRecordingSettings(*pelmMachineChild, hardwareMachine.graphicsAdapter.cMonitors, recordingSettings);
        }

        if (m->sv < SettingsVersion_v1_9)
            // go through Hardware once more to repair the settings controller structures
            // with data from old DVDDrive and FloppyDrive elements
            readDVDAndFloppies_pre1_9(*pelmHardware, hardwareMachine.storage);
    }
    else
        throw ConfigFileError(this, &elmMachine, N_("Required Machine/@uuid or @name attributes is missing"));
}

/**
 * Called from the constructor to decrypt the machine config and read
 * data from it.
 * @param elmMachine
 * @param pCryptoIf             Pointer to the cryptographic interface.
 * @param pszPassword           The password to decrypt the config with.
 */
void MachineConfigFile::readMachineEncrypted(const xml::ElementNode &elmMachine,
                                             PCVBOXCRYPTOIF pCryptoIf = NULL,
                                             const char *pszPassword = NULL)
{
    Utf8Str strUUID;
    if (elmMachine.getAttributeValue("uuid", strUUID))
    {
        parseUUID(uuid, strUUID, &elmMachine);
        if (!elmMachine.getAttributeValue("keyId", strKeyId))
            throw ConfigFileError(this, &elmMachine, N_("Required MachineEncrypted/@keyId attribute is missing"));
        if (!elmMachine.getAttributeValue("keyStore", strKeyStore))
            throw ConfigFileError(this, &elmMachine, N_("Required MachineEncrypted/@keyStore attribute is missing"));

        if (!pszPassword)
        {
            enmParseState = ParseState_PasswordError;
            return;
        }

        VBOXCRYPTOCTX hCryptoCtx = NULL;
        int vrc = pCryptoIf->pfnCryptoCtxLoad(strKeyStore.c_str(), pszPassword, &hCryptoCtx);
        if (RT_SUCCESS(vrc))
        {
            com::Utf8Str str = elmMachine.getValue();
            IconBlob abEncrypted; /** @todo Rename IconBlob because this is not about icons. */
            /** @todo This is not nice. */
            try
            {
                parseBase64(abEncrypted, str, &elmMachine);
            }
            catch (...)
            {
                int vrc2 = pCryptoIf->pfnCryptoCtxDestroy(hCryptoCtx);
                AssertRC(vrc2);
                throw;
            }

            IconBlob abDecrypted(abEncrypted.size());
            size_t cbDecrypted = 0;
            vrc = pCryptoIf->pfnCryptoCtxDecrypt(hCryptoCtx, false /*fPartial*/,
                                                 &abEncrypted[0], abEncrypted.size(),
                                                 uuid.raw(), sizeof(RTUUID),
                                                 &abDecrypted[0], abDecrypted.size(), &cbDecrypted);
            int vrc2 = pCryptoIf->pfnCryptoCtxDestroy(hCryptoCtx);
            AssertRC(vrc2);

            if (RT_SUCCESS(vrc))
            {
                abDecrypted.resize(cbDecrypted);
                xml::XmlMemParser parser;
                xml::Document *pDoc = new xml::Document;
                parser.read(&abDecrypted[0], abDecrypted.size(), m->strFilename, *pDoc);
                xml::ElementNode *pelmRoot = pDoc->getRootElement();
                if (!pelmRoot || !pelmRoot->nameEquals("Machine"))
                    throw ConfigFileError(this, pelmRoot, N_("Root element in Machine settings encrypted block must be \"Machine\""));
                readMachine(*pelmRoot);
                delete pDoc;
            }
        }

        if (RT_FAILURE(vrc))
        {
            if (vrc == VERR_ACCESS_DENIED)
                enmParseState = ParseState_PasswordError;
            else
                throw ConfigFileError(this, &elmMachine, N_("Parsing config failed. (%Rrc)"), vrc);
        }
    }
    else
        throw ConfigFileError(this, &elmMachine, N_("Required MachineEncrypted/@uuid attribute is missing"));
}

/**
 * Writes x86-specific platform settings out to the XML.
 *
 * For settings >= v1.20 this creates a \<x86\> node under elmParent.
 * keys under that. Called for both the \<Machine\> node and for snapshots.
 *
 * @param elmParent                         Parent element.
 *                                          For settings >= v1.20 this is the \<Platform\> element.
 *                                          For settings  < v1.20 this is the \<Hardware\> element.
 * @param elmCPU                            CPU element platform-generic settings.
 *                                          For settings >= v1.20 this is the \<Platform/CPU\> element.
 *                                          For settings  < v1.20 this is the \<Hardware/CPU\> element.
 * @param platX86                           x86-specific platform settings to use for building the XML.
 */
void MachineConfigFile::buildPlatformX86XML(xml::ElementNode &elmParent, xml::ElementNode &elmCPU, const PlatformX86 &platX86)
{
    xml::ElementNode *pelmX86Root;
    xml::ElementNode *pelmX86CPU;
    if (m->sv >= SettingsVersion_v1_20)
    {
        pelmX86Root = elmParent.createChild("x86");
        pelmX86CPU  = pelmX86Root->createChild("CPU");

    }
    else
    {
        pelmX86Root = &elmParent; /* Before settings v1.20 the x86-specifics were sprinkled across the Machine element. */
        pelmX86CPU  = &elmCPU;    /* Use the generic CPU element, even for x86-specific things. */
    }

    if (   (m->sv >= SettingsVersion_v1_10)
        && platX86.fHPETEnabled)
    {
        xml::ElementNode *pelmHPET = pelmX86Root->createChild("HPET");
        pelmHPET->setAttribute("enabled", platX86.fHPETEnabled);
    }

    // HardwareVirtExLargePages has too crazy default handling, must always save this setting.
    pelmX86CPU->createChild("HardwareVirtExLargePages")->setAttribute("enabled", platX86.fHWVirtExLargePages);

    if (m->sv >= SettingsVersion_v1_9)
    {
        if (platX86.fHWVirtExForce)
            pelmX86CPU->createChild("HardwareVirtForce")->setAttribute("enabled", platX86.fHWVirtExForce);
    }

    if (m->sv >= SettingsVersion_v1_9 && platX86.fHWVirtExUseNativeApi)
        pelmX86CPU->createChild("HardwareVirtExUseNativeApi")->setAttribute("enabled", platX86.fHWVirtExUseNativeApi);

    if (!platX86.fHWVirtEx)
        pelmX86CPU->createChild("HardwareVirtEx")->setAttribute("enabled", platX86.fHWVirtEx);
    if (!platX86.fHWVirtExNestedPaging)
        pelmX86CPU->createChild("HardwareVirtExNestedPaging")->setAttribute("enabled", platX86.fHWVirtExNestedPaging);
    if (!platX86.fHWVirtExVPID)
        pelmX86CPU->createChild("HardwareVirtExVPID")->setAttribute("enabled", platX86.fHWVirtExVPID);
    if (!platX86.fHWVirtExUX)
        pelmX86CPU->createChild("HardwareVirtExUX")->setAttribute("enabled", platX86.fHWVirtExUX);
    // PAE has too crazy default handling, must always save this setting.
    pelmX86CPU->createChild("PAE")->setAttribute("enabled", platX86.fPAE);
    if (m->sv >= SettingsVersion_v1_16)
    {
        if (platX86.fIBPBOnVMEntry || platX86.fIBPBOnVMExit)
        {
            xml::ElementNode *pelmChild = pelmX86CPU->createChild("IBPBOn");
            if (platX86.fIBPBOnVMExit)
                pelmChild->setAttribute("vmexit", platX86.fIBPBOnVMExit);
            if (platX86.fIBPBOnVMEntry)
                pelmChild->setAttribute("vmentry", platX86.fIBPBOnVMEntry);
        }
        if (platX86.fSpecCtrl)
            pelmX86CPU->createChild("SpecCtrl")->setAttribute("enabled", platX86.fSpecCtrl);
        if (platX86.fSpecCtrlByHost)
            pelmX86CPU->createChild("SpecCtrlByHost")->setAttribute("enabled", platX86.fSpecCtrlByHost);
        if (!platX86.fL1DFlushOnSched || platX86.fL1DFlushOnVMEntry)
        {
            xml::ElementNode *pelmChild = pelmX86CPU->createChild("L1DFlushOn");
            if (!platX86.fL1DFlushOnSched)
                pelmChild->setAttribute("scheduling", platX86.fL1DFlushOnSched);
            if (platX86.fL1DFlushOnVMEntry)
                pelmChild->setAttribute("vmentry", platX86.fL1DFlushOnVMEntry);
        }
        if (!platX86.fMDSClearOnSched || platX86.fMDSClearOnVMEntry)
        {
            xml::ElementNode *pelmChild = pelmX86CPU->createChild("MDSClearOn");
            if (!platX86.fMDSClearOnSched)
                pelmChild->setAttribute("scheduling", platX86.fMDSClearOnSched);
            if (platX86.fMDSClearOnVMEntry)
                pelmChild->setAttribute("vmentry", platX86.fMDSClearOnVMEntry);
        }
    }
    if (m->sv >= SettingsVersion_v1_17 && platX86.fNestedHWVirt)
        pelmX86CPU->createChild("NestedHWVirt")->setAttribute("enabled", platX86.fNestedHWVirt);

    if (m->sv >= SettingsVersion_v1_18 && !platX86.fHWVirtExVirtVmsaveVmload)
        pelmX86CPU->createChild("HardwareVirtExVirtVmsaveVmload")->setAttribute("enabled", platX86.fHWVirtExVirtVmsaveVmload);

    if (m->sv >= SettingsVersion_v1_14 && platX86.enmLongMode != PlatformX86::LongMode_Legacy)
    {
        // LongMode has too crazy default handling, must always save this setting.
        pelmX86CPU->createChild("LongMode")->setAttribute("enabled", platX86.enmLongMode == PlatformX86::LongMode_Enabled);
    }

    if (platX86.fTripleFaultReset)
        pelmX86CPU->createChild("TripleFaultReset")->setAttribute("enabled", platX86.fTripleFaultReset);
    if (m->sv >= SettingsVersion_v1_14)
    {
        if (platX86.fX2APIC)
            pelmX86CPU->createChild("X2APIC")->setAttribute("enabled", platX86.fX2APIC);
        else if (!platX86.fAPIC)
            pelmX86CPU->createChild("APIC")->setAttribute("enabled", platX86.fAPIC);
    }

    xml::ElementNode *pelmCpuIdTree = NULL;
    for (CpuIdLeafsX86List::const_iterator it  = platX86.llCpuIdLeafs.begin();
                                           it != platX86.llCpuIdLeafs.end();
         ++it)
    {
        const CpuIdLeafX86 &leaf = *it;

        if (pelmCpuIdTree == NULL)
             pelmCpuIdTree = pelmX86CPU->createChild("CpuIdTree");

        xml::ElementNode *pelmCpuIdLeaf = pelmCpuIdTree->createChild("CpuIdLeaf");
        pelmCpuIdLeaf->setAttribute("id",  leaf.idx);
        if (leaf.idxSub != 0)
            pelmCpuIdLeaf->setAttribute("subleaf",  leaf.idxSub);
        pelmCpuIdLeaf->setAttribute("eax", leaf.uEax);
        pelmCpuIdLeaf->setAttribute("ebx", leaf.uEbx);
        pelmCpuIdLeaf->setAttribute("ecx", leaf.uEcx);
        pelmCpuIdLeaf->setAttribute("edx", leaf.uEdx);
    }
}

/**
 * Stores platform-generic and platform-specific data and then writes out the XML.
 *
 * For settings >= v.120 this creates a \<Platform\> node under elmParent.
 * For settings  < v.120 this stores the data directly under elmParent.
 *
 * Called for both the \<Machine\> node and for snapshots.
 *
 * @param elmParent                         Parent element.
 * @param hw                                Hardware settings to use for building the XML.
 *                                          For CPU stuff we don't have in Platform (yet).
 * @param plat                              Platform settings to use for building the XML.
 */
void MachineConfigFile::buildPlatformXML(xml::ElementNode &elmParent,
                                         const Hardware &hw, const Platform &plat)
{
    xml::ElementNode *pelmPlatformOrHardware;
    if (m->sv >= SettingsVersion_v1_20) /* Since settings v1.20 we have a dedicated Platform element. */
    {
        pelmPlatformOrHardware = elmParent.createChild("Platform");

        switch (plat.architectureType)
        {
            case PlatformArchitecture_x86:
                pelmPlatformOrHardware->setAttribute("architecture", "x86");
                break;

            case PlatformArchitecture_ARM:
                pelmPlatformOrHardware->setAttribute("architecture", "ARM");
                break;

            default:
                AssertFailed();
                break;
        }
    }
    else /* For settings < v1.20 we use the parent element (which is Hardware). */
        pelmPlatformOrHardware = &elmParent;

    if (   m->sv >= SettingsVersion_v1_10
        && plat.fRTCUseUTC)
    {
        xml::ElementNode *pelmRTC = pelmPlatformOrHardware->createChild("RTC");
        pelmRTC->setAttribute("localOrUTC", plat.fRTCUseUTC ? "UTC" : "local");
    }

    if (m->sv >= SettingsVersion_v1_11)
    {
        if (plat.chipsetType != ChipsetType_PIIX3)
        {
            xml::ElementNode *pelmChipset = pelmPlatformOrHardware->createChild("Chipset");
            const char *pcszChipset;

            switch (plat.chipsetType)
            {
                case ChipsetType_PIIX3:             pcszChipset = "PIIX3";        break;
                case ChipsetType_ICH9:              pcszChipset = "ICH9";         break;
                case ChipsetType_ARMv8Virtual:      pcszChipset = "ARMv8Virtual"; break;
                default:            Assert(false);  pcszChipset = "PIIX3";        break;
            }
            pelmChipset->setAttribute("type", pcszChipset);
        }
    }

    if (   m->sv >= SettingsVersion_v1_19
        && plat.iommuType != IommuType_None)
    {
        const char *pcszIommuType;
        switch (plat.iommuType)
        {
            case IommuType_None:         pcszIommuType = "None";        break;
            case IommuType_Automatic:    pcszIommuType = "Automatic";   break;
            case IommuType_AMD:          pcszIommuType = "AMD";         break;
            case IommuType_Intel:        pcszIommuType = "Intel";       break;
            default:     Assert(false);  pcszIommuType = "None";        break;
        }

        xml::ElementNode *pelmIommu = pelmPlatformOrHardware->createChild("Iommu");
        pelmIommu->setAttribute("type", pcszIommuType);
    }

    xml::ElementNode *pelmCPU = pelmPlatformOrHardware->createChild("CPU");

    if (hw.cCPUs > 1)
        pelmCPU->setAttribute("count", hw.cCPUs);
    if (hw.ulCpuExecutionCap != 100)
        pelmCPU->setAttribute("executionCap", hw.ulCpuExecutionCap);
    if (hw.uCpuIdPortabilityLevel != 0)
        pelmCPU->setAttribute("CpuIdPortabilityLevel", hw.uCpuIdPortabilityLevel);
    if (!hw.strCpuProfile.equals("host") && hw.strCpuProfile.isNotEmpty())
        pelmCPU->setAttribute("CpuProfile", hw.strCpuProfile);

    if (m->sv >= SettingsVersion_v1_10)
    {
        if (hw.fCpuHotPlug)
            pelmCPU->setAttribute("hotplug", hw.fCpuHotPlug);

        xml::ElementNode *pelmCpuTree = NULL;
        for (CpuList::const_iterator it = hw.llCpus.begin();
             it != hw.llCpus.end();
             ++it)
        {
            const Cpu &cpu = *it;

            if (pelmCpuTree == NULL)
                 pelmCpuTree = pelmCPU->createChild("CpuTree");

            xml::ElementNode *pelmCpu = pelmCpuTree->createChild("Cpu");
            pelmCpu->setAttribute("id", cpu.ulId);
        }
    }

    /* We only store specific stuff for the selected platform. */
    if (plat.architectureType == PlatformArchitecture_x86)
        buildPlatformX86XML(*pelmPlatformOrHardware, *pelmCPU, plat.x86);
    /** @todo Put ARM stuff here as soon as we have it. */
}

/**
 * Creates a \<Hardware\> node under elmParent and then writes out the XML
 * keys under that. Called for both the \<Machine\> node and for snapshots.
 *
 * @param elmParent                         Parent element.
 * @param hw                                Hardware settings to use for building the XML.
 * @param fl                                Flags.
 * @param pllElementsWithUuidAttributes     Document me.
 */
void MachineConfigFile::buildHardwareXML(xml::ElementNode &elmParent,
                                         const Hardware &hw,
                                         uint32_t fl,
                                         std::list<xml::ElementNode*> *pllElementsWithUuidAttributes)
{
    xml::ElementNode *pelmHardware = elmParent.createChild("Hardware");

    if (   m->sv >= SettingsVersion_v1_4
        && (m->sv < SettingsVersion_v1_7 ? hw.strVersion != "1" : hw.strVersion != "2"))
        pelmHardware->setAttribute("version", hw.strVersion);

    if ((m->sv >= SettingsVersion_v1_9)
         && !hw.uuid.isZero()
         && hw.uuid.isValid()
       )
        pelmHardware->setAttribute("uuid", hw.uuid.toStringCurly());

    xml::ElementNode *pelmMemory = pelmHardware->createChild("Memory");
    pelmMemory->setAttribute("RAMSize", hw.ulMemorySizeMB);
    if (m->sv >= SettingsVersion_v1_10)
    {
        if (hw.fPageFusionEnabled)
            pelmMemory->setAttribute("PageFusion", hw.fPageFusionEnabled);
    }

    if (   m->sv >= SettingsVersion_v1_10
        && (   hw.pointingHIDType != PointingHIDType_PS2Mouse
            || hw.keyboardHIDType != KeyboardHIDType_PS2Keyboard))
    {
        xml::ElementNode *pelmHID = pelmHardware->createChild("HID");
        const char *pcszHID;

        if (hw.pointingHIDType != PointingHIDType_PS2Mouse)
        {
            switch (hw.pointingHIDType)
            {
                case PointingHIDType_USBMouse:      pcszHID = "USBMouse";     break;
                case PointingHIDType_USBTablet:     pcszHID = "USBTablet";    break;
                case PointingHIDType_PS2Mouse:      pcszHID = "PS2Mouse";     break;
                case PointingHIDType_ComboMouse:    pcszHID = "ComboMouse";   break;
                case PointingHIDType_USBMultiTouch: pcszHID = "USBMultiTouch";break;
                case PointingHIDType_USBMultiTouchScreenPlusPad: pcszHID = "USBMTScreenPlusPad";break;
                case PointingHIDType_None:          pcszHID = "None";         break;
                default:            Assert(false);  pcszHID = "PS2Mouse";     break;
            }
            pelmHID->setAttribute("Pointing", pcszHID);
        }

        if (hw.keyboardHIDType != KeyboardHIDType_PS2Keyboard)
        {
            switch (hw.keyboardHIDType)
            {
                case KeyboardHIDType_USBKeyboard:   pcszHID = "USBKeyboard";   break;
                case KeyboardHIDType_PS2Keyboard:   pcszHID = "PS2Keyboard";   break;
                case KeyboardHIDType_ComboKeyboard: pcszHID = "ComboKeyboard"; break;
                case KeyboardHIDType_None:          pcszHID = "None";          break;
                default:            Assert(false);  pcszHID = "PS2Keyboard";   break;
            }
            pelmHID->setAttribute("Keyboard", pcszHID);
        }
    }



    if (    (m->sv >= SettingsVersion_v1_15)
        && !hw.areParavirtDefaultSettings(m->sv)
       )
    {
        const char *pcszParavirtProvider;
        switch (hw.paravirtProvider)
        {
            case ParavirtProvider_None:         pcszParavirtProvider = "None";     break;
            case ParavirtProvider_Default:      pcszParavirtProvider = "Default";  break;
            case ParavirtProvider_Legacy:       pcszParavirtProvider = "Legacy";   break;
            case ParavirtProvider_Minimal:      pcszParavirtProvider = "Minimal";  break;
            case ParavirtProvider_HyperV:       pcszParavirtProvider = "HyperV";   break;
            case ParavirtProvider_KVM:          pcszParavirtProvider = "KVM";      break;
            default:            Assert(false);  pcszParavirtProvider = "None";     break;
        }

        xml::ElementNode *pelmParavirt = pelmHardware->createChild("Paravirt");
        pelmParavirt->setAttribute("provider", pcszParavirtProvider);

        if (   m->sv >= SettingsVersion_v1_16
            && hw.strParavirtDebug.isNotEmpty())
            pelmParavirt->setAttribute("debug", hw.strParavirtDebug);
    }

    if (!hw.areBootOrderDefaultSettings())
    {
        xml::ElementNode *pelmBoot = pelmHardware->createChild("Boot");
        for (BootOrderMap::const_iterator it = hw.mapBootOrder.begin();
             it != hw.mapBootOrder.end();
             ++it)
        {
            uint32_t i = it->first;
            DeviceType_T type = it->second;
            const char *pcszDevice;

            switch (type)
            {
                case DeviceType_Floppy:     pcszDevice = "Floppy"; break;
                case DeviceType_DVD:        pcszDevice = "DVD"; break;
                case DeviceType_HardDisk:   pcszDevice = "HardDisk"; break;
                case DeviceType_Network:    pcszDevice = "Network"; break;
                default: /*case DeviceType_Null:*/      pcszDevice = "None"; break;
            }

            xml::ElementNode *pelmOrder = pelmBoot->createChild("Order");
            pelmOrder->setAttribute("position",
                                    i + 1);   // XML is 1-based but internal data is 0-based
            pelmOrder->setAttribute("device", pcszDevice);
        }
    }

    if (!hw.graphicsAdapter.areDefaultSettings())
    {
        xml::ElementNode *pelmDisplay = pelmHardware->createChild("Display");
        if (hw.graphicsAdapter.graphicsControllerType != GraphicsControllerType_VBoxVGA)
        {
            const char *pcszGraphics;
            switch (hw.graphicsAdapter.graphicsControllerType)
            {
                case GraphicsControllerType_VBoxVGA:            pcszGraphics = "VBoxVGA"; break;
                case GraphicsControllerType_VMSVGA:             pcszGraphics = "VMSVGA"; break;
                case GraphicsControllerType_VBoxSVGA:           pcszGraphics = "VBoxSVGA"; break;
                case GraphicsControllerType_QemuRamFB:          pcszGraphics = "QemuRamFB"; break;
                default: /*case GraphicsControllerType_Null:*/  pcszGraphics = "None"; break;
            }
            pelmDisplay->setAttribute("controller", pcszGraphics);
        }
        if (hw.graphicsAdapter.ulVRAMSizeMB != 8)
            pelmDisplay->setAttribute("VRAMSize", hw.graphicsAdapter.ulVRAMSizeMB);
        if (hw.graphicsAdapter.cMonitors > 1)
            pelmDisplay->setAttribute("monitorCount", hw.graphicsAdapter.cMonitors);
        if (hw.graphicsAdapter.fAccelerate3D)
            pelmDisplay->setAttribute("accelerate3D", hw.graphicsAdapter.fAccelerate3D);

        if (m->sv >= SettingsVersion_v1_8)
        {
            if (hw.graphicsAdapter.fAccelerate2DVideo)
                pelmDisplay->setAttribute("accelerate2DVideo", hw.graphicsAdapter.fAccelerate2DVideo);
        }
    }

    if (!hw.vrdeSettings.areDefaultSettings(m->sv))
    {
        xml::ElementNode *pelmVRDE = pelmHardware->createChild("RemoteDisplay");
        if (m->sv < SettingsVersion_v1_16 ? !hw.vrdeSettings.fEnabled : hw.vrdeSettings.fEnabled)
            pelmVRDE->setAttribute("enabled", hw.vrdeSettings.fEnabled);
        if (m->sv < SettingsVersion_v1_11)
        {
            /* In VBox 4.0 these attributes are replaced with "Properties". */
            Utf8Str strPort;
            StringsMap::const_iterator it = hw.vrdeSettings.mapProperties.find("TCP/Ports");
            if (it != hw.vrdeSettings.mapProperties.end())
                strPort = it->second;
            if (!strPort.length())
                strPort = "3389";
            pelmVRDE->setAttribute("port", strPort);

            Utf8Str strAddress;
            it = hw.vrdeSettings.mapProperties.find("TCP/Address");
            if (it != hw.vrdeSettings.mapProperties.end())
                strAddress = it->second;
            if (strAddress.length())
                pelmVRDE->setAttribute("netAddress", strAddress);
        }
        if (hw.vrdeSettings.authType != AuthType_Null)
        {
            const char *pcszAuthType;
            switch (hw.vrdeSettings.authType)
            {
                case AuthType_Guest:    pcszAuthType = "Guest";    break;
                case AuthType_External: pcszAuthType = "External"; break;
                default: /*case AuthType_Null:*/ pcszAuthType = "Null"; break;
            }
            pelmVRDE->setAttribute("authType", pcszAuthType);
        }

        if (hw.vrdeSettings.ulAuthTimeout != 0 && hw.vrdeSettings.ulAuthTimeout != 5000)
            pelmVRDE->setAttribute("authTimeout", hw.vrdeSettings.ulAuthTimeout);
        if (hw.vrdeSettings.fAllowMultiConnection)
            pelmVRDE->setAttribute("allowMultiConnection", hw.vrdeSettings.fAllowMultiConnection);
        if (hw.vrdeSettings.fReuseSingleConnection)
            pelmVRDE->setAttribute("reuseSingleConnection", hw.vrdeSettings.fReuseSingleConnection);

        if (m->sv == SettingsVersion_v1_10)
        {
            xml::ElementNode *pelmVideoChannel = pelmVRDE->createChild("VideoChannel");

            /* In 4.0 videochannel settings were replaced with properties, so look at properties. */
            Utf8Str str;
            StringsMap::const_iterator it = hw.vrdeSettings.mapProperties.find("VideoChannel/Enabled");
            if (it != hw.vrdeSettings.mapProperties.end())
                str = it->second;
            bool fVideoChannel =    RTStrICmp(str.c_str(), "true") == 0
                                 || RTStrCmp(str.c_str(), "1") == 0;
            pelmVideoChannel->setAttribute("enabled", fVideoChannel);

            it = hw.vrdeSettings.mapProperties.find("VideoChannel/Quality");
            if (it != hw.vrdeSettings.mapProperties.end())
                str = it->second;
            uint32_t ulVideoChannelQuality = RTStrToUInt32(str.c_str()); /* This returns 0 on invalid string which is ok. */
            if (ulVideoChannelQuality == 0)
                ulVideoChannelQuality = 75;
            else
                ulVideoChannelQuality = RT_CLAMP(ulVideoChannelQuality, 10, 100);
            pelmVideoChannel->setAttribute("quality", ulVideoChannelQuality);
        }
        if (m->sv >= SettingsVersion_v1_11)
        {
            if (hw.vrdeSettings.strAuthLibrary.length())
                pelmVRDE->setAttribute("authLibrary", hw.vrdeSettings.strAuthLibrary);
            if (hw.vrdeSettings.strVrdeExtPack.isNotEmpty())
                pelmVRDE->setAttribute("VRDEExtPack", hw.vrdeSettings.strVrdeExtPack);
            if (hw.vrdeSettings.mapProperties.size() > 0)
            {
                xml::ElementNode *pelmProperties = pelmVRDE->createChild("VRDEProperties");
                for (StringsMap::const_iterator it = hw.vrdeSettings.mapProperties.begin();
                     it != hw.vrdeSettings.mapProperties.end();
                     ++it)
                {
                    const Utf8Str &strName = it->first;
                    const Utf8Str &strValue = it->second;
                    xml::ElementNode *pelm = pelmProperties->createChild("Property");
                    pelm->setAttribute("name", strName);
                    pelm->setAttribute("value", strValue);
                }
            }
        }
    }

    if (   !hw.firmwareSettings.areDefaultSettings(  hw.platformSettings.architectureType == PlatformArchitecture_ARM
                                                   /* Good enough for now (we don't support ARMv7). */
                                                   ? CPUArchitecture_ARMv8_64 : CPUArchitecture_x86)
        || !hw.nvramSettings.areDefaultSettings())
    {
        /* Settings < v1.20 also had a "Firmware" element, but that only stored the firmware type. */
        xml::ElementNode *pelmFirmware = pelmHardware->createChild("Firmware");

        if (   (m->sv >= SettingsVersion_v1_9)
            && (hw.firmwareSettings.firmwareType >= FirmwareType_EFI)
           )
        {
            const char *pcszFirmware;

            switch (hw.firmwareSettings.firmwareType)
            {
                case FirmwareType_EFI:      pcszFirmware = "EFI";     break;
                case FirmwareType_EFI32:    pcszFirmware = "EFI32";   break;
                case FirmwareType_EFI64:    pcszFirmware = "EFI64";   break;
                case FirmwareType_EFIDUAL:  pcszFirmware = "EFIDUAL"; break;
                default:                    pcszFirmware = "None";    break;
            }
            pelmFirmware->setAttribute("type", pcszFirmware);
        }

        xml::ElementNode *pelmFirmwareOrBIOS;
        if (m->sv >= SettingsVersion_v1_20) /* Since settings v1.20 the rest also gets stored in "Firmware". */
            pelmFirmwareOrBIOS = pelmFirmware; /* Use the "Firmware" element from above. */
        else /* For settings < v1.20 the rest was stored in the "BIOS" element. */
            pelmFirmwareOrBIOS = pelmHardware->createChild("BIOS");

        if (!hw.firmwareSettings.fACPIEnabled)
            pelmFirmwareOrBIOS->createChild("ACPI")->setAttribute("enabled", hw.firmwareSettings.fACPIEnabled);
        if (hw.firmwareSettings.fIOAPICEnabled)
            pelmFirmwareOrBIOS->createChild("IOAPIC")->setAttribute("enabled", hw.firmwareSettings.fIOAPICEnabled);
        if (hw.firmwareSettings.apicMode != APICMode_APIC)
        {
            const char *pcszAPIC;
            switch (hw.firmwareSettings.apicMode)
            {
                case APICMode_Disabled:
                    pcszAPIC = "Disabled";
                    break;
                case APICMode_APIC:
                default:
                    pcszAPIC = "APIC";
                    break;
                case APICMode_X2APIC:
                    pcszAPIC = "X2APIC";
                    break;
            }
            pelmFirmwareOrBIOS->createChild("APIC")->setAttribute("mode", pcszAPIC);
        }

        if (   !hw.firmwareSettings.fLogoFadeIn
            || !hw.firmwareSettings.fLogoFadeOut
            ||  hw.firmwareSettings.ulLogoDisplayTime
            || !hw.firmwareSettings.strLogoImagePath.isEmpty())
        {
            xml::ElementNode *pelmLogo = pelmFirmwareOrBIOS->createChild("Logo");
            pelmLogo->setAttribute("fadeIn", hw.firmwareSettings.fLogoFadeIn);
            pelmLogo->setAttribute("fadeOut", hw.firmwareSettings.fLogoFadeOut);
            pelmLogo->setAttribute("displayTime", hw.firmwareSettings.ulLogoDisplayTime);
            if (!hw.firmwareSettings.strLogoImagePath.isEmpty())
                pelmLogo->setAttribute("imagePath", hw.firmwareSettings.strLogoImagePath);
        }

        if (hw.firmwareSettings.enmBootMenuMode != FirmwareBootMenuMode_MessageAndMenu)
        {
            const char *pcszBootMenu;
            switch (hw.firmwareSettings.enmBootMenuMode)
            {
                case FirmwareBootMenuMode_Disabled: pcszBootMenu = "Disabled"; break;
                case FirmwareBootMenuMode_MenuOnly: pcszBootMenu = "MenuOnly"; break;
                default: /*FirmwareBootMenuMode_MessageAndMenu*/ pcszBootMenu = "MessageAndMenu"; break;
            }
            pelmFirmwareOrBIOS->createChild("BootMenu")->setAttribute("mode", pcszBootMenu);
        }
        if (hw.firmwareSettings.llTimeOffset)
            pelmFirmwareOrBIOS->createChild("TimeOffset")->setAttribute("value", hw.firmwareSettings.llTimeOffset);
        if (hw.firmwareSettings.fPXEDebugEnabled)
            pelmFirmwareOrBIOS->createChild("PXEDebug")->setAttribute("enabled", hw.firmwareSettings.fPXEDebugEnabled);
        if (!hw.nvramSettings.areDefaultSettings())
        {
            xml::ElementNode *pelmNvram = pelmFirmwareOrBIOS->createChild("NVRAM");
            if (!hw.nvramSettings.strNvramPath.isEmpty())
                pelmNvram->setAttribute("path", hw.nvramSettings.strNvramPath);
            if (m->sv >= SettingsVersion_v1_9)
            {
                if (hw.nvramSettings.strKeyId.isNotEmpty())
                    pelmNvram->setAttribute("keyId", hw.nvramSettings.strKeyId);
                if (hw.nvramSettings.strKeyStore.isNotEmpty())
                    pelmNvram->setAttribute("keyStore", hw.nvramSettings.strKeyStore);
            }
        }
        if (hw.firmwareSettings.fSmbiosUuidLittleEndian)
            pelmFirmwareOrBIOS->createChild("SmbiosUuidLittleEndian")->setAttribute("enabled", hw.firmwareSettings.fSmbiosUuidLittleEndian);
        if (hw.firmwareSettings.fAutoSerialNumGen)
            pelmFirmwareOrBIOS->createChild("AutoSerialNumGen")->setAttribute("enabled", hw.firmwareSettings.fAutoSerialNumGen);
    }

    if (!hw.tpmSettings.areDefaultSettings())
    {
        xml::ElementNode *pelmTpm = pelmHardware->createChild("TrustedPlatformModule");

        const char *pcszTpm;
        switch (hw.tpmSettings.tpmType)
        {
            default:
            case TpmType_None:
                pcszTpm = "None";
                break;
            case TpmType_v1_2:
                pcszTpm = "v1_2";
                break;
            case TpmType_v2_0:
                pcszTpm = "v2_0";
                break;
            case TpmType_Host:
                pcszTpm = "Host";
                break;
            case TpmType_Swtpm:
                pcszTpm = "Swtpm";
                break;
        }
        pelmTpm->setAttribute("type", pcszTpm);
        pelmTpm->setAttribute("location", hw.tpmSettings.strLocation);
    }

    if (m->sv < SettingsVersion_v1_9)
    {
        // settings formats before 1.9 had separate DVDDrive and FloppyDrive items under Hardware;
        // run thru the storage controllers to see if we have a DVD or floppy drives
        size_t cDVDs = 0;
        size_t cFloppies = 0;

        xml::ElementNode *pelmDVD = pelmHardware->createChild("DVDDrive");
        xml::ElementNode *pelmFloppy = pelmHardware->createChild("FloppyDrive");

        for (StorageControllersList::const_iterator it = hw.storage.llStorageControllers.begin();
             it != hw.storage.llStorageControllers.end();
             ++it)
        {
            const StorageController &sctl = *it;
            // in old settings format, the DVD drive could only have been under the IDE controller
            if (sctl.storageBus == StorageBus_IDE)
            {
                for (AttachedDevicesList::const_iterator it2 = sctl.llAttachedDevices.begin();
                     it2 != sctl.llAttachedDevices.end();
                     ++it2)
                {
                    const AttachedDevice &att = *it2;
                    if (att.deviceType == DeviceType_DVD)
                    {
                        if (cDVDs > 0)
                            throw ConfigFileError(this, NULL, N_("Internal error: cannot save more than one DVD drive with old settings format"));

                        ++cDVDs;

                        pelmDVD->setAttribute("passthrough", att.fPassThrough);
                        if (att.fTempEject)
                            pelmDVD->setAttribute("tempeject", att.fTempEject);

                        if (!att.uuid.isZero() && att.uuid.isValid())
                            pelmDVD->createChild("Image")->setAttribute("uuid", att.uuid.toStringCurly());
                        else if (att.strHostDriveSrc.length())
                            pelmDVD->createChild("HostDrive")->setAttribute("src", att.strHostDriveSrc);
                    }
                }
            }
            else if (sctl.storageBus == StorageBus_Floppy)
            {
                size_t cFloppiesHere = sctl.llAttachedDevices.size();
                if (cFloppiesHere > 1)
                    throw ConfigFileError(this, NULL, N_("Internal error: floppy controller cannot have more than one device attachment"));
                if (cFloppiesHere)
                {
                    const AttachedDevice &att = sctl.llAttachedDevices.front();
                    pelmFloppy->setAttribute("enabled", true);

                    if (!att.uuid.isZero() && att.uuid.isValid())
                        pelmFloppy->createChild("Image")->setAttribute("uuid", att.uuid.toStringCurly());
                    else if (att.strHostDriveSrc.length())
                        pelmFloppy->createChild("HostDrive")->setAttribute("src", att.strHostDriveSrc);
                }

                cFloppies += cFloppiesHere;
            }
        }

        if (cFloppies == 0)
            pelmFloppy->setAttribute("enabled", false);
        else if (cFloppies > 1)
            throw ConfigFileError(this, NULL, N_("Internal error: cannot save more than one floppy drive with old settings format"));
    }

    if (m->sv < SettingsVersion_v1_14)
    {
        bool fOhciEnabled = false;
        bool fEhciEnabled = false;
        xml::ElementNode *pelmUSB = pelmHardware->createChild("USBController");

        for (USBControllerList::const_iterator it = hw.usbSettings.llUSBControllers.begin();
             it != hw.usbSettings.llUSBControllers.end();
             ++it)
        {
            const USBController &ctrl = *it;

            switch (ctrl.enmType)
            {
                case USBControllerType_OHCI:
                    fOhciEnabled = true;
                    break;
                case USBControllerType_EHCI:
                    fEhciEnabled = true;
                    break;
                default:
                    AssertMsgFailed(("Unknown USB controller type %d\n", ctrl.enmType));
            }
        }

        pelmUSB->setAttribute("enabled", fOhciEnabled);
        pelmUSB->setAttribute("enabledEhci", fEhciEnabled);

        buildUSBDeviceFilters(*pelmUSB, hw.usbSettings.llDeviceFilters, false /* fHostMode */);
    }
    else
    {
        if (   hw.usbSettings.llUSBControllers.size()
            || hw.usbSettings.llDeviceFilters.size())
        {
            xml::ElementNode *pelmUSB = pelmHardware->createChild("USB");
            if (hw.usbSettings.llUSBControllers.size())
            {
                xml::ElementNode *pelmCtrls = pelmUSB->createChild("Controllers");

                for (USBControllerList::const_iterator it = hw.usbSettings.llUSBControllers.begin();
                     it != hw.usbSettings.llUSBControllers.end();
                     ++it)
                {
                    const USBController &ctrl = *it;
                    com::Utf8Str strType;
                    xml::ElementNode *pelmCtrl = pelmCtrls->createChild("Controller");

                    switch (ctrl.enmType)
                    {
                        case USBControllerType_OHCI:
                            strType = "OHCI";
                            break;
                        case USBControllerType_EHCI:
                            strType = "EHCI";
                            break;
                        case USBControllerType_XHCI:
                            strType = "XHCI";
                            break;
                        default:
                            AssertMsgFailed(("Unknown USB controller type %d\n", ctrl.enmType));
                    }

                    pelmCtrl->setAttribute("name", ctrl.strName);
                    pelmCtrl->setAttribute("type", strType);
                }
            }

            if (hw.usbSettings.llDeviceFilters.size())
            {
                xml::ElementNode *pelmFilters = pelmUSB->createChild("DeviceFilters");
                buildUSBDeviceFilters(*pelmFilters, hw.usbSettings.llDeviceFilters, false /* fHostMode */);
            }
        }
    }

    if (   hw.llNetworkAdapters.size()
        && !hw.areAllNetworkAdaptersDefaultSettings(m->sv))
    {
        xml::ElementNode *pelmNetwork = pelmHardware->createChild("Network");
        for (NetworkAdaptersList::const_iterator it = hw.llNetworkAdapters.begin();
             it != hw.llNetworkAdapters.end();
             ++it)
        {
            const NetworkAdapter &nic = *it;

            if (!nic.areDefaultSettings(m->sv))
            {
                xml::ElementNode *pelmAdapter = pelmNetwork->createChild("Adapter");
                pelmAdapter->setAttribute("slot", nic.ulSlot);
                if (nic.fEnabled)
                    pelmAdapter->setAttribute("enabled", nic.fEnabled);
                if (!nic.strMACAddress.isEmpty())
                    pelmAdapter->setAttribute("MACAddress", nic.strMACAddress);
                if (   (m->sv >= SettingsVersion_v1_16 && !nic.fCableConnected)
                    || (m->sv < SettingsVersion_v1_16 && nic.fCableConnected))
                    pelmAdapter->setAttribute("cable", nic.fCableConnected);
                if (nic.ulLineSpeed)
                    pelmAdapter->setAttribute("speed", nic.ulLineSpeed);
                if (nic.ulBootPriority != 0)
                    pelmAdapter->setAttribute("bootPriority", nic.ulBootPriority);
                if (nic.fTraceEnabled)
                {
                    pelmAdapter->setAttribute("trace", nic.fTraceEnabled);
                    pelmAdapter->setAttribute("tracefile", nic.strTraceFile);
                }
                if (nic.strBandwidthGroup.isNotEmpty())
                    pelmAdapter->setAttribute("bandwidthGroup", nic.strBandwidthGroup);

                const char *pszPolicy;
                switch (nic.enmPromiscModePolicy)
                {
                    case NetworkAdapterPromiscModePolicy_Deny:          pszPolicy = NULL; break;
                    case NetworkAdapterPromiscModePolicy_AllowNetwork:  pszPolicy = "AllowNetwork"; break;
                    case NetworkAdapterPromiscModePolicy_AllowAll:      pszPolicy = "AllowAll"; break;
                    default:                                            pszPolicy = NULL; AssertFailed(); break;
                }
                if (pszPolicy)
                    pelmAdapter->setAttribute("promiscuousModePolicy", pszPolicy);

                if (   (m->sv >= SettingsVersion_v1_16 && nic.type != NetworkAdapterType_Am79C973)
                    || (m->sv < SettingsVersion_v1_16 && nic.type != NetworkAdapterType_Am79C970A))
                {
                    const char *pcszType;
                    switch (nic.type)
                    {
                        case NetworkAdapterType_Am79C973:   pcszType = "Am79C973"; break;
                        case NetworkAdapterType_Am79C960:   pcszType = "Am79C960"; break;
                        case NetworkAdapterType_I82540EM:   pcszType = "82540EM"; break;
                        case NetworkAdapterType_I82543GC:   pcszType = "82543GC"; break;
                        case NetworkAdapterType_I82545EM:   pcszType = "82545EM"; break;
                        case NetworkAdapterType_Virtio:     pcszType = "virtio"; break;
                        case NetworkAdapterType_NE1000:     pcszType = "NE1000"; break;
                        case NetworkAdapterType_NE2000:     pcszType = "NE2000"; break;
                        case NetworkAdapterType_WD8003:     pcszType = "WD8003"; break;
                        case NetworkAdapterType_WD8013:     pcszType = "WD8013"; break;
                        case NetworkAdapterType_ELNK2:      pcszType = "3C503"; break;
                        case NetworkAdapterType_ELNK1:      pcszType = "3C501"; break;
                        default: /*case NetworkAdapterType_Am79C970A:*/  pcszType = "Am79C970A"; break;
                    }
                    pelmAdapter->setAttribute("type", pcszType);
                }

                xml::ElementNode *pelmNAT;
                if (m->sv < SettingsVersion_v1_10)
                {
                    switch (nic.mode)
                    {
                        case NetworkAttachmentType_NAT:
                            pelmNAT = pelmAdapter->createChild("NAT");
                            if (nic.nat.strNetwork.length())
                                pelmNAT->setAttribute("network", nic.nat.strNetwork);
                            break;

                        case NetworkAttachmentType_Bridged:
                            pelmAdapter->createChild("BridgedInterface")->setAttribute("name", nic.strBridgedName);
                            break;

                        case NetworkAttachmentType_Internal:
                            pelmAdapter->createChild("InternalNetwork")->setAttribute("name", nic.strInternalNetworkName);
                            break;

                        case NetworkAttachmentType_HostOnly:
                            pelmAdapter->createChild("HostOnlyInterface")->setAttribute("name", nic.strHostOnlyName);
                            break;

                        default: /*case NetworkAttachmentType_Null:*/
                            break;
                    }
                }
                else
                {
                    /* m->sv >= SettingsVersion_v1_10 */
                    if (!nic.areDisabledDefaultSettings(m->sv))
                    {
                        xml::ElementNode *pelmDisabledNode = pelmAdapter->createChild("DisabledModes");
                        if (nic.mode != NetworkAttachmentType_NAT)
                            buildNetworkXML(NetworkAttachmentType_NAT, false, *pelmDisabledNode, nic);
                        if (nic.mode != NetworkAttachmentType_Bridged)
                            buildNetworkXML(NetworkAttachmentType_Bridged, false, *pelmDisabledNode, nic);
                        if (nic.mode != NetworkAttachmentType_Internal)
                            buildNetworkXML(NetworkAttachmentType_Internal, false, *pelmDisabledNode, nic);
                        if (nic.mode != NetworkAttachmentType_HostOnly)
                            buildNetworkXML(NetworkAttachmentType_HostOnly, false, *pelmDisabledNode, nic);
                        if (nic.mode != NetworkAttachmentType_Generic)
                            buildNetworkXML(NetworkAttachmentType_Generic, false, *pelmDisabledNode, nic);
                        if (nic.mode != NetworkAttachmentType_NATNetwork)
                            buildNetworkXML(NetworkAttachmentType_NATNetwork, false, *pelmDisabledNode, nic);
#ifdef VBOX_WITH_CLOUD_NET
                        /// @todo Bump settings version!
                        if (nic.mode != NetworkAttachmentType_Cloud)
                            buildNetworkXML(NetworkAttachmentType_Cloud, false, *pelmDisabledNode, nic);
#endif /* VBOX_WITH_CLOUD_NET */
#ifdef VBOX_WITH_VMNET
                        if (nic.mode != NetworkAttachmentType_HostOnlyNetwork)
                            buildNetworkXML(NetworkAttachmentType_HostOnlyNetwork, false, *pelmDisabledNode, nic);
#endif /* VBOX_WITH_VMNET */
                    }
                    buildNetworkXML(nic.mode, true, *pelmAdapter, nic);
                }
            }
        }
    }

    if (hw.llSerialPorts.size())
    {
        xml::ElementNode *pelmPorts = pelmHardware->createChild("UART");
        for (SerialPortsList::const_iterator it = hw.llSerialPorts.begin();
             it != hw.llSerialPorts.end();
             ++it)
        {
            const SerialPort &port = *it;
            xml::ElementNode *pelmPort = pelmPorts->createChild("Port");
            pelmPort->setAttribute("slot", port.ulSlot);
            pelmPort->setAttribute("enabled", port.fEnabled);
            if (m->sv >= SettingsVersion_v1_20) /* IOBase was renamed to IOAddress in v1.20. */
                pelmPort->setAttributeHex("IOAddress", port.ulIOAddress);
            else
                pelmPort->setAttributeHex("IOBase", port.ulIOAddress);
            pelmPort->setAttribute("IRQ", port.ulIRQ);

            const char *pcszHostMode;
            switch (port.portMode)
            {
                case PortMode_HostPipe: pcszHostMode = "HostPipe"; break;
                case PortMode_HostDevice: pcszHostMode = "HostDevice"; break;
                case PortMode_TCP: pcszHostMode = "TCP"; break;
                case PortMode_RawFile: pcszHostMode = "RawFile"; break;
                default: /*case PortMode_Disconnected:*/ pcszHostMode = "Disconnected"; break;
            }
            switch (port.portMode)
            {
                case PortMode_TCP:
                case PortMode_HostPipe:
                    pelmPort->setAttribute("server", port.fServer);
                    RT_FALL_THRU();
                case PortMode_HostDevice:
                case PortMode_RawFile:
                    pelmPort->setAttribute("path", port.strPath);
                    break;

                default:
                    break;
            }
            pelmPort->setAttribute("hostMode", pcszHostMode);

            if (   m->sv >= SettingsVersion_v1_17
                && port.uartType != UartType_U16550A)
            {
                const char *pcszUartType;

                switch (port.uartType)
                {
                    case UartType_U16450: pcszUartType = "16450"; break;
                    case UartType_U16550A: pcszUartType = "16550A"; break;
                    case UartType_U16750: pcszUartType = "16750"; break;
                    default: pcszUartType = "16550A"; break;
                }
                pelmPort->setAttribute("uartType", pcszUartType);
            }
        }
    }

    if (hw.llParallelPorts.size())
    {
        xml::ElementNode *pelmPorts = pelmHardware->createChild("LPT");
        for (ParallelPortsList::const_iterator it = hw.llParallelPorts.begin();
             it != hw.llParallelPorts.end();
             ++it)
        {
            const ParallelPort &port = *it;
            xml::ElementNode *pelmPort = pelmPorts->createChild("Port");
            pelmPort->setAttribute("slot", port.ulSlot);
            pelmPort->setAttribute("enabled", port.fEnabled);
            pelmPort->setAttributeHex("IOBase", port.ulIOBase);
            pelmPort->setAttribute("IRQ", port.ulIRQ);
            if (port.strPath.length())
                pelmPort->setAttribute("path", port.strPath);
        }
    }

    /* Always write the AudioAdapter config, intentionally not checking if
     * the settings are at the default, because that would be problematic
     * for the configured host driver type, which would automatically change
     * if the default host driver is detected differently. */
    {
        xml::ElementNode *pelmAudio = pelmHardware->createChild("AudioAdapter");

        const char *pcszController = NULL;
        switch (hw.audioAdapter.controllerType)
        {
            case AudioControllerType_VirtioSound:
                if (m->sv >= SettingsVersion_v1_20)
                {
                    pcszController = "Virtio-Sound";
                    break;
                }
                break;
            case AudioControllerType_SB16:
                pcszController = "SB16";
                break;
            case AudioControllerType_HDA:
                if (m->sv >= SettingsVersion_v1_11)
                {
                    pcszController = "HDA";
                    break;
                }
                RT_FALL_THRU();
            case AudioControllerType_AC97:
            default:
                pcszController = NULL;
                break;
        }
        if (pcszController)
            pelmAudio->setAttribute("controller", pcszController);

        const char *pcszCodec;
        switch (hw.audioAdapter.codecType)
        {
            /* Only write out the setting for non-default AC'97 codec
             * and leave the rest alone.
             */
#if 0
            case AudioCodecType_SB16:
                pcszCodec = "SB16";
                break;
            case AudioCodecType_STAC9221:
                pcszCodec = "STAC9221";
                break;
            case AudioCodecType_STAC9700:
                pcszCodec = "STAC9700";
                break;
#endif
            case AudioCodecType_AD1980:
                pcszCodec = "AD1980";
                break;
            default:
                /* Don't write out anything if unknown. */
                pcszCodec = NULL;
        }
        if (pcszCodec)
            pelmAudio->setAttribute("codec", pcszCodec);

        /*
         * Keep settings >= 1.19 compatible with older VBox versions (on a best effort basis, of course).
         * So use a dedicated attribute for the new "Default" audio driver type, which did not exist prior
         * settings 1.19 (VBox 7.0) and explicitly set the driver type to something older VBox versions
         * know about.
         */
        AudioDriverType_T driverType = hw.audioAdapter.driverType;

        if (driverType == AudioDriverType_Default)
        {
            /* Only recognized by VBox >= 7.0. */
            pelmAudio->setAttribute("useDefault", true);

            /* Make sure to set the actual driver type to the OS' default driver type.
             * This is required for VBox < 7.0. */
            driverType = getHostDefaultAudioDriver();
        }

        const char *pcszDriver = NULL;
        switch (driverType)
        {
            case AudioDriverType_Default: /* Handled above. */                  break;
            case AudioDriverType_WinMM:             pcszDriver = "WinMM";       break;
            case AudioDriverType_DirectSound:       pcszDriver = "DirectSound"; break;
            case AudioDriverType_WAS:               pcszDriver = "WAS";         break;
            case AudioDriverType_ALSA:              pcszDriver = "ALSA";        break;
            case AudioDriverType_OSS:               pcszDriver = "OSS";         break;
            case AudioDriverType_Pulse:             pcszDriver = "Pulse";       break;
            case AudioDriverType_CoreAudio:         pcszDriver = "CoreAudio";   break;
            case AudioDriverType_SolAudio:          pcszDriver = "SolAudio";    break;
            case AudioDriverType_MMPM:              pcszDriver = "MMPM";        break;
            default: /*case AudioDriverType_Null:*/ pcszDriver = "Null";        break;
        }

        /* Deliberately have the audio driver explicitly in the config file,
         * otherwise an unwritten default driver triggers auto-detection. */
        AssertStmt(pcszDriver != NULL, pcszDriver = "Null");
        pelmAudio->setAttribute("driver", pcszDriver);

        if (hw.audioAdapter.fEnabled || m->sv < SettingsVersion_v1_16)
            pelmAudio->setAttribute("enabled", hw.audioAdapter.fEnabled);

        if (   (m->sv <= SettingsVersion_v1_16 && !hw.audioAdapter.fEnabledIn)
            || (m->sv > SettingsVersion_v1_16 && hw.audioAdapter.fEnabledIn))
            pelmAudio->setAttribute("enabledIn", hw.audioAdapter.fEnabledIn);

        if (   (m->sv <= SettingsVersion_v1_16 && !hw.audioAdapter.fEnabledOut)
            || (m->sv > SettingsVersion_v1_16 && hw.audioAdapter.fEnabledOut))
            pelmAudio->setAttribute("enabledOut", hw.audioAdapter.fEnabledOut);

        if (m->sv >= SettingsVersion_v1_15 && hw.audioAdapter.properties.size() > 0)
        {
            for (StringsMap::const_iterator it = hw.audioAdapter.properties.begin();
                 it != hw.audioAdapter.properties.end();
                 ++it)
            {
                const Utf8Str &strName = it->first;
                const Utf8Str &strValue = it->second;
                xml::ElementNode *pelm = pelmAudio->createChild("Property");
                pelm->setAttribute("name", strName);
                pelm->setAttribute("value", strValue);
            }
        }
    }

    if (hw.llSharedFolders.size())
    {
        xml::ElementNode *pelmSharedFolders = pelmHardware->createChild("SharedFolders");
        for (SharedFoldersList::const_iterator it = hw.llSharedFolders.begin();
             it != hw.llSharedFolders.end();
             ++it)
        {
            const SharedFolder &sf = *it;
            xml::ElementNode *pelmThis = pelmSharedFolders->createChild("SharedFolder");
            pelmThis->setAttribute("name", sf.strName);
            pelmThis->setAttribute("hostPath", sf.strHostPath);
            pelmThis->setAttribute("writable", sf.fWritable);
            pelmThis->setAttribute("autoMount", sf.fAutoMount);
            if (sf.strAutoMountPoint.isNotEmpty())
                pelmThis->setAttribute("autoMountPoint", sf.strAutoMountPoint);
            const char *pcszSymlinkPolicy;
            if (sf.enmSymlinkPolicy != SymlinkPolicy_None)
            {
                switch (sf.enmSymlinkPolicy)
                {
                    default: /*case SymlinkPolicy_Forbidden:*/    pcszSymlinkPolicy = "forbidden";  break;
                    case SymlinkPolicy_AllowedInShareSubtree:     pcszSymlinkPolicy = "subtree";    break;
                    case SymlinkPolicy_AllowedToRelativeTargets:  pcszSymlinkPolicy = "relative";   break;
                    case SymlinkPolicy_AllowedToAnyTarget:        pcszSymlinkPolicy = "any";        break;
                }
                pelmThis->setAttribute("symlinkPolicy", pcszSymlinkPolicy);
            }
        }
    }

    xml::ElementNode *pelmClip = pelmHardware->createChild("Clipboard");
    if (pelmClip)
    {
        if (hw.clipboardMode != ClipboardMode_Disabled)
        {
            const char *pcszClip;
            switch (hw.clipboardMode)
            {
                default: /*case ClipboardMode_Disabled:*/ pcszClip = "Disabled"; break;
                case ClipboardMode_HostToGuest: pcszClip = "HostToGuest"; break;
                case ClipboardMode_GuestToHost: pcszClip = "GuestToHost"; break;
                case ClipboardMode_Bidirectional: pcszClip = "Bidirectional"; break;
            }
            pelmClip->setAttribute("mode", pcszClip);
        }

        if (hw.fClipboardFileTransfersEnabled)
            pelmClip->setAttribute("fileTransfersEnabled", hw.fClipboardFileTransfersEnabled);
    }

    if (hw.dndMode != DnDMode_Disabled)
    {
        xml::ElementNode *pelmDragAndDrop = pelmHardware->createChild("DragAndDrop");
        const char *pcszDragAndDrop;
        switch (hw.dndMode)
        {
            default: /*case DnDMode_Disabled:*/ pcszDragAndDrop = "Disabled"; break;
            case DnDMode_HostToGuest: pcszDragAndDrop = "HostToGuest"; break;
            case DnDMode_GuestToHost: pcszDragAndDrop = "GuestToHost"; break;
            case DnDMode_Bidirectional: pcszDragAndDrop = "Bidirectional"; break;
        }
        pelmDragAndDrop->setAttribute("mode", pcszDragAndDrop);
    }

    if (   m->sv >= SettingsVersion_v1_10
        && !hw.ioSettings.areDefaultSettings())
    {
        xml::ElementNode *pelmIO = pelmHardware->createChild("IO");
        xml::ElementNode *pelmIOCache;

        if (!hw.ioSettings.areDefaultSettings())
        {
            pelmIOCache = pelmIO->createChild("IoCache");
            if (!hw.ioSettings.fIOCacheEnabled)
                pelmIOCache->setAttribute("enabled", hw.ioSettings.fIOCacheEnabled);
            if (hw.ioSettings.ulIOCacheSize != 5)
                pelmIOCache->setAttribute("size", hw.ioSettings.ulIOCacheSize);
        }

        if (   m->sv >= SettingsVersion_v1_11
            && hw.ioSettings.llBandwidthGroups.size())
        {
            xml::ElementNode *pelmBandwidthGroups = pelmIO->createChild("BandwidthGroups");
            for (BandwidthGroupList::const_iterator it = hw.ioSettings.llBandwidthGroups.begin();
                 it != hw.ioSettings.llBandwidthGroups.end();
                 ++it)
            {
                const BandwidthGroup &gr = *it;
                const char *pcszType;
                xml::ElementNode *pelmThis = pelmBandwidthGroups->createChild("BandwidthGroup");
                pelmThis->setAttribute("name", gr.strName);
                switch (gr.enmType)
                {
                    case BandwidthGroupType_Network: pcszType = "Network"; break;
                    default: /* BandwidthGrouptype_Disk */ pcszType = "Disk"; break;
                }
                pelmThis->setAttribute("type", pcszType);
                if (m->sv >= SettingsVersion_v1_13)
                    pelmThis->setAttribute("maxBytesPerSec", gr.cMaxBytesPerSec);
                else
                    pelmThis->setAttribute("maxMbPerSec", gr.cMaxBytesPerSec / _1M);
            }
        }
    }

    if (   m->sv >= SettingsVersion_v1_12
        && hw.pciAttachments.size())
    {
        xml::ElementNode *pelmPCI = pelmHardware->createChild("HostPci");
        xml::ElementNode *pelmPCIDevices = pelmPCI->createChild("Devices");

        for (HostPCIDeviceAttachmentList::const_iterator it = hw.pciAttachments.begin();
             it != hw.pciAttachments.end();
             ++it)
        {
            const HostPCIDeviceAttachment &hpda = *it;

            xml::ElementNode *pelmThis = pelmPCIDevices->createChild("Device");

            pelmThis->setAttribute("host",  hpda.uHostAddress);
            pelmThis->setAttribute("guest", hpda.uGuestAddress);
            pelmThis->setAttribute("name",  hpda.strDeviceName);
        }
    }

    if (   m->sv >= SettingsVersion_v1_12
        && hw.fEmulatedUSBCardReader)
    {
        xml::ElementNode *pelmEmulatedUSB = pelmHardware->createChild("EmulatedUSB");

        xml::ElementNode *pelmCardReader = pelmEmulatedUSB->createChild("CardReader");
        pelmCardReader->setAttribute("enabled", hw.fEmulatedUSBCardReader);
    }

    if (   m->sv >= SettingsVersion_v1_14
        && !hw.strDefaultFrontend.isEmpty())
    {
        xml::ElementNode *pelmFrontend = pelmHardware->createChild("Frontend");
        xml::ElementNode *pelmDefault = pelmFrontend->createChild("Default");
        pelmDefault->setAttribute("type", hw.strDefaultFrontend);
    }

    if (hw.ulMemoryBalloonSize)
    {
        xml::ElementNode *pelmGuest = pelmHardware->createChild("Guest");
        pelmGuest->setAttribute("memoryBalloonSize", hw.ulMemoryBalloonSize);
    }

    if (hw.llGuestProperties.size())
    {
        xml::ElementNode *pelmGuestProps = pelmHardware->createChild("GuestProperties");
        for (GuestPropertiesList::const_iterator it = hw.llGuestProperties.begin();
             it != hw.llGuestProperties.end();
             ++it)
        {
            const GuestProperty &prop = *it;
            xml::ElementNode *pelmProp = pelmGuestProps->createChild("GuestProperty");
            pelmProp->setAttribute("name", prop.strName);
            pelmProp->setAttribute("value", prop.strValue);
            pelmProp->setAttribute("timestamp", prop.timestamp);
            pelmProp->setAttribute("flags", prop.strFlags);
        }
    }

    /* Starting with settings version of 6.0 (and only 6.1 and later does this, while
     * 5.2 and 6.0 understand it), place storage controller settings under hardware,
     * where it always should've been. */
    xml::ElementNode &elmStorageParent = (m->sv >= SettingsVersion_v1_17) ? *pelmHardware : elmParent;
    buildStorageControllersXML(elmStorageParent,
                               hw.storage,
                               !!(fl & BuildMachineXML_SkipRemovableMedia),
                               pllElementsWithUuidAttributes);
}

/**
 * Fill a \<Network\> node. Only relevant for XML version >= v1_10.
 * @param mode
 * @param fEnabled
 * @param elmParent
 * @param nic
 */
void MachineConfigFile::buildNetworkXML(NetworkAttachmentType_T mode,
                                        bool fEnabled,
                                        xml::ElementNode &elmParent,
                                        const NetworkAdapter &nic)
{
    switch (mode)
    {
        case NetworkAttachmentType_NAT:
            // For the currently active network attachment type we have to
            // generate the tag, otherwise the attachment type is lost.
            if (fEnabled || !nic.nat.areDefaultSettings(m->sv))
            {
                xml::ElementNode *pelmNAT = elmParent.createChild("NAT");

                if (!nic.nat.areDefaultSettings(m->sv))
                {
                    if (nic.nat.strNetwork.length())
                        pelmNAT->setAttribute("network", nic.nat.strNetwork);
                    if (nic.nat.strBindIP.length())
                        pelmNAT->setAttribute("hostip", nic.nat.strBindIP);
                    if (nic.nat.u32Mtu)
                        pelmNAT->setAttribute("mtu", nic.nat.u32Mtu);
                    if (nic.nat.u32SockRcv)
                        pelmNAT->setAttribute("sockrcv", nic.nat.u32SockRcv);
                    if (nic.nat.u32SockSnd)
                        pelmNAT->setAttribute("socksnd", nic.nat.u32SockSnd);
                    if (nic.nat.u32TcpRcv)
                        pelmNAT->setAttribute("tcprcv", nic.nat.u32TcpRcv);
                    if (nic.nat.u32TcpSnd)
                        pelmNAT->setAttribute("tcpsnd", nic.nat.u32TcpSnd);
                    if (!nic.nat.areLocalhostReachableDefaultSettings(m->sv))
                        pelmNAT->setAttribute("localhost-reachable", nic.nat.fLocalhostReachable);
                    if (!nic.nat.areDNSDefaultSettings())
                    {
                        xml::ElementNode *pelmDNS = pelmNAT->createChild("DNS");
                        if (!nic.nat.fDNSPassDomain)
                            pelmDNS->setAttribute("pass-domain", nic.nat.fDNSPassDomain);
                        if (nic.nat.fDNSProxy)
                            pelmDNS->setAttribute("use-proxy", nic.nat.fDNSProxy);
                        if (nic.nat.fDNSUseHostResolver)
                            pelmDNS->setAttribute("use-host-resolver", nic.nat.fDNSUseHostResolver);
                    }

                    if (!nic.nat.areAliasDefaultSettings())
                    {
                        xml::ElementNode *pelmAlias = pelmNAT->createChild("Alias");
                        if (nic.nat.fAliasLog)
                            pelmAlias->setAttribute("logging", nic.nat.fAliasLog);
                        if (nic.nat.fAliasProxyOnly)
                            pelmAlias->setAttribute("proxy-only", nic.nat.fAliasProxyOnly);
                        if (nic.nat.fAliasUseSamePorts)
                            pelmAlias->setAttribute("use-same-ports", nic.nat.fAliasUseSamePorts);
                    }

                    if (!nic.nat.areTFTPDefaultSettings())
                    {
                        xml::ElementNode *pelmTFTP;
                        pelmTFTP = pelmNAT->createChild("TFTP");
                        if (nic.nat.strTFTPPrefix.length())
                            pelmTFTP->setAttribute("prefix", nic.nat.strTFTPPrefix);
                        if (nic.nat.strTFTPBootFile.length())
                            pelmTFTP->setAttribute("boot-file", nic.nat.strTFTPBootFile);
                        if (nic.nat.strTFTPNextServer.length())
                            pelmTFTP->setAttribute("next-server", nic.nat.strTFTPNextServer);
                    }
                    buildNATForwardRulesMap(*pelmNAT, nic.nat.mapRules);
                }
            }
            break;

        case NetworkAttachmentType_Bridged:
            // For the currently active network attachment type we have to
            // generate the tag, otherwise the attachment type is lost.
            if (fEnabled || !nic.strBridgedName.isEmpty())
            {
                xml::ElementNode *pelmMode = elmParent.createChild("BridgedInterface");
                if (!nic.strBridgedName.isEmpty())
                    pelmMode->setAttribute("name", nic.strBridgedName);
            }
            break;

        case NetworkAttachmentType_Internal:
            // For the currently active network attachment type we have to
            // generate the tag, otherwise the attachment type is lost.
            if (fEnabled || !nic.strInternalNetworkName.isEmpty())
            {
                xml::ElementNode *pelmMode = elmParent.createChild("InternalNetwork");
                if (!nic.strInternalNetworkName.isEmpty())
                    pelmMode->setAttribute("name", nic.strInternalNetworkName);
            }
            break;

        case NetworkAttachmentType_HostOnly:
            // For the currently active network attachment type we have to
            // generate the tag, otherwise the attachment type is lost.
            if (fEnabled || !nic.strHostOnlyName.isEmpty())
            {
                xml::ElementNode *pelmMode = elmParent.createChild("HostOnlyInterface");
                if (!nic.strHostOnlyName.isEmpty())
                    pelmMode->setAttribute("name", nic.strHostOnlyName);
            }
            break;

#ifdef VBOX_WITH_VMNET
        case NetworkAttachmentType_HostOnlyNetwork:
            // For the currently active network attachment type we have to
            // generate the tag, otherwise the attachment type is lost.
            if (fEnabled || !nic.strHostOnlyNetworkName.isEmpty())
            {
                xml::ElementNode *pelmMode = elmParent.createChild("HostOnlyNetwork");
                if (!nic.strHostOnlyNetworkName.isEmpty())
                    pelmMode->setAttribute("name", nic.strHostOnlyNetworkName);
            }
            break;
#endif /* VBOX_WITH_VMNET */

        case NetworkAttachmentType_Generic:
            // For the currently active network attachment type we have to
            // generate the tag, otherwise the attachment type is lost.
            if (fEnabled || !nic.areGenericDriverDefaultSettings())
            {
                xml::ElementNode *pelmMode = elmParent.createChild("GenericInterface");
                if (!nic.areGenericDriverDefaultSettings())
                {
                    pelmMode->setAttribute("driver", nic.strGenericDriver);
                    for (StringsMap::const_iterator it = nic.genericProperties.begin();
                         it != nic.genericProperties.end();
                         ++it)
                    {
                        xml::ElementNode *pelmProp = pelmMode->createChild("Property");
                        pelmProp->setAttribute("name", it->first);
                        pelmProp->setAttribute("value", it->second);
                    }
                }
            }
            break;

        case NetworkAttachmentType_NATNetwork:
            // For the currently active network attachment type we have to
            // generate the tag, otherwise the attachment type is lost.
            if (fEnabled || !nic.strNATNetworkName.isEmpty())
            {
                xml::ElementNode *pelmMode = elmParent.createChild("NATNetwork");
                if (!nic.strNATNetworkName.isEmpty())
                    pelmMode->setAttribute("name", nic.strNATNetworkName);
            }
            break;

#ifdef VBOX_WITH_CLOUD_NET
        case NetworkAttachmentType_Cloud:
            // For the currently active network attachment type we have to
            // generate the tag, otherwise the attachment type is lost.
            if (fEnabled || !nic.strCloudNetworkName.isEmpty())
            {
                xml::ElementNode *pelmMode = elmParent.createChild("CloudNetwork");
                if (!nic.strCloudNetworkName.isEmpty())
                    pelmMode->setAttribute("name", nic.strCloudNetworkName);
            }
            break;
#endif /* VBOX_WITH_CLOUD_NET */

        default: /*case NetworkAttachmentType_Null:*/
            break;
    }
}

/**
 * Creates a \<StorageControllers\> node under elmParent and then writes out the XML
 * keys under that. Called for both the \<Machine\> node and for snapshots.
 * @param elmParent
 * @param st
 * @param fSkipRemovableMedia If true, DVD and floppy attachments are skipped and
 *   an empty drive is always written instead. This is for the OVF export case.
 *   This parameter is ignored unless the settings version is at least v1.9, which
 *   is always the case when this gets called for OVF export.
 * @param pllElementsWithUuidAttributes If not NULL, must point to a list of element node
 *   pointers to which we will append all elements that we created here that contain
 *   UUID attributes. This allows the OVF export code to quickly replace the internal
 *   media UUIDs with the UUIDs of the media that were exported.
 */
void MachineConfigFile::buildStorageControllersXML(xml::ElementNode &elmParent,
                                                   const Storage &st,
                                                   bool fSkipRemovableMedia,
                                                   std::list<xml::ElementNode*> *pllElementsWithUuidAttributes)
{
    if (!st.llStorageControllers.size())
        return;
    xml::ElementNode *pelmStorageControllers = elmParent.createChild("StorageControllers");

    for (StorageControllersList::const_iterator it = st.llStorageControllers.begin();
         it != st.llStorageControllers.end();
         ++it)
    {
        const StorageController &sc = *it;

        if (    (m->sv < SettingsVersion_v1_9)
             && (sc.controllerType == StorageControllerType_I82078)
           )
            // floppy controller already got written into <Hardware>/<FloppyController> in buildHardwareXML()
            // for pre-1.9 settings
            continue;

        xml::ElementNode *pelmController = pelmStorageControllers->createChild("StorageController");
        com::Utf8Str name = sc.strName;
        if (m->sv < SettingsVersion_v1_8)
        {
            // pre-1.8 settings use shorter controller names, they are
            // expanded when reading the settings
            if (name == "IDE Controller")
                name = "IDE";
            else if (name == "SATA Controller")
                name = "SATA";
            else if (name == "SCSI Controller")
                name = "SCSI";
        }
        pelmController->setAttribute("name", sc.strName);

        const char *pcszType;
        switch (sc.controllerType)
        {
            case StorageControllerType_IntelAhci: pcszType = "AHCI"; break;
            case StorageControllerType_LsiLogic: pcszType = "LsiLogic"; break;
            case StorageControllerType_BusLogic: pcszType = "BusLogic"; break;
            case StorageControllerType_PIIX4: pcszType = "PIIX4"; break;
            case StorageControllerType_ICH6: pcszType = "ICH6"; break;
            case StorageControllerType_I82078: pcszType = "I82078"; break;
            case StorageControllerType_LsiLogicSas: pcszType = "LsiLogicSas"; break;
            case StorageControllerType_USB: pcszType = "USB"; break;
            case StorageControllerType_NVMe: pcszType = "NVMe"; break;
            case StorageControllerType_VirtioSCSI: pcszType = "VirtioSCSI"; break;
            default: /*case StorageControllerType_PIIX3:*/ pcszType = "PIIX3"; break;
        }
        pelmController->setAttribute("type", pcszType);

        pelmController->setAttribute("PortCount", sc.ulPortCount);

        if (m->sv >= SettingsVersion_v1_9)
            if (sc.ulInstance)
                pelmController->setAttribute("Instance", sc.ulInstance);

        if (m->sv >= SettingsVersion_v1_10)
            pelmController->setAttribute("useHostIOCache", sc.fUseHostIOCache);

        if (m->sv >= SettingsVersion_v1_11)
            pelmController->setAttribute("Bootable", sc.fBootable);

        if (sc.controllerType == StorageControllerType_IntelAhci)
        {
            pelmController->setAttribute("IDE0MasterEmulationPort", 0);
            pelmController->setAttribute("IDE0SlaveEmulationPort", 1);
            pelmController->setAttribute("IDE1MasterEmulationPort", 2);
            pelmController->setAttribute("IDE1SlaveEmulationPort", 3);
        }

        for (AttachedDevicesList::const_iterator it2 = sc.llAttachedDevices.begin();
             it2 != sc.llAttachedDevices.end();
             ++it2)
        {
            const AttachedDevice &att = *it2;

            // For settings version before 1.9, DVDs and floppies are in hardware, not storage controllers,
            // so we shouldn't write them here; we only get here for DVDs though because we ruled out
            // the floppy controller at the top of the loop
            if (    att.deviceType == DeviceType_DVD
                 && m->sv < SettingsVersion_v1_9
               )
                continue;

            xml::ElementNode *pelmDevice = pelmController->createChild("AttachedDevice");

            pcszType = NULL;

            switch (att.deviceType)
            {
                case DeviceType_HardDisk:
                    pcszType = "HardDisk";
                    if (att.fNonRotational)
                        pelmDevice->setAttribute("nonrotational", att.fNonRotational);
                    if (att.fDiscard)
                        pelmDevice->setAttribute("discard", att.fDiscard);
                    break;

                case DeviceType_DVD:
                    pcszType = "DVD";
                    pelmDevice->setAttribute("passthrough", att.fPassThrough);
                    if (att.fTempEject)
                        pelmDevice->setAttribute("tempeject", att.fTempEject);
                    break;

                case DeviceType_Floppy:
                    pcszType = "Floppy";
                    break;

                default: break; /* Shut up MSC. */
            }

            pelmDevice->setAttribute("type", pcszType);

            if (m->sv >= SettingsVersion_v1_15)
                pelmDevice->setAttribute("hotpluggable", att.fHotPluggable);

            pelmDevice->setAttribute("port", att.lPort);
            pelmDevice->setAttribute("device", att.lDevice);

            if (att.strBwGroup.length())
                pelmDevice->setAttribute("bandwidthGroup", att.strBwGroup);

            // attached image, if any
            if (!att.uuid.isZero()
                 && att.uuid.isValid()
                 && (att.deviceType == DeviceType_HardDisk
                      || !fSkipRemovableMedia
                    )
               )
            {
                xml::ElementNode *pelmImage = pelmDevice->createChild("Image");
                pelmImage->setAttribute("uuid", att.uuid.toStringCurly());

                // if caller wants a list of UUID elements, give it to them
                if (pllElementsWithUuidAttributes)
                    pllElementsWithUuidAttributes->push_back(pelmImage);
            }
            else if (    (m->sv >= SettingsVersion_v1_9)
                      && (att.strHostDriveSrc.length())
                    )
                pelmDevice->createChild("HostDrive")->setAttribute("src", att.strHostDriveSrc);
        }
    }
}

/**
 * Creates a \<Debugging\> node under elmParent and then writes out the XML
 * keys under that. Called for both the \<Machine\> node and for snapshots.
 *
 * @param elmParent     Parent element.
 * @param dbg           Debugging settings.
 */
void MachineConfigFile::buildDebuggingXML(xml::ElementNode &elmParent, const Debugging &dbg)
{
    if (m->sv < SettingsVersion_v1_13 || dbg.areDefaultSettings())
        return;

    xml::ElementNode *pElmDebugging = elmParent.createChild("Debugging");
    xml::ElementNode *pElmTracing   = pElmDebugging->createChild("Tracing");
    pElmTracing->setAttribute("enabled", dbg.fTracingEnabled);
    pElmTracing->setAttribute("allowTracingToAccessVM", dbg.fAllowTracingToAccessVM);
    pElmTracing->setAttribute("config", dbg.strTracingConfig);

    xml::ElementNode *pElmGuestDebug = pElmDebugging->createChild("GuestDebug");
    const char *pcszDebugProvider = NULL;
    const char *pcszIoProvider = NULL;

    switch (dbg.enmDbgProvider)
    {
        case GuestDebugProvider_None:    pcszDebugProvider = "None"; break;
        case GuestDebugProvider_GDB:     pcszDebugProvider = "GDB";  break;
        case GuestDebugProvider_KD:      pcszDebugProvider = "KD";   break;
        default:         AssertFailed(); pcszDebugProvider = "None"; break;
    }

    switch (dbg.enmIoProvider)
    {
        case GuestDebugIoProvider_None:  pcszIoProvider = "None"; break;
        case GuestDebugIoProvider_TCP:   pcszIoProvider = "TCP";  break;
        case GuestDebugIoProvider_UDP:   pcszIoProvider = "UDP";  break;
        case GuestDebugIoProvider_IPC:   pcszIoProvider = "IPC";  break;
        default:         AssertFailed(); pcszIoProvider = "None"; break;
    }

    pElmGuestDebug->setAttribute("provider", pcszDebugProvider);
    pElmGuestDebug->setAttribute("io",       pcszIoProvider);
    pElmGuestDebug->setAttribute("address",  dbg.strAddress);
    pElmGuestDebug->setAttribute("port",     dbg.ulPort);
}

/**
 * Creates a \<Autostart\> node under elmParent and then writes out the XML
 * keys under that. Called for both the \<Machine\> node and for snapshots.
 *
 * @param elmParent     Parent element.
 * @param autostrt      Autostart settings.
 */
void MachineConfigFile::buildAutostartXML(xml::ElementNode &elmParent, const Autostart &autostrt)
{
    const char *pcszAutostop = NULL;

    if (m->sv < SettingsVersion_v1_13 || autostrt.areDefaultSettings())
        return;

    xml::ElementNode *pElmAutostart = elmParent.createChild("Autostart");
    pElmAutostart->setAttribute("enabled", autostrt.fAutostartEnabled);
    pElmAutostart->setAttribute("delay", autostrt.uAutostartDelay);

    switch (autostrt.enmAutostopType)
    {
        case AutostopType_Disabled:     pcszAutostop = "Disabled";     break;
        case AutostopType_SaveState:    pcszAutostop = "SaveState";    break;
        case AutostopType_PowerOff:     pcszAutostop = "PowerOff";     break;
        case AutostopType_AcpiShutdown: pcszAutostop = "AcpiShutdown"; break;
        default:         Assert(false); pcszAutostop = "Disabled";     break;
    }
    pElmAutostart->setAttribute("autostop", pcszAutostop);
}

void MachineConfigFile::buildRecordingXML(xml::ElementNode &elmParent, const Recording &Settings)
{
    if (Settings.areDefaultSettings()) /* Omit branch if we still have the default settings (i.e. nothing to save). */
        return;

    AssertReturnVoid(Settings.mapScreens.size() <= 64); /* Make sure we never exceed the bitmap of 64 monitors. */

    /* Note: Since settings 1.19 the recording settings have a dedicated XML branch outside of Hardware. */
    if (m->sv >= SettingsVersion_v1_19 /* VBox >= 7.0 */)
    {
        /* Note: elmParent is Machine or Snapshot. */
        xml::ElementNode *pelmRecording = elmParent.createChild("Recording");

        if (!recordingSettings.common.areDefaultSettings())
        {
            pelmRecording->setAttribute("enabled", Settings.common.fEnabled);
        }

        /* Only serialize screens which have non-default settings. */
        uint32_t cScreensToWrite = 0;

        RecordingScreenSettingsMap::const_iterator itScreen = Settings.mapScreens.begin();
        while (itScreen != Settings.mapScreens.end())
        {
            if (!itScreen->second.areDefaultSettings())
                cScreensToWrite++;
            ++itScreen;
        }

        if (cScreensToWrite)
            pelmRecording->setAttribute("screens", cScreensToWrite);

        itScreen = Settings.mapScreens.begin();
        while (itScreen != Settings.mapScreens.end())
        {
            if (!itScreen->second.areDefaultSettings()) /* Skip serializing screen settings which have default settings. */
            {
                xml::ElementNode *pelmScreen = pelmRecording->createChild("Screen");

                pelmScreen->setAttribute("id",                  itScreen->first); /* The key equals the monitor ID. */
                pelmScreen->setAttribute("enabled",             itScreen->second.fEnabled);
                com::Utf8Str strTemp;
                RecordingScreen::featuresToString(itScreen->second.featureMap, strTemp);
                pelmScreen->setAttribute("featuresEnabled",     strTemp);
                if (itScreen->second.ulMaxTimeS)
                    pelmScreen->setAttribute("maxTimeS",        itScreen->second.ulMaxTimeS);
                if (itScreen->second.strOptions.isNotEmpty())
                    pelmScreen->setAttributePath("options",     itScreen->second.strOptions);
                pelmScreen->setAttribute("dest",                itScreen->second.enmDest);
                if (!itScreen->second.File.strName.isEmpty())
                    pelmScreen->setAttributePath("file",        itScreen->second.File.strName);
                if (itScreen->second.File.ulMaxSizeMB)
                    pelmScreen->setAttribute("maxSizeMB",       itScreen->second.File.ulMaxSizeMB);

                RecordingScreen::videoCodecToString(itScreen->second.Video.enmCodec, strTemp);
                pelmScreen->setAttribute("videoCodec",          strTemp);
                if (itScreen->second.Video.enmDeadline != RecordingCodecDeadline_Default)
                    pelmScreen->setAttribute("videoDeadline",   itScreen->second.Video.enmDeadline);
                if (itScreen->second.Video.enmRateCtlMode != RecordingRateControlMode_VBR) /* Is default. */
                    pelmScreen->setAttribute("videoRateCtlMode", itScreen->second.Video.enmRateCtlMode);
                if (itScreen->second.Video.enmScalingMode != RecordingVideoScalingMode_None)
                    pelmScreen->setAttribute("videoScalingMode",itScreen->second.Video.enmScalingMode);
                if (   itScreen->second.Video.ulWidth  != 1024
                    || itScreen->second.Video.ulHeight != 768)
                {
                    pelmScreen->setAttribute("horzRes",         itScreen->second.Video.ulWidth);
                    pelmScreen->setAttribute("vertRes",         itScreen->second.Video.ulHeight);
                }
                if (itScreen->second.Video.ulRate != 512)
                    pelmScreen->setAttribute("rateKbps",        itScreen->second.Video.ulRate);
                if (itScreen->second.Video.ulFPS)
                    pelmScreen->setAttribute("fps",             itScreen->second.Video.ulFPS);

                RecordingScreen::audioCodecToString(itScreen->second.Audio.enmCodec, strTemp);
                pelmScreen->setAttribute("audioCodec",          strTemp);
                if (itScreen->second.Audio.enmDeadline != RecordingCodecDeadline_Default)
                    pelmScreen->setAttribute("audioDeadline",   itScreen->second.Audio.enmDeadline);
                if (itScreen->second.Audio.enmRateCtlMode != RecordingRateControlMode_VBR) /* Is default. */
                    pelmScreen->setAttribute("audioRateCtlMode", itScreen->second.Audio.enmRateCtlMode);
                if (itScreen->second.Audio.uHz != 22050)
                    pelmScreen->setAttribute("audioHz",         itScreen->second.Audio.uHz);
                if (itScreen->second.Audio.cBits != 16)
                    pelmScreen->setAttribute("audioBits",       itScreen->second.Audio.cBits);
                if (itScreen->second.Audio.cChannels != 2)
                    pelmScreen->setAttribute("audioChannels",   itScreen->second.Audio.cChannels);
            }
            ++itScreen;
        }
    }
    else if (   m->sv >= SettingsVersion_v1_14
             && m->sv <  SettingsVersion_v1_19 /* VBox < 7.0 */)
    {
        /* Note: elmParent is Hardware or Snapshot. */
        xml::ElementNode *pelmVideoCapture = elmParent.createChild("VideoCapture");

        if (!recordingSettings.common.areDefaultSettings())
        {
            pelmVideoCapture->setAttribute("enabled", Settings.common.fEnabled);
        }

        /* Convert the enabled screens to the former uint64_t bit array and vice versa. */
        uint64_t uScreensBitmap = 0;
        RecordingScreenSettingsMap::const_iterator itScreen = Settings.mapScreens.begin();
        while (itScreen != Settings.mapScreens.end())
        {
            if (itScreen->second.fEnabled)
                uScreensBitmap |= RT_BIT_64(itScreen->first);
            ++itScreen;
        }

        if (uScreensBitmap)
            pelmVideoCapture->setAttribute("screens",      uScreensBitmap);

        Assert(Settings.mapScreens.size());
        const RecordingScreenSettingsMap::const_iterator itScreen0Settings = Settings.mapScreens.find(0);
        Assert(itScreen0Settings != Settings.mapScreens.end());

        if (itScreen0Settings->second.ulMaxTimeS)
            pelmVideoCapture->setAttribute("maxTime",      itScreen0Settings->second.ulMaxTimeS);
        if (itScreen0Settings->second.strOptions.isNotEmpty())
            pelmVideoCapture->setAttributePath("options",  itScreen0Settings->second.strOptions);

        if (!itScreen0Settings->second.File.strName.isEmpty())
            pelmVideoCapture->setAttributePath("file",     itScreen0Settings->second.File.strName);
        if (itScreen0Settings->second.File.ulMaxSizeMB)
            pelmVideoCapture->setAttribute("maxSize",      itScreen0Settings->second.File.ulMaxSizeMB);

        if (   itScreen0Settings->second.Video.ulWidth  != 1024
            || itScreen0Settings->second.Video.ulHeight != 768)
        {
            pelmVideoCapture->setAttribute("horzRes",      itScreen0Settings->second.Video.ulWidth);
            pelmVideoCapture->setAttribute("vertRes",      itScreen0Settings->second.Video.ulHeight);
        }
        if (itScreen0Settings->second.Video.ulRate != 512)
            pelmVideoCapture->setAttribute("rate",         itScreen0Settings->second.Video.ulRate);
        if (itScreen0Settings->second.Video.ulFPS)
            pelmVideoCapture->setAttribute("fps",          itScreen0Settings->second.Video.ulFPS);
    }
}

/**
 * Creates a \<Groups\> node under elmParent and then writes out the XML
 * keys under that. Called for the \<Machine\> node only.
 *
 * @param elmParent     Parent element.
 * @param llGroups      Groups list.
 */
void MachineConfigFile::buildGroupsXML(xml::ElementNode &elmParent, const StringsList &llGroups)
{
    if (   m->sv < SettingsVersion_v1_13 || llGroups.size() == 0
        || (llGroups.size() == 1 && llGroups.front() == "/"))
        return;

    xml::ElementNode *pElmGroups = elmParent.createChild("Groups");
    for (StringsList::const_iterator it = llGroups.begin();
         it != llGroups.end();
         ++it)
    {
        const Utf8Str &group = *it;
        xml::ElementNode *pElmGroup = pElmGroups->createChild("Group");
        pElmGroup->setAttribute("name", group);
    }
}

/**
 * Writes a single snapshot into the DOM tree. Initially this gets called from
 * MachineConfigFile::write() for the root snapshot of a machine, if present;
 * elmParent then points to the \<Snapshots\> node under the \<Machine\> node
 * to which \<Snapshot\> must be added. This may then continue processing the
 * child snapshots.
 *
 * @param elmParent
 * @param snap
 */
void MachineConfigFile::buildSnapshotXML(xml::ElementNode &elmParent,
                                         const Snapshot &snap)
{
    std::list<const Snapshot *> llSettingsTodo;
    llSettingsTodo.push_back(&snap);
    std::list<xml::ElementNode *> llElementsTodo;
    llElementsTodo.push_back(&elmParent);
    std::list<uint32_t> llDepthsTodo;
    llDepthsTodo.push_back(1);

    while (llSettingsTodo.size() > 0)
    {
        const Snapshot *pSnap = llSettingsTodo.front();
        llSettingsTodo.pop_front();
        xml::ElementNode *pElement = llElementsTodo.front();
        llElementsTodo.pop_front();
        uint32_t depth = llDepthsTodo.front();
        llDepthsTodo.pop_front();

        if (depth > SETTINGS_SNAPSHOT_DEPTH_MAX)
            throw ConfigFileError(this, NULL, N_("Maximum snapshot tree depth of %u exceeded"), SETTINGS_SNAPSHOT_DEPTH_MAX);

        xml::ElementNode *pelmSnapshot = pElement->createChild("Snapshot");

        pelmSnapshot->setAttribute("uuid", pSnap->uuid.toStringCurly());
        pelmSnapshot->setAttribute("name", pSnap->strName);
        pelmSnapshot->setAttribute("timeStamp", stringifyTimestamp(pSnap->timestamp));

        if (pSnap->strStateFile.length())
            pelmSnapshot->setAttributePath("stateFile", pSnap->strStateFile);

        if (pSnap->strDescription.length())
            pelmSnapshot->createChild("Description")->addContent(pSnap->strDescription);

        // We only skip removable media for OVF, but OVF never includes snapshots.
        buildHardwareXML(*pelmSnapshot, pSnap->hardware, 0 /* fl */, NULL /* pllElementsWithUuidAttributes */);
        buildDebuggingXML(*pelmSnapshot, pSnap->debugging);
        buildAutostartXML(*pelmSnapshot, pSnap->autostart);
        buildRecordingXML(*pelmSnapshot, pSnap->recordingSettings);
        // note: Groups exist only for Machine, not for Snapshot

        if (m->sv < SettingsVersion_v1_20) /* Before settings v1.20 the platform stuff lived directly in the Hardware element. */
        {
            xml::ElementNode *pelHardware = unconst(pelmSnapshot->findChildElement("Hardware"));
            if (pelHardware)
                buildPlatformXML(*pelHardware, pSnap->hardware, pSnap->hardware.platformSettings);
        }
        else /* Now lives outside of "Hardware", in "Platform". */
            buildPlatformXML(*pelmSnapshot, pSnap->hardware, pSnap->hardware.platformSettings);

        if (pSnap->llChildSnapshots.size())
        {
            xml::ElementNode *pelmChildren = pelmSnapshot->createChild("Snapshots");
            for (SnapshotsList::const_iterator it = pSnap->llChildSnapshots.begin();
                it != pSnap->llChildSnapshots.end();
                ++it)
            {
                llSettingsTodo.push_back(&*it);
                llElementsTodo.push_back(pelmChildren);
                llDepthsTodo.push_back(depth + 1);
            }
        }
    }
}

/**
 * Builds the XML DOM tree for the machine config under the given XML element.
 *
 * This has been separated out from write() so it can be called from elsewhere,
 * such as the OVF code, to build machine XML in an existing XML tree.
 *
 * As a result, this gets called from two locations:
 *
 *  --  MachineConfigFile::write();
 *
 *  --  Appliance::buildXMLForOneVirtualSystem()
 *
 * In fl, the following flag bits are recognized:
 *
 *  --  BuildMachineXML_MediaRegistry: If set, the machine's media registry will
 *      be written, if present. This is not set when called from OVF because OVF
 *      has its own variant of a media registry. This flag is ignored unless the
 *      settings version is at least v1.11 (VirtualBox 4.0).
 *
 *  --  BuildMachineXML_IncludeSnapshots: If set, descend into the snapshots tree
 *      of the machine and write out \<Snapshot\> and possibly more snapshots under
 *      that, if snapshots are present. Otherwise all snapshots are suppressed
 *      (when called from OVF).
 *
 *  --  BuildMachineXML_WriteVBoxVersionAttribute: If set, add a settingsVersion
 *      attribute to the machine tag with the vbox settings version. This is for
 *      the OVF export case in which we don't have the settings version set in
 *      the root element.
 *
 *  --  BuildMachineXML_SkipRemovableMedia: If set, removable media attachments
 *      (DVDs, floppies) are silently skipped. This is for the OVF export case
 *      until we support copying ISO and RAW media as well.  This flag is ignored
 *      unless the settings version is at least v1.9, which is always the case
 *      when this gets called for OVF export.
 *
 * --   BuildMachineXML_SuppressSavedState: If set, the Machine/stateFile
 *      attribute is never set. This is also for the OVF export case because we
 *      cannot save states with OVF.
 *
 * @param elmMachine XML \<Machine\> element to add attributes and elements to.
 * @param fl Flags.
 * @param pllElementsWithUuidAttributes pointer to list that should receive UUID elements or NULL;
 *        see buildStorageControllersXML() for details.
 */
void MachineConfigFile::buildMachineXML(xml::ElementNode &elmMachine,
                                        uint32_t fl,
                                        std::list<xml::ElementNode*> *pllElementsWithUuidAttributes)
{
    if (fl & BuildMachineXML_WriteVBoxVersionAttribute)
    {
        // add settings version attribute to machine element
        setVersionAttribute(elmMachine);
        LogRel(("Exporting settings file \"%s\" with version \"%s\"\n", m->strFilename.c_str(), m->strSettingsVersionFull.c_str()));
    }

    elmMachine.setAttribute("uuid", uuid.toStringCurly());
    elmMachine.setAttribute("name", machineUserData.strName);
    if (machineUserData.fDirectoryIncludesUUID)
        elmMachine.setAttribute("directoryIncludesUUID", machineUserData.fDirectoryIncludesUUID);
    if (!machineUserData.fNameSync)
        elmMachine.setAttribute("nameSync", machineUserData.fNameSync);
    if (machineUserData.strDescription.length())
        elmMachine.createChild("Description")->addContent(machineUserData.strDescription);

#ifdef GUEST_OS_ID_STYLE_PARTIAL_CLEANUP
    com::Utf8Str strOsType = machineUserData.strOsType;
    if (m->sv < SettingsVersion_v1_20)
        convertGuestOSTypeToPre1_20(strOsType);
    elmMachine.setAttribute("OSType", strOsType);
#else
    elmMachine.setAttribute("OSType", machineUserData.strOsType);
#endif

    if (m->sv >= SettingsVersion_v1_19)
    {
        if (strStateKeyId.length())
            elmMachine.setAttribute("stateKeyId", strStateKeyId);
        if (strStateKeyStore.length())
            elmMachine.setAttribute("stateKeyStore", strStateKeyStore);
        if (strLogKeyId.length())
            elmMachine.setAttribute("logKeyId", strLogKeyId);
        if (strLogKeyStore.length())
            elmMachine.setAttribute("logKeyStore", strLogKeyStore);
    }
    if (    strStateFile.length()
         && !(fl & BuildMachineXML_SuppressSavedState)
       )
        elmMachine.setAttributePath("stateFile", strStateFile);

    if ((fl & BuildMachineXML_IncludeSnapshots)
         && !uuidCurrentSnapshot.isZero()
         && uuidCurrentSnapshot.isValid())
        elmMachine.setAttribute("currentSnapshot", uuidCurrentSnapshot.toStringCurly());

    if (machineUserData.strSnapshotFolder.length())
        elmMachine.setAttributePath("snapshotFolder", machineUserData.strSnapshotFolder);
    if (!fCurrentStateModified)
        elmMachine.setAttribute("currentStateModified", fCurrentStateModified);
    elmMachine.setAttribute("lastStateChange", stringifyTimestamp(timeLastStateChange));
    if (fAborted)
        elmMachine.setAttribute("aborted", fAborted);

    switch (machineUserData.enmVMPriority)
    {
        case VMProcPriority_Flat:
            elmMachine.setAttribute("processPriority", "Flat");
            break;
        case VMProcPriority_Low:
            elmMachine.setAttribute("processPriority", "Low");
            break;
        case VMProcPriority_Normal:
            elmMachine.setAttribute("processPriority", "Normal");
            break;
        case VMProcPriority_High:
            elmMachine.setAttribute("processPriority", "High");
            break;
        default:
            break;
    }

    switch (machineUserData.enmExecEngine)
    {
        case VMExecutionEngine_HwVirt:
            elmMachine.setAttribute("executionEngine", "HwVirt");
            break;
        case VMExecutionEngine_NativeApi:
            elmMachine.setAttribute("executionEngine", "NativeApi");
            break;
        case VMExecutionEngine_Interpreter:
            elmMachine.setAttribute("executionEngine", "Interpreter");
            break;
        case VMExecutionEngine_Recompiler:
            elmMachine.setAttribute("executionEngine", "Recompiler");
            break;
        case VMExecutionEngine_Default:
        default:
            break;
    }

    // Please keep the icon last so that one doesn't have to check if there
    // is anything in the line after this very long attribute in the XML.
    if (machineUserData.ovIcon.size())
    {
        Utf8Str strIcon;
        toBase64(strIcon, machineUserData.ovIcon);
        elmMachine.setAttribute("icon", strIcon);
    }
    if (    m->sv >= SettingsVersion_v1_9
        &&  (   machineUserData.fTeleporterEnabled
            ||  machineUserData.uTeleporterPort
            ||  !machineUserData.strTeleporterAddress.isEmpty()
            ||  !machineUserData.strTeleporterPassword.isEmpty()
            )
       )
    {
        xml::ElementNode *pelmTeleporter = elmMachine.createChild("Teleporter");
        pelmTeleporter->setAttribute("enabled", machineUserData.fTeleporterEnabled);
        pelmTeleporter->setAttribute("port", machineUserData.uTeleporterPort);
        pelmTeleporter->setAttribute("address", machineUserData.strTeleporterAddress);
        pelmTeleporter->setAttribute("password", machineUserData.strTeleporterPassword);
    }

    if (    (fl & BuildMachineXML_MediaRegistry)
         && (m->sv >= SettingsVersion_v1_11)
       )
        buildMediaRegistry(elmMachine, mediaRegistry);

    buildExtraData(elmMachine, mapExtraDataItems);

    if (    (fl & BuildMachineXML_IncludeSnapshots)
         && llFirstSnapshot.size())
        buildSnapshotXML(elmMachine, llFirstSnapshot.front());

    buildHardwareXML(elmMachine, hardwareMachine, fl, pllElementsWithUuidAttributes);
    buildDebuggingXML(elmMachine, debugging);
    buildAutostartXML(elmMachine, autostart);

    /* Note: Must come *after* buildHardwareXML(), as the "Hardware" branch is needed. */
    if (   m->sv >= SettingsVersion_v1_14
        && m->sv  < SettingsVersion_v1_19) /* < VBox 7.0. */
    {
        xml::ElementNode *pelHardware = unconst(elmMachine.findChildElement("Hardware"));
        if (pelHardware)
            buildRecordingXML(*pelHardware, recordingSettings);
    }
    else if (m->sv >= SettingsVersion_v1_19) /* Now lives outside of "Hardware", in "Machine". */
        buildRecordingXML(elmMachine, recordingSettings);

    buildGroupsXML(elmMachine, machineUserData.llGroups);

    /* Note: Must come *after* buildHardwareXML(), as the "Hardware" branch is needed. */
    if (m->sv < SettingsVersion_v1_20)
    {
        xml::ElementNode *pelHardware = unconst(elmMachine.findChildElement("Hardware"));
        if (pelHardware)
            buildPlatformXML(*pelHardware, hardwareMachine, hardwareMachine.platformSettings);
    }
    else /* Now lives outside of "Hardware", in "Platform". */
        buildPlatformXML(elmMachine, hardwareMachine, hardwareMachine.platformSettings);
}

 /**
 * Builds encrypted config.
 *
 * @sa MachineConfigFile::buildMachineXML
 */
void MachineConfigFile::buildMachineEncryptedXML(xml::ElementNode &elmMachine,
                                                 uint32_t fl,
                                                 std::list<xml::ElementNode*> *pllElementsWithUuidAttributes,
                                                 PCVBOXCRYPTOIF pCryptoIf,
                                                 const char *pszPassword = NULL)
{
    if (   !pszPassword
        || !pCryptoIf)
        throw ConfigFileError(this, &elmMachine, N_("Password is required"));

    xml::Document *pDoc = new xml::Document;
    xml::ElementNode *pelmRoot = pDoc->createRootElement("Machine");
    pelmRoot->setAttribute("xmlns", VBOX_XML_NAMESPACE);
    // Have the code for producing a proper schema reference. Not used by most
    // tools, so don't bother doing it. The schema is not on the server anyway.
#ifdef VBOX_WITH_SETTINGS_SCHEMA
    pelmRoot->setAttribute("xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance");
    pelmRoot->setAttribute("xsi:schemaLocation", VBOX_XML_NAMESPACE " " VBOX_XML_SCHEMA);
#endif

    buildMachineXML(*pelmRoot, fl, pllElementsWithUuidAttributes);
    xml::XmlStringWriter writer;
    com::Utf8Str strMachineXml;
    int vrc = writer.write(*pDoc, &strMachineXml);
    delete pDoc;
    if (RT_SUCCESS(vrc))
    {
        VBOXCRYPTOCTX hCryptoCtx;
        if (strKeyStore.isEmpty())
        {
            vrc = pCryptoIf->pfnCryptoCtxCreate("AES-GCM256", pszPassword, &hCryptoCtx);
            if (RT_SUCCESS(vrc))
            {
                char *pszNewKeyStore;
                vrc = pCryptoIf->pfnCryptoCtxSave(hCryptoCtx, &pszNewKeyStore);
                if (RT_SUCCESS(vrc))
                {
                    strKeyStore = pszNewKeyStore;
                    RTStrFree(pszNewKeyStore);
                }
                else
                    pCryptoIf->pfnCryptoCtxDestroy(hCryptoCtx);
            }
        }
        else
            vrc = pCryptoIf->pfnCryptoCtxLoad(strKeyStore.c_str(), pszPassword, &hCryptoCtx);
        if (RT_SUCCESS(vrc))
        {
            IconBlob abEncrypted;
            size_t cbEncrypted = 0;
            vrc = pCryptoIf->pfnCryptoCtxQueryEncryptedSize(hCryptoCtx, strMachineXml.length(), &cbEncrypted);
            if (RT_SUCCESS(vrc))
            {
                abEncrypted.resize(cbEncrypted);
                vrc = pCryptoIf->pfnCryptoCtxEncrypt(hCryptoCtx, false /*fPartial*/, NULL /*pvIV*/, 0 /*cbIV*/,
                                                     strMachineXml.c_str(), strMachineXml.length(),
                                                     uuid.raw(), sizeof(RTUUID),
                                                     &abEncrypted[0], abEncrypted.size(), &cbEncrypted);
                int vrc2 = pCryptoIf->pfnCryptoCtxDestroy(hCryptoCtx);
                AssertRC(vrc2);
                if (RT_SUCCESS(vrc))
                {
                    abEncrypted.resize(cbEncrypted);
                    toBase64(strMachineXml, abEncrypted);
                    elmMachine.setAttribute("uuid", uuid.toStringCurly());
                    elmMachine.setAttribute("keyId", strKeyId);
                    elmMachine.setAttribute("keyStore", strKeyStore);
                    elmMachine.setContent(strMachineXml.c_str());
                }
            }
        }

        if (RT_FAILURE(vrc))
            throw ConfigFileError(this, &elmMachine, N_("Creating machine encrypted xml failed. (%Rrc)"), vrc);
    }
    else
        throw ConfigFileError(this, &elmMachine, N_("Creating machine xml failed. (%Rrc)"), vrc);
}

/**
 * Returns true only if the given AudioDriverType is supported on
 * the current host platform. For example, this would return false
 * for AudioDriverType_DirectSound when compiled on a Linux host.
 *
*  @return @c true if the current host supports the driver, @c false if not.
 * @param enmDrvType            AudioDriverType_* enum to test.
 */
/*static*/
bool MachineConfigFile::isAudioDriverAllowedOnThisHost(AudioDriverType_T enmDrvType)
{
    switch (enmDrvType)
    {
        case AudioDriverType_Default:
            RT_FALL_THROUGH();
        case AudioDriverType_Null:
            return true; /* Default and Null audio are always allowed. */
#ifdef RT_OS_WINDOWS
        case AudioDriverType_WAS:
            /* We only support WAS on systems we tested so far (Vista+). */
            if (RTSystemGetNtVersion() < RTSYSTEM_MAKE_NT_VERSION(6,1,0))
                break;
            RT_FALL_THROUGH();
        case AudioDriverType_DirectSound:
#endif
#ifdef VBOX_WITH_AUDIO_OSS
        case AudioDriverType_OSS:
#endif
#ifdef VBOX_WITH_AUDIO_ALSA
        case AudioDriverType_ALSA:
#endif
#ifdef VBOX_WITH_AUDIO_PULSE
        case AudioDriverType_Pulse:
#endif
#ifdef RT_OS_DARWIN
        case AudioDriverType_CoreAudio:
#endif
#ifdef RT_OS_OS2
        case AudioDriverType_MMPM:
#endif
            return true;
        default: break; /* Shut up MSC. */
    }

    return false;
}

/**
 * Returns the AudioDriverType_* which should be used by default on this
 * host platform. On Linux, this will check at runtime whether PulseAudio
 * or ALSA are actually supported on the first call.
 *
 * When more than one supported audio stack is available, choose the most suited
 * (probably newest in most cases) one.
 *
 * @return Default audio driver type for this host platform.
 */
/*static*/
AudioDriverType_T MachineConfigFile::getHostDefaultAudioDriver()
{
#if defined(RT_OS_WINDOWS)
    if (RTSystemGetNtVersion() >= RTSYSTEM_MAKE_NT_VERSION(6,1,0))
        return AudioDriverType_WAS;
    return AudioDriverType_DirectSound;

#elif defined(RT_OS_LINUX)
    /* On Linux, we need to check at runtime what's actually supported.
     * Descending precedence. */
    static RTCLockMtx s_mtx;
    static AudioDriverType_T s_enmLinuxDriver = AudioDriverType_Null;
    RTCLock lock(s_mtx);
    if (s_enmLinuxDriver == AudioDriverType_Null) /* Already determined from a former run? */
    {
# ifdef VBOX_WITH_AUDIO_PULSE
        /* Check for the pulse library & that the PulseAudio daemon is running. */
        if (   (   RTProcIsRunningByName("pulseaudio")
                /* We also use the PulseAudio backend when we find pipewire-pulse running, which
                 * acts as a PulseAudio-compatible daemon for Pipewire-enabled applications. See @ticketref{21575} */
                || RTProcIsRunningByName("pipewire-pulse"))
            && RTLdrIsLoadable("libpulse.so.0"))
        {
            s_enmLinuxDriver = AudioDriverType_Pulse;
        }
#endif /* VBOX_WITH_AUDIO_PULSE */

# ifdef VBOX_WITH_AUDIO_ALSA
        if (s_enmLinuxDriver == AudioDriverType_Null)
        {
            /* Check if we can load the ALSA library */
            if (RTLdrIsLoadable("libasound.so.2"))
                s_enmLinuxDriver = AudioDriverType_ALSA;
        }
# endif /* VBOX_WITH_AUDIO_ALSA */

# ifdef VBOX_WITH_AUDIO_OSS
        if (s_enmLinuxDriver == AudioDriverType_Null)
            s_enmLinuxDriver = AudioDriverType_OSS;
# endif /* VBOX_WITH_AUDIO_OSS */
    }
    return s_enmLinuxDriver;

#elif defined(RT_OS_DARWIN)
    return AudioDriverType_CoreAudio;

#elif defined(RT_OS_OS2)
    return AudioDriverType_MMPM;

#else /* All other platforms. */
# ifdef VBOX_WITH_AUDIO_OSS
    return AudioDriverType_OSS;
# else
    /* Return NULL driver as a fallback if nothing of the above is available. */
    return AudioDriverType_Null;
# endif
#endif
}

/**
 * Called from write() before calling ConfigFileBase::createStubDocument().
 * This adjusts the settings version in m->sv if incompatible settings require
 * a settings bump, whereas otherwise we try to preserve the settings version
 * to avoid breaking compatibility with older versions.
 *
 * We do the checks in here in reverse order: newest first, oldest last, so
 * that we avoid unnecessary checks since some of these are expensive.
 */
void MachineConfigFile::bumpSettingsVersionIfNeeded()
{
    if (m->sv < SettingsVersion_v1_20)
    {
        // VirtualBox 7.1 (settings v1.20) adds support for different VM platforms and the QEMU RAM based framebuffer device.
        if (   (   hardwareMachine.platformSettings.architectureType != PlatformArchitecture_None
                && hardwareMachine.platformSettings.architectureType != PlatformArchitecture_x86)
            || hardwareMachine.graphicsAdapter.graphicsControllerType == GraphicsControllerType_QemuRamFB
            || machineUserData.enmExecEngine != VMExecutionEngine_Default)
        {
            /* Note: The new chipset type ARMv8Virtual implies setting the platform architecture type to ARM. */
            m->sv = SettingsVersion_v1_20;
            return;
        }

        // VirtualBox 7.1 (settings v1.20) adds support for customizable control over Shared Folders symlink creation.
        if (hardwareMachine.llSharedFolders.size())
        {
            for (SharedFoldersList::const_iterator it = hardwareMachine.llSharedFolders.begin();
                 it != hardwareMachine.llSharedFolders.end();
                 ++it)
            {
                if (it->enmSymlinkPolicy != SymlinkPolicy_None)
                {
                    m->sv = SettingsVersion_v1_20;
                    return;
                }
            }
        }
    }

    if (m->sv < SettingsVersion_v1_19)
    {
        // VirtualBox 7.0 adds iommu device and full VM encryption.
        if (   hardwareMachine.platformSettings.iommuType != IommuType_None
            || strKeyId.isNotEmpty()
            || strKeyStore.isNotEmpty()
            || strStateKeyId.isNotEmpty()
            || strStateKeyStore.isNotEmpty()
            || hardwareMachine.nvramSettings.strKeyId.isNotEmpty()
            || hardwareMachine.nvramSettings.strKeyStore.isNotEmpty()
            /* Default for newly created VMs in VBox 7.0.
             * Older VMs might have a specific audio driver set (also for VMs created with < VBox 7.0). */
            || hardwareMachine.audioAdapter.driverType == AudioDriverType_Default
            || recordingSettings.areDefaultSettings() == false
            || strLogKeyId.isNotEmpty()
            || strLogKeyStore.isEmpty())
        {
            m->sv = SettingsVersion_v1_19;
            return;
        }

        // VirtualBox 7.0 adds a Trusted Platform Module.
        if (   hardwareMachine.tpmSettings.tpmType != TpmType_None
            || hardwareMachine.tpmSettings.strLocation.isNotEmpty())
        {
            m->sv = SettingsVersion_v1_19;
            return;
        }

        NetworkAdaptersList::const_iterator netit;
        for (netit = hardwareMachine.llNetworkAdapters.begin();
             netit != hardwareMachine.llNetworkAdapters.end();
             ++netit)
        {
            // VirtualBox 7.0 adds a flag if NAT can reach localhost.
            if (   netit->fEnabled
                && netit->mode == NetworkAttachmentType_NAT
                && !netit->nat.fLocalhostReachable)
            {
                m->sv = SettingsVersion_v1_19;
                break;
            }

#ifdef VBOX_WITH_VMNET
            // VirtualBox 7.0 adds a host-only network attachment.
            if (netit->mode == NetworkAttachmentType_HostOnlyNetwork)
            {
                m->sv = SettingsVersion_v1_19;
                break;
            }
#endif /* VBOX_WITH_VMNET */
        }

        // VirtualBox 7.0 adds guest debug settings.
        if (   debugging.enmDbgProvider != GuestDebugProvider_None
            || debugging.enmIoProvider != GuestDebugIoProvider_None
            || debugging.strAddress.isNotEmpty()
            || debugging.ulPort != 0)
        {
            m->sv = SettingsVersion_v1_19;
            return;
        }
    }

    if (m->sv < SettingsVersion_v1_18)
    {
        if (!hardwareMachine.nvramSettings.strNvramPath.isEmpty())
        {
            m->sv = SettingsVersion_v1_18;
            return;
        }

        // VirtualBox 6.1 adds AMD-V virtualized VMSAVE/VMLOAD setting.
        if (hardwareMachine.platformSettings.x86.fHWVirtExVirtVmsaveVmload == false)
        {
            m->sv = SettingsVersion_v1_18;
            return;
        }

        // VirtualBox 6.1 adds a virtio-scsi storage controller.
        for (StorageControllersList::const_iterator it = hardwareMachine.storage.llStorageControllers.begin();
             it != hardwareMachine.storage.llStorageControllers.end();
             ++it)
        {
            const StorageController &sctl = *it;

            if (sctl.controllerType == StorageControllerType_VirtioSCSI)
            {
                m->sv = SettingsVersion_v1_18;
                return;
            }
        }

#ifdef VBOX_WITH_CLOUD_NET
        NetworkAdaptersList::const_iterator netit;
        for (netit = hardwareMachine.llNetworkAdapters.begin();
             netit != hardwareMachine.llNetworkAdapters.end();
             ++netit)
        {
            // VirtualBox 6.1 adds support for cloud networks.
            if (   netit->fEnabled
                && netit->mode == NetworkAttachmentType_Cloud)
            {
                m->sv = SettingsVersion_v1_18;
                break;
            }

        }
#endif /* VBOX_WITH_CLOUD_NET */
    }

    if (m->sv < SettingsVersion_v1_17)
    {
        if (machineUserData.enmVMPriority != VMProcPriority_Default)
        {
            m->sv = SettingsVersion_v1_17;
            return;
        }

        // VirtualBox 6.0 adds nested hardware virtualization, using native API (NEM).
        if (   hardwareMachine.platformSettings.x86.fNestedHWVirt
            || hardwareMachine.platformSettings.x86.fHWVirtExUseNativeApi)
        {
            m->sv = SettingsVersion_v1_17;
            return;
        }
        if (hardwareMachine.llSharedFolders.size())
            for (SharedFoldersList::const_iterator it = hardwareMachine.llSharedFolders.begin();
                 it != hardwareMachine.llSharedFolders.end();
                 ++it)
                if (it->strAutoMountPoint.isNotEmpty())
                {
                    m->sv = SettingsVersion_v1_17;
                    return;
                }

        /*
         * Check if any serial port uses a non 16550A serial port.
         */
        for (SerialPortsList::const_iterator it = hardwareMachine.llSerialPorts.begin();
             it != hardwareMachine.llSerialPorts.end();
             ++it)
        {
            const SerialPort &port = *it;
            if (port.uartType != UartType_U16550A)
            {
                m->sv = SettingsVersion_v1_17;
                return;
            }
        }
    }

    if (m->sv < SettingsVersion_v1_16)
    {
        // VirtualBox 5.1 adds a NVMe storage controller, paravirt debug
        // options, cpu profile, APIC settings (CPU capability and BIOS).

        if (    hardwareMachine.strParavirtDebug.isNotEmpty()
            || (!hardwareMachine.strCpuProfile.equals("host") && hardwareMachine.strCpuProfile.isNotEmpty())
            ||  hardwareMachine.firmwareSettings.apicMode != APICMode_APIC
            || !hardwareMachine.platformSettings.x86.fAPIC
            ||  hardwareMachine.platformSettings.x86.fX2APIC
            ||  hardwareMachine.platformSettings.x86.fIBPBOnVMExit
            ||  hardwareMachine.platformSettings.x86.fIBPBOnVMEntry
            ||  hardwareMachine.platformSettings.x86.fSpecCtrl
            ||  hardwareMachine.platformSettings.x86.fSpecCtrlByHost
            || !hardwareMachine.platformSettings.x86.fL1DFlushOnSched
            ||  hardwareMachine.platformSettings.x86.fL1DFlushOnVMEntry
            || !hardwareMachine.platformSettings.x86.fMDSClearOnSched
            ||  hardwareMachine.platformSettings.x86.fMDSClearOnVMEntry)
        {
            m->sv = SettingsVersion_v1_16;
            return;
        }

        for (StorageControllersList::const_iterator it = hardwareMachine.storage.llStorageControllers.begin();
             it != hardwareMachine.storage.llStorageControllers.end();
             ++it)
        {
            const StorageController &sctl = *it;

            if (sctl.controllerType == StorageControllerType_NVMe)
            {
                m->sv = SettingsVersion_v1_16;
                return;
            }
        }

        for (CpuIdLeafsX86List::const_iterator it = hardwareMachine.platformSettings.x86.llCpuIdLeafs.begin();
             it != hardwareMachine.platformSettings.x86.llCpuIdLeafs.end();
             ++it)
            if (it->idxSub != 0)
            {
                m->sv = SettingsVersion_v1_16;
                return;
            }
    }

    if (m->sv < SettingsVersion_v1_15)
    {
        // VirtualBox 5.0 adds paravirt providers, explicit AHCI port hotplug
        // setting, USB storage controller, xHCI, serial port TCP backend
        // and VM process priority.

        /*
         * Check simple configuration bits first, loopy stuff afterwards.
         */
        if (   hardwareMachine.paravirtProvider != ParavirtProvider_Legacy
            || hardwareMachine.uCpuIdPortabilityLevel != 0)
        {
            m->sv = SettingsVersion_v1_15;
            return;
        }

        /*
         * Check whether the hotpluggable flag of all storage devices differs
         * from the default for old settings.
         * AHCI ports are hotpluggable by default every other device is not.
         * Also check if there are USB storage controllers.
         */
        for (StorageControllersList::const_iterator it = hardwareMachine.storage.llStorageControllers.begin();
             it != hardwareMachine.storage.llStorageControllers.end();
             ++it)
        {
            const StorageController &sctl = *it;

            if (sctl.controllerType == StorageControllerType_USB)
            {
                m->sv = SettingsVersion_v1_15;
                return;
            }

            for (AttachedDevicesList::const_iterator it2 = sctl.llAttachedDevices.begin();
                 it2 != sctl.llAttachedDevices.end();
                 ++it2)
            {
                const AttachedDevice &att = *it2;

                if (   (   att.fHotPluggable
                        && sctl.controllerType != StorageControllerType_IntelAhci)
                    || (   !att.fHotPluggable
                        && sctl.controllerType == StorageControllerType_IntelAhci))
                {
                    m->sv = SettingsVersion_v1_15;
                    return;
                }
            }
        }

        /*
         * Check if there is an xHCI (USB3) USB controller.
         */
        for (USBControllerList::const_iterator it = hardwareMachine.usbSettings.llUSBControllers.begin();
             it != hardwareMachine.usbSettings.llUSBControllers.end();
             ++it)
        {
            const USBController &ctrl = *it;
            if (ctrl.enmType == USBControllerType_XHCI)
            {
                m->sv = SettingsVersion_v1_15;
                return;
            }
        }

        /*
         * Check if any serial port uses the TCP backend.
         */
        for (SerialPortsList::const_iterator it = hardwareMachine.llSerialPorts.begin();
             it != hardwareMachine.llSerialPorts.end();
             ++it)
        {
            const SerialPort &port = *it;
            if (port.portMode == PortMode_TCP)
            {
                m->sv = SettingsVersion_v1_15;
                return;
            }
        }
    }

    if (m->sv < SettingsVersion_v1_14)
    {
        // VirtualBox 4.3 adds default frontend setting, graphics controller
        // setting, explicit long mode setting, (video) capturing and NAT networking.
        if (   !hardwareMachine.strDefaultFrontend.isEmpty()
            || hardwareMachine.graphicsAdapter.graphicsControllerType != GraphicsControllerType_VBoxVGA
            || hardwareMachine.platformSettings.x86.enmLongMode != PlatformX86::LongMode_Legacy
            || machineUserData.ovIcon.size() > 0
            || recordingSettings.common.fEnabled)
        {
            m->sv = SettingsVersion_v1_14;
            return;
        }
        NetworkAdaptersList::const_iterator netit;
        for (netit = hardwareMachine.llNetworkAdapters.begin();
             netit != hardwareMachine.llNetworkAdapters.end();
             ++netit)
        {
            if (netit->mode == NetworkAttachmentType_NATNetwork)
            {
                m->sv = SettingsVersion_v1_14;
                break;
            }
        }
    }

    if (m->sv < SettingsVersion_v1_14)
    {
        unsigned cOhciCtrls = 0;
        unsigned cEhciCtrls = 0;
        bool fNonStdName = false;

        for (USBControllerList::const_iterator it = hardwareMachine.usbSettings.llUSBControllers.begin();
             it != hardwareMachine.usbSettings.llUSBControllers.end();
             ++it)
        {
            const USBController &ctrl = *it;

            switch (ctrl.enmType)
            {
                case USBControllerType_OHCI:
                    cOhciCtrls++;
                    if (ctrl.strName != "OHCI")
                        fNonStdName = true;
                    break;
                case USBControllerType_EHCI:
                    cEhciCtrls++;
                    if (ctrl.strName != "EHCI")
                        fNonStdName = true;
                    break;
                default:
                    /* Anything unknown forces a bump. */
                    fNonStdName = true;
            }

            /* Skip checking other controllers if the settings bump is necessary. */
            if (cOhciCtrls > 1 || cEhciCtrls > 1 || fNonStdName)
            {
                m->sv = SettingsVersion_v1_14;
                break;
            }
        }
    }

    if (m->sv < SettingsVersion_v1_13)
    {
        // VirtualBox 4.2 adds tracing, autostart, UUID in directory and groups.
        if (   !debugging.areDefaultSettings()
            || !autostart.areDefaultSettings()
            || machineUserData.fDirectoryIncludesUUID
            || machineUserData.llGroups.size() > 1
            || machineUserData.llGroups.front() != "/")
            m->sv = SettingsVersion_v1_13;
    }

    if (m->sv < SettingsVersion_v1_13)
    {
        // VirtualBox 4.2 changes the units for bandwidth group limits.
        for (BandwidthGroupList::const_iterator it = hardwareMachine.ioSettings.llBandwidthGroups.begin();
             it != hardwareMachine.ioSettings.llBandwidthGroups.end();
             ++it)
        {
            const BandwidthGroup &gr = *it;
            if (gr.cMaxBytesPerSec % _1M)
            {
                // Bump version if a limit cannot be expressed in megabytes
                m->sv = SettingsVersion_v1_13;
                break;
            }
        }
    }

    if (m->sv < SettingsVersion_v1_12)
    {
        // VirtualBox 4.1 adds PCI passthrough and emulated USB Smart Card reader
        if (   hardwareMachine.pciAttachments.size()
            || hardwareMachine.fEmulatedUSBCardReader)
            m->sv = SettingsVersion_v1_12;
    }

    if (m->sv < SettingsVersion_v1_12)
    {
        // VirtualBox 4.1 adds a promiscuous mode policy to the network
        // adapters and a generic network driver transport.
        NetworkAdaptersList::const_iterator netit;
        for (netit = hardwareMachine.llNetworkAdapters.begin();
             netit != hardwareMachine.llNetworkAdapters.end();
             ++netit)
        {
            if (   netit->enmPromiscModePolicy != NetworkAdapterPromiscModePolicy_Deny
                || netit->mode == NetworkAttachmentType_Generic
                || !netit->areGenericDriverDefaultSettings()
               )
            {
                m->sv = SettingsVersion_v1_12;
                break;
            }
        }
    }

    if (m->sv < SettingsVersion_v1_11)
    {
        // VirtualBox 4.0 adds HD audio, CPU priorities, ~fault tolerance~,
        // per-machine media registries, VRDE, JRockitVE, bandwidth groups,
        // ICH9 chipset
        if (    hardwareMachine.audioAdapter.controllerType == AudioControllerType_HDA
             || hardwareMachine.ulCpuExecutionCap != 100
             || mediaRegistry.llHardDisks.size()
             || mediaRegistry.llDvdImages.size()
             || mediaRegistry.llFloppyImages.size()
             || !hardwareMachine.vrdeSettings.strVrdeExtPack.isEmpty()
             || !hardwareMachine.vrdeSettings.strAuthLibrary.isEmpty()
             || machineUserData.strOsType == "JRockitVE"
             || hardwareMachine.ioSettings.llBandwidthGroups.size()
             || hardwareMachine.platformSettings.chipsetType == ChipsetType_ICH9
           )
            m->sv = SettingsVersion_v1_11;
    }

    if (m->sv < SettingsVersion_v1_10)
    {
        /* If the properties contain elements other than "TCP/Ports" and "TCP/Address",
         * then increase the version to at least VBox 3.2, which can have video channel properties.
         */
        unsigned cOldProperties = 0;

        StringsMap::const_iterator it = hardwareMachine.vrdeSettings.mapProperties.find("TCP/Ports");
        if (it != hardwareMachine.vrdeSettings.mapProperties.end())
            cOldProperties++;
        it = hardwareMachine.vrdeSettings.mapProperties.find("TCP/Address");
        if (it != hardwareMachine.vrdeSettings.mapProperties.end())
            cOldProperties++;

        if (hardwareMachine.vrdeSettings.mapProperties.size() != cOldProperties)
            m->sv = SettingsVersion_v1_10;
    }

    if (m->sv < SettingsVersion_v1_11)
    {
        /* If the properties contain elements other than "TCP/Ports", "TCP/Address",
         * "VideoChannel/Enabled" and "VideoChannel/Quality" then increase the version to VBox 4.0.
         */
        unsigned cOldProperties = 0;

        StringsMap::const_iterator it = hardwareMachine.vrdeSettings.mapProperties.find("TCP/Ports");
        if (it != hardwareMachine.vrdeSettings.mapProperties.end())
            cOldProperties++;
        it = hardwareMachine.vrdeSettings.mapProperties.find("TCP/Address");
        if (it != hardwareMachine.vrdeSettings.mapProperties.end())
            cOldProperties++;
        it = hardwareMachine.vrdeSettings.mapProperties.find("VideoChannel/Enabled");
        if (it != hardwareMachine.vrdeSettings.mapProperties.end())
            cOldProperties++;
        it = hardwareMachine.vrdeSettings.mapProperties.find("VideoChannel/Quality");
        if (it != hardwareMachine.vrdeSettings.mapProperties.end())
            cOldProperties++;

        if (hardwareMachine.vrdeSettings.mapProperties.size() != cOldProperties)
            m->sv = SettingsVersion_v1_11;
    }

    // settings version 1.9 is required if there is not exactly one DVD
    // or more than one floppy drive present or the DVD is not at the secondary
    // master; this check is a bit more complicated
    //
    // settings version 1.10 is required if the host cache should be disabled
    //
    // settings version 1.11 is required for bandwidth limits and if more than
    // one controller of each type is present.
    if (m->sv < SettingsVersion_v1_11)
    {
        // count attached DVDs and floppies (only if < v1.9)
        size_t cDVDs = 0;
        size_t cFloppies = 0;

        // count storage controllers (if < v1.11)
        size_t cSata = 0;
        size_t cScsiLsi = 0;
        size_t cScsiBuslogic = 0;
        size_t cSas = 0;
        size_t cIde = 0;
        size_t cFloppy = 0;

        // need to run thru all the storage controllers and attached devices to figure this out
        for (StorageControllersList::const_iterator it = hardwareMachine.storage.llStorageControllers.begin();
             it != hardwareMachine.storage.llStorageControllers.end();
             ++it)
        {
            const StorageController &sctl = *it;

            // count storage controllers of each type; 1.11 is required if more than one
            // controller of one type is present
            switch (sctl.storageBus)
            {
                case StorageBus_IDE:
                    cIde++;
                    break;
                case StorageBus_SATA:
                    cSata++;
                    break;
                case StorageBus_SAS:
                    cSas++;
                    break;
                case StorageBus_SCSI:
                    if (sctl.controllerType == StorageControllerType_LsiLogic)
                        cScsiLsi++;
                    else
                        cScsiBuslogic++;
                    break;
                case StorageBus_Floppy:
                    cFloppy++;
                    break;
                default:
                    // Do nothing
                    break;
            }

            if (   cSata > 1
                || cScsiLsi > 1
                || cScsiBuslogic > 1
                || cSas > 1
                || cIde > 1
                || cFloppy > 1)
            {
                m->sv = SettingsVersion_v1_11;
                break; // abort the loop -- we will not raise the version further
            }

            for (AttachedDevicesList::const_iterator it2 = sctl.llAttachedDevices.begin();
                 it2 != sctl.llAttachedDevices.end();
                 ++it2)
            {
                const AttachedDevice &att = *it2;

                // Bandwidth limitations are new in VirtualBox 4.0 (1.11)
                if (m->sv < SettingsVersion_v1_11)
                {
                    if (att.strBwGroup.length() != 0)
                    {
                        m->sv = SettingsVersion_v1_11;
                        break; // abort the loop -- we will not raise the version further
                    }
                }

                // disabling the host IO cache requires settings version 1.10
                if (    (m->sv < SettingsVersion_v1_10)
                     && (!sctl.fUseHostIOCache)
                   )
                    m->sv = SettingsVersion_v1_10;

                // we can only write the StorageController/@Instance attribute with v1.9
                if (    (m->sv < SettingsVersion_v1_9)
                     && (sctl.ulInstance != 0)
                   )
                    m->sv = SettingsVersion_v1_9;

                if (m->sv < SettingsVersion_v1_9)
                {
                    if (att.deviceType == DeviceType_DVD)
                    {
                         if (    (sctl.storageBus != StorageBus_IDE) // DVD at bus other than DVD?
                              || (att.lPort != 1)                    // DVDs not at secondary master?
                              || (att.lDevice != 0)
                            )
                            m->sv = SettingsVersion_v1_9;

                        ++cDVDs;
                    }
                    else if (att.deviceType == DeviceType_Floppy)
                        ++cFloppies;
                }
            }

            if (m->sv >= SettingsVersion_v1_11)
                break;  // abort the loop -- we will not raise the version further
        }

        // VirtualBox before 3.1 had zero or one floppy and exactly one DVD,
        // so any deviation from that will require settings version 1.9
        if (    (m->sv < SettingsVersion_v1_9)
             && (    (cDVDs != 1)
                  || (cFloppies > 1)
                )
           )
            m->sv = SettingsVersion_v1_9;
    }

    // VirtualBox 3.2: Check for non default I/O settings
    if (m->sv < SettingsVersion_v1_10)
    {
        if (   (hardwareMachine.ioSettings.fIOCacheEnabled != true)
            || (hardwareMachine.ioSettings.ulIOCacheSize != 5)
                // and page fusion
            || (hardwareMachine.fPageFusionEnabled)
                // and CPU hotplug, RTC timezone control, HID type and HPET
            || hardwareMachine.platformSettings.fRTCUseUTC
            || hardwareMachine.fCpuHotPlug
            || hardwareMachine.pointingHIDType != PointingHIDType_PS2Mouse
            || hardwareMachine.keyboardHIDType != KeyboardHIDType_PS2Keyboard
            || hardwareMachine.platformSettings.x86.fHPETEnabled
           )
            m->sv = SettingsVersion_v1_10;
    }

    // VirtualBox 3.2 adds NAT and boot priority to the NIC config in Main
    // VirtualBox 4.0 adds network bandwitdth
    if (m->sv < SettingsVersion_v1_11)
    {
        NetworkAdaptersList::const_iterator netit;
        for (netit = hardwareMachine.llNetworkAdapters.begin();
             netit != hardwareMachine.llNetworkAdapters.end();
             ++netit)
        {
            if (    (m->sv < SettingsVersion_v1_12)
                 && (netit->strBandwidthGroup.isNotEmpty())
               )
            {
                /* New in VirtualBox 4.1 */
                m->sv = SettingsVersion_v1_12;
                break;
            }
            else if (    (m->sv < SettingsVersion_v1_10)
                      && (netit->fEnabled)
                      && (netit->mode == NetworkAttachmentType_NAT)
                      && (   netit->nat.u32Mtu != 0
                          || netit->nat.u32SockRcv != 0
                          || netit->nat.u32SockSnd != 0
                          || netit->nat.u32TcpRcv != 0
                          || netit->nat.u32TcpSnd != 0
                          || !netit->nat.fDNSPassDomain
                          || netit->nat.fDNSProxy
                          || netit->nat.fDNSUseHostResolver
                          || netit->nat.fAliasLog
                          || netit->nat.fAliasProxyOnly
                          || netit->nat.fAliasUseSamePorts
                          || netit->nat.strTFTPPrefix.length()
                          || netit->nat.strTFTPBootFile.length()
                          || netit->nat.strTFTPNextServer.length()
                          || netit->nat.mapRules.size()
                         )
                     )
            {
                m->sv = SettingsVersion_v1_10;
                // no break because we still might need v1.11 above
            }
            else if (    (m->sv < SettingsVersion_v1_10)
                      && (netit->fEnabled)
                      && (netit->ulBootPriority != 0)
                    )
            {
                m->sv = SettingsVersion_v1_10;
                // no break because we still might need v1.11 above
            }
        }
    }

    // all the following require settings version 1.9
    if (    (m->sv < SettingsVersion_v1_9)
        && (    (hardwareMachine.firmwareSettings.firmwareType >= FirmwareType_EFI)
              || machineUserData.fTeleporterEnabled
              || machineUserData.uTeleporterPort
              || !machineUserData.strTeleporterAddress.isEmpty()
              || !machineUserData.strTeleporterPassword.isEmpty()
              || (!hardwareMachine.uuid.isZero() && hardwareMachine.uuid.isValid())
            )
        )
        m->sv = SettingsVersion_v1_9;

    // "accelerate 2d video" requires settings version 1.8
    if (    (m->sv < SettingsVersion_v1_8)
         && (hardwareMachine.graphicsAdapter.fAccelerate2DVideo)
       )
        m->sv = SettingsVersion_v1_8;

    // The hardware versions other than "1" requires settings version 1.4 (2.1+).
    if (    m->sv < SettingsVersion_v1_4
         && hardwareMachine.strVersion != "1"
       )
        m->sv = SettingsVersion_v1_4;
}

/**
 * Called from Main code to write a machine config file to disk. This builds a DOM tree from
 * the member variables and then writes the XML file; it throws xml::Error instances on errors,
 * in particular if the file cannot be written.
 */
void MachineConfigFile::write(const com::Utf8Str &strFilename, PCVBOXCRYPTOIF pCryptoIf, const char *pszPassword)
{
    try
    {
        // createStubDocument() sets the settings version to at least 1.7; however,
        // we might need to enfore a later settings version if incompatible settings
        // are present:
        bumpSettingsVersionIfNeeded();

        m->strFilename = strFilename;
        /*
         * Only create a backup if it is not encrypted.
         * Otherwise we get an unencrypted copy of the settings.
         */
        if (strKeyId.isEmpty() && strKeyStore.isEmpty())
            specialBackupIfFirstBump();
        createStubDocument();

        if (strKeyStore.isNotEmpty())
        {
            xml::ElementNode *pelmMachine = m->pelmRoot->createChild("MachineEncrypted");
            buildMachineEncryptedXML(*pelmMachine,
                                       MachineConfigFile::BuildMachineXML_IncludeSnapshots
                                     | MachineConfigFile::BuildMachineXML_MediaRegistry,
                                         // but not BuildMachineXML_WriteVBoxVersionAttribute
                                     NULL, /* pllElementsWithUuidAttributes */
                                     pCryptoIf,
                                     pszPassword);
        }
        else
        {
            xml::ElementNode *pelmMachine = m->pelmRoot->createChild("Machine");
            buildMachineXML(*pelmMachine,
                              MachineConfigFile::BuildMachineXML_IncludeSnapshots
                            | MachineConfigFile::BuildMachineXML_MediaRegistry,
                                // but not BuildMachineXML_WriteVBoxVersionAttribute
                            NULL); /* pllElementsWithUuidAttributes */
        }

        // now go write the XML
        xml::XmlFileWriter writer(*m->pDoc);
        writer.write(m->strFilename.c_str(), true /*fSafe*/);

        m->fFileExists = true;
        clearDocument();
        LogRel(("Finished saving settings file \"%s\"\n", m->strFilename.c_str()));
    }
    catch (...)
    {
        clearDocument();
        LogRel(("Finished saving settings file \"%s\" with failure\n", m->strFilename.c_str()));
        throw;
    }
}
