#include <osai/assert.h>
#include <osai/cpu_ai_runtime.h>
#include <osai/klog.h>
#include <osai/model_arena.h>

#define CPU_AI_MAGIC UINT32_C(0x4941494d)
#define CPU_AI_VERSION UINT16_C(1)
#define CPU_AI_QUANTIZATION_SUPPORTED UINT16_C(8)

#define OSAI_CPU_AI_RUNTIME_STATE_EMPTY 0U
#define OSAI_CPU_AI_RUNTIME_STATE_BOUND 1U

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint16_t quantization;
  uint8_t key;
  uint8_t stride;
  uint8_t reserved[6];
} cpu_ai_model_header_t;

typedef struct {
  uint8_t state;
  uint32_t model_arena_id;
  const uint8_t *model_base;
  uint64_t model_size;
  uint64_t decode_calls;
  uint64_t bytes_in;
  uint64_t bytes_out;
  uint8_t key;
  uint8_t stride;
  const char *model_name;
} osai_cpu_ai_runtime_cell_t;

static osai_cpu_ai_runtime_cell_t g_cells[OSAI_CPU_AI_RUNTIME_MAX_CELLS];
static const char k_hex[] = "0123456789ABCDEF";

static int validate_cell_id(uint32_t cell_id) {
  return cell_id < OSAI_CPU_AI_RUNTIME_MAX_CELLS;
}

static void bytes_zero(void *bytes, uint64_t size) {
  uint8_t *ptr = (uint8_t *)bytes;
  for (uint64_t i = 0; i < size; ++i) {
    ptr[i] = 0;
  }
}

static int bytes_equal(const char *lhs, const char *rhs) {
  if (lhs == 0 || rhs == 0) {
    return 0;
  }
  for (uint32_t i = 0;; ++i) {
    if (lhs[i] != rhs[i]) {
      return 0;
    }
    if (lhs[i] == '\0') {
      return 1;
    }
  }
}

void cpu_ai_runtime_init(void) {
  for (uint32_t i = 0; i < OSAI_CPU_AI_RUNTIME_MAX_CELLS; ++i) {
    bytes_zero(&g_cells[i], sizeof(g_cells[i]));
  }
  klog("cpu-ai-runtime: initialized cells=%u\n",
       OSAI_CPU_AI_RUNTIME_MAX_CELLS);
}

osai_status_t cpu_ai_runtime_bind_model(uint32_t cell_id,
                                        uint32_t model_arena_id) {
  if (!validate_cell_id(cell_id)) {
    return OSAI_ERR_INVALID;
  }

  osai_cpu_ai_runtime_cell_t *cell = &g_cells[cell_id];
  if (cell->state != OSAI_CPU_AI_RUNTIME_STATE_EMPTY) {
    return OSAI_ERR_BUSY;
  }

  const osai_model_arena_t *model = 0;
  if (model_arena_acquire(model_arena_id, &model) != OSAI_OK) {
    return OSAI_ERR_INVALID;
  }

  if (model->size < sizeof(cpu_ai_model_header_t)) {
    kassert(model_arena_release(model_arena_id) == OSAI_OK);
    return OSAI_ERR_INVALID;
  }

  const cpu_ai_model_header_t *header =
      (const cpu_ai_model_header_t *)(const void *)model->base;
  if (header->magic != CPU_AI_MAGIC || header->version != CPU_AI_VERSION ||
      header->quantization != CPU_AI_QUANTIZATION_SUPPORTED ||
      header->stride == 0 || header->stride > 32U) {
    kassert(model_arena_release(model_arena_id) == OSAI_OK);
    return OSAI_ERR_INVALID;
  }

  cell->state = OSAI_CPU_AI_RUNTIME_STATE_BOUND;
  cell->model_arena_id = model_arena_id;
  cell->model_base = (const uint8_t *)model->base;
  cell->model_size = model->size;
  cell->key = header->key;
  cell->stride = header->stride;
  cell->model_name = model->name;
  cell->decode_calls = 0;
  cell->bytes_in = 0;
  cell->bytes_out = 0;

  klog("cpu-ai-runtime: cell=%u bound model_id=%u name=%s size=%lu quant=%u stride=%u\n",
       cell_id, model_arena_id,
       cell->model_name != 0 ? cell->model_name : "<anonymous>",
       cell->model_size, header->quantization, cell->stride);
  return OSAI_OK;
}

