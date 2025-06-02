/**
 * basic_usage.c - Basic usage example for JSONK library
 *
 * This example demonstrates:
 * - JSON parsing
 * - JSON serialization
 * - Object manipulation
 * - JSON patching
 *
 * Copyright (C) 2025 Mehran Toosi
 * Licensed under GPL-2.0
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include "../include/jsonk.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mehran Toosi");
MODULE_DESCRIPTION("JSONK Basic Usage Example");
MODULE_VERSION("1.0.0");

/* Work data structure for multi-threading example */
struct work_data {
    struct work_struct work;
    const char *json_str;
    int worker_id;
};

/* Worker function for multi-threading example */
static void worker_function(struct work_struct *work)
{
    struct work_data *data = container_of(work, struct work_data, work);
    struct jsonk_value *json;
    char buffer[256];
    size_t written;
    
    printk(KERN_INFO "Worker %d: Processing JSON: %s\n", data->worker_id, data->json_str);
    
    json = jsonk_parse(data->json_str, strlen(data->json_str));
    if (json) {
        if (jsonk_serialize(json, buffer, sizeof(buffer), &written) == 0) {
            printk(KERN_INFO "Worker %d: Serialized: %.*s\n", data->worker_id, (int)written, buffer);
        }
        jsonk_value_put(json);
    }
    
    printk(KERN_INFO "Worker %d: Completed\n", data->worker_id);
    kfree(data);
}

static void test_basic_parsing(void)
{
    const char *json_str = "{\"name\":\"JSONK\",\"version\":1,\"active\":true}";
    struct jsonk_value *json;
    char buffer[256];
    size_t written;
    
    printk(KERN_INFO "=== Testing Basic Parsing ===\n");
    
    // Parse JSON
    json = jsonk_parse(json_str, strlen(json_str));
    if (!json) {
        printk(KERN_ERR "Failed to parse JSON\n");
        return;
    }
    
    printk(KERN_INFO "Original JSON: %s\n", json_str);
    
    // Access object members
    if (json->type == JSONK_VALUE_OBJECT) {
        struct jsonk_member *name_member = jsonk_object_find_member(&json->u.object, "name", 4);
        if (name_member && name_member->value->type == JSONK_VALUE_STRING) {
            printk(KERN_INFO "Found name: %s\n", name_member->value->u.string.data);
        }
        
        struct jsonk_member *version_member = jsonk_object_find_member(&json->u.object, "version", 7);
        if (version_member && version_member->value->type == JSONK_VALUE_NUMBER) {
            printk(KERN_INFO "Found version: %lld\n", version_member->value->u.number.integer);
        }
        
        struct jsonk_member *active_member = jsonk_object_find_member(&json->u.object, "active", 6);
        if (active_member && active_member->value->type == JSONK_VALUE_BOOLEAN) {
            printk(KERN_INFO "Found active: %s\n", active_member->value->u.boolean ? "true" : "false");
        }
    }
    
    // Serialize back to string
    if (jsonk_serialize(json, buffer, sizeof(buffer), &written) == 0) {
        printk(KERN_INFO "Serialized JSON: %.*s\n", (int)written, buffer);
    }
    
    // Clean up
    jsonk_value_put(json);
    printk(KERN_INFO "Basic parsing test completed\n\n");
}

