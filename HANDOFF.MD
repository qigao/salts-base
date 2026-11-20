# Native thread runtime verification

## Reproducing short thread handoffs on ARM64

- ARM64 untimed condition waiters can park on a native SRW lock and condition
  variable scoped to the selected waiter. Compare the Salts and direct SRW/CV
  rows over four alternating pairs at each affinity. The earlier keyed-event
  rows below provide the pre-change reference. The delayed handoff and pthread
  rows still run once at each affinity.
- In the same CTest job, `placement=shared` lets both threads run on the two
  CPUs allowed by the process mask. `placement=split` pins the two threads to
  separate logical CPUs from that same mask. Use the four `trace=0` runs for
  throughput and percentile comparisons. The `trace=1` runs instrument one
  trip in eight, with sample pages committed before warmup. The phase
  percentile bands use instrumented trips and report their sample counts.
  Request and reply intervals partition the trip. The call
  phases are clipped to that trip and can overlap across threads. The
  condition wait phase includes rescheduling and predicate mutex reacquisition.
  The start row identifies the active workload if CTest stops before its summary.
- The ARM64 predicate-mutex auto-reset event experiment passed the full matrix.
  On the same two-CPU split workload, the LLVM-MinGW median P99.9 rose from
  19.1 µs to 13.0 ms while MSVC dropped from 7.05 ms to 28.0 µs. The experiment
  was reverted. The mutex continues to use keyed events, and the measured tail
  interval still includes the native condition wait and mutex reacquisition.
  [Keyed baseline](https://github.com/qigao/salts-base/actions/runs/44757625305)
  and [auto-reset event experiment](https://github.com/qigao/salts-base/actions/runs/44759495028).
- On Windows, count the initiating thread's committed keyed waits separately
  from notifications observed before parking. Use consecutive reply marks
  under the caller mutex to define a complete trip. Time one in eight trips
  separately from the counter-only runs, and select the P98.5–P99.5 band by
  full-trip duration before averaging phase times. A mean over all samples
  above P99 also includes rare scheduler stalls. The process affinity mask
  specifies the allowed logical processors for both threads.
- With two allowed processors and immediate replies, 91.8% and 94.4% of
  initiator waits committed to the kernel in two ARM64 jobs. In the second
  job, 278 sampled trips fell in the near-P99 band over three runs. Medians
  of the per-run means were 14.52 µs per trip, including 8.59 µs in the
  keyed wait, 4.01 µs releasing the peer's key, 0.35 µs in
  `SwitchToThread`, and 0.04 µs reacquiring the caller mutex. These are
  elapsed intervals on the initiating thread. With one allowed processor,
  the counter-only runs yielded to a runnable thread almost every time and
  almost all notifications arrived before parking.
  [First Windows log](https://github.com/qigao/salts-base/actions/runs/44275841956/job/134704399346)
  and [repeat with phase samples](https://github.com/qigao/salts-base/actions/runs/44277383560/job/134709248330).
- In three paired runs without per-trip probes, increasing ARM64 pre-yield
  acquire loads from 128 to 512, 2,048 and 8,192 gave two-processor P99
  medians of 13.9, 14.4, 17.8 and 13.7 µs. P99.9 rose from 17.3 µs at
  128 loads to 14.0 ms at 8,192. The one-processor P99 rose from 1.8 to
  6.7 µs. Keep these controls when evaluating future changes to parking.
- To reproduce the diagnostic rows, use verification revision
  [3a372d7](https://github.com/qigao/salts-base/commit/3a372d7) with the
  job's configure and build presets. Run `ctest --preset
  ci-llvm-mingw-arm64-release-user -R win_threads_benchmark -V` or
  `ctest --preset ci-llvm-native-release-user -R posix_threads_benchmark -V`.
  The verification job keeps the temporary counters and alternate policies
  out of the shipped runtime.

- For Darwin, instrument `dispatch_semaphore_signal` on mutex release and count
  its nonzero results per thread. Read the counters after join. Run the same
  `mtx_lock` / increment / `mtx_unlock` loop with a raw dispatch semaphore,
  a plain C11 mutex, and a pthread mutex. Rotate their order across runs. In
  four two-worker runs of 200,000 acquisitions each, the timed-capable C11
  mutex woke a waiter 378,912 times (47.4%). The median of its per-run means
  was 1,951 ns/acquisition, compared with 1,232 ns for raw dispatch and 43 ns
  for plain C11. Both semaphore cases recorded zero wakes with one worker.
  This isolates contention in ordinary lock/unlock on a timed-capable mutex.
  The benchmark takes no deadline in that loop. [macOS CTest log](https://github.com/qigao/salts-base/actions/runs/44275841956/job/134704399032)
  and [Apple signal return contract](https://github.com/apple-oss-distributions/libdispatch/blob/main/dispatch/semaphore.h#L90).
  In a second job, the two timed-capable runs with 0.4–3.7% wakes measured
  107–146 ns/acquisition. The two runs with 47–50% wakes measured
  1,915–2,267 ns/acquisition. [Repeat macOS log](https://github.com/qigao/salts-base/actions/runs/44277383560/job/134709247505).

- For the Darwin timed mutex follow-up, compare the same
  `saltscore.posix_threads_benchmark` on the old and new providers. The
  `Darwin mutex` rows rotate pthread, plain C11 and timed C11 across four
  repeats and sample acquisition latency. `hold=1` sleeps briefly inside
  every sixteenth critical section. `mixed=1` alternates ordinary and timed
  acquisitions. The held-lock workload exercises parking pressure without
  instrumenting the production wake path.
- A dispatch semaphore wait can enter an unbounded drain after an expired
  or zero timeout when a signal has incremented the counter and paused before
  the kernel wake. Darwin timed and platform mutexes now keep ownership in an
  atomic word and reserve at most one Mach semaphore notification. The C11
  facade CTest pauses between reservation and the native signal to verify that
  try-lock and timeout can complete and the next sleeper can still progress.
  [libdispatch drain](https://github.com/apple-oss-distributions/libdispatch/blob/main/src/semaphore.c#L108).

## Hosted validation and deadline edge cases

The parser registration fix in PR #23 lets the normal presets build the full
graph from this tree. The Windows ARM64 CI lane runs native executables and
CTest on `windows-11-arm`. The earlier local results below describe the base
revision and successive handoff experiments.

- To run the Android thread CTests on a device, enable `BUILD_TESTS` in a
  verification checkout of the matching CI preset. Set
  `CMAKE_CROSSCOMPILING_EMULATOR` to an adb runner that stages the target
  executable, runs it with its matching `libsalts.so` and propagates the
  remote exit status. On the API 35 x86_64 image, the ARM64 native bridge
  reported a missing `libsalts.so` even with the device directory in
  `LD_LIBRARY_PATH`. Linking the verification executables with
  `-Wl,--enable-new-dtags,-rpath,<device-directory>` let the translated
  linker resolve the same staged library. Keep that device path local to
  verification. The seven Core thread and atomic CTests each passed three
  times on API 26 x86_64, API 35 x86_64 and API 35 translated ARM64.

- If only Linux TSan times out in the C11 initializer-exit case, inspect its
  `pthread_once` interceptor. Clang 18 marks completion after the callback
  returns and leaves the flag running on `pthread_exit`. The instrumented
  build uses the same cleanup-aware pthread monitor as Android and Darwin.
  Run the normal C11 facade CTest in the existing TSan preset to exercise
  the exiting caller and all subsequent callers.
  [compiler-rt interceptor](https://github.com/llvm/llvm-project/blob/llvmorg-18.1.8/compiler-rt/lib/tsan/rtl/tsan_interceptors_posix.cpp).

- On the dispatch-backed path, a Darwin wait can hang if an expired C11
  deadline at epoch zero is passed to `dispatch_walltime`. Epoch nanoseconds
  zero and one map to the forever sentinel. Use an immediate acquire for those
  dates. Wall times also have a 62-bit nanosecond limit. A larger date can
  decode as the continuous clock. For such dates the dispatch path sleeps on
  its semaphore with a finite timeout and rechecks UTC on expiration. The C11
  threads CTest covers expiry and a contended year-2262 deadline, and the
  portable timed mutex CTest covers monotonic durations.
  [libdispatch time encoding](https://github.com/apple-oss-distributions/libdispatch/blob/main/src/shims/time.h)
  and [wall-time conversion](https://github.com/apple-oss-distributions/libdispatch/blob/main/src/time.c).
- A timed Windows mutex waiter that receives the only successful keyed wake
  must attempt ownership before checking the clock again. Otherwise the lock
  can remain free while a second waiter sleeps indefinitely. Exercise a timed
  and an untimed contender on the same held mutex in
  `saltscore.c11_threads_facade`. The queue guard and user mutex use the same
  acquisition protocol.

## Windows thread runtime verification on 8cb71f1

- The original tree fails configure in `parser/utils` at the missing
  `ini_parser` directory and in `parser/data` at the missing
  `cyaml_json_adapter` target. For thread verification an isolated worktree
  used the complete parent revision's parser registration with subdirectory
  paths updated to the moved modules. It used the original build's verified
  host tool receipts. Copying the receipts into a different binary directory
  fails the shared restore CLI's absolute-root check.
- Load the machine environment before using the user presets. Configure with
  `BUILD_TESTS=ON`, `BUILD_BENCHMARKS=ON`, the selected triplet's restored
  installation tree and the test Python interpreter. Build the complete
  `llvm-mingw-x64-release-user` graph and run `ctest --preset
  llvm-mingw-x64-release-user -LE benchmark --output-on-failure`. For the
  contention measurements run the same CTest preset with
  `-R saltscore.win_threads_benchmark -V --repeat until-fail:3`.
- On this x64 host the final non-benchmark run passed 171/171. MSVC x64
  passed six thread CTests five times each. Clang-cl x64 passed the same six
  with a temporary MSVC-compatible preset. The Windows ARM64 SDK was absent
  from the local ARM64 preset's installation tree. Native ARM64 execution
  remains unqualified.
- Compare the original and revised Core with the same benchmark executable
  sources, build flags and affinity. The table gives medians across three
  LLVM-MinGW x64 runs. Each mutex workload lasts 350 ms on at most two logical
  processors. Fairness is the least/most successful worker's acquisition count
  ratio. One QPC tick on this host is 100 ns.

| Workload | Previous | Revised |
| --- | ---: | ---: |
| 1 worker, million acquisitions/s | 17.51 | 19.18 |
| 2 workers, million acquisitions/s | 8.22 | 18.45 |
| 8 workers, million acquisitions/s | 7.76 | 15.08 |
| 8 workers, P99 acquisition (µs) | 3.0 | 0.3 |
| 8 workers, context switches | 1,076 | 1,806 |
| 8 workers, fairness ratio | 0.321 | 0.252 |
| Condition round trips/s | 1,542,750 | 2,086,283 |
| Condition exchange P99 (µs) | 0.9 | 7.6 |

- The early-signal path publishes to the waiter's stack record before it
  parks. This avoids making the signaler rendezvous in the kernel with a
  thread still releasing its mutex. The measured private pointer reads were
  1.24 ns with compiler TLS and 2.18 ns with the allocated TEB slot. Keep
  the compiler TLS default. Full percentile, CPU, switch and per-worker
  results, including direct SRW and winpthreads baselines, are in
  `build/winthreads-old-bench.log` and `build/winthreads-final-bench.log`.
- The condition percentiles time the initiating thread's single `exchange`.
  Round trips/s uses both threads' completed loop and the join. In five
  temporary branch-flag runs, 203 of the 205 slowest-one-percent samples
  entered the condition keyed wait or keyed release, versus 4,371/20,275
  other samples. Five separate timestamped runs found that the interval
  inside the condition keyed wait was the longest phase in 173/205 tail
  samples. Reacquisition was longest in 7/205. Instrumentation changes the
  handoff timing. Use these runs for attribution and the uninstrumented
  runs for latency. See `build/cv-tail-flags-ctest.log` and
  `build/cv-tail-stage-ctest.log`. Only the verification worktree was
  instrumented.

## Condition handoff after the initial refactor

- Use the same benchmark sources and affinity for both builds. The original
  4,096-sample benchmark timed only the initiating thread's exchange. The
  revised benchmark also timestamps the point under the mutex where the
  initiator observes a reply and issues the next request. Consecutive marks
  include the responder's turn and scheduling gaps between caller exchanges.
  It warms up for 1,024 immediate handoffs and measures 65,536 more per run.
- The baseline below is the initial keyed-event implementation with 32
  interlocked polls. The revised policy checks for an early signal, yields
  once, then polls with acquire loads before committing to the NT wait.
  A thread whose kernel wait crossed a system tick skips that speculation on
  its next untimed wait. The clock is sampled only around a committed untimed
  kernel wait. An unconditional 128-poll policy had increased waiter cycles
  for delayed replies during the experiments.
- Medians of five runs per build on this Windows x64 host (LLVM-MinGW Release).
  The baseline and revised binaries ran in alternating groups with the
  same CTest condition workload. CPU counts are allowed logical processors.

| Immediate reply | 1 CPU baseline | 1 CPU revised | 2 CPUs baseline | 2 CPUs revised |
| --- | ---: | ---: | ---: | ---: |
| Complete round-trip P99 (µs) | 6.0 | 2.1 | 7.0 | 0.6 |
| Complete round-trip P99.9 (µs) | 87.1 | 14.8 | 22.5 | 5.7 |
| Caller exchange P50 (µs) | 3.7 | 1.3 | 0.1 | 0.2 |
| Caller exchange P99 (µs) | 5.9 | 2.1 | 6.9 | 0.5 |
| Round trips/s | 247,037 | 693,253 | 323,821 | 1,363,504 |
| Process cycles/trip | 7,714 | 2,875 | 1,381 | 985 |

- With `Sleep(1)` before each reply (8 warmups, 256 measured trips), the
  median rate was 64 trips/s on both builds at both affinities. Waiting
  thread cycles/trip went from 21,055 to 17,561 on one CPU and from 19,436
  to 19,788 on two. Total process cycles/trip went from 65,166 to 55,839
  and from 65,354 to 71,084 respectively. The delay follows the host timer
  resolution. The benchmark does not establish a latency bound.
- Reproduce with `ctest --preset llvm-mingw-x64-release-user -R
  '^saltscore.win_threads_benchmark$' -V` after building
  `benchmark_win_threads`. The local comparison logs are
  `build/cv-tune-delivery-*-base.log` and `build/cv-tune-delivery-*-new.log`.
- After this change the full LLVM-MinGW x64 build passed all 171 non-benchmark
  CTests. MSVC and clang-cl x64 each passed all six thread CTests five times.
  The checks used the isolated parser registration described above. ARM64
  execution remains unverified.
- For the Windows FIFO contract, record tickets while holding the user mutex
  immediately before re-enrollment. Wait for one reply per signal before
  checking the ticket order. The initial broadcast runs without the user
  mutex so its recipients can re-enroll during notification. In an isolated
  negative control, selecting the queue tail passed the original count-only
  test but returned ticket 5 where the revised test expected 0. Retaining one
  broadcast credit for a future waiter produced one completion before the
  first signal. Both controls are in the local `build/cv-selection-reject-*.log`
  files. Run the permanent regression with `ctest --preset
  llvm-mingw-x64-release-user -R '^saltscore.c11_threads_facade$' --repeat until-fail:20`.

# Thread-pool admission investigation

- CHttp #286's deferred CPU gate received HTTP 503, not a lost HTTP success.
  The raw benchmark had waited for an ok body and converted that explicit
  rejection into a timeout. Temporary Server tracing observed matching request/
  response counts, zero handler/protocol errors and one worker admission error.
  Snapshot: ENOBUFS (-4027), queued=1, active=0, capacity=16. Trace logs remain in
  http-services/build/migration-deferred-admission-trace-ctest.log; temporary
  trace code/branch is removed from the product checkout.
- Source evidence: ordinary threadpool_try_submit used Disruptor's one-shot
  publisher try-claim, whose CAS contention returns false just like capacity
  exhaustion. The pool mapped either case to ENOBUFS. Its public contract reserves
  that status for a full queue. Four concurrent producers with one unsettled task
  each and capacity 16 reproduce this in the existing formal thread-pool suite:
  migration-threadpool-admission-red-formal-ctest.log, expected OK, got -4027.
- Reuse dispatch_mutex for the fixed-size claim/copy/publish transfer. No queue
  wait or callback runs under it; the existing blocking path releases it before
  waiting. Preserve real-full rejection, lifecycle, shutdown and LF behavior.
  The regression passes in the full build/CTest run. That run passed 164/165;
  its sole failure was the default Python missing the declared cmarkgfm oracle.
  The documented test virtualenv is selected explicitly and its pinned oracle
  requirements restored. The remaining formal oracle check passes 1/1, 12.66s
  (migration-threadpool-oracle-tests-ctest.log). The Release preset resets
  BUILD_TESTS=OFF on each configure, so explicitly retain BUILD_TESTS=ON when
  selecting Python3_EXECUTABLE; otherwise stale CTest files still name the old
  interpreter. Preserve the intermediate failed oracle log as configuration
  evidence, not a parser failure. All 165 formal tests have now been exercised
  successfully across the full run and the corrected oracle-only run.
- Local verification uses llvm-mingw-x64-release-user, the existing configured
  profile. An initial native-release-user build failed because that build cache
  had no CMAKE_PROJECT_NAME; retain migration-threadpool-admission-red-build.log.
  No product SDK/compiler fallback was added. No published SDK version or asset
  is changed by this source branch; downstream CHttp still uses released inputs.

# Root CMake and example verification

- `BUILD_TESTS=ON` does not compile example branches. Verify example CMake edits
  through the existing root preset with `-DBUILD_EXAMPLES=ON`; retain explicit
  `-DBUILD_TESTS=ON` because the Release preset resets tests to OFF.
- This exposed the stale `<core/cron.h>` include in Cron's example. Its owner is
  `cron/include/cron.h`, exposed as `<cron.h>`; fix the source include instead of
  adding a compatibility include path. The failed build is preserved in
  `build/cmake-cleanup-examples-build.log`.
- `llvm-mingw-x64-release-user` passes the complete build and all 170 CTest
  cases (`build/cmake-cleanup-ctest.log`). The complete example-enabled build
  passes after the include fix (`build/cmake-cleanup-examples-fixed-build.log`),
  followed by the formal Cron test passing 1/1. The local example setting is
  restored to OFF after verification; no extra validation project is created.

- Installing the independently qualified local SDK through
  `install-llvm-mingw-x64-release-user` exposed a bare `COMMAND mib_compiler`
  in the ASN.1 fixture generator: that executable was not found through PATH.
  Use `$<TARGET_FILE:mib_compiler>` with the existing target dependency so the
  formal graph invokes its declared executable directly. The install then
  passes (`build/local-sdk-fe1e96c-fixed-install.log`) and the formal
  `asn1.mib_generated` CTest passes 1/1.
