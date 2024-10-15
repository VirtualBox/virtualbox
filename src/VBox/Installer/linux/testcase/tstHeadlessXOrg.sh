#!/bin/sh
# $Id: tstHeadlessXOrg.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# VirtualBox X Server auto-start service unit test.
#

#
# Copyright (C) 2012-2024 Oracle and/or its affiliates.
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

## The function definition at the start of every non-trivial shell script!
abort()
{
  ## $@, ... Error text to output to standard error in printf format.
  format="$1"
  shift
  printf "${TEST_NAME}: ${format}" "$@" >&2
  exit 1
}

## Print a TESTING line.  Takes printf arguments but without a '\n'.
print_line()
{
  format="$1"
  shift
  printf "${TEST_NAME}: TESTING ${format}... " "$@"
}

## Expected a process to complete within a certain time and call a function if
# it does which should check whether the test was successful and print status
# information.  The function takes the exit status as its single parameter.
expect_exit()
{
  PID="$1"            ## The PID we are waiting for.
  TIME_OUT="$2"       ## The time-out before we terminate the process.
  TEST_FUNCTION="$3"  ## The function to call on exit to check the test result.

  # Give it time to complete.
  { sleep "${TIME_OUT}"; kill "${PID}" 2>/dev/null; } &

  wait "${PID}"
  STATUS="$?"
  case "${STATUS}" in
  143) # SIGTERM
    printf "\nFAILED: time-out.\n"
    ;;
  *)
    ${TEST_FUNCTION} "${STATUS}"
esac
}

## Create a simple configuration file.  Add items onto the end to override them
# on an item-by-item basis.
create_basic_configuration()
{
  TEST_FOLDER="${1}"
  FILE_NAME="${TEST_FOLDER}conf"    ## The name of the configuration file to create.
  BASE_FOLDER="${TEST_FOLDER}"
  XORG_FOLDER="${TEST_FOLDER}/xorg"
  mkdir -p "${XORG_FOLDER}"
  cat > "${FILE_NAME}" << EOF
HEADLESS_X_ORG_CONFIGURATION_FOLDER="${BASE_FOLDER}/xorg"
HEADLESS_X_ORG_LOG_FOLDER="${BASE_FOLDER}/log"
HEADLESS_X_ORG_LOG_FILE="log"
HEADLESS_X_ORG_RUN_FOLDER="${BASE_FOLDER}/run"
HEADLESS_X_ORG_WAIT_FOR_PREREQUISITES="true"
HEADLESS_X_ORG_SERVER_PRE_COMMAND=
HEADLESS_X_ORG_SERVER_COMMAND="echo"
EOF

}

# Get the directory where the script is located and the parent.
OUR_FOLDER="$(dirname "$0")"
OUR_FOLDER=$(cd "${OUR_FOLDER}" && pwd)
VBOX_FOLDER=$(cd "${OUR_FOLDER}/.." && pwd)
[ -d "${VBOX_FOLDER}" ] ||
  abort "Failed to change to directory ${VBOX_FOLDER}.\n"
cd "${VBOX_FOLDER}"

# Get our name for output.
TEST_NAME="$(basename "$0" .sh)"

# And remember the full path.
TEST_NAME_FULL="${OUR_FOLDER}/$(basename "$0")"

# Create a temporary directory for configuration and logging.
TEST_FOLDER_BASE="/tmp/${TEST_NAME} 99/"  # Space in the name to test quoting.
if [ -n "${TESTBOX_PATH_SCRATCH}" ]; then
    TEST_FOLDER_BASE="${TESTBOX_PATH_SCRATCH}/${TEST_NAME} 99/"
fi
{
  rm -rf "${TEST_FOLDER_BASE}" 2>/dev/null &&
    mkdir -m 0700 "${TEST_FOLDER_BASE}" 2>/dev/null
} || abort "Could not create test folder (${TEST_FOLDER_BASE}).\n"

###############################################################################
# Simple start-up test.                                                       #
###############################################################################
print_line "simple start-up test"
create_basic_configuration "${TEST_FOLDER_BASE}simple_start-up_test/"
touch "${XORG_FOLDER}/xorg.conf.2"
touch "${XORG_FOLDER}/xorg.conf.4"

test_simple_start_up()
{
  STATUS="$1"
  case "${STATUS}" in
  0)
    LOG_FOLDER="${TEST_FOLDER}/log"
    LOG="${LOG_FOLDER}/log"
    if grep -q "2 ${XORG_FOLDER}/xorg.conf.2 ${LOG_FOLDER}/Xorg.2.log" "${LOG}" &&
      grep -q "4 ${XORG_FOLDER}/xorg.conf.4 ${LOG_FOLDER}/Xorg.4.log" "${LOG}"; then
      printf "SUCCESS.\n"
    else
      printf "\nFAILED: incorrect log output.\n"
    fi
    ;;
  *)
    printf "\nFAILED: exit status ${STATUS}.\n"
  esac
}

scripts/VBoxHeadlessXOrg.sh -c "${TEST_FOLDER}conf" &
PID=$!
expect_exit "${PID}" 5 test_simple_start_up

###############################################################################
# No configuration files.                                                     #
###############################################################################
create_basic_configuration "${TEST_FOLDER_BASE}no_configuration_files/"
print_line "no configuration files"

test_should_fail()
{
  STATUS="$1"
  case "${STATUS}" in
  0)
    printf "\nFAILED: successful exit when an error was expected.\n"
    ;;
  *)
    printf "SUCCESS.\n"  # At least it behaved the way we wanted.
  esac
}

