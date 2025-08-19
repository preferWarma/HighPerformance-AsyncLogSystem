#pragma once

#include <any>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <deque>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <unistd.h>
#include <vector>

// 平台特定头文件
#ifdef __linux__
#include <sys/epoll.h>
#include <sys/timerfd.h>
#elif defined(__APPLE__) || defined(__FreeBSD__)
#include <sys/event.h>
#else
#error "Unsupported platform"
#endif

namespace lyf {

// 前向声明
class CoroutinePool;
template <typename T> class Channel;
template <typename T> class Task;
class Context;

// ==================================================================================
// 核心概念：协程上下文
// ==================================================================================

class CancellationToken {
public:
  CancellationToken()
      : cancelled_(std::make_shared<std::atomic<bool>>(false)) {}

  void cancel() {
    cancelled_->store(true, std::memory_order_release);
    std::lock_guard lock(mutex_);
    for (auto &callback : callbacks_) {
      callback();
    }
    callbacks_.clear();
  }

  bool is_cancelled() const {
    return cancelled_->load(std::memory_order_acquire);
  }

  void on_cancel(std::function<void()> callback) {
    std::lock_guard lock(mutex_);
    if (is_cancelled()) {
      callback();
    } else {
      callbacks_.push_back(std::move(callback));
    }
  }

private:
  std::shared_ptr<std::atomic<bool>> cancelled_;
  mutable std::mutex mutex_;
  std::vector<std::function<void()>> callbacks_;
};

class Context {
public:
  Context() = default;

  // 错误处理
  void set_error(std::exception_ptr e) { error_ = e; }
  std::exception_ptr get_error() const { return error_; }
  bool has_error() const { return error_ != nullptr; }

  // 取消支持
  CancellationToken &cancellation() { return cancel_token_; }

  // 协程本地存储
  template <typename T> void set_local(const std::string &key, T value) {
    locals_[key] = std::make_any<T>(std::move(value));
  }

  template <typename T>
  std::optional<T> get_local(const std::string &key) const {
    auto it = locals_.find(key);
    if (it != locals_.end()) {
      try {
        return std::any_cast<T>(it->second);
      } catch (...) {
      }
    }
    return std::nullopt;
  }

private:
  std::exception_ptr error_;
  CancellationToken cancel_token_;
  std::map<std::string, std::any> locals_;
};

// 当前协程的上下文
inline thread_local Context *current_context = nullptr;

// ==================================================================================
// 时间轮定时器（优化版）
// ==================================================================================

class TimerWheel {
public:
  using TimePoint = std::chrono::steady_clock::time_point;
  using Callback = std::function<void()>;

  struct Timer {
    TimePoint when;
    Callback callback;
    bool operator<(const Timer &other) const {
      return when > other.when; // 小顶堆
    }
  };

  void add_timer(TimePoint when, Callback cb) {
    std::lock_guard lock(mutex_);
    timers_.push({when, std::move(cb)});
    cv_.notify_one();
  }

  void run() {
    while (!stop_flag_.load()) {
      std::unique_lock lock(mutex_);

      if (timers_.empty()) {
        cv_.wait_for(lock, std::chrono::milliseconds(100));
        continue;
      }

      auto now = std::chrono::steady_clock::now();
      if (timers_.top().when <= now) {
        auto timer = timers_.top();
        timers_.pop();
        lock.unlock();
        timer.callback();
      } else {
        cv_.wait_until(lock, timers_.top().when);
      }
    }
  }

  void stop() {
    stop_flag_.store(true);
    cv_.notify_all();
  }

private:
  std::priority_queue<Timer> timers_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<bool> stop_flag_{false};
};

// ==================================================================================
// 增强的协程池
// ==================================================================================

class CoroutinePool {
public:
  static CoroutinePool &instance() {
    static CoroutinePool pool(std::thread::hardware_concurrency());
    return pool;
  }

  CoroutinePool(const CoroutinePool &) = delete;
  CoroutinePool &operator=(const CoroutinePool &) = delete;

  void schedule(std::coroutine_handle<> h) {
    if (!h)
      return;

    // 工作窃取：选择任务最少的队列
    size_t min_size = SIZE_MAX;
    size_t chosen = 0;

    for (size_t i = 0; i < worker_queues_.size(); ++i) {
      size_t size = worker_queues_[i].size_approx();
      if (size < min_size) {
        min_size = size;
        chosen = i;
      }
    }

    worker_queues_[chosen].enqueue(h);
  }

