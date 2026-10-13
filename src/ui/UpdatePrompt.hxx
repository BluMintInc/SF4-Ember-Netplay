#pragma once

#include <optional>
#include <string>
#include "GameMenu.hxx"
#include "../launcher/update/InstallOffer.hxx"
#include "../platform/ApplicationServices.hxx"

namespace sf4e { namespace ui {

// The question the game asks when its launch check found an update: Version X
// is available, with Update now, Later and Skip this version. It owns which
// offer was asked about and whether the question is on screen; the policy that
// decided to offer it, and the answers' effects, live in ApplicationServices.
//
// It asks only where a player is free to answer: on Home with no room, match,
// editor or other dialog, and after the launch-time settings notice. An offer is
// asked once per run; Later asks again on the next launch (the policy's cooldown
// and the saved skip decide whether that run offers it).
class UpdatePrompt {
public:
    struct Place {
        bool home = false;            // the screen is Home
        bool free = false;            // no room, match, Training or pad setup
        bool uncovered = false;       // no editor, reader, confirmation, choice or dialog open
        bool settingsNoticeDone = false;
        bool Open() const { return home && free && uncovered && settingsNoticeDone; }
    };
    enum class Answer { UpdateNow, Skip, Later };

    // The offer to ask about now, or nothing.
    std::optional<launcher::InstallOffer> Due(const platform::ServiceSnapshot& services, const Place& place) const;
    // The dialog for `offer`, installed being the version on this PC.
    static NoticeDialog Dialog(const launcher::InstallOffer& offer, const std::string& installed);
    // The question went on screen.
    void Asked(const launcher::InstallOffer& offer) { open_ = offer; }
    bool Open() const { return open_.has_value(); }
    // The question closed: the offer it was about, which is not asked about again this run.
    std::optional<launcher::InstallOffer> Close();
    // What a closed notice's result means for the open question: Update now and
    // Skip are their buttons; anything else (Later, Back, another notice taking
    // the place) leaves it unanswered, which asks again next launch.
    static Answer AnswerOf(NoticeResult result);

private:
    std::optional<launcher::InstallOffer> open_, done_;
};

} }
