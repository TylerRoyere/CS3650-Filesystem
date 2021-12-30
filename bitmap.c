#include <stdio.h>
#include "bitmap.h"

// gets the value of the bitmap at bit index ii
int bitmap_get(void* bm, int ii)
{
    char* bitmap = (char*)bm;
    return (bitmap[ii>>3] & (0x80 >> (ii & 7) )) != 0;
}

// sets the value of the bitmap at bit index ii
void bitmap_put(void* bm, int ii, int vv)
{
    char* bitmap = (char*)bm;
    char byte = bitmap[ii>>3];
    if(vv) {
        bitmap[ii>>3] = bitmap[ii>>3] | (0x80 >> (ii & 7));
    }
    else {
        bitmap[ii>>3] = bitmap[ii>>3] & ~(0x80 >> (ii & 7));
    }
}

// prints the contents of the bitmap
void bitmap_print(void* bm, int size)
{
    if(!bm) {
        printf("bitmap NULL\n");
        return;
    }
    for(int ii = 0; ii < size; ii += 8) {
        for(int yy = 0; yy < 8; ++yy) {
            printf("%d", (bitmap_get(bm, ii + yy) != 0));
        }
        printf(" ");
    }
    printf("\n");
}
