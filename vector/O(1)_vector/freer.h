#pragma once
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>

template <typename T>

class background_freer
{
public:
    static background_freer &instance()
    {
        static background_freer freer;
        return freer;
    }

    ~background_freer()
    {
        {
            std::lock_guard lock{m_mutex};
            m_stop = true;
        }
        m_cv.notify_one();
        m_worker.join();
    }

    background_freer(const background_freer &) = delete;
    background_freer &operator=(const background_freer &) = delete;
    void free_later(T *ptr)
    {
        {
            std::lock_guard lock{m_mutex};
            m_queue.push_back(ptr);
        }
        m_cv.notify_one();
    }

private:
    background_freer() : m_worker{[this]
                                  { run(); }}
    {
    }

    void run()
    {
        std::vector<T *> batch;
        batch.reserve(64);

        while (true)
        {
            {
                std::unique_lock lock{m_mutex};
                m_cv.wait(lock, [this]
                          { return m_stop || !m_queue.empty(); });

                if (m_queue.empty())
                {
                    return;
                }
                batch.swap(m_queue);
            }

            for (T *p : batch)
            {
                delete[] p;
            }
            batch.clear();
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<T *> m_queue;
    bool m_stop{false};
    std::thread m_worker;
};
