#ifndef KERNEL_API

#include <vfs.h>

struct kernel_api {
    void (*kprint)(const char *str, int color);
    int (*vfs_pseudo_register)(const char* filename, vfs_dev_read_t read_fn, vfs_dev_write_t write_fn, vfs_dev_seek_t seek_fn, vfs_dev_ioctl_t ioctl_fn, void* dev_data);
    void* (*kmalloc)(size_t size);
    void (*kfree)(void* ptr);
    int (*keyboard_register_hotkey)(int scancode, int modifiers, void (*callback)(void*), void* data);
    void (*keyboard_unregister_hotkey)(int id);
    void* (*get_framebuffer)(void);
    void (*get_fb_dimensions)(uint32_t* width, uint32_t* height, uint32_t* pitch);
    uint32_t (*get_fb_pitch_pixels)(void);
};
#endif