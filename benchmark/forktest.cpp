#include <thread>
#include <iostream>
#include <atomic>
#include <chrono>
#include <vector>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace std::chrono;

size_t count_per_thd = 0;

void fork_worker() {
    for (size_t i = 0; i < count_per_thd; ++i) {
        if (fork() == 0) {
            _Exit(0);
        }
    }
}

void multi_fork_worker(size_t nthd) {
    std::vector<std::thread> thds;
    thds.reserve(nthd);
    for (size_t i = 0; i < nthd; ++i) {
        thds.push_back(std::thread(fork_worker));
    }
    for (auto &t : thds) {
        t.join();
    }
}

int main() {
    // This is a hack for deadlock checker waiter.
    // It must have at least one child to wait.
    if (fork() == 0) {
        while(true)
            std::this_thread::sleep_for(std::chrono::hours(24));
    }

    count_per_thd = 100000;

    std::thread waiter([=]() {
        // Wait for all checker child process and assume we do the rollback here
        for (int i = 0; i < count_per_thd; ++i)
            wait(NULL);
    });

    // size_t thds = 8;
    size_t thds = 0;

    auto t1 = high_resolution_clock::now();
    if (thds == 0)
        fork_worker();
    else
        multi_fork_worker(thds);
    auto t2 = high_resolution_clock::now();
    waiter.join();
    auto t3 = high_resolution_clock::now();

    long long duration = duration_cast<nanoseconds>(t2 - t1).count();

    std::cout << duration << std::endl;
    std::cout << (duration_cast<nanoseconds>(t3 - t2).count()) << std::endl;
    std::cout << (double) count_per_thd * (thds == 0 ? 1 : thds) / duration * 1000000000 << std::endl;

    return 0;

}
