// SPDX-License-Identifier: MIT
// LoadPluginSnapshot.h - bounded, read-only snapshot of the engine's loaded
// full/light plugin tables for early load admission (issue #262).
//
// Layout constants are pinned to the SKSE 1.7.104 headers in this repository
// and are NOT autodetected:
//   DataHandler::modList          GameData.h   0xD60 (STATIC_ASSERT present)
//   ModList::loadedMods (full)    GameData.h   0xD60 + 0x10 = 0xD70
//   ModList second array (light)  GameData.h   0xD60 + 0x28 = 0xD88. The header
//                                 comment calls it "loadedCCMods, just
//                                 assuming"; this reader therefore verifies the
//                                 class of EVERY entry through fileFlags and
//                                 never trusts the table it came from.
//   tArray                        GameTypes.h  entries@0 capacity@8 count@0x10
//   ModInfo                       GameData.h   name@0x58[0x104] fileFlags@0x438
//                                              modIndex@0x478 lightIndex@0x47A
//   kFileFlags_Light = 1 << 9; inactive modIndex = 0xFF (GameData.h)
//
// Preconditions the CALLER must gate on before calling: exact pinned runtime
// 1.7.104 executable, DataLoaded already dispatched, non-null DataHandler
// singleton. Reads happen only through the caller-supplied bounded reader
// (bool(uintptr_t address, void* dest, size_t length), false = unreadable).
// Engine pointers are never dereferenced here, no engine class is copied or
// constructed, and no engine function or virtual is called. Assumes a
// little-endian host reading a 64-bit engine image.
//
// Failure model: every refusal throws SnapshotError; std::string/std::vector/
// std::set allocation may throw std::bad_alloc. The caller MUST catch all
// exceptions before control returns into engine frames.
//
// Name policy: bytes are preserved verbatim. Duplicate detection folds ASCII
// letters only. Case equivalence for non-ASCII names is UNRESOLVED; no claim
// of Windows or engine equivalence is made and no locale folding is used.
#ifndef SKSE64_LOAD_PLUGIN_SNAPSHOT_H
#define SKSE64_LOAD_PLUGIN_SNAPSHOT_H
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace LoadPluginSnapshot {

class SnapshotError : public std::runtime_error {
public:
	explicit SnapshotError(const std::string& what) : std::runtime_error(what) {}
};

struct Snapshot {
	std::vector<std::string> full;   // loaded full plugins in modIndex order
	std::vector<std::string> light;  // loaded light plugins in lightIndex order
};

static const std::uint64_t kModListOffset = 0xD60;
static const std::uint64_t kFullArrayOffset = 0xD70;
static const std::uint64_t kLightArrayOffset = 0xD88;
static const std::uint64_t kArrayEntriesOffset = 0x00;
static const std::uint64_t kArrayCapacityOffset = 0x08;
static const std::uint64_t kArrayCountOffset = 0x10;
static const std::uint64_t kEnginePointerSize = 8;
static const std::uint64_t kNameOffset = 0x58;
static const std::size_t kNameCapacity = 0x104;
static const std::uint64_t kFileFlagsOffset = 0x438;
static const std::uint64_t kModIndexOffset = 0x478;
static const std::uint64_t kLightIndexOffset = 0x47A;
static const std::uint32_t kFileFlagLight = 1u << 9;
static const std::uint8_t kInactiveModIndex = 0xFF;
static const std::uint32_t kMaxFull = 254;
static const std::uint32_t kMaxLight = 4096;
static_assert(kFullArrayOffset == kModListOffset + 0x10, "loadedMods is ModList+0x10");
static_assert(kLightArrayOffset == kModListOffset + 0x28, "light array is ModList+0x28");

namespace detail {

inline std::uintptr_t Offset(std::uintptr_t base, std::uint64_t offset)
{
	if (offset > (std::numeric_limits<std::uintptr_t>::max)() - base)
		throw SnapshotError("address arithmetic overflow");
	return base + static_cast<std::uintptr_t>(offset);
}

inline std::uintptr_t ToAddress(std::uint64_t value, const char* what)
{
	if (value == 0) throw SnapshotError(std::string("null ") + what);
	if (value > (std::numeric_limits<std::uintptr_t>::max)())
		throw SnapshotError(std::string("pointer exceeds host address space: ") + what);
	return static_cast<std::uintptr_t>(value);
}

template <class T, class Reader>
T ReadValue(Reader& read, std::uintptr_t address, const char* what)
{
	Offset(address, sizeof(T) - 1); // Refuse a wrapping range before invoking any reader.
	T value;
	if (!read(address, &value, sizeof(value)))
		throw SnapshotError(std::string("unreadable ") + what);
	return value;
}

inline std::string FoldAscii(const std::string& name)
{
	std::string key = name;
	for (std::size_t i = 0; i < key.size(); ++i)
		if (key[i] >= 'A' && key[i] <= 'Z') key[i] = static_cast<char>(key[i] - 'A' + 'a');
	return key;
}

inline void ValidateName(const std::string& name)
{
	if (name.empty() || name == "." || name == "..") throw SnapshotError("invalid file name");
	for (std::size_t i = 0; i < name.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(name[i]);
		if (c < 0x20 || c == 0x7F) throw SnapshotError("control character in file name");
		if (std::strchr("\\/:*?\"<>|", static_cast<char>(c)) && c != 0)
			throw SnapshotError("path character in file name");
	}
}

template <class Reader>
void ReadTable(Reader& read, std::uintptr_t array, bool light,
	std::vector<std::string>& out, std::set<std::string>& seen)
{
	const char* table = light ? "light table" : "full table";
	const std::uint64_t entries = ReadValue<std::uint64_t>(read, Offset(array, kArrayEntriesOffset), table);
	const std::uint32_t capacity = ReadValue<std::uint32_t>(read, Offset(array, kArrayCapacityOffset), table);
	const std::uint32_t count = ReadValue<std::uint32_t>(read, Offset(array, kArrayCountOffset), table);
	if (count > capacity) throw SnapshotError(std::string("count exceeds capacity in ") + table);
	if (count > (light ? kMaxLight : kMaxFull)) throw SnapshotError(std::string("count exceeds pinned maximum in ") + table);
	if (count == 0) return;
	const std::uintptr_t base = ToAddress(entries, table);
	out.reserve(count);
	for (std::uint32_t i = 0; i < count; ++i) {
		const std::uintptr_t slot = Offset(base, std::uint64_t(i) * kEnginePointerSize);
		const std::uintptr_t file = ToAddress(ReadValue<std::uint64_t>(read, slot, "file pointer"), "file pointer");
		char raw[kNameCapacity];
		Offset(file, kNameOffset + kNameCapacity - 1);
		if (!read(Offset(file, kNameOffset), raw, sizeof(raw))) throw SnapshotError("unreadable file name");
		const void* end = std::memchr(raw, 0, sizeof(raw));
		if (!end) throw SnapshotError("unterminated file name");
		const std::string name(raw, static_cast<std::size_t>(static_cast<const char*>(end) - raw));
		ValidateName(name);
		const std::uint32_t flags = ReadValue<std::uint32_t>(read, Offset(file, kFileFlagsOffset), "file flags");
		const std::uint8_t modIndex = ReadValue<std::uint8_t>(read, Offset(file, kModIndexOffset), "mod index");
		const std::uint16_t lightIndex = ReadValue<std::uint16_t>(read, Offset(file, kLightIndexOffset), "light index");
		if (modIndex == kInactiveModIndex) throw SnapshotError("inactive file in loaded table: " + name);
		if (light && modIndex != 0xFE) throw SnapshotError("light file lacks FE index: " + name);
		if (((flags & kFileFlagLight) != 0) != light) throw SnapshotError("file class does not match table: " + name);
		if (light ? (lightIndex != i) : (modIndex != i)) throw SnapshotError("non-sequential index: " + name);
		if (!seen.insert(FoldAscii(name)).second) throw SnapshotError("duplicate file name: " + name);
		out.push_back(name);
	}
}

} // namespace detail

// Reads both loaded tables. Zero loaded full plugins is refused: the engine
// always has at least Skyrim.esm, so an empty table means a wrong layout.
template <class Reader>
Snapshot Read(Reader& read, std::uintptr_t dataHandler)
{
	if (!dataHandler) throw SnapshotError("null DataHandler");
	Snapshot snapshot;
	std::set<std::string> seen;
	detail::ReadTable(read, detail::Offset(dataHandler, kFullArrayOffset), false, snapshot.full, seen);
	detail::ReadTable(read, detail::Offset(dataHandler, kLightArrayOffset), true, snapshot.light, seen);
	if (snapshot.full.empty()) throw SnapshotError("no loaded full plugins");
	return snapshot;
}

} // namespace LoadPluginSnapshot

#endif
