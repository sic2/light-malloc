/**
* The following code is under GNU License.
* See attached License file or visit:
* https://gnu.org/licenses/gpl.html
*/

#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <math.h>
#include "memoryManagement.h"

// Define the type bool. 
typedef int bool;
#define true 1
#define false 0

// MACROS
#define MMAP(lenght) mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0)
#define ADDRESS_PLUS_OFFSET(address, offset) address + sizeof(offset)
#define ADDRESS_MINUS_OFFSET(address, offset) address - sizeof(offset)
#define GET_FLAG(attribute) attribute & MSB_TO_ONE
#define GET_SIZE(attribute) attribute & ALL_BITS_EXCEPT_FIRST
#define BYTES_TO_WORDS(attribute_in_bytes) (uint32_t) ceil(attribute_in_bytes / WORD_SIZE)
#define WORDS_TO_BYTES(attribute_in_words) (uint64_t) attribute_in_words * WORD_SIZE
#define NEXT_BLOCK(blockLinks) blockLinks->next
#define PREV_BLOCK(blockLinks) blockLinks->prev
#define MMAP_LENGTH(header) header->length
#define NEXT_MMAP_ADDRESS(header)header->nextMmap 

#define INIT_STRUCT(type, address) (struct type*) address

// MASKS
const uint32_t MSB_TO_ONE = 2147483648; // 2^31
const uint32_t ALL_BITS_EXCEPT_FIRST = 2147483647; // 2^i, i = 0..30
const uint32_t UPPER_LIMIT_SIZE = 2147483648 - 1;
const uint32_t LOWER_LIMIT_SIZE = 1;
const uint32_t MSB_TO_ZERO = 0;

// Constants
static const int DEFAULT_NUMBER_MMAP_PAGES = 1024; // size Page size varies from system to system.
static const double WORD_SIZE = 4.0; // Assumption.

// Static variables
static int thereIsAnMmapRegion = 0;

int numberFreeBlocks = 0;
uint64_t totalFreeSpace = 0;
int currentAllocatedMemory= 0;

// If larger block, just update. If block of size -largestFreeBlock- is 
// freed, then loop through all the free list to find the new largest free region.
// If freeList == NULL -> return 0.
int largestFreeBlock = 0; 

// Variables
static void* newMmapRegion = NULL;
static void* freeList = NULL;
static void* mmapsList = NULL;

// Prototypes
void mmapRegion(uint64_t size);
void *getFreeBlock(uint64_t size);
void initialiseFreeBlock(void* freeRegion, uint32_t size, void* prevFreeRegion, 
	void* nextFreeRegion, bool flag, bool setHeader);
void insertFreeBlock(void* newBlock, struct freeBlockLinks *prevBlockLinks);
void removeAllocatedBlock(struct freeBlockLinks *blockToBeRemovedLinks);
void *splitFreeBlock(void* freeBlock, uint64_t spaceLeft, uint32_t size, 
	void* currentFreeBlock, struct freeBlockLinks *currentBlockLinks);
void coalescingAndFree(void* ptr);
uint32_t minimumSize();
bool sufficientSize(uint32_t size);
bool coalesceMmapRegions(void* mmapsList, void* newMmapRegion, uint64_t lengthMmapRegion);
void setMmapFooter(void* newMmapRegion, uint64_t length);
void initialiseFreeMmapRegion(void* beginningFreeRegion, uint64_t sizeFreeRegion);
void allocateBlock(uint32_t size, uint32_t spaceLeft, 
	uint32_t minFreeRegionSize, uint32_t sizeFreeRegion,  
	void* freeBlock, void* currentFreeBlock, 
	struct freeBlockLinks *currentBlockLinks);
void findAndSetLargestFreeBlock();
void coalesceWithNeighbours(void* newAddr, uint32_t newSize, 
	struct freeBlockLinks *successorBlockLinks);
void coalesceWithNextBlock(void* newAddr, uint32_t newSize, 
	struct freeBlockLinks *successorBlockLinks);
void coalesceWithPrevBlock(void* newAddr, uint32_t newSize);
void updateNextBlockOnCoalescing(void* newAddr, uint64_t sizeOffset);
void setFooterBlockOnCoalescing(void* newAddr, uint64_t sizeOffset, uint32_t newSize);

/* 
==============
 METHODS
==============
 */

