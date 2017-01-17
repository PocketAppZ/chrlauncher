// chrlauncher
// Copyright (c) 2015-2017 Henry++

#ifndef __MAIN_H__
#define __MAIN_H__

#include <windows.h>
#include "resource.h"
#include "rconfig.h"

// config
#define APP_NAME L"chrlauncher"
#define APP_NAME_SHORT L"chrlauncher"
#define APP_VERSION L"2.1"
#define APP_VERSION_RES 2,1,0,0
#define APP_COPYRIGHT L"(c) 2015-2017 " _APP_AUTHOR L". All Rights Reserved."

#define WM_TRAYICON WM_APP + 1
#define UID 2008

// libs
#pragma comment(lib, "version.lib")
#pragma comment(lib, "ws2_32.lib")

struct STATIC_DATA
{
	HANDLE thread = nullptr;
	HANDLE end_evt = nullptr;
	HANDLE stop_evt = nullptr;

	BOOL is_silent = FALSE;
	BOOL is_forcecheck = FALSE;
	BOOL is_autodownload = FALSE;
	BOOL is_bringtofront = FALSE;
	BOOL is_isurlpresent = FALSE;
	BOOL is_isdownloading = FALSE;

	UINT architecture = 0;

	__time64_t last_build = 0;
	WCHAR last_version[32] = {0};

	WCHAR new_url[512] = {0};
	WCHAR current_version[32] = {0};
	WCHAR name[64] = {0};
	WCHAR name_full[64] = {0};
	WCHAR type[64] = {0};

	WCHAR cache_path[512] = {0};
	WCHAR binary_dir[512] = {0};
	WCHAR binary_path[512] = {0};

	WCHAR args[1024] = {0};
};

#endif // __MAIN_H__
