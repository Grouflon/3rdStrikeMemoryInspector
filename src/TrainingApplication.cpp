#include "TrainingApplication.h"



#include <windows.h>
#include <imgui.h>
#include <cstdio>
#include <algorithm>

using namespace System;
using namespace System::Diagnostics;
using namespace System::Threading;

static const char* s_applicationDataFileName = "application.data";

char s_calibrationSequence[] = { 0x78, 0x00, 0x79, 0x00, 0x76, 0x00 };
size_t s_calibrationSequenceOffset = 0x160B1B94 - 0x161091C0 - 0x02011377;

enum class GlobalInputFlags : uint32_t
{
	GlobalInputFlags_Up = 0x0100,
	GlobalInputFlags_Down = 0x0200,
	GlobalInputFlags_Left = 0x0400,
	GlobalInputFlags_Right = 0x0800,
	GlobalInputFlags_LP = 0x1000,
	GlobalInputFlags_MP = 0x2000,
	GlobalInputFlags_HP = 0x4000,
	GlobalInputFlags_LK = 0x8000,
	GlobalInputFlags_MK = 0x0001,
	GlobalInputFlags_HK = 0x0002,
	GlobalInputFlags_Start = 0x0010
};

enum class GameInputFlags : uint32_t
{
	GameInputFlags_Up = 0x0001,
	GameInputFlags_Down = 0x0002,
	GameInputFlags_Left = 0x0004,
	GameInputFlags_Right = 0x0008,
	GameInputFlags_LP = 0x0010,
	GameInputFlags_MP = 0x0020,
	GameInputFlags_HP = 0x0040,
	GameInputFlags_LK = 0x0100,
	GameInputFlags_MK = 0x0200,
	GameInputFlags_HK = 0x0400
};

namespace memoryMap
{
	static size_t timer = 0x02011377;
	static size_t frameNumber = 0x02007F03;
	static size_t P1Health = 0x02068d0b;
	static size_t P2Health = 0x020691a3;
	static size_t P1Stun = 0x02069601;
	static size_t P2Stun = 0x02069615;
	static size_t P1Pos = 0x02068cd5;
	static size_t P2Pos = 0x0206916d;
	static size_t P1Super = 0x020695b9;
	static size_t P2Super = 0x020695e5;
	static size_t P1MetersA = 0x020286ab;
	static size_t P1MetersB = 0x020695bf;
	static size_t P2MetersA = 0x020286df;
	static size_t P2MetersB = 0x020695eb;
	static size_t P1MaxMeters = 0x020695c1;
	static size_t P2MaxMeters = 0x020695ed;
	static size_t P1Height = 0x020698bf;
	static size_t P2Height = 0x02069bbf;
	static size_t P1Combo = 0x020696c9;
	static size_t P2Combo = 0x02069621;
	static size_t P1Attacking = 0x0206909a;
}

#define IMGUI_APPDATA(exp) if (exp) { _saveApplicationData(); }


size_t s_maxAddress = 0x02080000;

size_t FindRAMStartAddress(HANDLE _processHandle)
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	void* minAddress = si.lpMinimumApplicationAddress;
	void* maxAddress = si.lpMaximumApplicationAddress;

	MEMORY_BASIC_INFORMATION mbi;
	size_t bufferSize = 0x10000000;
	uint8_t* buffer = (uint8_t*)malloc(bufferSize);

	size_t RAMStartAddress = 0;

	while (minAddress < maxAddress && RAMStartAddress == 0)
	{
		size_t result = VirtualQueryEx(_processHandle, minAddress, &mbi, sizeof(mbi));


		if (mbi.Protect == PAGE_READWRITE && mbi.State == MEM_COMMIT)
		{
			assert(mbi.RegionSize <= bufferSize);
			SIZE_T bytesRead;
			ReadProcessMemory(_processHandle, minAddress, buffer, mbi.RegionSize, &bytesRead);
			assert(bytesRead == mbi.RegionSize);


			size_t calibrationSequenceSize = sizeof(s_calibrationSequence) / sizeof(*s_calibrationSequence);

			for (size_t i = 0; i < bytesRead; ++i)
			{
				bool match = true;
				for (size_t j = 0; j < calibrationSequenceSize; ++j)
				{
					if (buffer[i + j] != s_calibrationSequence[j])
					{
						match = false;
						break;
					}
					else
					{
						int a = 0;
					}
				}

				if (match)
				{
					RAMStartAddress = (size_t)minAddress + i + s_calibrationSequenceOffset;
					break;
				}
			}
		}

		minAddress = (uint8_t*)minAddress + mbi.RegionSize;
	}
	free(buffer);

	return RAMStartAddress;
}

