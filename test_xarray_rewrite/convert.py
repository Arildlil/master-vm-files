import re, os, argparse, sys
from pprint import pprint

# Argsparse related code
# -------------------------------------------------------------------

default_filename = "/home/ubuntu/src/test_xarray_rewrite/kernel/test_xarray_rewrite.c"

parser = argparse.ArgumentParser(
    description='A python script hand crafted for converting "test_xarray.c" to KTF.')
parser.add_argument('-f', '--file', action='store', default=default_filename)
parser.add_argument('-o', '--out', action='store')
parser.add_argument('-t', '--testfuncs', nargs='+', type=str)
args = parser.parse_args()

with open(args.file, 'r') as f:
    data = f.read()


# Code local for one specific test file. This could be supplied in
# other ways, but currently it will be written here.
# -------------------------------------------------------------------
bp_test_code = r"""static struct array_context cxa = { .xa = &array };

KTF_INIT();

\g<1>
    \tKTF_CONTEXT_ADD(&cxa.k, "array");"""


def printr(matches):
    print("\n\t" + matches.group(1))
    for match in matches.groups()[1:]:
        print("\t\t" + str(match))
    return matches.group(1)



class CurrentState(object):
    """
    Class used to store and handle parameters from the user,
    as well as information about the file in question. For example,
    instead of repeatedly using a regex to find all locally defined
    functions, this can be done once and stored here for future use.
    """

    def __init__(self, text, test_func_names, outfile_name, 
        init_code=[], exit_code=[], bp_test_code=[], debug=True):

        # All the contents of the source file.
        self._text = text

        # Names of all the functions to convert to TEST.
        self._test_function_names = test_func_names

        # The name of the file to write output to.
        self._outfile_name = outfile_name

        # Will store the names of all local functions defined in
        # the file.
        self._local_function_names = {}

        # Will store the locally defined helper functions, defined
        # as all functions that have not been declared as test 
        # functions.
        self._local_helper_function_names = {}
        
        # Boiler plate code that should be added to the beginning
        # of all TEST functions.
        self._boilerplate_test_code = bp_test_code

        # Code to be added to the init function of the module, if any.
        self._init_code = init_code
        
        # Code to be added to the exit function of the module, if any.
        self._exit_code = exit_code

        self._debug = debug

        self._module_init_name = ""
        self._module_exit_name = ""

        # Regexes used in this class
        self._regexes = {
            'all_static_functions': r"((static *(noinline)*? *([a-z_*0-9 ]+?) *([a-zA-Z_0-9]*)\()([a-z_*0-9,\n\t ]*)(\)))\s*{",
            'module_init': r"module_init\((.*)\);",
            'module_exit': r"module_exit\((.*)\);",
        }
            
        self._register_init_and_exit()
        self._register_local_functions()
        self._register_helper_functions()
        self.dprintwl("self._boilerplate_test_code", self._boilerplate_test_code)


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


    # Functions for registering info for later use.

    def _register_init_and_exit(self):
        """
        Registers the init and exit function in the file.
        Based on the assumption that the file uses the 
        Linux module system, with the use of both 'module_init' 
        and 'module_exit'.
        """
        reg_init = self._regexes['module_init']
        reg_exit = self._regexes['module_exit']
        self._module_init_name = re.search(reg_init, self._text).group(1)
        self._module_exit_name = re.search(reg_exit, self._text).group(1)
        
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
        for func_name in self._local_function_names:
            if func_name not in self._test_function_names and \
             func_name != self._module_init_name and \
             func_name != self._module_exit_name:   
                self._local_helper_function_names[func_name] = True

        self.dprintwl("self._local_helper_function_names:", self._local_helper_function_names)


state = CurrentState(data, args.testfuncs, args.out, \
    bp_test_code=bp_test_code)



# Utility functions

def _register_local_func_names(text):
    """
    Helper function that registers all local function names
    we want to modify, and use the resulting dictionary later
    in the main function .
    """
    debug = True

    valid_func_names = {}

    #if debug:
    #    print("Local static functions:")

    def _on_match(text):
        """
        Helper function for the helper function.
        Adds matches to the 
        """
        full_match = text.group(1)
        func_name = text.group(5)
        valid_func_names[func_name] = True
        return full_match
    
    reg = r'((static *(noinline)*? *([a-z_*0-9 ]*) [*]*((?!xarray_exit)(?!xarray_checks)[a-z_0-9]*)\()([a-z_*0-9,\n\t ]*)(\)))'
    # Does not actually modify the input.
    re.sub(reg, _on_match, text)

    if debug:
        pprint(valid_func_names)

    return valid_func_names




