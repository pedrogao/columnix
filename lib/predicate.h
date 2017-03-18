#ifndef CX_PREDICATE_H_
#define CX_PREDICATE_H_

#include <stdarg.h>

#include "match.h"
#include "row_group.h"

struct cx_predicate;

void cx_predicate_free(struct cx_predicate *);

struct cx_predicate *cx_predicate_new_true(void);

struct cx_predicate *cx_predicate_new_null(size_t);

struct cx_predicate *cx_predicate_negate(struct cx_predicate *);

struct cx_predicate *cx_predicate_new_bit_eq(size_t, bool);

struct cx_predicate *cx_predicate_new_i32_eq(size_t, int32_t);
struct cx_predicate *cx_predicate_new_i32_lt(size_t, int32_t);
struct cx_predicate *cx_predicate_new_i32_gt(size_t, int32_t);

struct cx_predicate *cx_predicate_new_i64_eq(size_t, int64_t);
struct cx_predicate *cx_predicate_new_i64_lt(size_t, int64_t);
struct cx_predicate *cx_predicate_new_i64_gt(size_t, int64_t);

struct cx_predicate *cx_predicate_new_flt_eq(size_t, float);
struct cx_predicate *cx_predicate_new_flt_lt(size_t, float);
struct cx_predicate *cx_predicate_new_flt_gt(size_t, float);

struct cx_predicate *cx_predicate_new_dbl_eq(size_t, double);
struct cx_predicate *cx_predicate_new_dbl_lt(size_t, double);
struct cx_predicate *cx_predicate_new_dbl_gt(size_t, double);

struct cx_predicate *cx_predicate_new_str_eq(size_t, const char *, bool);
struct cx_predicate *cx_predicate_new_str_lt(size_t, const char *, bool);
struct cx_predicate *cx_predicate_new_str_gt(size_t, const char *, bool);
struct cx_predicate *cx_predicate_new_str_contains(size_t, const char *, bool,
                                                   enum cx_str_location);

struct cx_predicate *cx_predicate_new_and(size_t, ...);
struct cx_predicate *cx_predicate_new_vand(size_t, va_list);
struct cx_predicate *cx_predicate_new_aand(size_t, struct cx_predicate **);
struct cx_predicate *cx_predicate_new_or(size_t, ...);
struct cx_predicate *cx_predicate_new_vor(size_t, va_list);
struct cx_predicate *cx_predicate_new_aor(size_t, struct cx_predicate **);

bool cx_predicate_valid(const struct cx_predicate *,
                        const struct cx_row_group *);

void cx_predicate_optimize(struct cx_predicate *, const struct cx_row_group *);

const struct cx_predicate **cx_predicate_operands(const struct cx_predicate *,
                                                  size_t *);

enum cx_index_match cx_index_match_indexes(const struct cx_predicate *,
                                           const struct cx_row_group *);

bool cx_index_match_rows(const struct cx_predicate *predicate,
                         const struct cx_row_group *row_group,
                         struct cx_row_group_cursor *cursor, uint64_t *matches,
                         size_t *count);

typedef enum cx_index_match (*cx_index_match_index_t)(enum cx_column_type,
                                                      const struct cx_index *,
                                                      void *data);

typedef bool (*cx_index_match_rows_t)(enum cx_column_type, size_t count,
                                      const void *values, uint64_t *matches,
                                      void *data);

struct cx_predicate *cx_predicate_new_custom(size_t column, enum cx_column_type,
                                             cx_index_match_rows_t,
                                             cx_index_match_index_t, int cost,
                                             void *data);

#endif