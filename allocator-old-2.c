/*
 * File: allocator.c
 * Author: Renner Leite Lucena && Keanu Phillip Spies
 * ----------------------
 * A trivial allocator. Very simple code, heinously inefficient.
 * An allocation request is serviced by incrementing the heap segment
 * to place new block on its own page(s). The block has a pre-node header
 * containing size information. Free is a no-op: blocks are never coalesced
 * or reused. Realloc is implemented using malloc/memcpy/free.
 * Using page-per-node means low utilization. Because it grows the heap segment
 * (using expensive OS call) for each node, it also has low throughput
 * and terrible cache performance.  The code is not robust in terms of
 * handling unusual cases either.
 *
 * In short, this implementation has not much going for it other than being
 * the smallest amount of code that "works" for ordinary cases.  But
 * even that's still a good place to start from.
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
#define BUFFER 0// 8 - headersize
#define ROUND_UP(SIZE,MULT) (SIZE + MULT-1) & ~(MULT-1) + 4;
#define SIZE_MASK 0x7ffffffc
#define FREE_MASK 0x80000000
#define PREV_FREE 0x00000001
#define NEXT_FREE 0x00000002
#define GARBAGE_MASK 0x00000001
#define INIT_MASK 0xfffffffe
#define LOG_TWO log(2)
#define NUM_BUCKETS 1
#define INT_BITS 32

#pragma pack(1)


typedef struct {
   unsigned int payloadsz;
   unsigned int prevpayloadsz; 
} headerT;


void *freeList;
void *buckets[NUM_BUCKETS];
void *max_block;
void *min_block;

// Very efficient bitwise round of sz up to nearest multiple of mult
// does this by adding mult-1 to sz, then masking off the
// the bottom bits to compute least multiple of mult that is
// greater/equal than sz, this value is returned
// NOTE: mult has to be power of 2 for the bitwise trick to work!
static inline size_t roundup(size_t sz, size_t mult)
{    
    return (sz + mult-1) & ~(mult-1);
}


// Given a pointer to start of payload, simply back up
// to access its block header
static inline headerT *hdr_for_payload(void *payload)
{
    return (headerT *)((char *)payload - sizeof(headerT));
}

// Given a pointer to block header, advance past
// header to access start of payload
static inline void *payload_for_hdr(headerT *header)
{
    return (char *)header + sizeof(headerT);
}

//My functions
void set_payload(void* payload, unsigned int value)
{
    (hdr_for_payload(payload))->payloadsz = value;
}

void set_prevpayload(void* payload, unsigned int value)
{
    (hdr_for_payload(payload))->prevpayloadsz = value;
}

int cal_bucket(unsigned int value){
    int count = INT_BITS - __builtin_clz(value);
    if(count >= NUM_BUCKETS + 2) return NUM_BUCKETS - 1;
    return count - 3;
}

/*void point_prev_to(void* payload, void* dest)
{
    *(void**)payload = dest;
}

void point_next_to(void* payload, void* dest)
{
    *(void**)((void*)((char*)payload + sizeof(void*))) = dest;
}*/

void set_prevfree(void* block){
    void* next_block = ((headerT*)((char*)block + (((hdr_for_payload(block))->payloadsz)&SIZE_MASK)));
    ((headerT *)(next_block))->payloadsz |= PREV_FREE;
}

