#ifndef AUTH_UTIL_H
#define AUTH_UTIL_H

#include <QString>
#include <QCryptographicHash>
#include <cstdlib>

// 生成 16 字节随机盐（32 位十六进制）
inline QString generatePasswordSalt()
{
    QByteArray bytes(16, Qt::Uninitialized);
    for (int i = 0; i < 16; ++i)
        bytes[i] = static_cast<char>(qrand() & 0xFF);
    return QString(bytes.toHex());
}

// 每用户随机盐：SHA-256(salt + password)
inline QString authPasswordHash(const QString &password, const QString &salt)
{
    const QByteArray input = salt.toUtf8() + password.toUtf8();
    return QString(QCryptographicHash::hash(
        input, QCryptographicHash::Sha256).toHex());
}

// 旧版固定盐（仅用于兼容已注册用户）
inline QString authLegacyPasswordHash(const QString &password)
{
    const QString salted = QStringLiteral("ShortVideo_2026_")
        + password + QStringLiteral("_@platform");
    return QString(QCryptographicHash::hash(
        salted.toUtf8(), QCryptographicHash::Sha256).toHex());
}

inline bool isSha256Hex(const QString &s)
{
    if (s.length() != 64)
        return false;
    for (const QChar &c : s) {
        if (c.isDigit())
            continue;
        const QChar lc = c.toLower();
        if (lc < QLatin1Char('a') || lc > QLatin1Char('f'))
            return false;
    }
    return true;
}

inline bool isValidPasswordSalt(const QString &salt)
{
    if (salt.length() != 32)
        return false;
    for (const QChar &c : salt) {
        if (c.isDigit())
            continue;
        const QChar lc = c.toLower();
        if (lc < QLatin1Char('a') || lc > QLatin1Char('f'))
            return false;
    }
    return true;
}

#endif // AUTH_UTIL_H