static char* m_memoryBuffer = nullptr;
static char* m_memoryMapData = nullptr;
static size_t m_memoryMapSize = 0;

static bool m_isRecording = false;
static size_t m_memorySamplesCount = 0;
static size_t m_memorySamplesSize = 0;
static char** m_memorySamples = nullptr;

void TrainingThreadHelper::watchFrameThreadMain()
{
	application->watchFrameChange();
}

void TrainingApplication::initialize()
{
	m_memoryBuffer = (char*)malloc(s_maxAddress);

	m_lock = gcnew SpinLock();

	_loadApplicationData();
}


void TrainingApplication::shutdown()
{
	_dettachFromFBA();

	m_threads = nullptr;

	free(m_memoryBuffer);
	m_memoryBuffer = nullptr;
}

static uint16_t m_p1GlobalInputFlags;
static uint16_t m_p1GameInputFlags;
static bool m_p1CoinDown;

static uint16_t m_p2GlobalInputFlags;
static uint16_t m_p2GameInputFlags;
static bool m_p2CoinDown;

void TrainingApplication::onFrameBegin()
{
	{
		m_p1GlobalInputFlags = _readUnsignedInt(0x0206AA90, 2);
		m_p1GameInputFlags = _readUnsignedInt(0x0202564B, 2);
		m_p1CoinDown = _readByte(0x0206AABB);
		
		m_p2GlobalInputFlags = _readUnsignedInt(0x0206AA93, 2);
		m_p2GameInputFlags = _readUnsignedInt(0x02025685, 2);
		m_p2CoinDown = _readByte(0x00206AAC1);
	}
}

