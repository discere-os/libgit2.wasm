/*
 * libgit2.wasm SIMD Optimizations
 * Copyright 2025 Superstruct Ltd, New Zealand
 * Licensed under GPL v2 with Linking Exception (same as libgit2)
 */

#include <git2.h>
#include <emscripten.h>
#include <string.h>
#include <stdint.h>

#ifdef GIT_WASM_SIMD

#include <wasm_simd128.h>

/* SIMD-optimized SHA-1 processing for Git objects */

/* SHA-1 constants for SIMD operations */
static const uint32_t SHA1_K[4] = {
    0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6
};

/* SIMD-optimized memory operations */
EMSCRIPTEN_KEEPALIVE
void git_wasm_simd_memcpy(void* dest, const void* src, size_t size) {
    if (!dest || !src || size == 0) return;
    
    const uint8_t* src_bytes = (const uint8_t*)src;
    uint8_t* dest_bytes = (uint8_t*)dest;
    
    /* Use SIMD for large blocks (>= 16 bytes) */
    if (size >= 16) {
        size_t simd_blocks = size / 16;
        
        for (size_t i = 0; i < simd_blocks; i++) {
            v128_t data = wasm_v128_load(src_bytes + i * 16);
            wasm_v128_store(dest_bytes + i * 16, data);
        }
        
        /* Handle remaining bytes */
        size_t remaining = size % 16;
        if (remaining > 0) {
            memcpy(dest_bytes + simd_blocks * 16, 
                   src_bytes + simd_blocks * 16, 
                   remaining);
        }
    } else {
        /* Use regular memcpy for small sizes */
        memcpy(dest, src, size);
    }
}

/* SIMD-optimized memory comparison */
EMSCRIPTEN_KEEPALIVE
int git_wasm_simd_memcmp(const void* ptr1, const void* ptr2, size_t size) {
    if (!ptr1 || !ptr2) return ptr1 == ptr2 ? 0 : -1;
    if (size == 0) return 0;
    
    const uint8_t* bytes1 = (const uint8_t*)ptr1;
    const uint8_t* bytes2 = (const uint8_t*)ptr2;
    
    /* Use SIMD for large blocks */
    if (size >= 16) {
        size_t simd_blocks = size / 16;
        
        for (size_t i = 0; i < simd_blocks; i++) {
            v128_t data1 = wasm_v128_load(bytes1 + i * 16);
            v128_t data2 = wasm_v128_load(bytes2 + i * 16);
            
            /* Compare vectors */
            v128_t cmp = wasm_i8x16_eq(data1, data2);
            
            /* Check if all lanes are equal */
            if (!wasm_v128_any_true(cmp)) {
                /* Found difference, fall back to byte comparison */
                return memcmp(bytes1 + i * 16, bytes2 + i * 16, 16);
            }
        }
        
        /* Handle remaining bytes */
        size_t remaining = size % 16;
        if (remaining > 0) {
            return memcmp(bytes1 + simd_blocks * 16, 
                         bytes2 + simd_blocks * 16, 
                         remaining);
        }
        
        return 0;
    } else {
        /* Use regular memcmp for small sizes */
        return memcmp(ptr1, ptr2, size);
    }
}

/* SIMD-optimized string length calculation */
EMSCRIPTEN_KEEPALIVE
size_t git_wasm_simd_strlen(const char* str) {
    if (!str) return 0;
    
    const uint8_t* bytes = (const uint8_t*)str;
    size_t len = 0;
    
    /* Use SIMD to find null terminator */
    v128_t zero = wasm_i8x16_splat(0);
    
    while (1) {
        /* Load 16 bytes */
        v128_t data = wasm_v128_load(bytes + len);
        
        /* Compare with zero */
        v128_t cmp = wasm_i8x16_eq(data, zero);
        
        /* Check if any byte is zero */
        if (wasm_v128_any_true(cmp)) {
            /* Found null terminator, find exact position */
            for (int i = 0; i < 16; i++) {
                if (bytes[len + i] == 0) {
                    return len + i;
                }
            }
        }
        
        len += 16;
        
        /* Safety check to prevent infinite loop */
        if (len > 1024 * 1024) {
            /* Fall back to regular strlen for very long strings */
            return strlen(str);
        }
    }
}

/* SIMD-optimized hash functions for Git objects */

/* Rotate left operation using SIMD */
static inline v128_t simd_rotl_32(v128_t x, int n) {
    v128_t left = wasm_i32x4_shl(x, n);
    v128_t right = wasm_u32x4_shr(x, 32 - n);
    return wasm_v128_or(left, right);
}

