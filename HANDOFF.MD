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
