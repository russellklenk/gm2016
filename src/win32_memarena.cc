/*/////////////////////////////////////////////////////////////////////////////
/// @summary Implement an arena-style allocator based on OS virtual memory. 
/// The arena allocator is not safe for concurrent access by multiple threads.
///////////////////////////////////////////////////////////////////////////80*/

/*//////////////////
//   Data Types   //
//////////////////*/
/// @summary Define the data associated with an operating system arena allocator. General-purpose sub-arenas can be created from a single underlying arena.
struct WIN32_MEMORY_ARENA
{
    size_t   NextOffset;         /// The offset, in bytes relative to BaseAddress, of the next available byte.
    size_t   BytesCommitted;     /// The number of bytes committed for the arena.
    size_t   BytesReserved;      /// The number of bytes reserved for the arena.
    uint8_t *BaseAddress;        /// The base address of the reserved segment of process virtual address space.
    size_t   ReserveAlignBytes;  /// The number of alignment-overhead bytes for the current user reservation.
    size_t   ReserveTotalBytes;  /// The total size of the current user reservation, in bytes.
    DWORD    PageSize;           /// The operating system page size.
};

/// @summary Define the data associated with a user memory arena. The memory arena is layered on top of an OS memory arena.
struct MEMORY_ARENA
{
    size_t   BytesTotal;         /// The total number of bytes reserved for the arena.
    size_t   BytesUsed;          /// The number of bytes allocated from the arena.
    uint8_t *BaseAddress;        /// The base address of the reserved memory block.
    size_t   ReserveAlignBytes;  /// The number of alignment-overhead bytes for the current user reservation.
    size_t   ReserveTotalBytes;  /// The total size of the current reservation, in bytes.
};

/// @summary Reserve process address space for a memory arena. No address space is committed.
/// @param arena The memory arena to initialize.
/// @param arena_size The number of bytes of process address space to reserve.
/// @return Zero if the arena is initialize, or -1 if an error occurred.
public_function int
CreateMemoryArena
(
    WIN32_MEMORY_ARENA     *arena, 
    size_t             arena_size
)
{   // virtual memory allocations are rounded up to the next even multiple of the system 
    // page size, and have a starting address that is an even multiple of the system 
    // allocation granularity (SYSTEM_INFO::dwAllocationGranularity).
    SYSTEM_INFO sys_info = {};
    GetNativeSystemInfo(&sys_info);
    arena_size  = AlignUp(arena_size, size_t(sys_info.dwPageSize));
    void *base  = VirtualAlloc(NULL, arena_size, MEM_RESERVE, PAGE_READWRITE);
    if   (base == NULL)
    {   // unable to reserve the requested amount of address space; fail.
        arena->NextOffset        = 0;
        arena->BytesCommitted    = 0;
        arena->BytesReserved     = 0;
        arena->BaseAddress       = NULL;
        arena->ReserveAlignBytes = 0;
        arena->ReserveTotalBytes = 0;
        arena->PageSize          = sys_info.dwPageSize;
        return -1;
    }
    arena->NextOffset        = 0;
    arena->BytesCommitted    = 0;
    arena->BytesReserved     = arena_size;
    arena->BaseAddress       = (uint8_t*) base;
    arena->ReserveAlignBytes = 0;
    arena->ReserveTotalBytes = 0;
    arena->PageSize          = sys_info.dwPageSize;
    return 0;
}

/// @summary Release process address space reserved for a memory arena. All allocations are invalidated.
/// @param arena The memory arena to delete.
public_function void
DeleteMemoryArena
(
    WIN32_MEMORY_ARENA *arena
)
{
    if (arena->BaseAddress != NULL)
    {   // free the entire range of reserved virtual address space.
        VirtualFree(arena->BaseAddress, 0, MEM_RELEASE);
    }
    arena->NextOffset        = 0;
    arena->BytesCommitted    = 0;
    arena->BytesReserved     = 0;
    arena->BaseAddress       = 0;
    arena->ReserveAlignBytes = 0;
    arena->ReserveTotalBytes = 0;
}

/// @summary Retrieve an exact marker that can be used to reset or decommit the arena, preserving all current allocations.
/// @param arena The memory arena to query.
/// @return The marker representing the byte offset of the next allocation.
public_function size_t
MemoryArenaMarker
(
    WIN32_MEMORY_ARENA *arena
)
{
    return arena->NextOffset;
}

