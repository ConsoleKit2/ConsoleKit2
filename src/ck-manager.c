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
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "ck-manager.h"
#include "ck-manager-glue.h"
#include "ck-seat.h"
#include "ck-session.h"
#include "ck-job.h"
#include "ck-marshal.h"

#include "ck-sysdeps.h"

#define CK_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CK_TYPE_MANAGER, CkManagerPrivate))

#define CK_SEAT_DIR SYSCONFDIR "/ConsoleKit/seats.d"

#define CK_DBUS_PATH         "/org/freedesktop/ConsoleKit"
#define CK_MANAGER_DBUS_PATH CK_DBUS_PATH "/Manager"
#define CK_MANAGER_DBUS_NAME "org.freedesktop.ConsoleKit.Manager"

#define CK_TYPE_PARAMETER_STRUCT (dbus_g_type_get_struct ("GValueArray", \
                                                          G_TYPE_STRING, \
                                                          G_TYPE_VALUE, \
                                                          G_TYPE_INVALID))
#define CK_TYPE_PARAMETER_LIST (dbus_g_type_get_collection ("GPtrArray", \
                                                            CK_TYPE_PARAMETER_STRUCT))
struct CkManagerPrivate
{
        GHashTable      *seats;
        GHashTable      *sessions;
        GHashTable      *leaders;

        DBusGProxy      *bus_proxy;
        DBusGConnection *connection;

        guint32          session_serial;
        guint32          seat_serial;

        gboolean         system_idle_hint;
        GTimeVal         system_idle_since_hint;
};


typedef struct {
        int         refcount;
        gboolean    cancelled;
        uid_t       uid;
        pid_t       pid;
        char       *service_name;
        char       *ssid;
        char       *cookie;
        GList      *pending_jobs;
} LeaderInfo;

enum {
        SEAT_ADDED,
        SEAT_REMOVED,
        SYSTEM_IDLE_HINT_CHANGED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     ck_manager_class_init  (CkManagerClass *klass);
static void     ck_manager_init        (CkManager      *manager);
static void     ck_manager_finalize    (GObject        *object);

static gpointer manager_object = NULL;

G_DEFINE_TYPE (CkManager, ck_manager, G_TYPE_OBJECT)

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

static void
_leader_info_free (LeaderInfo *info)
{
        g_debug ("Freeing leader info: %s", info->ssid);

        g_free (info->ssid);
        info->ssid = NULL;
        g_free (info->cookie);
        info->cookie = NULL;
        g_free (info->service_name);
        info->service_name = NULL;

        g_free (info);
}

static void
leader_info_cancel (LeaderInfo *info)
{
        if (info->pending_jobs != NULL) {
                g_list_foreach (info->pending_jobs, (GFunc)remove_pending_job, NULL);
                g_list_free (info->pending_jobs);
                info->pending_jobs = NULL;
        }

        info->cancelled = TRUE;
}

static void
leader_info_unref (LeaderInfo *info)
{
        /* Probably should use some kind of atomic op here */
        info->refcount -= 1;
        if (info->refcount == 0) {
                _leader_info_free (info);
        }
}

static LeaderInfo *
leader_info_ref (LeaderInfo *info)
{
        info->refcount += 1;

        return info;
}

GQuark
ck_manager_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("ck_manager_error");
        }

        return ret;
}

static guint32
get_next_session_serial (CkManager *manager)
{
        guint32 serial;

        serial = manager->priv->session_serial++;

        if ((gint32)manager->priv->session_serial < 0) {
                manager->priv->session_serial = 1;
        }

        return serial;
}

static guint32
get_next_seat_serial (CkManager *manager)
{
        guint32 serial;

        serial = manager->priv->seat_serial++;

        if ((gint32)manager->priv->seat_serial < 0) {
                manager->priv->seat_serial = 1;
        }

        return serial;
}

static char *
generate_session_cookie (CkManager *manager)
{
        guint32  num;
        char    *cookie;
        GTimeVal tv;
        char    *uuid;

        uuid = dbus_get_local_machine_id ();
        if (uuid == NULL) {
                uuid = g_strdup (g_get_host_name ());
        }

        /* We want this to be globally unique
           or at least such that it won't cycle when there
           may be orphan processes in a dead session. */

        cookie = NULL;
 again:
        num = (guint32)g_random_int_range (1, G_MAXINT32);

        g_get_current_time (&tv);

        g_free (cookie);
        cookie = g_strdup_printf ("%s-%ld.%ld-%u", uuid, tv.tv_sec, tv.tv_usec, num);

        if (g_hash_table_lookup (manager->priv->leaders, cookie)) {
                goto again;
        }

        g_free (uuid);

        return cookie;
}

static char *
generate_session_id (CkManager *manager)
{
        guint32 serial;
        char   *id;

        id = NULL;
 again:
        serial = get_next_session_serial (manager);
        g_free (id);
        id = g_strdup_printf ("%s/Session%u", CK_DBUS_PATH, serial);

        if (g_hash_table_lookup (manager->priv->sessions, id)) {
                goto again;
        }

        return id;
}

