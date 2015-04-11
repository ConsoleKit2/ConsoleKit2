/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014 Eric Koegel <eric.koegel@gmail.com>
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

#include <glib.h>
#include <glib-object.h>
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <gio/gio.h>

#define DBUS_NAME                        "org.freedesktop.ConsoleKit"
#define DBUS_MANAGER_INTERFACE           DBUS_NAME ".Manager"
#define DBUS_SESSION_INTERFACE           DBUS_NAME ".Session"
#define DBUS_MANAGER_OBJECT_PATH         "/org/freedesktop/ConsoleKit/Manager"

GDBusProxy *manager;


static void
print_proxy_info (GDBusProxy *proxy)
{
    g_print ("proxy info:\n"
             "name %s\n"
             "name owner %s\n"
             "object path %s\n"
             "interface name %s\n",
             g_dbus_proxy_get_name (proxy),
             g_dbus_proxy_get_name_owner (proxy),
             g_dbus_proxy_get_object_path (proxy),
             g_dbus_proxy_get_interface_name (proxy));
}

static void
print_reply (GDBusProxy *proxy, const gchar *method)
{
    GVariant *var;
    GError   *error = NULL;

    g_print ("calling %s\t", method);
    var = g_dbus_proxy_call_sync (proxy, method, g_variant_new ("()"), G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &error);
    if (var != NULL) {
        GString *string = g_variant_print_string (var, NULL, TRUE);
        g_print ("%s", string->str);
    } else {
        g_print ("returned NULL\t");
        if (error)
            g_print ("error %s", error->message);
    }
    g_clear_error (&error);
    if (var)
        g_variant_unref (var);
    g_print ("\n");
}

static gboolean
validate_stuff (const gchar *path)
{
    GDBusProxy *session;
    GError     *error = NULL;

    g_print ("entering validate_stuff for %s\n", path);

    session = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                             G_DBUS_PROXY_FLAGS_NONE,
                                             NULL,
                                             DBUS_NAME,
                                             path,
                                             DBUS_SESSION_INTERFACE,
                                             NULL,
                                             &error);

    if (session == NULL || error != NULL)
    {
        if (error != NULL) {
            g_printerr ("Error creating session proxy: %s\n", error->message);
            g_clear_error (&error);
        }
        return FALSE;
    }

    print_proxy_info (session);

    print_reply (session, "GetId");

    print_reply (session, "GetSeatId");

    print_reply (session, "GetSessionType");

    print_reply (session, "GetUser");

    print_reply (session, "GetUnixUser");

    print_reply (session, "GetX11Display");

    print_reply (session, "GetX11DisplayDevice");

    print_reply (session, "GetDisplayDevice");

    print_reply (session, "GetRemoteHostName");

    print_reply (session, "GetLoginSessionId");

    print_reply (session, "IsActive");

    print_reply (session, "IsLocal");

    print_reply (session, "GetCreationTime");

    print_reply (session, "GetIdleHint");

    print_reply (session, "GetIdleSinceHint");

    g_print ("done printing stuff for %s\n\n", path);

    g_object_unref (session);

    return TRUE;
}

static gboolean
get_sessions (gpointer user_data)
{
    GVariant     *var;
    GVariantIter *iter;
    GError       *error = NULL;
    gchar        *path = NULL;

    g_print ("calling GetSessions\n");
    var = g_dbus_proxy_call_sync (manager, "GetSessions", g_variant_new ("()"), G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &error);
    if (var == NULL) {
        g_print ("returned NULL\t");

        if (error)
            g_print ("error %s", error->message);

        g_print ("\n");
        g_clear_error (&error);
        return FALSE;
    }

    g_variant_get (var, "(ao)", &iter);
    while (g_variant_iter_next (iter, "o", &path))
    {
        validate_stuff (path);
    }
    g_variant_iter_free (iter);
    g_variant_unref (var);

    return FALSE;
}

