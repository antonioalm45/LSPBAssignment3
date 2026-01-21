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
#include "aesd_ioctl.h"
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
    struct aesd_dev *dev;
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;

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
    struct aesd_dev *dev;
    ssize_t retval = 0;
    char *kbuf = NULL;

    struct aesd_buffer_entry new_entry;
    size_t total_size;
    char *combined = NULL;

    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);

    if (!buf || count == 0)
        return -EINVAL;

    dev = filp->private_data;
    if (!dev)
        return -EINVAL;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    kbuf = kmalloc(count, GFP_KERNEL);
    if (!kbuf)
    {
        retval = -ENOMEM;
        goto out_unlock;
    }

    if (copy_from_user(kbuf, buf, count))
    {
        retval = -EFAULT;
        goto out_free_kbuf;
    }

    /* Si la escritura termina en '\n', debemos "cerrar" un comando:
     * - concatenar cualquier pending_buf + kbuf
     * - añadir como UNA entrada al buffer circular
     * - limpiar pending_buf
     */
    if (kbuf[count - 1] == '\n')
    {
        total_size = dev->pending_size + count;

        combined = kmalloc(total_size, GFP_KERNEL);
        if (!combined)
        {
            retval = -ENOMEM;
            goto out_free_kbuf;
        }

        /* Copia lo pendiente primero, luego lo nuevo */
        if (dev->pending_buf && dev->pending_size)
            memcpy(combined, dev->pending_buf, dev->pending_size);

        memcpy(combined + dev->pending_size, kbuf, count);

        /* Liberamos el buffer pendiente, ya consumido */
        kfree(dev->pending_buf);
        dev->pending_buf = NULL;
        dev->pending_size = 0;

        /* Entregamos 'combined' al buffer circular (NO lo liberamos después) */
        new_entry.buffptr = combined;
        new_entry.size = total_size;
        aesd_circular_buffer_add_entry(&dev->buffer, &new_entry);

        /* Importante: NO liberar 'combined', ahora es propiedad del buffer */
        retval = count;
    }
    else
    {
        /* Acumular escritura parcial (sin '\n') en pending_buf */
        size_t new_size = dev->pending_size + count;
        char *new_buf = krealloc(dev->pending_buf, new_size, GFP_KERNEL);
        if (!new_buf)
        {
            retval = -ENOMEM;
            goto out_free_kbuf;
        }
        memcpy(new_buf + dev->pending_size, kbuf, count);
        dev->pending_buf = new_buf;
        dev->pending_size = new_size;

        retval = count;
    }

out_free_kbuf:
    kfree(kbuf);
out_unlock:
    mutex_unlock(&dev->lock);
    return retval;
}

long aesd_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_seekto seekto;
    struct aesd_buffer_entry *entry;
    size_t entry_offset_byte = 0;
    size_t fpos = 0;
    uint32_t write_cmd_count = 0;

    if (mutex_lock_interruptible(&dev->lock))
    {
        return -ERESTARTSYS;
    }

    if (cmd == AESDCHAR_IOCSEEKTO)
    {
        if (copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto)))
        {
            mutex_unlock(&dev->lock);
            return -EFAULT;
        }

        // Encuentra la entrada correspondiente al write_cmd especificado
        entry = aesd_circular_buffer_find_entry_offset_for_fpos(
            &dev->buffer, 0, &entry_offset_byte);

        // Recorre las entradas hasta encontrar el comando write_cmd solicitado
        while (entry && write_cmd_count < seekto.write_cmd)
        {
            write_cmd_count++;
            // Busca la siguiente entrada desde donde terminó la actual
            size_t next_offset = entry_offset_byte + entry->size;
            entry = aesd_circular_buffer_find_entry_offset_for_fpos(
                &dev->buffer, next_offset, &entry_offset_byte);
        }

        // Verifica que la entrada existe y el offset está dentro del rango
        if (!entry || seekto.write_cmd_offset >= entry->size)
        {
            mutex_unlock(&dev->lock);
            return -EINVAL;
        }

        // Calcula la nueva posición del fichero
        fpos = entry_offset_byte + seekto.write_cmd_offset;
        filp->f_pos = fpos;

        mutex_unlock(&dev->lock);
        return 0;
    }
    else
    {
        mutex_unlock(&dev->lock);
        return -ENOTTY;
    }
}

loff_t aesd_llseek(struct file *filp, loff_t off, int whence)
{
    struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;
    loff_t newpos;

    switch (whence)
    {
    case 0: /* SEEK_SET */
        newpos = off;
        break;
    case 1: /* SEEK_CUR */
        newpos = filp->f_pos + off;
        break;
    case 2: /* SEEK_END */
        newpos = off;
        break;

    default:
        return -EINVAL;
    }

    if (newpos < 0)
        return -EINVAL;

    filp->f_pos = newpos;
    return newpos;
}

struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_release,
    .llseek = aesd_llseek,
    .unlocked_ioctl = aesd_unlocked_ioctl,
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

    /* Inicializa los buffers pendientes */
    aesd_device.pending_buf = NULL;
    aesd_device.pending_size = 0;

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
    uint8_t i;

    cdev_del(&aesd_device.cdev);

    /* Liberar cualquier pendencia */
    if (aesd_device.pending_buf)
    {
        kfree(aesd_device.pending_buf);
        aesd_device.pending_buf = NULL;
        aesd_device.pending_size = 0;
    }

    /* Liberar entradas del buffer circular si quedaron asignadas */
    for (i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++)
    {
        kfree(aesd_device.buffer.entry[i].buffptr);
        aesd_device.buffer.entry[i].buffptr = NULL;
        aesd_device.buffer.entry[i].size = 0;
    }

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
