// chrlauncher
// Copyright (c) 2015-2017 Henry++

#include <windows.h>

#include "main.h"
#include "rapp.h"
#include "routine.h"
#include "unzip.h"

#include "resource.h"

rapp app (APP_NAME, APP_NAME_SHORT, APP_VERSION, APP_COPYRIGHT);

#define CHROMIUM_UPDATE_URL L"http://chromium.woolyss.com/api/v3/?os=windows&bit=%d&type=%s&out=string"
#define BUFFER_SIZE (_R_BUFFER_LENGTH * 4)
#define INET_FLAGS (INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_RESYNCHRONIZE | INTERNET_FLAG_NO_COOKIES)

STATIC_DATA config;

VOID _app_setstatus (HWND hwnd, LPCWSTR text, DWORDLONG v, DWORDLONG t)
{
	// primary part
	_r_status_settext (hwnd, IDC_STATUSBAR, 0, text);

	// second part
	rstring text2;
	UINT percent = 0;

	if (t)
	{
		percent = static_cast<UINT>((double (v) / double (t)) * 100.0);
		text2.Format (L"%s/%s", _r_fmt_size64 (v), _r_fmt_size64 (t));
	}

	SendDlgItemMessage (hwnd, IDC_PROGRESS, PBM_SETPOS, percent, 0);

	_r_status_settext (hwnd, IDC_STATUSBAR, 1, text2);

	if (text)
		app.TraySetInfo (nullptr, _r_fmt (L"%s\r\n%s %d%%", APP_NAME, text, percent));
	else
		app.TraySetInfo (nullptr, APP_NAME);
}

BOOL _app_unpackzip (HWND hwnd, LPCWSTR path)
{
	BOOL result = FALSE;
	ZIPENTRY ze = {0};
	const size_t title_length = wcslen (L"chrome-win32");

	HZIP hz = OpenZip (path, nullptr);

	if (!IsZipHandleU (hz))
	{
		if (!config.is_silent)
			_r_msg (hwnd, MB_ICONSTOP, nullptr, nullptr, I18N (&app, IDS_STATUS_ERROR, 0), L"OpenZip", GetLastError ());
	}
	else
	{
		// count total files
		GetZipItem (hz, -1, &ze);
		INT total_files = ze.index;

		// check archive is right package
		GetZipItem (hz, 0, &ze);

		if (wcsncmp (ze.name, L"chrome-win32", title_length) == 0)
		{
			DWORDLONG total_size = 0;
			DWORDLONG total_read = 0; // this is our progress so far

			// count size of unpacked files
			for (INT i = 1; i < total_files; i++)
			{
				GetZipItem (hz, i, &ze);

				total_size += ze.unc_size;
			}

			rstring fpath;

			CHAR buffer[BUFFER_SIZE] = {0};
			DWORD written = 0;

			for (INT i = 1; i < total_files; i++)
			{
				GetZipItem (hz, i, &ze);

				fpath.Format (L"%s\\%s", config.binary_dir, (ze.name + title_length + 1));

				_app_setstatus (hwnd, I18N (&app, IDS_STATUS_INSTALL, 0), total_read, total_size);

				if ((ze.attr & FILE_ATTRIBUTE_DIRECTORY) != 0)
				{
					_r_fs_mkdir (fpath);
				}
				else
				{
					const HANDLE h = CreateFile (fpath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_FLAG_WRITE_THROUGH, nullptr);

					if (h != INVALID_HANDLE_VALUE)
					{
						DWORD total_read_file = 0;

						for (ZRESULT zr = ZR_MORE; zr == ZR_MORE;)
						{
							DWORD bufsize = BUFFER_SIZE;

							zr = UnzipItem (hz, static_cast<INT>(i), buffer, bufsize);

							if (zr == ZR_OK)
								bufsize = ze.unc_size - total_read_file;

							buffer[bufsize] = 0;

							WriteFile (h, buffer, bufsize, &written, nullptr);

							total_read_file += bufsize;
							total_read += bufsize;
						}

						CloseHandle (h);
					}
				}
			}

			result = TRUE;
		}

		CloseZip (hz);
	}

	return result;
}

