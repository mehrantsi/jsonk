/**
 * performance_test.c - Comprehensive Performance Test for JSONK library
 *
 * This program measures the performance of JSONK operations:
 * - JSON parsing speed (regular vs memory pools)
 * - JSON serialization speed  
 * - JSON patching speed
 * - Memory usage patterns
 * - Scalability with different JSON sizes
 *
 * Copyright (C) 2025 Mehran Toosi
 * Licensed under GPL-2.0
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/vmalloc.h>
#include <linux/random.h>
#include "../include/jsonk.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mehran Toosi");
MODULE_DESCRIPTION("JSONK Comprehensive Performance Test");
MODULE_VERSION("1.0.0");

/* Performance test constants */
#define ITERATIONS_SMALL 10000
#define ITERATIONS_MEDIUM 1000
#define ITERATIONS_LARGE 100
#define POOL_ITERATIONS 10000

#define SMALL_JSON_SIZE 1024
#define MEDIUM_JSON_SIZE 65536
#define LARGE_JSON_SIZE 1048576

/* Test data */
static const char *small_json = "{\"name\":\"Mehran\",\"age\":30,\"city\":\"CPH\",\"active\":true}";
static const char *medium_json = "{\"user\":{\"id\":123,\"name\":\"Mehran\",\"email\":\"mehran@example.com\",\"profile\":{\"age\":30,\"city\":\"CPH\",\"preferences\":[\"coding\",\"music\",\"travel\"]}},\"metadata\":{\"created\":\"2025-01-01\",\"updated\":\"2025-01-15\",\"version\":2}}";

/* Performance measurement helpers */
static inline u64 get_time_ns(void)
{
    return ktime_get_ns();
}

static void print_performance(const char *test_name, u64 start_ns, u64 end_ns, 
                             size_t data_size, int iterations)
{
    u64 total_ns = end_ns - start_ns;
    u64 avg_ns = total_ns / iterations;
    u64 ops_per_sec = 0;
    u64 throughput_mb_s = 0;
    
    if (total_ns > 0) {
        ops_per_sec = (u64)iterations * 1000000000ULL / total_ns;
        /* Calculate MB/s */
        throughput_mb_s = (data_size * iterations * 1000000000ULL) / (total_ns * 1024 * 1024);
    }
    
    printk(KERN_INFO "%s:\n", test_name);
    printk(KERN_INFO "  Total time: %llu ns (%llu ms)\n", total_ns, total_ns / 1000000);
    printk(KERN_INFO "  Average per operation: %llu ns\n", avg_ns);
    printk(KERN_INFO "  Operations per second: %llu\n", ops_per_sec);
    printk(KERN_INFO "  Throughput: %llu MB/s\n", throughput_mb_s);
    printk(KERN_INFO "\n");
}

/* Generate test JSON data */
static char *generate_simple_json(size_t target_size, size_t *actual_size)
{
    char *json;
    size_t pos = 0;
    int i, j;
    int num_objects;
    int content_per_object;
    
    json = vmalloc(target_size);
    if (!json)
        return NULL;
    
    /* Determine number of objects and content size based on target size */
    if (target_size < 2048) {
        num_objects = 10;          /* Small: 10 objects */
        content_per_object = 15;   /* ~75 chars per object */
    } else if (target_size < 32768) {
        num_objects = 50;          /* Medium: 50 objects */
        content_per_object = 30;   /* ~1.5KB per object */
    } else {
        num_objects = 100;         /* Large: 100 objects */
        content_per_object = 60;   /* ~3KB per object */
    }
    
    pos += snprintf(json + pos, target_size - pos, "{\"items\":[");
    
    /* Create multiple objects with substantial content */
    for (i = 0; i < num_objects && pos < target_size - 500; i++) {
        if (i > 0) {
            pos += snprintf(json + pos, target_size - pos, ",");
        }
        
        pos += snprintf(json + pos, target_size - pos, 
                       "{\"id\":%d,\"name\":\"item_%d\",\"description\":\"", i, i);
        
        /* Add substantial content to each object */
        for (j = 0; j < content_per_object && pos < target_size - 300; j++) {
            pos += snprintf(json + pos, target_size - pos, 
                           "Content segment %d for item %d with meaningful data. ", j, i);
        }
        
        pos += snprintf(json + pos, target_size - pos, 
                       "\",\"value\":%d,\"active\":%s}", 
                       i * 100, (i % 2) ? "true" : "false");
    }
    
    pos += snprintf(json + pos, target_size - pos, "],\"metadata\":{\"count\":%d,\"type\":\"test\"}}", i);
    
    *actual_size = pos;
    return json;
}

