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


#ifndef __CK_SESSION_H
#define __CK_SESSION_H

#include <glib-object.h>
#include "ck-session-generated.h"

G_BEGIN_DECLS

#define CK_TYPE_SESSION         (ck_session_get_type ())
#define CK_SESSION(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CK_TYPE_SESSION, CkSession))
#define CK_SESSION_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), CK_TYPE_SESSION, CkSessionClass))
#define CK_IS_SESSION(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CK_TYPE_SESSION))
#define CK_IS_SESSION_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CK_TYPE_SESSION))
#define CK_SESSION_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CK_TYPE_SESSION, CkSessionClass))

typedef struct CkSessionPrivate CkSessionPrivate;

typedef struct
{
        ConsoleKitSessionSkeleton  parent;
        CkSessionPrivate  *priv;
} CkSession;

typedef struct
{
        /* internal signals */
        void          (* activate)          (CkSession             *session,
                                             GDBusMethodInvocation *context);

        ConsoleKitSessionSkeletonClass parent_class;
} CkSessionClass;

typedef enum
{
        CK_SESSION_ERROR_FAILED,
        CK_SESSION_ERROR_GENERAL,
        CK_SESSION_ERROR_INSUFFICIENT_PERMISSION,
        CK_SESSION_ERROR_NOT_SUPPORTED,
        CK_SESSION_ERROR_ALREADY_ACTIVE,
        CK_SESSION_NUM_ERRORS
} CkSessionError;

#define CK_SESSION_ERROR ck_session_error_quark ()


GQuark              ck_session_error_quark            (void);
GType               ck_session_error_get_type         (void);
GType               ck_session_get_type               (void);

CkSession         * ck_session_new                    (const char            *ssid,
                                                       const char            *cookie,
                                                       GDBusConnection       *connection);
CkSession         * ck_session_new_with_parameters    (const char            *ssid,
                                                       const char            *cookie,
                                                       const GVariant        *parameters,
                                                       GDBusConnection       *connection);

void                ck_session_dump                   (CkSession             *session,
                                                       GKeyFile              *key_file);
void                ck_session_run_programs           (CkSession             *session,
                                                       const char            *action);

gboolean            ck_session_set_active             (CkSession             *session,
                                                       gboolean               active,
                                                       gboolean               force);
gboolean            ck_session_set_is_local           (CkSession             *session,
                                                       gboolean               is_local,
                                                       GError               **error);
gboolean            ck_session_set_unix_user          (CkSession             *session,
                                                       guint                  uid,
                                                       GError               **error);
gboolean            ck_session_set_x11_display        (CkSession             *session,
                                                       const char            *xdisplay,
                                                       GError               **error);
gboolean            ck_session_set_x11_display_device (CkSession             *session,
                                                       const char            *xdisplay,
                                                       GError               **error);
gboolean            ck_session_set_display_device     (CkSession             *session,
                                                       const char            *device,
                                                       GError               **error);
gboolean            ck_session_set_remote_host_name   (CkSession             *session,
                                                       const char            *remote_host_name,
                                                       GError               **error);
gboolean            ck_session_set_session_type       (CkSession             *session,
                                                       const char            *type,
                                                       GError               **error);
void                ck_session_set_session_controller (CkSession             *session,
                                                       const gchar           *bus_name);

/* Authoritative properties */
gboolean            ck_session_get_id                 (CkSession             *session,
                                                       char                 **ssid,
                                                       GError               **error);
const char        * ck_session_get_path               (CkSession             *session);
gboolean            ck_session_get_seat_id            (CkSession             *session,
                                                       char                 **sid,
                                                       GError               **error);
gboolean            ck_session_is_active              (CkSession             *session,
                                                       gboolean              *active,
                                                       GError               **error);
gboolean            ck_session_is_local               (CkSession             *session,
                                                       gboolean              *local,
                                                       GError               **error);
gboolean            ck_session_get_login_session_id   (CkSession             *session,
                                                       char                 **login_session_id,
                                                       GError               **error);
gboolean            ck_session_get_creation_time      (CkSession             *session,
                                                       char                 **iso8601_datetime,
                                                       GError               **error);
const char        * ck_session_get_runtime_dir        (CkSession             *session);


gboolean            ck_session_set_runtime_dir        (CkSession             *session,
                                                       const char            *runtime_dir);

gboolean            ck_session_set_id                 (CkSession             *session,
                                                       const char            *ssid,
                                                       GError               **error);
gboolean            ck_session_set_cookie             (CkSession             *session,
                                                       const char            *cookie,
                                                       GError               **error);
gboolean            ck_session_set_seat_id            (CkSession             *session,
                                                       const char            *sid,
                                                       const char            *path,
                                                       GError               **error);
gboolean            ck_session_set_login_session_id   (CkSession             *session,
                                                       const char            *login_session_id,
                                                       GError               **error);

void                ck_session_lock                   (CkSession             *session);
void                ck_session_unlock                 (CkSession             *session);

G_END_DECLS

#endif /* __CK_SESSION_H */
