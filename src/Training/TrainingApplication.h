#pragma once

#include <cstdint>
#include <vcclr.h>

typedef void* HANDLE;
class TrainingApplication;

ref class TrainingThreadHelper
{
public:
	void watchFrameThreadMain();

	System::Threading::Thread^ watchFrameThread;
	System::Threading::ReaderWriterLock^ lock;
	TrainingApplication* application;
};

class TrainingApplication
{
public:
	void initialize();
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


	HANDLE m_FBAProcessHandle = nullptr;
	size_t m_ramStartingAddress = 0;
	uint32_t m_currentFrame = 0;

	bool m_showDemoWindow = false;
	bool m_showMemoryDebugger = true;

	size_t m_debugAddress = 0;

	gcroot<TrainingThreadHelper^> m_threads;
};
