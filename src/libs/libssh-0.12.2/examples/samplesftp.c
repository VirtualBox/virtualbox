/*
Copyright 2003-2009 Aris Adamantiadis

This file is part of the SSH Library

You are free to copy this file, modify it in any way, consider it being public
domain. This does not apply to the rest of the library though, but it is
allowed to cut-and-paste working code from this file to any license of
program.
The goal is to show the API in action. It's not a reference on how terminal
clients must be made or how a client should react.
*/

#include "config.h"

#include <sys/statvfs.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <libssh/libssh.h>
#include <libssh/sftp.h>

#include "examples_common.h"
#ifdef WITH_SFTP

#ifndef BUF_SIZE
#define BUF_SIZE 65536
#endif

static void do_sftp(ssh_session session) {
    sftp_session sftp = sftp_new(session);
    sftp_dir dir;
    sftp_attributes file;
    sftp_statvfs_t sftpstatvfs;
    struct statvfs sysstatvfs;
    sftp_file source;
    sftp_file to;
    int len = 1;
    unsigned int i;
    char data[BUF_SIZE] = {0};
    char *lnk = NULL;

    unsigned int count;

    if (!sftp) {
        fprintf(stderr, "sftp error initialising channel: %s\n",
                ssh_get_error(session));
        goto end;
    }

    if (sftp_init(sftp)) {
        fprintf(stderr, "error initialising sftp: %s\n",
                ssh_get_error(session));
        goto end;
    }

    printf("Additional SFTP extensions provided by the server:\n");
    count = sftp_extensions_get_count(sftp);
    for (i = 0; i < count; i++) {
        printf("\t%s, version: %s\n",
               sftp_extensions_get_name(sftp, i),
               sftp_extensions_get_data(sftp, i));
    }

    /* test symlink and readlink */
    if (sftp_symlink(sftp, "/tmp/this_is_the_link",
                     "/tmp/sftp_symlink_test") < 0)
    {
        fprintf(stderr, "Could not create link (%s)\n",
                ssh_get_error(session));
        goto end;
    }

    lnk = sftp_readlink(sftp, "/tmp/sftp_symlink_test");
    if (lnk == NULL) {
        fprintf(stderr, "Could not read link (%s)\n", ssh_get_error(session));
        goto end;
    }
    printf("readlink /tmp/sftp_symlink_test: %s\n", lnk);
    ssh_string_free_char(lnk);

    sftp_unlink(sftp, "/tmp/sftp_symlink_test");

    if (sftp_extension_supported(sftp, "statvfs@openssh.com", "2")) {
        sftpstatvfs = sftp_statvfs(sftp, "/tmp");
        if (sftpstatvfs == NULL) {
            fprintf(stderr, "statvfs failed (%s)\n", ssh_get_error(session));
            goto end;
        }

        printf("sftp statvfs:\n"
               "\tfile system block size: %llu\n"
               "\tfundamental fs block size: %llu\n"
               "\tnumber of blocks (unit f_frsize): %llu\n"
               "\tfree blocks in file system: %llu\n"
               "\tfree blocks for non-root: %llu\n"
               "\ttotal file inodes: %llu\n"
               "\tfree file inodes: %llu\n"
               "\tfree file inodes for to non-root: %llu\n"
               "\tfile system id: %llu\n"
               "\tbit mask of f_flag values: %llu\n"
               "\tmaximum filename length: %llu\n",
               (unsigned long long) sftpstatvfs->f_bsize,
               (unsigned long long) sftpstatvfs->f_frsize,
               (unsigned long long) sftpstatvfs->f_blocks,
               (unsigned long long) sftpstatvfs->f_bfree,
               (unsigned long long) sftpstatvfs->f_bavail,
               (unsigned long long) sftpstatvfs->f_files,
               (unsigned long long) sftpstatvfs->f_ffree,
               (unsigned long long) sftpstatvfs->f_favail,
               (unsigned long long) sftpstatvfs->f_fsid,
               (unsigned long long) sftpstatvfs->f_flag,
               (unsigned long long) sftpstatvfs->f_namemax);

        sftp_statvfs_free(sftpstatvfs);

        if (statvfs("/tmp", &sysstatvfs) < 0) {
            fprintf(stderr, "statvfs failed (%s)\n", strerror(errno));
            goto end;
        }

        printf("sys statvfs:\n"
               "\tfile system block size: %llu\n"
               "\tfundamental fs block size: %llu\n"
               "\tnumber of blocks (unit f_frsize): %llu\n"
               "\tfree blocks in file system: %llu\n"
               "\tfree blocks for non-root: %llu\n"
               "\ttotal file inodes: %llu\n"
               "\tfree file inodes: %llu\n"
               "\tfree file inodes for to non-root: %llu\n"
               "\tfile system id: %llu\n"
               "\tbit mask of f_flag values: %llu\n"
               "\tmaximum filename length: %llu\n",
               (unsigned long long) sysstatvfs.f_bsize,
               (unsigned long long) sysstatvfs.f_frsize,
               (unsigned long long) sysstatvfs.f_blocks,
               (unsigned long long) sysstatvfs.f_bfree,
               (unsigned long long) sysstatvfs.f_bavail,
               (unsigned long long) sysstatvfs.f_files,
               (unsigned long long) sysstatvfs.f_ffree,
               (unsigned long long) sysstatvfs.f_favail,
               (unsigned long long) sysstatvfs.f_fsid,
               (unsigned long long) sysstatvfs.f_flag,
               (unsigned long long) sysstatvfs.f_namemax);
    }

    /* the connection is made */
    /* opening a directory */
    dir = sftp_opendir(sftp, "./");
    if (!dir) {
        fprintf(stderr, "Directory not opened(%s)\n", ssh_get_error(session));
        goto end;
    }

    /* reading the whole directory, file by file */
    while ((file = sftp_readdir(sftp, dir))) {
        fprintf(stderr, "%30s(%.8o) : %s(%.5d) %s(%.5d) : %.10llu bytes\n",
                file->name,
                file->permissions,
                file->owner,
                file->uid,
                file->group,
                file->gid,
                (long long unsigned int) file->size);
        sftp_attributes_free(file);
    }

    /* when file = NULL, an error has occurred OR the directory listing is end of
     * file */
    if (!sftp_dir_eof(dir)) {
        fprintf(stderr, "Error: %s\n", ssh_get_error(session));
        goto end;
    }

    if (sftp_closedir(dir)) {
        fprintf(stderr, "Error: %s\n", ssh_get_error(session));
        goto end;
    }
    /* this will open a file and copy it into your /home directory */
    /* the small buffer size was intended to stress the library. of course, you
     * can use a buffer till 20kbytes without problem */

    source = sftp_open(sftp, "/usr/bin/ssh", O_RDONLY, 0);
    if (!source) {
        fprintf(stderr, "Error opening /usr/bin/ssh: %s\n",
                ssh_get_error(session));
        goto end;
    }

    /* open a file for writing... */
    to = sftp_open(sftp, "ssh-copy", O_WRONLY | O_CREAT, 0700);
    if (!to) {
        fprintf(stderr, "Error opening ssh-copy for writing: %s\n",
                ssh_get_error(session));
        sftp_close(source);
        goto end;
    }

    while ((len = sftp_read(source, data, 4096)) > 0) {
        if (sftp_write(to, data, len) != len) {
            fprintf(stderr, "Error writing %d bytes: %s\n",
                    len, ssh_get_error(session));
            sftp_close(to);
            sftp_close(source);
            goto end;
        }
    }

    printf("finished\n");
    if (len < 0) {
        fprintf(stderr, "Error reading file: %s\n", ssh_get_error(session));
    }

    sftp_close(source);
    sftp_close(to);
    printf("file closed\n");
    to = sftp_open(sftp, "/tmp/large_file", O_WRONLY|O_CREAT, 0644);

    for (i = 0; i < 1000; ++i) {
        len = sftp_write(to, data, sizeof(data));
        printf("wrote %d bytes\n", len);
        if (len != sizeof(data)) {
            printf("chunk %d : %d (%s)\n", i, len, ssh_get_error(session));
        }
    }

    sftp_close(to);
end:
    /* close the sftp session */
    sftp_free(sftp);
    printf("sftp session terminated\n");
}

