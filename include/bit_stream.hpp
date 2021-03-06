#pragma once

#include <cstddef>
#include "defines.hpp"

/*
 * This class stores up to 64 bits of information that can be pushed/popped bit by bit or byte by byte.
 * First-in, First-out; just like sf::Packet.
 * It's designed to efficiently send multiple packed booleans over the network without having to use a byte for each.

 * Keep in mind that the least amount of data that can be sent over the network is 1 byte.
 * This means that this is only efficient for more than 2 booleans that are sent together.
 * Also be careful not to mix calls to bit/byte, as the data can be corrupted.
 * 
 * @REMOVE (since our custom implementation of sf::Packet now does this automatically)
*/

class BitStream
{
public:
    BitStream();

    void pushByte(u8 byte);
    void pushBit(bool bit);

    u8 popByte();
    bool popBit();

    bool endOfStream() const;
    void clear();
private:
    size_t m_stream;
    size_t m_currentIndex;
};
