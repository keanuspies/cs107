/*
 * File: allocator.c
 * Author: Renner Leite Lucena && Keanu Phillip Spies
 * --------------------------------------------------
 * Cointains a heap-allocator that took alot of time,
 * energy, but also provided alot of fun :)
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include "allocator.h"
#include "segment.h"

// Heap blocks are required to be aligned to 8-byte boundary
#define ALIGNMENT 8
#define SIZE_MASK 0x7ffffffc
#define FREE_MASK 0x80000000
#define PREV_FREE 0x00000001
#define NEXT_FREE 0x00000002
#define INIT_MASK 0xfffffffe
#define NUM_BUCKETS 15
#define INT_BITS 32
#define INIT_PAGES 1 

#pragma pack(1)

typedef struct {
   unsigned int payloadsz;
   unsigned int prevpayloadsz; 
} headerT;

// global variables
void *buckets[NUM_BUCKETS]; // explicit segregated free-lists
void *max_block; // pointer to the largest block in the heap
void *min_block; // pointer to the smallest block in the heap

// Very efficient bitwise round of sz up to nearest multiple of mult
// does this by adding mult-1 to sz, then masking off the
// the bottom bits to compute least multiple of mult that is
// greater/equal than sz, this value is returned
// NOTE: mult has to be power of 2 for the bitwise trick to work!
static inline size_t roundup(size_t sz, size_t mult)
{    
    return (sz + mult-1) & ~(mult-1);
}

/** Given a pointer to start of payload, simply back up
  * to access its block header
  */
static inline headerT *hdr_for_payload(void *payload)
{
    return (headerT *)((char *)payload - sizeof(headerT));
}

/** Given a pointer to block header, advance past
  * header to access start of payload
  */
static inline void *payload_for_hdr(headerT *header)
{
    return (char *)header + sizeof(headerT);
}

/**
 * Helper function that sets the payload size in the header of payload to 
 * value 
 */
static inline void set_payload_size(void* payload, unsigned int value)
{
    (hdr_for_payload(payload))->payloadsz = value;
}

/**
 * Helper function that sets the previous payload's size in the header 
 * of payload to value 
 */
static inline void set_prevpayload_size(void* payload, unsigned int value)
{
    (hdr_for_payload(payload))->prevpayloadsz = value;
}

/**
 * Given a pointer to the payload of a block, returns the size of the
 * payload in the block
 */
static inline unsigned int get_size(void *payload){
    return (hdr_for_payload(payload)->payloadsz&SIZE_MASK);
}

/**
 * Given a pointer to the payload of a block, returns the stored size
 * in a block, including the flags of that block
 */
static inline unsigned int get_payloadsz(void *payload){
    return (hdr_for_payload(payload)->payloadsz);
}

/**
 * Given a pointer to th payload of a block, returns the size of the 
 * block directly under-neath the current block
 */
static inline unsigned int get_prev_size(void *payload){
    return (hdr_for_payload(payload)->prevpayloadsz&SIZE_MASK);
}

/**
 * Given a value of a payload's size , calculates the bucket 
 * to place a payload of that size in the segregated free-ist
 */
static inline int cal_bucket(unsigned int value){
    int bucket = INT_BITS -__builtin_clz(value);
    if(bucket >= NUM_BUCKETS + 2) return NUM_BUCKETS - 1;
    return bucket - 3;
}

/**
 * Given a pointer to the payload of a free-block, sets the
 * next pointer in the block to point to the block pointed
 * to by dest
 */
static inline void set_next_in_list(void* payload, void* dest)
{
    *(void**)payload = dest;
}

/**
 * Given a pointer to the payload of a free-block, sets the
 * previous pointer in the block to point to the block pointed
 * to by dest
 */
static inline void set_prev_in_list(void* payload, void* dest)
{
    *(void**)((char*)payload + sizeof(void*)) = dest;
}

/**
 * Returns the next block of the free-list following the block
 * to which payload belongs
 */
static inline void *get_next_in_list(void *payload){
    return *(void**)payload;
}

/**
 * Returns the previous block in the free-list preceeding the 
 * block to which payload belongs
 */
static inline void *get_prev_in_list(void *payload){
    return *(void **)((char *)payload + sizeof(void *));
}

/**
 * Returns the block directly above a given blocks payload
 */
static inline void *get_next(void *block){
    return  (char *)block + get_size(block); 
}

/**
 * Returns the block directly below a given blocks payload
 */
static inline void *get_prev(void *block){
    return (char *)hdr_for_payload(block) - get_prev_size(block);
}

