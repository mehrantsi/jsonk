/**
 * jsonk.c - High-Performance JSON Library for Linux Kernel
 *
 * This file implements the core functionality for the JSONK library,
 * providing efficient methods for parsing JSON, manipulating JSON structures,
 * and applying atomic patches in kernel space.
 *
 * Copyright (C) 2025 Mehran Toosi
 * Licensed under GPL-2.0
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/bug.h>
#include <linux/vmalloc.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/bitmap.h>
#include "../include/jsonk.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mehran Toosi");
MODULE_DESCRIPTION("High-Performance JSON Library for Linux Kernel");
MODULE_VERSION("1.0.0");

/* Kernel slab caches for different object types */
static struct kmem_cache *jsonk_value_cache = NULL;
static struct kmem_cache *jsonk_member_cache = NULL;
static struct kmem_cache *jsonk_element_cache = NULL;

/* ========================================================================
 * Memory Management
 * ======================================================================== */

static void *jsonk_tracked_alloc(struct jsonk_parser *parser, size_t size)
{
    void *ptr;
    
    if (parser && parser->total_memory_used + size > JSONK_MAX_TOTAL_MEMORY) {
        printk(KERN_WARNING "JSONK: Memory limit exceeded (%zu + %zu > %d)\n",
               parser->total_memory_used, size, JSONK_MAX_TOTAL_MEMORY);
        return NULL;
    }
    
    if (size <= JSONK_LARGE_ALLOC_THRESHOLD) {
        ptr = kmalloc(size, GFP_KERNEL);
    } else {
        ptr = vmalloc(size);
    }
    
    if (ptr && parser) {
        parser->total_memory_used += size;
    }
    
    return ptr;
}

static void jsonk_tracked_free(void *ptr, size_t size)
{
    if (!ptr)
        return;
        
    if (size <= JSONK_LARGE_ALLOC_THRESHOLD)
        kfree(ptr);
    else
        vfree(ptr);
}

static struct jsonk_value *jsonk_value_create_tracked(enum jsonk_value_type type, struct jsonk_parser *parser)
{
    struct jsonk_value *value;
    
    if (!jsonk_value_cache) {
        printk(KERN_ERR "JSONK: Value cache not initialized\n");
        return NULL;
    }
    
    if (parser && parser->total_memory_used + sizeof(struct jsonk_value) > JSONK_MAX_TOTAL_MEMORY) {
        printk(KERN_WARNING "JSONK: Memory limit exceeded for value creation\n");
        return NULL;
    }
    
    value = kmem_cache_alloc(jsonk_value_cache, GFP_KERNEL);
    if (!value)
        return NULL;
    
    memset(value, 0, sizeof(struct jsonk_value));
    atomic_set(&value->refcount, 1);
    value->type = type;
    
    if (parser) {
        parser->total_memory_used += sizeof(struct jsonk_value);
    }
    
    if (type == JSONK_VALUE_OBJECT) {
        INIT_LIST_HEAD(&value->u.object.members);
        value->u.object.size = 0;
        if (parser) {
            parser->object_count++;
        }
    } else if (type == JSONK_VALUE_ARRAY) {
        INIT_LIST_HEAD(&value->u.array.elements);
        value->u.array.size = 0;
        if (parser) {
            parser->array_count++;
        }
    }
    
    return value;
}

/* ========================================================================
 * Internal Helper Functions
 * ======================================================================== */

static int jsonk_parse_string(struct jsonk_parser *parser, struct jsonk_token *token)
{
    size_t start_pos;
    char c;
    
    /* Skip the opening quote */
    parser->pos++;
    start_pos = parser->pos;
    
    /* Find the closing quote, handling escapes */
    while (parser->pos < parser->buffer_len) {
        c = parser->buffer[parser->pos];
        
        if (c == '"') {
            /* Found closing quote */
            token->type = JSONK_TOKEN_STRING;
            token->start = &parser->buffer[start_pos];
            token->len = parser->pos - start_pos;
            parser->pos++; /* Skip closing quote */
            return 0;
        } else if (c == '\\') {
            /* Handle escape sequence */
            parser->pos++; /* Skip backslash */
            if (parser->pos >= parser->buffer_len) {
                return -EINVAL; /* Incomplete escape */
            }
            
            c = parser->buffer[parser->pos];
            switch (c) {
            case '"':
            case '\\':
            case '/':
            case 'b':
            case 'f':
            case 'n':
            case 'r':
            case 't':
                parser->pos++; /* Valid escape */
                break;
            case 'u':
                /* Unicode escape: \uXXXX */
                parser->pos++; /* Skip 'u' */
                for (int i = 0; i < 4; i++) {
                    if (parser->pos >= parser->buffer_len) {
                        return -EINVAL;
                    }
                    c = parser->buffer[parser->pos];
                    if (!((c >= '0' && c <= '9') || 
                          (c >= 'a' && c <= 'f') || 
                          (c >= 'A' && c <= 'F'))) {
                        return -EINVAL; /* Invalid hex digit */
                    }
                    parser->pos++;
                }
                break;
            default:
                return -EINVAL; /* Invalid escape sequence */
            }
        } else if (c < 0x20) {
            /* Control characters must be escaped */
            return -EINVAL;
        } else {
            parser->pos++;
        }
    }
    
    /* Reached end without closing quote */
    return -EINVAL;
}

