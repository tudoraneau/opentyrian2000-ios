/*
 * OpenTyrian: A modern cross-platform port of Tyrian
 * Copyright (C) 2007-2009  The OpenTyrian Development Team
 * Copyright (C) 2026 Felix Tudoran (OpenTyrian2000-iOS port)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#ifndef GAMEPAD_H
#define GAMEPAD_H

#include "SDL.h" // defines __IPHONEOS__ via SDL_platform.h

#ifdef __IPHONEOS__

#include <stdbool.h>

/* Initialize the SDL game controller subsystem.
 * Must be called after init_joysticks(). */
void init_gamecontrollers(void);

/* Returns true if at least one Bluetooth/MFi controller is currently open. */
bool gamepad_is_connected(void);

/* Poll all open controllers and push their state into keysactive[].
 * Called from service_SDL_events() every frame. */
void poll_gamecontrollers(void);

#endif /* __IPHONEOS__ */

#endif /* GAMEPAD_H */
