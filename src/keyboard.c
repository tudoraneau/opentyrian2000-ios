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
#include "keyboard.h"

#include "config.h"
#include "joystick.h"
#include "mouse.h"
#include "network.h"
#include "opentyr.h"
#include "video.h"
#include "video_scale.h"

#include "SDL.h"

#include <stdio.h>
#include <string.h>

#ifdef __IPHONEOS__
#include <math.h>

// Virtual Gamepad (orientation sensitive)

#define VPAD_BTN_COUNT  5
#define VPAD_DPAD_DEAD  0.04f   // dead-zone radius

typedef struct { float cx, cy, r; } VPadBtnPos;

typedef struct {
    float      dpad_cx, dpad_cy, dpad_radius;
    VPadBtnPos btns[VPAD_BTN_COUNT];
} VPadLayout;

// Landscape layo
static const VPadLayout vpadLandscape = {
    0.12f, 0.76f, 0.19f,
    {
        { 0.86f, 0.78f, 0.10f  }, // Fire  (Space)
        { 0.75f, 0.63f, 0.075f }, // LKick (LCtrl)
        { 0.93f, 0.63f, 0.075f }, // RKick (LAlt)
        { 0.05f, 0.06f, 0.055f }, // Escape
        { 0.96f, 0.06f, 0.055f }, // Enter
    }
};

// Portrait layout
// Positions chosen so that cx ± r*minDim/W stays inside [0,W] for a 390×844
// device (and scales correctly on any portrait screen).
static const VPadLayout vpadPortrait = {
    0.22f, 0.78f, 0.18f,
    {
        { 0.78f, 0.82f, 0.10f  }, // Fire  (Space)
        { 0.65f, 0.70f, 0.08f  }, // LKick (LCtrl)
        { 0.85f, 0.70f, 0.08f  }, // RKick (LAlt)
        { 0.08f, 0.06f, 0.055f }, // Escape
        { 0.92f, 0.06f, 0.055f }, // Enter
    }
};

// Scancodes for each button slot (same order in both layouts)
static const SDL_Scancode vpadBtnScancodes[VPAD_BTN_COUNT] = {
    SDL_SCANCODE_SPACE,
    SDL_SCANCODE_LCTRL,
    SDL_SCANCODE_LALT,
    SDL_SCANCODE_ESCAPE,
    SDL_SCANCODE_RETURN,
};

#define VPAD_MAX_FINGERS 8
#define VPAD_MAX_KEYS    6

typedef struct {
    SDL_FingerID id;
    SDL_Scancode keys[VPAD_MAX_KEYS];
    int          keyCount;
    bool         active;
} FingerTrack;

static FingerTrack fingerTracks[VPAD_MAX_FINGERS];

static FingerTrack *findFinger(SDL_FingerID id)
{
    for (int i = 0; i < VPAD_MAX_FINGERS; i++)
        if (fingerTracks[i].active && fingerTracks[i].id == id)
            return &fingerTracks[i];
    return NULL;
}

static FingerTrack *allocFinger(SDL_FingerID id)
{
    for (int i = 0; i < VPAD_MAX_FINGERS; i++)
    {
        if (!fingerTracks[i].active)
        {
            fingerTracks[i].active   = true;
            fingerTracks[i].id       = id;
            fingerTracks[i].keyCount = 0;
            memset(fingerTracks[i].keys, 0, sizeof(fingerTracks[i].keys));
            return &fingerTracks[i];
        }
    }
    return NULL;
}

static void releaseFinger(FingerTrack *ft)
{
    for (int i = 0; i < ft->keyCount; i++)
        if (ft->keys[i] != SDL_SCANCODE_UNKNOWN)
            keysactive[ft->keys[i]] = 0;
    ft->keyCount = 0;
    ft->active   = false;
}

static void pressKey(FingerTrack *ft, SDL_Scancode sc)
{
    if (sc == SDL_SCANCODE_UNKNOWN || ft->keyCount >= VPAD_MAX_KEYS)
        return;
    keysactive[sc] = 1;
    ft->keys[ft->keyCount++] = sc;
}