static int jsonk_parse_number(struct jsonk_parser *parser, struct jsonk_token *token)
{
    size_t start_pos = parser->pos;
    char c;
    
    /* Parse optional minus sign */
    c = jsonk_peek_char(parser);
    if (c == '-') {
        parser->pos++;
    }
    
    /* Parse integer part */
    c = jsonk_peek_char(parser);
    if (c == '0') {
        /* Leading zero - must not be followed by more digits */
        parser->pos++;
        c = jsonk_peek_char(parser);
        if (c >= '0' && c <= '9') {
            return -EINVAL; /* Invalid: leading zeros not allowed */
        }
    } else if (c >= '1' && c <= '9') {
        /* Non-zero digit - parse remaining digits */
        parser->pos++;
        while (parser->pos < parser->buffer_len) {
            c = parser->buffer[parser->pos];
            if (c >= '0' && c <= '9') {
                parser->pos++;
            } else {
                break;
            }
        }
    } else {
        return -EINVAL; /* Invalid: no digits found */
    }
    
    /* Parse optional fractional part */
    if (parser->pos < parser->buffer_len && parser->buffer[parser->pos] == '.') {
        parser->pos++; /* Skip decimal point */
        
        /* Must have at least one digit after decimal point */
        if (parser->pos >= parser->buffer_len || 
            parser->buffer[parser->pos] < '0' || 
            parser->buffer[parser->pos] > '9') {
            return -EINVAL;
        }
        
        /* Parse fractional digits */
        while (parser->pos < parser->buffer_len) {
            c = parser->buffer[parser->pos];
            if (c >= '0' && c <= '9') {
                parser->pos++;
            } else {
                break;
            }
        }
    }
    
    /* Parse optional exponent */
    if (parser->pos < parser->buffer_len && 
        (parser->buffer[parser->pos] == 'e' || parser->buffer[parser->pos] == 'E')) {
        parser->pos++; /* Skip 'e' or 'E' */
        
        /* Parse optional exponent sign */
        if (parser->pos < parser->buffer_len && 
            (parser->buffer[parser->pos] == '+' || parser->buffer[parser->pos] == '-')) {
            parser->pos++;
        }
        
        /* Must have at least one digit in exponent */
        if (parser->pos >= parser->buffer_len || 
            parser->buffer[parser->pos] < '0' || 
            parser->buffer[parser->pos] > '9') {
            return -EINVAL;
        }
        
        /* Parse exponent digits */
        while (parser->pos < parser->buffer_len) {
            c = parser->buffer[parser->pos];
            if (c >= '0' && c <= '9') {
                parser->pos++;
            } else {
                break;
            }
        }
    }
    
    /* Set token */
    token->type = JSONK_TOKEN_NUMBER;
    token->start = &parser->buffer[start_pos];
    token->len = parser->pos - start_pos;
    return 0;
}

static int jsonk_parse_literal(struct jsonk_parser *parser, struct jsonk_token *token)
{
    size_t start_pos = parser->pos;
    const char *literals[] = {"true", "false", "null"};
    enum jsonk_token_type types[] = {JSONK_TOKEN_TRUE, JSONK_TOKEN_FALSE, JSONK_TOKEN_NULL};
    size_t lengths[] = {4, 5, 4};
    int i;
    
    for (i = 0; i < 3; i++) {
        if (parser->pos + lengths[i] <= parser->buffer_len &&
            memcmp(&parser->buffer[parser->pos], literals[i], lengths[i]) == 0) {
            
            token->type = types[i];
            token->start = &parser->buffer[start_pos];
            token->len = lengths[i];
            parser->pos += lengths[i];
            return 0;
        }
    }
    
    return -EINVAL;
}

/**
 * Get the next token from the JSON input
 * @param parser Parser context
 * @param token Token structure to fill
 * @return 0 on success, negative error code on failure
 */
static int jsonk_next_token(struct jsonk_parser *parser, struct jsonk_token *token)
{
    char c;
    
    /* Skip whitespace */
    jsonk_skip_whitespace(parser);
    
    /* Check if we reached the end */
    if (parser->pos >= parser->buffer_len)
        return -ENODATA;
    
    c = parser->buffer[parser->pos];
    
    /* Parse the token based on the first character */
    switch (c) {
    case '{':
        token->type = JSONK_TOKEN_OBJECT_START;
        token->start = &parser->buffer[parser->pos];
        token->len = 1;
        parser->pos++;
        return 0;
    case '}':
        token->type = JSONK_TOKEN_OBJECT_END;
        token->start = &parser->buffer[parser->pos];
        token->len = 1;
        parser->pos++;
        return 0;
    case '[':
        token->type = JSONK_TOKEN_ARRAY_START;
        token->start = &parser->buffer[parser->pos];
        token->len = 1;
        parser->pos++;
        return 0;
    case ']':
        token->type = JSONK_TOKEN_ARRAY_END;
        token->start = &parser->buffer[parser->pos];
        token->len = 1;
        parser->pos++;
        return 0;
    case ':':
        token->type = JSONK_TOKEN_COLON;
        token->start = &parser->buffer[parser->pos];
        token->len = 1;
        parser->pos++;
        return 0;
    case ',':
        token->type = JSONK_TOKEN_COMMA;
        token->start = &parser->buffer[parser->pos];
        token->len = 1;
        parser->pos++;
        return 0;
    case '"':
        return jsonk_parse_string(parser, token);
    case '-':
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
        return jsonk_parse_number(parser, token);
    case 't':
    case 'f':
    case 'n':
        return jsonk_parse_literal(parser, token);
    default:
        return -EINVAL;
    }
}

/* ========================================================================
 * Value Creation and Management Functions
 * ======================================================================== */

/**
 * Unescape a JSON string and create a value with tracking
 */
static struct jsonk_value *jsonk_value_create_string_tracked(const char *str, size_t len, struct jsonk_parser *parser)
{
    struct jsonk_value *value;
    char *unescaped;
    size_t unescaped_len = 0;
    size_t i = 0;
    
    /* Check string length limit */
    if (len > JSONK_MAX_STRING_LENGTH) {
        printk(KERN_WARNING "JSONK: String too long (%zu > %d)\n", len, JSONK_MAX_STRING_LENGTH);
        return NULL;
    }
    
    /* Check string count limit */
    if (parser && parser->string_count >= JSONK_MAX_ARRAY_SIZE) {
        printk(KERN_WARNING "JSONK: Too many strings (%zu >= %d)\n", parser->string_count, JSONK_MAX_ARRAY_SIZE);
        return NULL;
    }
    
    value = jsonk_value_create_tracked(JSONK_VALUE_STRING, parser);
    if (!value)
        return NULL;
    
    /* Allocate buffer for unescaped string (worst case: same size) */
    unescaped = jsonk_tracked_alloc(parser, len + 1);
    if (!unescaped) {
        kmem_cache_free(jsonk_value_cache, value);
        return NULL;
    }
    
