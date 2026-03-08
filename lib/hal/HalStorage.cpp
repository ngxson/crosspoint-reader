#define HAL_STORAGE_IMPL
#include "HalStorage.h"

#include <FS.h>  // need to be included before SdFat.h for compatibility with FS.h's File class
#include <Logging.h>
#include <SDCardManager.h>
#include <mbedtls/base64.h>
#include <mbedtls/chacha20.h>

#include <algorithm>
#include <cassert>
#include <cstring>

#define SDCard SDCardManager::getInstance()

HalStorage HalStorage::instance;

HalStorage::HalStorage() {
  storageMutex = xSemaphoreCreateMutex();
  assert(storageMutex != nullptr);
}

// begin() and ready() are only called from setup, no need to acquire mutex for them

bool HalStorage::begin() {
  bool ok = SDCard.begin();
  if (ok) {
    // TODO: replace with real key before shipping
    static constexpr std::array<uint8_t, 32> TEST_KEY = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
        0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
        0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20,
    };
    setEncryptionKey(TEST_KEY);
    LOG_INF("HAL", "Encryption enabled: %s", encryptionEnabled ? "yes" : "no");
  }
  return ok;
}

bool HalStorage::ready() const { return SDCard.ready(); }

// For the rest of the methods, we acquire the mutex to ensure thread safety

class HalStorage::StorageLock {
 public:
  StorageLock() { xSemaphoreTake(HalStorage::getInstance().storageMutex, portMAX_DELAY); }
  ~StorageLock() { xSemaphoreGive(HalStorage::getInstance().storageMutex); }
};

// ---------------------------------------------------------------------------
// Encryption helpers (file-scope)
// ---------------------------------------------------------------------------

// Nonce for filename encryption (fixed – filenames must be deterministic)
static constexpr uint8_t FILENAME_ENC_NONCE[12] = {0xC5, 0x2A, 0x8F, 0x1E, 0x7B, 0x3D,
                                                    0x94, 0x60, 0xAE, 0xF1, 0x27, 0x5C};
// Nonce for file content encryption (fixed for all files)
static constexpr uint8_t CONTENT_ENC_NONCE[12] = {0x91, 0xB4, 0x3E, 0xD7, 0x0F, 0x62,
                                                   0xA8, 0x15, 0xCD, 0x49, 0x76, 0x2B};
// Nonce for the /.cp_encrypted marker file content
static constexpr uint8_t MARKER_ENC_NONCE[12] = {0x37, 0xE9, 0x5C, 0x08, 0x4A, 0xB1,
                                                  0x7D, 0x2F, 0x86, 0x13, 0x9A, 0xC4};

static constexpr char ENCRYPTED_MARKER_FILE[] = "/.cp_encrypted";
static constexpr char ENCRYPTION_MAGIC[] = "CROSSPOINT_ENCRYPTED_SUCCESS";

// ChaCha20 in-place process at arbitrary byte offset within a logical stream.
// key: 32 bytes, nonce: 12 bytes, fileOffset: byte position of buf[0] in the stream.
// Uses a 64-byte stack buffer only for partial first blocks (within 256-byte limit).
static void chachaProcess(const uint8_t* key, const uint8_t* nonce, uint64_t fileOffset,
                          uint8_t* buf, size_t len) {
  if (len == 0) return;
  uint32_t blockCounter = static_cast<uint32_t>(fileOffset / 64);
  size_t skip = static_cast<size_t>(fileOffset % 64);

  if (skip > 0) {
    // Generate keystream for the partial first block and XOR manually
    uint8_t ks[64] = {};
    mbedtls_chacha20_crypt(key, nonce, blockCounter, 64, ks, ks);
    size_t firstBlockBytes = std::min(len, 64 - skip);
    for (size_t i = 0; i < firstBlockBytes; i++) buf[i] ^= ks[skip + i];
    buf += firstBlockBytes;
    len -= firstBlockBytes;
    blockCounter++;
  }

  if (len > 0) {
    mbedtls_chacha20_crypt(key, nonce, blockCounter, len, buf, buf);
  }
}

