// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tests of the OpenScatter core: the progressive blue-noise generator and pattern files.

#include <doctest/doctest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

#include "helpers.h"

namespace
{

// A file in the temporary folder that is removed when the test ends.
struct TempFile
{
    std::filesystem::path path;
    explicit TempFile(const char* name) : path(std::filesystem::temp_directory_path() / name)
    {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
    ~TempFile()
    {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
};

std::vector<char> readAll(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::vector<char>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

void writeAll(const std::filesystem::path& path, const std::vector<char>& bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

TEST_CASE("pattern: the generator gives the points asked for, inside the unit square")
{
    const osc::Pattern& pattern = helpers::testPattern();
    CHECK(pattern.size() == 4096);
    CHECK(osc::isValid(pattern));
    CHECK(pattern.id == osc::hashPattern(pattern));
    CHECK(pattern.id != 0);
}

TEST_CASE("pattern: the same arguments give the same pattern, other seeds another")
{
    const osc::Pattern a = osc::makeProgressiveBlueNoise(512, 11);
    const osc::Pattern b = osc::makeProgressiveBlueNoise(512, 11);
    const osc::Pattern c = osc::makeProgressiveBlueNoise(512, 12);
    CHECK(a.id == b.id);
    CHECK(a.id != c.id);
}

TEST_CASE("pattern: stored hashes (the generator must give these on every machine)")
{
    // If one of these changes, the generator changed: every pattern shipped has to be regenerated and
    // kAlgorithmVersion raised, because old scenes would scatter differently.
    CHECK(osc::makeProgressiveBlueNoise(1024, 2026).id == 0x958B3B8AC9C0348Eull);
    CHECK(helpers::defaultPattern().id == 0x367AFC7339581055ull);
}

TEST_CASE("pattern: every prefix is well spread")
{
    const osc::Pattern& pattern = helpers::testPattern();
    for (const std::size_t count : { std::size_t(16), std::size_t(64), std::size_t(256), std::size_t(1024),
                                     std::size_t(4096) })
    {
        CAPTURE(count);
        CHECK(osc::prefixSpacing(pattern, count) > 0.5f);
    }

    // Purely random points for comparison: nowhere near.
    osc::Pattern white;
    for (std::uint64_t i = 0; i < 1024; ++i)
    {
        const std::uint64_t h = osc::hashCombine(7, i);
        white.points.push_back({ osc::unitFloat(h), osc::unitFloat(osc::mix64(h)) });
    }
    CHECK(osc::prefixSpacing(white, 1024) < 0.2f);
}

TEST_CASE("pattern: the points cover the square evenly")
{
    // 4 096 points in 8 x 8 cells: 64 per cell on average, and blue noise stays close to it.
    const osc::Pattern& pattern = helpers::testPattern();
    int cells[64] = {};
    for (const osc::Vec2& p : pattern.points)
        ++cells[static_cast<int>(p.y * 8.0f) * 8 + static_cast<int>(p.x * 8.0f)];
    for (const int count : cells)
    {
        CHECK(count > 48);
        CHECK(count < 80);
    }
}

TEST_CASE("pattern: edge cases of the generator")
{
    CHECK(osc::makeProgressiveBlueNoise(0, 1).empty());
    CHECK(osc::makeProgressiveBlueNoise(osc::kMaxPatternPoints + 1, 1).empty());
    for (const std::size_t count : { std::size_t(1), std::size_t(2), std::size_t(3), std::size_t(7), std::size_t(65) })
    {
        const osc::Pattern p = osc::makeProgressiveBlueNoise(count, 5);
        CHECK(p.size() == count);
        CHECK(osc::isValid(p));
    }
    // The oversampling is clamped to something sensible at both ends.
    CHECK(osc::makeProgressiveBlueNoise(32, 5, 0).size() == 32);
    CHECK(osc::makeProgressiveBlueNoise(32, 5, 1).size() == 32);
    CHECK(osc::makeProgressiveBlueNoise(32, 5, 100000).size() == 32);

    CHECK(osc::prefixSpacing(osc::Pattern{}, 10) == 1.0f);
    CHECK(osc::prefixSpacing(helpers::testPattern(), 1) == 1.0f);
}

TEST_CASE("pattern: validity")
{
    osc::Pattern p;
    CHECK(osc::isValid(p));
    p.points = { { 0.0f, 0.5f }, { 0.999f, 0.0f } };
    CHECK(osc::isValid(p));
    p.points.push_back({ 1.0f, 0.5f });
    CHECK_FALSE(osc::isValid(p));
    p.points.back() = { 0.5f, -0.001f };
    CHECK_FALSE(osc::isValid(p));
    p.points.back() = { std::numeric_limits<float>::quiet_NaN(), 0.5f };
    CHECK_FALSE(osc::isValid(p));
}

TEST_CASE("pattern: a file reads back as the same pattern")
{
    const TempFile file("osc_test_roundtrip.oscp");
    const osc::Pattern original = osc::makeProgressiveBlueNoise(300, 3);
    std::string error;
    REQUIRE(osc::savePattern(original, file.path, &error));
    CHECK(std::filesystem::file_size(file.path) == 16 + 300 * 8 + 8);

    osc::Pattern loaded;
    REQUIRE(osc::loadPattern(file.path, loaded, &error));
    CHECK(loaded.size() == original.size());
    CHECK(loaded.id == original.id);
    CHECK(osc::hashPattern(loaded) == osc::hashPattern(original));
}

TEST_CASE("pattern: files that are not good patterns are refused, with a reason")
{
    const TempFile good("osc_test_good.oscp");
    const TempFile bad("osc_test_bad.oscp");
    const osc::Pattern original = osc::makeProgressiveBlueNoise(64, 3);
    REQUIRE(osc::savePattern(original, good.path));
    const std::vector<char> bytes = readAll(good.path);
    REQUIRE(bytes.size() == 16 + 64 * 8 + 8);

    osc::Pattern loaded = original;
    std::string error;

    SUBCASE("a file that does not exist")
    {
        CHECK_FALSE(osc::loadPattern(bad.path, loaded, &error));
    }
    SUBCASE("an empty file")
    {
        writeAll(bad.path, {});
        CHECK_FALSE(osc::loadPattern(bad.path, loaded, &error));
    }
    SUBCASE("another kind of file")
    {
        std::vector<char> other = bytes;
        other[0] = 'X';
        writeAll(bad.path, other);
        CHECK_FALSE(osc::loadPattern(bad.path, loaded, &error));
    }
    SUBCASE("a version from the future")
    {
        std::vector<char> other = bytes;
        other[8] = 99;
        writeAll(bad.path, other);
        CHECK_FALSE(osc::loadPattern(bad.path, loaded, &error));
    }
    SUBCASE("cut short")
    {
        writeAll(bad.path, std::vector<char>(bytes.begin(), bytes.end() - 5));
        CHECK_FALSE(osc::loadPattern(bad.path, loaded, &error));
    }
    SUBCASE("too long")
    {
        std::vector<char> other = bytes;
        other.push_back(0);
        writeAll(bad.path, other);
        CHECK_FALSE(osc::loadPattern(bad.path, loaded, &error));
    }
    SUBCASE("a point count that cannot be")
    {
        std::vector<char> other = bytes;
        other[12] = other[13] = other[14] = other[15] = static_cast<char>(0xFF);
        writeAll(bad.path, other);
        CHECK_FALSE(osc::loadPattern(bad.path, loaded, &error));
        other[12] = other[13] = other[14] = other[15] = 0;
        writeAll(bad.path, other);
        CHECK_FALSE(osc::loadPattern(bad.path, loaded, &error));
    }
    SUBCASE("one bit of one coordinate flipped")
    {
        std::vector<char> other = bytes;
        other[16 + 40] = static_cast<char>(other[16 + 40] ^ 0x01);
        writeAll(bad.path, other);
        CHECK_FALSE(osc::loadPattern(bad.path, loaded, &error));
    }
    SUBCASE("a point outside the square")
    {
        std::vector<char> other = bytes;
        const float two = 2.0f;
        std::memcpy(other.data() + 16, &two, sizeof(two));
        writeAll(bad.path, other);
        CHECK_FALSE(osc::loadPattern(bad.path, loaded, &error));
    }

    // Whatever went wrong, there is a reason and the pattern handed back is empty, never half read.
    CHECK_FALSE(error.empty());
    CHECK(loaded.empty());
}

TEST_CASE("pattern: saving refuses what could not be loaded back")
{
    const TempFile file("osc_test_refused.oscp");
    std::string error;
    CHECK_FALSE(osc::savePattern(osc::Pattern{}, file.path, &error));
    CHECK_FALSE(error.empty());

    osc::Pattern outside;
    outside.points = { { 0.5f, 1.5f } };
    CHECK_FALSE(osc::savePattern(outside, file.path, &error));
    CHECK_FALSE(std::filesystem::exists(file.path));

    // A folder that does not exist: an error, not an exception.
    const osc::Pattern fine = osc::makeProgressiveBlueNoise(8, 1);
    CHECK_FALSE(osc::savePattern(fine, std::filesystem::temp_directory_path() / "osc_no_such_folder" / "p.oscp", &error));
}
