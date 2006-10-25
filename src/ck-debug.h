/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2005-2006 William Jon McCann <mccann@jhu.edu>
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
 * Authors: William Jon McCann <mccann@jhu.edu>
 *
 */

#ifndef __CK_DEBUG_H
#define __CK_DEBUG_H

#include <stdarg.h>
#include <glib.h>

G_BEGIN_DECLS

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define ck_debug(...) ck_debug_real (__func__, __FILE__, __LINE__, __VA_ARGS__)
#elif defined(__GNUC__) && __GNUC__ >= 3
#define ck_debug(...) ck_debug_real (__FUNCTION__, __FILE__, __LINE__, __VA_ARGS__)
#else
#define ck_debug(...)
#endif

void ck_debug_init             (gboolean debug,
                                gboolean to_file);
gboolean ck_debug_enabled      (void);
void ck_debug_shutdown         (void);
void ck_debug_real             (const char *func,
                                const char *file,
                                int         line,
                                const char *format, ...);

#define ENABLE_PROFILING 1
#ifdef ENABLE_PROFILING
#ifdef G_HAVE_ISO_VARARGS
#define ck_profile_start(...) _ck_profile_log (G_STRFUNC, "start", __VA_ARGS__)
#define ck_profile_end(...)   _ck_profile_log (G_STRFUNC, "end", __VA_ARGS__)
#define ck_profile_msg(...)   _ck_profile_log (NULL, NULL, __VA_ARGS__)
#elif defined(G_HAVE_GNUC_VARARGS)
#define ck_profile_start(format...) _ck_profile_log (G_STRFUNC, "start", format)
#define ck_profile_end(format...)   _ck_profile_log (G_STRFUNC, "end", format)
#define ck_profile_msg(format...)   _ck_profile_log (NULL, NULL, format)
#endif
#else
#define ck_profile_start(...)
#define ck_profile_end(...)
#define ck_profile_msg(...)
#endif

void            _ck_profile_log    (const char *func,
                                    const char *note,
                                    const char *format,
                                    ...) G_GNUC_PRINTF (3, 4);

G_END_DECLS

#endif /* __CK_DEBUG_H */
