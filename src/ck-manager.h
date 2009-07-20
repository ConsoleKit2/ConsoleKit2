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
#include <dbus/dbus-glib.h>

#include "ck-seat.h"

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
        GObject           parent;
        CkManagerPrivate *priv;
} CkManager;

typedef struct
{
        GObjectClass   parent_class;

        void          (* seat_added)               (CkManager  *manager,
                                                    const char *sid);
        void          (* seat_removed)             (CkManager  *manager,
                                                    const char *sid);
        void          (* system_idle_hint_changed) (CkManager  *manager,
                                                    gboolean    idle_hint);
} CkManagerClass;

typedef enum
{
        CK_MANAGER_ERROR_GENERAL,
        CK_MANAGER_ERROR_NOT_PRIVILEGED,
        CK_MANAGER_NUM_ERRORS
} CkManagerError;

#define CK_MANAGER_ERROR ck_manager_error_quark ()

GType               ck_manager_error_get_type                 (void);
#define CK_MANAGER_TYPE_ERROR (ck_manager_error_get_type ())

GQuark              ck_manager_error_quark                    (void);
GType               ck_manager_get_type                       (void);

CkManager         * ck_manager_new                            (void);

/* unprivileged methods */


/* System actions */
gboolean            ck_manager_stop                           (CkManager             *manager,
                                                               DBusGMethodInvocation *context);

gboolean            ck_manager_restart                        (CkManager             *manager,
                                                               DBusGMethodInvocation *context);

gboolean            ck_manager_can_stop                       (CkManager *manager,
                                                                DBusGMethodInvocation *context);
gboolean            ck_manager_can_restart                    (CkManager *manager,
                                                               DBusGMethodInvocation *context);
/* Authoritative properties */
gboolean            ck_manager_open_session                   (CkManager             *manager,
                                                               DBusGMethodInvocation *context);
gboolean            ck_manager_get_sessions                   (CkManager             *manager,
                                                               GPtrArray            **sessions,
                                                               GError               **error);
gboolean            ck_manager_get_seats                      (CkManager             *manager,
                                                               GPtrArray            **seats,
                                                               GError               **error);
gboolean            ck_manager_close_session                  (CkManager             *manager,
                                                               const char            *cookie,
                                                               DBusGMethodInvocation *context);
gboolean            ck_manager_get_current_session            (CkManager             *manager,
                                                               DBusGMethodInvocation *context);
gboolean            ck_manager_get_session_for_cookie         (CkManager             *manager,
                                                               const char            *cookie,
                                                               DBusGMethodInvocation *context);
gboolean            ck_manager_get_session_for_unix_process   (CkManager             *manager,
                                                               guint                  pid,
                                                               DBusGMethodInvocation *context);
gboolean            ck_manager_get_sessions_for_unix_user     (CkManager             *manager,
                                                               guint                  uid,
                                                               DBusGMethodInvocation *context);
/* deprecated */
gboolean            ck_manager_get_sessions_for_user          (CkManager             *manager,
                                                               guint                  uid,
                                                               DBusGMethodInvocation *context);

/* Non-authoritative properties */
gboolean            ck_manager_get_system_idle_hint           (CkManager             *manager,
                                                               gboolean              *idle_hint,
                                                               GError               **error);
gboolean            ck_manager_get_system_idle_since_hint     (CkManager             *manager,
                                                               char                 **iso8601_datetime,
                                                               GError               **error);

/* privileged methods - should be protected by D-Bus policy */
gboolean            ck_manager_open_session_with_parameters   (CkManager             *manager,
                                                               const GPtrArray       *parameters,
                                                               DBusGMethodInvocation *context);

G_END_DECLS

#endif /* __CK_MANAGER_H */