void *getMemory(int size) {
    
	if (size > (UPPER_LIMIT_SIZE * WORD_SIZE) || size < LOWER_LIMIT_SIZE) {
		printf("Size is out of bounds \n");
		return NULL;
	}
	/*
	 Check if it's first request of memory allocation.
	 If so then mmap
	*/
	if (!thereIsAnMmapRegion) {
		mmapRegion(size);
		thereIsAnMmapRegion = 1;
	}
	
	/*
	Find free block from the free list.
	If no free block is found, then mmap 
	a sufficient large region of memory
	and append it to the list of 
	mmap regions.
	*/
	void* block = getFreeBlock(size);

	if (block)
		currentAllocatedMemory += size; // From user perspective.

	return block;
}

void freeMemory(void *ptr) {
	/* 
	Read flag for coalescing with previous block
	Read size to get to the next block and check if it's free (coalescing)
	*/
	void *startBlock = ADDRESS_MINUS_OFFSET(ptr, blockHeader);
	struct blockHeader *header = INIT_STRUCT(blockHeader, startBlock);
	uint32_t size = GET_SIZE(header->attribute); // in words
	if(!freeList) {
		initialiseFreeBlock(startBlock, size, 
		startBlock, startBlock, false, false);
		freeList = startBlock;
		numberFreeBlocks++;

		totalFreeSpace += WORDS_TO_BYTES(size); // Stats
	} else {
		coalescingAndFree(startBlock);
	}

	currentAllocatedMemory -= WORDS_TO_BYTES(size);
}

void mmapRegion(uint64_t size) {
	uint64_t length; 
	bool mmapRegionsCoalesced = false; 
	// Make sure there's enough allocated memory.
	if (DEFAULT_NUMBER_MMAP_PAGES * sysconf(_SC_PAGE_SIZE) - 
		(sizeof(headerMmapRegion) + sizeof(blockHeader)) < size) {
		uint64_t numberPages = (uint64_t) (size / sysconf(_SC_PAGE_SIZE)) + 1;
		length = numberPages * sysconf(_SC_PAGE_SIZE);
	} else {
		length = DEFAULT_NUMBER_MMAP_PAGES * sysconf(_SC_PAGE_SIZE);
	}

	newMmapRegion = MMAP(length); // TODO: check for errors
	if (newMmapRegion == MAP_FAILED) {
		fprintf(stderr, "Memory overflow\n");
		exit(-1);
	}

	// 	Header Mmap region
	struct headerMmapRegion *headerMmap = INIT_STRUCT(headerMmapRegion, newMmapRegion);
	if (!mmapsList) {
		NEXT_MMAP_ADDRESS(headerMmap) = NULL;
	} else if (coalesceMmapRegions(mmapsList, newMmapRegion, length)) {
			mmapRegionsCoalesced = true; 
	} else {
		NEXT_MMAP_ADDRESS(headerMmap) = mmapsList; // Add the new mmap at front.
	}

	if(!mmapRegionsCoalesced) {
		headerMmap->length = BYTES_TO_WORDS(length);
		mmapsList = newMmapRegion; // Update the pointer of mmapsList.

		setMmapFooter(newMmapRegion, length);


		// Set first free region.
		void* beginningFreeRegion = ADDRESS_PLUS_OFFSET(newMmapRegion, headerMmapRegion); 
		// header free region and footer mmap to be excluded.
		uint64_t sizeFreeRegion = length - sizeof(headerMmapRegion) - (2 * sizeof(blockHeader)); 
		initialiseFreeMmapRegion(beginningFreeRegion, sizeFreeRegion);
	}

	numberFreeBlocks++; // Add one free block to the counter.
}

// size free region in bytes
void initialiseFreeMmapRegion(void* beginningFreeRegion, uint64_t sizeFreeRegion) {
	uint32_t sizeFreeRegionInWords = BYTES_TO_WORDS(sizeFreeRegion); // headerMmapsize in words.
	if(!freeList) {
		freeList = beginningFreeRegion; // Initialise the free list.
		initialiseFreeBlock(beginningFreeRegion, sizeFreeRegionInWords, 
			beginningFreeRegion, beginningFreeRegion, false, true);
	} else {
		// Add to free list.
		void *currentBlock = freeList;
		void* links = ADDRESS_PLUS_OFFSET(currentBlock, blockHeader);
		struct freeBlockLinks *currentBlockLinks = INIT_STRUCT(freeBlockLinks, links);

		void* prevBlock = PREV_BLOCK(currentBlockLinks);
		initialiseFreeBlock(beginningFreeRegion, sizeFreeRegionInWords, 
			prevBlock, currentBlock, false, true); 

		// Tell next block that its previous one is NOW free. 
		uint64_t sizeOffset = WORDS_TO_BYTES(sizeFreeRegionInWords);
		updateNextBlockOnCoalescing(beginningFreeRegion, sizeFreeRegion);

		insertFreeBlock(beginningFreeRegion, currentBlockLinks);
	}
}

