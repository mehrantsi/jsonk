# JSONK - JSON Library for Linux Kernel

[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html)
[![Kernel](https://img.shields.io/badge/Kernel-5.4%2B-green.svg)](https://kernel.org/)

JSONK is a JSON parsing, manipulation, and atomic patching library designed specifically for Linux kernel space. It provides efficient JSON operations with memory safety and atomic patching capabilities.

## Features

- **RFC 8259 Compliant**: Full JSON specification support
- **Atomic JSON Patching**: Apply partial updates to JSON objects with rollback safety
- **Path-Based Access**: Access nested values using dot notation (e.g., "user.profile.name")
- **Memory Safe**: Built-in limits and validation to prevent DoS attacks and UAF vulnerabilities
- **Reference Counting**: Automatic memory management with reference counting
- **Lock-Free Design**: Callers handle synchronization for maximum flexibility
- **Zero Dependencies**: No external dependencies beyond standard kernel APIs

### RFC 8259 Compliance

JSONK implements the complete JSON specification (RFC 8259):

- **All JSON Data Types**: null, boolean, number, string, object, array
- **String Escaping**: Complete support for `\"`, `\\`, `\/`, `\b`, `\f`, `\n`, `\r`, `\t`
- **Unicode Escapes**: `\uXXXX` sequences (stored literally for kernel efficiency)
- **Number Parsing**: Integers, decimals, scientific notation, negative numbers
- **Validation**: Proper syntax checking, no leading zeros, control character rejection
- **Whitespace Handling**: Correct parsing of JSON whitespace
- **Nested Structures**: Objects and arrays with configurable depth limits
- **Error Handling**: Robust parsing with detailed error reporting

## Performance

JSONK delivers excellent performance for kernel space JSON operations:

### JSON Operations Performance
- **Small JSON Parsing (~833 bytes, 10 objects)**: 425K ops/sec (337 MB/s throughput)
- **Medium JSON Parsing (~8KB, 100 objects)**: 13.8K ops/sec (859 MB/s throughput)
- **Large JSON Parsing (~1MB, 200 objects)**: 1.12K ops/sec (972 MB/s throughput)
- **JSON Serialization**: 5.94M ops/sec (872 MB/s throughput)
- **JSON Patching**: 827K ops/sec (42 MB/s throughput)
- **Scalability**: Excellent scaling (53-220 ns per element as size increases)

*Performance measured on Linux 6.8.0 in kernel space*

## Project Structure

```
jsonk/
├── src/                    # Core library source
│   └── jsonk.c            # Main implementation
├── include/               # Header files  
│   └── jsonk.h           # Public API
├── examples/              # Usage examples
│   └── basic_usage.c     # Comprehensive usage examples
├── tests/                 # Test modules
│   ├── atomic_test.c     # Atomic patching tests
│   └── performance_test.c # Comprehensive performance tests
└── Makefile              # Build system
```

## Development Environment Setup

JSONK includes a complete development environment using Docker devcontainers that works seamlessly with VS Code and supports both local development (using Colima on macOS) and cloud development (GitHub Codespaces).

### Prerequisites

#### For macOS (Local Development)
- [VS Code](https://code.visualstudio.com/) with the [Dev Containers extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers)
- [Homebrew](https://brew.sh/) (will be installed automatically if missing)

#### For Linux (Local Development)
- [VS Code](https://code.visualstudio.com/) with the [Dev Containers extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers)
- Docker (will be installed automatically if missing)

#### For GitHub Codespaces
- Just a web browser! No local setup required.

### Quick Setup

1. **Clone the repository:**
   ```bash
   git clone <repository-url>
   cd jsonk
   ```

2. **Run the local setup script (macOS/Linux only):**
   ```bash
   bash .devcontainer/local-setup.sh
   ```
   
   This script will:
   - **macOS**: Install Homebrew, Colima, and Docker CLI, then start Colima with optimized settings
   - **Linux**: Install Docker and development tools

3. **Open in VS Code:**
   ```bash
   code .
   ```

4. **Start development container:**
   - VS Code will prompt you to "Reopen in Container" - click it
   - Or use Command Palette (`Cmd/Ctrl+Shift+P`) → "Dev Containers: Reopen in Container"

### Colima Configuration (macOS)

The setup script configures Colima with the following settings:
- **4 CPU cores**
- **8GB RAM**
- **20GB disk space**
- **VirtioFS mount type** for better file system performance
- **ARM64 architecture** (Apple Silicon optimized)

### Manual Setup (Alternative)

If you prefer manual setup or need to troubleshoot:

1. **Install Colima (macOS):**
   ```bash
   brew install colima docker
   colima start --cpu 4 --memory 8 --disk 20 --vm-type vz --arch aarch64 --mount-type virtiofs
   ```

2. **Install Docker (Linux):**
   ```bash
   sudo apt-get update
   sudo apt-get install -y docker.io
   sudo usermod -aG docker $USER
   # Log out and back in
   ```

3. **Open in VS Code and select "Reopen in Container"**

### Troubleshooting

#### Colima Issues (macOS)
```bash
# Check Colima status
colima status

# Restart Colima if needed
colima stop
colima start --cpu 4 --memory 8 --disk 20 --vm-type vz --arch aarch64 --mount-type virtiofs

# Check Docker connectivity
docker info
```

#### Container Build Issues
```bash
# Rebuild container
# In VS Code: Cmd/Ctrl+Shift+P → "Dev Containers: Rebuild Container"

# Or manually:
docker system prune -f
# Then reopen in container
```

#### Kernel Header Issues
The setup automatically handles kernel headers, but if you encounter issues:
```bash
# Check current kernel
uname -r

# Install headers manually (inside container)
sudo apt-get update
sudo apt-get install -y linux-headers-$(uname -r)
```

## Quick Start

### Building

```bash
# Inside the development container:
cd /workspaces/jsonk  # or your workspace path

# Build all modules
make

# Install the modules (optional)
sudo make install

# Load the core module
sudo make load
```

### Testing

```bash
# Test basic functionality
make test-basic

# Run performance benchmarks
make test-perf

# Test atomic patching
make test-atomic

# View results
dmesg | tail -50
```

### Basic Usage

```c
#include "include/jsonk.h"

// Parse JSON string
const char *json_str = "{\"name\":\"test\",\"value\":42}";
struct jsonk_value *json = jsonk_parse(json_str, strlen(json_str));

// Access object members
struct jsonk_member *name_member = jsonk_object_find_member(&json->u.object, "name", 4);
if (name_member && name_member->value->type == JSONK_VALUE_STRING) {
    printk("Name: %s\n", name_member->value->u.string.data);
}

// Serialize back to string
char buffer[1024];
size_t written;
int ret = jsonk_serialize(json, buffer, sizeof(buffer), &written);
if (ret == 0) {
    printk("JSON: %.*s\n", (int)written, buffer);
}

// Clean up with reference counting
jsonk_value_put(json);
```

### JSON Patching

```c
// Original JSON
const char *target = "{\"name\":\"test\",\"value\":42}";
// Patch to apply
const char *patch = "{\"value\":100,\"new_field\":\"added\"}";

char result[1024];
size_t result_len;

int ret = jsonk_apply_patch(target, strlen(target),
                           patch, strlen(patch),
                           result, sizeof(result), &result_len);

if (ret == JSONK_PATCH_SUCCESS) {
    printk("Patched JSON: %.*s\n", (int)result_len, result);
    // Result: {"name":"test","value":100,"new_field":"added"}
}
```

### Path-Based Access Example

```c
// Access nested values using dot notation
const char *json_str = "{\"user\":{\"profile\":{\"name\":\"Mehran\",\"age\":30}}}";
struct jsonk_value *json = jsonk_parse(json_str, strlen(json_str));

// Get nested value
struct jsonk_value *name = jsonk_get_value_by_path(json, "user.profile.name", 17);
if (name && name->type == JSONK_VALUE_STRING) {
    printk("Name: %s\n", name->u.string.data);
}

// Set nested value (creates intermediate objects if needed)
struct jsonk_value *new_role = jsonk_value_create_string("admin", 5);
jsonk_set_value_by_path(json, "user.profile.role", 17, new_role);
jsonk_value_put(new_role);

jsonk_value_put(json);
```

### Multi-Threading Example

```c
// JSONK is not thread-safe - callers must handle synchronization
static struct jsonk_value *shared_json = NULL;
static DEFINE_SPINLOCK(json_lock);

void safe_json_update(const char *path, struct jsonk_value *value) {
    unsigned long flags;
    
    spin_lock_irqsave(&json_lock, flags);
    if (shared_json) {
        jsonk_set_value_by_path(shared_json, path, strlen(path), value);
    }
    spin_unlock_irqrestore(&json_lock, flags);
}

void safe_json_read(char *buffer, size_t buffer_size, size_t *written) {
    unsigned long flags;
    struct jsonk_value *json_ref = NULL;
    
    spin_lock_irqsave(&json_lock, flags);
    if (shared_json) {
        json_ref = jsonk_value_get(shared_json);  // Get reference
    }
    spin_unlock_irqrestore(&json_lock, flags);
    
    if (json_ref) {
        jsonk_serialize(json_ref, buffer, buffer_size, written);
        jsonk_value_put(json_ref);  // Release reference
    }
}
```

## API Reference

### Core Functions

#### `jsonk_parse()`
```c
struct jsonk_value *jsonk_parse(const char *json_str, size_t json_len);
```
Parse a JSON string into an in-memory structure.

**Parameters:**
- `json_str`: JSON string to parse
- `json_len`: Length of JSON string

**Returns:** Pointer to parsed JSON value or NULL on error

#### `jsonk_serialize()`
```c
int jsonk_serialize(struct jsonk_value *value, char *buffer, size_t buffer_size, size_t *written);
```
Serialize a JSON value structure to a string.

**Parameters:**
- `value`: JSON value to serialize
- `buffer`: Output buffer
- `buffer_size`: Size of output buffer
- `written`: Pointer to store actual bytes written

**Returns:** 0 on success, negative error code on failure

#### `jsonk_value_get()`
```c
struct jsonk_value *jsonk_value_get(struct jsonk_value *value);
```
Get a reference to a JSON value (increment reference count).

#### `jsonk_value_put()`
```c
void jsonk_value_put(struct jsonk_value *value);
```
Release a reference to a JSON value (decrement reference count, free when zero).

#### `jsonk_apply_patch()`
```c
int jsonk_apply_patch(const char *target, size_t target_len,
                      const char *patch, size_t patch_len,
                      char *result, size_t result_max_len, size_t *result_len);
```
Apply a JSON patch to a target buffer atomically.

**Returns:**
- `JSONK_PATCH_SUCCESS`: Operation completed successfully
- `JSONK_PATCH_NO_CHANGE`: No changes were made
- `JSONK_PATCH_ERROR_*`: Various error conditions

### Value Creation Functions

#### `jsonk_value_create_string()`
```c
struct jsonk_value *jsonk_value_create_string(const char *str, size_t len);
```

#### `jsonk_value_create_number()`
```c
struct jsonk_value *jsonk_value_create_number(const char *str, size_t len);
```

#### `jsonk_value_create_boolean()`
```c
struct jsonk_value *jsonk_value_create_boolean(bool val);
```

#### `jsonk_value_create_null()`
```c
struct jsonk_value *jsonk_value_create_null(void);
```

### Object Manipulation

#### `jsonk_object_add_member()`
```c
int jsonk_object_add_member(struct jsonk_object *obj, const char *key, size_t key_len, struct jsonk_value *value);
```

#### `jsonk_object_find_member()`
```c
struct jsonk_member *jsonk_object_find_member(struct jsonk_object *obj, const char *key, size_t key_len);
```

#### `jsonk_object_remove_member()`
```c
int jsonk_object_remove_member(struct jsonk_object *obj, const char *key, size_t key_len);
```

### Array Manipulation

#### `jsonk_array_add_element()`
```c
int jsonk_array_add_element(struct jsonk_array *arr, struct jsonk_value *value);
```

### Path-Based Access

#### `jsonk_get_value_by_path()`
```c
struct jsonk_value *jsonk_get_value_by_path(struct jsonk_value *root, const char *path, size_t path_len);
```
Get a value using a dot-separated path (e.g., "user.profile.name").

**Parameters:**
- `root`: Root JSON object
- `path`: Dot-separated path string
- `path_len`: Length of path string

**Returns:** Pointer to found value or NULL if not found

#### `jsonk_set_value_by_path()`
```c
int jsonk_set_value_by_path(struct jsonk_value *root, const char *path, size_t path_len, struct jsonk_value *value);
```
Set a value using a dot-separated path. Creates intermediate objects if they don't exist.

**Parameters:**
- `root`: Root JSON object
- `path`: Dot-separated path string
- `path_len`: Length of path string
- `value`: Value to set (will be deep copied)

**Returns:** 0 on success, negative error code on failure

## Performance Characteristics

- **Parsing**: Single-pass, O(n) complexity
- **Memory**: Efficient memory management with reference counting
- **Patching**: Atomic operations, copy-on-write semantics
- **Serialization**: Direct buffer writing, no intermediate allocations

## Build Targets

The Makefile provides complete build and test targets:

### Core Targets
```bash
make all          # Build all modules
make clean        # Clean build files
make install      # Install modules system-wide
make uninstall    # Remove installed modules
```

### Module Management
```bash
make load         # Load the jsonk core module
make unload       # Unload the jsonk core module
```

### Testing Targets
```bash
make test-basic   # Run basic usage examples
make test-perf    # Run comprehensive performance tests
make test-atomic  # Run atomic patching tests

# Individual module loading
make load-basic   # Load basic usage module
make load-perf    # Load performance test module
make load-atomic  # Load atomic test module

# Individual module unloading
make unload-basic # Unload basic usage module
make unload-perf  # Unload performance test module
make unload-atomic # Unload atomic test module
```

### Usage Examples
```bash
# Complete test workflow
make clean && make
make test-basic
make test-perf
make test-atomic

# View test results
dmesg | tail -100
```

## Integration in Other Modules

To use JSONK in your kernel module:

1. **Include the header:**
```c
#include "path/to/jsonk.h"
```

2. **Add dependency in your Makefile:**
```makefile
# Ensure jsonk is loaded first
obj-m += your_module.o
your_module-objs := your_source.o
```

3. **Declare dependency:**
```c
MODULE_SOFTDEP("pre: jsonk");
```

## JSON Patch Behavior

JSONK supports **both additive and removal operations** through JSON patching:

### Additive Operations
- **Add new fields**: Include them in the patch object
- **Update existing fields**: Provide new values in the patch object
- **Merge objects**: Nested objects are recursively merged

### Removal Operations
Fields are **removed** when the patch contains:
- `null` values
- Empty strings (`""`)
- Empty objects (`{}`)
- Empty arrays (`[]`)

### Examples

```c
// Adding and updating
const char *patch1 = "{\"new_field\":\"value\",\"existing_field\":\"updated\"}";

// Removing fields
const char *patch2 = "{\"remove_me\":null,\"also_remove\":\"\"}";

// Mixed operations
const char *patch3 = "{\"add\":\"new\",\"update\":\"changed\",\"remove\":null}";

// Nested operations
const char *patch4 = "{\"user\":{\"name\":\"updated\",\"old_field\":null}}";
```

## Limitations

- **Maximum nesting depth**: 32 levels (configurable via `JSONK_MAX_DEPTH`)
- **Maximum object members**: 1000 per individual object (configurable via `JSONK_MAX_OBJECT_MEMBERS`)
- **Maximum array elements**: 10000 per individual array (configurable via `JSONK_MAX_ARRAY_SIZE`)
- **Number precision**: Limited to 64-bit integers and basic fractional support
- **Unicode handling**: Unicode escape sequences stored literally for kernel efficiency
- **Memory limits**: Built-in limits to prevent DoS attacks in kernel space

## Thread Safety

JSONK is not thread-safe by design. Callers must handle synchronization when using JSONK functions from multiple threads.

```c
// Example with spinlock
static DEFINE_SPINLOCK(json_lock);

spin_lock(&json_lock);
ret = jsonk_apply_patch(target, target_len, patch, patch_len, 
                       result, result_len, &written);
spin_unlock(&json_lock);
```

## Memory Management

JSONK uses reference counting for memory safety and UAF protection:

### Reference Counting
- All JSON values start with a reference count of 1
- Use `jsonk_value_get()` to increment the reference count
- Use `jsonk_value_put()` to decrement the reference count
- Memory is automatically freed when the reference count reaches 0

### Example
```c
// Create a JSON value (starts with refcount = 1)
struct jsonk_value *json = jsonk_parse(json_str, len);

// Get additional reference (refcount = 2)
struct jsonk_value *json_ref = jsonk_value_get(json);

// Release references (refcount decrements, frees when reaches 0)
jsonk_value_put(json_ref);  // refcount = 1
jsonk_value_put(json);      // refcount = 0, memory freed
```

### Multi-threading Safety
Reference counting provides protection against use-after-free vulnerabilities in multi-threaded environments:

```c
// Thread 1: Get reference before using
struct jsonk_value *safe_ref = jsonk_value_get(shared_json);
// Use safe_ref...
jsonk_value_put(safe_ref);

// Thread 2: Can safely free without affecting Thread 1
jsonk_value_put(shared_json);  // Only frees when all references released
```

## License

This project is licensed under the GNU General Public License v2.0 - see the [LICENSE](LICENSE) file for details.

## Support

For issues, questions, or contributions, please open an issue on the project repository. 