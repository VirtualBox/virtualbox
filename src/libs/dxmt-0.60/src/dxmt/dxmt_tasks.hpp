#pragma once

#include "thread.hpp"
#include <atomic>
#include <queue>
#include <unordered_map>

namespace dxmt {

template <typename Task> struct task_trait {
  Task run_task(Task task);
  bool get_done(Task task);
  void set_done(Task task);
};

template <typename Task> class task_scheduler {
public:
  void submit(Task task);

  task_scheduler();
  ~task_scheduler();

  uint64_t
  get_running_threads() {
    return running.load(std::memory_order_relaxed);
  }

private:
  void worker_func();

  dxmt::mutex worker_mutex_;
  dxmt::condition_variable worker_cond_;
  std::queue<Task> task_queue_;
  std::queue<Task> task_continuation_queue_;

  dxmt::mutex deps_mutex_;
  std::unordered_multimap<Task, Task> task_continuation_;

  std::vector<dxmt::thread> workers_;

  std::atomic_bool destroyed = false;
  std::atomic_uint64_t running = 0;
  uint64_t threads;
  uint64_t max_threads;
};

template <typename Task> task_scheduler<Task>::task_scheduler() {
  max_threads = dxmt::thread::hardware_concurrency() * 2;
  workers_.reserve(max_threads);
  threads = 2;

  for (unsigned i = 0; i < threads; i++) {
    workers_.emplace_back([this]() { worker_func(); });
  }
}

template <typename Task> task_scheduler<Task>::~task_scheduler() {
  destroyed.store(true);
  worker_cond_.notify_all();

  for (auto &worker : workers_)
    worker.join();

  workers_.clear();
}

template <typename Task>
void
task_scheduler<Task>::worker_func() {
  // FIXME: use windows alternative?
  // __pthread_set_qos_class_self_np(__QOS_CLASS_USER_INTERACTIVE, 0);
  struct task_trait<Task> task_trait;
  std::vector<Task> continutation_buffer;
  while (!destroyed.load()) {
    Task task;
    {
      std::unique_lock<dxmt::mutex> lock(worker_mutex_);

      if (task_queue_.empty() && task_continuation_queue_.empty()) {
        worker_cond_.wait(lock, [this]() {
          return task_queue_.size() || task_continuation_queue_.size() || destroyed.load();
        });
      }

      if (!task_continuation_queue_.empty()) {
        task = task_continuation_queue_.front();
        task_continuation_queue_.pop();
      } else if (!task_queue_.empty()) {
        task = task_queue_.front();
        task_queue_.pop();
      } else {
        break;
      }
    }
    running.fetch_add(1, std::memory_order_relaxed);
    while (true) {
      Task continuation = task_trait.run_task(task);
      if (continuation == task) {
        {
          std::unique_lock<dxmt::mutex> lock(deps_mutex_);
          auto range = task_continuation_.equal_range(continuation);
          for (auto itr = range.first; itr != range.second; ++itr) {
            continutation_buffer.push_back(itr->second);
          }
          task_continuation_.erase(range.first, range.second);
          task_trait.set_done(continuation);
        }
        {
          std::unique_lock<dxmt::mutex> lock(worker_mutex_);
          for (auto &task : continutation_buffer) {
            task_continuation_queue_.push(task);
          }
        }
        worker_cond_.notify_all();
        continutation_buffer.clear();
      } else {
        std::unique_lock<dxmt::mutex> lock(deps_mutex_);
        // spurious dependency
        if (task_trait.get_done(continuation)) {
          continue;
        }
        task_continuation_.insert({continuation, task});
      }
      break;
    }
    running.fetch_sub(1, std::memory_order_relaxed);
  }
};

template <typename Task>
void
task_scheduler<Task>::submit(Task task) {
  std::unique_lock<dxmt::mutex> lock(worker_mutex_);
  task_queue_.push(task);

  if (running.load(std::memory_order_relaxed) == threads && threads < max_threads) {
    workers_.emplace_back([this]() { worker_func(); });
    threads++;
  }

  worker_cond_.notify_one();
}

}; // namespace dxmt
