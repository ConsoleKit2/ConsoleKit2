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
#include "ck-marshal.h"


#define CK_INHIBIT_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CK_TYPE_INHIBIT_MANAGER, CkInhibitManagerPrivate))

struct CkInhibitManagerPrivate
{
         /* We could use something more complicated but
         * it's doubtful there will be more than a dozen items in the list.
         */
        GList *inhibit_list;
        /* inhibitors is an array of which events to suppress.
         * The CkInhibitEvent value is used to indicate how many different
         * inhibits are suppressing that event. */
        gint inhibitors[CK_INHIBIT_EVENT_LAST];
};

typedef enum {
        SIG_CHANGED_EVENT,
        SIG_N_SIGNALS,
} INHIBIT_SIGNALS;

static guint __signals[SIG_N_SIGNALS] = { 0, };


static void     ck_inhibit_manager_class_init  (CkInhibitManagerClass *klass);
static void     ck_inhibit_manager_init        (CkInhibitManager      *manager);
static void     ck_inhibit_manager_finalize    (GObject               *object);

G_DEFINE_TYPE (CkInhibitManager, ck_inhibit_manager, G_TYPE_OBJECT)


static void
ck_inhibit_manager_class_init (CkInhibitManagerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = ck_inhibit_manager_finalize;

        g_type_class_add_private (klass, sizeof (CkInhibitManagerPrivate));

        __signals[SIG_CHANGED_EVENT] = g_signal_new("changed-event",
                                                    G_OBJECT_CLASS_TYPE (object_class),
                                                    G_SIGNAL_RUN_LAST,
                                                    G_STRUCT_OFFSET (CkInhibitManagerClass, changed_event),
                                                    NULL, NULL,
                                                    ck_marshal_VOID__INT_BOOLEAN,
                                                    G_TYPE_NONE,
                                                    2, G_TYPE_INT, G_TYPE_BOOLEAN);
}

static void
ck_inhibit_manager_init (CkInhibitManager *manager)
{
        manager->priv = CK_INHIBIT_MANAGER_GET_PRIVATE (manager);
}

static void
ck_inhibit_manager_finalize (GObject *object)
{
        G_OBJECT_CLASS (ck_inhibit_manager_parent_class)->finalize (object);
}

static void
cb_changed_event (CkInhibit *inhibit, gint event, gboolean enabled, gpointer user_data)
{
        CkInhibitManager        *manager;
        CkInhibitManagerPrivate *priv;

        g_return_if_fail (CK_IS_INHIBIT_MANAGER (user_data));

        manager = CK_INHIBIT_MANAGER (user_data);

        priv = CK_INHIBIT_MANAGER_GET_PRIVATE (manager);

        if (event < 0 || event >= CK_INHIBIT_EVENT_LAST) {
                g_warning ("invalid event id");
                return;
        }

        if (enabled) {
                priv->inhibitors[event]++;

                if (priv->inhibitors[event] == 1) {
                        /* event is now inhibited, send a notification */
                        g_signal_emit(G_OBJECT (manager),
                                      __signals[SIG_CHANGED_EVENT],
                                      0,
                                      event,
                                      TRUE);
                }
        } else {
                priv->inhibitors[event]--;
                if (priv->inhibitors[event] < 0) {
                        g_warning ("cb_changed_event: priv->inhibitors[%d] "
                                   "is negative, that's not supposed to happen",
                                   event);
                }

                if (priv->inhibitors[event] == 0) {
                        /* event is no longer inhibited, send a notification */
                        g_signal_emit(G_OBJECT (manager),
                                      __signals[SIG_CHANGED_EVENT],
                                      0,
                                      event,
                                      FALSE);
                }
        }
}

/**
 * ck_inhibit_manager_create_lock:
 * @manager: The @CkInhibitManager object
 * @who:  A human-readable, descriptive string of who is taking
 *        the lock. Example: "Xfburn"
 * @what: What is a colon-separated list of lock types.
 *        The list of lock types are: shutdown, sleep, idle,
 *        handle-power-key, handle-suspend-key, handle-hibernate-key.
 *        Example: "shutdown:idle"
 * @why:  A human-readable, descriptive string of why the program
 *        is taking the lock. Example: "Burning a DVD, interrupting now
 *        will ruin the DVD."
 *
 * Initializes an inhibit lock with the supplied paramters and returns
 * the named pipe. An application can only hold one lock at a time, multiple
 * calls will fail.
 *
 * Return value: The named pipe (a file descriptor) on success.
 *               This is a value of 0 or greater.
 *               Returns a CkInhbitError on failure.
 **/
