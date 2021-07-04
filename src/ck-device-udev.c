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
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <stdint.h>
#include <errno.h>

#ifdef HAVE_LINUX_INPUT_H
#include <linux/input.h>
#endif

#ifdef HAVE_LIBDRM
#include <libdrm/drm.h>
#endif

#ifdef HAVE_SYS_SYSMACROS_H
#include <sys/sysmacros.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_LIBUDEV_H
#include <libudev.h>
#endif

#ifdef HAVE_DEVATTR_H
#include <devattr.h>
#endif

#ifdef HAVE_LIBPROP_PROPLIB_H
#include <libprop/proplib.h>
#endif

#ifdef HAVE_SYS_MKDEV_H
#include <sys/mkdev.h>
#endif

#ifdef HAVE_LIBEVDEV_H
#include "libevdev.h"
#endif

/* For TRACE */
#include "ck-sysdeps.h"

#include "ck-device.h"

/* We may not have the evdev header because of license issues, so
 * we'll define the symbol here */
#ifndef EVIOCREVOKE
#define EVIOCREVOKE _IOW('E', 0x91, int)
#endif


static void     ck_device_finalize    (GObject       *object);



struct _CkDevice
{
        GObject             parent_instance;
        guint               major;
        guint               minor;
        struct udev_device *udevice;
        gchar              *devnode;
        CkDeviceCategory    category;
        gint                fd;
        gboolean            state;
};

static struct udev *dev = NULL;


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
        device->fd = -1;
}


static void
ck_device_finalize (GObject *object)
{
        CkDevice *device = CK_DEVICE (object);

        /* Always revoke/drop master before we are removed */
        ck_device_set_active (device, FALSE);

        if (device->udevice != NULL) {
                udev_device_unref (device->udevice);
        }

        if (device->fd >= 0) {
                g_close (device->fd, NULL);
        }
        g_free (device->devnode);

        G_OBJECT_CLASS (ck_device_parent_class)->finalize (object);
}


static gboolean
ck_device_evdev_revoke (gint fd)
{
        errno = 0;

        if (fd < 0) {
                return FALSE;
        }

        if (ioctl (fd, EVIOCREVOKE, NULL) == -1) {
                g_warning ("failed to revoke access. Error: %s", g_strerror(errno));
                return FALSE;
        }

        return TRUE;
}


static gboolean
change_drm_master (gint fd,
                   gulong request)
{
        errno = 0;

        if (fd < 0) {
                return FALSE;
        }

        if (ioctl (fd, request, NULL) == -1) {
                g_warning ("failed to change drm master setting. Error: %s", g_strerror(errno));
                return FALSE;
        }

        return TRUE;
}


static gint
ck_device_drm_drop_master (gint fd)
{
        TRACE ();

        return change_drm_master (fd, DRM_IOCTL_DROP_MASTER);
}


static gboolean
ck_device_drm_set_master (gint fd)
{
        TRACE ();

        return change_drm_master (fd, DRM_IOCTL_SET_MASTER);
}


static gint
ck_device_open (CkDevice *device,
                gboolean  active)
{
        gint fd = -1;

        TRACE ();

        if (device == NULL || device->udevice == NULL || device->devnode == NULL) {
                g_debug ("invalid device");
                return -1;
        }

        g_debug ("opening devnode: %s", device->devnode);

        fd = g_open (device->devnode,
                     O_RDWR|O_CLOEXEC|O_NOCTTY|O_NONBLOCK,
                     S_IRUSR|S_IWUSR);

        if (fd < 0)
        {
                g_debug ("failed to open device");
                return -1;
        }

        switch (device->category)
        {
        case DEVICE_DRM:
                if (active)
                {
                        if (!ck_device_drm_set_master (fd))
                        {
                                g_debug ("failed to call drm set master");
                                g_close (fd, NULL);
                                return -1;
                        }
                }
                else
                {
                        ck_device_drm_drop_master (fd);
                }
                break;
        case DEVICE_EVDEV:
                if (active == FALSE)
                {
                        ck_device_evdev_revoke (fd);
                }
                break;
        default:
                g_debug ("ck_device_open: Device type not supported");
                break;
        }

        return fd;
}

static struct udev_device*
ck_device_get_udevice_from_devnum (guint     major,
                                   guint     minor)
{
#if defined(HAVE_DEVATTR_H)
        struct udev_enumerate  *enumerate;
        struct udev_list_entry *current;
        struct udev_device     *udevice = NULL;
        prop_dictionary_t       dict;
        gchar                   majorbuf[16];
        gchar                   minorbuf[16];

        enumerate = udev_enumerate_new (dev);
        if (enumerate == NULL) {
                g_warning ("ck_device_get_udevice_from_devnum: failed to call udev_enumerate_new");
                return NULL;
        }

        memset (majorbuf, 0, sizeof(majorbuf));
        memset (minorbuf, 0, sizeof(minorbuf));
        g_snprintf (majorbuf, 15, "%d", major);
        g_snprintf (minorbuf, 15, "%d", minor);
        udev_enumerate_add_match_expr (enumerate, "major", majorbuf);
        udev_enumerate_add_match_expr (enumerate, "minor", minorbuf);

        if (udev_enumerate_scan_devices (enumerate) != 0) {
                /* failed to scan devices */
                udev_enumerate_unref (enumerate);
                return NULL;
        }

        current = udev_enumerate_get_list_entry (enumerate);
        if (current == NULL) {
                /* No device with that major/minor */
                udev_enumerate_unref (enumerate);
                return NULL;
        }

        udev_list_entry_foreach (current, current)
        {
                udevice = udev_list_entry_get_device (current);
                if (udevice == NULL) {
                        continue;
                }

                dict = udev_device_get_dictionary (udevice);
                if (dict == NULL) {
                        /* Why is there a device with no properties? */
                        udevice = NULL;
                        continue;
                }

                /* found our device, create our ref */
                udev_device_ref (udevice);
                break;
        }

        udev_enumerate_unref (enumerate);

        return udevice;
#else
        return udev_device_new_from_devnum (dev, 'c', makedev (major, minor));
#endif
}

