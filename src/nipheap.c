/**
 * @file 
 * @brief Heap for storing candidate groups of variables for various algorithms.
 *
 * @author Janne Toivola
 * @author Antti Rasinen
 * @author Mikko Korpela
 * @copyright &copy; 2007,2012 Janne Toivola <br>
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version. <br>
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details. <br>
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "nipheap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "niplists.h"
#include "niperrorhandler.h"

#define NIP_DEBUG_HEAP

/* Defines the heap order between two heap items */
static int nip_heap_less_than(nip_heap_item h1, nip_heap_item h2);

/* Helper function for nip_build_min_heap */
static void nip_min_heapify(nip_heap h, int i);



/*** Public functions ***/
nip_heap nip_new_heap(int initial_size, 
		      int (*primary)(void* item, int size),
		      int (*secondary)(void* item, int size)) {
  nip_heap h;

  if (initial_size <= 0){
    nip_report_error(__FILE__, __LINE__, EINVAL, 1);
    return NULL;
  }

  h = (nip_heap) malloc(sizeof(nip_heap_struct));
  if(!h){
    nip_report_error(__FILE__, __LINE__, ENOMEM, 1);
    return NULL;
  }
  
  h->heap_items = (nip_heap_item*) calloc(initial_size, sizeof(nip_heap_item));
  if(!h->heap_items){
    nip_report_error(__FILE__, __LINE__, ENOMEM, 1);
    free(h);
    return NULL;
  }

  h->heap_size = 0;
  h->allocated_size = initial_size;
  h->primary_key = primary;
  h->secondary_key = secondary;
  h->heapified = 0;
  h->updated_items = nip_new_int_list();
  if(!h->updated_items){
    nip_report_error(__FILE__, __LINE__, ENOMEM, 1);
    free(h->heap_items);
    free(h);
    return NULL;
  }

  return h;
}


int nip_heap_insert(nip_heap h, void* content, int size) {
  int i;
  nip_heap_item hi;
  nip_heap_item* bigger;

  /* Create a new heap element */
  hi = (nip_heap_item) malloc(sizeof(nip_heap_item_struct));
  if(!hi)
    return nip_report_error(__FILE__, __LINE__, ENOMEM, 1);
  hi->content = content;
  hi->content_size = size;
  hi->primary_key = (*h->primary_key)(hi->content, hi->content_size);
  hi->secondary_key = (*h->secondary_key)(hi->content, hi->content_size);

  /* Assign it to the heap */
  if(h->heap_size == h->allocated_size){
    /* time to expand */
    i = 2 * h->allocated_size;
    bigger = (nip_heap_item*) realloc(h->heap_items, i); /* realloc */
    if(bigger != NULL){
      h->heap_items = bigger;
      h->allocated_size = i;
      for(i = h->allocated_size-1; i >= h->heap_size; i--)
	h->heap_items[i] = NULL; /* Empty the newly allocated area */
    }
    else
      return nip_report_error(__FILE__, __LINE__, ENOMEM, 1);
  }
  h->heap_items[h->heap_size] = hi;
  h->heap_size++;
  h->heapified = 0;
  /* TODO: Just prepend index to h->updated_items? */

  return 0;
}


int nip_search_heap_item(nip_heap h, 
			 int (*comparison)(void* i, int isize, 
					   void* r, int rsize), 
			 void* ref, int refsize){
    /* Finds the heap element that matches the comparison operation with
     * provided reference content. */
    /* Linear search, but at the moment the only option */
    /* Hopefully this will not be too slow */
    int n;
    nip_heap_item hi;

    for (n = 0; n < h->heap_size; n++) {
      hi = h->heap_items[n];
      if ((*comparison)(hi->content, hi->content_size, ref, refsize))
	return n;
    }
    return -1;
}


void* nip_get_heap_item(nip_heap h, int index, int* size) {
  nip_heap_item hi;
  if (h == NULL || index < 0 || index >= h->heap_size){
    nip_report_error(__FILE__, __LINE__, ENOMEM, 1);
    if (size != NULL)
      *size = 0;
    return NULL;
  }
  hi = h->heap_items[index];
  if (size != NULL)
    *size = hi->content_size;
  return hi->content;
}


