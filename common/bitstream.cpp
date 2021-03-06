#include "common.h"
#include "bitstream.h"

using namespace x265;

#if defined(_MSC_VER)
#pragma warning(disable: 4244)
#endif

#define MIN_FIFO_SIZE 1000

Bitstream::Bitstream()
{
    m_fifo = X265_MALLOC(uint8_t, MIN_FIFO_SIZE);
    m_byteAlloc = MIN_FIFO_SIZE;
    resetBits();
}

void Bitstream::push_back(uint8_t val)
{
    if (!m_fifo)
        return;

    if (m_byteOccupancy >= m_byteAlloc)
    {
        /** reallocate buffer with doubled size */
        uint8_t *temp = X265_MALLOC(uint8_t, m_byteAlloc * 2);
        if (temp)
        {
            ::memcpy(temp, m_fifo, m_byteOccupancy);
            X265_FREE(m_fifo);
            m_fifo = temp;
            m_byteAlloc *= 2;
        }
        else
        {
            x265_log(NULL, X265_LOG_ERROR, "Unable to realloc bitstream buffer");
            return;
        }
    }
    m_fifo[m_byteOccupancy++] = val;
}

void Bitstream::write(uint32_t val, uint32_t numBits)
{
    X265_CHECK(numBits <= 32, "numBits out of range\n");
    X265_CHECK(numBits == 32 || (val & (~0 << numBits)) == 0, "numBits & val out of range\n");

    uint32_t totalPartialBits = m_partialByteBits + numBits;
    uint32_t nextPartialBits = totalPartialBits & 7;
    uint8_t  nextHeldByte = val << (8 - nextPartialBits);
    uint32_t writeBytes = totalPartialBits >> 3;

    if (writeBytes)
    {
        /* topword aligns m_partialByte with the msb of val */
        uint32_t topword = (numBits - nextPartialBits) & ~7;
        uint32_t write_bits = (m_partialByte << topword) | (val >> nextPartialBits);

        switch (writeBytes)
        {
        case 4: push_back(write_bits >> 24);
        case 3: push_back(write_bits >> 16);
        case 2: push_back(write_bits >> 8);
        case 1: push_back(write_bits);
        }

        m_partialByte = nextHeldByte;
        m_partialByteBits = nextPartialBits;
    }
    else
    {
        m_partialByte |= nextHeldByte;
        m_partialByteBits = nextPartialBits;
    }
}

void Bitstream::writeByte(uint32_t val)
{
    // Only CABAC will call writeByte, the fifo must be byte aligned
    X265_CHECK(!m_partialByteBits, "expecting m_partialByteBits = 0\n");

    push_back(val);
}

void Bitstream::writeAlignOne()
{
    uint32_t numBits = (8 - m_partialByteBits) & 0x7;

    write((1 << numBits) - 1, numBits);
}

void Bitstream::writeAlignZero()
{
    if (m_partialByteBits)
    {
        push_back(m_partialByte);
        m_partialByte = 0;
        m_partialByteBits = 0;
    }
}

void Bitstream::writeByteAlignment()
{
    write(1, 1);
    writeAlignZero();
}