static char *
generate_seat_id (CkManager *manager)
{
        guint32 serial;
        char   *id;

        id = NULL;
 again:
        serial = get_next_seat_serial (manager);
        g_free (id);
        id = g_strdup_printf ("%s/Seat%u", CK_DBUS_PATH, serial);

        if (g_hash_table_lookup (manager->priv->seats, id)) {
                goto again;
        }

        return id;
}

static CkSeat *
add_new_seat (CkManager *manager,
              CkSeatKind kind)
{
        char   *sid;
        CkSeat *seat;

        sid = generate_seat_id (manager);

        seat = ck_seat_new (sid, kind);
        if (seat == NULL) {
                /* returns null if connection to bus fails */
                g_free (sid);
                goto out;
        }

        g_hash_table_insert (manager->priv->seats, sid, seat);

        g_debug ("Added seat: %s kind:%d", sid, kind);

        g_signal_emit (manager, signals [SEAT_ADDED], 0, sid);

 out:
        return seat;
}

static void
remove_seat (CkManager *manager,
             CkSeat    *seat)
{
        char *sid;

        sid = NULL;
        ck_seat_get_id (seat, &sid, NULL);

        if (sid != NULL) {
                g_hash_table_remove (manager->priv->seats, sid);
        }

        g_signal_emit (manager, signals [SEAT_REMOVED], 0, sid);

        g_debug ("Removed seat: %s", sid);

        g_free (sid);
}

#define IS_STR_SET(x) (x != NULL && x[0] != '\0')

static CkSeat *
find_seat_for_session (CkManager *manager,
                       CkSession *session)
{
        CkSeat  *seat;
        gboolean is_static_x11;
        gboolean is_static_text;
        char    *display_device;
        char    *x11_display_device;
        char    *x11_display;
        char    *remote_host_name;
        gboolean is_local;

        is_static_text = FALSE;
        is_static_x11 = FALSE;

        seat = NULL;
        display_device = NULL;
        x11_display_device = NULL;
        x11_display = NULL;
        remote_host_name = NULL;
        is_local = FALSE;

        /* FIXME: use matching to group entries? */

        ck_session_get_display_device (session, &display_device, NULL);
        ck_session_get_x11_display_device (session, &x11_display_device, NULL);
        ck_session_get_x11_display (session, &x11_display, NULL);
        ck_session_get_remote_host_name (session, &remote_host_name, NULL);
        ck_session_is_local (session, &is_local, NULL);

        if (IS_STR_SET (x11_display)
            && IS_STR_SET (x11_display_device)
            && ! IS_STR_SET (remote_host_name)
            && is_local == TRUE) {
                is_static_x11 = TRUE;
        } else if (! IS_STR_SET (x11_display)
                   && ! IS_STR_SET (x11_display_device)
                   && IS_STR_SET (display_device)
                   && ! IS_STR_SET (remote_host_name)
                   && is_local == TRUE) {
                is_static_text = TRUE;
        }

        if (is_static_x11 || is_static_text) {
                char *sid;
                sid = g_strdup_printf ("%s/Seat%u", CK_DBUS_PATH, 1);
                seat = g_hash_table_lookup (manager->priv->seats, sid);
                g_free (sid);
        }

        g_free (display_device);
        g_free (x11_display_device);
        g_free (x11_display);
        g_free (remote_host_name);

        return seat;
}

/* adapted from PolicyKit */
static gboolean
get_caller_info (CkManager   *manager,
                 const char  *sender,
                 uid_t       *calling_uid,
                 pid_t       *calling_pid)
{
        gboolean res;
        GError  *error = NULL;

        res = FALSE;

        if (sender == NULL) {
                goto out;
        }

        if (! dbus_g_proxy_call (manager->priv->bus_proxy, "GetConnectionUnixUser", &error,
                                 G_TYPE_STRING, sender,
                                 G_TYPE_INVALID,
                                 G_TYPE_UINT, calling_uid,
                                 G_TYPE_INVALID)) {
                g_debug ("GetConnectionUnixUser() failed: %s", error->message);
                g_error_free (error);
                goto out;
        }

        if (! dbus_g_proxy_call (manager->priv->bus_proxy, "GetConnectionUnixProcessID", &error,
                                 G_TYPE_STRING, sender,
                                 G_TYPE_INVALID,
                                 G_TYPE_UINT, calling_pid,
                                 G_TYPE_INVALID)) {
                g_debug ("GetConnectionUnixProcessID() failed: %s", error->message);
                g_error_free (error);
                goto out;
        }

        res = TRUE;

        g_debug ("uid = %d", *calling_uid);
        g_debug ("pid = %d", *calling_pid);

out:
        return res;
}

