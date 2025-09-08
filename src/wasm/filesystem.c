/*
 * libgit2.wasm WASM Filesystem Integration
 * Copyright 2025 Superstruct Ltd, New Zealand
 * Licensed under GPL v2 with Linking Exception (same as libgit2)
 */

#include <git2.h>
#include <emscripten.h>
#include <emscripten/emscripten.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef GIT_FS_VIRTUAL

/* Forward declarations for JavaScript callback functions */
extern int js_fs_mkdir(const char *path);
extern int js_fs_rmdir(const char *path);
extern int js_fs_exists(const char *path);
extern int js_fs_read(const char *path, char **buffer, size_t *size);
extern int js_fs_write(const char *path, const char *data, size_t size);
extern int js_fs_stat(const char *path, struct stat *stat_buf);

/* Global flag to track filesystem initialization */
static int wasm_fs_initialized = 0;

/* Initialize WASM filesystem patterns */
EMSCRIPTEN_KEEPALIVE
int git_wasm_fs_init(void) {
    if (wasm_fs_initialized) {
        return 0;
    }
    
    /* Initialize virtual filesystem */
    EM_ASM({
        // Create standard directories in Emscripten FS
        if (typeof FS !== 'undefined') {
            try {
                FS.mkdir('/tmp');
                FS.mkdir('/home');
                FS.mkdir('/persist');
                console.log('WASM filesystem initialized');
            } catch(e) {
                console.warn('Failed to create base directories:', e);
            }
        }
    });
    
    wasm_fs_initialized = 1;
    return 0;
}

/* WASM-native filesystem operations */

EMSCRIPTEN_KEEPALIVE
int git_wasm_fs_mkdir(const char *path) {
    if (!path) return -EINVAL;
    
    /* Use Emscripten FS directly for directory creation */
    int result = EM_ASM_INT({
        if (typeof FS === 'undefined') return -1;
        
        try {
            var path = UTF8ToString($0);
            FS.mkdir(path);
            return 0;
        } catch (e) {
            console.warn('mkdir failed:', e);
            return -1;
        }
    }, path);
    
    return result;
}

EMSCRIPTEN_KEEPALIVE
int git_wasm_fs_rmdir(const char *path) {
    if (!path) return -EINVAL;
    
    int result = EM_ASM_INT({
        if (typeof FS === 'undefined') return -1;
        
        try {
            var path = UTF8ToString($0);
            FS.rmdir(path);
            return 0;
        } catch (e) {
            console.warn('rmdir failed:', e);
            return -1;
        }
    }, path);
    
    return result;
}

EMSCRIPTEN_KEEPALIVE
int git_wasm_fs_exists(const char *path) {
    if (!path) return 0;
    
    int result = EM_ASM_INT({
        if (typeof FS === 'undefined') return 0;
        
        try {
            var path = UTF8ToString($0);
            var stat = FS.stat(path);
            return stat ? 1 : 0;
        } catch (e) {
            return 0;
        }
    }, path);
    
    return result;
}

EMSCRIPTEN_KEEPALIVE
int git_wasm_fs_read(const char *path, char **buffer, size_t *size) {
    if (!path || !buffer || !size) return -EINVAL;
    
    *buffer = NULL;
    *size = 0;
    
    /* Read file using Emscripten FS */
    char *data = (char*)EM_ASM_INT({
        if (typeof FS === 'undefined') return 0;
        
        try {
            var path = UTF8ToString($0);
            var data = FS.readFile(path, { encoding: 'binary' });
            
            // Allocate memory for the data
            var buffer = _malloc(data.length);
            if (buffer === 0) return 0;
            
            // Copy data to allocated buffer
            HEAP8.set(data, buffer);
            
            // Store size
            setValue($1, data.length, 'i32');
            
            return buffer;
        } catch (e) {
            console.warn('read file failed:', e);
            return 0;
        }
    }, path, size);
    
    if (data) {
        *buffer = data;
        return 0;
    }
    
    return -1;
}

EMSCRIPTEN_KEEPALIVE
int git_wasm_fs_write(const char *path, const char *data, size_t size) {
    if (!path || !data) return -EINVAL;
    
    int result = EM_ASM_INT({
        if (typeof FS === 'undefined') return -1;
        
        try {
            var path = UTF8ToString($0);
            var size = $2;
            
            // Create Uint8Array from memory
            var data = new Uint8Array(HEAP8.buffer, $1, size);
            
            FS.writeFile(path, data, { encoding: 'binary' });
            
            // Sync to persistent storage if path is under /persist/
            if (typeof IDBFS !== 'undefined' && path.indexOf('/persist/') === 0) {
                FS.syncfs(false, function(err) {
                    if (err) console.warn('Failed to sync to IndexedDB:', err);
                });
            }
            
            return 0;
        } catch (e) {
            console.warn('write file failed:', e);
            return -1;
        }
    }, path, data, size);
    
    return result;
}

