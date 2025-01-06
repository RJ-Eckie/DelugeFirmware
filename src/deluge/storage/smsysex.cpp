#include "storage/smsysex.h"
#include "fatfs.hpp"
#include "fatfs/ff.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/oled.h"
#include "hid/display/seven_segment.h"
#include "hid/hid_sysex.h"
#include "io/debug/log.h"
#include "io/debug/print.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_engine.h"
#include "io/midi/sysex.h"
#include "memory/general_memory_allocator.h"
#include "processing/engines/audio_engine.h"
#include "scheduler_api.h"
#include "util/containers.h"
#include "util/pack.h"
#include "util/try.h"
#include <cstring>
#include <utility>

#define MAX_DIR_LINES 25

extern "C" {
extern uint8_t currentlyAccessingCard;
}
DIR sxDIR;
uint32_t dirOffsetCounter;

JsonSerializer jWriter;
std::string activeDirName;
const size_t blockBufferMax = 1024;
const size_t sysexBufferMax = blockBufferMax + 256;
uint8_t* writeBlockBuffer = nullptr;
uint8_t* readBlockBuffer = nullptr;
const uint32_t MAX_OPEN_FILES = 4;

struct FILdata {
	String fName;
	uint32_t fileID;
	uint32_t LRUstamp = 0;
	uint32_t fSize = 0;
	uint32_t fPosition = 0; // file offset noted after last read/write operation.
	bool fileOpen = false;
	int forWrite = false;
	FIL file;
};

int32_t FIDcounter = 1;
uint32_t LRUcounter = 1;

FILdata openFiles[MAX_OPEN_FILES];

const int MaxSysExLength = 1024;

struct SysExDataEntry {
	MIDICable& cable;
	size_t len;
	std::byte data[sysexBufferMax];

	SysExDataEntry(MIDICable& forCable, int32_t newLen) : cable{forCable}, len{newLen} {}
};

deluge::deque<SysExDataEntry> SysExQ;

// The following constants assume that the messageID part ranges from 1 to SYSEX_MSGID_MAX
// and that SYSEX_MSGID_MAX is 1 less than a power of 2.
// It also assumes that MAX_SYSEX_SESSIONS is also 1 less than a power of 2.
const uint32_t MAX_SYSEX_SESSIONS = 15;
const uint32_t SYSEX_MSGID_MAX = 7;
const uint32_t SYSEX_MSGID_MASK = 0x07;
const uint32_t SYSEX_SESSION_MASK = 0x78;
const uint32_t SYSEX_SESSION_SHIFT = 3;

uint32_t session_mono_counter = 1;
uint32_t sessionLRU_array[MAX_SYSEX_SESSIONS + 1] = {0};

void smSysex::noteSessionIdUse(uint8_t msgId) {
	uint32_t sessionNum = (msgId & SYSEX_SESSION_MASK) >> SYSEX_SESSION_SHIFT;
	sessionLRU_array[sessionNum] = session_mono_counter++;
}

void smSysex::noteFileIdUse(FILdata* fp) {
	fp->LRUstamp = LRUcounter++;
}

// Returns the entry in the FILdata array for the given fid
FILdata* smSysex::entryForFID(uint32_t fid) {
	for (int i = 0; i < MAX_OPEN_FILES; ++i) {
		if (openFiles[i].fileID == fid) {
			return openFiles + i;
		}
	}
	return nullptr;
}

// Assign a FIL from our pool.
FILdata* smSysex::findEmptyFIL() {
	uint32_t LRUtime = 0xFFFFFFFF;
	uint32_t LRUindex = 0;

	for (int i = 0; i < MAX_OPEN_FILES; ++i) {
		if (!openFiles[i].fileOpen) {
			return openFiles + i;
		}
		else if (openFiles[i].LRUstamp < LRUtime) {
			LRUtime = openFiles[i].LRUstamp;
			LRUindex = i;
		}
	}
	// Close the abandoned file before we reuse the entry.
	FILdata* oldest = openFiles + LRUindex;
	closeFIL(oldest);
	return oldest;
}

