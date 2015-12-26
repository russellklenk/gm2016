/*/////////////////////////////////////////////////////////////////////////////
/// @summary Define macros abstracting the current compiler.
///////////////////////////////////////////////////////////////////////////80*/

#ifndef COMPILER_CONFIG_H
#define COMPILER_CONFIG_H

/*////////////////
//   Includes   //
////////////////*/
#include <stddef.h>
#include <stdint.h>

/*////////////////////
//   Preprocessor   //
////////////////////*/
/// @summary Define the default cacheline size, in bytes. This must always be a power-of-two.
#ifndef CACHELINE_SIZE
        #define CACHELINE_SIZE                64
#endif

/// @summary Define the CDECL calling convention attribute.
#ifndef RTA_CALL_C
    #if   defined(_MSC_VER)
        #define CALL_C                        __cdecl
        #define C_API                         CALL_C
    #elif defined(__GNUC__)
        #define CALL_C                        __attribute__((cdecl))
        #define C_API                         CALL_C
    #else
        #error Unsupported compiler (need CALL_C declaration in compiler_config.h)
    #endif
#endif

/// @summary Define force inline and never inline declarations for the compiler.
#if   defined(_MSC_VER)
        #define never_inline                   __declspec(noinline)
        #define force_inline                   __forceinline
#elif defined(__GNUC__)
        #define never_inline                   __attribute__((noinline))
        #define force_inline                   __attribute__((always_inline))
#else
        #error Unsupported compiler (need never_inline and force_inline declaration in compiler_config.h)
#endif

/// @summary Define alignment declarations for the compiler.
#if   defined(_MSC_VER)
        #define struct_alignment(_alignment)   __declspec(align(_alignment))
        #define field_alignment(_alignment)    __declspec(align(_alignment))
        #define alignment_of_type(_x)          __alignof(_x)
        #define alignment_of_field(_x)         __alignof(_x)
#elif defined(__GNUC__)
        #define struct_alignment(_alignment)   __attribute__((aligned(_alignment)))
        #define field_alignment(_alignment)    __attribute__((aligned(_alignment)))
        #define alignment_of_type(_x)          __alignof__(_x)
        #define alignment_of_field(_x)         __alignof__(_x)
#else
        #error Unsupported compiler (need struct_alignment, field_alignment, alignment_of_type and alignment_of_field declaration in compiler_config.h)
#endif

/// @summary Define restrict declarations for the compiler.
#if   defined(_MSC_VER)
        #define restrict_ptr                   __restrict
#elif defined(__GNUC__)
        #define restrict_ptr                   __restrict
#else
        #error Unsupported compiler (need restrict_ptr declaration in compiler_config.h)
#endif

/// @summary Define static/dynamic library import/export for the compiler.
#if   defined(_MSC_VER)
    #if   defined(BUILD_DYNAMIC)
        #define library_function               __declspec(dllexport)
    #elif defined(BUILD_STATIC)
        #define library_function
    #else
        #define library_function               __declspec(dllimport)
    #endif
#elif defined(__GNUC__)
    #if   defined(BUILD_DYNAMIC)
        #define library_function               __attribute__((visibility("default")))
    #elif defined(BUILD_STATIC)
        #define library_function
    #else
        #define library_function               __attribute__((visibility("default")))
    #endif
#else
        #error Unsupported compiler (need library_function declaration in compier_config.h)
#endif

/// @summary Tag used to mark a function as available for use outside of the current translation unit (the default visibility).
#ifndef export_function
    #define export_function                    library_function
#endif

/// @summary Tag used to mark a function as available for public use, but not exported outside of the translation unit.
#ifndef public_function
    #define public_function                    static
#endif

/// @summary Tag used to mark a function internal to the translation unit.
#ifndef internal_function
    #define internal_function                  static
#endif

/// @summary Tag used to mark a variable as local to a function, and persistent across invocations of that function.
#ifndef local_persist
    #define local_persist                      static
#endif

/// @summary Tag used to mark a variable as global to the translation unit.
#ifndef global_variable
    #define global_variable                    static
#endif

/// @summary Tag used to mark a field as being aligned on a cacheline boundary.
#ifndef cacheline_align
    #define cacheline_align                    field_alignment(CACHELINE_SIZE)
#endif

/// @summary Helper macro to begin a multi-line macro safely.
#ifndef MULTI_LINE_MACRO_BEGIN
    #define MULTI_LINE_MACRO_BEGIN             do {
#endif

