#define _POSIX_C_SOURCE 200809L
/* test_c11_threads.c owns the runner for this shared test executable. */
#define TINYTEST_NO_MAIN 1
#include <tinytest.h>
#include <threads/threads.h>
#include <signal.h>
#include <unistd.h>

static volatile sig_atomic_t alarm_seen;
static void sleep_alarm(int signo) {
    (void)signo;
    alarm_seen = 1;
}

spec("C11 POSIX sleep interruption") {
    it("reports interruption and the unelapsed duration") {
        struct sigaction action = {0}, previous;
        struct timespec duration = {3, 0}, remaining = {0, 0};
        int result;
        action.sa_handler = sleep_alarm;
        sigemptyset(&action.sa_mask);
        alarm_seen = 0;
        check_equal(sigaction(SIGALRM, &action, &previous), 0);
        alarm(1);
        result = thrd_sleep(&duration, &remaining);
        alarm(0);
        check_equal(sigaction(SIGALRM, &previous, NULL), 0);
        check_equal(result, -1);
        check_equal((int)alarm_seen, 1);
        check(remaining.tv_sec >= 0 && remaining.tv_sec < duration.tv_sec);
        check(remaining.tv_sec != 0 || remaining.tv_nsec != 0);
        check(remaining.tv_nsec >= 0 && remaining.tv_nsec < 1000000000L);
    }
}
