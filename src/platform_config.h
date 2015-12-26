/*/////////////////////////////////////////////////////////////////////////////
/// @summary Define standard macros to detect the target platform and compiler.
///////////////////////////////////////////////////////////////////////////80*/

#ifndef PLATFORM_CONFIG_H
#define PLATFORM_CONFIG_H

/// @summary Define various feature macros relating to threading and atomics.
#define HAS_GCD_SEMAPHORES                0
#define HAS_POSIX_CONDVARS                0
#define HAS_GCC_ATOMICS                   0
#define HAS_POSIX_THREADS                 0
#define HAS_PTHREAD_YIELD_NP              0
#define HAS_POSIX_SEMAPHORES              0
#define HAS_SCHED_YIELD                   0
#define HAS_WIN32_SEMAPHORES              0
#define HAS_WIN32_CONDVARS                0
#define HAS_WIN32_ATOMICS                 0
#define HAS_WIN32_THREADS                 0
#define HAS_WIN32_YIELD                   0

/// @summary Define common platform identifiers for platform detection, so that client platform code isn't scattered with #if defined(...) blocks.
#define PLATFORM_UNKNOWN                  0
#define PLATFORM_iOS                      1
#define PLATFORM_ANDROID                  2
#define PLATFORM_WIN32                    3
#define PLATFORM_WINRT                    4
#define PLATFORM_WINP8                    5
#define PLATFORM_MACOS                    6
#define PLATFORM_LINUX                    7

/// @summary Define common compiler identifiers for compiler detection, so that client platform code isn't scattered with #if defined(...) blocks.
#define COMPILER_UNKNOWN                  0
#define COMPILER_MSVC                     1
#define COMPILER_GNUC                     2

/// @summary Define common CPU architecture identifiers for CPU architecture detection, so that client platform code isn't scattered with #if defined(...) blocks.
#define ARCHITECTURE_UNKNOWN              0
#define ARCHITECTURE_X86_32               1
#define ARCHITECTURE_X86_64               2
#define ARCHITECTURE_ARM_32               3
#define ARCHITECTURE_ARM_64               4
#define ARCHITECTURE_POWER                5

/// @summary Client code can check RTA_TARGET_PLATFORM to get the current target platform.
#define TARGET_PLATFORM                   PLATFORM_UNKNOWN

/// @summary Client code can check RTA_TARGET_COMPILER to get the current target compiler.
#define TARGET_COMPILER                   COMPILER_UNKNOWN

/// @summary Client code can check RTA_TARGET_ARCHITECTURE to get the current CPU architecture.
#define TARGET_ARCHITECTURE               ARCHITECTURE_UNKNOWN

/// @summary Detect Visual C++ compiler. Visual C++ 11.0 (VS2012) is required.
#ifdef _MSC_VER
    #if _MSC_VER < 1700
        #error  Visual C++ 2012 or later is required.
    #else
        #undef  TARGET_COMPILER
        #define TARGET_COMPILER           COMPILER_MSVC
    #endif
#endif

/// @summary Detect GCC-compatible compilers (GNC C/C++, CLANG and Intel C++).
#ifdef __GNUC__
        #undef  TARGET_COMPILER
        #define TARGET_COMPILER           COMPILER_GNUC
#endif

/// @summary Detect Apple platforms (MacOSX and iOS). Legacy MacOS not supported.
#if   defined(__APPLE__)
    #include <TargetConditionals.h>
    #if   defined(TARGET_OS_IPHONE) || defined(TARGET_IPHONE_SIMULATOR)
        #undef  TARGET_PLATFORM
        #define TARGET_PLATFORM           PLATFORM_iOS
    #elif defined(__MACH__)
        #undef  TARGET_PLATFORM
        #define TARGET_PLATFORM           PLATFORM_MACOS
    #else
        #error Legacy MacOS is not supported.
    #endif

    #undef      HAS_GCD_SEMAPHORES
    #undef      HAS_POSIX_CONDVARS
    #undef      HAS_GCC_ATOMICS
    #undef      HAS_POSIX_THREADS
    #undef      HAS_PTHREAD_YIELD_NP

    #define     HAS_GCD_SEMAPHORES        1
    #define     HAS_POSIX_CONDVARS        1
    #define     HAS_GCC_ATOMICS           1
    #define     HAS_POSIX_THREADS         1
    #define     HAS_PTHREAD_YIELD_NP      1
#endif

/// @summary Detect Windows platforms. Desktop platforms are all RTA_PLATFORM_WIN32. Win32 might use MSVC -or- a GNU C-compatible compiler.
#if   defined(WIN32) || defined(WIN64) || defined(WINDOWS)
    #if   defined(WINRT)
        #undef  TARGET_PLATFORM
        #define TARGET_PLATFORM           PLATFORM_WINRT
    #elif defined(WP8)
        #undef  TARGET_PLATFORM
        #define TARGET_PLATFORM           PLATFORM_WINP8
    #else
        #undef  TARGET_PLATFORM
        #define TARGET_PLATFORM           PLATFORM_WIN32
    #endif

    #if defined(_POSIX_THREADS) && defined(_POSIX_SEMAPHORES)
        #undef  HAS_POSIX_SEMAPHORES
        #undef  HAS_POSIX_CONDVARS
        #undef  HAS_GCC_ATOMICS
        #undef  HAS_POSIX_THREADS
        #undef  HAS_SCHED_YIELD

        #define HAS_POSIX_SEMAPHORES      1
        #define HAS_POSIX_CONDVARS        1
        #define HAS_GCC_ATOMICS           1
        #define HAS_POSIX_THREADS         1
        #define HAS_SCHED_YIELD           1
    #else
        #undef  HAS_WIN32_SEMAPHORES
        #undef  HAS_WIN32_CONDVARS
        #undef  HAS_WIN32_ATOMICS
        #undef  HAS_WIN32_THREADS
        #undef  HAS_WIN32_YIELD

        #define HAS_WIN32_SEMAPHORES      1
        #define HAS_WIN32_CONDVARS        1
        #define HAS_WIN32_ATOMICS         1
        #define HAS_WIN32_THREADS         1
        #define HAS_WIN32_YIELD           1
    #endif