/// @summary Helper macro to close a multi-line macro safely.
#ifndef MULTI_LINE_MACRO_CLOSE
    #if   defined(_MSC_VER)
        #define MULTI_LINE_MACRO_CLOSE          \
        __pragma(warning(push))                 \
        __pragma(warning(disable:4127))         \
        } while (0);                            \
        __pragma(warning(pop))
    #elif defined(__GNUC__)
        #define MULTI_LINE_MACRO_CLOSE          \
        } while (0)
    #endif
#endif

/// @summary Helper macro to prevent compiler warnings about unused arguments.
#ifndef UNUSED_HELPER
    #if defined(_MSC_VER)
        #define UNUSED_HELPER(x)               (x)
    #else
        #define UNUSED_HELPER(x)               (void)sizeof((x))
    #endif
#endif

/// @summary Prevent compiler warnings about unused arguments.
#ifndef UNUSED_ARG
#define UNUSED_ARG(x)                          \
    MULTI_LINE_MACRO_BEGIN                     \
        UNUSED_HELPER(x);                      \
    MULTI_LINE_MACRO_CLOSE
#endif

/// @summary Prevent compiler warnings about unused locals.
#ifndef UNUSED_LOCAL
#define UNUSED_LOCAL(x)                        \
    MULTI_LINE_MACRO_BEGIN                     \
        UNUSED_HELPER(x);                      \
    MULTI_LINE_MACRO_CLOSE
#endif

/// @summary Define our own memory order values for use with compiler intrinsics.
/// RTA_MEMORY_ORDER_RELAXED: No inter-thread ordering constraint. The load or store must be atomic.
/// RTA_MEMORY_ORDER_CONSUME: Impose a happens-before constraint relative to a store-release to prevent load reordering from moving before the barrier.
/// RTA_MEMORY_ORDER_ACQUIRE: Impose a happens-before constraint relative to a store-release to prevent load reordering from moving before the barrier.
/// RTA_MEMORY_ORDER_RELEASE: Impose a happens-before constraint to a load operation to prevent store reordering from moving below the barrier.
/// RTA_MEMORY_ORDER_ACQ_REL: Prevent subsequent load operations from moving before the barrier, and prior store operations from moving after the barrier.
/// RTA_MEMORY_ORDER_SEQ_CST: Enforces a total ordering relative to all other SEQ_CST operations.
#if   TARGET_COMPILER == COMPILER_MSVC
            #define MEMORY_ORDER_RELAXED       0
            #define MEMORY_ORDER_CONSUME       1
            #define MEMORY_ORDER_ACQUIRE       2 
            #define MEMORY_ORDER_RELEASE       3
            #define MEMORY_ORDER_ACQ_REL       4
            #define MEMORY_ORDER_SEQ_CST       5
#elif TARGET_COMPILER == COMPILER_GNUC
            #define MEMORY_ORDER_RELAXED       __ATOMIC_RELAXED
            #define MEMORY_ORDER_CONSUME       __ATOMIC_CONSUME
            #define MEMORY_ORDER_ACQUIRE       __ATOMIC_ACQUIRE
            #define MEMORY_ORDER_RELEASE       __ATOMIC_RELEASE
            #define MEMORY_ORDER_ACQ_REL       __ATOMIC_ACQ_REL
            #define MEMORY_ORDER_SEQ_CST       __ATOMIC_SEQ_CST
#else
    #error Unsupported compiler in compiler_config.h - need MEMORY_ORDER_* values
#endif

/// @summary Define memory barrier intrinsics for the compiler and processor. Most code is better off using std::atomic instead of these macros.
#if   defined(USE_STD_ATOMICS) && (USE_STD_ATOMICS != 0)
            #include <atomic>
            #define COMPILER_BARRIER_LOAD      std::atomic_signal_fence(std::memory_order_acquire)
            #define COMPILER_BARRIER_STORE     std::atomic_signal_fence(std::memory_order_release)
            #define COMPILER_BARRIER_FULL      std::atomic_signal_fence(std::memory_order_seq_cst)
            #define HARDWARE_BARRIER_LOAD      std::atomic_thread_fence(std::memory_order_acquire)
            #define HARDWARE_BARRIER_STORE     std::atomic_thread_fence(std::memory_order_release)
            #define HARDWARE_BARRIER_FULL      std::atomic_thread_fence(std::memory_order_seq_cst)
