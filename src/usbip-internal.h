#pragma once

#include <cstdint>

namespace m8 {

template <typename T>
T swap(const T& arg)
{
#if BYTE_ORDER == BIG_ENDIAN
    return arg;             // no byte-swapping needed
#else                       // swap bytes
    T ret;
    char* dst = reinterpret_cast<char*>(&ret);
    const char* src = reinterpret_cast<const char*>(&arg + 1);
    for (size_t i = 0; i < sizeof(T); i++)
        *dst++ = *--src;
    return ret;
#endif
}

template <typename T>
class __attribute__ ((__packed__)) BigEndian
{
    T value;

public:
    BigEndian() { }
    BigEndian(const T& t) : value(swap(t)) { }
    operator T() const { return swap(value); }
};

using be_uint16_t = BigEndian<uint16_t>;
using be_uint32_t = BigEndian<uint32_t>;

struct __attribute__ ((__packed__)) OP_REQ_HEADER {
    be_uint16_t version;
    be_uint16_t command;
    be_uint32_t status;
};

struct __attribute__ ((__packed__)) OP_REQ_IMPORT : public OP_REQ_HEADER {
    char busid[32];
};

struct  __attribute__ ((__packed__)) OP_REP_IMPORT {
    be_uint16_t version;
    be_uint16_t command;
    be_uint32_t status;
    char path[256];
    char busid[32];
    be_uint32_t busnum;
    be_uint32_t devnum;
    be_uint32_t speed;
    be_uint16_t idVendor;
    be_uint16_t idProduct;
    be_uint16_t bcdDevice;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bConfigurationValue;
    uint8_t bNumConfigurations;
    uint8_t bNumInterfaces;
};

struct  __attribute__ ((__packed__)) USBIP_HEADER_BASIC {
    be_uint32_t command;
    be_uint32_t seqnum;
    be_uint32_t devid;
    be_uint32_t direction;
    be_uint32_t ep;
};

struct __attribute__ ((__packed__)) USBIP_SETUP_BYTES {
    uint32_t bytes0;
    uint32_t bytes1;
};

struct __attribute__ ((__packed__)) USBIP_CMD_SUBMIT : public USBIP_HEADER_BASIC {
    be_uint32_t transfer_flags;
    be_uint32_t transfer_buffer_length;
    be_uint32_t start_frame;
    be_uint32_t number_of_packets;
    be_uint32_t interval;
    USBIP_SETUP_BYTES setup;
};

struct __attribute__ ((__packed__)) USBIP_ISOC_DESC
{
    be_uint32_t offset;
    be_uint32_t length;
    be_uint32_t actual_length;
    be_uint32_t status;
};

struct __attribute__ ((__packed__)) USBIP_RET_SUBMIT : public USBIP_HEADER_BASIC {
    be_uint32_t status;
    be_uint32_t actual_length;
    be_uint32_t start_frame;
    be_uint32_t number_of_packets;
    be_uint32_t error_count;
    USBIP_SETUP_BYTES setup;
};

} // namespace m8
