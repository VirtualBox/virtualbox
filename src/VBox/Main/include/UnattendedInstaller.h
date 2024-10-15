/* $Id: UnattendedInstaller.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * UnattendedInstaller class header
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

#ifndef MAIN_INCLUDED_UnattendedInstaller_h
#define MAIN_INCLUDED_UnattendedInstaller_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "UnattendedScript.h"

/* Forward declarations */
class Unattended;
class UnattendedInstaller;
class BaseTextScript;


/**
 * The abstract UnattendedInstaller class declaration
 *
 * The class is intended to service a new VM that this VM will be able to
 * execute an unattended installation
 */
class UnattendedInstaller : public RTCNonCopyable
{
/*data*/
protected:
    /** Main unattended installation script. */
    UnattendedScriptTemplate    mMainScript;
    /** Full path to the main template file (set by initInstaller). */
    Utf8Str                     mStrMainScriptTemplate;

    /** Post installation (shell) script. */
    UnattendedScriptTemplate    mPostScript;
    /** Full path to the post template file (set by initInstaller). */
    Utf8Str                     mStrPostScriptTemplate;

    /** Pointer to the parent object.
     * We use this for setting errors and querying attributes. */
    Unattended                 *mpParent;
    /** The path of the extra ISO image we create (set by initInstaller).
     * This is only valid when isAdditionsIsoNeeded() returns true. */
    Utf8Str                     mStrAuxiliaryIsoFilePath;
    /** The path of the extra floppy image we create (set by initInstaller)
     * This is only valid when isAdditionsFloppyNeeded() returns true. */
    Utf8Str                     mStrAuxiliaryFloppyFilePath;
    /** The boot device. */
    DeviceType_T const          meBootDevice;
    /** Default extra install kernel parameters (set by constructor).
     * This can be overridden by the extraInstallKernelParameters attribute of
     * IUnattended. */
    Utf8Str                     mStrDefaultExtraInstallKernelParameters;
    /** The directory of the post install script in the unattended install
     * environment, i.e. when it gets started by the unattended installer
     * of the respective guest OS. */
    Utf8Str                     mStrAuxiliaryInstallDir;

private:
    UnattendedInstaller(); /* no default constructors */

public:
    DECLARE_TRANSLATE_METHODS(UnattendedInstaller)

    /**
     * Regular constructor.
     *
     * @param   pParent                     The parent object.  Used for setting
     *                                      errors and querying attributes.
     * @param   pszMainScriptTemplateName   The name of the template file (no path)
     *                                      for the main unattended installer
     *                                      script.
     * @param   pszPostScriptTemplateName   The name of the template file (no path)
     *                                      for the post installation script.
     * @param   pszMainScriptFilename       The main unattended installer script
     *                                      filename (on aux media).
     * @param   pszPostScriptFilename       The post installation script filename
     *                                      (on aux media).
     * @param   enmBootDevice               The boot device type.
     */
    UnattendedInstaller(Unattended *pParent,
                        const char *pszMainScriptTemplateName, const char *pszPostScriptTemplateName,
                        const char *pszMainScriptFilename,     const char *pszPostScriptFilename,
                        DeviceType_T enmBootDevice = DeviceType_DVD);
    virtual ~UnattendedInstaller();

    /**
     * Instantiates the appropriate child class.
     *
     * @returns Pointer to the new instance, NULL if no appropriate installer.
     * @param   enmDetectedOSType       The detected guest OS type value.
     * @param   strDetectedOSType       The detected guest OS type string
     * @param   strDetectedOSVersion    The detected guest OS version.
     * @param   strDetectedOSFlavor     The detected guest OS flavor.
     * @param   strDetectedOSHints      Hints about the detected guest OS.
     * @param   pParent                 The parent object.  Used for setting errors
     *                                  and querying attributes.
     * @throws  std::bad_alloc
     */
    static UnattendedInstaller *createInstance(VBOXOSTYPE enmDetectedOSType, const Utf8Str &strDetectedOSType,
                                               const Utf8Str &strDetectedOSVersion, const Utf8Str &strDetectedOSFlavor,
                                               const Utf8Str &strDetectedOSHints, Unattended *pParent);

    /**
     * Initialize the installer.
     *
     * @note This is called immediately after instantiation and the caller will
     *       always destroy the unattended installer instance on failure, so it
     *       is not necessary to keep track of whether this succeeded or not.
     */
    virtual HRESULT initInstaller();

#if 0 /* These are now in the AUX VISO. */
    /**
     * Whether the VBox Guest Additions ISO is needed or not.
     *
     * The default implementation always returns false when a VISO is used, see
     * UnattendedInstaller::addFilesToAuxVisoVectors.
     */
    virtual bool isAdditionsIsoNeeded() const;

    /**
     * Whether the VBox validation kit ISO is needed or not.
     *
     * The default implementation always returns false when a VISO is used, see
     * UnattendedInstaller::addFilesToAuxVisoVectors.
     */
    virtual bool isValidationKitIsoNeeded() const;
#endif

    /**
     * Indicates whether an original installation ISO is needed or not.
     */
    virtual bool isOriginalIsoNeeded() const        { return true; }

    /**
     * Indicates whether a floppy image is needed or not.
     */
    virtual bool isAuxiliaryFloppyNeeded() const    { return false; }

    /**
     * Indicates whether an additional or replacement ISO image is needed or not.
     */
    virtual bool isAuxiliaryIsoNeeded() const;

