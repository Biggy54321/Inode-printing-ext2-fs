/**
 * @file ext2.c
 * @author Bhaskar Pardeshi
 * @brief The program can be used to display the inode details of any file in
 *        an ext2 formatted file system. The contents of only file and directory
 *        can be displayed.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <ext2fs/ext2_fs.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>

/**
 * Program constraints
 */

#define DEVICE_FILE_PATH "/dev/sdb1"
#define MAX_PATH_TOKS    (256)

/**
 * Utility
 */

/* Typedef the integer types */
typedef uint64_t _u64;
typedef int64_t  _s64;
typedef uint32_t _u32;
typedef uint16_t _u16;
typedef uint8_t  _u8;

/* Error print and return macro */
#define exit_err(...)                           \
    {                                           \
        fprintf(stderr, __VA_ARGS__);           \
        exit(EXIT_FAILURE);                     \
    }

/* Request type - Inode */
#define REQUEST_TYPE_INODE    (0)
/* Request type - Data */
#define REQUEST_TYPE_DATA     (1)
/* Request type - Data */
#define REQUEST_TYPE_INVALID  (2)

/**
 * @brief Returns the request type given the request string
 * @param[in] String argument
 * @return Request type number
 */
static inline _u8 _get_req_type(_u8 *arg) {

    /* If the argument is inode */
    if (!strcmp(arg, "inode")) {
        return REQUEST_TYPE_INODE;
    }
    /* If the argument is data */
    else if (!strcmp(arg, "data")) {
        return REQUEST_TYPE_DATA;
    }
    /* If the argument is anything else */
    else {
        return REQUEST_TYPE_INVALID;
    }
}

/**
 * @brief Tokenize the argument on '/'
 * @param[in] arg Argument string
 * @param[out] toks Array of token strings
 * @return Return the number of tokens
 */
static inline _u32 _get_toks(_u8 *arg, _u8 *delim, _u8 *toks[MAX_PATH_TOKS]) {

    _u8 *tok;
    _u32 tok_i = 0;

    /* While we get the more tokens */
    while (tok = strtok_r(arg, delim, (char **)&arg)) {

        /* Allocate the token to the token array */
        toks[tok_i++] = strdup(tok);
    }

    /* Return the number of tokens */
    return tok_i;
}

/**
 * Ext2 parameters
 */

/* File system specific macros */
#define EXT2_SUPER_BLOCK_OFFSET    (1024u)
#define EXT2_SUPER_BLOCK_SIZE      (sizeof(struct ext2_super_block))
#define EXT2_DIR_ENTRY_SIZE        (sizeof(struct ext2_dir_entry_2))
#define EXT2_IS_INODE_DIR(ino_st)  ((((ino_st)->i_mode) & 0xF000) == (0x4000))
#define EXT2_IS_INODE_REG_FILE(ino_st)  ((((ino_st)->i_mode) & 0xF000) == (0x8000))
#define EXT2_PHY_TO_LOG_BLKS(sup, phy_nb)                           \
    ({                                                              \
        uint64_t log_to_phy_fact;                                   \
        uint64_t log_nb;                                            \
        log_to_phy_fact = EXT2_BLOCK_SIZE((sup)) / (512u);          \
        log_nb = ((phy_nb) / log_to_phy_fact);                      \
        log_nb += (((phy_nb) % log_to_phy_fact) != 0);              \
    })

/* File type to string map */
char *_ft_to_str[EXT2_FT_MAX] = {"Unknown  ", "Regular  ", "Directory",
                                 "Character", "Block    ", "Fifo     ",
                                 "Socket   ", "Softlink "};

/* File descriptor for the device file */
static _u32 _fd;
/* Super block for the device */
static struct ext2_super_block _sb;

/**
 * @brief Locates and reads the requested amount of data
 * @param[in] offset Offset number of bytes from the start of the device
 * @param[in] buff Starting address of the buffer
 * @param[in] size Number of bytes to be read from the #offset
 */
static inline void _ext2_read(_u64 offset, void *buff, _u64 size) {

    /* Locate the offset number of bytes */
    lseek64(_fd, offset, SEEK_SET);

    /* Read the bytes into the buffer */
    read(_fd, buff, size);
}

