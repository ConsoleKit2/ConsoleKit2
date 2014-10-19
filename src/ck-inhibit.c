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

#include "config.h"

#include <stdlib.h>
#include <stdio.h>

#include <glib.h>
#include <glib-object.h>
#include <glib-unix.h>
#include <glib/gstdio.h>

#include "ck-inhibit.h"
#include "ck-marshal.h"

#define CK_INHIBIT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CK_TYPE_INHIBIT, CkInhibitPrivate))

struct CkInhibitPrivate
{
        /*
         * Who is a human-readable, descriptive string of who is taking
         * the lock. Example: "Xfburn"
         * We use this as unique identifier for the lock since an app
         * isn't supposed to have multiple locks.
         */
        const gchar *who;
        /* inhibitors is an array of which events to suppress.
         * Setting an CkInhibitEvent value inside the array to TRUE
         * marks it for suppression. */
        gboolean inhibitors[CK_INHIBIT_EVENT_LAST];
        /*
         * Why is a human-readable, descriptive string of why the program
         * is taking the lock. Example: "Burning a DVD, interrupting now
         * will ruin the DVD."
         */
        const gchar *why;
        /*
         * named_pipe is a named pipe that the user app will hold onto
         * while they want the lock to be held. When they close all
         * references to the named pipe then the lock is released and
         * this object can be destroyed.
         */
        gint         named_pipe;
        /* named_pipe_path is the location the named pipe is created from */
        gchar *named_pipe_path;
        /* fd_source is the event source id for the g_unix_fd_add call */
        gint fd_source;
};


typedef enum {
        SIG_CHANGED_EVENT,
        SIG_N_SIGNALS,
} INHIBIT_SIGNALS;

static guint __signals[SIG_N_SIGNALS] = { 0, };



static void     ck_inhibit_class_init  (CkInhibitClass *klass);
static void     ck_inhibit_init        (CkInhibit      *inhibit);
static void     ck_inhibit_finalize    (GObject        *object);

static void     close_named_pipe (CkInhibit *inhibit);

G_DEFINE_TYPE (CkInhibit, ck_inhibit, G_TYPE_OBJECT)


static void
ck_inhibit_class_init (CkInhibitClass *klass)
{
        GObjectClass   *object_class = G_OBJECT_CLASS (klass);

        object_class->finalize = ck_inhibit_finalize;

        g_type_class_add_private (klass, sizeof (CkInhibitPrivate));

        __signals[SIG_CHANGED_EVENT] = g_signal_new("changed-event",
                                                    G_OBJECT_CLASS_TYPE (object_class),
                                                    G_SIGNAL_RUN_LAST,
                                                    G_STRUCT_OFFSET (CkInhibitClass, changed_event),
                                                    NULL, NULL,
                                                    ck_marshal_VOID__INT_BOOLEAN,
                                                    G_TYPE_NONE,
                                                    2, G_TYPE_INT, G_TYPE_BOOLEAN);
}

static void
ck_inhibit_init (CkInhibit *inhibit)
{
        inhibit->priv = CK_INHIBIT_GET_PRIVATE (inhibit);

        inhibit->priv->named_pipe = -1;
}

static void
ck_inhibit_finalize (GObject *object)
{
        CkInhibit *inhibit;
        CkInhibitPrivate *priv;

        g_return_if_fail (object != NULL);
        g_return_if_fail (CK_IS_INHIBIT (object));

        inhibit = CK_INHIBIT (object);

        g_return_if_fail (inhibit->priv != NULL);

        priv = CK_INHIBIT_GET_PRIVATE (inhibit);

        if (priv->fd_source != 0) {
                g_source_remove (priv->fd_source);
                priv->fd_source = 0;
        }

        close_named_pipe (inhibit);

        G_OBJECT_CLASS (ck_inhibit_parent_class)->finalize (object);
}

/**
 * ck_inhibit_new:
 *
 * Creates a new CkInhibit object.
 *
 * Return value: A @CkInhibit object or NULL on failure.
 **/
CkInhibit*
ck_inhibit_new (void)
{
        GObject *object;

        object = g_object_new (CK_TYPE_INHIBIT,
                               NULL);

        if (object == NULL) {
                return NULL;
        }

        return CK_INHIBIT (object);
}

/* Parses what for the inhibitors to assign to inhibit, invalid options
 * generate a warning but the operation continues. If at least one
 * inhibitor was set then it returns TRUE. */
