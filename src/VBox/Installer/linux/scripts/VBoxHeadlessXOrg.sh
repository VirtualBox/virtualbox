#!/bin/sh
# $Id: VBoxHeadlessXOrg.sh 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $
## @file
# VirtualBox X Server auto-start service.
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

PATH=$PATH:/bin:/sbin:/usr/sbin

## Start one or several X servers in the background for use with headless
# rendering.  For details, options and configuration see the usage() function
# further down.
#
# I have tried to follow the best practices I could find for writing a Linux
# service (and doing it in shell script) which should work well with
# traditional and modern service systems using minimal init or service files.
# In our case this boils down to:
#  * Start with a single command line, stop using one of ${EXIT_SIGNALS} below.
#  * Stopping with a signal can be done safely using the pid stored in the
#    pid-file and our (presumably unique) command name.  For this reason we
#    only support running one instance of the service though.
#  * Start in the foreground.  Systems without proper service control can take
#    care of the backgrounding in the init script.
#  * Clean up all sub-processes (X servers) ourselves when we are stopped
#    cleanly and don't provide any other way to clean them up automatically (in
#    case we are stopped uncleanly) as we don't know of a generic safe way to
#    do so, though some service management systems (i.e. systemd) can do so.
#    (A more thorough automatic clean-up would be possible if Xorg didn't
#    potentially have to be run as root, so that we could run all processes
#    using a service-specific user account and just terminate all processes
#    run by that user to clean up.)

## Default configuration file name.
# @note This is not very nice - /etc/default is actually Debian-specific.
CONFIGURATION_FILE=/etc/default/virtualbox
## The name of this script.
SCRIPT_NAME="$0"
## The service name.
SERVICE_NAME="vboxheadlessxorg"
## The service description.
SERVICE_DESCRIPTION="Headless rendering service"
## Signals and conditions which may be used to terminate the service.
EXIT_SIGNALS="EXIT HUP INT QUIT ABRT TERM"
## The default run-time data folder.
DEFAULT_RUN_FOLDER="/var/run/${SERVICE_NAME}/"
## The default X server configuration directory.
DEFAULT_CONFIGURATION_FOLDER="${DEFAULT_RUN_FOLDER}/xorg.conf.d/"
## The extra data key used to provide the list of available X server displays.
EXTRA_DATA_KEY_DISPLAYS="HeadlessXServer/Displays"
## The extra data key used to specify the X server authority file.
EXTRA_DATA_KEY_AUTH="HeadlessXServer/AuthFile"

## Print usage information for the service script.
## @todo Perhaps we should support some of the configuration file options from
#        the command line.  Opinions welcome.
## @todo Possibly extract this information for the user manual.
usage() {
  cat << EOF
Usage:

  $(basename "${SCRIPT_NAME}") [<options>]

Start one or several X servers in the background for use with headless
rendering.  We only support X.Org Server at the moment.  On service start-up available graphics devices are detected and an X server configuration file is
generated for each.  We attempt to start an X server process for each
configuration file.  The process is configurable by setting values in a file as
described below.

Options:

  -c|--conf-file         Specify an alternative locations for the configuration
                         file.  The default location is:
                           "${CONFIGURATION_FILE}"

  --help|--usage         Print this text.

The optional configuration file should contain a series of lines of the form
"KEY=value".  It will be read in as a command shell sub-script.  Here is the
current list of possible key settings with a short explanation.  Usually it
should be sufficient to change the value of \${HEADLESS_X_ORG_USERS} and to
leave all other settings unchanged.

  HEADLESS_X_ORG_CONFIGURATION_FOLDER
    The folder where the X server configuration files are to be created.

  HEADLESS_X_ORG_LOG_FOLDER
    The folder where log files will be saved.

  HEADLESS_X_ORG_LOG_FILE
    The main log file name.

  HEADLESS_X_ORG_RUN_FOLDER
    The folder to store run-time data in.

  HEADLESS_X_ORG_WAIT_FOR_PREREQUISITES
    Command to execute to wait until all dependencies for the X servers are
    available.  The default command waits until the udev event queue has
    settled.  The command may return failure to signal that it has given up.
    No arguments may be passsed.

  HEADLESS_X_ORG_USERS
    List of users who will have access to the X servers started and for whom we
    will provide the configuration details via VirtualBox extra data.  This
    variable is only used by the commands in the default configuration
    (\${HEADLESS_X_ORG_SERVER_PRE_COMMAND} and
    \${HEADLESS_X_ORG_SERVER_POST_COMMAND}), and not by the service itself.

  HEADLESS_X_ORG_FIRST_DISPLAY
    The first display number which will be used for a started X server.  The
    others will use the following numbers.

  HEADLESS_X_ORG_SERVER_PRE_COMMAND
    Command to execute once to perform any set-up needed before starting the
    X servers, such as setting up the X server authentication.  The default
    command creates an authority file for each of the users in the list
    \${HEADLESS_X_ORG_USERS} and generates server configuration files for all
    detected graphics cards.  No arguments may be passed.

  HEADLESS_X_ORG_SERVER_COMMAND
    The default X server start-up command.  It will be passed three parameters
    - in order, the screen number to use, the path of the X.Org configuration
    file to use and the path of the X server log file to create.

  HEADLESS_X_ORG_SERVER_POST_COMMAND
    Command to execute once the X servers have been successfully started.  It
    will be passed a single parameter which is a space-separated list of the
    X server screen numbers.  By default this stores the service configuration
    information to VirtualBox extra data for each of the users in the list
    from the variable HEADLESS_X_ORG_USERS: the list of displays is set to the
    key "${EXTRA_DATA_KEY_DISPLAYS}" and the path of the authority file to
    "${EXTRA_DATA_KEY_AUTH}".
EOF
}