/**
 * @brief Sets the offset of the device file to the given value
 * @param[in] offset Offset (unsigned)
 * @note The offset is set with respect to the base offset
 */
static inline void _ext2_seek_set(_u64 offset) {

    /* Shift the head to the offset */
    lseek64(_fd, offset, SEEK_SET);
}

/**
 * @brief Sets the offset of the device file to the given value
 * @param[in] offset Offset (signed)
 * @note The offset is set with respect to the current offset
 */
static inline void _ext2_seek_cur(_s64 offset) {

    /* Shift the head to the offset */
    lseek64(_fd, offset, SEEK_CUR);
}

/**
 * @brief Initialize globals
 */
void ext2_init() {

    /* Open the device file */
    _fd = open(DEVICE_FILE_PATH, O_RDONLY);

    /* Check for failure */
    if (_fd == -1) {
        /* Exit with failure */
        exit_err("Failed to open the device file\n");
    }

    /* Read the superblock */
    _ext2_read(EXT2_SUPER_BLOCK_OFFSET, &_sb, EXT2_SUPER_BLOCK_SIZE);
}

/**
 * @brief Denitialize globals
 */
void ext2_deinit() {

    /* Close the device file */
    close(_fd);
}

/**
 * @brief Obtains the inode structure given the inode number
 * @param[in] ino Inode number
 * @param[out] p_ino_st Pointer to the inode structure
 */
static void _ext2_ino_to_ino_st(_u64 ino, struct ext2_inode *p_ino_st) {

    _u64 grp_nb;
    _u64 grp_desc_off;
    struct ext2_group_desc grp_desc;
    _u64 ino_tab_off;
    _u64 ino_idx;
    _u64 ino_off;

    /* Get the group number of the inode */
    grp_nb = (ino - 1) / EXT2_INODES_PER_GROUP(&_sb);
    /* Locate the group descriptor for the same */
    grp_desc_off = EXT2_BLOCK_SIZE(&_sb) + grp_nb * EXT2_DESC_SIZE(&_sb);
    /* Read the group descriptor */
    _ext2_read(grp_desc_off, &grp_desc, EXT2_DESC_SIZE(&_sb));

    /* Get the inode table offset */
    ino_tab_off = (_u64)grp_desc.bg_inode_table * EXT2_BLOCK_SIZE(&_sb);
    /* Get the inode index in the table */
    ino_idx = (ino - 1) % EXT2_INODES_PER_GROUP(&_sb);
    /* Get the inode offset */
    ino_off = ino_tab_off + ino_idx * EXT2_INODE_SIZE(&_sb);
    /* Read the inode */
    _ext2_read(ino_off, p_ino_st, sizeof(struct ext2_inode));
}

/**
 * @brief Searches the given directory data block directly
 *        for the argument string
 * @param[in] blk_addr Directory data block number
 * @param[in] nxt_arg Argument string to be searched
 * @return Inode number of the argument string
 */
static _u64 _ext2_dir_search(_u32 blk_addr, _u8 *nxt_arg) {

    struct ext2_dir_entry_2 dir_ent;
    _u64 blk_off;
    _u32 i = 0;

    /* Get the directory data block offset */
    blk_off = (_u64)blk_addr * EXT2_BLOCK_SIZE(&_sb);

    /* While the entire block is traversed */
    while (i < EXT2_BLOCK_SIZE(&_sb)) {
        /* Read the directory entry */
        _ext2_read(blk_off + i, &dir_ent, EXT2_DIR_ENTRY_SIZE);

        /* Compare the next argument string */
        if (!memcmp(nxt_arg, dir_ent.name, dir_ent.name_len)) {
            /* Return the inode number */
            return dir_ent.inode;
        }

        /* Update the total bytes read */
        i += dir_ent.rec_len;
    }

    /* Return bad inode number */
    return EXT2_BAD_INO;
}

/**
 * @brief Searches the given directory data block indirectly
 *        for the argument string
 * @param[in] blk_addr Directory data block number
 * @param[in] nxt_arg Argument string to be searched
 * @param[in] indir_level Indirection level of the current block
 * @return Inode number of the argument string
 */
