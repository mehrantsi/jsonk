/**
 * jsonk.h - High-Performance JSON Library for Linux Kernel
 *
 * This library provides efficient JSON parsing, manipulation, and atomic patching
 * capabilities for kernel space. It implements a zero-copy, single-pass parser
 * optimized for kernel space with minimal memory allocations.
 *
 * Features:
 * - Full JSON parsing (RFC 8259 compliant)
 * - JSON serialization
 * - Atomic JSON patching
 * - Memory-efficient data structures
 * - Lock-free design (caller handles synchronization)
 * - Optimized for kernel space
 *
 * Copyright (C) 2025 Mehran Toosi
 * Licensed under GPL-2.0
 */

#ifndef JSONK_H
#define JSONK_H

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/vmalloc.h>
#include <linux/bitmap.h>

/* Maximum nesting depth for JSON structures */
#define JSONK_MAX_DEPTH 32

/* Security limits to prevent DoS attacks */
#define JSONK_MAX_STRING_LENGTH (1024 * 1024)      /* 1MB max string */
#define JSONK_MAX_ARRAY_SIZE 10000                 /* Max array elements */
#define JSONK_MAX_OBJECT_MEMBERS 1000              /* Max object members */
#define JSONK_MAX_TOTAL_MEMORY (64 * 1024 * 1024)  /* 64MB total memory per parse */
#define JSONK_MAX_KEY_LENGTH 256                   /* Max object key length */

/* Maximum length of a JSON path representation */
#define JSONK_MAX_PATH_LEN 256

/* Size threshold for using vmalloc instead of kmalloc */
#define JSONK_LARGE_ALLOC_THRESHOLD (2 * 1024 * 1024) // 2MB

/* Token types for JSON parser */
enum jsonk_token_type {
    JSONK_TOKEN_NONE,
    JSONK_TOKEN_OBJECT_START,    /* { */
    JSONK_TOKEN_OBJECT_END,      /* } */
    JSONK_TOKEN_ARRAY_START,     /* [ */
    JSONK_TOKEN_ARRAY_END,       /* ] */
    JSONK_TOKEN_COLON,           /* : */
    JSONK_TOKEN_COMMA,           /* , */
    JSONK_TOKEN_STRING,          /* "string" */
    JSONK_TOKEN_NUMBER,          /* 123, 123.45, etc. */
    JSONK_TOKEN_TRUE,            /* true */
    JSONK_TOKEN_FALSE,           /* false */
    JSONK_TOKEN_NULL,            /* null */
    JSONK_TOKEN_ERROR            /* Invalid token */
};

/* Token structure to track current JSON parsing state */
struct jsonk_token {
    enum jsonk_token_type type;
    const char *start;     /* Pointer to start of token in original buffer */
    size_t len;            /* Length of the token */
};

/* Parser context structure */
struct jsonk_parser {
    const char *buffer;    /* Input buffer */
    size_t buffer_len;     /* Input buffer length */
    size_t pos;            /* Current position in buffer */
    int depth;             /* Current nesting depth */
    char path[JSONK_MAX_PATH_LEN]; /* Current path in the JSON structure */
    size_t path_len;       /* Current path length */
    
    /* Security tracking */
    size_t total_memory_used;  /* Total memory allocated during parsing */
    size_t string_count;       /* Number of strings parsed */
    size_t array_count;        /* Number of arrays parsed */
    size_t object_count;       /* Number of objects parsed */
};

/* JSON value types for in-memory representation */
enum jsonk_value_type {
    JSONK_VALUE_NULL,
    JSONK_VALUE_BOOLEAN,
    JSONK_VALUE_NUMBER,
    JSONK_VALUE_STRING,
    JSONK_VALUE_ARRAY,
    JSONK_VALUE_OBJECT
};

/* Forward declarations for the in-memory structures */
struct jsonk_value;
struct jsonk_object;
struct jsonk_array;

/* Key-value pair for JSON objects */
struct jsonk_member {
    struct list_head list;    /* Linked list for members */
    char *key;                /* Member key (allocated) */
    size_t key_len;           /* Length of key */
    struct jsonk_value *value; /* Member value */
};

/* Structure for arrays, stores values in a list */
struct jsonk_array {
    struct list_head elements; /* List of jsonk_array_element */
    size_t size;               /* Number of elements */
};

/* Array element wrapper */
struct jsonk_array_element {
    struct list_head list;      /* Linked list for array elements */
    struct jsonk_value *value;   /* Element value */
};

/* Structure for objects, stores key-value pairs */
struct jsonk_object {
    struct list_head members;   /* List of jsonk_member */
    size_t size;                /* Number of members */
};

