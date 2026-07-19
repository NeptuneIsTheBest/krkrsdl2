/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

#include "tjsCommHead.h"

#include <algorithm>
#include <string>
#include <vector>
#include <assert.h>

#include "tjsError.h"
#include "tjsDebug.h"

#include "Application.h"
#include "SysInitIntf.h"
#include "SysInitImpl.h"
#include "DebugIntf.h"
#include "MsgIntf.h"
#include "ScriptMgnIntf.h"
#include "tjsError.h"
#include "PluginImpl.h"
#include "SystemIntf.h"

#include "Exception.h"
#include "SystemControl.h"
#include "SystemImpl.h"
#include "WaveImpl.h"
#include "GraphicsLoadThread.h"
#include "CharacterSet.h"
#include "EventIntf.h"
#include "StorageIntf.h"
#include "TVPColor.h"
#include "WindowImpl.h"
#include "VirtualKey.h"
#include "TVPWindow.h"
#include <unistd.h>
#include <SDL.h>

tTVPApplication* Application;

#include <unistd.h>
#include <TargetConditionals.h>
#include <libproc.h>
tjs_string ExePath() {
	static tjs_string exepath(TJS_W(""));
	if (exepath.empty())
	{
		char pathbuf[PROC_PIDPATHINFO_MAXSIZE];
		int ret = proc_pidpath(getpid(), pathbuf, sizeof(pathbuf));
		if (ret > 0)
		{
			std::string npathbuf = pathbuf;
			TVPUtf8ToUtf16(exepath, npathbuf);
		}
	}
	if (exepath.empty())
	{
		exepath = tjs_string(_wargv[0]);
	}
	return exepath;
}
bool TVPCheckAbout();
bool TVPCheckPrintDataPath();
void TVPOnError();
void TVPLockSoundMixer();
void TVPUnlockSoundMixer();

int _argc;
tjs_char ** _wargv;
tTVPApplication::tTVPApplication() : is_attach_console_(false), tarminate_(false), application_activating_(true)
	 , image_load_thread_(NULL), has_map_report_process_(false), console_cache_(1024)
{
	should_sync_savedata_ = false;
	syncfs_is_finished_ = true;
}
tTVPApplication::~tTVPApplication() {
	while( windows_list_.size() ) {
		std::vector<class TTVPWindowForm*>::iterator i = windows_list_.begin();
		delete (*i);
		// TTVPWindowForm のデストラクタ内でリストから削除されるはず
	}
	windows_list_.clear();
}
bool tTVPApplication::StartApplication( int argc, tjs_char* argv[] ) {
	// _set_se_translator(se_translator_function);

	ArgC = argc;
	ArgV = argv;
	TVPTerminateCode = 0;

	CheckConsole();

	// try starting the program!
	bool engine_init = false;
	try {

		TVPInitScriptEngine();
		engine_init = true;

		// banner
		TVPAddImportantLog( TVPFormatMessage(TVPProgramStartedOn, TVPGetOSName(), TVPGetPlatformName()) );

		// TVPInitializeBaseSystems
		TVPInitializeBaseSystems();

		if(TVPCheckPrintDataPath()) return true;
		if(TVPExecuteUserConfig()) return true;

#ifdef KRKRSDL2_ENABLE_ASYNC_IMAGE_LOAD
		image_load_thread_ = new tTVPAsyncImageLoader();
#endif

		TVPSystemInit();

		if(TVPCheckAbout()) return true; // version information dialog box;

		SetTitle( tjs_string(TVPKirikiri) );
		TVPSystemControl = new tTVPSystemControl();

#ifdef KRKRSDL2_ENABLE_ASYNC_IMAGE_LOAD
		// start image load thread
		image_load_thread_->StartTread();
#endif

		if(TVPProjectDirSelected) TVPInitializeStartupScript();

		return false;
	} catch( const EAbort & ) {
		// nothing to do
	} catch( const Exception &exception ) {
		TVPOnError();
		if(!TVPSystemUninitCalled)
			ShowException(exception.what());
	} catch( const TJS::eTJSScriptError &e ) {
		TVPOnError();
		if(!TVPSystemUninitCalled)
			ShowException( e.GetMessage().c_str() );
	} catch( const TJS::eTJS &e) {
		TVPOnError();
		if(!TVPSystemUninitCalled)
			ShowException( e.GetMessage().c_str() );
	} catch( const std::exception &e ) {
		ShowException( ttstr(e.what()).c_str() );
	} catch( const char* e ) {
		ShowException( ttstr(e).c_str() );
	} catch( const tjs_char* e ) {
		ShowException( e );
	} catch(...) {
		ShowException( (const tjs_char*)TVPUnknownError );
	}

	return true;
}
/**
 * コンソールからの起動か確認し、コンソールからの起動の場合は、標準出力を割り当てる
 */
