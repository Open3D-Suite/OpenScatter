// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenScatter core - trigonometry that gives the same bits on every machine.
//
// The scatter must be identical on every render node. The C library's sin and cos are not guaranteed
// to be: an implementation may take a different code path on a CPU with other instructions and differ
// in the last bit, which is enough to move an instance across a threshold. These use only additions and
// multiplications, which IEEE 754 defines exactly. (The build must not contract a * b + c into one fused
// operation: see the compile options in core/CMakeLists.txt.)

#pragma once

#include <cmath>

namespace osc
{

// Sine and cosine of an angle given in turns (1 turn = 360 degrees). Accurate to about 1e-14.
inline void sinCosTurns(double turns, double& s, double& c)
{
    if (!std::isfinite(turns))
    {
        s = 0.0;
        c = 1.0;
        return;
    }
    const double x = (turns - std::floor(turns)) * 4.0;   // in quarter turns: [0, 4]
    const double q = std::floor(x + 0.5);                 // the nearest quarter turn
    const double a = (x - q) * 1.5707963267948966;        // what is left: [-pi/4, pi/4]
    const double a2 = a * a;

    // Taylor series; on this interval the first term left out is below 1e-13.
    const double sn = a * (1.0 + a2 * (-1.0 / 6.0 + a2 * (1.0 / 120.0 + a2 * (-1.0 / 5040.0
                    + a2 * (1.0 / 362880.0 + a2 * (-1.0 / 39916800.0 + a2 * (1.0 / 6227020800.0)))))));
    const double cs = 1.0 + a2 * (-1.0 / 2.0 + a2 * (1.0 / 24.0 + a2 * (-1.0 / 720.0 + a2 * (1.0 / 40320.0
                    + a2 * (-1.0 / 3628800.0 + a2 * (1.0 / 479001600.0 + a2 * (-1.0 / 87178291200.0)))))));

    switch (static_cast<int>(q) & 3)
    {
    case 0:  s = sn;  c = cs;  break;
    case 1:  s = cs;  c = -sn; break;
    case 2:  s = -sn; c = -cs; break;
    default: s = -cs; c = sn;  break;
    }
}

inline void sinCosDegrees(float degrees, float& s, float& c)
{
    double ds = 0.0;
    double dc = 1.0;
    sinCosTurns(static_cast<double>(degrees) / 360.0, ds, dc);
    s = static_cast<float>(ds);
    c = static_cast<float>(dc);
}

inline float cosDegrees(float degrees)
{
    float s = 0.0f;
    float c = 1.0f;
    sinCosDegrees(degrees, s, c);
    return c;
}

} // namespace osc
