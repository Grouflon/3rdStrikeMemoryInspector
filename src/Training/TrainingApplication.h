#pragma once

typedef void* HANDLE;

class TrainingApplication
{
public:
	void initialize();
	void shutdown();
	void update();

private:

	bool _isAttachedToFBA();
	void _attachToFBA();
	void _dettachFromFBA();

	void _writeByte(size_t _address, char _byte);
	char _readByte(size_t _address);
	long long unsigned int _readUnsignedInt(size_t _address, size_t _size);

	HANDLE m_FBAProcessHandle = nullptr;
	size_t m_ramStartingAddress = 0;

	bool m_showDemoWindow = false;
};
