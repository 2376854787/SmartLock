#include "as608_service.h"

#include <string.h>

#include "cmsis_os2.h"

#include "as608_port.h"
#include "driver_as608_interface.h"

/* ============================ RTOS 配置 ============================ */

#ifndef AS608_SVC_QUEUE_DEPTH
#define AS608_SVC_QUEUE_DEPTH 4u
#endif

#ifndef AS608_SVC_TASK_STACK
#define AS608_SVC_TASK_STACK 1024u
#endif

#ifndef AS608_SVC_TASK_PRIO
#define AS608_SVC_TASK_PRIO osPriorityNormal
#endif

/* ============================ 内部命令定义 ============================ */

typedef enum
{
    CMD_INIT = 0,
    CMD_CREATE,
    CMD_READ,
    CMD_UPDATE,
    CMD_DELETE,
    CMD_CLEAR,
    CMD_LIST_INDEX,
} as608_cmd_t;

typedef struct
{
    as608_cmd_t cmd;
    uint32_t addr;
    uint32_t password;

    uint16_t id;
    uint8_t index_num;

    uint32_t timeout_ms;

    /* out */
    as608_status_t status;
    uint16_t found_id;
    uint16_t score;
    uint8_t index_table[32];

    /* sync */
    osSemaphoreId_t done;
    int32_t rc; /* as608_svc_rc_t */
} as608_req_t;

/* ============================ 模块上下文 ============================ */

static osMessageQueueId_t s_queue = NULL;
static osThreadId_t s_task = NULL;

static as608_handle_t s_handle;
static uint32_t s_addr = 0xFFFFFFFFu;
static uint16_t s_capacity = 300u; /* 默认 300；初始化后用 get_params 更新 */
static volatile bool s_ready = false;

static void as608_task(void *argument);

/* ============================ 工具函数 ============================ */

static inline uint32_t clamp_timeout(uint32_t t, uint32_t min_ms, uint32_t max_ms)
{
    if (t < min_ms) return min_ms;
    if (t > max_ms) return max_ms;
    return t;
}

static as608_svc_rc_t wait_done(osSemaphoreId_t sem, uint32_t timeout_ms)
{
    if (sem == NULL)
    {
        return AS608_SVC_ERR;
    }

    osStatus_t st = osSemaphoreAcquire(sem, timeout_ms);
    if (st == osOK)
    {
        return AS608_SVC_OK;
    }
    if (st == osErrorTimeout)
    {
        return AS608_SVC_TIMEOUT;
    }
    return AS608_SVC_ERR;
}

static uint8_t wait_finger_image(uint32_t addr, uint32_t timeout_ms, as608_status_t *status)
{
    /* 每 200ms 轮询一次 as608_get_image */
    const uint32_t step = 200u;
    uint32_t left = timeout_ms;

    while (left > 0)
    {
        uint8_t r = as608_get_image(&s_handle, addr, status);
        if (r != 0)
        {
            return r;
        }
        if (*status == AS608_STATUS_OK)
        {
            return 0;
        }
        as608_interface_delay_ms(step);
        left = (left > step) ? (left - step) : 0;
    }

    /* 超时：保持 status 原值（一般是 NO_FINGERPRINT） */
    return 0xFEu;
}

static void wait_finger_remove(uint32_t addr, uint32_t max_ms)
{
    /* 有些用户体验希望两次录入之间“拿开手指”，否则第二次采集可能还是同一张图 */
    const uint32_t step = 200u;
    uint32_t left = max_ms;
    as608_status_t st;

    while (left > 0)
    {
        (void)as608_get_image(&s_handle, addr, &st);
        if (st == AS608_STATUS_NO_FINGERPRINT)
        {
            return;
        }
        as608_interface_delay_ms(step);
        left = (left > step) ? (left - step) : 0;
    }
}