void tTVPApplication::CheckConsole() {
	is_attach_console_ = isatty(fileno(stdout)) != 0;
	for( int i = 0; i < ArgC; i++ ) {
		if(!TJS_strcmp(ArgV[i], TJS_W("-forceoutputlogtoconsole"))) {
			is_attach_console_ = true;
		}
	}
}

void tTVPApplication::PrintConsole( const tjs_char* mes, unsigned long len, bool iserror ) {
	if( console_cache_.size() < (len*3+1) ) {
		console_cache_.resize(len*3+1);
	}
	tjs_int u8len = TVPWideCharToUtf8String( mes, &(console_cache_[0]) );
	console_cache_[u8len] = '\0';
	if (is_attach_console_)
	{
		if (iserror)
		{
			fprintf(stdout, "%s\n", &(console_cache_[0]) );
		}
		else
		{
			fprintf(stdout, "%s\n", &(console_cache_[0]) );
		}
	}
}

void tTVPApplication::BringToFront() {
	size_t size = windows_list_.size();
	for( size_t i = 0; i < size; i++ ) {
		windows_list_[i]->BringToFront();
	}
}
void tTVPApplication::ShowException( const tjs_char* e ) {
	TVPAddLog(ttstr(TVPFatalError) + TJS_W(": ") + e);
	tjs_string e_utf16 = e;
	std::string e_utf8;
	tjs_string v_utf16 = (const tjs_char *)TVPFatalError;
	std::string v_utf8;
	if (TVPUtf16ToUtf8(v_utf8, v_utf16) && TVPUtf16ToUtf8(e_utf8, e_utf16))
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, v_utf8.c_str(), e_utf8.c_str(), nullptr);
	}
}

void tTVPApplication::Run() {
	sdl_process_events();
	if (tarminate_)
	{
		return;
	}
	bool done = false;
	if (TVPSystemControl)
	{
		done = TVPSystemControl->ApplicationIdle();
	}
	tjs_int count = TVPGetWindowCount();
	for( tjs_int i = 0; i<count; i++ ) {
		tTJSNI_Window *win = TVPGetWindowListAt(i);
		win->TickBeat();
	}
	if (should_sync_savedata_ && syncfs_is_finished_)
	{
		should_sync_savedata_ = false;
		syncfs_is_finished_ = false;
	}
	if (done)
	{
		if (SDL_WasInit(SDL_INIT_EVENTS) != 0)
		{
			SDL_WaitEvent(NULL);
		}
	}
}

void tTVPApplication::SetTitle( const tjs_string& caption ) {
	title_ = caption;
	if( windows_list_.size() > 0 ) {
		windows_list_[0]->SetCaption( caption );
	}
}

void tTVPApplication::Terminate() {
	if (!tarminate_)
	{
		tarminate_ = true;
#ifdef KRKRSDL2_ENABLE_ASYNC_IMAGE_LOAD
		if (image_load_thread_)
		{
			try
			{
				delete image_load_thread_;
				image_load_thread_ = NULL;
			}
			catch (...)
			{
				// ignore errors
			}
		}
#endif
	}
}

void tTVPApplication::RemoveWindow( TTVPWindowForm* win ) {
	std::vector<class TTVPWindowForm*>::iterator it = std::remove( windows_list_.begin(), windows_list_.end(), win );
	if( it != windows_list_.end() ) {
		windows_list_.erase( it, windows_list_.end() );
	}
}

bool tTVPApplication::GetNotMinimizing() const
{
	return true; // メインがない時は最小化されているとみなす
}

void tTVPApplication::LoadImageRequest( class iTJSDispatch2 *owner, class tTJSNI_Bitmap* bmp, const ttstr &name ) {
#ifdef KRKRSDL2_ENABLE_ASYNC_IMAGE_LOAD
	if( image_load_thread_ ) {
		image_load_thread_->LoadRequest( owner, bmp, name );
	}
#endif
}