static gchar*
udevice_get_device_name (struct udev_device *udevice)
{
#if defined(HAVE_DEVATTR_H)
        gchar            *name;
        prop_dictionary_t dict;

        if (udevice == NULL) {
                return NULL;
        }

        dict = udev_device_get_dictionary (udevice);
        if (dict == NULL) {
                return NULL;
        }

        prop_dictionary_get_cstring (dict, "driver", &name);

        return name;
#else
        return g_strdup (udev_device_get_sysname (udevice));
#endif
}

CkDevice*
ck_device_new (guint    major,
               guint    minor,
               gboolean active)
{
        CkDevice    *device;
        const gchar *subsystem;
        const gchar *sysname;

        TRACE ();

        /* We create a single udev instance */
        if (dev == NULL)
        {
                dev = udev_new ();
                if (dev == NULL)
                {
                        return NULL;
                }
        }

        device = g_object_new (CK_TYPE_DEVICE, NULL);
        device->major = major;
        device->minor = minor;
        device->state = active;
        device->udevice = ck_device_get_udevice_from_devnum (major, minor);
        if (device->udevice == NULL)
        {
                g_warning ("failed to get a udev device, it probably doesn't exist");
                g_object_unref (device);
                return NULL;
        }

        subsystem = udev_device_get_subsystem (device->udevice);
        sysname = udevice_get_device_name (device->udevice);
        device->devnode = g_strdup (udev_device_get_devnode (device->udevice));

        g_debug ("major %d minor %d subsystem %s sysname %s devnode %s active ? %s",
                 major, minor, subsystem, sysname, device->devnode,
                 active ? "TRUE" : "FALSE");

        /* Start with other device as a default, we have special things
         * we do with DRM and EVDEV devices so find and tag them */
        device->category = DEVICE_OTHER;
        if ((g_strcmp0 (subsystem, "drm") == 0 && g_str_has_prefix (sysname, "card"))
#ifndef __linux__
                        /* on BSD, the dri/card0 -> drm/0 symlink gets resolved,
                         * and subsystem is not emulated by libudev-devd */
                        || strstr (device->devnode, "drm") != NULL
#endif
           )
        {
                g_debug ("DEVICE_DRM");
                device->category = DEVICE_DRM;
        }
        else if (g_strcmp0 (subsystem, "input") == 0)
        {
                if (g_str_has_prefix (sysname, "event"))
                {
                        g_debug ("DEVICE_EVDEV");
                        device->category = DEVICE_EVDEV;
                }
        }

        if (device->category != DEVICE_OTHER)
        {
                /* we don't do device management for anything but
                 * DRM/EVDEV */
                device->fd = ck_device_open (device, active);
                if (device->fd < 0 && active)
                {
                        /* try again in an inactive state */
                        device->fd = ck_device_open (device, FALSE);
                }
        }

        return device;
}


void
ck_device_set_active (CkDevice *device,
                      gboolean  active)
{
        TRACE ();

        if (device == NULL)
                return;

        if (device->state == active)
                return;

        device->state = active;

        switch (device->category)
        {
        case DEVICE_DRM:
                if (active)
                {
                        ck_device_drm_set_master (device->fd);
                }
                else
                {
                        ck_device_drm_drop_master (device->fd);
                }
                break;
        case DEVICE_EVDEV:
                if (active)
                {
                        /* close the old fd since it was revoked when
                         * inactive */
                        g_close (device->fd, NULL);
                        device->fd = ck_device_open (device, active);
                }
                else
                {
                        ck_device_evdev_revoke (device->fd);
                }
                break;
        default:
                g_warning ("ck_device_set_active: Device type not supported");
                break;
        }
}


gboolean
ck_device_get_active (CkDevice *device)
{
        return device->state;
}


guint
ck_device_get_major (CkDevice *device)
{
        return device->major;
}


guint
ck_device_get_minor (CkDevice *device)
{
        return device->minor;
}


gint
ck_device_get_fd (CkDevice *device)
{
        return device->fd;
}


CkDeviceCategory
ck_device_get_category (CkDevice *device)
{
        return device->category;
}


gboolean
ck_device_compare_devices (CkDevice *device1,
                           CkDevice *device2)
{
        if (device1 == NULL || device2 == NULL)
        {
                return FALSE;
        }
        if (device1->major != device2->major)
        {
                return FALSE;
        }
        if (device1->minor != device2->minor)
        {
                return FALSE;
        }
        return TRUE;
}

gboolean
ck_device_compare (CkDevice *device,
                   guint     major,
                   guint     minor)
{
        if (device == NULL)
        {
                return FALSE;
        }
        if (device->major != major)
        {
                return FALSE;
        }
        if (device->minor != minor)
        {
                return FALSE;
        }
        return TRUE;
}

gboolean
ck_device_is_server_managed (void)
{
        return TRUE;
}