VOID _app_cleanup (LPCWSTR version)
{
	WIN32_FIND_DATA wfd = {0};
	const HANDLE h = FindFirstFile (_r_fmt (L"%s\\*.manifest", config.binary_dir), &wfd);

	if (h != INVALID_HANDLE_VALUE)
	{
		const size_t len = wcslen (version);

		do
		{
			if (_wcsnicmp (version, wfd.cFileName, len) != 0)
				_r_fs_delete (_r_fmt (L"%s\\%s", config.binary_dir, wfd.cFileName), FALSE);
		}
		while (FindNextFile (h, &wfd));

		FindClose (h);
	}
}

VOID _app_openbrowser ()
{
	if (!_r_fs_exists (config.binary_path))
		return;

	if (!config.is_isurlpresent && _r_process_is_exists (config.binary_dir, wcslen (config.binary_dir)))
		return;

	WCHAR args[2048] = {0};

	if (!ExpandEnvironmentStrings (config.args, args, _countof (config.args)))
		StringCchCopy (args, _countof (args), config.args);

	config.is_isurlpresent = FALSE; // reset url state

	rstring arg;
	arg.Format (L"\"%s\" %s", config.binary_path, args);

	_r_run (config.binary_path, arg, config.binary_dir);
}

BOOL _app_installupdate (HWND hwnd, LPCWSTR version, LPCWSTR path)
{
	BOOL result = FALSE;

	if (!_r_fs_exists (config.binary_dir))
		_r_fs_mkdir (config.binary_dir);

	result = _app_unpackzip (hwnd, path);

	if (result)
	{
		_r_fs_delete (path, FALSE); // remove cache file
		_app_cleanup (version);
	}

	_app_setstatus (hwnd, nullptr, 0, 0);

	return result;
}

BOOL _app_downloadupdate (HWND hwnd, LPCWSTR url, LPCWSTR path)
{
	BOOL result = FALSE;

	HINTERNET internet = nullptr;
	HINTERNET connect = nullptr;

	internet = InternetOpen (app.GetUserAgent (), INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);

	if (!internet)
	{
		if (!config.is_silent)
			_r_msg (hwnd, MB_ICONSTOP, nullptr, nullptr, I18N (&app, IDS_STATUS_ERROR, 0), L"InternetOpen", GetLastError ());
	}
	else
	{
		connect = InternetOpenUrl (internet, url, nullptr, 0, INET_FLAGS, 0);

		if (!connect)
		{
			if (!config.is_silent)
				_r_msg (hwnd, MB_ICONSTOP, nullptr, nullptr, I18N (&app, IDS_STATUS_ERROR, 0), L"InternetOpenUrl", GetLastError ());
		}
		else
		{
			DWORD status = 0, size = sizeof (status);
			HttpQueryInfo (connect, HTTP_QUERY_FLAG_NUMBER | HTTP_QUERY_STATUS_CODE, &status, &size, nullptr);

			if (status == HTTP_STATUS_OK)
			{
				DWORD total_size = 0;
				size = sizeof (total_size);

				HttpQueryInfo (connect, HTTP_QUERY_FLAG_NUMBER | HTTP_QUERY_CONTENT_LENGTH, &total_size, &size, nullptr);

				WCHAR temp_fn[512] = {0};
				StringCchPrintf (temp_fn, _countof (temp_fn), L"%s.tmp", path);

				HANDLE f = CreateFile (temp_fn, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_FLAG_WRITE_THROUGH, nullptr);

				if (f != INVALID_HANDLE_VALUE)
				{
					CHAR buffera[BUFFER_SIZE] = {0};
					DWORD out = 0, written = 0, total_written = 0, retn = 0;

					while ((retn = WaitForSingleObjectEx (config.stop_evt, 0, FALSE)) != WAIT_OBJECT_0)
					{
						if (!InternetReadFile (connect, buffera, BUFFER_SIZE - 1, &out) || !out)
							break;

						buffera[out] = 0;
						WriteFile (f, buffera, out, &written, nullptr);

						total_written += out;

						_app_setstatus (hwnd, I18N (&app, IDS_STATUS_DOWNLOAD, 0), total_written, total_size);
					}

					CloseHandle (f);

					if (retn != WAIT_OBJECT_0)
					{
						_r_fs_ren (temp_fn, path);
						result = TRUE;
					}
				}
			}

			InternetCloseHandle (connect);
		}

		InternetCloseHandle (internet);
	}

	_app_setstatus (hwnd, nullptr, 0, 0);

	return result;
}