    /**
     * Indicates whether we should boot from the auxiliary ISO image.
     *
     * Will boot from installation ISO if false.
     */
    virtual bool bootFromAuxiliaryIso() const       { return isAuxiliaryIsoNeeded(); }

    /**
     * Indicates whether a the auxiliary ISO is a .viso-file rather than an
     * .iso-file.
     *
     * Different worker methods are used depending on the return value.  A
     * .viso-file is generally only used when the installation media needs to
     * be remastered with small changes and additions.
     */
    virtual bool isAuxiliaryIsoIsVISO() const       { return true; }

    /*
     * Getters
     */
    DeviceType_T   getBootableDeviceType() const        { return meBootDevice; }
    const Utf8Str &getTemplateFilePath() const          { return mStrMainScriptTemplate; }
    const Utf8Str &getPostTemplateFilePath() const      { return mStrPostScriptTemplate; }
    const Utf8Str &getAuxiliaryIsoFilePath() const      { return mStrAuxiliaryIsoFilePath; }
    const Utf8Str &getAuxiliaryFloppyFilePath() const   { return mStrAuxiliaryFloppyFilePath; }
    const Utf8Str &getDefaultExtraInstallKernelParameters() const { return mStrDefaultExtraInstallKernelParameters; }
    const Utf8Str &getAuxiliaryInstallDir() const       { return mStrAuxiliaryInstallDir; }

    /*
     * Setters
     */
    void setTemplatePath(const Utf8Str& data); /**< @todo r=bird: This is confusing as heck. Dir for a while, then it's a file. Not a comment about it. Brilliant. */

    /**
     * Prepares the unattended scripts, does all but write them to the installation
     * media.
     */
    HRESULT prepareUnattendedScripts();

    /**
     * Prepares the media - floppy image, ISO image.
     *
     * This method calls prepareAuxFloppyImage() and prepareAuxIsoImage(), child
     * classes may override these methods or methods they call.
     *
     * @returns COM status code.
     * @param   fOverwrite      Whether to overwrite media files or fail if they
     *                          already exist.
     */
    HRESULT prepareMedia(bool fOverwrite = true);

protected:
    /**
     * Prepares (creates) the auxiliary floppy image.
     *
     * This is called by the base class prepareMedia() when
     * isAuxiliaryFloppyNeeded() is true. The base class implementation puts the
     * edited unattended script onto it.
     */
    HRESULT prepareAuxFloppyImage(bool fOverwrite);

    /**
     * Creates and formats (FAT12) a floppy image.
     *
     * This can be overridden to do more preparation work or/and create a different
     * sized floppy.
     *
     * @returns COM status code.
     * @param   pszFilename     The path to the image file.
     * @param   fOverwrite      Whether to overwrite the file.
     * @param   phVfsFile       Where to return a read-writable handle to the newly
     *                          created image.
     */
    virtual HRESULT newAuxFloppyImage(const char *pszFilename, bool fOverwrite, PRTVFSFILE phVfsFile);

    /**
     * Copies files to the auxiliary floppy image.
     *
     * The base class implementation copies the main and post scripts to the root of
     * the floppy using the default script names.  Child classes may override this
     * to add additional or different files.
     *
     * @returns COM status code.
     * @param   hVfs            The floppy image VFS handle.
     */
    virtual HRESULT copyFilesToAuxFloppyImage(RTVFS hVfs);

    /**
     * Adds the given script to the root of the floppy image under the default
     * script filename.
     *
     * @returns COM status code.
     * @param   pEditor         The script to add.
     * @param   hVfs            The VFS to add it to.
     */
    HRESULT addScriptToFloppyImage(BaseTextScript *pEditor, RTVFS hVfs);

    /**
     * Copy an arbritrary file onto the floopy image.
     *
     * @returns COM status code.
     * @param   hVfs            The VFS to add it to.
     * @param   pszSrc          The source filename.
     * @param   pszDst          The destination filename (on @a hVfs).
     */
    HRESULT addFileToFloppyImage(RTVFS hVfs, const char *pszSrc, const char *pszDst);

    /**
     * Prepares (creates) the auxiliary ISO image.
     *
     * This is called by the base class prepareMedia() when isAuxiliaryIsoNeeded()
     * is true.  The base class implementation puts the edited unattended script
     * onto it.
     */
    virtual HRESULT prepareAuxIsoImage(bool fOverwrite);

    /**
     * Opens the installation ISO image.
     *
     * @returns COM status code.
     * @param   phVfsIso        Where to return the VFS handle for the ISO.
     * @param   fFlags          RTFSISO9660_F_XXX flags to pass to the
     *                          RTFsIso9660VolOpen API.
     */
    virtual HRESULT openInstallIsoImage(PRTVFS phVfsIso, uint32_t fFlags = 0);

    /**
     * Creates and configures the ISO maker instance.
     *
     * This can be overridden to set configure options.
     *
     * @returns COM status code.
     * @param   phIsoMaker      Where to return the ISO maker.
     */
    virtual HRESULT newAuxIsoImageMaker(PRTFSISOMAKER phIsoMaker);

    /**
     * Adds files to the auxiliary ISO image maker.
     *
     * The base class implementation copies just the mMainScript and mPostScript
     * files to root directory using the default filenames.
     *
     * @returns COM status code.
     * @param   hIsoMaker       The ISO maker handle.
     * @param   hVfsOrgIso      The VFS handle to the original ISO in case files
     *                          needs to be added from it.
     */
    virtual HRESULT addFilesToAuxIsoImageMaker(RTFSISOMAKER hIsoMaker, RTVFS hVfsOrgIso);

