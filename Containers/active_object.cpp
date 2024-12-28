#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <utility>
#include <iostream>
#include <vector>
#include <random>
#include <algorithm>

// ActiveObject is the deque allows to work asynchronously via futures
class ActiveObject {
public:
    template <typename Func, typename... Args>
    auto enqueueTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))> {
        using ReturnType = decltype(func(args...));
        auto task = std::packaged_task<ReturnType()>(std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
        auto future = task.get_future();

        {
            std::lock_guard<std::mutex> lock(activationListMutex);
            activationList.emplace_back([task = std::move(task)]() mutable { task(); });
        }

        taskAvailable.notify_one();
        return future;
    }

    void run() {
        std::thread([this] {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(activationListMutex);
                    taskAvailable.wait(lock, [this] { return !activationList.empty(); });

                    task = std::move(activationList.front());
                    activationList.pop_front();
                }

                task();
            }
        }).detach();
    }

private:
    std::deque<std::function<void()>> activationList;
    std::mutex activationListMutex;
    std::condition_variable taskAvailable;
};


#ifdef TEST

std::vector<int> generateRandomNumbers(int count, int min, int max) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(min, max);

    std::vector<int> numbers(count);
    std::generate(numbers.begin(), numbers.end(), [&]() { return dist(gen); });
    return numbers;
}

int main() {
    ActiveObject activeObject;
    activeObject.run();

    auto numbers = generateRandomNumbers(100, 1'000'000, 1'000'000'000);
    std::vector<std::future<bool>> results;

    for (auto num : numbers) {
        results.push_back(activeObject.enqueueTask(isPrime, num));
    }

    for (size_t i = 0; i < results.size(); ++i) {
        std::cout << "Number " << numbers[i] << " is prime: " << std::boolalpha << results[i].get() << '\n';
    }

    return 0;
}

#endif
