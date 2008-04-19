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
#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#ifdef HAVE_POLKIT
#include <polkit/polkit.h>
#endif

#ifdef ENABLE_RBAC_SHUTDOWN
#include <auth_attr.h>
#include <secdb.h>
#endif

#include "ck-manager.h"
#include "ck-manager-glue.h"
#include "ck-seat.h"
#include "ck-session-leader.h"
#include "ck-session.h"
#include "ck-marshal.h"
#include "ck-event-logger.h"

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
        PolKitContext   *pol_ctx;
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
};

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
        const char *filename = LOCALSTATEDIR "/run/ConsoleKit/database";
        const char *filename_tmp = LOCALSTATEDIR "/run/ConsoleKit/database~";

        if (manager == NULL) {
                return;
        }

        /* always make sure we have a directory */
        errno = 0;
        res = g_mkdir_with_parents (LOCALSTATEDIR "/run/ConsoleKit",
                                    S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
        if (res < 0) {
                g_warning ("Unable to create directory %s (%s)",
                           LOCALSTATEDIR "/run/ConsoleKit",
                           g_strerror (errno));
                return;
        }

        fd = g_open (filename_tmp, O_CREAT | O_WRONLY, 0600);
        if (fd == -1) {
                g_warning ("Cannot create file %s: %s", filename_tmp, g_strerror (errno));
                goto error;
        }

        if (! do_dump (manager, fd)) {
                g_warning ("Cannot write to file %s", filename_tmp);
                close (fd);
                goto error;
        }
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

static void
log_system_stop_event (CkManager  *manager)
{
        CkLogEvent         event;
        gboolean           res;
        GError            *error;

        memset (&event, 0, sizeof (CkLogEvent));

        event.type = CK_LOG_EVENT_SYSTEM_STOP;
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
log_system_restart_event (CkManager  *manager)
{
        CkLogEvent         event;
        gboolean           res;
        GError            *error;

        memset (&event, 0, sizeof (CkLogEvent));

        event.type = CK_LOG_EVENT_SYSTEM_RESTART;
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

static CkSession *
get_session_for_unix_process (CkManager *manager,
                              guint      pid)
{
        CkSessionLeader *leader;
        CkSession       *session;
        char            *cookie;

        session = NULL;
        leader = NULL;

        cookie = get_cookie_for_pid (manager, pid);
        if (cookie == NULL) {
                goto out;
        }

        leader = g_hash_table_lookup (manager->priv->leaders, cookie);
        if (leader == NULL) {
                goto out;
        }

        session = g_hash_table_lookup (manager->priv->sessions, ck_session_leader_peek_session_id (leader));

 out:
        g_free (cookie);

        return session;
}

#ifdef HAVE_POLKIT
static PolKitSession *
new_polkit_session_from_session (CkManager *manager,
                                 CkSession *ck_session)
{
        PolKitSession *pk_session;
        PolKitSeat    *pk_seat;
        uid_t          uid;
        gboolean       is_active;
        gboolean       is_local;
        char          *sid;
        char          *ssid;
        char          *remote_host;

        sid = NULL;
        ssid = NULL;
        remote_host = NULL;

        ck_session_get_seat_id (ck_session, &sid, NULL);

        g_object_get (ck_session,
                      "active", &is_active,
                      "is-local", &is_local,
                      "id", &ssid,
                      "unix-user", &uid,
                      "remote-host-name", &remote_host,
                      NULL);

        pk_session = polkit_session_new ();
        if (pk_session == NULL) {
                goto out;
        }
        if (!polkit_session_set_uid (pk_session, uid)) {
                polkit_session_unref (pk_session);
                pk_session = NULL;
                goto out;
        }
        if (!polkit_session_set_ck_objref (pk_session, ssid)) {
                polkit_session_unref (pk_session);
                pk_session = NULL;
                goto out;
        }
        if (!polkit_session_set_ck_is_active (pk_session, is_active)) {
                polkit_session_unref (pk_session);
                pk_session = NULL;
                goto out;
        }
        if (!polkit_session_set_ck_is_local (pk_session, is_local)) {
                polkit_session_unref (pk_session);
                pk_session = NULL;
                goto out;
        }
        if (!is_local) {
                if (!polkit_session_set_ck_remote_host (pk_session, remote_host)) {
                        polkit_session_unref (pk_session);
                        pk_session = NULL;
                        goto out;
                }

        }


        pk_seat = polkit_seat_new ();
        if (pk_seat == NULL) {
                polkit_session_unref (pk_session);
                pk_session = NULL;
                goto out;
        }
        if (!polkit_seat_set_ck_objref (pk_seat, sid)) {
                polkit_seat_unref (pk_seat);
                pk_seat = NULL;
                polkit_session_unref (pk_session);
                pk_session = NULL;
                goto out;
        }
        if (!polkit_seat_validate (pk_seat)) {
                polkit_seat_unref (pk_seat);
                pk_seat = NULL;
                polkit_session_unref (pk_session);
                pk_session = NULL;
                goto out;
        }

        if (!polkit_session_set_seat (pk_session, pk_seat)) {
                polkit_seat_unref (pk_seat);
                pk_seat = NULL;
                polkit_session_unref (pk_session);
                pk_session = NULL;
                goto out;
        }
        polkit_seat_unref (pk_seat); /* session object now owns this object */
        pk_seat = NULL;

        if (!polkit_session_validate (pk_session)) {
                polkit_session_unref (pk_session);
                pk_session = NULL;
                goto out;
        }

out:
        g_free (ssid);
        g_free (sid);
        g_free (remote_host);

        return pk_session;
}

static PolKitCaller *
new_polkit_caller_from_dbus_name (CkManager  *manager,
                                  const char *dbus_name)
{
        PolKitCaller *caller;
        pid_t pid;
        uid_t uid;
        char *selinux_context;
        PolKitSession *pk_session;
        DBusMessage *message;
        DBusMessage *reply;
        DBusMessageIter iter;
        DBusMessageIter sub_iter;
        char *str;
        int num_elems;
        DBusConnection *con;
        DBusError       error;
        CkSession      *ck_session;

        dbus_error_init (&error);

        con = dbus_g_connection_get_connection (manager->priv->connection);

        g_return_val_if_fail (con != NULL, NULL);
        g_return_val_if_fail (dbus_name != NULL, NULL);

        selinux_context = NULL;

        caller = NULL;
        ck_session = NULL;
        pk_session = NULL;

        uid = dbus_bus_get_unix_user (con, dbus_name, &error);
        if (dbus_error_is_set (&error)) {
                g_warning ("Could not get uid for connection: %s %s",
                           error.name,
                           error.message);
                dbus_error_free (&error);
                goto out;
        }

        message = dbus_message_new_method_call ("org.freedesktop.DBus",
                                                "/org/freedesktop/DBus/Bus",
                                                "org.freedesktop.DBus",
                                                "GetConnectionUnixProcessID");
        dbus_message_iter_init_append (message, &iter);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &dbus_name);
        reply = dbus_connection_send_with_reply_and_block (con, message, -1, &error);

        if (reply == NULL || dbus_error_is_set (&error)) {
                g_warning ("Error doing GetConnectionUnixProcessID on Bus: %s: %s",
                           error.name,
                           error.message);
                dbus_message_unref (message);
                if (reply != NULL) {
                        dbus_message_unref (reply);
                }
                dbus_error_free (&error);
                goto out;
        }
        dbus_message_iter_init (reply, &iter);
        dbus_message_iter_get_basic (&iter, &pid);
        dbus_message_unref (message);
        dbus_message_unref (reply);

        message = dbus_message_new_method_call ("org.freedesktop.DBus",
                                                "/org/freedesktop/DBus/Bus",
                                                "org.freedesktop.DBus",
                                                "GetConnectionSELinuxSecurityContext");
        dbus_message_iter_init_append (message, &iter);
        dbus_message_iter_append_basic (&iter, DBUS_TYPE_STRING, &dbus_name);
        reply = dbus_connection_send_with_reply_and_block (con, message, -1, &error);
        /* SELinux might not be enabled */
        if (dbus_error_is_set (&error) &&
            strcmp (error.name, "org.freedesktop.DBus.Error.SELinuxSecurityContextUnknown") == 0) {
                dbus_message_unref (message);
                if (reply != NULL) {
                        dbus_message_unref (reply);
                }
                dbus_error_init (&error);
        } else if (reply == NULL || dbus_error_is_set (&error)) {
                g_warning ("Error doing GetConnectionSELinuxSecurityContext on Bus: %s: %s", error.name, error.message);
                dbus_message_unref (message);
                if (reply != NULL) {
                        dbus_message_unref (reply);
                }
                goto out;
        } else {
                /* TODO: verify signature */
                dbus_message_iter_init (reply, &iter);
                dbus_message_iter_recurse (&iter, &sub_iter);
                dbus_message_iter_get_fixed_array (&sub_iter, (void *) &str, &num_elems);
                if (str != NULL && num_elems > 0) {
                        selinux_context = g_strndup (str, num_elems);
                }
                dbus_message_unref (message);
                dbus_message_unref (reply);
        }

        ck_session = get_session_for_unix_process (manager, pid);
        if (ck_session == NULL) {
                /* OK, this is not a catastrophe; just means the caller is not a
                 * member of any session or that ConsoleKit is not available..
                 */
                goto not_in_session;
        }

        pk_session = new_polkit_session_from_session (manager, ck_session);
        if (pk_session == NULL) {
                g_warning ("Got a session but couldn't construct polkit session object!");
                goto out;
        }
        if (!polkit_session_validate (pk_session)) {
                polkit_session_unref (pk_session);
                pk_session = NULL;
                goto out;
        }

not_in_session:

        caller = polkit_caller_new ();
        if (caller == NULL) {
                if (pk_session != NULL) {
                        polkit_session_unref (pk_session);
                        pk_session = NULL;
                }
                goto out;
        }

        if (!polkit_caller_set_dbus_name (caller, dbus_name)) {
                if (pk_session != NULL) {
                        polkit_session_unref (pk_session);
                        pk_session = NULL;
                }
                polkit_caller_unref (caller);
                caller = NULL;
                goto out;
        }
        if (!polkit_caller_set_uid (caller, uid)) {
                if (pk_session != NULL) {
                        polkit_session_unref (pk_session);
                        pk_session = NULL;
                }
                polkit_caller_unref (caller);
                caller = NULL;
                goto out;
        }
        if (!polkit_caller_set_pid (caller, pid)) {
                if (pk_session != NULL) {
                        polkit_session_unref (pk_session);
                        pk_session = NULL;
                }
                polkit_caller_unref (caller);
                caller = NULL;
                goto out;
        }
        if (selinux_context != NULL) {
                if (!polkit_caller_set_selinux_context (caller, selinux_context)) {
                        if (pk_session != NULL) {
                                polkit_session_unref (pk_session);
                                pk_session = NULL;
                        }
                        polkit_caller_unref (caller);
                        caller = NULL;
                        goto out;
                }
        }
        if (pk_session != NULL) {
                if (!polkit_caller_set_ck_session (caller, pk_session)) {
                        if (pk_session != NULL) {
                                polkit_session_unref (pk_session);
                                pk_session = NULL;
                        }
                        polkit_caller_unref (caller);
                        caller = NULL;
                        goto out;
                }
                polkit_session_unref (pk_session); /* caller object now own this object */
                pk_session = NULL;
        }

        if (!polkit_caller_validate (caller)) {
                polkit_caller_unref (caller);
                caller = NULL;
                goto out;
        }

out:
        g_free (selinux_context);

        return caller;
}

static gboolean
_check_polkit_for_action (CkManager             *manager,
                          DBusGMethodInvocation *context,
                          const char            *action)
{
        const char   *sender;
        GError       *error;
        DBusError     dbus_error;
        PolKitCaller *pk_caller;
        PolKitAction *pk_action;
        PolKitResult  pk_result;

        error = NULL;

        g_debug ("constructing polkit data");

        /* Check that caller is privileged */
        sender = dbus_g_method_get_sender (context);
        dbus_error_init (&dbus_error);

        pk_caller = new_polkit_caller_from_dbus_name (manager, sender);
        if (pk_caller == NULL) {
                error = g_error_new (CK_MANAGER_ERROR,
                                     CK_MANAGER_ERROR_GENERAL,
                                     "Error getting information about caller: %s: %s",
                                     dbus_error.name,
                                     dbus_error.message);
                dbus_error_free (&dbus_error);
                dbus_g_method_return_error (context, error);
                g_error_free (error);
                return FALSE;
        }

        pk_action = polkit_action_new ();
        polkit_action_set_action_id (pk_action, action);

        g_debug ("checking if caller %s is authorized", sender);

        /* this version crashes if error is used */
        pk_result = polkit_context_is_caller_authorized (manager->priv->pol_ctx,
                                                         pk_action,
                                                         pk_caller,
                                                         TRUE,
                                                         NULL);
        g_debug ("answer is: %s", (pk_result == POLKIT_RESULT_YES) ? "yes" : "no");

        polkit_caller_unref (pk_caller);
        polkit_action_unref (pk_action);

        if (pk_result != POLKIT_RESULT_YES) {
                error = g_error_new (CK_MANAGER_ERROR,
                                     CK_MANAGER_ERROR_NOT_PRIVILEGED,
                                     "Not privileged for action: %s %s",
                                     action,
                                     polkit_result_to_string_representation (pk_result));
                dbus_error_free (&dbus_error);
                dbus_g_method_return_error (context, error);
                g_error_free (error);
                return FALSE;
        }

        return TRUE;
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
                      "session-type", session_type,
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
                        DBusGMethodInvocation *context)
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
            !chkauthattr (RBAC_SHUTDOWN_KEY, username)) {
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
        return res;
}
#endif

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
        gboolean    ret;
        gboolean    res;
        const char *action;
        GError     *error;

        ret = FALSE;

        if (get_system_num_users (manager) > 1) {
                action = "org.freedesktop.consolekit.system.restart-multiple-users";
        } else {
                action = "org.freedesktop.consolekit.system.restart";
        }

        g_debug ("ConsoleKit Restart: %s", action);

#ifdef HAVE_POLKIT
        res = _check_polkit_for_action (manager, context, action);
        if (! res) {
                goto out;
        }
#endif

#ifdef ENABLE_RBAC_SHUTDOWN
        if (! check_rbac_permissions (manager, context)) {
                goto out;
        }
#endif

        g_debug ("ConsoleKit preforming Restart: %s", action);

        log_system_restart_event (manager);

        error = NULL;
        res = g_spawn_command_line_async (PREFIX "/lib/ConsoleKit/scripts/ck-system-restart",
                                          &error);
        if (! res) {
                GError *new_error;

                g_warning ("Unable to restart system: %s", error->message);

                new_error = g_error_new (CK_MANAGER_ERROR,
                                         CK_MANAGER_ERROR_GENERAL,
                                         "Unable to restart system: %s", error->message);
                dbus_g_method_return_error (context, new_error);
                g_error_free (new_error);

                g_error_free (error);
        } else {
                ret = TRUE;
                dbus_g_method_return (context);
        }

 out:

        return ret;
}

