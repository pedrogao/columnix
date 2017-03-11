#ifndef CX_FILE_
#define CX_FILE_

#include "index.h"

#define CX_FILE_MAGIC 0x65726f7473637a1dLLU

#define CX_WRITE_ALIGN 8

struct cx_header {
    uint64_t magic;
};

struct cx_footer {
    uint64_t strings_offset;
    uint64_t strings_size;
    uint32_t row_group_count;
    uint32_t column_count;
    uint64_t row_count;
    uint64_t magic;
};

struct cx_column_descriptor {
    uint32_t name;
    uint32_t type;
    uint32_t encoding;
    uint32_t compression;
    int32_t level;
    uint32_t __padding;
};

struct cx_row_group_header {
    uint64_t size;
    uint64_t offset;
};

struct cx_column_header {
    uint64_t offset;
    uint64_t size;
    uint64_t decompressed_size;
    uint32_t compression;
    uint32_t __padding;
    struct cx_column_index index;
};

#endif