    /* Process the string, handling escape sequences */
    while (i < len) {
        char c = str[i];
        
        if (c == '\\' && i + 1 < len) {
            /* Handle escape sequence */
            i++; /* Skip backslash */
            char escaped = str[i];
            
            switch (escaped) {
            case '"':
                unescaped[unescaped_len++] = '"';
                break;
            case '\\':
                unescaped[unescaped_len++] = '\\';
                break;
            case '/':
                unescaped[unescaped_len++] = '/';
                break;
            case 'b':
                unescaped[unescaped_len++] = '\b';
                break;
            case 'f':
                unescaped[unescaped_len++] = '\f';
                break;
            case 'n':
                unescaped[unescaped_len++] = '\n';
                break;
            case 'r':
                unescaped[unescaped_len++] = '\r';
                break;
            case 't':
                unescaped[unescaped_len++] = '\t';
                break;
            case 'u':
                /* Unicode escape: \uXXXX - simplified to ASCII for kernel space */
                if (i + 4 < len) {
                    /* For kernel space, we'll just store the literal \uXXXX */
                    /* In production, you might want proper Unicode handling */
                    unescaped[unescaped_len++] = '\\';
                    unescaped[unescaped_len++] = 'u';
                    i++; /* We'll copy the hex digits as-is */
                    for (int j = 0; j < 4 && i < len; j++, i++) {
                        unescaped[unescaped_len++] = str[i];
                    }
                    i--; /* Adjust for loop increment */
                } else {
                    /* Invalid unicode escape */
                    jsonk_tracked_free(unescaped, len + 1);
                    kmem_cache_free(jsonk_value_cache, value);
                    return NULL;
                }
                break;
            default:
                /* Invalid escape sequence */
                jsonk_tracked_free(unescaped, len + 1);
                kmem_cache_free(jsonk_value_cache, value);
                return NULL;
            }
            i++;
        } else {
            /* Regular character */
            unescaped[unescaped_len++] = c;
            i++;
        }
    }
    
    unescaped[unescaped_len] = '\0';
    
    /* Resize buffer to actual size needed */
    if (unescaped_len < len) {
        char *resized = jsonk_tracked_alloc(parser, unescaped_len + 1);
        if (resized) {
            memcpy(resized, unescaped, unescaped_len + 1);
            jsonk_tracked_free(unescaped, len + 1);
            unescaped = resized;
        }
        /* If resize fails, keep the larger buffer */
    }
    
    value->u.string.data = unescaped;
    value->u.string.len = unescaped_len;
    
    if (parser) {
        parser->string_count++;
    }
    
    return value;
}

/**
 * Create a JSON number value from string (improved implementation)
 */
struct jsonk_value *jsonk_value_create_number(const char *str, size_t len)
{
    struct jsonk_value *value;
    char *num_str;
    char *endptr;
    
    value = jsonk_value_create(JSONK_VALUE_NUMBER);
    if (!value)
        return NULL;
    
    /* Initialize default values */
    value->u.number.integer = 0;
    value->u.number.fraction = 0;
    value->u.number.is_negative = false;
    value->u.number.is_integer = true;
    
    /* Allocate null-terminated string for parsing */
    num_str = jsonk_memory_alloc(len + 1);
    if (!num_str) {
        kmem_cache_free(jsonk_value_cache, value);
        return NULL;
    }
    
    memcpy(num_str, str, len);
    num_str[len] = '\0';
    
    /* Check if it contains decimal point or exponent */
    bool has_decimal = false;
    bool has_exponent = false;
    size_t i;
    
    for (i = 0; i < len; i++) {
        if (num_str[i] == '.') {
            has_decimal = true;
        } else if (num_str[i] == 'e' || num_str[i] == 'E') {
            has_exponent = true;
            break;
        }
    }
    
    if (has_decimal || has_exponent) {
        /* Parse as floating point - simplified approach */
        value->u.number.is_integer = false;
        
        /* For kernel space, we'll store a simplified representation */
        /* Parse integer part */
        s64 int_part = 0;
        i = 0;
        bool negative = false;
        
        if (num_str[i] == '-') {
            negative = true;
            i++;
        }
        
        /* Parse digits before decimal */
        while (i < len && num_str[i] >= '0' && num_str[i] <= '9') {
            if (int_part > S64_MAX / 10) {
                int_part = S64_MAX; /* Overflow protection */
                break;
            }
            int_part = int_part * 10 + (num_str[i] - '0');
            i++;
        }
        
        value->u.number.integer = negative ? -int_part : int_part;
        value->u.number.is_negative = negative;
        
        /* Parse fractional part if present */
        if (i < len && num_str[i] == '.') {
            i++; /* Skip decimal point */
            u32 frac_part = 0;
            int frac_digits = 0;
            
            while (i < len && num_str[i] >= '0' && num_str[i] <= '9' && frac_digits < 9) {
                if (frac_part > UINT_MAX / 10) {
                    break; /* Overflow protection */
                }
                frac_part = frac_part * 10 + (num_str[i] - '0');
                frac_digits++;
                i++;
            }
            
            value->u.number.fraction = frac_part;
        }
        
        /* Note: Exponent handling is simplified for kernel space */
        /* In production, you might want more sophisticated floating point handling */
        
    } else {
        /* Parse as integer */
        value->u.number.is_integer = true;
        value->u.number.integer = simple_strtoll(num_str, &endptr, 10);
        value->u.number.is_negative = (value->u.number.integer < 0);
        
        /* Validate that entire string was consumed */
        if (endptr != num_str + len) {
            jsonk_memory_free(num_str, len + 1);
            kmem_cache_free(jsonk_value_cache, value);
            return NULL;
        }
    }
    
    jsonk_memory_free(num_str, len + 1);
    return value;
}

/**
 * Create a JSON boolean value
 */
struct jsonk_value *jsonk_value_create_boolean(bool val)
{
    struct jsonk_value *value;
    
    value = jsonk_value_create(JSONK_VALUE_BOOLEAN);
    if (!value)
        return NULL;
    
    value->u.boolean = val;
    return value;
}

/**
 * Create a JSON null value
 */
struct jsonk_value *jsonk_value_create_null(void)
{
    return jsonk_value_create(JSONK_VALUE_NULL);
}

static void jsonk_value_free_internal(struct jsonk_value *value)
{
    struct jsonk_member *member, *tmp_member;
    struct jsonk_array_element *element, *tmp_element;
    
    if (!value)
        return;
    
    switch (value->type) {
    case JSONK_VALUE_STRING:
        if (value->u.string.data)
            jsonk_memory_free(value->u.string.data, value->u.string.len + 1);
        break;
        
    case JSONK_VALUE_OBJECT:
        list_for_each_entry_safe(member, tmp_member, &value->u.object.members, list) {
            list_del(&member->list);
            if (member->key)
                jsonk_memory_free(member->key, member->key_len + 1);
            if (member->value)
                jsonk_value_put(member->value);
            kmem_cache_free(jsonk_member_cache, member);
        }
        break;
        
    case JSONK_VALUE_ARRAY:
        list_for_each_entry_safe(element, tmp_element, &value->u.array.elements, list) {
            list_del(&element->list);
            if (element->value)
                jsonk_value_put(element->value);
            kmem_cache_free(jsonk_element_cache, element);
        }
        break;
        
    default:
        break;
    }
    
    kmem_cache_free(jsonk_value_cache, value);
}

