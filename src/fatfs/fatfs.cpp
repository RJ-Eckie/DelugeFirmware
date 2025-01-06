#include "fatfs.hpp"
#include "ff.h"
#include <string>
extern "C" {
FRESULT f_readdir_get_filepointer(
    DIR *dp,      /* Pointer to the open directory object */
    FILINFO *fno, /* Pointer to file information to return */
    FilePointer *filePointer);
}

#define FF_TRY(expr)                                                           \
  do {                                                                         \
    FRESULT error = (expr);                                                    \
    if (error != FR_OK) {                                                      \
      return std::unexpected(FatFS::Error(error));                             \
    }                                                                          \
  } while (0)

namespace FatFS {

std::expected<File, Error> File::open(const char* path,
                                      FileAccessMode mode) {
  File file{};
  FF_TRY(f_open(&file.file_, path, mode));
  return file;
}

std::expected<File, Error> File::open(std::string_view path,
                                      FileAccessMode mode) {
  return open(std::string(path).c_str(), mode);
}

std::expected<File, Error> File::open(const std::string& path,
                                      FileAccessMode mode) {
  return open(path.c_str(), mode);
}

std::expected<void, Error> File::close() {
  FF_TRY(f_close(&file_));
  return {};
}

std::expected<std::span<std::byte>, Error>
File::read(std::span<std::byte> buffer) {
  unsigned int num_bytes_read = 0;
  FF_TRY(f_read(&file_, buffer.data(), buffer.size_bytes(), &num_bytes_read));
  return std::span{buffer.data(), num_bytes_read};
}

std::expected<unsigned int, Error> File::write(std::span<std::byte> buffer) {
  unsigned int num_bytes_written = 0;
  FF_TRY(
      f_write(&file_, buffer.data(), buffer.size_bytes(), &num_bytes_written));
  return num_bytes_written;
}

std::expected<void, Error> File::truncate() {
  FF_TRY(f_truncate(&file_));
  return {};
}

std::expected<void, Error> File::sync() {
  FF_TRY(f_sync(&file_));
  return {};
}

std::expected<void, Error> File::lseek(size_t offset) {
  FF_TRY(f_lseek(&file_, offset));
  return {};
}

std::expected<Directory, Error> Directory::open(const char* path) {
  Directory dir{};
  FF_TRY(f_opendir(&dir.dir_, path));
  return dir;
}


std::expected<Directory, Error> Directory::open(std::string_view path) {
  return open(std::string(path).c_str());
}

std::expected<Directory, Error> Directory::open(const std::string& path) {
  return open(path.c_str());
}

std::expected<void, Error> Directory::close() {
  FF_TRY(f_closedir(&dir_));
  return {};
}

std::expected<FileInfo, Error> Directory::read() {
  FileInfo info;
  FF_TRY(f_readdir(&dir_, &info));
  return info;
}

std::expected<std::pair<FileInfo, FilePointer>, Error>
Directory::read_and_get_filepointer() {
  FileInfo info;
  FilePointer fp;
  FF_TRY(f_readdir_get_filepointer(&dir_, &info, &fp));
  return std::make_pair(info, fp);
}

std::expected<void, Error> Directory::create_name(const char **path) {
  FF_TRY(::create_name(&dir_, path));
  return {};
}

std::expected<void, Error> Directory::find() {
  FF_TRY(dir_find(&dir_));
  return {};
}

std::expected<void, Error> Directory::rewind() {
  FF_TRY(f_readdir(&dir_, nullptr));
  return {};
}

std::expected<void, Error> mkdir(const char* path) {
  FF_TRY(f_mkdir(path));
  return {};
}

std::expected<void, Error> mkdir(std::string_view path) {
  return mkdir(std::string(path).c_str());
}

std::expected<void, Error> mkdir(const std::string& path) {
  return mkdir(path.c_str());
}

std::expected<Directory, Error> mkdir_and_open(const char* path) {
  Directory dir{};
  FF_TRY(f_mkdir_and_open(&dir.dir_, path));
  return dir;
}

std::expected<Directory, Error> mkdir_and_open(std::string_view path) {
  return mkdir_and_open(std::string(path).c_str());
}

std::expected<Directory, Error> mkdir_and_open(const std::string& path) {
  return mkdir_and_open(path.c_str());
}

#if FF_FS_READONLY == 0 and FF_FS_MINIMIZE == 0
std::expected<void, Error> unlink(const char* path) {
  FF_TRY(f_unlink(path));
  return {};
}

std::expected<void, Error> unlink(std::string_view path) {
  return unlink(std::string(path).c_str());
}

std::expected<void, Error> unlink(const std::string& path) {
  return unlink(path.c_str());
}

std::expected<void, Error> rename(const char* path_old, const char* path_new) {
  FF_TRY(f_rename(path_old, path_new));
  return {};
}

std::expected<void, Error> rename(std::string_view path_old,
                                  std::string_view path_new) {
  return rename(std::string(path_old).c_str(), std::string(path_new).c_str());
}

std::expected<void, Error> rename(const std::string& path_old,
								  const std::string& path_new) {
  return rename(path_old.c_str(), path_new.c_str());
}

std::expected<FileInfo, Error> stat(const char* path) {
  FileInfo info;
  FF_TRY(f_stat(path, &info));
  return info;
}

std::expected<FileInfo, Error> stat(std::string_view path) {
  return stat(std::string(path).c_str());
}

std::expected<FileInfo, Error> stat(const std::string& path) {
  return stat(path.c_str());
}
#endif

std::expected<bool, Error> Filesystem::mount(BYTE opt, char const* path) {
  FRESULT error = f_mount(this, path, opt);

  // From the fatfs docs for f_mount:
  // If the function with forced mounting (opt = 1) failed with FR_NOT_READY, it
  // means that the filesystem object has been registered successfully but the
  // volume is currently not ready to work. The volume mount process will be
  // attempted on subsequent file/directroy function.
  if (error == FR_OK) {
    return true;
  }
  if (error == FR_NOT_READY && opt == 1) {
    return false;
  }
// at this point we'd have no filesystem, but the only possible error is an invalid path and our null string is valid
  return std::unexpected(FatFS::Error(error));
}

#if FF_USE_CHMOD
/* Change attribute of a file/dir */
std::expected<void, Error> chmod(const char* path, BYTE attr, BYTE mask) {
	FRESULT error = f_chmod(path, attr, mask);
	if (error != FR_OK) {
		return std::unexpected(FatFS::Error(error));
	}
	return {};
}

std::expected<void, Error> chmod(std::string_view path, BYTE attr, BYTE mask) {
  return chmod(std::string(path).c_str(), attr, mask);
}

std::expected<void, Error> chmod(const std::string& path, BYTE attr, BYTE mask) {
  return chmod(path.c_str(), attr, mask);
}


/* Change timestamp of a file/dir */
std::expected<void, Error> utime(const char* path, const FILINFO *fno) {
	FRESULT error = f_utime(path, fno);
	if (error != FR_OK) {
		return std::unexpected(FatFS::Error(error));
	}
	return {};
}

std::expected<void, Error> utime(std::string_view path, const FILINFO *fno) {
  return utime(std::string(path).c_str(), fno);
}

std::expected<void, Error> utime(const std::string& path, const FILINFO *fno) {
  return utime(path.c_str(), fno);
}
#endif

} // namespace FatFS
