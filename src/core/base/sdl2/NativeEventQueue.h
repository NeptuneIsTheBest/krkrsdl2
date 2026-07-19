/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

#ifndef __NATIVE_EVENT_QUEUE_H__
#define __NATIVE_EVENT_QUEUE_H__

// 呼び出されるハンドラがシングルスレッドで動作するイベントキュー

class NativeEvent;
class NativeEventQueueIntarface {
public:
	virtual void Dispatch(NativeEvent& event) = 0;
};
class NativeEvent {
public:
 	unsigned int Message;
	intptr_t WParam;
	intptr_t LParam;
	NativeEventQueueIntarface * queue;

	NativeEvent( int mes ) : /*Result(0), HWnd(NULL),*/ Message(mes), WParam(0), LParam(0) {}
	void SetQueue(NativeEventQueueIntarface * tmp_queue)
	{
		queue = tmp_queue;
	}
	void HandleEvent()
	{
		NativeEvent _this = *this;
		queue->Dispatch(_this);
		delete this;
	}
};

class NativeEventQueueImplement : public NativeEventQueueIntarface {

	int CreateUtilWindow();

public:
	static tjs_uint32 native_event_queue_custom_event_type;
	NativeEventQueueImplement();

	// デフォルトハンドラ
	void HandlerDefault( NativeEvent& event ) {}

	// Queue の生成
	void Allocate() {}

	// Queue の削除
	void Deallocate() {}

	void PostEvent( const NativeEvent& event );

	void Dispatch(NativeEvent& event) {}
};


template<typename T>
class NativeEventQueue : public NativeEventQueueImplement {
	void (T::*handler_)(NativeEvent&);
	T* owner_;

public:
	NativeEventQueue( T* owner, void (T::*Handler)(NativeEvent&) ) : owner_(owner), handler_(Handler) {}

	void Dispatch( NativeEvent &ev ) {
		(owner_->*handler_)(ev);
	}

	T* GetOwner() { return owner_; }
};

#endif // __NATIVE_EVENT_QUEUE_H__
