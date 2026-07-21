/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

#include "tjsCommHead.h"
#include "WindowImpl.h"
#include "VirtualKey.h"
#include "Application.h"
#include "SystemImpl.h"
#include "TVPWindow.h"
#include "SysInitIntf.h"
#include "SysInitImpl.h"
#include "CharacterSet.h"
#include "WaveImpl.h"
#include "TimerThread.h"
#include "MsgIntf.h"
#include "DebugIntf.h"
#include "tjsArray.h"
#include "StorageIntf.h"
#include "SDLBitmapCompletion.h"
#include "TouchPoint.h"
#include "VelocityTracker.h"
#include "TVPTimer.h"
#include "MacWindowBridge.h"
#include "ScriptMgnIntf.h"
#include "SystemControl.h"
#include "PluginImpl.h"
#ifdef KRKRZ_ENABLE_CANVAS
#include "OpenGLScreenSDL3.h"
#endif
#include <SDL3/SDL.h>
#include <CoreText/CoreText.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <unistd.h>
#include <vector>

extern void TVPLoadMessage();

class TVPWindowWindow;
static TVPWindowWindow *_lastWindowWindow, *_currentWindowWindow;
static SDL_Gamepad **sdl_gamepads = nullptr;
static int sdl_gamepad_count = 0;

static bool process_events();

static void close_gamepads()
{
	if (!sdl_gamepads)
	{
		sdl_gamepad_count = 0;
		return;
	}
	for (int i = 0; i < sdl_gamepad_count; i += 1)
	{
		if (sdl_gamepads[i])
		{
			SDL_CloseGamepad(sdl_gamepads[i]);
			sdl_gamepads[i] = nullptr;
		}
	}
	SDL_free(sdl_gamepads);
	sdl_gamepads = nullptr;
	sdl_gamepad_count = 0;
}

static void refresh_gamepads()
{
	if (!SDL_WasInit(SDL_INIT_GAMEPAD))
	{
		if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
		{
			TVPAddLog(ttstr("Could not initialize SDL gamepad subsystem: ") + ttstr(SDL_GetError()));
			return;
		}
	}
	close_gamepads();

	SDL_JoystickID *gamepad_ids = SDL_GetGamepads(&sdl_gamepad_count);
	if (!gamepad_ids)
	{
		sdl_gamepad_count = 0;
		TVPAddLog(ttstr("Could not enumerate SDL gamepads: ") + ttstr(SDL_GetError()));
		return;
	}
	if (sdl_gamepad_count > 0)
	{
		sdl_gamepads = static_cast<SDL_Gamepad **>(SDL_calloc(static_cast<size_t>(sdl_gamepad_count), sizeof(SDL_Gamepad *)));
		if (!sdl_gamepads)
		{
			SDL_free(gamepad_ids);
			sdl_gamepad_count = 0;
			TVPAddLog(ttstr("Could not allocate memory to store SDL gamepad information"));
			return;
		}
		for (int i = 0; i < sdl_gamepad_count; i += 1)
		{
			sdl_gamepads[i] = SDL_OpenGamepad(gamepad_ids[i]);
			if (!sdl_gamepads[i])
			{
				TVPAddLog(ttstr("Could not open gamepad: ") + ttstr(SDL_GetError()));
			}
		}
	}
	SDL_free(gamepad_ids);
}

static SDL_GamepadButton vk_key_to_sdl_gamepadbutton(tjs_uint key)
{
	switch (key)
	{
		case VK_PAD1: return SDL_GAMEPAD_BUTTON_SOUTH;
		case VK_PAD2: return SDL_GAMEPAD_BUTTON_EAST;
		case VK_PAD3: return SDL_GAMEPAD_BUTTON_WEST;
		case VK_PAD4: return SDL_GAMEPAD_BUTTON_NORTH;
		case VK_PAD7: return SDL_GAMEPAD_BUTTON_BACK;
		case VK_PAD8: return SDL_GAMEPAD_BUTTON_START;
		case VK_PAD9: return SDL_GAMEPAD_BUTTON_LEFT_STICK;
		case VK_PAD10: return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
		case VK_PAD5: return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
		case VK_PAD6: return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
		case VK_PADUP: return SDL_GAMEPAD_BUTTON_DPAD_UP;
		case VK_PADDOWN: return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
		case VK_PADLEFT: return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
		case VK_PADRIGHT: return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
		default: return SDL_GAMEPAD_BUTTON_INVALID;
	}
}

static tjs_uint sdl_gamepadbutton_to_vk_key(SDL_GamepadButton key)
{
	switch (key)
	{
		case SDL_GAMEPAD_BUTTON_SOUTH: return VK_PAD1;
		case SDL_GAMEPAD_BUTTON_EAST: return VK_PAD2;
		case SDL_GAMEPAD_BUTTON_WEST: return VK_PAD3;
		case SDL_GAMEPAD_BUTTON_NORTH: return VK_PAD4;
		case SDL_GAMEPAD_BUTTON_BACK: return VK_PAD7;
		case SDL_GAMEPAD_BUTTON_START: return VK_PAD8;
		case SDL_GAMEPAD_BUTTON_LEFT_STICK: return VK_PAD9;
		case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return VK_PAD10;
		case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return VK_PAD5;
		case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return VK_PAD6;
		case SDL_GAMEPAD_BUTTON_DPAD_UP: return VK_PADUP;
		case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return VK_PADDOWN;
		case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return VK_PADLEFT;
		case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return VK_PADRIGHT;
		default: return 0;
	}
}

#ifdef MK_SHIFT
#undef MK_SHIFT
#endif
#ifdef MK_CONTROL
#undef MK_CONTROL
#endif
#ifdef MK_ALT
#undef MK_ALT
#endif
#define MK_SHIFT 4
#define MK_CONTROL 8
#define MK_ALT (0x20)

static SDL_Cursor *sdl_system_cursors[SDL_SYSTEM_CURSOR_COUNT] = {0};
static bool sdl_system_cursors_initialized = false;

static void ensure_system_cursors()
{
	if (sdl_system_cursors_initialized)
	{
		return;
	}
	for (int i = 0; i < SDL_SYSTEM_CURSOR_COUNT; i += 1)
	{
		sdl_system_cursors[i] = SDL_CreateSystemCursor(static_cast<SDL_SystemCursor>(i));
	}
	sdl_system_cursors_initialized = true;
}

static void destroy_system_cursors()
{
	if (SDL_WasInit(SDL_INIT_VIDEO))
	{
		SDL_SetCursor(SDL_GetDefaultCursor());
	}
	for (int i = 0; i < SDL_SYSTEM_CURSOR_COUNT; i += 1)
	{
		if (sdl_system_cursors[i])
		{
			SDL_DestroyCursor(sdl_system_cursors[i]);
			sdl_system_cursors[i] = nullptr;
		}
	}
	sdl_system_cursors_initialized = false;
}

static SDL_Keycode vk_key_to_sdl_key(tjs_uint key)
{
	// This is generated using regex find replace
	switch (key)
	{
		case VK_BACK: return SDLK_BACKSPACE;
		case VK_TAB: return SDLK_TAB;
		case VK_CLEAR: return SDLK_CLEAR;
		case VK_RETURN: return SDLK_RETURN;
		case VK_PAUSE: return SDLK_PAUSE;
		case VK_CAPITAL: return SDLK_CAPSLOCK;
		case VK_ESCAPE: return SDLK_ESCAPE;
		case VK_SPACE: return SDLK_SPACE;
		case VK_PRIOR: return SDLK_PAGEUP;
		case VK_NEXT: return SDLK_PAGEDOWN;
		case VK_END: return SDLK_END;
		case VK_HOME: return SDLK_HOME;
		case VK_LEFT: return SDLK_LEFT;
		case VK_UP: return SDLK_UP;
		case VK_RIGHT: return SDLK_RIGHT;
		case VK_DOWN: return SDLK_DOWN;
		case VK_SELECT: return SDLK_SELECT;
		case VK_EXECUTE: return SDLK_EXECUTE;
		case VK_SNAPSHOT: return SDLK_PRINTSCREEN;
		case VK_INSERT: return SDLK_INSERT;
		case VK_DELETE: return SDLK_DELETE;
		case VK_HELP: return SDLK_HELP;
		case VK_0: return SDLK_0;
		case VK_1: return SDLK_1;
		case VK_2: return SDLK_2;
		case VK_3: return SDLK_3;
		case VK_4: return SDLK_4;
		case VK_5: return SDLK_5;
		case VK_6: return SDLK_6;
		case VK_7: return SDLK_7;
		case VK_8: return SDLK_8;
		case VK_9: return SDLK_9;
		case VK_A: return SDLK_A;
		case VK_B: return SDLK_B;
		case VK_C: return SDLK_C;
		case VK_D: return SDLK_D;
		case VK_E: return SDLK_E;
		case VK_F: return SDLK_F;
		case VK_G: return SDLK_G;
		case VK_H: return SDLK_H;
		case VK_I: return SDLK_I;
		case VK_J: return SDLK_J;
		case VK_K: return SDLK_K;
		case VK_L: return SDLK_L;
		case VK_M: return SDLK_M;
		case VK_N: return SDLK_N;
		case VK_O: return SDLK_O;
		case VK_P: return SDLK_P;
		case VK_Q: return SDLK_Q;
		case VK_R: return SDLK_R;
		case VK_S: return SDLK_S;
		case VK_T: return SDLK_T;
		case VK_U: return SDLK_U;
		case VK_V: return SDLK_V;
		case VK_W: return SDLK_W;
		case VK_X: return SDLK_X;
		case VK_Y: return SDLK_Y;
		case VK_Z: return SDLK_Z;
		case VK_LWIN: return SDLK_LGUI;
		case VK_RWIN: return SDLK_RGUI;
		case VK_SLEEP: return SDLK_SLEEP;
		case VK_NUMPAD0: return SDLK_KP_0;
		case VK_NUMPAD1: return SDLK_KP_1;
		case VK_NUMPAD2: return SDLK_KP_2;
		case VK_NUMPAD3: return SDLK_KP_3;
		case VK_NUMPAD4: return SDLK_KP_4;
		case VK_NUMPAD5: return SDLK_KP_5;
		case VK_NUMPAD6: return SDLK_KP_6;
		case VK_NUMPAD7: return SDLK_KP_7;
		case VK_NUMPAD8: return SDLK_KP_8;
		case VK_NUMPAD9: return SDLK_KP_9;
		case VK_MULTIPLY: return SDLK_KP_MULTIPLY;
		case VK_ADD: return SDLK_KP_PLUS;
		case VK_SUBTRACT: return SDLK_KP_MINUS;
		case VK_DECIMAL: return SDLK_KP_PERIOD;
		case VK_DIVIDE: return SDLK_KP_DIVIDE;
		case VK_F1: return SDLK_F1;
		case VK_F2: return SDLK_F2;
		case VK_F3: return SDLK_F3;
		case VK_F4: return SDLK_F4;
		case VK_F5: return SDLK_F5;
		case VK_F6: return SDLK_F6;
		case VK_F7: return SDLK_F7;
		case VK_F8: return SDLK_F8;
		case VK_F9: return SDLK_F9;
		case VK_F10: return SDLK_F10;
		case VK_F11: return SDLK_F11;
		case VK_F12: return SDLK_F12;
		case VK_F13: return SDLK_F13;
		case VK_F14: return SDLK_F14;
		case VK_F15: return SDLK_F15;
		case VK_F16: return SDLK_F16;
		case VK_F17: return SDLK_F17;
		case VK_F18: return SDLK_F18;
		case VK_F19: return SDLK_F19;
		case VK_F20: return SDLK_F20;
		case VK_F21: return SDLK_F21;
		case VK_F22: return SDLK_F22;
		case VK_F23: return SDLK_F23;
		case VK_F24: return SDLK_F24;
		case VK_NUMLOCK: return SDLK_NUMLOCKCLEAR;
		case VK_SCROLL: return SDLK_SCROLLLOCK;
		case VK_LSHIFT: return SDLK_LSHIFT;
		case VK_RSHIFT: return SDLK_RSHIFT;
		case VK_LCONTROL: return SDLK_LCTRL;
		case VK_RCONTROL: return SDLK_RCTRL;
		case VK_LMENU: return SDLK_LALT;
		case VK_RMENU: return SDLK_RALT;
		case VK_BROWSER_BACK: return SDLK_AC_BACK;
		case VK_BROWSER_FORWARD: return SDLK_AC_FORWARD;
		case VK_BROWSER_REFRESH: return SDLK_AC_REFRESH;
		case VK_BROWSER_STOP: return SDLK_AC_STOP;
		case VK_BROWSER_SEARCH: return SDLK_AC_SEARCH;
		case VK_BROWSER_FAVORITES: return SDLK_AC_BOOKMARKS;
		case VK_BROWSER_HOME: return SDLK_AC_HOME;
		case VK_VOLUME_MUTE: return SDLK_MUTE;
		case VK_VOLUME_DOWN: return SDLK_VOLUMEDOWN;
		case VK_VOLUME_UP: return SDLK_VOLUMEUP;
		case VK_MEDIA_NEXT_TRACK: return SDLK_MEDIA_NEXT_TRACK;
		case VK_MEDIA_PREV_TRACK: return SDLK_MEDIA_PREVIOUS_TRACK;
		case VK_MEDIA_STOP: return SDLK_MEDIA_STOP;
		case VK_MEDIA_PLAY_PAUSE: return SDLK_MEDIA_PLAY;
		case VK_LAUNCH_MAIL: return SDLK_UNKNOWN;
		case VK_LAUNCH_MEDIA_SELECT: return SDLK_MEDIA_SELECT;
		default: return 0;
	}
}

static tjs_uint sdl_key_to_vk_key(SDL_Keycode key)
{
	// This is generated using regex find replace
	switch (key)
	{
		case SDLK_BACKSPACE: return VK_BACK;
		case SDLK_TAB: return VK_TAB;
		case SDLK_CLEAR: return VK_CLEAR;
		case SDLK_RETURN: return VK_RETURN;
		case SDLK_PAUSE: return VK_PAUSE;
		case SDLK_CAPSLOCK: return VK_CAPITAL;
		case SDLK_ESCAPE: return VK_ESCAPE;
		case SDLK_SPACE: return VK_SPACE;
		case SDLK_PAGEUP: return VK_PRIOR;
		case SDLK_PAGEDOWN: return VK_NEXT;
		case SDLK_END: return VK_END;
		case SDLK_HOME: return VK_HOME;
		case SDLK_LEFT: return VK_LEFT;
		case SDLK_UP: return VK_UP;
		case SDLK_RIGHT: return VK_RIGHT;
		case SDLK_DOWN: return VK_DOWN;
		case SDLK_SELECT: return VK_SELECT;
		case SDLK_EXECUTE: return VK_EXECUTE;
		case SDLK_PRINTSCREEN: return VK_SNAPSHOT;
		case SDLK_INSERT: return VK_INSERT;
		case SDLK_DELETE: return VK_DELETE;
		case SDLK_HELP: return VK_HELP;
		case SDLK_0: return VK_0;
		case SDLK_1: return VK_1;
		case SDLK_2: return VK_2;
		case SDLK_3: return VK_3;
		case SDLK_4: return VK_4;
		case SDLK_5: return VK_5;
		case SDLK_6: return VK_6;
		case SDLK_7: return VK_7;
		case SDLK_8: return VK_8;
		case SDLK_9: return VK_9;
		case SDLK_A: return VK_A;
		case SDLK_B: return VK_B;
		case SDLK_C: return VK_C;
		case SDLK_D: return VK_D;
		case SDLK_E: return VK_E;
		case SDLK_F: return VK_F;
		case SDLK_G: return VK_G;
		case SDLK_H: return VK_H;
		case SDLK_I: return VK_I;
		case SDLK_J: return VK_J;
		case SDLK_K: return VK_K;
		case SDLK_L: return VK_L;
		case SDLK_M: return VK_M;
		case SDLK_N: return VK_N;
		case SDLK_O: return VK_O;
		case SDLK_P: return VK_P;
		case SDLK_Q: return VK_Q;
		case SDLK_R: return VK_R;
		case SDLK_S: return VK_S;
		case SDLK_T: return VK_T;
		case SDLK_U: return VK_U;
		case SDLK_V: return VK_V;
		case SDLK_W: return VK_W;
		case SDLK_X: return VK_X;
		case SDLK_Y: return VK_Y;
		case SDLK_Z: return VK_Z;
		case SDLK_LGUI: return VK_LWIN;
		case SDLK_RGUI: return VK_RWIN;
		case SDLK_SLEEP: return VK_SLEEP;
		case SDLK_KP_0: return VK_NUMPAD0;
		case SDLK_KP_1: return VK_NUMPAD1;
		case SDLK_KP_2: return VK_NUMPAD2;
		case SDLK_KP_3: return VK_NUMPAD3;
		case SDLK_KP_4: return VK_NUMPAD4;
		case SDLK_KP_5: return VK_NUMPAD5;
		case SDLK_KP_6: return VK_NUMPAD6;
		case SDLK_KP_7: return VK_NUMPAD7;
		case SDLK_KP_8: return VK_NUMPAD8;
		case SDLK_KP_9: return VK_NUMPAD9;
		case SDLK_KP_MULTIPLY: return VK_MULTIPLY;
		case SDLK_KP_PLUS: return VK_ADD;
		case SDLK_KP_MINUS: return VK_SUBTRACT;
		case SDLK_KP_PERIOD: return VK_DECIMAL;
		case SDLK_KP_DIVIDE: return VK_DIVIDE;
		case SDLK_F1: return VK_F1;
		case SDLK_F2: return VK_F2;
		case SDLK_F3: return VK_F3;
		case SDLK_F4: return VK_F4;
		case SDLK_F5: return VK_F5;
		case SDLK_F6: return VK_F6;
		case SDLK_F7: return VK_F7;
		case SDLK_F8: return VK_F8;
		case SDLK_F9: return VK_F9;
		case SDLK_F10: return VK_F10;
		case SDLK_F11: return VK_F11;
		case SDLK_F12: return VK_F12;
		case SDLK_F13: return VK_F13;
		case SDLK_F14: return VK_F14;
		case SDLK_F15: return VK_F15;
		case SDLK_F16: return VK_F16;
		case SDLK_F17: return VK_F17;
		case SDLK_F18: return VK_F18;
		case SDLK_F19: return VK_F19;
		case SDLK_F20: return VK_F20;
		case SDLK_F21: return VK_F21;
		case SDLK_F22: return VK_F22;
		case SDLK_F23: return VK_F23;
		case SDLK_F24: return VK_F24;
		case SDLK_NUMLOCKCLEAR: return VK_NUMLOCK;
		case SDLK_SCROLLLOCK: return VK_SCROLL;
		case SDLK_LSHIFT: return VK_LSHIFT;
		case SDLK_RSHIFT: return VK_RSHIFT;
		case SDLK_LCTRL: return VK_LCONTROL;
		case SDLK_RCTRL: return VK_RCONTROL;
		case SDLK_MENU: return VK_LMENU;
		case SDLK_LALT: return VK_LMENU;
		case SDLK_RALT: return VK_RMENU;
		case SDLK_AC_BACK: return VK_BROWSER_BACK;
		case SDLK_AC_FORWARD: return VK_BROWSER_FORWARD;
		case SDLK_AC_REFRESH: return VK_BROWSER_REFRESH;
		case SDLK_AC_STOP: return VK_BROWSER_STOP;
		case SDLK_AC_SEARCH: return VK_BROWSER_SEARCH;
		case SDLK_AC_BOOKMARKS: return VK_BROWSER_FAVORITES;
		case SDLK_AC_HOME: return VK_BROWSER_HOME;
		case SDLK_MUTE: return VK_VOLUME_MUTE;
		case SDLK_VOLUMEDOWN: return VK_VOLUME_DOWN;
		case SDLK_VOLUMEUP: return VK_VOLUME_UP;
		case SDLK_MEDIA_NEXT_TRACK: return VK_MEDIA_NEXT_TRACK;
		case SDLK_MEDIA_PREVIOUS_TRACK: return VK_MEDIA_PREV_TRACK;
		case SDLK_MEDIA_STOP: return VK_MEDIA_STOP;
		case SDLK_MEDIA_PLAY: return VK_MEDIA_PLAY_PAUSE;
		case SDLK_MEDIA_SELECT: return VK_LAUNCH_MEDIA_SELECT;
		default: return 0;
	}
}