void smSysex::startDirect(JsonSerializer& writer) {
	writer.reset();
	writer.setMemoryBased();
	uint8_t reply_hdr[7] = {0xf0, 0x00, 0x21, 0x7B, 0x01, SysEx::SysexCommands::Json, 0};
	writer.writeBlock({reinterpret_cast<std::byte*>(reply_hdr), sizeof(reply_hdr)});
}

void smSysex::startReply(JsonSerializer& writer, JsonDeserializer& reader) {
	writer.reset();
	writer.setMemoryBased();
	uint8_t reply_hdr[7] = {0xf0, 0x00, 0x21, 0x7B, 0x01, SysEx::SysexCommands::JsonReply, reader.getReplySeqNum()};
	writer.writeBlock({reinterpret_cast<std::byte*>(reply_hdr), sizeof(reply_hdr)});
}

void smSysex::sendMsg(MIDICable& cable, JsonSerializer& writer) {
	writer.writeByte(std::byte{0xF7});

	std::byte* bitz = writer.getBufferPtr();
	int32_t bw = writer.bytesWritten();
	cable.sendSysex((const uint8_t*)bitz, bw);
};

FILdata* smSysex::openFIL(const char* fPath, int forWrite, uint32_t* fsize, FRESULT* eCode) {
	FILdata* fp = findEmptyFIL();
	fp->fName.set(fPath);
	fp->fileID = FIDcounter++;
	noteFileIdUse(fp);
	BYTE mode = FA_READ;
	if (forWrite) {
		mode = FA_WRITE;
		if (forWrite == 1) {
			mode |= FA_CREATE_ALWAYS;
		}
	}
	FRESULT err = f_open(&fp->file, fPath, mode);
	*eCode = err;
	if (err == FRESULT::FR_OK) {
		fp->fileOpen = true;
		fp->forWrite = forWrite;
		fp->fSize = f_size(&fp->file);
		fp->fPosition = 0;
		return fp;
	}
	return nullptr;
}

FRESULT smSysex::closeFIL(FILdata* fp) {
	if (fp == nullptr)
		return FRESULT::FR_INVALID_OBJECT;

	FRESULT err = f_close(&fp->file);
	fp->fileOpen = false;
	fp->forWrite = 0;
	fp->fSize = 0;
	return err;
}

// Fill in missing directories for the full path name given.
// Unless the last character in the path is a /, we assume the
// path given ends with a filename (which we ignore).
FRESULT smSysex::createPathDirectories(std::string_view path, uint32_t date, uint32_t time) {
	FRESULT errCode;
	if (path.length() > 256) {
		return FRESULT::FR_INVALID_PARAMETER;
	}

	char working[257];
	char pathPart[257];
	strncpy(working, path.data(), path.size());
	// ignore the file name and extension part.
	int len = strlen(working);
	int lastSlash;
	for (lastSlash = len - 1; lastSlash >= 0; lastSlash--) {
		if (working[lastSlash] == '/')
			break;
	}
	if (lastSlash == 0)
		return FRESULT::FR_INVALID_PARAMETER;

	int jx = 1; // skip the leading slash.
	while (jx <= lastSlash) {
		if (working[jx] == '/') {
			DIR wDIR;
			working[jx] = 0;
			strcpy(pathPart, working);
			working[jx] = '/';
			if (strlen(pathPart)) {
				errCode = f_opendir(&wDIR, (TCHAR*)pathPart);
				if (errCode == FRESULT::FR_NO_PATH) {
					errCode = f_mkdir((TCHAR*)pathPart);
					if (errCode == FRESULT::FR_OK && (date != 0 || time != 0)) {
						FILINFO finfo;
						finfo.fdate = date;
						finfo.ftime = time;
						errCode = f_utime(pathPart, &finfo);
					}
				}
				else if (errCode == 0) {
					errCode = f_closedir(&wDIR);
				}
				else {
					return errCode;
				}
			}
		}
		jx++;
	}
	return errCode;
}