void set_nextfree(void* block){
    void *prev_block = (char*)hdr_for_payload(block) - ((hdr_for_payload(block)->prevpayloadsz)&SIZE_MASK);
    hdr_for_payload(prev_block)->payloadsz |= NEXT_FREE;
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
    for(int i = 0; i < NUM_BUCKETS; i ++){
        *(void **)((char *) buckets + i*sizeof(void *)) = NULL;
    }

    void *first = init_heap_segment(1);
    if(first == NULL) return false;

    first = (char *)first + BUFFER;

    max_block = payload_for_hdr(first);

    ((headerT *)first)->payloadsz = ((PAGE_SIZE - sizeof(headerT) - BUFFER)|FREE_MASK);
    ((headerT *)first)->prevpayloadsz = INIT_MASK;
    min_block = payload_for_hdr(first);
         
    //int bucket = (int)(log((double)((((headerT *)first)->payloadsz)&SIZE_MASK))/LOG_TWO);
    int bucket = cal_bucket((((headerT *)first)->payloadsz)&SIZE_MASK); //maybe more parantheses
    void *bucketFirst = buckets[bucket];
    ((headerT *)first)->payloadsz |= FREE_MASK;

    *(void **)(payload_for_hdr(first)) = bucketFirst;
    *(void **)((char*)payload_for_hdr(first) + sizeof(void*)) = NULL;
    buckets[bucket] = payload_for_hdr(first);

    return true;
}

//For and while loop run all the freeList, looking for something with enough space for requestedsz
void *get_free_space(int bucket, int* bucketNo,  size_t requestedsz){
    for (int i = bucket; i < NUM_BUCKETS; i++){ //CHANGE TO RIGHT THING
        void *curr = buckets[i];
        *bucketNo = i;

        while(curr != NULL){
            if(((hdr_for_payload(curr)->payloadsz)&SIZE_MASK) >= requestedsz){
                //gets prev_free and next_free to remove block from the list
                void* prev_free = *(void **)((char*)curr + sizeof(void*));
                void* next_free = *(void **)curr;

                //prev gets next
                if(prev_free != NULL){ 
                    *(void **)prev_free = next_free;
                } else{
                     buckets[i] = next_free;
                } 
                
                //next gets prev
                if(next_free != NULL) *(void **)((char *)next_free + sizeof(void*)) = prev_free; 

                return curr;
            }
            curr = *(void**)curr;
        }  
    }
    //return NULL if there is not a large enough block
    return NULL;
}

