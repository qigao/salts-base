#define Struct(...) 101
#define StructMeta(...) 102
#define FieldCount(...) 103
#define FieldMeta(...) 104
#define FieldFind(...) 105

#include "tlog.h"
#include "tinytest.h"

enum {
  host_struct_semantics = Struct(host_record, (int, value)),
  host_struct_meta_semantics = StructMeta(host_record),
  host_field_count_semantics = FieldCount(host_record),
  host_field_meta_semantics = FieldMeta(host_record, 0),
  host_field_find_semantics = FieldFind(host_record, value)
};

_Static_assert(host_struct_semantics == 101, "tlog.h replaced the host Struct macro");
_Static_assert(host_struct_meta_semantics == 102, "tlog.h replaced the host StructMeta macro");
_Static_assert(host_field_count_semantics == 103, "tlog.h replaced the host FieldCount macro");
_Static_assert(host_field_meta_semantics == 104, "tlog.h replaced the host FieldMeta macro");
_Static_assert(host_field_find_semantics == 105, "tlog.h replaced the host FieldFind macro");

typedef struct cmeta_log_entry_collision_layout {
  cmeta_log_level_t level;
  uint64_t timestamp_ms;
  uint32_t thread_id;
  vstr component;
  vstr file;
  int line;
  vstr message;
} cmeta_log_entry_collision_layout;

_Static_assert(sizeof(cmeta_log_entry_t) == sizeof(cmeta_log_entry_collision_layout),
               "cmeta_log_entry_t size changed under host macros");
_Static_assert(_Alignof(cmeta_log_entry_t) ==
                   _Alignof(cmeta_log_entry_collision_layout),
               "cmeta_log_entry_t alignment changed under host macros");
_Static_assert(offsetof(cmeta_log_entry_t, message) ==
                   offsetof(cmeta_log_entry_collision_layout, message),
               "cmeta_log_entry_t layout changed under host macros");

spec("TLog C public-header collisions") {
  it("preserves host macros and exposes log entry metadata") {
    const tlog_struct_desc *meta = cmeta_log_entry_t_meta();

    check_not_null(meta);
    check_equal(meta->name, "cmeta_log_entry_t");
    check_equal(meta->field_count, (size_t)7);
    check_equal(meta->fields[0].offset, offsetof(cmeta_log_entry_t, level));
    check_equal(meta->fields[6].offset, offsetof(cmeta_log_entry_t, message));
    check_equal(meta->fields[3].type_name, "vstr");
    check_equal(meta->fields[4].type_name, "vstr");
    check_equal(meta->fields[6].type_name, "vstr");
    check_null(tlog_struct_find_field(meta, "message_len"));
    check_equal(host_struct_semantics, 101);
    check_equal(host_field_find_semantics, 105);
  }
}
