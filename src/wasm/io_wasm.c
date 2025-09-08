/*
 * libgit2.wasm WASM I/O Integration
 * Copyright 2025 Superstruct Ltd, New Zealand
 * Licensed under GPL v2 with Linking Exception (same as libgit2)
 */

#include <git2.h>
#include <emscripten.h>
#include <emscripten/emscripten.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#ifdef GIT_IO_WASM

/* WASM-optimized I/O operations for Git objects and files */

/* Memory pool for frequent allocations */
#define WASM_IO_POOL_SIZE 64
static struct {
    void *buffers[WASM_IO_POOL_SIZE];
    size_t sizes[WASM_IO_POOL_SIZE];
    int in_use[WASM_IO_POOL_SIZE];
    int initialized;
} wasm_io_pool = {0};

/* Initialize WASM I/O subsystem */
EMSCRIPTEN_KEEPALIVE
int git_wasm_io_init(void) {
    if (wasm_io_pool.initialized) return 0;
    
    /* Pre-allocate common buffer sizes */
    const size_t common_sizes[] = {
        256, 1024, 4096, 16384, 65536
    };
    
    for (int i = 0; i < 5 && i < WASM_IO_POOL_SIZE; i++) {
        wasm_io_pool.buffers[i] = malloc(common_sizes[i]);
        if (wasm_io_pool.buffers[i]) {
            wasm_io_pool.sizes[i] = common_sizes[i];
            wasm_io_pool.in_use[i] = 0;
        }
    }
    
    wasm_io_pool.initialized = 1;
    return 0;
}

/* Get buffer from pool or allocate new one */
static void* git_wasm_io_get_buffer(size_t size) {
    /* Try to find suitable buffer in pool */
    for (int i = 0; i < WASM_IO_POOL_SIZE; i++) {
        if (!wasm_io_pool.in_use[i] && 
            wasm_io_pool.buffers[i] && 
            wasm_io_pool.sizes[i] >= size) {
            wasm_io_pool.in_use[i] = 1;
            return wasm_io_pool.buffers[i];
        }
    }
    
    /* Allocate new buffer */
    return malloc(size);
}

/* Return buffer to pool or free it */
static void git_wasm_io_return_buffer(void* buffer) {
    if (!buffer) return;
    
    /* Check if buffer is from pool */
    for (int i = 0; i < WASM_IO_POOL_SIZE; i++) {
        if (wasm_io_pool.buffers[i] == buffer) {
            wasm_io_pool.in_use[i] = 0;
            return;
        }
    }
    
    /* Not from pool, free it */
    free(buffer);
}

/* WASM-optimized file reading with caching */
EMSCRIPTEN_KEEPALIVE
int git_wasm_io_read_file(const char *path, char **content, size_t *size) {
    if (!path || !content || !size) return -EINVAL;
    
    *content = NULL;
    *size = 0;
    
    /* Use Emscripten FS with optimized reading */
    int result = EM_ASM_INT({
        if (typeof FS === 'undefined') return -1;
        
        try {
            var path = UTF8ToString($0);
            var data = FS.readFile(path, { encoding: 'binary' });
            
            if (!data) return -1;
            
            // Allocate buffer using WASM allocator
            var buffer = _malloc(data.length + 1); // +1 for null terminator
            if (buffer === 0) return -1;
            
            // Copy data with null terminator
            HEAP8.set(data, buffer);
            HEAP8[buffer + data.length] = 0;
            
            // Store results
            setValue($1, buffer, 'i32');
            setValue($2, data.length, 'i32');
            
            return 0;
        } catch (e) {
            console.warn('WASM I/O read failed:', e);
            return -1;
        }
    }, path, content, size);
    
    return result;
}

/* WASM-optimized file writing with buffering */
EMSCRIPTEN_KEEPALIVE  
int git_wasm_io_write_file(const char *path, const char *content, size_t size) {
    if (!path || !content) return -EINVAL;
    
    /* Write with automatic sync for persistent paths */
    int result = EM_ASM_INT({
        if (typeof FS === 'undefined') return -1;
        
        try {
            var path = UTF8ToString($0);
            var size = $2;
            
            // Create efficient view of data
            var data = new Uint8Array(HEAP8.buffer, $1, size);
            
            // Write file
            FS.writeFile(path, data, { 
                encoding: 'binary',
                flags: 'w'
            });
            
            // Auto-sync for persistent storage
            if (typeof IDBFS !== 'undefined' && path.indexOf('/persist/') === 0) {
                FS.syncfs(false, function(err) {
                    if (err) console.warn('Auto-sync failed:', err);
                });
            }
            
            return 0;
        } catch (e) {
            console.warn('WASM I/O write failed:', e);  
            return -1;
        }
    }, path, content, size);
    
    return result;
}

