#pragma once

#include <windows.h>
#include <cstdint>
#include <vcclr.h>

#include <mirror.h>
#include <tools/BinarySerializer.h>

typedef void* HANDLE;
class TrainingApplication;

ref class TrainingThreadHelper
{
public:
	void watchFrameThreadMain();

	System::Threading::Thread^ watchFrameThread;
	TrainingApplication* application;
};

enum class DockingMode : int
{
	Undocked,
	Left,
	Right,
};

struct TrainingApplicationData
{
	bool autoAttach = true;
	bool showMemoryDebugger = false;
	bool showMemoryMap = false;
	DockingMode dockingMode = DockingMode::Undocked;
	int windowX = 100;
	int windowY = 100;
	int windowW = 400;
	int windowH = 700;

	MIRROR_CLASS(TrainingApplicationData)
	(
		MIRROR_MEMBER(autoAttach)
		MIRROR_MEMBER(showMemoryDebugger)
		MIRROR_MEMBER(showMemoryMap)
		MIRROR_MEMBER(dockingMode)
		MIRROR_MEMBER(windowX)
		MIRROR_MEMBER(windowY)
		MIRROR_MEMBER(windowW)
		MIRROR_MEMBER(windowH)
	)
};

class TrainingApplication
{
public:
	void initialize(HWND _windowHandle);
	void shutdown();
	void update();

	void watchFrameChange();
	void onFrameBegin();

private:

	bool _isAttachedToFBA();
	void _attachToFBA();
	void _dettachFromFBA();

	void _writeByte(size_t _address, char _byte);
	char _readByte(size_t _address);
	long long unsigned int _readUnsignedInt(size_t _address, size_t _size);
	void _incrementDebugAddress(int64_t _increment);

	bool _findFBAProcessHandle();

	void _saveApplicationData();
	void _loadApplicationData();

	HWND m_windowHandle = nullptr;

	bool m_isDettachRequested = false;
	gcroot<System::Diagnostics::Process^> m_FBAProcess = nullptr;
	HANDLE m_FBAProcessHandle = nullptr;
	size_t m_ramStartingAddress = 0;

	uint32_t m_currentFrame = 0;

	bool m_showDemoWindow = false;

	size_t m_debugAddress = 0;

	gcroot<TrainingThreadHelper^> m_threads;
	gcroot<System::Threading::SpinLock^> m_lock;

	mirror::BinarySerializer m_dataSerializer;


	TrainingApplicationData m_applicationData;

	char lol[128] = {};

};
