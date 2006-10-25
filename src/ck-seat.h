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

#ifndef __CK_SEAT_H
#define __CK_SEAT_H

#include <glib-object.h>
#include <dbus/dbus-glib.h>

#include "ck-session.h"

G_BEGIN_DECLS

#define CK_TYPE_SEAT         (ck_seat_get_type ())
#define CK_SEAT(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CK_TYPE_SEAT, CkSeat))
#define CK_SEAT_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), CK_TYPE_SEAT, CkSeatClass))
#define CK_IS_SEAT(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CK_TYPE_SEAT))
#define CK_IS_SEAT_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CK_TYPE_SEAT))
#define CK_SEAT_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CK_TYPE_SEAT, CkSeatClass))

typedef struct CkSeatPrivate CkSeatPrivate;

typedef struct
{
        GObject        parent;
        CkSeatPrivate *priv;
} CkSeat;

typedef struct
{
        GObjectClass   parent_class;

        void          (* active_session_changed) (CkSeat     *seat,
                                                  const char *ssid);
        void          (* session_added)          (CkSeat     *seat,
                                                  const char *ssid);
        void          (* session_removed)        (CkSeat     *seat,
                                                  const char *ssid);
} CkSeatClass;

typedef enum
{
        CK_SEAT_KIND_STATIC,
        CK_SEAT_KIND_DYNAMIC,
} CkSeatKind;

GType ck_seat_kind_get_type (void);
#define CK_TYPE_SEAT_KIND (ck_seat_kind_get_type ())

typedef enum
{
        CK_SEAT_ERROR_GENERAL
} CkSeatError;

#define CK_SEAT_ERROR ck_seat_error_quark ()

GQuark              ck_seat_error_quark         (void);
GType               ck_seat_get_type            (void);
CkSeat            * ck_seat_new                 (const char            *sid,
                                                 CkSeatKind             kind);
gboolean            ck_seat_get_kind            (CkSeat                *seat,
                                                 CkSeatKind            *kind,
                                                 GError               **error);
gboolean            ck_seat_add_session         (CkSeat                *seat,
                                                 CkSession             *session,
                                                 GError               **error);
gboolean            ck_seat_remove_session      (CkSeat                *seat,
                                                 CkSession             *session,
                                                 GError               **error);
gboolean            ck_seat_set_active_session  (CkSeat                *seat,
                                                 CkSession             *session,
                                                 GError               **error);

/* exported methods */
gboolean            ck_seat_get_id              (CkSeat                *seat,
                                                 char                 **sid,
                                                 GError               **error);
gboolean            ck_seat_get_seat_type       (CkSeat                *seat,
                                                 char                 **type,
                                                 GError               **error);
gboolean            ck_seat_get_sessions        (CkSeat                *seat,
                                                 GPtrArray            **sessions,
                                                 GError               **error);
gboolean            ck_seat_get_active_session  (CkSeat                *seat,
                                                 char                 **ssid,
                                                 GError               **error);

gboolean            ck_seat_activate_session    (CkSeat                *seat,
                                                 const char            *ssid,
                                                 DBusGMethodInvocation *context);

G_END_DECLS

#endif /* __CK_SEAT_H */