// malloc a block by rounding up size to number of pages, extending heap
// segment and using most recently added page(s) for this block. This
// means each block gets its own page -- how generous! :-)
void *mymalloc(size_t requestedsz)
{  
    if(requestedsz == 0) return NULL;

    requestedsz = roundup(requestedsz, ALIGNMENT) + BUFFER; 
    if(requestedsz < 16) requestedsz = 16;

    //int bucket = (int)(log((double)requestedsz))/LOG_TWO;
    int bucket = cal_bucket((unsigned int)requestedsz); //unsigned int?
    int bucketNo;

    void *curr = get_free_space(bucket, &bucketNo, requestedsz);
      
    //No free space available. Call new pages. What if 0 pages in init?
    if(curr == NULL){
        size_t npages = roundup(requestedsz + sizeof(headerT), PAGE_SIZE)/PAGE_SIZE;
        headerT *header = extend_heap_segment(npages);

        header = (void *)((char *)header + BUFFER);

        //Set payloadsz and prevpayload with max_block, since it is a new buch of pages
        header->payloadsz = requestedsz;
        header->prevpayloadsz = (hdr_for_payload(max_block)->payloadsz)&SIZE_MASK;

        //Set prevfree, if max_block(the thing before) is free
        if((hdr_for_payload(max_block)->payloadsz&FREE_MASK) != 0){
            header->payloadsz |= PREV_FREE;
            //set_prevfree(payload_for_hdr(header)); ineficient
        }

        //Perfect fit - there will be not next (no need to set mask NF) and PF already set, so max_block gets this block    
        if(npages*PAGE_SIZE - requestedsz - sizeof(headerT) - BUFFER  == 0){
            max_block = payload_for_hdr(header);
            return payload_for_hdr(header);
        }

        //Garbage - The space that remains after the header, footer and requestedsz of the new block is set is smaller than a header + footer (16),
        //which results in a garbage, not added to the freeList. Here, it is called next_free! TODO: Change name next_free to garbage?
        if(npages*PAGE_SIZE < requestedsz + 2*sizeof(headerT) + BUFFER + 2*sizeof(void *)){
            void *next_free = (char *)(payload_for_hdr(header)) + requestedsz;

            //Nor next of garbage(does not exist) nor prev (block of the malloc) is free
            ((headerT *)next_free)->payloadsz = (npages*PAGE_SIZE - requestedsz - 2*sizeof(headerT))|FREE_MASK;
            ((headerT *)next_free)->prevpayloadsz = requestedsz;
            //((headerT *)next_free)->prevpayloadsz |= GARBAGE_MASK; NOT GARBAGE MASK

            //Block before becomes next free (garbage above can be used). Can be set directly
            header->payloadsz |= NEXT_FREE;
            max_block = payload_for_hdr(next_free);

            return payload_for_hdr(header);
        }

        //Regular case - We have space to add something to the freeList. Here, it is called curr. TODO: Change name, local new variable?
        curr = (char *)header + sizeof(headerT) + requestedsz;
        ((headerT*)curr)->payloadsz = ((npages*PAGE_SIZE - 2*sizeof(headerT) - requestedsz - BUFFER));
        ((headerT*)curr)->prevpayloadsz = requestedsz;

        //My free changes PF, but not NF (does not exist) 
        curr = payload_for_hdr(curr);
        myfree(curr);
        
        //New max_block
        max_block = curr;
        return payload_for_hdr(header);
    }

    //Case that new pages are not called. tmp gets all the space found in the freeList
    unsigned int tmp = (hdr_for_payload(curr)->payloadsz)&SIZE_MASK; 
   
    //The payload becomes the requestedsz, and there is some remainder (maybe)
    hdr_for_payload(curr)->payloadsz = requestedsz;

    //If tmp is enough for requestedsz, its footer, and a header plus another footer, part of a second big enough block, we can 
    //call free in this second block (just like a garbage, but freeing it, adding to the freeList)
    if(tmp >= requestedsz + sizeof(headerT) + 2*sizeof(void*)){ //?? GARBAGE?? 
        //Set values for next_free
        void *next_free = (char *)curr + requestedsz;
        ((headerT *)next_free)->payloadsz = tmp - requestedsz - sizeof(headerT);
        ((headerT *)next_free)->prevpayloadsz = requestedsz;

        next_free = payload_for_hdr(next_free);
       
        //Go up, if possible, and set PF for block up there
        if(next_free < max_block){
           ((headerT* )((char *)next_free + (hdr_for_payload(next_free))->payloadsz))->prevpayloadsz = 
               (hdr_for_payload(next_free))->payloadsz&SIZE_MASK; //LOOSE?
            ((headerT* )((char *)next_free + (hdr_for_payload(next_free))->payloadsz))->payloadsz |= PREV_FREE;
            //set_prevfree(next_free);
        }

        //Change max_block and free next_block block
        if(next_free > max_block) max_block = next_free;
        myfree(next_free);

     //Case of garbage - not enough space for something new in the block from the freeList
     }else if(requestedsz != tmp){ 
        void *next_free = (char *)curr + requestedsz;
        ((headerT *)next_free)->payloadsz = (tmp - requestedsz - sizeof(headerT))|FREE_MASK;
        ((headerT *)next_free)->prevpayloadsz = requestedsz;
        //((headerT *)next_free)->prevpayloadsz |= GARBAGE_MASK;        
       
        next_free = payload_for_hdr(next_free);
        
        //Setting up this garbage. First if it is 
        if(next_free > max_block){ 
            max_block = next_free;
        } else if(next_free < max_block) {

        unsigned int garbage = 0;    
        if(((((headerT* )((char *)next_free + (((hdr_for_payload(next_free))->payloadsz)&SIZE_MASK)))->prevpayloadsz)&GARBAGE_MASK) != 0){
            garbage = GARBAGE_MASK;
        }

        ((headerT* )((char *)next_free + (((hdr_for_payload(next_free))->payloadsz)&SIZE_MASK)))->prevpayloadsz = 
               (((hdr_for_payload(next_free))->payloadsz&SIZE_MASK) | garbage); //LOOSE?

            ((headerT*)((char *)next_free + (((hdr_for_payload(next_free))->payloadsz)&SIZE_MASK)))->payloadsz |= PREV_FREE;
            if((((headerT*)((char *)next_free + ((hdr_for_payload(next_free))->payloadsz&SIZE_MASK)))->payloadsz&FREE_MASK) != 0) hdr_for_payload(next_free)->payloadsz |= NEXT_FREE;
        }

        if(next_free > min_block){
            hdr_for_payload((void*)((char*)next_free - requestedsz))->payloadsz |= NEXT_FREE;
        }
       
     }else{     
           if(curr < max_block) ((headerT*)((char *)curr + hdr_for_payload(curr)->payloadsz))->payloadsz &= ~(PREV_FREE);
     }

    if(curr > min_block){
        void *prev = (char*)curr - sizeof(headerT) - (hdr_for_payload(curr)->prevpayloadsz&SIZE_MASK);
        hdr_for_payload(prev)->payloadsz &= ~(NEXT_FREE);

        if((hdr_for_payload(prev)->payloadsz&FREE_MASK) != 0) hdr_for_payload(curr)->payloadsz |= PREV_FREE;

    }

    if(curr < max_block){
        void *next = (char*)curr + ((hdr_for_payload(curr)->payloadsz)&SIZE_MASK);

        if((((headerT*)next)->payloadsz&FREE_MASK) != 0) hdr_for_payload(curr)->payloadsz |= NEXT_FREE;
    }

    return curr;
}


