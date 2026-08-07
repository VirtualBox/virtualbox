# - Try to find softhsm
# Once done this will define
#
#  SOFTHSM_FOUND - system has softhsm
#  SOFTHSM_LIBRARIES - Link these to use softhsm
#
#=============================================================================
#  Copyright (c) 2019 Sahana Prasad <sahana@redhat.com>
#
#  Distributed under the OSI-approved BSD License (the "License");
#  see accompanying file Copyright.txt for details.
#
#  This software is distributed WITHOUT ANY WARRANTY; without even the
#  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#  See the License for more information.
#=============================================================================
#


find_library(SOFTHSM2_LIBRARY
    NAMES
        softhsm2
)

if (SOFTHSM2_LIBRARY)
    set(SOFTHSM_LIBRARIES
        ${SOFTHSM_LIBRARIES}
        ${SOFTHSM2_LIBRARY}
    )
endif (SOFTHSM2_LIBRARY)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(softhsm DEFAULT_MSG SOFTHSM_LIBRARIES)

# show the SOFTHSM_INCLUDE_DIR and SOFTHSM_LIBRARIES variables only in the advanced view
mark_as_advanced(SOFTHSM_LIBRARIES)
