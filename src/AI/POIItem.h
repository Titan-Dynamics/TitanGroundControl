#pragma once

#include <QtCore/QObject>
#include <QtPositioning/QGeoCoordinate>
#include <QtQmlIntegration/QtQmlIntegration>

class POIItem : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("")

    Q_PROPERTY(QGeoCoordinate coordinate READ coordinate NOTIFY coordinateChanged)
    Q_PROPERTY(int number READ number NOTIFY numberChanged)

public:
    explicit POIItem(const QGeoCoordinate& coordinate, int number, QObject* parent = nullptr)
        : QObject(parent), _coordinate(coordinate), _number(number) {}

    QGeoCoordinate coordinate() const { return _coordinate; }
    int number() const { return _number; }

    void setNumber(int number) {
        if (_number != number) {
            _number = number;
            emit numberChanged();
        }
    }

signals:
    void coordinateChanged();
    void numberChanged();

private:
    QGeoCoordinate _coordinate;
    int _number = 0;
};
