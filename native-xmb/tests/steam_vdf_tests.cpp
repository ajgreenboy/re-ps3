// Golden tests for the Valve KeyValues (VDF) readers behind scanSteam() and scanSteamAccounts().
//
// The parsers are private to src/steam_library.cpp and src/steam_accounts.cpp (or, once merged, to
// src/steam_vdf.cpp), so the tests go through the two public entry points only. Every fixture is
// written into a throw-away Steam tree in the temp directory in one of three roles:
//   Login    -> config/loginusers.vdf          (read by BOTH scanSteamAccounts and scanSteam)
//   Folders  -> steamapps/libraryfolders.vdf   (scanSteam)
//   Manifest -> steamapps/appmanifest_100.acf  (scanSteam)
// and the observable result (accounts, error text, selected id, warnings, games) is reduced to a
// hash plus a short summary, then compared with tests/steam_vdf_golden.txt.
//
// The golden file was recorded from the ORIGINAL, unmerged parsers and must not change when the
// parser implementation changes. Fixtures are fully synthetic. The real Steam install of the machine
// is covered by `--real <file>`: it records or compares only counts and hashes, in a file the caller
// keeps OUTSIDE the repository, and prints no account id, path or title.
//
// Build with -DXMB_STEAM_VDF_BUILD_TESTS=ON. Usage:
//   xmb-steam-vdf-tests                       compare the fixtures with the golden file
//   xmb-steam-vdf-tests --record              rewrite the golden file (only from known-good parsers)
//   xmb-steam-vdf-tests --real <file>         compare the real Steam files with <file>
//   xmb-steam-vdf-tests --real <file> --record
#include "steam_accounts.h"
#include "steam_library.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QTemporaryDir>
#include <cstdio>

#ifndef XMB_VDF_GOLDEN
#error XMB_VDF_GOLDEN must name tests/steam_vdf_golden.txt
#endif

using namespace xmb;

namespace {

enum class Role { Login, Folders, Manifest };

const char* roleName(Role r) {
    return r == Role::Login ? "login" : r == Role::Folders ? "folders" : "manifest";
}

struct Style {
    bool bom = false;
    bool crlf = false;
    bool compact = false;     // one line, single spaces
    bool bare = false;        // unquoted keys/values where that is legal
    bool upper = false;       // key names in upper case
    bool lower = false;       // key names in lower case
    bool quotedBraces = false; // "{" and "}" written as quoted strings
    bool dupFields = false;   // every field repeated with a different value
    bool caseDup = false;     // wrong-case top-level block first, right-case one second
    bool fieldCaseDup = false; // lower-case field first with a wrong value, then the real one
    bool comments = false;
    bool tabs = false;        // tabs, vertical tabs and form feeds as separators
};

const char* IdA = "76561198000000001";
const char* IdB = "76561198000000002";

class Emitter {
public:
    explicit Emitter(Style s) : st_(s) {}
    void begin(const QString& key) {
        lineStart();
        out_ += tok(key, true);
        sep();
        out_ += open();
        if (st_.comments) out_ += " // open";
        ++depth_;
    }
    void end() {
        --depth_;
        lineStart();
        out_ += close();
        if (st_.comments) out_ += " // close";
    }
    void item(const QString& key, const QString& value, bool isKeyName = true) {
        if (st_.fieldCaseDup && isKeyName && key.at(0).isLetter()) {
            lineStart();
            out_ += tok(key.toLower(), false) + sepText() + tok(QStringLiteral("wrong"), false);
        }
        lineStart();
        out_ += tok(key, isKeyName) + sepText() + tok(value, false);
        if (st_.dupFields && isKeyName) {
            lineStart();
            out_ += tok(key, isKeyName) + sepText() + tok(value + QStringLiteral("-dup"), false);
        }
        if (st_.comments) out_ += " // field";
    }
    QByteArray finish() {
        if (st_.comments) out_ += st_.crlf ? "\r\n// trailing comment, no final newline" : "\n// trailing comment, no final newline";
        else out_ += st_.compact ? "" : (st_.crlf ? "\r\n" : "\n");
        QByteArray result;
        if (st_.bom) result += QByteArray("\xef\xbb\xbf", 3);
        return result + out_;
    }
    const Style& style() const { return st_; }

private:
    QByteArray open() const { return st_.quotedBraces ? "\"{\"" : "{"; }
    QByteArray close() const { return st_.quotedBraces ? "\"}\"" : "}"; }
    QByteArray sepText() const { return st_.tabs ? "\t" : " "; }
    void sep() { out_ += st_.tabs ? "\t\v" : " "; }
    void lineStart() {
        if (st_.compact) {
            if (!out_.isEmpty()) out_ += ' ';
            return;
        }
        if (!out_.isEmpty()) out_ += st_.crlf ? "\r\n" : "\n";
        if (st_.comments) out_ += "// note\n";
        out_ += st_.tabs ? QByteArray(depth_, '\t') + "\f" : QByteArray(depth_ * 2, ' ');
    }
    QByteArray tok(const QString& text, bool isKey) const {
        QString x = text;
        if (isKey && st_.upper) x = x.toUpper();
        else if (isKey && st_.lower) x = x.toLower();
        bool safe = !x.isEmpty();
        for (QChar c : x)
            if (c.isSpace() || c == QLatin1Char('"') || c == QLatin1Char('{') || c == QLatin1Char('}') ||
                c == QLatin1Char('/') || c == QLatin1Char('\\') || c.unicode() > 126)
                safe = false;
        if (st_.bare && safe) return x.toUtf8();
        QString quoted = x;
        quoted.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
        return '"' + quoted.toUtf8() + '"';
    }