# Default configuration.
HEADLESS_X_ORG_CONFIGURATION_FOLDER="${DEFAULT_CONFIGURATION_FOLDER}"
HEADLESS_X_ORG_LOG_FOLDER="/var/log/${SERVICE_NAME}"
HEADLESS_X_ORG_LOG_FILE="${SERVICE_NAME}.log"
HEADLESS_X_ORG_RUN_FOLDER="/var/run/${SERVICE_NAME}"
HEADLESS_X_ORG_USERS=""
HEADLESS_X_ORG_FIRST_DISPLAY=40
X_AUTH_FILE="${HEADLESS_X_ORG_RUN_FOLDER}/xauth"

default_wait_for_prerequisites()
{
    udevadm settle || udevsettle # Fails if no udevadm.
}
HEADLESS_X_ORG_WAIT_FOR_PREREQUISITES="default_wait_for_prerequisites"

default_pre_command()
{
  # Create new authority file.
  echo > "${X_AUTH_FILE}"
  # Create the xorg.conf files.
  mkdir -p "${HEADLESS_X_ORG_CONFIGURATION_FOLDER}" || return 1
  display="${HEADLESS_X_ORG_FIRST_DISPLAY}"
  for i in /sys/bus/pci/devices/*; do
    read class < "${i}/class"
    case "${class}" in *03????)
      address="${i##*/}"
      address="${address%%:*}${address#*:}"
      address="PCI:${address%%.*}:${address#*.}"
      read vendor < "${i}/vendor"
      case "${vendor}" in *10de|*10DE)  # NVIDIA
        cat > "${HEADLESS_X_ORG_CONFIGURATION_FOLDER}/xorg.conf.${display}" << EOF
Section "Module"
    Load       "glx"
EndSection
Section "Device"
    Identifier "Device${display}"
    Driver     "nvidia"
    Option     "UseDisplayDevice" "none"
EndSection
Section "Screen"
    Identifier "Screen${display}"
    Device     "Device${display}"
EndSection
Section "ServerLayout"
    Identifier "Layout${display}"
    Screen     "Screen${display}"
    Option     "AllowMouseOpenFail" "true"
    Option     "AutoAddDevices"     "false"
    Option     "AutoAddGPU"         "false"
    Option     "AutoEnableDevices"  "false"
    Option     "IsolateDevice"      "${address}"
EndSection
EOF
      esac
      # Add key to the authority file.
      key="$(dd if=/dev/urandom count=1 bs=16 2>/dev/null | od -An -x)"
      xauth -f "${X_AUTH_FILE}" add :${display} . "${key}"
      display=`expr ${display} + 1`
    esac
  done
  # Duplicate the authority file.
  for i in ${HEADLESS_X_ORG_USERS}; do
    cp "${X_AUTH_FILE}" "${X_AUTH_FILE}.${i}"
    chown "${i}" "${X_AUTH_FILE}.${i}"
  done
}
HEADLESS_X_ORG_SERVER_PRE_COMMAND="default_pre_command"

default_command()
{
  auth="${HEADLESS_X_ORG_RUN_FOLDER}/xauth"
  # screen=$1
  # conf_file=$2
  # log_file=$3
  trap "kill \${PID}; sleep 5; kill -KILL \${PID} 2>/dev/null" ${EXIT_SIGNALS}
  Xorg :"${1}" -auth "${auth}" -config "${2}" -logverbose 0 -logfile /dev/null -verbose 7 > "${3}" 2>&1 &
  PID="$!"
  wait
  exit
}
HEADLESS_X_ORG_SERVER_COMMAND="default_command"

default_post_command()
{
  # screens=$1
  for i in ${HEADLESS_X_ORG_USERS}; do
    su ${i} -c "VBoxManage setextradata global ${EXTRA_DATA_KEY_DISPLAYS} \"${1}\""
    su ${i} -c "VBoxManage setextradata global ${EXTRA_DATA_KEY_AUTH} \"${HEADLESS_X_ORG_RUN_FOLDER}/xauth\""
  done
}
HEADLESS_X_ORG_SERVER_POST_COMMAND="default_post_command"

