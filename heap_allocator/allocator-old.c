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
#define NUM_BUCKETS 32

#pragma pack(1)


typedef struct {
   unsigned int payloadsz;   // header contains just one 4-byte field
   unsigned int prevpayloadsz; // 1 byte
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
    //size_t roundUp = (sz + mult-1) & ~(mult-1);
    //printf("%zu\n", roundUp);
    
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
    /*freeList = init_heap_segment(1); // reset heap segment to empty, no pages allocated
    freeList = (char *)freeList + BUFFER;

    ((headerT *)freeList)->payloadsz = ((PAGE_SIZE - sizeof(headerT) - BUFFER)|FREE_MASK);

    freeList = payload_for_hdr(freeList); //awesome here
    *(void**)freeList = NULL;*/

/*--------------------------------------------------------------------------------------*/    
    
    
    for(int i = 0; i < 32; i ++){
        *(void **)((char *) buckets + i*sizeof(void *)) = NULL;
    }

    void *first = init_heap_segment(1); // reset heap segment to empty, no pages allocated
    if(first == NULL) return false;

    first = (char *)first + BUFFER;

    max_block = payload_for_hdr(first);

    //printf("Before lost free list %p\n", freeList);
    ((headerT *)first)->payloadsz = ((PAGE_SIZE - sizeof(headerT) - BUFFER)|FREE_MASK);
    ((headerT *)first)->prevpayloadsz = INIT_MASK;
    min_block = payload_for_hdr(first);
     
    //myfree(payload_for_hdr(first));
    
    int bucket = (int)(log((double)((((headerT *)first)->payloadsz)&SIZE_MASK))/LOG_TWO);
    void *bucketFirst = buckets[bucket];
    ((headerT *)first)->payloadsz |= FREE_MASK;


    *(void **)(payload_for_hdr(first)) = bucketFirst;
    *(void **)((char*)payload_for_hdr(first) + sizeof(void*)) = NULL;
    buckets[bucket] = payload_for_hdr(first);

    /*
    int bucket = (int)(log((double)((((headerT *)first)->payloadsz)&SIZE_MASK))/LOG_TWO);
    printf("Init bucket: %d\n", bucket);  
    printf("Payload for initial block %d\n\n", (((headerT *)first)->payloadsz)&SIZE_MASK);
    first = payload_for_hdr(first); //awesome here
    
    
    //int number = (((headerT *)first)->payloadsz)&SIZE_MASK; 
    //double num = (double)((((headerT *)first)->payloadsz)&SIZE_MASK);
    //printf("Bucket: %d\n",bucket);
    //
    *(void **)first = buckets[bucket];
    buckets[bucket] = first;*/
    //*(void **)first = NULL;

    //printf("First: %p, Bucket Num: %d, Bucket: %p\n", first, bucket, buckets[bucket]  );

    
    //freeList = first;
    //printf("After lost free list %p\n", freeList);
    return true;
}

void *get_free_space(int bucket, int* bucketNo,  size_t requestedsz){
    //printf("\nFREE LIST! REQUESTED SIZE: %zu\n", requestedsz);
    for (int i = 0; i < NUM_BUCKETS; i++){ //CHANGE LATER TO RIGHT THING
        void *curr = buckets[i];
        //printf("Curr: %p\n", curr);
        /*if((unsigned long)curr == 0x1070076f90 && i == 6){
            curr = payload_for_hdr(curr);
        }*/

        *bucketNo = i;
        //printf("I am here\n");
        //printf("%d: \n", i); 
        while(curr != NULL){
            //printf("CURR: %p, payloads %d \n", curr, (hdr_for_payload(curr)->payloadsz)&SIZE_MASK);
            if(((hdr_for_payload(curr)->payloadsz)&SIZE_MASK) >= requestedsz){
                void* prev_free = *(void **)((char*)curr + sizeof(void*));
                void* next_free = *(void **)curr;

               //if((prev_free == NULL) && (next_free == NULL)) buckets[i] = NULL;

                if(prev_free != NULL){ 
                    *(void **)prev_free = next_free;
                } else{
                     buckets[i] = next_free;
                } 
                
                if(next_free != NULL) *(void **)((char *)next_free + sizeof(void*)) = prev_free;
                
                //printf("Happy end!\n\n");
                return curr;
            }
            curr = *(void**)curr;
        }  
        //printf("\n");
    }

    //printf("Sad end!\n\n");
    return NULL;
}

