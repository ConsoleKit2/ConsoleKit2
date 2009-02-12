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

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/inotify.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include "ck-file-monitor.h"

#define CK_FILE_MONITOR_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CK_TYPE_FILE_MONITOR, CkFileMonitorPrivate))

typedef struct
{
        int     wd;
        char   *path;
        GSList *notifies;
} FileInotifyWatch;

typedef struct
{
        guint                   id;
        int                     mask;
        CkFileMonitorNotifyFunc notify_func;
        gpointer                user_data;
        FileInotifyWatch       *watch;
} FileMonitorNotify;

typedef struct
{
        int                wd;
        CkFileMonitorEvent event;
        char              *path;
} FileMonitorEventInfo;

#define DEFAULT_NOTIFY_BUFLEN (32 * (sizeof (struct inotify_event) + 16))
#define MAX_NOTIFY_BUFLEN     (32 * DEFAULT_NOTIFY_BUFLEN)

struct CkFileMonitorPrivate
{
        guint       serial;

        gboolean    initialized_inotify;

        int         inotify_fd;
        guint       io_watch;

        GHashTable *wd_to_watch;
        GHashTable *path_to_watch;
        GHashTable *notifies;

        guint       buflen;
        guchar     *buffer;

        guint       events_idle_id;
        guint       remove_idle_id;
        GQueue     *notify_events;
        GQueue     *remove_events;
};

enum {
        PROP_0,
};

static void     ck_file_monitor_class_init  (CkFileMonitorClass *klass);
static void     ck_file_monitor_init        (CkFileMonitor      *file_monitor);
static void     ck_file_monitor_finalize    (GObject            *object);

G_DEFINE_TYPE (CkFileMonitor, ck_file_monitor, G_TYPE_OBJECT)

static gpointer monitor_object = NULL;

GQuark
ck_file_monitor_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("ck_file_monitor_error");
        }

        return ret;
}

/* most of this is adapted from libgnome-menu */

static int
our_event_mask_to_inotify_mask (int our_mask)
{
        int mask;

        mask = 0;

        if (our_mask & CK_FILE_MONITOR_EVENT_ACCESS) {
                mask |= IN_ACCESS;
        }

        if (our_mask & CK_FILE_MONITOR_EVENT_CREATE) {
                mask |= IN_CREATE | IN_MOVED_TO;
        }

        if (our_mask & CK_FILE_MONITOR_EVENT_DELETE) {
                mask |= IN_DELETE | IN_DELETE_SELF | IN_MOVED_FROM | IN_MOVE_SELF;
        }

        if (our_mask & CK_FILE_MONITOR_EVENT_CHANGE) {
                mask |= IN_MODIFY | IN_ATTRIB;
        }

        return mask;
}

static char *
imask_to_string (guint32 mask)
{
        GString *out;

        out = g_string_new (NULL);

        if (mask & IN_ACCESS) {
                g_string_append (out, "ACCESS ");
        }
        if (mask & IN_MODIFY) {
                g_string_append (out, "MODIFY ");
        }
        if (mask & IN_ATTRIB) {
                g_string_append (out, "ATTRIB ");
        }
        if (mask & IN_CLOSE_WRITE) {
                g_string_append (out, "CLOSE_WRITE ");
        }
        if (mask & IN_CLOSE_NOWRITE) {
                g_string_append (out, "CLOSE_NOWRITE ");
        }
        if (mask & IN_OPEN) {
                g_string_append (out, "OPEN ");
        }
        if (mask & IN_MOVED_FROM) {
                g_string_append (out, "MOVED_FROM ");
        }
        if (mask & IN_MOVED_TO) {
                g_string_append (out, "MOVED_TO ");
        }
        if (mask & IN_DELETE) {
                g_string_append (out, "DELETE ");
        }
        if (mask & IN_CREATE) {
                g_string_append (out, "CREATE ");
        }
        if (mask & IN_DELETE_SELF) {
                g_string_append (out, "DELETE_SELF ");
        }
        if (mask & IN_UNMOUNT) {
                g_string_append (out, "UNMOUNT ");
        }
        if (mask & IN_Q_OVERFLOW) {
                g_string_append (out, "Q_OVERFLOW ");
        }
        if (mask & IN_IGNORED) {
                g_string_append (out, "IGNORED ");
        }

        return g_string_free (out, FALSE);
}