  void add_timer(std::coroutine_handle<> h,
                 std::chrono::steady_clock::time_point when) {
    timer_wheel_.add_timer(when, [this, h]() { schedule(h); });
  }

private:
  class WorkQueue {
  public:
    void enqueue(std::coroutine_handle<> h) {
      std::lock_guard lock(mutex_);
      queue_.push_back(h);
      cv_.notify_one();
    }

    std::optional<std::coroutine_handle<>> dequeue() {
      std::unique_lock lock(mutex_);
      if (queue_.empty()) {
        return std::nullopt;
      }
      auto h = queue_.front();
      queue_.pop_front();
      return h;
    }

    std::optional<std::coroutine_handle<>> steal() {
      std::lock_guard lock(mutex_);
      if (queue_.empty()) {
        return std::nullopt;
      }
      auto h = queue_.back();
      queue_.pop_back();
      return h;
    }

    bool wait_dequeue(std::coroutine_handle<> &h,
                      std::chrono::milliseconds timeout) {
      std::unique_lock lock(mutex_);
      if (cv_.wait_for(lock, timeout, [this] { return !queue_.empty(); })) {
        h = queue_.front();
        queue_.pop_front();
        return true;
      }
      return false;
    }

    size_t size_approx() const {
      std::lock_guard lock(mutex_);
      return queue_.size();
    }

  private:
    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    std::deque<std::coroutine_handle<>> queue_;
  };

  CoroutinePool(unsigned n) : worker_queues_(n) {
    workers_.reserve(n);
    for (unsigned i = 0; i < n; ++i) {
      workers_.emplace_back(&CoroutinePool::worker, this, i);
    }
    timer_thread_ = std::thread(&TimerWheel::run, &timer_wheel_);
  }

  ~CoroutinePool() {
    stop_flag_.store(true);
    timer_wheel_.stop();

    for (auto &q : worker_queues_) {
      q.enqueue(nullptr); // 唤醒所有工作线程
    }

    for (auto &t : workers_) {
      if (t.joinable())
        t.join();
    }

    if (timer_thread_.joinable()) {
      timer_thread_.join();
    }
  }

  void worker(size_t id) {
    auto &my_queue = worker_queues_[id];

    while (!stop_flag_.load(std::memory_order_relaxed)) {
      std::coroutine_handle<> h;

      // 1. 尝试从自己的队列获取
      if (auto opt = my_queue.dequeue()) {
        h = *opt;
      }
      // 2. 工作窃取：从其他队列偷取
      else {
        for (size_t i = 0; i < worker_queues_.size(); ++i) {
          if (i != id) {
            if (auto opt = worker_queues_[i].steal()) {
              h = *opt;
              break;
            }
          }
        }
      }

      // 3. 如果还是没有任务，等待
      if (!h) {
        my_queue.wait_dequeue(h, std::chrono::milliseconds(10));
      }

      // 4. 执行任务
      if (h && h.address()) {
        h.resume();
      }
    }
  }