struct jsonk_value *jsonk_value_get(struct jsonk_value *value)
{
    if (value)
        atomic_inc(&value->refcount);
    return value;
}

void jsonk_value_put(struct jsonk_value *value)
{
    if (!value)
        return;
    
    if (atomic_dec_and_test(&value->refcount)) {
        jsonk_value_free_internal(value);
    }
}

/* ========================================================================
 * Object Manipulation Functions
 * ======================================================================== */

/**
 * Add a member to a JSON object with tracking
 */
static int jsonk_object_add_member_tracked(struct jsonk_object *obj, const char *key, size_t key_len, 
                                          struct jsonk_value *value, struct jsonk_parser *parser)
{
    struct jsonk_member *member;
    
    /* Check object member limit */
    if (obj->size >= JSONK_MAX_OBJECT_MEMBERS) {
        printk(KERN_WARNING "JSONK: Too many object members (%zu >= %d)\n", 
               obj->size, JSONK_MAX_OBJECT_MEMBERS);
        return -ENOSPC;
    }
    
    /* Check key length limit */
    if (key_len > JSONK_MAX_KEY_LENGTH) {
        printk(KERN_WARNING "JSONK: Object key too long (%zu > %d)\n", 
               key_len, JSONK_MAX_KEY_LENGTH);
        return -EINVAL;
    }
    
    /* Create new member */
    if (!jsonk_member_cache) {
        printk(KERN_ERR "JSONK: Member cache not initialized\n");
        return -ENOMEM;
    }
    
    /* Check memory limit */
    if (parser && parser->total_memory_used + sizeof(struct jsonk_member) + key_len + 1 > JSONK_MAX_TOTAL_MEMORY) {
        printk(KERN_WARNING "JSONK: Memory limit exceeded for member creation\n");
        return -ENOMEM;
    }
    
    member = kmem_cache_alloc(jsonk_member_cache, GFP_KERNEL);
    if (!member)
        return -ENOMEM;
    
    member->key = jsonk_tracked_alloc(parser, key_len + 1);
    if (!member->key) {
        kmem_cache_free(jsonk_member_cache, member);
        return -ENOMEM;
    }
    
    memcpy(member->key, key, key_len);
    member->key[key_len] = '\0';
    member->key_len = key_len;
    member->value = value;
    
    list_add_tail(&member->list, &obj->members);
    obj->size++;
    
    return 0;
}

/**
 * Find a member in a JSON object
 */
struct jsonk_member *jsonk_object_find_member(struct jsonk_object *obj, const char *key, size_t key_len)
{
    struct jsonk_member *member;
    
    list_for_each_entry(member, &obj->members, list) {
        if (member->key_len == key_len && 
            memcmp(member->key, key, key_len) == 0) {
            return member;
        }
    }
    
    return NULL;
}

/**
 * Remove a member from a JSON object
 */
int jsonk_object_remove_member(struct jsonk_object *obj, const char *key, size_t key_len)
{
    struct jsonk_member *member = jsonk_object_find_member(obj, key, key_len);
    
    if (!member)
        return -ENOENT;
    
    list_del(&member->list);
    obj->size--;
    
    if (member->key)
        jsonk_memory_free(member->key, member->key_len + 1);
    if (member->value)
        jsonk_value_put(member->value);
    kmem_cache_free(jsonk_member_cache, member);
    
    return 0;
}

/* ========================================================================
 * Array Manipulation Functions
 * ======================================================================== */

/**
 * Add an element to a JSON array with tracking
 */
static int jsonk_array_add_element_tracked(struct jsonk_array *arr, struct jsonk_value *value, struct jsonk_parser *parser)
{
    struct jsonk_array_element *element;
    
    /* Check array size limit */
    if (arr->size >= JSONK_MAX_ARRAY_SIZE) {
        printk(KERN_WARNING "JSONK: Array too large (%zu >= %d)\n", 
               arr->size, JSONK_MAX_ARRAY_SIZE);
        return -ENOSPC;
    }
    
    if (!jsonk_element_cache) {
        printk(KERN_ERR "JSONK: Element cache not initialized\n");
        return -ENOMEM;
    }
    
    /* Check memory limit */
    if (parser && parser->total_memory_used + sizeof(struct jsonk_array_element) > JSONK_MAX_TOTAL_MEMORY) {
        printk(KERN_WARNING "JSONK: Memory limit exceeded for element creation\n");
        return -ENOMEM;
    }
    
    element = kmem_cache_alloc(jsonk_element_cache, GFP_KERNEL);
    if (!element)
        return -ENOMEM;
    
    element->value = value;
    list_add_tail(&element->list, &arr->elements);
    arr->size++;
    
    if (parser) {
        parser->total_memory_used += sizeof(struct jsonk_array_element);
    }
    
    return 0;
}

/* ========================================================================
 * Parser Implementation
 * ======================================================================== */

static struct jsonk_value *jsonk_parse_value(struct jsonk_parser *parser);

/**
 * Parse a JSON object
 */
static struct jsonk_value *jsonk_parse_object(struct jsonk_parser *parser)
{
    struct jsonk_value *object_value;
    struct jsonk_token token;
    int ret;
    
    object_value = jsonk_value_create(JSONK_VALUE_OBJECT);
    if (!object_value)
        return NULL;
    
    /* Skip the opening brace */
    ret = jsonk_next_token(parser, &token);
    if (ret < 0 || token.type != JSONK_TOKEN_OBJECT_START) {
        jsonk_value_put(object_value);
        return NULL;
    }
    
    /* Check for empty object */
    ret = jsonk_next_token(parser, &token);
    if (ret < 0) {
        jsonk_value_put(object_value);
        return NULL;
    }
    
    if (token.type == JSONK_TOKEN_OBJECT_END)
        return object_value;
    
