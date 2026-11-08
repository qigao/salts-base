/*
 * Copyright (c) 2026 Salts Project
 */

#if defined(_WIN32) || defined(__CYGWIN__)

#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <initguid.h>
#include <devguid.h>
#include <setupapi.h>
#include <regstr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

DEFINE_GUID(SALTS_GUID_DEVINTERFACE_COMPORT, 0x86e0d1e0, 0x8089, 0x11d0, 0x9c, 0xe4, 0x08, 0x00, 0x3e, 0x30, 0x1f, 0x73);

#include "serial_internal.h"
#include "tstr.h"

struct salts_port_handle {
  HANDLE handle;
  char port_name[256];
  salts_serial_mode_t mode;
};

#define MAX_EVENT_PORTS 64

struct salts_serial_event_set_impl {
  salts_port_handle_t *ports[MAX_EVENT_PORTS];
  unsigned int event_masks[MAX_EVENT_PORTS];
  HANDLE events[MAX_EVENT_PORTS];
  OVERLAPPED overlapped[MAX_EVENT_PORTS];
  DWORD comm_events[MAX_EVENT_PORTS];
  size_t count;
};

static wchar_t *utf8_to_wchar(const char *utf8_str) {
  int wlen;
  wchar_t *wstr;

  if (!utf8_str) return NULL;
  wlen = MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, NULL, 0);
  if (wlen <= 0) return NULL;

  wstr = (wchar_t *)malloc((size_t)wlen * sizeof(wchar_t));
  if (!wstr) return NULL;

  if (MultiByteToWideChar(CP_UTF8, 0, utf8_str, -1, wstr, wlen) <= 0) {
    free(wstr);
    return NULL;
  }
  return wstr;
}

static tstr wchar_to_utf8(const wchar_t *wstr) {
  int len;
  tstr str;

  if (!wstr) return NULL;
  len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
  if (len <= 0) return NULL;

  /* Port storage clones and destroys these values through tstr traits. */
  str = tstr_new_len(NULL, (size_t)len - 1);
  if (!str) return NULL;

  if (WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str, len, NULL, NULL) <= 0) {
    tstr_free(str);
    return NULL;
  }
  return str;
}

#ifdef SALTS_SERIAL_TESTING
tstr salts_serial_test_wchar_to_utf8(const wchar_t *wstr) {
  return wchar_to_utf8(wstr);
}
#endif

static void format_device_path(const char *port_name, wchar_t *out_path, size_t max_chars) {
  if (_strnicmp(port_name, "\\\\.\\", 4) == 0) {
    wchar_t *wname = utf8_to_wchar(port_name);
    if (wname) {
      wcsncpy_s(out_path, max_chars, wname, _TRUNCATE);
      free(wname);
      return;
    }
  }

  if (_strnicmp(port_name, "COM", 3) == 0) {
    wchar_t *wname = utf8_to_wchar(port_name);
    if (wname) {
      swprintf_s(out_path, max_chars, L"\\\\.\\%s", wname);
      free(wname);
      return;
    }
  }

  wchar_t *wname = utf8_to_wchar(port_name);
  if (wname) {
    wcsncpy_s(out_path, max_chars, wname, _TRUNCATE);
    free(wname);
  } else {
    out_path[0] = L'\0';
  }
}

