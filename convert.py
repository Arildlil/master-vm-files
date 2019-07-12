import re, os, argparse, sys
from pprint import pprint
from collections import namedtuple
from string import whitespace



class Converter(object):
    """
    The class takes the name of an input file, an output file and a dictionary 
    of data. This keys in the dictionary should be the names of the desired 
    transformations; the values are the associated data (often code, regular 
    expressions or the names of either functions or parameters). 
    
    First, the contents of the input file is read and stored internally as a 
    string. Secondly, the string is modified using a combination of format 
    strings, data from the dictionary parameter, and regular expressions; the 
    goal is to partly or completely adapt the input file to the Kernel Test 
    Framework. Finally, the contents of this transformation is written to the 
    specified output file.

    The most important fields to fill in the dictionary are:
        ["test_functions"] : string
        ->  A string containing the names of test functions to redefine with 
            the TEST macro. No functions will be redefined if this field is 
            left out.

        ["blacklist"] : string
        ->  Names of functions that should NOT be redefined with the TEST 
            macro. This is mainly intended for helper functions or functions 
            with unique parameter lists. Can be skipped if ["test_functions"] 
            is skipped or if every function in the file should be redefined 
            with TEST.

    Other supported fields (they are all optional):
        ["should_add_new_main"] : boolean
        ->  Set this field to True if the test file needs a new main function. 
            One use case for this is if the test file only contains one 
            function. This function must then be redefined with TEST for KTF to 
            work, thus requiring a new one to be added. 
        ->  Skip this field unless a new main function is needed.

        ["init_code"] : string (has default value)
        ->  Initialization code to be added to the main function and/or just 
            above it. This field has a default value and can be skipped unless 
            additional setup code is required. Should contain the substring 
            "\g<1>" where the signature of the main function should be placed. 
            One use case is if contexts or fixtures need to be setup. In that 
            case the value of the "self._default_init_code" field in the class 
            should be used as a baseline, with any new code appended to the end 
            of it. See the "convert_wrapper_xarray.py" file for an example.
        -> Skip this field unless custom setup code is needed.
        
        ["exit_code"] : string (has default value)
        ->  Cleanup code to be added to the exit function and/or just above it.
            Functions the same way as the ["init_code"] field.
        ->  Skip this field unless custom cleanup code is needed.

        ["include_code"] : string (used in regex) (has default value)
        ->  Include code to be added to the top of the file. Should begin with 
            the substring "\g<1>". This field should generally be skipped. 
            NOTE: It's assumed that the input file contains at least one 
            other include statement! Otherwise, this won't work!
        ->  Skip this field unless additional headers are needed.

        ["new_types"] : string (used in regex)
        ->  Used for defining new types near the top of the file. Should begin 
            with the substring "\g<1>". One use case for this field is when 
            contexts are used. In that case a new type must be defined to hold 
            the context data. For example:
                "new_types": r\"\"\"\g<1>

                struct array_context {
                    struct ktf_context k;
                    struct xarray *xa;
                };

                \"\"\",
        ->  Skip this field unless new types are needed.

        ["boilerplate_code"] : string (used in regex)
        ->  The code added for this field will be added to the beginning of 
            every function redefined with TEST. Should begin with the substring 
            "\g<1>". One use case for this is to retrieve data from a context 
            in the beginning of every TEST function.
        ->  Skip this field unless code should be added to all TEST functions.

        ["test_suite_name"] : string
        ->  This string will be used by the TEST macro as the name for the test 
            suite. Can be skipped if the "module_init()" is called. In that 
            case, the argument used will be used as the name of the test suite.
        ->  Skip this field unless "module_init()" is used, or a custom test 
            suite name is desired.

        ["replacements"] : tuple(regex, regex)
        ->  The first regex specifies the pattern to replace, the second regex 
            specifies the result. See the "convert_wrapper_xarray.py" file for 
            an example.
        ->  Skip this field unless simple patterns should be replaced.

        ["context_args"] : string (used in regex)
        ->  Parameters found in functions that should be redefined with TEST.
            This field is used if either 
            1) a set of common arguments should be supplied by a context 
            instead, or 
            2) if the parameters should be removed.

            For example, if every test function to be redefined has "void" 
            in their parameter lists, specify "void" as the value; likewise, if 
            the test functions all have "struct xarray *xa" as parameter, use 
            "struct xarray [*]xa" as a value. Note that the special character 
            \* is enclosing in [], as the string will be used in a regex. This 
            rule also applies to other special characters, such as ( and ).
            
            Multiple parameters can also be specified if they are separated by 
            a |. For instance, "struct xarray [*]xa|void" means that the 
            parameter list of a function can be either "struct xarray *xa" or 
            a void. 
        ->  Skip this field unless test functions are to be redefined with the 
            TEST macro. In that case, the value of this field should represent 
            old parameters that will be supplied in other ways, like through a 
            fixture or a context.

        ["common_call_args"] : string
        ->  Serves pretty much the same purpose as ["context_args"], except 
            that this is the value(s) used in the function CALLS instead of the 
            function DEFINITION.

            If the value for the ["context_args"] field is 
            "struct xarray [*]xa", the value of this field could be "&array".
        ->  Skip this field unless test functions are to be redefined with the 
            TEST macro. In that case, the value of this field should represent 
            the old argument value(s).

        ["extra_dummy_args_call"] : string
        ->  This field is used when creating dummy functions that wraps a 
            function call. 
            See the "convert_wrapper_xarray.py" file for an example.
        ->  Skip this field unless dummy functions are going to be used.
    """

    def __init__(self, text, outfile_name, rules, debug=False):

        # Default KTF snippets
        # --------------------
        self._default_init_code = "KTF_INIT();\n\n\g<1>"
        self._default_exit_code = '\g<1>\n\tKTF_CLEANUP();'
        self._default_include_code = '\g<1>\n#include "ktf.h"\n'
        self._default_common_call_args = ""
        self._default_context_args = "void|''"

        # Argument handling:
        # ------------------
        # All the contents of the source file.
        self._text = text

        # The name of the file to write output to.
        self._outfile_name = outfile_name

        # Names of all the functions to convert to TEST.
        self._test_function_names = rules.get("test_functions")

        # Code to be added to the init function of the module, if any.
        self._init_code = rules.get("init_code") or self._default_init_code
        
        # Code to be added to the exit function of the module, if any.
        self._exit_code = rules.get("exit_code") or self._default_exit_code

        # Code for header inclusion
        self._include_code = rules.get("include_code") or self._default_include_code

        # Code for definition of structs and typedefs.
        self._new_types = rules.get("new_types")
                
        # Context code that should be added to the beginning
        # of every TEST function.
        self._boilerplate_code = rules.get("boilerplate_code")

        # The name of the test suite itself.
        self._test_suite_name = rules.get("test_suite_name")

        # Common arguments used by most test functions, which could be moved 
        # into a context instead. The default value is "void|''", meaning
        # no arguments. 
        self._context_args = rules.get("context_args") or self._default_context_args

        # Common arguments often used when calling test functions. For the xarray suite,
        # this is the '&array' argument supplied to all test functions. For functions
        # without arguments, this could be '' or 'void'.
        self._common_call_args = rules.get("common_call_args") or self._default_common_call_args

        # Replacement for _common_call_args for multi argument function calls
        # that needs to be put in a dummy function.
        self._common_call_multi_arg_replace = rules.get("extra_dummy_args_call")

        # The names of functions to be ignored.
        self._blacklist = rules.get("blacklist")

        # Specifies which assertions to convert to which KTF assertions.
        self._replacements = rules.get("replacements")

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
        self._new_main_and_module_init = "KTF_INIT();\n\nint {new_main_name}(void)\n{{\n\tADD_TEST({old_main});\n\n\treturn 0;\n}}\n\nmodule_init({new_main_name});"
        self._single_space = " "

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
            # We exploit the fact that this should be used AFTER the KTF specific
            # arguments have been added.
            'function_calls_with_args': "(((\w)+)(\()((?!struct)(?!\))))",
            'find_exact_match': "({pattern})",
            'function_calls_without_args': "(((\w)+)(\(\)))",
            'multi_arg_test_function_calls': "(([a-zA-Z0-9_]*)\(({common_args}), *(.*?)\));",
            'find__init_and_exit': "\s(__init|__exit)\s",
        }
            

        # Calls to initialization methods:
        # --------------------------------
        self._register_init_and_exit()
        self._register_local_functions()
        self._register_helper_functions()

        # Calls to conversion methods:
        # --------------------------------
        if self._should_add_new_main:
            self._add_new_main()
            # This action is repeated because "_add_new_main" method changes
            # the init function name.
            self._register_init_and_exit()
        self._remove_init_and_exit_macro_calls()

        self.dprintwl("self._test_function_names", self._test_function_names)
        self.dprintwl("self._boilerplate_code", self._boilerplate_code)
        self.dprintwl("self._test_suite_name", self._test_suite_name)
        self.dprintwl("self._init_code", self._init_code)
        self.dprintwl("self._exit_code", self._exit_code)
        self.dprintwl("self._include_code", self._include_code)
        self.dprintwl("self._new_types", self._new_types)
        self.dprintwl("self._boilerplate_code", self._boilerplate_code)
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
            return self._add_test_call.format(func_name=test_name)
        else:
            return matches.group(1)


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
        return test_name not in self._blacklist and \
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
            extra_params = self._extra_parameters_calls_comma
            
            new_call = self._extra_ktf_args_calls.format(
                func_name=func_name, new_args=extra_params,
                old_arg_char="") 
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
            extra_params = self._extra_parameters_calls

            new_call = self._extra_ktf_args_calls_no_args.format(
                func_name=func_name, new_args=extra_params)    # "{func_name}()"
            return new_call
        else:
            return matches.group(1)

    def _convert_assertions_to_KTF(self, matches):
        """
        Replaces assertions from the self._replacements
        list of tuples to the corresponding right KTF assertion.
        """
        func_name_with_left_par = matches.group(1)
        for item in self._replacements:
            if item[0]+"(" == func_name_with_left_par:
                return item[1]+"("
        return matches.group(1)

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
            self._new_types)

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

    def use_replacements(self):
        """
        Converts assertions calls to KTF assertions.
        """
        for pattern in self._replacements:
            self._text = re.sub(pattern[0], pattern[1], self._text, flags=re.MULTILINE)
        return self

    def add_boilerplate_code(self):
        """
        Adds boilerplate test code to all test functions defined 
        with the TEST macro. This can for example
        """
        return self._sub(
            self._regexes['test_macro_function'],
            self._boilerplate_code)

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

    def _remove_init_and_exit_macro_calls(self):
        """
        Removes all occurences of __init and __exit from the code. This is 
        important as the use of these macros can cause problems when KTF runs 
        the file.
        """
        self._sub(
            self._regexes['find__init_and_exit'],
            self._single_space)

    
    # Other methods.

    def result(self):
        """
        Prints the result to the earlier specified output stream.
        """
        with open(self._outfile_name, 'w') as f:
            f.write(self._text)