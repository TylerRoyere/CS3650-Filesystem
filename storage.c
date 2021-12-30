// based on cs3650 starter code
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include "storage.h"
#include "sizes.h"
#include "directory.h"
#include "util.h"


// gets the inode corresponding to path and fills in
// the inode number pointed to by inum (if not NULL) and
// the inode ptr pointer to by ptr (if not NULL) returning
// zero for success
int
path_get_inode(const char* path, int* inum, inode** ptr)
{
    // get inode of path
    int inode_index = tree_lookup(path);
    if(inode_index < 0) {
        return inode_index;
    }

    // get the corresponding inode entry
    inode* node = get_inode(inode_index);
    if(node == 0) {
        return -EIO;
    }

    if(inum) {
        *inum = inode_index;
    }
    
    if(ptr) {
        *ptr = node;
    }

    return 0;
}

// init storage
void
storage_init(const char* path)
{
    pages_init(path);
    directory_init();
}

// get inode status information
int
storage_stat(const char* path, struct stat* st)
{
    int inode_index, rv;
    inode* node = 0;
    memset(st, 0, sizeof(struct stat));
    if((rv = path_get_inode(path, &inode_index, &node))) {
        return rv;
    }

    // fill in the status structure
    st->st_ino = inode_index;
    st->st_mode = node->mode;
    st->st_nlink = node->refs;
    st->st_size = node->size;
    st->st_uid = getuid();
    st->st_atime = node->atime;
    st->st_mtime = node->mtime;

    return 0;
}

// reads from a given page into a given buffer starting at index
// start until either the end of the page or length bytes are read
// returning the number of bytes read
static
int
read_page(char* page, char* buf, int start, int length)
{
    int ind = start;
    int read_bytes = 0;
    while( (read_bytes < length) && (ind < page_size) ) {
        buf[read_bytes] = page[ind];
        ind += 1;
        read_bytes += 1;
    }

    return read_bytes;
}

// read from the specified file starting at offset, a number of bytes
// size or until the file is done 
int
storage_read(const char* path, char* buf, size_t size, off_t offset)
{
    int rv;
    inode* node = 0;
    if((rv = path_get_inode(path, 0, &node))) {
        return rv;
    }

    // if this is a directory is cannot be read from
    if(S_ISDIR(node->mode)) {
        return -EISDIR;
    }

    // check that offset is inside file contents
    if(offset >= node->size) {
        return 0;
    }

    // update access time
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    node->atime = ts.tv_sec;
    
    // in order to do the read a few things are tracked
    // 1. current page being read from
    // 2. bytes read
    // 3. bytes left to read
    int page_index = offset/page_size;

    // bytes left to read is the smaller of 
    // 1. size of buffer
    // 2. bytes left in file 
    int bytes_left = size;
    if(size > (node->size - offset)) {
        bytes_left = node->size - offset;
    }

    int bytes_read = 0;
    int page_offset = offset % page_size;
    char* page = 0;
    while(bytes_left > 0) {
        page = inode_get_page(node, page_index++);
        if(page == 0) {
            printf("bad read page\n");
            break;
        }

        int read = read_page(page, buf+bytes_read, page_offset, bytes_left);
        bytes_read += read;
        bytes_left -= read;

        // after reading from the first page, offset is 0
        page_offset = 0;
    }

    return bytes_read; 
}

// writes to a given page starting at index start into a given
// buffer of size length until either the buffer used or the 
// page ends returning the number of bytes written 
static
int
write_page(char* page, char* buf, int start, int length)
{
    int ind = start;
    int write_bytes = 0;
    while( (write_bytes < length) && (ind < page_size) ) {
        page[ind] = buf[write_bytes];
        ind += 1;
        write_bytes += 1;
    }

    return write_bytes;
}


// writes data in buf of length size, into a storage object at path
// starting at offset bytes in the file 
int
storage_write(const char* path, const char* buf, size_t size, off_t offset)
{
    int rv;
    inode* node = 0;
    if((rv = path_get_inode(path, 0, &node))) {
        return rv;
    }

    // if this is a directory is cannot be read from
    if(S_ISDIR(node->mode)) {
        return -EISDIR;
    }

    // check that offset is allowed 
    if(offset > node->size) {
        rv = storage_truncate(path, offset);
        if(rv) {
            return rv;
        }
    }

    // try to grow node to needed size
    if((offset + size) > node->size) {
        rv = grow_inode(node, offset+size - node->size);
        if(rv) {
            return rv;
        }
    }

    // update modification time
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    node->mtime = ts.tv_sec;

    // now just need to write buffer contents to file
    int page_index = offset / page_size;
    int bytes_left = size;
    int page_offset = offset % page_size;
    int write_bytes = 0;
    char* page = 0;
    while(bytes_left > 0) {
        page = inode_get_page(node, page_index++);
        if(page == 0) {
            printf("bad write page\n");
            return 0;
        }

        int bytes = write_page(page, (char*)(buf + write_bytes), page_offset, bytes_left);
        write_bytes += bytes;
        bytes_left -= bytes;

        // after first page, writing is aligned
        page_offset = 0;
    }
    
    return write_bytes;
}

// sets the size of a file to size, appending /000 if necessary
int
storage_truncate(const char *path, off_t size)
{
    inode* node;
    int rv = path_get_inode(path, 0, &node);
    if(rv) {
        return rv;
    }
    
    if(size > node->size) {
        return grow_inode(node, size - node->size);
    }
    else if(size < node->size) {
        return shrink_inode(node, node->size - size);
    }
    return 0;
}