// Returns the layout that matches the current window orientation.
static const VPadLayout *vpad_current_layout(void)
{
    if (!main_window) return &vpadLandscape;
    int w, h;
    SDL_GetWindowSize(main_window, &w, &h);
    return (h > w) ? &vpadPortrait : &vpadLandscape;
}

static void applyTouch(FingerTrack *ft, float x, float y)
{
    const VPadLayout *layout = vpad_current_layout();

    // Release previously held keys for this finger
    for (int i = 0; i < ft->keyCount; i++)
        if (ft->keys[i] != SDL_SCANCODE_UNKNOWN)
            keysactive[ft->keys[i]] = 0;
    ft->keyCount = 0;

    // D-pad zone
    float dx = x - layout->dpad_cx;
    float dy = y - layout->dpad_cy;
    float dr = layout->dpad_radius;
    if (dx * dx + dy * dy <= dr * dr)
    {
        if (dy < -VPAD_DPAD_DEAD) pressKey(ft, keySettings[KEY_SETTING_UP]);
        if (dy >  VPAD_DPAD_DEAD) pressKey(ft, keySettings[KEY_SETTING_DOWN]);
        if (dx < -VPAD_DPAD_DEAD) pressKey(ft, keySettings[KEY_SETTING_LEFT]);
        if (dx >  VPAD_DPAD_DEAD) pressKey(ft, keySettings[KEY_SETTING_RIGHT]);
        return; // d-pad zone wins; don't check buttons
    }

    // Action-button zones
    for (int i = 0; i < VPAD_BTN_COUNT; i++)
    {
        float bx = x - layout->btns[i].cx;
        float by = y - layout->btns[i].cy;
        float r  = layout->btns[i].r;
        if (bx * bx + by * by <= r * r)
            pressKey(ft, vpadBtnScancodes[i]);
    }
}

static void handleFingerEvent(SDL_TouchFingerEvent *e, int evType)
{
    if (evType == SDL_FINGERDOWN)
    {
        FingerTrack *ft = findFinger(e->fingerId);
        if (!ft) ft = allocFinger(e->fingerId);
        if (ft)
        {
            applyTouch(ft, e->x, e->y);

            // Menus check newkey/lastkey_scan, not keysactive[].
            // Synthesize a key-down so menus respond to touch.
            if (ft->keyCount > 0)
            {
                newkey = true;
                lastkey_scan = ft->keys[0];
                lastkey_mod = KMOD_NONE;
                keydown = true;
            }
        }
    }
    else if (evType == SDL_FINGERMOTION)
    {
        FingerTrack *ft = findFinger(e->fingerId);
        if (!ft) ft = allocFinger(e->fingerId);
        if (ft)  applyTouch(ft, e->x, e->y);
    }
    else // SDL_FINGERUP
    {
        FingerTrack *ft = findFinger(e->fingerId);
        if (ft)
        {
            releaseFinger(ft);
            keydown = false;
        }
    }
}

// Overlay drawing

// Draw a filled circle using horizontal scanlines
static void fillCircle(SDL_Renderer *r, int cx, int cy, int radius)
{
    for (int dy = -radius; dy <= radius; dy++)
    {
        int w = (int)sqrtf((float)(radius * radius - dy * dy));
        SDL_RenderDrawLine(r, cx - w, cy + dy, cx + w, cy + dy);
    }
}

// Draw a cross/plus shape for the d-pad indicator
static void fillCross(SDL_Renderer *r, int cx, int cy, int radius)
{
    int arm = radius;
    int thick = radius / 3;
    // Horizontal bar
    SDL_Rect h = { cx - arm, cy - thick, arm * 2, thick * 2 };
    SDL_RenderFillRect(r, &h);
    // Vertical bar
    SDL_Rect v = { cx - thick, cy - arm, thick * 2, arm * 2 };
    SDL_RenderFillRect(r, &v);
}

// ── Tiny 5×7 bitmap font for button labels ────────────────────────────────────
#define GLYPH_W 5
#define GLYPH_H 7

typedef struct { char ch; Uint8 rows[GLYPH_H]; } VPadGlyph;

