#include "Utils/MemoryMgr.h"
#include "Utils/Patterns.h"

#include "pure3d.h"

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
}


void OnInitializeHook()
{
	using namespace Memory;
	using namespace hook;

	std::unique_ptr<ScopedUnprotect::Unprotect> Protect = ScopedUnprotect::UnprotectSectionOrFullModule( GetModuleHandle( nullptr ), ".text" );

	// Scarface malloc
	{
		auto alloc = get_pattern( "E8 ? ? ? ? 89 28" );
		ReadCall( alloc, orgMalloc );
	}


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

	// Remove D3DCREATE_MULTITHREADED flag from device creation, as the game does not actually need it
	// and it might decrease performance
	{
		auto createdevice = get_pattern( "50 6A 01 57 51 FF 52 40", -2 + 1 );
		Patch<int8_t>( createdevice, 0x40 ); // D3DCREATE_HARDWARE_VERTEXPROCESSING
	}

	// Pooled D3D vertex and index buffers for improve performance
	{
		gpPure3d = *get_pattern<pure3d**>( "FF 52 10 A1", 3 + 1 );

		auto createResources = pattern( "E8 ? ? ? ? 8B 4C 24 20 51 8B CE" ).get_one();
	
		auto vb = createResources.get<void>();
		ReadCall( vb, pure3d::d3dPrimBuffer::orgCreateVertexBuffer );
		InjectHook( vb, &pure3d::d3dPrimBuffer::GetOrCreateVertexBuffer );

		auto ib = createResources.get<void>( 0xC );
		ReadCall( ib, pure3d::d3dPrimBuffer::orgCreateIndexBuffer );
		InjectHook( ib, &pure3d::d3dPrimBuffer::GetOrCreateIndexBuffer );

		auto dtor = get_pattern( "8B 48 10 89 4E 0C 8B C6", 0xF + 3 );
		ReadCall( dtor, pure3d::d3dPrimBuffer::orgDtor );
		InjectHook( dtor, &pure3d::d3dPrimBuffer::ReclaimAndDestroy );

		auto freeMem = get_pattern( "E8 ? ? ? ? 0F B7 4E 18" );
		ReadCall( freeMem, pure3d::orgFreeMemory );

		auto deviceLost1 = get_pattern( "75 26 E8 ? ? ? ? 8B 86 ? ? ? ?", 2 );
		ReadCall( deviceLost1, pure3d::orgOnDeviceLost );
		InjectHook( deviceLost1, pure3d::FlushCachesOnDeviceLost );

		auto deviceLost2 = get_pattern( "88 8D ? ? ? ? E8", 6 );
		InjectHook( deviceLost2, pure3d::FlushCachesOnDeviceLost );
	}
}