/**
 * Sets the flag of the block above 'block' to ensure that it 
 * knows that the block below it is free
 */
static inline void set_prevfree(void* block){
    ((headerT *)(get_next(block)))->payloadsz |= PREV_FREE;
}

/**
 * Sets the flag of the block below 'block' to ensure that it 
 * knows that the block above it is free
 */
static inline void set_nextfree(void* block){  
    hdr_for_payload(get_prev(block))->payloadsz |= NEXT_FREE;
}

/**
 * Sets the flag of a given block to show that it is presently
 * free
 */
static inline void set_free(void * payload){
    set_payload_size(payload, (get_payloadsz(payload) | FREE_MASK));
}

/**
 * Returns if a given block is free or not, going down from 
 * the payload to read the flag from the header
 */
static inline bool is_free(void * payload){
    return ((get_size(payload)&FREE_MASK) != 0);
}

/**
 * Return if the block has a free block above it 
 */
static inline bool has_next_free(void *payload){
    return ((get_payloadsz(payload)&NEXT_FREE) != 0);
}

/**
 * Returns if the block has a free block below it
 */
static inline bool has_prev_free(void * payload){
    return ((get_payloadsz(payload)&PREV_FREE) != 0);
}

/**
 * Sets all of the buckets of the segregated freelist 
 * to NULL to initialise it.
 */
static inline void clear_buckets(){
    for(int i = 0; i < NUM_BUCKETS; i ++){
        *(void **)((char*)buckets + i*sizeof(void*)) = NULL;
    }
}

void *coalesce(void *ptr);

/**
 * Function: remove_from_list
 * --------------------------
 * Removes the block pointed to by curr from the list given by
 * bucket_num from that segregated list.
 */
static inline void remove_from_list(void *curr, int bucket_num){
    //gets prev_free and next_free to remove block from the list
    void* prev_free = get_prev_in_list(curr);
    void* next_free = get_next_in_list(curr);
    //prev gets next
    if(prev_free != NULL){ 
        set_next_in_list(prev_free, next_free);
    } else{
        buckets[bucket_num] = next_free;
    } 
    //next gets prev
    if(next_free != NULL) set_prev_in_list(next_free, prev_free);
}

/* The responsibility of the myinit function is to configure a new
 * empty heap. Typically this function will initialize the
 * segment (you decide the initial number pages to set aside, can be
 * zero if you intend to defer until first request) and set up the
 * global variables for the empty, ready-to-go state. The myinit
 * function is called once at program start, before any allocation 
 * requests are made. It may also be called later to wipe out the current
 * heap contents and start over fresh. This "reset" option is specifically
 * needed by the test harness to run a sequence of scripts, one after another,
 * without restarting program from scratch.
 */
bool myinit()
{   
    //empty buckets
    clear_buckets();
    //initialize the first block
    void *first = init_heap_segment(INIT_PAGES);
    if(first == NULL) return false;
    first = payload_for_hdr(first);
    max_block = first;
    min_block = first;
    // set the sizes
    set_payload_size(first, (INIT_PAGES*PAGE_SIZE - sizeof(headerT))|FREE_MASK); 
    set_prevpayload_size(first, INIT_MASK);
     // add the first segment to the bucket-list
    int bucket = cal_bucket(get_size(first));
    set_next_in_list(first, NULL);
    set_prev_in_list(first, NULL);
    buckets[bucket] = first;
    return true;
}

/**
 * Function: get_free_space
 * ------------------------
 * Searches through the free-list given by bucket initially to find a free space 
 * large enough for the requested size, if no space is found goes up the buckets 
 * to find any space that can fit, retuns a space if available else returns NULL
 * in which case a new page is required
 */
static inline void *get_free_space(int bucket, int* bucketNo,  size_t requestedsz){
    // start at the bucket and iterate through until right size is found
    for (int i = bucket; i < NUM_BUCKETS; i++){
        void *curr = buckets[i]; 
        while(curr != NULL){
            if(get_size(curr) >= requestedsz){ 
                remove_from_list(curr, i);
                *bucketNo = i;
                return curr;
            }
            curr = get_next_in_list(curr);
        }  
    }
    //return NULL if there is not a large enough block
    return NULL;
}

/**
 * Function: get_new_page
 * ----------------------
 * Makes a new page in order to store the requestedsz, handles the case of 
 * a perfect fit, a garbage block or a freeable block. Calls the page 
 * handler to extend the heap segment. Returns a pointer to the base payload
 * of the page which malloc will return. 
 */
