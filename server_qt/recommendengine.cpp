#include "recommendengine.h"
#include "database.h"

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QSet>
#include <QtMath>

RecommendEngine::RecommendEngine(Database *db)
    : m_db(db)
    , m_params(loadParamsFromConfig())
{
}

void RecommendEngine::setParams(const Params &p)
{
    m_params = p;
}

RecommendEngine::Params RecommendEngine::params() const
{
    return m_params;
}

RecommendEngine::Params RecommendEngine::loadParamsFromConfig()
{
    Params p;
    const QString configPath = QCoreApplication::applicationDirPath() + "/config.ini";
    if (!QFile::exists(configPath))
        return p;

    QSettings settings(configPath, QSettings::IniFormat);
    settings.beginGroup("recommend");
    p.alpha = settings.value("alpha", p.alpha).toDouble();
    p.beta = settings.value("beta", p.beta).toDouble();
    p.T = settings.value("T", p.T).toDouble();
    p.gamma = settings.value("gamma", p.gamma).toDouble();
    p.freshRatio = settings.value("fresh_ratio", p.freshRatio).toDouble();
    p.safetyMargin = settings.value("safety_margin", p.safetyMargin).toInt();
    settings.endGroup();
    return p;
}

QVariantList RecommendEngine::getFeed(int offset, int limit) const
{
    if (!m_db || limit <= 0)
        return QVariantList();

    const int need = offset + limit;
    const int freshNeed = static_cast<int>(qCeil(need * m_params.freshRatio))
        + m_params.safetyMargin;
    const int hotNeed = need + freshNeed;

    const QVariantList hot = m_db->getHotVideoList(
        0, hotNeed, m_params.alpha, m_params.beta, m_params.T, m_params.gamma);
    const QVariantList fresh = m_db->getVideoList(0, freshNeed);
    const QVariantList merged = mergeFresh(hot, fresh, need);

    if (offset >= merged.size())
        return QVariantList();
    return merged.mid(offset, limit);
}

QVariantList RecommendEngine::mergeFresh(const QVariantList &hot,
                                         const QVariantList &fresh,
                                         int targetSize) const
{
    if (targetSize <= 0)
        return QVariantList();

    QVariantList result;
    result.reserve(targetSize);

    QSet<int> seen;
    int hotIdx = 0;
    int freshIdx = 0;
    int hotSinceFresh = 0;
    const int hotInterval = qMax(1, static_cast<int>(qRound(1.0 / m_params.freshRatio)));

    auto appendIfNew = [&](const QVariant &item) -> bool {
        const int id = item.toMap().value("id").toInt();
        if (seen.contains(id))
            return false;
        seen.insert(id);
        result.append(item);
        return true;
    };

    while (result.size() < targetSize) {
        const bool wantFresh = hotSinceFresh >= hotInterval;

        if (wantFresh) {
            bool addedFresh = false;
            while (freshIdx < fresh.size()) {
                if (appendIfNew(fresh.at(freshIdx++))) {
                    hotSinceFresh = 0;
                    addedFresh = true;
                    break;
                }
            }
            if (addedFresh)
                continue;
        }

        bool addedHot = false;
        while (hotIdx < hot.size()) {
            if (appendIfNew(hot.at(hotIdx++))) {
                hotSinceFresh++;
                addedHot = true;
                break;
            }
        }

        if (addedHot)
            continue;

        bool addedFallbackFresh = false;
        while (freshIdx < fresh.size()) {
            if (appendIfNew(fresh.at(freshIdx++))) {
                addedFallbackFresh = true;
                break;
            }
        }

        if (!addedFallbackFresh)
            break;
    }

    return result;
}
