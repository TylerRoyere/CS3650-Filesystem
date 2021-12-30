#ifndef SIZES_H
#define SIZES_H

/**
 * This file defines some of the constants used throughout
 * the program, allowing them to be changed from a central
 * locations for easier adjustments
 */

// page size
static const int page_size = 4096;

// number of data blocks of page_size
static const int num_pages = (1024 * 1024 * 1024) / page_size;

// root node is always first inode
static const int root_inode = 0;

// we will have 256 inodes (even if all of them can't be used)
// starting after block 0
// sizeof(inode) = 40 (right now) so we need 2.5 -> 3 pages
static const int inode_start_page = 1;
static const int inode_end_page = 17; // page after inodes end

// number of available inodes after removing metadata
static const int num_inodes = num_pages-1;

#endif 
