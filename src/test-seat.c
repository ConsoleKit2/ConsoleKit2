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
    GDBusProxy *seat;
    GError     *error = NULL;

    g_print ("entering validate_stuff for %s\n", path);

    seat = g_dbus_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
                                          G_DBUS_PROXY_FLAGS_NONE,
                                          NULL,
                                          DBUS_NAME,
                                          path,
                                          DBUS_SEAT_INTERFACE,
                                          NULL,
                                          &error);

    if (seat == NULL || error != NULL)
    {
        if (error != NULL) {
            g_printerr ("Error creating seat proxy: %s\n", error->message);
            g_clear_error (&error);
        }
        return FALSE;
    }

    print_proxy_info (seat);

    print_reply (seat, "GetId");

    print_reply (seat, "GetSessions");

    print_reply (seat, "GetDevices");

    print_reply (seat, "CanActivateSessions");

    print_reply (seat, "GetActiveSession");

    g_print ("done printing stuff for %s\n\n", path);

    g_object_unref (seat);

    return TRUE;
}

static gboolean
get_seats (gpointer user_data)
{
    GVariant     *var;
    GVariantIter *iter;
    GError       *error = NULL;
    gchar        *path = NULL;

    g_print ("calling GetSeats\n");
    var = g_dbus_proxy_call_sync (manager, "GetSeats", g_variant_new ("()"), G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &error);
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

    get_seats (NULL);

    g_object_unref (manager);

    return 0;
}
