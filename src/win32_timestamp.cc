/*/////////////////////////////////////////////////////////////////////////////
/// @summary Implement functions for retrieving high-resolution timestamps.
///////////////////////////////////////////////////////////////////////////80*/

/*///////////////
//   Globals   //
///////////////*/
/// @summary Stores the frequency value of the high-resolution timer.
global_variable uint64_t GlobalClockFrequency = 0;

/*////////////////////////
//   Public Functions   //
////////////////////////*/
/// @summary Query the frequency of the high-resolution timer and store it in the GlobalClockFrequency. This function should be called once at startup.
public_function void
QueryClockFrequency
(
    void
)
{   // MSDN says this will always succeed on Windows XP and later.
    LARGE_INTEGER ticks_per_second;
    QueryPerformanceFrequency(&ticks_per_second);
    GlobalClockFrequency = uint64_t(ticks_per_second.QuadPart);
}

/// @summary Retrieve a high-resolution timestamp value.
/// @return A high-resolution timestamp. The timestamp is specified in counts per-second.
public_function uint64_t
TimestampInTicks
(
    void
)
{   // MSDN says this will always succeed on Windows XP and later.
    LARGE_INTEGER ticks;
    QueryPerformanceCounter(&ticks);
    return uint64_t(ticks.QuadPart);
}

/// @summary Given two timestamp values, calculate the number of nanoseconds between them.
/// @param start_ticks The TimestampInTicks at the beginning of the measured interval.
/// @param end_ticks The TimestampInTicks at the end of the measured interval.
/// @return The elapsed time between the timestamps, specified in nanoseconds.
public_function uint64_t
ElapsedNanoseconds
(
    uint64_t start_ticks, 
    uint64_t   end_ticks
)
{   // scale the tick value by the nanoseconds-per-second multiplier
    // before scaling back down by ticks-per-second to avoid loss of precision.
    return (1000000000ULL * (end_ticks - start_ticks)) / GlobalClockFrequency;
}

/// @summary Convert a time value specified in milliseconds to nanoseconds.
/// @param milliseconds The time value, in milliseconds.
/// @return The input time value, converted to nanoseconds.
public_function inline uint64_t
MillisecondsToNanoseconds
(
    uint32_t milliseconds
)
{
    return uint64_t(milliseconds) * 1000000ULL;
}

/// @summary Convert a time value specified in nanoseconds to whole milliseconds. Fractional nanoseconds are truncated.
/// @param nanoseconds The time value, in nanoseconds.
/// @return The number of whole milliseconds in the input time value.
public_function inline uint32_t
NanosecondsToWholeMilliseconds
(
    uint64_t nanoseconds
)
{
    return (uint32_t)(nanoseconds / 1000000ULL);
}