gboolean
ck_manager_stop (CkManager             *manager,
                 DBusGMethodInvocation *context)
{
        gboolean    ret;
        gboolean    res;
        const char *action;
        GError     *error;

        ret = TRUE;

        if (get_system_num_users (manager) > 1) {
                action = "org.freedesktop.consolekit.system.stop-multiple-users";
        } else {
                action = "org.freedesktop.consolekit.system.stop";
        }

#ifdef HAVE_POLKIT
        res = _check_polkit_for_action (manager, context, action);
        if (! res) {
                goto out;
        }
#endif

#ifdef ENABLE_RBAC_SHUTDOWN
        if (!check_rbac_permissions (manager, context))
                goto out;
#endif

        g_debug ("Stopping system");

        log_system_stop_event (manager);

        error = NULL;
        res = g_spawn_command_line_async (PREFIX "/lib/ConsoleKit/scripts/ck-system-stop",
                                          &error);
        if (! res) {
                GError *new_error;

                g_warning ("Unable to stop system: %s", error->message);

                new_error = g_error_new (CK_MANAGER_ERROR,
                                         CK_MANAGER_ERROR_GENERAL,
                                         "Unable to stop system: %s", error->message);
                dbus_g_method_return_error (context, new_error);
                g_error_free (new_error);

                g_error_free (error);
        } else {
                ret = TRUE;
                dbus_g_method_return (context);
        }

 out:
        return ret;
}

