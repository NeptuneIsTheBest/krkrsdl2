/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

#ifndef __T_APPLICATION_H__
#define __T_APPLICATION_H__

#include "tjsVariant.h"
#include "tjsString.h"
#include "CharacterSet.h"
#include <vector>
#include <functional>
#include <tuple>
#include <map>
#include <stack>
#include <SDL.h>

tjs_string ExePath();

// 見通しのよい方法に変更した方が良い
extern int _argc;
extern tjs_char ** _wargv;

enum {
	mrOk,
	mrAbort,
	mrCancel,
};

enum
{
	mtWarning = SDL_MESSAGEBOX_WARNING,
	mtError = SDL_MESSAGEBOX_ERROR,
	mtInformation = SDL_MESSAGEBOX_INFORMATION,
	mtConfirmation = SDL_MESSAGEBOX_INFORMATION,
	mtStop = SDL_MESSAGEBOX_ERROR,
	mtCustom = 0
};
enum
{
	mbOK = 0,
};

enum class eTVPActiveEvent
{
	onActive,
	onDeactive,
};
class tTVPApplication {
	std::vector<class TTVPWindowForm*> windows_list_;
	tjs_string title_;

	bool is_attach_console_;
	tjs_string console_title_;
	bool tarminate_;
	bool application_activating_;
	bool has_map_report_process_;

	bool should_sync_savedata_;
	bool syncfs_is_finished_;

#ifdef KRKRSDL2_ENABLE_ASYNC_IMAGE_LOAD
	class tTVPAsyncImageLoader *image_load_thread_;
#else
	void *image_load_thread_;
#endif

	std::vector<char> console_cache_;

private:
	void CheckConsole();
	void ShowException( const tjs_char* e );

public:
	tTVPApplication();
	~tTVPApplication();
	bool StartApplication( int argc, tjs_char* argv[] );
	void Run();

	void PrintConsole( const tjs_char* mes, unsigned long len, bool iserror = false );
	bool IsAttachConsole() { return is_attach_console_; }

	bool IsTarminate() const { return tarminate_; }

	void BringToFront();

	void AddWindow( class TTVPWindowForm* win ) {
		windows_list_.push_back( win );
	}
	void RemoveWindow( class TTVPWindowForm* win );
	unsigned int GetWindowCount() const {
		return (unsigned int)windows_list_.size();
	}

	tjs_string GetTitle() const { return title_; }
	void SetTitle( const tjs_string& caption );

	static inline int MessageDlg( const tjs_string& string, const tjs_string& caption, int type, int button ) {
		tjs_string s_utf16 = string;
		std::string s_utf8;
		tjs_string c_utf16 = caption;
		std::string c_utf8;
		if (TVPUtf16ToUtf8(s_utf8, s_utf16) && TVPUtf16ToUtf8(c_utf8, c_utf16))
		{
			return SDL_ShowSimpleMessageBox(type, c_utf8.c_str(), s_utf8.c_str(), nullptr);
		}
		return 0;
	}
	void Terminate();

	int ArgC;
	tjs_char ** ArgV;

	bool GetActivating() const { return application_activating_; }
	bool GetNotMinimizing() const;

	/**
	 * 画像の非同期読込み要求
	 */
	void LoadImageRequest( class iTJSDispatch2 *owner, class tTJSNI_Bitmap* bmp, const ttstr &name );

	void SyncSavedata()
	{
		should_sync_savedata_ = true;
	}

	void FinishedSyncSavedata()
	{
		syncfs_is_finished_ = true;
	}
};
extern class tTVPApplication* Application;


#endif // __T_APPLICATION_H__
