/* $Id: main.c 168914 2025-05-16 21:10:21Z klaus.espenlaub@oracle.com $ */
/** @file
 * svnsync tool. Modified by Oracle.
 */
/*
 * ====================================================================
 * Copyright (c) 2005-2006 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

#ifdef VBOX
#include <svn_version.h>
#include <svn_cmdline.h>
#include <svn_config.h>
#include <svn_pools.h>
#include <svn_delta.h>
#include <svn_path.h>
#include <svn_props.h>
#include <svn_auth.h>
#include <svn_opt.h>
#include <svn_ra.h>

/* Debug conditional code. */
#ifdef DEBUG
#define DX(x) do { x } while (0);
#else /* !DEBUG */
#define DX(x) do { } while (0);
#endif /* !DEBUG */

#define _(x) x
#define N_(x) x

#define SVNSYNC_PROP_START_REV          SVNSYNC_PROP_PREFIX "start-rev"
#define SVNSYNC_PROP_DEFAULT            SVNSYNC_PROP_PREFIX "default"
#define SVNSYNC_PROP_PROCESS            SVNSYNC_PROP_PREFIX "process"
#define SVNSYNC_PROP_EXTERNALS          SVNSYNC_PROP_PREFIX "externals"
#define SVNSYNC_PROP_LICENSE            SVNSYNC_PROP_PREFIX "license"
#define SVNSYNC_PROP_DEFAULT_PROCESS    SVNSYNC_PROP_PREFIX "default-process"
#define SVNSYNC_PROP_CENSOR_AUTHOR      SVNSYNC_PROP_PREFIX "censor-author"
#define SVNSYNC_PROP_REPLACE_EXTERNALS  SVNSYNC_PROP_PREFIX "replace-externals"
#define SVNSYNC_PROP_REPLACE_LICENSE    SVNSYNC_PROP_PREFIX "replace-license"
#define SVNSYNC_PROP_IGNORE_CHANGESET   SVNSYNC_PROP_PREFIX "ignore-changeset"
#define SVNSYNC_PROP_REV__FMT           SVNSYNC_PROP_PREFIX "rev-%ld"
#define SVNSYNC_PROP_XREF_SRC_REPO_REV  SVNSYNC_PROP_PREFIX "xref-src-repo-rev"

#define SVN_PROP_LICENSE                "license"

#define STRIP_LEADING_SLASH(x) (*(x) == '/' ? ((x)+1) : (x))

static svn_error_t * add_file(const char *, void *, const char *, svn_revnum_t, apr_pool_t *, void **);
static svn_error_t * add_directory(const char *, void *, const char *, svn_revnum_t, apr_pool_t *, void **);
static svn_error_t * close_file(void *, const char *, apr_pool_t *);
static svn_error_t * close_directory(void *, apr_pool_t *);
static svn_error_t * change_dir_prop(void *, const char *, const svn_string_t *, apr_pool_t *);
static svn_error_t * apply_textdelta(void *, const char *, apr_pool_t *, svn_txdelta_window_handler_t *, void **);
static svn_error_t * change_file_prop(void *, const char *, const svn_string_t *, apr_pool_t *);

/* The base for this code is version 1.5 from the subversion repository,
 * revision 22364. The VBOX code has been updated to use the 1.6 API. */

#else /* !VBOX */
#include "svn_cmdline.h"
#include "svn_config.h"
#include "svn_pools.h"
#include "svn_delta.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_auth.h"
#include "svn_opt.h"
#include "svn_ra.h"

#include "svn_private_config.h"
#endif /* !VBOX */

#include <apr_network_io.h>
#include <apr_signal.h>
#include <apr_uuid.h>

static svn_opt_subcommand_t initialize_cmd,
                            synchronize_cmd,
                            copy_revprops_cmd,
                            help_cmd;

enum {
  svnsync_opt_non_interactive = SVN_OPT_FIRST_LONGOPT_ID,
  svnsync_opt_no_auth_cache,
  svnsync_opt_auth_username,
  svnsync_opt_auth_password,
  svnsync_opt_config_dir,
#ifdef VBOX
  svnsync_opt_censor_author,
  svnsync_opt_start_rev,
  svnsync_opt_default_process,
  svnsync_opt_replace_externals,
  svnsync_opt_replace_license,
#endif /* VBOX */
  svnsync_opt_version
};

#define SVNSYNC_OPTS_DEFAULT svnsync_opt_non_interactive, \
                             svnsync_opt_no_auth_cache, \
                             svnsync_opt_auth_username, \
                             svnsync_opt_auth_password, \
                             svnsync_opt_config_dir

#ifdef VBOX
#define SVNSYNC_OPTS_INITIALIZE SVNSYNC_OPTS_DEFAULT, \
  svnsync_opt_censor_author, \
  svnsync_opt_start_rev, \
  svnsync_opt_default_process, \
  svnsync_opt_replace_externals, \
  svnsync_opt_replace_license
#endif /* VBOX */

#ifdef VBOX
static const svn_opt_subcommand_desc2_t svnsync_cmd_table[] =
#else /* !VBOX */
static const svn_opt_subcommand_desc_t svnsync_cmd_table[] =
#endif /* !VBOX */
  {
    { "initialize", initialize_cmd, { "init" },
      N_("usage: svnsync initialize DEST_URL SOURCE_URL\n"
         "\n"
         "Initialize a destination repository for synchronization from\n"
         "another repository.\n"
         "\n"
         "The destination URL must point to the root of a repository with\n"
         "no committed revisions.  The destination repository must allow\n"
         "revision property changes.\n"
         "\n"
         "You should not commit to, or make revision property changes in,\n"
         "the destination repository by any method other than 'svnsync'.\n"
         "In other words, the destination repository should be a read-only\n"
         "mirror of the source repository.\n"),
#ifdef VBOX
      { SVNSYNC_OPTS_INITIALIZE } },
#else /* !VBOX */
      { SVNSYNC_OPTS_DEFAULT } },
#endif /* !VBOX */
    { "synchronize", synchronize_cmd, { "sync" },
      N_("usage: svnsync synchronize DEST_URL\n"
         "\n"
         "Transfer all pending revisions from source to destination.\n"),
      { SVNSYNC_OPTS_DEFAULT } },
    { "copy-revprops", copy_revprops_cmd, { 0 },
      N_("usage: svnsync copy-revprops DEST_URL REV\n"
         "\n"
         "Copy all revision properties for revision REV from source to\n"
         "destination.\n"),
      { SVNSYNC_OPTS_DEFAULT } },
    { "help", help_cmd, { "?", "h" },
      N_("usage: svnsync help [SUBCOMMAND...]\n"
         "\n"
         "Describe the usage of this program or its subcommands.\n"),
      { 0 } },
    { NULL, NULL, { 0 }, NULL, { 0 } }
  };

static const apr_getopt_option_t svnsync_options[] =
  {
    {"non-interactive", svnsync_opt_non_interactive, 0,
                       N_("do no interactive prompting") },
    {"no-auth-cache",  svnsync_opt_no_auth_cache, 0,
                       N_("do not cache authentication tokens") },
    {"username",       svnsync_opt_auth_username, 1,
                       N_("specify a username ARG") },
    {"password",       svnsync_opt_auth_password, 1,
                       N_("specify a password ARG") },
    {"config-dir",     svnsync_opt_config_dir, 1,
                       N_("read user configuration files from directory ARG")},
#ifdef VBOX
    {"censor-author",  svnsync_opt_censor_author, 0,
                       N_("ignore all revisions before ARG")},
    {"start-rev",      svnsync_opt_start_rev, 1,
                       N_("ignore all revisions before ARG")},
    {"default-process", svnsync_opt_default_process, 1,
                       N_("set default for processing files and directories to ARG")},
    {"replace-externals", svnsync_opt_replace_externals, 0,
                       N_("replace svn:externals properties")},
    {"replace-license", svnsync_opt_replace_license, 0,
                       N_("replace license properties")},
#endif /* VBOX */
    {"version",        svnsync_opt_version, 0,
                       N_("show program version information")},
    {"help",           'h', 0,
                       N_("show help on a subcommand")},
    {NULL,             '?', 0,
                       N_("show help on a subcommand")},
    { 0, 0, 0, 0 }
  };

typedef struct {
  svn_auth_baton_t *auth_baton;
  svn_boolean_t non_interactive;
  svn_boolean_t no_auth_cache;
  const char *auth_username;
  const char *auth_password;
  const char *config_dir;
#ifdef VBOX
  svn_boolean_t censor_author;
  svn_revnum_t start_rev;
  const char *default_process;
  svn_boolean_t replace_externals;
  svn_boolean_t replace_license;
#endif /* VBOX */
  apr_hash_t *config;
  svn_boolean_t version;
  svn_boolean_t help;
} opt_baton_t;




/*** Helper functions ***/


/* Global record of whether the user has requested cancellation. */
static volatile sig_atomic_t cancelled = FALSE;


/* Callback function for apr_signal(). */
static void
signal_handler(int signum)
{
  apr_signal(signum, SIG_IGN);
  cancelled = TRUE;
}


/* Cancellation callback function. */
static svn_error_t *
check_cancel(void *baton)
{
  if (cancelled)
    return svn_error_create(SVN_ERR_CANCELLED, NULL, _("Caught signal"));
  else
    return SVN_NO_ERROR;
}


/* Check that the version of libraries in use match what we expect. */
static svn_error_t *
check_lib_versions(void)
{
  static const svn_version_checklist_t checklist[] =
    {
      { "svn_subr",  svn_subr_version },
      { "svn_delta", svn_delta_version },
      { "svn_ra",    svn_ra_version },
      { NULL, NULL }
    };

  SVN_VERSION_DEFINE(my_version);

  return svn_ver_check_list(&my_version, checklist);
}


#ifdef VBOX
/* Get the export properties of the file/directory in PATH, as of REVISION.
 * Cannot be done in the change_*_props callbacks, as they are invoked too
 * late. Need to know before adding/opening a file/directory. */
static svn_error_t *
get_props_sync(svn_ra_session_t *session,
               const char *default_process,
               svn_boolean_t parent_deflt,
               svn_boolean_t parent_rec,
               const char *path,
               svn_revnum_t revision,
               svn_boolean_t *proc,
               svn_boolean_t *deflt,
               svn_boolean_t *rec,
               apr_pool_t *pool)
{
  apr_hash_t *props;
  svn_string_t *value;
  svn_node_kind_t nodekind;

  SVN_ERR(svn_ra_check_path(session, path, revision, &nodekind, pool));
  if (nodekind == svn_node_file)
    SVN_ERR(svn_ra_get_file(session, path, revision, NULL, NULL, &props, pool));
  else
    SVN_ERR(svn_ra_get_dir2(session, NULL, NULL, &props, path, revision, 0, pool));
  value = apr_hash_get(props, SVNSYNC_PROP_PROCESS, APR_HASH_KEY_STRING);
  if (value)
    *proc = !strcmp(value->data, "export");
  else
    *proc = parent_deflt;
  if (deflt)
  {
    value = apr_hash_get(props, SVNSYNC_PROP_DEFAULT, APR_HASH_KEY_STRING);
    if (value)
    {
      if (!strcmp(value->data, "export"))
      {
        *deflt = TRUE;
        *rec = FALSE;
      }
      else if (!strcmp(value->data, "export-recursive"))
      {
        *proc = TRUE;
        *deflt = TRUE;
        *rec = TRUE;
      }
      else
      {
        *deflt = FALSE;
        *rec = TRUE;
      }
    }
    else
    {
      if (parent_rec)
      {
        *deflt = parent_deflt;
        *rec = TRUE;
      }
      else
      {
        *deflt = !strcmp(default_process, "export");
        *rec = FALSE;
      }
    }
  }

  return SVN_NO_ERROR;
}
#endif /* VBOX */


/* Acquire a lock (of sorts) on the repository associated with the
 * given RA SESSION.
 */
static svn_error_t *
get_lock(svn_ra_session_t *session, apr_pool_t *pool)
{
  char hostname_str[APRMAXHOSTLEN + 1] = { 0 };
  svn_string_t *mylocktoken, *reposlocktoken;
  apr_status_t apr_err;
  apr_pool_t *subpool;
  int i;

  apr_err = apr_gethostname(hostname_str, sizeof(hostname_str), pool);
  if (apr_err)
    return svn_error_wrap_apr(apr_err, _("Can't get local hostname"));

  mylocktoken = svn_string_createf(pool, "%s:%s", hostname_str,
                                   svn_uuid_generate(pool));

  subpool = svn_pool_create(pool);

  for (i = 0; i < 10; ++i)
    {
      svn_pool_clear(subpool);

      SVN_ERR(svn_ra_rev_prop(session, 0, SVNSYNC_PROP_LOCK, &reposlocktoken,
                              subpool));

      if (reposlocktoken)
        {
          /* Did we get it?   If so, we're done, otherwise we sleep. */
          if (strcmp(reposlocktoken->data, mylocktoken->data) == 0)
            return SVN_NO_ERROR;
          else
            {
              SVN_ERR(svn_cmdline_printf
                      (pool, _("Failed to get lock on destination "
                               "repos, currently held by '%s'\n"),
                       reposlocktoken->data));

              apr_sleep(apr_time_from_sec(1));
            }
        }
      else
        {
#ifdef VBOX
          SVN_ERR(svn_ra_change_rev_prop2(session, 0, SVNSYNC_PROP_LOCK,
                                          NULL, mylocktoken, subpool));
#else /* !VBOX */
          SVN_ERR(svn_ra_change_rev_prop(session, 0, SVNSYNC_PROP_LOCK,
                                         mylocktoken, subpool));
#endif /* !VBOX */
        }
    }

  return svn_error_createf(APR_EINVAL, NULL,
                           "Couldn't get lock on destination repos "
                           "after %d attempts\n", i);
}


typedef svn_error_t *(*with_locked_func_t)(svn_ra_session_t *session,
                                           void *baton,
                                           apr_pool_t *pool);


/* Lock the repository associated with RA SESSION, then execute the
 * given FUNC/BATON pair while holding the lock.  Finally, drop the
 * lock once it finishes.
 */
static svn_error_t *
with_locked(svn_ra_session_t *session,
            with_locked_func_t func,
            void *baton,
            apr_pool_t *pool)
{
  svn_error_t *err, *err2;

  SVN_ERR(get_lock(session, pool));

  err = func(session, baton, pool);

#ifdef VBOX
  err2 = svn_ra_change_rev_prop2(session, 0, SVNSYNC_PROP_LOCK, NULL, NULL, pool);
#else /* !VBOX */
  err2 = svn_ra_change_rev_prop(session, 0, SVNSYNC_PROP_LOCK, NULL, pool);
#endif /* !VBOX */
  if (err2 && err)
    {
      svn_error_clear(err2); /* XXX what to do here? */

      return err;
    }
  else if (err2)
    {
      return err2;
    }
  else
    {
      return err;
    }
}


/* Callback function for the RA session's open_tmp_file()
 * requirements.
 */
