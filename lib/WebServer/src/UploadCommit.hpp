#pragma once

// Filesystem adapter supplies exists/remove/rename. Promotion happens only
// after the HTTP request and its one nonempty file part have been accepted.
// Preserve a backup if recovery fails; never erase an unexplained old backup.
template <class Filesystem, class Path>
int commitUpload(Filesystem& fs, const Path& staged, const Path& destination,
                 const Path& backup) {
    if (fs.exists(backup)) return 409;
    const bool replacing = fs.exists(destination);
    if (replacing && !fs.rename(destination, backup)) return 500;
    if (!fs.rename(staged, destination)) {
        if (replacing && !fs.rename(backup, destination)) return 507;
        return 500;
    }
    if (replacing) fs.remove(backup);
    return 0;
}