#else
    #if   defined(_MSC_VER)
            #define COMPILER_BARRIER_LOAD      _ReadBarrier()
            #define COMPILER_BARRIER_STORE     _WriteBarrier()
            #define COMPILER_BARRIER_FULL      _ReadWriteBarrier()
            #define HARDWARE_BARRIER_LOAD      _mm_lfence()
            #define HARDWARE_BARRIER_STORE     _mm_sfence()
            #define HARDWARE_BARRIER_FULl      _mm_mfence()
    #elif defined(__GNUC__)
        #if    defined(__INTEL_COMPILER)
            #define COMPILER_BARRIER_LOAD      __memory_barrier()
            #define COMPILER_BARRIER_STORE     __memory_barrier()
            #define COMPILER_BARRIER_FULL      __memory_barrier()
            #define HARDWARE_BARRIER_LOAD      __mm_lfence()
            #define HARDWARE_BARRIER_STORE     __mm_sfence()
            #define HARDWARE_BARRIER_FULL      __mm_mfence()
        #elif (__GNUC__ > 4) || (__GNUC__ == 4 && __GNUC_MINOR__ >= 1)
            #define COMPILER_BARRIER_LOAD      __asm__ __volatile__ ("" : : : "memory")
            #define COMPILER_BARRIER_STORE     __asm__ __volatile__ ("" : : : "memory")
            #define COMPILER_BARRIER_FULL      __asm__ __volatile__ ("" : : : "memory")
            #define HARDWARE_BARRIER_LOAD      __sync_synchronize()
            #define HARDWARE_BARRIER_STORE     __sync_synchronize()
            #define HARDWARE_BARRIER_FULL      __sync_synchronize()
        #elif defined(__ppc__) || defined(__powerpc__) || defined(__PPC__)
            #define COMPILER_BARRIER_LOAD      __asm__ __volatile__ ("" : : : "memory")
            #define COMPILER_BARRIER_STORE     __asm__ __volatile__ ("" : : : "memory")
            #define COMPILER_BARRIER_FULL      __asm__ __volatile__ ("" : : : "memory")
            #define HARDWARE_BARRIER_LOAD      __asm__ __volatile__ ("lwsync" : : : "memory")
            #define HARDWARE_BARRIER_STORE     __asm__ __volatile__ ("lwsync" : : : "memory")
            #define HARDWARE_BARRIER_FULL      __asm__ __volatile__ ("lwsync" : : : "memory")
        #elif defined(__arm__)
            #define COMPILER_BARRIER_LOAD      __asm__ __volatile__ ("" : : : "memory")
            #define COMPILER_BARRIER_STORE     __asm__ __volatile__ ("" : : : "memory")
            #define COMPILER_BARRIER_FULL      __asm__ __volatile__ ("" : : : "memory")
            #define HARDWARE_BARRIER_LOAD      __asm__ __volatile__ ("dmb" : : : "memory")
            #define HARDWARE_BARRIER_STORE     __asm__ __volatile__ ("dmb" : : : "memory")
            #define HARDWARE_BARRIER_FULL      __asm__ __volatile__ ("dmb" : : : "memory")
        #elif defined(__i386__) || defined(__i486__) || defined(__i586__) || defined(__i686__) || defined(__x86_64__)
            #define COMPILER_BARRIER_LOAD      __asm__ __volatile__ ("" : : : "memory")
            #define COMPILER_BARRIER_STORE     __asm__ __volatile__ ("" : : : "memory")
            #define COMPILER_BARRIER_FULL      __asm__ __volatile__ ("" : : : "memory")
            #define HARDWARE_BARRIER_LOAD      __asm__ __volatile__ ("lfence" : : : "memory")
            #define HARDWARE_BARRIER_STORE     __asm__ __volatile__ ("sfence" : : : "memory")
            #define HARDWARE_BARRIER_FULL      __asm__ __volatile__ ("mfence" : : : "memory")
        #else
            #error Unsupported __GNUC__ (need COMPILER_BARRIER_* and HARDWARE_BARRIER_* intrinsics in compiler_config.h)
        #endif
    #else
        #error Unsupported compiler (need COMPILER_BARRIER_* and HARDWARE_BARRIER_* intrinsics in compiler_config.h)
    #endif
#endif

/// @summary Perform an atomic load of the signed 32-bit integer value at the specified address.
/// @param ptr The address to read.
/// @param mem_order The memory order constraint to apply to the load operation.
/// @return The value stored at the address ptr.
public_function inline int32_t
atomic_load_int32
(
    int32_t      *ptr, 
    int      mem_order
)
{
#if   HAS_GCC_ATOMICS
    return __atomic_load_n(ptr, mem_order);
#else   // aligned loads of 32-bit values are atomic on all modern platforms.
    UNUSED_ARG(mem_order);
    return *ptr;
#endif
}

