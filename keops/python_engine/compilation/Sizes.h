#pragma once

#define index_t int*

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)<(b))?(b):(a))
#define MAX3(a,b,c) (MAX(MAX(a,b),c))

#define do_checks 0
#if do_checks
void error(std::string message) {
    throw std::runtime_error(message);
}
#endif
                
int get_min(int *v) {
    int size = v[0];
    if (size==0)
        return -1;
    else {
        int res = v[1];
        for(int i=2; i<size+1; i++)
            res = MIN(v[i],res);
        return res;
    }
}

int get_max(int *v) {
    int size = v[0];
    if (size==0)
        return -1;
    else {
        int res = v[1];
        for(int i=2; i<size+1; i++) {
            res = MAX(v[i],res);
        }
        return res;
    }
}

int get_size(index_t shape) {
    return shape[0];
}

int get_val(index_t shape, int pos) {
    return shape[pos+1];
}
                
#if C_CONTIGUOUS
int get_val_batch(index_t shape, int nbatch, int b) {
  return get_val(shape, b);
}
#else
int get_val_batch(index_t shape, int nbatch, int b) {
  return get_val(shape, nbatch - b);
}
#endif

template < typename TYPE >
class Sizes {
public:
  // constructors
  Sizes(int _nargs, TYPE** args, index_t* argshapes, int _nx, int _ny, 
        int tagIJ_, int use_half_, int dimout_, 
        int *indsI_, int *indsJ_, int *indsP_,
        int *dimsX_, int *dimsY_, int *dimsP_) {
    tagIJ = tagIJ_;
    use_half = use_half_;
    indsI = indsI_;
    indsJ = indsJ_;
    indsP = indsP_;
    pos_first_argI = get_min(indsI);
    pos_first_argJ = get_min(indsJ);
    dimout = dimout_;

    nminargs = 1 + MAX3(get_max(indsI),get_max(indsJ),get_max(indsP));

    nvarsI = get_size(indsI);
    nvarsJ = get_size(indsJ);
    nvarsP = get_size(indsP);                
    dimsX = dimsX_;
    dimsY = dimsY_;
    dimsP = dimsP_;
    nargs = _nargs;
	nx = _nx;
	ny = _ny;
	
    // fill shapes wit "batch dimensions" [A, .., B], the table will look like:
    //
    // [ A, .., B, M, N, D_out]  -> output
    // [ A, .., B, M, 1, D_1  ]  -> "i" variable
    // [ A, .., B, 1, N, D_2  ]  -> "j" variable
    // [ A, .., B, 1, 1, D_3  ]  -> "parameter"
    // [ A, .., 1, M, 1, D_4  ]  -> N.B.: we support broadcasting on the batch dimensions!
    // [ 1, .., 1, M, 1, D_5  ]  ->      (we'll just ask users to fill in the shapes with *explicit* ones)
    fill_shape(_nargs, args, argshapes);
	
    check_ranges(_nargs, args, argshapes);
   
    // fill shape_out
    shape_out = (int*)malloc(sizeof(int)*(nbatchdims + 2));
	// Copy the "batch dimensions":
#if C_CONTIGUOUS
	for(int i=0; i<nbatchdims; i++)
		shape_out[i] = shapes[i];
	if(tagIJ==0)
		shape_out[nbatchdims] = shapes[nbatchdims];
	else
		shape_out[nbatchdims] = shapes[nbatchdims+1];
	shape_out[nbatchdims+1] = shapes[nbatchdims+2];
#else
	for(int i=0; i<nbatchdims; i++)
		shape_out[nbatchdims+1-i] = shapes[i];
	if(tagIJ==0)
		shape_out[1] = shapes[nbatchdims];
	else
		shape_out[1] = shapes[nbatchdims+1];
	shape_out[0] = shapes[nbatchdims+2];
#endif

    // fill nx and ny
    M = shapes[nbatchdims];      // = M
    N = shapes[nbatchdims + 1];  // = N

    // Compute the product of all "batch dimensions"
    nbatches = 1;

    for (int b = 0; b < nbatchdims; b++) {
      nbatches *= shapes[b];
    }
      
    //int nbatches = std::accumulate(shapes, shapes + nbatchdims, 1, std::multiplies< int >());
    
    nx = nbatches * M;  // = A * ... * B * M
    ny = nbatches * N;  // = A * ... * B * N

  }
  
