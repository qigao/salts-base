#include <native_io.h>
#include <native_io_accept.h>
#include <native_io_sharded.h>
#include <salts/error_codes.h>
#include <tinytest.h>

static native_io_backend_kind native_backend(void) {
#if defined(_WIN32)
  return NATIVE_IO_BACKEND_IOCP;
#elif defined(__APPLE__)
  return NATIVE_IO_BACKEND_KQUEUE;
#elif defined(__linux__)
  return NATIVE_IO_BACKEND_EPOLL;
#else
  return (native_io_backend_kind)0;
#endif
}

spec("NativeIO backend lifecycle") {
  it("uses the platform native backend without fallback") {
    native_io_backend backend = {0};
    native_io_backend_stats stats = {0};
    native_io_backend_config config = { native_backend(), 2u, 4u, 2u };
    native_io_sharded_endpoint none = {0};
    int status;

    /* A new native platform must implement its owner; never pass vacuously. */
    check(config.kind != 0);
    check(native_io_backend_kind_supported(config.kind));
    check(native_io_backend_kind_model(config.kind) != NATIVE_IO_MODEL_NONE);

    check(!native_io_endpoint_valid((native_io_endpoint){0}));
    check(!native_io_request_valid((native_io_request){0}));
    check(!native_io_sharded_endpoint_valid(none));

    status = native_io_backend_init(&backend, &config);
    check(status == SALTS_OK, "native_io_backend_init status=%d", status);
    check(native_io_backend_get_stats(&backend, &stats));
    check(stats.endpoint_capacity == config.endpoint_capacity);
    check(stats.request_capacity == config.request_capacity);
    check(stats.active_requests == 0u);

    status = native_io_backend_close(&backend);
    check(status == SALTS_OK, "native_io_backend_close status=%d", status);

    status = native_io_backend_destroy(&backend);
    check(status == SALTS_OK, "native_io_backend_destroy status=%d", status);
    check(backend.impl == NULL);
  }
}


spec("NativeIO public accept admission") {
  it("clears invalid admission outputs and never transfers invalid ownership") {
    native_io_backend backend = {0};
    native_io_request request = {1u, 2u};
    uintptr_t transport = 123u;
    check_equal(native_io_internal_submit_stream_accept(NULL,
        (native_io_endpoint){0}, &request), SALTS_EINVAL);
    check_equal(request.slot, 0u);
    check_equal(request.generation, 0u);
    check_equal(native_io_internal_submit_stream_accept(&backend,
        (native_io_endpoint){0}, NULL), SALTS_EINVAL);
    check_equal(native_io_internal_take_stream_accept(&backend,
        (native_io_request){1u, 2u}, &transport), SALTS_EINVAL);
    check_equal(transport, UINTPTR_MAX);
    check_equal(native_io_internal_take_stream_accept(NULL,
        (native_io_request){0}, NULL), SALTS_EINVAL);
  }
}
