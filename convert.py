import re, os, argparse, sys
from pprint import pprint
from collections import namedtuple
from string import whitespace


"""
ConversionRules = namedtuple('ConversionRules', 
    'test_funcs init exit include new_types boilerplate suite_name ctx_args_def \
    cmn_test_args_call extra_dummy_args_call blacklist replacements')
"""

# Argsparse related code
# -------------------------------------------------------------------

default_filename = "/home/ubuntu/src/test_xarray_rewrite/kernel/test_xarray_rewrite.c"

parser = argparse.ArgumentParser(
    description='A python script hand crafted for converting "test_xarray.c" to KTF.')
parser.add_argument('-f', '--file', action='store', default=default_filename)
parser.add_argument('-o', '--out', action='store')
args = parser.parse_args()

with open(args.file, 'r') as f:
    data = f.read()


# Code local for one specific test file. This could be supplied in
# other ways, but currently it will be written here.
# -------------------------------------------------------------------

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




def printr(matches):
    print("\n\t" + matches.group(1))
    for match in matches.groups()[1:]:
        print("\t\t" + str(match))
    return matches.group(1)



    """
    def __init__(self, text, test_func_names="", outfile_name, test_suite_name="", 
        ignore_func_names="", init_code="", exit_code="", include_code="", type_def_code="",
        boilerplate_test_code="", ctx_args="", common_call_args="",
        common_call_multi_arg_replace="", assertion_replacements="", add_new_main=False, debug=True):
    """