// Encrypt a filename (just the basename, no slashes) and return "#" + base64url.
// URL-safe base64: '+' -> '-', '/' -> '_', strip '=' padding.
static std::string encryptFilename(const uint8_t* key, const char* name) {
  size_t nameLen = strlen(name);
  if (nameLen == 0) return name;

  // Encrypt a copy (stack buffer too small in general; heap-allocate)
  auto* enc = static_cast<uint8_t*>(malloc(nameLen));
  if (!enc) return name;
  memcpy(enc, name, nameLen);
  chachaProcess(key, FILENAME_ENC_NONCE, 0, enc, nameLen);

  // Base64 encode: compute length first, then encode
  size_t b64Len = 0;
  mbedtls_base64_encode(nullptr, 0, &b64Len, enc, nameLen);
  // b64Len includes the null terminator from mbedtls; result buffer = '#' + base64 + '\0'
  auto* b64 = static_cast<uint8_t*>(malloc(b64Len + 2));
  if (!b64) {
    free(enc);
    return name;
  }
  b64[0] = '#';
  mbedtls_base64_encode(b64 + 1, b64Len, &b64Len, enc, nameLen);
  // b64Len now holds the actual base64 byte count (excluding null terminator)
  free(enc);

  // Convert to URL-safe: '+' -> '-', '/' -> '_', strip trailing '='
  size_t outLen = 1;  // account for leading '#'
  for (size_t i = 0; i < b64Len; i++) {
    uint8_t c = b64[1 + i];
    if (c == '=') break;
    if (c == '+') c = '-';
    if (c == '/') c = '_';
    b64[outLen++] = c;
  }
  b64[outLen] = '\0';

  std::string result(reinterpret_cast<char*>(b64));
  free(b64);
  return result;
}

// Decrypt an encrypted filename (must start with '#').  Returns plaintext name.
static std::string decryptFilename(const uint8_t* key, const char* encName) {
  if (!encName || encName[0] != '#') return encName;

  // Reverse URL-safe base64: '-' -> '+', '_' -> '/'
  std::string b64str(encName + 1);
  for (char& c : b64str) {
    if (c == '-') c = '+';
    if (c == '_') c = '/';
  }
  // Re-add '=' padding
  while (b64str.size() % 4 != 0) b64str += '=';

  size_t outLen = 0;
  mbedtls_base64_decode(nullptr, 0, &outLen,
                        reinterpret_cast<const uint8_t*>(b64str.c_str()), b64str.size());
  if (outLen == 0) return encName;

  auto* buf = static_cast<uint8_t*>(malloc(outLen + 1));
  if (!buf) return encName;
  mbedtls_base64_decode(buf, outLen, &outLen,
                        reinterpret_cast<const uint8_t*>(b64str.c_str()), b64str.size());
  chachaProcess(key, FILENAME_ENC_NONCE, 0, buf, outLen);
  buf[outLen] = '\0';

  std::string result(reinterpret_cast<char*>(buf));
  free(buf);
  return result;
}

// Translate a full path: encrypts every non-empty segment.
// Already-encrypted segments (starting with '#') and the marker file are left unchanged.
static std::string translatePathForEncryption(const uint8_t* key, const char* path) {
  if (!path || path[0] == '\0') return path ? path : "";
  if (strcmp(path, ENCRYPTED_MARKER_FILE) == 0) return path;

  std::string result;
  const char* p = path;
  while (*p) {
    if (*p == '/') {
      result += '/';
      p++;
      continue;
    }
    const char* end = strchr(p, '/');
    size_t segLen = end ? static_cast<size_t>(end - p) : strlen(p);
    std::string seg(p, segLen);
    if (seg[0] != '#') seg = encryptFilename(key, seg.c_str());
    result += seg;
    p += segLen;
  }
  return result;
}

// Decrypt every encrypted segment ('#'-prefixed) in a path.
static std::string decryptAllSegmentsInPath(const uint8_t* key, const char* path) {
  if (!path) return "";
  std::string result;
  const char* p = path;
  while (*p) {
    if (*p == '/') {
      result += '/';
      p++;
      continue;
    }
    const char* end = strchr(p, '/');
    size_t segLen = end ? static_cast<size_t>(end - p) : strlen(p);
    std::string seg(p, segLen);
    if (seg[0] == '#') seg = decryptFilename(key, seg.c_str());
    result += seg;
    p += segLen;
  }
  return result;
}

// ---------------------------------------------------------------------------
// HalStorage – encryption public API
// ---------------------------------------------------------------------------