EMSCRIPTEN_KEEPALIVE
int git_wasm_fs_stat(const char *path, struct stat *stat_buf) {
    if (!path || !stat_buf) return -EINVAL;
    
    memset(stat_buf, 0, sizeof(struct stat));
    
    int result = EM_ASM_INT({
        if (typeof FS === 'undefined') return -1;
        
        try {
            var path = UTF8ToString($0);
            var stat = FS.stat(path);
            var stat_ptr = $1;
            
            if (!stat) return -1;
            
            // Write basic stat fields (simplified for WASM)
            // struct stat layout varies, but we'll set common fields
            setValue(stat_ptr + 16, stat.mode || (stat.isDir ? 16877 : 33188), 'i32');  // st_mode offset ~16
            setValue(stat_ptr + 44, stat.size || 0, 'i32');                             // st_size offset ~44  
            setValue(stat_ptr + 72, Math.floor((stat.mtime || Date.now()) / 1000), 'i32'); // st_mtime offset ~72
            
            return 0;
        } catch (e) {
            console.warn('stat failed:', e);
            return -1;
        }
    }, path, stat_buf);
    
    return result;
}

/* Git integration functions */

int git_wasm_create_repository_workdir(const char *repo_path) {
    if (!repo_path) return -EINVAL;
    
    char git_dir[1024];
    snprintf(git_dir, sizeof(git_dir), "%s/.git", repo_path);
    
    /* Create repository directory structure */
    if (git_wasm_fs_mkdir(repo_path) != 0) {
        return -1;
    }
    
    if (git_wasm_fs_mkdir(git_dir) != 0) {
        return -1;
    }
    
    /* Create standard Git subdirectories */
    char objects_dir[1024], refs_dir[1024], heads_dir[1024], tags_dir[1024];
    snprintf(objects_dir, sizeof(objects_dir), "%s/objects", git_dir);
    snprintf(refs_dir, sizeof(refs_dir), "%s/refs", git_dir);
    snprintf(heads_dir, sizeof(heads_dir), "%s/heads", refs_dir);
    snprintf(tags_dir, sizeof(tags_dir), "%s/tags", refs_dir);
    
    git_wasm_fs_mkdir(objects_dir);
    git_wasm_fs_mkdir(refs_dir);
    git_wasm_fs_mkdir(heads_dir);
    git_wasm_fs_mkdir(tags_dir);
    
    /* Create basic Git files */
    char head_file[1024], config_file[1024], description_file[1024];
    snprintf(head_file, sizeof(head_file), "%s/HEAD", git_dir);
    snprintf(config_file, sizeof(config_file), "%s/config", git_dir);
    snprintf(description_file, sizeof(description_file), "%s/description", git_dir);
    
    const char *head_content = "ref: refs/heads/main\n";
    const char *config_content = "[core]\n\trepositoryformatversion = 0\n\tfilemode = true\n\tbare = false\n\tlogallrefupdates = true\n";
    const char *description_content = "Unnamed repository; edit this file to name the repository.\n";
    
    git_wasm_fs_write(head_file, head_content, strlen(head_content));
    git_wasm_fs_write(config_file, config_content, strlen(config_content));
    git_wasm_fs_write(description_file, description_content, strlen(description_content));
    
    return 0;
}

int git_wasm_sync_to_persistent_storage(const char *repo_path) {
    if (!repo_path) return 0;
    
    /* Trigger sync to IndexedDB if the repo is under /persist/ */
    if (strncmp(repo_path, "/persist/", 9) == 0) {
        EM_ASM({
            if (typeof FS !== 'undefined' && typeof IDBFS !== 'undefined') {
                FS.syncfs(false, function(err) {
                    if (err) {
                        console.warn('Failed to sync repository to persistent storage:', err);
                    } else {
                        console.log('Repository synced to persistent storage');
                    }
                });
            }
        });
    }
    
    return 0;
}

int git_wasm_load_from_persistent_storage(const char *repo_path) {
    if (!repo_path) return 0;
    
    /* Load from IndexedDB if the repo is under /persist/ */
    if (strncmp(repo_path, "/persist/", 9) == 0) {
        int result = EM_ASM_INT({
            if (typeof FS !== 'undefined' && typeof IDBFS !== 'undefined') {
                // Synchronous load is not possible, but we can return success
                // The actual loading happens asynchronously
                FS.syncfs(true, function(err) {
                    if (err) {
                        console.warn('Failed to load repository from persistent storage:', err);
                    } else {
                        console.log('Repository loaded from persistent storage');
                    }
                });
                return 0;
            }
            return 0;
        });
        
        return result;
    }
    
    return 0;
}

#endif /* GIT_FS_VIRTUAL */