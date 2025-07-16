# VMSort

Successful yet impractical counting sort implementation that sorts using page faults.

### Core Concept

Each possible key value maps to a specific memory address, where writing to that address marks the key as present. The kernel's page fault handler lazily allocates memory when needed with a two-level bitmap tracking which pages contain data. 

Results are extracted by scanning the bitmap.

### Architecture

256 MiB user-space mapping divided into 128 chunks. 512 pages per chunk.

### Bitmap

- L0 Bitmap: 65,536 bits tracking individual keys (one bit per possible uint16_t value)
- L1 Bitmap: 1,024 bits tracking which 64-key blocks contain data (optimization for fast scanning)

### Speed

The aim of this project was to obtain speed with hardware-assisted virtual memory operations. In reality, the CPU generates a page fault exception that requires a ~200 cycle context switch. Additionally, every alloc_pages() call could trigger buddy allocator searches, page reclaims, memory compactions, and NUMA balancing decisions that make this infeasible.

## Conclusion

I believe hardware-assisted sorting is underexplored, and has the potential to yield massive speed gains when sorting large arrays (where the context switches are insignificant).

Here are some ideas on how to make this possible:
- batching multiple faults and handling them in parallel
- dedicated fault cores
- reduced context switch overhead
- predictive page allocation
- hardware-accelerated bitmap operations (DRAM embedded)
- support for large, sparse virtual address spaces
- efficient mixing of variable page sizes in the same mapping
- hardware assissted, NUMA-aware page movement (exists with migrate_pages() but clunky)



