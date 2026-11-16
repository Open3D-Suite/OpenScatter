// SPDX-License-Identifier: GPL-3.0-or-later
//
// osc_tilegen - generates a point pattern and writes it to a pattern file (.oscp).
//
// The patterns OpenScatter ships with are made with this tool, so that anyone can regenerate them:
// the same arguments give the same file, bit for bit, on any machine.
//
//   osc_tilegen --out blue_noise_16384.oscp [--points 16384] [--seed 2026] [--oversample 5]
//               [--svg preview.svg]

#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "osc/pattern.h"

namespace
{

void usage()
{
    std::puts("osc_tilegen - generates a progressive blue-noise pattern for OpenScatter\n"
              "\n"
              "  osc_tilegen --out <file.oscp> [--points N] [--seed S] [--oversample K] [--svg <file.svg>]\n"
              "\n"
              "  --out         the pattern file to write\n"
              "  --points      number of points in the pattern (default 16384)\n"
              "  --seed        seed of the random candidates (default 2026)\n"
              "  --oversample  candidates per point, 1 to 64; more is slower and slightly better (default 5)\n"
              "  --svg         also write a picture of the pattern: the first quarter of the points in black");
}

bool parseNumber(std::string_view text, std::uint64_t& value)
{
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc() && result.ptr == text.data() + text.size();
}

bool writeSvg(const osc::Pattern& pattern, const std::filesystem::path& path)
{
    std::ofstream file(path, std::ios::trunc);
    if (!file)
        return false;
    // The radius of a dot: a quarter of the spacing of a perfect packing of that many points.
    const double radius = 0.25 * std::sqrt(1.0 / (0.8660254 * static_cast<double>(pattern.size())));
    file << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1 1\" width=\"1024\" height=\"1024\">\n"
         << "<rect width=\"1\" height=\"1\" fill=\"white\"/>\n";
    for (std::size_t i = pattern.size(); i-- > 0;)   // the first points last, on top
    {
        const osc::Vec2& p = pattern.points[i];
        file << "<circle cx=\"" << p.x << "\" cy=\"" << (1.0f - p.y) << "\" r=\"" << radius << "\" fill=\""
             << (i < pattern.size() / 4 ? "#111" : "#bbb") << "\"/>\n";
    }
    file << "</svg>\n";
    return static_cast<bool>(file);
}

} // namespace

int main(int argc, char** argv)
{
    std::uint64_t points = 16384;
    std::uint64_t seed = 2026;
    std::uint64_t oversample = 5;
    std::string out;
    std::string svg;

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--help" || arg == "-h")
        {
            usage();
            return 0;
        }
        if (arg == "--out" && hasValue)
            out = argv[++i];
        else if (arg == "--svg" && hasValue)
            svg = argv[++i];
        else if (arg == "--points" && hasValue && parseNumber(argv[i + 1], points))
            ++i;
        else if (arg == "--seed" && hasValue && parseNumber(argv[i + 1], seed))
            ++i;
        else if (arg == "--oversample" && hasValue && parseNumber(argv[i + 1], oversample))
            ++i;
        else
        {
            std::fprintf(stderr, "osc_tilegen: bad argument \"%s\"\n\n", argv[i]);
            usage();
            return 1;
        }
    }
    if (out.empty())
    {
        usage();
        return 1;
    }
    if (points == 0 || points > osc::kMaxPatternPoints)
    {
        std::fprintf(stderr, "osc_tilegen: --points must be between 1 and %zu\n", osc::kMaxPatternPoints);
        return 1;
    }

    try
    {
        const auto start = std::chrono::steady_clock::now();
        const osc::Pattern pattern =
            osc::makeProgressiveBlueNoise(static_cast<std::size_t>(points), seed, static_cast<std::size_t>(oversample));
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("pattern: %zu points, seed %llu, built in %.2f s\n", pattern.size(),
                    static_cast<unsigned long long>(seed), seconds);
        std::printf("id: %016llx\n", static_cast<unsigned long long>(pattern.id));

        // How well spread every prefix is: 1 would be a perfect packing, random points give about 0.02.
        for (std::size_t count = 64; count <= pattern.size() && count <= 4096; count *= 4)
            std::printf("  first %5zu points: closest pair at %.2f of the ideal spacing\n", count,
                        osc::prefixSpacing(pattern, count));

        std::string error;
        if (!osc::savePattern(pattern, out, &error))
        {
            std::fprintf(stderr, "osc_tilegen: %s: %s\n", out.c_str(), error.c_str());
            return 2;
        }
        osc::Pattern check;
        if (!osc::loadPattern(out, check, &error) || check.id != pattern.id)
        {
            std::fprintf(stderr, "osc_tilegen: %s: the file does not read back (%s)\n", out.c_str(), error.c_str());
            return 2;
        }
        std::printf("written: %s\n", out.c_str());

        if (!svg.empty())
        {
            if (!writeSvg(pattern, svg))
            {
                std::fprintf(stderr, "osc_tilegen: %s: the picture could not be written\n", svg.c_str());
                return 2;
            }
            std::printf("written: %s\n", svg.c_str());
        }
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "osc_tilegen: %s\n", e.what());
        return 2;
    }
    return 0;
}
