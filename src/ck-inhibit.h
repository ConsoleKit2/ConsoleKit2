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

#ifndef __CK_INHIBIT_H
#define __CK_INHIBIT_H

#include <glib-object.h>

G_BEGIN_DECLS

#define CK_TYPE_INHIBIT         (ck_inhibit_get_type ())
#define CK_INHIBIT(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CK_TYPE_INHIBIT, CkInhibit))
#define CK_INHIBIT_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), CK_TYPE_INHIBIT, CkInhibitClass))
#define CK_IS_INHIBIT(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CK_TYPE_INHIBIT))
#define CK_IS_INHIBIT_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CK_TYPE_INHIBIT))
#define CK_INHIBIT_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CK_TYPE_INHIBIT, CkInhibitClass))

typedef struct CkInhibitPrivate CkInhibitPrivate;

typedef struct
{
        GObject           parent;
        CkInhibitPrivate *priv;
} CkInhibit;

typedef struct
{
        GObjectClass parent_class;

        /*< signals >*/
        void (*changed_event) (CkInhibit *inhibit, gint event, gboolean enabled);
} CkInhibitClass;

/* The list of events that may be inhibited -- except for event_last :) */
typedef enum
{
        CK_INHIBIT_EVENT_SHUTDOWN = 0,
        CK_INHIBIT_EVENT_SUSPEND,
        CK_INHIBIT_EVENT_IDLE,
        CK_INHIBIT_EVENT_POWER_KEY,
        CK_INHIBIT_EVENT_SUSPEND_KEY,
        CK_INHIBIT_EVENT_HIBERNATE_KEY,
        CK_INHIBIT_EVENT_LAST
} CkInhibitEvent;

/*
 * Various error codes for CkInhibit. All the values will be negative except
 * for NO_ERROR
 */
typedef enum
{
        CK_INHIBIT_ERROR_NO_ERROR      =   1,
        CK_INHIBIT_ERROR_GENERAL       = -10,
        CK_INHIBIT_ERROR_INVALID_INPUT = -20,
        CK_INHIBIT_ERROR_OOM           = -30,
} CkInhbitError;


GType           ck_inhibit_get_type                    (void);

CkInhibit      *ck_inhibit_new                         (void);

gint            ck_inhibit_create_lock                 (CkInhibit   *inhibit,
                                                        const gchar *who,
                                                        const gchar *what,
                                                        const gchar *why);

void            ck_inhibit_remove_lock                 (CkInhibit   *inhibit);

const gchar    *ck_inhibit_get_who                     (CkInhibit   *inhibit);
const gchar    *ck_inhibit_get_why                     (CkInhibit   *inhibit);

gboolean        ck_inhibit_is_shutdown_inhibited       (CkInhibit   *inhibit);
gboolean        ck_inhibit_is_suspend_inhibited        (CkInhibit   *inhibit);
gboolean        ck_inhibit_is_idle_inhibited           (CkInhibit   *inhibit);
gboolean        ck_inhibit_is_power_key_inhibited      (CkInhibit   *inhibit);
gboolean        ck_inhibit_is_suspend_key_inhibited    (CkInhibit   *inhibit);
gboolean        ck_inhibit_is_hibernate_key_inhibited  (CkInhibit   *inhibit);

G_END_DECLS

#endif /* __CK_INHIBIT_H */
