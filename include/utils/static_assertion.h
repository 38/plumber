/**
 * Copyright (C) 2017, Hao Hou
 **/

/**
 * @file static_assertion.h
 * @brief the constant assertion utils, if assertion fails, a compilation error rises
 **/
#ifndef __STATIC_ASSERTION_H__
#define __STATIC_ASSERTION_H__
#include <stddef.h>
/** @brief this macro is used to check if the constant is less than a given value,
 *         if the assertion fails, this macro will rise a compilation error
 */
#define STATIC_ASSERTION_LE(id,val) struct __const_checker_le_##id { int test[((val) >= (id)) - 1];};
/** @brief this macro is used to check if the constant is greater than a given value,
 *         if the assertion fails, this macro will rise a compilation error
 */
#define STATIC_ASSERTION_GE(id,val) struct __const_checker_ge_##id { int test[((id) >= (val)) - 1];};
/** @brief this macro is used to check if the constant is less than a given value,
 *         if the assertion fails, this macro will rise a compilation error
 */
#define STATIC_ASSERTION_LT(id,val) struct __const_checker_lt_##id { int test[((val) > (id)) - 1];};
/** @brief this macro is used to check if the constant is greater than a given value,
 *         if the assertion fails, this macro will rise a compilation error
 */
#define STATIC_ASSERTION_GT(id,val) struct __const_checker_gt_##id { int test[((id) > (val)) - 1];};
/** @brief this macro is used to check if the constant is greater than a given value,
 *         if the assertion fails, this macro will rise a compilation error
 */
#define STATIC_ASSERTION_EQ(id,val) struct __const_checker_eq_##id { int test[(((id) == (val)) - 1) * (1 - ((val) == (id)))];};

/**
 * @brief assert that the compile time constant is equal to another one.
 *        the only difference of this macro is it allow you to give an id other than LHS
 **/
#define STATIC_ASSERTION_EQ_ID(id,lhs,rhs) struct __const_checker_eq_##id { int test[(((lhs) == (rhs)) - 1) * (1 - ((rhs) == (lhs)))];}

/**
 * @brief assert that the compile time const is less or equal than another const
 **/
#define STATIC_ASSERTION_LE_ID(id,lhs,rhs) struct __const_checker_eq_##id { int test[((rhs) >= (lhs)) - 1]; };

/** @brief this macro is used to check if the size of the variable is expected */
#define STATIC_ASSERTION_VAR_SIZE(id, val) STATIC_ASSERTION_EQ_ID(_size_##id, val, sizeof(id))

/* internal macros do not use outside of this file */
#define __STATIC_ASSERTION_POSITIVE__(name, val) struct __const_checker_##name {int test[(val >= 0) - 1];};
/** @brief check if the member B is followed by member A */
#define STATIC_ASSERTION_FOLLOWS(type, A, B) __STATIC_ASSERTION_POSITIVE__(follows_##type##_##A##_##B, \
        ((long long)offsetof(type,B) - (long long)offsetof(type,A) - (long long)sizeof(((type*)0)->A)) * \
        ((long long)offsetof(type,A) + (long long)sizeof(((type*)0)->A) - (long long)offsetof(type, B)))

/** @brief check if type A and type B are compatible on A.f and B.g. which means we can use both ((A*)ptr)->f and ((B*)ptr)->g to access the same memory */
#define  STATIC_ASSERTION_TYPE_COMPATIBLE(type_a, field_a, type_b, field_b)STATIC_ASSERTION_EQ_ID(type_compatible_ofs_##type_a##_##field_a##_##type_b##field_b, \
        (offsetof(type_a, field_a)),\
        (offsetof(type_b, field_b))); \
        STATIC_ASSERTION_EQ_ID(type_compatible_sz_##type_a##_##field_a##_##type_b##_##field_b, \
        (sizeof(((type_a*)0)->field_a)),\
        (sizeof(((type_b*)0)->field_b)))

/** @brief assert A.F and B.G has the same offset */
#define STATIC_ASSERTION_OFFSET_EQ(A, F, B, G) __STATIC_ASSERTION_POSITIVE__(offset_##type##_##A_##F##_##B_##G, \
        ((long long)offsetof(A,F) - (long long)offsetof(B,G)) * ((long long)offsetof(B,G) - (long long)offsetof(A,F)))

/** @brief check if the member A is before member B */
#define STATIC_ASSERTION_BEFORE(type, A, B) __STATIC_ASSERTION_POSITIVE__(before_##type##_##A##_##B, \
        ((long long)offsetof(type, B) - (long long)offsetof(type, A)))

/** @brief check if the member B is after member A */
#define STATIC_ASSERTION_AFETER(type, A, B) __STATIC_ASSERTION_POSITIVE__(after_##type##_##A##_##B, \
        ((long long)offsetof(type, A) - (long long)offsetof(type, B)))

/** @brief check if the member is the last member */
#define STATIC_ASSERTION_FIRST(type, A) __STATIC_ASSERTION_POSITIVE__(first_##type##_##A,\
        -(long long)offsetof(type, A))

/** @brief check if the member is the last member */
#define STATIC_ASSERTION_LAST(type, A) __STATIC_ASSERTION_POSITIVE__(last_##type##_##A,\
        ((long long)offsetof(type, A) + (long long)sizeof(((type*)0)->A) - (long long)sizeof(type)) * \
        ((long long)sizeof(type) - (long long)offsetof(type, A) - (long long)sizeof(((type*)0)->A)))

/** @brief check the size */
#define STATIC_ASSERTION_SIZE(type, A, S) __STATIC_ASSERTION_POSITIVE__(size_##type##_##A,\
        ((long long)sizeof(((type*)0)->A) - (long long)(S)) *\
        ((long long)(S) - (long long)sizeof(((type*)0)->A)))

/** @brief check the type is signed */
#define STATIC_ASSERTION_SIGNED(type) __STATIC_ASSERTION_POSITIVE__(signed_check_##type,\
        ((~(type)0) < ((type)0)) - 1)

/** @brief check the type is unsigned */
#define STATIC_ASSERTION_UNSIGNED(type) __STATIC_ASSERTION_POSITIVE__(unsigned_check_##type,\
        (((type)0) < (~(type)0)) - 1)
#endif
