// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenScatter core - baked maps.
//
// The host bakes a map (density, later scale or rotation) into an image on its main thread; the core
// samples that image from every thread.

#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

#include "osc/math.h"

namespace osc
{

// One channel of floats. Pixel (0, 0) is at u = 0, v = 0 and rows go towards v = 1: an image that
// comes with its first row at the top has to be handed over flipped.
struct Image
{
    int width = 0;
    int height = 0;
    std::vector<float> pixels; // width * height values, row after row

    bool valid() const
    {
        return width > 0 && height > 0 &&
               pixels.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    }
};

enum class Wrap
{
    Clamp,  // outside [0, 1] the edge pixels go on for ever
    Repeat, // the image tiles
};

// The image at (u, v), blended from the four nearest pixels. 0 when the image is not valid or the
// coordinates are not numbers.
inline float sampleBilinear(const Image& image, float u, float v, Wrap wrap)
{
    if (!image.valid() || !std::isfinite(u) || !std::isfinite(v))
        return 0.0f;

    const auto axis = [wrap](float t, int size, std::size_t& i0, std::size_t& i1, float& blend) {
        // First into [0, 1], so that the pixel position below cannot overflow.
        const double unit = wrap == Wrap::Repeat ? static_cast<double>(t) - std::floor(static_cast<double>(t))
                                                 : (t < 0.0f ? 0.0 : (t > 1.0f ? 1.0 : static_cast<double>(t)));
        const double pos = unit * size - 0.5; // pixel centres are at whole numbers
        const double base = std::floor(pos);
        blend = static_cast<float>(pos - base);
        const long long a = static_cast<long long>(base);
        const long long b = a + 1;
        if (wrap == Wrap::Repeat)
        {
            i0 = static_cast<std::size_t>((a % size + size) % size);
            i1 = static_cast<std::size_t>((b % size + size) % size);
        }
        else
        {
            i0 = static_cast<std::size_t>(a < 0 ? 0 : (a >= size ? size - 1 : a));
            i1 = static_cast<std::size_t>(b < 0 ? 0 : (b >= size ? size - 1 : b));
        }
    };

    std::size_t x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    float tx = 0.0f, ty = 0.0f;
    axis(u, image.width, x0, x1, tx);
    axis(v, image.height, y0, y1, ty);

    const std::size_t w = static_cast<std::size_t>(image.width);
    const float p00 = image.pixels[y0 * w + x0];
    const float p10 = image.pixels[y0 * w + x1];
    const float p01 = image.pixels[y1 * w + x0];
    const float p11 = image.pixels[y1 * w + x1];
    const float top = p00 + (p10 - p00) * tx;
    const float bottom = p01 + (p11 - p01) * tx;
    return top + (bottom - top) * ty;
}

} // namespace osc
