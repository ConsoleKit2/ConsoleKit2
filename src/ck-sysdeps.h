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

#ifndef __CK_SYSDEPS_H
#define __CK_SYSDEPS_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct _CkProcessStat CkProcessStat;

gboolean     ck_process_stat_new_for_unix_pid (pid_t           pid,
                                               CkProcessStat **stat,
                                               GError        **error);
pid_t        ck_process_stat_get_ppid         (CkProcessStat  *stat);
char        *ck_process_stat_get_tty          (CkProcessStat  *stat);
char        *ck_process_stat_get_cmd          (CkProcessStat  *stat);
void         ck_process_stat_free             (CkProcessStat  *stat);


char        *ck_unix_pid_get_env              (pid_t           pid,
                                               const char     *var);

GHashTable  *ck_unix_pid_get_env_hash         (pid_t           pid);

pid_t        ck_unix_pid_get_ppid             (pid_t           pid);
uid_t        ck_unix_pid_get_uid              (pid_t           pid);

G_END_DECLS

#endif /* __CK_SYSDEPS_H */
