#pragma once

#include <deque>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <vector>

namespace Zeri::Core {

    template <typename T>
    class ThreadSafeQueue {
    public:
        void Push(T item) {
            {
                std::lock_guard lock(m_mutex);
                m_queue.push_back(std::move(item));
            }
            m_cv.notify_one();
        }

        [[nodiscard]] std::vector<T> DrainAll() {
            std::lock_guard lock(m_mutex);
            std::vector<T> result(
                std::make_move_iterator(m_queue.begin()),
                std::make_move_iterator(m_queue.end())
            );
            m_queue.clear();
            return result;
        }

        [[nodiscard]] std::optional<T> WaitAndPop() {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [this] { return !m_queue.empty() || m_shutdown; });
            if (m_queue.empty()) return std::nullopt;
            T item = std::move(m_queue.front());
            m_queue.pop_front();
            return item;
        }

        void Shutdown() {
            {
                std::lock_guard lock(m_mutex);
                m_shutdown = true;
            }
            m_cv.notify_all();
        }

        [[nodiscard]] bool Empty() const {
            std::lock_guard lock(m_mutex);
            return m_queue.empty();
        }

    private:
        std::deque<T> m_queue;
        mutable std::mutex m_mutex;
        std::condition_variable m_cv;
        bool m_shutdown = false;
    };

}

/*
Generic thread-safe FIFO queue implementing the producer-consumer pattern.
Multiple threads can Push items concurrently; a consumer thread can
DrainAll pending items at once (batched, ideal for the UI render loop)
or WaitAndPop for blocking semantics.
Shutdown unblocks all waiting consumers so threads can terminate cleanly.
*/
