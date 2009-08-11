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
#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "ck-session-leader.h"
#include "ck-job.h"

#define CK_SESSION_LEADER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CK_TYPE_SESSION_LEADER, CkSessionLeaderPrivate))

#define CK_TYPE_PARAMETER_STRUCT (dbus_g_type_get_struct ("GValueArray", \
                                                          G_TYPE_STRING, \
                                                          G_TYPE_VALUE, \
                                                          G_TYPE_INVALID))
#define CK_TYPE_PARAMETER_LIST (dbus_g_type_get_collection ("GPtrArray", \
                                                            CK_TYPE_PARAMETER_STRUCT))

struct CkSessionLeaderPrivate
{
        char       *id;
        uid_t       uid;
        pid_t       pid;
        char       *service_name;
        char       *session_id;
        char       *cookie;
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


static void
add_param_int (GPtrArray  *parameters,
               const char *key,
               const char *value)
{
        GValue val = { 0, };
        GValue param_val = { 0, };
        int    num;

        num = atoi (value);

        g_value_init (&val, G_TYPE_INT);
        g_value_set_int (&val, num);
        g_value_init (&param_val, CK_TYPE_PARAMETER_STRUCT);
        g_value_take_boxed (&param_val,
                            dbus_g_type_specialized_construct (CK_TYPE_PARAMETER_STRUCT));
        dbus_g_type_struct_set (&param_val,
                                0, key,
                                1, &val,
                                G_MAXUINT);
        g_value_unset (&val);

        g_ptr_array_add (parameters, g_value_get_boxed (&param_val));
}

static void
add_param_boolean (GPtrArray  *parameters,
                   const char *key,
                   const char *value)
{
        GValue   val = { 0, };
        GValue   param_val = { 0, };
        gboolean b;

        if (value != NULL && strcmp (value, "true") == 0) {
                b = TRUE;
        } else {
                b = FALSE;
        }

        g_value_init (&val, G_TYPE_BOOLEAN);
        g_value_set_boolean (&val, b);
        g_value_init (&param_val, CK_TYPE_PARAMETER_STRUCT);
        g_value_take_boxed (&param_val,
                            dbus_g_type_specialized_construct (CK_TYPE_PARAMETER_STRUCT));
        dbus_g_type_struct_set (&param_val,
                                0, key,
                                1, &val,
                                G_MAXUINT);
        g_value_unset (&val);

        g_ptr_array_add (parameters, g_value_get_boxed (&param_val));
}

static void
add_param_string (GPtrArray  *parameters,
                  const char *key,
                  const char *value)
{
        GValue val = { 0, };
        GValue param_val = { 0, };

        g_value_init (&val, G_TYPE_STRING);
        g_value_set_string (&val, value);

        g_value_init (&param_val, CK_TYPE_PARAMETER_STRUCT);
        g_value_take_boxed (&param_val,
                            dbus_g_type_specialized_construct (CK_TYPE_PARAMETER_STRUCT));

        dbus_g_type_struct_set (&param_val,
                                0, key,
                                1, &val,
                                G_MAXUINT);
        g_value_unset (&val);

        g_ptr_array_add (parameters, g_value_get_boxed (&param_val));
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
add_to_parameters (gpointer   key,
                   gpointer   data,
                   GPtrArray *parameters)
{
        gpointer data_copy;

        data_copy = g_boxed_copy (CK_TYPE_PARAMETER_STRUCT, data);
        g_ptr_array_add (parameters, data_copy);
}

typedef void (* CkAddParamFunc) (GPtrArray  *arr,
                                 const char *key,
                                 const char *value);

static struct {
        char          *key;
        CkAddParamFunc func;
} parse_ops[] = {
        { "login-session-id",   add_param_string },
        { "display-device",     add_param_string },
        { "x11-display-device", add_param_string },
        { "x11-display",        add_param_string },
        { "remote-host-name",   add_param_string },
        { "session-type",       add_param_string },
        { "is-local",           add_param_boolean },
        { "unix-user",          add_param_int },
};

static GPtrArray *
parse_output (CkSessionLeader *leader,
              const char      *output)
{
        GPtrArray *parameters;
        char     **lines;
        int        i;
        int        j;

        lines = g_strsplit (output, "\n", -1);
        if (lines == NULL) {
                return NULL;
        }

        parameters = g_ptr_array_sized_new (10);

        /* first add generated params */
        for (i = 0; lines[i] != NULL; i++) {
                char **vals;

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

                for (j = 0; j < G_N_ELEMENTS (parse_ops); j++) {
                        if (strcmp (vals[0], parse_ops[j].key) == 0) {
                                parse_ops[j].func (parameters, vals[0], vals[1]);
                                break;
                        }
                }
                g_strfreev (vals);
        }
        g_strfreev (lines);

        /* now overlay the overrides */
        g_hash_table_foreach (leader->priv->override_parameters,
                              (GHFunc)add_to_parameters,
                              parameters);

        return parameters;
}

static void
parameters_free (GPtrArray *parameters)
{
        int i;

        for (i = 0; i < parameters->len; i++) {
                gpointer data;
                data = g_ptr_array_index (parameters, i);
                if (data != NULL) {
                        g_boxed_free (CK_TYPE_PARAMETER_STRUCT, data);
                }
        }

        g_ptr_array_free (parameters, TRUE);
}

static void
save_parameters (CkSessionLeader *leader,
                 const GPtrArray *parameters)
{
        int i;

        for (i = 0; i < parameters->len; i++) {
                gpointer data;
                data = g_ptr_array_index (parameters, i);

                /* filter out the nulls? - sure why not */

                if (data != NULL) {
                        gpointer data_copy;
                        GValue   val_struct = { 0, };
                        char    *prop_name;
                        gboolean res;

                        g_value_init (&val_struct, CK_TYPE_PARAMETER_STRUCT);
                        g_value_set_static_boxed (&val_struct, g_ptr_array_index (parameters, i));

                        res = dbus_g_type_struct_get (&val_struct,
                                                      0, &prop_name,
                                                      G_MAXUINT);
                        if (! res) {
                                g_debug ("Unable to extract parameter input");
                                g_free (prop_name);
                                continue;
                        }

                        if (prop_name == NULL) {
                                g_debug ("Skipping NULL parameter");
                                g_free (prop_name);
                                continue;
                        }

                        if (strcmp (prop_name, "id") == 0
                            || strcmp (prop_name, "cookie") == 0) {
                                g_debug ("Skipping restricted parameter: %s", prop_name);
                                g_free (prop_name);
                                continue;
                        }

                        g_debug ("Setting override parameters for: %s", prop_name);

                        data_copy = g_boxed_copy (CK_TYPE_PARAMETER_STRUCT, data);

                        /* takes ownership */
                        g_hash_table_insert (leader->priv->override_parameters,
                                             prop_name,
                                             data_copy);
                }
        }
}

typedef struct {
        CkSessionLeader        *leader;
        CkSessionLeaderDoneFunc done_cb;
        gpointer                user_data;
        DBusGMethodInvocation  *context;
} JobData;

static void
job_completed (CkJob     *job,
               int        status,
               JobData   *data)
{
        g_debug ("Job status: %d", status);
        if (status == 0) {
                char      *output;
                GPtrArray *parameters;

                output = NULL;
                ck_job_get_stdout (job, &output);
                g_debug ("Job output: %s", output);

                parameters = parse_output (data->leader, output);
                g_free (output);

                data->done_cb (data->leader,
                               parameters,
                               data->context,
                               data->user_data);
                parameters_free (parameters);
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
        g_free (data);
}

gboolean
ck_session_leader_collect_parameters (CkSessionLeader        *session_leader,
                                      DBusGMethodInvocation  *context,
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
        data->leader = session_leader;
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
                                           const GPtrArray       *parameters)
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
        CkSessionLeader *self;

        self = CK_SESSION_LEADER (object);

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
        CkSessionLeader *self;

        self = CK_SESSION_LEADER (object);

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
        CkSessionLeaderClass *klass;

        klass = CK_SESSION_LEADER_CLASS (g_type_class_peek (CK_TYPE_SESSION_LEADER));

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
        g_boxed_free (CK_TYPE_PARAMETER_STRUCT, data);
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