void smSysex::openFile(MIDICable& cable, JsonDeserializer& reader) {
	bool forWrite = false;
	std::string path;
	int32_t rn = 0;
	char const* tagName;
	uint32_t date = 0;
	uint32_t time = 0;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "write")) {
			forWrite = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "path")) {
			path = reader.readTagOrAttributeValueString().value_or("");
		}
		// Since you can't change the date/time of an open file, we use date/time
		// only for created directoris.
		else if (!strcmp(tagName, "date")) {
			date = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "time")) {
			time = reader.readTagOrAttributeValueInt();
		}
		else {
			reader.exitTag();
		}
	}

	reader.match('}');
	bool pathCreateTried = false;
retry:
	FRESULT errCode;
	uint32_t fSize = 0;

	FILdata* fp = openFIL(path.c_str(), forWrite, &fSize, &errCode);

	if (fp != nullptr) {
		fSize = fp->fSize;
	}
	if (forWrite && !pathCreateTried && errCode == FRESULT::FR_NO_PATH) { // was the path missing?
		createPathDirectories(path, date, time);
		pathCreateTried = true;
		goto retry;
	}

	startReply(jWriter, reader);
	jWriter.writeOpeningTag("^open", false, true);
	jWriter.writeAttribute("fid", fp != nullptr ? fp->fileID : 0);
	jWriter.writeAttribute("size", fSize);
	jWriter.writeAttribute("err", errCode);
	jWriter.closeTag(true);

	sendMsg(cable, jWriter);
}

void smSysex::closeFile(MIDICable& cable, JsonDeserializer& reader) {
	int32_t fid = 0;
	char const* tagName;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "fid")) {
			fid = reader.readTagOrAttributeValueInt();
		}
		else {
			reader.exitTag();
		}
	}
	reader.match('}');

	FILdata* fd = entryForFID(fid);
	FRESULT errCode = closeFIL(fd);

	startReply(jWriter, reader);
	jWriter.writeOpeningTag("^close", false, true);
	jWriter.writeAttribute("fid", (uint32_t)fid);
	jWriter.writeAttribute("err", errCode);
	jWriter.closeTag(true);

	sendMsg(cable, jWriter);
}

void smSysex::deleteFile(MIDICable& cable, JsonDeserializer& reader) {
	char const* tagName;
	std::string path;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "path")) {
			path = D_TRY_CATCH(reader.readTagOrAttributeValueString(), error, {
				return; // fail fast if we can't read the path.
			});
		}
		else {
			reader.exitTag();
		}
	}
	reader.match('}');

	if (!path.empty()) {
		D_PRINTLN(path.c_str());
		auto unlinked = FatFS::unlink(path);
		startReply(jWriter, reader);
		jWriter.writeOpeningTag("^delete", false, true);
		jWriter.writeAttribute("err", unlinked ? FRESULT::FR_OK : std::to_underlying(unlinked.error()));
		jWriter.closeTag(true);
		sendMsg(cable, jWriter);
	}
}

void smSysex::createDirectory(MIDICable& cable, JsonDeserializer& reader) {
	char const* tagName;
	std::string path;
	uint32_t date = 0;
	uint32_t time = 0;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "path")) {
			path = D_TRY_CATCH(reader.readTagOrAttributeValueString(), error, {
				return; // fail fast if we can't read the path.
			});
		}
		else if (!strcmp(tagName, "date")) {
			date = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "time")) {
			time = reader.readTagOrAttributeValueInt();
		}
		else {
			reader.exitTag();
		}
	}
	reader.match('}');

	if (!path.empty()) {
		D_PRINTLN(path.c_str());
		auto newDir = FatFS::mkdir(path);

		FRESULT errCode = FRESULT::FR_OK;
		if (newDir.has_value() && (date != 0 || time != 0)) {
			FILINFO finfo;
			finfo.fdate = date;
			finfo.ftime = time;
			auto updated = FatFS::utime(path, &finfo);
			errCode = updated ? FRESULT::FR_OK : static_cast<FRESULT>(updated.error());
		}
		startReply(jWriter, reader);
		jWriter.writeOpeningTag("^mkdir", false, true);
		jWriter.writeAttribute("path", path.c_str());
		jWriter.writeAttribute("err", errCode);
		jWriter.closeTag(true);
		sendMsg(cable, jWriter);
	}
}

