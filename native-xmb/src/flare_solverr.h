#pragma once
#include <QByteArray>
#include <QUrl>

namespace xmb {
// FlareSolverr client: POSTs a URL to a local FlareSolverr instance and returns the challenge-solved
// page content. Blocking, like the other network clients; call from a worker. The endpoint is the
// configured base URL (e.g. "http://localhost:8191").
class FlareSolverr {
public:
    explicit FlareSolverr(const QString& endpoint):endpoint_(endpoint) {}
    bool request(const QUrl& url, QByteArray* response, QString* error=nullptr) const;
private:
    QString endpoint_;
};
} // namespace xmb
