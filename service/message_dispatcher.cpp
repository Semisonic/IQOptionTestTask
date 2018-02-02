#include "message_dispatcher.h"
#include "core_data.h"
#include "job_queue.h"

#include "../utils/date_time.h"

MessageDispatcher::MessageDispatcher (JobQueue& queue, IncomingDataBuffer& buffer): m_queue(queue), m_buffer(&buffer) {}

void MessageDispatcher::setBuffer (IncomingDataBuffer& buffer) { m_buffer = &buffer; }

void MessageDispatcher::dispatch (const IpcProto::UserRegisteredMsg &msg) {
#ifdef PASS_NAMES_AROUND
    m_buffer.usersRegistered[msg.id()] = msg.name();
#else
    m_buffer->usersRegistered.insert(msg.id());
#endif
}

void MessageDispatcher::dispatch (const IpcProto::UserRenamedMsg &msg) {
#ifdef PASS_NAMES_AROUND
    m_buffer.usersRenamed[msg.id()] = msg.name();
#endif
}

void MessageDispatcher::dispatch (const IpcProto::UserConnectedMsg &msg) {
    m_buffer->connectionChanges[msg.id()] = DateTime::currentSecondIndex();
    m_queue.enqueueRatingJob(msg.id());
}

void MessageDispatcher::dispatch (const IpcProto::UserDisconnectedMsg &msg) {
    m_buffer->connectionChanges[msg.id()] = UserDataConstants::invalidSecond;
}

void MessageDispatcher::dispatch (const IpcProto::UserDealWonMsg &msg) {
    m_buffer->dealsWon[msg.id()] += msg.amount();
}