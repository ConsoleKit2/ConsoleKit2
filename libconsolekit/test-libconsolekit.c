/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (c) 2017, Eric Koegel <eric.koegel@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "libconsolekit.h"



static void
test_seat_get_active (LibConsoleKit *ck,
                      const gchar *seat)
{
    gchar *session = NULL;
    uid_t uid = 0;
    GError *error = NULL;

    /* try it with all the defaults */
    lib_consolekit_seat_get_active (ck, seat, &session, &uid, &error);
    if (!error) {
        g_print ("lib_consolekit_seat_get_active (seat %s) : session %s uid %d\n", seat, session, uid);
    } else {
        g_print ("lib_consolekit_seat_get_active (seat %s) : error %s\n", seat, error->message);
        g_clear_error (&error);
    }

    /* try it without the UID */
    lib_consolekit_seat_get_active (ck, seat, &session, NULL, &error);
    if (!error) {
        g_print ("lib_consolekit_seat_get_active (seat %s) : session %s ignoring uid\n", seat, session);
    } else {
        g_print ("lib_consolekit_seat_get_active (seat %s) : error %s\n", seat, error->message);
        g_clear_error (&error);
    }

    g_free (session);
}

static void
test_seat_get_sessions (LibConsoleKit *ck,
                        const gchar *seat)
{
    gchar **sessions = NULL;
    gint num_sessions;
    gint i;
    GError *error = NULL;

    num_sessions = lib_consolekit_seat_get_sessions (ck, seat, &sessions, &error);
    if (!error) {
        g_print ("lib_consolekit_seat_get_sessions (seat %s) : number of sessions %d\n", seat, num_sessions);
        for (i = 0; i < num_sessions && num_sessions > 0; i++) {
            g_print ("lib_consolekit_seat_get_sessions (seat %s) : session %s\n", seat, sessions[i]);
        }
    } else {
        g_print ("lib_consolekit_seat_get_sessions (seat %s) : error %s\n", seat, error->message);
        g_clear_error (&error);
    }

    g_strfreev (sessions);
}

static void
test_seat_can_multisession (LibConsoleKit *ck,
                            const gchar *seat)
{
    gboolean can_multisession;
    GError *error = NULL;

    can_multisession = lib_consolekit_seat_can_multi_session (ck, seat, &error);
    if (!error) {
        g_print ("lib_consolekit_seat_can_multi_session (seat %s) : %s\n", seat, can_multisession ? "TRUE" : "FALSE");
    } else {
        g_print ("lib_consolekit_seat_can_multi_session (seat %s) : error %s\n", seat, error->message);
        g_clear_error (&error);
    }
}

static void
test_session_is_active (LibConsoleKit *ck,
                        const gchar *session)
{
    gboolean is_active;
    GError *error = NULL;

    is_active = lib_consolekit_session_is_active (ck, session, &error);
    if (!error) {
        g_print ("lib_consolekit_session_is_active (session %s) : %s\n", session, is_active ? "TRUE" : "FALSE");
    } else {
        g_print ("lib_consolekit_session_is_active (session %s) : error %s\n", session, error->message);
        g_clear_error (&error);
    }
}

static void
test_session_is_remote (LibConsoleKit *ck,
                        const gchar *session)
{
    gboolean is_remote;
    GError *error = NULL;

    is_remote = lib_consolekit_session_is_remote (ck, session, &error);
    if (!error) {
        g_print ("lib_consolekit_session_is_remote (session %s) : %s\n", session, is_remote ? "TRUE" : "FALSE");
    } else {
        g_print ("lib_consolekit_session_is_remote (session %s) : error %s\n", session, error->message);
        g_clear_error (&error);
    }
}

static void
test_session_get_uid (LibConsoleKit *ck,
                      const gchar *session)
{
    guint uid;
    GError *error = NULL;

    lib_consolekit_session_get_uid (ck, session, &uid, &error);
    if (!error) {
        g_print ("lib_consolekit_session_get_uid (session %s) : uid %d\n", session, uid);
    } else {
        g_print ("lib_consolekit_session_get_uid (session %s) : error %s\n", session, error->message);
        g_clear_error (&error);
    }
}

static void
test_session_get_seat (LibConsoleKit *ck,
                       const gchar *session)
{
    gchar *seat = NULL;
    GError *error = NULL;

    lib_consolekit_session_get_seat (ck, session, &seat, &error);
    if (!error) {
        g_print ("lib_consolekit_session_get_seat (session %s) : seat %s\n", session, seat);
    } else {
        g_print ("lib_consolekit_session_get_seat (session %s) : error %s\n", session, error->message);
        g_clear_error (&error);
    }

    g_free (seat);
}

static void
test_session_get_display (LibConsoleKit *ck,
                          const gchar *session)
{
    gchar *display = NULL;
    GError *error = NULL;

    lib_consolekit_session_get_display (ck, session, &display, &error);
    if (!error) {
        g_print ("lib_consolekit_session_get_display (session %s) : display %s\n", session, display);
    } else {
        g_print ("lib_consolekit_session_get_display (session %s) : error %s\n", session, error->message);
        g_clear_error (&error);
    }

    g_free (display);
}

