
#include <linux/sort.h>
#include <linux/slab.h>
#include <linux/module.h>
#include "ktf.h"

KTF_INIT();

/* a simple boot-time regression test */

#define TEST_LEN 1000

static int __init cmpint(const void *a, const void *b)
{
	return *(int *)a - *(int *)b;
}

TEST(test_sort_rewrite, test_sort_init_2)
{
	int *a, i, r = 1, err = -ENOMEM;

	a = kmalloc_array(TEST_LEN, sizeof(*a), GFP_KERNEL);
	ASSERT_OK_ADDR(a);

	for (i = 0; i < TEST_LEN; i++) {
		r = (r * 725861) % 6599;
		a[i] = r;
	}

	sort(a, TEST_LEN, sizeof(*a), cmpint, NULL);

	err = -EINVAL;
	for (i = 0; i < TEST_LEN-1; i++)
		ASSERT_FALSE_GOTO(a[i] > a[i+1], exit);
	err = 0;
exit:
	kfree(a);
	ASSERT_INT_EQ(err, 0);
}

static int test_sort_init(void)
{
	ADD_TEST(test_sort_init_2);

	return 0;
}

static void __exit test_sort_exit(void)
{
	KTF_CLEANUP();
}

module_init(test_sort_init);
module_exit(test_sort_exit);

MODULE_LICENSE("GPL");
