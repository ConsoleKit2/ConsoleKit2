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
#include "ck-marshal.h"

#include "ck-debug.h"
#include "proc.h"

#define CK_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CK_TYPE_MANAGER, CkManagerPrivate))

#define CK_DBUS_PATH         "/org/freedesktop/ConsoleKit"
#define CK_MANAGER_DBUS_PATH CK_DBUS_PATH "/Manager"
#define CK_MANAGER_DBUS_NAME "org.freedesktop.ConsoleKit.Manager"

struct CkManagerPrivate
{
        GHashTable      *seats;
        GHashTable      *sessions;
        GHashTable      *leaders;

        DBusGProxy      *bus_proxy;
        DBusGConnection *connection;

        guint32          session_serial;
        guint32          seat_serial;
};


typedef struct {
        uid_t  uid;
        pid_t  pid;
        char  *service_name;
        char  *ssid;
        char  *cookie;
} LeaderInfo;

enum {
        SEAT_ADDED,
        SEAT_REMOVED,
        LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     ck_manager_class_init  (CkManagerClass *klass);
static void     ck_manager_init        (CkManager      *manager);
static void     ck_manager_finalize    (GObject        *object);

static gpointer manager_object = NULL;

G_DEFINE_TYPE (CkManager, ck_manager, G_TYPE_OBJECT)

static void
leader_info_free (LeaderInfo *info)
{
        g_free (info->ssid);
        g_free (info->cookie);
        g_free (info->service_name);
        g_free (info);
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

        /* We want this to be globally unique
           or at least such that it won't cycle when there
           may be orphan processes in a dead session. */

        cookie = NULL;
 again:
        num = (guint32)g_random_int_range (1, G_MAXINT32);

        g_get_current_time (&tv);

        g_free (cookie);
        cookie = g_strdup_printf ("%ld.%ld-%u", tv.tv_sec, tv.tv_usec, num);

        if (g_hash_table_lookup (manager->priv->leaders, cookie)) {
                goto again;
        }

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

        ck_debug ("Added seat: %s kind:%d", sid, kind);

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

        ck_debug ("Removed seat: %s", sid);

        g_free (sid);
}

static CkSeat *
find_seat_for_session (CkManager *manager,
                       CkSession *session)
{
        CkSeat  *seat;
        gboolean is_static;
        gboolean is_local;

        seat = NULL;
        is_local = TRUE;

        /* FIXME: use matching to group entries? */

        /* for now group all local entries */
        ck_session_is_local (session, &is_local, NULL);
        is_static = is_local;

        if (is_static) {
                char *sid;
                sid = g_strdup_printf ("%s/Seat%u", CK_DBUS_PATH, 1);
                seat = g_hash_table_lookup (manager->priv->seats, sid);
                g_free (sid);
        }

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
                g_warning ("GetConnectionUnixUser() failed: %s", error->message);
                g_error_free (error);
                goto out;
        }

        if (! dbus_g_proxy_call (manager->priv->bus_proxy, "GetConnectionUnixProcessID", &error,
                                 G_TYPE_STRING, sender,
                                 G_TYPE_INVALID,
                                 G_TYPE_UINT, calling_pid,
                                 G_TYPE_INVALID)) {
                g_warning ("GetConnectionUnixProcessID() failed: %s", error->message);
                g_error_free (error);
                goto out;
        }

        res = TRUE;

        ck_debug ("uid = %d", *calling_uid);
        ck_debug ("pid = %d", *calling_pid);

out:
        return res;
}

