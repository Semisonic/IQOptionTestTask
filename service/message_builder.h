#ifndef IQOPTIONTESTTASK_MESSAGE_BUILDER_H
#define IQOPTIONTESTTASK_MESSAGE_BUILDER_H

#include "../ipc/protocol.h"
#include "../ipc/transport.h"

struct MessageBattery {
    IpcProto::UserRegisteredMsg userRegisteredMsg;
    IpcProto::UserRenamedMsg userRenamedMsg;
    IpcProto::UserConnectedMsg userConnectedMsg;
    IpcProto::UserDisconnectedMsg userDisconnectedMsg;
    IpcProto::UserDealWonMsg userDealWonMsg;
};

// --------------------------------------------------------------------- //
/*
 *  MessageBuilder class
 *
 *  reads the data as it comes from the transport layer, interprets it and initializes
 *  the corresponding message object in the battery provided during construction
 *
 *  reason behind such approach is to reuse the message objects and minimize allocations
 */
// --------------------------------------------------------------------- //

class MessageBuilder {

    using ClientMessageCode = IpcProto::ProtocolConstants::ClientMessageCode;

public:

    class message_code_unrecognized {
        using message_code_t = IpcProto::message_code_t;

    public:

        message_code_unrecognized (message_code_t code) : m_code(code) {}

        message_code_t code () const { return m_code; }

    private:

        message_code_t m_code;
    };

public:

    MessageBuilder (MessageBattery& battery, ServerIpcTransport& transport)
    : m_battery {battery}, m_transport {transport} {}

    ClientMessageCode build () {
        BinaryIStream messageData = m_transport.receive(m_bufferStorage);
        IpcProto::message_code_t messageCode {IpcProto::ProtocolConstants::invalidMessageCode};

        messageData >> messageCode;

        auto properMessageCode = static_cast<ClientMessageCode>(messageCode);

        switch (properMessageCode) {
        case ClientMessageCode::USER_REGISTERED : m_battery.userRegisteredMsg.init(messageData); break;
        case ClientMessageCode::USER_RENAMED : m_battery.userRenamedMsg.init(messageData); break;
        case ClientMessageCode::USER_CONNECTED : m_battery.userConnectedMsg.init(messageData); break;
        case ClientMessageCode::USER_DISCONNECTED : m_battery.userDisconnectedMsg.init(messageData); break;
        case ClientMessageCode::USER_DEAL_WON : m_battery.userDealWonMsg.init(messageData); break;
        default: throw message_code_unrecognized{messageCode};
        }

        return properMessageCode;
    }

private:

    MessageBattery& m_battery;
    ServerIpcTransport& m_transport;
    buffer_t m_bufferStorage;
};


#endif //IQOPTIONTESTTASK_MESSAGE_BUILDER_H
