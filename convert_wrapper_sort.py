from convert import Converter

source_directory = "/home/ubuntu/src/test_sort_rewrite/kernel/"
source_file_name = "test_sort_backup.c"
target_file_name = "test_sort_rewrite.c"
full_source_path = source_directory + source_file_name
full_target_path = source_directory + target_file_name

print("Converting " + source_file_name)
print("Input file path: " + full_source_path)
print("Output path: " + full_target_path)



# |----------------------------------------------|
# | Conversion rules for "test_sort_rewrite.c" |
# |----------------------------------------------|

test_sort_rules_2 = {
    "test_functions":
    ["test_sort_init"],

    "test_suite_name": "test_sort_rewrite",
    
    "blacklist": ["cmpint"],

    "replacements": [
        ("return err;", "ASSERT_INT_EQ(err, 0);")
    ],
    
    "should_add_new_main": True
}

state = Converter(full_source_path, full_target_path, test_sort_rules_2, True)
state.add_include_code() \
    .add_init_code_to_main() \
    .add_exit_code() \
    .convert_to_test_common_args() \
    .use_replacements() \
    .result()
