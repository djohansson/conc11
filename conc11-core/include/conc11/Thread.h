#pragma once

#include <thread>

namespace conc11
{

class Thread
{
public:

    typedef void (std::thread::*DestructAction)();

    Thread(std::thread&& t, DestructAction a)
        : m_thread(std::move(t))
        , m_destructAction(a)
    {
    }

    Thread(Thread&& t)
        : m_thread(std::move(t.m_thread))
        , m_destructAction(std::move(t.m_destructAction))
    {
        t.m_destructAction = nullptr;
    }

    ~Thread()
    {
        if (m_thread.joinable())
            (m_thread.*m_destructAction)();
    }

    std::thread& get()
    {
        return m_thread;
    }

    const std::thread& get() const
    {
        return m_thread;
    }

private:

    std::thread m_thread;
    DestructAction m_destructAction;
};

} // namespace conc11