// Lenght in bytes
void setMmapFooter(void* newMmapRegion, uint64_t length) {
	// Footer Mmap region - Flag (0) + size (0) to indicate end region (same struct than block header) 
	void* footer = ADDRESS_MINUS_OFFSET((newMmapRegion + length), blockHeader);
	struct blockHeader *footerMmap = INIT_STRUCT(blockHeader, footer);
	footerMmap->attribute = MSB_TO_ONE; // Previous region is free.
}

// lengthMmapRegion in bytes
bool coalesceMmapRegions(void* mmapsList, void* newMmapRegion, uint64_t lengthMmapRegion) {
	
	void* currentMmapRegion = mmapsList;
	struct headerMmapRegion *headerMmap = INIT_STRUCT(headerMmapRegion, currentMmapRegion);
	do {
		uint64_t oldLength = WORDS_TO_BYTES(headerMmap->length);
		void* endMmapRegion = currentMmapRegion + oldLength;
		if(endMmapRegion == newMmapRegion) { 
			// Coalesce mmap regions.
			uint32_t newLength = headerMmap->length + BYTES_TO_WORDS(lengthMmapRegion); // update size mmap region.
			headerMmap->length = newLength;
			// Initialize footer mmap
			setMmapFooter(currentMmapRegion, WORDS_TO_BYTES(newLength));

			// Calculate size new free region.
			void* beginningFreeRegion = ADDRESS_MINUS_OFFSET(endMmapRegion, blockHeader);
			uint64_t newFreeSpaceSize = lengthMmapRegion - sizeof(blockHeader); // size free region.
			struct blockHeader *footerPrevMmapRegion = INIT_STRUCT(blockHeader, beginningFreeRegion);
			uint32_t flag = GET_FLAG(footerPrevMmapRegion->attribute);

			// Coalesce with prev block if free.
			if (flag == MSB_TO_ONE) {
				void* previous = ADDRESS_MINUS_OFFSET(beginningFreeRegion, freeBlockFooter);
				struct freeBlockFooter *prevUsableBlockFooter = INIT_STRUCT(freeBlockFooter, previous); 
				uint64_t sizePrevBlock = WORDS_TO_BYTES(prevUsableBlockFooter->size); // Footer has no flag
				beginningFreeRegion = beginningFreeRegion - sizePrevBlock - sizeof(blockHeader);
				newFreeSpaceSize += sizeof(blockHeader) + sizePrevBlock;
				numberFreeBlocks--;

				totalFreeSpace += newFreeSpaceSize; // Stats

				uint32_t newFreeSpaceSizeInWords = BYTES_TO_WORDS(newFreeSpaceSize);
				coalesceWithPrevBlock(beginningFreeRegion, newFreeSpaceSizeInWords);
			} else {
				initialiseFreeMmapRegion(beginningFreeRegion, newFreeSpaceSize); 
			}
			return true; // coalescing.
		} else { // Check next mmap region
			currentMmapRegion = NEXT_MMAP_ADDRESS(headerMmap);
			headerMmap = currentMmapRegion;
		}
	
	} while (headerMmap != NULL && NEXT_MMAP_ADDRESS(headerMmap) != NULL);

	return false; // no coalescing.
}

