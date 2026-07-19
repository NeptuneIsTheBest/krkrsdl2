/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

#ifndef __APPLICATION_SPECIAL_PATH_H__
#define __APPLICATION_SPECIAL_PATH_H__

#include "FilePathUtil.h"
#include "StorageIntf.h"
#include "CharacterSet.h"
#include <SDL.h>

class ApplicationSpecialPath {
public:
	static inline tjs_string ReplaceStringAll( tjs_string src, const tjs_string& target, const tjs_string& dest ) {
		tjs_string::size_type nPos = 0;
		while( (nPos = src.find(target, nPos)) != tjs_string::npos ) {
			src.replace( nPos, target.length(), dest );
		}
		return src;
	}

	static inline tjs_string GetConfigFileName( const tjs_string& exename ) {
		return ChangeFileExt(exename, TJS_W(".cf"));
	}
	static tjs_string GetDataPathDirectory( tjs_string datapath, const tjs_string& exename ) {
		if (datapath != TJS_W(""))
		{
			return datapath;
		}
		char *pref_path = SDL_GetPrefPath(NULL, "krkrsdl2");
		std::string pref_path_utf8;
		if (pref_path)
		{
			pref_path_utf8 = pref_path;
			SDL_free(pref_path);
			tjs_string pref_path_utf16;
			TVPUtf8ToUtf16(pref_path_utf16, pref_path_utf8);
			return pref_path_utf16;
		}
		ttstr nativeDataPath = ttstr(TVPGetAppPath().AsStdString());
		if (!nativeDataPath.IsEmpty())
		{
			TVPGetLocalName(nativeDataPath);
		}
		nativeDataPath += TJS_W("/savedata/");
		return nativeDataPath.AsStdString();
	}
	static tjs_string GetUserConfigFileName( const tjs_string& datapath, const tjs_string& exename ) {
		// exepath, personalpath, appdatapath
		return GetDataPathDirectory(datapath, exename) + ExtractFileName(ChangeFileExt(exename, TJS_W(".cfu")));
	}
};


#endif // __APPLICATION_SPECIAL_PATH_H__
