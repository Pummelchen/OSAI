#include <osai_user.h>

static int text_equal(const char *lhs, const char *rhs) {
  if (lhs == 0 || rhs == 0) {
    return 0;
  }
  for (u64 i = 0;; ++i) {
    if (lhs[i] != rhs[i]) {
      return 0;
    }
    if (lhs[i] == '\0') {
      return 1;
    }
  }
}

int main(void) {
  char output[96];
  u64 out = 0;
  const unsigned char xor_input[2] = {1, 0};
  const unsigned char sum_input[4] = {1, 2, 3, 4};
  const unsigned char parity_input[3] = {1, 1, 1};

  osai_log("/bin/mltest: validating general CPU-only ML runtime dispatcher\n");
  osai_memzero(output, sizeof(output));
  if (osai_ml_run(OSAI_ML_MODEL_XOR, xor_input, sizeof(xor_input), output,
                  sizeof(output), &out) < 0 ||
      !text_equal(output, "1")) {
    osai_log("/bin/mltest: xor model failed\n");
    return 1;
  }
  osai_log("/bin/mltest: xor output=");
  osai_log(output);
  osai_log("\n");

  osai_memzero(output, sizeof(output));
  if (osai_ml_run(OSAI_ML_MODEL_SUM, sum_input, sizeof(sum_input), output,
                  sizeof(output), &out) < 0 ||
      !text_equal(output, "10")) {
    osai_log("/bin/mltest: sum model failed\n");
    return 1;
  }
  osai_log("/bin/mltest: sum output=");
  osai_log(output);
  osai_log("\n");

  osai_memzero(output, sizeof(output));
  if (osai_ml_run(OSAI_ML_MODEL_PARITY, parity_input, sizeof(parity_input),
                  output, sizeof(output), &out) < 0 ||
      !text_equal(output, "odd")) {
    osai_log("/bin/mltest: parity model failed\n");
    return 1;
  }
  osai_log("/bin/mltest: parity output=");
  osai_log(output);
  osai_log("\n");
  osai_log("/bin/mltest: multi-model CPU-only ML runtime passed\n");
  osai_log("/bin/mltest: complete\n");
  return 0;
}