// fills buf with string corresponding to parent directory of path
static
void
get_parent_dir(const char* path, char* buf, int length)
{
    memset(buf, 0, length);
    int end = path_file_index(path);
    strncpy(buf, path, end-1);
}


// create an inode storage object at path with mode
int
storage_mknod(const char* path, int mode)
{
    // root directory already created
    if(streq(path, "/")) {
        return 0;
    }

    // get the directories in the path
    char dir[strlen(path) + 10];
    get_parent_dir(path, dir, sizeof(dir));
    printf("mknod path %s -> dir %s\n", path, dir);

    // find the inode of the directory
    inode* dir_node;
    int rv = path_get_inode(dir, 0, &dir_node);
    if(rv) {
        return rv;
    }

    // allocate a new inode
    int new_inode = alloc_inode();
    if(new_inode < 0) {
        return new_inode;
    }

    inode* new_node = get_inode(new_inode);
    if(new_inode == 0) {
        return -EIO;
    }
    new_node->mode = mode;
    char* name = (char*)path + path_file_index(path);
    
    // check if this name already exists in the directory
    if(directory_lookup(dir_node, name) >= 0) {
        return -EEXIST;
    }
    
    // add it to the directory
    printf("adding %s, %d to %s\n", name, new_inode, dir);
    return directory_put(dir_node, name, new_inode);
}


// unlinks a reference to path removing the directory reference
// and the underlying file if this was the last reference to it
int
storage_unlink(const char* path)
{
    // get the node corresponding to path
    inode* node = 0;
    int rv = path_get_inode(path, 0 ,&node);
    if(rv) {
        return rv;
    }

    char dir[strlen(path) + 10];
    get_parent_dir(path, dir, sizeof(dir));
    inode* dir_node;
    rv = path_get_inode(dir, 0, &dir_node);
    if(rv) {
        return rv;
    }

    // remove a references from the node
    return directory_delete(dir_node, path + path_file_index(path));
}


// creates a link at path 'to', to from
int
storage_link(const char *from, const char *to)
{
    inode* from_node;
    int from_inode;
    int rv = path_get_inode(from, &from_inode, &from_node);
    if(rv) {
        return rv;
    }

    // make a dirent in to's parent dir, with inum same as to
    char dir[strlen(to) + 10];
    get_parent_dir(to, dir, sizeof(dir));
    inode* to_dir;
    rv = path_get_inode(dir, 0, &to_dir);
    if(rv) {
        return rv;
    }

    return directory_put(to_dir, to + path_file_index(to), from_inode);
}


// renames path from to path 'to'
int
storage_rename(const char *from, const char *to)
{
    // get the parent directories of from and to
    char from_dir[strlen(from) + 10];
    char to_dir[strlen(to) + 10];
    get_parent_dir(from, from_dir, sizeof(from_dir));
    get_parent_dir(to, to_dir, sizeof(to_dir));
    char* from_file = (char*)from + path_file_index(from);
    char* to_file = (char*)to + path_file_index(to);

    printf("from dir %s, file %s\n", from_dir, from_file);
    printf("to dir %s, file %s\n", to_dir, to_file);

    // get inodes for both parent directories
    inode* from_dir_node, *to_dir_node;
    int rv = path_get_inode(from_dir, 0, &from_dir_node);
    if(rv) {
        return rv;
    }
    rv = path_get_inode(to_dir, 0 , &to_dir_node);
    if(rv) {
        return rv;
    }

    // also need to get information on the inode being moved
    int move_inode;
    inode* move_node;
    rv = path_get_inode(from, &move_inode, &move_node);
    if(rv) {
        return rv;
    }

    // if 'to' already exists, delete it
    directory_delete(to_dir_node, to_file);

    // now that we have both directories, delete from, add to
    rv = directory_put(to_dir_node, to_file, move_inode);
    if(rv) {
        return rv;
    }

    return directory_delete(from_dir_node, from_file);
}


// sets a new access and modification time for a storage object 
int
storage_set_time(const char* path, const struct timespec ts[2])
{
    inode* node;
    int rv = path_get_inode(path, 0, &node);
    if(rv) {
        return rv;
    }

    if(!node) {
        return -ENOENT;
    }

    node->mtime = ts[0].tv_sec;
    node->atime = ts[1].tv_sec;

    return 0;
}

// creates a symlink 'from' pointing to the file 'to'
int
storage_symlink(const char* to, const char* from)
{
    // make a file to hold the symlink
    int rv = storage_mknod(from, S_IFLNK | 0777);
    if(rv) {
        return rv;
    }
    
    // write the path 'to' into this inode to complete the 
    if(storage_write(from, to, strlen(to) + 1, 0) >= strlen(to)) {
        return 0;
    }

    return -EIO;
}

// updates the mode field of a storage object
int
storage_chmod(const char* path, mode_t mode)
{
    inode* node;
    int rv = path_get_inode(path, 0, &node);
    if(rv) {
        return rv;
    }

    if(!node) {
        return -ENOENT;
    }

    node->mode = mode;
    return 0;
}


slist*
storage_list(const char* path)
{
    inode* node;
    int rv = path_get_inode(path, 0, &node);
    if(rv) {
        return 0;
    }

    if(!S_ISDIR(node->mode)) {
        return 0;
    }
    else {
        return directory_list(path);
    }
}

