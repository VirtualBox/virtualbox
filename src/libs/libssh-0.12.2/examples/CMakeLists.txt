project(libssh-examples C CXX)

set(examples_SRCS
  authentication.c
  knownhosts.c
  connect_ssh.c
)

include_directories(${libssh_BINARY_DIR}/include ${libssh_BINARY_DIR})

if (ARGP_INCLUDE_DIR)
    include_directories(${ARGP_INCLUDE_DIR})
endif()

if (UNIX AND NOT WIN32)
    add_executable(libssh_scp libssh_scp.c ${examples_SRCS})
    target_compile_options(libssh_scp PRIVATE ${DEFAULT_C_COMPILE_FLAGS})
    target_link_libraries(libssh_scp ssh::ssh)

    add_executable(scp_download scp_download.c ${examples_SRCS})
    target_compile_options(scp_download PRIVATE ${DEFAULT_C_COMPILE_FLAGS})
    target_link_libraries(scp_download ssh::ssh)

    add_executable(sshnetcat sshnetcat.c ${examples_SRCS})
    target_compile_options(sshnetcat PRIVATE ${DEFAULT_C_COMPILE_FLAGS})
    target_link_libraries(sshnetcat ssh::ssh)

    if (WITH_SFTP)
        add_executable(samplesftp samplesftp.c ${examples_SRCS})
        target_compile_options(samplesftp PRIVATE ${DEFAULT_C_COMPILE_FLAGS})
        target_link_libraries(samplesftp ssh::ssh)

        if (WITH_SERVER)
            add_executable(sample_sftpserver sample_sftpserver.c ${examples_SRCS})
            target_compile_options(sample_sftpserver PRIVATE ${DEFAULT_C_COMPILE_FLAGS})
            target_link_libraries(sample_sftpserver ssh::ssh ${ARGP_LIBRARIES})
        endif (WITH_SERVER)
    endif (WITH_SFTP)

    add_executable(ssh-client ssh_client.c ${examples_SRCS})
    target_compile_options(ssh-client PRIVATE ${DEFAULT_C_COMPILE_FLAGS})
    target_link_libraries(ssh-client ssh::ssh)

    add_executable(ssh-X11-client ssh_X11_client.c ${examples_SRCS})
    target_compile_options(ssh-X11-client PRIVATE ${DEFAULT_C_COMPILE_FLAGS})
    target_link_libraries(ssh-X11-client ssh::ssh)

    if (WITH_SERVER AND (ARGP_LIBRARIES OR HAVE_ARGP_H))
        if (HAVE_LIBUTIL)
            add_executable(ssh_server_fork ssh_server.c)
            target_compile_options(ssh_server_fork PRIVATE ${DEFAULT_C_COMPILE_FLAGS} -DWITH_FORK)
            target_link_libraries(ssh_server_fork ssh::ssh ${ARGP_LIBRARIES} util)

            add_executable(ssh_server_pthread ssh_server.c)
            target_compile_options(ssh_server_pthread PRIVATE ${DEFAULT_C_COMPILE_FLAGS})
            target_link_libraries(ssh_server_pthread ssh::ssh ${ARGP_LIBRARIES} pthread util)
        endif (HAVE_LIBUTIL)

        if (WITH_GSSAPI AND GSSAPI_FOUND)
            add_executable(proxy proxy.c)
            target_compile_options(proxy PRIVATE ${DEFAULT_C_COMPILE_FLAGS})
            target_link_libraries(proxy ssh::ssh ${ARGP_LIBRARIES})

            add_executable(sshd_direct-tcpip sshd_direct-tcpip.c)
            target_compile_options(sshd_direct-tcpip PRIVATE ${DEFAULT_C_COMPILE_FLAGS})
            target_link_libraries(sshd_direct-tcpip ssh::ssh ${ARGP_LIBRARIES})
        endif (WITH_GSSAPI AND GSSAPI_FOUND)

        add_executable(samplesshd-kbdint samplesshd-kbdint.c)
        target_compile_options(samplesshd-kbdint PRIVATE ${DEFAULT_C_COMPILE_FLAGS})
        target_link_libraries(samplesshd-kbdint ssh::ssh ${ARGP_LIBRARIES})

        add_executable(keygen2 keygen2.c ${examples_SRCS})
        target_compile_options(keygen2 PRIVATE ${DEFAULT_C_COMPILE_FLAGS})
        target_link_libraries(keygen2 ssh::ssh ${ARGP_LIBRARIES})

    endif()
endif (UNIX AND NOT WIN32)

if (WITH_SERVER)
    add_executable(samplesshd-cb samplesshd-cb.c)
    target_compile_options(samplesshd-cb PRIVATE ${DEFAULT_C_COMPILE_FLAGS})
    target_link_libraries(samplesshd-cb ssh::ssh)
    if (ARGP_LIBRARIES OR HAVE_ARGP_H)
        target_link_libraries(samplesshd-cb ${ARGP_LIBRARIES})
    endif(ARGP_LIBRARIES OR HAVE_ARGP_H)
endif()

add_executable(exec exec.c ${examples_SRCS})
target_compile_options(exec PRIVATE ${DEFAULT_C_COMPILE_FLAGS})
target_link_libraries(exec ssh::ssh)

add_executable(senddata senddata.c ${examples_SRCS})
target_compile_options(senddata PRIVATE ${DEFAULT_C_COMPILE_FLAGS})
target_link_libraries(senddata ssh::ssh)

add_executable(keygen keygen.c)
target_compile_options(keygen PRIVATE ${DEFAULT_C_COMPILE_FLAGS})
target_link_libraries(keygen ssh::ssh)

add_executable(libsshpp libsshpp.cpp)
target_link_libraries(libsshpp ssh::ssh)

add_executable(libsshpp_noexcept libsshpp_noexcept.cpp)
target_link_libraries(libsshpp_noexcept ssh::ssh)