// malloc a block by rounding up size to number of pages, extending heap
// segment and using most recently added page(s) for this block. This
// means each block gets its own page -- how generous! :-)
void *mymalloc(size_t requestedsz)
{
    /*printf("\n\nFree List M\n\n");
        for (int i = 0; i < NUM_BUCKETS; i++){
        void *curr = buckets[i];
        printf("Curr: %p\n", curr);

        // bucketNo = i;
        //printf("I am here\n");
        printf("%d: \n", i); 
        while(curr != NULL){
            printf("CURR: %p, payloads %d \n", curr, (hdr_for_payload(curr)->payloadsz)&SIZE_MASK);
            curr = *(void**)curr;
        }  
        printf("\n");
    }*/

    
    //printf("\n\n\nSTART!!!! max_address: %p and max_payload: %d\n", max_block, (hdr_for_payload(max_block)->payloadsz)&SIZE_MASK);
   //printf("\nM NEW  requestedsz %zu\n", requestedsz);
   
    if(requestedsz == 0) return NULL;

    requestedsz = roundup(requestedsz, ALIGNMENT) + BUFFER; 
    if(requestedsz < 16) requestedsz = 16;

    int bucket = (int)(log((double)requestedsz))/LOG_TWO;
    int bucketNo;

    void *curr = get_free_space(bucket, &bucketNo,  requestedsz);
    //printf("Malloc Curr %p\n", curr);
    /*if(curr == (void*)0x107008b338){
        printf("\nAddressM: %p, payloadsz %d and prevpayload: %d ", (void*)0x107008b338, (hdr_for_payload((void*)0x107008b338))->payloadsz&SIZE_MASK, (hdr_for_payload((void*)0x107008b338))->prevpayloadsz&SIZE_MASK);
        if(((hdr_for_payload((void*)0x107008b338))->payloadsz&FREE_MASK) != 0) printf(" F ");
        else printf(" !F ");

        if(((hdr_for_payload((void*)0x107008b338))->payloadsz&NEXT_FREE) != 0) printf(" NF ");
        else printf(" !NF ");

        if(((hdr_for_payload((void*)0x107008b338))->payloadsz&PREV_FREE) != 0) printf(" PF ");
        else printf(" !PF ");

        if(((hdr_for_payload((void*)0x107008b338))->prevpayloadsz&GARBAGE_MASK) != 0) printf(" G  ");
        printf("\n");

        //printf("Malloc Curr %p\n", curr);   
    }*/

      
    if(curr == NULL){
        //printf("\n\nNew page requested!!! %zu\n", requestedsz);
        size_t npages = roundup(requestedsz + sizeof(headerT), PAGE_SIZE)/PAGE_SIZE; //+4 lost maybe
        //printf("NPAGES: %zu, REQUESTED %zu\n", npages, requestedsz);
        headerT *header = extend_heap_segment(npages);
        //printf("HEADER: %p\n", header);
        //printf("\n*********NEW PAGE :( ********\n");
        
        //printf("new header %p\n", header);

        header = (void *)((char *)header + BUFFER);
        //printf("unaligned header %p\n", header);

        //header->payloadsz = originalsz;
        header->payloadsz = requestedsz;
        header->prevpayloadsz = (hdr_for_payload(max_block)->payloadsz)&SIZE_MASK;
        if ((hdr_for_payload(max_block)->payloadsz&FREE_MASK) != 0){
            header->payloadsz |= PREV_FREE;
        }
        //if((hdr_for_payload(max_block)->payloadsz&FREE_MASK) != 0) header->payloadsz |= PREV_FREE;

        //int paylou = payload_for_hdr(header);
        
        if(npages*PAGE_SIZE - requestedsz - sizeof(headerT) - BUFFER  == 0){
            //if(header->payloadsz == 0) printf("HERE0!!! %zu\n", requestedsz);
            max_block = payload_for_hdr(header);
            //printf("HERE EQUAL SIZE0 %d\n",((header->payloadsz)&SIZE_MASK));
            return payload_for_hdr(header); // minus 4 for alignmen
        }
        //printf("NUmber of bytes free: %lu", npages*PAGE_SIZE - requestedsz - 2*sizeof(headerT) - 2*sizeof(void*));
        if(npages*PAGE_SIZE < requestedsz + 2*sizeof(headerT) + BUFFER + 2*sizeof(void *)){
            //if(header->payloadsz == 0) printf("HERE1!!! %zu\n", requestedsz);
            void * next_free = (char *)(payload_for_hdr(header)) + requestedsz;

            ((headerT *)next_free)->payloadsz = (npages*PAGE_SIZE - requestedsz - 2*sizeof(headerT))|FREE_MASK;
            ((headerT *)next_free)->prevpayloadsz = requestedsz;
            ((headerT *)next_free)->prevpayloadsz |= GARBAGE_MASK;
            header->payloadsz |= NEXT_FREE;
            
            max_block = payload_for_hdr(next_free);
            //printf("HERE FOR GARBAGE %d\n",((header->payloadsz)&SIZE_MASK));

            return payload_for_hdr(header);
        }
        //printf("curr0 %p\n", curr);
        //printf("req size %zu\n", originalsz);

        curr = (char *)header + sizeof(headerT) + requestedsz;
        //printf("curr1 %p\n", curr);
        ((headerT *)curr)->payloadsz = ((npages*PAGE_SIZE -  2*sizeof(headerT) - requestedsz - BUFFER)); // minus 4 to align
        ((headerT*)curr)->prevpayloadsz = requestedsz;

        curr = payload_for_hdr(curr);
        myfree(curr);
                
        //if(header->payloadsz == 0) printf("HERE2!!! %zu\n", requestedsz);
        //printf("HERE FOR NORMAL WITH MYFREE %d\n",((header->payloadsz)&SIZE_MASK));
        max_block = curr;
        return payload_for_hdr(header);
    }
    
    unsigned int tmp = (hdr_for_payload(curr)->payloadsz)&SIZE_MASK; 
    
    //hdr_for_payload(curr)->payloadsz = originalsz;
    hdr_for_payload(curr)->payloadsz = requestedsz;
   // printf("REQUESTEDSZ %d\n",  hdr_for_payload(curr)->payloadsz);


    
   /*if(curr == buckets[bucketNo]){
        buckets[bucketNo] = *(void **)(buckets[bucketNo]);
   } else {
       *(void **)prev = *(void **)freeSpace;
   }*/

    //printf("Space for alloc: %d, Space needed: %lu\n", tmp, requestedsz + sizeof(headerT) + sizeof(void*));

    if(tmp >= requestedsz + sizeof(headerT) + 2*sizeof(void*)){ // + 4 taken out //WHAT
        //printf("space still free %lu\n", tmp - requestedsz - sizeof(headerT));
        //printf("curr before moving up %p\n", curr);
        
        void * next_free = (char *)curr + requestedsz;
        ((headerT *)next_free)->payloadsz = tmp - requestedsz - sizeof(headerT);
        //printf("\n\nHELP: %ld, ptr: %p\n", tmp - requestedsz - sizeof(headerT), next_free);
        //if(((headerT *)next_free)->payloadsz == 0) printf("HERE3!!! %zu\n", requestedsz);
        ((headerT *)next_free)->prevpayloadsz = requestedsz;
        //((headerT *)curr)->payloadsz |= FREE_MASK;

        next_free = payload_for_hdr(next_free);

        //printf("curr after moving up (in a payload) %p\n", curr);
        /**(void **) curr = freeList;
        freeList = curr;*/
        
        if(next_free < max_block){
           ((headerT* )((char *)next_free + (hdr_for_payload(next_free))->payloadsz))->prevpayloadsz = 
               (hdr_for_payload(next_free))->payloadsz&SIZE_MASK; //HERE
            ((headerT* )((char *)next_free + (hdr_for_payload(next_free))->payloadsz))->payloadsz |= PREV_FREE;
        }
        if(next_free > max_block) max_block = next_free;
      // printf("PAYLOADSZ OF THIS FREE%d\n", (hdr_for_payload(next_free))->payloadsz); 
        myfree(next_free);
        

    } else if(requestedsz != tmp){
        void * next_free = (char *)curr + requestedsz;

        ((headerT *)next_free)->payloadsz = (tmp - requestedsz - sizeof(headerT))|FREE_MASK;

        ((headerT *)next_free)->prevpayloadsz = requestedsz;
        ((headerT *)next_free)->prevpayloadsz |= GARBAGE_MASK;        
        //hdr_for_payload(curr)->payloadsz |= NEXT_FREE;
        
        next_free = payload_for_hdr(next_free);
        if(next_free > max_block) max_block = next_free;
        else if(next_free < max_block) {

        unsigned int garbage = 0;    
        if(((((headerT* )((char *)next_free + (((hdr_for_payload(next_free))->payloadsz)&SIZE_MASK)))->prevpayloadsz)&GARBAGE_MASK) != 0){
            garbage = GARBAGE_MASK;
        }

        ((headerT* )((char *)next_free + (((hdr_for_payload(next_free))->payloadsz)&SIZE_MASK)))->prevpayloadsz = 
               (((hdr_for_payload(next_free))->payloadsz&SIZE_MASK) | garbage); //HERE


            //((headerT* )((char *)next_free + (((hdr_for_payload(next_free))->payloadsz)&SIZE_MASK)))->prevpayloadsz |= 
            //    ((((headerT* )((char *)next_free + (((hdr_for_payload(next_free))->payloadsz)&SIZE_MASK)))->prevpayloadsz)&GARBAGE_MASK);


            ((headerT*)((char *)next_free + (((hdr_for_payload(next_free))->payloadsz)&SIZE_MASK)))->payloadsz |= PREV_FREE;
            if((((headerT*)((char *)next_free + ((hdr_for_payload(next_free))->payloadsz&SIZE_MASK)))->payloadsz&FREE_MASK) != 0) hdr_for_payload(next_free)->payloadsz |= NEXT_FREE;
        }

        if(next_free > min_block){
            hdr_for_payload((void*)((char*)next_free - requestedsz))->payloadsz |= NEXT_FREE;
        }
        /*if(next_free < max_block){
           ((headerT* )((char *)next_free + (hdr_for_payload(next_free))->payloadsz))->prevpayloadsz = 
               (hdr_for_payload(next_free))->payloadsz&SIZE_MASK; //HERE
            ((headerT* )((char *)next_free + (hdr_for_payload(next_free))->payloadsz))->payloadsz |= PREV_FREE;

        }*/
       
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

       //printf("FreeSpace %p\n", freeSpace);
     
    //if((hdr_for_payload(curr))->payloadsz == 0) printf("HERE3!!! %zu\n", requestedsz);

    //printf("HERE3!!! %d\n", (hdr_for_payload(curr)->payloadsz&SIZE_MASK));
    return curr;
}


// free does nothing.  fast!... but lame :(
void myfree(void *ptr){
    /*if(ptr == (void*)0x107008b338){
    printf("\nAddressF: %p, payloadsz %d and prevpayload: %d ", ptr, (hdr_for_payload(ptr))->payloadsz&SIZE_MASK, (hdr_for_payload(ptr))->prevpayloadsz&SIZE_MASK);
        if(((hdr_for_payload(ptr))->payloadsz&FREE_MASK) != 0) printf(" F ");
        else printf(" !F ");

        if(((hdr_for_payload(ptr))->payloadsz&NEXT_FREE) != 0) printf(" NF ");
        else printf(" !NF ");

        if(((hdr_for_payload(ptr))->payloadsz&PREV_FREE) != 0) printf(" PF ");
        else printf(" !PF ");

        if(((hdr_for_payload(ptr))->prevpayloadsz&GARBAGE_MASK) != 0) printf(" G  ");
        printf("\n");
    }*/

    /*printf("\nF NEW\n");

    printf("\n\nFree List 1 F\n\n");
for (int i = 0; i < NUM_BUCKETS; i++){
        void *curr = buckets[i];
        printf("Curr: %p\n", curr);

        // bucketNo = i;
        //printf("I am here\n");
        printf("%d: \n", i); 
        while(curr != NULL){
            printf("CURR: %p, payloads %d \n", curr, (hdr_for_payload(curr)->payloadsz)&SIZE_MASK);
            curr = *(void**)curr;
        }  
        printf("\n");
    }*/

    //printf("Max Block: %p\n", max_block);
    if(ptr == NULL) return;
    //if((PREV_FREE&usedSpace != 0) && (NEXT_FREE&usedSpace != 0)){           
   // } else if((PREV_FREE&usedSpace == 0) &&  (NEXT_FREE&usedSpace != 0)){
    //
    //} else if ((PREV_FREE&usedSpace != 0)  (NEXT_FREE&usedSpace == 0)){
    //
    //} else{
     //   
    //}

    //void *upptr = ptr + roundup(hdr_for_payloadsz(ptr)->payloadszi, ALIGNMENT) + 4;
    //(headerT *)upptr->payloadsz |= PREV_FREE;
    int bucket = (int)(log((double)((hdr_for_payload(ptr)->payloadsz)&SIZE_MASK))/LOG_TWO);
    //printf("bucket/hash: %d -> payload %d\n", bucket, (int)((hdr_for_payload(ptr))->payloadsz)&SIZE_MASK);   

    //printf("Help: %zu", roundup(7, ALIGNMENT));
    //if(((char *)buckets + bucket*8) == NULL) printf("Fucking Crazy\n");

    void *bucketFirst = buckets[bucket];

    //(int)(log((double)requestedsz))/LOG_TWO
    if(ptr < max_block){
        //printf("\n\nBEFORE IN FREE: %d\n",((headerT*)((char*)ptr + (((hdr_for_payload(ptr))->payloadsz)&SIZE_MASK)))->payloadsz );
        ((headerT*)((char*)ptr + (((hdr_for_payload(ptr))->payloadsz)&SIZE_MASK)))->payloadsz |= PREV_FREE;
       // printf("AFTER IN FREE: %d",((headerT*)((char*)ptr + (((hdr_for_payload(ptr))->payloadsz)&SIZE_MASK)))->payloadsz  );
    }
    
    if(ptr != min_block /*(hdr_for_payload(ptr)->prevpayloadsz != INIT_MASK*/ ){ 
       void *prev_block = (char*)hdr_for_payload(ptr) - ((hdr_for_payload(ptr)->prevpayloadsz)&SIZE_MASK);
       hdr_for_payload(prev_block)->payloadsz |= NEXT_FREE;
    }
    //printf("\nFREE!!!\\n\nPayload in the begginning of free %d\n", hdr_for_payload(ptr)->payloadsz&SIZE_MASK); 
    hdr_for_payload(ptr)->payloadsz |= FREE_MASK;

    if(ptr > max_block) max_block = ptr;

    if(bucketFirst != NULL) *(void **)((char *)bucketFirst + sizeof(void *)) = ptr;

    *(void **)ptr = bucketFirst;
    *(void **)((char*)ptr + sizeof(void *)) = NULL;
    buckets[bucket] = ptr;
    //printf("FREE PORRA - Buckets[bucket] %p bucket %d\n", buckets[bucket], bucket);


    //printf("Payload in the end of free %d\n", hdr_for_payload(ptr)->payloadsz&SIZE_MASK); 
    
    /*printf("\n\nFree List 3 F\n\n");
for (int i = 0; i < NUM_BUCKETS; i++){
        void *curr = buckets[i];
        printf("Curr: %p\n", curr);

        // bucketNo = i;
        //printf("I am here\n");
        printf("%d: \n", i); 
        while(curr != NULL){
            printf("CURR: %p, payloads %d \n", curr, (hdr_for_payload(curr)->payloadsz)&SIZE_MASK);
            curr = *(void**)curr;
        }  
        printf("\n");
    }*/

    /*if(ptr == (void*)0x1070076fb0){
    printf("AddressREMAINDER1.75: %p, payloadsz %d and prevpayload: %d ", ptr, (hdr_for_payload(ptr))->payloadsz&SIZE_MASK, (hdr_for_payload(ptr))->prevpayloadsz&SIZE_MASK);
        if(((hdr_for_payload(ptr))->payloadsz&FREE_MASK) != 0) printf(" F ");
        else printf(" !F ");

        if(((hdr_for_payload(ptr))->payloadsz&NEXT_FREE) != 0) printf(" NF ");
        else printf(" !NF ");

        if(((hdr_for_payload(ptr))->payloadsz&PREV_FREE) != 0) printf(" PF ");
        else printf(" !PF ");

        if(((hdr_for_payload(ptr))->prevpayloadsz&GARBAGE_MASK) != 0) printf(" G  ");
        printf("\n");
    }*/

    
}


// realloc built on malloc/memcpy/free is easy to write.
// This code will work ok on ordinary cases, but needs attention
// to robustness. Realloc efficiency can be improved by
// implementing a standalone realloc as opposed to
// delegating to malloc/free.
void *myrealloc(void *oldptr, size_t newsz)
{
    //if(oldptr == (void*)0x1070076f68)printf("----------------------------------------------------------0\n");

    /*printf("\n\nFree List R\n\n");
    for (int i = 0; i < NUM_BUCKETS; i++){
        void *curr = buckets[i];
        printf("Curr: %p\n", curr);

        // bucketNo = i;
        //printf("I am here\n");
        printf("%d: \n", i); 
        while(curr != NULL){
            printf("CURR: %p, payloads %d \n", curr, (hdr_for_payload(curr)->payloadsz)&SIZE_MASK);
            curr = *(void**)curr;
        }  
        printf("\n");
    }*/

    /*headerT* header = hdr_for_payload(oldptr);
    if(header&NEXT_FREE != 0){
        headerT* next = (headerT *)((char *)oldptr + header->payloadsz + BUFFER);
        header->payloadsz =  
        if(newsz <= ){
            
        }
    }*/       
    //printf("\n\n\nAdresses\n\n");

    /*if(oldptr == (void*)0x1070082ED8){
    printf("\nAddress: %p, payloadsz %d and prevpayload: %d ", oldptr, (hdr_for_payload(oldptr))->payloadsz&SIZE_MASK, ((hdr_for_payload(oldptr))->prevpayloadsz)&SIZE_MASK);
        if(((hdr_for_payload(oldptr)->payloadsz&FREE_MASK) != 0)) printf(" F ");
        else printf(" !F ");

        if(((hdr_for_payload(oldptr)->payloadsz&NEXT_FREE) != 0)) printf(" NF ");
        else printf(" !NF ");

        if(((hdr_for_payload(oldptr)->payloadsz&PREV_FREE) != 0)) printf(" PF ");
        else printf(" !PF ");

        if(((hdr_for_payload(oldptr)->prevpayloadsz&GARBAGE_MASK)) != 0) printf(" G  ");
        printf("\n");
    }*/


    /*if((void*)((char*)oldptr) >= (void*)0x107008b338 || (void*)((char*)oldptr + 33888)  > (void*)0x107008b338){
    printf("Address1.0: %p, payloadsz %d and prevpayload: %d ", (void*)0x107008b338, (hdr_for_payload((void*)0x107008b338))->payloadsz&SIZE_MASK, ((hdr_for_payload((void*)0x107008b338))->prevpayloadsz)&SIZE_MASK);
        if(((hdr_for_payload((void*)0x107008b338))->payloadsz&FREE_MASK) != 0) printf(" F ");
        else{ printf(" !F ");}

        if(((hdr_for_payload((void*)0x107008b338))->payloadsz&NEXT_FREE) != 0) printf(" NF ");
        else printf(" !NF ");

        if(((hdr_for_payload((void*)0x107008b338))->payloadsz&PREV_FREE) != 0) printf(" PF ");
        else printf(" !PF ");

        if(((hdr_for_payload((void*)0x107008b338))->prevpayloadsz&GARBAGE_MASK) != 0) printf(" G  ");
        printf("\n");
    }*/

    //printf("\n\nREALLOC\n\n");
    //printf("\nnew size %zu\n", newsz);
    //printf("R NEW\n");
    //if(oldptr != NULL) printf("AddressME: %p, payloadsz %d and prevpayload: %d \n", oldptr, (hdr_for_payload(oldptr))->payloadsz&SIZE_MASK, ((hdr_for_payload(oldptr))->prevpayloadsz)&SIZE_MASK);
    
        

    unsigned int oldsz = (hdr_for_payload(oldptr)->payloadsz)&SIZE_MASK; 
    unsigned int new_size = roundup((unsigned int)newsz, ALIGNMENT) + BUFFER; 
    if(new_size == oldsz) return oldptr;
    //printf("\nOLD POINTER: %p, size %d  , NEXT BLOCK: %p, next-block size %d, new size %d\n", oldptr, oldsz,  next_block, (hdr_for_payload(next_block)->payloadsz)&SIZE_MASK,new_size);
  
    
    if(new_size < 16) new_size = 16; 

    if((oldsz < new_size) && (((hdr_for_payload(oldptr)->payloadsz)&NEXT_FREE) != 0) && (oldptr < max_block) && true == true){//what if it is max?? nextfree?? 
        
        unsigned int nextsz = (((headerT *)((char *)oldptr + oldsz))->payloadsz)&SIZE_MASK;
        //printf("HELP: %d\n", ((headerT*)((char*)oldptr + new_size + oldsz + sizeof(headerT) + nextsz - new_size))->payloadsz&SIZE_MASK);
        //printf("\n\nOLD SIZE: %d\n", oldsz);
        //printf("NEXT SIZE: %d\n", nextsz);
        //printf("new size %zu   NEW SIZE: %d\n", newsz, new_size);
        //printf("HERE1 %d\n", ((headerT* )(0x1070076f88))->payloadsz&SIZE_MASK);
        //printf("SIZE: %d\n",    nextsz + oldsz - new_size );
        void *next_block = payload_for_hdr((void *)((char *)oldptr + oldsz));
       // printf("Old Block: %p\n", oldptr);
       // printf("Next Block: %p\n", next_block);
        if((int)(nextsz + oldsz - new_size)  >= (int)(2*sizeof(void *))){
                //printf("HELLO SAVE ME PLEASE\n");
                //printf("nextsz1 %d nextsz2 %d", nextsz);
                int bucket = (int)((log((double)nextsz))/LOG_TWO);
                //printf("bucket/hash: %d -> nextsz %d\n", bucket, nextsz);

                void* prev_free = *(void **)((char*)next_block + sizeof(void*));
                void* next_free = *(void **)next_block;

                //printf("(next) block %p prev_free %p and next_free %p\n", next_block, prev_free, next_free);
                //printf("\nPREV: %p, NEXT: %p\n", prev_free, next_free); //if(next_free > max_block) max_block = next_free;;
                
                //printf("HELP2: %d\n", ((headerT*)((char*)oldptr + new_size + oldsz + sizeof(headerT) + nextsz - new_size))->payloadsz&SIZE_MASK);
              
              // if(((((headerT *)((char *)oldptr + oldsz))->prevpayloadsz)&GARBAGE_MASK) == 0){
                        
                        if(prev_free != NULL){
                            *(void **)prev_free = next_free;
                        }else{
                            //printf("I got here!!!\n");
                            //printf("1Next free %p and Buckets[bucket] %p bucket %d\n", next_free, buckets[bucket], bucket);
                            buckets[bucket] = next_free;
                            //printf("2Next free %p and Buckets[bucket] %p bucket %d\n", next_free, buckets[bucket], bucket);
                        }
                        
                        if(next_free != NULL){
                            //printf("I got here2!!!\n");
                            //printf("Prev free %p\n", prev_free);

                            *(void **)((char *)next_free + sizeof(void*)) = prev_free;
                        }


    
    //printf("\n\nFree List R2\n\n");
    for (int i = 0; i < NUM_BUCKETS; i++){
        void *curr = buckets[i];
        //printf("Curr: %p\n", curr);

        // bucketNo = i;
        //printf("I am here\n");
        //printf("%d: \n", i); 
        while(curr != NULL){
            //printf("CURR: %p, payloads %d \n", curr, (hdr_for_payload(curr)->payloadsz)&SIZE_MASK);
            curr = *(void**)curr;
        }  
        //printf("\n");
    }



        //printf("new size %zu   NEW SIZE: %d\n", newsz, (unsigned int)new_size|NEXT_FREE);
                //}
                //truncate
                //printf("HELP3: %d\n", ((headerT*)((char*)oldptr + new_size + oldsz + sizeof(headerT) + nextsz - new_size))->payloadsz&SIZE_MASK);
                (hdr_for_payload(oldptr))->payloadsz = ( ((unsigned int)new_size|NEXT_FREE) | (((hdr_for_payload(oldptr))->payloadsz)&PREV_FREE));
                //printf("NEW SIZE: %d\n", (hdr_for_payload(oldptr))->payloadsz&SIZE_MASK);
         
                void *remainder = (char *)oldptr + new_size;
                
                //up me is wrong

                ((headerT*)remainder)->payloadsz = oldsz +/* sizeof(headerT) +*/ nextsz - new_size;
                //printf("HELP MY NUMBRS ARE WRONG: %d\n", ((headerT*)remainder)->payloadsz); 
                //printf("BEFORE: %d\n", ((headerT*)((char*)payload_for_hdr(remainder) + ((headerT*)(remainder))->payloadsz))->payloadsz);
                //printf("HELP4: %d\n", ((headerT*)((char*)oldptr + new_size + oldsz + sizeof(headerT) + nextsz - new_size))->payloadsz&SIZE_MASK);
                ((headerT*)remainder)->prevpayloadsz = (unsigned int)new_size;
                //printf("REMAINDER SIZE: %d\n", ((headerT*)remainder)->payloadsz );

                remainder = payload_for_hdr(remainder);
                //printf("REMAINDER: %p", remainder);
                if(remainder <  max_block){ 
                    void* next = (char*)remainder + ((hdr_for_payload(remainder))->payloadsz&SIZE_MASK);
                    //printf("NEXT: %p", next);
                    ((headerT*)next)->prevpayloadsz = ((hdr_for_payload(remainder))->payloadsz&SIZE_MASK);
                   // ((headerT*)next)-> payloadsz = ((headerT*)((char*)oldptr + new_size + oldsz + sizeof(headerT) + nextsz - new_size))->payloadsz&SIZE_MASK;
                   //printf("AFTER: %d\n",((headerT *)next)->payloadsz );
                   //printf("HELP5: %d\n", ((headerT*)((char*)oldptr + new_size + oldsz + sizeof(headerT) + nextsz - new_size))->payloadsz&SIZE_MASK);
                }
                if(remainder > max_block) max_block = remainder;


    /*if((void*)((char*)oldptr) >= (void*)0x107008b338 || (void*)((char*)oldptr + 33888)  > (void*)0x107008b338){
    printf("Address1.5: %p, payloadsz %d and prevpayload: %d ", (void*)0x107008b338, (hdr_for_payload((void*)0x107008b338))->payloadsz&SIZE_MASK, ((hdr_for_payload((void*)0x107008b338))->prevpayloadsz)&SIZE_MASK);
        if(((hdr_for_payload((void*)0x107008b338))->payloadsz&FREE_MASK) != 0) printf(" F ");
        else{ printf(" !F ");}

        if(((hdr_for_payload((void*)0x107008b338))->payloadsz&NEXT_FREE) != 0) printf(" NF ");
        else printf(" !NF ");

        if(((hdr_for_payload((void*)0x107008b338))->payloadsz&PREV_FREE) != 0) printf(" PF ");
        else printf(" !PF ");

        if(((hdr_for_payload((void*)0x107008b338))->prevpayloadsz&GARBAGE_MASK) != 0) printf(" G  ");
        printf("\n");
    }*/

               

                myfree(remainder); ///ALREADY WRONG we have to take off the freelist??????

    /*if(oldptr == (void*)0x1070076f68){
    //printf("AddressREMAINDER2: %p, payloadsz %d and prevpayload: %d ", remainder, (hdr_for_payload(remainder))->payloadsz&SIZE_MASK, (hdr_for_payload(remainder))->prevpayloadsz&SIZE_MASK);
        if(((hdr_for_payload(remainder))->payloadsz&FREE_MASK) != 0) printf(" F ");
        else printf(" !F ");

        if(((hdr_for_payload(remainder))->payloadsz&NEXT_FREE) != 0) printf(" NF ");
        else printf(" !NF ");

        if(((hdr_for_payload(remainder))->payloadsz&PREV_FREE) != 0) printf(" PF ");
        else printf(" !PF ");

        if(((hdr_for_payload(remainder))->prevpayloadsz&GARBAGE_MASK) != 0) printf(" G  ");
        printf("\n");
    }*/


    /*if((void*)((char*)oldptr) >= (void*)0x107008b338 || (void*)((char*)oldptr + 33888)  > (void*)0x107008b338){
    printf("Address2.0: %p, payloadsz %d and prevpayload: %d ", (void*)0x107008b338, (hdr_for_payload((void*)0x107008b338))->payloadsz&SIZE_MASK, ((hdr_for_payload((void*)0x107008b338))->prevpayloadsz)&SIZE_MASK);
        if(((hdr_for_payload((void*)0x107008b338))->payloadsz&FREE_MASK) != 0) printf(" F ");
        else{ printf(" !F ");}

        if(((hdr_for_payload((void*)0x107008b338))->payloadsz&NEXT_FREE) != 0) printf(" NF ");
        else printf(" !NF ");

        if(((hdr_for_payload((void*)0x107008b338))->payloadsz&PREV_FREE) != 0) printf(" PF ");
        else printf(" !PF ");

        if(((hdr_for_payload((void*)0x107008b338))->prevpayloadsz&GARBAGE_MASK) != 0) printf(" G  ");
        printf("\n");
    }*/


                //if(oldptr == (void*)0x1070076f68)printf("----------------------------------------------------------1\n\n");
                return oldptr;
        }
    }     
    //This is prpbably inefficient 
    void *newptr = mymalloc(new_size);
    void* above_me = (char *)newptr + (((hdr_for_payload(newptr))->payloadsz)&SIZE_MASK); 
    //printf("ABOVE ME: %p, payloadsz: %d, prev_payloadsz: %d", above_me, ((headerT*)above_me)->payloadsz, ((headerT*)above_me)->prevpayloadsz);
    above_me = payload_for_hdr(above_me);
    if(above_me < max_block){
        void* above_that = (char *)above_me + (((hdr_for_payload(above_me))->payloadsz)&SIZE_MASK);
        ((headerT *)above_that)->prevpayloadsz = ((hdr_for_payload(above_me))->payloadsz)&SIZE_MASK;
    }
    memcpy(newptr, oldptr, oldsz < new_size ? oldsz: new_size);

    /*if((void*)((char*)oldptr) >= (void*)0x107008b338 || (void*)((char*)oldptr + 33888)  > (void*)0x107008b338){
    printf("Address2.5: %p, payloadsz %d and prevpayload: %d ", (void*)0x107008b338, (hdr_for_payload((void*)0x107008b338))->payloadsz&SIZE_MASK, ((hdr_for_payload((void*)0x107008b338))->prevpayloadsz)&SIZE_MASK);
        if(((hdr_for_payload((void*)0x107008b338))->payloadsz&FREE_MASK) != 0) printf(" F ");
        else{ printf(" !F ");}

        if(((hdr_for_payload((void*)0x107008b338))->payloadsz&NEXT_FREE) != 0) printf(" NF ");
        else printf(" !NF ");

        if(((hdr_for_payload((void*)0x107008b338))->payloadsz&PREV_FREE) != 0) printf(" PF ");
        else printf(" !PF ");

        if(((hdr_for_payload((void*)0x107008b338))->prevpayloadsz&GARBAGE_MASK) != 0) printf(" G  ");
        printf("\n");
    }*/


    //printf("\nAfter the malloc in realloc\n");


    /*int n=44, i =0;
    unsigned char* byte_array = oldptr;
    printf("Old Pointer: ");
    while (i < n)
    {
       printf("%02X",(unsigned)byte_array[i]);
       i++;
    }

    i =0;
    byte_array = newptr;
    printf("\nNew Pointer: ");
    while (i < n)
    {
       printf("%02X",(unsigned)byte_array[i]);
       i++;
    }*/
    myfree(oldptr);

    /*if((void*)((char*)oldptr) >= (void*)0x107008b338 || (void*)((char*)oldptr + 33888)  > (void*)0x107008b338){
    printf("Address3.0: %p, payloadsz %d and prevpayload: %d ", (void*)0x107008b338, (hdr_for_payload((void*)0x107008b338))->payloadsz&SIZE_MASK, ((hdr_for_payload((void*)0x107008b338))->prevpayloadsz)&SIZE_MASK);
        if(((hdr_for_payload((void*)0x107008b338))->payloadsz&FREE_MASK) != 0) printf(" F ");
        else{ printf(" !F ");}

        if(((hdr_for_payload((void*)0x107008b338))->payloadsz&NEXT_FREE) != 0) printf(" NF ");
        else printf(" !NF ");

        if(((hdr_for_payload((void*)0x107008b338))->payloadsz&PREV_FREE) != 0) printf(" PF ");
        else printf(" !PF ");

        if(((hdr_for_payload((void*)0x107008b338))->prevpayloadsz&GARBAGE_MASK) != 0) printf(" G  ");
        printf("\n");
    }*/

    //if(oldptr == (void*)0x1070076f68)printf("----------------------------------------------------------2\n\n");
    return newptr;
}

// validate_heap is your debugging routine to detect/report
// on problems/inconsistency within your heap data structures
bool validate_heap()
{
    void *ptra = min_block;
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
    }

  return true;
}

