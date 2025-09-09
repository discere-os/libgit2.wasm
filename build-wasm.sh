#!/bin/bash
# libgit2.wasm - Production WASM Build System
# Copyright 2025 Superstruct Ltd, New Zealand
# Licensed under GPL v2 with Linking Exception (same as libgit2)

set -euo pipefail

VARIANT="${1:-release}"
EMSCRIPTEN_VERSION="4.0.14"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log() {
    echo -e "${GREEN}[libgit2.wasm]${NC} $1"
}

warn() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1" >&2
    exit 1
}

# Validate Emscripten installation
check_emscripten() {
    if ! command -v emcc &> /dev/null; then
        error "Emscripten not found. Please install Emscripten ${EMSCRIPTEN_VERSION}"
    fi
    
    local version=$(emcc --version | head -n1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+')
    log "Using Emscripten version: ${version}"
    
    if [[ "$version" != "$EMSCRIPTEN_VERSION" ]]; then
        warn "Emscripten version ${version} != expected ${EMSCRIPTEN_VERSION}"
    fi
}

# Configure build variant
configure_build() {
    log "Configuring build variant: ${VARIANT}"
    
    case "$VARIANT" in
        "release")
            CMAKE_BUILD_TYPE="Release"
            EMCC_FLAGS="-O3 -flto --closure 1 -DNDEBUG -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864 -sFORCE_FILESYSTEM=1 -sEXPORTED_RUNTIME_METHODS=['FS','IDBFS','PATH'] -sWASM_ASYNC_COMPILATION=0"
            SIMD_FLAGS=""
            THREADING_FLAGS=""
            DEBUG_FLAGS=""
            ;;
        "simd")
            CMAKE_BUILD_TYPE="Release"
            EMCC_FLAGS="-O3 -flto -msimd128 -DGIT_WASM_SIMD=1 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864 -sFORCE_FILESYSTEM=1 -sEXPORTED_RUNTIME_METHODS=['FS','IDBFS','PATH'] -sWASM_ASYNC_COMPILATION=0"
            SIMD_FLAGS="-DENABLE_SIMD=ON"
            THREADING_FLAGS=""
            DEBUG_FLAGS=""
            ;;
        "debug")
            CMAKE_BUILD_TYPE="Debug"
            EMCC_FLAGS="-O1 -g4 --source-map-base / -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864 -sFORCE_FILESYSTEM=1 -sEXPORTED_RUNTIME_METHODS=['FS','IDBFS','PATH'] -sWASM_ASYNC_COMPILATION=0 -sASSERTIONS=1"
            SIMD_FLAGS=""
            THREADING_FLAGS=""
            DEBUG_FLAGS="-DDEBUG_POOL=ON -DDEBUG_STRICT_ALLOC=ON"
            ;;
        "threaded")
            CMAKE_BUILD_TYPE="Release"
            EMCC_FLAGS="-O3 -flto -pthread -sPTHREAD_POOL_SIZE=4 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=67108864 -sFORCE_FILESYSTEM=1 -sEXPORTED_RUNTIME_METHODS=['FS','IDBFS','PATH'] -sWASM_ASYNC_COMPILATION=0 -sPROXY_TO_PTHREAD"
            SIMD_FLAGS=""
            THREADING_FLAGS="-DUSE_THREADS=pthreads"
            DEBUG_FLAGS=""
            warn "Threading support is experimental in browsers"
            ;;
        *)
            error "Unknown variant: ${VARIANT}. Use: release, simd, debug, or threaded"
            ;;
    esac
    
    export CMAKE_BUILD_TYPE EMCC_FLAGS SIMD_FLAGS THREADING_FLAGS DEBUG_FLAGS
}

