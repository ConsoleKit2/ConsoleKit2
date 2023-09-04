/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006-2008 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2023 Serenity Cybersecurity, LLC <license@futurecrew.ru>
 *                    Author: Gleb Popov <arrowd@FreeBSD.org>
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
#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#if defined HAVE_POLKIT
#include <polkit/polkit.h>
#elif defined ENABLE_RBAC_SHUTDOWN
#include <auth_attr.h>
#include <secdb.h>
#endif

#include "ck-manager.h"
#include "ck-seat.h"
#include "ck-session-leader.h"
#include "ck-session.h"
#include "ck-marshal.h"
#include "ck-event-logger.h"
#include "ck-inhibit-manager.h"
#include "ck-inhibit.h"
#include "ck-sysdeps.h"
#include "ck-process-group.h"

#define CK_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CK_TYPE_MANAGER, CkManagerPrivate))

#define CK_SEAT_DIR            SYSCONFDIR "/ConsoleKit/seats.d"
#define LOG_FILE               LOCALSTATEDIR "/log/ConsoleKit/history"

typedef enum {
        PREPARE_FOR_SHUTDOWN,
        PREPARE_FOR_SLEEP,
        LAST_SIGNAL
} SIGNALS;

typedef struct
{
        CkManager             *manager;
        GDBusMethodInvocation *context;
        const gchar           *command;
        CkLogEventType         event_type;
        const gchar           *description;
        SIGNALS                signal;
} SystemActionData;

struct CkManagerPrivate
{
#ifdef HAVE_POLKIT
        PolkitAuthority *pol_ctx;
#endif

        GHashTable      *seats;
        GHashTable      *sessions;
        GHashTable      *leaders;

        GDBusProxy      *bus_proxy;
        GDBusConnection *connection;
        CkEventLogger   *logger;

        guint            name_owner_id;

        guint32          session_serial;
        guint32          seat_serial;

        gboolean         system_idle_hint;
        GTimeVal         system_idle_since_hint;

        /* How long to delay after emitting the PREPARE_FOR_SHUTDOWN or
         * PREPARE_FOR_SLEEP signal
         */
        guint            system_action_idle_delay;
        /* How long to delay after emitting the PREPARE_FOR_SHUTDOWN or
         * PREPARE_FOR_SLEEP signal with something holding the delay
         * inhibit
         */
        guint            system_action_idle_inhibited_delay;
        /* The idle callback id so we can detect multiple attempts to
         * perform a system action at the same time
         */
        guint            system_action_idle_id;
        /* Now that the action data can be delayed and that delay can
         * be lifted sooner than system_action_idle_inhibited_delay,
         * we need to keep a pointer to the system action data so we
         * can manually call system_action_idle_cb
         */
        SystemActionData *system_action_data;

        CkInhibitManager *inhibit_manager;
};

static void     ck_manager_iface_init  (ConsoleKitManagerIface *iface);
static void     ck_manager_finalize    (GObject                *object);

static void     remove_sessions_for_connection (CkManager   *manager,
                                                const gchar *service_name);
static void     create_seats                   (CkManager *manager);
static gboolean session_is_real_user           (CkSession *session,
                                                char     **userp);

static gpointer manager_object = NULL;

G_DEFINE_TYPE_WITH_CODE (CkManager, ck_manager, CONSOLE_KIT_TYPE_MANAGER_SKELETON, G_IMPLEMENT_INTERFACE (CONSOLE_KIT_TYPE_MANAGER, ck_manager_iface_init));

static void
dump_manager_seat_iter (char      *id,
                        CkSeat    *seat,
                        GString   *str)
{
        char   *seat_id;
        GError *error;

        error = NULL;
        if (! ck_seat_get_id (seat, &seat_id, &error)) {
                g_warning ("Cannot get seat id from manager: %s", error->message);
                g_error_free (error);
        } else {
                /* ; is the seperator between elements in the list.
                 * per keyfile standards
                 */
                if (str->len > 0) {
                        g_string_append_c (str, ';');
                }
                g_string_append (str, seat_id);
                g_free (seat_id);
        }
}

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

typedef struct {
        GString *sessions;
        gboolean is_local;
} UserDataDump;

static void
dump_user_iter (guint        *uid,
                UserDataDump *data,
                GKeyFile     *key_file)
{
        gchar *user_group = NULL;
        gchar *session_list = NULL;

        TRACE ();

        if (data == NULL) {
                g_critical ("ck-manager: dump_user_iter: data == NULL");
                return;
        }

        if (uid == NULL) {
                g_critical ("ck-manager: dump_user_iter: uid == NULL");
                return;
        }

        user_group = g_strdup_printf ("User %u", *uid);
        if (user_group == NULL) {
                g_critical ("ck-manager: dump_user_iter: user_group == NULL, OOM");
                return;
        }

        session_list = g_string_free(data->sessions, FALSE);

        g_key_file_set_boolean (key_file, user_group, "is_local", data->is_local);
        g_key_file_set_string  (key_file, user_group, "sessions", session_list);

        g_free (user_group);
        g_free (session_list);
}

static void
collect_user_data_dump (const char *ssid,
                        CkSession  *session,
                        GHashTable *hash)
{
        char         *username;
        guint         uid;
        gboolean      is_local;
        UserDataDump *data;

        TRACE ();

        g_debug ("ssid %s", ssid);

        /* ensure we filter out DMs and stuff */
        if (session_is_real_user (session, &username)) {
                if (username != NULL) {
                        uid = console_kit_session_get_unix_user (CONSOLE_KIT_SESSION (session));
                        /* Try to get the existing data */
                        data = g_hash_table_lookup (hash, &uid);
                        if (data == NULL) {
                                data = g_new0 (UserDataDump, 1);
                                if (data == NULL) {
                                        g_critical ("ck-manager: collect_user_data_dump: OOM");
                                        return;
                                }

                                /* allocate space for our sessions string */
                                data->sessions = g_string_new(NULL);
                                if (data->sessions == NULL) {
                                        g_critical ("ck-manager: collect_user_data_dump: OOM");
                                        return;
                                }

                                /* place the data into the hash table */
                                g_hash_table_insert (hash, &uid, data);
                        }

                        if (data->sessions == NULL) {
                                g_critical ("ck-manager: collect_user_data_dump: failed sanity check");
                                return;
                        }

                        /* ; is the seperator between elements in the list.
                         * per keyfile standards
                         */
                        if (data->sessions->len > 0) {
                                g_string_append_c (data->sessions, ';');
                        }

                        /* add the new ssid to the list */
                        g_string_append (data->sessions, ssid);

                        /* update the is_local property, we set this to True
                         * if one seat/session is local */
                        is_local = console_kit_session_get_is_local (CONSOLE_KIT_SESSION (session));
                        if (is_local) {
                                data->is_local = is_local;
                        }
                }
        }
}

static void
dump_user_section (CkManager *manager,
                   GKeyFile *key_file)
{
        GHashTable      *users_hash;

        TRACE ();

        /* using direct since there is no g_uint_hash or g_uint_equal */
        users_hash = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);

        /* populate the users_hash filling in the user details */
        g_hash_table_foreach (manager->priv->sessions, (GHFunc)collect_user_data_dump, users_hash);

        /* now print out each entry in a user section */
        g_hash_table_foreach (users_hash, (GHFunc)dump_user_iter, key_file);

        g_hash_table_destroy (users_hash);
}

