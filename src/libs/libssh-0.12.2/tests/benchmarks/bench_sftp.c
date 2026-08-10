/* bench_sftp.c
 *
 * This file is part of the SSH Library
 *
 * Copyright (c) 2011 by Aris Adamantiadis
 *
 * The SSH Library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * The SSH Library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with the SSH Library; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330, Boston,
 * MA 02111-1307, USA.
 */

#include "benchmarks.h"
#include <libssh/libssh.h>
#include <libssh/sftp.h>
#include <libssh/misc.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>

#define SFTPDIR "/tmp/"
#define SFTPFILE "scpbenchmark"

/** @internal
 * @brief benchmarks a synchronous sftp upload using an
 * existing SSH session.
 * @param[in] session Open SSH session
 * @param[in] args Parsed command line arguments
 * @param[out] bps The calculated bytes per second obtained via benchmark.
 * @return 0 on success, -1 on error.
 */
int benchmarks_sync_sftp_up (ssh_session session, struct argument_s *args,
    float *bps){
  unsigned long bytes;
  struct timestamp_struct ts;
  float ms=0.0;
  unsigned long total=0;
  sftp_session sftp;
  sftp_file file = NULL;

  bytes = args->datasize * 1024 * 1024;
  sftp = sftp_new(session);
  if(sftp == NULL)
    goto error;
  if(sftp_init(sftp)==SSH_ERROR)
    goto error;
  file = sftp_open(sftp,SFTPDIR SFTPFILE,O_RDWR | O_CREAT | O_TRUNC, 0777);
  if(!file)
    goto error;
  if(args->verbose>0)
    fprintf(stdout,"Starting upload of %lu bytes now\n",bytes);
  timestamp_init(&ts);
  while(total < bytes){
    unsigned long towrite = bytes - total;
    int w;
    if(towrite > args->chunksize)
      towrite = args->chunksize;
    w=sftp_write(file,buffer,towrite);
    if(w == SSH_ERROR)
      goto error;
    total += w;
  }
  sftp_close(file);
  ms=elapsed_time(&ts);
  *bps=8000 * (float)bytes / ms;
  if(args->verbose > 0)
    fprintf(stdout,"Upload took %f ms for %lu bytes, at %f bps\n",ms,
        bytes,*bps);
  sftp_free(sftp);
  return 0;
error:
  fprintf(stderr,"Error during scp upload : %s\n",ssh_get_error(session));
  if(file)
    sftp_close(file);
  if(sftp)
    sftp_free(sftp);
  return -1;
}

/** @internal
 * @brief benchmarks a synchronous sftp download using an
 * existing SSH session.
 * @param[in] session Open SSH session
 * @param[in] args Parsed command line arguments
 * @param[out] bps The calculated bytes per second obtained via benchmark.
 * @return 0 on success, -1 on error.
 */
int benchmarks_sync_sftp_down (ssh_session session, struct argument_s *args,
    float *bps){
  unsigned long bytes;
  struct timestamp_struct ts;
  float ms=0.0;
  unsigned long total=0;
  sftp_session sftp;
  sftp_file file = NULL;
  int r;

  bytes = args->datasize * 1024 * 1024;
  sftp = sftp_new(session);
  if(sftp == NULL)
    goto error;
  if(sftp_init(sftp)==SSH_ERROR)
    goto error;
  file = sftp_open(sftp,SFTPDIR SFTPFILE,O_RDONLY,0);
  if(!file)
    goto error;
  if(args->verbose>0)
    fprintf(stdout,"Starting download of %lu bytes now\n",bytes);
  timestamp_init(&ts);
  while(total < bytes){
    unsigned long toread = bytes - total;
    if(toread > args->chunksize)
      toread = args->chunksize;
    r=sftp_read(file,buffer,toread);
    if(r == SSH_ERROR)
      goto error;
    total += r;
    /* we had a smaller file */
    if(r==0){
      fprintf(stdout,"File smaller than expected : %lu (expected %lu).\n",total,bytes);
      bytes = total;
      break;
    }
  }
  sftp_close(file);
  ms=elapsed_time(&ts);
  *bps=8000 * (float)bytes / ms;
  if(args->verbose > 0)
    fprintf(stdout,"download took %f ms for %lu bytes, at %f bps\n",ms,
        bytes,*bps);
  sftp_free(sftp);
  return 0;
error:
  fprintf(stderr,"Error during sftp download : %s\n",ssh_get_error(session));
  if(file)
    sftp_close(file);
  if(sftp)
    sftp_free(sftp);
  return -1;
}

/** @internal
 * @brief benchmarks an asynchronous sftp download using an
 * existing SSH session.
 * @param[in] session Open SSH session
 * @param[in] args Parsed command line arguments
 * @param[out] bps The calculated bytes per second obtained via benchmark.
 * @return 0 on success, -1 on error.
 */
