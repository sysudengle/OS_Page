#include <assert.h>
#include <string.h> 
#include "sim.h"
#include "pagetable.h"

// The top-level page table (also known as the 'page directory')
pgdir_entry_t pgdir[PTRS_PER_PGDIR]; 	//4096个，也就是4k个一级索引

// Counters for various events.
// Your code must increment these when the related events occur.
int hit_count = 0;
int miss_count = 0;
int ref_count = 0;
int evict_clean_count = 0;
int evict_dirty_count = 0;


////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

/*
 * Allocates a frame to be used for the virtual page represented by p.
 * If all frames are in use, calls the replacement algorithm's evict_fcn to
 * select a victim frame.  Writes victim to swap if needed, and updates 
 * pagetable entry for victim to indicate that virtual page is no longer in
 * (simulated) physical memory.
 *
 * Counters for evictions should be updated appropriately in this function.
 *
 * @param: second-level page table entry pointer
 * @return: frame number
 */
int allocate_frame(pgtbl_entry_t *p) {
	int fn = -1;
	int i;

	// select frame from physical memory
	for(i = 0; i < memsize; i++)
	{
		if(!coremap[i].in_use)
		{
			fn = i;
			break;
		}
	}

	// If all frame is in used, use schedule algorithm to select a frame
	if(fn == -1)
	{
		fn = evict_fcn();

		pgtbl_entry_t *victim = coremap[fn].pte;
		off_t swap_off = victim->swap_off;

		victim->frame &= (~PG_VALID);

		// counting
		if((victim->frame & PG_DIRTY)) evict_dirty_count++;
		else evict_clean_count++;

		// evicted and dirty page should be written to swap file
		if(!(victim->frame & PG_ONSWAP) || (victim->frame & PG_DIRTY))
		{
			victim->frame &= (~PG_DIRTY);
			off_t ret_off = swap_pageout((victim->frame)>>PAGE_SHIFT, swap_off);
			if(ret_off != -1)
			{
				victim->swap_off = ret_off;
			}
			else
			{
				perror("swap_pageout error\n");
				fprintf(stderr, "not on swap error");
				exit(1);
			}
		}


	}
		
	// coremap record for debug
	coremap[fn].in_use = 1;
	coremap[fn].pte = p;

	return fn;
}

/*
 * Initializes the top-level pagetable.
 * This function is called once at the start of the simulation.
 * For the simulation, there is a single "process" whose reference trace is 
 * being simulated, so there is just one top-level page table (page directory).
 * To keep things simple, we use a global array of 'page directory entries'.
 *
 * In a real OS, each process would have its own page directory, which would
 * need to be allocated and initialized as part of process creation.
 */
void init_pagetable() {
	int i;
	// Set all entries in top-level pagetable to 0, which ensures valid
	// bits are all 0 initially.
	for (i=0; i < PTRS_PER_PGDIR; i++) {
		pgdir[i].pde = 0;
	}
}

// For simulation, we get second-level pagetables from ordinary memory
// 二级目录初始化，对应malloc出一个table entry的数组，默认是PTRS_PER_PGTBL = 4096大小的，初始化所有元素的frame是0保证invalid，swap位也设置为0
pgdir_entry_t init_second_level() {
	int i;
	pgtbl_entry_t *pgtbl;
	pgdir_entry_t new_entry;

	// Allocating aligned memory ensures the low bits in the pointer must
	// be zero, so we can use them to store our status bits, like PG_VALID
	// 利用对齐可以使我们利用指针的低位
	if (posix_memalign((void **)&pgtbl, PAGE_SIZE, 
			   PTRS_PER_PGTBL*sizeof(pgtbl_entry_t)) != 0) {
		perror("Failed to allocate aligned memory for page table");
		exit(1);
	}

	for(i = 0; i < PTRS_PER_PGTBL; i++)
	{
		pgtbl[i].frame = 0;
		pgtbl[i].swap_off = INVALID_SWAP;
	}

	new_entry.pde = (uintptr_t)pgtbl | PG_VALID;

	return new_entry;
}

/* 
 * Initializes the content of a (simulated) physical memory frame when it 
 * is first allocated for some virtual address.  Just like in a real OS,
 * we fill the frame with zero's to prevent leaking information across
 * pages. 
 * 
 * In our simulation, we also store the the virtual address itself in the 
 * page frame to help with error checking.
 *
 */
// 初始化frame，将给定下标为frame的physical memory所有bytes设为0,并且用coremap绑定好virtual memory
// 到底是frame_number还是frame？
void init_frame(int frame_num, addr_t vaddr) {
	char *mem_ptr = &physmem[frame_num * SIMPAGESIZE];
	addr_t *vaddr_ptr = (addr_t *)(mem_ptr + sizeof(int));

	memset(mem_ptr, 0, SIMPAGESIZE);
	*vaddr_ptr = vaddr;
}

/*
 * Locate the physical frame number for the given vaddr using the page table.
 *
 * If the entry is invalid and not on swap, then this is the first reference 
 * to the page and a (simulated) physical frame should be allocated and 
 * initialized (using init_frame).  
 *
 * If the entry is invalid and on swap, then a (simulated) physical frame
 * should be allocated and filled by reading the page data from swap.
 *
 * Counters for hit, miss and reference events should be incremented in
 * this function.
 */
char *find_physpage(addr_t vaddr, char type) {

}

////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

void print_pagetbl(pgtbl_entry_t *pgtbl) {
	int i;
	int first_invalid, last_invalid;
	first_invalid = last_invalid = -1;

	for (i=0; i < PTRS_PER_PGTBL; i++) {
		if (!(pgtbl[i].frame & PG_VALID) && 
		    !(pgtbl[i].frame & PG_ONSWAP)) {
			if (first_invalid == -1) {
				first_invalid = i;
			}
			last_invalid = i;
		} else {
			if (first_invalid != -1) {
				printf("\t[%d] - [%d]: INVALID\n",
				       first_invalid, last_invalid);
				first_invalid = last_invalid = -1;
			}
			printf("\t[%d]: ",i);
			if (pgtbl[i].frame & PG_VALID) {
				printf("VALID, ");
				if (pgtbl[i].frame & PG_DIRTY) {
					printf("DIRTY, ");
				}
				printf("in frame %d\n",pgtbl[i].frame >> PAGE_SHIFT);
			} else {
				assert(pgtbl[i].frame & PG_ONSWAP);
				printf("ONSWAP, at offset %lu\n",pgtbl[i].swap_off);
			}			
		}
	}
	if (first_invalid != -1) {
		printf("\t[%d] - [%d]: INVALID\n", first_invalid, last_invalid);
		first_invalid = last_invalid = -1;
	}
}

void print_pagedirectory() {
	int i; // index into pgdir
	int first_invalid,last_invalid;
	first_invalid = last_invalid = -1;

	pgtbl_entry_t *pgtbl;

	for (i=0; i < PTRS_PER_PGDIR; i++) {
		if (!(pgdir[i].pde & PG_VALID)) {
			if (first_invalid == -1) {
				first_invalid = i;
			}
			last_invalid = i;
		} else {
			if (first_invalid != -1) {
				printf("[%d]: INVALID\n  to\n[%d]: INVALID\n", 
				       first_invalid, last_invalid);
				first_invalid = last_invalid = -1;
			}
			pgtbl = (pgtbl_entry_t *)(pgdir[i].pde & PAGE_MASK);
			printf("[%d]: %p\n",i, pgtbl);
			print_pagetbl(pgtbl);
		}
	}
}