static gboolean
do_dump (CkManager *manager,
         int        fd)
{
        char     *str;
        char     *s;
        gsize     str_len;
        GString  *seats_string;
        GKeyFile *key_file;
        GError   *error;
        gboolean  ret;

        str = NULL;
        error = NULL;
        ret = FALSE;

        key_file = g_key_file_new ();

        /* Create the [seats] section listing all the seats */
        seats_string = g_string_new (NULL);
        g_hash_table_foreach (manager->priv->seats, (GHFunc) dump_manager_seat_iter, seats_string);
        s = g_string_free (seats_string, FALSE);
        g_key_file_set_string (key_file, "Seats", "seats", s);
        g_free (s);

        g_hash_table_foreach (manager->priv->seats, (GHFunc) dump_state_seat_iter, key_file);
        g_hash_table_foreach (manager->priv->sessions, (GHFunc) dump_state_session_iter, key_file);
        g_hash_table_foreach (manager->priv->leaders, (GHFunc) dump_state_leader_iter, key_file);

        dump_user_section (manager, key_file);

        str = g_key_file_to_data (key_file, &str_len, &error);
        g_key_file_free (key_file);
        if (str != NULL) {
                ssize_t written;

                written = 0;
                while ((size_t)written < str_len) {
                        ssize_t num_written;
                        num_written = write (fd, str + written, str_len - written);
                        if (num_written < 0) {
                                if (errno == EAGAIN || errno == EINTR) {
                                        continue;
                                } else {
                                        g_warning ("Error writing state file: %s", strerror (errno));
                                        goto out;
                                }
                        }
                        written += num_written;
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


static const GDBusErrorEntry ck_manager_error_entries[] =
{
        { CK_MANAGER_ERROR_FAILED,                  DBUS_MANAGER_INTERFACE ".Error.Failed" },
        { CK_MANAGER_ERROR_GENERAL,                 DBUS_MANAGER_INTERFACE ".Error.General" },
        { CK_MANAGER_ERROR_INSUFFICIENT_PERMISSION, DBUS_MANAGER_INTERFACE ".Error.InsufficientPermission" },
        { CK_MANAGER_ERROR_AUTHORIZATION_REQUIRED,  DBUS_MANAGER_INTERFACE ".Error.AuthorizationRequired" },
        { CK_MANAGER_ERROR_BUSY,                    DBUS_MANAGER_INTERFACE ".Error.Busy" },
        { CK_MANAGER_ERROR_NOT_SUPPORTED,           DBUS_MANAGER_INTERFACE ".Error.NotSupported" },
        { CK_MANAGER_ERROR_INHIBITED,               DBUS_MANAGER_INTERFACE ".Error.Inhibited" },
        { CK_MANAGER_ERROR_INVALID_INPUT,           DBUS_MANAGER_INTERFACE ".Error.InvalidInput" },
        { CK_MANAGER_ERROR_OOM,                     DBUS_MANAGER_INTERFACE ".Error.OutOfMemory" },
        { CK_MANAGER_ERROR_NO_SEATS,                DBUS_MANAGER_INTERFACE ".Error.NoSeats" },
        { CK_MANAGER_ERROR_NO_SESSIONS,             DBUS_MANAGER_INTERFACE ".Error.NoSessions" },
        { CK_MANAGER_ERROR_NOTHING_INHIBITED,       DBUS_MANAGER_INTERFACE ".Error.NothingInhibited" },
};

GQuark
ck_manager_error_quark (void)
{
        static volatile gsize quark_volatile = 0;

        g_dbus_error_register_error_domain ("ck_manager_error",
                                            &quark_volatile,
                                            ck_manager_error_entries,
                                            G_N_ELEMENTS (ck_manager_error_entries));

        return (GQuark) quark_volatile;
}
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
ck_manager_error_get_type (void)
{
  static GType etype = 0;

  if (etype == 0)
    {
      static const GEnumValue values[] =
        {
          ENUM_ENTRY (CK_MANAGER_ERROR_FAILED,                  "Failed"),
          ENUM_ENTRY (CK_MANAGER_ERROR_GENERAL,                 "General"),
          ENUM_ENTRY (CK_MANAGER_ERROR_INSUFFICIENT_PERMISSION, "InsufficientPermission"),
          ENUM_ENTRY (CK_MANAGER_ERROR_AUTHORIZATION_REQUIRED,  "AuthorizationRequired" ),
          ENUM_ENTRY (CK_MANAGER_ERROR_BUSY,                    "Busy" ),
          ENUM_ENTRY (CK_MANAGER_ERROR_NOT_SUPPORTED,           "NotSupported"),
          ENUM_ENTRY (CK_MANAGER_ERROR_INHIBITED,               "Inhibited"),
          ENUM_ENTRY (CK_MANAGER_ERROR_INVALID_INPUT,           "InvalidInput"),
          ENUM_ENTRY (CK_MANAGER_ERROR_OOM,                     "OutOfMemory"),
          ENUM_ENTRY (CK_MANAGER_ERROR_NO_SEATS,                "NoSeats"),
          ENUM_ENTRY (CK_MANAGER_ERROR_NO_SESSIONS,             "NoSessions"),
          ENUM_ENTRY (CK_MANAGER_ERROR_NOTHING_INHIBITED,       "NothingInhibited"),
          { 0, 0, 0 }
        };
      g_assert (CK_MANAGER_NUM_ERRORS == G_N_ELEMENTS (values) - 1);
      etype = g_enum_register_static ("Error", values);
    }
  return etype;
}

static void
throw_error (GDBusMethodInvocation *context,
             gint                   error_code,
             const gchar           *format,
             ...)
{
        va_list args;
        gchar *message;

        va_start (args, format);
        message = g_strdup_vprintf (format, args);
        va_end (args);

        g_debug ("manager: throwing error: %s", message);

        g_dbus_method_invocation_return_error (context, CK_MANAGER_ERROR, error_code, "%s", message);

        g_free (message);
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
                manager->priv->seat_serial = 0;
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

        uuid = g_strdup (g_get_host_name ());

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
        id = g_strdup_printf ("Session%u", serial);

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
        id = g_strdup_printf ("seat%u", serial);

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

        event.event.seat_added.seat_id = sid;
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

        event.event.seat_removed.seat_id = sid;
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

        event.event.seat_session_added.seat_id = sid;
        event.event.seat_session_added.session_id = (char *) ssid;

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

        event.event.seat_session_removed.seat_id = sid;
        event.event.seat_session_removed.session_id = (char *) ssid;

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

        event.event.seat_active_session_changed.seat_id = sid;
        event.event.seat_active_session_changed.session_id = (char *) ssid;

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
                             GVariant    *device)
{
        CkLogEvent         event;
        gboolean           res;
        GError            *error;
        char              *sid;
        char              *device_id;
        char              *device_type;

        memset (&event, 0, sizeof (CkLogEvent));

        event.type = CK_LOG_EVENT_SEAT_DEVICE_ADDED;
        g_get_current_time (&event.timestamp);

        sid = NULL;
        device_type = NULL;
        device_id = NULL;

        ck_seat_get_id (seat, &sid, NULL);

        g_variant_get (device, "(ss)", &device_type, &device_id);

        event.event.seat_device_added.seat_id = sid;

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
                               GVariant    *device)
{
        CkLogEvent         event;
        gboolean           res;
        GError            *error;
        char              *sid;
        char              *device_id;
        char              *device_type;

        memset (&event, 0, sizeof (CkLogEvent));

        event.type = CK_LOG_EVENT_SEAT_DEVICE_REMOVED;
        g_get_current_time (&event.timestamp);

        sid = NULL;
        device_type = NULL;
        device_id = NULL;

        ck_seat_get_id (seat, &sid, NULL);

        g_variant_get (device, "(ss)", &device_type, &device_id);

        event.event.seat_device_removed.seat_id = sid;

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
        char            *cookie = NULL;
        gchar           *ssid = NULL;
        CkProcessGroup  *pgroup;
        CkSession       *session;

        pgroup = ck_process_group_get ();

        ssid = ck_process_group_get_ssid (pgroup, pid);

        if (ssid != NULL) {
                g_debug ("looking for session for ssid %s", ssid);
                session = g_hash_table_lookup (manager->priv->sessions, ssid);
                if (session != NULL) {
                        g_object_get (session, "cookie", &cookie, NULL);
                }

                g_free (ssid);
        }

        if (cookie == NULL) {
                cookie = ck_unix_pid_get_env (pid, "XDG_SESSION_COOKIE");
        }

        return cookie;
}

typedef void (*AuthorizedCallback) (CkManager             *manager,
                                    GDBusMethodInvocation *context);

typedef struct
{
        CkManager             *manager;
        GDBusMethodInvocation *context;
        AuthorizedCallback     callback;
} AuthorizedCallbackData;

#ifdef HAVE_POLKIT
static void
data_free (AuthorizedCallbackData *data)
{
        g_object_unref (data->manager);
        g_free (data);
}

static void
auth_ready_callback (PolkitAuthority        *authority,
                     GAsyncResult           *res,
                     AuthorizedCallbackData *data)
{
        GError *error;
        PolkitAuthorizationResult *result;

        error = NULL;

        result = polkit_authority_check_authorization_finish (authority,
                                                              res,
                                                              &error);
        if (error != NULL) {
                throw_error (data->context, CK_MANAGER_ERROR_INSUFFICIENT_PERMISSION, _("Not Authorized: %s"), error->message);
                g_clear_error (&error);
        }
        else if (polkit_authorization_result_get_is_authorized (result)) {
                data->callback (data->manager, data->context);
        }
        else if (polkit_authorization_result_get_is_challenge (result)) {
                throw_error (data->context, CK_MANAGER_ERROR_AUTHORIZATION_REQUIRED, _("Authorization is required"));
        }
        else {
                throw_error (data->context, CK_MANAGER_ERROR_INSUFFICIENT_PERMISSION, _("Not Authorized"));
        }

        g_object_unref (result);

        data_free (data);
}

static void
check_polkit_permissions (CkManager             *manager,
                          GDBusMethodInvocation *context,
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
        sender = g_dbus_method_invocation_get_sender (context);
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
          GDBusMethodInvocation *context)
{
        PolkitAuthorizationResult *ret;
        GError *error;

        error = NULL;
        ret = polkit_authority_check_authorization_finish (authority, res, &error);
        if (error != NULL) {
                throw_error (context, CK_MANAGER_ERROR_FAILED, error->message);
                g_clear_error (&error);
        }
        else if (polkit_authorization_result_get_is_authorized (ret)) {
                g_dbus_method_invocation_return_value (context, g_variant_new ("(b)", TRUE));
        }
        else if (polkit_authorization_result_get_is_challenge (ret)) {
                g_dbus_method_invocation_return_value (context, g_variant_new ("(b)", TRUE));
        }
        else {
                g_dbus_method_invocation_return_value (context, g_variant_new ("(b)", FALSE));
        }

        g_object_unref (ret);
}

/* We use this to avoid breaking API compability with ConsoleKit1 for
 * CanStop and CanRestart, but this method emulates how logind
 * presents it's API */
static void
logind_ready_cb (PolkitAuthority *authority,
                 GAsyncResult    *res,
                 GDBusMethodInvocation *context)
{
        PolkitAuthorizationResult *ret;
        GError *error;

        error = NULL;
        ret = polkit_authority_check_authorization_finish (authority, res, &error);
        if (error != NULL) {
                throw_error (context, CK_MANAGER_ERROR_FAILED, error->message);
                g_clear_error (&error);
        }
        else if (polkit_authorization_result_get_is_authorized (ret)) {
                g_dbus_method_invocation_return_value (context, g_variant_new ("(s)", "yes"));
        }
        else if (polkit_authorization_result_get_is_challenge (ret)) {
                g_dbus_method_invocation_return_value (context, g_variant_new ("(s)", "challenge"));
        }
        else {
                g_dbus_method_invocation_return_value (context, g_variant_new ("(s)", "no"));
        }

        g_object_unref (ret);
}

/* We use this to avoid breaking API compability with ConsoleKit1 for
 * CanStop and CanRestart, but this method emulates how logind
 * presents it's API */
static void
get_polkit_logind_permissions (CkManager   *manager,
                               const char  *action,
                               GDBusMethodInvocation *context)
{
        const char    *sender;
        PolkitSubject *subject;

        g_debug ("get permissions for action %s", action);

        sender = g_dbus_method_invocation_get_sender (context);
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
                        GDBusMethodInvocation *context)
{
        const char    *sender;
        PolkitSubject *subject;

        g_debug ("get permissions for action %s", action);

        sender = g_dbus_method_invocation_get_sender (context);
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
        gboolean  res   = FALSE;
        GVariant *value = NULL;
        GError   *error = NULL;

        if (sender == NULL) {
                g_debug ("sender == NULL");
                goto out;
        }

        if (manager->priv->bus_proxy == NULL) {
                g_debug ("manager->priv->bus_proxy == NULL");
                goto out;
        }

        value = g_dbus_proxy_call_sync (manager->priv->bus_proxy, "GetConnectionUnixUser",
                                        g_variant_new ("(s)", sender),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        2000,
                                        NULL,
                                        &error);

        if (value == NULL) {
                g_warning ("GetConnectionUnixUser() failed: %s", error->message);
                g_error_free (error);
                goto out;
        }
        g_variant_get (value, "(u)", calling_uid);
        g_variant_unref (value);

        value = g_dbus_proxy_call_sync (manager->priv->bus_proxy, "GetConnectionUnixProcessID",
                                        g_variant_new ("(s)", sender),
                                        G_DBUS_CALL_FLAGS_NONE,
                                        2000,
                                        NULL,
                                        &error);

        if (value == NULL) {
                g_warning ("GetConnectionUnixProcessID() failed: %s", error->message);
                g_error_free (error);
                goto out;
        }
        g_variant_get (value, "(u)", calling_pid);
        g_variant_unref (value);

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

        /* filter out GDM/SDDM/KDM user */
        if (g_strcmp0 (username, "gdm")   == 0 ||
            g_strcmp0 (username, "_gdm")  == 0 ||
            g_strcmp0 (username, "sddm")  == 0 ||
            g_strcmp0 (username, "_sddm") == 0 ||
            g_strcmp0 (username, "kdm")   == 0 ||
            g_strcmp0 (username, "_kdm")  == 0) {
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

static gboolean
session_has_user (const char *ssid,
                  CkSession  *session,
                  guint      *unix_user)
{
        guint session_user;

        session_user = console_kit_session_get_unix_user (CONSOLE_KIT_SESSION (session));

        if (session_user == *unix_user) {
                g_debug ("Found session for user %d", *unix_user);
                return TRUE;
        }

        return FALSE;
}

static const gchar *
get_runtime_dir_for_user (CkManager *manager,
                          guint      unix_user)
{
        gpointer session;

        TRACE ();

        session = g_hash_table_find (manager->priv->sessions, (GHRFunc)session_has_user, &unix_user);

        if (session != NULL) {
                return ck_session_get_runtime_dir (CK_SESSION (session));
        }

        return NULL;
}

#ifdef ENABLE_RBAC_SHUTDOWN
static gboolean
check_rbac_permissions (CkManager             *manager,
                        GDBusMethodInvocation *context,
                        const char            *action,
                        AuthorizedCallback     callback)
{
        const char *sender;
        char       *username;
        gboolean    res;
        uid_t       uid = 0;
        pid_t       pid = 0;

        username = NULL;
        sender   = g_dbus_method_invocation_get_sender (context);
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
                g_debug ("User %s does not have RBAC permission to %s", username, action);
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
                     GDBusMethodInvocation *context,
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
                  GDBusMethodInvocation *context,
                  const char            *action)
{
#if defined HAVE_POLKIT
        /* This will return the yes, no, and challenge */
        get_polkit_logind_permissions (manager, action, context);
#elif defined ENABLE_RBAC_SHUTDOWN
        /* rbac determines either yes or no. There is no challenge with rbac */
        if (check_rbac_permissions (manager, context, RBAC_SHUTDOWN_KEY, NULL)) {
                g_dbus_method_invocation_return_value (context, g_variant_new ("(s)", "yes"));
        } else {
                g_dbus_method_invocation_return_value (context, g_variant_new ("(s)","no"));
        }
#else
        /* neither polkit or rbac. assumed single user system */
        g_dbus_method_invocation_return_value (context, g_variant_new ("(s)", "yes"));
#endif
}

/* Logs the event and performs the system call such as ck-system-restart.
 * returns an error message over dbus to the user if needed, otherwise returns
 * success (also over dbus).
 */
static void
do_system_action (CkManager             *manager,
                  GDBusMethodInvocation *context,
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
                g_warning ("Unable to %s system: %s", description, error->message);

                throw_error (context, CK_MANAGER_ERROR_GENERAL, _("Unable to %s system: %s"), description, error->message);

                g_clear_error (&error);
        } else {
                g_dbus_method_invocation_return_value (context, NULL);
        }
}

static gboolean
system_action_idle_cb(SystemActionData *data)
{
        g_return_val_if_fail (data != NULL, FALSE);

        /* Perform the action, it will handle the g_dbus_method_return */
        do_system_action (data->manager,
                          data->context,
                          data->command,
                          data->event_type,
                          data->description);

        /* If we got here the sleep/shutdown action is done and we're awake again
         * or the operation failed. Either way we can signal to the apps */
        switch (data->signal) {
        case PREPARE_FOR_SHUTDOWN:
                console_kit_manager_emit_prepare_for_shutdown (CONSOLE_KIT_MANAGER (data->manager), FALSE);
                break;
        case PREPARE_FOR_SLEEP:
                console_kit_manager_emit_prepare_for_sleep (CONSOLE_KIT_MANAGER (data->manager), FALSE);
                break;
        default:
                g_error ("system_action_idle_cb, unknown signal for command %s", data->command);
        }

        /* reset this since we'll return FALSE here and kill the cb */
        data->manager->priv->system_action_idle_id = 0;
        /* set this to NULL as we've processed this event and are about
         * to free the data */
        data->manager->priv->system_action_data = NULL;

        g_free (data);

        return FALSE;
}

static void
do_restart (CkManager             *manager,
            GDBusMethodInvocation *context)
{
        SystemActionData *data;
        guint             delay_time;

        /* Don't allow multiple system actions at the same time */
        if (manager->priv->system_action_idle_id != 0) {
                throw_error (context, CK_MANAGER_ERROR_BUSY, _("Attempting to perform a system action while one is in progress"));
                return;
        }

        /* Emit the signal */
        console_kit_manager_emit_prepare_for_shutdown (CONSOLE_KIT_MANAGER (manager), TRUE);

        /* Allocate and fill the data struct to pass to the idle cb */
        data = g_new0 (SystemActionData, 1);
        if (data == NULL) {
                g_critical ("failed to allocate memory to perform shutdown\n");
                console_kit_manager_emit_prepare_for_shutdown (CONSOLE_KIT_MANAGER (manager), FALSE);
                throw_error (context, CK_MANAGER_ERROR_FAILED, _("failed to allocate memory to perform restart"));
                return;
        }

        data->manager = manager;
        data->context = context;
        data->command = LIBDIR "/ConsoleKit/scripts/ck-system-restart";
        data->event_type = CK_LOG_EVENT_SYSTEM_RESTART;
        data->description = "Restart";
        data->signal = PREPARE_FOR_SHUTDOWN;

        if (ck_inhibit_manager_is_shutdown_delayed (manager->priv->inhibit_manager)) {
                delay_time = manager->priv->system_action_idle_inhibited_delay;
                /* We need to keep a pointer to the system action data because
                 * the inhibit lock can be lifted before the timeout
                 */
                manager->priv->system_action_data = data;
        } else {
                delay_time = manager->priv->system_action_idle_delay;
        }

        /* Sleep so user applications have time to respond */
        manager->priv->system_action_idle_id = g_timeout_add (delay_time,
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
static gboolean
dbus_restart (ConsoleKitManager     *ckmanager,
              GDBusMethodInvocation *context)
{
        CkManager  *manager;
        const char *action;

        TRACE ();

        manager = CK_MANAGER (ckmanager);

        /* Check if something is blocking that action */
        if (ck_inhibit_manager_is_shutdown_blocked (manager->priv->inhibit_manager)) {
                throw_error (context, CK_MANAGER_ERROR_INHIBITED, _("Operation is being inhibited"));
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

static gboolean
dbus_can_restart (ConsoleKitManager     *ckmanager,
                  GDBusMethodInvocation *context)

{
#if defined HAVE_POLKIT
        const char *action = "org.freedesktop.consolekit.system.restart";

        get_polkit_permissions (CK_MANAGER (ckmanager), action, context);
#elif defined ENABLE_RBAC_SHUTDOWN
        if (check_rbac_permissions (CK_MANAGER (ckmanager), context, RBAC_SHUTDOWN_KEY, NULL)) {
                console_kit_manager_complete_can_restart (ckmanager, context , TRUE);
        } else {
                console_kit_manager_complete_can_restart (ckmanager, context , FALSE);
        }
#else
        console_kit_manager_complete_can_restart (ckmanager, context , TRUE);
#endif

        return TRUE;
}

static void
do_stop (CkManager             *manager,
         GDBusMethodInvocation *context)
{
        SystemActionData *data;
        guint             delay_time;

        /* Don't allow multiple system actions at the same time */
        if (manager->priv->system_action_idle_id != 0) {
                throw_error (context, CK_MANAGER_ERROR_BUSY, _("Attempting to perform a system action while one is in progress"));
                return;
        }

        /* Emit the signal */
        console_kit_manager_emit_prepare_for_shutdown (CONSOLE_KIT_MANAGER (manager), TRUE);

        /* Allocate and fill the data struct to pass to the idle cb */
        data = g_new0 (SystemActionData, 1);
        if (data == NULL) {
                g_critical ("failed to allocate memory to perform shutdown\n");
                console_kit_manager_emit_prepare_for_shutdown (CONSOLE_KIT_MANAGER (manager), FALSE);
                throw_error (context, CK_MANAGER_ERROR_FAILED, _("failed to allocate memory to perform shutdown"));
                return;
        }

        data->manager = manager;
        data->context = context;
        data->command = LIBDIR "/ConsoleKit/scripts/ck-system-stop";
        data->event_type = CK_LOG_EVENT_SYSTEM_STOP;
        data->description = "Stop";
        data->signal = PREPARE_FOR_SHUTDOWN;

        if (ck_inhibit_manager_is_shutdown_delayed (manager->priv->inhibit_manager)) {
                delay_time = manager->priv->system_action_idle_inhibited_delay;
                /* We need to keep a pointer to the system action data because
                 * the inhibit lock can be lifted before the timeout
                 */
                manager->priv->system_action_data = data;
        } else {
                delay_time = manager->priv->system_action_idle_delay;
        }

        /* Sleep so user applications have time to respond */
        manager->priv->system_action_idle_id = g_timeout_add (delay_time,
                                                              (GSourceFunc)system_action_idle_cb,
                                                              data);
}

static gboolean
dbus_stop (ConsoleKitManager     *ckmanager,
           GDBusMethodInvocation *context)
{
        CkManager  *manager;
        const char *action;

        TRACE ();

        manager = CK_MANAGER (ckmanager);

        /* Check if something is blocked that action */
        if (ck_inhibit_manager_is_shutdown_blocked (manager->priv->inhibit_manager)) {
                throw_error (context, CK_MANAGER_ERROR_INHIBITED, _("Operation is being inhibited"));
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

static gboolean
dbus_can_stop (ConsoleKitManager     *ckmanager,
               GDBusMethodInvocation *context)
{
#if defined HAVE_POLKIT
        const char *action = "org.freedesktop.consolekit.system.stop";

        get_polkit_permissions (CK_MANAGER (ckmanager), action, context);
#elif defined ENABLE_RBAC_SHUTDOWN
        if (check_rbac_permissions (CK_MANAGER (ckmanager), context, RBAC_SHUTDOWN_KEY, NULL)) {
                console_kit_manager_complete_can_stop (ckmanager, context, TRUE);
        } else {
                console_kit_manager_complete_can_stop (ckmanager, context, FALSE);
        }
#else
        console_kit_manager_complete_can_stop (ckmanager, context, TRUE);
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
static gboolean
dbus_power_off (ConsoleKitManager     *ckmanager,
                GDBusMethodInvocation *context,
                gboolean               policykit_interactivity)
{
        CkManager  *manager;
        const char *action;

        TRACE ();

        manager = CK_MANAGER (ckmanager);

        /* Check if something is blocking that action */
        if (ck_inhibit_manager_is_shutdown_blocked (manager->priv->inhibit_manager)) {
                throw_error (context, CK_MANAGER_ERROR_INHIBITED, _("Operation is being inhibited"));
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
 * dbus_can_power_off:
 * @ckmanager: the @ConsoleKitManager object
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
static gboolean
dbus_can_power_off (ConsoleKitManager     *ckmanager,
                    GDBusMethodInvocation *context)

{
        CkManager  *manager;
        const char *action;

        TRACE ();

        manager = CK_MANAGER (ckmanager);

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
static gboolean
dbus_reboot (ConsoleKitManager     *ckmanager,
             GDBusMethodInvocation *context,
             gboolean               policykit_interactivity)
{
        CkManager  *manager;
        const char *action;

        TRACE ();

        manager = CK_MANAGER (ckmanager);

        /* Check if something is blocking that action */
        if (ck_inhibit_manager_is_shutdown_blocked (manager->priv->inhibit_manager)) {
                throw_error (context, CK_MANAGER_ERROR_INHIBITED, _("Operation is being inhibited"));
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
 * dbus_can_reboot:
 * @ckmanager: the @ConsoleKitManager object
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
static gboolean
dbus_can_reboot (ConsoleKitManager     *ckmanager,
                 GDBusMethodInvocation *context)

{
        CkManager  *manager;
        const char *action;

        TRACE ();

        manager = CK_MANAGER (ckmanager);

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
            GDBusMethodInvocation *context)
{
        SystemActionData *data;
        guint             delay_time;

        /* Don't allow multiple system actions at the same time */
        if (manager->priv->system_action_idle_id != 0) {
                throw_error (context, CK_MANAGER_ERROR_BUSY, _("Attempting to perform a system action while one is in progress"));
                return;
        }

        /* Emit the signal */
        console_kit_manager_emit_prepare_for_sleep (CONSOLE_KIT_MANAGER (manager), TRUE);

        /* Allocate and fill the data struct to pass to the idle cb */
        data = g_new0 (SystemActionData, 1);
        if (data == NULL) {
                g_critical ("failed to allocate memory to perform suspend\n");
                console_kit_manager_emit_prepare_for_sleep (CONSOLE_KIT_MANAGER (manager), FALSE);
                throw_error (context, CK_MANAGER_ERROR_FAILED, _("failed to allocate memory to perform suspend"));
                return;
        }

        data->manager = manager;
        data->context = context;
        data->command = LIBDIR "/ConsoleKit/scripts/ck-system-suspend";
        data->event_type = CK_LOG_EVENT_SYSTEM_SUSPEND;
        data->description = "Suspend";
        data->signal = PREPARE_FOR_SLEEP;

        if (ck_inhibit_manager_is_suspend_delayed (manager->priv->inhibit_manager)) {
                delay_time = manager->priv->system_action_idle_inhibited_delay;
                /* We need to keep a pointer to the system action data because
                 * the inhibit lock can be lifted before the timeout
                 */
                manager->priv->system_action_data = data;
        } else {
                delay_time = manager->priv->system_action_idle_delay;
        }

        /* Sleep so user applications have time to respond */
        manager->priv->system_action_idle_id = g_timeout_add (delay_time,
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
static gboolean
dbus_suspend (ConsoleKitManager     *ckmanager,
              GDBusMethodInvocation *context,
              gboolean               policykit_interactivity)
{
        CkManager *manager;
        const char *action;

        TRACE ();

        manager = CK_MANAGER (ckmanager);

        /* Check if something is blocking that action */
        if (ck_inhibit_manager_is_suspend_blocked (manager->priv->inhibit_manager)) {
                throw_error (context, CK_MANAGER_ERROR_INHIBITED, _("Operation is being inhibited"));
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
 * dbus_can_suspend:
 * @ckmanager: the @ConsoleKitManager object
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
static gboolean
dbus_can_suspend (ConsoleKitManager     *ckmanager,
                  GDBusMethodInvocation *context)

{
        CkManager  *manager;
        const char *action;

        TRACE ();

        manager = CK_MANAGER (ckmanager);

        if (get_system_num_users (manager) > 1) {
                action = "org.freedesktop.consolekit.system.suspend-multiple-users";
        } else {
                action = "org.freedesktop.consolekit.system.suspend";
        }

        if (ck_system_can_suspend ()) {
                check_can_action (manager, context, action);
        } else {
                /* not supported by system (or consolekit backend) */
                console_kit_manager_complete_can_suspend (ckmanager, context, "na");
        }

        return TRUE;
}

static void
do_hibernate (CkManager             *manager,
              GDBusMethodInvocation *context)
{
        SystemActionData *data;
        guint             delay_time;

        /* Don't allow multiple system actions at the same time */
        if (manager->priv->system_action_idle_id != 0) {
                throw_error (context, CK_MANAGER_ERROR_BUSY, _("Attempting to perform a system action while one is in progress"));
                return;
        }

        /* Emit the signal */
        console_kit_manager_emit_prepare_for_sleep (CONSOLE_KIT_MANAGER (manager), TRUE);

        /* Allocate and fill the data struct to pass to the idle cb */
        data = g_new0 (SystemActionData, 1);
        if (data == NULL) {
                g_critical ("failed to allocate memory to perform hibernate\n");
                console_kit_manager_emit_prepare_for_sleep (CONSOLE_KIT_MANAGER (manager), FALSE);
                throw_error (context, CK_MANAGER_ERROR_FAILED, _("failed to allocate memory to perform hibernate"));
                return;
        }

        data->manager = manager;
        data->context = context;
        data->command = LIBDIR "/ConsoleKit/scripts/ck-system-hibernate";
        data->event_type = CK_LOG_EVENT_SYSTEM_HIBERNATE;
        data->description = "Hibernate";
        data->signal = PREPARE_FOR_SLEEP;

        if (ck_inhibit_manager_is_suspend_delayed (manager->priv->inhibit_manager)) {
                delay_time = manager->priv->system_action_idle_inhibited_delay;
                /* We need to keep a pointer to the system action data because
                 * the inhibit lock can be lifted before the timeout
                 */
                manager->priv->system_action_data = data;
        } else {
                delay_time = manager->priv->system_action_idle_delay;
        }

        /* Sleep so user applications have time to respond */
        manager->priv->system_action_idle_id = g_timeout_add (delay_time,
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
static gboolean
dbus_hibernate (ConsoleKitManager     *ckmanager,
                GDBusMethodInvocation *context,
                gboolean               policykit_interactivity)
{
        CkManager  *manager;
        const char *action;

        TRACE ();

        manager = CK_MANAGER (ckmanager);

        /* Check if something is blocking that action */
        if (ck_inhibit_manager_is_suspend_blocked (manager->priv->inhibit_manager)) {
                throw_error (context, CK_MANAGER_ERROR_INHIBITED, _("Operation is being inhibited"));
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
 * dbus_can_hibernate:
 * @ckmanager: the @ConsoleKitManager object
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
static gboolean
dbus_can_hibernate (ConsoleKitManager     *ckmanager,
                    GDBusMethodInvocation *context)

{
        CkManager  *manager;
        const char *action;

        TRACE ();

        manager = CK_MANAGER (ckmanager);

        if (get_system_num_users (manager) > 1) {
                action = "org.freedesktop.consolekit.system.hibernate-multiple-users";
        } else {
                action = "org.freedesktop.consolekit.system.hibernate";
        }

        if (ck_system_can_hibernate ()) {
                check_can_action (manager, context, action);
        } else {
                /* not supported by system (or consolekit backend) */
                console_kit_manager_complete_can_hibernate (ckmanager, context, "na");
        }

        return TRUE;
}

static void
do_hybrid_sleep (CkManager             *manager,
                 GDBusMethodInvocation *context)
{
        SystemActionData *data;
        guint             delay_time;

        /* Don't allow multiple system actions at the same time */
        if (manager->priv->system_action_idle_id != 0) {
                throw_error (context, CK_MANAGER_ERROR_BUSY, _("Attempting to perform a system action while one is in progress"));
                return;
        }

        /* Emit the signal */
        console_kit_manager_emit_prepare_for_sleep (CONSOLE_KIT_MANAGER (manager), TRUE);

        /* Allocate and fill the data struct to pass to the idle cb */
        data = g_new0 (SystemActionData, 1);
        if (data == NULL) {
                g_critical ("failed to allocate memory to perform hybrid sleep\n");
                console_kit_manager_emit_prepare_for_sleep (CONSOLE_KIT_MANAGER (manager), FALSE);
                throw_error (context, CK_MANAGER_ERROR_FAILED, _("failed to allocate memory to perform hybrid sleep"));
                return;
        }

        data->manager = manager;
        data->context = context;
        data->command = LIBDIR "/ConsoleKit/scripts/ck-system-hybridsleep";
        data->event_type = CK_LOG_EVENT_SYSTEM_HIBERNATE;
        data->description = "Hybrid Sleep";
        data->signal = PREPARE_FOR_SLEEP;

        if (ck_inhibit_manager_is_suspend_delayed (manager->priv->inhibit_manager)) {
                delay_time = manager->priv->system_action_idle_inhibited_delay;
                /* We need to keep a pointer to the system action data because
                 * the inhibit lock can be lifted before the timeout
                 */
                manager->priv->system_action_data = data;
        } else {
                delay_time = manager->priv->system_action_idle_delay;
        }

        /* Sleep so user applications have time to respond */
        manager->priv->system_action_idle_id = g_timeout_add (delay_time,
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
static gboolean
dbus_hybrid_sleep (ConsoleKitManager     *ckmanager,
                   GDBusMethodInvocation *context,
                   gboolean               policykit_interactivity)
{
        CkManager  *manager;
        const char *action;

        TRACE ();

        manager = CK_MANAGER (ckmanager);

        /* Check if something is blocking that action */
        if (ck_inhibit_manager_is_suspend_blocked (manager->priv->inhibit_manager)) {
                throw_error (context, CK_MANAGER_ERROR_INHIBITED, _("Operation is being inhibited"));
                return TRUE;
        }

        if (get_system_num_users (manager) > 1) {
                action = "org.freedesktop.consolekit.system.hybridsleep-multiple-users";
        } else {
                action = "org.freedesktop.consolekit.system.hybridsleep";
        }

        g_debug ("ConsoleKit Hybrid Sleep: %s", action);

        check_system_action (manager, policykit_interactivity, context, action, do_hybrid_sleep);

        return TRUE;
}

/**
 * dbus_can_hybrid_sleep:
 * @ckmanager: the @ConsoleKitManager object
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
static gboolean
dbus_can_hybrid_sleep (ConsoleKitManager     *ckmanager,
                       GDBusMethodInvocation *context)

{
        CkManager  *manager;
        const char *action;

        TRACE ();

        manager = CK_MANAGER (ckmanager);

        if (get_system_num_users (manager) > 1) {
                action = "org.freedesktop.consolekit.system.hybridsleep-multiple-users";
        } else {
                action = "org.freedesktop.consolekit.system.hybridsleep";
        }

        if (ck_system_can_hybrid_sleep ()) {
                check_can_action (manager, context, action);
        } else {
                /* not supported by system (or consolekit backend) */
                console_kit_manager_complete_can_hybrid_sleep (ckmanager, context, "na");
        }

        return TRUE;
}

static gboolean
dbus_inhibit (ConsoleKitManager     *ckmanager,
              GDBusMethodInvocation *context,
              GUnixFDList *fd_list,
              const gchar *what,
              const gchar *who,
              const gchar *why,
              const gchar *mode)
{
        CkManagerPrivate *priv;
        gint              fd = -1;
        GUnixFDList      *out_fd_list = NULL;
        const gchar      *sender;
        uid_t             uid = 0;
        pid_t             pid = 0;
        gint              res;

        TRACE ();

        g_return_val_if_fail (CK_IS_MANAGER (ckmanager), FALSE);

        priv = CK_MANAGER_GET_PRIVATE (CK_MANAGER (ckmanager));

        sender   = g_dbus_method_invocation_get_sender (context);
        res      = get_caller_info (CK_MANAGER (ckmanager),
                                    sender,
                                    &uid,
                                    &pid);

        if (!res) {
                throw_error (context, CK_MANAGER_ERROR_GENERAL, _("Error creating the inhibit lock"));
                return TRUE;
        }

        if (priv->inhibit_manager == NULL) {
                throw_error (context, CK_MANAGER_ERROR_GENERAL, _("Inhibit manager failed to initialize"));
                return TRUE;
        }

        fd = ck_inhibit_manager_create_lock (priv->inhibit_manager,
                                             who,
                                             what,
                                             why,
                                             mode,
                                             uid,
                                             pid);

        /* if we didn't get an inhibit lock, translate and throw the error */
        if (fd < 0) {
                switch (fd) {
                case CK_INHIBIT_ERROR_INVALID_INPUT:
                        throw_error (context, CK_MANAGER_ERROR_INVALID_INPUT, _("Invalid input when creating inhibit lock"));
                        return TRUE;
                case CK_INHIBIT_ERROR_OOM:
                        throw_error (context, CK_MANAGER_ERROR_OOM, _("Unable to create inhibit lock, insufficient memory"));
                        return TRUE;
                default:
                        throw_error (context, CK_MANAGER_ERROR_GENERAL, _("Error creating the inhibit lock"));
                        return TRUE;
                }
        }

        out_fd_list = g_unix_fd_list_new_from_array (&fd, 1);

        console_kit_manager_complete_inhibit (ckmanager, context, out_fd_list, g_variant_new_handle (0));
        g_clear_object (&out_fd_list);
        return TRUE;
}

/**
 * dbus_list_inhibitors:
 * @ckmanager: the @ConsoleKitManager object
 * @context: The GDBus context.
 * Example:
  dbus-send --system --dest=org.freedesktop.ConsoleKit \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/freedesktop/ConsoleKit/Manager \
  org.freedesktop.ConsoleKit.Manager.ListInhibitors
 *
 * Returnes TRUE.
 **/
static gboolean
dbus_list_inhibitors (ConsoleKitManager     *ckmanager,
                      GDBusMethodInvocation *context)
{
        CkManagerPrivate *priv;
        GVariantBuilder   inhibitor_builder;
        GVariant         *inhibitor;
        GList            *l, *inhibit_list;

        TRACE ();

        g_return_val_if_fail (CK_IS_MANAGER (ckmanager), FALSE);

        priv = CK_MANAGER_GET_PRIVATE (CK_MANAGER (ckmanager));

        if (priv->inhibit_manager == NULL) {
                throw_error (context, CK_MANAGER_ERROR_GENERAL, _("Inhibit manager failed to initialize"));
                return TRUE;
        }

        inhibit_list = ck_inhibit_manager_get_inhibit_list (priv->inhibit_manager);

        if (inhibit_list == NULL) {
                throw_error (context, CK_MANAGER_ERROR_NOTHING_INHIBITED, _("There is nothing currently inhibited"));
                return TRUE;
        }

        g_variant_builder_init (&inhibitor_builder, G_VARIANT_TYPE_ARRAY);

        for (l = inhibit_list; l != NULL; l = g_list_next (l)) {
                g_debug ("what %s", ck_inhibit_get_what (CK_INHIBIT (l->data)));
                g_debug ("who %s", ck_inhibit_get_who (CK_INHIBIT (l->data)));
                g_debug ("why %s", ck_inhibit_get_why (CK_INHIBIT (l->data)));
                g_debug ("mode %s", ck_inhibit_get_mode  (CK_INHIBIT (l->data)));

                inhibitor = g_variant_new("(ssssuu)",
                                          ck_inhibit_get_what (CK_INHIBIT (l->data)),
                                          ck_inhibit_get_who (CK_INHIBIT (l->data)),
                                          ck_inhibit_get_why (CK_INHIBIT (l->data)),
                                          ck_inhibit_get_mode (CK_INHIBIT (l->data)),
                                          ck_inhibit_get_uid (CK_INHIBIT (l->data)),
                                          ck_inhibit_get_pid (CK_INHIBIT (l->data)));

                g_variant_builder_add_value (&inhibitor_builder, inhibitor);
        }

        console_kit_manager_complete_list_inhibitors(ckmanager, context, g_variant_builder_end (&inhibitor_builder));
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
                      GVariant    *device,
                      CkManager   *manager)
{
        ck_manager_dump (manager);
        log_seat_device_added_event (manager, seat, device);
}

static void
on_seat_device_removed (CkSeat      *seat,
                        GVariant    *device,
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

        seat = ck_seat_new (sid, kind, manager->priv->connection);

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

        g_debug ("Emitting seat-added: %s", ck_seat_get_path (seat));
        console_kit_manager_emit_seat_added (CONSOLE_KIT_MANAGER (manager), ck_seat_get_path (seat));

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

        g_debug ("Emitting seat-removed: %s", ck_seat_get_path (orig_seat));
        console_kit_manager_emit_seat_removed (CONSOLE_KIT_MANAGER (manager), ck_seat_get_path (orig_seat));

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
        guint vtnr;
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
        vtnr               = console_kit_session_get_vtnr (cksession);
        ck_session_is_local (session, &is_local, NULL);

        g_debug ("find_seat_for_session vtnr returned: %d", vtnr);

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

        if ((is_static_x11 || is_static_text) && vtnr > 0) {
                seat = g_hash_table_lookup (manager->priv->seats, "seat0");
        }

        return seat;
}

static gboolean
manager_set_system_idle_hint (CkManager *manager,
                              gboolean   idle_hint)
{
        /* Check if something is blocking that action */
        if (ck_inhibit_manager_is_idle_blocked (manager->priv->inhibit_manager)) {
                g_debug ("idle inhibited, forcing idle_hint to FALSE");
                idle_hint = FALSE;
        }

        if (manager->priv->system_idle_hint != idle_hint) {
                manager->priv->system_idle_hint = idle_hint;

                /* FIXME: can we get a time from the dbus message? */
                g_get_current_time (&manager->priv->system_idle_since_hint);

                g_debug ("Emitting system-idle-hint-changed: %d", idle_hint);
                console_kit_manager_emit_system_idle_hint_changed (CONSOLE_KIT_MANAGER (manager), idle_hint);
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
static gboolean
dbus_get_system_idle_hint (ConsoleKitManager     *ckmanager,
                           GDBusMethodInvocation *context)
{
        CkManager *manager = CK_MANAGER (ckmanager);

        TRACE ();

        g_return_val_if_fail (CK_IS_MANAGER (manager), FALSE);

        console_kit_manager_complete_get_system_idle_hint (ckmanager, context, manager->priv->system_idle_hint);
        return TRUE;
}

static gboolean
dbus_get_system_idle_since_hint (ConsoleKitManager     *ckmanager,
                                 GDBusMethodInvocation *context)
{
        CkManager *manager = CK_MANAGER (ckmanager);
        char *date_str;

        TRACE ();

        g_return_val_if_fail (CK_IS_MANAGER (manager), FALSE);

        if (manager->priv->system_idle_hint) {
                date_str = g_time_val_to_iso8601 (&manager->priv->system_idle_since_hint);
        } else {
                date_str = g_strdup ("");
        }

        console_kit_manager_complete_get_system_idle_since_hint (ckmanager, context, date_str);
        g_free (date_str);
        return TRUE;
}

static void
open_session_for_leader (CkManager             *manager,
                         CkSessionLeader       *leader,
                         const GVariant        *parameters,
                         gboolean               is_local,
                         GDBusMethodInvocation *context)
{
        CkProcessGroup *pgroup;
        CkSession      *session;
        CkSeat         *seat;
        const char     *ssid;
        const char     *cookie;
        char           *runtime_dir;
        guint           unix_user;

        ssid = ck_session_leader_peek_session_id (leader);
        cookie = ck_session_leader_peek_cookie (leader);

        session = ck_session_new_with_parameters (ssid,
                                                  cookie,
                                                  parameters,
                                                  manager->priv->connection);

        if (session == NULL) {
                throw_error (context, CK_MANAGER_ERROR_GENERAL, "Unable to create new session");
                return;
        }

        unix_user = console_kit_session_get_unix_user (CONSOLE_KIT_SESSION (session));

        /* If the user is already logged in, continue to use the same runtime dir.
         * We need to do this before adding the session to the manager's table. */
        runtime_dir = g_strdup (get_runtime_dir_for_user (manager, unix_user));

        /* otherwise generate a new one */
        if (runtime_dir == NULL) {
                runtime_dir = ck_generate_runtime_dir_for_user (unix_user);
        }

        g_debug ("XDG_RUNTIME_DIR is %s", runtime_dir);
        ck_session_set_runtime_dir (session, runtime_dir);

        /* If supported, add the session leader to a process group so we
         * can track it with something better than an environment variable */
        pgroup = ck_process_group_get ();
        ck_process_group_create (pgroup,
                                 ck_session_leader_get_pid (leader),
                                 ssid,
                                 unix_user);

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

        /* set the is_local flag for the session */
        g_debug ("setting session %s is_local %s", ssid, is_local ? "TRUE" : "FALSE");
        ck_session_set_is_local (session, is_local, NULL);

        manager_update_system_idle_hint (manager);
        g_signal_connect (CONSOLE_KIT_SESSION (session), "idle-hint-changed",
                          G_CALLBACK (session_idle_hint_changed),
                          manager);

        /* let consumers know of the new session */
        console_kit_manager_emit_session_new (CONSOLE_KIT_MANAGER (manager), ssid, ck_session_get_path (session));

        g_object_unref (session);
        g_free (runtime_dir);

        g_dbus_method_invocation_return_value (context , g_variant_new ("(s)", cookie));
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
verify_and_open_session_for_leader (CkManager             *manager,
                                    CkSessionLeader       *leader,
                                    GVariant              *parameters,
                                    GDBusMethodInvocation *context)
{
        gboolean is_local = FALSE;
        GVariantIter     *iter;
        gchar            *prop_name;
        GVariant         *value;

        /* Only allow a local session if originating from an existing
           local session.  Effectively this means that only trusted
           parties can create local sessions. */

        g_debug ("CkManager: verifying session for leader");

        g_variant_get ((GVariant *)parameters, "a{sv}", &iter);
        while (!is_local && g_variant_iter_next (iter, "{sv}", &prop_name, &value)) {
                if (g_strcmp0 (prop_name, "is-local") == 0) {
                        is_local = TRUE;
                }
                if (g_strcmp0 (prop_name, "login-session-id") == 0) {
                        is_local = _verify_login_session_id_is_local (manager, g_variant_get_string (value, 0));
                }
                g_free (prop_name);
                g_variant_unref (value);
        }
        g_variant_iter_free (iter);

        g_debug ("CkManager: found is-local=%s", is_local ? "true" : "false");

        open_session_for_leader (manager,
                                 leader,
                                 parameters,
                                 is_local,
                                 context);
}

static void
collect_parameters_cb (CkSessionLeader       *leader,
                       GVariant              *parameters,
                       GDBusMethodInvocation *context,
                       CkManager             *manager)
{
        if (parameters == NULL) {
                throw_error (context, CK_MANAGER_ERROR_GENERAL, "Unable to get information about the calling process");
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
                             GDBusMethodInvocation *context)
{
        gboolean res;

        res = ck_session_leader_collect_parameters (leader,
                                                    context,
                                                    (CkSessionLeaderDoneFunc)collect_parameters_cb,
                                                    manager);
        if (! res) {
                throw_error (context, CK_MANAGER_ERROR_GENERAL, "Unable to get information about the calling process");
        }
}

static gboolean
create_session_for_sender (CkManager             *manager,
                           const char            *sender,
                           const GVariant        *parameters,
                           GDBusMethodInvocation *context)
{
        pid_t           pid = 0;
        uid_t           uid = 0;
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
                g_debug ("Unable to get information about the calling process");
                throw_error (context, CK_MANAGER_ERROR_GENERAL, "Unable to get information about the calling process");
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
static gboolean
dbus_get_session_for_cookie (ConsoleKitManager     *ckmanager,
                             GDBusMethodInvocation *context,
                             const char            *cookie)
{
        CkManager       *manager;
        gboolean         res;
        const char      *sender;
        uid_t            calling_uid = 0;
        pid_t            calling_pid = 0;
        CkProcessStat   *stat;
        char            *ssid;
        CkSession       *session;
        CkSessionLeader *leader;
        GError          *local_error;

        ssid = NULL;
        manager = CK_MANAGER (ckmanager);

        TRACE ();

        sender = g_dbus_method_invocation_get_sender (context);

        res = get_caller_info (manager,
                               sender,
                               &calling_uid,
                               &calling_pid);

        if (! res) {
                g_debug ("CkManager: Unable to lookup caller info - failing");
                throw_error (context, CK_MANAGER_ERROR_GENERAL, _("Unable to get information about the calling process"));
                return TRUE;
        }

        local_error = NULL;
        res = ck_process_stat_new_for_unix_pid (calling_pid, &stat, &local_error);
        if (! res) {
                if (local_error != NULL) {
                        g_debug ("stat on pid %d failed: %s", calling_pid, local_error->message);
                        g_clear_error (&local_error);
                }

                g_debug ("CkManager: Unable to lookup info for caller - failing");

                throw_error (context, CK_MANAGER_ERROR_GENERAL, _("Unable to lookup information about calling process '%d'"), calling_pid);
                return TRUE;
        }

        /* FIXME: should we restrict this by uid? */

        ck_process_stat_free (stat);

        leader = g_hash_table_lookup (manager->priv->leaders, cookie);
        if (leader == NULL) {
                g_debug ("CkManager: Unable to lookup cookie for caller - failing");
                throw_error (context, CK_MANAGER_ERROR_GENERAL, _("Unable to find session for cookie"));
                return TRUE;
        }

        session = g_hash_table_lookup (manager->priv->sessions, ck_session_leader_peek_session_id (leader));
        if (session == NULL) {
                g_debug ("CkManager: Unable to lookup session for cookie - failing");
                throw_error (context, CK_MANAGER_ERROR_GENERAL, _("Unable to find session for cookie"));
                return TRUE;
        }

        ck_session_get_id (session, &ssid, NULL);

        g_debug ("CkManager: Found session '%s'", ssid);

        console_kit_manager_complete_get_session_for_cookie (ckmanager, context, ck_session_get_path (session));

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
static gboolean
dbus_get_session_for_unix_process (ConsoleKitManager     *ckmanager,
                                   GDBusMethodInvocation *context,
                                   guint                  pid)
{
        CkManager     *manager;
        gboolean       res;
        const char    *sender;
        uid_t          calling_uid = 0;
        pid_t          calling_pid = 0;
        char          *cookie;

        if (pid <= 1 || pid >= UINT_MAX) {
                throw_error (context, CK_MANAGER_ERROR_INVALID_INPUT, _("pid must be > 1"));
                return TRUE;
        }

        manager = CK_MANAGER (ckmanager);

        sender = g_dbus_method_invocation_get_sender (context);

        TRACE ();
        g_debug ("pid: %u", pid);

        res = get_caller_info (manager,
                               sender,
                               &calling_uid,
                               &calling_pid);

        if (! res) {
                throw_error (context, CK_MANAGER_ERROR_GENERAL, _("Unable to get information about the calling process"));
                return TRUE;
        }

        cookie = get_cookie_for_pid (manager, pid);
        if (cookie == NULL) {
                g_debug ("CkManager: unable to lookup session for unix process: %u", pid);

                throw_error (context, CK_MANAGER_ERROR_GENERAL, _("Unable to lookup session information for process '%d'"), pid);
                return TRUE;
        }

        dbus_get_session_for_cookie (ckmanager, context, cookie);
        g_free (cookie);

        return TRUE;
}

static gboolean
dbus_get_session_by_pid (ConsoleKitManager     *ckmanager,
                         GDBusMethodInvocation *context,
                         guint                  pid)
{
        dbus_get_session_for_unix_process (ckmanager, context, pid);
        return TRUE;
}

/*
  Example:
  dbus-send --system --dest=org.freedesktop.ConsoleKit \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/freedesktop/ConsoleKit/Manager \
  org.freedesktop.ConsoleKit.Manager.GetCurrentSession
*/
static gboolean
dbus_get_current_session (ConsoleKitManager     *ckmanager,
                          GDBusMethodInvocation *context)
{
        CkManager  *manager;
        gboolean    res;
        const char *sender;
        uid_t       calling_uid = 0;
        pid_t       calling_pid = 0;

        TRACE ();

        manager = CK_MANAGER (ckmanager);
        sender = g_dbus_method_invocation_get_sender (context);

        g_debug ("CkManager: get current session");

        res = get_caller_info (manager,
                               sender,
                               &calling_uid,
                               &calling_pid);

        if (! res) {
                throw_error (context, CK_MANAGER_ERROR_GENERAL, _("Unable to get information about the calling process"));
                return TRUE;
        }

        dbus_get_session_for_unix_process (ckmanager, context, calling_pid);

        return TRUE;
}

static gboolean
dbus_open_session (ConsoleKitManager     *ckmanager,
                   GDBusMethodInvocation *context)
{
        const char *sender;

        TRACE ();

        sender = g_dbus_method_invocation_get_sender (context);
        create_session_for_sender (CK_MANAGER (ckmanager), sender, NULL, context);

        return TRUE;
}

/* privileged method - should be protected by D-Bus policy */
static gboolean
dbus_open_session_with_parameters (ConsoleKitManager     *ckmanager,
                                   GDBusMethodInvocation *context,
                                   GVariant              *parameters)
{
        const char *sender;

        TRACE ();

        sender = g_dbus_method_invocation_get_sender (context);
        create_session_for_sender (CK_MANAGER (ckmanager), sender, parameters, context);

        return TRUE;
}

static gboolean
remove_session_for_cookie (CkManager  *manager,
                           const char *cookie,
                           CkSessionLeader *leader,
                           GError    **error)
{
        CkSession       *orig_session;
        char            *orig_ssid;
        char            *sid;
        guint            unix_user;
        gboolean         res;
        gboolean         ret;

        ret = FALSE;
        orig_ssid = NULL;
        orig_session = NULL;

        g_debug ("Removing session for cookie: %s", cookie);

        if (leader == NULL) {
            leader = g_hash_table_lookup (manager->priv->leaders, cookie);

            if (leader == NULL) {
                g_set_error (error,
                             CK_MANAGER_ERROR,
                             CK_MANAGER_ERROR_GENERAL,
                             "Unable to find session for cookie");
                goto out;
            }
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

        /* Get the session's uid, we'll need this if we have to remove the
         * runtime dir
         */
        unix_user = console_kit_session_get_unix_user (CONSOLE_KIT_SESSION (orig_session));

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

        /* let consumers know the session is gone */
        console_kit_manager_emit_session_removed (CONSOLE_KIT_MANAGER (manager),
                                                  orig_ssid,
                                                  ck_session_get_path (orig_session));

        if (get_runtime_dir_for_user (manager, unix_user) == NULL) {
                /* We removed the session and now there's no runtime dir
                 * associated with that user.
                 * Remove the runtime dir from the system.
                 */
                ck_remove_runtime_dir_for_user (unix_user);
        }

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

static gboolean
dbus_close_session (ConsoleKitManager     *ckmanager,
                    GDBusMethodInvocation *context,
                    const char            *cookie)
{
        CkManager  *manager;
        gboolean    res;
        const char *sender;
        uid_t       calling_uid = 0;
        pid_t       calling_pid = 0;
        GError     *error;

        TRACE ();

        g_debug ("Closing session for cookie: %s", cookie);

        manager = CK_MANAGER (ckmanager);

        sender = g_dbus_method_invocation_get_sender (context);
        res = get_caller_info (manager,
                               sender,
                               &calling_uid,
                               &calling_pid);

        if (! res) {
                throw_error (context, CK_MANAGER_ERROR_FAILED, _("Unable to lookup information about calling process '%d'"), calling_pid);
                return TRUE;
        }

        error = NULL;
        res = paranoia_check_is_cookie_owner (manager, cookie, calling_uid, calling_pid, &error);
        if (! res) {
                throw_error (context, CK_MANAGER_ERROR_FAILED, "%s", error->message);
                g_error_free (error);

                return TRUE;
        }

        error = NULL;
        res = remove_session_for_cookie (manager, cookie, NULL, &error);
        if (! res) {
                throw_error (context, CK_MANAGER_ERROR_FAILED, "%s", error->message);
                g_clear_error (&error);
                return TRUE;
        } else {
                g_hash_table_remove (manager->priv->leaders, cookie);
        }

        console_kit_manager_complete_close_session (ckmanager, context, TRUE);
        return TRUE;
}

typedef struct {
    CkManager *manager;
    const char *cookie;
    CkSessionLeader *leader;
} RemoveEntity;

typedef struct {
        const char *service_name;
        CkManager *manager;
        GPtrArray *entities;
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
                RemoveEntity *entity = g_malloc(sizeof(*entity));
                if (entity) {
                    entity->manager = data->manager;
                    entity->cookie = cookie;
                    entity->leader = leader;
                    g_ptr_array_add(data->entities, entity);
                    return TRUE;
                }
        }

        return FALSE;
}

static void
destroy_entity(gpointer data)
{
    RemoveEntity *ent = data;

    remove_session_for_cookie (ent->manager, ent->cookie, ent->leader, NULL);
    ck_session_leader_cancel (ent->leader);
    g_object_unref(ent->leader);
    g_free(ent);
}

static void
remove_sessions_for_connection (CkManager   *manager,
                                const gchar *service_name)
{
        RemoveLeaderData data;
        guint n;

        g_debug ("Removing sessions for service name: %s", service_name);

        data.manager = manager;
        data.service_name = service_name;
        data.entities = g_ptr_array_new_with_free_func(destroy_entity);

        n = g_hash_table_foreach_steal (manager->priv->leaders,
                                        (GHRFunc)remove_leader_for_connection,
                                        &data);
        while (n-- > 0) {
            g_ptr_array_remove_index_fast (data.entities, 0);
        }
        g_assert (data.entities->len == 0);
        g_ptr_array_free (data.entities, TRUE);
}

#ifdef HAVE_POLKIT
static void
polkit_authority_get_cb (GObject *source_object,
                         GAsyncResult *res,
                         gpointer user_data)
{
        CkManager *manager = CK_MANAGER (user_data);

        manager->priv->pol_ctx = polkit_authority_get_finish (res, NULL);
}
#endif


static void
on_name_owner_notify (GDBusConnection *connection,
                      const gchar     *sender_name,
                      const gchar     *object_path,
                      const gchar     *interface_name,
                      const gchar     *signal_name,
                      GVariant        *parameters,
                      gpointer         user_data)
{
        CkManager *manager = CK_MANAGER (user_data);
        gchar     *service_name, *old_service_name, *new_service_name;

        g_variant_get (parameters, "(&s&s&s)", &service_name, &old_service_name, &new_service_name);

        if (strlen (new_service_name) == 0) {
                remove_sessions_for_connection (manager, old_service_name);
        }
}

static gboolean
register_manager (CkManager *manager, GDBusConnection *connection)
{
        GError *error = NULL;

        manager->priv->connection = connection;

#ifdef HAVE_POLKIT
        polkit_authority_get_async (NULL, polkit_authority_get_cb, manager);
#endif

        if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (CONSOLE_KIT_MANAGER (manager)),
                                               manager->priv->connection,
                                               CK_MANAGER_DBUS_PATH,
                                               &error)) {
                if (error != NULL) {
                        g_critical ("error exporting interface: %s", error->message);
                        g_error_free (error);
                        return FALSE;
                }
        }

        g_debug ("exported on %s", g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (CONSOLE_KIT_MANAGER (manager))));

        /* connect to DBus for get_caller_info */
        manager->priv->bus_proxy = g_dbus_proxy_new_sync (manager->priv->connection,
                                                          G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
                                                          G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                          NULL,
                                                          "org.freedesktop.DBus",
                                                          "/org/freedesktop/DBus/Bus",
                                                          "org.freedesktop.DBus",
                                                          NULL,
                                                          &error);
        if (manager->priv->bus_proxy == NULL) {
                g_warning ("cannot connect to DBus: %s", error->message);
                g_clear_error (&error);
                return FALSE;
        }

        manager->priv->name_owner_id = g_dbus_connection_signal_subscribe (manager->priv->connection,
                                                                           "org.freedesktop.DBus",
                                                                           "org.freedesktop.DBus",
                                                                           "NameOwnerChanged",
                                                                           "/org/freedesktop/DBus",
                                                                           NULL,
                                                                           G_DBUS_SIGNAL_FLAGS_NONE,
                                                                           on_name_owner_notify,
                                                                           manager,
                                                                           NULL);

        /* create the seats after we've registered on the manager on the bus */
        create_seats (manager);

        return TRUE;
}

static void
ck_manager_class_init (CkManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = ck_manager_finalize;

        g_type_class_add_private (klass, sizeof (CkManagerPrivate));
}

typedef struct {
        guint       uid;
        GHashTable *hash;
} GetSessionsData;

static void
collect_sessions_for_user (char            *ssid,
                           CkSession       *session,
                           GetSessionsData *data)
{
        guint    uid;

        uid = console_kit_session_get_unix_user (CONSOLE_KIT_SESSION(session));

        if (uid == data->uid) {
                g_hash_table_add (data->hash, g_strdup (ck_session_get_path (session)));
        }
}

static gboolean
dbus_get_sessions_for_unix_user (ConsoleKitManager     *ckmanager,
                                 GDBusMethodInvocation *context,
                                 guint                  uid)
{
        CkManager        *manager;
        GetSessionsData  *data;
        const gchar     **sessions;

        TRACE ();

        manager = CK_MANAGER (ckmanager);

        g_return_val_if_fail (CK_IS_MANAGER (manager), FALSE);

        data = g_new0 (GetSessionsData, 1);
        data->uid = uid;

        /* Create a new hash table that we can fill with the ssids that belong to the user */
        data->hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

        g_hash_table_foreach (manager->priv->sessions, (GHFunc)collect_sessions_for_user, data);

        /* pull out the session ids in a format gdbus likes */
        sessions = (const gchar**)g_hash_table_get_keys_as_array (data->hash, NULL);

        /* gdbus/gvariant requires that we throw an error to return NULL */
        if (sessions == NULL) {
                throw_error (context, CK_MANAGER_ERROR_NO_SESSIONS, _("User has no sessions"));
                g_hash_table_destroy (data->hash);
                g_free (data);
                return TRUE;
        }

        console_kit_manager_complete_get_sessions_for_unix_user (ckmanager, context, sessions);
        g_hash_table_destroy (data->hash);
        g_free (data);
        return TRUE;
}

/* This is deprecated */
static gboolean
dbus_get_sessions_for_user (ConsoleKitManager     *ckmanager,
                            GDBusMethodInvocation *context,
                            guint                  uid)
{
        TRACE ();
        return dbus_get_sessions_for_unix_user (ckmanager, context, uid);
}

static CkSession*
get_session_from_id (CkManager   *manager,
                     const gchar *arg_session_id)
{
        gpointer session = NULL;

        TRACE ();

        g_return_val_if_fail (CK_IS_MANAGER (manager), NULL);

        if (arg_session_id == NULL) {
                return NULL;
        }

        session = g_hash_table_lookup (manager->priv->sessions, arg_session_id);

        if (!CK_IS_SESSION (session)) {
                return NULL;
        }

        return CK_SESSION (session);
}

static gboolean
dbus_activate_session_on_seat (ConsoleKitManager *ckmanager,
                               GDBusMethodInvocation *context,
                               const gchar *arg_session_id,
                               const gchar *arg_seat_id)
{
        CkManager *manager;
        CkSession *session = NULL;
        gpointer seat = NULL;
        GError  *error;

        TRACE ();

        manager = CK_MANAGER (ckmanager);

        g_return_val_if_fail (CK_IS_MANAGER (manager), FALSE);

        session = get_session_from_id (manager, arg_session_id);
        if (session == NULL) {
                throw_error (context, CK_MANAGER_ERROR_INVALID_INPUT, _("Invalid session"));
                return TRUE;
        }

        if (arg_seat_id == NULL) {
                throw_error (context, CK_MANAGER_ERROR_INVALID_INPUT, _("Invalid seat"));
                return TRUE;
        }

        seat = g_hash_table_lookup (manager->priv->seats, arg_seat_id);

        if (!CK_IS_SEAT (seat)) {
                throw_error (context, CK_MANAGER_ERROR_INVALID_INPUT, _("Invalid seat"));
                return TRUE;
        }

        error = ck_seat_activate_session (seat, session, NULL);

        if (error != NULL) {
                throw_error (context, CK_MANAGER_ERROR_FAILED, error->message);
                g_clear_error (&error);
                return TRUE;
        }

        console_kit_manager_complete_activate_session_on_seat (ckmanager, context);
        return TRUE;
}

static gboolean
dbus_activate_session (ConsoleKitManager *ckmanager,
                       GDBusMethodInvocation *context,
                       const gchar *arg_session_id)
{
        CkManager *manager;
        CkSession *session = NULL;
        gchar   *seat_id = NULL;

        TRACE ();

        manager = CK_MANAGER (ckmanager);

        g_return_val_if_fail (CK_IS_MANAGER (manager), FALSE);

        session = get_session_from_id (manager, arg_session_id);
        if (session == NULL) {
                throw_error (context, CK_MANAGER_ERROR_INVALID_INPUT, _("Invalid session"));
                return TRUE;
        }

        ck_session_get_seat_id (session, &seat_id, NULL);

        if (seat_id == NULL) {
                throw_error (context, CK_MANAGER_ERROR_NO_SEATS, _("Session is not attached to a seat"));
                return TRUE;
        }

        dbus_activate_session_on_seat (ckmanager, context, arg_session_id, seat_id);

        g_free (seat_id);
        return TRUE;
}

static gboolean
dbus_lock_session (ConsoleKitManager *ckmanager,
                   GDBusMethodInvocation *context,
                   const gchar *arg_session_id)
{
        CkManager *manager;
        CkSession *session = NULL;

        TRACE ();

        manager = CK_MANAGER (ckmanager);

        g_return_val_if_fail (CK_IS_MANAGER (manager), FALSE);

        session = get_session_from_id (manager, arg_session_id);
        if (session == NULL) {
                throw_error (context, CK_MANAGER_ERROR_INVALID_INPUT, _("Invalid session"));
                return TRUE;
        }

        ck_session_lock (CK_SESSION (session));

        console_kit_manager_complete_lock_session (ckmanager, context);
        return TRUE;
}

static gboolean
dbus_unlock_session (ConsoleKitManager *ckmanager,
                     GDBusMethodInvocation *context,
                     const gchar *arg_session_id)
{
        CkManager *manager;
        CkSession *session = NULL;

        TRACE ();

        manager = CK_MANAGER (ckmanager);

        g_return_val_if_fail (CK_IS_MANAGER (manager), FALSE);

        session = get_session_from_id (manager, arg_session_id);
        if (session == NULL) {
                throw_error (context, CK_MANAGER_ERROR_INVALID_INPUT, _("Invalid session"));
                return TRUE;
        }

        ck_session_unlock (CK_SESSION (session));

        console_kit_manager_complete_unlock_session (ckmanager, context);
        return TRUE;
}

static gboolean
dbus_list_sessions (ConsoleKitManager     *ckmanager,
                    GDBusMethodInvocation *context)
{
        CkManager       *manager;
        GVariantBuilder  session_builder;
        GVariant        *session;
        GHashTableIter   session_iter;
        const gchar     *key;
        CkSeat          *value;

        TRACE ();

        manager = CK_MANAGER (ckmanager);

        g_return_val_if_fail (CK_IS_MANAGER (manager), FALSE);

        /* if we don't have sessions, we need to return NULL */
        if (g_hash_table_size (manager->priv->sessions) == 0) {
                throw_error (context, CK_MANAGER_ERROR_NO_SESSIONS, _("User has no sessions"));
                return TRUE;
        }

        g_variant_builder_init (&session_builder, G_VARIANT_TYPE_ARRAY);

        g_hash_table_iter_init (&session_iter, manager->priv->sessions);
        while (g_hash_table_iter_next (&session_iter,  (gpointer *)&key,  (gpointer *)&value)) {
                gchar *sid;
                struct passwd *ent = getpwuid (console_kit_session_get_unix_user ( CONSOLE_KIT_SESSION(value) ));
                ck_session_get_seat_id ( CK_SESSION(value), &sid, NULL );

                session = g_variant_new("(susso)",
                                        key,
                                        console_kit_session_get_unix_user ( CONSOLE_KIT_SESSION(value) ),
                                        ent ? ent->pw_name : "",
                                        sid,
                                        ck_session_get_path ( CK_SESSION(value) )
                                        );

                g_variant_builder_add_value (&session_builder, session);

                g_free (sid);
        }

        console_kit_manager_complete_list_sessions (ckmanager, context, g_variant_builder_end (&session_builder));
        return TRUE;
}

static gboolean
dbus_list_seats (ConsoleKitManager     *ckmanager,
                 GDBusMethodInvocation *context)
{
        CkManager       *manager;
        GVariantBuilder  seat_builder;
        GVariant        *seat;
        GHashTableIter   seat_iter;
        const gchar     *key;
        CkSeat          *value;

        TRACE ();

        manager = CK_MANAGER (ckmanager);

        g_return_val_if_fail (CK_IS_MANAGER (manager), FALSE);

        /* if we don't have seats, we need to return NULL */
        if (g_hash_table_size (manager->priv->seats) == 0) {
                throw_error (context, CK_MANAGER_ERROR_NO_SEATS, _("User has no seats"));
                return TRUE;
        }

        g_variant_builder_init (&seat_builder, G_VARIANT_TYPE_ARRAY);

        g_hash_table_iter_init (&seat_iter, manager->priv->seats);
        while (g_hash_table_iter_next (&seat_iter,  (gpointer *)&key,  (gpointer *)&value)) {
                seat = g_variant_new("(so)",
                                     console_kit_seat_get_name( CONSOLE_KIT_SEAT(value) ),
                                     ck_seat_get_path (value));

                g_variant_builder_add_value (&seat_builder, seat);
        }

        console_kit_manager_complete_list_seats (ckmanager, context, g_variant_builder_end (&seat_builder));
        return TRUE;
}

static gboolean
dbus_get_seats (ConsoleKitManager     *ckmanager,
                GDBusMethodInvocation *context)
{
        CkManager    *manager;
        GPtrArray    *seats;

        TRACE ();

        manager = CK_MANAGER (ckmanager);

        g_return_val_if_fail (CK_IS_MANAGER (manager), FALSE);

        seats = g_hash_table_get_values_as_ptr_array (manager->priv->seats);

        /* gdbus/gvariant requires that we throw an error to return NULL */
        if (seats->len == 0) {
                throw_error (context, CK_MANAGER_ERROR_NO_SEATS, _("User has no seats"));
                return TRUE;
        }

        /* replace each session with its path */
        for (guint i = 0; i < seats->len; i++) {
                seats->pdata[i] = (gpointer) ck_seat_get_path ( CK_SEAT(seats->pdata[i]) );
        }

        g_ptr_array_add (seats, NULL);
        console_kit_manager_complete_get_seats (ckmanager, context, (const gchar**)seats->pdata);
        g_ptr_array_unref (seats);
        return TRUE;
}

static gboolean
dbus_get_sessions (ConsoleKitManager     *ckmanager,
                   GDBusMethodInvocation *context)
{
        CkManager    *manager;
        GPtrArray    *sessions;

        TRACE ();

        manager = CK_MANAGER (ckmanager);

        g_return_val_if_fail (CK_IS_MANAGER (manager), FALSE);

        sessions = g_hash_table_get_values_as_ptr_array (manager->priv->sessions);

        /* gdbus/gvariant requires that we throw an error to return NULL */
        if (sessions->len == 0) {
                throw_error (context, CK_MANAGER_ERROR_NO_SESSIONS, _("There are no sessions"));
                return TRUE;
        }

        /* replace each session with its path */
        for (guint i = 0; i < sessions->len; i++) {
                sessions->pdata[i] = (gpointer)ck_session_get_path ( CK_SESSION(sessions->pdata[i]) );
        }

        g_ptr_array_add (sessions, NULL);
        console_kit_manager_complete_get_sessions (ckmanager, context, (const gchar**)sessions->pdata);
        g_ptr_array_unref (sessions);

        return TRUE;
}

static void
add_seat_for_file (CkManager  *manager,
                   const char *filename)
{
        char   *sid;
        CkSeat *seat;

        sid = generate_seat_id (manager);

        seat = ck_seat_new_from_file (sid, filename, manager->priv->connection);

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

        g_debug ("Emitting seat-added: %s", ck_seat_get_path (seat));
        console_kit_manager_emit_seat_added (CONSOLE_KIT_MANAGER (manager), ck_seat_get_path (seat));

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
on_inhibit_manager_changed_event (CkInhibitManager *manager, gint inhibit_mode, gint event, gboolean enabled, gpointer user_data)
{
        CkManagerPrivate *priv;

        g_return_if_fail (CK_IS_MANAGER (user_data));

        priv = CK_MANAGER_GET_PRIVATE (CK_MANAGER (user_data));

        /* No system action pending, return */
        if (priv->system_action_idle_id == 0) {
                return;
        }

        /* No system action data? normal shutdown/suspend, return */
        if (priv->system_action_data == NULL) {
                return;
        }

        /* this system action must be for a sleep or shutdown operation */
        if (priv->system_action_data->signal != PREPARE_FOR_SLEEP &&
            priv->system_action_data->signal != PREPARE_FOR_SHUTDOWN) {
                return;
        }

        /* the inhibit change must be for sleep or shutdown */
        if (event != CK_INHIBIT_EVENT_SUSPEND && event != CK_INHIBIT_EVENT_SHUTDOWN) {
                return;
        }

        /* must be a delay inhibitor */
        if (inhibit_mode != CK_INHIBIT_MODE_DELAY) {
                return;
        }

        /* the inhibit lock must be removed */
        if (enabled != FALSE) {
                return;
        }

        /* The inhibit lock for this action was removed.
         * Stop the timeout and call the system action now.
         */
        g_source_remove (priv->system_action_idle_id);
        priv->system_action_idle_id = 0;
        system_action_idle_cb (priv->system_action_data);
}

static void
ck_manager_init (CkManager *manager)
{

        manager->priv = CK_MANAGER_GET_PRIVATE (manager);

        /* reserve zero */
        manager->priv->session_serial = 1;
        manager->priv->seat_serial = 0;

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
        if (manager->priv->inhibit_manager) {
                g_signal_connect (manager->priv->inhibit_manager, "changed-event", G_CALLBACK (on_inhibit_manager_changed_event), manager);
        }

        manager->priv->system_action_idle_delay = 2 * 1000;
        manager->priv->system_action_idle_inhibited_delay = 8 * 1000;
        manager->priv->system_action_idle_id = 0;
}

static void
ck_manager_finalize (GObject *object)
{
        CkManager *manager;

        TRACE ();

        g_return_if_fail (object != NULL);
        g_return_if_fail (CK_IS_MANAGER (object));

        manager = CK_MANAGER (object);

        g_return_if_fail (manager->priv != NULL);

        g_hash_table_destroy (manager->priv->seats);
        g_hash_table_destroy (manager->priv->sessions);
        g_hash_table_destroy (manager->priv->leaders);

        if (manager->priv->name_owner_id > 0 && manager->priv->connection) {
                g_dbus_connection_signal_unsubscribe (manager->priv->connection, manager->priv->name_owner_id);
                manager->priv->name_owner_id = 0;
        }

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
ck_manager_new (GDBusConnection *connection)
{
        if (manager_object != NULL) {
                g_object_ref (manager_object);
        } else {
                gboolean res;

                manager_object = g_object_new (CK_TYPE_MANAGER, NULL);
                g_object_add_weak_pointer (manager_object,
                                           (gpointer *) &manager_object);
                res = register_manager (manager_object, connection);
                if (! res) {
                        g_object_unref (manager_object);
                        return NULL;
                }
        }

        return CK_MANAGER (manager_object);
}

static void
ck_manager_iface_init (ConsoleKitManagerIface *iface)
{
        iface->handle_can_hibernate                = dbus_can_hibernate;
        iface->handle_can_hybrid_sleep             = dbus_can_hybrid_sleep;
        iface->handle_can_power_off                = dbus_can_power_off;
        iface->handle_can_reboot                   = dbus_can_reboot;
        iface->handle_can_restart                  = dbus_can_restart;
        iface->handle_can_stop                     = dbus_can_stop;
        iface->handle_can_suspend                  = dbus_can_suspend;
        iface->handle_hibernate                    = dbus_hibernate;
        iface->handle_hybrid_sleep                 = dbus_hybrid_sleep;
        iface->handle_inhibit                      = dbus_inhibit;
        iface->handle_list_inhibitors              = dbus_list_inhibitors;
        iface->handle_power_off                    = dbus_power_off;
        iface->handle_reboot                       = dbus_reboot;
        iface->handle_restart                      = dbus_restart;
        iface->handle_stop                         = dbus_stop;
        iface->handle_suspend                      = dbus_suspend;
        iface->handle_close_session                = dbus_close_session;
        iface->handle_list_sessions                = dbus_list_sessions;
        iface->handle_list_seats                   = dbus_list_seats;
        iface->handle_get_seats                    = dbus_get_seats;
        iface->handle_get_sessions                 = dbus_get_sessions;
        iface->handle_get_sessions_for_unix_user   = dbus_get_sessions_for_unix_user;
        iface->handle_get_sessions_for_user        = dbus_get_sessions_for_user;
        iface->handle_get_session_for_cookie       = dbus_get_session_for_cookie;
        iface->handle_get_session_for_unix_process = dbus_get_session_for_unix_process;
        iface->handle_get_session_by_pid           = dbus_get_session_by_pid;
        iface->handle_get_current_session          = dbus_get_current_session;
        iface->handle_open_session                 = dbus_open_session;
        iface->handle_open_session_with_parameters = dbus_open_session_with_parameters;
        iface->handle_get_system_idle_hint         = dbus_get_system_idle_hint;
        iface->handle_get_system_idle_since_hint   = dbus_get_system_idle_since_hint;
        iface->handle_activate_session             = dbus_activate_session;
        iface->handle_activate_session_on_seat     = dbus_activate_session_on_seat;
        iface->handle_lock_session                 = dbus_lock_session;
        iface->handle_unlock_session               = dbus_unlock_session;
}
