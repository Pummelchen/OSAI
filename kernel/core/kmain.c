#include <osai/assert.h>
#include <osai/ai_cell.h>
#include <osai/arena.h>
#include <osai/arp.h>
#include <osai/boot_info.h>
#include <osai/core_lease.h>
#include <osai/elf_loader.h>
#include <osai/exception.h>
#include <osai/gic.h>
#include <osai/icmp.h>
#include <osai/initramfs.h>
#include <osai/cpu_ai_runtime.h>
#include <osai/ipv4.h>
#include <osai/kheap.h>
#include <osai/git_workspace.h>
#include <osai/klog.h>
#include <osai/model_arena.h>
#include <osai/mutable_fs.h>
#include <osai/pmm.h>
#include <osai/persistence.h>
#include <osai/remote_login.h>
#include <osai/sandbox.h>
#include <osai/scheduler.h>
#include <osai/security.h>
#include <osai/source_index.h>
#include <osai/service.h>
#include <osai/smp.h>
#include <osai/network_stack.h>
#include <osai/syscall.h>
#include <osai/telemetry.h>
#include <osai/timer.h>
#include <osai/update.h>
#include <osai/user.h>
#include <osai/virtio_blk.h>
#include <osai/virtio_net.h>
#include <osai/vmm.h>

static const char g_vmm_rodata_probe[] = "vmm-rodata";
static uint64_t g_vmm_data_probe;

static void run_user_app(const char *path, uint32_t pid, uint64_t capabilities) {
  const osai_initramfs_file_t *file = 0;
  osai_user_process_t process;
  kassert(initramfs_lookup(path, &file) == OSAI_OK);
  kassert(user_load_process(file, pid, capabilities, &process) == OSAI_OK);
  int exit_code = user_process_run(&process);
  kassert(exit_code == 0);
  klog("kernel: %s returned to kernel exit_code=%u\n",
       path, (unsigned)exit_code);
  user_process_reclaim_address_space(&process);
}

static void map_mmio_range(uint64_t start, uint64_t size) {
  const uint64_t page_size = 4096;
  uint64_t page = start & ~(page_size - 1U);
  uint64_t end = (start + size + page_size - 1U) & ~(page_size - 1U);
  while (page < end) {
    kassert(vmm_map_page(page, page,
                         OSAI_VMM_PRESENT | OSAI_VMM_WRITABLE |
                             OSAI_VMM_DEVICE) == OSAI_OK);
    page += page_size;
  }
}