static FileInotifyWatch *
file_monitor_add_watch_for_path (CkFileMonitor *monitor,
                                 const char    *path,
                                 int            mask)

{
        FileInotifyWatch *watch;
        int               wd;
        int               imask;
        char             *mask_str;

        imask = our_event_mask_to_inotify_mask (mask);

        mask_str = imask_to_string (imask);
        g_debug ("adding inotify watch %s", mask_str);
        g_free (mask_str);

        wd = inotify_add_watch (monitor->priv->inotify_fd, path, IN_MASK_ADD | imask);
        if (wd < 0) {
                /* FIXME: remove watch etc */
                return NULL;
        }

        watch = g_hash_table_lookup (monitor->priv->path_to_watch, path);
        if (watch == NULL) {
                watch = g_new0 (FileInotifyWatch, 1);

                watch->wd = wd;
                watch->path = g_strdup (path);

                g_hash_table_insert (monitor->priv->path_to_watch, watch->path, watch);
                g_hash_table_insert (monitor->priv->wd_to_watch, GINT_TO_POINTER (wd), watch);
        }

        return watch;
}

static void
monitor_release_watch (CkFileMonitor    *monitor,
                       FileInotifyWatch *watch)
{
        g_slist_free (watch->notifies);
        watch->notifies = NULL;

        g_free (watch->path);
        watch->path = NULL;

        inotify_rm_watch (monitor->priv->inotify_fd, watch->wd);
        watch->wd = -1;
}

static void
file_monitor_remove_watch (CkFileMonitor    *monitor,
                           FileInotifyWatch *watch)
{
        if (watch->wd == -1) {
                return;
        }

        g_hash_table_remove (monitor->priv->path_to_watch,
                             watch->path);
        g_hash_table_remove (monitor->priv->wd_to_watch,
                             GINT_TO_POINTER (watch->wd));
        monitor_release_watch (monitor, watch);
}

static gboolean
remove_watch_foreach (const char       *path,
                      FileInotifyWatch *watch,
                      CkFileMonitor    *monitor)
{
        monitor_release_watch (monitor, watch);
        return TRUE;
}

static void
close_inotify (CkFileMonitor *monitor)
{
        if (! monitor->priv->initialized_inotify) {
                return;
        }

        monitor->priv->initialized_inotify = FALSE;

        g_hash_table_foreach_remove (monitor->priv->path_to_watch,
                                     (GHRFunc) remove_watch_foreach,
                                     monitor);
        monitor->priv->path_to_watch = NULL;

        if (monitor->priv->wd_to_watch != NULL) {
                g_hash_table_destroy (monitor->priv->wd_to_watch);
        }
        monitor->priv->wd_to_watch = NULL;

        g_free (monitor->priv->buffer);
        monitor->priv->buffer = NULL;
        monitor->priv->buflen = 0;

        if (monitor->priv->io_watch) {
                g_source_remove (monitor->priv->io_watch);
        }
        monitor->priv->io_watch = 0;

        if (monitor->priv->inotify_fd > 0) {
                close (monitor->priv->inotify_fd);
        }
        monitor->priv->inotify_fd = 0;
}