void TrainingApplication::update()
{
	// CHECK FBA PROCESS
	if (m_applicationData.autoAttach && !_isAttachedToFBA())
	{
		_attachToFBA();
	}
	if (static_cast<Process^>(m_FBAProcess) != nullptr)
	{
		m_FBAProcess->Refresh();

		bool hasExited = m_FBAProcess->HasExited;
		String^ title = m_FBAProcess->MainWindowTitle;

		if (hasExited
		|| (title != "" && title->IndexOf("Street Fighter III") == -1))
		{
			_dettachFromFBA();
		}
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::Begin("Body", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration);
	ImGui::PopStyleVar(2);
	ImGui::SetWindowPos(ImVec2(0.f, 0.f));
	ImGui::SetWindowSize(ImGui::GetIO().DisplaySize);

	// BEGIN

	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("Options"))
		{
			IMGUI_APPDATA(ImGui::MenuItem("Auto attach to FBA", "", &m_applicationData.autoAttach));
			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Misc"))
		{
			IMGUI_APPDATA(ImGui::MenuItem("Show Memory Debugger", "", &m_applicationData.showMemoryDebugger));
			IMGUI_APPDATA(ImGui::MenuItem("Show Memory Map", "", &m_applicationData.showMemoryMap));
			ImGui::MenuItem("Show ImGui Demo Window", "", &m_showDemoWindow);
			ImGui::EndMenu();
		}

		if (!_isAttachedToFBA())
		{
			float offset = m_applicationData.autoAttach ? 130.f : 191.f;
			ImGui::SameLine(0, ImGui::GetContentRegionAvail().x - offset);
			ImGui::Text("Not attached to FBA");

			if (!m_applicationData.autoAttach)
			{
				if (ImGui::Button("Attach"))
				{
					_attachToFBA();
				}
			}
		}
		else
		{
			float offset = m_applicationData.autoAttach ? 102.f : 170.f;

			ImGui::SameLine(0, ImGui::GetContentRegionAvail().x - offset);
			ImGui::Text("Attached to FBA");
			if (!m_applicationData.autoAttach)
			{
				if (ImGui::Button("Dettach"))
				{
					_dettachFromFBA();
				}
			}
		}
		ImGui::EndMenuBar();
	}

	if (_isAttachedToFBA())
	{
		bool lockTaken = false;
		m_lock->TryEnter(-1, lockTaken);
		assert(lockTaken);

		ImGui::Text("RAM starting address: 0x%08x", m_ramStartingAddress);
		ImGui::Text("Frame: %d", _readUnsignedInt(memoryMap::frameNumber, 4));
		ImGui::Text("Timer: %d", _readByte(memoryMap::timer));
		ImGui::Text("P1 Health: %d", _readUnsignedInt(memoryMap::P1Health, 2));
		ImGui::Text("P2 Health: %d", _readUnsignedInt(memoryMap::P2Health, 2));
		ImGui::Text("P1 Stun: %d", _readUnsignedInt(memoryMap::P1Stun, 2));
		ImGui::Text("P2 Stun: %d", _readUnsignedInt(memoryMap::P2Stun, 2));
		ImGui::Text("P1Pos: %d", _readByte(memoryMap::P1Pos));
		ImGui::Text("P2Pos: %d", _readByte(memoryMap::P2Pos));
		ImGui::Text("P1Super: %d", _readByte(memoryMap::P1Super));
		ImGui::Text("P2Super: %d", _readByte(memoryMap::P2Super));
		ImGui::Text("P1MetersA: %d", _readByte(memoryMap::P1MetersA));
		ImGui::Text("P1MetersB: %d", _readByte(memoryMap::P1MetersB));
		ImGui::Text("P1MaxMeters: %d", _readByte(memoryMap::P1MaxMeters));
		ImGui::Text("P2MetersA: %d", _readByte(memoryMap::P2MetersA));
		ImGui::Text("P2MetersB: %d", _readByte(memoryMap::P2MetersB));
		ImGui::Text("P2MaxMeters: %d", _readByte(memoryMap::P2MaxMeters));
		ImGui::Text("P1Height: %d", _readByte(memoryMap::P1Height));
		ImGui::Text("P1Combo: %d", _readByte(memoryMap::P1Combo));
		ImGui::Text("P2Combo: %d", _readByte(memoryMap::P2Combo));

		ImGui::Text("P1Attacking: %d", _readByte(memoryMap::P1Attacking));

		ImGui::Text("P1 Global Input: %04X", m_p1GlobalInputFlags);
		ImGui::Text("P1 Game Input: %04X", m_p1GameInputFlags);
		ImGui::Text("P1 Coin Down: %d", m_p1CoinDown);

		ImGui::Text("P2 Global Input: %04X", m_p2GlobalInputFlags);
		ImGui::Text("P2 Game Input: %04X", m_p2GameInputFlags);
		ImGui::Text("P2 Coin Down: %d", m_p2CoinDown);

		m_lock->Exit();
	}
	else
	{
	}

	if (m_showDemoWindow)
		ImGui::ShowDemoWindow(&m_showDemoWindow);

	bool showMemoryDebugger = m_applicationData.showMemoryDebugger;
	if (showMemoryDebugger)
	{
		static const size_t rowSize = 0x10;
		static const size_t rowCount = 25;
		static const size_t bytesToRead = rowSize * rowCount;

		ImGui::Begin("Memory Debugger", &showMemoryDebugger);
		if (!_isAttachedToFBA())
		{
			ImGui::Text("Not attached to FBA");
		}
		else
		{
			float mouseWheel = ImGui::GetIO().MouseWheel;
			if (mouseWheel < 0.f)
			{
				_incrementDebugAddress(0x10);
			}
			else if (mouseWheel > 0.f)
			{
				_incrementDebugAddress(-0x10);
			}

			if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_PageUp)))
			{
				_incrementDebugAddress(-(int)rowCount);
			}
			if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_PageDown)))
			{
				_incrementDebugAddress((int)rowCount);
			}

			char addressBuffer[9] = {};
			sprintf(addressBuffer, "%08X\0", m_debugAddress);
			if (ImGui::InputText("Address", addressBuffer, 9))
			{
				m_debugAddress = (size_t)strtoll(addressBuffer, nullptr, 16);
			}
			ImGui::Separator();

			char memoryBuffer[bytesToRead];
			SIZE_T bytesRead;
			void* FBAAddress = (void*)(m_ramStartingAddress + m_debugAddress);
			ReadProcessMemory(m_FBAProcessHandle, FBAAddress, memoryBuffer, bytesToRead, &bytesRead);

			for (size_t row = 0; row < rowCount; ++row)
			{
				ImGui::Text("%08X", m_debugAddress + row * 0x10);
				ImGui::SameLine(0.f, 20.f);
				for (size_t col = 0; col < rowSize; ++col)
				{
					ImGui::Text("%02X", (uint8_t)(memoryBuffer[row * 0x10 + col]));

					if (col == 7)
					{
						ImGui::SameLine(0.f, 15.f);
					}
					else if (col != rowSize - 1)
					{
						ImGui::SameLine();
					}
				}
			}
		}
		ImGui::End();
	}
	if (showMemoryDebugger != m_applicationData.showMemoryDebugger)
	{
		m_applicationData.showMemoryDebugger = showMemoryDebugger;
		_saveApplicationData();
	}

	bool showMemoryMap = m_applicationData.showMemoryMap;
	if (showMemoryMap)
	{
		SIZE_T bytesRead = 0;
		ReadProcessMemory(m_FBAProcessHandle, (void*)m_ramStartingAddress, m_memoryBuffer, s_maxAddress, &bytesRead);
		ImGui::Begin("Memory Map", &showMemoryMap);
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.f, 0.f, 0.f, 1.f));
		if (!m_isRecording)
		{
			if (ImGui::Button("Record"))
			{
				m_isRecording = true;
			}
		}
		else
		{
			if (ImGui::Button("Stop Recording"))
			{
				m_isRecording = false;
			}
		}
		ImGui::PopStyleColor(1);
		ImGui::Text("%llu", bytesRead / 8);
		ImGui::Separator();

		/*ImVec2 drawStart = ImGui::GetCursorScreenPos();
		ImVec2 drawSize = ImGui::GetContentRegionAvail();
		size_t minMemoryMapSize = (long long unsigned)drawSize.x * (long long unsigned)drawSize.y;
		if (minMemoryMapSize > m_memoryMapSize)
		{
			m_memoryMapSize = minMemoryMapSize;
			m_memoryMapData = (char*)realloc(m_memoryMapData, m_memoryMapSize);
		}
		memset(m_memoryMapData, 0, m_memoryMapSize);

		long long unsigned* u64Buffer = (long long unsigned*)m_memoryBuffer;
		float increment = float(bytesRead / sizeof(long long unsigned)) / float(minMemoryMapSize);
		for (size_t i = 0; i < (bytesRead / sizeof(long long unsigned)); ++i)
		{
			size_t pixelIndex = i / std::ceil(increment);
			m_memoryMapData[pixelIndex] = m_memoryMapData[pixelIndex] & u64Buffer[i];
		}

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		for (size_t i = 0; i < minMemoryMapSize; ++i)
		{
			ImColor col = m_memoryMapData[i] ? ImColor(1.f, 1.f, 1.f, 1.f) : ImColor(1.f, 1.f, 1.f, .3f);
			ImVec2 pixelStart = ImVec2(drawStart.x + i % (long long unsigned)drawSize.x, drawStart.y + i / (long long unsigned)drawSize.x);
			ImVec2 pixelEnd = ImVec2(pixelStart.x + 1, pixelStart.y + 1);
			drawList->AddRectFilled(pixelStart, pixelEnd, col);
		}*/

		ImGui::End();
	}
	if (showMemoryMap != m_applicationData.showMemoryMap)
	{
		m_applicationData.showMemoryMap = showMemoryMap;
		_saveApplicationData();
	}

	if (m_isRecording)
	{
		
	}

	// END
	ImGui::End();
}