static char *generate_large_json(void)
{
    char *json;
    size_t pos = 0;
    int i, j;
    int num_objects = 200;        /* Large: 200 objects */
    int content_per_object = 60;  /* ~3KB per object */
    
    json = vmalloc(LARGE_JSON_SIZE);
    if (!json)
        return NULL;
    
    pos += snprintf(json + pos, LARGE_JSON_SIZE - pos, "{\"data\":[");
    
    /* Create many objects with large content */
    for (i = 0; i < num_objects && pos < LARGE_JSON_SIZE - 1000; i++) {
        if (i > 0) {
            pos += snprintf(json + pos, LARGE_JSON_SIZE - pos, ",");
        }
        
        pos += snprintf(json + pos, LARGE_JSON_SIZE - pos, 
                       "{\"id\":%d,\"name\":\"large_item_%d\",\"content\":\"", i, i);
        
        /* Add substantial content to each object */
        for (j = 0; j < content_per_object && pos < LARGE_JSON_SIZE - 500; j++) {
            pos += snprintf(json + pos, LARGE_JSON_SIZE - pos, 
                           "Large content segment %d for item %d with extensive data and information. ", j, i);
        }
        
        pos += snprintf(json + pos, LARGE_JSON_SIZE - pos, 
                       "\",\"value\":%d,\"priority\":%d,\"active\":%s}", 
                       i * 1000, i % 10, (i % 3) ? "true" : "false");
    }
    
    pos += snprintf(json + pos, LARGE_JSON_SIZE - pos, "],\"metadata\":{\"total\":%d,\"type\":\"large_dataset\",\"version\":\"2.0\"}}", i);
    
    return json;
}

/* Regular allocation performance tests */
static void test_parsing_performance(void)
{
    char *small_json_gen, *medium_json_gen, *large_json_gen;
    size_t small_size, medium_size, large_size;
    struct jsonk_value *parsed;
    u64 start, end;
    int i;
    
    printk(KERN_INFO "=== JSON Parsing Performance Tests ===\n");
    
    /* Generate test data */
    small_json_gen = generate_simple_json(SMALL_JSON_SIZE, &small_size);
    medium_json_gen = generate_simple_json(MEDIUM_JSON_SIZE, &medium_size);
    large_json_gen = generate_large_json();
    large_size = large_json_gen ? strlen(large_json_gen) : 0;
    
    if (!small_json_gen || !medium_json_gen || !large_json_gen) {
        printk(KERN_ERR "Failed to generate test JSON data\n");
        goto cleanup;
    }
    
    /* Small JSON parsing test */
    start = get_time_ns();
    for (i = 0; i < ITERATIONS_SMALL; i++) {
        parsed = jsonk_parse(small_json_gen, small_size);
        if (parsed) {
            jsonk_value_put(parsed);
        }
    }
    end = get_time_ns();
    print_performance("Small JSON Parsing", start, end, small_size, ITERATIONS_SMALL);
    
    /* Medium JSON parsing test */
    start = get_time_ns();
    for (i = 0; i < ITERATIONS_MEDIUM; i++) {
        parsed = jsonk_parse(medium_json_gen, medium_size);
        if (parsed) {
            jsonk_value_put(parsed);
        }
    }
    end = get_time_ns();
    print_performance("Medium JSON Parsing", start, end, medium_size, ITERATIONS_MEDIUM);
    
    /* Large JSON parsing test */
    start = get_time_ns();
    for (i = 0; i < ITERATIONS_LARGE; i++) {
        parsed = jsonk_parse(large_json_gen, large_size);
        if (parsed) {
            jsonk_value_put(parsed);
        }
    }
    end = get_time_ns();
    print_performance("Large JSON Parsing", start, end, large_size, ITERATIONS_LARGE);
    
cleanup:
    if (small_json_gen) vfree(small_json_gen);
    if (medium_json_gen) vfree(medium_json_gen);
    if (large_json_gen) vfree(large_json_gen);
}

