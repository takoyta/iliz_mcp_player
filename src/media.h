#pragma once
#include "app.h"

void MediaKeysInit(HWND wnd);
void MediaKeysShutdown();
void MediaSync();
BOOL MediaCommand(int cmd);
void MediaSmtcButton(WPARAM btn);