    /**
     * Adds the given script to the ISO maker.
     *
     * @returns COM status code.
     * @param   pEditor         The script to add.
     * @param   hIsoMaker       The ISO maker to add it to.
     * @param   pszDstFilename  The file name (w/ path) to add it under.  If NULL,
     *                          the default script filename is used to add it to the
     *                          root.
     */
    HRESULT addScriptToIsoMaker(BaseTextScript *pEditor, RTFSISOMAKER hIsoMaker, const char *pszDstFilename = NULL);

    /**
     * Writes the ISO image to disk.
     *
     * @returns COM status code.
     * @param   hIsoMaker       The ISO maker handle.
     * @param   pszFilename     The filename.
     * @param   fOverwrite      Whether to overwrite the destination file or not.
     */
    HRESULT finalizeAuxIsoImage(RTFSISOMAKER hIsoMaker, const char *pszFilename, bool fOverwrite);

    /**
     * Adds files to the .viso-file vectors.
     *
     * The base class implementation adds the script from mAlg, additions ISO
     * content to '/vboxadditions', validation kit ISO to '/vboxvalidationkit',
     * and user payload ISO to '/vboxuserpayload'.
     *
     * @returns COM status code.
     * @param   rVecArgs        The ISO maker argument list that will be turned into
     *                          a .viso-file.
     * @param   rVecFiles       The list of files we've created.  This is for
     *                          cleaning up at the end.
     * @param   hVfsOrgIso      The VFS handle to the original ISO in case files
     *                          needs to be added from it.
     * @param   fOverwrite      Whether to overwrite files or not.
     */
    virtual HRESULT addFilesToAuxVisoVectors(RTCList<RTCString> &rVecArgs, RTCList<RTCString> &rVecFiles,
                                             RTVFS hVfsOrgIso, bool fOverwrite);

    /**
     * Saves the given script to disk and adds it to the .viso-file vectors.
     *
     * @returns COM status code.
     * @param   pEditor         The script to add.
     * @param   rVecArgs        The ISO maker argument list that will be turned into
     *                          a .viso-file.
     * @param   rVecFiles       The list of files we've created.  This is for
     *                          cleaning up at the end.
     * @param   fOverwrite      Whether to overwrite files or not.
     */
    HRESULT addScriptToVisoVectors(BaseTextScript *pEditor, RTCList<RTCString> &rVecArgs,
                                   RTCList<RTCString> &rVecFiles, bool fOverwrite);

    /**
     * Writes out the .viso-file to disk.
     *
     * @returns COM status code.
     * @param   rVecArgs        The ISO maker argument list to write out.
     * @param   pszFilename     The filename.
     * @param   fOverwrite      Whether to overwrite the destination file or not.
     */
    HRESULT finalizeAuxVisoFile(RTCList<RTCString> const &rVecArgs, const char *pszFilename, bool fOverwrite);

    /**
     * Loads @a pszFilename from @a hVfsOrgIso into @a pEditor and parses it.
     *
     * @returns COM status code.
     * @param   hVfsOrgIso      The handle to the original installation ISO.
     * @param   pszFilename     The filename to open and load from the ISO.
     * @param   pEditor         The editor instance to load the file into and
     *                          do the parseing with.
     */
    HRESULT loadAndParseFileFromIso(RTVFS hVfsOrgIso, const char *pszFilename, AbstractScript *pEditor);
};


/**
 * Windows installer, for versions up to xp 64 / w2k3.
 */
class UnattendedWindowsSifInstaller : public UnattendedInstaller
{
public:
    DECLARE_TRANSLATE_METHODS(UnattendedWindowsSifInstaller)

    UnattendedWindowsSifInstaller(Unattended *pParent)
        : UnattendedInstaller(pParent,
                              "win_nt5_unattended.sif", "win_postinstall.cmd",
                              "WINNT.SIF",              "VBOXPOST.CMD")
    {
        Assert(isOriginalIsoNeeded()); Assert(isAuxiliaryFloppyNeeded()); Assert(isAuxiliaryIsoIsVISO()); Assert(!bootFromAuxiliaryIso());
        mStrAuxiliaryInstallDir = "A:\\";
    }
    ~UnattendedWindowsSifInstaller()        {}

    bool isAuxiliaryFloppyNeeded() const    { return true; }
    bool bootFromAuxiliaryIso() const       { return false; }

};

/**
 * Windows installer, for versions starting with Vista.
 */
class UnattendedWindowsXmlInstaller : public UnattendedInstaller
{
public:
    DECLARE_TRANSLATE_METHODS(UnattendedWindowsXmlInstaller)

    UnattendedWindowsXmlInstaller(Unattended *pParent)
        : UnattendedInstaller(pParent,
                              "win_nt6_unattended.xml", "win_postinstall.cmd",
                              "autounattend.xml",       "VBOXPOST.CMD")
    {
        Assert(isOriginalIsoNeeded()); Assert(isAuxiliaryFloppyNeeded() || isAuxiliaryIsoNeeded()); Assert(isAuxiliaryIsoIsVISO()); Assert(!bootFromAuxiliaryIso());
        if (isAuxiliaryFloppyNeeded())
            mStrAuxiliaryInstallDir = "A:\\";
        else if (bootFromAuxiliaryIso())
            mStrAuxiliaryInstallDir = "D:\\";
        else
            mStrAuxiliaryInstallDir = "E:\\";
    }
    ~UnattendedWindowsXmlInstaller()      {}

