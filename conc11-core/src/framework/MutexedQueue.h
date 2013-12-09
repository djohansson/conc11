#pragma once

#include <mutex>
#include <list>

template<typename T>
class MutexedQueue
{
public:

    bool empty(void)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }

    bool push(T item)
    {
        std::list<T> tmp;
        tmp.emplace_back(std::move(item));
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.splice(std::end(m_queue), tmp);
        }

        return true;
    }

    bool try_pop(T& ret)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_queue.empty())
        {
            ret = m_queue.front();
            m_queue.pop_front();

            return true;
        }

        return false;
    }

private:

    std::mutex m_mutex;
    std::list<T> m_queue;
};
