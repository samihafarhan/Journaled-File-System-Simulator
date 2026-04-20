#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>


//  MEMBER 1 START: STRUCTS & HELPERS  


#define BLOCK_SIZE 4096
#define JOURNAL_BLOCK_START 1
#define JOURNAL_BLOCK_COUNT 16
#define JOURNAL_MAGIC 0x4A524E4C

// Superblock (128 bytes)
struct superblock {
    uint32_t magic;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t inode_count;
    uint32_t journal_block;
    uint32_t inode_bitmap;
    uint32_t data_bitmap;
    uint32_t inode_start;
    uint32_t data_start;
    uint8_t _pad[128 - 36];
};

struct journal_header {
    uint32_t magic;
    uint32_t nbytes_used;
};

#define REC_DATA 1
#define REC_COMMIT 2

struct rec_header {
    uint16_t type;
    uint16_t size;
};

struct data_record {
    struct rec_header hdr;
    uint32_t block_no;
    uint8_t data[4096];
};

struct commit_record {
    struct rec_header hdr;
};

struct inode {
    uint16_t type;         
    uint16_t links;        
    uint32_t size;         
    uint32_t direct[8];    
    uint32_t ctime;        
    uint32_t mtime;        
    uint8_t _pad[128-(2+2+4+8*4+4+4)];      
};


#define NAME_LEN 28
struct dirent {
    uint32_t inode;
    char name[NAME_LEN];
};

int img_fd; 

void read_block(int block_index, void* disc_mem) {
    int offset = block_index * BLOCK_SIZE;
    lseek(img_fd, offset, SEEK_SET);
    if(read(img_fd, disc_mem, BLOCK_SIZE) != BLOCK_SIZE) {
        printf("read error at block %d\n", block_index);
        exit(1);
    }
}

void write_block(int block_index, void* disc_mem) {
    int offset = block_index * BLOCK_SIZE;
    lseek(img_fd, offset, SEEK_SET);
    if(write(img_fd, disc_mem, BLOCK_SIZE) != BLOCK_SIZE) {
        printf("write error at block %d\n", block_index);
        exit(1);
    }
}

//MEMBER 1 END:STRUCTS & HELPERS    




//      MEMBER 2 START:CREATE COMMAND     


void cmd_create(char *fname) 
{
    uint8_t sb_buf[BLOCK_SIZE];
    read_block(0,sb_buf);
    struct superblock *sb=(struct superblock*)sb_buf;
    struct journal_header jh;
    lseek(img_fd, JOURNAL_BLOCK_START * BLOCK_SIZE, SEEK_SET);
    read(img_fd, &jh, sizeof(jh));

    if (jh.magic!=JOURNAL_MAGIC) {
        jh.magic=JOURNAL_MAGIC;
        jh.nbytes_used=sizeof(struct journal_header);}
    int j_offset=jh.nbytes_used;

    uint8_t map_buf[BLOCK_SIZE];
    read_block(sb->inode_bitmap, map_buf);

    int target_inode=-1;
    for (int i=0; i<sb->inode_count; i++) {
        int byte=i/8;
        int bit=i%8;
        
        if (!((map_buf[byte]>>bit) & 1)) {
            target_inode=i;
            map_buf[byte]|=(1 << bit); 
            break;
        }
    }

    if (target_inode==-1) {
        printf("no free inodes left\n");
        exit(1);
    }

    uint8_t dir_buf[BLOCK_SIZE];
    read_block(sb->data_start, dir_buf);
    
    struct dirent *dir_arr=(struct dirent*)dir_buf;
    int max_files=BLOCK_SIZE/sizeof(struct dirent);
    int dir_slot=-1;

    for (int k=0; k<max_files; k++) {
        if (dir_arr[k].name[0]=='\0') {
            dir_arr[k].inode=target_inode;
            strncpy(dir_arr[k].name, fname, 27);
            dir_arr[k].name[27]='\0';
            dir_slot=k;
            break;
        }
    }

    if (dir_slot==-1) {
        printf("root dir is full\n");
        exit(1);
    }
    uint8_t inode_buf[BLOCK_SIZE];
    read_block(sb->inode_start, inode_buf);
    struct inode *node_arr = (struct inode *)inode_buf;
    
    node_arr[target_inode].type=1; 
    node_arr[target_inode].size=0;
    node_arr[target_inode].links=1;
    memset(node_arr[target_inode].direct, 0, sizeof(node_arr[target_inode].direct));

    int size_needed=(dir_slot + 1)*sizeof(struct dirent);
    if (size_needed>node_arr[0].size) {
        node_arr[0].size=size_needed;
    }

    struct data_record d_rec;
    d_rec.hdr.type=REC_DATA;
    d_rec.hdr.size=sizeof(struct data_record);

    d_rec.block_no=sb->inode_bitmap;
    memcpy(d_rec.data, map_buf, BLOCK_SIZE);
    lseek(img_fd, JOURNAL_BLOCK_START * BLOCK_SIZE + j_offset, SEEK_SET);
    write(img_fd, &d_rec, sizeof(d_rec));
    j_offset+=sizeof(d_rec);

    d_rec.block_no=sb->inode_start;
    memcpy(d_rec.data, inode_buf, BLOCK_SIZE);
    write(img_fd, &d_rec, sizeof(d_rec));
    j_offset+=sizeof(d_rec);

    d_rec.block_no=sb->data_start;
    memcpy(d_rec.data, dir_buf, BLOCK_SIZE);
    write(img_fd, &d_rec, sizeof(d_rec));
    j_offset+=sizeof(d_rec);

    struct commit_record c_rec;
    c_rec.hdr.type=REC_COMMIT;
    c_rec.hdr.size=sizeof(struct commit_record);
    write(img_fd, &c_rec, sizeof(c_rec));
    j_offset+=sizeof(c_rec);

    jh.nbytes_used = j_offset;
    lseek(img_fd, JOURNAL_BLOCK_START * BLOCK_SIZE, SEEK_SET);
    if (write(img_fd, &jh, sizeof(jh)) != sizeof(jh)) {
    perror("journal header write");
    }
    printf("created %s at inode %d\n", fname, target_inode);
}

