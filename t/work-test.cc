// SPDX-FileCopyrightText: 2026 Edward Kmett <ekmett@gmail.com>
// SPDX-License-Identifier: BSD-2-Clause OR Apache-2.0
#include <new>
#include <array>
#include <atomic>
#include <barrier>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <semaphore>
#include <source_location>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>
import work;

void check(bool condition, std::source_location where = std::source_location::current()) {
  if (!condition) { std::fprintf(stderr, "%s:%u\n", where.file_name(), where.line()); std::abort(); }
}
int main() {
  // Exactly once, including descendants submitted after external close.
  for (auto width : {1u, 2u, 4u}) {
    work::pool pool{width};
    for (unsigned round = 0; round != 8; ++round) {
      constexpr unsigned count = 32767;
      std::vector<std::atomic<unsigned>> seen(count + 1);
      auto g = pool.gig<unsigned>([&](unsigned value, work::context<unsigned> & children) noexcept {
        check(value && value <= count);
        check(seen[value].fetch_add(1, std::memory_order_relaxed) == 0);
        if (value * 2 <= count) { children.push(value * 2); children.push(value * 2 + 1); }
      });
      g.push(1); g.close(); g.join(); g.join();
      for (unsigned value = 1; value <= count; ++value) check(seen[value] == 1);
      bool rejected = false;
      try { g.push(1); } catch (std::logic_error const &) { rejected = true; }
      check(rejected);
    }
  }
  {
    // Different T and handler types must execute at the same time, before join.
    work::pool pool{3};
    std::barrier rendezvous{2};
    std::atomic<unsigned> total{0};
    auto a = pool.gig<unsigned>([&](unsigned n) noexcept { rendezvous.arrive_and_wait(); total.fetch_add(n); });
    auto b = pool.gig<std::unique_ptr<unsigned>>([&](std::unique_ptr<unsigned> n) noexcept {
      rendezvous.arrive_and_wait(); total.fetch_add(*n);
    });
    a.push(3); b.push(std::make_unique<unsigned>(4));
    a.close(); b.close(); a.join(); b.join(); check(total == 7);
  }
  {
    // Idle is not done while external submissions remain open.
    work::pool pool{4};
    std::atomic<unsigned> count{0};
    auto g = pool.gig<unsigned>([&](unsigned n) noexcept { count.fetch_add(n); count.notify_all(); });
    for (unsigned i = 1; i != 101; ++i) {
      g.push(1);
      auto observed = count.load();
      while (observed != i) { count.wait(observed); observed = count.load(); }
    }
    g.close(); g.join(); check(count == 100);
  }
  {
    // close races admission; only accepted tasks run, and all of them finish.
    for (unsigned round = 0; round != 32; ++round) {
      work::pool pool{4};
      std::atomic<unsigned> accepted{0}, executed{0};
      auto g = pool.gig<unsigned>([&](unsigned) noexcept { executed.fetch_add(1); });
      std::barrier start{5};
      std::vector<std::jthread> producers;
      for (unsigned i = 0; i != 4; ++i) producers.emplace_back([&] {
        start.arrive_and_wait();
        for (unsigned n = 0; n != 500; ++n) {
          try { g.push(n); accepted.fetch_add(1); }
          catch (std::logic_error const &) { break; }
        }
      });
      start.arrive_and_wait(); g.close();
      producers.clear(); g.join(); check(accepted == executed);
    }
  }
  {
    // A handler can join a different gig even when the pool has no spare worker.
    work::pool pool{1};
    unsigned total = 0;
    auto outer = pool.gig<unsigned>([&](unsigned n) noexcept {
      check(pool.available_workers() == 1);
      auto inner = pool.gig<unsigned>([&](unsigned x) noexcept { total += x; });
      inner.push(n); inner.close(); inner.join();
    });
    outer.push(42); outer.join(); check(total == 42);
  }
  {
    // Explicit caller-only mode is needed for callbacks on registered VM threads.
    work::pool pool{4};
    auto const caller = std::this_thread::get_id();
    unsigned count = 0;
    auto g = pool.gig<unsigned>([&](unsigned n, work::context<unsigned> & children) noexcept {
      check(std::this_thread::get_id() == caller); ++count;
      if (n) children.push(n - 1);
    }, {.caller_only = true});
    g.push(1000); g.close(); check(count == 0); g.join(); check(count == 1001);
  }
  {
    // Batch admission and destruction's implicit join publish ordinary writes.
    work::pool pool{4};
    std::array<unsigned, 1024> input{}, output{};
    for (unsigned i = 0; i != input.size(); ++i) input[i] = i;
    {
      auto g = pool.gig<unsigned>([&](unsigned n) noexcept { output[n] = n + 1; });
      g.push(std::span<unsigned const>{input});
    }
    for (unsigned i = 0; i != output.size(); ++i) check(output[i] == i + 1);
    auto empty = pool.gig<unsigned>([](unsigned) noexcept { std::abort(); });
    empty.join();
  }
  {
    // Donation really overlaps descendants with their parent, bounded by two lanes.
    work::pool pool{4};
    std::atomic<unsigned> active{0}, children_started{0};
    auto g = pool.gig<unsigned>([&](unsigned n, work::context<unsigned> & children) noexcept {
      check(active.fetch_add(1) < 2);
      if (!n) {
        for (unsigned i = 1; i != 101; ++i) children.push(i);
        while (!children_started.load()) { children.poll(); std::this_thread::yield(); }
      } else children_started.fetch_add(1);
      active.fetch_sub(1);
    }, {.concurrency = 2});
    g.push(0); g.join(); check(children_started == 100 && active == 0);
  }
  {
    work::pool pool{4};
    std::atomic<unsigned> count{0};
    auto g = pool.gig<unsigned>([&](unsigned n, work::context<unsigned> & children) noexcept {
      count.fetch_add(1);
      if (n) children.push(n - 1);
    }, {.caller_only = true});
    g.push(10000);
    std::barrier start{3};
    std::jthread a([&] { start.arrive_and_wait(); g.join(); });
    std::jthread b([&] { start.arrive_and_wait(); g.join(); });
    start.arrive_and_wait(); a.join(); b.join(); check(count == 10001);
  }
  {
    // Helping B from A.join may enter another A.join without A being an ancestor.
    work::pool pool{1};
    unsigned total = 0;
    auto a = pool.gig<unsigned>([&](unsigned n) noexcept { total += n; });
    auto b = pool.gig<unsigned>([&](unsigned) noexcept { a.join(); });
    b.push(0); a.push(42); a.join(); b.join(); check(total == 42);
  }
  {
    // Batch scopes end before join publishes completion; descendants cross many quanta.
    work::pool pool{3};
    std::atomic<unsigned> live_scopes{0}, batches{0}, count{0};
    auto g = pool.gig<unsigned>([&](work::context<unsigned> & queue) noexcept {
      struct guard {
        std::atomic<unsigned> & live;
        explicit guard(std::atomic<unsigned> & n) noexcept : live(n) { live.fetch_add(1); }
        ~guard() { live.fetch_sub(1); }
      } scope{live_scopes};
      batches.fetch_add(1);
      unsigned n;
      while (queue.pop(n)) {
        count.fetch_add(1);
        if (n) queue.push(n - 1);
      }
    });
    g.push(20000); g.join();
    check(count == 20001 && batches > 1 && live_scopes == 0);
  }
  {
    // Joining A must return once A finishes, even while helping an open B.
    work::pool pool{2};
    std::binary_semaphore started{0}, release{0};
    std::atomic<bool> stop{false};
    auto a = pool.gig<unsigned>([&](unsigned) noexcept {
      started.release(); release.acquire();
    });
    a.push(0); started.acquire();
    auto b = pool.gig<unsigned>([&](unsigned n, work::context<unsigned> & queue) noexcept {
      if (!n) release.release();
      if (!stop.load()) queue.push(n + 1);
    });
    b.push(0);
    a.join();
    stop.store(true);
    b.join();
  }
  std::puts("work checks passed");
}