  std::vector<WorkQueue> worker_queues_;
  std::vector<std::thread> workers_;
  TimerWheel timer_wheel_;
  std::thread timer_thread_;
  std::atomic<bool> stop_flag_{false};
};

// ==================================================================================
// Task - 支持返回值的协程
// ==================================================================================

// 前向声明
template <typename T = void> class Task;

// Task<void> 特化声明（必须在任何使用前）
template <> class Task<void> {
public:
  struct promise_type {
    Task get_return_object() {
      return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }

    std::suspend_always initial_suspend() noexcept { return {}; }

    struct final_awaiter {
      bool await_ready() noexcept { return false; }

      void await_suspend(std::coroutine_handle<promise_type> h) noexcept {
        if (h.promise().continuation_) {
          h.promise().continuation_.resume();
        }
      }

      void await_resume() noexcept {}
    };

    final_awaiter final_suspend() noexcept { return {}; }

    void unhandled_exception() {
      exception_ = std::current_exception();
      if (current_context) {
        current_context->set_error(exception_);
      }
    }

    void return_void() {}

    void result() {
      if (exception_) {
        std::rethrow_exception(exception_);
      }
    }

    std::coroutine_handle<> continuation_;
    std::exception_ptr exception_;
  };

  bool await_ready() const noexcept { return h_.done(); }

  void await_suspend(std::coroutine_handle<> continuation) noexcept;

  void await_resume() { h_.promise().result(); }

  void get(); // 定义移到类外

  explicit Task(std::coroutine_handle<promise_type> h) : h_(h) {}

  ~Task() {
    if (h_)
      h_.destroy();
  }

  Task(Task &&other) noexcept : h_(other.h_) { other.h_ = nullptr; }
  Task &operator=(Task &&other) noexcept {
    if (this != &other) {
      if (h_)
        h_.destroy();
      h_ = other.h_;
      other.h_ = nullptr;
    }
    return *this;
  }

  std::coroutine_handle<promise_type> h_; // 公开给 go_co 使用
};

// Task 通用模板
template <typename T> class Task {
public:
  struct promise_type {
    Task get_return_object() {
      return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
    }

    std::suspend_always initial_suspend() noexcept { return {}; }

    struct final_awaiter {
      bool await_ready() noexcept { return false; }

      void await_suspend(std::coroutine_handle<promise_type> h) noexcept {
        if (h.promise().continuation_) {
          h.promise().continuation_.resume();
        }
      }

      void await_resume() noexcept {}
    };

    final_awaiter final_suspend() noexcept { return {}; }

    void unhandled_exception() {
      exception_ = std::current_exception();
      if (current_context) {
        current_context->set_error(exception_);
      }
    }

    template <typename U> void return_value(U &&value) {
      value_ = std::forward<U>(value);
    }

    T &result() {
      if (exception_) {
        std::rethrow_exception(exception_);
      }
      return value_;
    }

    std::coroutine_handle<> continuation_;
    std::exception_ptr exception_;
    T value_;
  };

  bool await_ready() const noexcept { return h_.done(); }

  void await_suspend(std::coroutine_handle<> continuation) noexcept;

  T await_resume() { return h_.promise().result(); }

  T get(); // 定义移到类外

  explicit Task(std::coroutine_handle<promise_type> h) : h_(h) {}

  ~Task() {
    if (h_)
      h_.destroy();
  }

  Task(Task &&other) noexcept : h_(other.h_) { other.h_ = nullptr; }
  Task &operator=(Task &&other) noexcept {
    if (this != &other) {
      if (h_)
        h_.destroy();
      h_ = other.h_;
      other.h_ = nullptr;
    }
    return *this;
  }

  std::coroutine_handle<promise_type> h_; // 公开给 go_co 使用
};

// Task 成员函数的外部定义（避免循环依赖）
inline void
Task<void>::await_suspend(std::coroutine_handle<> continuation) noexcept {
  h_.promise().continuation_ = continuation;
  CoroutinePool::instance().schedule(h_);
}

template <typename T>
void Task<T>::await_suspend(std::coroutine_handle<> continuation) noexcept {
  h_.promise().continuation_ = continuation;
  CoroutinePool::instance().schedule(h_);
}

inline void Task<void>::get() {
  if (!h_.done()) {
    std::mutex m;
    std::condition_variable cv;
    bool done = false;

    auto wait_coro = [](Task<void> &task, std::mutex &m,
                        std::condition_variable &cv, bool &done) -> Task<void> {
      co_await task;
      {
        std::lock_guard lock(m);
        done = true;
      }
      cv.notify_one();
    };

    auto waiter = wait_coro(*this, m, cv, done);
    CoroutinePool::instance().schedule(waiter.h_);
    waiter.h_ = nullptr;

    std::unique_lock lock(m);
    cv.wait(lock, [&done] { return done; });
  }
  h_.promise().result();
}

template <typename T> T Task<T>::get() {
  if (!h_.done()) {
    std::mutex m;
    std::condition_variable cv;
    bool done = false;

    auto wait_coro = [](Task<T> &task, std::mutex &m,
                        std::condition_variable &cv, bool &done) -> Task<void> {
      co_await task;
      {
        std::lock_guard lock(m);
        done = true;
      }
      cv.notify_one();
    };

    auto waiter = wait_coro(*this, m, cv, done);
    CoroutinePool::instance().schedule(waiter.h_);
    waiter.h_ = nullptr;

    std::unique_lock lock(m);
    cv.wait(lock, [&done] { return done; });
  }
  return h_.promise().result();
}

// ==================================================================================
// Channel - Go风格的通道
// ==================================================================================

template <typename T> class Channel {
public:
  explicit Channel(size_t capacity = 0) : capacity_(capacity), closed_(false) {}

  // 发送操作的可等待对象
  struct SendAwaiter {
    Channel *chan;
    T value;

    bool await_ready() const noexcept {
      std::lock_guard lock(chan->mutex_);
      return chan->closed_ ||
             (chan->capacity_ > 0 && chan->buffer_.size() < chan->capacity_);
    }

    bool await_suspend(std::coroutine_handle<> h) noexcept {
      std::lock_guard lock(chan->mutex_);

      if (chan->closed_) {
        return false;
      }

      // 如果有接收者在等待
      if (!chan->recv_waiters_.empty()) {
        auto [recv_handle, recv_ptr] = chan->recv_waiters_.front();
        chan->recv_waiters_.pop_front();
        *recv_ptr = std::move(value);
        CoroutinePool::instance().schedule(recv_handle);
        return false;
      }

      // 如果有缓冲区空间
      if (chan->capacity_ > 0 && chan->buffer_.size() < chan->capacity_) {
        chan->buffer_.push_back(std::move(value));
        return false;
      }

      // 否则挂起
      chan->send_waiters_.push_back({h, std::move(value)});
      return true;
    }

    void await_resume() {
      if (chan->closed_) {
        throw std::runtime_error("send on closed channel");
      }
    }
  };

  // 接收操作的可等待对象
  struct RecvAwaiter {
    Channel *chan;
    std::optional<T> result;

    bool await_ready() noexcept {
      std::lock_guard lock(chan->mutex_);
      return !chan->buffer_.empty() || chan->closed_;
    }

    bool await_suspend(std::coroutine_handle<> h) noexcept {
      std::lock_guard lock(chan->mutex_);

      // 如果缓冲区有数据
      if (!chan->buffer_.empty()) {
        result = std::move(chan->buffer_.front());
        chan->buffer_.pop_front();

        // 唤醒等待的发送者
        if (!chan->send_waiters_.empty()) {
          auto [send_handle, send_value] =
              std::move(chan->send_waiters_.front());
          chan->send_waiters_.pop_front();
          chan->buffer_.push_back(std::move(send_value));
          CoroutinePool::instance().schedule(send_handle);
        }
        return false;
      }

      // 如果通道已关闭
      if (chan->closed_) {
        result = std::nullopt;
        return false;
      }

      // 否则挂起等待
      chan->recv_waiters_.push_back({h, &result});
      return true;
    }

    std::optional<T> await_resume() { return result; }
  };

  SendAwaiter send(T value) { return {this, std::move(value)}; }

  RecvAwaiter recv() { return {this}; }

  void close() {
    std::lock_guard lock(mutex_);
    closed_ = true;

    // 唤醒所有等待的接收者
    for (auto [h, ptr] : recv_waiters_) {
      *ptr = std::nullopt;
      CoroutinePool::instance().schedule(h);
    }
    recv_waiters_.clear();

    // 发送者会在恢复时抛出异常
    for (auto [h, _] : send_waiters_) {
      CoroutinePool::instance().schedule(h);
    }
    send_waiters_.clear();
  }

  bool is_closed() const {
    std::lock_guard lock(mutex_);
    return closed_;
  }

private:
  mutable std::mutex mutex_;
  size_t capacity_;
  std::deque<T> buffer_;
  std::deque<std::pair<std::coroutine_handle<>, T>> send_waiters_;
  std::deque<std::pair<std::coroutine_handle<>, std::optional<T> *>>
      recv_waiters_;
  bool closed_;
};

// ==================================================================================
// Select - 多路复用
// ==================================================================================

template <typename... Cases> class Select {
public:
  struct Awaiter {
    std::tuple<Cases...> cases;
    size_t selected_index = SIZE_MAX;

    bool await_ready() noexcept { return try_select(); }

    void await_suspend(std::coroutine_handle<> h) {
      // 简化实现：轮询检查（实际应该注册到各个case）
      CoroutinePool::instance().add_timer(h, std::chrono::steady_clock::now() +
                                                 std::chrono::milliseconds(1));
    }

    size_t await_resume() {
      if (selected_index == SIZE_MAX) {
        try_select();
      }
      return selected_index;
    }

  private:
    bool try_select() {
      // 简化实现：按顺序检查每个case
      size_t index = 0;
      auto check = [&](auto &c) {
        if (c.ready()) {
          selected_index = index;
          return true;
        }
        index++;
        return false;
      };

      return std::apply([&](auto &...c) { return (check(c) || ...); }, cases);
    }
  };

  explicit Select(Cases... cases) : cases_(std::move(cases)...) {}

  Awaiter operator co_await() { return {std::move(cases_)}; }

private:
  std::tuple<Cases...> cases_;
};

// Select case 类型
template <typename Awaitable, typename Handler> struct Case {
  Awaitable awaitable;
  Handler handler;

  bool ready() const {
    // 简化：需要根据具体 Awaitable 类型判断
    return false;
  }
};

template <typename Awaitable, typename Handler>
auto case_(Awaitable &&aw, Handler &&h) {
  return Case<std::decay_t<Awaitable>, std::decay_t<Handler>>{
      std::forward<Awaitable>(aw), std::forward<Handler>(h)};
}

// ==================================================================================
// 辅助工具
// ==================================================================================

// 睡眠
struct SleepAwaiter {
  std::chrono::steady_clock::time_point when;

