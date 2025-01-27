"""
Python bindings for columnix.

Example write (Python):

    from columnix import Writer, Column, I64, I32, STR, LZ4, ZSTD

    columns = [Column(I64, "timestamp", compression=LZ4),
               Column(STR, "email", compression=ZSTD),
               Column(I32, "id")]

    rows = [(1400000000000, "foo@bar.com", 23),
            (1400000001000, "foo@bar.com", 45),
            (1400000002000, "baz@bar.com", 67)]

    with Writer("example.cx", columns, row_group_size=2) as writer:
        for row in rows:
            writer.put(row)

Example read (C):

    #define __STDC_FORMAT_MACROS
    #include <assert.h>
    #include <inttypes.h>
    #include <stdio.h>
    #include <columnix/reader.h>

    int main()
    {
        struct cx_reader *reader = cx_reader_new("example.cx");
        assert(reader);
        int64_t timestamp;
        const struct cx_string *email;
        int32_t event;
        while (cx_reader_next(reader)) {
            assert(cx_reader_get_i64(reader, 0, &timestamp) &&
                   cx_reader_get_str(reader, 1, &email) &&
                   cx_reader_get_i32(reader, 2, &event));
            printf("{%" PRIi64 ", %s, %d}\n", timestamp, email->ptr, event);
        }
        assert(!cx_reader_error(reader));
        cx_reader_free(reader);
    }
"""

from ctypes import cdll, util
from ctypes import (c_char_p, c_size_t, c_void_p, c_int, c_int32, c_int64,
                    c_bool, c_float, c_double)
import ctypes

# 加载 column library
if util.find_library("columnix"):
    lib = cdll.LoadLibrary(util.find_library("columnix"))
else:
    lib = cdll.LoadLibrary('./lib/libcolumnix.1.0.0.dylib')

# Writer 构造函数
cx_writer_new = lib.cx_writer_new
cx_writer_new.argtypes = [c_char_p, c_size_t]
cx_writer_new.restype = c_void_p

# Writer 析构函数
cx_writer_free = lib.cx_writer_free
cx_writer_free.argtypes = [c_void_p]

# Writer 新增一列
cx_writer_add_column = lib.cx_writer_add_column
cx_writer_add_column.argtypes = [
    c_void_p, c_char_p, c_int, c_int, c_int, c_int]

# Writer 添加 null 数据
cx_writer_put_null = lib.cx_writer_put_null
cx_writer_put_null.argtypes = [c_void_p, c_size_t]

# Writer 添加 bit 数据
cx_writer_put_bit = lib.cx_writer_put_bit
cx_writer_put_bit.argtypes = [c_void_p, c_size_t, c_bool]

# Writer 添加 i32 数据
cx_writer_put_i32 = lib.cx_writer_put_i32
cx_writer_put_i32.argtypes = [c_void_p, c_size_t, c_int32]

# Writer 添加 i64 数据
cx_writer_put_i64 = lib.cx_writer_put_i64
cx_writer_put_i64.argtypes = [c_void_p, c_size_t, c_int64]

# Writer 添加 float 数据
cx_writer_put_flt = lib.cx_writer_put_flt
cx_writer_put_flt.argtypes = [c_void_p, c_size_t, c_float]

# Writer 添加 double 数据
cx_writer_put_dbl = lib.cx_writer_put_dbl
cx_writer_put_dbl.argtypes = [c_void_p, c_size_t, c_double]

# Writer 添加 string 数据
cx_writer_put_str = lib.cx_writer_put_str
cx_writer_put_str.argtypes = [c_void_p, c_size_t, c_char_p]

# Writer 完成，是否刷磁盘
cx_writer_finish = lib.cx_writer_finish
cx_writer_finish.argtypes = [c_void_p, c_bool]

# 数据类型
BIT = 0  # 位
I32 = 1  # interger 32
I64 = 2  # interger 64
FLT = 3  # float
DBL = 4  # double
STR = 5  # string
# 压缩算法
LZ4 = 1
LZ4HC = 2
ZSTD = 3


class Column(object):
    """ 列定义 """

    def __init__(self, type, name, encoding=None, compression=None, level=1):
        self.type = type
        self.name = name
        self.encoding = encoding or 0  # 0 表示无编码
        self.compression = compression or 0  # 0 表示无压缩
        self.level = level


class Writer(object):
    def __init__(self, path, columns, row_group_size=100000, sync=True):
        self.path = path
        self.columns = columns
        self.row_group_size = row_group_size
        self.sync = sync
        put_fn = [self._put_bit, self._put_i32, self._put_i64, self._put_flt,
                  self._put_dbl, self._put_str]
        self.put_lookup = [put_fn[column.type] for column in columns]
        self.writer = None

    def __enter__(self):
        assert self.writer is None
        self.writer = cx_writer_new(
            self.path.encode('utf-8'), ctypes.c_size_t(self.row_group_size))
        if not self.writer:
            raise RuntimeError("failed to create writer for %s" % self.path)
        for column in self.columns:
            if not cx_writer_add_column(self.writer, column.name.encode('utf-8'),
                                        ctypes.c_int(column.type),
                                        ctypes.c_int(column.encoding),
                                        ctypes.c_int(column.compression),
                                        ctypes.c_int(column.level)):
                raise RuntimeError("failed to add column")
        return self

    def __exit__(self, err, value, traceback):
        assert self.writer is not None
        if not err:
            cx_writer_finish(self.writer, self.sync)
        cx_writer_free(self.writer)
        self.writer = None

    def put(self, row):
        assert self.writer is not None
        put_lookup = self.put_lookup
        put_null = self._put_null
        for column, value in enumerate(row):
            if value is None:
                put_null(column)
            else:
                put_lookup[column](column, value)

    def _put_null(self, column):
        if not cx_writer_put_null(self.writer, column):
            raise RuntimeError("put_null(%d)" % column)

    def _put_bit(self, column, value):
        if not cx_writer_put_bit(self.writer, column, value):
            raise RuntimeError("put_bit(%d, %r)" % (column, value))

    def _put_i32(self, column, value):
        if not cx_writer_put_i32(self.writer, column, value):
            raise RuntimeError("put_i32(%d, %r)" % (column, value))

    def _put_i64(self, column, value):
        if not cx_writer_put_i64(self.writer, column, value):
            raise RuntimeError("put_i64(%d, %r)" % (column, value))

    def _put_flt(self, column, value):
        if not cx_writer_put_flt(self.writer, column, value):
            raise RuntimeError("put_flt(%d, %r)" % (column, value))

    def _put_dbl(self, column, value):
        if not cx_writer_put_dbl(self.writer, column, value):
            raise RuntimeError("put_dbl(%d, %r)" % (column, value))

    def _put_str(self, column, value):
        if not cx_writer_put_str(self.writer, c_size_t(column), value.encode('utf-8')):
            raise RuntimeError("put_str(%d, %r)" % (column, value))

    # from columnix import Writer, Column, I64, I32, STR, LZ4, ZSTD


if __name__ == '__main__':
    # 列定义
    columns = [Column(I64, "timestamp", compression=LZ4),
               Column(STR, "email", compression=ZSTD),
               Column(I32, "id")]
    # 行数据
    rows = [(1400000000000, "foo@bar.com", 23),
            (1400000001000, "foo@bar.com", 45),
            (1400000002000, "baz@bar.com", 67)]
    # 虽然以列的方式来存储，但数据表示仍是行格式，更加符合业务需求
    with Writer("example.cx", columns, row_group_size=2) as writer:
        for row in rows:
            writer.put(row)
