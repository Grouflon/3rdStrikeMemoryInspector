#include "TrainingApplication.h"

#include <windows.h>
#include <cstdio>
#include <algorithm>
#include <filesystem>

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <Log.h>
#include <Keys.h>

using namespace System;
using namespace System::Diagnostics;
using namespace System::Threading;

static const char* s_applicationDataFileName = "data/application.data";
static const char* s_trainingDataFileName = "data/training.data";
static const char* s_memoryLabelsFileName = "data/labels.data";

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
#define IMGUI_TRAINDATA(exp) if (exp) { _saveTrainingData(); }


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

void TrainingApplication::initialize(HWND _windowHandle)
{
	//NOTE: we should use double buffering in ordre to have only locks on the write requests array
	// and we should also test the thread pointer before joining with it (can it really be null with all that gc shit ?)

	assert(_windowHandle);
	m_windowHandle = _windowHandle;

	m_memoryBufferSize = SF33_MAXADDRESS;
	m_memoryBuffer = (uint8_t*)malloc(m_memoryBufferSize);

	m_lock = gcnew SpinLock();

	_loadApplicationData();
	_loadTrainingData();
	_loadMemoryLabels();
}


void TrainingApplication::shutdown()
{
	_saveTrainingData();
	_saveApplicationData();

	_dettachFromFBA();

	m_threads = nullptr;

	free(m_memoryBuffer);
	m_memoryBuffer = nullptr;
	m_memoryBufferSize = 0;
}

static uint16_t m_p1GlobalInputFlags;
static uint16_t m_p1GameInputFlags;
static bool m_p1CoinDown;

static uint16_t m_p2GlobalInputFlags;
static uint16_t m_p2GameInputFlags;
static bool m_p2CoinDown;