// Each row is a 5-bit mask; bit 4 (0x10) = leftmost pixel.
static const VPadGlyph vpadGlyphs[] = {
    { 'A', { 0x0E,0x11,0x11,0x1F,0x11,0x11,0x11 } },
    { 'C', { 0x0E,0x11,0x10,0x10,0x10,0x11,0x0E } },
    { 'E', { 0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F } },
    { 'L', { 0x10,0x10,0x10,0x10,0x10,0x10,0x1F } },
    { 'N', { 0x11,0x19,0x15,0x13,0x11,0x11,0x11 } },
    { 'P', { 0x1E,0x11,0x11,0x1E,0x10,0x10,0x10 } },
    { 'S', { 0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E } },
    { 'T', { 0x1F,0x04,0x04,0x04,0x04,0x04,0x04 } },
};
#define VPAD_GLYPH_COUNT ((int)(sizeof(vpadGlyphs)/sizeof(vpadGlyphs[0])))

static const VPadGlyph *vpadFindGlyph(char c)
{
    for (int i = 0; i < VPAD_GLYPH_COUNT; i++)
        if (vpadGlyphs[i].ch == c)
            return &vpadGlyphs[i];
    return NULL;
}

// Draw a short text label centred on (cx, cy) at a size proportional to radius.
static void vpad_draw_label(SDL_Renderer *r, int cx, int cy, int radius,
                             const char *label)
{
    int len = 0;
    while (label[len]) len++;
    if (len == 0) return;

    int scale  = radius / 11;
    if (scale < 1) scale = 1;

    int charW  = GLYPH_W * scale;
    int gap    = scale;
    int totalW = len * charW + (len - 1) * gap;
    int totalH = GLYPH_H * scale;

    int startX = cx - totalW / 2;
    int startY = cy - totalH / 2;

    for (int ci = 0; ci < len; ci++)
    {
        const VPadGlyph *g = vpadFindGlyph(label[ci]);
        if (!g) { startX += charW + gap; continue; }

        int gx = startX + ci * (charW + gap);
        for (int row = 0; row < GLYPH_H; row++)
        {
            for (int col = 0; col < GLYPH_W; col++)
            {
                if (g->rows[row] & (0x10 >> col))
                {
                    SDL_Rect px = {
                        gx + col * scale,
                        startY + row * scale,
                        scale, scale
                    };
                    SDL_RenderFillRect(r, &px);
                }
            }
        }
    }
}

void vpad_draw_overlay(void)
{
    if (!main_window) return;
    SDL_Renderer *renderer = SDL_GetRenderer(main_window);
    if (!renderer) return;

    int win_w, win_h;
    SDL_GetWindowSize(main_window, &win_w, &win_h);

    const VPadLayout *layout = vpad_current_layout();
    int minDim = (win_h < win_w) ? win_h : win_w;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // D-pad cross
    {
        int cx = (int)(layout->dpad_cx * win_w);
        int cy = (int)(layout->dpad_cy * win_h);
        int r  = (int)(layout->dpad_radius * minDim);

        // Outer ring (dark grey, semi-transparent)
        SDL_SetRenderDrawColor(renderer, 50, 50, 50, 100);
        fillCircle(renderer, cx, cy, r);

        // Cross shape (white, semi-transparent)
        SDL_SetRenderDrawColor(renderer, 220, 220, 220, 160);
        fillCross(renderer, cx, cy, r - 4);
    }

    // Action buttons (fire / sidekicks / utility)
    static const struct { Uint8 r, g, b; } btnColors[VPAD_BTN_COUNT] = {
        { 220,  60,  60 },   // Fire   – red
        {  60, 140, 220 },   // LKick  – blue
        { 220, 200,  60 },   // RKick  – yellow
        { 160, 160, 160 },   // Escape – grey
        {  60, 200,  60 },   // Enter  – green
    };
    static const char *const btnLabels[VPAD_BTN_COUNT] = {
        "SPC", "LCT", "LAL", "ESC", "ENT"
    };

    for (int i = 0; i < VPAD_BTN_COUNT; i++)
    {
        int cx = (int)(layout->btns[i].cx * win_w);
        int cy = (int)(layout->btns[i].cy * win_h);
        int r  = (int)(layout->btns[i].r  * minDim);

        // Dark background
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 100);
        fillCircle(renderer, cx, cy, r);

        // Coloured foreground
        SDL_SetRenderDrawColor(renderer,
            btnColors[i].r, btnColors[i].g, btnColors[i].b, 160);
        fillCircle(renderer, cx, cy, r - 4);

        // White label
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 220);
        vpad_draw_label(renderer, cx, cy, r, btnLabels[i]);
    }

    // Restore opaque blending for the rest of the frame
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

