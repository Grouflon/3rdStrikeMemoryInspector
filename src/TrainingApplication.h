#pragma once

#include <windows.h>
#include <cstdint>
#include <vcclr.h>

#include <mirror.h>
#include <tools/BinarySerializer.h>

typedef void* HANDLE;
class TrainingApplication;

#define ADDRESS_UNDEFINED ~size_t(0)

#define SF33_MAXADDRESS 0x02080000

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

struct MemoryDisplayData
{
	size_t address = 0;
	size_t selectionBeginAddress = ~size_t(0);
	size_t selectionEndAddress = ~size_t(0);

	bool isDraggingSelection = false;
	char labelBuffer[128] = {};

	MIRROR_CLASS(MemoryDisplayData)
	(
		MIRROR_MEMBER(address);
	);
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

	MemoryDisplayData memoryDebuggerData;

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
		MIRROR_MEMBER(memoryDebuggerData)
	);
};

struct TrainingModeData
{
	bool enabled = false;
	bool lockTimer = true;
	bool infiniteLife = true;
	bool noStun = true;
	bool disableMusic = false;

	MIRROR_CLASS(TrainingModeData)
	(
		MIRROR_MEMBER(enabled)
		MIRROR_MEMBER(lockTimer)
		MIRROR_MEMBER(infiniteLife)
		MIRROR_MEMBER(noStun)
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

struct GameObjectData
{
	size_t address = 0;

	uint8_t friends = 0;
	int8_t flipX = 0;
	int16_t posX = 0;
	int16_t posY = 0;
	uint16_t charID = 0;
	uint16_t validObject = 0;

	int16_t hitboxes[22][4] = {};
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

	// char array size must be at least _digitCount + 3 (for 0x and \0)
	void _sanitizeAddressString(char* _string, size_t _digitCount) const;

	void _writeByte(size_t _address, uint8_t _byte);
	void _writeData(size_t _address, void* _data, size_t _dataSize);
	void _readData(size_t _address, void* _data, size_t _dataSize, bool _reverse = true);

	int8_t _readInt8(void* _memory, size_t _address);
	uint8_t _readUInt8(void* _memory, size_t _address);
	int16_t _readInt16(void* _memory, size_t _address);
	uint16_t _readUInt16(void* _memory, size_t _address);

	size_t _incrementAddress(size_t _address, int64_t _increment, size_t _minAddress = 0, size_t _maxAddress = SF33_MAXADDRESS);

	void _requestMemoryWrite(size_t _address, void* _data, size_t _dataSize, bool _reverse = true);
	void _resolveMemoryWriteRequests();

	void _copyReverse(const void* _src, void* _dst, size_t _size);

	bool _findFBAProcessHandle();

	void _updateMemoryDebugger(bool* _showMemoryDebugger);

	void _saveApplicationData();
	void _loadApplicationData();

	void _saveTrainingData();
	void _loadTrainingData();

	void _saveMemoryLabels();
	void _loadMemoryLabels();

	void _calibrateP2InputMapping();

	void _readGameObjectData(void* _memory, size_t _address, GameObjectData& _data);
	void _displayGameObjectData(const GameObjectData& _data);

	void _drawMemory(void* _memory, MemoryDisplayData& _data, std::vector<MemoryLabel>& _labels, int _selectedLabel = -1);

	HWND m_windowHandle = nullptr;

	bool m_isDettachRequested = false;
	gcroot<System::Diagnostics::Process^> m_FBAProcess = nullptr;
	HANDLE m_FBAProcessHandle = nullptr;
	size_t m_ramStartingAddress = 0;

	uint32_t m_currentFrame = 0;

	bool m_showDemoWindow = false;

	gcroot<TrainingThreadHelper^> m_threads;
	gcroot<System::Threading::SpinLock^> m_lock;

	mirror::BinarySerializer m_dataSerializer;

	int m_p2Keys[GameInput_COUNT] = {};

	TrainingApplicationData m_applicationData;
	TrainingModeData m_trainingData;
	std::vector<MemoryLabel> m_memoryLabels;

	uint8_t* m_memoryBuffer = nullptr;
	size_t m_memoryBufferSize = 0;

	struct MemoryWriteRequest
	{
		size_t address = 0;
		void* data = nullptr;
		size_t dataSize = 0;
	};
	std::vector<MemoryWriteRequest> m_memoryWriteRequests;

	int m_selectedLabel = -1;

	// GAME DATA
	bool m_isInMatch = false;
};
