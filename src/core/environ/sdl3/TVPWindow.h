/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

#ifndef __TVP_WINDOW_H__
#define __TVP_WINDOW_H__

#include <string>

enum {
	 ssShift = TVP_SS_SHIFT,
	 ssAlt = TVP_SS_ALT,
	 ssCtrl = TVP_SS_CTRL,
	 ssLeft = TVP_SS_LEFT,
	 ssRight = TVP_SS_RIGHT,
	 ssMiddle = TVP_SS_MIDDLE,
	 ssDouble = TVP_SS_DOUBLE,
	 ssRepeat = TVP_SS_REPEAT,
	 ssX1 = TVP_SS_X1,
	 ssX2 = TVP_SS_X2,
};

enum {
	orientUnknown,
	orientPortrait,
	orientLandscape,
};

typedef tjs_uint32 TShiftState;
extern tjs_uint32 TVP_TShiftState_To_uint32(TShiftState state);
extern TShiftState TVP_TShiftState_From_uint32(tjs_uint32 state);

extern void sdl_process_events();
class TVPSDLBitmapCompletion;
class tTVPBaseBitmap;
#ifdef KRKRZ_ENABLE_CANVAS
class tTVPOpenGLScreen;
#endif

class TTVPWindowForm {
public:
	virtual ~TTVPWindowForm() {};
	virtual void SetPaintBoxSize(tjs_int w, tjs_int h) = 0;
	virtual bool GetFormEnabled() = 0;
	virtual void SetDefaultMouseCursor() = 0;
	virtual void SetMouseCursor(tjs_int handle) = 0;
	virtual void SetMouseCursorState(tTVPMouseCursorState mcs) = 0;
	virtual tTVPMouseCursorState GetMouseCursorState() const = 0;
	virtual void HideMouseCursor() = 0;
	virtual void GetCursorPos(tjs_int &x, tjs_int &y) = 0;
	virtual void SetCursorPos(tjs_int x, tjs_int y) = 0;
	virtual void SetAttentionPoint(tjs_int left, tjs_int top, const struct tTVPFont * font) = 0;
	virtual void BringToFront() = 0;
	virtual void ShowWindowAsModal() = 0;
	virtual bool GetVisible() = 0;
	virtual void SetVisible(bool visible) = 0;
	virtual void SetFullScreenMode(bool fullscreen) = 0;
	virtual bool GetFullScreenMode() = 0;
	virtual void SetBorderStyle(tTVPBorderStyle) = 0;
	virtual tTVPBorderStyle GetBorderStyle() const = 0;
	virtual tjs_string GetCaption() = 0;
	virtual void GetCaption(tjs_string & v) const = 0;
	virtual void SetCaption(const tjs_string & v) = 0;
	virtual void SetWidth(tjs_int w) = 0;
	virtual void SetHeight(tjs_int h) = 0;
	virtual void SetSize(tjs_int w, tjs_int h) = 0;
	virtual void GetSize(tjs_int &w, tjs_int &h) = 0;
	virtual tjs_int GetWidth() const = 0;
	virtual tjs_int GetHeight() const = 0;
	virtual void SetMinWidth(tjs_int w) = 0;
	virtual void SetMaxWidth(tjs_int w) = 0;
	virtual void SetMinHeight(tjs_int h) = 0;
	virtual void SetMaxHeight(tjs_int h) = 0;
	virtual void SetMinSize(tjs_int w, tjs_int h) = 0;
	virtual void SetMaxSize(tjs_int w, tjs_int h) = 0;
	virtual tjs_int GetMinWidth() = 0;
	virtual tjs_int GetMaxWidth() = 0;
	virtual tjs_int GetMinHeight() = 0;
	virtual tjs_int GetMaxHeight() = 0;
	virtual tjs_int GetLeft() = 0;
	virtual void SetLeft(tjs_int l) = 0;
	virtual tjs_int GetTop() = 0;
	virtual void SetTop(tjs_int l) = 0;
	virtual void SetPosition(tjs_int l, tjs_int t) = 0;
	virtual TVPSDLBitmapCompletion *GetTVPSDLBitmapCompletion() = 0;
#ifdef KRKRZ_ENABLE_CANVAS
	virtual void SetOpenGLScreen(tTVPOpenGLScreen *s) = 0;
	virtual void SetSwapInterval(int interval) = 0;
	virtual void GetDrawableSize(tjs_int &w, tjs_int &h) = 0;
	virtual void Swap() = 0;
#endif
	virtual void Show() = 0;
	virtual void TickBeat() = 0;
	virtual void InvalidateClose() = 0;
	virtual bool GetWindowActive() = 0;
	virtual void Close() = 0;
	virtual void OnCloseQueryCalled(bool b) = 0;
	virtual void SetImeMode(tTVPImeMode mode) = 0;
	virtual void ResetImeMode() = 0;
	virtual void UpdateWindow(tTVPUpdateType type) = 0;
	virtual void InternalKeyDown(tjs_uint16 key, tjs_uint32 shift) = 0;
	virtual void OnKeyUp(tjs_uint16 vk, int shift) = 0;
	virtual void OnKeyPress(tjs_uint16 vk, int repeat, bool prevkeystate, bool convertkey) = 0;
	virtual void SetZoom(tjs_int numer, tjs_int denom, bool set_logical = true) = 0;
	virtual void SetZoomNumer(tjs_int n) = 0;
	virtual tjs_int GetZoomNumer() const = 0;
	virtual void SetZoomDenom(tjs_int d) = 0;
	virtual tjs_int GetZoomDenom() const = 0;
	virtual void SetInnerWidth(tjs_int v) = 0;
	virtual void SetInnerHeight(tjs_int v) = 0;
	virtual void SetInnerSize(tjs_int w, tjs_int h) = 0;
	virtual tjs_int GetInnerWidth() = 0;
	virtual tjs_int GetInnerHeight() = 0;