UINT WINAPI _app_thread (LPVOID lparam)
{
	config.is_isdownloading = TRUE;

	const HWND hwnd = (HWND)lparam;
	BOOL is_installed = FALSE;

	if (!is_installed)
	{
		_app_openbrowser ();

		if (_app_downloadupdate (hwnd, config.new_url, config.cache_path))
		{
			if (!_r_process_is_exists (config.binary_dir, wcslen (config.binary_dir)))
				is_installed = _app_installupdate (hwnd, config.last_version, config.cache_path);
		}
	}

	if (is_installed)
		app.ConfigSet (L"ChromiumLastCheck", _r_unixtime_now ());

	config.is_isdownloading = FALSE;

	SetEvent (config.end_evt);

	PostMessage (hwnd, WM_DESTROY, 0, 0);

	return ERROR_SUCCESS;
}

VOID _app_downloadandinstall (HWND hwnd)
{
	if (!config.is_isdownloading)
		config.thread = (HANDLE)_beginthreadex (nullptr, 0, &_app_thread, hwnd, 0, nullptr);

	_r_ctrl_enable (hwnd, IDC_START_BTN, FALSE);
}

BOOL _app_checkupdate (HWND hwnd)
{
	HINTERNET internet = nullptr;
	HINTERNET hurl = nullptr;

	rstring::map_one result;

	const INT days = app.ConfigGet (L"ChromiumCheckPeriod", 1).AsInt ();
	const BOOL is_exists = _r_fs_exists (config.binary_path);

	if (days == -1)
		config.is_forcecheck = TRUE; // there is .ini option to force update checking

	if (config.is_forcecheck || (days || !is_exists))
	{
		if (config.is_forcecheck || !is_exists || (_r_unixtime_now () - app.ConfigGet (L"ChromiumLastCheck", 0).AsLonglong ()) >= (86400 * days))
		{
			internet = InternetOpen (app.GetUserAgent (), INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);

			if (!internet)
			{
				if (!config.is_silent)
					_r_msg (hwnd, MB_ICONSTOP, nullptr, nullptr, I18N (&app, IDS_STATUS_ERROR, 0), L"InternetOpen", GetLastError ());
			}
			else
			{
				hurl = InternetOpenUrl (internet, _r_fmt (CHROMIUM_UPDATE_URL, config.architecture, config.type), nullptr, 0, INET_FLAGS, 0);

				if (!hurl)
				{
					if (!config.is_silent)
						_r_msg (hwnd, MB_ICONSTOP, nullptr, nullptr, I18N (&app, IDS_STATUS_ERROR, 0), L"InternetOpenUrl", GetLastError ());
				}
				else
				{
					DWORD status = 0, size = sizeof (status);
					HttpQueryInfo (hurl, HTTP_QUERY_FLAG_NUMBER | HTTP_QUERY_STATUS_CODE, &status, &size, nullptr);

					if (status == HTTP_STATUS_OK)
					{
						DWORD out = 0;

						CHAR buffera[BUFFER_SIZE] = {0};
						rstring bufferw;

						while (WaitForSingleObjectEx (config.stop_evt, 0, FALSE) != WAIT_OBJECT_0)
						{
							if (!InternetReadFile (hurl, buffera, BUFFER_SIZE - 1, &out) || !out)
								break;

							buffera[out] = 0;
							bufferw.Append (buffera);
						}

						if (!bufferw.IsEmpty ())
						{
							rstring::rvector vc = bufferw.AsVector (L";");

							for (size_t i = 0; i < vc.size (); i++)
							{
								const size_t pos = vc.at (i).Find (L'=');

								if (pos != rstring::npos)
									result[vc.at (i).Midded (0, pos)] = vc.at (i).Midded (pos + 1);
							}
						}
					}

					InternetCloseHandle (hurl);
				}

				InternetCloseHandle (internet);
			}
		}
	}

	if (result.size ())
	{
		if (!is_exists || result[L"version"].CompareNoCase (config.current_version) != 0)
		{
			// show info
			SetDlgItemText (hwnd, IDC_BROWSER, _r_fmt (I18N (&app, IDS_BROWSER, 0), config.name_full));
			SetDlgItemText (hwnd, IDC_CURRENTVERSION, _r_fmt (I18N (&app, IDS_CURRENTVERSION, 0), !config.current_version[0] ? L"<not found>" : config.current_version));
			SetDlgItemText (hwnd, IDC_VERSION, _r_fmt (I18N (&app, IDS_VERSION, 0), result[L"version"]));
			SetDlgItemText (hwnd, IDC_DATE, _r_fmt (I18N (&app, IDS_DATE, 0), _r_fmt_date (result[L"timestamp"].AsLonglong (), FDTF_SHORTDATE | FDTF_SHORTTIME)));

			StringCchCopy (config.last_version, _countof (config.last_version), result[L"version"]);
			StringCchCopy (config.new_url, _countof (config.new_url), result[L"download"]);

			app.ConfigSet (L"ChromiumLastBuild", result[L"timestamp"]);
			app.ConfigSet (L"ChromiumLastVersion", result[L"version"]);

			return TRUE;
		}
	}

	return FALSE;
}

