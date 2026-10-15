// The game's raw keyboard lists kept to the eight keyboards its object has
// room for (common/KeyboardSlots.hxx), on a stand-in for the object.
#include "../common/KeyboardSlots.hxx"
#include <cstdint>
#include <cstring>
#include <vector>

#include "test_support.hxx"

using namespace sf4e::input;
using L = KeyboardSlotLayout;

namespace {
// The object's fields the scan leaves, over storage this test owns.
struct Keyboards {
	std::vector<std::uint8_t> object = std::vector<std::uint8_t>(0x29A8, 0);
	std::vector<std::uint8_t> devices;
	std::vector<std::uint32_t> keys;

	Keyboards(unsigned listed, bool multiple) : devices(listed * L::DeviceSize + 4 * L::DeviceSize), keys(listed + 4) {
		Set(L::Devices, reinterpret_cast<std::uintptr_t>(devices.data()));
		Set(L::Devices + 4, reinterpret_cast<std::uintptr_t>(devices.data() + listed * L::DeviceSize));
		Set(L::Devices + 8, reinterpret_cast<std::uintptr_t>(devices.data() + devices.size()));
		Set(L::Keys, reinterpret_cast<std::uintptr_t>(keys.data()));
		Set(L::Keys + 4, reinterpret_cast<std::uintptr_t>(keys.data() + listed));
		Set(L::Keys + 8, reinterpret_cast<std::uintptr_t>(keys.data() + keys.size()));
		object[L::MultipleKeyboards] = multiple ? 1 : 0;
		Set(L::DeviceCount, multiple ? listed : 2);
	}
	void Set(std::size_t offset, std::uintptr_t value) { std::memcpy(object.data() + offset, &value, 4); }
	std::uintptr_t Get(std::size_t offset) const { std::uintptr_t v; std::memcpy(&v, object.data() + offset, 4); return v; }
	unsigned DeviceCount() const { return static_cast<unsigned>((Get(L::Devices + 4) - Get(L::Devices)) / L::DeviceSize); }
	unsigned KeyCount() const { return static_cast<unsigned>((Get(L::Keys + 4) - Get(L::Keys)) / 4); }
	KeyboardTrim Trim() { return TrimKeyboardSlots(object.data()); }
};
}

int main() {
	// Up to eight, nothing changes.
	for (unsigned listed : {0u, 1u, 8u}) {
		Keyboards k(listed, true);
		const auto before = k.object;
		const auto trim = k.Trim();
		CHECK(trim.found == listed && trim.dropped == 0);
		CHECK(k.object == before);
	}
	// Nine (eight keyboards and the first one's extra entry) keep eight; the
	// storage and the capacity stay where they were.
	{
		Keyboards k(9, true);
		const auto begin = k.Get(L::Devices), capacity = k.Get(L::Devices + 8), keysCapacity = k.Get(L::Keys + 8);
		const auto trim = k.Trim();
		CHECK(trim.found == 9 && trim.dropped == 1);
		CHECK(k.DeviceCount() == 8 && k.KeyCount() == 8);
		CHECK(k.Get(L::Devices) == begin && k.Get(L::Devices + 8) == capacity && k.Get(L::Keys + 8) == keysCapacity);
		CHECK(k.Get(L::DeviceCount) == 8);
		// A second scan's trim finds the eight it left.
		CHECK(k.Trim().found == 8 && k.Trim().dropped == 0);
	}
	// With several keyboards off the pad system counts the keyboard as one
	// device (2); that count is the game's and stays.
	{
		Keyboards k(12, false);
		const auto trim = k.Trim();
		CHECK(trim.found == 12 && trim.dropped == 4);
		CHECK(k.DeviceCount() == 8 && k.KeyCount() == 8);
		CHECK(k.Get(L::DeviceCount) == 2);
	}
	// A key list already shorter than eight is left as it is.
	{
		Keyboards k(10, true);
		k.Set(L::Keys + 4, k.Get(L::Keys) + 3 * 4);
		k.Trim();
		CHECK(k.DeviceCount() == 8 && k.KeyCount() == 3);
	}
	// A list that is not whole records is not touched.
	{
		Keyboards k(10, true);
		k.Set(L::Devices + 4, k.Get(L::Devices + 4) + 1);
		const auto before = k.object;
		CHECK(k.Trim().found == 0);
		CHECK(k.object == before);
	}
	{
		Keyboards k(10, true);
		k.Set(L::Devices, 0);
		const auto before = k.object;
		CHECK(k.Trim().found == 0);
		CHECK(k.object == before);
	}
	std::puts("keyboard slots: ok");
	return 0;
}
