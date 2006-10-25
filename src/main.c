/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 William Jon McCann <mccann@jhu.edu>
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

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>

#include "ck-manager.h"
#include "ck-debug.h"

#define CK_DBUS_NAME         "org.freedesktop.ConsoleKit"

static gboolean
timed_exit_cb (GMainLoop *loop)
{
	g_main_loop_quit (loop);
	return FALSE;
}

int
main (int    argc,
      char **argv)
{
        GMainLoop       *loop;
        CkManager       *manager;
        GOptionContext  *context;
        GError          *error;
	DBusGProxy      *bus_proxy;
        DBusGConnection *connection;
        gboolean         res;
	guint            result;

        static gboolean     debug            = FALSE;
        static gboolean     no_daemon        = FALSE;
        static gboolean     do_timed_exit    = FALSE;
        static GOptionEntry entries []   = {
                { "debug", 0, 0, G_OPTION_ARG_NONE, &debug, N_("Enable debugging code"), NULL },
                { "no-daemon", 0, 0, G_OPTION_ARG_NONE, &no_daemon, N_("Don't become a daemon"), NULL },
                { NULL }
        };


        g_type_init ();
        g_thread_init (NULL);
        dbus_g_thread_init ();

        context = g_option_context_new (_("Console kit daemon"));
        g_option_context_add_main_entries (context, entries, NULL);
        g_option_context_parse (context, &argc, &argv, NULL);
        g_option_context_free (context);

        error = NULL;
        connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
        if (connection == NULL) {
                g_warning ("Couldn't connect to system bus: %s", error->message);
                g_error_free (error);
                exit (1);
        }

	bus_proxy = dbus_g_proxy_new_for_name (connection,
                                               DBUS_SERVICE_DBUS,
                                               DBUS_PATH_DBUS,
                                               DBUS_INTERFACE_DBUS);

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
                }
                exit (1);
	}

 	if (result != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
                if (error != NULL) {
                        g_warning ("Failed to acquire %s: %s", CK_DBUS_NAME, error->message);
                        g_error_free (error);
                }
                exit (1);
        }

        /* debug to a file if in deamon mode */
        ck_debug_init (debug, ! no_daemon);
        ck_debug ("initializing console-kit-daemon %s", VERSION);

        /* Don't close stdout and stderr for now */
        if (! no_daemon && daemon (0, 1)) {
                g_error ("Could not daemonize: %s", g_strerror (errno));
        }

        manager = ck_manager_new ();

        if (manager == NULL) {
                exit (1);
        }

        loop = g_main_loop_new (NULL, FALSE);

	if (do_timed_exit) {
		g_timeout_add (1000 * 60, (GSourceFunc) timed_exit_cb, loop);
	}

        g_main_loop_run (loop);

        g_object_unref (manager);

        return 0;
}