void *get_new_page(size_t requestedsz){
        size_t npages = roundup(requestedsz + sizeof(headerT), PAGE_SIZE)/PAGE_SIZE;
        headerT *header = extend_heap_segment(npages);
        void *page = payload_for_hdr(header);
        // set sizes
        set_payload_size(page, requestedsz);
        set_prevpayload_size(page, get_size(max_block));
        // remember if max is free
        if((get_size(max_block)&FREE_MASK) != 0) set_prevfree(page); 
        // perfect fit
        int size_left = npages*PAGE_SIZE - requestedsz - sizeof(headerT); 
        if(size_left== 0){
            max_block = page;
            return page;
        }
        // garbage
        if(size_left < sizeof(headerT)+ 2*sizeof(void *)){
            void* next_free = get_next(page);
            next_free = payload_for_hdr(next_free);
            set_payload_size(next_free, (npages*PAGE_SIZE - requestedsz - 2*sizeof(headerT))|FREE_MASK);
            set_prevpayload_size(next_free, requestedsz); 
            set_nextfree(next_free);
            max_block = next_free;
            return page;
        }
        // fit with freeable block
        void *curr = get_next(page);
        curr = payload_for_hdr(curr);
        set_payload_size(curr, size_left - sizeof(headerT));
        set_prevpayload_size(curr, requestedsz);
        myfree(curr);
        max_block = curr;
        return page;

}

/**
 * Function: mymalloc 
 * ------------------
 * Scans thorugh the explicit segregated freelist in order to find if there is 
 * a space available for the users requested size, if none is available it 
 * will call the page manager to ask for more space. Handles all of the extra
 * space either setting it as usable garbage or free space which is freed by
 * myfree. Returns a pointer to a space of exact or larger than requested size.
 */
void *mymalloc(size_t requestedsz)
{  
    if(requestedsz == 0) return NULL;
    // align requested sz
    requestedsz = roundup(requestedsz, ALIGNMENT);
    if(requestedsz < 16) requestedsz = 16;
    // get available space from the list if possible
    int bucket = cal_bucket((unsigned int)requestedsz);
    int bucketNo;
    void *curr = get_free_space(bucket, &bucketNo, requestedsz);
    // no free space available 
    if(curr == NULL) return get_new_page(requestedsz); 
    // found in free-list
    unsigned int tmp = get_size(curr);
    set_payload_size(curr, requestedsz);
    unsigned int remaining_size = tmp - requestedsz;
    // fit with freeable block
    if(remaining_size >= sizeof(headerT) + 2*sizeof(void*)){   
        // set the next free block
        void *next_free = get_next(curr);
        next_free = payload_for_hdr(next_free);
        set_payload_size(next_free, remaining_size - sizeof(headerT)); 
        set_prevpayload_size(next_free, requestedsz);
        // set next block if possible
        if(next_free < max_block){
           set_prevpayload_size(payload_for_hdr(get_next(next_free)), get_size(next_free)); 
           set_prevfree(next_free);
        }
        if(next_free > max_block) max_block = next_free;
        myfree(next_free);
    } else if(requestedsz != tmp) { // garbage
        // make garbage block
        void *next_free = get_next(curr);
        next_free = payload_for_hdr(next_free);
        set_payload_size(next_free, ((remaining_size - sizeof(headerT))|FREE_MASK));
        set_prevpayload_size(next_free, requestedsz); 
        // set next block if possible
        if(next_free > max_block) max_block = next_free;
        else if(next_free < max_block) {
            set_prevpayload_size(payload_for_hdr(get_next(next_free)), get_size(next_free));
            set_prevfree(next_free);
            if(is_free(payload_for_hdr(get_next(next_free)))){
                set_payload_size(next_free, (get_size(next_free)|NEXT_FREE));
            }
        }
        // tell block above I'm free
        if(next_free > min_block) set_nextfree(next_free);
        coalesce(next_free);
    }else{ // perfect fit
        // undo prev free 
        if(curr < max_block) {
            ((headerT*)((char *)curr +
                hdr_for_payload(curr)->payloadsz))->payloadsz &= ~(PREV_FREE);
        }
    }
    // unset NF for thing below curr, if possible
    if(curr > min_block) {
        void *prev = get_prev(curr);
        hdr_for_payload(prev)->payloadsz &= ~(NEXT_FREE);
        if(is_free(prev)) set_prevfree(prev); 
    }
    // go to the thing above curr, if it is possible, and change curr to be NF if the thing there is free.
    if(curr < max_block) {
        void *next = get_next(curr);
        next = payload_for_hdr(next);
        if(is_free(next)) set_nextfree(next); 
    }
    return curr;
}

