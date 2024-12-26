#include <iostream>
#include <thread>
#include <vector>
#include <queue>
#include <functional>
#include <atomic>
#include <condition_variable>
#include <utility>
#include <chrono>

using lock_t = std::unique_lock <std::mutex>;


// Потокобезопасная очередь
class notification_queue {
    std::queue<std::function<void()>> _q;
    std::mutex _mutex;
    std::condition_variable _ready;
    bool _done{ false };

public:
    template<typename Func>
    void push(Func&& f) {
        {
            lock_t lock(_mutex);
            _q.emplace(std::forward<Func>(f));
        }
        _ready.notify_one();
    }

    template<typename Func>
    bool try_push(Func&& f) {
        {
            lock_t lock(_mutex, std::try_to_lock);
            if(!lock) return false;
            _q.emplace(std::forward<Func>(f));
        }
        _ready.notify_one();
        return true;
    }

    bool pop(std::function<void()>& f) {
        lock_t lock(_mutex);
        _ready.wait(lock, [this] { return !_q.empty() || _done; });

        if (_q.empty()) return false;
        f = std::move(_q.front());
        _q.pop();
        return true;
    }

    bool try_pop(std::function<void()>& f) {
        lock_t lock(_mutex);
        if (_q.empty()) return false;
        f = std::move(_q.front());
        _q.pop();
        return true;
    }

    void done() {
        {
            lock_t lock(_mutex);
            _done = true;
        }
        _ready.notify_all();
    }
};

// Потокобезопасная система выполнения задач
class task_system {
    const unsigned _count = std::thread::hardware_concurrency();
    std::vector<notification_queue> _q{ _count };
    std::vector<std::thread> _threads;
    std::atomic<unsigned> _index{ 0 };

public:
    void run(unsigned i) {
        while (true) {
            std::function<void()> f;
            if (_q[i].try_pop(f)) {
                f();
                continue;
            }
            if (!_q[i].pop(f)) break;
            f();
        }
    }

    task_system() {
        for (unsigned i = 0; i < _count; ++i)
            _threads.emplace_back([&, i] { run(i); });
    }

    ~task_system() {
        for (auto& e : _q) e.done();
        for (auto& e : _threads) e.join();
    }

    template<typename Func>
    void async(Func&& f) {
        auto i = _index++;
        for (unsigned n = 0; n < _count; n++) {
            if (_q[(i + n) % _count].try_push(std::forward<Func>(f)))
                return;
        }
        _q[i % _count].push(std::forward<Func>(f));
    }
};
