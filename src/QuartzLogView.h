#pragma once

#include "QuartzLogStore.h"

struct ImFont;

void DrawLogWindow(FLogSource& Source);
void SetLogViewMonoFont(ImFont* Font);
bool& LogViewFreshFlashEnabled();
void RegisterLogViewSettings();