# Set up build environment
setup_environment() {
    log "Setting up build environment for ${VARIANT}"
    
    # Create build directory
    BUILD_DIR="${SCRIPT_DIR}/build-${VARIANT}"
    DIST_DIR="${SCRIPT_DIR}/dist"
    
    mkdir -p "${BUILD_DIR}"
    mkdir -p "${DIST_DIR}"
    
    cd "${BUILD_DIR}"
    
    export BUILD_DIR DIST_DIR
}

# Configure CMake
configure_cmake() {
    log "Configuring CMake for libgit2 WASM build"
    
    # Base CMake configuration for WASM
    local CMAKE_ARGS=(
        -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
        -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}/install"
        
        # WASM-specific configuration
        -DUSE_HTTPS="OpenSSL-Dynamic"     # Use OpenSSL.wasm module
        -DUSE_SSH="OFF"                   # Disable SSH for browser compatibility
        -DUSE_SHA1="HTTPS"                # Use OpenSSL for SHA-1 (with SIMD potential)
        -DUSE_SHA256="HTTPS"              # Use OpenSSL for SHA-256 (with SIMD potential)
        -DUSE_HTTP_PARSER="builtin"       # Use bundled llhttp parser
        -DUSE_REGEX="builtin"             # Use bundled PCRE (can be changed to pcre2)
        -DUSE_COMPRESSION="builtin"       # Use bundled zlib
        
        # WASM-native filesystem configuration  
        -DGIT_IO_WASM=ON                  # Enable WASM I/O patterns
        -DGIT_FS_VIRTUAL=ON               # Enable virtual filesystem support
        
        # Add WASM source files
        -DWASM_SOURCES_DIR="${SCRIPT_DIR}/src/wasm"
        
        # Build configuration
        -DBUILD_TESTS="OFF"               # Disable tests for WASM build
        -DBUILD_CLI="OFF"                 # Disable command-line interface
        -DBUILD_EXAMPLES="OFF"            # Disable examples
        -DBUILD_FUZZERS="OFF"             # Disable fuzzers
        -DBUILD_SHARED_LIBS="OFF"         # Static library for WASM
        
        # Optimization flags
        ${SIMD_FLAGS}
        ${THREADING_FLAGS}
        ${DEBUG_FLAGS}
    )
    
    # Add threading configuration if enabled
    if [[ "${VARIANT}" == "threaded" ]]; then
        CMAKE_ARGS+=(-DUSE_THREADS="pthreads")
    else
        CMAKE_ARGS+=(-DUSE_THREADS="OFF")
    fi
    
    # Configure with emcmake
    log "Running emcmake cmake with configuration..."
    emcmake cmake "${SCRIPT_DIR}" "${CMAKE_ARGS[@]}"
    
    if [[ $? -ne 0 ]]; then
        error "CMake configuration failed"
    fi
}

# Build with emmake
build_library() {
    log "Building libgit2 WASM library..."
    
    # Build with emmake
    emmake make -j$(nproc) VERBOSE=1
    
    if [[ $? -ne 0 ]]; then
        error "Build failed"
    fi
    
    # Install to get clean artifacts
    make install
    
    if [[ $? -ne 0 ]]; then
        error "Install failed"
    fi
}

# Create WASM artifacts
create_artifacts() {
    log "Creating WASM artifacts..."
    
    # Find generated WASM and JS files
    local WASM_FILE=$(find . -name "*.wasm" | head -1)
    local JS_FILE=$(find . -name "*.js" | head -1)
    
    if [[ -z "$WASM_FILE" || -z "$JS_FILE" ]]; then
        error "WASM or JS files not found after build"
    fi
    
    # Copy to dist directory with proper naming
    cp "$WASM_FILE" "${DIST_DIR}/libgit2-${VARIANT}.wasm"
    cp "$JS_FILE" "${DIST_DIR}/libgit2-${VARIANT}.js"
    
    log "✅ WASM artifacts created:"
    log "   📦 WASM: $(wc -c < "$WASM_FILE" | numfmt --to=iec-i)B"
    log "   📦 JS:   $(wc -c < "$JS_FILE" | numfmt --to=iec-i)B"
}