rstring _app_getversion (LPCWSTR path)
{
	rstring result;

	DWORD verHandle = 0;
	const DWORD verSize = GetFileVersionInfoSize (path, &verHandle);

	if (verSize)
	{
		LPSTR verData = new char[verSize];

		if (GetFileVersionInfo (path, verHandle, verSize, verData))
		{
			LPBYTE buffer = nullptr;
			UINT size = 0;

			if (VerQueryValue (verData, L"\\", (VOID FAR* FAR*)&buffer, &size))
			{
				if (size)
				{
					VS_FIXEDFILEINFO const *verInfo = (VS_FIXEDFILEINFO*)buffer;

					if (verInfo->dwSignature == 0xfeef04bd)
					{
						// Doesn't matter if you are on 32 bit or 64 bit,
						// DWORD is always 32 bits, so first two revision numbers
						// come from dwFileVersionMS, last two come from dwFileVersionLS

						result.Format (L"%d.%d.%d.%d", (verInfo->dwFileVersionMS >> 16) & 0xffff, (verInfo->dwFileVersionMS >> 0) & 0xffff, (verInfo->dwFileVersionLS >> 16) & 0xffff, (verInfo->dwFileVersionLS >> 0) & 0xffff);
					}
				}
			}
		}

		delete[] verData;
	}

	return result;
}