void HalStorage::setEncryptionKey(const std::array<uint8_t, 32>& key) {
  encryptionKey = key;
  encryptionEnabled = false;

  StorageLock lock;
  const size_t magicLen = strlen(ENCRYPTION_MAGIC);

  if (SDCard.exists(ENCRYPTED_MARKER_FILE)) {
    // Validate key against existing marker
    FsFile f;
    if (!SDCard.openFileForRead("CRYPTO", ENCRYPTED_MARKER_FILE, f)) {
      LOG_ERR("CRYPTO", "Cannot open %s for validation", ENCRYPTED_MARKER_FILE);
      return;
    }
    auto* buf = static_cast<uint8_t*>(malloc(magicLen));
    if (!buf) {
      f.close();
      LOG_ERR("CRYPTO", "malloc failed during key validation");
      return;
    }
    int bytesRead = f.read(buf, magicLen);
    f.close();
    if (bytesRead == static_cast<int>(magicLen)) {
      chachaProcess(key.data(), MARKER_ENC_NONCE, 0, buf, magicLen);
      if (memcmp(buf, ENCRYPTION_MAGIC, magicLen) == 0) {
        encryptionEnabled = true;
        LOG_INF("CRYPTO", "Encryption key validated");
      } else {
        LOG_ERR("CRYPTO", "Encryption key validation failed: wrong key");
      }
    } else {
      LOG_ERR("CRYPTO", "Marker file read error: got %d of %u bytes", bytesRead,
              static_cast<unsigned>(magicLen));
    }
    free(buf);
  } else {
    // First use: create the marker file
    auto* buf = static_cast<uint8_t*>(malloc(magicLen));
    if (!buf) {
      LOG_ERR("CRYPTO", "malloc failed creating marker file");
      return;
    }
    memcpy(buf, ENCRYPTION_MAGIC, magicLen);
    chachaProcess(key.data(), MARKER_ENC_NONCE, 0, buf, magicLen);
    FsFile f;
    if (!SDCard.openFileForWrite("CRYPTO", ENCRYPTED_MARKER_FILE, f)) {
      free(buf);
      LOG_ERR("CRYPTO", "Cannot create %s", ENCRYPTED_MARKER_FILE);
      return;
    }
    f.write(buf, magicLen);
    f.close();
    free(buf);
    encryptionEnabled = true;
    LOG_INF("CRYPTO", "Encryption initialised on this card");
  }
}

bool HalStorage::isEncrypted() const { return encryptionEnabled; }

// ---------------------------------------------------------------------------
// HalFile::Impl  –  gains encryption state
// ---------------------------------------------------------------------------

class HalFile::Impl {
 public:
  explicit Impl(FsFile&& fsFile) : file(std::move(fsFile)) { mbedtls_chacha20_init(&chachaCtx); }
  ~Impl() { mbedtls_chacha20_free(&chachaCtx); }
  FsFile file;
  bool encrypted = false;
  uint8_t contentKey[32] = {};
  // Per-file nonce is shared for all files (see design note in HalStorage.h)
  uint8_t contentNonce[12] = {};

  // Cached ChaCha20 streaming context.
  // chachaPos is the file byte offset at which the context is currently positioned.
  // Sentinel UINT64_MAX means "not yet initialised".
  // ensureChachaAt() is a no-op for sequential access; it only re-initialises after a seek.
  mbedtls_chacha20_context chachaCtx;
  uint64_t chachaPos = UINT64_MAX;

  void ensureChachaAt(uint64_t pos) {
    if (chachaPos == pos) return;
    mbedtls_chacha20_setkey(&chachaCtx, contentKey);
    mbedtls_chacha20_starts(&chachaCtx, contentNonce, static_cast<uint32_t>(pos / 64));
    // Advance within the first block if starting mid-block
    size_t skip = static_cast<size_t>(pos % 64);
    if (skip > 0) {
      uint8_t ks[64] = {};
      mbedtls_chacha20_update(&chachaCtx, skip, ks, ks);
    }
    chachaPos = pos;
  }
};

// ---------------------------------------------------------------------------
// HalFile boilerplate
// ---------------------------------------------------------------------------

HalFile::HalFile() = default;
HalFile::HalFile(std::unique_ptr<Impl> impl) : impl(std::move(impl)) {}
HalFile::~HalFile() = default;
HalFile::HalFile(HalFile&&) = default;
HalFile& HalFile::operator=(HalFile&&) = default;

