
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

static int foo = 0;

TEST(rh_init, t1)
{
	struct rhashtable my_table;
	
	struct rhashtable_params rht_params = {
		.head_offset = offsetof(struct object, head),
		.key_offset = offsetof(struct object, key),
		.key_len = sizeof(int),
	};
	
	int success = rhashtable_init(&my_table, &rht_params);

	EXPECT_TRUE(success != -EINVAL);
	
	rhashtable_destroy(&my_table);

	ASSERT_TRUE(false);
	foo = 1;
	EXPECT_TRUE(1 == 1);
}

static void add_tests(void)
{
	ADD_TEST(t1);
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