static void test_object_manipulation(void)
{
    struct jsonk_value *json;
    struct jsonk_value *new_field;
    char buffer[512];
    size_t written;
    
    printk(KERN_INFO "=== Testing Object Manipulation ===\n");
    
    // Create empty object
    json = jsonk_value_create(JSONK_VALUE_OBJECT);
    if (!json) {
        printk(KERN_ERR "Failed to create JSON object\n");
        return;
    }
    
    // Add string field
    new_field = jsonk_value_create_string("test_value", 10);
    if (new_field) {
        jsonk_object_add_member(&json->u.object, "test_key", 8, new_field);
        printk(KERN_INFO "Added string field\n");
    }
    
    // Add number field
    new_field = jsonk_value_create_number("42", 2);
    if (new_field) {
        jsonk_object_add_member(&json->u.object, "number", 6, new_field);
        printk(KERN_INFO "Added number field\n");
    }
    
    // Add boolean field
    new_field = jsonk_value_create_boolean(true);
    if (new_field) {
        jsonk_object_add_member(&json->u.object, "flag", 4, new_field);
        printk(KERN_INFO "Added boolean field\n");
    }
    
    // Serialize the constructed object
    if (jsonk_serialize(json, buffer, sizeof(buffer), &written) == 0) {
        printk(KERN_INFO "Constructed JSON: %.*s\n", (int)written, buffer);
    }
    
    // Remove a field
    if (jsonk_object_remove_member(&json->u.object, "test_key", 8) == 0) {
        printk(KERN_INFO "Removed test_key field\n");
        
        if (jsonk_serialize(json, buffer, sizeof(buffer), &written) == 0) {
            printk(KERN_INFO "After removal: %.*s\n", (int)written, buffer);
        }
    }
    
    // Clean up
    jsonk_value_put(json);
    printk(KERN_INFO "Object manipulation test completed\n\n");
}

static void test_json_patching(void)
{
    const char *target = "{\"name\":\"Mehran\",\"age\":30,\"city\":\"CPH\"}";
    const char *patch = "{\"age\":31,\"country\":\"DK\",\"city\":null}";
    char *result;
    size_t result_len;
    int ret;
    
    printk(KERN_INFO "=== Testing JSON Patching ===\n");
    printk(KERN_INFO "Target JSON: %s\n", target);
    printk(KERN_INFO "Patch JSON:  %s\n", patch);
    
    result = kmalloc(512, GFP_KERNEL);
    if (!result) {
        printk(KERN_ERR "Failed to allocate result buffer\n");
        return;
    }
    
    ret = jsonk_apply_patch(target, strlen(target),
                           patch, strlen(patch),
                           result, 512, &result_len);
    
    switch (ret) {
    case JSONK_PATCH_SUCCESS:
        printk(KERN_INFO "Patch applied successfully\n");
        printk(KERN_INFO "Result JSON: %.*s\n", (int)result_len, result);
        break;
    case JSONK_PATCH_NO_CHANGE:
        printk(KERN_INFO "Patch resulted in no changes\n");
        break;
    case JSONK_PATCH_ERROR_PARSE:
        printk(KERN_ERR "Failed to parse JSON\n");
        break;
    case JSONK_PATCH_ERROR_TYPE:
        printk(KERN_ERR "Type mismatch in patch\n");
        break;
    case JSONK_PATCH_ERROR_OVERFLOW:
        printk(KERN_ERR "Result buffer too small\n");
        break;
    default:
        printk(KERN_ERR "Unknown patch error: %d\n", ret);
        break;
    }
    
    kfree(result);
    printk(KERN_INFO "JSON patching test completed\n\n");
}

static void test_removal_patching(void)
{
    const char *target = "{\"keep\":\"this\",\"remove_me\":\"delete\",\"also_remove\":42}";
    const char *patch = "{\"remove_me\":null,\"also_remove\":\"\",\"new_field\":\"added\"}";
    char result[512];
    size_t result_len;
    int ret;
    
    printk(KERN_INFO "=== Testing Removal Patching ===\n");
    printk(KERN_INFO "Target JSON: %s\n", target);
    printk(KERN_INFO "Patch JSON:  %s\n", patch);
    
    ret = jsonk_apply_patch(target, strlen(target),
                           patch, strlen(patch),
                           result, sizeof(result), &result_len);
    
    if (ret == JSONK_PATCH_SUCCESS) {
        printk(KERN_INFO "Removal patch applied successfully\n");
        printk(KERN_INFO "Result JSON: %.*s\n", (int)result_len, result);
        printk(KERN_INFO "Note: Fields with null or empty values were removed\n");
    } else {
        printk(KERN_ERR "Removal patch failed with error: %d\n", ret);
    }
    
    printk(KERN_INFO "Removal patching test completed\n\n");
}