void TrainingApplication::watchFrameChange()
{
	while (!m_isDettachRequested)
	{
		bool lockTaken = false;
		m_lock->TryEnter(-1, lockTaken);
		assert(lockTaken);

		uint32_t currentFrame = _readUnsignedInt(memoryMap::frameNumber, 4);
		if (currentFrame != m_currentFrame)
		{
			m_currentFrame = currentFrame;
			onFrameBegin();
		}

		m_lock->Exit();
	}
}

bool TrainingApplication::_isAttachedToFBA()
{
	return m_FBAProcessHandle != nullptr;
}

void TrainingApplication::_attachToFBA()
{
	if (_isAttachedToFBA())
		return;

	if (!_findFBAProcessHandle())
	{
		if (!m_applicationData.autoAttach)
			printf("Error: Failed to find FBA process.\n");

		return;
	}

	m_ramStartingAddress = FindRAMStartAddress(m_FBAProcessHandle);
	if (!m_ramStartingAddress)
	{
		if (!m_applicationData.autoAttach)
			printf("Error: Failed to find RAM starting address.\n");

		return;
	}

	m_currentFrame = _readUnsignedInt(memoryMap::frameNumber, 4);

	m_threads = gcnew TrainingThreadHelper();
	m_threads->watchFrameThread = gcnew Thread(gcnew ThreadStart(m_threads, &TrainingThreadHelper::watchFrameThreadMain));
	m_threads->watchFrameThread->Name = "WatchFrame";
	m_threads->application = this;
	m_threads->watchFrameThread->Start();
}