static salts_serial_result_t win32_list_ports(salts_serial_port_info_vec_t *vec) {
  HDEVINFO dev_info;
  SP_DEVINFO_DATA dev_data;
  DWORD i;

  if (!vec) return SALTS_SERIAL_INVALID_VALUE;

  dev_info = SetupDiGetClassDevsW(&SALTS_GUID_DEVINTERFACE_COMPORT, NULL, NULL,
                                 DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (dev_info == INVALID_HANDLE_VALUE) {
    dev_info = SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, NULL, NULL, DIGCF_PRESENT);
  }
  if (dev_info == INVALID_HANDLE_VALUE) return SALTS_SERIAL_OK;

  dev_data.cbSize = sizeof(SP_DEVINFO_DATA);
  for (i = 0; SetupDiEnumDeviceInfo(dev_info, i, &dev_data); i++) {
    HKEY key;
    wchar_t port_name_w[256] = {0};
    wchar_t desc_w[512] = {0};
    wchar_t mfg_w[256] = {0};
    DWORD name_len = sizeof(port_name_w);
    DWORD desc_len = sizeof(desc_w);
    DWORD mfg_len = sizeof(mfg_w);
    DWORD type;

    key = SetupDiOpenDevRegKey(dev_info, &dev_data, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
    if (key != INVALID_HANDLE_VALUE) {
      RegQueryValueExW(key, L"PortName", NULL, &type, (BYTE *)port_name_w, &name_len);
      RegCloseKey(key);
    }

    if (port_name_w[0] == L'\0') continue;

    SetupDiGetDeviceRegistryPropertyW(dev_info, &dev_data, SPDRP_FRIENDLYNAME, &type,
                                      (BYTE *)desc_w, desc_len, NULL);
    if (desc_w[0] == L'\0') {
      SetupDiGetDeviceRegistryPropertyW(dev_info, &dev_data, SPDRP_DEVICEDESC, &type,
                                        (BYTE *)desc_w, desc_len, NULL);
    }

    SetupDiGetDeviceRegistryPropertyW(dev_info, &dev_data, SPDRP_MFG, &type,
                                      (BYTE *)mfg_w, mfg_len, NULL);

    {
      salts_serial_port_info_storage_t storage;
      memset(&storage, 0, sizeof(storage));

      storage.name = wchar_to_utf8(port_name_w);
      storage.description = wchar_to_utf8(desc_w);
      storage.usb_manufacturer = wchar_to_utf8(mfg_w);
      if (!storage.name || !storage.description || !storage.usb_manufacturer) {
        salts_serial_port_info_storage_destroy(&storage);
        SetupDiDestroyDeviceInfoList(dev_info);
        return SALTS_SERIAL_NO_MEMORY;
      }

      storage.view.name = storage.name;
      storage.view.description = storage.description;
      storage.view.usb_manufacturer = storage.usb_manufacturer;
      storage.view.transport = SALTS_SERIAL_TRANSPORT_NATIVE;

      wchar_t hwid_w[512] = {0};
      if (SetupDiGetDeviceRegistryPropertyW(dev_info, &dev_data, SPDRP_HARDWAREID, &type,
                                            (BYTE *)hwid_w, sizeof(hwid_w), NULL)) {
        int vid = 0, pid = 0;
        if (swscanf_s(hwid_w, L"USB\\VID_%4x&PID_%4x", &vid, &pid) == 2 ||
            swscanf_s(hwid_w, L"FTDIBUS\\COMPORT&VID_%4x+PID_%4x", &vid, &pid) == 2) {
          storage.view.has_usb_vid_pid = 1;
          storage.view.usb_vid = vid;
          storage.view.usb_pid = pid;
          storage.view.transport = SALTS_SERIAL_TRANSPORT_USB;
        }
      }

      {
        salts_serial_result_t status = salts_serial_port_info_vec_t_push(vec, storage);
        salts_serial_port_info_storage_destroy(&storage);
        if (status != SALTS_SERIAL_OK) {
          SetupDiDestroyDeviceInfoList(dev_info);
          return status;
        }
      }
    }
  }

  SetupDiDestroyDeviceInfoList(dev_info);
  return SALTS_SERIAL_OK;
}

static salts_serial_result_t win32_get_port_info(const char *port_name,
                                                 salts_serial_port_info_storage_t *storage) {
  salts_serial_port_info_vec_t vec = {0};
  size_t i;
  salts_serial_result_t res;

  if (!port_name || !storage) return SALTS_SERIAL_INVALID_VALUE;
  memset(storage, 0, sizeof(*storage));

  res = salts_serial_port_info_vec_t_init(&vec);
  if (res != SALTS_SERIAL_OK) return res;

  res = win32_list_ports(&vec);
  if (res == SALTS_SERIAL_OK) {
    for (i = 0; i < salts_serial_port_info_vec_t_size(&vec); ++i) {
      salts_serial_port_info_storage_t *item = salts_serial_port_info_vec_t_at(&vec, i);
      if (item->name && _stricmp(item->name, port_name) == 0) {
        *storage = *item;
        memset(item, 0, sizeof(*item));
        break;
      }
    }
  }

  salts_serial_port_info_vec_t_destroy(&vec);

  if (!storage->name) {
    storage->name = tstr_dup(port_name);
    storage->view.name = storage->name;
    storage->view.transport = SALTS_SERIAL_TRANSPORT_UNKNOWN;
  }
  return SALTS_SERIAL_OK;
}

static salts_serial_result_t win32_open(salts_port_handle_t **handle_out, const char *port_name,
                                        salts_serial_mode_t mode) {
  wchar_t device_path[256];
  DWORD desired_access = 0;
  HANDLE h;
  salts_port_handle_t *port_handle;

  if (!handle_out || !port_name) return SALTS_SERIAL_INVALID_VALUE;
  *handle_out = NULL;

  if ((mode & (SALTS_SERIAL_MODE_READ | SALTS_SERIAL_MODE_WRITE)) == 0 ||
      (mode & ~(SALTS_SERIAL_MODE_READ | SALTS_SERIAL_MODE_WRITE)) != 0) {
    return SALTS_SERIAL_INVALID_VALUE;
  }

  format_device_path(port_name, device_path, 256);

  if (mode & SALTS_SERIAL_MODE_READ) desired_access |= GENERIC_READ;
  if (mode & SALTS_SERIAL_MODE_WRITE) desired_access |= GENERIC_WRITE;

  h = CreateFileW(device_path, desired_access, 0, NULL, OPEN_EXISTING,
                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);

  if (h == INVALID_HANDLE_VALUE) {
    DWORD err = GetLastError();
    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
      return SALTS_SERIAL_IO_FAILED;
    }
    return SALTS_SERIAL_IO_FAILED;
  }

  port_handle = (salts_port_handle_t *)calloc(1, sizeof(*port_handle));
  if (!port_handle) {
    CloseHandle(h);
    return SALTS_SERIAL_NO_MEMORY;
  }

  port_handle->handle = h;
  strncpy_s(port_handle->port_name, sizeof(port_handle->port_name), port_name, _TRUNCATE);
  port_handle->mode = mode;

  SetupComm(h, 4096, 4096);
  PurgeComm(h, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);

  *handle_out = port_handle;
  return SALTS_SERIAL_OK;
}

