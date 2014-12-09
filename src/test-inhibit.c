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

#include <libintl.h>
#include <locale.h>

#include <glib.h>
#include <glib-object.h>
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <sys/types.h>

#include "ck-inhibit.h"
#include "ck-inhibit-manager.h"


static const gchar* WHO  = "test-inhibit";
static const gchar* WHAT = "sleep:idle";
static const gchar* WHY  = "for testing";

/* If this doesn't get escaped we won't have rights to write here */
static const gchar* WHO2  = "../../../../../root";
static const gchar* WHAT2 = "shutdown:sleep";

CkInhibitManager *manager;
CkInhibit *inhibit;
gint fd;

static void
test_create_inhibit (void)
{
    /* Let's not overwrite our object */
    g_assert (inhibit == NULL);

    inhibit = ck_inhibit_new ();

    /* Verify we got a valid inhibit object */
    g_assert (inhibit != NULL);
    g_assert (CK_IS_INHIBIT (inhibit));
}

static void
test_ck_create_inhibit_lock (void)
{
    /* sanity checks */
    g_assert (fd == -1);
    g_assert (CK_IS_INHIBIT (inhibit));

    fd = ck_inhibit_create_lock (inhibit,
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
    g_assert (ck_inhibit_is_shutdown_inhibited      (inhibit) == FALSE);
    g_assert (ck_inhibit_is_power_key_inhibited     (inhibit) == FALSE);
    g_assert (ck_inhibit_is_suspend_key_inhibited   (inhibit) == FALSE);
    g_assert (ck_inhibit_is_hibernate_key_inhibited (inhibit) == FALSE);

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
    g_assert (g_close (fd, NULL)  == FALSE);

    fd = -1;
}

static void
test_unref_inhibit (void)
{
    /* Verify we have an inhibit object */
    g_assert (inhibit != NULL);
    g_assert (CK_IS_INHIBIT (inhibit));

    g_object_unref (inhibit);
    inhibit = NULL;
}

static void
cb_changed_event (CkInhibitManager *manager, gint event, gboolean enabled, gpointer user_data)
{
    g_print ("cb_changed_event: event %d is now %s\n", event,
             enabled ? "TRUE" : "FALSE");
}

static void
test_manager_init (void)
{
    manager = ck_inhibit_manager_get ();
    g_assert (CK_IS_INHIBIT_MANAGER (manager));

    g_signal_connect (manager, "changed-event", G_CALLBACK (cb_changed_event), NULL);
}

static void
test_manager_add_lock1 (void)
{
    g_print ("\n");
    ck_inhibit_manager_create_lock (manager,
                                    WHO,
                                    WHAT,
                                    WHY);
}

static void
test_manager_add_lock2 (void)
{
    g_print ("\n");
    ck_inhibit_manager_create_lock (manager,
                                    WHO2,
                                    WHAT2,
                                    WHY);
}

static void
test_manager_which_are_inhibited (void)
{
    g_assert (ck_inhibit_manager_is_shutdown_inhibited (manager));
    g_assert (ck_inhibit_manager_is_suspend_inhibited  (manager));
    g_assert (ck_inhibit_manager_is_idle_inhibited     (manager));

    g_assert (ck_inhibit_manager_is_hibernate_key_inhibited (manager) == FALSE);
    g_assert (ck_inhibit_manager_is_suspend_key_inhibited   (manager) == FALSE);
    g_assert (ck_inhibit_manager_is_power_key_inhibited     (manager) == FALSE);

}

static void
test_manager_remove_lock1 (void)
{
    g_print ("\n");
    ck_inhibit_manager_remove_lock (manager, WHO);
}

static void
test_manager_remove_lock2 (void)
{
    g_print ("\n");
    ck_inhibit_manager_remove_lock (manager, WHO2);
}

static void
test_manager_unref (void)
{
    g_object_unref (manager);
}

int
main (int   argc,
      char *argv[])
{
    /* do not run these tests as root */
    if (getuid () == 0) {
            g_warning (_("You must NOT be root to run these tests"));
            exit (1);
    }

        /* Setup for i18n */
        setlocale(LC_ALL, "");

#ifdef ENABLE_NLS
	bindtextdomain(PACKAGE, LOCALEDIR);
	textdomain(PACKAGE);
#endif

    g_test_init (&argc, &argv, NULL);

    inhibit = NULL;
    fd = -1;

    /* Create the inhibit object */
    g_test_add_func ("/inhibit/create", test_create_inhibit);

    g_test_add_func ("/inhibit/ck_create_inhibit_lock", test_ck_create_inhibit_lock);

    g_test_add_func ("/inhibit/which_are_inhibited", test_which_are_inhibited);

    g_test_add_func ("/inhibit/close_inhibit_lock", test_close_inhibit_lock);

    /* Release the inhibit object */
    g_test_add_func ("/inhibit/unref", test_unref_inhibit);

    /* Now let's try using the inhibit manager */
    g_test_add_func ("/inhibit/test_manager_init", test_manager_init);
    g_test_add_func ("/inhibit/test_manager_add_lock1", test_manager_add_lock1);
    g_test_add_func ("/inhibit/test_manager_add_lock2", test_manager_add_lock2);
    g_test_add_func ("/inhibit/test_manager_which_are_inhibited", test_manager_which_are_inhibited);
    g_test_add_func ("/inhibit/test_manager_remove_lock1", test_manager_remove_lock1);
    g_test_add_func ("/inhibit/test_manager_remove_lock2", test_manager_remove_lock2);
    g_test_add_func ("/inhibit/test_manager_unref", test_manager_unref);

    return g_test_run();
}