/// @summary Retrieves the virtual memory manager page size for a given arena.
/// @param arena The memory arena to query.
/// @return The operating system page size, in bytes.
public_function size_t
MemoryArenaPageSize
(
    WIN32_MEMORY_ARENA *arena
)
{
    return size_t(arena->PageSize);
}

/// @summary Allocate memory from an arena. Additional address space is committed up to the initial reservation size.
/// @param arena The memory arena to allocate from.
/// @param alloc_size The minimum number of bytes to allocate.
/// @param alloc_alignment A power-of-two, greater than or equal to 1, specifying the alignment of the returned address.
/// @return A pointer to the start of the allocated block, or NULL if the request could not be satisfied.
public_function void*
MemoryArenaAllocate
(
    WIN32_MEMORY_ARENA          *arena, 
    size_t                  alloc_size, 
    size_t             alloc_alignment
)
{
    size_t base_address    = size_t(arena->BaseAddress) + arena->NextOffset;
    size_t aligned_address = AlignUp(base_address, alloc_alignment);
    size_t bytes_total     = alloc_size + (aligned_address - base_address);
    if ((arena->NextOffset + bytes_total) > arena->BytesReserved)
    {   // there's not enough reserved address space to satisfy the request.
        return NULL;
    }
    if ((arena->NextOffset + bytes_total) > arena->BytesCommitted)
    {   // additional address space needs to be committed.
        if (VirtualAlloc(arena->BaseAddress + arena->NextOffset, bytes_total, MEM_COMMIT, PAGE_READWRITE) == NULL)
        {   // there's not enough committed address space to satisfy the request.
            return NULL;
        }
        arena->BytesCommitted = AlignUp(arena->NextOffset + bytes_total, arena->PageSize);
    }
    arena->NextOffset += bytes_total;
    return (void*) aligned_address;
}

/// @summary Reserve memory within an arena. Additional address space is committed up to the initial OS reservation size. Use MemoryArenaCommit to commit the number of bytes actually used.
/// @param arena The memory arena to allocate from.
/// @param reserve_size The number of bytes to reserve for the active allocation.
/// @param alloc_alignment A power-of-two, greater than or equal to 1, specifying the alignment of the returned address.
/// @return A pointer to the start of the allocated block, or NULL if the request could not be satisfied.
public_function void*
MemoryArenaReserve
(
    WIN32_MEMORY_ARENA          *arena, 
    size_t                reserve_size, 
    size_t             alloc_alignment
)
{
    if (arena->ReserveAlignBytes != 0 || arena->ReserveTotalBytes != 0)
    {   // there's an existing reservation, which must be committed or canceled first.
        return NULL;
    }
    size_t base_address    = size_t(arena->BaseAddress) + arena->NextOffset;
    size_t aligned_address = AlignUp(base_address, alloc_alignment);
    size_t bytes_total     = reserve_size + (aligned_address - base_address);
    if ((arena->NextOffset + bytes_total) > arena->BytesReserved)
    {   // there's not enough reserved address space to satisfy the request.
        return NULL;
    }
    if ((arena->NextOffset + bytes_total) > arena->BytesCommitted)
    {   // additional address space needs to be committed.
        if (VirtualAlloc(arena->BaseAddress + arena->NextOffset, bytes_total, MEM_COMMIT, PAGE_READWRITE) == NULL)
        {   // there's not enough committed address space to satisfy the request.
            return NULL;
        }
        arena->BytesCommitted = AlignUp(arena->NextOffset + bytes_total, arena->PageSize);
    }
    arena->ReserveAlignBytes = aligned_address - base_address;
    arena->ReserveTotalBytes = bytes_total;
    return (void*) aligned_address;
}

/// @summary Commits the active reservation within the memory arena.
/// @param arena The memory arena maintaining the reservation.
/// @param commit_size The number of bytes to commit, which must be less than or equal to the reservation size.
/// @return Zero if the commit is successful, or -1 if the commit size is invalid.
public_function int
MemoryArenaCommit
(
    WIN32_MEMORY_ARENA      *arena, 
    size_t             commit_size
)
{
    if (commit_size == 0)
    {   // cancel the reservation; don't commit any space.
        arena->ReserveAlignBytes = 0;
        arena->ReserveTotalBytes = 0;
        return 0;
    }
    if (commit_size <= (arena->ReserveTotalBytes - arena->ReserveAlignBytes))
    {   // the commit size is valid, so commit the space.
        arena->NextOffset       += arena->ReserveAlignBytes + commit_size;
        arena->ReserveAlignBytes = 0;
        arena->ReserveTotalBytes = 0;
        return 0;
    }
    else
    {   // the commit size is not valid, so cancel the outstanding reservation.
        arena->ReserveAlignBytes = 0;
        arena->ReserveTotalBytes = 0;
        return -1;
    }
}