    bool isAuxiliaryFloppyNeeded() const    { return !mpParent->i_isFirmwareEFI(); }
    bool isAuxiliaryIsoNeeded() const       { return UnattendedInstaller::isAuxiliaryIsoNeeded() || mpParent->i_isFirmwareEFI(); }
    bool isAuxiliaryIsoIsVISO() const       { return true; }
    bool bootFromAuxiliaryIso() const       { return false; }
};


/**
 * OS/2 installer.
 */
class UnattendedOs2Installer : public UnattendedInstaller
{
public:
    DECLARE_TRANSLATE_METHODS(UnattendedOs2Installer)

    UnattendedOs2Installer(Unattended *pParent, Utf8Str const &rStrHints);
    ~UnattendedOs2Installer()                           {}

    /* Remaster original ISO with auxiliary floppy used for el torito floppy emulation: */
    bool isOriginalIsoNeeded()     const RT_OVERRIDE    { return false; }
    bool isAuxiliaryFloppyNeeded() const RT_OVERRIDE    { return true; }
    bool isAuxiliaryIsoNeeded()    const RT_OVERRIDE    { return true; }

protected:
    HRESULT replaceAuxFloppyImageBootSector(RTVFSFILE hVfsFile) RT_NOEXCEPT;
    HRESULT newAuxFloppyImage(const char *pszFilename, bool fOverwrite, PRTVFSFILE phVfsFile) RT_OVERRIDE;
    HRESULT copyFilesToAuxFloppyImage(RTVFS hVfs) RT_OVERRIDE;
    HRESULT addFilesToAuxVisoVectors(RTCList<RTCString> &rVecArgs, RTCList<RTCString> &rVecFiles,
                                     RTVFS hVfsOrgIso, bool fOverwrite) RT_OVERRIDE;

    HRESULT splitResponseFile() RT_NOEXCEPT;

    /**
     * Splits up the given file into sub-files and writes them out with the auxilary
     * path base as prefix.
     *
     * The source file contains @@VBOX_SPLITTER_START[filename]@@ and
     * @@VBOX_SPLITTER_END[filename]@@  markup that is used to split it up.  Any
     * text between END and START tags are ignored and can be used for comments.
     *
     * @returns COM status code (error info set).
     * @param   pszFileToSplit      The name of the file to split.
     * @param   rVecSplitFiles      Vector where names of the sub-files are appended
     *                              (without any path or prefix).
     */
    HRESULT splitFile(const char *pszFileToSplit, RTCList<RTCString> &rVecSplitFiles) RT_NOEXCEPT;

    /**
     * Splits up the given editor output into sub-files and writes them out with the
     * auxilary path base as prefix.
     *
     * The source file contains @@VBOX_SPLITTER_START[filename]@@ and
     * @@VBOX_SPLITTER_END[filename]@@  markup that is used to split it up.  Any
     * text between END and START tags are ignored and can be used for comments.
     *
     * @returns COM status code (error info set).
     * @param   pEditor             The editor which output should be split.
     * @param   rVecSplitFiles      Vector where names of the sub-files are appended
     *                              (without any path or prefix).
     */
    HRESULT splitFile(BaseTextScript *pEditor, RTCList<RTCString> &rVecSplitFiles) RT_NOEXCEPT;

    HRESULT splitFileInner(const char *pszFileToSplit, RTCList<RTCString> &rVecSplitFiles,
                           const char *pszSrc, size_t cbLeft) RT_NOEXCEPT;

    static int patchTestCfg(uint8_t *pbFile, size_t cbFile, const char *pszFilename, UnattendedOs2Installer *pThis);
    static int patchOs2Ldr(uint8_t *pbFile, size_t cbFile, const char *pszFilename, UnattendedOs2Installer *pThis);

    /** The OS2SE20.SRC path ("\\OS2IMAGES"). */
    Utf8Str mStrOs2Images;
    /** Files split out from os2_response_files.rsp (bare filenames, no paths). */
    RTCList<RTCString> mVecSplitFiles;
};



/**
 * Base class for the unattended linux installers.
 */
class UnattendedLinuxInstaller : public UnattendedInstaller
{
protected:
    /** Array of linux parameter patterns that should be removed by editIsoLinuxCfg.
     * The patterns are proceed by RTStrSimplePatternNMatch. */
    RTCList<RTCString, RTCString *> mArrStrRemoveInstallKernelParameters;

public:
    DECLARE_TRANSLATE_METHODS(UnattendedLinuxInstaller)

    UnattendedLinuxInstaller(Unattended *pParent,
                             const char *pszMainScriptTemplateName, const char *pszPostScriptTemplateName,
                             const char *pszMainScriptFilename,     const char *pszPostScriptFilename = "vboxpostinstall.sh")
        : UnattendedInstaller(pParent,
                              pszMainScriptTemplateName, pszPostScriptTemplateName,
                              pszMainScriptFilename,     pszPostScriptFilename) {}
    ~UnattendedLinuxInstaller() {}

    bool isAuxiliaryIsoNeeded() const       { return true; }

protected:
    /**
     * Performs basic edits on a isolinux.cfg file.
     *
     * @returns COM status code
     * @param   pEditor               Editor with the isolinux.cfg file loaded and parsed.
     */
    virtual HRESULT editIsoLinuxCfg(GeneralTextScript *pEditor);
    /**
     * Performs basic common edits on a isolinux.cfg and menu configuration file(s) (txt.cfg or menu.cfg etc).
     *
     * @returns COM status code
     * @param   pEditor         Editor with the isolinux.cfg file loaded and parsed.
     */
    virtual HRESULT editIsoLinuxCommon(GeneralTextScript *pEditor);

