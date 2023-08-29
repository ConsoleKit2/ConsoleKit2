/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006-2007 William Jon McCann <mccann@jhu.edu>
 * Copyright (C) 2023, Serenity Cybersecurity, LLC <license@futurecrew.ru>
 *                     Author: Gleb Popov <arrowd@FreeBSD.org>
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
#include <pwd.h>
#include <sys/ioctl.h>

#ifdef HAVE_SYS_VT_H
#include <sys/vt.h>
#endif

#ifdef HAVE_SYS_KD_H
#include <sys/kd.h>
#endif

#ifdef HAVE_SYS_CONSIO_H
#include <sys/consio.h>
#endif

#ifdef HAVE_SYS_KBIO_H
#include <sys/kbio.h>
#endif

#ifdef HAVE_DEV_WSCONS_WSCONSIO_H
#include <dev/wscons/wsconsio.h>
#endif

#ifdef HAVE_DEV_WSCONS_WSDISPLAY_USL_IO_H
#include <dev/wscons/wsdisplay_usl_io.h>
#endif

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#include <termios.h>
#endif

#include <glib.h>
#include <glib-unix.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include "ck-tty-idle-monitor.h"
#include "ck-manager.h"
#include "ck-session.h"
#include "ck-seat.h"
#include "ck-marshal.h"
#include "ck-run-programs.h"
#include "ck-sysdeps.h"
#include "ck-device.h"

#define CK_SESSION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CK_TYPE_SESSION, CkSessionPrivate))

#define CK_SESSION_DBUS_NAME "org.freedesktop.ConsoleKit.Session"

#define NONULL_STRING(x) ((x) != NULL ? (x) : "")

#define IDLE_TIME_SECS 60

#ifndef KDSKBMUTE
#define KDSKBMUTE 0x4B51
#endif

#ifdef K_OFF
#define KBD_OFF_MODE K_OFF
#else
#define KBD_OFF_MODE K_RAW
#endif

struct CkSessionPrivate
{
        char            *id;
        char            *path;
        char            *cookie;
        char            *seat_id;
        char            *seat_path;
        char            *runtime_dir;
        char            *login_session_id;

        gchar           *session_controller;
        guint            session_controller_watchid;
        GList           *devices;
        guint            pause_devices_timer;
        gint             tty_fd;
        gint             old_kbd_mode;
        guint            sig_watch_s1;
        guint            sig_watch_s2;

        GTimeVal         creation_time;

        CkTtyIdleMonitor *idle_monitor;

        GTimeVal         idle_since_hint;

        GDBusConnection *connection;
        GDBusProxy      *bus_proxy;
};

enum {
        ACTIVATE,
        LAST_SIGNAL
};

/* Private properties not exported over D-BUS */
enum {
        PROP_0,
        PROP_ID,
        PROP_COOKIE,
        PROP_LOGIN_SESSION_ID,
        PROP_SESSION_CONTROLLER,
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     ck_session_iface_init           (ConsoleKitSessionIface *iface);
static void     ck_session_finalize             (GObject                *object);
static void     ck_session_remove_all_devices   (CkSession              *session);


G_DEFINE_TYPE_WITH_CODE (CkSession, ck_session, CONSOLE_KIT_TYPE_SESSION_SKELETON, G_IMPLEMENT_INTERFACE (CONSOLE_KIT_TYPE_SESSION, ck_session_iface_init));

static const GDBusErrorEntry ck_session_error_entries[] =
{
        { CK_SESSION_ERROR_FAILED,                  CK_SESSION_DBUS_NAME ".Error.Failed" },
        { CK_SESSION_ERROR_GENERAL,                 CK_SESSION_DBUS_NAME ".Error.General" },
        { CK_SESSION_ERROR_INSUFFICIENT_PERMISSION, CK_SESSION_DBUS_NAME ".Error.InsufficientPermission" },
        { CK_SESSION_ERROR_NOT_SUPPORTED,           CK_SESSION_DBUS_NAME ".Error.NotSupported" },
        { CK_SESSION_ERROR_ALREADY_ACTIVE,          CK_SESSION_DBUS_NAME ".Error.AlreadyActive" }
};

GQuark
ck_session_error_quark (void)
{
        static volatile gsize quark_volatile = 0;

        g_dbus_error_register_error_domain ("ck_session_error",
                                            &quark_volatile,
                                            ck_session_error_entries,
                                            G_N_ELEMENTS (ck_session_error_entries));

        return (GQuark) quark_volatile;
}
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
ck_session_error_get_type (void)
{
  static GType etype = 0;

  if (etype == 0)
    {
      static const GEnumValue values[] =
        {
          ENUM_ENTRY (CK_SESSION_ERROR_FAILED,                  "Failed"),
          ENUM_ENTRY (CK_SESSION_ERROR_GENERAL,                 "General"),
          ENUM_ENTRY (CK_SESSION_ERROR_INSUFFICIENT_PERMISSION, "InsufficientPermission"),
          ENUM_ENTRY (CK_SESSION_ERROR_NOT_SUPPORTED,           "NotSupported"),
          ENUM_ENTRY (CK_SESSION_ERROR_ALREADY_ACTIVE,          "AlreadyActive"),
          { 0, 0, 0 }
        };
      g_assert (CK_SESSION_NUM_ERRORS == G_N_ELEMENTS (values) - 1);
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

        g_debug ("session: throwing error: %s", message);

        g_dbus_method_invocation_return_error (context, CK_SESSION_ERROR, error_code, "%s", message);

        g_free (message);
}

static gboolean
register_session (CkSession *session, GDBusConnection *connection)
{
        GError *error = NULL;

        g_debug ("register session");

        error = NULL;

        if (connection == NULL) {
                g_critical ("CkSession-register_session:connection == NULL");
        } else {
                session->priv->connection = connection;
        }


        if (session->priv->connection == NULL) {
                if (error != NULL) {
                        g_critical ("error getting system bus: %s", error->message);
                        g_error_free (error);
                }
                return FALSE;
        }

        g_debug ("exporting path %s", session->priv->path);

        if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (CONSOLE_KIT_SESSION (session)),
                                               session->priv->connection,
                                               session->priv->path,
                                               &error)) {
                if (error != NULL) {
                        g_critical ("error exporting interface: %s", error->message);
                        g_error_free (error);
                        return FALSE;
                }
        }

        g_debug ("exported on %s", g_dbus_interface_skeleton_get_object_path (G_DBUS_INTERFACE_SKELETON (CONSOLE_KIT_SESSION (session))));

        /* connect to DBus for get_caller_info */
        session->priv->bus_proxy = g_dbus_proxy_new_sync (session->priv->connection,
                                                          G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
                                                          G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS,
                                                          NULL,
                                                          "org.freedesktop.DBus",
                                                          "/org/freedesktop/DBus/Bus",
                                                          "org.freedesktop.DBus",
                                                          NULL,
                                                          &error);
        if (session->priv->bus_proxy == NULL) {
                g_warning ("cannot connect to DBus: %s", error->message);
                g_error_free (error);
        }

        /* default to unspecified for the session type on startup */
        if (console_kit_session_get_session_type (CONSOLE_KIT_SESSION (session)) == NULL) {
                console_kit_session_set_session_type (CONSOLE_KIT_SESSION (session), "unspecified");
        }

        /* default to unspecified for the session service on startup */
        if (console_kit_session_get_session_service (CONSOLE_KIT_SESSION (session)) == NULL) {
                console_kit_session_set_session_service (CONSOLE_KIT_SESSION (session), "unspecified");
        }

        /* default to user for the session class on startup */
        if (console_kit_session_get_session_class (CONSOLE_KIT_SESSION (session)) == NULL) {
                console_kit_session_set_session_class (CONSOLE_KIT_SESSION (session), "user");
        }

        /* default to online for the session state on startup */
        if (console_kit_session_get_session_class (CONSOLE_KIT_SESSION (session)) == NULL) {
                console_kit_session_set_session_class (CONSOLE_KIT_SESSION (session), "online");
        }

        return TRUE;
}

/*
  lock and unlock are separate functions because we may want
  security policy to be handled separately
*/
void
ck_session_lock (CkSession *session)
{
        ConsoleKitSession     *cksession;

        TRACE ();

        g_return_if_fail (CK_IS_SESSION (session));

        cksession = CONSOLE_KIT_SESSION (session);

        g_debug ("Emitting lock for session %s", session->priv->id);
        console_kit_session_set_locked_hint (cksession, TRUE);
        console_kit_session_emit_lock (cksession);
}

void
ck_session_unlock (CkSession *session)
{
        ConsoleKitSession     *cksession;

        TRACE ();

        g_return_if_fail (CK_IS_SESSION (session));

        cksession = CONSOLE_KIT_SESSION (session);

        g_debug ("Emitting unlock for session %s", session->priv->id);
        console_kit_session_set_locked_hint (cksession, FALSE);
        console_kit_session_emit_unlock (cksession);
}

