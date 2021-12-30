#include <string.h>
#include <bsd/string.h>
#include <errno.h>

#include "directory.h"
#include "bitmap.h"
#include "util.h"
#include "sizes.h"

static const int ents_per_page = (page_size / sizeof(dirent));

// initializes the root ("/") directory
void directory_init()
{
    char* inode_bm = get_inode_bitmap();
    // check if this is a fs 
    for(int ii = 0; ii < inode_end_page; ++ii) {
        if(bitmap_get(inode_bm, ii)) {
            return;
        }
    }
    
    // must be a fresh filesystem, lets initialize
    memset(pages_get_page(0), 0, page_size);
    // allocating pages for inodes
    for(int ii = inode_start_page; ii < inode_end_page; ++ii) {
        alloc_page();
    }
    alloc_inode(); // allocate inode for root dir always inode 0
    inode* root_inode = get_inode(0);
    root_inode->mode = 040755;
    root_inode->size = 0;
    root_inode->refs = 1;
}

// given a page of dirents, returns the inum of an entry with 
// inode node name, -1 if none found
static
int
page_find_name(void* page, const char* name, int size)
{
    dirent* entries = (dirent*)page;
    for(int ii = 0; ii < size; ++ii) {
        if(streq(entries[ii].name, name)) {
            return entries[ii].inum;
        }
    }
    return -1;
}

// finds the index of the dirent with name and returns the index
// in the page, -1 otherwise
static
int
page_find_name_index(void* page, const char* name, int size)
{
    dirent* entries = (dirent*)page;
    for(int ii = 0; ii < size; ++ii) {
        if(streq(entries[ii].name, name)) {
            return ii;
        }
    }
    return -1;
}

// for a given page index in a directory, return how
// many dirents are in that page
static
int
directory_page_length(inode* dd, int index)
{
    int num_ents = dd->size / sizeof(dirent);
    int last_page = dd->size / page_size;
    if(index < last_page) {
        return ents_per_page;
    }
    else {
        return num_ents - (ents_per_page * last_page);
    }
}

// for a given directory return the number of pages used
// to store all of its dirents
static
int
directory_num_pages(inode* dd)
{
    int npages = dd->size / page_size;
    if(dd->size > (npages * page_size)) {
        npages += 1;
    }

    return npages;
}


// looks in the directory inode dd, looking for name
// returning 'names's inode index
int 
directory_lookup(inode* dd, const char* name)
{
    // get the number of entries, and therefore the number of 
    // pages used to store all directory entries
    int dir_pages = directory_num_pages(dd);
    
    // go through directory entry pages looking for name 
    char* curr_dir_page = 0;
    for(int ii = 0; ii < dir_pages; ++ii) {
        curr_dir_page = inode_get_page(dd, ii);
        if(curr_dir_page == 0) { 
            return -EIO;
        }
        
        // use the specified page to look for name
        int arr_size = directory_page_length(dd, ii);
        int inode = page_find_name(curr_dir_page, name, arr_size);
        if(inode > 0) {
            // we found the name, return it's inode
            return inode;
        }
    }
    return -ENOENT;
}


// looks up path starting from the root node, in a tree way, returning
// the inode index corresponding to path's parent dir
int 
tree_lookup(const char* path)
{
    // get a list of strings from the path
    slist* dir_list = s_split(path+1, '/');
    slist* swalk = dir_list;
    int iwalk = root_inode;

    // parse the tree until the last search
    while(swalk) {
        inode* node = get_inode(iwalk);
        iwalk = directory_lookup(node, swalk->data);
        if(iwalk < 0) {
            return iwalk;
        }
        swalk = swalk->next;
    }
    
    // free list of names
    s_free(dir_list);

    return iwalk;
}

// adds a name an inode, inum, to a directory dd
int 
directory_put(inode* dd, const char* name, int inum)
{
    // grow directory to fit another dirent
    int rv = grow_inode(dd, sizeof(dirent));
    if(rv){
        return rv;
    }

    // get the inode corresponing to inum
    inode* node = get_inode(inum);
    if(node == 0) {
        return -ENOENT;
    }

    // get index and index of page to use for new entry
    int num_ents = dd->size / sizeof(dirent);
    int new_ind = num_ents - 1;
    int page_ind = (new_ind / ents_per_page);
    
    dirent* dir_page = inode_get_page(dd, page_ind);

    dirent* entry = &dir_page[new_ind % ents_per_page];
    strlcpy(entry->name, name, DIR_NAME);
    entry->inum = inum;
    node->refs += 1;
    return 0;
}

// deletes the specified name from the directory
int 
directory_delete(inode* dd, const char* name)
{
    int num_ents = dd->size / sizeof(dirent);
    int dir_pages = directory_num_pages(dd);
    
    // if there are no entry's this always fails
    if(num_ents == 0) {
        return -ENOENT;
    }

    if(dd->iptr) {
        int* iptrs = pages_get_page(dd->iptr);
    }
    
    // can remove a directory entry by swapping the last entry with
    // the entry with 'name' and then deleting the last entry
    char* curr_page = 0;
    int find = 0;

    // look for entry in all pages
    for(int ii = 0; ii < dir_pages; ++ii) {
        curr_page = inode_get_page(dd, ii);
        if(curr_page == 0){
            return -EIO;
        }
        
        int arr_size = directory_page_length(dd, ii);
        find = page_find_name_index(curr_page, name, arr_size);

        if(find >= 0) {
            break;
        }
    }

    // never found the entry
    if(find < 0) {
        return -ENOENT;
    }

    // we have the page and index of the entry to remove, just get the 
    // last entry and puts its contents into the one to be removed then
    // then shrink
    int last_page_index = directory_page_length(dd, dir_pages-1) - 1;
    dirent* last_page = inode_get_page(dd, dir_pages-1);
    dirent* last =  last_page + last_page_index;
    dirent* remove = (dirent*)curr_page + find;

    // need to free the inode
    int rv = free_inode(get_inode(remove->inum));
    if(rv) {
        return rv;
    }
    
    // move last inodes contents to the entry that's being removed
    memmove(remove, last, sizeof(dirent));
    // just in case
    memset(last, 0, sizeof(dirent));

    return shrink_inode(dd, sizeof(dirent));
}


// returns a string list of all the inode names inside the 
// specified directory
slist* 
directory_list(const char* path)
{
    // get the inode of the directory
    int inode_index = tree_lookup(path);
    if(inode_index < 0) {
        return 0;
    }

    inode* dir_node = get_inode(inode_index);
    if(dir_node == 0) {
        return 0;
    }
    
    // loop through all pages of dirent, getting names
    int num_pages = directory_num_pages(dir_node);
    dirent* dirents;
    int length;
    slist* list = 0;
    for(int page_index = 0; page_index < num_pages; ++page_index) {
        dirents = inode_get_page(dir_node, page_index);
        length = directory_page_length(dir_node, page_index);
        for(int ii = 0; ii < length; ++ii) {
            list = s_cons(dirents[ii].name, list);
        }
    }

    return list;
}


// helper function for printing
void 
print_directory(inode* dd)
{
    int num_ents = dd->size / sizeof(dirent);
    int num_pages = directory_num_pages(dd);

    for(int ii = 0; ii < num_pages; ++ii) {
        dirent* curr_page = (dirent*)inode_get_page(dd, ii);
        if(curr_page) {
            for(int jj = 0; jj < ents_per_page; ++jj) {
                printf("%5d: %28s, %3d\n", 
                        (ii * ents_per_page) + jj,
                        curr_page[jj].name,
                        curr_page[jj].inum);
            }
        }
    }
    printf("\n");
}

