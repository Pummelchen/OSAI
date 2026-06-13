#include <osai/assert.h>
#include <osai/initramfs.h>
#include <osai/klog.h>

extern char __user_text_start[];
extern char __user_text_end[];

static osai_initramfs_file_t g_files[1];

static int str_eq(const char *a, const char *b) {
  while (*a != '\0' && *b != '\0') {
    if (*a != *b) {
      return 0;
    }
    ++a;
    ++b;
  }
  return *a == '\0' && *b == '\0';
}

void initramfs_init(void) {
  g_files[0].path = "/init";
  g_files[0].base = __user_text_start;
  g_files[0].size = (uint64_t)(__user_text_end - __user_text_start);
  g_files[0].executable = 1;
  klog("initramfs: registered /init base=0x%lx size=%lu\n",
       (uint64_t)(uintptr_t)g_files[0].base, g_files[0].size);
}

osai_status_t initramfs_lookup(const char *path,
                               const osai_initramfs_file_t **file) {
  if (str_eq(path, g_files[0].path)) {
    *file = &g_files[0];
    return OSAI_OK;
  }
  return OSAI_ERR_INVALID;
}

void initramfs_self_test(void) {
  initramfs_init();
  const osai_initramfs_file_t *init = 0;
  kassert(initramfs_lookup("/init", &init) == OSAI_OK);
  kassert(init != 0);
  kassert(init->base != 0);
  kassert(init->size != 0);
  kassert(init->executable != 0);
  kassert(initramfs_lookup("/missing", &init) == OSAI_ERR_INVALID);
  klog("initramfs: lookup self-test passed\n");
}
