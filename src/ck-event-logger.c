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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include "ck-event-logger.h"
#include "ck-log-event.h"

#define CK_EVENT_LOGGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CK_TYPE_EVENT_LOGGER, CkEventLoggerPrivate))

#define DEFAULT_LOG_FILENAME LOCALSTATEDIR "/log/ConsoleKit/history"

struct CkEventLoggerPrivate
{
        int              fd;
        FILE            *file;
        GThread         *writer_thread;
        GAsyncQueue     *event_queue;
        char            *log_filename;
};

enum {
        PROP_0,
        PROP_LOG_FILENAME
};

static void     ck_event_logger_class_init  (CkEventLoggerClass *klass);
static void     ck_event_logger_init        (CkEventLogger      *event_logger);
static void     ck_event_logger_finalize    (GObject            *object);

G_DEFINE_TYPE (CkEventLogger, ck_event_logger, G_TYPE_OBJECT)

GQuark
ck_event_logger_error_quark (void)
{
        static GQuark ret = 0;
        if (ret == 0) {
                ret = g_quark_from_static_string ("ck_event_logger_error");
        }

        return ret;
}

gboolean
ck_event_logger_queue_event (CkEventLogger      *event_logger,
                             CkLogEvent         *event,
                             GError            **error)
{
        CkLogEvent *event_copy;
        gboolean    ret;

        g_return_val_if_fail (CK_IS_EVENT_LOGGER (event_logger), FALSE);
        g_return_val_if_fail (event != NULL, FALSE);

        event_copy = ck_log_event_copy (event);

        g_async_queue_push (event_logger->priv->event_queue,
                            event_copy);

        ret = TRUE;

        return ret;
}