void *getFreeBlock(uint64_t requestedSize) {
	// Transform size in bytes to size in words.
	uint32_t size = BYTES_TO_WORDS(requestedSize);

	if (!freeList) {
	 	mmapRegion(requestedSize);
	 	return getFreeBlock(requestedSize);
	}

	int numberTraversedNodes = 0;
	bool freeBlockFound = false;
	bool updateLargestFreeBlock = false;
	void* freeBlock;
	void* currentFreeBlock = freeList;

	/*
	 Traverse free list (circular double linked list)
	 Use next-fit.
	*/
	do {
		struct blockHeader *currentFreeRegionHeader = INIT_STRUCT(blockHeader, currentFreeBlock);
		uint32_t sizeFreeRegion = GET_SIZE(currentFreeRegionHeader->attribute);

		freeBlock = ADDRESS_PLUS_OFFSET(currentFreeBlock, blockHeader);
		struct freeBlockLinks *currentBlockLinks = INIT_STRUCT(freeBlockLinks, freeBlock);
		if (sizeFreeRegion >= size) {
			freeBlockFound = true;

			int sizeFreeRegionInBytes =  WORDS_TO_BYTES(sizeFreeRegion);
			// Larger blocks than largestFreeBlock can exist if mmap is called again.
			if (sizeFreeRegionInBytes >= largestFreeBlock) {
				updateLargestFreeBlock = true;
			}

			uint32_t spaceLeft; // in words
			uint32_t minFreeRegionSize = minimumSize();
			if (sufficientSize(size)) {
				spaceLeft = sizeFreeRegion - size;
			} else {
				spaceLeft = sizeFreeRegion - minFreeRegionSize;
			}

			allocateBlock(size, spaceLeft, minFreeRegionSize, 
				sizeFreeRegion, freeBlock, currentFreeBlock, currentBlockLinks);

			// Remove allocated block.
			if (numberFreeBlocks == 1) {
				freeList = NULL;
			} else {
				removeAllocatedBlock(currentBlockLinks);
			}
			numberFreeBlocks--;

			// Look for new largestFreeBlock
			if(numberFreeBlocks && updateLargestFreeBlock) {
				findAndSetLargestFreeBlock();
			}

		}
		// Step to the next free block in the free list.
		numberTraversedNodes++;
		currentFreeBlock = NEXT_BLOCK(currentBlockLinks);
	} while (numberTraversedNodes < numberFreeBlocks && !freeBlockFound);

	if (!freeBlockFound) {
	 	mmapRegion(requestedSize);
	 	return getFreeBlock(requestedSize);
	}

	return freeBlock;
}

// Check if the new region is big enough to store a new free block.
// if not do not split, but free list now points to next node.
void allocateBlock(uint32_t size, uint32_t spaceLeft, 
	uint32_t minFreeRegionSize, uint32_t sizeFreeRegion,  
	void* freeBlock, void* currentFreeBlock, 
	struct freeBlockLinks *currentBlockLinks) {

	if (spaceLeft > minFreeRegionSize) {
		if (sufficientSize(size)) {
			freeList = splitFreeBlock(freeBlock, spaceLeft, 
				size, currentFreeBlock, currentBlockLinks);
		} else {
			minFreeRegionSize -= BYTES_TO_WORDS(sizeof(freeBlockFooter));
			freeList = splitFreeBlock(freeBlock, spaceLeft, 
				minFreeRegionSize, currentFreeBlock, currentBlockLinks);
		}

		numberFreeBlocks++;
	} else { 
		// No split.
		// If successor is mmap footer, then set its flag to 0.
		totalFreeSpace -= WORDS_TO_BYTES(sizeFreeRegion);
		
		uint64_t sizeOffset = WORDS_TO_BYTES(sizeFreeRegion);
		void* next = ADDRESS_PLUS_OFFSET(currentFreeBlock + sizeOffset, blockHeader);
		struct blockHeader *nextBlockHeader = INIT_STRUCT(blockHeader, next);
		// check if last one (size = 0.)
		if (nextBlockHeader->attribute == MSB_TO_ONE) { 
			// Tell next that block is not free anymore.
			nextBlockHeader->attribute = MSB_TO_ZERO; 
		} else {
			// If next block is not last one, then just set its flag to zero and keep size.
			// Flag is automatically set to zero.
			nextBlockHeader->attribute = GET_SIZE(nextBlockHeader->attribute); 
		}

		freeList = NEXT_BLOCK(currentBlockLinks);
	}
}

void findAndSetLargestFreeBlock() {
	// traverse free list.
	largestFreeBlock = 0; // Reset largest free region.
	int numberTraversedNodes = 0;
	void* currentFreeBlock = freeList;
	do {
		struct blockHeader *currentFreeRegionHeader = INIT_STRUCT(blockHeader, currentFreeBlock);
		uint32_t sizeFreeRegion = GET_SIZE(currentFreeRegionHeader->attribute); // in words.
		uint64_t sizeFreeRegionInBytes = WORDS_TO_BYTES(sizeFreeRegion);
		if(sizeFreeRegionInBytes > largestFreeBlock) {
			largestFreeBlock = sizeFreeRegionInBytes;
		}

		void* freeBlock = ADDRESS_PLUS_OFFSET(currentFreeBlock, blockHeader);
		struct freeBlockLinks *currentBlockLinks = INIT_STRUCT(freeBlockLinks, freeBlock);

		numberTraversedNodes++;
		currentFreeBlock = NEXT_BLOCK(currentBlockLinks);
	} while (numberTraversedNodes < numberFreeBlocks);
}


