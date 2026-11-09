#ifndef SALTS_H
#define SALTS_H

/* Core SDK umbrella. Other modules expose their own public headers. */
#include <salts/api.h>
#include <salts/error_codes.h>
#include <atomic/atomic.h>

#include <threads/threads.h>
#include <threads/threads_types.h>
#include <queue/ring_buffer.h>
#include <queue/spsc_ring.h>
#include <queue/ring_buffer_spsc.h>
#include <queue/bucket_priority_queue.h>
#include <queue/bucket_priority_queue_spsc.h>
#include <queue/bucket_priority_queue_mpmc.h>
#include <queue/deadline_queue.h>
#include <queue/disruptor.h>
#include <threads/thread_pool.h>

#include <salts/base64.h>
#include <memory/buffer.h>
#include <salts/bytes.h>
#include <salts/clock.h>
#include <salts/error.h>
#include <salts/fmt.h>
#include <file/fs.h>
#include <file/fs_watch.h>
#include <memory/memory_pool.h>
#include <memory/mmap.h>
#include <memory/object_pool.h>
#include <salts/platform.h>
#include <salts/process.h>
#include <salts/rcu.h>
#include <salts/re.h>
#include <salts/scan.h>
#include <salts/tlog.h>
#include <salts/tlog_helper.h>
#include <salts/tstr.h>
#include <salts/utf8.h>
#include <salts/uuid.h>
#include <salts/vstr.h>
#include <salts/zstd.h>

#endif /* SALTS_H */