    /**
     * Performs basic edits on grub configuration file (grub.cfg).
     *
     * @returns COM status code
     * @param   pEditor                 Editor with the grub.cfg file loaded and parsed.
     */
    virtual HRESULT editGrubCfg(GeneralTextScript *pEditor);
};


/**
 * Debian installer.
 *
 * This will remaster the orignal ISO and therefore be producing a .viso-file.
 */
class UnattendedDebianInstaller : public UnattendedLinuxInstaller
{
public:
    DECLARE_TRANSLATE_METHODS(UnattendedDebianInstaller)

    UnattendedDebianInstaller(Unattended *pParent,
                              const char *pszMainScriptTemplateName = "debian_preseed.cfg",
                              const char *pszPostScriptTemplateName = "debian_postinstall.sh",
                              const char *pszMainScriptFilename     = "preseed.cfg")
        : UnattendedLinuxInstaller(pParent, pszMainScriptTemplateName, pszPostScriptTemplateName, pszMainScriptFilename)
    {
        Assert(!isOriginalIsoNeeded()); Assert(isAuxiliaryIsoNeeded());
        Assert(!isAuxiliaryFloppyNeeded()); Assert(isAuxiliaryIsoIsVISO());
        mStrDefaultExtraInstallKernelParameters.setNull();
        mStrDefaultExtraInstallKernelParameters += " auto=true";
        mStrDefaultExtraInstallKernelParameters.append(" preseed/file=/cdrom/").append(pszMainScriptFilename);
        mStrDefaultExtraInstallKernelParameters += " priority=critical";
        mStrDefaultExtraInstallKernelParameters += " quiet";
        mStrDefaultExtraInstallKernelParameters += " splash";
        mStrDefaultExtraInstallKernelParameters += " noprompt";  /* no questions about things like CD/DVD ejections */
        mStrDefaultExtraInstallKernelParameters += " noshell";   /* No shells on VT1-3 (debian, not ubuntu). */
        mStrDefaultExtraInstallKernelParameters += " automatic-ubiquity";   // ubiquity
        // the following can probably go into the preseed.cfg:
        mStrDefaultExtraInstallKernelParameters.append(" debian-installer/locale=").append(pParent->i_getLocale());
        mStrDefaultExtraInstallKernelParameters += " keyboard-configuration/layoutcode=us";
        mStrDefaultExtraInstallKernelParameters += " languagechooser/language-name=English"; /** @todo fixme */
        mStrDefaultExtraInstallKernelParameters.append(" localechooser/supported-locales=").append(pParent->i_getLocale()).append(".UTF-8");
        mStrDefaultExtraInstallKernelParameters.append(" countrychooser/shortlist=").append(pParent->i_getCountry()); // ubiquity?
        mStrDefaultExtraInstallKernelParameters += " --";
    }
    ~UnattendedDebianInstaller()            {}

    bool isOriginalIsoNeeded() const        { return false; }

protected:
    HRESULT addFilesToAuxVisoVectors(RTCList<RTCString> &rVecArgs, RTCList<RTCString> &rVecFiles,
                                     RTVFS hVfsOrgIso, bool fOverwrite);
    /**
     * Performs basic edits on menu configuration file(s) of isolinux (txt.cfg or menu.cfg etc).
     *
     * @returns COM status code
     * @param   pEditor                  Editor with the menu config. file loaded and parsed.
     */
    HRESULT editDebianMenuCfg(GeneralTextScript *pEditor);

    /**
     * Performs basic edits on a isolinux.cfg file.
     *
     * @returns COM status code
     * @param   pEditor               Editor with the isolinux.cfg file loaded and parsed.
     * @param   pszMenuConfigFileName Name of the menu config file to include in isolinux.txt. On Debians (at least)
                                      it includes the kernel command line with our preseed file and command line argument.
     */
    virtual HRESULT editIsoLinuxCfg(GeneralTextScript *pEditor, const char *pszMenuConfigFileName);

private:

    /**
     * Tries to set label name of a label line.
     *
     * @returns                 true if label line is found and label name can be set.
     * @param   pEditor         Editor with the menu configuration file loaded and parsed.
     * @param   vecLineNumbers  Indices of the label lines (within pEditor data).
     * @param   pszKeyWord      The keyword searched within the original label name.
     * @param   pszNewLabelName The new name of the label.
     */
    bool modifyLabelLine(GeneralTextScript *pEditor, const std::vector<size_t> &vecLineNumbers,
                         const char *pszKeyWord, const char *pszNewLabelName);
};


/**
 * Ubuntu preseed installer (same as Debian, except for the template).
 *
 * Only for older Ubuntu desktop versions (<= 22.04).
 */
class UnattendedUbuntuPreseedInstaller : public UnattendedDebianInstaller
{
public:
    DECLARE_TRANSLATE_METHODS(UnattendedUbuntuPreseedInstaller)

    UnattendedUbuntuPreseedInstaller(Unattended *pParent)
        : UnattendedDebianInstaller(pParent, "ubuntu_preseed.cfg")
    { Assert(!isOriginalIsoNeeded()); Assert(isAuxiliaryIsoNeeded()); Assert(!isAuxiliaryFloppyNeeded()); Assert(isAuxiliaryIsoIsVISO()); }
    ~UnattendedUbuntuPreseedInstaller() {}
};