// Split a large block into two sub-blocks.
// spaceLeft and size are in words
void *splitFreeBlock(void* blockToSplit, uint64_t spaceLeft,
		uint32_t size, void* currentFreeBlock, 
		struct freeBlockLinks *currentBlockLinks) {

	// Reinitialise header of first sub-block.
	void* block = ADDRESS_MINUS_OFFSET(blockToSplit, blockHeader);
	struct blockHeader *allocatedBlock = INIT_STRUCT(blockHeader, block);
	allocatedBlock->attribute = size;

	// Create and initialise new node.
	uint32_t sizeNewNode = spaceLeft - BYTES_TO_WORDS(sizeof(blockHeader));
 
 	uint64_t sizeOffset =  WORDS_TO_BYTES(size);
	void* endUserRequestedBlock = blockToSplit + sizeOffset;
	// initialise new block.
	void* prevBlock = PREV_BLOCK(currentBlockLinks);

	totalFreeSpace -= WORDS_TO_BYTES(spaceLeft);
	totalFreeSpace -= WORDS_TO_BYTES(size); // Stats

	initialiseFreeBlock(endUserRequestedBlock, sizeNewNode, 
		prevBlock, currentFreeBlock, false, true);
	insertFreeBlock(endUserRequestedBlock, currentBlockLinks);

	return endUserRequestedBlock;
}

// size in words.
// Flag one means block is free.
void initialiseFreeBlock(void* freeRegion, uint32_t size, void* prevFreeRegion, 
	void* nextFreeRegion, bool flag, bool setHeader) {
	
	totalFreeSpace += WORDS_TO_BYTES(size); // Stats

	 // Update largest free region.
	if (size > largestFreeBlock) {
		largestFreeBlock = WORDS_TO_BYTES(size);
	}

	if (setHeader) {
		struct blockHeader *header = INIT_STRUCT(blockHeader, freeRegion);
		header->attribute = size; // Set size. Size is max 2^30. Flag is 0 by default.
		if (flag)
			header->attribute |= MSB_TO_ONE; // Set flag to 1.
	}

	void* usableArea = ADDRESS_PLUS_OFFSET(freeRegion, blockHeader);
	struct freeBlockLinks *blockLinks = INIT_STRUCT(freeBlockLinks, usableArea);
	PREV_BLOCK(blockLinks) = prevFreeRegion; 
	NEXT_BLOCK(blockLinks) = nextFreeRegion;

	// Set footer free region.
	uint64_t sizeOffset = WORDS_TO_BYTES(size);
	void* footer = ADDRESS_MINUS_OFFSET(usableArea + sizeOffset, freeBlockFooter);
	struct freeBlockFooter *blockFooter = INIT_STRUCT(freeBlockFooter, footer);
	blockFooter->size = size;
}

// Insert new free block into the free list
void insertFreeBlock(void* newBlock, struct freeBlockLinks *prevBlockLinks) {
	void* usableArea = ADDRESS_PLUS_OFFSET(newBlock, blockHeader);
	struct freeBlockLinks *newBlockLinks = INIT_STRUCT(freeBlockLinks, usableArea);

	void* next = NEXT_BLOCK(newBlockLinks) + sizeof(blockHeader);
	struct freeBlockLinks *nextBlock = INIT_STRUCT(freeBlockLinks, next);
	PREV_BLOCK(nextBlock) = newBlock;

	void* prev = PREV_BLOCK(newBlockLinks) + sizeof(blockHeader);
	struct freeBlockLinks *prevBlock = INIT_STRUCT(freeBlockLinks, prev);
	NEXT_BLOCK(prevBlock) = newBlock;
	
}

