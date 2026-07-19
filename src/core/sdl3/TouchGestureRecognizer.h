/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

#pragma once

#include <cstdint>
#include <map>

namespace krkrsdl3
{

class TouchGestureRecognizer
{
public:
	using TouchId = std::uint64_t;
	using FingerId = std::uint64_t;

	enum class EventType
	{
		Down,
		Motion,
		Up,
		Canceled,
	};

	struct Event
	{
		EventType type;
		TouchId touchId;
		FingerId fingerId;
		float x;
		float y;
	};

	struct Delta
	{
		bool valid = false;
		float scale = 0.0f;
		float rotation = 0.0f;
		float centerX = 0.0f;
		float centerY = 0.0f;
	};

	Delta Update(const Event &event);
	void Reset();

private:
	struct TouchPoint
	{
		float x;
		float y;
		std::uint64_t order;
	};

	struct Gesture
	{
		bool valid = false;
		FingerId first = 0;
		FingerId second = 0;
		float centerX = 0.0f;
		float centerY = 0.0f;
		float distance = 0.0f;
		float angle = 0.0f;
	};

	struct TouchState
	{
		std::map<FingerId, TouchPoint> points;
		Gesture gesture;
	};

	std::map<TouchId, TouchState> touches_;
	std::uint64_t nextOrder_ = 0;
};

} // namespace krkrsdl3