static int GetShiftState()
{
	int s = 0;
	if (TVPGetAsyncKeyState(VK_MENU)) s |= MK_ALT;
	if (TVPGetAsyncKeyState(VK_LMENU)) s |= MK_ALT;
	if (TVPGetAsyncKeyState(VK_RMENU)) s |= MK_ALT;
	if (TVPGetAsyncKeyState(VK_SHIFT)) s |= MK_SHIFT;
	if (TVPGetAsyncKeyState(VK_LSHIFT)) s |= MK_SHIFT;
	if (TVPGetAsyncKeyState(VK_RCONTROL)) s |= MK_SHIFT;
	if (TVPGetAsyncKeyState(VK_CONTROL)) s |= MK_CONTROL;
	if (TVPGetAsyncKeyState(VK_LCONTROL)) s |= MK_CONTROL;
	if (TVPGetAsyncKeyState(VK_RCONTROL)) s |= MK_CONTROL;
	return s;
}
static int GetMouseButtonState()
{
	int s = 0;
	if (TVPGetAsyncKeyState(VK_LBUTTON)) s |= ssLeft;
	if (TVPGetAsyncKeyState(VK_RBUTTON)) s |= ssRight;
	if (TVPGetAsyncKeyState(VK_MBUTTON)) s |= ssMiddle;
	if (TVPGetAsyncKeyState(VK_XBUTTON1)) s |= ssX1;
	if (TVPGetAsyncKeyState(VK_XBUTTON2)) s |= ssX2;
	return s;
}

class TVPWindowWindow : public TTVPWindowForm, private TouchHandler
{
protected:
	SDL_Window *window = nullptr;

	TVPWindowWindow *_prevWindow;
	TVPWindowWindow *_nextWindow;
	SDL_Texture *texture = nullptr;
	SDL_Renderer *renderer = nullptr;
	SDL_Surface *surface = nullptr;
#ifdef KRKRZ_ENABLE_CANVAS
	SDL_GLContext context = nullptr;
#endif
	tTJSNI_Window *TJSNativeInstance;
	bool hasDrawn = false;
	bool isVisible = true;
	bool visibilityHasInitialized = false;
	bool needsGraphicUpdate = false;
	bool isBeingDeleted = false;
	bool cursorTemporaryHidden = false;
	char *imeCompositionStr = nullptr;
	size_t imeCompositionCursor = 0;
	size_t imeCompositionLen = 0;
	size_t imeCompositionSelection = 0;
	SDL_Rect attentionPointRect = { 0, 0, 0, 0 };
	bool attentionPointEnabled = false;
	tTVPImeMode defaultImeMode = ::imClose;
	tTVPImeMode currentImeMode = ::imClose;
	iTJSDispatch2 *fileDropArray;
	tjs_int fileDropArrayCount;
	TVPSDLBitmapCompletion *bitmapCompletion;
#ifdef KRKRZ_ENABLE_CANVAS
	tTVPOpenGLScreen *openGlScreen;
#endif
	int lastMouseX;
	int lastMouseY;
	VelocityTracker mouseVelocityTracker;
	VelocityTrackers touchVelocityTrackers;
	TouchPointList touchPoints;
	typedef std::pair<SDL_TouchID, SDL_FingerID> TouchKey;
	struct ActiveTouch
	{
		tjs_uint32 id;
		double x;
		double y;
		double cx;
		double cy;
		tjs_uint32 tick;
	};
	std::map<TouchKey, ActiveTouch> activeTouches;
	tjs_uint32 nextTouchID = 1;
	bool enableTouch = true;
	bool useMouseKey = false;
	int mouseKeyXAccel = 0;
	int mouseKeyYAccel = 0;
	Uint64 lastMouseKeyTick = 0;
	bool emulatedLeftButtonDown = false;
	bool emulatedRightButtonDown = false;
	bool gamepadMouseLeft = false;
	bool gamepadMouseRight = false;
	bool gamepadMouseUp = false;
	bool gamepadMouseDown = false;
	std::unique_ptr<TVPTimer> hintTimer;
	tjs_int hintDelay = 500;
	ttstr hintMessage;
	iTJSDispatch2 *lastHintSender = nullptr;
	SDL_Surface *maskSurface = nullptr;
	bool maskActive = false;
	tjs_int maskThreshold = 0;

#ifdef KRKRSDL3_ENABLE_ZOOM
	tTVPRect FullScreenDestRect;
	tTVPRect LastSentDrawDeviceDestRect;

	//-- layer position / size
	tjs_int LayerLeft = 0;
	tjs_int LayerTop = 0;
	tjs_int LayerWidth = 32;
	tjs_int LayerHeight = 32;
	tjs_int ZoomDenom = 1; // Zooming factor denominator (setting)
	tjs_int ZoomNumer = 1; // Zooming factor numerator (setting)
	tjs_int ActualZoomDenom = 1; // Zooming factor denominator (actual)
	tjs_int ActualZoomNumer = 1; // Zooming factor numerator (actual)
	tjs_int InnerWidth = 32;
	tjs_int InnerHeight = 32;
#endif

public:
	TVPWindowWindow(tTJSNI_Window *w);
	virtual ~TVPWindowWindow();
	/* Called from tTJSNI_Window */
	virtual void SetPaintBoxSize(tjs_int w, tjs_int h) override;
	void TranslateWindowToDrawArea(int &x, int &y);
	void TranslateDrawAreaToWindow(int &x, int &y);
	/* Called from tTJSNI_Window */
	virtual bool GetFormEnabled() override;
	virtual void SetStayOnTop(bool b) override;
	virtual bool GetStayOnTop() const override;
	virtual void SetTrapKey(bool b) override;
	virtual bool GetTrapKey() const override;
	virtual void SetFocusable(bool b) override;
	virtual bool GetFocusable() const override;
	/* Called from tTJSNI_Window */
	virtual void SetDefaultMouseCursor() override;
	/* Called from tTJSNI_Window */
	virtual void SetMouseCursor(tjs_int handle) override;
	/* Called from tTJSNI_Window */
	virtual void SetMouseCursorState(tTVPMouseCursorState mcs) override;
	/* Called from tTJSNI_Window */
	virtual tTVPMouseCursorState GetMouseCursorState() const override;
	void RestoreMouseCursor();
	/* Called from tTJSNI_Window */
	virtual void HideMouseCursor() override;
	/* Called from tTJSNI_Window */
	virtual void GetCursorPos(tjs_int &x, tjs_int &y) override;
	/* Called from tTJSNI_Window */
	virtual void SetCursorPos(tjs_int x, tjs_int y) override;
	/* Called from tTJSNI_Window */
	virtual void SetAttentionPoint(tjs_int left, tjs_int top, const struct tTVPFont * font) override;
	virtual void DisableAttentionPoint() override;
	/* Called from tTJSNI_Window */
	virtual void BringToFront() override;
	/* Called from tTJSNI_Window */
	virtual void ShowWindowAsModal() override;
	/* Called from tTJSNI_Window */
	virtual bool GetVisible() override;
	/* Called from member SetVisibleFromScript */
	virtual void SetVisible(bool visible) override;
	/* Called from tTJSNI_Window */
	virtual void SetFullScreenMode(bool fullscreen) override;
	/* Called from tTJSNI_Window */
	virtual bool GetFullScreenMode() override;
	/* Called from tTJSNI_Window */
	virtual void SetBorderStyle(tTVPBorderStyle bs) override;
	/* Called from tTJSNI_Window */
	virtual tTVPBorderStyle GetBorderStyle() const override;
	/* Called from tTJSNI_Window */
	virtual tjs_string GetCaption() override;
	virtual void GetCaption(tjs_string & v) const override;
	/* Called from tTJSNI_Window */
	virtual void SetCaption(const tjs_string & v) override;
	/* Called from tTJSNI_Window */
	virtual void SetWidth(tjs_int w) override;
	/* Called from tTJSNI_Window */
	virtual void SetHeight(tjs_int h) override;
	/* Called from tTJSNI_Window */
	virtual void SetSize(tjs_int w, tjs_int h) override;
	virtual void GetSize(tjs_int &w, tjs_int &h) override;
	/* Called from tTJSNI_Window */
	virtual tjs_int GetWidth() const override;
	/* Called from tTJSNI_Window */
	virtual tjs_int GetHeight() const override;
	/* Called from tTJSNI_Window */
	virtual void SetMinWidth(tjs_int w) override;
	/* Called from tTJSNI_Window */
	virtual void SetMaxWidth(tjs_int w) override;
	/* Called from tTJSNI_Window */
	virtual void SetMinHeight(tjs_int h) override;
	/* Called from tTJSNI_Window */
	virtual void SetMaxHeight(tjs_int h) override;
	/* Called from tTJSNI_Window */
	virtual void SetMinSize(tjs_int w, tjs_int h) override;
	/* Called from tTJSNI_Window */
	virtual void SetMaxSize(tjs_int w, tjs_int h) override;
	/* Called from tTJSNI_Window */
	virtual tjs_int GetMinWidth() override;
	/* Called from tTJSNI_Window */
	virtual tjs_int GetMaxWidth() override;
	/* Called from tTJSNI_Window */
	virtual tjs_int GetMinHeight() override;
	/* Called from tTJSNI_Window */
	virtual tjs_int GetMaxHeight() override;
	/* Called from tTJSNI_Window */
	virtual tjs_int GetLeft() override;
	/* Called from tTJSNI_Window */
	virtual void SetLeft(tjs_int l) override;
	/* Called from tTJSNI_Window */
	virtual tjs_int GetTop() override;
	/* Called from tTJSNI_Window */
	virtual void SetTop(tjs_int t) override;
	/* Called from tTJSNI_Window */
	virtual void SetPosition(tjs_int l, tjs_int t) override;
	virtual TVPSDLBitmapCompletion *GetTVPSDLBitmapCompletion() override;
#ifdef KRKRZ_ENABLE_CANVAS
	virtual void SetOpenGLScreen(tTVPOpenGLScreen *s) override;
	virtual void SetSwapInterval(int interval) override;
	virtual void GetDrawableSize(tjs_int &w, tjs_int &h) override;
	virtual void Swap() override;
#endif
	virtual void Show() override;
	/* Called from tTJSNI_Window */
	virtual void TickBeat() override;
	/* Called from tTJSNI_Window */
	virtual void InvalidateClose() override;
	/* Called from tTJSNI_Window */
	virtual bool GetWindowActive() override;
	bool Closing = false;
	bool ProgramClosing = false;
	bool CanCloseWork = false;
	bool in_mode_ = false; // is modal
	int modal_result_ = 0;
	enum CloseAction
	{
		caNone,
		caHide,
		caFree,
		caMinimize
	};
	void OnClose(CloseAction& action);
	bool OnCloseQuery();
	/* Called from tTJSNI_Window */
	virtual void Close() override;
	/* Called from tTJSNI_Window */
	virtual void OnCloseQueryCalled(bool b) override;
	/* Called from tTJSNI_Window */
	virtual void SetImeMode(tTVPImeMode mode) override;
	virtual void SetDefaultImeMode(tTVPImeMode mode, bool reset) override;
	virtual tTVPImeMode GetDefaultImeMode() const override;
	/* Called from tTJSNI_Window */
	virtual void ResetImeMode() override;
	virtual void SetHintDelay(tjs_int delay) override;
	virtual tjs_int GetHintDelay() const override;
	virtual void SetHintText(iTJSDispatch2* sender, const ttstr &text) override;
	void UpdateHint();
	/* Called from tTJSNI_Window */
	virtual void UpdateWindow(tTVPUpdateType type) override;
	/* Called from tTJSNI_Window */
	virtual void InternalKeyDown(tjs_uint16 key, tjs_uint32 shift) override;
	/* Called from tTJSNI_Window */
	virtual void OnKeyUp(tjs_uint16 vk, int shift) override;
	/* Called from tTJSNI_Window */
	virtual void OnKeyPress(tjs_uint16 vk, int repeat, bool prevkeystate, bool convertkey) override;
	void UpdateActualZoom(void);
	void SetDrawDeviceDestRect(void);
	/* Called from tTJSNI_Window */
	virtual void SetZoom(tjs_int numer, tjs_int denom, bool set_logical = true) override;
	/* Called from tTJSNI_Window */
	virtual void SetZoomNumer(tjs_int n) override;
	/* Called from tTJSNI_Window */
	virtual tjs_int GetZoomNumer() const override;
	/* Called from tTJSNI_Window */
	virtual void SetZoomDenom(tjs_int d) override;
	/* Called from tTJSNI_Window */
	virtual tjs_int GetZoomDenom() const override;
	/* Called from tTJSNI_Window */
	virtual void SetInnerWidth(tjs_int v) override;
	/* Called from tTJSNI_Window */
	virtual void SetInnerHeight(tjs_int v) override;
	/* Called from tTJSNI_Window */
	virtual void SetInnerSize(tjs_int w, tjs_int h) override;
	/* Called from tTJSNI_Window */
	virtual tjs_int GetInnerWidth() override;
	/* Called from tTJSNI_Window */
	virtual tjs_int GetInnerHeight() override;
	virtual void SetUseMouseKey(bool b) override;
	virtual bool GetUseMouseKey() const override;
	virtual void ReleaseMouseCapture() override;
	virtual void SetEnableTouch(bool b) override;
	virtual bool GetEnableTouch() const override;
	virtual void ResetMouseVelocity() override;
	virtual void ResetTouchVelocity(tjs_int id) override;
	virtual bool GetMouseVelocity(float& x, float& y, float& speed) const override;
	virtual bool GetTouchVelocity(tjs_int id, float& x, float& y, float& speed) const override;
	virtual void SetTouchScaleThreshold(double threshold) override;
	virtual double GetTouchScaleThreshold() const override;
	virtual void SetTouchRotateThreshold(double threshold) override;
	virtual double GetTouchRotateThreshold() const override;
	virtual tjs_real GetTouchPointStartX(tjs_int index) const override;
	virtual tjs_real GetTouchPointStartY(tjs_int index) const override;
	virtual tjs_real GetTouchPointX(tjs_int index) const override;
	virtual tjs_real GetTouchPointY(tjs_int index) const override;
	virtual tjs_int GetTouchPointID(tjs_int index) const override;
	virtual tjs_int GetTouchPointCount() const override;
	virtual void ZoomRectangle(tjs_int &left, tjs_int &top, tjs_int &right, tjs_int &bottom) override;
	virtual void GetVideoOffset(tjs_int &ofsx, tjs_int &ofsy) override;
	virtual void SetMaskRegion(tTVPBaseBitmap *bitmap, tjs_int threshold) override;
	virtual void RemoveMaskRegion() override;
	virtual void SetWaitVSync(bool enabled) override;
	virtual void ResetDrawDevice() override;
	void SyncWindowShadow();
	void ApplySoftwareMask(const SDL_Rect &rect);
	void ClearImeComposition();
	void ApplyTextInputArea();
	tjs_uint32 AllocateTouchID();
	void HandleTouchEvent(const SDL_TouchFingerEvent &event);
	void CancelActiveTouches(bool postEvents);
	bool HandleMouseKey(const SDL_KeyboardEvent &event, bool down);
	void GenerateMouseKeyMovement(bool forceLeft, bool forceRight, bool forceUp, bool forceDown);
	void PostEmulatedMouseButton(tTVPMouseButton button, bool down, bool click = true);
	virtual void OnTouchScaling(double startdist, double currentdist, double cx, double cy, int flag) override;
	virtual void OnTouchRotate(double startangle, double currentangle, double distance, double cx, double cy, int flag) override;
	virtual void OnMultiTouch() override;
	bool should_try_parent_window(SDL_Event event);
	void window_receive_event(SDL_Event event);
	bool window_receive_event_input(SDL_Event event);
};