/// @summary Cancel the active memory arena reservation, indicating that the returned memory block will not be used.
/// @param arena The memory arena maintaining the reservation to be cancelled.
public_function void
MemoryArenaCancel
(
    WIN32_MEMORY_ARENA *arena
)
{
    arena->ReserveAlignBytes = 0;
    arena->ReserveTotalBytes = 0;
}

/// @summary Resets the state of the arena back to a marker, without decommitting any memory.
/// @param arena The memory arena to reset.
/// @param arena_marker The marker value returned by os_memory_arena_marker().
public_function void
MemoryArenaResetToMarker
(
    WIN32_MEMORY_ARENA       *arena,
    size_t             arena_marker
)
{
    if (arena_marker <= arena->NextOffset)
        arena->NextOffset = arena_marker;
}

/// @summary Resets the state of the arena to empty, without decomitting any memory.
/// @param arena The memory arena to reset.
public_function void
MemoryArenaReset
(
    WIN32_MEMORY_ARENA *arena
)
{
    arena->NextOffset = 0;
}

/// @summary Decommit memory within an arena back to a previously retrieved marker.
/// @param arena The memory arena to decommit.
/// @param arena_marker The marker value returned by os_memory_arena_marker().
public_function void
MemoryArenaDecommitToMarker
(
    WIN32_MEMORY_ARENA       *arena, 
    size_t             arena_marker
)
{
    if (arena_marker < arena->NextOffset)
    {   // VirtualFree will decommit any page with at least one byte in the 
        // address range [arena->BaseAddress+arena_marker, arena->BaseAddress+arena->BytesCommitted].
        // since the marker might appear within a page, round the address up to the next page boundary.
        // this avoids decommitting a page that is still partially allocated.
        size_t last_page  = size_t(arena->BaseAddress) + arena->BytesCommitted;
        size_t mark_page  = size_t(arena->BaseAddress) + arena_marker;
        size_t next_page  = AlignUp(mark_page, arena->PageSize);
        size_t free_size  = last_page - next_page;
        arena->NextOffset = arena_marker;
        if (free_size > 0)
        {   // the call will decommit at least one page.
            VirtualFree((void*) next_page, free_size, MEM_DECOMMIT);
            arena->BytesCommitted -= free_size;
        }
    }
}

/// @summary Decommit all of the pages within a memory arena, without releasing the address space reservation.
/// @param arena The memory arena to decommit.
public_function void
MemoryArenaDecommit
(
    WIN32_MEMORY_ARENA *arena
)
{
    if (arena->BytesCommitted > 0)
    {   // decommit the entire committed region of address space.
        VirtualFree(arena->BaseAddress, 0, MEM_DECOMMIT);
        arena->BytesCommitted = 0;
    }
}

/// @summary Creates a new memory arena backed by an OS-level arena.
/// @param arena The memory arena to initialize.
/// @param arena_size The size of the memory arena, in bytes.
/// @param arena_alignment The base alignment of the memory arena, in bytes.
/// @param source_arena The OS-level arena from which memory is allocated.
/// @return Zero if the arena is successfully initialized, or -1 if an error occurred.
public_function int
CreateArena
(
    MEMORY_ARENA                *arena,
    size_t                  arena_size,
    size_t             arena_alignment,
    WIN32_MEMORY_ARENA   *source_arena
)
{
    // allocate and touch all of the pages.
    void  *base = MemoryArenaAllocate(source_arena, arena_size, arena_alignment);
    if (base   != NULL)
    {   // the memory arena was created successfully.
        arena->BytesTotal        = arena_size;
        arena->BytesUsed         = 0;
        arena->BaseAddress       = (uint8_t*) base;
        arena->ReserveAlignBytes = 0;
        arena->ReserveTotalBytes = 0;
        // touch all of the pages in the arena to allocate the physical pages.
        uint8_t *page_iter = arena->BaseAddress;
        size_t   page_size = MemoryArenaPageSize(source_arena);
        for (size_t i = 0, page_count = arena_size / page_size; i < page_count; ++i)
        {
            *page_iter  = 0;
             page_iter += page_size;
        }
        return 0;
    }
    else
    {   // not enough space to satisfy the allocation in the source arena.
        arena->BytesTotal        = 0;
        arena->BytesUsed         = 0;
        arena->BaseAddress       = NULL;
        arena->ReserveAlignBytes = 0;
        arena->ReserveTotalBytes = 0;
        return -1;
    }
}

