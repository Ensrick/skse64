// SPDX-License-Identifier: MIT
// Synthetic address-space regression for skse64/LoadPluginSnapshot.h.
// No game process, game file, installed mod or SKSE runtime is touched.
#include "LoadPluginSnapshot.h"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace LPS = LoadPluginSnapshot;

// Simulated engine memory: reads succeed only when the whole range lies in
// one mapped fixture region. Addresses are fake and never dereferenced.
struct Space {
	std::map<std::uintptr_t, std::vector<unsigned char> > regions;
	unsigned reads;
	Space() : reads(0) {}
	void mapRegion(std::uintptr_t at, std::size_t size) { regions[at] = std::vector<unsigned char>(size, 0); }
	unsigned char* locate(std::uintptr_t address, std::size_t length)
	{
		for (std::map<std::uintptr_t, std::vector<unsigned char> >::iterator it = regions.begin(); it != regions.end(); ++it)
			if (address >= it->first && length <= it->second.size() && address - it->first <= it->second.size() - length)
				return &it->second[address - it->first];
		return 0;
	}
	bool operator()(std::uintptr_t address, void* dest, std::size_t length)
	{
		const unsigned char* source = locate(address, length);
		if (!source) return false;
		std::memcpy(dest, source, length);
		++reads;
		return true;
	}
	template <class T> void put(std::uintptr_t at, T value)
	{
		unsigned char* target = locate(at, sizeof(T));
		if (!target) throw std::logic_error("fixture write outside mapped region");
		std::memcpy(target, &value, sizeof(T));
	}
	void putName(std::uintptr_t file, const std::string& name)
	{
		unsigned char* target = locate(file + LPS::kNameOffset, LPS::kNameCapacity);
		if (!target) throw std::logic_error("fixture name outside mapped region");
		std::memset(target, 0, LPS::kNameCapacity);
		std::memcpy(target, name.data(), name.size() < LPS::kNameCapacity ? name.size() : LPS::kNameCapacity);
	}
};

const std::uintptr_t kHandler = 0x10000, kFullEntries = 0x20000, kLightEntries = 0x30000, kFiles = 0x100000;
const std::uintptr_t kFileStride = 0x1000, kFileSize = 0x4C8;

std::uintptr_t FileAt(std::size_t index) { return kFiles + index * kFileStride; }

// Consistent fixture: full files get modIndex 0..n-1, light files get
// lightIndex 0..m-1 with the light flag; light files get modIndex 0xFE as a
// required shared FE full-index marker.
Space Build(const std::vector<std::string>& full, const std::vector<std::string>& light)
{
	Space s;
	s.mapRegion(kHandler, 0xDC0);
	s.mapRegion(kFullEntries, 8 * 256);
	s.mapRegion(kLightEntries, 8 * 4200);
	std::size_t next = 0;
	s.put<std::uint64_t>(kHandler + LPS::kFullArrayOffset, full.empty() ? 0 : kFullEntries);
	s.put<std::uint32_t>(kHandler + LPS::kFullArrayOffset + 8, 256);
	s.put<std::uint32_t>(kHandler + LPS::kFullArrayOffset + 0x10, static_cast<std::uint32_t>(full.size()));
	for (std::size_t i = 0; i < full.size(); ++i, ++next) {
		s.mapRegion(FileAt(next), kFileSize);
		s.put<std::uint64_t>(kFullEntries + 8 * i, FileAt(next));
		s.putName(FileAt(next), full[i]);
		s.put<std::uint32_t>(FileAt(next) + LPS::kFileFlagsOffset, 0x800);
		s.put<std::uint8_t>(FileAt(next) + LPS::kModIndexOffset, static_cast<std::uint8_t>(i));
		s.put<std::uint16_t>(FileAt(next) + LPS::kLightIndexOffset, 0);
	}
	s.put<std::uint64_t>(kHandler + LPS::kLightArrayOffset, light.empty() ? 0 : kLightEntries);
	s.put<std::uint32_t>(kHandler + LPS::kLightArrayOffset + 8, 4200);
	s.put<std::uint32_t>(kHandler + LPS::kLightArrayOffset + 0x10, static_cast<std::uint32_t>(light.size()));
	for (std::size_t i = 0; i < light.size(); ++i, ++next) {
		s.mapRegion(FileAt(next), kFileSize);
		s.put<std::uint64_t>(kLightEntries + 8 * i, FileAt(next));
		s.putName(FileAt(next), light[i]);
		s.put<std::uint32_t>(FileAt(next) + LPS::kFileFlagsOffset, 0x800 | LPS::kFileFlagLight);
		s.put<std::uint8_t>(FileAt(next) + LPS::kModIndexOffset, 0xFE);
		s.put<std::uint16_t>(FileAt(next) + LPS::kLightIndexOffset, static_cast<std::uint16_t>(i));
	}
	return s;
}

unsigned checks = 0;
void require(bool ok, const char* what)
{
	++checks;
	if (!ok) throw std::runtime_error(std::string("check failed: ") + what);
}