static gboolean
emit_events_in_idle (CkFileMonitor *monitor)
{
        FileMonitorEventInfo *event_info;
        char                 *path;
        char                 *freeme;

        monitor->priv->events_idle_id = 0;

        while ((event_info = g_queue_pop_head (monitor->priv->notify_events)) != NULL) {
                GSList           *l;
                FileInotifyWatch *watch;

                watch = g_hash_table_lookup (monitor->priv->wd_to_watch,
                                             GINT_TO_POINTER (event_info->wd));

                if (watch != NULL) {
                    for (l = watch->notifies; l != NULL; l = l->next) {
                            FileMonitorNotify *notify;

                            notify = g_hash_table_lookup (monitor->priv->notifies,
                                                          GUINT_TO_POINTER (l->data));
                            if (notify == NULL) {
                                    continue;
                            }

                            if (! (notify->mask & event_info->event)) {
                                    continue;
                            }

                            if (notify->notify_func) {
                                    freeme = NULL;
                                    if (event_info->path != NULL) {
                                            path = freeme = g_build_filename (watch->path, event_info->path, NULL);
                                    } else {
                                            path = watch->path;
                                    }
                                    notify->notify_func (monitor, event_info->event, path, notify->user_data);
                                    if (freeme != NULL) {
                                        g_free (freeme);
                                    }
                            }
                    }
                }

                event_info->path = NULL;
                event_info->event = CK_FILE_MONITOR_EVENT_NONE;

                g_free (event_info);
        }

        return FALSE;
}

static gboolean
emit_removals_in_idle (CkFileMonitor *monitor)
{
        gpointer          wd_p;
        FileInotifyWatch *watch;
        GSList           *l;

        monitor->priv->remove_idle_id = 0;

        while ((wd_p = g_queue_pop_head (monitor->priv->remove_events)) != NULL) {
                watch = g_hash_table_lookup (monitor->priv->wd_to_watch, wd_p);
                if (watch && watch->wd != -1) {
                    for (l = watch->notifies; l != NULL; l = l->next) {
                            FileMonitorNotify *notify;

                            notify = g_hash_table_lookup (monitor->priv->notifies,
                                                          GUINT_TO_POINTER (l->data));
                            if (notify == NULL) {
                                    continue;
                            }
                            notify->watch = NULL;
                    }
                    file_monitor_remove_watch (monitor, watch);
                    g_free (watch);
                }
        }

        return FALSE;
}

static void
file_monitor_queue_event (CkFileMonitor        *monitor,
                          FileMonitorEventInfo *event_info)
{
        g_queue_push_tail (monitor->priv->notify_events, event_info);

        if (monitor->priv->events_idle_id == 0) {
                monitor->priv->events_idle_id = g_idle_add ((GSourceFunc) emit_events_in_idle, monitor);
        }
}

static void
queue_watch_event (CkFileMonitor     *monitor,
                   int                wd,
                   CkFileMonitorEvent event,
                   const char        *path)
{
        FileMonitorEventInfo *event_info;

        event_info = g_new0 (FileMonitorEventInfo, 1);

        event_info->wd      = wd;
        event_info->path    = g_strdup (path);
        event_info->event   = event;

        file_monitor_queue_event (monitor, event_info);
}

static void
queue_remove_event (CkFileMonitor *monitor,
                    int            wd)
{
        g_queue_push_tail (monitor->priv->remove_events, GINT_TO_POINTER (wd));

        if (monitor->priv->remove_idle_id == 0) {
                monitor->priv->remove_idle_id = g_idle_add ((GSourceFunc) emit_removals_in_idle, monitor);
        }
}

static void
handle_inotify_event (CkFileMonitor        *monitor,
                      struct inotify_event *ievent)
{
        CkFileMonitorEvent  event;
        const char         *path;
        char               *freeme;
        char               *mask_str;

        freeme = NULL;

        if (ievent->len > 0) {
                path = ievent->name;
        } else {
                path = NULL;
        }

        mask_str = imask_to_string (ievent->mask);
        g_debug ("handing inotify event %s for %s", mask_str, path);
        g_free (mask_str);

        event = CK_FILE_MONITOR_EVENT_NONE;

        if (ievent->mask & (IN_CREATE | IN_MOVED_TO)) {
                event = CK_FILE_MONITOR_EVENT_CREATE;
        } else if (ievent->mask & (IN_DELETE | IN_DELETE_SELF | IN_MOVED_FROM | IN_MOVE_SELF)) {
                event = CK_FILE_MONITOR_EVENT_DELETE;
        } else if (ievent->mask & (IN_MODIFY | IN_ATTRIB)) {
                event = CK_FILE_MONITOR_EVENT_CHANGE;
        } else if (ievent->mask & IN_ACCESS) {
                event = CK_FILE_MONITOR_EVENT_ACCESS;
        }

