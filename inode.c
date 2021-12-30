// based on cs3650 starter code

#include <stdio.h>
#include <stdint.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include "inode.h"
#include "pages.h"
#include "util.h"
#include "bitmap.h"
#include "sizes.h"

// prints the contents of an inode
void 
print_inode(inode* node)
{
    printf("refs: %d\nmode: 0x%X\nsize: %d\nptrs: %d, %d\niptr: %d\n",
            node->refs, node->mode, node->size, node->ptrs[0],
            node->ptrs[1], node->iptr);
    if(node->iptr) {
        int* iptrs = pages_get_page(node->iptr);
        for(int ii = 0; ii < (page_size / sizeof(int)); ++ii) {
            printf("iptr[%d] = %d\n", ii, iptrs[ii]); 
        }
    }
}

// gets a pointer to the specified inode
inode*
get_inode(int num)
{
    // ensure this is a valid inode index
    if(num >= num_inodes) {
        return 0;
    }

    // get the page of the first inode
    inode* first_inode = (inode*)pages_get_page((int)inode_start_page);

    return (first_inode + num);
}

// allocates an inode from the bitmap
int
alloc_inode()
{
    char* bm = get_inode_bitmap();

    // finding an open inode
    for(long ii = 0; ii < num_inodes; ++ii) {
        if(bitmap_get(bm, ii) == 0) {
            // found free inode, mark it as allocated
            bitmap_put(bm, ii, 1);
            printf("+ alloc_inode(%li)\n", ii);
            inode* node = get_inode(ii);
            memset(node, 0, sizeof(inode));

            // set current modification time
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            node->mtime = ts.tv_sec;
            // use the current time for modification time of new inode
            return ii;
        }
    }

    // there are no more inodes left!
    return -ENOSPC;
}

// frees an inode from the bitmap
int
free_inode(inode* node)
{
    // decrement refs counter
    node->refs -= 1;
    if(node->refs > 0) {
        return 0;
    }

    shrink_inode(node, node->size);

    // getting integers to do some arithmetic with
    inode* start = get_inode(0);

    // index of an inode is the difference between the start
    // and node inode_ptr
    int inode_index;
    if(node > start) { // inode needs to be located after first inode
       inode_index = (node - start);
    }
    else {
        return -ENOENT;
    }

    if(inode_index < 0 || inode_index >= num_inodes) {
        return -ENOENT;
    }

    // can safely free the inode
    printf("+ free_inode(%d)\n", inode_index);
    bitmap_put(get_inode_bitmap(), inode_index, 0);
    return 0;
}

// free's an array of page ptrs
static
void
free_all_pages(int* arr, int size)
{
    for(int ii = 0; ii < size; ++ii) {
        if(arr[ii] >= 0) {
            free_page(arr[ii]);
        }
    }
}


// grows an inode, increasing the size and allocating
// additional data pages if needed
int
grow_inode(inode* node, int size)
{
    int old_pages_used = bytes_to_pages(node->size);
    int new_size = node->size + size;
    int new_pages_used = bytes_to_pages(new_size);

    // check that all of required pages even fit into an inode
    if(new_pages_used > ( (sizeof(node->ptrs) + page_size) / sizeof(int) )) {
        return -EFBIG;
    }

    // calculate the number of pages to be added 
    int add_pages = new_pages_used - old_pages_used;

    // if this is going to require indirect pointers 
    // allocate them now
    if(old_pages_used < 3 && new_pages_used >= 3) {
        int iptr_page = alloc_page();
        if(iptr_page < 0) {
            return -ENOSPC;
        }
        memset(pages_get_page(iptr_page), 0, page_size);
        node->iptr = iptr_page;
    }

    // if this inode has indirect pointers get them now
    int* iptrs = 0;
    int iptr_ind = 0;
    if(node->iptr) {
        iptrs = (int*)pages_get_page(node->iptr);
        // get the first available iptr
        for(int ii = 0; ii < (page_size / sizeof(int)); ++ii) {
            if(iptrs[ii] == 0) {
                iptr_ind = ii;
                break;
            }
        } 
    }

    // allocate all the pages needed in one go
    int new_pages[add_pages];
    for(int ii = 0; ii < add_pages; ++ii) {
        int new_page = alloc_page();
        if(new_page < 0) {
            free_all_pages(new_pages, ii);
            return -ENOSPC;
        }
        new_pages[ii] = new_page;
        memset(pages_get_page(new_page), 0, page_size);
    }
   
    // to grow we need to allocate more data blocks, these are
    // filled in in the following order
    // 1. ptrs[0]
    // 2. ptrs[1]
    // 3. iptrs[0] -> iptrs[NN-1] 
    for(int ii = 0; ii < add_pages; ++ii) {
        int new_page = new_pages[ii];
        if(new_page < 0) {
            // check page was allocated
            return -ENOSPC;
        }
        if(node->ptrs[0] == 0) {
            node->ptrs[0] = new_page;
        }
        else if(node->ptrs[1] == 0) {
            node->ptrs[1] = new_page;
        }
        else if(iptrs && 
                (iptr_ind < (page_size/sizeof(int)) ) &&
                (iptrs[iptr_ind] == 0)) {
            iptrs[iptr_ind++] = new_page;
        }
        else {
            // this shouldn't happen
            return -EFBIG;
        }
    }

    // successfully update size_field
    node->size += size;
    return 0;
}

// does the reverse of grow_inode
int
shrink_inode(inode* node, int size)
{
    if(size > node->size) {
        return -EINVAL;
    }

    // determine how many pages need to be freed
    int old_pages_used = bytes_to_pages(node->size);
    int new_size = node->size - size;
    int new_pages_used = bytes_to_pages(new_size);
    
    int free_pages = old_pages_used - new_pages_used;
    
    // get iptrs if needed
    int* iptrs = 0;
    int iptrs_ind = 0;
    if(node->iptr) {
        iptrs = pages_get_page(node->iptr);
        // get the last used iptr
        for(iptrs_ind = 0; iptrs_ind < (page_size / sizeof(int)); ++iptrs_ind) {
            if(iptrs[iptrs_ind] == 0) {
                iptrs_ind -= 1;
                break;
            }
        }
    }

    // pages are freed in the opposite order they are added
    // 1. iptrs
    // 2. ptrs[1]
    // 3. ptrs[0]
    for(int ii = 0; ii < free_pages; ++ii) {
        if(iptrs && 
           iptrs_ind < (page_size/sizeof(int)) &&
           iptrs[iptrs_ind]) {
            int temp = iptrs[iptrs_ind];
            iptrs[iptrs_ind] = 0;
            free_page(temp);
            if(iptrs_ind == 0) {
                free_page(node->iptr);
                node->iptr = 0;
            }
            iptrs_ind -= 1;
        }
        else if(node->ptrs[1]) {
            free_page(node->ptrs[1]);
            node->ptrs[1] = 0;
        }
        else if(node->ptrs[0]) {
            free_page(node->ptrs[0]);
            node->ptrs[0] = 0;
        }
        else {
            // probably shouldn't ever get here
            return -EIO;
        }
    }

    // successfully update size and return
    node->size -= size;
    return 0;
}


// returns the page at the specified index in the inode
void*
inode_get_page(inode* node, int index)
{
    if(index > (node->size / page_size)) {
        return 0;
    }

    if(index < 2) {
        return pages_get_page(node->ptrs[index]);
    }
    else if(node->iptr) {
        int* iptrs = pages_get_page(node->iptr);
        return pages_get_page(iptrs[index - 2]);
    }

    return 0;
}