void smSysex::rename(MIDICable& cable, JsonDeserializer& reader) {
	FRESULT errCode = FRESULT::FR_OK;

	char const* tagName;
	String fromName;
	String toName;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "from")) {
			fromName = D_TRY_CATCH(reader.readTagOrAttributeValueString(), error, {
				return; // fail fast if we can't read the path.
			});
		}
		else if (!strcmp(tagName, "to")) {
			toName = D_TRY_CATCH(reader.readTagOrAttributeValueString(), error, {
				return; // fail fast if we can't read the path.
			});
		}
		else {
			reader.exitTag();
		}
	}
	reader.match('}');

	const char* fromVal = fromName.get();
	const TCHAR* fromTC = (const TCHAR*)fromVal;
	const char* toVal = toName.get();
	const TCHAR* toTC = (const TCHAR*)toVal;

	if (fromTC && strlen(fromTC) && toTC && strlen(toTC)) {
		D_PRINTLN(fromTC);
		D_PRINTLN(toTC);
		errCode = f_rename(fromTC, toTC);
		startReply(jWriter, reader);
		jWriter.writeOpeningTag("^mkdir", false, true);
		jWriter.writeAttribute("path", toName.get());
		jWriter.writeAttribute("err", errCode);
		jWriter.closeTag(true);
		sendMsg(cable, jWriter);
	}
}

// Returns a block of directory entries as a Json array.
void smSysex::getDirEntries(MIDICable& cable, JsonDeserializer& reader) {
	std::string path = "/";
	uint32_t lineOffset = 0;
	uint32_t linesWanted = 20;

	FRESULT errCode = FRESULT::FR_OK;
	char const* tagName;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "offset")) {
			lineOffset = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "lines")) {
			linesWanted = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "path")) {
			path = D_TRY_CATCH(reader.readTagOrAttributeValueString(), error, {
				return; // fail fast if we can't read the path.
			});
		}
		else {
			reader.exitTag();
		}
	}
	reader.match('}');
	if (linesWanted > MAX_DIR_LINES)
		linesWanted = MAX_DIR_LINES;
	// We should pick up on path changes and out-of-order offset requests.

	if (lineOffset == 0 || activeDirName != path || lineOffset != dirOffsetCounter) {
		errCode = f_opendir(&sxDIR, path.c_str());
		if (errCode != FRESULT::FR_OK) {
			goto errorFound;
		}
		dirOffsetCounter = 0;
		activeDirName = path;
		if (lineOffset > 0) {
			FILINFO fno;
			for (uint32_t ix = 0; ix < lineOffset; ++ix) {
				errCode = f_readdir(&sxDIR, &fno);
				if (errCode != FRESULT::FR_OK)
					break;
				if (fno.altname[0] == 0)
					break;
				dirOffsetCounter++;
			}
		}
	}
