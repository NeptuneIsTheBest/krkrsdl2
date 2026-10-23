/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

#ifndef __TVP_SYS_FONT_H__
#define __TVP_SYS_FONT_H__

class tTVPSysFont {

private:
	void InitializeMemDC();

public:
	tTVPSysFont();
	tTVPSysFont( const tTVPFont &font );
	~tTVPSysFont();

	int GetAscentHeight();
	bool Assign( const tTVPSysFont* font );
	bool Assign( const tTVPFont &font );
};

extern void TVPGetAllFontList( std::vector<tjs_string>& list );
extern void TVPGetFontList(std::vector<ttstr> & list, tjs_uint32 flags, const tTVPFont & font );
extern tjs_uint8 TVPGetCharSetFromFaceName( const tjs_char* face );
#endif // __TVP_SYS_FONT_H__
