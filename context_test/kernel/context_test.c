
#include <linux/module.h>
#include "ktf.h"

MODULE_LICENSE("GPL");

KTF_INIT();

struct my_ctx {
	struct ktf_context k;
	int counter;
};

static struct my_ctx some_ctx = { .counter = 1 };


TEST(simple, t1)
{
	struct my_ctx *data_ctx = KTF_CONTEXT_GET("data", struct my_ctx);
	struct my_ctx *no_ctx = KTF_CONTEXT_GET("invalid", struct my_ctx);

	EXPECT_TRUE(data_ctx != NULL);
	EXPECT_TRUE(data_ctx->counter == 1);
	data_ctx->counter++;

	EXPECT_TRUE(no_ctx == NULL);
}

TEST(simple, t2)
{
	struct my_ctx *data_ctx = KTF_CONTEXT_GET("data", struct my_ctx);
	EXPECT_TRUE(data_ctx->counter == 2);
	data_ctx->counter += 3;
}

TEST(simple, t3)
{
	struct my_ctx *data_ctx = KTF_CONTEXT_GET("data", struct my_ctx);
	EXPECT_TRUE(data_ctx->counter == 5);
}

static void add_tests(void)
{
	KTF_CONTEXT_ADD(&some_ctx.k, "data");

	ADD_TEST(t1);
	ADD_TEST(t2);
	ADD_TEST(t3);
}

static int __init context_test_init(void)
{
	add_tests();
	return 0;
}
static void __exit context_test_exit(void)
{
	struct ktf_context *pctx = KTF_CONTEXT_FIND("data");
	KTF_CONTEXT_REMOVE(pctx);

	KTF_CLEANUP();
}

module_init(context_test_init);
module_exit(context_test_exit);
