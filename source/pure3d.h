#pragma once

#include <d3d9.h>
#include <functional>

class pure3d
{
public:
    struct vertexBufferEntry
    {
        uint32_t m_length;
        uint32_t m_usage;
        uint32_t m_fvf;
        IDirect3DVertexBuffer9** m_ppBuffer;
        void* m_scratchSpace;
    };

    struct d3dPrim
    {
        BYTE __pad[16];
        DWORD m_vertexSize;
    };

	class d3dPrimBuffer
	{
	private:
		virtual ~d3dPrimBuffer() = default; // To create a vtable

	private:
        int m_refCount;
        BYTE gap8[4];
        IDirect3DDevice9* m_d3dDevice;
        BYTE gap10[8];
        int m_flags;
        int m_numVertices;
        int m_numIndices;
        int m_indexBufferSize;
        int field_28;
        IDirect3DIndexBuffer9* m_indexBuffer;
        IDirect3DVertexBuffer9* m_vertexBuffer;
        bool m_isLocked;
        void* field_34;
        d3dPrim* m_primData;
        bool m_isManaged;
        void* m_scratchSpace;
        void* m_lockedSpace;

    public:
        static inline void (d3dPrimBuffer::*orgCreateVertexBuffer)(uint32_t size);
        static inline void (d3dPrimBuffer::*orgCreateIndexBuffer)(uint32_t size);
        static inline void (d3dPrimBuffer::*orgDtor)();

        void CreateVertexBuffer(uint32_t size)
        {
            std::invoke( orgCreateVertexBuffer, this, size );
        }

        void CreateIndexBuffer(uint32_t size)
        {
            std::invoke( orgCreateIndexBuffer, this, size );
        }

        void Dtor()
        {
            std::invoke( orgDtor, this );
        }

        // Functions with cache
        void ReclaimAndDestroy();
        void GetOrCreateVertexBuffer(uint32_t size);
        void GetOrCreateIndexBuffer(uint32_t size);

    private:
         void ReclaimBuffers(bool vertex, bool index);
	};

private:
    BYTE __pad[16];

    vertexBufferEntry** m_dynamicBufferListHead;
    vertexBufferEntry** m_dynamicBufferListCur;
    vertexBufferEntry** m_dynamicBufferListTail;

public:
    static inline void (*orgOnDeviceLost)();
    static inline void (*orgFreeMemory)(void* mem);

    static void OnDeviceLost()
    {
        std::invoke( orgOnDeviceLost );
    }

    static void FreeMemory(void* mem)
    {
        std::invoke( orgFreeMemory, mem );
    }

    static void FlushCachesOnDeviceLost();

    static void ReuseDynamicVertexBuffer(uint32_t length, uint32_t usage, uint32_t fvf, IDirect3DVertexBuffer9** pOut, void* scratch);
};

extern pure3d** gpPure3d;

// Scarface allocator
void* scarMalloc(size_t size);

static_assert(sizeof(pure3d::d3dPrimBuffer) == 0x4C, "Wrong size: pure3d::d3dPrimBuffer");
static_assert(sizeof(pure3d::vertexBufferEntry) == 0x14, "Wrong size: pure3d::vertexBufferEntry");