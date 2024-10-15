-- $Id: tmdb-r26-testboxes-4.pgsql 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
--- @file
-- VBox Test Manager Database - Adds fNativeApi to TestBoxes.
--

--
-- Copyright (C) 2024 Oracle and/or its affiliates.
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

--
-- Cleanup after failed runs.
--
DROP TABLE IF EXISTS OldTestBoxes;

--
-- Die on error from now on.
--
\set ON_ERROR_STOP 1
\set AUTOCOMMIT 0

-- Total grid lock.
LOCK TABLE TestBoxStatuses      IN ACCESS EXCLUSIVE MODE;
LOCK TABLE TestSets             IN ACCESS EXCLUSIVE MODE;
LOCK TABLE SchedGroupMembers    IN ACCESS EXCLUSIVE MODE;
LOCK TABLE TestBoxes            IN ACCESS EXCLUSIVE MODE;

--
-- Rename the original table, drop constrains and foreign key references so we
-- get the right name automatic when creating the new one.
--
\d+ TestBoxes;

ALTER TABLE TestBoxes RENAME TO OldTestBoxes;

ALTER TABLE OldTestBoxes    DROP CONSTRAINT testboxes_ccpus_check;
ALTER TABLE OldTestBoxes    DROP CONSTRAINT testboxes_check;
ALTER TABLE OldTestBoxes    DROP CONSTRAINT testboxes_cmbmemory_check;
ALTER TABLE OldTestBoxes    DROP CONSTRAINT testboxes_cmbscratch_check;
ALTER TABLE OldTestBoxes    DROP CONSTRAINT testboxes_pctscaletimeout_check;

ALTER TABLE TestBoxStatuses DROP CONSTRAINT TestBoxStatuses_idGenTestBox_fkey;
ALTER TABLE TestSets        DROP CONSTRAINT TestSets_idGenTestBox_fkey;

ALTER TABLE OldTestBoxes    DROP CONSTRAINT testboxes_pkey;
ALTER TABLE OldTestBoxes    DROP CONSTRAINT testboxes_idgentestbox_key;

DROP INDEX IF EXISTS TestBoxesUuidIdx;
DROP INDEX IF EXISTS TestBoxesExpireEffectiveIdx;

