// main.cpp

#define msg_prefix	"DriverHost_PX4"

#include <string>
#include <iostream>
#include <stdexcept>

#include <windows.h>

#include "driver_host.hpp"
#include "config.hpp"
#include "util.hpp"
#include "msg.h"

#ifdef USE_HIGH_RESOLUTION_TIMER
#pragma comment(lib, "winmm.lib")
#endif

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR pCmdLine, _In_ int nCmdShow)
{
	px4::util::path::Init(hInstance);
	SetCurrentDirectoryW(px4::util::path::GetDir().c_str());

#ifdef _DEBUG
	FILE *fp;

	AllocConsole();

	freopen_s(&fp, "CONOUT$", "w", stdout);
	freopen_s(&fp, "CONOUT$", "w", stderr);

	msg_set_mode(MSG_MODE_CONSOLE);
#else
	px4::Config log_config;
	bool log_enabled = false;
	std::wstring log_path = L"DriverHost_PX4.log";

	if (log_config.Load(px4::util::path::GetFileBase() + L".ini", L"DriverHost_PX4")) {
		log_enabled = px4::util::wtob(log_config.Get(L"DebugLog", L"false"));
		log_path = log_config.Get(L"DebugLogPath", log_path);
	}

	if (log_enabled && msg_open_file(log_path.c_str()))
		msg_set_mode(static_cast<enum msg_mode>(MSG_MODE_DEBUGGER | MSG_MODE_LOG_FILE));
	else
		msg_set_mode(MSG_MODE_DEBUGGER);
#endif

	msg_info("Start\n");

#ifdef USE_HIGH_RESOLUTION_TIMER
	UINT timer_resolution = 1;
	TIMECAPS tc;

	if (timeGetDevCaps(&tc, sizeof(tc)) == TIMERR_NOERROR)
		timer_resolution = tc.wPeriodMin;

	if (timeBeginPeriod(timer_resolution) != TIMERR_NOERROR)
		timer_resolution = 0;

	msg_info("Timer Resolution: %u\n", timer_resolution);
#endif

	try {
		px4::DriverHost host;

		host.Run();
	} catch (const std::runtime_error & e) {
		msg_err("%s\n", e.what());
		MessageBoxA(nullptr, e.what(), "DriverHost_PX4 (wWinMain)", MB_OK | MB_ICONERROR);
	}

#ifdef USE_HIGH_RESOLUTION_TIMER
	if (timer_resolution)
		timeEndPeriod(timer_resolution);
#endif

	msg_info("Exiting...\n");
	msg_close_file();

#ifdef _DEBUG
	Sleep(2000);
	FreeConsole();
#endif

	return 0;
}
