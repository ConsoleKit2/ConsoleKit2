/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006-2007 William Jon McCann <mccann@jhu.edu>
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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include "ck-session-leader.h"
#include "ck-job.h"

#define CK_SESSION_LEADER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CK_TYPE_SESSION_LEADER, CkSessionLeaderPrivate))


static struct {
        const char *name;
        const char *variant_type;
        GType       gtype;
} parameter_lookup[] = {
        { "login-session-id",   "s", G_TYPE_STRING },
        { "display-device",     "s", G_TYPE_STRING },
        { "x11-display-device", "s", G_TYPE_STRING },
        { "x11-display",        "s", G_TYPE_STRING },
        { "remote-host-name",   "s", G_TYPE_STRING },
        { "session-type",       "s", G_TYPE_STRING },
        { "is-local",           "b", G_TYPE_BOOLEAN },
        { "unix-user",          "i", G_TYPE_INT },
};

struct CkSessionLeaderPrivate
{
        char       *id;
        uid_t       uid;
        pid_t       pid;
        char       *service_name;
        char       *session_id;
        char       *cookie;
        char       *runtime_dir;
        GList      *pending_jobs;
        gboolean    cancelled;
        GHashTable *override_parameters;
};

enum {
        PROP_0,
};

static void     ck_session_leader_class_init  (CkSessionLeaderClass *klass);
static void     ck_session_leader_init        (CkSessionLeader      *session_leader);
static void     ck_session_leader_finalize    (GObject              *object);

G_DEFINE_TYPE (CkSessionLeader, ck_session_leader, G_TYPE_OBJECT)

GQuark
ck_session_leader_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("ck_session_leader_error");
        }

        return ret;
}

static void
remove_pending_job (CkJob *job)
{
        if (job != NULL) {
                char *command;

                command = NULL;
                ck_job_get_command (job, &command);
                g_debug ("Removing pending job: %s", command);
                g_free (command);

                ck_job_cancel (job);
                g_object_unref (job);
        }
}

void
ck_session_leader_cancel (CkSessionLeader *leader)
{
        g_return_if_fail (CK_IS_SESSION_LEADER (leader));

        if (leader->priv->pending_jobs != NULL) {
                g_list_foreach (leader->priv->pending_jobs, (GFunc)remove_pending_job, NULL);
                g_list_free (leader->priv->pending_jobs);
                leader->priv->pending_jobs = NULL;
        }

        leader->priv->cancelled = TRUE;
}

static gboolean
have_override_parameter (CkSessionLeader *leader,
                         const char      *prop_name)
{
        gpointer data;

        if (leader->priv->override_parameters == NULL) {
                return FALSE;
        }

        if (prop_name == NULL) {
                return FALSE;
        }

        data = g_hash_table_lookup (leader->priv->override_parameters, prop_name);
        if (data == NULL) {
                return FALSE;
        }

        return TRUE;
}

static void
lookup_parameter_type (const char *name, const char **variant_type, GType *gtype)
{
        int i;

        *gtype = G_TYPE_INVALID;

        for (i = 0; i < G_N_ELEMENTS (parameter_lookup); i++) {
                if (strcmp (name, parameter_lookup[i].name) == 0) {
                        *variant_type = parameter_lookup[i].variant_type;
                        *gtype = parameter_lookup[i].gtype;
                        return;
                }
        }
}

static void
add_to_parameters (gpointer         key,
                   gpointer         data,
                   GVariantBuilder *ck_parameters)
{
        g_variant_builder_add (ck_parameters, "{sv}", key, (GVariant*) data);
}

/* Allocates and returns a GVariantBuilder holding all the parameters,
 * free with g_variant_builder_unref when done using it */
