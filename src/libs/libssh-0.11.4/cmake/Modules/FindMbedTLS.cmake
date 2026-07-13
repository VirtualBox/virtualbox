# - Try to find mbedTLS
# Once done this will define
#
#  MBEDTLS_FOUND - system has mbedTLS
#  MBEDTLS_INCLUDE_DIRS - the mbedTLS include directory
#  MBEDTLS_LIBRARIES - Link these to use mbedTLS
#  MBEDTLS_DEFINITIONS - Compiler switches required for using mbedTLS
#=============================================================================
#  Copyright (c) 2017 Sartura d.o.o.
#
#  Author: Juraj Vijtiuk <juraj.vijtiuk@sartura.hr>
#
#  Distributed under the OSI-approved BSD License (the "License");
#  see accompanying file Copyright.txt for details.
#
#  This software is distributed WITHOUT ANY WARRANTY; without even the
#  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#  See the License for more information.
#=============================================================================
#


set(_MBEDTLS_ROOT_HINTS
    $ENV{MBEDTLS_ROOT_DIR}
    ${MBEDTLS_ROOT_DIR})

set(_MBEDTLS_ROOT_PATHS
    "$ENV{PROGRAMFILES}/libmbedtls")

set(_MBEDTLS_ROOT_HINTS_AND_PATHS
    HINTS ${_MBEDTLS_ROOT_HINTS}
    PATHS ${_MBEDTLS_ROOT_PATHS})


find_path(MBEDTLS_INCLUDE_DIR
    NAMES
        mbedtls/ssl.h
    HINTS
        ${_MBEDTLS_ROOT_HINTS_AND_PATHS}
    PATH_SUFFIXES
       include
)

find_library(MBEDTLS_SSL_LIBRARY
        NAMES
            mbedtls
        HINTS
            ${_MBEDTLS_ROOT_HINTS_AND_PATHS}
        PATH_SUFFIXES
            lib

)

find_library(MBEDTLS_CRYPTO_LIBRARY
        NAMES
            mbedcrypto
        HINTS
            ${_MBEDTLS_ROOT_HINTS_AND_PATHS}
        PATH_SUFFIXES
            lib
)

find_library(MBEDTLS_X509_LIBRARY
        NAMES
            mbedx509
        HINTS
            ${_MBEDTLS_ROOT_HINTS_AND_PATHS}
        PATH_SUFFIXES
            lib
)

set(MBEDTLS_LIBRARIES ${MBEDTLS_SSL_LIBRARY} ${MBEDTLS_CRYPTO_LIBRARY}
        ${MBEDTLS_X509_LIBRARY})

if (MBEDTLS_INCLUDE_DIR AND EXISTS "${MBEDTLS_INCLUDE_DIR}/mbedtls/version.h")
    # mbedtls 2.8
    file(STRINGS "${MBEDTLS_INCLUDE_DIR}/mbedtls/version.h" _mbedtls_version_str REGEX
            "^#[\t ]*define[\t ]+MBEDTLS_VERSION_STRING[\t ]+\"[0-9]+.[0-9]+.[0-9]+\"")

    string(REGEX REPLACE "^.*MBEDTLS_VERSION_STRING.*([0-9]+.[0-9]+.[0-9]+).*"
            "\\1" MBEDTLS_VERSION "${_mbedtls_version_str}")
elseif (MBEDTLS_INCLUDE_DIR AND EXISTS "${MBEDTLS_INCLUDE_DIR}/mbedtls/build_info.h")
    # mbedtls 3.6
    file(STRINGS "${MBEDTLS_INCLUDE_DIR}/mbedtls/version.h" _mbedtls_version_str REGEX
            "^#[\t ]*define[\t ]+MBEDTLS_VERSION_STRING[\t ]+\"[0-9]+.[0-9]+.[0-9]+\"")

    string(REGEX REPLACE "^.*MBEDTLS_VERSION_STRING.*([0-9]+.[0-9]+.[0-9]+).*"
            "\\1" MBEDTLS_VERSION "${_mbedtls_version_str}")
endif ()

include(FindPackageHandleStandardArgs)
if (MBEDTLS_VERSION)
    find_package_handle_standard_args(MbedTLS
        REQUIRED_VARS
            MBEDTLS_INCLUDE_DIR
            MBEDTLS_LIBRARIES
        VERSION_VAR
            MBEDTLS_VERSION
        FAIL_MESSAGE
            "Could NOT find mbedTLS, try to set the path to mbedTLS root folder
            in the system variable MBEDTLS_ROOT_DIR"
    )
else (MBEDTLS_VERSION)
    find_package_handle_standard_args(MbedTLS
        "Could NOT find mbedTLS, try to set the path to mbedTLS root folder in
        the system variable MBEDTLS_ROOT_DIR"
        MBEDTLS_INCLUDE_DIR
        MBEDTLS_LIBRARIES)
endif (MBEDTLS_VERSION)

# show the MBEDTLS_INCLUDE_DIRS and MBEDTLS_LIBRARIES variables only in the advanced view
mark_as_advanced(MBEDTLS_INCLUDE_DIR MBEDTLS_LIBRARIES)

if(MBEDTLS_FOUND)
  if(NOT TARGET MbedTLS::mbedcrypto)
      add_library(MbedTLS::mbedcrypto UNKNOWN IMPORTED)
      set_target_properties(MbedTLS::mbedcrypto PROPERTIES
                            INTERFACE_INCLUDE_DIRECTORIES "${MBEDTLS_INCLUDE_DIR}"
                            INTERFACE_LINK_LIBRARIES MbedTLS::mbedcrypto
                            IMPORTED_LINK_INTERFACE_LANGUAGES "C"
                            IMPORTED_LOCATION "${MBEDTLS_CRYPTO_LIBRARY}")
  endif()

  if(NOT TARGET MbedTLS::mbedx509)
      add_library(MbedTLS::mbedx509 UNKNOWN IMPORTED)
      set_target_properties(MbedTLS::mbedx509 PROPERTIES
                            INTERFACE_INCLUDE_DIRECTORIES "${MBEDTLS_INCLUDE_DIR}"
                            INTERFACE_LINK_LIBRARIES MbedTLS::mbedx509
                            IMPORTED_LINK_INTERFACE_LANGUAGES "C"
                            IMPORTED_LOCATION "${MBEDTLS_X509_LIBRARY}")
  endif()

  if(NOT TARGET MbedTLS::mbedtls)
      add_library(MbedTLS::mbedtls UNKNOWN IMPORTED)
      set_target_properties(MbedTLS::mbedtls PROPERTIES
                            INTERFACE_INCLUDE_DIRECTORIES "${MBEDTLS_INCLUDE_DIR}"
                            INTERFACE_LINK_LIBRARIES MbedTLS::mbedtls
                            IMPORTED_LINK_INTERFACE_LANGUAGES "C"
                            IMPORTED_LOCATION "${MBEDTLS_LIBRARY}")
  endif()
endif()
