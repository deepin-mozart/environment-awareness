#include "EventBus.h"

namespace Awareness {

EventBus::EventBus(QObject *parent)
    : QObject(parent)
{
}

void EventBus::publish(const Event &event)
{
    emit eventReceived(event);
    emit eventSignal(event);
}

} // namespace Awareness
