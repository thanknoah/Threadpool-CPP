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
    struct info {
        std::queue<std::function<void()>> taskQuene; // Stores the tasks 
        std::mutex queneMutex; // stop inserting while internalThreadOperations reads quene
    };
    std::unordered_map<int, std::shared_ptr<info>> threadInformation; // never changes size so no need for mutex
    std::vector<std::thread> threadList;
    std::atomic<int> optimalThread;
    std::atomic<bool> stopThread;

    void internalThreadOperation(std::shared_ptr<info>& e) {
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
            }
            task();
        }
    }



    void findLeastExhaustedWorker() {
        while (true) {
            if (stopThread.load())
                break;

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            int smallestNum = 0;
            int smallestWorkerSize = 0;

            for (auto it = threadInformation.begin(); it != threadInformation.end(); ++it) {
                std::unique_lock<std::mutex> templock(it->second->queneMutex);

                if (it->second->taskQuene.size() <= smallestNum) {
                    smallestWorkerSize = it->first;
                    smallestNum = it->second->taskQuene.size();
                }
            }
            optimalThread.store(smallestWorkerSize);
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
        stopThread = false;
        for (int x = 0; x < std::thread::hardware_concurrency(); ++x) {
            auto internalThreadDetails = std::make_shared<info>();
            std::thread t(std::bind(&threadPool::internalThreadOperation, this, internalThreadDetails));
            threadList.push_back(std::move(t));
            threadInformation.insert({ x, internalThreadDetails });
        }

        std::thread t1(std::bind(&threadPool::findLeastExhaustedWorker, this));
        threadList.push_back(std::move(t1));
    };

    template <typename F, typename... Args>
    auto executeTaskInThreadPool(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> { // Telling compiler return result is std::future<> and wtv value of function return value is
        auto threadToExecute = optimalThread.load();
        using return_type = std::invoke_result_t<F, Args...>; // Gets value type of typenmae F and Args at compile time
        std::future<return_type>futureValue;

        auto func = std::make_shared<std::packaged_task<return_type()>>( // Turns function into an std::packaged_task, so user can use future to aysncronously retrieve result 
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        threadInformation[threadToExecute]->taskQuene.emplace([func]() { (*func)(); }); // place function into threads task quene

        futureValue = func->get_future();
        return futureValue;
    }
};

int add(int a, int b) {
    return a + b;
}

// testing
int main() {
    threadPool t;
    t.init();


    auto calculation = t.executeTaskInThreadPool(add, 1, 2); 
    auto calculation2 = t.executeTaskInThreadPool(add, 3, 5);
    auto calculation3 = t.executeTaskInThreadPool(add, 5, 6);
    std::cout << "Non blocking\n"; 
    std::cout << calculation.get() << "\n";
    std::cout << calculation2.get() << "\n";
    std::cout << calculation3.get() << "\n";
}

