-- $Id: TestManagerDatabaseInit.pgsql 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
--- @file
-- VBox Test Manager Database Creation script.
--

--
-- Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
-- Declaimer:
--
--     The guys working on this design are not database experts, web
--     programming experts or similar, rather we are low level guys
--     who's main job is x86 & AMD64 virtualization.  So, please don't
--     be too hard on us. :-)
--
--


--  D R O P   D A T A B A S E    t e s t m a n a g e r  - -   you do this now.
\set ON_ERROR_STOP 1
CREATE DATABASE testmanager;
\connect testmanager;


-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
--
--     S y s t e m
--
-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

---
-- Log table for a few important events.
--
-- Currently, two events are planned to be logged:
--      - Sign on of an unknown testbox, including the IP and System UUID.
--        This will be restricted to one entry per 24h or something like that:
--              SELECT  COUNT(*)
--              FROM    SystemLog
--              WHERE   tsCreated >= (current_timestamp - interval '24 hours')
--                  AND sEvent = 'TBoxUnkn'
--                  AND sLogText = :sNewLogText;
--      - When cleaning up an abandoned testcase (scenario #9), log which
--        testbox abandoned which testset.
--
-- The Web UI will have some way of displaying the log.
--
-- A batch job should regularly clean out old log messages, like for instance
-- > 64 days.
--
CREATE TABLE SystemLog (
    --- When this was logged.
    tsCreated           TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- The event type.
    -- This is a 8 character string identifier so that we don't need to change
    -- some enum type everytime we introduce a new event type.
    sEvent              CHAR(8)     NOT NULL,
    --- The log text.
    sLogText            text        NOT NULL,

    PRIMARY KEY (tsCreated, sEvent)
);


-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
--
--     C o n f i g u r a t i o n
--
-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

--- @table Users
-- Test manager users.
--
-- This is mainly for doing simple access checks before permitting access to
-- the test manager.  This needs to be coordinated with
-- apache/ldap/Oracle-Single-Sign-On.
--
-- The main purpose, though, is for tracing who changed the test config and
-- analysis data.
--
-- @remarks This table stores history.  Never update or delete anything.  The
--          equivalent of deleting is done by setting the 'tsExpire' field to
--          current_timestamp.
--
CREATE SEQUENCE UserIdSeq
    START 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;
CREATE TABLE Users (
    --- The user id.
    uid                 INTEGER     DEFAULT NEXTVAL('UserIdSeq')  NOT NULL,
    --- When this row starts taking effect (inclusive).
    tsEffective         TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- When this row stops being tsEffective (exclusive).
    tsExpire            TIMESTAMP WITH TIME ZONE  DEFAULT TIMESTAMP WITH TIME ZONE 'infinity'  NOT NULL,
    --- The user id of the one who created/modified this entry.
    -- Non-unique foreign key: Users(uid)
    uidAuthor           INTEGER     DEFAULT NULL,
    --- User name.
    sUsername           text        NOT NULL,
    --- The email address of the user.
    sEmail              text        NOT NULL,
    --- The full name.
    sFullName           text        NOT NULL,
    --- The login name used by apache.
    sLoginName          text        NOT NULL,
    --- Read access only.
    fReadOnly           BOOLEAN     NOT NULL DEFAULT FALSE,

    PRIMARY KEY (uid, tsExpire)
);
CREATE INDEX UsersLoginNameIdx ON Users (sLoginName, tsExpire DESC);


--- @table GlobalResources
-- Global resource configuration.
--
-- For example an iSCSI target.
--
-- @remarks This table stores history.  Never update or delete anything.  The
--          equivalent of deleting is done by setting the 'tsExpire' field to
--          current_timestamp.
--
CREATE SEQUENCE GlobalResourceIdSeq
    START 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;
CREATE TABLE GlobalResources (
    --- The global resource ID.
    -- This stays the same thru updates.
    idGlobalRsrc        INTEGER     DEFAULT NEXTVAL('GlobalResourceIdSeq')  NOT NULL,
    --- When this row starts taking effect (inclusive).
    tsEffective         TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- When this row stops being tsEffective (exclusive).
    tsExpire            TIMESTAMP WITH TIME ZONE  DEFAULT TIMESTAMP WITH TIME ZONE 'infinity'  NOT NULL,
    --- The user id of the one who created/modified this entry.
    -- Non-unique foreign key: Users(uid)
    uidAuthor           INTEGER     NOT NULL,
    --- The name of the resource.
    sName               text        NOT NULL,
    --- Optional resource description.
    sDescription        text,
    --- Indicates whether this resource is currently enabled (online).
    fEnabled            boolean     DEFAULT FALSE  NOT NULL,

    PRIMARY KEY (idGlobalRsrc, tsExpire)
);


--- @table BuildSources
-- Build sources.
--
-- This is used by a scheduling group to select builds and the default
-- Validation Kit from the Builds table.
--
-- @remarks This table stores history.  Never update or delete anything.  The
--          equivalent of deleting is done by setting the 'tsExpire' field to
--          current_timestamp.
--
-- @todo Any better way of representing this so we could more easily
--       join/whatever when searching for builds?
--
CREATE SEQUENCE BuildSourceIdSeq
    START 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;
CREATE TABLE BuildSources (
    --- The build source identifier.
    -- This stays constant over time.
    idBuildSrc          INTEGER     DEFAULT NEXTVAL('BuildSourceIdSeq')  NOT NULL,
    --- When this row starts taking effect (inclusive).
    tsEffective         TIMESTAMP WITH TIME ZONE    DEFAULT current_timestamp  NOT NULL,
    --- When this row stops being tsEffective (exclusive).
    tsExpire            TIMESTAMP WITH TIME ZONE    DEFAULT TIMESTAMP WITH TIME ZONE 'infinity'  NOT NULL,
    --- The user id of the one who created/modified this entry.
    -- Non-unique foreign key: Users(uid)
    uidAuthor           INTEGER     NOT NULL,

    --- The name of the build source.
    sName               TEXT        NOT NULL,
    --- Description.
    sDescription        TEXT        DEFAULT NULL,

    --- Which product.
    -- ASSUME that it is okay to limit a build source to a single product.
    sProduct            text        NOT NULL,
    --- Which branch.
    -- ASSUME that it is okay to limit a build source to a branch.
    sBranch             text        NOT NULL,

    --- Build types to include, all matches if NULL.
    -- @todo Weighting the types would be nice in a later version.
    asTypes             text ARRAY  DEFAULT NULL,
    --- Array of the 'sOs.sCpuArch' to match, all matches if NULL.
    -- See KBUILD_OSES in kBuild for a list of standard target OSes, and
    -- KBUILD_ARCHES for a list of standard architectures.
    --
    -- @remarks See marks on 'os-agnostic' and 'noarch' in BuildCategories.
    asOsArches          text ARRAY  DEFAULT NULL,

    --- The first subversion tree revision to match, no lower limit if NULL.
    iFirstRevision      INTEGER     DEFAULT NULL,
    --- The last subversion tree revision to match, no upper limit if NULL.
    iLastRevision       INTEGER     DEFAULT NULL,

    --- The maximum age of the builds in seconds, unlimited if NULL.
    cSecMaxAge          INTEGER     DEFAULT NULL,

    PRIMARY KEY (idBuildSrc, tsExpire)
);


--- @table TestCases
-- Test case configuration.
--
-- @remarks This table stores history.  Never update or delete anything.  The
--          equivalent of deleting is done by setting the 'tsExpire' field to
--          current_timestamp.
--
CREATE SEQUENCE TestCaseIdSeq
    START 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;
CREATE SEQUENCE TestCaseGenIdSeq
    START 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;
CREATE TABLE TestCases (
    --- The fixed test case ID.
    -- This is assigned when the test case is created and will never change.
    idTestCase          INTEGER     DEFAULT NEXTVAL('TestCaseIdSeq')  NOT NULL,
    --- When this row starts taking effect (inclusive).
    tsEffective         TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- When this row stops being tsEffective (exclusive).
    tsExpire            TIMESTAMP WITH TIME ZONE  DEFAULT TIMESTAMP WITH TIME ZONE 'infinity'  NOT NULL,
    --- The user id of the one who created/modified this entry.
    -- Non-unique foreign key: Users(uid)
    uidAuthor           INTEGER     NOT NULL,
    --- Generation ID for this row, a truly unique identifier.
    -- This is primarily for referencing by TestSets.
    idGenTestCase       INTEGER     UNIQUE DEFAULT NEXTVAL('TestCaseGenIdSeq')  NOT NULL,

    --- The name of the test case.
    sName               TEXT        NOT NULL,
    --- Optional test case description.
    sDescription        TEXT        DEFAULT NULL,
    --- Indicates whether this test case is currently enabled.
    fEnabled            BOOLEAN     DEFAULT FALSE  NOT NULL,
    --- Default test case timeout given in seconds.
    cSecTimeout         INTEGER     NOT NULL  CHECK (cSecTimeout > 0),
    --- Default TestBox requirement expression (python boolean expression).
    -- All the scheduler properties are available for use with the same names
    -- as in that table.
    -- If NULL everything matches.
    sTestBoxReqExpr     TEXT        DEFAULT NULL,
    --- Default build requirement expression (python boolean expression).
    -- The following build properties are available: sProduct, sBranch,
    -- sType, asOsArches, sVersion, iRevision, uidAuthor and idBuild.
    -- If NULL everything matches.
    sBuildReqExpr       TEXT        DEFAULT NULL,

    --- The base command.
    -- String suitable for executing in bourne shell with space as separator
    -- (IFS). References to @BUILD_BINARIES@ will be replaced WITH the content
    -- of the Builds(sBinaries) field.
    sBaseCmd            TEXT        NOT NULL,

    --- Comma separated list of test suite zips (or tars) that the testbox will
    -- need to download and expand prior to testing.
    -- If NULL the current test suite of the scheduling group will be used (the
    -- scheduling group will have an optional test suite build queue associated
    -- with it).  The current test suite can also be referenced by
    -- @VALIDATIONKIT_ZIP@ in case more downloads are required.  Files may also be
    -- uploaded to the test manager download area, in which case the
    -- @DOWNLOAD_BASE_URL@ prefix can be used to refer to this area.
    sTestSuiteZips      TEXT        DEFAULT NULL,

    -- Comment regarding a change or something.
    sComment            TEXT        DEFAULT NULL,

    PRIMARY KEY (idTestCase, tsExpire)
);


--- @table TestCaseArgs
-- Test case argument list variations.
--
-- For example, we have a test case that does a set of tests on a virtual
-- machine.  To get better code/feature coverage of this testcase we wish to
-- run it with different guest hardware configuration.  The test case may do
-- the same stuff, but the guest OS as well as the VMM may react differently to
-- the hardware configurations and uncover issues in the VMM, device emulation
-- or other places.
--
-- Typical hardware variations are:
--      - guest memory size (RAM),
--      - guest video memory size (VRAM),
--      - virtual CPUs / cores / threads,
--      - virtual chipset
--      - virtual network interface card (NIC)
--      - USB 1.1, USB 2.0, no USB
--
-- The TM web UI will help the user create a reasonable set of permutations
-- of these parameters, the user specifies a maximum and the TM uses certain
-- rules together with random selection to generate the desired number.  The
-- UI will also help suggest fitting testbox requirements according to the
-- RAM/VRAM sizes and the virtual CPU counts.  The user may then make
-- adjustments to the suggestions before commit them.
--
-- Alternatively, the user may also enter all the permutations without any
-- help from the UI.
--
-- Note! All test cases has at least one entry in this table, even if it is
-- empty, because testbox requirements are specified thru this.
--
-- Querying the valid parameter lists for a testase this way:
--      SELECT * ... WHERE idTestCase = TestCases.idTestCase
--                     AND tsExpire     > <when>
--                     AND tsEffective <= <when>;
--
-- Querying the valid parameter list for the latest generation can be
-- simplified by just checking tsExpire date:
--      SELECT * ... WHERE idTestCase = TestCases.idTestCase
--                     AND tsExpire    == TIMESTAMP WITH TIME ZONE 'infinity';
--
-- @remarks This table stores history.  Never update or delete anything.  The
--          equivalent of deleting is done by setting the 'tsExpire' field to
--          current_timestamp.
--
CREATE SEQUENCE TestCaseArgsIdSeq
    START 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;
CREATE SEQUENCE TestCaseArgsGenIdSeq
    START 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;
CREATE TABLE TestCaseArgs (
    --- The test case ID.
    -- Non-unique foreign key: TestCases(idTestCase).
    idTestCase          INTEGER     NOT NULL,
    --- The testcase argument variation ID (fixed).
    -- This is primarily for TestGroupMembers.aidTestCaseArgs.
    idTestCaseArgs      INTEGER     DEFAULT NEXTVAL('TestCaseArgsIdSeq')  NOT NULL,
    --- When this row starts taking effect (inclusive).
    tsEffective         TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- When this row stops being tsEffective (exclusive).
    tsExpire            TIMESTAMP WITH TIME ZONE  DEFAULT TIMESTAMP WITH TIME ZONE 'infinity'  NOT NULL,
    --- The user id of the one who created/modified this entry.
    -- Non-unique foreign key: Users(uid)
    uidAuthor           INTEGER     NOT NULL,
    --- Generation ID for this row.
    -- This is primarily for efficient referencing by TestSets and SchedQueues.
    idGenTestCaseArgs   INTEGER     UNIQUE DEFAULT NEXTVAL('TestCaseArgsGenIdSeq')  NOT NULL,

    --- The additional arguments.
    -- String suitable for bourne shell style argument parsing with space as
    -- separator (IFS).  References to @BUILD_BINARIES@ will be replaced with
    -- the content of the Builds(sBinaries) field.
    sArgs               TEXT        NOT NULL,
    --- Optional test case timeout given in seconds.
    -- If NULL, the TestCases.cSecTimeout field is used instead.
    cSecTimeout         INTEGER     DEFAULT NULL  CHECK (cSecTimeout IS NULL OR cSecTimeout > 0),
    --- Additional TestBox requirement expression (python boolean expression).
    -- All the scheduler properties are available for use with the same names
    -- as in that table.  This is checked after first checking the requirements
    -- in the TestCases.sTestBoxReqExpr field.
    sTestBoxReqExpr     TEXT        DEFAULT NULL,
    --- Additional build requirement expression (python boolean expression).
    -- The following build properties are available: sProduct, sBranch,
    -- sType, asOsArches, sVersion, iRevision, uidAuthor and idBuild. This is
    -- checked after first checking the requirements in the
    -- TestCases.sBuildReqExpr field.
    sBuildReqExpr       TEXT        DEFAULT NULL,
    --- Number of testboxes required (gang scheduling).
    cGangMembers        SMALLINT    DEFAULT 1  NOT NULL  CHECK (cGangMembers > 0 AND cGangMembers < 1024),
    --- Optional variation sub-name.
    sSubName            TEXT        DEFAULT NULL,

    --- The arguments are part of the primary key for several reasons.
    -- No duplicate argument lists (makes no sense - if you want to prioritize
    -- argument lists, we add that explicitly).  This may hopefully enable us
    -- to more easily check coverage later on, even when the test case is
    -- reconfigured with more/less permutations.
    PRIMARY KEY (idTestCase, tsExpire, sArgs)
);
CREATE INDEX TestCaseArgsLookupIdx ON TestCaseArgs (idTestCase, tsExpire DESC, tsEffective ASC);


--- @table TestCaseDeps
-- Test case dependencies (N:M)
--
-- This effect build selection.  The build must have passed all runs of the
-- given prerequisite testcase (idTestCasePreReq) and executed at a minimum one
-- argument list variation.
--
-- This should also affect scheduling order, if possible at least one
-- prerequisite testcase variation should be place before the specific testcase
-- in the scheduling queue.
--
-- @remarks This table stores history.  Never update or delete anything.  The
--          equivalent of deleting is done by setting the 'tsExpire' field to
--          current_timestamp.  To select the currently valid entries use
--          tsExpire = TIMESTAMP WITH TIME ZONE 'infinity'.
--
CREATE TABLE TestCaseDeps (
    --- The test case that depends on someone.
    -- Non-unique foreign key: TestCases(idTestCase).
    idTestCase          INTEGER     NOT NULL,
    --- The prerequisite test case ID.
    -- Non-unique foreign key: TestCases(idTestCase).
    idTestCasePreReq    INTEGER     NOT NULL,
    --- When this row starts taking effect (inclusive).
    tsEffective         TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- When this row stops being tsEffective (exclusive).
    tsExpire            TIMESTAMP WITH TIME ZONE  DEFAULT TIMESTAMP WITH TIME ZONE 'infinity'  NOT NULL,
    --- The user id of the one who created/modified this entry.
    -- Non-unique foreign key: Users(uid)
    uidAuthor           INTEGER     NOT NULL,

    PRIMARY KEY (idTestCase, idTestCasePreReq, tsExpire)
);


--- @table TestCaseGlobalRsrcDeps
-- Test case dependencies on global resources (N:M)
--
-- @remarks This table stores history.  Never update or delete anything.  The
--          equivalent of deleting is done by setting the 'tsExpire' field to
--          current_timestamp.  To select the currently valid entries use
--          tsExpire = TIMESTAMP WITH TIME ZONE 'infinity'.
--
CREATE TABLE TestCaseGlobalRsrcDeps (
    --- The test case that depends on someone.
    -- Non-unique foreign key: TestCases(idTestCase).
    idTestCase          INTEGER     NOT NULL,
    --- The prerequisite resource ID.
    -- Non-unique foreign key: GlobalResources(idGlobalRsrc).
    idGlobalRsrc        INTEGER     NOT NULL,
    --- When this row starts taking effect (inclusive).
    tsEffective         TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- When this row stops being tsEffective (exclusive).
    tsExpire            TIMESTAMP WITH TIME ZONE  DEFAULT TIMESTAMP WITH TIME ZONE 'infinity'  NOT NULL,
    --- The user id of the one who created/modified this entry.
    -- Non-unique foreign key: Users(uid)
    uidAuthor           INTEGER     NOT NULL,

    PRIMARY KEY (idTestCase, idGlobalRsrc, tsExpire)
);


--- @table TestGroups
-- Test Group - A collection of test cases.
--
-- This is for simplifying test configuration by working with a few groups
-- instead of a herd of individual testcases.  It may also be used for creating
-- test suites for certain areas (like guest additions) or tasks (like
-- performance measurements).
--
-- A test case can be member of any number of test groups.
--
-- @remarks This table stores history.  Never update or delete anything.  The
--          equivalent of deleting is done by setting the 'tsExpire' field to
--          current_timestamp.  To select the currently valid entries use
--          tsExpire = TIMESTAMP WITH TIME ZONE 'infinity'.
--
CREATE SEQUENCE TestGroupIdSeq
    START 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;
CREATE TABLE TestGroups (
    --- The fixed scheduling group ID.
    -- This is assigned when the group is created and will never change.
    idTestGroup         INTEGER     DEFAULT NEXTVAL('TestGroupIdSeq')  NOT NULL,
    --- When this row starts taking effect (inclusive).
    tsEffective         TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- When this row stops being tsEffective (exclusive).
    tsExpire            TIMESTAMP WITH TIME ZONE  DEFAULT TIMESTAMP WITH TIME ZONE 'infinity'  NOT NULL,
    --- The user id of the one who created/modified this entry.
    -- Non-unique foreign key: Users(uid)
    uidAuthor           INTEGER     NOT NULL,

    --- The name of the scheduling group.
    sName               TEXT        NOT NULL,
    --- Optional group description.
    sDescription        TEXT,
    -- Comment regarding a change or something.
    sComment            TEXT        DEFAULT NULL,

    PRIMARY KEY (idTestGroup, tsExpire)
);
CREATE INDEX TestGroups_id_index ON TestGroups (idTestGroup, tsExpire DESC, tsEffective ASC);


--- @table TestGroupMembers
-- The N:M relationship between test case configurations and test groups.
--
-- @remarks This table stores history.  Never update or delete anything.  The
--          equivalent of deleting is done by setting the 'tsExpire' field to
--          current_timestamp.  To select the currently valid entries use
--          tsExpire = TIMESTAMP WITH TIME ZONE 'infinity'.
--
CREATE TABLE TestGroupMembers (
    --- The group ID.
    -- Non-unique foreign key: TestGroups(idTestGroup).
    idTestGroup         INTEGER     NOT NULL,
    --- The test case ID.
    -- Non-unique foreign key: TestCases(idTestCase).
    idTestCase          INTEGER     NOT NULL,
    --- When this row starts taking effect (inclusive).
    tsEffective         TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- When this row stops being tsEffective (exclusive).
    tsExpire            TIMESTAMP WITH TIME ZONE  DEFAULT TIMESTAMP WITH TIME ZONE 'infinity'  NOT NULL,
    --- The user id of the one who created/modified this entry.
    -- Non-unique foreign key: Users(uid)
    uidAuthor           INTEGER     NOT NULL,

    --- Test case scheduling priority.
    -- Higher number causes the test case to be run more frequently.
    -- @sa SchedGroupMembers.iSchedPriority, TestBoxesInSchedGroups.iSchedPriority
    -- @todo Not sure we want to keep this...
    iSchedPriority      INTEGER     DEFAULT 16  CHECK (iSchedPriority >= 0 AND iSchedPriority < 32)  NOT NULL,

    --- Limit the memberships to the given argument variations.
    -- Non-unique foreign key: TestCaseArgs(idTestCase, idTestCaseArgs).
    aidTestCaseArgs     INTEGER ARRAY  DEFAULT NULL,

    PRIMARY KEY (idTestGroup, idTestCase, tsExpire)
);


--- @table SchedGroups
-- Scheduling group (aka. testbox partitioning) configuration.
--
-- A testbox is associated with exactly one scheduling group.  This association
-- can be changed, of course.  If we (want to) retire a group which still has
-- testboxes associated with it, these will be moved to the 'default' group.
--
-- The TM web UI will make sure that a testbox is always in a group and that
-- the default group cannot be deleted.
--
-- A scheduling group combines several things:
--      - A selection of builds to test (via idBuildSrc).
--      - A collection of test groups to test with (via SchedGroupMembers).
--      - A set of testboxes to test on (via TestBoxes.idSchedGroup).
--
-- In additions there is an optional source of fresh test suite builds (think
-- VBoxTestSuite) as well as scheduling options.
--
-- @remarks This table stores history.  Never update or delete anything.  The
--          equivalent of deleting is done by setting the 'tsExpire' field to
--          current_timestamp.  To select the currently valid entries use
--          tsExpire = TIMESTAMP WITH TIME ZONE 'infinity'.
--
CREATE TYPE Scheduler_T AS ENUM (
    'bestEffortContinousItegration',
    'reserved'
);
CREATE SEQUENCE SchedGroupIdSeq
    START 2
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;
CREATE TABLE SchedGroups (
    --- The fixed scheduling group ID.
    -- This is assigned when the group is created and will never change.
    idSchedGroup        INTEGER     DEFAULT NEXTVAL('SchedGroupIdSeq')  NOT NULL,
    --- When this row starts taking effect (inclusive).
    tsEffective         TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- When this row stops being tsEffective (exclusive).
    tsExpire            TIMESTAMP WITH TIME ZONE  DEFAULT TIMESTAMP WITH TIME ZONE 'infinity'  NOT NULL,
    --- The user id of the one who created/modified this entry.
    -- Non-unique foreign key: Users(uid)
    -- @note This is NULL for the default group.
    uidAuthor           INTEGER     DEFAULT NULL,

    --- The name of the scheduling group.
    sName               TEXT        NOT NULL,
    --- Optional group description.
    sDescription        TEXT,
    --- Indicates whether this group is currently enabled.
    fEnabled            boolean     NOT NULL,
    --- The scheduler to use.
    -- This is for when we later desire different scheduling that the best
    -- effort stuff provided by the initial implementation.
    enmScheduler        Scheduler_T DEFAULT 'bestEffortContinousItegration'::Scheduler_T  NOT NULL,
    --- The build source.
    -- Non-unique foreign key: BuildSources(idBuildSrc)
    idBuildSrc          INTEGER     DEFAULT NULL,
    --- The Validation Kit build source (@VALIDATIONKIT_ZIP@).
    -- Non-unique foreign key: BuildSources(idBuildSrc)
    idBuildSrcTestSuite INTEGER     DEFAULT NULL,
    -- Comment regarding a change or something.
    sComment            TEXT        DEFAULT NULL,

    PRIMARY KEY (idSchedGroup, tsExpire)
);

-- Special default group.
INSERT INTO SchedGroups (idSchedGroup, tsEffective, tsExpire, sName, sDescription, fEnabled)
    VALUES (1, TIMESTAMP WITH TIME ZONE 'epoch', TIMESTAMP WITH TIME ZONE 'infinity', 'default', 'default group', FALSE);


--- @table SchedGroupMembers
-- N:M relationship between scheduling groups and test groups.
--
-- Several scheduling parameters are associated with this relationship.
--
-- The test group dependency (idTestGroupPreReq) can be used in the same way as
-- TestCaseDeps.idTestCasePreReq, only here on test group level.  This means it
-- affects the build selection.  The builds needs to have passed all test runs
-- the prerequisite test group and done at least one argument variation of each
-- test case in it.
--
-- @remarks This table stores history.  Never update or delete anything.  The
--          equivalent of deleting is done by setting the 'tsExpire' field to
--          current_timestamp.  To select the currently valid entries use
--          tsExpire = TIMESTAMP WITH TIME ZONE 'infinity'.
--
CREATE TABLE SchedGroupMembers (
    --- Scheduling ID.
    -- Non-unique foreign key: SchedGroups(idSchedGroup).
    idSchedGroup        INTEGER     NOT NULL,
    --- Testgroup ID.
    -- Non-unique foreign key: TestGroups(idTestGroup).
    idTestGroup         INTEGER     NOT NULL,
    --- When this row starts taking effect (inclusive).
    tsEffective         TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- When this row stops being tsEffective (exclusive).
    tsExpire            TIMESTAMP WITH TIME ZONE  DEFAULT TIMESTAMP WITH TIME ZONE 'infinity'  NOT NULL,
    --- The user id of the one who created/modified this entry.
    -- Non-unique foreign key: Users(uid)
    uidAuthor           INTEGER     NOT NULL,

    --- The scheduling priority of the test group.
    -- Higher number causes the test case to be run more frequently.
    -- @sa TestGroupMembers.iSchedPriority, TestBoxesInSchedGroups.iSchedPriority
    iSchedPriority      INTEGER     DEFAULT 16 CHECK (iSchedPriority >= 0 AND iSchedPriority < 32)  NOT NULL,
    --- When during the week this group is allowed to start running, NULL means
    -- there are no constraints.
    -- Each bit in the bitstring represents one hour, with bit 0 indicating the
    -- midnight hour on a monday.
    bmHourlySchedule    bit(168)    DEFAULT NULL,
    --- Optional test group dependency.
    -- Non-unique foreign key: TestGroups(idTestGroup).
    -- This is for requiring that a build has been subject to smoke tests
    -- before bothering to subject it to longer tests.
    -- @todo Not entirely sure this should be here, but I'm not so keen on yet
    --       another table as the only use case is smoketests.
    idTestGroupPreReq   INTEGER     DEFAULT NULL,

    PRIMARY KEY (idSchedGroup, idTestGroup, tsExpire)
);


--- @table TestBoxStrTab
-- String table for the test boxes.
--
-- This is a string cache for all string members in TestBoxes except the name.
-- The rational is to avoid duplicating large strings like sReport when the
-- testbox reports a new cMbScratch value or the box when the test sheriff
-- sends a reboot command or similar.
--
-- At the time this table was introduced, we had 400558 TestBoxes rows,  where
-- the SUM(LENGTH(sReport)) was 993MB.  There were really just 1066 distinct
-- sReport values, with a total length of 0x3 MB.
--
-- Nothing is ever deleted from this table.
--
-- @note Should use a stored procedure to query/insert a string.
--
--
-- TestBox stats prior to conversion:
--      SELECT COUNT(*) FROM TestBoxes:                     400558 rows
--      SELECT pg_total_relation_size('TestBoxes'):      740794368 bytes (706 MB)
--      Average row cost:           740794368 / 400558 =      1849 bytes/row
--
-- After conversion:
--      SELECT COUNT(*) FROM TestBoxes:                     400558 rows
--      SELECT pg_total_relation_size('TestBoxes'):      144375808 bytes (138 MB)
--      SELECT COUNT(idStr) FROM TestBoxStrTab:               1292 rows
--      SELECT pg_total_relation_size('TestBoxStrTab'):    5709824 bytes (5.5 MB)
--                   (144375808 + 5709824) / 740794368 =        20 %
--      Average row cost boxes:     144375808 / 400558 =       360 bytes/row
--      Average row cost strings:       5709824 / 1292 =      4420 bytes/row
--
CREATE SEQUENCE TestBoxStrTabIdSeq
    START 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;
CREATE TABLE TestBoxStrTab (
    --- The ID of this string.
    idStr               INTEGER     PRIMARY KEY DEFAULT NEXTVAL('TestBoxStrTabIdSeq'),
    --- The string value.
    sValue              text        NOT NULL,
    --- Creation time stamp.
    tsCreated           TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL
);
-- Note! Must use hash index as the sReport strings are too long for regular indexing.
CREATE INDEX TestBoxStrTabNameIdx ON TestBoxStrTab USING hash (sValue);

--- Empty string with ID 0.
INSERT INTO TestBoxStrTab (idStr, sValue) VALUES (0, '');


--- @type TestBoxCmd_T
-- Testbox commands.
CREATE TYPE TestBoxCmd_T AS ENUM (
    'none',
    'abort',
    'reboot',                   --< This implies abort. Status changes when reaching 'idle'.
    'upgrade',                  --< This is only handled when asking for work.
    'upgrade-and-reboot',       --< Ditto.
    'special'                   --< Similar to upgrade, reserved for the future.
);


--- @type LomKind_T
-- The kind of lights out management on a testbox.
CREATE TYPE LomKind_T AS ENUM (
    'none',
    'ilom',
    'elom',
    'apple-xserve-lom'
);


--- @table TestBoxes
-- Testbox configurations.
--
-- The testboxes are identified by IP and the system UUID if available. Should
-- the IP change, the testbox will be refused at sign on and the testbox
-- sheriff will have to update it's IP.
--
-- @todo Implement the UUID stuff. Get it from DMI, UEFI or whereever.
--       Mismatching needs to be logged somewhere...
--
-- To query the currently valid configuration:
--     SELECT ... WHERE id = idTestBox AND tsExpire = TIMESTAMP WITH TIME ZONE 'infinity';
--
-- @remarks This table stores history.  Never update or delete anything.  The
--          equivalent of deleting is done by setting the 'tsExpire' field to
--          current_timestamp.  To select the currently valid entries use
--          tsExpire = TIMESTAMP WITH TIME ZONE 'infinity'.
--
CREATE SEQUENCE TestBoxIdSeq
    START 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;
CREATE SEQUENCE TestBoxGenIdSeq
    START 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;
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
CREATE UNIQUE INDEX TestBoxesUuidIdx ON TestBoxes (uuidSystem, tsExpire DESC);
CREATE INDEX TestBoxesExpireEffectiveIdx ON TestBoxes (tsExpire DESC, tsEffective ASC);


--
-- Create a view for TestBoxes where the strings are resolved.
--
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


--- @table TestBoxesInSchedGroups
-- N:M relationship between test boxes and scheduling groups.
--
-- We associate a priority with this relationship.
--
-- @remarks This table stores history.  Never update or delete anything.  The
--          equivalent of deleting is done by setting the 'tsExpire' field to
--          current_timestamp.  To select the currently valid entries use
--          tsExpire = TIMESTAMP WITH TIME ZONE 'infinity'.
--
CREATE TABLE TestBoxesInSchedGroups (
    --- TestBox ID.
    -- Non-unique foreign key: TestBoxes(idTestBox).
    idTestBox           INTEGER     NOT NULL,
    --- Scheduling ID.
    -- Non-unique foreign key: SchedGroups(idSchedGroup).
    idSchedGroup        INTEGER     NOT NULL,
    --- When this row starts taking effect (inclusive).
    tsEffective         TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- When this row stops being tsEffective (exclusive).
    tsExpire            TIMESTAMP WITH TIME ZONE  DEFAULT TIMESTAMP WITH TIME ZONE 'infinity'  NOT NULL,
    --- The user id of the one who created/modified this entry.
    -- Non-unique foreign key: Users(uid)
    uidAuthor           INTEGER     NOT NULL,

    --- The scheduling priority of the scheduling group for the test box.
    -- Higher number causes the scheduling group to be serviced more frequently.
    -- @sa TestGroupMembers.iSchedPriority, SchedGroups.iSchedPriority
    iSchedPriority      INTEGER     DEFAULT 16 CHECK (iSchedPriority >= 0 AND iSchedPriority < 32)  NOT NULL,

    PRIMARY KEY (idTestBox, idSchedGroup, tsExpire)
);


-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
--
--     F a i l u r e   T r a c k i n g
--
-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --


--- @table FailureCategories
-- Failure categories.
--
-- This is for organizing the failure reasons.
--
-- @remarks This table stores history.  Never update or delete anything.  The
--          equivalent of deleting is done by setting the 'tsExpire' field to
--          current_timestamp.  To select the currently valid entries use
--          tsExpire = TIMESTAMP WITH TIME ZONE 'infinity'.
--
CREATE SEQUENCE FailureCategoryIdSeq
    START 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;
CREATE TABLE FailureCategories (
    --- The identifier of this failure category (once assigned, it will never change).
    idFailureCategory   INTEGER     DEFAULT NEXTVAL('FailureCategoryIdSeq')  NOT NULL,
    --- When this row starts taking effect (inclusive).
    tsEffective         TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- When this row stops being tsEffective (exclusive).
    tsExpire            TIMESTAMP WITH TIME ZONE  DEFAULT TIMESTAMP WITH TIME ZONE 'infinity'  NOT NULL,
    --- The user id of the one who created/modified this entry.
    -- Non-unique foreign key: Users(uid)
    uidAuthor           INTEGER     NOT NULL,
    --- The short category description.
    -- For combo boxes and other selection lists.
    sShort              text        NOT NULL,
    --- Full description
    -- For cursor-over-poppups for instance.
    sFull               text        NOT NULL,

    PRIMARY KEY (idFailureCategory, tsExpire)
);


--- @table FailureReasons
-- Failure reasons.
--
-- When analysing a test failure, the testbox sheriff will try assign a fitting
-- reason for the failure.  This table is here to help the sheriff in his/hers
-- job as well as developers looking checking if their changes affected the
-- test results in any way.
--
-- @remarks This table stores history.  Never update or delete anything.  The
--          equivalent of deleting is done by setting the 'tsExpire' field to
--          current_timestamp.  To select the currently valid entries use
--          tsExpire = TIMESTAMP WITH TIME ZONE 'infinity'.
--
CREATE SEQUENCE FailureReasonIdSeq
    START 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;
CREATE TABLE FailureReasons (
    --- The identifier of this failure reason (once assigned, it will never change).
    idFailureReason     INTEGER     DEFAULT NEXTVAL('FailureReasonIdSeq')  NOT NULL,
    --- When this row starts taking effect (inclusive).
    tsEffective         TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- When this row stops being tsEffective (exclusive).
    tsExpire            TIMESTAMP WITH TIME ZONE  DEFAULT TIMESTAMP WITH TIME ZONE 'infinity'  NOT NULL,
    --- The user id of the one who created/modified this entry.
    -- Non-unique foreign key: Users(uid)
    uidAuthor           INTEGER     NOT NULL,

    --- The failure category this reason belongs to.
    -- Non-unique foreign key: FailureCategories(idFailureCategory)
    idFailureCategory   INTEGER     NOT NULL,
    --- The short failure description.
    -- For combo boxes and other selection lists.
    sShort              text        NOT NULL,
    --- Full failure description.
    sFull               text        NOT NULL,
    --- Ticket number in the primary bugtracker.
    iTicket             INTEGER     DEFAULT NULL,
    --- Other URLs to reports or discussions of the observed symptoms.
    asUrls              text ARRAY  DEFAULT NULL,

    PRIMARY KEY (idFailureReason, tsExpire)
);
CREATE INDEX FailureReasonsCategoryIdx ON FailureReasons (idFailureCategory, idFailureReason);



--- @table TestResultFailures
-- This is for tracking/discussing test result failures.
--
-- The rational for putting this is a separate table is that we need history on
-- this while TestResults does not.
--
-- @remarks This table stores history.  Never update or delete anything.  The
--          equivalent of deleting is done by setting the 'tsExpire' field to
--          current_timestamp.  To select the currently valid entries use
--          tsExpire = TIMESTAMP WITH TIME ZONE 'infinity'.
--
CREATE TABLE TestResultFailures (
    --- The test result we're disucssing.
    -- @note The foreign key is declared after TestResults (further down).
    idTestResult        INTEGER     NOT NULL,
    --- When this row starts taking effect (inclusive).
    tsEffective         TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- When this row stops being tsEffective (exclusive).
    tsExpire            TIMESTAMP WITH TIME ZONE  DEFAULT TIMESTAMP WITH TIME ZONE 'infinity'  NOT NULL,
    --- The user id of the one who created/modified this entry.
    -- Non-unique foreign key: Users(uid)
    uidAuthor           INTEGER     NOT NULL,
    --- The testsest this result is a part of.
    -- This is mainly an aid for bypassing the enormous TestResults table.
    -- Note! This is a foreign key, but we have to add it after TestSets has
    --       been created, see further down.
    idTestSet           INTEGER     NOT NULL,

    --- The suggested failure reason.
    -- Non-unique foreign key: FailureReasons(idFailureReason)
    idFailureReason     INTEGER     NOT NULL,
    --- Optional comment.
    sComment            text        DEFAULT NULL,

    PRIMARY KEY (idTestResult, tsExpire)
);
CREATE INDEX TestResultFailureIdx  ON TestResultFailures (idTestSet, tsExpire DESC, tsEffective ASC);
CREATE INDEX TestResultFailureIdx2 ON TestResultFailures (idTestResult, tsExpire DESC, tsEffective ASC);
CREATE INDEX TestResultFailureIdx3 ON TestResultFailures (idFailureReason, idTestResult, tsExpire DESC, tsEffective ASC);




-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
--
--     T e s t   I n p u t
--
-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --


--- @table BuildBlacklist
-- Table used to blacklist sets of builds.
--
-- The best usage example is a VMM developer realizing that a change causes the
-- host to panic, hang, or otherwise misbehave.  To prevent the testbox sheriff
-- from repeatedly having to reboot testboxes, the builds gets blacklisted
-- until there is a working build again.  This may mean adding an open ended
-- blacklist spec and then updating it with the final revision number once the
-- fix has been committed.
--
-- @remarks This table stores history.  Never update or delete anything.  The
--          equivalent of deleting is done by setting the 'tsExpire' field to
--          current_timestamp.  To select the currently valid entries use
--          tsExpire = TIMESTAMP WITH TIME ZONE 'infinity'.
--
-- @todo Would be nice if we could replace the text strings below with a set of
--       BuildCategories, or sore it in any other way which would enable us to
--       do a negative join with build category...  The way it is specified
--       now, it looks like we have to open a cursor of prospecitve builds and
--       filter then thru this table one by one.
--
--       Any better representation is welcome, but this is low prioirty for
--       now, as it's relatively easy to change this later one.
--
CREATE SEQUENCE BuildBlacklistIdSeq
    START 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;
CREATE TABLE BuildBlacklist (
    --- The blacklist entry id.
    -- This stays constant over time.
    idBlacklisting      INTEGER     DEFAULT NEXTVAL('BuildBlacklistIdSeq')  NOT NULL,
    --- When this row starts taking effect (inclusive).
    tsEffective         TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- When this row stops being tsEffective (exclusive).
    tsExpire            TIMESTAMP WITH TIME ZONE  DEFAULT TIMESTAMP WITH TIME ZONE 'infinity'  NOT NULL,
    --- The user id of the one who created/modified this entry.
    -- Non-unique foreign key: Users(uid)
    uidAuthor           INTEGER     NOT NULL,

    --- The reason for the blacklisting.
    -- Non-unique foreign key: FailureReasons(idFailureReason)
    idFailureReason     INTEGER     NOT NULL,

    --- Which product.
    -- ASSUME that it is okay to limit a blacklisting to a single product.
    sProduct            text        NOT NULL,
    --- Which branch.
    -- ASSUME that it is okay to limit a blacklisting to a branch.
    sBranch             text        NOT NULL,

    --- Build types to include, all matches if NULL.
    asTypes             text ARRAY  DEFAULT NULL,
    --- Array of the 'sOs.sCpuArch' to match, all matches if NULL.
    -- See KBUILD_OSES in kBuild for a list of standard target OSes, and
    -- KBUILD_ARCHES for a list of standard architectures.
    --
    -- @remarks See marks on 'os-agnostic' and 'noarch' in BuildCategories.
    asOsArches          text ARRAY  DEFAULT NULL,

    --- The first subversion tree revision to blacklist.
    iFirstRevision      INTEGER     NOT NULL,
    --- The last subversion tree revision to blacklist, no upper limit if NULL.
    iLastRevision       INTEGER     NOT NULL,

    PRIMARY KEY (idBlacklisting, tsExpire)
);
CREATE INDEX BuildBlacklistIdx ON BuildBlacklist (iLastRevision DESC, iFirstRevision ASC, sProduct, sBranch,
                                                  tsExpire DESC, tsEffective ASC);

--- @table BuildCategories
-- Build categories.
--
-- The purpose of this table is saving space in the Builds table and hopefully
-- speed things up when selecting builds as well (compared to selecting on 4
-- text fields in the much larger Builds table).
--
-- Insert only table, no update, no delete.  History is not needed.
--
CREATE SEQUENCE BuildCategoryIdSeq
    START 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;
CREATE TABLE BuildCategories (
    --- The build type identifier.
    idBuildCategory     INTEGER     PRIMARY KEY DEFAULT NEXTVAL('BuildCategoryIdSeq')  NOT NULL,
    --- Product.
    -- The product name.  For instance 'VBox' or 'VBoxTestSuite'.
    sProduct            TEXT        NOT NULL,
    --- The version control repository name.
    sRepository         TEXT        NOT NULL,
    --- The branch name (in the version control system).
    sBranch             TEXT        NOT NULL,
    --- The build type.
    -- See KBUILD_BLD_TYPES in kBuild for a list of standard build types.
    sType               TEXT        NOT NULL,
    --- Array of the 'sOs.sCpuArch' supported by the build.
    -- See KBUILD_OSES in kBuild for a list of standard target OSes, and
    -- KBUILD_ARCHES for a list of standard architectures.
    --
    -- @remarks 'os-agnostic' is used if the build doesn't really target any
    --          specific OS or if it targets all applicable OSes.
    --          'noarch' is used if the build is architecture independent or if
    --          all applicable architectures are handled.
    --          Thus, 'os-agnostic.noarch' will run on all build boxes.
    --
    -- @note    The array shall be sorted ascendingly to prevent unnecessary duplicates!
    --
    asOsArches          TEXT ARRAY  NOT NULL,

    UNIQUE (sProduct, sRepository, sBranch, sType, asOsArches)
);


--- @table Builds
-- The builds table contains builds from the tinderboxes and oaccasionally from
-- developers.
--
-- The tinderbox side could be fed by a batch job enumerating the build output
-- directories every so often, looking for new builds.  Or we could query them
-- from the tinderbox database.  Yet another alternative is making the
-- tinderbox server or client side software inform us about all new builds.
--
-- The developer builds are entered manually thru the TM web UI.  They are used
-- for subjecting new code to some larger scale testing before commiting,
-- enabling, or merging a private branch.
--
-- The builds are being selected from this table by the via the build source
-- specification that SchedGroups.idBuildSrc and
-- SchedGroups.idBuildSrcTestSuite links to.
--
-- @remarks This table stores history.  Never update or delete anything.  The
--          equivalent of deleting is done by setting the 'tsExpire' field to
--          current_timestamp.  To select the currently valid entries use
--          tsExpire = TIMESTAMP WITH TIME ZONE 'infinity'.
--
CREATE SEQUENCE BuildIdSeq
    START 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;
CREATE TABLE Builds (
    --- The build identifier.
    -- This remains unchanged
    idBuild             INTEGER     DEFAULT NEXTVAL('BuildIdSeq')  NOT NULL,
    --- When this build was created or entered into the database.
    -- This remains unchanged
    tsCreated           TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- When this row starts taking effect (inclusive).
    tsEffective         TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- When this row stops being tsEffective (exclusive).
    tsExpire            TIMESTAMP WITH TIME ZONE  DEFAULT TIMESTAMP WITH TIME ZONE 'infinity'  NOT NULL,
    --- The user id of the one who created/modified this entry.
    -- Non-unique foreign key: Users(uid)
    -- @note This is NULL if added by a batch job / tinderbox.
    uidAuthor           INTEGER     DEFAULT NULL,
    --- The build category.
    idBuildCategory     INTEGER     REFERENCES BuildCategories(idBuildCategory)  NOT NULL,
    --- The subversion tree revision of the build.
    iRevision           INTEGER     NOT NULL,
    --- The product version number (suitable for RTStrVersionCompare).
    sVersion            TEXT        NOT NULL,
    --- The link to the tinderbox log of this build.
    sLogUrl             TEXT,
    --- Comma separated list of binaries.
    -- The binaries have paths relative to the TESTBOX_PATH_BUILDS or full URLs.
    sBinaries           TEXT        NOT NULL,
    --- Set when the binaries gets deleted by the build quota script.
    fBinariesDeleted    BOOLEAN     DEFAULT FALSE  NOT NULL,

    UNIQUE (idBuild, tsExpire)
);
CREATE INDEX BuildsLookupIdx ON Builds (idBuildCategory, iRevision);


--- @table VcsRevisions
-- This table is for translating build revisions into commit details.
--
-- For graphs and test results, it would be useful to translate revisions into
-- dates and maybe provide commit message and the committer.
--
-- Data is entered exclusively thru one or more batch jobs, so no internal
-- authorship needed.  Also, since we're mirroring data from external sources
-- here, the batch job is allowed to update/replace existing records.
--
-- @todo We we could collect more info from the version control systems, if we
--       believe it's useful and can be presented in a reasonable manner.
--       Getting a list of affected files would be simple (requires
--       a separate table with a M:1 relationship to this table), or try
--       associate a commit to a branch.
--
CREATE TABLE VcsRevisions (
    --- The version control tree name.
    sRepository         TEXT        NOT NULL,
    --- The version control tree revision number.
    iRevision           INTEGER     NOT NULL,
    --- When the revision was created (committed).
    tsCreated           TIMESTAMP WITH TIME ZONE  NOT NULL,
    --- The name of the committer.
    -- @note Not to be confused with uidAuthor and test manager users.
    sAuthor             TEXT,
    --- The commit message.
    sMessage            TEXT,

    UNIQUE (sRepository, iRevision)
);
CREATE INDEX VcsRevisionsByDate ON VcsRevisions (tsCreated DESC);


--- @table VcsBugReferences
-- This is for relating commits to a bug and vice versa.
--
-- This feature isn't so much for the test manager as a cheap way of extending
-- bug trackers without VCS integration.  We just need to parse the commit
-- messages when inserting them into the VcsRevisions table.
--
-- Same input, updating and history considerations as VcsRevisions.
--
CREATE TABLE VcsBugReferences (
    --- The version control tree name.
    sRepository         TEXT        NOT NULL,
    --- The version control tree revision number.
    iRevision           INTEGER     NOT NULL,
    --- The bug tracker identifier - see g_kdBugTrackers in config.py.
    sBugTracker         CHAR(4)     NOT NULL,
    --- The bug number in the bug tracker.
    lBugNo              BIGINT      NOT NULL,

    UNIQUE (sRepository, iRevision, sBugTracker, lBugNo)
);
CREATE INDEX VcsBugReferencesLookupIdx ON VcsBugReferences (sBugTracker, lBugNo);




-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
--
--     T e s t   R e s u l t s
--
-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --


--- @table TestResultStrTab
-- String table for the test results.
--
-- This is a string cache for value names, test names and possible more, that
-- is frequently repated in the test results record for each test run.  The
-- purpose is not only to save space, but to make datamining queries faster by
-- giving them integer fields to work on instead of text fields.  There may
-- possibly be some benefits on INSERT as well as there are only integer
-- indexes.
--
-- Nothing is ever deleted from this table.
--
-- @note Should use a stored procedure to query/insert a string.
--
CREATE SEQUENCE TestResultStrTabIdSeq
    START 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;
CREATE TABLE TestResultStrTab (
    --- The ID of this string.
    idStr               INTEGER     PRIMARY KEY DEFAULT NEXTVAL('TestResultStrTabIdSeq'),
    --- The string value.
    sValue              text        NOT NULL,
    --- Creation time stamp.
    tsCreated           TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL
);
CREATE UNIQUE INDEX TestResultStrTabNameIdx ON TestResultStrTab (sValue);

--- Empty string with ID 0.
INSERT INTO TestResultStrTab (idStr, sValue) VALUES (0, '');


--- @type TestStatus_T
-- The status of a test (set / result).
--
CREATE TYPE TestStatus_T AS ENUM (
    -- Initial status:
    'running',
    -- Final statuses:
    'success',
    -- Final status: Test didn't fail as such, it was something else.
    'skipped',
    'bad-testbox',
    'aborted',
    -- Final status: Test failed.
    'failure',
    'timed-out',
    'rebooted'
);


--- @table TestResults
-- Test results - a recursive bundle of joy!
--
-- A test case will be created when the testdriver calls reporter.testStart and
-- concluded with reporter.testDone.  The testdriver (or it subordinates) can
-- use these methods to create nested test results.  For IPRT based test cases,
-- RTTestCreate, RTTestInitAndCreate and RTTestSub will both create new test
-- result records, where as RTTestSubDone, RTTestSummaryAndDestroy and
-- RTTestDestroy will conclude records.
--
-- By concluding is meant updating the status.  When the test driver reports
-- success, we check it against reported results. (paranoia strikes again!)
--
-- Nothing is ever deleted from this table.
--
-- @note    As seen below, several other tables associate data with a
--          test result, and the top most test result is referenced by the
--          test set.
--
CREATE SEQUENCE TestResultIdSeq
    START 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;
CREATE TABLE TestResults (
    --- The ID of this test result.
    idTestResult        INTEGER     PRIMARY KEY DEFAULT NEXTVAL('TestResultIdSeq'),
    --- The parent test result.
    -- This is NULL for the top test result.
    idTestResultParent  INTEGER     REFERENCES TestResults(idTestResult),
    --- The test set this result is a part of.
    -- Note! This is a foreign key, but we have to add it after TestSets has
    --       been created, see further down.
    idTestSet           INTEGER     NOT NULL,
    --- Creation time stamp.  This may also be the timestamp of when the test started.
    tsCreated           TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- The elapsed time for this test.
    -- This is either reported by the directly (with some sanity checking) or
    -- calculated (current_timestamp - created_ts).
    -- @todo maybe use a nanosecond field here, check with what
    tsElapsed           interval    DEFAULT NULL,
    --- The test name.
    idStrName           INTEGER     REFERENCES TestResultStrTab(idStr)  NOT NULL,
    --- The error count.
    cErrors             INTEGER     DEFAULT 0  NOT NULL,
    --- The test status.
    enmStatus           TestStatus_T DEFAULT 'running'::TestStatus_T  NOT NULL,
    --- Nesting depth.
    iNestingDepth       smallint    NOT NULL CHECK (iNestingDepth >= 0 AND iNestingDepth < 16),
    -- Make sure errors and status match up.
    CONSTRAINT CheckStatusMatchesErrors
        CHECK (   (cErrors > 0 AND enmStatus IN ('running'::TestStatus_T,
                                                 'failure'::TestStatus_T, 'timed-out'::TestStatus_T, 'rebooted'::TestStatus_T ))
               OR (cErrors = 0 AND enmStatus IN ('running'::TestStatus_T, 'success'::TestStatus_T,
                                                 'skipped'::TestStatus_T, 'aborted'::TestStatus_T, 'bad-testbox'::TestStatus_T))
              ),
    -- The following is for the TestResultFailures foreign key.
    -- Note! This was added with the name TestResults_idTestResult_idTestSet_key in the tmdb-r16 update script.
    UNIQUE (idTestResult, idTestSet)
);

CREATE INDEX TestResultsSetIdx ON TestResults (idTestSet, idStrName, idTestResult);
CREATE INDEX TestResultsParentIdx ON TestResults (idTestResultParent);
-- The TestResultsNameIdx and TestResultsNameIdx2 are for speeding up the result graph & reporting code.
CREATE INDEX TestResultsNameIdx ON TestResults (idStrName, tsCreated DESC);
CREATE INDEX TestResultsNameIdx2 ON TestResults (idTestResult, idStrName);

ALTER TABLE TestResultFailures ADD CONSTRAINT TestResultFailures_idTestResult_idTestSet_fkey
    FOREIGN KEY (idTestResult, idTestSet) REFERENCES TestResults(idTestResult, idTestSet) MATCH FULL;


--- @table TestResultValues
-- Test result values.
--
-- A testdriver or subordinate may report a test value via
-- reporter.testValue(), while IPRT based test will use RTTestValue and
-- associates.
--
-- This is an insert only table, no deletes, no updates.
--
CREATE SEQUENCE TestResultValueIdSeq
    START 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;
CREATE TABLE TestResultValues (
    --- The ID of this value.
    idTestResultValue   INTEGER     PRIMARY KEY DEFAULT NEXTVAL('TestResultValueIdSeq'),
    --- The test result it was reported within.
    idTestResult        INTEGER     REFERENCES TestResults(idTestResult)  NOT NULL,
    --- The test set this value is a part of (for avoiding joining thru TestResults).
    -- Note! This is a foreign key, but we have to add it after TestSets has
    --       been created, see further down.
    idTestSet           INTEGER     NOT NULL,
    --- Creation time stamp.
    tsCreated           TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- The name.
    idStrName           INTEGER     REFERENCES TestResultStrTab(idStr)  NOT NULL,
    --- The value.
    lValue              bigint      NOT NULL,
    --- The unit.
    -- @todo This is currently not defined properly. Will fix/correlate this
    --       with the other places we use unit (IPRT/testdriver/VMMDev).
    iUnit               smallint    NOT NULL CHECK (iUnit >= 0 AND iUnit < 1024)
);

CREATE INDEX TestResultValuesIdx ON TestResultValues(idTestResult);
-- The TestResultValuesGraphIdx is for speeding up the result graph & reporting code.
CREATE INDEX TestResultValuesGraphIdx ON TestResultValues(idStrName, tsCreated);
-- The TestResultValuesLogIdx is for speeding up the log viewer.
CREATE INDEX TestResultValuesLogIdx ON TestResultValues(idTestSet, tsCreated);


--- @table TestResultFiles
-- Test result files.
--
-- A testdriver or subordinate may report a file by using
-- reporter.addFile() or reporter.addLogFile().
--
-- The files stored here as well as the primary log file will be processed by a
-- batch job and compressed if considered compressable.  Thus, TM will look for
-- files with a .gz/.bz2 suffix first and then without a suffix.
--
-- This is an insert only table, no deletes, no updates.
--
CREATE SEQUENCE TestResultFileId
    START 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;
CREATE TABLE TestResultFiles (
    --- The ID of this file.
    idTestResultFile    INTEGER     PRIMARY KEY DEFAULT NEXTVAL('TestResultFileId'),
    --- The test result it was reported within.
    idTestResult        INTEGER     REFERENCES TestResults(idTestResult)  NOT NULL,
    --- The test set this file is a part of (for avoiding joining thru TestResults).
    -- Note! This is a foreign key, but we have to add it after TestSets has
    --       been created, see further down.
    idTestSet           INTEGER     NOT NULL,
    --- Creation time stamp.
    tsCreated           TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- The filename relative to TestSets(sBaseFilename) + '-'.
    -- The set of valid filename characters should be very limited so that no
    -- file system issues can occure either on the TM side or the user when
    -- loading the files.  Tests trying to use other characters will fail.
    -- Valid character regular expession: '^[a-zA-Z0-9_-(){}#@+,.=]*$'
    idStrFile           INTEGER     REFERENCES TestResultStrTab(idStr)  NOT NULL,
    --- The description.
    idStrDescription    INTEGER     REFERENCES TestResultStrTab(idStr)  NOT NULL,
    --- The kind of file.
    -- For instance: 'log/release/vm',
    --               'screenshot/failure',
    --               'screencapture/failure',
    --               'xmllog/somestuff'
    idStrKind           INTEGER     REFERENCES TestResultStrTab(idStr)  NOT NULL,
    --- The mime type for the file.
    -- For instance: 'text/plain',
    --               'image/png',
    --               'video/webm',
    --               'text/xml'
    idStrMime           INTEGER     REFERENCES TestResultStrTab(idStr)  NOT NULL
);

CREATE INDEX TestResultFilesIdx ON TestResultFiles(idTestResult);
CREATE INDEX TestResultFilesIdx2 ON TestResultFiles(idTestSet, tsCreated DESC);


--- @table TestResultMsgs
-- Test result message.
--
-- A testdriver or subordinate may report a message via the sDetails parameter
-- of the reporter.testFailure() method, while IPRT test cases will use
-- RTTestFailed, RTTestPrintf and their friends.  For RTTestPrintf, we will
-- ignore the more verbose message levels since these can also be found in one
-- of the logs.
--
-- This is an insert only table, no deletes, no updates.
--
CREATE TYPE TestResultMsgLevel_T AS ENUM (
    'failure',
    'info'
);
CREATE SEQUENCE TestResultMsgIdSeq
    START 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;
CREATE TABLE TestResultMsgs (
    --- The ID of this file.
    idTestResultMsg     INTEGER     PRIMARY KEY DEFAULT NEXTVAL('TestResultMsgIdSeq'),
    --- The test result it was reported within.
    idTestResult        INTEGER     REFERENCES TestResults(idTestResult)  NOT NULL,
    --- The test set this file is a part of (for avoiding joining thru TestResults).
    -- Note! This is a foreign key, but we have to add it after TestSets has
    --       been created, see further down.
    idTestSet           INTEGER     NOT NULL,
    --- Creation time stamp.
    tsCreated           TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- The message string.
    idStrMsg            INTEGER     REFERENCES TestResultStrTab(idStr)  NOT NULL,
    --- The message level.
    enmLevel            TestResultMsgLevel_T  NOT NULL
);

CREATE INDEX TestResultMsgsIdx  ON TestResultMsgs(idTestResult);
CREATE INDEX TestResultMsgsIdx2 ON TestResultMsgs(idTestSet, tsCreated DESC);


--- @table TestSets
-- Test sets / Test case runs.
--
-- This is where we collect data about test runs.
--
-- @todo Not entirely sure where the 'test set' term came from.  Consider
--       finding something more appropriate.
--
CREATE SEQUENCE TestSetIdSeq
    START 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;
CREATE TABLE TestSets (
    --- The ID of this test set.
    idTestSet           INTEGER     PRIMARY KEY DEFAULT NEXTVAL('TestSetIdSeq')  NOT NULL,

    --- The test config timestamp, used when reading test config.
    tsConfig            TIMESTAMP WITH TIME ZONE  DEFAULT CURRENT_TIMESTAMP  NOT NULL,
    --- When this test set was scheduled.
    -- idGenTestBox is valid at this point.
    tsCreated           TIMESTAMP WITH TIME ZONE  DEFAULT CURRENT_TIMESTAMP  NOT NULL,
    --- When this test completed, i.e. testing stopped.  This should only be set once.
    tsDone              TIMESTAMP WITH TIME ZONE  DEFAULT NULL,
    --- The current status.
    enmStatus           TestStatus_T  DEFAULT 'running'::TestStatus_T  NOT NULL,

    --- The build we're testing.
    -- Non-unique foreign key: Builds(idBuild)
    idBuild             INTEGER     NOT NULL,
    --- The build category of idBuild when the test started.
    -- This is for speeding up graph data collection, i.e. avoid idBuild
    -- the WHERE part of the selection.
    idBuildCategory     INTEGER     REFERENCES BuildCategories(idBuildCategory) NOT NULL,
    --- The test suite build we're using to do the testing.
    -- This is NULL if the test suite zip wasn't referred or if a test suite
    -- build source wasn't configured.
    -- Non-unique foreign key: Builds(idBuild)
    idBuildTestSuite    INTEGER     DEFAULT NULL,

    --- The exact testbox configuration.
    idGenTestBox        INTEGER     REFERENCES TestBoxes(idGenTestBox)  NOT NULL,
    --- The testbox ID for joining with (valid: tsStarted).
    -- Non-unique foreign key: TestBoxes(idTestBox)
    idTestBox           INTEGER     NOT NULL,
    --- The scheduling group ID the test was scheduled thru (valid: tsStarted).
    -- Non-unique foreign key: SchedGroups(idSchedGroup)
    idSchedGroup        INTEGER     NOT NULL,

    --- The testgroup (valid: tsConfig).
    -- Non-unique foreign key: TestBoxes(idTestGroup)
    -- Note! This also gives the member ship entry, since a testcase can only
    --       have one membership per test group.
    idTestGroup         INTEGER     NOT NULL,

    --- The exact test case config we executed in this test run.
    idGenTestCase       INTEGER     REFERENCES TestCases(idGenTestCase)  NOT NULL,
    --- The test case ID for joining with (valid: tsConfig).
    -- Non-unique foreign key: TestBoxes(idTestCase)
    idTestCase          INTEGER     NOT NULL,

    --- The arguments (and requirements++) we executed this test case with.
    idGenTestCaseArgs   INTEGER     REFERENCES TestCaseArgs(idGenTestCaseArgs)  NOT NULL,
    --- The argument variation ID (valid: tsConfig).
    -- Non-unique foreign key: TestCaseArgs(idTestCaseArgs)
    idTestCaseArgs      INTEGER     NOT NULL,

    --- The root of the test result tree.
    -- @note This will only be NULL early in the transaction setting up the testset.
    -- @note If the test reports more than one top level test result, we'll
    --       fail the whole test run and let the test developer fix it.
    idTestResult        INTEGER     REFERENCES TestResults(idTestResult)  DEFAULT NULL,

    --- The base filename used for storing files related to this test set.
    -- This is a path relative to wherever TM is dumping log files.  In order
    -- to not become a file system test case, we will try not to put too many
    -- hundred thousand files in a directory.  A simple first approach would
    -- be to just use the current date (tsCreated) like this:
    --    TM_FILE_DIR/year/month/day/TestSets.idTestSet
    --
    -- The primary log file for the test is this name suffixed by '.log'.
    --
    -- The files in the testresultfile table gets their full names like this:
    --    TM_FILE_DIR/sBaseFilename-testresultfile.id-TestResultStrTab(testresultfile.idStrFilename)
    --
    -- @remarks We store this explicitly in case we change the directly layout
    --          at some later point.
    sBaseFilename       text        UNIQUE  NOT NULL,

    --- The gang member number number, 0 is the leader.
    iGangMemberNo       SMALLINT    DEFAULT 0  NOT NULL  CHECK (iGangMemberNo >= 0 AND iGangMemberNo < 1024),
    --- The test set of the gang leader, NULL if no gang involved.
    -- @note This is set by the gang leader as well, so that we can find all
    --       gang members by WHERE idTestSetGangLeader = :id.
    idTestSetGangLeader INTEGER     REFERENCES TestSets(idTestSet)  DEFAULT NULL

);
CREATE INDEX TestSetsGangIdx        ON TestSets (idTestSetGangLeader);
CREATE INDEX TestSetsBoxIdx         ON TestSets (idTestBox, idTestResult);
CREATE INDEX TestSetsBuildIdx       ON TestSets (idBuild, idTestResult);
CREATE INDEX TestSetsTestCaseIdx    ON TestSets (idTestCase, idTestResult);
CREATE INDEX TestSetsTestVarIdx     ON TestSets (idTestCaseArgs, idTestResult);
--- The TestSetsDoneCreatedBuildCatIdx is for testbox results, graph options and such.
CREATE INDEX TestSetsDoneCreatedBuildCatIdx ON TestSets (tsDone DESC NULLS FIRST, tsCreated ASC, idBuildCategory);
--- For graphs.
CREATE INDEX TestSetsGraphBoxIdx    ON TestSets (idTestBox, tsCreated DESC, tsDone ASC NULLS LAST, idBuildCategory, idTestCase);

ALTER TABLE TestResults        ADD FOREIGN KEY (idTestSet) REFERENCES TestSets(idTestSet) MATCH FULL;
ALTER TABLE TestResultValues   ADD FOREIGN KEY (idTestSet) REFERENCES TestSets(idTestSet) MATCH FULL;
ALTER TABLE TestResultFiles    ADD FOREIGN KEY (idTestSet) REFERENCES TestSets(idTestSet) MATCH FULL;
ALTER TABLE TestResultMsgs     ADD FOREIGN KEY (idTestSet) REFERENCES TestSets(idTestSet) MATCH FULL;
ALTER TABLE TestResultFailures ADD FOREIGN KEY (idTestSet) REFERENCES TestSets(idTestSet) MATCH FULL;




-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
--
--     T e s t   M a n g e r   P e r s i s t e n t   S t o r a g e
--
-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --

--- @type TestBoxState_T
-- TestBox state.
--
-- @todo Consider drawing a state diagram for this.
--
CREATE TYPE TestBoxState_T AS ENUM (
    --- Nothing to do.
    -- Prev: testing, gang-cleanup, rebooting, upgrading,
    --       upgrading-and-rebooting, doing-special-cmd.
    -- Next: testing, gang-gathering, rebooting, upgrading,
    --       upgrading-and-rebooting, doing-special-cmd.
    'idle',
    --- Executing a test.
    -- Prev: idle
    -- Next: idle
    'testing',

    -- Gang scheduling statuses:
    --- The gathering of a gang.
    -- Prev: idle
    -- Next: gang-gathering-timedout, gang-testing
    'gang-gathering',
    --- The gathering timed out, the testbox needs to cleanup and move on.
    -- Prev: gang-gathering
    -- Next: idle
    -- This is set on all gathered members by the testbox who triggers the
    -- timeout.
    'gang-gathering-timedout',
    --- The gang scheduling equivalent of 'testing'.
    -- Prev: gang-gathering
    -- Next: gang-cleanup
    'gang-testing',
    --- Waiting for the other gang members to stop testing so that cleanups
    -- can be performed and members safely rescheduled.
    -- Prev: gang-testing
    -- Next: idle
    --
    -- There are two resource clean up issues being targeted here:
    --  1. Global resources will be allocated by the leader when he enters the
    --     'gang-gathering' state.  If the leader quits and frees the resource
    --     while someone is still using it, bad things will happen.  Imagine a
    --     global resource without any access checks and relies exclusivly on
    --     the TM doing its job.
    --  2. TestBox resource accessed by other gang members may also be used in
    --     other tests.  Should a gang member leave early and embark on a
    --     testcase using the same resources, bad things will happen.  Example:
    --     Live migration.  One partner leaves early because it detected some
    --     fatal failure, the other one is still trying to connect to him.
    --     The testbox is scheduled again on the same live migration testcase,
    --     only with different arguments (VM config), it will try migrate using
    --     the same TCP ports. Confusion ensues.
    --
    -- To figure out whether to remain in this status because someone is
    -- still testing:
    --      SELECT COUNT(*) FROM TestBoxStatuses, TestSets
    --      WHERE TestSets.idTestSetGangLeader = :idGangLeader
    --        AND TestSets.idTestBox = TestBoxStatuses.idTestBox
    --        AND TestSets.idTestSet = TestBoxStatuses.idTestSet
    --        AND TestBoxStatuses.enmState = 'gang-testing'::TestBoxState_T;
    'gang-cleanup',

    -- Command related statuses (all command status changes comes from 'idle'
    -- and goes back to 'idle'):
    'rebooting',
    'upgrading',
    'upgrading-and-rebooting',
    'doing-special-cmd'
);

--- @table TestBoxStatuses
-- Testbox status table.
--
-- History is not planned on this table.
--
CREATE TABLE TestBoxStatuses (
    --- The testbox.
    idTestBox           INTEGER     PRIMARY KEY  NOT NULL,
    --- The testbox generation ID.
    idGenTestBox        INTEGER     REFERENCES TestBoxes(idGenTestBox)  NOT NULL,
    --- When this status was last updated.
    -- This is updated everytime the testbox talks to the test manager, thus it
    -- can easily be used to find testboxes which has stopped responding.
    --
    -- This is used for timeout calculation during gang-gathering, so in that
    -- scenario it won't be updated until the gang is gathered or we time out.
    tsUpdated           TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL,
    --- The current state.
    enmState            TestBoxState_T DEFAULT 'idle'::TestBoxState_T  NOT NULL,
    --- Reference to the test set
    idTestSet           INTEGER     REFERENCES TestSets(idTestSet),
    --- Interal work item number.
    -- This is used to pick and prioritize between multiple scheduling groups.
    iWorkItem           INTEGER     DEFAULT 0  NOT NULL
);


--- @table GlobalResourceStatuses
-- Global resource status, tracks which test set resources are allocated by.
--
-- History is not planned on this table.
--
CREATE TABLE GlobalResourceStatuses (
    --- The resource ID.
    -- Non-unique foreign key: GlobalResources(idGlobalRsrc).
    idGlobalRsrc        INTEGER     PRIMARY KEY  NOT NULL,
    --- The resource owner.
    -- @note This is going thru testboxstatus to be able to use the testbox ID
    --       as a foreign key.
    idTestBox           INTEGER     REFERENCES TestBoxStatuses(idTestBox)  NOT NULL,
    --- When the allocation took place.
    tsAllocated         TIMESTAMP WITH TIME ZONE  DEFAULT current_timestamp  NOT NULL
);


--- @table SchedQueues
-- Scheduler queue.
--
-- The queues are currently associated with a scheduling group, it could
-- alternative be changed to hook on to a testbox instead.  It depends on what
-- kind of scheduling method we prefer.  The former method aims at test case
-- thruput, making sacrifices in the hardware distribution area.  The latter is
-- more like the old buildbox style testing, making sure that each test case is
-- executed on each testbox.
--
-- When there are configuration changes, TM will regenerate the scheduling
-- queue for the affected scheduling groups.  We do not concern ourselves with
-- trying to continue at the approximately same queue position, we simply take
-- it from the top.
--
-- When a testbox ask for work, we will open a cursor on the queue and take the
-- first test in the queue that can be executed on that testbox.  The test will
-- be moved to the end of the queue (getting a new item_id).
--
-- If a test is manually changed to the head of the queue, the item will get a
-- item_id which is 1 lower than the head of the queue.  Unless someone does
-- this a couple of billion times, we shouldn't have any trouble running out of
-- number space. :-)
--
-- Manually moving a test to the end of the queue is easy, just get a new
-- 'item_id'.
--
-- History is not planned on this table.
--
CREATE SEQUENCE SchedQueueItemIdSeq
    START 1
    INCREMENT BY 1
    NO MAXVALUE
    NO MINVALUE
    CACHE 1;
