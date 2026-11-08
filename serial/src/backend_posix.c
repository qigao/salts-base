/*
 * Copyright (c) 2026 Salts Project
 */

#if !defined(_WIN32) && !defined(__CYGWIN__)

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <dirent.h>

#include "serial_internal.h"
#include "tstr.h"

struct salts_port_handle {
  int fd;
  char port_name[256];
  salts_serial_mode_t mode;
};

#define MAX_EVENT_PORTS 64

struct salts_serial_event_set_impl {
  salts_port_handle_t *ports[MAX_EVENT_PORTS];
  unsigned int event_masks[MAX_EVENT_PORTS];
  struct pollfd fds[MAX_EVENT_PORTS];
  size_t count;
};

static speed_t baudrate_to_speed(int baudrate) {
  switch (baudrate) {
  case 50: return B50;
  case 75: return B75;
  case 110: return B110;
  case 134: return B134;
  case 150: return B150;
  case 200: return B200;
  case 300: return B300;
  case 600: return B600;
  case 1200: return B1200;
  case 1800: return B1800;
  case 2400: return B2400;
  case 4800: return B4800;
  case 9600: return B9600;
  case 19200: return B19200;
  case 38400: return B38400;
  case 57600: return B57600;
  case 115200: return B115200;
  case 230400: return B230400;
#ifdef B460800
  case 460800: return B460800;
#endif
#ifdef B500000
  case 500000: return B500000;
#endif
#ifdef B576000
  case 576000: return B576000;
#endif
#ifdef B921600
  case 921600: return B921600;
#endif
#ifdef B1000000
  case 1000000: return B1000000;
#endif
  default: return B115200;
  }
}

static salts_serial_result_t posix_list_ports(salts_serial_port_info_vec_t *vec) {
  DIR *dir;
  struct dirent *entry;

  if (!vec) return SALTS_SERIAL_INVALID_VALUE;

  dir = opendir("/dev");
  if (!dir) return SALTS_SERIAL_OK;

  while ((entry = readdir(dir)) != NULL) {
    if (strncmp(entry->d_name, "ttyUSB", 6) == 0 ||
        strncmp(entry->d_name, "ttyACM", 6) == 0 ||
        strncmp(entry->d_name, "ttyS", 4) == 0 ||
        strncmp(entry->d_name, "cu.", 3) == 0) {
      char full_path[512];
      snprintf(full_path, sizeof(full_path), "/dev/%s", entry->d_name);

      salts_serial_port_info_storage_t storage;
      memset(&storage, 0, sizeof(storage));

      storage.name = tstr_dup(full_path);
      storage.description = tstr_dup(entry->d_name);
      storage.view.name = storage.name;
      storage.view.description = storage.description;

      if (strncmp(entry->d_name, "ttyUSB", 6) == 0 || strncmp(entry->d_name, "ttyACM", 6) == 0) {
        storage.view.transport = SALTS_SERIAL_TRANSPORT_USB;
      } else {
        storage.view.transport = SALTS_SERIAL_TRANSPORT_NATIVE;
      }

      {
        salts_serial_result_t status = salts_serial_port_info_vec_t_push(vec, storage);
        salts_serial_port_info_storage_destroy(&storage);
        if (status != SALTS_SERIAL_OK) {
          closedir(dir);
          return status;
        }
      }
    }
  }

  closedir(dir);
  return SALTS_SERIAL_OK;
}

static salts_serial_result_t posix_get_port_info(const char *port_name,
                                                  salts_serial_port_info_storage_t *storage) {
  if (!port_name || !storage) return SALTS_SERIAL_INVALID_VALUE;
  memset(storage, 0, sizeof(*storage));

  storage->name = tstr_dup(port_name);
  storage->view.name = storage->name;
  storage->view.transport = SALTS_SERIAL_TRANSPORT_UNKNOWN;
  return SALTS_SERIAL_OK;
}

static salts_serial_result_t posix_open(salts_port_handle_t **handle_out, const char *port_name,
                                         salts_serial_mode_t mode) {
  int flags = O_NOCTTY | O_NONBLOCK;
  int fd;
  salts_port_handle_t *handle;

  if (!handle_out || !port_name) return SALTS_SERIAL_INVALID_VALUE;
  *handle_out = NULL;

  if ((mode & (SALTS_SERIAL_MODE_READ | SALTS_SERIAL_MODE_WRITE)) == 0 ||
      (mode & ~(SALTS_SERIAL_MODE_READ | SALTS_SERIAL_MODE_WRITE)) != 0) {
    return SALTS_SERIAL_INVALID_VALUE;
  }

  if ((mode & SALTS_SERIAL_MODE_READ_WRITE) == SALTS_SERIAL_MODE_READ_WRITE) {
    flags |= O_RDWR;
  } else if (mode & SALTS_SERIAL_MODE_WRITE) {
    flags |= O_WRONLY;
  } else {
    flags |= O_RDONLY;
  }

  fd = open(port_name, flags);
  if (fd < 0) {
    if (errno == ENOENT || errno == ENODEV) return SALTS_SERIAL_IO_FAILED;
    if (errno == EACCES) return SALTS_SERIAL_IO_FAILED;
    return SALTS_SERIAL_IO_FAILED;
  }

  handle = (salts_port_handle_t *)calloc(1, sizeof(*handle));
  if (!handle) {
    close(fd);
    return SALTS_SERIAL_NO_MEMORY;
  }

  handle->fd = fd;
  strncpy(handle->port_name, port_name, sizeof(handle->port_name) - 1);
  handle->mode = mode;

  *handle_out = handle;
  return SALTS_SERIAL_OK;
}