# Generate manifest
generate_manifest() {
    log "Generating manifest for ${VARIANT}..."
    
    local WASM_FILE="${DIST_DIR}/libgit2-${VARIANT}.wasm"
    local JS_FILE="${DIST_DIR}/libgit2-${VARIANT}.js"
    
    if [[ ! -f "$WASM_FILE" || ! -f "$JS_FILE" ]]; then
        error "WASM or JS files not found for manifest generation"
    fi
    
    local WASM_SIZE=$(wc -c < "$WASM_FILE")
    local JS_SIZE=$(wc -c < "$JS_FILE")
    local GZIP_WASM_SIZE=$(gzip -c "$WASM_FILE" | wc -c)
    local GZIP_JS_SIZE=$(gzip -c "$JS_FILE" | wc -c)
    
    # Determine features based on build variant
    local FEATURES='["git", "http", "https"]'
    if [[ "$VARIANT" == "simd" ]]; then
        FEATURES='["git", "http", "https", "simd"]'
    elif [[ "$VARIANT" == "threaded" ]]; then
        FEATURES='["git", "http", "https", "threading"]'
    elif [[ "$VARIANT" == "debug" ]]; then
        FEATURES='["git", "http", "https", "debug"]'
    fi
    
    # Generate manifest JSON
    cat > "${DIST_DIR}/manifest-${VARIANT}.json" << EOF
{
  "name": "@superstruct/libgit2.wasm",
  "version": "1.9.0",
  "variant": "${VARIANT}",
  "build_date": "$(date -Iseconds)",
  "emscripten_version": "${EMSCRIPTEN_VERSION}",
  "tier": 2,
  "dependencies": [
    "@superstruct/openssl.wasm"
  ],
  "files": {
    "wasm": "libgit2-${VARIANT}.wasm",
    "js": "libgit2-${VARIANT}.js",
    "wasm_size": ${WASM_SIZE},
    "js_size": ${JS_SIZE},
    "gzip_wasm_size": ${GZIP_WASM_SIZE},
    "gzip_js_size": ${GZIP_JS_SIZE},
    "total_size": $((WASM_SIZE + JS_SIZE)),
    "total_gzip_size": $((GZIP_WASM_SIZE + GZIP_JS_SIZE))
  },
  "features": ${FEATURES},
  "capabilities": {
    "simd": $([ "${VARIANT}" = "simd" ] && echo "true" || echo "false"),
    "threading": $([ "${VARIANT}" = "threaded" ] && echo "true" || echo "false"),
    "networking": true,
    "filesystem": true,
    "https": true,
    "ssh": false,
    "webgpu": false,
    "memory_min": 67108864,
    "memory_recommended": 268435456
  },
  "exports": {
    "git_libgit2_init": "number",
    "git_libgit2_shutdown": "number",
    "git_repository_open": "number",
    "git_repository_init": "number",
    "git_clone": "number",
    "git_status_list_new": "number",
    "git_commit_create": "number",
    "malloc": "number",
    "free": "void",
    "git_error_last": "number"
  },
  "build_info": {
    "cmake_build_type": "${CMAKE_BUILD_TYPE}",
    "emcc_flags": "${EMCC_FLAGS}",
    "git_commit": "$(git rev-parse HEAD 2>/dev/null || echo 'unknown')",
    "git_branch": "$(git branch --show-current 2>/dev/null || echo 'unknown')"
  }
}
EOF
    
    log "✅ Manifest generated: manifest-${VARIANT}.json"
}

