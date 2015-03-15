/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006-2008 William Jon McCann <mccann@jhu.edu>
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
#include <errno.h>
#include <pwd.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>
#include <gio/gio.h>

#if defined HAVE_POLKIT
#include <polkit/polkit.h>
#elif defined ENABLE_RBAC_SHUTDOWN
#include <auth_attr.h>
#include <secdb.h>
#endif

#include "ck-manager.h"
#include "ck-manager-glue.h"
#include "ck-seat.h"
#include "ck-session-leader.h"
#include "ck-session.h"
#include "ck-session-generated.h"
#include "ck-marshal.h"
#include "ck-event-logger.h"
#include "ck-inhibit-manager.h"
#include "ck-sysdeps.h"

#define CK_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CK_TYPE_MANAGER, CkManagerPrivate))

#define CK_SEAT_DIR          SYSCONFDIR "/ConsoleKit/seats.d"
#define LOG_FILE             LOCALSTATEDIR "/log/ConsoleKit/history"
#define CK_DBUS_PATH         "/org/freedesktop/ConsoleKit"
#define CK_MANAGER_DBUS_PATH CK_DBUS_PATH "/Manager"
#define CK_MANAGER_DBUS_NAME "org.freedesktop.ConsoleKit.Manager"

struct CkManagerPrivate
{
#ifdef HAVE_POLKIT
        PolkitAuthority *pol_ctx;
#endif

        GHashTable      *seats;
        GHashTable      *sessions;
        GHashTable      *leaders;

        DBusGProxy      *bus_proxy;
        DBusGConnection *connection;
        CkEventLogger   *logger;

        guint32          session_serial;
        guint32          seat_serial;

        gboolean         system_idle_hint;
        GTimeVal         system_idle_since_hint;

        /* How long to delay after emitting the PREPARE_FOR_SHUTDOWN or
         * PREPARE_FOR_SLEEP signal */
        guint            system_action_idle_delay;
        /* The idle callback id so we can detect multiple attempts to
         * perform a system action at the same time */
        guint            system_action_idle_id;

        CkInhibitManager *inhibit_manager;
};

typedef enum {
        SEAT_ADDED,
        SEAT_REMOVED,
        SYSTEM_IDLE_HINT_CHANGED,
        PREPARE_FOR_SHUTDOWN,
        PREPARE_FOR_SLEEP,
        LAST_SIGNAL
} SIGNALS;

typedef struct
{
        CkManager             *manager;
        DBusGMethodInvocation *context;
        const gchar           *command;
        CkLogEventType         event_type;
        const gchar           *description;
        SIGNALS                signal;
} SystemActionData;

static guint signals [LAST_SIGNAL] = { 0, };

static void     ck_manager_class_init  (CkManagerClass *klass);
static void     ck_manager_init        (CkManager      *manager);
static void     ck_manager_finalize    (GObject        *object);

static gpointer manager_object = NULL;

G_DEFINE_TYPE (CkManager, ck_manager, G_TYPE_OBJECT)

static void
dump_state_seat_iter (char     *id,
                      CkSeat   *seat,
                      GKeyFile *key_file)
{
        ck_seat_dump (seat, key_file);
}

static void
dump_state_session_iter (char      *id,
                         CkSession *session,
                         GKeyFile  *key_file)
{
        ck_session_dump (session, key_file);
}

static void
dump_state_leader_iter (char            *id,
                        CkSessionLeader *leader,
                        GKeyFile        *key_file)
{
        ck_session_leader_dump (leader, key_file);
}

static gboolean
do_dump (CkManager *manager,
         int        fd)
{
        char     *str;
        gsize     str_len;
        GKeyFile *key_file;
        GError   *error;
        gboolean  ret;

        str = NULL;
        error = NULL;
        ret = FALSE;

        key_file = g_key_file_new ();

        g_hash_table_foreach (manager->priv->seats, (GHFunc) dump_state_seat_iter, key_file);
        g_hash_table_foreach (manager->priv->sessions, (GHFunc) dump_state_session_iter, key_file);
        g_hash_table_foreach (manager->priv->leaders, (GHFunc) dump_state_leader_iter, key_file);

        str = g_key_file_to_data (key_file, &str_len, &error);
        g_key_file_free (key_file);
        if (str != NULL) {
                ssize_t written;

                written = 0;
                while (written < str_len) {
                        ssize_t ret;
                        ret = write (fd, str + written, str_len - written);
                        if (ret < 0) {
                                if (errno == EAGAIN || errno == EINTR) {
                                        continue;
                                } else {
                                        g_warning ("Error writing state file: %s", strerror (errno));
                                        goto out;
                                }
                        }
                        written += ret;
                }
        } else {
                g_warning ("Couldn't construct state file: %s", error->message);
                g_error_free (error);
        }

        ret = TRUE;

out:
        g_free (str);
        return ret;
}