-- This output should be free of index, constraints and references from other tables.
\d+ OldTestBoxes;


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
    idStrDescription    INTEGER     REFERENCES TestBoxStrTab(idStr)  DEFAULT NULL,

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

    --- Change comment or similar.
    idStrComment        INTEGER     REFERENCES TestBoxStrTab(idStr)  DEFAULT NULL,

    --- @name Scheduling properties (reported by testbox script).
    -- @{
    --- Same abbrieviations as kBuild, see KBUILD_OSES.
    idStrOs             INTEGER     REFERENCES TestBoxStrTab(idStr)  DEFAULT NULL,
    --- Informational, no fixed format.
    idStrOsVersion      INTEGER     REFERENCES TestBoxStrTab(idStr)  DEFAULT NULL,
    --- Same as CPUID reports (GenuineIntel, AuthenticAMD, CentaurHauls, ...).
    idStrCpuVendor      INTEGER     REFERENCES TestBoxStrTab(idStr)  DEFAULT NULL,
    --- Same as kBuild - x86, amd64, ... See KBUILD_ARCHES.
    idStrCpuArch        INTEGER     REFERENCES TestBoxStrTab(idStr)  DEFAULT NULL,
    --- The CPU name if available.
    idStrCpuName        INTEGER     REFERENCES TestBoxStrTab(idStr)  DEFAULT NULL,
    --- Number identifying the CPU family/model/stepping/whatever.
    -- For x86 and AMD64 type CPUs, this will on the following format:
    --   (EffFamily << 24) | (EffModel << 8) | Stepping.
    lCpuRevision        bigint      DEFAULT NULL,
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
    --- Set if the test box does raw-mode tests.
    fRawMode            boolean     DEFAULT NULL,
    -- Set if the test box does native API (NEM) tests.
    fNativeApi          boolean     DEFAULT NULL,
    --- The (approximate) memory size in megabytes (rounded down to nearest 4 MB).
    cMbMemory           bigint      DEFAULT NULL  CHECK (cMbMemory IS NULL OR cMbMemory > 0),
    --- The amount of scratch space in megabytes (rounded down to nearest 64 MB).
    cMbScratch          bigint      DEFAULT NULL  CHECK (cMbScratch IS NULL OR cMbScratch >= 0),
    --- Free form hardware and software report field.
    idStrReport         INTEGER     REFERENCES TestBoxStrTab(idStr)  DEFAULT NULL,
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


--
-- Populate the test box table.
--

INSERT INTO TestBoxes (
            idTestBox,          --  0
            tsEffective,        --  1
            tsExpire,           --  2
            uidAuthor,          --  3
            idGenTestBox,       --  4
            ip,                 --  5
            uuidSystem,         --  6
            sName,              --  7
            idStrDescription,   --  8
            fEnabled,           --  9
            enmLomKind,         -- 10
            ipLom,              -- 11
            pctScaleTimeout,    -- 12
            idStrComment,       -- 13
            idStrOs,            -- 14
            idStrOsVersion,     -- 15
            idStrCpuVendor,     -- 16
            idStrCpuArch,       -- 17
            idStrCpuName,       -- 18
            lCpuRevision,       -- 19
            cCpus,              -- 20
            fCpuHwVirt,         -- 21
            fCpuNestedPaging,   -- 22
            fCpu64BitGuest,     -- 23
            fChipsetIoMmu,      -- 24
            fRawMode,           -- 25
            fNativeApi,         -- 26
            cMbMemory,          -- 27
            cMbScratch,         -- 28
            idStrReport,        -- 29
            iTestBoxScriptRev,  -- 30
            iPythonHexVersion,  -- 31
            enmPendingCmd       -- 32
            )
SELECT      idTestBox,          --  0
            tsEffective,        --  1
            tsExpire,           --  2
            uidAuthor,          --  3
            idGenTestBox,       --  4
            ip,                 --  5
            uuidSystem,         --  6
            sName,              --  7
            idStrDescription,   --  8
            fEnabled,           --  9
            enmLomKind,         -- 10
            ipLom,              -- 11
            pctScaleTimeout,    -- 12
            idStrComment,       -- 13
            idStrOs,            -- 14
            idStrOsVersion,     -- 15
            idStrCpuVendor,     -- 16
            idStrCpuArch,       -- 17
            idStrCpuName,       -- 18
            lCpuRevision,       -- 19
            cCpus,              -- 20
            fCpuHwVirt,         -- 21
            fCpuNestedPaging,   -- 22
            fCpu64BitGuest,     -- 23
            fChipsetIoMmu,      -- 24
            fRawMode,           -- 25
            NULL,               -- 26
            cMbMemory,          -- 27
            cMbScratch,         -- 28
            idStrReport,        -- 29
            iTestBoxScriptRev,  -- 30
            iPythonHexVersion,  -- 31
            enmPendingCmd       -- 32
FROM        OldTestBoxes;

-- Restore indexes.
CREATE UNIQUE INDEX TestBoxesUuidIdx ON TestBoxes (uuidSystem, tsExpire DESC);
CREATE INDEX TestBoxesExpireEffectiveIdx ON TestBoxes (tsExpire DESC, tsEffective ASC);

-- Restore foreign key references to the table.
ALTER TABLE TestBoxStatuses ADD  CONSTRAINT TestBoxStatuses_idGenTestBox_fkey
    FOREIGN KEY (idGenTestBox) REFERENCES TestBoxes(idGenTestBox);
ALTER TABLE TestSets        ADD  CONSTRAINT TestSets_idGenTestBox_fkey
    FOREIGN KEY (idGenTestBox) REFERENCES TestBoxes(idGenTestBox);

DROP VIEW TestBoxesWithStrings;
CREATE VIEW TestBoxesWithStrings AS
    SELECT  TestBoxes.*,
            Str1.sValue AS sDescription,
            Str2.sValue AS sComment,
            Str3.sValue AS sOs,
            Str4.sValue AS sOsVersion,
            Str5.sValue AS sCpuVendor,
            Str6.sValue AS sCpuArch,
            Str7.sValue AS sCpuName,
            Str8.sValue AS sReport
    FROM    TestBoxes
            LEFT OUTER JOIN TestBoxStrTab Str1 ON idStrDescription = Str1.idStr
            LEFT OUTER JOIN TestBoxStrTab Str2 ON idStrComment     = Str2.idStr
            LEFT OUTER JOIN TestBoxStrTab Str3 ON idStrOs          = Str3.idStr
            LEFT OUTER JOIN TestBoxStrTab Str4 ON idStrOsVersion   = Str4.idStr
            LEFT OUTER JOIN TestBoxStrTab Str5 ON idStrCpuVendor   = Str5.idStr
            LEFT OUTER JOIN TestBoxStrTab Str6 ON idStrCpuArch     = Str6.idStr
            LEFT OUTER JOIN TestBoxStrTab Str7 ON idStrCpuName     = Str7.idStr
            LEFT OUTER JOIN TestBoxStrTab Str8 ON idStrReport      = Str8.idStr;

-- Drop the old table.
DROP TABLE OldTestBoxes;

COMMIT;

\d+ TestBoxes;


--
-- Update the TestBoxLogic_updateOnSignOn function from core/testbox.pgsql.
-- Note sure if we usually do it this way or not...
--
CREATE OR REPLACE function TestBoxLogic_updateOnSignOn(a_idTestBox          INTEGER,
                                                       a_ip                 inet,
                                                       a_sOs                TEXT,
                                                       a_sOsVersion         TEXT,
                                                       a_sCpuVendor         TEXT,
                                                       a_sCpuArch           TEXT,
                                                       a_sCpuName           TEXT,
                                                       a_lCpuRevision       bigint,
                                                       a_cCpus              INTEGER, -- Actually smallint, but default typing fun.
                                                       a_fCpuHwVirt         boolean,
                                                       a_fCpuNestedPaging   boolean,
                                                       a_fCpu64BitGuest     boolean,
                                                       a_fChipsetIoMmu      boolean,
                                                       a_fRawMode           boolean,
                                                       a_fNativeApi         boolean,
                                                       a_cMbMemory          bigint,
                                                       a_cMbScratch         bigint,
                                                       a_sReport            TEXT,
                                                       a_iTestBoxScriptRev  INTEGER,
                                                       a_iPythonHexVersion  INTEGER,
                                                       OUT r_idGenTestBox   INTEGER
                                                       ) AS $$
    DECLARE
        v_Row               TestBoxes%ROWTYPE;
        v_idStrOs           INTEGER;
        v_idStrOsVersion    INTEGER;
        v_idStrCpuVendor    INTEGER;
        v_idStrCpuArch      INTEGER;
        v_idStrCpuName      INTEGER;
        v_idStrReport       INTEGER;
    BEGIN
        SELECT TestBoxLogic_lookupOrFindString(a_sOs)           INTO v_idStrOs;
        SELECT TestBoxLogic_lookupOrFindString(a_sOsVersion)    INTO v_idStrOsVersion;
        SELECT TestBoxLogic_lookupOrFindString(a_sCpuVendor)    INTO v_idStrCpuVendor;
        SELECT TestBoxLogic_lookupOrFindString(a_sCpuArch)      INTO v_idStrCpuArch;
        SELECT TestBoxLogic_lookupOrFindString(a_sCpuName)      INTO v_idStrCpuName;
        SELECT TestBoxLogic_lookupOrFindString(a_sReport)       INTO v_idStrReport;

        -- Fetch and historize the current row - there must be one.
        UPDATE      TestBoxes
            SET     tsExpire     = CURRENT_TIMESTAMP
            WHERE   idTestBox    = a_idTestBox
                AND tsExpire     = 'infinity'::TIMESTAMP
            RETURNING * INTO STRICT v_Row;

        -- Modify the row with the new data.
        v_Row.uidAuthor             := NULL;
        v_Row.ip                    := a_ip;
        v_Row.idStrOs               := v_idStrOs;
        v_Row.idStrOsVersion        := v_idStrOsVersion;
        v_Row.idStrCpuVendor        := v_idStrCpuVendor;
        v_Row.idStrCpuArch          := v_idStrCpuArch;
        v_Row.idStrCpuName          := v_idStrCpuName;
        v_Row.lCpuRevision          := a_lCpuRevision;
        v_Row.cCpus                 := a_cCpus;
        v_Row.fCpuHwVirt            := a_fCpuHwVirt;
        v_Row.fCpuNestedPaging      := a_fCpuNestedPaging;
        v_Row.fCpu64BitGuest        := a_fCpu64BitGuest;
        v_Row.fChipsetIoMmu         := a_fChipsetIoMmu;
        v_Row.fRawMode              := a_fRawMode;
        v_Row.fNativeApi            := a_fNativeApi;
        v_Row.cMbMemory             := a_cMbMemory;
        v_Row.cMbScratch            := a_cMbScratch;
        v_Row.idStrReport           := v_idStrReport;
        v_Row.iTestBoxScriptRev     := a_iTestBoxScriptRev;
        v_Row.iPythonHexVersion     := a_iPythonHexVersion;
        v_Row.tsEffective           := v_Row.tsExpire;
        v_Row.tsExpire              := 'infinity'::TIMESTAMP;

        -- Get a new generation ID.
        SELECT NEXTVAL('TestBoxGenIdSeq') INTO v_Row.idGenTestBox;
        r_idGenTestBox  := v_Row.idGenTestBox;

        -- Insert the modified row.
        INSERT INTO TestBoxes VALUES (v_Row.*);
    END;
$$ LANGUAGE plpgsql;

COMMIT;

