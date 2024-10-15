/* $Id: VBox-MakefileGuidelines.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox - Makefile Guidelines.
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

/** @page pg_vbox_makefile_guidelines      VBox Makefile Guidelines
 *
 * These guidelines apply to all the Makefile.kmk files in the tree.
 * No exceptions.
 *
 * All these makefiles are ultimately the responsiblity of bird.  Since there
 * are currently more than three hundred files and the number is growing, they
 * have to be very kept uniform or it will become very difficult to maintain
 * them and impossible do bulk refactoring.  Thus these guidelines have no bits
 * that are optional unlike the coding guidelines, and should be thought of as
 * rules rather than guidelines.
 *
 * Note! The guidelines do not apply to the non-kBuild makefiles found in the
 *       source tree, like the ones shipped in the SDK and the ones for the
 *       linux kernel modules.
 *
 *
 * @section sec_vbox_makefile_guidelines_kbuild         kBuild
 *
 * kBuild is way older than VirtualBox, at least as a concept, but the
 * VirtualBox project was a push to get something done about it again.  It's
 * maintained by bird in his spare time because: "We don't make buildsystems, we
 * make virtual machines".  So, kBuild makes progress when there is spare time
 * or when there is an urgent need for something.
 *
 * The kBuild docs are in the process of being written.  The current items and
 * their status per 2018-10-29:
 *      - kmk Quick Reference [completed]:
 *        http://svn.netlabs.org/kbuild/wiki/kmk%20Quick%20Reference
 *      - kBuild Quick Reference [just started]:
 *        http://svn.netlabs.org/kbuild/wiki/kBuild%20Quick%20Reference
 *
 * Local copies of the docs can be found in kBuild/docs, just keep in mind that
 * they might be slightly behind the online version.
 *
 *
 * @section sec_vbox_makefile_guidelines_example        Example Makefiles
 *
 * Let me point to some good sample makefiles:
 *      - src/VBox/Additions/common/VBoxService/Makefile.kmk
 *      - src/VBox/Debugger/Makefile.kmk
 *      - src/VBox/Disassembler/Makefile.kmk
 *
 * And some bad ones:
 *      - src/lib/xpcom18a4/Makefile.kmk
 *      - src/recompiler/Makefile.kmk
 *      - src/VBox/Devices/Makefile.kmk
 *      - src/VBox/Main/Makefile.kmk
 *      - src/VBox/Runtime/Makefile.kmk
 *
 *
 * @section sec_vbox_makefile_guidelines                Guidelines
 *
 *  First one really important fact:
 *
 *       Everything is global because all makefiles
 *       are virtually one single makefile.
 *
 *  The rules:
 *
 *      - Using bits defined by a sub-makefile is fine, using anything defined
 *        by a parent, sibling, uncle, cousine, or remoter relatives is not
 *        Okay. It may break sub-tree building which is not acceptable.
 *
 *      - Template names starts with VBox and are camel cased, no
 *        underscores or other separators.  (Note this used to be all upper
 *        case, fixing this incomplete.)
 *
 *      - Makefile variables shall be prefixed with VBOX or VB to avoid clashes
 *        with environment and kBuild variables.
 *
 *      - Makefile variables are all upper cased and uses underscores to
 *        separate the words.
 *
 *      - All variables are global. Make sure they are globally unique, but try
 *        not make them incredible long.
 *
 *      - Makefile variables goes after the inclusion of the header and
 *        usually after including sub-makefiles.
 *
 *      - Variables that are used by more than one makefile usually ends up
 *        in the monster file, Config.kmk.  However, see if there are any
 *        sub-tree specific Config.kmk files that could do the job first.
 *
 *      - Targets are lower or camel cased and as a rule the same as the
 *        resulting binary.
 *
 *      - Install targets frequently have a -inst in their name, and a name that
 *        gives some idea what they install
 *
 *      - Always use templates (mytarget_TEMPLATE = VBoxSomething).
 *
 *      - Comment each target with a 3+ line block as seen in
 *        src/VBox/Debugger/Makefile.kmk.
 *
 *      - No space between the comment block and the target definition.
 *
 *      - Try fit all the custom recipes after the target they apply to.
 *
 *      - Custom recipes that apply to more than one target should be placed at
 *        the bottom of the makefile, before the footer inclusion when possible.
 *
 *      - Do NOT use custom recipes to install stuff, use install targets.
 *        Generate files to inst-target_0_OUTDIR. (Yes, there are a lot places
 *        where we don't do this yet.)
 *
 *      - Always break SOURCES, LIBS, long target list and other lists the
 *        manner Debugger_SOURCES is broken into multiple lines in
 *        src/VBox/Debugger/Makefile.kmk. I.e. exactly one tab, the file name /
 *        list item, another space, the slash and then the newline.
 *
 *      - Line continuation slashes shall never ever be aligned vertically (that
 *        always goes crooked sooner or later), but have exactly one space
 *        before them.
 *
 *      - The last element of an broken list shall not have a slash-newline,
 *        otherwise we risk getting the next variable into the list.
 *
 *      - When if'ed blocks come into play, we will only indent the conditional
 *        makefile directives (if, ifeq, ifneq, if1of, ifn1of, ifdef, ifndef,
 *        else, endif, ++), one space for each level.  (Note! We used to indent
 *        non-directives, which made emacs upset as we'd have both tabs and
 *        spaces on as indentation on the same line.  There are a lot of cases
 *        of this still around.)
 *
 *      - \$(NO_SUCH_VARIABLE) should be when you need to put nothing somewhere,
 *        for instance to prevent inherting an attribute.
 *
 *      - Always put the defines in the DEFS properties, never use the FLAGS
 *        properties for this.  Doing so may screw up depenencies and object
 *        caches.
 *
 *      - Mark each section and target of the file with a 3+ lines comment
 *        block.
 *
 *      - Document variables that are not immediately obvious using double hash
 *        comments, doxygen style.
 *
 *      - Each an every Makefile.kmk shall have a file header with Id, file
 *        description and copyright/license exactly like in the examples.  (The
 *        SCM tool will complain if you don't.)
 *
 *      - Multiple blank lines in a makefile is very seldom there without a
 *        reason and shall be preserved.
 *
 *      - Inserting blank lines between target properties is all right if the
 *        target definition is long and/or crooked.
 *
 *      - if1of and ifn1of shall always have a space after the comma, while ifeq
 *        and ifneq shall not. That way they are easier to tell apart.
 *
 *      - Do a svn diff before committing makefile changes.
 *
 *
 * @section sec_vbox_makefile_guidelines_reminders      Helpful reminders
 *
 *      - Do not be afraid to ask for help on IRC or in the defect you're
 *        working on. There are usually somebody around that know how to best do
 *        something.
 *
 *      - Watch out for "Heads Up!" bugtracker messages concerning the build
 *        system.
 *
 *      - To remove bits from a template you're using you have to create a new
 *        template that extends the existing one and creatively use
 *        \$(filter-out) or \$(patsubst).
 *
 *      - You can build sub-trees.
 *
 *      - You don't have to cd into sub-trees: kmk -C src/recompiler
 *
 *      - You can build individual targets: kmk VBoxRT
 *
 *      - Even install targets: kmk nobin
 *
 *      - You can compile individual source files: kmk ConsoleImpl.o
 *
 *      - You can tell kmk to continue on failure: kmk -k
 *
 *      - You can tell kmk to run at low priority: kmk --nice
 *
 *      - The --pretty-command-printing option is useful for seeing exactly
 *        what's passed to the tools.
 *
 *      - You can invoke recipes in the root makefile more efficiently via the
 *        Maintenance.kmk file: kmk -f Maintenance.kmk incs
 *
 */