static uint8_t do_enroll_to_id(uint32_t addr, uint16_t id, uint32_t timeout_ms, as608_status_t *status)
{
    /*
     * 录入流程来自 example/driver_as608_basic.c（两次采集 -> 合并 -> 存储）。
     * 与示例不同：示例用 get_valid_template_number() 自动分配页号；这里用指定 id。
     */

    uint8_t r;

    timeout_ms = clamp_timeout(timeout_ms, 5000u, 60000u);

    /* 1) 第一次采集 */
    r = wait_finger_image(addr, timeout_ms, status);
    if (r == 0xFEu)
    {
        return 2; /* timeout */
    }
    if (r != 0)
    {
        return 1;
    }

    r = as608_generate_feature(&s_handle, addr, AS608_BUFFER_NUMBER_1, status);
    if (r != 0 || *status != AS608_STATUS_OK)
    {
        return 1;
    }

    /* 2) 提示用户移开手指（可选，但推荐） */
    wait_finger_remove(addr, 3000u);

    /* 3) 第二次采集 */
    r = wait_finger_image(addr, timeout_ms, status);
    if (r == 0xFEu)
    {
        return 2; /* timeout */
    }
    if (r != 0)
    {
        return 1;
    }

    r = as608_generate_feature(&s_handle, addr, AS608_BUFFER_NUMBER_2, status);
    if (r != 0 || *status != AS608_STATUS_OK)
    {
        return 1;
    }

    /* 4) 合并模板 */
    r = as608_combine_feature(&s_handle, addr, status);
    if (r != 0 || *status != AS608_STATUS_OK)
    {
        return 1;
    }

    /* 5) 存储到指定页号 id */
    r = as608_store_feature(&s_handle, addr, AS608_BUFFER_NUMBER_2, id, status);
    if (r != 0 || *status != AS608_STATUS_OK)
    {
        return 1;
    }

    return 0;
}

/* ============================ 对外 API ============================ */

as608_svc_rc_t AS608_Service_Init(uint32_t addr, uint32_t password)
{
    if (osKernelGetState() != osKernelRunning)
    {
        /* 建议在 osKernelStart 后调用；否则 delay_ms 退回 HAL_Delay 也能跑，但任务/队列不可用 */
    }

    if (s_queue == NULL)
    {
        /*
         * 队列里只放请求指针：
         * - 调用方在栈上构造 as608_req_t，然后把指针投递进队列；
         * - 任务线程直接在原结构体上写回结果；
         * - 调用方通过 semaphore 等待完成后读取结果字段。
         */
        s_queue = osMessageQueueNew(AS608_SVC_QUEUE_DEPTH, sizeof(as608_req_t *), NULL);
        if (s_queue == NULL)
        {
            return AS608_SVC_ERR;
        }
    }

    if (s_task == NULL)
    {
        const osThreadAttr_t attr = {
            .name = "as608",
            .priority = AS608_SVC_TASK_PRIO,
            .stack_size = AS608_SVC_TASK_STACK,
        };
        s_task = osThreadNew(as608_task, NULL, &attr);
        if (s_task == NULL)
        {
            return AS608_SVC_ERR;
        }
    }

    /* 发送 init 请求并等待完成 */
    as608_req_t req;
    memset(&req, 0, sizeof(req));
    req.cmd = CMD_INIT;
    req.addr = addr;
    req.password = password;
    req.timeout_ms = 2000u;

    req.done = osSemaphoreNew(1, 0, NULL);
    if (req.done == NULL)
    {
        return AS608_SVC_ERR;
    }

    as608_req_t *preq = &req;
    if (osMessageQueuePut(s_queue, &preq, 0, 0) != osOK)
    {
        osSemaphoreDelete(req.done);
        return AS608_SVC_ERR;
    }

    as608_svc_rc_t w = wait_done(req.done, 3000u);
    osSemaphoreDelete(req.done);

    if (w != AS608_SVC_OK)
    {
        return w;
    }

    return (as608_svc_rc_t)req.rc;
}

uint16_t AS608_Get_Capacity(void)
{
    return s_capacity;
}

as608_svc_rc_t AS608_CRUD_Create(uint16_t id, uint32_t timeout_ms, as608_status_t *out_status)
{
    if (!s_ready) return AS608_SVC_NOT_READY;

    as608_req_t req;
    memset(&req, 0, sizeof(req));
    req.cmd = CMD_CREATE;
    req.id = id;
    req.timeout_ms = timeout_ms;

    req.done = osSemaphoreNew(1, 0, NULL);
    if (req.done == NULL) return AS608_SVC_ERR;

    as608_req_t *preq = &req;
    if (osMessageQueuePut(s_queue, &preq, 0, 0) != osOK)
    {
        osSemaphoreDelete(req.done);
        return AS608_SVC_ERR;
    }

    as608_svc_rc_t w = wait_done(req.done, timeout_ms);
    if (out_status) *out_status = req.status;
    osSemaphoreDelete(req.done);

    if (w != AS608_SVC_OK) return w;
    return (as608_svc_rc_t)req.rc;
}