	void SetVisibleFromScript(bool b) { SetVisible(b); };
	virtual void SetStayOnTop(bool b) = 0;
	virtual bool GetStayOnTop() const = 0;
	virtual void SetTrapKey(bool b) = 0;
	virtual bool GetTrapKey() const = 0;
	virtual void SetMaskRegion(tTVPBaseBitmap *bitmap, tjs_int threshold) = 0;
	virtual void RemoveMaskRegion() = 0;
	virtual void SetFocusable(bool b) = 0;
	virtual bool GetFocusable() const = 0;
	int GetDisplayRotate() { return 0; }
	int GetDisplayOrientation() { return orientLandscape; }
	virtual void SetEnableTouch(bool b) = 0;
	virtual bool GetEnableTouch() const = 0;
	virtual void SetHintDelay(tjs_int delay) = 0;
	virtual tjs_int GetHintDelay() const = 0;
	virtual void SetDefaultImeMode(tTVPImeMode mode, bool reset) = 0;
	virtual tTVPImeMode GetDefaultImeMode() const = 0;
	virtual void SetUseMouseKey(bool b) = 0;
	virtual bool GetUseMouseKey() const = 0;
	virtual void ReleaseMouseCapture() = 0;
	virtual void ResetMouseVelocity() = 0;
	virtual void ResetTouchVelocity(tjs_int id) = 0;
	virtual bool GetMouseVelocity(float& x, float& y, float& speed) const = 0;
	virtual void ZoomRectangle(tjs_int & left, tjs_int & top, tjs_int & right, tjs_int & bottom) = 0;
	virtual void SetHintText(iTJSDispatch2* sender, const ttstr &text) = 0;
	virtual void DisableAttentionPoint() = 0;
	virtual void GetVideoOffset(tjs_int &ofsx, tjs_int &ofsy) = 0;
	virtual void SetTouchScaleThreshold(double threshold) = 0;
	virtual double GetTouchScaleThreshold() const = 0;
	virtual void SetTouchRotateThreshold(double threshold) = 0;
	virtual double GetTouchRotateThreshold() const = 0;
	virtual tjs_real GetTouchPointStartX(tjs_int index) const = 0;
	virtual tjs_real GetTouchPointStartY(tjs_int index) const = 0;
	virtual tjs_real GetTouchPointX(tjs_int index) const = 0;
	virtual tjs_real GetTouchPointY(tjs_int index) const = 0;
	virtual tjs_int GetTouchPointID(tjs_int index) const = 0;
	virtual tjs_int GetTouchPointCount() const = 0;
	virtual bool GetTouchVelocity(tjs_int id, float& x, float& y, float& speed) const = 0;
	virtual void SetWaitVSync(bool enabled) = 0;
	virtual void ResetDrawDevice() = 0;
#ifdef USE_OBSOLETE_FUNCTIONS
	void SetInnerSunken(bool b) {}
	bool GetInnerSunken() const { return false; }
	void BeginMove() {}
	void SetLayerLeft(tjs_int l) {}
	tjs_int GetLayerLeft() const { return 0; }
	void SetLayerTop(tjs_int t) {}
	tjs_int GetLayerTop() const { return 0; }
	void SetLayerPosition(tjs_int l, tjs_int t) {}
	void SetShowScrollBars(bool b) {}
	bool GetShowScrollBars() const { return true; }
#endif
};

#endif // __TVP_WINDOW_H__