static gboolean
parse_inhibitors_string (CkInhibit *inhibit,
                         const gchar *what)
{
        CkInhibitPrivate  *priv;
        gchar            **tokens;
        gint               i;
        gboolean           inhibit_set = FALSE;

        g_return_val_if_fail (CK_IS_INHIBIT (inhibit), FALSE);

        priv = CK_INHIBIT_GET_PRIVATE (inhibit);

        tokens = g_strsplit (what, ":", 0);

        /* This rather dense block just parses all the tokens and sets the
         * inhibit flags*/
        for (i = 0; tokens[i] && g_strcmp0 ("", tokens[i]); i++) {
                if (g_strcmp0 (tokens[i], "shutdown") == 0) {
                        priv->inhibitors[CK_INHIBIT_EVENT_SHUTDOWN] = TRUE;
                        inhibit_set = TRUE;
                } else if (g_strcmp0 (tokens[i], "sleep") == 0) {
                        priv->inhibitors[CK_INHIBIT_EVENT_SUSPEND] = TRUE;
                        inhibit_set = TRUE;
                } else if (g_strcmp0 (tokens[i], "idle") == 0) {
                        priv->inhibitors[CK_INHIBIT_EVENT_IDLE] = TRUE;
                        inhibit_set = TRUE;
                } else if (g_strcmp0 (tokens[i], "handle-power-key") == 0) {
                        priv->inhibitors[CK_INHIBIT_EVENT_POWER_KEY] = TRUE;
                        inhibit_set = TRUE;
                } else if (g_strcmp0 (tokens[i], "handle-suspend-key") == 0) {
                        priv->inhibitors[CK_INHIBIT_EVENT_SUSPEND_KEY] = TRUE;
                        inhibit_set = TRUE;
                } else if (g_strcmp0 (tokens[i], "handle-hibernate-key") == 0) {
                        priv->inhibitors[CK_INHIBIT_EVENT_HIBERNATE_KEY] = TRUE;
                        inhibit_set = TRUE;
                } else {
                        g_warning ("requested inhibit operation not supported %s",
                                   tokens[i]);
                }
        }
        g_strfreev (tokens);

        return inhibit_set;
}

/*
 * Creates the /run/ConsoleKit/inhibit directory. The
 * state_files will exist inside this directory.
 * Returns TRUE on success.
 */