static void win32_close(salts_port_handle_t *handle) {
  if (!handle) return;
  if (handle->handle != INVALID_HANDLE_VALUE && handle->handle != NULL) {
    PurgeComm(handle->handle, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);
    CloseHandle(handle->handle);
  }
  free(handle);
}

static salts_serial_result_t win32_configure(salts_port_handle_t *handle,
                                             const salts_serial_config_t *config) {
  DCB dcb;
  COMMTIMEOUTS timeouts;

  if (!handle || !config || handle->handle == INVALID_HANDLE_VALUE) {
    return SALTS_SERIAL_INVALID_VALUE;
  }

  memset(&dcb, 0, sizeof(dcb));
  dcb.DCBlength = sizeof(dcb);
  if (!GetCommState(handle->handle, &dcb)) return SALTS_SERIAL_IO_FAILED;

  dcb.BaudRate = (DWORD)config->baudrate;
  dcb.ByteSize = (BYTE)config->bits;

  switch (config->stopbits) {
  case 1: dcb.StopBits = ONESTOPBIT; break;
  case 2: dcb.StopBits = TWOSTOPBITS; break;
  default: return SALTS_SERIAL_INVALID_VALUE;
  }

  switch (config->parity) {
  case SALTS_SERIAL_PARITY_NONE: dcb.Parity = NOPARITY; dcb.fParity = FALSE; break;
  case SALTS_SERIAL_PARITY_ODD: dcb.Parity = ODDPARITY; dcb.fParity = TRUE; break;
  case SALTS_SERIAL_PARITY_EVEN: dcb.Parity = EVENPARITY; dcb.fParity = TRUE; break;
  case SALTS_SERIAL_PARITY_MARK: dcb.Parity = MARKPARITY; dcb.fParity = TRUE; break;
  case SALTS_SERIAL_PARITY_SPACE: dcb.Parity = SPACEPARITY; dcb.fParity = TRUE; break;
  default: return SALTS_SERIAL_INVALID_VALUE;
  }

  dcb.fOutxCtsFlow = FALSE;
  dcb.fRtsControl = RTS_CONTROL_ENABLE;
  dcb.fOutxDsrFlow = FALSE;
  dcb.fDtrControl = DTR_CONTROL_ENABLE;
  dcb.fOutX = FALSE;
  dcb.fInX = FALSE;

  if (config->flowcontrol == SALTS_SERIAL_FLOWCONTROL_RTSCTS) {
    dcb.fOutxCtsFlow = TRUE;
    dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
  } else if (config->flowcontrol == SALTS_SERIAL_FLOWCONTROL_DTRDSR) {
    dcb.fOutxDsrFlow = TRUE;
    dcb.fDtrControl = DTR_CONTROL_HANDSHAKE;
  } else if (config->flowcontrol == SALTS_SERIAL_FLOWCONTROL_XONXOFF) {
    dcb.fOutX = TRUE;
    dcb.fInX = TRUE;
  }

  if (!SetCommState(handle->handle, &dcb)) return SALTS_SERIAL_IO_FAILED;

  memset(&timeouts, 0, sizeof(timeouts));
  timeouts.ReadIntervalTimeout = MAXDWORD;
  timeouts.ReadTotalTimeoutMultiplier = 0;
  timeouts.ReadTotalTimeoutConstant = 0;
  timeouts.WriteTotalTimeoutMultiplier = 0;
  timeouts.WriteTotalTimeoutConstant = 0;
  SetCommTimeouts(handle->handle, &timeouts);

  return SALTS_SERIAL_OK;
}