static char *
create_session_for_caller (CkManager       *manager,
                           const char      *sender,
                           const GPtrArray *parameters,
                           GError         **error)
{
        char       *ssid;
        proc_t     *stat;
        char       *cmd;
        char       *xdisplay;
        char       *tty;
        CkSession  *session;
        CkSeat     *seat;
        char       *cookie;
        pid_t       pid;
        uid_t       uid;
        gboolean    res;
        LeaderInfo *leader_info;

        res = get_caller_info (manager,
                               sender,
                               &uid,
                               &pid);
        if (! res) {
                g_set_error (error,
                             CK_MANAGER_ERROR,
                             CK_MANAGER_ERROR_GENERAL,
                             "Unable to get information about the calling process");

                return NULL;
        }

        cookie = generate_session_cookie (manager);
        ssid = generate_session_id (manager);

        ck_debug ("Creating new session ssid: %s", ssid);

        session = ck_session_new_with_parameters (ssid,
                                                  cookie,
                                                  parameters);

        if (session == NULL) {
                g_warning ("Unable to create new session");
                g_free (cookie);
                cookie = NULL;
                g_set_error (error,
                             CK_MANAGER_ERROR,
                             CK_MANAGER_ERROR_GENERAL,
                             "Unable to create new session");
                goto out;
        }

        proc_stat_pid (pid, &stat);
        tty = proc_get_tty (stat);
        cmd = proc_get_cmd (stat);
        xdisplay = NULL;
        proc_free (stat);

        /* If the parameters are not set then try to get them */
        if (parameters == NULL) {
                ck_session_set_user (session, uid, NULL);
                ck_session_set_session_type (session, cmd, NULL);
                ck_session_set_display_device (session, tty, NULL);
                ck_session_set_x11_display (session, xdisplay, NULL);
        }

        g_free (xdisplay);
        g_free (cmd);
        g_free (tty);

        leader_info = g_new0 (LeaderInfo, 1);
        leader_info->uid = uid;
        leader_info->pid = pid;
        leader_info->service_name = g_strdup (sender);
        leader_info->ssid = g_strdup (ssid);
        leader_info->cookie = g_strdup (cookie);

        g_hash_table_insert (manager->priv->leaders, g_strdup (cookie), leader_info);
        g_hash_table_insert (manager->priv->sessions, g_strdup (ssid), g_object_ref (session));

        /* Add to seat */
        seat = find_seat_for_session (manager, session);
        if (seat == NULL) {
                /* create a new seat */
                seat = add_new_seat (manager, CK_SEAT_KIND_DYNAMIC);
        }

        ck_seat_add_session (seat, session, NULL);

        /* FIXME: connect to signals */
        /* FIXME: add weak ref */

        g_object_unref (session);

 out:

        g_free (ssid);

        return cookie;
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
        gboolean    res;
        char       *sender;
        uid_t       calling_uid;
        pid_t       calling_pid;
        proc_t     *stat;
        char       *ssid;
        CkSession  *session;
        LeaderInfo *leader_info;

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

        res = proc_stat_pid (calling_pid, &stat);
        if (! res) {
                GError *error;
                error = g_error_new (CK_MANAGER_ERROR,
                                     CK_MANAGER_ERROR_GENERAL,
                                     _("Unable to lookup information about calling process '%d'"),
                                     calling_pid);
                g_warning ("stat on pid %d failed", calling_pid);
                dbus_g_method_return_error (context, error);
                g_error_free (error);
                return FALSE;
        }

        /* FIXME: should we restrict this by uid? */

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

        cookie = proc_get_env (pid, "XDG_SESSION_COOKIE");

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
        gboolean    res;
        char       *sender;
        uid_t       calling_uid;
        pid_t       calling_pid;
        proc_t     *stat;
        char       *cookie;

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

        res = proc_stat_pid (calling_pid, &stat);
        if (! res) {
                GError *error;
                g_warning ("stat on pid %d failed", calling_pid);
                error = g_error_new (CK_MANAGER_ERROR,
                                     CK_MANAGER_ERROR_GENERAL,
                                     _("Unable to lookup information about calling process '%d'"),
                                     calling_pid);
                dbus_g_method_return_error (context, error);
                g_error_free (error);
                return FALSE;
        }

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
        char    *cookie;
        GError  *error;

        sender = dbus_g_method_get_sender (context);

        error = NULL;
        cookie = create_session_for_caller (manager, sender, NULL, &error);
        if (cookie == NULL) {
                dbus_g_method_return_error (context, error);
                g_error_free (error);
                return FALSE;
        }

        dbus_g_method_return (context, cookie);

        g_free (cookie);

        return TRUE;
}

