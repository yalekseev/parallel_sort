#ifndef PARALLEL_SORT
#define PARALLEL_SORT

#include <algorithm>
#include <iostream>
#include <iterator>
#include <utility>
#include <thread>
#include <vector>
#include <atomic>
#include <stack>
#include <mutex>
#include <map>

namespace internal {

template <typename T>
class SynchronizedStack {
public:
    void push(const T & val) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stack.push(val);
    }

    bool pop(T & val) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stack.empty()) {
            return false;
        }

        val = m_stack.top();
        m_stack.pop();
        return true;
    }

private:
    std::stack<T> m_stack;
    mutable std::mutex m_mutex;
};

template <typename Iterator>
class ParallelSorter {
public:
    ParallelSorter() = default;
    ParallelSorter(const ParallelSorter & other) = delete;

    void sort(Iterator begin, Iterator end) {
        m_synchronized_stack.push(std::make_pair(begin, end));

        size_t num_spawn_threads = std::max(std::thread::hardware_concurrency(), 2U);

        std::vector<std::thread> threads(num_spawn_threads);
        for (size_t i = 0; i < num_spawn_threads; ++i) {
            threads[i] = std::thread(&ParallelSorter<Iterator>::do_sort, this);
        }

        do_sort();

        for (auto & th : threads) {
            th.join();
        }
    }

private:
    bool done() {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto & thread_done : m_thread_done) {
            if (!thread_done.second) {
                return false;
            }
        }

        return true;
    }

    void do_sort() {
        while (true) {
            if (process_chunk()) {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_thread_done[std::this_thread::get_id()] = false;
            } else {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_thread_done[std::this_thread::get_id()] = true;
            }

            if (done()) {
                break;
            }
        }
    }

    bool process_chunk() {
        std::pair<Iterator, Iterator> range;
        if (!m_synchronized_stack.pop(range)) {
            return false;
        }

        std::size_t size = std::distance(range.first, range.second);
        if (size < 10) {
            std::sort(range.first, range.second);
            return true;
        }

        Iterator mid = range.first + size / 2;

        std::nth_element(range.first, mid, range.second);

        m_synchronized_stack.push(std::make_pair(range.first, mid));
        m_synchronized_stack.push(std::make_pair(mid + 1, range.second));

        return true;
    }

    SynchronizedStack<std::pair<Iterator, Iterator> > m_synchronized_stack;

    mutable std::mutex m_mutex;
    std::map<std::thread::id, bool> m_thread_done;
};

} // namespace internal

template <typename Iterator>
void parallel_sort(Iterator begin, Iterator end) {
    internal::ParallelSorter<Iterator> sorter;
    sorter.sort(begin, end);
}

#endif