BOOL initializer_callback (HWND hwnd, DWORD msg, LPVOID, LPVOID)
{
	switch (msg)
	{
		case _RM_INITIALIZE:
		{
			app.TrayCreate (hwnd, UID, WM_TRAYICON, _r_loadicon (app.GetHINSTANCE (), MAKEINTRESOURCE (IDI_MAIN), GetSystemMetrics (SM_CXSMICON)), TRUE);

			SetCurrentDirectory (app.GetDirectory ());

			// configure paths
			GetTempPath (_countof (config.cache_path), config.cache_path);
			StringCchCat (config.cache_path, _countof (config.cache_path), APP_NAME_SHORT L"Cache.ZIP");

			StringCchCopy (config.binary_dir, _countof (config.binary_dir), _r_path_expand (app.ConfigGet (L"ChromiumDirectory", L".\\bin")));
			StringCchPrintf (config.binary_path, _countof (config.binary_path), L"%s\\%s", config.binary_dir, app.ConfigGet (L"ChromiumBinary", L"chrome.exe"));

			// get browser architecture...
			if (_r_sys_validversion (5, 1, VER_EQUAL) || _r_sys_validversion (5, 2, VER_EQUAL))
				config.architecture = 32; // on XP only 32-bit supported
			else
				config.architecture = app.ConfigGet (L"ChromiumArchitecture", 0).AsUint ();

			if (!config.architecture || (config.architecture != 64 && config.architecture != 32))
			{
				config.architecture = 0;

				// ...by executable
				if (_r_fs_exists (config.binary_path))
				{
					DWORD exe_type = 0;

					if (GetBinaryType (config.binary_path, &exe_type))
					{
						if (exe_type == SCS_32BIT_BINARY)
							config.architecture = 32;
						else if (exe_type == SCS_64BIT_BINARY)
							config.architecture = 64;
					}
				}

				// ...by processor architecture
				if (!config.architecture)
				{
					SYSTEM_INFO si = {0};
					GetNativeSystemInfo (&si);

					if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
						config.architecture = 64;
					else
						config.architecture = 32;
				}
			}

			if (!config.architecture)
				config.architecture = 32; // architecture fallback

			// set others
			StringCchCopy (config.type, _countof (config.type), app.ConfigGet (L"ChromiumType", L"dev-codecs-sync"));
			StringCchPrintf (config.name_full, _countof (config.name_full), L"Chromium %d-bit (%s)", config.architecture, config.type);

			StringCchCopy (config.args, _countof (config.args), app.ConfigGet (L"ChromiumCommandLine", L"--user-data-dir=..\\profile --no-default-browser-check --allow-outdated-plugins --disable-component-update"));

			StringCchCopy (config.current_version, _countof (config.current_version), _app_getversion (config.binary_path));
			StringCchCopy (config.last_version, _countof (config.last_version), app.ConfigGet (L"ChromiumLastVersion", nullptr));
			config.last_build = app.ConfigGet (L"ChromiumLastBuild", 0).AsLonglong ();

			// set controls data
			SetDlgItemText (hwnd, IDC_BROWSER, _r_fmt (I18N (&app, IDS_BROWSER, 0), config.name_full));
			SetDlgItemText (hwnd, IDC_CURRENTVERSION, _r_fmt (I18N (&app, IDS_CURRENTVERSION, 0), !config.current_version[0] ? L"<not found>" : config.current_version));
			SetDlgItemText (hwnd, IDC_VERSION, _r_fmt (I18N (&app, IDS_VERSION, 0), config.last_version));
			SetDlgItemText (hwnd, IDC_DATE, _r_fmt (I18N (&app, IDS_DATE, 0), _r_fmt_date (config.last_build, FDTF_SHORTDATE | FDTF_SHORTTIME)));

			// parse command line
			INT numargs = 0;
			LPWSTR* arga = CommandLineToArgvW (GetCommandLine (), &numargs);

			if (arga)
			{
				for (INT i = 1; i < numargs; i++)
				{
					if (_wcsicmp (arga[i], L"/a") == 0)
					{
						config.is_autodownload = TRUE;
					}
					else if (_wcsicmp (arga[i], L"/b") == 0)
					{
						config.is_bringtofront = TRUE;
					}
					else if (_wcsicmp (arga[i], L"/f") == 0)
					{
						config.is_forcecheck = TRUE;
					}
					if (_wcsicmp (arga[i], L"/q") == 0)
					{
						config.is_silent = TRUE;
					}
					else if (arga[i][0] == L'-' && arga[i][1] == L'-')
					{
						// there is Chromium arguments
						StringCchCat (config.args, _countof (config.args), L" ");
						StringCchCat (config.args, _countof (config.args), arga[i]);
					}
					else
					{
						config.is_isurlpresent = TRUE;

						StringCchCat (config.args, _countof (config.args), L" -- \"");
						StringCchCat (config.args, _countof (config.args), arga[i]);
						StringCchCat (config.args, _countof (config.args), L"\"");
					}
				}

				LocalFree (arga);
			}

			// ppapi
			rstring ppapi_path = _r_path_expand (app.ConfigGet (L"FlashPlayerPath", L".\\plugins\\pepflashplayer.dll"));

			if (_r_fs_exists (ppapi_path))
			{
				StringCchCat (config.args, _countof (config.args), L" --ppapi-flash-path=\"");
				StringCchCat (config.args, _countof (config.args), ppapi_path);
				StringCchCat (config.args, _countof (config.args), L"\" --ppapi-flash-version=\"");
				StringCchCat (config.args, _countof (config.args), _app_getversion (ppapi_path));
				StringCchCat (config.args, _countof (config.args), L"\"");
			}

			if (!config.is_silent)
				config.is_silent = app.ConfigGet (L"ChromiumIsSilent", FALSE).AsBool ();

			if (!config.is_autodownload)
				config.is_autodownload = app.ConfigGet (L"ChromiumAutoDownload", FALSE).AsBool ();

			if (!config.is_bringtofront)
				config.is_bringtofront = app.ConfigGet (L"ChromiumBringToFront", FALSE).AsBool ();

			if (!config.stop_evt)
				config.stop_evt = CreateEvent (nullptr, FALSE, FALSE, nullptr);

			if (!config.end_evt)
				config.end_evt = CreateEvent (nullptr, FALSE, FALSE, nullptr);

			if (hwnd)
			{
				BOOL is_installed = FALSE;

				if (_r_fs_exists (config.cache_path) && !_r_process_is_exists (config.binary_dir, wcslen (config.binary_dir)))
				{
					app.TrayToggle (UID, TRUE); // show tray icon

					if (config.is_bringtofront)
						_r_wnd_toggle (hwnd, TRUE); // show window

					is_installed = _app_installupdate (hwnd, config.last_version, config.cache_path);

					if (is_installed)
						app.ConfigSet (L"ChromiumLastCheck", _r_unixtime_now ());
				}

				if (!is_installed && _app_checkupdate (hwnd))
				{
					app.TrayToggle (UID, TRUE); // show tray icon

					if (!_r_fs_exists (config.binary_path) || config.is_autodownload)
					{
						if (config.is_bringtofront)
							_r_wnd_toggle (hwnd, TRUE); // show window

						_app_downloadandinstall (hwnd); // download and install update
					}
					else
					{
						app.TrayPopup (NIIF_INFO, APP_NAME, I18N (&app, IDS_STATUS_FOUND, 0)); // just inform user
					}

					_app_openbrowser ();

					return FALSE;
				}
			}

			DestroyWindow (hwnd);

			break;
		}

		case _RM_ARGUMENTS:
		{
			initializer_callback (nullptr, _RM_INITIALIZE, nullptr, nullptr);
			break;
		}
	}

	return FALSE;
}

