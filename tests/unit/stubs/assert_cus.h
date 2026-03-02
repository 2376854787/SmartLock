#ifndef TEST_ASSERT_CUS_H
#define TEST_ASSERT_CUS_H

#define REQUIRE_RET(cond, ret) \
    do {                       \
        if (!(cond)) return (ret); \
    } while (0)

#define REQUIRE_RET_VOID(cond) \
    do {                       \
        if (!(cond)) return;   \
    } while (0)

#define ASSERT_PARAM(cond) ((void)(cond))
#define CORE_ASSERT(cond)  ((void)(cond))

#endif /* TEST_ASSERT_CUS_H */