    /* Parse key-value pairs */
    while (true) {
        /* Expect a string key */
        if (token.type != JSONK_TOKEN_STRING) {
            jsonk_value_put(object_value);
            return NULL;
        }
        
        /* Extract key string */
        char *key = jsonk_memory_alloc(token.len + 1);
        if (!key)
            return NULL;
        
        memcpy(key, token.start, token.len);
        key[token.len] = '\0';
        
        /* Expect colon */
        ret = jsonk_next_token(parser, &token);
        if (ret < 0 || token.type != JSONK_TOKEN_COLON) {
            jsonk_memory_free(key, token.len + 1);
            jsonk_value_put(object_value);
            return NULL;
        }
        
        /* Parse value */
        struct jsonk_value *member_value = jsonk_parse_value(parser);
        if (!member_value) {
            jsonk_memory_free(key, token.len + 1);
            jsonk_value_put(object_value);
            return NULL;
        }
        
        /* Add member to object */
        ret = jsonk_object_add_member_tracked(&object_value->u.object, key, token.len, member_value, parser);
        jsonk_memory_free(key, token.len + 1);
        if (ret < 0) {
            jsonk_value_put(member_value);
            jsonk_value_put(object_value);
            return NULL;
        }
        
        /* Check for comma or end of object */
        ret = jsonk_next_token(parser, &token);
        if (ret < 0) {
            jsonk_value_put(object_value);
            return NULL;
        }
        
        if (token.type == JSONK_TOKEN_OBJECT_END)
            break;
        
        if (token.type != JSONK_TOKEN_COMMA) {
            jsonk_value_put(object_value);
            return NULL;
        }
        
        /* Get next key */
        ret = jsonk_next_token(parser, &token);
        if (ret < 0) {
            jsonk_value_put(object_value);
            return NULL;
        }
    }
    
    return object_value;
}

/**
 * Parse a JSON array
 */
static struct jsonk_value *jsonk_parse_array(struct jsonk_parser *parser)
{
    struct jsonk_value *array_value;
    struct jsonk_token token;
    int ret;
    
    array_value = jsonk_value_create(JSONK_VALUE_ARRAY);
    if (!array_value)
        return NULL;
    
    /* Consume the opening bracket */
    ret = jsonk_next_token(parser, &token);
    if (ret < 0 || token.type != JSONK_TOKEN_ARRAY_START) {
        jsonk_value_put(array_value);
        return NULL;
    }
    
    /* Parse array elements */
    while (1) {
        /* Peek at the next token to check for empty array */
        jsonk_skip_whitespace(parser);
        if (parser->pos < parser->buffer_len && parser->buffer[parser->pos] == ']') {
            /* Empty array, consume the closing bracket */
            parser->pos++;
            break;
        }
        
        /* Parse the element value */
        struct jsonk_value *element_value = jsonk_parse_value(parser);
        if (!element_value) {
            jsonk_value_put(array_value);
            return NULL;
        }
        
        /* Add the element to the array */
        ret = jsonk_array_add_element_tracked(&array_value->u.array, element_value, parser);
        if (ret < 0) {
            jsonk_value_put(element_value);
            jsonk_value_put(array_value);
            return NULL;
        }
        
        /* Look ahead for comma or end of array */
        ret = jsonk_next_token(parser, &token);
        if (ret < 0) {
            jsonk_value_put(array_value);
            return NULL;
        }
        
        if (token.type == JSONK_TOKEN_ARRAY_END)
            break;
        
        if (token.type != JSONK_TOKEN_COMMA) {
            jsonk_value_put(array_value);
            return NULL;
        }
    }
    
    return array_value;
}

/**
 * Parse a JSON value (recursive)
 */
static struct jsonk_value *jsonk_parse_value(struct jsonk_parser *parser)
{
    struct jsonk_token token;
    struct jsonk_value *value = NULL;
    int ret;
    
    /* Check depth limit */
    if (parser->depth >= JSONK_MAX_DEPTH)
        return NULL;
    
    parser->depth++;
    
    ret = jsonk_next_token(parser, &token);
    if (ret < 0) {
        parser->depth--;
        return NULL;
    }
    
    switch (token.type) {
    case JSONK_TOKEN_OBJECT_START:
        /* Put back the token and parse object */
        parser->pos -= token.len;
        value = jsonk_parse_object(parser);
        break;
        
    case JSONK_TOKEN_ARRAY_START:
        /* Put back the token and parse array */
        parser->pos -= token.len;
        value = jsonk_parse_array(parser);
        break;
        
    case JSONK_TOKEN_STRING:
        value = jsonk_value_create_string_tracked(token.start, token.len, parser);
        break;
        
    case JSONK_TOKEN_NUMBER:
        value = jsonk_value_create_number(token.start, token.len);
        break;
        
    case JSONK_TOKEN_TRUE:
        value = jsonk_value_create_boolean(true);
        break;
        
    case JSONK_TOKEN_FALSE:
        value = jsonk_value_create_boolean(false);
        break;
        
    case JSONK_TOKEN_NULL:
        value = jsonk_value_create_null();
        break;
        
    default:
        /* Invalid token */
        break;
    }
    
    parser->depth--;
    return value;
}

/**
 * Parse a JSON string into an in-memory structure
 */
struct jsonk_value *jsonk_parse(const char *json_str, size_t json_len)
{
    struct jsonk_parser parser;
    struct jsonk_value *value;
    
    if (!json_str || json_len == 0)
        return NULL;
    
    jsonk_parser_init(&parser, json_str, json_len);
    value = jsonk_parse_value(&parser);
    
    return value;
}

/* ========================================================================
 * Serialization Implementation
 * ======================================================================== */

/**
 * Serialize a JSON value structure to a string
 */
