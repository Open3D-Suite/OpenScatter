// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenScatter core - point patterns: the progressive blue-noise generator and pattern files.
//
// The generator follows weighted sample elimination as described in Cem Yuksel's 2015 paper; the code
// is ours. It uses no C library function whose result may vary between machines (no pow, exp or
// trigonometry: only arithmetic and square roots), so a pattern can be regenerated bit for bit anywhere.

#include "osc/pattern.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <utility>

#include "osc/hash.h"

namespace osc
{

namespace
{

float torusDistance(Vec2 a, Vec2 b)
{
    float dx = std::fabs(a.x - b.x);
    float dy = std::fabs(a.y - b.y);
    dx = std::min(dx, 1.0f - dx);
    dy = std::min(dy, 1.0f - dy);
    return std::sqrt(dx * dx + dy * dy);
}

// The largest radius n discs can have in a 2D domain of area 1 (hexagonal packing).
float maxRadius(std::size_t n)
{
    return std::sqrt(1.0f / (2.0f * std::sqrt(3.0f) * static_cast<float>(n)));
}

// A max-heap over a fixed set of items whose keys can only go down.
class MaxHeap
{
public:
    MaxHeap(const std::vector<std::int32_t>& items, const std::vector<float>& keys, std::size_t universe)
        : m_heap(items), m_keys(keys), m_pos(universe, -1)
    {
        for (std::size_t i = 0; i < m_heap.size(); ++i)
            m_pos[static_cast<std::size_t>(m_heap[i])] = static_cast<std::int32_t>(i);
        for (std::size_t i = m_heap.size() / 2; i-- > 0;)
            siftDown(i);
    }

    bool contains(std::int32_t item) const { return m_pos[static_cast<std::size_t>(item)] >= 0; }

    std::int32_t pop()
    {
        const std::int32_t top = m_heap[0];
        m_pos[static_cast<std::size_t>(top)] = -1;
        m_heap[0] = m_heap.back();
        m_heap.pop_back();
        if (!m_heap.empty())
        {
            m_pos[static_cast<std::size_t>(m_heap[0])] = 0;
            siftDown(0);
        }
        return top;
    }

    // The key of "item" went down.
    void lowered(std::int32_t item)
    {
        const std::int32_t pos = m_pos[static_cast<std::size_t>(item)];
        if (pos >= 0)
            siftDown(static_cast<std::size_t>(pos));
    }

private:
    float key(std::size_t heapIndex) const { return m_keys[static_cast<std::size_t>(m_heap[heapIndex])]; }

    void siftDown(std::size_t i)
    {
        const std::size_t n = m_heap.size();
        for (;;)
        {
            std::size_t largest = i;
            const std::size_t l = 2 * i + 1;
            const std::size_t r = l + 1;
            if (l < n && key(l) > key(largest))
                largest = l;
            if (r < n && key(r) > key(largest))
                largest = r;
            if (largest == i)
                return;
            std::swap(m_heap[i], m_heap[largest]);
            m_pos[static_cast<std::size_t>(m_heap[i])] = static_cast<std::int32_t>(i);
            m_pos[static_cast<std::size_t>(m_heap[largest])] = static_cast<std::int32_t>(largest);
            i = largest;
        }
    }

