#pragma once

#include "QuartzLogStore.h"

#include <thread>

// splits a raw log line into timestamp, frame, category, level and message
FLogLine ParseLogLine(const char* Text, size_t Length);

// reads existing content then follows growth. signal Source->bStop and join to shut down
std::thread StartTail(FLogSource* Source);
