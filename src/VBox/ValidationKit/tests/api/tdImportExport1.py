#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: tdImportExport1.py 110220 2025-07-14 23:25:19Z brent.paulson@oracle.com $

"""
VirtualBox Validation Kit - Import and Export EFI-base VM Test #1
"""

__copyright__ = \
"""
Copyright (C) 2025 Oracle and/or its affiliates.

This file is part of VirtualBox base platform packages, as
available from https://www.virtualbox.org.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation, in version 3 of the
License.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <https://www.gnu.org/licenses>.

The contents of this file may alternatively be used under the terms
of the Common Development and Distribution License Version 1.0
(CDDL), a copy of it is provided in the "COPYING.CDDL" file included
in the VirtualBox distribution, in which case the provisions of the
CDDL are applicable instead of those of the GPL.

You may elect to license modified versions of this file under the
terms and conditions of either the GPL or the CDDL or both.

SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
"""
__version__ = "$Revision: 110220 $"


# Standard Python imports.
import os
import sys
import shutil

# Only the main script needs to modify the path.
try:    __file__                            # pylint: disable=used-before-assignment
except: __file__ = sys.argv[0]
g_ksValidationKitDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.append(g_ksValidationKitDir)

# Validation Kit imports.
from testdriver import base
from testdriver import reporter
from testdriver import vboxcon
from testdriver import vboxwrappers
from common import utils