static gboolean
manager_set_system_idle_hint (CkManager *manager,
                              gboolean   idle_hint)
{
        if (manager->priv->system_idle_hint != idle_hint) {
                manager->priv->system_idle_hint = idle_hint;

                /* FIXME: can we get a time from the dbus message? */
                g_get_current_time (&manager->priv->system_idle_since_hint);

                g_debug ("Emitting system-idle-hint-changed: %d", idle_hint);
                g_signal_emit (manager, signals [SYSTEM_IDLE_HINT_CHANGED], 0, idle_hint);
        }

        return TRUE;
}

static gboolean
is_session_busy (char      *id,
                 CkSession *session,
                 gpointer   data)
{
        gboolean idle_hint;

        idle_hint = FALSE;

        ck_session_get_idle_hint (session, &idle_hint, NULL);

        /* return TRUE to stop search */
        return !idle_hint;
}

static void
manager_update_system_idle_hint (CkManager *manager)
{
        CkSession *session;
        gboolean   system_idle;

        /* just look for any session that doesn't have the idle-hint set */
        session = g_hash_table_find (manager->priv->sessions, (GHRFunc)is_session_busy, NULL);

        /* if there aren't any busy sessions then the system is idle */
        system_idle = (session == NULL);

        manager_set_system_idle_hint (manager, system_idle);
}

static void
session_idle_hint_changed (CkSession  *session,
                           gboolean    idle_hint,
                           CkManager  *manager)
{
        manager_update_system_idle_hint (manager);
}

/*
  Example:
  dbus-send --system --dest=org.freedesktop.ConsoleKit \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/freedesktop/ConsoleKit/Manager \
  org.freedesktop.ConsoleKit.Manager.GetSystemIdleHint
*/
gboolean
ck_manager_get_system_idle_hint (CkManager *manager,
                                 gboolean  *idle_hint,
                                 GError   **error)
{
        g_return_val_if_fail (CK_IS_MANAGER (manager), FALSE);

        if (idle_hint != NULL) {
                *idle_hint = manager->priv->system_idle_hint;
        }

        return TRUE;
}

#if GLIB_CHECK_VERSION(2,12,0)
#define _g_time_val_to_iso8601(t) g_time_val_to_iso8601(t)
#else
/* copied from GLib */
static gchar *
_g_time_val_to_iso8601 (GTimeVal *time_)
{
  gchar *retval;

  g_return_val_if_fail (time_->tv_usec >= 0 && time_->tv_usec < G_USEC_PER_SEC, NULL);

#define ISO_8601_LEN    21
#define ISO_8601_FORMAT "%Y-%m-%dT%H:%M:%SZ"
  retval = g_new0 (gchar, ISO_8601_LEN + 1);

  strftime (retval, ISO_8601_LEN,
            ISO_8601_FORMAT,
            gmtime (&(time_->tv_sec)));

  return retval;
}
#endif

gboolean
ck_manager_get_system_idle_since_hint (CkManager *manager,
                                       char    **iso8601_datetime,
                                       GError  **error)
{
        char *date_str;

        g_return_val_if_fail (CK_IS_MANAGER (manager), FALSE);

        date_str = NULL;
        if (manager->priv->system_idle_hint) {
                date_str = _g_time_val_to_iso8601 (&manager->priv->system_idle_since_hint);
        }

        if (iso8601_datetime != NULL) {
                *iso8601_datetime = g_strdup (date_str);
        }

        g_free (date_str);

        return TRUE;
}

static void
open_session_for_leader_info (CkManager             *manager,
                              LeaderInfo            *leader_info,
                              const GPtrArray       *parameters,
                              DBusGMethodInvocation *context)
{
        CkSession   *session;
        CkSeat      *seat;

        session = ck_session_new_with_parameters (leader_info->ssid,
                                                  leader_info->cookie,
                                                  parameters);

        if (session == NULL) {
                GError *error;
                g_debug ("Unable to create new session");
                error = g_error_new (CK_MANAGER_ERROR,
                                     CK_MANAGER_ERROR_GENERAL,
                                     "Unable to create new session");
                dbus_g_method_return_error (context, error);
                g_error_free (error);

                return;
        }

        g_hash_table_insert (manager->priv->sessions, g_strdup (leader_info->ssid), g_object_ref (session));

        /* Add to seat */
        seat = find_seat_for_session (manager, session);
        if (seat == NULL) {
                /* create a new seat */
                seat = add_new_seat (manager, CK_SEAT_KIND_DYNAMIC);
        }

        ck_seat_add_session (seat, session, NULL);

        /* FIXME: connect to signals */
        /* FIXME: add weak ref */

        manager_update_system_idle_hint (manager);
        g_signal_connect (session, "idle-hint-changed",
                          G_CALLBACK (session_idle_hint_changed),
                          manager);

        g_object_unref (session);

        dbus_g_method_return (context, leader_info->cookie);
}

