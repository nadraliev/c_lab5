#define FUSE_USE_VERSION 30
#define _FILE_OFFSET_BITS 64

#include <fuse.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

struct fuse_node_s;

typedef enum fuse_node_type_e {
    FUSE_NODE_FILE,
    FUSE_NODE_DIRECTORY,
    FUSE_NODE_LINK
} fuse_node_type_t;

typedef struct fuse_file_node_s {
    int size;
    char *data;
} fuse_file_node_t;

typedef struct fuse_dir_node_s {
    struct fuse_node_s *child;
} fuse_dir_node_t;

typedef struct fuse_link_node_s {
    struct fuse_node_s *target;
} fuse_link_node_t;

typedef struct fuse_node_s {
    char *name;
    struct fuse_node_s *next_sibling;
    fuse_node_type_t type;
    mode_t mode;
    int n_links;

    union {
        fuse_file_node_t file;
        fuse_dir_node_t dir;
        fuse_link_node_t link;
    } info;

} fuse_node_t;

static fuse_node_t *create_file_with_perm(char *, mode_t);

static fuse_node_t *create_directory_with_perm(char *, mode_t);

static fuse_node_t *add_file_with_perm(fuse_node_t *, char *, mode_t mode);

static fuse_node_t *add_directory_with_perm(fuse_node_t *, char *, mode_t);

static void add_child_node(fuse_node_t *, fuse_node_t *);

static fuse_node_t *find_node(const char *, fuse_node_t *);

static fuse_node_t *create_link_with_perm(char *, fuse_node_t *, mode_t);

static fuse_node_t *add_link_with_perm(fuse_node_t *, char *, fuse_node_t *, mode_t);

fuse_node_t *root;
char testText[15];
char *lessBinary;

