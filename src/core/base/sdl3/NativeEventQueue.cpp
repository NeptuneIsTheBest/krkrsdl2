/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

#include "tjsCommHead.h"
#include "NativeEventQueue.h"

#include "Application.h"
#include "DebugIntf.h"
#include <SDL3/SDL.h>

tjs_uint32 NativeEventQueueImplement::native_event_queue_custom_event_type = 0;

NativeEventQueueImplement::NativeEventQueueImplement() {
	if (NativeEventQueueImplement::native_event_queue_custom_event_type != 0)
	{
		return;
	}
	if (SDL_WasInit(SDL_INIT_EVENTS) == 0)
	{
		if (!SDL_InitSubSystem(SDL_INIT_EVENTS))
		{
			return;
		}
	}
	NativeEventQueueImplement::native_event_queue_custom_event_type = SDL_RegisterEvents(1);
}

void NativeEventQueueImplement::PostEvent(const NativeEvent& ev) {
	if (NativeEventQueueImplement::native_event_queue_custom_event_type == 0)
	{
		return;
	}
	NativeEvent * tmp_ev = new NativeEvent(ev);
	SDL_Event event;
	SDL_memset(&event, 0, sizeof(event));
	event.type = NativeEventQueueImplement::native_event_queue_custom_event_type;
	event.user.code = 0;
	event.user.data2 = (void *)tmp_ev;
	tmp_ev->SetQueue(this);
	while (true)
	{
		if (SDL_PushEvent(&event))
		{
			break;
		}
		SDL_Delay(100);
	}
}