static _u64 _ext2_indir_search(_u32 blk_addr, _u8 *nxt_arg, _u8 indir_level) {

    _u64 blk_off;
    _u32 nxt_blk_addr;
    _u64 nxt_ino;
    _u32 i = 0;

    /* Get the block offset */
    blk_off = (_u64)blk_addr * EXT2_BLOCK_SIZE(&_sb);

    /* For every 4 bytes of the block */
    while (i < EXT2_ADDR_PER_BLOCK(&_sb)) {

        /* Read the 4 byte block number */
        _ext2_read(blk_off + 4 * i, &nxt_blk_addr, 4);

        /* If the block number is zero */
        if (!nxt_blk_addr) {
            /* Return not found */
            return EXT2_BAD_INO;
        }

        /* If the current block is single indirect one */
        if (indir_level == 1) {
            /* Get the next inode number */
            nxt_ino = _ext2_dir_search(nxt_blk_addr, nxt_arg);
        }
        /* If the current block is single indirect one */
        else {
            /* Get the next inode number */
            nxt_ino = _ext2_indir_search(nxt_blk_addr, nxt_arg, indir_level - 1);
        }

        /* If inode number is valid */
        if (nxt_ino > EXT2_BAD_INO) {
            /* Then return the inode number */
            return nxt_ino;
        }

        /* Update the pointer */
        i++;
    }

    /* Return bad inode number */
    return EXT2_BAD_INO;
}

/**
 * @brief Returns the inode number of the file which is child of the file
 *        pointed by given inode number
 * @param[in] ino Parent inode number
 * @param[in] nxt_arg Path name argument of the child file
 * @return Inode number of the child file
 */
static _u64 _ext2_nxt_ino(_u64 ino, _u8 *nxt_arg) {

    struct ext2_inode ino_st;
    _u64 nxt_ino;
    _u32 blk_addr;
    _u32 i = 0;

    /* Get the inode from the inode number */
    _ext2_ino_to_ino_st(ino, &ino_st);

    /* Check if the inode is of type directory */
    if (!EXT2_IS_INODE_DIR(&ino_st)) {
        /* Exit with failure */
        exit_err("The path consists of non-directory files\n");
    }

    /* While the block addresses are non zero */
    while ((i < EXT2_N_BLOCKS) && (blk_addr = ino_st.i_block[i])) {
        /* If the current block is a direct block */
        if (i < EXT2_NDIR_BLOCKS) {

            nxt_ino = _ext2_dir_search(blk_addr, nxt_arg);
        }
        /* If the current block is single indirect block */
        else if (i == EXT2_IND_BLOCK) {

            nxt_ino = _ext2_indir_search(blk_addr, nxt_arg, 1);
        }
        /* If the current block is double indirect block */
        else if (i == EXT2_DIND_BLOCK) {

            nxt_ino = _ext2_indir_search(blk_addr, nxt_arg, 2);
        }
        /* If the current block is triple indirect block */
        else if (i == EXT2_TIND_BLOCK) {

            nxt_ino = _ext2_indir_search(blk_addr, nxt_arg, 3);
        }

        /* If the inode number is valid */
        if (nxt_ino > EXT2_BAD_INO) {
            /* Return the inode number */
            return nxt_ino;
        }

        /* Update the block number */
        i++;
    }

    /* Return bad inode number */
    return EXT2_BAD_INO;
}

/**
 * @brief Returns the inode number of a file given its absolute path
 * @param[in] path Absolute path of the file
 * @return Inode number
 */
_u64 ext2_path_to_ino(_u8 *path) {

    _u8 *toks[MAX_PATH_TOKS];
    _u32 nb_toks;
    _u32 i;
    _u64 ino;

    /* Tokenize the path to get seperate file names */
    nb_toks = _get_toks(path, "/", toks);

    /* Initialize the inode number to that of root */
    ino = EXT2_ROOT_INO;

    /* For each token */
    for (i = 0; i < nb_toks; i++) {
        /* Get the next inode number */
        ino = _ext2_nxt_ino(ino, toks[i]);

        /* Check if inode number is valid */
        if (ino < EXT2_ROOT_INO) {
            /* Exit with failure */
            exit_err("File search failed\n");
        }
    }

    /* Free the tokens */
    for (i = 0; i < nb_toks; i++) {
        free(toks[i]);
    }

    /* Return the inode number of the file */
    return ino;
}

/**
 * @brief Prints the inode structure contents given
 *        the inode number
 * @param[in] ino Inode number
 */
