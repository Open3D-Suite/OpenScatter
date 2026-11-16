// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tests of the OpenScatter core: projection onto a surface.

#include <doctest/doctest.h>

#include <cmath>
#include <limits>

#include "helpers.h"
#include "terrain.h"

namespace
{

// The same question answered the slow way: every triangle, no index.
bool projectBruteForce(const osc::TriMesh& mesh, float x, float y, double& bestZ)
{
    bool found = false;
    for (std::size_t t = 0; t < mesh.triangleCount(); ++t)
    {
        const std::uint32_t i0 = mesh.indices[t * 3], i1 = mesh.indices[t * 3 + 1], i2 = mesh.indices[t * 3 + 2];
        if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size())
            continue;
        const osc::Vec3 a = mesh.vertices[i0], b = mesh.vertices[i1], c = mesh.vertices[i2];
        if (!osc::isFinite(a) || !osc::isFinite(b) || !osc::isFinite(c))
            continue;
        const double e1x = double(b.x) - a.x, e1y = double(b.y) - a.y;
        const double e2x = double(c.x) - a.x, e2y = double(c.y) - a.y;
        const double det = e1x * e2y - e2x * e1y;
        if (det == 0.0)
            continue;
        const double u = ((double(x) - a.x) * e2y - e2x * (double(y) - a.y)) / det;
        const double v = (e1x * (double(y) - a.y) - (double(x) - a.x) * e1y) / det;
        if (u < -1e-9 || v < -1e-9 || u + v > 1.0 + 1e-9)
            continue;
        const double z = a.z + u * (double(b.z) - a.z) + v * (double(c.z) - a.z);
        if (!found || z > bestZ)
        {
            found = true;
            bestZ = z;
        }
    }
    return found;
}

} // namespace

TEST_CASE("surface: an empty mesh has nothing to hit")
{
    osc::SurfaceIndex index;
    osc::SurfaceHit hit;
    CHECK(index.empty());
    CHECK(index.bounds().empty());
    CHECK_FALSE(index.project(0.0f, 0.0f, hit));

    index.build(osc::TriMesh{});
    CHECK(index.empty());
    CHECK_FALSE(index.project(0.0f, 0.0f, hit));
    CHECK_FALSE(index.hasUVs());
}

TEST_CASE("surface: a flat rectangle")
{
    const osc::SurfaceIndex index(helpers::quad(-10.0f, -5.0f, 10.0f, 5.0f, 3.0f));
    CHECK(index.usableTriangles() == 2);
    CHECK(index.bounds().minX == -10.0f);
    CHECK(index.bounds().maxY == 5.0f);
    CHECK(index.minZ() == 3.0f);
    CHECK(index.maxZ() == 3.0f);

    osc::SurfaceHit hit;
    REQUIRE(index.project(2.0f, 1.0f, hit));
    CHECK(hit.z == 3.0f);
    CHECK(hit.normal.x == 0.0f);
    CHECK(hit.normal.y == 0.0f);
    CHECK(hit.normal.z == 1.0f);

    // The corners and the edges belong to it; anything beyond does not.
    CHECK(index.project(-10.0f, -5.0f, hit));
    CHECK(index.project(10.0f, 5.0f, hit));
    CHECK(index.project(0.0f, 5.0f, hit));
    CHECK_FALSE(index.project(10.001f, 0.0f, hit));
    CHECK_FALSE(index.project(0.0f, -5.001f, hit));
    CHECK_FALSE(index.project(std::numeric_limits<float>::quiet_NaN(), 0.0f, hit));
    CHECK_FALSE(index.project(0.0f, std::numeric_limits<float>::infinity(), hit));
}

