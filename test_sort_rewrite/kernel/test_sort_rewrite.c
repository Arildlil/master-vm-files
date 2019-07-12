
#include <linux/sort.h>
#include <linux/slab.h>
#include <linux/module.h>
#include "ktf.h"
/* a simple boot-time regression test */

#define TEST_LEN 1000

static int cmpint(const void *a, const void *b)
{
	return *(int *)a - *(int *)b;
}

TEST(test_sort_init, test_sort_init) {

	int *a, i, r = 1, err = -ENOMEM;

	a = kmalloc_array(TEST_LEN, sizeof(*a), GFP_KERNEL);
	if (!a)
		ASSERT_INT_EQ(err, 0);

	for (i = 0; i < TEST_LEN; i++) {
		r = (r * 725861) % 6599;
		a[i] = r;
	}

	sort(a, TEST_LEN, sizeof(*a), cmpint, NULL);

	err = -EINVAL;
	for (i = 0; i < TEST_LEN-1; i++)
		if (a[i] > a[i+1]) {
			pr_err("test has failed\n");
			goto exit;
		}
	err = 0;
	pr_info("test passed\n");
exit:
	kfree(a);
	ASSERT_INT_EQ(err, 0);
}

static void test_sort_exit(void)
{
	KTF_CLEANUP();
}

KTF_INIT();

int test_sort_init_1(void)
{
	ADD_TEST(test_sort_init);

	return 0;
}

module_init(test_sort_init_1);
module_exit(test_sort_exit);

MODULE_LICENSE("GPL");
