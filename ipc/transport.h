#ifndef IQOPTIONTESTTASK_TRANSPORT_H
#define IQOPTIONTESTTASK_TRANSPORT_H

#include <memory>
#include <asio.hpp>

#include "../utils/binary_storage.h"
#include "../utils/spinlock.h"
#include "protocol.h"


class TCPGenericSocketTransport {
public:

    TCPGenericSocketTransport () : sock(ios) {}

    bool send (const void* buf, size_t size) {
        asio::error_code ec;
        asio::write(sock, asio::buffer(buf, size), ec);

        return static_cast<bool>(ec);
    }

    bool send (const buffer_t& buf) {
        asio::error_code ec;
        asio::write(sock, asio::buffer(buf), ec);

        return static_cast<bool>(ec);
    }

    bool receive (void* buf, int size) {
        asio::error_code ec;
        asio::read(sock, asio::buffer(buf, size), ec);

        return static_cast<bool>(ec);
    }

    bool receive (buffer_t& buf) {
        asio::error_code ec;
        asio::write(sock, asio::buffer(buf), ec);

        return static_cast<bool>(ec);
    }

protected:

    asio::io_service ios;
    asio::ip::tcp::socket sock;
};

// --------------------------------------------------------------------- //

class TCPClientSocketTransport : public TCPGenericSocketTransport {
public:

    void init (const std::string& host, const std::string& port) {
        using asio::ip::tcp;

        tcp::resolver resolver(ios);
        tcp::resolver::query query(host, port);
        tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);

        asio::connect(sock, endpoint_iterator);
    }
};

// --------------------------------------------------------------------- //

class TCPServerSocketTransport : public TCPGenericSocketTransport {
public:

    void init (unsigned short port) {
        using asio::ip::tcp;

        tcp::acceptor acceptor(ios, tcp::endpoint(tcp::v4(), port));

        acceptor.accept(sock);
    }
};

// --------------------------------------------------------------------- //

class transport_error_recoverable {};

template <class Transport>
class GenericMessageLayer {
protected:

    GenericMessageLayer () = default;
    GenericMessageLayer (const GenericMessageLayer&) = delete;
    GenericMessageLayer (GenericMessageLayer&&) = default;

public:

    void send (const BinaryOStream& buffer) {
        if (!m_transport.send(buffer.storage())) {
            throw transport_error_recoverable {};
        }
    }

    BinaryIStream receive (buffer_t& storage) {
        IpcProto::message_size_t ms;

        if (!m_transport.receive(&ms, sizeof(ms))) {
            throw transport_error_recoverable {};
        }

        storage.resize(ms);

        if (!m_transport.receive(storage)) {
            throw transport_error_recoverable {};
        }

        return BinaryIStream{storage};
    }

protected:

    Transport m_transport;
};

// --------------------------------------------------------------------- //

using SpinlockPtr = std::unique_ptr<Spinlock>;

template <class Transport>
class ServerSideTransport : public GenericMessageLayer<Transport> {
public:

    ServerSideTransport (SpinlockPtr&& writerLock) : m_writerLock {std::move(writerLock)} {
        assert(m_writerLock);
    }

    template <class... Args>
    void launch (Args&&... args) {
        using IpcProto::message_code_t;

        // won't compile without the "this->" prefix
        this->m_transport.init(std::forward<Args>(args)...);

        buffer_t handshakeStorage;
        BinaryIStream buffer = this->receive(handshakeStorage);

        try {
            message_code_t mc;

            buffer >> mc;

            if (mc != static_cast<message_code_t>(IpcProto::ProtocolConstants::ClientMessageCode::HANDSHAKE)) {
                std::cerr << "Protocol error: invalid handshake message code" << std::endl;

                throw "";
            }

            IpcProto::HandshakeMsg msg;

            msg.init(buffer);

            if (msg.version() != IpcProto::ProtocolConstants::version) {
                std::cerr << "Protocol error: invalid protocol version" << std::endl;

                BinaryOStream errorBuffer = createAdaptedErrorBuffer();
                IpcProto::UnsupportedProtocolVersionError error;

                error.store(errorBuffer);
                writeMessage(errorBuffer);

                throw "";
            }
        } catch (...) {
            throw transport_error_recoverable {};
        }
    }

    BinaryOStream createAdaptedRatingBuffer () const {
        BinaryOStream buffer;

        buffer << IpcProto::message_size_t {0}
               << static_cast<IpcProto::message_code_t>(IpcProto::ProtocolConstants::ServiceMessageCode::USER_RATING);
        return buffer;
    }

    BinaryOStream createAdaptedErrorBuffer () const {
        BinaryOStream buffer;

        buffer << IpcProto::message_size_t {0}
               << static_cast<IpcProto::message_code_t>(IpcProto::ProtocolConstants::ServiceMessageCode::PROTOCOL_ERROR);
        return buffer;
    }

    void writeMessage (BinaryOStream& buffer) {
        buffer.setPos(0);
        buffer << static_cast<IpcProto::message_size_t>(buffer.storage().size());

        this->send(buffer);
    }

    void blockedWriteMessage (BinaryOStream& buffer) {
        std::lock_guard(*m_writerLock.get());

        writeMessage(buffer);
    }

private:

    SpinlockPtr m_writerLock;
};

// --------------------------------------------------------------------- //

template <class Transport>
class ClientSideTransport : public GenericMessageLayer<Transport> {
public:

    template <class... Args>
    void launch (Args&&... args) {
        this->m_transport.init(std::forward<Args>(args)...);

        BinaryOStream messageBuffer = createAdaptedMessageBuffer();
        IpcProto::HandshakeMsg msg {IpcProto::ProtocolConstants::version};

        messageBuffer << static_cast<IpcProto::message_code_t>(IpcProto::ProtocolConstants::ClientMessageCode::HANDSHAKE);
        msg.store(messageBuffer);

        writeMessage(messageBuffer);
    }

    BinaryOStream createAdaptedMessageBuffer () const {
        BinaryOStream buffer;

        buffer << IpcProto::message_size_t {0};

        return buffer;
    }

    void writeMessage (BinaryOStream& buffer) {
        buffer.setPos(0);
        buffer << static_cast<IpcProto::message_size_t>(buffer.storage().size());

        this->send(buffer);
    }
};

using ServerIpcTransport = ServerSideTransport<TCPServerSocketTransport>;

#endif //IQOPTIONTESTTASK_TRANSPORT_H
