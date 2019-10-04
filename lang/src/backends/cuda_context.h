#if defined(TLANG_WITH_CUDA)
#include <cuda.h>
#endif
#include "llvm_jit.h"
#include "context.h"

TLANG_NAMESPACE_BEGIN

class CUDAContext {
  CUdevice device;
  CUmodule cudaModule;
  CUcontext context;
  CUlinkState linker;
  int devCount;
  CUdeviceptr context_buffer;

 public:
  CUDAContext();

  CUfunction compile(const std::string &ptx, const std::string &kernel_name);

  void launch(CUfunction func,
              void *context_ptr,
              unsigned gridDim,
              unsigned blockDim);

  ~CUDAContext();
};

extern CUDAContext cuda_context;

TLANG_NAMESPACE_END
