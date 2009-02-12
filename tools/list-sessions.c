/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 William Jon McCann <mccann@jhu.edu>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <pwd.h>
#include <string.h>

#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#define CK_NAME      "org.freedesktop.ConsoleKit"
#define CK_PATH      "/org/freedesktop/ConsoleKit"
#define CK_INTERFACE "org.freedesktop.ConsoleKit"

#define CK_MANAGER_PATH      "/org/freedesktop/ConsoleKit/Manager"
#define CK_MANAGER_INTERFACE "org.freedesktop.ConsoleKit.Manager"
#define CK_SEAT_INTERFACE    "org.freedesktop.ConsoleKit.Seat"
#define CK_SESSION_INTERFACE "org.freedesktop.ConsoleKit.Session"

static gboolean
get_uint (DBusGProxy *proxy,
          const char *method,
          guint      *val)
{
        GError  *error;
        gboolean res;

        error = NULL;
        res = dbus_g_proxy_call (proxy,
                                 method,
                                 &error,
                                 G_TYPE_INVALID,
                                 G_TYPE_UINT, val,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("%s failed: %s", method, error->message);
                g_error_free (error);
        }

        return res;
}

static gboolean
get_path (DBusGProxy *proxy,
          const char *method,
          char      **str)
{
        GError  *error;
        gboolean res;

        error = NULL;
        res = dbus_g_proxy_call (proxy,
                                 method,
                                 &error,
                                 G_TYPE_INVALID,
                                 DBUS_TYPE_G_OBJECT_PATH, str,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("%s failed: %s", method, error->message);
                g_error_free (error);
        }

        return res;
}

static gboolean
get_string (DBusGProxy *proxy,
            const char *method,
            char      **str)
{
        GError  *error;
        gboolean res;

        error = NULL;
        res = dbus_g_proxy_call (proxy,
                                 method,
                                 &error,
                                 G_TYPE_INVALID,
                                 G_TYPE_STRING, str,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("%s failed: %s", method, error->message);
                g_error_free (error);
        }

        return res;
}

static gboolean
get_boolean (DBusGProxy *proxy,
             const char *method,
             gboolean   *value)
{
        GError  *error;
        gboolean res;

        error = NULL;
        res = dbus_g_proxy_call (proxy,
                                 method,
                                 &error,
                                 G_TYPE_INVALID,
                                 G_TYPE_BOOLEAN, value,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("%s failed: %s", method, error->message);
                g_error_free (error);
        }

        return res;
}

static char *
get_real_name (uid_t uid)
{
        struct passwd *pwent;
        char          *realname;

        realname = NULL;

        pwent = getpwuid (uid);

        if (pwent != NULL
            && pwent->pw_gecos
            && *pwent->pw_gecos != '\0') {
                char **gecos_fields;
                char **name_parts;

                /* split the gecos field and substitute '&' */
                gecos_fields = g_strsplit (pwent->pw_gecos, ",", 0);
                name_parts = g_strsplit (gecos_fields[0], "&", 0);
                pwent->pw_name[0] = g_ascii_toupper (pwent->pw_name[0]);
                realname = g_strjoinv (pwent->pw_name, name_parts);
                g_strfreev (gecos_fields);
                g_strfreev (name_parts);
        }

        return realname;
}

static void
list_session (DBusGConnection *connection,
              const char      *ssid)
{
        DBusGProxy *proxy;
        guint       uid;
        char       *realname;
        char       *sid;
        char       *lsid;
        char       *session_type;
        char       *x11_display;
        char       *x11_display_device;
        char       *display_device;
        char       *remote_host_name;
        char       *creation_time;
        char       *idle_since_hint;
        gboolean    is_active;
        gboolean    is_local;
        char       *short_sid;
        const char *short_ssid;

        proxy = dbus_g_proxy_new_for_name (connection,
                                           CK_NAME,
                                           ssid,
                                           CK_SESSION_INTERFACE);
        if (proxy == NULL) {
                return;
        }

        sid = NULL;
        lsid = NULL;
        session_type = NULL;
        x11_display = NULL;
        x11_display_device = NULL;
        display_device = NULL;
        remote_host_name = NULL;
        creation_time = NULL;
        idle_since_hint = NULL;

        get_uint (proxy, "GetUnixUser", &uid);
        get_path (proxy, "GetSeatId", &sid);
        get_string (proxy, "GetLoginSessionId", &lsid);
        get_string (proxy, "GetSessionType", &session_type);
        get_string (proxy, "GetX11Display", &x11_display);
        get_string (proxy, "GetX11DisplayDevice", &x11_display_device);
        get_string (proxy, "GetDisplayDevice", &display_device);
        get_string (proxy, "GetRemoteHostName", &remote_host_name);
        get_boolean (proxy, "IsActive", &is_active);
        get_boolean (proxy, "IsLocal", &is_local);
        get_string (proxy, "GetCreationTime", &creation_time);
        get_string (proxy, "GetIdleSinceHint", &idle_since_hint);

        realname = get_real_name (uid);

        short_sid = sid;
        short_ssid = ssid;

        if (g_str_has_prefix (sid, CK_PATH "/")) {
                short_sid = sid + strlen (CK_PATH) + 1;
        }
        if (g_str_has_prefix (ssid, CK_PATH "/")) {
                short_ssid = ssid + strlen (CK_PATH) + 1;
        }

        printf ("%s:\n\tunix-user = '%d'\n\trealname = '%s'\n\tseat = '%s'\n\tsession-type = '%s'\n\tactive = %s\n\tx11-display = '%s'\n\tx11-display-device = '%s'\n\tdisplay-device = '%s'\n\tremote-host-name = '%s'\n\tis-local = %s\n\ton-since = '%s'\n\tlogin-session-id = '%s'",
                short_ssid,
                uid,
                realname,
                short_sid,
                session_type,
                is_active ? "TRUE" : "FALSE",
                x11_display,
                x11_display_device,
                display_device,
                remote_host_name,
                is_local ? "TRUE" : "FALSE",
                creation_time,
                lsid);
        if (idle_since_hint != NULL && idle_since_hint[0] != '\0') {
                printf ("\n\tidle-since-hint = '%s'", idle_since_hint);
        }
        printf ("\n");

        g_free (idle_since_hint);
        g_free (creation_time);
        g_free (remote_host_name);
        g_free (realname);
        g_free (sid);
        g_free (lsid);
        g_free (session_type);
        g_free (x11_display);
        g_free (x11_display_device);
        g_free (display_device);
        g_object_unref (proxy);
}

