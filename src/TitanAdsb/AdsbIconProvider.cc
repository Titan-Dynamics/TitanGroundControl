#include "AdsbIconProvider.h"

#include <QtCore/QHash>
#include <QtGui/QColor>
#include <QtGui/QPainter>
#include <QtSvg/QSvgRenderer>

#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(AdsbIconProviderLog, "TitanAdsb.AdsbIconProvider")

namespace {

/// Silhouettes in a 72 x 72 box, nose up. The airliner is QGroundControl's own traffic aircraft. Balloons and
/// parachutes are drawn from the side.
const QHash<QString, QString>& iconPaths()
{
    static const QHash<QString, QString> paths = {
        {QStringLiteral("airliner"),
         QStringLiteral(
             "M34.066 9.531c-1.349.775-2.116 2.277-2.116 3.833v11.91a4.74 4.74 0 0 1-2.227 4.018L12.838 39.845a5.29 "
             "5.29 "
             "0 0 0-2.488 4.489V46.8l18.25-5.703a2.58 2.58 0 0 1 3.35 2.463V54.9l-4.364 3.273a2.59 2.59 0 0 0-1.036 "
             "2.073V63L36 60.3l9.45 2.7v-2.754c0-.816-.384-1.584-1.036-2.073L40.05 54.9V43.56a2.58 2.58 0 0 1 "
             "3.35-2.463"
             "L61.65 46.8v-2.466c0-1.825-.94-3.522-2.488-4.489L42.277 29.292a4.74 4.74 0 0 1-2.227-4.018V13.05c0-2.795-"
             "3.164-5.138-5.984-3.519Z")},
        {QStringLiteral("light"),
         QStringLiteral(
             "M36 12C38.2 12 39.5 13.8 39.5 16L39.5 24L62.5 24C64.4 24 66 25.6 66 27.5C66 29.4 64.4 31 62.5 31L39.5 31"
             "L38.4 52L46.5 52C47.9 52 49 53.1 49 54.5C49 55.9 47.9 57 46.5 57L38 57L37 61.5C36.8 62.3 35.2 62.3 35 "
             "61.5"
             "L34 57L25.5 57C24.1 57 23 55.9 23 54.5C23 53.1 24.1 52 25.5 52L33.6 52L32.5 31L9.5 31C7.6 31 6 29.4 6 "
             "27.5"
             "C6 25.6 7.6 24 9.5 24L32.5 24L32.5 16C32.5 13.8 33.8 12 36 12Z M27 8.5L45 8.5C45.6 8.5 46 8.9 46 9.5C46 "
             "10.1 45.6 10.5 45 10.5L27 10.5C26.4 10.5 26 10.1 26 9.5C26 8.9 26.4 8.5 27 8.5Z")},
        {QStringLiteral("fighter"),
         QStringLiteral(
             "M36 6C37.4 6 38.4 9 38.8 13L39.6 26L62 46C63.2 47.1 64 48.6 64 50.2L64 52L40.6 46.5L41.4 56L47.5 60.5"
             "C48.4 61.2 49 62.2 49 63.3L49 65L36 62.2L23 65L23 63.3C23 62.2 23.6 61.2 24.5 60.5L30.6 56L31.4 46.5L8 52"
             "L8 50.2C8 48.6 8.8 47.1 10 46L32.4 26L33.2 13C33.6 9 34.6 6 36 6Z")},
        {QStringLiteral("helicopter"),
         QStringLiteral(
             "M20.13 9.87L54.13 43.87L51.87 46.13L17.87 12.13Z M51.87 9.87L54.13 12.13L20.13 46.13L17.87 43.87Z "
             "M36 15C41.5 15 45 21 45 28C45 35 41.5 41 36 41C30.5 41 27 35 27 28C27 21 30.5 15 36 15Z "
             "M34.2 40L37.8 40L37.2 62L34.8 62Z M29 59L43 59C43.8 59 44.5 59.7 44.5 60.5C44.5 61.3 43.8 62 43 62L29 62"
             "C28.2 62 27.5 61.3 27.5 60.5C27.5 59.7 28.2 59 29 59Z M36 25A3 3 0 1 1 36 31A3 3 0 1 1 36 25Z")},
        {QStringLiteral("glider"),
         QStringLiteral(
             "M36 14C37.6 14 38.5 15.8 38.5 18L38.5 29L66 30.2C67.1 30.3 68 31.2 68 32.3C68 33.4 67.1 34.3 66 34.3"
             "L38.5 34.6L37.6 58L45 58C46.1 58 47 58.9 47 60C47 61.1 46.1 62 45 62L37.3 62.3L36.7 64.6C36.5 65.3 35.5 "
             "65.3 35.3 64.6L34.7 62.3L27 62C25.9 62 25 61.1 25 60C25 58.9 25.9 58 27 58L34.4 58L33.5 34.6L6 34.3C4.9 "
             "34.3 4 33.4 4 32.3C4 31.2 4.9 30.3 6 30.2L33.5 29L33.5 18C33.5 15.8 34.4 14 36 14Z")},
        {QStringLiteral("balloon"),
         QStringLiteral(
             "M36 6C48 6 56 15 56 26C56 36.5 47 44 41 49L31 49C25 44 16 36.5 16 26C16 15 24 6 36 6Z "
             "M31.6 49L33 54L32 54L30.6 49Z M40.4 49L39 54L40 54L41.4 49Z M31.5 54L40.5 54C41.3 54 41.9 54.7 41.8 55.5"
             "L41 62C40.9 62.6 40.4 63 39.8 63L32.2 63C31.6 63 31.1 62.6 31 62L30.2 55.5C30.1 54.7 30.7 54 31.5 54Z")},
        {QStringLiteral("parachute"),
         QStringLiteral(
             "M8 30C8 17 20.5 8 36 8C51.5 8 64 17 64 30C59.3 27.5 54.7 27.5 50 30C45.3 27.5 40.7 27.5 36 30C31.3 27.5 "
             "26.7 27.5 22 30C17.3 27.5 12.7 27.5 8 30Z M8.8 30.4L32.2 50.4L31.2 51.6L7.8 31.6Z M63.2 30.4L39.8 50.4"
             "L40.8 51.6L64.2 31.6Z M35.2 30L36.8 30L36.8 50L35.2 50Z M32 49L40 49C41.1 49 42 49.9 42 51L42 61C42 62.1 "
             "41.1 63 40 63L32 63C30.9 63 30 62.1 30 61L30 51C30 49.9 30.9 49 32 49Z")},
        {QStringLiteral("drone"),
         QStringLiteral(
             "M18.41 15.59L56.41 53.59L53.59 56.41L15.59 18.41Z M56.41 18.41L18.41 56.41L15.59 53.59L53.59 15.59Z "
             "M17 8A9 9 0 1 1 17 26A9 9 0 1 1 17 8Z M55 8A9 9 0 1 1 55 26A9 9 0 1 1 55 8Z M17 46A9 9 0 1 1 17 64A9 9 0 "
             "1 1 17 46Z M55 46A9 9 0 1 1 55 64A9 9 0 1 1 55 46Z M30 27L42 27C43.7 27 45 28.3 45 30L45 42C45 43.7 43.7 "
             "45 42 45L30 45C28.3 45 27 43.7 27 42L27 30C27 28.3 28.3 27 30 27Z M36 19.5L40.5 25L31.5 25Z")},
        {QStringLiteral("vehicle"),
         QStringLiteral(
             "M29 10L43 10C45.2 10 47 11.8 47 14L47 58C47 60.2 45.2 62 43 62L29 62C26.8 62 25 60.2 25 58L25 14C25 11.8 "
             "26.8 10 29 10Z M21.5 17L25 17L25 26L21.5 26C20.7 26 20 25.3 20 24.5L20 18.5C20 17.7 20.7 17 21.5 17Z "
             "M47 17L50.5 17C51.3 17 52 17.7 52 18.5L52 24.5C52 25.3 51.3 26 50.5 26L47 26Z M21.5 46L25 46L25 55L21.5 "
             "55"
             "C20.7 55 20 54.3 20 53.5L20 47.5C20 46.7 20.7 46 21.5 46Z M47 46L50.5 46C51.3 46 52 46.7 52 47.5L52 53.5"
             "C52 54.3 51.3 55 50.5 55L47 55Z M28.5 22L43.5 22L42 28L30 28Z")},
        {QStringLiteral("emergency"),
         QStringLiteral(
             "M29 10L43 10C45.2 10 47 11.8 47 14L47 58C47 60.2 45.2 62 43 62L29 62C26.8 62 25 60.2 25 58L25 14C25 11.8 "
             "26.8 10 29 10Z M21.5 17L25 17L25 26L21.5 26C20.7 26 20 25.3 20 24.5L20 18.5C20 17.7 20.7 17 21.5 17Z "
             "M47 17L50.5 17C51.3 17 52 17.7 52 18.5L52 24.5C52 25.3 51.3 26 50.5 26L47 26Z M21.5 46L25 46L25 55L21.5 "
             "55"
             "C20.7 55 20 54.3 20 53.5L20 47.5C20 46.7 20.7 46 21.5 46Z M47 46L50.5 46C51.3 46 52 46.7 52 47.5L52 53.5"
             "C52 54.3 51.3 55 50.5 55L47 55Z M28.5 22L43.5 22L42 28L30 28Z M29 34L43 34L43 38L29 38Z")},
    };
    return paths;
}

}  // namespace

