#pragma once

#include <QString>
#include <QDateTime>
#include <QMap>
#include <QVariantMap>

namespace Katabasis {
enum class Activity {
    Idle,
    LoggingIn,
    LoggingOut,
    Refreshing,
    FailedSoft, //!< soft failure. this generally means the user auth details haven't been invalidated
    FailedHard, //!< hard failure. auth is invalid
    FailedGone, //!< hard failure. auth is invalid, and the account no longer exists
    Succeeded
};

enum class Validity {
    None,
    Assumed,
    Certain
};

struct Token {
    QDateTime issueInstant;
    QDateTime notAfter;
    QString token;
    QString refresh_token;
    QVariantMap extra;
    QString yggname;
    QString yggurl;

    Validity validity = Validity::None;
    bool persistent = true;
};

}
