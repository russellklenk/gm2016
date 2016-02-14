/*/////////////////////////////////////////////////////////////////////////////
/// @summary Implement random number generation and supporting routines based 
/// on the WELL family of pseudo-random number generators. 
///////////////////////////////////////////////////////////////////////////80*/

/*/////////////////
//   Constants   //
/////////////////*/
/// @summary Define the number of uint32_t values that comprise a seed value.
#ifndef WELL512_SEED_UNITS
#define WELL512_SEED_UNITS    (16)
#endif

/// @summary Define the number of bytes that comprise a seed value.
#ifndef WELL512_SEED_SIZE
#define WELL512_SEED_SIZE     (WELL512_SEED_UNITS * sizeof(uint32_t))
#endif

/// @summary Define the number of bytes required to store the state data for a WELL512 PRNG.
#ifndef WELL512_STATE_SIZE
#define WELL512_STATE_SIZE    (17U * sizeof(uint32_t))
#endif 

/// @summary Define the maximum value that can be output by the WELL512 PRNG.
#ifndef WELL512_RAND_MAX
#define WELL512_RAND_MAX      (4294967295ULL)
#endif

/// @summary The value 1.0 / (WELL512_RAND_MAX + 1.0) as an IEEE-754 double.
#ifndef WELL512_RAND_SCALE
#define WELL512_RAND_SCALE    (2.32830643653869628906e-10)
#endif

/*//////////////////
//   Data Types   //
//////////////////*/
/// @summary Define the state data associated with a WELL512 PRNG instance. The application allocates a new instance of this structure, and initializes it using the InitWell512PRNG function.
struct WELL512_PRNG_STATE
{
    uint32_t Index;     /// The current index into the state block.
    uint32_t State[16]; /// The current state of the RNG.
};

/*//////////////////////////
//   Internal Functions   //
//////////////////////////*/

/*////////////////////////
//   Public Functions   //
////////////////////////*/
/// @summary Retrieve the size of the seed data for the WELL512 PRNG.
/// @return The number of bytes of seed data required for a WELL512 PRNG.
public_function inline size_t
Well512SeedSize
(
    void
)
{
    return WELL512_SEED_SIZE;
}

/// @summary Retrieve the size of the state data for the WELL512 PRNG.
/// @return The number of bytes required to represent the state of a WELL512 PRNG.
public_function inline size_t
Well512StateSize
(
    void
)
{
    return WELL512_STATE_SIZE;
}

/// @summary Initialize the state of a WELL512 PRNG instance.
/// @param rng The PRNG state to initialize.
public_function void
InitWell512PRNG
(
    WELL512_PRNG_STATE *rng
)
{
    ZeroMemory(rng, sizeof(WELL512_PRNG_STATE));
}

/// @summary Seed a WELL512 PRNG instance using externally-generated data.
/// @param rng The PRNG state to seed.
/// @param seed_data The externally-generated seed data.
/// @param seed_size The number of bytes of seed data. This must be at least the value returned by Well512SeedSize.
/// @return Zero if the seed data was used to initialize the PRNG, or -1 if an error occurred.
public_function int
SeedWell512PRNG
(
    WELL512_PRNG_STATE      *rng, 
    void   const      *seed_data,
    size_t const       seed_size   
)
{
    if (seed_size < WELL512_SEED_SIZE)
    {   // insufficient seed data supplied.
        return -1;
    }
    rng->Index = 0;
    CopyMemory(rng->State, seed_data, WELL512_SEED_SIZE);
    return 0;
}

/// @summary Initialize a WELL512 PRNG using previously saved state.
/// @param rng The PRNG state to initialize.
/// @param state_data The previously saved state data.
/// @param state_size The size of the saved state data, in bytes. This must be at least the value returned by Well512StateSize.
/// @return Zero if the PRNG state data was successfully loaded, or -1 if an error occurred.
public_function int
LoadWell512PRNG
(
    WELL512_PRNG_STATE       *rng, 
    void   const      *state_data, 
    size_t const       state_size
)
{
    if (state_size < WELL512_STATE_SIZE)
    {   // the supplied data has an incorrect size.
        return -1;
    }
    CopyMemory(rng, state_data, WELL512_STATE_SIZE);
    return 0;
}

