#include "Utils/MemoryMgr.h"
#include "Utils/Patterns.h"

#include "pure3d.h"

#include <set>

static void* (*orgMalloc)(size_t size);
void* scarMalloc( size_t size )
{
	return std::invoke( orgMalloc, size );
}

namespace INISettings
{
	bool WriteSetting( const char* key, int value )
	{
		char buffer[32];
		sprintf_s( buffer, "%d", value );
		return WritePrivateProfileStringA( "Scarface", key, buffer, ".\\settings.ini" ) != FALSE;
	}

	int ReadSetting( const char* key )
	{
		return GetPrivateProfileIntA( "Scarface", key, -1, ".\\settings.ini" );
	}

	float ReadFloatSetting(const char* key)
	{
		float value = 0.0f;
		char buffer[32];
		GetPrivateProfileStringA( "Scarface", key, "0.0", buffer, sizeof(buffer), ".\\settings.ini" );
		sscanf_s(buffer, "%f", &value);
		return value;
	}
}

namespace ListAllResolutions
{
	static void (*orgInvokeScriptCommand)(const char* format, ...);

	static void** pUnknown3dStruct; // Used to get the D3D device
	static IDirect3D9* GetD3D()
	{
		// Ugly, but those structs are totally unknown at the moment
		void* ptr1 = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(pUnknown3dStruct) + 4);
		void* d3dDisplay = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(ptr1) + 232);
		if ( d3dDisplay != nullptr )
		{
			return *reinterpret_cast<IDirect3D9**>(reinterpret_cast<uintptr_t>(d3dDisplay) + 12);
		}
		return nullptr;
	}

	// Original function only lists a set of supported resolutions - list them all instead
	static void ListResolutions()
	{
		IDirect3D9* d3d = GetD3D();
		if ( d3d != nullptr )
		{
			// Filters out duplicates and sorts everything for us
			std::set< std::pair<UINT, UINT> > resolutionsSet;

			for ( UINT i = 0, j = d3d->GetAdapterModeCount( 0, D3DFMT_X8R8G8B8 ); i < j; i++ )
			{
				D3DDISPLAYMODE mode;
				if ( SUCCEEDED(d3d->EnumAdapterModes( 0, D3DFMT_X8R8G8B8, i, &mode )) )
				{
					resolutionsSet.emplace( mode.Width, mode.Height );
				}
			}

			for ( auto& it : resolutionsSet )
			{
				orgInvokeScriptCommand( "AddScreenResolutionEntry( %u, %u, %u );", it.second, it.first, 32 );
			}
		}
	}
}

// Based very heavily on a fix for a similar issue in NFS Underground 2
// https://github.com/ThirteenAG/WidescreenFixesPack/pull/1045
// by CrabJournal
namespace AffinityChanges
{
	DWORD_PTR gameThreadAffinity = 0;
	static bool Init()
	{
		DWORD_PTR processAffinity, systemAffinity;
		if (!GetProcessAffinityMask(GetCurrentProcess(), &processAffinity, &systemAffinity))
		{
			return false;
		}

		DWORD_PTR otherCoresAff = (processAffinity - 1) & processAffinity;
		if (otherCoresAff == 0) // Only one core is available for the game
		{
			return false;
		}
		gameThreadAffinity = processAffinity & ~otherCoresAff;

		SetThreadAffinityMask(GetCurrentThread(), gameThreadAffinity);

		return true;
	}

	static HANDLE WINAPI CreateThread_GameThread(LPSECURITY_ATTRIBUTES lpThreadAttributes, SIZE_T dwStackSize, LPTHREAD_START_ROUTINE lpStartAddress,
			PVOID lpParameter, DWORD dwCreationFlags, LPDWORD lpThreadId)
	{
		HANDLE hThread = CreateThread(lpThreadAttributes, dwStackSize, lpStartAddress, lpParameter, dwCreationFlags | CREATE_SUSPENDED, lpThreadId);
		if (hThread != nullptr)
		{
			SetThreadAffinityMask(hThread, gameThreadAffinity);
			if ((dwCreationFlags & CREATE_SUSPENDED) == 0) // Resume only if the game didn't pass CREATE_SUSPENDED
			{
				ResumeThread(hThread);
			}
		}
		return hThread;
	}