        if (event != CK_FILE_MONITOR_EVENT_NONE) {
                queue_watch_event (monitor, ievent->wd, event, path);
        }

        if (ievent->mask & IN_IGNORED) {
                queue_remove_event (monitor, ievent->wd);
        }
}

static gboolean
inotify_data_pending (GIOChannel    *source,
                      GIOCondition   condition,
                      CkFileMonitor *monitor)
{
        int len;
        int i;

        g_debug ("Inotify data pending");

        g_assert (monitor->priv->inotify_fd > 0);
        g_assert (monitor->priv->buffer != NULL);

        do {
                while ((len = read (monitor->priv->inotify_fd, monitor->priv->buffer, monitor->priv->buflen)) < 0 && errno == EINTR);

                if (len > 0) {
                        break;
                } else if (len < 0) {
                        g_warning ("Error reading inotify event: %s",
                                   g_strerror (errno));
                        goto error_cancel;
                }

                g_assert (len == 0);

                if ((monitor->priv->buflen << 1) > MAX_NOTIFY_BUFLEN) {
                        g_warning ("Error reading inotify event: Exceded maximum buffer size");
                        goto error_cancel;
                }

                g_debug ("Buffer size %u too small, trying again at %u\n",
                         monitor->priv->buflen, monitor->priv->buflen << 1);

                monitor->priv->buflen <<= 1;
                monitor->priv->buffer = g_realloc (monitor->priv->buffer, monitor->priv->buflen);
        } while (TRUE);

        g_debug ("Inotify buffer filled");

        i = 0;
        while (i < len) {
                struct inotify_event *ievent = (struct inotify_event *) &monitor->priv->buffer [i];
                FileInotifyWatch     *watch;

                g_debug ("Got event wd = %d, mask = 0x%x, cookie = %d, len = %d, name= %s\n",
                         ievent->wd,
                         ievent->mask,
                         ievent->cookie,
                         ievent->len,
                         ievent->len > 0 ? ievent->name : "<none>");

                watch = g_hash_table_lookup (monitor->priv->wd_to_watch,
                                             GINT_TO_POINTER (ievent->wd));
                if (watch != NULL) {
                        handle_inotify_event (monitor, ievent);
                }

                i += sizeof (struct inotify_event) + ievent->len;
        }

        return TRUE;

 error_cancel:
        monitor->priv->io_watch = 0;

        close_inotify (monitor);

        return FALSE;
}

static FileMonitorNotify *
file_monitor_add_notify_for_path (CkFileMonitor          *monitor,
                                  const char             *path,
                                  int                     mask,
                                  CkFileMonitorNotifyFunc notify_func,
                                  gpointer                data)
{
        FileMonitorNotify *notify;
        FileInotifyWatch  *watch;

        notify = NULL;

        watch = file_monitor_add_watch_for_path (monitor, path, mask);
        if (watch != NULL) {
                notify = g_new0 (FileMonitorNotify, 1);
                notify->notify_func = notify_func;
                notify->user_data = data;
                notify->id = monitor->priv->serial++;
                notify->watch = watch;
                notify->mask = mask;

                g_debug ("Adding notify for %s mask:%d", path, mask);

                g_hash_table_insert (monitor->priv->notifies, GUINT_TO_POINTER (notify->id), notify);
                watch->notifies = g_slist_prepend (watch->notifies, GUINT_TO_POINTER (notify->id));
        }

        return notify;
}

static void
file_monitor_remove_notify (CkFileMonitor *monitor,
                            guint          id)
{
        FileMonitorNotify *notify;

        g_debug ("removing notify for %u", id);

        notify = g_hash_table_lookup (monitor->priv->notifies,
                                      GUINT_TO_POINTER (id));
        if (notify == NULL) {
                return;
        }

        g_hash_table_steal (monitor->priv->notifies,
                            GUINT_TO_POINTER (id));

        if (notify->watch) {
            notify->watch->notifies = g_slist_remove (notify->watch->notifies, GUINT_TO_POINTER (id));

            if (g_slist_length (notify->watch->notifies) == 0) {
                    file_monitor_remove_watch (monitor, notify->watch);
                    g_free (notify->watch);
            }
        }

        g_free (notify);
}