// Remove allocated block from free list.
void removeAllocatedBlock(struct freeBlockLinks *blockToBeRemovedLinks) {
	void* prevBlock = PREV_BLOCK(blockToBeRemovedLinks);
	void* prevLinks = ADDRESS_PLUS_OFFSET(prevBlock, blockHeader);
	struct freeBlockLinks *prevBlockLinks = INIT_STRUCT(freeBlockLinks, prevLinks);
	NEXT_BLOCK(prevBlockLinks) = NEXT_BLOCK(blockToBeRemovedLinks);

	void* nextBlock = NEXT_BLOCK(blockToBeRemovedLinks);
	void* nextLinks = ADDRESS_PLUS_OFFSET(nextBlock, blockHeader);
	struct freeBlockLinks *nextBlockLinks = INIT_STRUCT(freeBlockLinks, nextLinks);
	PREV_BLOCK(nextBlockLinks) = PREV_BLOCK(blockToBeRemovedLinks);
}

bool sufficientSize(uint32_t size) {
	if (size < minimumSize())
		return false;
	else 
		return true;
}

uint32_t minimumSize() {
	uint32_t min = sizeof(blockHeader) + sizeof(freeBlockLinks) + sizeof(freeBlockFooter);
	return  BYTES_TO_WORDS(min);
}

void coalescingAndFree(void* block) {
	struct blockHeader *header = INIT_STRUCT(blockHeader, block);
	uint32_t size = GET_SIZE(header->attribute); // in words
	uint32_t flag = GET_FLAG(header->attribute);

	// State 0: Neighbour blocks are not free.
	// State 1: precedent block only is free
	// State 2: successive block only is free.
	// State 3: both blocks are free.
	int state = 0; 

	struct freeBlockLinks *successorBlockLinks = NULL; // update only if necessary

	// Initialise addr and size for no change. 
	void* newAddr = block; 
	uint32_t newSize = size;
	uint64_t sizeOffset;
	// Is precedent block free? 
	if (flag == MSB_TO_ONE) {
		state = 1;
		// In this case it's just about updating the size of the prev free block 
		// and removing the successive one if free.
		void* footer = ADDRESS_MINUS_OFFSET(block, freeBlockFooter);
		struct freeBlockFooter *prevBlockFooter = INIT_STRUCT(freeBlockFooter, footer);
		sizeOffset = WORDS_TO_BYTES(prevBlockFooter->size); // Footer has no flag.
		newAddr = block - sizeOffset - sizeof(blockHeader);
		newSize = prevBlockFooter->size + BYTES_TO_WORDS(sizeof(blockHeader)) + size;
	}

	// Is successive block free? 
	sizeOffset = WORDS_TO_BYTES(size);
	void* nextBlock = ADDRESS_PLUS_OFFSET(block + sizeOffset, blockHeader);
	struct blockHeader *nextHeader = INIT_STRUCT(blockHeader, nextBlock);
	if (nextHeader->attribute != MSB_TO_ZERO) { // check if next block is mmap footer or not.
		uint32_t nextSize = GET_SIZE(nextHeader->attribute);
		sizeOffset = WORDS_TO_BYTES(nextSize);
		void* nextNextBlock =  ADDRESS_PLUS_OFFSET(nextBlock + sizeOffset, blockHeader);

		struct blockHeader *nextNextHeader = INIT_STRUCT(blockHeader, nextNextBlock);
		if (nextNextHeader->attribute != MSB_TO_ZERO) {
			flag = GET_FLAG(nextNextHeader->attribute);
			if (flag == MSB_TO_ONE) {
				if (!state) {
					state = 2;
					// Do not change address, but Increase size
					newSize = size + BYTES_TO_WORDS(sizeof(blockHeader)) + nextSize;
				} else {
					state = 3;
					newSize = newSize + BYTES_TO_WORDS(sizeof(blockHeader)) + nextSize;
				}
				successorBlockLinks = nextBlock + sizeof(blockHeader);
				// IF NO PREV block is free:
				// Just update the pointers of the prev and next blocks 
				// relative to this block AND the size.
			}
		}
	} // Otherwise it's next block is the mmap footer.

	switch(state){
		case 0: {
			void *currentBlock = freeList;
			struct freeBlockLinks *currentBlockLinks = ADDRESS_PLUS_OFFSET(currentBlock, blockHeader);

			void* prevBlock = PREV_BLOCK(currentBlockLinks);
			initialiseFreeBlock(newAddr, newSize, 
				prevBlock, currentBlock, false, false);

			// Tell next block that its previous one is NOW free.
			sizeOffset = WORDS_TO_BYTES(size);
			updateNextBlockOnCoalescing(newAddr, sizeOffset);

			insertFreeBlock(newAddr, currentBlockLinks);
			numberFreeBlocks++;
			break;
		}
		case 1: {
			coalesceWithPrevBlock(newAddr, newSize);
			break;
		}
		case 2: {
			coalesceWithNextBlock(newAddr, newSize, successorBlockLinks);
			break;
		}
		case 3: {
			coalesceWithNeighbours(newAddr, newSize, successorBlockLinks);
			break;
		}
		default:
			printf("ERROR");
			exit(-1);
	}

	// Update total free space
	// When state == 0, the totalFreeSpace is updated when the block is initialised.
	totalFreeSpace += WORDS_TO_BYTES(size); // Stats
	if (state > 0)
		totalFreeSpace += sizeof(blockHeader); 
	
	// Largest free region
	if (newSize > largestFreeBlock)
		largestFreeBlock = newSize;

	freeList = newAddr;
}