static GVariant *
parse_output (CkSessionLeader *leader,
              const char      *output)
{
        GVariantBuilder ck_parameters;
        char     **lines;
        int        i;

        lines = g_strsplit (output, "\n", -1);
        if (lines == NULL) {
                return NULL;
        }

        g_variant_builder_init (&ck_parameters, G_VARIANT_TYPE ("a{sv}"));

        /* first add generated params */
        for (i = 0; lines[i] != NULL; i++) {
                char      **vals;
                const char *variant_type;
                GVariant   *element;
                GType       gtype;
                glong       unix_user;
                gboolean    is_local = FALSE;

                vals = g_strsplit (lines[i], " = ", 2);
                if (vals == NULL || vals[0] == NULL) {
                        g_strfreev (vals);
                        continue;
                }

                /* we're going to override this anyway so just shortcut out */
                if (have_override_parameter (leader, vals[0])) {
                        g_strfreev (vals);
                        continue;
                }

                lookup_parameter_type (vals[0], &variant_type, &gtype);
                if (gtype == G_TYPE_INVALID) {
                        g_warning ("invalid parameter type\n");
                        continue;
                }

                switch (gtype) {
                case G_TYPE_STRING:
                        element = g_variant_new (variant_type, vals[1]);
                        break;
                case G_TYPE_BOOLEAN:
                        if(g_ascii_strncasecmp (vals[1], "TRUE", 4) == 0) {
                                is_local = TRUE;
                        }

                        element = g_variant_new (variant_type, is_local);
                        break;
                case G_TYPE_INT:
                        unix_user = strtol (vals[1], NULL, 10);

                        /* Error checking for untrusted input */
                        if ((errno == ERANGE && (unix_user == LONG_MAX || unix_user == LONG_MIN)) || (errno != 0 && unix_user == 0))
                        {
                                continue;
                        }

                        /* Sanity checks */
                        if (unix_user > INT_MAX)
                                continue;

                        if (unix_user < 0)
                                continue;

                        element = g_variant_new (variant_type, (uid_t)unix_user);
                        break;
                default:
                        g_warning ("ck-session-leader unsupported type");
                        continue;
                }

                g_variant_builder_add (&ck_parameters, "{sv}", vals[0], element);

                g_strfreev (vals);
        }
        g_strfreev (lines);

        /* now overlay the overrides */
        g_hash_table_foreach (leader->priv->override_parameters,
                              (GHFunc)add_to_parameters,
                              &ck_parameters);

        return g_variant_builder_end (&ck_parameters);
}

static void
save_parameters (CkSessionLeader *leader,
                 const GVariant  *parameters)
{
        GVariantIter     *iter;
        gchar            *prop_name;
        GVariant         *value;

        g_variant_get ((GVariant *)parameters, "a(sv)", &iter);

        while (g_variant_iter_next (iter, "(sv)", &prop_name, &value)) {

                /* filter out the nulls? - sure why not */
                if (value != NULL) {
                        const char *variant_type;
                        GType       gtype;

                        if (prop_name == NULL) {
                                g_debug ("Skipping NULL parameter");
                                g_variant_unref (value);
                                continue;
                        }

                        if (strcmp (prop_name, "id") == 0
                            || strcmp (prop_name, "cookie") == 0) {
                                g_debug ("Skipping restricted parameter: %s", prop_name);
                                g_free (prop_name);
                                g_variant_unref (value);
                                continue;
                        }

                        lookup_parameter_type (prop_name, &variant_type, &gtype);
                        if (gtype == G_TYPE_INVALID) {
                                g_debug ("Unable to extract parameter input");
                                g_free (prop_name);
                                g_variant_unref (value);
                                continue;
                        }

                        g_debug ("Setting override parameters for: %s", prop_name);

                        /* takes ownership */
                        g_hash_table_insert (leader->priv->override_parameters,
                                             prop_name,
                                             value);
                }
        }
        g_variant_iter_free (iter);
}

typedef struct {
        CkSessionLeader        *leader;
        CkSessionLeaderDoneFunc done_cb;
        gpointer                user_data;
        GDBusMethodInvocation  *context;
} JobData;

