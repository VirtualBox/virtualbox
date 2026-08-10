project(libssh-benchmarks C)

set(benchmarks_SRCS
  bench_scp.c bench_raw.c benchmarks.c latency.c
)
if (WITH_SFTP)
  set(benchmarks_SRCS
    ${benchmarks_SRCS}
    bench_sftp.c
  )
endif (WITH_SFTP)

include_directories(${libssh_BINARY_DIR})

add_executable(benchmarks ${benchmarks_SRCS})

target_link_libraries(benchmarks ssh::static pthread)
