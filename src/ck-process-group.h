/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (c) 2015, Eric Koegel <eric.koegel@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __CK_PROCESS_GROUP_H_
#define __CK_PROCESS_GROUP_H_

#include <glib-object.h>

#define CK_TYPE_PROCESS_GROUP           (ck_process_group_get_type ())
#define CK_PROCESS_GROUP(o)             (G_TYPE_CHECK_INSTANCE_CAST ((o), CK_TYPE_PROCESS_GROUP, CkProcessGroup))
#define CK_PROCESS_GROUP_CLASS(k)       (G_TYPE_CHECK_CLASS_CAST((k), CK_TYPE_PROCESS_GROUP, CkProcessGroupClass))
#define CK_IS_PROCESS_GROUP(o)          (G_TYPE_CHECK_INSTANCE_TYPE ((o), CK_TYPE_PROCESS_GROUP))
#define CK_IS_PROCESS_GROUP_CLASS(k)    (G_TYPE_CHECK_CLASS_TYPE ((k), CK_TYPE_PROCESS_GROUP))
#define CK_PROCESS_GROUP_GET_CLASS(o)   (G_TYPE_INSTANCE_GET_CLASS ((o), CK_TYPE_PROCESS_GROUP, CkProcessGroupClass))

typedef struct CkProcessGroupPrivate CkProcessGroupPrivate;

typedef struct
{
        GObject           parent;
        CkProcessGroupPrivate *priv;
} CkProcessGroup;

typedef struct
{
        GObjectClass parent_class;
} CkProcessGroupClass;


GType             ck_process_group_get_type         (void);

CkProcessGroup   *ck_process_group_get              (void);

gboolean          ck_process_group_create           (CkProcessGroup *pgroup,
                                                     pid_t process,
                                                     const gchar *ssid,
                                                     guint unix_user);

gchar            *ck_process_group_get_ssid         (CkProcessGroup *pgroup,
                                                     pid_t process);

#endif /* __CK_PROCESS_GROUP_H_ */