static void
test_session_get_remote_host (LibConsoleKit *ck,
                              const gchar *session)
{
    gchar *hostname = NULL;
    GError *error = NULL;

    lib_consolekit_session_get_remote_host (ck, session, &hostname, &error);
    if (!error) {
        g_print ("lib_consolekit_session_get_remote_host (session %s) : hostname %s\n", session, hostname);
    } else {
        g_print ("lib_consolekit_session_get_remote_host (session %s) : error %s\n", session, error->message);
        g_clear_error (&error);
    }

    g_free (hostname);
}

static void
test_session_get_type (LibConsoleKit *ck,
                       const gchar *session)
{
    gchar *type = NULL;
    GError *error = NULL;

    lib_consolekit_session_get_type (ck, session, &type, &error);
    if (!error) {
        g_print ("lib_consolekit_session_get_type (session %s) : session type %s\n", session, type);
    } else {
        g_print ("lib_consolekit_session_get_type (session %s) : error %s\n", session, error->message);
        g_clear_error (&error);
    }

    g_free (type);
}

static void
test_session_get_class (LibConsoleKit *ck,
                        const gchar *session)
{
    gchar *session_class = NULL;
    GError *error = NULL;

    lib_consolekit_session_get_class (ck, session, &session_class, &error);
    if (!error) {
        g_print ("lib_consolekit_session_get_class (session %s) : session class %s\n", session, session_class);
    } else {
        g_print ("lib_consolekit_session_get_class (session %s) : error %s\n", session, error->message);
        g_clear_error (&error);
    }

    g_free (session_class);
}

static void
test_session_get_state (LibConsoleKit *ck,
                        const gchar *session)
{
    gchar *state = NULL;
    GError *error = NULL;

    lib_consolekit_session_get_state (ck, session, &state, &error);
    if (!error) {
        g_print ("lib_consolekit_session_get_state (session %s) : session state %s\n", session, state);
    } else {
        g_print ("lib_consolekit_session_get_state (session %s) : error %s\n", session, error->message);
        g_clear_error (&error);
    }

    g_free (state);
}

static void
test_session_get_tty (LibConsoleKit *ck,
                      const gchar *session)
{
    gchar *tty = NULL;
    GError *error = NULL;

    lib_consolekit_session_get_tty (ck, session, &tty, &error);
    if (!error) {
        g_print ("lib_consolekit_session_get_tty (session %s) : tty %s\n", session, tty);
    } else {
        g_print ("lib_consolekit_session_get_tty (session %s) : error %s\n", session, error->message);
        g_clear_error (&error);
    }

    g_free (tty);
}

static void
test_session_get_vt (LibConsoleKit *ck,
                     const gchar *session)
{
    guint vt;
    GError *error = NULL;

    lib_consolekit_session_get_vt (ck, session, &vt, &error);
    if (!error) {
        g_print ("lib_consolekit_session_get_vt (session %s) : vt %d\n", session, vt);
    } else {
        g_print ("lib_consolekit_session_get_vt (session %s) : error %s\n", session, error->message);
        g_clear_error (&error);
    }
}

static void
test_pid_get_session (LibConsoleKit *ck,
                      gint pid)
{
    gchar *session = NULL;
    GError *error = NULL;

    lib_consolekit_pid_get_session (ck, pid, &session, &error);
    if (!error) {
        g_print ("lib_consolekit_pid_get_session (pid %d) : session %s\n", pid, session);
    } else {
        g_print ("lib_consolekit_pid_get_session (pid %d) : error %s\n", pid, error->message);
        g_clear_error (&error);
    }

    g_free (session);
}




gchar *opt_seat = "/org/freedesktop/ConsoleKit/seat0";
gchar *opt_session = "/org/freedesktop/ConsoleKit/Session2";
gint opt_pid = 0;

static GOptionEntry option_entries[] =
{
  { "seat", 0, G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_STRING, &opt_seat,
    "Specify seat to use in queries",
    NULL
  },
  { "session", 0, G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_STRING, &opt_session,
    "Specify session to use in queries",
    NULL
  },
  { "pid", 0, G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_INT, &opt_pid,
    "Specify process id to use in queries",
    NULL
  },
  { NULL }
};

int
main (int   argc,
      char *argv[])
{
    LibConsoleKit *ck = NULL;
    GError *error = NULL;
    GOptionContext *context;

    g_setenv ("G_DEBUG", "fatal_criticals", FALSE);
              g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL);

    context = g_option_context_new ("");
    g_option_context_add_main_entries (context, option_entries, GETTEXT_PACKAGE);

    if (!g_option_context_parse (context, &argc, &argv, &error)) {
        g_print ("option parsing failed: %s\n", error->message);
        exit (1);
    }

    if (opt_pid <= 0) {
        opt_pid = getpid ();
    }

    ck = lib_consolekit_new ();

    test_seat_get_active (ck, opt_seat);

    test_seat_get_sessions (ck, opt_seat);

    test_seat_can_multisession (ck, opt_seat);

    test_session_is_active (ck, opt_session);

    test_session_is_remote (ck, opt_session);

    test_session_get_uid (ck, opt_session);

    test_session_get_seat (ck, opt_session);

    test_session_get_display (ck, opt_session);

    test_session_get_remote_host (ck, opt_session);

    test_session_get_type (ck, opt_session);

    test_session_get_class (ck, opt_session);

    test_session_get_state (ck, opt_session);

    test_session_get_tty (ck, opt_session);

    test_session_get_vt (ck, opt_session);

    test_pid_get_session (ck, opt_pid);

    g_object_unref (ck);
    g_option_context_free (context);
    return 0;
}
