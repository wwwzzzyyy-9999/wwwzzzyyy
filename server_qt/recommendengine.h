#ifndef RECOMMENDENGINE_H
#define RECOMMENDENGINE_H

#include <QVariantList>

class Database;

class RecommendEngine
{
public:
    struct Params {
        double alpha = 3.0;
        double beta  = 2.0;
        double T     = 2.0;
        double gamma = 1.5;
        double freshRatio = 0.15;
        int safetyMargin = 20;
    };

    explicit RecommendEngine(Database *db);

    QVariantList getFeed(int offset, int limit) const;

    void setParams(const Params &p);
    Params params() const;

    static Params loadParamsFromConfig();

private:
    QVariantList mergeFresh(const QVariantList &hot,
                            const QVariantList &fresh,
                            int targetSize) const;

    Database *m_db;
    Params m_params;
};

#endif // RECOMMENDENGINE_H