void _ext2_print_ino_st(_u64 ino) {

    _u32 i = 0;
    struct ext2_inode ino_st;

    /* Get the inode structure from the inode number */
    _ext2_ino_to_ino_st(ino, &ino_st);

    /* Print the important inode structure fields */
    printf("Inode: %u ", ino);
    printf("Type: 0x%x ", ino_st.i_mode & 0xF000);
    printf("Mode: 0%o ", ino_st.i_mode & 0x0FFF);
    printf("Flags: 0x%x\n", ino_st.i_flags);
    printf("Generation: %u\n", ino_st.i_generation);
    printf("User: %u ", ino_st.i_uid);
    printf("Group: %u ", ino_st.i_gid);
    printf("Size: %u\n", ino_st.i_size);
    printf("File ACL: %u\n", ino_st.i_file_acl);
    printf("Links: %u ", ino_st.i_links_count);
    printf("Blockcount: %u\n", ino_st.i_blocks);
    printf("ctime: 0x%x\n", ino_st.i_ctime);
    printf("atime: 0x%x\n", ino_st.i_atime);
    printf("mtime: 0x%x\n", ino_st.i_mtime);

    /* Print the block addresses */
    printf("BLOCKS:\n");

    while ((i < EXT2_N_BLOCKS) && (ino_st.i_block[i])) {

        if (i < EXT2_NDIR_BLOCKS) {
            printf("Direct data block (%u): %u\n", i, ino_st.i_block[i]);
        }
        else if (i == EXT2_IND_BLOCK) {
            printf("Single indirect data block: %u\n", ino_st.i_block[i]);
        }
        else if (i == EXT2_DIND_BLOCK) {
            printf("Double indirect data block: %u\n", ino_st.i_block[i]);
        }
        else if (i == EXT2_TIND_BLOCK) {
            printf("Triple indirect data block: %u\n", ino_st.i_block[i]);
        }

        /* Update the pointer */
        i++;
    }
}

/**
 * @brief Prints the contents of the direct regular file data block
 * @param[in] block_addr Block address
 */
void _ext2_dir_print_reg_file(_u32 blk_addr) {

    _u64 blk_off;
    _u32 i = 0;
    _u8 data;

    /* Get the block offset */
    blk_off = (_u64)blk_addr * EXT2_BLOCK_SIZE(&_sb);

    /* Print the block */
    while (i < EXT2_BLOCK_SIZE(&_sb)) {
        /* Read the next byte */
        _ext2_read(blk_off + i, &data, 1);
        /* Print the byte */
        printf("%c", data);
        /* Update the pointer */
        i++;
    }
}

/**
 * @brief Prints the contents of the direct directory data block
 * @param[in] block_addr Block address
 */
void _ext2_dir_print_dir(_u32 blk_addr) {

    _u64 blk_off;
    _u32 i = 0;
    struct ext2_dir_entry_2 dir_ent;

    /* Get the block offset */
    blk_off = (_u64)blk_addr * EXT2_BLOCK_SIZE(&_sb);

    /* Print the directory mappings in the block */
    while (i < EXT2_BLOCK_SIZE(&_sb)) {
        /* Read the directory entry */
        _ext2_read(blk_off + i, &dir_ent, EXT2_DIR_ENTRY_SIZE);
        /* Print the directory entry contents */
        printf("%d\t", dir_ent.inode);
        printf("%s\t", _ft_to_str[dir_ent.file_type]);
        printf("%.*s\n", dir_ent.name_len, dir_ent.name);
        /* Update the pointer */
        i += dir_ent.rec_len;
    }
}

/**
 * @brief Prints the contents of the direct data block
 * @param[in] block_addr Block address
 * @param[in] file_type File type
 */
void _ext2_dir_print(
        _u32 blk_addr,
        _u8 file_type) {

    /* If the block belongs to a regular file */
    if (file_type == EXT2_FT_REG_FILE) {
        /* Print the regular file block */
        _ext2_dir_print_reg_file(blk_addr);
    }
    /* If the block belongs to a directory  */
    else if (file_type == EXT2_FT_DIR) {
        /* Print the directory */
        _ext2_dir_print_dir(blk_addr);
    }
}