static salts_serial_result_t win32_blocking_read(salts_port_handle_t *handle, void *buf,
                                                 size_t count, unsigned int timeout_ms,
                                                 size_t *bytes_read) {
  COMMTIMEOUTS timeouts;
  OVERLAPPED ov;
  DWORD read_bytes = 0;
  HANDLE event = INVALID_HANDLE_VALUE;

  if (bytes_read) *bytes_read = 0;
  if (!handle || (!buf && count != 0) || handle->handle == INVALID_HANDLE_VALUE) {
    return SALTS_SERIAL_INVALID_VALUE;
  }

  event = CreateEventW(NULL, TRUE, FALSE, NULL);
  if (event == NULL || event == INVALID_HANDLE_VALUE) {
    return SALTS_SERIAL_NO_MEMORY;
  }

  memset(&timeouts, 0, sizeof(timeouts));
  timeouts.ReadIntervalTimeout = 0;
  timeouts.ReadTotalTimeoutMultiplier = 0;
  timeouts.ReadTotalTimeoutConstant = timeout_ms;
  SetCommTimeouts(handle->handle, &timeouts);

  memset(&ov, 0, sizeof(ov));
  ov.hEvent = event;

  if (!ReadFile(handle->handle, buf, (DWORD)count, &read_bytes, &ov)) {
    if (GetLastError() == ERROR_IO_PENDING) {
      if (WaitForSingleObject(event, timeout_ms) == WAIT_OBJECT_0) {
        GetOverlappedResult(handle->handle, &ov, &read_bytes, FALSE);
      } else {
        CancelIo(handle->handle);
      }
    }
  }

  CloseHandle(event);
  if (bytes_read) *bytes_read = (size_t)read_bytes;
  return SALTS_SERIAL_OK;
}

