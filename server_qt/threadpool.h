#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// 固定大小工作线程池，用于执行业务逻辑任务
class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads)
    {
        if (numThreads == 0)
            numThreads = 1;
        for (size_t i = 0; i < numThreads; ++i) {
            m_workers.emplace_back([this]() { workerLoop(); });
        }
    }

    ~ThreadPool()
    {
        shutdown();
    }

    void submit(std::function<void()> task)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopped)
                return;
            m_tasks.push(std::move(task));
        }
        m_cv.notify_one();
    }

    void shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopped)
                return;
            m_stopped = true;
        }
        m_cv.notify_all();
        for (std::thread &t : m_workers) {
            if (t.joinable())
                t.join();
        }
        m_workers.clear();
    }

    size_t size() const { return m_workers.size(); }

private:
    void workerLoop()
    {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this]() { return m_stopped || !m_tasks.empty(); });
                if (m_stopped && m_tasks.empty())
                    return;
                task = std::move(m_tasks.front());
                m_tasks.pop();
            }
            if (task)
                task();
        }
    }

    std::vector<std::thread> m_workers;
    std::queue<std::function<void()>> m_tasks;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stopped = false;
};

#endif // THREADPOOL_H