/**
 * Funciton: coalesce
 * ------------------
 * Given a pointer to a free-block's payload, coalesce will look up and
 * down to find if the spaces are free and then will conjoin those spaces to make
 * larger blocks of space, this will also do garbage clean-up by coalescing 
 * garbage blocks.
 */
void *coalesce(void *ptr) {
    bool prev_is_free = has_prev_free(ptr);
    bool next_is_free = has_next_free(ptr); 
    unsigned int ptr_size = get_size(ptr);
    if((!prev_is_free) && (!next_is_free)) { // no coalesce
        return ptr;
    } else if((!prev_is_free) && (next_is_free)) { // coalesce up
        // get next block
        void* next_block = payload_for_hdr(get_next(ptr));
        unsigned int next_size = get_size(next_block);
        int bucket = cal_bucket(next_size);
        // remove from free-list if not garbage
        if(next_size >= 2*sizeof(void*))remove_from_list(next_block, bucket);
        // set new block
        unsigned int new_size = ptr_size + next_size + sizeof(headerT);
        set_payload_size(ptr, (new_size | (get_payloadsz(next_block)&NEXT_FREE)));
        if(next_block < max_block){
            void *next_next_block = payload_for_hdr(get_next(next_block));
            set_prevpayload_size(next_next_block, new_size);
        }
        // remember max
        if(next_block == max_block) max_block = ptr;
        return ptr;
    } else if((!next_is_free) && (prev_is_free)) { //coalesce down
        // get prev block
        void *prev_block = get_prev(ptr);
        unsigned int prev_size = get_size(prev_block);
        int bucket = cal_bucket(prev_size);
        // remove from free list if not garbage
        if(prev_size >= 2*sizeof(void*))remove_from_list(prev_block, bucket);
        // set new block
        unsigned int new_size = ptr_size + prev_size + sizeof(headerT);
        set_payload_size(prev_block, (new_size | (get_payloadsz(prev_block)&PREV_FREE)));
        if(ptr < max_block){
            void *next_block = payload_for_hdr(get_next(ptr));
            set_prevpayload_size(next_block, new_size);
        }
        // remember max
        if(ptr == max_block) max_block = prev_block;
        return prev_block;
    } else { // coalesce up and down
        // get previous block
        void *prev_block = get_prev(ptr);        
        unsigned int prev_size = get_size(prev_block);
        // get next block
        void* next_block = payload_for_hdr(get_next(ptr));
        unsigned int next_size = get_size(next_block);    
        // remove both from free list if not garbage
        int bucket = cal_bucket(next_size);
        if(next_size >= 2*sizeof(void*))remove_from_list(next_block, bucket);
        bucket = cal_bucket(prev_size);
        if(prev_size >= 2*sizeof(void*))remove_from_list(prev_block, bucket);
        // set new block
        unsigned int new_size = prev_size + sizeof(headerT) + ptr_size + sizeof(headerT) + next_size;
        set_payload_size(prev_block, (new_size | (get_size(prev_block)&PREV_FREE)));   
        if(next_block < max_block){
            void *next_next_block = payload_for_hdr(get_next(next_block));
            set_prevpayload_size(next_next_block, new_size);
        }
        // remember if max
        if(next_block == max_block) max_block = prev_block;
        return prev_block;
    }
    return ptr;
}

/**
 * Function: myfree
 * ----------------
 * Takes a pointer to a block currentluy allocated by the user, will add the
 * block to the appropriate bucket in the segregated free-list, as well as 
 * call coalesce to make larger spaces if appplicable. 
 */
