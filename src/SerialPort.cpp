#include "SerialPort.h"

#include <chrono>
#include <format>

#include "PluginManager.h"

namespace {

// Restores the port's configured timeout when readLine() exits, including on
// exceptions. A failure here must not mask the original error.
class TimeoutGuard {
public:
    TimeoutGuard(serial_cpp::Serial& serial, const serial_cpp::Timeout& timeout)
        : _serial(serial), _timeout(timeout) {}
    ~TimeoutGuard() {
        try {
            _serial.setTimeout(_timeout);
        } catch (...) {
        }
    }
    TimeoutGuard(const TimeoutGuard&) = delete;
    TimeoutGuard& operator=(const TimeoutGuard&) = delete;

private:
    serial_cpp::Serial& _serial;
    serial_cpp::Timeout _timeout;
};

} // namespace

void SerialPort::open(const std::string& portName, std::uint32_t baudRate, std::uint32_t timeoutMillis,
                      SerialFormat format, serial_cpp::parity_t parity, serial_cpp::flowcontrol_t flowControl) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_serial.isOpen()) {
        const std::string errorMsg = std::format("SerialPort::open({}) but already open on {}", portName, _serial.getPort());
        throw std::runtime_error(errorMsg);
    }

    serial_cpp::stopbits_t stopbits = serial_cpp::stopbits_t::stopbits_one;
    switch (format) {
        case SerialFormat::f8N1:
            stopbits = serial_cpp::stopbits_t::stopbits_one;
            break;
        case SerialFormat::f8N2:
            stopbits = serial_cpp::stopbits_t::stopbits_two;
            break;
    }

    _timeoutMillis = timeoutMillis;
    _Call("open", [&] {
        _serial.setPort(portName);
        _serial.setBaudrate(baudRate);
        _serial.setBytesize(serial_cpp::eightbits);
        _serial.setParity(parity);
        _serial.setFlowcontrol(flowControl);
        _serial.setStopbits(stopbits);
        // A single overall timeout per operation. The inter-byte timeout is
        // set equal to the total timeout: on Linux the inter-byte timeout
        // never ends a read early, but on Windows it maps to
        // ReadIntervalTimeout and would otherwise return partial data early,
        // making the behavior platform-dependent. Timeout::max() must not be
        // used for the inter-byte timeout because MAXDWORD has a special
        // meaning in the Windows API.
        _serial.setTimeout(serial_cpp::Timeout(timeoutMillis, timeoutMillis, 0, timeoutMillis, 0));
        _serial.open();
    });

    if (!_serial.isOpen()) {
        const std::string errorMsg = std::format("SerialPort::open({}) failed to open", portName);
        throw std::runtime_error(errorMsg);
    }
}

void SerialPort::close() {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (_serial.isOpen()) {
        _Call("close", [&] { _serial.close(); });
    }
}

std::vector<serial_cpp::PortInfo> SerialPort::listPorts() {
    try {
        return serial_cpp::list_ports();
    } catch (const std::exception& e) {
        throw std::runtime_error(std::format("SerialPort::listPorts: {}", e.what()));
    }
}

void SerialPort::write(const std::string& data) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    // serial_cpp applies the write timeout per write() call, so bound the
    // whole operation with an overall deadline instead of relying on it.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(_timeoutMillis);
    std::size_t nBytesWrittenTotal = 0;
    while (nBytesWrittenTotal < data.size()) {
        const std::size_t nBytesWritten = _Call("write", [&] {
            return _serial.write(reinterpret_cast<const std::uint8_t*>(data.data()) + nBytesWrittenTotal,
                                 data.size() - nBytesWrittenTotal);
        });
        if (nBytesWritten == 0 || std::chrono::steady_clock::now() >= deadline) {
            const std::string errMsg = std::format("SerialPort::write timed out or failed to write to {} ({} of {} bytes written)",
                                                   _serial.getPort(), nBytesWrittenTotal, data.size());
            throw std::runtime_error(errMsg);
        }
        nBytesWrittenTotal += nBytesWritten;
    }

    if (_printCommunication) {
        // convert '\r' to '\n' since it affects the terminal output
        const std::string edited = _ConvertCRtoLF(data);
        const std::string msg = std::format("{} wrote: {}\n", _serial.getPort(), edited);
        PluginManager::Manager().Print(msg);
    }
}

std::string SerialPort::read() {
    return read(MAX_READ_SIZE);
}

