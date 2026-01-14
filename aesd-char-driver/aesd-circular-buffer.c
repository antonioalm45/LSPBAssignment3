/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/mutex.h>
#else
#include <string.h>
#include <pthread.h>
#endif

#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
                                                                          size_t char_offset, size_t *entry_offset_byte_rtn)
{
    /**
     * TODO: implement per description
     */
    if (buffer != NULL && entry_offset_byte_rtn != NULL)
    {

#ifdef __KERNEL__
        mutex_lock(&buffer->lock);
#else
        pthread_mutex_lock(&buffer->lock);
#endif

        size_t cumulative_size = 0;
        uint8_t index = buffer->out_offs;
        uint8_t count = 0;
        uint8_t max_entries = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

        if (!buffer->full)
        {
            max_entries = buffer->in_offs;
        }

        while (count < max_entries)
        {
            struct aesd_buffer_entry *current_entry = &buffer->entry[index];
            if (char_offset < (cumulative_size + current_entry->size))
            {
                *entry_offset_byte_rtn = char_offset - cumulative_size;

#ifdef __KERNEL__
                mutex_unlock(&buffer->lock);
#else
                pthread_mutex_unlock(&buffer->lock);
#endif

                return current_entry;
            }
            cumulative_size += current_entry->size;
            index = (index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
            count++;
        }
    }

#ifdef __KERNEL__
    mutex_unlock(&buffer->lock);
#else
    pthread_mutex_unlock(&buffer->lock);
#endif

    return NULL;
}

/**
 * Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
 * If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
 * new start location.
 * Any necessary locking must be handled by the caller
 * Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
 */
void aesd_circular_buffer_add_entry(struct aesd_circular_buffer *buffer, const struct aesd_buffer_entry *add_entry)
{
    /**
     * TODO: implement per description
     */
    if (buffer != NULL && add_entry != NULL)
    {

#ifdef __KERNEL__
        mutex_lock(&buffer->lock);
#else
        pthread_mutex_lock(&buffer->lock);
#endif

        if (buffer->full == true)
        {
            // Avanzamos out_offs para descartar la entrada más antigua
            buffer->out_offs = (buffer->out_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        }
        // Copiamos la nueva entrada en la posición in_offs
        buffer->entry[buffer->in_offs] = *add_entry;

        // Avanzamos in_offs
        buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

        buffer->full = (buffer->in_offs == buffer->out_offs);

#ifdef __KERNEL__
        mutex_unlock(&buffer->lock);
#else
        pthread_mutex_unlock(&buffer->lock);
#endif
    }
}

/**
 * Initializes the circular buffer described by @param buffer to an empty struct
 */
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer, 0, sizeof(struct aesd_circular_buffer));

#ifdef __KERNEL__
    mutex_init(&buffer->lock);
#else
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    /* Opcional: mutex robusto para recuperación si un hilo muere con el lock */
    // pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    pthread_mutex_init(&buffer->lock, &attr);
    pthread_mutexattr_destroy(&attr);
#endif
}
