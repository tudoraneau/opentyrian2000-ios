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
#include "gamepad.h"
#include "SDL.h"

#ifdef __IPHONEOS__

#include "config.h"
#include "font.h"
#include "keyboard.h"
#include "video.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// Bluetooth Controller Support (MFi / PS4 / PS5 / Xbox via SDL_GameController)
// Uses pure polling (SDL_GameControllerGetButton/Axis) instead of events so
// that SDL_JoystickEventState(SDL_IGNORE) in init_joysticks() cannot interfere.

#define GPAD_MAX       4
#define GPAD_AXIS_DEAD 8000  // ~25% of SDL axis range (32767)

static SDL_GameController *gpads[GPAD_MAX];
static int                 gpad_count  = 0;
static int                 gpad_last_n = -1; // last SDL_NumJoysticks() value
static bool                gpad_keydown = false; // controller contributing to keydown
static Uint32              gpad_notify_until = 0; // SDL_GetTicks() deadline for the HUD notification

// Previous button states for rising-edge detection (newkey / lastkey_scan).
static bool gpad_prev_up     = false;
static bool gpad_prev_down   = false;
static bool gpad_prev_left   = false;
static bool gpad_prev_right  = false;
static bool gpad_prev_fire   = false;
static bool gpad_prev_chfire = false;
static bool gpad_prev_lkick  = false;
static bool gpad_prev_rkick  = false;
static bool gpad_prev_esc    = false;
static bool gpad_prev_enter  = false;

// Release all controller-driven keys and clear keydown.
static void gpad_release_all(void)
{
    keysactive[keySettings[KEY_SETTING_UP]]             = 0;
    keysactive[keySettings[KEY_SETTING_DOWN]]           = 0;
    keysactive[keySettings[KEY_SETTING_LEFT]]           = 0;
    keysactive[keySettings[KEY_SETTING_RIGHT]]          = 0;
    keysactive[keySettings[KEY_SETTING_FIRE]]           = 0;
    keysactive[keySettings[KEY_SETTING_CHANGE_FIRE]]    = 0;
    keysactive[keySettings[KEY_SETTING_LEFT_SIDEKICK]]  = 0;
    keysactive[keySettings[KEY_SETTING_RIGHT_SIDEKICK]] = 0;
    keysactive[SDL_SCANCODE_ESCAPE]                     = 0;
    keysactive[SDL_SCANCODE_RETURN]                     = 0;
    if (gpad_keydown) { keydown = false; gpad_keydown = false; }
}

// Re-scan the joystick list, closing detached controllers and opening new ones.
static void gpad_scan(void)
{
    // Remove any that are no longer attached.
    for (int i = gpad_count - 1; i >= 0; i--)
    {
        if (!SDL_GameControllerGetAttached(gpads[i]))
        {
            printf("gamepad disconnected: %s\n", SDL_GameControllerName(gpads[i]));
            SDL_GameControllerClose(gpads[i]);
            for (int j = i; j < gpad_count - 1; j++)
                gpads[j] = gpads[j + 1];
            gpad_count--;
        }
    }
    if (gpad_count == 0)
        gpad_release_all();

    // Open newly-visible controllers.
    int n = SDL_NumJoysticks();
    for (int i = 0; i < n; i++)
    {
        if (!SDL_IsGameController(i))
            continue;
        SDL_JoystickID iid = SDL_JoystickGetDeviceInstanceID(i);
        bool already = false;
        for (int k = 0; k < gpad_count; k++)
        {
            if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gpads[k])) == iid)
            { already = true; break; }
        }
        if (!already && gpad_count < GPAD_MAX)
        {
            SDL_GameController *gc = SDL_GameControllerOpen(i);
            if (gc)
            {
                gpads[gpad_count++] = gc;
                printf("gamepad connected: %s\n", SDL_GameControllerName(gc));
                // Only start the timer if it isn't already counting down;
                // avoids the message staying forever when the joystick count
                // flickers (iOS BT enumeration) and gpad_scan re-opens the same
                // controller multiple times in quick succession.
                Uint32 _now = SDL_GetTicks();
                if ((Sint32)(_now - gpad_notify_until) >= 0)
                    gpad_notify_until = _now + 4000;
            }
        }
    }
    gpad_last_n = n;
}

// Write a key state into keysactive[] and trigger newkey on the rising edge.
static void gpad_set_key(SDL_Scancode sc, bool pressed, bool *prev)
{
    keysactive[sc] = pressed ? 1 : 0;
    if (pressed && !*prev)
    {
        newkey       = true;
        lastkey_scan = sc;
        lastkey_mod  = KMOD_NONE;
    }
    *prev = pressed;
}