INT_PTR CALLBACK DlgProc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg)
	{
		case WM_INITDIALOG:
		{
			// configure statusbar
			INT parts[] = {app.GetDPI (228), -1};
			SendDlgItemMessage (hwnd, IDC_STATUSBAR, SB_SETPARTS, 2, (LPARAM)parts);

			break;
		}

		case WM_CLOSE:
		{
			if (config.is_isdownloading && _r_msg (hwnd, MB_YESNO | MB_ICONQUESTION, APP_NAME, nullptr, I18N (&app, IDS_QUESTION_STOP, 0)) != IDYES)
				return TRUE;

			DestroyWindow (hwnd);

			break;
		}

		case WM_DESTROY:
		{
			_app_openbrowser ();

			app.TrayDestroy (UID);

			SetEvent (config.stop_evt);

			if (config.is_isdownloading)
				WaitForSingleObjectEx (config.end_evt, 2000, FALSE);

			if (config.end_evt)
			{
				CloseHandle (config.end_evt);
				config.end_evt = nullptr;
			}

			if (config.stop_evt)
			{
				CloseHandle (config.stop_evt);
				config.stop_evt = nullptr;
			}

			if (config.thread)
			{
				config.thread = nullptr;
				CloseHandle (config.thread);
			}

			PostQuitMessage (0);

			break;
		}

		case WM_LBUTTONDOWN:
		{
			SendMessage (hwnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
			break;
		}

		case WM_ENTERSIZEMOVE:
		case WM_EXITSIZEMOVE:
		case WM_CAPTURECHANGED:
		{
			LONG_PTR exstyle = GetWindowLongPtr (hwnd, GWL_EXSTYLE);

			if ((exstyle & WS_EX_LAYERED) == 0)
			{
				SetWindowLongPtr (hwnd, GWL_EXSTYLE, exstyle | WS_EX_LAYERED);
			}

			SetLayeredWindowAttributes (hwnd, 0, (msg == WM_ENTERSIZEMOVE) ? 100 : 255, LWA_ALPHA);
			SetCursor (LoadCursor (nullptr, (msg == WM_ENTERSIZEMOVE) ? IDC_SIZEALL : IDC_ARROW));

			break;
		}

		case WM_NOTIFY:
		{
			switch (LPNMHDR (lparam)->code)
			{
				case NM_CLICK:
				case NM_RETURN:
				{
					ShellExecute (nullptr, nullptr, PNMLINK (lparam)->item.szUrl, nullptr, nullptr, SW_SHOWNORMAL);
					break;
				}
			}

			break;
		}

		case WM_TRAYICON:
		{
			switch (LOWORD (lparam))
			{
				case NIN_BALLOONUSERCLICK:
				{
					_r_wnd_toggle (hwnd, FALSE);
					break;
				}

				case WM_LBUTTONUP:
				{
					SetForegroundWindow (hwnd);
					break;
				}

				case WM_LBUTTONDBLCLK:
				{
					_r_wnd_toggle (hwnd, FALSE);
					break;
				}

				case WM_RBUTTONUP:
				{
					SetForegroundWindow (hwnd); // don't touch

					HMENU menu = LoadMenu (nullptr, MAKEINTRESOURCE (IDM_TRAY)), submenu = GetSubMenu (menu, 0);

					if (config.is_isdownloading)
						EnableMenuItem (submenu, IDM_TRAY_START, MF_BYCOMMAND | MF_DISABLED);

					POINT pt = {0};
					GetCursorPos (&pt);

					TrackPopupMenuEx (submenu, TPM_RIGHTBUTTON | TPM_LEFTBUTTON, pt.x, pt.y, hwnd, nullptr);

					DestroyMenu (submenu);
					DestroyMenu (menu);

					break;
				}
			}

			break;
		}

		case WM_COMMAND:
		{
			switch (LOWORD (wparam))
			{
				case IDCANCEL: // process Esc key
				case IDM_TRAY_SHOW:
				{
					_r_wnd_toggle (hwnd, FALSE);
					break;
				}

				case IDM_EXIT:
				case IDM_TRAY_EXIT:
				{
					SendMessage (hwnd, WM_CLOSE, 0, 0);
					break;
				}

				case IDM_WEBSITE:
				case IDM_TRAY_WEBSITE:
				{
					ShellExecute (hwnd, nullptr, _APP_WEBSITE_URL, nullptr, nullptr, SW_SHOWDEFAULT);
					break;
				}

				case IDM_DONATE:
				{
					ShellExecute (hwnd, nullptr, _APP_DONATION_URL, nullptr, nullptr, SW_SHOWDEFAULT);
					break;
				}

				case IDM_ABOUT:
				case IDM_TRAY_ABOUT:
				{
					app.CreateAboutWindow ();
					break;
				}

				case IDC_START_BTN:
				case IDM_TRAY_START:
				{
					_r_wnd_toggle (hwnd, TRUE);
					_app_downloadandinstall (hwnd);

					break;
				}
			}

			break;
		}
	}

	return FALSE;
}

INT APIENTRY wWinMain (HINSTANCE, HINSTANCE, LPWSTR, INT)
{
	if (app.CreateMainWindow (&DlgProc, &initializer_callback))
	{
		MSG msg = {0};

		while (GetMessage (&msg, nullptr, 0, 0))
		{
			if (!IsDialogMessage (app.GetHWND (), &msg))
			{
				TranslateMessage (&msg);
				DispatchMessage (&msg);
			}
		}
	}

	return ERROR_SUCCESS;
}
