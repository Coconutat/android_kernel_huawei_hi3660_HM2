#ifndef __LINUX_ZRHUNG_WP_IO_H__
#define __LINUX_ZRHUNG_WP_IO_H__

#ifdef CONFIG_HW_ZEROHUNG
__maybe_unused void iowp_workqueue_init(void);
__maybe_unused void iowp_report(pid_t pid , pid_t count, char* name);
#endif
#endif
