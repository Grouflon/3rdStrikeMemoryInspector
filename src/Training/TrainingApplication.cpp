#include "TrainingApplication.h"

#include <windows.h>
#include <ImGui\imgui.h>
#include <cstdio>
#include <algorithm>

using namespace System;
using namespace System::Diagnostics;

char s_calibrationSequence[] = { 0x78, 0x00, 0x79, 0x00, 0x76, 0x00 };
size_t s_calibrationSequenceOffset = 0x160B1B94 - 0x161091C0 - 0x02011377;

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


size_t s_maxAddress = 0x02080000;

bool m_showMemoryDebugger = true;
size_t m_debugAddress = 0;

void _IncrementDebugAddress(long long int _increment)
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

HANDLE FindFBAProcessHandle()
{
	array<Process^>^ processes;
	processes = Process::GetProcessesByName("ggpofba");
	if (processes->Length == 0)
		processes = Process::GetProcessesByName("ggpofba-ng");

	Process^ fbaProcess = nullptr;

	for (int i = 0; i < processes->Length; ++i)
	{
		Process^ p = processes[i];
		if (p->MainWindowTitle->IndexOf("Street Fighter III") != -1)
		{
			fbaProcess = p;
			break;
		}
	}

	if (fbaProcess == nullptr)
	{
		return nullptr;
	}

	return OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION, false, fbaProcess->Id);
}

size_t FindRAMStartAddress(HANDLE _processHandle)
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	void* minAddress = si.lpMinimumApplicationAddress;
	void* maxAddress = si.lpMaximumApplicationAddress;

	MEMORY_BASIC_INFORMATION mbi;
	size_t bufferSize = 0x10000000;
	char* buffer = (char*)malloc(bufferSize);

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

		minAddress = (char*)minAddress + mbi.RegionSize;
	}


	free(buffer);

	return RAMStartAddress;
}

static char* m_memoryBuffer = nullptr;
static char* m_memoryMapData = nullptr;
static size_t m_memoryMapSize = 0;
bool m_showMemoryMap = true;

void TrainingApplication::initialize()
{
	m_memoryBuffer = (char*)malloc(s_maxAddress);

	_attachToFBA();
}

void TrainingApplication::update()
{
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::Begin("Body", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration);
	ImGui::PopStyleVar(2);
	ImGui::SetWindowPos(ImVec2(0.f, 0.f));
	ImGui::SetWindowSize(ImGui::GetIO().DisplaySize);

	// BEGIN

	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("Misc"))
		{
			ImGui::MenuItem("Show Memory Debugger", "", &m_showMemoryDebugger);
			ImGui::MenuItem("Show Memory Map", "", &m_showMemoryMap);
			ImGui::MenuItem("Show ImGui Demo Window", "", &m_showDemoWindow);
			ImGui::EndMenu();
		}

		if (!_isAttachedToFBA())
		{
			ImGui::SameLine(0, ImGui::GetContentRegionAvail().x - 191.f);
			ImGui::Text("Not attached to FBA");
			if (ImGui::Button("Attach"))
			{
				_attachToFBA();
			}
		}
		else
		{
			ImGui::SameLine(0, ImGui::GetContentRegionAvail().x - 170.f);
			ImGui::Text("Attached to FBA");
			if (ImGui::Button("Dettach"))
			{
				_dettachFromFBA();
			}
		}
		ImGui::EndMenuBar();
	}

	if (_isAttachedToFBA())
	{
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
	}
	else
	{
		ImGui::Text("zob");
	}

	if (m_showDemoWindow)
		ImGui::ShowDemoWindow(&m_showDemoWindow);

	if (m_showMemoryDebugger)
	{
		static const size_t rowSize = 0x10;
		static const size_t rowCount = 25;
		static const size_t bytesToRead = rowSize * rowCount;

		ImGui::Begin("Memory Debugger", &m_showMemoryDebugger);

		if (!_isAttachedToFBA())
		{
			ImGui::Text("Not attached to FBA");
			ImGui::End();
		}
		else
		{
			float mouseWheel = ImGui::GetIO().MouseWheel;
			if (mouseWheel < 0.f)
			{
				_IncrementDebugAddress(0x10);
			}
			else if (mouseWheel > 0.f)
			{
				_IncrementDebugAddress(-0x10);
			}

			if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_PageUp)))
			{
				_IncrementDebugAddress(-(int)rowCount);
			}
			if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_PageDown)))
			{
				_IncrementDebugAddress((int)rowCount);
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
					ImGui::Text("%02X", (unsigned)(unsigned char)(memoryBuffer[row * 0x10 + col]));

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

			ImGui::End();
		}
	}

	SIZE_T bytesRead = 0;
	ReadProcessMemory(m_FBAProcessHandle, (void*)m_ramStartingAddress, m_memoryBuffer, s_maxAddress, &bytesRead);
	ImGui::Begin("Memory Map", &m_showMemoryMap);
	ImGui::Text("%llu", bytesRead/8);
	ImVec2 windowSize = ImGui::GetWindowSize();
	if (windowSize.x * windowSize.y > m_memoryMapSize)
	{
		m_memoryMapSize = (unsigned)windowSize.x * (unsigned)windowSize.y;
		m_memoryMapData = (char*)realloc(m_memoryMapData, m_memoryMapSize);
	}
	memset(m_memoryMapData, 0, m_memoryMapSize);
	long long unsigned* u64Buffer = (long long unsigned*)m_memoryBuffer;
	size_t increment = (bytesRead / 8) / m_memoryMapSize;
	for (size_t i = 0; i < (bytesRead / 8); ++i)
	{
		size_t pixelIndex = i / increment;
		m_memoryMapData[pixelIndex] = m_memoryMapData[pixelIndex] & u64Buffer[i];
	}

	ImGui::Text("%.1f, %.1f", windowSize.x, windowSize.y);
	ImGui::End();
	

	// END
	ImGui::End();
}

void TrainingApplication::shutdown()
{

}

bool TrainingApplication::_isAttachedToFBA()
{
	return m_FBAProcessHandle != nullptr && m_ramStartingAddress != 0;
}

void TrainingApplication::_attachToFBA()
{
	if (_isAttachedToFBA())
		return;

	m_FBAProcessHandle = FindFBAProcessHandle();
	if (!m_FBAProcessHandle)
	{
		printf("Error: Failed to find FBA process.\n");
		return;
	}

	m_ramStartingAddress = FindRAMStartAddress(m_FBAProcessHandle);
	if (!m_ramStartingAddress)
	{
		printf("Error: Failed to find RAM starting address.\n");
		return;
	}
}

void TrainingApplication::_dettachFromFBA()
{
	if (!_isAttachedToFBA())
		return;

	m_FBAProcessHandle = nullptr;
	m_ramStartingAddress = 0;
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
