
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
	struct my_data data;
	struct rhash_head head;
};

static struct rhashtable_params rht_params = {
	.head_offset = offsetof(struct object, head),
	.key_offset = offsetof(struct object, key),
	.key_len = sizeof(int),
};



// Fixture setup
DECLARE_F(fixture_test)
	struct rhashtable my_table;
};

SETUP_F(fixture_test, fsetup)
{
	int success = rhashtable_init(&fixture_test->my_table,
		&rht_params);
	fixture_test->ok = true;
}

TEARDOWN_F(fixture_test, fteardown)
{
	rhashtable_destroy(&fixture_test->my_table);
}

INIT_F(fixture_test, fsetup, fteardown);
// Done


TEST(simple, t1)
{
	EXPECT_TRUE(true);
}

TEST_F(fixture_test, ts, f1)
{
	struct object obj = {
		.key = 1,
		.data = {123},
	};
	EXPECT_TRUE(atomic_read(&ctx->my_table.nelems) == 0);

	rhashtable_insert_fast(&ctx->my_table, &obj.head, rht_params);
	EXPECT_TRUE(atomic_read(&ctx->my_table.nelems) == 1);
}

TEST_F(fixture_test, ts, f2)
{
	EXPECT_TRUE(atomic_read(&ctx->my_table.nelems) == 0);
}

static void add_tests(void)
{
	ADD_TEST(t1);
	ADD_TEST(f1);
	ADD_TEST(f2);
}

static int __init fixture_test_init(void)
{
	add_tests();
	return 0;
}
static void __exit fixture_test_exit(void)
{
	KTF_CLEANUP();
}

module_init(fixture_test_init);
module_exit(fixture_test_exit);
