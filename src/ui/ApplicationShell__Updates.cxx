#include "ApplicationShell.hxx"
#include "../common/Localization.hxx"
#include "../platform/UiPreferencesStore.hxx"
#include <utility>

namespace sf4e { namespace ui {
// The first Home asks the service for the launch check; the service's policy
// decides whether one is due, and the question is put up only once its offer
// exists and the player is free to answer it. Ask again until the service has
// taken the request: it declines to start while another operation runs.
void ApplicationShell::UpdateLaunchCheck(const ShellView& v,double now,const Submit& submit) {
 using namespace netplay; auto& nav=menu_.navigation;
 if(!launchCheckSeeded_){launchCheck_=v.checkUpdatesAtLaunch;launchCheckSeeded_=true;}
 const bool home=nav.Screen()=="home";
 if(home&&v.services.launchCheck==platform::LaunchCheck::Waiting&&now-launchCheckAskedAt_>=1){
  launchCheckAskedAt_=now;
  ShellAction ask;ask.service=platform::ServiceAction::LaunchUpdateCheck;ask.command.generation=v.session.generation;
  submit(std::move(ask));
 }
 // Update now closes the game when the Launcher starts; when it does not, the player is still here and is told.
 const auto& services=v.services;
 if(services.lastAction==platform::ServiceAction::UpdateNow&&!services.pending&&!services.succeeded&&!services.message.empty()){
  if(!updateNowReported_){updateNowReported_=true;error_=services.message;}
 }else if(services.lastAction!=platform::ServiceAction::UpdateNow||services.pending)updateNowReported_=false;
 UpdatePrompt::Place place;
 place.home=home;
 // canEditPreferences is the runtime's own test for a game that may be left (UpdateNow needs it).
 place.free=v.canEditPreferences&&v.session.room==RoomState::Idle&&v.session.match==MatchState::None&&!v.practiceFight&&
  v.inputCapture==input::Capture::Idle;
 place.uncovered=!nav.Editing()&&!nav.Reading()&&!nav.Confirming()&&!nav.Choosing()&&!menu_.NoticeOpen();
 place.settingsNoticeDone=gameSettingsChecked_||!v.showGameSettingsCard;
 if(const auto offer=updatePrompt_.Due(v.services,place)){
  updateGeneration_=v.session.generation;
  updatePrompt_.Asked(*offer);
  menu_.ShowNotice(UpdatePrompt::Dialog(*offer,v.services.installedVersion.empty()?v.build:v.services.installedVersion));
 }
}
// The question closed: Update now and Skip this version send the offer the
// player was shown; anything else leaves it for the next launch.
void ApplicationShell::FinishUpdatePrompt(NoticeResult result,const Submit& submit) {
 const auto offer=updatePrompt_.Close();
 if(!offer)return;
 const auto answer=UpdatePrompt::AnswerOf(result);
 if(answer==UpdatePrompt::Answer::Later)return;
 ShellAction r;r.command.generation=updateGeneration_;
 r.service=answer==UpdatePrompt::Answer::UpdateNow?platform::ServiceAction::UpdateNow:platform::ServiceAction::SkipUpdate;
 r.updateOffer=*offer;
 if(!submit(std::move(r)))error_=loc::T("error.queue_failed");
}
void ApplicationShell::SetLaunchCheck(bool enabled) {
 if(enabled==launchCheck_&&launchCheckSaveError_.empty())return;
 std::string diagnostic;
 const bool saved=updateCheckSaver_?updateCheckSaver_(enabled,diagnostic):platform::SaveCheckUpdatesAtLaunch(enabled,diagnostic);
 if(saved){launchCheck_=enabled;launchCheckSaveError_.clear();}
 else launchCheckSaveError_=loc::T("common.save_failed");
}
} }
