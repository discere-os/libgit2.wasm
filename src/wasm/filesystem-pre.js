// Filesystem pre-initialization for libgit2.wasm
// Copyright 2025 Superstruct Ltd, New Zealand
// Licensed under GPL v2 with Linking Exception (same as libgit2)

// Set up Module object for Emscripten
if (typeof Module === 'undefined') {
  var Module = {};
}

// Configure filesystem settings
Module.preRun = Module.preRun || [];
Module.preRun.push(function() {
  // Set up filesystem before main() runs
  if (typeof FS !== 'undefined') {
    // Create standard directories
    try {
      FS.mkdir('/tmp');
      FS.mkdir('/home'); 
      FS.mkdir('/persist');
      console.log('libgit2.wasm: Virtual filesystem directories created');
    } catch (e) {
      // Directories might already exist
      console.log('libgit2.wasm: Virtual filesystem directories already exist');
    }
    
    // Mount IDBFS for browser persistence
    if (typeof IDBFS !== 'undefined' && typeof window !== 'undefined') {
      try {
        FS.mount(IDBFS, {}, '/persist');
        console.log('libgit2.wasm: IDBFS mounted for persistent storage');
        
        // Try to sync existing data
        FS.syncfs(true, function(err) {
          if (err) {
            console.warn('libgit2.wasm: Failed to sync existing IDBFS data:', err);
          } else {
            console.log('libgit2.wasm: Existing IDBFS data synchronized');
          }
        });
      } catch (e) {
        console.warn('libgit2.wasm: Failed to mount IDBFS:', e);
      }
    }
    
    // Set up filesystem optimization
    FS.trackingDelegate = {
      'willMovePath': function(old_path, new_path) {
        console.debug('libgit2.wasm: Moving', old_path, 'to', new_path);
      },
      'onMovePath': function(old_path, new_path) {
        // Auto-sync moves in persistent storage
        if (new_path.startsWith('/persist/') && typeof IDBFS !== 'undefined') {
          FS.syncfs(false, function(err) {
            if (err) console.warn('libgit2.wasm: Auto-sync after move failed:', err);
          });
        }
      },
      'willDeletePath': function(path) {
        console.debug('libgit2.wasm: Deleting', path);
      },
      'onDeletePath': function(path) {
        // Auto-sync deletes in persistent storage
        if (path.startsWith('/persist/') && typeof IDBFS !== 'undefined') {
          FS.syncfs(false, function(err) {
            if (err) console.warn('libgit2.wasm: Auto-sync after delete failed:', err);
          });
        }
      }
    };
  }
});

// Error handling setup
Module.printErr = function(text) {
  console.error('libgit2.wasm:', text);
};

Module.print = function(text) {
  console.log('libgit2.wasm:', text);
};