/// @summary Atomically store a 32-bit signed integer value to an address.
/// @param ptr The address to which the value will be written.
/// @param val The value to write to the specified memory address.
/// @param mem_order The memory ordering constraint to apply to the store operation.
public_function inline void
atomic_store_int32
(
    int32_t       *ptr, 
    int32_t        val,
    int      mem_order
)
{
#if   HAS_GCC_ATOMICS
    __atomic_store_n(ptr, val, mem_order);
#else   // aligned stores of 32-bit values are atomic on all modern platforms.
    UNUSED_ARG(mem_order);
    *ptr = val;
#endif
}

/// @summary Performs an atomic read-modify-write operation, with the modify being an addition operation.
/// @param ptr The address from which to read and write.
/// @param val The value to add to the value loaded from address ptr.
/// @param mem_order The memory ordering constraint to apply to the RWM operation.
/// @return The result of the addition operation.
public_function inline int32_t
atomic_fetch_add_int32
(
    int32_t      *ptr, 
    int32_t       val, 
    int     mem_order
)
{
#if   HAS_GCC_ATOMICS
    return __atomic_fetch_add(ptr, val, mem_order);
#elif HAS_WIN32_ATOMICS
    #if TARGET_ARCHITECTURE == ARCHITECTURE_X86_32 
        UNUSED_ARG(mem_order);
        return _InterlockedExchangeAdd((volatile LONG*) ptr, val);
    #else
        UNUSED_ARG(mem_order);
        return  InterlockedExchangeAdd((volatile LONG*) ptr, val);
    #endif
#else
    #error Unsupported atomics implementation - need atomic_fetch_add_int32 in compiler_config.h
#endif
}

/// @summary Performs an atomic read-modify-write operation, with the modify being a bitwise-OR operation.
/// @param ptr The address from which to read and write.
/// @param val The value to bitwise-OR with the value loaded from address ptr.
/// @param mem_order The memory ordering constraint to apply to the RWM operation.
/// @return The result of the bitwise-OR operation.
public_function inline int32_t
atomic_fetch_or_int32
(
    int32_t      *ptr, 
    int32_t       val, 
    int     mem_order
)
{
#if   HAS_GCC_ATOMICS
    return __atomic_fetch_or(ptr, val, mem_order);
#elif HAS_WIN32_ATOMICS
    #if TARGET_ARCHITECTURE == ARCHITECTURE_X86_32
        UNUSED_ARG(mem_order);
        return _InterlockedOr((volatile LONG*) ptr, val);
    #else
        UNUSED_ARG(mem_order);
        return  InterlockedOr((volatile LONG*) ptr, val);
    #endif
#else
    #error Unsupported atomics implementation - need atomic_fetch_or_int32 in compiler_config.h
#endif
}

/// @summary Performs an atomic compare-and-swap operation.
/// @param ptr The memory address from which to read and write.
/// @param expected The value expected to be stored at the memory address ptr. On return, this value is updated with the value previously stored at address ptr.
/// @param desired The value to write to the memory address ptr if the value loaded from that address matches the expected value.
/// @param weak Specify 1 if spurious failures are allowable. See documentation for std::compare_exchange_weak.
/// @param success The memory ordering constraint to apply if the CAS operation is successful and the value stored at the memory address ptr is updated.
/// @param failure The memory ordering constraint to apply if the CAS operation fails and the value stored at the memory address ptr is not updated.
/// @return true if the value stored at the memory address ptr is updated.
public_function inline bool
atomic_compare_exchange_int32
(
    int32_t      *ptr, 
    int32_t *expected, 
    int32_t   desired, 
    int          weak, 
    int       success, 
    int       failure

)
{
#if   HAS_GCC_ATOMICS
    return __atomic_compare_exchange_n(ptr, expected, desired, weak, success, failure);
#elif HAS_WIN32_ATOMICS
    UNUSED_ARG(weak);
    UNUSED_ARG(success);
    UNUSED_ARG(failure);

    int32_t original;
    int32_t      exp = atomic_load_int32(expected, MEMORY_ORDER_SEQ_CST);
    #if TARGET_ARCHITECTURE == ARCHITECTURE_X86_32
        original = _InterlockedCompareExchange((volatile LONG*) ptr, desired, exp);
    #else
        original =  InterlockedCompareExchange((volatile LONG*) ptr, desired, exp);
    #endif

    atomic_store_int32(expected, original, MEMORY_ORDER_SEQ_CST);
    return (original==exp);
#else
    #error Unsupported atomics implementation - need atomic_compare_exchange_int32 in compiler_config.h
#endif
}

#endif /* !defined(COMPILER_CONFIG_H) */

