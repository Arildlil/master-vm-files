from convert import Converter

source_directory = "/home/ubuntu/src/test_xarray_rewrite/kernel/"
source_file_name = "test_xarray_rewrite_old.c"
target_file_name = "test_xarray_rewrite.c"
full_source_path = source_directory + source_file_name
full_target_path = source_directory + target_file_name

print("Converting " + source_file_name)
print("Input file path: " + full_source_path)
print("Output path: " + full_target_path)

with open(full_source_path, 'r') as f:
    data = f.read()



# |----------------------------------------------|
# | Conversion rules for "test_xarray_rewrite.c" |
# |----------------------------------------------|

test_xarray_rules = {
    "test_funcs":
    """
    check_xa_err 
    check_xas_retry
	check_xa_load
	check_xa_mark 
    check_xa_shrink 
	check_xas_erase 
	check_cmpxchg 
	check_reserve 
	check_multi_store 
	check_xa_alloc 
	check_find 
	check_find_entry 
	check_account 
	check_destroy 
	check_move 
	check_create_range 
	check_store_range 
	check_store_iter
	check_workingset
    """,
    "init": 
r"""static struct array_context cxa = { .xa = &array };

KTF_INIT();

\g<1>
    \tKTF_CONTEXT_ADD(&cxa.k, "array");
""", 
    "exit":
r"""\g<1>
    struct ktf_context *pctx = KTF_CONTEXT_FIND("data");
    KTF_CONTEXT_REMOVE(pctx);

    KTF_CLEANUP();
""",
    "include": r"""\g<1>
#include "ktf.h" 

""",
    "new_types": r"""\g<1>

struct array_context {
    struct ktf_context k;
    struct xarray *xa;
};

""",
    "boilerplate": 
r"""\g<1>
        struct array_context *actx = KTF_CONTEXT_GET("array", struct array_context);
        struct xarray *xa = actx->xa;
    """,
    "suite_name": "test_xarray_rewrite",
    "ctx_args_def": "struct xarray [*]xa|void",
    "cmn_test_args_call": "&array",
    "extra_dummy_args_call": "xa",
    "blacklist": ["test_update_node", "xa_load", "xa_alloc", "xas_retry", "xa_err"],
    "replacements": [
        (r"(^\s*)(XA_BUG_ON[(]xa, *)", "\g<1>EXPECT_FALSE("),
        (r"(^\s*)(XA_BUG_ON[(]&xa0, *)", "\g<1>EXPECT_FALSE(")
    ],
    "should_add_new_main": False
}

state = Converter(data, full_target_path, test_xarray_rules, False)
state.add_include_code() \
    .add_init_code_to_main() \
    .add_exit_code() \
    .add_type_definitions() \
    .convert_to_test_common_args() \
    .convert_to_test_extra_args() \
    .convert_calls_to_add_test() \
    .add_boilerplate_test_code() \
    .add_extra_parameters_to_helpers_and_multi_arg_defs() \
    .add_self_argument_to_helper_calls() \
    .convert_assertions() \
    .result()