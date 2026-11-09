/**
 * Platform abstraction implementation - minimal but sufficient
 * "Write portable code, but test on real systems" - Linus
 */
#include <salts/platform.h>
#include <salts/clock.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <iphlpapi.h>
  #include <windows.h>
  #include <winternl.h>
#else
  #include <arpa/inet.h>
  #include <ifaddrs.h>
  #include <net/if.h>
  #include <netinet/in.h>
  #include <pwd.h>
  #include <signal.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <sys/utsname.h>
  #include <time.h>
  #include <unistd.h>
  #if defined(__APPLE__)
    #include <mach/mach.h>
    #include <sys/sysctl.h>
  #endif
#endif


static int copy_string(char *buffer, size_t buffer_size, const char *value) {
  size_t len;
  if (!buffer || buffer_size == 0 || !value) {
    return -EINVAL;
  }
  len = strlen(value);
  if (len + 1 > buffer_size) {
    return -ENOSPC;
  }
  memcpy(buffer, value, len + 1);
  return 0;
}

static void ascii_lowercase(char *value) {
  if (!value) {
    return;
  }
  while (*value) {
    if (*value >= 'A' && *value <= 'Z') {
      *value = (char)(*value - 'A' + 'a');
    }
    ++value;
  }
}

static void cpu_info_clear(cpu_info_t *info) {
  if (!info) {
    return;
  }
  memset(info, 0, sizeof(*info));
  copy_string(info->model, sizeof(info->model), "unknown");
}

static void network_interface_clear(network_interface_t *iface) {
  if (!iface) {
    return;
  }
  memset(iface, 0, sizeof(*iface));
}

static int is_loopback_address(const char *address) {
  if (!address || address[0] == '\0') {
    return 0;
  }
  return strcmp(address, "127.0.0.1") == 0 || strcmp(address, "::1") == 0;
}

#ifdef _WIN32
static int windows_version(char *buffer, size_t buffer_size) {
  HMODULE ntdll = GetModuleHandleA("ntdll.dll");
  typedef LONG(WINAPI * rtl_get_version_fn)(PRTL_OSVERSIONINFOW);
  rtl_get_version_fn rtl_get_version;
  RTL_OSVERSIONINFOW version_info;
  int written;

  if (!ntdll) {
    return -ENOENT;
  }

  rtl_get_version = (rtl_get_version_fn)GetProcAddress(ntdll, "RtlGetVersion");
  if (!rtl_get_version) {
    return -ENOENT;
  }

  memset(&version_info, 0, sizeof(version_info));
  version_info.dwOSVersionInfoSize = sizeof(version_info);
  if (rtl_get_version(&version_info) != 0) {
    return -EIO;
  }

  written = snprintf(buffer, buffer_size, "%lu.%lu.%lu", (unsigned long)version_info.dwMajorVersion,
                     (unsigned long)version_info.dwMinorVersion,
                     (unsigned long)version_info.dwBuildNumber);
  if (written < 0 || (size_t)written >= buffer_size) {
    return -ENOSPC;
  }
  return 0;
}
#endif

#ifdef _WIN32
int timeval_now(wall_time_t *tv, timezone_info_t *tz) {
  UNUSED(tz);
  if (tv) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;

    /* 100ns intervals since Jan 1, 1601. */
    uint64_t intervals = uli.QuadPart - 116444736000000000ULL;
    tv->tv_sec = (int64_t)(intervals / 10000000ULL);
    tv->tv_usec = (int32_t)((intervals % 10000000ULL) / 10ULL);
  }
  return 0;
}
#else
int timeval_now(wall_time_t *tv, timezone_info_t *tz) {
  UNUSED(tz);
  struct timeval system_tv;
  int result = gettimeofday(&system_tv, NULL);
  if (result == 0 && tv) {
    tv->tv_sec = (int64_t)system_tv.tv_sec;
    tv->tv_usec = (int32_t)system_tv.tv_usec;
  }
  return result;
}
#endif