TVPWindowWindow::TVPWindowWindow(tTJSNI_Window *w) : touchPoints(this)
{
	this->fileDropArray = nullptr;
	this->fileDropArrayCount = 0;
	this->lastMouseX = 0;
	this->lastMouseY = 0;
	this->_nextWindow = nullptr;
	this->_prevWindow = _lastWindowWindow;
	_lastWindowWindow = this;
	if (this->_prevWindow)
	{
		this->_prevWindow->_nextWindow = this;
	}
	if (!_currentWindowWindow)
	{
		_currentWindowWindow = this;
	}
	this->TJSNativeInstance = w;

	if (!SDL_WasInit(SDL_INIT_VIDEO))
	{
		if (!SDL_Init(SDL_INIT_VIDEO))
		{
			TVPThrowExceptionMessage(TJS_W("Cannot initialize SDL video subsystem: %1"), ttstr(SDL_GetError()));
		}
	}
	refresh_gamepads();

	int new_window_w = 640;
	int new_window_h = 480;
	SDL_WindowFlags window_flags = SDL_WINDOW_TRANSPARENT;

#ifdef KRKRZ_ENABLE_CANVAS
	if (!TVPIsEnableDrawDevice())
	{
#ifdef SDL_HINT_OPENGL_ES_DRIVER
		SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
#endif
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
		SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
		window_flags |= SDL_WINDOW_OPENGL;
	}
#endif

#ifdef SDL_HINT_TOUCH_MOUSE_EVENTS
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "1");
#endif
#ifdef SDL_HINT_MOUSE_TOUCH_EVENTS
	SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
#endif

#ifdef KRKRSDL3_WINDOW_SIZE_IS_LAYER_SIZE
	window_flags |= SDL_WINDOW_RESIZABLE;
	window_flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif

	this->window = SDL_CreateWindow("krkrsdl3", new_window_w, new_window_h, window_flags);
	if (!this->window)
	{
		TVPThrowExceptionMessage(TJS_W("Cannot create SDL window: %1"), ttstr(SDL_GetError()));
	}
#ifdef KRKRSDL3_WINDOW_SIZE_IS_LAYER_SIZE
	SDL_SetWindowFullscreen(this->window, true);
#endif
#ifdef KRKRZ_ENABLE_CANVAS
	this->context = nullptr;
	if (!TVPIsEnableDrawDevice())
	{
		this->context = SDL_GL_CreateContext(this->window);
		if (!this->context)
		{
			TVPThrowExceptionMessage(TJS_W("Cannot create SDL context: %1"), ttstr(SDL_GetError()));
		}
		SDL_GL_MakeCurrent(this->window, this->context);
	}
#endif
	this->bitmapCompletion = nullptr;
#ifdef KRKRZ_ENABLE_CANVAS
	this->openGlScreen = nullptr;
#endif
#ifdef KRKRZ_ENABLE_CANVAS
	if (TVPIsEnableDrawDevice())
#endif
	{
		this->renderer = SDL_CreateRenderer(this->window, nullptr);
		if (!this->renderer)
		{
			TVPAddLog(ttstr("Cannot create SDL renderer: ") + ttstr(SDL_GetError()));
		}

		this->bitmapCompletion = new TVPSDLBitmapCompletion();
		if (!this->renderer)
		{
			this->surface = SDL_GetWindowSurface(this->window);
			if (!this->surface)
			{
				TVPAddLog(ttstr("Cannot get surface from SDL window: ") + ttstr(SDL_GetError()));
			}
			this->bitmapCompletion->surface = this->surface;
		}
		if (!this->renderer && !this->surface)
		{
			TVPThrowExceptionMessage(TJS_W("Cannot get surface or renderer from SDL window"));
		}
		if (this->renderer)
		{
			SDL_SetRenderDrawColor(this->renderer, 0x00, 0x00, 0x00, 0xFF);
		}
	}
	this->SetWaitVSync(w && w->GetWaitVSync());
	this->SyncWindowShadow();
	Application->AddWindow(this);
}

TVPWindowWindow::~TVPWindowWindow()
{
	this->hintTimer.reset();
	this->CancelActiveTouches(false);
	this->ClearImeComposition();
	if (this->maskSurface)
	{
		SDL_DestroySurface(this->maskSurface);
		this->maskSurface = nullptr;
	}
	if (_lastWindowWindow == this)
	{
		_lastWindowWindow = this->_prevWindow;
	}
	if (this->_nextWindow)
	{
		this->_nextWindow->_prevWindow = this->_prevWindow;
	}
	if (this->_prevWindow)
	{
		this->_prevWindow->_nextWindow = this->_nextWindow;
	}
	if (_currentWindowWindow == this)
	{
		_currentWindowWindow = _lastWindowWindow;
	}
	if (this->bitmapCompletion)
	{
		delete this->bitmapCompletion;
		this->bitmapCompletion = nullptr;
	}
#ifdef KRKRZ_ENABLE_CANVAS
	if (this->context)
	{
		SDL_GL_DestroyContext(this->context);
		this->context = nullptr;
	}
#endif
	if (this->texture)
	{
		SDL_DestroyTexture(this->texture);
		this->texture = nullptr;
	}
	if (this->surface && this->renderer)
	{
		SDL_DestroySurface(this->surface);
		this->surface = nullptr;
	}
	if (this->renderer)
	{
		SDL_DestroyRenderer(this->renderer);
		this->renderer = nullptr;
	}
	if (this->window)
	{
		SDL_DestroyWindow(this->window);
		this->window = nullptr;
	}

	Application->RemoveWindow(this);
}

void TVPWindowWindow::SetPaintBoxSize(tjs_int w, tjs_int h)
{
#ifdef KRKRSDL3_ENABLE_ZOOM
	this->LayerWidth = w;
	this->LayerHeight = h;
#endif
	if (this->renderer)
	{
		if (this->texture)
		{
			SDL_DestroyTexture(this->texture);
			this->texture = nullptr;
		}
		this->texture = SDL_CreateTexture(this->renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
		if (!this->texture)
		{
			TVPThrowExceptionMessage(TJS_W("Cannot create texture texture: %1"), ttstr(SDL_GetError()));
		}
		SDL_SetTextureScaleMode(this->texture, SDL_SCALEMODE_LINEAR);
		this->bitmapCompletion->surface = nullptr;
		if (this->surface)
		{
			SDL_DestroySurface(this->surface);
			this->surface = nullptr;
		}
		this->surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_XRGB8888);
		if (!this->surface)
		{
			TVPThrowExceptionMessage(TJS_W("Cannot create surface: %1"), ttstr(SDL_GetError()));
		}
		this->bitmapCompletion->surface = this->surface;
	}
#ifndef KRKRSDL3_ENABLE_ZOOM
	SDL_Rect cliprect;
	cliprect.x = 0;
	cliprect.y = 0;
	cliprect.w = w;
	cliprect.h = h;
#endif
	if (this->renderer)
	{
#ifdef KRKRSDL3_ENABLE_ZOOM
		this->UpdateActualZoom();
#else
		SDL_SetRenderLogicalPresentation(this->renderer, w, h, SDL_LOGICAL_PRESENTATION_LETTERBOX);
#endif
	}
	if (this->TJSNativeInstance)
	{
		tTVPRect r;
		r.left = 0;
		r.top = 0;
		r.right = w;
		r.bottom = h;
		this->TJSNativeInstance->NotifyWindowExposureToLayer(r);
		this->TJSNativeInstance->GetDrawDevice()->SetClipRectangle(r);
		this->TJSNativeInstance->GetDrawDevice()->SetDestRectangle(r);
	}
}

static int MulDiv(int nNumber, int nNumerator, int nDenominator)
{
	return (int)(((int64_t)nNumber * (int64_t)nNumerator) / nDenominator);
}

void TVPWindowWindow::TranslateWindowToDrawArea(int &x, int &y)
{
#ifdef KRKRSDL3_ENABLE_ZOOM
#ifdef KRKRZ_ENABLE_CANVAS
	if (this->context)
	{
		return;
	}
#endif
	x -= this->LastSentDrawDeviceDestRect.left;
	y -= this->LastSentDrawDeviceDestRect.top;
	x = MulDiv(x, this->GetInnerWidth(), this->LastSentDrawDeviceDestRect.get_width());
	y = MulDiv(y, this->GetInnerHeight(), this->LastSentDrawDeviceDestRect.get_height());
	return;
#endif
	if(this->renderer)
	{
		float draw_x = static_cast<float>(x);
		float draw_y = static_cast<float>(y);
		if(SDL_RenderCoordinatesFromWindow(this->renderer, draw_x, draw_y, &draw_x, &draw_y))
		{
			x = static_cast<int>(std::lround(draw_x));
			y = static_cast<int>(std::lround(draw_y));
		}
	}
}

void TVPWindowWindow::TranslateDrawAreaToWindow(int &x, int &y)
{
#ifdef KRKRSDL3_ENABLE_ZOOM
#ifdef KRKRZ_ENABLE_CANVAS
	if (this->context)
	{
		return;
	}
#endif
	x = MulDiv(x, this->LastSentDrawDeviceDestRect.get_width(), this->GetInnerWidth());
	y = MulDiv(y, this->LastSentDrawDeviceDestRect.get_height(), this->GetInnerHeight());
	x += this->LastSentDrawDeviceDestRect.left;
	y += this->LastSentDrawDeviceDestRect.top;
	return;
#endif
	if(this->renderer)
	{
		float window_x = static_cast<float>(x);
		float window_y = static_cast<float>(y);
		if(SDL_RenderCoordinatesToWindow(this->renderer, window_x, window_y, &window_x, &window_y))
		{
			x = static_cast<int>(std::lround(window_x));
			y = static_cast<int>(std::lround(window_y));
		}
	}
}

