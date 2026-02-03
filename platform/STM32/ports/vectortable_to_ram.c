#include "vectortable_to_ram.h"

#include "stm32_hal.h"

/* 向量表大小 字*/
#define VECTOR_TABLE_SIZE 256

/* RAM 数组 */
__attribute__((aligned(512))) uint32_t g_ram_vector_table[VECTOR_TABLE_SIZE];

/* 声明 Flash 中原本的向量表（在启动文件 startup_stm32xxx.s 中定义的） */
extern const uint32_t g_pfnVectors[];

#include <string.h>  // for memcpy

void Move_Vector_Table_To_RAM(void) {
    /*１.关闭中断　*／
    __disable_irq();
    /*  2. 拷贝数据  */
    for (int i = 0; i < VECTOR_TABLE_SIZE; i++) {
        g_ram_vector_table[i] = g_pfnVectors[i];
    }
    /* 设置中断向量表地址 */
    SCB->VTOR = (uint32_t)g_ram_vector_table;
    /*  重新开启中断 */
    __enable_irq();
}