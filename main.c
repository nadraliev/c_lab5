#define FUSE_USE_VERSION 30
#define _FILE_OFFSET_BITS 64

#include <fuse.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* foward structure declaration (fs_node_t and fs_dir_node_t cross-reference each other) */
struct fs_node_s;

typedef enum fs_node_type_e {
    FS_NODE_FILE,
    FS_NODE_DIRECTORY,
    FS_NODE_LINK
} fs_node_type_t;

/* file-related information */
typedef struct fs_file_node_s {
    int size;
    char *data_ptr;
} fs_file_node_t;

/* directory-related information */
typedef struct fs_dir_node_s {
    struct fs_node_s *child;
} fs_dir_node_t;

typedef struct fs_link_node_s {
    struct fs_node_s *target;
} fs_link_node_t;

/* structure representing a node in our FS tree */
typedef struct fs_node_s {
    char *name;
    struct fs_node_s *next_sibling;
    fs_node_type_t type;
    mode_t mode;
    int n_links;

    union {
        fs_file_node_t file;
        fs_dir_node_t dir;
        fs_link_node_t link;
    } info;

} fs_node_t;

static fs_node_t *create_file_with_perm(char *, mode_t);

static fs_node_t *create_directory_with_perm(char *, mode_t);

static fs_node_t *add_file_with_perm(fs_node_t *, char *, mode_t mode);

static fs_node_t *add_directory_with_perm(fs_node_t *, char *, mode_t);

static void add_child_node(fs_node_t *, fs_node_t *);

static fs_node_t *find_node(const char *, fs_node_t *);

static fs_node_t *create_link_with_perm(char *, fs_node_t *, mode_t);

static fs_node_t *add_link_with_perm(fs_node_t *, char *, fs_node_t *, mode_t);

fs_node_t *root;
char testText[15];
char *headBinary;

/* Find a node in our virtual FS tree corresponding to the specified path. */
static fs_node_t *find_node(const char *path, fs_node_t *parent) {
    int entry_len = 0;
    fs_node_t *current, *result = NULL;

    printf("find %s %p\n", path, parent);

    /* skip leading '/' symbols */
    while (*path != '\0' && *path == '/') {
        path++;
    }

    /* calculate the length of the current path entry */
    while (path[entry_len] != '\0' &&
           path[entry_len] != '/') {
        entry_len++;
    }

    if (parent == NULL || parent->type != FS_NODE_DIRECTORY) {
        /* 'parent' must represent a directory */
        result = parent;
    } else if (entry_len == 0) {
        /* if the path is empty (e.g. "/" or ""), return parent */
        result = parent;
    } else {
        /* traverse children in search for the next entry */
        current = parent->info.dir.child;
        while (current != NULL && strncmp(current->name, path, entry_len)) {
            current = current->next_sibling;
        }

        if (current != NULL) {
            result = find_node(path + entry_len, current);
        }
    }

    return result;
}

static int do_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    fs_node_t *parent_node, *current_node;

    printf("do_readdir: %s\n", path);

    filler(buffer, ".", NULL, 0);
    filler(buffer, "..", NULL, 0);

    parent_node = find_node(path, root);

    if (parent_node != NULL) {
        printf("dir found, n_links %d\n", parent_node->n_links);

        current_node = parent_node->info.dir.child;
        while (current_node != NULL) {
            filler(buffer, current_node->name, NULL, 0);
            current_node = current_node->next_sibling;
        }
    } else {
        printf("node not found!\n");
    }

    return 0;
}

static int do_getattr(const char *path, struct stat *st) {
    fs_node_t *node;
    int ret = 0;

    printf("do_getattr: %s\n", path);

    node = find_node(path, root);
    if (node != NULL) {
        st->st_mode = node->mode;
        if (node->type == FS_NODE_DIRECTORY) {
            st->st_mode |= S_IFDIR;
        } else if (node->type == FS_NODE_FILE) {
            st->st_mode |= S_IFREG;
            st->st_size = node->info.file.size;
        } else if (node->type == FS_NODE_LINK) {
            st->st_mode |= S_IFLNK;
            st->st_size = 1;
        }

        st->st_nlink = node->n_links;
        st->st_uid = getuid();
        st->st_gid = getgid();
    } else {
        printf("node not found!\n");
        ret = -ENOENT;
    }

    return ret;
}

static int do_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    fs_node_t *node;
    int bytes_read = 0;

    node = find_node(path, root);
    if (node != NULL && offset < node->info.file.size) {
        int bytes_available = node->info.file.size - offset;
        if (bytes_available >= size) {
            bytes_read = size;
        } else {
            bytes_read = bytes_available;
        }

        if (bytes_read > 0) {
            memcpy(buffer, node->info.file.data_ptr, bytes_read);
        }
    }

    return bytes_read;
}