as608_svc_rc_t AS608_CRUD_Read(uint32_t timeout_ms,
                               uint16_t *out_found_id,
                               uint16_t *out_score,
                               as608_status_t *out_status)
{
    if (!s_ready) return AS608_SVC_NOT_READY;

    as608_req_t req;
    memset(&req, 0, sizeof(req));
    req.cmd = CMD_READ;
    req.timeout_ms = timeout_ms;

    req.done = osSemaphoreNew(1, 0, NULL);
    if (req.done == NULL) return AS608_SVC_ERR;

    as608_req_t *preq = &req;
    if (osMessageQueuePut(s_queue, &preq, 0, 0) != osOK)
    {
        osSemaphoreDelete(req.done);
        return AS608_SVC_ERR;
    }

    as608_svc_rc_t w = wait_done(req.done, timeout_ms);
    if (out_found_id) *out_found_id = req.found_id;
    if (out_score) *out_score = req.score;
    if (out_status) *out_status = req.status;
    osSemaphoreDelete(req.done);

    if (w != AS608_SVC_OK) return w;
    return (as608_svc_rc_t)req.rc;
}

as608_svc_rc_t AS608_CRUD_Update(uint16_t id, uint32_t timeout_ms, as608_status_t *out_status)
{
    if (!s_ready) return AS608_SVC_NOT_READY;

    as608_req_t req;
    memset(&req, 0, sizeof(req));
    req.cmd = CMD_UPDATE;
    req.id = id;
    req.timeout_ms = timeout_ms;

    req.done = osSemaphoreNew(1, 0, NULL);
    if (req.done == NULL) return AS608_SVC_ERR;

    as608_req_t *preq = &req;
    if (osMessageQueuePut(s_queue, &preq, 0, 0) != osOK)
    {
        osSemaphoreDelete(req.done);
        return AS608_SVC_ERR;
    }

    as608_svc_rc_t w = wait_done(req.done, timeout_ms);
    if (out_status) *out_status = req.status;
    osSemaphoreDelete(req.done);

    if (w != AS608_SVC_OK) return w;
    return (as608_svc_rc_t)req.rc;
}

as608_svc_rc_t AS608_CRUD_Delete(uint16_t id, as608_status_t *out_status)
{
    if (!s_ready) return AS608_SVC_NOT_READY;

    as608_req_t req;
    memset(&req, 0, sizeof(req));
    req.cmd = CMD_DELETE;
    req.id = id;
    req.timeout_ms = 2000u;

    req.done = osSemaphoreNew(1, 0, NULL);
    if (req.done == NULL) return AS608_SVC_ERR;

    as608_req_t *preq = &req;
    if (osMessageQueuePut(s_queue, &preq, 0, 0) != osOK)
    {
        osSemaphoreDelete(req.done);
        return AS608_SVC_ERR;
    }

    as608_svc_rc_t w = wait_done(req.done, 3000u);
    if (out_status) *out_status = req.status;
    osSemaphoreDelete(req.done);

    if (w != AS608_SVC_OK) return w;
    return (as608_svc_rc_t)req.rc;
}

as608_svc_rc_t AS608_CRUD_ClearAll(as608_status_t *out_status)
{
    if (!s_ready) return AS608_SVC_NOT_READY;

    as608_req_t req;
    memset(&req, 0, sizeof(req));
    req.cmd = CMD_CLEAR;
    req.timeout_ms = 3000u;

    req.done = osSemaphoreNew(1, 0, NULL);
    if (req.done == NULL) return AS608_SVC_ERR;

    as608_req_t *preq = &req;
    if (osMessageQueuePut(s_queue, &preq, 0, 0) != osOK)
    {
        osSemaphoreDelete(req.done);
        return AS608_SVC_ERR;
    }

    as608_svc_rc_t w = wait_done(req.done, 5000u);
    if (out_status) *out_status = req.status;
    osSemaphoreDelete(req.done);

    if (w != AS608_SVC_OK) return w;
    return (as608_svc_rc_t)req.rc;
}

