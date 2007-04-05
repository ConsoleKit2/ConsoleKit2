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

#ifndef __CK_FILE_MONITOR_H
#define __CK_FILE_MONITOR_H

#include <glib-object.h>

G_BEGIN_DECLS

#define CK_TYPE_FILE_MONITOR         (ck_file_monitor_get_type ())
#define CK_FILE_MONITOR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CK_TYPE_FILE_MONITOR, CkFileMonitor))
#define CK_FILE_MONITOR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), CK_TYPE_FILE_MONITOR, CkFileMonitorClass))
#define CK_IS_FILE_MONITOR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CK_TYPE_FILE_MONITOR))
#define CK_IS_FILE_MONITOR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CK_TYPE_FILE_MONITOR))
#define CK_FILE_MONITOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CK_TYPE_FILE_MONITOR, CkFileMonitorClass))

typedef struct CkFileMonitorPrivate CkFileMonitorPrivate;

typedef struct
{
        GObject               parent;
        CkFileMonitorPrivate *priv;
} CkFileMonitor;

typedef struct
{
        GObjectClass parent_class;
} CkFileMonitorClass;

typedef enum
{
        CK_FILE_MONITOR_EVENT_NONE    = 1 << 0,
        CK_FILE_MONITOR_EVENT_ACCESS  = 1 << 1,
        CK_FILE_MONITOR_EVENT_CREATE  = 1 << 2,
        CK_FILE_MONITOR_EVENT_DELETE  = 1 << 3,
        CK_FILE_MONITOR_EVENT_CHANGE  = 1 << 4,
} CkFileMonitorEvent;

typedef enum
{
         CK_FILE_MONITOR_ERROR_GENERAL
} CkFileMonitorError;

#define CK_FILE_MONITOR_ERROR ck_file_monitor_error_quark ()

typedef void (*CkFileMonitorNotifyFunc) (CkFileMonitor      *monitor,
                                         CkFileMonitorEvent  event,
                                         const char         *path,
                                         gpointer            user_data);

GQuark              ck_file_monitor_error_quark           (void);
GType               ck_file_monitor_get_type              (void);

CkFileMonitor     * ck_file_monitor_new                   (void);

guint               ck_file_monitor_add_notify            (CkFileMonitor          *monitor,
                                                           const char             *path,
                                                           int                     mask,
                                                           CkFileMonitorNotifyFunc notify_func,
                                                           gpointer                data);
void                ck_file_monitor_remove_notify         (CkFileMonitor          *monitor,
                                                           guint                   id);

G_END_DECLS

#endif /* __CK_FILE_MONITOR_H */
