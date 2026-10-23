/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

#define NOMINMAX
#include "tjsCommHead.h"
#include "DrawDevice.h"
#include "BasicDrawDevice.h"
#include "LayerIntf.h"
#include "MsgIntf.h"
#include "SysInitIntf.h"
#include "WindowIntf.h"
#include "DebugIntf.h"
#include "ThreadIntf.h"
#include "ComplexRect.h"
#include "EventIntf.h"
#include "WindowImpl.h"
#include "SDLBitmapCompletion.h"

#if 0
#include <d3d9.h>
#include <mmsystem.h>
#endif
#include <algorithm>
#ifndef ZeroMemory
#define ZeroMemory(p,n) memset(p, 0, n)
#endif

//---------------------------------------------------------------------------
// オプション
//---------------------------------------------------------------------------
static tjs_int TVPBasicDrawDeviceOptionsGeneration = 0;
bool TVPZoomInterpolation = true;
//---------------------------------------------------------------------------
static void TVPInitBasicDrawDeviceOptions()
{
	if(TVPBasicDrawDeviceOptionsGeneration == TVPGetCommandLineArgumentGeneration()) return;
	TVPBasicDrawDeviceOptionsGeneration = TVPGetCommandLineArgumentGeneration();

	tTJSVariant val;
	TVPZoomInterpolation = true;
	if(TVPGetCommandLine(TJS_W("-smoothzoom"), &val))
	{
		ttstr str(val);
		if(str == TJS_W("no"))
			TVPZoomInterpolation = false;
		else
			TVPZoomInterpolation = true;
	}
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
tTVPBasicDrawDevice::tTVPBasicDrawDevice()
{
	TVPInitBasicDrawDeviceOptions(); // read and initialize options
	bitmap_completion = nullptr;
#if 0
	TargetWindow = NULL;
	DrawUpdateRectangle = false;
	BackBufferDirty = true;

	Direct3D = NULL;
	Direct3DDevice = NULL;
	Texture = NULL;
	ShouldShow = false;
	TextureBuffer = NULL;
	TextureWidth = TextureHeight = 0;
	VsyncInterval = 16;
	ZeroMemory( &D3dPP, sizeof(D3dPP) );
	ZeroMemory( &DispMode, sizeof(DispMode) );
#endif
}
//---------------------------------------------------------------------------
tTVPBasicDrawDevice::~tTVPBasicDrawDevice()
{
	DestroyD3DDevice();
}
//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::DestroyD3DDevice() {
#if 0
	DestroyTexture();
	if(Direct3DDevice) Direct3DDevice->Release(), Direct3DDevice = NULL;
	if(Direct3D) Direct3D = NULL;
#endif
}
//---------------------------------------------------------------------------
#if 0
void tTVPBasicDrawDevice::DestroyTexture() {
	if(TextureBuffer && Texture) Texture->UnlockRect(0), TextureBuffer = NULL;
	if(Texture) Texture->Release(), Texture = NULL;
}
#endif
//---------------------------------------------------------------------------
void tTVPBasicDrawDevice::InvalidateAll()
{
	// レイヤ演算結果をすべてリクエストする
	// サーフェースが lost した際に内容を再構築する目的で用いる
	RequestInvalidation(tTVPRect(0, 0, DestRect.get_width(), DestRect.get_height()));
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPBasicDrawDevice::AddLayerManager(iTVPLayerManager * manager)
{
	if(inherited::Managers.size() > 0)
	{
		// "Basic" デバイスでは２つ以上のLayer Managerを登録できない
		TVPThrowExceptionMessage(TVPBasicDrawDeviceDoesNotSupporteLayerManagerMoreThanOne);
	}
	inherited::AddLayerManager(manager);

	manager->SetDesiredLayerType(ltOpaque); // ltOpaque な出力を受け取りたい
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPBasicDrawDevice::SetTargetWindow(HWND wnd, bool is_main)
{
#if 0
	TVPInitBasicDrawDeviceOptions();
	DestroyD3DDevice();
	TargetWindow = wnd;
	IsMainWindow = is_main;
#endif
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPBasicDrawDevice::SetDestRectangle(const tTVPRect & rect)
{
#if 0
	BackBufferDirty = true;
#endif
	// 位置だけの変更の場合かどうかをチェックする
	if(rect.get_width() == DestRect.get_width() && rect.get_height() == DestRect.get_height()) {
		// 位置だけの変更だ
		inherited::SetDestRectangle(rect);
	} else {
		// サイズも違う
#if 0
		if( rect.get_width() > (tjs_int)D3dPP.BackBufferWidth || rect.get_height() > (tjs_int)D3dPP.BackBufferHeight ) {
			// バックバッファサイズよりも大きいサイズが指定された場合一度破棄する。後のEnsureDeviceで再生成される。
			DestroyD3DDevice();
		}
		bool success = true;
#endif
		inherited::SetDestRectangle(rect);

#if 0
		try {
			EnsureDevice();
		} catch(const eTJS & e) {
			TVPAddImportantLog( TVPFormatMessage(TVPBasicDrawDeviceFailedToCreateDirect3DDevices,e.GetMessage() ) );
			success = false;
		} catch(...) {
			TVPAddImportantLog( (const tjs_char*)TVPBasicDrawDeviceFailedToCreateDirect3DDevicesUnknownReason );
			success = false;
		}
		if( success == false ) {
			DestroyD3DDevice();
		}
#endif
	}
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPBasicDrawDevice::NotifyLayerResize(iTVPLayerManager * manager)
{
	inherited::NotifyLayerResize(manager);

#if 0
	BackBufferDirty = true;

	// テクスチャを捨てて作り直す。
	CreateTexture();
#endif
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPBasicDrawDevice::Show()
{
	if (Window)
	{
		TTVPWindowForm *form = ((tTJSNI_Window*)Window)->GetForm();
		if (form)
		{
			form->Show();
		}
	}
}
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPBasicDrawDevice::StartBitmapCompletion(iTVPLayerManager * manager)
{
	if (Window)
	{
		TTVPWindowForm *form = ((tTJSNI_Window*)Window)->GetForm();
		if (form)
		{
			bitmap_completion = form->GetTVPSDLBitmapCompletion();
		}
	}
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPBasicDrawDevice::NotifyBitmapCompleted(iTVPLayerManager * manager,
	tjs_int x, tjs_int y, const void * bits, const class BitmapInfomation * bmpinfo,
	const tTVPRect &cliprect, tTVPLayerType type, tjs_int opacity)
{
	if (bitmap_completion)
	{
		bitmap_completion->NotifyBitmapCompleted(manager, x, y, bits, bmpinfo, cliprect, type, opacity);
	}
#if 0
	const BITMAPINFO *bitmapinfo = bmpinfo->GetBITMAPINFO();

	// bits, bitmapinfo で表されるビットマップの cliprect の領域を、x, y に描画
	// する。
	// opacity と type は無視するしかないので無視する
	tjs_int w, h;
	GetSrcSize( w, h );
	if( TextureBuffer && TargetWindow &&
		!(x < 0 || y < 0 ||
			x + cliprect.get_width() > w ||
			y + cliprect.get_height() > h) &&
		!(cliprect.left < 0 || cliprect.top < 0 ||
			cliprect.right > bitmapinfo->bmiHeader.biWidth ||
			cliprect.bottom > bitmapinfo->bmiHeader.biHeight))
	{
		// 範囲外の転送は(一部だけ転送するのではなくて)無視してよい
		ShouldShow = true;

		// bitmapinfo で表された cliprect の領域を x,y にコピーする
		long src_y       = cliprect.top;
		long src_y_limit = cliprect.bottom;
		long src_x       = cliprect.left;
		long width_bytes   = cliprect.get_width() * 4; // 32bit
		long dest_y      = y;
		long dest_x      = x;
		const tjs_uint8 * src_p = (const tjs_uint8 *)bits;
		long src_pitch;

		if(bitmapinfo->bmiHeader.biHeight < 0)
		{
			// bottom-down
			src_pitch = bitmapinfo->bmiHeader.biWidth * 4;
			//src_pitch = -bitmapinfo->bmiHeader.biWidth * 4;
			//src_p += bitmapinfo->bmiHeader.biWidth * 4 * (bitmapinfo->bmiHeader.biHeight - 1);
		}
		else
		{
			// bottom-up
			src_pitch = -bitmapinfo->bmiHeader.biWidth * 4;
			src_p += bitmapinfo->bmiHeader.biWidth * 4 * (bitmapinfo->bmiHeader.biHeight - 1);
			//src_pitch = bitmapinfo->bmiHeader.biWidth * 4;
		}

		for(; src_y < src_y_limit; src_y ++, dest_y ++)
		{
			const void *srcp = src_p + src_pitch * src_y + src_x * 4;
			void *destp = (tjs_uint8*)TextureBuffer + TexturePitch * dest_y + dest_x * 4;
			memcpy(destp, srcp, width_bytes);
		}
	}
#endif
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPBasicDrawDevice::EndBitmapCompletion(iTVPLayerManager * manager)
{
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPBasicDrawDevice::SetShowUpdateRect(bool b)
{
#if 0
	DrawUpdateRectangle = b;
#endif
}
//---------------------------------------------------------------------------
bool TJS_INTF_METHOD tTVPBasicDrawDevice::SwitchToFullScreen( HWND window, tjs_uint w, tjs_uint h, tjs_uint bpp, tjs_uint color, bool changeresolution )
{
#if 0
	// フルスクリーン化の処理はなにも行わない、互換性のためにウィンドウを全画面化するのみで処理する
	// Direct3D9 でフルスクリーン化するとフォーカスを失うとデバイスをロストするので、そのたびにリセットor作り直しが必要になる。
	// モーダルウィンドウを使用するシステムでは、これは困るので常にウィンドウモードで行う。
	// モーダルウィンドウを使用しないシステムにするのなら、フルスクリーンを使用するDrawDeviceを作ると良い。
	BackBufferDirty = true;
	ShouldShow = true;
	CheckMonitorMoved();
#endif
	return true;
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTVPBasicDrawDevice::RevertFromFullScreen( HWND window, tjs_uint w, tjs_uint h, tjs_uint bpp, tjs_uint color )
{
#if 0
	BackBufferDirty = true;
	ShouldShow = true;
	CheckMonitorMoved();
#endif
}
//---------------------------------------------------------------------------







//---------------------------------------------------------------------------
// tTJSNI_BasicDrawDevice : BasicDrawDevice TJS native class
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_BasicDrawDevice::ClassID = (tjs_uint32)-1;
tTJSNC_BasicDrawDevice::tTJSNC_BasicDrawDevice() :
	tTJSNativeClass(TJS_W("BasicDrawDevice"))
{
	// register native methods/properties

	TJS_BEGIN_NATIVE_MEMBERS(BasicDrawDevice)
	TJS_DECL_EMPTY_FINALIZE_METHOD
//----------------------------------------------------------------------
// constructor/methods
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(/*var.name*/_this, /*var.type*/tTJSNI_BasicDrawDevice,
	/*TJS class name*/BasicDrawDevice)
{
	return TJS_S_OK;
}
TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/BasicDrawDevice)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/recreate)
{
	TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_BasicDrawDevice);
#if 0
	_this->GetDevice()->SetToRecreateDrawer();
	_this->GetDevice()->EnsureDevice();
#endif
	return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/recreate)
//----------------------------------------------------------------------


//---------------------------------------------------------------------------
//----------------------------------------------------------------------
// properties
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(interface)
{
	TJS_BEGIN_NATIVE_PROP_GETTER
	{
		TJS_GET_NATIVE_INSTANCE(/*var. name*/_this, /*var. type*/tTJSNI_BasicDrawDevice);
		*result = reinterpret_cast<tjs_int64>(_this->GetDevice());
		return TJS_S_OK;
	}
	TJS_END_NATIVE_PROP_GETTER

	TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(interface)
//----------------------------------------------------------------------
	TJS_END_NATIVE_MEMBERS
}
//---------------------------------------------------------------------------
iTJSNativeInstance *tTJSNC_BasicDrawDevice::CreateNativeInstance()
{
	return new tTJSNI_BasicDrawDevice();
}
//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
tTJSNI_BasicDrawDevice::tTJSNI_BasicDrawDevice()
{
	Device = new tTVPBasicDrawDevice();
}
//---------------------------------------------------------------------------
tTJSNI_BasicDrawDevice::~tTJSNI_BasicDrawDevice()
{
	if(Device) Device->Destruct(), Device = NULL;
}
//---------------------------------------------------------------------------
tjs_error TJS_INTF_METHOD
	tTJSNI_BasicDrawDevice::Construct(tjs_int numparams, tTJSVariant **param,
		iTJSDispatch2 *tjs_obj)
{
	return TJS_S_OK;
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTJSNI_BasicDrawDevice::Invalidate()
{
	if(Device) Device->Destruct(), Device = NULL;
}
//---------------------------------------------------------------------------

