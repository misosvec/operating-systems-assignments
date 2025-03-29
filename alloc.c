#include "wrapper.h"
#define ALLOCATED 1
#define FREE 0
#define BLOCK_STATE_SIZE 1
#define BLOCK_HEADER_SIZE 5 // block_state + size
#define BLOCK_TAIL_SIZE 4	// pointer to start of block
#define BLOCK_METADATA_SIZE (BLOCK_HEADER_SIZE + BLOCK_TAIL_SIZE)

// writes 4 byte integer
void mwrite4(unsigned int addr, unsigned int val)
{
	uint8_t u1 = (uint8_t)(val >> 24);
	uint8_t u2 = (uint8_t)(val >> 16);
	uint8_t u3 = (uint8_t)(val >> 8);
	uint8_t u4 = (uint8_t)(val >> 0);
	mwrite(addr, u1);
	mwrite(addr + 1, u2);
	mwrite(addr + 2, u3);
	mwrite(addr + 3, u4);
}

void write_head(unsigned int addr, uint8_t block_state, unsigned int size)
{
	mwrite(addr, block_state);
	mwrite4(addr + BLOCK_STATE_SIZE, size);
}

void write_tail(unsigned int addr, unsigned int head_pointer)
{
	mwrite4(addr, head_pointer);
}

unsigned int mread4(unsigned int addr)
{
	uint8_t u1 = mread(addr);
	uint8_t u2 = mread(addr + 1);
	uint8_t u3 = mread(addr + 2);
	uint8_t u4 = mread(addr + 3);
	return (unsigned int)u1 << 24 | (unsigned int)u2 << 16 | (unsigned int)u3 << 8 | (unsigned int)u4 << 0;
}

void best_fit(unsigned int searched_size, unsigned int *best_fit_addr, unsigned int *best_fit_block_size)
{
	unsigned int block_loc = 0;

	while (1)
	{
		if (block_loc > msize() - 1 - BLOCK_METADATA_SIZE)
			break;

		uint8_t block_state = mread(block_loc);
		unsigned int block_size = mread4(block_loc + BLOCK_STATE_SIZE);
		if (block_state == 0 && block_size == 0)
			break;

		if (block_state == FREE && block_size >= searched_size && (*best_fit_block_size == 0 || block_size < *best_fit_block_size))
		{
			*best_fit_addr = block_loc;
			*best_fit_block_size = block_size;
		}

		block_loc = block_loc + BLOCK_METADATA_SIZE + block_size;
	}
}

void my_init(void)
{
	mwrite(0, 0);							   // 1 byte = block_state
	mwrite4(1, msize() - BLOCK_METADATA_SIZE); // 4 bytes = available space
	mwrite4(msize() - BLOCK_TAIL_SIZE, 0);	   // pointer to the start of block (block_state byte)
}

int my_alloc(unsigned int size)
{
	// we can not alloc more space than available
	if (size > msize() - BLOCK_METADATA_SIZE)
		return FAIL;

	unsigned int best_fit_addr;
	unsigned int best_fit_block_size = 0;
	best_fit(size, &best_fit_addr, &best_fit_block_size);

	if (best_fit_block_size == 0)
		return FAIL;

	unsigned int unused_size = best_fit_block_size - size;

	if (unused_size > BLOCK_METADATA_SIZE)
	{
		// alloc required block
		write_head(best_fit_addr, ALLOCATED, size);
		write_tail(best_fit_addr + BLOCK_HEADER_SIZE + size, best_fit_addr);
		// create new free block from unused space
		unsigned int unused_block_addr = best_fit_addr + BLOCK_METADATA_SIZE + size;
		write_head(unused_block_addr, FREE, unused_size - BLOCK_METADATA_SIZE);
		write_tail(unused_block_addr + unused_size - BLOCK_TAIL_SIZE, unused_block_addr);
	}
	else
	{
		// if unused space is too small (metadata can not fit), alloc larger block than required to fill up space
		write_head(best_fit_addr, ALLOCATED, best_fit_block_size);
		write_tail(best_fit_addr + BLOCK_HEADER_SIZE + best_fit_block_size, best_fit_addr);
	}
	// returns first usable address of block, right after header
	return (int)(best_fit_addr + BLOCK_HEADER_SIZE);
}

int my_free(unsigned int addr)
{
	// addr validation
	unsigned int valid_block_start_addr = 0;
	unsigned int valid_block_data_addr = valid_block_start_addr + BLOCK_HEADER_SIZE;

	while (1)
	{
		if (valid_block_data_addr == addr)
			break;
		if (valid_block_data_addr > addr)
			return FAIL;
		valid_block_start_addr = valid_block_start_addr + mread4(valid_block_start_addr + BLOCK_STATE_SIZE) + BLOCK_METADATA_SIZE;
		valid_block_data_addr = valid_block_start_addr + BLOCK_HEADER_SIZE;
	}

	// if addr is valid we can proceed further
	unsigned int block_start_addr = addr - BLOCK_HEADER_SIZE;
	uint8_t block_state = mread(block_start_addr);
	if (block_state != ALLOCATED)
		return FAIL;

	// mark current block as free
	mwrite(block_start_addr, FREE);

	unsigned int block_size = mread4(block_start_addr + BLOCK_STATE_SIZE);
	unsigned int block_tail_addr = block_start_addr + BLOCK_HEADER_SIZE + block_size;
	unsigned int next_block_addr = block_start_addr + block_size + BLOCK_METADATA_SIZE;

	// if next block is free we can merge it with current block
	if (next_block_addr < msize() - 1 - BLOCK_METADATA_SIZE && mread(next_block_addr) == FREE)
	{
		unsigned int next_block_size = mread4(next_block_addr + BLOCK_STATE_SIZE);
		unsigned int next_block_tail_addr = next_block_addr + BLOCK_HEADER_SIZE + next_block_size;
		block_size = block_size + next_block_size + BLOCK_METADATA_SIZE;
		block_tail_addr = next_block_tail_addr;
		write_head(block_start_addr, FREE, block_size);
		write_tail(next_block_tail_addr, block_start_addr);
	}

	// if previous block is free we can merge it with current block
	int prev_block_tail_addr = (int)addr - BLOCK_METADATA_SIZE;
	if (prev_block_tail_addr > 0)
	{
		unsigned int prev_block_addr = mread4(block_start_addr - BLOCK_TAIL_SIZE);
		if (mread(prev_block_addr) == FREE)
		{
			unsigned int prev_block_size = mread4(prev_block_addr + BLOCK_STATE_SIZE);
			write_head(prev_block_addr, FREE, prev_block_size + block_size + BLOCK_METADATA_SIZE);
			write_tail(block_tail_addr, prev_block_addr);
		}
	}

	return OK;
}
