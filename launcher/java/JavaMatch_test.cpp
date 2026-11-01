#include <QTest>
#include "TestUtil.h"

#include "java/JavaMatch.h"
#include "minecraft/VersionFilterData.h"

class JavaMatchTest : public QObject
{
    Q_OBJECT
private
slots:
    void test_RequiredJavaMajor()
    {
        auto &fd = g_VersionFilterData;

        // invalid date and dates before the first requirement boundary
        QCOMPARE(requiredJavaMajor(QDateTime()), 0);
        QCOMPARE(requiredJavaMajor(QDateTime::fromString("2010-01-01T00:00:00+00:00", Qt::ISODate)), 0);

        // boundary days: the begin date itself requires the new version
        QCOMPARE(requiredJavaMajor(fd.java8BeginsDate), 8);
        QCOMPARE(requiredJavaMajor(fd.java8BeginsDate.addSecs(-1)), 0);

        QCOMPARE(requiredJavaMajor(fd.java16BeginsDate), 16);
        QCOMPARE(requiredJavaMajor(fd.java16BeginsDate.addSecs(-1)), 8);

        QCOMPARE(requiredJavaMajor(fd.java17BeginsDate), 17);
        QCOMPARE(requiredJavaMajor(fd.java17BeginsDate.addSecs(-1)), 16);

        QCOMPARE(requiredJavaMajor(fd.java21BeginsDate), 21);
        QCOMPARE(requiredJavaMajor(fd.java21BeginsDate.addSecs(-1)), 17);

        QCOMPARE(requiredJavaMajor(fd.java25BeginsDate), 25);
        QCOMPARE(requiredJavaMajor(fd.java25BeginsDate.addSecs(-1)), 21);

        // anything newer than the newest known boundary
        QCOMPARE(requiredJavaMajor(fd.java25BeginsDate.addYears(1)), 25);
    }

    void test_FilterCompatible_data()
    {
        QTest::addColumn<QStringList>("versions");
        QTest::addColumn<int>("requiredMajor");
        QTest::addColumn<int>("matchedCount");

        QTest::newRow("no candidates") << QStringList() << 17 << 0;
        QTest::newRow("single match") << (QStringList() << "17.0.8" << "1.8.0_312") << 17 << 1;
        QTest::newRow("multiple matches") << (QStringList() << "17.0.8" << "21.0.2" << "1.8.0_312") << 17 << 2;
        QTest::newRow("no match") << (QStringList() << "1.8.0_312" << "16.0.2") << 17 << 0;
        // minor/security differences within the same major version don't matter
        QTest::newRow("minor version differences ignored") << (QStringList() << "17.0.1" << "17.0.2" << "17.1") << 17 << 3;
        // exact major boundary and higher majors match
        QTest::newRow("exact boundary") << (QStringList() << "8" << "1.7.0_80") << 8 << 1;
        QTest::newRow("higher major matches") << (QStringList() << "25" << "21.0.2") << 21 << 2;
    }

    void test_FilterCompatible()
    {
        QFETCH(QStringList, versions);
        QFETCH(int, requiredMajor);
        QFETCH(int, matchedCount);

        QList<JavaInstallPtr> candidates;
        for (int i = 0; i < versions.size(); ++i)
        {
            auto install = std::make_shared<JavaInstall>();
            install->id = JavaVersion(versions.at(i));
            install->path = QDir::tempPath() + "/java" + QString::number(i);
            candidates.append(install);
        }
        // broken entries must be skipped without crashing
        candidates.append(JavaInstallPtr());

        auto matched = JavaMatch::filterCompatible(requiredMajor, candidates);
        QCOMPARE(matched.size(), matchedCount);
        for (const auto &install : matched)
        {
            QVERIFY(install.get() != nullptr);
            QVERIFY(install->id.major() >= requiredMajor);
        }
    }
};

QTEST_GUILESS_MAIN(JavaMatchTest)

#include "JavaMatch_test.moc"
