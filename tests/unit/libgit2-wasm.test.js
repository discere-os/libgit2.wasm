/**
 * libgit2.wasm Unit Tests
 * Copyright 2025 Superstruct Ltd, New Zealand
 * Licensed under GPL v2 with Linking Exception (same as libgit2)
 */

import { describe, it, expect, beforeAll, afterAll, beforeEach } from 'vitest';
import { LibGit2WASMLoader } from '../../dist/libgit2-loader.js';

describe('libgit2.wasm Unit Tests', () => {
  let git;
  let loader;
  
  beforeAll(async () => {
    // Set up WASM loader
    loader = new LibGit2WASMLoader('./dist/');
    
    // Load the release variant by default
    git = await loader.load('release');
    
    // Initialize libgit2
    await git.initialize();
  });

  afterAll(() => {
    git?.cleanup();
  });

  describe('Basic WASM Module', () => {
    it('should load WASM module successfully', () => {
      expect(git).toBeDefined();
      expect(git.instance).toBeDefined();
      expect(git.memory).toBeDefined();
      expect(git.exports).toBeDefined();
    });

    it('should have required exports', () => {
      const requiredExports = [
        'malloc',
        'free',
        'git_libgit2_init',
        'git_libgit2_shutdown',
        'git_repository_open',
        'git_repository_init',
        'git_error_last'
      ];

      requiredExports.forEach(exportName => {
        expect(git.exports[exportName]).toBeDefined();
        expect(typeof git.exports[exportName]).toBe('function');
      });
    });

    it('should have initialized libgit2', () => {
      expect(git._initialized).toBe(true);
    });
  });

  describe('Memory Management', () => {
    it('should allocate and free memory', () => {
      const size = 1024;
      const ptr = git.malloc(size);
      
      expect(ptr).toBeGreaterThan(0);
      expect(ptr % 4).toBe(0); // Should be aligned
      
      // Should not throw
      expect(() => git.free(ptr)).not.toThrow();
    });

    it('should handle null pointer in free', () => {
      expect(() => git.free(0)).not.toThrow();
      expect(() => git.free(null)).not.toThrow();
      expect(() => git.free(undefined)).not.toThrow();
    });

    it('should write and read strings correctly', () => {
      const testString = 'Hello, libgit2.wasm!';
      const ptr = git.writeString(testString);
      
      expect(ptr).toBeGreaterThan(0);
      
      const readString = git.readString(ptr);
      expect(readString).toBe(testString);
      
      git.free(ptr);
    });

    it('should handle empty strings', () => {
      const emptyString = '';
      const ptr = git.writeString(emptyString);
      
      expect(ptr).toBeGreaterThan(0);
      
      const readString = git.readString(ptr);
      expect(readString).toBe(emptyString);
      
      git.free(ptr);
    });

    it('should handle Unicode strings', () => {
      const unicodeString = 'Hello 世界 🌍 Мир';
      const ptr = git.writeString(unicodeString);
      
      expect(ptr).toBeGreaterThan(0);
      
      const readString = git.readString(ptr);
      expect(readString).toBe(unicodeString);
      
      git.free(ptr);
    });
  });

  describe('Error Handling', () => {
    it('should get error messages', () => {
      // Try to open a non-existent repository to trigger an error
      const pathPtr = git.writeString('/non/existent/repo');
      const repoPtr = git.malloc(4);
      
      const result = git.exports.git_repository_open(repoPtr, pathPtr);
      
      expect(result).not.toBe(0); // Should fail
      
      const error = git.getLastError();
      expect(error).toBeDefined();
      expect(typeof error).toBe('string');
      expect(error.length).toBeGreaterThan(0);
      
      git.free(pathPtr);
      git.free(repoPtr);
    });

    it('should handle no error condition', () => {
      // Call a successful operation first
      const result = git.exports.git_libgit2_init();
      expect(result).toBeGreaterThanOrEqual(0);
      
      // Error should be empty or indicate no error
      const error = git.getLastError();
      expect(error).toBeDefined();
    });
  });

  describe('Repository Operations', () => {
    const testRepoPath = '/tmp/test-repo-unit';

    it('should initialize a repository', async () => {
      const repo = await git.initRepository(testRepoPath, false);
      
      expect(repo).toBeDefined();
      expect(repo.repoPtr).toBeGreaterThan(0);
      
      repo.free();
    });

    it('should handle repository initialization errors', async () => {
      // Try to initialize in an invalid path
      const invalidPath = '/invalid/\0/path';
      
      await expect(git.initRepository(invalidPath, false))
        .rejects.toThrow();
    });

    it('should open an existing repository', async () => {
      // First initialize a repository
      const initRepo = await git.initRepository(testRepoPath + '-open', false);
      initRepo.free();
      
      // Then open it
      const repo = await git.openRepository(testRepoPath + '-open');
      
      expect(repo).toBeDefined();
      expect(repo.repoPtr).toBeGreaterThan(0);
      
      repo.free();
    });

    it('should handle open repository errors', async () => {
      const nonExistentPath = '/non/existent/repo/path';
      
      await expect(git.openRepository(nonExistentPath))
        .rejects.toThrow();
    });
  });

  describe('libgit2 Version and Features', () => {
    it('should report libgit2 version', () => {
      // Most libgit2 builds have version info
      if (git.exports.git_libgit2_version) {
        const version = git.exports.git_libgit2_version();
        expect(version).toBeDefined();
        expect(typeof version).toBe('number');
      }
    });

    it('should have HTTPS support', () => {
      // Check if HTTPS features are compiled in
      if (git.exports.git_libgit2_features) {
        const features = git.exports.git_libgit2_features();
        expect(features).toBeDefined();
        
        // HTTPS feature flag (value depends on libgit2 version)
        expect(features & 1).toBeTruthy(); // GIT_FEATURE_HTTPS
      }
    });
  });

  describe('Manifest Validation', () => {
    it('should have valid manifest', () => {
      const manifest = git.manifest;
      
      expect(manifest).toBeDefined();
      expect(manifest.name).toBe('@superstruct/libgit2.wasm');
      expect(manifest.version).toBeDefined();
      expect(manifest.capabilities).toBeDefined();
      expect(manifest.features).toBeInstanceOf(Array);
      expect(manifest.files).toBeDefined();
    });

    it('should have correct capabilities', () => {
      const capabilities = git.manifest.capabilities;
      
      expect(capabilities.networking).toBe(true);
      expect(capabilities.filesystem).toBe(true);
      expect(capabilities.https).toBe(true);
      expect(capabilities.ssh).toBe(false); // Disabled for browser compatibility
      expect(capabilities.webgpu).toBe(false); // Not applicable for libgit2
    });

    it('should have size information', () => {
      const files = git.manifest.files;
      
      expect(files.wasm_size).toBeGreaterThan(0);
      expect(files.js_size).toBeGreaterThan(0);
      expect(files.total_size).toBe(files.wasm_size + files.js_size);
    });
  });

  describe('Performance Tests', () => {
    it('should perform memory operations efficiently', () => {
      const iterations = 1000;
      const size = 1024;
      
      const start = performance.now();
      
      for (let i = 0; i < iterations; i++) {
        const ptr = git.malloc(size);
        git.free(ptr);
      }
      
      const end = performance.now();
      const timePerOp = (end - start) / iterations;
      
      // Should be fast (less than 1ms per allocation/free pair)
      expect(timePerOp).toBeLessThan(1.0);
    });

    it('should handle string operations efficiently', () => {
      const testString = 'Performance test string with some length to it';
      const iterations = 100;
      
      const start = performance.now();
      
      for (let i = 0; i < iterations; i++) {
        const ptr = git.writeString(testString);
        const result = git.readString(ptr);
        git.free(ptr);
        
        expect(result).toBe(testString);
      }
      
      const end = performance.now();
      const timePerOp = (end - start) / iterations;
      
      // Should be reasonably fast (less than 5ms per string operation)
      expect(timePerOp).toBeLessThan(5.0);
    });
  });

  describe('SIMD and Threading Tests', () => {
    it('should detect SIMD support', () => {
      // Test if SIMD is available in the build
      const hasSIMD = git.manifest.capabilities?.simd || false;
      
      if (hasSIMD) {
        // Test SIMD functions are available
        expect(git.exports.git_wasm_simd_memcpy).toBeDefined();
        expect(git.exports.git_wasm_simd_benchmark).toBeDefined();
        
        // Run SIMD benchmark if available
        if (git.exports.git_wasm_simd_benchmark) {
          const throughput = git.exports.git_wasm_simd_benchmark();
          expect(throughput).toBeGreaterThan(0);
          console.log(`SIMD benchmark: ${throughput.toFixed(2)} MB/s`);
        }
      } else {
        console.log('SIMD not available in this build variant');
      }
    });

    it('should detect threading support', async () => {
      // Test if threading is available in the build
      const hasThreading = git.manifest.capabilities?.threading || false;
      
      if (hasThreading) {
        // Test threading functions are available
        expect(git.exports.git_wasm_threading_init).toBeDefined();
        expect(git.exports.git_wasm_get_thread_count).toBeDefined();
        
        // Initialize threading
        if (git.exports.git_wasm_threading_init) {
          const initResult = git.exports.git_wasm_threading_init();
          expect(initResult).toBe(0);
          
          // Check thread count
          if (git.exports.git_wasm_get_thread_count) {
            const threadCount = git.exports.git_wasm_get_thread_count();
            expect(threadCount).toBeGreaterThan(0);
            expect(threadCount).toBeLessThanOrEqual(4); // MAX_WORKER_THREADS
            console.log(`Threading initialized with ${threadCount} threads`);
          }
          
          // Run threading benchmark if available
          if (git.exports.git_wasm_threading_benchmark) {
            const tasksPerSecond = git.exports.git_wasm_threading_benchmark();
            expect(tasksPerSecond).toBeGreaterThan(0);
            console.log(`Threading benchmark: ${tasksPerSecond.toFixed(2)} tasks/s`);
          }
          
          // Cleanup threading
          if (git.exports.git_wasm_threading_cleanup) {
            git.exports.git_wasm_threading_cleanup();
          }
        }
      } else {
        console.log('Threading not available in this build variant');
      }
    });

    it('should handle SIMD string operations', () => {
      if (!git.manifest.capabilities?.simd) {
        it.skip('SIMD not available');
        return;
      }

      if (git.exports.git_wasm_simd_strlen) {
        const testStrings = [
          'hello',
          'hello world',
          'a'.repeat(100),
          'libgit2.wasm SIMD test string'
        ];

        testStrings.forEach(str => {
          const strPtr = git.writeString(str);
          const simdLength = git.exports.git_wasm_simd_strlen(strPtr);
          
          expect(simdLength).toBe(str.length);
          
          git.free(strPtr);
        });
      }
    });

    it('should validate OIDs with SIMD', () => {
      if (!git.manifest.capabilities?.simd) {
        it.skip('SIMD not available');
        return;
      }

      if (git.exports.git_wasm_simd_validate_oid) {
        const validOids = [
          'da39a3ee5e6b4b0d3255bfef95601890afd80709',
          'aabbccddee1122334455667788990011223344ff',
          '1234567890abcdef1234567890abcdef12345678'
        ];

        const invalidOids = [
          'invalid',
          'da39a3ee5e6b4b0d3255bfef95601890afd8070', // too short
          'da39a3ee5e6b4b0d3255bfef95601890afd80709x', // too long
          'da39a3ee5e6b4b0d3255bfef95601890afd8070g' // invalid hex
        ];

        validOids.forEach(oid => {
          const oidPtr = git.writeString(oid);
          const isValid = git.exports.git_wasm_simd_validate_oid(oidPtr);
          expect(isValid).toBe(1);
          git.free(oidPtr);
        });

        invalidOids.forEach(oid => {
          const oidPtr = git.writeString(oid);
          const isValid = git.exports.git_wasm_simd_validate_oid(oidPtr);
          expect(isValid).toBe(0);
          git.free(oidPtr);
        });
      }
    });

    it('should handle async threaded operations', async () => {
      if (!git.manifest.capabilities?.threading) {
        it.skip('Threading not available');
        return;
      }

      // Initialize threading if not already done
      if (git.exports.git_wasm_threading_init) {
        git.exports.git_wasm_threading_init();
      }

      if (git.exports.git_wasm_async_validate_oid) {
        const testOid = 'da39a3ee5e6b4b0d3255bfef95601890afd80709';
        const oidPtr = git.writeString(testOid);
        
        const result = git.exports.git_wasm_async_validate_oid(oidPtr, 1000);
        expect(result).toBe(1);
        
        git.free(oidPtr);
      }

      // Cleanup threading
      if (git.exports.git_wasm_threading_cleanup) {
        git.exports.git_wasm_threading_cleanup();
      }
    });
  });

  describe('Stress Tests', () => {
    it('should handle multiple repository operations', async () => {
      const repoCount = 10;
      const repos = [];
      
      try {
        // Create multiple repositories
        for (let i = 0; i < repoCount; i++) {
          const repo = await git.initRepository(`/tmp/stress-test-${i}`, false);
          repos.push(repo);
        }
        
        expect(repos.length).toBe(repoCount);
        
        // Verify all repositories are valid
        repos.forEach(repo => {
          expect(repo.repoPtr).toBeGreaterThan(0);
        });
        
      } finally {
        // Clean up all repositories
        repos.forEach(repo => repo.free());
      }
    });

    it('should handle large memory allocations', () => {
      const largeSize = 10 * 1024 * 1024; // 10MB
      
      const ptr = git.malloc(largeSize);
      expect(ptr).toBeGreaterThan(0);
      
      // Write some data to verify the memory is accessible
      const view = new Uint8Array(git.memory.buffer, ptr, 1024);
      view.fill(0xFF);
      
      // Verify the data was written
      expect(view[0]).toBe(0xFF);
      expect(view[1023]).toBe(0xFF);
      
      git.free(ptr);
    });
  });
});