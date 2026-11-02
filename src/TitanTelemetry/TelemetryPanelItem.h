#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QtNumeric>
#include <QtQmlIntegration/QtQmlIntegration>

class Fact;
class Vehicle;

/// One tile of the Fly View telemetry panel: which value it shows, as a number or a bar, and when it turns amber or
/// red. Bar range and alert values are in the value's displayed units.
class TelemetryPanelItem : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by TelemetryPanelController")
    Q_MOC_INCLUDE("Fact.h")
    Q_PROPERTY(QString factGroupName READ factGroupName NOTIFY factChanged)
    Q_PROPERTY(QString factName READ factName NOTIFY factChanged)
    Q_PROPERTY(Fact* fact READ fact NOTIFY factChanged)
    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY settingsChanged)  ///< Empty for the value's own name
    Q_PROPERTY(QString displayLabel READ displayLabel NOTIFY displayLabelChanged)
    Q_PROPERTY(bool showUnits READ showUnits WRITE setShowUnits NOTIFY settingsChanged)
    Q_PROPERTY(bool showPercentage READ showPercentage WRITE setShowPercentage NOTIFY
                   settingsChanged)  ///< Where the value sits in its range, in place of the value
    Q_PROPERTY(Style style READ style WRITE setStyle NOTIFY settingsChanged)
    Q_PROPERTY(double barMinimum READ barMinimum WRITE setBarMinimum NOTIFY settingsChanged)
    Q_PROPERTY(double barMaximum READ barMaximum WRITE setBarMaximum NOTIFY settingsChanged)
    Q_PROPERTY(Alert alert READ alert WRITE setAlert NOTIFY settingsChanged)
    Q_PROPERTY(double cautionValue READ cautionValue WRITE setCautionValue NOTIFY settingsChanged)  ///< NaN when off
    Q_PROPERTY(double warningValue READ warningValue WRITE setWarningValue NOTIFY settingsChanged)  ///< NaN when off
    Q_PROPERTY(int level READ level NOTIFY stateChanged)           ///< 0 normal, 1 caution, 2 warning
    Q_PROPERTY(double fraction READ fraction NOTIFY stateChanged)  ///< How full the bar is, 0 to 1

public:
    enum Style
    {
        NumberStyle,
        BarStyle
    };
    Q_ENUM(Style)

    enum Alert
    {
        NoAlert,
        AlertAbove,  ///< High values are bad
        AlertBelow   ///< Low values are bad
    };
    Q_ENUM(Alert)

    explicit TelemetryPanelItem(QObject* parent = nullptr);

    Q_INVOKABLE void setFact(const QString& factGroupName, const QString& factName);
    /// A bar range from the value's own limits, else 0 to 100 for a percentage, else from zero to above its value
    Q_INVOKABLE void suggestBarRange();

    QString factGroupName() const { return _factGroupName; }

    QString factName() const { return _factName; }

    Fact* fact() const { return _fact; }

    QString label() const { return _label; }

    QString displayLabel() const;

    bool showUnits() const { return _showUnits; }

    bool showPercentage() const { return _showPercentage; }

    Style style() const { return _style; }

    double barMinimum() const { return _barMinimum; }

    double barMaximum() const { return _barMaximum; }

    Alert alert() const { return _alert; }

    double cautionValue() const { return _cautionValue; }

    double warningValue() const { return _warningValue; }

    int level() const { return _level; }

    double fraction() const { return _fraction; }

    void setLabel(const QString& label);
    void setShowUnits(bool showUnits);
    void setShowPercentage(bool showPercentage);
    void setStyle(Style style);
    void setBarMinimum(double minimum);
    void setBarMaximum(double maximum);
    void setAlert(Alert alert);
    void setCautionValue(double value);
    void setWarningValue(double value);

    /// Looks the value up on this vehicle, or again once the vehicle reports more value groups
    void setVehicle(Vehicle* vehicle);

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);

signals:
    void factChanged();
    void settingsChanged();
    void displayLabelChanged();
    void stateChanged();

private:
    void _resolveFact();
    void _updateState();

    QPointer<Vehicle> _vehicle;
    QPointer<Fact> _fact;
    QString _factGroupName;
    QString _factName;
    QString _label;
    bool _showUnits = true;
    bool _showPercentage = false;
    Style _style = NumberStyle;
    double _barMinimum = 0.0;
    double _barMaximum = 100.0;
    Alert _alert = NoAlert;
    double _cautionValue = qQNaN();
    double _warningValue = qQNaN();
    int _level = 0;
    double _fraction = 0.0;
};
