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
#include <gio/gunixfdlist.h>

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

static GVariant*
print_method (GDBusProxy *proxy, const gchar *method, GVariant *variant)
{
    GVariant *var;
    GError   *error = NULL;

    g_print ("calling %s\t", method);
    var = g_dbus_proxy_call_sync (proxy, method, variant, G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &error);
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
    return var;
}

static void
print_reply (GDBusProxy *proxy, const gchar *method)
{
    GVariant *var = print_method (proxy, method, g_variant_new ("()"));

    if (var)
        g_variant_unref (var);

}

static gint
print_inhibit_reply (GDBusProxy *proxy, const gchar *method)
{
    GVariant    *var;
    gint         fd = -1;
    GError      *error = NULL;
    GUnixFDList *out_fd_list = NULL;

    g_print ("calling %s\t", method);
    var = g_dbus_proxy_call_with_unix_fd_list_sync (proxy,
                                                    method,
                                                    g_variant_new ("(ssss)",
                                                                   "sleep:shutdown",
                                                                   "test-manager",
                                                                   "testing inhibit",
                                                                   "block"),
                                                    G_DBUS_CALL_FLAGS_NONE,
                                                    -1,
                                                    NULL,
                                                    &out_fd_list,
                                                    NULL,
                                                    &error);
    if (var != NULL) {
        GString *string = g_variant_print_string (var, NULL, TRUE);
        g_print ("%s", string->str);
        fd = g_unix_fd_list_get (out_fd_list, 0, NULL);
        g_print ("fd %d", fd);
    } else {
        g_print ("returned NULL\t");
        if (error)
            g_print ("error %s", error->message);
        g_variant_unref (var);
    }
    g_clear_error (&error);
    g_print ("\n");

    return fd;
}

static gboolean
validate_session (const gchar *path)
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

static void
open_print_and_test_session (GDBusProxy *proxy, const gchar *method)
{
    GVariantBuilder ck_parameters;
    GVariant       *open_var = NULL;
    GVariant       *ssid_var = NULL;
    GVariant       *close_var = NULL;
    GVariant       *activate_var = NULL;
    GError         *error = NULL;
    const gchar    *cookie;
    const gchar    *ssid;
    gboolean        is_session_closed;

    /* Build the params so we can call OpenSessionWithParameters. This
     * is copied from LightDM (with fake values), so we can verify it
     * works as expected */
    g_variant_builder_init (&ck_parameters, G_VARIANT_TYPE ("(a(sv))"));
    g_variant_builder_open (&ck_parameters, G_VARIANT_TYPE ("a(sv)"));
    g_variant_builder_add (&ck_parameters, "(sv)", "unix-user", g_variant_new_int32 (9000));
    g_variant_builder_add (&ck_parameters, "(sv)", "session-type", g_variant_new_string ("LoginWindow"));
    g_variant_builder_add (&ck_parameters, "(sv)", "x11-display", g_variant_new_string (":0.0"));
    g_variant_builder_add (&ck_parameters, "(sv)", "x11-display-device", g_variant_new_string ("/dev/tty15"));
    g_variant_builder_add (&ck_parameters, "(sv)", "is-local", g_variant_new_boolean (TRUE));

    g_print ("calling %s\t", method);
    open_var = g_dbus_proxy_call_sync (proxy, method,
                                       g_variant_new ("(a(sv))", &ck_parameters),
                                       G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &error);
    if (open_var == NULL) {
        g_print ("returned NULL\t");
        if (error)
            g_print ("error %s", error->message);
        goto out;
    }

    g_variant_get (open_var, "(s)", &cookie);
    g_print ("cookie: %s", cookie);

    g_print ("\n");

    /* Since we built a session go ahead and test getting the ssid */
    g_print ("calling GetSessionForCookie\t");
    ssid_var = g_dbus_proxy_call_sync (proxy, "GetSessionForCookie",
                                       g_variant_new ("(s)", cookie),
                                       G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &error);
    if (ssid_var == NULL) {
        g_print ("returned NULL\t");
        if (error)
            g_print ("error %s", error->message);
        goto out;
    }

    g_variant_get (ssid_var, "(o)", &ssid);
    g_print ("ssid: %s", ssid);

    g_print ("\n");

    validate_session (ssid);

    g_print ("calling LockSession (expected: GDBus.Error:org.freedesktop.DBus.Error.AccessDenied if not run as root)\n");
    activate_var = g_dbus_proxy_call_sync (proxy, "LockSession", g_variant_new ("(s)", ssid), G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &error);
    if (activate_var == NULL) {
        g_print ("returned NULL\t");

        if (error)
            g_print ("error %s", error->message);

        g_print ("\n");
        g_clear_error (&error);
    }
    if (activate_var)
        g_variant_unref (activate_var);

    g_print ("calling UnlockSession (expected: GDBus.Error:org.freedesktop.DBus.Error.AccessDenied if not run as root)\n");
    activate_var = g_dbus_proxy_call_sync (proxy, "UnlockSession", g_variant_new ("(s)", ssid), G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &error);
    if (activate_var == NULL) {
        g_print ("returned NULL\t");

        if (error)
            g_print ("error %s", error->message);

        g_print ("\n");
        g_clear_error (&error);
    }
    if (activate_var)
        g_variant_unref (activate_var);

    /* test closing our session */
    g_print ("calling CloseSession\t");
    close_var = g_dbus_proxy_call_sync (proxy, "CloseSession",
                                        g_variant_new ("(s)", cookie),
                                        G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &error);
    if (close_var == NULL) {
        g_print ("returned NULL\t");
        if (error)
            g_print ("error %s", error->message);
        goto out;
    }

    g_variant_get (close_var, "(b)", &is_session_closed);
    g_print ("session closed? %s", is_session_closed ? "Closed" : "Not Closed");

    g_print ("\n");

out:
    g_clear_error (&error);
    if (ssid_var != NULL)
        g_variant_unref (ssid_var);
    if (open_var != NULL)
        g_variant_unref (open_var);
    if (close_var != NULL)
        g_variant_unref (close_var);
    g_print ("\n");
}

