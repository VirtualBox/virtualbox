#!/usr/bin/env kmk_ash
# $Id: dita-refentry-flat-to-single-topic.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# Helper Script for splitting up a convert manpage into separate topic
# files (named by @id).
#

#
# Copyright (C) 2023-2024 Oracle and/or its affiliates.
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
# This script is very internal, so we got the following fixed position parameters:
#   1: The input DITA file (output from docbook-refentry-to-manual-dita.xsl).
#   2: dita-refentry-flat-topic-ids.xsl
#   3: dita-refentry-flat-to-single-topic.xsl
#   4: The refentry.kmk file where the DITA file list will be appended.
#   5: Variable name to update in refentry.kmk.
#   6: The out directory.
#   7: '--'
#   8+: xsltproc invocation (sans output, input and xslt file).
#
if test $# -lt 8; then
    echo "syntax error: too few arguments" 1>&2;
    exit 2;
fi
MY_INPUT_FILE="$1"
MY_XSLT_TOPIC_IDS="$2"
MY_XSLT_TO_SINGLE_TOPIC="$3"
MY_GENERATED_KMK="$4"
MY_GENERATED_KMK_VARIABLE="$5"
MY_OUTPUT_DIR="$6"
if test "$7" != "--"; then
    echo "syntax error: Expected '--' as the 7th parameter, got: $7" 1>&2;
    exit 2;
fi
shift 7

if ! test -f "${MY_INPUT_FILE}"; then
    echo "error: Input file does not exists or is not a regular file: ${MY_INPUT_FILE}" 1>&2;
    exit 1;
fi
if ! test -f "${MY_XSLT_TOPIC_IDS}"; then
    echo "error: The given dita-refentry-flat-topic-ids.xsl file does not exists or is not a regular file: ${MY_XSLT_TOPIC_IDS}" 1>&2;
    exit 1;
fi
if ! test -f "${MY_XSLT_TO_SINGLE_TOPIC}"; then
    echo "error: The given dita-refentry-flat-to-single-topic.xsl file does not exists or is not a regular file: ${MY_XSLT_TO_SINGLE_TOPIC}" 1>&2;
    exit 1;
fi
if ! test -d "${MY_OUTPUT_DIR}"; then
    echo "error: Destination directory does not exists or not a directory: ${MY_OUTPUT_DIR}" 1>&2;
    exit 1;
fi

# Exit on failure.
set -e

#
# First get the ID list from it.
#
MY_TOPIC_IDS=$($* "${MY_XSLT_TOPIC_IDS}" "${MY_INPUT_FILE}")

echo "${MY_GENERATED_KMK_VARIABLE} += \\" > "${MY_GENERATED_KMK}"

#
# Extract each topic.
#
for MY_ID in ${MY_TOPIC_IDS};
do
    $* \
        --stringparam g_sMode topic \
        --stringparam g_idTopic "${MY_ID}" \
        --output "${MY_OUTPUT_DIR}/${MY_ID}.dita" "${MY_XSLT_TO_SINGLE_TOPIC}" "${MY_INPUT_FILE}"
    echo "    ${MY_OUTPUT_DIR}/${MY_ID}.dita \\" >> "${MY_GENERATED_KMK}"
done

echo "" >> "${MY_GENERATED_KMK}"
exit 0