void coalesceWithPrevBlock(void* newAddr, uint32_t newSize) {
	struct blockHeader *newBlock = INIT_STRUCT(blockHeader, newAddr);
	newBlock->attribute = newSize;

	uint64_t sizeOffset = WORDS_TO_BYTES(newSize);
	setFooterBlockOnCoalescing(newAddr, sizeOffset, newSize);
	updateNextBlockOnCoalescing(newAddr, sizeOffset);
}

void coalesceWithNextBlock(void* newAddr, uint32_t newSize, struct freeBlockLinks *successorBlockLinks) {
	struct blockHeader *newBlock = newAddr;
	newBlock->attribute = newSize;

	// Update links to freenodes.
	void* blockLinks = ADDRESS_PLUS_OFFSET(newAddr, blockHeader);
	struct freeBlockLinks *newBlockLinks = INIT_STRUCT(freeBlockLinks, blockLinks);
	PREV_BLOCK(newBlockLinks) = PREV_BLOCK(successorBlockLinks);
	NEXT_BLOCK(newBlockLinks) = NEXT_BLOCK(successorBlockLinks);

	// Prev block.
	void* prevLinks = ADDRESS_PLUS_OFFSET(PREV_BLOCK(newBlockLinks), blockHeader);
	struct freeBlockLinks *prevBlockLinks = INIT_STRUCT(freeBlockLinks, prevLinks);
	NEXT_BLOCK(prevBlockLinks) = newAddr;

	// Next block.
	void* nextLinks = ADDRESS_PLUS_OFFSET(NEXT_BLOCK(newBlockLinks), blockHeader);
	struct freeBlockLinks *nextBlockLinks = INIT_STRUCT(freeBlockLinks, nextLinks);
	PREV_BLOCK(prevBlockLinks) = newAddr;

	uint64_t sizeOffset = WORDS_TO_BYTES(newSize);
	setFooterBlockOnCoalescing(newAddr, sizeOffset, newSize);
	updateNextBlockOnCoalescing(newAddr, sizeOffset);
}

void coalesceWithNeighbours(void* newAddr, uint32_t newSize, struct freeBlockLinks *successorBlockLinks) {
	struct blockHeader *newBlock = INIT_STRUCT(blockHeader, newAddr);
	newBlock->attribute = newSize;
	uint64_t sizeOffset = WORDS_TO_BYTES(newSize);
	setFooterBlockOnCoalescing(newAddr, sizeOffset, newSize);
	updateNextBlockOnCoalescing(newAddr, sizeOffset);

	// Remove successor block
	removeAllocatedBlock(successorBlockLinks);
	numberFreeBlocks--;
}

void setFooterBlockOnCoalescing(void* newAddr, uint64_t sizeOffset, uint32_t newSize) {
	// Footer
	void* footerAddr = newAddr + sizeof(blockHeader) + sizeOffset - sizeof(freeBlockFooter);
	struct freeBlockFooter *footer = INIT_STRUCT(freeBlockFooter, footerAddr);
	footer->size = newSize;
}

void updateNextBlockOnCoalescing(void* newAddr, uint64_t sizeOffset) {
	// Next block
	void* nextBlock = ADDRESS_PLUS_OFFSET(newAddr + sizeOffset, blockHeader);
	struct blockHeader *nextBlockHeader = INIT_STRUCT(blockHeader, nextBlock);
	nextBlockHeader->attribute |= MSB_TO_ONE; // works for all blocks (also for mmap footer)
}