#endif

/// @summary Detect Linux and GNU/Linux.
#if   defined(__linux__) || defined(__gnu_linux__) || defined(unix) || defined(__unix__) || defined(__unix)
        #include <unistd.h>
        #undef  TARGET_PLATFORM
        #define TARGET_PLATFORM           PLATFORM_LINUX

    #if defined(_POSIX_THREADS) && defined(_POSIX_SEMAPHORES)
        #undef  HAS_POSIX_SEMAPHORES
        #undef  HAS_POSIX_CONDVARS
        #undef  HAS_GCC_ATOMICS
        #undef  HAS_POSIX_THREADS
        #undef  HAS_SCHED_YIELD

        #define HAS_POSIX_SEMAPHORES      1
        #define HAS_POSIX_CONDVARS        1
        #define HAS_GCC_ATOMICS           1
        #define HAS_POSIX_THREADS         1
        #define HAS_SCHED_YIELD           1
    #endif
#endif

/// @summary Detect Android-based platforms.
#if   defined(ANDROID)
        #include <unistd.h>
        #undef  TARGET_PLATFORM
        #define TARGET_PLATFORM           PLATFORM_ANDROID

    #if defined(_POSIX_THREADS) && defined(_POSIX_SEMAPHORES)
        #undef  HAS_POSIX_SEMAPHORES
        #undef  HAS_POSIX_CONDVARS
        #undef  HAS_GCC_ATOMICS
        #undef  HAS_POSIX_THREADS
        #undef  HAS_SCHED_YIELD

        #define HAS_POSIX_SEMAPHORES      1
        #define HAS_POSIX_CONDVARS        1
        #define HAS_GCC_ATOMICS           1
        #define HAS_POSIX_THREADS         1
        #define HAS_SCHED_YIELD           1
    #endif
#endif

/// @summary Detect 32-bit x86 target processor architecture.
#if    defined(__i386__) || defined(_M_IX86) || defined(_X86_)
        #undef  TARGET_ARCHITECTURE
        #define TARGET_ARCHITECTURE       ARCHITECTURE_X86_32
#endif

/// @summary Detect 64-bit x86 target processor architecture.
#if    defined(__x86_64__) || defined(__amd64__) || defined(_M_X64) || defined(_M_AMD64)
        #undef  TARGET_ARCHITECTURE
        #define TARGET_ARCHITECTURE       ARCHITECTURE_X86_64
#endif

/// @summary Detect 32-bit ARM target processor architecture.
#if    defined(__arm__) || defined(_M_ARM)
        #undef  TARGET_ARCHITECTURE
        #define TARGET_ARCHITECTURE       ARCHITECTURE_ARM_32
#endif

/// @summary Detect 64-bit ARM target processor architecture.
#if    defined(__arm64__) || defined(__arm64) || defined(__aarch64__) || (defined(__arm__) && defined(__LP64__)) || (defined(_M_ARM) && defined(_WIN64))
        #undef  TARGET_ARCHITECTURE
        #define TARGET_ARCHITECTURE       ARCHITECTURE_ARM_64
#endif

/// @summary Detect 32-bit PowerPC target processor architecture.
#if    defined(__powerpc) || defined(__powerpc__) || defined(__POWERPC__) || defined(__ppc__) || defined(__PPC__) || defined(_ARCH_PPC) || defined(_M_PPC)
        #undef  TARGET_ARCHITECTURE
        #define TARGET_ARCHITECTURE       ARCHITECTURE_PPC_32
#endif

/// @summary Detect 64-bit PowerPC target processor architecture.
#if    defined(__powerpc64__) || defined(__ppc64__) || defined(__PPC64__) || defined(_ARCH_PPC64) || defined(_XENON)
        #undef  TARGET_ARCHITECTURE
        #define TARGET_ARCHITECTURE       ARCHITECTURE_PPC_64
#endif

/// @summary Detect unsupported compilers.
#if   TARGET_COMPILER == COMPILER_UNKNOWN
        #error Unsupported compiler (update compiler detection in platform_config.h?)
#endif

/// @summary Detect unsupported target platforms.
#if   TARGET_PLATFORM == PLATFORM_UNKNOWN
        #error Unsupported target platform (update platform detection in platform_config.h?)
#endif

/// @summary Detect unsupported target CPU architecture.
#if   TARGET_ARCHITECTURE == ARCHITECTURE_UNKNOWN
        #error Unsupported target CPU architecture (update architecture detection in platform_config.h?)
#endif

/*////////////////
//   Includes   //
////////////////*/
/// @summary Include the correct intrinsics file based on compiler and target platform.
#if   defined(_MSC_VER)
        #include <intrin.h>
#elif defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
        #include <x86intrin.h>
#elif defined(__GNUC__) &&  defined(__ARM_NEON__)
        #include <arm_neon.h>
#elif defined(__GNUC__) && (defined(__VEC__) || defined(__ALTIVEC__))
        #include <altivec.h>
#else
        #error Unknown intrinsics include file for compiler and/or target architecture (update detection in platform_config.h?)
#endif


#endif /* !defined(PLATFORM_CONFIG_H) */

