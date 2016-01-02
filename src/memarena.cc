/*/////////////////////////////////////////////////////////////////////////////
/// @summary Implement an arena-style memory allocator built on top of the 
/// OS-specific memory arena implementation. Memory can be allocated in chunks,
/// but all memory must be freed at the same time.
///////////////////////////////////////////////////////////////////////////80*/

/*//////////////////
//   Data Types   //
//////////////////*/
/// @summary Define the data associated with a user memory arena. The memory arena is layered on top of an OS memory arena.
struct MEMORY_ARENA
{
    size_t   BytesTotal;         /// The total number of bytes reserved for the arena.
    size_t   BytesUsed;          /// The number of bytes allocated from the arena.
    uint8_t *BaseAddress;        /// The base address of the reserved memory block.
    size_t   ReserveAlignBytes;  /// The number of alignment-overhead bytes for the current user reservation.
    size_t   ReserveTotalBytes;  /// The total size of the current reservation, in bytes.
};

/*///////////////
//   Globals   //
///////////////*/

/*///////////////////////
//   Local Functions   //
///////////////////////*/

/*////////////////////////
//   Public Functions   //
////////////////////////*/
/// @summary Creates a new memory arena backed by an OS-level arena.
/// @param arena The memory arena to initialize.
/// @param arena_size The size of the memory arena, in bytes.
/// @param arena_alignment The base alignment of the memory arena, in bytes.
/// @param source_arena The OS-level arena from which memory is allocated.
/// @return Zero if the arena is successfully initialized, or -1 if an error occurred.
public_function int
CreateArena
(
    MEMORY_ARENA             *arena,
    size_t               arena_size,
    size_t          arena_alignment,
    OS_MEMORY_ARENA   *source_arena
)
{
    // allocate and touch all of the pages.
    void  *base = MemoryArenaAllocate(source_arena, arena_size, arena_alignment);
    if (base   != nullptr)
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

/// @summary Allocate aligned memory from an arena.
/// @param arena The memory arena to allocate from.
/// @param size The number of bytes to allocate.
/// @param alignment The desired alignment of the returned address. This must be a power of two greater than zero.
/// @return A pointer to the start of the allocated block, or nullptr if the request could not be satisfied.
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
        return nullptr;
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
        return nullptr;
    }
    size_t base_address    = size_t(arena->BaseAddress) + arena->BytesUsed;
    size_t aligned_address = AlignUp(base_address, alloc_alignment);
    size_t bytes_total     = reserve_size + (aligned_address - base_address);
    if ((arena->BytesUsed  + bytes_total) > arena->BytesTotal)
    {   // there's not enough reserved address space to satisfy the request.
        return nullptr;
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

/// @summary Allocate a new instance of a structure from a memory arena.
/// @typeparam T The type of structure to allocate.
/// @param arena The memory arena to allocate from.
/// @return A pointer to the new instance, or nullptr.
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
/// @return A pointer to the start of the array, or nullptr.
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
    if (base != nullptr)
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

