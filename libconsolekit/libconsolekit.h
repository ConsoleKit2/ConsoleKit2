/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (c) 2017, Eric Koegel <eric.koegel@gmail.com>
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

#ifndef LIB_CONSOLEKIT_H_
#define LIB_CONSOLEKIT_H_

#include <glib-object.h>

#define LIB_TYPE_CONSOLEKIT           (lib_consolekit_get_type ())
#define LIB_CONSOLEKIT(o)             (G_TYPE_CHECK_INSTANCE_CAST ((o), LIB_TYPE_CONSOLEKIT, LibConsoleKit))
#define LIB_CONSOLEKIT_CLASS(k)       (G_TYPE_CHECK_CLASS_CAST((k), LIB_TYPE_CONSOLEKIT, LibConsoleKitClass))
#define LIB_IS_CONSOLEKIT(o)          (G_TYPE_CHECK_INSTANCE_TYPE ((o), LIB_TYPE_CONSOLEKIT))
#define LIB_IS_CONSOLEKIT_CLASS(k)    (G_TYPE_CHECK_CLASS_TYPE ((k), LIB_TYPE_CONSOLEKIT))
#define LIB_CONSOLEKIT_GET_CLASS(o)   (G_TYPE_INSTANCE_GET_CLASS ((o), LIB_TYPE_CONSOLEKIT, LibConsoleKitClass))


typedef struct _LibConsoleKit LibConsoleKit;

typedef struct
{
        GObjectClass parent_class;
} LibConsoleKitClass;

/**
 * LibConsoleKitError:
 * @CONSOLEKIT_ERROR_FAILED: unknown or unclassified error
 * @CONSOLEKIT_ERROR_INVALID_INPUT: the variable passed into the calling function is invalid.
 *
 * Describes errors that may result from operations involving a #LibConsoleKit.
 *
 **/
typedef enum
{
        CONSOLEKIT_ERROR_FAILED,
        CONSOLEKIT_ERROR_INVALID_INPUT
} LibConsoleKitError;


GQuark          lib_consolekit_error_quark             (void);

GType           lib_consolekit_get_type                (void);

LibConsoleKit  *lib_consolekit_new                     (void);

gboolean        lib_consolekit_seat_get_active         (LibConsoleKit *ck,
                                                        const gchar *seat,
                                                        gchar **session,
                                                        uid_t *uid,
                                                        GError **error);

gint            lib_consolekit_seat_get_sessions       (LibConsoleKit *ck,
                                                        const gchar *seat,
                                                        gchar ***sessions,
                                                        GError **error);

gboolean        lib_consolekit_seat_can_multi_session  (LibConsoleKit *ck,
                                                        const gchar *seat,
                                                        GError **error);

gboolean        lib_consolekit_session_is_active       (LibConsoleKit *ck,
                                                        const gchar *session,
                                                        GError **error);

gboolean        lib_consolekit_session_is_remote       (LibConsoleKit *ck,
                                                        const gchar *session,
                                                        GError **error);

gboolean        lib_consolekit_session_get_uid         (LibConsoleKit *ck,
                                                        const gchar *session,
                                                        uid_t *uid,
                                                        GError **error);

gboolean        lib_consolekit_session_get_seat        (LibConsoleKit *ck,
                                                        const gchar *session,
                                                        gchar **seat,
                                                        GError **error);

gboolean        lib_consolekit_session_get_type        (LibConsoleKit *ck,
                                                        const gchar *session,
                                                        gchar **type,
                                                        GError **error);

gboolean        lib_consolekit_session_get_class       (LibConsoleKit *ck,
                                                        const gchar *session,
                                                        gchar **session_class,
                                                        GError **error);

gboolean        lib_consolekit_session_get_display     (LibConsoleKit *ck,
                                                        const gchar *session,
                                                        gchar **display,
                                                        GError **error);

gboolean        lib_consolekit_session_get_remote_host (LibConsoleKit *ck,
                                                        const gchar *session,
                                                        gchar **remote_host,
                                                        GError **error);

gboolean        lib_consolekit_session_get_tty         (LibConsoleKit *ck,
                                                        const gchar *session,
                                                        gchar **tty,
                                                        GError **error);

gboolean        lib_consolekit_session_get_vt          (LibConsoleKit *ck,
                                                        const gchar *session,
                                                        guint *vt,
                                                        GError **error);

gboolean        lib_consolekit_pid_get_session         (LibConsoleKit *ck,
                                                        pid_t pid,
                                                        gchar **session,
                                                        GError **error);




#endif /* LIB_CONSOLEKIT_H_ */
