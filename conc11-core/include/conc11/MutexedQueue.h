#pragma once

#include <mutex>
#include <list>

namespace conc11
{

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
        tmp.push_back(std::move(item));
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.splice(std::end(m_queue), tmp);
        }

        return true;
    }

    bool pop(T& ret)
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

} // namespace conc11
