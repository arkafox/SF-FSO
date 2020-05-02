#ifndef DISK_H
#define DISK_H

#define DISK_BLOCK_SIZE 4096

int  disk_init( const char *filename, int nblocks );
int  disk_size();
void disk_read( int blocknum, char *data );
void disk_write( int blocknum, const char *data );
void disk_read_data( int blocknum, char *data ); // cache aware read
void disk_write_data( int blocknum, const char *data );	// cache aware write
void disk_flush();
void disk_close();
void cache_debug();

#endif
