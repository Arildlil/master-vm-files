from convert import Converter

source_directory = "/home/ubuntu/src/test_sort_rewrite/kernel/"
source_file_name = "test_sort_backup.c"
target_file_name = "test_sort_rewrite.c"
full_source_path = source_directory + source_file_name
full_target_path = source_directory + target_file_name

print("Converting " + source_file_name)
print("Input file path: " + full_source_path)
print("Output path: " + full_target_path)

with open(full_source_path, 'r') as f:
    data = f.read()



# |----------------------------------------------|
# | Conversion rules for "test_sort_rewrite.c" |
# |----------------------------------------------|

test_sort_rules = {
    "test_funcs":
    """
    test_sort_init
    """,
    
    "suite_name": "test_sort_rewrite",
    
    "blacklist": ["cmpint"],
    
    "replacements": [
        (
r"""if (!a)
		return err;""", "ASSERT_INT_NE(a,0);")
    ]
}

test_sort_rules_2 = {
    "test_funcs":
    """
    test_sort_init
    """,
    
    "blacklist": ["cmpint"],
    
    "should_add_new_main": True
}

state = Converter(data, full_target_path, test_sort_rules_2, True)
state.add_include_code() \
    .add_init_code_to_main() \
    .add_exit_code() \
    .convert_to_test_common_args() \
    .result()
    #.convert_to_test_common_args() \
    #.convert_calls_to_add_test() \
    #.convert_assertions() \
    #.result()