// free does nothing.  fast!... but lame :(
void myfree(void *ptr){
    if(ptr == NULL) return;

    //int bucket = (int)(log((double)((hdr_for_payload(ptr)->payloadsz)&SIZE_MASK))/LOG_TWO);
    int bucket = cal_bucket(((hdr_for_payload(ptr))->payloadsz)&SIZE_MASK);
    void *bucketFirst = buckets[bucket];

    //If ptr is smaller than max_block, it is necessary to go up and set PF for block up there
    if(ptr < max_block){
        //((headerT*)((char*)ptr + (((hdr_for_payload(ptr))->payloadsz)&SIZE_MASK)))->payloadsz |= PREV_FREE;
        set_prevfree(ptr);
    }
   
    //If ptr is larger than min_block, it is necessary to go down and set NF for block down there TODO: Change to ptr >
    if(ptr != min_block){ 
       //void *prev_block = (char*)hdr_for_payload(ptr) - ((hdr_for_payload(ptr)->prevpayloadsz)&SIZE_MASK);
       //hdr_for_payload(prev_block)->payloadsz |= NEXT_FREE;
       set_nextfree(ptr);
    }

    //Own payload is free now
    hdr_for_payload(ptr)->payloadsz |= FREE_MASK;

    //Changes max_block, if necessary
    if(ptr > max_block) max_block = ptr;

    //Pointers arithmetic to add free block to the beginning of a bucket
    if(bucketFirst != NULL) *(void **)((char *)bucketFirst + sizeof(void *)) = ptr;
    *(void **)ptr = bucketFirst;
    *(void **)((char*)ptr + sizeof(void *)) = NULL;
    buckets[bucket] = ptr; 
}


