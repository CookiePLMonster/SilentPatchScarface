#include "pure3d.h"

#include <map>

pure3d** gpPure3d;

// Cached entries
// We need entries to be sorted (and thus a hash map won't work), as we want to be able to reuse buffers slightly bigger than really needed
std::multimap<uint32_t, IDirect3DVertexBuffer9*> staticVertexBuffers;
std::multimap<uint32_t, std::pair<IDirect3DVertexBuffer9*, void*> > dynamicVertexBuffers; // Also caches scratch space
std::multimap<uint32_t, IDirect3DIndexBuffer9*> indexBuffers;

template<typename Container>
typename Container::mapped_type GetItemFromCache( Container& container, uint32_t size )
{
	// Heuristics:
	// We try to find an item of closest matching size
	// In an ideal scenario, size of a cached buffer will be identical, but since this might not happen often,
	// we'll also allow reusing buffers up to 2x the requested size
	// Upon success, cache item gets consumed
	typename Container::mapped_type entry {};

	auto match = container.lower_bound( size );
	if ( match != container.end() )
	{
		// match is at least of the correct size
		if ( match->first <= size * 2 )
		{
			// match is not too big, we can accept it
			entry = match->second;
			container.erase( match );
		}
	}
	return entry;
}

void pure3d::d3dPrimBuffer::GetOrCreateVertexBuffer( uint32_t size )
{
	const uint32_t realSize = size * m_primData->m_vertexSize;

	ReclaimBuffers( true, false );
	
	// Try to find a matching item in cache, if not present - create
	IDirect3DVertexBuffer9* cachedBuffer;
	if ( m_isManaged )
	{
		cachedBuffer = GetItemFromCache( staticVertexBuffers, realSize );
	}
	else
	{
		auto item = GetItemFromCache( dynamicVertexBuffers, realSize );
		cachedBuffer = std::get<IDirect3DVertexBuffer9*>(item);
		m_scratchSpace = std::get<void*>(item);
	}

	if ( cachedBuffer != nullptr )
	{
		// TODO: RegisterD3DResource

		m_numVertices = size;
		m_vertexBuffer = cachedBuffer;

		if ( !m_isManaged )
		{
			ReuseDynamicVertexBuffer( realSize, D3DUSAGE_WRITEONLY|D3DUSAGE_DYNAMIC, 0, &m_vertexBuffer, m_scratchSpace );
		}
	}
	else
	{
		CreateVertexBuffer( size );
	}
}

void pure3d::d3dPrimBuffer::GetOrCreateIndexBuffer( uint32_t size )
{
	const uint32_t realSize = size * 2;

	ReclaimBuffers( false, true );
	
	// Try to find a matching item in cache, if not present - create
	IDirect3DIndexBuffer9* cachedBuffer = GetItemFromCache( indexBuffers, realSize );
	if ( cachedBuffer != nullptr )
	{
		m_numIndices = size;
		m_indexBuffer = cachedBuffer;
	}
	else
	{
		CreateIndexBuffer( size );
	}
}

void pure3d::d3dPrimBuffer::ReclaimBuffers( bool vertex, bool index )
{
	if ( vertex )
	{
		if ( m_vertexBuffer != nullptr )
		{
			m_vertexBuffer->AddRef();

			// Get the real size (d3d device isn't a PUREDEVICE so we can query for it)
			D3DVERTEXBUFFER_DESC desc;
			m_vertexBuffer->GetDesc( &desc );
			if ( m_isManaged )
			{
				staticVertexBuffers.emplace( desc.Size, m_vertexBuffer );
			}
			else
			{
				dynamicVertexBuffers.emplace( std::piecewise_construct, std::forward_as_tuple(desc.Size), 
									std::forward_as_tuple(m_vertexBuffer, std::exchange(m_scratchSpace, nullptr)) );
			}
		}
	}

	if ( index )
	{
		if ( m_indexBuffer != nullptr )
		{
			m_indexBuffer->AddRef();

			// Get the real size (d3d device isn't a PUREDEVICE so we can query for it)
			D3DINDEXBUFFER_DESC desc;
			m_indexBuffer->GetDesc( &desc );
			indexBuffers.emplace( desc.Size, m_indexBuffer );
		}
	}
}


void pure3d::d3dPrimBuffer::ReclaimAndDestroy()
{
	ReclaimBuffers( true, true );
	Dtor();
}

void pure3d::FlushCachesOnDeviceLost()
{
	for ( auto& it : dynamicVertexBuffers )
	{
		std::get<IDirect3DVertexBuffer9*>(it.second)->Release();
		FreeMemory( std::get<void*>(it.second) );
	}
	dynamicVertexBuffers.clear();

	OnDeviceLost();
}

void pure3d::ReuseDynamicVertexBuffer(uint32_t length, uint32_t usage, uint32_t fvf, IDirect3DVertexBuffer9** pOut, void* scratch)
{
	vertexBufferEntry* entry = static_cast<vertexBufferEntry*>(scarMalloc( sizeof(*entry) ));

	entry->m_length = length;
	entry->m_usage = usage;
	entry->m_fvf = fvf;
	entry->m_ppBuffer = pOut;
	entry->m_scratchSpace = scratch;

	if ( (*gpPure3d)->m_dynamicBufferListCur != (*gpPure3d)->m_dynamicBufferListTail )
	{
		vertexBufferEntry** cur = (*gpPure3d)->m_dynamicBufferListCur++;
		*cur = entry;
	}
}
