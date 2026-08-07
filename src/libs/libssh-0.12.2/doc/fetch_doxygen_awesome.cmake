# Script to download doxygen-awesome-css at build time
#
# Usage:
#   cmake -P fetch_doxygen_awesome.cmake \
#         -DURL=<download_url> \
#         -DDEST_DIR=<destination_directory> \
#         -DVERSION=<version>

if(NOT DEFINED URL)
    message(FATAL_ERROR "URL not specified")
endif()
if(NOT DEFINED DEST_DIR)
    message(FATAL_ERROR "DEST_DIR not specified")
endif()
if(NOT DEFINED VERSION)
    message(FATAL_ERROR "VERSION not specified")
endif()

set(EXTRACT_DIR "${DEST_DIR}/doxygen-awesome-css-${VERSION}")

if(NOT EXISTS "${EXTRACT_DIR}/doxygen-awesome.css")
    message(STATUS "Downloading doxygen-awesome-css ${VERSION}...")
    set(TARBALL "${DEST_DIR}/doxygen-awesome-css.tar.gz")
    file(DOWNLOAD
        "${URL}"
        "${TARBALL}"
        STATUS download_status
        SHOW_PROGRESS
    )
    list(GET download_status 0 status_code)
    if(NOT status_code EQUAL 0)
        list(GET download_status 1 error_msg)
        message(FATAL_ERROR "Download failed: ${error_msg}")
    endif()
    message(STATUS "Extracting doxygen-awesome-css...")
    file(ARCHIVE_EXTRACT
        INPUT "${TARBALL}"
        DESTINATION "${DEST_DIR}"
    )
    file(REMOVE "${TARBALL}")
endif()
