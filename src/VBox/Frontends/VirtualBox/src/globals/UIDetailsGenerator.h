/* $Id: UIDetailsGenerator.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Qt GUI - UIDetailsGenerator declaration.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

#ifndef FEQT_INCLUDED_SRC_globals_UIDetailsGenerator_h
#define FEQT_INCLUDED_SRC_globals_UIDetailsGenerator_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* GUI includes: */
#include "UIExtraDataDefs.h"
#include "UITextTable.h"

/* COM includes: */
#include "KParavirtProvider.h"
#include "KVMExecutionEngine.h"

/* Forward declarations: */
class CCloudMachine;
class CConsole;
class CFormValue;
class CGuest;
class CMachine;
class CNetworkAdapter;

/** Details generation namespace. */
namespace UIDetailsGenerator
{
    SHARED_LIBRARY_STUFF UITextTable generateMachineInformationGeneral(CMachine &comMachine,
                                                                       const UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral &fOptions);

    SHARED_LIBRARY_STUFF UITextTable generateMachineInformationGeneral(CCloudMachine &comCloudMachine,
                                                                       const UIExtraDataMetaDefs::DetailsElementOptionTypeGeneral &fOptions);
    SHARED_LIBRARY_STUFF QString generateFormValueInformation(const CFormValue &comFormValue, bool fFull = false);

    SHARED_LIBRARY_STUFF UITextTable generateMachineInformationSystem(CMachine &comMachine,
                                                                      const UIExtraDataMetaDefs::DetailsElementOptionTypeSystem &fOptions);

    SHARED_LIBRARY_STUFF UITextTable generateMachineInformationDisplay(CMachine &comMachine,
                                                                       const UIExtraDataMetaDefs::DetailsElementOptionTypeDisplay &fOptions);

    SHARED_LIBRARY_STUFF UITextTable generateMachineInformationStorage(CMachine &comMachine,
                                                                       const UIExtraDataMetaDefs::DetailsElementOptionTypeStorage &fOptions,
                                                                       bool fLink = true);

    SHARED_LIBRARY_STUFF UITextTable generateMachineInformationAudio(CMachine &comMachine,
                                                                     const UIExtraDataMetaDefs::DetailsElementOptionTypeAudio &fOptions);

    SHARED_LIBRARY_STUFF UITextTable generateMachineInformationNetwork(CMachine &comMachine,
                                                                       const UIExtraDataMetaDefs::DetailsElementOptionTypeNetwork &fOptions);

    SHARED_LIBRARY_STUFF UITextTable generateMachineInformationSerial(CMachine &comMachine,
                                                                      const UIExtraDataMetaDefs::DetailsElementOptionTypeSerial &fOptions);

    SHARED_LIBRARY_STUFF UITextTable generateMachineInformationUSB(CMachine &comMachine,
                                                                   const UIExtraDataMetaDefs::DetailsElementOptionTypeUsb &fOptions);

    SHARED_LIBRARY_STUFF UITextTable generateMachineInformationSharedFolders(CMachine &comMachine,
                                                                             const UIExtraDataMetaDefs::DetailsElementOptionTypeSharedFolders &fOptions);

    SHARED_LIBRARY_STUFF UITextTable generateMachineInformationUI(CMachine &comMachine,
                                                                  const UIExtraDataMetaDefs::DetailsElementOptionTypeUserInterface &fOptions);

    SHARED_LIBRARY_STUFF UITextTable generateMachineInformationDescription(CMachine &comMachine,
                                                                           const UIExtraDataMetaDefs::DetailsElementOptionTypeDescription &fOptions);

    SHARED_LIBRARY_STUFF void acquireHardDiskStatusInfo(CMachine &comMachine, QString &strInfo,
                                                        uint &cAttachmentsCount);

    SHARED_LIBRARY_STUFF void acquireOpticalDiskStatusInfo(CMachine &comMachine, QString &strInfo,
                                                           uint &cAttachmentsCount, uint &cAttachmentsMountedCount);

    SHARED_LIBRARY_STUFF void acquireFloppyDiskStatusInfo(CMachine &comMachine, QString &strInfo,
                                                          uint &cAttachmentsCount, uint &cAttachmentsMountedCount);

    SHARED_LIBRARY_STUFF void acquireAudioStatusInfo(CMachine &comMachine, QString &strInfo,
                                                     bool &fAudioEnabled, bool &fEnabledOutput, bool &fEnabledInput);

    SHARED_LIBRARY_STUFF void acquireNetworkStatusInfo(CMachine &comMachine, QString &strInfo,
                                                       bool &fAdaptersPresent, bool &fCablesDisconnected);

    SHARED_LIBRARY_STUFF void acquireUsbStatusInfo(CMachine &comMachine, CConsole &comConsole,
                                                   QString &strInfo, bool &fUsbEnabled, uint &cUsbFilterCount);

    SHARED_LIBRARY_STUFF void acquireSharedFoldersStatusInfo(CMachine &comMachine, CConsole &comConsole, CGuest &comGuest,
                                                             QString &strInfo, uint &cFoldersCount);

    SHARED_LIBRARY_STUFF void acquireDisplayStatusInfo(CMachine &comMachine, QString &strInfo,
                                                       uint &uVRAMSize, uint &cMonitorCount, bool &fAcceleration3D);

    SHARED_LIBRARY_STUFF void acquireRecordingStatusInfo(CMachine &comMachine, QString &strInfo,
                                                         bool &fRecordingEnabled);

    SHARED_LIBRARY_STUFF void acquireFeaturesStatusInfo(CMachine &comMachine, QString &strInfo,
                                                        KVMExecutionEngine &enmEngine,
                                                        bool fNestedPagingEnabled, bool fUxEnabled,
                                                        KParavirtProvider enmProvider);

    SHARED_LIBRARY_STUFF QString summarizeGenericProperties(const CNetworkAdapter &comAdapter);

    /** Holds the table row format 1. */
    extern const QString e_strTableRow1;
    /** Holds the table row format 2. */
    extern const QString e_strTableRow2;
    /** Holds the table row format 3. */
    extern const QString e_strTableRow3;
}

#endif /* !FEQT_INCLUDED_SRC_globals_UIDetailsGenerator_h */
