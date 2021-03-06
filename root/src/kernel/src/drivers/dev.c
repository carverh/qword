#include <stdint.h>
#include <stddef.h>
#include <dev.h>
#include <klib.h>

#define MAX_DEVICES 128

struct device {
    int used;
    char name[128];
    int magic;
    uint64_t size;
    int (*read)(int, void *, uint64_t, size_t);
    int (*write)(int, const void *, uint64_t, size_t);
    int (*flush)(int);
};

static struct device devices[MAX_DEVICES];

uint64_t device_size(int dev) {
    return devices[dev].size;
}

int device_read(int dev, void *buf, uint64_t loc, size_t count) {
    int magic = devices[dev].magic;

    return devices[dev].read(magic, buf, loc, count);
}

int device_write(int dev, const void *buf, uint64_t loc, size_t count) {
    int magic = devices[dev].magic;

    return devices[dev].write(magic, buf, loc, count);
}

int device_flush(int dev) {
    int magic = devices[dev].magic;

    return devices[dev].flush(magic);
}

/* Returns a device ID by name, (dev_t)(-1) if not found. */
dev_t device_find(const char *name) {
    dev_t dev;

    for (dev = 0; dev < MAX_DEVICES; dev++) {
        if (!devices[dev].used)
            continue;
        if (!kstrcmp(devices[dev].name, name))
            return dev;
    }

    return (dev_t)(-1);
}

/* Registers a device. Returns the ID of the registered device. */
/* (dev_t)(-1) on error. */
dev_t device_add(const char *name, int magic, uint64_t size,
        int (*read)(int, void *, uint64_t, size_t),
        int (*write)(int, const void *, uint64_t, size_t),
        int (*flush)(int)) {

    dev_t dev;

    for (dev = 0; dev < MAX_DEVICES; dev++) {
        if (!devices[dev].used) {
            devices[dev].used = 1;
            kstrcpy(devices[dev].name, name);
            devices[dev].magic = magic;
            devices[dev].size = size;
            devices[dev].read = read;
            devices[dev].write = write;
            devices[dev].flush = flush;
            return dev;
        }
    }

    return (dev_t)(-1);
}
