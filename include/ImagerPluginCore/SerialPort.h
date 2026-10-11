#ifndef SERIALPORT_H
#define SERIALPORT_H

#include <cstdint>
#include <format>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "serial_cpp/serial.h"

// Serial port support for Imager plugins: a thin wrapper around
// serial_cpp::Serial with a small, explicit timeout and failure contract.
//
// Timeout and failure contract
// ----------------------------
// open() configures a single timeout (in milliseconds) that applies to every
// operation. Port-level timeouts are only approximate: their precision and
// their exact partial-return behavior depend on the operating system and on
// the serial port driver, so they are not consistent across platforms or
// drivers (on Windows the timeouts are delegated to the driver via
// COMMTIMEOUTS; on Linux they are implemented in userspace; USB-serial
// adapters add their own buffering, e.g. the FTDI latency timer). This class
// normalizes what it can — every operation has one overall deadline — but
// callers that need a hard deadline should enforce their own
// application-level timeout on top.
//
// - read() and readLine() are primitives: they return whatever arrived,
//   possibly fewer bytes than requested. An empty or unterminated result means
//   the timeout expired; they do not throw on timeout.
// - write() throws std::runtime_error if it cannot complete within the
//   timeout.
// - writeByteAndReadByte() and writeAndReadUntilString() throw
//   std::runtime_error unless a complete response (including the terminator)
//   arrives within the timeout.
// - Only std::runtime_error (and std::invalid_argument for bad arguments)
//   escape this class; serial_cpp exceptions are translated at the boundary.
// - Retrying after a timeout is the caller's policy; keep retry loops bounded
//   and give up with an error rather than polling indefinitely.
class SerialPort {
public:

    enum class SerialFormat {
        f8N1,    // 8 data bits, no parity, 1 stop bit
        f8N2     // 8 data bits, no parity, 2 stop bits
    };

    // Largest number of bytes read() requests by default and readLine()
    // accumulates.
    static constexpr std::size_t MAX_READ_SIZE = 65536;

    SerialPort() = default;
    ~SerialPort() = default;
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    void setPrintCommunication(bool print) { _printCommunication = print; }

    // Opens the port. Throws std::runtime_error if the port is already open
    // or cannot be opened. timeoutMillis is the single per-operation timeout
    // used by all subsequent operations (see the contract above).
    void open(const std::string& portName, std::uint32_t baudRate, std::uint32_t timeoutMillis,
              SerialFormat format = SerialFormat::f8N1,
              serial_cpp::parity_t parity = serial_cpp::parity_none,
              serial_cpp::flowcontrol_t flowControl = serial_cpp::flowcontrol_none);
    void close();
    bool isOpen() const { return _serial.isOpen(); }

    // Lists the serial ports available on the system.
    static std::vector<serial_cpp::PortInfo> listPorts();

    // Writes all of data. Throws std::runtime_error if that cannot complete
    // within the configured timeout.
    void write(const std::string& data);

    // Reads up to maxNBytesToRead bytes: returns as soon as that many bytes
    // have been read or the timeout expires, whichever comes first. On
    // timeout returns the (possibly empty) partial result.
    std::string read();
    std::string read(std::size_t maxNBytesToRead);

    // Reads until terminatorString is found (the terminator is included in
    // the result), until maxBytes bytes have been read, or until the timeout
    // expires — whichever comes first. On timeout returns the (possibly
    // empty) partial fragment. Unlike serial_cpp::Serial::readline, the
    // timeout is a single overall deadline for the whole line, not a fresh
    // per-byte budget. Throws std::invalid_argument if terminatorString is
    // empty; use read() for terminator-less reads.
    std::string readLine(const std::string& terminatorString, std::size_t maxBytes = MAX_READ_SIZE);

    // Writes one byte and reads one byte back. Throws std::runtime_error if
    // no byte arrives within the timeout.
    std::uint8_t writeByteAndReadByte(const std::uint8_t byte);

    // Writes dataToWrite and reads until terminatorString. Throws
    // std::runtime_error if no complete response (terminator included)
    // arrives within the timeout.
    std::string writeAndReadUntilString(const std::string& dataToWrite, const std::string& terminatorString);

    // Flushes both the input and output buffers, only the input buffer, or
    // only the output buffer.
    void clearBuffers();
    void flushInput();
    void flushOutput();

    // Number of bytes currently available to read.
    std::size_t available();

    // Handshaking line controls.
    void setRTS(bool level = true);
    void setDTR(bool level = true);
    void setBreak(bool level = true);

private:
    // Runs f(), translating serial_cpp exceptions into std::runtime_error so
    // that only std::runtime_error escapes this class.
    template <typename F>
    auto _Call(const char* operation, F&& f) -> decltype(f()) {
        try {
            return f();
        } catch (const serial_cpp::PortNotOpenedException& e) {
            _Rethrow(operation, e);
        } catch (const serial_cpp::SerialException& e) {
            _Rethrow(operation, e);
        } catch (const serial_cpp::IOException& e) {
            _Rethrow(operation, e);
        }
    }

    [[noreturn]] void _Rethrow(const char* operation, const std::exception& e) const {
        throw std::runtime_error(std::format("SerialPort::{} on {}: {}", operation, _serial.getPort(), e.what()));
    }

    static std::string _ConvertCRtoLF(const std::string& input);

    serial_cpp::Serial _serial;
    std::recursive_mutex _mutex;
    std::uint32_t _timeoutMillis = 0;
    bool _printCommunication = false;
};

#endif // SERIALPORT_H
