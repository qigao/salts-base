#ifndef SALTS_DISRUPTOR_H
#define SALTS_DISRUPTOR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct disruptor_s disruptor_t;
typedef struct disruptor_topology_s disruptor_topology_t;

#define DISRUPTOR_STAGE_INVALID UINT32_MAX
#define DISRUPTOR_GROUP_INVALID UINT32_MAX

typedef uint32_t disruptor_stage_t;
typedef uint32_t disruptor_group_t;

typedef struct {
  uint64_t sequence;
} disruptor_cursor_t;

typedef struct {
  uint64_t first_sequence;
  uint64_t last_sequence;
} disruptor_sequence_range_t;

typedef struct {
  uint32_t slot;
} disruptor_consumer_t;

typedef enum { DISRUPTOR_MODE_BROADCAST = 0, DISRUPTOR_MODE_WORKER_POOL = 1 } disruptor_mode_t;

typedef int (*disruptor_keep_running_fn)(void *ctx);
typedef disruptor_keep_running_fn disruptor_should_run_fn;

typedef struct {
  size_t entry_size;
  uint64_t capacity;
  uint32_t consumer_capacity;
  disruptor_mode_t mode;
} disruptor_config_t;

disruptor_t *disruptor_create(const disruptor_config_t *config);
void disruptor_destroy(disruptor_t *disruptor);
int disruptor_reset(disruptor_t *disruptor);
uint64_t disruptor_capacity(const disruptor_t *disruptor);
size_t disruptor_entry_size(const disruptor_t *disruptor);
void *disruptor_acquire_entry(disruptor_t *disruptor, const disruptor_cursor_t *cursor);
const void *disruptor_show_entry(const disruptor_t *disruptor, const disruptor_cursor_t *cursor);

int disruptor_publisher_try_claim(disruptor_t *disruptor, disruptor_cursor_t *cursor);
int disruptor_publisher_try_claim_n(disruptor_t *disruptor, uint32_t count,
                                    disruptor_sequence_range_t *range);
void disruptor_publisher_next_entry_blocking(disruptor_t *disruptor, disruptor_cursor_t *cursor);
void *disruptor_publisher_next_entry_and_acquire_blocking(disruptor_t *disruptor,
                                                          disruptor_cursor_t *cursor);
int disruptor_publisher_claim_n_blocking(disruptor_t *disruptor, uint32_t count,
                                         disruptor_sequence_range_t *range);
int disruptor_publisher_try_commit(disruptor_t *disruptor, const disruptor_cursor_t *cursor);
void disruptor_publisher_commit_entry_blocking(disruptor_t *disruptor,
                                               const disruptor_cursor_t *cursor);
int disruptor_publisher_publish_range(disruptor_t *disruptor,
                                      const disruptor_sequence_range_t *range);
void disruptor_publisher_commit_range_blocking(disruptor_t *disruptor,
                                               const disruptor_sequence_range_t *range);
int disruptor_publisher_publish(disruptor_t *disruptor, const disruptor_cursor_t *cursor);

int disruptor_consumer_try_register(disruptor_t *disruptor, disruptor_consumer_t *consumer,
                                    uint64_t *next_sequence);
uint64_t disruptor_consumer_register(disruptor_t *disruptor, disruptor_consumer_t *consumer);
void disruptor_consumer_unregister(disruptor_t *disruptor, const disruptor_consumer_t *consumer);
int disruptor_consumer_wait_for_nonblocking(const disruptor_t *disruptor,
                                            disruptor_cursor_t *cursor);
void disruptor_consumer_wait_for_blocking(const disruptor_t *disruptor, disruptor_cursor_t *cursor);
int disruptor_consumer_wait_for_nonblocking_for(const disruptor_t *disruptor,
                                                const disruptor_consumer_t *consumer,
                                                disruptor_cursor_t *cursor);
void disruptor_consumer_wait_for_blocking_for(const disruptor_t *disruptor,
                                              const disruptor_consumer_t *consumer,
                                              disruptor_cursor_t *cursor);
void disruptor_consumer_release_entry(disruptor_t *disruptor, const disruptor_consumer_t *consumer,
                                      const disruptor_cursor_t *cursor);
int disruptor_consumer_set_dependencies(disruptor_t *disruptor,
                                        const disruptor_consumer_t *consumer,
                                        const disruptor_consumer_t *dependencies,
                                        uint32_t dependency_count);

disruptor_topology_t *disruptor_topology_create(disruptor_t *disruptor);
void disruptor_topology_destroy(disruptor_topology_t *topology);
disruptor_stage_t disruptor_topology_stage(disruptor_topology_t *topology, const char *name,
                                           const disruptor_consumer_t *consumer);
disruptor_group_t disruptor_topology_group(disruptor_topology_t *topology, const char *name,
                                           const disruptor_stage_t *stages, uint32_t stage_count);
int disruptor_topology_after(disruptor_topology_t *topology, disruptor_stage_t stage,
                             disruptor_stage_t dependency);
int disruptor_topology_after_all(disruptor_topology_t *topology, disruptor_stage_t stage,
                                 const disruptor_stage_t *dependencies, uint32_t dependency_count);
int disruptor_topology_stage_after_group(disruptor_topology_t *topology, disruptor_stage_t stage,
                                         disruptor_group_t dependency_group);
int disruptor_topology_group_after(disruptor_topology_t *topology, disruptor_group_t group,
                                   disruptor_stage_t dependency);
int disruptor_topology_group_after_group(disruptor_topology_t *topology, disruptor_group_t group,
                                         disruptor_group_t dependency_group);
int disruptor_topology_chain(disruptor_topology_t *topology, const disruptor_stage_t *stages,
                             uint32_t stage_count);
int disruptor_topology_commit(disruptor_topology_t *topology);

int disruptor_worker_try_claim(disruptor_t *disruptor, disruptor_cursor_t *cursor);
void disruptor_worker_claim_blocking(disruptor_t *disruptor, disruptor_cursor_t *cursor);
int disruptor_worker_claim_wait(disruptor_t *disruptor, disruptor_cursor_t *cursor,
                                disruptor_keep_running_fn keep_running, void *ctx);
void disruptor_worker_wake_all(disruptor_t *disruptor);
void disruptor_worker_release_entry(disruptor_t *disruptor, const disruptor_cursor_t *cursor);

typedef void (*disruptor_batch_fn)(void *ctx, uint64_t first_seq, uint64_t last_seq);

void disruptor_consumer_run(disruptor_t *disruptor, disruptor_consumer_t *consumer,
                            disruptor_keep_running_fn keep_running,
                            disruptor_batch_fn process_batch, void *ctx);


#ifdef __cplusplus
}
#endif

#endif /* SALTS_DISRUPTOR_H */