static salts_serial_result_t win32_blocking_write(salts_port_handle_t *handle, const void *buf,
                                                  size_t count, unsigned int timeout_ms,
                                                  size_t *bytes_written) {
  COMMTIMEOUTS timeouts;
  OVERLAPPED ov;
  DWORD written_bytes = 0;
  HANDLE event = INVALID_HANDLE_VALUE;

  if (bytes_written) *bytes_written = 0;
  if (!handle || (!buf && count != 0) || handle->handle == INVALID_HANDLE_VALUE) {
    return SALTS_SERIAL_INVALID_VALUE;
  }

  event = CreateEventW(NULL, TRUE, FALSE, NULL);
  if (event == NULL || event == INVALID_HANDLE_VALUE) {
    return SALTS_SERIAL_NO_MEMORY;
  }

  memset(&timeouts, 0, sizeof(timeouts));
  timeouts.WriteTotalTimeoutMultiplier = 0;
  timeouts.WriteTotalTimeoutConstant = timeout_ms;
  SetCommTimeouts(handle->handle, &timeouts);

  memset(&ov, 0, sizeof(ov));
  ov.hEvent = event;

  if (!WriteFile(handle->handle, buf, (DWORD)count, &written_bytes, &ov)) {
    if (GetLastError() == ERROR_IO_PENDING) {
      if (WaitForSingleObject(event, timeout_ms) == WAIT_OBJECT_0) {
        GetOverlappedResult(handle->handle, &ov, &written_bytes, FALSE);
      } else {
        CancelIo(handle->handle);
      }
    }
  }

  CloseHandle(event);
  if (bytes_written) *bytes_written = (size_t)written_bytes;
  return SALTS_SERIAL_OK;
}

static salts_serial_result_t win32_nonblocking_read(salts_port_handle_t *handle, void *buf,
                                                    size_t count, size_t *bytes_read) {
  COMMTIMEOUTS timeouts;
  DWORD read_bytes = 0;
  OVERLAPPED ov;
  HANDLE event = INVALID_HANDLE_VALUE;

  if (bytes_read) *bytes_read = 0;
  if (!handle || (!buf && count != 0) || handle->handle == INVALID_HANDLE_VALUE) {
    return SALTS_SERIAL_INVALID_VALUE;
  }

  event = CreateEventW(NULL, TRUE, FALSE, NULL);
  if (event == NULL || event == INVALID_HANDLE_VALUE) {
    return SALTS_SERIAL_NO_MEMORY;
  }

  memset(&timeouts, 0, sizeof(timeouts));
  timeouts.ReadIntervalTimeout = MAXDWORD;
  timeouts.ReadTotalTimeoutMultiplier = 0;
  timeouts.ReadTotalTimeoutConstant = 0;
  SetCommTimeouts(handle->handle, &timeouts);

  memset(&ov, 0, sizeof(ov));
  ov.hEvent = event;

  if (!ReadFile(handle->handle, buf, (DWORD)count, &read_bytes, &ov)) {
    if (GetLastError() == ERROR_IO_PENDING) {
      GetOverlappedResult(handle->handle, &ov, &read_bytes, TRUE);
    }
  }

  CloseHandle(event);
  if (bytes_read) *bytes_read = (size_t)read_bytes;
  return SALTS_SERIAL_OK;
}

static salts_serial_result_t win32_nonblocking_write(salts_port_handle_t *handle, const void *buf,
                                                     size_t count, size_t *bytes_written) {
  DWORD written_bytes = 0;
  OVERLAPPED ov;
  HANDLE event = INVALID_HANDLE_VALUE;

  if (bytes_written) *bytes_written = 0;
  if (!handle || (!buf && count != 0) || handle->handle == INVALID_HANDLE_VALUE) {
    return SALTS_SERIAL_INVALID_VALUE;
  }

  event = CreateEventW(NULL, TRUE, FALSE, NULL);
  if (event == NULL || event == INVALID_HANDLE_VALUE) {
    return SALTS_SERIAL_NO_MEMORY;
  }

  memset(&ov, 0, sizeof(ov));
  ov.hEvent = event;

  if (!WriteFile(handle->handle, buf, (DWORD)count, &written_bytes, &ov)) {
    if (GetLastError() == ERROR_IO_PENDING) {
      GetOverlappedResult(handle->handle, &ov, &written_bytes, TRUE);
    }
  }

  CloseHandle(event);
  if (bytes_written) *bytes_written = (size_t)written_bytes;
  return SALTS_SERIAL_OK;
}

static salts_serial_result_t win32_new_event_set(salts_serial_event_set_impl_t **set_out) {
  salts_serial_event_set_impl_t *set;

  if (!set_out) return SALTS_SERIAL_INVALID_VALUE;
  set = (salts_serial_event_set_impl_t *)calloc(1, sizeof(*set));
  if (!set) return SALTS_SERIAL_NO_MEMORY;

  *set_out = set;
  return SALTS_SERIAL_OK;
}

