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

#ifndef __CK_VT_MONITOR_H
#define __CK_VT_MONITOR_H

#include <glib-object.h>

G_BEGIN_DECLS

#define CK_TYPE_VT_MONITOR         (ck_vt_monitor_get_type ())
#define CK_VT_MONITOR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CK_TYPE_VT_MONITOR, CkVtMonitor))
#define CK_VT_MONITOR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), CK_TYPE_VT_MONITOR, CkVtMonitorClass))
#define CK_IS_VT_MONITOR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CK_TYPE_VT_MONITOR))
#define CK_IS_VT_MONITOR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CK_TYPE_VT_MONITOR))
#define CK_VT_MONITOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CK_TYPE_VT_MONITOR, CkVtMonitorClass))

typedef struct CkVtMonitorPrivate CkVtMonitorPrivate;

typedef struct
{
        GObject             parent;
        CkVtMonitorPrivate *priv;
} CkVtMonitor;

typedef struct
{
        GObjectClass   parent_class;

        void          (* active_changed) (CkVtMonitor     *vt_monitor,
                                          guint            num);
} CkVtMonitorClass;

typedef enum
{
         CK_VT_MONITOR_ERROR_GENERAL
} CkVtMonitorError;

#define CK_VT_MONITOR_ERROR ck_vt_monitor_error_quark ()

GQuark              ck_vt_monitor_error_quark         (void);
GType               ck_vt_monitor_get_type            (void);
CkVtMonitor       * ck_vt_monitor_new                 (void);

gboolean            ck_vt_monitor_set_active          (CkVtMonitor    *vt_monitor,
                                                       guint32         num,
                                                       GError        **error);
gboolean            ck_vt_monitor_get_active          (CkVtMonitor    *vt_monitor,
                                                       guint32        *num,
                                                       GError        **error);

G_END_DECLS

#endif /* __CK_VT_MONITOR_H */
