import re, os, argparse

default_filename = "/home/ubuntu/src/test_xarray_rewrite/kernel/test_xarray_rewrite.c"

parser = argparse.ArgumentParser(
    description='A python script hand crafted for converting "test_xarray.c" to KTF.')
parser.add_argument('-f', '--file', action='store', default=default_filename)
args = parser.parse_args()
print(args)
print(args.file)

with open(args.file, 'r') as f:
    data = f.read()


#print(data)

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

def add_ktf_context_add_to_main(text):
    """
    Adds a call to KTF_CONTEXT_ADD to the beginning of the
    main function.
    """
    reg = r'(static int xarray_checks\(void\)\s*?{\s*)'
    ctx_code = r'KTF_CONTEXT_ADD(&cxa.k, "array");'
    res_pat = r'\g<1>{}\n\n\t'.format(ctx_code)
    return re.sub(reg, res_pat, text, 0, re.MULTILINE)
    

print(
    add_ktf_context_add_to_main(
        convert_func_signature_to_test_macro(
            convert_func_calls_to_add_test_macro(data))))

#for x in reg.finditer(data):
#    print(x.groups())
