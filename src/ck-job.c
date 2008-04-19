/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
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
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "ck-job.h"
#include "ck-marshal.h"

#define CK_JOB_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CK_TYPE_JOB, CkJobPrivate))

struct CkJobPrivate
{
        guint       err_watch_id;
        guint       out_watch_id;

        char       *command;
        GString    *std_out;
        GString    *std_err;
        GPid        child_pid;

};

enum {
        COMPLETED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     ck_job_class_init  (CkJobClass *klass);
static void     ck_job_init        (CkJob      *job);
static void     ck_job_finalize    (GObject     *object);

G_DEFINE_TYPE (CkJob, ck_job, G_TYPE_OBJECT)

GQuark
ck_job_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("ck_job_error");
        }

        return ret;
}

static int
wait_on_child (int pid)
{
        int status;

 wait_again:
        if (waitpid (pid, &status, 0) < 0) {
                if (errno == EINTR) {
                        goto wait_again;
                } else if (errno == ECHILD) {
                        ; /* do nothing, child already reaped */
                } else {
                        g_debug ("waitpid () should not fail");
                }
        }

        return status;
}

static int
wait_on_job (CkJob *job)
{
        int status;

        status = wait_on_child (job->priv->child_pid);

        g_spawn_close_pid (job->priv->child_pid);
        job->priv->child_pid = 0;

        return WEXITSTATUS(status);
}

static void
maybe_complete_job (CkJob *job)
{
        if (job->priv->out_watch_id == 0
            && job->priv->err_watch_id == 0) {
                int status;

                status = wait_on_job (job);

                g_debug ("Emitting completed");
                g_signal_emit (job, signals [COMPLETED], 0, status);
        }
}

static gboolean
error_watch (GIOChannel   *source,
             GIOCondition  condition,
             CkJob        *job)
{
        gboolean finished = FALSE;

        if (condition & G_IO_IN) {
                GIOStatus status;
                GError   *error = NULL;
                char     *line;

                line = NULL;
                status = g_io_channel_read_line (source, &line, NULL, NULL, &error);

                switch (status) {
                case G_IO_STATUS_NORMAL:
                        g_debug ("command error output: %s", line);
                        g_string_append (job->priv->std_err, line);
                        break;
                case G_IO_STATUS_EOF:
                        finished = TRUE;
                        break;
                case G_IO_STATUS_ERROR:
                        finished = TRUE;
                        g_debug ("Error reading from child: %s\n", error->message);
                        break;
                case G_IO_STATUS_AGAIN:
                default:
                        break;
                }
                g_free (line);
        } else if (condition & G_IO_HUP) {
                finished = TRUE;
        }

        if (finished) {
                job->priv->err_watch_id = 0;
                maybe_complete_job (job);
                return FALSE;
        }

        return TRUE;
}

static gboolean
out_watch (GIOChannel   *source,
           GIOCondition  condition,
           CkJob        *job)
{
        gboolean finished = FALSE;

        if (condition & G_IO_IN) {
                GIOStatus status;
                GError   *error = NULL;
                char     *line;

                line = NULL;
                status = g_io_channel_read_line (source, &line, NULL, NULL, &error);

                switch (status) {
                case G_IO_STATUS_NORMAL:
                        g_string_append (job->priv->std_out, line);
                        break;
                case G_IO_STATUS_EOF:
                        finished = TRUE;
                        break;
                case G_IO_STATUS_ERROR:
                        finished = TRUE;
                        g_debug ("Error reading from child: %s\n", error->message);
                        break;
                case G_IO_STATUS_AGAIN:
                default:
                        break;
                }
                g_free (line);
        } else if (condition & G_IO_HUP) {
                finished = TRUE;
        }

        if (finished) {
                job->priv->out_watch_id = 0;
                maybe_complete_job (job);
                return FALSE;
        }

        return TRUE;
}

