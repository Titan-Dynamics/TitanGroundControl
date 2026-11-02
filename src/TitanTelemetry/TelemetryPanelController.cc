#include "TelemetryPanelController.h"

#include <algorithm>

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSettings>
#include <QtCore/QVariantMap>

#include "Fact.h"
#include "FactGroup.h"
#include "MultiVehicleManager.h"
#include "QGCMAVLink.h"
#include "QmlObjectListModel.h"
#include "ScreenToolsController.h"
#include "TelemetryPanelItem.h"
#include "Vehicle.h"

namespace {
constexpr const char* kSettingsGroup = "TitanTelemetryPanel";
constexpr const char* kVehicleGroup = "Vehicle";
constexpr int kLayoutVersion = 1;
/// Every settings write rewrites the settings file, so edits are saved once they settle
constexpr int kSaveDelayMs = 500;
/// What every vehicle starts with, and returns to on reset, in the saved layout's form: the values to fly by over two
/// rows, then the battery
constexpr const char* kDefaultLayout = R"({
    "columns": 4,
    "items": [
        {"group": "Vehicle", "name": "AltitudeRelative"},
        {"group": "Vehicle", "name": "AirSpeed"},
        {"group": "Vehicle", "name": "FlightTime"},
        {"group": "Vehicle", "name": "ClimbRate"},
        {"group": "Vehicle", "name": "rcRSSI", "style": "bar", "percent": true, "units": false},
        {"group": "Vehicle", "name": "DistanceToHome"},
        {"group": "Vehicle", "name": "FlightDistance"},
        {"group": "Vehicle", "name": "GroundSpeed"},
        {"group": "battery0", "name": "mahConsumed"},
        {"group": "battery0", "name": "current"},
        {"group": "battery0", "name": "instantPower"},
        {"group": "battery0", "name": "whPerKm", "label": "Wh/km"}
    ]
})";

/// "distanceSensor" reads "Distance Sensor", "gps2" "GPS 2"
QString groupLabel(const QString& name)
{
    static const QStringList initialisms = {QStringLiteral("adsb"), QStringLiteral("efi"), QStringLiteral("esc"),
                                            QStringLiteral("gps"),  QStringLiteral("rc"),  QStringLiteral("rpm")};
    QStringList words;
    QString word;
    for (const QChar character : name) {
        const bool startsWord = !word.isEmpty() && ((character.isUpper() && word.back().isLower()) ||
                                                    (character.isDigit() != word.back().isDigit()));
        if (startsWord) {
            words.append(word);
            word.clear();
        }
        word.append(character);
    }
    if (!word.isEmpty()) {
        words.append(word);
    }
    for (QString& part : words) {
        if (initialisms.contains(part.toLower())) {
            part = part.toUpper();
        } else {
            part[0] = part[0].toUpper();
        }
    }
    return words.join(QLatin1Char(' '));
}
}  // namespace

TelemetryPanelController::TelemetryPanelController(QObject* parent)
    : QObject(parent)
    , _items(new QmlObjectListModel(this))
{
    _saveTimer.setSingleShot(true);
    _saveTimer.setInterval(kSaveDelayMs);
    (void) connect(&_saveTimer, &QTimer::timeout, this, &TelemetryPanelController::_save);

    MultiVehicleManager* const manager = MultiVehicleManager::instance();
    (void) connect(manager, &MultiVehicleManager::activeVehicleChanged, this, &TelemetryPanelController::_setVehicle);
    _setVehicle(manager->activeVehicle());
}

TelemetryPanelController::~TelemetryPanelController()
{
    _flushSave();
}

void TelemetryPanelController::_setVehicle(Vehicle* vehicle)
{
    if (!vehicle) {
        vehicle = MultiVehicleManager::instance()->offlineEditingVehicle();
    }
    if (vehicle == _vehicle) {
        return;
    }
    _flushSave();
    if (_vehicle) {
        (void) disconnect(_vehicle, &Vehicle::vehicleTypeChanged, this, nullptr);
    }
    _vehicle = vehicle;
    if (_vehicle) {
        // Each vehicle type keeps its own layout
        (void) connect(_vehicle, &Vehicle::vehicleTypeChanged, this, [this]() {
            _flushSave();
            _load();
        });
    }
    _load();
}

QString TelemetryPanelController::_settingsKey() const
{
    const QGCMAVLink::VehicleClass_t vehicleClass =
        _vehicle ? QGCMAVLink::vehicleClass(_vehicle->vehicleType()) : QGCMAVLink::VehicleClassGeneric;
    return QStringLiteral("layout-%1").arg(static_cast<int>(vehicleClass));
}

TelemetryPanelItem* TelemetryPanelController::_newItem()
{
    auto* const item = new TelemetryPanelItem(_items);
    item->setVehicle(_vehicle);
    (void) connect(item, &TelemetryPanelItem::settingsChanged, this, &TelemetryPanelController::_scheduleSave);
    (void) connect(item, &TelemetryPanelItem::settingsChanged, this, &TelemetryPanelController::_updateHasBars);
    return item;
}

TelemetryPanelItem* TelemetryPanelController::_addItem(const QString& factGroupName, const QString& factName)
{
    TelemetryPanelItem* const item = _newItem();
    item->setFact(factGroupName, factName);
    _items->append(item);
    _updateHasBars();
    _scheduleSave();
    return item;
}