/* Unified structure for any JSON value */
struct jsonk_value {
    atomic_t refcount;          /* Reference count for memory safety */
    enum jsonk_value_type type;
    union {
        bool boolean;           /* For JSONK_VALUE_BOOLEAN */
        struct {                /* For JSONK_VALUE_NUMBER */
            s64 integer;        /* Integer part */
            u32 fraction;       /* Fraction part (if needed) */
            bool is_negative;   /* Sign indicator */
            bool is_integer;    /* True if pure integer, false if has fraction */
        } number;
        struct {                /* For JSONK_VALUE_STRING */
            char *data;
            size_t len;
        } string;
        struct jsonk_array array;     /* For JSONK_VALUE_ARRAY */
        struct jsonk_object object;   /* For JSONK_VALUE_OBJECT */
    } u;
};

/* Patch operation result codes */
enum jsonk_patch_result {
    JSONK_PATCH_SUCCESS,           /* Operation completed successfully */
    JSONK_PATCH_ERROR_PARSE,       /* Error parsing JSON */
    JSONK_PATCH_ERROR_PATH,        /* Invalid or not found path */
    JSONK_PATCH_ERROR_TYPE,        /* Type mismatch in update */
    JSONK_PATCH_ERROR_MEMORY,      /* Memory allocation error */
    JSONK_PATCH_ERROR_OVERFLOW,    /* Buffer overflow */
    JSONK_PATCH_NO_CHANGE          /* No change needed */
};



/* ========================================================================
 * Memory Management Functions
 * ======================================================================== */

/**
 * Allocate memory with appropriate function based on size
 * @param size Size to allocate
 * @return Pointer to allocated memory or NULL
 */
static inline void *jsonk_memory_alloc(size_t size)
{
    if (size > JSONK_LARGE_ALLOC_THRESHOLD)
        return vmalloc(size);
    else
        return kmalloc(size, GFP_KERNEL);
}

/**
 * Free memory allocated with jsonk_memory_alloc
 * @param ptr Pointer to memory
 * @param size Size of allocation
 */
static inline void jsonk_memory_free(void *ptr, size_t size)
{
    if (size > JSONK_LARGE_ALLOC_THRESHOLD)
        vfree(ptr);
    else
        kfree(ptr);
}

/* ========================================================================
 * Parser Utility Functions
 * ======================================================================== */

/**
 * Initialize a JSON parser
 * @param parser Pointer to parser structure to initialize
 * @param buffer Input JSON buffer
 * @param buffer_len Length of input buffer
 */
static inline void jsonk_parser_init(struct jsonk_parser *parser, const char *buffer, size_t buffer_len)
{
    parser->buffer = buffer;
    parser->buffer_len = buffer_len;
    parser->pos = 0;
    parser->depth = 0;
    parser->path[0] = '\0';
    parser->path_len = 0;
    
    /* Initialize security tracking */
    parser->total_memory_used = 0;
    parser->string_count = 0;
    parser->array_count = 0;
    parser->object_count = 0;
}

/**
 * Skip whitespace in the input buffer
 * @param parser Parser context
 */
static inline void jsonk_skip_whitespace(struct jsonk_parser *parser)
{
    while (parser->pos < parser->buffer_len) {
        char c = parser->buffer[parser->pos];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            break;
        parser->pos++;
    }
}

/**
 * Check if character is a JSON whitespace
 * @param c Character to check
 * @return True if whitespace, false otherwise
 */
