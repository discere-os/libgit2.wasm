// Filesystem post-initialization for libgit2.wasm  
// Copyright 2025 Superstruct Ltd, New Zealand
// Licensed under GPL v2 with Linking Exception (same as libgit2)

// Expose filesystem utilities on Module object
Module.FS = FS;
Module.IDBFS = typeof IDBFS !== 'undefined' ? IDBFS : null;
Module.PATH = PATH;

// Convenience functions for Git operations
Module.syncToPersistentStorage = function() {
  return new Promise(function(resolve, reject) {
    if (Module.IDBFS) {
      FS.syncfs(false, function(err) {
        if (err) {
          console.error('libgit2.wasm: Failed to sync to persistent storage:', err);
          reject(err);
        } else {
          console.log('libgit2.wasm: Synced to persistent storage');
          resolve();
        }
      });
    } else {
      console.log('libgit2.wasm: No persistent storage available');
      resolve(); // No persistence available
    }
  });
};

Module.loadFromPersistentStorage = function() {
  return new Promise(function(resolve, reject) {
    if (Module.IDBFS) {
      FS.syncfs(true, function(err) {
        if (err) {
          console.error('libgit2.wasm: Failed to load from persistent storage:', err);
          reject(err);
        } else {
          console.log('libgit2.wasm: Loaded from persistent storage');
          resolve();
        }
      });
    } else {
      console.log('libgit2.wasm: No persistent storage available');
      resolve(); // No persistence available  
    }
  });
};

// Git-specific filesystem utilities
Module.createGitRepository = function(path) {
  return new Promise(function(resolve, reject) {
    try {
      // Create repository directory structure
      FS.mkdir(path, true);
      
      var gitDir = path + '/.git';
      FS.mkdir(gitDir, true);
      FS.mkdir(gitDir + '/objects', true);
      FS.mkdir(gitDir + '/refs', true);
      FS.mkdir(gitDir + '/refs/heads', true);
      FS.mkdir(gitDir + '/refs/tags', true);
      
      // Create basic Git files
      FS.writeFile(gitDir + '/HEAD', 'ref: refs/heads/main\n');
      FS.writeFile(gitDir + '/description', 
        'Unnamed repository; edit this file to name the repository.\n');
      FS.writeFile(gitDir + '/config', 
        '[core]\n\trepositoryformatversion = 0\n\tfilemode = true\n\tbare = false\n\tlogallrefupdates = true\n');
      
      console.log('libgit2.wasm: Created Git repository at', path);
      
      // Auto-sync if in persistent storage
      if (path.startsWith('/persist/') && Module.IDBFS) {
        Module.syncToPersistentStorage().then(resolve).catch(function(err) {
          console.warn('libgit2.wasm: Repository created but sync failed:', err);
          resolve(); // Repository was created, sync failure is not critical
        });
      } else {
        resolve();
      }
    } catch (e) {
      console.error('libgit2.wasm: Failed to create Git repository:', e);
      reject(e);
    }
  });
};

Module.listGitRepositories = function(basePath) {
  basePath = basePath || '/persist';
  var repositories = [];
  
  try {
    function findGitDirs(path) {
      try {
        var entries = FS.readdir(path);
        for (var i = 0; i < entries.length; i++) {
          var entry = entries[i];
          if (entry === '.' || entry === '..') continue;
          
          var fullPath = path + '/' + entry;
          var stat = FS.stat(fullPath);
          
          if (stat.isDir) {
            if (entry === '.git') {
              // Found a Git repository
              repositories.push(path);
            } else {
              // Recurse into subdirectories
              findGitDirs(fullPath);
            }
          }
        }
      } catch (e) {
        // Ignore errors when scanning directories
      }
    }
    
    findGitDirs(basePath);
  } catch (e) {
    console.warn('libgit2.wasm: Failed to scan for Git repositories:', e);
  }
  
  return repositories;
};

Module.deleteGitRepository = function(path) {
  return new Promise(function(resolve, reject) {
    try {
      function removeRecursive(path) {
        var stat = FS.stat(path);
        if (stat.isDir) {
          var entries = FS.readdir(path);
          for (var i = 0; i < entries.length; i++) {
            var entry = entries[i];
            if (entry !== '.' && entry !== '..') {
              removeRecursive(path + '/' + entry);
            }
          }
          FS.rmdir(path);
        } else {
          FS.unlink(path);
        }
      }
      
      removeRecursive(path);
      console.log('libgit2.wasm: Deleted Git repository at', path);
      
      // Auto-sync if in persistent storage
      if (path.startsWith('/persist/') && Module.IDBFS) {
        Module.syncToPersistentStorage().then(resolve).catch(function(err) {
          console.warn('libgit2.wasm: Repository deleted but sync failed:', err);
          resolve(); // Repository was deleted, sync failure is not critical
        });
      } else {
        resolve();
      }
    } catch (e) {
      console.error('libgit2.wasm: Failed to delete Git repository:', e);
      reject(e);
    }
  });
};

// File system utilities for debugging
Module.debugFS = {
  listDirectory: function(path) {
    try {
      var entries = FS.readdir(path);
      console.log('Directory listing for', path + ':', entries);
      return entries;
    } catch (e) {
      console.error('Failed to list directory', path + ':', e);
      return [];
    }
  },
  
  statPath: function(path) {
    try {
      var stat = FS.stat(path);
      console.log('Stat for', path + ':', {
        isFile: !stat.isDir,
        isDirectory: stat.isDir,
        size: stat.size,
        mtime: new Date(stat.mtime)
      });
      return stat;
    } catch (e) {
      console.error('Failed to stat', path + ':', e);
      return null;
    }
  },
  
  readTextFile: function(path) {
    try {
      var content = FS.readFile(path, { encoding: 'utf8' });
      console.log('Content of', path + ':', content);
      return content;
    } catch (e) {
      console.error('Failed to read', path + ':', e);
      return null;
    }
  }
};

console.log('libgit2.wasm: Filesystem integration ready');
console.log('libgit2.wasm: Available utilities:', Object.keys(Module).filter(k => 
  typeof Module[k] === 'function' && (k.startsWith('sync') || k.startsWith('load') || k.startsWith('create') || k.startsWith('list') || k.startsWith('delete'))
));