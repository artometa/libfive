/*
libfive: a CAD kernel for modeling with implicit functions
Copyright (C) 2018  Matt Keeter

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <iostream>

#include "libfive/render/brep/progress.hpp"

namespace Kernel {

bool EMPTY_PROGRESS_CALLBACK(float) { return true; }

ProgressWatcher* ProgressWatcher::build(uint32_t total, float offset,
                                        std::function<bool(float)> callback,
                                        std::atomic_bool& done,
                                        std::atomic_bool& cancel)
{
    auto progress_cb_ptr = callback.target<bool(*)(float)>();
    const bool has_progress_callback = !progress_cb_ptr ||
        (*progress_cb_ptr != EMPTY_PROGRESS_CALLBACK);

    if (has_progress_callback)
    {
        return new ProgressWatcher(total, offset, callback, done, cancel);
    }
    else
    {
        return nullptr;
    }
}

ProgressWatcher::ProgressWatcher(uint32_t total, float offset,
                                 std::function<bool(float)> callback,
                                 std::atomic_bool& done,
                                 std::atomic_bool& cancel)
    : callback(callback), done(done), cancel(cancel),
      counter(0), total(total), offset(offset)
{
    if (!counter.is_lock_free())
    {
        std::cerr << "ProgressWatcher: counter is not lock-free" << std::endl;
    }
    mut.lock();

    future = std::async(std::launch::async,
        [this]()
        {
            this->callback(0.0f);

            uint32_t n = 0;
            while (!this->done.load() && !this->cancel.load())
            {
                const uint32_t next = this->counter.load();
                if (next != n)
                {
                    n = next;
                    this->callback(n / (float)this->total + this->offset);
                }
                // Sleep for 50 ms, returning early if the mutex is
                // unlocked (which happens in the destructor)
                this->mut.try_lock_for(std::chrono::milliseconds(50));
            }

            // Once evaluation is finished, report that we're completely done
            if (!this->cancel.load())
            {
                this->callback(1.0f + this->offset);
            }
        });
}

ProgressWatcher::~ProgressWatcher()
{
    assert(done.load() || cancel.load());

    // Speed up the thread's exit condition by releasing the timex mutex
    mut.unlock();
    // Then wait for the thread to finish
    future.wait();
}

void ProgressWatcher::tick(uint32_t i)
{
    counter += i;
}

}   // namespace Kernel