#endif // __IPHONEOS__

JE_boolean ESCPressed;

JE_boolean newkey, newmouse, keydown, mousedown;
SDL_Scancode lastkey_scan;
SDL_Keymod lastkey_mod;
Uint8 lastmouse_but;
Sint32 lastmouse_x, lastmouse_y;
JE_boolean mouse_pressed[4] = {false, false, false, false};
Sint32 mouse_x, mouse_y;

bool windowHasFocus;

Uint8 keysactive[SDL_NUM_SCANCODES];

bool new_text;
char last_text[SDL_TEXTINPUTEVENT_TEXT_SIZE];

static bool mouseRelativeEnabled;

void mouseSetRelative(bool enable)
{
#ifndef __IPHONEOS__
	SDL_SetRelativeMouseMode(enable ? SDL_TRUE : SDL_FALSE);
#endif
	mouseRelativeEnabled = enable;
}

// Relative mouse position in window coordinates.
static Sint32 mouseWindowXRelative;
static Sint32 mouseWindowYRelative;

void flush_events_buffer(void)
{
	SDL_Event ev;

	while (SDL_PollEvent(&ev));
}

void wait_input(JE_boolean keyboard, JE_boolean mouse, JE_boolean joystick)
{
	service_SDL_events(false);
	while (!((keyboard && keydown) || (mouse && mousedown) || (joystick && joydown)))
	{
		SDL_Delay(SDL_POLL_INTERVAL);
		push_joysticks_as_keyboard();
		service_SDL_events(false);

#ifdef WITH_NETWORK
		if (isNetworkGame)
			network_check();
#endif
	}
}

void wait_noinput(JE_boolean keyboard, JE_boolean mouse, JE_boolean joystick)
{
	service_SDL_events(false);
	while ((keyboard && keydown) || (mouse && mousedown) || (joystick && joydown))
	{
		SDL_Delay(SDL_POLL_INTERVAL);
		poll_joysticks();
		service_SDL_events(false);

#ifdef WITH_NETWORK
		if (isNetworkGame)
			network_check();
#endif
	}
}

void init_keyboard(void)
{
	//SDL_EnableKeyRepeat(500, 60); TODO Find if SDL2 has an equivalent.

	newkey = newmouse = false;
	keydown = mousedown = false;

	SDL_ShowCursor(SDL_FALSE);

#if SDL_VERSION_ATLEAST(2, 26, 0)
	SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "1");
#endif

#ifdef __IPHONEOS__
        // Disable SDL's automatic touch-to-mouse event synthesis so that raw
        // finger events are handled exclusively by the virtual gamepad.
        SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
        SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
        memset(fingerTracks, 0, sizeof(fingerTracks));
#endif
}

JE_word JE_mousePosition(JE_word *mouseX, JE_word *mouseY)
{
	service_SDL_events(false);
	*mouseX = mouse_x;
	*mouseY = mouse_y;
	return mousedown ? lastmouse_but : 0;
}

void mouseGetRelativePosition(Sint32 *const out_x, Sint32 *const out_y)
{
	service_SDL_events(false);

	scaleWindowDistanceToScreen(&mouseWindowXRelative, &mouseWindowYRelative);
	*out_x = mouseWindowXRelative;
	*out_y = mouseWindowYRelative;

	mouseWindowXRelative = 0;
	mouseWindowYRelative = 0;
}

