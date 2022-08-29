#ifndef MUNIT_GOTO_H
#define MUNIT_GOTO_H

#include "munit.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define munit_assert_goto(expr, label) \
  do { \
    if (!MUNIT_LIKELY(expr)) { \
      munit_log(MUNIT_LOG_ERROR, "assertion failed: " #expr); \
      goto label; \
    } \
    MUNIT_PUSH_DISABLE_MSVC_C4127_ \
  } while (0) \
  MUNIT_POP_DISABLE_MSVC_C4127_

#define munit_assert_true_goto(expr, label) \
  do { \
    if (!MUNIT_LIKELY(expr)) { \
      munit_log(MUNIT_LOG_ERROR, "assertion failed: " #expr " is not true"); \
      goto label; \
    } \
    MUNIT_PUSH_DISABLE_MSVC_C4127_ \
  } while (0) \
  MUNIT_POP_DISABLE_MSVC_C4127_

#define munit_assert_false_goto(expr, label) \
  do { \
    if (!MUNIT_LIKELY(!(expr))) { \
      munit_log(MUNIT_LOG_ERROR, "assertion failed: " #expr " is not false"); \
      goto label; \
    } \
    MUNIT_PUSH_DISABLE_MSVC_C4127_ \
  } while (0) \
  MUNIT_POP_DISABLE_MSVC_C4127_

#define munit_assert_type_full_goto(prefix, suffix, T, fmt, a, op, b, label)   \
  do { \
    T munit_tmp_a_ = (a); \
    T munit_tmp_b_ = (b); \
    if (!(munit_tmp_a_ op munit_tmp_b_)) {                               \
      munit_logf(MUNIT_LOG_ERROR, \
            "assertion failed: %s %s %s (" prefix "%" fmt suffix " %s " prefix "%" fmt suffix ")", \
                   #a, #op, #b, munit_tmp_a_, #op, munit_tmp_b_); \
      goto label; \
    } \
    MUNIT_PUSH_DISABLE_MSVC_C4127_ \
  } while (0) \
  MUNIT_POP_DISABLE_MSVC_C4127_

#define munit_assert_type_goto(T, fmt, a, op, b, label) \
  munit_assert_type_full_goto("", "", T, fmt, a, op, b, label)

#define munit_assert_char_goto(a, op, b, label) \
  munit_assert_type_full_goto("'\\x", "'", char, "02" MUNIT_CHAR_MODIFIER "x", a, op, b, label)
#define munit_assert_uchar_goto(a, op, b, label) \
  munit_assert_type_full_goto("'\\x", "'", unsigned char, "02" MUNIT_CHAR_MODIFIER "x", a, op, b, label)
#define munit_assert_short_goto(a, op, b, label) \
  munit_assert_type_goto(short, MUNIT_SHORT_MODIFIER "d", a, op, b, label)
#define munit_assert_ushort_goto(a, op, b, label) \
  munit_assert_type_goto(unsigned short, MUNIT_SHORT_MODIFIER "u", a, op, b, label)
#define munit_assert_int_goto(a, op, b, label) \
  munit_assert_type_goto(int, "d", a, op, b, label)
#define munit_assert_uint_goto(a, op, b, label) \
  munit_assert_type_goto(unsigned int, "u", a, op, b, label)
#define munit_assert_long_goto(a, op, b, label) \
  munit_assert_type_goto(long int, "ld", a, op, b, label)
#define munit_assert_ulong_goto(a, op, b, label) \
  munit_assert_type_goto(unsigned long int, "lu", a, op, b, label)
#define munit_assert_llong_goto(a, op, b, label) \
  munit_assert_type_goto(long long int, "lld", a, op, b, label)
#define munit_assert_ullong_goto(a, op, b, label) \
  munit_assert_type_goto(unsigned long long int, "llu", a, op, b, label)

#define munit_assert_size_goto(a, op, b, label) \
  munit_assert_type_goto(size_t, MUNIT_SIZE_MODIFIER "u", a, op, b, label)

#define munit_assert_float_goto(a, op, b, label) \
  munit_assert_type_goto(float, "f", a, op, b, label)
#define munit_assert_double_goto(a, op, b, label) \
  munit_assert_type_goto(double, "g", a, op, b, label)
#define munit_assert_ptr_goto(a, op, b, label) \
  munit_assert_type_goto(const void*, "p", a, op, b, label)

#define munit_assert_int8_goto(a, op, b, label)             \
  munit_assert_type_goto(munit_int8_t, PRIi8, a, op, b, label)
#define munit_assert_uint8_goto(a, op, b, label) \
  munit_assert_type_goto(munit_uint8_t, PRIu8, a, op, b, label)
#define munit_assert_int16_goto(a, op, b, label) \
  munit_assert_type_goto(munit_int16_t, PRIi16, a, op, b, label)
#define munit_assert_uint16_goto(a, op, b, label) \
  munit_assert_type_goto(munit_uint16_t, PRIu16, a, op, b, label)
#define munit_assert_int32_goto(a, op, b, label) \
  munit_assert_type_goto(munit_int32_t, PRIi32, a, op, b, label)
#define munit_assert_uint32_goto(a, op, b, label) \
  munit_assert_type_goto(munit_uint32_t, PRIu32, a, op, b, label)
#define munit_assert_int64_goto(a, op, b, label) \
  munit_assert_type_goto(munit_int64_t, PRIi64, a, op, b, label)
#define munit_assert_uint64_goto(a, op, b, label) \
  munit_assert_type_goto(munit_uint64_t, PRIu64, a, op, b, label)

#define munit_assert_double_equal_goto(a, b, precision, label) \
  do { \
    const double munit_tmp_a_ = (a); \
    const double munit_tmp_b_ = (b); \
    const double munit_tmp_diff_ = ((munit_tmp_a_ - munit_tmp_b_) < 0) ? \
      -(munit_tmp_a_ - munit_tmp_b_) : \
      (munit_tmp_a_ - munit_tmp_b_); \
    if (MUNIT_UNLIKELY(munit_tmp_diff_ > 1e-##precision)) { \
      munit_logf(MUNIT_LOG_ERROR, \
        "assertion failed: %s == %s (%0." #precision "g == %0." #precision "g)", \
        #a, #b, munit_tmp_a_, munit_tmp_b_); \
      goto label; \
    } \
    MUNIT_PUSH_DISABLE_MSVC_C4127_ \
  } while (0) \
  MUNIT_POP_DISABLE_MSVC_C4127_

#include <string.h>
#define munit_assert_string_equa_gotol(a, b, label) \
  do { \
    const char* munit_tmp_a_ = a; \
    const char* munit_tmp_b_ = b; \
    if (MUNIT_UNLIKELY(strcmp(munit_tmp_a_, munit_tmp_b_) != 0)) { \
      munit_logf(MUNIT_LOG_ERROR, \
        "assertion failed: string %s == %s (\"%s\" == \"%s\")", \
        #a, #b, munit_tmp_a_, munit_tmp_b_); \
      goto label; \
    } \
    MUNIT_PUSH_DISABLE_MSVC_C4127_ \
  } while (0) \
  MUNIT_POP_DISABLE_MSVC_C4127_

#define munit_assert_string_not_equal_goto(a, b, label) \
  do { \
    const char* munit_tmp_a_ = a; \
    const char* munit_tmp_b_ = b; \
    if (MUNIT_UNLIKELY(strcmp(munit_tmp_a_, munit_tmp_b_) == 0)) { \
      munit_logf(MUNIT_LOG_ERROR, \
        "assertion failed: string %s != %s (\"%s\" == \"%s\")", \
        #a, #b, munit_tmp_a_, munit_tmp_b_); \
      goto label; \
    } \
    MUNIT_PUSH_DISABLE_MSVC_C4127_ \
  } while (0) \
  MUNIT_POP_DISABLE_MSVC_C4127_

#define munit_assert_memory_equal_goto(size, a, b, label) \
  do { \
    const unsigned char* munit_tmp_a_ = (const unsigned char*) (a); \
    const unsigned char* munit_tmp_b_ = (const unsigned char*) (b); \
    const size_t munit_tmp_size_ = (size); \
    if (MUNIT_UNLIKELY(memcmp(munit_tmp_a_, munit_tmp_b_, munit_tmp_size_)) != 0) { \
      size_t munit_tmp_pos_; \
      for (munit_tmp_pos_ = 0 ; munit_tmp_pos_ < munit_tmp_size_ ; munit_tmp_pos_++) { \
        if (munit_tmp_a_[munit_tmp_pos_] != munit_tmp_b_[munit_tmp_pos_]) { \
          munit_logf(MUNIT_LOG_ERROR, \
            "assertion failed: memory %s == %s, at offset %" MUNIT_SIZE_MODIFIER "u", \
            #a, #b, munit_tmp_pos_); \
          goto label; \
        } \
      } \
    } \
    MUNIT_PUSH_DISABLE_MSVC_C4127_ \
  } while (0) \
  MUNIT_POP_DISABLE_MSVC_C4127_

#define munit_assert_memory_not_equal_goto(size, a, b, label) \
  do { \
    const unsigned char* munit_tmp_a_ = (const unsigned char*) (a); \
    const unsigned char* munit_tmp_b_ = (const unsigned char*) (b); \
    const size_t munit_tmp_size_ = (size); \
    if (MUNIT_UNLIKELY(memcmp(munit_tmp_a_, munit_tmp_b_, munit_tmp_size_)) == 0) { \
      munit_logf(MUNIT_LOG_ERROR, \
        "assertion failed: memory %s != %s (%zu bytes)", \
        #a, #b, munit_tmp_size_); \
      goto label; \
    } \
    MUNIT_PUSH_DISABLE_MSVC_C4127_ \
  } while (0) \
  MUNIT_POP_DISABLE_MSVC_C4127_

#define munit_assert_ptr_equal_goto(a, b, label) \
  munit_assert_ptr_goto(a, ==, b, label)
#define munit_assert_ptr_not_equal_goto(a, b, label) \
  munit_assert_ptr_goto(a, !=, b, label)
#define munit_assert_null_goto(ptr, label) \
  munit_assert_ptr_goto(ptr, ==, NULL, label)
#define munit_assert_not_null_goto(ptr, label) \
  munit_assert_ptr_goto(ptr, !=, NULL, label)
#define munit_assert_ptr_null_goto(ptr, label) \
  munit_assert_ptr_goto(ptr, ==, NULL, label)
#define munit_assert_ptr_not_null_goto(ptr, label) \
  munit_assert_ptr_goto(ptr, !=, NULL, label)

#endif
