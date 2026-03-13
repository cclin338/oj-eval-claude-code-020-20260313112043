#include "buddy.h"
#define NULL ((void *)0)

#define MAX_RANK 16
#define PAGE_SIZE 4096
#define MAX_PAGES 32768

// Buddy system structure
typedef struct FreeArea {
    void *free_list[20000];  // Array to store free block addresses (increased for large allocations)
    int count;              // Number of free blocks
} FreeArea;

static FreeArea free_area[MAX_RANK + 1];
static void *base_addr = NULL;
static int total_pages = 0;
static char page_rank[MAX_PAGES];  // Static array to store rank of each page
                                   // 0 means free, >0 means allocated with that rank

// Helper function to get page index from address
static int addr_to_page_idx(void *p) {
    if (p < base_addr) return -1;
    long offset = (char *)p - (char *)base_addr;
    if (offset < 0 || offset % PAGE_SIZE != 0) return -1;
    int idx = offset / PAGE_SIZE;
    if (idx >= total_pages) return -1;
    return idx;
}

// Helper function to get address from page index
static void *page_idx_to_addr(int idx) {
    return (char *)base_addr + idx * PAGE_SIZE;
}

// Helper function to find buddy index
static int get_buddy_idx(int idx, int rank) {
    int block_size = 1 << (rank - 1);  // 2^(rank-1) pages
    return idx ^ block_size;
}

// Helper function to check if a block is free in the free list
static int is_block_in_free_list(int idx, int rank) {
    void *target = page_idx_to_addr(idx);
    for (int i = 0; i < free_area[rank].count; i++) {
        if (free_area[rank].free_list[i] == target) {
            return i;
        }
    }
    return -1;
}

// Remove a block from free list
static void remove_from_free_list(int rank, int list_idx) {
    for (int i = list_idx; i < free_area[rank].count - 1; i++) {
        free_area[rank].free_list[i] = free_area[rank].free_list[i + 1];
    }
    free_area[rank].count--;
}

int init_page(void *p, int pgcount) {
    base_addr = p;
    total_pages = pgcount;

    // Initialize free_area
    for (int i = 0; i <= MAX_RANK; i++) {
        free_area[i].count = 0;
    }

    // Initialize page_rank array (0 = free initially)
    for (int i = 0; i < pgcount; i++) {
        page_rank[i] = 0;
    }

    // Build free list by repeatedly adding largest possible blocks
    int remaining = pgcount;
    int current_idx = 0;

    while (remaining > 0) {
        int rank = MAX_RANK;
        int block_size = 1 << (rank - 1);

        // Find largest rank that fits and is properly aligned
        while ((block_size > remaining || (current_idx % block_size) != 0) && rank > 1) {
            rank--;
            block_size = 1 << (rank - 1);
        }

        // Add block to free list
        void *addr = page_idx_to_addr(current_idx);
        free_area[rank].free_list[free_area[rank].count++] = addr;

        current_idx += block_size;
        remaining -= block_size;
    }

    return OK;
}

void *alloc_pages(int rank) {
    // Validate rank
    if (rank < 1 || rank > MAX_RANK) {
        return ERR_PTR(-EINVAL);
    }

    // Find a free block of the requested rank or larger
    int alloc_rank = rank;
    while (alloc_rank <= MAX_RANK && free_area[alloc_rank].count == 0) {
        alloc_rank++;
    }

    if (alloc_rank > MAX_RANK) {
        return ERR_PTR(-ENOSPC);
    }

    // Get the block (take from end of list)
    void *addr = free_area[alloc_rank].free_list[--free_area[alloc_rank].count];

    // Split the block if necessary
    while (alloc_rank > rank) {
        alloc_rank--;
        int block_size = 1 << (alloc_rank - 1);
        void *buddy_addr = (char *)addr + block_size * PAGE_SIZE;
        free_area[alloc_rank].free_list[free_area[alloc_rank].count++] = buddy_addr;
    }

    // Mark the allocated pages with the allocation rank
    int idx = addr_to_page_idx(addr);
    int block_size = 1 << (rank - 1);
    for (int i = 0; i < block_size; i++) {
        page_rank[idx + i] = rank;
    }

    return addr;
}

int return_pages(void *p) {
    // Validate address
    if (p == NULL) {
        return -EINVAL;
    }

    int idx = addr_to_page_idx(p);
    if (idx < 0) {
        return -EINVAL;
    }

    // Get the rank of the block (must be > 0 if allocated)
    int rank = page_rank[idx];
    if (rank < 1 || rank > MAX_RANK) {
        return -EINVAL;
    }

    // Clear the allocation marks
    int block_size = 1 << (rank - 1);
    for (int i = 0; i < block_size; i++) {
        page_rank[idx + i] = 0;
    }

    // Try to merge with buddy repeatedly
    while (rank < MAX_RANK) {
        int buddy_idx = get_buddy_idx(idx, rank);

        // Buddy must be within valid range
        if (buddy_idx < 0 || buddy_idx >= total_pages) {
            break;
        }

        // Check if buddy is free (first page of buddy block must have page_rank == 0)
        if (page_rank[buddy_idx] != 0) {
            break;
        }

        // Check if buddy block is in the free list for this rank
        int buddy_list_idx = is_block_in_free_list(buddy_idx, rank);
        if (buddy_list_idx < 0) {
            break;
        }

        // All checks passed - merge the blocks
        remove_from_free_list(rank, buddy_list_idx);

        // Use lower index as the merged block start
        if (buddy_idx < idx) {
            idx = buddy_idx;
        }

        // Increase rank
        rank++;
    }

    // Add the (possibly merged) block to free list
    void *addr = page_idx_to_addr(idx);
    free_area[rank].free_list[free_area[rank].count++] = addr;

    return OK;
}

int query_ranks(void *p) {
    int idx = addr_to_page_idx(p);
    if (idx < 0) {
        return -EINVAL;
    }

    // If allocated, return stored rank
    if (page_rank[idx] > 0) {
        return page_rank[idx];
    }

    // If not allocated, find the maximum rank of free block containing this page
    for (int rank = MAX_RANK; rank >= 1; rank--) {
        int block_size = 1 << (rank - 1);
        for (int i = 0; i < free_area[rank].count; i++) {
            void *addr = free_area[rank].free_list[i];
            int free_idx = addr_to_page_idx(addr);
            if (idx >= free_idx && idx < free_idx + block_size) {
                return rank;
            }
        }
    }

    return -EINVAL;
}

int query_page_counts(int rank) {
    if (rank < 1 || rank > MAX_RANK) {
        return -EINVAL;
    }

    return free_area[rank].count;
}
