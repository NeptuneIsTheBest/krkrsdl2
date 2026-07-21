/* SPDX-License-Identifier: MIT */

#include "MacWindowBridge.h"

#include <SDL3/SDL.h>
#import <AppKit/AppKit.h>

void TVPMacSetWindowShadow(SDL_Window *window, bool enabled)
{
	if(!window) return;

	SDL_PropertiesID properties = SDL_GetWindowProperties(window);
	if(!properties) return;

	NSWindow *native_window = (__bridge NSWindow *)SDL_GetPointerProperty(
		properties, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
	if(!native_window) return;

	native_window.hasShadow = enabled ? YES : NO;
	if(enabled) [native_window invalidateShadow];
}
