#ifndef IQOPTIONTESTTASK_MESSAGE_INTERPRETER_H
#define IQOPTIONTESTTASK_MESSAGE_INTERPRETER_H

#include "../ipc/protocol.h"
#include "storage.h"

using MessageCode = IpcProto::ProtocolConstants::ServiceMessageCode;

class MessageInterpreter {
public:

    static MessageCode interpretIncomingData (BinaryIStream& data, ErrorPtr& error, IpcProto::RatingPackMessage& rating) {
        using ProtocolError = IpcProto::ProtocolConstants::ProtocolError;

        IpcProto::message_code_t mc;

        data >> mc;

        switch (static_cast<MessageCode>(mc)) {
            case MessageCode::PROTOCOL_ERROR: {
                IpcProto::error_code_t ec;

                data >> ec;

                switch (static_cast<ProtocolError>(ec)) {
                case ProtocolError::PROTOCOL_VERSION_UNSUPPORTED:
                    error = std::make_unique<IpcProto::UnsupportedProtocolVersionError>();
                    error->init(data);
                    return MessageCode::PROTOCOL_ERROR;
                case ProtocolError::USER_UNRECOGNIZED:
                    error = std::make_unique<IpcProto::UserUnrecognizedError>();
                    error->init(data);
                    return MessageCode::PROTOCOL_ERROR;
                case ProtocolError::MULTIPLE_REGISTRATION:
                    error = std::make_unique<IpcProto::MultipleRegistrationError>();
                    error->init(data);
                    return MessageCode::PROTOCOL_ERROR;
                default: assert(false);
                }
                break;
            }
            case MessageCode::USER_RATING:
                rating.init(data);
                return MessageCode::USER_RATING;
            default: assert(false);
        }
    }
};

#endif //IQOPTIONTESTTASK_MESSAGE_INTERPRETER_H