static svn_error_t *
open_tmp_file(apr_file_t **fp, void *callback_baton, apr_pool_t *pool)
{
#ifdef VBOX
  return svn_io_open_unique_file3(fp, NULL, NULL,
                                  svn_io_file_del_on_pool_cleanup,
                                  pool, pool);
#else /* !VBOX */
  const char *path;

  SVN_ERR(svn_io_temp_dir(&path, pool));

  path = svn_path_join(path, "tempfile", pool);

  SVN_ERR(svn_io_open_unique_file2(fp, NULL, path, ".tmp",
                                   svn_io_file_del_on_close, pool));

  return SVN_NO_ERROR;
#endif
}


/* Return SVN_NO_ERROR iff URL identifies the root directory of the
 * repository associated with RA session SESS.
 */
static svn_error_t *
check_if_session_is_at_repos_root(svn_ra_session_t *sess,
                                  const char *url,
                                  apr_pool_t *pool)
{
  const char *sess_root;

#ifdef VBOX
  SVN_ERR(svn_ra_get_repos_root2(sess, &sess_root, pool));
#else /* !VBOX */
  SVN_ERR(svn_ra_get_repos_root(sess, &sess_root, pool));
#endif /* !VBOX */

  if (strcmp(url, sess_root) == 0)
    return SVN_NO_ERROR;
  else
    return svn_error_createf
      (APR_EINVAL, NULL,
       _("Session is rooted at '%s' but the repos root is '%s'"),
       url, sess_root);
}


/* Copy all the revision properties, except for those that have the
 * "svn:sync-" prefix, from revision REV of the repository associated
 * with RA session FROM_SESSION, to the repository associated with RA
 * session TO_SESSION.
 *
 * If SYNC is TRUE, then properties on the destination revision that
 * do not exist on the source revision will be removed.
 */