static void win32_free_event_set(salts_serial_event_set_impl_t *set) {
  size_t i;

  if (!set) return;
  for (i = 0; i < set->count; i++) {
    if (set->events[i]) CloseHandle(set->events[i]);
  }
  free(set);
}

static salts_serial_result_t win32_add_port_events(salts_serial_event_set_impl_t *set,
                                                   salts_port_handle_t *handle,
                                                   unsigned int events) {
  DWORD mask = 0;

  if (!set || !handle || handle->handle == INVALID_HANDLE_VALUE) {
    return SALTS_SERIAL_INVALID_VALUE;
  }
  if (set->count >= MAX_EVENT_PORTS) return SALTS_SERIAL_NO_MEMORY;

  if (events & SALTS_SERIAL_EVENT_RX_READY) mask |= EV_RXCHAR;
  if (events & SALTS_SERIAL_EVENT_TX_READY) mask |= EV_TXEMPTY;
  if (events & SALTS_SERIAL_EVENT_ERROR) mask |= EV_ERR;

  SetCommMask(handle->handle, mask);

  set->ports[set->count] = handle;
  set->event_masks[set->count] = events;
  set->events[set->count] = CreateEventW(NULL, TRUE, FALSE, NULL);
  memset(&set->overlapped[set->count], 0, sizeof(OVERLAPPED));
  set->overlapped[set->count].hEvent = set->events[set->count];
  set->count++;

  return SALTS_SERIAL_OK;
}

static salts_serial_result_t win32_wait(salts_serial_event_set_impl_t *set,
                                         unsigned int timeout_ms, unsigned int *events_out) {
  DWORD i, res;

  if (events_out) *events_out = 0;
  if (!set) return SALTS_SERIAL_INVALID_VALUE;
  if (set->count == 0) return SALTS_SERIAL_INVALID_STATE;

  for (i = 0; i < (DWORD)set->count; i++) {
    ResetEvent(set->events[i]);
    WaitCommEvent(set->ports[i]->handle, &set->comm_events[i], &set->overlapped[i]);
  }

  res = WaitForMultipleObjects((DWORD)set->count, set->events, FALSE, timeout_ms);

  if (res >= WAIT_OBJECT_0 && res < WAIT_OBJECT_0 + (DWORD)set->count) {
    DWORD signaled_index = res - WAIT_OBJECT_0;
    unsigned int fired = 0;
    DWORD evt = set->comm_events[signaled_index];

    /* Cancel only the non-signaled ports to avoid interrupting ready I/O */
    for (i = 0; i < (DWORD)set->count; i++) {
      if (i != signaled_index) {
        CancelIo(set->ports[i]->handle);
      }
    }

    if (evt & EV_RXCHAR) fired |= SALTS_SERIAL_EVENT_RX_READY;
    if (evt & EV_TXEMPTY) fired |= SALTS_SERIAL_EVENT_TX_READY;
    if (evt & EV_ERR) fired |= SALTS_SERIAL_EVENT_ERROR;

    if (events_out) *events_out = fired;
    return SALTS_SERIAL_OK;
  }

  /* Timeout or error: cancel all pending operations */
  for (i = 0; i < (DWORD)set->count; i++) {
    CancelIo(set->ports[i]->handle);
  }

  if (res == WAIT_TIMEOUT) return SALTS_SERIAL_WOULD_BLOCK;
  return SALTS_SERIAL_IO_FAILED;
}

static const salts_serial_backend_ops_t win32_backend_ops = {
    win32_list_ports,
    win32_get_port_info,
    win32_open,
    win32_close,
    win32_configure,
    win32_blocking_read,
    win32_blocking_write,
    win32_nonblocking_read,
    win32_nonblocking_write,
    win32_new_event_set,
    win32_free_event_set,
    win32_add_port_events,
    win32_wait,
};

const salts_serial_backend_ops_t *salts_serial_get_native_backend(void) {
  return &win32_backend_ops;
}

#endif /* _WIN32 */
