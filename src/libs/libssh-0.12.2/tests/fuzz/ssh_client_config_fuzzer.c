/*
 * Copyright 2021 Stanislav Zidek <szidek@redhat.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIBSSH_STATIC 1
#include "libssh/libssh.h"
#include "libssh/options.h"

#include "nallocinc.c"

static void _fuzz_finalize(void)
{
    ssh_finalize();
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc;

    nalloc_init(*argv[0]);

    ssh_init();

    atexit(_fuzz_finalize);

    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    ssh_session session = NULL;
    char *input = NULL;

    input = (char *)malloc(size+1);
    if (!input) {
        return 1;
    }
    strncpy(input, (const char *)data, size);
    input[size] = '\0';

    assert(nalloc_start(data, size) > 0);

    session = ssh_new();
    if (session == NULL) {
        goto out;
    }

    /* Make sure we have default options set */
    ssh_options_set(session, SSH_OPTIONS_SSH_DIR, NULL);
    ssh_options_set(session, SSH_OPTIONS_HOST, "example.com");

    ssh_config_parse_string(session, input);

    ssh_free(session);

out:
    free(input);

    nalloc_end();
    return 0;
}