/// @summary Determine whether a memory allocation request can be satisfied.
/// @param arena The memory arena to query.
/// @param size The size of the allocation request, in bytes.
/// @param alignment The desired alignment of the returned address. This must be a power of two greater than zero.
/// @return true if the specified allocation will succeed.
public_function bool
ArenaCanAllocate
(
    MEMORY_ARENA    *arena, 
    size_t            size,
    size_t       alignment
)
{
    size_t    base_address = size_t(arena->BaseAddress) + arena->BytesUsed;
    size_t aligned_address = AlignUp(base_address, alignment);
    size_t     alloc_bytes = size + (aligned_address - base_address);
    if ((arena->BytesUsed + alloc_bytes) > arena->BytesTotal)
    {   // there's not enough space to satisfy the allocation request.
        return false;
    }
    return true;
}

/// @summary Determine whether a memory allocation request can be satisfied.
/// @typeparam T The type being allocated. This type is used to determine the required alignment.
/// @param arena The memory arena to query.
/// @return true if the specified allocation will succeed.
template <typename T>
public_function inline bool
ArenaCanAllocateStruct
(
    MEMORY_ARENA *arena
)
{
    return ArenaCanAllocate(arena, sizeof(T), std::alignment_of<T>::value);
}

/// @summary Determine whether a memory allocation request for an array can be satisfied.
/// @typeparam T The type of array element. This type is used to determine the required alignment.
/// @param arena The memory arena to query.
/// @param count The number of items in the array.
/// @return true if the specified allocation will succeed.
template <typename T>
public_function inline bool
ArenaCanAllocateArray
(
    MEMORY_ARENA *arena,
    size_t        count
)
{
    return ArenaCanAllocate(arena, sizeof(T) * count, std::alignment_of<T>::value);
}

/// @summary Allocate aligned memory from an arena.
/// @param arena The memory arena to allocate from.
/// @param size The number of bytes to allocate.
/// @param alignment The desired alignment of the returned address. This must be a power of two greater than zero.
/// @return A pointer to the start of the allocated block, or NULL if the request could not be satisfied.
public_function void*
ArenaAllocate
(
    MEMORY_ARENA    *arena, 
    size_t            size, 
    size_t       alignment
)
{
    size_t    base_address = size_t(arena->BaseAddress) + arena->BytesUsed;
    size_t aligned_address = AlignUp(base_address, alignment);
    size_t     alloc_bytes = size + (aligned_address - base_address);
    if ((arena->BytesUsed + alloc_bytes) > arena->BytesTotal)
    {   // there's not enough space to satisfy the allocation request.
        return NULL;
    }
    arena->BytesUsed += alloc_bytes;
    return (void*) aligned_address;
}

/// @summary Reserve memory within an arena. Additional address space is committed up to the initial reservation size. Use ArenaCommit to commit the number of bytes actually used.
/// @param arena The memory arena to allocate from.
/// @param reserve_size The number of bytes to reserve for the active allocation.
/// @param alloc_alignment A power-of-two, greater than or equal to 1, specifying the alignment of the returned address.
/// @return A pointer to the start of the allocated block, or NULL if the request could not be satisfied.
public_function void*
ArenaReserve
(
    MEMORY_ARENA          *arena, 
    size_t          reserve_size, 
    size_t       alloc_alignment
)
{
    if (arena->ReserveAlignBytes != 0 || arena->ReserveTotalBytes != 0)
    {   // there's an existing reservation, which must be committed or canceled first.
        return NULL;
    }
    size_t base_address    = size_t(arena->BaseAddress) + arena->BytesUsed;
    size_t aligned_address = AlignUp(base_address, alloc_alignment);
    size_t bytes_total     = reserve_size + (aligned_address - base_address);
    if ((arena->BytesUsed  + bytes_total) > arena->BytesTotal)
    {   // there's not enough reserved address space to satisfy the request.
        return NULL;
    }
    arena->ReserveAlignBytes = aligned_address - base_address;
    arena->ReserveTotalBytes = bytes_total;
    return (void*) aligned_address;
}

