import re, os, argparse, sys

default_filename = "/home/ubuntu/src/test_xarray_rewrite/kernel/test_xarray_rewrite.c"

parser = argparse.ArgumentParser(
    description='A python script hand crafted for converting "test_xarray.c" to KTF.')
parser.add_argument('-f', '--file', action='store', default=default_filename)
parser.add_argument('-o', '--out', action='store')
args = parser.parse_args()
print(args)
print(args.file)

with open(args.file, 'r') as f:
    data = f.read()


#print(data)

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
    counter = 1
    func_calls = []

    def _collect_calls(text):
        func_name = text.group(2)
        args_rest = text.group(4)
        print(text.groups())
        print("\t\tfoo: " + text.group(1) + "\t: " + text.group(2) + "\t: " + text.group(4))
        print("\tNew call: '" + func_name + "(self, " + args_rest + ");'")
        return text.group(1)

    reg = r'((check_[a-z_]*)\((&array)*(.*)\);)\s*'
    res_pat = r'ADD_TEST(\g<1>);\n\t'
    #return re.sub(reg, res_pat, text, 0, re.MULTILINE)
    re.sub(reg, _collect_calls, text)
    return text

def _register_local_func_names(text):
    """
    Helper function that registers all local function names
    we want to modify, and use the resulting dictionary later
    in the main function .
    """
    valid_func_names = {}

    print("Local static functions:")

    def _on_match(text):
        """
        Helper function for the helper function.
        Adds matches to the 
        """
        full_match = text.group(1)
        func_name = text.group(5)
        print("\t" + func_name)
        valid_func_names[func_name] = True
        return full_match

    reg = r'((static *(noinline)*? *([a-z_*0-9 ]*) [*]*((?!xarray_exit)(?!xarray_checks)[a-z_0-9]*)\()([a-z_*0-9,\n\t ]*)(\)))'
    # Does not actually modify the input.
    re.sub(reg, _on_match, text)
    return valid_func_names

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
        func_name = text.group(3)
        args = text.group(4)
        rest = text.group(5)
        if func_name in valid_func_names:
            print("\tMatch on '" + func_name + "(" + args + ")" + rest)
            modified_sig = func_name + "(self"
            # Special case because one single function call was 
            # without arguments...
            if args:
                modified_sig += ", " + args + ")" + rest
            else:
                modified_sig += ")" + rest
            print("\targs after: " + modified_sig)
            return modified_sig
        else:
            return text.group(1)
            #print("\tNope! " + func_name + " is not a local function!")

    reg = r'((_)*([a-z_*0-9]+)\((.*?)\)([, \);]))'
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
    

out(#add_self_argument_to_function_calls(
        convert_multi_arg_funcs_to_test_macro(
            convert_func_calls_to_add_test_macro(
                add_self_argument_to_helper_functions(
                    convert_func_signature_to_test_macro(
                        add_ktf_header_and_struct_def(
                            add_ktf_cleanup_context_code(
                                add_ktf_init_code_to_main(
                                    data))))))))#)

#for x in reg.finditer(data):
#    print(x.groups())
