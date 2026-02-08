#include <stdbool.h>

#include "RingBuffer.h"
#include "eb_types.h"
bool eb_port_mailbox_push(void* mailbox, const eb_event_t* ev) {
    RingBuffer* rb      = (RingBuffer*)mailbox;
    uint32_t n          = (uint32_t)sizeof(eb_event_t);
    const ret_code_t rc = WriteRingBuffer_SPSC(rb, (const uint8_t*)ev, &n, 0);
    /* 必须要求：写入成功且写入字节数 == sizeof(event) */
    return (rc == RET_OK) && (n == sizeof(eb_event_t));
}

bool eb_port_mailbox_pop(void* mailbox, eb_event_t* out) {
    RingBuffer* rb      = (RingBuffer*)mailbox;
    uint32_t n          = (uint32_t)sizeof(eb_event_t);
    const ret_code_t rc = ReadRingBuffer_SPSC(rb, (uint8_t*)out, &n, 0);
    return (rc == RET_OK) && (n == sizeof(eb_event_t));
}
// 模块Task用