bool TVPWindowWindow::GetFormEnabled()
{
	return this->window && !(SDL_GetWindowFlags(this->window) & SDL_WINDOW_HIDDEN);
}
void TVPWindowWindow::SetStayOnTop(bool b)
{
	if(this->window && !SDL_SetWindowAlwaysOnTop(this->window, b))
		TVPAddLog(ttstr("Cannot change SDL always-on-top state: ") + ttstr(SDL_GetError()));
}
bool TVPWindowWindow::GetStayOnTop() const
{
	return this->window && (SDL_GetWindowFlags(this->window) & SDL_WINDOW_ALWAYS_ON_TOP) != 0;
}
void TVPWindowWindow::SetTrapKey(bool b)
{
	if(this->window && !SDL_SetWindowKeyboardGrab(this->window, b))
		TVPAddLog(ttstr("Cannot change SDL keyboard grab: ") + ttstr(SDL_GetError()));
}
bool TVPWindowWindow::GetTrapKey() const
{
	return this->window && SDL_GetWindowKeyboardGrab(this->window);
}
void TVPWindowWindow::SetFocusable(bool b)
{
	if(this->window && !SDL_SetWindowFocusable(this->window, b))
		TVPAddLog(ttstr("Cannot change SDL window focusability: ") + ttstr(SDL_GetError()));
}
bool TVPWindowWindow::GetFocusable() const
{
	return !this->window || (SDL_GetWindowFlags(this->window) & SDL_WINDOW_NOT_FOCUSABLE) == 0;
}
void TVPWindowWindow::SetDefaultMouseCursor()
{
	ensure_system_cursors();
	SDL_SetCursor(sdl_system_cursors[SDL_SYSTEM_CURSOR_DEFAULT]);
}
void TVPWindowWindow::SetMouseCursor(tjs_int handle)
{
	ensure_system_cursors();
	switch (handle)
	{
		case -2: // crArrow
			SDL_SetCursor(sdl_system_cursors[SDL_SYSTEM_CURSOR_DEFAULT]);
			break;
		case -3: // crCross
			SDL_SetCursor(sdl_system_cursors[SDL_SYSTEM_CURSOR_CROSSHAIR]);
			break;
		case -4: // crIBeam
			SDL_SetCursor(sdl_system_cursors[SDL_SYSTEM_CURSOR_TEXT]);
			break;
		case -5: // crSize
			SDL_SetCursor(sdl_system_cursors[SDL_SYSTEM_CURSOR_MOVE]);
			break;
		case -6: // crSizeNESW
			SDL_SetCursor(sdl_system_cursors[SDL_SYSTEM_CURSOR_NESW_RESIZE]);
			break;
		case -7: // crSizeNS
			SDL_SetCursor(sdl_system_cursors[SDL_SYSTEM_CURSOR_NS_RESIZE]);
			break;
		case -8: // crSizeNWSE
			SDL_SetCursor(sdl_system_cursors[SDL_SYSTEM_CURSOR_NWSE_RESIZE]);
			break;
		case -9: // crSizeWE
			SDL_SetCursor(sdl_system_cursors[SDL_SYSTEM_CURSOR_EW_RESIZE]);
			break;
		case -11: // crHourGlass
			SDL_SetCursor(sdl_system_cursors[SDL_SYSTEM_CURSOR_WAIT]);
			break;
		case -18: // crNo
			SDL_SetCursor(sdl_system_cursors[SDL_SYSTEM_CURSOR_NOT_ALLOWED]);
			break;
		case -19: // crAppStart
			SDL_SetCursor(sdl_system_cursors[SDL_SYSTEM_CURSOR_PROGRESS]);
			break;
		case -21: // crHandPoint
			SDL_SetCursor(sdl_system_cursors[SDL_SYSTEM_CURSOR_POINTER]);
			break;
		case -22: // crSizeAll
			SDL_SetCursor(sdl_system_cursors[SDL_SYSTEM_CURSOR_MOVE]);
			break;
		default:
			SDL_SetCursor(sdl_system_cursors[SDL_SYSTEM_CURSOR_DEFAULT]);
			break;
	}
}
void TVPWindowWindow::SetMouseCursorState(tTVPMouseCursorState mcs)
{
	this->cursorTemporaryHidden = (mcs == mcsTempHidden);
	if (mcs == mcsVisible)
	{
		SDL_ShowCursor();
	}
	else
	{
		SDL_HideCursor();
	}
}
tTVPMouseCursorState TVPWindowWindow::GetMouseCursorState() const
{
	return this->cursorTemporaryHidden ? mcsTempHidden : (SDL_CursorVisible() ? mcsVisible : mcsHidden);
}
void TVPWindowWindow::HideMouseCursor()
{
	this->SetMouseCursorState(mcsTempHidden);
}
void TVPWindowWindow::RestoreMouseCursor()
{
	if (this->cursorTemporaryHidden)
	{
		this->SetMouseCursorState(mcsVisible);
	}
}
void TVPWindowWindow::GetCursorPos(tjs_int &x, tjs_int &y)
{
	x = this->lastMouseX;
	y = this->lastMouseY;
	if (this->window != SDL_GetMouseFocus())
	{
		return;
	}
	float mouse_x = 0.0f;
	float mouse_y = 0.0f;
	SDL_GetMouseState(&mouse_x, &mouse_y);
	tjs_int new_x = static_cast<tjs_int>(std::lround(mouse_x));
	tjs_int new_y = static_cast<tjs_int>(std::lround(mouse_y));
	this->TranslateWindowToDrawArea(new_x, new_y);
	x = new_x;
	y = new_y;
}
void TVPWindowWindow::SetCursorPos(tjs_int x, tjs_int y)
{
	this->RestoreMouseCursor();
	if (!this->window)
	{
		return;
	}
	tjs_int new_x = x;
	tjs_int new_y = y;

	this->TranslateDrawAreaToWindow(new_x, new_y);
	SDL_WarpMouseInWindow(this->window, static_cast<float>(new_x), static_cast<float>(new_y));
}
void TVPWindowWindow::SetAttentionPoint(tjs_int left, tjs_int top, const struct tTVPFont *font)
{
	if (!font)
	{
		return;
	}
	this->attentionPointRect.x = left;
	this->attentionPointRect.y = top;
	this->attentionPointRect.w = 0;
	this->attentionPointRect.h = font->Height;
	this->TranslateDrawAreaToWindow(this->attentionPointRect.x, this->attentionPointRect.y);
	this->attentionPointEnabled = true;
	this->ApplyTextInputArea();
}
void TVPWindowWindow::DisableAttentionPoint()
{
	this->attentionPointEnabled = false;
	this->ApplyTextInputArea();
}
void TVPWindowWindow::BringToFront()
{
	if (_currentWindowWindow != this)
	{
		if (_currentWindowWindow)
		{
			_currentWindowWindow->TJSNativeInstance->OnReleaseCapture();
		}
		_currentWindowWindow = this;
	}
	if (this->window)
	{
		SDL_RaiseWindow(this->window);
	}
}
void TVPWindowWindow::ShowWindowAsModal()
{
#if defined(KRKRSDL3_WINDOW_SIZE_IS_LAYER_SIZE)
	TVPThrowExceptionMessage(TJS_W("Showing window as modal is not supported"));
#else
	this->in_mode_ = true;
	this->BringToFront();
	this->modal_result_ = 0;
	while (this == _currentWindowWindow && !this->modal_result_)
	{
		process_events();
		if (::Application->IsTarminate())
		{
			this->modal_result_ = mrCancel;
		}
		else if (this->modal_result_)
		{
			break;
		}
	}
	this->in_mode_ = false;
#endif
}
bool TVPWindowWindow::GetVisible()
{
	return (!this->visibilityHasInitialized) ? this->isVisible : (this->window && !(SDL_GetWindowFlags(this->window) & SDL_WINDOW_HIDDEN));
}
void TVPWindowWindow::SetVisible(bool visible)
{
	this->isVisible = visible;
	if (!this->visibilityHasInitialized)
	{
		return;
	}
	if (this->window)
	{
#ifndef KRKRSDL3_WINDOW_SIZE_IS_LAYER_SIZE
		if (visible)
		{
			SDL_ShowWindow(this->window);
		}
		else
		{
			SDL_HideWindow(this->window);
		}
#endif
	}
	if (visible)
	{
		this->BringToFront();
	}
	else if (_currentWindowWindow == this)
	{
		_currentWindowWindow = this->_prevWindow ? this->_prevWindow : this->_nextWindow;
		if (_currentWindowWindow)
		{
			_currentWindowWindow->BringToFront();
		}
	}
}
void TVPWindowWindow::SetFullScreenMode(bool fullscreen)
{
#ifndef KRKRSDL3_WINDOW_SIZE_IS_LAYER_SIZE
	if (this->window)
	{
		if(!SDL_SetWindowFullscreen(this->window, fullscreen))
			TVPAddLog(ttstr("Cannot change SDL fullscreen state: ") + ttstr(SDL_GetError()));
	}
	this->SyncWindowShadow();
	this->UpdateWindow(utNormal);
#endif
}
bool TVPWindowWindow::GetFullScreenMode()
{
#ifndef KRKRSDL3_WINDOW_SIZE_IS_LAYER_SIZE
	return this->window && !!(SDL_GetWindowFlags(this->window) & SDL_WINDOW_FULLSCREEN);
#else
	return false;
#endif
}
void TVPWindowWindow::SetBorderStyle(tTVPBorderStyle bs)
{
#ifndef KRKRSDL3_WINDOW_SIZE_IS_LAYER_SIZE
	if (this->window)
	{
		if(!SDL_SetWindowBordered(this->window, bs != bsNone))
			TVPAddLog(ttstr("Cannot change SDL window border: ") + ttstr(SDL_GetError()));
		if(!SDL_SetWindowResizable(this->window, bs == bsSizeable || bs == bsSizeToolWin))
			TVPAddLog(ttstr("Cannot change SDL window resizable state: ") + ttstr(SDL_GetError()));
		this->SyncWindowShadow();
	}
#endif
}
tTVPBorderStyle TVPWindowWindow::GetBorderStyle() const
{
#ifndef KRKRSDL3_WINDOW_SIZE_IS_LAYER_SIZE
	if (this->window)
	{
		SDL_WindowFlags flags = SDL_GetWindowFlags(this->window);
		if (flags & SDL_WINDOW_BORDERLESS)
		{
			return bsNone;
		}
		else if (flags & SDL_WINDOW_RESIZABLE)
		{
			return bsSizeable;
		}
	}
#endif
	return bsSingle;
}
tjs_string TVPWindowWindow::GetCaption()
{
	std::string v_utf8 = this->window ? SDL_GetWindowTitle(this->window) : "";
	tjs_string v_utf16;
	TVPUtf8ToUtf16(v_utf16, v_utf8);
	return v_utf16;
}
void TVPWindowWindow::GetCaption(tjs_string & v) const
{
	v.clear();
	std::string v_utf8 = this->window ? SDL_GetWindowTitle(this->window) : "";
	TVPUtf8ToUtf16(v, v_utf8);
}
void TVPWindowWindow::SetCaption(const tjs_string & v)
{
	if (!this->window)
	{
		return;
	}
	std::string v_utf8;
	if (!TVPUtf16ToUtf8(v_utf8, v))
	{
		return;
	}
	SDL_SetWindowTitle(this->window, v_utf8.c_str());
}
void TVPWindowWindow::SetWidth(tjs_int w)
{
#ifndef KRKRSDL3_WINDOW_SIZE_IS_LAYER_SIZE
	if (this->window)
	{
		int h;
		SDL_GetWindowSize(this->window, nullptr, &h);
		SDL_SetWindowSize(this->window, w, h);
		if (!this->renderer && this->surface)
		{
			this->bitmapCompletion->surface = nullptr;
			this->surface = SDL_GetWindowSurface(this->window);
			if (!this->surface)
			{
				TVPThrowExceptionMessage(TJS_W("Cannot get surface from SDL window: %1"), ttstr(SDL_GetError()));
			}
			this->bitmapCompletion->surface = this->surface;
		}
	}
#endif
#ifdef KRKRSDL3_ENABLE_ZOOM
	this->UpdateActualZoom();
#else
	this->UpdateWindow(utNormal);
#endif
}
void TVPWindowWindow::SetHeight(tjs_int h)
{
#ifndef KRKRSDL3_WINDOW_SIZE_IS_LAYER_SIZE
	if (this->window)
	{
		int w;
		SDL_GetWindowSize(this->window, &w, nullptr);
		SDL_SetWindowSize(this->window, w, h);
		if (!this->renderer && this->surface)
		{
			this->bitmapCompletion->surface = nullptr;
			this->surface = SDL_GetWindowSurface(this->window);
			if (!this->surface)
			{
				TVPThrowExceptionMessage(TJS_W("Cannot get surface from SDL window: %1"), ttstr(SDL_GetError()));
			}
			this->bitmapCompletion->surface = this->surface;
		}
	}
#endif
#ifdef KRKRSDL3_ENABLE_ZOOM
	this->UpdateActualZoom();
#else
	this->UpdateWindow(utNormal);
#endif
}
void TVPWindowWindow::SetSize(tjs_int w, tjs_int h)
{
#ifndef KRKRSDL3_WINDOW_SIZE_IS_LAYER_SIZE
	if (this->window)
	{
		SDL_SetWindowSize(this->window, w, h);
		if (!this->renderer && this->surface)
		{
			this->bitmapCompletion->surface = nullptr;
			this->surface = SDL_GetWindowSurface(this->window);
			if (!this->surface)
			{
				TVPThrowExceptionMessage(TJS_W("Cannot get surface from SDL window: %1"), ttstr(SDL_GetError()));
			}
			this->bitmapCompletion->surface = this->surface;
		}
	}
#endif
#ifdef KRKRSDL3_ENABLE_ZOOM
	this->UpdateActualZoom();
#else
	this->UpdateWindow(utNormal);
#endif
}
void TVPWindowWindow::GetSize(tjs_int &w, tjs_int &h)
{
	w = 0;
	h = 0;
#ifndef KRKRSDL3_WINDOW_SIZE_IS_LAYER_SIZE
	if (this->window)
	{
		SDL_GetWindowSize(this->window, &w, &h);
		return;
	}
#endif
	if (this->renderer)
	{
#ifdef KRKRSDL3_ENABLE_ZOOM
		SDL_GetCurrentRenderOutputSize(this->renderer, &w, &h);
#else
		SDL_GetRenderLogicalPresentation(this->renderer, &w, &h, nullptr);
#endif
		return;
	}
}
tjs_int TVPWindowWindow::GetWidth() const
{
	int w = 0;
#ifndef KRKRSDL3_WINDOW_SIZE_IS_LAYER_SIZE
	if (this->window)
	{
		SDL_GetWindowSize(this->window, &w, nullptr);
		return w;
	}
#endif
	if (this->renderer)
	{
		int h;
#ifdef KRKRSDL3_ENABLE_ZOOM
		SDL_GetCurrentRenderOutputSize(this->renderer, &w, &h);
#else
		SDL_GetRenderLogicalPresentation(this->renderer, &w, &h, nullptr);
#endif
	}
	return w;
}
tjs_int TVPWindowWindow::GetHeight() const
{
	int h = 0;
#ifndef KRKRSDL3_WINDOW_SIZE_IS_LAYER_SIZE
	if (this->window)
	{
		SDL_GetWindowSize(this->window, nullptr, &h);
		return h;
	}
#endif
	if (this->renderer)
	{
		int w;
#ifdef KRKRSDL3_ENABLE_ZOOM
		SDL_GetCurrentRenderOutputSize(this->renderer, &w, &h);
#else
		SDL_GetRenderLogicalPresentation(this->renderer, &w, &h, nullptr);
#endif
	}
	return h;
}
void TVPWindowWindow::SetMinWidth(tjs_int w)
{
	this->SetMinSize(w, this->GetMinHeight());
}
void TVPWindowWindow::SetMaxWidth(tjs_int w)
{
	this->SetMaxSize(w, this->GetMaxHeight());
}
void TVPWindowWindow::SetMinHeight(tjs_int h)
{
	this->SetMinSize(this->GetMinWidth(), h);
}
void TVPWindowWindow::SetMaxHeight(tjs_int h)
{
	this->SetMaxSize(this->GetMaxWidth(), h);
}
void TVPWindowWindow::SetMinSize(tjs_int w, tjs_int h)
{
#ifndef KRKRSDL3_WINDOW_SIZE_IS_LAYER_SIZE
	if (this->window)
	{
		SDL_SetWindowMinimumSize(this->window, w, h);
	}
#endif
}
void TVPWindowWindow::SetMaxSize(tjs_int w, tjs_int h)
{
#ifndef KRKRSDL3_WINDOW_SIZE_IS_LAYER_SIZE
	if (this->window)
	{
		SDL_SetWindowMaximumSize(this->window, w, h);
	}
#endif
}
tjs_int TVPWindowWindow::GetMinWidth()
{
	int w = 0;
#ifndef KRKRSDL3_WINDOW_SIZE_IS_LAYER_SIZE
	if (this->window)
	{
		SDL_GetWindowMinimumSize(this->window, &w, nullptr);
		return w;
	}
#endif
	if (this->renderer)
	{
		int h;
#ifdef KRKRSDL3_ENABLE_ZOOM
		SDL_GetCurrentRenderOutputSize(this->renderer, &w, &h);
#else
		SDL_GetRenderLogicalPresentation(this->renderer, &w, &h, nullptr);
#endif
	}
	return w;
}
tjs_int TVPWindowWindow::GetMaxWidth()
{
	int w = 0;
#ifndef KRKRSDL3_WINDOW_SIZE_IS_LAYER_SIZE
	if (this->window)
	{
		SDL_GetWindowMaximumSize(this->window, &w, nullptr);
		return w;
	}
#endif
	if (this->renderer)
	{
		int h;
#ifdef KRKRSDL3_ENABLE_ZOOM
		SDL_GetCurrentRenderOutputSize(this->renderer, &w, &h);
#else
		SDL_GetRenderLogicalPresentation(this->renderer, &w, &h, nullptr);
#endif
	}
	return w;
}
tjs_int TVPWindowWindow::GetMinHeight()
{
	int h = 0;
#ifndef KRKRSDL3_WINDOW_SIZE_IS_LAYER_SIZE
	if (this->window)
	{
		SDL_GetWindowMinimumSize(this->window, &h, nullptr);
		return h;
	}
#endif
	if (this->renderer)
	{
		int w;
#ifdef KRKRSDL3_ENABLE_ZOOM
		SDL_GetCurrentRenderOutputSize(this->renderer, &w, &h);
#else
		SDL_GetRenderLogicalPresentation(this->renderer, &w, &h, nullptr);
#endif
	}
	return h;
}
tjs_int TVPWindowWindow::GetMaxHeight()
{
	int h = 0;
#ifndef KRKRSDL3_WINDOW_SIZE_IS_LAYER_SIZE
	if (this->window)
	{
		SDL_GetWindowMaximumSize(this->window, &h, nullptr);
		return h;
	}
#endif
	if (this->renderer)
	{
		int w;
#ifdef KRKRSDL3_ENABLE_ZOOM
		SDL_GetCurrentRenderOutputSize(this->renderer, &w, &h);
#else
		SDL_GetRenderLogicalPresentation(this->renderer, &w, &h, nullptr);
#endif
	}
	return h;
}
tjs_int TVPWindowWindow::GetLeft()
{
	int x = 0;
#ifndef KRKRSDL3_WINDOW_SIZE_IS_LAYER_SIZE
	if (this->window)
	{
		SDL_GetWindowPosition(this->window, &x, nullptr);
	}
#endif
	return x;
}
void TVPWindowWindow::SetLeft(tjs_int l)
{
	this->SetPosition(l, this->GetTop());
}
tjs_int TVPWindowWindow::GetTop()
{
	int y = 0;
#ifndef KRKRSDL3_WINDOW_SIZE_IS_LAYER_SIZE
	if (this->window)
	{
		SDL_GetWindowPosition(this->window, nullptr, &y);
	}
#endif
	return y;
}
void TVPWindowWindow::SetTop(tjs_int t)
{
	this->SetPosition(this->GetLeft(), t);
}
void TVPWindowWindow::SetPosition(tjs_int l, tjs_int t)
{
#ifndef KRKRSDL3_WINDOW_SIZE_IS_LAYER_SIZE
	if (this->window)
	{
		SDL_SetWindowPosition(this->window, l, t);
	}
#endif
}
TVPSDLBitmapCompletion *TVPWindowWindow::GetTVPSDLBitmapCompletion()
{
	this->needsGraphicUpdate = true;
	return this->bitmapCompletion;
}
#ifdef KRKRZ_ENABLE_CANVAS
void TVPWindowWindow::SetOpenGLScreen(tTVPOpenGLScreen *s)
{
	this->openGlScreen = s;
}
void TVPWindowWindow::SetSwapInterval(int interval)
{
	if (this->context)
	{
		SDL_GL_SetSwapInterval(interval);
	}
}
void TVPWindowWindow::GetDrawableSize(tjs_int &w, tjs_int &h)
{
	w = 0;
	h = 0;
	if (this->context)
	{
		SDL_GetWindowSizeInPixels(this->window, &w, &h);
	}
}
void TVPWindowWindow::Swap()
{
	if (this->context)
	{
		SDL_GL_SwapWindow(this->window);
	}
}
#endif
void TVPWindowWindow::Show()
{
}
void TVPWindowWindow::TickBeat()
{
	if (!this->visibilityHasInitialized)
	{
		this->visibilityHasInitialized = true;
		this->SetVisible(this->isVisible);
	}
	if(this->useMouseKey && this->GetWindowActive())
		this->GenerateMouseKeyMovement(false, false, false, false);
	if (this->needsGraphicUpdate)
	{
		if (this->bitmapCompletion)
		{
			SDL_Rect rect;
			rect.x = this->bitmapCompletion->update_rect.left;
			rect.y = this->bitmapCompletion->update_rect.top;
			rect.w = this->bitmapCompletion->update_rect.get_width();
			rect.h = this->bitmapCompletion->update_rect.get_height();
			SDL_FRect render_rect = {
				static_cast<float>(rect.x), static_cast<float>(rect.y),
				static_cast<float>(rect.w), static_cast<float>(rect.h)
			};
			if (this->renderer)
			{
#if defined(KRKRSDL3_ENABLE_ZOOM) || defined(KRKRSDL3_RENDERER_FULL_UPDATES)
				SDL_RenderFillRect(this->renderer, nullptr);
#else
				SDL_Rect logical_rect;
				SDL_GetRenderLogicalPresentation(this->renderer, &(logical_rect.w), &(logical_rect.h), nullptr);
				if (logical_rect.w == rect.w && logical_rect.h == rect.h)
				{
					// Clear extra artifacts
					SDL_SetRenderLogicalPresentation(this->renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
					SDL_RenderFillRect(this->renderer, nullptr);
					SDL_SetRenderLogicalPresentation(this->renderer, logical_rect.w, logical_rect.h, SDL_LOGICAL_PRESENTATION_LETTERBOX);
				}
#endif
				if (this->texture)
				{
					if (this->surface)
					{
						if ((rect.w + rect.x) > this->surface->w)
						{
							rect.w = this->surface->w;
						}
						if ((rect.h + rect.y) > this->surface->h)
						{
							rect.h = this->surface->h;
						}
						SDL_UpdateTexture(this->texture, &rect, this->surface->pixels, this->surface->pitch);
					}
					render_rect = {
						static_cast<float>(rect.x), static_cast<float>(rect.y),
						static_cast<float>(rect.w), static_cast<float>(rect.h)
					};
#if defined(KRKRSDL3_ENABLE_ZOOM)
					SDL_FRect destrect;
					destrect.x = static_cast<float>(this->LastSentDrawDeviceDestRect.left);
					destrect.y = static_cast<float>(this->LastSentDrawDeviceDestRect.top);
					destrect.w = static_cast<float>(this->LastSentDrawDeviceDestRect.get_width());
					destrect.h = static_cast<float>(this->LastSentDrawDeviceDestRect.get_height());
					SDL_FRect srcrect;
					srcrect.x = 0;
					srcrect.y = 0;
					srcrect.w = static_cast<float>(this->GetInnerWidth());
					srcrect.h = static_cast<float>(this->GetInnerHeight());
					SDL_RenderTexture(this->renderer, this->texture, &srcrect, &destrect);
#elif defined(KRKRSDL3_RENDERER_FULL_UPDATES)
					SDL_RenderTexture(this->renderer, this->texture, nullptr, nullptr);
#else
					SDL_RenderTexture(this->renderer, this->texture, &render_rect, &render_rect);
#endif
				}
				SDL_RenderPresent(this->renderer);
#if !defined(KRKRSDL3_ENABLE_ZOOM) && !defined(KRKRSDL3_RENDERER_FULL_UPDATES)
				if (logical_rect.w == rect.w && logical_rect.h == rect.h)
				{
					// Clear extra artifacts (for the back buffer)
					SDL_SetRenderLogicalPresentation(this->renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
					SDL_RenderFillRect(this->renderer, nullptr);
					SDL_SetRenderLogicalPresentation(this->renderer, logical_rect.w, logical_rect.h, SDL_LOGICAL_PRESENTATION_LETTERBOX);
				}
				if (this->texture)
				{
					SDL_RenderTexture(this->renderer, this->texture, &render_rect, &render_rect);
				}
#endif
				this->hasDrawn = true;
			}
			else if (this->window && this->surface)
			{
				this->ApplySoftwareMask(rect);
				SDL_UpdateWindowSurfaceRects(this->window, &rect, 1);
				this->hasDrawn = true;
			}
			this->needsGraphicUpdate = false;
		}
	}
#ifdef KRKRZ_ENABLE_CANVAS
	else if (this->context && this->TJSNativeInstance)
	{
		this->TJSNativeInstance->StartDrawing();
		this->hasDrawn = true;
	}
#endif
}
void TVPWindowWindow::InvalidateClose()
{
	this->TJSNativeInstance = nullptr;
	this->SetVisible(false);
	delete this;
}
bool TVPWindowWindow::GetWindowActive()
{
	return _currentWindowWindow == this && SDL_GetWindowFlags(this->window) & SDL_WINDOW_INPUT_FOCUS;
}
void TVPWindowWindow::OnClose(CloseAction& action)
{
	action = (!this->modal_result_) ? caNone : caHide;

	if (!this->ProgramClosing || !this->TJSNativeInstance)
	{
		return;
	}
	if (!this->TJSNativeInstance->IsMainWindow())
	{
		action = caFree;
	}
	iTJSDispatch2 *obj = this->TJSNativeInstance->GetOwnerNoAddRef();
	this->TJSNativeInstance->NotifyWindowClose();
	obj->Invalidate(0, nullptr, nullptr, obj);
	this->TJSNativeInstance = nullptr;
	this->SetVisible(false);
}
bool TVPWindowWindow::OnCloseQuery()
{
	// closing actions are 3 patterns;
	// 1. closing action by the user
	// 2. "close" method
	// 3. object invalidation

	if (TVPGetBreathing())
	{
		return false;
	}

	// the default event handler will invalidate this object when an onCloseQuery
	// event reaches the handler.
	if (!this->TJSNativeInstance)
	{
		return true;
	}
	if (this->modal_result_ && this->modal_result_ != mrCancel/* mrCancel=when close button is pushed in modal window */)
	{
		return true;
	}
	iTJSDispatch2 *obj = this->TJSNativeInstance->GetOwnerNoAddRef();
	if (!obj)
	{
		return true;
	}
	tTJSVariant arg[1] = { true };
	static ttstr eventname(TJS_W("onCloseQuery"));

	if (!this->ProgramClosing)
	{
		// close action does not happen immediately
		if (this->TJSNativeInstance)
		{
			TVPPostInputEvent(new tTVPOnCloseInputEvent(this->TJSNativeInstance));
		}

		this->Closing = true; // waiting closing...
	//	TVPSystemControl->NotifyCloseClicked();
		return false;
	}
	else
	{
		this->CanCloseWork = true;
		TVPPostEvent(obj, obj, eventname, 0, TVP_EPT_IMMEDIATE, sizeof(arg)/sizeof(arg[0]), arg);
		process_events(); // for post event
		// this event happens immediately
		// and does not return until done
		return this->CanCloseWork; // CanCloseWork is set by the event handler
	}
}
void TVPWindowWindow::Close()
{
	// closing action by "close" method
	if (this->Closing)
	{
		return; // already waiting closing...
	}

	this->ProgramClosing = true;
	try
	{
		//tTVPWindow::Close();
		if (this->in_mode_)
		{
			this->modal_result_ = mrCancel;
		}
		else if (this->OnCloseQuery())
		{
			CloseAction action = caFree;
			this->OnClose(action);
			switch (action)
			{
				case caNone:
					break;
				case caHide:
					this->SetVisible(false);
					break;
				case caMinimize:
					if (this->window)
					{
						SDL_MinimizeWindow(this->window);
					}
					break;
				case caFree:
				default:
					this->isBeingDeleted = true;
					//::PostMessage(this->GetHandle(), TVP_EV_WINDOW_RELEASE, 0, 0);
					break;
			}
		}
	}
	catch (...)
	{
		this->ProgramClosing = false;
		throw;
	}
	this->ProgramClosing = false;
}
void TVPWindowWindow::OnCloseQueryCalled(bool b)
{
	// closing is allowed by onCloseQuery event handler
	if (!this->ProgramClosing)
	{
		// closing action by the user
		if (b)
		{
			if (this->in_mode_)
			{
				this->modal_result_ = 1; // when modal
			}
			else
			{
				this->SetVisible(false);  // just hide
			}

			this->Closing = false;
			if (this->TJSNativeInstance)
			{
				if (this->TJSNativeInstance->IsMainWindow())
				{
					// this is the main window
					iTJSDispatch2 *obj = this->TJSNativeInstance->GetOwnerNoAddRef();
					obj->Invalidate(0, nullptr, nullptr, obj);
				}
			}
			else
			{
				delete this;
			}
		}
		else
		{
			this->Closing = false;
		}
	}
	else
	{
		// closing action by the program
		this->CanCloseWork = b;
	}
}
void TVPWindowWindow::SetImeMode(tTVPImeMode mode)
{
	if(mode == ::imDontCare) return;
	this->currentImeMode = mode;
	if(!this->window) return;

	if(mode == ::imDisable || mode == ::imClose)
	{
		this->ClearImeComposition();
		if(SDL_TextInputActive(this->window) && !SDL_StopTextInput(this->window))
			TVPAddLog(ttstr("Cannot stop SDL text input: ") + ttstr(SDL_GetError()));
	}
	else
	{
		this->ApplyTextInputArea();
		if(!SDL_TextInputActive(this->window) && !SDL_StartTextInput(this->window))
			TVPAddLog(ttstr("Cannot start SDL text input: ") + ttstr(SDL_GetError()));
	}
}
void TVPWindowWindow::SetDefaultImeMode(tTVPImeMode mode, bool reset)
{
	this->defaultImeMode = mode;
	if(reset) this->ResetImeMode();
}
tTVPImeMode TVPWindowWindow::GetDefaultImeMode() const
{
	return this->defaultImeMode;
}
void TVPWindowWindow::ResetImeMode()
{
	this->SetImeMode(this->defaultImeMode);
}
void TVPWindowWindow::ClearImeComposition()
{
	if(this->imeCompositionStr)
	{
		SDL_free(this->imeCompositionStr);
	}
	this->imeCompositionStr = nullptr;
	this->imeCompositionLen = 0;
	this->imeCompositionCursor = 0;
	this->imeCompositionSelection = 0;
}
void TVPWindowWindow::ApplyTextInputArea()
{
	if(!this->window) return;
	const SDL_Rect *area = this->attentionPointEnabled ? &this->attentionPointRect : nullptr;
	if(!SDL_SetTextInputArea(this->window, area, 0))
		TVPAddLog(ttstr("Cannot set SDL text input area: ") + ttstr(SDL_GetError()));
}
void TVPWindowWindow::SetHintDelay(tjs_int delay)
{
	this->hintDelay = delay;
}
tjs_int TVPWindowWindow::GetHintDelay() const
{
	return this->hintDelay;
}
void TVPWindowWindow::SetHintText(iTJSDispatch2 *sender, const ttstr &text)
{
	const bool update_text = this->hintMessage != text;
	if(update_text && !text.IsEmpty())
	{
		this->hintMessage.Clear();
		this->UpdateHint();
	}
	this->hintMessage = text;

	if(text.IsEmpty())
	{
		if(this->hintTimer) this->hintTimer->SetEnabled(false);
		this->UpdateHint();
	}
	else if(this->lastHintSender != sender || update_text)
	{
		if(this->hintTimer) this->hintTimer->SetEnabled(false);
		if(this->hintDelay > 0)
		{
			if(!this->hintTimer)
			{
				this->hintTimer.reset(new TVPTimer());
				this->hintTimer->SetOnTimerHandler(this, &TVPWindowWindow::UpdateHint);
			}
			this->hintTimer->SetInterval(static_cast<tjs_uint64>(this->hintDelay));
			this->hintTimer->SetEnabled(true);
		}
		else if(this->hintDelay == 0)
		{
			this->UpdateHint();
		}
	}
	this->lastHintSender = sender;
}
void TVPWindowWindow::UpdateHint()
{
	if(this->TJSNativeInstance)
	{
		tjs_int x = 0;
		tjs_int y = 0;
		this->GetCursorPos(x, y);
		TVPPostInputEvent(new tTVPOnHintChangeInputEvent(this->TJSNativeInstance,
			this->hintMessage, x, y, !this->hintMessage.IsEmpty()));
	}
	if(this->hintTimer) this->hintTimer->SetEnabled(false);
}
void TVPWindowWindow::UpdateWindow(tTVPUpdateType type)
{
	if (!this->TJSNativeInstance)
	{
		return;
	}
	tTVPRect r;
#ifdef KRKRSDL3_ENABLE_ZOOM
	r.left = 0;
	r.top = 0;
	r.right = this->LayerWidth;
	r.bottom = this->LayerHeight;
#else
	r.clear();
	if (this->renderer)
	{
		SDL_GetRenderLogicalPresentation(this->renderer, &(r.right), &(r.bottom), nullptr);
		SDL_SetRenderLogicalPresentation(this->renderer, r.right, r.bottom, SDL_LOGICAL_PRESENTATION_LETTERBOX);
	}
	else if (this->window)
	{
		SDL_GetWindowSize(this->window, &(r.right), &(r.bottom));
	}
#endif
	this->TJSNativeInstance->NotifyWindowExposureToLayer(r);
	TVPDeliverWindowUpdateEvents();
}
void TVPWindowWindow::InternalKeyDown(tjs_uint16 key, tjs_uint32 shift)
{
	TVPPostInputEvent(new tTVPOnKeyDownInputEvent(this->TJSNativeInstance, key, shift));
}
void TVPWindowWindow::OnKeyUp(tjs_uint16 vk, int shift)
{
	TVPPostInputEvent(new tTVPOnKeyUpInputEvent(this->TJSNativeInstance, vk, shift));
}
void TVPWindowWindow::OnKeyPress(tjs_uint16 vk, int repeat, bool prevkeystate, bool convertkey)
{
	TVPPostInputEvent(new tTVPOnKeyPressInputEvent(this->TJSNativeInstance, vk));
}

#ifdef KRKRSDL3_ENABLE_ZOOM
//---------------------------------------------------------------------------
//! @brief	do reduction for numer over denom
static void TVPDoReductionNumerAndDenom(tjs_int &n, tjs_int &d)
{
	tjs_int a = n;
	tjs_int b = d;
	while (b)
	{
		tjs_int t = b;
		b = a % b;
		a = t;
	}
	n = n / a;
	d = d / a;
}
#endif

void TVPWindowWindow::UpdateActualZoom(void)
{
#ifdef KRKRSDL3_ENABLE_ZOOM
	if (!this->renderer)
	{
		return;
	}
#ifdef KRKRZ_ENABLE_CANVAS
	if (this->context)
	{
		return;
	}
#endif
	// determine fullscreen zoom factor and client size
	int sb_w, sb_h, zoom_d, zoom_n, output_w, output_h;
	SDL_GetCurrentRenderOutputSize(this->renderer, &output_w, &output_h);

	float layer_aspect = (float)this->GetInnerWidth() / this->GetInnerHeight();
	float output_aspect = (float)output_w / output_h;

	// 0=letterbox, 1=crop
	int scale_policy = 0;
	SDL_Rect viewport;
	if (SDL_fabs(layer_aspect - output_aspect) < 0.0001)
	{
		zoom_n = 1;
		zoom_d = 1;
		viewport.x = 0;
		viewport.y = 0;
		viewport.w = this->GetInnerWidth();
		viewport.h = this->GetInnerHeight();
	}
	else if (layer_aspect > output_aspect)
	{
		if (scale_policy == 1)
		{
			// Crop left and right
			zoom_n = output_h;
			zoom_d = this->GetInnerHeight();
			TVPDoReductionNumerAndDenom(zoom_n, zoom_d);
			viewport.y = 0;
			viewport.h = output_h;
			viewport.w = MulDiv(this->GetInnerWidth(), zoom_n, zoom_d);
			viewport.x = (output_w - viewport.w) / 2;
		}
		else
		{
			// Top and bottom black bars (letterbox)
			zoom_n = output_w;
			zoom_d = this->GetInnerWidth();
			TVPDoReductionNumerAndDenom(zoom_n, zoom_d);
			viewport.x = 0;
			viewport.w = output_w;
			viewport.h = MulDiv(this->GetInnerHeight(), zoom_n, zoom_d);
			viewport.y = (output_h - viewport.h) / 2;
		}
	}
	else
	{
		if (scale_policy == 1)
		{
			// Crop top and bottom
			zoom_n = output_w;
			zoom_d = this->GetInnerWidth();
			TVPDoReductionNumerAndDenom(zoom_n, zoom_d);
			viewport.x = 0;
			viewport.w = output_w;
			viewport.h = MulDiv(this->GetInnerHeight(), zoom_n, zoom_d);
			viewport.y = (output_h - viewport.h) / 2;
		}
		else
		{
			// Left and right black bars (letterbox)
			zoom_n = output_h;
			zoom_d = this->GetInnerHeight();
			TVPDoReductionNumerAndDenom(zoom_n, zoom_d);
			viewport.y = 0;
			viewport.h = output_h;
			viewport.w = MulDiv(this->GetInnerWidth(), zoom_n, zoom_d);
			viewport.x = (output_w - viewport.w) / 2;
		}
	}
	this->FullScreenDestRect.set_size(viewport.w, viewport.h);
	this->FullScreenDestRect.set_offsets(viewport.x, viewport.y);

	this->ActualZoomNumer = zoom_n;
	this->ActualZoomDenom = zoom_d;
	this->SetDrawDeviceDestRect();
#endif
}

void TVPWindowWindow::SetDrawDeviceDestRect(void)
{
#ifdef KRKRSDL3_ENABLE_ZOOM
	tTVPRect destrect;
	tjs_int w = MulDiv(this->GetInnerWidth(),  this->ActualZoomNumer, this->ActualZoomDenom);
	tjs_int h = MulDiv(this->GetInnerHeight(), this->ActualZoomNumer, this->ActualZoomDenom);
	if (w < 1)
	{
		w = 1;
	}
	if (h < 1)
	{
		h = 1;
	}
	{
		destrect.left = this->FullScreenDestRect.left;
		destrect.top = this->FullScreenDestRect.top;
		destrect.right = destrect.left + w;
		destrect.bottom = destrect.top + h;
	}

	if (this->LastSentDrawDeviceDestRect != destrect)
	{
		this->LastSentDrawDeviceDestRect = destrect;
		this->UpdateWindow(utNormal);
	}
#endif
}

void TVPWindowWindow::SetZoom(tjs_int numer, tjs_int denom, bool set_logical)
{
#ifdef KRKRSDL3_ENABLE_ZOOM
	bool ischanged = false;
	// set layer zooming factor;
	// the zooming factor is passed in numerator/denoiminator style.
	// we must find GCM to optimize numer/denium via Euclidean algorithm.
	TVPDoReductionNumerAndDenom(numer, denom);
	if (set_logical)
	{
		if (this->ZoomNumer != numer || this->ZoomDenom != denom)
		{
			ischanged = true;
		}
		this->ZoomNumer = numer;
		this->ZoomDenom = denom;
	}
	this->UpdateActualZoom();
#endif
}

void TVPWindowWindow::SetZoomNumer(tjs_int n)
{
	this->SetZoom(n, this->GetZoomDenom());
}

tjs_int TVPWindowWindow::GetZoomNumer() const
{
#ifdef KRKRSDL3_ENABLE_ZOOM
	return this->ZoomNumer;
#else
	return 1;
#endif
}

void TVPWindowWindow::SetZoomDenom(tjs_int d)
{
	this->SetZoom(this->GetZoomNumer(), d);
}

tjs_int TVPWindowWindow::GetZoomDenom() const
{
#ifdef KRKRSDL3_ENABLE_ZOOM
	return this->ZoomDenom;
#else
	return 1;
#endif
}

void TVPWindowWindow::SetInnerWidth(tjs_int v)
{
#ifdef KRKRSDL3_ENABLE_ZOOM
	this->SetInnerSize(v, this->GetInnerHeight());
#else
	this->SetWidth(v);
#endif
}

void TVPWindowWindow::SetInnerHeight(tjs_int v)
{
#ifdef KRKRSDL3_ENABLE_ZOOM
	this->SetInnerSize(this->GetInnerWidth(), v);
#else
	this->SetHeight(v);
#endif
}

void TVPWindowWindow::SetInnerSize(tjs_int w, tjs_int h)
{
#ifdef KRKRSDL3_ENABLE_ZOOM
	this->InnerWidth = w;
	this->InnerHeight = h;
	this->UpdateActualZoom();
#endif
	this->SetSize(w, h);
}

tjs_int TVPWindowWindow::GetInnerWidth()
{
#ifdef KRKRSDL3_ENABLE_ZOOM
	return this->InnerWidth;
#else
	return this->GetWidth();
#endif
}

tjs_int TVPWindowWindow::GetInnerHeight()
{
#ifdef KRKRSDL3_ENABLE_ZOOM
	return this->InnerHeight;
#else
	return this->GetHeight();
#endif
}

void TVPWindowWindow::ZoomRectangle(tjs_int &left, tjs_int &top, tjs_int &right, tjs_int &bottom)
{
#ifdef KRKRSDL3_ENABLE_ZOOM
	left = MulDiv(left, this->ActualZoomNumer, this->ActualZoomDenom);
	top = MulDiv(top, this->ActualZoomNumer, this->ActualZoomDenom);
	right = MulDiv(right, this->ActualZoomNumer, this->ActualZoomDenom);
	bottom = MulDiv(bottom, this->ActualZoomNumer, this->ActualZoomDenom);
#else
	if(!this->renderer) return;
	float origin_x = 0.0f;
	float origin_y = 0.0f;
	float left_x = static_cast<float>(left);
	float top_y = static_cast<float>(top);
	float right_x = static_cast<float>(right);
	float bottom_y = static_cast<float>(bottom);
	float ignored = 0.0f;
	if(!SDL_RenderCoordinatesToWindow(this->renderer, 0.0f, 0.0f, &origin_x, &origin_y)) return;
	SDL_RenderCoordinatesToWindow(this->renderer, left_x, 0.0f, &left_x, &ignored);
	SDL_RenderCoordinatesToWindow(this->renderer, 0.0f, top_y, &ignored, &top_y);
	SDL_RenderCoordinatesToWindow(this->renderer, right_x, 0.0f, &right_x, &ignored);
	SDL_RenderCoordinatesToWindow(this->renderer, 0.0f, bottom_y, &ignored, &bottom_y);
	left = static_cast<tjs_int>(std::lround(left_x - origin_x));
	top = static_cast<tjs_int>(std::lround(top_y - origin_y));
	right = static_cast<tjs_int>(std::lround(right_x - origin_x));
	bottom = static_cast<tjs_int>(std::lround(bottom_y - origin_y));
#endif
}
void TVPWindowWindow::GetVideoOffset(tjs_int &ofsx, tjs_int &ofsy)
{
	ofsx = 0;
	ofsy = 0;
#ifdef KRKRSDL3_ENABLE_ZOOM
	ofsx = this->LastSentDrawDeviceDestRect.left;
	ofsy = this->LastSentDrawDeviceDestRect.top;
#else
	if(this->renderer)
	{
		float x = 0.0f;
		float y = 0.0f;
		if(SDL_RenderCoordinatesToWindow(this->renderer, 0.0f, 0.0f, &x, &y))
		{
			ofsx = static_cast<tjs_int>(std::lround(x));
			ofsy = static_cast<tjs_int>(std::lround(y));
		}
	}
#endif
}
void TVPWindowWindow::SyncWindowShadow()
{
	const bool fullscreen = this->window && (SDL_GetWindowFlags(this->window) & SDL_WINDOW_FULLSCREEN) != 0;
	TVPMacSetWindowShadow(this->window, !this->maskActive && !fullscreen);
}
void TVPWindowWindow::SetMaskRegion(tTVPBaseBitmap *bitmap, tjs_int threshold)
{
	if(!this->window || !bitmap) return;
	const int width = static_cast<int>(bitmap->GetWidth());
	const int height = static_cast<int>(bitmap->GetHeight());
	if(width <= 0 || height <= 0) return;

	SDL_Surface *shape = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_ARGB8888);
	if(!shape)
		TVPThrowExceptionMessage(TJS_W("Cannot create SDL window shape: %1"), ttstr(SDL_GetError()));
	if(!SDL_LockSurface(shape))
	{
		SDL_DestroySurface(shape);
		TVPThrowExceptionMessage(TJS_W("Cannot lock SDL window shape: %1"), ttstr(SDL_GetError()));
	}
	for(int y = 0; y < height; ++y)
	{
		const tjs_uint32 *source = static_cast<const tjs_uint32 *>(bitmap->GetScanLine(y));
		Uint32 *destination = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(shape->pixels) + y * shape->pitch);
		for(int x = 0; x < width; ++x)
			destination[x] = static_cast<tjs_uint>(source[x] >> 24) >= static_cast<tjs_uint>(threshold)
				? 0xffffffffU : 0x00ffffffU;
	}
	SDL_UnlockSurface(shape);
	if(!SDL_SetWindowShape(this->window, shape))
	{
		SDL_DestroySurface(shape);
		TVPThrowExceptionMessage(TJS_W("Cannot set SDL window shape: %1"), ttstr(SDL_GetError()));
	}
	if(this->maskSurface) SDL_DestroySurface(this->maskSurface);
	this->maskSurface = shape;
	this->maskActive = true;
	this->maskThreshold = threshold;
	this->SyncWindowShadow();
	if(this->bitmapCompletion && this->surface)
		this->bitmapCompletion->update_rect.do_union(tTVPRect(0, 0, this->surface->w, this->surface->h));
	this->needsGraphicUpdate = true;
}
void TVPWindowWindow::RemoveMaskRegion()
{
	if(this->window && !SDL_SetWindowShape(this->window, nullptr))
		TVPAddLog(ttstr("Cannot remove SDL window shape: ") + ttstr(SDL_GetError()));
	if(!this->renderer && this->surface && this->maskSurface)
	{
		for(int y = 0; y < this->surface->h; ++y)
		{
			for(int x = 0; x < this->surface->w; ++x)
			{
				Uint8 r, g, b, a;
				if(SDL_ReadSurfacePixel(this->surface, x, y, &r, &g, &b, &a))
					SDL_WriteSurfacePixel(this->surface, x, y, r, g, b, SDL_ALPHA_OPAQUE);
			}
		}
	}
	if(this->maskSurface)
	{
		SDL_DestroySurface(this->maskSurface);
		this->maskSurface = nullptr;
	}
	this->maskActive = false;
	this->SyncWindowShadow();
	if(this->bitmapCompletion && this->surface)
		this->bitmapCompletion->update_rect.do_union(tTVPRect(0, 0, this->surface->w, this->surface->h));
	this->needsGraphicUpdate = true;
}
void TVPWindowWindow::ApplySoftwareMask(const SDL_Rect &rect)
{
	if(this->renderer || !this->surface || !this->maskSurface) return;
	const int left = std::max(0, rect.x);
	const int top = std::max(0, rect.y);
	const int right = std::min(this->surface->w, rect.x + rect.w);
	const int bottom = std::min(this->surface->h, rect.y + rect.h);
	for(int y = top; y < bottom; ++y)
	{
		const int mask_y = std::min(this->maskSurface->h - 1, y * this->maskSurface->h / std::max(1, this->surface->h));
		for(int x = left; x < right; ++x)
		{
			const int mask_x = std::min(this->maskSurface->w - 1, x * this->maskSurface->w / std::max(1, this->surface->w));
			Uint8 r, g, b, a, mask_alpha;
			if(SDL_ReadSurfacePixel(this->surface, x, y, &r, &g, &b, &a) &&
				SDL_ReadSurfacePixel(this->maskSurface, mask_x, mask_y, nullptr, nullptr, nullptr, &mask_alpha))
				SDL_WriteSurfacePixel(this->surface, x, y, r, g, b, mask_alpha);
		}
	}
}
void TVPWindowWindow::SetWaitVSync(bool enabled)
{
	if(this->renderer && !SDL_SetRenderVSync(this->renderer, enabled ? 1 : 0))
		TVPAddLog(ttstr("Cannot change SDL renderer VSync: ") + ttstr(SDL_GetError()));
#ifdef KRKRZ_ENABLE_CANVAS
	if(this->context && !SDL_GL_SetSwapInterval(enabled ? 1 : 0))
		TVPAddLog(ttstr("Cannot change SDL OpenGL swap interval: ") + ttstr(SDL_GetError()));
#endif
}
void TVPWindowWindow::ResetDrawDevice()
{
	this->needsGraphicUpdate = false;
	this->hasDrawn = false;
	if(this->bitmapCompletion)
	{
		this->bitmapCompletion->surface = nullptr;
		this->bitmapCompletion->update_rect.clear();
	}
	if(this->texture)
	{
		SDL_DestroyTexture(this->texture);
		this->texture = nullptr;
	}
	if(this->renderer && this->surface)
	{
		SDL_DestroySurface(this->surface);
		this->surface = nullptr;
	}
	if(!this->renderer && this->window)
	{
		this->surface = SDL_GetWindowSurface(this->window);
		if(this->bitmapCompletion) this->bitmapCompletion->surface = this->surface;
	}

	if(!this->TJSNativeInstance || !this->TJSNativeInstance->GetDrawDevice()) return;
	tjs_int width = 0;
	tjs_int height = 0;
	this->TJSNativeInstance->GetDrawDevice()->GetSrcSize(width, height);
	if(width <= 0 || height <= 0) return;
	this->SetPaintBoxSize(width, height);
	if(this->maskActive)
	{
		tTJSNI_BaseLayer *layer = this->TJSNativeInstance->GetDrawDevice()->GetPrimaryLayer();
		if(layer && layer->GetMainImage()) this->SetMaskRegion(layer->GetMainImage(), this->maskThreshold);
		else this->RemoveMaskRegion();
	}
	this->TJSNativeInstance->NotifyWindowExposureToLayer(tTVPRect(0, 0, width, height));
}