errorFound:;
	jWriter.reset();
	jWriter.setMemoryBased();
	startReply(jWriter, reader);
	jWriter.writeOpeningTag("^dir", false, true);
	jWriter.writeArrayStart("list", true, false);

	for (uint32_t ix = 0; ix < linesWanted; ++ix) {
		FILINFO fno;
		FRESULT err = f_readdir(&sxDIR, &fno);
		if (err != FRESULT::FR_OK)
			break;
		if (fno.altname[0] == 0)
			break;

		jWriter.writeOpeningTag(NULL, true);
		jWriter.writeAttribute("name", fno.fname);
		jWriter.writeAttribute("size", fno.fsize);
		jWriter.writeAttribute("date", fno.fdate);
		jWriter.writeAttribute("time", fno.ftime);

		// AM_RDO  0x01 Read only
		// AM_HID  0x02 Hidden
		// AM_SYS  0x04 System

		// AM_DIR  0x10 Directory
		// AM_ARC  0x20 Archive
		jWriter.writeAttribute("attr", fno.fattrib);

		jWriter.closeTag();
		dirOffsetCounter++;
	}
	jWriter.writeArrayEnding("list", true, false);
	jWriter.writeAttribute("err", errCode);
	jWriter.closeTag(true);
	sendMsg(cable, jWriter);
}

void smSysex::readBlock(MIDICable& cable, JsonDeserializer& reader) {
	char const* tagName;
	uint32_t addr = 0;
	uint32_t size = blockBufferMax;
	int32_t fid = 0;

	auto repSN = reader.getReplySeqNum();
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "fid")) {
			fid = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "addr")) {
			addr = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "size")) {
			size = reader.readTagOrAttributeValueInt();
			if (size > blockBufferMax)
				size = blockBufferMax;
		}
		else {
			reader.exitTag();
		}
	}
	reader.match('}');

	FILdata* fp = entryForFID(fid);
	FRESULT errCode = FR_OK;

	if (fp == nullptr) {
		errCode = FRESULT::FR_NOT_ENABLED;
	}
	UINT actuallyRead = 0;
	uint8_t* srcAddr = (uint8_t*)addr;
	if (errCode == FRESULT::FR_OK) {
		if (!readBlockBuffer && fp) {
			readBlockBuffer = (uint8_t*)GeneralMemoryAllocator::get().allocLowSpeed(blockBufferMax);
		}

		if (readBlockBuffer && fp) {
			noteFileIdUse(fp);
			// If file position requested is not what we expect, seek to requested.
			if (fp->fPosition != addr) {
				errCode = f_lseek(&fp->file, addr);
			}
			if (errCode == FRESULT::FR_OK) {
				errCode = f_read(&fp->file, readBlockBuffer, size, &actuallyRead);
				size = actuallyRead;
				srcAddr = readBlockBuffer;
				fp->fPosition = addr + actuallyRead;
			}
			else {
				D_PRINTLN("lseek issue: %d", errCode);
			}
		}
	}
	else {
		size = 0;
	}
	startReply(jWriter, reader);
	jWriter.writeOpeningTag("^read", false, true);
	jWriter.writeAttribute("fid", fid);
	jWriter.writeAttribute("addr", addr);
	jWriter.writeAttribute("size", size);
	jWriter.writeAttribute("err", errCode);
	jWriter.closeTag(true);

	jWriter.writeByte(std::byte{0}); // spacer between Json and encoded block.

	std::byte working[8];
	if (size == 0) {
		D_PRINTLN("Read size 0");
	}
	for (size_t ix = 0; ix < size; ix += 7) {
		size_t pktSize = 7;
		if (ix + pktSize > size) {
			pktSize = size - ix;
		}
		std::byte hiBits{0};
		std::byte rotBit{1};
		for (size_t i = 1; i <= pktSize; ++i) {
			working[i] = std::byte((*srcAddr) & 0x7F);
			if ((*srcAddr) & 0x80) {
				hiBits |= rotBit;
			}
			srcAddr++;
			rotBit <<= 1;
		}
		working[0] = hiBits;
		jWriter.writeBlock({working, pktSize + 1});
	}
	sendMsg(cable, jWriter);
}

