#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/dcache.h>
#include <linux/path.h>
#include <linux/fdtable.h>
#include <linux/namei.h>
#include <linux/pid.h>
#include <linux/security.h>
#include <linux/file.h>
#include <linux/seq_file.h>
#include <linux/fs.h>

#include <linux/proc_fs.h>
#include <linux/susfs_def.h>
#endif

#include "../mount.h"
#include "internal.h"
#include "fd.h"


static int seq_show(struct seq_file *m, void *v)
{
	struct files_struct *files = NULL;
	int f_flags = 0, ret = -ENOENT;
	struct file *file = NULL;
	struct task_struct *task;

	task = get_proc_task(m->private);
	if (!task)
		return -ENOENT;

	files = get_files_struct(task);
	put_task_struct(task);

	if (files) {
		unsigned int fd = proc_fd(m->private);

		spin_lock(&files->file_lock);
		file = fcheck_files(files, fd);
		if (file) {
			struct fdtable *fdt = files_fdtable(files);

			f_flags = file->f_flags;
			if (close_on_exec(fd, fdt))
				f_flags |= O_CLOEXEC;

			get_file(file);
			ret = 0;
		}
		spin_unlock(&files->file_lock);
		put_files_struct(files);
	}

	if (ret)
		return ret;

