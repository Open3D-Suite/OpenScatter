// SPDX-License-Identifier: GPL-3.0-or-later
//
// OpenScatter core - running independent pieces of work on several threads.
//
// The scatter is split into pieces that do not depend on one another (one per pattern tile), each
// writing to its own place, and the pieces are put together in a fixed order afterwards. Which thread
// computes which piece, and when, therefore cannot change the result.

#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace osc
{

inline unsigned hardwareThreads() { return std::max(1u, std::thread::hardware_concurrency()); }

// Calls fn(index, worker) once for every index in [0, count), on up to "threads" threads, the calling
// one included. "worker" is below "threads" and identifies the thread making the call. If a call throws,
// the remaining work is abandoned and the first exception is thrown again here.
template <class Fn>
void parallelFor(std::size_t count, unsigned threads, Fn&& fn)
{
    if (count == 0)
        return;
    threads = static_cast<unsigned>(std::min<std::size_t>(std::max(1u, threads), count));

    std::atomic<std::size_t> next{ 0 };
    std::atomic<bool> failed{ false };
    std::exception_ptr error;
    std::mutex errorMutex;

    const auto work = [&](unsigned worker) {
        try
        {
            while (!failed.load(std::memory_order_relaxed))
            {
                const std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
                if (index >= count)
                    return;
                fn(index, worker);
            }
        }
        catch (...)
        {
            const std::lock_guard<std::mutex> lock(errorMutex);
            if (!error)
                error = std::current_exception();
            failed.store(true, std::memory_order_relaxed);
        }
    };

    std::vector<std::thread> pool;
    try
    {
        pool.reserve(threads - 1);
        for (unsigned worker = 1; worker < threads; ++worker)
            pool.emplace_back(work, worker);
    }
    catch (...)
    {
        // No more threads to be had: the ones that did start, and this one, do all the work.
    }
    work(0);
    for (std::thread& thread : pool)
        thread.join();
    if (error)
        std::rethrow_exception(error);
}

} // namespace osc
