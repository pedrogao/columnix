#define _BSD_SOURCE

#include "column.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

// when SSE4.2 optimizations are enabled, we make sure there are
// at least 16 initialized bytes after each column value
#if CX_SSE42
#include <smmintrin.h>
#define CX_COLUMN_OVER_ALLOC 16
#endif

static const size_t cx_column_initial_size = 64;

// 列定义
struct cx_column {
    union {
        void *mutable;
        const void *mmapped;
    } buffer;
    size_t count;
    size_t offset;
    size_t size;
    enum cx_column_type type;
    enum cx_encoding_type encoding;
    bool mmapped;  // 是否 memory mmap
};

// 列浮标
struct cx_column_cursor {
    const struct cx_column *column;
    const void *start;                 // 开始位置
    const void *end;                   // 结束位置
    const void *position;              // 当前位置
    cx_value_t buffer[CX_BATCH_SIZE];  // 缓存
};

static struct cx_column *cx_column_new_size(enum cx_column_type type,
                                            enum cx_encoding_type encoding,
                                            size_t size, size_t count)
{
    struct cx_column *column =
        calloc(1, sizeof(*column));  // 这样骚写法，容易误解
    if (!column)
        return NULL;
    if (size) {
#ifdef CX_COLUMN_OVER_ALLOC
        size += CX_COLUMN_OVER_ALLOC;
#endif
        column->buffer.mutable = malloc(size);
        if (!column->buffer.mutable)
            goto error;
#ifdef CX_COLUMN_OVER_ALLOC
        memset(column->buffer.mutable, 0, size);
#endif
        column->size = size;
    }
    column->type = type;
    column->encoding = encoding;
    column->count = count;
    return column;
error:
    free(column);
    return NULL;
}

struct cx_column *cx_column_new(enum cx_column_type type,
                                enum cx_encoding_type encoding)
{
    return cx_column_new_size(type, encoding, cx_column_initial_size, 0);
}

struct cx_column *cx_column_new_mmapped(enum cx_column_type type,
                                        enum cx_encoding_type encoding,
                                        const void *ptr, size_t size,
                                        size_t count)
{
    struct cx_column *column = cx_column_new_size(type, encoding, 0, count);
    if (!column)
        return NULL;
    column->offset = size;
    column->size = size;
    column->mmapped = true;
    column->buffer.mmapped = ptr;
    return column;
}

struct cx_column *cx_column_new_compressed(enum cx_column_type type,
                                           enum cx_encoding_type encoding,
                                           void **ptr, size_t size,
                                           size_t count)
{
    if (!size)
        return NULL;
    struct cx_column *column = cx_column_new_size(type, encoding, size, count);
    if (!column)
        return NULL;
    column->offset = size;
    *ptr = column->buffer.mutable;
    return column;
}

void cx_column_free(struct cx_column *column)
{
    if (!column->mmapped)
        free(column->buffer.mutable);
    free(column);
}

static const void *cx_column_head(const struct cx_column *column)
{
    if (column->mmapped)
        return column->buffer.mmapped;
    else
        return column->buffer.mutable;
}

static const void *cx_column_offset(const struct cx_column *column,
                                    size_t offset)
{
    const void *ptr = cx_column_head(column);
    return (const void *)((uintptr_t)ptr + offset);
}

static const void *cx_column_tail(const struct cx_column *column)
{
    return cx_column_offset(column, column->offset);
}

const void *cx_column_export(const struct cx_column *column, size_t *size)
{
    *size = column->offset;
    return cx_column_head(column);
}

enum cx_column_type cx_column_type(const struct cx_column *column)
{
    return column->type;
}

enum cx_encoding_type cx_column_encoding(const struct cx_column *column)
{
    return column->encoding;
}

size_t cx_column_count(const struct cx_column *column)
{
    return column->count;
}

__attribute__((noinline)) static bool cx_column_resize(struct cx_column *column,
                                                       size_t alloc_size)
{
    size_t size = column->size;
    size_t required_size = column->offset + alloc_size;
#ifdef CX_COLUMN_OVER_ALLOC
    required_size += CX_COLUMN_OVER_ALLOC;
#endif
    while (size < required_size) {
        assert(size * 2 > size);
        size *= 2;
    }
    void *buffer = realloc(column->buffer.mutable, size);
    if (!buffer)
        return false;
#ifdef CX_COLUMN_OVER_ALLOC
    memset((void *)((uintptr_t)buffer + column->offset), 0,
           size - column->offset);
#endif
    column->buffer.mutable = buffer;
    column->size = size;
    return true;
}