void myfree(void *ptr){
    if(ptr == NULL) return;
    // coalesce
    ptr = coalesce(ptr);
    // set flags above
    if(ptr < max_block) set_prevfree(ptr);
    // set flags below
    if(ptr != min_block) set_nextfree(ptr);
    // set free flag
    set_free(ptr);
    // remember if max
    if(ptr > max_block) max_block = ptr;
    // get bucket
    int bucket = cal_bucket(get_size(ptr));
    void *bucketFirst = buckets[bucket];
    // add to list
    void *curr = buckets[bucket];
    if(curr == NULL){
        set_prev_in_list(ptr, NULL);
        set_next_in_list(ptr, bucketFirst);
        if(bucketFirst != NULL) set_prev_in_list(bucketFirst, ptr);
        buckets[bucket] = ptr;
        return;
    }else if(get_size(curr) > get_size(ptr) ){
        set_prev_in_list(ptr, NULL);
        set_next_in_list(ptr, bucketFirst);
        if(bucketFirst != NULL) set_prev_in_list(bucketFirst, ptr);
        buckets[bucket] = ptr;
        return;

    }

    void *prev = curr;
    while(curr != NULL){
        //printf("%p, %d\n", curr, get_size(curr));
        if(get_size(curr) >= get_size(ptr)) break;
        prev = curr;
        curr = get_next_in_list(curr);
    }
    //printf("b");
    if(curr != NULL) set_prev_in_list(curr, ptr);
    set_next_in_list(ptr, curr);
    if(prev != NULL) set_prev_in_list(ptr, prev);
    if(prev != NULL) set_next_in_list(prev, ptr);

    /*if(curr != NULL) set_next_in_list(curr,ptr);
    if(curr != NULL)set_prev_in_list(ptr, curr);
    else set_prev_in_list(ptr, NULL);
    if(curr != NULL) set_prev_in_list(ptr, get_prev_in_list(curr));
    else set_prev_in_list(ptr, NULL);
    buckets[bucket] = ptr;*/

}

/**
 * Taking in a pointer of a previously allocated block this method will
 * either use a free block above to extend the payload, or will run 
 * into my malloc to find the next free-block available to resize,
 * the date will be copied over from the old block.
 */
void *myrealloc(void *oldptr, size_t newsz)
{        
    unsigned int oldsz = get_size(oldptr);
        //(hdr_for_payload(oldptr)->payloadsz)&SIZE_MASK; 
    unsigned int new_size = roundup((unsigned int)newsz, ALIGNMENT); 
    if(new_size == oldsz) return oldptr;
    if(new_size < 16) new_size = 16; 
    // if the next is free, use it
    if((oldsz < new_size) && has_next_free(oldptr)&& (oldptr < max_block)){
        // get next block
        unsigned int nextsz = get_size(payload_for_hdr(get_next(oldptr)));
        void *next_block = payload_for_hdr(get_next(oldptr));
        int leftover = nextsz + oldsz - new_size;
        // if enough space for a free
        if(leftover >= (int)(2*sizeof(void *))){
                // remove from free-list
                int bucket = cal_bucket(nextsz);
                remove_from_list(next_block, bucket);
                // set the remaining blocks data
                void *remainder = (char *)oldptr + new_size;
                remainder = payload_for_hdr(remainder);
                set_payload_size(remainder,(oldsz + nextsz - new_size));
                set_prevpayload_size(remainder, (unsigned int)new_size); 
                (hdr_for_payload(remainder))->payloadsz |= (get_payloadsz(next_block)&NEXT_FREE);
                // remember new size and flags
                (hdr_for_payload(oldptr))->payloadsz = (((unsigned int)new_size|NEXT_FREE) 
                        | (get_payloadsz(oldptr)&PREV_FREE));
                // set the prev size of block above
                if(remainder <  max_block){ 
                    void* next = payload_for_hdr(get_next(remainder));
                    set_prevpayload_size(next, get_size(remainder));
                }
                if(remainder > max_block) max_block = remainder;             
                myfree(remainder); 
                return oldptr;
        }
    } 
    // next cannot accomodate
    void *newptr = mymalloc(new_size); 
    memmove(newptr, oldptr, oldsz < new_size ? oldsz: new_size);
    myfree(oldptr);
    return newptr;
}

/**
 * Function: validate_heap
 * -----------------------
 * Prints all of the blocks in the heap, including the free, next free
 * and prev free flags, as well as the size, the previous blocks size,
 * and the address of each block.
 */
bool validate_heap()
{
    // prints all of the blocks in the heap, with their flags
    /*void *ptra = min_block;
    printf("\n\nSTART:\n");
    while(ptra <= max_block){
        printf("Address: %p, payloadsz %d and prevpayload: %d ", ptra, 
                (hdr_for_payload(ptra))->payloadsz&SIZE_MASK, 
                ((hdr_for_payload(ptra))->prevpayloadsz)&SIZE_MASK);
        // print free flags
        if(((hdr_for_payload(ptra))->payloadsz&FREE_MASK) != 0) printf(" F ");
        else{ printf(" !F ");}
        // print next free flags
        if(((hdr_for_payload(ptra))->payloadsz&NEXT_FREE) != 0) printf(" NF ");
        else printf(" !NF ");
        // print prev free flags
        if(((hdr_for_payload(ptra))->payloadsz&PREV_FREE) != 0) printf(" PF ");
        else printf(" !PF ");
        printf("\n");
        ptra = get_next(ptra);
        ptra = payload_for_hdr(ptra);
    }*/

  return true;
}