// Every refusal must be the typed error, never a raw crash or std::logic_error.
void expectRefusal(Space& s, std::uintptr_t handler, const char* what)
{
	Space before = s;
	bool refused = false;
	try { LPS::Read(s, handler); } catch (const LPS::SnapshotError&) { refused = true; }
	require(refused, what);
	require(before.regions == s.regions, "refusal must not write fixture memory");
}

std::vector<std::string> Names(const char* a = 0, const char* b = 0, const char* c = 0)
{
	std::vector<std::string> out;
	if (a) out.push_back(a);
	if (b) out.push_back(b);
	if (c) out.push_back(c);
	return out;
}

int main()
{
	try {
		{
			bool called = false, refused = false;
			auto permissiveReader = [&](std::uintptr_t, void*, std::size_t) { called = true; return true; };
			try { LPS::detail::ReadValue<std::uint64_t>(permissiveReader, std::numeric_limits<std::uintptr_t>::max() - 3, "overflow fixture"); }
			catch (const LPS::SnapshotError&) { refused = true; }
			require(refused && !called, "wrapping read refused before callback");
		}
		{ Space s = Build(Names("Skyrim.esm"), Names("A.esl")); s.put<std::uint8_t>(FileAt(1) + LPS::kModIndexOffset, 0); expectRefusal(s, kHandler, "light file must have FE marker"); }
		{ Space s = Build(Names(".."), Names()); expectRefusal(s, kHandler, "parent directory name refused"); }
		// Reader itself fails safely outside mapped fixtures and at boundaries.
		{
			Space s = Build(Names("Skyrim.esm"), Names());
			char byte = 0;
			require(!s(0x5, &byte, 1), "unmapped address refused");
			require(!s(kHandler + 0xDC0 - 1, &byte, 2), "range crossing region end refused");
			require(s(kHandler + 0xDC0 - 1, &byte, 1), "last byte readable");
		}
		// Happy path with a non-ASCII name preserved verbatim and zero light files.
		{
			Space s = Build(Names("Skyrim.esm", "Update.esm", "Br\xC3\xBCma.esp"), Names());
			Space before = s;
			LPS::Snapshot one = LPS::Read(s, kHandler);
			require(one.full.size() == 3 && one.light.empty(), "three full, zero light");
			require(one.full[0] == "Skyrim.esm" && one.full[1] == "Update.esm" && one.full[2] == "Br\xC3\xBCma.esp", "index order and bytes preserved");
			LPS::Snapshot two = LPS::Read(s, kHandler);
			require(one.full == two.full && one.light == two.light, "repeat output stable");
			require(before.regions == s.regions, "no input writes on success");
			require(s.reads > 0, "reads went through the callback");
		}
		// Happy path with light files and the pinned maxima exactly.
		{
			std::vector<std::string> full, light;
			for (unsigned i = 0; i < LPS::kMaxFull; ++i) full.push_back("full" + std::to_string(i) + ".esp");
			for (unsigned i = 0; i < LPS::kMaxLight; ++i) light.push_back("light" + std::to_string(i) + ".esl");
			Space s = Build(full, light);
			LPS::Snapshot got = LPS::Read(s, kHandler);
			require(got.full.size() == LPS::kMaxFull && got.light.size() == LPS::kMaxLight, "maximum counts admitted");
			require(got.light[4095] == "light4095.esl" && got.full[253] == "full253.esp", "last entries in order");
		}
		// Handler and table metadata refusals.
		{
			Space s = Build(Names("Skyrim.esm"), Names("A.esl"));
			expectRefusal(s, 0, "null DataHandler");
			expectRefusal(s, std::numeric_limits<std::uintptr_t>::max() - 0x100, "handler offset overflow");
			expectRefusal(s, kHandler + 0x8000, "unreadable handler tables");
		}
		{ Space s = Build(Names(), Names("A.esl")); expectRefusal(s, kHandler, "zero full plugins refused"); }
		{ Space s = Build(Names("Skyrim.esm"), Names("A.esl")); s.put<std::uint32_t>(kHandler + LPS::kFullArrayOffset + 8, 0); expectRefusal(s, kHandler, "full count above capacity"); }
		{ Space s = Build(Names("Skyrim.esm"), Names("A.esl")); s.put<std::uint32_t>(kHandler + LPS::kLightArrayOffset + 0x10, 0); s.put<std::uint32_t>(kHandler + LPS::kLightArrayOffset + 8, 0); s.put<std::uint32_t>(kHandler + LPS::kLightArrayOffset + 0x10, 1); expectRefusal(s, kHandler, "light count above capacity"); }
		{ Space s = Build(Names("Skyrim.esm"), Names()); s.put<std::uint32_t>(kHandler + LPS::kFullArrayOffset + 0x10, LPS::kMaxFull + 1); expectRefusal(s, kHandler, "full count above pinned maximum"); }
		{ Space s = Build(Names("Skyrim.esm"), Names()); s.put<std::uint32_t>(kHandler + LPS::kLightArrayOffset + 0x10, LPS::kMaxLight + 1); expectRefusal(s, kHandler, "light count above pinned maximum"); }
		{ Space s = Build(Names("Skyrim.esm"), Names()); s.put<std::uint64_t>(kHandler + LPS::kFullArrayOffset, 0); expectRefusal(s, kHandler, "null entries with nonzero count"); }
		{ Space s = Build(Names("Skyrim.esm"), Names()); s.put<std::uint64_t>(kHandler + LPS::kFullArrayOffset, 0x900000); expectRefusal(s, kHandler, "unmapped entries array"); }
		{ Space s = Build(Names("Skyrim.esm", "Update.esm"), Names()); s.put<std::uint64_t>(kHandler + LPS::kFullArrayOffset, std::numeric_limits<std::uint64_t>::max() - 8); expectRefusal(s, kHandler, "entries slot arithmetic overflow or host range"); }
		// File node refusals.
		{ Space s = Build(Names("Skyrim.esm", "Update.esm"), Names()); s.put<std::uint64_t>(kFullEntries + 8, 0); expectRefusal(s, kHandler, "null file pointer"); }
		{ Space s = Build(Names("Skyrim.esm", "Update.esm"), Names()); s.put<std::uint64_t>(kFullEntries + 8, 0x900000); expectRefusal(s, kHandler, "unmapped file node"); }
		{ Space s = Build(Names("Skyrim.esm"), Names()); s.regions[FileAt(0)].resize(LPS::kNameOffset + 16); expectRefusal(s, kHandler, "truncated file node"); }
		{ Space s = Build(Names("Skyrim.esm"), Names()); std::memset(&s.regions[FileAt(0)][LPS::kNameOffset], 'a', LPS::kNameCapacity); expectRefusal(s, kHandler, "unterminated name"); }
		{ Space s = Build(Names("Skyrim.esm"), Names()); s.putName(FileAt(0), ""); expectRefusal(s, kHandler, "empty name"); }
		{ Space s = Build(Names("Skyrim.esm"), Names()); s.putName(FileAt(0), std::string("Sky\x01rim.esm")); expectRefusal(s, kHandler, "control character"); }
		{ Space s = Build(Names("Skyrim.esm"), Names()); s.putName(FileAt(0), "Data\\Skyrim.esm"); expectRefusal(s, kHandler, "path separator"); }
		{ Space s = Build(Names("Skyrim.esm"), Names()); s.putName(FileAt(0), "Sky:rim.esm"); expectRefusal(s, kHandler, "drive separator"); }
		{ Space s = Build(Names("Skyrim.esm"), Names()); s.put<std::uint8_t>(FileAt(0) + LPS::kModIndexOffset, LPS::kInactiveModIndex); expectRefusal(s, kHandler, "inactive full file"); }
		{ Space s = Build(Names("Skyrim.esm"), Names("A.esl")); s.put<std::uint8_t>(FileAt(1) + LPS::kModIndexOffset, LPS::kInactiveModIndex); expectRefusal(s, kHandler, "inactive light file"); }
		{ Space s = Build(Names("Skyrim.esm", "Update.esm"), Names()); s.put<std::uint32_t>(FileAt(1) + LPS::kFileFlagsOffset, 0x800 | LPS::kFileFlagLight); expectRefusal(s, kHandler, "light flag in full table"); }
		{ Space s = Build(Names("Skyrim.esm"), Names("A.esl")); s.put<std::uint32_t>(FileAt(1) + LPS::kFileFlagsOffset, 0x800); expectRefusal(s, kHandler, "missing light flag in light table"); }
		{ Space s = Build(Names("Skyrim.esm", "Update.esm"), Names()); s.put<std::uint8_t>(FileAt(1) + LPS::kModIndexOffset, 5); expectRefusal(s, kHandler, "non-sequential modIndex"); }
		{ Space s = Build(Names("Skyrim.esm"), Names("A.esl", "B.esl")); s.put<std::uint16_t>(FileAt(2) + LPS::kLightIndexOffset, 7); expectRefusal(s, kHandler, "non-sequential lightIndex"); }
		// Duplicate detection: ASCII case only, within and across tables.
		{ Space s = Build(Names("Skyrim.esm", "SKYRIM.ESM"), Names()); expectRefusal(s, kHandler, "ASCII-case duplicate within full"); }
		{ Space s = Build(Names("Skyrim.esm"), Names("a.esl", "A.ESL")); expectRefusal(s, kHandler, "ASCII-case duplicate within light"); }
		{ Space s = Build(Names("Skyrim.esm", "Update.esm"), Names("UPDATE.ESM")); expectRefusal(s, kHandler, "ASCII-case duplicate across tables"); }
		{
			// Non-ASCII names differing only by non-ASCII case are NOT folded: policy unresolved, both admitted.
			Space s = Build(Names("Skyrim.esm", "Br\xC3\xBCma.esp", "Br\xC3\x9Cma.esp"), Names());
			LPS::Snapshot got = LPS::Read(s, kHandler);
			require(got.full.size() == 3, "non-ASCII case is not claimed equivalent");
		}
	} catch (const std::exception& error) {
		std::cerr << "FAILED after " << checks << " checks: " << error.what() << "\n";
		return 1;
	}
	std::cout << checks << " plugin snapshot checks passed\n";
	return 0;
}
