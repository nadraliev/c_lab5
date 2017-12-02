#define FUSE_USE_VERSION 30
#define _FILE_OFFSET_BITS 64

#include <fuse.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* foward structure declaration (fs_node_t and fs_dir_node_t cross-reference each other) */
struct fs_node_s;

typedef enum fs_node_type_e
{
    FS_NODE_FILE,
    FS_NODE_DIRECTORY
} fs_node_type_t;

/* file-related information */
typedef struct fs_file_node_s
{
    int size;
    char *data_ptr;
} fs_file_node_t;

/* directory-related information */
typedef struct fs_dir_node_s
{
    struct fs_node_s *child;
} fs_dir_node_t;

/* structure representing a node in our FS tree */
typedef struct fs_node_s
{
    char *name;
    struct fs_node_s *next_sibling;
    fs_node_type_t type;
    mode_t mode;
    int n_links;

    union {
        fs_file_node_t file;
        fs_dir_node_t dir;
    } info;

} fs_node_t;

fs_node_t *root;

/* Find a node in our virtual FS tree corresponding to the specified path. */
fs_node_t *find_node(const char *path, fs_node_t *parent)
{
    int entry_len = 0;
    fs_node_t *current, *result = NULL;

    printf("find %s %p\n", path, parent);

    /* skip leading '/' symbols */
    while (*path != '\0' && *path == '/')
    {
        path++;
    }

    /* calculate the length of the current path entry */
    while (path[entry_len] != '\0' &&
           path[entry_len] != '/')
    {
        entry_len++;
    }

    if (parent == NULL || parent->type != FS_NODE_DIRECTORY)
    {
        /* 'parent' must represent a directory */
        result = parent;
    }
    else if (entry_len == 0)
    {
        /* if the path is empty (e.g. "/" or ""), return parent */
        result = parent;
    }
    else
    {
        /* traverse children in search for the next entry */
        current = parent->info.dir.child;
        while (current != NULL && strncmp(current->name, path, entry_len))
        {
            current = current->next_sibling;
        }

        if (current != NULL)
        {
            result = find_node(path + entry_len, current);
        }
    }

    return result;
}

static int do_readdir( const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi )
{
    fs_node_t *parent_node, *current_node;

    printf("do_readdir: %s\n", path);

    filler( buffer, ".", NULL, 0 );
    filler( buffer, "..", NULL, 0 );

    parent_node = find_node(path, root);

    if (parent_node != NULL)
    {
        printf("dir found, n_links %d\n", parent_node->n_links);

        current_node = parent_node->info.dir.child;
        while (current_node != NULL)
        {
            filler(buffer, current_node->name, NULL, 0);
            current_node = current_node->next_sibling;
        }
    }
    else
    {
        printf("node not found!\n");
    }

    return 0;
}

static int do_getattr( const char *path, struct stat *st )
{
    fs_node_t *node;
    int ret = 0;

    printf("do_getattr: %s\n", path);

    node = find_node(path, root);
    if (node != NULL)
    {
        st->st_mode = node->mode;
        if (node->type == FS_NODE_DIRECTORY)
        {
            st->st_mode |= S_IFDIR;
        }
        else if(node->type == FS_NODE_FILE)
        {
            st->st_mode |= S_IFREG;
            st->st_size = node->info.file.size;
        }

        st->st_nlink = node->n_links;
        st->st_uid = getuid();
        st->st_gid = getgid();
    }
    else
    {
        printf("node not found!");
        ret = -ENOENT;
    }

    return ret;
}

static int do_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi)
{
    fs_node_t *node;
    int bytes_read = 0;

    node = find_node(path, root);
    if (node != NULL && offset < node->info.file.size)
    {
        int bytes_available = node->info.file.size - offset;
        if (bytes_available >= size)
        {
            bytes_read = size;
        }
        else
        {
            bytes_read = bytes_available;
        }

        if (bytes_read > 0)
        {
            memcpy(buffer, node->info.file.data_ptr, bytes_read);
        }
    }

    return bytes_read;
}

static struct fuse_operations operations = {
        .getattr	= do_getattr,
        .readdir	= do_readdir,
        .read         = do_read
};

static fs_node_t *create_directory(char *name)
{
    fs_node_t *node;

    node = (fs_node_t *)malloc(sizeof(fs_node_t));

    /* set default values */
    node->name = name;
    node->mode = 0666;
    node->type = FS_NODE_DIRECTORY;
    node->next_sibling = NULL;
    node->info.dir.child = NULL;
    node->n_links = 2; /* . and .. */

    return node;
}

static fs_node_t *create_file(char *name)
{
    fs_node_t *node;

    node = (fs_node_t *)malloc(sizeof(fs_node_t));

    /* set default values */
    node->name = name;
    node->mode = 0666;
    node->type = FS_NODE_FILE;
    node->next_sibling = NULL;
    node->info.file.size = 0;
    node->info.file.data_ptr = NULL;
    node->n_links = 0;

    return node;
}

static void add_child_node(fs_node_t *parent, fs_node_t *child)
{
    /* assuming parent is always a directory */

    child->next_sibling = parent->info.dir.child;
    parent->info.dir.child = child;
}

static void make_sample_hierarchy()
{
    fs_node_t *first, *second;
    fs_node_t *file;
    fs_node_t *first_inner;

    first = create_directory("first");
    root->n_links++; /* a new subfolder */
    add_child_node(root, first);

    second = create_directory("second");
    root->n_links++; /* a new subfolder */
    add_child_node(root, second);

    first_inner = create_directory("first_inner");
    first->n_links++; /* a new subfolder */
    add_child_node(first, first_inner);

    file = create_file("sample.txt");
    file->n_links++; /* a new hard link */
    add_child_node(root, file);

    file->info.file.data_ptr = "Hello";
    file->info.file.size = strlen(file->info.file.data_ptr);
}

int main( int argc, char *argv[] )
{
    root = create_directory("");
    make_sample_hierarchy();

    return fuse_main( argc, argv, &operations, NULL );
}