/// @summary Save the state of a WELL512 PRNG instance.
/// @param dst THe destination buffer where the state will be copied.
/// @param src The WELL512 PRNG whose state will be saved.
/// @param size The size of the destination buffer, in bytes. This must be at least the value returned by Well512StateSize.
/// @return The number of bytes copied to the destination buffer, or 0 if an error occurred.
public_function size_t
CopyWell512PRNG
(
    void                     *dst, 
    WELL512_PRNG_STATE const *src, 
    size_t             const size
)
{
    if (size < WELL512_STATE_SIZE)
    {   // the destination buffer is too small.
        return false;
    }
    CopyMemory(dst, src, WELL512_STATE_SIZE);
    return WELL512_STATE_SIZE;
}

/// @summary Draws a single double-precision IEEE-754 floating point value from a PRNG. Values are uniformly distributed over the range [0, 1).
/// @param rng The WELL512 PRNG instance to draw from.
/// @return A value selected from the range [0, 1).
public_function double
RandomUniformReal
(
    WELL512_PRNG_STATE *rng
)
{
    uint32_t &r = rng->Index;
    uint32_t *s = rng->State;
    uint32_t  n = r;
    uint32_t  a = s[n];
    uint32_t  b = 0;
    uint32_t  c = s[(n + 13) & 15];
    uint32_t  d = 0;
    b           = a ^ c ^ (a << 16) ^ (c << 15);
    c           = s[(n + 9)   & 15];
    c          ^= (c >> 11);
    a           = s[n] = b ^ c;
    d           = a ^ ((a << 5) & 0xDA442D24UL);
    r           = (n + 15) & 15;
    n           = (n + 15) & 15;
    a           = s[n];
    s[n]        = a ^ b ^ d ^ (a << 2) ^ (b << 18) ^ (c << 28);
    return s[n] * WELL512_RAND_SCALE;
}

/// @summary Retrieves 32 random bits from a PRNG. The bits are returned without any transformation performed and the full range [0, UINT32_MAX] is possible.
/// @param rng The PRNG instance to draw from.
/// @return A value selected from the range [0, 4294967295].
public_function uint32_t
RandomU32
(
    WELL512_PRNG_STATE *rng
)
{
    uint32_t &r = rng->Index;
    uint32_t *s = rng->State;
    uint32_t  n = r;
    uint32_t  a = s[n];
    uint32_t  b = 0;
    uint32_t  c = s[(n + 13) & 15];
    uint32_t  d = 0;
    b           = a ^ c ^ (a << 16) ^ (c << 15);
    c           = s[(n + 9)   & 15];
    c          ^= (c >> 11);
    a           = s[n] = b ^ c;
    d           = a ^ ((a << 5) & 0xDA442D24UL);
    r           = (n + 15) & 15;
    n           = (n + 15) & 15;
    a           = s[n];
    s[n]        = a ^ b ^ d ^ (a << 2) ^ (b << 18) ^ (c << 28);
    return s[n];
}

/// @summary Draws a 32-bit unsigned integer value from a PRNG. Values are uniformly distributed over the range [min_value, max_value). The caller must ensure that @a min_value is less than @a max_value, and that the min and max values are within the defined range, or the function may return invalid results.
/// @param min_value The inclusive lower-bound of the range. The maximum allowable value is UINT32_MAX + 1 (4294967296).
/// @param max_value The exclusive upper-bound of the range. The maximum allowable value is UINT32_MAX + 1 (4294967296).
/// @param rng The PRNG instance to draw from.
/// @return A value selected from the range [min_value, max_value).
public_function uint32_t
RandomU32InRange
(
    uint64_t           min_value, 
    uint64_t           max_value,
    WELL512_PRNG_STATE      *rng
)
{   // NOTE: max_value must be greater than min_value (or we divide by zero).
    // NOTE: the max value of max_value is UINT32_MAX + 1.
    // see http://www.azillionmonkeys.com/qed/random.html
    // see http://en.wikipedia.org/wiki/Fisher-Yates_shuffle#Modulo_bias
    // remove the bias that can result when the range 'r'
    // does not divide evenly into the PRNG range 'n'.
    uint64_t r = max_value - min_value; // size of request range [min, max)
    uint64_t u = WELL512_RAND_MAX;      // PRNG inclusive upper bound
    uint64_t n = u + 1;                 // size of PRNG range [0, UINT32_MAX]
    uint64_t i = n / r;                 // # times whole of 'r' fits in 'n'
    uint64_t m = r * i;                 // largest integer multiple of 'r'<='n'
    uint64_t x = 0;                     // raw value from PRNG
    do
    {
        x = RandomU32(rng);             // x in [0, UINT32_MAX]
    } while (x >= m);
    x /= i;                             // x -> [0, r) and [0, UINT32_MAX]
    return uint32_t(x + min_value);     // x -> [min, max)
}

