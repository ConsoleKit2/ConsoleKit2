/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 William Jon McCann <mccann@jhu.edu>
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
#include <signal.h>
#include <errno.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include "ck-file-monitor.h"
#include "ck-tty-idle-monitor.h"

#define CK_TTY_IDLE_MONITOR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CK_TYPE_TTY_IDLE_MONITOR, CkTtyIdleMonitorPrivate))

#define DEFAULT_THRESHOLD_SECONDS 30

struct CkTtyIdleMonitorPrivate
{
        char            *device;
        guint            threshold;

        guint            timeout_id;
        gboolean         idle_hint;
        GTimeVal         idle_since_hint;

        CkFileMonitor   *file_monitor;
        guint            file_notify_id;
};

enum {
        IDLE_HINT_CHANGED,
        LAST_SIGNAL
};

enum {
        PROP_0,
        PROP_DEVICE,
        PROP_THRESHOLD,
};

static guint signals [LAST_SIGNAL] = { 0, };

static void     ck_tty_idle_monitor_class_init  (CkTtyIdleMonitorClass *klass);
static void     ck_tty_idle_monitor_init        (CkTtyIdleMonitor      *tty_idle_monitor);
static void     ck_tty_idle_monitor_finalize    (GObject               *object);

static void     schedule_tty_check              (CkTtyIdleMonitor      *monitor,
                                                 guint                  seconds);

G_DEFINE_TYPE (CkTtyIdleMonitor, ck_tty_idle_monitor, G_TYPE_OBJECT)

GQuark
ck_tty_idle_monitor_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("ck_tty_idle_monitor_error");
        }

        return ret;
}

static gboolean
tty_idle_monitor_set_idle_hint_internal (CkTtyIdleMonitor *tty_idle_monitor,
                                         gboolean          idle_hint)
{
        if (tty_idle_monitor->priv->idle_hint != idle_hint) {
                tty_idle_monitor->priv->idle_hint = idle_hint;

                g_debug ("Emitting idle-changed for tty_idle_monitor %s: %d", tty_idle_monitor->priv->device, idle_hint);
                g_signal_emit (tty_idle_monitor, signals [IDLE_HINT_CHANGED], 0, idle_hint);
                return TRUE;
        }

        return FALSE;
}

static void
ck_tty_idle_monitor_set_device (CkTtyIdleMonitor *monitor,
                                const char       *device)
{
        g_return_if_fail (CK_IS_TTY_IDLE_MONITOR (monitor));

        g_free (monitor->priv->device);
        monitor->priv->device = g_strdup (device);
}

void
ck_tty_idle_monitor_set_threshold (CkTtyIdleMonitor *monitor,
                                   guint             threshold)
{
        g_return_if_fail (CK_IS_TTY_IDLE_MONITOR (monitor));

        monitor->priv->threshold = threshold;
}