static void posix_close(salts_port_handle_t *handle) {
  if (!handle) return;
  if (handle->fd >= 0) {
    close(handle->fd);
  }
  free(handle);
}

static salts_serial_result_t posix_configure(salts_port_handle_t *handle,
                                              const salts_serial_config_t *config) {
  struct termios options;
  speed_t speed;

  if (!handle || !config || handle->fd < 0) return SALTS_SERIAL_INVALID_VALUE;

  if (tcgetattr(handle->fd, &options) < 0) return SALTS_SERIAL_IO_FAILED;

  speed = baudrate_to_speed(config->baudrate);
  cfsetispeed(&options, speed);
  cfsetospeed(&options, speed);

  options.c_cflag |= (CLOCAL | CREAD);

  options.c_cflag &= ~CSIZE;
  switch (config->bits) {
  case 5: options.c_cflag |= CS5; break;
  case 6: options.c_cflag |= CS6; break;
  case 7: options.c_cflag |= CS7; break;
  case 8: options.c_cflag |= CS8; break;
  default: return SALTS_SERIAL_INVALID_VALUE;
  }

  switch (config->stopbits) {
  case 1: options.c_cflag &= ~CSTOPB; break;
  case 2: options.c_cflag |= CSTOPB; break;
  default: return SALTS_SERIAL_INVALID_VALUE;
  }

  switch (config->parity) {
  case SALTS_SERIAL_PARITY_NONE:
    options.c_cflag &= ~PARENB;
    break;
  case SALTS_SERIAL_PARITY_ODD:
    options.c_cflag |= (PARENB | PARODD);
    break;
  case SALTS_SERIAL_PARITY_EVEN:
    options.c_cflag |= PARENB;
    options.c_cflag &= ~PARODD;
    break;
  default:
    return SALTS_SERIAL_INVALID_VALUE;
  }

  if (config->flowcontrol == SALTS_SERIAL_FLOWCONTROL_RTSCTS) {
#ifdef CRTSCTS
    options.c_cflag |= CRTSCTS;
#endif
  } else {
#ifdef CRTSCTS
    options.c_cflag &= ~CRTSCTS;
#endif
  }

  if (config->flowcontrol == SALTS_SERIAL_FLOWCONTROL_XONXOFF) {
    options.c_iflag |= (IXON | IXOFF | IXANY);
  } else {
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
  }

  options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
  options.c_oflag &= ~OPOST;

  options.c_cc[VMIN] = 0;
  options.c_cc[VTIME] = 0;

  if (tcsetattr(handle->fd, TCSANOW, &options) < 0) return SALTS_SERIAL_IO_FAILED;

  tcflush(handle->fd, TCIOFLUSH);
  return SALTS_SERIAL_OK;
}

static salts_serial_result_t posix_blocking_read(salts_port_handle_t *handle, void *buf,
                                                  size_t count, unsigned int timeout_ms,
                                                  size_t *bytes_read) {
  struct pollfd pfd;
  int poll_res;
  ssize_t res;

  if (bytes_read) *bytes_read = 0;
  if (!handle || (!buf && count != 0) || handle->fd < 0) return SALTS_SERIAL_INVALID_VALUE;

  pfd.fd = handle->fd;
  pfd.events = POLLIN;
  pfd.revents = 0;

  poll_res = poll(&pfd, 1, (int)timeout_ms);
  if (poll_res < 0) return SALTS_SERIAL_IO_FAILED;
  if (poll_res == 0) return SALTS_SERIAL_OK;

  res = read(handle->fd, buf, count);
  if (res < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) return SALTS_SERIAL_OK;
    return SALTS_SERIAL_IO_FAILED;
  }

  if (bytes_read) *bytes_read = (size_t)res;
  return SALTS_SERIAL_OK;
}

static salts_serial_result_t posix_blocking_write(salts_port_handle_t *handle, const void *buf,
                                                   size_t count, unsigned int timeout_ms,
                                                   size_t *bytes_written) {
  struct pollfd pfd;
  int poll_res;
  ssize_t res;

  if (bytes_written) *bytes_written = 0;
  if (!handle || (!buf && count != 0) || handle->fd < 0) return SALTS_SERIAL_INVALID_VALUE;

  pfd.fd = handle->fd;
  pfd.events = POLLOUT;
  pfd.revents = 0;

  poll_res = poll(&pfd, 1, (int)timeout_ms);
  if (poll_res < 0) return SALTS_SERIAL_IO_FAILED;
  if (poll_res == 0) return SALTS_SERIAL_OK;

  res = write(handle->fd, buf, count);
  if (res < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) return SALTS_SERIAL_OK;
    return SALTS_SERIAL_IO_FAILED;
  }

  if (bytes_written) *bytes_written = (size_t)res;
  return SALTS_SERIAL_OK;
}

