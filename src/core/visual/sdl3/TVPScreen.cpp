/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

#include "tjsCommHead.h"

#include "TVPScreen.h"

#include <SDL3/SDL.h>

static bool TVPGetPrimaryDisplayUsableBounds(SDL_Rect &bounds)
{
	SDL_DisplayID display = SDL_GetPrimaryDisplay();
	return display != 0 && SDL_GetDisplayUsableBounds(display, &bounds);
}

int tTVPScreen::GetWidth() {
	SDL_Rect r;
	if (!TVPGetPrimaryDisplayUsableBounds(r))
	{
		return 0;
	}
	return r.w;
}
int tTVPScreen::GetHeight() {
	SDL_Rect r;
	if (!TVPGetPrimaryDisplayUsableBounds(r))
	{
		return 0;
	}
	return r.h;
}

int tTVPScreen::GetDesktopLeft() {
	SDL_Rect r;
	if (!TVPGetPrimaryDisplayUsableBounds(r))
	{
		return 0;
	}
	return r.x;
}
int tTVPScreen::GetDesktopTop() {
	SDL_Rect r;
	if (!TVPGetPrimaryDisplayUsableBounds(r))
	{
		return 0;
	}
	return r.y;
}
int tTVPScreen::GetDesktopWidth() {
	return GetWidth();
}
int tTVPScreen::GetDesktopHeight() {
	return GetHeight();
}