static void usage(const char *argv0) {
    fprintf(stderr, "Usage : %s [-v] remotehost\n"
            "sample sftp test client - libssh-%s\n"
            "Options :\n"
            "  -l user : log in as user\n"
            "  -p port : connect to port\n"
            "  -v : increase log verbosity\n",
            argv0,
            ssh_version(0));
    exit(0);
}

int main(int argc, char **argv)
{
    ssh_session session = NULL;
    char *destination = NULL;
    int auth = 0;
    int state;

    ssh_init();
    session = ssh_new();

    if (ssh_options_getopt(session, &argc, argv)) {
        fprintf(stderr,
                "Error parsing command line: %s\n",
                ssh_get_error(session));
        ssh_free(session);
        ssh_finalize();
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (argc < 1) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    destination = argv[1];

    if (ssh_options_set(session, SSH_OPTIONS_HOST, destination) < 0) {
        return -1;
    }
    if (ssh_connect(session)) {
        fprintf(stderr, "Connection failed : %s\n", ssh_get_error(session));
        return -1;
    }

    state = verify_knownhost(session);
    if (state != 0) {
        return -1;
    }

    auth = authenticate_console(session);
    if (auth != SSH_AUTH_SUCCESS) {
        return -1;
    }

    do_sftp(session);
    ssh_disconnect(session);
    ssh_free(session);

    ssh_finalize();

    return 0;
}

#endif