std::string SerialPort::read(std::size_t maxNBytesToRead) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    const std::string response = _Call("read", [&] { return _serial.read(maxNBytesToRead); });
    if (_printCommunication) {
        // convert '\r' to '\n' since it affects the terminal output
        const std::string editedResponse = _ConvertCRtoLF(response);
        const std::string msg = std::format("{} read: {}\n", _serial.getPort(), editedResponse);
        PluginManager::Manager().Print(msg);
    }
    return response;
}

std::string SerialPort::readLine(const std::string& terminatorString, std::size_t maxBytes) {
    if (terminatorString.empty()) {
        throw std::invalid_argument("SerialPort::readLine: terminatorString must not be empty (use read() for terminator-less reads)");
    }
    std::lock_guard<std::recursive_mutex> lock(_mutex);

    // serial_cpp::Serial::readline gives every byte its own fresh timeout
    // budget, so a device that trickles bytes can keep it alive far beyond
    // the configured timeout. Enforce a single overall deadline here instead
    // by giving each byte read only the time left until that deadline.
    // (waitReadable() cannot be used for this: it is not implemented on
    // Windows.)
    const serial_cpp::Timeout configuredTimeout(_timeoutMillis, _timeoutMillis, 0, _timeoutMillis, 0);
    const TimeoutGuard timeoutGuard(_serial, configuredTimeout);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(_timeoutMillis);

    std::string response;
    while (response.size() < maxBytes) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        const auto remainingMillis = static_cast<std::uint32_t>(remaining > 0 ? remaining : 0);
        _Call("readLine", [&] {
            _serial.setTimeout(serial_cpp::Timeout(remainingMillis, remainingMillis, 0, remainingMillis, 0));
        });
        const std::string byte = _Call("readLine", [&] { return _serial.read(1); });
        if (byte.empty()) {
            break;  // timeout expired before a byte arrived
        }
        response += byte;
        if (response.ends_with(terminatorString)) {
            break;  // terminator found
        }
    }

    if (_printCommunication) {
        // convert '\r' to '\n' since it affects the terminal output
        const std::string editedResponse = _ConvertCRtoLF(response);
        const std::string msg = std::format("{} read: {}\n", _serial.getPort(), editedResponse);
        PluginManager::Manager().Print(msg);
    }
    return response;
}

std::uint8_t SerialPort::writeByteAndReadByte(const std::uint8_t byte) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    const std::string byteStr(1, static_cast<char>(byte));
    write(byteStr);
    const std::string response = read(1);
    if (response.empty()) {
        const std::string errMsg = std::format("SerialPort::writeByteAndReadByte: expected to read 1 byte from {} but read nothing",
                                               _serial.getPort());
        throw std::runtime_error(errMsg);
    }

    return static_cast<std::uint8_t>(response[0]);
}

std::string SerialPort::writeAndReadUntilString(const std::string& dataToWrite, const std::string& terminatorString) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    if (terminatorString.empty()) {
        throw std::invalid_argument("SerialPort::writeAndReadUntilString: terminatorString must not be empty");
    }

    write(dataToWrite);
    // readLine() logs the response when communication printing is enabled.
    const std::string response = readLine(terminatorString);
    if (response.empty()) {
        throw std::runtime_error(std::format("SerialPort::writeAndReadUntilString: no response from {} within {} ms",
                                             _serial.getPort(), _timeoutMillis));
    }
    if (!response.ends_with(terminatorString)) {
        throw std::runtime_error(std::format("SerialPort::writeAndReadUntilString: incomplete response from {} within {} ms: \"{}\"",
                                             _serial.getPort(), _timeoutMillis, _ConvertCRtoLF(response)));
    }

    return response;
}

void SerialPort::clearBuffers() {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _Call("clearBuffers", [&] { _serial.flush(); });
}

void SerialPort::flushInput() {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _Call("flushInput", [&] { _serial.flushInput(); });
}

void SerialPort::flushOutput() {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _Call("flushOutput", [&] { _serial.flushOutput(); });
}

std::size_t SerialPort::available() {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    return _Call("available", [&] { return _serial.available(); });
}

void SerialPort::setRTS(bool level) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _Call("setRTS", [&] { _serial.setRTS(level); });
}

void SerialPort::setDTR(bool level) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _Call("setDTR", [&] { _serial.setDTR(level); });
}

void SerialPort::setBreak(bool level) {
    std::lock_guard<std::recursive_mutex> lock(_mutex);
    _Call("setBreak", [&] { _serial.setBreak(level); });
}

std::string SerialPort::_ConvertCRtoLF(const std::string &input) {
    // convert '\r' to '\n' since it affects the terminal output
    std::string edited = input;
    for (std::size_t i = 0; i < edited.size(); ++i) {
        if (edited.at(i) == '\r') {
            edited.at(i) = '\n';
        }
    }
    return edited;
}