/* Adapted from auditd auditd-event.c */
static gboolean
open_log_file (CkEventLogger *event_logger)
{
        int   flags;
        int   fd;
        int   res;
        char *dirname;

        /*
         * Likely errors on rotate: ENFILE, ENOMEM, ENOSPC
         */
        flags = O_WRONLY | O_APPEND;
#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif

        dirname = g_path_get_dirname (event_logger->priv->log_filename);
        /* always make sure we have a directory */
        errno = 0;
        res = g_mkdir_with_parents (dirname,
                                    S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
        if (res < 0) {
                g_warning ("Unable to create directory %s (%s)",
                           dirname,
                           g_strerror (errno));
                g_free (dirname);
                return FALSE;
        }
        g_free (dirname);

retry:
        errno = 0;
        fd = g_open (event_logger->priv->log_filename, flags, 0600);
        if (fd < 0) {
                if (errno == ENOENT) {
                        fd = g_open (event_logger->priv->log_filename,
                                     O_CREAT | O_EXCL | O_APPEND,
                                     S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                        if (fd < 0) {
                                g_warning ("Couldn't create log file %s (%s)",
                                           event_logger->priv->log_filename,
                                           g_strerror (errno));
                                return FALSE;
                        }

                        close (fd);
                        fd = g_open (event_logger->priv->log_filename, flags, 0600);
                } else if (errno == ENFILE) {
                        /* All system descriptors used, try again... */
                        goto retry;
                }
                if (fd < 0) {
                        g_warning ("Couldn't open log file %s (%s)",
                                   event_logger->priv->log_filename,
                                   g_strerror (errno));
                        return FALSE;
                }
        }

        if (fcntl (fd, F_SETFD, FD_CLOEXEC) == -1) {
                close (fd);
                g_warning ("Error setting log file CLOEXEC flag (%s)",
                           g_strerror (errno));
                return FALSE;
        }

        if (fchown (fd, 0, 0) == -1) {
                close (fd);
                g_warning ("Error setting owner of log file (%s)",
                           g_strerror (errno));
                return FALSE;
        }

        event_logger->priv->file = fdopen (fd, "a");
        if (event_logger->priv->file == NULL) {
                g_warning ("Error setting up log descriptor (%s)",
                           g_strerror (errno));
                close (fd);
                return FALSE;
        }
        event_logger->priv->fd = fd;

        /* Set it to line buffering */
        setlinebuf (event_logger->priv->file);

        return TRUE;
}

static void
reopen_file_stream (CkEventLogger *event_logger)
{
        /* fclose will also close the underlying fd */
        if (event_logger->priv->file != NULL) {
                fclose (event_logger->priv->file);
        }

        /* FIXME: retries */
        open_log_file (event_logger);
}

static void
check_file_stream (CkEventLogger *event_logger)
{
        int         old_fd;
        struct stat old_stats;
        int         new_fd;
        struct stat new_stats;

        old_fd = event_logger->priv->fd;
        if (fstat (old_fd, &old_stats) != 0) {
                g_warning ("Unable to stat file: %s",
                           g_strerror (errno));
                reopen_file_stream (event_logger);
                return;
        }

        new_fd = g_open (event_logger->priv->log_filename, O_RDONLY | O_NONBLOCK, 0600);
        if (new_fd == -1 || fstat (new_fd, &new_stats) < 0) {
                close (new_fd);
                g_debug ("Unable to open or stat %s - will try to reopen", event_logger->priv->log_filename);
                reopen_file_stream (event_logger);
                return;
        }
        close (new_fd);

        if (old_stats.st_ino != new_stats.st_ino || old_stats.st_dev != new_stats.st_dev) {
                g_debug ("File %s has been replaced; writing to end of new file", event_logger->priv->log_filename);
                reopen_file_stream (event_logger);
                return;
        }
}

static gboolean
write_log_for_event (CkEventLogger *event_logger,
                     CkLogEvent    *event)
{
        GString *str;

        str = g_string_new (NULL);

        ck_log_event_to_string (event, str);

        g_debug ("Writing log for event: %s", str->str);
        check_file_stream (event_logger);

        if (event_logger->priv->file != NULL) {
                int         rc;

                rc = fprintf (event_logger->priv->file, "%s\n", str->str);
                if (rc <= 0) {
                        g_warning ("Record was not written to disk (%s)",
                                   g_strerror (errno));
                }
        } else {
                g_warning ("Log file not open for writing");
        }

        g_string_free (str, TRUE);

        return TRUE;
}

static void *
writer_thread_start (CkEventLogger *event_logger)
{
        CkLogEvent *event;

        while (1) {
                event = g_async_queue_pop (event_logger->priv->event_queue);
                if (event == NULL || event->type == CK_LOG_EVENT_NONE) {
                        break;
                }
                write_log_for_event (event_logger, event);
                ck_log_event_free (event);
        }

        g_debug ("Writer thread received None event - exiting");
        return NULL;
}

static void
create_writer_thread (CkEventLogger *event_logger)
{
        GError *error;

        g_debug ("Creating thread for log writing");

        error = NULL;
        event_logger->priv->writer_thread = g_thread_create_full ((GThreadFunc)writer_thread_start,
                                                                  event_logger,
                                                                  65536,
                                                                  TRUE,
                                                                  TRUE,
                                                                  G_THREAD_PRIORITY_NORMAL,
                                                                  &error);
        if (event_logger->priv->writer_thread == NULL) {
                g_debug ("Unable to create thread: %s", error->message);
                g_error_free (error);
        }
}

static void
destroy_writer_thread (CkEventLogger *event_logger)
{
        CkLogEvent event;

        event.type = CK_LOG_EVENT_NONE;

        g_debug ("Destroying writer thread");
        g_async_queue_push (event_logger->priv->event_queue,
                            &event);
#if 1
        g_debug ("Joining writer thread");
        g_thread_join (event_logger->priv->writer_thread);
#endif
}

static GObject *
ck_event_logger_constructor (GType                  type,
                             guint                  n_construct_properties,
                             GObjectConstructParam *construct_properties)
{
        CkEventLogger      *event_logger;
        CkEventLoggerClass *klass;

        klass = CK_EVENT_LOGGER_CLASS (g_type_class_peek (CK_TYPE_EVENT_LOGGER));

        event_logger = CK_EVENT_LOGGER (G_OBJECT_CLASS (ck_event_logger_parent_class)->constructor (type,
                                                                                                    n_construct_properties,
                                                                                                    construct_properties));

        if (open_log_file (event_logger)) {
                create_writer_thread (event_logger);
        }

        return G_OBJECT (event_logger);
}

static void
_ck_event_logger_set_log_filename (CkEventLogger  *event_logger,
                                   const char     *filename)
{
        g_free (event_logger->priv->log_filename);
        event_logger->priv->log_filename = g_strdup (filename);
}

static void
ck_event_logger_set_property (GObject            *object,
                              guint               prop_id,
                              const GValue       *value,
                              GParamSpec         *pspec)
{
        CkEventLogger *self;

        self = CK_EVENT_LOGGER (object);

        switch (prop_id) {
        case PROP_LOG_FILENAME:
                _ck_event_logger_set_log_filename (self, g_value_get_string (value));
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
ck_event_logger_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
        CkEventLogger *self;

        self = CK_EVENT_LOGGER (object);

        switch (prop_id) {
        case PROP_LOG_FILENAME:
                g_value_set_string (value, self->priv->log_filename);
                break;
        default:
                G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
                break;
        }
}

static void
ck_event_logger_class_init (CkEventLoggerClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = ck_event_logger_finalize;
        object_class->constructor = ck_event_logger_constructor;
        object_class->get_property = ck_event_logger_get_property;
        object_class->set_property = ck_event_logger_set_property;

        g_object_class_install_property (object_class,
                                         PROP_LOG_FILENAME,
                                         g_param_spec_string ("log-filename",
                                                              "log-filename",
                                                              "log-filename",
                                                              DEFAULT_LOG_FILENAME,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

        g_type_class_add_private (klass, sizeof (CkEventLoggerPrivate));
}

static void
ck_event_logger_init (CkEventLogger *event_logger)
{
        event_logger->priv = CK_EVENT_LOGGER_GET_PRIVATE (event_logger);

        event_logger->priv->event_queue = g_async_queue_new ();
}

static void
ck_event_logger_finalize (GObject *object)
{
        CkEventLogger *event_logger;

        g_return_if_fail (object != NULL);
        g_return_if_fail (CK_IS_EVENT_LOGGER (object));

        event_logger = CK_EVENT_LOGGER (object);

        g_return_if_fail (event_logger->priv != NULL);

        destroy_writer_thread (event_logger);

        if (event_logger->priv->event_queue != NULL) {
                g_async_queue_unref (event_logger->priv->event_queue);
        }

        if (event_logger->priv->file != NULL) {
                fclose (event_logger->priv->file);
        }

        g_free (event_logger->priv->log_filename);

        G_OBJECT_CLASS (ck_event_logger_parent_class)->finalize (object);
}

CkEventLogger *
ck_event_logger_new (const char *filename)
{
        GObject *object;

        object = g_object_new (CK_TYPE_EVENT_LOGGER,
                               "log-filename", filename,
                               NULL);

        return CK_EVENT_LOGGER (object);
}
