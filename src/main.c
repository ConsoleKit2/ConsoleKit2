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
#include <signal.h>
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

#include "ck-sysdeps.h"
#include "ck-manager.h"
#include "ck-log.h"

#define CK_DBUS_NAME         "org.freedesktop.ConsoleKit"

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

        bus_proxy = dbus_g_proxy_new_for_name (connection,
                                               DBUS_SERVICE_DBUS,
                                               DBUS_PATH_DBUS,
                                               DBUS_INTERFACE_DBUS);
        return bus_proxy;
}

static gboolean
acquire_name_on_proxy (DBusGProxy *bus_proxy)
{
        GError     *error;
        guint       result;
        gboolean    res;
        gboolean    ret;

        ret = FALSE;

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

        ret = TRUE;

 out:
        return ret;
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

static void
bus_proxy_destroyed_cb (DBusGProxy *bus_proxy,
                        GMainLoop  *loop)
{
        g_debug ("Disconnected from D-Bus");
        g_main_loop_quit (loop);
}

static void
delete_pid (void)
{
        unlink (CONSOLE_KIT_PID_FILE);
}

/* copied from nautilus */
static int debug_log_pipes[2];

static gboolean
debug_log_io_cb (GIOChannel  *io,
                 GIOCondition condition,
                 gpointer     data)
{
        char a;

        while (read (debug_log_pipes[0], &a, 1) != 1)
                ;

        ck_log_toggle_debug ();

        return TRUE;
}

static void
sigusr1_handler (int sig)
{
        while (write (debug_log_pipes[1], "a", 1) != 1)
                ;
}

static void
setup_debug_log_signals (void)
{
        struct sigaction sa;
        GIOChannel      *io;

        if (pipe (debug_log_pipes) == -1) {
                g_error ("Could not create pipe() for debug log");
        }

        io = g_io_channel_unix_new (debug_log_pipes[0]);
        g_io_add_watch (io, G_IO_IN, debug_log_io_cb, NULL);

        sa.sa_handler = sigusr1_handler;
        sigemptyset (&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction (SIGUSR1, &sa, NULL);
}

static void
setup_debug_log (gboolean debug)
{
        ck_log_init ();
        ck_log_set_debug (debug);
        setup_debug_log_signals ();
}

static void
create_pid_file (void)
{
        char   *dirname;
        int     res;
        int     pf;
        ssize_t written;
        char    pid[9];

        /* remove old pid file */
        unlink (CONSOLE_KIT_PID_FILE);

        dirname = g_path_get_dirname (CONSOLE_KIT_PID_FILE);
        errno = 0;
        res = g_mkdir_with_parents (dirname,
                                    S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
        if (res < 0) {
                g_warning ("Unable to create directory %s (%s)",
                           dirname,
                           g_strerror (errno));
        }
        g_free (dirname);

        /* make a new pid file */
        if ((pf = open (CONSOLE_KIT_PID_FILE, O_WRONLY|O_CREAT|O_TRUNC|O_EXCL, 0644)) > 0) {
                snprintf (pid, sizeof (pid), "%lu\n", (long unsigned) getpid ());
                written = write (pf, pid, strlen (pid));
                close (pf);
                g_atexit (delete_pid);
        } else {
                g_warning ("Unable to write pid file %s: %s",
                           CONSOLE_KIT_PID_FILE,
                           g_strerror (errno));
        }
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
        GError          *error;
        int              ret;
        gboolean         res;
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

        if (! g_thread_supported ()) {
                g_thread_init (NULL);
        }
        dbus_g_thread_init ();
        g_type_init ();

        if (! ck_is_root_user ()) {
                g_warning ("Must be run as root");
                exit (1);
        }

        if (debug) {
                g_setenv ("G_DEBUG", "fatal_criticals", FALSE);
                g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL);
        }

        context = g_option_context_new (_("Console kit daemon"));
        g_option_context_add_main_entries (context, entries, NULL);
        error = NULL;
        res = g_option_context_parse (context, &argc, &argv, &error);
        g_option_context_free (context);
        if (! res) {
                g_warning (error->message);
                g_error_free (error);
                goto out;
        }

        if (! no_daemon && daemon (0, 0)) {
                g_error ("Could not daemonize: %s", g_strerror (errno));
        }

        setup_debug_log (debug);

        connection = get_system_bus ();
        if (connection == NULL) {
                goto out;
        }

        bus_proxy = get_bus_proxy (connection);
        if (bus_proxy == NULL) {
                g_warning ("Could not construct bus_proxy object; bailing out");
                goto out;
        }

        if (! acquire_name_on_proxy (bus_proxy) ) {
                g_warning ("Could not acquire name; bailing out");
                goto out;
        }

        g_debug ("initializing console-kit-daemon %s", VERSION);

        create_pid_file ();

        manager = ck_manager_new ();

        if (manager == NULL) {
                goto out;
        }

        loop = g_main_loop_new (NULL, FALSE);

        g_signal_connect (bus_proxy,
                          "destroy",
                          G_CALLBACK (bus_proxy_destroyed_cb),
                          loop);

        if (do_timed_exit) {
                g_timeout_add (1000 * 30, (GSourceFunc) timed_exit_cb, loop);
        }

        g_main_loop_run (loop);

        if (manager != NULL) {
                g_object_unref (manager);
        }

        g_main_loop_unref (loop);

        ret = 0;

 out:

        return ret;
}
