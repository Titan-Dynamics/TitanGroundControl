#include "Map3DModel.h"

#include <cmath>
#include <cstring>

#include <QtCore/QFile>
#include <QtCore/QtEndian>

#include "QGCMAVLink.h"

namespace Map3D::Model {

namespace {

void appendVertex(std::vector<float>& out, float x, float y, float z, float nx, float ny, float nz)
{
    // Rotate -90 degrees about Z: x' = y, y' = -x
    out.insert(out.end(), {y, -x, z, ny, -nx, nz});
}

bool parseBinary(const QByteArray& data, std::vector<float>& out)
{
    if (data.size() < 84) {
        return false;
    }
    const quint32 count = qFromLittleEndian<quint32>(data.constData() + 80);
    if (static_cast<qint64>(data.size()) != 84 + static_cast<qint64>(count) * 50) {
        return false;
    }
    out.reserve(static_cast<size_t>(count) * 18);
    const char* p = data.constData() + 84;
    for (quint32 t = 0; t < count; t++, p += 50) {
        float values[12];
        for (int i = 0; i < 12; i++) {
            values[i] = qFromLittleEndian<float>(p + i * 4);
        }
        for (int v = 0; v < 3; v++) {
            appendVertex(out, values[3 + v * 3], values[4 + v * 3], values[5 + v * 3], values[0], values[1], values[2]);
        }
    }
    return true;
}

bool parseAscii(const QByteArray& data, std::vector<float>& out)
{
    float nx = 0;
    float ny = 0;
    float nz = 0;
    for (const QByteArray& rawLine : data.split('\n')) {
        const QByteArray line = rawLine.trimmed();
        if (line.startsWith("facet normal")) {
            const QList<QByteArray> parts = line.simplified().split(' ');
            if (parts.size() >= 5) {
                nx = parts[2].toFloat();
                ny = parts[3].toFloat();
                nz = parts[4].toFloat();
            }
        } else if (line.startsWith("vertex")) {
            const QList<QByteArray> parts = line.simplified().split(' ');
            if (parts.size() >= 4) {
                appendVertex(out, parts[1].toFloat(), parts[2].toFloat(), parts[3].toFloat(), nx, ny, nz);
            }
        }
    }
    return !out.empty() && (out.size() % 18 == 0);
}

}  // namespace

QString defaultModelPath(MAV_TYPE vehicleType, bool quadPlane)
{
    QString name = QStringLiteral("plane");
    if (quadPlane || QGCMAVLink::isVTOL(vehicleType)) {
        name = QStringLiteral("quadplane");
    } else {
        switch (vehicleType) {
            case MAV_TYPE_QUADROTOR:
            case MAV_TYPE_HEXAROTOR:
            case MAV_TYPE_OCTOROTOR:
            case MAV_TYPE_TRICOPTER:
            case MAV_TYPE_COAXIAL:
            case MAV_TYPE_DODECAROTOR:
                name = QStringLiteral("copter");
                break;
            case MAV_TYPE_HELICOPTER:
                name = QStringLiteral("helicopter");
                break;
            case MAV_TYPE_GROUND_ROVER:
                name = QStringLiteral("rover");
                break;
            case MAV_TYPE_SURFACE_BOAT:
                name = QStringLiteral("boat");
                break;
            case MAV_TYPE_SUBMARINE:
                name = QStringLiteral("sub");
                break;
            default:
                break;
        }
    }
    return QStringLiteral(":/qml/QGroundControl/TitanMap3D/models/%1.stl").arg(name);
}

std::vector<float> loadStl(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QFile::ReadOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return {};
    }
    const QByteArray data = file.readAll();
    std::vector<float> out;
    if (!parseBinary(data, out)) {
        out.clear();
        if (!parseAscii(data, out)) {
            if (error) {
                *error = QStringLiteral("not an STL file");
            }
            return {};
        }
    }

    // Facet normals are unit length in ASCII files but not always in binary ones
    for (size_t i = 0; i + 5 < out.size(); i += 6) {
        const float length = std::sqrt(out[i + 3] * out[i + 3] + out[i + 4] * out[i + 4] + out[i + 5] * out[i + 5]);
        if (length > 1e-12f) {
            out[i + 3] /= length;
            out[i + 4] /= length;
            out[i + 5] /= length;
        }
    }
    return out;
}

}  // namespace Map3D::Model
