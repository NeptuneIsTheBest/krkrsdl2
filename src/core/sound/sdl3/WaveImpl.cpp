//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Wave Player implementation
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include <math.h>
#include <algorithm>
#include <mutex>
#include "SystemControl.h"
#include "DebugIntf.h"
#include "MsgIntf.h"
#include "StorageIntf.h"
#include "WaveImpl.h"
#include "PluginImpl.h"
#include "SysInitIntf.h"
#include "ThreadIntf.h"
#include "Random.h"
#include "UtilStreams.h"
#include "TickCount.h"

#include "TVPTimer.h"
#include "Application.h"
#include "UserEvent.h"
#include "NativeEventQueue.h"

#define DWORD uint32_t


//---------------------------------------------------------------------------
// static function for TJS WaveSoundBuffer class
//---------------------------------------------------------------------------
void TVPSoundSetGlobalVolume(tjs_int v)
{
	tTJSNI_WaveSoundBuffer::SetGlobalVolume(v);
}
tjs_int TVPSoundGetGlobalVolume()
{
	return tTJSNI_WaveSoundBuffer::GetGlobalVolume();
}
void TVPSoundSetGlobalFocusMode(tTVPSoundGlobalFocusMode b)
{
	tTJSNI_WaveSoundBuffer::SetGlobalFocusMode(b);
}
tTVPSoundGlobalFocusMode TVPSoundGetGlobalFocusMode()
{
	return tTJSNI_WaveSoundBuffer::GetGlobalFocusMode();
}