void service_SDL_events(JE_boolean clear_new)
{
	SDL_Event ev;

	if (clear_new)
	{
		newkey = false;
		newmouse = false;
		new_text = false;
	}

	while (SDL_PollEvent(&ev))
	{
		switch (ev.type)
		{
			case SDL_WINDOWEVENT:
				switch (ev.window.event)
				{
				case SDL_WINDOWEVENT_FOCUS_LOST:
					windowHasFocus = false;

					mouseSetRelative(mouseRelativeEnabled);
					break;

				case SDL_WINDOWEVENT_FOCUS_GAINED:
					windowHasFocus = true;

					mouseSetRelative(mouseRelativeEnabled);
					break;

				case SDL_WINDOWEVENT_RESIZED:
					video_on_win_resize();
					break;
				}
				break;

			case SDL_KEYDOWN:
				/* <alt><enter> toggle fullscreen */
				if (ev.key.keysym.mod & KMOD_ALT && ev.key.keysym.scancode == SDL_SCANCODE_RETURN)
				{
					toggle_fullscreen();
					break;
				}

				keysactive[ev.key.keysym.scancode] = 1;

				newkey = true;
				lastkey_scan = ev.key.keysym.scancode;
				lastkey_mod = ev.key.keysym.mod;
				keydown = true;

				mouseInactive = true;
				return;

			case SDL_KEYUP:
				keysactive[ev.key.keysym.scancode] = 0;
				keydown = false;
				return;

			case SDL_MOUSEMOTION:
				mouse_x = ev.motion.x;
				mouse_y = ev.motion.y;
				mapWindowPointToScreen(&mouse_x, &mouse_y);

				if (mouseRelativeEnabled && windowHasFocus)
				{
					mouseWindowXRelative += ev.motion.xrel;
					mouseWindowYRelative += ev.motion.yrel;
				}

				// Show system mouse pointer if outside screen.
				SDL_ShowCursor(mouse_x < 0 || mouse_x >= vga_width ||
				               mouse_y < 0 || mouse_y >= vga_height ? SDL_TRUE : SDL_FALSE);

				if (ev.motion.xrel != 0 || ev.motion.yrel != 0)
					mouseInactive = false;
				break;

			case SDL_MOUSEBUTTONDOWN:
				mouseInactive = false;

				// fall through
			case SDL_MOUSEBUTTONUP:
				mapWindowPointToScreen(&ev.button.x, &ev.button.y);
				if (ev.type == SDL_MOUSEBUTTONDOWN)
				{
					newmouse = true;
					lastmouse_but = ev.button.button;
					lastmouse_x = ev.button.x;
					lastmouse_y = ev.button.y;
					mousedown = true;
				}
				else
				{
					mousedown = false;
				}

				int whichMB = -1;
				switch (ev.button.button)
				{
					case SDL_BUTTON_LEFT:   whichMB = 0; break;
					case SDL_BUTTON_RIGHT:  whichMB = 1; break;
					case SDL_BUTTON_MIDDLE: whichMB = 2; break;
				}
				if (whichMB < 0)
					break;

				switch (mouseSettings[whichMB])
				{
					case 1: // Fire Main Weapons
						mouse_pressed[0] = mousedown;
						break;
					case 2: // Fire Left Sidekick
						mouse_pressed[1] = mousedown;
						break;
					case 3: // Fire Right Sidekick
						mouse_pressed[2] = mousedown;
						break;
					case 4: // Fire BOTH Sidekicks
						mouse_pressed[1] = mousedown;
						mouse_pressed[2] = mousedown;
						break;
					case 5: // Change Rear Mode
						mouse_pressed[3] = mousedown;
						break;
				}
				break;

			case SDL_TEXTINPUT:
				SDL_strlcpy(last_text, ev.text.text, COUNTOF(last_text));
				new_text = true;
				break;

			case SDL_TEXTEDITING:
				break;

			case SDL_QUIT:
				/* TODO: Call the cleanup code here. */
				exit(0);
				break;

#ifdef __IPHONEOS__
		case SDL_FINGERDOWN:
		case SDL_FINGERMOTION:
		case SDL_FINGERUP:
			handleFingerEvent(&ev.tfinger, ev.type);
			break;

		case SDL_APP_TERMINATING:
			exit(0);
			break;
#endif
		}
	}
}

void JE_clearKeyboard(void)
{
	// /!\ Doesn't seems important. I think. D:
}