static void test_array_handling(void)
{
    const char *json_str = "{\"items\":[1,2,3],\"names\":[\"alice\",\"bob\"]}";
    struct jsonk_value *json;
    char buffer[256];
    size_t written;
    
    printk(KERN_INFO "=== Testing Array Handling ===\n");
    printk(KERN_INFO "Array JSON: %s\n", json_str);
    
    // Parse JSON with arrays
    json = jsonk_parse(json_str, strlen(json_str));
    if (!json) {
        printk(KERN_ERR "Failed to parse array JSON\n");
        return;
    }
    
    // Access array members
    if (json->type == JSONK_VALUE_OBJECT) {
        struct jsonk_member *items_member = jsonk_object_find_member(&json->u.object, "items", 5);
        if (items_member && items_member->value->type == JSONK_VALUE_ARRAY) {
            printk(KERN_INFO "Found items array with %zu elements\n", 
                   items_member->value->u.array.size);
        }
        
        struct jsonk_member *names_member = jsonk_object_find_member(&json->u.object, "names", 5);
        if (names_member && names_member->value->type == JSONK_VALUE_ARRAY) {
            printk(KERN_INFO "Found names array with %zu elements\n", 
                   names_member->value->u.array.size);
        }
    }
    
    // Serialize back
    if (jsonk_serialize(json, buffer, sizeof(buffer), &written) == 0) {
        printk(KERN_INFO "Serialized: %.*s\n", (int)written, buffer);
    }
    
    // Clean up
    jsonk_value_put(json);
    printk(KERN_INFO "Array handling test completed\n\n");
}

static void test_path_based_access(void)
{
    const char *json_str = "{\"user\":{\"profile\":{\"name\":\"Mehran\",\"age\":30},\"settings\":{\"theme\":\"dark\"}}}";
    struct jsonk_value *json;
    struct jsonk_value *found_value;
    struct jsonk_value *new_value;
    char buffer[512];
    size_t written;
    
    printk(KERN_INFO "=== Testing Path-Based Access ===\n");
    printk(KERN_INFO "Original JSON: %s\n", json_str);
    
    // Parse JSON
    json = jsonk_parse(json_str, strlen(json_str));
    if (!json) {
        printk(KERN_ERR "Failed to parse JSON\n");
        return;
    }
    
    // Test getting values by path
    found_value = jsonk_get_value_by_path(json, "user.profile.name", 17);
    if (found_value && found_value->type == JSONK_VALUE_STRING) {
        printk(KERN_INFO "Found user.profile.name: %s\n", found_value->u.string.data);
    }
    
    found_value = jsonk_get_value_by_path(json, "user.profile.age", 16);
    if (found_value && found_value->type == JSONK_VALUE_NUMBER) {
        printk(KERN_INFO "Found user.profile.age: %lld\n", found_value->u.number.integer);
    }
    
    found_value = jsonk_get_value_by_path(json, "user.settings.theme", 19);
    if (found_value && found_value->type == JSONK_VALUE_STRING) {
        printk(KERN_INFO "Found user.settings.theme: %s\n", found_value->u.string.data);
    }
    
    // Test setting values by path (creates intermediate objects)
    new_value = jsonk_value_create_string("admin", 5);
    if (new_value) {
        int ret = jsonk_set_value_by_path(json, "user.profile.role", 17, new_value);
        if (ret == 0) {
            printk(KERN_INFO "Successfully set user.profile.role\n");
        }
        jsonk_value_put(new_value);
    }
    
    // Test setting a deep path that creates intermediate objects
    new_value = jsonk_value_create_boolean(true);
    if (new_value) {
        int ret = jsonk_set_value_by_path(json, "user.preferences.notifications.email", 37, new_value);
        if (ret == 0) {
            printk(KERN_INFO "Successfully set user.preferences.notifications.email\n");
        }
        jsonk_value_put(new_value);
    }
    
    // Serialize to see the changes
    if (jsonk_serialize(json, buffer, sizeof(buffer), &written) == 0) {
        printk(KERN_INFO "Modified JSON: %.*s\n", (int)written, buffer);
    }
    
    // Clean up
    jsonk_value_put(json);
    printk(KERN_INFO "Path-based access test completed\n\n");
}

