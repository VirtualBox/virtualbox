#!/bin/bash
################################################################################
# .doc_coverage.sh                                                             #
# Script to detect overall documentation coverage of libssh. The script uses   #
# doxygen to generate the documentation then parses it's output.               #
#                                                                              #
# maintainer: Norbert Pocs <npocs@redhat.com>                                  #
################################################################################
BUILD_DIR="$1"
DOXYFILE_PATH="$BUILD_DIR/doc/Doxyfile.docs"
INDEX_XML_PATH="$BUILD_DIR/doc/xml/index.xml"
# filters
F_EXCLUDE_FILES=' wrapper.h legacy.h crypto.h priv.h chacha.h curve25519.h '
F_UNDOC_FUNC='(function).*is not documented'
F_FUNC='kind="function"'
F_HEADERS='libssh_8h_|group__libssh__'
F_CUT_BEFORE='.*<name>'
F_CUT_AFTER='<\/name><\/member>'
# Doxygen options
O_QUIET='QUIET=YES'
O_GEN_XML='GENERATE_XML=YES'

# check if build dir given
if [ $# -eq 0 ]; then
  echo "Please provide the build directory e.g.: ./build"
  exit 255
fi

# modify doxyfile to our needs:
# QUIET - less output
# GENERATE_XML - xml needed to inspect all the functions
# (note: the options are needed to be on separate lines)
# We want to exclude irrelevant files
MOD_DOXYFILE=$(cat "$DOXYFILE_PATH"; echo "$O_QUIET"; echo "$O_GEN_XML")
MOD_DOXYFILE=${MOD_DOXYFILE//EXCLUDE_PATTERNS.*=/EXCLUDE_PATTERNS=$F_EXCLUDE_FILES/g}

# call doxygen to get the warning messages
# and also generate the xml for inspection
DOXY_WARNINGS=$(echo "$MOD_DOXYFILE" | doxygen - 2>&1)

# get the number of undocumented functions
UNDOC_FUNC=$(echo "$DOXY_WARNINGS" | grep -cE "$F_UNDOC_FUNC")

# filter out the lines consisting of functions of our interest
FUNC_LINES=$(grep "$F_FUNC" "$INDEX_XML_PATH" | grep -E "$F_HEADERS")
# cut the irrelevant information and leave just the function names
ALL_FUNC=$(echo "$FUNC_LINES" | sed -e "s/$F_CUT_BEFORE//g" -e "s/$F_CUT_AFTER//")
# remove duplicates and get the number of functions
ALL_FUNC=$(echo "$ALL_FUNC" | sort - | uniq | wc -l)

# percentage of the documented functions
awk "BEGIN {printf \"Documentation coverage is %.2f%\n\", 100 - (${UNDOC_FUNC}/${ALL_FUNC}*100)}"
