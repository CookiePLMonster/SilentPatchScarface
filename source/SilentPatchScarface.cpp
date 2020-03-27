#include "Utils/MemoryMgr.h"
#include "Utils/Patterns.h"

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
}


void OnInitializeHook()
{
	using namespace Memory;
	using namespace hook;

	std::unique_ptr<ScopedUnprotect::Unprotect> Protect = ScopedUnprotect::UnprotectSectionOrFullModule( GetModuleHandle( nullptr ), ".text" );


	// Remove D3DLOCK_DISCARD flag from vertex locks, as game makes false assumptions about its behaviour
	{
		auto vblock = get_pattern( "52 6A 00 6A 00 50 FF 51 2C 50 E8 ? ? ? ? BA", -5 + 1 );
		Patch<int32_t>( vblock, 0 );
	}

	// Allow the game to run on all cores
	{
		auto setAffinity = get_pattern( "56 8B 35 ? ? ? ? 8D 44 24 08", -3 );
		Patch( setAffinity, { 0xC3 } ); // retn
	}

	// Move settings to a settings.ini file in game directory (saves are already being stored there)
	{
		using namespace INISettings;

		auto write = get_pattern( "74 5A 8B 0D ? ? ? ? 8D 44 24 2C", -0xA );
		InjectHook( write, WriteSetting, PATCH_JUMP );

		auto read = get_pattern( "83 EC 2C 8D 04 24 50", -6 );
		InjectHook( read, ReadSetting, PATCH_JUMP );
	}
}
