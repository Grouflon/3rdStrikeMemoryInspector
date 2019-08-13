#pragma once

#include <windows.h>
#include <cstdint>
#include <vcclr.h>

#include <mirror.h>
#include <tools/BinarySerializer.h>

typedef void* HANDLE;
class TrainingApplication;

#define ADDRESS_UNDEFINED ~size_t(0)

enum GameInput
{
	GameInput_Coin,
	GameInput_Start,
	GameInput_Up,
	GameInput_Down,
	GameInput_Left,
	GameInput_Right,
	GameInput_LP,
	GameInput_MP,
	GameInput_HP,
	GameInput_LK,
	GameInput_MK,
	GameInput_HK,
	GameInput_COUNT
};

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
	size_t debugAddress = 0;
	size_t selectionBeginAddress = ~size_t(0);
	size_t selectionEndAddress = ~size_t(0);


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
		MIRROR_MEMBER(debugAddress)
	);
};

struct TrainingModeData
{
	bool enabled = false;
	bool lockTimer = true;
	bool infiniteLife = true;
	bool disableMusic = false;

	MIRROR_CLASS(TrainingModeData)
	(
		MIRROR_MEMBER(enabled)
		MIRROR_MEMBER(lockTimer)
		MIRROR_MEMBER(infiniteLife)
		MIRROR_MEMBER(disableMusic)
	);
};

struct MemoryLabel
{
	std::string name;
	size_t beginAddress = ADDRESS_UNDEFINED;
	size_t endAddress = ADDRESS_UNDEFINED;

	MIRROR_CLASS(MemoryLabel)
	(
		MIRROR_MEMBER(name)
		MIRROR_MEMBER(beginAddress)
		MIRROR_MEMBER(endAddress)
	);
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

	void _updateMemoryDebugger(bool* _showMemoryDebugger);

	void _saveApplicationData();
	void _loadApplicationData();

	void _saveTrainingData();
	void _loadTrainingData();

	void _saveMemoryLabels();
	void _loadMemoryLabels();

	void _calibrateP2InputMapping();

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

	int m_p2Keys[GameInput_COUNT] = {};

	TrainingApplicationData m_applicationData;
	TrainingModeData m_trainingData;
	std::vector<MemoryLabel> m_memoryLabels;

	uint8_t* m_memoryBuffer = nullptr;
	size_t m_memoryBufferSize = 0;

	int m_selectedLabel = -1;
};