int nip_set_heap_item(nip_heap h, int index,
		      void* content, int size) {
  nip_heap_item hi; 
  
  if (h == NULL || index < 0 || index >= h->heap_size || size <= 0)
    return nip_report_error(__FILE__, __LINE__, EINVAL, 1);

  hi = h->heap_items[index];
  hi->content = content; /* FIXME: free(hi->content) here or in nipgraph.c */
  hi->content_size = size;
  hi->primary_key = (*h->primary_key)(hi->content, hi->content_size);
  hi->secondary_key = (*h->secondary_key)(hi->content, hi->content_size);
  /*h->heapified = 0;*/
  nip_append_int(h->updated_items, index);

  return 0;
}


void nip_build_min_heap(nip_heap h) {
  int i, index;
  nip_int_link il;
  if(!h)
    return;
  if(!h->heapified){
    /* heapify everything */
    for (i = NIP_HEAP_PARENT(h->heap_size-1); i >= 0; i--)
      nip_min_heapify(h, i);
  }
  else {
    /* heapify only updated items */
    il = NIP_LIST_ITERATOR(h->updated_items);
    while (il != NULL){
      index = NIP_LIST_ELEMENT(il);
      nip_min_heapify(h, index);
      il = NIP_LIST_NEXT(il);
    }
  }
  nip_empty_int_list(h->updated_items);
  h->heapified = 1;
}


void* nip_heap_extract_min(nip_heap h, int* size) {
    nip_heap_item min;	/* item with smallest weight */
    void* c;

    if (h->heap_size < 1) {
      if (size != NULL)
	*size = 0;
      return NULL;
    }

    /* FIXME: check h->heapified and h->updated_items ? */

    /* Min is the first one in the array, _if heapified_ */
    min = h->heap_items[0];

    /* Move the last one to the top */
    h->heap_items[0] = h->heap_items[h->heap_size-1]; 
    h->heap_items[h->heap_size-1] = NULL;
    h->heap_size--;

    /* JJ: nip_update_cluster_heap() was originally here */

    /* Push the new root downwards if not smaller than children */
    nip_min_heapify(h, 0);
    
    if (size != NULL)
      *size = min->content_size; /* Return content size */
    c = min->content;
    free(min);
    return c; /* Return content */
}


void nip_free_heap(nip_heap h) {
  int i;
  nip_heap_item hi;

  if(!h)
    return;

  /* Go through every heap item*/
  for(i = 0; i < h->heap_size; i++){
    hi = h->heap_items[i];
    if(hi){
      free(hi);
    }
  }
  free(h->heap_items);
  nip_empty_int_list(h->updated_items);
  free(h->updated_items);
  free(h);
  return;
}


int nip_heap_size(nip_heap h) {
  if(h)
    return h->heap_size;
  return 0;
}


static int nip_heap_less_than(nip_heap_item h1, nip_heap_item h2) {
  if(h1!=NULL){
    if(h2!=NULL)
      return ((h1->primary_key < h2->primary_key) || 
	      (h1->primary_key == h2->primary_key && 
	       h1->secondary_key < h2->secondary_key));
    else
      return 1; /* h2 == NULL => belongs to the bottom of the heap */
  }
  return 0; /* h1 == NULL => belongs to the bottom */
}


static void nip_min_heapify(nip_heap h, int i) {
    int l,r;
    int min, flag;
    nip_heap_item temp;
    
    do {
        flag = 0;   
        l = NIP_HEAP_LEFT(i); r = NIP_HEAP_RIGHT(i);
    
        /* Note the difference between l (ell) and i (eye) */
	min = i;
	if (l < h->heap_size && 
	    nip_heap_less_than(h->heap_items[l], h->heap_items[i]))
	  min = l;
        if (r < h->heap_size && 
	    nip_heap_less_than(h->heap_items[r], h->heap_items[min]))
	  min = r;
            
        if (min != i) {
            /* Exchange array[min] and array[i] */
            temp = h->heap_items[min];
            h->heap_items[min] = h->heap_items[i];
            h->heap_items[i] = temp;
            i = min; flag = 1;
        }
    } while (flag);
}