static void
ck_manager_dump (CkManager *manager)
{
        int         fd;
        int         res;
        const char *filename = RUNDIR "/ConsoleKit/database";
        const char *filename_tmp = RUNDIR "/ConsoleKit/database~";

        if (manager == NULL) {
                g_warning ("ck_manager_dump: manager == NULL");
                return;
        }

        /* always make sure we have a directory */
        errno = 0;
        res = g_mkdir_with_parents (RUNDIR "/ConsoleKit",
                                    S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
        if (res < 0) {
                g_warning ("Unable to create directory %s (%s)",
                           RUNDIR "/ConsoleKit",
                           g_strerror (errno));
                return;
        }

        g_debug ("ck_manager_dump: %s/ConsoleKit folder created", RUNDIR);

        fd = g_open (filename_tmp, O_CREAT | O_WRONLY, 0644);
        if (fd == -1) {
                g_warning ("Cannot create file %s: %s", filename_tmp, g_strerror (errno));
                goto error;
        }

        g_debug ("ck_manager_dump: %s created", filename_tmp);

        if (! do_dump (manager, fd)) {
                g_warning ("Cannot write to file %s", filename_tmp);
                close (fd);
                goto error;
        }

        g_debug ("ck_manager_dump: wrote database to %s", filename_tmp);

 again:
        if (close (fd) != 0) {
                if (errno == EINTR)
                        goto again;
                else {
                        g_warning ("Cannot close fd for %s: %s", filename_tmp, g_strerror (errno));
                        goto error;
                }
        }

        if (g_rename (filename_tmp, filename) != 0) {
                g_warning ("Cannot rename %s to %s: %s", filename_tmp, filename, g_strerror (errno));
                goto error;
        }

        g_debug ("ck_manager_dump: renamed %s to %s, operation successful", filename_tmp, filename);

        return;
error:
        /* For security reasons; unlink the existing file since it
           contains outdated information */
        if (g_unlink (filename) != 0) {
                g_warning ("Cannot unlink %s: %s", filename, g_strerror (errno));
        }
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

#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
ck_manager_error_get_type (void)
{
        static GType etype = 0;

        if (etype == 0) {
                static const GEnumValue values[] = {
                        ENUM_ENTRY (CK_MANAGER_ERROR_GENERAL, "GeneralError"),
                        ENUM_ENTRY (CK_MANAGER_ERROR_NOT_PRIVILEGED, "NotPrivileged"),
                        { 0, 0, 0 }
                };

                g_assert (CK_MANAGER_NUM_ERRORS == G_N_ELEMENTS (values) - 1);

                etype = g_enum_register_static ("CkManagerError", values);
        }

        return etype;
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

static const char *
get_object_id_basename (const char *id)
{
        const char *base;

        if (id != NULL && g_str_has_prefix (id, CK_DBUS_PATH "/")) {
                base = id + strlen (CK_DBUS_PATH "/");
        } else {
                base = id;
        }

        return base;
}

static void
log_seat_added_event (CkManager  *manager,
                      CkSeat     *seat)
{
        CkLogEvent         event;
        gboolean           res;
        GError            *error;
        char              *sid;
        CkSeatKind         seat_kind;

        memset (&event, 0, sizeof (CkLogEvent));

        event.type = CK_LOG_EVENT_SEAT_ADDED;
        g_get_current_time (&event.timestamp);

        sid = NULL;
        ck_seat_get_id (seat, &sid, NULL);
        ck_seat_get_kind (seat, &seat_kind, NULL);

        event.event.seat_added.seat_id = (char *)get_object_id_basename (sid);
        event.event.seat_added.seat_kind = (int)seat_kind;

        error = NULL;
        res = ck_event_logger_queue_event (manager->priv->logger, &event, &error);
        if (! res) {
                g_debug ("Unable to log event: %s", error->message);
                g_error_free (error);
        }

        g_free (sid);
}

static void
log_seat_removed_event (CkManager  *manager,
                        CkSeat     *seat)
{
        CkLogEvent         event;
        gboolean           res;
        GError            *error;
        char              *sid;
        CkSeatKind         seat_kind;

        memset (&event, 0, sizeof (CkLogEvent));

        event.type = CK_LOG_EVENT_SEAT_REMOVED;
        g_get_current_time (&event.timestamp);

        sid = NULL;
        ck_seat_get_id (seat, &sid, NULL);
        ck_seat_get_kind (seat, &seat_kind, NULL);

        event.event.seat_removed.seat_id = (char *)get_object_id_basename (sid);
        event.event.seat_removed.seat_kind = (int)seat_kind;

        error = NULL;
        res = ck_event_logger_queue_event (manager->priv->logger, &event, &error);
        if (! res) {
                g_debug ("Unable to log event: %s", error->message);
                g_error_free (error);
        }

        g_free (sid);
}

/* Generic logger for system actions such as CK_LOG_EVENT_SYSTEM_STOP,
 * restart, hibernate, and suspend */
static void
log_system_action_event (CkManager *manager,
                         CkLogEventType type)
{
        CkLogEvent         event;
        gboolean           res;
        GError            *error;

        memset (&event, 0, sizeof (CkLogEvent));

        event.type = type;
        g_get_current_time (&event.timestamp);

        error = NULL;
        res = ck_event_logger_queue_event (manager->priv->logger, &event, &error);
        if (! res) {
                g_debug ("Unable to log event: %s", error->message);
                g_error_free (error);
        }

        /* FIXME: in this case we should block and wait for log to flush */
}

static void
log_seat_session_added_event (CkManager  *manager,
                              CkSeat     *seat,
                              const char *ssid)
{
        CkLogEvent         event;
        gboolean           res;
        GError            *error;
        char              *sid;
        CkSession         *session;

        memset (&event, 0, sizeof (CkLogEvent));

        event.type = CK_LOG_EVENT_SEAT_SESSION_ADDED;
        g_get_current_time (&event.timestamp);

        sid = NULL;
        ck_seat_get_id (seat, &sid, NULL);

        event.event.seat_session_added.seat_id = (char *)get_object_id_basename (sid);
        event.event.seat_session_added.session_id = (char *)get_object_id_basename (ssid);

        session = g_hash_table_lookup (manager->priv->sessions, ssid);
        if (session != NULL) {
                g_object_get (session,
                              "session-type", &event.event.seat_session_added.session_type,
                              "x11-display", &event.event.seat_session_added.session_x11_display,
                              "x11-display-device", &event.event.seat_session_added.session_x11_display_device,
                              "display-device", &event.event.seat_session_added.session_display_device,
                              "remote-host-name", &event.event.seat_session_added.session_remote_host_name,
                              "is-local", &event.event.seat_session_added.session_is_local,
                              "unix-user", &event.event.seat_session_added.session_unix_user,
                              NULL);
                ck_session_get_creation_time (session, &event.event.seat_session_added.session_creation_time, NULL);
                g_debug ("Got uid: %u", event.event.seat_session_added.session_unix_user);
        } else {
        }

        error = NULL;
        res = ck_event_logger_queue_event (manager->priv->logger, &event, &error);
        if (! res) {
                g_debug ("Unable to log event: %s", error->message);
                g_error_free (error);
        }

        g_free (sid);

        g_free (event.event.seat_session_added.session_type);
        g_free (event.event.seat_session_added.session_x11_display);
        g_free (event.event.seat_session_added.session_x11_display_device);
        g_free (event.event.seat_session_added.session_display_device);
        g_free (event.event.seat_session_added.session_remote_host_name);
        g_free (event.event.seat_session_added.session_creation_time);
}

static void
log_seat_session_removed_event (CkManager  *manager,
                                CkSeat     *seat,
                                const char *ssid)
{
        CkLogEvent         event;
        gboolean           res;
        GError            *error;
        char              *sid;
        CkSession         *session;

        memset (&event, 0, sizeof (CkLogEvent));

        event.type = CK_LOG_EVENT_SEAT_SESSION_REMOVED;
        g_get_current_time (&event.timestamp);

        sid = NULL;
        ck_seat_get_id (seat, &sid, NULL);

        event.event.seat_session_removed.seat_id = (char *)get_object_id_basename (sid);
        event.event.seat_session_removed.session_id = (char *)get_object_id_basename (ssid);

        session = g_hash_table_lookup (manager->priv->sessions, ssid);
        if (session != NULL) {
                g_object_get (session,
                              "session-type", &event.event.seat_session_removed.session_type,
                              "x11-display", &event.event.seat_session_removed.session_x11_display,
                              "x11-display-device", &event.event.seat_session_removed.session_x11_display_device,
                              "display-device", &event.event.seat_session_removed.session_display_device,
                              "remote-host-name", &event.event.seat_session_removed.session_remote_host_name,
                              "is-local", &event.event.seat_session_removed.session_is_local,
                              "unix-user", &event.event.seat_session_removed.session_unix_user,
                              NULL);
                ck_session_get_creation_time (session, &event.event.seat_session_removed.session_creation_time, NULL);
                g_debug ("Got uid: %u", event.event.seat_session_removed.session_unix_user);
        }

        error = NULL;
        res = ck_event_logger_queue_event (manager->priv->logger, &event, &error);
        if (! res) {
                g_debug ("Unable to log event: %s", error->message);
                g_error_free (error);
        }

        g_free (sid);

        g_free (event.event.seat_session_removed.session_type);
        g_free (event.event.seat_session_removed.session_x11_display);
        g_free (event.event.seat_session_removed.session_x11_display_device);
        g_free (event.event.seat_session_removed.session_display_device);
        g_free (event.event.seat_session_removed.session_remote_host_name);
        g_free (event.event.seat_session_removed.session_creation_time);
}

static void
log_seat_active_session_changed_event (CkManager  *manager,
                                       CkSeat     *seat,
                                       const char *ssid)
{
        CkLogEvent         event;
        gboolean           res;
        GError            *error;
        char              *sid;

        memset (&event, 0, sizeof (CkLogEvent));

        event.type = CK_LOG_EVENT_SEAT_ACTIVE_SESSION_CHANGED;
        g_get_current_time (&event.timestamp);

        sid = NULL;
        ck_seat_get_id (seat, &sid, NULL);

        event.event.seat_active_session_changed.seat_id = (char *)get_object_id_basename (sid);
        event.event.seat_active_session_changed.session_id = (char *)get_object_id_basename (ssid);

        error = NULL;
        res = ck_event_logger_queue_event (manager->priv->logger, &event, &error);
        if (! res) {
                g_debug ("Unable to log event: %s", error->message);
                g_error_free (error);
        }

        g_free (sid);
}

static void
log_seat_device_added_event (CkManager   *manager,
                             CkSeat      *seat,
                             GValueArray *device)
{
        CkLogEvent         event;
        gboolean           res;
        GError            *error;
        char              *sid;
        GValue             val_struct = { 0, };
        char              *device_id;
        char              *device_type;

        memset (&event, 0, sizeof (CkLogEvent));

        event.type = CK_LOG_EVENT_SEAT_DEVICE_ADDED;
        g_get_current_time (&event.timestamp);

        sid = NULL;
        device_type = NULL;
        device_id = NULL;

        ck_seat_get_id (seat, &sid, NULL);

        g_value_init (&val_struct, CK_TYPE_DEVICE);
        g_value_set_static_boxed (&val_struct, device);
        res = dbus_g_type_struct_get (&val_struct,
                                      0, &device_type,
                                      1, &device_id,
                                      G_MAXUINT);

        event.event.seat_device_added.seat_id = (char *)get_object_id_basename (sid);

        event.event.seat_device_added.device_id = device_id;
        event.event.seat_device_added.device_type = device_type;

        error = NULL;
        res = ck_event_logger_queue_event (manager->priv->logger, &event, &error);
        if (! res) {
                g_debug ("Unable to log event: %s", error->message);
                g_error_free (error);
        }

        g_free (sid);
        g_free (device_type);
        g_free (device_id);
}

static void
log_seat_device_removed_event (CkManager   *manager,
                               CkSeat      *seat,
                               GValueArray *device)
{
        CkLogEvent         event;
        gboolean           res;
        GError            *error;
        char              *sid;
        GValue             val_struct = { 0, };
        char              *device_id;
        char              *device_type;

        memset (&event, 0, sizeof (CkLogEvent));

        event.type = CK_LOG_EVENT_SEAT_DEVICE_REMOVED;
        g_get_current_time (&event.timestamp);

        sid = NULL;
        device_type = NULL;
        device_id = NULL;

        ck_seat_get_id (seat, &sid, NULL);

        g_value_init (&val_struct, CK_TYPE_DEVICE);
        g_value_set_static_boxed (&val_struct, device);
        res = dbus_g_type_struct_get (&val_struct,
                                      0, &device_type,
                                      1, &device_id,
                                      G_MAXUINT);

        event.event.seat_device_removed.seat_id = (char *)get_object_id_basename (sid);

        event.event.seat_device_removed.device_id = device_id;
        event.event.seat_device_removed.device_type = device_type;

        error = NULL;
        res = ck_event_logger_queue_event (manager->priv->logger, &event, &error);
        if (! res) {
                g_debug ("Unable to log event: %s", error->message);
                g_error_free (error);
        }

        g_free (sid);
        g_free (device_type);
        g_free (device_id);
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

typedef void (*AuthorizedCallback) (CkManager             *manager,
                                    DBusGMethodInvocation *context);

typedef struct
{
        CkManager             *manager;
        DBusGMethodInvocation *context;
        AuthorizedCallback     callback;
} AuthorizedCallbackData;

static void
data_free (AuthorizedCallbackData *data)
{
        g_object_unref (data->manager);
        g_free (data);
}

#ifdef HAVE_POLKIT
static void
auth_ready_callback (PolkitAuthority        *authority,
                     GAsyncResult           *res,
                     AuthorizedCallbackData *data)
{
        GError *error;
        GError *error2;
        PolkitAuthorizationResult *result;

        error = NULL;

        result = polkit_authority_check_authorization_finish (authority,
                                                              res,
                                                              &error);
        if (error != NULL) {
                error2 = g_error_new (CK_MANAGER_ERROR,
                                      CK_MANAGER_ERROR_NOT_PRIVILEGED,
                                      "Not Authorized: %s", error->message);
                dbus_g_method_return_error (data->context, error2);
                g_error_free (error2);
                g_error_free (error);
        }
        else if (polkit_authorization_result_get_is_authorized (result)) {
                data->callback (data->manager, data->context);
        }
        else if (polkit_authorization_result_get_is_challenge (result)) {
                error = g_error_new (CK_MANAGER_ERROR,
                                     CK_MANAGER_ERROR_NOT_PRIVILEGED,
                                     "Authorization is required");
                dbus_g_method_return_error (data->context, error);
                g_error_free (error);
        }
        else {
                error = g_error_new (CK_MANAGER_ERROR,
                                     CK_MANAGER_ERROR_NOT_PRIVILEGED,
                                     "Not Authorized");
                dbus_g_method_return_error (data->context, error);
                g_error_free (error);
        }

        g_object_unref (result);

        data_free (data);
}

static void
check_polkit_permissions (CkManager             *manager,
                          DBusGMethodInvocation *context,
                          const char            *action,
                          gboolean               policykit_interactivity,
                          AuthorizedCallback     callback)
{
        const char    *sender;
        PolkitSubject *subject;
        AuthorizedCallbackData *data;
        PolkitCheckAuthorizationFlags auth_flag;

        g_debug ("constructing polkit data");

        /* Check that caller is privileged */
        sender = dbus_g_method_get_sender (context);
        subject = polkit_system_bus_name_new (sender);
        auth_flag = policykit_interactivity ? POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION : POLKIT_CHECK_AUTHORIZATION_FLAGS_NONE;

        g_debug ("checking if caller %s is authorized", sender);

        data = g_new0 (AuthorizedCallbackData, 1);
        data->manager = g_object_ref (manager);
        data->context = context;
        data->callback = callback;

        polkit_authority_check_authorization (manager->priv->pol_ctx,
                                              subject,
                                              action,
                                              NULL,
                                              auth_flag,
                                              NULL,
                                              (GAsyncReadyCallback)auth_ready_callback,
                                              data);
        g_object_unref (subject);
}

static void
ready_cb (PolkitAuthority *authority,
          GAsyncResult    *res,
          DBusGMethodInvocation *context)
{
        PolkitAuthorizationResult *ret;
        GError *error;

        error = NULL;
        ret = polkit_authority_check_authorization_finish (authority, res, &error);
        if (error != NULL) {
                dbus_g_method_return_error (context, error);
                g_error_free (error);
        }
        else if (polkit_authorization_result_get_is_authorized (ret)) {
                dbus_g_method_return (context, TRUE);
        }
        else if (polkit_authorization_result_get_is_challenge (ret)) {
                dbus_g_method_return (context, TRUE);
        }
        else {
                dbus_g_method_return (context, FALSE);
        }

        g_object_unref (ret);
}

/* We use this to avoid breaking API compability with ConsoleKit1 for
 * CanStop and CanRestart, but this method emulates how logind
 * presents it's API */
static void
logind_ready_cb (PolkitAuthority *authority,
                 GAsyncResult    *res,
                 DBusGMethodInvocation *context)
{
        PolkitAuthorizationResult *ret;
        GError *error;

        error = NULL;
        ret = polkit_authority_check_authorization_finish (authority, res, &error);
        if (error != NULL) {
                dbus_g_method_return_error (context, error);
                g_error_free (error);
        }
        else if (polkit_authorization_result_get_is_authorized (ret)) {
                dbus_g_method_return (context, "yes");
        }
        else if (polkit_authorization_result_get_is_challenge (ret)) {
                dbus_g_method_return (context, "challenge");
        }
        else {
                dbus_g_method_return (context, "no");
        }

        g_object_unref (ret);
}

/* We use this to avoid breaking API compability with ConsoleKit1 for
 * CanStop and CanRestart, but this method emulates how logind
 * presents it's API */
static void
get_polkit_logind_permissions (CkManager   *manager,
                               const char  *action,
                               DBusGMethodInvocation *context)
{
        const char    *sender;
        PolkitSubject *subject;

        g_debug ("get permissions for action %s", action);

        sender = dbus_g_method_get_sender (context);
        subject = polkit_system_bus_name_new (sender);

        polkit_authority_check_authorization (manager->priv->pol_ctx,
                                              subject,
                                              action,
                                              NULL,
                                              0,
                                              NULL,
                                              (GAsyncReadyCallback) logind_ready_cb,
                                              context);
        g_object_unref (subject);
}

static void
get_polkit_permissions (CkManager   *manager,
                        const char  *action,
                        DBusGMethodInvocation *context)
{
        const char    *sender;
        PolkitSubject *subject;

        g_debug ("get permissions for action %s", action);

        sender = dbus_g_method_get_sender (context);
        subject = polkit_system_bus_name_new (sender);

        polkit_authority_check_authorization (manager->priv->pol_ctx,
                                              subject,
                                              action,
                                              NULL,
                                              0,
                                              NULL,
                                              (GAsyncReadyCallback) ready_cb,
                                              context);
        g_object_unref (subject);
}
#endif

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

static char *
get_user_name (uid_t uid)
{
        struct passwd *pwent;
        char          *name;

        name = NULL;

        pwent = getpwuid (uid);

        if (pwent != NULL) {
                name = g_strdup (pwent->pw_name);
        }

        return name;
}

static gboolean
session_is_real_user (CkSession *session,
                      char     **userp)
{
        int         uid;
        char       *username;
        char       *session_type;
        gboolean    ret;

        ret = FALSE;
        session_type = NULL;
        username = NULL;

        session_type = NULL;

        g_object_get (session,
                      "unix-user", &uid,
                      "session-type", &session_type,
                      NULL);

        username = get_user_name (uid);

        /* filter out GDM user */
        if (username != NULL && strcmp (username, "gdm") == 0) {
                ret = FALSE;
                goto out;
        }

        if (userp != NULL) {
                *userp = g_strdup (username);
        }

        ret = TRUE;

 out:
        g_free (username);
        g_free (session_type);

        return ret;
}

static void
collect_users (const char *ssid,
               CkSession  *session,
               GHashTable *hash)
{
        char *username;

        if (session_is_real_user (session, &username)) {
                if (username != NULL) {
                        g_hash_table_insert (hash, username, NULL);
                }
        }
}

static guint
get_system_num_users (CkManager *manager)
{
        guint            num_users;
        GHashTable      *hash;

        hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

        g_hash_table_foreach (manager->priv->sessions, (GHFunc)collect_users, hash);

        num_users = g_hash_table_size (hash);

        g_hash_table_destroy (hash);

        g_debug ("found %u unique users", num_users);

        return num_users;
}

#ifdef ENABLE_RBAC_SHUTDOWN
static gboolean
check_rbac_permissions (CkManager             *manager,
                        DBusGMethodInvocation *context,
                        const char            *action,
                        AuthorizedCallback     callback)
{
        const char *sender;
        char       *username;
        gboolean    res;
        uid_t       uid;
        pid_t       pid;

        username = NULL;
        sender   = dbus_g_method_get_sender (context);
        res      = get_caller_info (manager,
                                    sender,
                                    &uid,
                                    &pid);
        if (!res) {
                goto out;
        }

        username = get_user_name (uid);

        if (username == NULL ||
            !chkauthattr (action, username)) {
                res = FALSE;
                goto out;
        }

out:

        if (res == TRUE) {
                g_debug ("User %s has RBAC permission to stop/restart", username);
        } else {
                g_debug ("User %s does not have RBAC permission to stop/restart", username);
        }

        g_free (username);

        if (res && callback) {
                callback (manager, context);
        }

        return res;
}
#endif

/* Performs the callback if the user has permissions authorizing them either
 * via the RBAC_SHUTDOWN_KEY or PolicyKit action. The policykit_interactivity
 * flag is used only by PolicyKit to determine if the user should be prompted
 * for their password if can returns a "challenge" response.
 */
static void
check_system_action (CkManager             *manager,
                     gboolean               policykit_interactivity,
                     DBusGMethodInvocation *context,
                     const char            *action,
                     AuthorizedCallback     callback)
{
        g_return_if_fail (callback != NULL);
        g_return_if_fail (action != NULL);

#if defined HAVE_POLKIT
        check_polkit_permissions (manager, context, action, policykit_interactivity, callback);
#elif defined ENABLE_RBAC_SHUTDOWN
        check_rbac_permissions (manager, context, RBAC_SHUTDOWN_KEY, callback);
#else
        g_warning ("Compiled without PolicyKit or RBAC support!");
        callback(manager, context);
#endif
}

/* Determines if the user can perform the action via PolicyKit, rbac, or
 * otherwise.
 * Only used for the 0.9.0+ calls, not CanStop or CanRestart as they
 * return a boolean.
 */
static void
check_can_action (CkManager             *manager,
                  DBusGMethodInvocation *context,
                  const char            *action)
{
#if defined HAVE_POLKIT
        /* This will return the yes, no, and challenge */
        get_polkit_logind_permissions (manager, action, context);
#elif defined ENABLE_RBAC_SHUTDOWN
        /* rbac determines either yes or no. There is no challenge with rbac */
        if (check_rbac_permissions (manager, context, RBAC_SHUTDOWN_KEY, NULL)) {
                dbus_g_method_return (context, "yes");
        } else {
                dbus_g_method_return (context, "no");
        }
#else
        /* neither polkit or rbac. assumed single user system */
        dbus_g_method_return (context, "yes");
#endif
}

/* Logs the event and performs the system call such as ck-system-restart.
 * returns an error message over dbus to the user if needed, otherwise returns
 * success (also over dbus).
 */
static void
do_system_action (CkManager             *manager,
                  DBusGMethodInvocation *context,
                  const gchar           *command,
                  CkLogEventType         event_type,
                  const gchar           *description)
{
        GError *error;
        gboolean res;

        g_debug ("ConsoleKit preforming %s", description);

        log_system_action_event (manager, event_type);

        g_debug ("command is %s", command);

        error = NULL;
        res = g_spawn_command_line_sync (command, NULL, NULL, NULL, &error);

        if (! res) {
                GError *new_error;

                g_warning ("Unable to %s system: %s", description, error->message);

                new_error = g_error_new (CK_MANAGER_ERROR,
                                         CK_MANAGER_ERROR_GENERAL,
                                         "Unable to %s system: %s", description, error->message);

                dbus_g_method_return_error (context, new_error);
                g_error_free (new_error);

                g_error_free (error);
        } else {
                dbus_g_method_return (context);
        }
}

static gboolean
system_action_idle_cb(SystemActionData *data)
{
        g_return_val_if_fail (data != NULL, FALSE);

        /* Perform the action, it will handle the dbus_g_method_return */
        do_system_action (data->manager,
                          data->context,
                          data->command,
                          data->event_type,
                          data->description);

        /* If we got here the sleep action is done and we're awake again
         * or the operation failed. Either way we can signal to the apps */
        g_signal_emit (data->manager, signals [data->signal], 0, FALSE);

        /* reset this since we'll return FALSE here and kill the cb */
        data->manager->priv->system_action_idle_id = 0;

        g_free (data);

        return FALSE;
}

static void
do_restart (CkManager             *manager,
            DBusGMethodInvocation *context)
{
        SystemActionData *data;

        /* Don't allow multiple system actions at the same time */
        if (manager->priv->system_action_idle_id != 0) {
                g_error ("attempting to perform a system action while one is in progress");
                dbus_g_method_return (context, FALSE);
                return;
        }

        /* Emit the signal */
        g_signal_emit (manager, signals [PREPARE_FOR_SHUTDOWN], 0, TRUE);

        /* Allocate and fill the data struct to pass to the idle cb */
        data = g_new0 (SystemActionData, 1);
        if (data == NULL) {
                g_critical ("failed to allocate memory to perform shutdown\n");
                g_signal_emit (manager, signals [PREPARE_FOR_SHUTDOWN], 0, FALSE);
                return;
        }

        data->manager = manager;
        data->context = context;
        data->command = PREFIX "/lib/ConsoleKit/scripts/ck-system-restart";
        data->event_type = CK_LOG_EVENT_SYSTEM_RESTART;
        data->description = "Restart";
        data->signal = PREPARE_FOR_SHUTDOWN;

        /* Sleep so user applications have time to respond */
        manager->priv->system_action_idle_id = g_timeout_add (data->manager->priv->system_action_idle_delay,
                                                              (GSourceFunc)system_action_idle_cb,
                                                              data);
}

/*
  Example:
  dbus-send --system --dest=org.freedesktop.ConsoleKit \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/freedesktop/ConsoleKit/Manager \
  org.freedesktop.ConsoleKit.Manager.Restart
*/
gboolean
ck_manager_restart (CkManager             *manager,
                    DBusGMethodInvocation *context)
{
        const char *action;

        /* Check if something in inhibiting that action */
        if (ck_inhibit_manager_is_shutdown_inhibited (manager->priv->inhibit_manager)) {
                g_debug ("restart inhibited");
                dbus_g_method_return (context, FALSE);
                return TRUE;
        }

        if (get_system_num_users (manager) > 1) {
                action = "org.freedesktop.consolekit.system.restart-multiple-users";
        } else {
                action = "org.freedesktop.consolekit.system.restart";
        }

        g_debug ("ConsoleKit Restart: %s", action);

        check_system_action (manager, TRUE, context, action, do_restart);

        return TRUE;
}

gboolean
ck_manager_can_restart (CkManager  *manager,
                    DBusGMethodInvocation *context)

{
        const char *action;

        action = "org.freedesktop.consolekit.system.restart";

#if defined HAVE_POLKIT
        get_polkit_permissions (manager, action, context);
#elif defined ENABLE_RBAC_SHUTDOWN
        if (check_rbac_permissions (manager, context, RBAC_SHUTDOWN_KEY,
                                        NULL)) {
                dbus_g_method_return (context, TRUE);
        } else {
                dbus_g_method_return (context, FALSE);
        }
#endif

        return TRUE;
}

static void
do_stop (CkManager             *manager,
         DBusGMethodInvocation *context)
{
        SystemActionData *data;

        /* Don't allow multiple system actions at the same time */
        if (manager->priv->system_action_idle_id != 0) {
                g_error ("attempting to perform a system action while one is in progress");
                dbus_g_method_return (context, FALSE);
                return;
        }

        /* Emit the signal */
        g_signal_emit (manager, signals [PREPARE_FOR_SHUTDOWN], 0, TRUE);

        /* Allocate and fill the data struct to pass to the idle cb */
        data = g_new0 (SystemActionData, 1);
        if (data == NULL) {
                g_critical ("failed to allocate memory to perform shutdown\n");
                g_signal_emit (manager, signals [PREPARE_FOR_SHUTDOWN], 0, FALSE);
                dbus_g_method_return (context, FALSE);
                return;
        }

        data->manager = manager;
        data->context = context;
        data->command = PREFIX "/lib/ConsoleKit/scripts/ck-system-stop";
        data->event_type = CK_LOG_EVENT_SYSTEM_STOP;
        data->description = "Stop";
        data->signal = PREPARE_FOR_SHUTDOWN;

        /* Sleep so user applications have time to respond */
        manager->priv->system_action_idle_id = g_timeout_add (data->manager->priv->system_action_idle_delay,
                                                              (GSourceFunc)system_action_idle_cb,
                                                              data);
}

gboolean
ck_manager_stop (CkManager             *manager,
                 DBusGMethodInvocation *context)
{
        const char *action;

        /* Check if something in inhibiting that action */
        if (ck_inhibit_manager_is_shutdown_inhibited (manager->priv->inhibit_manager)) {
                g_debug ("shutdown inhibited");
                dbus_g_method_return (context, FALSE);
                return TRUE;
        }

        if (get_system_num_users (manager) > 1) {
                action = "org.freedesktop.consolekit.system.stop-multiple-users";
        } else {
                action = "org.freedesktop.consolekit.system.stop";
        }

        check_system_action (manager, TRUE, context, action, do_stop);

        return TRUE;
}

gboolean
ck_manager_can_stop (CkManager  *manager,
                    DBusGMethodInvocation *context)
{
        const char *action;

        action = "org.freedesktop.consolekit.system.stop";

#if defined HAVE_POLKIT
        get_polkit_permissions (manager, action, context);
#elif defined ENABLE_RBAC_SHUTDOWN
        if (check_rbac_permissions (manager, context, RBAC_SHUTDOWN_KEY,
                                        NULL)) {
                dbus_g_method_return (context, TRUE);
        } else {
                dbus_g_method_return (context, FALSE);
        }
#endif

        return TRUE;
}

/*
  Example:
  dbus-send --system --dest=org.freedesktop.ConsoleKit \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/freedesktop/ConsoleKit/Manager \
  org.freedesktop.ConsoleKit.Manager.PowerOff boolean:TRUE
*/
gboolean
ck_manager_power_off (CkManager             *manager,
                      gboolean               policykit_interactivity,
                      DBusGMethodInvocation *context)
{
        const char *action;

        /* Check if something in inhibiting that action */
        if (ck_inhibit_manager_is_shutdown_inhibited (manager->priv->inhibit_manager)) {
                g_debug ("poweroff inhibited");
                dbus_g_method_return (context, FALSE);
                return TRUE;
        }

        if (get_system_num_users (manager) > 1) {
                action = "org.freedesktop.consolekit.system.stop-multiple-users";
        } else {
                action = "org.freedesktop.consolekit.system.stop";
        }

        g_debug ("ConsoleKit PowerOff: %s", action);

        check_system_action (manager, policykit_interactivity, context, action, do_stop);

        return TRUE;
}

/**
 * ck_manager_can_power_off:
 * @manager: the @CkManager object
 * @context: We return a string here, either:
 * yes - system can and user explicitly authorized by polkit, rbac, or neither is running
 * no  - system can and user explicitly unauthorized by polkit or rbac
 * challenge - system can and user requires elevation via polkit
 * na - system does not support it (hardware or backend support missing).
 *
 * Determines if the system can shutdown.
 * Example:
  dbus-send --system --dest=org.freedesktop.ConsoleKit \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/freedesktop/ConsoleKit/Manager \
  org.freedesktop.ConsoleKit.Manager.CanPowerOff
 *
 * Returnes TRUE.
 **/
gboolean
ck_manager_can_power_off (CkManager  *manager,
                          DBusGMethodInvocation *context)

{
        const char *action;

        if (get_system_num_users (manager) > 1) {
                action = "org.freedesktop.consolekit.system.stop-multiple-users";
        } else {
                action = "org.freedesktop.consolekit.system.stop";
        }

        check_can_action (manager, context, action);

        return TRUE;
}

/*
  Example:
  dbus-send --system --dest=org.freedesktop.ConsoleKit \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/freedesktop/ConsoleKit/Manager \
  org.freedesktop.ConsoleKit.Manager.Reboot boolean:TRUE
*/
gboolean
ck_manager_reboot  (CkManager             *manager,
                    gboolean               policykit_interactivity,
                    DBusGMethodInvocation *context)
{
        const char *action;

        /* Check if something in inhibiting that action */
        if (ck_inhibit_manager_is_shutdown_inhibited (manager->priv->inhibit_manager)) {
                g_debug ("reboot inhibited");
                dbus_g_method_return (context, FALSE);
                return TRUE;
        }

        if (get_system_num_users (manager) > 1) {
                action = "org.freedesktop.consolekit.system.restart-multiple-users";
        } else {
                action = "org.freedesktop.consolekit.system.restart";
        }

        g_debug ("ConsoleKit Restart: %s", action);

        check_system_action (manager, policykit_interactivity, context, action, do_restart);

        return TRUE;
}

/**
 * ck_manager_can_reboot:
 * @manager: the @CkManager object
 * @context: We return a string here, either:
 * yes - system can and user explicitly authorized by polkit, rbac, or neither is running
 * no  - system can and user explicitly unauthorized by polkit or rbac
 * challenge - system can and user requires elevation via polkit
 * na - system does not support it (hardware or backend support missing).
 *
 * Determines if the system can suspend.
 * Example:
  dbus-send --system --dest=org.freedesktop.ConsoleKit \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/freedesktop/ConsoleKit/Manager \
  org.freedesktop.ConsoleKit.Manager.CanReboot
 *
 * Returnes TRUE.
 **/
gboolean
ck_manager_can_reboot  (CkManager  *manager,
                        DBusGMethodInvocation *context)

{
        const char *action;

        if (get_system_num_users (manager) > 1) {
                action = "org.freedesktop.consolekit.system.restart-multiple-users";
        } else {
                action = "org.freedesktop.consolekit.system.restart";
        }

        check_can_action (manager, context, action);

        return TRUE;
}

static void
do_suspend (CkManager             *manager,
            DBusGMethodInvocation *context)
{
        SystemActionData *data;

        /* Don't allow multiple system actions at the same time */
        if (manager->priv->system_action_idle_id != 0) {
                g_error ("attempting to perform a system action while one is in progress");
                dbus_g_method_return (context, FALSE);
                return;
        }

        /* Emit the signal */
        g_signal_emit (manager, signals [PREPARE_FOR_SLEEP], 0, TRUE);

        /* Allocate and fill the data struct to pass to the idle cb */
        data = g_new0 (SystemActionData, 1);
        if (data == NULL) {
                g_critical ("failed to allocate memory to perform suspend\n");
                g_signal_emit (manager, signals [PREPARE_FOR_SLEEP], 0, FALSE);
                dbus_g_method_return (context, FALSE);
                return;
        }

        data->manager = manager;
        data->context = context;
        data->command = PREFIX "/lib/ConsoleKit/scripts/ck-system-suspend";
        data->event_type = CK_LOG_EVENT_SYSTEM_SUSPEND;
        data->description = "Suspend";
        data->signal = PREPARE_FOR_SLEEP;

        /* Sleep so user applications have time to respond */
        manager->priv->system_action_idle_id = g_timeout_add (data->manager->priv->system_action_idle_delay,
                                                              (GSourceFunc)system_action_idle_cb,
                                                              data);
}

/*
  Example:
  dbus-send --system --dest=org.freedesktop.ConsoleKit \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/freedesktop/ConsoleKit/Manager \
  org.freedesktop.ConsoleKit.Manager.Suspend boolean:true
*/
gboolean
ck_manager_suspend (CkManager             *manager,
                    gboolean               policykit_interactivity,
                    DBusGMethodInvocation *context)
{
        const char *action;

        /* Check if something in inhibiting that action */
        if (ck_inhibit_manager_is_suspend_inhibited (manager->priv->inhibit_manager)) {
                g_debug ("suspend inhibited");
                dbus_g_method_return (context, FALSE);
                return TRUE;
        }

        if (get_system_num_users (manager) > 1) {
                action = "org.freedesktop.consolekit.system.suspend-multiple-users";
        } else {
                action = "org.freedesktop.consolekit.system.suspend";
        }

        g_debug ("ConsoleKit Suspend: %s", action);

        check_system_action (manager, policykit_interactivity, context, action, do_suspend);

        return TRUE;
}

/**
 * ck_manager_can_suspend:
 * @manager: the @CkManager object
 * @context: We return a string here, either:
 * yes - system can and user explicitly authorized by polkit, rbac, or neither is running
 * no  - system can and user explicitly unauthorized by polkit or rbac
 * challenge - system can and user requires elevation via polkit
 * na - system does not support it (hardware or backend support missing).
 *
 * Determines if the system can suspend.
 * Example:
  dbus-send --system --dest=org.freedesktop.ConsoleKit \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/freedesktop/ConsoleKit/Manager \
  org.freedesktop.ConsoleKit.Manager.CanSuspend
 *
 * Returnes TRUE.
 **/
gboolean
ck_manager_can_suspend (CkManager  *manager,
                        DBusGMethodInvocation *context)

{
        const char *action;

        if (get_system_num_users (manager) > 1) {
                action = "org.freedesktop.consolekit.system.suspend-multiple-users";
        } else {
                action = "org.freedesktop.consolekit.system.suspend";
        }

        if (ck_system_can_suspend ()) {
                check_can_action (manager, context, action);
        } else {
                /* not supported by system (or consolekit backend) */
                dbus_g_method_return (context, "na");
        }

        return TRUE;
}

static void
do_hibernate (CkManager             *manager,
              DBusGMethodInvocation *context)
{
        SystemActionData *data;

        /* Emit the signal */
        g_signal_emit (manager, signals [PREPARE_FOR_SLEEP], 0, TRUE);

        /* Allocate and fill the data struct to pass to the idle cb */
        data = g_new0 (SystemActionData, 1);
        if (data == NULL) {
                g_critical ("failed to allocate memory to perform suspend\n");
                g_signal_emit (manager, signals [PREPARE_FOR_SLEEP], 0, FALSE);
                dbus_g_method_return (context, FALSE);
                return;
        }

        data->manager = manager;
        data->context = context;
        data->command = PREFIX "/lib/ConsoleKit/scripts/ck-system-hibernate";
        data->event_type = CK_LOG_EVENT_SYSTEM_HIBERNATE;
        data->description = "Hibernate";
        data->signal = PREPARE_FOR_SLEEP;

        /* Sleep so user applications have time to respond */
        manager->priv->system_action_idle_id = g_timeout_add (data->manager->priv->system_action_idle_delay,
                                                              (GSourceFunc)system_action_idle_cb,
                                                              data);
}

/*
  Example:
  dbus-send --system --dest=org.freedesktop.ConsoleKit \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/freedesktop/ConsoleKit/Manager \
  org.freedesktop.ConsoleKit.Manager.Hibernate boolean:true
*/
gboolean
ck_manager_hibernate (CkManager             *manager,
                      gboolean               policykit_interactivity,
                      DBusGMethodInvocation *context)
{
        const char *action;

        /* Check if something in inhibiting that action */
        if (ck_inhibit_manager_is_suspend_inhibited (manager->priv->inhibit_manager)) {
                g_debug ("hibernate inhibited");
                dbus_g_method_return (context, FALSE);
                return TRUE;
        }

        if (get_system_num_users (manager) > 1) {
                action = "org.freedesktop.consolekit.system.hibernate-multiple-users";
        } else {
                action = "org.freedesktop.consolekit.system.hibernate";
        }

        g_debug ("ConsoleKit Hibernate: %s", action);

        check_system_action (manager, policykit_interactivity, context, action, do_hibernate);

        return TRUE;
}

/**
 * ck_manager_can_hibernate:
 * @manager: the @CkManager object
 * @context: We return a string here, either:
 * yes - system can and user explicitly authorized by polkit, rbac, or neither is running
 * no  - system can and user explicitly unauthorized by polkit or rbac
 * challenge - system can and user requires elevation via polkit
 * na - system does not support it (hardware or backend support missing).
 *
 * Determines if the system can hibernate.
 * Example:
  dbus-send --system --dest=org.freedesktop.ConsoleKit \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/freedesktop/ConsoleKit/Manager \
  org.freedesktop.ConsoleKit.Manager.CanHibernate
 *
 * Returnes TRUE.
 **/
gboolean
ck_manager_can_hibernate (CkManager  *manager,
                          DBusGMethodInvocation *context)

{
        const char *action;

        if (get_system_num_users (manager) > 1) {
                action = "org.freedesktop.consolekit.system.hibernate-multiple-users";
        } else {
                action = "org.freedesktop.consolekit.system.hibernate";
        }

        if (ck_system_can_hibernate ()) {
                check_can_action (manager, context, action);
        } else {
                /* not supported by system (or consolekit backend) */
                dbus_g_method_return (context, "na");
        }

        return TRUE;
}

static void
do_hybrid_sleep (CkManager             *manager,
                 DBusGMethodInvocation *context)
{
        SystemActionData *data;

        /* Emit the signal */
        g_signal_emit (manager, signals [PREPARE_FOR_SLEEP], 0, TRUE);

        /* Allocate and fill the data struct to pass to the idle cb */
        data = g_new0 (SystemActionData, 1);
        if (data == NULL) {
                g_critical ("failed to allocate memory to perform suspend\n");
                g_signal_emit (manager, signals [PREPARE_FOR_SLEEP], 0, FALSE);
                dbus_g_method_return (context, FALSE);
                return;
        }

        data->manager = manager;
        data->context = context;
        data->command = PREFIX "/lib/ConsoleKit/scripts/ck-system-hybridsleep";
        data->event_type = CK_LOG_EVENT_SYSTEM_HIBERNATE;
        data->description = "Hybrid Sleep";
        data->signal = PREPARE_FOR_SLEEP;

        /* Sleep so user applications have time to respond */
        manager->priv->system_action_idle_id = g_timeout_add (data->manager->priv->system_action_idle_delay,
                                                              (GSourceFunc)system_action_idle_cb,
                                                              data);
}

/*
  Example:
  dbus-send --system --dest=org.freedesktop.ConsoleKit \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/freedesktop/ConsoleKit/Manager \
  org.freedesktop.ConsoleKit.Manager.HybridSleep boolean:true
*/
gboolean
ck_manager_hybrid_sleep (CkManager             *manager,
                         gboolean               policykit_interactivity,
                         DBusGMethodInvocation *context)
{
        const char *action;

        /* Check if something in inhibiting that action */
        if (ck_inhibit_manager_is_suspend_inhibited (manager->priv->inhibit_manager)) {
                g_debug ("hybrid sleep inhibited");
                dbus_g_method_return (context, FALSE);
                return TRUE;
        }

        if (get_system_num_users (manager) > 1) {
                action = "org.freedesktop.consolekit.system.hybridsleep-multiple-users";
        } else {
                action = "org.freedesktop.consolekit.system.hybridsleep";
        }

        g_debug ("ConsoleKit Hibernate: %s", action);

        check_system_action (manager, policykit_interactivity, context, action, do_hybrid_sleep);

        return TRUE;
}

/**
 * ck_manager_can_hybrid_sleep:
 * @manager: the @CkManager object
 * @context: We return a string here, either:
 * yes - system can and user explicitly authorized by polkit, rbac, or neither is running
 * no  - system can and user explicitly unauthorized by polkit or rbac
 * challenge - system can and user requires elevation via polkit
 * na - system does not support it (hardware or backend support missing).
 *
 * Determines if the system can hybrid sleep (suspend + hibernate).
 * Example:
  dbus-send --system --dest=org.freedesktop.ConsoleKit \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/freedesktop/ConsoleKit/Manager \
  org.freedesktop.ConsoleKit.Manager.CanHybridSleep
 *
 * Returnes TRUE.
 **/
gboolean
ck_manager_can_hybrid_sleep (CkManager  *manager,
                             DBusGMethodInvocation *context)

{
        const char *action;

        if (get_system_num_users (manager) > 1) {
                action = "org.freedesktop.consolekit.system.hybridsleep-multiple-users";
        } else {
                action = "org.freedesktop.consolekit.system.hybridsleep";
        }

        if (ck_system_can_hybrid_sleep ()) {
                check_can_action (manager, context, action);
        } else {
                /* not supported by system (or consolekit backend) */
                dbus_g_method_return (context, "na");
        }

        return TRUE;
}

gboolean
ck_manager_inhibit (CkManager *manager,
                    gchar *what,
                    gchar *who,
                    gchar *why,
                    DBusGMethodInvocation *context)
{
        CkManagerPrivate *priv;
        gint              fd = -1;

        g_return_val_if_fail (CK_IS_MANAGER (manager), FALSE);

        priv = CK_MANAGER_GET_PRIVATE (manager);

        if (priv->inhibit_manager == NULL) {
                priv->inhibit_manager = ck_inhibit_manager_get ();
        }

        fd = ck_inhibit_manager_create_lock (priv->inhibit_manager,
                                             who,
                                             what,
                                             why);

        dbus_g_method_return (context, fd);

        return TRUE;
}

static void
on_seat_active_session_changed_full (CkSeat     *seat,
                                     CkSession  *old_session,
                                     CkSession  *session,
                                     CkManager  *manager)
{
        char *ssid = NULL;

        if (session != NULL) {
                ck_session_get_id (session, &ssid, NULL);
        }

        ck_manager_dump (manager);
        ck_seat_run_programs (seat, old_session, session, "seat_active_session_changed");

        log_seat_active_session_changed_event (manager, seat, ssid);

        g_free (ssid);
}

static void
on_seat_session_added_full (CkSeat     *seat,
                            CkSession  *session,
                            CkManager  *manager)
{
        char *ssid = NULL;

        ck_session_get_id (session, &ssid, NULL);

        ck_manager_dump (manager);
        ck_session_run_programs (session, "session_added");

        log_seat_session_added_event (manager, seat, ssid);

        g_free (ssid);
}

static void
on_seat_session_removed_full (CkSeat     *seat,
                              CkSession  *session,
                              CkManager  *manager)
{
        char *ssid = NULL;

        ck_session_get_id (session, &ssid, NULL);

        ck_manager_dump (manager);
        ck_session_run_programs (session, "session_removed");

        log_seat_session_removed_event (manager, seat, ssid);

        g_free (ssid);
}

static void
on_seat_device_added (CkSeat      *seat,
                      GValueArray *device,
                      CkManager   *manager)
{
        ck_manager_dump (manager);
        log_seat_device_added_event (manager, seat, device);
}

static void
on_seat_device_removed (CkSeat      *seat,
                        GValueArray *device,
                        CkManager   *manager)
{
        ck_manager_dump (manager);
        log_seat_device_removed_event (manager, seat, device);
}

static void
connect_seat_signals (CkManager *manager,
                      CkSeat    *seat)
{
        ConsoleKitSeat *ckseat = CONSOLE_KIT_SEAT (seat);

        /* Private signals on CkSeat */
        g_signal_connect (seat, "active-session-changed-full", G_CALLBACK (on_seat_active_session_changed_full), manager);
        g_signal_connect (seat, "session-added-full", G_CALLBACK (on_seat_session_added_full), manager);
        g_signal_connect (seat, "session-removed-full", G_CALLBACK (on_seat_session_removed_full), manager);
        /* Public dbus signals on ConsoleKitSeat */
        g_signal_connect (ckseat, "device-added", G_CALLBACK (on_seat_device_added), manager);
        g_signal_connect (ckseat, "device-removed", G_CALLBACK (on_seat_device_removed), manager);
}

static void
disconnect_seat_signals (CkManager *manager,
                         CkSeat    *seat)
{
        ConsoleKitSeat *ckseat = CONSOLE_KIT_SEAT (seat);

        g_signal_handlers_disconnect_by_func (seat, on_seat_active_session_changed_full, manager);
        g_signal_handlers_disconnect_by_func (seat, on_seat_session_added_full, manager);
        g_signal_handlers_disconnect_by_func (seat, on_seat_session_removed_full, manager);
        g_signal_handlers_disconnect_by_func (ckseat, on_seat_device_added, manager);
        g_signal_handlers_disconnect_by_func (ckseat, on_seat_device_removed, manager);
}

static CkSeat *
add_new_seat (CkManager *manager,
              CkSeatKind kind)
{
        char   *sid;
        CkSeat *seat;

        sid = generate_seat_id (manager);

        seat = ck_seat_new (sid, kind, NULL);

        /* First we connect our own signals to the seat, followed by
         * the D-Bus signal hookup to make sure we can first dump the
         * database and only then send out the D-Bus signals for
         * it. GObject guarantees us that the signal handlers are
         * called in the same order as they are registered. */

        connect_seat_signals (manager, seat);
        if (!ck_seat_register (seat)) {
                /* returns false if connection to bus fails */
                disconnect_seat_signals (manager, seat);
                g_object_unref (seat);
                g_free (sid);
                return NULL;
        }

        g_hash_table_insert (manager->priv->seats, sid, seat);

        g_debug ("Added seat: %s kind:%d", sid, kind);

        ck_manager_dump (manager);
        ck_seat_run_programs (seat, NULL, NULL, "seat_added");

        g_debug ("Emitting seat-added: %s", sid);
        g_signal_emit (manager, signals [SEAT_ADDED], 0, sid);

        log_seat_added_event (manager, seat);

        return seat;
}

static void
remove_seat (CkManager *manager,
             CkSeat    *seat)
{
        char    *sid;
        char    *orig_sid;
        CkSeat  *orig_seat;
        gboolean res;

        sid = NULL;
        ck_seat_get_id (seat, &sid, NULL);

        /* Need to get the original key/value */
        res = g_hash_table_lookup_extended (manager->priv->seats,
                                            sid,
                                            (gpointer *)&orig_sid,
                                            (gpointer *)&orig_seat);
        if (! res) {
                g_debug ("Seat %s is not attached", sid);
                goto out;
        }

        /* Remove the seat from the list but don't call
         * unref until the signal is emitted */
        g_hash_table_steal (manager->priv->seats, sid);

        disconnect_seat_signals (manager, orig_seat);

        if (sid != NULL) {
                g_hash_table_remove (manager->priv->seats, sid);
        }

        ck_manager_dump (manager);
        ck_seat_run_programs (seat, NULL, NULL, "seat_removed");

        g_debug ("Emitting seat-removed: %s", sid);
        g_signal_emit (manager, signals [SEAT_REMOVED], 0, sid);

        log_seat_removed_event (manager, orig_seat);

        g_debug ("Removed seat: %s", sid);

        if (orig_seat != NULL) {
                g_object_unref (orig_seat);
        }
        g_free (orig_sid);

 out:
        g_free (sid);
}

#define IS_STR_SET(x) (x != NULL && x[0] != '\0')

static CkSeat *
find_seat_for_session (CkManager *manager,
                       CkSession *session)
{
        CkSeat  *seat;
        ConsoleKitSession *cksession;
        gboolean is_static_x11;
        gboolean is_static_text;
        const char    *display_device;
        const char    *x11_display_device;
        const char    *x11_display;
        const char    *remote_host_name;
        gboolean is_local;

        is_static_text = FALSE;
        is_static_x11 = FALSE;

        seat = NULL;
        display_device = NULL;
        x11_display_device = NULL;
        x11_display = NULL;
        remote_host_name = NULL;
        is_local = FALSE;
        cksession = CONSOLE_KIT_SESSION (session);

        /* FIXME: use matching to group entries? */

        display_device     = console_kit_session_get_display_device (cksession);
        x11_display_device = console_kit_session_get_x11_display_device (cksession);
        x11_display        = console_kit_session_get_x11_display (cksession);
        remote_host_name   = console_kit_session_get_remote_host_name (cksession);
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

        return seat;
}

static gboolean
manager_set_system_idle_hint (CkManager *manager,
                              gboolean   idle_hint)
{
        /* Check if something in inhibiting that action */
        if (ck_inhibit_manager_is_idle_inhibited (manager->priv->inhibit_manager)) {
                g_debug ("idle inhibited, forcing idle_hint to FALSE");
                idle_hint = FALSE;
        }

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
        /* return TRUE to stop search */
        return !console_kit_session_get_idle_hint (CONSOLE_KIT_SESSION (session));
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
open_session_for_leader (CkManager             *manager,
                         CkSessionLeader       *leader,
                         const GPtrArray       *parameters,
                         DBusGMethodInvocation *context)
{
        CkSession   *session;
        CkSeat      *seat;
        const char  *ssid;
        const char  *cookie;

        ssid = ck_session_leader_peek_session_id (leader);
        cookie = ck_session_leader_peek_cookie (leader);

        session = ck_session_new_with_parameters (ssid,
                                                  cookie,
                                                  parameters,
                                                  NULL);

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

        g_hash_table_insert (manager->priv->sessions,
                             g_strdup (ssid),
                             g_object_ref (session));

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
        g_signal_connect (CONSOLE_KIT_SESSION (session), "idle-hint-changed",
                          G_CALLBACK (session_idle_hint_changed),
                          manager);

        g_object_unref (session);

        dbus_g_method_return (context, cookie);
}

enum {
        PROP_STRING,
        PROP_BOOLEAN,
};

#define CK_TYPE_PARAMETER_STRUCT (dbus_g_type_get_struct ("GValueArray", \
                                                          G_TYPE_STRING,  \
                                                          G_TYPE_VALUE, \
                                                          G_TYPE_INVALID))

static gboolean
_get_parameter (GPtrArray  *parameters,
                const char *name,
                int         prop_type,
                gpointer   *value)
{
        gboolean ret;
        int      i;

        if (parameters == NULL) {
                return FALSE;
        }

        ret = FALSE;

        for (i = 0; i < parameters->len && ret == FALSE; i++) {
                gboolean    res;
                GValue      val_struct = { 0, };
                char       *prop_name;
                GValue     *prop_val;

                g_value_init (&val_struct, CK_TYPE_PARAMETER_STRUCT);
                g_value_set_static_boxed (&val_struct, g_ptr_array_index (parameters, i));

                res = dbus_g_type_struct_get (&val_struct,
                                              0, &prop_name,
                                              1, &prop_val,
                                              G_MAXUINT);
                if (! res) {
                        g_debug ("Unable to extract parameter input");
                        goto cont;
                }

                if (prop_name == NULL) {
                        g_debug ("Skipping NULL parameter");
                        goto cont;
                }

                if (strcmp (prop_name, name) != 0) {
                        goto cont;
                }

                switch (prop_type) {
                case PROP_STRING:
                        if (value != NULL) {
                                *value = g_value_dup_string (prop_val);
                        }
                        break;
                case PROP_BOOLEAN:
                        if (value != NULL) {
                                *(gboolean *)value = g_value_get_boolean (prop_val);
                        }
                        break;
                default:
                        g_assert_not_reached ();
                        break;
                }

                ret = TRUE;

        cont:
                g_free (prop_name);
                if (prop_val != NULL) {
                        g_value_unset (prop_val);
                        g_free (prop_val);
                }
        }

        return ret;
}

static gboolean
_verify_login_session_id_is_local (CkManager  *manager,
                                   const char *login_session_id)
{
        GHashTableIter iter;
        const char    *id;
        CkSession     *session;

        g_return_val_if_fail (CK_IS_MANAGER (manager), FALSE);

        /* If any local session exists for the given login session id
           then that means a trusted party has vouched for the
           original login */

        g_debug ("Looking for local sessions for login-session-id=%s", login_session_id);

        session = NULL;
        g_hash_table_iter_init (&iter, manager->priv->sessions);
        while (g_hash_table_iter_next (&iter, (gpointer *)&id, (gpointer *)&session)) {
                if (session != NULL) {
                        gboolean is_local;
                        char    *sessid;

                        sessid = NULL;
                        g_object_get (session,
                                      "login-session-id", &sessid,
                                      "is-local", &is_local,
                                      NULL);
                        if (g_strcmp0 (sessid, login_session_id) == 0 && is_local) {
                                g_debug ("CkManager: found is-local=true on %s", id);
                                return TRUE;
                        }
                }
        }

        return FALSE;
}

static void
add_param_boolean (GPtrArray  *parameters,
                   const char *key,
                   gboolean    value)
{
        GValue   val = { 0, };
        GValue   param_val = { 0, };

        g_value_init (&val, G_TYPE_BOOLEAN);
        g_value_set_boolean (&val, value);
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
verify_and_open_session_for_leader (CkManager             *manager,
                                    CkSessionLeader       *leader,
                                    GPtrArray             *parameters,
                                    DBusGMethodInvocation *context)
{
        /* Only allow a local session if originating from an existing
           local session.  Effectively this means that only trusted
           parties can create local sessions. */

        g_debug ("CkManager: verifying session for leader");

        if (parameters != NULL && ! _get_parameter (parameters, "is-local", PROP_BOOLEAN, NULL)) {
                gboolean is_local;
                char    *login_session_id;

                g_debug ("CkManager: is-local has not been set, will inherit from existing login-session-id if available");

                is_local = FALSE;

                if (_get_parameter (parameters, "login-session-id", PROP_STRING, (gpointer *) &login_session_id)) {
                        is_local = _verify_login_session_id_is_local (manager, login_session_id);
                        g_debug ("CkManager: found is-local=%s", is_local ? "true" : "false");
                }

                add_param_boolean (parameters, "is-local", is_local);
        }

        open_session_for_leader (manager,
                                 leader,
                                 parameters,
                                 context);
}

static void
collect_parameters_cb (CkSessionLeader       *leader,
                       GPtrArray             *parameters,
                       DBusGMethodInvocation *context,
                       CkManager             *manager)
{
        if (parameters == NULL) {
                GError *error;
                error = g_error_new (CK_MANAGER_ERROR,
                                     CK_MANAGER_ERROR_GENERAL,
                                     "Unable to get information about the calling process");
                dbus_g_method_return_error (context, error);
                g_error_free (error);
                return;
        }

        verify_and_open_session_for_leader (manager,
                                            leader,
                                            parameters,
                                            context);
}

static void
generate_session_for_leader (CkManager             *manager,
                             CkSessionLeader       *leader,
                             DBusGMethodInvocation *context)
{
        gboolean res;

        res = ck_session_leader_collect_parameters (leader,
                                                    context,
                                                    (CkSessionLeaderDoneFunc)collect_parameters_cb,
                                                    manager);
        if (! res) {
                GError *error;
                error = g_error_new (CK_MANAGER_ERROR,
                                     CK_MANAGER_ERROR_GENERAL,
                                     "Unable to get information about the calling process");
                dbus_g_method_return_error (context, error);
                g_error_free (error);
        }
}

static gboolean
create_session_for_sender (CkManager             *manager,
                           const char            *sender,
                           const GPtrArray       *parameters,
                           DBusGMethodInvocation *context)
{
        pid_t           pid;
        uid_t           uid;
        gboolean        res;
        char            *cookie;
        char            *ssid;
        CkSessionLeader *leader;

        g_debug ("CkManager: create session for sender: %s", sender);

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

        leader = ck_session_leader_new ();
        ck_session_leader_set_uid (leader, uid);
        ck_session_leader_set_pid (leader, pid);
        ck_session_leader_set_service_name (leader, sender);
        ck_session_leader_set_session_id (leader, ssid);
        ck_session_leader_set_cookie (leader, cookie);
        ck_session_leader_set_override_parameters (leader, parameters);

        /* need to store the leader info first so the pending request can be revoked */
        g_hash_table_insert (manager->priv->leaders,
                             g_strdup (cookie),
                             g_object_ref (leader));

        generate_session_for_leader (manager,
                                     leader,
                                     context);

        g_free (cookie);
        g_free (ssid);
        g_object_unref (leader);

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
        gboolean         res;
        char            *sender;
        uid_t            calling_uid;
        pid_t            calling_pid;
        CkProcessStat   *stat;
        char            *ssid;
        CkSession       *session;
        CkSessionLeader *leader;
        GError          *local_error;

        ssid = NULL;

        g_debug ("CkManager: get session for cookie");

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
                g_debug ("CkManager: Unable to lookup caller info - failing");
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

                g_debug ("CkManager: Unable to lookup info for caller - failing");

                return FALSE;
        }

        /* FIXME: should we restrict this by uid? */

        ck_process_stat_free (stat);

        leader = g_hash_table_lookup (manager->priv->leaders, cookie);
        if (leader == NULL) {
                GError *error;
                error = g_error_new (CK_MANAGER_ERROR,
                                     CK_MANAGER_ERROR_GENERAL,
                                     _("Unable to find session for cookie"));
                dbus_g_method_return_error (context, error);
                g_error_free (error);
                g_debug ("CkManager: Unable to lookup cookie for caller - failing");
                return FALSE;
        }

        session = g_hash_table_lookup (manager->priv->sessions, ck_session_leader_peek_session_id (leader));
        if (session == NULL) {
                GError *error;
                error = g_error_new (CK_MANAGER_ERROR,
                                     CK_MANAGER_ERROR_GENERAL,
                                     _("Unable to find session for cookie"));
                dbus_g_method_return_error (context, error);
                g_error_free (error);
                g_debug ("CkManager: Unable to lookup session for cookie - failing");
                return FALSE;
        }

        ck_session_get_id (session, &ssid, NULL);

        g_debug ("CkManager: Found session '%s'", ssid);

        dbus_g_method_return (context, ssid);

        g_free (ssid);

        return TRUE;
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
        char          *cookie;

        sender = dbus_g_method_get_sender (context);

        g_debug ("CkManager: get session for unix process: %u", pid);

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

        cookie = get_cookie_for_pid (manager, pid);
        if (cookie == NULL) {
                GError *error;

                g_debug ("CkManager: unable to lookup session for unix process: %u", pid);

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

        g_debug ("CkManager: get current session");

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
        CkSession       *orig_session;
        char            *orig_ssid;
        CkSessionLeader *leader;
        char            *sid;
        gboolean         res;
        gboolean         ret;

        ret = FALSE;
        orig_ssid = NULL;
        orig_session = NULL;

        g_debug ("Removing session for cookie: %s", cookie);

        leader = g_hash_table_lookup (manager->priv->leaders, cookie);

        if (leader == NULL) {
                g_set_error (error,
                             CK_MANAGER_ERROR,
                             CK_MANAGER_ERROR_GENERAL,
                             "Unable to find session for cookie");
                goto out;
        }

        /* Need to get the original key/value */
        res = g_hash_table_lookup_extended (manager->priv->sessions,
                                            ck_session_leader_peek_session_id (leader),
                                            (gpointer *)&orig_ssid,
                                            (gpointer *)&orig_session);
        if (! res) {
                g_set_error (error,
                             CK_MANAGER_ERROR,
                             CK_MANAGER_ERROR_GENERAL,
                             "Unable to find session for cookie");
                goto out;
        }

        /* Must keep a reference to the session in the manager until
         * all events for seats are cleared.  So don't remove
         * or steal the session from the master list until
         * it is removed from all seats.  Otherwise, event logging
         * for seat removals doesn't work.
         */

        /* remove from seat */
        sid = NULL;
        ck_session_get_seat_id (orig_session, &sid, NULL);
        if (sid != NULL) {
                CkSeat *seat;
                seat = g_hash_table_lookup (manager->priv->seats, sid);
                if (seat != NULL) {
                        CkSeatKind kind;

                        ck_seat_remove_session (seat, orig_session, NULL);

                        kind = CK_SEAT_KIND_STATIC;
                        /* if dynamic seat has no sessions then remove it */
                        ck_seat_get_kind (seat, &kind, NULL);
                        if (kind == CK_SEAT_KIND_DYNAMIC) {
                                remove_seat (manager, seat);
                        }
                }
        }
        g_free (sid);

        /* Remove the session from the list but don't call
         * unref until we are done with it */
        g_hash_table_steal (manager->priv->sessions,
                            ck_session_leader_peek_session_id (leader));

        ck_manager_dump (manager);

        manager_update_system_idle_hint (manager);

        ret = TRUE;
 out:
        if (orig_session != NULL) {
                g_object_unref (orig_session);
        }
        g_free (orig_ssid);

        return ret;
}

static gboolean
paranoia_check_is_cookie_owner (CkManager  *manager,
                                const char *cookie,
                                uid_t       calling_uid,
                                pid_t       calling_pid,
                                GError    **error)
{
        CkSessionLeader *leader;

        if (cookie == NULL) {
                g_set_error (error,
                             CK_MANAGER_ERROR,
                             CK_MANAGER_ERROR_GENERAL,
                             "No cookie specified");
                return FALSE;
        }

        leader = g_hash_table_lookup (manager->priv->leaders, cookie);
        if (leader == NULL) {
                g_set_error (error,
                             CK_MANAGER_ERROR,
                             CK_MANAGER_ERROR_GENERAL,
                             _("Unable to find session for cookie"));
                return FALSE;
        }

        if (ck_session_leader_get_uid (leader) != calling_uid) {
                g_set_error (error,
                             CK_MANAGER_ERROR,
                             CK_MANAGER_ERROR_GENERAL,
                             _("User ID does not match the owner of cookie"));
                return FALSE;

        }

        /* do we want to restrict to the same process? */
        if (ck_session_leader_get_pid (leader) != calling_pid) {
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
                              CkSessionLeader  *leader,
                              RemoveLeaderData *data)
{
        const char *name;

        g_assert (leader != NULL);
        g_assert (data->service_name != NULL);

        name = ck_session_leader_peek_service_name (leader);
        if (strcmp (name, data->service_name) == 0) {
                remove_session_for_cookie (data->manager, cookie, NULL);
                ck_session_leader_cancel (leader);
                return TRUE;
        }

        return FALSE;
}

static void
remove_sessions_for_connection (CkManager  *manager,
                                const char *service_name)
{
        RemoveLeaderData data;

        data.service_name = service_name;
        data.manager = manager;

        g_debug ("Removing sessions for service name: %s", service_name);

        g_hash_table_foreach_remove (manager->priv->leaders,
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

static void
polkit_authority_get_cb (GObject *source_object,
                         GAsyncResult *res,
                         gpointer user_data)
{
        CkManager *manager = CK_MANAGER (user_data);

#ifdef HAVE_POLKIT
        manager->priv->pol_ctx = polkit_authority_get_finish (res, NULL);
#endif
}

static gboolean
register_manager (CkManager *manager)
{
        GError *error = NULL;

#ifdef HAVE_POLKIT
        polkit_authority_get_async (NULL, polkit_authority_get_cb, manager);
#endif

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
                              g_cclosure_marshal_VOID__BOXED,
                              G_TYPE_NONE,
                              1, DBUS_TYPE_G_OBJECT_PATH);
        signals [SEAT_REMOVED] =
                g_signal_new ("seat-removed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CkManagerClass, seat_removed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__BOXED,
                              G_TYPE_NONE,
                              1, DBUS_TYPE_G_OBJECT_PATH);
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
        signals [PREPARE_FOR_SHUTDOWN] =
                g_signal_new ("prepare-for-shutdown",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CkManagerClass, prepare_for_shutdown),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__BOOLEAN,
                              G_TYPE_NONE,
                              1, G_TYPE_BOOLEAN);
        signals [PREPARE_FOR_SLEEP] =
                g_signal_new ("prepare-for-sleep",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CkManagerClass, prepare_for_sleep),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__BOOLEAN,
                              G_TYPE_NONE,
                              1, G_TYPE_BOOLEAN);

        dbus_g_object_type_install_info (CK_TYPE_MANAGER, &dbus_glib_ck_manager_object_info);
        dbus_g_error_domain_register (CK_MANAGER_ERROR, NULL, CK_MANAGER_TYPE_ERROR);

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
        guint uid = console_kit_session_get_unix_user (CONSOLE_KIT_SESSION (session));

        if (uid == data->uid) {
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
listify_session_ids (char       *id,
                     CkSession  *session,
                     GPtrArray **array)
{
        g_ptr_array_add (*array, g_strdup (id));
}

gboolean
ck_manager_get_sessions (CkManager  *manager,
                         GPtrArray **sessions,
                         GError    **error)
{
        g_return_val_if_fail (CK_IS_MANAGER (manager), FALSE);

        if (sessions == NULL) {
                return FALSE;
        }

        *sessions = g_ptr_array_new ();
        g_hash_table_foreach (manager->priv->sessions, (GHFunc)listify_session_ids, sessions);

        return TRUE;
}

static void
add_seat_for_file (CkManager  *manager,
                   const char *filename)
{
        char   *sid;
        CkSeat *seat;

        sid = generate_seat_id (manager);

        seat = ck_seat_new_from_file (sid, filename, NULL);

        if (seat == NULL) {
                return;
        }

        connect_seat_signals (manager, seat);
        if (!ck_seat_register (seat)) {
                /* returns false if connection to bus fails */
                disconnect_seat_signals (manager, seat);
                g_object_unref (seat);
                g_free (sid);
                return;
        }

        g_hash_table_insert (manager->priv->seats, sid, seat);

        g_debug ("Added seat: %s", sid);

        ck_manager_dump (manager);
        ck_seat_run_programs (seat, NULL, NULL, "seat_added");

        g_debug ("Emitting seat-added: %s", sid);
        g_signal_emit (manager, signals [SEAT_ADDED], 0, sid);

        log_seat_added_event (manager, seat);
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
                if (g_str_has_suffix (file, ".seat")) {
                        char *path;
                        path = g_build_filename (CK_SEAT_DIR, file, NULL);
                        add_seat_for_file (manager, path);
                        g_free (path);
                }
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
                                                        (GDestroyNotify) g_object_unref);

        manager->priv->logger = ck_event_logger_new (LOG_FILE);

        manager->priv->inhibit_manager = ck_inhibit_manager_get ();

        manager->priv->system_action_idle_delay = 4 * 1000;
        manager->priv->system_action_idle_id = 0;

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
        if (manager->priv->bus_proxy != NULL) {
                g_object_unref (manager->priv->bus_proxy);
        }

        if (manager->priv->logger != NULL) {
                g_object_unref (manager->priv->logger);
        }

        if (manager->priv->inhibit_manager != NULL) {
                g_object_unref (manager->priv->inhibit_manager);
        }

        if (manager->priv->system_action_idle_id != 0) {
                g_source_remove (manager->priv->system_action_idle_id);
        }

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
