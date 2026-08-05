#include <archive.h>
#include <archive_entry.h>

#include <stdio.h>
#include <stdlib.h>

static const char *archive_message(struct archive *archive) {
    const char *message = archive_error_string(archive);
    return message == NULL ? "no libarchive diagnostic" : message;
}

static int reject_entry(const char *category, const char *path,
                        struct archive *archive) {
    fprintf(stderr,
            "moguet-live-aur-archive-metadata: rejected: category=%s "
            "entry=%s: %s\n",
            category, path, archive_message(archive));
    return 1;
}

static int reject_archive_read(const char *path, struct archive *archive) {
    return reject_entry("archive-read", path, archive);
}

int main(int argc, char *argv[]) {
    const char *archive_path;
    struct archive *archive;
    struct archive_entry *entry;
    unsigned long entry_count = 0;
    int status;

    if (argc != 2) {
        fprintf(stderr, "usage: aur-archive-metadata-check ARCHIVE\n");
        return 2;
    }

    archive_path = argv[1];
    archive = archive_read_new();
    if (archive == NULL) {
        fprintf(stderr,
                "moguet-live-aur-archive-metadata: rejected: "
                "category=archive-read entry=%s: archive allocation failed\n",
                archive_path);
        return 1;
    }

    status = archive_read_support_filter_all(archive);
    if (status != ARCHIVE_OK) {
        (void)reject_archive_read(archive_path, archive);
        (void)archive_read_free(archive);
        return 1;
    }
    status = archive_read_support_format_all(archive);
    if (status != ARCHIVE_OK) {
        (void)reject_archive_read(archive_path, archive);
        (void)archive_read_free(archive);
        return 1;
    }
    status = archive_read_open_filename(archive, archive_path, 10240);
    if (status != ARCHIVE_OK) {
        (void)reject_archive_read(archive_path, archive);
        (void)archive_read_free(archive);
        return 1;
    }

    for (;;) {
        const char *entry_path;
        unsigned long set_flags;
        unsigned long clear_flags;
        int acl_types;
        int xattr_count;

        status = archive_read_next_header(archive, &entry);
        if (status == ARCHIVE_EOF) {
            break;
        }
        if (status != ARCHIVE_OK) {
            (void)reject_archive_read(archive_path, archive);
            (void)archive_read_free(archive);
            return 1;
        }

        entry_path = archive_entry_pathname(entry);
        if (entry_path == NULL || entry_path[0] == '\0') {
            (void)reject_archive_read("(unavailable)", archive);
            (void)archive_read_free(archive);
            return 1;
        }
        entry_count++;

        acl_types = archive_entry_acl_types(entry);
        if (acl_types != 0) {
            (void)reject_entry("acl", entry_path, archive);
            (void)archive_read_free(archive);
            return 1;
        }

        xattr_count = archive_entry_xattr_count(entry);
        if (xattr_count < 0) {
            (void)reject_archive_read(entry_path, archive);
            (void)archive_read_free(archive);
            return 1;
        }
        if (xattr_count != 0) {
            (void)reject_entry("xattr", entry_path, archive);
            (void)archive_read_free(archive);
            return 1;
        }

        archive_entry_fflags(entry, &set_flags, &clear_flags);
        if (set_flags != 0 || clear_flags != 0) {
            (void)reject_entry("fflags", entry_path, archive);
            (void)archive_read_free(archive);
            return 1;
        }

        status = archive_read_data_skip(archive);
        if (status != ARCHIVE_OK) {
            (void)reject_archive_read(entry_path, archive);
            (void)archive_read_free(archive);
            return 1;
        }
    }

    status = archive_read_close(archive);
    if (status != ARCHIVE_OK) {
        (void)reject_archive_read(archive_path, archive);
        (void)archive_read_free(archive);
        return 1;
    }
    status = archive_read_free(archive);
    if (status != ARCHIVE_OK) {
        fprintf(stderr,
                "moguet-live-aur-archive-metadata: rejected: "
                "category=archive-read entry=%s: archive cleanup failed\n",
                archive_path);
        return 1;
    }
    printf("moguet-live-aur-archive-metadata: accepted: entries=%lu\n",
           entry_count);
    return 0;
}
