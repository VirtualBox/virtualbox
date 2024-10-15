#!/usr/bin/env kmk_ash
# $Id: docbook-changelog-to-manual-dita.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# Helper script for converting the changelog into a ditamap and a topic
# file per version.
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
# Globals.
#
MY_SED=kmk_sed

#
# This script is very internal, so we got the following fixed position parameters:
#   1: user_ChangeLogImpl.xml to use as input.
#   2: docbook-changelog-to-manual-dita.xsl
#   5: The out directory.
#   6: '--'
#   7+: xsltproc invocation (sans output, input and xslt file).
#
if test $# -lt 8; then
    echo "syntax error: too few arguments" 1>&2;
    exit 2;
fi
MY_INPUT_FILE="$1"
MY_XSLT="$2"
MY_GENERATED_KMK="$3"
MY_GENERATED_KMK_VARIABLE="$4"
MY_OUTPUT_DIR="$5"
if test "$6" != "--"; then
    echo "syntax error: Expected '--' as the 6th parameter, got: $6" 1>&2;
    exit 2;
fi
shift 6

if ! test -f "${MY_INPUT_FILE}"; then
    echo "error: Input file does not exists or is not a regular file: ${MY_INPUT_FILE}" 1>&2;
    exit 1;
fi
if ! test -f "${MY_XSLT}"; then
    echo "error: The given dita-refentry-flat-topic-ids.xsl file does not exists or is not a regular file: ${MY_XSLT_TOPIC_IDS}" 1>&2;
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
# We drop the first line with the <?xml ... ?> stuff.
#
MY_TOPIC_IDS=$($* --stringparam g_sMode ids "${MY_XSLT}" "${MY_INPUT_FILE}" | ${MY_SED} -e 1d)

echo "${MY_GENERATED_KMK_VARIABLE} += \\" > "${MY_GENERATED_KMK}"

#
# Extract each topic.
#
for MY_ID in ${MY_TOPIC_IDS};
do
    $* \
        --stringparam g_sMode topic \
        --stringparam g_idTopic "${MY_ID}" \
        --output "${MY_OUTPUT_DIR}/${MY_ID}.dita" "${MY_XSLT}" "${MY_INPUT_FILE}"
    echo "    ${MY_OUTPUT_DIR}/${MY_ID}.dita \\" >> "${MY_GENERATED_KMK}"
done

#
# Now for the ditamap file.
#
$* --stringparam g_sMode map --output "${MY_OUTPUT_DIR}/changelog-versions.ditamap" "${MY_XSLT}" "${MY_INPUT_FILE}"
echo "" >> "${MY_GENERATED_KMK}"
exit 0