  // attributs
  int nargs;
  int nx, ny;
  int M, N;
  int nbatchdims;
  int nbatches;
  
  int *shapes;
  int *shape_out;
  
  int tagIJ;
  int use_half;
    int *indsI;
    int *indsJ;
    int *indsP;
    int pos_first_argI;
    int pos_first_argJ;
    int dimout;
    int nminargs;
    int nvarsI;
    int nvarsJ;
    int nvarsP;                
    int *dimsX;
    int *dimsY;
    int *dimsP;
  
  // methods

  void switch_to_half2_indexing();

private:
  void fill_shape(int nargs, TYPE** args, index_t* argshapes);
  
  void check_ranges(int nargs, TYPE** args, index_t* argshapes);

  int MN_pos, D_pos;
};




template < typename TYPE >
void Sizes<TYPE>::fill_shape(int nargs, TYPE** args, index_t* argshapes) {

  int pos = (pos_first_argI > pos_first_argJ) ? pos_first_argI : pos_first_argJ;

  if (pos > -1) {
    // Are we working in batch mode? Infer the answer from the first arg =============
    nbatchdims =  get_size(argshapes[pos]) - 2;  // number of batch dimensions = Number of dims of the first tensor - 2

    if (nbatchdims < 0) {
#if do_checks
      error("[KeOps] Wrong number of dimensions for arg at position 0: is "
                  + std::to_string(get_size(argshapes[0])) + " but should be at least 2."
      );
#endif
    }
  } else {
    nbatchdims = 0;
  }
  
#if C_CONTIGUOUS
  MN_pos = nbatchdims;
  D_pos = nbatchdims + 1;
#else
  D_pos = 0;
  MN_pos = 1;
#endif
  
  // Now, we'll keep track of the output + all arguments' shapes in a large array:
  shapes = (int*) malloc(sizeof(int)*((nargs + 1) * (nbatchdims + 3)));


  for (int i=0; i<(nargs + 1) * (nbatchdims + 3); i++)
	  shapes[i] = 1;


  
  if(use_half) {
    if(tagIJ==0) {
        shapes[nbatchdims] = nx%2? nx+1 : nx;
        shapes[nbatchdims + 1] = 2*ny;
    } else {
        shapes[nbatchdims] = 2*nx;
        shapes[nbatchdims + 1] = ny%2? ny+1 : ny;
    }
  } else {
    shapes[nbatchdims] = nx;
    shapes[nbatchdims + 1] = ny;
  }
  
  shapes[nbatchdims + 2] = dimout;   // Top right corner: dimension of the output
  
}