static salts_serial_result_t posix_nonblocking_read(salts_port_handle_t *handle, void *buf,
                                                     size_t count, size_t *bytes_read) {
  ssize_t res;

  if (bytes_read) *bytes_read = 0;
  if (!handle || (!buf && count != 0) || handle->fd < 0) return SALTS_SERIAL_INVALID_VALUE;

  res = read(handle->fd, buf, count);
  if (res < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) return SALTS_SERIAL_OK;
    return SALTS_SERIAL_IO_FAILED;
  }

  if (bytes_read) *bytes_read = (size_t)res;
  return SALTS_SERIAL_OK;
}

static salts_serial_result_t posix_nonblocking_write(salts_port_handle_t *handle, const void *buf,
                                                      size_t count, size_t *bytes_written) {
  ssize_t res;

  if (bytes_written) *bytes_written = 0;
  if (!handle || (!buf && count != 0) || handle->fd < 0) return SALTS_SERIAL_INVALID_VALUE;

  res = write(handle->fd, buf, count);
  if (res < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) return SALTS_SERIAL_OK;
    return SALTS_SERIAL_IO_FAILED;
  }

  if (bytes_written) *bytes_written = (size_t)res;
  return SALTS_SERIAL_OK;
}

static salts_serial_result_t posix_new_event_set(salts_serial_event_set_impl_t **set_out) {
  salts_serial_event_set_impl_t *set;

  if (!set_out) return SALTS_SERIAL_INVALID_VALUE;
  set = (salts_serial_event_set_impl_t *)calloc(1, sizeof(*set));
  if (!set) return SALTS_SERIAL_NO_MEMORY;

  *set_out = set;
  return SALTS_SERIAL_OK;
}

static void posix_free_event_set(salts_serial_event_set_impl_t *set) {
  free(set);
}

static salts_serial_result_t posix_add_port_events(salts_serial_event_set_impl_t *set,
                                                    salts_port_handle_t *handle,
                                                    unsigned int events) {
  short poll_events = 0;

  if (!set || !handle || handle->fd < 0) return SALTS_SERIAL_INVALID_VALUE;
  if (set->count >= MAX_EVENT_PORTS) return SALTS_SERIAL_NO_MEMORY;

  if (events & SALTS_SERIAL_EVENT_RX_READY) poll_events |= POLLIN;
  if (events & SALTS_SERIAL_EVENT_TX_READY) poll_events |= POLLOUT;
  if (events & SALTS_SERIAL_EVENT_ERROR) poll_events |= POLLERR;

  set->ports[set->count] = handle;
  set->event_masks[set->count] = events;
  set->fds[set->count].fd = handle->fd;
  set->fds[set->count].events = poll_events;
  set->fds[set->count].revents = 0;
  set->count++;

  return SALTS_SERIAL_OK;
}

static salts_serial_result_t posix_wait(salts_serial_event_set_impl_t *set,
                                         unsigned int timeout_ms, unsigned int *events_out) {
  int poll_res;
  size_t i;

  if (events_out) *events_out = 0;
  if (!set) return SALTS_SERIAL_INVALID_VALUE;
  if (set->count == 0) return SALTS_SERIAL_INVALID_STATE;

  poll_res = poll(set->fds, (nfds_t)set->count, (int)timeout_ms);
  if (poll_res < 0) return SALTS_SERIAL_IO_FAILED;
  if (poll_res == 0) return SALTS_SERIAL_WOULD_BLOCK;

  for (i = 0; i < set->count; i++) {
    if (set->fds[i].revents != 0) {
      unsigned int fired = 0;
      if (set->fds[i].revents & POLLIN) fired |= SALTS_SERIAL_EVENT_RX_READY;
      if (set->fds[i].revents & POLLOUT) fired |= SALTS_SERIAL_EVENT_TX_READY;
      if (set->fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) fired |= SALTS_SERIAL_EVENT_ERROR;

      if (events_out) *events_out = fired;
      return SALTS_SERIAL_OK;
    }
  }

  return SALTS_SERIAL_WOULD_BLOCK;
}

static const salts_serial_backend_ops_t posix_backend_ops = {
    posix_list_ports,
    posix_get_port_info,
    posix_open,
    posix_close,
    posix_configure,
    posix_blocking_read,
    posix_blocking_write,
    posix_nonblocking_read,
    posix_nonblocking_write,
    posix_new_event_set,
    posix_free_event_set,
    posix_add_port_events,
    posix_wait,
};

const salts_serial_backend_ops_t *salts_serial_get_native_backend(void) {
  return &posix_backend_ops;
}

#endif /* !_WIN32 */
