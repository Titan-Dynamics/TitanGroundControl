#include "Map3DClouds.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QtCore/QtMath>

namespace Map3D::Clouds {

namespace {

constexpr int kOctaves = 5;
constexpr int kBasePeriod = 4;
constexpr double kPersistence = 0.5;
constexpr int kCoverGrid = 120;
constexpr int kCoverSearchSteps = 30;

/// .NET Framework's System.Random(seed) (Knuth's subtractive generator), so the noise matches Mission Planner's.
class DotNetRandom
{
public:
    explicit DotNetRandom(int seed)
    {
        const int subtraction =
            (seed == std::numeric_limits<int>::min()) ? std::numeric_limits<int>::max() : std::abs(seed);
        int mj = kSeed - subtraction;
        _seedArray[55] = mj;
        int mk = 1;
        for (int i = 1; i < 55; i++) {
            const int ii = (21 * i) % 55;
            _seedArray[ii] = mk;
            mk = mj - mk;
            if (mk < 0) {
                mk += kBig;
            }
            mj = _seedArray[ii];
        }
        for (int k = 1; k < 5; k++) {
            for (int i = 1; i < 56; i++) {
                _seedArray[i] -= _seedArray[1 + (i + 30) % 55];
                if (_seedArray[i] < 0) {
                    _seedArray[i] += kBig;
                }
            }
        }
    }

    double nextDouble() { return _internalSample() * (1.0 / kBig); }

private:
    int _internalSample()
    {
        int locNext = _next + 1;
        int locNextp = _nextp + 1;
        if (locNext >= 56) {
            locNext = 1;
        }
        if (locNextp >= 56) {
            locNextp = 1;
        }
        int value = _seedArray[locNext] - _seedArray[locNextp];
        if (value == kBig) {
            value--;
        }
        if (value < 0) {
            value += kBig;
        }
        _seedArray[locNext] = value;
        _next = locNext;
        _nextp = locNextp;
        return value;
    }

