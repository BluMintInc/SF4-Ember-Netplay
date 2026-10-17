#pragma once
// The ping shot of ui_render_test.cxx: the room board with each member's ping
// on the member cards and on the table fighters, in all three bands, while
// measuring and while unavailable. The entries stay on the view, so the room
// shots that follow show them too.
#include "../ui/ApplicationShell.hxx"
namespace {
template<class Draw,class Page>
void ShootMemberLatency(sf4e::ui::ShellView& view,const Draw& draw,const Page& page) {
    using State=sf4e::session::MemberLatency::State;
    const auto ping=[&](sf4e::room::MemberId id,State state,unsigned rtt,const char* reason){
        sf4e::session::MemberLatency entry;entry.incarnation=1;entry.state=state;entry.rttMs=rtt;entry.reason=reason;
        view.memberLatency[id]=entry;
    };
    ping(2,State::Value,42,"");ping(3,State::Value,118,"");ping(4,State::Value,236,"");
    ping(5,State::Measuring,0,"");ping(6,State::Unavailable,0,"unreachable");
    page("room");draw("room-latency");
}
}