// MEMBER 2 END    




//  MEMBER 3 START:INSTALL   



void cmd_install() 
{
    struct journal_header jh; 
    int cursor;
    struct data_record *txn_cache[16]; 
    int txn_count = 0;


    
    lseek(img_fd, JOURNAL_BLOCK_START * BLOCK_SIZE, SEEK_SET);
    read(img_fd, &jh, sizeof(jh));


    if (jh.magic!=JOURNAL_MAGIC) {
        printf("wrong magic number\n");
        return;
    }
    

    if (jh.nbytes_used == sizeof(struct journal_header)) {
        printf("nothing to install\n");
        return;
    }


    printf("installing... %d bytes used\n", jh.nbytes_used);


    uint8_t *j_mem =malloc(JOURNAL_BLOCK_COUNT * BLOCK_SIZE); //takes space from ram of journal's size

    lseek(img_fd, JOURNAL_BLOCK_START * BLOCK_SIZE, SEEK_SET);
    read(img_fd, j_mem, JOURNAL_BLOCK_COUNT * BLOCK_SIZE); //jMem is starting add of journal's copy in ram


    cursor = sizeof(struct journal_header); //skipping the jrnl hdr


    while(cursor < jh.nbytes_used) {
        struct rec_header *rh = (struct rec_header *)(j_mem + cursor); //rec_hdr splits them in TYPE and SIZE
        
        if (rh->type == REC_DATA) {
            txn_cache[txn_count++] = (struct data_record *)rh; //note down that we need to update at this ADDRESSes

            cursor += rh->size; //cursor moves to the next update. but size stays the same as all updates are 4096B
        } 


        else if (rh->type == REC_COMMIT) {
            // apply changes
            for(int i=0; i<txn_count; i++) {
                 printf("  - Writing to Block %d\n", txn_cache[i]->block_no);
                write_block(txn_cache[i]->block_no, txn_cache[i]->data); //goes to the specifc block (i.e bitmap) and overwrites the data ,  data is the updated bitmap
            }
            txn_count = 0;
            cursor += rh->size;
        } 
        else break;
    }


    //reset journal
    
    jh.nbytes_used=sizeof(struct journal_header);
    lseek(img_fd, JOURNAL_BLOCK_START * BLOCK_SIZE, SEEK_SET);
    write(img_fd, &jh, sizeof(jh));
    
    free(j_mem);
    printf("install finished\n");

}



//  MEMBER 3 END 




// === MEMBER 1 START: MAIN      


void cmd_print() 
{
    uint8_t super_buffer[BLOCK_SIZE];
    read_block(0, super_buffer);
    struct superblock *sb = (struct superblock *)super_buffer;
    
    uint8_t journal_buffer[BLOCK_SIZE];
    read_block(JOURNAL_BLOCK_START, journal_buffer);
    struct journal_header *jh = (struct journal_header *)journal_buffer;
    
    printf("--- File System Status ---\n");
    printf("Magic: 0x%x\n", sb->magic);
    printf("Journal Used: %d\n", jh->nbytes_used);
}

int main(int argc, char *argv[]) 
{
    if(argc < 2) {
        printf("usage: ./journal <create|install|print> [args]\n");
        return 1;
    }

    img_fd = open("vsfs.img", O_RDWR);
    if(img_fd < 0) {
        printf("cannot open vsfs.img\n");
        return 1;
    }

    if(strcmp(argv[1], "create") == 0) {
        if(argc < 3){
            printf("usage: ./journal create <name>\n");
        }
        else{
            cmd_create(argv[2]);
        }
    } 
    else if (strcmp(argv[1], "install") == 0) {
        cmd_install();
    } 
    else if (strcmp(argv[1], "print") == 0) {
        cmd_print();
    } 
    else {
        printf("unknown cmd '%s'\n", argv[1]);
    }

    close(img_fd);
    return 0;
}
//  MEMBER 1 END       
