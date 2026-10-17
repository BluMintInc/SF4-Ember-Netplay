#include "../session/sf4e__SessionProtocol.hxx"
#include <iostream>
#include <nlohmann/json.hpp>
#include "test_support.hxx"

using sf4e::SessionProtocol::FindMatchProblem;
using sf4e::SessionProtocol::LobbyData;
using sf4e::SessionProtocol::MatchData;
using sf4e::SessionProtocol::MatchProblem;

// A room's match is checked as the game would take it, before any of it is
// written: its round count and time, its stage, and both fighters.
int main() {
    LobbyData lobby;
    MatchData match;
    match.stageID = 0;
    for (auto& chara : match.chara) { chara.charaID = 0; chara.ultraCombo = 0; chara.unc_edition = 14; }
    CHECK(FindMatchProblem(lobby, match) == MatchProblem::None);

    for (const int rounds : {0, 2, 4, 100, -1}) {
        LobbyData bad = lobby; bad.roundCount = rounds;
        CHECK(FindMatchProblem(bad, match) == MatchProblem::Settings);
    }
    for (const int time : {0, 45, 100}) {
        LobbyData bad = lobby; bad.roundTime.integral = time;
        CHECK(FindMatchProblem(bad, match) == MatchProblem::Settings);
    }
    { LobbyData bad = lobby; bad.roundTime.fractional = 1; CHECK(FindMatchProblem(bad, match) == MatchProblem::Settings); }

    { MatchData bad = match; bad.stageID = 9999; CHECK(FindMatchProblem(lobby, bad) == MatchProblem::Stage); }
    { MatchData bad = match; bad.stageID = -1; CHECK(FindMatchProblem(lobby, bad) == MatchProblem::Stage); }

    for (const int side : {0, 1}) {
        MatchData bad = match; bad.chara[side].charaID = 200;
        CHECK(FindMatchProblem(lobby, bad) == MatchProblem::Fighter);
        bad = match; bad.chara[side].costume = 250;
        CHECK(FindMatchProblem(lobby, bad) == MatchProblem::Fighter);
        bad = match; bad.chara[side].color = 250;
        CHECK(FindMatchProblem(lobby, bad) == MatchProblem::Fighter);
        bad = match; bad.chara[side].unc_edition = 99;
        CHECK(FindMatchProblem(lobby, bad) == MatchProblem::Fighter);
        bad = match; bad.chara[side].handicap = 9;
        CHECK(FindMatchProblem(lobby, bad) == MatchProblem::Fighter);
    }
    // With edition select off, only the Ultra edition is a fighter's.
    { LobbyData plain = lobby; plain.editionSelect = false; MatchData older = match; older.chara[1].unc_edition = 0;
      CHECK(FindMatchProblem(plain, older) == MatchProblem::Fighter); }

    // A seat's delay is accepted through the wire ceiling (a match committed
    // by an earlier build plays at its record) and refused past it.
    for (const int side : {0, 1}) {
        MatchData delayed = match;
        for (const int delay : {0, 5, 8, sf4e::InputDelayWireCeiling}) {
            delayed.inputDelay[side] = static_cast<std::uint8_t>(delay);
            CHECK(FindMatchProblem(lobby, delayed) == MatchProblem::None);
        }
        delayed.inputDelay[side] = sf4e::InputDelayWireCeiling + 1;
        CHECK(FindMatchProblem(lobby, delayed) == MatchProblem::Delay);
        delayed.inputDelay[side] = 255;
        CHECK(FindMatchProblem(lobby, delayed) == MatchProblem::Delay);
    }

    // The delays are read as the sender wrote them, before any narrowing to eight bits.
    {
        const auto base = nlohmann::json(match);
        const auto withDelay = [&](nlohmann::json delay) { auto j = base; j["inputDelay"] = std::move(delay); return j; };
        const auto reads = [&](const nlohmann::json& j) { try { j.get<MatchData>(); return true; } catch (const std::exception&) { return false; } };
        for (int first = 0; first <= 10; ++first)
            for (const int second : {0, 10}) {
                const auto read = withDelay(nlohmann::json::array({first, second})).get<MatchData>();
                CHECK(read.inputDelay[0] == first && read.inputDelay[1] == second);
            }
        for (const auto& bad : {nlohmann::json::array({257, 2}), nlohmann::json::array({2, 258}), nlohmann::json::array({2, 4294967298ull}),
            nlohmann::json::array({11, 2}), nlohmann::json::array({2, 255}), nlohmann::json::array({-1, 2}), nlohmann::json::array({2, -255}),
            nlohmann::json::array({1.5, 2}), nlohmann::json::array({2, 2.0}), nlohmann::json::array({"2", 2}), nlohmann::json::array({true, 2}),
            nlohmann::json::array({nullptr, 2}), nlohmann::json::array(), nlohmann::json::array({2}), nlohmann::json::array({2, 2, 2}),
            nlohmann::json(2), nlohmann::json("2,2"), nlohmann::json(nullptr), nlohmann::json::object()})
            CHECK(!reads(withDelay(bad)));
        // Parsed text takes the same path as a value built in code.
        CHECK(!reads(nlohmann::json::parse(R"({"inputDelay":[257,2]})")));
        CHECK(!reads(nlohmann::json::parse(R"({"inputDelay":[2,-1]})")));
        CHECK(!reads(nlohmann::json::parse(R"({"inputDelay":[2.5,2]})")));
        CHECK(!reads(nlohmann::json::parse(R"({"inputDelay":[18446744073709551615,2]})")));
        // A match written without the field keeps the default delay; the rest still reads.
        auto absent = base; absent.erase("inputDelay");
        const auto defaulted = absent.get<MatchData>();
        CHECK(defaulted.inputDelay[0] == 2 && defaulted.inputDelay[1] == 2 && defaulted.stageID == match.stageID);
        CHECK(nlohmann::json::parse("{}").get<MatchData>().inputDelay[1] == 2);
        CHECK(base.at("inputDelay") == nlohmann::json::array({2, 2}));
    }

    std::cout << "Match problems: settings, stage, either fighter and a delay past the wire ceiling refused; a valid match passes\n";
}
