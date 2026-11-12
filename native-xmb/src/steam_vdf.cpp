#include "steam_vdf.h"

#include <QFile>
#include <utility>

namespace xmb::steamvdf {
namespace {

enum class TokenKind { Word, BraceOpen, BraceClose };

struct Token {
    QString text;
    TokenKind kind = TokenKind::Word;
};

bool isSpace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

class Parser final {
public:
    Parser(const QByteArray& input, const Options& options) : input_(input), options_(options) {}

    Document parse() {
        Document document;
        if (!tokenize()) {
            document.error = error_;
            return document;
        }
        qsizetype entries = 0;
        if (!parseEntries(0, document.entries, entries) || position_ != tokens_.size()) {
            document.entries.clear();
            document.error = error_.isEmpty() ? QStringLiteral("unexpected trailing KeyValues data") : error_;
        }
        return document;
    }

private:
    bool tokenize() {
        qsizetype i = 0;
        if (options_.skipBom && input_.size() >= 3 && static_cast<unsigned char>(input_.at(0)) == 0xef &&
            static_cast<unsigned char>(input_.at(1)) == 0xbb && static_cast<unsigned char>(input_.at(2)) == 0xbf)
            i = 3; // UTF-8 BOM
        while (i < input_.size()) {
            const auto c = static_cast<unsigned char>(input_.at(i));
            if (isSpace(c)) {
                ++i;
                continue;
            }
            if (c == '/' && i + 1 < input_.size() && input_.at(i + 1) == '/') {
                i += 2;
                while (i < input_.size() && input_.at(i) != '\n') ++i;
                continue;
            }
            if (c == '{' || c == '}') {
                Token token;
                token.kind = c == '{' ? TokenKind::BraceOpen : TokenKind::BraceClose;
                tokens_.append(std::move(token));
                ++i;
            } else if (c == '"') {
                ++i;
                QByteArray text;
                bool closed = false;
                while (i < input_.size()) {
                    const char current = input_.at(i++);
                    if (current == '"') {
                        closed = true;
                        break;
                    }
                    if (current == '\\' && i < input_.size()) {
                        const char next = input_.at(i);
                        if (next == '"' || next == '\\') {
                            text.append(next);
                            ++i;
                        } else {
                            text.append(current);
                        }
                    } else {
                        text.append(current);
                    }
                    if (text.size() > options_.maxTokenBytes) {
                        error_ = QStringLiteral("KeyValues token exceeds the limit");
                        return false;
                    }
                }
                if (!closed) {
                    error_ = QStringLiteral("unterminated KeyValues string");
                    return false;
                }
                Token token;
                token.text = QString::fromUtf8(text);
                if (options_.quotedBracesAreStructural) {
                    if (token.text == QLatin1String("{")) token.kind = TokenKind::BraceOpen;
                    else if (token.text == QLatin1String("}")) token.kind = TokenKind::BraceClose;
                }
                tokens_.append(std::move(token));
            } else {
                const qsizetype start = i;
                while (i < input_.size()) {
                    const auto current = static_cast<unsigned char>(input_.at(i));
                    if (isSpace(current) || current == '{' || current == '}') break;
                    if (current == '/' && i + 1 < input_.size() && input_.at(i + 1) == '/') break;
                    ++i;
                }
                if (start == i) {
                    error_ = QStringLiteral("unsupported character in KeyValues file");
                    return false;
                }
                const QByteArray text = input_.mid(start, i - start);
                if (text.size() > options_.maxTokenBytes) {
                    error_ = QStringLiteral("KeyValues token exceeds the limit");
                    return false;
                }
                Token token;
                token.text = QString::fromUtf8(text);
                tokens_.append(std::move(token));
            }
            if (tokens_.size() > options_.maxTokens) {
                error_ = QStringLiteral("KeyValues token count exceeds the limit");
                return false;
            }
        }
        return true;
    }

    bool parseEntries(int depth, QVector<Entry>& entries, qsizetype& entriesUsed) {
        while (position_ < tokens_.size()) {
            const Token& token = tokens_.at(position_);
            if (token.kind == TokenKind::BraceClose) {
                if (depth == 0) {
                    error_ = QStringLiteral("unexpected closing brace");
                    return false;
                }
                return true; // the caller consumes the closing brace
            }
            if (token.kind == TokenKind::BraceOpen) {
                error_ = QStringLiteral("missing KeyValues key");
                return false;
            }
            const QString key = token.text;
            ++position_;
            if (position_ >= tokens_.size()) {
                error_ = QStringLiteral("missing KeyValues value");
                return false;
            }
            const Token& value = tokens_.at(position_);
            if (value.kind == TokenKind::BraceOpen) {
                ++position_;
                if (depth + 1 > options_.maxDepth) {
                    error_ = QStringLiteral("KeyValues nesting exceeds the limit");
                    return false;
                }
                Entry entry;
                entry.key = key;
                entry.block = true;
                if (!parseEntries(depth + 1, entry.children, entriesUsed)) return false;
                if (position_ >= tokens_.size() || tokens_.at(position_).kind != TokenKind::BraceClose) {
                    error_ = QStringLiteral("unterminated KeyValues block");
                    return false;
                }
                ++position_;
                if (++entriesUsed > options_.maxEntries) {
                    error_ = QStringLiteral("KeyValues entry count exceeds the limit");
                    return false;
                }
                entries.append(std::move(entry));
            } else if (value.kind == TokenKind::Word) {
                Entry entry;
                entry.key = key;
                entry.value = value.text;
                ++position_;
                if (++entriesUsed > options_.maxEntries) {
                    error_ = QStringLiteral("KeyValues entry count exceeds the limit");
                    return false;
                }
                entries.append(std::move(entry));
            } else {
                error_ = QStringLiteral("missing KeyValues value");
                return false;
            }
        }
        if (depth != 0) {
            error_ = QStringLiteral("unterminated KeyValues block");
            return false;
        }
        return true;
    }

    const QByteArray& input_;
    Options options_;
    QVector<Token> tokens_;
    qsizetype position_ = 0;
    QString error_;
};

} // namespace

Document parse(const QByteArray& input, const Options& options) {
    return Parser(input, options).parse();
}

const Entry* findEntry(const QVector<Entry>& entries, const QString& key, Qt::CaseSensitivity sensitivity) {
    for (const auto& entry : entries)
        if (entry.key.compare(key, sensitivity) == 0) return &entry;
    return nullptr;
}

QString stringValue(const QVector<Entry>& entries, const QString& key, Qt::CaseSensitivity sensitivity) {
    const auto* entry = findEntry(entries, key, sensitivity);
    return entry && !entry->block ? entry->value : QString{};
}

bool readBounded(const QString& path, qsizetype maximum, QByteArray* bytes, QString* detail, bool requireEof) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (detail) *detail = file.errorString();
        return false;
    }
    const qint64 size = file.size();
    if (size < 0 || size > maximum) {
        if (detail) *detail = QStringLiteral("the file exceeds the supported size");
        return false;
    }
    *bytes = file.read(maximum + 1);
    if (file.error() != QFileDevice::NoError || bytes->size() > maximum || (requireEof && !file.atEnd())) {
        if (detail) {
            *detail = file.errorString().isEmpty() ? QStringLiteral("the file could not be read completely")
                                                   : file.errorString();
        }
        bytes->clear();
        return false;
    }
    return true;
}

} // namespace xmb::steamvdf
