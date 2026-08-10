# - Try to find libfido2
# Once done this will define
#
#  LIBFIDO2_ROOT_DIR - Set this variable to the root installation of libfido2
#
# Read-Only variables:
#  LIBFIDO2_FOUND - system has libfido2
#  LIBFIDO2_INCLUDE_DIR - the libfido2 include directory
#  LIBFIDO2_LIBRARIES - Link these to use libfido2
#
# The libfido2 library provides support for communicating
# with FIDO2/U2F devices over USB/NFC.
#
#  Copyright (c) 2025 Praneeth Sarode <praneethsarode@gmail.com>
#
#  Redistribution and use is allowed according to the terms of the New
#  BSD license.
#  For details see the accompanying COPYING-CMAKE-SCRIPTS file.
#


set(_LIBFIDO2_ROOT_HINTS
    $ENV{LIBFIDO2_ROOT_DIR}
    ${LIBFIDO2_ROOT_DIR}
)

set(_LIBFIDO2_ROOT_PATHS
    "$ENV{PROGRAMFILES}/libfido2"
)

set(_LIBFIDO2_ROOT_HINTS_AND_PATHS
    HINTS ${_LIBFIDO2_ROOT_HINTS}
    PATHS ${_LIBFIDO2_ROOT_PATHS}
)

find_path(LIBFIDO2_INCLUDE_DIR
    NAMES
        fido.h
    HINTS
        ${_LIBFIDO2_ROOT_HINTS_AND_PATHS}
    PATH_SUFFIXES
        include
)

find_library(LIBFIDO2_LIBRARY
    NAMES
        fido2
    HINTS
        ${_LIBFIDO2_ROOT_HINTS_AND_PATHS}
    PATH_SUFFIXES
        lib
        lib64
)

set(LIBFIDO2_LIBRARIES
    ${LIBFIDO2_LIBRARY}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(libfido2 DEFAULT_MSG LIBFIDO2_LIBRARIES LIBFIDO2_INCLUDE_DIR)

# show the LIBFIDO2_INCLUDE_DIR and LIBFIDO2_LIBRARIES variables only in the advanced view
mark_as_advanced(LIBFIDO2_INCLUDE_DIR LIBFIDO2_LIBRARIES)