// ---------------------------------------------------------------------------
// HalStorage file operations
// ---------------------------------------------------------------------------

std::vector<String> HalStorage::listFiles(const char* path, int maxFiles) {
  StorageLock lock;
  const char* actualPath = path;
  std::string encPath;
  if (encryptionEnabled) {
    encPath = translatePathForEncryption(encryptionKey.data(), path);
    actualPath = encPath.c_str();
  }
  auto files = SDCard.listFiles(actualPath, maxFiles);
  if (encryptionEnabled) {
    for (auto& f : files) {
      std::string dec = decryptAllSegmentsInPath(encryptionKey.data(), f.c_str());
      f = dec.c_str();
    }
  }
  return files;
}

String HalStorage::readFile(const char* path) {
  if (!encryptionEnabled) {
    StorageLock lock;
    return SDCard.readFile(path);
  }
  // Route through encrypted HalFile so content is decrypted automatically
  HalFile f;
  if (!openFileForRead("HAL", path, f)) return "";
  size_t sz = f.fileSize();
  if (sz == 0) {
    f.close();
    return "";
  }
  auto* buf = static_cast<char*>(malloc(sz + 1));
  if (!buf) {
    f.close();
    return "";
  }
  f.read(buf, sz);
  f.close();
  buf[sz] = '\0';
  String result(buf);
  free(buf);
  return result;
}

bool HalStorage::readFileToStream(const char* path, Print& out, size_t chunkSize) {
  if (!encryptionEnabled) {
    StorageLock lock;
    return SDCard.readFileToStream(path, out, chunkSize);
  }
  HalFile f;
  if (!openFileForRead("HAL", path, f)) return false;
  auto* buf = static_cast<uint8_t*>(malloc(chunkSize));
  if (!buf) {
    f.close();
    return false;
  }
  while (f.available() > 0) {
    int n = f.read(buf, chunkSize);
    if (n <= 0) break;
    out.write(buf, static_cast<size_t>(n));
  }
  free(buf);
  f.close();
  return true;
}

size_t HalStorage::readFileToBuffer(const char* path, char* buffer, size_t bufferSize,
                                    size_t maxBytes) {
  if (!encryptionEnabled) {
    StorageLock lock;
    return SDCard.readFileToBuffer(path, buffer, bufferSize, maxBytes);
  }
  HalFile f;
  if (!openFileForRead("HAL", path, f)) return 0;
  size_t toRead = maxBytes > 0 ? std::min(maxBytes, bufferSize - 1) : bufferSize - 1;
  int n = f.read(buffer, toRead);
  f.close();
  if (n < 0) n = 0;
  buffer[n] = '\0';
  return static_cast<size_t>(n);
}

bool HalStorage::writeFile(const char* path, const String& content) {
  if (!encryptionEnabled) {
    StorageLock lock;
    return SDCard.writeFile(path, content);
  }
  HalFile f;
  if (!openFileForWrite("HAL", path, f)) return false;
  size_t written = f.write(content.c_str(), content.length());
  f.close();
  return written == content.length();
}

bool HalStorage::ensureDirectoryExists(const char* path) {
  StorageLock lock;
  if (!encryptionEnabled) return SDCard.ensureDirectoryExists(path);
  std::string enc = translatePathForEncryption(encryptionKey.data(), path);
  return SDCard.ensureDirectoryExists(enc.c_str());
}

HalFile HalStorage::open(const char* path, const oflag_t oflag) {
  StorageLock lock;
  bool isMarker = (strcmp(path, ENCRYPTED_MARKER_FILE) == 0);
  const char* actualPath = path;
  std::string encPath;
  if (encryptionEnabled && !isMarker) {
    encPath = translatePathForEncryption(encryptionKey.data(), path);
    actualPath = encPath.c_str();
  }
  FsFile fsFile = SDCard.open(actualPath, oflag);
  auto implPtr = std::make_unique<HalFile::Impl>(std::move(fsFile));
  if (encryptionEnabled && !isMarker && implPtr->file.isOpen()) {
    implPtr->encrypted = true;
    memcpy(implPtr->contentKey, encryptionKey.data(), 32);
    memcpy(implPtr->contentNonce, CONTENT_ENC_NONCE, 12);
  }
  return HalFile(std::move(implPtr));
}