TEST_CASE("surface: height and normal on a tilted plane")
{
    // z = 5 + 0.5 x - 0.25 y
    const osc::SurfaceIndex index(helpers::plane(0.0f, 0.0f, 100.0f, 100.0f, 5.0f, 0.5f, -0.25f));
    osc::SurfaceHit hit;
    REQUIRE(index.project(40.0f, 20.0f, hit));
    CHECK(hit.z == doctest::Approx(5.0f + 20.0f - 5.0f));
    const float len = std::sqrt(0.5f * 0.5f + 0.25f * 0.25f + 1.0f);
    CHECK(hit.normal.x == doctest::Approx(-0.5f / len));
    CHECK(hit.normal.y == doctest::Approx(0.25f / len));
    CHECK(hit.normal.z == doctest::Approx(1.0f / len));
    CHECK(osc::length(hit.normal) == doctest::Approx(1.0f));
}

TEST_CASE("surface: the normal faces up whatever the winding")
{
    osc::TriMesh mesh = helpers::quad(0.0f, 0.0f, 10.0f, 10.0f, 1.0f);
    mesh.indices = { 0, 2, 1, 0, 3, 2 };   // facing down
    const osc::SurfaceIndex index(mesh);
    osc::SurfaceHit hit;
    REQUIRE(index.project(3.0f, 4.0f, hit));
    CHECK(hit.z == 1.0f);
    CHECK(hit.normal.z == 1.0f);
}

TEST_CASE("surface: with several layers the highest one wins")
{
    osc::TriMesh mesh = helpers::quad(0.0f, 0.0f, 10.0f, 10.0f, 1.0f);
    const osc::TriMesh roof = helpers::quad(2.0f, 2.0f, 6.0f, 6.0f, 8.0f);
    for (const std::uint32_t i : roof.indices)
        mesh.indices.push_back(i + 4);
    mesh.vertices.insert(mesh.vertices.end(), roof.vertices.begin(), roof.vertices.end());

    const osc::SurfaceIndex index(mesh);
    osc::SurfaceHit hit;
    REQUIRE(index.project(4.0f, 4.0f, hit));
    CHECK(hit.z == 8.0f);
    CHECK(hit.triangle >= 2);
    REQUIRE(index.project(8.0f, 8.0f, hit));
    CHECK(hit.z == 1.0f);
    CHECK(hit.triangle < 2);
}

TEST_CASE("surface: triangles that cannot be used are left out")
{
    osc::TriMesh mesh = helpers::quad(0.0f, 0.0f, 10.0f, 10.0f, 2.0f);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    mesh.vertices.push_back({ 0.0f, 0.0f, 50.0f });   // 4: the foot of a wall
    mesh.vertices.push_back({ 10.0f, 0.0f, 50.0f });  // 5
    mesh.vertices.push_back({ 10.0f, 0.0f, 90.0f });  // 6: straight above 5
    mesh.vertices.push_back({ nan, 5.0f, 99.0f });    // 7
    mesh.vertices.push_back({ 5.0f, 5.0f, std::numeric_limits<float>::infinity() });   // 8
    const std::uint32_t extra[] = {
        4, 5, 6,          // a vertical wall: a line seen from above
        0, 1, 999,        // an index out of range
        0, 1, 7,          // a vertex that is not a number
        0, 2, 8,          // a vertex at infinity
        3, 3, 3,          // a point
        0, 2, 0,          // a line
    };
    mesh.indices.insert(mesh.indices.end(), std::begin(extra), std::end(extra));
    mesh.indices.push_back(1);   // and a stray index that does not make a triangle

    const osc::SurfaceIndex index(mesh);
    CHECK(index.usableTriangles() == 2);
    CHECK(index.maxZ() == 2.0f);
    osc::SurfaceHit hit;
    REQUIRE(index.project(5.0f, 0.0f, hit));
    CHECK(hit.z == 2.0f);
    REQUIRE(index.project(5.0f, 5.0f, hit));
    CHECK(hit.z == 2.0f);

    // Nothing but unusable triangles: an empty index.
    osc::TriMesh wall;
    wall.vertices = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 1.0f } };
    wall.indices = { 0, 1, 2 };
    const osc::SurfaceIndex nothing(wall);
    CHECK(nothing.empty());
    CHECK_FALSE(nothing.project(0.5f, 0.0f, hit));
}