  bool await_ready() const noexcept {
    return std::chrono::steady_clock::now() >= when;
  }

  void await_suspend(std::coroutine_handle<> h) const noexcept {
    CoroutinePool::instance().add_timer(h, when);
  }

  void await_resume() const noexcept {}
};

inline auto co_sleep_for(std::chrono::steady_clock::duration duration) {
  return SleepAwaiter{std::chrono::steady_clock::now() + duration};
}

// 启动协程
template <typename T> void go_co(Task<T> &&task) {
  CoroutinePool::instance().schedule(task.h_);
  task.h_ = nullptr;
}

template <typename Func, typename... Args>
void go_co(Func &&func, Args &&...args) {
  auto task =
      std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
  CoroutinePool::instance().schedule(task.h_);
  task.h_ = nullptr;
}

// ==================================================================================
// WaitGroup - 等待一组协程完成
// ==================================================================================

class WaitGroup {
public:
  void add(int delta = 1) {
    counter_.fetch_add(delta, std::memory_order_relaxed);
  }

  void done() {
    if (counter_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      std::lock_guard lock(mutex_);
      for (auto h : waiters_) {
        CoroutinePool::instance().schedule(h);
      }
      waiters_.clear();
    }
  }

  struct WaitAwaiter {
    WaitGroup *wg;