void TVPWindowWindow::SetUseMouseKey(bool b)
{
	if(this->useMouseKey == b) return;
	this->useMouseKey = b;
	this->mouseKeyXAccel = 0;
	this->mouseKeyYAccel = 0;
	this->gamepadMouseLeft = this->gamepadMouseRight = false;
	this->gamepadMouseUp = this->gamepadMouseDown = false;
	this->lastMouseKeyTick = SDL_GetTicks();
	if(!b)
	{
		if(this->emulatedLeftButtonDown) this->PostEmulatedMouseButton(mbLeft, false, false);
		if(this->emulatedRightButtonDown) this->PostEmulatedMouseButton(mbRight, false, false);
	}
}
bool TVPWindowWindow::GetUseMouseKey() const
{
	return this->useMouseKey;
}
void TVPWindowWindow::ReleaseMouseCapture()
{
	if(!SDL_CaptureMouse(false))
		TVPAddLog(ttstr("Cannot release SDL mouse capture: ") + ttstr(SDL_GetError()));
}
void TVPWindowWindow::ResetMouseVelocity()
{
	this->mouseVelocityTracker.clear();
}
void TVPWindowWindow::ResetTouchVelocity(tjs_int id)
{
	this->touchVelocityTrackers.end(id);
}
bool TVPWindowWindow::GetMouseVelocity(float &x, float &y, float &speed) const
{
	if(this->mouseVelocityTracker.getVelocity(x, y))
	{
		speed = std::hypot(x, y);
		return true;
	}
	x = y = speed = 0.0f;
	return false;
}
bool TVPWindowWindow::GetTouchVelocity(tjs_int id, float &x, float &y, float &speed) const
{
	if(this->touchVelocityTrackers.getVelocity(id, x, y, speed)) return true;
	x = y = speed = 0.0f;
	return false;
}
void TVPWindowWindow::SetTouchScaleThreshold(double threshold)
{
	this->touchPoints.SetScaleThreshold(threshold);
}
double TVPWindowWindow::GetTouchScaleThreshold() const
{
	return this->touchPoints.GetScaleThreshold();
}
void TVPWindowWindow::SetTouchRotateThreshold(double threshold)
{
	this->touchPoints.SetRotateThreshold(threshold);
}
double TVPWindowWindow::GetTouchRotateThreshold() const
{
	return this->touchPoints.GetRotateThreshold();
}
tjs_real TVPWindowWindow::GetTouchPointStartX(tjs_int index) const
{
	return this->touchPoints.GetStartX(index);
}
tjs_real TVPWindowWindow::GetTouchPointStartY(tjs_int index) const
{
	return this->touchPoints.GetStartY(index);
}
tjs_real TVPWindowWindow::GetTouchPointX(tjs_int index) const
{
	return this->touchPoints.GetX(index);
}
tjs_real TVPWindowWindow::GetTouchPointY(tjs_int index) const
{
	return this->touchPoints.GetY(index);
}
tjs_int TVPWindowWindow::GetTouchPointID(tjs_int index) const
{
	return static_cast<tjs_int>(this->touchPoints.GetID(index));
}
tjs_int TVPWindowWindow::GetTouchPointCount() const
{
	return this->touchPoints.CountUsePoint();
}
void TVPWindowWindow::SetEnableTouch(bool b)
{
	if(this->enableTouch == b) return;
	if(!b) this->CancelActiveTouches(true);
	this->enableTouch = b;
}
bool TVPWindowWindow::GetEnableTouch() const
{
	return this->enableTouch;
}
tjs_uint32 TVPWindowWindow::AllocateTouchID()
{
	for(tjs_uint32 attempts = 0; attempts < static_cast<tjs_uint32>(std::numeric_limits<tjs_int>::max()); ++attempts)
	{
		if(this->nextTouchID == 0 || this->nextTouchID > static_cast<tjs_uint32>(std::numeric_limits<tjs_int>::max()))
			this->nextTouchID = 1;
		const tjs_uint32 candidate = this->nextTouchID++;
		bool used = false;
		for(const auto &entry : this->activeTouches)
		{
			if(entry.second.id == candidate)
			{
				used = true;
				break;
			}
		}
		if(!used) return candidate;
	}
	return 1;
}
void TVPWindowWindow::HandleTouchEvent(const SDL_TouchFingerEvent &event)
{
	if(!this->enableTouch || !this->window) return;

	int window_width = 0;
	int window_height = 0;
	SDL_GetWindowSize(this->window, &window_width, &window_height);
	tjs_int x = static_cast<tjs_int>(std::lround(event.x * window_width));
	tjs_int y = static_cast<tjs_int>(std::lround(event.y * window_height));
	this->TranslateWindowToDrawArea(x, y);
	const double cx = 1.0;
	const double cy = 1.0;
	const tjs_uint32 tick = static_cast<tjs_uint32>(event.timestamp / 1000000ULL);
	const TouchKey key(event.touchID, event.fingerID);
	auto found = this->activeTouches.find(key);

	if(event.type == SDL_EVENT_FINGER_DOWN)
	{
		if(found != this->activeTouches.end()) return;
		ActiveTouch touch = { this->AllocateTouchID(), static_cast<double>(x), static_cast<double>(y), cx, cy, tick };
		this->activeTouches.emplace(key, touch);
		this->touchVelocityTrackers.start(static_cast<tjs_int>(touch.id));
		this->touchVelocityTrackers.update(static_cast<tjs_int>(touch.id), tick, static_cast<float>(x), static_cast<float>(y));
		if(this->TJSNativeInstance)
			TVPPostInputEvent(new tTVPOnTouchDownInputEvent(this->TJSNativeInstance, x, y, cx, cy, touch.id));
		this->touchPoints.TouchDown(x, y, cx, cy, touch.id, tick);
		return;
	}
	if(found == this->activeTouches.end()) return;

	ActiveTouch &touch = found->second;
	touch.x = x;
	touch.y = y;
	touch.tick = tick;
	this->touchVelocityTrackers.update(static_cast<tjs_int>(touch.id), tick, static_cast<float>(x), static_cast<float>(y));
	if(event.type == SDL_EVENT_FINGER_MOTION)
	{
		if(this->TJSNativeInstance)
			TVPPostInputEvent(new tTVPOnTouchMoveInputEvent(this->TJSNativeInstance, x, y, cx, cy, touch.id));
		this->touchPoints.TouchMove(x, y, cx, cy, touch.id, tick);
	}
	else if(event.type == SDL_EVENT_FINGER_UP || event.type == SDL_EVENT_FINGER_CANCELED)
	{
		const tjs_uint32 id = touch.id;
		if(this->TJSNativeInstance)
			TVPPostInputEvent(new tTVPOnTouchUpInputEvent(this->TJSNativeInstance, x, y, cx, cy, id));
		this->touchPoints.TouchUp(x, y, cx, cy, id, tick);
		this->activeTouches.erase(found);
	}
}
void TVPWindowWindow::CancelActiveTouches(bool postEvents)
{
	const std::map<TouchKey, ActiveTouch> touches = this->activeTouches;
	for(const auto &entry : touches)
	{
		const ActiveTouch &touch = entry.second;
		if(postEvents && this->TJSNativeInstance)
			TVPPostInputEvent(new tTVPOnTouchUpInputEvent(this->TJSNativeInstance,
				touch.x, touch.y, touch.cx, touch.cy, touch.id));
		this->touchPoints.TouchUp(touch.x, touch.y, touch.cx, touch.cy, touch.id, touch.tick);
		if(!postEvents) this->touchVelocityTrackers.end(static_cast<tjs_int>(touch.id));
	}
	this->activeTouches.clear();
}
void TVPWindowWindow::OnTouchScaling(double startdist, double currentdist, double cx, double cy, int flag)
{
	if(this->TJSNativeInstance)
		TVPPostInputEvent(new tTVPOnTouchScalingInputEvent(this->TJSNativeInstance,
			startdist, currentdist, cx, cy, flag));
}
void TVPWindowWindow::OnTouchRotate(double startangle, double currentangle, double distance, double cx, double cy, int flag)
{
	if(this->TJSNativeInstance)
		TVPPostInputEvent(new tTVPOnTouchRotateInputEvent(this->TJSNativeInstance,
			startangle, currentangle, distance, cx, cy, flag));
}
void TVPWindowWindow::OnMultiTouch()
{
	if(this->TJSNativeInstance)
		TVPPostInputEvent(new tTVPOnMultiTouchInputEvent(this->TJSNativeInstance));
}
void TVPWindowWindow::PostEmulatedMouseButton(tTVPMouseButton button, bool down, bool click)
{
	tjs_int x = 0;
	tjs_int y = 0;
	this->GetCursorPos(x, y);
	if(down && (x < 0 || y < 0 || x >= this->GetInnerWidth() || y >= this->GetInnerHeight())) return;
	const tjs_uint32 state = TVP_TShiftState_To_uint32(GetShiftState()) | GetMouseButtonState();
	this->mouseVelocityTracker.addMovement(SDL_GetTicks(), static_cast<float>(x), static_cast<float>(y));
	if(button == mbLeft) this->emulatedLeftButtonDown = down;
	if(button == mbRight) this->emulatedRightButtonDown = down;
	if(!this->TJSNativeInstance) return;
	if(down)
	{
		TVPPostInputEvent(new tTVPOnMouseDownInputEvent(this->TJSNativeInstance, x, y, button, state));
	}
	else
	{
		if(click && button == mbLeft)
			TVPPostInputEvent(new tTVPOnClickInputEvent(this->TJSNativeInstance, x, y));
		TVPPostInputEvent(new tTVPOnMouseUpInputEvent(this->TJSNativeInstance, x, y, button, state));
	}
}
void TVPWindowWindow::GenerateMouseKeyMovement(bool forceLeft, bool forceRight, bool forceUp, bool forceDown)
{
	if(!this->useMouseKey || !this->window) return;
	const Uint64 now = SDL_GetTicks();
	if(!forceLeft && !forceRight && !forceUp && !forceDown && now < this->lastMouseKeyTick + 45) return;

	const bool *keys = SDL_GetKeyboardState(nullptr);
	const bool left = forceLeft || this->gamepadMouseLeft || keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_KP_4];
	const bool right = forceRight || this->gamepadMouseRight || keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_KP_6];
	const bool up = forceUp || this->gamepadMouseUp || keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_KP_8];
	const bool down = forceDown || this->gamepadMouseDown || keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_KP_2];
	const bool shift = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
	if(!left && !right && !up && !down)
	{
		this->mouseKeyXAccel = 0;
		this->mouseKeyYAccel = 0;
		this->lastMouseKeyTick = now;
		return;
	}
	if(left && !right && this->mouseKeyXAccel > 0) this->mouseKeyXAccel = 0;
	if(right && !left && this->mouseKeyXAccel < 0) this->mouseKeyXAccel = 0;
	if(up && !down && this->mouseKeyYAccel > 0) this->mouseKeyYAccel = 0;
	if(down && !up && this->mouseKeyYAccel < 0) this->mouseKeyYAccel = 0;

	if(shift)
	{
		if(left) this->mouseKeyXAccel = -40;
		if(right) this->mouseKeyXAccel = 40;
		if(up) this->mouseKeyYAccel = -40;
		if(down) this->mouseKeyYAccel = 40;
	}
	else
	{
		if(left) this->mouseKeyXAccel = std::max(-30, this->mouseKeyXAccel ? this->mouseKeyXAccel - 2 : -2);
		else if(right) this->mouseKeyXAccel = std::min(30, this->mouseKeyXAccel ? this->mouseKeyXAccel + 2 : 2);
		else this->mouseKeyXAccel += this->mouseKeyXAccel > 0 ? -1 : (this->mouseKeyXAccel < 0 ? 1 : 0);
		if(up) this->mouseKeyYAccel = std::max(-30, this->mouseKeyYAccel ? this->mouseKeyYAccel - 2 : -2);
		else if(down) this->mouseKeyYAccel = std::min(30, this->mouseKeyYAccel ? this->mouseKeyYAccel + 2 : 2);
		else this->mouseKeyYAccel += this->mouseKeyYAccel > 0 ? -1 : (this->mouseKeyYAccel < 0 ? 1 : 0);
	}

	float x = 0.0f;
	float y = 0.0f;
	SDL_GetMouseState(&x, &y);
	SDL_WarpMouseInWindow(this->window, x + (this->mouseKeyXAccel >> 1), y + (this->mouseKeyYAccel >> 1));
	this->RestoreMouseCursor();
	this->lastMouseKeyTick = now;
}
bool TVPWindowWindow::HandleMouseKey(const SDL_KeyboardEvent &event, bool down)
{
	if(!this->useMouseKey) return false;
	const SDL_Keycode key = event.key;
	if(key == SDLK_LEFT || key == SDLK_KP_4 || key == SDLK_RIGHT || key == SDLK_KP_6 ||
		key == SDLK_UP || key == SDLK_KP_8 || key == SDLK_DOWN || key == SDLK_KP_2)
	{
		if(down && this->mouseKeyXAccel == 0 && this->mouseKeyYAccel == 0)
		{
			this->GenerateMouseKeyMovement(key == SDLK_LEFT || key == SDLK_KP_4,
				key == SDLK_RIGHT || key == SDLK_KP_6, key == SDLK_UP || key == SDLK_KP_8,
				key == SDLK_DOWN || key == SDLK_KP_2);
			this->lastMouseKeyTick = SDL_GetTicks() + 100;
		}
		return true;
	}
	if(key == SDLK_RETURN || key == SDLK_SPACE || key == SDLK_KP_1)
	{
		if(down != this->emulatedLeftButtonDown) this->PostEmulatedMouseButton(mbLeft, down);
		return true;
	}
	if(key == SDLK_ESCAPE)
	{
		if(down != this->emulatedRightButtonDown) this->PostEmulatedMouseButton(mbRight, down);
		return true;
	}
	return false;
}