/**
 * Ubuntu autoinstall installer.
 *
 * Newer Ubuntu desktop versions (>= 22.10) as well as newer Ubuntu server versions(>= 20.04) use a different installer ("subiquity")
 * which in turn uses the autoinstall / cloud-init installer. This is substantially different from the old preseed files,
 * as this is now YAML-based along with a different scheme.
 */
class UnattendedUbuntuAutoInstallInstaller : public UnattendedDebianInstaller
{
public:
    DECLARE_TRANSLATE_METHODS(UnattendedUbuntuAutoInstall)

    UnattendedUbuntuAutoInstallInstaller(Unattended *pParent)
        : UnattendedDebianInstaller(pParent,
                                    /* pszMainScriptTemplateName = */ "ubuntu_autoinstall_user_data",
                                    /* pszPostScriptTemplateName = */ "debian_postinstall.sh",
                                    /* pszMainScriptFilename     = */ "user-data")
    {
        Assert(!isOriginalIsoNeeded()); Assert(isAuxiliaryIsoNeeded());
        Assert(!isAuxiliaryFloppyNeeded()); Assert(isAuxiliaryIsoIsVISO());
        mStrDefaultExtraInstallKernelParameters.setNull();
        mStrDefaultExtraInstallKernelParameters += " autoinstall";
        mStrDefaultExtraInstallKernelParameters += " ds=nocloud\\;s=/cdrom/";
        mStrDefaultExtraInstallKernelParameters += " ---";
        mStrDefaultExtraInstallKernelParameters += " quiet";
        mStrDefaultExtraInstallKernelParameters += " splash";
        mStrDefaultExtraInstallKernelParameters += " noprompt";  /* no questions about things like CD/DVD ejections */
        mStrDefaultExtraInstallKernelParameters += " noshell";   /* No shells on VT1-3 (debian, not ubuntu). */
        mStrDefaultExtraInstallKernelParameters += " automatic-ubiquity";   // ubiquity
        // the following can probably go into the preseed.cfg:
        mStrDefaultExtraInstallKernelParameters.append(" debian-installer/locale=").append(pParent->i_getLocale());
        mStrDefaultExtraInstallKernelParameters += " keyboard-configuration/layoutcode=us";
        mStrDefaultExtraInstallKernelParameters += " languagechooser/language-name=English"; /** @todo fixme */
        mStrDefaultExtraInstallKernelParameters.append(" localechooser/supported-locales=").append(pParent->i_getLocale()).append(".UTF-8");
        mStrDefaultExtraInstallKernelParameters.append(" countrychooser/shortlist=").append(pParent->i_getCountry()); // ubiquity?
        mStrDefaultExtraInstallKernelParameters += " --";
    }
    ~UnattendedUbuntuAutoInstallInstaller() {}

protected:
    HRESULT addFilesToAuxVisoVectors(RTCList<RTCString> &rVecArgs, RTCList<RTCString> &rVecFiles,
                                     RTVFS hVfsOrgIso, bool fOverwrite);
};

/**
 * RHEL installer.
 *
 * This serves as a base for the kickstart based installers.
 */
class UnattendedRhelInstaller : public UnattendedLinuxInstaller
{
public:
    DECLARE_TRANSLATE_METHODS(UnattendedRhelInstaller)

    UnattendedRhelInstaller(Unattended *pParent,
                             const char *pszMainScriptTemplateName,
                             const char *pszPostScriptTemplateName,
                             const char *pszMainScriptFilename)
        : UnattendedLinuxInstaller(pParent, pszMainScriptTemplateName, pszPostScriptTemplateName, pszMainScriptFilename)
    {}
    ~UnattendedRhelInstaller() {}

    bool isAuxiliaryIsoIsVISO()             { return true; }
    bool isOriginalIsoNeeded() const        { return false; }

protected:
    HRESULT addFilesToAuxVisoVectors(RTCList<RTCString> &rVecArgs, RTCList<RTCString> &rVecFiles,
                                     RTVFS hVfsOrgIso, bool fOverwrite);
};

/**
 * RHEL 6 installer.
 *
 */
class UnattendedRhel6Installer : public UnattendedRhelInstaller
{
public:
    DECLARE_TRANSLATE_METHODS(UnattendedRhel6Installer)

    UnattendedRhel6Installer(Unattended *pParent,
                             const char *pszMainScriptTemplateName = "redhat67_ks.cfg",
                             const char *pszPostScriptTemplateName = "redhat_postinstall.sh",
                             const char *pszMainScriptFilename     = "ks.cfg")
        : UnattendedRhelInstaller(pParent, pszMainScriptTemplateName, pszPostScriptTemplateName, pszMainScriptFilename)
    {
        Assert(!isOriginalIsoNeeded()); Assert(isAuxiliaryIsoNeeded());
        Assert(!isAuxiliaryFloppyNeeded()); Assert(isAuxiliaryIsoIsVISO());
        mStrDefaultExtraInstallKernelParameters.assign(" ks=cdrom:/").append(pszMainScriptFilename).append(' ');
        mArrStrRemoveInstallKernelParameters.append("rd.live.check"); /* Disables the checkisomd5 step. Required for VISO. */
    }
    ~UnattendedRhel6Installer() {}
};

/**
 * RHEL 7 installer (same as RHEL 6).
 * The class was added for better handling any possible subtle difference between RHEL6 and RHEL7.
 */