static void
job_completed (CkJob     *job,
               int        status,
               JobData   *data)
{
        g_debug ("Job status: %d", status);
        if (status == 0) {
                char      *output;
                GVariant  *parameters;

                output = NULL;
                ck_job_get_stdout (job, &output);
                g_debug ("Job output: %s", output);

                parameters = parse_output (data->leader, output);
                g_free (output);

                data->done_cb (data->leader,
                               parameters,
                               data->context,
                               data->user_data);
        } else {
                data->done_cb (data->leader,
                               NULL,
                               data->context,
                               data->user_data);
        }

        /* remove job from queue */
        data->leader->priv->pending_jobs = g_list_remove (data->leader->priv->pending_jobs, job);

        g_signal_handlers_disconnect_by_func (job, job_completed, data);
        g_object_unref (job);
}

static void
job_data_free (JobData *data)
{
        g_object_unref (data->leader);
        g_free (data);
}

gboolean
ck_session_leader_collect_parameters (CkSessionLeader        *session_leader,
                                      GDBusMethodInvocation  *context,
                                      CkSessionLeaderDoneFunc done_cb,
                                      gpointer                user_data)
{
        GError      *local_error;
        char        *command;
        gboolean     res;
        gboolean     ret;
        CkJob       *job;
        JobData     *data;

        ret = FALSE;

        data = g_new0 (JobData, 1);
        data->leader = g_object_ref (session_leader);
        data->done_cb = done_cb;
        data->user_data = user_data;
        data->context = context;

        command = g_strdup_printf ("%s --uid %u --pid %u",
                                   LIBEXECDIR "/ck-collect-session-info",
                                   session_leader->priv->uid,
                                   session_leader->priv->pid);
        job = ck_job_new ();
        ck_job_set_command (job, command);
        g_free (command);

        g_signal_connect_data (job,
                               "completed",
                               G_CALLBACK (job_completed),
                               data,
                               (GClosureNotify)job_data_free,
                               0);

        local_error = NULL;
        res = ck_job_execute (job, &local_error);
        if (! res) {
                if (local_error != NULL) {
                        g_debug ("stat on pid %d failed: %s", session_leader->priv->pid, local_error->message);
                        g_error_free (local_error);
                }

                g_object_unref (job);

                goto out;
        }

        /* Add job to queue */
        session_leader->priv->pending_jobs = g_list_prepend (session_leader->priv->pending_jobs, job);
        ret = TRUE;

 out:
        return ret;
}


const char *
ck_session_leader_peek_session_id    (CkSessionLeader        *session_leader)
{
        g_return_val_if_fail (CK_IS_SESSION_LEADER (session_leader), NULL);
        return session_leader->priv->session_id;
}

const char *
ck_session_leader_peek_cookie        (CkSessionLeader        *session_leader)
{
        g_return_val_if_fail (CK_IS_SESSION_LEADER (session_leader), NULL);
        return session_leader->priv->cookie;
}

const char *
ck_session_leader_peek_service_name  (CkSessionLeader        *session_leader)
{
        g_return_val_if_fail (CK_IS_SESSION_LEADER (session_leader), NULL);
        return session_leader->priv->service_name;
}

uid_t
ck_session_leader_get_uid            (CkSessionLeader        *session_leader)
{
        g_return_val_if_fail (CK_IS_SESSION_LEADER (session_leader), -1);
        return session_leader->priv->uid;
}

pid_t
ck_session_leader_get_pid            (CkSessionLeader        *session_leader)
{
        g_return_val_if_fail (CK_IS_SESSION_LEADER (session_leader), -1);
        return session_leader->priv->pid;
}

void
ck_session_leader_set_pid          (CkSessionLeader       *session_leader,
                                    pid_t                  pid)
{
        g_return_if_fail (CK_IS_SESSION_LEADER (session_leader));
        session_leader->priv->pid = pid;
}

void
ck_session_leader_set_uid          (CkSessionLeader       *session_leader,
                                    uid_t                  uid)
{
        g_return_if_fail (CK_IS_SESSION_LEADER (session_leader));
        session_leader->priv->uid = uid;
}

void
ck_session_leader_set_session_id   (CkSessionLeader       *session_leader,
                                    const char            *session_id)
{
        g_return_if_fail (CK_IS_SESSION_LEADER (session_leader));
        g_free (session_leader->priv->session_id);
        session_leader->priv->session_id = g_strdup (session_id);
}

