#
# Build the documentation
#
if (${CMAKE_VERSION} VERSION_GREATER "3.8.99")

find_package(Doxygen)

if (DOXYGEN_FOUND)
    set(DOXYGEN_PROJECT_NAME ${PROJECT_NAME})
    set(DOXYGEN_PROJECT_NUMBER ${PROJECT_VERSION})
    set(DOXYGEN_PROJECT_BRIEF "The SSH library")

    set(DOXYGEN_TAB_SIZE 4)
    set(DOXYGEN_OPTIMIZE_OUTPUT_FOR_C YES)
    set(DOXYGEN_MARKDOWN_SUPPORT YES)
    set(DOXYGEN_FULL_PATH_NAMES NO)
    set(DOXYGEN_GENERATE_TAGFILE "tags.xml")

    set(DOXYGEN_PREDEFINED DOXYGEN
                           WITH_SERVER
                           WITH_SFTP
                           PRINTF_ATTRIBUTE\(x,y\))
    set(DOXYGEN_DOT_GRAPH_MAX_NODES 100)

    set(DOXYGEN_EXCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/that_style)
    set(DOXYGEN_HTML_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/that_style/header.html)
    set(DOXYGEN_HTML_EXTRA_STYLESHEET ${CMAKE_CURRENT_SOURCE_DIR}/that_style/that_style.css)
    set(DOXYGEN_HTML_EXTRA_FILES ${CMAKE_CURRENT_SOURCE_DIR}/that_style/img/nav_edge_left.svg
                                 ${CMAKE_CURRENT_SOURCE_DIR}/that_style/img/nav_edge_right.svg
                                 ${CMAKE_CURRENT_SOURCE_DIR}/that_style/img/nav_edge_inter.svg
                                 ${CMAKE_CURRENT_SOURCE_DIR}/that_style/img/sync_off.png
                                 ${CMAKE_CURRENT_SOURCE_DIR}/that_style/img/sync_on.png
                                 ${CMAKE_CURRENT_SOURCE_DIR}/that_style/img/splitbar_handle.svg
                                 ${CMAKE_CURRENT_SOURCE_DIR}/that_style/img/doc.svg
                                 ${CMAKE_CURRENT_SOURCE_DIR}/that_style/img/mag_glass.svg
                                 ${CMAKE_CURRENT_SOURCE_DIR}/that_style/img/folderclosed.svg
                                 ${CMAKE_CURRENT_SOURCE_DIR}/that_style/img/folderopen.svg
                                 ${CMAKE_CURRENT_SOURCE_DIR}/that_style/js/striped_bg.js)
     set(DOXYGEN_EXCLUDE_PATTERNS */src/external/* fe25519.h ge25519.h sc25519.h
                                  blf.h)
     set(DOXYGEN_EXCLUDE_SYMBOLS_STRUCTS chacha20_poly1305_keysched,dh_ctx,dh_ctx,dh_keypair,error_struct,
                                         packet_struct,pem_get_password_struct,ssh_tokens_st,
                                         sftp_attributes_struct,sftp_client_message_struct,
                                         sftp_dir_struct,sftp_ext_struct,sftp_file_struct,sftp_message_struct,
                                         sftp_packet_struct,sftp_request_queue_struct,sftp_session_struct,
                                         sftp_status_message_struct,ssh_agent_state_struct,
                                         ssh_agent_struct,ssh_auth_auto_state_struct,ssh_auth_request,
                                         ssh_bind_config_keyword_table_s,ssh_bind_config_match_keyword_table_s,
                                         ssh_bind_struct,ssh_buffer_struct,ssh_channel_callbacks_struct,
                                         ssh_channel_read_termination_struct,ssh_channel_request,
                                         ssh_channel_request_open,ssh_channel_struct,ssh_cipher_struct,
                                         ssh_common_struct,ssh_config_keyword_table_s,
                                         ssh_config_match_keyword_table_s,ssh_connector_struct,
                                         ssh_counter_struct,ssh_crypto_struct,ssh_event_fd_wrapper,
                                         ssh_event_struct,ssh_global_request,ssh_gssapi_struct,ssh_hmac_struct,
                                         ssh_iterator,ssh_kbdint_struct,ssh_kex_struct,ssh_key_struct,
                                         ssh_knownhosts_entry,ssh_list,ssh_mac_ctx_struct,ssh_message_struct,
                                         ssh_packet_callbacks_struct,ssh_packet_header,ssh_poll_ctx_struct,
                                         ssh_poll_handle_struct,ssh_pollfd_struct,ssh_private_key_struct,
                                         ssh_public_key_struct,ssh_scp_struct,ssh_service_request,
                                         ssh_session_struct,ssh_signature_struct,ssh_socket_struct,
                                         ssh_string_struct,ssh_threads_callbacks_struct,ssh_timestamp,)
     set(DOXYGEN_EXCLUDE_SYMBOLS_MACRO SSH_FXP*,SSH_SOCKET*,SERVERBANNER,SOCKOPT_TYPE_ARG4,SSH_FILEXFER*,
                                       SSH_FXF*,SSH_S_*,SFTP_*,NSS_BUFLEN_PASSWD,CLOCK,MAX_LINE_SIZE,
                                       PKCS11_URI,KNOWNHOSTS_MAXTYPES,)
     set(DOXYGEN_EXCLUDE_SYMBOLS_TYPEDEFS sftp_attributes,sftp_client_message,sftp_dir,sftp_ext,sftp_file,
                                          sftp_message,sftp_packet,sftp_request_queue,sftp_session,
                                          sftp_status_message,sftp_statvfs_t,poll_fn,ssh_callback_int,
                                          ssh_callback_data,ssh_callback_int_int,ssh_message_callback,
                                          ssh_channel_callback_int,ssh_channel_callback_data,ssh_callbacks,
                                          ssh_gssapi_select_oid_callback,ssh_gssapi_accept_sec_ctx_callback,
                                          ssh_gssapi_verify_mic_callback,ssh_server_callbacks,ssh_socket_callbacks,
                                          ssh_packet_callbacks,ssh_channel_callbacks,ssh_bind,ssh_bind_callbacks,)
     set(DOXYGEN_EXCLUDE_SYMBOLS ${DOXYGEN_EXCLUDE_SYMBOLS_STRUCTS}
                                 ${DOXYGEN_EXCLUDE_SYMBOLS_MACRO}
                                 ${DOXYGEN_EXCLUDE_SYMBOLS_TYPEDEFS})

    # This updates the Doxyfile if we do changes here
    set(_doxyfile_template "${CMAKE_BINARY_DIR}/CMakeDoxyfile.in")
    set(_target_doxyfile "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile.docs")
    configure_file("${_doxyfile_template}" "${_target_doxyfile}")

    doxygen_add_docs(docs
                     ${CMAKE_SOURCE_DIR}/include/libssh
                     ${CMAKE_SOURCE_DIR}/src
                     ${CMAKE_CURRENT_SOURCE_DIR})

    add_custom_target(docs_coverage COMMAND ${CMAKE_SOURCE_DIR}/doc/doc_coverage.sh ${CMAKE_BINARY_DIR})
endif() # DOXYGEN_FOUND

endif() # CMAKE_VERSION