void TrainingApplication::_dettachFromFBA()
{
	if (!_isAttachedToFBA())
		return;

	m_isDettachRequested = true;
	m_threads->watchFrameThread->Join();
	m_threads = nullptr;
	m_isDettachRequested = false;

	m_ramStartingAddress = 0;
	m_FBAProcessHandle = nullptr;
	m_FBAProcess = nullptr;
}

void TrainingApplication::_writeByte(size_t _address, char _byte)
{
	assert(_isAttachedToFBA());
	WriteProcessMemory(m_FBAProcessHandle, (void*)(m_ramStartingAddress + _address), &_byte, 1, nullptr);
}

char TrainingApplication::_readByte(size_t _address)
{
	assert(_isAttachedToFBA());
	char byte = 0;
	ReadProcessMemory(m_FBAProcessHandle, (void*)(m_ramStartingAddress + _address), &byte, 1, nullptr);
	return byte;
}

void TrainingApplication::_incrementDebugAddress(int64_t _increment)
{
	if (_increment < 0 && (_increment * -1) > m_debugAddress)
		m_debugAddress = 0;
	else
	{
		m_debugAddress += _increment;
		if (m_debugAddress > s_maxAddress)
			m_debugAddress = s_maxAddress;
	}
}

bool TrainingApplication::_findFBAProcessHandle()
{
	array<Process^>^ processes;
	processes = Process::GetProcessesByName("ggpofba");
	if (processes->Length == 0)
		processes = Process::GetProcessesByName("ggpofba-ng");

	m_FBAProcess = nullptr;

	for (int i = 0; i < processes->Length; ++i)
	{
		Process^ p = processes[i];
		if (p->MainWindowTitle->IndexOf("Street Fighter III") != -1)
		{
			m_FBAProcess = p;
			break;
		}
	}

	if (static_cast<Process^>(m_FBAProcess) == nullptr)
	{
		return false;
	}

	m_FBAProcessHandle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION, false, m_FBAProcess->Id);
	if (m_FBAProcessHandle == nullptr)
	{
		return false;
	}
	return true;
}

void TrainingApplication::_saveApplicationData()
{
	m_dataSerializer.beginWrite();
	m_dataSerializer.serialize("applicationData", m_applicationData);
	m_dataSerializer.endWrite();

	const void* data = nullptr;
	size_t dataSize = 0u;
	m_dataSerializer.getWriteData(data, dataSize);

	FILE* fp = fopen(s_applicationDataFileName, "w");
	fwrite(data, dataSize, 1, fp);
	fclose(fp);
}

void TrainingApplication::_loadApplicationData()
{
	size_t dataSize = 0u;
	void* data = nullptr;

	FILE* fp = fopen(s_applicationDataFileName, "r");
	if (!fp)
		return;

	fseek(fp, 0, SEEK_END);
	dataSize = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	data = malloc(dataSize);

	fread(data, dataSize, 1, fp);
	fclose(fp);

	m_dataSerializer.beginRead(data, dataSize);
	m_dataSerializer.serialize("applicationData", m_applicationData);
	m_dataSerializer.endRead();

	free(data);
}

long long unsigned int TrainingApplication::_readUnsignedInt(size_t _address, size_t _size)
{
	assert(_isAttachedToFBA());
	assert(_size <= 8);
	char buffer[8];
	ReadProcessMemory(m_FBAProcessHandle, (void*)(m_ramStartingAddress + _address), buffer, _size, nullptr);

	long long unsigned int result = 0;
	for (int i = 0; i < _size; ++i)
	{
		result += (unsigned char)buffer[i] * std::pow(0x100, i);
	}
	return result;
}
