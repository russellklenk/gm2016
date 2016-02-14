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
public_function inline size_t
Well512SeedSize
(
    void
)
{
    return WELL512_SEED_SIZE;
}

public_function inline size_t
Well512StateSize
(
    void
)
{
    return WELL512_STATE_SIZE;
}

public_function void
InitWell512PRNG
(
    WELL512_PRNG_STATE *rng
)
{
    ZeroMemory(rng, sizeof(WELL512_PRNG_STATE));
}

public_function bool
SeedWell512PRNG
(
    WELL512_PRNG_STATE      *rng, 
    void   const      *seed_data,
    size_t const       seed_size   
)
{
    if (seed_size < WELL512_SEED_SIZE)
    {   // insufficient seed data supplied.
        return false;
    }
    rng->Index = 0;
    CopyMemory(rng->State, seed_data, WELL512_SEED_SIZE);
    return true;
}

public_function bool
LoadWell512PRNG
(
    WELL512_PRNG_STATE       *rng, 
    void   const      *state_data, 
    size_t const       state_size
)
{
    if (state_size < WELL512_STATE_SIZE)
    {   // the supplied data has an incorrect size.
        return false;
    }
    CopyMemory(rng, state_data, WELL512_STATE_SIZE);
    return true;
}

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
    uint64_t u = RNG_RAND_MAX;          // PRNG inclusive upper bound
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

#pragma optimize("", off)
public_function void
CyclePRNG
(
    WELL512_PRNG_STATE *rng
)
{
}
#pragma optimize("", on)

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

public_function void
ChooseWithoutReplacement
(
    uint32_t                  *values, 
    uint64_t const    population_size, 
    uint64_t const        sample_size, 
    WELL512_PRNG_STATE           *rng
)
{   // algorithm 3.4.2S of The Art of Computer Programming, Vol. 2
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

