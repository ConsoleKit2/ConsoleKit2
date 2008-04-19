/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2006-2007 William Jon McCann <mccann@jhu.edu>
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


#ifndef __CK_SESSION_LEADER_H
#define __CK_SESSION_LEADER_H

#include <glib-object.h>
#include <dbus/dbus-glib.h>

G_BEGIN_DECLS

#define CK_TYPE_SESSION_LEADER         (ck_session_leader_get_type ())
#define CK_SESSION_LEADER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CK_TYPE_SESSION_LEADER, CkSessionLeader))
#define CK_SESSION_LEADER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), CK_TYPE_SESSION_LEADER, CkSessionLeaderClass))
#define CK_IS_SESSION_LEADER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CK_TYPE_SESSION_LEADER))
#define CK_IS_SESSION_LEADER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CK_TYPE_SESSION_LEADER))
#define CK_SESSION_LEADER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CK_TYPE_SESSION_LEADER, CkSessionLeaderClass))

typedef struct CkSessionLeaderPrivate CkSessionLeaderPrivate;

typedef struct
{
        GObject                 parent;
        CkSessionLeaderPrivate *priv;
} CkSessionLeader;

typedef struct
{
        GObjectClass   parent_class;

} CkSessionLeaderClass;

typedef enum
{
         CK_SESSION_LEADER_ERROR_GENERAL
} CkSessionLeaderError;

#define CK_SESSION_LEADER_ERROR ck_session_leader_error_quark ()

typedef void  (* CkSessionLeaderDoneFunc) (CkSessionLeader       *session_leader,
                                           GPtrArray             *parameters,
                                           DBusGMethodInvocation *context,
                                           gpointer               data);

GQuark              ck_session_leader_error_quark             (void);
GType               ck_session_leader_get_type                (void);
CkSessionLeader   * ck_session_leader_new                     (void);

void                ck_session_leader_set_pid                 (CkSessionLeader        *session_leader,
                                                               pid_t                   pid);
void                ck_session_leader_set_uid                 (CkSessionLeader        *session_leader,
                                                               uid_t                   uid);
void                ck_session_leader_set_session_id          (CkSessionLeader        *session_leader,
                                                               const char             *session_id);
void                ck_session_leader_set_cookie              (CkSessionLeader        *session_leader,
                                                               const char             *cookie);
void                ck_session_leader_set_service_name        (CkSessionLeader        *session_leader,
                                                               const char             *sender);

void                ck_session_leader_set_override_parameters (CkSessionLeader       *session_leader,
                                                               const GPtrArray       *parameters);

const char *        ck_session_leader_peek_session_id         (CkSessionLeader        *session_leader);
const char *        ck_session_leader_peek_cookie             (CkSessionLeader        *session_leader);
const char *        ck_session_leader_peek_service_name       (CkSessionLeader        *session_leader);
uid_t               ck_session_leader_get_uid                 (CkSessionLeader        *session_leader);
pid_t               ck_session_leader_get_pid                 (CkSessionLeader        *session_leader);


gboolean            ck_session_leader_collect_parameters      (CkSessionLeader        *session_leader,
                                                               DBusGMethodInvocation  *context,
                                                               CkSessionLeaderDoneFunc done_cb,
                                                               gpointer                data);
void                ck_session_leader_cancel                  (CkSessionLeader        *session_leader);

void                ck_session_leader_dump                    (CkSessionLeader         *session_leader,
                                                               GKeyFile                *key_file);


G_END_DECLS

#endif /* __CK_SESSION_LEADER_H */
