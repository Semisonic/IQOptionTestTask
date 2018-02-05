#ifndef IQOPTIONTESTTASK_BINARY_STORAGE_H
#define IQOPTIONTESTTASK_BINARY_STORAGE_H

#include <cstddef>
#include <memory.h>
#include <cassert>
#include <climits>

#include "types.h"

// --------------------------------------------------------------------- //
/*
 *  BinaryIStream & BinaryOStream classes
 *
 *  convenience types for serializing/deserializing POD objects and short byte buffers
 */
// --------------------------------------------------------------------- //

class BinaryIStream {
public:

    class storage_underflow {};

public:

    BinaryIStream (buffer_t& storage) : m_storage{storage} {}
    BinaryIStream (BinaryIStream&&) = default;

    template <typename POD,
            typename std::enable_if_t<std::is_pod<POD>::value>* = nullptr>
    BinaryIStream& operator>> (POD& data) {
        if (m_storage.size() - m_curPos < sizeof(data)) {
            throw storage_underflow{};
        }

        memcpy(&data, m_storage.data() + m_curPos, sizeof(data));
        m_curPos += sizeof(data);

        return *this;
    }

    BinaryIStream& operator>> (buffer_t& data) {
        unsigned char size {0};

        *this >> size;
        data.resize(size);

        if (m_storage.size() - m_curPos < size) {
            throw storage_underflow{};
        }

        memcpy(data.data(), m_storage.data() + m_curPos, size);
        m_curPos += size;

        return *this;
    }

    buffer_t& storage () { return m_storage; }

private:

    buffer_t::size_type m_curPos {0};
    buffer_t& m_storage; // storing by reference is for the calling party to reuse the buffer later without reallocations
};

// --------------------------------------------------------------------- //

class BinaryOStream {
public:

    using pos_t = buffer_t::size_type;

public:

    BinaryOStream () = default;
    BinaryOStream (const BinaryOStream&) = delete;
    BinaryOStream (BinaryOStream&&) = default;

    pos_t getPos () const { return m_curPos; }
    bool setPos (pos_t newPos) {
        if (newPos > m_storage.size()) {
            return false;
        }

        m_curPos = newPos;

        return true;
    }

    bool rewind (pos_t pos = 0) {
        if (pos > m_storage.size()) {
            return false;
        }

        m_storage.resize(pos);
        m_curPos = pos;

        return true;
    }

    template <typename POD,
              typename std::enable_if_t<std::is_pod<POD>::value>* = nullptr>
    BinaryOStream& operator<< (POD data) {
        if (m_storage.size() - m_curPos < sizeof(data)) {
            m_storage.resize(m_curPos + sizeof(data));
        }

        memcpy(m_storage.data() + m_curPos, &data, sizeof(data));
        m_curPos += sizeof(data);

        return *this;
    }

    BinaryOStream& operator<< (const buffer_t& data) {
        assert(data.size() <= UCHAR_MAX);

        auto size = static_cast<unsigned char>(data.size());

        if (m_storage.size() - m_curPos < size + 1) {
            // +1 is for storing the size itself
            m_storage.resize(m_curPos + size + 1);
        }

        *this << size;
        memcpy(m_storage.data() + m_curPos, data.data(), size);
        m_curPos += size;

        return *this;
    }

    const buffer_t& storage () const { return m_storage; }

private:

    pos_t m_curPos {0};
    buffer_t m_storage;
};

#endif //IQOPTIONTESTTASK_BINARY_STORAGE_H
