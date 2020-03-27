#include "Utils/MemoryMgr.h"
#include "Utils/Patterns.h"

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
}
