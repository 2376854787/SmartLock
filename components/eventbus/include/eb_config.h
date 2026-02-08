#ifndef SMARTLOCK_EB_CONFIG_H
#define SMARTLOCK_EB_CONFIG_H
#include <stdint.h>

/* 三队列深度：先用保守值，后续压测再调 */
#ifndef EB_QDEPTH_H
#define EB_QDEPTH_H   32u
#endif
#ifndef EB_QDEPTH_M
#define EB_QDEPTH_M   64u
#endif
#ifndef EB_QDEPTH_L
#define EB_QDEPTH_L   128u
#endif

/* L 配额：每一轮最多处理多少条 L（防止 L 洪水饿死 H/M） */
#ifndef EB_L_QUOTA_PER_ROUND
#define EB_L_QUOTA_PER_ROUND  8u
#endif

/* 统计与断言（Debug 可开） */
#ifndef EB_ENABLE_ASSERT
#define EB_ENABLE_ASSERT  1
#endif
#endif  // SMARTLOCK_EB_CONFIG_H
