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

#ifndef __PROC_H
#define __PROC_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct _proc_stat_t proc_stat_t;

gboolean proc_stat_new_for_pid (pid_t         pid,
                                proc_stat_t **stat,
                                GError      **error);
char    *proc_stat_get_tty     (proc_stat_t  *stat);
char    *proc_stat_get_cmd     (proc_stat_t  *stat);
void     proc_stat_free        (proc_stat_t  *stat);

char    *proc_pid_get_env      (pid_t         pid,
                                const char   *var);

G_END_DECLS

#endif /* __PROC_H */