osai_status_t cpu_ai_runtime_unbind_model(uint32_t cell_id) {
  if (!validate_cell_id(cell_id)) {
    return OSAI_ERR_INVALID;
  }

  osai_cpu_ai_runtime_cell_t *cell = &g_cells[cell_id];
  if (cell->state != OSAI_CPU_AI_RUNTIME_STATE_BOUND) {
    return OSAI_ERR_INVALID;
  }

  const uint32_t model_arena_id = cell->model_arena_id;
  kassert(model_arena_release(model_arena_id) == OSAI_OK);
  bytes_zero(cell, sizeof(*cell));
  klog("cpu-ai-runtime: cell=%u unbound model_id=%u\n", cell_id,
       model_arena_id);
  return OSAI_OK;
}

osai_status_t cpu_ai_runtime_decode_piece(uint32_t cell_id, const uint8_t *piece,
                                         uint64_t piece_bytes, char *output,
                                         uint64_t output_capacity,
                                         uint64_t *output_bytes) {
  if (!validate_cell_id(cell_id) || piece == 0 || output == 0 ||
      output_bytes == 0 || output_capacity == 0) {
    return OSAI_ERR_INVALID;
  }

  osai_cpu_ai_runtime_cell_t *cell = &g_cells[cell_id];
  if (cell->state != OSAI_CPU_AI_RUNTIME_STATE_BOUND) {
    return OSAI_ERR_INVALID;
  }

  if (piece_bytes == 0) {
    *output_bytes = 0;
    if (output_capacity > 0) {
      output[0] = '\0';
    }
    return OSAI_OK;
  }

  const uint64_t required_output = piece_bytes * 2U;
  if (required_output + 1U > output_capacity) {
    return OSAI_ERR_NO_MEMORY;
  }

  for (uint64_t i = 0; i < piece_bytes; ++i) {
    const uint8_t mix =
        (uint8_t)(piece[i] ^ (uint8_t)(cell->key + (cell->stride * i)));
    output[(i * 2U)] = k_hex[(mix >> 4) & 0x0fU];
    output[(i * 2U) + 1U] = k_hex[mix & 0x0fU];
  }

  output[required_output] = '\0';
  *output_bytes = required_output;
  ++cell->decode_calls;
  cell->bytes_in += piece_bytes;
  cell->bytes_out += required_output;

  klog("cpu-ai-runtime: cell=%u decode piece_len=%lu output_len=%lu\n", cell_id,
       piece_bytes, required_output);
  return OSAI_OK;
}

uint64_t cpu_ai_runtime_decode_count(uint32_t cell_id) {
  if (!validate_cell_id(cell_id)) {
    return 0;
  }
  return g_cells[cell_id].decode_calls;
}

void cpu_ai_runtime_self_test(void) {
  cpu_ai_runtime_init();

  static const uint8_t model_image[] = {
      0x4d,
      0x49,
      0x41,
      0x49,
      0x01,
      0x00,
      0x08,
      0x00,
      0x5a,
      0x03,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
  };

  kassert(model_arena_register(2, "cpu-ai-mvp", model_image,
                               sizeof(model_image)) == OSAI_OK);

  kassert(cpu_ai_runtime_bind_model(0, 2) == OSAI_OK);
  const uint8_t piece[] = {'A', 'B', 'C', 'D'};
  char output[32];
  uint64_t out = 0;
  kassert(cpu_ai_runtime_decode_piece(0, piece, sizeof(piece), output,
                                     sizeof(output), &out) == OSAI_OK);
  kassert(out == 8);
  kassert(cpu_ai_runtime_decode_count(0) == 1);
  kassert(bytes_equal(output, "1B1F2327"));
  kassert(cpu_ai_runtime_unbind_model(0) == OSAI_OK);
  klog("cpu-ai-runtime: deterministic decode fixture input=ABCD output=%s\n",
       output);
  kassert(cpu_ai_runtime_decode_piece(0, piece, sizeof(piece), output,
                                     sizeof(output), &out) == OSAI_ERR_INVALID);
  klog("cpu-ai-runtime: self-test passed\n");
}
