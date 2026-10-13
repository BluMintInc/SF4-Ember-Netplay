#include "UpdatePrompt.hxx"
#include "../common/Localization.hxx"

namespace sf4e { namespace ui {

std::optional<launcher::InstallOffer> UpdatePrompt::Due(const platform::ServiceSnapshot& services, const Place& place) const {
    if (!services.launchOffer || open_ || !place.Open()) return std::nullopt;
    if (done_ && *done_ == *services.launchOffer) return std::nullopt;
    return services.launchOffer;
}

NoticeDialog UpdatePrompt::Dialog(const launcher::InstallOffer& offer, const std::string& installed) {
    // Later is highlighted: a press meant for the menu behind must not close the game.
    return NoticeDialog{loc::Tf("updates.launch_text", offer.tag, installed), loc::T("updates.launch_heading"),
        {{loc::T("updates.launch_now"), NoticeResult::UpdateNow}, {loc::T("updates.launch_later")},
         {loc::T("updates.launch_skip"), NoticeResult::SkipUpdate}}, 1};
}

std::optional<launcher::InstallOffer> UpdatePrompt::Close() {
    auto offer = std::move(open_);
    open_.reset();
    if (offer) done_ = offer;
    return offer;
}

UpdatePrompt::Answer UpdatePrompt::AnswerOf(NoticeResult result) {
    return result == NoticeResult::UpdateNow ? Answer::UpdateNow : result == NoticeResult::SkipUpdate ? Answer::Skip : Answer::Later;
}

} }
