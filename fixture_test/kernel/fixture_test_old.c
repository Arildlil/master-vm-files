
#include <linux/module.h>
#include "ktf.h"

MODULE_LICENSE("GPL");

KTF_INIT();

struct my_data {
	int value;
};

// Fixture setup
DECLARE_F(fixture_test)
	struct my_data data;
	int counter;
};

SETUP_F(fixture_test, fsetup)
{
	fixture_test->data.value = 3;
	fixture_test->counter = 0;
	fixture_test->ok = true;
}

TEARDOWN_F(fixture_test, fteardown)
{
	fixture_test->data.value = 0;
	fixture_test->counter = 0;
}

INIT_F(fixture_test, fsetup, fteardown);
// Done

TEST(simple, t1)
{
	EXPECT_TRUE(true);
}

TEST_F(fixture_test, ts, f2)
{
	ctx->data.value = 7;
	EXPECT_TRUE(ctx->data.value > 6);
	ctx->counter++;
	EXPECT_TRUE(ctx->counter == 1);
}

TEST_F(fixture_test, ts, f1)
{
	ctx->data.value += 1;
	EXPECT_TRUE(ctx->data.value == 4);
}

TEST_F(fixture_test, ts, f3)
{
	EXPECT_TRUE(ctx->data.value == 3);
	EXPECT_TRUE(ctx->counter == 0);
}

static void add_tests(void)
{
	ADD_TEST(t1);
	ADD_TEST(f2);
	ADD_TEST(f1);
	ADD_TEST(f3);
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
