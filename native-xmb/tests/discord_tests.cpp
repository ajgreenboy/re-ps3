// Discord sign-in failure messages and discord.json parsing (audit/DISCORD.md).
#include "discord_client.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

namespace {
int checks=0,failures=0;
void check(bool condition,const char* label) {
    ++checks;if(!condition){++failures;std::fprintf(stderr,"FAIL: %s\n",label);}
}
bool has(const QString& text,const char* part) { return text.contains(QLatin1String(part),Qt::CaseInsensitive); }
bool writeConfig(const QString& dir,const QByteArray& json) {
    QFile file(QDir(dir).filePath("discord.json"));
    return file.open(QIODevice::WriteOnly|QIODevice::Truncate) && file.write(json)==json.size();
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    using namespace xmb;
    qunsetenv("DISCORD_CLIENT_ID");qunsetenv("DISCORD_REDIRECT_PORT");
    const QString redirect=QStringLiteral("http://127.0.0.1:8888/callback");

    // The loopback redirect.
    check(has(discord::authorizeError("access_denied",{}),"cancelled"),"authorize: access_denied says cancelled");
    check(has(discord::authorizeError("invalid_scope",{}),"relationships.read"),"authorize: invalid_scope names the restricted scope");
    check(has(discord::authorizeError("invalid_scope",{}),"Social SDK"),"authorize: invalid_scope says where to apply");
    check(has(discord::authorizeError("unauthorized_client",{}),"Public Client"),"authorize: unauthorized_client says Public Client");
    check(has(discord::authorizeError("server_error","Something\nbroke"),"Something broke"),"authorize: description shown, control characters removed");
    check(discord::authorizeError("x",QString(500,'a')).size()<260,"authorize: description is bounded");

    // The token endpoint.
    check(has(discord::tokenError(0,{},"Host not found",redirect,false),"Host not found"),"token: network error shown");
    check(has(discord::tokenError(401,R"({"error":"invalid_client"})",{},redirect,false),"Public Client"),"token: invalid_client says Public Client");
    check(has(discord::tokenError(401,{},{},redirect,false),"clientId"),"token: bare 401 says check clientId");
    check(has(discord::tokenError(400,R"({"error":"invalid_grant","error_description":"Invalid \"redirect_uri\" in request."})",{},redirect,false),"127.0.0.1:8888/callback"),"token: redirect mismatch names the URI");
    check(has(discord::tokenError(400,R"({"error":"invalid_grant"})",{},redirect,true),"expired"),"token: refused refresh says expired");
    check(has(discord::tokenError(400,R"({"error":"invalid_grant"})",{},redirect,false),"try again"),"token: refused code says try again");
    check(has(discord::tokenError(400,R"({"error":"invalid_scope"})",{},redirect,false),"relationships.read"),"token: invalid_scope names the scope");
    check(has(discord::tokenError(429,R"({"retry_after":2.4})",{},redirect,false),"3 seconds"),"token: rate limit rounds retry_after up");
    check(has(discord::tokenError(503,"<html>",{},redirect,false),"HTTP 503"),"token: server error");
    check(has(discord::tokenError(400,"not json",{},redirect,false),"HTTP 400"),"token: unknown body falls back to status");

    // The relationships request.
    check(has(discord::friendsError(403,R"({"message":"Missing Access","code":50001})",{}),"relationships.read"),"friends: 403 names the scope");
    check(has(discord::friendsError(400,R"({"message":"Missing Access","code":50001})",{}),"Social SDK"),"friends: code 50001 says apply");
    check(has(discord::friendsError(401,{},{}),"Sign In to Discord"),"friends: 401 says sign in again");
    check(has(discord::friendsError(429,R"({"retry_after":10})",{}),"10 seconds"),"friends: rate limit");
    check(has(discord::friendsError(0,{},"Connection refused"),"Connection refused"),"friends: network error shown");
    check(has(discord::friendsError(200,"{}",{}),"could not read"),"friends: unreadable 200");

    // discord.json text.
    QString id;int port=-1;
    check(discord::parseConfig(R"({"clientId":"123456789012345678","redirectPort":8890})",id,port).isEmpty() && id=="123456789012345678" && port==8890,"config: id and port");
    check(discord::parseConfig(R"({"clientId":" 123456789012345678 "})",id,port).isEmpty() && id=="123456789012345678" && port==0,"config: trimmed id, no port");
    check(has(discord::parseConfig(R"({"clientId":123456789012345678})",id,port),"quotes"),"config: numeric id asks for quotes");
    check(has(discord::parseConfig(R"({"clientId":"1",})",id,port),"not valid JSON"),"config: trailing comma");
    check(has(discord::parseConfig(R"({"clientId":"123456789012345678","redirectPort":80})",id,port),"1024"),"config: privileged port refused");
    check(has(discord::parseConfig(R"({"clientId":"123456789012345678","redirectPort":"8888"})",id,port),"1024"),"config: string port refused");
    check(has(discord::setupText(redirect),"127.0.0.1:8888/callback") && has(discord::setupText(redirect),"Public Client"),"setup text names the redirect and Public Client");

    // DiscordClient::configure on a real data folder.
    QTemporaryDir dir;check(dir.isValid(),"temp dir");
    {
        DiscordClient client;
        check(!client.configure(dir.path()) && client.unset(),"configure: no file is unset");
        check(has(client.configurationError(),"127.0.0.1:8888/callback"),"configure: unset error names the redirect");
    }
    {
        check(writeConfig(dir.path(),R"({"clientId":"123456789012345678","redirectPort":8890})"),"write config");
        DiscordClient client;
        check(client.configure(dir.path()) && client.configured() && !client.unset(),"configure: valid file");
        check(client.redirectUri()=="http://127.0.0.1:8890/callback","configure: redirect uses redirectPort");
    }
    {
        check(writeConfig(dir.path(),R"({"clientId":"123456789012345678",)"),"write broken config");
        DiscordClient client;
        check(!client.configure(dir.path()) && !client.unset() && has(client.configurationError(),"not valid JSON"),"configure: broken file is reported, not unset");
        qputenv("DISCORD_CLIENT_ID","123456789012345678");
        check(client.configure(dir.path()),"configure: environment overrides a broken file");
        qunsetenv("DISCORD_CLIENT_ID");
    }
    {
        check(writeConfig(dir.path(),R"({"clientId":"my-app"})"),"write bad id");
        DiscordClient client;
        check(!client.configure(dir.path()) && has(client.configurationError(),"application id"),"configure: non-numeric id");
        check(writeConfig(dir.path(),R"({"clientId":"12345"})"),"write short id");
        check(!client.configure(dir.path()),"configure: too short to be a snowflake");
        check(writeConfig(dir.path(),R"({"clientId":"000000000000000000"})"),"write placeholder id");
        check(!client.configure(dir.path()) && has(client.configurationError(),"example clientId"),"configure: example placeholder refused");
    }
    {
        check(writeConfig(dir.path(),QByteArray(5000,' ')),"write big config");
        DiscordClient client;
        check(!client.configure(dir.path()) && has(client.configurationError(),"4 KB"),"configure: oversized file");
    }
    {
        // The shipped example parses (it carries a placeholder id, so it is not a usable config).
        QFile example(QStringLiteral(XMB_DISCORD_EXAMPLE));
        check(example.open(QIODevice::ReadOnly),"open examples/discord.json");
        check(discord::parseConfig(example.readAll(),id,port).isEmpty() && port==8888,"examples/discord.json parses");
    }
    std::printf("%d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
