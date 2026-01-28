// engler, cs140e: your code to find the tty-usb device on your laptop.
#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>

#include "libunix.h"

#define _SVID_SOURCE
#include <dirent.h>
static const char *ttyusb_prefixes[] = {
    "ttyUSB",	// linux
    "ttyACM",   // linux
    "cu.SLAB_USB", // mac os
    "cu.usbserial", // mac os
    // if your system uses another name, add it.
	0
};

static int filter(const struct dirent *d) {
    // scan through the prefixes, returning 1 when you find a match.
    // 0 if there is no match.
    //unimplemented();

    for (const char **p = ttyusb_prefixes; *p; p++) {
        size_t n = strlen(*p);
        if (strncmp(d->d_name, *p, n) == 0) {
            return 1;
        }
    }
    return 0;
}

// find the TTY-usb device (if any) by using <scandir> to search for
// a device with a prefix given by <ttyusb_prefixes> in /dev
// returns:
//  - device name.
// error: panic's if 0 or more than 1 devices.
char *find_ttyusb(void) {
    // use <alphasort> in <scandir>
    // return a malloc'd name so doesn't corrupt.
    // unimplemented();
    struct dirent **nl = 0;
    int n = scandir("/dev", &nl, filter, alphasort);
    if (n < 0) panic("scandir failed\n");
    if (n == 0) panic("no ttyusb\n");
    if (n != 1) panic("multiple ttyusb\n");

    char *ret = strdupf("/dev/%s", nl[0]->d_name);

    free(nl[0]);
    free(nl);
    return ret;
}

// return the most recently mounted ttyusb (the one
// mounted last).  use the modification time 
// returned by state.
char *find_ttyusb_last(void) {
    // unimplemented();
    struct dirent **nl = 0;
    int n = scandir("/dev", &nl, filter, alphasort);
    if (n < 0) panic("scandir failed\n");
    if (n == 0) panic("no ttyusb\n");

    int best = 0;
    struct stat best_st;

    char *p = strdupf("/dev/%s", nl[0]->d_name);
    if (stat(p, &best_st) < 0) panic("stat failed\n");
    free(p);

    for (int i = 1; i < n; i++) {
        struct stat st;
        p = strdupf("/dev/%s", nl[i]->d_name);
        if (stat(p, &st) < 0) panic("stat failed\n");
        free(p);

        if (st.st_mtime > best_st.st_mtime) {
            best_st = st;
            best = i;
        }
    }

    char *ret = strdupf("/dev/%s", nl[best]->d_name);

    for (int i = 0; i < n; i++) free(nl[i]);
    free(nl);
    return ret;
}

// return the oldest mounted ttyusb (the one mounted
// "first") --- use the modification returned by
// stat()
char *find_ttyusb_first(void) {
    // unimplemented();
    struct dirent **nl = 0;
    int n = scandir("/dev", &nl, filter, alphasort);
    if (n < 0) panic("scandir failed\n");
    if (n == 0) panic("no ttyusb\n");

    int best = 0;
    struct stat best_st;

    char *p = strdupf("/dev/%s", nl[0]->d_name);
    if (stat(p, &best_st) < 0) panic("stat failed\n");
    free(p);

    for (int i = 1; i < n; i++) {
        struct stat st;
        p = strdupf("/dev/%s", nl[i]->d_name);
        if (stat(p, &st) < 0) panic("stat failed\n");
        free(p);

        if (st.st_mtime < best_st.st_mtime) {
            best_st = st;
            best = i;
        }
    }

    char *ret = strdupf("/dev/%s", nl[best]->d_name);

    for (int i = 0; i < n; i++) free(nl[i]);
    free(nl);
    return ret;
}