as608_svc_rc_t AS608_List_IndexTable(uint8_t num, uint8_t out_table[32], as608_status_t *out_status)
{
    if (!s_ready) return AS608_SVC_NOT_READY;

    as608_req_t req;
    memset(&req, 0, sizeof(req));
    req.cmd = CMD_LIST_INDEX;
    req.index_num = num;
    req.timeout_ms = 2000u;

    req.done = osSemaphoreNew(1, 0, NULL);
    if (req.done == NULL) return AS608_SVC_ERR;

    as608_req_t *preq = &req;
    if (osMessageQueuePut(s_queue, &preq, 0, 0) != osOK)
    {
        osSemaphoreDelete(req.done);
        return AS608_SVC_ERR;
    }

    as608_svc_rc_t w = wait_done(req.done, 3000u);
    if (out_status) *out_status = req.status;
    if (out_table) memcpy(out_table, req.index_table, 32);
    osSemaphoreDelete(req.done);

    if (w != AS608_SVC_OK) return w;
    return (as608_svc_rc_t)req.rc;
}

/* ============================ 任务实现 ============================ */

static void do_init(as608_req_t *req)
{
    /* link interface */
    DRIVER_AS608_LINK_INIT(&s_handle, as608_handle_t);
    DRIVER_AS608_LINK_UART_INIT(&s_handle, as608_interface_uart_init);
    DRIVER_AS608_LINK_UART_DEINIT(&s_handle, as608_interface_uart_deinit);
    DRIVER_AS608_LINK_UART_READ(&s_handle, as608_interface_uart_read);
    DRIVER_AS608_LINK_UART_WRITE(&s_handle, as608_interface_uart_write);
    DRIVER_AS608_LINK_UART_FLUSH(&s_handle, as608_interface_uart_flush);
    DRIVER_AS608_LINK_DELAY_MS(&s_handle, as608_interface_delay_ms);
    DRIVER_AS608_LINK_DEBUG_PRINT(&s_handle, as608_interface_debug_print);

    s_addr = req->addr;

    s_ready = false;

    uint8_t r = as608_init(&s_handle, s_addr);
    if (r != 0)
    {
        req->rc = AS608_SVC_ERR;
        return;
    }

    /* 校验密码（默认 0x00000000） */
    as608_status_t st;
    r = as608_verify_password(&s_handle, s_addr, req->password, &st);
    req->status = st;
    if (r != 0 || st != AS608_STATUS_OK)
    {
        req->rc = AS608_SVC_ERR;
        return;
    }

    /* 读取参数，获取容量（fingerprint_size） */
    as608_params_t p;
    r = as608_get_params(&s_handle, s_addr, &p, &st);
    if (r == 0 && st == AS608_STATUS_OK)
    {
        /* 这里的 fingerprint_size 在库里被称为“指纹容量” */
        if (p.fingerprint_size != 0)
        {
            s_capacity = p.fingerprint_size;
        }
    }

    s_ready = true;
    req->rc = AS608_SVC_OK;
}

static void do_create(as608_req_t *req)
{
    if (req->id >= s_capacity)
    {
        req->status = AS608_STATUS_LIB_ADDR_OVER;
        req->rc = AS608_SVC_ERR;
        return;
    }

    uint8_t r = do_enroll_to_id(s_addr, req->id, req->timeout_ms, &req->status);
    if (r == 0)
    {
        req->rc = AS608_SVC_OK;
    }
    else if (r == 2)
    {
        req->rc = AS608_SVC_TIMEOUT;
    }
    else
    {
        req->rc = AS608_SVC_ERR;
    }
}