void
ck_session_leader_set_cookie       (CkSessionLeader       *session_leader,
                                    const char            *cookie)
{
        g_return_if_fail (CK_IS_SESSION_LEADER (session_leader));
        g_free (session_leader->priv->cookie);
        session_leader->priv->cookie = g_strdup (cookie);
}

void
ck_session_leader_set_service_name (CkSessionLeader       *session_leader,
                                    const char            *service_name)
{
        g_return_if_fail (CK_IS_SESSION_LEADER (session_leader));
        g_free (session_leader->priv->service_name);
        session_leader->priv->service_name = g_strdup (service_name);
}

void
ck_session_leader_set_override_parameters (CkSessionLeader       *session_leader,
                                           const GVariant        *parameters)
{
        g_return_if_fail (CK_IS_SESSION_LEADER (session_leader));

        if (session_leader->priv->override_parameters != NULL) {
                g_hash_table_remove_all (session_leader->priv->override_parameters);
        }

        if (parameters != NULL) {
                save_parameters (session_leader, parameters);
        }
}

static void
ck_session_leader_set_property (GObject            *object,
                                guint               prop_id,
                                const GValue       *value,
                                GParamSpec         *pspec)
{
        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
ck_session_leader_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
        switch (prop_id) {
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static GObject *
ck_session_leader_constructor (GType                  type,
                               guint                  n_construct_properties,
                               GObjectConstructParam *construct_properties)
{
        CkSessionLeader      *session_leader;

        session_leader = CK_SESSION_LEADER (G_OBJECT_CLASS (ck_session_leader_parent_class)->constructor (type,
                                                                                                          n_construct_properties,
                                                                                                          construct_properties));

        return G_OBJECT (session_leader);
}

static void
ck_session_leader_class_init (CkSessionLeaderClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->constructor = ck_session_leader_constructor;
        object_class->get_property = ck_session_leader_get_property;
        object_class->set_property = ck_session_leader_set_property;
        object_class->finalize = ck_session_leader_finalize;

        g_type_class_add_private (klass, sizeof (CkSessionLeaderPrivate));
}

static void
parameter_free (gpointer data)
{
        g_variant_unref ((GVariant*)data);
}

static void
ck_session_leader_init (CkSessionLeader *session_leader)
{
        session_leader->priv = CK_SESSION_LEADER_GET_PRIVATE (session_leader);

        session_leader->priv->override_parameters = g_hash_table_new_full (g_str_hash,
                                                                           g_str_equal,
                                                                           g_free,
                                                                           (GDestroyNotify)parameter_free);
}

static void
ck_session_leader_finalize (GObject *object)
{
        CkSessionLeader *session_leader;

        g_return_if_fail (object != NULL);
        g_return_if_fail (CK_IS_SESSION_LEADER (object));

        session_leader = CK_SESSION_LEADER (object);

        g_return_if_fail (session_leader->priv != NULL);

        g_free (session_leader->priv->session_id);
        session_leader->priv->session_id = NULL;
        g_free (session_leader->priv->cookie);
        session_leader->priv->cookie = NULL;
        g_free (session_leader->priv->service_name);
        session_leader->priv->service_name = NULL;

        g_hash_table_destroy (session_leader->priv->override_parameters);

        G_OBJECT_CLASS (ck_session_leader_parent_class)->finalize (object);
}

CkSessionLeader *
ck_session_leader_new (void)
{
        GObject *object;

        object = g_object_new (CK_TYPE_SESSION_LEADER,
                               NULL);

        return CK_SESSION_LEADER (object);
}

void
ck_session_leader_dump (CkSessionLeader *session_leader,
                        GKeyFile        *key_file)
{
        char *group_name;

        group_name = g_strdup_printf ("SessionLeader %s", session_leader->priv->session_id);
        g_key_file_set_string (key_file, group_name, "session", session_leader->priv->session_id);
        g_key_file_set_integer (key_file, group_name, "uid", session_leader->priv->uid);
        g_key_file_set_integer (key_file, group_name, "pid", session_leader->priv->pid);
        g_key_file_set_string (key_file, group_name, "service_name", session_leader->priv->service_name);

        g_free (group_name);
}