bool HalStorage::mkdir(const char* path, const bool pFlag) {
  StorageLock lock;
  if (!encryptionEnabled) return SDCard.mkdir(path, pFlag);
  std::string enc = translatePathForEncryption(encryptionKey.data(), path);
  return SDCard.mkdir(enc.c_str(), pFlag);
}

bool HalStorage::exists(const char* path) {
  StorageLock lock;
  if (!encryptionEnabled) return SDCard.exists(path);
  std::string enc = translatePathForEncryption(encryptionKey.data(), path);
  return SDCard.exists(enc.c_str());
}

bool HalStorage::remove(const char* path) {
  StorageLock lock;
  if (!encryptionEnabled) return SDCard.remove(path);
  std::string enc = translatePathForEncryption(encryptionKey.data(), path);
  return SDCard.remove(enc.c_str());
}

bool HalStorage::rename(const char* oldPath, const char* newPath) {
  StorageLock lock;
  if (!encryptionEnabled) return SDCard.rename(oldPath, newPath);
  std::string encOld = translatePathForEncryption(encryptionKey.data(), oldPath);
  std::string encNew = translatePathForEncryption(encryptionKey.data(), newPath);
  return SDCard.rename(encOld.c_str(), encNew.c_str());
}

bool HalStorage::rmdir(const char* path) {
  StorageLock lock;
  if (!encryptionEnabled) return SDCard.rmdir(path);
  std::string enc = translatePathForEncryption(encryptionKey.data(), path);
  return SDCard.rmdir(enc.c_str());
}

bool HalStorage::openFileForRead(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock;
  bool isMarker = (strcmp(path, ENCRYPTED_MARKER_FILE) == 0);
  const char* actualPath = path;
  std::string encPath;
  if (encryptionEnabled && !isMarker) {
    encPath = translatePathForEncryption(encryptionKey.data(), path);
    actualPath = encPath.c_str();
  }
  FsFile fsFile;
  bool ok = SDCard.openFileForRead(moduleName, actualPath, fsFile);
  auto implPtr = std::make_unique<HalFile::Impl>(std::move(fsFile));
  if (ok && encryptionEnabled && !isMarker) {
    implPtr->encrypted = true;
    memcpy(implPtr->contentKey, encryptionKey.data(), 32);
    memcpy(implPtr->contentNonce, CONTENT_ENC_NONCE, 12);
  }
  file = HalFile(std::move(implPtr));
  return ok;
}

