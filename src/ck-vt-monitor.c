/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006 William Jon McCann <mccann@jhu.edu>
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
#include <sys/ioctl.h>
#include <sys/vt.h>

#if defined (__linux__)
#include <linux/tty.h>
#include <linux/kd.h>
#endif /* linux */

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#define DBUS_API_SUBJECT_TO_CHANGE
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "ck-vt-monitor.h"
#include "ck-marshal.h"
#include "ck-debug.h"

#define ERROR -1
#define DEBUG_ENABLED 1

#define CK_VT_MONITOR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CK_TYPE_VT_MONITOR, CkVtMonitorPrivate))

struct CkVtMonitorPrivate
{
        int              vfd;
        GHashTable      *vt_thread_hash;
        guint            active_num;
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

G_DEFINE_TYPE (CkVtMonitor, ck_vt_monitor, G_TYPE_OBJECT)

static void watch_vts (CkVtMonitor *vt_monitor);

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

        res = ioctl (vt_monitor->priv->vfd, VT_ACTIVATE, num);
        if (res == 0) {
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
        g_return_val_if_fail (CK_IS_VT_MONITOR (vt_monitor), FALSE);

        if (num != NULL) {
                *num = vt_monitor->priv->active_num;
        }

        return TRUE;
}

static void
change_active_num (CkVtMonitor *vt_monitor,
                   guint        num)
{

        if (vt_monitor->priv->active_num != num) {

                vt_monitor->priv->active_num = num;

                g_hash_table_remove (vt_monitor->priv->vt_thread_hash, GUINT_TO_POINTER (num));
                watch_vts (vt_monitor);

                g_signal_emit (vt_monitor, signals[ACTIVE_CHANGED], 0, num);
        }
}

typedef struct {
        gint32       num;
        CkVtMonitor *vt_monitor;
} ThreadData;

static gboolean
vt_activated (ThreadData *data)
{
        change_active_num (data->vt_monitor, data->num);

        g_free (data);

        return FALSE;
}

static void *
vt_thread_start (ThreadData *data)
{
        CkVtMonitor *vt_monitor;
        int          ret;
        gint32       num;

        vt_monitor = data->vt_monitor;
        num = data->num;

 again:
        ck_debug ("VT_WAITACTIVE for vt %d", num);
        ret = ioctl (vt_monitor->priv->vfd, VT_WAITACTIVE, num);

        ck_debug ("VT_WAITACTIVE returned %d", ret);

        if (ret == ERROR) {

                if (errno == EINTR) {
                        ck_debug ("Interrupted waiting for native console %d activation: %s",
                                  num,
                                  g_strerror (errno));
                       goto again;
                } else {
                        ck_debug ("Error waiting for native console %d activation: %s",
                                   num,
                                   g_strerror (errno));

                        g_warning ("Error waiting for native console %d activation: %s",
                                   num,
                                   g_strerror (errno));
                }

                g_free (data);

                if (vt_monitor->priv->vt_thread_hash != NULL) {
                        g_hash_table_remove (vt_monitor->priv->vt_thread_hash, GUINT_TO_POINTER (num));
                }
        } else {
                g_idle_add ((GSourceFunc)vt_activated, data);
        }

        g_thread_exit (NULL);

        return NULL;
}

static guint
get_active_native (CkVtMonitor *vt_monitor)
{
        int            ret;
        struct vt_stat stat;

        ret = ioctl (vt_monitor->priv->vfd, VT_GETSTATE, &stat);
        if (ret == ERROR) {
                perror ("ioctl VT_GETSTATE");
                return -1;
        }

        {
                int i;

                ck_debug ("Current VT: tty%d", stat.v_active);
                for (i = 1; i <= 16; i++) {
                        gboolean is_on;
                        is_on = stat.v_state & (1 << i);

                        ck_debug ("VT %d:%s", i, is_on ? "on" : "off");
                }
        }

        return stat.v_active;
}

static void
watch_vts (CkVtMonitor *vt_monitor)
{
        int    i;
        gint32 current_num;

        current_num = vt_monitor->priv->active_num;

        for (i = 1; i < MAX_NR_CONSOLES; i++) {
                gpointer id;

                /* don't wait on the active vc */
                if (i == current_num) {
                        continue;
                }

                id = GINT_TO_POINTER (i);
                if (g_hash_table_lookup (vt_monitor->priv->vt_thread_hash, id) == NULL) {
                        GThread    *thread;
                        GError     *error;
                        ThreadData *data;

                        data = g_new0 (ThreadData, 1);
                        data->num = i;
                        data->vt_monitor = vt_monitor;

                        ck_debug ("Creating thread for vt %d", i);

                        error = NULL;
                        thread = g_thread_create_full ((GThreadFunc)vt_thread_start, data, 16384, FALSE, TRUE, G_THREAD_PRIORITY_NORMAL, &error);
                        if (thread == NULL) {
                                ck_debug ("Unable to create thread: %s", error->message);
                                g_error_free (error);
                        } else {
                                g_hash_table_insert (vt_monitor->priv->vt_thread_hash, id, thread);
                        }
                }
        }
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

extern int getfd (void);

static void
ck_vt_monitor_init (CkVtMonitor *vt_monitor)
{
        int fd;

        vt_monitor->priv = CK_VT_MONITOR_GET_PRIVATE (vt_monitor);

        fd = getfd ();
        if (fd == ERROR) {
                ck_debug ("Unable to open console: %s", g_strerror (errno));
                g_critical ("Unable to open console: %s", g_strerror (errno));
        }

        vt_monitor->priv->vfd = fd;
        vt_monitor->priv->vt_thread_hash = g_hash_table_new (g_direct_hash, g_direct_equal);

        vt_monitor->priv->active_num = get_active_native (vt_monitor);

        watch_vts (vt_monitor);
}

static void
ck_vt_monitor_finalize (GObject *object)
{
        CkVtMonitor *vt_monitor;

        g_return_if_fail (object != NULL);
        g_return_if_fail (CK_IS_VT_MONITOR (object));

        vt_monitor = CK_VT_MONITOR (object);

        g_return_if_fail (vt_monitor->priv != NULL);

        g_hash_table_destroy (vt_monitor->priv->vt_thread_hash);

        close (vt_monitor->priv->vfd);

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
