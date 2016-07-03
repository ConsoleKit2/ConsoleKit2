/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (c) 2016, Eric Koegel <eric.koegel@gmail.com>
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

#ifndef CK_DEVICE_H_
#define CK_DEVICE_H_

#include <glib-object.h>

#define CK_TYPE_DEVICE           (ck_device_get_type ())
#define CK_DEVICE(o)             (G_TYPE_CHECK_INSTANCE_CAST ((o), CK_TYPE_DEVICE, CkDevice))
#define CK_DEVICE_CLASS(k)       (G_TYPE_CHECK_CLASS_CAST((k), CK_TYPE_DEVICE, CkDeviceClass))
#define CK_IS_DEVICE(o)          (G_TYPE_CHECK_INSTANCE_TYPE ((o), CK_TYPE_DEVICE))
#define CK_IS_DEVICE_CLASS(k)    (G_TYPE_CHECK_CLASS_TYPE ((k), CK_TYPE_DEVICE))
#define CK_DEVICE_GET_CLASS(o)   (G_TYPE_INSTANCE_GET_CLASS ((o), CK_TYPE_DEVICE, CkDeviceClass))


typedef enum
{
        DEVICE_DRM,
        DEVICE_EVDEV,
        DEVICE_OTHER
} CkDeviceCategory;

typedef struct _CkDevice CkDevice;

typedef struct
{
        GObjectClass parent_class;
} CkDeviceClass;


GType             ck_device_get_type                    (void);

CkDevice         *ck_device_new                         (guint     major,
                                                         guint     minor,
                                                         gboolean  active);

void              ck_device_set_active                  (CkDevice *device,
                                                         gboolean  active);
gboolean          ck_device_get_active                  (CkDevice *device);
guint             ck_device_get_major                   (CkDevice *device);
guint             ck_device_get_minor                   (CkDevice *device);
CkDeviceCategory  ck_device_get_category                (CkDevice *device);
gint              ck_device_get_fd                      (CkDevice *device);

gboolean          ck_device_compare_devices             (CkDevice *device1,
                                                         CkDevice *device2);
gboolean          ck_device_compare                     (CkDevice *device,
                                                         guint     major,
                                                         guint     minor);

gboolean          ck_device_is_server_managed           (void);

#endif /* CK_DEVICE_H_ */