# Create ecosystem-compatible loader
create_loader() {
    log "Creating ecosystem-compatible WASM loader..."
    
    cat > "${DIST_DIR}/libgit2-loader.js" << 'EOF'
/**
 * libgit2.wasm Ecosystem-Compatible Loader
 * Copyright 2025 Superstruct Ltd, New Zealand
 * Licensed under GPL v2 with Linking Exception (same as libgit2)
 */

export class LibGit2WASMLoader {
  constructor(basePath = './') {
    this.basePath = basePath.endsWith('/') ? basePath : basePath + '/';
  }

  async load(variant = 'release') {
    const manifest = await this.loadManifest(variant);
    const wasmPath = this.basePath + manifest.files.wasm;
    const jsPath = this.basePath + manifest.files.js;
    
    // Load WASM and JS files
    const [wasmBytes, jsModule] = await Promise.all([
      fetch(wasmPath).then(r => {
        if (!r.ok) throw new Error(`Failed to load WASM: ${r.status} ${r.statusText}`);
        return r.arrayBuffer();
      }),
      import(jsPath).catch(err => {
        throw new Error(`Failed to load JS module: ${err.message}`);
      })
    ]);

    // Create WebAssembly module
    const module = await WebAssembly.compile(wasmBytes);
    const instance = await WebAssembly.instantiate(module, this.getImports());
    
    return new LibGit2WASMInterface(instance, manifest);
  }

  async loadManifest(variant = 'release') {
    const manifestPath = this.basePath + `manifest-${variant}.json`;
    const response = await fetch(manifestPath);
    
    if (!response.ok) {
      throw new Error(`Failed to load manifest: ${response.status} ${response.statusText}`);
    }
    
    return response.json();
  }

  getImports() {
    return {
      env: {
        memory: new WebAssembly.Memory({ 
          initial: 1024,  // 64MB initial for filesystem operations
          maximum: 2048,  // 128MB maximum
          shared: false
        }),
        
        // System call stubs for WASM
        __syscall_openat: () => -1,
        __syscall_read: () => -1,
        __syscall_write: () => -1,
        __syscall_close: () => -1,
        __syscall_lseek: () => -1,
        __syscall_stat64: () => -1,
        __syscall_fstat64: () => -1,
        
        // Environment stubs
        getenv: () => 0,
        setenv: () => 0,
        
        // Time functions
        gettimeofday: () => Date.now(),
        
        // Math functions (if needed)
        cos: Math.cos,
        sin: Math.sin,
        exp: Math.exp,
        log: Math.log,
        sqrt: Math.sqrt,
        
        // Filesystem initialization callback
        __git_wasm_fs_init: this.initializeFilesystem.bind(this),
        
        // Virtual filesystem callbacks for libgit2
        __git_wasm_fs_mkdir: this.createDirectory.bind(this),
        __git_wasm_fs_rmdir: this.removeDirectory.bind(this),
        __git_wasm_fs_exists: this.pathExists.bind(this),
        __git_wasm_fs_read: this.readFile.bind(this),
        __git_wasm_fs_write: this.writeFile.bind(this),
        __git_wasm_fs_stat: this.statFile.bind(this)
      }
    };
  }

  // WASM-native filesystem pattern implementations
  initializeFilesystem() {
    // Initialize virtual filesystem for Git operations
    if (typeof FS !== 'undefined') {
      try {
        // Set up common Git directories in virtual filesystem
        FS.mkdir('/tmp');
        FS.mkdir('/home');
        FS.mkdir('/home/git');
        
        // Mount IndexedDB filesystem for persistence (browser only)
        if (typeof IDBFS !== 'undefined' && typeof window !== 'undefined') {
          FS.mkdir('/persist');
          FS.mount(IDBFS, {}, '/persist');
          
          // Sync existing data from IndexedDB
          return new Promise((resolve, reject) => {
            FS.syncfs(true, (err) => {
              if (err) {
                console.warn('Failed to sync IDBFS:', err);
                resolve(); // Continue without persistence
              } else {
                console.log('IDBFS synchronized successfully');
                resolve();
              }
            });
          });
        }
        
        return Promise.resolve();
      } catch (err) {
        console.warn('Virtual filesystem setup failed:', err);
        return Promise.resolve();
      }
    }
    
    return Promise.resolve();
  }
  
  createDirectory(pathPtr) {
    if (typeof FS === 'undefined') return -1;
    
    try {
      const path = this.readString(pathPtr);
      FS.mkdir(path);
      return 0;
    } catch (err) {
      console.warn('Failed to create directory:', err);
      return -1;
    }
  }
  
  removeDirectory(pathPtr) {
    if (typeof FS === 'undefined') return -1;
    
    try {
      const path = this.readString(pathPtr);
      FS.rmdir(path);
      return 0;
    } catch (err) {
      console.warn('Failed to remove directory:', err);
      return -1;
    }
  }
  
  pathExists(pathPtr) {
    if (typeof FS === 'undefined') return 0;
    
    try {
      const path = this.readString(pathPtr);
      const stat = FS.stat(path);
      return stat ? 1 : 0;
    } catch (err) {
      return 0;
    }
  }
  
  readFile(pathPtr, bufferPtr, sizePtr) {
    if (typeof FS === 'undefined') return -1;
    
    try {
      const path = this.readString(pathPtr);
      const data = FS.readFile(path);
      
      // Write size
      const sizeView = new Uint32Array(this.memory.buffer, sizePtr, 1);
      sizeView[0] = data.length;
      
      // Allocate buffer for file content
      const buffer = this.malloc(data.length);
      const bufferView = new Uint8Array(this.memory.buffer, buffer, data.length);
      bufferView.set(data);
      
      // Write buffer pointer  
      const bufferPtrView = new Uint32Array(this.memory.buffer, bufferPtr, 1);
      bufferPtrView[0] = buffer;
      
      return 0;
    } catch (err) {
      console.warn('Failed to read file:', err);
      return -1;
    }
  }
  
  writeFile(pathPtr, dataPtr, size) {
    if (typeof FS === 'undefined') return -1;
    
    try {
      const path = this.readString(pathPtr);
      const dataView = new Uint8Array(this.memory.buffer, dataPtr, size);
      FS.writeFile(path, dataView);
      
      // Sync to IndexedDB if available (browser persistence)
      if (typeof IDBFS !== 'undefined' && path.startsWith('/persist/')) {
        FS.syncfs(false, (err) => {
          if (err) console.warn('Failed to sync to IndexedDB:', err);
        });
      }
      
      return 0;
    } catch (err) {
      console.warn('Failed to write file:', err);
      return -1;
    }
  }
  
  statFile(pathPtr, statPtr) {
    if (typeof FS === 'undefined') return -1;
    
    try {
      const path = this.readString(pathPtr);
      const stat = FS.stat(path);
      
      // Write stat structure (simplified)
      // struct stat: mode, size, mtime
      const statView = new Uint32Array(this.memory.buffer, statPtr, 8);
      statView[0] = stat.mode || 0;
      statView[1] = stat.size || 0;
      statView[2] = Math.floor((stat.mtime || Date.now()) / 1000); // mtime in seconds
      statView[3] = 0; // padding
      
      return 0;
    } catch (err) {
      console.warn('Failed to stat file:', err);
      return -1;
    }
  }
}

export class LibGit2WASMInterface {
  constructor(instance, manifest) {
    this.instance = instance;
    this.manifest = manifest;
    this.memory = instance.exports.memory;
    this.exports = instance.exports;
    this._initialized = false;
  }

  // Standard WASM module interface
  malloc(size) {
    if (!this.exports.malloc) {
      throw new Error('malloc not available in WASM exports');
    }
    return this.exports.malloc(size);
  }

  free(ptr) {
    if (ptr && this.exports.free) {
      this.exports.free(ptr);
    }
  }

  getLastError() {
    if (!this.exports.git_error_last) {
      return 'git_error_last not available';
    }
    
    const errorPtr = this.exports.git_error_last();
    if (!errorPtr) return 'Unknown error';
    
    // git_error struct: { int klass; char* message; }
    const errorView = new Uint32Array(this.memory.buffer, errorPtr, 2);
    const messagePtr = errorView[1];
    
    return this.readString(messagePtr);
  }

  // Utility methods
  writeString(str) {
    const bytes = new TextEncoder().encode(str + '\0');
    const ptr = this.malloc(bytes.length);
    const view = new Uint8Array(this.memory.buffer, ptr, bytes.length);
    view.set(bytes);
    return ptr;
  }

  readString(ptr) {
    if (!ptr) return '';
    const view = new Uint8Array(this.memory.buffer, ptr);
    const end = view.indexOf(0);
    return new TextDecoder().decode(view.slice(0, end !== -1 ? end : view.length));
  }

  // libgit2-specific operations
  async initialize() {
    if (this._initialized) return;
    
    // Initialize WASM filesystem first
    if (this.exports.__git_wasm_fs_init) {
      await this.exports.__git_wasm_fs_init();
    }
    
    if (!this.exports.git_libgit2_init) {
      throw new Error('git_libgit2_init not available in WASM exports');
    }
    
    const result = this.exports.git_libgit2_init();
    if (result < 0) {
      throw new Error(`Failed to initialize libgit2: ${this.getLastError()}`);
    }
    
    this._initialized = true;
  }

  cleanup() {
    if (this._initialized && this.exports.git_libgit2_shutdown) {
      this.exports.git_libgit2_shutdown();
      this._initialized = false;
    }
  }

  // Repository operations
  async openRepository(path) {
    if (!this._initialized) await this.initialize();
    
    const pathPtr = this.writeString(path);
    const repoPtr = this.malloc(4); // Pointer to git_repository*
    
    try {
      const result = this.exports.git_repository_open(repoPtr, pathPtr);
      
      if (result !== 0) {
        throw new Error(`Failed to open repository: ${this.getLastError()}`);
      }
      
      const repo = new Uint32Array(this.memory.buffer, repoPtr, 1)[0];
      return new GitRepository(this, repo);
      
    } finally {
      this.free(pathPtr);
      this.free(repoPtr);
    }
  }

  async initRepository(path, isBare = false) {
    if (!this._initialized) await this.initialize();
    
    const pathPtr = this.writeString(path);
    const repoPtr = this.malloc(4);
    
    try {
      const result = this.exports.git_repository_init(repoPtr, pathPtr, isBare ? 1 : 0);
      
      if (result !== 0) {
        throw new Error(`Failed to init repository: ${this.getLastError()}`);
      }
      
      const repo = new Uint32Array(this.memory.buffer, repoPtr, 1)[0];
      return new GitRepository(this, repo);
      
    } finally {
      this.free(pathPtr);
      this.free(repoPtr);
    }
  }
}

export class GitRepository {
  constructor(wasm, repoPtr) {
    this.wasm = wasm;
    this.repoPtr = repoPtr;
  }

  free() {
    if (this.repoPtr && this.wasm.exports.git_repository_free) {
      this.wasm.exports.git_repository_free(this.repoPtr);
      this.repoPtr = null;
    }
  }

  // WASM filesystem integration methods
  async createWorkdir(path) {
    // Create working directory structure in virtual filesystem
    if (typeof FS !== 'undefined') {
      try {
        // Create .git directory structure
        const gitDir = `${path}/.git`;
        FS.mkdir(path, true);
        FS.mkdir(gitDir, true);
        FS.mkdir(`${gitDir}/objects`, true);
        FS.mkdir(`${gitDir}/refs`, true);
        FS.mkdir(`${gitDir}/refs/heads`, true);
        FS.mkdir(`${gitDir}/refs/tags`, true);
        
        // Create basic Git files
        FS.writeFile(`${gitDir}/HEAD`, 'ref: refs/heads/main\n');
        FS.writeFile(`${gitDir}/description`, 'Unnamed repository; edit this file to name the repository.\n');
        FS.writeFile(`${gitDir}/config`, '[core]\n\trepositoryformatversion = 0\n\tfilemode = true\n\tbare = false\n\tlogallrefupdates = true\n');
        
        // Sync to persistent storage if available
        if (typeof IDBFS !== 'undefined' && path.startsWith('/persist/')) {
          return new Promise((resolve, reject) => {
            FS.syncfs(false, (err) => {
              if (err) {
                console.warn('Failed to sync repository to IndexedDB:', err);
                resolve(); // Continue without persistence
              } else {
                console.log('Repository synced to IndexedDB');
                resolve();
              }
            });
          });
        }
        
        return Promise.resolve();
      } catch (err) {
        console.warn('Failed to create working directory:', err);
        throw new Error(`Failed to create working directory: ${err.message}`);
      }
    }
    
    return Promise.resolve();
  }
  
  async syncToPersistentStorage() {
    // Sync repository to IndexedDB for browser persistence
    if (typeof FS !== 'undefined' && typeof IDBFS !== 'undefined') {
      return new Promise((resolve, reject) => {
        FS.syncfs(false, (err) => {
          if (err) {
            console.warn('Failed to sync to persistent storage:', err);
            reject(new Error(`Sync failed: ${err.message}`));
          } else {
            console.log('Repository synced to persistent storage');
            resolve();
          }
        });
      });
    }
    
    return Promise.resolve();
  }
  
  async loadFromPersistentStorage() {
    // Load repository from IndexedDB
    if (typeof FS !== 'undefined' && typeof IDBFS !== 'undefined') {
      return new Promise((resolve, reject) => {
        FS.syncfs(true, (err) => {
          if (err) {
            console.warn('Failed to load from persistent storage:', err);
            resolve(); // Continue without loading
          } else {
            console.log('Repository loaded from persistent storage');
            resolve();
          }
        });
      });
    }
    
    return Promise.resolve();
  }

  // Add more Git operations as needed
  // async getStatus() { ... }
  // async createCommit() { ... }
  // async checkout() { ... }
}

// Default exports for ecosystem compatibility
export default LibGit2WASMLoader;
EOF

    log "✅ Ecosystem-compatible loader created"
}

