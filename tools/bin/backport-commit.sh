#!/usr/bin/env kmk_ash
# $Id: backport-commit.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# Script for committing a backport from trunk.
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
# Determin script dir so we can source the common bits.
#
MY_SED=kmk_sed
MY_SCRIPT_DIR=`echo "$0" | "${MY_SED}" -e 's|\\\|/|g' -e 's|^\(.*\)/[^/][^/]*$|\1|'` # \ -> / is for windows.
if test "${MY_SCRIPT_DIR}" = "$0"; then
    MY_SCRIPT_DIR=`pwd -L`
else
    MY_SCRIPT_DIR=`cd "${MY_SCRIPT_DIR}"; pwd -L`       # pwd is built into kmk_ash.
fi

#
# This does a lot.
#
MY_SCRIPT_NAME="backport-commit.sh"
. "${MY_SCRIPT_DIR}/backport-common.sh"

#
# If no revisions was given, try figure it out from the svn:merge-info
# property.
#
if test -z "${MY_REVISIONS}"; then
    MY_REV_TMP=backport-revisions.tmp
    if ! svn di --properties-only --depth empty "${MY_BRANCH_DIR}" > "${MY_REV_TMP}"; then
        echo "error: failed to get revisions from svn:mergeinfo (svn)"
        exit 1;
    fi
    for MY_REV in $("${MY_SED}" -e '/ *Merged \//!d' -e "s/^ [^:]*:[r]*//" -e 's/,[r]*/ /g' "${MY_REV_TMP}");
    do
        case "${MY_REV}" in
            [0-9][0-9][0-9][0-9][0-9]|[0-9][0-9][0-9][0-9][0-9][0-9]|[0-9][0-9][0-9][0-9][0-9][0-9][0-9])
                AddRevision "${MY_REV}"
                ;;
            [0-9][0-9][0-9][0-9][0-9]-[0-9][0-9][0-9][0-9][0-9]|[0-9][0-9][0-9][0-9][0-9][0-9]-[0-9][0-9][0-9][0-9][0-9][0-9]|[0-9][0-9][0-9][0-9][0-9][0-9][0-9]-[0-9][0-9][0-9][0-9][0-9][0-9][0-9])
                AddRevisionRange "${MY_REV}"
                ;;

            *)  echo "error: failed to get revisions from svn:mergeinfo - does not grok: ${MY_ARG}"
                exit 1;;
        esac
    done
    "${MY_RM}" -f -- "${MY_REV_TMP}"
    if test -z "${MY_REVISIONS}"; then
        echo "error: No backported revisions found";
        exit 1;
    fi
    echo "info: Detected revisions: ${MY_REVISIONS}"
fi

#
# Generate the commit message into MY_MSG_FILE.
#
test -n "${MY_DEBUG}" && echo "MY_REVISIONS=${MY_REVISIONS}"
MY_MSG_FILE=backport-commit.txt
MY_TMP_FILE=backport-commit.tmp

if test "${MY_REVISION_COUNT}" -eq 1; then
    # Single revision, just prefix the commit message.
    MY_REV=`echo ${MY_REVISIONS}`       # strip leading space
    echo -n "${MY_BRANCH}: Backported r${MY_REV}: " > "${MY_MSG_FILE}"
    if ! "${MY_SVN}" log "-r${MY_REV}" "${MY_TRUNK_DIR}" > "${MY_TMP_FILE}"; then
        echo "error: failed to get log entry for revision ${MY_REV}"
        exit 1;
    fi
    if ! "${MY_SED}" -e '1d;2d;3d;$d' --append "${MY_MSG_FILE}" "${MY_TMP_FILE}"; then
        echo "error: failed to get log entry for revision ${MY_REV} (sed failed)"
        exit 1;
    fi
else
    # First line.
    echo -n "${MY_BRANCH}: Backported" > "${MY_MSG_FILE}"
    MY_COUNTER=0
    for MY_REV in ${MY_REVISIONS};
    do
        if test ${MY_COUNTER} -eq 0; then
            echo -n " r${MY_REV}" >> "${MY_MSG_FILE}"
        else
            echo -n " r${MY_REV}" >> "${MY_MSG_FILE}"
        fi
        MY_COUNTER=`"${MY_EXPR}" ${MY_COUNTER} + 1`
    done
    echo "." >> "${MY_MSG_FILE}"
    echo ""  >> "${MY_MSG_FILE}"

    # One bullet with the commit text.
    for MY_REV in ${MY_REVISIONS};
    do
        echo -n "* r${MY_REV}: " >> "${MY_MSG_FILE}"
        if ! "${MY_SVN}" log "-r${MY_REV}" "${MY_TRUNK_DIR}" > "${MY_TMP_FILE}"; then
            echo "error: failed to get log entry for revision ${MY_REV}"
            exit 1;
        fi
        if ! "${MY_SED}" -e '1d;2d;3d;$d' --append "${MY_MSG_FILE}" "${MY_TMP_FILE}"; then
            echo "error: failed to get log entry for revision ${MY_REV} (sed failed)"
            exit 1;
        fi
    done

    # This is a line ending hack for windows hosts.
    if    "${MY_SED}" -e 's/1/1/g' --output-text "${MY_TMP_FILE}" "${MY_MSG_FILE}" \
       && "${MY_SED}" -e 's/1/1/g' --output-text "${MY_MSG_FILE}" "${MY_TMP_FILE}"; then
        :
    else
        echo "error: SED failed to clean up commit message line-endings."
        exit 1;
    fi
fi
"${MY_RM}" -f -- "${MY_TMP_FILE}"

#
# Do the committing.
#
if [ -n "${MY_SHOW_DIFF}" ]; then
    echo "***"
    echo "*** Diff:"
    "${MY_SVN}" diff --internal-diff
    echo "*** end diff ***"
    echo "***"
    echo ""
fi
echo "***"
echo "*** Commit message:"
"${MY_CAT}" "${MY_MSG_FILE}"
echo "*** end commit message ***"
echo "***"
IFS=`"${MY_PRINTF}" " \t\r\n"` # windows needs \r for proper 'read' operation.
for MY_IGNORE in 1 2 3; do
    read -p "*** Does the above commit message look okay (y/n)?" MY_ANSWER
    case "${MY_ANSWER}" in
        y|Y|[yY][eE][sS])
            if "${MY_SVN}" commit -F "${MY_MSG_FILE}" "${MY_BRANCH_DIR}"; then
                "${MY_RM}" -f -- "${MY_MSG_FILE}"

                #
                # Update the branch so we don't end up with mixed revisions.
                #
                echo "***"
                echo "*** Updating branch dir..."
                "${MY_SVN}" up "${MY_BRANCH_DIR}"
                exit 0
            fi
            echo "error: commit failed" 1>&2
            exit 1
            ;;
        n|N|[nN][oO])
            exit 1
            ;;
        *)
            echo
            echo "Please answer 'y' or 'n'... (MY_ANSWER=${MY_ANSWER})"
    esac
done
exit 1;