/// @summary Commits the active reservation within the memory arena.
/// @param arena The memory arena maintaining the reservation.
/// @param commit_size The number of bytes to commit, which must be less than or equal to the reservation size.
/// @return Zero if the commit is successful, or -1 if the commit size is invalid.
public_function int
ArenaCommit
(
    MEMORY_ARENA      *arena, 
    size_t       commit_size
)
{
    if (commit_size == 0)
    {   // cancel the reservation; don't commit any space.
        arena->ReserveAlignBytes = 0;
        arena->ReserveTotalBytes = 0;
        return  0;
    }
    if (commit_size <= (arena->ReserveTotalBytes - arena->ReserveAlignBytes))
    {   // the commit size is valid, so commit the space.
        arena->BytesUsed        += arena->ReserveAlignBytes + commit_size;
        arena->ReserveAlignBytes = 0;
        arena->ReserveTotalBytes = 0;
        return  0;
    }
    else
    {   // the commit size is not valid, so cancel the outstanding reservation.
        arena->ReserveAlignBytes = 0;
        arena->ReserveTotalBytes = 0;
        return -1;
    }
}

/// @summary Cancel the active memory arena reservation, indicating that the returned memory block will not be used.
/// @param arena The memory arena maintaining the reservation to be cancelled.
public_function void
ArenaCancel
(
    MEMORY_ARENA *arena
)
{
    arena->ReserveAlignBytes = 0;
    arena->ReserveTotalBytes = 0;
}

/// @summary Reset a memory arena, invalidating all existing allocations.
/// @param arena The memory arena to reset.
public_function void
ArenaReset
(
    MEMORY_ARENA *arena
)
{
    arena->BytesUsed = 0;
}

/// @summary Retrieve an exact marker that can be used to reset the arena, preserving all current allocations.
/// @param arena The memory arena to query.
/// @return The marker representing the byte offset of the next allocation.
public_function size_t
ArenaMarker
(
    MEMORY_ARENA *arena
)
{
    return arena->BytesUsed;
}

/// @summary Resets the state of the arena back to a marker, without decommitting any memory.
/// @param arena The memory arena to reset.
/// @param arena_marker The marker value returned by ArenaMarker().
public_function void
ArenaResetToMarker
(
    MEMORY_ARENA       *arena,
    size_t       arena_marker
)
{
    if (arena_marker <= arena->BytesUsed)
        arena->BytesUsed = arena_marker;
}

/// @summary Allocate a new instance of a structure from a memory arena.
/// @typeparam T The type of structure to allocate.
/// @param arena The memory arena to allocate from.
/// @return A pointer to the new instance, or NULL.
template <typename T>
public_function T*
PushStruct
(
    MEMORY_ARENA *arena
)
{
    return (T*) ArenaAllocate(arena, sizeof(T), std::alignment_of<T>::value);
}

/// @summary Allocate an array from a memory arena.
/// @typeparam T The type of items in the array.
/// @param arena The memory arena to allocate from.
/// @param count The number of items of type T, representing the maximum size of the array (in items.)
/// @return A pointer to the start of the array, or NULL.
template <typename T>
public_function T*
PushArray
(
    MEMORY_ARENA *arena, 
    size_t        count
)
{
    return (T*) ArenaAllocate(arena, sizeof(T) * count, std::alignment_of<T>::value);
}

/// @summary Create a new sub-arena within an existing memory arena.
/// @param arena The memory arena to initialize.
/// @param arena_size The size of the sub-arena, in bytes.
/// @param arena_alignment The base memory alignment of the sub-arena, in bytes. This must be a power of two greater than zero.
/// @param source_arena The memory arena to allocate from.
/// @return Zero if the sub-arena is successfully initialized, or -1 if an error occurred.
public_function int
CreateSubArena
(
    MEMORY_ARENA           *arena, 
    size_t             arena_size, 
    size_t        arena_alignment, 
    MEMORY_ARENA    *source_arena
)
{
    void  *base = ArenaAllocate(source_arena, arena_size, arena_alignment);
    if (base != NULL)
    {   // the sub-arena was created successfully.
        arena->BytesTotal        = arena_size;
        arena->BytesUsed         = 0;
        arena->BaseAddress       = (uint8_t*) base;
        arena->ReserveAlignBytes = 0;
        arena->ReserveTotalBytes = 0;
        return 0;
    }
    else
    {   // not enough space to satisfy the allocation in the source arena.
        arena->BytesTotal        = 0;
        arena->BytesUsed         = 0;
        arena->BaseAddress       = NULL;
        arena->ReserveAlignBytes = 0;
        arena->ReserveTotalBytes = 0;
        return -1;
    }
}