bool HalStorage::openFileForRead(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForRead(const char* moduleName, const String& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock;
  bool isMarker = (strcmp(path, ENCRYPTED_MARKER_FILE) == 0);
  const char* actualPath = path;
  std::string encPath;
  if (encryptionEnabled && !isMarker) {
    encPath = translatePathForEncryption(encryptionKey.data(), path);
    actualPath = encPath.c_str();
  }
  FsFile fsFile;
  bool ok = SDCard.openFileForWrite(moduleName, actualPath, fsFile);
  auto implPtr = std::make_unique<HalFile::Impl>(std::move(fsFile));
  if (ok && encryptionEnabled && !isMarker) {
    implPtr->encrypted = true;
    memcpy(implPtr->contentKey, encryptionKey.data(), 32);
    memcpy(implPtr->contentNonce, CONTENT_ENC_NONCE, 12);
  }
  file = HalFile(std::move(implPtr));
  return ok;
}

bool HalStorage::openFileForWrite(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const String& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::removeDir(const char* path) {
  StorageLock lock;
  if (!encryptionEnabled) return SDCard.removeDir(path);
  std::string enc = translatePathForEncryption(encryptionKey.data(), path);
  return SDCard.removeDir(enc.c_str());
}

// ---------------------------------------------------------------------------
// HalFile implementation
// Allow doing file operations while ensuring thread safety via HalStorage's mutex.
// Please keep the list below in sync with the HalFile.h header
// ---------------------------------------------------------------------------

#define HAL_FILE_WRAPPED_CALL(method, ...) \
  HalStorage::StorageLock lock;            \
  assert(impl != nullptr);                 \
  return impl->file.method(__VA_ARGS__);

#define HAL_FILE_FORWARD_CALL(method, ...) \
  assert(impl != nullptr);                 \
  return impl->file.method(__VA_ARGS__);

void HalFile::flush() { HAL_FILE_WRAPPED_CALL(flush, ); }

size_t HalFile::getName(char* name, size_t len) {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  size_t n = impl->file.getName(name, len);
  if (impl->encrypted && n > 0 && name[0] == '#') {
    std::string dec = decryptFilename(impl->contentKey, name);
    snprintf(name, len, "%s", dec.c_str());
    n = strnlen(name, len);
  }
  return n;
}

size_t HalFile::size() { HAL_FILE_FORWARD_CALL(size, ); }          // already thread-safe
size_t HalFile::fileSize() { HAL_FILE_FORWARD_CALL(fileSize, ); }  // already thread-safe
bool HalFile::seek(size_t pos) { HAL_FILE_WRAPPED_CALL(seekSet, pos); }
bool HalFile::seekCur(int64_t offset) { HAL_FILE_WRAPPED_CALL(seekCur, offset); }
bool HalFile::seekSet(size_t offset) { HAL_FILE_WRAPPED_CALL(seekSet, offset); }
int HalFile::available() const { HAL_FILE_WRAPPED_CALL(available, ); }
size_t HalFile::position() const { HAL_FILE_WRAPPED_CALL(position, ); }

int HalFile::read(void* buf, size_t count) {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  size_t pos = impl->file.position();
  int n = impl->file.read(buf, count);
  if (n > 0 && impl->encrypted) {
    impl->ensureChachaAt(static_cast<uint64_t>(pos));
    mbedtls_chacha20_update(&impl->chachaCtx, static_cast<size_t>(n),
                            static_cast<uint8_t*>(buf), static_cast<uint8_t*>(buf));
    impl->chachaPos += static_cast<uint64_t>(n);
  }
  return n;
}

int HalFile::read() {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  size_t pos = impl->file.position();
  int result = impl->file.read();
  if (result >= 0 && impl->encrypted) {
    impl->ensureChachaAt(static_cast<uint64_t>(pos));
    uint8_t b = static_cast<uint8_t>(result);
    mbedtls_chacha20_update(&impl->chachaCtx, 1, &b, &b);
    impl->chachaPos++;
    return static_cast<int>(b);
  }
  return result;
}

size_t HalFile::write(const void* buf, size_t count) {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  if (!impl->encrypted) return impl->file.write(buf, count);
  size_t pos = impl->file.position();
  impl->ensureChachaAt(static_cast<uint64_t>(pos));
  // Use stack buffer for small writes to avoid heap overhead
  if (count <= 64) {
    uint8_t tmp[64];
    memcpy(tmp, buf, count);
    mbedtls_chacha20_update(&impl->chachaCtx, count, tmp, tmp);
    impl->chachaPos += count;
    return impl->file.write(tmp, count);
  }
  auto* tmp = static_cast<uint8_t*>(malloc(count));
  if (!tmp) return 0;
  memcpy(tmp, buf, count);
  mbedtls_chacha20_update(&impl->chachaCtx, count, tmp, tmp);
  impl->chachaPos += count;
  size_t written = impl->file.write(tmp, count);
  free(tmp);
  return written;
}

size_t HalFile::write(uint8_t b) {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  if (impl->encrypted) {
    size_t pos = impl->file.position();
    impl->ensureChachaAt(static_cast<uint64_t>(pos));
    mbedtls_chacha20_update(&impl->chachaCtx, 1, &b, &b);
    impl->chachaPos++;
  }
  return impl->file.write(b);
}

bool HalFile::rename(const char* newPath) { HAL_FILE_WRAPPED_CALL(rename, newPath); }
bool HalFile::isDirectory() const { HAL_FILE_FORWARD_CALL(isDirectory, ); }  // already thread-safe
void HalFile::rewindDirectory() { HAL_FILE_WRAPPED_CALL(rewindDirectory, ); }
bool HalFile::close() { HAL_FILE_WRAPPED_CALL(close, ); }

HalFile HalFile::openNextFile() {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  auto nextImpl = std::make_unique<HalFile::Impl>(impl->file.openNextFile());
  if (impl->encrypted) {
    nextImpl->encrypted = true;
    memcpy(nextImpl->contentKey, impl->contentKey, 32);
    memcpy(nextImpl->contentNonce, impl->contentNonce, 12);
  }
  return HalFile(std::move(nextImpl));
}

bool HalFile::isOpen() const { return impl != nullptr && impl->file.isOpen(); }
HalFile::operator bool() const { return isOpen(); }