static void
verify_and_open_session_for_leader_info (CkManager             *manager,
                                         LeaderInfo            *leader_info,
                                         const GPtrArray       *parameters,
                                         DBusGMethodInvocation *context)
{
        /* for now don't bother verifying since we protect OpenSessionWithParameters */
        open_session_for_leader_info (manager,
                                      leader_info,
                                      parameters,
                                      context);
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

typedef void (* CkAddParamFunc) (GPtrArray  *arr,
                                 const char *key,
                                 const char *value);

static struct {
        char          *key;
        CkAddParamFunc func;
} parse_ops[] = {
        { "display-device",     add_param_string },
        { "x11-display-device", add_param_string },
        { "x11-display",        add_param_string },
        { "remote-host-name",   add_param_string },
        { "session-type",       add_param_string },
        { "is-local",           add_param_boolean },
        { "unix-user",          add_param_int },
};

static GPtrArray *
parse_output (const char *output)
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

        for (i = 0; lines[i] != NULL; i++) {
                char **vals;

                vals = g_strsplit (lines[i], " = ", 2);
                if (vals == NULL || vals[0] == NULL) {
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

        return parameters;
}

typedef struct {
        CkManager             *manager;
        LeaderInfo            *leader_info;
        DBusGMethodInvocation *context;
} JobData;

static void
job_data_free (JobData *data)
{
        leader_info_unref (data->leader_info);
        g_free (data);
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

                parameters = parse_output (output);
                g_free (output);

                verify_and_open_session_for_leader_info (data->manager,
                                                         data->leader_info,
                                                         parameters,
                                                         data->context);
                parameters_free (parameters);
        }

        /* remove job from queue */
        data->leader_info->pending_jobs = g_list_remove (data->leader_info->pending_jobs, job);

        g_signal_handlers_disconnect_by_func (job, job_completed, data);
        g_object_unref (job);
}

static void
generate_session_for_leader_info (CkManager             *manager,
                                  LeaderInfo            *leader_info,
                                  DBusGMethodInvocation *context)
{
        GError      *local_error;
        char        *command;
        gboolean     res;
        CkJob       *job;
        JobData     *data;

        command = g_strdup_printf ("%s --uid %u --pid %u",
                                   LIBEXECDIR "/ck-collect-session-info",
                                   leader_info->uid,
                                   leader_info->pid);
        job = ck_job_new ();
        ck_job_set_command (job, command);
        g_free (command);

        data = g_new0 (JobData, 1);
        data->manager = manager;
        data->leader_info = leader_info_ref (leader_info);
        data->context = context;
        g_signal_connect_data (job,
                               "completed",
                               G_CALLBACK (job_completed),
                               data,
                               (GClosureNotify)job_data_free,
                               0);

        local_error = NULL;
        res = ck_job_execute (job, &local_error);
        if (! res) {
                GError *error;
                error = g_error_new (CK_MANAGER_ERROR,
                                     CK_MANAGER_ERROR_GENERAL,
                                     "Unable to get information about the calling process");
                dbus_g_method_return_error (context, error);
                g_error_free (error);

                if (local_error != NULL) {
                        g_debug ("stat on pid %d failed: %s", leader_info->pid, local_error->message);
                        g_error_free (local_error);
                }

                g_object_unref (job);

                return;
        }

        /* Add job to queue */
        leader_info->pending_jobs = g_list_prepend (leader_info->pending_jobs, job);
}

static gboolean
create_session_for_sender (CkManager             *manager,
                           const char            *sender,
                           const GPtrArray       *parameters,
                           DBusGMethodInvocation *context)
{
        pid_t        pid;
        uid_t        uid;
        gboolean     res;
        char        *cookie;
        char        *ssid;
        LeaderInfo  *leader_info;

        res = get_caller_info (manager,
                               sender,
                               &uid,
                               &pid);
        if (! res) {
                GError *error;
                error = g_error_new (CK_MANAGER_ERROR,
                                     CK_MANAGER_ERROR_GENERAL,
                                     "Unable to get information about the calling process");
                dbus_g_method_return_error (context, error);
                g_error_free (error);
                return FALSE;
        }

        cookie = generate_session_cookie (manager);
        ssid = generate_session_id (manager);

        g_debug ("Creating new session ssid: %s", ssid);

        leader_info = g_new0 (LeaderInfo, 1);
        leader_info->uid = uid;
        leader_info->pid = pid;
        leader_info->service_name = g_strdup (sender);
        leader_info->ssid = g_strdup (ssid);
        leader_info->cookie = g_strdup (cookie);

        /* need to store the leader info first so the pending request can be revoked */
        g_hash_table_insert (manager->priv->leaders,
                             g_strdup (leader_info->cookie),
                             leader_info_ref (leader_info));

        if (parameters == NULL) {
                generate_session_for_leader_info (manager,
                                                  leader_info,
                                                  context);
        } else {
                verify_and_open_session_for_leader_info (manager,
                                                         leader_info,
                                                         parameters,
                                                         context);
        }

        g_free (cookie);
        g_free (ssid);

        return TRUE;
}

/*
  Example:
  dbus-send --system --dest=org.freedesktop.ConsoleKit \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/freedesktop/ConsoleKit/Manager \
  org.freedesktop.ConsoleKit.Manager.GetSessionForCookie string:$XDG_SESSION_COOKIE
*/
gboolean
ck_manager_get_session_for_cookie (CkManager             *manager,
                                   const char            *cookie,
                                   DBusGMethodInvocation *context)
{
        gboolean       res;
        char          *sender;
        uid_t          calling_uid;
        pid_t          calling_pid;
        CkProcessStat *stat;
        char          *ssid;
        CkSession     *session;
        LeaderInfo    *leader_info;
        GError        *local_error;

        ssid = NULL;

        sender = dbus_g_method_get_sender (context);

        res = get_caller_info (manager,
                               sender,
                               &calling_uid,
                               &calling_pid);
        g_free (sender);

        if (! res) {
                GError *error;
                error = g_error_new (CK_MANAGER_ERROR,
                                     CK_MANAGER_ERROR_GENERAL,
                                     _("Unable to get information about the calling process"));
                dbus_g_method_return_error (context, error);
                g_error_free (error);
                return FALSE;
        }

        local_error = NULL;
        res = ck_process_stat_new_for_unix_pid (calling_pid, &stat, &local_error);
        if (! res) {
                GError *error;
                error = g_error_new (CK_MANAGER_ERROR,
                                     CK_MANAGER_ERROR_GENERAL,
                                     _("Unable to lookup information about calling process '%d'"),
                                     calling_pid);
                if (local_error != NULL) {
                        g_debug ("stat on pid %d failed: %s", calling_pid, local_error->message);
                        g_error_free (local_error);
                }

                dbus_g_method_return_error (context, error);
                g_error_free (error);
                return FALSE;
        }

        /* FIXME: should we restrict this by uid? */
        ck_process_stat_free (stat);

        leader_info = g_hash_table_lookup (manager->priv->leaders, cookie);
        if (leader_info == NULL) {
                GError *error;
                error = g_error_new (CK_MANAGER_ERROR,
                                     CK_MANAGER_ERROR_GENERAL,
                                     _("Unable to find session for cookie"));
                dbus_g_method_return_error (context, error);
                g_error_free (error);
                return FALSE;
        }

        session = g_hash_table_lookup (manager->priv->sessions, leader_info->ssid);
        if (session == NULL) {
                GError *error;
                error = g_error_new (CK_MANAGER_ERROR,
                                     CK_MANAGER_ERROR_GENERAL,
                                     _("Unable to find session for cookie"));
                dbus_g_method_return_error (context, error);
                g_error_free (error);
                return FALSE;
        }

        ck_session_get_id (session, &ssid, NULL);

        dbus_g_method_return (context, ssid);

        g_free (ssid);

        return TRUE;
}

static char *
get_cookie_for_pid (CkManager *manager,
                    guint      pid)
{
        char *cookie;

        /* FIXME: need a better way to get the cookie */

        cookie = ck_unix_pid_get_env (pid, "XDG_SESSION_COOKIE");

        return cookie;
}

/*
  Example:
  dbus-send --system --dest=org.freedesktop.ConsoleKit \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/freedesktop/ConsoleKit/Manager \
  org.freedesktop.ConsoleKit.Manager.GetSessionForUnixProcess uint32:`/sbin/pidof -s bash`
*/
gboolean
ck_manager_get_session_for_unix_process (CkManager             *manager,
                                         guint                  pid,
                                         DBusGMethodInvocation *context)
{
        gboolean       res;
        char          *sender;
        uid_t          calling_uid;
        pid_t          calling_pid;
        CkProcessStat *stat;
        char          *cookie;
        GError        *error;

        sender = dbus_g_method_get_sender (context);

        res = get_caller_info (manager,
                               sender,
                               &calling_uid,
                               &calling_pid);
        g_free (sender);

        if (! res) {
                GError *error;
                error = g_error_new (CK_MANAGER_ERROR,
                                     CK_MANAGER_ERROR_GENERAL,
                                     _("Unable to get information about the calling process"));
                dbus_g_method_return_error (context, error);
                g_error_free (error);
                return FALSE;
        }

        error = NULL;
        res = ck_process_stat_new_for_unix_pid (calling_pid, &stat, &error);
        if (! res) {
                GError *error;
                g_debug ("stat on pid %d failed", calling_pid);
                error = g_error_new (CK_MANAGER_ERROR,
                                     CK_MANAGER_ERROR_GENERAL,
                                     _("Unable to lookup information about calling process '%d'"),
                                     calling_pid);
                dbus_g_method_return_error (context, error);
                g_error_free (error);
                return FALSE;
        }

        /* FIXME: check stuff? */

        ck_process_stat_free (stat);

        cookie = get_cookie_for_pid (manager, pid);
        if (cookie == NULL) {
                GError *error;
                error = g_error_new (CK_MANAGER_ERROR,
                                     CK_MANAGER_ERROR_GENERAL,
                                     _("Unable to lookup session information for process '%d'"),
                                     pid);
                dbus_g_method_return_error (context, error);
                g_error_free (error);
                return FALSE;
        }

        res = ck_manager_get_session_for_cookie (manager, cookie, context);
        g_free (cookie);

        return res;
}

/*
  Example:
  dbus-send --system --dest=org.freedesktop.ConsoleKit \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/freedesktop/ConsoleKit/Manager \
  org.freedesktop.ConsoleKit.Manager.GetCurrentSession
*/
gboolean
ck_manager_get_current_session (CkManager             *manager,
                                DBusGMethodInvocation *context)
{
        gboolean    res;
        char       *sender;
        uid_t       calling_uid;
        pid_t       calling_pid;

        sender = dbus_g_method_get_sender (context);

        res = get_caller_info (manager,
                               sender,
                               &calling_uid,
                               &calling_pid);
        g_free (sender);

        if (! res) {
                GError *error;
                error = g_error_new (CK_MANAGER_ERROR,
                                     CK_MANAGER_ERROR_GENERAL,
                                     _("Unable to get information about the calling process"));
                dbus_g_method_return_error (context, error);
                g_error_free (error);
                return FALSE;
        }

        res = ck_manager_get_session_for_unix_process (manager, calling_pid, context);

        return res;
}

gboolean
ck_manager_open_session (CkManager             *manager,
                         DBusGMethodInvocation *context)
{
        char    *sender;
        gboolean ret;

        sender = dbus_g_method_get_sender (context);
        ret = create_session_for_sender (manager, sender, NULL, context);
        g_free (sender);

        return ret;
}

gboolean
ck_manager_open_session_with_parameters (CkManager             *manager,
                                         const GPtrArray       *parameters,
                                         DBusGMethodInvocation *context)
{
        char    *sender;
        gboolean ret;

        sender = dbus_g_method_get_sender (context);
        ret = create_session_for_sender (manager, sender, parameters, context);
        g_free (sender);

        return ret;
}

static gboolean
remove_session_for_cookie (CkManager  *manager,
                           const char *cookie,
                           GError    **error)
{
        CkSession  *session;
        LeaderInfo *leader_info;
        char       *ssid;
        char       *sid;

        g_debug ("Removing session for cookie: %s", cookie);

        leader_info = g_hash_table_lookup (manager->priv->leaders, cookie);

        if (leader_info == NULL) {
                g_set_error (error,
                             CK_MANAGER_ERROR,
                             CK_MANAGER_ERROR_GENERAL,
                             "Unable to find session for cookie");
                return FALSE;
        }

        session = g_hash_table_lookup (manager->priv->sessions, leader_info->ssid);
        if (session == NULL) {
                g_set_error (error,
                             CK_MANAGER_ERROR,
                             CK_MANAGER_ERROR_GENERAL,
                             "Unable to find session for cookie");
                return FALSE;
        }

        ssid = g_strdup (leader_info->ssid);

        /* remove from seat */
        ck_session_get_seat_id (session, &sid, NULL);
        if (sid != NULL) {
                CkSeat *seat;
                seat = g_hash_table_lookup (manager->priv->seats, sid);
                if (seat != NULL) {
                        CkSeatKind kind;

                        ck_seat_remove_session (seat, session, NULL);

                        kind = CK_SEAT_KIND_STATIC;
                        /* if dynamic seat has no sessions then remove it */
                        ck_seat_get_kind (seat, &kind, NULL);
                        if (kind == CK_SEAT_KIND_DYNAMIC) {
                                remove_seat (manager, seat);
                        }
                }
        }

        g_hash_table_remove (manager->priv->sessions, ssid);

        g_free (sid);
        g_free (ssid);

        manager_update_system_idle_hint (manager);

        return TRUE;
}

static gboolean
paranoia_check_is_cookie_owner (CkManager  *manager,
                                const char *cookie,
                                uid_t       calling_uid,
                                pid_t       calling_pid,
                                GError    **error)
{
        LeaderInfo *leader_info;

        if (cookie == NULL) {
                g_set_error (error,
                             CK_MANAGER_ERROR,
                             CK_MANAGER_ERROR_GENERAL,
                             "No cookie specified");
                return FALSE;
        }

        leader_info = g_hash_table_lookup (manager->priv->leaders, cookie);
        if (leader_info == NULL) {
                g_set_error (error,
                             CK_MANAGER_ERROR,
                             CK_MANAGER_ERROR_GENERAL,
                             _("Unable to find session for cookie"));
                return FALSE;
        }

        if (leader_info->uid != calling_uid) {
                g_set_error (error,
                             CK_MANAGER_ERROR,
                             CK_MANAGER_ERROR_GENERAL,
                             _("User ID does not match the owner of cookie"));
                return FALSE;

        }

        /* do we want to restrict to the same process? */
        if (leader_info->pid != calling_pid) {
                g_set_error (error,
                             CK_MANAGER_ERROR,
                             CK_MANAGER_ERROR_GENERAL,
                             _("Process ID does not match the owner of cookie"));
                return FALSE;

        }

        return TRUE;
}

gboolean
ck_manager_close_session (CkManager             *manager,
                          const char            *cookie,
                          DBusGMethodInvocation *context)
{
        gboolean res;
        char    *sender;
        uid_t    calling_uid;
        pid_t    calling_pid;
        GError  *error;

        g_debug ("Closing session for cookie: %s", cookie);

        sender = dbus_g_method_get_sender (context);
        res = get_caller_info (manager,
                               sender,
                               &calling_uid,
                               &calling_pid);
        g_free (sender);

        if (! res) {
                error = g_error_new (CK_MANAGER_ERROR,
                                     CK_MANAGER_ERROR_GENERAL,
                                     "Unable to get information about the calling process");
                dbus_g_method_return_error (context, error);
                g_error_free (error);

                return FALSE;
        }

        error = NULL;
        res = paranoia_check_is_cookie_owner (manager, cookie, calling_uid, calling_pid, &error);
        if (! res) {
                dbus_g_method_return_error (context, error);
                g_error_free (error);

                return FALSE;
        }

        error = NULL;
        res = remove_session_for_cookie (manager, cookie, &error);
        if (! res) {
                dbus_g_method_return_error (context, error);
                g_error_free (error);
                return FALSE;
        } else {
                g_hash_table_remove (manager->priv->leaders, cookie);
        }

        dbus_g_method_return (context, res);

        return TRUE;
}

typedef struct {
        const char *service_name;
        CkManager  *manager;
} RemoveLeaderData;

static gboolean
remove_leader_for_connection (const char       *cookie,
                              LeaderInfo       *info,
                              RemoveLeaderData *data)
{
        g_assert (info != NULL);
        g_assert (data->service_name != NULL);

        if (strcmp (info->service_name, data->service_name) == 0) {
                remove_session_for_cookie (data->manager, cookie, NULL);
                leader_info_cancel (info);
                return TRUE;
        }

        return FALSE;
}

static void
remove_sessions_for_connection (CkManager  *manager,
                                const char *service_name)
{
        guint            n_removed;
        RemoveLeaderData data;

        data.service_name = service_name;
        data.manager = manager;

        g_debug ("Removing sessions for service name: %s", service_name);

        n_removed = g_hash_table_foreach_remove (manager->priv->leaders,
                                                 (GHRFunc)remove_leader_for_connection,
                                                 &data);

}

static void
bus_name_owner_changed (DBusGProxy  *bus_proxy,
                        const char  *service_name,
                        const char  *old_service_name,
                        const char  *new_service_name,
                        CkManager   *manager)
{
        if (strlen (new_service_name) == 0) {
                remove_sessions_for_connection (manager, old_service_name);
        }

        g_debug ("NameOwnerChanged: service_name='%s', old_service_name='%s' new_service_name='%s'",
                   service_name, old_service_name, new_service_name);
}

static gboolean
register_manager (CkManager *manager)
{
        GError *error = NULL;

        error = NULL;
        manager->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (manager->priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting system bus: %s", error->message);
                        g_error_free (error);
                }
                exit (1);
        }

        manager->priv->bus_proxy = dbus_g_proxy_new_for_name (manager->priv->connection,
                                                              DBUS_SERVICE_DBUS,
                                                              DBUS_PATH_DBUS,
                                                              DBUS_INTERFACE_DBUS);
        dbus_g_proxy_add_signal (manager->priv->bus_proxy,
                                 "NameOwnerChanged",
                                 G_TYPE_STRING,
                                 G_TYPE_STRING,
                                 G_TYPE_STRING,
                                 G_TYPE_INVALID);
        dbus_g_proxy_connect_signal (manager->priv->bus_proxy,
                                     "NameOwnerChanged",
                                     G_CALLBACK (bus_name_owner_changed),
                                     manager,
                                     NULL);

        dbus_g_connection_register_g_object (manager->priv->connection, CK_MANAGER_DBUS_PATH, G_OBJECT (manager));

        return TRUE;
}

