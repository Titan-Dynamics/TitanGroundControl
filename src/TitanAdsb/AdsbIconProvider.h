#pragma once

#include <QtQuick/QQuickImageProvider>

/// Draws the Fly View map's traffic icons: a vector silhouette for each kind of aircraft, in a 72 x 72 box nose up,
/// filled in the traffic's state color with a dark outline that keeps it readable over imagery. Image URLs are
/// image://titanadsb/<icon>/<state>, the state normal, danger or ground. QML's pixmap cache keeps one image for each
/// icon, state and size, shared by every aircraft that shows it, so traffic draws as batched textures rather than as
/// a vector shape each.
class AdsbIconProvider : public QQuickImageProvider
{
public:
    static constexpr const char* ProviderId = "titanadsb";

    AdsbIconProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) final;

    /// The icons there are, such as "airliner" and "helicopter"
    static QStringList iconNames();

    static constexpr int kViewBoxSize = 72;
};