int benchmarks_async_sftp_down (ssh_session session, struct argument_s *args,
    float *bps){
  unsigned long bytes;
  struct timestamp_struct ts;
  float ms=0.0;
  unsigned long total=0;
  sftp_session sftp;
  sftp_file file = NULL;
  int r,i;
  int warned = 0;
  unsigned long toread;
  int *ids=NULL;
  int concurrent_downloads = args->concurrent_requests;

  bytes = args->datasize * 1024 * 1024;
  sftp = sftp_new(session);
  if(sftp == NULL)
    goto error;
  if(sftp_init(sftp)==SSH_ERROR)
    goto error;
  file = sftp_open(sftp,SFTPDIR SFTPFILE,O_RDONLY,0);
  if(!file)
    goto error;
  ids = malloc(concurrent_downloads * sizeof(int));
  if (ids == NULL) {
    return -1;
  }
  if(args->verbose>0)
    fprintf(stdout,"Starting download of %lu bytes now, using %d concurrent downloads\n",bytes,
        concurrent_downloads);
  timestamp_init(&ts);
  for (i=0;i<concurrent_downloads;++i){
    ids[i]=sftp_async_read_begin(file, args->chunksize);
    if(ids[i]==SSH_ERROR)
        goto error;
  }
  i=0;
  while(total < bytes){
    r = sftp_async_read(file, buffer, args->chunksize, ids[i]);
    if(r == SSH_ERROR)
      goto error;
    total += r;
    if(r != (int)args->chunksize && total != bytes && !warned){
      fprintf(stderr,"async_sftp_download : receiving short reads (%d, requested %d) "
          "the received file will be corrupted and shorted. Adapt chunksize to %d\n",
          r, args->chunksize,r);
      warned = 1;
    }
    /* we had a smaller file */
    if(r==0){
      fprintf(stdout,"File smaller than expected : %lu (expected %lu).\n",total,bytes);
      bytes = total;
      break;
    }
    toread = bytes - total;
    if(toread < args->chunksize * concurrent_downloads){
      /* we've got enough launched downloads */
      ids[i]=-1;
    }
    if(toread > args->chunksize)
      toread = args->chunksize;
    ids[i]=sftp_async_read_begin(file,toread);
    if(ids[i] == SSH_ERROR)
      goto error;
    i = (i+1) % concurrent_downloads;
  }
  sftp_close(file);
  ms=elapsed_time(&ts);
  *bps=8000 * (float)bytes / ms;
  if(args->verbose > 0)
    fprintf(stdout,"download took %f ms for %lu bytes, at %f bps\n",ms,
        bytes,*bps);
  sftp_free(sftp);
  free(ids);
  return 0;
error:
  fprintf(stderr,"Error during sftp download : %s\n",ssh_get_error(session));
  if(file)
    sftp_close(file);
  if(sftp)
    sftp_free(sftp);
  free(ids);
  return -1;
}