int jsonk_serialize(struct jsonk_value *value, char *buffer, size_t buffer_size, size_t *written)
{
    size_t pos = 0;
    struct jsonk_member *member;
    struct jsonk_array_element *element;
    bool first;
    
    if (!value || !buffer || !written)
        return -EINVAL;
    
    *written = 0;
    
    switch (value->type) {
    case JSONK_VALUE_NULL:
        if (pos + 4 >= buffer_size)
            return -EOVERFLOW;
        memcpy(buffer + pos, "null", 4);
        pos += 4;
        break;
        
    case JSONK_VALUE_BOOLEAN:
        if (value->u.boolean) {
            if (pos + 4 >= buffer_size)
                return -EOVERFLOW;
            memcpy(buffer + pos, "true", 4);
            pos += 4;
        } else {
            if (pos + 5 >= buffer_size)
                return -EOVERFLOW;
            memcpy(buffer + pos, "false", 5);
            pos += 5;
        }
        break;
        
    case JSONK_VALUE_NUMBER:
        /* Simple integer serialization for now */
        if (value->u.number.is_integer) {
            int len = snprintf(buffer + pos, buffer_size - pos, "%lld", 
                             value->u.number.is_negative ? -value->u.number.integer : value->u.number.integer);
            if (len < 0 || pos + len >= buffer_size)
                return -EOVERFLOW;
            pos += len;
        } else {
            /* Handle fractional numbers - simplified */
            int len = snprintf(buffer + pos, buffer_size - pos, "%lld.%u", 
                             value->u.number.is_negative ? -value->u.number.integer : value->u.number.integer,
                             value->u.number.fraction);
            if (len < 0 || pos + len >= buffer_size)
                return -EOVERFLOW;
            pos += len;
        }
        break;
        
    case JSONK_VALUE_STRING:
        /* Output with quotes and proper escaping */
        if (pos + 1 >= buffer_size)
            return -EOVERFLOW;
        
        buffer[pos++] = '"';
        
        /* Copy string with escaping */
        for (size_t i = 0; i < value->u.string.len; i++) {
            char c = value->u.string.data[i];
            
            /* Check for characters that need escaping */
            if (c == '"' || c == '\\') {
                if (pos + 2 >= buffer_size)
                    return -EOVERFLOW;
                buffer[pos++] = '\\';
                buffer[pos++] = c;
            } else if (c == '\b') {
                if (pos + 2 >= buffer_size)
                    return -EOVERFLOW;
                buffer[pos++] = '\\';
                buffer[pos++] = 'b';
            } else if (c == '\f') {
                if (pos + 2 >= buffer_size)
                    return -EOVERFLOW;
                buffer[pos++] = '\\';
                buffer[pos++] = 'f';
            } else if (c == '\n') {
                if (pos + 2 >= buffer_size)
                    return -EOVERFLOW;
                buffer[pos++] = '\\';
                buffer[pos++] = 'n';
            } else if (c == '\r') {
                if (pos + 2 >= buffer_size)
                    return -EOVERFLOW;
                buffer[pos++] = '\\';
                buffer[pos++] = 'r';
            } else if (c == '\t') {
                if (pos + 2 >= buffer_size)
                    return -EOVERFLOW;
                buffer[pos++] = '\\';
                buffer[pos++] = 't';
            } else {
                if (pos + 1 >= buffer_size)
                    return -EOVERFLOW;
                buffer[pos++] = c;
            }
        }
        
        if (pos + 1 >= buffer_size)
            return -EOVERFLOW;
        buffer[pos++] = '"';
        break;
        
    case JSONK_VALUE_OBJECT:
        if (pos + 1 >= buffer_size)
            return -EOVERFLOW;
        buffer[pos++] = '{';
        
        first = true;
        list_for_each_entry(member, &value->u.object.members, list) {
            if (!first) {
                if (pos + 1 >= buffer_size)
                    return -EOVERFLOW;
                buffer[pos++] = ',';
            }
            first = false;
            
            /* Write key */
            if (pos + member->key_len + 3 >= buffer_size)
                return -EOVERFLOW;
            buffer[pos++] = '"';
            memcpy(buffer + pos, member->key, member->key_len);
            pos += member->key_len;
            buffer[pos++] = '"';
            buffer[pos++] = ':';
            
            /* Write value */
            size_t value_written;
            int ret = jsonk_serialize(member->value, buffer + pos, buffer_size - pos, &value_written);
            if (ret < 0)
                return ret;
            pos += value_written;
        }
        
        if (pos + 1 >= buffer_size)
            return -EOVERFLOW;
        buffer[pos++] = '}';
        break;
        
    case JSONK_VALUE_ARRAY:
        if (pos + 1 >= buffer_size)
            return -EOVERFLOW;
        buffer[pos++] = '[';
        
        first = true;
        list_for_each_entry(element, &value->u.array.elements, list) {
            if (!first) {
                if (pos + 1 >= buffer_size)
                    return -EOVERFLOW;
                buffer[pos++] = ',';
            }
            first = false;
            
            /* Write element */
            size_t element_written;
            int ret = jsonk_serialize(element->value, buffer + pos, buffer_size - pos, &element_written);
            if (ret < 0)
                return ret;
            pos += element_written;
        }
        
        if (pos + 1 >= buffer_size)
            return -EOVERFLOW;
        buffer[pos++] = ']';
        break;
    }
    
    *written = pos;
    return 0;
}

/* ========================================================================
 * Deep Copy Implementation
 * ======================================================================== */

/**
 * Deep copy a JSON value
 */
struct jsonk_value *jsonk_value_deep_copy(struct jsonk_value *source, int current_depth)
{
    struct jsonk_value *copy = NULL;
    struct jsonk_member *member;
    struct jsonk_array_element *element;
    
    if (!source || current_depth > JSONK_MAX_DEPTH)
        return NULL;
    
    switch (source->type) {
    case JSONK_VALUE_NULL:
        copy = jsonk_value_create_null();
        break;
        
    case JSONK_VALUE_BOOLEAN:
        copy = jsonk_value_create_boolean(source->u.boolean);
        break;
        
    case JSONK_VALUE_NUMBER:
        copy = jsonk_value_create(JSONK_VALUE_NUMBER);
        if (copy) {
            copy->u.number = source->u.number;
        }
        break;
        
    case JSONK_VALUE_STRING:
        copy = jsonk_value_create_string_tracked(source->u.string.data, source->u.string.len, NULL);
        break;
        
    case JSONK_VALUE_OBJECT:
        copy = jsonk_value_create(JSONK_VALUE_OBJECT);
        if (copy) {
            list_for_each_entry(member, &source->u.object.members, list) {
                struct jsonk_value *value_copy = jsonk_value_deep_copy(member->value, current_depth + 1);
                if (value_copy) {
                    jsonk_object_add_member_tracked(&copy->u.object, member->key, member->key_len, value_copy, NULL);
                }
            }
        }
        break;
        
    case JSONK_VALUE_ARRAY:
        copy = jsonk_value_create(JSONK_VALUE_ARRAY);
        if (copy) {
            list_for_each_entry(element, &source->u.array.elements, list) {
                struct jsonk_value *value_copy = jsonk_value_deep_copy(element->value, current_depth + 1);
                if (value_copy) {
                    jsonk_array_add_element_tracked(&copy->u.array, value_copy, NULL);
                }
            }
        }
        break;
    }
    
    return copy;
}

/* ========================================================================
 * JSON Patch Implementation
 * ======================================================================== */

/**
 * Parse a path component and find the next dot
 * @param path Current path position
 * @param path_len Remaining path length
 * @param component_len Output: length of current component
 * @return Pointer to next component or NULL if this is the last
 */
static const char *jsonk_parse_path_component(const char *path, size_t path_len, size_t *component_len)
{
    const char *dot = memchr(path, '.', path_len);
    if (dot) {
        *component_len = dot - path;
        return dot + 1; /* Skip the dot */
    } else {
        *component_len = path_len;
        return NULL; /* Last component */
    }
}