/**
 * @brief Prints the contents of the indirect data block
 * @param[in] blk_addr Block address
 * @param[in] file_type File type
 * @param[in] indir_level Indirection level
 */
void _ext2_indir_print(
        _u32 blk_addr,
        _u8 file_type,
        _u8 indir_level) {

    _u64 blk_off;
    _u32 nxt_blk_addr;
    _u32 i = 0;

    /* Get the block offset */
    blk_off = (_u64)blk_addr * EXT2_BLOCK_SIZE(&_sb);

   /* For all the addresses in the block */
    while (i < EXT2_ADDR_PER_BLOCK(&_sb)) {

        /* Read the next block address */
        _ext2_read(blk_off + 4 * i, &nxt_blk_addr, 4);

        /* If the address is zero */
        if (!nxt_blk_addr) {
            return;
        }

        /* If the current block is single indirect one */
        if (indir_level == 1) {
            /* Print the next direct block */
            _ext2_dir_print(nxt_blk_addr, file_type);
        }
        /* If the current block is single indirect one */
        else {
            /* Print the next indirect block */
            _ext2_indir_print(nxt_blk_addr, file_type, indir_level - 1);
        }

        /* Update the pointer */
        i++;
    }
}

/**
 * @brief Prints the data blocks of the inode
 * @param[in] ino Inode number
 */
void _ext2_print_ino_data(_u64 ino) {

    struct ext2_inode ino_st;
    _u8 file_type;
    _u32 blk_addr;
    _u32 i = 0;

    /* Get the inode structure */
    _ext2_ino_to_ino_st(ino, &ino_st);

    /* If the inode is a regular file */
    if (EXT2_IS_INODE_REG_FILE(&ino_st)) {
        /* Set file type to regular file */
        file_type = EXT2_FT_REG_FILE;
    }
    /* If the inode is a directory file */
    else if (EXT2_IS_INODE_DIR(&ino_st)) {
        /* Set file type to directory */
        file_type = EXT2_FT_DIR;
    }
    /* If we encountered any other file type */
    else {
        /* Exit with error */
        exit_err("File type not supported\n");
    }

    /* While we get valid addresses */
    while ((i < EXT2_N_BLOCKS) && (blk_addr = ino_st.i_block[i])) {
        /* If the current block is a direct block */
        if (i < EXT2_NDIR_BLOCKS) {
            /* Print the direct data block */
            _ext2_dir_print(blk_addr, file_type);
        }
        /* If the current block is single indirect block */
        else if (i == EXT2_IND_BLOCK) {

            _ext2_indir_print(blk_addr, file_type, 1);
        }
        /* If the current block is double indirect block */
        else if (i == EXT2_DIND_BLOCK) {

            _ext2_indir_print(blk_addr, file_type, 2);
        }
        /* If the current block is triple indirect block */
        else if (i == EXT2_TIND_BLOCK) {

            _ext2_indir_print(blk_addr, file_type, 3);
        }

        /* Update the block number */
        i++;
    }
}

/**
 * @brief Prints the inode contents depending on the
 *        request made
 * @param[in] ino Inode number
 * @param[in] req Request type
 */
void ext2_print_ino(_u64 ino, _u8 req) {

    /* If the request is to print inode struct */
    if (req == REQUEST_TYPE_INODE) {
        /* Print the inode structure */
        _ext2_print_ino_st(ino);
    }
    /* If the request is to print inode data */
    else if (req == REQUEST_TYPE_DATA) {
        /* Print the inode data */
        _ext2_print_ino_data(ino);
    }
    /* If invalid request is passed  */
    else {
        /* Exit with failure */
        exit_err("Invalid request\n");
    }
}

/**
 * @brief Main routine
 */
int main(int argc, char *argv[]) {

    _u64 ino;
    _u8 req;

    /* Validate the number of command line arguments */
    if (argc != 3) {
        /* Exit with failure */
        exit_err("Invalid number of arguments\n");
    }

    /* Init the global vars */
    ext2_init();

    /* Get the inode number of the file */
    ino = ext2_path_to_ino(argv[1]);

    /* Get the requested action */
    req = _get_req_type(argv[2]);

    /* Perform the action */
    ext2_print_ino(ino, req);

    /* Deinitialize the global vars */
    ext2_deinit();

    /* Exit with success */
    exit(EXIT_SUCCESS);
}
