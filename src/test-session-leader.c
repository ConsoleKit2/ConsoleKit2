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

#include "ck-session-leader.h"

#define UID 9000
#define PID 8999
#define SERVICE_NAME "test-session-leader"
#define SESSION_ID "Session9999"
#define COOKIE "test-session-leader-cookie"

CkSessionLeader *leader = NULL;

static void
collect_parameters_cb (CkSessionLeader       *leader,
                       GVariant              *parameters,
                       GDBusMethodInvocation *context,
                       GMainLoop             *loop)
{
        g_debug ("[collect_parameters_cb] Parameters: %s", g_variant_print (parameters, TRUE));
        g_main_loop_quit (loop);
}

static void
test_leader_init (void)
{
        leader = ck_session_leader_new ();
        ck_session_leader_set_uid (leader, UID);
        ck_session_leader_set_pid (leader, PID);
        ck_session_leader_set_service_name (leader, SERVICE_NAME);
        ck_session_leader_set_session_id (leader, SESSION_ID);
        ck_session_leader_set_cookie (leader, COOKIE);

        /* Verify! */
        g_assert (CK_IS_SESSION_LEADER (leader));
        g_assert (ck_session_leader_get_uid (leader) == UID);
        g_assert (ck_session_leader_get_pid (leader) == PID);
        g_assert (g_strcmp0 (ck_session_leader_peek_service_name (leader), SERVICE_NAME) == 0);
        g_assert (g_strcmp0 (ck_session_leader_peek_session_id (leader), SESSION_ID) == 0);
        g_assert (g_strcmp0 (ck_session_leader_peek_cookie (leader), COOKIE) == 0);
}

static void
test_leader_collect (void)
{
        GMainLoop *loop;

        g_debug ("Setting up main loop");

        loop = g_main_loop_new (NULL, FALSE);

        ck_session_leader_collect_parameters (leader, NULL,
                                              (CkSessionLeaderDoneFunc)collect_parameters_cb, loop);

        g_main_loop_run (loop);
}

static void
test_leader_unref (void)
{
    g_object_unref (leader);
}

int
main (int   argc,
      char *argv[])
{
        /* do not run these tests as root */
        if (getuid () == 0) {
                g_warning ("You must NOT be root to run this tests");
                exit (1);
        }

        g_test_init (&argc, &argv, NULL);

        g_test_add_func ("/test_session_leader/test_leader_init", test_leader_init);
        g_test_add_func ("/test_session_leader/test_leader_collect", test_leader_collect);
        g_test_add_func ("/test_session_leader/test_leader_unref", test_leader_unref);

        return g_test_run();
}
