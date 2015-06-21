/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014 Eric Koegel <eric.koegel@gmail.com>
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

#ifndef __CK_INHIBIT_MANAGER_H
#define __CK_INHIBIT_MANAGER_H

#include <glib-object.h>


G_BEGIN_DECLS

#define CK_TYPE_INHIBIT_MANAGER         (ck_inhibit_manager_get_type ())
#define CK_INHIBIT_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CK_TYPE_INHIBIT_MANAGER, CkInhibitManager))
#define CK_INHIBIT_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), CK_TYPE_INHIBIT_MANAGER, CkInhibitManagerClass))
#define CK_IS_INHIBIT_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CK_TYPE_INHIBIT_MANAGER))
#define CK_IS_INHIBIT_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CK_TYPE_INHIBIT_MANAGER))
#define CK_INHIBIT_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CK_TYPE_INHIBIT_MANAGER, CkInhibitManagerClass))

typedef struct CkInhibitManagerPrivate CkInhibitManagerPrivate;

typedef struct
{
        GObject           parent;
        CkInhibitManagerPrivate *priv;
} CkInhibitManager;

typedef struct
{
        GObjectClass parent_class;

        /*< signals >*/
        void (*changed_event) (CkInhibitManager *manager, gint inhibit_mode, gint event, gboolean enabled);
} CkInhibitManagerClass;


GType             ck_inhibit_manager_get_type                    (void);

CkInhibitManager *ck_inhibit_manager_get                         (void);

gint              ck_inhibit_manager_create_lock                 (CkInhibitManager *manager,
                                                                  const gchar      *who,
                                                                  const gchar      *what,
                                                                  const gchar      *why,
                                                                  const gchar      *mode,
                                                                  uid_t             uid,
                                                                  pid_t             pid);

gboolean          ck_inhibit_manager_remove_lock                 (CkInhibitManager *manager,
                                                                  const gchar      *who);

gboolean          ck_inhibit_manager_is_shutdown_delayed         (CkInhibitManager *manager);
gboolean          ck_inhibit_manager_is_suspend_delayed          (CkInhibitManager *manager);
gboolean          ck_inhibit_manager_is_idle_delayed             (CkInhibitManager *manager);
gboolean          ck_inhibit_manager_is_power_key_delayed        (CkInhibitManager *manager);
gboolean          ck_inhibit_manager_is_suspend_key_delayed      (CkInhibitManager *manager);
gboolean          ck_inhibit_manager_is_hibernate_key_delayed    (CkInhibitManager *manager);
gboolean          ck_inhibit_manager_is_lid_switch_delayed       (CkInhibitManager *manager);

gboolean          ck_inhibit_manager_is_shutdown_blocked         (CkInhibitManager *manager);
gboolean          ck_inhibit_manager_is_suspend_blocked          (CkInhibitManager *manager);
gboolean          ck_inhibit_manager_is_idle_blocked             (CkInhibitManager *manager);
gboolean          ck_inhibit_manager_is_power_key_blocked        (CkInhibitManager *manager);
gboolean          ck_inhibit_manager_is_suspend_key_blocked      (CkInhibitManager *manager);
gboolean          ck_inhibit_manager_is_hibernate_key_blocked    (CkInhibitManager *manager);
gboolean          ck_inhibit_manager_is_lid_switch_blocked       (CkInhibitManager *manager);

GList            *ck_inhibit_manager_get_inhibit_list            (CkInhibitManager *manager);

G_END_DECLS

#endif /* __CK_INHIBIT_MANAGER_H */