static void
ck_manager_class_init (CkManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = ck_manager_finalize;

        signals [SEAT_ADDED] =
                g_signal_new ("seat-added",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CkManagerClass, seat_added),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [SEAT_REMOVED] =
                g_signal_new ("seat-removed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CkManagerClass, seat_removed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__STRING,
                              G_TYPE_NONE,
                              1, G_TYPE_STRING);
        signals [SYSTEM_IDLE_HINT_CHANGED] =
                g_signal_new ("system-idle-hint-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CkManagerClass, system_idle_hint_changed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__BOOLEAN,
                              G_TYPE_NONE,
                              1, G_TYPE_BOOLEAN);

        dbus_g_object_type_install_info (CK_TYPE_MANAGER, &dbus_glib_ck_manager_object_info);

        g_type_class_add_private (klass, sizeof (CkManagerPrivate));
}

typedef struct {
        guint                  uid;
        GPtrArray             *sessions;
} GetSessionsData;

static void
get_sessions_for_unix_user_iter (char            *id,
                                 CkSession       *session,
                                 GetSessionsData *data)
{
        guint    uid;
        gboolean res;

        res = ck_session_get_unix_user (session, &uid, NULL);

        if (res && uid == data->uid) {
                g_ptr_array_add (data->sessions, g_strdup (id));
        }
}