## The function definition at the start of every non-trivial shell script!
abort() {
  ## $@, ... Error text to output to standard error in printf format.
  printf "$@" >&2
  exit 1
}

## Milder version of abort, when we can't continue because of a valid condition.
abandon() {
  ## $@, ... Text to output to standard error in printf format.
  printf "$@" >&2
  exit 0
}

abort_usage() {
  usage >&2
  abort "$@"
}

# Print a banner message
banner() {
  cat << EOF
${VBOX_PRODUCT} VBoxHeadless X Server start-up service Version ${VBOX_VERSION_STRING}
(C) 2005-${VBOX_C_YEAR} ${VBOX_VENDOR}
All rights reserved.

EOF
}

# Get the directory where the script is located.
SCRIPT_FOLDER=$(dirname "${SCRIPT_NAME}")"/"
[ -r "${SCRIPT_FOLDER}generated.sh" ] ||
  abort "${LOG_FILE}" "Failed to find installation information.\n"
. "${SCRIPT_FOLDER}generated.sh"

# Parse our arguments.
while [ "$#" -gt 0 ]; do
  case $1 in
    -c|--conf-file)
      [ "$#" -gt 1 ] ||
      {
        banner
        abort "%s requires at least one argument.\n" "$1"
      }
      CONFIGURATION_FILE="$2"
      shift
      ;;
    --help|--usage)
      banner
      usage
      exit 0
      ;;
    *)
      banner
      abort_usage "Unknown argument $1.\n"
      ;;
  esac
  shift
done

[ -r "${CONFIGURATION_FILE}" ] && . "${CONFIGURATION_FILE}"

# Change to the root directory so we don't hold any other open.
cd /

# If something fails here we will catch it when we create the directory.
[ -e "${HEADLESS_X_ORG_LOG_FOLDER}" ] &&
  [ -d "${HEADLESS_X_ORG_LOG_FOLDER}" ] &&
  rm -rf "${HEADLESS_X_ORG_LOG_FOLDER}.old" 2> /dev/null &&
mv "${HEADLESS_X_ORG_LOG_FOLDER}" "${HEADLESS_X_ORG_LOG_FOLDER}.old" 2> /dev/null
mkdir -p "${HEADLESS_X_ORG_LOG_FOLDER}" 2>/dev/null ||
{
  banner
  abort "Failed to create log folder \"${HEADLESS_X_ORG_LOG_FOLDER}\".\n"
}
mkdir -p "${HEADLESS_X_ORG_RUN_FOLDER}" 2>/dev/null ||
{
  banner
  abort "Failed to create run folder \"${HEADLESS_X_ORG_RUN_FOLDER}\".\n"
}
exec > "${HEADLESS_X_ORG_LOG_FOLDER}/${HEADLESS_X_ORG_LOG_FILE}" 2>&1

banner

# Wait for our dependencies to become available.
if [ -n "${HEADLESS_X_ORG_WAIT_FOR_PREREQUISITES}" ]; then
  "${HEADLESS_X_ORG_WAIT_FOR_PREREQUISITES}" ||
    abort "Service prerequisites not available.\n"
fi

# Do any pre-start setup.
if [ -n "${HEADLESS_X_ORG_SERVER_PRE_COMMAND}" ]; then
  "${HEADLESS_X_ORG_SERVER_PRE_COMMAND}" ||
    abort "Pre-requisite failed.\n"
fi

X_SERVER_PIDS=""
X_SERVER_SCREENS=""
trap "kill \${X_SERVER_PIDS} 2>/dev/null" ${EXIT_SIGNALS}
space=""  # Hack to put spaces between the pids but not before or after.
for conf_file in "${HEADLESS_X_ORG_CONFIGURATION_FOLDER}"/*; do
  [ x"${conf_file}" = x"${HEADLESS_X_ORG_CONFIGURATION_FOLDER}/*" ] &&
    ! [ -e "${conf_file}" ] &&
    abort "No configuration files found.\n"
  filename="$(basename "${conf_file}")"
  screen="$(expr "${filename}" : "xorg\.conf\.\(.*\)")"
  [ 0 -le "${screen}" ] 2>/dev/null ||
    abort "Badly formed file name \"${conf_file}\".\n"
  log_file="${HEADLESS_X_ORG_LOG_FOLDER}/Xorg.${screen}.log"
  "${HEADLESS_X_ORG_SERVER_COMMAND}" "${screen}" "${conf_file}" "${log_file}" &
  X_SERVER_PIDS="${X_SERVER_PIDS}${space}$!"
  X_SERVER_SCREENS="${X_SERVER_SCREENS}${space}${screen}"
  space=" "
done

# Do any post-start work.
if [ -n "${HEADLESS_X_ORG_SERVER_POST_COMMAND}" ]; then
  "${HEADLESS_X_ORG_SERVER_POST_COMMAND}" "${X_SERVER_SCREENS}" ||
    abort "Post-command failed.\n"
fi

wait
