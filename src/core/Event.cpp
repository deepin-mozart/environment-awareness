#include "Event.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>

namespace Awareness {

QString eventTypeToString(EventType type)
{
    switch (type) {
    case EventType::Window:    return QStringLiteral("window");
    case EventType::File:      return QStringLiteral("file");
    case EventType::Clipboard: return QStringLiteral("clipboard");
    case EventType::Input:     return QStringLiteral("input");
    case EventType::System:    return QStringLiteral("system");
    case EventType::Browser:   return QStringLiteral("browser");
    }
    return QStringLiteral("unknown");
}

EventType stringToEventType(const QString &str)
{
    if (str == QLatin1String("window"))     return EventType::Window;
    if (str == QLatin1String("file"))       return EventType::File;
    if (str == QLatin1String("clipboard")) return EventType::Clipboard;
    if (str == QLatin1String("input"))      return EventType::Input;
    if (str == QLatin1String("system"))     return EventType::System;
    if (str == QLatin1String("browser"))    return EventType::Browser;
    return EventType::Window; // fallback
}

QVariantMap Event::toVariantMap() const
{
    QVariantMap map;
    map.insert(QStringLiteral("timestamp"), timestamp);
    map.insert(QStringLiteral("type"), eventTypeToString(type));
    map.insert(QStringLiteral("action"), action);
    if (!appName.isEmpty())
        map.insert(QStringLiteral("app_name"), appName);
    if (appPid != 0)
        map.insert(QStringLiteral("app_pid"), static_cast<qint64>(appPid));
    if (!windowTitle.isEmpty())
        map.insert(QStringLiteral("window_title"), windowTitle);
    if (!filePath.isEmpty())
        map.insert(QStringLiteral("file_path"), filePath);
    if (!contentPreview.isEmpty())
        map.insert(QStringLiteral("content_preview"), contentPreview);
    if (!metadata.isEmpty())
        map.insert(QStringLiteral("metadata"), metadata);
    return map;
}

Event Event::fromVariantMap(const QVariantMap &map)
{
    Event ev;
    ev.timestamp = map.value(QStringLiteral("timestamp")).toLongLong();
    ev.type = stringToEventType(map.value(QStringLiteral("type")).toString());
    ev.action = map.value(QStringLiteral("action")).toString();
    ev.appName = map.value(QStringLiteral("app_name")).toString();
    ev.appPid = map.value(QStringLiteral("app_pid")).toLongLong();
    ev.windowTitle = map.value(QStringLiteral("window_title")).toString();
    ev.filePath = map.value(QStringLiteral("file_path")).toString();
    ev.contentPreview = map.value(QStringLiteral("content_preview")).toString();
    ev.metadata = map.value(QStringLiteral("metadata")).toMap();
    return ev;
}

QString Event::metadataToJson() const
{
    if (metadata.isEmpty()) return QString();
    return QString::fromUtf8(
        QJsonDocument(QJsonObject::fromVariantMap(metadata)).toJson(QJsonDocument::Compact));
}

} // namespace Awareness
