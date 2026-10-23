/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

#include "tjsCommHead.h"

#include "TVPScreen.h"
#include "Application.h"

#include "DebugIntf.h"
#include "MsgIntf.h"

int tTVPScreen::GetWidth() {
#if 0
	HMONITOR hMonitor = ::MonitorFromWindow( Application->GetMainWindowHandle(), MONITOR_DEFAULTTOPRIMARY );
	MONITORINFO monitorinfo = {sizeof(MONITORINFO)};
	if( ::GetMonitorInfo( hMonitor, &monitorinfo ) != FALSE ) {
		return monitorinfo.rcMonitor.right - monitorinfo.rcMonitor.left;
	}
	return ::GetSystemMetrics(SM_CXSCREEN);
#endif
	SDL_Rect r;
	return SDL_GetDisplayUsableBounds(0, &r) ? 0 : r.w;
}
int tTVPScreen::GetHeight() {
#if 0
	HMONITOR hMonitor = ::MonitorFromWindow( Application->GetMainWindowHandle(), MONITOR_DEFAULTTOPRIMARY );
	MONITORINFO monitorinfo = {sizeof(MONITORINFO)};
	if( ::GetMonitorInfo( hMonitor, &monitorinfo ) != FALSE ) {
		return monitorinfo.rcMonitor.bottom - monitorinfo.rcMonitor.top;
	}
	return ::GetSystemMetrics(SM_CYSCREEN);
#endif
	SDL_Rect r;
	return SDL_GetDisplayUsableBounds(0, &r) ? 0 : r.h;
}

int tTVPScreen::GetDesktopLeft() {
#if 0
	RECT r;
	GetDesktopRect(r);
	return r.left;
#endif
	SDL_Rect r;
	return SDL_GetDisplayUsableBounds(0, &r) ? 0 : r.x;
}
int tTVPScreen::GetDesktopTop() {
#if 0
	RECT r;
	GetDesktopRect(r);
	return r.top;
#endif
	SDL_Rect r;
	return SDL_GetDisplayUsableBounds(0, &r) ? 0 : r.y;
}
int tTVPScreen::GetDesktopWidth() {
#if 0
	RECT r;
	GetDesktopRect(r);
	return r.right - r.left;
#endif
	return GetWidth();
}
int tTVPScreen::GetDesktopHeight() {
#if 0
	RECT r;
	GetDesktopRect(r);
	return r.bottom - r.top;
#endif
	return GetHeight();
}
