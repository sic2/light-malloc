/*
Student: 100003610
Interface to manage allocated memory.
*/
#include <inttypes.h>

// Structs
struct headerMmapRegion {
	void* nextMmap;
	uint32_t length;
} headerMmapRegion;

struct freeBlockLinks {
	void* prev;
	void* next;
} freeBlockLinks;

struct freeBlockFooter {
	uint32_t size; 
} freeBlockFooter;

// Used for both allocated and free blocks.
struct blockHeader {
	uint32_t attribute;
} blockHeader;


// Functions
extern void *getMemory(int size);

extern void freeMemory(void *ptr);

// Variables
extern int numberFreeBlocks;
extern uint64_t totalFreeSpace;
extern int largestFreeBlock;
extern int currentAllocatedMemory;
