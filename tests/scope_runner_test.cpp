#include "assert.hpp"
#include "utility.hpp"
#include <atomic>
#include <chrono>
#include <thread>

using namespace SimpleWeb;

int main() {
  // Independent runners must not share local callback state.
  ScopeRunner runner_a;
  ScopeRunner runner_b;
  auto lock_a = runner_a.continue_lock();
  auto lock_b = runner_b.continue_lock();
  ASSERT(lock_a != nullptr && lock_b != nullptr);
  runner_b.stop();
  ASSERT(runner_a.continue_lock() != nullptr);
  lock_b.reset();
  lock_a.reset();

  // stop() from the current callback must cancel without waiting for itself.
  ScopeRunner self_stop;
  auto self_lock = self_stop.continue_lock();
  ASSERT(self_lock != nullptr);
  self_stop.stop();
  ASSERT(self_stop.continue_lock() == nullptr);
  self_lock.reset();

  // An external stop remains a barrier for a lock held by another thread.
  ScopeRunner external_stop;
  std::atomic<bool> release(false);
  std::atomic<bool> entered(false);
  std::thread holder([&] {
    auto lock = external_stop.continue_lock();
    entered.store(true);
    while(!release.load())
      std::this_thread::yield();
  });
  while(!entered.load())
    std::this_thread::yield();

  std::atomic<bool> stop_returned(false);
  std::thread stopper([&] {
    external_stop.stop();
    stop_returned.store(true);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  ASSERT(!stop_returned.load());
  release.store(true);
  holder.join();
  stopper.join();
  ASSERT(stop_returned.load());
  ASSERT(external_stop.continue_lock() == nullptr);

  // Exercise nested A -> B -> A tracking and TLS cleanup on thread exit.
  for(int iteration = 0; iteration < 200; ++iteration) {
    std::thread worker([] {
      ScopeRunner a;
      ScopeRunner b;
      auto a1 = a.continue_lock();
      auto b1 = b.continue_lock();
      auto a2 = a.continue_lock();
      ASSERT(a1 != nullptr && b1 != nullptr && a2 != nullptr);

      // Remove the middle node to exercise genuinely non-LIFO unlinking.
      b1.reset();
      a.stop();
      ASSERT(a.continue_lock() == nullptr);

      a2.reset();
      a1.reset();
    });
    worker.join();
  }
}
