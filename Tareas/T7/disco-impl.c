#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h> /* printk() */
#include <linux/slab.h> /* kmalloc() */
#include <linux/fs.h> /* everything... */
#include <linux/errno.h> /* error codes */
#include <linux/types.h> /* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h> /* O_ACCMODE */
#include <linux/uaccess.h> /* copy_from/to_user */

#include "kmutex.h"

MODULE_LICENSE("Dual BSD/GPL");

/* Declaration of disc.c functions */
int disc_open(struct inode *inode, struct file *filp);
int disc_release(struct inode *inode, struct file *filp);
ssize_t disc_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);
ssize_t disc_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos);
int disc_init(void);
void disc_exit(void);

struct file_operations disc_fops = {
	open: disc_open,
	release: disc_release,
	read: disc_read,
	write: disc_write
};

module_init(disc_init);
module_exit(disc_exit);

#define MAX_SIZE 4096
int disc_major = 61;

typedef struct {
	int reader, writer, size;
	char *disc_buffer;

	int writer_closed;

	KMutex mutex;
	KCondition cond;
} Pipe;

Pipe *global_pipe;
int waiting = 0;

int disc_init(void) {
	int rc;

	rc = register_chrdev(disc_major, "disc", &disc_fops);
	if (rc < 0) {
		printk("<1>disc: cannot obtain major number %d\n", disc_major);
		return rc;
	}

	printk("<1>Inserting disc module\n");
	return 0;
}

void disc_exit(void) {
	unregister_chrdev(disc_major, "disc");
	printk("<1>Removing disc module\n");
}

int disc_open(struct inode *inode, struct file *filp) {
	int rc = 0;

	if (!filp->private_data) {
		if (waiting) {
			filp->private_data = global_pipe;
			waiting = 0;
		} else {
			printk("<1>nuevo pipe\n");
			global_pipe = kmalloc(sizeof(Pipe), GFP_KERNEL);
			if (global_pipe == NULL) {
				disc_exit();
				return -ENOMEM;
			}
			memset(global_pipe, 0, sizeof(Pipe));

			char *disc_buffer = kmalloc(MAX_SIZE, GFP_KERNEL);
			if (disc_buffer == NULL) {
				disc_exit();
				return -ENOMEM;
			}
			memset(disc_buffer, 0, MAX_SIZE);

			*global_pipe = (Pipe) {0, 0, 0, disc_buffer, 0 /*, m_init(&mutex), c_init(&cond)*/};
			m_init(&global_pipe->mutex);
			c_init(&global_pipe->cond);
			filp->private_data = global_pipe;
		}
	}
	Pipe *pipe = (Pipe*) filp->private_data;

	m_lock(&pipe->mutex);
	if (filp->f_mode & FMODE_WRITE) {
		printk("<1>open request for write\n");
		pipe->writer = 1;
		int rc;

		while (!pipe->reader) {
			waiting = 1;
			printk("<1>writer esperando\n");
			if (c_wait(&pipe->cond, &pipe->mutex)) {
				c_broadcast(&pipe->cond);
				rc = -EINTR;
				goto epilog;
			}
		}

		c_broadcast(&pipe->cond);
		printk("<1>open for write successful\n");
	} else if (filp->f_mode & FMODE_READ) {
		printk("<1>open request for read\n");
		pipe->reader = 1;

		while (!pipe->writer) {
			waiting = 1;
			printk("<1>reader esperando\n");
			if (c_wait(&pipe->cond, &pipe->mutex)) {
				rc = -EINTR;
				goto epilog;
			}
		}

		c_broadcast(&pipe->cond);
		printk("<1>open for read successful\n");
	}

epilog:
	m_unlock(&pipe->mutex);
	return rc;
}

int disc_release(struct inode *inode, struct file *filp) {
	Pipe *pipe = (Pipe*) filp->private_data;
	m_lock(&pipe->mutex);

	if (filp->f_mode & FMODE_WRITE) {
		pipe->writer = 0;
		pipe->writer_closed = 1;
		if (!pipe->reader)
			waiting = 0;
		c_broadcast(&pipe->cond);
		printk("<1>close for write successful\n");
	} else if (filp->f_mode & FMODE_READ) {
		pipe->reader = 0;
		if (pipe->writer)
			waiting = 1;
		global_pipe = pipe;
		printk("<1>close for reader successful\n");
	}

	m_unlock(&pipe->mutex);

	if(!pipe->writer && !pipe->reader) {
		kfree(pipe->disc_buffer);
		kfree(pipe);
	}

	return 0;
}

ssize_t disc_read(struct file *filp, char *buf, size_t count, loff_t *f_pos) {
	ssize_t rc;
	Pipe *pipe = (Pipe*) filp->private_data;

	m_lock(&pipe->mutex);
	while (pipe->size <= *f_pos) {
		if (pipe->writer_closed) {
			rc = 0;
			goto epilog;
		}
		if (c_wait(&pipe->cond, &pipe->mutex)) {
			printk("<1>read interrupted\n");
			rc = -EINTR;
			goto epilog;
		}
	}

	if (count > pipe->size - *f_pos)
		count = pipe->size - *f_pos;
	printk("<1>read %d bytes at %d\n", (int)count, (int)*f_pos);

	if (copy_to_user(buf, pipe->disc_buffer + *f_pos, count)) {
		rc = -EFAULT;
		goto epilog;
	}

	*f_pos += count;
	rc = count;

epilog:
	m_unlock(&pipe->mutex);
	return rc;
}

ssize_t disc_write( struct file *filp, const char *buf, size_t count, loff_t *f_pos) {
	ssize_t rc;
	loff_t last;
	Pipe *pipe = (Pipe*) filp->private_data;

	m_lock(&pipe->mutex);
	last = *f_pos + count;
	if (last > MAX_SIZE)
		count -= last - MAX_SIZE;
	printk("<1>write %d bytes at %d\n", (int)count, (int)*f_pos);

	if (copy_from_user(pipe->disc_buffer + *f_pos, buf, count)) {
		rc = -EFAULT;
		goto epilog;
	}

	*f_pos += count;
	pipe->size = *f_pos;
	rc = count;
	c_broadcast(&pipe->cond);

epilog:
	m_unlock(&pipe->mutex);
	return rc;
}