static gboolean
validate_stuff (void)
{
    gint      fd;
    GVariant *session_var = NULL, *close_var = NULL;
    gboolean  is_session_closed;
    GError   *error = NULL;

    print_reply (manager, "CanRestart");

    print_reply (manager, "CanStop");

    print_reply (manager, "CanReboot");

    print_reply (manager, "CanPowerOff");

    print_reply (manager, "CanSuspend");

    print_reply (manager, "CanHibernate");

    print_reply (manager, "CanHybridSleep");

    fd = print_inhibit_reply (manager, "Inhibit");

    session_var = print_method (manager, "OpenSession", g_variant_new ("()"));

    print_reply (manager, "GetSeats");

    print_reply (manager, "GetSessions");

    print_reply (manager, "GetCurrentSession");

    print_reply (manager, "GetSystemIdleHint");

    print_reply (manager, "GetSystemIdleSinceHint");

    open_print_and_test_session (manager, "OpenSessionWithParameters");

    print_reply (manager, "ListInhibitors");

    if (fd > -1)
    {
        g_print ("Expecting an Error.Inhibited message\n");
        print_method (manager, "Suspend", g_variant_new ("(b)", FALSE));

        g_close (fd, NULL);
    }

    /* test closing our session */
    if (session_var != NULL) {
        g_print ("calling CloseSession\t");
        close_var = g_dbus_proxy_call_sync (manager, "CloseSession",
                                            session_var,
                                            G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &error);
        if (close_var == NULL) {
            g_print ("returned NULL\t");
            if (error)
                g_print ("error %s", error->message);
        }
    }

    g_variant_get (close_var, "(b)", &is_session_closed);
    g_print ("session closed? %s\n", is_session_closed ? "Closed" : "Not Closed");

    g_print ("done printing stuff\n\n");

    if (session_var)
        g_variant_unref (session_var);
    if (close_var)
        g_variant_unref (close_var);

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

    g_object_unref (manager);

    return 0;
}
