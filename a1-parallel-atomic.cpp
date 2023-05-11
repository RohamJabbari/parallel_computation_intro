#include <iostream>
#include <thread>
#include <random>
#include <chrono>
#include <complex>
#include <sstream>
#include <string>
#include <fstream>
#include <iomanip>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <future>

#include "a1-helpers.hpp"

template <typename T>
class SafeQ {
private:
    std::queue<T> q;
    std::atomic_flag locked = ATOMIC_FLAG_INIT;
    std::atomic<bool> producer_done{false};

public:

    // following method was suggested by GPT
    void set_producer_done() {
        producer_done.store(true);
    }

    // following method was suggested by GPT
    bool is_producer_done() {
        return producer_done.load();
    }

    void push(T value) {
        while (locked.test_and_set(std::memory_order_acquire)) {}
        q.push(value);
        locked.clear(std::memory_order_release);
    }

    bool pop(T& value) {
        while (locked.test_and_set(std::memory_order_acquire)) {}
        if (!q.empty()) {
            value = q.front();
            q.pop();
            locked.clear(std::memory_order_release);
            return true;
        }
        locked.clear(std::memory_order_release);
        return false;
    }
    std::shared_ptr<T> wait_and_pop() {
        T value;
            while (true) {
                while (locked.test_and_set(std::memory_order_acquire)) {}
                if (!q.empty()) {
                    value = q.front();
                    q.pop();
                    locked.clear(std::memory_order_release);
                    return std::make_shared<T>(std::move(value));
                }
                locked.clear(std::memory_order_release);
                if (is_producer_done()) {
                    return nullptr;
                }
                std::this_thread::yield(); // Give up the CPU to avoid spinning
            }
    }

    size_t size()
    {
        return q.size();
    }

    bool empty()
    {
        return q.empty();
    }
};

/**
 * To be executed by the master thread
 * The function reads numbers from the file
 * and puts them into the given queue
 * 
 * @param[in] filename
 * @param[inout] q
 * @returns The number of produced items
 * 
*/
int producer(std::string filename, std::vector<SafeQ<int>> &qs, int num_threads)
{
    int produced_count = 0;
    int current_q = 0;
    // while there are entries in the file
    // put numbers into the queue "q"
    std::ifstream ifs(filename);
    
    while (!ifs.eof()) {
        int num;
        ifs >> num;
        qs[current_q].push(num);
        produced_count++;
        current_q = (current_q + 1) % num_threads;
    }

    ifs.close();

    for(auto &q: qs)
        q.set_producer_done();

    return produced_count;
}

/**
 * To be executed by worker threads
 * The function removes a number from the queue "q"
 * and does the processing
 * Implement 2 versions with atomic and with mutexes
 * extend as needed
 * 
 * @param[inout] q
 * @param[inout] primes
 * @param[inout] nonprimes
 * @param[inout] mean
 * @param[inout] number_counts
 * 
*/
void worker(SafeQ<int> &q, std::atomic<int> &primes, std::atomic<int> &nonprimes, std::atomic<double> &sum, std::atomic<int> &consumed_count, std::vector<std::atomic<int>> &number_counts)
{
    // implement: use synchronization
    // Note: This part may need some rearranging and rewriting
    // the while loop cannot just check for the size, 
    // it has to now wait until the next element can be popped,
    // or it has to terminate if producer has finished and the queue is empty.

    while (true)
    {
        int num;
        auto num_ptr = q.wait_and_pop();
        if (num_ptr == nullptr)
            break;
        num = *num_ptr;

        consumed_count.fetch_add(1, std::memory_order_relaxed);
        if (kernel(num) == 1) {
          primes.fetch_add(1, std::memory_order_relaxed);
        } else {
          nonprimes.fetch_add(1, std::memory_order_relaxed);
        }
        number_counts[num % 10].fetch_add(1, std::memory_order_relaxed);
        sum.fetch_add(static_cast<double>(num), std::memory_order_relaxed);
    }
    
}

int main(int argc, char **argv)
{
    int num_threads = 32;
    // int num_threads = std::thread::hardware_concurrency();
    // int num_threads = std::thread::hardware_concurrency();
    bool no_exec_times = false, only_exec_times = false;; // reporting of time measurements
    std::string filename = "input.txt";
    parse_args(argc, argv, num_threads, filename, no_exec_times, only_exec_times);

    // The actuall code
    std::atomic<int> primes{0}, nonprimes{0}, consumed_count{0};
    std::atomic<double> sum{0.0};
    std::atomic<double> mean{0.0};
    // vector for storing numbers ending with different digits (0-9)
    std::vector<std::atomic<int>> number_counts(10);
    
    // Queue that needs to be made safe 
    // In the simple form it takes integers 
    SafeQ<int> q;
    std::vector<SafeQ<int>> qs(num_threads);

    // put you worker threads here
    std::vector<std::thread> workers;
    workers.reserve(num_threads);

    // time measurement
    auto t1 =  std::chrono::high_resolution_clock::now();
    
    // implement: call the producer function with futures/async 
    // int produced_count = producer(filename, q);
    auto producer_future = std::async(std::launch::async, producer, filename, std::ref(qs), num_threads);


    int produced_count = producer_future.get(); 
   
    // implement: spawn worker threads - transform to spawn num_threads threads and store in the "workers" vector
    // for (int i=0;i<num_threads;++i) {
    //     worker(q, primes, nonprimes, sum, consumed_count, number_counts); 
    //     // imlpement: switch the line above with something like: 
    //     // workers.push_back(thread(worker,...));
    //     workers.push_back(std::thread(worker, std::ref(q), std::ref(primes), std::ref(nonprimes), std::ref(sum), std::ref(consumed_count), std::ref(number_counts))); 
    // }
    for (int i = 0; i < num_threads; ++i) {
        workers.emplace_back(std::thread(worker, std::ref(qs[i]), std::ref(primes), std::ref(nonprimes), std::ref(sum), std::ref(consumed_count), std::ref(number_counts)));
    }
    
    for (std::thread &worker_thread : workers) {
        worker_thread.join();
    }

    mean = sum/consumed_count;

    
    // end time measurement
    auto t2 =  std::chrono::high_resolution_clock::now();

    // do not remove
    if ( produced_count != consumed_count ) {
         std::cout << "[error]: produced_count (" << produced_count << ") != consumed_count (" << consumed_count << ")." <<  std::endl;
    }

    // priting the results
    // print_output(num_threads, primes, nonprimes, mean, number_counts, t1, t2, only_exec_times, no_exec_times);
    std::vector<int> plain_number_counts(number_counts.size());
    for (size_t i = 0; i < number_counts.size(); i++) {
        plain_number_counts[i] = number_counts[i].load();
    }
    print_output(num_threads, primes, nonprimes, mean, plain_number_counts, t1, t2, only_exec_times, no_exec_times);

    return 0;
}