bool TVPWindowWindow::should_try_parent_window(SDL_Event event)
{
	if (!this->window || !this->_prevWindow)
	{
		return false;
	}
	SDL_Window *event_window = SDL_GetWindowFromEvent(&event);
	return event_window && event_window != this->window;
}

void TVPWindowWindow::window_receive_event(SDL_Event event)
{
	if (this->isBeingDeleted)
	{
		delete this;
		return;
	}
	if (this->should_try_parent_window(event))
	{
		if (!this->in_mode_)
		{
			this->_prevWindow->window_receive_event(event);
		}
		return;
	}
	if (this->window && this->hasDrawn)
	{
		tjs_uint32 s = TVP_TShiftState_To_uint32(GetShiftState());
		s |= GetMouseButtonState();
		if (this->TJSNativeInstance->CanDeliverEvents())
		{
			switch (event.type)
			{
				case SDL_EVENT_TEXT_INPUT:
				case SDL_EVENT_TEXT_EDITING:
				{
					if (!SDL_TextInputActive(this->window))
					{
						return;
					}
					// TODO: figure out vertical edit
					const size_t previous_cursor = std::min(this->imeCompositionCursor, this->imeCompositionLen);
					const size_t previous_selection = std::min(this->imeCompositionSelection, previous_cursor);
					for (size_t i = 0; i < previous_selection; i += 1)
					{
						TVPPostInputEvent(new tTVPOnKeyDownInputEvent(this->TJSNativeInstance, VK_LEFT, TVP_SS_SHIFT));
						TVPPostInputEvent(new tTVPOnKeyUpInputEvent(this->TJSNativeInstance, VK_LEFT, TVP_SS_SHIFT));
					}
					for (size_t i = 0; i < this->imeCompositionLen - previous_cursor; i += 1)
					{
						TVPPostInputEvent(new tTVPOnKeyDownInputEvent(this->TJSNativeInstance, VK_RIGHT, 0));
						TVPPostInputEvent(new tTVPOnKeyUpInputEvent(this->TJSNativeInstance, VK_RIGHT, 0));
					}
					for (size_t i = 0; i < this->imeCompositionLen; i += 1)
					{
						TVPPostInputEvent(new tTVPOnKeyDownInputEvent(this->TJSNativeInstance, VK_BACK, 0));
						TVPPostInputEvent(new tTVPOnKeyUpInputEvent(this->TJSNativeInstance, VK_BACK, 0));
					}
					const char *composition_text = nullptr;
					if (event.type == SDL_EVENT_TEXT_INPUT)
					{
						composition_text = event.text.text;
						this->imeCompositionCursor = 0;
						this->imeCompositionSelection = 0;
					}
					else
					{
						composition_text = event.edit.text;
						this->imeCompositionCursor = event.edit.start > 0 ? static_cast<size_t>(event.edit.start) : 0;
						this->imeCompositionSelection = event.edit.length > 0 ? static_cast<size_t>(event.edit.length) : 0;
					}
					if (this->imeCompositionStr)
					{
						SDL_free(this->imeCompositionStr);
						this->imeCompositionStr = nullptr;
					}
					this->imeCompositionStr = SDL_strdup(composition_text ? composition_text : "");
					if (!this->imeCompositionStr)
					{
						this->imeCompositionLen = 0;
						this->imeCompositionCursor = 0;
						this->imeCompositionSelection = 0;
						return;
					}
					size_t buffer_len = TVPUtf8ToWideCharString(this->imeCompositionStr, nullptr);
					if (buffer_len == (size_t)-1)
					{
						SDL_free(this->imeCompositionStr);
						this->imeCompositionStr = nullptr;
						this->imeCompositionLen = 0;
						return;
					}
					this->imeCompositionLen = buffer_len;
					this->imeCompositionCursor = std::min(this->imeCompositionCursor, this->imeCompositionLen);
					this->imeCompositionSelection = std::min(this->imeCompositionSelection, this->imeCompositionLen - this->imeCompositionCursor);
					if (buffer_len)
					{
						tjs_char *buffer = new tjs_char[buffer_len + 1];
						TVPUtf8ToWideCharString(this->imeCompositionStr, buffer);
						for (size_t i = 0; i < buffer_len; i += 1)
						{
							TVPPostInputEvent(new tTVPOnKeyPressInputEvent(this->TJSNativeInstance, buffer[i]));
						}
						delete[] buffer;
					}
					else
					{
						SDL_free(this->imeCompositionStr);
						this->imeCompositionStr = nullptr;
						this->imeCompositionLen = 0;
						this->imeCompositionCursor = 0;
						this->imeCompositionSelection = 0;
					}
					if (event.type == SDL_EVENT_TEXT_EDITING)
					{
						for (size_t i = 0; i < this->imeCompositionLen - this->imeCompositionCursor; i += 1)
						{
							TVPPostInputEvent(new tTVPOnKeyDownInputEvent(this->TJSNativeInstance, VK_LEFT, 0));
							TVPPostInputEvent(new tTVPOnKeyUpInputEvent(this->TJSNativeInstance, VK_LEFT, 0));
						}
						for (size_t i = 0; i < this->imeCompositionSelection; i += 1)
						{
							TVPPostInputEvent(new tTVPOnKeyDownInputEvent(this->TJSNativeInstance, VK_RIGHT, TVP_SS_SHIFT));
							TVPPostInputEvent(new tTVPOnKeyUpInputEvent(this->TJSNativeInstance, VK_RIGHT, TVP_SS_SHIFT));
						}
					}
					if (event.type == SDL_EVENT_TEXT_INPUT)
					{
						if (this->imeCompositionStr)
						{
							SDL_free(this->imeCompositionStr);
						}
						this->imeCompositionStr = nullptr;
						this->imeCompositionLen = 0;
						this->imeCompositionCursor = 0;
						this->imeCompositionSelection = 0;
					}
					return;
				}
				case SDL_EVENT_DROP_BEGIN:
				{
					if (!this->fileDropArray)
					{
						this->fileDropArray = TJSCreateArrayObject();
					}
					return;
				}
				case SDL_EVENT_DROP_COMPLETE:
				{
					if (this->fileDropArray)
					{
						tTJSVariant arg(this->fileDropArray, this->fileDropArray);
						TVPPostInputEvent(new tTVPOnFileDropInputEvent(this->TJSNativeInstance, arg));
						this->fileDropArray->Release();
						this->fileDropArray = nullptr;
						this->fileDropArrayCount = 0;
					}
					return;
				}
				case SDL_EVENT_DROP_FILE:
				case SDL_EVENT_DROP_TEXT:
				{
					if (event.drop.data)
					{
						std::string f_utf8 = event.drop.data;
						tjs_string f_utf16;
						TVPUtf8ToUtf16(f_utf16, f_utf8);
						if (TVPIsExistentStorageNoSearch(f_utf16))
						{
							tTJSVariant val = TVPNormalizeStorageName(ttstr(f_utf16));
							if (this->fileDropArray)
							{
								this->fileDropArray->PropSetByNum(TJS_MEMBERENSURE|TJS_IGNOREPROP, this->fileDropArrayCount, &val, this->fileDropArray);
								this->fileDropArrayCount += 1;
							}
							else
							{
								iTJSDispatch2 *file_drop_array_single = TJSCreateArrayObject();
								file_drop_array_single->PropSetByNum(TJS_MEMBERENSURE|TJS_IGNOREPROP, 0, &val, file_drop_array_single);
								{
									tTJSVariant arg(file_drop_array_single, file_drop_array_single);
									TVPPostInputEvent(new tTVPOnFileDropInputEvent(this->TJSNativeInstance, arg));
								}
								file_drop_array_single->Release();
							}
						}
					}
					return;
				}
				case SDL_EVENT_GAMEPAD_ADDED:
				case SDL_EVENT_GAMEPAD_REMOVED:
				case SDL_EVENT_GAMEPAD_REMAPPED:
				{
					refresh_gamepads();
					return;
				}
				case SDL_EVENT_WINDOW_EXPOSED:
				case SDL_EVENT_WINDOW_MINIMIZED:
				case SDL_EVENT_WINDOW_MAXIMIZED:
				case SDL_EVENT_WINDOW_RESTORED:
				case SDL_EVENT_WINDOW_RESIZED:
				case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
				case SDL_EVENT_WINDOW_MOUSE_ENTER:
				case SDL_EVENT_WINDOW_MOUSE_LEAVE:
				case SDL_EVENT_WINDOW_FOCUS_GAINED:
				case SDL_EVENT_WINDOW_FOCUS_LOST:
				case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
				case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
				case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
				{
					switch (event.type)
					{
						case SDL_EVENT_WINDOW_EXPOSED:
						{
							this->UpdateWindow(utNormal);
							return;
						}
						case SDL_EVENT_WINDOW_MINIMIZED:
						case SDL_EVENT_WINDOW_MAXIMIZED:
						case SDL_EVENT_WINDOW_RESTORED:
						case SDL_EVENT_WINDOW_RESIZED:
						case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
						{
#ifdef KRKRSDL3_ENABLE_ZOOM
							this->UpdateActualZoom();
#else
							this->UpdateWindow(utNormal);
#endif
							TVPPostInputEvent(new tTVPOnResizeInputEvent(this->TJSNativeInstance), TVP_EPT_REMOVE_POST);
							return;
						}
						case SDL_EVENT_WINDOW_MOUSE_ENTER:
						{
							TVPPostInputEvent(new tTVPOnMouseEnterInputEvent(this->TJSNativeInstance));
							return;
						}
						case SDL_EVENT_WINDOW_MOUSE_LEAVE:
						{
							TVPPostInputEvent(new tTVPOnMouseOutOfWindowInputEvent(this->TJSNativeInstance));
							TVPPostInputEvent(new tTVPOnMouseLeaveInputEvent(this->TJSNativeInstance));
							return;
						}
						case SDL_EVENT_WINDOW_FOCUS_GAINED:
						case SDL_EVENT_WINDOW_FOCUS_LOST:
						{
							if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
							{
								this->CancelActiveTouches(true);
								if(this->emulatedLeftButtonDown) this->PostEmulatedMouseButton(mbLeft, false, false);
								if(this->emulatedRightButtonDown) this->PostEmulatedMouseButton(mbRight, false, false);
								this->gamepadMouseLeft = this->gamepadMouseRight = false;
								this->gamepadMouseUp = this->gamepadMouseDown = false;
								this->mouseKeyXAccel = this->mouseKeyYAccel = 0;
								this->ReleaseMouseCapture();
								TVPPostInputEvent(new tTVPOnReleaseCaptureInputEvent(this->TJSNativeInstance));
							}
							TVPPostInputEvent(new tTVPOnWindowActivateEvent(this->TJSNativeInstance, event.type == SDL_EVENT_WINDOW_FOCUS_GAINED), TVP_EPT_REMOVE_POST);
							return;
						}
						case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
						case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
						{
							this->SyncWindowShadow();
							return;
						}
						case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
						{
							TVPPostInputEvent(new tTVPOnCloseInputEvent(this->TJSNativeInstance));
							return;
						}
						default:
						{
							return;
						}
					}
				}
				case SDL_EVENT_QUIT:
				{
					TVPPostInputEvent(new tTVPOnCloseInputEvent(this->TJSNativeInstance));
					return;
				}
				default:
				{
					this->window_receive_event_input(event);
					return;
				}
			}
		}
	}
}

