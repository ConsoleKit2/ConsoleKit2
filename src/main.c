/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006-2007 William Jon McCann <mccann@jhu.edu>
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
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "ck-manager.h"
#include "ck-debug.h"

#define CK_DBUS_NAME         "org.freedesktop.ConsoleKit"

static void bus_proxy_destroyed_cb (DBusGProxy *bus_proxy,
                                    CkManager  *manager);

static gboolean
timed_exit_cb (GMainLoop *loop)
{
	g_main_loop_quit (loop);
	return FALSE;
}

static DBusGProxy *
get_bus_proxy (DBusGConnection *connection)
{
        DBusGProxy *bus_proxy;
        GError     *error;
        guint       result;
        gboolean    res;

	bus_proxy = dbus_g_proxy_new_for_name (connection,
                                               DBUS_SERVICE_DBUS,
                                               DBUS_PATH_DBUS,
                                               DBUS_INTERFACE_DBUS);
        if (bus_proxy == NULL) {
                goto out;
        }

        error = NULL;
	res = dbus_g_proxy_call (bus_proxy,
                                 "RequestName",
                                 &error,
                                 G_TYPE_STRING, CK_DBUS_NAME,
                                 G_TYPE_UINT, 0,
                                 G_TYPE_INVALID,
                                 G_TYPE_UINT, &result,
                                 G_TYPE_INVALID);
        if (! res) {
                if (error != NULL) {
                        g_warning ("Failed to acquire %s: %s", CK_DBUS_NAME, error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to acquire %s", CK_DBUS_NAME);
                }
                goto out;
	}


 	if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
                if (error != NULL) {
                        g_warning ("Failed to acquire %s: %s", CK_DBUS_NAME, error->message);
                        g_error_free (error);
                } else {
                        g_warning ("Failed to acquire %s", CK_DBUS_NAME);
                }
                goto out;
        }

out:
        return bus_proxy;
}

static DBusGConnection *
get_system_bus (void)
{
        GError          *error;
        DBusGConnection *bus;
        DBusConnection  *connection;

        error = NULL;
        bus = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (bus == NULL) {
                g_warning ("Couldn't connect to system bus: %s",
                           error->message);
                g_error_free (error);
                goto out;
        }

        connection = dbus_g_connection_get_connection (bus);
        dbus_connection_set_exit_on_disconnect (connection, FALSE);

 out:
        return bus;
}

static gboolean
bus_reconnect (CkManager *manager)
{
        DBusGConnection *bus;
        DBusGProxy      *bus_proxy;
        gboolean         ret;

        ret = TRUE;

        bus = get_system_bus ();
        if (bus == NULL) {
                goto out;
        }

        bus_proxy = get_bus_proxy (bus);
        if (bus_proxy == NULL) {
                g_warning ("Could not construct bus_proxy object; will retry");
                goto out;
        }

        manager = ck_manager_new ();
        if (manager == NULL) {
                g_warning ("Could not construct manager object; will retry");
                exit (1);
        }

        g_signal_connect (bus_proxy,
                          "destroy",
                          G_CALLBACK (bus_proxy_destroyed_cb),
                          manager);

        ck_debug ("Successfully reconnected to D-Bus");

        ret = FALSE;

 out:
        return ret;
}

static void
bus_proxy_destroyed_cb (DBusGProxy *bus_proxy,
                        CkManager  *manager)
{
        ck_debug ("Disconnected from D-Bus");

        g_object_unref (manager);
        manager = NULL;

        g_timeout_add (3000, (GSourceFunc)bus_reconnect, manager);
}

static void
delete_pid (void)
{
        unlink (CONSOLE_KIT_PID_FILE);
}

int
main (int    argc,
      char **argv)
{
        GMainLoop       *loop;
        CkManager       *manager;
        GOptionContext  *context;
        DBusGProxy      *bus_proxy;
        DBusGConnection *connection;
        int              ret;
        int              pf;
        ssize_t          written;
        char             pid[9];

        static gboolean     debug            = FALSE;
        static gboolean     no_daemon        = FALSE;
        static gboolean     do_timed_exit    = FALSE;
        static GOptionEntry entries []   = {
                { "debug", 0, 0, G_OPTION_ARG_NONE, &debug, N_("Enable debugging code"), NULL },
                { "no-daemon", 0, 0, G_OPTION_ARG_NONE, &no_daemon, N_("Don't become a daemon"), NULL },
                { "timed-exit", 0, 0, G_OPTION_ARG_NONE, &do_timed_exit, N_("Exit after a time - for debugging"), NULL },
                { NULL }
        };

        ret = 1;

        g_type_init ();
        g_thread_init (NULL);
        dbus_g_thread_init ();

        context = g_option_context_new (_("Console kit daemon"));
        g_option_context_add_main_entries (context, entries, NULL);
        g_option_context_parse (context, &argc, &argv, NULL);
        g_option_context_free (context);

        connection = get_system_bus ();
        if (connection == NULL) {
                goto out;
        }

        bus_proxy = get_bus_proxy (connection);
        if (bus_proxy == NULL) {
                g_warning ("Could not construct bus_proxy object; bailing out");
                goto out;
        }

        /* debug to a file if in deamon mode */
        ck_debug_init (debug, ! no_daemon);
        ck_debug ("initializing console-kit-daemon %s", VERSION);

        /* Don't close stdout and stderr for now */
        if (! no_daemon && daemon (0, 1)) {
                g_error ("Could not daemonize: %s", g_strerror (errno));
        }

        /* remove old pid file */
        unlink (CONSOLE_KIT_PID_FILE);

        /* make a new pid file */
        if ((pf = open (CONSOLE_KIT_PID_FILE, O_WRONLY|O_CREAT|O_TRUNC|O_EXCL, 0644)) > 0) {
                snprintf (pid, sizeof (pid), "%lu\n", (long unsigned) getpid ());
                written = write (pf, pid, strlen (pid));
                close (pf);
                g_atexit (delete_pid);
        }

        manager = ck_manager_new ();

        if (manager == NULL) {
                goto out;
        }

        g_signal_connect (bus_proxy,
                          "destroy",
                          G_CALLBACK (bus_proxy_destroyed_cb),
                          manager);

        loop = g_main_loop_new (NULL, FALSE);

        if (do_timed_exit) {
                g_timeout_add (3000 * 60, (GSourceFunc) timed_exit_cb, loop);
        }

        g_main_loop_run (loop);

        g_object_unref (manager);

        ret = 0;

 out:

        return ret;
}

