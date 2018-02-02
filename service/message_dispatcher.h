#ifndef IQOPTIONTESTTASK_MESSAGE_DISPATCHER_H
#define IQOPTIONTESTTASK_MESSAGE_DISPATCHER_H

namespace IpcProto {
    class UserRegisteredMsg;
    class UserRenamedMsg;
    class UserConnectedMsg;
    class UserDisconnectedMsg;
    class UserDealWonMsg;
}

struct IncomingDataBuffer;
struct JobQueue;

class MessageDispatcher {
public:

    MessageDispatcher (JobQueue& queue, IncomingDataBuffer& buffer);

    void setBuffer (IncomingDataBuffer& buffer);

    void dispatch (const IpcProto::UserRegisteredMsg& msg);
    void dispatch (const IpcProto::UserRenamedMsg& msg);
    void dispatch (const IpcProto::UserConnectedMsg& msg);
    void dispatch (const IpcProto::UserDisconnectedMsg& msg);
    void dispatch (const IpcProto::UserDealWonMsg& msg);

private:

    JobQueue& m_queue;
    IncomingDataBuffer* m_buffer;
};

#endif //IQOPTIONTESTTASK_MESSAGE_DISPATCHER_H
