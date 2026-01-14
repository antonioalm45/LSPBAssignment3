/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/device.h>

#include "aesdchar.h"
int aesd_major = 0; // use dynamic major
int aesd_minor = 0;

MODULE_AUTHOR("Antonio Almenara López");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */

    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                  loff_t *f_pos)
{
    struct aesd_dev *dev;
    const struct aesd_buffer_entry *entry;
    size_t entry_offset_byte = 0;
    ssize_t retval = 0;
    size_t to_copy;

    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);
    /**
     * TODO: handle read
     */

    if (!buf)
    {
        PDEBUG("buf invalid");
        return -EINVAL;
    }

    dev = filp->private_data;
    if (!dev)
    {
        PDEBUG("dev invalid");
        return -EINVAL;
    }

    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);

    if (mutex_lock_interruptible(&dev->lock))
    {
        PDEBUG("mutex not acquired");
        return -ERESTARTSYS;
    }

    // Traduce *f_pos a (entrada, offset dentro de la entrada)
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(
        &dev->buffer, (size_t)*f_pos, &entry_offset_byte);

    if (!entry)
    {
        PDEBUG("entry not found");
        // No hay datos a partir de *f_pos => EOF
        retval = 0;
        goto out_unlock;
    }

    // Disponible en esta entrada a partir de entry_offset
    to_copy = entry->size - entry_offset_byte;

    // Respeta 'count'
    if (to_copy > count)
        to_copy = count;

    if (copy_to_user(buf, entry->buffptr + entry_offset_byte, to_copy))
    {
        PDEBUG("copy to user failed");
        retval = -EFAULT;
        goto out_unlock;
    }

    // Avanza el puntero de fichero y devuelve bytes copiados
    *f_pos += to_copy;
    retval = (ssize_t)to_copy;

out_unlock:
    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                   loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    char *kbuf = NULL;
    bool ends_with_newline = false;
    size_t new_size = 0;
    char *new_buf = NULL;
    struct aesd_dev *dev;
    struct aesd_buffer_entry new_entry;
    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);
    /**
     * TODO: handle write
     */

    if (!buf || count == 0)
    {
        PDEBUG("buf invalid or count is zero");
        return -EINVAL;
    }

    dev = filp->private_data;
    if (!dev)
    {
        PDEBUG("dev invalid");
        return -EINVAL;
    }

    if (mutex_lock_interruptible(&dev->lock))
    {
        PDEBUG("mutex not acquired");
        return -ERESTARTSYS;
    }
    kbuf = kmalloc(count, GFP_KERNEL);
    if (!kbuf)
    {
        PDEBUG("kmalloc failed");
        retval = -ENOMEM;
        goto out_unlock;
    }

    if (copy_from_user(kbuf, buf, count))
    {
        PDEBUG("copy from user failed");
        retval = -EFAULT;
        goto out_free;
    }

    // Check if command is terminated with \n
    if (kbuf[count - 1] == '\n')
    {
        PDEBUG("write ends with newline");
        new_entry.buffptr = kbuf;
        new_entry.size = count;
        // Introducir en bufer si termina en \n, sino almacenar
        aesd_circular_buffer_add_entry(&dev->buffer, &new_entry);
        ends_with_newline = true;
    }
    else
    {
        // Append to pending buffer
        new_size = dev->pending_size + count;
        new_buf = krealloc(dev->pending_buf, new_size, GFP_KERNEL);
        if (!new_buf)
        {
            retval = -ENOMEM;
            goto out_free;
        }
        memcpy(new_buf + dev->pending_size, kbuf, count);
        dev->pending_buf = new_buf;
        dev->pending_size = new_size;
    }

    retval = count;

out_free:
    kfree(kbuf);
out_unlock:
    mutex_unlock(&dev->lock);
    return retval;
}

struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err)
    {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
                                 "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0)
    {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device, 0, sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    aesd_circular_buffer_init(&aesd_device.buffer);

    result = aesd_setup_cdev(&aesd_device);

    if (result)
    {
        unregister_chrdev_region(dev, 1);
    }
    return result;
}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
