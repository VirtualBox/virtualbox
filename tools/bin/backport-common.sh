# $Id: backport-common.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# Common backport script bits.
#

#
# Copyright (C) 2020-2024 Oracle and/or its affiliates.
#
# This file is part of VirtualBox base platform packages, as
# available from https://www.virtualbox.org.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation, in version 3 of the
# License.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <https://www.gnu.org/licenses>.
#
# SPDX-License-Identifier: GPL-3.0-only
#

#
# Globals.
#
   MY_CAT=kmk_cat
  MY_EXPR=kmk_expr
MY_PRINTF=kmk_printf
    MY_RM=kmk_rm
   MY_SVN=svn
   MY_SED=kmk_sed

#
# Functions.
#
BranchDirToName()
{
    MY_DIR=$1
    MY_NAME=`echo "${MY_DIR}" | "${MY_SED}" -e 's|^\(.*\)/\([^/][^/]*\)$|\2|'`
    case "${MY_NAME}" in
        VBox-[5-9].[0-3]|VBox-1[0-5].[0-3])
            echo "${MY_NAME}" | "${MY_SED}" -e 's/VBox-//'
            ;;
        [Vv][Bb][Oo][Xx][5-9][0-3])
            echo "${MY_NAME}" | "${MY_SED}" -e 's/[Vv][Bb][Oo][Xx]\([0-9]\)\([0-3]\)/\1.\2/'
            ;;
        [Tt][Rr][Uu][Nn][Kk])
            echo trunk
            ;;
        *)
            echo "warning: Unable to guess branch given ${MY_NAME} ($1)" 1>&2
            ;;
    esac
}

AddRevision()
{
    if test -z "${MY_REVISIONS}"; then
        MY_REVISIONS=$1
        MY_REVISION_COUNT=1
    else
        MY_REVISIONS="${MY_REVISIONS} $1"
        MY_REVISION_COUNT=$(${MY_EXPR} ${MY_REVISION_COUNT} + 1)
    fi
}

AddRevisionRange()
{
    MY_REV=$1
    MY_REV_FIRST=${MY_REV%-*}
    MY_REV_LAST=${MY_REV#*-}
    if test -z "${MY_REV_FIRST}" -o -z "${MY_REV_LAST}" -o '(' '!' "${MY_REV_FIRST}" -lt "${MY_REV_LAST}" ')'; then
        echo "error: Failed to parse revision range: MY_REV_FIRST=${MY_REV_FIRST} MY_REV_LAST=${MY_REV_LAST} MY_REV=${MY_REV}"
        exit 1
    fi
    MY_REV=${MY_REV_FIRST}
    while test ${MY_REV} -le ${MY_REV_LAST};
    do
        AddRevision "${MY_REV}"
        MY_REV=$(${MY_EXPR} ${MY_REV} + 1)
    done
}

#
# Figure default branch given the script location.
#
MY_BRANCH_DEFAULT_DIR=`cd "${MY_SCRIPT_DIR}"; cd ../..; pwd -L`
MY_BRANCH_DEFAULT=`BranchDirToName "${MY_BRANCH_DEFAULT_DIR}"`
if test "${MY_BRANCH_DEFAULT}" = "trunk"; then
    MY_TRUNK_DIR=${MY_BRANCH_DEFAULT_DIR}
elif test -d "${MY_BRANCH_DEFAULT_DIR}/../../trunk"; then
    MY_TRUNK_DIR=`cd "${MY_BRANCH_DEFAULT_DIR}"; cd ../../trunk; pwd -L`
else
    MY_TRUNK_DIR="^/trunk"
fi


#
# Parse arguments.
#
MY_BRANCH_DIR=
MY_BRANCH=
MY_REVISIONS=
MY_REVISION_COUNT=0
MY_EXTRA_ARGS=
MY_DEBUG=
MY_FORCE=
MY_SHOW_DIFF=

while test $# -ge 1;
do
    ARG=$1
    shift
    case "${ARG}" in
        r[0-9][0-9][0-9][0-9][0-9]|r[0-9][0-9][0-9][0-9][0-9][0-9]|r[0-9][0-9][0-9][0-9][0-9][0-9][0-9])
            MY_REV=`echo ${ARG} | "${MY_SED}" -e 's/^r//'`
            AddRevision ${MY_REV}
            ;;

        [0-9][0-9][0-9][0-9][0-9]|[0-9][0-9][0-9][0-9][0-9][0-9]|[0-9][0-9][0-9][0-9][0-9][0-9][0-9])
            AddRevision ${ARG}
            ;;

        [0-9][0-9][0-9][0-9][0-9]-[0-9][0-9][0-9][0-9][0-9]|[0-9][0-9][0-9][0-9][0-9][0-9]-[0-9][0-9][0-9][0-9][0-9][0-9]|[0-9][0-9][0-9][0-9][0-9][0-9][0-9]-[0-9][0-9][0-9][0-9][0-9][0-9][0-9])
            AddRevisionRange ${ARG}
            ;;
        r[0-9][0-9][0-9][0-9][0-9]-[0-9][0-9][0-9][0-9][0-9]|r[0-9][0-9][0-9][0-9][0-9][0-9]-[0-9][0-9][0-9][0-9][0-9][0-9]|r[0-9][0-9][0-9][0-9][0-9][0-9][0-9]-[0-9][0-9][0-9][0-9][0-9][0-9][0-9])
            MY_REV=`echo "${ARG}" | "${MY_SED}" -e 's/^r//'`
            AddRevisionRange ${MY_REV}
            ;;

        --trunk-dir)
            if test $# -eq 0; then
                echo "error: missing --trunk-dir argument." 1>&2
                exit 1;
            fi
            MY_TRUNK_DIR=`echo "$1" | "${MY_SED}" -e 's|\\\|/|g'`
            shift
            ;;

        --branch-dir)
            if test $# -eq 0; then
                echo "error: missing --branch-dir argument." 1>&2
                exit 1;
            fi
            MY_BRANCH_DIR=`echo "$1" | "${MY_SED}" -e 's|\\\|/|g'`
            shift
            ;;

        --branch)
            if test $# -eq 0; then
                echo "error: missing --branch argument." 1>&2
                exit 1;
            fi
            MY_BRANCH="$1"
            shift
            ;;

        --first-rev|--first|-1)
            MY_FIRST_REV=1
            ;;

        --force)
            MY_FORCE=1
            ;;

        --update-first|--update|-u)
            MY_UPDATE_FIRST=1
            ;;

        --show-diff)
            MY_SHOW_DIFF=1
            ;;

        --extra)
            if test $# -eq 0; then
                echo "error: missing --extra argument." 1>&2
                exit 1;
            fi
            MY_EXTRA_ARGS="${MY_EXTRA_ARGS} $1"
            shift
            ;;

        --debug)
            MY_DEBUG=1
            ;;

        # usage
        --h*|-h*|-?|--?)
            echo "usage: $0 [--trunk-dir <dir>] [--branch <ver>] [--branch-dir <dir>] [--extra <svn-arg>] \\"
            echo "                   [--first-rev] [--update-first] rev1 [rev2..[revN]]]"
            echo ""
            echo "Options:"
            echo "  --trunk-dir <dir>"
            echo "    The source of the changeset being backported."
            echo "  --branch-dir <dir>"
            echo "    The backport destination directory. default: script location"
            echo "  --branch <ver>"
            echo "    The name of the branch being backported to. default: auto"
            echo "  --debug"
            echo "    Enables verbose output."
            echo "  --first-rev, --first, -1"
            echo "    Merge only: Check that the branch does not have any pending changes."
            echo "  --force"
            echo "    Forces backporting, regardless of ancestry. Use with caution!"
            echo "  --show-diff"
            echo "    Shows unified diff before backporting."
            echo "  --update-first, --update, -u"
            echo "    Merge only: Update the branch before merging."
            echo "  --extra <svn-arg>"
            echo "    Additional arguments to specify to SVN."
            echo ""
            exit 2;
            ;;

        *)
            echo "syntax error: ${ARG}"
            exit 2;
            ;;
    esac