AdsbIconProvider::AdsbIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{}

QStringList AdsbIconProvider::iconNames()
{
    return iconPaths().keys();
}

QImage AdsbIconProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize)
{
    const QStringList parts = id.split(QLatin1Char('/'));
    const QString path = iconPaths().value(parts.value(0));
    if (path.isEmpty()) {
        qCWarning(AdsbIconProviderLog) << "no icon for" << id;
        return {};
    }

    // TitanPlanner's colors: orange, red for a high threat, translucent grey on the ground
    const QString state = parts.value(1);
    QColor fill(0xff, 0xa5, 0x00);
    QColor outline(0x1a, 0x1a, 0x1a);
    if (state == QLatin1String("danger")) {
        fill = QColor(0xff, 0x00, 0x00);
    } else if (state == QLatin1String("ground")) {
        fill = QColor(0xa9, 0xa9, 0xa9, 128);
        outline.setAlphaF(0.6f);
    }

    const QByteArray svg =
        QStringLiteral(
            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 %1 %1' width='%1' height='%1'><path d='%2' "
            "fill='%3' fill-opacity='%4' stroke='%5' stroke-opacity='%6' stroke-width='3' "
            "stroke-linejoin='round'/></svg>")
            .arg(kViewBoxSize)
            .arg(path, fill.name(), QString::number(fill.alphaF()), outline.name(), QString::number(outline.alphaF()))
            .toUtf8();
    QSvgRenderer renderer(svg);
    if (!renderer.isValid()) {
        qCWarning(AdsbIconProviderLog) << "could not draw" << id;
        return {};
    }

    const QSize imageSize =
        (requestedSize.width() > 0) && (requestedSize.height() > 0) ? requestedSize : QSize(kViewBoxSize, kViewBoxSize);
    QImage image(imageSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter);
    painter.end();

    if (size) {
        *size = imageSize;
    }
    return image;
}