void TelemetryPanelController::_load()
{
    _loading = true;
    _items->clearAndDeleteContents();
    _layoutKey = _settingsKey();

    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    const QJsonObject layout = QJsonDocument::fromJson(settings.value(_layoutKey).toByteArray()).object();
    if (layout.value(QStringLiteral("version")).toInt() == kLayoutVersion) {
        _columns = std::clamp(layout.value(QStringLiteral("columns")).toInt(3), kMinColumns, kMaxColumns);
        _textSize = static_cast<TextSize>(std::clamp(layout.value(QStringLiteral("textSize")).toInt(MediumText),
                                                     static_cast<int>(SmallText), static_cast<int>(LargeText)));
        const QJsonArray items = layout.value(QStringLiteral("items")).toArray();
        for (const QJsonValue& value : items) {
            TelemetryPanelItem* const item = _newItem();
            item->fromJson(value.toObject());
            _items->append(item);
        }
    } else {
        _createDefaults();
    }
    _loading = false;

    emit columnsChanged();
    emit textSizeChanged();
    _updateHasBars();
}

void TelemetryPanelController::_createDefaults()
{
    const QJsonObject layout = QJsonDocument::fromJson(QByteArray(kDefaultLayout)).object();
    _columns = std::clamp(layout.value(QStringLiteral("columns")).toInt(4), kMinColumns, kMaxColumns);
    _textSize = ScreenToolsController::isMobile() ? SmallText : MediumText;
    const QJsonArray items = layout.value(QStringLiteral("items")).toArray();
    for (const QJsonValue& value : items) {
        TelemetryPanelItem* const item = _newItem();
        item->fromJson(value.toObject());
        _items->append(item);
    }
}

void TelemetryPanelController::_scheduleSave()
{
    if (!_loading) {
        _saveTimer.start();
    }
}

void TelemetryPanelController::_flushSave()
{
    if (_saveTimer.isActive()) {
        _saveTimer.stop();
        _save();
    }
}

void TelemetryPanelController::_save()
{
    QJsonArray items;
    for (int index = 0; index < _items->count(); index++) {
        items.append(_items->value<TelemetryPanelItem*>(index)->toJson());
    }
    const QJsonObject layout{
        {QStringLiteral("version"), kLayoutVersion},
        {QStringLiteral("columns"), _columns},
        {QStringLiteral("textSize"), static_cast<int>(_textSize)},
        {QStringLiteral("items"), items},
    };
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    settings.setValue(_layoutKey, QString::fromUtf8(QJsonDocument(layout).toJson(QJsonDocument::Compact)));
}

void TelemetryPanelController::_updateHasBars()
{
    bool hasBars = false;
    for (int index = 0; index < _items->count(); index++) {
        hasBars = hasBars || (_items->value<TelemetryPanelItem*>(index)->style() == TelemetryPanelItem::BarStyle);
    }
    if (hasBars != _hasBars) {
        _hasBars = hasBars;
        emit hasBarsChanged();
    }
}

TelemetryPanelItem* TelemetryPanelController::appendItem()
{
    return _addItem(QString::fromLatin1(kVehicleGroup), QStringLiteral("AltitudeRelative"));
}

void TelemetryPanelController::removeItem(TelemetryPanelItem* item)
{
    const int index = _items->indexOf(item);
    if (index < 0) {
        return;
    }
    _items->removeAt(index)->deleteLater();
    _updateHasBars();
    _scheduleSave();
}

void TelemetryPanelController::moveItem(int from, int to)
{
    if ((from == to) || (from < 0) || (to < 0) || (from >= _items->count()) || (to >= _items->count())) {
        return;
    }
    _items->move(from, to);
    _scheduleSave();
}

void TelemetryPanelController::resetToDefaults()
{
    _saveTimer.stop();
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    settings.remove(_layoutKey);
    _load();
}

void TelemetryPanelController::setColumns(int columns)
{
    columns = std::clamp(columns, kMinColumns, kMaxColumns);
    if (columns == _columns) {
        return;
    }
    _columns = columns;
    emit columnsChanged();
    _scheduleSave();
}

void TelemetryPanelController::setTextSize(TextSize textSize)
{
    if (textSize == _textSize) {
        return;
    }
    _textSize = textSize;
    emit textSizeChanged();
    _scheduleSave();
}

QVariantList TelemetryPanelController::valueCatalog() const
{
    QVariantList catalog;
    if (!_vehicle) {
        return catalog;
    }

    auto addGroup = [&catalog](const QString& groupName, const QString& label, FactGroup* group) {
        QVariantList values;
        for (const QString& name : group->factNames()) {
            Fact* const fact = group->getFact(name);
            if (!fact) {
                continue;
            }
            const QString description = fact->shortDescription().isEmpty() ? name : fact->shortDescription();
            values.append(QVariantMap{
                {QStringLiteral("group"), groupName},
                {QStringLiteral("groupLabel"), label},
                {QStringLiteral("name"), name},
                {QStringLiteral("description"), description},
                {QStringLiteral("units"), fact->cookedUnits()},
            });
        }
        std::sort(values.begin(), values.end(), [](const QVariant& left, const QVariant& right) {
            return left.toMap()
                       .value(QStringLiteral("description"))
                       .toString()
                       .compare(right.toMap().value(QStringLiteral("description")).toString(), Qt::CaseInsensitive) < 0;
        });
        catalog.append(values);
    };

    addGroup(QString::fromLatin1(kVehicleGroup), tr("Vehicle"), _vehicle);
    QStringList groupNames = _vehicle->factGroupNames();
    std::sort(groupNames.begin(), groupNames.end(), [](const QString& left, const QString& right) {
        return groupLabel(left).compare(groupLabel(right), Qt::CaseInsensitive) < 0;
    });
    for (const QString& groupName : groupNames) {
        if (FactGroup* const group = _vehicle->getFactGroup(groupName)) {
            addGroup(groupName, groupLabel(groupName), group);
        }
    }
    return catalog;
}