bool TVPWindowWindow::window_receive_event_input(SDL_Event event)
{
	if (this->isBeingDeleted)
	{
		delete this;
		return false;
	}
	if (this->should_try_parent_window(event))
	{
		if (!this->in_mode_)
		{
			return this->_prevWindow->window_receive_event_input(event);
		}
		return false;
	}
	if (this->window && this->hasDrawn)
	{
		tjs_uint32 s = TVP_TShiftState_To_uint32(GetShiftState());
		s |= GetMouseButtonState();
		if (this->TJSNativeInstance->CanDeliverEvents())
		{
			switch (event.type)
			{
				case SDL_EVENT_MOUSE_MOTION:
				{
					this->RestoreMouseCursor();
					this->lastMouseX = static_cast<int>(event.motion.x);
					this->lastMouseY = static_cast<int>(event.motion.y);
					this->TranslateWindowToDrawArea(this->lastMouseX, this->lastMouseY);
					this->mouseVelocityTracker.addMovement(event.motion.timestamp / 1000000ULL,
						static_cast<float>(this->lastMouseX), static_cast<float>(this->lastMouseY));
					TVPPostInputEvent(new tTVPOnMouseMoveInputEvent(this->TJSNativeInstance, this->lastMouseX, this->lastMouseY, s));
					return true;
				}
				case SDL_EVENT_MOUSE_BUTTON_DOWN:
				case SDL_EVENT_MOUSE_BUTTON_UP:
				{
					if (SDL_TextInputActive(this->window) && this->imeCompositionStr)
					{
						return false;
					}
					tTVPMouseButton btn;
					bool hasbtn = true;
					switch (event.button.button)
					{
						case SDL_BUTTON_RIGHT:
							btn = tTVPMouseButton::mbRight;
							break;
						case SDL_BUTTON_MIDDLE:
							btn = tTVPMouseButton::mbMiddle;
							break;
						case SDL_BUTTON_LEFT:
							btn = tTVPMouseButton::mbLeft;
							break;
						case SDL_BUTTON_X1:
							btn = tTVPMouseButton::mbX1;
							break;
						case SDL_BUTTON_X2:
							btn = tTVPMouseButton::mbX2;
							break;
						default:
							hasbtn = false;
							break;
					}
					if (hasbtn)
					{
						this->lastMouseX = static_cast<int>(event.button.x);
						this->lastMouseY = static_cast<int>(event.button.y);
						this->TranslateWindowToDrawArea(this->lastMouseX, this->lastMouseY);
						this->mouseVelocityTracker.addMovement(event.button.timestamp / 1000000ULL,
							static_cast<float>(this->lastMouseX), static_cast<float>(this->lastMouseY));
						TVPPostInputEvent(new tTVPOnMouseMoveInputEvent(this->TJSNativeInstance, this->lastMouseX, this->lastMouseY, s));
						switch (event.type)
						{
							case SDL_EVENT_MOUSE_BUTTON_DOWN:
								if(!SDL_CaptureMouse(true))
									TVPAddLog(ttstr("Cannot enable SDL mouse capture: ") + ttstr(SDL_GetError()));
								TVPPostInputEvent(new tTVPOnMouseDownInputEvent(this->TJSNativeInstance, this->lastMouseX, this->lastMouseY, btn, s));
								break;
							case SDL_EVENT_MOUSE_BUTTON_UP:
								if (event.button.clicks >= 2)
								{
									TVPPostInputEvent(new tTVPOnDoubleClickInputEvent(this->TJSNativeInstance, this->lastMouseX, this->lastMouseY));
								}
								else
								{
									TVPPostInputEvent(new tTVPOnClickInputEvent(this->TJSNativeInstance, this->lastMouseX, this->lastMouseY));
								}
								TVPPostInputEvent(new tTVPOnMouseUpInputEvent(this->TJSNativeInstance, this->lastMouseX, this->lastMouseY, btn, s));
								{
									float unused_x = 0.0f;
									float unused_y = 0.0f;
									if(SDL_GetMouseState(&unused_x, &unused_y) == 0) this->ReleaseMouseCapture();
								}
								break;
						}
						return true;
					}
					return false;
				}
				case SDL_EVENT_MOUSE_WHEEL:
				{
					TVPPostInputEvent(new tTVPOnMouseWheelInputEvent(this->TJSNativeInstance, static_cast<int>(event.wheel.x), static_cast<int>(event.wheel.y), this->lastMouseX, this->lastMouseY));
					return true;
				}
				case SDL_EVENT_FINGER_MOTION:
				case SDL_EVENT_FINGER_DOWN:
				case SDL_EVENT_FINGER_UP:
				case SDL_EVENT_FINGER_CANCELED:
				{
					this->HandleTouchEvent(event.tfinger);
					return true;
				}
				case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
				case SDL_EVENT_GAMEPAD_BUTTON_UP:
				{
					const SDL_GamepadButton button = static_cast<SDL_GamepadButton>(event.gbutton.button);
					const tjs_uint key = sdl_gamepadbutton_to_vk_key(button);
					if (!key)
					{
						return false;
					}
					if(this->useMouseKey)
					{
						const bool down = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
						switch(key)
						{
							case VK_PADLEFT: this->gamepadMouseLeft = down; break;
							case VK_PADRIGHT: this->gamepadMouseRight = down; break;
							case VK_PADUP: this->gamepadMouseUp = down; break;
							case VK_PADDOWN: this->gamepadMouseDown = down; break;
							case VK_PAD1:
								if(down != this->emulatedLeftButtonDown) this->PostEmulatedMouseButton(mbLeft, down);
								return true;
							case VK_PAD2:
								if(down != this->emulatedRightButtonDown) this->PostEmulatedMouseButton(mbRight, down);
								return true;
							default: break;
						}
						if(key == VK_PADLEFT || key == VK_PADRIGHT || key == VK_PADUP || key == VK_PADDOWN)
						{
							if(down) this->GenerateMouseKeyMovement(key == VK_PADLEFT, key == VK_PADRIGHT,
								key == VK_PADUP, key == VK_PADDOWN);
							return true;
						}
					}
					if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
					{
						TVPPostInputEvent(new tTVPOnKeyDownInputEvent(this->TJSNativeInstance, key, s));
					}
					else
					{
						if (!SDL_TextInputActive(this->window))
						{
							TVPPostInputEvent(new tTVPOnKeyPressInputEvent(this->TJSNativeInstance, key));
						}
						TVPPostInputEvent(new tTVPOnKeyUpInputEvent(this->TJSNativeInstance, key, s));
					}
					return true;
				}
				case SDL_EVENT_KEY_DOWN:
				{
					if(this->HandleMouseKey(event.key, true)) return true;
					if (SDL_TextInputActive(this->window))
					{
						if (this->imeCompositionStr)
						{
							return false;
						}
					}
					if (event.key.repeat)
					{
						s |= TVP_SS_REPEAT;
					}
					tjs_uint unified_vk_key = 0;
					switch (event.key.key)
					{
						case SDLK_LSHIFT:
						case SDLK_RSHIFT:
							unified_vk_key = VK_SHIFT;
							break;
						case SDLK_LCTRL:
						case SDLK_RCTRL:
							unified_vk_key = VK_CONTROL;
							break;
						case SDLK_LALT:
						case SDLK_RALT:
							unified_vk_key = VK_MENU;
							break;
					}
					TVPPostInputEvent(new tTVPOnKeyDownInputEvent(this->TJSNativeInstance, sdl_key_to_vk_key(event.key.key), s));
					if (unified_vk_key)
					{
						TVPPostInputEvent(new tTVPOnKeyDownInputEvent(this->TJSNativeInstance, unified_vk_key, s));
					}
					this->ApplyTextInputArea();
					return true;
				}
				case SDL_EVENT_KEY_UP:
				{
					if(this->HandleMouseKey(event.key, false)) return true;
					if (SDL_TextInputActive(this->window))
					{
						if (this->imeCompositionStr)
						{
							return false;
						}
					}
					tjs_uint unified_vk_key = 0;
					switch (event.key.key)
					{
						case SDLK_LSHIFT:
						case SDLK_RSHIFT:
							unified_vk_key = VK_SHIFT;
							break;
						case SDLK_LCTRL:
						case SDLK_RCTRL:
							unified_vk_key = VK_CONTROL;
							break;
						case SDLK_LALT:
						case SDLK_RALT:
							unified_vk_key = VK_MENU;
							break;
					}
					if (!SDL_TextInputActive(this->window))
					{
						TVPPostInputEvent(new tTVPOnKeyPressInputEvent(this->TJSNativeInstance, sdl_key_to_vk_key(event.key.key)));
						if (unified_vk_key)
						{
							TVPPostInputEvent(new tTVPOnKeyPressInputEvent(this->TJSNativeInstance, unified_vk_key));
						}
					}
					TVPPostInputEvent(new tTVPOnKeyUpInputEvent(this->TJSNativeInstance, sdl_key_to_vk_key(event.key.key), s));
					if (unified_vk_key)
					{
						TVPPostInputEvent(new tTVPOnKeyUpInputEvent(this->TJSNativeInstance, unified_vk_key, s));
					}
					this->ApplyTextInputArea();
					return true;
				}
				default:
				{
					return false;
				}
			}
		}
	}
	return false;
}

