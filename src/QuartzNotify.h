#pragma once

#include "QuartzTypes.h"

namespace QuartzNotify
{
	enum class ESeverity : uint8
	{
		Info,
		Success,
		Failure,
	};

	// queues a toast
	void Push(const char* Title, const char* Body, ESeverity Severity);

	// draw and age the stack once per frame after the panels
	void Draw();

	// persisted with the rest of the ui settings
	bool& Enabled();
}