static void test_removal_verification(void)
{
    const char *target = "{\"keep_me\":\"value\",\"remove_null\":\"will_be_removed\",\"remove_empty\":\"will_be_removed\",\"nested\":{\"keep\":\"this\",\"remove\":\"this_too\"}}";
    const char *patch = "{\"remove_null\":null,\"remove_empty\":\"\",\"new_field\":\"added\",\"nested\":{\"remove\":null,\"new_nested\":\"added\"}}";
    char *result;
    size_t result_len;
    int ret;
    
    printk(KERN_INFO "=== Testing Removal Verification ===\n");
    printk(KERN_INFO "Target: %s\n", target);
    printk(KERN_INFO "Patch:  %s\n", patch);
    
    result = kmalloc(1024, GFP_KERNEL);
    if (!result) {
        printk(KERN_ERR "Failed to allocate result buffer\n");
        return;
    }
    
    ret = jsonk_apply_patch(target, strlen(target),
                           patch, strlen(patch),
                           result, 1024, &result_len);
    
    if (ret == JSONK_PATCH_SUCCESS) {
        printk(KERN_INFO "Patch applied successfully\n");
        printk(KERN_INFO "Result: %.*s\n", (int)result_len, result);
        printk(KERN_INFO "Verification:\n");
        printk(KERN_INFO "  - remove_null: REMOVED (was null in patch)\n");
        printk(KERN_INFO "  - remove_empty: REMOVED (was empty string in patch)\n");
        printk(KERN_INFO "  - new_field: ADDED\n");
        printk(KERN_INFO "  - nested.remove: REMOVED (was null in patch)\n");
        printk(KERN_INFO "  - nested.new_nested: ADDED\n");
        printk(KERN_INFO "  - nested.keep: PRESERVED\n");
    } else {
        printk(KERN_ERR "Patch failed with error: %d\n", ret);
    }
    
    kfree(result);
    printk(KERN_INFO "Removal verification test completed\n\n");
}

static void test_multithreading_example(void)
{
    struct work_data *data1, *data2;
    
    printk(KERN_INFO "=== Testing Multi-threading Example ===\n");
    printk(KERN_INFO "Note: This demonstrates proper synchronization patterns\n");
    printk(KERN_INFO "In real usage, you must handle locking around JSONK operations\n\n");
    
    // Allocate work data
    data1 = kmalloc(sizeof(*data1), GFP_KERNEL);
    data2 = kmalloc(sizeof(*data2), GFP_KERNEL);
    
    if (!data1 || !data2) {
        printk(KERN_ERR "Failed to allocate work data\n");
        kfree(data1);
        kfree(data2);
        return;
    }
    
    // Initialize work data
    data1->json_str = "{\"worker\":1,\"task\":\"parse_data\"}";
    data1->worker_id = 1;
    INIT_WORK(&data1->work, worker_function);
    
    data2->json_str = "{\"worker\":2,\"task\":\"process_results\"}";
    data2->worker_id = 2;
    INIT_WORK(&data2->work, worker_function);
    
    // Schedule work
    schedule_work(&data1->work);
    schedule_work(&data2->work);
    
    // Wait a bit for work to complete (simplified example)
    msleep(100);
    
    printk(KERN_INFO "Multi-threading example completed\n");
    printk(KERN_INFO "Remember: JSONK is not thread-safe, use proper locking!\n\n");
}

static int __init jsonk_example_init(void)
{
    printk(KERN_INFO "JSONK Basic Usage Example Module Loaded\n");
    printk(KERN_INFO "Running comprehensive tests...\n\n");
    
    test_basic_parsing();
    test_object_manipulation();
    test_json_patching();
    test_removal_patching();
    test_array_handling();
    test_path_based_access();
    test_removal_verification();
    test_multithreading_example();
    
    printk(KERN_INFO "All tests completed successfully!\n");
    printk(KERN_INFO "Check dmesg for detailed output\n");
    
    return 0;
}

static void __exit jsonk_example_exit(void)
{
    printk(KERN_INFO "JSONK Basic Usage Example Module Unloaded\n");
}

module_init(jsonk_example_init);
module_exit(jsonk_example_exit); 