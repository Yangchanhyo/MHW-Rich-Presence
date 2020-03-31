#include <iostream>
#include "discord/discord.h"
#include <Windows.h>
#include <memory.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <string>
#include "QuestData.h"
#include "Player.h"

///
// Discord.
///
discord::Core* core{};

///
// Application management.
///
int waittime    = 0
   ,updatetimer = 0
   ,loopnumber  = 0
   ,process_id;

///
// Storage for retrieved memory values.
///
QuestData quest{};
Player player{};

///
// Process information
///
LPCWSTR process_name = L"MonsterHunterWorld.exe";
HANDLE mhw_handle = NULL;
boolean checking = true;

///
// Pointer information
///
long long MHW_PTR     = 0x140000000 + 0x04EA20A8
	     ,START_INDEX = 0xA4100080
		 ,END_INDEX   = 0xA7100000
		 ,BASE_ADDRESS;

///
// This generates a new core to display Rich Presence for use during the application's runtime.
///
void InitializeDiscord()
{
	discord::Core::Create(666709854622187570, DiscordCreateFlags_Default, &core);
}

///
// This sends an update tick to the Discord RPC (Must be run after InitializeDiscord()).
///
void UpdateDiscord()
{
	std::cout << "Updating Discord" << std::endl;
	std::cout << "Is in quest: " << player.is_in_quest() << std::endl;

	discord::Activity activity{}; // A blank object to send to the Discord RPC.

	///
	// Generate and format strings for use in the Rich Presence.
	///
	std::string details = (player.is_in_quest() == true ? "In Quest" : (std::string)"Chillin' in the Hub");
	std::string state   = player.get_name() + " -- HR/MR: " + std::to_string((int)player.get_hunter_rank()) + "/" + std::to_string((int)player.get_master_rank());
	std::string map     = "";

	///
	// Apply the image assets.
	///
	activity.GetAssets().SetLargeImage("astera");
	activity.GetAssets().SetLargeText("In Astera");

	///
	// Apply the state and details to the activity object.
	///
	activity.SetDetails(details.c_str());
	activity.SetState(state.c_str());

	core->ActivityManager().UpdateActivity(activity, [](discord::Result result) {}); // Update the Discord status.
}

///
// Locate a process from a provided process name. Will return 0 in the case that there is no process by that name or if it fails.
///
DWORD FindProcessId(const std::wstring& processName)
{
	///
	// Create a ProcessEntry, figure out its' size, and make a new snapshot.
	///
	PROCESSENTRY32 processInfo;
	HANDLE         processesSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
	processInfo.dwSize = sizeof(processInfo);

	if (processesSnapshot == INVALID_HANDLE_VALUE) // If the handle represented by the snapshot tool is invalid, then fail.
		return 0;

	///
	// Obtain the first process visible to the snapshot, and check if the requested name is the same as the file, return its' ID if so.
	///
	Process32First(processesSnapshot, &processInfo);
	if (!processName.compare(processInfo.szExeFile)) {
		CloseHandle(processesSnapshot);
		return processInfo.th32ProcessID;
	}

	///
	// Check every other process for the one with the requested name until it finds it or runs out of processes.
	///
	while (Process32Next(processesSnapshot, &processInfo)) {
		if (!processName.compare(processInfo.szExeFile)) {
			CloseHandle(processesSnapshot);
			process_id = processInfo.th32ProcessID;
			return processInfo.th32ProcessID;
		}
	}

	CloseHandle(processesSnapshot); // IMPORTANT, keeps from memory leakage.
	return 0;
}

