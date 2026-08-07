# Simple fuzzers for libssh

This directory contains fuzzer programs, that are usable either in
oss-fuzz infrastructure or suitable for running fuzzing locally or
even for reproducing crashes with given trace files.

When building with clang, fuzzers are automatically built with address
sanitizer. With gcc, they are built as they are without instrumentation,
but they are suitable for debugging.

## Background

### Turn off encryption

Fuzzing ssh protocol is complicated by the way that all the communication
between client and server is encrypted and authenticated using keys based
on random data, making it impossible to fuzz the actual underlying protocol
as every change in the encrypted data causes integrity errors. For that reason,
libssh needs to implement "none" cipher and MAC as described in RFC 4253
and these need to be used during fuzzing to be able to accomplish
reproducibility and for fuzzers to be able to progress behind key exchange.
This is enabled with the `WITH_INSECURE_NONE` CMake option.

### Do not allow filesystem modification

The OpenSSH configuration files are quite rich and expects users to know what
they do when they write their configuration files. The fuzzer driver is not an
average user so it is very happy to try whatever commands come to its "mind",
including `rm -rf /` and libssh would be very happy to run it by default. This
might remove some parts of the system that are mandatory for fuzzing.
To avoid executing dangerous commands like this, the `WITH_EXEC=OFF` CMake
option prevents invoking any external command through `exec()` syscall.

## Corpus creation

For effective fuzzing, we need to provide corpus of initial (valid) inputs that
can be used for deriving other inputs. libssh already supports creation of pcap
files (packet capture), which include all the information we need for fuzzing.
This file is also created from date before encryption and after decryption so
it is in plain text as we expect it, but we still need to adjust configuration
to use none cipher for the key exchange to be plausible.

### Creating packet capture using example libssh client

 * Compile libssh with support for none cipher and pcap:

    cmake -DWITH_INSECURE_NONE=ON -DWITH_EXEC=OFF -DWITH_PCAP=ON ../

 * Create a configuration file enabling none cipher and mac:

    printf 'Ciphers none\nMACs none' > /tmp/ssh_config

 * Generate test host key:

    ./examples/keygen2 -f /tmp/hostkey -t rsa

 * Run example libssh server:

    ./examples/samplesshd-cb -f /tmp/ssh_config -k /tmp/hostkey -p 22222 127.0.0.1

 * In other terminal, run the example libssh client with pcap enabled (use mypassword for password):

    ./examples/ssh-client -F /tmp/ssh_config -l myuser -P /tmp/ssh.pcap -p 22222 127.0.0.1

 * Kill the server (in the first terminal, press Ctrl+C)

 * Convert the pcap file to raw traces (separate client and server messages) usable by fuzzer:

    tshark -r /tmp/ssh.pcap -T fields -e data -Y "tcp.dstport==22222" | tr -d '\n',':' | xxd -r -ps > /tmp/ssh_server
    tshark -r /tmp/ssh.pcap -T fields -e data -Y "tcp.dstport!=22222" | tr -d '\n',':' | xxd -r -ps > /tmp/ssh_client

 * Now we should be able to "replay" the sessions in respective fuzzers, getting some more coverage:

    LIBSSH_VERBOSITY=9 ./tests/fuzz/ssh_client_fuzzer /tmp/ssh_client
    LIBSSH_VERBOSITY=9 ./tests/fuzz/ssh_server_fuzzer /tmp/ssh_server

   (note, that the client fuzzer fails now because of invalid hostkey signature; TODO)

 * Store the appropriately named traces in the fuzers directory:

    cp /tmp/ssh_client tests/fuzz/ssh_client_fuzzer_corpus/$(sha1sum /tmp/ssh_client | cut -d ' ' -f 1)
    cp /tmp/ssh_server tests/fuzz/ssh_server_fuzzer_corpus/$(sha1sum /tmp/ssh_server | cut -d ' ' -f 1)

## Debugging issues reported by oss-fuzz

OSS Fuzz provides helper scripts to reproduce issues locally. Even though the
fuzzing scripts can ran anywhere, the best bet for reproducing is to use
their container infrastructure. There is a
[complete documentation](https://google.github.io/oss-fuzz/advanced-topics/reproducing/)
but I will try to focus here on the workflow I use and libssh specifics.

### Environment

The helper scripts are written in Python and use docker to run containers
so these needs to be installed. I am using podman instead of docker for
some time, but it has some quirks that needs to be addressed in advance
and that I describe in the rejected [PR](https://github.com/google/oss-fuzz/pull/4774).
You can either pick up my branch or workaround them locally:

 * Package `podman-docker` installs symlink from `/bin/docker` to `/bin/podman`
 * The directories mounted to the containers need to have `container_file_t`
   SELinux labels -- this is needed for the `build` directory that is created
   under the oss-fuzz repository, for testcases and for source files
 * `podman` does not like combination of `--privileged` and
   `--cap-add SYS_PTRACE` flags. Podman can work with non-privileged containers
   so you can just remove the `--privileged` from the `infra/helper.py`

### Reproduce locally

Clone the above repository from https://github.com/google/oss-fuzz/, apply
changes from previous section if needed, setup local clone of libssh repository
and build the fuzzers locally (where `~/devel/libssh` is path to local libssh
checkout):

    python infra/helper.py build_fuzzers libssh ~/devel/libssh/

Now, download the testcase from oss-fuzz.com (the file under `~/Downloads`)
and we are ready to reproduce the issue locally (replace the `ssh_client_fuzzer`
with the fuzzer name if the issue happens in other fuzzer):

    python infra/helper.py reproduce libssh ssh_client_fuzzer ~/Downloads/clusterfuzz-testcase-ssh_client_fuzzer-4637376441483264

This should give you the same error/leak/crash as you see on the testcase
detail in oss-fuzz.com.

I find it very useful to run libssh in debug mode, to see what happened and
what exit path was taken to get to the error. Fortunately, we can simply
pass environment variables to the container:

    python infra/helper.py reproduce -eLIBSSH_VERBOSITY=9 libssh ssh_client_fuzzer ~/Downloads/clusterfuzz-testcase-ssh_client_fuzzer-4637376441483264

### Fix the issue and verify the fix

Now, we can properly investigate the issue and once we have a fix, we can
make changes in our local checkout and repeat the steps above (from building
fuzzers) to verify the issue is no longer present.

### Fuzzing locally

We can use the oss-fuzz tools even further and run the fuzzing process
locally, to verify there are no similar issues happening very close to
existing code paths and which would cause more reports very soon after
we would fix the current issue. The following command will run fuzzer
until it finds an issue or until killed:

    python infra/helper.py run_fuzzer libssh ssh_client_fuzzer