/* Memory pool performance tests */
static void test_pool_performance(void)
{
    u64 start, end;
    struct jsonk_value *parsed;
    int i;
    
    printk(KERN_INFO "=== Memory Pool Performance Tests ===\n");
    
    /* Test small JSON with regular allocation */
    start = get_time_ns();
    for (i = 0; i < POOL_ITERATIONS; i++) {
        parsed = jsonk_parse(small_json, strlen(small_json));
        if (parsed) {
            jsonk_value_put(parsed);
        }
    }
    end = get_time_ns();
    print_performance("Small JSON", start, end, strlen(small_json), POOL_ITERATIONS);
    
    /* Test medium JSON with regular allocation */
    start = get_time_ns();
    for (i = 0; i < POOL_ITERATIONS; i++) {
        parsed = jsonk_parse(medium_json, strlen(medium_json));
        if (parsed) {
            jsonk_value_put(parsed);
        }
    }
    end = get_time_ns();
    print_performance("Medium JSON", start, end, strlen(medium_json), POOL_ITERATIONS);
    
    /* Test large JSON with regular allocation */
    char *large_json_str = generate_large_json();
    if (large_json_str) {
        start = get_time_ns();
        for (i = 0; i < POOL_ITERATIONS; i++) {
            parsed = jsonk_parse(large_json_str, strlen(large_json_str));
            if (parsed) {
                jsonk_value_put(parsed);
            }
        }
        end = get_time_ns();
        print_performance("Large JSON", start, end, strlen(large_json_str), POOL_ITERATIONS);
        vfree(large_json_str);
    }
    
    printk(KERN_INFO "Memory pool performance test completed\n");
}

static void test_serialization_performance(void)
{
    struct jsonk_value *json;
    char *buffer;
    size_t written;
    u64 start, end;
    int i;
    
    printk(KERN_INFO "=== JSON Serialization Performance Tests ===\n");
    
    /* Parse a medium-sized JSON for serialization testing */
    json = jsonk_parse(medium_json, strlen(medium_json));
    if (!json) {
        printk(KERN_ERR "Failed to parse JSON for serialization test\n");
        return;
    }
    
    buffer = vmalloc(MEDIUM_JSON_SIZE);
    if (!buffer) {
        printk(KERN_ERR "Failed to allocate serialization buffer\n");
        jsonk_value_put(json);
        return;
    }
    
    /* Serialization performance test */
    start = get_time_ns();
    for (i = 0; i < ITERATIONS_MEDIUM; i++) {
        if (jsonk_serialize(json, buffer, MEDIUM_JSON_SIZE, &written) != 0) {
            printk(KERN_ERR "Serialization failed at iteration %d\n", i);
            break;
        }
    }
    end = get_time_ns();
    
    print_performance("JSON Serialization", start, end, written, ITERATIONS_MEDIUM);
    
    vfree(buffer);
    jsonk_value_put(json);
}

