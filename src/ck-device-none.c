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

#include "config.h"

#include <glib.h>
#include <glib-object.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>


/* For TRACE */
#include "ck-sysdeps.h"

#include "ck-device.h"



static void     ck_device_class_init  (CkDeviceClass *klass);
static void     ck_device_init        (CkDevice      *device);
static void     ck_device_finalize    (GObject       *object);



struct _CkDevice
{
        GObject parent_instance;
};


G_DEFINE_TYPE (CkDevice, ck_device, G_TYPE_OBJECT)


static void
ck_device_class_init (CkDeviceClass *klass)
{
        GObjectClass *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = ck_device_finalize;
}


static void
ck_device_init (CkDevice *device)
{
}


static void
ck_device_finalize (GObject *object)
{
        G_OBJECT_CLASS (ck_device_parent_class)->finalize (object);
}


CkDevice*
ck_device_new (guint    major,
               guint    minor,
               gboolean active)
{
                return NULL;
}


void
ck_device_set_active (CkDevice *device,
                      gboolean  active)
{
}


gboolean
ck_device_get_active (CkDevice *device)
{
        return FALSE;
}


guint
ck_device_get_major (CkDevice *device)
{
        return 0;
}


guint
ck_device_get_minor (CkDevice *device)
{
        return 0;
}


gint
ck_device_get_fd (CkDevice *device)
{
        return -1;
}


CkDeviceCategory
ck_device_get_category (CkDevice *device)
{
        return DEVICE_OTHER;
}

gboolean
ck_device_compare_devices (CkDevice *device1,
                           CkDevice *device2)
{
        return FALSE;
}

gboolean
ck_device_compare (CkDevice *device,
                   guint     major,
                   guint     minor)
{
        return FALSE;
}

gboolean
ck_device_is_server_managed (void)
{
        return FALSE;
}