TEST_CASE("surface: the hit knows its triangle, its corner weights and its texture coordinates")
{
    osc::TriMesh mesh = osc::tools::makeTerrain(100.0f, 16, 20.0f, 4, true);
    const osc::TriMesh copy = mesh;
    const osc::SurfaceIndex index(std::move(mesh));
    REQUIRE(index.hasUVs());

    for (std::uint64_t i = 0; i < 500; ++i)
    {
        const float x = osc::unitFloat(osc::hashCombine(1, i)) * 100.0f;
        const float y = osc::unitFloat(osc::hashCombine(2, i)) * 100.0f;
        osc::SurfaceHit hit;
        REQUIRE(index.project(x, y, hit));
        REQUIRE(hit.triangle < copy.triangleCount());

        // The corner weights put the hit back where it was asked for.
        const osc::Vec3 a = copy.vertices[copy.indices[hit.triangle * 3]];
        const osc::Vec3 b = copy.vertices[copy.indices[hit.triangle * 3 + 1]];
        const osc::Vec3 c = copy.vertices[copy.indices[hit.triangle * 3 + 2]];
        const float b0 = 1.0f - hit.b1 - hit.b2;
        CHECK(a.x * b0 + b.x * hit.b1 + c.x * hit.b2 == doctest::Approx(x).epsilon(1e-4));
        CHECK(a.y * b0 + b.y * hit.b1 + c.y * hit.b2 == doctest::Approx(y).epsilon(1e-4));
        CHECK(a.z * b0 + b.z * hit.b1 + c.z * hit.b2 == doctest::Approx(hit.z).epsilon(1e-4));
        CHECK(hit.b1 >= -1e-5f);
        CHECK(hit.b2 >= -1e-5f);
        CHECK(b0 >= -1e-5f);

        // This terrain's texture coordinates are its position divided by its size.
        const osc::Vec2 uv = index.uvAt(hit);
        CHECK(uv.x == doctest::Approx(x / 100.0f).epsilon(1e-4));
        CHECK(uv.y == doctest::Approx(y / 100.0f).epsilon(1e-4));
    }

    // Without texture coordinates, or with the wrong number of them: (0, 0).
    osc::TriMesh bare = copy;
    bare.cornerUVs.pop_back();
    const osc::SurfaceIndex noUVs(std::move(bare));
    CHECK_FALSE(noUVs.hasUVs());
    osc::SurfaceHit hit;
    REQUIRE(noUVs.project(50.0f, 50.0f, hit));
    CHECK(noUVs.uvAt(hit).x == 0.0f);
    CHECK(noUVs.uvAt(hit).y == 0.0f);
}

TEST_CASE("surface: the index agrees with checking every triangle")
{
    const osc::TriMesh mesh = osc::tools::makeTerrain(250.0f, 60, 40.0f, 17);
    const osc::SurfaceIndex index(mesh);
    CHECK(index.usableTriangles() == 60 * 60 * 2);

    int hits = 0;
    for (std::uint64_t i = 0; i < 3000; ++i)
    {
        // Some of the points fall outside the terrain.
        const float x = osc::unitFloat(osc::hashCombine(31, i)) * 280.0f - 15.0f;
        const float y = osc::unitFloat(osc::hashCombine(32, i)) * 280.0f - 15.0f;
        osc::SurfaceHit hit;
        double expected = 0.0;
        const bool found = index.project(x, y, hit);
        REQUIRE(found == projectBruteForce(mesh, x, y, expected));
        if (found)
        {
            ++hits;
            CHECK(hit.z == static_cast<float>(expected));
            CHECK(osc::length(hit.normal) == doctest::Approx(1.0f));
            CHECK(hit.normal.z > 0.0f);
        }
    }
    CHECK(hits > 2000);
}