template < typename TYPE >
void Sizes<TYPE>::check_ranges(int nargs, TYPE** args, index_t* argshapes) {

  // Check the compatibility of all tensor shapes ==================================
  if (nminargs > 0) {
    
    // Checks args in all the positions that correspond to "i" variables:
    for (int k = 0; k < nvarsI; k++) {
      int i = get_val(indsI,k);

      // Fill in the (i+1)-th line of the "shapes" array ---------------------------
      int off_i = (i + 1) * (nbatchdims + 3);
      
      // Check the number of dimensions --------------------------------------------
      int ndims = get_size(argshapes[i]);  // Number of dims of the i-th tensor
      
#if do_checks
      if (ndims != nbatchdims + 2) {
        error("[KeOps] Wrong number of dimensions for arg at position " + std::to_string(i)
                    + " (i type): KeOps detected " + std::to_string(nbatchdims)
                    + " batch dimensions from the first argument 0, and thus expected "
                    + std::to_string(nbatchdims + 2)
                    + " dimensions here, but only received "
                    + std::to_string(ndims)
                    + ". Note that KeOps supports broadcasting on batch dimensions, "
                    + "but still expects 'dummy' unit dimensions in the input shapes, "
                    + "for the sake of clarity.");
      }
#endif
  
  
  
      // First, the batch dimensions:
      for (int b = 0; b < nbatchdims; b++) {
        shapes[off_i + b] = get_val_batch(argshapes[i], nbatchdims + 2, b);
        
        // Check that the current value is compatible with what
        // we've encountered so far, as stored in the first line of "shapes"
        if (shapes[off_i + b] != 1) {  // This dimension is not "broadcasted"
          if (shapes[b] == 1) {
            shapes[b] = shapes[off_i + b];  // -> it becomes the new standard
          } 
#if do_checks
		  else if (shapes[b] != shapes[off_i + b]) {
            error("[KeOps] Wrong value of the batch dimension "
                        + std::to_string(b) + " for argument number " + std::to_string(i)
                        + " : is " + std::to_string(shapes[off_i + b])
                        + " but was " + std::to_string(shapes[b])
                        + " or 1 in previous arguments.");
          }
#endif
        }
      }

      shapes[off_i + nbatchdims] = get_val(argshapes[i], MN_pos);  // = "M"
      shapes[off_i + nbatchdims + 2] = get_val(argshapes[i], D_pos);  // = "D"


#if do_checks
      // Check the number of "lines":
      if (shapes[nbatchdims] != shapes[off_i + nbatchdims]) {
        error("[KeOps] Wrong value of the 'i' dimension "
                    + std::to_string(nbatchdims) + "for arg at position " + std::to_string(i)
                    + " : is " + std::to_string(shapes[off_i + nbatchdims])
                    + " but was " + std::to_string(shapes[nbatchdims])
                    + " in previous 'i' arguments.");
      }
      
      // And the number of "columns":
      if (shapes[off_i + nbatchdims + 2] != static_cast< int >(dimsX[k])) {
        error("[KeOps] Wrong value of the 'vector size' dimension "
                    + std::to_string(nbatchdims + 1) + " for arg at position " + std::to_string(i)
                    + " : is " + std::to_string(shapes[off_i + nbatchdims + 2])
                    + " but should be " + std::to_string(dimsX[k]));
      }
#endif
    }

      
    // Checks args in all the positions that correspond to "j" variables:
    for (int k = 0; k < nvarsJ; k++) {
      int i = get_val(indsJ,k);
	  
      // Check the number of dimensions --------------------------------------------
      int ndims = get_size(argshapes[i]);  // Number of dims of the i-th tensor
      
#if do_checks
      if (ndims != nbatchdims + 2) {
        error("[KeOps] Wrong number of dimensions for arg at position " + std::to_string(i)
                    + " (j type): KeOps detected " + std::to_string(nbatchdims)
                    + " batch dimensions from the first argument 0, and thus expected "
                    + std::to_string(nbatchdims + 2)
                    + " dimensions here, but only received "
                    + std::to_string(ndims)
                    + ". Note that KeOps supports broadcasting on batch dimensions, "
                    + "but still expects 'dummy' unit dimensions in the input shapes, "
                    + "for the sake of clarity.");
      }
#endif
      
      // Fill in the (i+1)-th line of the "shapes" array ---------------------------
      int off_i = (i + 1) * (nbatchdims + 3);
      
      // First, the batch dimensions:
      for (int b = 0; b < nbatchdims; b++) {
        shapes[off_i + b] = get_val_batch(argshapes[i], nbatchdims + 2, b);
        
        // Check that the current value is compatible with what
        // we've encountered so far, as stored in the first line of "shapes"
        if (shapes[off_i + b] != 1) {  // This dimension is not "broadcasted"
          if (shapes[b] == 1) {
            shapes[b] = shapes[off_i + b];  // -> it becomes the new standard
          } 
#if do_checks
		  else if (shapes[b] != shapes[off_i + b]) {
            error("[KeOps] Wrong value of the batch dimension "
                        + std::to_string(b) + " for argument number " + std::to_string(i)
                        + " : is " + std::to_string(shapes[off_i + b])
                        + " but was " + std::to_string(shapes[b])
                        + " or 1 in previous arguments.");
          }
#endif
        }
      }
 
      shapes[off_i + nbatchdims + 1] = get_val(argshapes[i], MN_pos);  // = "N"
      shapes[off_i + nbatchdims + 2] = get_val(argshapes[i], D_pos);  // = "D"
 
     
#if do_checks
      // Check the number of "lines":
      if (shapes[nbatchdims + 1] != shapes[off_i + nbatchdims + 1]) {
        error("[KeOps] Wrong value of the 'j' dimension "
                    + std::to_string(nbatchdims) + " for arg at position " + std::to_string(i)
                    + " : is " + std::to_string(shapes[off_i + nbatchdims + 1])
                    + " but was " + std::to_string(shapes[nbatchdims + 1])
                    + " in previous 'j' arguments.");
      }
      
      // And the number of "columns":
      if (shapes[off_i + nbatchdims + 2] != static_cast< int >(dimsY[k])) {
        error("[KeOps] Wrong value of the 'vector size' dimension "
                    + std::to_string(nbatchdims + 1) + " for arg at position " + std::to_string(i)
                    + " : is " + std::to_string(shapes[off_i + nbatchdims + 2])
                    + " but should be " + std::to_string(dimsY[k]));
      }
#endif
    }

    
    for (int k = 0; k < nvarsP; k++) {
      int i = get_val(indsP,k);
      // Fill in the (i+1)-th line of the "shapes" array ---------------------------
      int off_i = (i + 1) * (nbatchdims + 3);
      // First, the batch dimensions:
      for (int b = 0; b < nbatchdims; b++) {
        shapes[off_i + b] = get_val_batch(argshapes[i], nbatchdims + 2, b);
      }
      shapes[off_i + nbatchdims + 2] = get_val(argshapes[i], nbatchdims);  // = "D" 
#if do_checks
      int dim_param;
      if (use_half)  
        dim_param = shapes[off_i + nbatchdims + 2] / 2;
      else
        dim_param = shapes[off_i + nbatchdims + 2];
      if (dim_param != static_cast< int >(dimsP[k])) {
        error("[KeOps] Wrong value of the 'vector size' dimension "
                    + std::to_string(nbatchdims) + " for arg at position " + std::to_string(i)
                    + " : is " + std::to_string(dim_param)
                    + " but should be " + std::to_string(dimsP[k]));
      }
#endif
    }
  }

}

template < typename TYPE >
void Sizes<TYPE>::switch_to_half2_indexing() {
    // special case of float16 inputs : because we use half2 type in Cuda codes, we need to divide by two nx, ny, and M, N, or D
    // values inside the shapes vector.
    nx = nx / 2;
    ny = ny / 2;
    M = M / 2;
    N = N / 2;
    shapes[nbatchdims] = shapes[nbatchdims] / 2;
    shapes[nbatchdims+1] = shapes[nbatchdims+1] / 2;
    for (int i = 0; i < nargs; i++) {
        int off_i = (i + 1) * (nbatchdims + 3);
        // we don't have anymore the category information...
        // the last three dimensions are either of the form (M,1,D), (1,N,D), or (1,1,D)
        // where M or N are even in the 2 first cases, or D is even in the third case.
        if(shapes[off_i+nbatchdims]>1)
            shapes[off_i+nbatchdims] = shapes[off_i+nbatchdims] / 2;
        else if(shapes[off_i+nbatchdims+1]>1)
            shapes[off_i+nbatchdims+1] = shapes[off_i+nbatchdims+1] / 2;
        else
            shapes[off_i+nbatchdims+2] = shapes[off_i+nbatchdims+2] / 2;   
    }
}