static svn_error_t *
copy_revprops(svn_ra_session_t *from_session,
              svn_ra_session_t *to_session,
              svn_revnum_t rev,
#ifdef VBOX
              svn_revnum_t rev_to,
              svn_boolean_t censor_author,
#endif /* VBOX */
              svn_boolean_t sync,
              apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_hash_t *revprops, *existing_props;
  svn_boolean_t saw_sync_props = FALSE;
  apr_hash_index_t *hi;

  if (sync)
#ifdef VBOX
    SVN_ERR(svn_ra_rev_proplist(to_session, rev_to, &existing_props, pool));
#else /* !VBOX */
    SVN_ERR(svn_ra_rev_proplist(to_session, rev, &existing_props, pool));
#endif /* !VBOX */

  SVN_ERR(svn_ra_rev_proplist(from_session, rev, &revprops, pool));

  for (hi = apr_hash_first(pool, revprops); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;

      svn_pool_clear(subpool);
      apr_hash_this(hi, &key, NULL, &val);

      if (strncmp(key, SVNSYNC_PROP_PREFIX,
                  sizeof(SVNSYNC_PROP_PREFIX) - 1) == 0)
        saw_sync_props = TRUE;
      else
#ifdef VBOX
        if (   (!censor_author || strncmp(key, SVN_PROP_REVISION_AUTHOR,
                                          sizeof(SVN_PROP_REVISION_AUTHOR) - 1))
            && ((rev_to != 1) || (rev == rev_to) || strncmp(key, SVN_PROP_REVISION_LOG,
                                                            sizeof(SVN_PROP_REVISION_LOG)-1)))
          SVN_ERR(svn_ra_change_rev_prop2(to_session, rev_to, key, NULL, val, subpool));
#else /* !VBOX */
        SVN_ERR(svn_ra_change_rev_prop(to_session, rev, key, val, subpool));
#endif /* !VBOX */

      if (sync)
        apr_hash_set(existing_props, key, APR_HASH_KEY_STRING, NULL);
    }

  if (sync)
    {
      for (hi = apr_hash_first(pool, existing_props);
           hi;
           hi = apr_hash_next(hi))
        {
          const void *name;

          svn_pool_clear(subpool);

          apr_hash_this(hi, &name, NULL, NULL);

#ifdef VBOX
          SVN_ERR(svn_ra_change_rev_prop2(to_session, rev_to, name, NULL, NULL,
                                          subpool));
#else /* !VBOX */
          SVN_ERR(svn_ra_change_rev_prop(to_session, rev, name, NULL,
                                         subpool));
#endif /* !VBOX */
        }
    }

#ifdef VBOX
  if (saw_sync_props)
  {
    if (rev_to == rev)
      SVN_ERR(svn_cmdline_printf(subpool,
                                 _("Copied properties for revision %ld "
                                   "(%s* properties skipped).\n"),
                                 rev_to, SVNSYNC_PROP_PREFIX));
    else
      SVN_ERR(svn_cmdline_printf(subpool,
                                 _("Copied properties for revision %ld "
                                   "(%ld in source repository) "
                                   "(%s* properties skipped).\n"),
                                 rev_to, rev, SVNSYNC_PROP_PREFIX));
  }
  else
  {
    if (rev_to == rev)
      SVN_ERR(svn_cmdline_printf(subpool,
                                 _("Copied properties for revision %ld.\n"),
                                 rev_to));
    else
      SVN_ERR(svn_cmdline_printf(subpool,
                                 _("Copied properties for revision %ld "
                                   "(%ld in source repository).\n"),
                                 rev_to, rev));
  }
#else /* !VBOX */
  if (saw_sync_props)
    SVN_ERR(svn_cmdline_printf(subpool,
                               _("Copied properties for revision %ld "
                                 "(%s* properties skipped).\n"),
                               rev, SVNSYNC_PROP_PREFIX));
  else
    SVN_ERR(svn_cmdline_printf(subpool,
                               _("Copied properties for revision %ld.\n"),
                               rev));
#endif /* !VBOX */

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


#ifdef VBOX

/*** Initialization Editor ***/

/* This editor has the job of creating the initial state for a destination
 * repository that starts without history before a certain starting revision.
 * Going the export/import way would lose the versioned properties. Unversioned
 * properties are dropped, because they don't belong to the initial snapshot.
 *
 * It needs to create an entire tree in a single commit.
 */


/* InitEdit baton */
typedef struct {
  const svn_delta_editor_t *wrapped_editor;
  void *wrapped_edit_baton;
  svn_ra_session_t *from_session_prop;
  svn_boolean_t censor_author;
  svn_revnum_t current;
  const char *default_process;
  svn_boolean_t replace_externals;
  svn_boolean_t replace_license;
} initedit_baton_t;


/* InitDir baton */
typedef struct {
  initedit_baton_t *edit_baton;
  void *wrapped_dir_baton;
  svn_boolean_t process_default;
  svn_boolean_t process_recursive;
  svn_boolean_t process;
} initdir_baton_t;


/* InitFile baton */
typedef struct {
  initedit_baton_t *edit_baton;
  void *wrapped_file_baton;
  svn_boolean_t process;
} initfile_baton_t;


/*** Editor vtable functions ***/

static svn_error_t *
init_set_target_revision(void *edit_baton,
                         svn_revnum_t target_revision,
                         apr_pool_t *pool)
{
  initedit_baton_t *eb = edit_baton;

  DX(fprintf(stderr, "init set_target_revision %ld\n", target_revision);)
  target_revision = 1;
  DX(fprintf(stderr, "init override set_target_revision %ld\n", target_revision);)
  SVN_ERR(eb->wrapped_editor->set_target_revision(eb->wrapped_edit_baton,
                                                  target_revision, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
init_open_root(void *edit_baton,
               svn_revnum_t base_revision,
               apr_pool_t *pool,
               void **root_baton)
{
  initedit_baton_t *eb = edit_baton;
  initdir_baton_t *db = apr_pcalloc(pool, sizeof(*db));

  DX(fprintf(stderr, "init open_root %ld\n", base_revision);)
  base_revision = 1;
  DX(fprintf(stderr, "init override open_root %ld\n", base_revision);)
  SVN_ERR(get_props_sync(eb->from_session_prop, eb->default_process, TRUE,
                         FALSE,"", eb->current, &db->process,
                         &db->process_default, &db->process_recursive, pool));
  DX(fprintf(stderr, "  %s\n", db->process ? "EXPORT" : "IGNORE");)
  if (db->process)
    SVN_ERR(eb->wrapped_editor->open_root(eb->wrapped_edit_baton,
                                          base_revision - 1, pool,
                                          &db->wrapped_dir_baton));

  db->edit_baton = edit_baton;
  *root_baton = db;

  return SVN_NO_ERROR;
}

static svn_error_t *
init_add_directory(const char *path,
                   void *parent_baton,
                   const char *copyfrom_path,
                   svn_revnum_t copyfrom_rev,
                   apr_pool_t *pool,
                   void **child_baton)
{
  initdir_baton_t *pb = parent_baton;
  initedit_baton_t *eb = pb->edit_baton;
  initdir_baton_t *db = apr_pcalloc(pool, sizeof(*db));

  DX(fprintf(stderr, "init add_directory %s\n", path);)
  SVN_ERR(get_props_sync(eb->from_session_prop, eb->default_process,
                         pb->process_default, pb->process_recursive, path,
                         eb->current, &db->process, &db->process_default,
                         &db->process_recursive, pool));
  DX(fprintf(stderr, "  %s\n", db->process ? "EXPORT" : "IGNORE");)
  if (db->process && !pb->process)
  {
    /* Parent directory is not exported, but this directory is. Warn user,
     * because this can lead to destination repository weirdness. */
    SVN_ERR(svn_cmdline_printf(pool,
                               _("The parent of directory %s is not exported, "
                                 "but the directory is. FIX ASAP!\n"), path));
    db->process = FALSE;
  }
  if (db->process)
    SVN_ERR(eb->wrapped_editor->add_directory(path, pb->wrapped_dir_baton,
                                              NULL, SVN_IGNORED_REVNUM, pool,
                                              &db->wrapped_dir_baton));

  db->edit_baton = eb;
  *child_baton = db;

  return SVN_NO_ERROR;
}

static svn_error_t *
init_close_directory(void *dir_baton,
                     apr_pool_t *pool)
{
  initdir_baton_t *db = dir_baton;
  initedit_baton_t *eb = db->edit_baton;

  DX(fprintf(stderr, "init close_directory\n");)
  DX(fprintf(stderr, "  %s\n", db->process ? "EXPORT" : "IGNORE");)
  if (db->process)
    SVN_ERR(eb->wrapped_editor->close_directory(db->wrapped_dir_baton, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
init_add_file(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_rev,
              apr_pool_t *pool,
              void **file_baton)
{
  initdir_baton_t *pb = parent_baton;
  initedit_baton_t *eb = pb->edit_baton;
  initfile_baton_t *fb = apr_pcalloc(pool, sizeof(*fb));

  DX(fprintf(stderr, "init add_file %s\n", path);)
  SVN_ERR(get_props_sync(eb->from_session_prop, eb->default_process,
                         pb->process_default, pb->process_recursive,
                         path, eb->current, &fb->process, NULL, NULL, pool));
  DX(fprintf(stderr, "  %s\n", fb->process ? "EXPORT" : "IGNORE");)
  if (fb->process && !pb->process)
  {
    /* Parent directory is not exported, but this file is. Warn user,
     * because this can lead to destination repository weirdness. */
    SVN_ERR(svn_cmdline_printf(pool,
                               _("The parent of file %s is not exported, "
                                 "but the file is. FIX ASAP!\n"), path));
    fb->process = FALSE;
  }
  if (fb->process)
    SVN_ERR(eb->wrapped_editor->add_file(path, pb->wrapped_dir_baton,
                                         NULL, SVN_IGNORED_REVNUM, pool,
                                         &fb->wrapped_file_baton));

  fb->edit_baton = eb;
  *file_baton = fb;

  return SVN_NO_ERROR;
}


static svn_error_t *
init_apply_textdelta(void *file_baton,
                     const char *base_checksum,
                     apr_pool_t *pool,
                     svn_txdelta_window_handler_t *handler,
                     void **handler_baton)
{
  initfile_baton_t *fb = file_baton;
  initedit_baton_t *eb = fb->edit_baton;

  DX(fprintf(stderr, "init apply_textdelta\n");)
  DX(fprintf(stderr, "  %s\n", fb->process ? "EXPORT" : "IGNORE");)
  if (fb->process)
    SVN_ERR(eb->wrapped_editor->apply_textdelta(fb->wrapped_file_baton,
                                                base_checksum, pool,
                                                handler, handler_baton));
  else
  {
    /* Must provide a window handler, there's no way of telling our caller
     * to throw away its data as we're not interested. */
    *handler = svn_delta_noop_window_handler;
    *handler_baton = NULL;
  }

  return SVN_NO_ERROR;
}

static svn_error_t *
init_close_file(void *file_baton,
                const char *text_checksum,
                apr_pool_t *pool)
{
  initfile_baton_t *fb = file_baton;
  initedit_baton_t *eb = fb->edit_baton;

  DX(fprintf(stderr, "init close_file\n");)
  DX(fprintf(stderr, "  %s\n", fb->process ? "EXPORT" : "IGNORE");)
  if (fb->process)
    SVN_ERR(eb->wrapped_editor->close_file(fb->wrapped_file_baton,
                                           text_checksum, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
init_change_file_prop(void *file_baton,
                      const char *name,
                      const svn_string_t *value,
                      apr_pool_t *pool)
{
  initfile_baton_t *fb = file_baton;
  initedit_baton_t *eb = fb->edit_baton;

  DX(fprintf(stderr, "init change_file_prop %s\n", name);)
  DX(fprintf(stderr, "  %s\n", fb->process ? "EXPORT" : "IGNORE");)
  if (svn_property_kind2(name) != svn_prop_regular_kind)
    return SVN_NO_ERROR;
  if (!strcmp(name, "cvs2svn:cvs-rev"))
    return SVN_NO_ERROR;
  if (eb->replace_license)
  {
    /* Throw away the normal license property and replace it by the value
     * of svn:sync-license, if present. */
    if (!strcmp(name, SVN_PROP_LICENSE))
      return SVN_NO_ERROR;
    if (!strcmp(name, SVNSYNC_PROP_LICENSE))
      name = SVN_PROP_LICENSE;
  }
  /* Never export any svn:sync-* properties. */
  if (!strncmp(name, SVNSYNC_PROP_PREFIX, sizeof(SVNSYNC_PROP_PREFIX) - 1))
    return SVN_NO_ERROR;

  if (fb->process)
    SVN_ERR(eb->wrapped_editor->change_file_prop(fb->wrapped_file_baton,
                                                 name, value, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
init_change_dir_prop(void *dir_baton,
                     const char *name,
                     const svn_string_t *value,
                     apr_pool_t *pool)
{
  initdir_baton_t *db = dir_baton;
  initedit_baton_t *eb = db->edit_baton;

  DX(fprintf(stderr, "init change_dir_prop %s\n", name);)
  DX(fprintf(stderr, "  %s\n", db->process ? "EXPORT" : "IGNORE");)
  if (svn_property_kind2(name) != svn_prop_regular_kind)
    return SVN_NO_ERROR;
  if (!strcmp(name, "cvs2svn:cvs-rev"))
    return SVN_NO_ERROR;
  if (eb->replace_externals)
  {
    /* Throw away the normal externals and replace them by the value of
     * svn:sync-externals, if present. */
    if (!strcmp(name, SVN_PROP_EXTERNALS))
      return SVN_NO_ERROR;
    if (!strcmp(name, SVNSYNC_PROP_EXTERNALS))
      name = SVN_PROP_EXTERNALS;
  }
  /* Never export any svn:sync-* properties. */
  if (!strncmp(name, SVNSYNC_PROP_PREFIX, sizeof(SVNSYNC_PROP_PREFIX) - 1))
    return SVN_NO_ERROR;

  if (db->process)
    SVN_ERR(eb->wrapped_editor->change_dir_prop(db->wrapped_dir_baton,
                                                name, value, pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
init_close_edit(void *edit_baton,
                apr_pool_t *pool)
{
  initedit_baton_t *eb = edit_baton;

  DX(fprintf(stderr, "init close_edit\n");)
  SVN_ERR(eb->wrapped_editor->close_edit(eb->wrapped_edit_baton, pool));

  return SVN_NO_ERROR;
}

/*** Initialization Editor factory function ***/

/* Set WRAPPED_EDITOR and WRAPPED_EDIT_BATON to an editor/baton pair
 * that wraps our own commit EDITOR/EDIT_BATON.  BASE_REVISION is the
 * revision on which the driver of this returned editor will be basing
 * the commit.  TO_URL is the URL of the root of the repository into
 * which the commit is being made.
 */
static svn_error_t *
get_init_editor(const svn_delta_editor_t *wrapped_editor,
                void *wrapped_edit_baton,
                svn_revnum_t start_rev,
                svn_ra_session_t *prop_session,
                const char *default_process,
                svn_boolean_t censor_author,
                svn_boolean_t replace_externals,
                svn_boolean_t replace_license,
                const svn_delta_editor_t **editor,
                void **edit_baton,
                apr_pool_t *pool)
{
  svn_delta_editor_t *tree_editor = svn_delta_default_editor(pool);
  initedit_baton_t *eb = apr_pcalloc(pool, sizeof(*eb));

  tree_editor->set_target_revision = init_set_target_revision;
  tree_editor->open_root = init_open_root;
  tree_editor->add_directory = init_add_directory;
  tree_editor->change_dir_prop = init_change_dir_prop;
  tree_editor->close_directory = init_close_directory;
  tree_editor->add_file = init_add_file;
  tree_editor->apply_textdelta = init_apply_textdelta;
  tree_editor->close_file = init_close_file;
  tree_editor->change_file_prop = init_change_file_prop;
  tree_editor->close_edit = init_close_edit;

  eb->wrapped_editor = wrapped_editor;
  eb->wrapped_edit_baton = wrapped_edit_baton;
  eb->current = start_rev;
  eb->default_process = default_process;
  eb->replace_externals = replace_externals;
  eb->replace_license = replace_license;
  eb->from_session_prop = prop_session;

  *editor = tree_editor;
  *edit_baton = eb;

  return SVN_NO_ERROR;
}


#endif /* VBOX */

/*** `svnsync init' ***/

/* Baton for initializing the destination repository while locked. */
typedef struct {
  const char *from_url;
  const char *to_url;
  apr_hash_t *config;
#ifdef VBOX
  svn_boolean_t censor_author;
  svn_revnum_t start_rev;
  const char *default_process;
  svn_boolean_t replace_externals;
  svn_boolean_t replace_license;
#endif /* VBOX */
  svn_ra_callbacks2_t *callbacks;
} init_baton_t;


#ifdef VBOX
/* Implements `svn_commit_callback2_t' interface. */
static svn_error_t *
init_commit_callback(const svn_commit_info_t *commit_info,
                     void *baton,
                     apr_pool_t *pool)
{
  init_baton_t *sb = baton;

  SVN_ERR(svn_cmdline_printf(pool, _("Imported source revision %ld as revision %ld.\n"),
                             sb->start_rev, commit_info->revision));

  return SVN_NO_ERROR;
}
#endif /* VBOX */


/* Initialize the repository associated with RA session TO_SESSION,
 * using information found in baton B, while the repository is
 * locked.  Implements `with_locked_func_t' interface.
 */
static svn_error_t *
do_initialize(svn_ra_session_t *to_session, void *b, apr_pool_t *pool)
{
  svn_ra_session_t *from_session;
  init_baton_t *baton = b;
  svn_string_t *from_url;
  svn_revnum_t latest;
  const char *uuid;
#ifdef VBOX
  svn_string_t *start_rev_str;
  const char *default_process;
  svn_ra_session_t *from_session_prop;
#endif /* VBOX */

  /* First, sanity check to see that we're copying into a brand new repos. */

  SVN_ERR(svn_ra_get_latest_revnum(to_session, &latest, pool));

  if (latest != 0)
    return svn_error_create
      (APR_EINVAL, NULL,
       _("Cannot initialize a repository with content in it"));

  /* And check to see if anyone's run initialize on it before...  We
     may want a --force option to override this check. */

  SVN_ERR(svn_ra_rev_prop(to_session, 0, SVNSYNC_PROP_FROM_URL,
                          &from_url, pool));

  if (from_url)
    return svn_error_createf
      (APR_EINVAL, NULL,
       _("Destination repository is already synchronizing from '%s'"),
       from_url->data);

  /* Now fill in our bookkeeping info in the dest repository. */

#ifdef VBOX
  SVN_ERR(svn_ra_open4(&from_session, NULL, baton->from_url, NULL, baton->callbacks,
                       baton, baton->config, pool));
#else /* !VBOX */
  SVN_ERR(svn_ra_open2(&from_session, baton->from_url, baton->callbacks,
                       baton, baton->config, pool));
#endif /* !VBOX */

  SVN_ERR(check_if_session_is_at_repos_root(from_session, baton->from_url,
                                            pool));

#ifdef VBOX
  SVN_ERR(svn_ra_change_rev_prop2(to_session, 0, SVNSYNC_PROP_FROM_URL, NULL,
                                  svn_string_create(baton->from_url, pool),
                                  pool));
#else /* !VBOX */
  SVN_ERR(svn_ra_change_rev_prop(to_session, 0, SVNSYNC_PROP_FROM_URL,
                                 svn_string_create(baton->from_url, pool),
                                 pool));
#endif /* !VBOX */

#ifdef VBOX
  SVN_ERR(svn_ra_get_uuid2(from_session, &uuid, pool));
#else /* !VBOX */
  SVN_ERR(svn_ra_get_uuid(from_session, &uuid, pool));
#endif /* !VBOX */

#ifdef VBOX
  SVN_ERR(svn_ra_change_rev_prop2(to_session, 0, SVNSYNC_PROP_FROM_UUID, NULL,
                                 svn_string_create(uuid, pool), pool));
#else /* !VBOX */
  SVN_ERR(svn_ra_change_rev_prop(to_session, 0, SVNSYNC_PROP_FROM_UUID,
                                 svn_string_create(uuid, pool), pool));
#endif /* !VBOX */

#ifdef VBOX
  start_rev_str = svn_string_create(apr_psprintf(pool, "%ld", baton->start_rev),
                                    pool);
  SVN_ERR(svn_ra_change_rev_prop2(to_session, 0, SVNSYNC_PROP_START_REV, NULL,
                                  start_rev_str, pool));
  SVN_ERR(svn_ra_change_rev_prop2(to_session, 0, SVNSYNC_PROP_LAST_MERGED_REV, NULL,
                                  start_rev_str, pool));
  if (!baton->default_process)
    default_process = "export";
  else
    default_process = baton->default_process;
  SVN_ERR(svn_ra_change_rev_prop2(to_session, 0, SVNSYNC_PROP_DEFAULT_PROCESS, NULL,
                                  svn_string_create(default_process, pool),
                                  pool));
  if (baton->censor_author)
    SVN_ERR(svn_ra_change_rev_prop2(to_session, 0,
                                    SVNSYNC_PROP_CENSOR_AUTHOR, NULL,
                                    svn_string_create("", pool), pool));
  if (baton->replace_externals)
    SVN_ERR(svn_ra_change_rev_prop2(to_session, 0,
                                    SVNSYNC_PROP_REPLACE_EXTERNALS, NULL,
                                    svn_string_create("", pool), pool));
  if (baton->replace_license)
    SVN_ERR(svn_ra_change_rev_prop2(to_session, 0,
                                    SVNSYNC_PROP_REPLACE_LICENSE, NULL,
                                    svn_string_create("", pool), pool));
#else /* !VBOX */
  SVN_ERR(svn_ra_change_rev_prop(to_session, 0, SVNSYNC_PROP_LAST_MERGED_REV,
                                 svn_string_create("0", pool), pool));
#endif /* !VBOX */

  /* Finally, copy all non-svnsync revprops from rev 0 of the source
     repos into the dest repos. */

#ifdef VBOX
  SVN_ERR(copy_revprops(from_session, to_session, 0, 0, baton->censor_author, FALSE, pool));
#else /* !VBOX */
  SVN_ERR(copy_revprops(from_session, to_session, 0, FALSE, pool));
#endif /* !VBOX */

  /** @todo It would be nice if we could set the dest repos UUID to be
     equal to the UUID of the source repos, at least optionally.  That
     way people could check out/log/diff using a local fast mirror,
     but switch --relocate to the actual final repository in order to
     make changes...  But at this time, the RA layer doesn't have a
     way to set a UUID. */

#ifdef VBOX
  if (baton->start_rev > 0)
  {
    const svn_delta_editor_t *commit_editor;
    const svn_delta_editor_t *cancel_editor;
    const svn_delta_editor_t *init_editor;
    const svn_ra_reporter3_t *reporter;
    void *commit_baton;
    void *cancel_baton;
    void *init_baton;
    void *report_baton;
    apr_hash_t *logrevprop;

    logrevprop = apr_hash_make(pool);
    apr_hash_set(logrevprop, SVN_PROP_REVISION_LOG, APR_HASH_KEY_STRING,
                 svn_string_create("import", pool));
    SVN_ERR(svn_ra_get_commit_editor3(to_session, &commit_editor, &commit_baton,
                                      logrevprop,
                                      init_commit_callback, baton,
                                      NULL, FALSE, pool));

    SVN_ERR(svn_ra_open4(&from_session_prop, NULL, baton->from_url, NULL,
                         baton->callbacks, baton, baton->config, pool));

    SVN_ERR(get_init_editor(commit_editor, commit_baton, baton->start_rev,
                            from_session_prop, baton->default_process,
                            baton->censor_author,
                            baton->replace_externals, baton->replace_license,
                            &init_editor, &init_baton, pool));

    SVN_ERR(svn_delta_get_cancellation_editor(check_cancel, NULL,
                                              init_editor, init_baton,
                                              &cancel_editor, &cancel_baton,
                                              pool));

    /* Run it via an update reporter. */
    SVN_ERR(svn_ra_do_update3(from_session, &reporter, &report_baton,
                              baton->start_rev, "", svn_depth_infinity, FALSE,
                              FALSE, cancel_editor, cancel_baton, pool, pool));
    SVN_ERR(reporter->set_path(report_baton, "", baton->start_rev,
                               svn_depth_infinity, TRUE, NULL, pool));
    SVN_ERR(reporter->finish_report(report_baton, pool));
  }

  /* Finally, copy all non-svnsync revprops from rev start_rev of the source
     repos into rev 1 of the dest repos. */

  SVN_ERR(copy_revprops(from_session, to_session, baton->start_rev, 1, baton->censor_author, FALSE, pool));
#endif /* VBOX */

  return SVN_NO_ERROR;
}


/* SUBCOMMAND: init */
static svn_error_t *
initialize_cmd(apr_getopt_t *os, void *b, apr_pool_t *pool)
{
  svn_ra_callbacks2_t callbacks = { 0 };
  const char *to_url, *from_url;
  svn_ra_session_t *to_session;
  opt_baton_t *opt_baton = b;
  apr_array_header_t *args;
  init_baton_t baton;

  SVN_ERR(svn_opt_parse_num_args(&args, os, 2, pool));

  to_url = svn_uri_canonicalize(APR_ARRAY_IDX(args, 0, const char *), pool);
  from_url = svn_uri_canonicalize(APR_ARRAY_IDX(args, 1, const char *), pool);

  if (! svn_path_is_url(to_url))
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Path '%s' is not a URL"), to_url);
  if (! svn_path_is_url(from_url))
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Path '%s' is not a URL"), from_url);

  baton.to_url = to_url;
  baton.from_url = from_url;
  baton.config = opt_baton->config;
#ifdef VBOX
  baton.start_rev = opt_baton->start_rev;
  baton.default_process = opt_baton->default_process;
  baton.censor_author = opt_baton->censor_author;
  baton.replace_externals = opt_baton->replace_externals;
  baton.replace_license = opt_baton->replace_license;
#endif /* VBOX */

  callbacks.open_tmp_file = open_tmp_file;
  callbacks.auth_baton = opt_baton->auth_baton;

  baton.callbacks = &callbacks;

#ifdef VBOX
  SVN_ERR(svn_ra_open4(&to_session, NULL, baton.to_url, NULL,
                       &callbacks, &baton, baton.config, pool));
#else /* !VBOX */
  SVN_ERR(svn_ra_open2(&to_session,
                       baton.to_url,
                       &callbacks,
                       &baton,
                       baton.config,
                       pool));
#endif /* !VBOX */

  SVN_ERR(check_if_session_is_at_repos_root(to_session, baton.to_url, pool));

  SVN_ERR(with_locked(to_session, do_initialize, &baton, pool));

  return SVN_NO_ERROR;
}



/*** Synchronization Editor ***/

/* This editor has a couple of jobs.
 *
 * First, it needs to filter out the propchanges that can't be passed over
 * libsvn_ra.
 *
 * Second, it needs to adjust for the fact that we might not actually have
 * permission to see all of the data from the remote repository, which means
 * we could get revisions that are totally empty from our point of view.
 *
 * Third, it needs to adjust copyfrom paths, adding the root url for the
 * destination repository to the beginning of them.
 */


/* Edit baton */
typedef struct {
  const svn_delta_editor_t *wrapped_editor;
  void *wrapped_edit_baton;
  const char *to_url;  /* URL we're copying into, for correct copyfrom URLs */
  svn_boolean_t called_open_root;
#ifdef VBOX
  svn_ra_session_t *from_session_prop;
  svn_ra_session_t *to_session_prop;
  svn_boolean_t changeset_live;
  svn_revnum_t start_rev;
  svn_revnum_t current;
  const char *default_process;
  svn_boolean_t replace_externals;
  svn_boolean_t replace_license;
#endif /* VBOX */
  svn_revnum_t base_revision;
} edit_baton_t;


/* A dual-purpose baton for files and directories. */
typedef struct {
  void *edit_baton;
#ifdef VBOX
  svn_boolean_t prev_process, process;
  svn_boolean_t prev_process_default, process_default;
  svn_boolean_t prev_process_recursive, process_recursive;
  svn_boolean_t ignore_everything; /* Ignore operations on this dir/file. */
  svn_boolean_t ignore_everything_rec; /* Recursively ignore operations on subdirs/files. */
#endif /* VBOX */
  void *wrapped_node_baton;
} node_baton_t;


#ifdef VBOX
static svn_revnum_t
lookup_revnum(svn_ra_session_t *to_session,
              svn_revnum_t revnum,
              apr_pool_t *pool)
{
  svn_error_t *err;
  svn_string_t *revprop;

  err = svn_ra_rev_prop(to_session, 0, apr_psprintf(pool,
                                                    SVNSYNC_PROP_REV__FMT,
                                                    revnum),
                        &revprop, pool);
  if (err || !revprop)
    return SVN_INVALID_REVNUM;
  else
    return SVN_STR_TO_REV(revprop->data);
}


/* Helper which copies file contents and properties from src to dst. */
static svn_error_t *
copy_file(const char *src_path,
          svn_revnum_t src_rev,
          const char *dst_path,
          void *file_baton,
          void *wrapped_parent_node_baton,
          svn_ra_session_t *from_session,
          apr_pool_t *pool)
{
  node_baton_t *fb = file_baton;
  edit_baton_t *eb = fb->edit_baton;
  apr_pool_t *subpool;
  apr_file_t *tmpfile;
  apr_off_t offset = 0;
  svn_stream_t *filestream;
  svn_stream_t *emptystream = svn_stream_empty(pool);
  svn_txdelta_stream_t *deltastream;
  svn_txdelta_window_t *window;
  svn_txdelta_window_handler_t window_handler;
  void *window_handler_baton;
  apr_hash_t *fileprops;
  apr_hash_index_t *hi;
  svn_error_t *e = NULL;

  e = eb->wrapped_editor->add_file(dst_path, wrapped_parent_node_baton,
                                   NULL, SVN_IGNORED_REVNUM, pool,
                                   &fb->wrapped_node_baton);
  if (e)
  {
    svn_error_clear(e);
    SVN_ERR(eb->wrapped_editor->open_file(dst_path, wrapped_parent_node_baton,
                                          SVN_IGNORED_REVNUM, pool,
                                          &fb->wrapped_node_baton));
  }

  subpool = svn_pool_create(pool);
  /* Copy over contents from src revision in source repository. */
  SVN_ERR(open_tmp_file(&tmpfile, NULL, subpool));
  filestream = svn_stream_from_aprfile2(tmpfile, FALSE, subpool);
  SVN_ERR(svn_ra_get_file(from_session, STRIP_LEADING_SLASH(src_path), src_rev,
                          filestream, NULL, &fileprops, subpool));
  SVN_ERR(svn_io_file_seek(tmpfile, APR_SET, &offset, subpool));

  SVN_ERR(apply_textdelta(file_baton, NULL, subpool, &window_handler,
                          &window_handler_baton));
  svn_txdelta2(&deltastream, emptystream, filestream, FALSE, subpool);
  do
  {
    SVN_ERR(svn_txdelta_next_window(&window, deltastream, subpool));
    window_handler(window, window_handler_baton);
  } while (window);

  SVN_ERR(svn_stream_close(filestream));

  /* Copy over properties from src revision in source repository. */
  for (hi = apr_hash_first(subpool, fileprops); hi; hi = apr_hash_next(hi))
  {
    const void *key;
    void *val;

    apr_hash_this(hi, &key, NULL, &val);
    SVN_ERR(change_file_prop(file_baton, key, val, subpool));
  }

  svn_pool_clear(subpool);
  return SVN_NO_ERROR;
}

/* Helper which copies a directory and all contents from src to dst. */
static svn_error_t *
copy_dir_rec(const char *src_path,
             svn_revnum_t src_rev,
             const char *dst_path,
             void *dir_baton,
             void *wrapped_parent_node_baton,
             svn_ra_session_t *from_session,
             apr_pool_t *pool)
{
  node_baton_t *db = dir_baton;
  edit_baton_t *eb = db->edit_baton;
  apr_pool_t *subpool;
  apr_hash_t *dirents, *dirprops;
  apr_hash_index_t *hi;

  SVN_ERR(eb->wrapped_editor->add_directory(dst_path, wrapped_parent_node_baton,
                                            NULL, SVN_IGNORED_REVNUM, pool,
                                            &db->wrapped_node_baton));

  subpool = svn_pool_create(pool);
  SVN_ERR(svn_ra_get_dir2(from_session, &dirents, NULL, &dirprops, src_path,
                          src_rev, SVN_DIRENT_KIND, subpool));

  /* Copy over files and directories from src revision in source repository. */
  for (hi = apr_hash_first(subpool, dirents); hi; hi = apr_hash_next(hi))
  {
    const void *key;
    void *val;
    svn_dirent_t *val_ent;
    apr_pool_t *oppool;
    const char *from_path, *to_path;

    apr_hash_this(hi, &key, NULL, &val);
    val_ent = (svn_dirent_t *)val;
    oppool = svn_pool_create(subpool);
    from_path = svn_relpath_join(src_path, key, oppool);
    to_path = svn_relpath_join(dst_path, key, oppool);
    switch (val_ent->kind)
    {
      case svn_node_file:
      {
        void *fb;
        /* Need to copy it from the to_path in the src repository (revision
         * current), because that's where the updated (including
         * deltas/properties) version is. */
        SVN_ERR(add_file(to_path, db, from_path, src_rev, oppool, &fb));
        SVN_ERR(close_file(fb, NULL, oppool));
        break;
      }
      case svn_node_dir:
      {
        void *cdb;
        /* Same as above, just for the directory. */
        SVN_ERR(add_directory(to_path, db, from_path, src_rev, oppool, &cdb));
        SVN_ERR(close_directory(cdb, oppool));
        break;
      }
      default:
        return svn_error_create(APR_EINVAL, NULL, _("unexpected svn node kind"));
    }
    svn_pool_clear(oppool);
  }

  /* Copy over properties from src revision in source repository. */
  for (hi = apr_hash_first(subpool, dirprops); hi; hi = apr_hash_next(hi))
  {
    const void *key;
    void *val;

    apr_hash_this(hi, &key, NULL, &val);
    SVN_ERR(change_dir_prop(dir_baton, key, val, subpool));
  }

  svn_pool_clear(subpool);
  return SVN_NO_ERROR;
}
#endif /* VBOX */

/*** Editor vtable functions ***/

static svn_error_t *
set_target_revision(void *edit_baton,
                    svn_revnum_t target_revision,
                    apr_pool_t *pool)
{
  edit_baton_t *eb = edit_baton;
#ifdef VBOX
  DX(fprintf(stderr, "set_target_revision %ld\n", target_revision);)
#endif /* VBOX */
  return eb->wrapped_editor->set_target_revision(eb->wrapped_edit_baton,
                                                 target_revision, pool);
}

static svn_error_t *
open_root(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **root_baton)
{
  edit_baton_t *eb = edit_baton;
#ifdef VBOX
  node_baton_t *db = apr_pcalloc(pool, sizeof(*db));

  DX(fprintf(stderr, "open_root\n");)
  SVN_ERR(get_props_sync(eb->from_session_prop, eb->default_process, TRUE,
                         FALSE, "", eb->current-1, &db->prev_process,
                         &db->prev_process_default,
                         &db->prev_process_recursive, pool));
  SVN_ERR(get_props_sync(eb->from_session_prop, eb->default_process,
                         TRUE, FALSE, "", eb->current, &db->process,
                         &db->process_default, &db->process_recursive, pool));
  DX(fprintf(stderr, "  %s (prev %s)\n", db->process ? "EXPORT" : "IGNORE", db->prev_process ? "EXPORT" : "IGNORE");)
  if (db->process)
  {
    SVN_ERR(eb->wrapped_editor->open_root(eb->wrapped_edit_baton,
                                          base_revision, pool,
                                          &db->wrapped_node_baton));
    eb->called_open_root = TRUE;
  }
  db->edit_baton = edit_baton;
  *root_baton = db;
#else /* !VBOX */
  node_baton_t *dir_baton = apr_palloc(pool, sizeof(*dir_baton));

  SVN_ERR(eb->wrapped_editor->open_root(eb->wrapped_edit_baton,
                                        base_revision, pool,
                                        &dir_baton->wrapped_node_baton));

  eb->called_open_root = TRUE;
  dir_baton->edit_baton = edit_baton;
  *root_baton = dir_baton;
#endif /* !VBOX */

  return SVN_NO_ERROR;
}

static svn_error_t *
delete_entry(const char *path,
             svn_revnum_t base_revision,
             void *parent_baton,
             apr_pool_t *pool)
{
  node_baton_t *pb = parent_baton;
  edit_baton_t *eb = pb->edit_baton;
#ifdef VBOX
  svn_boolean_t prev_process = FALSE;
  svn_boolean_t ignore_everything;
#endif /* VBOX */

#ifdef VBOX
  DX(fprintf(stderr, "delete_entry %s\n", path);)
  /* Apply sync properties here, too. Avoid deleting items which are
   * not in the exported tree, taking transient files into account (can happen
   * e.g. if a directory is renamed and in the same changeset a file is
   * deleted). Very tricky business. */
  ignore_everything = pb->ignore_everything;
  if (!ignore_everything)
  {
    svn_node_kind_t nodekind;
    /* Verify if the entry did actually exist. Note that some files exist
     * only temporarily within a changeset and get deleted. So there's no
     * reliable way for checking their presence. So always delete and hope
     * that subversion optimizes out deletes for files which don't exist. */
    SVN_ERR(svn_ra_check_path(eb->from_session_prop, STRIP_LEADING_SLASH(path),
                              eb->current-1, &nodekind, pool));
    if (nodekind == svn_node_none)
      prev_process = TRUE;
    else
    {
      /* Of course it doesn't make sense to get the properties of the current
       * revision - it is to be deleted, so it doesn't have any properties. */
      SVN_ERR(get_props_sync(eb->from_session_prop, eb->default_process,
                             pb->prev_process_default, pb->prev_process_recursive,
                             path, eb->current-1, &prev_process, NULL, NULL, pool));
    }
    DX(fprintf(stderr, "  %s\n", prev_process ? "EXPORT" : "IGNORE");)
    if (prev_process && !pb->process)
    {
      /* Parent directory is not exported, but this entry is. Warn user,
       * because this can lead to destination repository weirdness. */
      SVN_ERR(svn_cmdline_printf(pool,
                                 _("The parent of %s is not exported, but the file/directory (scheduled for deletion) is. FIX ASAP!\n"), path));
      prev_process = FALSE;
    }
  }
  if (prev_process && !ignore_everything)
  {
    eb->changeset_live = TRUE;
    /* Deliberately ignore error, it's the only safe solution. */
    eb->wrapped_editor->delete_entry(path, base_revision,
                                     pb->wrapped_node_baton, pool);
  }

  return SVN_NO_ERROR;
#else /* !VBOX */
  return eb->wrapped_editor->delete_entry(path, base_revision,
                                          pb->wrapped_node_baton, pool);
#endif
}

static svn_error_t *
add_directory(const char *path,
              void *parent_baton,
              const char *copyfrom_path,
              svn_revnum_t copyfrom_rev,
              apr_pool_t *pool,
              void **child_baton)
{
  node_baton_t *pb = parent_baton;
  edit_baton_t *eb = pb->edit_baton;
#ifdef VBOX
  node_baton_t *b = apr_pcalloc(pool, sizeof(*b));
  svn_revnum_t dst_rev;

  DX(fprintf(stderr, "add_directory %s\n", path);)
  b->ignore_everything_rec = pb->ignore_everything_rec;
  b->ignore_everything = pb->ignore_everything_rec;
  if (!b->ignore_everything)
  {
    /* Of course it doesn't make sense to get the properties of the previous
     * revision - it is to be added, so it didn't have any properties. */
    SVN_ERR(get_props_sync(eb->from_session_prop, eb->default_process,
                           pb->process_default, pb->process_recursive, path,
                           eb->current, &b->process, &b->process_default,
                           &b->process_recursive, pool));
    DX(fprintf(stderr, "  %s\n", b->process ? "EXPORT" : "IGNORE");)
    if (b->process && !pb->process)
    {
      /* Parent directory is not exported, but this directory is. Warn user,
       * because this can lead to destination repository weirdness. */
      SVN_ERR(svn_cmdline_printf(pool,
                                 _("The parent of directory %s is not exported, but the directory is. FIX ASAP!\n"), path));
      b->process = FALSE;
    }
    /* Fake previous process settings, to avoid warnings later on. */
    b->prev_process = b->process;
    b->prev_process_default = b->process_default;
    b->prev_process_recursive = b->process_recursive;
  }
  else
    b->process = FALSE;
  b->edit_baton = eb;
  if (b->process && !b->ignore_everything)
  {
    eb->changeset_live = TRUE;
    if (copyfrom_path)
    {
      dst_rev = lookup_revnum(eb->to_session_prop, copyfrom_rev, pool);
      if (SVN_IS_VALID_REVNUM(dst_rev))
      {
        svn_node_kind_t nodekind;
        /* Verify that the copyfrom source was exported to the destination
         * repository. */
        SVN_ERR(svn_ra_check_path(eb->to_session_prop,
                                  STRIP_LEADING_SLASH(copyfrom_path), dst_rev,
                                  &nodekind, pool));
        if (nodekind == svn_node_none || nodekind != svn_node_dir)
          dst_rev = SVN_INVALID_REVNUM;
        else
          copyfrom_path = apr_psprintf(pool, "%s%s", eb->to_url,
                                       svn_path_uri_encode(copyfrom_path, pool));
      }
    }
    else
      dst_rev = copyfrom_rev;

    if (!SVN_IS_VALID_REVNUM(copyfrom_rev) || SVN_IS_VALID_REVNUM(dst_rev))
    {
      /* Genuinely add a new dir, referring to other revision/name if known. */
      SVN_ERR(eb->wrapped_editor->add_directory(path, pb->wrapped_node_baton,
                                                copyfrom_path,
                                                dst_rev, pool,
                                                &b->wrapped_node_baton));
    }
    else
    {
      if (!SVN_IS_VALID_REVNUM(copyfrom_rev))
        copyfrom_rev = eb->current;
      /* Detect copying from a branch and in that case copy from the
       * destination directory in the revision currently being processed. */
      if (copyfrom_path[0] == '/')
      {
        copyfrom_path = path;
        copyfrom_rev = eb->current;
      }
      /* The dir was renamed, need to copy previous contents because we
       * don't know which revnum to use for destination repository. */
      SVN_ERR(copy_dir_rec(copyfrom_path, copyfrom_rev, path, b,
                           pb->wrapped_node_baton, eb->from_session_prop, pool));
      b->ignore_everything_rec = TRUE;
      b->ignore_everything = TRUE;
    }
  }
  else
  {
    /* In this changeset there may be changes to files/dirs in this ignored
     * directory. Make sure we ignore them all. */
    b->ignore_everything_rec = TRUE;
    b->ignore_everything = TRUE;
  }
#else /* !VBOX */
  node_baton_t *b = apr_palloc(pool, sizeof(*b));

  if (copyfrom_path)
    copyfrom_path = apr_psprintf(pool, "%s%s", eb->to_url,
                                 svn_path_uri_encode(copyfrom_path, pool));

  SVN_ERR(eb->wrapped_editor->add_directory(path, pb->wrapped_node_baton,
                                            copyfrom_path,
                                            copyfrom_rev, pool,
                                            &b->wrapped_node_baton));

  b->edit_baton = eb;
#endif /* !VBOX */
  *child_baton = b;

  return SVN_NO_ERROR;
}

static svn_error_t *
open_directory(const char *path,
               void *parent_baton,
               svn_revnum_t base_revision,
               apr_pool_t *pool,
               void **child_baton)
{
  node_baton_t *pb = parent_baton;
  edit_baton_t *eb = pb->edit_baton;
#ifdef VBOX
  node_baton_t *db = apr_pcalloc(pool, sizeof(*db));
  svn_boolean_t dir_added_this_changeset = FALSE;
  svn_boolean_t dir_present_in_target = FALSE;

  DX(fprintf(stderr, "open_directory %s\n", path);)
  db->ignore_everything_rec = pb->ignore_everything_rec;
  db->ignore_everything = db->ignore_everything_rec;
  if (!db->ignore_everything)
  {
    svn_node_kind_t nodekind;
    /* Verify that the directory was exported from the source
     * repository. Can happen to be not there if the rename and
     * a change to some file in the directory is in one changeset. */
    SVN_ERR(svn_ra_check_path(eb->from_session_prop, STRIP_LEADING_SLASH(path),
                              eb->current-1, &nodekind, pool));
    dir_added_this_changeset = (nodekind != svn_node_dir);
    if (!dir_added_this_changeset)
    {
      svn_revnum_t dst_rev;

      SVN_ERR(get_props_sync(eb->from_session_prop, eb->default_process,
                             pb->prev_process_default, pb->prev_process_recursive,
                             path, eb->current-1, &db->prev_process,
                             &db->prev_process_default, &db->prev_process_recursive,
                             pool));
      dst_rev = lookup_revnum(eb->to_session_prop, eb->current-1, pool);
      if (SVN_IS_VALID_REVNUM(dst_rev))
      {
        SVN_ERR(svn_ra_check_path(eb->to_session_prop, STRIP_LEADING_SLASH(path),
                                  dst_rev, &nodekind, pool));
        dir_present_in_target = (nodekind == svn_node_dir);
      }
    }
    else
    {
      dir_present_in_target = TRUE;
    }
    SVN_ERR(get_props_sync(eb->from_session_prop, eb->default_process,
                           pb->process_default, pb->process_recursive, path,
                           eb->current, &db->process, &db->process_default,
                           &db->process_recursive, pool));
    if (dir_added_this_changeset)
    {
      db->prev_process = db->process;
      db->prev_process_default = db->process_default;
      db->prev_process_recursive = db->process_recursive;
    }
    DX(fprintf(stderr, "  %s (prev %s)\n", db->process ? "EXPORT" : "IGNORE", db->prev_process ? "EXPORT" : "IGNORE");)
    if (db->process && !pb->process)
    {
      /* Parent directory is not exported, but this directory is. Warn user,
       * because this can lead to destination repository weirdness. */
      SVN_ERR(svn_cmdline_printf(pool,
                                 _("The parent of directory %s is not exported, but the directory is. FIX ASAP!\n"), path));
      db->process = FALSE;
      db->ignore_everything_rec = TRUE;
      db->ignore_everything = TRUE;
    }
    if (db->process && db->prev_process && !dir_added_this_changeset && !dir_present_in_target)
    {
      /* Directory is supposed to be there, but actually is not. Warn user,
       * because this can lead to destination repository weirdness. */
      SVN_ERR(svn_cmdline_printf(pool,
                                 _("The directory %s is exported but not present in the target repository. Ignoring it. FIX ASAP!\n"), path));
      db->process = FALSE;
      db->ignore_everything_rec = TRUE;
      db->ignore_everything = TRUE;
    }
  }
  else
    db->process = FALSE;
  db->edit_baton = eb;
  if (!db->ignore_everything)
  {
    if (db->process)
    {
      if (db->prev_process)
        SVN_ERR(eb->wrapped_editor->open_directory(path, pb->wrapped_node_baton,
                                                   base_revision, pool,
                                                   &db->wrapped_node_baton));
      else
      {
        apr_hash_t *dirprops;
        apr_hash_index_t *hi;

        /* Directory appears due to changes to the process settings. */
        eb->changeset_live = TRUE;
        SVN_ERR(eb->wrapped_editor->add_directory(path, pb->wrapped_node_baton,
                                                  NULL, SVN_IGNORED_REVNUM, pool,
                                                  &db->wrapped_node_baton));
        /* Copy over properties from current revision in source repo */
        SVN_ERR(svn_ra_get_dir2(eb->from_session_prop, NULL, NULL, &dirprops,
                                path, eb->current, 0, pool));
        for (hi = apr_hash_first(pool, dirprops); hi; hi = apr_hash_next(hi))
        {
          const void *key;
          void *val;

          apr_hash_this(hi, &key, NULL, &val);
          SVN_ERR(change_dir_prop(db, key, val, pool));
        }
        /* Suppress change_dir_prop for this directory. Done already. */
        db->ignore_everything = TRUE;

        /** @todo copy over files in this directory which were already exported
         * due to inconsistent export settings (e.g. directory is not exported,
         * but file in it is exported). */
      }
    }
    else
    {
      if (db->prev_process && dir_present_in_target)
      {
        /* Directory disappears due to changes to the process settings. */
        eb->changeset_live = TRUE;
        SVN_ERR(eb->wrapped_editor->delete_entry(path, SVN_IGNORED_REVNUM,
                                                 pb->wrapped_node_baton, pool));
      }
      db->ignore_everything_rec = TRUE;
    }
  }
#else /* !VBOX */
  node_baton_t *db = apr_palloc(pool, sizeof(*db));

  SVN_ERR(eb->wrapped_editor->open_directory(path, pb->wrapped_node_baton,
                                             base_revision, pool,
                                             &db->wrapped_node_baton));

  db->edit_baton = eb;
#endif /* !VBOX */
  *child_baton = db;

  return SVN_NO_ERROR;
}

static svn_error_t *
add_file(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_rev,
         apr_pool_t *pool,
         void **file_baton)
{
  node_baton_t *pb = parent_baton;
  edit_baton_t *eb = pb->edit_baton;
#ifdef VBOX
  node_baton_t *fb = apr_pcalloc(pool, sizeof(*fb));
  svn_revnum_t dst_rev;

  DX(fprintf(stderr, "add_file %s\n", path);)
  fb->ignore_everything_rec = pb->ignore_everything_rec;
  fb->ignore_everything = fb->ignore_everything_rec;
  if (!fb->ignore_everything)
  {
    /* Of course it doesn't make sense to get the properties of the previous
     * revision - it is to be added, so it didn't have any properties. */
    SVN_ERR(get_props_sync(eb->from_session_prop, eb->default_process,
                           pb->process_default, pb->process_recursive, path,
                           eb->current, &fb->process, NULL, NULL, pool));
    fb->process_default = FALSE;
    DX(fprintf(stderr, "  %s\n", fb->process ? "EXPORT" : "IGNORE");)
    if (fb->process && !pb->process)
    {
      /* Parent directory is not exported, but this file is. Warn user,
       * because this can lead to destination repository weirdness. */
      SVN_ERR(svn_cmdline_printf(pool,
                                 _("The parent of directory %s is not exported, but the file is. FIX ASAP!\n"), path));
      fb->process = FALSE;
    }
    /* Fake previous process settings, to avoid warnings later on. */
    fb->prev_process = fb->process;
    fb->prev_process_default = fb->process_default;
  }
  else
    fb->process = FALSE;
  fb->edit_baton = eb;
  if (fb->process && !fb->ignore_everything)
  {
    eb->changeset_live = TRUE;
    if (copyfrom_path)
    {
      dst_rev = lookup_revnum(eb->to_session_prop, copyfrom_rev, pool);
      if (SVN_IS_VALID_REVNUM(dst_rev))
      {
        svn_node_kind_t nodekind;
        /* Verify that the copyfrom source was exported to the destination
         * repository. */
        SVN_ERR(svn_ra_check_path(eb->to_session_prop,
                                  STRIP_LEADING_SLASH(copyfrom_path), dst_rev,
                                  &nodekind, pool));
        if (nodekind == svn_node_none || nodekind != svn_node_file)
          dst_rev = SVN_INVALID_REVNUM;
        else
          copyfrom_path = apr_psprintf(pool, "%s%s", eb->to_url,
                                       svn_path_uri_encode(copyfrom_path, pool));
      }
    }
    else
      dst_rev = copyfrom_rev;

    if (!SVN_IS_VALID_REVNUM(copyfrom_rev) || SVN_IS_VALID_REVNUM(dst_rev))
    {
      /* Genuinely add a new file, referring to other revision/name if known. */
      SVN_ERR(eb->wrapped_editor->add_file(path, pb->wrapped_node_baton,
                                           copyfrom_path, dst_rev,
                                           pool, &fb->wrapped_node_baton));
    }
    else
    {
      /* The file was renamed, need to copy previous contents because we
       * don't know which revnum to use for destination repository. */
      SVN_ERR(copy_file(copyfrom_path, copyfrom_rev, path, fb,
                        pb->wrapped_node_baton, eb->from_session_prop, pool));
    }
  }
#else /* !VBOX */
  node_baton_t *fb = apr_palloc(pool, sizeof(*fb));

  if (copyfrom_path)
    copyfrom_path = apr_psprintf(pool, "%s%s", eb->to_url,
                                 svn_path_uri_encode(copyfrom_path, pool));

  SVN_ERR(eb->wrapped_editor->add_file(path, pb->wrapped_node_baton,
                                       copyfrom_path, copyfrom_rev,
                                       pool, &fb->wrapped_node_baton));

  fb->edit_baton = eb;
#endif /* !VBOX */
  *file_baton = fb;

  return SVN_NO_ERROR;
}

static svn_error_t *
open_file(const char *path,
          void *parent_baton,
          svn_revnum_t base_revision,
          apr_pool_t *pool,
          void **file_baton)
{
  node_baton_t *pb = parent_baton;
  edit_baton_t *eb = pb->edit_baton;
#ifdef VBOX
  node_baton_t *fb = apr_pcalloc(pool, sizeof(*fb));
  svn_boolean_t file_added_this_changeset = FALSE;

  DX(fprintf(stderr, "open_file %s\n", path);)
  fb->ignore_everything_rec = pb->ignore_everything_rec;
  fb->ignore_everything = fb->ignore_everything_rec;
  if (!fb->ignore_everything)
  {
    svn_node_kind_t nodekind;
    /* Check whether the file was added in this changeset. If it was added
     * there, the export check for the previous revision would fail. */
    SVN_ERR(svn_ra_check_path(eb->from_session_prop, STRIP_LEADING_SLASH(path),
                              eb->current-1, &nodekind, pool));
    file_added_this_changeset = (nodekind != svn_node_file);
    if (!file_added_this_changeset)
      SVN_ERR(get_props_sync(eb->from_session_prop, eb->default_process,
                             pb->prev_process_default,
                             pb->prev_process_recursive,
                             path, eb->current-1, &fb->prev_process,
                             NULL, NULL, pool));
    SVN_ERR(get_props_sync(eb->from_session_prop, eb->default_process,
                           pb->process_default, pb->process_recursive, path,
                           eb->current, &fb->process, NULL, NULL, pool));
    if (file_added_this_changeset)
      fb->prev_process = FALSE;
    fb->prev_process_default = FALSE;
    fb->process_default = FALSE;
    DX(fprintf(stderr, "  %s (prev %s)\n", fb->process ? "EXPORT" : "IGNORE", fb->prev_process ? "EXPORT" : "IGNORE");)
    if (fb->process && !pb->process)
    {
      /* Parent directory is not exported, but this file is. Warn user,
       * because this can lead to destination repository weirdness. */
      SVN_ERR(svn_cmdline_printf(pool,
                                 _("The parent of directory %s is not exported, but the file is. FIX ASAP!\n"), path));
      fb->process = FALSE;
      fb->ignore_everything = TRUE;
    }
  }
  else
    fb->process = FALSE;
  fb->edit_baton = eb;
  if (!fb->ignore_everything)
  {
    if (fb->process)
    {
      if (!file_added_this_changeset)
      {
        svn_node_kind_t nodekind;
        /* Verify that the previous source was exported to the destination
         * repository. */
        SVN_ERR(svn_ra_check_path(eb->to_session_prop,
                                  STRIP_LEADING_SLASH(path),
                                  SVN_IGNORED_REVNUM, &nodekind, pool));
        if (nodekind == svn_node_none || nodekind != svn_node_file)
          fb->prev_process = FALSE;
      }

      if (fb->prev_process)
        SVN_ERR(eb->wrapped_editor->open_file(path, pb->wrapped_node_baton,
                                              base_revision, pool,
                                              &fb->wrapped_node_baton));
      else
      {
        /* File appears due to changes to the process settings. */
        eb->changeset_live = TRUE;

        SVN_ERR(copy_file(path, eb->current, path, fb, pb->wrapped_node_baton,
                          eb->from_session_prop, pool));
        /* Suppress change_file_prop/apply_textdelta this file. Done already. */
        fb->ignore_everything = TRUE;
      }
    }
    else
    {
      if (!file_added_this_changeset)
      {
        svn_node_kind_t nodekind;
        /* Verify that the previous source was exported to the destination
         * repository. */
        SVN_ERR(svn_ra_check_path(eb->to_session_prop,
                                  STRIP_LEADING_SLASH(path),
                                  SVN_IGNORED_REVNUM, &nodekind, pool));
        if (nodekind == svn_node_none || nodekind != svn_node_file)
          fb->prev_process = FALSE;
      }

      if (fb->prev_process)
      {
        /* File disappears due to changes to the process settings. */
        eb->changeset_live = TRUE;
        SVN_ERR(eb->wrapped_editor->delete_entry(path, SVN_IGNORED_REVNUM,
                                                 pb->wrapped_node_baton, pool));
        fb->ignore_everything = TRUE;
      }
    }
  }
#else /* !VBOX */
  node_baton_t *fb = apr_palloc(pool, sizeof(*fb));

  SVN_ERR(eb->wrapped_editor->open_file(path, pb->wrapped_node_baton,
                                        base_revision, pool,
                                        &fb->wrapped_node_baton));

  fb->edit_baton = eb;
#endif /* !VBOX */
  *file_baton = fb;

  return SVN_NO_ERROR;
}

static svn_error_t *
apply_textdelta(void *file_baton,
                const char *base_checksum,
                apr_pool_t *pool,
                svn_txdelta_window_handler_t *handler,
                void **handler_baton)
{
  node_baton_t *fb = file_baton;
  edit_baton_t *eb = fb->edit_baton;

#ifdef VBOX
  DX(fprintf(stderr, "apply_textdelta\n");)
  DX(fprintf(stderr, "  %s (ignore_everything %d)\n", fb->process ? "EXPORT" : "IGNORE", fb->ignore_everything);)
  if (fb->process && !fb->ignore_everything)
  {
    eb->changeset_live = TRUE;
    return eb->wrapped_editor->apply_textdelta(fb->wrapped_node_baton,
                                               base_checksum, pool,
                                               handler, handler_baton);
  }
  else
  {
    /* Must provide a window handler, there's no way of telling our caller
     * to throw away its data as we're not interested. */
    *handler = svn_delta_noop_window_handler;
    *handler_baton = NULL;
    return SVN_NO_ERROR;
  }
#else /* !VBOX */
  return eb->wrapped_editor->apply_textdelta(fb->wrapped_node_baton,
                                             base_checksum, pool,
                                             handler, handler_baton);
#endif /* VBOX */
}

static svn_error_t *
close_file(void *file_baton,
           const char *text_checksum,
           apr_pool_t *pool)
{
  node_baton_t *fb = file_baton;
  edit_baton_t *eb = fb->edit_baton;
#ifdef VBOX
  DX(fprintf(stderr, "close_file\n");)
  DX(fprintf(stderr, "  %s\n", fb->process ? "EXPORT" : "IGNORE");)
  if (!fb->process)
    return SVN_NO_ERROR;
#endif /* VBOX */
  return eb->wrapped_editor->close_file(fb->wrapped_node_baton,
                                        text_checksum, pool);
}

static svn_error_t *
absent_file(const char *path,
            void *file_baton,
            apr_pool_t *pool)
{
  node_baton_t *fb = file_baton;
  edit_baton_t *eb = fb->edit_baton;
#ifdef VBOX
  DX(fprintf(stderr, "absent_file\n");)
  DX(fprintf(stderr, "  %s\n", fb->process ? "EXPORT" : "IGNORE");)
  if (!fb->process)
    return SVN_NO_ERROR;
#endif /* VBOX */
  return eb->wrapped_editor->absent_file(path, fb->wrapped_node_baton, pool);
}

static svn_error_t *
close_directory(void *dir_baton,
                apr_pool_t *pool)
{
  node_baton_t *db = dir_baton;
  edit_baton_t *eb = db->edit_baton;
#ifdef VBOX
  DX(fprintf(stderr, "close_directory\n");)
  DX(fprintf(stderr, "  %s\n", db->process ? "EXPORT" : "IGNORE");)
  if (!db->process)
    return SVN_NO_ERROR;
#endif /* VBOX */
  return eb->wrapped_editor->close_directory(db->wrapped_node_baton, pool);
}

static svn_error_t *
absent_directory(const char *path,
                 void *dir_baton,
                 apr_pool_t *pool)
{
  node_baton_t *db = dir_baton;
  edit_baton_t *eb = db->edit_baton;
#ifdef VBOX
  DX(fprintf(stderr, "absent_directory\n");)
  DX(fprintf(stderr, "  %s\n", db->process ? "EXPORT" : "IGNORE");)
  if (!db->process)
    return SVN_NO_ERROR;
#endif /* VBOX */
  return eb->wrapped_editor->absent_directory(path, db->wrapped_node_baton,
                                              pool);
}

static svn_error_t *
change_file_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *pool)
{
  node_baton_t *fb = file_baton;
  edit_baton_t *eb = fb->edit_baton;

#ifdef VBOX
  DX(fprintf(stderr, "change_file_prop %s\n", name);)
  DX(fprintf(stderr, "  %s (ignore_everything %d)\n", fb->process ? "EXPORT" : "IGNORE", fb->ignore_everything);)
#endif /* VBOX */
  /* only regular properties can pass over libsvn_ra */
#ifdef VBOX
  if (svn_property_kind2(name) != svn_prop_regular_kind)
    return SVN_NO_ERROR;
  if (!strcmp(name, "cvs2svn:cvs-rev"))
    return SVN_NO_ERROR;
  if (eb->replace_license)
  {
    /* Throw away the normal license property and replace it by the value
     * of svn:sync-license, if present. */
    if (!strcmp(name, SVN_PROP_LICENSE))
      return SVN_NO_ERROR;
    if (!strcmp(name, SVNSYNC_PROP_LICENSE))
      name = SVN_PROP_LICENSE;
  }
  /* Never export any svn:sync-* properties. */
  if (!strncmp(name, SVNSYNC_PROP_PREFIX, sizeof(SVNSYNC_PROP_PREFIX) - 1))
    return SVN_NO_ERROR;
  if (!fb->process || fb->ignore_everything)
    return SVN_NO_ERROR;
  eb->changeset_live = TRUE;
#else /* !VBOX */
  if (svn_property_kind(NULL, name) != svn_prop_regular_kind)
    return SVN_NO_ERROR;
#endif /* !VBOX */

  return eb->wrapped_editor->change_file_prop(fb->wrapped_node_baton,
                                              name, value, pool);
}

static svn_error_t *
change_dir_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *pool)
{
  node_baton_t *db = dir_baton;
  edit_baton_t *eb = db->edit_baton;

#ifdef VBOX
  DX(fprintf(stderr, "change_dir_prop %s\n", name);)
  DX(fprintf(stderr, "  %s (ignore_everything %d)\n", db->process ? "EXPORT" : "IGNORE", db->ignore_everything);)
#endif /* VBOX */
  /* only regular properties can pass over libsvn_ra */
#ifdef VBOX
  if (svn_property_kind2(name) != svn_prop_regular_kind)
    return SVN_NO_ERROR;
  if (!strcmp(name, "cvs2svn:cvs-rev"))
    return SVN_NO_ERROR;
  if (eb->replace_externals)
  {
    /* Throw away the normal externals and replace them by the value of
     * svn:sync-externals, if present. */
    if (!strcmp(name, SVN_PROP_EXTERNALS))
      return SVN_NO_ERROR;
    if (!strcmp(name, SVNSYNC_PROP_EXTERNALS))
      name = SVN_PROP_EXTERNALS;
  }
  /* Never export any svn:sync-* properties. */
  if (!strncmp(name, SVNSYNC_PROP_PREFIX, sizeof(SVNSYNC_PROP_PREFIX) - 1))
    return SVN_NO_ERROR;
  if (!db->process || db->ignore_everything)
    return SVN_NO_ERROR;
  eb->changeset_live = TRUE;
#else /* !VBOX */
  if (svn_property_kind(NULL, name) != svn_prop_regular_kind)
    return SVN_NO_ERROR;
#endif /* !VBOX */

  return eb->wrapped_editor->change_dir_prop(db->wrapped_node_baton,
                                             name, value, pool);
}

static svn_error_t *
close_edit(void *edit_baton,
           apr_pool_t *pool)
{
  edit_baton_t *eb = edit_baton;

#ifdef VBOX
  DX(fprintf(stderr, "close_edit\n");)
  /* Suppress empty commits. No need to record something in the
   * repository if the entire contents of a changeset is to be ignored. */
  if (eb->start_rev && !eb->changeset_live)
  {
    DX(fprintf(stderr, "  discard empty commit\n");)
    SVN_ERR(eb->wrapped_editor->abort_edit(eb->wrapped_edit_baton, pool));
    SVN_ERR(svn_cmdline_printf(pool, _("Skipped revision %ld in source "
                                       "repository, empty commit.\n"),
                               eb->current));
    return SVN_NO_ERROR;
  }
#endif /* VBOX */

  /* If we haven't opened the root yet, that means we're transferring
     an empty revision, probably because we aren't allowed to see the
     contents for some reason.  In any event, we need to open the root
     and close it again, before we can close out the edit, or the
     commit will fail. */

  if (! eb->called_open_root)
    {
      void *baton;
#ifdef VBOX
      SVN_ERR(eb->wrapped_editor->open_root(eb->wrapped_edit_baton,
                                            eb->current, pool,
                                            &baton));
#else /* !VBOX */
      SVN_ERR(eb->wrapped_editor->open_root(eb->wrapped_edit_baton,
                                            eb->base_revision, pool,
                                            &baton));
#endif /* !VBOX */
      SVN_ERR(eb->wrapped_editor->close_directory(baton, pool));
    }

  return eb->wrapped_editor->close_edit(eb->wrapped_edit_baton, pool);
}

/*** Editor factory function ***/

/* Set WRAPPED_EDITOR and WRAPPED_EDIT_BATON to an editor/baton pair
 * that wraps our own commit EDITOR/EDIT_BATON.  BASE_REVISION is the
 * revision on which the driver of this returned editor will be basing
 * the commit.  TO_URL is the URL of the root of the repository into
 * which the commit is being made.
 */
static svn_error_t *
get_sync_editor(const svn_delta_editor_t *wrapped_editor,
                void *wrapped_edit_baton,
                svn_revnum_t base_revision,
#ifdef VBOX
                svn_revnum_t start_rev,
                svn_revnum_t current,
                svn_ra_session_t *prop_session_from,
                svn_ra_session_t *prop_session_to,
                const char *default_process,
                svn_boolean_t replace_externals,
                svn_boolean_t replace_license,
#endif /* VBOX */
                const char *to_url,
                const svn_delta_editor_t **editor,
                void **edit_baton,
                apr_pool_t *pool)
{
  svn_delta_editor_t *tree_editor = svn_delta_default_editor(pool);
  edit_baton_t *eb = apr_palloc(pool, sizeof(*eb));

  tree_editor->set_target_revision = set_target_revision;
  tree_editor->open_root = open_root;
  tree_editor->delete_entry = delete_entry;
  tree_editor->add_directory = add_directory;
  tree_editor->open_directory = open_directory;
  tree_editor->change_dir_prop = change_dir_prop;
  tree_editor->close_directory = close_directory;
  tree_editor->absent_directory = absent_directory;
  tree_editor->add_file = add_file;
  tree_editor->open_file = open_file;
  tree_editor->apply_textdelta = apply_textdelta;
  tree_editor->change_file_prop = change_file_prop;
  tree_editor->close_file = close_file;
  tree_editor->absent_file = absent_file;
  tree_editor->close_edit = close_edit;

  eb->wrapped_editor = wrapped_editor;
  eb->wrapped_edit_baton = wrapped_edit_baton;
  eb->called_open_root = FALSE;
  eb->base_revision = base_revision;
#ifdef VBOX
  eb->changeset_live = FALSE;
  eb->start_rev = start_rev;
  eb->current = current;
  eb->default_process = default_process;
  eb->replace_externals = replace_externals;
  eb->replace_license = replace_license;
  eb->from_session_prop = prop_session_from;
  eb->to_session_prop = prop_session_to;
#endif /* VBOX */
  eb->to_url = to_url;

  *editor = tree_editor;
  *edit_baton = eb;

  return SVN_NO_ERROR;
}



/*** `svnsync sync' ***/

/* Baton for synchronizing the destination repository while locked. */
typedef struct {
  apr_hash_t *config;
  svn_ra_callbacks2_t *callbacks;
  const char *to_url;
  svn_revnum_t committed_rev;
#ifdef VBOX
  svn_revnum_t from_rev;
#endif /* VBOX */
} sync_baton_t;


/* Implements `svn_commit_callback2_t' interface. */
static svn_error_t *
commit_callback(const svn_commit_info_t *commit_info,
                void *baton,
                apr_pool_t *pool)
{
  sync_baton_t *sb = baton;

#ifdef VBOX
  if (sb->from_rev != commit_info->revision)
    SVN_ERR(svn_cmdline_printf(pool, _("Committed revision %ld (%ld in source repository).\n"),
                               commit_info->revision, sb->from_rev));
  else
    SVN_ERR(svn_cmdline_printf(pool, _("Committed revision %ld.\n"),
                               commit_info->revision));
#else /* !VBOX */
  SVN_ERR(svn_cmdline_printf(pool, _("Committed revision %ld.\n"),
                             commit_info->revision));
#endif /* !VBOX */

  sb->committed_rev = commit_info->revision;

  return SVN_NO_ERROR;
}


/* Set *FROM_SESSION to an RA session associated with the source
 * repository of the synchronization, as determined by reading
 * svn:sync- properties from the destination repository (associated
 * with TO_SESSION).  Set LAST_MERGED_REV to the value of the property
 * which records the most recently synchronized revision.
*** VBOX
 * Set START_REV_STR to the property which records the starting revision.
*** VBOX
 *
 * CALLBACKS is a vtable of RA callbacks to provide when creating
 * *FROM_SESSION.  CONFIG is a configuration hash.
 */
static svn_error_t *
open_source_session(svn_ra_session_t **from_session,
                    svn_string_t **last_merged_rev,
#ifdef VBOX
                    svn_revnum_t *start_rev,
#endif /* VBOX */
                    svn_ra_session_t *to_session,
                    svn_ra_callbacks2_t *callbacks,
                    apr_hash_t *config,
                    void *baton,
                    apr_pool_t *pool)
{
#ifdef VBOX
  svn_string_t *start_rev_str;
#endif /* VBOX */
  svn_string_t *from_url, *from_uuid;
  const char *uuid;

  SVN_ERR(svn_ra_rev_prop(to_session, 0, SVNSYNC_PROP_FROM_URL,
                          &from_url, pool));
  SVN_ERR(svn_ra_rev_prop(to_session, 0, SVNSYNC_PROP_FROM_UUID,
                          &from_uuid, pool));
  SVN_ERR(svn_ra_rev_prop(to_session, 0, SVNSYNC_PROP_LAST_MERGED_REV,
                          last_merged_rev, pool));
#ifdef VBOX
  SVN_ERR(svn_ra_rev_prop(to_session, 0, SVNSYNC_PROP_START_REV,
                          &start_rev_str, pool));
#endif /* VBOX */

#ifdef VBOX
  if (! from_url || ! from_uuid || ! *last_merged_rev || ! start_rev_str)
#else /* !VBOX */
  if (! from_url || ! from_uuid || ! *last_merged_rev)
#endif /* !VBOX */
    return svn_error_create
      (APR_EINVAL, NULL, _("Destination repository has not been initialized"));

#ifdef VBOX
  *start_rev = SVN_STR_TO_REV(start_rev_str->data);
#endif /* VBOX */

#ifdef VBOX
  SVN_ERR(svn_ra_open4(from_session, NULL, from_url->data, NULL, callbacks, baton,
                       config, pool));
#else /* !VBOX */
  SVN_ERR(svn_ra_open2(from_session, from_url->data, callbacks, baton,
                       config, pool));
#endif /* !VBOX */

  SVN_ERR(check_if_session_is_at_repos_root(*from_session, from_url->data,
                                            pool));

  /* Ok, now sanity check the UUID of the source repository, it
     wouldn't be a good thing to sync from a different repository. */

#ifdef VBOX
  SVN_ERR(svn_ra_get_uuid2(*from_session, &uuid, pool));
#else /* !VBOX */
  SVN_ERR(svn_ra_get_uuid(*from_session, &uuid, pool));
#endif /* !VBOX */

  if (strcmp(uuid, from_uuid->data) != 0)
    return svn_error_createf(APR_EINVAL, NULL,
                             _("UUID of source repository (%s) does not "
                               "match expected UUID (%s)"),
                             uuid, from_uuid->data);

  return SVN_NO_ERROR;
}


/* Synchronize the repository associated with RA session TO_SESSION,
 * using information found in baton B, while the repository is
 * locked.  Implements `with_locked_func_t' interface.
 */
static svn_error_t *
do_synchronize(svn_ra_session_t *to_session, void *b, apr_pool_t *pool)
{
  svn_string_t *last_merged_rev;
  svn_revnum_t from_latest, current;
  svn_ra_session_t *from_session;
  sync_baton_t *baton = b;
  apr_pool_t *subpool;
  svn_string_t *currently_copying;
  svn_revnum_t to_latest, copying, last_merged;
#ifdef VBOX
  svn_string_t *censor_author_str;
  svn_boolean_t censor_author;
  svn_revnum_t start_rev;
  svn_string_t *from_url;
  svn_string_t *default_process;
  svn_string_t *replace_externals_str;
  svn_boolean_t replace_externals;
  svn_string_t *replace_license_str;
  svn_boolean_t replace_license;
  svn_string_t *ignoreprop;
  svn_ra_session_t *from_session_prop;
  svn_ra_session_t *to_session_prop;
#endif /* VBOX */

#ifdef VBOX
  SVN_ERR(open_source_session(&from_session, &last_merged_rev, &start_rev,
                              to_session, baton->callbacks, baton->config,
                              baton, pool));
  SVN_ERR(svn_ra_rev_prop(to_session, 0, SVNSYNC_PROP_FROM_URL,
                          &from_url, pool));
  SVN_ERR(svn_ra_rev_prop(to_session, 0, SVNSYNC_PROP_DEFAULT_PROCESS,
                          &default_process, pool));
  if (!default_process)
    default_process = svn_string_create("export", pool);
  SVN_ERR(svn_ra_rev_prop(to_session, 0, SVNSYNC_PROP_CENSOR_AUTHOR,
                          &censor_author_str, pool));
  censor_author = !!censor_author_str;
  SVN_ERR(svn_ra_rev_prop(to_session, 0, SVNSYNC_PROP_REPLACE_EXTERNALS,
                          &replace_externals_str, pool));
  replace_externals = !!replace_externals_str;
  SVN_ERR(svn_ra_rev_prop(to_session, 0, SVNSYNC_PROP_REPLACE_LICENSE,
                          &replace_license_str, pool));
  replace_license = !!replace_license_str;
  SVN_ERR(svn_ra_open4(&from_session_prop, NULL, from_url->data, NULL,
                       baton->callbacks, baton, baton->config, pool));
  SVN_ERR(svn_ra_open4(&to_session_prop, NULL, baton->to_url, NULL,
                       baton->callbacks, baton, baton->config, pool));
#else /* !VBOX */
  SVN_ERR(open_source_session(&from_session, &last_merged_rev, to_session,
                              baton->callbacks, baton->config, baton, pool));
#endif /* !VBOX */

  /* Check to see if we have revprops that still need to be copied for
     a prior revision we didn't finish copying.  But first, check for
     state sanity.  Remember, mirroring is not an atomic action,
     because revision properties are copied separately from the
     revision's contents.

     So, any time that currently-copying is not set, then
     last-merged-rev should be the HEAD revision of the destination
     repository.  That is, if we didn't fall over in the middle of a
     previous synchronization, then our destination repository should
     have exactly as many revisions in it as we've synchronized.

     Alternately, if currently-copying *is* set, it must
     be either last-merged-rev or last-merged-rev + 1, and the HEAD
     revision must be equal to either last-merged-rev or
     currently-copying. If this is not the case, somebody has meddled
     with the destination without using svnsync.
  */

  SVN_ERR(svn_ra_rev_prop(to_session, 0, SVNSYNC_PROP_CURRENTLY_COPYING,
                          &currently_copying, pool));

#ifndef VBOX
  SVN_ERR(svn_ra_get_latest_revnum(to_session, &to_latest, pool));
#endif /* !VBOX */

  last_merged = SVN_STR_TO_REV(last_merged_rev->data);

#ifdef VBOX
  if (start_rev)
  {
    /* Fake the destination repository revnum to be what the complete sync
     * code expects.  TODO: this probably breaks continuing after an abort.*/
    to_latest = last_merged;
  }
  else
    SVN_ERR(svn_ra_get_latest_revnum(to_session, &to_latest, pool));
#endif /* VBOX */

  if (currently_copying)
    {
      copying = SVN_STR_TO_REV(currently_copying->data);

      if ((copying < last_merged)
          || (copying > (last_merged + 1))
          || ((to_latest != last_merged) && (to_latest != copying)))
        {
          return svn_error_createf
            (APR_EINVAL, NULL,
             _("Revision being currently copied (%ld), last merged revision "
               "(%ld), and destination HEAD (%ld) are inconsistent; have you "
               "committed to the destination without using svnsync?"),
             copying, last_merged, to_latest);
        }
      else if (copying == to_latest)
        {
          if (copying > last_merged)
            {
#ifdef VBOX
/** @todo fix use of from/to revision numbers. */
              SVN_ERR(copy_revprops(from_session, to_session,
                                    to_latest, to_latest, censor_author, TRUE, pool));
#else /* !VBOX */
              SVN_ERR(copy_revprops(from_session, to_session,
                                    to_latest, TRUE, pool));
#endif /* !VBOX */
              last_merged = copying;
              last_merged_rev = svn_string_create
                (apr_psprintf(pool, "%ld", last_merged), pool);
            }

          /* Now update last merged rev and drop currently changing.
             Note that the order here is significant, if we do them
             in the wrong order there are race conditions where we
             end up not being able to tell if there have been bogus
             (i.e. non-svnsync) commits to the dest repository. */

#ifdef VBOX
          SVN_ERR(svn_ra_change_rev_prop2(to_session, 0,
                                          SVNSYNC_PROP_LAST_MERGED_REV, NULL,
                                          last_merged_rev, pool));
          SVN_ERR(svn_ra_change_rev_prop2(to_session, 0,
                                          SVNSYNC_PROP_CURRENTLY_COPYING, NULL,
                                          NULL, pool));
#else /* !VBOX */
          SVN_ERR(svn_ra_change_rev_prop(to_session, 0,
                                         SVNSYNC_PROP_LAST_MERGED_REV,
                                         last_merged_rev, pool));
          SVN_ERR(svn_ra_change_rev_prop(to_session, 0,
                                         SVNSYNC_PROP_CURRENTLY_COPYING,
                                         NULL, pool));
#endif /* !VBOX */
        }
      /* If copying > to_latest, then we just fall through to
         attempting to copy the revision again. */
    }
  else
    {
      if (to_latest != last_merged)
        {
          return svn_error_createf
            (APR_EINVAL, NULL,
             _("Destination HEAD (%ld) is not the last merged revision (%ld); "
               "have you committed to the destination without using svnsync?"),
             to_latest, last_merged);
        }
    }

  /* Now check to see if there are any revisions to copy. */

  SVN_ERR(svn_ra_get_latest_revnum(from_session, &from_latest, pool));

  if (from_latest < atol(last_merged_rev->data))
    return SVN_NO_ERROR;

  subpool = svn_pool_create(pool);

  /* Ok, so there are new revisions, iterate over them copying them
     into the destination repository. */

  for (current = atol(last_merged_rev->data) + 1;
       current <= from_latest;
       ++current)
    {
      const svn_delta_editor_t *commit_editor;
      const svn_delta_editor_t *cancel_editor;
      const svn_delta_editor_t *sync_editor;
      void *commit_baton;
      void *cancel_baton;
      void *sync_baton;
#ifdef VBOX
      apr_hash_t *logrevprop;
#endif /* VBOX */

      svn_pool_clear(subpool);

      /* We set this property so that if we error out for some reason
         we can later determine where we were in the process of
         merging a revision.  If we had committed the change, but we
         hadn't finished copying the revprops we need to know that, so
         we can go back and finish the job before we move on.

         NOTE: We have to set this before we start the commit editor,
         because ra_svn doesn't let you change rev props during a
         commit. */
#ifdef VBOX
      SVN_ERR(svn_ra_change_rev_prop2(to_session, 0,
                                      SVNSYNC_PROP_CURRENTLY_COPYING, NULL,
                                      svn_string_createf(subpool, "%ld",
                                                         current),
                                      subpool));
#else /* !VBOX */
      SVN_ERR(svn_ra_change_rev_prop(to_session, 0,
                                     SVNSYNC_PROP_CURRENTLY_COPYING,
                                     svn_string_createf(subpool, "%ld",
                                                        current),
                                     subpool));
#endif /* !VBOX */

      /* The actual copy is just a replay hooked up to a commit. */

#ifdef VBOX
      logrevprop = apr_hash_make(pool);
      apr_hash_set(logrevprop, SVN_PROP_REVISION_LOG, APR_HASH_KEY_STRING,
                   svn_string_create("", pool));
      SVN_ERR(svn_ra_get_commit_editor3(to_session, &commit_editor,
                                        &commit_baton,
                                        logrevprop,
                                        commit_callback, baton,
                                        NULL, FALSE, subpool));
#else /* !VBOX */
      SVN_ERR(svn_ra_get_commit_editor2(to_session, &commit_editor,
                                        &commit_baton,
                                        "", /* empty log */
                                        commit_callback, baton,
                                        NULL, FALSE, subpool));
#endif /* !VBOX */

      /* There's one catch though, the diff shows us props we can't
         send over the RA interface, so we need an editor that's smart
         enough to filter those out for us.  */

#ifdef VBOX
      baton->from_rev = current;
      baton->committed_rev = SVN_INVALID_REVNUM;
      SVN_ERR(get_sync_editor(commit_editor, commit_baton, current - 1,
                              start_rev, current, from_session_prop,
                              to_session_prop, default_process->data,
                              replace_externals, replace_license,
                              baton->to_url, &sync_editor, &sync_baton,
                              subpool));
#else /* !VBOX */
      SVN_ERR(get_sync_editor(commit_editor, commit_baton, current - 1,
                              baton->to_url, &sync_editor, &sync_baton,
                              subpool));
#endif /* !VBOX */

      SVN_ERR(svn_delta_get_cancellation_editor(check_cancel, NULL,
                                                sync_editor, sync_baton,
                                                &cancel_editor,
                                                &cancel_baton,
                                                subpool));

#ifdef VBOX
      /* If svn:sync-ignore-changeset revprop exists in changeset, skip it. */
      SVN_ERR(svn_ra_rev_prop(from_session, current,
                              SVNSYNC_PROP_IGNORE_CHANGESET,
                              &ignoreprop, subpool));
      if (!ignoreprop)
        SVN_ERR(svn_ra_replay(from_session, current, start_rev, TRUE,
                              cancel_editor, cancel_baton, subpool));
#else /* !VBOX */
      SVN_ERR(svn_ra_replay(from_session, current, 0, TRUE,
                            cancel_editor, cancel_baton, subpool));
#endif /* !VBOX */

      SVN_ERR(cancel_editor->close_edit(cancel_baton, subpool));

#ifdef VBOX
      if (!start_rev)
      {
        /* Sanity check that we actually committed the revision we meant to. */
        if (baton->committed_rev != current)
          return svn_error_createf
                   (APR_EINVAL, NULL,
                    _("Commit created rev %ld but should have created %ld"),
                    baton->committed_rev, current);
      }
#else /* !VBOX */
      /* Sanity check that we actually committed the revision we meant to. */
      if (baton->committed_rev != current)
        return svn_error_createf
                 (APR_EINVAL, NULL,
                  _("Commit created rev %ld but should have created %ld"),
                  baton->committed_rev, current);
#endif /* !VBOX */

      /* Ok, we're done with the data, now we just need to do the
         revprops and we're all set. */

#ifdef VBOX
      if (SVN_IS_VALID_REVNUM(baton->committed_rev))
      {
        SVN_ERR(copy_revprops(from_session, to_session, current,
                              baton->committed_rev, censor_author, TRUE, subpool));

        /* Add a revision cross-reference revprop visible to the public. */
        SVN_ERR(svn_ra_change_rev_prop2(to_session, baton->committed_rev,
                                        SVNSYNC_PROP_XREF_SRC_REPO_REV, NULL,
                                        svn_string_create(apr_psprintf(subpool,
                                                                       "%ld",
                                                                       current),
                                                          subpool),
                                        subpool));

        /* Add a revision cross-reference revprop for sync purposes. */
        SVN_ERR(svn_ra_change_rev_prop2(to_session, 0,
                                        apr_psprintf(subpool,
                                                     SVNSYNC_PROP_REV__FMT,
                                                     current), NULL,
                                        svn_string_create(apr_psprintf(subpool,
                                                                       "%ld",
                                                                       baton->committed_rev),
                                                          subpool),
                                        subpool));
      }
      else
      {
        /* Add a revision cross-reference revprop for an empty commit,
         * referring to the previous commit (this avoids unnecessary copy_file
         * operation just because a source file was not modified when it
         * appears in the destination repository. */
        SVN_ERR(svn_ra_get_latest_revnum(to_session, &to_latest, subpool));
        SVN_ERR(svn_ra_change_rev_prop2(to_session, 0,
                                        apr_psprintf(subpool,
                                                     SVNSYNC_PROP_REV__FMT,
                                                     current), NULL,
                                        svn_string_create(apr_psprintf(subpool,
                                                                       "%ld",
                                                                       to_latest),
                                                          subpool),
                                        subpool));
      }
#else /* !VBOX */
      SVN_ERR(copy_revprops(from_session, to_session, current, TRUE, subpool));
#endif /* !VBOX */

      /* Ok, we're done, bring the last-merged-rev property up to date. */

#ifdef VBOX
      SVN_ERR(svn_ra_change_rev_prop2
              (to_session,
               0,
               SVNSYNC_PROP_LAST_MERGED_REV, NULL,
               svn_string_create(apr_psprintf(subpool, "%ld", current),
                                 subpool),
               subpool));
#else /* !VBOX */
      SVN_ERR(svn_ra_change_rev_prop
              (to_session,
               0,
               SVNSYNC_PROP_LAST_MERGED_REV,
               svn_string_create(apr_psprintf(subpool, "%ld", current),
                                 subpool),
               subpool));
#endif /* !VBOX */

      /* And finally drop the currently copying prop, since we're done
         with this revision. */

#ifdef VBOX
      SVN_ERR(svn_ra_change_rev_prop2(to_session, 0,
                                      SVNSYNC_PROP_CURRENTLY_COPYING, NULL,
                                      NULL, subpool));
#else /* !VBOX */
      SVN_ERR(svn_ra_change_rev_prop(to_session, 0,
                                     SVNSYNC_PROP_CURRENTLY_COPYING,
                                     NULL, subpool));
#endif /* !VBOX */
    }

  return SVN_NO_ERROR;
}


/* SUBCOMMAND: sync */
static svn_error_t *
synchronize_cmd(apr_getopt_t *os, void *b, apr_pool_t *pool)
{
  svn_ra_callbacks2_t callbacks = { 0 };
  svn_ra_session_t *to_session;
  opt_baton_t *opt_baton = b;
  apr_array_header_t *args;
  sync_baton_t baton;
  const char *to_url;

  SVN_ERR(svn_opt_parse_num_args(&args, os, 1, pool));

  to_url = svn_uri_canonicalize(APR_ARRAY_IDX(args, 0, const char *), pool);

  if (! svn_path_is_url(to_url))
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Path '%s' is not a URL"), to_url);

  callbacks.open_tmp_file = open_tmp_file;
  callbacks.auth_baton = opt_baton->auth_baton;

  baton.callbacks = &callbacks;
  baton.config = opt_baton->config;
  baton.to_url = to_url;

#ifdef VBOX
  SVN_ERR(svn_ra_open4(&to_session, NULL, to_url, NULL,
                       baton.callbacks, &baton, baton.config, pool));
#else /* !VBOX */
  SVN_ERR(svn_ra_open2(&to_session,
                       to_url,
                       baton.callbacks,
                       &baton,
                       baton.config,
                       pool));
#endif /* !VBOX */

  SVN_ERR(check_if_session_is_at_repos_root(to_session, to_url, pool));

  SVN_ERR(with_locked(to_session, do_synchronize, &baton, pool));

  return SVN_NO_ERROR;
}



/*** `svnsync copy-revprops' ***/


/* Baton for copying revision properties to the destination repository
 * while locked.
 */
typedef struct {
  apr_hash_t *config;
  svn_ra_callbacks2_t *callbacks;
  const char *to_url;
  svn_revnum_t rev;
} copy_revprops_baton_t;


/* Copy revision properties to the repository associated with RA
 * session TO_SESSION, using information found in baton B, while the
 * repository is locked.  Implements `with_locked_func_t' interface.
 */
static svn_error_t *
do_copy_revprops(svn_ra_session_t *to_session, void *b, apr_pool_t *pool)
{
  copy_revprops_baton_t *baton = b;
  svn_ra_session_t *from_session;
  svn_string_t *last_merged_rev;
#ifdef VBOX
  svn_revnum_t start_rev;
  svn_string_t *censor_author_str;
  svn_boolean_t censor_author;
#endif /* VBOX */

#ifdef VBOX
  SVN_ERR(open_source_session(&from_session, &last_merged_rev, &start_rev,
                              to_session, baton->callbacks, baton->config,
                              baton, pool));
  if (start_rev)
    return svn_error_create
      (APR_EINVAL, NULL, _("Cannot copy revprops for repositories using "
                           "the start-rev feature (unimplemented)"));

  SVN_ERR(svn_ra_rev_prop(to_session, 0, SVNSYNC_PROP_CENSOR_AUTHOR,
                          &censor_author_str, pool));
  censor_author = !!censor_author_str;
#else /* !VBOX */
  SVN_ERR(open_source_session(&from_session, &last_merged_rev, to_session,
                              baton->callbacks, baton->config, baton, pool));
#endif /* !VBOX */

  if (baton->rev > SVN_STR_TO_REV(last_merged_rev->data))
    return svn_error_create
      (APR_EINVAL, NULL, _("Cannot copy revprops for a revision that has not "
                           "been synchronized yet"));

#ifdef VBOX
  SVN_ERR(copy_revprops(from_session, to_session, baton->rev, baton->rev, censor_author, FALSE, pool));
#else /* !VBOX */
  SVN_ERR(copy_revprops(from_session, to_session, baton->rev, FALSE, pool));
#endif /* !VBOX */

  return SVN_NO_ERROR;
}


/* SUBCOMMAND: copy-revprops */
static svn_error_t *
copy_revprops_cmd(apr_getopt_t *os, void *b, apr_pool_t *pool)
{
  svn_ra_callbacks2_t callbacks = { 0 };
  svn_ra_session_t *to_session;
  opt_baton_t *opt_baton = b;
  apr_array_header_t *args;
  copy_revprops_baton_t baton;
  const char *to_url;
  svn_revnum_t revision = SVN_INVALID_REVNUM;
  char *digits_end = NULL;

  SVN_ERR(svn_opt_parse_num_args(&args, os, 2, pool));

  to_url = svn_uri_canonicalize(APR_ARRAY_IDX(args, 0, const char *), pool);
  revision = strtol(APR_ARRAY_IDX(args, 1, const char *), &digits_end, 10);

  if (! svn_path_is_url(to_url))
    return svn_error_createf(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                             _("Path '%s' is not a URL"), to_url);
  if ((! SVN_IS_VALID_REVNUM(revision)) || (! digits_end) || *digits_end)
    return svn_error_create(SVN_ERR_CL_ARG_PARSING_ERROR, NULL,
                            _("Invalid revision number"));

  callbacks.open_tmp_file = open_tmp_file;
  callbacks.auth_baton = opt_baton->auth_baton;

  baton.callbacks = &callbacks;
  baton.config = opt_baton->config;
  baton.to_url = to_url;
  baton.rev = revision;

#ifdef VBOX
  SVN_ERR(svn_ra_open4(&to_session, NULL, to_url, NULL,
                       baton.callbacks, &baton, baton.config, pool));
#else /* !VBOX */
  SVN_ERR(svn_ra_open2(&to_session,
                       to_url,
                       baton.callbacks,
                       &baton,
                       baton.config,
                       pool));
#endif /* !VBOX */

  SVN_ERR(check_if_session_is_at_repos_root(to_session, to_url, pool));

  SVN_ERR(with_locked(to_session, do_copy_revprops, &baton, pool));

  return SVN_NO_ERROR;
}



/*** `svnsync help' ***/


/* SUBCOMMAND: help */
static svn_error_t *
help_cmd(apr_getopt_t *os, void *baton, apr_pool_t *pool)
{
  opt_baton_t *opt_baton = baton;

  const char *header =
    _("general usage: svnsync SUBCOMMAND DEST_URL  [ARGS & OPTIONS ...]\n"
      "Type 'svnsync help <subcommand>' for help on a specific subcommand.\n"
      "Type 'svnsync --version' to see the program version and RA modules.\n"
      "\n"
      "Available subcommands:\n");

  const char *ra_desc_start
    = _("The following repository access (RA) modules are available:\n\n");

  svn_stringbuf_t *version_footer = svn_stringbuf_create(ra_desc_start,
                                                         pool);

  SVN_ERR(svn_ra_print_modules(version_footer, pool));

#ifdef VBOX
  SVN_ERR(svn_opt_print_help4(os, "svnsync",
                              opt_baton ? opt_baton->version : FALSE,
                              FALSE, FALSE, version_footer->data, header,
                              svnsync_cmd_table, svnsync_options, NULL,
                              NULL, pool));
#else /* !VBOX */
  SVN_ERR(svn_opt_print_help(os, "svnsync",
                             opt_baton ? opt_baton->version : FALSE,
                             FALSE, version_footer->data, header,
                             svnsync_cmd_table, svnsync_options, NULL,
                             pool));
#endif /* !VBOX */

  return SVN_NO_ERROR;
}



/*** Main ***/

int
main(int argc, const char *argv[])
{
#ifdef VBOX
  const svn_opt_subcommand_desc2_t *subcommand = NULL;
#else /* !VBOX */
  const svn_opt_subcommand_desc_t *subcommand = NULL;
#endif /* !VBOX */
  apr_array_header_t *received_opts;
  opt_baton_t opt_baton;
  svn_config_t *config;
  apr_status_t apr_err;
  apr_getopt_t *os;
  apr_pool_t *pool;
  svn_error_t *err;
  int opt_id, i;

  if (svn_cmdline_init("svnsync", stderr) != EXIT_SUCCESS)
    {
      return EXIT_FAILURE;
    }

  err = check_lib_versions();
  if (err)
    {
      svn_handle_error2(err, stderr, FALSE, "svnsync: ");
      return EXIT_FAILURE;
    }

  pool = svn_pool_create(NULL);

  err = svn_ra_initialize(pool);
  if (err)
    {
      svn_handle_error2(err, stderr, FALSE, "svnsync: ");
      return EXIT_FAILURE;
    }

  memset(&opt_baton, 0, sizeof(opt_baton));

  received_opts = apr_array_make(pool, SVN_OPT_MAX_OPTIONS, sizeof(int));

  if (argc <= 1)
    {
      help_cmd(NULL, NULL, pool);
      svn_pool_destroy(pool);
      return EXIT_FAILURE;
    }

  {
    apr_status_t apr_err;
    apr_err = apr_getopt_init(&os, pool, argc, argv);
    if (apr_err)
    {
      err = svn_error_wrap_apr(apr_err, "Error initializing command line parsing");
      return svn_cmdline_handle_exit_error(err, pool, "svnsync: ");
    }
  }

  os->interleave = 1;

  for (;;)
    {
      const char *opt_arg;

      apr_err = apr_getopt_long(os, svnsync_options, &opt_id, &opt_arg);
      if (APR_STATUS_IS_EOF(apr_err))
        break;
      else if (apr_err)
        {
          help_cmd(NULL, NULL, pool);
          svn_pool_destroy(pool);
          return EXIT_FAILURE;
        }

      APR_ARRAY_PUSH(received_opts, int) = opt_id;

      switch (opt_id)
        {
          case svnsync_opt_non_interactive:
            opt_baton.non_interactive = TRUE;
            break;

          case svnsync_opt_no_auth_cache:
            opt_baton.no_auth_cache = TRUE;
            break;

          case svnsync_opt_auth_username:
            opt_baton.auth_username = opt_arg;
            break;

          case svnsync_opt_auth_password:
            opt_baton.auth_password = opt_arg;
            break;

          case svnsync_opt_config_dir:
            opt_baton.config_dir = opt_arg;
            break;

#ifdef VBOX
          case svnsync_opt_censor_author:
            opt_baton.censor_author = TRUE;
            break;

          case svnsync_opt_start_rev:
            opt_baton.start_rev = SVN_STR_TO_REV(opt_arg);
            break;

          case svnsync_opt_default_process:
            opt_baton.default_process = opt_arg;
            break;

          case svnsync_opt_replace_externals:
            opt_baton.replace_externals = TRUE;
            break;

          case svnsync_opt_replace_license:
            opt_baton.replace_license = TRUE;
            break;
#endif /* VBOX */

          case svnsync_opt_version:
            opt_baton.version = TRUE;
            break;

          case '?':
          case 'h':
            opt_baton.help = TRUE;
            break;

          default:
            {
              help_cmd(NULL, NULL, pool);
              svn_pool_destroy(pool);
              return EXIT_FAILURE;
            }
        }
    }

  if (opt_baton.help)
#ifdef VBOX
    subcommand = svn_opt_get_canonical_subcommand2(svnsync_cmd_table, "help");
#else /* !VBOX */
    subcommand = svn_opt_get_canonical_subcommand(svnsync_cmd_table, "help");
#endif /* !VBOX */

  if (subcommand == NULL)
    {
      if (os->ind >= os->argc)
        {
          if (opt_baton.version)
            {
              /* Use the "help" subcommand to handle the "--version" option. */
#ifdef VBOX
              static const svn_opt_subcommand_desc2_t pseudo_cmd =
#else /* !VBOX */
              static const svn_opt_subcommand_desc_t pseudo_cmd =
#endif /* !VBOX */
                { "--version", help_cmd, {0}, "",
                  {svnsync_opt_version,  /* must accept its own option */
                  } };

              subcommand = &pseudo_cmd;
            }
          else
            {
              help_cmd(NULL, NULL, pool);
              svn_pool_destroy(pool);
              return EXIT_FAILURE;
            }
        }
      else
        {
          const char *first_arg = os->argv[os->ind++];
#ifdef VBOX
          subcommand = svn_opt_get_canonical_subcommand2(svnsync_cmd_table,
                                                         first_arg);
#else /* !VBOX */
          subcommand = svn_opt_get_canonical_subcommand(svnsync_cmd_table,
                                                        first_arg);
#endif /* !VBOX */
          if (subcommand == NULL)
            {
              help_cmd(NULL, NULL, pool);
              svn_pool_destroy(pool);
              return EXIT_FAILURE;
            }
        }
    }

  for (i = 0; i < received_opts->nelts; ++i)
    {
      opt_id = APR_ARRAY_IDX(received_opts, i, int);

      if (opt_id == 'h' || opt_id == '?')
        continue;

#ifdef VBOX
      if (! svn_opt_subcommand_takes_option3(subcommand, opt_id, NULL))
#else /* !VBOX */
      if (! svn_opt_subcommand_takes_option(subcommand, opt_id))
#endif /* !VBOX */
        {
          const char *optstr;
#ifdef VBOX
          const apr_getopt_option_t *badopt =
            svn_opt_get_option_from_code2(opt_id, svnsync_options, subcommand,
                                          pool);
#else /* !VBOX */
          const apr_getopt_option_t *badopt =
            svn_opt_get_option_from_code(opt_id, svnsync_options);
#endif /* !VBOX */
          svn_opt_format_option(&optstr, badopt, FALSE, pool);
          if (subcommand->name[0] == '-')
            help_cmd(NULL, NULL, pool);
          else
            svn_error_clear
              (svn_cmdline_fprintf
               (stderr, pool, _("subcommand '%s' doesn't accept option '%s'\n"
                                "Type 'svnsync help %s' for usage.\n"),
                subcommand->name, optstr, subcommand->name));
          svn_pool_destroy(pool);
          return EXIT_FAILURE;
        }
    }

  err = svn_config_get_config(&opt_baton.config, NULL, pool);
  if (err)
    return svn_cmdline_handle_exit_error(err, pool, "svnsync: ");

  config = apr_hash_get(opt_baton.config, SVN_CONFIG_CATEGORY_CONFIG,
                        APR_HASH_KEY_STRING);

  apr_signal(SIGINT, signal_handler);

#ifdef SIGBREAK
  /* SIGBREAK is a Win32 specific signal generated by ctrl-break. */
  apr_signal(SIGBREAK, signal_handler);
#endif

#ifdef SIGHUP
  apr_signal(SIGHUP, signal_handler);
#endif

#ifdef SIGTERM
  apr_signal(SIGTERM, signal_handler);
#endif

#ifdef SIGPIPE
  /* Disable SIGPIPE generation for the platforms that have it. */
  apr_signal(SIGPIPE, SIG_IGN);
#endif

#ifdef SIGXFSZ
  /* Disable SIGXFSZ generation for the platforms that have it,
     otherwise working with large files when compiled against an APR
     that doesn't have large file support will crash the program,
     which is uncool. */
  apr_signal(SIGXFSZ, SIG_IGN);
#endif

#ifdef VBOX
  err = svn_cmdline_create_auth_baton(&opt_baton.auth_baton,
                                      opt_baton.non_interactive,
                                      opt_baton.auth_username,
                                      opt_baton.auth_password,
                                      opt_baton.config_dir,
                                      opt_baton.no_auth_cache,
                                      1,
                                      config,
                                      check_cancel, NULL,
                                      pool);
  if (!err)
    err = svn_cmdline_create_auth_baton(&opt_baton.auth_baton,
                                        opt_baton.non_interactive,
                                        opt_baton.auth_username,
                                        opt_baton.auth_password,
                                        opt_baton.config_dir,
                                        opt_baton.no_auth_cache,
                                        1,
                                        config,
                                        check_cancel, NULL,
                                        pool);
#else /* !VBOX */
  err = svn_cmdline_setup_auth_baton(&opt_baton.auth_baton,
                                     opt_baton.non_interactive,
                                     opt_baton.auth_username,
                                     opt_baton.auth_password,
                                     opt_baton.config_dir,
                                     opt_baton.no_auth_cache,
                                     config,
                                     check_cancel, NULL,
                                     pool);
#endif /* !VBOX */

  err = (*subcommand->cmd_func)(os, &opt_baton, pool);
  if (err)
    {
      /* For argument-related problems, suggest using the 'help'
         subcommand. */
      if (err->apr_err == SVN_ERR_CL_INSUFFICIENT_ARGS
          || err->apr_err == SVN_ERR_CL_ARG_PARSING_ERROR)
        {
          err = svn_error_quick_wrap(err,
                                     _("Try 'svnsync help' for more info"));
        }
      svn_handle_error2(err, stderr, FALSE, "svnsync: ");
      svn_error_clear(err);

      return EXIT_FAILURE;
    }

  svn_pool_destroy(pool);

  return EXIT_SUCCESS;
}