	static void ReplaceFunction(void** funcPtr)
	{
		DWORD dwProtect;

		VirtualProtect(funcPtr, sizeof(*funcPtr), PAGE_READWRITE, &dwProtect);
		*funcPtr = &CreateThread_GameThread;
		VirtualProtect(funcPtr, sizeof(*funcPtr), dwProtect, &dwProtect);
	}

	static bool RedirectImports()
	{
		const DWORD_PTR instance = reinterpret_cast<DWORD_PTR>(GetModuleHandle(nullptr));
		const PIMAGE_NT_HEADERS ntHeader = reinterpret_cast<PIMAGE_NT_HEADERS>(instance + reinterpret_cast<PIMAGE_DOS_HEADER>(instance)->e_lfanew);

		// Find IAT
		PIMAGE_IMPORT_DESCRIPTOR pImports = reinterpret_cast<PIMAGE_IMPORT_DESCRIPTOR>(instance + ntHeader->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

		for ( ; pImports->Name != 0; pImports++ )
		{
			if ( _stricmp(reinterpret_cast<const char*>(instance + pImports->Name), "kernel32.dll") == 0 )
			{
				if ( pImports->OriginalFirstThunk != 0 )
				{
					const PIMAGE_THUNK_DATA pThunk = reinterpret_cast<PIMAGE_THUNK_DATA>(instance + pImports->OriginalFirstThunk);

					for ( ptrdiff_t j = 0; pThunk[j].u1.AddressOfData != 0; j++ )
					{
						if ( strcmp(reinterpret_cast<PIMAGE_IMPORT_BY_NAME>(instance + pThunk[j].u1.AddressOfData)->Name, "CreateThread") == 0 )
						{
							void** pAddress = reinterpret_cast<void**>(instance + pImports->FirstThunk) + j;
							ReplaceFunction(pAddress);
							return true;
						}
					}
				}
				else
				{
					void** pFunctions = reinterpret_cast<void**>(instance + pImports->FirstThunk);

					for ( ptrdiff_t j = 0; pFunctions[j] != nullptr; j++ )
					{
						if ( pFunctions[j] == &CreateThread )
						{
							ReplaceFunction(&pFunctions[j]);
							return true;
						}
					}
				}
			}
		}
		return false;
	}
}

// Stub for pure3d error checking
static void CheckRefCountStub( uint32_t /*count*/ )
{
}


void OnInitializeHook()
{
	using namespace Memory;
	using namespace hook::txn;

	std::unique_ptr<ScopedUnprotect::Unprotect> Protect = ScopedUnprotect::UnprotectSectionOrFullModule( GetModuleHandle( nullptr ), ".text" );


	// Remove D3DLOCK_DISCARD flag from vertex locks, as game makes false assumptions about its behaviour
	try
	{
		auto vblock = get_pattern( "52 6A 00 6A 00 50 FF 51 2C 50 E8 ? ? ? ? BA", -5 + 1 );
		Patch<int32_t>( vblock, 0 );
	}
	TXN_CATCH();

	// Allow the game to run on all cores
	// SingleCoreAffinity shouldn't be set, but allow for it in case of emergency
	if (int singleCoreAffinity = INISettings::ReadSetting("SingleCoreAffinity"); singleCoreAffinity != 1)
	{
		try
		{
			using namespace AffinityChanges;

			auto setAffinity = get_pattern( "56 8B 35 ? ? ? ? 8D 44 24 08", -3 );

			if (Init() && RedirectImports())
			{
				Patch( setAffinity, { 0xC3 } ); // retn
			}
		}
		TXN_CATCH();
	}

	// Move settings to a settings.ini file in game directory (saves are already being stored there)
	try
	{
		using namespace INISettings;

		auto write = get_pattern( "8D 44 24 2C 50 68 3F 00 0F 00 6A 00 51", -0x12 );
		auto read = get_pattern( "83 EC 2C 8D 04 24 50", -6 );

		InjectHook( write, WriteSetting, PATCH_JUMP );	
		InjectHook( read, ReadSetting, PATCH_JUMP );
	}
	TXN_CATCH();

	// Remove D3DCREATE_MULTITHREADED flag from device creation, as the game does not actually need it
	// and it might decrease performance
	try
	{
		// This code changed between 1.0 and 1.00.2 and building one pattern for both is not feasible
		auto pattern10 = pattern( "51 8B 8D D8 01 00 00 6A 01 51 50 FF 52 40" ).count_hint(1); // 1.0
		if ( pattern10.size() == 1 )
		{
			Patch<int8_t>( pattern10.get_first( -2 + 1 ), 0x40 ); // D3DCREATE_HARDWARE_VERTEXPROCESSING
		}
		else
		{
			auto pattern1002 = pattern( "50 6A 01 57 51 FF 52 40" ).count_hint(1); // 1.00.2
			if ( pattern1002.size() == 1 )
			{
				Patch<int8_t>( pattern1002.get_first( -2 + 1 ), 0x40 ); // D3DCREATE_HARDWARE_VERTEXPROCESSING
			}
		}
	}
	TXN_CATCH();

	// Pooled D3D vertex and index buffers for improve performance
	try
	{
		gpPure3d = *get_pattern<pure3d**>( "FF 52 10 A1", 3 + 1 );

		auto createResources = pattern( "E8 ? ? ? ? 8B 4C 24 20 51 8B CE" ).get_one();
		auto checkRefCount = pattern( "FF 52 08 50 E8 ? ? ? ? A1 ? ? ? ? 83 C4 04" );
		auto alloc = get_pattern( "E8 ? ? ? ? 89 28" );
		auto dtor = get_pattern( "8B 48 10 89 4E 0C 8B C6", 0xF + 3 );
		auto freeMem = get_pattern( "E8 ? ? ? ? 0F B7 4E 18" );
		auto deviceLost1 = get_pattern( "75 26 E8 ? ? ? ? 8B 86 ? ? ? ?", 2 );
		auto deviceLost2 = get_pattern( "E8 ? ? ? ? 8B 06 8B 08 53" );

		// Scarface malloc used internally in the cache
		ReadCall( alloc, orgMalloc );

		ReadCall( freeMem, pure3d::orgFreeMemory );
	
		auto vb = createResources.get<void>();
		ReadCall( vb, pure3d::d3dPrimBuffer::orgCreateVertexBuffer );
		InjectHook( vb, &pure3d::d3dPrimBuffer::GetOrCreateVertexBuffer );

		auto ib = createResources.get<void>( 0xC );
		ReadCall( ib, pure3d::d3dPrimBuffer::orgCreateIndexBuffer );
		InjectHook( ib, &pure3d::d3dPrimBuffer::GetOrCreateIndexBuffer );

		ReadCall( dtor, pure3d::d3dPrimBuffer::orgDtor );
		InjectHook( dtor, &pure3d::d3dPrimBuffer::ReclaimAndDestroy );

		ReadCall( deviceLost1, pure3d::orgOnDeviceLost );
		InjectHook( deviceLost1, pure3d::FlushCachesOnDeviceLost );

		InjectHook( deviceLost2, pure3d::FlushCachesOnDeviceLost );

		// Remove a false positive from 1.0, where vb->Release() return value gets treated as HRESULT
		// and breaks with our cache
		checkRefCount.for_each_result( []( pattern_match match ) {
			InjectHook( match.get<void>( 4 ), CheckRefCountStub );
		} );
	}
	TXN_CATCH();

	// List all resolutions
	try
	{
		using namespace ListAllResolutions;

		pUnknown3dStruct = *get_pattern<void**>( "FF D7 8B 0D ? ? ? ? 83 C4 0C", 2 + 2 );

		auto scriptCommand = get_pattern( "68 ? ? ? ? E8 ? ? ? ? 8B 53 14", 5 );
		auto listResolutions = get_pattern( "E8 ? ? ? ? A1 ? ? ? ? 85 C0 74 20" );

		ReadCall( scriptCommand, orgInvokeScriptCommand );		
		InjectHook( listResolutions, ListResolutions );
	}
	TXN_CATCH();

	// Adjust game camera speed
	if (int camSpeedMultiplier = INISettings::ReadSetting("CameraSpeedMultiplier"); camSpeedMultiplier != -1)
	{
		try
		{
			// this 100.0 is used just for camera y variables so no problem overwriting it
			auto y = get_pattern("B8 00 00 C8 42 C7 86 84 01 00 00", 1);
			auto freeX = get_pattern("C7 86 84 01 00 00 00 00 48 43", 6);
			auto gunX = get_pattern("C7 86 8C 01 00 00 00 00 16 43", 6);
			auto rageX = get_pattern("C7 86 94 01 00 00 00 00 96 43", 6);
			// "disable" script handler so that script doesnt change new values
			// nulling strings seems to do the trick
			auto RageSpeedY = get_pattern("52 61 67 65 53 70 65 65 64 59 00");
			auto RageSpeedX = get_pattern("52 61 67 65 53 70 65 65 64 58 00");
			auto FreeLookSpeedX = get_pattern("46 72 65 65 4C 6F 6F 6B 53 70 65 65 64 59 00");
			auto FreeLookSpeedY = get_pattern("46 72 65 65 4C 6F 6F 6B 53 70 65 65 64 58 00 ");
			auto GunSpeedY = get_pattern("47 75 6E 53 70 65 65 64 59 00"); 
			auto GunSpeedX = get_pattern("47 75 6E 53 70 65 65 64 58 00"); 

			Patch<char>(RageSpeedY, 0x00);
			Patch<char>(RageSpeedX, 0x00);
			Patch<char>(FreeLookSpeedX, 0x00);
			Patch<char>(FreeLookSpeedY, 0x00);
			Patch<char>(GunSpeedY, 0x00);
			Patch<char>(GunSpeedX, 0x00);

			// shared y
			float origY = *(float*)(y);
			Patch<float>(y, origY * camSpeedMultiplier);

			// freelook
			float origX = *(float*)(freeX);
			Patch<float>(freeX, origX * camSpeedMultiplier);
			// aiming
			origX = *(float*)(gunX);
			Patch<float>(gunX, origX * camSpeedMultiplier);
			// rage mode
			origX = *(float*)(rageX);
			Patch<float>(rageX, origX * camSpeedMultiplier);
		}
		TXN_CATCH();
	}

	// Adjust FOV
	if (float FOV = INISettings::ReadFloatSetting("FOV"); FOV != 0.0f)
	{
		try
		{
			// TODO: vehicle?
			// on foot
			auto oldFOV = get_pattern("B8 00 00 70 42 89 86 64 01 00 00", 1);
			auto ExteriorDefaultFOV = get_pattern("45 78 74 65 72 69 6F 72 44 65 66 61 75 6C 74 46"); 
			auto ExteriorCombatFOV = get_pattern("45 78 74 65 72 69 6F 72 43 6F 6D 62 61 74 46 4F");
			auto ExteriorLockedFOV = get_pattern("45 78 74 65 72 69 6F 72 4C 6F 63 6B 65 64 46 4F");
			auto InteriorDefaultFOV = get_pattern("49 6E 74 65 72 69 6F 72 44 65 66 61 75 6C 74 46");
			auto InteriorCombatFOV = get_pattern("49 6E 74 65 72 69 6F 72 43 6F 6D 62 61 74 46 4F");
			auto InteriorLockedFOV = get_pattern("49 6E 74 65 72 69 6F 72 4C 6F 63 6B 65 64 46 4F");
			auto RageFOV = get_pattern("52 61 67 65 46 4F 56 00"); 

			Patch<float>(oldFOV, FOV);
			Patch<char>(ExteriorDefaultFOV, 0x00);
			Patch<char>(ExteriorCombatFOV, 0x00);
			Patch<char>(ExteriorLockedFOV, 0x00);
			Patch<char>(InteriorDefaultFOV, 0x00);
			Patch<char>(InteriorCombatFOV, 0x00);
			Patch<char>(InteriorLockedFOV, 0x00);
			Patch<char>(RageFOV, 0x00);
		}
		TXN_CATCH();
	}
}
