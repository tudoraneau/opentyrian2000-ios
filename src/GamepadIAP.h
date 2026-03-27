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

/*
 * GamepadIAP.h — C-callable interface to the Gamepad Support in-app purchase.
 */

#ifndef GAMEPAD_IAP_H
#define GAMEPAD_IAP_H

#ifdef __IPHONEOS__

#include <stdbool.h>

/*
 * Returns true when the user has previously purchased or restored the
 * "Gamepad Support" IAP.  The result is persisted in NSUserDefaults and
 * survives app restarts.
 *
 * Must be called on the main thread.
 */
bool gamepad_iap_is_unlocked(void);

/*
 * Present the native iOS alert that lets the user unlock gamepad support,
 * restore a prior purchase, or decline.
 *
 *   on_result(true)  ← purchase/restore succeeded; gamepad may be enabled.
 *   on_result(false) ← user tapped "Not Now", or the transaction failed/was
 *                      cancelled; caller should suppress gamepad input.
 *
 * on_result is always invoked on the main thread.
 * Must be called on the main thread.
 */
void gamepad_iap_show_prompt(void (*on_result)(bool enabled));

#endif /* __IPHONEOS__ */
#endif /* GAMEPAD_IAP_H */
