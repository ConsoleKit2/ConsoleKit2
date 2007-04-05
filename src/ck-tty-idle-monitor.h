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


#ifndef __CK_TTY_IDLE_MONITOR_H
#define __CK_TTY_IDLE_MONITOR_H

#include <glib-object.h>
#include <dbus/dbus-glib.h>

G_BEGIN_DECLS

#define CK_TYPE_TTY_IDLE_MONITOR         (ck_tty_idle_monitor_get_type ())
#define CK_TTY_IDLE_MONITOR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CK_TYPE_TTY_IDLE_MONITOR, CkTtyIdleMonitor))
#define CK_TTY_IDLE_MONITOR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), CK_TYPE_TTY_IDLE_MONITOR, CkTtyIdleMonitorClass))
#define CK_IS_TTY_IDLE_MONITOR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CK_TYPE_TTY_IDLE_MONITOR))
#define CK_IS_TTY_IDLE_MONITOR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CK_TYPE_TTY_IDLE_MONITOR))
#define CK_TTY_IDLE_MONITOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CK_TYPE_TTY_IDLE_MONITOR, CkTtyIdleMonitorClass))

typedef struct CkTtyIdleMonitorPrivate CkTtyIdleMonitorPrivate;

typedef struct
{
        GObject                  parent;
        CkTtyIdleMonitorPrivate *priv;
} CkTtyIdleMonitor;

typedef struct
{
        GObjectClass   parent_class;

        void          (* idle_hint_changed) (CkTtyIdleMonitor *monitor,
                                             gboolean          idle_hint);
} CkTtyIdleMonitorClass;

typedef enum
{
         CK_TTY_IDLE_MONITOR_ERROR_GENERAL
} CkTtyIdleMonitorError;

#define CK_TTY_IDLE_MONITOR_ERROR ck_tty_idle_monitor_error_quark ()

GQuark              ck_tty_idle_monitor_error_quark            (void);
GType               ck_tty_idle_monitor_get_type               (void);

CkTtyIdleMonitor  * ck_tty_idle_monitor_new                    (const char *device);
void                ck_tty_idle_monitor_set_threshold          (CkTtyIdleMonitor *monitor,
                                                                guint             seconds);
void                ck_tty_idle_monitor_start                  (CkTtyIdleMonitor *monitor);
void                ck_tty_idle_monitor_stop                   (CkTtyIdleMonitor *monitor);

G_END_DECLS

#endif /* __CK_TTY_IDLE_MONITOR_H */
