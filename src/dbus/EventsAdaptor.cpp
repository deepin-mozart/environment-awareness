#include "EventsAdaptor.h"
#include "../utils/Logger.h"

namespace Awareness {

EventsAdaptor::EventsAdaptor(QObject *parent)
    : QDBusAbstractAdaptor(parent)
{
}

void EventsAdaptor::emitEvent(const Event &event)
{
    const QVariantMap info = event.toVariantMap();

    switch (event.type) {
    case EventType::Window:
        emit WindowChanged(info);
        break;
    case EventType::File:
        if (event.action == QStringLiteral("open") || event.action == QStringLiteral("create"))
            emit FileOpened(info);
        else if (event.action == QStringLiteral("modify"))
            emit FileModified(info);
        break;
    case EventType::Clipboard:
        emit ClipboardChanged(info);
        break;
    case EventType::Input:
        emit InputPattern(info);
        break;
    case EventType::System:
        emit SystemAlert(info);
        break;
    case EventType::Browser:
        if (event.action == QStringLiteral("tab_changed"))
            emit BrowserTabChanged(info);
        else if (event.action == QStringLiteral("navigation"))
            emit BrowserNavigation(info);
        else if (event.action == QStringLiteral("download"))
            emit BrowserDownload(info);
        break;
    }
}

} // namespace Awareness