static void
on_seat_active_session_changed (CkSeat     *seat,
                                const char *ssid,
                                CkManager  *manager)
{
        ck_manager_dump (manager);
        log_seat_active_session_changed_event (manager, seat, ssid);
}

static void
on_seat_session_added (CkSeat     *seat,
                       const char *ssid,
                       CkManager  *manager)
{
        ck_manager_dump (manager);
        log_seat_session_added_event (manager, seat, ssid);
}

static void
on_seat_session_removed (CkSeat     *seat,
                         const char *ssid,
                         CkManager  *manager)
{
        ck_manager_dump (manager);
        log_seat_session_removed_event (manager, seat, ssid);
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
        g_signal_connect (seat, "active-session-changed", G_CALLBACK (on_seat_active_session_changed), manager);
        g_signal_connect (seat, "session-added", G_CALLBACK (on_seat_session_added), manager);
        g_signal_connect (seat, "session-removed", G_CALLBACK (on_seat_session_removed), manager);
        g_signal_connect (seat, "device-added", G_CALLBACK (on_seat_device_added), manager);
        g_signal_connect (seat, "device-removed", G_CALLBACK (on_seat_device_removed), manager);
}

static void
disconnect_seat_signals (CkManager *manager,
                         CkSeat    *seat)
{
        g_signal_handlers_disconnect_by_func (seat, on_seat_active_session_changed, manager);
        g_signal_handlers_disconnect_by_func (seat, on_seat_session_added, manager);
        g_signal_handlers_disconnect_by_func (seat, on_seat_session_removed, manager);
        g_signal_handlers_disconnect_by_func (seat, on_seat_device_added, manager);
        g_signal_handlers_disconnect_by_func (seat, on_seat_device_removed, manager);
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

        connect_seat_signals (manager, seat);

        g_hash_table_insert (manager->priv->seats, sid, seat);

        g_debug ("Added seat: %s kind:%d", sid, kind);

        ck_manager_dump (manager);

        g_signal_emit (manager, signals [SEAT_ADDED], 0, sid);

        log_seat_added_event (manager, seat);

 out:
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
        g_signal_connect (session, "idle-hint-changed",
                          G_CALLBACK (session_idle_hint_changed),
                          manager);

        g_object_unref (session);

        dbus_g_method_return (context, cookie);
}