    std::vector<std::int32_t> m_heap;
    const std::vector<float>& m_keys;
    std::vector<std::int32_t> m_pos;
};

struct Neighbour
{
    std::int32_t index;
    float weight;
};

// Removes points from "active" until "target" remain, each time the one that crowds its neighbours
// most. The removed ones are appended to "order" (when given) in the order they go. target >= 1.
void eliminate(const std::vector<Vec2>& pts, std::vector<std::int32_t>& active, std::size_t target,
               std::vector<std::int32_t>* order)
{
    const std::size_t n = active.size();
    if (n <= target || target == 0)
        return;

    const float rmax = maxRadius(target);
    const float reach = 2.0f * rmax;
    const float ratio = static_cast<float>(target) / static_cast<float>(n);
    const float dmin = rmax * (1.0f - ratio * std::sqrt(ratio)) * 0.65f;   // ratio ^ 1.5
    const auto weight = [&](float d) {
        d = std::max(d, dmin);
        const float t = 1.0f - d / reach;
        if (t <= 0.0f)
            return 0.0f;
        const float t2 = t * t;
        const float t4 = t2 * t2;
        return t4 * t4;                                                    // t ^ 8
    };

    // Every pair of points closer than "reach", through a grid that wraps around (or all pairs when
    // the set is tiny or the reach is too long for a grid of 3 x 3 cells).
    std::vector<std::vector<Neighbour>> neighbours(pts.size());
    const auto link = [&](std::int32_t a, std::int32_t b) {
        const float d = torusDistance(pts[static_cast<std::size_t>(a)], pts[static_cast<std::size_t>(b)]);
        if (d < reach)
        {
            const float w = weight(d);
            neighbours[static_cast<std::size_t>(a)].push_back({ b, w });
            neighbours[static_cast<std::size_t>(b)].push_back({ a, w });
        }
    };
    const int grid = reach > 0.0f ? static_cast<int>(std::min(4096.0f, std::floor(1.0f / reach))) : 0;
    if (n <= 64 || grid < 3)
    {
        for (std::size_t a = 0; a < n; ++a)
            for (std::size_t b = a + 1; b < n; ++b)
                link(active[a], active[b]);
    }
    else
    {
        const std::size_t g = static_cast<std::size_t>(grid);
        std::vector<std::vector<std::int32_t>> cells(g * g);
        const auto cellOf = [&](Vec2 p) {
            const int cx = std::clamp(static_cast<int>(p.x * static_cast<float>(grid)), 0, grid - 1);
            const int cy = std::clamp(static_cast<int>(p.y * static_cast<float>(grid)), 0, grid - 1);
            return std::pair<int, int>(cx, cy);
        };
        for (const std::int32_t i : active)
        {
            const auto c = cellOf(pts[static_cast<std::size_t>(i)]);
            cells[static_cast<std::size_t>(c.second) * g + static_cast<std::size_t>(c.first)].push_back(i);
        }
        for (const std::int32_t i : active)
        {
            const auto c = cellOf(pts[static_cast<std::size_t>(i)]);
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const std::size_t cx = static_cast<std::size_t>((c.first + dx + grid) % grid);
                    const std::size_t cy = static_cast<std::size_t>((c.second + dy + grid) % grid);
                    for (const std::int32_t j : cells[cy * g + cx])
                        if (j > i)
                            link(i, j);
                }
        }
    }

    std::vector<float> weights(pts.size(), 0.0f);
    for (const std::int32_t i : active)
        for (const Neighbour& nb : neighbours[static_cast<std::size_t>(i)])
            weights[static_cast<std::size_t>(i)] += nb.weight;

    MaxHeap heap(active, weights, pts.size());
    for (std::size_t remaining = n; remaining > target; --remaining)
    {
        const std::int32_t worst = heap.pop();
        for (const Neighbour& nb : neighbours[static_cast<std::size_t>(worst)])
        {
            if (heap.contains(nb.index))
            {
                weights[static_cast<std::size_t>(nb.index)] -= nb.weight;
                heap.lowered(nb.index);
            }
        }
        if (order)
            order->push_back(worst);
    }

    std::vector<std::int32_t> survivors;
    survivors.reserve(target);
    for (const std::int32_t i : active)
        if (heap.contains(i))
            survivors.push_back(i);
    active.swap(survivors);
}

// --- pattern files ---------------------------------------------------------------------------------

constexpr char kMagic[8] = { 'O', 'S', 'C', 'P', 'A', 'T', 'R', 'N' };
constexpr std::uint32_t kFileVersion = 1;
constexpr std::size_t kHeaderSize = 16;   // magic, version, count
constexpr std::size_t kFooterSize = 8;    // checksum

void putU32(std::vector<unsigned char>& out, std::uint32_t v)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<unsigned char>((v >> shift) & 0xFFu));
}

void putU64(std::vector<unsigned char>& out, std::uint64_t v)
{
    for (int shift = 0; shift < 64; shift += 8)
        out.push_back(static_cast<unsigned char>((v >> shift) & 0xFFu));
}

std::uint32_t getU32(const unsigned char* p)
{
    std::uint32_t v = 0;
    for (int i = 3; i >= 0; --i)
        v = (v << 8) | p[i];
    return v;
}

std::uint64_t getU64(const unsigned char* p)
{
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i)
        v = (v << 8) | p[i];
    return v;
}

bool fail(std::string* error, const char* message)
{
    if (error)
        *error = message;
    return false;
}

} // namespace

Pattern makeProgressiveBlueNoise(std::size_t count, std::uint64_t seed, std::size_t oversample)
{
    Pattern pattern;
    if (count == 0 || count > kMaxPatternPoints)
        return pattern;
    oversample = std::clamp<std::size_t>(oversample, 1, 64);

    const std::size_t m = count * oversample;
    std::vector<Vec2> candidates(m);
    for (std::size_t i = 0; i < m; ++i)
    {
        const std::uint64_t h = hashCombine(seed, i);
        candidates[i] = { unitFloat(h), unitFloat(mix64(h)) };
    }
    std::vector<std::int32_t> active(m);
    for (std::size_t i = 0; i < m; ++i)
        active[i] = static_cast<std::int32_t>(i);

    eliminate(candidates, active, count, nullptr);   // the pattern itself

    // Then halve it again and again, remembering the order in which the points go: the last point
    // removed is the one every prefix needs most, so it gets rank 0.
    std::vector<std::int32_t> removed;
    removed.reserve(count);
    for (std::size_t current = count; current > 1;)
    {
        const std::size_t next = current / 2;
        eliminate(candidates, active, next, &removed);
        current = next;
    }
    for (const std::int32_t i : active)
        removed.push_back(i);

    pattern.points.reserve(count);
    for (auto it = removed.rbegin(); it != removed.rend(); ++it)
        pattern.points.push_back(candidates[static_cast<std::size_t>(*it)]);
    pattern.id = hashPattern(pattern);
    return pattern;
}

