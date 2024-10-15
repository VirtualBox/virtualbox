#!/usr/bin/env python
# -*- coding: utf-8 -*-
# $Id: tdNetBenchmark1.py 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $

"""
VirtualBox Validation Kit - Networking benchmark #1.
"""

__copyright__ = \
"""
Copyright (C) 2010-2024 Oracle and/or its affiliates.

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
__version__ = "$Revision: 106320 $"


# Standard Python imports.
import os;
import socket
import sys;

# Only the main script needs to modify the path.
try:    __file__                            # pylint: disable=used-before-assignment
except: __file__ = sys.argv[0];
g_ksValidationKitDir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))));
sys.path.append(g_ksValidationKitDir);

# Validation Kit imports.
from testdriver import reporter;
from testdriver import base;
from testdriver import vbox;
from testdriver import vboxcon;


class tdNetBenchmark1(vbox.TestDriver):                                         # pylint: disable=too-many-instance-attributes
    """
    Networking benchmark #1.
    """

    def __init__(self):
        vbox.TestDriver.__init__(self);
        self.asRsrcs            = None;
        self.sLocalName         = socket.getfqdn();
        self.sLocalIP           = None
        self.sRemoteName        = None;
        self.sRemoteIP          = None;
        self.sGuestName         = None;
        self.sGuestIP           = None;
        self.oGuestToGuestVM    = None;
        self.oGuestToGuestSess  = None;
        self.oGuestToGuestTxs   = None;
        self.asTestVMsDef       = ['tst-rhel5', 'tst-win2k3ent', 'tst-sol10'];
        self.asTestVMs          = self.asTestVMsDef;
        self.asSkipVMs          = [];
        self.asVirtModesDef     = ['hwvirt', 'hwvirt-np', 'raw',]
        self.asVirtModes        = self.asVirtModesDef
        self.acCpusDef          = [1, 2,]
        self.acCpus             = self.acCpusDef;
        self.asNicTypesDef      = ['E1000', 'PCNet', 'Virtio',];
        self.asNicTypes         = self.asNicTypesDef;
        self.sNicAttachmentDef  = 'bridged';
        self.sNicAttachment     = self.sNicAttachmentDef;
        self.asSetupsDef        = ['g2h', 'g2r', 'g2g',];
        self.asSetups           = self.asSetupsDef;
        self.cSecsRunDef        = 30;
        self.cSecsRun           = self.cSecsRunDef;
        self.asTestsDef         = ['tcp-latency', 'tcp-throughput', 'udp-latency', 'udp-throughput', 'tbench'];
        self.asTests            = self.asTestsDef
        self.acbLatencyPktsDef  = [32, 1024, 4096, 8192, 65536,];
        self.acbLatencyPkts     = self.acbLatencyPktsDef
        self.acbThroughputPktsDef = [8192, 65536];
        self.acbThroughputPkts  = self.acbThroughputPktsDef

        try: self.sLocalName = socket.gethostbyname(self.sLocalName);
        except: pass;

    #
    # Overridden methods.
    #
    def showUsage(self):
        rc = vbox.TestDriver.showUsage(self);
        reporter.log('');
        reporter.log('tdNetBenchmark1 Options:');
        reporter.log('  --remote-host  <hostname|address>');
        reporter.log('  --local-host   <hostname|address>');
        reporter.log('  --guest-host   <hostname|address>');
        reporter.log('  --virt-modes   <m1[:m2[:]]');
        reporter.log('      Default: %s' % (':'.join(self.asVirtModesDef)));
        reporter.log('  --cpu-counts   <c1[:c2[:]]');
        reporter.log('      Default: %s' % (':'.join(str(c) for c in self.acCpusDef)));
        reporter.log('  --nic-types    <type1[:type2[:...]]>');
        reporter.log('      Default: %s' % (':'.join(self.asNicTypes)));
        reporter.log('  --nic-attachment <bridged|nat>');
        reporter.log('      Default: %s' % (self.sNicAttachmentDef));
        reporter.log('  --setups       <s1[:s2[:]]>');
        reporter.log('      Default: %s  (all)' % (':'.join(self.asSetupsDef)));
        reporter.log('  --secs-per-run <seconds>');
        reporter.log('      Default: %s' % (self.cSecsRunDef));
        reporter.log('  --tests        <s1[:s2[:]]>');
        reporter.log('      Default: %s  (all)' % (':'.join(self.asTestsDef)));
        reporter.log('  --latency-sizes <size1[:size2[:...]]>');
        reporter.log('      Default: %s' % (':'.join(str(cb) for cb in self.acbLatencyPktsDef))); # pychecker bug?
        reporter.log('  --throughput-sizes <size1[:size2[:...]]>');
        reporter.log('      Default: %s' % (':'.join(str(cb) for cb in self.acbThroughputPktsDef))); # pychecker bug?
        reporter.log('  --test-vms     <vm1[:vm2[:...]]>');
        reporter.log('      Test the specified VMs in the given order. Use this to change');
        reporter.log('      the execution order or limit the choice of VMs');
        reporter.log('      Default: %s  (all)' % (':'.join(self.asTestVMsDef)));
        reporter.log('  --skip-vms     <vm1[:vm2[:...]]>');
        reporter.log('      Skip the specified VMs when testing.');
        reporter.log('  --quick');
        reporter.log('      Shorthand for: --virt-modes hwvirt --cpu-counts 1 --secs-per-run 5 --latency-sizes 32');
        reporter.log('                     --throughput-sizes 8192 --test-vms tst-rhel5:tst-win2k3ent:tst-sol10');
        return rc;

    def parseOption(self, asArgs, iArg):                                        # pylint: disable=too-many-branches,too-many-statements
        if asArgs[iArg] == '--remote-host':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--remote-host" takes an IP address or a hostname');
            self.sRemoteName = asArgs[iArg];
        elif asArgs[iArg] == '--local-host':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--local-host" takes an IP address or a hostname');
            self.sLocalName = asArgs[iArg];
        elif asArgs[iArg] == '--guest-host':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--guest-host" takes an IP address or a hostname');
            self.sGuestName = asArgs[iArg];
        elif asArgs[iArg] == '--virt-modes':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--virt-modes" takes a colon separated list of modes');
            self.asVirtModes = asArgs[iArg].split(':');
            for s in self.asVirtModes:
                if s not in self.asVirtModesDef:
                    raise base.InvalidOption('The "--virt-modes" value "%s" is not valid; valid values are: %s' \
                        % (s, ' '.join(self.asVirtModesDef)));
        elif asArgs[iArg] == '--cpu-counts':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--cpu-counts" takes a colon separated list of cpu counts');
            self.acCpus = [];
            for s in asArgs[iArg].split(':'):
                try: c = int(s);
                except: raise base.InvalidOption('The "--cpu-counts" value "%s" is not an integer' % (s,));
                if c <= 0:  raise base.InvalidOption('The "--cpu-counts" value "%s" is zero or negative' % (s,));
                self.acCpus.append(c);
        elif asArgs[iArg] == '--nic-types':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--nic-types" takes a colon separated list of NIC types');
            self.asNicTypes = asArgs[iArg].split(':');
        elif asArgs[iArg] == '--nic-attachment':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--nic-attachment" takes an argument');
            self.sNicAttachment = asArgs[iArg];
            if self.sNicAttachment not in ('bridged', 'nat'):
                raise base.InvalidOption('The "--nic-attachment" value "%s" is not supported. Valid values are: bridged, nat' \
                        % (self.sNicAttachment));
        elif asArgs[iArg] == '--setups':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--setups" takes a colon separated list of setups');
            self.asSetups = asArgs[iArg].split(':');
            for s in self.asSetups:
                if s not in self.asSetupsDef:
                    raise base.InvalidOption('The "--setups" value "%s" is not valid; valid values are: %s' \
                        % (s, ' '.join(self.asSetupsDef)));
        elif asArgs[iArg] == '--secs-per-run':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--secs-per-run" takes second count');
            try:    self.cSecsRun = int(asArgs[iArg]);
            except: raise base.InvalidOption('The "--secs-per-run" value "%s" is not an integer' % (self.cSecsRun,));
            if self.cSecsRun <= 0:
                raise base.InvalidOption('The "--secs-per-run" value "%s" is zero or negative.' % (self.cSecsRun,));
        elif asArgs[iArg] == '--tests':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--tests" takes a colon separated list of tests');
            self.asTests = asArgs[iArg].split(':');
            for s in self.asTests:
                if s not in self.asTestsDef:
                    raise base.InvalidOption('The "--tests" value "%s" is not valid; valid values are: %s' \
                        % (s, ' '.join(self.asTestsDef)));
        elif asArgs[iArg] == '--latency-sizes':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--latency-sizes" takes a colon separated list of sizes');
            self.acbLatencyPkts = [];
            for s in asArgs[iArg].split(':'):
                try: cb = int(s);
                except: raise base.InvalidOption('The "--latency-sizes" value "%s" is not an integer' % (s,));
                if cb <= 0: raise base.InvalidOption('The "--latency-sizes" value "%s" is zero or negative' % (s,));
                self.acbLatencyPkts.append(cb);
        elif asArgs[iArg] == '--throughput-sizes':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--throughput-sizes" takes a colon separated list of sizes');
            self.acbThroughputPkts = [];
            for s in asArgs[iArg].split(':'):
                try: cb = int(s);
                except: raise base.InvalidOption('The "--throughput-sizes" value "%s" is not an integer' % (s,));
                if cb <= 0: raise base.InvalidOption('The "--throughput-sizes" value "%s" is zero or negative' % (s,));
                self.acbThroughputPkts.append(cb);
        elif asArgs[iArg] == '--test-vms':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--test-vms" takes colon separated list');
            self.asTestVMs = asArgs[iArg].split(':');
            for s in self.asTestVMs:
                if s not in self.asTestVMsDef:
                    raise base.InvalidOption('The "--test-vms" value "%s" is not valid; valid values are: %s' \
                        % (s, ' '.join(self.asTestVMsDef)));
        elif asArgs[iArg] == '--skip-vms':
            iArg += 1;
            if iArg >= len(asArgs): raise base.InvalidOption('The "--skip-vms" takes colon separated list');
            self.asSkipVMs = asArgs[iArg].split(':');
            for s in self.asSkipVMs:
                if s not in self.asTestVMsDef:
                    reporter.log('warning: The "--test-vms" value "%s" does not specify any of our test VMs.' % (s));
        elif asArgs[iArg] == '--quick':
            self.cSecsRun           = 5;
            self.asVirtModes        = ['hwvirt',];
            self.acCpus             = [1,];
            self.acbLatencyPkts     = [32,];
            self.acbThroughputPkts  = [8192,];
            self.asTestVMs          = ['tst-rhel5', 'tst-win2k3ent', 'tst-sol10',];
        else:
            return vbox.TestDriver.parseOption(self, asArgs, iArg);
        return iArg + 1;

    def completeOptions(self):
        # Remove skipped VMs from the test list.
        for sVM in self.asSkipVMs:
            try:    self.asTestVMs.remove(sVM);
            except: pass;

        # Resolve any names we've been given.
        self.sLocalIP  = base.tryGetHostByName(self.sLocalName);
        self.sRemoteIP = base.tryGetHostByName(self.sRemoteName);
        self.sGuestIP  = base.tryGetHostByName(self.sGuestName);

        reporter.log('Local IP : %s' % (self.sLocalIP));
        reporter.log('Remote IP: %s' % (self.sRemoteIP));
        if self.sGuestIP is None:
            reporter.log('Guest IP : use tst-guest2guest');
        else:
            reporter.log('Guest IP : %s' % (self.sGuestIP));

        return vbox.TestDriver.completeOptions(self);

    def getResourceSet(self):
        # Construct the resource list the first time it's queried.
        if self.asRsrcs is None:
            self.asRsrcs = [];
            if 'tst-rhel5' in self.asTestVMs  or  'g2g' in self.asSetups:
                self.asRsrcs.append('3.0/tcp/rhel5.vdi');
            if 'tst-rhel5-64' in self.asTestVMs:
                self.asRsrcs.append('3.0/tcp/rhel5-64.vdi');
            if 'tst-sles11' in self.asTestVMs:
                self.asRsrcs.append('3.0/tcp/sles11.vdi');
            if 'tst-sles11-64' in self.asTestVMs:
                self.asRsrcs.append('3.0/tcp/sles11-64.vdi');
            if 'tst-oel' in self.asTestVMs:
                self.asRsrcs.append('3.0/tcp/oel.vdi');
            if 'tst-oel-64' in self.asTestVMs:
                self.asRsrcs.append('3.0/tcp/oel-64.vdi');
            if 'tst-win2k3ent' in self.asTestVMs:
                self.asRsrcs.append('3.0/tcp/win2k3ent-acpi.vdi');
            if 'tst-win2k3ent-64' in self.asTestVMs:
                self.asRsrcs.append('3.0/tcp/win2k3ent-64.vdi');
            if 'tst-win2k8' in self.asTestVMs:
                self.asRsrcs.append('3.0/tcp/win2k8.vdi');
            if 'tst-sol10' in self.asTestVMs:
                self.asRsrcs.append('3.0/tcp/solaris10.vdi');
            if 'tst-sol11' in self.asTestVMs:
                self.asRsrcs.append('3.0/tcp/solaris11.vdi');
        return self.asRsrcs;

    def actionConfig(self):
        # Some stupid trickery to guess the location of the iso. ## fixme - testsuite unzip ++
        sVBoxValidationKit_iso = os.path.abspath(os.path.join(os.path.dirname(__file__), '../../VBoxValidationKit.iso'));
        if not os.path.isfile(sVBoxValidationKit_iso):
            sVBoxValidationKit_iso = os.path.abspath(os.path.join(os.path.dirname(__file__), '../../VBoxTestSuite.iso'));
        if not os.path.isfile(sVBoxValidationKit_iso):
            sVBoxValidationKit_iso = '/mnt/ramdisk/vbox/svn/trunk/validationkit/VBoxValidationKit.iso';
        if not os.path.isfile(sVBoxValidationKit_iso):
            sVBoxValidationKit_iso = '/mnt/ramdisk/vbox/svn/trunk/testsuite/VBoxTestSuite.iso';
        if not os.path.isfile(sVBoxValidationKit_iso):
            sCur = os.getcwd();
            for i in range(0, 10):
                sVBoxValidationKit_iso = os.path.join(sCur, 'validationkit/VBoxValidationKit.iso');
                if os.path.isfile(sVBoxValidationKit_iso):
                    break;
                sVBoxValidationKit_iso = os.path.join(sCur, 'testsuite/VBoxTestSuite.iso');
                if os.path.isfile(sVBoxValidationKit_iso):
                    break;
                sCur = os.path.abspath(os.path.join(sCur, '..'));
                if i is None: pass; # shut up pychecker/pylint.
        if not os.path.isfile(sVBoxValidationKit_iso):
            sVBoxValidationKit_iso = '/home/bird/validationkit/VBoxValidationKit.iso';
        if not os.path.isfile(sVBoxValidationKit_iso):
            sVBoxValidationKit_iso = '/home/bird/testsuite/VBoxTestSuite.iso';

        # Make sure vboxapi has been imported so we can use the constants.
        if not self.importVBoxApi():
            return False;

        # Guest to Guest VM.
        if self.sGuestName is None and 'g2g' in self.asSetups:
            oVM = self.createTestVM('tst-guest2guest', 0, '3.0/tcp/rhel5.vdi', sKind = 'RedHat', fIoApic = True, \
                    eNic0Type = vboxcon.NetworkAdapterType_I82545EM, \
                    eNic0AttachType = vboxcon.NetworkAttachmentType_Bridged, \
                    fVirtEx = True, sDvdImage = sVBoxValidationKit_iso);
            if oVM is None:
                return False;
            self.oGuestToGuestVM = oVM;

        #
        # Configure the VMs we're going to use.
        #
        eNic0AttachType = vboxcon.NetworkAttachmentType_Bridged;
        if self.sNicAttachment == 'nat':
            eNic0AttachType = vboxcon.NetworkAttachmentType_NAT;

        # Linux VMs
        if 'tst-rhel5' in self.asTestVMs:
            oVM = self.createTestVM('tst-rhel5', 1, '3.0/tcp/rhel5.vdi', sKind = 'RedHat', fIoApic = True, \
                eNic0AttachType = eNic0AttachType, sDvdImage = sVBoxValidationKit_iso);
            if oVM is None:
                return False;

        if 'tst-rhel5-64' in self.asTestVMs:
            oVM = self.createTestVM('tst-rhel5-64', 1, '3.0/tcp/rhel5-64.vdi', sKind = 'RedHat_64', \
                eNic0AttachType = eNic0AttachType, sDvdImage = sVBoxValidationKit_iso);
            if oVM is None:
                return False;

        if 'tst-sles11' in self.asTestVMs:
            oVM = self.createTestVM('tst-sles11', 1, '3.0/tcp/sles11.vdi', sKind = 'OpenSUSE', fIoApic = True, \
                eNic0AttachType = eNic0AttachType, sDvdImage = sVBoxValidationKit_iso);
            if oVM is None:
                return False;

        if 'tst-sles11-64' in self.asTestVMs:
            oVM = self.createTestVM('tst-sles11-64', 1, '3.0/tcp/sles11-64.vdi', sKind = 'OpenSUSE_64', \
                eNic0AttachType = eNic0AttachType, sDvdImage = sVBoxValidationKit_iso);
            if oVM is None:
                return False;

        if 'tst-oel' in self.asTestVMs:
            oVM = self.createTestVM('tst-oel', 1, '3.0/tcp/oel.vdi', sKind = 'Oracle', fIoApic = True, \
                eNic0AttachType = eNic0AttachType, sDvdImage = sVBoxValidationKit_iso);
            if oVM is None:
                return False;

        if 'tst-oel-64' in self.asTestVMs:
            oVM = self.createTestVM('tst-oel-64', 1, '3.0/tcp/oel-64.vdi', sKind = 'Oracle_64', \
                eNic0AttachType = eNic0AttachType, sDvdImage = sVBoxValidationKit_iso);
            if oVM is None:
                return False;

        # Windows VMs
        if 'tst-win2k3ent' in self.asTestVMs:
            oVM = self.createTestVM('tst-win2k3ent', 1, '3.0/tcp/win2k3ent-acpi.vdi', sKind = 'Windows2003', \
                eNic0AttachType = eNic0AttachType, sDvdImage = sVBoxValidationKit_iso);
            if oVM is None:
                return False;

        if 'tst-win2k3ent-64' in self.asTestVMs:
            oVM = self.createTestVM('tst-win2k3ent-64', 1, '3.0/tcp/win2k3ent-64.vdi', sKind = 'Windows2003_64', \
                eNic0AttachType = eNic0AttachType, sDvdImage = sVBoxValidationKit_iso);
            if oVM is None:
                return False;

        if 'tst-win2k8' in self.asTestVMs:
            oVM = self.createTestVM('tst-win2k8', 1, '3.0/tcp/win2k8.vdi', sKind = 'Windows2008_64', \
                eNic0AttachType = eNic0AttachType, sDvdImage = sVBoxValidationKit_iso);
            if oVM is None:
                return False;

        # Solaris VMs
        if 'tst-sol10' in self.asTestVMs:
            oVM = self.createTestVM('tst-sol10', 1, '3.0/tcp/solaris10.vdi', sKind = 'Solaris_64', \
                eNic0AttachType = eNic0AttachType, sDvdImage = sVBoxValidationKit_iso);
            if oVM is None:
                return False;

        if 'tst-sol11' in self.asTestVMs:
            oVM = self.createTestVM('tst-sol11', 1, '3.0/tcp/os2009-11.vdi', sKind = 'Solaris_64', \
                eNic0AttachType = eNic0AttachType, sDvdImage = sVBoxValidationKit_iso);
            if oVM is None:
                return False;

        return True;

    def actionExecute(self):
        """
        Execute the testcase.
        """
        fRc = self.test1();
        return fRc;


    #
    # Test execution helpers.
    #

    def test1RunTestProgs(self, oTxsSession, fRc, sTestName, sAddr):
        """
        Runs all the test programs against one 'server' machine.
        """
        reporter.testStart(sTestName);

        reporter.testStart('TCP latency');
        if fRc and 'tcp-latency' in self.asTests and sAddr is not None:
            for cbPkt in self.acbLatencyPkts:
                fRc = self.txsRunTest(oTxsSession, '%u bytes' % (cbPkt), self.cSecsRun * 1000 * 4,
                                      '${CDROM}/${OS/ARCH}/NetPerf${EXESUFF}',
                                      ('NetPerf', '--client', sAddr, '--interval', self.cSecsRun, '--len', cbPkt,
                                       '--mode', 'latency'));
                if not fRc:
                    break;
            reporter.testDone();
        else:
            reporter.testDone(fSkipped = True);

        reporter.testStart('TCP throughput');
        if fRc and 'tcp-throughput' in self.asTests and sAddr is not None:
            for cbPkt in self.acbThroughputPkts:
                fRc = self.txsRunTest(oTxsSession, '%u bytes' % (cbPkt), self.cSecsRun * 2 * 1000 * 4,
                                      '${CDROM}/${OS/ARCH}/NetPerf${EXESUFF}',
                                      ('NetPerf', '--client', sAddr, '--interval', self.cSecsRun, '--len', cbPkt,
                                       '--mode', 'throughput'));
                if not fRc:
                    break;
            reporter.testDone();
        else:
            reporter.testDone(fSkipped = True);

        reporter.testStart('UDP latency');
        if fRc and 'udp-latency' in self.asTests and sAddr is not None:
            ## @todo Netperf w/UDP.
            reporter.testDone(fSkipped = True);
        else:
            reporter.testDone(fSkipped = True);

        reporter.testStart('UDP throughput');
        if fRc and 'udp-throughput' in self.asTests and sAddr is not None:
            ## @todo Netperf w/UDP.
            reporter.testDone(fSkipped = True);
        else:
            reporter.testDone(fSkipped = True);

        reporter.testStart('tbench');
        if fRc and 'tbench' in self.asTests and sAddr is not None:
            ## @todo tbench.
            reporter.testDone(fSkipped = True);
        else:
            reporter.testDone(fSkipped = True);

        reporter.testDone(not fRc);
        return fRc;

    def test1OneCfg(self, sVmName, eNicType, cCpus, fHwVirt, fNestedPaging):
        """
        Runs the specified VM thru test #1.

        Returns a success indicator on the general test execution. This is not
        the actual test result.
        """
        oVM = self.getVmByName(sVmName);

        # Reconfigure the VM
        fRc = True;
        oSession = self.openSession(oVM);
        if oSession is not None:
            fRc = fRc and oSession.setNicType(eNicType);
            fRc = fRc and oSession.enableVirtExX86(fHwVirt);
            fRc = fRc and oSession.enableNestedPagingX86(fNestedPaging);
            fRc = fRc and oSession.setCpuCount(cCpus);
            fRc = fRc and oSession.saveSettings();
            fRc = oSession.close() and fRc and True; # pychecker hack.
            oSession = None;
        else:
            fRc = False;

        # Start up.
        if fRc is True:
            self.logVmInfo(oVM);
            oSession, oTxsSession = self.startVmAndConnectToTxsViaTcp(sVmName, fCdWait = True);
            if oSession is not None:
                self.addTask(oTxsSession);

                # Fudge factor - Allow the guest to finish starting up.
                self.sleep(5);

                # Benchmark #1 - guest <-> host.
                if 'g2h' in self.asSetups:
                    self.test1RunTestProgs(oTxsSession, fRc, 'guest <-> host',  self.sLocalIP);

                # Benchmark #2 - guest <-> host.
                if 'g2r' in self.asSetups:
                    self.test1RunTestProgs(oTxsSession, fRc, 'guest <-> remote', self.sRemoteIP);

                # Benchmark #3 - guest <-> guest.
                if 'g2g' in self.asSetups:
                    self.test1RunTestProgs(oTxsSession, fRc, 'guest <-> guest',  self.sGuestIP);

                # cleanup.
                self.removeTask(oTxsSession);
                self.terminateVmBySession(oSession)
            else:
                fRc = False;
        return fRc;

    def test1OneVM(self, sVmName, asSkipNicTypes = (), asSupVirtModes = None, rSupCpus = range(1, 256)):
        """
        Runs one VM thru the various configurations.
        """
        if asSupVirtModes is None:
            asSupVirtModes = self.asVirtModes;

        reporter.testStart(sVmName);
        fRc = True;
        for sNicType in self.asNicTypes:
            if sNicType in asSkipNicTypes:
                continue;
            reporter.testStart(sNicType);

            if sNicType == 'E1000':
                eNicType = vboxcon.NetworkAdapterType_I82545EM;
            elif sNicType == 'PCNet':
                eNicType = vboxcon.NetworkAdapterType_Am79C973;
            elif sNicType == 'Virtio':
                eNicType = vboxcon.NetworkAdapterType_Virtio;
            else:
                eNicType = None;

            for cCpus in self.acCpus:
                if cCpus == 1:  reporter.testStart('1 cpu');
                else:           reporter.testStart('%u cpus' % (cCpus));

                for sVirtMode in self.asVirtModes:
                    if sVirtMode == 'raw' and cCpus > 1:
                        continue;
                    if cCpus not in rSupCpus:
                        continue;
                    if sVirtMode not in asSupVirtModes:
                        continue;
                    hsVirtModeDesc = {};
                    hsVirtModeDesc['raw']       = 'Raw-mode';
                    hsVirtModeDesc['hwvirt']    = 'HwVirt';
                    hsVirtModeDesc['hwvirt-np'] = 'NestedPaging';
                    reporter.testStart(hsVirtModeDesc[sVirtMode]);

                    fHwVirt       = sVirtMode != 'raw';
                    fNestedPaging = sVirtMode == 'hwvirt-np';
                    fRc = self.test1OneCfg(sVmName, eNicType, cCpus, fHwVirt, fNestedPaging)  and  fRc and True; # pychecker hack.

                    reporter.testDone();
                reporter.testDone();
            reporter.testDone();
        reporter.testDone();
        return fRc;

    def test1(self):
        """
        Executes test #1.
        """

        # Start the VM for the guest to guest testing, if required.
        fRc = True;
        if 'g2g' in self.asSetups  and  self.sGuestName is None:
            self.oGuestToGuestSess, self.oGuestToGuestTxs = self.startVmAndConnectToTxsViaTcp('tst-guest2guest', fCdWait = True);
            if self.oGuestToGuestSess is None:
                return False;
            self.sGuestIP = self.oGuestToGuestSess.getPrimaryIp();
            reporter.log('tst-guest2guest IP: %s' % (self.sGuestIP));

            # Start the test servers on it.
            fRc = self.oGuestToGuestTxs.syncExec('${CDROM}/${OS/ARCH}/NetPerf${EXESUFF}',
                                                 ('NetPerf', '--server', '--daemonize'), fWithTestPipe=False);

        # Loop thru the test VMs.
        if fRc:
            for sVM in self.asTestVMs:
                # figure args.
                asSkipNicTypes = [];
                if sVM not in ('tst-sles11', 'tst-sles11-64'):
                    asSkipNicTypes.append('Virtio');
                if sVM in ('tst-sol11', 'tst-sol10'):
                    asSkipNicTypes.append('PCNet');
                asSupVirtModes = None;
                if sVM in ('tst-sol11', 'tst-sol10'): # 64-bit only
                    asSupVirtModes = ('hwvirt', 'hwvirt-np',);

                # run test on the VM.
                if not self.test1OneVM(sVM, asSkipNicTypes, asSupVirtModes):
                    fRc = False;

        # Kill the guest to guest VM and clean up the state.
        if self.oGuestToGuestSess is not None:
            self.terminateVmBySession(self.oGuestToGuestSess);
            self.oGuestToGuestSess = None;
            self.oGuestToGuestTxs  = None;
            self.sGuestIP          = None;

        return fRc;



if __name__ == '__main__':
    sys.exit(tdNetBenchmark1().main(sys.argv));

