#pragma once

#include <stdint.h>
#include <string.h>
#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

// Single-precision ADD, SUB, MUL and DIV without softfloat in the common case.
//
// When both operands are zero or normal numbers and the rounding mode is
// round to nearest even, the result computed in double precision and then
// rounded to float is the correctly rounded float result: double has more
// than 2 * 24 + 2 bits of precision, so rounding twice gives the same value
// as rounding once. Such an operation can only raise "inexact", and each
// function below detects it exactly. Whenever the result could underflow,
// overflow or involve an infinity, the functions return false and the caller
// uses softfloat as before.
//
// Only enabled on x64, where double arithmetic is SSE2 and follows MXCSR. The
// Win32 build compiles floating point for the x87 unit.
namespace InterpreterFpuFast
{
#if defined(_M_X64) || defined(__x86_64__)
inline bool Available(uint32_t RoundingMode)
{
    // R4300 rounding mode 0 is round to nearest; the host must round the same way
    return RoundingMode == 0 && (_mm_getcsr() & 0x6000) == 0;
}
#else
inline bool Available(uint32_t /*RoundingMode*/)
{
    return false;
}
#endif

// Zero or a normal number: not a subnormal, an infinity or a NaN
inline bool Ordinary(uint32_t Value)
{
    uint32_t Exponent = Value & 0x7F800000;
    return Exponent != 0x7F800000 && (Exponent != 0 || (Value & 0x007FFFFF) == 0);
}

inline double ToDouble(uint32_t Value)
{
    float Result;
    memcpy(&Result, &Value, sizeof(Result));
    return Result;
}

// Rounds to float; false when the result is infinite or not above the
// smallest normal, where underflow and overflow rules apply. An exact zero is
// accepted.
inline bool RoundToFloat(double Value, float & Result)
{
    Result = (float)Value;
    uint32_t Bits;
    memcpy(&Bits, &Result, sizeof(Bits));
    uint32_t Exponent = Bits & 0x7F800000;
    if (Exponent == 0x7F800000)
    {
        return false;
    }
    return Exponent > 0x00800000 || Value == 0.0;
}

inline uint32_t ToBits(float Value)
{
    uint32_t Bits;
    memcpy(&Bits, &Value, sizeof(Bits));
    return Bits;
}

inline bool Add(uint32_t A, uint32_t B, uint32_t & Result, bool & Inexact)
{
    double X = ToDouble(A), Y = ToDouble(B);
    double Sum = X + Y;
    // Error of the double sum (Knuth's TwoSum): X + Y == Sum + Error exactly
    double YPart = Sum - X;
    double Error = (X - (Sum - YPart)) + (Y - YPart);
    float Rounded;
    if (!RoundToFloat(Sum, Rounded))
    {
        return false;
    }
    Result = ToBits(Rounded);
    Inexact = Error != 0.0 || (double)Rounded != Sum;
    return true;
}

inline bool Sub(uint32_t A, uint32_t B, uint32_t & Result, bool & Inexact)
{
    return Add(A, B ^ 0x80000000, Result, Inexact);
}

inline bool Mul(uint32_t A, uint32_t B, uint32_t & Result, bool & Inexact)
{
    // Two 24-bit significands: the double product is exact
    double Product = ToDouble(A) * ToDouble(B);
    float Rounded;
    if (!RoundToFloat(Product, Rounded))
    {
        return false;
    }
    Result = ToBits(Rounded);
    Inexact = (double)Rounded != Product;
    return true;
}

inline bool Div(uint32_t A, uint32_t B, uint32_t & Result, bool & Inexact)
{
    double X = ToDouble(A), Y = ToDouble(B);
    if (Y == 0.0)
    {
        return false;
    }
    float Rounded;
    if (!RoundToFloat(X / Y, Rounded))
    {
        return false;
    }
    Result = ToBits(Rounded);
    // The quotient is exact when the rounded result times the divisor (an
    // exact double product) gives back the dividend
    Inexact = (double)Rounded * Y != X;
    return true;
}
} // namespace InterpreterFpuFast