void TrainingApplication::onFrameBegin()
{
	_resolveMemoryWriteRequests();

	SIZE_T bytesRead = 0;
	ReadProcessMemory(m_FBAProcessHandle, (void*)m_ramStartingAddress, m_memoryBuffer, SF33_MAXADDRESS, &bytesRead);

	/*HWND activeWindow = GetForegroundWindow();
	if (activeWindow == (HWND)(void*)(m_FBAProcess->MainWindowHandle))
	{
		// INJECT INPUT HERE
		INPUT inputs[2] = {};
		inputs[0].type = INPUT_KEYBOARD;
		inputs[0].ki.wVk = m_p2Keys[GameInput_LP];
		inputs[0].ki.dwFlags = m_currentFrame % 2 ? 0 : KEYEVENTF_KEYUP;
		UINT result = SendInput(1, inputs, sizeof(INPUT));
	}*/

	if (m_trainingData.enabled)
	{
		if (m_trainingData.lockTimer) _writeByte(memoryMap::timer, 100);
		if (m_trainingData.infiniteLife)
		{
			_writeByte(memoryMap::P1Health, 160);
			_writeByte(memoryMap::P2Health, 160);
		}
		if (m_trainingData.noStun)
		{
			_writeByte(0x020695FD, 0); // P1 Stun timer
			_writeByte(0x02069611, 0); // P2 Stun timer
			char data[4] = {};
			_writeData(0x020695FF, data, 4); // P1 Stun bar
			_writeData(0x02069613, data, 4); // P2 Stun bar
		}
		if (m_trainingData.disableMusic)
		{
			_writeByte(0x02078D06, 0x00);
		}
		else
		{ 
			_writeByte(0x02078D06, 0x06);
		}
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

	ImGui::PushStyleColor(ImGuiCol_MenuBarBg, IM_COL32(0, 0, 0, 230));

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 180));
	ImGui::Begin("Body", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDecoration);
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);
	ImGui::SetWindowPos(ImVec2(0.f, 0.f));
	ImGui::SetWindowSize(ImGui::GetIO().DisplaySize);

	// BEGIN

	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("Options"))
		{
			IMGUI_APPDATA(ImGui::MenuItem("Auto attach to FBA", "", &m_applicationData.autoAttach));
			IMGUI_APPDATA(ImGui::Combo("Docking Mode", (int*)&m_applicationData.dockingMode, "Undocked\0Left\0Right\0\0"));
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
		// Joystick emulation doc
		// https://stackoverflow.com/questions/28662875/c-sending-joystick-directional-input-to-program/28687064#28687064
		// http://vjoystick.sourceforge.net/site/index.php/dev/87-writing-a-feeder-application2
		// https://os.mbed.com/users/wim/notebook/usb-joystick-device/
		// https://github.com/jkuhlmann/gainput

		// WINDOW DOCKING
		if (m_applicationData.dockingMode != DockingMode::Undocked)
		{
			WINDOWINFO FBAWindowInfo = {};
			{
				bool result = GetWindowInfo((HWND)(void*)(m_FBAProcess->MainWindowHandle), &FBAWindowInfo);
				assert(result);
			}

			WINDOWINFO TrainingWindowInfo = {};
			{
				bool result = GetWindowInfo(m_windowHandle, &TrainingWindowInfo);
				assert(result);
			}

			int fbaX = FBAWindowInfo.rcWindow.left;
			int fbaY = FBAWindowInfo.rcWindow.top;
			int fbaW = FBAWindowInfo.rcWindow.right - FBAWindowInfo.rcWindow.left;
			int fbaH = FBAWindowInfo.rcWindow.bottom - FBAWindowInfo.rcWindow.top;

			int trX = TrainingWindowInfo.rcWindow.left;
			int trY = TrainingWindowInfo.rcWindow.top;
			int trW = TrainingWindowInfo.rcWindow.right - TrainingWindowInfo.rcWindow.left;
			int trH = TrainingWindowInfo.rcWindow.bottom - TrainingWindowInfo.rcWindow.top;

			if (trY + trH < fbaY + fbaH)
			{
				trY = fbaY + fbaH - trH;
			}

			if (trY > fbaY)
			{
				trY = fbaY;
			}

			if (m_applicationData.dockingMode == DockingMode::Left)
			{
				trX = fbaX - trW;
			}
			else if (m_applicationData.dockingMode == DockingMode::Right)
			{
				trX = fbaX + fbaW;
			}

			SetWindowPos(m_windowHandle, (HWND)(void*)(m_FBAProcess->MainWindowHandle), trX, trY, trW, trH, 0);

			if (trX != m_applicationData.windowX
				|| trY != m_applicationData.windowY
				|| trW != m_applicationData.windowW
				|| trH != m_applicationData.windowH
				)
			{
				m_applicationData.windowX = trX;
				m_applicationData.windowY = trY;
				m_applicationData.windowW = trW;
				m_applicationData.windowH = trH;
				_saveApplicationData();
			}
		}
		

		bool lockTaken = false;
		m_lock->TryEnter(-1, lockTaken);
		assert(lockTaken);

		// READ SOME SHIT
		// I believe the bytes that are expected to be 0xff means that a character has been locked, while the byte expected to be 0x02 is the current match state. 0x02 means that round has started and players can move
		int8_t p1Locked = _readInt8(m_memoryBuffer, 0x020154C8);
		int8_t p2Locked = _readInt8(m_memoryBuffer, 0x020154CE);
		uint8_t matchState = _readInt8(m_memoryBuffer, 0x020154A7);
		m_isInMatch = ((p1Locked == int8_t(0xFF) || p2Locked == int8_t(0xFF)) && matchState == uint8_t(0x02));
		{
			m_p1GlobalInputFlags = _readUInt16(m_memoryBuffer, 0x0206AA90);
			m_p1GameInputFlags = _readUInt16(m_memoryBuffer, 0x0202564B);
			m_p1CoinDown = _readUInt8(m_memoryBuffer, 0x0206AABB);

			m_p2GlobalInputFlags = _readUInt16(m_memoryBuffer, 0x0206AA93);
			m_p2GameInputFlags = _readUInt16(m_memoryBuffer, 0x02025685);
			m_p2CoinDown = _readUInt8(m_memoryBuffer, 0x00206AAC1);
		}

		if (ImGui::BeginTabBar("MyTabBar", ImGuiTabBarFlags_Reorderable))
		{
			if (ImGui::BeginTabItem("Memory Debugger"))
			{
				_updateMemoryDebugger(nullptr);

				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Training Settings"))
			{
				IMGUI_TRAINDATA(ImGui::Checkbox("Training Mode Enabled", &m_trainingData.enabled));
				ImGui::Separator();
				IMGUI_TRAINDATA(ImGui::Checkbox("Lock timer", &m_trainingData.lockTimer));
				IMGUI_TRAINDATA(ImGui::Checkbox("Infinite life", &m_trainingData.infiniteLife));
				IMGUI_TRAINDATA(ImGui::Checkbox("No stun", &m_trainingData.noStun));
				IMGUI_TRAINDATA(ImGui::Checkbox("Disable music", &m_trainingData.disableMusic));

				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}

		/*ImGui::Text("RAM starting address: 0x%08x", m_ramStartingAddress);
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
		ImGui::Text("P2 Coin Down: %d", m_p2CoinDown);*/

		// READ THE MYSTERIOUS LISTS
		//0x02068997
		//0x02068AB6

		if (m_isInMatch)
		{
			//player = { 0x02068C6C, 0x2069104 }, --0x498
			/*if (ImGui::TreeNode((void*)(0x02068C6D), "player1(0x%08X)", 0x02068C6D))
			{
				GameObjectData data;
				_readGameObjectData(m_memoryBuffer, 0x02068C6D, data);
				_displayGameObjectData(data);

				ImGui::TreePop();
			}

			if (ImGui::TreeNode((void*)(0x2069105), "player2(0x%08X)", 0x2069105))
			{
				GameObjectData data;
				_readGameObjectData(m_memoryBuffer, 0x2069105, data);
				_displayGameObjectData(data);

				ImGui::TreePop();
			}*/

			/*size_t index = 0x02068997;
			size_t initialObject = 0x02028990;
			for (size_t i = 0; i < 144; ++i)
			{
				int16_t objectIndex = _readInt16(m_memoryBuffer, index + (i * 2));
				//ImGui::Text("%d", objectIndex);

				if (objectIndex == -1)
				{
					ImGui::Text("Invalid");
				}
				else
				{

					size_t objectBase = initialObject + (objectIndex << 11);

					uint8_t friends = _readUInt8(m_memoryBuffer, objectBase + 0x1);
					int8_t flipX = _readInt8(m_memoryBuffer, objectBase + 0x0A);
					int16_t posX = _readInt16(m_memoryBuffer, objectBase + 0x64);
					int16_t posY = _readInt16(m_memoryBuffer, objectBase + 0x68);
					uint16_t charID = _readUInt16(m_memoryBuffer, objectBase + 0x3C0);

					char bufID[16];
					sprintf(bufID, "list%d", i);
					if (ImGui::TreeNode(bufID, "object 0x%02X(0x%08X)(%d,%d)", objectIndex, objectBase, posX, posY))
					{
						ImGui::Text("friends: %d", friends);
						ImGui::Text("flipX: %d", flipX);
						ImGui::Text("charID: %d", charID);

						ImGui::TreePop();
					}
					//objectIndex = _readInt16(m_memoryBuffer, objectBase + 0x21);
				}
			}*/
		}
		

		m_lock->Exit();
	}
	else
	{
	}

	if (m_showDemoWindow)
		ImGui::ShowDemoWindow(&m_showDemoWindow);

	/*bool showMemoryDebugger = m_applicationData.showMemoryDebugger;
	if (showMemoryDebugger)
	{
		_updateMemoryDebugger(&showMemoryDebugger);
	}
	if (showMemoryDebugger != m_applicationData.showMemoryDebugger)
	{
		m_applicationData.showMemoryDebugger = showMemoryDebugger;
		_saveApplicationData();
	}*/

	bool showMemoryMap = m_applicationData.showMemoryMap;
	if (showMemoryMap)
	{
		SIZE_T bytesRead = 0;
		ReadProcessMemory(m_FBAProcessHandle, (void*)m_ramStartingAddress, m_memoryBuffer, SF33_MAXADDRESS, &bytesRead);
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

	ImGui::PopStyleColor();
}

void TrainingApplication::watchFrameChange()
{
	while (!m_isDettachRequested)
	{
		bool lockTaken = false;
		m_lock->TryEnter(-1, lockTaken);
		assert(lockTaken);

		uint32_t currentFrame = 0;
		_readData(memoryMap::frameNumber, &currentFrame, 4, false);
		if (currentFrame != m_currentFrame)
		{
			if (m_currentFrame != -1 && currentFrame != m_currentFrame + 1)
			{
				LOG_ERROR("Missed %d frames", (currentFrame - m_currentFrame) - 1)
			}

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
			LOG_ERROR("Error: Failed to find FBA process.");

		return;
	}

	m_ramStartingAddress = FindRAMStartAddress(m_FBAProcessHandle);
	if (!m_ramStartingAddress)
	{
		if (!m_applicationData.autoAttach)
			LOG_ERROR("Error: Failed to find RAM starting address.");

		return;
	}

	m_currentFrame = -1;

	_calibrateP2InputMapping();

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

void TrainingApplication::_sanitizeAddressString(char* _string, size_t _digitCount) const
{
	char tempString[128];
	char* xLocation = strstr(_string, "x");
	char* relevantString = _string;
	if (xLocation)
	{
		relevantString = xLocation + 1;
	}
	size_t len = strlen(relevantString);
	assert(len < 128);
	strcpy(tempString, relevantString);

	_string[0] = '0';
	_string[1] = 'x';

	int missingDigitCount = _digitCount - len;
	if (missingDigitCount > 0)
	{
		memset(_string + 2, '0', missingDigitCount);
	}
	else
	{
		missingDigitCount = 0;
	}
	memcpy(_string + 2 + missingDigitCount, tempString + len - (_digitCount - missingDigitCount), (_digitCount - missingDigitCount));
	_string[_digitCount + 2] = '\0';
}

void TrainingApplication::_writeByte(size_t _address, uint8_t _byte)
{
	assert(_isAttachedToFBA());
	WriteProcessMemory(m_FBAProcessHandle, (void*)(m_ramStartingAddress + _address), &_byte, 1, nullptr);
}


void TrainingApplication::_writeData(size_t _address, void* _data, size_t _dataSize)
{
	assert(_isAttachedToFBA());
	WriteProcessMemory(m_FBAProcessHandle, (void*)(m_ramStartingAddress + _address), _data, _dataSize, nullptr);
}

void TrainingApplication::_readData(size_t _address, void* _data, size_t _dataSize, bool _reverse)
{
	assert(_isAttachedToFBA());
	ReadProcessMemory(m_FBAProcessHandle, (void*)(m_ramStartingAddress + _address), _data, _dataSize, nullptr);

	if (_reverse)
	{
		uint8_t* data = reinterpret_cast<uint8_t*>(_data);
		for (size_t i = 0u; i < _dataSize / 2; ++i)
		{
			uint8_t byte = data[i];
			data[i] = data[_dataSize - 1 - i];
			data[_dataSize - 1 - i] = byte;
		}
	}
}

int8_t TrainingApplication::_readInt8(void* _memory, size_t _address)
{
	int8_t value;
	memcpy(&value, reinterpret_cast<uint8_t*>(_memory) + _address, 1);
	return value;
}

uint8_t TrainingApplication::_readUInt8(void* _memory, size_t _address)
{
	uint8_t value;
	memcpy(&value, reinterpret_cast<uint8_t*>(_memory) + _address, 1);
	return value;
}

int16_t TrainingApplication::_readInt16(void* _memory, size_t _address)
{
	int16_t value;
	memcpy(&value, reinterpret_cast<uint8_t*>(_memory) + _address, 2);
	//_copyReverse(reinterpret_cast<uint8_t*>(_memory) + _address, &value, 2);
	return value;
}

uint16_t TrainingApplication::_readUInt16(void* _memory, size_t _address)
{
	uint16_t value;
	memcpy(&value, reinterpret_cast<uint8_t*>(_memory) + _address, 2);
	//_copyReverse(reinterpret_cast<uint8_t*>(_memory) + _address, &value, 2);
	return value;
}

size_t TrainingApplication::_incrementAddress(size_t _address, int64_t _increment, size_t _minAddress /*= 0*/, size_t _maxAddress /*= SF33_MAXADDRESS*/)
{
	if (_increment < _minAddress && (_increment * -1) > _address)
		return _minAddress;
	else
	{
		_address += size_t(_increment);
		if (_address > _maxAddress)
			_address = _maxAddress;

		return _address;
	}
}

void TrainingApplication::_requestMemoryWrite(size_t _address, void* _data, size_t _dataSize, bool _reverse /*= true*/)
{
	MemoryWriteRequest request;
	request.address = _address;
	request.data = malloc(_dataSize);
	request.dataSize = _dataSize;

	if (_reverse)
	{
		_copyReverse(_data, request.data, request.dataSize);
	}
	else
	{
		memcpy(request.data, _data, request.dataSize);
	}

	m_memoryWriteRequests.push_back(request);
}

void TrainingApplication::_resolveMemoryWriteRequests()
{
	for (MemoryWriteRequest& request : m_memoryWriteRequests)
	{
		WriteProcessMemory(m_FBAProcessHandle, (void*)(m_ramStartingAddress + request.address), request.data, request.dataSize, nullptr);
		free(request.data);
	}

	m_memoryWriteRequests.clear();
}

void TrainingApplication::_copyReverse(const void* _src, void* _dst, size_t _size)
{
	const char* src = reinterpret_cast<const char*>(_src);
	char* dst = reinterpret_cast<char*>(_dst);
	for (size_t i = 0; i < _size; ++i)
	{
		dst[i] = src[_size - 1 - i];
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

void TrainingApplication::_updateMemoryDebugger(bool* _showMemoryDebugger)
{
	char buf[256] = {};
	char addressBuffer[11] = {};

	ImVec2 addressSize = ImGui::CalcTextSize("0x00000000");
	ImVec2 byteSize = ImGui::CalcTextSize("00");

	//ImGui::SetNextWindowSize(ImVec2(totalSize.x + 16.f, totalSize.y + 16.f + 38.f));
	//ImGui::Begin("Memory Debugger", _showMemoryDebugger, /*ImGuiWindowFlags_NoResize | */ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_HorizontalScrollbar);
	
	/*if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("Options"))
		{
			char buf[11];
			sprintf(buf, "0x%08X\0", m_applicationData.pageUpDownIncrement);
			if (ImGui::InputText("Page Up/Down increment", buf, 11))
			{
				_sanitizeAddressString(buf, 8);

				m_applicationData.pageUpDownIncrement = size_t(strtol(buf, nullptr, 0));
				_saveApplicationData();
			}

			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}*/

	if (!_isAttachedToFBA())
	{
		ImGui::Text("Not attached to FBA");
	}
	else
	{
		static const float detailsColumnWidth = 190.f;

		ImGui::BeginChild("details", ImVec2(detailsColumnWidth, 0.f), false, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);

		// BASE ADDRESS
		{
			ImGui::PushItemWidth(addressSize.x + 12.f);
			sprintf(addressBuffer, "0x%08X\0", m_applicationData.memoryDebuggerData.address);
			if (ImGui::InputText("Address", addressBuffer, 11, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				_sanitizeAddressString(addressBuffer, 8);

				m_applicationData.memoryDebuggerData.address = size_t(strtol(addressBuffer, nullptr, 0));
				m_applicationData.memoryDebuggerData.address -= m_applicationData.memoryDebuggerData.address % 0x10;

				_saveApplicationData();
			}
			ImGui::PopItemWidth();
		}

		ImGui::Separator();

		// MEMORY LABELS
		{
			ImGui::PushStyleColor(ImGuiCol_ChildWindowBg, IM_COL32(255, 255, 255, 20));
			ImGui::BeginChild("MemoryLabels", ImVec2(ImGui::GetWindowContentRegionWidth(), 350.f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
			static std::vector<MemoryLabel> s_memoryLabels;
			s_memoryLabels = m_memoryLabels;
			for (size_t i = 0; i < s_memoryLabels.size(); ++i)
			{
				MemoryLabel& label = s_memoryLabels[i];
				if (ImGui::Selectable(label.name.c_str(), i == m_selectedLabel))
				{
					if (m_selectedLabel != i)
					{
						m_selectedLabel = i;

						m_applicationData.memoryDebuggerData.address = s_memoryLabels[m_selectedLabel].beginAddress;
						m_applicationData.memoryDebuggerData.address = _incrementAddress(m_applicationData.memoryDebuggerData.address, -0x40);
						m_applicationData.memoryDebuggerData.address -= m_applicationData.memoryDebuggerData.address % 0x10;
					}
					else
					{
						m_selectedLabel = -1;
					}
				}
				char buf[32];
				sprintf(buf, "memorylabel%d", i);
				if (ImGui::BeginPopupContextItem(buf))
				{
					if (ImGui::InputText("name", &m_memoryLabels[i].name, ImGuiInputTextFlags_EnterReturnsTrue))
					{
						_saveMemoryLabels();
					}

					ImGui::Separator();
					if (ImGui::Button("^"))
					{
						if (i > 0)
						{
							m_memoryLabels[i - 1] = s_memoryLabels[i];
							m_memoryLabels[i] = s_memoryLabels[i - 1];
							_saveMemoryLabels();
							ImGui::CloseCurrentPopup();
						}
					}
					ImGui::SameLine();
					if (ImGui::Button("v"))
					{
						if (i < s_memoryLabels.size() - 1)
						{
							m_memoryLabels[i + 1] = s_memoryLabels[i];
							m_memoryLabels[i] = s_memoryLabels[i + 1];
							_saveMemoryLabels();
							ImGui::CloseCurrentPopup();
						}
					}
					ImGui::Separator();
					if (ImGui::Selectable("Delete"))
					{
						m_memoryLabels.erase(m_memoryLabels.begin() + i);
						_saveMemoryLabels();
					}
					ImGui::EndPopup();
				}
			}
			ImGui::EndChild();
			ImGui::PopStyleColor();
		}
		
		ImGui::EndChild();

		ImGui::SameLine(0.0f, 15.f);

		size_t address = m_applicationData.memoryDebuggerData.address;
		_drawMemory(m_memoryBuffer, m_applicationData.memoryDebuggerData, m_memoryLabels, m_selectedLabel);
		if (address != m_applicationData.memoryDebuggerData.address)
		{
			_saveApplicationData();
		}
	}

	//ImGui::End();
}

void TrainingApplication::_saveApplicationData()
{
	mirror::SaveToFile(m_applicationData, s_applicationDataFileName);
}

void TrainingApplication::_loadApplicationData()
{
	mirror::LoadFromFile(m_applicationData, s_applicationDataFileName);

	SetWindowPos(m_windowHandle, nullptr, m_applicationData.windowX, m_applicationData.windowY, m_applicationData.windowW, m_applicationData.windowH, SWP_NOZORDER);
}

void TrainingApplication::_saveTrainingData()
{
	mirror::SaveToFile(m_trainingData, s_trainingDataFileName);
}

void TrainingApplication::_loadTrainingData()
{
	mirror::LoadFromFile(m_trainingData, s_trainingDataFileName);
}

void TrainingApplication::_saveMemoryLabels()
{
	mirror::SaveToFile(m_memoryLabels, s_memoryLabelsFileName);
}

void TrainingApplication::_loadMemoryLabels()
{
	mirror::LoadFromFile(m_memoryLabels, s_memoryLabelsFileName);
}

void TrainingApplication::_calibrateP2InputMapping()
{
	assert(m_FBAProcessHandle);
	
	char filename[MAX_PATH];
	sprintf_s(filename, "%ws", PtrToStringChars(m_FBAProcess->MainModule->FileName));

	size_t pathLen = strlen(filename);
	bool foundRoot = false;

	for (int i = pathLen - 1; i > 0; --i)
	{
		if (filename[i] == '\\')
		{
			filename[i + 1] = 0;
			foundRoot = true;
			break;
		}
	}
	if (!foundRoot)
	{
		LOG_ERROR("Failed to find Fightcade root folder.");
		return;
	}

	char configFilePath[MAX_PATH];
	sprintf(configFilePath, "%sconfig\\games\\sfiii3n.ini", filename);

	FILE* fp = nullptr;
	if (fopen_s(&fp, configFilePath, "r") != 0)
	{
		LOG_ERROR("Failed to open \"%s\".", configFilePath);
		return;
	}
	fseek(fp, 0, SEEK_END);
	size_t dataSize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	char* data = (char*)malloc(dataSize);
	fread(data, dataSize, 1, fp);
	fclose(fp);

	auto seekInputKeyCode = [](const char* _seekInput, const char* _data, size_t _dataSize) -> int
	{
		const char* cursor = strstr(_data, _seekInput);
		if (!cursor)
			return 0x00;

		const char* lineEnd = strstr(cursor, "\n");
		if (!lineEnd)
			lineEnd = _data + _dataSize;

		cursor = strstr(cursor, "switch");
		if (!cursor || cursor > lineEnd)
			return 0x00;

		return DInputKeyCodeToVirtualKeyCode(static_cast<int>(strtol(cursor + 7, nullptr, 0)));
	};

	m_p2Keys[GameInput_Coin] = seekInputKeyCode("P2 Coin", data, dataSize);
	m_p2Keys[GameInput_Start] = seekInputKeyCode("P2 Start", data, dataSize);
	m_p2Keys[GameInput_Up] = seekInputKeyCode("P2 Up", data, dataSize);
	m_p2Keys[GameInput_Down] = seekInputKeyCode("P2 Down", data, dataSize);
	m_p2Keys[GameInput_Left] = seekInputKeyCode("P2 Left", data, dataSize);
	m_p2Keys[GameInput_Right] = seekInputKeyCode("P2 Right", data, dataSize);
	m_p2Keys[GameInput_LP] = seekInputKeyCode("P2 Weak punch", data, dataSize);
	m_p2Keys[GameInput_MP] = seekInputKeyCode("P2 Medium punch", data, dataSize);
	m_p2Keys[GameInput_HP] = seekInputKeyCode("P2 Strong punch", data, dataSize);
	m_p2Keys[GameInput_LK] = seekInputKeyCode("P2 Weak kick", data, dataSize);
	m_p2Keys[GameInput_MK] = seekInputKeyCode("P2 Medium kick", data, dataSize);
	m_p2Keys[GameInput_HK] = seekInputKeyCode("P2 Strong kick", data, dataSize);

	free(data);
}

void TrainingApplication::_readGameObjectData(void* _memory, size_t _address, GameObjectData& _data)
{
	_data.address = _address;

	_data.friends = _readUInt8(_memory, _address + 0x1);
	_data.flipX = _readInt8(_memory, _address + 0x0A);
	_data.posX = _readInt16(_memory, _address + 0x64);
	_data.posY = _readInt16(_memory, _address + 0x68);
	_data.charID = _readUInt16(_memory, _address + 0x3C0);
	_data.validObject = _readUInt16(_memory, _address + 0x2A0);

	//0x29A
	//0x02068F07 - 0x02068C6D
	for (size_t i = 0; i < 22; ++i)
	{
		for (size_t j = 0; j < 4; ++j)
		{
			_data.hitboxes[i][j] = _readInt16(_memory, _address + 0x29A + (i * 8) + (j * 2));
		}
	}
	/*0x02068F07 / 0x02068FBA

	{offset = 0x2D4, type = "push"},
	{ offset = 0x2C0, type = "throwable" },
	{ offset = 0x2A0, type = "vulnerability", number = 4 },
	{ offset = 0x2A8, type = "ext. vulnerability", number = 4 },
	{ offset = 0x2C8, type = "attack", number = 4 },
	{ offset = 0x2B8, type = "throw" },
	*/
}

void TrainingApplication::_displayGameObjectData(const GameObjectData& _data)
{
	ImGui::Text("friends(0x%08X): 0x%02X", _data.address + 0x1, _data.friends);
	ImGui::Text("flipX(0x%08X): 0x%02X", _data.address + 0x0A, _data.flipX);
	ImGui::Text("posX(0x%08X): 0x%04X", _data.address + 0x64, _data.posX);
	ImGui::Text("posY(0x%08X): 0x%04X", _data.address + 0x68, _data.posY);
	ImGui::Text("charID(0x%08X): 0x%04X", _data.address + 0x3C0, _data.charID);
	ImGui::Text("validObject(0x%08X): 0x%04X", _data.address + 0x2A0, _data.validObject);

	for (size_t i = 0; i < 22; ++i)
	{
		ImGui::Text("Hitbox %d: %d, %d, %d, %d", i, _data.hitboxes[i][0], _data.hitboxes[i][1], _data.hitboxes[i][2], _data.hitboxes[i][3]);
	}
}

void TrainingApplication::_drawMemory(void* _memory, MemoryDisplayData& _data, std::vector<MemoryLabel>& _labels, int _selectedLabel)
{
	static const size_t colCount = 0x10;

	static const float rowMarginHeight = 4.f;
	static const float addressesMarginWidth = 15.f;
	static const float byteMarginWidth = 5.f;
	static const float centralMarginWidth = 15.f;

	char buf[256] = {};

	ImVec2 addressSize = ImGui::CalcTextSize("0x00000000");
	ImVec2 byteSize = ImGui::CalcTextSize("00");
	float rowHeight = byteSize.y + rowMarginHeight;
	size_t rowCount = size_t(ImGui::GetContentRegionAvail().y / rowHeight);
	size_t bytesToRead = colCount * rowCount;
	ImVec2 size = ImVec2(addressSize.x + addressesMarginWidth + colCount * (byteSize.x + byteMarginWidth) + centralMarginWidth, rowHeight * rowCount);

	ImGui::BeginChild("memory", size, false, ImGuiWindowFlags_NoResize);

	// MEMORY TABLE
	ImVec2 basePos = ImGui::GetCursorScreenPos();

	ImVec2 firstTableBasePos = ImVec2(basePos.x + addressSize.x + addressesMarginWidth, basePos.y);
	ImVec2 secondTableBasePos = ImVec2(firstTableBasePos.x + (byteSize.x + byteMarginWidth) * 0x8 + centralMarginWidth, basePos.y);
	ImVec2 tableSize = ImVec2((byteSize.x + byteMarginWidth) * 0x8, rowHeight * rowCount);

	auto computeAddressAtPos = [=](const ImVec2& _pos) -> size_t
	{
		size_t address = ADDRESS_UNDEFINED;
		if (_pos.x >= firstTableBasePos.x && _pos.x <= firstTableBasePos.x + tableSize.x && _pos.y >= firstTableBasePos.y && _pos.y <= firstTableBasePos.y + tableSize.y)
		{
			int addressX = int((_pos.x - firstTableBasePos.x) / (byteSize.x + byteMarginWidth));
			int addressY = int((_pos.y - firstTableBasePos.y) / (byteSize.y + rowMarginHeight));

			if (_pos.x <= firstTableBasePos.x + addressX * (byteSize.x + byteMarginWidth) + byteSize.x
				&& _pos.y <= firstTableBasePos.y + addressY * (byteSize.y + rowMarginHeight) + byteSize.y)
			{
				address = _data.address + (0x10 * addressY) + addressX;
			}
		}
		else if (_pos.x >= secondTableBasePos.x && _pos.x <= secondTableBasePos.x + tableSize.x && _pos.y >= secondTableBasePos.y && _pos.y <= secondTableBasePos.y + tableSize.y)
		{
			int addressX = int((_pos.x - secondTableBasePos.x) / (byteSize.x + byteMarginWidth));
			int addressY = int((_pos.y - secondTableBasePos.y) / (byteSize.y + rowMarginHeight));

			if (_pos.x <= secondTableBasePos.x + addressX * (byteSize.x + byteMarginWidth) + byteSize.x
				&& _pos.y <= secondTableBasePos.y + addressY * (byteSize.y + rowMarginHeight) + byteSize.y)
			{
				address = _data.address + (0x10 * addressY) + addressX + 0x08;
			}
		}
		return address;
	};

	ImVec2 mp = ImGui::GetIO().MousePos;
	size_t hoveredAddress = computeAddressAtPos(mp);
	bool isMouseOverMemory = mp.x >= basePos.x && mp.x <= basePos.x + size.x && mp.y >= basePos.y && mp.y <= basePos.y + size.y;
	bool isMemorySelectionPopupOpen = ImGui::IsPopupOpen("memorySelection");

	// PRECOMPUTE BYTE INFO
	struct ByteInfo
	{
		bool hovered = false;
		bool beingSelected = false;
		bool selected = false;
		std::vector<int> labels;
	};
	static std::vector<ByteInfo> s_byteInfo;
	s_byteInfo.resize(colCount * rowCount);
	for (size_t i = 0; i < s_byteInfo.size(); ++i)
	{
		s_byteInfo[i].hovered = false;
		s_byteInfo[i].beingSelected = false;
		s_byteInfo[i].selected = false;
		s_byteInfo[i].labels.clear();

		size_t address = _data.address + i;

		if (address == hoveredAddress)
			s_byteInfo[i].hovered = true;

		if (address >= _data.selectionBeginAddress && address <= _data.selectionEndAddress)
		{
			if (_data.isDraggingSelection && !isMemorySelectionPopupOpen)
			{
				s_byteInfo[i].beingSelected = true;
			}
			else
			{
				s_byteInfo[i].selected = true;
			}
		}

		for (size_t j = 0; j < _labels.size(); ++j)
		{
			if (address >= _labels[j].beginAddress && address <= _labels[j].endAddress)
				s_byteInfo[i].labels.push_back(j);
		}
	}

	// SELECTION LOGIC
	if (isMouseOverMemory && !isMemorySelectionPopupOpen)
	{
		if (ImGui::IsMouseClicked(0))
		{
			if (hoveredAddress == ADDRESS_UNDEFINED)
			{
				_data.selectionBeginAddress = ADDRESS_UNDEFINED;
				_data.selectionEndAddress = ADDRESS_UNDEFINED;
			}
			else
			{
				_data.isDraggingSelection = true;
				_data.selectionBeginAddress = hoveredAddress;
				_data.selectionEndAddress = hoveredAddress;
			}
		}
		if (_data.isDraggingSelection && _data.selectionBeginAddress != ADDRESS_UNDEFINED && hoveredAddress != ADDRESS_UNDEFINED)
		{
			_data.selectionEndAddress = hoveredAddress;
		}

	}
	if (ImGui::IsMouseReleased(0))
	{
		_data.isDraggingSelection = false;
	}

	// DRAW MEMORY TABLE
	ImGui::InvisibleButton("mem_bg", size);
	ImColor c = IM_COL32(255, 255, 255, 255);
	for (size_t row = 0; row < rowCount; ++row)
	{
		sprintf(buf, "0x%08X", _data.address + row * 0x10);
		ImGui::GetWindowDrawList()->AddText(ImVec2(basePos.x, basePos.y + row * rowHeight), IM_COL32(255, 255, 255, 127), buf);
		ImVec2 pos = ImVec2(basePos.x + addressSize.x + addressesMarginWidth, basePos.y + row * rowHeight);
		for (size_t col = 0; col < colCount; ++col)
		{
			size_t byteRelativeAddress = row * 0x10 + col;
			size_t byteAddress = _data.address + byteRelativeAddress;
			ImVec2 bytePos = ImVec2(pos.x + col * (byteSize.x + byteMarginWidth) + (col >= 0x8 ? centralMarginWidth : 0.f), pos.y);

			ImVec2 rectA = bytePos;
			ImVec2 rectB = ImVec2(bytePos.x + byteSize.x, bytePos.y + byteSize.y);
			if (s_byteInfo[byteRelativeAddress].hovered)
			{
				ImGui::GetWindowDrawList()->AddRectFilled(rectA, rectB, IM_COL32(255, 255, 255, 40));
			}
			if (s_byteInfo[byteRelativeAddress].beingSelected)
			{
				ImGui::GetWindowDrawList()->AddRectFilled(rectA, rectB, IM_COL32(255, 255, 255, 110));
			}
			if (s_byteInfo[byteRelativeAddress].selected)
			{
				ImGui::GetWindowDrawList()->AddRectFilled(rectA, rectB, IM_COL32(0, 255, 255, 110));
			}



			for (size_t i = 0; i < s_byteInfo[byteRelativeAddress].labels.size(); ++i)
			{
				ImU32 col = 0;
				if (s_byteInfo[byteRelativeAddress].labels[i] == _selectedLabel)
					col = IM_COL32(255, 255, 0, 130);
				else
					col = IM_COL32(255, 255, 0, 70);

				ImGui::GetWindowDrawList()->AddRectFilled(rectA, rectB, col);
			}

			sprintf(buf, "%02X", ((uint8_t*)_memory)[byteAddress]);
			ImGui::GetWindowDrawList()->AddText(bytePos, IM_COL32(255, 255, 255, 255), buf);

			if (s_byteInfo[byteRelativeAddress].hovered && !s_byteInfo[byteRelativeAddress].labels.empty())
			{
				ImGui::BeginTooltip();
				for (size_t i = 0; i < s_byteInfo[byteRelativeAddress].labels.size(); ++i)
				{
					ImGui::Text(_labels[s_byteInfo[byteRelativeAddress].labels[i]].name.c_str());
				}
				ImGui::EndTooltip();
			}
		}
	}

	if (ImGui::IsMouseReleased(1) && _data.selectionBeginAddress != ADDRESS_UNDEFINED && hoveredAddress >= _data.selectionBeginAddress && hoveredAddress <= _data.selectionEndAddress)
	{
		ImGui::OpenPopup("memorySelection");
	}
	if (ImGui::BeginPopup("memorySelection"))
	{
		char addressBuffer[11] = {};
		size_t selectionLength = (_data.selectionEndAddress - _data.selectionBeginAddress) + 1;
		if (selectionLength <= 4)
		{
			uint32_t data = 0u;
			_copyReverse(reinterpret_cast<uint8_t*>(_memory) + _data.selectionBeginAddress, reinterpret_cast<uint8_t*>(&data), selectionLength);
			sprintf(addressBuffer, "0x%08X\0", data);
			_sanitizeAddressString(addressBuffer, selectionLength * 2);
			if (ImGui::InputText("data", addressBuffer, selectionLength * 2 + 3, ImGuiInputTextFlags_EnterReturnsTrue))
			{
				data = uint32_t(strtol(addressBuffer, nullptr, 0));
				_requestMemoryWrite(_data.selectionBeginAddress, &data, selectionLength);
			}
		}
		else
		{
			ImGui::TextColored(ImVec4(1.f, 1.f, 1.f, .2f), "Selection too large to be edited.");
		}
		ImGui::Separator();

		ImGui::PushItemWidth(addressSize.x + 12.f);
		sprintf(addressBuffer, "0x%08X\0", _data.selectionBeginAddress);
		ImGui::PushID("SelectionBegin");
		if (ImGui::InputText("", addressBuffer, 11, ImGuiInputTextFlags_EnterReturnsTrue))
		{
			_sanitizeAddressString(addressBuffer, 8);
			_data.selectionBeginAddress = size_t(strtol(addressBuffer, nullptr, 0));
		}
		ImGui::PopID();

		ImGui::SameLine();
		ImGui::Text(":");
		ImGui::SameLine();

		sprintf(addressBuffer, "0x%08X\0", _data.selectionEndAddress);
		ImGui::PushID("SelectionEnd");
		if (ImGui::InputText("", addressBuffer, 11, ImGuiInputTextFlags_EnterReturnsTrue))
		{
			_sanitizeAddressString(addressBuffer, 8);
			_data.selectionEndAddress = size_t(strtol(addressBuffer, nullptr, 0));
			if (_data.selectionEndAddress < _data.selectionBeginAddress)
				_data.selectionEndAddress = _data.selectionBeginAddress;
		}
		ImGui::PopID();
		ImGui::PopItemWidth();

		ImGui::PushItemWidth(150.f);
		ImGui::InputText("Label", _data.labelBuffer, 127);
		ImGui::PopItemWidth();

		if (ImGui::Button("New Memory Label"))
		{
			MemoryLabel label;
			label.name = _data.labelBuffer;
			label.beginAddress = _data.selectionBeginAddress;
			label.endAddress = _data.selectionEndAddress;
			m_memoryLabels.push_back(label);

			_saveMemoryLabels();

			*_data.labelBuffer = 0;

			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
	else
	{
		*_data.labelBuffer = 0;
	}

	ImGui::EndChild();

	// SCROLL
	if (isMouseOverMemory)
	{
		float mouseWheel = ImGui::GetIO().MouseWheel;
		if (mouseWheel < 0.f)
		{
			_data.address = _incrementAddress(_data.address, 0x10);
			_saveApplicationData();
		}
		else if (mouseWheel > 0.f)
		{
			_data.address = _incrementAddress(_data.address, -0x10);
			_saveApplicationData();
		}
	}
	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_PageUp)))
	{
		_data.address = _incrementAddress(_data.address, -(int32_t)(rowCount * 0x10));
	}
	if (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_PageDown)))
	{
		_data.address = _incrementAddress(_data.address, (int32_t)(rowCount * 0x10));
	}
}