static void
ck_tty_idle_monitor_set_property (GObject            *object,
                                  guint               prop_id,
                                  const GValue       *value,
                                  GParamSpec         *pspec)
{
        CkTtyIdleMonitor *self;

        self = CK_TTY_IDLE_MONITOR (object);

        switch (prop_id) {
        case PROP_DEVICE:
                ck_tty_idle_monitor_set_device (self, g_value_get_string (value));
                break;
        case PROP_THRESHOLD:
                ck_tty_idle_monitor_set_threshold (self, g_value_get_uint (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
ck_tty_idle_monitor_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
        CkTtyIdleMonitor *self;

        self = CK_TTY_IDLE_MONITOR (object);

        switch (prop_id) {
        case PROP_DEVICE:
                g_value_set_string (value, self->priv->device);
                break;
        case PROP_THRESHOLD:
                g_value_set_uint (value, self->priv->threshold);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
remove_idle_hint_timeout (CkTtyIdleMonitor *tty_idle_monitor)
{
        if (tty_idle_monitor->priv->timeout_id > 0) {
                g_source_remove (tty_idle_monitor->priv->timeout_id);
        }
}

static void
file_access_cb (CkFileMonitor      *file_monitor,
                CkFileMonitorEvent  event,
                const char         *path,
                CkTtyIdleMonitor   *monitor)
{
        g_debug ("File access callback for %s", path);

        tty_idle_monitor_set_idle_hint_internal (monitor, FALSE);

        /* this resets timers */
        ck_tty_idle_monitor_start (monitor);
}

static gboolean
monitor_add_watch (CkTtyIdleMonitor *monitor)
{
        if (monitor->priv->file_monitor == NULL) {
                monitor->priv->file_monitor = ck_file_monitor_new ();
        }
        monitor->priv->file_notify_id = ck_file_monitor_add_notify (monitor->priv->file_monitor,
                                                                    monitor->priv->device,
                                                                    CK_FILE_MONITOR_EVENT_ACCESS,
                                                                    (CkFileMonitorNotifyFunc)file_access_cb,
                                                                    monitor);
        return monitor->priv->file_notify_id > 0;
}

static gboolean
monitor_remove_watch (CkTtyIdleMonitor *monitor)
{
        if (monitor->priv->file_monitor != NULL &&
            monitor->priv->file_notify_id > 0) {
                g_debug ("Removing notify");
                ck_file_monitor_remove_notify (monitor->priv->file_monitor,
                                               monitor->priv->file_notify_id);
        }

        return FALSE;
}

static gboolean
check_tty_idle (CkTtyIdleMonitor *monitor)
{
        struct stat sb;
        gboolean    is_idle;
        gboolean    changed;
        time_t      now;
        time_t      idletime;
        time_t      last_access;

        if (monitor->priv->device == NULL) {
                return FALSE;
        }

        if (g_stat (monitor->priv->device, &sb) < 0) {
                g_debug ("Unable to stat: %s: %s", monitor->priv->device, g_strerror (errno));
                return FALSE;
        }

        last_access = sb.st_atime;

        time (&now);
        if (last_access > now) {
                last_access = now;
        }

        idletime = now - last_access;
        is_idle = (idletime >= monitor->priv->threshold);

        changed = tty_idle_monitor_set_idle_hint_internal (monitor, is_idle);

        monitor->priv->timeout_id = 0;

        if (is_idle) {
                if (! monitor_add_watch (monitor)) {
                        /* if we can't add a watch just add a new timer */
                        g_debug ("Couldn't add watch: rescheduling check for %u sec", monitor->priv->threshold);
                        schedule_tty_check (monitor, monitor->priv->threshold);
                }
        } else {
                guint remaining;
                remaining = monitor->priv->threshold - idletime;
                if (remaining > 0) {
                        g_debug ("Time left, rescheduling check for %u sec", remaining);
                        /* reschedule for time left */
                        schedule_tty_check (monitor, remaining);
                } else {
                        g_debug ("restarting check for %u sec", monitor->priv->threshold);
                        schedule_tty_check (monitor, monitor->priv->threshold);

                }
        }

        return FALSE;
}

static void
schedule_tty_check (CkTtyIdleMonitor *monitor,
                    guint             seconds)
{
        if (monitor->priv->timeout_id == 0) {
#if GLIB_CHECK_VERSION(2,14,0)
                monitor->priv->timeout_id = g_timeout_add_seconds (seconds,
                                                                   (GSourceFunc)check_tty_idle,
                                                                   monitor);
#else
                monitor->priv->timeout_id = g_timeout_add (seconds * 1000,
                                                           (GSourceFunc)check_tty_idle,
                                                           monitor);
#endif
        }
}

static void
add_idle_hint_timeout (CkTtyIdleMonitor *monitor)
{
        schedule_tty_check (monitor, monitor->priv->threshold);
}

void
ck_tty_idle_monitor_stop (CkTtyIdleMonitor *monitor)
{
        remove_idle_hint_timeout (monitor);
        monitor_remove_watch (monitor);
}

void
ck_tty_idle_monitor_start (CkTtyIdleMonitor *monitor)
{
        ck_tty_idle_monitor_stop (monitor);

        add_idle_hint_timeout (monitor);
}

static void
ck_tty_idle_monitor_class_init (CkTtyIdleMonitorClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->get_property = ck_tty_idle_monitor_get_property;
        object_class->set_property = ck_tty_idle_monitor_set_property;
        object_class->finalize = ck_tty_idle_monitor_finalize;

        signals [IDLE_HINT_CHANGED] =
                g_signal_new ("idle-hint-changed",
                              G_TYPE_FROM_CLASS (object_class),
                              G_SIGNAL_RUN_LAST,
                              G_STRUCT_OFFSET (CkTtyIdleMonitorClass, idle_hint_changed),
                              NULL,
                              NULL,
                              g_cclosure_marshal_VOID__BOOLEAN,
                              G_TYPE_NONE,
                              1, G_TYPE_BOOLEAN);

        g_object_class_install_property (object_class,
                                         PROP_DEVICE,
                                         g_param_spec_string ("device",
                                                              "device",
                                                              "device",
                                                              NULL,
                                                              G_PARAM_READWRITE));
        g_object_class_install_property (object_class,
                                         PROP_THRESHOLD,
                                         g_param_spec_uint ("threshold",
                                                            "Threshold",
                                                            "Threshold",
                                                            0,
                                                            G_MAXINT,
                                                            DEFAULT_THRESHOLD_SECONDS,
                                                            G_PARAM_READWRITE));

        g_type_class_add_private (klass, sizeof (CkTtyIdleMonitorPrivate));
}

static void
ck_tty_idle_monitor_init (CkTtyIdleMonitor *monitor)
{
        monitor->priv = CK_TTY_IDLE_MONITOR_GET_PRIVATE (monitor);

        monitor->priv->threshold = DEFAULT_THRESHOLD_SECONDS;
}

static void
ck_tty_idle_monitor_finalize (GObject *object)
{
        CkTtyIdleMonitor *monitor;

        g_return_if_fail (object != NULL);
        g_return_if_fail (CK_IS_TTY_IDLE_MONITOR (object));

        monitor = CK_TTY_IDLE_MONITOR (object);

        g_return_if_fail (monitor->priv != NULL);

        ck_tty_idle_monitor_stop (monitor);

        g_free (monitor->priv->device);

        G_OBJECT_CLASS (ck_tty_idle_monitor_parent_class)->finalize (object);
}

CkTtyIdleMonitor *
ck_tty_idle_monitor_new (const char *device)
{
        GObject *object;

        object = g_object_new (CK_TYPE_TTY_IDLE_MONITOR,
                               "device", device,
                               NULL);

        return CK_TTY_IDLE_MONITOR (object);
}