class CurrentState(object):
    """
    Class used to store and handle parameters from the user,
    as well as information about the file in question. For example,
    instead of repeatedly using a regex to find all locally defined
    functions, this can be done once and stored here for future use.

    The most important fields to specify, for the 'rules' dict argument,
    is the following:
        ["test_funcs"], # Names of the test functions to convert to the TEST macro.
        ["blacklist"]   # Names of functions that should NOT be converted to the TEST macro.
    The class will add some KTF related code, even if an empty dict is passed
    as the 'rules' argument, although little code will be added in that case.
    """

    def __init__(self, text, outfile_name, rules, debug=False):

        # Default KTF snippets
        # --------------------
        self._default_init_code = "KTF_INIT();\n\n\g<1>"
        self._default_exit_code = '\g<1>\n\tKTF_CLEANUP();'
        self._default_include_code = '\g<1>\n#include "ktf.h"\n'
        self._default_cmn_test_args_call = ""
        self._default_context_args = "void|''"

        # Argument handling:
        # ------------------
        # All the contents of the source file.
        self._text = text

        # The name of the file to write output to.
        self._outfile_name = outfile_name

        # Names of all the functions to convert to TEST.
        self._test_function_names = rules.get("test_funcs")

        # Code to be added to the init function of the module, if any.
        self._init_code = rules.get("init") or self._default_init_code
        
        # Code to be added to the exit function of the module, if any.
        self._exit_code = rules.get("exit") or self._default_exit_code

        # Code for header inclusion
        self._include_code = rules.get("include") or self._default_include_code

        # Code for definition of structs and typedefs.
        self._type_def_code = rules.get("new_types")
                
        # Context code that should be added to the beginning
        # of every TEST function.
        self._boilerplate_test_code = rules.get("boilerplate")

        # The name of the test suite itself.
        self._test_suite_name = rules.get("suite_name")

        # Common arguments used by most test functions, which could be moved 
        # into a context instead. The default value is "void|''", meaning
        # no arguments. 
        self._context_args = rules.get("ctx_args_def") or self._default_context_args

        # Common arguments often used when calling test functions. For the xarray suite,
        # this is the '&array' argument supplied to all test functions. For functions
        # without arguments, this could be '' or 'void'.
        self._common_call_args = rules.get("cmn_test_args_call") or self._default_cmn_test_args_call

        # Replacement for _common_call_args for multi argument function calls
        # that needs to be put in a dummy function.
        self._common_call_multi_arg_replace = rules.get("extra_dummy_args_call")

        # The names of functions to be ignored.
        self._ignore_func_names = rules.get("blacklist")

        # Specifies which assertions to convert to which KTF assertions.
        self._assertion_replacements = rules.get("replacements")

        # Adds a new main function if set to True. The name of this new main 
        # function will be the function name argument in the call 'module_init' 
        # with "_1" appended to the end (specified by self._new_main_name). 
        # For example "test_xarray_init" -> "test_xarray_init_1"
        self._should_add_new_main = rules.get("should_add_new_main")

        self._debug = debug


        # Other object variables used:
        # ----------------------------
        # Will store the names of all local functions defined in
        # the file.
        self._local_function_names = {}

        # Will store the locally defined helper functions, defined
        # as all functions that have not been declared as test 
        # functions.
        self._local_helper_function_names = {}

        # Counter that is concatenated to the name of a dummy 
        # function to (hopefully) ensure unique names.
        self._dummy_function_counter = 1

        # A dict of prefix numbers to make sure that the tests are
        # run in the same order as they are added with ADD_TEST.
        self._test_prefix_numbers = { "counter" : 1 }

        self._module_init_name = ""
        self._module_exit_name = ""

        self._extra_parameters = "struct ktf_test *self"
        self._extra_parameters_calls = "self"
        self._extra_parameters_calls_comma = "self, "
        self._test_macro_result = "TEST({suite_name}, {test_name}) {{\n"
        self._dummy_function_name = "{func_name}_{counter}_"
        self._dummy_function_body = "TEST({suite_name}, {dummy_name}) {{\n\t{call}\n}}"
        self._dummy_function_result = "{dummy_body}\n\n{orig_func}"
        self._add_test_call = "ADD_TEST({func_name});"
        self._dummy_function_internal_call = "{func_name}({alt_args}, {rest_args});" # common_call_multi_arg_replace
        self._extra_parameters_result = "{sig}{ext_params}{opt_comma}{args}{end}\n{{"
        self._extra_ktf_args_calls = "{func_name}({new_args}{old_arg_char}"
        self._extra_ktf_args_calls_no_args = "{func_name}({new_args})"
        self._wrapped_multi_arg_function_calls = "{func_name}({common_args}{extra_args});"
        self._new_main_name = "{old_name}_1"
        self._new_module_init = "module_init({new_main_name});\n"
        self._new_main_and_module_init = "int {new_main_name}(void)\n{{\n\tADD_TEST({old_main});\n\n\treturn 0;\n}}\n\nmodule_init({new_main_name});"

        # Regexes used in this class
        self._regexes = {
            # Captures all static function definitions. Return types must be in all lowercase, and { on next line!
            'all_static_functions': r"((static *(noinline)*? *([a-z_*0-9 ]+?) *([a-zA-Z_0-9]*)\()([a-z_*0-9,\n\t ]*)(\))\s*{)",
            'specific_static_function': "((static *(noinline)*? *([a-z_*0-9 ]+?) *({func_name})\()([a-z_*0-9,\n\t ]*)(\))\s*{{)",
            'find_module_init': r"module_init\((.*)\);",
            'find_module_exit': r"module_exit\((.*)\);",
            'main_function': "((static *(noinline)*? *([a-z_*0-9 ]+?) *({main})\()([a-z_*0-9,\n\t ]*)(\))\s*{{)",
            'exit_function': "((static *(noinline)*? *([a-z_*0-9 ]+?) *({exit})\()([a-z_*0-9,\n\t ]*)(\))\s*{{)",
            'includes_end': "(#include [<\"].*?[>\"].*)(\s*\n\s*\n)",
            'statics_with_context_args': "((static *(noinline)*? *([a-z_*0-9 ]+?) *([a-zA-Z_0-9]*)\()({ctx_args})(\))\s*{{)",
            'statics_with_context_and_extra_args': "((static *(noinline)*? *([a-z_*0-9 ]+?) *([a-zA-Z_0-9]*)\()({ctx_args})(.*)(\))\s*{{)",
            'test_macro_function': "(TEST(.*?), *(.*?) *{)",
            'function_calls_common_args': "(([a-zA-Z0-9_]*)\(({common_args})*\);)",
            #'function_calls_all_args': "(([\w_]+)(\()(.*)(\)))([,; \s)])(?!{)",
            #'function_calls_all_args': "((([\w_]+)(\()(.*))(\)[,; \s)](?!{)))",
            # We exploit the fact that this should be used AFTER the KTF specific
            # arguments have been added.
            'function_calls_with_args': "(((\w)+)(\()((?!struct)(?!\))))",
            'find_exact_match': "({pattern})",
            'function_calls_without_args': "(((\w)+)(\(\)))",
            'multi_arg_test_function_calls': "(([a-zA-Z0-9_]*)\(({common_args}), *(.*?)\));",
        }
            

        # Calls to initialization methods:
        # --------------------------------
        self._register_init_and_exit()
        self._register_local_functions()
        self._register_helper_functions()

        if self._should_add_new_main:
            self._add_new_main()
        
        # This action is repeated in case the "_add_new_main" method changes
        # the init function name.
        self._register_init_and_exit()

        self.dprintwl("self._test_function_names", self._test_function_names)
        self.dprintwl("self._boilerplate_test_code", self._boilerplate_test_code)
        self.dprintwl("self._test_suite_name", self._test_suite_name)
        self.dprintwl("self._init_code", self._init_code)
        self.dprintwl("self._exit_code", self._exit_code)
        self.dprintwl("self._include_code", self._include_code)
        self.dprintwl("self._type_def_code", self._type_def_code)
        self.dprintwl("self._boilerplate_test_code", self._boilerplate_test_code)
        self.dprintwl("self._context_args", self._context_args)


    # Debug functions.

    def dprint(self, *args):
        """
        Debug print. Will only print if debug flag is set.
        """
        if self._debug:
            for arg in args:
                pprint(arg)

    def dprintwl(self, title, *args):
        """
        Debug print with dashed lines above and below.
        """
        self.dprintl()
        if title:
            self.dprint(title)
        self.dprint("")
        self.dprint(*args)
        self.dprintl()

    def dprintl(self):
        """
        Prints a line of dashes if debug flag is set.
        """
        self.dprint("-" * 30)

    def dprintp(self, *args):
        """
        Debug pretty print.
        """
        if self._debug:
            pprint(*args)

    def print_values(self):
        """
        Prints various fields of this object if debug flag is set.
        """
        if not self._debug:
            return
        
        self.dprintwl("Test function names:")
        self.dprintwl("Local function names:")
        self.dprintwl("Boilerplate TEST code:")


    # Private methods for registering info for later use.

    def _register_init_and_exit(self):
        """
        Registers the init and exit function in the file.
        Based on the assumption that the file uses the 
        Linux module system, with the use of both 'module_init' 
        and 'module_exit'.
        """
        reg_init = self._regexes['find_module_init']
        reg_exit = self._regexes['find_module_exit']
        self._module_init_name = re.search(reg_init, self._text).group(1)
        self._module_exit_name = re.search(reg_exit, self._text).group(1)
        if not self._test_suite_name:
            self._test_suite_name = self._module_init_name
        
        self.dprintwl("module_init_name", self._module_init_name)
        self.dprintwl("module_exit_name", self._module_exit_name)

    def _register_local_functions(self):
        """
        Registers all locally defined functions and stores
        them for future use. However, it is based on the 
        assumption that they will begin with the keyword 
        'static'.
        """
        reg = self._regexes['all_static_functions']
        for match in re.finditer(reg, self._text):
            func_name = match.group(5)
            self._local_function_names[func_name] = True
            """
            pprint("(_reg) Added func " + func_name)
            if func_name in self._test_function_names:
                self._test_prefix_numbers[func_name] = str(self._test_prefix_numbers["counter"]) + "_" + func_name
                self._test_prefix_numbers["counter"] += 1
                pprint("\t(def repl) Added " + self._test_prefix_numbers[func_name])
            """
        
        self.dprintwl("self._local_function_names:", self._local_function_names)

    def _register_helper_functions(self):
        """
        Registers all local functions that have not been 
        specified as a main test function.
        """
        if not self._local_function_names or not self._test_function_names:
            self.dprintwl("Local function names unspecified!")
            return
        for func_name in self._local_function_names:
            if func_name not in self._test_function_names and \
             func_name != self._module_init_name and \
             func_name != self._module_exit_name:   
                
                self._local_helper_function_names[func_name] = True

        self.dprintwl("self._local_helper_function_names:", self._local_helper_function_names)

    
    # Regex helper functions.

    def _replace_if_valid_test_function_def(self, matches):
        """
        Replaces a match if the function is a test function 
        "marked" for conversion.
        """
        test_name = matches.group(5)
        if test_name in self._test_function_names:
            pprint("(def repl) Replacing " + str(test_name))
            return self._test_macro_result.format(
                suite_name=self._test_suite_name, \
                test_name=test_name)
        else:
            return matches.group(1)

    def _replace_if_valid_test_function_call(self, matches):
        """
        Replaces a match if the function call is to a valid
        standard-argument test function "marked" for conversion.
        """
        test_name = matches.group(2)
        if test_name in self._test_function_names:
            """
            name_with_prefix = self._test_prefix_numbers[test_name]
            pprint("\t(call repl) Fetched " + self._test_prefix_numbers[test_name])
            return self._add_test_call.format(func_name=name_with_prefix) 
            """
            return self._add_test_call.format(func_name=test_name)
            #(func_name=test_name)
        else:
            return matches.group(1)

        #self._dummy_function_internal_call = "{func_name}({alt_args}, {rest_args});" # common_call_multi_arg_replace


    def _replace_if_valid_multi_arg_test_function(self, matches):
        """
        Replaces a match if the function is a test function
        "marked" for conversion, and has extra arguments.
        """
        test_name = matches.group(2)
        common_args = matches.group(3)
        extra_args = matches.group(4)
        old_call = matches.group(1)

        pprint(matches.group(1))
        if test_name in self._test_function_names:
            dummy_func_name = self._dummy_function_name.format(
                func_name=test_name, counter=self._dummy_function_counter)
            modified_call = self._dummy_function_internal_call.format(
                func_name=test_name, alt_args=self._common_call_multi_arg_replace,
                rest_args=extra_args)
            dummy_func_body = self._dummy_function_body.format(
                suite_name=self._test_suite_name, dummy_name=dummy_func_name, 
                call=modified_call)
            add_test_call = self._add_test_call.format(func_name=dummy_func_name)

            #print("(1 m.arg) dummy_func_name: " + dummy_func_name)
            """
            print("\tdummy_func_name: " + dummy_func_name)
            print("\t\told call: " + str(matches.groups()))
            print("\tinternal call: " + modified_call)
            print("\tADD_TEST call: " + add_test_call)
            print("\tdummy func body: " + dummy_func_body)
            """
            self._dummy_function_counter += 1

            # Add dummy functions to a list of tuples that will be added to the
            # code by the caller.
            self._dummy_functions_to_add.append((test_name, dummy_func_body))
            
            return add_test_call
        else: 
            return matches.group(1)

    def _is_helper_or_multi_arg_test_function(self, test_name):
        """
        Returns True if the test name refers to a helper function
        or a test function with additional arguments.
        """
        print("_local_helper_func_names: ")
        for h in self._local_helper_function_names:
            print(h)
        return test_name not in self._ignore_func_names and \
            (test_name in self._local_helper_function_names or \
            test_name in self._test_function_names)

    def _add_extra_args_if_valid_definition(self, matches):
        """
        Replaces a match if the function is either a helper function
        or a multiple argument function "marked" for conversion.
        """
        function_signature = matches.group(2)
        test_name = matches.group(5)
        old_args = matches.group(6)
        rest = matches.group(7)
        optional_comma = ", "
        if old_args == "" or old_args == "void":
            optional_comma = ""
            old_args = ""
        if self._is_helper_or_multi_arg_test_function(test_name):
            #pprint("Modded (1): " + str(matches.group(1)))
            #pprint("(2 v.def): " + str(matches.group(1)))
            return self._extra_parameters_result.format(
                sig=function_signature, ext_params=self._extra_parameters, 
                opt_comma=optional_comma, args=old_args, end=rest)
        else:
            return matches.group(1)

    def _add_extra_args_if_valid_call(self, matches):
        """
        Adds an extra self argument to a call if the function is
        either a helper function or a multiple argument function
        "marked" for conversion.
        """
        func_name = matches.group(2)
        if self._is_helper_or_multi_arg_test_function(func_name):
            #pprint("(3 v.call): " + str(func_name))
            #self._extra_ktf_args_calls = "{func_name}({new_args}{old_arg_char}"
            extra_params = self._extra_parameters_calls_comma
            
            new_call = self._extra_ktf_args_calls.format(
                func_name=func_name, new_args=extra_params,
                old_arg_char="") 
            #pprint("    new_call: " + new_call)
            return new_call
        else:
            return matches.group(1)

    def _add_extra_args_if_valid_call_no_args(self, matches):
        """
        Same as above, expect it's only used for function calls
        without any exisiting arguments.
        """
        func_name = matches.group(2)
        if self._is_helper_or_multi_arg_test_function(func_name):
            #pprint("(4 v.call): " + str(func_name))
            extra_params = self._extra_parameters_calls

            new_call = self._extra_ktf_args_calls_no_args.format(
                func_name=func_name, new_args=extra_params)    # "{func_name}()"
            return new_call
        else:
            return matches.group(1)

    def _convert_assertions_to_KTF(self, matches):
        """
        Replaces assertions from the self._assertion_replacements
        list of tuples to the corresponding right KTF assertion.
        """
        func_name_with_left_par = matches.group(1)
        #[pprint(item[1]) for item in self._assertion_replacements if item[0]+"(" == func_name_with_left_par]
        for item in self._assertion_replacements:
            if item[0]+"(" == func_name_with_left_par:
                return item[1]+"("
        return matches.group(1)

        #[pprint(item) for item in self._assertion_replacements if item[0] == func_name_with_left_par]
        #print("\t" + str(matches.group(1)))

    def _sub(self, reg, result):
        """
        Performs the actual substitution with the regexes.
        """
        self._text = re.sub(reg, result, self._text)
        return self


    # Public methods for adding and/or replacing text.

    def add_init_code_to_main(self):
        """
        Adds initialization code to the main function of the file.
        """
        return self._sub(
            self._regexes['main_function'].format(main=self._module_init_name),
            self._init_code)

    def add_exit_code(self):
        """
        Adds cleanup code to the exit function of the file.
        """
        return self._sub(
            self._regexes['exit_function'].format(exit=self._module_exit_name),
            self._exit_code)

    def add_include_code(self):
        """
        Adds additional code to include necessary headers.
        """
        return self._sub(
            self._regexes['includes_end'],
            self._include_code)

    def add_type_definitions(self):
        """
        Adds type definitions such as structs and typedefs.
        """
        return self._sub(
            self._regexes['includes_end'],
            self._type_def_code)

    def convert_to_test_common_args(self):
        """
        Converts all specified test functions to TEST functions,
        given that they do not take any extra arguments compared 
        to the common ones. For example, for test_xarray.c, 
        'struct xarray *xa' is considered common arguments, and all
        test functions only taking this single argument will be 
        matched. Test functions with additional arguments will 
        therefore NOT be matched here.
        """
        return self._sub(
            self._regexes['statics_with_context_args'].format(
                ctx_args=self._context_args),
            self._replace_if_valid_test_function_def)

    def convert_to_test_extra_args(self):
        """
        Converts the rest of the specified test functions to TEST 
        functions, mainly those with additional arguments which are
        not converted by 'convert_to_test_common_args'.
        """
        self._dummy_functions_to_add = []
        self._sub(
            self._regexes['multi_arg_test_function_calls'].format(
                common_args=self._common_call_args),
            self._replace_if_valid_multi_arg_test_function)
        # If there are dummy functions that needs to be added, it will be done here.
        if self._dummy_functions_to_add:
            print("# Dummy functions to add: " + str(len(self._dummy_functions_to_add)))
            for dummy_tuple in self._dummy_functions_to_add:
                self._sub(
                    self._regexes['specific_static_function'].format(
                        func_name=self._module_init_name),
                    self._dummy_function_result.format(
                        dummy_body=dummy_tuple[1], orig_func="\g<1>"))
        else:
            print("No dummy functions to add!")
        return self

    def convert_calls_to_add_test(self):
        """
        Converts all calls to the ordinary single/none argument
        test functions, "marked" for conversions, to use ADD_TEST.
        """
        return self._sub(
            self._regexes['function_calls_common_args'].format(
                common_args=self._common_call_args),
            self._replace_if_valid_test_function_call)

    def add_extra_parameters_to_helpers_and_multi_arg_defs(self):
        """
        Adds a KTF self argument to all helper functions.
        """
        return self._sub(
            self._regexes['all_static_functions'],
            self._add_extra_args_if_valid_definition)

    def add_self_argument_to_helper_calls(self):
        """
        Adds an extra self argument to calls on helper functions.
        Does two rounds of substitution to cover the very few cases
        where there are no arguments. 
        """
        self._sub(
            self._regexes['function_calls_with_args'],
            self._add_extra_args_if_valid_call)
        return self._sub(
            self._regexes['function_calls_without_args'],
            self._add_extra_args_if_valid_call_no_args)

    def convert_assertions(self):
        """
        Converts assertions calls to KTF assertions.
        """
        for pattern in self._assertion_replacements:
            self._text = re.sub(pattern[0], pattern[1], self._text, flags=re.MULTILINE)
        return self

    def add_boilerplate_test_code(self):
        """
        Adds boilerplate test code to all test functions defined 
        with the TEST macro. This can for example
        """
        return self._sub(
            self._regexes['test_macro_function'],
            self._boilerplate_test_code)

    def _add_new_main(self):
        """
        Adds a new main function if the previous main was converted to a TEST
        function. Automatically called if "should_add_new_main" rule is True.
        """
        old_main_name = self._module_init_name
        new_main_name = self._new_main_name.format(old_name=old_main_name)
        self.dprintwl("new_main_name", new_main_name)
        self._sub(
            self._regexes['find_module_init'],
            self._new_main_and_module_init.format(
                new_main_name=new_main_name, old_main=old_main_name))
        return self

    
    # Other methods.

    def result(self):
        """
        Prints the result to the earlier specified output stream.
        """
        with open(args.out, 'w') as f:
            f.write(self._text)