void kmain(const osai_boot_info_t *boot) {
  klog_init(boot);
  klog("OSAI kernel starting\n");
  kassert(boot->magic == OSAI_BOOT_INFO_MAGIC);
  kassert(boot->version == OSAI_BOOT_INFO_VERSION);

  klog("boot: memory_map=0x%lx size=%lu desc_size=%lu\n",
       boot->memory_map, boot->memory_map_size, boot->memory_descriptor_size);
  klog("boot: kernel=[0x%lx, 0x%lx)\n",
       boot->kernel_phys_base, boot->kernel_phys_end);

  exception_init();
  exception_self_test();
  timer_init();
  timer_self_test();
  smp_init_qemu_virt();
  smp_self_test();

  pmm_init(boot);
  vmm_init(boot);
  vmm_self_test();
  map_mmio_range(boot->uart_base, 4096);
  map_mmio_range(UINT64_C(0x08000000), UINT64_C(0x20000));
  map_mmio_range(UINT64_C(0x0a000000), UINT64_C(0x4000));
  klog("VMM MMIO device mappings installed\n");
  kheap_self_test();
  arena_manager_init();
  arena_self_test();
  security_self_test();
  remote_login_self_test();
  source_index_runtime_init();
  source_index_self_test();
  git_workspace_runtime_init();
  git_workspace_self_test();
  sandbox_self_test();
  core_lease_self_test();
  uint64_t translated = 0;
  uint32_t flags = 0;
  kassert(vmm_translate((uint64_t)(uintptr_t)&kmain, &translated, &flags) == OSAI_OK);
  kassert(translated == (uint64_t)(uintptr_t)&kmain);
  kassert((flags & OSAI_VMM_EXECUTABLE) != 0);
  kassert((flags & OSAI_VMM_DEVICE) == 0);
  kassert(vmm_translate((uint64_t)(uintptr_t)g_vmm_rodata_probe, &translated, &flags) == OSAI_OK);
  kassert(translated == (uint64_t)(uintptr_t)g_vmm_rodata_probe);
  kassert((flags & OSAI_VMM_WRITABLE) == 0);
  kassert((flags & OSAI_VMM_EXECUTABLE) == 0);
  kassert(vmm_translate((uint64_t)(uintptr_t)&g_vmm_data_probe, &translated, &flags) == OSAI_OK);
  kassert(translated == (uint64_t)(uintptr_t)&g_vmm_data_probe);
  kassert((flags & OSAI_VMM_WRITABLE) != 0);
  kassert((flags & OSAI_VMM_EXECUTABLE) == 0);
  kassert(vmm_translate(boot->uart_base, &translated, &flags) == OSAI_OK);
  kassert(translated == boot->uart_base);
  kassert((flags & OSAI_VMM_DEVICE) != 0);
  kassert((flags & OSAI_VMM_EXECUTABLE) == 0);
  klog("VMM translation test passed\n");
  gic_init_qemu_virt();
  gic_self_test();

  virtio_block_self_test();
  persistence_self_test();
  mutable_fs_self_test();
  /* mount persistent filesystem on 3rd VirtIO block device (slot 2) */
  osai_status_t persistent_status = mutable_fs_mount_persistent(2);
  if (persistent_status == OSAI_OK) {
    osai_mfs_fsck_result_t fsck = mutable_fs_fsck();
    klog("kernel: persistent fsck valid=%u v%u files=%lu dirs=%lu\n",
         fsck.valid, fsck.version, fsck.files, fsck.directories);
  } else {
    klog("kernel: persistent mount skipped status=%d\n", (int)persistent_status);
  }
  update_self_test();
  virtio_net_self_test();
  arp_self_test();
  ipv4_self_test();
  icmp_self_test();
  network_stack_self_test();
  initramfs_self_test();
  syscall_self_test();
  user_process_table_init();
  user_process_lifecycle_self_test();
  user_scheduler_self_test();
  scheduler_init();
  scheduler_self_test();
  elf_loader_self_test();
  service_supervisor_init();
  model_arena_self_test();
  cpu_ai_runtime_self_test();
  ai_cell_self_test();
  telemetry_emit_boot_summary();

#if defined(OSAI_FAULT_TEST_PAGE)
  exception_trigger_page_fault_for_test();
#elif defined(OSAI_FAULT_TEST_RO)
  klog("exceptions: triggering controlled rodata write fault\n");
  volatile char *ro = (volatile char *)(uintptr_t)g_vmm_rodata_probe;
  *ro = 'X';
#elif defined(OSAI_FAULT_TEST_NX)
  klog("exceptions: triggering controlled NX execute fault\n");
  void (*bad_exec)(void) = (void (*)(void))(uintptr_t)&g_vmm_data_probe;
  bad_exec();
#endif

  void *pages[1024];
  for (unsigned i = 0; i < 1024; ++i) {
    pages[i] = pmm_alloc_page();
    kassert(pages[i] != 0);
  }
  for (unsigned i = 0; i < 1024; ++i) {
    pmm_free_page(pages[i]);
  }

  klog("PMM 1024 page allocate/free test passed\n");

  const osai_initramfs_file_t *init_file = 0;
  const osai_initramfs_file_t *manager_file = 0;
  const osai_initramfs_file_t *worker_file = 0;
  osai_user_process_t init_process;
  osai_user_process_t manager_process;
  const osai_initramfs_config_t *init_config = initramfs_config();
  kassert(init_config != 0);
  kassert(initramfs_lookup(init_config->service_path, &init_file) == OSAI_OK);
  kassert(initramfs_lookup(init_config->service_manager_path, &manager_file) ==
          OSAI_OK);
  kassert(initramfs_lookup("/bin/osai-worker", &worker_file) == OSAI_OK);
  kassert(user_load_init(init_file, &init_process) == OSAI_OK);
  int init_exit_code = user_process_run(&init_process);
  kassert(init_exit_code == 0);
  klog("kernel: /init returned to kernel exit_code=%u\n",
       (unsigned)init_exit_code);
  user_process_reclaim_address_space(&init_process);

  kassert(user_load_process(manager_file, 2,
                            OSAI_CAP_LOG | OSAI_CAP_EXIT | OSAI_CAP_OSCTL |
                                OSAI_CAP_FS_READ | OSAI_CAP_SERVICE_CONTROL |
                                OSAI_CAP_ADMIN | OSAI_CAP_FS_WRITE,
                            &manager_process) == OSAI_OK);
  kassert(service_start(init_config->service_manager_path) == OSAI_OK);
  int manager_exit_code = user_process_run(&manager_process);
  kassert(manager_exit_code == 0);
  klog("kernel: /bin/service-manager returned to kernel exit_code=%u\n",
       (unsigned)manager_exit_code);
  user_process_reclaim_address_space(&manager_process);

  /* Initialize persistent network for real TX/RX */
  if (virtio_net_init_persistent() == OSAI_OK) {
    network_init_persistent();
    klog("kernel: persistent network stack enabled\n");
  } else {
    klog("kernel: persistent network init skipped\n");
  }

  /* Initialize preemptive scheduler infrastructure */
  gic_enable_full();
  timer_enable_periodic(OSAI_SCHEDULER_DEFAULT_TICK_HZ);
  klog("kernel: preemptive scheduler infrastructure enabled\n");

  for (uint32_t pid = 3; pid <= 5; ++pid) {
    osai_user_process_t worker_process;
    kassert(user_load_process(worker_file, pid, OSAI_CAP_LOG | OSAI_CAP_EXIT,
                              &worker_process) == OSAI_OK);
    kassert(user_process_make_runnable(pid, 2) == OSAI_OK);
    kassert(user_process_snapshot(pid, &worker_process) == OSAI_OK);
    kassert(service_start("/bin/osai-worker") == OSAI_OK);
    int worker_exit_code = user_process_run(&worker_process);
    kassert(worker_exit_code == 0);
    klog("kernel: /bin/osai-worker pid=%u returned to kernel exit_code=%u\n",
         pid, (unsigned)worker_exit_code);
    user_process_reclaim_address_space(&worker_process);
  }

  /* Disable preemptive infrastructure after concurrent workers */
  timer_disable();
  gic_disable_full();

  const uint64_t app_caps = OSAI_CAP_LOG | OSAI_CAP_EXIT | OSAI_CAP_OSCTL |
                            OSAI_CAP_FS_READ | OSAI_CAP_FS_WRITE |
                            OSAI_CAP_TIME | OSAI_CAP_NET | OSAI_CAP_SMP |
                            OSAI_CAP_CPU_AI | OSAI_CAP_REMOTE_LOGIN |
                            OSAI_CAP_THREADS | OSAI_CAP_ML |
                            OSAI_CAP_NET_SOCKET;
  run_user_app("/bin/osai-shell", 6, app_caps);
  run_user_app("/bin/hello", 7, app_caps);
  run_user_app("/bin/sysinfo", 8, app_caps);
  run_user_app("/bin/systest", 9, app_caps);
  run_user_app("/bin/smptest", 10, app_caps);
  run_user_app("/bin/nettest", 11, app_caps);
  run_user_app("/bin/lstm-xor", 12, app_caps);
  run_user_app("/bin/sshtest", 13, app_caps);
  run_user_app("/bin/mltest", 14, app_caps);

  telemetry_emit_boot_summary();

  for (;;) {
    __asm__ volatile("wfe");
  }
}