static gboolean
dbus_lock (ConsoleKitSession     *cksession,
           GDBusMethodInvocation *context)
{
        TRACE ();

        ck_session_lock (CK_SESSION (cksession));

        console_kit_session_complete_lock (cksession, context);
        return TRUE;
}

static gboolean
dbus_unlock (ConsoleKitSession     *cksession,
             GDBusMethodInvocation *context)
{
        TRACE ();

        ck_session_unlock (CK_SESSION (cksession));

        console_kit_session_complete_unlock (cksession, context);
        return TRUE;
}

/* adapted from PolicyKit */
static gboolean
get_caller_info (CkSession   *session,
                 const char  *sender,
                 uid_t       *calling_uid,
                 pid_t       *calling_pid)
{
        gboolean  res   = FALSE;
        GVariant *value = NULL;
        GError   *error = NULL;

        if (sender == NULL) {
                goto out;
        }

        value = g_dbus_proxy_call_sync (session->priv->bus_proxy, "GetConnectionUnixUser",
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

        value = g_dbus_proxy_call_sync (session->priv->bus_proxy, "GetConnectionUnixProcessID",
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

static gboolean
session_set_idle_hint_internal (CkSession      *session,
                                gboolean        idle_hint)
{
        ConsoleKitSession *cksession = CONSOLE_KIT_SESSION (session);

        if (console_kit_session_get_idle_hint (cksession) != idle_hint) {
                console_kit_session_set_idle_hint (cksession, idle_hint);

                /* FIXME: can we get a time from the dbus message? */
                g_get_current_time (&session->priv->idle_since_hint);

                g_debug ("Emitting idle-changed for session %s", session->priv->id);
                console_kit_session_emit_idle_hint_changed (cksession, idle_hint);
        }

        return TRUE;
}

static gboolean
dbus_get_idle_since_hint (ConsoleKitSession     *cksession,
                          GDBusMethodInvocation *context)
{
        CkSession *session = CK_SESSION(cksession);
        char *date_str;

        TRACE ();

        g_return_val_if_fail (CK_IS_SESSION (cksession), FALSE);

        if (console_kit_session_get_idle_hint (cksession) == FALSE) {
                throw_error (context, CK_SESSION_ERROR_GENERAL, "idle since hint not set");
                return TRUE;
        }

        date_str = g_time_val_to_iso8601 (&session->priv->idle_since_hint);

        console_kit_session_complete_get_idle_since_hint (cksession, context, date_str);

        g_free (date_str);
        return TRUE;
}

static gboolean
dbus_get_idle_hint (ConsoleKitSession *cksession,
                    GDBusMethodInvocation *context)
{
        TRACE ();
        console_kit_session_complete_get_idle_hint (cksession, context, console_kit_session_get_idle_hint (cksession));
        return TRUE;
}

/*
  Example:
  dbus-send --system --dest=org.freedesktop.ConsoleKit \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/freedesktop/ConsoleKit/Session1 \
  org.freedesktop.ConsoleKit.Session.SetIdleHint boolean:TRUE
*/
static gboolean
dbus_set_idle_hint (ConsoleKitSession     *cksession,
                    GDBusMethodInvocation *context,
                    gboolean               idle_hint)
{
        const char *sender;
        uid_t       calling_uid = 0;
        pid_t       calling_pid = 0;
        gboolean    res;
        CkSession *session;

        TRACE ();

        g_return_val_if_fail (CK_IS_SESSION (cksession), FALSE);

        session = CK_SESSION(cksession);

        sender = g_dbus_method_invocation_get_sender (context);

        res = get_caller_info (session,
                               sender,
                               &calling_uid,
                               &calling_pid);

        if (! res) {
                g_warning ("stat on pid %d failed", calling_pid);
                throw_error (context, CK_SESSION_ERROR_FAILED, _("Unable to lookup information about calling process '%d'"), calling_pid);
                return TRUE;
        }

        /* only restrict this by UID for now */
        if (console_kit_session_get_unix_user (cksession) != calling_uid) {
                throw_error (context, CK_SESSION_ERROR_INSUFFICIENT_PERMISSION, _("Only session owner may set idle hint state"));
                return TRUE;
        }

        session_set_idle_hint_internal (session, idle_hint);

        console_kit_session_complete_set_idle_hint (cksession, context);
        return TRUE;
}

/*
  Example:
  dbus-send --system --dest=org.freedesktop.ConsoleKit \
  --type=method_call --print-reply --reply-timeout=2000 \
  /org/freedesktop/ConsoleKit/Session1 \
  org.freedesktop.ConsoleKit.Session.SetLockedHint boolean:TRUE
*/
static gboolean
dbus_set_locked_hint (ConsoleKitSession     *cksession,
                      GDBusMethodInvocation *context,
                      gboolean               arg_locked_hint)
{
        const char *sender;
        uid_t       calling_uid = 0;
        pid_t       calling_pid = 0;
        gboolean    res;
        CkSession *session;

        TRACE ();

        g_return_val_if_fail (CK_IS_SESSION (cksession), FALSE);

        session = CK_SESSION(cksession);

        sender = g_dbus_method_invocation_get_sender (context);

        res = get_caller_info (session,
                               sender,
                               &calling_uid,
                               &calling_pid);

        if (! res) {
                g_warning ("stat on pid %d failed", calling_pid);
                throw_error (context, CK_SESSION_ERROR_FAILED, _("Unable to lookup information about calling process '%d'"), calling_pid);
                return TRUE;
        }

        /* only restrict this by UID for now */
        if (console_kit_session_get_unix_user (cksession) != calling_uid) {
                throw_error (context, CK_SESSION_ERROR_INSUFFICIENT_PERMISSION, _("Only session owner may set locked hint state"));
                return TRUE;
        }

        console_kit_session_set_locked_hint (cksession, arg_locked_hint);

        console_kit_session_complete_set_idle_hint (cksession, context);
        return TRUE;
}

static gboolean
dbus_get_session_service (ConsoleKitSession     *cksession,
                          GDBusMethodInvocation *context)
{
        const gchar *session_service = console_kit_session_get_session_service (cksession);

        TRACE ();

        if (session_service == NULL) {
                /* default to unspecified */
                session_service = "unspecified";
        }

        console_kit_session_complete_get_session_service (cksession, context, session_service);
        return TRUE;
}

static gboolean
dbus_get_session_type (ConsoleKitSession     *cksession,
                       GDBusMethodInvocation *context)
{
        const gchar *session_type = console_kit_session_get_session_type (cksession);

        TRACE ();

        if (session_type == NULL) {
                /* default to unspecified */
                session_type = "unspecified";
        }

        console_kit_session_complete_get_session_type (cksession, context, session_type);
        return TRUE;
}

static gboolean
dbus_get_session_class (ConsoleKitSession     *cksession,
                        GDBusMethodInvocation *context)
{
        const gchar *session_class = console_kit_session_get_session_class (cksession);

        TRACE ();

        if (session_class == NULL) {
                /* default to user */
                session_class = "user";
        }

        console_kit_session_complete_get_session_class (cksession, context, session_class);
        return TRUE;
}

static gboolean
dbus_get_session_state (ConsoleKitSession     *cksession,
                        GDBusMethodInvocation *context)
{
        const gchar *state = console_kit_session_get_session_state (cksession);

        TRACE ();

        if (state == NULL) {
                /* default to online, but this shouldn't really happen */
                state = "online";
        }

        console_kit_session_complete_get_session_state (cksession, context, state);
        return TRUE;
}

static void
ck_session_print_list_size (CkSession *session)
{
#if defined(CONSOLEKIT_DEBUGGING)
        g_debug ("session %s list size is %d",
                 session->priv->id,
                 g_list_length (session->priv->devices));
#endif
}

static void
ck_session_check_paused_devices (CkSession *session)
{
        ConsoleKitSession *cksession = CONSOLE_KIT_SESSION (session);
        GList             *itr;

        TRACE ();

        ck_session_print_list_size (session);

        /* See if we've paused all the devices in the session */
        for (itr = session->priv->devices; itr != NULL; itr = g_list_next (itr)) {
                CkDevice *device = CK_DEVICE (itr->data);
                if (ck_device_get_active (device)) {
                        return;
                }
        }

        /* If we didn't force the state change, do it now */
        if (console_kit_session_get_active (cksession) != FALSE) {
                g_debug ("marking session %s inactive", session->priv->id);

                console_kit_session_set_active (cksession, FALSE);
                console_kit_session_emit_active_changed (cksession, FALSE);
                console_kit_session_set_session_state (cksession, "online");
        }

        if (session->priv->pause_devices_timer != 0) {
                g_source_remove (session->priv->pause_devices_timer);
                session->priv->pause_devices_timer = 0;
        }
}

static gboolean
force_pause_devices (CkSession *session)
{
        GList *itr;

        TRACE ();

        ck_session_print_list_size (session);

        for (itr = session->priv->devices; itr != NULL; itr = g_list_next (itr))
        {
                ck_device_set_active (CK_DEVICE (itr->data), FALSE);
        }

        session->priv->pause_devices_timer = 0;

        ck_session_check_paused_devices (session);

        return FALSE;
}

static void
ck_session_pause_all_devices (CkSession *session,
                              gboolean   force)
{
        ConsoleKitSession *cksession = CONSOLE_KIT_SESSION (session);
        GList             *itr;

        TRACE ();

        ck_session_print_list_size (session);

        for (itr = session->priv->devices; itr != NULL; itr = g_list_next (itr))
        {
                CkDevice *device = CK_DEVICE (itr->data);

                if (ck_device_get_active (device) == FALSE) {
                        g_debug ("device already paused");
                        continue;
                }

                /* Let the session controller know about the change */
                g_debug ("emit pause device for %d, %d, type %s",
                         ck_device_get_major (device),
                         ck_device_get_minor (device),
                         force ? "force" : "pause");

                console_kit_session_emit_pause_device (CONSOLE_KIT_SESSION (session),
                                                       ck_device_get_major (device),
                                                       ck_device_get_minor (device),
                                                       force ? "force" : "pause");

                if (force) {
                        ck_device_set_active (device, FALSE);
                }
        }

        if (force || session->priv->devices == NULL) {
                g_debug ("marking session %s inactive", session->priv->id);

                console_kit_session_set_active (cksession, FALSE);
                console_kit_session_emit_active_changed (cksession, FALSE);
                console_kit_session_set_session_state (cksession, "online");
        } else {
                session->priv->pause_devices_timer = g_timeout_add_seconds (3, (GSourceFunc)force_pause_devices, session);
        }
}

static void
ck_session_debug_print_dbus_message (CkSession    *session,
                                     GDBusMessage *message)
{
#if defined(CONSOLEKIT_DEBUGGING)
        gchar  *string = g_dbus_message_print (message, 4);
        gchar **split_string = g_strsplit (string, "\n", 0);
        gint    i;

        for (i = 0; split_string != NULL && split_string[i] != NULL; i++) {
                g_debug ("%s", split_string[i]);
        }

        g_strfreev (split_string);
        g_free (string);
#endif
}

static void
ck_session_resume_all_devices (CkSession *session)
{
        ConsoleKitSession *cksession = CONSOLE_KIT_SESSION (session);
        GList             *itr;
        gint               vtnr;

        TRACE ();

        vtnr = console_kit_session_get_vtnr (cksession);

        /* Give ownership of the active console device to the user */
        if (vtnr > 0) {
                struct passwd *pwent;
                gint           fd;
                gint           uid;

                uid = console_kit_session_get_unix_user (cksession);

                fd = ck_open_a_console (ck_get_console_device_for_num (vtnr));
                if (fd >= 0) {
                        errno = 0;
                        pwent = getpwuid (uid);
                        if (pwent == NULL) {
                                g_warning ("Unable to lookup UID: %s", g_strerror (errno));
                                errno = 0;
                        } else {
                                if (fchown (fd, uid, pwent->pw_gid) != 0) {
                                        g_warning ("Failed to chown console device, reason was: %s", strerror(errno));
                                        errno = 0;
                                }
                        }

                        g_close (fd, NULL);
                }
        }

        /* without a controller, we just mark ourselved active */
        if (session->priv->session_controller == NULL) {
                g_debug ("no session controller: marking session active");
                console_kit_session_set_active (cksession, TRUE);
                console_kit_session_emit_active_changed (cksession, TRUE);
                console_kit_session_set_session_state (cksession, "active");
                return;
        }

        ck_session_print_list_size (session);

        for (itr = session->priv->devices; itr != NULL; itr = g_list_next (itr))
        {
                CkDevice     *device = CK_DEVICE (itr->data);
                GVariant     *body;
                GDBusMessage *message;
                GUnixFDList  *out_fd_list = NULL;
                gint          fd = -1;
                gint          fd2 = -1;

                ck_device_set_active (device, TRUE);

                fd = ck_device_get_fd (device);

                if (fd < 0) {
                        g_debug ("Unable to signal ResumeDevice, failed to get device");
                        continue;
                }

                message = g_dbus_message_new_signal (session->priv->id,
                                                     DBUS_SESSION_INTERFACE,
                                                     "ResumeDevice");

                /* We always send to the session controller */
                g_dbus_message_set_destination (message, session->priv->session_controller);

                body = g_variant_new ("(uu@h)",
                                      ck_device_get_major (device),
                                      ck_device_get_minor (device),
                                      g_variant_new_handle (0));

                g_dbus_message_set_body (message, body);

                /* we need to copy this, because gdbus closes the fd on us */
                fd2 = dup (fd);
                out_fd_list = g_unix_fd_list_new_from_array (&fd2, 1);
                g_dbus_message_set_unix_fd_list (message, out_fd_list);

                g_debug ("sending ResumeDevice signal to session controller");

                ck_session_debug_print_dbus_message (session, message);

                /* Let only the session controller know about the change and
                 * give them the new fd. */
                g_dbus_connection_send_message (session->priv->connection,
                                                message,
                                                G_DBUS_SEND_MESSAGE_FLAGS_NONE,
                                                NULL,
                                                NULL);

                g_clear_object (&out_fd_list);
                g_clear_object (&message);
        }

        g_debug ("marking session active");
        console_kit_session_set_active (cksession, TRUE);
        console_kit_session_emit_active_changed (cksession, TRUE);
        console_kit_session_set_session_state (cksession, "active");
}

gboolean
ck_session_set_active (CkSession *session,
                       gboolean   active,
                       gboolean   force)
{
        ConsoleKitSession *cksession;

        TRACE ();

        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        cksession = CONSOLE_KIT_SESSION (session);

        if (console_kit_session_get_active (cksession) == active) {
                /* redundant call, we shouldn't need to do anything */
                return TRUE;
        }


        if (session->priv->pause_devices_timer != 0) {
                g_source_remove (session->priv->pause_devices_timer);
                session->priv->pause_devices_timer = 0;
        }

        g_debug ("ck_session_set_active: session %s changing to %s, forced? %s",
                 session->priv->id,
                 active ? "active" : "not active",
                 force ? "yes" : "no");

        /* We handle device events then change the active state */
        if (active == FALSE) {
                ck_session_pause_all_devices (session, force);
        } else {
                ck_session_resume_all_devices (session);
        }

        return TRUE;
}

static gboolean
dbus_is_active (ConsoleKitSession     *cksession,
                GDBusMethodInvocation *context)
{
        TRACE ();
        console_kit_session_complete_is_active (cksession, context, console_kit_session_get_active (cksession));
        return TRUE;
}

static gboolean
dbus_activate (ConsoleKitSession     *cksession,
               GDBusMethodInvocation *context)
{
        GError    *error   = NULL;
        GError    *initial_error;
        CkSession *session = CK_SESSION (cksession);

        TRACE ();

        g_return_val_if_fail (session, FALSE);

        /* Set an initial error message in the event the signal isn't handeled */
        g_set_error (&error, CK_SESSION_ERROR, CK_SESSION_ERROR_NOT_SUPPORTED,
                     _("Activate signal not handeled. Session not attached to seat, or the seat doesn't support activation changes"));

        /* keep track of the starting error because the call to g_signal_emit
         * may change it and we still need to free it */
        initial_error = error;

        g_signal_emit (session, signals [ACTIVATE], 0, context, &error);
        if (error != NULL) {
                /* if the signal is not handled then either:
                   a) aren't attached to seat
                   b) seat doesn't support activation changes */
                g_debug ("Got error message: %s", error->message);

                /* translate and throw the error */
                switch (error->code) {
                case CK_SEAT_ERROR_ALREADY_ACTIVE:
                        /* Don't care */
                        break;
                case CK_SEAT_ERROR_NOT_SUPPORTED:
                        throw_error (context, CK_SESSION_ERROR_NOT_SUPPORTED, error->message);
                        break;
                default:
                        throw_error (context, CK_SESSION_ERROR_GENERAL, error->message);
                }

                g_clear_error (&error);
                g_clear_error (&initial_error);
                return TRUE;
        }

        g_clear_error (&initial_error);

        console_kit_session_complete_activate (cksession, context);
        return TRUE;
}

static gboolean
dbus_get_id (ConsoleKitSession     *cksession,
             GDBusMethodInvocation *context)
{
        CkSession *session = CK_SESSION (cksession);
        gchar     *id;

        TRACE ();

        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        id = session->priv->id;

        if (id == NULL) {
                id = "";
        }

        console_kit_session_complete_get_id (cksession, context, session->priv->path);
        return TRUE;
}

gboolean
ck_session_get_id (CkSession *session,
                   char **id,
                   GError **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        if (id != NULL) {
                *id = g_strdup (session->priv->id);
        }

        return TRUE;
}

const char *ck_session_get_path (CkSession *session)
{
        return session->priv->path;
}

static gboolean
dbus_get_seat_id (ConsoleKitSession     *cksession,
                  GDBusMethodInvocation *context)
{
        CkSession *session = CK_SESSION (cksession);
        const gchar *seat_path;

        TRACE ();

        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        seat_path = session->priv->seat_id;
        if (seat_path == NULL) {
                throw_error (context, CK_SESSION_ERROR_FAILED, "session not attached to a seat");
                return TRUE;
        }

        console_kit_session_complete_get_seat_id (cksession, context, session->priv->seat_path);
        return TRUE;
}

gboolean
ck_session_get_seat_id (CkSession      *session,
                        char          **id,
                        GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        if (id != NULL) {
                *id = g_strdup (session->priv->seat_id);
        }

        return TRUE;
}

static gboolean
dbus_get_user (ConsoleKitSession     *cksession,
               GDBusMethodInvocation *context)
{
        TRACE ();
        console_kit_session_complete_get_unix_user (cksession, context, console_kit_session_get_unix_user (cksession));
        return TRUE;
}

static gboolean
dbus_get_unix_user (ConsoleKitSession     *cksession,
                    GDBusMethodInvocation *context)
{
        TRACE ();
        console_kit_session_complete_get_unix_user (cksession, context, console_kit_session_get_unix_user (cksession));
        return TRUE;
}

gboolean
ck_session_set_unix_user (CkSession      *session,
                          guint           uid,
                          GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        console_kit_session_set_unix_user (CONSOLE_KIT_SESSION (session), uid);

        return TRUE;
}

static gboolean
dbus_get_x11_display (ConsoleKitSession     *cksession,
                      GDBusMethodInvocation *context)
{
        const gchar *x11_display;

        TRACE ();

        x11_display = console_kit_session_get_x11_display (cksession);

        if (x11_display == NULL) {
                x11_display = "";
        }

        console_kit_session_complete_get_x11_display (cksession, context, x11_display);
        return TRUE;
}

gboolean
ck_session_set_x11_display (CkSession      *session,
                            const char     *x11_display,
                            GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        console_kit_session_set_x11_display (CONSOLE_KIT_SESSION (session), x11_display);

        return TRUE;
}

static gboolean
dbus_get_display_device (ConsoleKitSession     *cksession,
                             GDBusMethodInvocation *context)
{
        const gchar *display_device;
        TRACE ();

        display_device = console_kit_session_get_display_device (cksession);

        if (display_device == NULL) {
                display_device = "";
        }

        console_kit_session_complete_get_x11_display_device (cksession, context, display_device);
        return TRUE;
}

gboolean
ck_session_set_display_device (CkSession      *session,
                               const char     *display_device,
                               GError        **error)
{
        ConsoleKitSession *cksession;

        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        cksession = CONSOLE_KIT_SESSION (session);

        console_kit_session_set_display_device (cksession, display_device);

        return TRUE;
}

static gboolean
dbus_get_x11_display_device (ConsoleKitSession     *cksession,
                             GDBusMethodInvocation *context)
{
        const gchar *x11_display_device;

        TRACE ();

        x11_display_device = console_kit_session_get_x11_display_device (cksession);

        if (x11_display_device == NULL) {
                x11_display_device = "";
        }

        console_kit_session_complete_get_x11_display_device (cksession, context, x11_display_device);
        return TRUE;
}

gboolean
ck_session_set_x11_display_device (CkSession      *session,
                                   const char     *x11_display_device,
                                   GError        **error)
{
        ConsoleKitSession *cksession;

        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        cksession = CONSOLE_KIT_SESSION (session);

        console_kit_session_set_x11_display_device (cksession, x11_display_device);

        return TRUE;
}

static gboolean
dbus_get_remote_host_name (ConsoleKitSession     *cksession,
                           GDBusMethodInvocation *context)
{
        const gchar *remote_host_name = console_kit_session_get_remote_host_name (cksession);

        TRACE ();

        if (remote_host_name == NULL) {
                remote_host_name = "";
        }

        console_kit_session_complete_get_remote_host_name (cksession, context, remote_host_name);
        return TRUE;
}

gboolean
ck_session_set_remote_host_name (CkSession      *session,
                                 const char     *remote_host_name,
                                 GError        **error)
{
        ConsoleKitSession *cksession;

        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        cksession = CONSOLE_KIT_SESSION (session);

        console_kit_session_set_remote_host_name (cksession, remote_host_name);

        return TRUE;
}

static gboolean
dbus_get_creation_time (ConsoleKitSession     *cksession,
                        GDBusMethodInvocation *context)
{
        CkSession *session = CK_SESSION(cksession);

        TRACE ();

        g_return_val_if_fail (CK_IS_SESSION (cksession), FALSE);

        console_kit_session_complete_get_creation_time (cksession, context, g_time_val_to_iso8601 (&session->priv->creation_time));
        return TRUE;
}

gboolean
ck_session_get_creation_time (CkSession      *session,
                              char          **iso8601_datetime,
                              GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        if (iso8601_datetime != NULL) {
                *iso8601_datetime = g_time_val_to_iso8601 (&session->priv->creation_time);
        }

        return TRUE;
}

static gboolean
dbus_is_local (ConsoleKitSession     *cksession,
               GDBusMethodInvocation *context)
{
        TRACE ();
        console_kit_session_complete_is_local (cksession, context, console_kit_session_get_is_local (cksession));
        return TRUE;
}

gboolean
ck_session_set_is_local (CkSession      *session,
                         gboolean        is_local,
                         GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        console_kit_session_set_is_local (CONSOLE_KIT_SESSION (session), is_local);

        return TRUE;
}

gboolean
ck_session_is_local (CkSession *session,
                     gboolean *local,
                     GError **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        if (local != NULL) {
                *local = console_kit_session_get_is_local (CONSOLE_KIT_SESSION (session));
        }

        return TRUE;
}

gboolean
ck_session_set_id (CkSession      *session,
                   const char     *id,
                   GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        g_free (session->priv->id);
        session->priv->id = g_strdup (id);

        return TRUE;
}

gboolean
ck_session_set_cookie (CkSession      *session,
                       const char     *cookie,
                       GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        g_free (session->priv->cookie);
        session->priv->cookie = g_strdup (cookie);

        return TRUE;
}

gboolean
ck_session_set_seat_id (CkSession      *session,
                        const char     *id,
                        const char     *path,
                        GError        **error)
{
        GVariant *seat = NULL;
        gchar **seat_split;

        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        g_free (session->priv->seat_id);
        session->priv->seat_id = g_strdup (id);
        g_free (session->priv->seat_path);
        session->priv->seat_path = g_strdup (path);

        if (id != NULL && path != NULL) {
                seat = g_variant_new ("(so)", id, path);
        } else {
                g_critical ("id %s or path %s are invalid", id, path);
                return FALSE;
        }

        console_kit_session_set_seat (CONSOLE_KIT_SESSION (session), seat);
        return TRUE;
}

/**
 * ck_session_set_runtime_dir
 * @session: CkSession object
 * @runtime_dir: The XDG_RUNTIME_DIR for the user of this session. For details, see:
 *  http://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html
 * returns: TRUE if successfully set, FALSE on failure.
 **/
gboolean
ck_session_set_runtime_dir (CkSession      *session,
                            const char     *runtime_dir)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        g_free (session->priv->runtime_dir);
        session->priv->runtime_dir = g_strdup (runtime_dir);

        return TRUE;
}

/**
 * ck_session_get_runtime_dir
 * @session: CkSession object
 * returns: The XDG_RUNTIME_DIR. For details, see:
 *  http://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html
 **/
const char *
ck_session_get_runtime_dir (CkSession *session)
{
        g_return_val_if_fail (CK_IS_SESSION (session), NULL);
        return session->priv->runtime_dir;
}

static gboolean
dbus_get_runtime_dir (ConsoleKitSession     *cksession,
                      GDBusMethodInvocation *context)
{
        CkSession   *session = CK_SESSION(cksession);

        TRACE ();

        g_return_val_if_fail (CK_IS_SESSION (cksession), FALSE);

        /* if no login session id is set return an empty string */
        if (session->priv->runtime_dir == NULL) {
            throw_error (context, CK_SESSION_ERROR_FAILED, _("Failed to create the XDG_RUNTIME_DIR"));
            return TRUE;
        }

        console_kit_session_complete_get_xdgruntime_dir (cksession, context, session->priv->runtime_dir);
        return TRUE;
}

static gboolean
dbus_get_login_session_id (ConsoleKitSession     *cksession,
                           GDBusMethodInvocation *context)
{
        CkSession *session = CK_SESSION(cksession);
        const gchar *login_session_id;

        TRACE ();

        g_return_val_if_fail (CK_IS_SESSION (cksession), FALSE);

        login_session_id = session->priv->login_session_id;

        /* if no login session id is set return an empty string */
        if (login_session_id == NULL) {
            login_session_id = "";
        }

        console_kit_session_complete_get_login_session_id (cksession, context, login_session_id);
        return TRUE;
}

static gboolean
dbus_get_vtnr (ConsoleKitSession     *cksession,
               GDBusMethodInvocation *context)
{
        TRACE ();

        console_kit_session_complete_get_vtnr (cksession, context, console_kit_session_get_vtnr (cksession));
        return TRUE;
}

gboolean
ck_session_set_login_session_id (CkSession      *session,
                                 const char     *login_session_id,
                                 GError        **error)
{
        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        g_free (session->priv->login_session_id);
        session->priv->login_session_id = g_strdup (login_session_id);

        return TRUE;
}

gboolean
ck_session_set_session_service (CkSession      *session,
                                const char     *service,
                                GError        **error)
{
        ConsoleKitSession *cksession = CONSOLE_KIT_SESSION (session);

        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        console_kit_session_set_session_service (cksession, service);

        return TRUE;
}

gboolean
ck_session_set_session_type (CkSession      *session,
                             const char     *type,
                             GError        **error)
{
        ConsoleKitSession *cksession = CONSOLE_KIT_SESSION (session);

        g_return_val_if_fail (CK_IS_SESSION (session), FALSE);

        console_kit_session_set_session_type (cksession, type);

        return TRUE;
}

static gboolean
vt_leave_handler (gpointer data)
{
        CkSession *session = CK_SESSION (data);

        TRACE ();

        /* g_unix_signal_add_full returns this callback in the GMainContext
         * so we don't have the limitations of POSIX signal handlers */

        /* Forcably disable all devices, we have to do this
         * now or we'll crash Xorg if it's running as root on
         * the new VT */
        ck_session_pause_all_devices (session, TRUE);

        /* release control */
#if defined(VT_RELDISP)
        ioctl (session->priv->tty_fd, VT_RELDISP, 1);
#endif

        return TRUE;
}

static gboolean
vt_acquire_handler (gpointer data)
{
        CkSession *session = CK_SESSION (data);

        TRACE ();

        /* g_unix_signal_add_full returns this callback in the GMainContext
         * so we don't have the limitations of POSIX signal handlers */

        /* ack that we are getting control, but let the normal
         * process handle granting access */
#if defined(VT_RELDISP)
        ioctl (session->priv->tty_fd, VT_RELDISP, VT_ACKACQ);
#endif

        return TRUE;
}

static void
ck_session_setup_vt_signal (CkSession *session,
                            guint      vtnr)
{
#if defined(KDGKBMODE) && defined(KDSETMODE) && defined(KD_GRAPHICS)
        struct vt_mode mode = { 0 };
        int    graphical_mode = KD_TEXT;

        session->priv->tty_fd = ck_open_a_console (ck_get_console_device_for_num (vtnr));

        if (session->priv->tty_fd < 0) {
                return;
        }

        ioctl(session->priv->tty_fd, KDGKBMODE, &session->priv->old_kbd_mode);

        /* So during setup here, we need to ensure we're in graphical mode,
         * otherwise it will try to draw stuff in the background */
#if defined(KDGETMODE)
        if (ioctl (session->priv->tty_fd, KDGETMODE, &graphical_mode) != 0) {
                g_warning ("failed to get current VT mode");
                return;
        }
#endif

        if (graphical_mode == KD_TEXT) {
                if (ioctl (session->priv->tty_fd, KDSETMODE, KD_GRAPHICS) != 0) {
                        g_warning ("failed to change VT to graphical mode");
                        return;
                }
        } else {
                g_debug ("already running in graphical mode? not messing with the tty or VT");
                return;
        }

/* We define KDSKBMUTE above so we can't do the usual check */
#if defined(__linux__)
        if (ioctl (session->priv->tty_fd, KDSKBMUTE, 1) != 0) {
                g_warning ("failed to mute FD with KDSKBMUTE");
        }
#endif /* defined(__linux__) */

#if defined(KDSKBMODE)
        if (ioctl (session->priv->tty_fd, KDSKBMODE, KBD_OFF_MODE) != 0) {
                g_warning ("failed to turn off keyboard");
        }
#endif

        mode.mode = VT_PROCESS;
        mode.relsig = SIGUSR1;
        mode.acqsig = SIGUSR2;
        mode.frsig = SIGIO; /* not used, but has to be set anyway */

        if (ioctl (session->priv->tty_fd, VT_SETMODE, &mode) < 0) {
                g_warning ("failed to take control of vt handling");
                return;
        }

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
        /* Put the tty into raw mode */
        struct termios tios;
        if (tcgetattr (session->priv->tty_fd, &tios))
          g_warning ("Failed to get terminal attributes");
        cfmakeraw (&tios);
        if (tcsetattr (session->priv->tty_fd, TCSAFLUSH, &tios))
          g_warning ("Failed to set terminal attributes");
#endif

        session->priv->sig_watch_s1 = g_unix_signal_add_full (G_PRIORITY_HIGH,
                                                              SIGUSR1,
                                                              (GSourceFunc)vt_leave_handler,
                                                              session,
                                                              NULL);
        session->priv->sig_watch_s2 = g_unix_signal_add_full (G_PRIORITY_HIGH,
                                                              SIGUSR2,
                                                              (GSourceFunc)vt_acquire_handler,
                                                              session,
                                                              NULL);
#endif /* defined(KDGKBMODE) && defined(KDSETMODE) && defined(KD_GRAPHICS) */
}

static void
session_controller_vanished (GDBusConnection *connection,
                             const gchar *name,
                             gpointer user_data)
{
        CkSession *session = CK_SESSION (user_data);

        TRACE ();

        session->priv->session_controller_watchid = 0;

        ck_session_set_session_controller (session, NULL);
}

static void
ck_session_controller_cleanup (CkSession *session)
{
        TRACE ();

        if (session->priv->session_controller_watchid != 0) {
                g_bus_unwatch_name (session->priv->session_controller_watchid);
                session->priv->session_controller_watchid = 0;
        }

        if (session->priv->pause_devices_timer != 0) {
                g_source_remove (session->priv->pause_devices_timer);
                session->priv->pause_devices_timer = 0;
        }

        if (session->priv->session_controller) {
                g_free (session->priv->session_controller);
                session->priv->session_controller = NULL;
        }

        ck_session_remove_all_devices (session);

#if defined(VT_SETMODE)
        /* Remove the old signal call backs, restore VT switching to auto
         * and text mode (put it back the way we found it) */
        if (session->priv->sig_watch_s1 != 0) {
                struct vt_mode mode = { 0 };
                mode.mode = VT_AUTO;

/* We define KDSKBMUTE above so we can't do the usual check */
#if defined(__linux__)
                if (ioctl (session->priv->tty_fd, KDSKBMUTE, 0) != 0) {
                        g_warning ("failed to unmute FD with KDSKBMUTE");
                }
#endif /* defined(__linux__) */

#if defined(KDSKBMODE)
                if (ioctl(session->priv->tty_fd, KDSKBMODE, session->priv->old_kbd_mode) != 0) {
                        g_warning ("failed to restore old keyboard mode");
                }
#endif /* defined(KDSKBMODE) */

#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
                /* Restore sane mode */
                struct termios tios;
                if (tcgetattr (session->priv->tty_fd, &tios)) {
                  g_warning ("Failed to get terminal attributes");
                } else {
                  cfmakesane (&tios);
                  if (tcsetattr (session->priv->tty_fd, TCSAFLUSH, &tios)) {
                    g_warning ("Failed to set terminal attributes");
                  }
                }
#endif

                if (ioctl (session->priv->tty_fd, VT_SETMODE, &mode) < 0) {
                        g_warning ("failed to return control of vt handling");
                }

                if (ioctl (session->priv->tty_fd, KDSETMODE, KD_TEXT) < 0) {
                        g_warning ("failed to return to text mode for the VT");
                }

                g_source_remove (session->priv->sig_watch_s1);
                session->priv->sig_watch_s1 = 0;
                g_source_remove (session->priv->sig_watch_s2);
                session->priv->sig_watch_s2 = 0;
        }

        /* Close the old tty fd */
        if (session->priv->tty_fd != -1) {
                g_close (session->priv->tty_fd, NULL);
                session->priv->tty_fd = -1;
        }
#endif /* defined(VT_SETMODE) */
}

void
ck_session_set_session_controller (CkSession   *session,
                                   const gchar *bus_name)
{
        guint vtnr;

        TRACE ();

        /* clean up after the old controller */
        ck_session_controller_cleanup (session);

        /* There's no reason to go further if we're removing the session
         * controller */
        if (bus_name == NULL)
        {
                return;
        }

        session->priv->session_controller = g_strdup (bus_name);

        /* if the session controller crashes or exits, we need to drop access
         * to all the devices it requested and let someone else become the
         * session controller.
         */
        session->priv->session_controller_watchid = g_bus_watch_name_on_connection (session->priv->connection,
                                                                                    bus_name,
                                                                                    G_BUS_NAME_WATCHER_FLAGS_NONE,
                                                                                    NULL,
                                                                                    session_controller_vanished,
                                                                                    session,
                                                                                    NULL);

        vtnr = console_kit_session_get_vtnr (CONSOLE_KIT_SESSION (session));
        if (vtnr > 0) {
                ck_session_setup_vt_signal (session, vtnr);
        }
}

static gboolean
dbus_can_control_session (ConsoleKitSession *object,
                          GDBusMethodInvocation *invocation)
{
        console_kit_session_complete_can_control_session (object, invocation, ck_device_is_server_managed ());
        return TRUE;
}

static gboolean
dbus_take_control (ConsoleKitSession *object,
                   GDBusMethodInvocation *invocation,
                   gboolean arg_force)
{
        CkSession   *session = CK_SESSION (object);
        uid_t        uid = 0;
        pid_t        pid = 0;
        const gchar *sender = g_dbus_method_invocation_get_sender (invocation);

        TRACE ();

        if (!ck_device_is_server_managed ()) {
                throw_error (invocation, CK_SESSION_ERROR_NOT_SUPPORTED, _("Server managed devices not supported"));
                return TRUE;
        }

        if (!get_caller_info (session, sender, &uid, &pid))
        {
                throw_error (invocation, CK_SESSION_ERROR_GENERAL, _("Failed to get uid of dbus caller"));
                return TRUE;
        }

        if (g_strcmp0 (session->priv->session_controller, sender) == 0)
        {
                /* The current session controller is trying to take control
                 * again? Odd, but we'll just do nothing here.
                 */
        }
        else if (session->priv->session_controller == NULL || (arg_force == TRUE && uid == 0))
        {
                ck_session_set_session_controller (CK_SESSION (session), sender);
        }
        else
        {
                if (arg_force == TRUE) {
                        throw_error (invocation, CK_SESSION_ERROR_INSUFFICIENT_PERMISSION, _("Failed to replace the current session controller"));
                } else {
                        throw_error (invocation, CK_SESSION_ERROR_FAILED, _("Session controller already present"));
                }
                return TRUE;
        }

        console_kit_session_complete_take_control (object, invocation);
        return TRUE;
}

static gboolean
dbus_release_control (ConsoleKitSession *object,
                      GDBusMethodInvocation *invocation)
{
        CkSession   *session = CK_SESSION (object);
        const gchar *sender = g_dbus_method_invocation_get_sender (invocation);

        TRACE ();

        /* only the session controller can release it */
        if (g_strcmp0 (session->priv->session_controller, sender) == 0) {
                ck_session_set_session_controller (CK_SESSION (object), NULL);
        } else {
                throw_error (invocation, CK_SESSION_ERROR_FAILED, _("Only the session controller may use this function"));
                return TRUE;
        }

        console_kit_session_complete_release_control (object, invocation);
        return TRUE;
}

static CkDevice*
ck_session_get_device (CkSession *session,
                       guint major,
                       guint minor)
{
        GList *iter;

        TRACE ();

        ck_session_print_list_size (session);

        for (iter = session->priv->devices; iter != NULL; iter = g_list_next (iter)) {
                if (ck_device_compare (iter->data, major, minor)) {
                        g_debug ("found device");
                        CkDevice *device = CK_DEVICE (iter->data);
                        if (ck_device_get_category(device) == DEVICE_EVDEV) {
                                struct stat st;
                                if (fstat(ck_device_get_fd(device), &st) == -1 && errno == EBADF) {
                                        g_debug ("but it was a dead input device, removing");
                                        session->priv->devices = g_list_remove (session->priv->devices, device);
                                        return NULL;
                                }
                        }
                        return iter->data;
                }
        }

        g_debug ("failed to find deivce");
        return NULL;
}

static CkDevice*
ck_session_create_device (CkSession *session,
                          guint      major,
                          guint      minor)
{
        CkDevice *device = ck_session_get_device (session, major, minor);

        /* If the device already exists for this session don't
         * create it again */
        if (device == NULL) {
                g_debug ("creating new device");

                device = ck_device_new (major, minor,
                                        console_kit_session_get_active (CONSOLE_KIT_SESSION (session)));

                if (device != NULL && CK_IS_DEVICE (device)) {
                        g_debug ("adding device to the list");
                        session->priv->devices = g_list_prepend (session->priv->devices, device);
                        ck_session_print_list_size (session);
                }
        }

        return device;
}

static void
ck_session_remove_device (CkSession *session,
                          CkDevice  *device)
{
        TRACE ();
        ck_session_print_list_size (session);
        session->priv->devices = g_list_remove (session->priv->devices, device);
        ck_session_print_list_size (session);
}

static void
ck_session_remove_all_devices (CkSession *session)
{
        TRACE ();
        ck_session_print_list_size (session);
        g_list_free_full (session->priv->devices, g_object_unref);
        session->priv->devices = NULL;
        g_debug ("list size is now 0");
}

static gboolean
dbus_take_device (ConsoleKitSession *object,
                  GDBusMethodInvocation *invocation,
                  GUnixFDList *fd_list,
                  guint arg_major,
                  guint arg_minor)
{
        CkSession   *session = CK_SESSION (object);
        CkDevice    *device;
        const gchar *sender = g_dbus_method_invocation_get_sender (invocation);
        gint         fd = -1;
        GUnixFDList *out_fd_list = NULL;


        TRACE ();

        /* only the session controller can call us */
        if (g_strcmp0 (session->priv->session_controller, sender) != 0) {
                throw_error (invocation, CK_SESSION_ERROR_FAILED, _("Only the session controller may call this function"));
                return TRUE;
        }

        /* you can't request the device again, that's confusing */
        if (ck_session_get_device (session, arg_major, arg_minor)) {
                throw_error (invocation, CK_SESSION_ERROR_GENERAL, _("Device has already been requested"));
                return TRUE;
        }

        device = ck_session_create_device (session, arg_major, arg_minor);

        if (device == NULL) {
                throw_error (invocation, CK_SESSION_ERROR_NOT_SUPPORTED, _("Failed to create device"));
                return TRUE;
        }

        fd = ck_device_get_fd (device);

        if (fd == -1) {
                throw_error (invocation, CK_SESSION_ERROR_NOT_SUPPORTED, _("Failed to get file descriptor for device"));
                return TRUE;
        }

        out_fd_list = g_unix_fd_list_new_from_array (&fd, 1);

        console_kit_session_complete_take_device (object, invocation,
                                                  out_fd_list, g_variant_new_handle (0),
                                                  console_kit_session_get_active (object));
        return TRUE;
}

static gboolean
dbus_release_device (ConsoleKitSession *object,
                     GDBusMethodInvocation *invocation,
                     guint arg_major,
                     guint arg_minor)
{
        CkSession   *session = CK_SESSION (object);
        CkDevice    *device;
        const gchar *sender = g_dbus_method_invocation_get_sender (invocation);


        TRACE ();

        /* only the session controller can call us */
        if (g_strcmp0 (session->priv->session_controller, sender) != 0) {
                throw_error (invocation, CK_SESSION_ERROR_FAILED, _("Only the session controller may call this function"));
                return TRUE;
        }

        device = ck_session_get_device (session, arg_major, arg_minor);

        if (device == NULL) {
                throw_error (invocation, CK_SESSION_ERROR_FAILED, _("Device doesn't exist"));
                return TRUE;
        }

        ck_session_remove_device (session, device);
        g_object_unref (device);

        console_kit_session_complete_release_device (object, invocation);
        return TRUE;
}

static gboolean
dbus_pause_device_complete (ConsoleKitSession *object,
                            GDBusMethodInvocation *invocation,
                            guint arg_major,
                            guint arg_minor)
{
        CkSession   *session = CK_SESSION (object);
        CkDevice    *device;
        const gchar *sender = g_dbus_method_invocation_get_sender (invocation);

        TRACE ();

        /* only the session controller can call us */
        if (g_strcmp0 (session->priv->session_controller, sender) != 0) {
                throw_error (invocation, CK_SESSION_ERROR_FAILED, _("Only the session controller may call this function"));
                return TRUE;
        }

        device = ck_session_get_device (session, arg_major, arg_minor);

        if (device == NULL) {
                throw_error (invocation, CK_SESSION_ERROR_FAILED, _("Device doesn't exist"));
                return TRUE;
        }

        ck_device_set_active (device, FALSE);

        ck_session_check_paused_devices (session);

        console_kit_session_complete_pause_device_complete (object, invocation);
        return TRUE;
}

static void
ck_session_set_property (GObject            *object,
                         guint               prop_id,
                         const GValue       *value,
                         GParamSpec         *pspec)
{
        CkSession *self;

        self = CK_SESSION (object);

        switch (prop_id) {
        case PROP_ID:
                ck_session_set_id (self, g_value_get_string (value), NULL);
                break;
        case PROP_COOKIE:
                ck_session_set_cookie (self, g_value_get_string (value), NULL);
                break;
        case PROP_LOGIN_SESSION_ID:
                ck_session_set_login_session_id (self, g_value_get_string (value), NULL);
                break;
        case PROP_SESSION_CONTROLLER:
                ck_session_set_session_controller (self, g_value_get_string (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
ck_session_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
        CkSession *self;

        self = CK_SESSION (object);

        switch (prop_id) {
        case PROP_ID:
                g_value_set_string (value, self->priv->id);
                break;
        case PROP_COOKIE:
                g_value_set_string (value, self->priv->cookie);
                break;
        case PROP_LOGIN_SESSION_ID:
                g_value_set_string (value, self->priv->login_session_id);
                break;
        case PROP_SESSION_CONTROLLER:
                g_value_set_string (value, self->priv->session_controller);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

#define IS_STR_SET(x) (x != NULL && x[0] != '\0')
static gboolean
session_is_text (CkSession *session)
{
        ConsoleKitSession *cksession;
        gboolean ret;

        ret = FALSE;
        cksession = CONSOLE_KIT_SESSION (session);

        if (! IS_STR_SET (console_kit_session_get_x11_display_device (cksession))
            && ! IS_STR_SET (console_kit_session_get_x11_display (cksession))
            && IS_STR_SET (console_kit_session_get_display_device (cksession))) {
                ret = TRUE;
        }

        g_debug ("Identified session '%s' as %s",
                  session->priv->id,
                  ret ? "text" : "graphical");

        return ret;
}

static void
tty_idle_changed_cb (CkTtyIdleMonitor *monitor,
                     gboolean          idle_hint,
                     CkSession        *session)
{
        session_set_idle_hint_internal (session, idle_hint);
}

static void
session_add_activity_watch (CkSession *session)
{
        if (session->priv->idle_monitor == NULL) {
                session->priv->idle_monitor = ck_tty_idle_monitor_new (console_kit_session_get_display_device (CONSOLE_KIT_SESSION (session)));
                g_signal_connect (session->priv->idle_monitor,
                                  "idle-hint-changed",
                                  G_CALLBACK (tty_idle_changed_cb),
                                  session);

        }
        ck_tty_idle_monitor_start (session->priv->idle_monitor);
}

static void
session_remove_activity_watch (CkSession *session)
{
        TRACE ();

        if (session->priv->idle_monitor == NULL) {
                return;
        }

        ck_tty_idle_monitor_stop (session->priv->idle_monitor);
        g_object_unref (session->priv->idle_monitor);
        session->priv->idle_monitor = NULL;
}

static GObject *
ck_session_constructor (GType                  type,
                        guint                  n_construct_properties,
                        GObjectConstructParam *construct_properties)
{
        CkSession      *session;

        session = CK_SESSION (G_OBJECT_CLASS (ck_session_parent_class)->constructor (type,
                                                                                     n_construct_properties,
                                                                                     construct_properties));
        if (session_is_text (session)) {
                session_add_activity_watch (session);
        }

        session->priv->path = g_strdup_printf ("%s/%s", CK_DBUS_PATH, session->priv->id);

        return G_OBJECT (session);
}

static void
ck_session_class_init (CkSessionClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->constructor = ck_session_constructor;
        object_class->get_property = ck_session_get_property;
        object_class->set_property = ck_session_set_property;
        object_class->finalize = ck_session_finalize;

        signals [ACTIVATE] =
                g_signal_new ("activate",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CkSessionClass, activate),
                              NULL,
                              NULL,
                              ck_marshal_POINTER__POINTER,
                              G_TYPE_POINTER,
                              1, G_TYPE_POINTER);

        /* Install private properties we're not exporting over D-BUS */
        g_object_class_install_property (object_class,
                                         PROP_ID,
                                         g_param_spec_string ("id",
                                                              "id",
                                                              "id",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_COOKIE,
                                         g_param_spec_string ("cookie",
                                                              "cookie",
                                                              "cookie",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
        g_object_class_install_property (object_class,
                                         PROP_LOGIN_SESSION_ID,
                                         g_param_spec_string ("login-session-id",
                                                              "login-session-id",
                                                              "login session id",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

        g_object_class_install_property (object_class,
                                         PROP_SESSION_CONTROLLER,
                                         g_param_spec_string ("session-controller",
                                                              "session-controller",
                                                              "session-controller",
                                                              NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

        g_type_class_add_private (klass, sizeof (CkSessionPrivate));
}

static void
ck_session_init (CkSession *session)
{
        session->priv = CK_SESSION_GET_PRIVATE (session);

        /* FIXME: should we have a property for this? */
        g_get_current_time (&session->priv->creation_time);

        session->priv->tty_fd = -1;
}

static void
ck_session_iface_init (ConsoleKitSessionIface *iface)
{
        iface->handle_activate               = dbus_activate;
        iface->handle_set_idle_hint          = dbus_set_idle_hint;
        iface->handle_set_locked_hint        = dbus_set_locked_hint;
        iface->handle_get_unix_user          = dbus_get_unix_user;
        iface->handle_get_seat_id            = dbus_get_seat_id;
        iface->handle_get_login_session_id   = dbus_get_login_session_id;
        iface->handle_get_vtnr               = dbus_get_vtnr;
        iface->handle_get_session_service    = dbus_get_session_service;
        iface->handle_get_session_type       = dbus_get_session_type;
        iface->handle_get_session_class      = dbus_get_session_class;
        iface->handle_get_session_state      = dbus_get_session_state;
        iface->handle_get_x11_display_device = dbus_get_x11_display_device;
        iface->handle_get_display_device     = dbus_get_display_device;
        iface->handle_get_x11_display        = dbus_get_x11_display;
        iface->handle_is_active              = dbus_is_active;
        iface->handle_get_creation_time      = dbus_get_creation_time;
        iface->handle_get_remote_host_name   = dbus_get_remote_host_name;
        iface->handle_get_idle_since_hint    = dbus_get_idle_since_hint;
        iface->handle_is_local               = dbus_is_local;
        iface->handle_get_id                 = dbus_get_id;
        iface->handle_get_user               = dbus_get_user;
        iface->handle_lock                   = dbus_lock;
        iface->handle_unlock                 = dbus_unlock;
        iface->handle_get_idle_hint          = dbus_get_idle_hint;
        iface->handle_get_xdgruntime_dir     = dbus_get_runtime_dir;
        iface->handle_can_control_session    = dbus_can_control_session;
        iface->handle_take_control           = dbus_take_control;
        iface->handle_release_control        = dbus_release_control;
        iface->handle_take_device            = dbus_take_device;
        iface->handle_release_device         = dbus_release_device;
        iface->handle_pause_device_complete  = dbus_pause_device_complete;
}

static void
ck_session_finalize (GObject *object)
{
        CkSession *session;

        g_debug ("ck_session_finalize");

        g_return_if_fail (object != NULL);
        g_return_if_fail (CK_IS_SESSION (object));

        session = CK_SESSION (object);

        g_return_if_fail (session->priv != NULL);

        session_remove_activity_watch (session);

        ck_session_set_session_controller (session, NULL);

        ck_session_remove_all_devices (session);

        g_free (session->priv->id);
        g_free (session->priv->path);
        g_free (session->priv->cookie);
        g_free (session->priv->login_session_id);
        g_free (session->priv->runtime_dir);
        g_free (session->priv->seat_id);
        g_free (session->priv->seat_path);
        g_free (session->priv->session_controller);

        if (session->priv->bus_proxy) {
            g_object_unref(session->priv->bus_proxy);
        }

        if (session->priv->session_controller_watchid != 0) {
                g_bus_unwatch_name (session->priv->session_controller_watchid);
        }

        if (session->priv->pause_devices_timer != 0) {
                g_source_remove (session->priv->pause_devices_timer);
                session->priv->pause_devices_timer = 0;
        }

        G_OBJECT_CLASS (ck_session_parent_class)->finalize (object);
}

CkSession *
ck_session_new (const char      *ssid,
                const char      *cookie,
                GDBusConnection *connection)
{
        GObject *object;
        gboolean res;

        object = g_object_new (CK_TYPE_SESSION,
                               "id", ssid,
                               "cookie", cookie,
                               NULL);
        res = register_session (CK_SESSION (object), connection);
        if (! res) {
                g_object_unref (object);
                return NULL;
        }

        return CK_SESSION (object);
}

CkSession *
ck_session_new_with_parameters (const char      *ssid,
                                const char      *cookie,
                                const GVariant  *parameters,
                                GDBusConnection *connection)
{
        GObject      *object;
        gboolean      session_registered;
        GParameter   *params;
        guint         n_allocated_params;
        guint         n_params;
        GObjectClass *class;
        GType         object_type;
        GVariantIter *iter;
        gchar        *prop_name;
        GVariant     *value;

        object_type = CK_TYPE_SESSION;
        class = g_type_class_ref (object_type);

        g_variant_get ((GVariant *)parameters, "a{sv}", &iter);

        n_allocated_params = 2;
        if (parameters != NULL) {
                n_allocated_params += g_variant_iter_n_children (iter);
        }

        params = g_new0 (GParameter, n_allocated_params);

        n_params = 0;
        params[n_params].name = g_strdup ("id");
        params[n_params].value.g_type = 0;
        g_value_init (&params[n_params].value, G_TYPE_STRING);
        g_value_set_string (&params[n_params].value, ssid);
        n_params++;

        params[n_params].name = g_strdup ("cookie");
        params[n_params].value.g_type = 0;
        g_value_init (&params[n_params].value, G_TYPE_STRING);
        g_value_set_string (&params[n_params].value, cookie);
        n_params++;

        if (parameters != NULL) {
                while (g_variant_iter_next (iter, "{sv}", &prop_name, &value)) {
                        gboolean    res;
                        GValue      val_struct = { 0, };
                        GParamSpec *pspec;


                        if (prop_name == NULL) {
                                g_debug ("Skipping NULL parameter");
                                goto cleanup;
                        }

                        g_debug ("prop_name = '%s'", prop_name);

                        if (strcmp (prop_name, "id") == 0
                            || strcmp (prop_name, "cookie") == 0) {
                                g_debug ("Skipping restricted parameter: %s", prop_name);
                                goto cleanup;
                        }

                        pspec = g_object_class_find_property (class, prop_name);
                        if (! pspec) {
                                g_debug ("Skipping unknown parameter: %s", prop_name);
                                goto cleanup;
                        }

                        if (!(pspec->flags & G_PARAM_WRITABLE)) {
                                g_debug ("property '%s' is not writable", pspec->name);
                                goto cleanup;
                        }

                        params[n_params].name = g_strdup (prop_name);
                        params[n_params].value.g_type = 0;
                        g_value_init (&params[n_params].value, G_PARAM_SPEC_VALUE_TYPE (pspec));
                        g_dbus_gvariant_to_gvalue (value, &val_struct);
                        res = g_value_transform (&val_struct, &params[n_params].value);
                        if (! res) {
                                g_debug ("unable to transform property value for '%s'", pspec->name);
                                goto cleanup;
                        }

                        n_params++;
cleanup:
                        g_free(prop_name);
                        if (value) {
                            g_variant_unref(value);
                        }
                }
                g_variant_iter_free (iter);
        }

        object = g_object_newv (object_type, n_params, params);

        while (n_params--) {
                g_free ((char *)params[n_params].name);
                g_value_unset (&params[n_params].value);
        }
        g_free (params);
        g_type_class_unref (class);

        session_registered = register_session (CK_SESSION (object), connection);
        if (! session_registered) {
                g_object_unref (object);
                return NULL;
        }

        return CK_SESSION (object);
}

void
ck_session_run_programs (CkSession  *session,
                         const char *action)
{
        ConsoleKitSession *cksession;
        guint n;
        char *extra_env[11]; /* be sure to adjust this as needed */

        TRACE ();

        n = 0;
        cksession = CONSOLE_KIT_SESSION (session);

        extra_env[n++] = g_strdup_printf ("CK_SESSION_ID=%s", session->priv->id);
        if (console_kit_session_get_session_service (cksession) != NULL) {
                extra_env[n++] = g_strdup_printf ("CK_SESSION_SERVICE=%s", console_kit_session_get_session_service (cksession));
        }
        if (console_kit_session_get_session_type (cksession) != NULL) {
                extra_env[n++] = g_strdup_printf ("CK_SESSION_TYPE=%s", console_kit_session_get_session_type (cksession));
        }
        extra_env[n++] = g_strdup_printf ("CK_SESSION_SEAT_ID=%s", session->priv->seat_id);
        extra_env[n++] = g_strdup_printf ("CK_SESSION_USER_UID=%d", console_kit_session_get_unix_user (cksession));
        if (console_kit_session_get_display_device (cksession) != NULL && strlen (console_kit_session_get_display_device (cksession)) > 0) {
                extra_env[n++] = g_strdup_printf ("CK_SESSION_DISPLAY_DEVICE=%s", console_kit_session_get_display_device (cksession));
        }
        if (console_kit_session_get_x11_display_device (cksession) != NULL && strlen (console_kit_session_get_x11_display_device (cksession) ) > 0) {
                extra_env[n++] = g_strdup_printf ("CK_SESSION_X11_DISPLAY_DEVICE=%s", console_kit_session_get_x11_display_device (cksession) );
        }
        extra_env[n++] = g_strdup_printf ("CK_SESSION_X11_DISPLAY=%s",
                console_kit_session_get_x11_display (cksession)  ? console_kit_session_get_x11_display (cksession)  : "");
        if (console_kit_session_get_remote_host_name (cksession)  != NULL && strlen (console_kit_session_get_remote_host_name (cksession)) > 0) {
                extra_env[n++] = g_strdup_printf ("CK_SESSION_REMOTE_HOST_NAME=%s", console_kit_session_get_remote_host_name (cksession));
        }
        extra_env[n++] = g_strdup_printf ("CK_SESSION_IS_ACTIVE=%s", console_kit_session_get_active (cksession) ? "true" : "false");
        extra_env[n++] = g_strdup_printf ("CK_SESSION_IS_LOCAL=%s", console_kit_session_get_is_local (cksession) ? "true" : "false");
        extra_env[n++] = NULL;

        g_assert(n <= G_N_ELEMENTS(extra_env));

        ck_run_programs (SYSCONFDIR "/ConsoleKit/run-session.d", action, extra_env);
        ck_run_programs (LIBDIR "/ConsoleKit/run-session.d", action, extra_env);

        for (n = 0; extra_env[n] != NULL; n++) {
                g_free (extra_env[n]);
        }
}

void
ck_session_dump (CkSession *session,
                 GKeyFile  *key_file)
{
        char *s;
        char *group_name;
        ConsoleKitSession *cksession;

        TRACE ();

        cksession = CONSOLE_KIT_SESSION (session);

        group_name = g_strdup_printf ("Session %s", session->priv->id);
        g_key_file_set_integer (key_file, group_name, "uid", console_kit_session_get_unix_user (cksession));
        g_key_file_set_string (key_file,
                               group_name,
                               "seat",
                               NONULL_STRING (session->priv->seat_id));
        if (console_kit_session_get_session_service (cksession) != NULL) {
                g_key_file_set_string (key_file,
                                       group_name,
                                       "service",
                                       NONULL_STRING (console_kit_session_get_session_service (cksession)));
        }
        if (console_kit_session_get_session_type (cksession) != NULL) {
                g_key_file_set_string (key_file,
                                       group_name,
                                       "type",
                                       NONULL_STRING (console_kit_session_get_session_type (cksession)));
        }
        if (session->priv->login_session_id != NULL && strlen (session->priv->login_session_id) > 0) {
                g_key_file_set_string (key_file,
                                       group_name,
                                       "login_session_id",
                                       NONULL_STRING (session->priv->login_session_id));
        }
        if (console_kit_session_get_display_device (cksession) != NULL && strlen (console_kit_session_get_display_device (cksession)) > 0) {
                g_key_file_set_string (key_file,
                                       group_name,
                                       "display_device",
                                       NONULL_STRING (console_kit_session_get_display_device (cksession)));
        }
        if (console_kit_session_get_x11_display_device (cksession) != NULL && strlen (console_kit_session_get_x11_display_device (cksession)) > 0) {
                g_key_file_set_string (key_file,
                                       group_name,
                                       "x11_display_device",
                                       NONULL_STRING (console_kit_session_get_x11_display_device (cksession)));
        }
        if (console_kit_session_get_x11_display (cksession) != NULL && strlen (console_kit_session_get_x11_display (cksession)) > 0) {
                g_key_file_set_string (key_file,
                                       group_name,
                                       "x11_display",
                                       NONULL_STRING (console_kit_session_get_x11_display (cksession)));
        }
        if (console_kit_session_get_remote_host_name (cksession) != NULL && strlen (console_kit_session_get_remote_host_name (cksession)) > 0) {
                g_key_file_set_string (key_file,
                                       group_name,
                                       "remote_host_name",
                                       NONULL_STRING (console_kit_session_get_remote_host_name (cksession)));
        }
        g_key_file_set_string (key_file,
                               group_name,
                               "remote_host_name",
                               NONULL_STRING (console_kit_session_get_remote_host_name (cksession)));

        g_key_file_set_boolean (key_file, group_name, "is_active", console_kit_session_get_active (cksession));
        g_key_file_set_boolean (key_file, group_name, "is_local", console_kit_session_get_is_local (cksession));
        g_key_file_set_string  (key_file, group_name, "XDG_RUNTIME_DIR", NONULL_STRING (session->priv->runtime_dir));

        s = g_time_val_to_iso8601 (&(session->priv->creation_time));
        g_key_file_set_string (key_file,
                               group_name,
                               "creation_time",
                               NONULL_STRING (s));
        g_free (s);
}
