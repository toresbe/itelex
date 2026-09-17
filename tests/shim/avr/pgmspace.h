/**
 * \file
 * \brief Host build shim for <avr/pgmspace.h>.
 *
 * On an AVR, program memory is a separate address space: data marked
 * \c PROGMEM lives in flash and has to be read through the \c pgm_read_*
 * accessors rather than dereferenced. A host has a single address space, so
 * this shim reduces \c PROGMEM to nothing and the accessors to ordinary
 * dereferences, which lets flash-table code such as the Baudot conversion
 * tables in \c BaudotCode.c be compiled and exercised by the host test
 * suites.
 *
 * Only the subset the suites need is provided. Add to it rather than working
 * around it, so that every suite sees the same model of program memory.
 *
 * \ingroup HOSTTESTS
 */

#ifndef TEST_SHIM_AVR_PGMSPACE_H
#define TEST_SHIM_AVR_PGMSPACE_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/** \addtogroup HOSTTESTS
 *  @{
 */

/** \brief No-op on the host: there is only one address space. */
#define PROGMEM

/** \brief Pointer to a string in program memory. */
typedef const char *PGM_P;
/** \brief Untyped pointer into program memory. */
typedef const void *PGM_VOID_P;

/** \brief Passes a string literal through unchanged; on an AVR it would place it in flash. */
#define PSTR(s) (s)

/** \brief Reads one byte from program memory. */
#define pgm_read_byte(addr)      (*(const uint8_t *)(addr))
/** \brief Near variant of \ref pgm_read_byte; identical on the host. */
#define pgm_read_byte_near(addr) pgm_read_byte(addr)
/** \brief Far variant of \ref pgm_read_byte; identical on the host. */
#define pgm_read_byte_far(addr)  pgm_read_byte(addr)
/** \brief Reads one 16-bit word from program memory. */
#define pgm_read_word(addr)      (*(const uint16_t *)(addr))
/** \brief Reads one 32-bit word from program memory. */
#define pgm_read_dword(addr)     (*(const uint32_t *)(addr))
/** \brief Reads one pointer from program memory. */
#define pgm_read_ptr(addr)       (*(void *const *)(addr))

/** \brief Program-memory \c strlen. */
#define strlen_P(s)              strlen(s)
/** \brief Program-memory \c strcpy. */
#define strcpy_P(d, s)           strcpy((d), (s))
/** \brief Program-memory \c strncpy. */
#define strncpy_P(d, s, n)       strncpy((d), (s), (n))
/** \brief Program-memory \c strcat. */
#define strcat_P(d, s)           strcat((d), (s))
/** \brief Program-memory \c strcmp. */
#define strcmp_P(a, b)           strcmp((a), (b))
/** \brief Program-memory \c strncmp. */
#define strncmp_P(a, b, n)       strncmp((a), (b), (n))
/** \brief Program-memory \c memcpy. */
#define memcpy_P(d, s, n)        memcpy((d), (s), (n))
/** \brief Program-memory \c printf. */
#define printf_P(...)            printf(__VA_ARGS__)
/** \brief Program-memory \c sprintf. */
#define sprintf_P(...)           sprintf(__VA_ARGS__)
/** \brief Program-memory \c snprintf. */
#define snprintf_P(...)          snprintf(__VA_ARGS__)
/** \brief Program-memory \c puts. */
#define puts_P(s)                puts(s)

#ifdef __PROG_TYPES_COMPAT__
/* The firmware builds with -D__PROG_TYPES_COMPAT__, which makes avr-libc
 * expose these deprecated aliases. Mirror them so shimmed sources see the
 * same type names they see on the AVR. */
typedef char     prog_char;    /**< \brief Deprecated avr-libc alias for a flash-resident \c char. */
typedef uint8_t  prog_uint8_t; /**< \brief Deprecated avr-libc alias for a flash-resident \c uint8_t. */
typedef int8_t   prog_int8_t;  /**< \brief Deprecated avr-libc alias for a flash-resident \c int8_t. */
typedef uint16_t prog_uint16_t;/**< \brief Deprecated avr-libc alias for a flash-resident \c uint16_t. */
typedef int16_t  prog_int16_t; /**< \brief Deprecated avr-libc alias for a flash-resident \c int16_t. */
#endif

/** @} */

#endif /* TEST_SHIM_AVR_PGMSPACE_H */
