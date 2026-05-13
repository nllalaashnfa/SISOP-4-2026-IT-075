#define FUSE_USE_VERSION 28

#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *base_dir = "/home/nllalaashnfa/SISOP-4-2026-IT-075/soal_2/encrypted_storage";
#define XOR_KEY 0x76

static void get_enc_path(char *enc_path, const char *path) {
    snprintf(enc_path, 1000, "%s%s.enc", base_dir, path);
}

static void get_dir_path(char *dir_path, const char *path) {
    snprintf(dir_path, 1000, "%s%s", base_dir, path);
}

static void xor_buffer(char *buf, size_t size) {
    for (size_t i = 0; i < size; i++) {
        buf[i] ^= XOR_KEY;
    }
}

static int xmp_getattr(const char *path, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));

    char dir_path[1000];
    get_dir_path(dir_path, path);
    if (stat(dir_path, stbuf) == 0 && S_ISDIR(stbuf->st_mode)) {
        return 0;
    }

    char enc_path[1000];
    get_enc_path(enc_path, path);
    if (stat(enc_path, stbuf) == 0) {
        return 0;
    }

    return -ENOENT;
}

static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi) {
    (void) offset; (void) fi;

    char dir_path[1000];
    get_dir_path(dir_path, path);

    DIR *dp = opendir(dir_path);
    if (!dp) return -ENOENT;

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;

        size_t len = strlen(de->d_name);
        if (len > 4 && strcmp(de->d_name + len - 4, ".enc") == 0) {
            char visible_name[256];
            strncpy(visible_name, de->d_name, len - 4);
            visible_name[len - 4] = '\0';
            filler(buf, visible_name, NULL, 0);
        } else {
            filler(buf, de->d_name, NULL, 0);
        }
    }

    closedir(dp);
    return 0;
}

static int xmp_mkdir(const char *path, mode_t mode) {
    char dir_path[1000];
    get_dir_path(dir_path, path);
    if (mkdir(dir_path, mode) == -1) return -errno;
    return 0;
}

static int xmp_rmdir(const char *path) {
    char dir_path[1000];
    get_dir_path(dir_path, path);
    if (rmdir(dir_path) == -1) return -errno;
    return 0;
}

static int xmp_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    char enc_path[1000];
    get_enc_path(enc_path, path);
    int fd = open(enc_path, fi->flags | O_CREAT | O_WRONLY | O_TRUNC, mode);
    if (fd == -1) return -errno;
    fi->fh = fd;
    return 0;
}

static int xmp_open(const char *path, struct fuse_file_info *fi) {
    char enc_path[1000];
    get_enc_path(enc_path, path);
    int fd = open(enc_path, fi->flags);
    if (fd == -1) return -errno;
    fi->fh = fd;
    return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi) {
    (void) path;
    int res = pread(fi->fh, buf, size, offset);
    if (res == -1) return -errno;
    xor_buffer(buf, res);
    return res;
}

static int xmp_write(const char *path, const char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi) {
    (void) path;
    char *enc_buf = malloc(size);
    if (!enc_buf) return -ENOMEM;
    memcpy(enc_buf, buf, size);
    xor_buffer(enc_buf, size);
    int res = pwrite(fi->fh, enc_buf, size, offset);
    free(enc_buf);
    if (res == -1) return -errno;
    return res;
}

static int xmp_truncate(const char *path, off_t size) {
    char enc_path[1000];
    get_enc_path(enc_path, path);
    if (truncate(enc_path, size) == -1) return -errno;
    return 0;
}

static int xmp_unlink(const char *path) {
    char enc_path[1000];
    get_enc_path(enc_path, path);
    if (unlink(enc_path) == -1) return -errno;
    return 0;
}

static int xmp_access(const char *path, int mask) {
    char dir_path[1000];
    get_dir_path(dir_path, path);
    if (access(dir_path, mask) == 0) return 0;

    char enc_path[1000];
    get_enc_path(enc_path, path);
    if (access(enc_path, mask) == 0) return 0;

    return -ENOENT;
}

static int xmp_utimens(const char *path, const struct timespec ts[2]) {
    char enc_path[1000];
    get_enc_path(enc_path, path);
    if (utimensat(0, enc_path, ts, 0) == -1) return -errno;
    return 0;
}

static int xmp_release(const char *path, struct fuse_file_info *fi) {
    (void) path;
    close(fi->fh);
    return 0;
}

static struct fuse_operations xmp_oper = {
    .getattr  = xmp_getattr,
    .readdir  = xmp_readdir,
    .mkdir    = xmp_mkdir,
    .rmdir    = xmp_rmdir,
    .create   = xmp_create,
    .open     = xmp_open,
    .read     = xmp_read,
    .write    = xmp_write,
    .truncate = xmp_truncate,
    .unlink   = xmp_unlink,
    .access   = xmp_access,
    .utimens  = xmp_utimens,
    .release  = xmp_release,
};

int main(int argc, char *argv[]) {
    umask(0);
    return fuse_main(argc, argv, &xmp_oper, NULL);
}