static gboolean
create_inhibit_base_directory (void)
{
        gint res;

        errno = 0;
        res = g_mkdir_with_parents (LOCALSTATEDIR "/run/ConsoleKit/inhibit",
                                    S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
        if (res < 0) {
                g_warning ("Unable to create directory %s (%s)",
                           LOCALSTATEDIR "/run/ConsoleKit/inhibit",
                           g_strerror (errno));

                return FALSE;
        }

        if (g_chmod (LOCALSTATEDIR "/run/ConsoleKit/inhibit", 0755) == -1) {
                g_warning ("Failed to change permissions for %s",
                           LOCALSTATEDIR "/run/ConsoleKit/inhibit");
        }

        return TRUE;
}

/* When the named pipe is created we then send off the inhibit events */
static void
emit_initial_inhibit_signals (CkInhibit *inhibit)
{
        CkInhibitPrivate *priv;

        g_return_val_if_fail (CK_IS_INHIBIT (inhibit), -1);

        priv = CK_INHIBIT_GET_PRIVATE (inhibit);

        if (priv->inhibitors[CK_INHIBIT_EVENT_SHUTDOWN]) {
                g_signal_emit(G_OBJECT (inhibit), __signals[SIG_CHANGED_EVENT], 0, CK_INHIBIT_EVENT_SHUTDOWN, TRUE);
        }

        if (priv->inhibitors[CK_INHIBIT_EVENT_SUSPEND]) {
                g_signal_emit(G_OBJECT (inhibit), __signals[SIG_CHANGED_EVENT], 0, CK_INHIBIT_EVENT_SUSPEND, TRUE);
        }

        if (priv->inhibitors[CK_INHIBIT_EVENT_IDLE]) {
                g_signal_emit(G_OBJECT (inhibit), __signals[SIG_CHANGED_EVENT], 0, CK_INHIBIT_EVENT_IDLE, TRUE);
        }

        if (priv->inhibitors[CK_INHIBIT_EVENT_POWER_KEY]) {
                g_signal_emit(G_OBJECT (inhibit), __signals[SIG_CHANGED_EVENT], 0, CK_INHIBIT_EVENT_POWER_KEY, TRUE);
        }

        if (priv->inhibitors[CK_INHIBIT_EVENT_SUSPEND_KEY]) {
                g_signal_emit(G_OBJECT (inhibit), __signals[SIG_CHANGED_EVENT], 0, CK_INHIBIT_EVENT_SUSPEND_KEY, TRUE);
        }

        if (priv->inhibitors[CK_INHIBIT_EVENT_HIBERNATE_KEY]) {
                g_signal_emit(G_OBJECT (inhibit), __signals[SIG_CHANGED_EVENT], 0, CK_INHIBIT_EVENT_HIBERNATE_KEY, TRUE);
        }
}

/* When the named pipe is closed we set them to FALSE and send off the
 * uninhibit events */
static void
emit_final_uninhibit_signals (CkInhibit *inhibit)
{
        CkInhibitPrivate *priv;

        g_return_val_if_fail (CK_IS_INHIBIT (inhibit), -1);

        priv = CK_INHIBIT_GET_PRIVATE (inhibit);

        if (priv->inhibitors[CK_INHIBIT_EVENT_SHUTDOWN]) {
                priv->inhibitors[CK_INHIBIT_EVENT_SHUTDOWN] = FALSE;
                g_signal_emit(G_OBJECT (inhibit), __signals[SIG_CHANGED_EVENT], 0,  CK_INHIBIT_EVENT_SHUTDOWN, FALSE);
        }

        if (priv->inhibitors[CK_INHIBIT_EVENT_SUSPEND]) {
                priv->inhibitors[CK_INHIBIT_EVENT_SUSPEND] = FALSE;
                g_signal_emit(G_OBJECT (inhibit), __signals[SIG_CHANGED_EVENT], 0,  CK_INHIBIT_EVENT_SUSPEND, FALSE);
        }

        if (priv->inhibitors[CK_INHIBIT_EVENT_IDLE]) {
                priv->inhibitors[CK_INHIBIT_EVENT_IDLE] = FALSE;
                g_signal_emit(G_OBJECT (inhibit), __signals[SIG_CHANGED_EVENT], 0,  CK_INHIBIT_EVENT_IDLE, FALSE);
        }

        if (priv->inhibitors[CK_INHIBIT_EVENT_POWER_KEY]) {
                priv->inhibitors[CK_INHIBIT_EVENT_POWER_KEY] = FALSE;
                g_signal_emit(G_OBJECT (inhibit), __signals[SIG_CHANGED_EVENT], 0,  CK_INHIBIT_EVENT_POWER_KEY, FALSE);
        }

        if (priv->inhibitors[CK_INHIBIT_EVENT_SUSPEND_KEY]) {
                priv->inhibitors[CK_INHIBIT_EVENT_SUSPEND_KEY] = FALSE;
                g_signal_emit(G_OBJECT (inhibit), __signals[SIG_CHANGED_EVENT], 0,  CK_INHIBIT_EVENT_SUSPEND_KEY, FALSE);
        }

        if (priv->inhibitors[CK_INHIBIT_EVENT_HIBERNATE_KEY]) {
                priv->inhibitors[CK_INHIBIT_EVENT_HIBERNATE_KEY] = FALSE;
                g_signal_emit(G_OBJECT (inhibit), __signals[SIG_CHANGED_EVENT], 0,  CK_INHIBIT_EVENT_HIBERNATE_KEY, FALSE);
        }
}

/*
 * - Closes the named_pipe if opened.
 * - Unlinks the named_pipe_path if created.
 * - Frees the memory allocated to the named_pipe_path.
 */
static void
close_named_pipe (CkInhibit *inhibit)
{
        CkInhibitPrivate *priv;
        GError *error = NULL;

        g_return_if_fail (CK_IS_INHIBIT (inhibit));

        priv = CK_INHIBIT_GET_PRIVATE (inhibit);

        if (priv->named_pipe) {
                if (g_close (priv->named_pipe, &error) == -1) {
                        g_warning ("Failed to close inhibit named pipe, error was %s",
                                   error->message);
                        g_error_free (error);
                }
                priv->named_pipe = -1;
        }

        if (priv->named_pipe_path) {
                if (g_unlink (priv->named_pipe_path) == -1) {
                        g_warning ("Failed to remove inhibit file %s, error reported: %s",
                                   priv->named_pipe_path,
                                   g_strerror(errno));
                        g_error_free (error);
                }
                g_free (priv->named_pipe_path);
                priv->named_pipe_path = NULL;
        }

        if (priv->fd_source != 0) {
                g_source_remove (priv->fd_source);
                priv->fd_source = 0;
        }

        /* let others know we aren't holding any locks anymore */
        emit_final_uninhibit_signals (inhibit);
}

static gboolean
cb_named_pipe_close (GIOChannel *source,
                     GIOCondition condition,
                     gpointer user_data)
{
        CkInhibit *inhibit;

        /* Sanity checks */
        if (user_data == NULL) {
                g_warning ("cb_named_pipe_close: user_data == NULL");
                return FALSE;
        }

        if (!CK_IS_INHIBIT (user_data)) {
                g_warning ("cb_named_pipe_close: !CK_IS_INHIBIT (user_data)");
                return FALSE;
        }

        inhibit = CK_INHIBIT (user_data);

        /* We're about to return FALSE and close the glib source so reset it */
        inhibit->priv->fd_source = 0;

        close_named_pipe (inhibit);
        return FALSE;
}

/*
 * Creates the named pipe.
 * Returns the fd if successful (return >= 0) or -1
 */
static gint
create_named_pipe (CkInhibit *inhibit)
{
        CkInhibitPrivate *priv;
        GIOChannel       *io;

        g_return_val_if_fail (CK_IS_INHIBIT (inhibit), -1);

        priv = CK_INHIBIT_GET_PRIVATE (inhibit);

        /* Basic error checking */
        if (priv->named_pipe != -1) {
                g_warning ("Attempting to create an inhibit fd when one already exists");
                return -1;
        }

        if (priv->named_pipe_path == NULL) {
                g_warning ("named_pipe_path cannot be NULL");
                return -1;
        }

        /* create the named pipe */
        errno = 0;
        if (mknod (priv->named_pipe_path, S_IFIFO | 0600 , 0) == -1) {
                g_warning ("failed to create named pipe: %s",
                           g_strerror(errno));
                return -1;
        }

        /* open our side */
        priv->named_pipe = g_open (priv->named_pipe_path, O_RDONLY|O_CLOEXEC|O_NDELAY);
        if (priv->named_pipe < 0) {
                g_warning ("failed to open the named pipe for reading %s",
                           g_strerror(errno));
                return -1;
        }

        /* Monitor the named pipe */
        io = g_io_channel_unix_new (priv->named_pipe);
        priv->fd_source = g_io_add_watch (io,
                                          G_IO_HUP | G_IO_ERR | G_IO_NVAL,
                                          (GIOFunc)cb_named_pipe_close,
                                          inhibit);

        /* open the client side of the named pipe and return it */
        return open(priv->named_pipe_path, O_WRONLY|O_CLOEXEC|O_NDELAY);
}

/**
 * ck_inhibit_create_lock:
 * @inhibit: The @CkInhibit object
 * @who:  A human-readable, descriptive string of who is taking
 *        the lock. Example: "Xfburn"
 * @what: What is a colon-separated list of lock types.
 *        The list of lock types are: shutdown, sleep, idle,
 *        handle-power-key, handle-suspend-key, handle-hibernate-key.
 *        Example: "shutdown:idle"
 * @why:  A human-readable, descriptive string of why the program
 *        is taking the lock. Example: "Burning a DVD, interrupting now
 *        will ruin the DVD."
 *
 * Initializes the lock fd and populates the inhibit object with data.
 *
 * Return value: The named pipe (a file descriptor) on success.
 *               This is a value of 0 or greater.
 *               Returns a CkInhbitError on failure.
 **/
gint
ck_inhibit_create_lock (CkInhibit   *inhibit,
                        const gchar *who,
                        const gchar *what,
                        const gchar *why)
{
        CkInhibitPrivate *priv;
        gint              pipe;

        g_return_val_if_fail (CK_IS_INHIBIT (inhibit), CK_INHIBIT_ERROR_INVALID_INPUT);

        /* These fields only get set here and are mandatory */
        if (!who || !what || !why) {
                g_warning ("who, what, and why and mandatory for inhibit locks");
                return CK_INHIBIT_ERROR_INVALID_INPUT;
        }

        priv = CK_INHIBIT_GET_PRIVATE (inhibit);

        priv->who = who;
        priv->why = why;
        priv->named_pipe_path = g_strdup_printf (LOCALSTATEDIR "/run/ConsoleKit/inhibit/%s.pipe", who);
        if (!parse_inhibitors_string(inhibit, what)) {
                g_error ("Failed to set any inhibitors.");
                return CK_INHIBIT_ERROR_INVALID_INPUT;
        }

        if(priv->named_pipe_path == NULL) {
                g_warning ("Failed to allocate memory for inhibit state_file string");
                return CK_INHIBIT_ERROR_OOM;
        }

        /* always make sure we have a directory to work in */
        if (create_inhibit_base_directory () < 0) {
                return CK_INHIBIT_ERROR_GENERAL;
        }

        /* create the named pipe and get the fd to return. */
        pipe = create_named_pipe (inhibit);

        if (pipe == -1) {
                g_warning ("Failed to created named pipe");
                return CK_INHIBIT_ERROR_GENERAL;
        }

        /* let others know which locks we now hold */
        emit_initial_inhibit_signals (inhibit);

        /* return the fd for the user */
        return pipe;
}

void
ck_inhibit_remove_lock (CkInhibit   *inhibit)
{
        return close_named_pipe (inhibit);
}

/**
 * ck_inhibit_get_who:
 * @inhibit: The @CkInhibit object
 *
 * Return value: who initiated the inhibit lock.
 **/
const gchar*
ck_inhibit_get_who (CkInhibit   *inhibit)
{
        g_return_val_if_fail (CK_IS_INHIBIT (inhibit), NULL);

        return inhibit->priv->who;
}

/**
 * ck_inhibit_get_why:
 * @inhibit: The @CkInhibit object
 *
 * Return value: why it is inhibited.
 **/
const gchar*
ck_inhibit_get_why (CkInhibit   *inhibit)
{
        g_return_val_if_fail (CK_IS_INHIBIT (inhibit), NULL);

        return inhibit->priv->why;
}

/**
 * ck_inhibit_is_shutdown_inhibited:
 * @inhibit: The @CkInhibit object
 *
 * Return value: TRUE is inhibited.
 **/
gboolean
ck_inhibit_is_shutdown_inhibited (CkInhibit *inhibit)
{
        g_return_val_if_fail (CK_IS_INHIBIT (inhibit), FALSE);

        return inhibit->priv->inhibitors[CK_INHIBIT_EVENT_SHUTDOWN];
}

/**
 * ck_inhibit_is_suspend_inhibited:
 * @inhibit: The @CkInhibit object
 *
 * Return value: TRUE is inhibited.
 **/
gboolean
ck_inhibit_is_suspend_inhibited (CkInhibit *inhibit)
{
        g_return_val_if_fail (CK_IS_INHIBIT (inhibit), FALSE);

        return inhibit->priv->inhibitors[CK_INHIBIT_EVENT_SUSPEND];
}

/**
 * ck_inhibit_is_idle_inhibited:
 * @inhibit: The @CkInhibit object
 *
 * Return value: TRUE is inhibited.
 **/
gboolean
ck_inhibit_is_idle_inhibited (CkInhibit *inhibit)
{
        g_return_val_if_fail (CK_IS_INHIBIT (inhibit), FALSE);

        return inhibit->priv->inhibitors[CK_INHIBIT_EVENT_IDLE];
}

/**
 * ck_inhibit_is_power_key_inhibited:
 * @inhibit: The @CkInhibit object
 *
 * Return value: TRUE is inhibited.
 **/
gboolean
ck_inhibit_is_power_key_inhibited (CkInhibit *inhibit)
{
        g_return_val_if_fail (CK_IS_INHIBIT (inhibit), FALSE);

        return inhibit->priv->inhibitors[CK_INHIBIT_EVENT_POWER_KEY];
}

/**
 * ck_inhibit_is_suspend_key_inhibited:
 * @inhibit: The @CkInhibit object
 *
 * Return value: TRUE is inhibited.
 **/
gboolean
ck_inhibit_is_suspend_key_inhibited (CkInhibit *inhibit)
{
        g_return_val_if_fail (CK_IS_INHIBIT (inhibit), FALSE);

        return inhibit->priv->inhibitors[CK_INHIBIT_EVENT_SUSPEND_KEY];
}

/**
 * ck_inhibit_is_hibernate_key_inhibited:
 * @inhibit: The @CkInhibit object
 *
 * Return value: TRUE is inhibited.
 **/
gboolean
ck_inhibit_is_hibernate_key_inhibited (CkInhibit *inhibit)
{
        g_return_val_if_fail (CK_IS_INHIBIT (inhibit), FALSE);

        return inhibit->priv->inhibitors[CK_INHIBIT_EVENT_HIBERNATE_KEY];
}
