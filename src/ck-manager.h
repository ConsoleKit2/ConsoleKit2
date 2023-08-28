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


#ifndef __CK_MANAGER_H
#define __CK_MANAGER_H

#include <glib-object.h>
#include "ck-manager-generated.h"

#include "ck-seat.h"

#define DBUS_NAME              "org.freedesktop.ConsoleKit"
#define CK_DBUS_PATH           "/org/freedesktop/ConsoleKit"
#define CK_MANAGER_DBUS_PATH   CK_DBUS_PATH "/Manager"
#define DBUS_SESSION_INTERFACE DBUS_NAME ".Session"
#define DBUS_MANAGER_INTERFACE DBUS_NAME ".Manager"

G_BEGIN_DECLS

#define CK_TYPE_MANAGER         (ck_manager_get_type ())
#define CK_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CK_TYPE_MANAGER, CkManager))
#define CK_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), CK_TYPE_MANAGER, CkManagerClass))
#define CK_IS_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CK_TYPE_MANAGER))
#define CK_IS_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CK_TYPE_MANAGER))
#define CK_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CK_TYPE_MANAGER, CkManagerClass))

typedef struct CkManagerPrivate CkManagerPrivate;

typedef struct
{
        ConsoleKitManagerSkeleton  parent;
        CkManagerPrivate *priv;
} CkManager;

typedef struct
{
        ConsoleKitManagerSkeletonClass parent_class;
} CkManagerClass;

typedef enum
{
        CK_MANAGER_ERROR_FAILED,
        CK_MANAGER_ERROR_GENERAL,
        CK_MANAGER_ERROR_INSUFFICIENT_PERMISSION,
        CK_MANAGER_ERROR_AUTHORIZATION_REQUIRED,
        CK_MANAGER_ERROR_NOT_SUPPORTED,
        CK_MANAGER_ERROR_BUSY,
        CK_MANAGER_ERROR_INHIBITED,
        CK_MANAGER_ERROR_INVALID_INPUT,
        CK_MANAGER_ERROR_OOM,
        CK_MANAGER_ERROR_NO_SEATS,
        CK_MANAGER_ERROR_NO_SESSIONS,
        CK_MANAGER_ERROR_NOTHING_INHIBITED,
        CK_MANAGER_NUM_ERRORS
} CkManagerError;


#define CK_MANAGER_ERROR ck_manager_error_quark ()


GQuark              ck_manager_error_quark                    (void);
GType               ck_manager_error_get_type                 (void);
GType               ck_manager_get_type                       (void);

CkManager         * ck_manager_new                            (GDBusConnection *connection);


G_END_DECLS

#endif /* __CK_MANAGER_H */
