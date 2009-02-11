/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 David Zeuthen <david@fubar.dk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include "ck-run-programs.h"

/* The number of wall-clock seconds a program is allowed to run before we kill it */
#define TIMEOUT_SECONDS 15

/* Guaranteed by POSIX; see 'man environ' for details */
extern char **environ;

typedef struct {
        int      refcount;

        char    *path;

        gboolean child_is_running;
        guint    watch_id;
        guint    timeout_id;
        GPid     pid;
} ChildData;

static ChildData *
_child_data_new (void)
{
        ChildData *cd;

        cd = g_new0 (ChildData, 1);
        cd->refcount = 1;
        g_debug ("Allocated ChildData %p", cd);

        return cd;
}

static ChildData *
_child_data_ref (ChildData *cd)
{
        cd->refcount++;
        return cd;
}


static void
_child_data_unref (ChildData *cd)
{
        cd->refcount--;
        if (cd->refcount == 0) {
                g_free (cd->path);
                g_free (cd);
                g_debug ("Freeing ChildData %p", cd);
        }
}


static void
_child_watch (GPid       pid,
              int        status,
              ChildData *cd)
{
        g_debug ("In _child_watch for pid %d", pid);

        g_spawn_close_pid (pid);
        g_source_remove (cd->timeout_id);

        cd->child_is_running = FALSE;

        _child_data_unref (cd);
}

static gboolean
_child_timeout (ChildData *cd)
{
        /* The program we ran timed out; this is a bug in the program */
        g_warning ("The program %s didn't exit within %d seconds; killing it", cd->path, TIMEOUT_SECONDS);

        kill (cd->pid, SIGTERM);

        cd->child_is_running = FALSE;
        return FALSE;
}

/**
 * ck_run_programs:
 * @dirpath: Path to a directory containing programs to run
 * @action: Argument to pass to each program
 * @extra_env: Extra environment to pass
 *
 * Synchronously run all scripts with suffix .ck in the given
 * directory.
 */
void
ck_run_programs (const char *dirpath,
                 const char *action,
                 char      **extra_env)
{
        GDir       *dir;
        GError     *error;
        const char *name;
        char      **env_for_child;
        int         environ_len;
        int         extra_env_len;
        int         n;
        int         m;

        g_return_if_fail (dirpath != NULL);
        g_return_if_fail (action != NULL);

        g_debug ("Running programs in %s for action %s", dirpath, action);

        /* Construct an environment consisting of the existing and the given environment */
        environ_len = environ != NULL ? g_strv_length (environ) : 0;
        extra_env_len = extra_env != NULL ? g_strv_length (extra_env) : 0;
        env_for_child = g_new0 (char *,  environ_len + extra_env_len + 2);
        m = 0;
        for (n = 0; n < environ_len; n++) {
                env_for_child [m++] = g_strdup (environ[n]);
        }
        for (n = 0; n < extra_env_len; n++) {
                env_for_child [m++] = g_strdup (extra_env[n]);
        }
        env_for_child[m] = NULL;

        error = NULL;

        dir = g_dir_open (dirpath, 0, &error);
        if (dir == NULL) {
                /* This is unexpected; it means ConsoleKit isn't properly installed */
                g_warning ("Unable to open directory %s: %s", dirpath, error->message);
                g_error_free (error);
                goto out;
        }

        while ((name = g_dir_read_name (dir)) != NULL) {
                char      *child_argv[3];
                ChildData *cd;
                gboolean   res;

                if (!g_str_has_suffix (name, ".ck"))
                        continue;

                child_argv[0] = g_strdup_printf ("%s/%s", dirpath, name);
                child_argv[1] = (char *) action;
                child_argv[2] = NULL;

                error = NULL;
                cd = _child_data_new ();
                cd->path = g_strdup (child_argv[0]);

                /* The ChildData instance is also unreffed in _child_watch; we only ref
                 * it here to prevent cd from being destroyed while checking it in
                 * the mainloop
                 */
                _child_data_ref (cd);

                res = g_spawn_async (NULL,
                                     child_argv,
                                     env_for_child,
                                     G_SPAWN_DO_NOT_REAP_CHILD,
                                     NULL,
                                     NULL,
                                     &cd->pid,
                                     &error);
                if (! res) {
                        /* This is unexpected; it means the program to run isn't installed correctly  */
                        g_warning ("Unable to spawn %s: %s", child_argv[0], error->message);
                        g_error_free (error);
                        _child_data_unref (cd);
                        _child_data_unref (cd);
                        goto out_loop;
                }
                cd->child_is_running = TRUE;

                g_debug ("Waiting for child with pid %d", cd->pid);

                cd->watch_id = g_child_watch_add (cd->pid,
                                                  (GChildWatchFunc)_child_watch,
                                                  cd);
                cd->timeout_id = g_timeout_add (TIMEOUT_SECONDS * 1000,
                                                (GSourceFunc)_child_timeout,
                                                cd);

                /* run the mainloop; this allows the main daemon to
                 * continue serving clients (including the program we
                 * just launched) */
                while (cd->child_is_running) {
                        g_main_context_iteration (NULL, TRUE);
                }

                g_debug ("Done waiting for child with pid %d", cd->pid);
                _child_data_unref (cd);

        out_loop:
                g_free (child_argv[0]);
        }
        g_dir_close (dir);
out:
        g_strfreev (env_for_child);
}