static bool cx_column_put(struct cx_column *column, enum cx_column_type type,
                          const void *value, size_t size)
{
    if (column->mmapped || column->type != type || !value)
        return false;
    if (column->offset + size > column->size)
        if (!cx_column_resize(column, size))
            return false;
    void *slot = (void *)cx_column_tail(column);
    memcpy(slot, value, size);
    column->count++;
    column->offset += size;
    return true;
}

bool cx_column_put_bit(struct cx_column *column, bool value)
{
    if (column->count % 64 == 0) {
        uint64_t bitset = value != 0;
        return cx_column_put(column, CX_COLUMN_BIT, &bitset, sizeof(uint64_t));
    } else if (column->type != CX_COLUMN_BIT || column->mmapped)
        return false;
    if (value) {
        uint64_t *bitset = (uint64_t *)cx_column_offset(
            column, column->offset - sizeof(uint64_t));
        *bitset |= (uint64_t)1 << column->count;
    }
    column->count++;
    return true;
}

bool cx_column_put_i32(struct cx_column *column, int32_t value)
{
    return cx_column_put(column, CX_COLUMN_I32, &value, sizeof(int32_t));
}

bool cx_column_put_i64(struct cx_column *column, int64_t value)
{
    return cx_column_put(column, CX_COLUMN_I64, &value, sizeof(int64_t));
}

bool cx_column_put_flt(struct cx_column *column, float value)
{
    return cx_column_put(column, CX_COLUMN_FLT, &value, sizeof(float));
}

bool cx_column_put_dbl(struct cx_column *column, double value)
{
    return cx_column_put(column, CX_COLUMN_DBL, &value, sizeof(double));
}

bool cx_column_put_str(struct cx_column *column, const char *value)
{
    return cx_column_put(column, CX_COLUMN_STR, value, strlen(value) + 1);
}

bool cx_column_put_unit(struct cx_column *column)
{
    switch (column->type) {
        case CX_COLUMN_BIT:
            return cx_column_put_bit(column, false);
        case CX_COLUMN_I32:
            return cx_column_put_i32(column, 0);
        case CX_COLUMN_I64:
            return cx_column_put_i64(column, 0);
        case CX_COLUMN_FLT:
            return cx_column_put_flt(column, 0.0);
        case CX_COLUMN_DBL:
            return cx_column_put_dbl(column, 0.0);
        case CX_COLUMN_STR:
            return cx_column_put_str(column, "");
    }
    return false;
}

static bool cx_column_madvise(const struct cx_column *column, int advice)
{
    if (!column->mmapped || !column->size)
        return true;
    size_t page_size = getpagesize();
    uintptr_t addr = (uintptr_t)column->buffer.mmapped;
    size_t offset = addr % page_size;
    return !madvise((void *)(addr - offset), (column->size + offset), advice);
}

struct cx_column_cursor *cx_column_cursor_new(const struct cx_column *column)
{
    struct cx_column_cursor *cursor = malloc(sizeof(*cursor));
    if (!cursor)
        return NULL;
    cursor->column = column;
    cursor->start = cx_column_head(column);
    cursor->end = cx_column_tail(column);
    if (!cx_column_madvise(column, MADV_SEQUENTIAL))
        goto error;
    cx_column_cursor_rewind(cursor);
    return cursor;
error:
    free(cursor);
    return NULL;
}

void cx_column_cursor_free(struct cx_column_cursor *cursor)
{
    free(cursor);
}

void cx_column_cursor_rewind(struct cx_column_cursor *cursor)
{
    cursor->position = cursor->start;
}

bool cx_column_cursor_valid(const struct cx_column_cursor *cursor)
{
    return cursor->position < cursor->end;
}

static void cx_column_cursor_advance(struct cx_column_cursor *cursor,
                                     size_t size)
{
    cursor->position = (void *)((uintptr_t)cursor->position + size);
    assert(cursor->position <= cursor->end);
}

static size_t cx_column_cursor_skip(struct cx_column_cursor *cursor,
                                    enum cx_column_type type, size_t size,
                                    size_t count)
{
    assert(cursor->column->type == type);
    size_t remaining =
        ((uintptr_t)cursor->end - (uintptr_t)cursor->position) / size;
    if (remaining < count)
        count = remaining;
    cx_column_cursor_advance(cursor, size * count);
    return count;
}