static fuse_node_t *find_node(const char *path, fuse_node_t *parent) {
    int entry_len = 0;
    fuse_node_t *current, *result = NULL;

    printf("find %s %p\n", path, parent);

    while (*path != '\0' && *path == '/') {
        path++;
    }

    while (path[entry_len] != '\0' &&
           path[entry_len] != '/') {
        entry_len++;
    }

    //search for node using recurse
    if (parent == NULL || parent->type != FUSE_NODE_DIRECTORY) {
        result = parent;
    } else if (entry_len == 0) {
        result = parent;
    } else {
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

static int
fuse_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    fuse_node_t *parent_node, *current_node;

    filler(buffer, ".", NULL, 0);
    filler(buffer, "..", NULL, 0);

    parent_node = find_node(path, root);

    if (parent_node != NULL) {
        printf("directory found, links %d\n", parent_node->n_links);

        current_node = parent_node->info.dir.child;
        while (current_node != NULL) {
            filler(buffer, current_node->name, NULL, 0);
            current_node = current_node->next_sibling;
        }
    } else {
        printf("directory not found!\n");
    }

    return 0;
}

static int fuse_getattr(const char *path, struct stat *st) {
    fuse_node_t *node;
    int ret = 0;

    node = find_node(path, root);
    if (node != NULL) {
        st->st_mode = node->mode;
        if (node->type == FUSE_NODE_DIRECTORY) {
            st->st_mode |= S_IFDIR;
        } else if (node->type == FUSE_NODE_FILE) {
            st->st_mode |= S_IFREG;
            st->st_size = node->info.file.size;
        } else if (node->type == FUSE_NODE_LINK) {
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

static int fuse_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    fuse_node_t *node;
    int bytes_read = 0;

    node = find_node(path, root);
    if (node != NULL && offset < node->info.file.size) {
        printf("read %s\n", node->name);
        if (!strcmp(node->name, "less")) {
            FILE *fileptr;

            fileptr = fopen("/bin/less", "rb");  // Open the file in binary mode
            fseek(fileptr, 0, SEEK_END);
            long fsize = ftell(fileptr);
            fseek(fileptr, 0, SEEK_SET);

            lessBinary = (char *) malloc(fsize);

            fread(lessBinary, fsize, 1, fileptr);
            fclose(fileptr);

            if (fsize - offset > size) {
                bytes_read = size;
                memcpy(buffer, lessBinary + offset, size);
            } else {
                bytes_read = fsize - offset;
                memcpy(buffer, lessBinary + offset, fsize - offset);
            }

        } else {
            int bytes_available = node->info.file.size - offset;
            if (bytes_available >= size) {
                bytes_read = size;
            } else {
                bytes_read = bytes_available;
            }

            if (bytes_read > 0) {
                memcpy(buffer, node->info.file.data, bytes_read);
            }
        }
    }

    return bytes_read;
}

static int fuse_symlink(const char *from, const char *to) {
    printf("link from %s to %s \n", from, to);
    fuse_node_t *fromNode = find_node(from, root);

    if (fromNode == NULL) {
        return -1;
    }

    int nameLen = 0;
    for (int i = strlen(to) - 1; i >= 0; i--) {
        if (to[i] == '/') break;
        nameLen++;
    }

    fuse_node_t *dirTo;

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
        .getattr    = fuse_getattr,
        .readdir    = fuse_readdir,
        .read         = fuse_read,
        .symlink = fuse_symlink
};

static fuse_node_t *create_directory_with_perm(char *name, mode_t mode) {
    fuse_node_t *node;

    node = (fuse_node_t *) malloc(sizeof(fuse_node_t));

    node->name = name;
    node->mode = mode;
    node->type = FUSE_NODE_DIRECTORY;
    node->next_sibling = NULL;
    node->info.dir.child = NULL;
    node->n_links = 2;

    return node;
}

static fuse_node_t *add_directory_with_perm(fuse_node_t *root, char *name, mode_t mode) {
    fuse_node_t *dir = create_directory_with_perm(name, mode);
    root->n_links++;
    add_child_node(root, dir);
    return dir;
}

static fuse_node_t *create_file_with_perm(char *name, mode_t mode) {
    fuse_node_t *node;

    node = (fuse_node_t *) malloc(sizeof(fuse_node_t));

    node->name = name;
    node->mode = mode;
    node->type = FUSE_NODE_FILE;
    node->next_sibling = NULL;
    node->info.file.size = 0;
    node->info.file.data = NULL;
    node->n_links = 0;

    return node;
}

static fuse_node_t *add_file_with_perm(fuse_node_t *root, char *name, mode_t mode) {
    fuse_node_t *file = create_file_with_perm(name, mode);
    root->n_links++;
    add_child_node(root, file);
    return file;
}

static fuse_node_t *create_link_with_perm(char *name, fuse_node_t *target, mode_t mode) {
    fuse_node_t *node;

    node = (fuse_node_t *) malloc(sizeof(fuse_node_t));

    node->name = name;
    node->mode = mode;
    node->type = FUSE_NODE_LINK;
    node->next_sibling = NULL;
    node->info.file.size = 1;
    node->info.file.data = NULL;
    node->n_links = 1;
    node->info.link.target = target;

    return node;
}

static fuse_node_t *add_link_with_perm(fuse_node_t *root, char *name, fuse_node_t *target, mode_t mode) {
    fuse_node_t *link = create_link_with_perm(name, target, mode);
    root->n_links++;
    add_child_node(root, link);
    return link;
}

static void add_child_node(fuse_node_t *parent, fuse_node_t *child) {
    child->next_sibling = parent->info.dir.child;
    parent->info.dir.child = child;
}

static void generate_tree() {
    fuse_node_t *bar, *bin, *less, *readme, *foo, *test, *baz, *example;

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
    readme->info.file.data = "Student Надралиев Андрей, 16150007\n\0";
    readme->info.file.size = strlen(readme->info.file.data);

    //------------------------------
    for (int i = 0; i < 7; i++) {
        testText[i * 2] = 'a';
        testText[i * 2 + 1] = '\n';
    }
    testText[14] = '\0';
    test->info.file.data = testText;
    test->info.file.size = strlen(testText);

    //--------------------------
    example->info.file.data = "Hello world\n\0";
    example->info.file.size = strlen(example->info.file.data);

    //-------------------
    FILE *fileptr;

    fileptr = fopen("/bin/less", "rb");  // Open the file in binary mode
    fseek(fileptr, 0, SEEK_END);
    long fsize = ftell(fileptr);
    fseek(fileptr, 0, SEEK_SET);

    lessBinary = (char *) malloc(fsize);

    fread(lessBinary, fsize, 1, fileptr);
    fclose(fileptr);

    less->info.file.size = fsize;
}

int main(int argc, char *argv[]) {
    root = create_directory_with_perm("", 0666);
    generate_tree();

    return fuse_main(argc, argv, &operations, NULL);
}