static inline bool jsonk_is_whitespace(char c)
{
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

/**
 * Check if character is a JSON structural character
 * @param c Character to check
 * @return True if structural, false otherwise
 */
static inline bool jsonk_is_structural(char c)
{
    return (c == '{' || c == '}' || c == '[' || c == ']' || 
            c == ':' || c == ',' || c == '"');
}

/**
 * Peek at the next character without advancing position
 * @param parser Parser context
 * @return Next character or '\0' if at end of buffer
 */
static inline char jsonk_peek_char(struct jsonk_parser *parser)
{
    if (parser->pos >= parser->buffer_len)
        return '\0';
    return parser->buffer[parser->pos];
}

/**
 * Get the next character and advance position
 * @param parser Parser context
 * @return Next character or '\0' if at end of buffer
 */
static inline char jsonk_next_char(struct jsonk_parser *parser)
{
    if (parser->pos >= parser->buffer_len)
        return '\0';
    return parser->buffer[parser->pos++];
}

/* ========================================================================
 * Core API Functions
 * ======================================================================== */

/**
 * Parse a JSON string into an in-memory structure
 * 
 * NOTE: This function is NOT thread-safe. Callers must ensure proper
 * synchronization when accessing shared JSON data.
 * 
 * @param json_str JSON string to parse
 * @param json_len Length of JSON string
 * @return Pointer to parsed JSON value or NULL on error
 */
struct jsonk_value *jsonk_parse(const char *json_str, size_t json_len);

/**
 * Serialize a JSON value structure to a string
 * 
 * @param value JSON value to serialize
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @param written Pointer to store actual bytes written
 * @return 0 on success, negative error code on failure
 */
int jsonk_serialize(struct jsonk_value *value, char *buffer, size_t buffer_size, size_t *written);

/**
 * Get a reference to a JSON value (increment reference count)
 */
struct jsonk_value *jsonk_value_get(struct jsonk_value *value);

/**
 * Release a reference to a JSON value (decrement reference count)
 */
void jsonk_value_put(struct jsonk_value *value);

/**
 * Apply a JSON patch to a target buffer
 * 
 * This function performs atomic JSON patching - either the entire patch
 * succeeds or fails. It does not provide locking; callers must handle
 * synchronization.
 * 
 * @param target Original JSON buffer
 * @param target_len Length of original buffer
 * @param patch Patch JSON buffer
 * @param patch_len Length of patch buffer
 * @param result Buffer to store the result
 * @param result_max_len Maximum length of result buffer
 * @param result_len Pointer to store the actual result length
 * @return JSONK_PATCH_SUCCESS on success, error code on failure
 */
int jsonk_apply_patch(const char *target, size_t target_len,
                      const char *patch, size_t patch_len,
                      char *result, size_t result_max_len, size_t *result_len);



/* ========================================================================
 * Value Creation and Manipulation Functions
 * ======================================================================== */

/**
 * Create a new JSON value of specified type
 * 
 * @param type Type of value to create
 * @return Pointer to new value or NULL on error
 */
struct jsonk_value *jsonk_value_create(enum jsonk_value_type type);

/**
 * Create a JSON string value
 * 
 * @param str String data
 * @param len Length of string
 * @return Pointer to new string value or NULL on error
 */
struct jsonk_value *jsonk_value_create_string(const char *str, size_t len);

/**
 * Create a JSON number value from string
 * 
 * @param str Number as string
 * @param len Length of string
 * @return Pointer to new number value or NULL on error
 */
struct jsonk_value *jsonk_value_create_number(const char *str, size_t len);

/**
 * Create a JSON boolean value
 * 
 * @param val Boolean value
 * @return Pointer to new boolean value or NULL on error
 */
struct jsonk_value *jsonk_value_create_boolean(bool val);

/**
 * Create a JSON null value
 * 
 * @return Pointer to new null value or NULL on error
 */
struct jsonk_value *jsonk_value_create_null(void);

/**
 * Deep copy a JSON value
 * 
 * @param source Source value to copy
 * @param current_depth Current recursion depth (for safety)
 * @return Pointer to copied value or NULL on error
 */
struct jsonk_value *jsonk_value_deep_copy(struct jsonk_value *source, int current_depth);

/* ========================================================================
 * Object Manipulation Functions
 * ======================================================================== */

/**
 * Add a member to a JSON object
 * 
 * @param obj Object to add member to
 * @param key Member key
 * @param key_len Length of key
 * @param value Member value
 * @return 0 on success, negative error code on failure
 */
int jsonk_object_add_member(struct jsonk_object *obj, const char *key, size_t key_len, struct jsonk_value *value);

/**
 * Find a member in a JSON object
 * 
 * @param obj Object to search
 * @param key Key to find
 * @param key_len Length of key
 * @return Pointer to member or NULL if not found
 */
struct jsonk_member *jsonk_object_find_member(struct jsonk_object *obj, const char *key, size_t key_len);

/**
 * Remove a member from a JSON object
 * 
 * @param obj Object to remove member from
 * @param key Key to remove
 * @param key_len Length of key
 * @return 0 on success, negative error code on failure
 */
int jsonk_object_remove_member(struct jsonk_object *obj, const char *key, size_t key_len);

/* ========================================================================
 * Array Manipulation Functions
 * ======================================================================== */

/**
 * Add an element to a JSON array
 * 
 * @param arr Array to add element to
 * @param value Element value
 * @return 0 on success, negative error code on failure
 */
int jsonk_array_add_element(struct jsonk_array *arr, struct jsonk_value *value);

/* ========================================================================
 * Path-based Access Functions
 * ======================================================================== */

/**
 * Get a value from a JSON object using a dot-separated path
 * 
 * @param root Root JSON value (must be an object)
 * @param path Dot-separated path (e.g., "user.profile.name")
 * @param path_len Length of path string
 * @return Pointer to found value or NULL if not found
 */
struct jsonk_value *jsonk_get_value_by_path(struct jsonk_value *root, const char *path, size_t path_len);

/**
 * Set a value at the specified path in a JSON structure
 * @param root Root JSON value
 * @param path JSON path (e.g., "user.name" or "items[0].id")
 * @param path_len Length of path
 * @param value New value to set
 * @return 0 on success, negative error code on failure
 */
int jsonk_set_value_by_path(struct jsonk_value *root, const char *path, size_t path_len, struct jsonk_value *value);


#endif /* JSONK_H */ 