scripts/VBoxHeadlessXOrg.sh -c "${TEST_FOLDER}conf" &
PID=$!
expect_exit "${PID}" 5 test_should_fail

###############################################################################
# Bad configuration files.                                                    #
###############################################################################
print_line "bad configuration files"
create_basic_configuration "${TEST_FOLDER_BASE}bad_configuration_files/"
touch "${XORG_FOLDER}/xorg.conf.2"
touch "${XORG_FOLDER}/xorg.conf.4"
touch "${XORG_FOLDER}/xorg.conf.other"
scripts/VBoxHeadlessXOrg.sh -c "${TEST_FOLDER}conf" &
PID=$!
expect_exit "${PID}" 5 test_should_fail

###############################################################################
# Long running server command.                                                #
###############################################################################

# Set up a configuration file for a long-running command.
create_basic_configuration "${TEST_FOLDER_BASE}long-running_command/"
cat >> "${TEST_FOLDER}conf" << EOF
HEADLESS_X_ORG_SERVER_COMMAND="${TEST_FOLDER}command.sh"
EOF

cat > "${TEST_FOLDER}command.sh" << EOF
#!/bin/sh
touch "${TEST_FOLDER}stopped"
touch "${TEST_FOLDER}started"
trap "touch \\"${TEST_FOLDER}stopped\\"; exit" TERM
rm "${TEST_FOLDER}stopped"
while true; do :; done
EOF
chmod a+x "${TEST_FOLDER}command.sh"

print_line "long running server command"
touch "${XORG_FOLDER}/xorg.conf.5"
FAILURE=""
scripts/VBoxHeadlessXOrg.sh -c "${TEST_FOLDER}conf" &
PID="$!"
while [ ! -f "${TEST_FOLDER}started" ]; do :; done
while [ -f "${TEST_FOLDER}stopped" ]; do :; done
[ -n "${PID}" ] && kill "${PID}" 2>/dev/null
while [ ! -f "${TEST_FOLDER}stopped" ]; do :; done
printf "SUCCESS.\n"

###############################################################################
# Pre-requisite test.                                                         #
###############################################################################

# Set up a configuration file with a pre-requisite.
create_basic_configuration "${TEST_FOLDER_BASE}pre-requisite/"
cat >> "${TEST_FOLDER}conf" << EOF
HEADLESS_X_ORG_WAIT_FOR_PREREQUISITES="false"
EOF

print_line "configuration file with failed pre-requisite"
touch "${XORG_FOLDER}/xorg.conf.2"
touch "${XORG_FOLDER}/xorg.conf.4"
if scripts/VBoxHeadlessXOrg.sh -c "${TEST_FOLDER}conf"; then
  echo "\nFAILED to stop for failed pre-requisite.\n"
else
  echo "SUCCESS"
fi

###############################################################################
# Pre-command test.                                                           #
###############################################################################

# Set up our pre-command test configuration file.
create_basic_configuration "${TEST_FOLDER_BASE}pre-command/"

cat >> "${TEST_FOLDER}conf" << EOF
test_pre_command_server_pre_command()
{
  touch "${TEST_FOLDER}/run/pre"
}
test_pre_command_server_command()
{
  cp "${TEST_FOLDER}/run/pre" "${TEST_FOLDER}/run/pre2"
}
HEADLESS_X_ORG_SERVER_PRE_COMMAND="test_pre_command_server_pre_command"
HEADLESS_X_ORG_SERVER_COMMAND="test_pre_command_server_command"
EOF

print_line "pre-command test"
touch "${XORG_FOLDER}/xorg.conf.2"

test_pre_command()
{
  STATUS="$1"
  case "${STATUS}" in
  0)
    LOG_FOLDER="${TEST_FOLDER}/log"
    LOG="${LOG_FOLDER}/log"
    if [ -e "${TEST_FOLDER}/run/pre" ] && [ -e "${TEST_FOLDER}/run/pre2" ]; then
      printf "SUCCESS.\n"
    else
      printf "\nFAILED: pre-command not executed.\n"
    fi
    ;;
  *)
    printf "\nFAILED: exit status ${STATUS}.\n"
  esac
}

rm -f "${TEST_FOLDER}/run/pre"
scripts/VBoxHeadlessXOrg.sh -c "${TEST_FOLDER}conf" &
PID=$!
expect_exit "${PID}" 5 test_pre_command

###############################################################################
# Post-command test.                                                          #
###############################################################################

# Set up our post-command test configuration file.
create_basic_configuration "${TEST_FOLDER_BASE}post-command/"
cat >> "${TEST_FOLDER}conf" << EOF
test_post_command_post_command()
{
  echo "\${1}" > "${TEST_FOLDER}/run/post"
}
HEADLESS_X_ORG_SERVER_POST_COMMAND="test_post_command_post_command"
EOF

print_line "post-command test"
touch "${XORG_FOLDER}/xorg.conf.2"
touch "${XORG_FOLDER}/xorg.conf.4"

test_post_command()
{
  STATUS="$1"
  case "${STATUS}" in
  0)
    LOG_FOLDER="${TEST_FOLDER}/log"
    LOG="${LOG_FOLDER}/log"
    if grep -q "2 4" "${TEST_FOLDER}/run/post"; then
      printf "SUCCESS.\n"
    else
      printf "\nFAILED: post-command not executed.\n"
    fi
    ;;
  *)
    printf "\nFAILED: exit status ${STATUS}.\n"
  esac
}

rm -f "${TEST_FOLDER}/run/post"
scripts/VBoxHeadlessXOrg.sh -c "${TEST_FOLDER}conf" &
PID=$!
expect_exit "${PID}" 5 test_post_command
