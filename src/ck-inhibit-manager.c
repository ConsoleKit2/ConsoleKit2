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
#include "ck-inhibit-manager.h"


#define CK_INHIBIT_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CK_TYPE_INHIBIT_MANAGER, CkInhibitManagerPrivate))

struct CkInhibitManagerPrivate
{
        GList *inhibit_list;
        /* inhibitors is an array of which events to suppress.
         * The CkInhibitEvent value is used to indicate how many different
         * inhibits are suppressing that event. */
        gint inhibitors[CK_INHIBIT_EVENT_LAST];
};

static void     ck_inhibit_manager_class_init  (CkInhibitManagerClass *klass);
static void     ck_inhibit_manager_init        (CkInhibitManager      *inhibit);
static void     ck_inhibit_manager_finalize    (GObject               *object);

G_DEFINE_TYPE (CkInhibitManager, ck_inhibit_manager, G_TYPE_OBJECT)


static void
ck_inhibit_manager_class_init (CkInhibitManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = ck_inhibit_manager_finalize;

        g_type_class_add_private (klass, sizeof (CkInhibitManagerPrivate));
}

static void
ck_inhibit_manager_init (CkInhibitManager *manager)
{
}

static void
ck_inhibit_manager_finalize (GObject *object)
{
        G_OBJECT_CLASS (ck_inhibit_manager_parent_class)->finalize (object);
}

/**
 * ck_inhibit_manager_get:
 *
 * Increases the reference count of the @CkInhibitManager object.
 *
 * Return value:  Returns a reference to the CkInhibitManager object or
 *                NULL on failure.
 **/
CkInhibitManager*
ck_inhibit_manager_get (void)
{
        static GObject *manager = NULL;

        if (manager != NULL) {
                g_object_ref (manager);
        } else {
                manager = g_object_new (CK_TYPE_INHIBIT_MANAGER, NULL);

                g_object_add_weak_pointer (manager,
                                           (gpointer *) &manager);
        }

        return CK_INHIBIT_MANAGER (manager);
}

/**
 * ck_inhibit_manager_is_shutdown_inhibited:
 *
 * Return value: TRUE is inhibited.
 **/
gboolean
ck_inhibit_manager_is_shutdown_inhibited (void)
{
        CkInhibitManager *manager = ck_inhibit_manager_get ();

        return manager->priv->inhibitors[CK_INHIBIT_EVENT_SHUTDOWN] > 0 ? TRUE : FALSE;
}

/**
 * ck_inhibit_manager_is_suspend_inhibited:
 *
 * Return value: TRUE is inhibited.
 **/
gboolean
ck_inhibit_manager_is_suspend_inhibited (void)
{
        CkInhibitManager *manager = ck_inhibit_manager_get ();

        return manager->priv->inhibitors[CK_INHIBIT_EVENT_SUSPEND] > 0 ? TRUE : FALSE;
}

/**
 * ck_inhibit_manager_is_idle_inhibited:
 *
 * Return value: TRUE is inhibited.
 **/
gboolean
ck_inhibit_manager_is_idle_inhibited (void)
{
        CkInhibitManager *manager = ck_inhibit_manager_get ();

        return manager->priv->inhibitors[CK_INHIBIT_EVENT_IDLE] > 0 ? TRUE : FALSE;
}

/**
 * ck_inhibit_manager_is_power_key_inhibited:
 *
 * Return value: TRUE is inhibited.
 **/
gboolean
ck_inhibit_manager_is_power_key_inhibited (void)
{
        CkInhibitManager *manager = ck_inhibit_manager_get ();

        return manager->priv->inhibitors[CK_INHIBIT_EVENT_POWER_KEY] > 0 ? TRUE : FALSE;
}

/**
 * ck_inhibit_manager_is_suspend_key_inhibited:
 *
 * Return value: TRUE is inhibited.
 **/
gboolean
ck_inhibit_manager_is_suspend_key_inhibited (void)
{
        CkInhibitManager *manager = ck_inhibit_manager_get ();

        return manager->priv->inhibitors[CK_INHIBIT_EVENT_SUSPEND_KEY] > 0 ? TRUE : FALSE;
}

/**
 * ck_inhibit_manager_is_hibernate_key_inhibited:
 *
 * Return value: TRUE is inhibited.
 **/
gboolean
ck_inhibit_manager_is_hibernate_key_inhibited (void)
{
        CkInhibitManager *manager = ck_inhibit_manager_get ();

        return manager->priv->inhibitors[CK_INHIBIT_EVENT_HIBERNATE_KEY] > 0 ? TRUE : FALSE;
}
