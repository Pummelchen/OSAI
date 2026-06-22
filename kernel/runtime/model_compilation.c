#include <xaios/ai_kernels.h>
#include <xaios/assert.h>
#include <xaios/klog.h>

/*
 * Model Compilation
 *
 * Dispatches to generic NEON kernels as the default compilation target.
 * Future: Parse model graph, generate optimized code, JIT compile.
 * Returns XAIOS_ERR_INVALID when no specialized kernel can be generated,
 * signaling the caller to use generic kernels directly.
 */

xaios_status_t ai_compile_model(const char *model_graph,
                                xaios_compiled_kernel_t *kernel_out) {
  kassert(model_graph != 0 && kernel_out != 0);
  
  /* No specialized compilation available; caller should use generic kernels */
  kernel_out->kernel_id = 0;
  kernel_out->quant = XAIOS_QUANT_Q88;
  kernel_out->execute = 0;
  
  klog("ai-compile: no specialized compilation available, returning fallback\n");
  
  return XAIOS_ERR_INVALID;
}

xaios_status_t ai_execute_compiled(const xaios_compiled_kernel_t *kernel,
                                   const void *input,
                                   void *output,
                                   uint64_t input_bytes,
                                   uint64_t output_bytes) {
  kassert(kernel != 0 && input != 0 && output != 0);
  (void)input_bytes;
  (void)output_bytes;
  
  /* Stub: If no compiled kernel, fallback to generic */
  if (kernel->execute == 0) {
    klog("ai-execute: no compiled kernel, using generic kernels\n");
    return XAIOS_ERR_INVALID;
  }
  
  /* Execute generated kernel */
  kernel->execute(input, output);
  
  return XAIOS_OK;
}