// realloc built on malloc/memcpy/free is easy to write.
// This code will work ok on ordinary cases, but needs attention
// to robustness. Realloc efficiency can be improved by
// implementing a standalone realloc as opposed to
// delegating to malloc/free.
void *myrealloc(void *oldptr, size_t newsz)
{        
    unsigned int oldsz = (hdr_for_payload(oldptr)->payloadsz)&SIZE_MASK; 
    unsigned int new_size = roundup((unsigned int)newsz, ALIGNMENT) + BUFFER; 
    if(new_size == oldsz) return oldptr;
    
    if(new_size < 16) new_size = 16; 

    if((oldsz < new_size) && (((hdr_for_payload(oldptr)->payloadsz)&NEXT_FREE) != 0) && (oldptr < max_block)){
        
        unsigned int nextsz = (((headerT *)((char *)oldptr + oldsz))->payloadsz)&SIZE_MASK;
        void *next_block = payload_for_hdr((void *)((char *)oldptr + oldsz));

        if((int)(nextsz + oldsz - new_size)  >= (int)(2*sizeof(void *))){
                int bucket = cal_bucket(nextsz);

                void* prev_free = *(void **)((char*)next_block + sizeof(void*));
                void* next_free = *(void **)next_block;
                        
                        if(prev_free != NULL){
                            *(void **)prev_free = next_free;
                        }else{
                            buckets[bucket] = next_free;
                        }
                        
                        if(next_free != NULL){
                            *(void **)((char *)next_free + sizeof(void*)) = prev_free;
                        }

                (hdr_for_payload(oldptr))->payloadsz = ( ((unsigned int)new_size|NEXT_FREE) | (((hdr_for_payload(oldptr))->payloadsz)&PREV_FREE));
         
                void *remainder = (char *)oldptr + new_size;
                

                ((headerT*)remainder)->payloadsz = oldsz + nextsz - new_size;
                ((headerT*)remainder)->prevpayloadsz = (unsigned int)new_size;

                remainder = payload_for_hdr(remainder);
                if(remainder <  max_block){ 
                    void* next = (char*)remainder + ((hdr_for_payload(remainder))->payloadsz&SIZE_MASK);
                    ((headerT*)next)->prevpayloadsz = ((hdr_for_payload(remainder))->payloadsz&SIZE_MASK);
                }
                if(remainder > max_block) max_block = remainder;             

                myfree(remainder); 

                return oldptr;
        }
    }

    //This is probably inneficient 
    void *newptr = mymalloc(new_size);
    void* above_me = (char *)newptr + (((hdr_for_payload(newptr))->payloadsz)&SIZE_MASK); 
    above_me = payload_for_hdr(above_me);
    if(above_me < max_block){
        void* above_that = (char *)above_me + (((hdr_for_payload(above_me))->payloadsz)&SIZE_MASK);
        ((headerT *)above_that)->prevpayloadsz = ((hdr_for_payload(above_me))->payloadsz)&SIZE_MASK;
    }
    memcpy(newptr, oldptr, oldsz < new_size ? oldsz: new_size);

    myfree(oldptr);


    return newptr;
}

// validate_heap is your debugging routine to detect/report
// on problems/inconsistency within your heap data structures
bool validate_heap()
{
    /*void *ptra = min_block;
    printf("\n\nSTART:\n");
    while(ptra <= max_block){
        printf("Address: %p, payloadsz %d and prevpayload: %d ", ptra, (hdr_for_payload(ptra))->payloadsz&SIZE_MASK, ((hdr_for_payload(ptra))->prevpayloadsz)&SIZE_MASK);
        if(((hdr_for_payload(ptra))->payloadsz&FREE_MASK) != 0) printf(" F ");
        else{ printf(" !F ");}

        if(((hdr_for_payload(ptra))->payloadsz&NEXT_FREE) != 0) printf(" NF ");
        else printf(" !NF ");

        if(((hdr_for_payload(ptra))->payloadsz&PREV_FREE) != 0) printf(" PF ");
        else printf(" !PF ");

        if((hdr_for_payload(ptra)->prevpayloadsz&GARBAGE_MASK) != 0) printf(" G  ");
        printf("\n");
        ptra = (char*)ptra + (((hdr_for_payload(ptra))->payloadsz)&SIZE_MASK);
        ptra = payload_for_hdr(ptra);
    }*/

  return true;
}

