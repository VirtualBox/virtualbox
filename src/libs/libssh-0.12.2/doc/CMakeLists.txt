#
# Build the documentation
#
# To build the documentation with a local doxygen-awesome-css directory:
#
#   cmake -S . -B obj \
#         -DDOXYGEN_AWESOME_CSS_DIR=/path/to/doxygen-awesome-css
#   cmake --build obj --target docs
#
# The tarball can be downloaded from:
#   https://github.com/jothepro/doxygen-awesome-css/archive/refs/tags/v2.4.1.tar.gz
#
find_package(Doxygen)

if (DOXYGEN_FOUND)
    set(DOXYGEN_AWESOME_CSS_PROJECT
        "https://github.com/jothepro/doxygen-awesome-css")
    set(DOXYGEN_AWESOME_CSS_VERSION "2.4.1")
    set(DOXYGEN_AWESOME_CSS_URL
        "${DOXYGEN_AWESOME_CSS_PROJECT}/archive/refs/tags/v${DOXYGEN_AWESOME_CSS_VERSION}.tar.gz"
    )

    # Allow specifying a local doxygen-awesome-css directory (useful for
    # packaging)
    if (NOT DEFINED DOXYGEN_AWESOME_CSS_DIR)
        # Custom target to download doxygen-awesome-css at build time
        add_custom_target(
            doxygen-awesome-css
            COMMAND
                ${CMAKE_COMMAND} -DURL=${DOXYGEN_AWESOME_CSS_URL}
                -DDEST_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DVERSION=${DOXYGEN_AWESOME_CSS_VERSION} -P
                ${CMAKE_CURRENT_SOURCE_DIR}/fetch_doxygen_awesome.cmake
            COMMENT "Fetching doxygen-awesome-css theme")

        set(AWESOME_CSS_DIR
            "${CMAKE_CURRENT_BINARY_DIR}/doxygen-awesome-css-${DOXYGEN_AWESOME_CSS_VERSION}"
        )
    else ()
        message(
            STATUS
                "Using doxygen-awesome-css from ${DOXYGEN_AWESOME_CSS_DIR}")
        set(AWESOME_CSS_DIR "${DOXYGEN_AWESOME_CSS_DIR}")
    endif ()

    # Project title shown in documentation
    set(DOXYGEN_PROJECT_NAME ${PROJECT_NAME})
    # Project version number shown in documentation
    set(DOXYGEN_PROJECT_NUMBER ${PROJECT_VERSION})
    # Brief description shown below project name
    set(DOXYGEN_PROJECT_BRIEF "The SSH library")
    # Project favicon (browser tab icon)
    set(DOXYGEN_PROJECT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/favicon.png)

    # Number of spaces used for indentation in code blocks
    set(DOXYGEN_TAB_SIZE 4)
    # Generate output optimized for C (vs C++)
    set(DOXYGEN_OPTIMIZE_OUTPUT_FOR_C YES)
    # Enable parsing of markdown in comments
    set(DOXYGEN_MARKDOWN_SUPPORT YES)
    # Warn about undocumented members to improve documentation quality
    set(DOXYGEN_WARN_IF_UNDOCUMENTED YES)
    # Do not extract private class members
    set(DOXYGEN_EXTRACT_PRIVATE NO)
    if (WITH_INTERNAL_DOC)
        # Include internal documentation
        set(DOXYGEN_INTERNAL_DOCS YES)
    else ()
        # Do not include internal documentation
        set(DOXYGEN_INTERNAL_DOCS NO)
    endif( WITH_INTERNAL_DOC)
    # Disable built-in clipboard (using doxygen-awesome extension instead)
    set(DOXYGEN_HTML_COPY_CLIPBOARD NO)
    # Disable page outline panel (using interactive TOC extension instead)
    set(DOXYGEN_PAGE_OUTLINE_PANEL NO)

    # Required configuration for doxygen-awesome-css theme Generate treeview
    # sidebar for navigation
    set(DOXYGEN_GENERATE_TREEVIEW YES)
    # Enable default index pages
    set(DOXYGEN_DISABLE_INDEX NO)
    # Use top navigation bar instead of full sidebar (required for theme
    # compatibility)
    set(DOXYGEN_FULL_SIDEBAR NO)
    # Use light color style (required for Doxygen >= 1.9.5)
    set(DOXYGEN_HTML_COLORSTYLE LIGHT)

    # Disable diagram generation (not relevant for C projects)
    set(DOXYGEN_HAVE_DOT NO)
    set(DOXYGEN_CLASS_DIAGRAMS NO)
    set(DOXYGEN_CALL_GRAPH NO)
    set(DOXYGEN_CALLER_GRAPH NO)

    # Preprocessor defines to use when parsing code
    set(DOXYGEN_PREDEFINED DOXYGEN WITH_SERVER WITH_SFTP
                           PRINTF_ATTRIBUTE\(x,y\))

    # Exclude patterns for files we don't want to document
    set(DOXYGEN_EXCLUDE_PATTERNS */src/external/* fe25519.h ge25519.h sc25519.h
                                 blf.h)
    # Exclude internal structures from documentation
    set(DOXYGEN_EXCLUDE_SYMBOLS_STRUCTS
        chacha20_poly1305_keysched,
        dh_ctx,
        dh_ctx,
        dh_keypair,
        error_struct,
        packet_struct,
        pem_get_password_struct,
        ssh_tokens_st,
        sftp_attributes_struct,
        sftp_client_message_struct,
        sftp_dir_struct,
        sftp_ext_struct,
        sftp_file_struct,
        sftp_message_struct,
        sftp_packet_struct,
        sftp_request_queue_struct,
        sftp_session_struct,
        sftp_status_message_struct,
        ssh_agent_state_struct,
        ssh_agent_struct,
        ssh_auth_auto_state_struct,
        ssh_auth_request,
        ssh_bind_config_keyword_table_s,
        ssh_bind_config_match_keyword_table_s,
        ssh_bind_struct,
        ssh_buffer_struct,
        ssh_channel_callbacks_struct,
        ssh_channel_read_termination_struct,
        ssh_channel_request,
        ssh_channel_request_open,
        ssh_channel_struct,
        ssh_cipher_struct,
        ssh_common_struct,
        ssh_config_keyword_table_s,
        ssh_config_match_keyword_table_s,
        ssh_connector_struct,
        ssh_counter_struct,
        ssh_crypto_struct,
        ssh_event_fd_wrapper,
        ssh_event_struct,
        ssh_global_request,
        ssh_gssapi_struct,
        ssh_hmac_struct,
        ssh_iterator,
        ssh_kbdint_struct,
        ssh_kex_struct,
        ssh_key_struct,
        ssh_knownhosts_entry,
        ssh_list,
        ssh_mac_ctx_struct,
        ssh_message_struct,
        ssh_packet_callbacks_struct,
        ssh_packet_header,
        ssh_poll_ctx_struct,
        ssh_poll_handle_struct,
        ssh_pollfd_struct,
        ssh_private_key_struct,
        ssh_public_key_struct,
        ssh_scp_struct,
        ssh_service_request,
        ssh_session_struct,
        ssh_signature_struct,
        ssh_socket_struct,
        ssh_string_struct,
        ssh_threads_callbacks_struct,
        ssh_timestamp)
    set(DOXYGEN_EXCLUDE_SYMBOLS_MACRO
        SSH_FXP*,
        SSH_SOCKET*,
        SERVERBANNER,
        SOCKOPT_TYPE_ARG4,
        SSH_FILEXFER*,
        SSH_FXF*,
        SSH_S_*,
        SFTP_*,
        NSS_BUFLEN_PASSWD,
        CLOCK,
        MAX_LINE_SIZE,
        PKCS11_URI,
        KNOWNHOSTS_MAXTYPES)
    set(DOXYGEN_EXCLUDE_SYMBOLS_TYPEDEFS
        sftp_attributes,
        sftp_client_message,
        sftp_dir,
        sftp_ext,
        sftp_file,
        sftp_message,
        sftp_packet,
        sftp_request_queue,
        sftp_status_message,
        sftp_statvfs_t,
        poll_fn,
        ssh_callback_int,
        ssh_callback_data,
        ssh_callback_int_int,
        ssh_message_callback,
        ssh_channel_callback_int,
        ssh_channel_callback_data,
        ssh_callbacks,
        ssh_gssapi_select_oid_callback,
        ssh_gssapi_accept_sec_ctx_callback,
        ssh_gssapi_verify_mic_callback,
        ssh_server_callbacks,
        ssh_socket_callbacks,
        ssh_packet_callbacks,
        ssh_channel_callbacks,
        ssh_bind,
        ssh_bind_callbacks)
    set(DOXYGEN_EXCLUDE_SYMBOLS
        ${DOXYGEN_EXCLUDE_SYMBOLS_STRUCTS} ${DOXYGEN_EXCLUDE_SYMBOLS_MACRO}
        ${DOXYGEN_EXCLUDE_SYMBOLS_TYPEDEFS})

    # Custom layout file to rename "Topics" to "API Reference" and simplify
    # navigation
    set(DOXYGEN_LAYOUT_FILE ${CMAKE_CURRENT_SOURCE_DIR}/DoxygenLayout.xml)
    # Custom HTML header with doxygen-awesome extension initialization
    set(DOXYGEN_HTML_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/header.html)
    # Modern CSS theme for documentation with custom libssh.org color scheme
    set(DOXYGEN_HTML_EXTRA_STYLESHEET
        ${AWESOME_CSS_DIR}/doxygen-awesome.css
        ${CMAKE_CURRENT_SOURCE_DIR}/doxygen-custom.css)
    # JavaScript extensions: dark mode toggle, copy button, paragraph links,
    # interactive TOC
    set(DOXYGEN_HTML_EXTRA_FILES
        ${AWESOME_CSS_DIR}/doxygen-awesome-darkmode-toggle.js
        ${AWESOME_CSS_DIR}/doxygen-awesome-fragment-copy-button.js
        ${AWESOME_CSS_DIR}/doxygen-awesome-paragraph-link.js
        ${AWESOME_CSS_DIR}/doxygen-awesome-interactive-toc.js)

    set(_doxyfile_template "${CMAKE_BINARY_DIR}/CMakeDoxyfile.in")
    set(_target_doxyfile "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.docs")
    configure_file("${_doxyfile_template}" "${_target_doxyfile}")

    doxygen_add_docs(docs ${CMAKE_SOURCE_DIR}/include/libssh
                     ${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})

    # Make docs depend on doxygen-awesome-css download (if not using local dir)
    if (TARGET doxygen-awesome-css)
        add_dependencies(docs doxygen-awesome-css)
    endif ()

    add_custom_target(
        docs_coverage COMMAND ${CMAKE_SOURCE_DIR}/doc/doc_coverage.sh
                              ${CMAKE_BINARY_DIR})
endif (DOXYGEN_FOUND)