static int is_leap_year(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int days_in_month(int year, int month) {
  static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  if (month == 2 && is_leap_year(year)) return 29;
  return days[month - 1];
}

static int64_t days_before_year(int year) {
  int64_t y = (int64_t)year - 1;
  return y * 365 + y / 4 - y / 100 + y / 400;
}

static int64_t days_before_month(int year, int month) {
  static const int days_before[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  int64_t days;
  if (month < 1 || month > 12) return -1;
  days = days_before[month - 1];
  if (month > 2 && is_leap_year(year)) days++;
  return days;
}

int time_utc(time_t t, struct tm *out) {
  if (!out) return -EINVAL;
#ifdef _WIN32
  return gmtime_s(out, &t) == 0 ? 0 : -EINVAL;
#else
  return gmtime_r(&t, out) ? 0 : -EINVAL;
#endif
}

int time_local(time_t t, struct tm *out) {
  if (!out) return -EINVAL;
#ifdef _WIN32
  return localtime_s(out, &t) == 0 ? 0 : -EINVAL;
#else
  return localtime_r(&t, out) ? 0 : -EINVAL;
#endif
}

time_t time_from_utc(const struct tm *tm_value) {
  int year;
  int month;
  int day;
  int64_t days;
  int64_t seconds;

  if (!tm_value) {
    errno = EINVAL;
    return (time_t)-1;
  }

  year = tm_value->tm_year + 1900;
  month = tm_value->tm_mon + 1;
  day = tm_value->tm_mday;
  if (month < 1 || month > 12 || day < 1 || day > days_in_month(year, month) ||
      tm_value->tm_hour < 0 || tm_value->tm_hour > 23 || tm_value->tm_min < 0 ||
      tm_value->tm_min > 59 || tm_value->tm_sec < 0 || tm_value->tm_sec > 60) {
    errno = EINVAL;
    return (time_t)-1;
  }

  days = days_before_year(year) - days_before_year(1970);
  days += days_before_month(year, month);
  days += day - 1;
  seconds = days * 86400 + tm_value->tm_hour * 3600 + tm_value->tm_min * 60 + tm_value->tm_sec;
  return (time_t)seconds;
}

time_t time_from_local(struct tm *tm_value) {
  if (!tm_value) {
    errno = EINVAL;
    return (time_t)-1;
  }
  return mktime(tm_value);
}

#if defined(__clang__)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined(__GNUC__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif

static size_t format_calendar_time(char *buffer, size_t buffer_size, const char *format,
                                    const struct tm *tm_value) {
  return strftime(buffer, buffer_size, format, tm_value);
}

#if defined(__clang__)
  #pragma clang diagnostic pop
#elif defined(__GNUC__)
  #pragma GCC diagnostic pop
#endif

int time_format_utc(time_t t, const char *format, char *buffer, size_t buffer_size) {
  struct tm tm_value;
  size_t written;

  if (!format || !buffer || buffer_size == 0) return -EINVAL;
  if (time_utc(t, &tm_value) != 0) return -EINVAL;
  written = format_calendar_time(buffer, buffer_size, format, &tm_value);
  if (written == 0) return -ENOSPC;
  return (int)written;
}

int time_format_local(time_t t, const char *format, char *buffer, size_t buffer_size) {
  struct tm tm_value;
  size_t written;

  if (!format || !buffer || buffer_size == 0) return -EINVAL;
  if (time_local(t, &tm_value) != 0) return -EINVAL;
  written = format_calendar_time(buffer, buffer_size, format, &tm_value);
  if (written == 0) return -ENOSPC;
  return (int)written;
}

int os_name(char *buffer, size_t buffer_size) {
#ifdef _WIN32
  return copy_string(buffer, buffer_size, "windows");
#else
  struct utsname info;
  if (uname(&info) != 0) {
    return -errno;
  }
  if (copy_string(buffer, buffer_size, info.sysname) != 0) {
    return -ENOSPC;
  }
  if (strcmp(buffer, "Darwin") == 0) {
    return copy_string(buffer, buffer_size, "macos");
  }
  ascii_lowercase(buffer);
  return 0;
#endif
}

int os_version(char *buffer, size_t buffer_size) {
#ifdef _WIN32
  return windows_version(buffer, buffer_size);
#else
  struct utsname info;
  if (uname(&info) != 0) {
    return -errno;
  }
  return copy_string(buffer, buffer_size, info.release);
#endif
}

int cpu_arch(char *buffer, size_t buffer_size) {
#ifdef _WIN32
  SYSTEM_INFO system_info;
  const char *arch = "unknown";

  GetNativeSystemInfo(&system_info);
  switch (system_info.wProcessorArchitecture) {
  case PROCESSOR_ARCHITECTURE_AMD64:
    arch = "x86_64";
    break;
  case PROCESSOR_ARCHITECTURE_INTEL:
    arch = "x86";
    break;
  case PROCESSOR_ARCHITECTURE_ARM64:
    arch = "arm64";
    break;
  case PROCESSOR_ARCHITECTURE_ARM:
    arch = "arm";
    break;
  default:
    break;
  }

  return copy_string(buffer, buffer_size, arch);
#else
  struct utsname info;
  if (uname(&info) != 0) {
    return -errno;
  }
  return copy_string(buffer, buffer_size, info.machine);
#endif
}

int user_name(char *buffer, size_t buffer_size) {
#ifdef _WIN32
  DWORD size = (DWORD)buffer_size;
  const DWORD env_len = GetEnvironmentVariableA("USERNAME", buffer, size);
  if (env_len > 0 && env_len < size) {
    return 0;
  }
  return -ENOENT;
#else
  const char *user = getenv("USER");
  if (user && user[0] != '\0') {
    return copy_string(buffer, buffer_size, user);
  }
  {
    struct passwd *pwd = getpwuid(getuid());
    if (!pwd || !pwd->pw_name) {
      return -ENOENT;
    }
    return copy_string(buffer, buffer_size, pwd->pw_name);
  }
#endif
}

int host_name(char *buffer, size_t buffer_size) {
#ifdef _WIN32
  DWORD size = (DWORD)buffer_size;
  if (!GetComputerNameA(buffer, &size)) {
    return -ENOENT;
  }
  return 0;
#else
  if (gethostname(buffer, buffer_size) != 0) {
    return -errno;
  }
  buffer[buffer_size - 1] = '\0';
  return 0;
#endif
}

int cpu_info(cpu_info_t *info) {
  if (!info) {
    return -EINVAL;
  }

  cpu_info_clear(info);

#ifdef _WIN32
  {
    SYSTEM_INFO system_info;
    const DWORD env_len =
        GetEnvironmentVariableA("PROCESSOR_IDENTIFIER", info->model, sizeof(info->model));
    GetNativeSystemInfo(&system_info);
    info->core_count = (int)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (info->core_count <= 0) {
      info->core_count = (int)system_info.dwNumberOfProcessors;
    }
    if (env_len == 0 || env_len >= sizeof(info->model)) {
      copy_string(info->model, sizeof(info->model), "unknown");
    }
    info->speed_mhz = 0.0;
    return 0;
  }
#elif defined(__APPLE__)
  {
    int core_count = 0;
    size_t core_count_size = sizeof(core_count);
    struct utsname uts;

    if (sysctlbyname("hw.logicalcpu", &core_count, &core_count_size, NULL, 0) == 0 &&
        core_count > 0) {
      info->core_count = core_count;
    }
    if (uname(&uts) == 0) {
      copy_string(info->model, sizeof(info->model), uts.machine);
    }
    info->speed_mhz = 0.0;
    return 0;
  }
#else
  {
    long core_count = sysconf(_SC_NPROCESSORS_ONLN);
    struct utsname uts;
    if (core_count > 0) {
      info->core_count = (int)core_count;
    }
    if (uname(&uts) == 0) {
      copy_string(info->model, sizeof(info->model), uts.machine);
    }
    info->speed_mhz = 0.0;
    return 0;
  }
#endif
}

int memory_info(memory_info_t *info) {
  if (!info) {
    return -EINVAL;
  }

  memset(info, 0, sizeof(*info));

#ifdef _WIN32
  {
    MEMORYSTATUSEX mem;
    memset(&mem, 0, sizeof(mem));
    mem.dwLength = sizeof(mem);
    if (!GlobalMemoryStatusEx(&mem)) {
      return -EIO;
    }
    info->total_memory = mem.ullTotalPhys;
    info->free_memory = mem.ullAvailPhys;
    info->available_memory = mem.ullAvailPhys;
    return 0;
  }
#elif defined(__APPLE__)
  {
    uint64_t total_memory = 0;
    uint64_t free_pages;
    uint64_t available_pages;
    size_t total_memory_size = sizeof(total_memory);
    mach_port_t host = mach_host_self();
    vm_size_t page_size = 0;
    vm_statistics64_data_t vm_stats;
    mach_msg_type_number_t vm_stats_count = HOST_VM_INFO64_COUNT;
    kern_return_t kr;

    if (sysctlbyname("hw.memsize", &total_memory, &total_memory_size, NULL, 0) != 0) {
      mach_port_deallocate(mach_task_self(), host);
      return -errno;
    }
    kr = host_page_size(host, &page_size);
    if (kr == KERN_SUCCESS) {
      kr = host_statistics64(host, HOST_VM_INFO64, (host_info64_t)(void *)&vm_stats,
                             &vm_stats_count);
    }
    mach_port_deallocate(mach_task_self(), host);
    if (kr != KERN_SUCCESS || page_size == 0) {
      return -EIO;
    }

    free_pages = (uint64_t)vm_stats.free_count;
    if ((uint64_t)vm_stats.inactive_count > UINT64_MAX - free_pages) {
      return -EOVERFLOW;
    }
    available_pages = free_pages + (uint64_t)vm_stats.inactive_count;
    if (free_pages > UINT64_MAX / (uint64_t)page_size ||
        available_pages > UINT64_MAX / (uint64_t)page_size) {
      return -EOVERFLOW;
    }

    info->total_memory = total_memory;
    info->free_memory = free_pages * (uint64_t)page_size;
    info->available_memory = available_pages * (uint64_t)page_size;
    return 0;
  }
#else
  {
    const long page_size = sysconf(_SC_PAGE_SIZE);
    const long total_pages = sysconf(_SC_PHYS_PAGES);
    const long avail_pages = sysconf(_SC_AVPHYS_PAGES);
    if (page_size <= 0 || total_pages <= 0 || avail_pages < 0) {
      return -EIO;
    }
    info->total_memory = (uint64_t)page_size * (uint64_t)total_pages;
    info->free_memory = (uint64_t)page_size * (uint64_t)avail_pages;
    info->available_memory = info->free_memory;
    return 0;
  }
#endif
}

int load_average(load_average_t *info) {
  if (!info) {
    return -EINVAL;
  }

  memset(info, 0, sizeof(*info));

#ifdef _WIN32
  return 0;
#elif defined(__ANDROID__)
  /* getloadavg is not available in Android Bionic; report all zeros */
  return 0;
#else
  {
    double load[3] = {0.0, 0.0, 0.0};
    if (getloadavg(load, 3) < 0) {
      return -errno;
    }
    info->one_minute = load[0];
    info->five_minutes = load[1];
    info->fifteen_minutes = load[2];
    return 0;
  }
#endif
}

#ifdef _WIN32
static void extract_windows_unicast(IP_ADAPTER_ADDRESSES *adapter,
                                          IP_ADAPTER_UNICAST_ADDRESS *unicast,
                                          network_interface_t *iface) {
  DWORD name_len;
  DWORD addr_len = (DWORD)sizeof(iface->address);

  network_interface_clear(iface);
  name_len = WideCharToMultiByte(CP_UTF8, 0, adapter->FriendlyName, -1, iface->name,
                                 (int)sizeof(iface->name), NULL, NULL);
  if (name_len == 0) {
    copy_string(iface->name, sizeof(iface->name), "unknown");
  }

  if (WSAAddressToStringA(unicast->Address.lpSockaddr, (DWORD)unicast->Address.iSockaddrLength,
                          NULL, iface->address, &addr_len) != 0) {
    copy_string(iface->address, sizeof(iface->address), "unknown");
  }

  if (unicast->Address.lpSockaddr->sa_family == AF_INET) {
    ULONG prefix = unicast->OnLinkPrefixLength;
    ULONG mask = prefix == 0 ? 0 : htonl(0xFFFFFFFFu << (32 - prefix));
    struct in_addr mask_addr;
    mask_addr.S_un.S_addr = mask;
    InetNtopA(AF_INET, &mask_addr, iface->netmask, (DWORD)sizeof(iface->netmask));
  } else if (unicast->Address.lpSockaddr->sa_family == AF_INET6) {
    copy_string(iface->netmask, sizeof(iface->netmask), "::");
  }

  iface->is_internal = (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) ||
                       is_loopback_address(iface->address);
}
#else
static int format_sockaddr(const struct sockaddr *address, char *buffer, size_t buffer_size) {
  if (!address || !buffer || buffer_size == 0) return -EINVAL;

  if (address->sa_family == AF_INET) {
    struct sockaddr_in ipv4;
    memcpy(&ipv4, address, sizeof(ipv4));
    return inet_ntop(AF_INET, &ipv4.sin_addr, buffer, buffer_size) ? 0 : -errno;
  }
  if (address->sa_family == AF_INET6) {
    struct sockaddr_in6 ipv6;
    memcpy(&ipv6, address, sizeof(ipv6));
    return inet_ntop(AF_INET6, &ipv6.sin6_addr, buffer, buffer_size) ? 0 : -errno;
  }
  return -EAFNOSUPPORT;
}
#endif

int network_interfaces(network_interface_t *interfaces,
                                      size_t max_interfaces, size_t *count) {
  if (!interfaces || max_interfaces == 0 || !count) {
    return -EINVAL;
  }

  *count = 0;

#ifdef _WIN32
  {
    ULONG flags = GAA_FLAG_INCLUDE_PREFIX;
    ULONG family = AF_UNSPEC;
    ULONG buffer_size = 16384;
    IP_ADAPTER_ADDRESSES *addresses = NULL;
    IP_ADAPTER_ADDRESSES *adapter;
    DWORD rc;

    addresses = (IP_ADAPTER_ADDRESSES *)malloc(buffer_size);
    if (!addresses) {
      return -ENOMEM;
    }

    rc = GetAdaptersAddresses(family, flags, NULL, addresses, &buffer_size);
    if (rc == ERROR_BUFFER_OVERFLOW) {
      IP_ADAPTER_ADDRESSES *bigger = (IP_ADAPTER_ADDRESSES *)realloc(addresses, buffer_size);
      if (!bigger) {
        free(addresses);
        return -ENOMEM;
      }
      addresses = bigger;
      rc = GetAdaptersAddresses(family, flags, NULL, addresses, &buffer_size);
    }

    if (rc != NO_ERROR) {
      free(addresses);
      return -EIO;
    }

    for (adapter = addresses; adapter && *count < max_interfaces; adapter = adapter->Next) {
      IP_ADAPTER_UNICAST_ADDRESS *unicast;
      for (unicast = adapter->FirstUnicastAddress; unicast && *count < max_interfaces;
           unicast = unicast->Next) {
        extract_windows_unicast(adapter, unicast, &interfaces[*count]);
        ++(*count);
      }
    }

    free(addresses);
    return *count > 0 ? 0 : -ENOENT;
  }
#else
  {
    struct ifaddrs *ifaddr = NULL;
    struct ifaddrs *ifa;

    if (getifaddrs(&ifaddr) != 0) {
      return -errno;
    }

    for (ifa = ifaddr; ifa && *count < max_interfaces; ifa = ifa->ifa_next) {
      network_interface_t *iface;
      int family;

      if (!ifa->ifa_addr || !ifa->ifa_name) {
        continue;
      }

      family = ifa->ifa_addr->sa_family;
      if (family != AF_INET && family != AF_INET6) {
        continue;
      }

      iface = &interfaces[*count];
      network_interface_clear(iface);
      copy_string(iface->name, sizeof(iface->name), ifa->ifa_name);
      iface->is_internal = (ifa->ifa_flags & IFF_LOOPBACK) ? 1 : 0;

      if (format_sockaddr(ifa->ifa_addr, iface->address, sizeof(iface->address)) != 0) {
        copy_string(iface->address, sizeof(iface->address), "unknown");
      }
      if (ifa->ifa_netmask &&
          format_sockaddr(ifa->ifa_netmask, iface->netmask, sizeof(iface->netmask)) != 0) {
        copy_string(iface->netmask, sizeof(iface->netmask), "unknown");
      }
      if (is_loopback_address(iface->address)) {
        iface->is_internal = 1;
      }
      ++(*count);
    }

    freeifaddrs(ifaddr);
    return *count > 0 ? 0 : -ENOENT;
  }
#endif
}