static int do_symlink(const char *from, const char *to) {
    printf("link from %s to %s \n", from, to);
    fs_node_t *fromNode = find_node(from, root);

    if (fromNode == NULL) {
        return -1;
    }

    int nameLen = 0;
    for (int i = strlen(to) - 1; i >= 0; i--) {
        if (to[i] == '/') break;
        nameLen++;
    }

    fs_node_t *dirTo;

    if (nameLen != strlen(to)) {
        char dir[strlen(to) - nameLen];
        strncpy(dir, to, strlen(to) - nameLen - 1);
        dir[strlen(to) - nameLen - 1] = '\0';

        dirTo = find_node(dir, root);
    } else {
        dirTo = root;
    }

    char name[nameLen + 1];
    strncpy(name, to + strlen(to) - nameLen, nameLen);
    name[nameLen] = '\0';

    add_link_with_perm(dirTo, name, fromNode, 0666);

    return 0;
}

static struct fuse_operations operations = {
        .getattr    = do_getattr,
        .readdir    = do_readdir,
        .read         = do_read,
        .symlink = do_symlink
};

static fs_node_t *create_directory_with_perm(char *name, mode_t mode) {
    fs_node_t *node;

    node = (fs_node_t *) malloc(sizeof(fs_node_t));

    /* set default values */
    node->name = name;
    node->mode = mode;
    node->type = FS_NODE_DIRECTORY;
    node->next_sibling = NULL;
    node->info.dir.child = NULL;
    node->n_links = 2; /* . and .. */

    return node;
}

static fs_node_t *add_directory_with_perm(fs_node_t *root, char *name, mode_t mode) {
    fs_node_t *dir = create_directory_with_perm(name, mode);
    root->n_links++;
    add_child_node(root, dir);
    return dir;
}

static fs_node_t *create_file_with_perm(char *name, mode_t mode) {
    fs_node_t *node;

    node = (fs_node_t *) malloc(sizeof(fs_node_t));

    /* set default values */
    node->name = name;
    node->mode = mode;
    node->type = FS_NODE_FILE;
    node->next_sibling = NULL;
    node->info.file.size = 0;
    node->info.file.data_ptr = NULL;
    node->n_links = 0;

    return node;
}

static fs_node_t *add_file_with_perm(fs_node_t *root, char *name, mode_t mode) {
    fs_node_t *file = create_file_with_perm(name, mode);
    root->n_links++;
    add_child_node(root, file);
    return file;
}

static fs_node_t *create_link_with_perm(char *name, fs_node_t *target, mode_t mode) {
    fs_node_t *node;

    node = (fs_node_t *) malloc(sizeof(fs_node_t));

    /* set default values */
    node->name = name;
    node->mode = mode;
    node->type = FS_NODE_LINK;
    node->next_sibling = NULL;
    node->info.file.size = 1;
    node->info.file.data_ptr = NULL;
    node->n_links = 1;
    node->info.link.target = target;

    return node;
}

static fs_node_t *add_link_with_perm(fs_node_t *root, char *name, fs_node_t *target, mode_t mode) {
    fs_node_t *link = create_link_with_perm(name, target, mode);
    root->n_links++;
    add_child_node(root, link);
    return link;
}

static void add_child_node(fs_node_t *parent, fs_node_t *child) {
    /* assuming parent is always a directory */

    child->next_sibling = parent->info.dir.child;
    parent->info.dir.child = child;
}

static void generate_tree() {
    fs_node_t *bar, *bin, *less, *readme, *foo, *test, *baz, *example;

    //---------------directories
    bar = add_directory_with_perm(root, "bar", 0676);
    bin = add_directory_with_perm(root, "bin", 0766);
    baz = add_directory_with_perm(bin, "baz", 0777);
    foo = add_directory_with_perm(root, "foo", 0777);

    less = add_file_with_perm(bar, "less", 0676);
    test = add_file_with_perm(foo, "test.txt", 0777);
    readme = add_file_with_perm(baz, "readme.txt", 0444);
    example = add_file_with_perm(baz, "example", 0777);

    //-------------------files
    //-----------------------------------
    readme->info.file.data_ptr = "Student Надралиев Андрей, 16150007\n\0";
    readme->info.file.size = strlen(readme->info.file.data_ptr);

    //------------------------------
    for (int i = 0; i < 7; i++) {
        testText[i * 2] = 'a';
        testText[i * 2 + 1] = '\n';
    }
    testText[14] = '\0';
    test->info.file.data_ptr = testText;
    test->info.file.size = strlen(testText);

    //--------------------------
    example->info.file.data_ptr = "Hello world\n\0";
    example->info.file.size = strlen(example->info.file.data_ptr);

    //-------------------
    FILE *fileptr;

    fileptr = fopen("/bin/less", "rb");  // Open the file in binary mode
    fseek(fileptr, 0, SEEK_END);
    long fsize = ftell(fileptr);
    fseek(fileptr, 0, SEEK_SET);

    headBinary = (char *) malloc(fsize);

    fread(headBinary, fsize, 1, fileptr); // Read in the entire file
    fclose(fileptr); // Close the file

    less->info.file.data_ptr = headBinary;
    less->info.file.size = fsize;
}

int main(int argc, char *argv[]) {
    root = create_directory_with_perm("", 0666);
    generate_tree();

    return fuse_main(argc, argv, &operations, NULL);
}