done

if test -n "${MY_DEBUG}"; then
    echo "        MY_SCRIPT_DIR=${MY_SCRIPT_DIR}"
    echo "        MY_BRANCH_DIR=${MY_BRANCH_DIR}"
    echo "            MY_BRANCH=${MY_BRANCH}"
    echo "MY_BRANCH_DEFAULT_DIR=${MY_BRANCH_DEFAULT_DIR}"
    echo "    MY_BRANCH_DEFAULT=${MY_BRANCH_DEFAULT}"
    echo "         MY_TRUNK_DIR=${MY_TRUNK_DIR}"
    echo "         MY_REVISIONS=${MY_REVISIONS}"
fi

#
# Resolve branch variables.
#
if test -z "${MY_BRANCH_DIR}" -a -z "${MY_BRANCH}"; then
    MY_BRANCH_DIR=${MY_BRANCH_DEFAULT_DIR}
    MY_BRANCH=${MY_BRANCH_DEFAULT}
elif test -n "${MY_BRANCH}" -a -z "${MY_BRANCH_DIR}"; then
    MY_BRANCH_DIR=${MY_BRANCH_DEFAULT_DIR}
elif test -z "${MY_BRANCH}" -a -n "${MY_BRANCH_DIR}"; then
    MY_BRANCH=`BranchDirToName "${MY_BRANCH_DIR}"`
    if test -z "${MY_BRANCH}" -o  "${MY_BRANCH}" = "${MY_BRANCH_DIR}"; then
        echo "error: Failed to guess branch name for: ${MY_BRANCH_DIR}" 1>&2
        echo "       Use --branch to specify it." 1>&2
        exit 2;
    fi
fi
if test "${MY_BRANCH}" = "trunk"; then
    echo "error: script does not work with 'trunk' as the branch" 1>&2
    exit 2;
fi

#
# Stop if no revisions specified.
#
if test -z "${MY_REVISIONS}" -a "${MY_SCRIPT_NAME}" '!=' "backport-commit.sh"; then
    echo "error: No revisions specified" 1>&2;
    exit 2;
fi
