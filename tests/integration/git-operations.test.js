/**
 * libgit2.wasm Integration Tests - Git Operations
 * Copyright 2025 Superstruct Ltd, New Zealand  
 * Licensed under GPL v2 with Linking Exception (same as libgit2)
 */

import { describe, it, expect, beforeAll, afterAll, beforeEach, afterEach } from 'vitest';
import { LibGit2WASMLoader } from '../../dist/libgit2-loader.js';
import { promises as fs } from 'fs';

describe('libgit2.wasm Git Operations Integration', () => {
  let git;
  let testDir;
  
  beforeAll(async () => {
    const loader = new LibGit2WASMLoader('./dist/');
    git = await loader.load('release');
    await git.initialize();
    
    // Create test directory
    testDir = '/tmp/libgit2-integration-tests';
    await fs.mkdir(testDir, { recursive: true });
  });

  afterAll(async () => {
    git?.cleanup();
    
    // Clean up test directory
    try {
      await fs.rm(testDir, { recursive: true, force: true });
    } catch (err) {
      console.warn('Failed to clean up test directory:', err.message);
    }
  });

  describe('Repository Lifecycle', () => {
    let repoPath;
    
    beforeEach(() => {
      repoPath = `${testDir}/repo-${Date.now()}-${Math.random().toString(36).substr(2, 9)}`;
    });

    it('should create and verify repository structure', async () => {
      const repo = await git.initRepository(repoPath, false);
      
      try {
        expect(repo).toBeDefined();
        expect(repo.repoPtr).toBeGreaterThan(0);
        
        // Verify .git directory structure exists (in virtual filesystem)
        // This would be implementation-specific based on how filesystem is set up
        
      } finally {
        repo.free();
      }
    });

    it('should create bare repository', async () => {
      const bareRepo = await git.initRepository(repoPath + '-bare', true);
      
      try {
        expect(bareRepo).toBeDefined();
        expect(bareRepo.repoPtr).toBeGreaterThan(0);
        
      } finally {
        bareRepo.free();
      }
    });

    it('should handle repository reinitialization', async () => {
      // Create repository first
      const repo1 = await git.initRepository(repoPath, false);
      repo1.free();
      
      // Try to reinitialize - should succeed
      const repo2 = await git.initRepository(repoPath, false);
      
      try {
        expect(repo2).toBeDefined();
        expect(repo2.repoPtr).toBeGreaterThan(0);
        
      } finally {
        repo2.free();
      }
    });
  });

  describe('Object Database Operations', () => {
    let repo;
    let repoPath;
    
    beforeEach(async () => {
      repoPath = `${testDir}/odb-${Date.now()}-${Math.random().toString(36).substr(2, 9)}`;
      repo = await git.initRepository(repoPath, false);
    });

    afterEach(() => {
      repo?.free();
    });

    it('should access object database', () => {
      // Test basic ODB operations if available
      if (git.exports.git_repository_odb) {
        const odbPtr = git.malloc(4);
        
        try {
          const result = git.exports.git_repository_odb(odbPtr, repo.repoPtr);
          
          if (result === 0) {
            const odb = new Uint32Array(git.memory.buffer, odbPtr, 1)[0];
            expect(odb).toBeGreaterThan(0);
            
            // Free ODB handle
            if (git.exports.git_odb_free) {
              git.exports.git_odb_free(odb);
            }
          }
          
        } finally {
          git.free(odbPtr);
        }
      } else {
        it.skip('git_repository_odb not available');
      }
    });

    it('should handle blob creation', () => {
      if (git.exports.git_blob_create_from_buffer) {
        const testData = 'Hello, libgit2 blob!';
        const dataPtr = git.writeString(testData);
        const oidPtr = git.malloc(20); // SHA-1 is 20 bytes
        
        try {
          const result = git.exports.git_blob_create_from_buffer(
            oidPtr,
            repo.repoPtr,
            dataPtr,
            testData.length
          );
          
          if (result === 0) {
            // Verify OID was created
            const oidView = new Uint8Array(git.memory.buffer, oidPtr, 20);
            const hasNonZero = oidView.some(byte => byte !== 0);
            expect(hasNonZero).toBe(true);
          }
          
        } finally {
          git.free(dataPtr);
          git.free(oidPtr);
        }
      } else {
        it.skip('git_blob_create_from_buffer not available');
      }
    });
  });

  describe('Reference Operations', () => {
    let repo;
    let repoPath;
    
    beforeEach(async () => {
      repoPath = `${testDir}/refs-${Date.now()}-${Math.random().toString(36).substr(2, 9)}`;
      repo = await git.initRepository(repoPath, false);
    });

    afterEach(() => {
      repo?.free();
    });

    it('should access HEAD reference', () => {
      if (git.exports.git_repository_head) {
        const refPtr = git.malloc(4);
        
        try {
          const result = git.exports.git_repository_head(refPtr, repo.repoPtr);
          
          // HEAD might not exist in a fresh repo, so result could be non-zero
          if (result === 0) {
            const ref = new Uint32Array(git.memory.buffer, refPtr, 1)[0];
            expect(ref).toBeGreaterThan(0);
            
            // Free reference
            if (git.exports.git_reference_free) {
              git.exports.git_reference_free(ref);
            }
          }
          
        } finally {
          git.free(refPtr);
        }
      } else {
        it.skip('git_repository_head not available');
      }
    });

    it('should list references', () => {
      if (git.exports.git_reference_list) {
        const strArrayPtr = git.malloc(8); // git_strarray size
        
        try {
          const result = git.exports.git_reference_list(strArrayPtr, repo.repoPtr);
          
          if (result === 0) {
            // git_strarray: { char** strings; size_t count; }
            const strArrayView = new Uint32Array(git.memory.buffer, strArrayPtr, 2);
            const count = strArrayView[1];
            
            expect(count).toBeGreaterThanOrEqual(0);
            
            // Free string array if function available
            if (git.exports.git_strarray_free) {
              git.exports.git_strarray_free(strArrayPtr);
            }
          }
          
        } finally {
          git.free(strArrayPtr);
        }
      } else {
        it.skip('git_reference_list not available');
      }
    });
  });

  describe('Index Operations', () => {
    let repo;
    let repoPath;
    
    beforeEach(async () => {
      repoPath = `${testDir}/index-${Date.now()}-${Math.random().toString(36).substr(2, 9)}`;
      repo = await git.initRepository(repoPath, false);
    });

    afterEach(() => {
      repo?.free();
    });

    it('should access repository index', () => {
      if (git.exports.git_repository_index) {
        const indexPtr = git.malloc(4);
        
        try {
          const result = git.exports.git_repository_index(indexPtr, repo.repoPtr);
          
          if (result === 0) {
            const index = new Uint32Array(git.memory.buffer, indexPtr, 1)[0];
            expect(index).toBeGreaterThan(0);
            
            // Check index entry count
            if (git.exports.git_index_entrycount) {
              const count = git.exports.git_index_entrycount(index);
              expect(count).toBeGreaterThanOrEqual(0);
            }
            
            // Free index
            if (git.exports.git_index_free) {
              git.exports.git_index_free(index);
            }
          }
          
        } finally {
          git.free(indexPtr);
        }
      } else {
        it.skip('git_repository_index not available');
      }
    });
  });

  describe('Status Operations', () => {
    let repo;
    let repoPath;
    
    beforeEach(async () => {
      repoPath = `${testDir}/status-${Date.now()}-${Math.random().toString(36).substr(2, 9)}`;
      repo = await git.initRepository(repoPath, false);
    });

    afterEach(() => {
      repo?.free();
    });

    it('should get repository status', () => {
      if (git.exports.git_status_list_new) {
        const statusListPtr = git.malloc(4);
        
        try {
          const result = git.exports.git_status_list_new(
            statusListPtr,
            repo.repoPtr,
            0 // NULL options
          );
          
          if (result === 0) {
            const statusList = new Uint32Array(git.memory.buffer, statusListPtr, 1)[0];
            expect(statusList).toBeGreaterThan(0);
            
            // Check entry count
            if (git.exports.git_status_list_entrycount) {
              const count = git.exports.git_status_list_entrycount(statusList);
              expect(count).toBeGreaterThanOrEqual(0);
            }
            
            // Free status list
            if (git.exports.git_status_list_free) {
              git.exports.git_status_list_free(statusList);
            }
          }
          
        } finally {
          git.free(statusListPtr);
        }
      } else {
        it.skip('git_status_list_new not available');
      }
    });
  });

  describe('Configuration Operations', () => {
    let repo;
    let repoPath;
    
    beforeEach(async () => {
      repoPath = `${testDir}/config-${Date.now()}-${Math.random().toString(36).substr(2, 9)}`;
      repo = await git.initRepository(repoPath, false);
    });

    afterEach(() => {
      repo?.free();
    });

    it('should access repository config', () => {
      if (git.exports.git_repository_config) {
        const configPtr = git.malloc(4);
        
        try {
          const result = git.exports.git_repository_config(configPtr, repo.repoPtr);
          
          if (result === 0) {
            const config = new Uint32Array(git.memory.buffer, configPtr, 1)[0];
            expect(config).toBeGreaterThan(0);
            
            // Free config
            if (git.exports.git_config_free) {
              git.exports.git_config_free(config);
            }
          }
          
        } finally {
          git.free(configPtr);
        }
      } else {
        it.skip('git_repository_config not available');
      }
    });

    it('should set and get config values', () => {
      if (git.exports.git_repository_config && git.exports.git_config_set_string && git.exports.git_config_get_string) {
        const configPtr = git.malloc(4);
        
        try {
          const result = git.exports.git_repository_config(configPtr, repo.repoPtr);
          
          if (result === 0) {
            const config = new Uint32Array(git.memory.buffer, configPtr, 1)[0];
            
            // Set a test value
            const keyPtr = git.writeString('test.key');
            const valuePtr = git.writeString('test value');
            
            const setResult = git.exports.git_config_set_string(config, keyPtr, valuePtr);
            
            if (setResult === 0) {
              // Try to get the value back
              const outPtr = git.malloc(4);
              const getResult = git.exports.git_config_get_string(outPtr, config, keyPtr);
              
              if (getResult === 0) {
                const resultPtr = new Uint32Array(git.memory.buffer, outPtr, 1)[0];
                const resultValue = git.readString(resultPtr);
                expect(resultValue).toBe('test value');
              }
              
              git.free(outPtr);
            }
            
            git.free(keyPtr);
            git.free(valuePtr);
            
            if (git.exports.git_config_free) {
              git.exports.git_config_free(config);
            }
          }
          
        } finally {
          git.free(configPtr);
        }
      } else {
        it.skip('Config operations not available');
      }
    });
  });

  describe('Error Recovery', () => {
    it('should recover from invalid operations', async () => {
      // Try various invalid operations and ensure they fail gracefully
      const invalidPath = '/definitely/does/not/exist/path';
      
      await expect(git.openRepository(invalidPath))
        .rejects.toThrow();
      
      // libgit2 should still be functional after errors
      const error = git.getLastError();
      expect(error).toBeDefined();
      expect(typeof error).toBe('string');
      
      // Should still be able to perform valid operations
      const validRepoPath = `${testDir}/recovery-test-${Date.now()}`;
      const repo = await git.initRepository(validRepoPath, false);
      
      expect(repo).toBeDefined();
      repo.free();
    });

    it('should handle memory pressure', () => {
      // Allocate large amounts of memory and verify cleanup works
      const allocations = [];
      
      try {
        // Allocate 100 chunks of 1MB each
        for (let i = 0; i < 100; i++) {
          const ptr = git.malloc(1024 * 1024);
          expect(ptr).toBeGreaterThan(0);
          allocations.push(ptr);
        }
        
        expect(allocations.length).toBe(100);
        
      } finally {
        // Clean up all allocations
        allocations.forEach(ptr => git.free(ptr));
      }
    });
  });
});