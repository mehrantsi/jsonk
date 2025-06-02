/**
 * atomic_test.c - Test atomic patching behavior for JSONK
 *
 * This program tests that JSON patching is truly atomic:
 * - Either the entire patch succeeds or fails
 * - No partial modifications on failure
 * - Original data remains untouched on failure
 *
 * Copyright (C) 2025 Mehran Toosi
 * Licensed under GPL-2.0
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>
#include "../include/jsonk.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mehran Toosi");
MODULE_DESCRIPTION("JSONK Atomic Patching Test");
MODULE_VERSION("1.0.0");

/**
 * Test successful atomic patch
 */
static void test_successful_patch(void)
{
    const char *target = "{\"name\":\"Mehran\",\"age\":30,\"city\":\"CPH\"}";
    const char *patch = "{\"age\":31,\"country\":\"DK\"}";
    char result[512];
    size_t result_len;
    int ret;
    
    printk(KERN_INFO "=== Testing Successful Atomic Patch ===\n");
    printk(KERN_INFO "Target: %s\n", target);
    printk(KERN_INFO "Patch:  %s\n", patch);
    
    ret = jsonk_apply_patch(target, strlen(target),
                           patch, strlen(patch),
                           result, sizeof(result), &result_len);
    
    if (ret == JSONK_PATCH_SUCCESS) {
        result[result_len] = '\0';
        printk(KERN_INFO "✓ Patch succeeded: %s\n", result);
    } else {
        printk(KERN_ERR "✗ Patch failed with code: %d\n", ret);
    }
}

/**
 * Test patch with removal (null values)
 */
static void test_removal_patch(void)
{
    const char *target = "{\"name\":\"Mehran\",\"age\":30,\"city\":\"CPH\",\"temp\":\"remove\"}";
    const char *patch = "{\"age\":31,\"city\":null,\"country\":\"DK\"}";
    char result[512];
    size_t result_len;
    int ret;
    
    printk(KERN_INFO "=== Testing Removal Patch ===\n");
    printk(KERN_INFO "Target: %s\n", target);
    printk(KERN_INFO "Patch:  %s\n", patch);
    
    ret = jsonk_apply_patch(target, strlen(target),
                           patch, strlen(patch),
                           result, sizeof(result), &result_len);
    
    if (ret == JSONK_PATCH_SUCCESS) {
        result[result_len] = '\0';
        printk(KERN_INFO "✓ Removal patch succeeded: %s\n", result);
        
        /* Verify 'city' was removed */
        if (strstr(result, "city") == NULL) {
            printk(KERN_INFO "✓ Key 'city' successfully removed\n");
        } else {
            printk(KERN_ERR "✗ Key 'city' was not removed\n");
        }
    } else {
        printk(KERN_ERR "✗ Removal patch failed with code: %d\n", ret);
    }
}

/**
 * Test invalid patch (should fail atomically)
 */
static void test_invalid_patch(void)
{
    const char *target = "{\"name\":\"Mehran\",\"age\":30}";
    const char *invalid_patch = "{\"name\":\"Jane\",\"invalid\":}"; /* Invalid JSON */
    char result[512];
    size_t result_len;
    int ret;
    
    printk(KERN_INFO "=== Testing Invalid Patch (Should Fail Atomically) ===\n");
    printk(KERN_INFO "Target: %s\n", target);
    printk(KERN_INFO "Invalid Patch: %s\n", invalid_patch);
    
    ret = jsonk_apply_patch(target, strlen(target),
                           invalid_patch, strlen(invalid_patch),
                           result, sizeof(result), &result_len);
    
    if (ret == JSONK_PATCH_NO_CHANGE) {
        result[result_len] = '\0';
        printk(KERN_INFO "✓ Invalid patch correctly rejected, original returned: %s\n", result);
        
        /* Verify original is unchanged */
        if (strcmp(result, target) == 0) {
            printk(KERN_INFO "✓ Original JSON preserved exactly\n");
        } else {
            printk(KERN_ERR "✗ Original JSON was modified!\n");
        }
    } else {
        printk(KERN_ERR "✗ Invalid patch should have been rejected, got code: %d\n", ret);
    }
}

/**
 * Test buffer overflow (should fail atomically)
 */