    Style st_;
    QByteArray out_;
    int depth_ = 0;
};

struct Paths {
    QString root, lib1, lib2;
};

void userBlock(Emitter& e, const char* id, const QString& account, const QString& persona, bool recent,
               const char* stamp) {
    e.begin(QString::fromLatin1(id));
    e.item(QStringLiteral("AccountName"), account);
    e.item(QStringLiteral("PersonaName"), persona);
    e.item(QStringLiteral("RememberPassword"), QStringLiteral("1"));
    e.item(QStringLiteral("AllowAutoLogin"), QStringLiteral("1"));
    e.item(QStringLiteral("MostRecent"), recent ? QStringLiteral("1") : QStringLiteral("0"));
    e.item(QStringLiteral("Timestamp"), QString::fromLatin1(stamp));
    e.end();
}

QByteArray buildLogin(const Style& s, const QString& idA = QString::fromLatin1(IdA)) {
    Emitter e(s);
    const auto users = [&](const QString& name) {
        e.begin(name);
        userBlock(e, idA.toLatin1().constData(), QStringLiteral("alpha"), QStringLiteral("Alpha One"), true, "1700000000");
        userBlock(e, IdB, QStringLiteral("beta"), QStringLiteral("Beta"), false, "1690000000");
        e.end();
    };
    if (s.caseDup) {
        e.begin(QStringLiteral("USERS"));
        userBlock(e, "76561198000000009", QStringLiteral("wrongcase"), QStringLiteral("Wrong"), true, "1800000000");
        e.end();
    }
    users(QStringLiteral("users"));
    return e.finish();
}

QString escapedNative(const QString& path) {
    QString p = QDir::toNativeSeparators(path);
    return p;
}

QByteArray buildFolders(const Style& s, const Paths& p) {
    Emitter e(s);
    const auto body = [&](const QString& name) {
        e.begin(name);
        e.begin(QStringLiteral("0"));
        e.item(QStringLiteral("path"), escapedNative(p.root));
        e.item(QStringLiteral("label"), QString());
        e.item(QStringLiteral("contentid"), QStringLiteral("1"));
        e.begin(QStringLiteral("apps"));
        e.item(QStringLiteral("100"), QStringLiteral("1234"), false);
        e.end();
        e.end();
        e.begin(QStringLiteral("1"));
        e.item(QStringLiteral("path"), escapedNative(p.lib1));
        e.end();
        e.item(QStringLiteral("2"), escapedNative(p.lib2), false); // the pre-2021 flat form
        e.end();
    };
    if (s.caseDup) {
        e.begin(QStringLiteral("LIBRARYFOLDERS"));
        e.end();
    }
    body(QStringLiteral("libraryfolders"));
    return e.finish();
}

QByteArray buildManifest(const Style& s, const QString& id, const QString& name, const QString& dir) {
    Emitter e(s);
    if (s.caseDup) {
        e.begin(QStringLiteral("APPSTATE"));
        e.item(QStringLiteral("appid"), id, false);
        e.end();
    }
    e.begin(QStringLiteral("AppState"));
    e.item(QStringLiteral("appid"), id);
    e.item(QStringLiteral("Universe"), QStringLiteral("1"));
    e.item(QStringLiteral("name"), name);
    e.item(QStringLiteral("StateFlags"), QStringLiteral("4"));
    e.item(QStringLiteral("installdir"), dir);
    e.item(QStringLiteral("SizeOnDisk"), QStringLiteral("123"));
    e.end();
    return e.finish();
}

QByteArray build(Role role, const Style& s, const Paths& p) {
    switch (role) {
    case Role::Login: return buildLogin(s);
    case Role::Folders: return buildFolders(s, p);
    case Role::Manifest: return buildManifest(s, QStringLiteral("100"), QStringLiteral("Game One"), QStringLiteral("g100"));
    }
    return {};
}

// A raw string with %ID1% / %ID2% / %R% / %L1% / %L2% placeholders per role.
QByteArray rawFor(Role role, const QString& body, const Paths& p) {
    QString t = body;
    t.replace(QStringLiteral("%ID1%"), QString::fromLatin1(IdA)).replace(QStringLiteral("%ID2%"), QString::fromLatin1(IdB));
    QString r = p.root, l1 = p.lib1, l2 = p.lib2;
    t.replace(QStringLiteral("%R%"), r).replace(QStringLiteral("%L1%"), l1).replace(QStringLiteral("%L2%"), l2);
    Q_UNUSED(role);
    return t.toUtf8();
}

struct Fixture {
    QString name;
    Role role;
    QByteArray data;
};

QByteArray pad(qsizetype count) { return QByteArray(count, 'x'); }

// Pads `base` with a trailing comment so the whole file is exactly `size` bytes.
QByteArray padTo(QByteArray base, qsizetype size) {
    const QByteArray lead("\n// ");
    if (base.size() + lead.size() > size) return base.left(size);
    base += lead;
    base += pad(size - base.size());
    return base;
}

QByteArray repeat(const QByteArray& piece, qsizetype count) {
    QByteArray out;
    out.reserve(piece.size() * count);
    for (qsizetype i = 0; i < count; ++i) out += piece;
    return out;
}

QByteArray nested(int depth) {
    QByteArray out;
    for (int i = 0; i < depth; ++i) out += "\"d\" {\n";
    out += "\"leaf\" \"1\"\n";
    for (int i = 0; i < depth; ++i) out += "}\n";
    return out;
}

QByteArray pairs(int count) {
    QByteArray out = "\"pad\" {\n";
    for (int i = 0; i < count; ++i) out += "\"k\" \"v\"\n";
    out += "}\n";
    return out;
}

struct Lcg {
    quint32 state;
    quint32 next() {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
};

QVector<Fixture> makeFixtures(const Paths& p) {
    QVector<Fixture> out;
    const Role roles[] = {Role::Login, Role::Folders, Role::Manifest};
    const auto add = [&](const QString& name, Role role, const QByteArray& data) {
        out.append({QStringLiteral("%1/%2").arg(QString::fromLatin1(roleName(role)), name), role, data});
    };

    struct Named { const char* name; Style style; };
    QVector<Named> styles;
    styles.append({"plain", Style{}});
    {
        Style s; s.bom = true; styles.append({"bom", s});
        s = Style{}; s.crlf = true; styles.append({"crlf", s});
        s = Style{}; s.bom = true; s.crlf = true; styles.append({"bom-crlf", s});
        s = Style{}; s.compact = true; styles.append({"compact", s});
        s = Style{}; s.bare = true; styles.append({"bare", s});
        s = Style{}; s.bare = true; s.compact = true; styles.append({"bare-compact", s});
        s = Style{}; s.upper = true; styles.append({"upper-keys", s});
        s = Style{}; s.lower = true; styles.append({"lower-keys", s});
        s = Style{}; s.quotedBraces = true; styles.append({"quoted-braces", s});
        s = Style{}; s.dupFields = true; styles.append({"dup-fields", s});
        s = Style{}; s.caseDup = true; styles.append({"case-dup-blocks", s});
        s = Style{}; s.fieldCaseDup = true; styles.append({"case-dup-fields", s});
        s = Style{}; s.comments = true; styles.append({"comments", s});
        s = Style{}; s.comments = true; s.crlf = true; s.bom = true; styles.append({"comments-crlf-bom", s});
        s = Style{}; s.tabs = true; styles.append({"tabs-vt-ff", s});
    }
    for (Role role : roles)
        for (const auto& n : styles) add(QString::fromLatin1(n.name), role, build(role, n.style, p));

    // Structural edge cases written as raw text.
    const struct { const char* name; const char* login; const char* folders; const char* manifest; } raws[] = {
        {"empty", "", "", ""},
        {"whitespace-only", " \r\n\t\v\f ", " \r\n\t\v\f ", " \r\n\t\v\f "},
        {"comment-only", "// nothing here", "// nothing here\n", "//\n//\n"},
        {"bom-only", "\xef\xbb\xbf", "\xef\xbb\xbf", "\xef\xbb\xbf"},
        {"nul-byte", "users\0{}", "libraryfolders\0{}", "AppState\0{}"},
        {"empty-top-block", "\"users\" { }", "\"libraryfolders\" {}", "\"AppState\" {}"},
        {"empty-user-blocks", "\"users\" { \"%ID1%\" { } \"%ID2%\" {} }",
         "\"libraryfolders\" { \"0\" { } \"1\" {} }", "\"AppState\" { \"appid\" \"100\" }"},
        {"glued-tokens",
         "\"users\"{\"%ID1%\"{\"AccountName\"\"alpha\"\"MostRecent\"\"1\"}}",
         "\"libraryfolders\"{\"0\"{\"path\"\"%R%\"}}",
         "\"AppState\"{\"appid\"\"100\"\"name\"\"Glued\"\"StateFlags\"\"4\"\"installdir\"\"g100\"}"},
        {"bare-glued",
         "users{%ID1%{AccountName alpha MostRecent 1}}", "libraryfolders{0{path x}}",
         "AppState{appid 100 name Bare StateFlags 4 installdir g100}"},
        {"bare-with-comment-start",
         "users{%ID1%{AccountName alpha//c\nMostRecent 1}}", "libraryfolders{0{path x//c\n}}",
         "AppState{appid 100//c\n name Bare StateFlags 4 installdir g100}"},
        {"slash-single",
         "\"users\" { \"%ID1%\" { \"AccountName\" a/b \"MostRecent\" 1 } }",
         "\"libraryfolders\" { \"0\" { \"path\" a/b } }",
         "\"AppState\" { \"appid\" \"100\" \"name\" a/b \"StateFlags\" 4 \"installdir\" g100 }"},
        {"escapes",
         "\"users\" { \"%ID1%\" { \"AccountName\" \"a\\\"b\\\\c\\nd\\te\" \"PersonaName\" \"q\\\"\" \"MostRecent\" \"1\" } }",
         "\"libraryfolders\" { \"0\" { \"path\" \"a\\\\b\\\"c\\qd\" } }",
         "\"AppState\" { \"appid\" \"100\" \"name\" \"a\\\"b\\\\c\\nd\" \"StateFlags\" \"4\" \"installdir\" \"g100\" }"},
        {"trailing-backslash",
         "\"users\" { \"%ID1%\" { \"AccountName\" \"abc\\", "\"libraryfolders\" { \"0\" { \"path\" \"abc\\", "\"AppState\" { \"name\" \"abc\\"},
        {"unterminated-string",
         "\"users\" { \"%ID1%\" { \"AccountName\" \"alpha", "\"libraryfolders\" { \"0\" { \"path\" \"%R%",
         "\"AppState\" { \"appid\" \"100"},
        {"missing-value-eof", "\"users\" { \"%ID1%\" { \"AccountName\" } }", "\"libraryfolders\" { \"0\" }",
         "\"AppState\" { \"appid\" "},
        {"key-then-close", "\"users\" { \"%ID1%\" } ", "\"libraryfolders\" { \"0\" } ", "\"AppState\" { \"appid\" } "},
        {"brace-as-key", "\"users\" { { \"a\" \"b\" } }", "\"libraryfolders\" { { \"a\" \"b\" } }",
         "\"AppState\" { { \"a\" \"b\" } }"},
        {"stray-close-top", "}", "}", "}"},
        {"stray-close-after",
         "\"users\" { \"%ID1%\" { \"AccountName\" \"alpha\" \"MostRecent\" \"1\" } } }",
         "\"libraryfolders\" { \"0\" { \"path\" \"%R%\" } } }",
         "\"AppState\" { \"appid\" \"100\" \"name\" \"G\" \"StateFlags\" \"4\" \"installdir\" \"g100\" } }"},
        {"unclosed-block", "\"users\" { \"%ID1%\" { \"AccountName\" \"alpha\" } ",
         "\"libraryfolders\" { \"0\" { \"path\" \"%R%\" } ", "\"AppState\" { \"appid\" \"100\" "},
        {"extra-open-top", "{", "{", "{"},
        {"two-top-docs",
         "\"users\" { \"%ID1%\" { \"AccountName\" \"alpha\" \"MostRecent\" \"1\" } } \"users\" { \"%ID2%\" { \"AccountName\" \"beta\" \"MostRecent\" \"1\" } }",
         "\"libraryfolders\" { \"0\" { \"path\" \"%R%\" } } \"libraryfolders\" { \"1\" { \"path\" \"%L1%\" } }",
         "\"AppState\" { \"appid\" \"100\" \"name\" \"One\" \"StateFlags\" \"4\" \"installdir\" \"g100\" } \"AppState\" { \"appid\" \"100\" \"name\" \"Two\" \"StateFlags\" \"4\" \"installdir\" \"g100\" }"},
        {"quoted-brace-value",
         "\"users\" { \"%ID1%\" { \"AccountName\" \"{\" \"PersonaName\" \"p\" \"MostRecent\" \"1\" } }",
         "\"libraryfolders\" { \"0\" { \"path\" \"{\" } }",
         "\"AppState\" { \"appid\" \"100\" \"name\" \"{\" \"StateFlags\" \"4\" \"installdir\" \"g100\" }"},
        {"quoted-brace-pad",
         "\"users\" { \"%ID1%\" { \"AccountName\" \"alpha\" \"MostRecent\" \"1\" } } \"pad\" { \"x\" \"{\" \"y\" \"z\" \"}\" }",
         "\"libraryfolders\" { \"0\" { \"path\" \"%R%\" } } \"pad\" { \"x\" \"{\" \"y\" \"z\" \"}\" }",
         "\"AppState\" { \"appid\" \"100\" \"name\" \"G\" \"StateFlags\" \"4\" \"installdir\" \"g100\" } \"pad\" { \"x\" \"{\" \"y\" \"z\" \"}\" }"},
        {"quoted-close-as-key",
         "\"users\" { \"%ID1%\" { \"AccountName\" \"alpha\" \"MostRecent\" \"1\" } \"}\" \"v\" }",
         "\"libraryfolders\" { \"0\" { \"path\" \"%R%\" } \"}\" \"v\" }",
         "\"AppState\" { \"appid\" \"100\" \"name\" \"G\" \"StateFlags\" \"4\" \"installdir\" \"g100\" \"}\" \"v\" }"},
        {"quoted-empty-key-value",
         "\"users\" { \"%ID1%\" { \"\" \"\" \"AccountName\" \"\" \"MostRecent\" \"1\" } }",
         "\"libraryfolders\" { \"0\" { \"\" \"\" \"path\" \"%R%\" } }",
         "\"AppState\" { \"\" \"\" \"appid\" \"100\" \"name\" \"\" \"StateFlags\" \"4\" \"installdir\" \"g100\" }"},
        {"stateflags-variants",
         "\"users\" { \"%ID1%\" { \"AccountName\" \"alpha\" \"MostRecent\" \"true\" \"AllowAutoLogin\" \"TRUE\" } }",
         "\"libraryfolders\" { \"0\" { \"path\" \"%R%\" } }",
         "\"AppState\" { \"appid\" \"100\" \"name\" \"  Padded  \" \"StateFlags\" \"4\" \"installdir\" \"g100\" }"},
        {"high-bytes",
         "\"users\" { \"%ID1%\" { \"AccountName\" \"\xc3\xa9t\xc3\xa9\" \"PersonaName\" \"\xff\xfe bad \xc3\" \"MostRecent\" \"1\" } }",
         "\"libraryfolders\" { \"0\" { \"path\" \"%R%\" } \"1\" { \"path\" \"%L1%\xff\" } }",
         "\"AppState\" { \"appid\" \"100\" \"name\" \"\xc3\xa9\xff\" \"StateFlags\" \"4\" \"installdir\" \"g100\" }"},
        {"cr-only",
         "\"users\"\r{\r\"%ID1%\"\r{\r\"AccountName\"\r\"alpha\" // c\r\"MostRecent\" \"1\"\r}\r}",
         "\"libraryfolders\"\r{\r\"0\"\r{\r\"path\" \"%R%\" // c\r}\r}",
         "\"AppState\"\r{\r\"appid\" \"100\" // c\r\"name\" \"G\" \"StateFlags\" \"4\" \"installdir\" \"g100\"\r}"},
    };
    for (const auto& r : raws) {
        const auto go = [&](Role role, const char* body, qsizetype length) {
            QByteArray data = rawFor(role, QString::fromLatin1(body, length), p);
            add(QString::fromLatin1(r.name), role, data);
        };
        // The nul-byte fixture needs explicit lengths because the literals contain "\0".
        const bool nul = QByteArray(r.name) == "nul-byte";
        go(Role::Login, r.login, nul ? 8 : qstrlen(r.login));
        go(Role::Folders, r.folders, nul ? 17 : qstrlen(r.folders));
        go(Role::Manifest, r.manifest, nul ? 11 : qstrlen(r.manifest));
    }

    // Nesting depth: a sibling block nested N deep next to a valid document.
    for (Role role : roles) {
        const QByteArray base = build(role, Style{}, p);
        for (int depth : {1, 2, 7, 8, 9, 10, 15, 31, 32, 33, 34, 40})
            add(QStringLiteral("depth-%1").arg(depth), role, base + "\n" + nested(depth));
        for (int depth : {8, 9, 32, 33})
            add(QStringLiteral("depth-empty-%1").arg(depth), role,
                base + "\n" + repeat("\"d\" {\n", depth) + repeat("}\n", depth));
        // Depth reached through the document's own block instead of a sibling.
        add(QStringLiteral("depth-inside-9"), role, "\"wrap\" {\n" + nested(8) + "}\n" + base);
    }

    // Entry-count limits (4096 in the account reader, 20000 in the library reader).
    for (Role role : roles) {
        const QByteArray base = build(role, Style{}, p);
        for (int k = 4040; k <= 4100; ++k) add(QStringLiteral("entries-%1").arg(k), role, base + "\n" + pairs(k));
        for (int k = 19940; k <= 20010; k += (k < 19990 || k > 20000) ? 5 : 1)
            add(QStringLiteral("entries-%1").arg(k), role, base + "\n" + pairs(k));
    }

    // Token-count limits (20000 / 100000): garbage made only of tokens.
    for (Role role : roles) {
        const QByteArray base = build(role, Style{}, p);
        add(QStringLiteral("tokens-braces-20001"), role, repeat("}", 20001));
        add(QStringLiteral("tokens-open-20001"), role, repeat("{", 20001));
        add(QStringLiteral("tokens-open-100001"), role, repeat("{", 100001));
        add(QStringLiteral("tokens-pairs-10001"), role, base + "\n" + repeat("\"k\" \"v\"\n", 10001));
        add(QStringLiteral("tokens-pairs-50001"), role, base + "\n" + repeat("\"k\" \"v\"\n", 50001));
    }

    // Token-length limits (4096 bytes in the account reader).
    for (Role role : roles) {
        const QByteArray base = build(role, Style{}, p);
        for (int n : {4094, 4095, 4096, 4097, 4098}) {
            add(QStringLiteral("qvalue-%1").arg(n), role, base + "\n\"pad\" { \"k\" \"" + pad(n) + "\" }\n");
            add(QStringLiteral("qkey-%1").arg(n), role, base + "\n\"pad\" { \"" + pad(n) + "\" \"v\" }\n");
            add(QStringLiteral("bvalue-%1").arg(n), role, base + "\n\"pad\" { \"k\" " + pad(n) + " }\n");
            add(QStringLiteral("bkey-%1").arg(n), role, base + "\n\"pad\" { " + pad(n) + " \"v\" }\n");
        }
        // Escapes shrink the decoded token; multi-byte characters grow it.
        for (int n : {2047, 2048, 2049, 4096, 4097})
            add(QStringLiteral("qescapes-%1").arg(n), role, base + "\n\"pad\" { \"k\" \"" + repeat("\\\\", n) + "\" }\n");
        for (int n : {2047, 2048, 2049})
            add(QStringLiteral("qutf8-%1").arg(n), role, base + "\n\"pad\" { \"k\" \"" + repeat("\xc3\xa9", n) + "\" }\n");
        add(QStringLiteral("qescape-unknown-4096"), role, base + "\n\"pad\" { \"k\" \"" + repeat("\\q", 2048) + "\" }\n");
    }
    // The real fields at the limit.
    {
        Style s;
        const QByteArray longName = pad(4096), longName2 = pad(4097);
        add(QStringLiteral("accountname-4096"), Role::Login,
            "\"users\" { \"" + QByteArray(IdA) + "\" { \"AccountName\" \"" + longName + "\" \"MostRecent\" \"1\" } }");
        add(QStringLiteral("accountname-4097"), Role::Login,
            "\"users\" { \"" + QByteArray(IdA) + "\" { \"AccountName\" \"" + longName2 + "\" \"MostRecent\" \"1\" } }");
        add(QStringLiteral("title-4096"), Role::Manifest,
            "\"AppState\" { \"appid\" \"100\" \"name\" \"" + longName + "\" \"StateFlags\" \"4\" \"installdir\" \"g100\" }");
        add(QStringLiteral("title-4097"), Role::Manifest,
            "\"AppState\" { \"appid\" \"100\" \"name\" \"" + longName2 + "\" \"StateFlags\" \"4\" \"installdir\" \"g100\" }");
        (void)s;
    }

    // File-size limits: account reader 1 MiB, library readers 2 MiB (login/folders) and 1 MiB (manifest).
    for (Role role : roles) {
        const QByteArray base = build(role, Style{}, p);
        const qsizetype mib = 1024 * 1024;
        for (qsizetype delta : {-1, 0, 1}) {
            add(QStringLiteral("size-1MiB%1").arg(delta), role, padTo(base, mib + delta));
            add(QStringLiteral("size-2MiB%1").arg(delta), role, padTo(base, 2 * mib + delta));
        }
    }

    // Truncation sweep over the plain, compact and bom/crlf documents.
    for (Role role : roles) {
        for (const char* styleName : {"plain", "compact"}) {
            Style s; s.compact = QByteArray(styleName) == "compact";
            const QByteArray base = build(role, s, p);
            for (qsizetype n = 0; n <= base.size(); ++n)
                add(QStringLiteral("trunc-%1-%2").arg(QString::fromLatin1(styleName)).arg(n), role, base.left(n));
        }
        Style s; s.bom = true; s.crlf = true;
        const QByteArray base = build(role, s, p);
        for (qsizetype n = 0; n <= base.size(); n += 3)
            add(QStringLiteral("trunc-bomcrlf-%1").arg(n), role, base.left(n));
    }

    // Deterministic garbage and mutations of the plain documents.
    for (Role role : roles) {
        Lcg g{0x12345u + quint32(role)};
        const QByteArray alphabet("\"\"{}{} \n\n/\\ab1\t");
        for (int i = 0; i < 120; ++i) {
            const int length = int(g.next() % 200);
            QByteArray data;
            for (int k = 0; k < length; ++k) data += alphabet.at(int(g.next() % alphabet.size()));
            add(QStringLiteral("garbage-alpha-%1").arg(i), role, data);
        }
        for (int i = 0; i < 40; ++i) {
            const int length = int(g.next() % 300);
            QByteArray data;
            for (int k = 0; k < length; ++k) data += char(g.next() & 0xff);
            add(QStringLiteral("garbage-bytes-%1").arg(i), role, data);
        }
        for (const char* styleName : {"plain", "compact"}) {
            Style s; s.compact = QByteArray(styleName) == "compact";
            const QByteArray base = build(role, s, p);
            for (int i = 0; i < 250; ++i) {
                QByteArray data = base;
                const int edits = 1 + int(g.next() % 4);
                for (int e = 0; e < edits && !data.isEmpty(); ++e) {
                    const int pos = int(g.next() % data.size());
                    switch (g.next() % 5) {
                    case 0: data[pos] = alphabet.at(int(g.next() % alphabet.size())); break;
                    case 1: data.remove(pos, 1 + int(g.next() % 6)); break;
                    case 2: data.insert(pos, data.mid(int(g.next() % data.size()), 1 + int(g.next() % 12))); break;
                    case 3: data.insert(pos, alphabet.at(int(g.next() % alphabet.size()))); break;
                    default: data[pos] = char(g.next() & 0xff); break;
                    }
                }
                add(QStringLiteral("mutate-%1-%2").arg(QString::fromLatin1(styleName)).arg(i), role, data);
            }
        }
    }
    return out;
}

// ---- the throw-away Steam tree ----------------------------------------------------------------

bool writeFile(const QString& path, const QByteArray& data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return file.write(data) == data.size();
}

struct World {
    QTemporaryDir dir;
    Paths paths;
    QString canonicalRoot;

