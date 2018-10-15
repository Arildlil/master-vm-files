#include <linux/module.h>
#include <linux/rhashtable.h>

#include "ktf.h"

MODULE_LICENSE("GPL");

KTF_INIT();

struct my_data {
	int data;
};

struct object {
	int key;
	struct rhash_head head;
	struct my_data data;
};

DECLARE_F(test_fixture)
    struct rhashtable shared_table;
    struct rhashtable_params params;
};

SETUP_F(test_fixture, test_setup)
{
    test_fixture->params.head_offset = offsetof(struct object, head);
    test_fixture->params.key_offset = offsetof(struct object, key);
    test_fixture->params.key_len = sizeof(int);
    test_fixture->ok = rhashtable_init(&test_fixture->shared_table,
        &test_fixture->params);
}

TEARDOWN_F(test_fixture, test_teardown)
{
    rhashtable_destroy(&test_fixture->shared_table);
}

INIT_F(test_fixture, test_setup, test_teardown);



TEST(rh_init, sfail)
{
    EXPECT_TRUE(1 == 234);
    EXPECT_TRUE(0 > 100);
}

TEST_F(test_fixture, rh_init, fix1)
{
    EXPECT_TRUE(1 == 234);
    EXPECT_TRUE(0 > 100);
}

TEST_F(test_fixture, rh_init, fix2)
{
    EXPECT_TRUE(ctx->ok == true);
}



static void add_tests(void)
{
    ADD_TEST(fix1);
    ADD_TEST(sfail);
    ADD_TEST(fix2);
}

static int __init rhashsuite_init(void)
{
    add_tests();
    return 0;
}

static void __exit rhashsuite_exit(void)
{
    KTF_CLEANUP();
}

module_init(rhashsuite_init);
module_exit(rhashsuite_exit);