static void do_read(as608_req_t *req)
{
    as608_status_t st;

    /* 1) get image */
    uint8_t r = wait_finger_image(s_addr, req->timeout_ms, &st);
    req->status = st;
    if (r == 0xFEu)
    {
        req->rc = AS608_SVC_TIMEOUT;
        return;
    }
    if (r != 0 || st != AS608_STATUS_OK)
    {
        req->rc = AS608_SVC_ERR;
        return;
    }

    /* 2) gen feature -> buffer 1 */
    r = as608_generate_feature(&s_handle, s_addr, AS608_BUFFER_NUMBER_1, &st);
    req->status = st;
    if (r != 0 || st != AS608_STATUS_OK)
    {
        req->rc = AS608_SVC_ERR;
        return;
    }

    /* 3) search */
    uint16_t found = 0;
    uint16_t score = 0;

    r = as608_search_feature(&s_handle, s_addr, AS608_BUFFER_NUMBER_1, 0, s_capacity, &found, &score, &st);
    req->status = st;
    if (r != 0)
    {
        req->rc = AS608_SVC_ERR;
        return;
    }

    if (st == AS608_STATUS_OK)
    {
        req->found_id = found;
        req->score = score;
        req->rc = AS608_SVC_OK;
    }
    else if (st == AS608_STATUS_NOT_FOUND)
    {
        req->rc = AS608_SVC_OK; /* 业务上可认为“读到了：未匹配” */
    }
    else
    {
        req->rc = AS608_SVC_ERR;
    }
}

static void do_update(as608_req_t *req)
{
    if (req->id >= s_capacity)
    {
        req->status = AS608_STATUS_LIB_ADDR_OVER;
        req->rc = AS608_SVC_ERR;
        return;
    }

    /* 1) delete one */
    as608_status_t st;
    uint8_t r = as608_delete_feature(&s_handle, s_addr, req->id, 1, &st);
    req->status = st;
    if (r != 0)
    {
        req->rc = AS608_SVC_ERR;
        return;
    }

    /* 即使 NOT_FOUND，也允许继续录入覆盖 */

    /* 2) create */
    r = do_enroll_to_id(s_addr, req->id, req->timeout_ms, &st);
    req->status = st;
    if (r == 0)
    {
        req->rc = AS608_SVC_OK;
    }
    else if (r == 2)
    {
        req->rc = AS608_SVC_TIMEOUT;
    }
    else
    {
        req->rc = AS608_SVC_ERR;
    }
}

static void do_delete(as608_req_t *req)
{
    if (req->id >= s_capacity)
    {
        req->status = AS608_STATUS_LIB_ADDR_OVER;
        req->rc = AS608_SVC_ERR;
        return;
    }

    as608_status_t st;
    uint8_t r = as608_delete_feature(&s_handle, s_addr, req->id, 1, &st);
    req->status = st;
    if (r == 0)
    {
        req->rc = AS608_SVC_OK;
    }
    else
    {
        req->rc = AS608_SVC_ERR;
    }
}

static void do_clear(as608_req_t *req)
{
    as608_status_t st;
    uint8_t r = as608_empty_all_feature(&s_handle, s_addr, &st);
    req->status = st;
    req->rc = (r == 0 && st == AS608_STATUS_OK) ? AS608_SVC_OK : AS608_SVC_ERR;
}

static void do_list_index(as608_req_t *req)
{
    as608_status_t st;
    uint8_t table[32];

    uint8_t r = as608_get_index_table(&s_handle, s_addr, req->index_num, table, &st);
    req->status = st;

    if (r == 0 && st == AS608_STATUS_OK)
    {
        memcpy(req->index_table, table, 32);
        req->rc = AS608_SVC_OK;
    }
    else
    {
        req->rc = AS608_SVC_ERR;
    }
}

static void as608_task(void *argument)
{
    (void)argument;

    for (;;)
    {
        as608_req_t *req = NULL;

        if (osMessageQueueGet(s_queue, &req, NULL, osWaitForever) != osOK)
        {
            continue;
        }

        if (req == NULL)
        {
            continue;
        }

        switch (req->cmd)
        {
            case CMD_INIT:
                do_init(req);
                break;
            case CMD_CREATE:
                do_create(req);
                break;
            case CMD_READ:
                do_read(req);
                break;
            case CMD_UPDATE:
                do_update(req);
                break;
            case CMD_DELETE:
                do_delete(req);
                break;
            case CMD_CLEAR:
                do_clear(req);
                break;
            case CMD_LIST_INDEX:
                do_list_index(req);
                break;
            default:
                req->rc = AS608_SVC_ERR;
                break;
        }

        /* 通过 semaphore 通知调用方完成（req 指向调用方栈上的请求结构体） */
        if (req->done != NULL)
        {
            (void)osSemaphoreRelease(req->done);
        }
    }
}
