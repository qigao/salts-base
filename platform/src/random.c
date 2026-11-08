#include <error_codes.h>
#include <random.h>

#include <errno.h>
#include <limits.h>
#include <stdint.h>

#if defined(_WIN32)
// clang-format off
  #include <windows.h>
  #include <bcrypt.h>
// clang-format on
#elif defined(__ANDROID__)
  #include <stdlib.h>
#elif defined(__linux__)
  #include <sys/random.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || \
    defined(__DragonFly__)
  #include <stdlib.h>
#else
  #include <fcntl.h>
  #include <unistd.h>
#endif

enum { SALTS_PLATFORM_RANDOM_CHUNK_BYTES = 256 };

int cmeta_platform_secure_random(void *buffer, size_t length) {
  uint8_t *cursor = (uint8_t *)buffer;
  if (length == 0u) return SALTS_OK;
  if (buffer == NULL) return SALTS_EINVAL;
#if defined(_WIN32)
  while (length != 0u) {
    const ULONG chunk = length > (size_t)ULONG_MAX ? ULONG_MAX : (ULONG)length;
    const NTSTATUS status = BCryptGenRandom(NULL, cursor, chunk, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!BCRYPT_SUCCESS(status)) return SALTS_EIO;
    cursor += chunk;
    length -= chunk;
  }
#elif defined(__ANDROID__) || defined(__APPLE__) || defined(__FreeBSD__) ||                        \
    defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
  arc4random_buf(cursor, length);
#elif defined(__linux__)
  while (length != 0u) {
    const size_t chunk =
        length > SALTS_PLATFORM_RANDOM_CHUNK_BYTES ? SALTS_PLATFORM_RANDOM_CHUNK_BYTES : length;
    const ssize_t received = getrandom(cursor, chunk, 0);
    if (received < 0) {
      if (errno == EINTR) continue;
      return -errno;
    }
    if (received == 0) return SALTS_EIO;
    cursor += (size_t)received;
    length -= (size_t)received;
  }
#else
  {
    int flags = O_RDONLY;
    int descriptor;
  #if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
  #endif
    descriptor = open("/dev/urandom", flags);
    if (descriptor < 0) return -errno;
    while (length != 0u) {
      const size_t chunk =
          length > SALTS_PLATFORM_RANDOM_CHUNK_BYTES ? SALTS_PLATFORM_RANDOM_CHUNK_BYTES : length;
      const ssize_t received = read(descriptor, cursor, chunk);
      if (received < 0) {
        const int error = errno;
        if (error == EINTR) continue;
        (void)close(descriptor);
        return -error;
      }
      if (received == 0) {
        (void)close(descriptor);
        return SALTS_EIO;
      }
      cursor += (size_t)received;
      length -= (size_t)received;
    }
    (void)close(descriptor);
  }
#endif
  return SALTS_OK;
}
