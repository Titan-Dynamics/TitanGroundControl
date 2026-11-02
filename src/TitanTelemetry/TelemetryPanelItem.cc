#include "TelemetryPanelItem.h"

#include <algorithm>
#include <cmath>

#include "Fact.h"
#include "FactGroup.h"
#include "Vehicle.h"

namespace {
constexpr const char* kVehicleGroup = "Vehicle";

/// The smallest 1-2-5 step of at least minimum
double niceCeiling(double minimum)
{
    const double magnitude = std::pow(10.0, std::floor(std::log10(std::max(minimum, 1e-6))));
    for (const double factor : {1.0, 2.0, 5.0}) {
        if (factor * magnitude >= minimum) {
            return factor * magnitude;
        }
    }
    return 10.0 * magnitude;
}

QString styleKey(TelemetryPanelItem::Style style)
{
    return style == TelemetryPanelItem::BarStyle ? QStringLiteral("bar") : QStringLiteral("number");
}

QString alertKey(TelemetryPanelItem::Alert alert)
{
    switch (alert) {
        case TelemetryPanelItem::AlertAbove:
            return QStringLiteral("above");
        case TelemetryPanelItem::AlertBelow:
            return QStringLiteral("below");
        case TelemetryPanelItem::NoAlert:
            break;
    }
    return QStringLiteral("off");
}
}  // namespace

TelemetryPanelItem::TelemetryPanelItem(QObject* parent)
    : QObject(parent)
{}

QString TelemetryPanelItem::displayLabel() const
{
    if (!_label.isEmpty()) {
        return _label;
    }
    if (_fact && !_fact->shortDescription().isEmpty()) {
        return _fact->shortDescription();
    }
    return _factName;
}

void TelemetryPanelItem::setFact(const QString& factGroupName, const QString& factName)
{
    if ((factGroupName == _factGroupName) && (factName == _factName)) {
        return;
    }
    _factGroupName = factGroupName;
    _factName = factName;
    _resolveFact();
    emit settingsChanged();
}

void TelemetryPanelItem::setVehicle(Vehicle* vehicle)
{
    if (_vehicle == vehicle) {
        return;
    }
    if (_vehicle) {
        (void) disconnect(_vehicle, &FactGroup::factGroupNamesChanged, this, &TelemetryPanelItem::_resolveFact);
    }
    _vehicle = vehicle;
    if (_vehicle) {
        // Groups such as a battery's appear once the vehicle first reports them
        (void) connect(_vehicle, &FactGroup::factGroupNamesChanged, this, &TelemetryPanelItem::_resolveFact);
    }
    _resolveFact();
}

void TelemetryPanelItem::_resolveFact()
{
    Fact* fact = nullptr;
    if (_vehicle && !_factName.isEmpty()) {
        FactGroup* group = nullptr;
        if (_factGroupName == QLatin1String(kVehicleGroup)) {
            group = _vehicle;
        } else if (!_factGroupName.isEmpty()) {
            // Asking for a group the vehicle has not reported yet logs a warning
            const QString key = _factGroupName.at(0).toLower() + _factGroupName.mid(1);
            if (_vehicle->factGroups().contains(key) || _vehicle->factGroups().contains(_factGroupName)) {
                group = _vehicle->getFactGroup(_factGroupName);
            }
        }
        if (group && group->factExists(_factName)) {
            fact = group->getFact(_factName);
        }
    }
    if (fact == _fact) {
        return;
    }
    if (_fact) {
        (void) disconnect(_fact, nullptr, this, nullptr);
    }
    _fact = fact;
    if (_fact) {
        (void) connect(_fact, &Fact::rawValueChanged, this, &TelemetryPanelItem::_updateState);
    }
    emit factChanged();
    emit displayLabelChanged();
    _updateState();
}

void TelemetryPanelItem::_updateState()
{
    const double value = _fact ? _fact->cookedValue().toDouble() : qQNaN();

    int level = 0;
    if (std::isfinite(value) && (_alert != NoAlert)) {
        auto beyond = [this, value](double limit) {
            return std::isfinite(limit) && ((_alert == AlertAbove) ? value >= limit : value <= limit);
        };
        level = beyond(_warningValue) ? 2 : beyond(_cautionValue) ? 1 : 0;
    }

    double fraction = 0.0;
    if (std::isfinite(value) && (_barMaximum > _barMinimum)) {
        fraction = std::clamp((value - _barMinimum) / (_barMaximum - _barMinimum), 0.0, 1.0);
    }

    if ((level != _level) || (fraction != _fraction)) {
        _level = level;
        _fraction = fraction;
        emit stateChanged();
    }
}

