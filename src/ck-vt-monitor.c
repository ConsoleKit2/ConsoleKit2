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
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "ck-vt-monitor.h"
#include "ck-sysdeps.h"
#include "ck-marshal.h"

#if defined (__sun) && defined (HAVE_SYS_VT_H)
#include <sys/vt.h>
#include <signal.h>
#include <stropts.h>
#endif

#ifndef ERROR
#define ERROR -1
#endif

#define CK_VT_MONITOR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CK_TYPE_VT_MONITOR, CkVtMonitorPrivate))

struct CkVtMonitorPrivate
{
        int              vfd;
        GHashTable      *vt_thread_hash;
        guint            active_num;

        GAsyncQueue     *event_queue;
        guint            process_queue_id;
};

enum {
        ACTIVE_CHANGED,
        LAST_SIGNAL
};

enum {
        PROP_0,
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     ck_vt_monitor_class_init  (CkVtMonitorClass *klass);
static void     ck_vt_monitor_init        (CkVtMonitor      *vt_monitor);
static void     ck_vt_monitor_finalize    (GObject          *object);

static void     vt_add_watches            (CkVtMonitor      *vt_monitor);

G_DEFINE_TYPE (CkVtMonitor, ck_vt_monitor, G_TYPE_OBJECT)

G_LOCK_DEFINE_STATIC (hash_lock);
G_LOCK_DEFINE_STATIC (schedule_lock);

static gpointer vt_object = NULL;

GQuark
ck_vt_monitor_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("ck_vt_monitor_error");
        }

        return ret;
}

gboolean
ck_vt_monitor_set_active (CkVtMonitor    *vt_monitor,
                          guint32         num,
                          GError        **error)
{
        gboolean ret;
        int      res;

        g_return_val_if_fail (CK_IS_VT_MONITOR (vt_monitor), FALSE);

        if (num == vt_monitor->priv->active_num) {
                g_set_error (error,
                             CK_VT_MONITOR_ERROR,
                             CK_VT_MONITOR_ERROR_GENERAL,
                             _("Session is already active"));
                return FALSE;
        }

        if (vt_monitor->priv->vfd == ERROR) {
                g_set_error (error,
                             CK_VT_MONITOR_ERROR,
                             CK_VT_MONITOR_ERROR_GENERAL,
                             _("No consoles available"));
                return FALSE;
        }

        res = ck_activate_console_num (vt_monitor->priv->vfd, num);
        if (res) {
                ret = TRUE;
        } else {
                g_set_error (error,
                             CK_VT_MONITOR_ERROR,
                             CK_VT_MONITOR_ERROR_GENERAL,
                             _("Unable to activate session"));
                ret = FALSE;
        }

        return ret;
}

gboolean
ck_vt_monitor_get_active (CkVtMonitor    *vt_monitor,
                          guint32        *num,
                          GError        **error)
{
        if (num != NULL) {
                *num = 0;
        }

        g_return_val_if_fail (CK_IS_VT_MONITOR (vt_monitor), FALSE);

        if (vt_monitor->priv->vfd == ERROR) {
                g_set_error (error,
                             CK_VT_MONITOR_ERROR,
                             CK_VT_MONITOR_ERROR_GENERAL,
                             _("No consoles available"));
                return FALSE;
        }

        if (num != NULL) {
                *num = vt_monitor->priv->active_num;
        }

        return TRUE;
}

#if defined (__sun) && defined (HAVE_SYS_VT_H)
static void
handle_vt_active (void)
{
        struct vt_stat state;
        guint          num;
        CkVtMonitor   *vt_monitor = CK_VT_MONITOR (vt_object);

        g_return_if_fail (CK_IS_VT_MONITOR (vt_monitor));

      /*
	 * state.v_active value: [1 .. N]
	 *
	 * VT device file	 VT #
	 *
	 * /dev/console		--- VT #1
	 * /dev/vt/2		--- VT #2
	 * /dev/vt/3		--- VT #3
	 * /dev/vt/N		--- VT #4
	 */
        if (ioctl (vt_monitor->priv->vfd, VT_GETSTATE, &state) != -1) {
                num = state.v_active;
        } else {
                g_debug ("Fails to ioctl VT_GETSTATE");
        }

        if (vt_monitor->priv->active_num != num) {
                g_debug ("Changing active VT: %d", num);

                vt_monitor->priv->active_num = num;

                g_signal_emit (vt_monitor, signals [ACTIVE_CHANGED], 0, num);
        } else {
                g_debug ("VT activated but already active: %d", num);
        }
}
#endif

