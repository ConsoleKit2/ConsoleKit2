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
#include <libintl.h>
#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gio/gio.h>

#define CK_NAME      "org.freedesktop.ConsoleKit"
#define CK_INTERFACE "org.freedesktop.ConsoleKit"

#define CK_MANAGER_PATH      "/org/freedesktop/ConsoleKit/Manager"
#define CK_MANAGER_INTERFACE "org.freedesktop.ConsoleKit.Manager"
#define CK_SEAT_INTERFACE    "org.freedesktop.ConsoleKit.Seat"
#define CK_SESSION_INTERFACE "org.freedesktop.ConsoleKit.Session"

static gboolean
get_value (GDBusProxy *proxy,
           const char *method,
           const char *variant_type,
           gpointer    val)
{
        GError   *error;
        GVariant *res;

        error = NULL;
        res = g_dbus_proxy_call_sync (proxy,
                                      method,
                                      g_variant_new ("()"),
                                      G_DBUS_CALL_FLAGS_NONE,
                                      6000,
                                      NULL,
                                      &error);
        if (res == NULL) {
                g_debug ("%s failed: %s", method, error->message);
                g_clear_error (&error);
                return FALSE;
        } else {
                g_variant_get (res, variant_type, val, NULL);
                g_variant_unref (res);
        }

        return TRUE;
}

static gboolean
get_property (GDBusProxy *proxy,
              const char *property,
              const char *variant_type,
              gpointer    val)
{
        GVariant *res;

        res = g_dbus_proxy_get_cached_property (proxy, property);

        if (res == NULL) {
                g_debug ("failed accessing %s", property);
                return FALSE;
        } else {
                g_variant_get (res, variant_type, val, NULL);
                g_variant_unref (res);
        }

        return TRUE;
}

static gboolean
get_uint (GDBusProxy *proxy,
          const char *method,
          guint      *val)
{
        return get_value (proxy, method, "(u)", val);
}

static gboolean
get_uint_property (GDBusProxy *proxy,
                   const char *property,
                   guint      *val)
{
        return get_property (proxy, property, "u", val);
}

static gboolean
get_path (GDBusProxy *proxy,
          const char *method,
          char      **str)
{
        return get_value (proxy, method, "(o)", str);
}

static gboolean
get_string (GDBusProxy *proxy,
            const char *method,
            char      **str)
{
        return get_value (proxy, method, "(s)", str);
}

