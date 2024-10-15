-- $Id: tmdb-r02-testboxes-1.pgsql 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
--- @file
-- VBox Test Manager Database - Adds fCpu64BitGuest to TestBoxes
--

--
-- Copyright (C) 2013-2024 Oracle and/or its affiliates.
--
-- This file is part of VirtualBox base platform packages, as
-- available from https://www.virtualbox.org.
--
-- This program is free software; you can redistribute it and/or
-- modify it under the terms of the GNU General Public License
-- as published by the Free Software Foundation, in version 3 of the
-- License.
--
-- This program is distributed in the hope that it will be useful, but
-- WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
-- General Public License for more details.
--
-- You should have received a copy of the GNU General Public License
-- along with this program; if not, see <https://www.gnu.org/licenses>.
--
-- The contents of this file may alternatively be used under the terms
-- of the Common Development and Distribution License Version 1.0
-- (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
-- in the VirtualBox distribution, in which case the provisions of the
-- CDDL are applicable instead of those of the GPL.
--
-- You may elect to license modified versions of this file under the
-- terms and conditions of either the GPL or the CDDL or both.
--
-- SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
--


DROP TABLE OldTestBoxes;
DROP TABLE NewTestBoxes;

\d TestBoxes;

\set ON_ERROR_STOP 1
\set AUTOCOMMIT 0

LOCK TABLE TestBoxStatuses IN ACCESS EXCLUSIVE MODE;
LOCK TABLE TestSets        IN ACCESS EXCLUSIVE MODE;
LOCK TABLE TestBoxes       IN ACCESS EXCLUSIVE MODE;

DROP INDEX TestBoxesUuidIdx;

--
-- Rename the original table, drop constrains and foreign key references so we
-- get the right name automatic when creating the new one.
--
ALTER TABLE TestBoxes RENAME TO OldTestBoxes;

ALTER TABLE OldTestBoxes DROP CONSTRAINT testboxes_ccpus_check;
ALTER TABLE OldTestBoxes DROP CONSTRAINT testboxes_check;
ALTER TABLE OldTestBoxes DROP CONSTRAINT testboxes_cmbmemory_check;
ALTER TABLE OldTestBoxes DROP CONSTRAINT testboxes_cmbscratch_check;
ALTER TABLE OldTestBoxes DROP CONSTRAINT testboxes_pctscaletimeout_check;

ALTER TABLE TestBoxStatuses DROP CONSTRAINT TestBoxStatuses_idGenTestBox_fkey;
ALTER TABLE TestSets        DROP CONSTRAINT TestSets_idGenTestBox_fkey;

ALTER TABLE OldTestBoxes DROP CONSTRAINT testboxes_pkey;
ALTER TABLE OldTestBoxes DROP CONSTRAINT testboxes_idgentestbox_key;

--
-- Create the new table, filling it with the current TestBoxes content.
--
CREATE TABLE TestBoxes (
    --- The fixed testbox ID.
    -- This is assigned when the testbox is created and will never change.
    idTestBox           INTEGER     DEFAULT NEXTVAL('TestBoxIdSeq')  NOT NULL,
    --- When this row starts taking effect (inclusive).
    tsEffective         TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- When this row stops being tsEffective (exclusive).
    tsExpire            TIMESTAMP WITH TIME ZONE  DEFAULT TIMESTAMP WITH TIME ZONE 'infinity'  NOT NULL,
    --- The user id of the one who created/modified this entry.
    -- When modified automatically by the testbox, NULL is used.
    -- Non-unique foreign key: Users(uid)
    uidAuthor           INTEGER     DEFAULT NULL,
    --- Generation ID for this row.
    -- This is primarily for referencing by TestSets.
    idGenTestBox        INTEGER     UNIQUE DEFAULT NEXTVAL('TestBoxGenIdSeq')  NOT NULL,

    --- The testbox IP.
    -- This is from the webserver point of view and automatically updated on
    -- SIGNON.  The test setup doesn't permit for IP addresses to change while
    -- the testbox is operational, because this will break gang tests.
    ip                  inet        NOT NULL,
    --- The system or firmware UUID.
    -- This uniquely identifies the testbox when talking to the server.  After
    -- SIGNON though, the testbox will also provide idTestBox and ip to
    -- establish its identity beyond doubt.
    uuidSystem          uuid        NOT NULL,
    --- The testbox name.
    -- Usually similar to the DNS name.
    sName               text        NOT NULL,
    --- Optional testbox description.
    -- Intended for describing the box as well as making other relevant notes.
    sDescription        text        DEFAULT NULL,

    --- Reference to the scheduling group that this testbox is a member of.
    -- Non-unique foreign key: SchedGroups(idSchedGroup)
    -- A testbox is always part of a group, the default one nothing else.
    idSchedGroup        INTEGER     DEFAULT 1  NOT NULL,

    --- Indicates whether this testbox is enabled.
    -- A testbox gets disabled when we're doing maintenance, debugging a issue
    -- that happens only on that testbox, or some similar stuff.  This is an
    -- alternative to deleting the testbox.
    fEnabled            BOOLEAN     DEFAULT NULL,

    --- The kind of lights-out-management.
    enmLomKind          LomKind_T   DEFAULT 'none'::LomKind_T  NOT NULL,
    --- The IP adress of the lights-out-management.
    -- This can be NULL if enmLomKind is 'none', otherwise it must contain a valid address.
    ipLom               inet        DEFAULT NULL,

    --- Timeout scale factor, given as a percent.
    -- This is a crude adjustment of the test case timeout for slower hardware.
    pctScaleTimeout     smallint    DEFAULT 100  NOT NULL  CHECK (pctScaleTimeout > 10 AND pctScaleTimeout < 20000),

    --- @name Scheduling properties (reported by testbox script).
    -- @{
    --- Same abbrieviations as kBuild, see KBUILD_OSES.
    sOs                 text        DEFAULT NULL,
    --- Informational, no fixed format.
    sOsVersion          text        DEFAULT NULL,
    --- Same as CPUID reports (GenuineIntel, AuthenticAMD, CentaurHauls, ...).
    sCpuVendor          text        DEFAULT NULL,
    --- Same as kBuild - x86, amd64, ... See KBUILD_ARCHES.
    sCpuArch            text        DEFAULT NULL,
    --- Number of CPUs, CPU cores and CPU threads.
    cCpus               smallint    DEFAULT NULL  CHECK (cCpus IS NULL OR cCpus > 0),
    --- Set if capable of hardware virtualization.
    fCpuHwVirt          boolean     DEFAULT NULL,
    --- Set if capable of nested paging.
    fCpuNestedPaging    boolean     DEFAULT NULL,
    --- Set if CPU capable of 64-bit (VBox) guests.
    fCpu64BitGuest      boolean     DEFAULT NULL,
    --- Set if chipset with usable IOMMU (VT-d / AMD-Vi).
    fChipsetIoMmu       boolean     DEFAULT NULL,
    --- The (approximate) memory size in megabytes (rounded down to nearest 4 MB).
    cMbMemory           bigint      DEFAULT NULL  CHECK (cMbMemory IS NULL OR cMbMemory > 0),
    --- The amount of scratch space in megabytes (rounded down to nearest 64 MB).
    cMbScratch          bigint      DEFAULT NULL  CHECK (cMbScratch IS NULL OR cMbScratch >= 0),
    --- @}

    --- The testbox script revision number, serves the purpose of a version number.
    -- Probably good to have when scheduling upgrades as well for status purposes.
    iTestBoxScriptRev   INTEGER     DEFAULT 0  NOT NULL,
    --- The python sys.hexversion (layed out as of 2.7).
    -- Good to know which python versions we need to support.
    iPythonHexVersion   INTEGER     DEFAULT NULL,

    --- Pending command.
    -- @note We put it here instead of in TestBoxStatuses to get history.
    enmPendingCmd       TestBoxCmd_T  DEFAULT 'none'::TestBoxCmd_T  NOT NULL,

    PRIMARY KEY (idTestBox, tsExpire),

    --- Nested paging requires hardware virtualization.
    CHECK (fCpuNestedPaging IS NULL OR (fCpuNestedPaging <> TRUE OR fCpuHwVirt = TRUE))
);


INSERT INTO TestBoxes ( idTestBox, tsEffective, tsExpire, uidAuthor, idGenTestBox, ip, uuidSystem, sName, sDescription,
           idSchedGroup, fEnabled, enmLomKind, ipLom, pctScaleTimeout, sOs, sOsVersion, sCpuVendor, sCpuArch, cCpus, fCpuHwVirt,
           fCpuNestedPaging, fCpu64BitGuest, fChipsetIoMmu, cMbMemory, cMbScratch, iTestBoxScriptRev, iPythonHexVersion,
           enmPendingCmd )
    SELECT idTestBox, tsEffective, tsExpire, uidAuthor, idGenTestBox, ip, uuidSystem, sName, sDescription,
           idSchedGroup, fEnabled, enmLomKind, ipLom, pctScaleTimeout, sOs, sOsVersion, sCpuVendor, sCpuArch, cCpus, fCpuHwVirt,
           fCpuNestedPaging, TRUE,           fChipsetIoMmu, cMbMemory, cMbScratch, iTestBoxScriptRev, iPythonHexVersion,
           enmPendingCmd
    FROM OldTestBoxes;

-- Add index.
CREATE UNIQUE INDEX TestBoxesUuidIdx ON TestBoxes (uuidSystem, tsExpire);

-- Restore foreign key references to the table.
ALTER TABLE TestBoxStatuses ADD  CONSTRAINT TestBoxStatuses_idGenTestBox_fkey  FOREIGN KEY (idGenTestBox) REFERENCES TestBoxes(idGenTestBox);
ALTER TABLE TestSets ADD  CONSTRAINT TestSets_idGenTestBox_fkey  FOREIGN KEY (idGenTestBox) REFERENCES TestBoxes(idGenTestBox);

-- Drop the old table.
DROP TABLE OldTestBoxes;

COMMIT;

\d TestBoxes;

