/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015 Eric Koegel <eric.koegel@gmail.com>
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


int
main (int   argc,
      char *argv[])
{
    /* This is a mock for the tools/ck-collect-session-info program.
     * It provides similar output to the real tool for test-session-leader.
     * You do not need to run this as root, but as it does nothing there's
     * little to worry about */
    printf ("unix-user = 9000\n");
    printf ("x11-display = :0.0\n");
    printf ("x11-display-device = /dev/tty15\n");
    printf ("display-device = /dev/pts/0\n");
    printf ("login-session-id = 99\n");


    return 0;
}