/// @summary Draw and discard a random number of values from a PRNG to randomize the PRNG state.
/// @param rng The PRNG to cycle.
/// @return The sum of the drawn values.
#pragma optimize("", off)
public_function uint64_t
CyclePRNG
(
    WELL512_PRNG_STATE *rng
)
{
    uint64_t      V = 0;
    for (uint32_t i = 0, n = RandomU32InRange(1, WELL512_SEED_UNITS * 2, rng); i < n; ++i)
    {
        V += RandomU32(rng);
    }
    return V;
}
#pragma optimize("", on)

/// @summary Generate a non-random sequence of integers in ascending order.
/// @param values The array of values to populate.
/// @param start The first value in the sequence.
/// @param count The number of values in the sequence.
public_function void
Sequence
(
    uint32_t      *values, 
    uint32_t const  start, 
    size_t   const  count
)
{
    for (uint32_t i = 0, n = uint32_t(count); i < n; ++i)
    {
        *values++ = start + i;
    }
}

/// @summary Shuffle the values in an array using the Knuth-Fisher-Yates algorithm.
/// @param values The array of values to shuffle.
/// @param count The number of values in the array.
/// @param rng The PRNG used to perform the random shuffling.
public_function void
Shuffle
(
    uint32_t          *values, 
    size_t const        count, 
    WELL512_PRNG_STATE   *rng
)
{   // algorithm 3.4.2P of The Art of Computer Programming, Vol. 2.
    size_t n = count;
    while (n > 1)
    {
        uint32_t k = RandomU32InRange(0, n, rng); // k in [0, n)
        uint32_t t = values[k];                   // swap values[k] and values[n-1]
        --n;                                      // n decreases every iteration
        values[k]  = values[n];
        values[n]  = t;
    }
}

/// @summary Samples a set without replacement. All values in the set have uniform weight; that is, all values are equally likely to be chosen. Each value in the set can only be chosen once. Values are returned in ascending order. Use Shuffle() to randomize the sampled values.
/// @param values Pointer to storage for an array of 32-bit unsigned integer values that will be set to the sampled values. Values are stored in ascending order.
/// @param population_size The total size of the population being sampled. The maximum allowable value is UINT32_MAX + 1 (4294967296).
/// @param sample_size The number of samples being drawn from the set. The maximum allowable value is UINT32_MAX + 1 (4294967296).
/// @param rng The state of the random number generator to be used when sampling the population.
public_function void
ChooseWithoutReplacement
(
    uint32_t                  *values, 
    uint64_t const    population_size, 
    uint64_t const        sample_size, 
    WELL512_PRNG_STATE           *rng
)
{   // algorithm 3.4.2S of The Art of Computer Programming, Vol. 2
    assert(sample_size     <= (uint64_t(UINT32_MAX)+1));
    assert(population_size <= (uint64_t(UINT32_MAX)+1));
    uint64_t n = sample_size;      // max allowable is UINT32_MAX + 1
    uint64_t N = population_size;  // max allowable is UINT32_MAX + 1
    uint32_t t = 0;                // total dealt with so far
    uint32_t m = 0;                // number selected so far
    while   (m < n)
    {
        double v = RandomUniformReal(rng);
        if ((N - t) * v >= (n - m))
        {
            ++t;
        }
        else
        {
            values[m++] = t++;
        }
    }
}

/// @summary Samples a set with replacement. All values in the set have uniform weight; that is, all values are equally likely to be chosen. Each value in the set may be selected multiple times.
/// @param population_size The total size of the population being sampled. The maximum allowable value is UINT32_MAX + 1 (4294967296).
/// @param sample_size The number of samples being drawn from the set. The maximum allowable value is UINT32_MAX + 1 (4294967296).
/// @param values Pointer to storage for an array of 32-bit unsigned integer values that will be set to the sampled values. Values are stored in ascending order.
/// @param rng The state of the random number generator to be used when sampling the population.
public_function void
ChooseWithReplacement
(
    uint32_t                  *values, 
    uint64_t const    population_size, 
    uint64_t const        sample_size, 
    WELL512_PRNG_STATE           *rng
)
{
    for (uint64_t i = 0; i < sample_size; ++i)
    {
        *values++ = RandomU32InRange(0, population_size, rng);
    }
}