# Create TypeScript definitions
create_typescript_definitions() {
    log "Creating TypeScript definitions..."
    
    cat > "${DIST_DIR}/libgit2.d.ts" << 'EOF'
/**
 * libgit2.wasm TypeScript Definitions
 * Copyright 2025 Superstruct Ltd, New Zealand
 * Licensed under GPL v2 with Linking Exception (same as libgit2)
 */

export interface LibGit2Manifest {
  name: string;
  version: string;
  variant: string;
  build_date: string;
  emscripten_version: string;
  tier: number;
  dependencies: string[];
  files: {
    wasm: string;
    js: string;
    wasm_size: number;
    js_size: number;
    gzip_wasm_size: number;
    gzip_js_size: number;
    total_size: number;
    total_gzip_size: number;
  };
  features: string[];
  capabilities: {
    simd: boolean;
    threading: boolean;
    networking: boolean;
    filesystem: boolean;
    https: boolean;
    ssh: boolean;
    webgpu: boolean;
    memory_min: number;
    memory_recommended: number;
  };
  exports: Record<string, string>;
  build_info: {
    cmake_build_type: string;
    emcc_flags: string;
    git_commit: string;
    git_branch: string;
  };
}

export declare class LibGit2WASMLoader {
  constructor(basePath?: string);
  load(variant?: string): Promise<LibGit2WASMInterface>;
  loadManifest(variant?: string): Promise<LibGit2Manifest>;
  getImports(): WebAssembly.Imports;
}

export declare class LibGit2WASMInterface {
  constructor(instance: WebAssembly.Instance, manifest: LibGit2Manifest);
  
  readonly instance: WebAssembly.Instance;
  readonly manifest: LibGit2Manifest;
  readonly memory: WebAssembly.Memory;
  readonly exports: any;

  // Memory management
  malloc(size: number): number;
  free(ptr: number): void;

  // Utility methods
  writeString(str: string): number;
  readString(ptr: number): string;
  getLastError(): string;

  // libgit2 operations
  initialize(): Promise<void>;
  cleanup(): void;
  openRepository(path: string): Promise<GitRepository>;
  initRepository(path: string, isBare?: boolean): Promise<GitRepository>;
}

export declare class GitRepository {
  constructor(wasm: LibGit2WASMInterface, repoPtr: number);
  
  readonly wasm: LibGit2WASMInterface;
  
  free(): void;
  
  // Git operations (to be implemented)
  // getStatus(): Promise<GitStatusEntry[]>;
  // createCommit(message: string): Promise<string>;
  // checkout(ref: string): Promise<void>;
}

export default LibGit2WASMLoader;
EOF

    log "✅ TypeScript definitions created"
}