def _register_functions_to_convert(func_names, params):
    """
    Utility function used to (semi-manually) register which functions
    to convert to TEST functions.
    """

def _filter_out_helpers(text):
    """
    Helper function that filters out names of helper functions.
    """
    valid_func_names = {}

    def _register_valid_func_names(text):
        #print("\t\tValid func: " + text.group(1))
        return text.group(0)

    reg_test_func_calls = r'(check_[a-z_]*)\((&array)*\);\s*'
    return re.sub(reg_test_func_calls, _register_valid_func_names, text.group(0))


# Specific regex functions

def add_ktf_init_code_to_main(text):
    """
    Adds a call to KTF_CONTEXT_ADD to the beginning of the
    main function.
    """
    reg = r'(static int [a-z_]*\(void\)\s*?{\s*)'

    ctx_code = r"""static struct array_context cxa = { .xa = &array };

KTF_INIT();

\g<1>
    \tKTF_CONTEXT_ADD(&cxa.k, "array");"""
    
    res_pat = r'{}\n\n\t'.format(ctx_code)
    return re.sub(reg, res_pat, text, 0, re.MULTILINE)

def add_ktf_cleanup_context_code(text):
    """
    Adds standard cleanup/exit code for KTF.
    """
    reg = r'(static void [a-z_]*_exit\(void\)\s*{)(\s*)(})'
    code = r"""\tstruct ktf_context *pctx = KTF_CONTEXT_FIND("data");
        KTF_CONTEXT_REMOVE(pctx);
        
        KTF_CLEANUP();"""
    res_pat = r'\g<1>\g<2>{}\n\g<3>'.format(code)
    return re.sub(reg, res_pat, text, 0, re.MULTILINE)
    #return re.findall(reg, text)[-1]

def add_ktf_header_and_struct_def(text):
    """
    Adds a #include "ktf.h" and definition of the struct type used to store
    the context.
    """
    reg = r'(#include [<\"].*[>\"].*)\s*\n\s*\n'
    code = r"""#include "ktf.h"

struct array_context {
    struct ktf_context k;
    struct xarray *xa;
};

"""
    res_pat = r'\g<1>\n{}'.format(code)
    return re.sub(reg, res_pat, text, 1)

def convert_func_signature_to_test_macro(text):
    """
    Converts regular test functions on the format
    "static noinline void check_XXX(struct xarray *xa)
    {",
    where XXX is the rest of the function name after 'check_'. It 
    captures and replaces the { in addition to the function 
    signature, as this function also adds context related code to 
    the beginning of the function.
    """
    reg = r'static noinline void (check_[a-z_]*)\((struct xarray [*]xa|void)\)\n\s*({)'
    ctx_code = r"""
        struct array_context *actx = KTF_CONTEXT_GET("array", struct array_context);
        struct xarray *xa = actx->xa;
        """
    res_pat = r'TEST(test_xarray_rewrite, \g<1>)\n{' + ctx_code
        
    re.sub(reg, _filter_out_helpers, text, 0, re.MULTILINE)
    #return re.sub(reg, res_pat, text, 0, re.MULTILINE)
    return re.sub(reg, res_pat, text, 0, re.MULTILINE)

def _add_comma_if_non_void(text):
    """
    Helper function for 'add_self_argument_to_helper_functions. 
    Used to handle cases where there a function does not have
    any arguments, so that a , cannot be added after the new 
    argument.
    """
    code = r'struct ktf_test *self'
    current_text = text.group(1) + code
    if text.group(5) != "void":
        current_text += ", " + text.group(5)
    current_text += text.group(6)
    #print(current_text)
    return current_text

def add_self_argument_to_helper_functions(text):
    """
    Adds an additional argument to (hopefully) all functions
    that are left after the main test functions have been 
    converted to using TEST. Note that this will convert most
    if not all functions in the file, so converting to TEST 
    first is important when used in this context! It is hardcoded
    to ignore the 'xarray_exit' function.
    """
    reg = r'(static *(noinline)*? *([a-z_*0-9 ]*) [*]*((?!xarray_exit)(?!xarray_checks)[a-z_0-9]*)\()([a-z_*0-9,\n\t ]*)(\))'
    return re.sub(reg, _add_comma_if_non_void, text)

def convert_func_calls_to_add_test_macro(text):
    """
    Converts calls to regular test functions into calls
    to ADD_TEST instead. It aims to only capture function 
    calls inside the main function 'xarray_checks', by not
    allowing any deviations in terms of arguments or numbers
    in function names.
    """
    reg = r'(check_[a-z_]*)\((&array)*\);\s*'
    res_pat = r'ADD_TEST(\g<1>);\n\t'
    return re.sub(reg, res_pat, text, 0, re.MULTILINE)