///
// Obtain the base memory address for Monster Hunter World.
///
DWORD_PTR GetProcessBaseAddress()
{
	// This structure contains lots of goodies about a module
	MODULEENTRY32 ModuleEntry = { 0 };
	// Grab a snapshot of all the modules in the specified process
	HANDLE SnapShot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, process_id);

	if (!SnapShot)
		return NULL;

	// You have to initialize the size, otherwise it will not work
	ModuleEntry.dwSize = sizeof(ModuleEntry);

	// Get the first module in the process
	if (!Module32First(SnapShot, &ModuleEntry))
		return NULL;

	do {
		// Check if the module name matches the one we're looking for
		if (!wcscmp(ModuleEntry.szModule, process_name)) {
			// If it does, close the snapshot handle and return the base address
			CloseHandle(SnapShot);
			return (DWORD_PTR)ModuleEntry.modBaseAddr;
		}
		// Grab the next module in the snapshot
	} while (Module32Next(SnapShot, &ModuleEntry));

	// We couldn't find the specified module, so return NULL
	CloseHandle(SnapShot);
	return NULL;
}

///
// Hook onto the Monster Hunter: World process if possible.
///
void Hook()
{
	mhw_handle = OpenProcess(PROCESS_ALL_ACCESS, true, FindProcessId(process_name));
	std::cout << (mhw_handle == NULL ? "Failed to hook onto " : "Successfully hooked onto ") << process_name << "!" << std::endl;
	checking = false; // Tell the system that it's not searching anymore.
}

///
// Return whether or not Monster Hunter: World is ACTUALLY RUNNING.
///
bool IsMHWRunning()
{
	return OpenProcess(PROCESS_ALL_ACCESS, true, FindProcessId(process_name)) != NULL;
}

///
// Make a valiant effort to hook onto Monster Hunter: World, and retry every 10 seconds until it hooks.
///
void AttemptHook()
{
	std::cout << "Attempting to hook onto " << process_name << "..." << std::endl;

	if (IsMHWRunning() == false) {
		std::cout << "Failed to hook onto " << process_name << "! Waiting for process..." << std::endl;

		while (checking) {
			waittime++;

			if (waittime >= 10000000) {
				waittime = 0;

				if (IsMHWRunning() == true)
					Hook();
			}
		}
	}
	else
		Hook();
}

///
// Read the memory of the current player, and store the information within the player object.
///
void ReadMemory()
{
	int hunter_rank,
		master_rank,
		session_duration = 0;

	long long current_quest = 0;

	char hunter_name[20];
	
	ReadProcessMemory(mhw_handle, (LPCVOID)(BASE_ADDRESS+0x90), &hunter_rank, sizeof(hunter_rank), NULL); // Obtain memory value for HR.
	ReadProcessMemory(mhw_handle, (LPCVOID)(BASE_ADDRESS+0x50), &hunter_name, sizeof(hunter_name), NULL); // Obtain memory value for name.
	ReadProcessMemory(mhw_handle, (LPCVOID)(BASE_ADDRESS+0xD4), &master_rank, sizeof(master_rank), NULL); // Obtain memory value for MR.

	player.set_data(hunter_name != NULL ? hunter_name : "Cross", hunter_rank, master_rank, session_duration, current_quest != 0);
	std::cout << player.get_name() << " -- HR/MR " << player.get_hunter_rank() << "/" << player.get_master_rank() << " >> " << "Quest: " << current_quest << " Last Session Ping/Current: " << player.get_last_session_time() << "/" << player.get_session_time() << std::endl;
}

///
// Find the player memory address.
///
void FindPlayerIndex()
{
	for (long long address = START_INDEX; address < END_INDEX; address+=0x1000) {
		int byteArray = 0;
		ReadProcessMemory(mhw_handle, (LPCVOID)address, &byteArray, sizeof(byteArray), NULL);

		if (byteArray == 1125346736) {
			BASE_ADDRESS = address;
			std::cout << "Found player memory address" << std::endl;
			break;
		}
	}
}

///
// This is a loop that fires every couple seconds.
///
void ApplicationLoop()
{
	loopnumber++;
	std::cout << "Application Loop " << loopnumber << "\n" << std::endl;

	ReadMemory();
	::core->RunCallbacks();
}

///
// Initialize Discord, attempt to hook the game, and begin the application loop.
///
int main()
{
	InitializeDiscord();
	AttemptHook();
	FindPlayerIndex();

	while (true) {
		Sleep(2000);
		ApplicationLoop();
		UpdateDiscord();
	}
}