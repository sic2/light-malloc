/**
* The following code is under GNU License.
* See attached License file or visit:
* https://gnu.org/licenses/gpl.html
*/

/*
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


/**
 * @brief 	getMemory returns an allocated chunk of memory
 *			of a given size in bytes.
 *
 * @param	[in] size	The number of bytes to be allocated
 * @returns	[out] 		The allocated memory	
 */
extern void *getMemory(int size);

/**
* @brief	freeMemory deallocates a given chunk of memory
*			previously allocated using \c #getMemory()
*
* @param	[in] ptr	The allocated memory to be freed.
*/
extern void freeMemory(void *ptr);

// Statistical variables

/**
* numberFreeBlocks indicates the number of available free blocks
* that Light-Malloc has already pre-allocated.
*/
extern int numberFreeBlocks;

/**
* totalFreeSpace is the total amount of space, in bytes,
* that Light-Malloc has already pre-allocated.
*/
extern uint64_t totalFreeSpace;

/**
* largestFreeBlock is the size, in bytes, of the largest
* free block of memory that Light-Malloc has
* already pre-allocated.
*/
extern int largestFreeBlock;

/**
* currentAllocatedMemory is the total amount of space, in bytes,
* that Light-Malloc has allocated through \c #getMemory()
*/
extern int currentAllocatedMemory;
