#pragma once
#include "Countries.hxx"
#include <string>

namespace sf4e {

// A country of the table (Countries.hxx), as the room carries it: its
// upper-case ISO 3166-1 alpha-2 code. Its index is its flag's atlas cell and its
// catalog key names it. A default CountryCode is none.
class CountryCode {
public:
	CountryCode() : index_(-1) {}
	bool Present() const { return index_ >= 0; }
	int Index() const { return index_; }
	std::string ToString() const { return Present() ? std::string(countries::Table[index_].code) : std::string(); }
	// The catalog key of its name, or null for none.
	const char* Key() const { return Present() ? countries::Table[index_].key : nullptr; }

	// Exactly two upper-case letters that name a row of the table. `out` is left
	// alone when it is refused.
	static bool Parse(const std::string& text, CountryCode& out) {
		if (text.size() != 2 || text[0] < 'A' || text[0] > 'Z' || text[1] < 'A' || text[1] > 'Z') return false;
		int low = 0, high = countries::Count - 1;
		while (low <= high) {
			const int middle = (low + high) / 2;
			const int order = Compare(text, countries::Table[middle].code);
			if (!order) { out.index_ = middle; return true; }
			if (order < 0) high = middle - 1; else low = middle + 1;
		}
		return false;
	}
	// None for text that is not a country of the table.
	static CountryCode Parsed(const std::string& text) {
		CountryCode code;
		if (!Parse(text, code)) code = CountryCode();
		return code;
	}
	static CountryCode AtIndex(int index) {
		CountryCode code;
		if (index >= 0 && index < countries::Count) code.index_ = index;
		return code;
	}
	bool operator==(const CountryCode& rhs) const { return index_ == rhs.index_; }
	bool operator!=(const CountryCode& rhs) const { return index_ != rhs.index_; }

private:
	static int Compare(const std::string& text, const char* code) {
		return text[0] != code[0] ? text[0] - code[0] : text[1] - code[1];
	}
	int index_;
};

// The player's choice of country: Automatic follows Windows' region, Code is
// one they picked, Hidden shows none. Saved as "auto", "hidden" or the code.
struct CountryPreference {
	enum class Kind { Automatic, Code, Hidden };
	Kind kind = Kind::Automatic;
	CountryCode code;
	static CountryPreference Automatic() { return {}; }
	static CountryPreference Hidden() { CountryPreference p; p.kind = Kind::Hidden; return p; }
	static CountryPreference Of(const CountryCode& code) {
		CountryPreference p;
		if (code.Present()) { p.kind = Kind::Code; p.code = code; }
		return p;
	}
	bool Valid() const { return kind == Kind::Code ? code.Present() : !code.Present(); }
	std::string ToString() const { return kind == Kind::Code ? code.ToString() : kind == Kind::Hidden ? "hidden" : "auto"; }
	// Saved text that is none of the three reads as Automatic.
	static CountryPreference Parsed(const std::string& text) {
		if (text == "hidden") return Hidden();
		return Of(CountryCode::Parsed(text));
	}
	bool operator==(const CountryPreference& rhs) const { return kind == rhs.kind && code == rhs.code; }
	bool operator!=(const CountryPreference& rhs) const { return !(*this == rhs); }
};

// The country a player shows: Automatic takes Windows' (`automatic`, none when
// Windows names none the table has), a pick its code, Hidden none.
inline CountryCode ShownCountry(const CountryPreference& preference, const CountryCode& automatic) {
	switch (preference.kind) {
	case CountryPreference::Kind::Code: return preference.code;
	case CountryPreference::Kind::Hidden: return CountryCode();
	default: return automatic;
	}
}

// Windows' answer for the user's location: the GEO_ISO2 text of their nation, or
// empty when Windows has none. Only a two-letter code the table has becomes a
// country; "XK" (Kosovo, which Windows reports), a numeric region or anything
// else is none.
inline CountryCode CountryFromGeoIso2(std::wstring text) {
	if (text.size() != 2) return CountryCode();
	std::string code;
	for (const wchar_t c : text) {
		const wchar_t upper = c >= L'a' && c <= L'z' ? static_cast<wchar_t>(c - L'a' + L'A') : c;
		if (upper < L'A' || upper > L'Z') return CountryCode();
		code += static_cast<char>(upper);
	}
	return CountryCode::Parsed(code);
}

}