size_t cx_column_cursor_skip_bit(struct cx_column_cursor *cursor, size_t count)
{
    assert(count % 64 == 0);
    size_t skipped = cx_column_cursor_skip(cursor, CX_COLUMN_BIT,
                                           sizeof(uint64_t), count / 64);
    skipped *= 64;
    if (!cx_column_cursor_valid(cursor)) {
        size_t trailing_bits = cursor->column->count % 64;
        if (trailing_bits)
            skipped -= 64 - trailing_bits;
    }
    return skipped;
}

size_t cx_column_cursor_skip_i32(struct cx_column_cursor *cursor, size_t count)
{
    return cx_column_cursor_skip(cursor, CX_COLUMN_I32, sizeof(int32_t), count);
}

size_t cx_column_cursor_skip_i64(struct cx_column_cursor *cursor, size_t count)
{
    return cx_column_cursor_skip(cursor, CX_COLUMN_I64, sizeof(int64_t), count);
}

size_t cx_column_cursor_skip_flt(struct cx_column_cursor *cursor, size_t count)
{
    return cx_column_cursor_skip(cursor, CX_COLUMN_FLT, sizeof(float), count);
}

size_t cx_column_cursor_skip_dbl(struct cx_column_cursor *cursor, size_t count)
{
    return cx_column_cursor_skip(cursor, CX_COLUMN_DBL, sizeof(double), count);
}

static inline size_t cx_strlen(const char *string)
{
#if CX_SSE42
    __m128i v_null = _mm_set1_epi8(0);
    size_t length = 0;
    for (;;) {
        __m128i v_str = _mm_loadu_si128((__m128i *)(string + length));
        int result = _mm_cmpistri(
            v_str, v_null,
            _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_EACH | _SIDD_BIT_MASK);
        length += result;
        if (result < 16)
            break;
    }
    return length;
#else
    return strlen(string);
#endif
}

size_t cx_column_cursor_skip_str(struct cx_column_cursor *cursor, size_t count)
{
    assert(cursor->column->type == CX_COLUMN_STR);
    size_t skipped = 0;
    // TODO: vectorise this
    for (; skipped < count && cx_column_cursor_valid(cursor); skipped++)
        cx_column_cursor_advance(cursor, cx_strlen(cursor->position) + 1);
    return skipped;
}

const uint64_t *cx_column_cursor_next_batch_bit(struct cx_column_cursor *cursor,
                                                size_t *available)
{
    const uint64_t *values = cursor->position;
    *available = cx_column_cursor_skip_bit(cursor, CX_BATCH_SIZE);
    return values;
}

const int32_t *cx_column_cursor_next_batch_i32(struct cx_column_cursor *cursor,
                                               size_t *available)
{
    const int32_t *values = cursor->position;
    *available = cx_column_cursor_skip_i32(cursor, CX_BATCH_SIZE);
    return values;
}

const int64_t *cx_column_cursor_next_batch_i64(struct cx_column_cursor *cursor,
                                               size_t *available)
{
    const int64_t *values = cursor->position;
    *available = cx_column_cursor_skip_i64(cursor, CX_BATCH_SIZE);
    return values;
}

const float *cx_column_cursor_next_batch_flt(struct cx_column_cursor *cursor,
                                             size_t *available)
{
    const float *values = cursor->position;
    *available = cx_column_cursor_skip_flt(cursor, CX_BATCH_SIZE);
    return values;
}

const double *cx_column_cursor_next_batch_dbl(struct cx_column_cursor *cursor,
                                              size_t *available)
{
    const double *values = cursor->position;
    *available = cx_column_cursor_skip_dbl(cursor, CX_BATCH_SIZE);
    return values;
}

const struct cx_string *cx_column_cursor_next_batch_str(
    struct cx_column_cursor *cursor, size_t *available)
{
    assert(cursor->column->type == CX_COLUMN_STR);
    size_t i = 0;
    struct cx_string *strings = (struct cx_string *)cursor->buffer;
    for (; i < CX_BATCH_SIZE && cx_column_cursor_valid(cursor); i++) {
        strings[i].ptr = cursor->position;
        strings[i].len = cx_strlen(cursor->position);
        cx_column_cursor_advance(cursor, strings[i].len + 1);
    }
    *available = i;
    return strings;
}