static void
change_active_num (CkVtMonitor *vt_monitor,
                   guint        num)
{

        if (vt_monitor->priv->active_num != num) {
                g_debug ("Changing active VT: %d", num);

                vt_monitor->priv->active_num = num;

                /* add a watch to every vt without a thread */
                vt_add_watches (vt_monitor);

                g_signal_emit (vt_monitor, signals[ACTIVE_CHANGED], 0, num);
        } else {
                g_debug ("VT activated but already active: %d", num);
        }
}

typedef struct {
        gint32       num;
        CkVtMonitor *vt_monitor;
} ThreadData;

typedef struct {
        gint32       num;
} EventData;

static void
thread_data_free (ThreadData *data)
{
        if (data == NULL) {
                return;
        }

        g_free (data);
}

static void
event_data_free (EventData *data)
{
        if (data == NULL) {
                return;
        }

        g_free (data);
}

static gboolean
process_queue (CkVtMonitor *vt_monitor)
{
        int        i;
        int        queue_length;
        EventData *data;
        EventData *d;

        g_async_queue_lock (vt_monitor->priv->event_queue);

        g_debug ("Processing VT event queue");

        queue_length = g_async_queue_length_unlocked (vt_monitor->priv->event_queue);
        data = NULL;

        G_LOCK (hash_lock);

        /* compress events in the queue */
        for (i = 0; i < queue_length; i++) {
                d = g_async_queue_try_pop_unlocked (vt_monitor->priv->event_queue);
                if (d == NULL) {
                        continue;

                }

                if (data != NULL) {
                        g_debug ("Compressing queue; skipping event for VT %d", data->num);
                        event_data_free (data);
                }

                data = d;
        }

        G_UNLOCK (hash_lock);

        if (data != NULL) {
                change_active_num (vt_monitor, data->num);
                event_data_free (data);
        }

        G_LOCK (schedule_lock);
        vt_monitor->priv->process_queue_id = 0;
        G_UNLOCK (schedule_lock);

        g_async_queue_unlock (vt_monitor->priv->event_queue);

        return FALSE;
}

static void
schedule_process_queue (CkVtMonitor *vt_monitor)
{
        G_LOCK (schedule_lock);
        if (vt_monitor->priv->process_queue_id == 0) {
                vt_monitor->priv->process_queue_id = g_idle_add ((GSourceFunc)process_queue, vt_monitor);
        }
        G_UNLOCK (schedule_lock);
}

static void *
vt_thread_start (ThreadData *data)
{
        CkVtMonitor *vt_monitor;
        gboolean     res;
        gint32       num;

        vt_monitor = data->vt_monitor;
        num = data->num;

        res = ck_wait_for_active_console_num (vt_monitor->priv->vfd, num);
        if (! res) {
                /* FIXME: what do we do if it fails? */
        } else {
                EventData *event;

                /* add event to queue */
                event = g_new0 (EventData, 1);
                event->num = num;
                g_debug ("Pushing activation event for VT %d onto queue", num);

                g_async_queue_push (vt_monitor->priv->event_queue, event);

                /* schedule processing of queue */
                schedule_process_queue (vt_monitor);
        }

        G_LOCK (hash_lock);
        if (vt_monitor->priv->vt_thread_hash != NULL) {
                g_hash_table_remove (vt_monitor->priv->vt_thread_hash, GUINT_TO_POINTER (num));
        }
        G_UNLOCK (hash_lock);

        g_thread_exit (NULL);
        thread_data_free (data);

        return NULL;
}

static void
vt_add_watch_unlocked (CkVtMonitor *vt_monitor,
                       gint32       num)
{
        GThread    *thread;
        GError     *error;
        ThreadData *data;
        gpointer    id;

        data = g_new0 (ThreadData, 1);
        data->num = num;
        data->vt_monitor = vt_monitor;

        g_debug ("Creating thread for vt %d", num);

        id = GINT_TO_POINTER (num);

        error = NULL;
        thread = g_thread_create_full ((GThreadFunc)vt_thread_start, data, 65536, FALSE, TRUE, G_THREAD_PRIORITY_NORMAL, &error);
        if (thread == NULL) {
                g_debug ("Unable to create thread: %s", error->message);
                g_error_free (error);
        } else {
                g_hash_table_insert (vt_monitor->priv->vt_thread_hash, id, thread);
        }
}