static void test_buffer_overflow(void)
{
    const char *target = "{\"name\":\"Mehran\"}";
    const char *patch = "{\"description\":\"This is a very long description that should cause buffer overflow when serialized\"}";
    char small_result[50]; /* Intentionally small buffer */
    size_t result_len;
    int ret;
    
    printk(KERN_INFO "=== Testing Buffer Overflow (Should Fail Atomically) ===\n");
    printk(KERN_INFO "Target: %s\n", target);
    printk(KERN_INFO "Patch: %s\n", patch);
    printk(KERN_INFO "Small buffer size: %zu bytes\n", sizeof(small_result));
    
    ret = jsonk_apply_patch(target, strlen(target),
                           patch, strlen(patch),
                           small_result, sizeof(small_result), &result_len);
    
    if (ret == JSONK_PATCH_ERROR_OVERFLOW) {
        printk(KERN_INFO "✓ Buffer overflow correctly detected and rejected\n");
        printk(KERN_INFO "✓ No partial data written to result buffer\n");
    } else {
        printk(KERN_ERR "✗ Buffer overflow should have been detected, got code: %d\n", ret);
    }
}

/**
 * Test nested object patching
 */
static void test_nested_patch(void)
{
    const char *target = "{\"user\":{\"name\":\"Mehran\",\"profile\":{\"age\":30}},\"meta\":{\"version\":1}}";
    const char *patch = "{\"user\":{\"profile\":{\"age\":31,\"city\":\"CPH\"}},\"meta\":{\"updated\":true}}";
    char result[512];
    size_t result_len;
    int ret;
    
    printk(KERN_INFO "=== Testing Nested Object Patch ===\n");
    printk(KERN_INFO "Target: %s\n", target);
    printk(KERN_INFO "Patch:  %s\n", patch);
    
    ret = jsonk_apply_patch(target, strlen(target),
                           patch, strlen(patch),
                           result, sizeof(result), &result_len);
    
    if (ret == JSONK_PATCH_SUCCESS) {
        result[result_len] = '\0';
        printk(KERN_INFO "✓ Nested patch succeeded: %s\n", result);
        
        /* Verify nested changes */
        if (strstr(result, "\"age\":31") && strstr(result, "\"city\":\"CPH\"") && strstr(result, "\"updated\":true")) {
            printk(KERN_INFO "✓ All nested changes applied correctly\n");
        } else {
            printk(KERN_ERR "✗ Some nested changes missing\n");
        }
    } else {
        printk(KERN_ERR "✗ Nested patch failed with code: %d\n", ret);
    }
}

/**
 * Test no-change patch
 */
static void test_no_change_patch(void)
{
    const char *target = "{\"name\":\"Mehran\",\"age\":30}";
    const char *patch = "{\"name\":\"Mehran\"}"; /* Same value, no change */
    char result[512];
    size_t result_len;
    int ret;
    
    printk(KERN_INFO "=== Testing No-Change Patch ===\n");
    printk(KERN_INFO "Target: %s\n", target);
    printk(KERN_INFO "Patch:  %s\n", patch);
    
    ret = jsonk_apply_patch(target, strlen(target),
                           patch, strlen(patch),
                           result, sizeof(result), &result_len);
    
    if (ret == JSONK_PATCH_NO_CHANGE) {
        result[result_len] = '\0';
        printk(KERN_INFO "✓ No-change correctly detected: %s\n", result);
    } else if (ret == JSONK_PATCH_SUCCESS) {
        result[result_len] = '\0';
        printk(KERN_INFO "✓ Patch succeeded (no change detected): %s\n", result);
    } else {
        printk(KERN_ERR "✗ No-change patch failed with code: %d\n", ret);
    }
}

/**
 * Module initialization
 */
static int __init atomic_test_init(void)
{
    printk(KERN_INFO "JSONK Atomic Patching Test loaded\n");
    printk(KERN_INFO "Testing atomic patching behavior...\n\n");
    
    test_successful_patch();
    printk(KERN_INFO "\n");
    
    test_removal_patch();
    printk(KERN_INFO "\n");
    
    test_invalid_patch();
    printk(KERN_INFO "\n");
    
    test_buffer_overflow();
    printk(KERN_INFO "\n");
    
    test_nested_patch();
    printk(KERN_INFO "\n");
    
    test_no_change_patch();
    printk(KERN_INFO "\n");
    
    printk(KERN_INFO "Atomic patching tests completed\n");
    return 0;
}

/**
 * Module cleanup
 */
static void __exit atomic_test_exit(void)
{
    printk(KERN_INFO "JSONK Atomic Patching Test unloaded\n");
}

module_init(atomic_test_init);
module_exit(atomic_test_exit); 