void sdl_process_events()
{
	if (!SDL_WasInit(SDL_INIT_EVENTS))
	{
		return;
	}
	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
		if (event.type == NativeEventQueueImplement::native_event_queue_custom_event_type)
		{
			((NativeEvent*)event.user.data2)->HandleEvent();
		}
		else
		if (_currentWindowWindow)
		{
			_currentWindowWindow->window_receive_event(event);
		}
		else if (event.type == SDL_EVENT_QUIT)
		{
			Application->Terminate();
		}
	}
}

static bool process_events()
{
	try
	{
		try
		{
			::Application->Run();
			if (::Application->IsTarminate())
			{
				TVPSystemUninit();
				if (TVPSystemControl)
				{
					delete TVPSystemControl;
					TVPSystemControl = nullptr;
				}
				return false;
			}
		}
		TJS_CONVERT_TO_TJS_EXCEPTION
	}
	TVP_CATCH_AND_SHOW_SCRIPT_EXCEPTION(TJS_W("SDL event processing"));

	return true;
}

void krkrsdl3_pre_init_platform(void)
{
	TVPTerminateCode = 0;
}

void krkrsdl3_set_args(int argc, tjs_char **argv)
{
	_argc = argc;
	_wargv = argv;
}

void krkrsdl3_convert_set_args(int argc, char **argv)
{
	_argc = argc;
	_wargv = new tjs_char*[argc];

	for (int i = 0; i < argc; i += 1)
	{
		const char *narg;
		if (!i)
		{
			narg = realpath(argv[i], nullptr);
		}
		else
		{
			narg = argv[i];
		}
		if (!narg)
		{
			tjs_char *warg_copy = new tjs_char[1];
			warg_copy[0] = '\0';
			_wargv[i] = warg_copy;
			continue;
		}
		std::string v_utf8 = narg;
		tjs_string v_utf16;
		TVPUtf8ToUtf16(v_utf16, v_utf8);
		if (!i)
		{
			free((void*)narg);
		}
		tjs_char *warg_copy = new tjs_char[v_utf16.length() + 1];
		SDL_memcpy(warg_copy, v_utf16.c_str(), sizeof(tjs_char) * (v_utf16.length()));
		warg_copy[v_utf16.length()] = '\0';
		_wargv[i] = warg_copy;
	}
}

bool krkrsdl3_init_platform(void)
{

#ifdef TVP_LOG_TO_COMMANDLINE_CONSOLE
	SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_VERBOSE);
#endif

	TVPLoadMessage();

	::Application = new tTVPApplication();
	return !!::Application->StartApplication(_argc, _wargv);
}

void krkrsdl3_run_main_loop(void)
{
	while (process_events());
}

void krkrsdl3_cleanup(void)
{
	// delete application and exit forcely
	// this prevents ugly exception message on exit
	delete ::Application;
	::Application = nullptr;
	close_gamepads();
	destroy_system_cursors();
	SDL_Quit();
}

bool TVPGetKeyMouseAsyncState(tjs_uint keycode, bool getcurrent)
{
	if (keycode >= VK_LBUTTON && keycode <= VK_XBUTTON2 && keycode != VK_CANCEL)
	{
		Uint32 state = SDL_GetMouseState(nullptr, nullptr);
		switch (keycode)
		{
			case VK_LBUTTON:
				return !!(state & SDL_BUTTON_MASK(SDL_BUTTON_LEFT));
			case VK_RBUTTON:
				return !!(state & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT));
			case VK_MBUTTON:
				return !!(state & SDL_BUTTON_MASK(SDL_BUTTON_MIDDLE));
			case VK_XBUTTON1:
				return !!(state & SDL_BUTTON_MASK(SDL_BUTTON_X1));
			case VK_XBUTTON2:
				return !!(state & SDL_BUTTON_MASK(SDL_BUTTON_X2));
			default:
				return false;
		}
	}
	if (keycode >= VK_SHIFT && keycode <= VK_MENU)
	{
		Uint32 state = SDL_GetModState();
		switch (keycode)
		{
			case VK_SHIFT:
				return !!(state & SDL_KMOD_SHIFT);
			case VK_MENU:
				return !!(state & SDL_KMOD_ALT);
			case VK_CONTROL:
				return !!(state & SDL_KMOD_CTRL);
			default:
				return false;
		}
	}
	const bool *state = SDL_GetKeyboardState(nullptr);
	return state[SDL_GetScancodeFromKey(vk_key_to_sdl_key(keycode), nullptr)];
}

bool TVPGetJoyPadAsyncState(tjs_uint keycode, bool getcurrent)
{
	bool is_pressed = false;
	if (sdl_gamepads)
	{
		for (int i = 0; i < sdl_gamepad_count; i += 1)
		{
			if (sdl_gamepads[i])
			{
				is_pressed = is_pressed || !!SDL_GetGamepadButton(sdl_gamepads[i], (SDL_GamepadButton)vk_key_to_sdl_gamepadbutton(keycode));
			}
		}
	}
	return is_pressed;
}

TTVPWindowForm *TVPCreateAndAddWindow(tTJSNI_Window *w)
{
	return new TVPWindowWindow(w);
}

tjs_uint32 TVPGetCurrentShiftKeyState()
{
	tjs_uint32 f = 0;
	if (TVPGetAsyncKeyState(VK_SHIFT)) f |= TVP_SS_SHIFT;
	if (TVPGetAsyncKeyState(VK_MENU)) f |= TVP_SS_ALT;
	if (TVPGetAsyncKeyState(VK_CONTROL)) f |= TVP_SS_CTRL;
	if (TVPGetAsyncKeyState(VK_LBUTTON)) f |= TVP_SS_LEFT;
	if (TVPGetAsyncKeyState(VK_RBUTTON)) f |= TVP_SS_RIGHT;
	if (TVPGetAsyncKeyState(VK_MBUTTON)) f |= TVP_SS_MIDDLE;
	return f;
}

ttstr TVPGetPlatformName()
{
	return SDL_GetPlatform();
}

ttstr TVPGetOSName()
{
	return TVPGetPlatformName();
}

tjs_uint32 TVP_TShiftState_To_uint32(TShiftState state)
{
	tjs_uint32 result = 0;
	if (state & MK_SHIFT) result |= ssShift;
	if (state & MK_CONTROL) result |= ssCtrl;
	if (state & MK_ALT) result |= ssAlt;
	return result;
}
TShiftState TVP_TShiftState_From_uint32(tjs_uint32 state)
{
	TShiftState result = 0;
	if (state & ssShift) result |= MK_SHIFT;
	if (state & ssCtrl) result |= MK_CONTROL;
	if (state & ssAlt) result |= MK_ALT;
	return result;
}

extern void TVPInitializeFont();
extern void TVPAddSystemFontToFreeType(const std::string &storage,
	std::vector<tjs_string> *faces);
extern void TVPGetSystemFontListFromFreeType(std::vector<tjs_string> &faces);

namespace
{
std::mutex TVPSystemFontListMutex;
bool TVPSystemFontListInitialized = false;

bool TVPCopyFontURLPath(CFURLRef url, std::string &path)
{
	path.clear();
	CFStringRef path_string = CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle);
	if(!path_string) return false;

	const CFIndex maximum_size = CFStringGetMaximumSizeForEncoding(
		CFStringGetLength(path_string), kCFStringEncodingUTF8);
	if(maximum_size < 0)
	{
		CFRelease(path_string);
		return false;
	}

	std::vector<char> buffer(static_cast<std::size_t>(maximum_size) + 1);
	const bool converted = CFStringGetCString(path_string, buffer.data(),
		static_cast<CFIndex>(buffer.size()), kCFStringEncodingUTF8);
	CFRelease(path_string);
	if(!converted) return false;

	path.assign(buffer.data());
	return !path.empty();
}
}

void TVPGetAllFontList(std::vector<tjs_string>& list)
{
	std::lock_guard<std::mutex> lock(TVPSystemFontListMutex);
	TVPInitializeFont();

	if(!TVPSystemFontListInitialized)
	{
		CFArrayRef font_urls = CTFontManagerCopyAvailableFontURLs();
		if(!font_urls)
		{
			TVPAddLog(TJS_W("Could not enumerate macOS system fonts"));
			TVPGetSystemFontListFromFreeType(list);
			return;
		}

		std::set<std::string> registered_paths;
		try
		{
			const CFIndex count = CFArrayGetCount(font_urls);
			for(CFIndex i = 0; i < count; ++i)
			{
				CFTypeRef value = CFArrayGetValueAtIndex(font_urls, i);
				if(!value || CFGetTypeID(value) != CFURLGetTypeID()) continue;

				std::string path;
				if(!TVPCopyFontURLPath(static_cast<CFURLRef>(value), path)) continue;
				if(!registered_paths.insert(path).second) continue;

				TVPAddSystemFontToFreeType(path, nullptr);
			}
		}
		catch(...)
		{
			CFRelease(font_urls);
			throw;
		}
		CFRelease(font_urls);
		TVPSystemFontListInitialized = true;
	}

	TVPGetSystemFontListFromFreeType(list);
}

const tjs_char *TVPGetDefaultFontName()
{
	if (!TVPGetCommandLine(TJS_W("-deffont"), nullptr))
	{
		TVPSetCommandLine(TJS_W("-deffont"), TJS_W("Noto Sans CJK JP"));
	}
	static tjs_int ArgumentGeneration = 0;
	if (ArgumentGeneration != TVPGetCommandLineArgumentGeneration())
	{
		ArgumentGeneration = TVPGetCommandLineArgumentGeneration();
		// Use that font, if specified on the command line
		tTJSVariant opt;
		if (TVPGetCommandLine(TJS_W("-deffont"), &opt))
		{
			ttstr str(opt);
			TVPDefaultFontName.AssignMessage(str.c_str());
		}
	}
	return TVPDefaultFontName;
}

void TVPSetDefaultFontName(const tjs_char *name)
{
	TVPSetCommandLine(TJS_W("-deffont"), name);
}

static ttstr TVPDefaultFaceNames;
const ttstr &TVPGetDefaultFaceNames()
{
	static tjs_int ArgumentGeneration = 0;
	if (ArgumentGeneration != TVPGetCommandLineArgumentGeneration())
	{
		ArgumentGeneration = TVPGetCommandLineArgumentGeneration();
		TVPDefaultFaceNames = TJS_W("");
	}
	if (TVPDefaultFaceNames.IsEmpty())
	{
		TVPDefaultFaceNames = ttstr(TVPGetDefaultFontName());
	}
	return TVPDefaultFaceNames;
}

// Override allocation functions by removing the std::bad_alloc throw and doing garbage collection.
#ifdef KRKRSDL3_OVERRIDE_NEW_ALLOCATOR_FUNCTIONS
void *operator new(std::size_t size) noexcept
{
	bool has_gced = false;

	if (!size)
		size = 1;
	void *p;
	while ((p = ::malloc(size)) == nullptr)
	{
		// If malloc fails, try to free up memory.
		if (has_gced)
			break;
		else
		{
			TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_MAX);
			has_gced = true;
		}
	}
	return p;
}

void *operator new(size_t size, const std::nothrow_t&) noexcept
{
	void *p = 0;
	p = ::operator new(size);
	return p;
}

void *operator new[](size_t size) noexcept
{
	return ::operator new(size);
}

void *operator new[](size_t size, const std::nothrow_t&) noexcept
{
	void *p = 0;
	p = ::operator new[](size);
	return p;
}

void operator delete(void *ptr) noexcept
{
	::free(ptr);
}

void operator delete(void *ptr, const std::nothrow_t&) noexcept
{
	::operator delete(ptr);
}

void operator delete(void *ptr, size_t) noexcept
{
	::operator delete(ptr);
}

void operator delete[] (void *ptr) noexcept
{
	::operator delete(ptr);
}

void operator delete[] (void *ptr, const std::nothrow_t&) noexcept
{
	::operator delete[](ptr);
}

void operator delete[] (void *ptr, size_t) noexcept
{
	::operator delete[](ptr);
}

#ifdef __cpp_aligned_new
void *operator new(std::size_t size, std::align_val_t alignment) noexcept
{
	bool has_gced = false;

	if (!size)
		size = 1;
	if (static_cast<size_t>(alignment) < sizeof(void*))
		alignment = std::align_val_t(sizeof(void*));
	void *p;
	while (::posix_memalign(&p, static_cast<size_t>(alignment), size))
	{
		// If posix_memalign fails, try to free up memory.
		if (has_gced)
		{
			p = nullptr; // posix_memalign doesn't initialize 'p' on failure
			break;
		}
		else
		{
			TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_MAX);
			has_gced = true;
		}
	}
	return p;
}

void *operator new(size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
	void *p = nullptr;
	p = ::operator new(size, alignment);
	return p;
}

void *operator new[](size_t size, std::align_val_t alignment) noexcept
{
	return ::operator new(size, alignment);
}

void *operator new[](size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
	void *p = nullptr;
	p = ::operator new[](size, alignment);
	return p;
}

void operator delete(void *ptr, std::align_val_t) noexcept
{
	::free(ptr);
}

void operator delete(void *ptr, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
	::operator delete(ptr, alignment);
}

void operator delete(void *ptr, size_t, std::align_val_t alignment) noexcept
{
	::operator delete(ptr, alignment);
}

void operator delete[] (void *ptr, std::align_val_t alignment) noexcept
{
	::operator delete(ptr, alignment);
}

void operator delete[] (void *ptr, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
	::operator delete[](ptr, alignment);
}

void operator delete[] (void *ptr, size_t, std::align_val_t alignment) noexcept
{
	::operator delete[](ptr, alignment);
}
#endif

#endif