static void
verify_and_open_session_for_leader (CkManager             *manager,
                                    CkSessionLeader       *leader,
                                    const GPtrArray       *parameters,
                                    DBusGMethodInvocation *context)
{
        /* for now don't bother verifying since we protect OpenSessionWithParameters */
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

        leader = g_hash_table_lookup (manager->priv->leaders, cookie);
        if (leader == NULL) {
                GError *error;
                error = g_error_new (CK_MANAGER_ERROR,
                                     CK_MANAGER_ERROR_GENERAL,
                                     _("Unable to find session for cookie"));
                dbus_g_method_return_error (context, error);
                g_error_free (error);
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
                return FALSE;
        }

        ck_session_get_id (session, &ssid, NULL);

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

#ifdef HAVE_POLKIT
static gboolean
pk_io_watch_have_data (GIOChannel  *channel,
                       GIOCondition condition,
                       gpointer     user_data)
{
        int            fd;
        PolKitContext *pk_context = user_data;

        fd = g_io_channel_unix_get_fd (channel);
        polkit_context_io_func (pk_context, fd);
        return TRUE;
}

static int
pk_io_add_watch (PolKitContext *pk_context,
                 int            fd)
{
        guint       id = 0;
        GIOChannel *channel;

        channel = g_io_channel_unix_new (fd);
        if (channel == NULL) {
                goto out;
        }

        id = g_io_add_watch (channel, G_IO_IN, pk_io_watch_have_data, pk_context);
        if (id == 0) {
                g_io_channel_unref (channel);
                goto out;
        }
        g_io_channel_unref (channel);

out:
        return id;
}

static void
pk_io_remove_watch (PolKitContext *pk_context,
                    int            watch_id)
{
        g_source_remove (watch_id);
}
#endif

static gboolean
register_manager (CkManager *manager)
{
        GError *error = NULL;

#ifdef HAVE_POLKIT
        manager->priv->pol_ctx = polkit_context_new ();
        polkit_context_set_io_watch_functions (manager->priv->pol_ctx, pk_io_add_watch, pk_io_remove_watch);
        if (! polkit_context_init (manager->priv->pol_ctx, NULL)) {
                g_critical ("cannot initialize libpolkit");
                return FALSE;
        }
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

        seat = ck_seat_new_from_file (sid, filename);
        if (seat == NULL) {
                /* returns null if connection to bus fails */
                g_free (sid);
                return;
        }

        connect_seat_signals (manager, seat);

        g_hash_table_insert (manager->priv->seats, sid, seat);

        g_debug ("Added seat: %s", sid);

        ck_manager_dump (manager);

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
                                                        (GDestroyNotify) g_object_unref);

        manager->priv->logger = ck_event_logger_new (LOG_FILE);

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
