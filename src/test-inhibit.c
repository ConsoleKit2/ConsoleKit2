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

#include "ck-inhibit.h"


static const gchar* WHO  = "test-inhibit";
static const gchar* WHAT = "sleep:idle";
static const gchar* WHY  = "for testing";

CkInhibit *inhibit;
gint fd;


static void
test_create_inhibit (void)
{
    /* Let's not overwrite our object */
    g_assert_null (inhibit);

    inhibit = ck_inhibit_new ();

    /* Verify we got a valid inhibit object */
    g_assert_nonnull (inhibit);
    g_assert (CK_IS_INHIBIT (inhibit));
}

static void
test_ck_create_inhibit_lock (void)
{
    /* sanity checks */
    g_assert (fd == -1);
    g_assert (CK_IS_INHIBIT (inhibit));

    fd = ck_create_inhibit_lock (inhibit,
                                 WHO,
                                 WHAT,
                                 WHY);

    /* ensure we succeeded */
    g_assert (fd >= 0);
}

static void
test_which_are_inhibited (void)
{
    /* sanity checks */
    g_assert (fd != -1);
    g_assert (CK_IS_INHIBIT (inhibit));

    /* these should test false */
    g_assert_false (ck_inhibit_is_shutdown_inhibited      (inhibit));
    g_assert_false (ck_inhibit_is_power_key_inhibited     (inhibit));
    g_assert_false (ck_inhibit_is_suspend_key_inhibited   (inhibit));
    g_assert_false (ck_inhibit_is_hibernate_key_inhibited (inhibit));

    /* these should test true */
    g_assert (ck_inhibit_is_suspend_inhibited (inhibit));
    g_assert (ck_inhibit_is_idle_inhibited    (inhibit));
}

static void
test_close_inhibit_lock (void)
{
    /* do we have a fd? */
    g_assert (fd >= 0);

    /* does it properly close? */
    g_assert (g_close (fd, NULL));

    /* verify we can't close it again */
    g_assert_false (g_close (fd, NULL));

    fd = -1;
}

static void
test_unref_inhibit (void)
{
    /* Verify we have an inhibit object */
    g_assert_nonnull (inhibit);
    g_assert (CK_IS_INHIBIT (inhibit));

    g_object_unref (inhibit);
    inhibit = NULL;
}

static void
test_cleanup (void)
{
    gchar *named_pipe_path;

    named_pipe_path = g_strdup_printf ("%s/run/ConsoleKit/inhibit/%s",
                                       LOCALSTATEDIR,
                                       WHO);

    /* Verify inhibit cleaned up the inhibit named_pipe_path */
    g_assert_false (g_file_test (named_pipe_path, G_FILE_TEST_EXISTS));
}

int
main (int   argc,
      char *argv[])
{
    gchar *path;

    g_test_init (&argc, &argv, NULL);

    inhibit = NULL;
    fd = -1;

    path = g_strdup_printf ("%s/run/ConsoleKit/inhibit/%s",
                            LOCALSTATEDIR,
                            WHO);

    g_print ("tmp file will be written to: %s\n",
             path);

    /* If there's already a named_pipe_path remove it; don't care if this
     * fails */
    g_unlink (path);

    /* Create the inhibit object */
    g_test_add_func ("/inhibit/create", test_create_inhibit);

    g_test_add_func ("/inhibit/ck_create_inhibit_lock", test_ck_create_inhibit_lock);

    g_test_add_func ("/inhibit/which_are_inhibited", test_which_are_inhibited);

    g_test_add_func ("/inhibit/close_inhibit_lock", test_close_inhibit_lock);

    /* Release the inhibit object */
    g_test_add_func ("/inhibit/unref", test_unref_inhibit);

    /* Ensure the named_pipe_path file was removed */
    g_test_add_func ("/inhibit/cleanup", test_cleanup);

    return g_test_run();
}
