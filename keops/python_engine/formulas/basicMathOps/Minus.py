from keops.python_engine.formulas.VectorizedScalarOp import VectorizedScalarOp
from keops.python_engine.formulas.variables.Zero import Zero


##########################
######    Minus      #####
##########################

class Minus_Impl(VectorizedScalarOp):
    """the "minus" vectorized operation"""
    string_id = "Minus"
    print_spec = "-", "pre", 2

    def ScalarOp(self, out, arg):
        # returns the atomic piece of c++ code to evaluate the function on arg and return
        # the result in out
        return f"{out.id} = -{arg.id};\n"

    def DiffT(self, v, gradin):
        f = self.children[0]
        return -f.DiffT(v, gradin)


# N.B. The following separate function should theoretically be implemented
# as a __new__ method of the previous class, but this can generate infinite recursion problems
def Minus(arg):
    if isinstance(arg, Zero):
        return arg
    else:
        return Minus_Impl(arg)