static void
list_sessions (DBusGConnection *connection,
               const char      *sid)
{
        DBusGProxy *proxy;
        GError     *error;
        gboolean    res;
        GPtrArray  *sessions;
        int         i;

        proxy = dbus_g_proxy_new_for_name (connection,
                                           CK_NAME,
                                           sid,
                                           CK_SEAT_INTERFACE);
        if (proxy == NULL) {
                return;
        }

        sessions = NULL;

        error = NULL;
        res = dbus_g_proxy_call (proxy,
                                 "GetSessions",
                                 &error,
                                 G_TYPE_INVALID,
                                 dbus_g_type_get_collection ("GPtrArray", DBUS_TYPE_G_OBJECT_PATH),
                                 &sessions,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("Failed to get list of sessions for %s: %s", sid, error->message);
                g_error_free (error);
                goto out;
        }

        for (i = 0; i < sessions->len; i++) {
                char *ssid;

                ssid = g_ptr_array_index (sessions, i);

                list_session (connection, ssid);

                g_free (ssid);
        }

        g_ptr_array_free (sessions, TRUE);
 out:
        g_object_unref (proxy);
}

static void
list_seats (DBusGConnection *connection)
{
        DBusGProxy *proxy;
        GError     *error;
        gboolean    res;
        GPtrArray  *seats;
        int         i;

        proxy = dbus_g_proxy_new_for_name (connection,
                                           CK_NAME,
                                           CK_MANAGER_PATH,
                                           CK_MANAGER_INTERFACE);
        if (proxy == NULL) {
                return;
        }

        seats = NULL;

        error = NULL;
        res = dbus_g_proxy_call (proxy,
                                 "GetSeats",
                                 &error,
                                 G_TYPE_INVALID,
                                 dbus_g_type_get_collection ("GPtrArray", DBUS_TYPE_G_OBJECT_PATH),
                                 &seats,
                                 G_TYPE_INVALID);
        if (! res) {
                g_warning ("Failed to get list of seats: %s", error->message);
                g_error_free (error);
                goto out;
        }

        for (i = 0; i < seats->len; i++) {
                char *sid;

                sid = g_ptr_array_index (seats, i);

                list_sessions (connection, sid);

                g_free (sid);
        }

        g_ptr_array_free (seats, TRUE);
 out:
        g_object_unref (proxy);
}

int
main (int    argc,
      char **argv)
{
        DBusGConnection *connection;

        GOptionContext *context;
        gboolean        retval;
        GError         *error = NULL;
        static gboolean do_version = FALSE;
        static GOptionEntry entries [] = {
                { "version", 'V', 0, G_OPTION_ARG_NONE, &do_version, N_("Version of this application"), NULL },
                { NULL }
        };

        g_type_init ();

        context = g_option_context_new (NULL);
        g_option_context_add_main_entries (context, entries, NULL);
        retval = g_option_context_parse (context, &argc, &argv, &error);

        g_option_context_free (context);

        if (! retval) {
                g_warning ("%s", error->message);
                g_error_free (error);
                exit (1);
        }

        if (do_version) {
                g_print ("%s %s\n", argv [0], VERSION);
                exit (1);
        }

        error = NULL;
        connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (connection == NULL) {
                g_message ("Failed to connect to the D-Bus daemon: %s", error->message);
                g_error_free (error);
                exit (1);
        }

        list_seats (connection);

        return 0;
}
