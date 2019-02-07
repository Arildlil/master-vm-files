import re

filename = "kernel/test_xarray_rewrite.c"

with open(filename, 'r') as f:
    data = f.read()

#print(data)

def convert_to_test_macro(text):
    """
    Converts regular test functions on the format
    "static noinline void check_XXX(struct xarray *xa)
    {",
    where XXX is the rest of the function name after 'check_'. It 
    captures and replaces the { in addition to the function 
    signature, as this function also adds context related code to 
    the beginning of the function.
    """
    reg = r"static noinline void (check_.*)\((struct xarray [*]xa|void)\)\n\s*({)"
    ctx_code = r"""
        struct array_context *actx = KTF_CONTEXT_GET("array", struct array_context);
        struct xarray *xa = actx->xa;
        """
    res_pat = r"TEST(test_xarray_rewrite, \g<1>)\n{" + ctx_code
    res = re.sub(reg, res_pat, text, 0, re.MULTILINE)
    return res
    
print(convert_to_test_macro(data))

#for x in reg.finditer(data):
#    print(x.groups())
