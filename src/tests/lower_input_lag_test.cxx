// Lower input lag (issue #77): the edit to the battle's input store, put in
// place and back as often as asked; each fighter's choice at Ready; and the
// match's flag, on only when both chose it, as every member's native
// projection carries it.
#include "../common/InputLagEdit.hxx"
#include "../session/sf4e__SessionServer.hxx"
#include <array>
#include <cstdint>
#include <cstring>
#include <string>

#include "test_support.hxx"
#include "server_transport_support.hxx"

using namespace sf4e;
namespace protocol = sf4e::SessionProtocol;
using nlohmann::json;

namespace {
// The game's bytes around 0x5CFBB6: `mov ecx, [edi]`, then `mov edx, [esi+2Ch]`, then `push -1`.
struct Code {
	std::uint8_t bytes[7] = { 0x8B, 0x0F, 0x8B, 0x56, 0x2C, 0x6A, 0xFF };
};
const std::uint8_t kNative[7] = { 0x8B, 0x0F, 0x8B, 0x56, 0x2C, 0x6A, 0xFF };
const std::uint8_t kLowerLag[7] = { 0x8B, 0x0F, 0x8B, 0x56, 0x28, 0x6A, 0xFF };

// Refuses every unlock when `refuse` is set; counts writes and what stays writable.
struct Pages {
	bool refuse = false;
	int unlocks = 0, open = 0;
	bool Unlock(std::uint8_t*, std::uint8_t, unsigned long& saved) {
		if (refuse) return false;
		++unlocks; ++open;
		saved = 0x20; // PAGE_EXECUTE_READ
		return true;
	}
	void Lock(std::uint8_t*, std::uint8_t, unsigned long saved) {
		CHECK(saved == 0x20);
		--open;
	}
};

void TestStoreEdit() {
	Code code;
	const auto edit = input_lag::StoreAtReadFrame(code.bytes + 2);
	Pages pages;
	// On, on again (no write), off, off again (no write).
	CHECK(code_edit::Set(edit, true, pages) && std::memcmp(code.bytes, kLowerLag, 7) == 0 && pages.unlocks == 1);
	CHECK(code_edit::Set(edit, true, pages) && pages.unlocks == 1);
	CHECK(code_edit::Set(edit, false, pages) && std::memcmp(code.bytes, kNative, 7) == 0 && pages.unlocks == 2);
	CHECK(code_edit::Set(edit, false, pages) && pages.unlocks == 2 && pages.open == 0);
	// Already in the state asked for, nothing needs to be writable.
	Pages refusing; refusing.refuse = true;
	CHECK(code_edit::Set(edit, false, refusing));
	// A place that cannot be made writable stays as it was.
	CHECK(!code_edit::Set(edit, true, refusing) && std::memcmp(code.bytes, kNative, 7) == 0);
	// Bytes of another game build are left alone either way.
	Code other;
	other.bytes[4] = 0x30;
	const auto foreign = input_lag::StoreAtReadFrame(other.bytes + 2);
	Pages untouched;
	CHECK(!code_edit::Set(foreign, true, untouched) && !code_edit::Set(foreign, false, untouched));
	CHECK(untouched.unlocks == 0 && other.bytes[4] == 0x30);
}

// A custom room of `members` admitted members on connections 1 to `members`.
struct Room {
	MockTransport* transport = new MockTransport();
	SessionServer server{"lower-lag", "build", true, 3, {0, 99}, std::unique_ptr<session::ServerTransport>(transport)};
	std::uint64_t actionId = 0;
	explicit Room(int members) {
		// Both fighters readying starts the match, which needs each member's endpoint.
		std::array<std::uint8_t, 16> authorization = {};
		authorization[0] = 77;
		server.EnableMatchAuthorization(authorization, [](session::Connection connection) {
			std::string identity(64, '0');
			identity[63] = "0123456789abcdef"[static_cast<std::size_t>(connection) & 15];
			return identity;
		});
		server.EnableCustomRooms("Lower lag", 8, 77);
		for (session::Connection connection = 1; connection <= static_cast<session::Connection>(members); ++connection) {
			protocol::SessionJoinRequest join;
			join.username = "Member-" + std::to_string(connection); join.sidecarHash = "build"; join.port = 30000;
			join.customRooms = true; join.roomProtocol = room::ProtocolVersion;
			protocol::SessionHelloMsg hello; hello.admission = json(join);
			Send(connection, json(hello));
		}
		server.AdvanceCustomRoom(1000);
	}
	void Send(session::Connection connection, const json& message) {
		transport->Push(connection, message);
		CHECK(server.Step() == 0);
	}
	void Act(session::Connection connection, room::ActionKind kind, bool lowerLag = false) {
		const auto snapshot = *server.RoomSnapshot();
		room::Action action;
		action.kind = kind; action.roomEpoch = snapshot.roomEpoch; action.revision = snapshot.revision;
		action.tableRevision = snapshot.tables[0].revision; action.actionId = ++actionId; action.lowerLag = lowerLag;
		protocol::RoomActionMessage message; message.action = action;
		Send(connection, json(message));
	}
	// Three members at table 0, the third queued behind the two fighters, who
	// have chosen their fighters, P1's stage and seed.
	void Seat() {
		for (session::Connection connection = 1; connection <= 3; ++connection) Act(connection, room::ActionKind::Queue);
		for (session::Connection connection = 1; connection <= 2; ++connection) {
			protocol::PreBattleSetChara pick; pick.chara = protocol::MatchData{}.chara[0];
			pick.chara.charaID = connection == 1 ? 5 : 6;
			Send(connection, json(pick));
		}
		protocol::PreBattleSetStage stage; stage.stageID = selection::StageList().back().id;
		Send(1, json(stage));
		protocol::PreBattleSetEnv env; env.rngSeed = 1234;
		Send(1, json(env));
	}
	const room::Table& Table() const { return server.RoomSnapshot()->tables[0]; }
	// The match's flag in the last native projection sent to `connection`.
	bool Projected(session::Connection connection) const {
		for (auto sent = transport->outgoing.rbegin(); sent != transport->outgoing.rend(); ++sent)
			if (sent->first == connection && sent->second.value("type", std::string()) == "data_update")
				return sent->second.at("matchData").get<protocol::MatchData>().lowerInputLag;
		CHECK(false);
		return false;
	}
};

// Both fighters on: on for them and for the member queued behind them.
// One on: off for all. Unready gives the seat's choice back, and the next
// Ready takes a fresh one.
void TestMatchFlag() {
	Room both(3);
	both.Seat();
	both.Act(1, room::ActionKind::Ready, true);
	CHECK(both.Table().lowerLag[0] && !both.Table().lowerLag[1] && !room::MatchLowerLag(both.Table()));
	both.Act(2, room::ActionKind::Ready, true);
	CHECK(both.Table().ready[1] && room::MatchLowerLag(both.Table()));
	for (session::Connection connection = 1; connection <= 3; ++connection) CHECK(both.Projected(connection));

	Room one(3);
	one.Seat();
	one.Act(1, room::ActionKind::Ready, true);
	one.Act(1, room::ActionKind::Unready);
	CHECK(!one.Table().ready[0] && !one.Table().lowerLag[0]);
	one.Act(1, room::ActionKind::Ready, true);
	one.Act(2, room::ActionKind::Ready, false);
	CHECK(one.Table().ready[0] && one.Table().ready[1] && one.Table().lowerLag[0] && !room::MatchLowerLag(one.Table()));
	for (session::Connection connection = 1; connection <= 3; ++connection) CHECK(!one.Projected(connection));
}

// On the wire each is written only when on and reads as off when absent, so
// state and messages from builds before it read as before.
void TestWire() {
	room::Action ready;
	ready.kind = room::ActionKind::Ready;
	CHECK(!json(ready).contains("lower_lag") && !json(ready).get<room::Action>().lowerLag);
	ready.lowerLag = true;
	const json readyWire = ready;
	CHECK(readyWire.at("lower_lag") == true && readyWire.get<room::Action>().lowerLag);
	json wrongType = readyWire;
	wrongType["lower_lag"] = 1;
	bool refused = false;
	try { (void)wrongType.get<room::Action>(); } catch (const std::exception&) { refused = true; }
	CHECK(refused);

	room::Table table;
	CHECK(!json(table).contains("lower_lag"));
	table.lowerLag[1] = true;
	const json tableWire = table;
	CHECK(tableWire.at("lower_lag") == json::array({false, true}));
	const auto tableBack = tableWire.get<room::Table>();
	CHECK(!tableBack.lowerLag[0] && tableBack.lowerLag[1]);
	json shortList = tableWire;
	shortList["lower_lag"] = json::array({true});
	refused = false;
	try { (void)shortList.get<room::Table>(); } catch (const std::exception&) { refused = true; }
	CHECK(refused);
	json older = tableWire;
	older.erase("lower_lag");
	const auto olderBack = older.get<room::Table>();
	CHECK(!olderBack.lowerLag[0] && !olderBack.lowerLag[1]);

	protocol::MatchData match;
	match.lowerInputLag = true;
	CHECK(json(match).get<protocol::MatchData>().lowerInputLag);
	json olderMatch = match;
	olderMatch.erase("lowerInputLag");
	CHECK(!olderMatch.get<protocol::MatchData>().lowerInputLag);
	match.Clear();
	CHECK(!match.lowerInputLag);
}
}

int main() {
	TestStoreEdit();
	TestMatchFlag();
	TestWire();
	std::puts("LowerInputLag test passed");
	return 0;
}
