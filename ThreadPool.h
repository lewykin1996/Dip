#pragma once

#include <iostream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <stdexcept>

class ThreadPool
{
public:
    explicit ThreadPool(size_t numberOfThreads)
        : stop(false)
    {
        if (numberOfThreads == 0)
        {
            throw std::invalid_argument(
                "Number of threads must be greater than zero."
            );
        }

        for (size_t i = 0; i < numberOfThreads; ++i)
        {
            workers.emplace_back([this]()
                {
                    while (true)
                    {
                        std::function<void()> task;

                        {
                            std::unique_lock<std::mutex> lock(queueMutex);

                            condition.wait(lock, [this]()
                                {
                                    return stop || !tasks.empty();
                                });

                            if (stop && tasks.empty())
                            {
                                return;
                            }

                            task = std::move(tasks.front());
                            tasks.pop();
                        }

                        task();
                    }
                });
        }
    }

    template <class F, class... Args>
    auto enqueue(F&& function, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using ReturnType = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<
            std::packaged_task<ReturnType()>
        >(
            std::bind(
                std::forward<F>(function),
                std::forward<Args>(args)...
            )
        );

        std::future<ReturnType> result = task->get_future();

        {
            std::unique_lock<std::mutex> lock(queueMutex);

            if (stop)
            {
                throw std::runtime_error(
                    "Cannot add task to stopped ThreadPool."
                );
            }

            tasks.emplace([task]()
                {
                    (*task)();
                });
        }

        condition.notify_one();

        return result;
    }

    ~ThreadPool()
    {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            stop = true;
        }

        condition.notify_all();

        for (std::thread& worker : workers)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    std::mutex queueMutex;
    std::condition_variable condition;

    bool stop;
};