guint
ck_file_monitor_add_notify (CkFileMonitor          *monitor,
                            const char             *path,
                            int                     mask,
                            CkFileMonitorNotifyFunc notify_func,
                            gpointer                data)
{
        FileMonitorNotify *notify;

        if (! monitor->priv->initialized_inotify) {
                return 0;
        }

        notify = file_monitor_add_notify_for_path (monitor,
                                                   path,
                                                   mask,
                                                   notify_func,
                                                   data);
        if (notify == NULL) {
                g_warning ("Failed to add monitor on '%s': %s",
                           path,
                           g_strerror (errno));
                return 0;
        }

        return notify->id;
}

void
ck_file_monitor_remove_notify (CkFileMonitor *monitor,
                               guint          id)
{
        if (! monitor->priv->initialized_inotify) {
                return;
        }

        file_monitor_remove_notify (monitor, id);
}

static void
ck_file_monitor_class_init (CkFileMonitorClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = ck_file_monitor_finalize;

        g_type_class_add_private (klass, sizeof (CkFileMonitorPrivate));
}


static void
setup_inotify (CkFileMonitor *monitor)
{
        GIOChannel *io_channel;
        int         fd;

        if (monitor->priv->initialized_inotify) {
                return;
        }

        if ((fd = inotify_init ()) < 0) {
                g_warning ("Failed to initialize inotify: %s",
                           g_strerror (errno));
                return;
        }

        monitor->priv->inotify_fd = fd;

        io_channel = g_io_channel_unix_new (fd);
        monitor->priv->io_watch = g_io_add_watch (io_channel,
                                                  G_IO_IN|G_IO_PRI,
                                                  (GIOFunc) inotify_data_pending,
                                                  monitor);
        g_io_channel_unref (io_channel);

        monitor->priv->buflen = DEFAULT_NOTIFY_BUFLEN;
        monitor->priv->buffer = g_malloc (DEFAULT_NOTIFY_BUFLEN);

        monitor->priv->notifies = g_hash_table_new (g_direct_hash,
                                                    g_direct_equal);

        monitor->priv->wd_to_watch = g_hash_table_new (g_direct_hash,
                                                       g_direct_equal);
        monitor->priv->path_to_watch = g_hash_table_new (g_str_hash,
                                                         g_str_equal);

        monitor->priv->initialized_inotify = TRUE;
}

static void
ck_file_monitor_init (CkFileMonitor *monitor)
{
        monitor->priv = CK_FILE_MONITOR_GET_PRIVATE (monitor);

        monitor->priv->serial = 1;
        monitor->priv->notify_events = g_queue_new ();
        monitor->priv->remove_events = g_queue_new ();

        setup_inotify (monitor);
}

static void
ck_file_monitor_finalize (GObject *object)
{
        CkFileMonitor *monitor;

        g_return_if_fail (object != NULL);
        g_return_if_fail (CK_IS_FILE_MONITOR (object));

        monitor = CK_FILE_MONITOR (object);

        g_return_if_fail (monitor->priv != NULL);

        close_inotify (monitor);

        g_hash_table_destroy (monitor->priv->notifies);
        g_queue_free (monitor->priv->notify_events);
        g_queue_free (monitor->priv->remove_events);

        G_OBJECT_CLASS (ck_file_monitor_parent_class)->finalize (object);
}

CkFileMonitor *
ck_file_monitor_new (void)
{
        if (monitor_object != NULL) {
                g_object_ref (monitor_object);
        } else {
                monitor_object = g_object_new (CK_TYPE_FILE_MONITOR, NULL);

                g_object_add_weak_pointer (monitor_object,
                                           (gpointer *) &monitor_object);
        }

        return CK_FILE_MONITOR (monitor_object);
}