/* WASM-optimized directory operations */
EMSCRIPTEN_KEEPALIVE
int git_wasm_io_list_directory(const char *path, char ***entries, size_t *count) {
    if (!path || !entries || !count) return -EINVAL;
    
    *entries = NULL;
    *count = 0;
    
    /* Get directory listing using Emscripten FS */
    char **result_entries = (char**)EM_ASM_INT({
        if (typeof FS === 'undefined') return 0;
        
        try {
            var path = UTF8ToString($0);
            var files = FS.readdir(path);
            
            if (!files || files.length === 0) {
                setValue($2, 0, 'i32');
                return 0;
            }
            
            // Filter out . and ..
            files = files.filter(function(f) { return f !== '.' && f !== '..'; });
            
            if (files.length === 0) {
                setValue($2, 0, 'i32');
                return 0;
            }
            
            // Allocate array of string pointers
            var entries = _malloc(files.length * 4); // 4 bytes per pointer
            if (entries === 0) return 0;
            
            // Allocate and copy each string
            for (var i = 0; i < files.length; i++) {
                var str = files[i];
                var str_len = lengthBytesUTF8(str) + 1;
                var str_ptr = _malloc(str_len);
                
                if (str_ptr === 0) {
                    // Cleanup on failure
                    for (var j = 0; j < i; j++) {
                        _free(getValue(entries + j * 4, 'i32'));
                    }
                    _free(entries);
                    return 0;
                }
                
                stringToUTF8(str, str_ptr, str_len);
                setValue(entries + i * 4, str_ptr, 'i32');
            }
            
            setValue($2, files.length, 'i32');
            return entries;
            
        } catch (e) {
            console.warn('WASM directory listing failed:', e);
            setValue($2, 0, 'i32');
            return 0;
        }
    }, path, entries, count);
    
    *entries = result_entries;
    return result_entries ? 0 : -1;
}

/* Free directory listing */
EMSCRIPTEN_KEEPALIVE
void git_wasm_io_free_directory_list(char **entries, size_t count) {
    if (!entries) return;
    
    for (size_t i = 0; i < count; i++) {
        free(entries[i]);
    }
    free(entries);
}

/* WASM-optimized Git object reading */
EMSCRIPTEN_KEEPALIVE
int git_wasm_io_read_git_object(const char *repo_path, const char *oid_hex, 
                                char **object_data, size_t *object_size,
                                int *object_type) {
    if (!repo_path || !oid_hex || !object_data || !object_size || !object_type) {
        return -EINVAL;
    }
    
    *object_data = NULL;
    *object_size = 0;
    *object_type = -1;
    
    /* Construct object path from OID */
    if (strlen(oid_hex) < 40) return -EINVAL;
    
    char object_path[1024];
    snprintf(object_path, sizeof(object_path), "%s/.git/objects/%.2s/%s", 
             repo_path, oid_hex, oid_hex + 2);
    
    /* Read object file */
    char *compressed_data;
    size_t compressed_size;
    
    int result = git_wasm_io_read_file(object_path, &compressed_data, &compressed_size);
    if (result != 0) return result;
    
    /* TODO: Implement zlib decompression for Git objects */
    /* For now, assume uncompressed for testing */
    *object_data = compressed_data;
    *object_size = compressed_size;
    *object_type = 1; /* Blob by default */
    
    return 0;
}

/* WASM-optimized Git object writing */
EMSCRIPTEN_KEEPALIVE
int git_wasm_io_write_git_object(const char *repo_path, const char *oid_hex,
                                 const char *object_data, size_t object_size,
                                 int object_type) {
    if (!repo_path || !oid_hex || !object_data) return -EINVAL;
    
    /* Ensure objects directory exists */
    char objects_dir[1024];
    snprintf(objects_dir, sizeof(objects_dir), "%s/.git/objects/%.2s", repo_path, oid_hex);
    
    EM_ASM({
        var path = UTF8ToString($0);
        if (typeof FS !== 'undefined') {
            try {
                FS.mkdir(path);
            } catch (e) {
                // Directory might already exist
            }
        }
    }, objects_dir);
    
    /* Construct object path */
    char object_path[1024];
    snprintf(object_path, sizeof(object_path), "%s/%s", objects_dir, oid_hex + 2);
    
    /* TODO: Implement zlib compression for Git objects */
    /* For now, write uncompressed for testing */
    return git_wasm_io_write_file(object_path, object_data, object_size);
}

/* Cleanup WASM I/O subsystem */
EMSCRIPTEN_KEEPALIVE
void git_wasm_io_cleanup(void) {
    for (int i = 0; i < WASM_IO_POOL_SIZE; i++) {
        if (wasm_io_pool.buffers[i]) {
            free(wasm_io_pool.buffers[i]);
            wasm_io_pool.buffers[i] = NULL;
        }
        wasm_io_pool.sizes[i] = 0;
        wasm_io_pool.in_use[i] = 0;
    }
    wasm_io_pool.initialized = 0;
}

#endif /* GIT_IO_WASM */