# def __init(self, text, outfile_name, rules, debug=False):


"""
state = CurrentState(data, args.testfuncs, args.out, test_suite_name, \
    ignore_func_names, init_code, exit_code, include_code, type_def_code, \
    boilerplate_test_code, context_args, common_call_args, \
    common_call_multi_arg_replace, assertion_replacements, debug=False)
    """

print(args.file)
if args.file.endswith("test_xarray_rewrite.c"):
    state = CurrentState(data, args.out, test_xarray_rules, False)
    print("Converting " + args.file)
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

test_sort_rules_2 = {
    "test_funcs":
    """
    test_sort_init
    """,
    "blacklist": ["cmpint"],
    "should_add_new_main": True
}

print(args.file)
if args.file.endswith("test_sort_rewrite.c"):
    state = CurrentState(data, args.out, test_sort_rules_2, True)
    print("Converting " + args.file)
    state.add_include_code() \
    .add_init_code_to_main() \
    .add_exit_code() \
    .convert_to_test_common_args() \
    .result()
    #.convert_to_test_common_args() \
    #.convert_calls_to_add_test() \
    #.convert_assertions() \
    #.result()



print(state._regexes['statics_with_context_args'].format(
                ctx_args=state._context_args))

def out(output):
    """
    Writes the output to either stdout or a file.
    """
    print(output)
    if args.out:
        with open(args.out, 'w') as f:
            f.write(output)
        print("convert.py: Wrote output to", args.out)