std::uint64_t hashPattern(const Pattern& pattern)
{
    std::uint64_t h = hashCombine(0x4F53435041545452ull, pattern.points.size());
    for (const Vec2& p : pattern.points)
    {
        const std::uint64_t x = std::bit_cast<std::uint32_t>(p.x);
        const std::uint64_t y = std::bit_cast<std::uint32_t>(p.y);
        h = hashCombine(h, (x << 32) | y);
    }
    return h;
}

bool isValid(const Pattern& pattern)
{
    for (const Vec2& p : pattern.points)
        if (!(p.x >= 0.0f && p.x < 1.0f && p.y >= 0.0f && p.y < 1.0f))
            return false;
    return true;
}

float prefixSpacing(const Pattern& pattern, std::size_t count)
{
    count = std::min(count, pattern.points.size());
    if (count < 2)
        return 1.0f;
    float closest = 2.0f;
    for (std::size_t a = 0; a < count; ++a)
        for (std::size_t b = a + 1; b < count; ++b)
            closest = std::min(closest, torusDistance(pattern.points[a], pattern.points[b]));
    return closest / (2.0f * maxRadius(count));
}

bool savePattern(const Pattern& pattern, const std::filesystem::path& path, std::string* error)
{
    if (pattern.points.empty())
        return fail(error, "the pattern has no points");
    if (pattern.points.size() > kMaxPatternPoints)
        return fail(error, "the pattern has too many points");
    if (!isValid(pattern))
        return fail(error, "the pattern has points outside [0, 1)");

    std::vector<unsigned char> bytes;
    bytes.reserve(kHeaderSize + pattern.points.size() * 8 + kFooterSize);
    bytes.insert(bytes.end(), kMagic, kMagic + sizeof(kMagic));
    putU32(bytes, kFileVersion);
    putU32(bytes, static_cast<std::uint32_t>(pattern.points.size()));
    for (const Vec2& p : pattern.points)
    {
        putU32(bytes, std::bit_cast<std::uint32_t>(p.x));
        putU32(bytes, std::bit_cast<std::uint32_t>(p.y));
    }
    putU64(bytes, hashPattern(pattern));

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        return fail(error, "the file could not be created");
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    file.close();
    if (!file)
        return fail(error, "the file could not be written");
    return true;
}

bool loadPattern(const std::filesystem::path& path, Pattern& pattern, std::string* error)
{
    pattern = Pattern{};

    std::ifstream file(path, std::ios::binary);
    if (!file)
        return fail(error, "the file could not be opened");
    unsigned char header[kHeaderSize];
    file.read(reinterpret_cast<char*>(header), static_cast<std::streamsize>(kHeaderSize));
    if (file.gcount() != static_cast<std::streamsize>(kHeaderSize))
        return fail(error, "the file is too short to be a pattern");
    if (!std::equal(kMagic, kMagic + sizeof(kMagic), header))
        return fail(error, "the file is not an OpenScatter pattern");
    if (getU32(header + 8) != kFileVersion)
        return fail(error, "the pattern file has a version this build does not read");
    const std::size_t count = getU32(header + 12);
    if (count == 0 || count > kMaxPatternPoints)
        return fail(error, "the pattern file declares an impossible number of points");

    // Read exactly what the header promises, and one byte more to notice a file that is too long.
    std::vector<unsigned char> body(count * 8 + kFooterSize + 1);
    file.read(reinterpret_cast<char*>(body.data()), static_cast<std::streamsize>(body.size()));
    if (static_cast<std::size_t>(file.gcount()) != body.size() - 1)
        return fail(error, "the pattern file does not have the size its header declares");

    Pattern loaded;
    loaded.points.resize(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        loaded.points[i].x = std::bit_cast<float>(getU32(body.data() + i * 8));
        loaded.points[i].y = std::bit_cast<float>(getU32(body.data() + i * 8 + 4));
    }
    if (!isValid(loaded))
        return fail(error, "the pattern file has points outside [0, 1)");
    loaded.id = hashPattern(loaded);
    if (loaded.id != getU64(body.data() + count * 8))
        return fail(error, "the pattern file is damaged (checksum mismatch)");

    pattern = std::move(loaded);
    return true;
}

} // namespace osc