class UnattendedRhel7Installer : public UnattendedRhel6Installer
{
public:
    DECLARE_TRANSLATE_METHODS(UnattendedRhel7Installer)

    UnattendedRhel7Installer(Unattended *pParent)
        : UnattendedRhel6Installer(pParent)
    { Assert(!isOriginalIsoNeeded()); Assert(isAuxiliaryIsoNeeded()); Assert(!isAuxiliaryFloppyNeeded()); Assert(isAuxiliaryIsoIsVISO()); }

    UnattendedRhel7Installer(Unattended *pParent,
                             const char *pszMainScriptTemplateName,
                             const char *pszPostScriptTemplateName,
                             const char *pszMainScriptFilename)
        : UnattendedRhel6Installer(pParent, pszMainScriptTemplateName, pszPostScriptTemplateName, pszMainScriptFilename)
    { Assert(!isOriginalIsoNeeded()); Assert(isAuxiliaryIsoNeeded()); Assert(!isAuxiliaryFloppyNeeded()); Assert(isAuxiliaryIsoIsVISO()); }
    ~UnattendedRhel7Installer() {}
};


/**
 * RHEL 8 installer (same as RHEL 7).
 * The class was added for better handling any possible subtle difference between RHEL7 and RHEL8.
 */
class UnattendedRhel8Installer : public UnattendedRhel7Installer
{
public:
    DECLARE_TRANSLATE_METHODS(UnattendedRhel8Installer)

    UnattendedRhel8Installer(Unattended *pParent)
        : UnattendedRhel7Installer(pParent)
    { Assert(!isOriginalIsoNeeded()); Assert(isAuxiliaryIsoNeeded()); Assert(!isAuxiliaryFloppyNeeded()); Assert(isAuxiliaryIsoIsVISO()); }

    UnattendedRhel8Installer(Unattended *pParent,
                             const char *pszMainScriptTemplateName,
                             const char *pszPostScriptTemplateName,
                             const char *pszMainScriptFilename)
        : UnattendedRhel7Installer(pParent, pszMainScriptTemplateName, pszPostScriptTemplateName, pszMainScriptFilename)
    { Assert(!isOriginalIsoNeeded()); Assert(isAuxiliaryIsoNeeded()); Assert(!isAuxiliaryFloppyNeeded()); Assert(isAuxiliaryIsoIsVISO()); }
    ~UnattendedRhel8Installer() {}
};


/**
 * RHEL 5 installer (same as RHEL 6, except for the kickstart template).
 */
class UnattendedRhel5Installer : public UnattendedRhel6Installer
{
public:
    DECLARE_TRANSLATE_METHODS(UnattendedRhel5Installer)

    UnattendedRhel5Installer(Unattended *pParent) : UnattendedRhel6Installer(pParent, "rhel5_ks.cfg") {}
    ~UnattendedRhel5Installer() {}
};


/**
 * RHEL 4 installer (same as RHEL 6, except for the kickstart template).
 */
class UnattendedRhel4Installer : public UnattendedRhel6Installer
{
public:
    DECLARE_TRANSLATE_METHODS(UnattendedRhel4Installer)

    UnattendedRhel4Installer(Unattended *pParent) : UnattendedRhel6Installer(pParent, "rhel4_ks.cfg") {}
    ~UnattendedRhel4Installer() {}
};


/**
 * RHEL 3 installer (same as RHEL 6, except for the kickstart template).
 */
class UnattendedRhel3Installer : public UnattendedRhel6Installer
{
public:
    DECLARE_TRANSLATE_METHODS(UnattendedRhel3Installer)

    UnattendedRhel3Installer(Unattended *pParent) : UnattendedRhel6Installer(pParent, "rhel3_ks.cfg") {}
    ~UnattendedRhel3Installer() {}
};


/**
 * Fedora installer (same as RHEL 6, except for the template).
 */
class UnattendedFedoraInstaller : public UnattendedRhel6Installer
{
public:
    DECLARE_TRANSLATE_METHODS(UnattendedFedoraInstaller)

    UnattendedFedoraInstaller(Unattended *pParent)
        : UnattendedRhel6Installer(pParent, "fedora_ks.cfg")
    { Assert(!isOriginalIsoNeeded()); Assert(isAuxiliaryIsoNeeded()); Assert(!isAuxiliaryFloppyNeeded()); Assert(isAuxiliaryIsoIsVISO()); }
    ~UnattendedFedoraInstaller() {}
};


/**
 * Oracle Linux 6 installer. Same as RHEL 6, except for the templates.
 * The reason of adding new class is to sepatate the RHEL from OL.
 */
class UnattendedOracleLinux6Installer : public UnattendedRhel6Installer
{
public:
    DECLARE_TRANSLATE_METHODS(UnattendedOracleLinux6Installer)

    UnattendedOracleLinux6Installer(Unattended *pParent,
                                    const char *pszMainScriptTemplateName = "ol_ks.cfg",
                                    const char *pszPostScriptTemplateName = "ol_postinstall.sh",
                                    const char *pszMainScriptFilename = "ks.cfg")
        : UnattendedRhel6Installer(pParent, pszMainScriptTemplateName, pszPostScriptTemplateName, pszMainScriptFilename)
    { Assert(!isOriginalIsoNeeded()); Assert(isAuxiliaryIsoNeeded()); Assert(!isAuxiliaryFloppyNeeded()); Assert(isAuxiliaryIsoIsVISO()); }
    ~UnattendedOracleLinux6Installer() {}
};


