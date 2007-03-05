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

#ifndef __CK_JOB_H
#define __CK_JOB_H

#include <glib-object.h>

G_BEGIN_DECLS

#define CK_TYPE_JOB         (ck_job_get_type ())
#define CK_JOB(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), CK_TYPE_JOB, CkJob))
#define CK_JOB_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), CK_TYPE_JOB, CkJobClass))
#define CK_IS_JOB(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), CK_TYPE_JOB))
#define CK_IS_JOB_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), CK_TYPE_JOB))
#define CK_JOB_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), CK_TYPE_JOB, CkJobClass))

typedef struct CkJobPrivate CkJobPrivate;

typedef struct
{
        GObject       parent;
        CkJobPrivate *priv;
} CkJob;

typedef struct
{
        GObjectClass   parent_class;

        void          (* completed)            (CkJob *job,
                                                int    status);
} CkJobClass;

typedef enum
{
        CK_JOB_ERROR_GENERAL
} CkJobError;

#define CK_JOB_ERROR ck_job_error_quark ()

GQuark              ck_job_error_quark         (void);
GType               ck_job_get_type            (void);
CkJob             * ck_job_new                 (void);

gboolean            ck_job_set_command         (CkJob      *job,
                                                const char *command);
gboolean            ck_job_get_command         (CkJob      *job,
                                                char      **command);
gboolean            ck_job_execute             (CkJob      *job,
                                                GError    **error);
gboolean            ck_job_get_stdout          (CkJob      *job,
                                                char      **std_output);
gboolean            ck_job_get_stderr          (CkJob      *job,
                                                char      **std_error);
gboolean            ck_job_cancel              (CkJob      *job);

G_END_DECLS

#endif /* __CK_JOB_H */