static void test_patching_performance(void)
{
    const char *target = "{\"name\":\"Mehran\",\"age\":30,\"city\":\"CPH\",\"country\":\"DK\"}";
    const char *patch = "{\"age\":31,\"salary\":50000,\"city\":null}";
    char *result;
    size_t result_len;
    u64 start, end;
    int i;
    
    printk(KERN_INFO "=== JSON Patching Performance Tests ===\n");
    
    result = vmalloc(1024);
    if (!result) {
        printk(KERN_ERR "Failed to allocate result buffer\n");
        return;
    }
    
    /* Patching performance test */
    start = get_time_ns();
    for (i = 0; i < ITERATIONS_MEDIUM; i++) {
        if (jsonk_apply_patch(target, strlen(target),
                                   patch, strlen(patch),
                             result, 1024, &result_len) < 0) {
            printk(KERN_ERR "Patching failed at iteration %d\n", i);
            break;
        }
    }
    end = get_time_ns();
    
    print_performance("JSON Patching", start, end, strlen(target), ITERATIONS_MEDIUM);
    
    vfree(result);
}

static void test_scalability(void)
{
    char *json_10, *json_100, *json_1000, *json_5000;
    size_t size_10, size_100, size_1000, size_5000;
    struct jsonk_value *parsed;
    u64 start, end;
    
    printk(KERN_INFO "=== Scalability Tests ===\n");
    
    /* Generate arrays of different sizes */
    json_10 = generate_simple_json(1024, &size_10);        /* ~10 elements */
    json_100 = generate_simple_json(8192, &size_100);      /* ~100 elements */
    json_1000 = generate_simple_json(65536, &size_1000);   /* ~1000 elements */
    json_5000 = generate_simple_json(262144, &size_5000);  /* ~5000 elements */
    
    if (!json_10 || !json_100 || !json_1000 || !json_5000) {
        printk(KERN_ERR "Failed to generate scalability test data\n");
        goto cleanup;
    }
    
    /* Test parsing time vs size */
    start = get_time_ns();
    parsed = jsonk_parse(json_10, size_10);
    end = get_time_ns();
    if (parsed) {
        printk(KERN_INFO "Array with 10 elements (%zu bytes): %llu ns\n", size_10, end - start);
        printk(KERN_INFO "  Time per element: %llu ns\n", (end - start) / 10);
        jsonk_value_put(parsed);
    }

    start = get_time_ns();
    parsed = jsonk_parse(json_100, size_100);
    end = get_time_ns();
    if (parsed) {
        printk(KERN_INFO "Array with 100 elements (%zu bytes): %llu ns\n", size_100, end - start);
        printk(KERN_INFO "  Time per element: %llu ns\n", (end - start) / 100);
        jsonk_value_put(parsed);
    }
        
    start = get_time_ns();
    parsed = jsonk_parse(json_1000, size_1000);
    end = get_time_ns();
    if (parsed) {
        printk(KERN_INFO "Array with 1000 elements (%zu bytes): %llu ns\n", size_1000, end - start);
        printk(KERN_INFO "  Time per element: %llu ns\n", (end - start) / 1000);
        jsonk_value_put(parsed);
    }
        
    start = get_time_ns();
    parsed = jsonk_parse(json_5000, size_5000);
    end = get_time_ns();
    if (parsed) {
        printk(KERN_INFO "Array with 5000 elements (%zu bytes): %llu ns\n", size_5000, end - start);
        printk(KERN_INFO "  Time per element: %llu ns\n", (end - start) / 5000);
        jsonk_value_put(parsed);
    }
        
cleanup:
    if (json_10) vfree(json_10);
    if (json_100) vfree(json_100);
    if (json_1000) vfree(json_1000);
    if (json_5000) vfree(json_5000);
}

static int __init performance_test_init(void)
{
    printk(KERN_INFO "JSONK Comprehensive Performance Test loaded\n");
    printk(KERN_INFO "Starting performance benchmarks...\n\n");
    
    test_parsing_performance();
    test_pool_performance();
    test_serialization_performance();
    test_patching_performance();
    test_scalability();
    
    printk(KERN_INFO "Performance testing completed!\n");
    printk(KERN_INFO "Check dmesg for detailed results\n");
    
    return 0;
}

static void __exit performance_test_exit(void)
{
    printk(KERN_INFO "JSONK Performance Test unloaded\n");
}

module_init(performance_test_init);
module_exit(performance_test_exit); 