/**
 * Oracle Linux 7 installer. Same as OL 6.
 * The class was added for better handling any possible subtle difference between OL6 and OL7.
 */
class UnattendedOracleLinux7Installer : public UnattendedOracleLinux6Installer
{
public:
    DECLARE_TRANSLATE_METHODS(UnattendedOracleLinux7Installer)

    UnattendedOracleLinux7Installer(Unattended *pParent)
        : UnattendedOracleLinux6Installer(pParent)
    { Assert(!isOriginalIsoNeeded()); Assert(isAuxiliaryIsoNeeded()); Assert(!isAuxiliaryFloppyNeeded()); Assert(isAuxiliaryIsoIsVISO()); }

    UnattendedOracleLinux7Installer(Unattended *pParent,
                                    const char *pszMainScriptTemplateName,
                                    const char *pszPostScriptTemplateName,
                                    const char *pszMainScriptFilename)
        : UnattendedOracleLinux6Installer(pParent, pszMainScriptTemplateName, pszPostScriptTemplateName, pszMainScriptFilename)
    { Assert(!isOriginalIsoNeeded()); Assert(isAuxiliaryIsoNeeded()); Assert(!isAuxiliaryFloppyNeeded()); Assert(isAuxiliaryIsoIsVISO()); }
    ~UnattendedOracleLinux7Installer() {}
};


/**
 * Oracle Linux 8 installer. Using a different kickstart file than earlier OL versions.
 */
class UnattendedOracleLinux8Installer : public UnattendedOracleLinux7Installer
{
public:
    DECLARE_TRANSLATE_METHODS(UnattendedOracleLinux8Installer)

    UnattendedOracleLinux8Installer(Unattended *pParent,
                                    const char *pszMainScriptTemplateName = "ol8_ks.cfg",
                                    const char *pszPostScriptTemplateName = "ol_postinstall.sh",
                                    const char *pszMainScriptFilename = "ks.cfg")

        : UnattendedOracleLinux7Installer(pParent, pszMainScriptTemplateName, pszPostScriptTemplateName, pszMainScriptFilename)
    { Assert(!isOriginalIsoNeeded()); Assert(isAuxiliaryIsoNeeded()); Assert(!isAuxiliaryFloppyNeeded()); Assert(isAuxiliaryIsoIsVISO()); }
    ~UnattendedOracleLinux8Installer() {}
};

/**
 * Oracle Linux 9 installer.
 * Uses a different kickstart file since several commands/options are removed in OL9.
 * See the ol9_ks.cfg file for comments. Also, as of OL9 kernel command argument 'ks' should have
 * 'inst.' prefix.
 */
class UnattendedOracleLinux9Installer : public UnattendedRhelInstaller
{
public:
    DECLARE_TRANSLATE_METHODS(UnattendedOracleLinux9Installer)

    UnattendedOracleLinux9Installer(Unattended *pParent,
                                    const char *pszMainScriptTemplateName = "ol9_ks.cfg",
                                    const char *pszPostScriptTemplateName = "ol_postinstall.sh",
                                    const char *pszMainScriptFilename = "ks.cfg")
        : UnattendedRhelInstaller(pParent, pszMainScriptTemplateName, pszPostScriptTemplateName, pszMainScriptFilename)
    {
        Assert(!isOriginalIsoNeeded());
        Assert(isAuxiliaryIsoNeeded());
        Assert(!isAuxiliaryFloppyNeeded());
        Assert(isAuxiliaryIsoIsVISO());
        mStrDefaultExtraInstallKernelParameters.assign(" inst.ks=cdrom:/").append(pszMainScriptFilename).append(' ');
        mArrStrRemoveInstallKernelParameters.append("rd.live.check"); /* Disables the checkisomd5 step. Required for VISO. */
    }
    ~UnattendedOracleLinux9Installer() {}
};


#if 0 /* fixme */
/**
 * SUSE linux installer.
 *
 * @todo needs fixing.
 */
class UnattendedSuseInstaller : public UnattendedLinuxInstaller
{
public:
    DECLARE_TRANSLATE_METHODS(UnattendedSuseInstaller)

    UnattendedSuseInstaller(BaseTextScript *pAlg, Unattended *pParent)
        : UnattendedLinuxInstaller(pAlg, pParent, "suse_autoinstall.xml")
    { Assert(isOriginalIsoNeeded()); Assert(isAuxiliaryIsoNeeded()); Assert(!isAuxiliaryFloppyNeeded()); Assert(!isAuxiliaryIsoIsVISO()); }
    ~UnattendedSuseInstaller() {}

    HRESULT setupScriptOnAuxiliaryCD(const Utf8Str &path);
};
#endif

/**
 * Base class for the unattended FreeBSD installers.
 */
class UnattendedFreeBsdInstaller : public UnattendedInstaller
{
public:
    UnattendedFreeBsdInstaller(Unattended *pParent)
        : UnattendedInstaller(pParent,
                              "freebsd_installer.cfg", "freebsd_postinstall.sh",
                              "installerconfig", "vboxpostinstall.sh") {}
    ~UnattendedFreeBsdInstaller() {}

    bool isAuxiliaryIsoNeeded() const       { return true;  }
    bool isOriginalIsoNeeded() const        { return false; }

protected:
    HRESULT addFilesToAuxVisoVectors(RTCList<RTCString> &rVecArgs, RTCList<RTCString> &rVecFiles,
                                     RTVFS hVfsOrgIso, bool fOverwrite);
};

#endif /* !MAIN_INCLUDED_UnattendedInstaller_h */
