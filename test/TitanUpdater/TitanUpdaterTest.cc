#include "TitanUpdaterTest.h"

#include "TitanUpdater.h"

namespace {

QByteArray release(const QString& tag, const QString& body)
{
    return QStringLiteral(
               R"({"tag_name":"%1","html_url":"https://github.com/Titan-Dynamics/TitanGroundControl/releases/tag/%1",
        "body":"%2","assets":[
        {"name":"TitanGroundControl-installer-AMD64.exe","size":1234,
         "browser_download_url":"https://github.com/Titan-Dynamics/TitanGroundControl/releases/download/%1/TitanGroundControl-installer-AMD64.exe",
         "digest":"sha256:00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff00ff"},
        {"name":"TitanGroundControl.dmg","size":5678,
         "browser_download_url":"https://github.com/Titan-Dynamics/TitanGroundControl/releases/download/%1/TitanGroundControl.dmg"}]})")
        .arg(tag, body)
        .toUtf8();
}

}  // namespace

void TitanUpdaterTest::_commitsAreRead()
{
    QCOMPARE(TitanUpdater::commitFromVersion(QStringLiteral("v1.0.8-45-gd955cca")), QStringLiteral("d955cca"));
    QCOMPARE(TitanUpdater::commitFromVersion(QStringLiteral("v1.0.9")), QString());
    QCOMPARE(TitanUpdater::commitFromReleaseBody(
                 QStringLiteral("## Development Build\\n**Commit:** D955CCABE1234567890abcdef1234567890abcd\\n")),
             QStringLiteral("d955ccabe1234567890abcdef1234567890abcd"));
    QCOMPARE(TitanUpdater::commitFromReleaseBody(QStringLiteral("No commit here")), QString());
}

void TitanUpdaterTest::_stableReleaseIsJudgedByVersion()
{
    const QString asset = QStringLiteral("TitanGroundControl-installer-AMD64.exe");

    // A development build of 1.0.8 is behind 1.0.9, but not behind 1.0.8 itself
    const TitanUpdater::Release newer =
        TitanUpdater::parseRelease(release(QStringLiteral("v1.0.9"), QString()), TitanUpdater::Stable,
                                   QStringLiteral("v1.0.8-45-gd955cca"), asset);
    QCOMPARE(newer.version, QStringLiteral("v1.0.9"));
    QVERIFY(newer.newer);

    const TitanUpdater::Release same =
        TitanUpdater::parseRelease(release(QStringLiteral("v1.0.8"), QString()), TitanUpdater::Stable,
                                   QStringLiteral("v1.0.8-45-gd955cca"), asset);
    QVERIFY(!same.newer);

    QVERIFY(TitanUpdater::parseRelease("not json", TitanUpdater::Stable, QStringLiteral("v1.0.8"), asset)
                .version.isEmpty());
}

void TitanUpdaterTest::_developmentReleaseIsJudgedByCommit()
{
    const QString asset = QStringLiteral("TitanGroundControl-installer-AMD64.exe");
    const QByteArray dev =
        release(QStringLiteral("dev"), QStringLiteral("**Commit:** d955ccabe1234567890abcdef1234567890abcd"));

    const TitanUpdater::Release running =
        TitanUpdater::parseRelease(dev, TitanUpdater::Development, QStringLiteral("v1.0.8-45-gd955cca"), asset);
    QCOMPARE(running.version, QStringLiteral("dev d955cca"));
    QVERIFY(!running.newer);

    QVERIFY(
        TitanUpdater::parseRelease(dev, TitanUpdater::Development, QStringLiteral("v1.0.8-44-g1234567"), asset).newer);
    // A tagged build has no commit of its own to compare, so the development build counts as newer
    QVERIFY(TitanUpdater::parseRelease(dev, TitanUpdater::Development, QStringLiteral("v1.0.8"), asset).newer);
}

void TitanUpdaterTest::_platformAssetIsFound()
{
    const TitanUpdater::Release found =
        TitanUpdater::parseRelease(release(QStringLiteral("v1.0.9"), QString()), TitanUpdater::Stable,
                                   QStringLiteral("v1.0.8"), QStringLiteral("TitanGroundControl-installer-AMD64.exe"));
    QCOMPARE(found.assetSize, qint64(1234));
    QVERIFY(found.assetUrl.endsWith(QStringLiteral("/v1.0.9/TitanGroundControl-installer-AMD64.exe")));
    QCOMPARE(found.sha256.size(), qsizetype(32));

    const TitanUpdater::Release missing =
        TitanUpdater::parseRelease(release(QStringLiteral("v1.0.9"), QString()), TitanUpdater::Stable,
                                   QStringLiteral("v1.0.8"), QStringLiteral("TitanGroundControl.apk"));
    QVERIFY(missing.assetUrl.isEmpty());

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    QVERIFY(!TitanUpdater::assetNameForPlatform().isEmpty());
#endif
}

UT_REGISTER_TEST(TitanUpdaterTest, TestLabel::Unit)
