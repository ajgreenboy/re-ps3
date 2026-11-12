#include "autologon.h"
#include <QCoreApplication>
#include <cstdio>

// ZF3, the pure mapping (src/autologon.cpp classify) and the page text. No registry is read. Build with
// -DXMB_AUTOLOGON_BUILD_TESTS=ON; exit code 0 = all pass.
using namespace xmb::autologon;
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    int failures=0;
    const auto check=[&](bool ok,const char* what){if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);++failures;}};
    const auto off=classify({},{},QStringLiteral("al"));
    check(off.state==State::Off && off.value==QStringLiteral("Off") && off.log==QStringLiteral("autologon: off"),"absent value is off");
    check(classify(QStringLiteral("0"),QStringLiteral("al"),QStringLiteral("al")).state==State::Off,"AutoAdminLogon 0 is off");
    check(classify(QStringLiteral("true"),QStringLiteral("al"),QStringLiteral("al")).state==State::Off,"only the string 1 means on");
    const auto mine=classify(QStringLiteral("1"),QStringLiteral("Al"),QStringLiteral("al"));
    check(mine.state==State::OnThisUser && mine.value==QStringLiteral("On for Al") && mine.log==QStringLiteral("autologon: on for Al"),"same user, case-insensitive");
    check(classify(QStringLiteral("1"),QStringLiteral("PC\\al"),QStringLiteral("AL")).state==State::OnThisUser,"domain prefix ignored");
    const auto other=classify(QStringLiteral("1"),QStringLiteral("guest"),QStringLiteral("al"));
    check(other.state==State::OnOtherUser && other.value==QStringLiteral("On for a different user"),"different user");
    check(classify(QStringLiteral("1"),{},QStringLiteral("al")).state==State::OnUnverifiedUser,"no name cannot be compared");
    const auto mail=classify(QStringLiteral("1"),QStringLiteral("someone@example.com"),QStringLiteral("someon"));
    check(mail.state==State::OnUnverifiedUser && mail.value==QStringLiteral("On for someone@example.com"),"e-mail account is not claimed as a mismatch");
    check(classify(QStringLiteral("1"),QStringLiteral("al@example.com"),QStringLiteral("al")).state==State::OnThisUser,"e-mail local part equal to the user");
    const auto bad=classify(QStringLiteral("1"),QStringLiteral("al"),QStringLiteral("al"),false);
    check(bad.state==State::Unreadable && bad.value==QStringLiteral("Unknown"),"unreadable key");
    // The text never mentions reading or handling a password value, and states the guarantee.
    for(const auto* status:{&off,&mine,&other,&bad}) {
        const QString page=pageText(*status);
        const QString flat=QString(page).replace(QLatin1Char('\n'),QLatin1Char(' '));
        for(const QString& line:page.split(QLatin1Char('\n'))) {
            check(line.size()<=100,"every page line is short enough not to run off the screen");
            check(!line.contains(QStringLiteral(": ")) || line.startsWith(QStringLiteral("Status: ")),"only the status line has a label column");
        }
        check(flat.contains(QStringLiteral("never asks for, reads or stores the password")) && flat.contains(QStringLiteral("never changes this setting")),"page states the guarantees");
        check(page.contains(QStringLiteral("netplwiz")) && page.contains(QStringLiteral("Autologon")),"page has the manual steps");
        check(!status->log.contains(QStringLiteral("assword")),"log carries no password information");
    }
    for(const auto* status:{&off,&mine,&other,&bad}) {
        const QString flat=shortPageText(*status).replace(QLatin1Char('\n'),QLatin1Char(' '));
        check(shortPageText(*status).count(QLatin1Char('\n'))<=11,"short page fits the wizard's message page");
        check(flat.contains(QStringLiteral("never asks for, reads or stores the password")) && flat.contains(QStringLiteral("never changes this setting")),"short page states the guarantees");
    }
    check(stepGuidance(off).contains(QStringLiteral("is off")) && stepGuidance(off).contains(QStringLiteral("netplwiz")),"off guidance says what to do");
    std::fprintf(stdout,failures?"autologon tests: %d FAILED\n":"autologon tests: all passed\n",failures);
    return failures?1:0;
}
