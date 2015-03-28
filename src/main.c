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
#include <libintl.h>
#include <locale.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "ck-sysdeps.h"
#include "ck-manager.h"
#include "ck-log.h"

#define CK_DBUS_NAME "org.freedesktop.ConsoleKit"

static gboolean
timed_exit_cb (GMainLoop *loop)
{
        g_main_loop_quit (loop);
        return FALSE;
}

static void
bus_acquired (GDBusConnection *connection,
              const gchar *name,
              gpointer user_data)
{
        CkManager *manager;

        g_debug ("bus_acquired %s\n", name);

        manager = ck_manager_new (connection);

        if (manager == NULL) {
                g_critical ("Could not create CkManager");
        }
}

static void
name_acquired (GDBusConnection *connection,
               const gchar *name,
               gpointer user_data)
{
        g_debug ("name_acquired\n");
}

static void
name_lost (GDBusConnection *connection,
           const gchar *name,
           gpointer user_data)
{
        g_debug ("name_lost\n");

        /* Release the  object */
        g_debug ("Disconnected from D-Bus");
        exit (0);
}

static void
delete_pid (void)
{
        unlink (CONSOLE_KIT_PID_FILE);
}

#define CONSOLE_TAGS_DIR RUNDIR "/console"

static void
delete_console_tags (void)
{
        GDir *dir;
        GError *error = NULL;
        const gchar *name;

        g_debug ("Cleaning up %s", CONSOLE_TAGS_DIR);

        dir = g_dir_open (CONSOLE_TAGS_DIR, 0, &error);
        if (dir == NULL) {
                g_debug ("Couldn't open directory %s: %s", CONSOLE_TAGS_DIR,
                         error->message);
                g_error_free (error);
                return;
        }
        while ((name = g_dir_read_name (dir)) != NULL) {
                gchar *file;
                file = g_build_filename (CONSOLE_TAGS_DIR, name, NULL);

                g_debug ("Removing tag file: %s", file);
                if (unlink (file) == -1) {
                        g_warning ("Couldn't delete tag file: %s", file);
                }
                g_free (file);
        }
        g_dir_close (dir);
}

static void
delete_inhibit_files (void)
{
        GDir *dir;
        GError *error = NULL;
        const gchar *INHIBIT_DIRECTORY = RUNDIR "/ConsoleKit/inhibit";
        const gchar *name;

        g_debug ("Cleaning up %s", INHIBIT_DIRECTORY);

        dir = g_dir_open (INHIBIT_DIRECTORY, 0, &error);
        if (dir == NULL) {
                g_debug ("Couldn't open directory %s: %s", INHIBIT_DIRECTORY,
                         error->message);
                g_error_free (error);
                return;
        }
        while ((name = g_dir_read_name (dir)) != NULL) {
                gchar *file;

                /* the inhibit files end in .pipe */
                if (!g_str_has_suffix (name, ".pipe")) {
                        continue;
                }

                file = g_build_filename (INHIBIT_DIRECTORY, name, NULL);

                g_debug ("Removing inhibit file: %s", file);
                if (unlink (file) == -1) {
                        g_warning ("Couldn't delete inhibit file: %s", file);
                }
                g_free (file);
        }
        g_dir_close (dir);
}

static void
cleanup (void)
{
        delete_console_tags ();
        delete_inhibit_files ();
        delete_pid ();
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
setup_termination_signals (void)
{
        struct sigaction sa;

        sa.sa_handler = SIG_DFL;
        sigemptyset (&sa.sa_mask);
        sa.sa_flags = 0;

        sigaction (SIGTERM, &sa, NULL);
        sigaction (SIGQUIT, &sa, NULL);
        sigaction (SIGINT, &sa, NULL);
        sigaction (SIGHUP, &sa, NULL);
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
        if ((pf = open (CONSOLE_KIT_PID_FILE, O_WRONLY|O_CREAT|O_TRUNC|O_EXCL, 0644)) >= 0) {
                snprintf (pid, sizeof (pid), "%lu\n", (long unsigned) getpid ());
                written = write (pf, pid, strlen (pid));
                if (written != strlen(pid)) {
                        g_warning ("unable to write pid file %s: %s",
                                   CONSOLE_KIT_PID_FILE,
                                   g_strerror (errno));
                }
                atexit (cleanup);
                close (pf);
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
        GOptionContext  *context;
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

        /* Setup for i18n */
        setlocale(LC_ALL, "");
 
#ifdef ENABLE_NLS
        bindtextdomain(PACKAGE, LOCALEDIR);
        textdomain(PACKAGE);
#endif

        ret = 1;

#if !GLIB_CHECK_VERSION(2, 32, 0)
        if (! g_thread_supported ()) {
                g_thread_init (NULL);
        }
#endif

#if !GLIB_CHECK_VERSION(2, 36, 0)
        g_type_init ();
#endif

        if (! ck_is_root_user ()) {
                g_warning (_("You must be root to run this program"));
                exit (1);
        }

        context = g_option_context_new (_("Console kit daemon"));
        g_option_context_add_main_entries (context, entries, NULL);
        error = NULL;
        res = g_option_context_parse (context, &argc, &argv, &error);
        g_option_context_free (context);
        if (! res) {
                g_warning ("%s", error->message);
                g_clear_error (&error);
                goto out;
        }

#ifdef CONSOLEKIT_DEBUGGING
        /* compiling with --enable-debug=full turns debugging on */
        debug = TRUE;
#endif

        if (debug) {
                g_setenv ("G_DEBUG", "fatal_criticals", FALSE);
                g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL);
        }

        if (! no_daemon && daemon (0, 0)) {
                g_error ("Could not daemonize: %s", g_strerror (errno));
                g_clear_error (&error);
        }

        setup_debug_log (debug);

        setup_termination_signals ();

        g_debug ("initializing console-kit-daemon %s", VERSION);

        g_bus_own_name (G_BUS_TYPE_SYSTEM,
                        CK_DBUS_NAME,
                        G_BUS_NAME_OWNER_FLAGS_NONE,
                        bus_acquired, name_acquired, name_lost,
                        NULL, NULL);

        delete_console_tags ();
        delete_inhibit_files ();

        create_pid_file ();

        loop = g_main_loop_new (NULL, FALSE);

        if (do_timed_exit) {
                g_timeout_add (1000 * 30, (GSourceFunc) timed_exit_cb, loop);
        }

        g_main_loop_run (loop);

        g_main_loop_unref (loop);

        ret = 0;

 out:

        return ret;
}
