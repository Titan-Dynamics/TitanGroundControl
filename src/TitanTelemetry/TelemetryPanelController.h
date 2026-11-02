#pragma once

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtCore/QVariantList>
#include <QtQmlIntegration/QtQmlIntegration>

class QmlObjectListModel;
class TelemetryPanelItem;
class Vehicle;

/// The Fly View telemetry panel's tiles for the active vehicle, laid out per vehicle type and saved as they are edited.
/// Without a vehicle the tiles follow the offline editing vehicle, so the panel can be set up beforehand.
class TelemetryPanelController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_MOC_INCLUDE("QmlObjectListModel.h")
    Q_MOC_INCLUDE("TelemetryPanelItem.h")
    Q_PROPERTY(QmlObjectListModel* items READ items CONSTANT)
    Q_PROPERTY(int columns READ columns WRITE setColumns NOTIFY columnsChanged)
    Q_PROPERTY(TextSize textSize READ textSize WRITE setTextSize NOTIFY textSizeChanged)
    Q_PROPERTY(bool hasBars READ hasBars NOTIFY hasBarsChanged)  ///< Any tile shows a bar
    Q_PROPERTY(int minColumns READ minColumns CONSTANT)
    Q_PROPERTY(int maxColumns READ maxColumns CONSTANT)

public:
    enum TextSize
    {
        SmallText,
        MediumText,
        LargeText
    };
    Q_ENUM(TextSize)

    explicit TelemetryPanelController(QObject* parent = nullptr);
    ~TelemetryPanelController() override;

    /// Adds a tile at the end, showing relative altitude until it is set up
    Q_INVOKABLE TelemetryPanelItem* appendItem();
    Q_INVOKABLE void removeItem(TelemetryPanelItem* item);
    Q_INVOKABLE void moveItem(int from, int to);
    Q_INVOKABLE void resetToDefaults();
    /// Every value the vehicle reports, to choose one from: group, groupLabel, name, description and units of each
    Q_INVOKABLE QVariantList valueCatalog() const;

    QmlObjectListModel* items() const { return _items; }

    int columns() const { return _columns; }

    TextSize textSize() const { return _textSize; }

    bool hasBars() const { return _hasBars; }

    static int minColumns() { return kMinColumns; }

    static int maxColumns() { return kMaxColumns; }

    void setColumns(int columns);
    void setTextSize(TextSize textSize);

    static constexpr int kMinColumns = 1;
    static constexpr int kMaxColumns = 8;

signals:
    void columnsChanged();
    void textSizeChanged();
    void hasBarsChanged();

private:
    void _setVehicle(Vehicle* vehicle);
    void _load();
    void _save();
    void _flushSave();
    void _scheduleSave();
    void _createDefaults();
    void _updateHasBars();
    TelemetryPanelItem* _addItem(const QString& factGroupName, const QString& factName);
    TelemetryPanelItem* _newItem();
    QString _settingsKey() const;

    QmlObjectListModel* _items = nullptr;
    QPointer<Vehicle> _vehicle;
    QString _layoutKey;  ///< Where the tiles on show were loaded from, as the vehicle type can change under them
    int _columns = 3;
    TextSize _textSize = MediumText;
    bool _hasBars = false;
    bool _loading = false;
    QTimer _saveTimer;
};