gboolean
ck_job_execute (CkJob   *job,
                GError **error)
{
        GError     *local_error;
        gboolean    res;
        GIOChannel *channel;
        int         standard_output;
        int         standard_error;
        int         argc;
        char      **argv;

        g_debug ("Executing %s", job->priv->command);
        local_error = NULL;
        if (! g_shell_parse_argv (job->priv->command, &argc, &argv, &local_error)) {
                g_debug ("Could not parse command: %s", local_error->message);
                g_propagate_error (error, local_error);
                return FALSE;
        }

        local_error = NULL;
        res = g_spawn_async_with_pipes (NULL,
                                        argv,
                                        NULL,
                                        G_SPAWN_DO_NOT_REAP_CHILD,
                                        NULL,
                                        NULL,
                                        &job->priv->child_pid,
                                        NULL,
                                        &standard_output,
                                        &standard_error,
                                        &local_error);

        g_strfreev (argv);
        if (! res) {
                g_debug ("Could not start command '%s': %s",
                          job->priv->command,
                          local_error->message);
                g_propagate_error (error, local_error);
                return FALSE;
        }

        /* output channel */
        channel = g_io_channel_unix_new (standard_output);
        g_io_channel_set_close_on_unref (channel, TRUE);
        g_io_channel_set_flags (channel,
                                g_io_channel_get_flags (channel) | G_IO_FLAG_NONBLOCK,
                                NULL);
        job->priv->out_watch_id = g_io_add_watch (channel,
                                                  G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
                                                  (GIOFunc)out_watch,
                                                  job);
        g_io_channel_unref (channel);

        /* error channel */
        channel = g_io_channel_unix_new (standard_error);
        g_io_channel_set_close_on_unref (channel, TRUE);
        g_io_channel_set_flags (channel,
                                g_io_channel_get_flags (channel) | G_IO_FLAG_NONBLOCK,
                                NULL);
        job->priv->err_watch_id = g_io_add_watch (channel,
                                                  G_IO_IN | G_IO_HUP | G_IO_ERR | G_IO_NVAL,
                                                  (GIOFunc)error_watch,
                                                  job);
        g_io_channel_unref (channel);

        return res;
}

gboolean
ck_job_get_stdout (CkJob *job,
                   char **std_outp)
{
        if (std_outp != NULL) {
                *std_outp = g_strdup (job->priv->std_out->str);
        }
        return TRUE;
}

gboolean
ck_job_set_command (CkJob      *job,
                    const char *command)
{
        g_free (job->priv->command);
        job->priv->command = g_strdup (command);
        return TRUE;
}

gboolean
ck_job_get_command (CkJob      *job,
                    char      **command)
{
        if (command != NULL) {
                *command = g_strdup (job->priv->command);
        }

        return TRUE;
}

static void
ck_job_class_init (CkJobClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = ck_job_finalize;

        signals [COMPLETED] =
                g_signal_new ("completed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CkJobClass, completed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__INT,
                              G_TYPE_NONE,
                              1, G_TYPE_INT);

        g_type_class_add_private (klass, sizeof (CkJobPrivate));
}

static void
ck_job_init (CkJob *job)
{
        job->priv = CK_JOB_GET_PRIVATE (job);

        job->priv->std_err = g_string_new (NULL);
        job->priv->std_out = g_string_new (NULL);
}

gboolean
ck_job_cancel (CkJob *job)
{
        if (job->priv->child_pid > 0) {
                kill (job->priv->child_pid, SIGTERM);
                wait_on_job (job);
                return TRUE;
        }

        return FALSE;
}

static void
ck_job_finalize (GObject *object)
{
        CkJob *job;

        g_debug ("Finalizing job");

        g_return_if_fail (object != NULL);
        g_return_if_fail (CK_IS_JOB (object));

        job = CK_JOB (object);

        g_return_if_fail (job->priv != NULL);

        ck_job_cancel (job);

        if (job->priv->out_watch_id > 0) {
                g_source_remove (job->priv->out_watch_id);
        }
        if (job->priv->err_watch_id > 0) {
                g_source_remove (job->priv->err_watch_id);
        }
        g_free (job->priv->command);
        g_string_free (job->priv->std_out, TRUE);
        g_string_free (job->priv->std_err, TRUE);

        G_OBJECT_CLASS (ck_job_parent_class)->finalize (object);
}

CkJob *
ck_job_new (void)
{
        GObject *object;

        object = g_object_new (CK_TYPE_JOB, NULL);

        return CK_JOB (object);
}