gboolean
ck_manager_get_sessions_for_unix_user (CkManager             *manager,
                                       guint                  uid,
                                       DBusGMethodInvocation *context)
{
        GetSessionsData *data;

        g_return_val_if_fail (CK_IS_MANAGER (manager), FALSE);

        data = g_new0 (GetSessionsData, 1);
        data->uid = uid;
        data->sessions = g_ptr_array_new ();

        g_hash_table_foreach (manager->priv->sessions, (GHFunc)get_sessions_for_unix_user_iter, data);

        dbus_g_method_return (context, data->sessions);

        g_ptr_array_foreach (data->sessions, (GFunc)g_free, NULL);
        g_ptr_array_free (data->sessions, TRUE);
        g_free (data);

        return TRUE;
}

/* This is deprecated */
gboolean
ck_manager_get_sessions_for_user (CkManager             *manager,
                                  guint                  uid,
                                  DBusGMethodInvocation *context)
{
        return ck_manager_get_sessions_for_unix_user (manager, uid, context);
}

static void
listify_seat_ids (char       *id,
                  CkSeat     *seat,
                  GPtrArray **array)
{
        g_ptr_array_add (*array, g_strdup (id));
}

gboolean
ck_manager_get_seats (CkManager  *manager,
                      GPtrArray **seats,
                      GError    **error)
{
        g_return_val_if_fail (CK_IS_MANAGER (manager), FALSE);

        if (seats == NULL) {
                return FALSE;
        }

        *seats = g_ptr_array_new ();
        g_hash_table_foreach (manager->priv->seats, (GHFunc)listify_seat_ids, seats);

        return TRUE;
}