gint
ck_inhibit_manager_create_lock (CkInhibitManager *manager,
                                const gchar *who,
                                const gchar *what,
                                const gchar *why)
{
        CkInhibitManagerPrivate *priv;
        CkInhibit               *inhibit;
        gint                     fd, signal_id;

        g_return_val_if_fail (CK_IS_INHIBIT_MANAGER (manager), CK_INHIBIT_ERROR_GENERAL);

        priv = CK_INHIBIT_MANAGER_GET_PRIVATE (manager);

        inhibit = ck_inhibit_new ();

        if (inhibit == NULL) {
                g_error ("error creating new inhibit object");
                return CK_INHIBIT_ERROR_OOM;
        }

        /* add our signal handler before we create the lock so we get
         * the inhibit enable signals.
         */
        signal_id = g_signal_connect (inhibit, "changed-event", G_CALLBACK (cb_changed_event), manager);

        fd = ck_inhibit_create_lock (inhibit, who, what, why);

        if (fd == -1) {
                g_error ("error creating inhibit lock");
                /* ensure we disconnect the signal handler and
                 * unref the inhibit object we won't be using */
                g_signal_handler_disconnect (inhibit, signal_id);
                g_object_unref (inhibit);
                return CK_INHIBIT_ERROR_GENERAL;
        }

        /* Add it to our list */
        priv->inhibit_list = g_list_append (priv->inhibit_list,
                                            inhibit);

        return fd;
}

/**
 * ck_inhibit_manager_remove_lock:
 * @manager: The @CkInhibitManager object
 * @who:  A human-readable, descriptive string of who has taken
 *        the lock. Example: "Xfburn"
 *
 * Finds the inhibit lock @who and removes it.
 *
 * Return value: TRUE on successful removal.
 **/
gboolean
ck_inhibit_manager_remove_lock (CkInhibitManager *manager,
                                const gchar      *who)
{
        CkInhibitManagerPrivate *priv;
        GList                   *l;

        g_return_val_if_fail (CK_IS_INHIBIT_MANAGER (manager), FALSE);

        priv = CK_INHIBIT_MANAGER_GET_PRIVATE (manager);

        for (l = g_list_first (priv->inhibit_list); l != NULL; l = g_list_next (priv->inhibit_list)) {
                if (l->data && g_strcmp0 (ck_inhibit_get_who (l->data), who) == 0) {
                        CkInhibit *inhibit = l->data;

                        /* Found it! Remove it from the list and unref the object */
                        priv->inhibit_list = g_list_remove (priv->inhibit_list, inhibit);
                        ck_inhibit_remove_lock (inhibit);
                        g_signal_handlers_disconnect_by_func (inhibit,
                                                              G_CALLBACK (cb_changed_event),
                                                              manager);
                        g_object_unref (inhibit);
                        return TRUE;
                }
        }

        return FALSE;
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
 * @manager: The @CkInhibitManager object
 *
 * Return value: TRUE is inhibited.
 **/
gboolean
ck_inhibit_manager_is_shutdown_inhibited (CkInhibitManager *manager)
{
        g_return_val_if_fail (CK_IS_INHIBIT_MANAGER (manager), FALSE);

        return manager->priv->inhibitors[CK_INHIBIT_EVENT_SHUTDOWN] > 0 ? TRUE : FALSE;
}

/**
 * ck_inhibit_manager_is_suspend_inhibited:
 * @manager: The @CkInhibitManager object
 *
 * Return value: TRUE is inhibited.
 **/
gboolean
ck_inhibit_manager_is_suspend_inhibited (CkInhibitManager *manager)
{
        g_return_val_if_fail (CK_IS_INHIBIT_MANAGER (manager), FALSE);

        return manager->priv->inhibitors[CK_INHIBIT_EVENT_SUSPEND] > 0 ? TRUE : FALSE;
}

/**
 * ck_inhibit_manager_is_idle_inhibited:
 * @manager: The @CkInhibitManager object
 *
 * Return value: TRUE is inhibited.
 **/
gboolean
ck_inhibit_manager_is_idle_inhibited (CkInhibitManager *manager)
{
        g_return_val_if_fail (CK_IS_INHIBIT_MANAGER (manager), FALSE);

        return manager->priv->inhibitors[CK_INHIBIT_EVENT_IDLE] > 0 ? TRUE : FALSE;
}

/**
 * ck_inhibit_manager_is_power_key_inhibited:
 * @manager: The @CkInhibitManager object
 *
 * Return value: TRUE is inhibited.
 **/
gboolean
ck_inhibit_manager_is_power_key_inhibited (CkInhibitManager *manager)
{
        g_return_val_if_fail (CK_IS_INHIBIT_MANAGER (manager), FALSE);

        return manager->priv->inhibitors[CK_INHIBIT_EVENT_POWER_KEY] > 0 ? TRUE : FALSE;
}

/**
 * ck_inhibit_manager_is_suspend_key_inhibited:
 * @manager: The @CkInhibitManager object
 *
 * Return value: TRUE is inhibited.
 **/
gboolean
ck_inhibit_manager_is_suspend_key_inhibited (CkInhibitManager *manager)
{
        g_return_val_if_fail (CK_IS_INHIBIT_MANAGER (manager), FALSE);

        return manager->priv->inhibitors[CK_INHIBIT_EVENT_SUSPEND_KEY] > 0 ? TRUE : FALSE;
}

/**
 * ck_inhibit_manager_is_hibernate_key_inhibited:
 * @manager: The @CkInhibitManager object
 *
 * Return value: TRUE is inhibited.
 **/
gboolean
ck_inhibit_manager_is_hibernate_key_inhibited (CkInhibitManager *manager)
{
        g_return_val_if_fail (CK_IS_INHIBIT_MANAGER (manager), FALSE);

        return manager->priv->inhibitors[CK_INHIBIT_EVENT_HIBERNATE_KEY] > 0 ? TRUE : FALSE;
}