void smSysex::writeBlock(MIDICable& cable, JsonDeserializer& reader) {
	char const* tagName;
	uint32_t fileId = 0;
	uint32_t addr = 0;
	uint32_t size = blockBufferMax;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "addr")) {
			addr = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "size")) {
			size = reader.readTagOrAttributeValueInt();
			if (size > blockBufferMax)
				size = blockBufferMax;
		}
		else if (!strcmp(tagName, "fid")) {
			fileId = reader.readTagOrAttributeValueInt();
		}
		else {
			reader.exitTag();
		}
	}
	if (!writeBlockBuffer) {
		writeBlockBuffer = (uint8_t*)GeneralMemoryAllocator::get().allocLowSpeed(blockBufferMax);
	}
	reader.match('}');
	reader.match('}'); // skip box too.

	// We should be on the separator character, check to make
	char aChar;
	if (reader.peekChar(&aChar) && aChar != 0) {
		D_PRINTLN("Missing Separater error in writeBlock");
	}
	uint32_t decodedSize = decodeDataFromReader(reader, writeBlockBuffer, size);
	D_PRINTLN("Decoded block len: %d", decodedSize);

	// Here is where we actually write the buffer out.
	FRESULT errCode = FRESULT::FR_OK;
	FILdata* fp = entryForFID(fileId);

	if (fp == nullptr) {
		errCode = FRESULT::FR_NOT_ENABLED;
	}
	if (writeBlockBuffer && (fp != nullptr)) {
		if (addr != fp->fPosition) {
			errCode = f_lseek(&fp->file, addr);
		}
		if (errCode == FRESULT::FR_OK) {
			noteFileIdUse(fp);
			UINT actuallyWritten = 0;
			errCode = f_write(&fp->file, writeBlockBuffer, decodedSize, &actuallyWritten);
			size = actuallyWritten;
			fp->fPosition = addr + actuallyWritten;
		}
	}
	startReply(jWriter, reader);
	jWriter.writeOpeningTag("^write", false, true);
	jWriter.writeAttribute("fid", fileId);
	jWriter.writeAttribute("addr", addr);
	jWriter.writeAttribute("size", size);
	jWriter.writeAttribute("err", errCode);
	jWriter.closeTag(true);

	sendMsg(cable, jWriter);
}

void smSysex::updateTime(MIDICable& cable, JsonDeserializer& reader) {

	char const* tagName;
	uint32_t date = 0;
	uint32_t time = 0;
	std::string path;

	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "path")) {
			path = D_TRY_CATCH(reader.readTagOrAttributeValueString(), error, {
				return; // fail fast if we can't read the path.
			});
		}
		else if (!strcmp(tagName, "date")) {
			date = reader.readTagOrAttributeValueInt();
		}
		else if (!strcmp(tagName, "time")) {
			time = reader.readTagOrAttributeValueInt();
		}
		else {
			reader.exitTag();
		}
	}
	reader.match('}');

	FRESULT errCode;
	if (!path.empty() && (date != 0 || time != 0)) {
		FILINFO finfo;
		finfo.fdate = date;
		finfo.ftime = time;
		auto time = FatFS::utime(path, &finfo);
		errCode = time.has_value() ? FRESULT::FR_OK : static_cast<FRESULT>(time.error());
	}
	else {
		errCode = FRESULT::FR_INVALID_PARAMETER;
	}
	startReply(jWriter, reader);
	jWriter.writeOpeningTag("^utime", false, true);
	jWriter.writeAttribute("err", errCode);
	jWriter.closeTag(true);
	sendMsg(cable, jWriter);
}

