//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// macOS platform extension point for the shared graphics loader.
//
// The shared krkrz implementation includes this header so that individual
// platforms can add loader declarations. The macOS build needs no additional
// declarations. Its shared call site still refers to the historical Web hook,
// so map that hook directly to the normal loader fallback without retaining a
// Web implementation or link-time symbol.
//---------------------------------------------------------------------------

#ifndef GraphicsLoaderImplH
#define GraphicsLoaderImplH

#define TVPLoadEmscriptenPreloadedData(...) false

#endif