/**
 * Get a value from a JSON object using a dot-separated path
 */
struct jsonk_value *jsonk_get_value_by_path(struct jsonk_value *root, const char *path, size_t path_len)
{
    struct jsonk_value *curr = root;
    const char *current_path = path;
    size_t remaining_len = path_len;
    
    if (!root || !path || path_len == 0)
        return NULL;
    
    /* Root must be an object */
    if (root->type != JSONK_VALUE_OBJECT)
        return NULL;
    
    while (remaining_len > 0) {
        size_t component_len;
        const char *next_path = jsonk_parse_path_component(current_path, remaining_len, &component_len);
        
        /* Current value must be an object to continue */
        if (curr->type != JSONK_VALUE_OBJECT)
            return NULL;
        
        /* Find the member */
        struct jsonk_member *member = jsonk_object_find_member(&curr->u.object, current_path, component_len);
        if (!member)
            return NULL;
        
        curr = member->value;
        
        /* Move to next component */
        if (next_path) {
            current_path = next_path;
            remaining_len = path_len - (next_path - path);
        } else {
            /* This was the last component */
            break;
        }
    }
    
    return curr;
}

/**
 * Set a value in a JSON object using a dot-separated path
 */
int jsonk_set_value_by_path(struct jsonk_value *root, const char *path, size_t path_len, struct jsonk_value *value)
{
    struct jsonk_value *curr = root;
    const char *current_path = path;
    size_t remaining_len = path_len;
    
    if (!root || !path || path_len == 0 || !value)
        return -EINVAL;
    
    /* Root must be an object */
    if (root->type != JSONK_VALUE_OBJECT)
        return -EINVAL;
    
    while (remaining_len > 0) {
        size_t component_len;
        const char *next_path = jsonk_parse_path_component(current_path, remaining_len, &component_len);
        
        /* Current value must be an object to continue */
        if (curr->type != JSONK_VALUE_OBJECT)
            return -EINVAL;
        
        if (next_path) {
            /* Not the last component - ensure intermediate object exists */
            struct jsonk_member *member = jsonk_object_find_member(&curr->u.object, current_path, component_len);
            if (!member) {
                /* Create intermediate object */
                struct jsonk_value *intermediate = jsonk_value_create(JSONK_VALUE_OBJECT);
                if (!intermediate)
                    return -ENOMEM;
                
                int ret = jsonk_object_add_member_tracked(&curr->u.object, current_path, component_len, intermediate, NULL);
                if (ret < 0) {
                    jsonk_value_put(intermediate);
                    return ret;
                }
                curr = intermediate;
            } else {
                /* Use existing member, but ensure it's an object */
                if (member->value->type != JSONK_VALUE_OBJECT) {
                    /* Replace with empty object */
                    struct jsonk_value *old_value = member->value;
                    struct jsonk_value *new_object = jsonk_value_create(JSONK_VALUE_OBJECT);
                    if (!new_object)
                        return -ENOMEM;
                    
                    member->value = new_object;
                    jsonk_value_put(old_value);
                }
                curr = member->value;
            }
            
            /* Move to next component */
            current_path = next_path;
            remaining_len = path_len - (next_path - path);
        } else {
            /* Last component - set the value */
            struct jsonk_member *member = jsonk_object_find_member(&curr->u.object, current_path, component_len);
            struct jsonk_value *value_copy = jsonk_value_deep_copy(value, 1);
            if (!value_copy)
                return -ENOMEM;
            
            if (member) {
                /* Replace existing value */
                struct jsonk_value *old_value = member->value;
                member->value = value_copy;
                jsonk_value_put(old_value);
            } else {
                /* Add new member */
                int ret = jsonk_object_add_member(&curr->u.object, current_path, component_len, value_copy);
                if (ret < 0) {
                    jsonk_value_put(value_copy);
                    return ret;
                }
            }
            break;
        }
    }
    
    return 0;
}

/**
 * Merge two JSON objects (fail-fast for atomicity)
 */
static int jsonk_merge_objects(struct jsonk_object *target, struct jsonk_object *patch, bool *changed)
{
    struct jsonk_member *member;
    
    *changed = false;
    
    list_for_each_entry(member, &patch->members, list) {
        struct jsonk_member *target_member = jsonk_object_find_member(target, member->key, member->key_len);
        
        /* Check if patch value is empty (should remove the key) */
        bool is_empty = false;
        if (member->value->type == JSONK_VALUE_NULL) {
            is_empty = true;
        } else if (member->value->type == JSONK_VALUE_STRING && member->value->u.string.len == 0) {
            is_empty = true;
        } else if (member->value->type == JSONK_VALUE_OBJECT && member->value->u.object.size == 0) {
            is_empty = true;
        } else if (member->value->type == JSONK_VALUE_ARRAY && member->value->u.array.size == 0) {
            is_empty = true;
        }
        
        if (is_empty) {
            /* Remove the key if it exists in target */
            if (target_member) {
                int ret = jsonk_object_remove_member(target, member->key, member->key_len);
                if (ret < 0)
                    return ret;
                *changed = true;
            }
            continue;
        }
        
        if (!target_member) {
            /* Key doesn't exist in target, add it */
            struct jsonk_value *value_copy = jsonk_value_deep_copy(member->value, 1);
            if (!value_copy)
                return -ENOMEM;
            
            int ret = jsonk_object_add_member(target, member->key, member->key_len, value_copy);
            if (ret < 0) {
                jsonk_value_put(value_copy);
                return ret;
            }
            *changed = true;
        } else {
            /* Key exists in target, update it */
            if (member->value->type == JSONK_VALUE_OBJECT && target_member->value->type == JSONK_VALUE_OBJECT) {
                /* Recursive merge for objects */
                bool sub_changed = false;
                int ret = jsonk_merge_objects(&target_member->value->u.object, &member->value->u.object, &sub_changed);
                if (ret < 0)
                    return ret;
                if (sub_changed)
                    *changed = true;
            } else {
                /* Replace value for non-objects */
                struct jsonk_value *new_value = jsonk_value_deep_copy(member->value, 1);
                if (!new_value)
                    return -ENOMEM;
                
                struct jsonk_value *old_value = target_member->value;
                target_member->value = new_value;
                jsonk_value_put(old_value);
                *changed = true;
            }
        }
    }
    
    return 0;
}

/**
 * Apply a JSON patch to a target buffer (truly atomic)
 */