    bool init() {
        if (!dir.isValid()) return false;
        const QString base = dir.path();
        paths.root = QDir(base).filePath(QStringLiteral("steam"));
        paths.lib1 = QDir(base).filePath(QStringLiteral("lib one"));
        paths.lib2 = QDir(base).filePath(QStringLiteral("lib2"));
        for (const QString& lib : {paths.root, paths.lib1, paths.lib2}) QDir().mkpath(lib + QStringLiteral("/steamapps/common"));
        QDir().mkpath(paths.root + QStringLiteral("/config"));
        QDir().mkpath(paths.root + QStringLiteral("/steamapps/common/g100"));
        QDir().mkpath(paths.lib1 + QStringLiteral("/steamapps/common/g200"));
        QDir().mkpath(paths.lib2 + QStringLiteral("/steamapps/common/g300"));
        QDir().mkpath(paths.root + QStringLiteral("/userdata/39734273/config"));
        writeFile(paths.root + QStringLiteral("/userdata/39734273/config/localconfig.vdf"), "\"UserLocalConfigStore\" { }");
        canonicalRoot = QDir(paths.root).canonicalPath();
        return true;
    }

    void reset() {
        const Style plain;
        writeFile(paths.root + QStringLiteral("/config/loginusers.vdf"), buildLogin(plain));
        writeFile(paths.root + QStringLiteral("/steamapps/libraryfolders.vdf"), buildFolders(plain, paths));
        writeFile(paths.root + QStringLiteral("/steamapps/appmanifest_100.acf"),
                  buildManifest(plain, QStringLiteral("100"), QStringLiteral("Game One"), QStringLiteral("g100")));
        writeFile(paths.lib1 + QStringLiteral("/steamapps/appmanifest_200.acf"),
                  buildManifest(plain, QStringLiteral("200"), QStringLiteral("Game Two"), QStringLiteral("g200")));
        writeFile(paths.lib2 + QStringLiteral("/steamapps/appmanifest_300.acf"),
                  buildManifest(plain, QStringLiteral("300"), QStringLiteral("Game Three"), QStringLiteral("g300")));
    }

