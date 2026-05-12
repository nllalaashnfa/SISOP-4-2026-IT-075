#define FUSE_USE_VERSION 28
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>

static char source_dir[1000];

// Fungsi untuk memetakan path FUSE ke path asli di source directory
void resolve_path(char *fpath, const char *path) {
    if (strcmp(path, "/") == 0) {
        sprintf(fpath, "%s", source_dir);
    } else {
        sprintf(fpath, "%s%s", source_dir, path);
    }
}

// Fungsi untuk membaca fragmen KOORD dari 1.txt sampai 7.txt dan menggabungkannya
char *build_tujuan(size_t *out_len) {
    char fragments[2000] = {0};
    int first = 1;

    for (int i = 1; i <= 7; i++) {
        char filepath[1000];
        snprintf(filepath, sizeof(filepath), "%s/%d.txt", source_dir, i);

        FILE *f = fopen(filepath, "r");
        if (!f) continue;

        char line[500];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "KOORD:", 6) == 0) {
                char *val = line + 6;
                while (*val == ' ') val++;
                int len = strlen(val);
                while (len > 0 && (val[len-1] == '\n' || val[len-1] == '\r' || val[len-1] == ' '))
                    val[--len] = '\0';

                if (!first)
                    strncat(fragments, " ", sizeof(fragments) - strlen(fragments) - 1);
                strncat(fragments, val, sizeof(fragments) - strlen(fragments) - 1);
                first = 0;
            }
        }
        fclose(f);
    }

    char *content = NULL;
    int written = asprintf(&content, "Tujuan Mas Amba: %s\n", fragments);
    if (written < 0) return NULL;

    *out_len = (size_t)written;
    return content;
}

static int xmp_getattr(const char *path, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));

    // File virtual tujuan.txt tidak ada di source, dibuat on-the-fly
    if (strcmp(path, "/tujuan.txt") == 0) {
        stbuf->st_mode  = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        size_t len = 0;
        char *content = build_tujuan(&len);
        stbuf->st_size = (off_t)len;
        free(content);
        return 0;
    }

    char fpath[1000];
    resolve_path(fpath, path);

    int res = lstat(fpath, stbuf);
    if (res == -1) return -errno;
    return 0;
}

static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    char fpath[1000];
    resolve_path(fpath, path);

    DIR *dp = opendir(fpath);
    if (dp == NULL) return -errno;

    struct dirent *de;
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    while ((de = readdir(dp)) != NULL) {
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino  = de->d_ino;
        st.st_mode = de->d_type << 12;
        filler(buf, de->d_name, &st, 0);
    }
    closedir(dp);

    // Tambahkan file virtual tujuan.txt hanya di root mount
    if (strcmp(path, "/") == 0) {
        filler(buf, "tujuan.txt", NULL, 0);
    }

    return 0;
}

static int xmp_open(const char *path, struct fuse_file_info *fi) {
    // File virtual tujuan.txt boleh dibuka read-only
    if (strcmp(path, "/tujuan.txt") == 0) {
        if ((fi->flags & O_ACCMODE) != O_RDONLY)
            return -EACCES;
        return 0;
    }

    char fpath[1000];
    resolve_path(fpath, path);

    int fd = open(fpath, fi->flags);
    if (fd == -1) return -errno;
    close(fd);
    return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    // File virtual tujuan.txt: isinya dibangkitkan on-the-fly dari KOORD di setiap file
    if (strcmp(path, "/tujuan.txt") == 0) {
        size_t len = 0;
        char *content = build_tujuan(&len);
        if (!content) return -ENOMEM;

        if ((size_t)offset >= len) {
            free(content);
            return 0;
        }

        size_t to_copy = len - (size_t)offset;
        if (to_copy > size) to_copy = size;
        memcpy(buf, content + offset, to_copy);
        free(content);
        return (int)to_copy;
    }

    char fpath[1000];
    resolve_path(fpath, path);

    int fd = open(fpath, O_RDONLY);
    if (fd == -1) return -errno;

    int res = pread(fd, buf, size, offset);
    if (res == -1) res = -errno;
    close(fd);
    return res;
}

static struct fuse_operations xmp_oper = {
    .getattr = xmp_getattr,
    .readdir = xmp_readdir,
    .open    = xmp_open,
    .read    = xmp_read,
};

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <source_directory> <mount_directory>\n", argv[0]);
        return 1;
    }

    // Simpan path source directory
    if (realpath(argv[1], source_dir) == NULL) {
        perror("realpath");
        return 1;
    }

    // Susun ulang argv untuk fuse_main (hapus argv[1] yaitu source_dir)
    char *fuse_argv[argc];
    fuse_argv[0] = argv[0];
    fuse_argv[1] = argv[2];
    for (int i = 3; i < argc; i++)
        fuse_argv[i - 1] = argv[i];
    int fuse_argc = argc - 1;

    umask(0);
    return fuse_main(fuse_argc, fuse_argv, &xmp_oper, NULL);
}