int jsonk_apply_patch(const char *target, size_t target_len,
                      const char *patch, size_t patch_len,
                      char *result, size_t result_max_len, size_t *result_len)
{
    struct jsonk_value *target_json = NULL;
    struct jsonk_value *patch_json = NULL;
    struct jsonk_value *target_copy = NULL;
    int ret = JSONK_PATCH_ERROR_PARSE;
    bool changed;
    
    /* Parse target JSON */
    target_json = jsonk_parse(target, target_len);
    if (!target_json)
        goto error;
    
    /* Validate target is an object */
    if (target_json->type != JSONK_VALUE_OBJECT) {
        ret = JSONK_PATCH_ERROR_TYPE;
        goto error;
    }
    
    /* Parse patch JSON */
    patch_json = jsonk_parse(patch, patch_len);
    if (!patch_json) {
        /* If patch is invalid, return original JSON unchanged */
        if (target_len <= result_max_len) {
            memcpy(result, target, target_len);
            *result_len = target_len;
            ret = JSONK_PATCH_NO_CHANGE;
        } else {
            ret = JSONK_PATCH_ERROR_OVERFLOW;
        }
        goto error;
    }
    
    /* Validate patch is an object */
    if (patch_json->type != JSONK_VALUE_OBJECT) {
        ret = JSONK_PATCH_ERROR_TYPE;
        goto error;
    }
    
    /* ATOMIC OPERATION: Deep copy target before modification */
    target_copy = jsonk_value_deep_copy(target_json, 0);
    if (!target_copy) {
        ret = JSONK_PATCH_ERROR_MEMORY;
        goto error;
    }
    
    /* Apply patch to the copy - if this fails, original is untouched */
    ret = jsonk_merge_objects(&target_copy->u.object, &patch_json->u.object, &changed);
    if (ret < 0) {
        ret = JSONK_PATCH_ERROR_MEMORY;
        goto error;
    }
    
    /* Serialize result from the successfully patched copy */
    size_t written;
    ret = jsonk_serialize(target_copy, result, result_max_len, &written);
    if (ret < 0) {
        if (ret == -EOVERFLOW)
            ret = JSONK_PATCH_ERROR_OVERFLOW;
        else
            ret = JSONK_PATCH_ERROR_PARSE;
        goto error;
    }
    
    *result_len = written;
    ret = changed ? JSONK_PATCH_SUCCESS : JSONK_PATCH_NO_CHANGE;
    
error:
    if (target_json)
        jsonk_value_put(target_json);
    if (patch_json)
        jsonk_value_put(patch_json);
    if (target_copy)
        jsonk_value_put(target_copy);
    
    return ret;
}



struct jsonk_value *jsonk_value_create(enum jsonk_value_type type)
{
    return jsonk_value_create_tracked(type, NULL);
}

struct jsonk_value *jsonk_value_create_string(const char *str, size_t len)
{
    return jsonk_value_create_string_tracked(str, len, NULL);
}

int jsonk_object_add_member(struct jsonk_object *obj, const char *key, size_t key_len, struct jsonk_value *value)
{
    return jsonk_object_add_member_tracked(obj, key, key_len, value, NULL);
}

int jsonk_array_add_element(struct jsonk_array *arr, struct jsonk_value *value)
{
    return jsonk_array_add_element_tracked(arr, value, NULL);
}

/* ========================================================================
 * Module Initialization and Cleanup
 * ======================================================================== */

static int __init jsonk_init(void)
{
    /* Create slab caches for different object types */
    jsonk_value_cache = kmem_cache_create("jsonk_value",
                                         sizeof(struct jsonk_value),
                                         0, SLAB_HWCACHE_ALIGN, NULL);
    if (!jsonk_value_cache) {
        printk(KERN_ERR "JSONK: Failed to create value cache\n");
        return -ENOMEM;
    }
    
    jsonk_member_cache = kmem_cache_create("jsonk_member",
                                          sizeof(struct jsonk_member),
                                          0, SLAB_HWCACHE_ALIGN, NULL);
    if (!jsonk_member_cache) {
        printk(KERN_ERR "JSONK: Failed to create member cache\n");
        kmem_cache_destroy(jsonk_value_cache);
        return -ENOMEM;
    }
    
    jsonk_element_cache = kmem_cache_create("jsonk_element",
                                           sizeof(struct jsonk_array_element),
                                           0, SLAB_HWCACHE_ALIGN, NULL);
    if (!jsonk_element_cache) {
        printk(KERN_ERR "JSONK: Failed to create element cache\n");
        kmem_cache_destroy(jsonk_value_cache);
        kmem_cache_destroy(jsonk_member_cache);
        return -ENOMEM;
    }
    
    printk(KERN_INFO "JSONK: JSON Library loaded\n");
    return 0;
}

static void __exit jsonk_exit(void)
{
    /* Destroy slab caches */
    if (jsonk_element_cache) {
        kmem_cache_destroy(jsonk_element_cache);
        jsonk_element_cache = NULL;
    }
    
    if (jsonk_member_cache) {
        kmem_cache_destroy(jsonk_member_cache);
        jsonk_member_cache = NULL;
    }
    
    if (jsonk_value_cache) {
        kmem_cache_destroy(jsonk_value_cache);
        jsonk_value_cache = NULL;
    }
    
    printk(KERN_INFO "JSONK: JSON Library unloaded\n");
}

module_init(jsonk_init);
module_exit(jsonk_exit);

/* ========================================================================
 * Module Export Symbols
 * ======================================================================== */

EXPORT_SYMBOL(jsonk_parse);
EXPORT_SYMBOL(jsonk_serialize);
EXPORT_SYMBOL(jsonk_value_get);
EXPORT_SYMBOL(jsonk_value_put);
EXPORT_SYMBOL(jsonk_apply_patch);
EXPORT_SYMBOL(jsonk_value_create);
EXPORT_SYMBOL(jsonk_value_create_string);
EXPORT_SYMBOL(jsonk_value_create_number);
EXPORT_SYMBOL(jsonk_value_create_boolean);
EXPORT_SYMBOL(jsonk_value_create_null);
EXPORT_SYMBOL(jsonk_value_deep_copy);
EXPORT_SYMBOL(jsonk_object_add_member);
EXPORT_SYMBOL(jsonk_object_find_member);
EXPORT_SYMBOL(jsonk_object_remove_member);
EXPORT_SYMBOL(jsonk_array_add_element);
EXPORT_SYMBOL(jsonk_get_value_by_path);
EXPORT_SYMBOL(jsonk_set_value_by_path);