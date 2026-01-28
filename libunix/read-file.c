#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "libunix.h"

// allocate buffer, read entire file into it, return it.   
// buffer is zero padded to a multiple of 4.
//
//  - <size> = exact nbytes of file.
//  - for allocation: round up allocated size to 4-byte multiple, pad
//    buffer with 0s. 
//
// fatal error: open/read of <name> fails.
//   - make sure to check all system calls for errors.
//   - make sure to close the file descriptor (this will
//     matter for later labs).
// 
void *read_file(unsigned *size, const char *name) {
    // How: 
    //    - use stat() to get the size of the file.
    //    - round up to a multiple of 4.
    //    - allocate a buffer
    //    - zero pads to a multiple of 4.
    //    - read entire file into buffer (read_exact())
    //    - fclose() the file descriptor
    //    - make sure any padding bytes have zeros.
    //    - return it.   
    //unimplemented();

    struct stat st;
    if (stat(name, &st) < 0) {
        panic("stat failed");
    }

    unsigned n = (unsigned)st.st_size;
    unsigned char *buf = calloc(n+4, 1);  // hack to zero fill up to next multiple of 4
    if (!buf) {
        panic("calloc failed");
    }

    int fd = open(name, O_RDONLY);
    if (fd < 0) {
        panic("open failed");
    }

    if (n)                      // only read when file is not zero length
        read_exact(fd, buf, n);

    if (close(fd) < 0) {
        panic("close failed");
    }

    *size = n;
    return buf;
}