// A session ID or sid is a number used by clients to keep track of which messages belong to who.
void smSysex::assignSession(MIDICable& cable, JsonDeserializer& reader) {
	char const* tagName;
	String tag;
	reader.match('{');
	while (*(tagName = reader.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "tag")) {
			tag = reader.readTagOrAttributeValueString().value_or("");
		}
		else {
			reader.exitTag();
		}
	}
	reader.match('}');

	int sessionNum = 0;
	uint32_t minSeen = session_mono_counter;
	for (int i = 1; i <= MAX_SYSEX_SESSIONS; ++i) {
		if (sessionLRU_array[i] == 0) {
			sessionNum = i;
			break;
		}
		if (sessionLRU_array[i] < minSeen) {
			minSeen = sessionLRU_array[i];
			sessionNum = i;
		}
	}
	// Note the sessionNum as MRU to claim it.
	sessionLRU_array[sessionNum] = session_mono_counter++;

	startDirect(jWriter);
	jWriter.writeOpeningTag("^session", false, true);
	jWriter.writeAttribute("sid", sessionNum);
	jWriter.writeAttribute("tag", tag.get());
	jWriter.writeAttribute("midBase", sessionNum << SYSEX_SESSION_SHIFT);
	jWriter.writeAttribute("midMin", (sessionNum << SYSEX_SESSION_SHIFT) + 1);
	jWriter.writeAttribute("midMax", (sessionNum << SYSEX_SESSION_SHIFT) + SYSEX_MSGID_MAX);
	jWriter.closeTag(true);
	sendMsg(cable, jWriter);
}

void smSysex::doPing(MIDICable& cable, JsonDeserializer& reader) {
	startReply(jWriter, reader);
	jWriter.writeOpeningTag("^ping", false, true);
	jWriter.closeTag(true);
	sendMsg(cable, jWriter);
}

uint32_t smSysex::decodeDataFromReader(JsonDeserializer& reader, uint8_t* dest, uint32_t destMax) {
	char zip = 0;
	if (!reader.readChar(&zip) || zip) // skip separator, fail if not there.
		return 0;
	uint32_t encodedSize = reader.bytesRemainingInBuffer() - 1; // don't count that 0xF7.
	uint32_t amount = unpack_7bit_to_8bit(dest, destMax, (uint8_t*)reader.GetCurrentAddressInBuffer(), encodedSize);
	return amount;
}

void smSysex::sysexReceived(MIDICable& cable, uint8_t* data, int32_t len) {
	if (len < 3) {
		return;
	}

	SysExDataEntry& de = SysExQ.emplace_back(cable, len);
	memcpy(de.data, data, len);
}

void smSysex::handleNextSysEx() {

	if (SysExQ.empty())
		return;
	if (currentlyAccessingCard != 0)
		return;

	SysExDataEntry& de = SysExQ.front();

	char const* tagName;
	uint8_t msgSeqNum = std::to_integer<uint8_t>(de.data[1]);
	noteSessionIdUse(msgSeqNum);
	JsonDeserializer parser({de.data + 2, de.len - 2});
	parser.setReplySeqNum(msgSeqNum);

	parser.match('{');
	while (*(tagName = parser.readNextTagOrAttributeName())) {
		if (!strcmp(tagName, "open")) {
			openFile(de.cable, parser);
			goto done;
		}
		else if (!strcmp(tagName, "close")) {
			closeFile(de.cable, parser);
			goto done;
		}
		else if (!strcmp(tagName, "dir")) {
			getDirEntries(de.cable, parser);
			goto done;
		}
		else if (!strcmp(tagName, "read")) {
			readBlock(de.cable, parser);
			goto done;
		}
		else if (!strcmp(tagName, "write")) {
			writeBlock(de.cable, parser);
			goto done; // Already skipped end.
		}
		else if (!strcmp(tagName, "delete")) {
			deleteFile(de.cable, parser);
			goto done;
		}
		else if (!strcmp(tagName, "mkdir")) {
			createDirectory(de.cable, parser);
			goto done;
		}
		else if (!strcmp(tagName, "rename")) {
			rename(de.cable, parser);
			goto done;
		}
		else if (!strcmp(tagName, "utime")) {
			updateTime(de.cable, parser);
			goto done;
		}
		else if (!strcmp(tagName, "session")) {
			assignSession(de.cable, parser);
			goto done;
		}
		else if (!strcmp(tagName, "ping")) {
			doPing(de.cable, parser);
			goto done;
		}
		parser.exitTag();
	}
done:
	SysExQ.pop_front();
}
