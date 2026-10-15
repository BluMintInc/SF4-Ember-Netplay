#pragma once

// The game's raw keyboard input (Dimps::Pad::System_RawInput, 0xCBC1E8) has
// state for eight keyboards inside the object, but its device scan (0x6E0150)
// adds a keyboard for every raw input keyboard Windows lists, plus one more for
// the first, with no limit. Gaming keyboards, mice with macro keys and wireless
// receivers each list several, so eight is easy to pass. The update (0x6DFA00)
// then keeps a ninth keyboard's key-repeat counters over the object's own
// lists: pressing L or D broke them and the game crashed or quit. This keeps
// the lists to the keyboards the object has room for, apart from the game so
// it can be tested; sf4e__Pad.cxx calls it after each scan.
#include <cstddef>
#include <cstdint>

namespace sf4e { namespace input {

static_assert(sizeof(std::uintptr_t) == 4, "the game's vectors hold 32-bit pointers");

// Where the scan leaves what the update reads, as offsets into the object.
struct KeyboardSlotLayout {
	static constexpr unsigned Capacity = 8;           // the constructor (0x6DFD60) sets up eight
	static constexpr std::size_t DeviceCount = 0x34;  // keyboards the pad system sees
	static constexpr std::size_t Devices = 0x2974;    // vector of 828-byte device records
	static constexpr std::size_t DeviceSize = 828;
	static constexpr std::size_t Keys = 0x2994;       // vector of one dword per keyboard
	static constexpr std::size_t MultipleKeyboards = 0x29A5; // SYSTEM/MultipleKeyboards is ON
};

struct KeyboardTrim {
	unsigned found = 0;   // keyboards the scan listed
	unsigned dropped = 0; // of those, how many past the object's room
};

// Ends both vectors after the first Capacity keyboards. Their elements are
// plain data the game copies with memcpy, so nothing needs freeing; the
// vectors keep their storage for the next scan. With several keyboards the
// pad system's count is the number listed, and it follows. A list that does
// not read as a vector of whole records is left alone and counts as found 0.
inline KeyboardTrim TrimKeyboardSlots(std::uint8_t* object) {
	using L = KeyboardSlotLayout;
	auto field = [object](std::size_t offset) { return reinterpret_cast<std::uintptr_t*>(object + offset); };
	KeyboardTrim trim;
	std::uintptr_t* devices = field(L::Devices);
	const std::uintptr_t begin = devices[0], end = devices[1];
	if (begin == 0 || end < begin || (end - begin) % L::DeviceSize != 0) return trim;
	trim.found = static_cast<unsigned>((end - begin) / L::DeviceSize);
	if (trim.found <= L::Capacity) return trim;
	trim.dropped = trim.found - L::Capacity;
	devices[1] = begin + L::Capacity * L::DeviceSize;
	std::uintptr_t* keys = field(L::Keys);
	const std::size_t keyBytes = L::Capacity * sizeof(std::uint32_t);
	if (keys[0] != 0 && keys[1] > keys[0] + keyBytes) keys[1] = keys[0] + keyBytes;
	std::int32_t* count = reinterpret_cast<std::int32_t*>(object + L::DeviceCount);
	if (object[L::MultipleKeyboards] != 0 && *count > static_cast<std::int32_t>(L::Capacity))
		*count = static_cast<std::int32_t>(L::Capacity);
	return trim;
}

} }
