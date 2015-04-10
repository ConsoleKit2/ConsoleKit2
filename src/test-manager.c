/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Eric Koegel <eric.koegel@gmail.com>
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
#define DBUS_SEAT_INTERFACE              DBUS_NAME ".Seat"
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
print_inhibit_reply (GDBusProxy *proxy, const gchar *method)
{
    GVariant *var;
    GError   *error = NULL;

    g_print ("calling %s\t", method);
    var = g_dbus_proxy_call_sync (proxy, method, g_variant_new ("(sss)", "sleep:idle", "test-manager", "because it's good to test things?"), G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &error);
    if (var != NULL) {
        GString *string = g_variant_print_string (var, NULL, TRUE);
        g_print ("%s", string->str);
    } else {
        g_print ("returned NULL\t");
        if (error)
            g_print ("error %s", error->message);
    }
    g_clear_error (&error);
    g_print ("\n");
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
    g_print ("\n");
}

static gboolean
validate_stuff ()
{
    print_reply (manager, "CanRestart");

    print_reply (manager, "CanStop");

    print_reply (manager, "CanReboot");

    print_reply (manager, "CanPowerOff");

    print_reply (manager, "CanSuspend");

    print_reply (manager, "CanHibernate");

    print_reply (manager, "CanHybridSleep");

    print_inhibit_reply (manager, "Inhibit");

    print_reply (manager, "OpenSession");

    print_reply (manager, "GetSeats");

    print_reply (manager, "GetSessions");

    print_reply (manager, "GetCurrentSession");

    print_reply (manager, "GetSystemIdleHint");

    print_reply (manager, "GetSystemIdleSinceHint");

    /* TODO: OpenSessionWithParameters / CloseSession / GetSessionForCookie
     * GetSessionForUnixProcess / GetSessionsForUnixUser / GetSessionsForUser
     */

    g_print ("done printing stuff\n\n");

    return TRUE;
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

    g_print ("\n");

    validate_stuff ();

    return 0;
}
