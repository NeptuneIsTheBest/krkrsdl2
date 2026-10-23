//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Graphics Loader ( loads graphic format from storage )
//---------------------------------------------------------------------------

#ifndef GraphicsLoaderImplH
#define GraphicsLoaderImplH

#include "GraphicsLoaderIntf.h"


// Compatibility hook called unconditionally by the upstream graphics loader.
bool TVPLoadEmscriptenPreloadedData(void* formatdata, void *callbackdata, tTVPGraphicSizeCallback sizecallback,
	tTVPGraphicScanLineCallback scanlinecallback, tTVPMetaInfoPushCallback metainfopushcallback,
	const ttstr &name, tjs_int keyidx, tTVPGraphicLoadMode mode);

#endif