CREATE TABLE SchedQueues (
    --- The scheduling queue (one queue per scheduling group).
    -- Non-unique foreign key: SchedGroups(idSchedGroup)
    idSchedGroup        INTEGER     NOT NULL,
    --- The scheduler queue entry ID.
    -- Lower numbers means early queue position.
    idItem              INTEGER     DEFAULT NEXTVAL('SchedQueueItemIdSeq')  NOT NULL,
    --- The queue offset.
    -- This is used for repositining the queue when recreating it.  It can also
    -- be used to figure out how jumbled the queue gets after real life has had
    -- it's effect on it.
    offQueue            INTEGER     NOT NULL,
    --- The test case argument variation to execute.
    idGenTestCaseArgs   INTEGER     REFERENCES TestCaseArgs(idGenTestCaseArgs)  NOT NULL,
    --- The relevant testgroup.
    -- Non-unique foreign key: TestGroups(idTestGroup).
    idTestGroup         INTEGER     NOT NULL,
    --- Aggregated test group dependencies (NULL if none).
    -- Non-unique foreign key: TestGroups(idTestGroup).
    -- See also comments on SchedGroupMembers.idTestGroupPreReq.
    aidTestGroupPreReqs INTEGER ARRAY  DEFAULT NULL,
    --- The scheduling time constraints (see SchedGroupMembers.bmHourlySchedule).
    bmHourlySchedule    bit(168)    DEFAULT NULL,
    --- When the queue entry was created and for which config is valid.
    -- This is the timestamp that should be used when reading config info.
    tsConfig            TIMESTAMP WITH TIME ZONE  DEFAULT CURRENT_TIMESTAMP  NOT NULL,
    --- When this status was last scheduled.
    -- This is set to current_timestamp when moving the entry to the end of the
    -- queue.  It's initial value is unix-epoch.  Not entirely sure if it's
    -- useful beyond introspection and non-unique foreign key hacking.
    tsLastScheduled     TIMESTAMP WITH TIME ZONE  DEFAULT TIMESTAMP WITH TIME ZONE 'epoch'  NOT NULL,

    --- This is used in gang scheduling.
    idTestSetGangLeader INTEGER     REFERENCES TestSets(idTestSet)  DEFAULT NULL  UNIQUE,
    --- The number of gang members still missing.
    --
    -- This saves calculating the number of missing members via selects like:
    --     SELECT COUNT(*) FROM TestSets WHERE idTestSetGangLeader = :idGang;
    -- and
    --     SELECT cGangMembers FROM TestCaseArgs WHERE idGenTestCaseArgs = :idTest;
    -- to figure out whether to remain in 'gather-gang'::TestBoxState_T.
    --
    cMissingGangMembers smallint    DEFAULT 1  NOT NULL,

    --- @todo
    --- The number of times this has been considered for scheduling.
    -- cConsidered SMALLINT DEFAULT 0 NOT NULL,

    PRIMARY KEY (idSchedGroup, idItem)
);
CREATE INDEX SchedQueuesItemIdx          ON SchedQueues(idItem);
CREATE INDEX SchedQueuesSchedGroupIdx    ON SchedQueues(idSchedGroup);

