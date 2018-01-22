#ifndef MYNETLIB_THREAD_POOL_H_
#define MYNETLIB_THREAD_POOL_H_

#include <atomic>
#include <type_traits>
#include <vector>
#include <queue>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <utility>
#include <memory>
#include <stdexcept>

class ThreadPool {
private:
    typedef std::function<void()> task_type;
public:
    explicit ThreadPool(int n = 0);
    ~ThreadPool() {
        stop_.store(true);
        cond_.notify_all(); // 唤醒所有线程
        for (std::thread& thread : threads_) {
            if (thread.joinable())
                thread.join(); // 等待任务结束
        }
    }

    template<typename Function, typename... Args>
    std::future<typename std::result_of<Function(Args...)>::type>
    submit(Function&&, Args&&...);
private:
    std::atomic<bool> stop_; // 是否关闭提交
    std::mutex mtx_; // 互斥器
    std::condition_variable cond_; // 条件变量
    std::queue<task_type> tasks_; // 任务队列
    std::vector<std::thread> threads_; // 线程池
}; // class ThreadPool

// class ThreadPool的实现
ThreadPool::ThreadPool(int n) : stop_(false) {
    auto nthreads = n;
    if (nthreads <= 0) {
        // 返回基于硬件特性的线程数，如果不确定则返回0
        nthreads = std::thread::hardware_concurrency();
        nthreads = nthreads == 0 ? 2 : nthreads;
    }

    for (auto i = 0; i != nthreads; ++i) {
        threads_.push_back(std::thread(
            // 工作线程函数
            [this]{
                while (!this->stop_) {
                    task_type task;
                    {
                        std::unique_lock<std::mutex> ulk(this->mtx_);
                        // wait直到有task
                        this->cond_.wait(ulk,
                            [this]{ return this->stop_.load() ||
                                !this->tasks_.empty(); });
                        if (this->stop_ && this->tasks_.empty()) return;
                        task = std::move(this->tasks_.front()); // 取一个task
                        this->tasks_.pop();
                    }
                    task();
                }
            }
        ));
    }
}

// 可变参数模板，无参数数量限制
template<typename Function, typename... Args>
std::future<typename std::result_of<Function(Args...)>::type>
ThreadPool::submit(Function&& fcn, Args&&... args) {
    typedef typename std::result_of<Function(Args...)>::type return_type;
    typedef std::packaged_task<return_type()> task;

    auto t = std::make_shared<task>(
        std::bind(std::forward<Function>(fcn), std::forward<Args>(args)...));
    auto ret = t->get_future();
    {
        std::lock_guard<std::mutex> lg(mtx_);
        if (stop_.load())
            throw std::runtime_error("thread pool has stopped!");
        tasks_.emplace([t]{ (*t)(); });
    }
    cond_.notify_one(); //　唤醒一个线程执行
    return ret;
}

#endif