static void
add_seat_for_file (CkManager  *manager,
                   const char *filename)
{
        char   *sid;
        CkSeat *seat;

        sid = generate_seat_id (manager);

        seat = ck_seat_new_from_file (sid, filename);
        if (seat == NULL) {
                /* returns null if connection to bus fails */
                g_free (sid);
                return;
        }

        g_hash_table_insert (manager->priv->seats, sid, seat);

        g_debug ("Added seat: %s", sid);

        g_signal_emit (manager, signals [SEAT_ADDED], 0, sid);
}

static gboolean
load_seats_from_dir (CkManager *manager)
{
        GDir       *d;
        GError     *error;
        const char *file;

        error = NULL;
        d = g_dir_open (CK_SEAT_DIR,
                        0,
                        &error);
        if (d == NULL) {
                g_warning ("Couldn't open seat dir: %s", error->message);
                g_error_free (error);
                return FALSE;
        }

        while ((file = g_dir_read_name (d)) != NULL) {
                char *path;
                path = g_build_filename (CK_SEAT_DIR, file, NULL);
                add_seat_for_file (manager, path);
                g_free (path);
        }

        g_dir_close (d);

        return TRUE;
}

static void
create_seats (CkManager *manager)
{
        load_seats_from_dir (manager);
}

static void
ck_manager_init (CkManager *manager)
{

        manager->priv = CK_MANAGER_GET_PRIVATE (manager);

        /* reserve zero */
        manager->priv->session_serial = 1;
        manager->priv->seat_serial = 1;

        manager->priv->system_idle_hint = TRUE;

        manager->priv->seats = g_hash_table_new_full (g_str_hash,
                                                      g_str_equal,
                                                      g_free,
                                                      (GDestroyNotify) g_object_unref);
        manager->priv->sessions = g_hash_table_new_full (g_str_hash,
                                                         g_str_equal,
                                                         g_free,
                                                         (GDestroyNotify) g_object_unref);
        manager->priv->leaders = g_hash_table_new_full (g_str_hash,
                                                        g_str_equal,
                                                        g_free,
                                                        (GDestroyNotify) leader_info_unref);

        create_seats (manager);
}

static void
ck_manager_finalize (GObject *object)
{
        CkManager *manager;

        g_return_if_fail (object != NULL);
        g_return_if_fail (CK_IS_MANAGER (object));

        manager = CK_MANAGER (object);

        g_return_if_fail (manager->priv != NULL);

        g_hash_table_destroy (manager->priv->seats);
        g_hash_table_destroy (manager->priv->sessions);
        g_hash_table_destroy (manager->priv->leaders);
        g_object_unref (manager->priv->bus_proxy);

        G_OBJECT_CLASS (ck_manager_parent_class)->finalize (object);
}

CkManager *
ck_manager_new (void)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                gboolean res;

                manager_object = g_object_new (CK_TYPE_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
                res = register_manager (manager_object);
                if (! res) {
                        g_object_unref (manager_object);
                        return NULL;
                }
        }

        return CK_MANAGER (manager_object);
}