// Poll every open controller and push its state into keysactive[].
// Called from service_SDL_events() every frame.
void poll_gamecontrollers(void)
{
    // SDL_PollEvent (called just before this) pumps events internally, so
    // GCController connect notifications are processed; re-scan if count changed.
    int n = SDL_NumJoysticks();
    if (n != gpad_last_n)
        gpad_scan();

    if (gpad_count == 0)
        return;

    SDL_GameController *gc = gpads[0];

    // Left analog stick.
    Sint16 ax = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
    Sint16 ay = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);

    // D-pad buttons (also maps correctly for MFi micro-gamepad).
    bool dp_up    = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_UP);
    bool dp_down  = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    bool dp_left  = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    bool dp_right = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);

    // D-pad wins; left stick is a fallback.
    bool mv_up    = dp_up    || (ay < -GPAD_AXIS_DEAD);
    bool mv_down  = dp_down  || (ay >  GPAD_AXIS_DEAD);
    bool mv_left  = dp_left  || (ax < -GPAD_AXIS_DEAD);
    bool mv_right = dp_right || (ax >  GPAD_AXIS_DEAD);

    bool fire      = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A);
    bool chfire    = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B);
    bool lkick     = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_X);
    bool rkick     = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_Y);
    bool esc_btn   = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_START);
    bool enter_btn = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_BACK);

    gpad_set_key(keySettings[KEY_SETTING_UP],             mv_up,     &gpad_prev_up);
    gpad_set_key(keySettings[KEY_SETTING_DOWN],           mv_down,   &gpad_prev_down);
    gpad_set_key(keySettings[KEY_SETTING_LEFT],           mv_left,   &gpad_prev_left);
    gpad_set_key(keySettings[KEY_SETTING_RIGHT],          mv_right,  &gpad_prev_right);
    gpad_set_key(keySettings[KEY_SETTING_FIRE],           fire,      &gpad_prev_fire);
    gpad_set_key(keySettings[KEY_SETTING_CHANGE_FIRE],    chfire,    &gpad_prev_chfire);
    gpad_set_key(keySettings[KEY_SETTING_LEFT_SIDEKICK],  lkick,     &gpad_prev_lkick);
    gpad_set_key(keySettings[KEY_SETTING_RIGHT_SIDEKICK], rkick,     &gpad_prev_rkick);
    gpad_set_key(SDL_SCANCODE_ESCAPE,                     esc_btn,   &gpad_prev_esc);
    gpad_set_key(SDL_SCANCODE_RETURN,                     enter_btn, &gpad_prev_enter);

    bool any = mv_up || mv_down || mv_left || mv_right ||
               fire || chfire || lkick || rkick || esc_btn || enter_btn;
    if (any)
    {
        keydown = true;
        gpad_keydown = true;
    }
    else if (gpad_keydown)
    {
        keydown = false;
        gpad_keydown = false;
    }
}

bool gamepad_is_connected(void)
{
    return gpad_count > 0;
}

// Draw a brief "Gamepad Connected" banner at the top-center of VGAScreen.
// Must be called after the game has finished drawing the current frame but
// before JE_showVGA() submits it to the renderer.
void draw_gamepad_notification(void)
{
    // Signed comparison handles Uint32 wraparound correctly.
    if (!VGAScreen || (Sint32)(SDL_GetTicks() - gpad_notify_until) >= 0)
        return;

    const int x = vga_width / 2; // 160 – center of the 320px screen
    draw_font_hv_shadow(VGAScreen, x,  4, "Gamepad Connected.",          small_font, centered, 15, 4, false, 1);
    draw_font_hv_shadow(VGAScreen, x, 13, "On-screen controls disabled.", small_font, centered, 15, 4, false, 1);
}

// Initialize the SDL game controller subsystem. Must be called after
// init_joysticks(). Detection and opening of controllers happens lazily
// via poll_gamecontrollers() so no event state flags need to be set.
void init_gamecontrollers(void)
{
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
    {
        fprintf(stderr, "warning: failed to init game controller subsystem: %s\n", SDL_GetError());
        return;
    }
    memset(gpads, 0, sizeof(gpads));
    gpad_count  = 0;
    gpad_last_n = -1; // forces a scan on the first poll_gamecontrollers() call
    printf("init_gamecontrollers: ready\n");
}

#endif /* __IPHONEOS__ */