void TelemetryPanelItem::suggestBarRange()
{
    if (!_fact) {
        return;
    }
    double minimum = 0.0;
    double maximum = 100.0;
    const double factMinimum = _fact->cookedMin().toDouble();
    const double factMaximum = _fact->cookedMax().toDouble();
    if (!_fact->minIsDefaultForType() && !_fact->maxIsDefaultForType() && std::isfinite(factMinimum) &&
        std::isfinite(factMaximum) && (factMaximum > factMinimum)) {
        minimum = factMinimum;
        maximum = factMaximum;
    } else if (_fact->cookedUnits() != QLatin1String("%")) {
        const double value = _fact->cookedValue().toDouble();
        if (std::isfinite(value) && (value != 0.0)) {
            const double span = niceCeiling(2.0 * std::abs(value));
            minimum = (value < 0.0) ? -span : 0.0;
            maximum = span;
        }
    }
    setBarMinimum(minimum);
    setBarMaximum(maximum);
}

void TelemetryPanelItem::setLabel(const QString& label)
{
    if (label == _label) {
        return;
    }
    _label = label;
    emit settingsChanged();
    emit displayLabelChanged();
}

void TelemetryPanelItem::setShowUnits(bool showUnits)
{
    if (showUnits == _showUnits) {
        return;
    }
    _showUnits = showUnits;
    emit settingsChanged();
}

void TelemetryPanelItem::setShowPercentage(bool showPercentage)
{
    if (showPercentage == _showPercentage) {
        return;
    }
    _showPercentage = showPercentage;
    emit settingsChanged();
}

void TelemetryPanelItem::setStyle(Style style)
{
    if (style == _style) {
        return;
    }
    _style = style;
    emit settingsChanged();
}

void TelemetryPanelItem::setBarMinimum(double minimum)
{
    if (!std::isfinite(minimum) || (minimum == _barMinimum)) {
        return;
    }
    _barMinimum = minimum;
    emit settingsChanged();
    _updateState();
}

void TelemetryPanelItem::setBarMaximum(double maximum)
{
    if (!std::isfinite(maximum) || (maximum == _barMaximum)) {
        return;
    }
    _barMaximum = maximum;
    emit settingsChanged();
    _updateState();
}

void TelemetryPanelItem::setAlert(Alert alert)
{
    if (alert == _alert) {
        return;
    }
    _alert = alert;
    emit settingsChanged();
    _updateState();
}

void TelemetryPanelItem::setCautionValue(double value)
{
    if ((value == _cautionValue) || (std::isnan(value) && std::isnan(_cautionValue))) {
        return;
    }
    _cautionValue = value;
    emit settingsChanged();
    _updateState();
}

void TelemetryPanelItem::setWarningValue(double value)
{
    if ((value == _warningValue) || (std::isnan(value) && std::isnan(_warningValue))) {
        return;
    }
    _warningValue = value;
    emit settingsChanged();
    _updateState();
}

QJsonObject TelemetryPanelItem::toJson() const
{
    QJsonObject json{
        {QStringLiteral("group"), _factGroupName},
        {QStringLiteral("name"), _factName},
        {QStringLiteral("label"), _label},
        {QStringLiteral("units"), _showUnits},
        {QStringLiteral("percent"), _showPercentage},
        {QStringLiteral("style"), styleKey(_style)},
        {QStringLiteral("barMin"), _barMinimum},
        {QStringLiteral("barMax"), _barMaximum},
        {QStringLiteral("alert"), alertKey(_alert)},
    };
    // JSON has no NaN; a missing value is off
    if (std::isfinite(_cautionValue)) {
        json.insert(QStringLiteral("caution"), _cautionValue);
    }
    if (std::isfinite(_warningValue)) {
        json.insert(QStringLiteral("warning"), _warningValue);
    }
    return json;
}

void TelemetryPanelItem::fromJson(const QJsonObject& json)
{
    _factGroupName = json.value(QStringLiteral("group")).toString(QString::fromLatin1(kVehicleGroup));
    _factName = json.value(QStringLiteral("name")).toString();
    _label = json.value(QStringLiteral("label")).toString();
    _showUnits = json.value(QStringLiteral("units")).toBool(true);
    _showPercentage = json.value(QStringLiteral("percent")).toBool(false);
    _style = (json.value(QStringLiteral("style")).toString() == QLatin1String("bar")) ? BarStyle : NumberStyle;
    _barMinimum = json.value(QStringLiteral("barMin")).toDouble(0.0);
    _barMaximum = json.value(QStringLiteral("barMax")).toDouble(100.0);
    const QString alert = json.value(QStringLiteral("alert")).toString();
    _alert = (alert == QLatin1String("above")) ? AlertAbove : (alert == QLatin1String("below")) ? AlertBelow : NoAlert;
    _cautionValue = json.value(QStringLiteral("caution")).toDouble(qQNaN());
    _warningValue = json.value(QStringLiteral("warning")).toDouble(qQNaN());
    _resolveFact();
    emit settingsChanged();
    emit displayLabelChanged();
    _updateState();
}