/* SIMD-optimized SHA-1 block processing (simplified) */
EMSCRIPTEN_KEEPALIVE
void git_wasm_simd_sha1_block(uint32_t hash[5], const uint8_t block[64]) {
    if (!hash || !block) return;
    
    /* Load hash state into SIMD registers */
    v128_t h0123 = wasm_v128_load((const void*)hash);
    uint32_t h4 = hash[4];
    
    /* Process block in 16-word chunks (simplified implementation) */
    uint32_t w[80];
    
    /* Copy block to working array */
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4] << 24) |
               ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) |
               ((uint32_t)block[i*4+3]);
    }
    
    /* Extend the working array using SIMD where possible */
    for (int i = 16; i < 80; i += 4) {
        /* Load 4 words at a time for SIMD processing */
        if (i + 3 < 80) {
            v128_t w_minus_3 = wasm_v128_load((const void*)&w[i-3]);
            v128_t w_minus_8 = wasm_v128_load((const void*)&w[i-8]);
            v128_t w_minus_14 = wasm_v128_load((const void*)&w[i-14]);
            v128_t w_minus_16 = wasm_v128_load((const void*)&w[i-16]);
            
            v128_t temp = wasm_v128_xor(w_minus_3, w_minus_8);
            temp = wasm_v128_xor(temp, w_minus_14);
            temp = wasm_v128_xor(temp, w_minus_16);
            
            /* Rotate left by 1 */
            temp = simd_rotl_32(temp, 1);
            
            wasm_v128_store((void*)&w[i], temp);
        } else {
            /* Handle remaining words individually */
            for (int j = i; j < 80; j++) {
                w[j] = w[j-3] ^ w[j-8] ^ w[j-14] ^ w[j-16];
                w[j] = (w[j] << 1) | (w[j] >> 31);
            }
            break;
        }
    }
    
    /* Main SHA-1 algorithm (simplified for demonstration) */
    uint32_t a = hash[0], b = hash[1], c = hash[2], d = hash[3], e = hash[4];
    
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = SHA1_K[0];
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = SHA1_K[1];
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = SHA1_K[2];
        } else {
            f = b ^ c ^ d;
            k = SHA1_K[3];
        }
        
        uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
        e = d;
        d = c;
        c = (b << 30) | (b >> 2);
        b = a;
        a = temp;
    }
    
    /* Update hash state */
    hash[0] += a;
    hash[1] += b;
    hash[2] += c;
    hash[3] += d;
    hash[4] += e;
}

/* SIMD-optimized Git object validation */
EMSCRIPTEN_KEEPALIVE
int git_wasm_simd_validate_oid(const char* oid_hex) {
    if (!oid_hex) return 0;
    
    /* OID should be exactly 40 hex characters */
    size_t len = git_wasm_simd_strlen(oid_hex);
    if (len != 40) return 0;
    
    /* Use SIMD to validate hex characters */
    const uint8_t* bytes = (const uint8_t*)oid_hex;
    v128_t char_0 = wasm_i8x16_splat('0');
    v128_t char_9 = wasm_i8x16_splat('9');
    v128_t char_a = wasm_i8x16_splat('a');
    v128_t char_f = wasm_i8x16_splat('f');
    v128_t char_A = wasm_i8x16_splat('A');
    v128_t char_F = wasm_i8x16_splat('F');
    
    for (int i = 0; i < 40; i += 16) {
        int remaining = (40 - i < 16) ? 40 - i : 16;
        
        /* Load characters */
        uint8_t temp_bytes[16] = {0};
        memcpy(temp_bytes, bytes + i, remaining);
        v128_t chars = wasm_v128_load((const void*)temp_bytes);
        
        /* Check if characters are valid hex */
        v128_t is_digit = wasm_v128_and(
            wasm_i8x16_ge(chars, char_0),
            wasm_i8x16_le(chars, char_9)
        );
        
        v128_t is_lower_hex = wasm_v128_and(
            wasm_i8x16_ge(chars, char_a),
            wasm_i8x16_le(chars, char_f)
        );
        
        v128_t is_upper_hex = wasm_v128_and(
            wasm_i8x16_ge(chars, char_A),
            wasm_i8x16_le(chars, char_F)
        );
        
        v128_t is_valid = wasm_v128_or(is_digit, wasm_v128_or(is_lower_hex, is_upper_hex));
        
        /* Check if all characters in this chunk are valid */
        for (int j = 0; j < remaining; j++) {
            if (!wasm_i8x16_extract_lane(is_valid, j)) {
                return 0;
            }
        }
    }
    
    return 1;
}

/* SIMD performance test */
EMSCRIPTEN_KEEPALIVE
double git_wasm_simd_benchmark(void) {
    const size_t test_size = 1024 * 1024; // 1MB
    const int iterations = 100;
    
    /* Allocate test data */
    uint8_t* src = (uint8_t*)malloc(test_size);
    uint8_t* dest = (uint8_t*)malloc(test_size);
    
    if (!src || !dest) {
        free(src);
        free(dest);
        return -1.0;
    }
    
    /* Initialize test data */
    for (size_t i = 0; i < test_size; i++) {
        src[i] = (uint8_t)(i & 0xFF);
    }
    
    /* Benchmark SIMD memcpy */
    double start_time = emscripten_get_now();
    
    for (int i = 0; i < iterations; i++) {
        git_wasm_simd_memcpy(dest, src, test_size);
    }
    
    double end_time = emscripten_get_now();
    
    /* Cleanup */
    free(src);
    free(dest);
    
    /* Return throughput in MB/s */
    double total_time = (end_time - start_time) / 1000.0; // Convert to seconds
    double total_data = (test_size * iterations) / (1024.0 * 1024.0); // MB
    
    return total_data / total_time;
}

#endif /* GIT_WASM_SIMD */