    static constexpr int kBig = std::numeric_limits<int>::max();
    static constexpr int kSeed = 161803398;
    int _seedArray[56] = {};
    int _next = 0;
    int _nextp = 21;
};

double smoothStep(double edge0, double edge1, double x)
{
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

double texel(const std::vector<float>& texture, int size, int x, int y)
{
    return texture[static_cast<size_t>(((y % size + size) % size)) * size + ((x % size + size) % size)];
}

// The texture as the GPU samples it: wrapping, bilinear between texel centres
double sampleTexture(const std::vector<float>& texture, int size, double u, double v)
{
    const double x = (u - std::floor(u)) * size - 0.5;
    const double y = (v - std::floor(v)) * size - 0.5;
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const double fx = x - x0;
    const double fy = y - y0;
    const double top =
        texel(texture, size, x0, y0) + (texel(texture, size, x0 + 1, y0) - texel(texture, size, x0, y0)) * fx;
    const double bottom = texel(texture, size, x0, y0 + 1) +
                          (texel(texture, size, x0 + 1, y0 + 1) - texel(texture, size, x0, y0 + 1)) * fx;
    return top + (bottom - top) * fy;
}

// The shader's density and weather at the grid's points, as pairs
std::vector<double> samples(const Layer& layer, const std::vector<float>& texture, int size)
{
    std::vector<double> result(static_cast<size_t>(kCoverGrid) * kCoverGrid * 2);
    const double span = 1 / kWeatherScale;
    const double detail = 1 / layer.detailScale;
    size_t o = 0;
    for (int y = 0; y < kCoverGrid; y++) {
        for (int x = 0; x < kCoverGrid; x++) {
            const double u = (x + 0.5) / kCoverGrid * span;
            const double v = (y + 0.5) / kCoverGrid * span;
            const double shape = sampleTexture(texture, size, u, v);
            result[o++] = shape + (sampleTexture(texture, size, u * detail, v * detail) - shape) * kDetailWeight;
            result[o++] = sampleTexture(texture, size, u * kWeatherScale, v * kWeatherScale);
        }
    }
    return result;
}

// The mean of the shader's cloud term over the samples
double coverOf(const Layer& layer, const std::vector<double>& points, double coverage)
{
    double sum = 0;
    for (size_t i = 0; i < points.size(); i += 2) {
        const double c = coverage + (0.5 - points[i + 1]) * 2 * kWeatherStrength;
        sum += smoothStep(c, c + layer.softness, points[i]);
    }
    return sum / (points.size() / 2);
}

double fade(double t)
{
    return t * t * t * (t * (t * 6 - 15) + 10);
}

int hashLattice(int x, int y, int period, int octave)
{
    x = ((x % period) + period) % period;
    y = ((y % period) + period) % period;
    // Mirrors the C# unchecked int arithmetic and arithmetic shifts
    const auto wrap = [](qint64 value) { return static_cast<qint32>(static_cast<quint32>(value)); };
    qint32 h = wrap(static_cast<qint64>(x) * 374761393 + static_cast<qint64>(y) * 668265263 +
                    static_cast<qint64>(octave) * 1274126177);
    h = wrap(static_cast<qint64>(h ^ (h >> 13)) * 1274126177);
    return (h ^ (h >> 16)) & 255;
}

double dotGradient(const std::vector<double>& gradients, int index, double x, double y)
{
    return gradients[index * 2] * x + gradients[index * 2 + 1] * y;
}

// Gradient noise on a lattice that repeats every period cells, so the texture wraps
double noise(double x, double y, int period, const std::vector<double>& gradients, int octave)
{
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const double fx = x - x0;
    const double fy = y - y0;
    const double u = fade(fx);
    const double v = fade(fy);
    const double n00 = dotGradient(gradients, hashLattice(x0, y0, period, octave), fx, fy);
    const double n10 = dotGradient(gradients, hashLattice(x0 + 1, y0, period, octave), fx - 1, fy);
    const double n01 = dotGradient(gradients, hashLattice(x0, y0 + 1, period, octave), fx, fy - 1);
    const double n11 = dotGradient(gradients, hashLattice(x0 + 1, y0 + 1, period, octave), fx - 1, fy - 1);
    const double nx0 = n00 + (n10 - n00) * u;
    const double nx1 = n01 + (n11 - n01) * u;
    return nx0 + (nx1 - nx0) * v;
}

std::vector<double> gradients(int seed)
{
    DotNetRandom random(seed);
    std::vector<double> result(256 * 2);
    for (int i = 0; i < 256; i++) {
        const double angle = random.nextDouble() * M_PI * 2;
        result[i * 2] = std::cos(angle);
        result[i * 2 + 1] = std::sin(angle);
    }
    return result;
}

}  // namespace

std::vector<float> coverages(const std::vector<float>& texture, int size, int density)
{
    std::vector<float> result(kLayerCount);
    for (int i = 0; i < kLayerCount; i++) {
        const Layer& layer = kLayers[i];
        const std::vector<double> points = samples(layer, texture, size);
        const double target = coverOf(layer, points, layer.coverage) * std::max(0, density) / kMaxDensity;
        // More coverage, less cloud: halve the range to the threshold that covers the target share
        double lo = -1;
        double hi = 2;
        for (int step = 0; step < kCoverSearchSteps; step++) {
            const double mid = (lo + hi) / 2;
            if (coverOf(layer, points, mid) > target) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        result[i] = static_cast<float>((lo + hi) / 2);
    }
    return result;
}

double cover(const Layer& layer, const std::vector<float>& texture, int size, double coverage)
{
    return coverOf(layer, samples(layer, texture, size), coverage);
}

double baseAltitude(double groundHeight, double vehicleAltitude)
{
    return std::max(groundHeight + kMinimumAboveGround, vehicleAltitude + kClearanceAboveVehicle);
}

QByteArray texture(int size, int seed, std::vector<float>& density)
{
    const std::vector<double> grads = gradients(seed);
    std::vector<double> raw(static_cast<size_t>(size) * size);
    double minValue = std::numeric_limits<double>::max();
    double maxValue = std::numeric_limits<double>::lowest();
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            double sum = 0;
            double amplitude = 1;
            int period = kBasePeriod;
            for (int o = 0; o < kOctaves; o++) {
                sum += amplitude * noise(static_cast<double>(x) / size * period, static_cast<double>(y) / size * period,
                                         period, grads, o);
                amplitude *= kPersistence;
                period *= 2;
            }
            raw[static_cast<size_t>(y) * size + x] = sum;
            minValue = std::min(minValue, sum);
            maxValue = std::max(maxValue, sum);
        }
    }

    density.resize(raw.size());
    QByteArray pixels(static_cast<qsizetype>(raw.size()), Qt::Uninitialized);
    for (size_t i = 0; i < raw.size(); i++) {
        const float value = static_cast<float>((raw[i] - minValue) / std::max(1e-9, maxValue - minValue));
        density[i] = value;
        pixels[static_cast<qsizetype>(i)] = static_cast<char>(static_cast<quint8>(std::lround(value * 255)));
    }
    return pixels;
}

}  // namespace Map3D::Clouds
