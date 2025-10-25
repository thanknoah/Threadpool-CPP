#include <any>
#include <atomic>
#include <functional>
#include <shared_mutex>
#include <iostream>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <chrono>
#include <utility>
#include <future>
#include <random>
#include <variant>

class threadPool {
private:
    struct threadUsageInfo {
        int threadId;
        std::shared_ptr<std::atomic<int>> threadTaskAmounts;
    };
    struct threadTaskInfo {
        std::queue<std::function<void()>> taskQuene; // Stores the tasks 
        std::mutex queneMutex; // stop inserting while internalThreadOperations reads quene
    };

    std::unordered_map<int, std::shared_ptr<threadTaskInfo>> threadInformation; // never changes size so no need for mutex
    std::vector<threadUsageInfo> threadUsage;
    std::vector<std::thread> threadList;
    std::atomic<bool> stopThread;

    int findLeastExhaustedWorker() {
        if (threadUsage.empty()) return -1; // no threads

        auto it = threadUsage.begin();
        int smallestWorkerSize = it->threadId;
        int smallestNum = it->threadTaskAmounts->load();

        for (; it != threadUsage.end(); ++it) {
            int currentThreadTasksAmount = it->threadTaskAmounts->load();

            if (currentThreadTasksAmount < smallestNum) {
                smallestWorkerSize = it->threadId;
                smallestNum = currentThreadTasksAmount;
            }
        }
        return smallestWorkerSize;
    }

    void internalThreadOperation(std::shared_ptr<threadTaskInfo> e, std::shared_ptr<std::atomic<int>> taskMonitoring) {
        while (true) {
            if (e->taskQuene.empty() && !stopThread.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            else if (e->taskQuene.empty() && stopThread.load()) {
                break;
            }

            std::function<void()> task;
            {
                std::lock_guard<std::mutex> queneMutexLock(e->queneMutex);
                task = std::move(e->taskQuene.front());
                e->taskQuene.pop();
                (*taskMonitoring)--;
            }
            task();
        }
    }

    void monitorTasksPerThread(int waitMs) {
        auto waitTime = std::chrono::milliseconds(waitMs);

        while (true) {
            std::this_thread::sleep_for(waitTime);

            for (auto it = threadUsage.begin(); it != threadUsage.end(); ++it) {
                std::cout << "Thread #" << it->threadId << ": " << it->threadTaskAmounts->load() << " Tasks \n ";
            }
            std::cout << "\n";
        }
    }

public:
    ~threadPool() {
        stopThread.store(true);
        for (auto& thread : threadList) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    void init() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        threadUsage.resize(std::thread::hardware_concurrency());
        stopThread.store(false);
     
        for (int x = 0; x < std::thread::hardware_concurrency(); ++x) {
            auto internalThreadDetails = std::make_shared<threadTaskInfo>();
            auto internalTaskRef = std::make_shared<std::atomic<int>>();

            threadUsage[x].threadId = x;
            threadUsage[x].threadTaskAmounts = internalTaskRef;

            std::thread t([this, internalThreadDetails, internalTaskRef]() {
                internalThreadOperation(internalThreadDetails, internalTaskRef);
            });

            threadInformation.insert({ x, internalThreadDetails });
            threadList.push_back(std::move(t));
        }

        //threadList.emplace_back([this]() { monitorTasksPerThread(1000); });
    };

    template <typename F, typename... Args>
    auto executeTaskInThreadPool(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> { // Telling compiler return result is std::future<> and wtv value of function return value is
        auto threadToExecute = findLeastExhaustedWorker();
        using return_type = std::invoke_result_t<F, Args...>; // Getting return type of function
        std::future<return_type> futureResult;

        if constexpr (std::is_void_v<return_type>) {
            auto task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
            {
                std::lock_guard<std::mutex> lock(threadInformation[threadToExecute]->queneMutex);
                threadInformation[threadToExecute]->taskQuene.push(task);
            }
            (*threadUsage[threadToExecute].threadTaskAmounts)++;
            return futureResult;

        }
        else {
            auto task = std::make_shared<std::packaged_task<return_type()>>( // Putitng it in a packaged task so can retrieve reuslts aysncronously
                std::bind(std::forward<F>(f), std::forward<Args>(args)...)
            );
            futureResult = task->get_future();

            {
                std::lock_guard<std::mutex> lock(threadInformation[threadToExecute]->queneMutex);
                threadInformation[threadToExecute]->taskQuene.push([task]() {
                    (*task)();
                });
            }

            (*threadUsage[threadToExecute].threadTaskAmounts)++;
            return futureResult;
        }
    }
};


// demonstration functions
void add(int a, int b) {
    a + b;
}

int returnAdd(int a, int b) {
    return a + b;
}

int main() {
    // Intialise it
    threadPool t;
    t.init();

    // Submit tasks without retrieving results
    for (size_t i = 0; i < 10; ++i) {
        t.executeTaskInThreadPool(add, 1, 2);
    }

    // Can do work on main thread
    std::cout << "All tasks submitted (non returning, non-blocking). \n";

    // Submit Tasks with getting results
    std::vector<std::future<int>> calculationResults; // storing results as futures
    for (size_t x = 0; x < 10; ++x) {
        calculationResults.emplace_back(t.executeTaskInThreadPool(returnAdd, x, x)); 
    }

    // Can do work on main thread
    std::cout << "All tasks submitted (returning, non-blocking).\n";

    // Retrieving results for all tasks
    for (size_t x = 0; x < 10; ++x) {
        int result = calculationResults[x].get();
        std::cout << "Result of task #" << x << ": " << result << "\n";
    }


}

