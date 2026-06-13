#ifndef SP_ADDON_H
#define SP_ADDON_H

/**
 * @file sp_addon.h
 * @brief Minimal C header for SP Language Native Addons.
 * 
 * This header provides a C99-compatible interface for SP's internal types,
 * allowing non-C++ languages to safely access arguments and return values.
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- SP Internal Layouts --- */

/**
 * @struct sp_value
 * @brief Represents a NaN-boxed SP value.
 */
typedef struct {
    uint64_t bits;
} sp_value;

/**
 * @struct sp_args
 * @brief C-compatible layout for std::vector<Value>.
 */
typedef struct {
    sp_value* start;          /* Pointer to the first element */
    sp_value* finish;         /* Pointer to the end of the data */
    sp_value* end_of_storage; /* Pointer to the end of allocated memory */
} sp_args;

/* --- Constants & Bit Patterns --- */

#define SP_QNAN_MASK 0x7FF0000000000000ULL

/* --- Helper Macros --- */

/**
 * @brief Returns true if the value is a number (not NaN-boxed).
 */
#define SP_IS_NUMBER(v) (((v).bits & SP_QNAN_MASK) != SP_QNAN_MASK)

/**
 * @brief Extracts a double from an sp_value.
 */
#define SP_AS_NUMBER(v) (*(double*)&(v).bits)

/**
 * @brief Extracts a boolean from an sp_value.
 */
#define SP_AS_BOOL(v) ((bool)((v).bits & 1))

/**
 * @brief Creates an sp_value from a double.
 */
#define SP_MAKE_NUMBER(n) ((sp_value){ .bits = *(uint64_t*)&(n) })

/**
 * @brief Returns the number of arguments in the sp_args vector.
 */
#define SP_ARGS_LEN(a) ((size_t)(((uintptr_t)(a)->finish - (uintptr_t)(a)->start) / sizeof(sp_value)))

/**
 * @brief Access an argument by index.
 */
#define SP_GET_ARG(a, i) ((a)->start[i])

#ifdef __cplusplus
}
#endif

#endif /* SP_ADDON_H */