int benchmarks_async_sftp_aio_down(ssh_session session,
                                   struct argument_s *args,
                                   float *bps)
{
    sftp_session sftp = NULL;
    sftp_limits_t li = NULL;
    sftp_file file = NULL;
    sftp_aio aio = NULL;

    struct ssh_list *aio_queue = NULL;

    int concurrent_downloads = args->concurrent_requests;
    size_t chunksize;
    struct timestamp_struct ts = {0};
    float ms = 0.0f;

    size_t total_bytes = args->datasize * 1024 * 1024;
    size_t total_bytes_requested = 0, total_bytes_read = 0;
    size_t bufsize = args->chunksize;
    size_t to_read;
    ssize_t bytes_read, bytes_requested;
    int warned = 0, i, rc;

    sftp = sftp_new(session);
    if (sftp == NULL) {
        fprintf(stderr, "Error during sftp aio download: %s\n",
                ssh_get_error(session));
        return -1;
    }

    /*
     * Errors which are logged in the ssh session are reported after
     * jumping to the goto label, errors which aren't logged inside the
     * ssh session are reported before jumping to that label
     */

    rc = sftp_init(sftp);
    if (rc == SSH_ERROR) {
        goto error;
    }

    li = sftp_limits(sftp);
    if (li == NULL) {
        goto error;
    }

    if (args->chunksize > li->max_read_length) {
       chunksize = li->max_read_length;
       if (args->verbose > 0) {
           fprintf(stdout,
                   "Using the chunk size %zu (not the set size %u), "
                   "to respect the max data limit for read packet\n",
                   chunksize, args->chunksize);
       }
    } else {
        chunksize = args->chunksize;
    }

    file = sftp_open(sftp, SFTPDIR SFTPFILE, O_RDONLY, 0);
    if (file == NULL) {
        goto error;
    }

    aio_queue = ssh_list_new();
    if (aio_queue == NULL) {
        fprintf(stderr,
                "Error during sftp aio download: Insufficient memory\n");
        goto error;
    }

    if (args->verbose > 0) {
        fprintf(stdout,
                "Starting download of %zu bytes now, "
                "using %d concurrent downloads.\n",
                total_bytes, concurrent_downloads);
    }

    timestamp_init(&ts);

    for (i = 0;
         i < concurrent_downloads && total_bytes_requested < total_bytes;
         ++i) {
        to_read = total_bytes - total_bytes_requested;
        if (to_read > chunksize) {
            to_read = chunksize;
        }

        bytes_requested = sftp_aio_begin_read(file, to_read, &aio);
        if (bytes_requested == SSH_ERROR) {
            goto error;
        }

        if ((size_t)bytes_requested != to_read) {
            fprintf(stderr,
                    "Error during sftp aio download: sftp_aio_begin_read() "
                    "requesting less bytes even when the number of bytes "
                    "asked to read are within the max limit");
            sftp_aio_free(aio);
            goto error;
        }

        total_bytes_requested += (size_t)bytes_requested;

        /* enqueue */
        rc = ssh_list_append(aio_queue, aio);
        if (rc == SSH_ERROR) {
            fprintf(stderr,
                    "Error during sftp aio download: Insufficient memory");
            sftp_aio_free(aio);
            goto error;
        }
    }

    while ((aio = ssh_list_pop_head(sftp_aio, aio_queue)) != NULL) {
        bytes_read = sftp_aio_wait_read(&aio, buffer, bufsize);
        if (bytes_read == -1) {
            goto error;
        }

        total_bytes_read += (size_t)bytes_read;
        if (bytes_read == 0) {
            fprintf(stdout ,
                    "File smaller than expected: %zu bytes (expected %zu).\n",
                    total_bytes_read, total_bytes);
            break;
        }

        if (total_bytes_read != total_bytes &&
            (size_t)bytes_read != chunksize &&
            warned != 1) {
            fprintf(stderr,
                    "async_sftp_aio_download: Receiving short reads "
                    "(%zu, expected %zu) before encountering eof, "
                    "the received file will be corrupted and shorted. "
                    "Adapt chunksize to %zu.\n",
                    bytes_read, chunksize, bytes_read);
            warned = 1;
        }

        if (total_bytes_requested == total_bytes) {
            /* No need to issue more requests */
            continue;
        }

        /* else issue a request */
        to_read = total_bytes - total_bytes_requested;
        if (to_read > chunksize) {
            to_read = chunksize;
        }

        bytes_requested = sftp_aio_begin_read(file, to_read, &aio);
        if (bytes_requested == SSH_ERROR) {
            goto error;
        }

        if ((size_t)bytes_requested != to_read) {
            fprintf(stderr,
                    "Error during sftp aio download: sftp_aio_begin_read() "
                    "requesting less bytes even when the number of bytes "
                    "asked to read are within the max limit");
            sftp_aio_free(aio);
            goto error;
        }

        total_bytes_requested += (size_t)bytes_requested;

        /* enqueue */
        rc = ssh_list_append(aio_queue, aio);
        if (rc == SSH_ERROR) {
            fprintf(stderr,
                    "Error during sftp aio download: Insufficient memory\n");
            sftp_aio_free(aio);
            goto error;
        }
    }

    sftp_close(file);
    ms = elapsed_time(&ts);
    *bps = (float)(8000 * total_bytes_read) / ms;
    if (args->verbose > 0) {
        fprintf(stdout, "Download took %f ms for %zu bytes at %f bps.\n",
                ms, total_bytes_read, *bps);
    }

    ssh_list_free(aio_queue);
    sftp_limits_free(li);
    sftp_free(sftp);
    return 0;

error:
    rc = ssh_get_error_code(session);
    if (rc != SSH_NO_ERROR) {
        fprintf(stderr, "Error during sftp aio download: %s\n",
                ssh_get_error(session));
    }

    /* Release aio structures corresponding to outstanding requests */
    while ((aio = ssh_list_pop_head(sftp_aio, aio_queue)) != NULL) {
        sftp_aio_free(aio);
    }

    ssh_list_free(aio_queue);
    sftp_close(file);
    sftp_limits_free(li);
    sftp_free(sftp);
    return -1;
}

