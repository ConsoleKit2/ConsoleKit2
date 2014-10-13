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

#include "ck-inhibit.h"

#define CK_INHIBIT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CK_TYPE_INHIBIT, CkInhibitPrivate))

struct CkInhibitPrivate
{
        CkManager   *manager;
        /*
         * Who is a human-readable, descriptive string of who is taking
         * the lock. Example: "Xfburn"
         * We use this as unique identifier for the lock since an app
         * isn't supposed to have multiple locks.
         */
        const gchar *who;
        /*
         * What is a colon-separated list of lock types.
         * The list of lock types are: shutdown, sleep, idle,
         * handle-power-key, handle-suspend-key, handle-hibernate-key.
         * Example: "shutdown:idle"
         */
        const gchar *what;
        /*
         * Why is a human-readable, descriptive string of why the program
         * is taking the lock. Example: "Burning a DVD, interrupting now
         * will ruin the DVD."
         */
        const gchar *why;
        /*
         * fd is a named pipe that the user app will hold onto while they
         * want the lock to be held. When they close all references to
         * the fd then the lock is released and this object can be
         * destroyed.
         */
        gint         fd;
        /*
         * state file is used to store information on the lock so that
         * duplicates can be prevented.
         */
        gchar       *state_file;
};

static void     ck_inhibit_class_init  (CkInhibitClass *klass);
static void     ck_inhibit_init        (CkInhibit      *inhibit);
static void     ck_inhibit_finalize    (GObject        *object);

G_DEFINE_TYPE (CkInhibit, ck_inhibit, G_TYPE_OBJECT)


static void
ck_inhibit_class_init (CkInhibitClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = ck_inhibit_finalize;

        g_type_class_add_private (klass, sizeof (CkInhibitPrivate));
}

static void
ck_inhibit_init (CkInhibit *monitor)
{
        monitor->priv = CK_INHIBIT_GET_PRIVATE (monitor);
}

static void
ck_inhibit_finalize (GObject *object)
{
        CkInhibit *monitor;

        g_return_if_fail (object != NULL);
        g_return_if_fail (CK_IS_INHIBIT (object));

        monitor = CK_INHIBIT (object);

        g_return_if_fail (monitor->priv != NULL);

        G_OBJECT_CLASS (ck_inhibit_parent_class)->finalize (object);
}

/*
 * Creates the LOCALSTATEDIR /run/ConsoleKit/inhibit directory. The
 * state_files will exist inside this directory.
 * Returns TRUE on success.
 */
static gboolean
create_inhibit_base_directory (void)
{
        errno = 0;
        res = g_mkdir_with_parents (LOCALSTATEDIR "/run/ConsoleKit/inhibit",
                                    S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
        if (res < 0) {
                g_warning ("Unable to create directory %s (%s)",
                           LOCALSTATEDIR "/run/ConsoleKit/inhibit",
                           g_strerror (errno));

                return FALSE;
        }

        return TRUE;
}

/*
 * Initializes the lock fd and populates the inhibit object with data.
 * Returns the fd on success which is a value of 0 or greater.
 * Returns a CkInhbitError on failure.
 */
gint
ck_create_inhibit_lock (CkManager   *manager,
                        CkInhibit   *inhibit,
                        const gchar *who,
                        const gchar *what,
                        const gchar *why)
{
        CkInhibitPrivate *priv;

        g_return_val_if_fail (inhibit, CK_INHIBIT_ERROR_INVALID_INPUT);
        g_return_val_if_fail (manager, CK_INHIBIT_ERROR_INVALID_INPUT);

        /* These fields only get set here and are mandatory */
        if (!who || !what || !why) {
                g_warning ("who, what, and why and mandatory for inhibit locks");
                return CK_INHIBIT_ERROR_INVALID_INPUT;
        }

        priv = CK_INHIBIT_GET_PRIVATE (inhibit);

        priv->who = who;
        priv->what = what;
        priv->why = why;
        priv->state_file = g_strdup_printf (LOCALSTATEDIR "/run/ConsoleKit/inhibit/%s", who);

        if(priv->state_file == NULL) {
                g_warning ("Failed to allocate memory for inhibit state_file string");
                return CK_INHIBIT_ERROR_OOM;
        }

        /* always make sure we have a directory to work in */
        if (create_inhibit_base_directory () < 0) {
                return CK_INHIBIT_ERROR_GENERAL;
        }
}
