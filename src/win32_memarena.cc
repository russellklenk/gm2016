/*/////////////////////////////////////////////////////////////////////////////
/// @summary Implement an arena-style allocator based on OS virtual memory. 
/// The arena allocator is not safe for concurrent access by multiple threads.
///////////////////////////////////////////////////////////////////////////80*/

/*//////////////////
//   Data Types   //
//////////////////*/
/// @summary Define the data associated with an operating system arena allocator. General-purpose sub-arenas can be created from a single underlying arena.
struct os_memory_arena_t
{
    size_t   NextOffset;         /// The offset, in bytes relative to BaseAddress, of the next available byte.
    size_t   BytesCommitted;     /// The number of bytes committed for the arena.
    size_t   BytesReserved;      /// The number of bytes reserved for the arena.
    uint8_t *BaseAddress;        /// The base address of the reserved segment of process virtual address space.
    size_t   ReserveAlignBytes;  /// The number of alignment-overhead bytes for the current user reservation.
    size_t   ReserveTotalBytes;  /// The total size of the current user reservation, in bytes.
    DWORD    PageSize;           /// The operating system page size.
};

/// @summary Reserve process address space for a memory arena. No address space is committed.
/// @param arena The memory arena to initialize.
/// @param arena_size The number of bytes of process address space to reserve.
/// @return Zero if the arena is initialize, or -1 if an error occurred.
public_function int
create_os_memory_arena
(
    os_memory_arena_t     *arena, 
    size_t            arena_size
)
{   // virtual memory allocations are rounded up to the next even multiple of the system 
    // page size, and have a starting address that is an even multiple of the system 
    // allocation granularity (SYSTEM_INFO::dwAllocationGranularity).
    SYSTEM_INFO sys_info = {};
    GetNativeSystemInfo(&sys_info);
    arena_size  = align_up(arena_size, size_t(sys_info.dwPageSize));
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
delete_os_memory_arena
(
    os_memory_arena_t *arena
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
os_memory_arena_marker
(
    os_memory_arena_t *arena
)
{
    return arena->NextOffset;
}

/// @summary Retrieves the virtual memory manager page size for a given arena.
/// @param arena The memory arena to query.
/// @return The operating system page size, in bytes.
public_function size_t
os_memory_arena_page_size
(
    os_memory_arena_t *arena
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
os_memory_arena_allocate
(
    os_memory_arena_t          *arena, 
    size_t                 alloc_size, 
    size_t            alloc_alignment
)
{
    size_t base_address    = size_t(arena->BaseAddress) + arena->NextOffset;
    size_t aligned_address = align_up(base_address, alloc_alignment);
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
        arena->BytesCommitted = align_up(arena->NextOffset + bytes_total, arena->PageSize);
    }
    arena->NextOffset += bytes_total;
    return (void*) aligned_address;
}

/// @summary Reserve memory within an arena. Additional address space is committed up to the initial OS reservation size. Use os_memory_arena_commit to commit the number of bytes actually used.
/// @param arena The memory arena to allocate from.
/// @param reserve_size The number of bytes to reserve for the active allocation.
/// @param alloc_alignment A power-of-two, greater than or equal to 1, specifying the alignment of the returned address.
/// @return A pointer to the start of the allocated block, or NULL if the request could not be satisfied.
public_function void*
os_memory_arena_reserve
(
    os_memory_arena_t          *arena, 
    size_t               reserve_size, 
    size_t            alloc_alignment
)
{
    if (arena->ReserveAlignBytes != 0 || arena->ReserveTotalBytes != 0)
    {   // there's an existing reservation, which must be committed or canceled first.
        return NULL;
    }
    size_t base_address    = size_t(arena->BaseAddress) + arena->NextOffset;
    size_t aligned_address = align_up(base_address, alloc_alignment);
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
        arena->BytesCommitted = align_up(arena->NextOffset + bytes_total, arena->PageSize);
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
os_memory_arena_commit
(
    os_memory_arena_t      *arena, 
    size_t            commit_size
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
os_memory_arena_cancel
(
    os_memory_arena_t *arena
)
{
    arena->ReserveAlignBytes = 0;
    arena->ReserveTotalBytes = 0;
}

/// @summary Resets the state of the arena back to a marker, without decommitting any memory.
/// @param arena The memory arena to reset.
/// @param arena_marker The marker value returned by os_memory_arena_marker().
public_function void
os_memory_arena_reset_to_marker
(
    os_memory_arena_t       *arena,
    size_t            arena_marker
)
{
    if (arena_marker <= arena->NextOffset)
        arena->NextOffset = arena_marker;
}

/// @summary Resets the state of the arena to empty, without decomitting any memory.
/// @param arena The memory arena to reset.
public_function void
os_memory_arena_reset
(
    os_memory_arena_t *arena
)
{
    arena->NextOffset = 0;
}

/// @summary Decommit memory within an arena back to a previously retrieved marker.
/// @param arena The memory arena to decommit.
/// @param arena_marker The marker value returned by os_memory_arena_marker().
public_function void
os_memory_arena_decommit_to_marker
(
    os_memory_arena_t       *arena, 
    size_t            arena_marker
)
{
    if (arena_marker < arena->NextOffset)
    {   // VirtualFree will decommit any page with at least one byte in the 
        // address range [arena->BaseAddress+arena_marker, arena->BaseAddress+arena->BytesCommitted].
        // since the marker might appear within a page, round the address up to the next page boundary.
        // this avoids decommitting a page that is still partially allocated.
        size_t last_page  = size_t(arena->BaseAddress) + arena->BytesCommitted;
        size_t mark_page  = size_t(arena->BaseAddress) + arena_marker;
        size_t next_page  = align_up(mark_page, arena->PageSize);
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
os_memory_arena_decommit
(
    os_memory_arena_t *arena
)
{
    if (arena->BytesCommitted > 0)
    {   // decommit the entire committed region of address space.
        VirtualFree(arena->BaseAddress, 0, MEM_DECOMMIT);
        arena->BytesCommitted = 0;
    }
}