int benchmarks_async_sftp_aio_up(ssh_session session,
                                 struct argument_s *args,
                                 float *bps)
{
    sftp_session sftp = NULL;
    sftp_limits_t li = NULL;
    sftp_file file = NULL;
    sftp_aio aio = NULL;
    struct ssh_list *aio_queue = NULL;

    int concurrent_uploads = args->concurrent_requests;
    size_t chunksize;
    struct timestamp_struct ts = {0};
    float ms = 0.0f;

    size_t total_bytes = args->datasize * 1024 * 1024;
    size_t to_write, total_bytes_requested = 0;
    ssize_t bytes_written, bytes_requested;
    int i, rc;

    sftp = sftp_new(session);
    if (sftp == NULL) {
        fprintf(stderr, "Error during sftp aio upload: %s\n",
                ssh_get_error(session));
        return -1;
    }

    /*
     * Errors which are logged in the ssh session are reported after
     * jumping to the goto label, errors which aren't logged inside the
     * ssh session are reported before jumping to that label
     */

    rc = sftp_init(sftp);
    if (rc == SSH_ERROR) {
        goto error;
    }

    li = sftp_limits(sftp);
    if (li == NULL) {
        goto error;
    }

    if (args->chunksize > li->max_write_length) {
       chunksize = li->max_write_length;
       if (args->verbose > 0) {
           fprintf(stdout,
                   "Using the chunk size %zu (not the set size %u), "
                   "to respect the max data limit for write packet\n",
                   chunksize, args->chunksize);
       }
    } else {
        chunksize = args->chunksize;
    }

    file = sftp_open(sftp, SFTPDIR SFTPFILE,
                     O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (file == NULL) {
        goto error;
    }

    aio_queue = ssh_list_new();
    if (aio_queue == NULL) {
        fprintf(stderr, "Error during sftp aio upload: Insufficient memory\n");
        goto error;
    }

    if (args->verbose > 0) {
        fprintf(stdout,
                "Starting upload of %zu bytes now, "
                "using %d concurrent uploads.\n",
                total_bytes, concurrent_uploads);
    }

    timestamp_init(&ts);

    for (i = 0;
         i < concurrent_uploads && total_bytes_requested < total_bytes;
         ++i) {
        to_write = total_bytes - total_bytes_requested;
        if (to_write > chunksize) {
            to_write = chunksize;
        }

        bytes_requested = sftp_aio_begin_write(file, buffer, to_write, &aio);
        if (bytes_requested == SSH_ERROR) {
            goto error;
        }

        if ((size_t)bytes_requested != to_write) {
            fprintf(stderr,
                    "Error during sftp aio upload: sftp_aio_begin_write() "
                    "requesting less bytes even when the number of bytes "
                    "asked to write are within the max write limit");
            sftp_aio_free(aio);
            goto error;
        }

        total_bytes_requested += (size_t)bytes_requested;

        /* enqueue */
        rc = ssh_list_append(aio_queue, aio);
        if (rc == SSH_ERROR) {
            fprintf(stderr,
                    "Error during sftp aio upload: Insufficient memory\n");
            sftp_aio_free(aio);
            goto error;
        }
    }

    while ((aio = ssh_list_pop_head(sftp_aio, aio_queue)) != NULL) {
        bytes_written = sftp_aio_wait_write(&aio);
        if (bytes_written == SSH_ERROR) {
            goto error;
        }

        if (total_bytes_requested == total_bytes) {
            /* No need to issue more requests */
            continue;
        }

        /* else issue a request */
        to_write = total_bytes - total_bytes_requested;
        if (to_write > chunksize) {
            to_write = chunksize;
        }

        bytes_requested = sftp_aio_begin_write(file, buffer, to_write, &aio);
        if (bytes_requested == SSH_ERROR) {
            goto error;
        }

        if ((size_t)bytes_requested != to_write) {
            fprintf(stderr,
                    "Error during sftp aio upload: sftp_aio_begin_write() "
                    "requesting less bytes even when the number of bytes "
                    "asked to write are within the max write limit");
            sftp_aio_free(aio);
            goto error;
        }

        total_bytes_requested += bytes_requested;

        /* enqueue */
        rc = ssh_list_append(aio_queue, aio);
        if (rc == SSH_ERROR) {
            fprintf(stderr,
                    "Error during sftp aio upload: Insufficient memory\n");
            sftp_aio_free(aio);
            goto error;
        }
    }

    sftp_close(file);
    ms = elapsed_time(&ts);
    *bps = (float)(8000 * total_bytes) / ms;
    if (args->verbose > 0) {
        fprintf(stdout, "Upload took %f ms for %zu bytes at %f bps.\n",
                ms, total_bytes, *bps);
    }

    ssh_list_free(aio_queue);
    sftp_limits_free(li);
    sftp_free(sftp);
    return 0;

error:
    rc = ssh_get_error_code(session);
    if (rc != SSH_NO_ERROR) {
        fprintf(stderr, "Error during sftp aio upload: %s\n",
                ssh_get_error(session));
    }

    /* Release aio structures corresponding to outstanding requests */
    while ((aio = ssh_list_pop_head(sftp_aio, aio_queue)) != NULL) {
        sftp_aio_free(aio);
    }

    ssh_list_free(aio_queue);
    sftp_close(file);
    sftp_limits_free(li);
    sftp_free(sftp);
    return -1;
}