def convert_multi_arg_funcs_to_test_macro(text):
    """
    Handles the special case of independent test functions that
    takes multiple additional arguments, like 'check_workingset'
    does. 
    """
    state = {
        'counter': 1,
        'func_calls': [],
        'old_func_names': [],
        'new_func_names': []
    }

    def _collect_calls(text):
        full_call = text.group(1)
        old_func_name = text.group(2)
        args_rest = text.group(4)

        new_func_name = old_func_name + "_" + str(state['counter'])
        state['counter'] += 1
        new_func_call = old_func_name + "(self, xa, " + args_rest + ");"
        add_test_call = "ADD_TEST(" + new_func_name + ");\n\t"
        
        print(text.groups())
        print("\told_func_call: " + full_call)
        print("\told_func_name: " + old_func_name)
        print("\tnew_func_call: " + new_func_call)
        print("\tadd_test_call: " + add_test_call)
        
        state['old_func_names'].append(old_func_name)
        state['new_func_names'].append(new_func_name)
        state['func_calls'].append(new_func_call)
#check_workingset(self, xa, 0);	ADD_TEST(check_workingset_1);
        return add_test_call

    reg_func_calls = r'((check_[a-z_]*)\((&array), (.*)\);)\s*'
    # Collect function calls and replace ocurrences with ADD_TEST.
    text = re.sub(reg_func_calls, _collect_calls, text)
    # Find the definition of the same functions. We will place
    # the dummy functions for all new functions above the first
    # match we get.
    reg_func_def = r'(static (noinline )*void (check_[a-z_]*)\((struct ktf_test [*]self, struct xarray [*]xa, ([a-z _*]*)\)\n\s*({)))'
    ctx_code = \
    r"""struct array_context *actx = KTF_CONTEXT_GET("array", struct array_context);
    struct xarray *xa = actx->xa;
        """
    res_pat_code = ""
    for i in range(state['counter']-1):
        res_pat_code += \
            """TEST(test_xarray_rewrite, {name}) 
{{
    {ctx}
    {expr}
}}

""".format(name=state['new_func_names'][i], ctx=ctx_code, 
        expr=state['func_calls'][i])
    res_pat_code += "\g<1>"
    return re.sub(reg_func_def, res_pat_code, text, 1)

def add_self_argument_to_function_calls(text):
    """
    Adds the extra self argument to all helper function that 
    need it. Because of the inherent difficulty in picking only 
    the exact right functions with this regex alone, it will first
    run an earlier regex and register the names of all relevant 
    functions in a dictionary. Then, the "local" regex will check 
    each function name with the ones found here, to check whether
    the function call should be converted or not. This is not the
    most elegang solution, but the difficulty in doing this with 
    one regex only proved too difficult.
    """
    valid_func_names = _register_local_func_names(text)

    print("\nChecking function calls...")

    def _check_if_local_func_name(text):
        """
        Helper function used by a regex to check if the function 
        name is a valid match or not.
        """
        debug = False

        func_name = text.group(2)
        args = text.group(4)
        rest = text.group(5)
        if func_name in valid_func_names:
            if debug:
                print("\tMatch on '" + func_name + "(" + args + ")" + rest)

            modified_sig = func_name + "(self"
            # Special case because one single function call was 
            # without arguments...
            if args:
                modified_sig += ", " + args + ")" + rest
            else:
                modified_sig += ")" + rest
            
            if debug:
                print("\targs after: " + modified_sig)
            return modified_sig
        else:
            return text.group(1)
            #print("\tNope! " + func_name + " is not a local function!")

    #reg = r'((_)*([a-z_*0-9]+)\((.*?)\)([, \);]))'
    reg = r'(((_)*[a-z_*0-9]+)\((.*?)\)([, \);]))'
    return re.sub(reg, _check_if_local_func_name, text)

def convert_assertions(text):
    """
    """
    pass
    
def out(output):
    """
    Writes the output to either stdout or a file.
    """
    print(output)
    if args.out:
        with open(args.out, 'w') as f:
            f.write(output)
        print("convert.py: Wrote output to", args.out)
    

"""
out(add_self_argument_to_function_calls(
        convert_multi_arg_funcs_to_test_macro(
            convert_func_calls_to_add_test_macro(
                add_self_argument_to_helper_functions(
                    convert_func_signature_to_test_macro(
                        add_ktf_header_and_struct_def(
                            add_ktf_cleanup_context_code(
                                add_ktf_init_code_to_main(
                                    data)))))))))
"""
#for x in reg.finditer(data):
#    print(x.groups())