static gboolean
get_boolean (GDBusProxy *proxy,
             const char *method,
             gboolean   *value)
{
        return get_value (proxy, method, "(b)", value);
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
list_session (GDBusConnection *connection,
              const char      *ssid)
{
        GDBusProxy *proxy;
        guint       uid = 0;
        char       *realname;
        char       *sid;
        char       *lsid;
        char       *session_type;
        char       *session_class;
        char       *session_state;
        char       *session_service;
        char       *x11_display;
        char       *x11_display_device;
        char       *display_device;
        char       *remote_host_name;
        char       *creation_time;
        char       *idle_since_hint;
        char       *runtime_dir;
        gboolean    is_active;
        gboolean    is_local;
        char       *short_sid;
        char       *short_ssid;
        guint       vtnum;
        GError     *error = NULL;

        proxy = g_dbus_proxy_new_sync (connection,
                                       G_DBUS_PROXY_FLAGS_NONE,
                                       NULL,
                                       CK_NAME,
                                       ssid,
                                       CK_SESSION_INTERFACE,
                                       NULL,
                                       &error);
        if (proxy == NULL) {
                g_print ("error creating proxy, %s", error->message);
                g_clear_error (&error);
                return;
        }

        sid = NULL;
        lsid = NULL;
        session_type = NULL;
        session_class = NULL;
        session_state = NULL;
        session_service = NULL;
        x11_display = NULL;
        x11_display_device = NULL;
        display_device = NULL;
        remote_host_name = NULL;
        creation_time = NULL;
        idle_since_hint = NULL;
        vtnum = -1;

        get_uint (proxy, "GetUnixUser", &uid);
        get_path (proxy, "GetSeatId", &sid);
        get_string (proxy, "GetLoginSessionId", &lsid);
        get_string (proxy, "GetSessionType", &session_type);
        get_string (proxy, "GetSessionClass", &session_class);
        get_string (proxy, "GetSessionState", &session_state);
        get_string (proxy, "GetSessionService", &session_service);
        get_string (proxy, "GetX11Display", &x11_display);
        get_string (proxy, "GetX11DisplayDevice", &x11_display_device);
        get_string (proxy, "GetDisplayDevice", &display_device);
        get_string (proxy, "GetRemoteHostName", &remote_host_name);
        get_boolean (proxy, "IsActive", &is_active);
        get_boolean (proxy, "IsLocal", &is_local);
        get_string (proxy, "GetCreationTime", &creation_time);
        get_string (proxy, "GetIdleSinceHint", &idle_since_hint);
        get_string (proxy, "GetXDGRuntimeDir", &runtime_dir);
        get_uint_property (proxy, "VTNr", &vtnum);

        realname = get_real_name (uid);

        short_sid = g_path_get_basename(sid);
        short_ssid = g_path_get_basename(ssid);

        printf ("%s:\n"
                "\tunix-user = '%d'\n"
                "\trealname = '%s'\n"
                "\tseat = '%s'\n"
                "\tsession-type = '%s'\n"
                "\tsession-class = '%s'\n"
                "\tsession-state = '%s'\n"
                "\tsession-service = '%s'\n"
                "\tactive = %s\n"
                "\tx11-display = '%s'\n"
                "\tx11-display-device = '%s'\n"
                "\tdisplay-device = '%s'\n"
                "\tremote-host-name = '%s'\n"
                "\tis-local = %s\n"
                "\ton-since = '%s'\n"
                "\tlogin-session-id = '%s'\n"
                "\tXDG_RUNTIME_DIR = '%s'",
                short_ssid,
                uid,
                realname,
                short_sid,
                session_type,
                session_class,
                session_state,
                session_service,
                is_active ? "TRUE" : "FALSE",
                x11_display,
                x11_display_device,
                display_device,
                remote_host_name,
                is_local ? "TRUE" : "FALSE",
                creation_time,
                lsid,
                runtime_dir);
        if (vtnum != 0) {
                printf ("\n\tVTNr = '%u'", vtnum);
        }
        if (idle_since_hint != NULL && idle_since_hint[0] != '\0') {
                printf ("\n\tidle-since-hint = '%s'", idle_since_hint);
        }
        printf ("\n");

        g_free (idle_since_hint);
        g_free (creation_time);
        g_free (remote_host_name);
        g_free (realname);
        g_free (sid);
        g_free (short_sid);
        g_free (short_ssid);
        g_free (lsid);
        g_free (session_type);
        g_free (session_class);
        g_free (session_state);
        g_free (x11_display);
        g_free (x11_display_device);
        g_free (display_device);
        g_free (runtime_dir);
        g_object_unref (proxy);
}

static void
list_sessions (GDBusConnection *connection,
               const char      *sid)
{
        GDBusProxy   *proxy;
        GError       *error;
        GVariant     *res;
        GVariantIter *iter;

        proxy = g_dbus_proxy_new_sync (connection,
                                       G_DBUS_PROXY_FLAGS_NONE,
                                       NULL,
                                       CK_NAME,
                                       sid,
                                       CK_SEAT_INTERFACE,
                                       NULL,
                                       &error);

        if (proxy == NULL) {
                g_print ("error creating proxy, %s", error->message);
                g_clear_error (&error);
                return;
        }

        error = NULL;
        res = g_dbus_proxy_call_sync (proxy,
                                      "GetSessions",
                                      g_variant_new ("()"),
                                      G_DBUS_CALL_FLAGS_NONE,
                                      6000,
                                      NULL,
                                      &error);

        if (res == NULL) {
                g_warning ("Failed to get list of sessions for %s: %s", sid, error->message);
                g_clear_error (&error);
                goto out;
        }

        g_variant_get (res, "(ao)", &iter);
        while (g_variant_iter_next (iter, "o", &sid))
        {
                list_session (connection, sid);
        }
        g_variant_iter_free (iter);
        g_variant_unref (res);

 out:
        g_object_unref (proxy);
}

static void
list_seats (GDBusConnection *connection)
{
        GDBusProxy   *proxy;
        GError       *error;
        GVariant     *res;
        GVariantIter *iter;
        gchar        *path = NULL;

        proxy = g_dbus_proxy_new_sync (connection,
                                       G_DBUS_PROXY_FLAGS_NONE,
                                       NULL,
                                       CK_NAME,
                                       CK_MANAGER_PATH,
                                       CK_MANAGER_INTERFACE,
                                       NULL,
                                       &error);

        if (proxy == NULL) {
                g_print ("error creating proxy, %s", error->message);
                g_clear_error (&error);
                return;
        }

        error = NULL;
        res = g_dbus_proxy_call_sync (proxy,
                                      "GetSeats",
                                      g_variant_new ("()"),
                                      G_DBUS_CALL_FLAGS_NONE,
                                      6000,
                                      NULL,
                                      &error);

        if (res == NULL) {
                g_warning ("Failed to get list of seats: %s", error->message);
                g_clear_error (&error);
                goto out;
        }

        g_variant_get (res, "(ao)", &iter);
        while (g_variant_iter_next (iter, "o", &path))
        {
                list_sessions (connection, path);
        }
        g_variant_iter_free (iter);
        g_variant_unref (res);

 out:
        g_object_unref (proxy);
}

int
main (int    argc,
      char **argv)
{
        GDBusConnection *connection;

        GOptionContext *context;
        gboolean        retval;
        GError         *error = NULL;
        static gboolean do_version = FALSE;
        static GOptionEntry entries [] = {
                { "version", 'V', 0, G_OPTION_ARG_NONE, &do_version, N_("Version of this application"), NULL },
                { NULL }
        };

        /* Setup for i18n */
        setlocale(LC_ALL, "");
 
#ifdef ENABLE_NLS
        bindtextdomain(PACKAGE, LOCALEDIR);
        textdomain(PACKAGE);
#endif

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
        connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
        if (connection == NULL) {
                g_message ("Failed to connect to the D-Bus daemon: %s", error->message);
                g_error_free (error);
                exit (1);
        }

        list_seats (connection);

        return 0;
}