//---------------------------------------------------------------------------
// Options management
//---------------------------------------------------------------------------
static tTVPThreadPriority TVPDecodeThreadHighPriority = ttpHigher;
static tTVPThreadPriority TVPDecodeThreadLowPriority = ttpLowest;
static bool TVPSoundOptionsInit = false;
static bool TVPControlPrimaryBufferRun = true;
static bool TVPAlwaysRecreateSoundBuffer = false;
static tjs_int TVPL1BufferLength = 1000; // in ms
static tjs_int TVPL2BufferLength = 1000; // in ms
static tjs_int TVPVolumeLogFactor = 3322;
static bool TVPPrimarySoundBufferPlaying = false;
//---------------------------------------------------------------------------
static void TVPInitSoundOptions()
{
	if(TVPSoundOptionsInit) return;

	// retrieve options from commandline
/*
 ttpIdle = 0
 ttpLowest = 1
 ttpLower = 2
 ttpNormal = 3
 ttpHigher = 4
 ttpHighest = 5
 ttpTimeCritical = 6
*/

	tTJSVariant val;
	if(TVPGetCommandLine(TJS_W("-wsdecpri"), &val))
	{
		tjs_int v = val;
		if(v < 0) v = 0;
		if(v > 5) v = 5; // tpTimeCritical is dangerous...
		TVPDecodeThreadLowPriority = (tTVPThreadPriority)v;
		if(TVPDecodeThreadHighPriority<TVPDecodeThreadLowPriority)
			TVPDecodeThreadHighPriority = TVPDecodeThreadLowPriority;
	}

	if(TVPGetCommandLine(TJS_W("-wscontrolpri"), &val))
	{
		if(ttstr(val) == TJS_W("yes"))
			TVPControlPrimaryBufferRun = true;
		else
			TVPControlPrimaryBufferRun = false;
	}

	if(TVPGetCommandLine(TJS_W("-wsrecreate"), &val))
	{
		if(ttstr(val) == TJS_W("yes"))
			TVPAlwaysRecreateSoundBuffer = true;
		else
			TVPAlwaysRecreateSoundBuffer = false;
	}

	if(TVPGetCommandLine(TJS_W("-wsl1len"), &val))
	{
		tjs_int n = (tjs_int)val;
		if(n > 0 && n < 600000) TVPL1BufferLength = n;
	}

	if(TVPGetCommandLine(TJS_W("-wsl2len"), &val))
	{
		tjs_int n = (tjs_int)val;
		if(n > 0 && n < 600000) TVPL2BufferLength = n;
	}

	if(TVPGetCommandLine(TJS_W("-wsvolfactor"), &val))
	{
		tjs_int n = (tjs_int)val;
		if(n > 0 && n < 200000) TVPVolumeLogFactor = n;
	}

	TVPSoundOptionsInit = true;
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// TSS plug-in interface
//---------------------------------------------------------------------------
class tTVPTSSWaveDecoder : public tTVPWaveDecoder
{
	ITSSWaveDecoder *Decoder;

public:

	tTVPTSSWaveDecoder(ITSSWaveDecoder *decoder) { Decoder = decoder; }
	~tTVPTSSWaveDecoder() { Decoder->Release(); };

	void GetFormat(tTVPWaveFormat & format)
	{
		TSSWaveFormat f;
		if(FAILED(Decoder->GetFormat(&f)))
		{
			TVPThrowExceptionMessage(TVPPluginError,
				TJS_W("ITSSWaveDecoder::GetFormat failed."));
		}
		format.SamplesPerSec = f.dwSamplesPerSec;
		format.Channels = f.dwChannels;
		if(f.dwBitsPerSample >= 0x10000)
		{
			// floating-point format since 2.17 beta 5
			format.IsFloat = true;
			format.BitsPerSample = f.dwBitsPerSample - 0x10000;
		}
		else
		{
			format.IsFloat = false;
			format.BitsPerSample = f.dwBitsPerSample;
		}
		format.BytesPerSample = format.BitsPerSample / 8;
		format.TotalSamples = f.ui64TotalSamples;
		format.TotalTime = f.dwTotalTime;
		format.Seekable = 0!=f.dwSeekable;
		format.SpeakerConfig = 0;
	}

	bool Render(void *buf, tjs_uint bufsamplelen, tjs_uint& rendered)
	{
		HRESULT hr;
		TSS_ULONG rend;
		TSS_ULONG st;
		hr = Decoder->Render(buf, bufsamplelen, &rend, &st);
		rendered = rend; // count of rendered samples
		if(FAILED(hr)) return false;
		return 0!=st;
	}

	bool SetPosition(tjs_uint64 samplepos)
	{
		HRESULT hr;
		hr = Decoder->SetPosition(samplepos);
		if(FAILED(hr)) return false;
		return true;
	}
};
//---------------------------------------------------------------------------
class tTVPTSSWaveDecoderCreator : public tTVPWaveDecoderCreator
{
public:
	tTVPWaveDecoder * Create(const ttstr & storagename,	const ttstr &extension)
	{
		ITSSWaveDecoder * dec = TVPSearchAvailTSSWaveDecoder(storagename, extension);
		if(!dec) return NULL;

		return new tTVPTSSWaveDecoder(dec);
	}
} static TVPTSSWaveDecoderCreator;
//---------------------------------------------------------------------------
static bool TVPTSSWaveDecoderCreatorRegistered = false;
void TVPRegisterTSSWaveDecoderCreator()
{
	if(!TVPTSSWaveDecoderCreatorRegistered)
	{
		TVPRegisterWaveDecoderCreator(&TVPTSSWaveDecoderCreator);
		TVPTSSWaveDecoderCreatorRegistered = true;
	}
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// Log Table for DirectSound volume
//---------------------------------------------------------------------------
static bool TVPLogTableInit = false;
static tjs_int TVPLogTable[101];
static void TVPInitLogTable()
{
	if(TVPLogTableInit) return;
	TVPLogTableInit = true;
	tjs_int i;
	TVPLogTable[0] = -10000;
	for(i = 1; i <= 100; i++)
	{
		TVPLogTable[i] = static_cast<tjs_int>( log10((double)i/100.0)*TVPVolumeLogFactor );
	}
}
//---------------------------------------------------------------------------
tjs_int TVPVolumeToDSAttenuate(tjs_int volume)
{
	TVPInitLogTable();
	volume = volume / 1000;
	if(volume > 100) volume = 100;
	if(volume < 0 ) volume = 0;
	return TVPLogTable[volume];
}
//---------------------------------------------------------------------------
tjs_int TVPDSAttenuateToVolume(tjs_int att)
{
	if(att <= -10000) return 0;
	return (tjs_int)(pow(10, (double)att / TVPVolumeLogFactor) * 100.0) * 1000;
}
//---------------------------------------------------------------------------
tjs_int TVPPanToDSAttenuate(tjs_int volume)
{
	TVPInitLogTable();
	volume = volume / 1000;
	if(volume > 100) volume = 100;
	if(volume < -100 ) volume = -100;
	if(volume < 0)
		return TVPLogTable[100 - (-volume)];
	else
		return -TVPLogTable[100 - volume];
}
//---------------------------------------------------------------------------
tjs_int TVPDSAttenuateToPan(tjs_int att)
{
	if(att <= -10000) return -100000;
	if(att >=  10000) return  100000;
	if(att < 0)
		return (100 - (tjs_int)(pow(10, (double)att /  TVPVolumeLogFactor) * 100.0)) * -1000;
	else
		return (100 - (tjs_int)(pow(10, (double)att / -TVPVolumeLogFactor) * 100.0)) *  1000;
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// DirectSound management
//---------------------------------------------------------------------------
static bool TVPPrimaryBufferPlayingByProgram = false;
static bool TVPDeferedSettingAvailable = false;
static bool TVPDirectSoundUse3D = false;
//---------------------------------------------------------------------------
static void TVPEnsurePrimaryBufferPlay()
{
	if(!TVPControlPrimaryBufferRun) return;

	TVPInitDirectSound();
	if(!TVPPrimaryBufferPlayingByProgram) {
		TVPPrimaryBufferPlayingByProgram = true;
	}
}
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
void TVPWaveSoundBufferCommitSettings()
{
	// OpenAL applies source settings immediately, but callers still rely on
	// this function consuming the deferred-settings state.
	if(TVPDeferedSettingAvailable)
		TVPDeferedSettingAvailable = false;
}
//---------------------------------------------------------------------------
static void TVPMakeSilentWaveBytes(void *dest, tjs_int bytes, const tTVPWaveFormat *format)
{
	if(format->BytesPerSample == 1)
	{
		// 0x80
		memset(dest, 0x80, bytes);
	}
	else
	{
		// 0x00
		memset(dest, 0x00, bytes);
	}
}
//---------------------------------------------------------------------------
static void TVPMakeSilentWave(void *dest, tjs_int count, const tTVPWaveFormat *format)
{
	tjs_int bytes = count * format->Channels * format->BytesPerSample;
	TVPMakeSilentWaveBytes(dest, bytes, format);
}
//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
// Buffer management
//---------------------------------------------------------------------------
std::vector<tTJSNI_WaveSoundBuffer *> TVPWaveSoundBufferVector;
tTJSCriticalSection TVPWaveSoundBufferVectorCS;

//---------------------------------------------------------------------------
// tTVPWaveSoundBufferThread : playing thread
//---------------------------------------------------------------------------
/*
	The system has one playing thread.
	The playing thread fills each sound buffer's L1 (DirectSound) buffer, and
	also manages timing for label events.
	The technique used in this algorithm is similar to Timer claass implementation.
*/
class tTVPWaveSoundBufferThread : public tTVPThread
{
	tTVPThreadEvent Event;
	std::mutex SuspendMutex;
	bool SuspendThread;

	void SetSuspend()
	{
		std::lock_guard<std::mutex> lock(SuspendMutex);
		SuspendThread = true;
	}
	void ResetSuspend()
	{
		std::lock_guard<std::mutex> lock(SuspendMutex);
		SuspendThread = false;
	}
	bool GetSuspend()
	{
		std::lock_guard<std::mutex> lock(SuspendMutex);
		return SuspendThread;
	}

	bool PendingLabelEventExists;
	bool WndProcToBeCalled;
	tjs_uint NextLabelEventTick;
	tjs_uint LastFilledTick;

	NativeEventQueue<tTVPWaveSoundBufferThread> EventQueue;
public:
	tTVPWaveSoundBufferThread();
	~tTVPWaveSoundBufferThread();

private:
	void UtilWndProc( NativeEvent& ev );

public:
	void ReschedulePendingLabelEvent(tjs_int tick);

protected:
	void Execute(void);

public:
	void InternalTrigger();
	void Start(void);
	void CheckBufferSleep();


} static *TVPWaveSoundBufferThread = NULL;
//---------------------------------------------------------------------------
void TVPLockSoundMixer() {
	TVPPrimaryBufferPlayingByProgram = false;
}
void TVPUnlockSoundMixer() {
	if (TVPWaveSoundBufferThread) TVPEnsurePrimaryBufferPlay();
}
//---------------------------------------------------------------------------
tTVPWaveSoundBufferThread::tTVPWaveSoundBufferThread()
	: SuspendThread( false ), PendingLabelEventExists( false ),
	WndProcToBeCalled( false ), NextLabelEventTick( 0 ), LastFilledTick( 0 ),
	EventQueue(this,&tTVPWaveSoundBufferThread::UtilWndProc)
{
	EventQueue.Allocate();
	SetPriority(ttpHighest);
	StartTread();
}
//---------------------------------------------------------------------------
tTVPWaveSoundBufferThread::~tTVPWaveSoundBufferThread()
{
	SetPriority(ttpNormal);
	Terminate();
	ResetSuspend();
	Event.Set();
	WaitFor();
	EventQueue.Deallocate();
}
//---------------------------------------------------------------------------
void tTVPWaveSoundBufferThread::UtilWndProc( NativeEvent& ev )
{
	// Window procedure of UtilWindow
	if( ev.Message == TVP_EV_WAVE_SND_BUF_THREAD && !GetTerminated())
	{
		// pending events occur
		tTJSCriticalSectionHolder holder(TVPWaveSoundBufferVectorCS); // protect the object

		WndProcToBeCalled = false;

		tjs_int64 tick = TVPGetTickCount();

		int nearest_next = TVP_TIMEOFS_INVALID_VALUE;

		std::vector<tTJSNI_WaveSoundBuffer *>::iterator i;
		for(i = TVPWaveSoundBufferVector.begin();
			i != TVPWaveSoundBufferVector.end(); i++)
		{
			int next = (*i)->FireLabelEventsAndGetNearestLabelEventStep(tick);
				// fire label events and get nearest label event step
			if(next != TVP_TIMEOFS_INVALID_VALUE)
			{
				if(nearest_next == TVP_TIMEOFS_INVALID_VALUE || nearest_next > next)
					nearest_next = next;
			}
		}

		if(nearest_next != TVP_TIMEOFS_INVALID_VALUE)
		{
			PendingLabelEventExists = true;
			NextLabelEventTick = TVPGetRoughTickCount32() + nearest_next;
		}
		else
		{
			PendingLabelEventExists = false;
		}
	}
	else
	{
		EventQueue.HandlerDefault(ev);
	}
}
//---------------------------------------------------------------------------
void tTVPWaveSoundBufferThread::ReschedulePendingLabelEvent(tjs_int tick)
{
	if(tick == TVP_TIMEOFS_INVALID_VALUE) return; // no need to reschedule
	tjs_uint32 eventtick = TVPGetRoughTickCount32() + tick;

	tTJSCriticalSectionHolder holder(TVPWaveSoundBufferVectorCS);

	if(PendingLabelEventExists)
	{
		if((tjs_int32)NextLabelEventTick - (tjs_int32)eventtick > 0)
			NextLabelEventTick = eventtick;
	}
	else
	{
		PendingLabelEventExists = true;
		NextLabelEventTick = eventtick;
	}
}
//---------------------------------------------------------------------------
#define TVP_WSB_THREAD_SLEEP_TIME 60
void tTVPWaveSoundBufferThread::Execute(void)
{
	while(!GetTerminated())
	{
		// thread loop for playing thread
		tjs_uint32 time = TVPGetRoughTickCount32();
		TVPPushEnvironNoise(&time, sizeof(time));

		{	// thread-protected
			tTJSCriticalSectionHolder holder(TVPWaveSoundBufferVectorCS);

			if (TVPPrimaryBufferPlayingByProgram != TVPPrimarySoundBufferPlaying) {
				TVPPrimarySoundBufferPlaying = TVPPrimaryBufferPlayingByProgram;
				std::vector<tTJSNI_WaveSoundBuffer *>::iterator i;
				for (i = TVPWaveSoundBufferVector.begin();
					i != TVPWaveSoundBufferVector.end(); i++)
				{
					if ((*i)->ThreadCallbackEnabled)
						(*i)->SetBufferPaused(!TVPPrimaryBufferPlayingByProgram); // for preventing buffer runs out on iOS' OpenAL implement
				}
			}

			// check PendingLabelEventExists
			if(PendingLabelEventExists)
			{
				if(!WndProcToBeCalled)
				{
					WndProcToBeCalled = true;
					EventQueue.PostEvent( NativeEvent(TVP_EV_WAVE_SND_BUF_THREAD) );
				}
			}

			if (TVPPrimarySoundBufferPlaying && time - LastFilledTick >= TVP_WSB_THREAD_SLEEP_TIME)
			{
				std::vector<tTJSNI_WaveSoundBuffer *>::iterator i;
				for(i = TVPWaveSoundBufferVector.begin();
					i != TVPWaveSoundBufferVector.end(); i++)
				{
					if((*i)->ThreadCallbackEnabled)
						(*i)->FillBuffer(); // fill sound buffer
				}
				LastFilledTick = time;
			}
		}	// end-of-thread-protected

		tjs_uint32 time2;
		time2 = TVPGetRoughTickCount32();
		time = time2 - time;

		if(time < TVP_WSB_THREAD_SLEEP_TIME)
		{
			tjs_int sleep_time = TVP_WSB_THREAD_SLEEP_TIME - time;
			if(PendingLabelEventExists)
			{
				tjs_int step_to_next = (tjs_int32)NextLabelEventTick - (tjs_int32)time2;
				if(step_to_next < sleep_time)
					sleep_time = step_to_next;
				if(sleep_time < 1) sleep_time = 1;
			}
			Event.WaitFor(sleep_time);
		}
		else
		{
			Event.WaitFor(1);
		}

		if(!GetTerminated() && GetSuspend())
			Event.WaitFor(0); // wait indefinitely until playback resumes
	}
}
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
void tTVPWaveSoundBufferThread::Start()
{
	ResetSuspend();
	TVPPrimaryBufferPlayingByProgram = true;
	Event.Set();
}
//---------------------------------------------------------------------------
void tTVPWaveSoundBufferThread::CheckBufferSleep()
{
	tTJSCriticalSectionHolder holder(TVPWaveSoundBufferVectorCS);
	std::vector<tTJSNI_WaveSoundBuffer *>::iterator i;
	for(i = TVPWaveSoundBufferVector.begin();
		i != TVPWaveSoundBufferVector.end(); i++)
	{
		if((*i)->ThreadCallbackEnabled)
			return;
	}

	SetSuspend(); // all buffers are sleeping
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
static void TVPReleaseSoundBuffers(bool disableevent = true)
{
	// release all secondary buffers.
	tTJSCriticalSectionHolder holder(TVPWaveSoundBufferVectorCS);
	std::vector<tTJSNI_WaveSoundBuffer *>::iterator i;
	for(i = TVPWaveSoundBufferVector.begin();
		i != TVPWaveSoundBufferVector.end(); i++)
	{
		(*i)->FreeDirectSoundBuffer(disableevent);
	}
}
//---------------------------------------------------------------------------
static void TVPShutdownWaveSoundBuffers()
{
	// clean up soundbuffers at exit
	if(TVPWaveSoundBufferThread)
		delete TVPWaveSoundBufferThread, TVPWaveSoundBufferThread = NULL;
	TVPReleaseSoundBuffers();
}
static tTVPAtExit TVPShutdownWaveSoundBuffersAtExit
	(TVP_ATEXIT_PRI_PREPARE, TVPShutdownWaveSoundBuffers);
//---------------------------------------------------------------------------
static void TVPEnsureWaveSoundBufferWorking()
{
	if(!TVPWaveSoundBufferThread)
		TVPWaveSoundBufferThread = new tTVPWaveSoundBufferThread();
	TVPWaveSoundBufferThread->Start();
}
//---------------------------------------------------------------------------
static void TVPCheckSoundBufferAllSleep()
{
	if(TVPWaveSoundBufferThread)
		TVPWaveSoundBufferThread->CheckBufferSleep();
}
//---------------------------------------------------------------------------
static void TVPAddWaveSoundBuffer(tTJSNI_WaveSoundBuffer * buffer)
{
	tTJSCriticalSectionHolder holder(TVPWaveSoundBufferVectorCS);
	TVPWaveSoundBufferVector.push_back(buffer);
}
//---------------------------------------------------------------------------
static void TVPRemoveWaveSoundBuffer(tTJSNI_WaveSoundBuffer * buffer)
{
	bool bufferempty;

	{
		tTJSCriticalSectionHolder holder(TVPWaveSoundBufferVectorCS);
		std::vector<tTJSNI_WaveSoundBuffer *>::iterator i;
		i = std::find(TVPWaveSoundBufferVector.begin(),
			TVPWaveSoundBufferVector.end(),
			buffer);
		if(i != TVPWaveSoundBufferVector.end())
			TVPWaveSoundBufferVector.erase(i);
		bufferempty = TVPWaveSoundBufferVector.size() == 0;
	}

	if(bufferempty)
	{
		if(TVPWaveSoundBufferThread)
			delete TVPWaveSoundBufferThread, TVPWaveSoundBufferThread = NULL;
	}
}
//---------------------------------------------------------------------------
static void TVPReschedulePendingLabelEvent(tjs_int tick)
{
	if(TVPWaveSoundBufferThread)
		TVPWaveSoundBufferThread->ReschedulePendingLabelEvent(tick);
}
//---------------------------------------------------------------------------
void TVPResetVolumeToAllSoundBuffer()
{
	// call each SoundBuffer's SetVolumeToSoundBuffer
	tTJSCriticalSectionHolder holder(TVPWaveSoundBufferVectorCS);
	std::vector<tTJSNI_WaveSoundBuffer *>::iterator i;
	for(i = TVPWaveSoundBufferVector.begin();
		i != TVPWaveSoundBufferVector.end(); i++)
	{
		(*i)->SetVolumeToSoundBuffer();
	}
}
//---------------------------------------------------------------------------
void TVPReleaseDirectSound()
{
	TVPReleaseSoundBuffers(false);
	TVPUninitDirectSound();
}
//---------------------------------------------------------------------------
void TVPSetWaveSoundBufferUse3DMode(bool b)
{
	// Changing the 3D mode invalidates the format of existing buffers.
	if(b != TVPDirectSoundUse3D)
	{
		TVPReleaseDirectSound();
		TVPDirectSoundUse3D = b;
	}
}
//---------------------------------------------------------------------------
bool TVPGetWaveSoundBufferUse3DMode()
{
	return TVPDirectSoundUse3D;
}
//---------------------------------------------------------------------------





//---------------------------------------------------------------------------
// tTVPWaveSoundBufferDcodeThread : decoding thread
//---------------------------------------------------------------------------
class tTVPWaveSoundBufferDecodeThread : public tTVPThread
{
	tTJSNI_WaveSoundBuffer * Owner;
	tTVPThreadEvent Event;
	tTJSCriticalSection OneLoopCS;
	volatile bool Running;

public:
	tTVPWaveSoundBufferDecodeThread(tTJSNI_WaveSoundBuffer * owner);
	~tTVPWaveSoundBufferDecodeThread();

	void Execute(void);

	void Interrupt();
	void Continue();

	bool GetRunning() const { return Running; }
};
//---------------------------------------------------------------------------
tTVPWaveSoundBufferDecodeThread::tTVPWaveSoundBufferDecodeThread(
	tTJSNI_WaveSoundBuffer * owner)
{
	TVPInitSoundOptions();

	Owner = owner;
	SetPriority(TVPDecodeThreadHighPriority);
	Running = false;
	StartTread();
}
//---------------------------------------------------------------------------
tTVPWaveSoundBufferDecodeThread::~tTVPWaveSoundBufferDecodeThread()
{
	SetPriority(TVPDecodeThreadHighPriority);
	Running = false;
	Terminate();
	Event.Set();
	WaitFor();
}
//---------------------------------------------------------------------------
#define TVP_WSB_DECODE_THREAD_SLEEP_TIME 110
void tTVPWaveSoundBufferDecodeThread::Execute(void)
{
	while(!GetTerminated())
	{
		// decoder thread main loop
		DWORD st = TVPGetTickCount();
		while(Running)
		{
			bool wait;
			DWORD et;

			if(Running)
			{
				volatile tTJSCriticalSectionHolder cs_holder(OneLoopCS);
				wait = !Owner->FillL2Buffer(false, true); // fill
			}

			if(GetTerminated()) break;

			if(Running)
			{
				et = TVPGetTickCount();
				TVPPushEnvironNoise(&et, sizeof(et));
				if(wait)
				{
					// buffer is full; sleep longer
					DWORD elapsed = et -st;
					if(elapsed < TVP_WSB_DECODE_THREAD_SLEEP_TIME)
					{
						Event.WaitFor(
							TVP_WSB_DECODE_THREAD_SLEEP_TIME - elapsed);
					}
				}
				else
				{
					// buffer is not full; sleep shorter
					if(!GetTerminated()) SetPriority(TVPDecodeThreadLowPriority);
				}
				st = et;
			}
		}
		if(GetTerminated()) break;
		// sleep while running
		Event.WaitFor(/*INFINITE*/0);
	}
}
//---------------------------------------------------------------------------
void tTVPWaveSoundBufferDecodeThread::Interrupt()
{
	// interrupt the thread
	if(!Running) return;
	SetPriority(TVPDecodeThreadHighPriority);
	Event.Set();
	tTJSCriticalSectionHolder cs_holder(OneLoopCS);
		// this ensures that this function stops the decoding
	Running = false;
}
//---------------------------------------------------------------------------
void tTVPWaveSoundBufferDecodeThread::Continue()
{
	SetPriority(TVPDecodeThreadHighPriority);
	Running = true;
	Event.Set();
}
//---------------------------------------------------------------------------





//---------------------------------------------------------------------------
// tTJSNI_WaveSoundBuffer
//---------------------------------------------------------------------------
tjs_int tTJSNI_WaveSoundBuffer::GlobalVolume = 100000;
tTVPSoundGlobalFocusMode tTJSNI_WaveSoundBuffer::GlobalFocusMode = sgfmNeverMute;
//---------------------------------------------------------------------------
tTJSNI_WaveSoundBuffer::tTJSNI_WaveSoundBuffer()
{
	TVPInitSoundOptions();
	TVPRegisterTSSWaveDecoderCreator();
	Decoder = NULL;
	LoopManager = NULL;
	Thread = NULL;
	UseVisBuffer = false;
	VisBuffer = NULL;
	ThreadCallbackEnabled = false;
	Level2Buffer = NULL;
	Level2BufferSize = 0;
	Volume =  100000;
	Volume2 = 100000;
	BufferCanControlPan = false;
	Pan = 0;
	PosX = PosY = PosZ = (D3DVALUE)0.0;
	SoundBuffer = NULL;
	L2BufferDecodedSamplesInUnit = NULL;
	L1BufferSegmentQueues = NULL;
	L2BufferSegmentQueues = NULL;
	L1BufferDecodeSamplePos = NULL;
	DecodePos = 0;
	L1BufferUnits = 0;
	L2BufferUnits = 0;
	TVPAddWaveSoundBuffer(this);
	Thread = new tTVPWaveSoundBufferDecodeThread(this);
	memset(&C_InputFormat, 0, sizeof(C_InputFormat));
	memset(&InputFormat, 0, sizeof(InputFormat));
	Looping = false;
	DSBufferPlaying = false;
	BufferPlaying = false;
	Paused = false;
	BufferBytes = 0;
	AccessUnitBytes = 0;
	AccessUnitSamples = 0;
	L2AccessUnitBytes = 0;
	SoundBufferPrevReadPos = 0;
	SoundBufferWritePos = 0;
	PlayStopPos = 0;
	L2BufferReadPos = 0;
	L2BufferWritePos = 0;
	L2BufferRemain = 0;
	L2BufferEnded = false;
	LastCheckedDecodePos = -1;
	LastCheckedTick = 0;
}
//---------------------------------------------------------------------------
tjs_error TJS_INTF_METHOD
tTJSNI_WaveSoundBuffer::Construct(tjs_int numparams, tTJSVariant **param,
		iTJSDispatch2 *tjs_obj)
{
	tjs_error hr = inherited::Construct(numparams, param, tjs_obj);
	if(TJS_FAILED(hr)) return hr;

	return TJS_S_OK;
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTJSNI_WaveSoundBuffer::Invalidate()
{
	inherited::Invalidate();

	Clear();

	DestroySoundBuffer();

	if(Thread) delete Thread, Thread = NULL;

	TVPRemoveWaveSoundBuffer(this);
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::ThrowSoundBufferException(const ttstr &reason)
{
	TVPThrowExceptionMessage(TVPCannotCreateSoundBuffer,
		reason, ttstr().printf(TJS_W("frequency=%d/channels=%d/bits=%d"),
		InputFormat.SamplesPerSec, InputFormat.Channels,
		InputFormat.BitsPerSample));
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::TryCreateSoundBuffer(bool use3d)
{
	// release previous sound buffer
	if(SoundBuffer) SoundBuffer->Release(), SoundBuffer = NULL;

	// compute buffer bytes
	AccessUnitSamples = InputFormat.SamplesPerSec / TVP_WSB_ACCESS_FREQ;
	AccessUnitBytes = AccessUnitSamples * InputFormat.Channels * InputFormat.BytesPerSample;

	L1BufferUnits = TVPL1BufferLength / (1000 / TVP_WSB_ACCESS_FREQ);
	if(L1BufferUnits <= 1) L1BufferUnits = 2;
	if(L1BufferSegmentQueues) delete [] L1BufferSegmentQueues, L1BufferSegmentQueues = NULL;
	L1BufferSegmentQueues = new tTVPWaveSegmentQueue[L1BufferUnits];
	LabelEventQueue.clear();
	if(L1BufferDecodeSamplePos) delete [] L1BufferDecodeSamplePos, L1BufferDecodeSamplePos = NULL;
	L1BufferDecodeSamplePos = new tjs_int64[L1BufferUnits];
	BufferBytes = AccessUnitBytes * L1BufferUnits;
		// l1 buffer bytes

	if(BufferBytes <= 0)
		ThrowSoundBufferException(TJS_W("Invalid format."));

	// allocate visualization buffer
	if(UseVisBuffer) ResetVisBuffer();

	// allocate level2 buffer ( 4sec. buffer )
	L2BufferUnits = TVPL2BufferLength / (1000 / TVP_WSB_ACCESS_FREQ);
	if(L2BufferUnits <= 1) L2BufferUnits = 2;

	if(L2BufferDecodedSamplesInUnit) delete [] L2BufferDecodedSamplesInUnit, L2BufferDecodedSamplesInUnit = NULL;
	if(L2BufferSegmentQueues) delete [] L2BufferSegmentQueues, L2BufferSegmentQueues = NULL;
	L2BufferDecodedSamplesInUnit = new tjs_int[L2BufferUnits];
	L2BufferSegmentQueues = new tTVPWaveSegmentQueue[L2BufferUnits];

	L2AccessUnitBytes = AccessUnitSamples * InputFormat.BytesPerSample * InputFormat.Channels;
	Level2BufferSize = L2AccessUnitBytes * L2BufferUnits;
	if(Level2Buffer) delete [] Level2Buffer, Level2Buffer = NULL;
	Level2Buffer = new tjs_uint8[Level2BufferSize];

	SoundBuffer = TVPCreateSoundBuffer(InputFormat, L1BufferUnits);
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::CreateSoundBuffer()
{
	// create a direct sound secondary buffer which has given format.

	TVPInitDirectSound(); // ensure DirectSound object

	bool format_is_not_identical = TVPAlwaysRecreateSoundBuffer ||
		C_InputFormat.SamplesPerSec		!= InputFormat.SamplesPerSec ||
		C_InputFormat.Channels			!= InputFormat.Channels ||
		C_InputFormat.BitsPerSample		!= InputFormat.BitsPerSample ||
		C_InputFormat.BytesPerSample	!= InputFormat.BytesPerSample ||
		C_InputFormat.SpeakerConfig		!= InputFormat.SpeakerConfig ||
		C_InputFormat.IsFloat			!= InputFormat.IsFloat;

	if(format_is_not_identical)
	{
		try
		{
			ttstr msg;
			bool failed;
			bool use3d = (InputFormat.Channels >= 3 || InputFormat.SpeakerConfig != 0) ?
				false : TVPDirectSoundUse3D;

			failed = false;
			try
			{
				TryCreateSoundBuffer(use3d);
			}
			catch(eTJSError &e)
			{
				failed = true;
				msg = e.GetMessage();
			}


			if(failed)
				TVPThrowExceptionMessage(msg.c_str());


		}
		catch(ttstr & e)
		{
			ThrowSoundBufferException(e);
		}
		catch(...)
		{
			throw;
		}

	}

	// reset volume, sound position and frequency
	SetVolumeToSoundBuffer();
	Set3DPositionToBuffer();
	SetFrequencyToBuffer();

	// reset sound buffer
	ResetSoundBuffer();

	C_InputFormat = InputFormat;
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::DestroySoundBuffer()
{

	if(SoundBuffer)
	{
		SoundBuffer->Stop();
		SoundBuffer->Release();
		SoundBuffer = NULL;
	}

	DSBufferPlaying = false;
	BufferPlaying = false;

	if(L1BufferSegmentQueues) delete [] L1BufferSegmentQueues, L1BufferSegmentQueues = NULL;
	LabelEventQueue.clear();
	if(L1BufferDecodeSamplePos) delete [] L1BufferDecodeSamplePos, L1BufferDecodeSamplePos = NULL;
	if(L2BufferDecodedSamplesInUnit) delete [] L2BufferDecodedSamplesInUnit, L2BufferDecodedSamplesInUnit = NULL;
	if(L2BufferSegmentQueues) delete [] L2BufferSegmentQueues, L2BufferSegmentQueues = NULL;
	if(Level2Buffer) delete [] Level2Buffer, Level2Buffer = NULL;
	L1BufferUnits = 0;
	L2BufferUnits = 0;

	memset(&C_InputFormat, 0x00, sizeof(C_InputFormat));

	Level2BufferSize = 0;

	DeallocateVisBuffer();
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::ResetSoundBuffer()
{
	if(SoundBuffer)
	{
		SoundBuffer->Reset();
	}

	ResetSamplePositions();
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::ResetSamplePositions()
{
	// reset L1BufferSegmentQueues and L2BufferSegmentQueues, and labels
	if(L1BufferSegmentQueues)
	{
		for(int i = 0; i < L1BufferUnits; i++)
			L1BufferSegmentQueues[i].Clear();
	}
	if(L2BufferSegmentQueues)
	{
		for(int i = 0; i < L2BufferUnits; i++)
			L2BufferSegmentQueues[i].Clear();
	}
	if(L1BufferDecodeSamplePos)
	{
		for(int i = 0; i < L1BufferUnits; i++)
			L1BufferDecodeSamplePos[i] = -1;
	}
	LabelEventQueue.clear();
	DecodePos = 0;
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::Clear()
{
	// clear all status and unload current decoder
	Stop();
	ThreadCallbackEnabled = false;
	TVPCheckSoundBufferAllSleep();
	Thread->Interrupt();
	if(LoopManager) delete LoopManager, LoopManager = NULL;
	ClearFilterChain();
	if(Decoder) delete Decoder, Decoder = NULL;
	BufferPlaying = false;
	DSBufferPlaying = false;
	Paused = false;

	ResetSamplePositions();

	SetStatus(ssUnload);
}
//---------------------------------------------------------------------------
tjs_uint tTJSNI_WaveSoundBuffer::Decode(void *buffer, tjs_uint bufsamplelen,
		tTVPWaveSegmentQueue & segments)
{
	// decode one buffer unit
	tjs_uint w = 0;

	try
	{
		// decode
		FilterOutput->Decode((tjs_uint8*)buffer, bufsamplelen, w, segments);
	}
	catch(...)
	{
		// ignore errors
		w = 0;
	}

	return w;
}
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
bool tTJSNI_WaveSoundBuffer::FillL2Buffer(bool firstwrite, bool fromdecodethread)
{
	if(!fromdecodethread && Thread->GetRunning())
		Thread->SetPriority(ttpHighest);
			// make decoder thread priority high, before entering critical section

	tTJSCriticalSectionHolder holder(L2BufferCS);

	if(firstwrite)
	{
		// only main thread runs here
		L2BufferReadPos = L2BufferWritePos = L2BufferRemain = 0;
		L2BufferEnded = false;
		for(tjs_int i = 0; i<L2BufferUnits; i++)
			L2BufferDecodedSamplesInUnit[i] = 0;
	}

	{
		tTVPThreadPriority ttpbefore = TVPDecodeThreadHighPriority;
		bool retflag = false;
		if(Thread->GetRunning())
		{
			ttpbefore = Thread->GetPriority();
			Thread->SetPriority(TVPDecodeThreadHighPriority);
		}
		{
			tTJSCriticalSectionHolder holder(L2BufferRemainCS);
			if(L2BufferRemain == L2BufferUnits) retflag = true;
		}
		if(!retflag) UpdateFilterChain(); // if the buffer is not full, update filter internal state
		if(Thread->GetRunning()) Thread->SetPriority(ttpbefore);
		if(retflag) return false; // buffer is full
	}

	if(L2BufferEnded)
	{
		L2BufferSegmentQueues[L2BufferWritePos].Clear();
		L2BufferDecodedSamplesInUnit[L2BufferWritePos] = 0;
	}
	else
	{
		L2BufferSegmentQueues[L2BufferWritePos].Clear();
		tjs_uint decoded = Decode(
			L2BufferWritePos * L2AccessUnitBytes + Level2Buffer,
			AccessUnitSamples,
			L2BufferSegmentQueues[L2BufferWritePos]);

		if(decoded < (tjs_uint) AccessUnitSamples) L2BufferEnded = true;

		L2BufferDecodedSamplesInUnit[L2BufferWritePos] = decoded;
	}

	L2BufferWritePos++;
	if(L2BufferWritePos >= L2BufferUnits) L2BufferWritePos = 0;

	{
		tTVPThreadPriority ttpbefore = TVPDecodeThreadHighPriority;
		if(Thread->GetRunning())
		{
			ttpbefore = Thread->GetPriority();
			Thread->SetPriority(TVPDecodeThreadHighPriority);
		}
		{
			tTJSCriticalSectionHolder holder(L2BufferRemainCS);
			L2BufferRemain++;
		}
		if(Thread->GetRunning()) Thread->SetPriority(ttpbefore);
	}

	return true;
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::PrepareToReadL2Buffer(bool firstread)
{
	if(L2BufferRemain == 0 && !L2BufferEnded)
		FillL2Buffer(firstread, false);

	if(Thread->GetRunning()) Thread->SetPriority(TVPDecodeThreadHighPriority);
			// make decoder thread priority higher than usual,
			// before entering critical section
}
//---------------------------------------------------------------------------
tjs_uint tTJSNI_WaveSoundBuffer::ReadL2Buffer(void *buffer,
		tTVPWaveSegmentQueue & segments)
{
	// This routine is protected by BufferCS, not L2BufferCS, while
	// this routine reads L2 buffer.
	// But It's ok because this function will never read currently writing L2
	// buffer. L2 buffer having at least one rendered unit is
	// guaranteed at this point.

	tjs_uint decoded = L2BufferDecodedSamplesInUnit[L2BufferReadPos];

	segments = L2BufferSegmentQueues[L2BufferReadPos];

	if (decoded) {
		SoundBuffer->AppendBuffer(L2BufferReadPos * L2AccessUnitBytes + Level2Buffer,
			decoded * InputFormat.BytesPerSample * InputFormat.Channels/*, SoundBufferWritePos*/);
		if (buffer) { // for VisBuffer
			memcpy(buffer, L2BufferReadPos * L2AccessUnitBytes + Level2Buffer, decoded * InputFormat.BytesPerSample * InputFormat.Channels);
		}
		TVPMakeSilentWave(L2BufferReadPos * L2AccessUnitBytes + Level2Buffer, decoded, &InputFormat);
	}
	if (buffer && decoded < (tjs_uint)AccessUnitSamples)
	{
		// fill rest with silence
		TVPMakeSilentWave((tjs_uint8*)buffer + decoded * InputFormat.Channels * InputFormat.BytesPerSample,
			AccessUnitSamples - decoded, &InputFormat);
	}

	L2BufferReadPos++;
	if(L2BufferReadPos >= L2BufferUnits) L2BufferReadPos = 0;

	{
		tTJSCriticalSectionHolder holder(L2BufferRemainCS);
		L2BufferRemain--;
	}

	return decoded;
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::FillDSBuffer(tjs_int writepos,
		tTVPWaveSegmentQueue & segments)
{

	segments.Clear();

	if (SoundBuffer->IsBufferValid())
	{
		ReadL2Buffer(UseVisBuffer ? VisBuffer + writepos : nullptr, segments);
	}
}
//---------------------------------------------------------------------------
bool tTJSNI_WaveSoundBuffer::FillBuffer(bool firstwrite, bool allowpause)
{
	// fill DirectSound secondary buffer with one render unit.

	tTJSCriticalSectionHolder holder(BufferCS);

	if(!SoundBuffer) return true;
	if(!Decoder) return true;
	if(!BufferPlaying) return true;
	if (!TVPPrimarySoundBufferPlaying) return true;

	// check paused state
	if(allowpause)
	{
		if(Paused)
		{
			if(DSBufferPlaying)
			{
				SoundBuffer->Pause();
				DSBufferPlaying = false;
			}
			return true;
		}
		else
		{
			if(!DSBufferPlaying)
			{
				SoundBuffer->Play(/*0, 0, DSBPLAY_LOOPING*/);
				DSBufferPlaying = true;
			}
		}
	}

	// check decoder thread status
	tjs_int bufferremain;
	{
		tTJSCriticalSectionHolder holder(L2BufferRemainCS);
		bufferremain = L2BufferRemain;
	}

	if(Thread->GetRunning() && bufferremain < TVP_WSB_ACCESS_FREQ )
		Thread->SetPriority(ttpNormal); // buffer remains under 1 sec

	// check buffer playing position
	tjs_int writepos;

	// check position
	tTVPWaveSegmentQueue * segment;
	tjs_int64 * bufferdecodesamplepos;

	if(firstwrite)
	{
		writepos = 0;
		segment = L1BufferSegmentQueues + 0;
		bufferdecodesamplepos = L1BufferDecodeSamplePos + 0;
		PlayStopPos = -1;
		SoundBufferWritePos = 1;
		SoundBufferPrevReadPos = 0;
	}
	else
	{
		ResetLastCheckedDecodePos(/*pp*/);

		if (L2BufferEnded) {
			if (SoundBuffer->GetRemainBuffers() == 0) {
					FlushAllLabelEvents();
					SoundBuffer->Pause();
					ResetSamplePositions();
					DSBufferPlaying = false;
					BufferPlaying = false;
					if(LoopManager) LoopManager->SetPosition(0);
					return true;
				}
			}

		writepos = SoundBufferWritePos * AccessUnitBytes;
		if (SoundBuffer->GetRemainBuffers() >= TVPAL_BUFFER_COUNT)
		{
			return true;
		}

		segment = L1BufferSegmentQueues + SoundBufferWritePos;
		bufferdecodesamplepos = L1BufferDecodeSamplePos + SoundBufferWritePos;
		SoundBufferWritePos ++;
		if(SoundBufferWritePos >= L1BufferUnits)
			SoundBufferWritePos = 0;
	}

	// decode
	if(bufferremain > 1) // buffer is ready
	{
		// with no locking operations
		FillDSBuffer(writepos, *segment);
	}
	else
	{
		PrepareToReadL2Buffer(false); // complete decoding before reading from L2

		{
			tTJSCriticalSectionHolder l2holder(L2BufferCS);
			FillDSBuffer(writepos, *segment);
		}
	}

	// insert labels into LabelEventQueue and sort
	const std::deque<tTVPWaveLabel> & labels = segment->GetLabels();
	if(labels.size() != 0)
	{
		// add DecodePos offset to each item->Offset
		// and insert into LabelEventQueue
		for(std::deque<tTVPWaveLabel>::const_iterator i = labels.begin();
			i != labels.end(); i++)
		{
			LabelEventQueue.emplace_back(
				i->Position,
						i->Name, static_cast<tjs_int>(i->Offset + DecodePos));
		}

		// sort
		std::sort(LabelEventQueue.begin(), LabelEventQueue.end(),
			tTVPWaveLabel::tSortByOffsetFuncObj());

		// re-schedule label events
		TVPReschedulePendingLabelEvent(GetNearestEventStep());
	}

	// write bufferdecodesamplepos
	*bufferdecodesamplepos = DecodePos;
	DecodePos += AccessUnitSamples;

	return false;
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::ResetLastCheckedDecodePos(DWORD pp)
{
	// set LastCheckedDecodePos and  LastCheckedTick
	// we shoud reset these values because the clock sources are usually
	// not identical.
	tTJSCriticalSectionHolder holder(BufferCS);

	if (!SoundBuffer) return;

	int offset, rblock;
	{
		rblock = SoundBufferWritePos;
		offset = 0;
	}
	if (L1BufferDecodeSamplePos[rblock] != -1)
	{
		LastCheckedDecodePos = L1BufferDecodeSamplePos[rblock] + offset;
		LastCheckedTick = TVPGetTickCount();
	}
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_WaveSoundBuffer::FireLabelEventsAndGetNearestLabelEventStep(tjs_int64 tick)
{
	// fire events, event.EventTick <= tick, and return relative time to
	// next nearest event (return TVP_TIMEOFS_INVALID_VALUE for no events).

	// the vector LabelEventQueue must be sorted by the position.
	tTJSCriticalSectionHolder holder(BufferCS);

	if(!BufferPlaying) return TVP_TIMEOFS_INVALID_VALUE; // buffer is not currently playing
	if(!DSBufferPlaying) return TVP_TIMEOFS_INVALID_VALUE; // direct sound buffer is not currently playing

	if(LabelEventQueue.size() == 0) return TVP_TIMEOFS_INVALID_VALUE; // no more events

	// calculate current playing decodepos
	// at this point, LastCheckedDecodePos must not be -1
	if(LastCheckedDecodePos == -1) ResetLastCheckedDecodePos();
	tjs_int64 decodepos = (tick - LastCheckedTick) * Frequency / 1000 +
		LastCheckedDecodePos;

	while(true)
	{
		if(LabelEventQueue.size() == 0) break;
		std::vector<tTVPWaveLabel>::iterator i = LabelEventQueue.begin();
		int diff = (tjs_int32)i->Offset - (tjs_int32)decodepos;
		if(diff <= 0)
			InvokeLabelEvent(i->Name);
		else
			break;
		LabelEventQueue.erase(i);
	}

	if(LabelEventQueue.size() == 0) return TVP_TIMEOFS_INVALID_VALUE; // no more events

	return (tjs_int)(
		(LabelEventQueue[0].Offset - (tjs_int32)decodepos) * 1000 / Frequency);
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_WaveSoundBuffer::GetNearestEventStep()
{
	// get nearest event stop from current tick
	// (current tick is taken from TVPGetTickCount)
	tTJSCriticalSectionHolder holder(BufferCS);

	if(LabelEventQueue.size() == 0) return TVP_TIMEOFS_INVALID_VALUE; // no more events

	// calculate current playing decodepos
	// at this point, LastCheckedDecodePos must not be -1
	if(LastCheckedDecodePos == -1) ResetLastCheckedDecodePos();
	tjs_int64 decodepos = (TVPGetTickCount() - LastCheckedTick) * Frequency / 1000 +
		LastCheckedDecodePos;

	return (tjs_int)(
		(LabelEventQueue[0].Offset - (tjs_int32)decodepos) * 1000 / Frequency);
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::FlushAllLabelEvents()
{
	// called at the end of the decode.
	// flush all undelivered events.
	tTJSCriticalSectionHolder holder(BufferCS);

	for(std::vector<tTVPWaveLabel>::iterator i = LabelEventQueue.begin();
		i != LabelEventQueue.end(); i++)
		InvokeLabelEvent(i->Name);

	LabelEventQueue.clear();
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::StartPlay()
{
	if(!Decoder) return;

	// let primary buffer to start running
	TVPEnsurePrimaryBufferPlay();

	// ensure playing thread
	TVPEnsureWaveSoundBufferWorking();

	// play from first

	{	// thread protected block
		if(Thread->GetRunning()) { Thread->SetPriority(TVPDecodeThreadHighPriority); }
		tTJSCriticalSectionHolder holder(BufferCS);
		tTJSCriticalSectionHolder l2holder(L2BufferCS);

		CreateSoundBuffer();

		// reset filter chain
		ResetFilterChain();

		// fill sound buffer with some first samples
		BufferPlaying = true;
		FillL2Buffer(true, false);
		FillBuffer(true, false);

		// start playing
		if(!Paused)
		{
			SoundBuffer->Play(/*0, 0, DSBPLAY_LOOPING*/);
			DSBufferPlaying = true;
		}

		// re-schedule label events
	}	// end of thread protected block

	// ensure thread
	TVPEnsureWaveSoundBufferWorking(); // wake the playing thread up again
	ThreadCallbackEnabled = true;
	Thread->Continue();

}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::StopPlay()
{
	if(!Decoder) return;
	if(!SoundBuffer) return;

	if(Thread->GetRunning()) { Thread->SetPriority(TVPDecodeThreadHighPriority);}
	tTJSCriticalSectionHolder holder(BufferCS);
	tTJSCriticalSectionHolder l2holder(L2BufferCS);

	SoundBuffer->Stop();
	DSBufferPlaying = false;
	BufferPlaying = false;

}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::Play()
{
	// play from first or current position
	if(!Decoder) return;
	if(BufferPlaying) return;

	StopPlay();

	TVPEnsurePrimaryBufferPlay(); // let primary buffer to start running

	if(Thread->GetRunning()) { Thread->SetPriority(TVPDecodeThreadHighPriority);}
	tTJSCriticalSectionHolder holder(BufferCS);
	tTJSCriticalSectionHolder l2holder(L2BufferCS);

	StartPlay();
	SetStatus(ssPlay);
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::Stop()
{
	// stop playing
	StopPlay();

	// delete thread
	ThreadCallbackEnabled = false;
	TVPCheckSoundBufferAllSleep();
	Thread->Interrupt();

	// set status
	if(Status != ssUnload) SetStatus(ssStop);

	// rewind
	if(LoopManager) LoopManager->SetPosition(0);
}

void tTJSNI_WaveSoundBuffer::SetBufferPaused(bool bPaused) {
	if (!Decoder || !SoundBuffer) return;

	if (bPaused)
		SoundBuffer->Pause();
	else { // restore
		if (!Paused && DSBufferPlaying && BufferPlaying) {
			SoundBuffer->Play();
		}
	}
}

//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetPaused(bool b)
{
	if(Thread->GetRunning())
		{/*orgpri = Thread->Priority;*/
			Thread->SetPriority(TVPDecodeThreadHighPriority); }
	tTJSCriticalSectionHolder holder(BufferCS);
	tTJSCriticalSectionHolder l2holder(L2BufferCS);

	Paused = b;
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::TimerBeatHandler()
{
	inherited::TimerBeatHandler();

	// check buffer stopping
	if(Status == ssPlay && !BufferPlaying)
	{
		// buffer was stopped
		ThreadCallbackEnabled = false;
		TVPCheckSoundBufferAllSleep();
		Thread->Interrupt();
		SetStatusAsync(ssStop);
	}
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::Open(const ttstr & storagename)
{
	// open a storage and prepare to play
	TVPEnsurePrimaryBufferPlay(); // let primary buffer to start running

	Clear();

	Decoder = TVPCreateWaveDecoder(storagename);

	try
	{
		// make manager
		LoopManager = new tTVPWaveLoopManager();
		LoopManager->SetDecoder(Decoder);
		LoopManager->SetLooping(Looping);

		// build filter chain
		RebuildFilterChain();

		// retrieve format
		InputFormat = FilterOutput->GetFormat();
		Frequency = InputFormat.SamplesPerSec;
	}
	catch(...)
	{
		Clear();
		throw;
	}

	// open loop information file
	ttstr sliname = storagename + TJS_W(".sli");
	if(TVPIsExistentStorage(sliname))
	{
		tTVPStreamHolder slistream(sliname);
		char *buffer;
		tjs_uint size;
		buffer = new char [ (size = static_cast<tjs_uint>(slistream->GetSize())) +1];
		try
		{
			slistream->ReadBuffer(buffer, size);
			buffer[size] = 0;

			if(!LoopManager->ReadInformation(buffer))
				TVPThrowExceptionMessage(TVPInvalidLoopInformation, sliname);
			RecreateWaveLabelsObject();
		}
		catch(...)
		{
			delete [] buffer;
			Clear();
			throw;
		}
		delete [] buffer;
	}

	// set status to stop
	SetStatus(ssStop);
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetLooping(bool b)
{
	Looping = b;
	if(LoopManager) LoopManager->SetLooping(Looping);
}
//---------------------------------------------------------------------------
tjs_uint64 tTJSNI_WaveSoundBuffer::GetSamplePosition()
{
	if(!Decoder) return 0L;
	if(!SoundBuffer) return 0L;

	tTJSCriticalSectionHolder holder(BufferCS);

	int offset, rblock;
	{
		rblock = SoundBufferWritePos;
		offset = 0;
	}

	tTVPWaveSegmentQueue & segs = L1BufferSegmentQueues[rblock];

	return segs.FilteredPositionToDecodePosition(offset);
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetSamplePosition(tjs_uint64 pos)
{
	tjs_uint64 possamples = pos; // in samples

	if(InputFormat.TotalSamples && InputFormat.TotalSamples <= possamples) return;

	if(BufferPlaying && DSBufferPlaying)
	{
		StopPlay();
		LoopManager->SetPosition(possamples);
		StartPlay();
	}
	else
	{
		LoopManager->SetPosition(possamples);
	}
}
//---------------------------------------------------------------------------
tjs_uint64 tTJSNI_WaveSoundBuffer::GetPosition()
{
	if(!Decoder) return 0L;
	if(!SoundBuffer) return 0L;

	return GetSamplePosition() * 1000 / InputFormat.SamplesPerSec;
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetPosition(tjs_uint64 pos)
{
	SetSamplePosition(pos * InputFormat.SamplesPerSec / 1000); // in samples
}
//---------------------------------------------------------------------------
tjs_uint64 tTJSNI_WaveSoundBuffer::GetTotalTime()
{
	return InputFormat.TotalSamples * 1000 / InputFormat.SamplesPerSec;
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetVolumeToSoundBuffer()
{
	// set current volume/pan to DirectSound buffer
	if(SoundBuffer)
	{
		tjs_int v;
		tjs_int mutevol = 100000;

		// compute volume for each buffer
		v = (Volume / 10) * (Volume2 / 10) / 1000;
		v = (v / 10) * (GlobalVolume / 10) / 1000;
		v = (v / 10) * (mutevol / 10) / 1000;
		SoundBuffer->SetVolume(/*TVPVolumeToDSAttenuate*/(v / 100000.0f));

		if(BufferCanControlPan)
		{
			// set pan
			SoundBuffer->SetPan(/*TVPPanToDSAttenuate*/(Pan / 100000.0f));
		}
	}
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetVolume(tjs_int v)
{
	if(v < 0) v = 0;
	if(v > 100000) v = 100000;

	if(Volume != v)
	{
		Volume = v;
		SetVolumeToSoundBuffer();
	}
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetVolume2(tjs_int v)
{
	if(v < 0) v = 0;
	if(v > 100000) v = 100000;

	if(Volume2 != v)
	{
		Volume2 = v;
		SetVolumeToSoundBuffer();
	}
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetPan(tjs_int v)
{
	if(v < -100000) v = -100000;
	if(v > 100000) v = 100000;
	if(Pan != v)
	{
		Pan = v;
		if(BufferCanControlPan)
		{
			// set pan with SetPan
			SetVolumeToSoundBuffer();
		}
		else
		{
			// set pan with 3D sound facility
			// note that setting pan can reset 3D position.
			PosZ = (D3DVALUE)0.0;
			PosY = (D3DVALUE)0.001;
			// PosX = -0.003 .. -0.0001 = 0 = +0.0001 ... +0.003
			float t;
			t = static_cast<float>( ((float)v / 100000.0) );
			t *= static_cast<float>( t * 0.003 );
			if(v < 0) t = - t;
			PosX = t;
			Set3DPositionToBuffer();
		}
	}
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetGlobalVolume(tjs_int v)
{
	if(v < 0) v = 0;
	if(v > 100000) v = 100000;

	if(GlobalVolume != v)
	{
		GlobalVolume = v;
		TVPResetVolumeToAllSoundBuffer();
	}
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetGlobalFocusMode(tTVPSoundGlobalFocusMode b)
{
	if(GlobalFocusMode != b)
	{
		GlobalFocusMode = b;
		TVPResetVolumeToAllSoundBuffer();
	}
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::Set3DPositionToBuffer()
{
	if(SoundBuffer)
	{
		SoundBuffer->SetPosition(PosX, PosY, PosZ/*, DS3D_DEFERRED*/);
		// defered settings are to be commited at next tickbeat event.
		TVPDeferedSettingAvailable = true;
	}
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetPos(D3DVALUE x, D3DVALUE y, D3DVALUE z)
{
	PosX = x;
	PosY = y;
	PosZ = z;
	Set3DPositionToBuffer();
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetPosX(D3DVALUE v)
{
	PosX = v;
	Set3DPositionToBuffer();
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetPosY(D3DVALUE v)
{
	PosY = v;
	Set3DPositionToBuffer();
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetPosZ(D3DVALUE v)
{
	PosZ = v;
	Set3DPositionToBuffer();
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetFrequencyToBuffer()
{
	if(SoundBuffer) SoundBuffer->SetFrequency(Frequency);
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetFrequency(tjs_int freq)
{
	// set frequency
	Frequency = freq;
	SetFrequencyToBuffer();
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::SetUseVisBuffer(bool b)
{
	tTJSCriticalSectionHolder holder(BufferCS);

	if(b)
	{
		UseVisBuffer = true;

		if(SoundBuffer) ResetVisBuffer();
	}
	else
	{
		DeallocateVisBuffer();
		UseVisBuffer = false;
	}
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::ResetVisBuffer()
{
	// reset or recreate visualication buffer
	tTJSCriticalSectionHolder holder(BufferCS);

	DeallocateVisBuffer();

	VisBuffer = new tjs_uint8 [BufferBytes];
	UseVisBuffer = true;
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::DeallocateVisBuffer()
{
	tTJSCriticalSectionHolder holder(BufferCS);

	if(VisBuffer) delete [] VisBuffer, VisBuffer = NULL;
	UseVisBuffer = false;
}
//---------------------------------------------------------------------------
void tTJSNI_WaveSoundBuffer::CopyVisBuffer(tjs_int16 *dest, const tjs_uint8 *src,
	tjs_int numsamples, tjs_int channels)
{

	if(channels == 1)
	{
		TVPConvertPCMTo16bits(dest, (const void*)src, InputFormat.Channels,
			InputFormat.BytesPerSample, InputFormat.BitsPerSample,
			InputFormat.IsFloat, numsamples, true);
	}
	else if (channels == InputFormat.Channels)
	{
		TVPConvertPCMTo16bits(dest, (const void*)src, InputFormat.Channels,
			InputFormat.BytesPerSample, InputFormat.BitsPerSample,
			InputFormat.IsFloat, numsamples, false);
	}
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_WaveSoundBuffer::GetVisBuffer(tjs_int16 *dest, tjs_int numsamples,
	tjs_int channels, tjs_int aheadsamples)
{
	// read visualization buffer samples
	if(!UseVisBuffer) return 0;
	if(!VisBuffer) return 0;
	if(!Decoder) return 0;
	if(!SoundBuffer) return 0;
	if(!DSBufferPlaying || !BufferPlaying) return 0;

	if(channels != InputFormat.Channels && channels != 1) return 0;

	// retrieve current playing position

	tjs_int buffersamples = BufferBytes / (InputFormat.Channels * InputFormat.BytesPerSample);
	int offset;

	{
		tTJSCriticalSectionHolder holder(BufferCS);
		// the critical section protects only here;
		// the rest is not important code (does anyone care about that the retrieved
		// visualization becomes wrong a little ?)

		offset = SoundBuffer->GetCurrentPlaySamples() + aheadsamples;
		int rblock = offset / AccessUnitSamples;
		offset %= buffersamples;
		if (L1BufferSegmentQueues[rblock % L1BufferUnits].GetFilteredLength() == 0)
			return 0;
	}

	// copy to distination buffer
	tjs_int writtensamples = 0;
	if(numsamples > 0)
	{
		while(true)
		{
			tjs_int bufrest = buffersamples - offset;
			tjs_int copysamples = (bufrest > numsamples ? numsamples : bufrest);

			CopyVisBuffer(dest, VisBuffer + offset * InputFormat.Channels * InputFormat.BytesPerSample,
				copysamples, channels);

			numsamples -= copysamples;
			writtensamples += copysamples;
			if(numsamples <= 0) break;

			dest += channels * copysamples;
			offset += copysamples;
			offset = offset % buffersamples;
		}
	}

	return writtensamples;
}
//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
// tTJSNC_WaveSoundBuffer
//---------------------------------------------------------------------------
//tTJSNativeInstance *tTJSNC_WaveSoundBuffer::CreateNativeInstance()
static tTJSNativeInstance *CreateNativeInstance()
{
	return new tTJSNI_WaveSoundBuffer();
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// TVPCreateNativeClass_WaveSoundBuffer
//---------------------------------------------------------------------------
tTJSNativeClass * TVPCreateNativeClass_WaveSoundBuffer()
{
	tTJSNativeClass *cls = new tTJSNC_WaveSoundBuffer();
	((tTJSNC_WaveSoundBuffer*)cls)->Factory = CreateNativeInstance;
	static tjs_uint32 TJS_NCM_CLASSID;
	TJS_NCM_CLASSID = tTJSNC_WaveSoundBuffer::ClassID;

//----------------------------------------------------------------------
// methods
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/freeDirectSound)  /* static */
{
	// release directsound
	TVPReleaseDirectSound();

	return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL_OUTER(/*object to register*/cls,
	/*func. name*/freeDirectSound)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/getVisBuffer)
{
	// get samples for visualization
	TJS_GET_NATIVE_INSTANCE(/*var. name*/_this,
		/*var. type*/tTJSNI_WaveSoundBuffer);

	if(numparams < 3) return TJS_E_BADPARAMCOUNT;
	tjs_int16 *dest = (tjs_int16*)(uintptr_t)(tTVInteger)(*param[0]);

	tjs_int ahead = 0;
	if(numparams >= 4) ahead = (tjs_int)*param[3];

	tjs_int res = _this->GetVisBuffer(dest, *param[1], *param[2], ahead);

	if(result) *result = res;

	return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL_OUTER(/*object to register*/cls,
	/*func. name*/getVisBuffer)
//----------------------------------------------------------------------



//----------------------------------------------------------------------
// properties
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(useVisBuffer)
{
	TJS_BEGIN_NATIVE_PROP_GETTER
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this,
			/*var. type*/tTJSNI_WaveSoundBuffer);

		*result = _this->GetUseVisBuffer();
		return TJS_S_OK;
	}
	TJS_END_NATIVE_PROP_GETTER

	TJS_BEGIN_NATIVE_PROP_SETTER
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this,
			/*var. type*/tTJSNI_WaveSoundBuffer);

		_this->SetUseVisBuffer(0!=(tjs_int)*param);

		return TJS_S_OK;
	}
	TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL_OUTER(cls, useVisBuffer)
//----------------------------------------------------------------------
	return cls;
}
//---------------------------------------------------------------------------
