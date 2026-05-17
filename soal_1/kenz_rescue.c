#include <unistd.h>
#include <fcntl.h>
#define FUSE_USE_VERSION 28
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>

// Variabel global untuk simpan lokasi folder asli (amba_files)
static char source_dir[1000];

// Fungsi untuk ubah path virtual (mnt) ke path asli (amba_files)
void resolve_path(char *fpath, const char *path) {
    if (strcmp(path, "/") == 0) {
        sprintf(fpath, "%s", source_dir); // Jika di root, arahkan ke folder sumber
    } else {
        sprintf(fpath, "%s%s", source_dir, path); // Gabungkan folder sumber + nama file
    }
}

// Fungsi utama untuk merakit koordinat dari 1.txt sampai 7.txt
char *build_tujuan(size_t *out_len) {
    char fragments[2000] = {0}; // Wadah untuk gabungan potongan koordinat
    int first = 1; // Penanda untuk mengatur spasi antar koordinat

    for (int i = 1; i <= 7; i++) { // Looping file 1 sampai 7
        char filepath[1000]; 
        snprintf(filepath, sizeof(filepath), "%s/%d.txt", source_dir, i); // Buat alamat file

        FILE *f = fopen(filepath, "r"); // Buka file .txt nya
        if (!f) continue; // Kalau gagal buka, lanjut ke file berikutnya

        char line[500];
        while (fgets(line, sizeof(line), f)) { // Baca file baris demi baris
            if (strncmp(line, "KOORD:", 6) == 0) { // Cari baris yang mulai dengan "KOORD:"
                char *val = line + 6; // Ambil teks setelah kata "KOORD:"
                while (*val == ' ') val++; // Buang spasi di depan koordinat
                
                int len = strlen(val);
                // Bersihkan karakter enter (\n atau \r) di ujung teks
                while (len > 0 && (val[len-1] == '\n' || val[len-1] == '\r' || val[len-1] == ' '))
                    val[--len] = '\0';

                // Gabungkan koordinat ke wadah utama (tambah spasi jika bukan yang pertama)
                if (!first) strncat(fragments, " ", sizeof(fragments) - strlen(fragments) - 1); 
                strncat(fragments, val, sizeof(fragments) - strlen(fragments) - 1);
                first = 0;
            }
        }
        fclose(f); // Tutup file setelah selesai dibaca
    }

    char *content = NULL;
    // Buat kalimat akhir: "Tujuan Mas Amba: [koordinat]"
    int written = asprintf(&content, "Tujuan Mas Amba: %s\n", fragments);
    if (written < 0) return NULL; 
    *out_len = (size_t)written; // Simpan panjang total teks
    return content; // Kembalikan hasil rakitan
}

// Fungsi untuk memberikan informasi file (ukuran, tipe, izin)
static int xmp_getattr(const char *path, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));

    // Logika khusus untuk file virtual tujuan.txt
    if (strcmp(path, "/tujuan.txt") == 0) {
        stbuf->st_mode = S_IFREG | 0444; // Tipe file biasa & read-only
        stbuf->st_nlink = 1;
        size_t len = 0;
        char *content = build_tujuan(&len); // Rakit konten untuk hitung ukurannya
        stbuf->st_size = (off_t)len; // Set ukuran file sesuai panjang koordinat
        free(content);
        return 0;
    }

    char fpath[1000]; resolve_path(fpath, path); // Cari lokasi asli file lainnya
    int res = lstat(fpath, stbuf); // Ambil info asli dari disk
    if (res == -1) return -errno;
    return 0;
}

// Fungsi untuk menampilkan daftar file saat di-ls
static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    char fpath[1000]; resolve_path(fpath, path);
    DIR *dp = opendir(fpath); // Buka folder asli
    if (dp == NULL) return -errno;

    struct dirent *de; 
    filler(buf, ".", NULL, 0); // Tampilkan direktori diri sendiri
    filler(buf, "..", NULL, 0); // Tampilkan direktori atas

    while ((de = readdir(dp)) != NULL) { // Baca semua file di folder asli
        struct stat st; 
        memset(&st, 0, sizeof(st)); 
        st.st_ino = de->d_ino; 
        st.st_mode = de->d_type << 12; 
        filler(buf, de->d_name, &st, 0); // Masukkan file asli ke daftar mnt/
    }
    closedir(dp);

    // Sisipkan file "tujuan.txt" secara manual ke daftar mnt/
    if (strcmp(path, "/") == 0) {
        filler(buf, "tujuan.txt", NULL, 0);
    } 
    return 0;
}

// Fungsi pengecekan saat file dibuka
static int xmp_open(const char *path, struct fuse_file_info *fi) {
    if (strcmp(path, "/tujuan.txt") == 0) {
        if ((fi->flags & O_ACCMODE) != O_RDONLY) return -EACCES; // tujuan.txt hanya boleh dibaca
        return 0;
    }
    char fpath[1000]; resolve_path(fpath, path);
    int fd = open(fpath, fi->flags); // Buka file asli
    if (fd == -1) return -errno; 
    close(fd);
    return 0;
}

// Fungsi untuk membaca isi file
static int xmp_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    if (strcmp(path, "/tujuan.txt") == 0) { // Jika yang dibaca adalah tujuan.txt
        size_t len = 0;
        char *content = build_tujuan(&len); // Rakit koordinat on-the-fly
        if (!content) return -ENOMEM;
        if ((size_t)offset >= len) { free(content); return 0; }
        size_t to_copy = len - (size_t)offset;
        if (to_copy > size) to_copy = size; 
        memcpy(buf, content + offset, to_copy); // Salin hasil rakitan ke layar user
        free(content);
        return (int)to_copy;
    }

    char fpath[1000]; resolve_path(fpath, path);
    int fd = open(fpath, O_RDONLY); // Baca dari file asli
    if (fd == -1) return -errno;
    int res = pread(fd, buf, size, offset); // Ambil data dari disk
    if (res == -1) res = -errno; 
    close(fd);
    return res;
}

// Daftarkan fungsi-fungsi di atas ke dalam operasi FUSE
static struct fuse_operations xmp_oper = { 
    .getattr = xmp_getattr,
    .readdir = xmp_readdir,
    .open    = xmp_open,
    .read    = xmp_read,
};

int main(int argc, char *argv[]) {
    if (argc < 3) { // Cek apakah argumen lengkap (source & mount)
        fprintf(stderr, "Usage: %s <source_directory> <mount_directory>\n", argv[0]);
        return 1;
    }

    // Ubah folder amba_files jadi path absolut agar aman
    if (realpath(argv[1], source_dir) == NULL) { 
        perror("realpath");
        return 1;
    }

    // Susun ulang argumen agar cocok dengan format fuse_main
    char *fuse_argv[argc]; 
    fuse_argv[0] = argv[0]; 
    fuse_argv[1] = argv[2]; // Folder mount (mnt)
    for (int i = 3; i < argc; i++) fuse_argv[i - 1] = argv[i];
    int fuse_argc = argc - 1;

    umask(0); // Reset permission mask
    return fuse_main(fuse_argc, fuse_argv, &xmp_oper, NULL); // Jalankan filesystem!
}
