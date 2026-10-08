# work

<!-- SPDX-FileCopyrightText: 2026 Edward Kmett <ekmett@gmail.com> -->
<!-- SPDX-License-Identifier: BSD-2-Clause OR Apache-2.0 -->

A pool does the work. A gig is a bunch of related work.

```cpp
#include <new>
#include <atomic>
import work;

int main() {
  work::pool pool{8};
  std::atomic<unsigned> sum{0};
  auto g = pool.gig<unsigned>([&](unsigned n) noexcept {
    sum.fetch_add(n, std::memory_order_relaxed);
  });
  g.push(20);
  g.push(22);
  g.close();
  g.join();
  return sum == 42 ? 0 : 1;
}
```

Make another gig with another task type and handler. They can overlap. The pool
keeps its threads; each gig owns its handler and task storage. `close()` stops
external submissions. `join()` waits for that gig and all its descendants, and
makes their writes visible to the caller. It also closes the gig if necessary.
Destruction joins, so keep things captured by reference alive until then.

For work that discovers more work, take a context:

```cpp
auto g = pool.gig<unsigned>([](unsigned n, work::context<unsigned> & todo) noexcept {
  if (n > 1) {
    todo.push(n / 2);
    todo.push(n - n / 2);
  }
});
g.push(1000);
g.close(); // The accepted tasks can still create children.
g.join();
```

Those child pushes go onto a private typed stack. Active workers occasionally
donate the older half of their backlog to an idle lane. The intervals are
exponentially distributed; deadlines accumulate during execution. Deadlines survive batch boundaries and individual idle lanes; an entirely
idle gig starts a fresh timer period when new work arrives. The pool dispatches batches, so its
indirect call and scheduler lock aren't paid for every task. There is still
synchronization at admission, handoff and completion; this is not a wait-free
scheduler.

A handler may run concurrently with itself. It must be `noexcept`, and tasks
must support nonthrowing move construction, move assignment and destruction.
Allocation failure inside the scheduler terminates. External submission may
throw on allocation, or `std::logic_error` after close. Racing `push()` and
`close()` either accepts the task completely or rejects it. A context belongs
to one callback on one thread; don't save or share it.

The pool width includes a joining caller: `pool{8}` creates seven background
threads. `pool{1}` runs when you join. Additional simultaneous joining callers
can also help execute work. A gig's concurrency is bounded by its lane count.
Joins cooperate, including nested joins of other gigs. Don't join your own gig
from its callback, introduce cyclic dependencies, or block waiting for pool
work when you could join it. Keep the pool alive until all its gigs are gone.
Gigs cannot be copied or moved.

When a batch needs a scope of its own, take just the context. `pop()` stops at
the batch boundary, even if the gig has more work. Returning lets other gigs run.

```cpp
auto g = pool.gig<unsigned>([](work::context<unsigned> & todo) noexcept {
  // Bind a thread-local environment or another RAII scope here.
  unsigned n;
  while (todo.pop(n)) {
    // Process n; todo.push(...) can add descendants.
  }
});
```

A long callback can call `todo.poll()` to donate queued children. It doesn't
preempt the callback. For a foreign runtime that requires callbacks on its
registered thread, pass `{.caller_only = true}` to `gig`; work then starts in
`join()` on that caller. `{.concurrency = n}` limits a normal gig's lanes.

Build with Clang 23+, CMake 4.4+ and Ninja. The only library dependencies are
[Hint](https://github.com/ekmett/hint) and the platform threads library.

```sh
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
cmake --install build --prefix /path/to/work
```

Use `find_package(work CONFIG REQUIRED)` and link `work::work`, or add this
repository with CMake's `FetchContent`. On Windows use `clang-cl` in an MSVC
SDK environment. The installed package includes module sources for consumer
BMI generation.

The work-pushing approach comes from Acar, Charguéraud and Rainey's
[Scheduling Parallel Programs by Work Stealing with Private Deques](https://www.chargueraud.org/research/2013/ppopp/full.pdf),
particularly its sender-initiated scheduler. This implementation uses its own
batch dispatch and completion protocol; it does not inherit the paper's proofs.

## Contact

Edward Kmett <ekmett@gmail.com>
