/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

#include "tjsCommHead.h"
//---------------------------------------------------------------------------
#include "SystemControl.h"
#include "EventIntf.h"
#include "SysInitIntf.h"
#include "SysInitImpl.h"
#include "ScriptMgnIntf.h"
#include "WindowIntf.h"
#include "StorageIntf.h"
#include "DebugIntf.h"
#include "SystemImpl.h"
#include "UserEvent.h"
#include "Application.h"
#include "TickCount.h"
#include "Random.h"

tTVPSystemControl *TVPSystemControl;
bool TVPSystemControlAlive = false;

tTVPSystemControl::tTVPSystemControl() : EventEnable(true) {
	InApplicationIdle = 0;
	ContinuousEventCalling = false;
	AutoShowConsoleOnError = false;

	LastCompactedTick = 0;
	LastCloseClickedTick = 0;
	LastShowModalWindowSentTick = 0;
	LastRehashedTick = 0;

	TVPSystemControlAlive = true;

	SystemWatchTimer.SetOnTimerHandler( this, &tTVPSystemControl::SystemWatchTimerTimer );
	SystemWatchTimer.SetInterval(50);
	SystemWatchTimer.SetEnabled( true );
}
void tTVPSystemControl::InvokeEvents() {
	CallDeliverAllEventsOnIdle();
}
void tTVPSystemControl::CallDeliverAllEventsOnIdle() {
}

void tTVPSystemControl::BeginContinuousEvent() {
	if(!ContinuousEventCalling)
	{
		ContinuousEventCalling = true;
		InvokeEvents();
	}
}
void tTVPSystemControl::EndContinuousEvent() {
	if(ContinuousEventCalling)
	{
		ContinuousEventCalling = false;
	}
}
//---------------------------------------------------------------------------
void tTVPSystemControl::NotifyCloseClicked() {
	// --close Button is clicked--
	// back button is pressed
	LastCloseClickedTick = TVPGetRoughTickCount32();
}

void tTVPSystemControl::NotifyEventDelivered() {
	// called from event system, notifying the event is delivered.
	LastCloseClickedTick = 0;
}

bool tTVPSystemControl::ApplicationIdle() {
	if (InApplicationIdle < 32)
	{
		InApplicationIdle += 1;
		try
		{
			DeliverEvents();
		}
		catch(...)
		{
			InApplicationIdle -= 1;
			throw;
		}
		InApplicationIdle -= 1;
	}
	bool cont = !ContinuousEventCalling;
	MixedIdleTick += TVPGetRoughTickCount32();
	return cont;
}

void tTVPSystemControl::DeliverEvents() {
	if(ContinuousEventCalling)
		TVPProcessContinuousHandlerEventFlag = true; // set flag

	if(EventEnable) TVPDeliverAllEvents();
}

void tTVPSystemControl::SystemWatchTimerTimer() {
	// call events
	tjs_uint32 tick = TVPGetRoughTickCount32();
	// push environ noise
	TVPPushEnvironNoise(&tick, sizeof(tick));
	TVPPushEnvironNoise(&LastCompactedTick, sizeof(LastCompactedTick));
	TVPPushEnvironNoise(&LastShowModalWindowSentTick, sizeof(LastShowModalWindowSentTick));
	TVPPushEnvironNoise(&MixedIdleTick, sizeof(MixedIdleTick));

	if( !ContinuousEventCalling && tick - LastCompactedTick > 4000 ) {
		// idle state over 4 sec.
		LastCompactedTick = tick;

		// fire compact event
		TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_IDLE);
	}
	if( !ContinuousEventCalling && tick > LastRehashedTick + 1500 ) {
		// TJS2 object rehash
		LastRehashedTick = tick;
		TJSDoRehash();
	}
	// ensure modal window visible
	if( tick > LastShowModalWindowSentTick + 4100 ) {
		// This is currently disabled because IME composition window
		// hides behind the window which is bringed top by the
		// window-rearrangement.
		LastShowModalWindowSentTick = tick;
	}
}