gboolean
ck_manager_open_session_with_parameters (CkManager             *manager,
                                         const GPtrArray       *parameters,
                                         DBusGMethodInvocation *context)
{
        char    *sender;
        char    *cookie;
        GError  *error;

        sender = dbus_g_method_get_sender (context);

        error = NULL;
        cookie = create_session_for_caller (manager, sender, parameters, &error);
        g_free (sender);

        if (cookie == NULL) {
                dbus_g_method_return_error (context, error);
                g_error_free (error);
                return FALSE;
        }

        dbus_g_method_return (context, cookie);

        g_free (cookie);

        return TRUE;
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

        ck_debug ("Removing session for cookie: %s", cookie);

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

        g_free (ssid);

        return TRUE;
}

gboolean
ck_manager_close_session (CkManager             *manager,
                          const char            *cookie,
                          DBusGMethodInvocation *context)
{
        gboolean res;
        GError  *error;

        ck_debug ("Closing session for cookie: %s", cookie);

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

        ck_debug ("Removing sessions for service name: %s", service_name);

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

        ck_debug ("NameOwnerChanged: service_name='%s', old_service_name='%s' new_service_name='%s'",
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

        signals [SEAT_ADDED] = g_signal_new ("seat-added",
                                             G_TYPE_FROM_CLASS (object_class),
                                             G_SIGNAL_RUN_LAST,
                                             G_STRUCT_OFFSET (CkManagerClass, seat_added),
                                             NULL,
                                             NULL,
                                             g_cclosure_marshal_VOID__STRING,
                                             G_TYPE_NONE,
                                             1, G_TYPE_STRING);
        signals [SEAT_REMOVED] = g_signal_new ("seat-removed",
                                             G_TYPE_FROM_CLASS (object_class),
                                             G_SIGNAL_RUN_LAST,
                                             G_STRUCT_OFFSET (CkManagerClass, seat_removed),
                                             NULL,
                                             NULL,
                                             g_cclosure_marshal_VOID__STRING,
                                             G_TYPE_NONE,
                                             1, G_TYPE_STRING);

        dbus_g_object_type_install_info (CK_TYPE_MANAGER, &dbus_glib_ck_manager_object_info);

        g_type_class_add_private (klass, sizeof (CkManagerPrivate));
}

typedef struct {
        guint                  uid;
        GPtrArray             *sessions;
} GetSessionsData;

static void
get_sessions_for_user_iter (char            *id,
                            CkSession       *session,
                            GetSessionsData *data)
{
        guint    uid;
        gboolean res;

        res = ck_session_get_user (session, &uid, NULL);

        if (res && uid == data->uid) {
                g_ptr_array_add (data->sessions, g_strdup (id));
        }
}

gboolean
ck_manager_get_sessions_for_user (CkManager             *manager,
                                  guint                  uid,
                                  DBusGMethodInvocation *context)
{
        GetSessionsData *data;

        g_return_val_if_fail (CK_IS_MANAGER (manager), FALSE);

        data = g_new0 (GetSessionsData, 1);
        data->uid = uid;
        data->sessions = g_ptr_array_new ();

        g_hash_table_foreach (manager->priv->sessions, (GHFunc)get_sessions_for_user_iter, data);

        dbus_g_method_return (context, data->sessions);

        g_ptr_array_foreach (data->sessions, (GFunc)g_free, NULL);
        g_ptr_array_free (data->sessions, TRUE);
        g_free (data);

        return TRUE;
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
create_seats (CkManager *manager)
{
        CkSeat *seat;

        seat = add_new_seat (manager, CK_SEAT_KIND_STATIC);
}

static void
ck_manager_init (CkManager *manager)
{

        manager->priv = CK_MANAGER_GET_PRIVATE (manager);

        /* reserve zero */
        manager->priv->session_serial = 1;
        manager->priv->seat_serial = 1;

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
                                                        (GDestroyNotify) leader_info_free);

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