# Create package.json for NPM publishing
create_package_json() {
    log "Creating package.json for NPM publishing..."
    
    cat > "${DIST_DIR}/package.json" << EOF
{
  "name": "@superstruct/libgit2.wasm",
  "version": "1.9.0",
  "description": "libgit2 compiled to WebAssembly for browser and Node.js",
  "type": "module",
  "main": "libgit2-loader.js",
  "types": "libgit2.d.ts",
  "files": [
    "*.wasm",
    "*.js",
    "*.json",
    "*.d.ts"
  ],
  "exports": {
    ".": {
      "import": "./libgit2-loader.js",
      "types": "./libgit2.d.ts"
    },
    "./release": "./libgit2-release.wasm",
    "./simd": "./libgit2-simd.wasm",
    "./debug": "./libgit2-debug.wasm"
  },
  "keywords": [
    "git",
    "libgit2",
    "wasm",
    "webassembly",
    "version-control",
    "superstruct"
  ],
  "author": "Superstruct Ltd <hello@superstruct.tech>",
  "license": "GPL-2.0-with-GCC-exception",
  "homepage": "https://github.com/superstruct/libgit2.wasm",
  "repository": {
    "type": "git",
    "url": "https://github.com/superstruct/libgit2.wasm.git"
  },
  "bugs": {
    "url": "https://github.com/superstruct/libgit2.wasm/issues"
  },
  "publishConfig": {
    "registry": "https://npm.pkg.github.com"
  },
  "peerDependencies": {
    "@superstruct/openssl.wasm": "^3.0.0"
  },
  "devDependencies": {
    "@types/emscripten": "^1.39.0"
  },
  "engines": {
    "node": ">=16.0.0"
  }
}
EOF

    log "✅ package.json created for NPM publishing"
}

# Main build process
main() {
    log "🚀 Starting libgit2.wasm production build"
    log "Variant: ${VARIANT}"
    log "Script directory: ${SCRIPT_DIR}"
    
    check_emscripten
    configure_build
    setup_environment
    configure_cmake
    build_library
    create_artifacts
    generate_manifest
    create_loader
    create_typescript_definitions
    create_package_json
    
    log "🎉 Build completed successfully!"
    log ""
    log "📦 Artifacts created in ${DIST_DIR}:"
    ls -la "${DIST_DIR}"
    
    log ""
    log "🔍 Build summary:"
    log "   Variant: ${VARIANT}"
    log "   WASM size: $(wc -c < "${DIST_DIR}/libgit2-${VARIANT}.wasm" | numfmt --to=iec-i)B"
    log "   JS size: $(wc -c < "${DIST_DIR}/libgit2-${VARIANT}.js" | numfmt --to=iec-i)B"
    log "   Gzipped total: $(gzip -c "${DIST_DIR}/libgit2-${VARIANT}.wasm" "${DIST_DIR}/libgit2-${VARIANT}.js" | wc -c | numfmt --to=iec-i)B"
}

# Execute main function
main "$@"