TEST_CASE("surface: no holes along the edges and at the vertices of the mesh")
{
    const int resolution = 24;
    const float size = 60.0f;
    const osc::SurfaceIndex index(osc::tools::makeTerrain(size, resolution, 10.0f, 5));
    const float step = size / static_cast<float>(resolution);
    osc::SurfaceHit hit;
    for (int y = 0; y <= resolution; ++y)
        for (int x = 0; x <= resolution; ++x)
        {
            const float vx = size * static_cast<float>(x) / static_cast<float>(resolution);
            const float vy = size * static_cast<float>(y) / static_cast<float>(resolution);
            CHECK(index.project(vx, vy, hit));                                   // a vertex
            if (x < resolution)
                CHECK(index.project(vx + step * 0.5f, vy, hit));                 // the middle of an edge
            if (y < resolution)
                CHECK(index.project(vx, vy + step * 0.5f, hit));
            if (x < resolution && y < resolution)
                CHECK(index.project(vx + step * 0.5f, vy + step * 0.5f, hit));   // on the diagonal
        }
}

TEST_CASE("surface: a mesh of huge overlapping triangles still gets an index that works")
{
    // Every triangle covers almost everything: binning them finely would take a reference per cell
    // and per triangle. The index has to notice and settle for a coarse grid.
    osc::TriMesh mesh;
    const int count = 3000;
    for (int i = 0; i < count; ++i)
    {
        const float z = static_cast<float>(i);
        const float jitter = static_cast<float>(i % 7) * 0.01f;
        const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back({ -1000.0f - jitter, -1000.0f, z });
        mesh.vertices.push_back({ 1000.0f + jitter, -1000.0f, z });
        mesh.vertices.push_back({ 0.0f, 1000.0f + jitter, z });
        mesh.indices.insert(mesh.indices.end(), { base, base + 1, base + 2 });
    }
    const osc::SurfaceIndex index(mesh);
    CHECK(index.usableTriangles() == static_cast<std::size_t>(count));
    osc::SurfaceHit hit;
    REQUIRE(index.project(0.0f, 0.0f, hit));
    CHECK(hit.z == static_cast<float>(count - 1));
    CHECK(hit.triangle == static_cast<std::uint32_t>(count - 1));
    CHECK_FALSE(index.project(-999.0f, 999.0f, hit));
}

TEST_CASE("surface: extreme and tiny meshes do not break the index")
{
    osc::SurfaceHit hit;

    // Coordinates near the limit of a float.
    const float big = 1e37f;
    const osc::SurfaceIndex huge(helpers::quad(-big, -big, big, big, 0.0f));
    CHECK(huge.usableTriangles() == 2);
    CHECK(huge.project(0.0f, 0.0f, hit));
    CHECK(huge.project(big * 0.5f, -big * 0.5f, hit));

    // A mesh a millionth of a unit across, far from the origin.
    const osc::SurfaceIndex tiny(helpers::quad(1000.0f, 1000.0f, 1000.001f, 1000.001f, 4.0f));
    CHECK(tiny.usableTriangles() == 2);
    REQUIRE(tiny.project(1000.0005f, 1000.0005f, hit));
    CHECK(hit.z == 4.0f);

    // A long thin strip: the grid follows its shape.
    osc::TriMesh strip;
    for (int i = 0; i <= 2000; ++i)
    {
        strip.vertices.push_back({ static_cast<float>(i), 0.0f, 0.0f });
        strip.vertices.push_back({ static_cast<float>(i), 0.5f, 0.0f });
    }
    for (std::uint32_t i = 0; i < 2000; ++i)
        strip.indices.insert(strip.indices.end(), { i * 2, i * 2 + 2, i * 2 + 3, i * 2, i * 2 + 3, i * 2 + 1 });
    const osc::SurfaceIndex road(strip);
    CHECK(road.usableTriangles() == 4000);
    for (int i = 0; i < 2000; i += 37)
        CHECK(road.project(static_cast<float>(i) + 0.3f, 0.25f, hit));
    CHECK_FALSE(road.project(100.0f, 0.6f, hit));
}