    QString target(Role role) const {
        switch (role) {
        case Role::Login: return paths.root + QStringLiteral("/config/loginusers.vdf");
        case Role::Folders: return paths.root + QStringLiteral("/steamapps/libraryfolders.vdf");
        case Role::Manifest: return paths.root + QStringLiteral("/steamapps/appmanifest_100.acf");
        }
        return {};
    }
};

struct Outcome {
    QString detail;  // everything observable, hashed
    QString summary; // short and printable
};

QString scrub(QString text, const World& w) {
    const QStringList forms = {w.canonicalRoot, QDir::toNativeSeparators(w.canonicalRoot), w.paths.root,
                               QDir::toNativeSeparators(w.paths.root)};
    for (const QString& form : forms) text.replace(form, QStringLiteral("<R>"), Qt::CaseInsensitive);
    QString base = QDir(w.dir.path()).canonicalPath();
    text.replace(base, QStringLiteral("<T>"), Qt::CaseInsensitive);
    text.replace(QDir::toNativeSeparators(base), QStringLiteral("<T>"), Qt::CaseInsensitive);
    return text;
}

QString accountsDetail(const QVector<SteamAccount>& accounts, const QString& error) {
    QString text = QStringLiteral("A%1 e=[%2]").arg(accounts.size()).arg(error);
    for (const auto& a : accounts)
        text += QStringLiteral("\n %1|%2|%3|%4%5%6%7|%8").arg(a.steamId64, a.accountName, a.personaName)
                    .arg(a.rememberPassword).arg(a.autoLogin).arg(a.active).arg(a.hasUserdata).arg(a.lastLoginUnix);
    return text;
}

QString scanDetail(const SteamInstallation& s) {
    QString text = QStringLiteral("S root=%1 id=%2 w=%3").arg(s.root, s.steamId).arg(s.warnings.join(QLatin1Char('|')));
    for (const auto& g : s.games)
        text += QStringLiteral("\n %1|%2|%3|%4|%5").arg(g.appId, g.title, g.installDirectory)
                    .arg(g.artwork.isEmpty() ? 0 : 1).arg(g.backgroundArtwork.isEmpty() ? 0 : 1);
    return text;
}

Outcome observe(const World& w) {
    QString accountsError;
    const auto accounts = scanSteamAccounts(w.paths.root, &accountsError);
    const auto scan = scanSteam(w.paths.root);
    Outcome o;
    o.detail = scrub(accountsDetail(accounts, accountsError) + QLatin1Char('\n') + scanDetail(scan), w);
    o.summary = QStringLiteral("a=%1 ae=%2 s=%3 g=%4 w=%5")
                    .arg(accounts.size()).arg(accountsError.isEmpty() ? 0 : 1)
                    .arg(scan.steamId.isEmpty() ? QStringLiteral("-") : scan.steamId.right(2))
                    .arg(scan.games.size()).arg(scan.warnings.size());
    return o;
}

QString digest(const QString& text) {
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex().left(20));
}

// ---- real Steam files (read only; counts and hashes, never content) ---------------------------

QStringList realLines() {
    QStringList lines;
    const auto line = [&](const QString& name, const QString& detail, const QString& counts) {
        lines << QStringLiteral("%1\t%2\t%3").arg(name, digest(detail), counts);
    };
    QString error;
    const auto accounts = scanSteamAccounts({}, &error);
    line(QStringLiteral("real/accounts-default"), accountsDetail(accounts, error),
         QStringLiteral("a=%1 ae=%2").arg(accounts.size()).arg(error.isEmpty() ? 0 : 1));
    const auto scan = scanSteam();
    line(QStringLiteral("real/scan-default"), scanDetail(scan),
         QStringLiteral("g=%1 w=%2 id=%3").arg(scan.games.size()).arg(scan.warnings.size()).arg(scan.steamId.isEmpty() ? 0 : 1));
    if (!scan.root.isEmpty()) {
        // An explicit root disables the registry preference, so the id comes from loginusers.vdf itself.
        const auto explicitScan = scanSteam(scan.root);
        line(QStringLiteral("real/scan-explicit"), scanDetail(explicitScan),
             QStringLiteral("g=%1 w=%2 id=%3").arg(explicitScan.games.size()).arg(explicitScan.warnings.size())
                 .arg(explicitScan.steamId.isEmpty() ? 0 : 1));
        QString explicitError;
        const auto explicitAccounts = scanSteamAccounts(scan.root, &explicitError);
        line(QStringLiteral("real/accounts-explicit"), accountsDetail(explicitAccounts, explicitError),
             QStringLiteral("a=%1 ae=%2").arg(explicitAccounts.size()).arg(explicitError.isEmpty() ? 0 : 1));
    }
    return lines;
}

QStringList readLines(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

bool writeLines(const QString& path, const QStringList& lines) {
    return writeFile(path, (lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
}

int compare(const QStringList& got, const QStringList& golden, const char* what) {
    QHash<QString, QString> expected;
    for (const QString& l : golden) expected.insert(l.section(QLatin1Char('\t'), 0, 0), l);
    int failures = 0;
    QSet<QString> seen;
    for (const QString& l : got) {
        const QString name = l.section(QLatin1Char('\t'), 0, 0);
        seen.insert(name);
        const auto it = expected.constFind(name);
        if (it == expected.constEnd()) {
            if (failures++ < 12) std::fprintf(stderr, "FAIL %s: no golden entry: %s\n", what, qPrintable(name));
        } else if (*it != l) {
            if (failures++ < 12)
                std::fprintf(stderr, "FAIL %s: %s\n  golden: %s\n  got:    %s\n", what, qPrintable(name),
                             qPrintable(it->section(QLatin1Char('\t'), 1)), qPrintable(l.section(QLatin1Char('\t'), 1)));
        }
    }
    for (auto it = expected.cbegin(); it != expected.cend(); ++it)
        if (!seen.contains(it.key()) && failures++ < 12) std::fprintf(stderr, "FAIL %s: missing: %s\n", what, qPrintable(it.key()));
    return failures;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    bool record = false;
    QString realPath;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--record")) record = true;
        else if (args.at(i) == QLatin1String("--real") && i + 1 < args.size()) realPath = args.at(++i);
    }

    if (!realPath.isEmpty()) {
        const QStringList lines = realLines();
        if (record) {
            if (!writeLines(realPath, lines)) { std::fprintf(stderr, "cannot write the real-file record\n"); return 2; }
            std::printf("recorded %lld real-file result(s)\n", static_cast<long long>(lines.size()));
            return 0;
        }
        const QStringList golden = readLines(realPath);
        if (golden.isEmpty()) { std::fprintf(stderr, "no real-file record at the given path\n"); return 2; }
        const int failures = compare(lines, golden, "real");
        std::printf("real Steam files: %lld result(s), %d failure(s)\n", static_cast<long long>(lines.size()), failures);
        return failures ? 1 : 0;
    }

    World world;
    if (!world.init()) { std::fprintf(stderr, "cannot create the temporary Steam tree\n"); return 2; }
    const QVector<Fixture> fixtures = makeFixtures(world.paths);

    QStringList lines;
    QHash<QString, QString> byName;
    int failures = 0;
    const auto check = [&](bool ok, const char* what) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; } };
    for (const Fixture& f : fixtures) {
        world.reset();
        if (!writeFile(world.target(f.role), f.data)) { std::fprintf(stderr, "cannot write a fixture\n"); return 2; }
        const Outcome o = observe(world);
        lines << QStringLiteral("%1\t%2\t%3").arg(f.name, digest(o.detail), o.summary);
        byName.insert(f.name, o.summary);
    }

    // The fixtures must not be vacuous: the plain documents resolve completely, and the table is varied.
    check(byName.value(QStringLiteral("login/plain")).startsWith(QStringLiteral("a=2 ae=0")), "plain loginusers.vdf yields two accounts");
    check(byName.value(QStringLiteral("manifest/plain")).contains(QStringLiteral("g=3")), "plain manifest yields all three games");
    check(byName.value(QStringLiteral("folders/plain")).contains(QStringLiteral("g=3 w=0")), "plain libraryfolders.vdf yields three libraries");
    QSet<QString> distinct;
    for (const QString& l : lines) distinct.insert(l.section(QLatin1Char('\t'), 1, 1));
    check(distinct.size() > 40, "the fixtures exercise many distinct outcomes");
    check(lines.size() > 2500, "the fixture set is complete");

    if (record) {
        if (!writeLines(QStringLiteral(XMB_VDF_GOLDEN), lines)) { std::fprintf(stderr, "cannot write the golden file\n"); return 2; }
        std::printf("recorded %lld fixture result(s), %lld distinct outcome(s)\n", static_cast<long long>(lines.size()),
                    static_cast<long long>(distinct.size()));
        return failures ? 1 : 0;
    }
    const QStringList golden = readLines(QStringLiteral(XMB_VDF_GOLDEN));
    if (golden.isEmpty()) { std::fprintf(stderr, "golden file missing: record it with --record\n"); return 2; }
    failures += compare(lines, golden, "fixture");
    std::printf("%lld fixture result(s), %lld distinct outcome(s), %d failure(s)\n", static_cast<long long>(lines.size()),
                static_cast<long long>(distinct.size()), failures);
    return failures ? 1 : 0;
}
