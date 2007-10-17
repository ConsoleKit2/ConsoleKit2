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

#ifndef __CK_EVENT_LOGGER_H
#define __CK_EVENT_LOGGER_H

#include <glib-object.h>
#include "ck-log-event.h"

G_BEGIN_DECLS

#define CK_TYPE_EVENT_LOGGER         (ck_event_logger_get_type ())
#define CK_EVENT_LOGGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CK_TYPE_EVENT_LOGGER, CkEventLogger))
#define CK_EVENT_LOGGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), CK_TYPE_EVENT_LOGGER, CkEventLoggerClass))
#define CK_IS_EVENT_LOGGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CK_TYPE_EVENT_LOGGER))
#define CK_IS_EVENT_LOGGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CK_TYPE_EVENT_LOGGER))
#define CK_EVENT_LOGGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CK_TYPE_EVENT_LOGGER, CkEventLoggerClass))

typedef struct CkEventLoggerPrivate CkEventLoggerPrivate;

typedef struct
{
        GObject               parent;
        CkEventLoggerPrivate *priv;
} CkEventLogger;

typedef struct
{
        GObjectClass   parent_class;
} CkEventLoggerClass;

typedef enum
{
         CK_EVENT_LOGGER_ERROR_GENERAL
} CkEventLoggerError;

#define CK_EVENT_LOGGER_ERROR ck_event_logger_error_quark ()

GQuark               ck_event_logger_error_quark         (void);
GType                ck_event_logger_get_type            (void);
CkEventLogger      * ck_event_logger_new                 (const char         *filename);

gboolean             ck_event_logger_queue_event         (CkEventLogger      *event_logger,
                                                          CkLogEvent         *event,
                                                          GError            **error);

G_END_DECLS

#endif /* __CK_EVENT_LOGGER_H */