static void
vt_add_watches (CkVtMonitor *vt_monitor)
{
        guint  max_consoles;
        int    i;
        gint32 current_num;

#if defined (__sun) && !defined (HAVE_SYS_VT_H)
        /* Best to do nothing if VT is not supported */
#elif defined (__sun) && defined (HAVE_SYS_VT_H)
        /*
         * Solaris supports synchronous event notification in STREAMS.
         * Applications that open the virtual console device  can
         * get a asynchronous notification of VT switching by setting
         * the S_MSG flag in an I_SETSIG STREAMS ioctl. Such processes
         * receive a SIGPOLL signal when a VT switching succeeds.
         */
        struct sigaction act;
        act.sa_handler = handle_vt_active;
        sigaction (SIGPOLL, &act, NULL);

        ioctl (vt_monitor->priv->vfd, I_SETSIG, S_MSG);
#else
        G_LOCK (hash_lock);

        current_num = vt_monitor->priv->active_num;

        max_consoles = 1;

        if (! ck_get_max_num_consoles (&max_consoles)) {
                /* FIXME: this can fail on solaris and freebsd */
        }

        for (i = 1; i < max_consoles; i++) {
                gpointer id;

                /* don't wait on the active vc */
                if (i == current_num) {
                        continue;
                }

                id = GINT_TO_POINTER (i);

                /* add a watch to all other VTs that don't have threads */
                if (g_hash_table_lookup (vt_monitor->priv->vt_thread_hash, id) == NULL) {
                        vt_add_watch_unlocked (vt_monitor, i);
                }
        }

        G_UNLOCK (hash_lock);
#endif
}

static void
ck_vt_monitor_class_init (CkVtMonitorClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = ck_vt_monitor_finalize;

        signals [ACTIVE_CHANGED] = g_signal_new ("active-changed",
                                                 G_TYPE_FROM_CLASS (object_class),
                                                 G_SIGNAL_RUN_LAST,
                                                 G_STRUCT_OFFSET (CkVtMonitorClass, active_changed),
                                                 NULL,
                                                 NULL,
                                                 g_cclosure_marshal_VOID__UINT,
                                                 G_TYPE_NONE,
                                                 1, G_TYPE_UINT);

        g_type_class_add_private (klass, sizeof (CkVtMonitorPrivate));
}

static void
ck_vt_monitor_init (CkVtMonitor *vt_monitor)
{
        int fd;

        vt_monitor->priv = CK_VT_MONITOR_GET_PRIVATE (vt_monitor);

        fd = ck_get_a_console_fd ();
        vt_monitor->priv->vfd = fd;

        if (fd == ERROR) {
                const char *errmsg;
                errmsg = g_strerror (errno);
                g_warning ("Unable to open a console: %s", errmsg);
        } else {
                gboolean res;
                guint    active;

                res = ck_get_active_console_num (fd, &active);
                if (! res) {
                        /* FIXME: handle failure */
                        g_warning ("Could not determine active console");
                        active = 0;
                }

                vt_monitor->priv->active_num = active;
                vt_monitor->priv->event_queue = g_async_queue_new ();
                vt_monitor->priv->vt_thread_hash = g_hash_table_new (g_direct_hash, g_direct_equal);

                vt_add_watches (vt_monitor);
        }
}

static void
ck_vt_monitor_finalize (GObject *object)
{
        CkVtMonitor *vt_monitor;

        g_return_if_fail (object != NULL);
        g_return_if_fail (CK_IS_VT_MONITOR (object));

        vt_monitor = CK_VT_MONITOR (object);

        g_return_if_fail (vt_monitor->priv != NULL);

        if (vt_monitor->priv->process_queue_id > 0) {
                g_source_remove (vt_monitor->priv->process_queue_id);
        }

        if (vt_monitor->priv->event_queue != NULL) {
                g_async_queue_unref (vt_monitor->priv->event_queue);
        }

        if (vt_monitor->priv->vt_thread_hash != NULL) {
                g_hash_table_destroy (vt_monitor->priv->vt_thread_hash);
        }

        if (vt_monitor->priv->vfd != ERROR) {
                close (vt_monitor->priv->vfd);
        }

        G_OBJECT_CLASS (ck_vt_monitor_parent_class)->finalize (object);
}

CkVtMonitor *
ck_vt_monitor_new (void)
{
        if (vt_object != NULL) {
                g_object_ref (vt_object);
        } else {
                vt_object = g_object_new (CK_TYPE_VT_MONITOR, NULL);

                g_object_add_weak_pointer (vt_object,
                                           (gpointer *) &vt_object);
        }

        return CK_VT_MONITOR (vt_object);
}