class SubTstDrvImportExportEFIVM1(base.SubTestDriverBase):
    """
    Sub-test driver for importing and exporting an EFI-based VM.
    """
    def __init__(self, oTstDrv):
        base.SubTestDriverBase.__init__(self, oTstDrv, 'tst-ubuntu-wily-werewolf-EFI', 'Import and Export an EFI-based VM');
        self.sVmName = 'tst-ubuntu-wily-werewolf-EFI';
        self.asRsrcs = ['6.1/efi/ubuntu-15_10-efi-amd64.vdi'];

    #
    # Overridden methods specified in the TestDriverBase class (testdriver/base.py).
    #

    # Handle the action to execute the test itself.
    def testIt(self):
        """
        Create a test VM using the Ubuntu 15.10 VDI and configure it to use EFI.
        We also copy the associated NVRAM file from the test resources share to
        the VM's machine folder.  This then sets things up for testing the export
        of the VM into an OVF appliance which we then import into a new VM.  In
        both the export and import scenarios we verify that the VM's NVRAM is
        included in the OVA file on export and is placed in the VM's machine
        folder on import.
        """
        reporter.log('Creating test VM with EFI firmware: \'%s\'' % self.sVmName);
        oVM = self.oTstDrv.createTestVM(self.sVmName, 1, sHd = '6.1/efi/ubuntu-15_10-efi-amd64.vdi',
                                        sKind = 'Ubuntu15_64', fIoApic = True, sFirmwareType = 'efi',
                                        sDvdImage = self.oTstDrv.sVBoxValidationKitIso);
        if oVM is None:
            reporter.error('Error creating test VM: \'%s\'' % self.sVmName);

        sNvramSrcPath = self.oTstDrv.getFullResourceName('6.1/efi/ubuntu-15_10-efi-amd64.nvram');
        sNvramTargetPath = os.path.join(self.oTstDrv.oVBox.systemProperties.defaultMachineFolder, self.sVmName,
                                        self.sVmName + '.nvram');
        reporter.log('Copying NVRAM file from \'%s\' to \'%s\'' % (sNvramSrcPath, sNvramTargetPath));
        shutil.copy(sNvramSrcPath, sNvramTargetPath);

        sOvaPath = os.path.join(self.oTstDrv.oVBox.systemProperties.defaultMachineFolder, self.sVmName  + '.ova');

        # Execute the sub-testcases.
        return  self.testExportVmWithEfiFirmware(oVM, sOvaPath) \
            and self.testImportVmWithEfiFirmware(sOvaPath);

    #
    # Test execution helpers.
    #
    def testExportVmWithEfiFirmware(self, oVM, sOvaPath):
        """
        Export the Ubuntu 15.10 EFI-based VM into an OVF appliance and verify its
        NVRAM file is present in the resultant OVA file.
        """
        reporter.testStart('testExportVmWithEfiFirmware');
        reporter.log('Export an EFI-based VM and verify its NVRAM included in the OVA file.');

        # Step #1 to export a VM into an OVF appliance is calling
        # IVirtualBox::createAppliance() to create an empty IAppliance object.
        try:
            oVirtualBox = self.oTstDrv.oVBoxMgr.getVirtualBox();
        except:
            return reporter.errorXcpt('Failed to get VirtualBox object');

        reporter.log('Export step #1: IVirtualBox::createAppliance()');
        try:
            oAppliance = oVirtualBox.createAppliance();
        except:
            return reporter.errorXcpt('Export step #1: IVirtualBox::createAppliance() failed');

        # Step #2 to export a VM into an OVF appliance is calling
        # IMachine::exportTo(IAppliance appliance, wstring location) using the
        # IAppliance object we just created and the location of where to create the
        # appliance.
        reporter.log('Export step #2: IMachine::exportTo(oAppliance, location=\'%s\')' % sOvaPath);
        try:
            _ = oVM.exportTo(oAppliance, sOvaPath); # returned IVirtualSystemDescription object not used below
        except:
            return reporter.errorXcpt('Export step #2: IMachine::exportTo() failed');

        # Step #3 to export a VM into an OVF appliance is calling
        # IAppliance::write(wstring format, ExportOptions options[], wstring path) and then
        # waiting on the IProgress object returned.
        reporter.log('Export step #3: IAppliance:write(): exporting VM \'%s\' to \'%s\'' % (self.sVmName, sOvaPath));
        oProgress = vboxwrappers.ProgressWrapper(oAppliance.write("ovf-2.0", [], sOvaPath),
                                                 self.oTstDrv.oVBoxMgr, self.oTstDrv,
                                                 'exporting VM \'%s\' to \'%s\'' % (self.sVmName, sOvaPath));

        oProgress.wait(cMsTimeout = 15 * 60 * 1000); # 15 minutes
        if oProgress.logResult() is False:
            return reporter.error('Export step #3: IAppliance:write() failed');

        # The VM has been successfully exported to an OVA file so now we examine the
        # OVA file to verify that the VM's NVRAM file was included.
        sTarCmd = self.oTstDrv.getBinTool('vts_tar');
        asTarCmdArgs = [ sTarCmd, '-tvf', sOvaPath, self.sVmName + '.nvram' ];

        reporter.log('Verifying NVRAM file is in OVA file by executing: %s' % asTarCmdArgs);
        # processOutputChecked() raises an exception in case of failure
        try:
            oProcessOutput = utils.processOutputChecked(asTarCmdArgs);
        except:
            return reporter.errorXcpt('Failed to find NVRAM file in exported OVA file.');

        reporter.log2('NVRAM file present in OVA file: "tar -tvf ova" returned: %s' % oProcessOutput);

        reporter.testDone();
        return True;

    def testImportVmWithEfiFirmware(self, sOvaPath):
        """
        Import the appliance containing the Ubuntu 15.10 EFI-based VM and then
        verify the NVRAM file in the OVA file is properly extracted into the
        imported VM's machine folder.
        """
        reporter.testStart('testImportVmWithEfiFirmware');
        reporter.log('Import an EFI-based VM and verify its NVRAM file is included in the imported VM.');

        # Step #1 to import an OVF appliance into VirtualBox is calling
        # IVirtualBox::createAppliance() to create an empty IAppliance object.
        try:
            oVirtualBox = self.oTstDrv.oVBoxMgr.getVirtualBox();
        except:
            return reporter.errorXcpt('Failed to get VirtualBox object');

        reporter.log('Import step #1: IVirtualBox::createAppliance()');
        try:
            oAppliance = oVirtualBox.createAppliance();
        except:
            return reporter.errorXcpt('Import step #1: IVirtualBox::createAppliance() failed');

        # Step #2 to import an OVF appliance into VirtualBox is calling
        # IAppliance::read(wstring appliance-name) using the IAppliance object we
        # just created and the location of the .ovf or .ova file.
        reporter.log('Import step #2: IAppliance::read(appliance-file=\'%s\')' % sOvaPath);
        try:
            oProgress = vboxwrappers.ProgressWrapper(oAppliance.read(sOvaPath),
                                                     self.oTstDrv.oVBoxMgr, self.oTstDrv,
                                                     'reading appliance \'%s\'' % sOvaPath);
        except:
            return reporter.errorXcpt('Import step #2: IAppliance::read() failed');

        oProgress.wait(cMsTimeout = 15 * 60 * 1000); # 15 minutes
        if oProgress.logResult() is False:
            return reporter.error('Import step #2: IAppliance::read() failed');

        # Step #3 to import an OVF appliance is calling IAppliance::interpret()
        reporter.log('Import step #3: IAppliance:interpret()');
        try:
            oAppliance.interpret();
        except:
            return reporter.errorXcpt('IAppliance::interpret() failed to interpret OVF data in OVA file "%s"' % sOvaPath);

        # Step #4 (technically optional) to import an OVF appliance is calling
        # IVirtualSystemDescription::setFinalValues() to change any of the
        # configuration details for the various Virtual System Descriptions.
        # Here we change the imported VM's name to one of our choosing.
        #
        # Several steps are involved in preparation for calling
        # IVirtualSystemDescription::setFinalValues().  First retrieve the
        # IAppliance::virtualSystemDescriptions[] attribute which contains an
        # array of IVirtualSystemDescription entries for each VM in the appliance.
        reporter.log('Import step #4: IVirtualSystemDescription::setFinalValues()');
        aoVSDArray = self.oTstDrv.oVBoxMgr.getArray(oAppliance, 'virtualSystemDescriptions')
        if aoVSDArray is None:
            return reporter.error('Import step #4: getvirtualSystemDescriptions() failed');
        oVSD = aoVSDArray[0];

        # Next we call IVirtualSystemDescription::getDescription() to retrieve the
        # configuration details of the VM being imported based on the
        # VirtualSystemDescriptionType enumeration type in the aTypes[] array such as
        # VirtualSystemDescriptionType_Name or VirtualSystemDescriptionType_OS.  Each
        # aTypes[] value has the corresponding original value (aOVFValues[]) from the
        # OVF file and the suggsted value in the aVBoxValues[] array.
        try:
            (aTypes, _, _, aVBoxValues, aExtraConfigValues) = oVSD.getDescription(); # aRefs[] and aOvfValues[] not needed
        except:
            return reporter.errorXcpt('Import step #4: IVirtualSystemDescription::getDescription() failed');

        # The IVirtualSystemDescription::setFinalValues() routine takes three array
        # arguments: the first is a boolean array (aEnabled[]) to indicate whether
        # the particular configuration item should be enabled.  The second is the
        # updated configuration values as returned in aVBoxValues[] above by
        # getDescription() with any changes to suggested values included.  The
        # third array is the extra configuration values (aExtraConfigValues[])
        # which only apply to certain item types.
        sNewVmName = self.sVmName + '-imported';
        aEnabled = [];
        for (i, aType) in enumerate(aTypes):
            aEnabled.append(True);
            if aType == vboxcon.VirtualSystemDescriptionType_Name:
                aVBoxValues[i] = sNewVmName;

        try:
            oVSD.setFinalValues(aEnabled, aVBoxValues, aExtraConfigValues);
        except:
            return reporter.errorXcpt('Import step #4: IVirtualSystemDescription::setFinalValues() failed');

        # Step #5 to import an OVF appliance is calling IAppliance::importMachines()
        reporter.log('Import step #5: IAppliance::importMachines()');
        try:
            oProgress = vboxwrappers.ProgressWrapper(oAppliance.importMachines([]),
                                                     self.oTstDrv.oVBoxMgr, self.oTstDrv,
                                                     'importing appliance \'%s\'' % sOvaPath);
        except:
            return reporter.errorXcpt('Import step #5: IAppliance::importMachines() failed');

        oProgress.wait(cMsTimeout = 15 * 60 * 1000); # 15 minutes
        if oProgress.logResult() is False:
            return reporter.error('Import step #5: IAppliance::importMachines() failed');

        # The appliance has been successfully imported so now verify the VM's
        # NVRAM file was extracted from the OVA file and placed in the VM's
        # machine folder.
        sImportedVmNvramPath = os.path.join(self.oTstDrv.oVBox.systemProperties.defaultMachineFolder, sNewVmName,
                                            sNewVmName + '.nvram');
        reporter.log('Checking for NVRAM file at: %s' % sImportedVmNvramPath);
        if not os.path.exists(sImportedVmNvramPath):
            return reporter.error('NVRAM file \'%s\' not present in the imported VM' % sImportedVmNvramPath);

        reporter.testDone();
        return True;

if __name__ == '__main__':
    sys.path.append(os.path.dirname(os.path.abspath(__file__)));
    from tdApi1 import tdApi1;      # pylint: disable=relative-import
    sys.exit(tdApi1([SubTstDrvImportExportEFIVM1]).main(sys.argv))
