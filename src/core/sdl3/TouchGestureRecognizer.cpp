/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

#include "TouchGestureRecognizer.h"

#include <cmath>

namespace krkrsdl3
{

namespace
{

constexpr float kMinimumDistance = 0.000001f;
constexpr float kPi = 3.14159265358979323846f;

float NormalizeAngle(float angle)
{
	while (angle > kPi)
	{
		angle -= 2.0f * kPi;
	}
	while (angle < -kPi)
	{
		angle += 2.0f * kPi;
	}
	return angle;
}

} // namespace

TouchGestureRecognizer::Delta TouchGestureRecognizer::Update(const Event &event)
{
	Delta delta;
	auto touch = touches_.find(event.touchId);

	if (event.type == EventType::Up || event.type == EventType::Canceled)
	{
		if (touch == touches_.end())
		{
			return delta;
		}
		touch->second.points.erase(event.fingerId);
	}
	else
	{
		if (touch == touches_.end())
		{
			touch = touches_.emplace(event.touchId, TouchState()).first;
		}

		auto point = touch->second.points.find(event.fingerId);
		if (event.type == EventType::Down || point == touch->second.points.end())
		{
			touch->second.points[event.fingerId] = {event.x, event.y, nextOrder_++};
		}
		else
		{
			point->second.x = event.x;
			point->second.y = event.y;
		}
	}

	TouchState &state = touch->second;
	if (state.points.size() < 2)
	{
		state.gesture = Gesture();
		if (state.points.empty())
		{
			touches_.erase(touch);
		}
		return delta;
	}

	auto first = state.points.end();
	auto second = state.points.end();
	for (auto point = state.points.begin(); point != state.points.end(); ++point)
	{
		if (first == state.points.end() || point->second.order < first->second.order)
		{
			second = first;
			first = point;
		}
		else if (second == state.points.end() || point->second.order < second->second.order)
		{
			second = point;
		}
	}

	const float dx = second->second.x - first->second.x;
	const float dy = second->second.y - first->second.y;
	const float centerX = (first->second.x + second->second.x) * 0.5f;
	const float centerY = (first->second.y + second->second.y) * 0.5f;
	const float distance = std::sqrt(dx * dx + dy * dy);
	const float angle = std::atan2(dy, dx);
	Gesture &gesture = state.gesture;

	if (!gesture.valid || gesture.first != first->first || gesture.second != second->first)
	{
		gesture.valid = true;
		gesture.first = first->first;
		gesture.second = second->first;
		gesture.centerX = centerX;
		gesture.centerY = centerY;
		gesture.distance = distance;
		gesture.angle = angle;
		return delta;
	}

	if (event.type != EventType::Motion ||
		(event.fingerId != gesture.first && event.fingerId != gesture.second))
	{
		return delta;
	}

	delta.valid = true;
	delta.scale = gesture.distance > kMinimumDistance ? distance / gesture.distance - 1.0f : 0.0f;
	delta.rotation = NormalizeAngle(angle - gesture.angle);
	delta.centerX = centerX;
	delta.centerY = centerY;

	gesture.centerX = centerX;
	gesture.centerY = centerY;
	gesture.distance = distance;
	gesture.angle = angle;
	return delta;
}

void TouchGestureRecognizer::Reset()
{
	touches_.clear();
	nextOrder_ = 0;
}

} // namespace krkrsdl3
