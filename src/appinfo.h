#pragma once

#define APP_VER_MAJOR 0
#define APP_VER_MINOR 9
#define APP_VER_PATCH 2

#define APP_VERSION_A   "0.9.2"
#define HTTP_PORT       17321
#define IDI_APP         1
#define APP_NAME_A      "iliz MCP player"
#define APP_COMPANY_A   "iliz"
#define APP_DESC_A      "Compact playlist audio player"
#define APP_INTERNAL_A  "iliz_mcp_player"
#define APP_EXE_A       "iliz_mcp_player.exe"
#define APP_COPYRIGHT_A "Copyright (C) 2026 iliz"

#define APP_WIDEN2(s) L##s
#define APP_WIDEN(s)  APP_WIDEN2(s)

#define APP_VERSION APP_WIDEN(APP_VERSION_A)
#define APP_NAME    APP_WIDEN(APP_NAME_A)
#define APP_COMPANY APP_WIDEN(APP_COMPANY_A)
#define APP_DESC    APP_WIDEN(APP_DESC_A)
