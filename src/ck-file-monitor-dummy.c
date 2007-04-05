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

#include <glib.h>
#include <glib-object.h>

#include "ck-file-monitor.h"

#define CK_FILE_MONITOR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CK_TYPE_FILE_MONITOR, CkFileMonitorPrivate))

struct CkFileMonitorPrivate
{
        gpointer    dummy;
};

static void     ck_file_monitor_class_init  (CkFileMonitorClass *klass);
static void     ck_file_monitor_init        (CkFileMonitor      *file_monitor);
static void     ck_file_monitor_finalize    (GObject            *object);

G_DEFINE_TYPE (CkFileMonitor, ck_file_monitor, G_TYPE_OBJECT)

static gpointer monitor_object = NULL;

GQuark
ck_file_monitor_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("ck_file_monitor_error");
        }

        return ret;
}

guint
ck_file_monitor_add_notify (CkFileMonitor          *monitor,
                            const char             *path,
                            int                     mask,
                            CkFileMonitorNotifyFunc notify_func,
                            gpointer                data)
{
        return 0;
}

void
ck_file_monitor_remove_notify (CkFileMonitor *monitor,
                               guint          id)
{
        return;
}

static void
ck_file_monitor_class_init (CkFileMonitorClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = ck_file_monitor_finalize;

        g_type_class_add_private (klass, sizeof (CkFileMonitorPrivate));
}

static void
ck_file_monitor_init (CkFileMonitor *monitor)
{
        monitor->priv = CK_FILE_MONITOR_GET_PRIVATE (monitor);
}

static void
ck_file_monitor_finalize (GObject *object)
{
        CkFileMonitor *monitor;

        g_return_if_fail (object != NULL);
        g_return_if_fail (CK_IS_FILE_MONITOR (object));

        monitor = CK_FILE_MONITOR (object);

        g_return_if_fail (monitor->priv != NULL);

        G_OBJECT_CLASS (ck_file_monitor_parent_class)->finalize (object);
}

CkFileMonitor *
ck_file_monitor_new (void)
{
        if (monitor_object != NULL) {
                g_object_ref (monitor_object);
        } else {
                monitor_object = g_object_new (CK_TYPE_FILE_MONITOR, NULL);

                g_object_add_weak_pointer (monitor_object,
                                           (gpointer *) &monitor_object);
        }

        return CK_FILE_MONITOR (monitor_object);
}
