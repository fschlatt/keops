
import numpy as np

from pykeops.numpy import Genred

from pykeops import default_cuda_type
from pykeops.common.utils import axis2cat, cat2axis
from pykeops.common.parse_type import get_type, get_sizes, complete_aliases
from pykeops.common.get_options import get_tag_backend
from pykeops.common.keops_io import load_keops

from pykeops.common.linsolve import ConjugateGradientSolver
        
class InvKernelOp:
    
    def __init__(self, formula, aliases, varinvalias, reduction_op='Sum', axis=0, cuda_type=default_cuda_type, opt_arg=None):
        if opt_arg:
            self.formula = reduction_op + 'Reduction(' + formula + ',' + str(opt_arg) + ',' + str(axis2cat(axis)) + ')'
        else:
            self.formula = reduction_op + 'Reduction(' + formula + ',' + str(axis2cat(axis)) + ')'
        self.aliases = complete_aliases(formula, aliases)
        self.varinvalias = varinvalias
        self.cuda_type = cuda_type
        self.myconv = load_keops(self.formula,  self.aliases,  self.cuda_type, 'numpy')

        tmp = aliases.copy()
        for (i,s) in enumerate(tmp):
            tmp[i] = s[:s.find("=")].strip()
        self.varinvpos = tmp.index(varinvalias)

    def __call__(self, *args, backend='auto', device_id=-1):
        # Get tags
        tagCpuGpu, tag1D2D, _ = get_tag_backend(backend, args)
        nx, ny = get_sizes(self.aliases, *args)
        varinv = args[self.varinvpos]      
        def linop(var):
            newargs = args[:self.varinvpos] + (var,) + args[self.varinvpos+1:]
            return self.myconv.genred_numpy(nx, ny, tagCpuGpu, tag1D2D, 0, device_id, *newargs)
        return ConjugateGradientSolver('numpy',linop,varinv,eps=1e-16)
     
     
     



