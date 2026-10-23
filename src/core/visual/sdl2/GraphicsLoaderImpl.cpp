//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Graphics Loader ( loads graphic format from storage )
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "GraphicsLoaderImpl.h"
#include "GraphicsLoaderIntf.h"
#include "tjsHashSearch.h"
#include "StorageImpl.h"
#include "MsgIntf.h"
#include "tjsUtils.h"
#include "SysInitIntf.h"
#include "DebugIntf.h"

#include "StorageImpl.h"
#include "LayerBitmapIntf.h"
#include "MsgIntf.h"

#include "CharacterSet.h"

#if 0
// move to intf
void tTVPGraphicHandlerType::Load( void* formatdata, void *callbackdata, tTVPGraphicSizeCallback sizecallback, tTVPGraphicScanLineCallback scanlinecallback,
	tTVPMetaInfoPushCallback metainfopushcallback, tTJSBinaryStream *src, tjs_int32 keyidx, tTVPGraphicLoadMode mode)
{
	if( LoadHandler == NULL ) TVPThrowExceptionMessage(TVPUnknownGraphicFormat, TJS_W("unknown"));

	if( IsPlugin )
	{
		tTVPIStreamAdapter *istream = new tTVPIStreamAdapter(src);
		try {
			LoadHandlerPlugin( formatdata, callbackdata, sizecallback, scanlinecallback, metainfopushcallback,
				istream, keyidx, mode);
		} catch(...) {
			istream->ClearStream();
			istream->Release();
			throw;
		}
		istream->ClearStream();
		istream->Release();
	}
	else
	{
		LoadHandler( formatdata, callbackdata, sizecallback, scanlinecallback, metainfopushcallback,
			src, keyidx, mode);
	}
}
void tTVPGraphicHandlerType::Save( const ttstr & storagename, const ttstr & mode, const tTVPBaseBitmap* image, iTJSDispatch2* meta )
{
	if( SaveHandler == NULL ) TVPThrowExceptionMessage(TVPUnknownGraphicFormat, mode );

	tTJSBinaryStream *stream = TVPCreateStream(TVPNormalizeStorageName(storagename), TJS_BS_WRITE);
	if( IsPlugin )
	{
		tTVPIStreamAdapter *istream = new tTVPIStreamAdapter(stream);
		try {
			tjs_uint h = image->GetHeight();
			tjs_uint w = image->GetWidth();
			SaveHandlerPlugin( FormatData, (void*)image, istream, mode, w, h, tTVPBitmapScanLineCallbackForSave, meta );
		} catch(...) {
			istream->Release();
			throw;
		}
		istream->Release();
	}
	else
	{
		try {
			SaveHandler( FormatData, stream, image, mode, meta );
		} catch(...) {
			delete stream;
			throw;
		}
		delete stream;
	}
}
void tTVPGraphicHandlerType::Header( tTJSBinaryStream *src, iTJSDispatch2** dic )
{
	if( HeaderHandler == NULL ) TVPThrowExceptionMessage(TVPUnknownGraphicFormat, TJS_W("unknown") );

	if( IsPlugin )
	{
		tTVPIStreamAdapter *istream = new tTVPIStreamAdapter(src);
		try {
			HeaderHandlerPlugin( FormatData, istream, dic );
		} catch(...) {
			istream->ClearStream();
			istream->Release();
			throw;
		}
		istream->ClearStream();
		istream->Release();
	}
	else
	{
		HeaderHandler( FormatData, src, dic );
	}
}
#endif

bool TVPLoadEmscriptenPreloadedData(void* formatdata, void *callbackdata, tTVPGraphicSizeCallback sizecallback,
	tTVPGraphicScanLineCallback scanlinecallback, tTVPMetaInfoPushCallback metainfopushcallback,
	const ttstr &name, tjs_int keyidx, tTVPGraphicLoadMode mode)
{
	// Required by the shared krkrz graphics loader. macOS always uses its
	// normal image decoder; there is no browser preload path.
	return false;
}

