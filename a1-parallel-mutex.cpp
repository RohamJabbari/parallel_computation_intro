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
#include <mutex> 
#include <condition_variable> 
#include <atomic> 
#include <future> 
 
#include "a1-helpers.hpp" 
 
template <typename T> 
class SafeQ 
{ 
private: 

    std::queue<T> q; // no other data structures are allowed
    
    // A condition variable used 
    // For synchronization between producer and consumer threads.
    std::condition_variable cv; 
    
    // A mutex to protect access to shared resources,
    std::mutex mtx; 
    
    // extend as needed 
public: 
    // void push(T value) 
    // { 
    //     // synchronization not necessary
    //     // std::unique_lock<std::mutex> lock(mtx); 
    //     q.push(value); 
    // } 
    void push(T value)
    {
        std::unique_lock<std::mutex> lock(mtx);
        q.push(value);
        lock.unlock(); // Unlock the mutex before notifying
        cv.notify_one(); // Notify one waiting thread
    }


    void pop(T &value) 
    { 
        // todo:  
        // in a thread-safe way take the front element 
        // and pop it from the queue 
        // multiple consumers may be accessing this method 
        std::unique_lock<std::mutex> lock(mtx); 
        while (q.empty()) // Use a while loop to handle wakeups 
        { 
            cv.wait(lock); 
        } 

        // if not empty, remove the element from the queue
        if (!q.empty()) 
        { 
            value = q.front(); 
            q.pop(); 
        } 
    } 
 
    std::shared_ptr<T> wait_and_pop() 
    { 
        // todo:  
        // in a thread-safe way take the front element 
        // and pop it from the queue 
        // multiple consumers may be accessing this method 

        std::unique_lock<std::mutex> lock(mtx); 
        cv.wait(lock, [this] { return !q.empty(); }); // Wait until the queue is not empty 
 
        if ( !q.empty() ){ 
            std::shared_ptr<T> res_ptr (std::make_shared<T>(q.front()));
            q.pop(); 
            return res_ptr; 
        } 
        return nullptr; 
    } 
 
    size_t size() 
    { 
        std::unique_lock<std::mutex> lock(mtx); 
        return q.size(); 
    } 
 
    bool empty() 
    { 
        std::unique_lock<std::mutex> lock(mtx); 
        return q.empty(); 
    } 
}; 
 
/* 
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

    // following code was suggested by GPT
    // Push sentinel values
    for (auto &q : qs)
        q.push(-1);

    return produced_count;
}

/*
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

void worker(SafeQ<int> &q, int &primes, int &nonprimes, double &sum, int &consumed_count, std::vector<int> &number_counts, std::mutex &mtx) 
{ 
    /* Local variables to store the intermediate results for each worker thread */
    int local_primes = 0, local_nonprimes = 0, local_consumed_count = 0;
    double local_sum = 0.0;
    std::vector<int> local_number_counts(10, 0);

    /* Keep processing items from the queue until there are no more items or a sentinel value is encountered */
    while (true) 
    { 
        auto num_ptr = q.wait_and_pop(); 
        // *num_ptr == -1 was suggested by GPT. It is related to the sentinel value check.
        
        if (num_ptr == nullptr || *num_ptr == -1) 
            break; 
 
        int num = *num_ptr; 
 
        /* Increment the local consumed count and check if the number is prime or not */
        local_consumed_count++; 
        if (kernel(num) == 1) { 
          local_primes++; 
        } else { 
          local_nonprimes++; 
        } 
         
        /* Update the count for numbers ending with different digits */
        local_number_counts[num % 10]++; 
        local_sum += num; 
    }

    /* Acquire a lock on the mutex to safely update shared variables */
    std::unique_lock<std::mutex> lock(mtx);

    /* Update the shared variables with the local results */
    consumed_count += local_consumed_count;
    primes += local_primes;
    nonprimes += local_nonprimes;
    sum += local_sum;
    for (size_t i = 0; i < 10; ++i)
    {
        number_counts[i] += local_number_counts[i];
    }
}

int main(int argc, char **argv) 
{ 
    /* Declare a mutex object that can be used to synchronize access to
        shared resources between multiple threads */
    std::mutex mtx; 

    int num_threads = 32;
    // int num_threads = std::thread::hardware_concurrency();
    bool no_exec_times = false, only_exec_times = false;; // reporting of time measurements 
    std::string filename = "input.txt"; 
    parse_args(argc, argv, num_threads, filename, no_exec_times, only_exec_times); 
 
    // The actuall code 
    int primes = 0, nonprimes = 0, count = 0; 
    int consumed_count = 0; 
    double mean = 0.0, sum = 0.0; 
    // vector for storing numbers ending with different digits (0-9) 
    std::vector<int> number_counts(10, 0); 
     
    // Queue that needs to be made safe  
    // In the simple form it takes integers  
    SafeQ<int> q; 
    
     
    // put you worker threads here 
    std::vector<std::thread> workers; 

    /* To improve performance and avoid unnecessary reallocations */
    workers.reserve(num_threads);

    // time measurement 
    auto t1 =  std::chrono::high_resolution_clock::now(); 

    // implement: call the producer function with futures/async 

    // auto producer_future = std::async(std::launch::async, producer, filename, std::ref(q)); 
    std::vector<SafeQ<int>> qs(num_threads);
    auto producer_future = std::async(std::launch::async, producer, filename, std::ref(qs), num_threads);

    int produced_count = producer_future.get(); 

    // implement: spawn worker threads - transform to spawn num_threads threads and store in the "workers" vector
    for (int i = 0; i < num_threads; ++i) 
    { 
        // imlpement: switch the line above with something like: 
        // workers.push_back(thread(worker,...));

        workers.emplace_back(std::thread(worker, std::ref(qs[i]), std::ref(primes), std::ref(nonprimes), std::ref(sum), std::ref(consumed_count), std::ref(number_counts), std::ref(mtx))); 
    } 

    /* joining workers */
    for (auto &worker: workers)
    {
        worker.join();
    }

    mean = sum/consumed_count; 

    // end time measurement 
    auto t2 =  std::chrono::high_resolution_clock::now(); 
    
    // do not remove 
    if ( produced_count != consumed_count ) { 
         std::cout << "[error]: produced_count (" << produced_count << ") != consumed_count (" << consumed_count << ")." <<  std::endl; 
    } 
 
    // priting the results 
    print_output(num_threads, primes, nonprimes, mean, number_counts, t1, t2, only_exec_times, no_exec_times); 

    return 0; 
}