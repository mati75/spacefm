/**
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <thread>
#include <atomic>

#include <memory>

#include <sigc++/sigc++.h>

#include <ztd/ztd.hxx>
#include <ztd/ztd_logger.hxx>

#include "signals.hxx"

struct VFSDir;

namespace vfs
{
    struct async_thread;
    using async_thread_t = std::shared_ptr<async_thread>;
    using async_thread_function_t = void* (*)(async_thread*, void*);

    using dir = ztd::raw_ptr<VFSDir>;

    struct async_thread
    {
        async_thread(async_thread_function_t task_function, void* user_data);
        ~async_thread();

        void run();
        void cancel();

        void* user_data() const;

        bool is_running() const;
        bool is_finished() const;
        bool is_canceled() const;

        void cleanup(bool finalize);

      private:
        async_thread_function_t task_function_;
        void* user_data_;

        std::jthread thread_;
        // std::mutex mutex_{};

        std::atomic<bool> running_{false};
        std::atomic<bool> finished_{false};
        std::atomic<bool> cancel_{false};
        std::atomic<bool> canceled_{false};

        // Signals
      public:
        // Signals function types
        using evt_task_finished_load_dir_t = void(vfs::dir, bool);

        // Signals Add Event
        template<spacefm::signal evt>
        typename std::enable_if<evt == spacefm::signal::task_finish, sigc::connection>::type
        add_event(evt_task_finished_load_dir_t fun, vfs::dir dir)
        {
            // ztd::logger::trace("Signal Connect   : spacefm::signal::task_finish");
            this->evt_data_load_dir = dir;
            return this->evt_task_finished_load_dir.connect(sigc::ptr_fun(fun));
        }

        // Signals Run Event
        template<spacefm::signal evt>
        typename std::enable_if<evt == spacefm::signal::task_finish, void>::type
        run_event(bool is_cancelled)
        {
            // ztd::logger::trace("Signal Execute   : spacefm::signal::task_finish");
            this->evt_task_finished_load_dir.emit(this->evt_data_load_dir, is_cancelled);
        }

        // Signals
      private:
        // Signal types
        sigc::signal<evt_task_finished_load_dir_t> evt_task_finished_load_dir;

      private:
        // Signal data
        vfs::dir evt_data_load_dir{nullptr};
    };
} // namespace vfs

vfs::async_thread_t vfs_async_thread_new(vfs::async_thread_function_t task_func, void* user_data);