static void
open_session (void)
{
    GDBusProxy   *session;
    GVariant     *cookie_var, *session_var, *activate_var;
    GError       *error = NULL;
    const gchar  *path = NULL, *cookie = NULL;

    g_print ("calling OpenSession\n");
    cookie_var = g_dbus_proxy_call_sync (manager, "OpenSession", g_variant_new ("()"), G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &error);
    if (cookie_var == NULL) {
        g_print ("returned NULL\t");

        if (error)
            g_print ("error %s", error->message);

        g_print ("\n");
        g_clear_error (&error);
        return;
    }

    g_variant_get (cookie_var, "(s)", &cookie, NULL);

    g_print ("calling GetSessionForCookie\n");
    session_var = g_dbus_proxy_call_sync (manager, "GetSessionForCookie", g_variant_new ("(s)", cookie), G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &error);
    if (session_var == NULL) {
        g_print ("returned NULL, is the daemon running?\t");

        if (error)
            g_print ("error %s", error->message);

        g_print ("\n");
        g_clear_error (&error);
        return;
    }

    g_variant_get (session_var, "(o)", &path, NULL);

    session = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                             G_DBUS_PROXY_FLAGS_NONE,
                                             NULL,
                                             DBUS_NAME,
                                             path,
                                             DBUS_SESSION_INTERFACE,
                                             NULL,
                                             &error);

    if (session == NULL || error != NULL)
    {
        if (error != NULL) {
            g_printerr ("Error creating session proxy: %s\n", error->message);
            g_clear_error (&error);
        }

        return;
    }

    validate_stuff (path);

    g_print ("calling Lock (expected: GDBus.Error:org.freedesktop.DBus.Error.AccessDenied if not run as root)\n");
    activate_var = g_dbus_proxy_call_sync (session, "Lock", g_variant_new ("()"), G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &error);
    if (activate_var == NULL) {
        g_print ("returned NULL\t");

        if (error)
            g_print ("error %s", error->message);

        g_print ("\n");
        g_clear_error (&error);
    }
    if (activate_var)
        g_variant_unref (activate_var);

    g_print ("calling Unlock (expected: GDBus.Error:org.freedesktop.DBus.Error.AccessDenied if not run as root)\n");
    activate_var = g_dbus_proxy_call_sync (session, "Unlock", g_variant_new ("()"), G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &error);
    if (activate_var == NULL) {
        g_print ("returned NULL\t");

        if (error)
            g_print ("error %s", error->message);

        g_print ("\n");
        g_clear_error (&error);
    }
    if (activate_var)
        g_variant_unref (activate_var);

    g_print ("calling Activate (expected: GDBus.Error:org.freedesktop.ConsoleKit.Seat.Error.NotSupported)\n");
    activate_var = g_dbus_proxy_call_sync (session, "Activate", g_variant_new ("()"), G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &error);
    if (activate_var == NULL) {
        g_print ("returned NULL\t");

        if (error)
            g_print ("error %s", error->message);

        g_print ("\n");
        g_clear_error (&error);
    }
    if (activate_var)
        g_variant_unref (activate_var);
}

int
main (int   argc,
      char *argv[])
{
    GError  *error = NULL;

    g_setenv ("G_DEBUG", "fatal_criticals", FALSE);
              g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL);

    manager = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                             G_DBUS_PROXY_FLAGS_NONE,
                                             NULL,
                                             DBUS_NAME,
                                             DBUS_MANAGER_OBJECT_PATH,
                                             DBUS_MANAGER_INTERFACE,
                                             NULL,
                                             &error);

    if (manager == NULL || error != NULL)
    {
        if (error != NULL) {
            g_printerr ("Error creating manager proxy: %s\n", error->message);
            g_clear_error (&error);
        }
        return FALSE;
    }

    print_proxy_info (manager);

    open_session ();

    get_sessions (NULL);

    g_object_unref (manager);

    return 0;
}