    bool await_ready() const noexcept {
      return wg->counter_.load(std::memory_order_acquire) == 0;
    }

    void await_suspend(std::coroutine_handle<> h) {
      std::lock_guard lock(wg->mutex_);
      if (wg->counter_.load(std::memory_order_acquire) == 0) {
        CoroutinePool::instance().schedule(h);
      } else {
        wg->waiters_.push_back(h);
      }
    }

    void await_resume() const noexcept {}
  };

  WaitAwaiter wait() { return {this}; }

private:
  std::atomic<int> counter_{0};
  std::mutex mutex_;
  std::vector<std::coroutine_handle<>> waiters_;
};

// ==================================================================================
// Mutex - 协程互斥锁
// ==================================================================================

class CoMutex {
public:
  struct LockAwaiter {
    CoMutex *mtx;

    bool await_ready() const noexcept { return mtx->try_lock(); }

    void await_suspend(std::coroutine_handle<> h) {
      std::lock_guard lock(mtx->mutex_);
      if (mtx->try_lock()) {
        CoroutinePool::instance().schedule(h);
      } else {
        mtx->waiters_.push_back(h);
      }
    }

    void await_resume() const noexcept {}
  };

  bool try_lock() {
    bool expected = false;
    return locked_.compare_exchange_strong(
        expected, true, std::memory_order_acquire, std::memory_order_relaxed);
  }

  LockAwaiter lock() { return {this}; }

  void unlock() {
    std::lock_guard lock(mutex_);
    locked_.store(false, std::memory_order_release);
    if (!waiters_.empty()) {
      auto h = waiters_.front();
      waiters_.pop_front();
      CoroutinePool::instance().schedule(h);
    }
  }

private:
  std::atomic<bool> locked_{false};
  std::mutex mutex_;
  std::deque<std::coroutine_handle<>> waiters_;
};

} // namespace lyf