#include <cserde/writer.h>

#include <stdbool.h>
#include <stddef.h>

#define CSERDE_FIELD_END(type, field) \
    (offsetof(type, field) + sizeof(((type *)0)->field))

static bool cserde_writer_ops_valid(const cserde_writer_ops *ops) {
    return ops != NULL &&
           ops->struct_size >= CSERDE_FIELD_END(cserde_writer_ops, finish) &&
           ops->abi_version == CSERDE_WRITER_OPS_ABI_VERSION &&
           ops->write != NULL &&
           ops->finish != NULL;
}

static bool cserde_writer_callback_status_valid(cserde_status status) {
    switch (status) {
        case CSERDE_OK:
        case CSERDE_LIMIT_EXCEEDED:
        case CSERDE_UNSUPPORTED:
        case CSERDE_SINK_ERROR:
            return true;
        default:
            return false;
    }
}

static cserde_status cserde_writer_fail(cserde_writer *writer,
                                        cserde_status status) {
    writer->state = CSERDE_WRITER_FAILED;
    writer->status = status;
    return status;
}

cserde_status cserde_writer_init(cserde_writer *writer,
                                  const cserde_writer_ops *ops,
                                  void *context) {
    cserde_writer initialized;

    if (writer == NULL || ops == NULL)
        return CSERDE_INVALID_ARGUMENT;
    if (writer->ops != NULL || writer->context != NULL ||
        writer->state != CSERDE_WRITER_ZERO || writer->status != CSERDE_OK)
        return CSERDE_INVALID_STATE;
    if (!cserde_writer_ops_valid(ops))
        return CSERDE_INVALID_ARGUMENT;

    initialized.ops = ops;
    initialized.context = context;
    initialized.state = CSERDE_WRITER_READY;
    initialized.status = CSERDE_OK;
    *writer = initialized;
    return CSERDE_OK;
}

cserde_status cserde_writer_write(cserde_writer *writer,
                                   const cserde_token *token) {
    cserde_status status;

    if (writer == NULL || token == NULL)
        return CSERDE_INVALID_ARGUMENT;
    if (writer->state == CSERDE_WRITER_FAILED)
        return writer->status;
    if (writer->state != CSERDE_WRITER_READY)
        return CSERDE_INVALID_STATE;
    if (!cserde_token_valid(token))
        return CSERDE_INVALID_TOKEN;

    status = writer->ops->write(writer->context, token);
    if (!cserde_writer_callback_status_valid(status))
        return cserde_writer_fail(writer, CSERDE_CALLBACK_ERROR);
    if (status == CSERDE_OK)
        return CSERDE_OK;
    return cserde_writer_fail(writer, status);
}

cserde_status cserde_writer_finish(cserde_writer *writer) {
    cserde_status status;

    if (writer == NULL)
        return CSERDE_INVALID_ARGUMENT;
    if (writer->state == CSERDE_WRITER_FAILED)
        return writer->status;
    if (writer->state != CSERDE_WRITER_READY)
        return CSERDE_INVALID_STATE;

    status = writer->ops->finish(writer->context);
    if (!cserde_writer_callback_status_valid(status))
        return cserde_writer_fail(writer, CSERDE_CALLBACK_ERROR);
    if (status != CSERDE_OK)
        return cserde_writer_fail(writer, status);

    writer->state = CSERDE_WRITER_FINISHED;
    writer->status = CSERDE_OK;
    return CSERDE_OK;
}
