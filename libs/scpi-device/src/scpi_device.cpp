#include "scpi_device/scpi_device.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

namespace scpi {
namespace {

constexpr auto kPostOpenSettleDelay = std::chrono::milliseconds(250);
constexpr int kFirstQueryRetryCount = 2;

speed_t baudToTermios(int baudRate) {
    switch (baudRate) {
    case 1200:
        return B1200;
    case 2400:
        return B2400;
    case 4800:
        return B4800;
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    case 115200:
        return B115200;
    case 230400:
        return B230400;
    default:
        throw std::invalid_argument("unsupported baud rate: " + std::to_string(baudRate));
    }
}

int timeoutMs(std::chrono::milliseconds timeout) {
    if (timeout.count() < 0) {
        return -1;
    }
    if (timeout.count() > std::numeric_limits<int>::max()) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(timeout.count());
}

cc_t timeoutDeciseconds(std::chrono::milliseconds timeout) {
    if (timeout.count() <= 0) {
        return 0;
    }

    const auto deciseconds = (timeout.count() + 99) / 100;
    if (deciseconds > std::numeric_limits<cc_t>::max()) {
        return std::numeric_limits<cc_t>::max();
    }
    return static_cast<cc_t>(deciseconds);
}

std::chrono::milliseconds postOpenSettleDelay() {
    if (const char *value = std::getenv("SCPI_DEVICE_OPEN_SETTLE_MS")) {
        if (*value != '\0') {
            char *end = nullptr;
            const long parsed = std::strtol(value, &end, 10);
            if (end != value && *end == '\0' && parsed >= 0) {
                return std::chrono::milliseconds(parsed);
            }
        }
    }
    return kPostOpenSettleDelay;
}

int firstQueryRetryCount() {
    if (const char *value = std::getenv("SCPI_DEVICE_OPEN_RETRIES")) {
        if (*value != '\0') {
            char *end = nullptr;
            const long parsed = std::strtol(value, &end, 10);
            if (end != value && *end == '\0' && parsed >= 0 &&
                parsed <= std::numeric_limits<int>::max()) {
                return static_cast<int>(parsed);
            }
        }
    }
    return kFirstQueryRetryCount;
}

std::string trimResponse(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
        value.pop_back();
    }
    return value;
}

std::string trimWhitespace(std::string value) {
    const auto first = std::find_if(
        value.begin(),
        value.end(),
        [](unsigned char ch) { return !std::isspace(ch); });
    const auto last = std::find_if(
        value.rbegin(),
        value.rend(),
        [](unsigned char ch) { return !std::isspace(ch); }).base();

    if (first >= last) {
        return {};
    }

    return std::string(first, last);
}

void throwSystemError(const std::string &message) {
    throw std::system_error(errno, std::generic_category(), message);
}

std::string commandWithArguments(const std::string &command, double range, double resolution) {
    return command + " " + std::to_string(range) + "," + std::to_string(resolution);
}

nlohmann::json serialOptionsToJson(const SerialOptions &options) {
    return {
        {"baudRate", options.baudRate},
        {"readTimeoutMs", options.readTimeout.count()},
        {"writeTimeoutMs", options.writeTimeout.count()},
        {"lineEnding", options.lineEnding},
        {"blockingIo", options.blockingIo},
    };
}

SerialOptions serialOptionsFromJson(const nlohmann::json &json) {
    SerialOptions options;
    options.baudRate = json.value("baudRate", options.baudRate);
    options.readTimeout = std::chrono::milliseconds(json.value(
        "readTimeoutMs",
        options.readTimeout.count()));
    options.writeTimeout = std::chrono::milliseconds(json.value(
        "writeTimeoutMs",
        options.writeTimeout.count()));
    options.lineEnding = json.value("lineEnding", options.lineEnding);
    options.blockingIo = json.value("blockingIo", options.blockingIo);
    return options;
}

} // namespace

const char *version() {
    return SCPI_DEVICE_VERSION;
}

DeviceRegistry::DeviceRegistry()
    : storagePath_(defaultStoragePath()) {
    load();
}

DeviceRegistry::DeviceRegistry(std::filesystem::path storagePath)
    : storagePath_(std::move(storagePath)) {
    load();
}

const std::filesystem::path &DeviceRegistry::storagePath() const {
    return storagePath_;
}

std::vector<RegisteredDevice> DeviceRegistry::devices() const {
    return devices_;
}

std::optional<RegisteredDevice> DeviceRegistry::getDevice(const std::string &name) const {
    const auto found = std::find_if(
        devices_.begin(),
        devices_.end(),
        [&name](const RegisteredDevice &device) { return device.name == name; });

    if (found == devices_.end()) {
        return std::nullopt;
    }

    return *found;
}

std::string DeviceRegistry::getPort(const std::string &name) const {
    const auto device = getDevice(name);
    if (!device) {
        throw std::out_of_range("device not registered: " + name);
    }
    return device->port;
}

SerialOptions DeviceRegistry::getOptions(const std::string &name) const {
    const auto device = getDevice(name);
    if (!device) {
        throw std::out_of_range("device not registered: " + name);
    }
    return device->options;
}

void DeviceRegistry::setDevice(
    const std::string &name,
    const std::string &port,
    const SerialOptions &options) {
    const auto found = std::find_if(
        devices_.begin(),
        devices_.end(),
        [&name](const RegisteredDevice &device) { return device.name == name; });

    if (found == devices_.end()) {
        devices_.push_back(RegisteredDevice{name, port, options});
    } else {
        found->port = port;
        found->options = options;
    }

    std::sort(
        devices_.begin(),
        devices_.end(),
        [](const RegisteredDevice &left, const RegisteredDevice &right) {
            return left.name < right.name;
        });

    save();
}

bool DeviceRegistry::removeDevice(const std::string &name) {
    const auto originalSize = devices_.size();
    devices_.erase(
        std::remove_if(
            devices_.begin(),
            devices_.end(),
            [&name](const RegisteredDevice &device) { return device.name == name; }),
        devices_.end());

    const bool removed = devices_.size() != originalSize;
    if (removed) {
        save();
    }
    return removed;
}

void DeviceRegistry::load() {
    devices_.clear();

    std::error_code error;
    if (!std::filesystem::exists(storagePath_, error)) {
        return;
    }

    std::ifstream input(storagePath_);
    if (!input) {
        throw std::runtime_error("failed to read device registry: " + storagePath_.string());
    }

    const auto root = nlohmann::json::parse(input);
    const auto deviceMap = root.value("devices", nlohmann::json::object());

    for (const auto &[name, value] : deviceMap.items()) {
        RegisteredDevice device;
        device.name = name;
        device.port = value.value("port", "");
        device.options = serialOptionsFromJson(value.value("serial", nlohmann::json::object()));

        if (!device.port.empty()) {
            devices_.push_back(std::move(device));
        }
    }

    std::sort(
        devices_.begin(),
        devices_.end(),
        [](const RegisteredDevice &left, const RegisteredDevice &right) {
            return left.name < right.name;
        });
}

void DeviceRegistry::save() const {
    const auto parent = storagePath_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    nlohmann::json root;
    root["version"] = 1;
    root["devices"] = nlohmann::json::object();

    for (const auto &device : devices_) {
        root["devices"][device.name] = {
            {"port", device.port},
            {"serial", serialOptionsToJson(device.options)},
        };
    }

    std::ofstream output(storagePath_, std::ios::trunc);
    if (!output) {
        throw std::runtime_error("failed to write device registry: " + storagePath_.string());
    }

    output << root.dump(4) << '\n';
}

std::filesystem::path DeviceRegistry::defaultStoragePath() {
    if (const char *xdgConfigHome = std::getenv("XDG_CONFIG_HOME")) {
        if (*xdgConfigHome != '\0') {
            return std::filesystem::path(xdgConfigHome) / "scpi-utils" / "devices.json";
        }
    }

    if (const char *home = std::getenv("HOME")) {
        if (*home != '\0') {
            return std::filesystem::path(home) / ".config" / "scpi-utils" / "devices.json";
        }
    }

    return std::filesystem::path(".scpi-utils-devices.json");
}

ScpiDevice::ScpiDevice() = default;

ScpiDevice::ScpiDevice(std::string port, SerialOptions options)
    : port_(std::move(port)), options_(std::move(options)) {}

ScpiDevice::ScpiDevice(const RegisteredDevice &device)
    : port_(device.port), options_(device.options) {
    open();
}

ScpiDevice::~ScpiDevice() {
    close();
}

ScpiDevice::ScpiDevice(ScpiDevice &&other) noexcept
    : fd_(other.fd_), port_(std::move(other.port_)), options_(std::move(other.options_)) {
    other.fd_ = -1;
}

ScpiDevice &ScpiDevice::operator=(ScpiDevice &&other) noexcept {
    if (this != &other) {
        close();
        fd_ = other.fd_;
        port_ = std::move(other.port_);
        options_ = std::move(other.options_);
        other.fd_ = -1;
    }
    return *this;
}

void ScpiDevice::open() {
    if (port_.empty()) {
        throw std::invalid_argument("serial port path is empty");
    }

    close();
    int flags = O_RDWR | O_NOCTTY;
    if (!options_.blockingIo) {
        flags |= O_NONBLOCK;
    }

    fd_ = ::open(port_.c_str(), flags);
    if (fd_ < 0) {
        throwSystemError("failed to open " + port_);
    }

    try {
        configureSerialPort();
        // Some USB serial bridges briefly toggle modem-control state on open/close.
        // Give the attached instrument a short settle window before the first command.
        std::this_thread::sleep_for(postOpenSettleDelay());
        firstCommandAfterOpen_ = true;
    } catch (...) {
        close();
        throw;
    }
}

void ScpiDevice::open(const std::string &port, SerialOptions options) {
    port_ = port;
    options_ = std::move(options);
    open();
}

void ScpiDevice::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    firstCommandAfterOpen_ = false;
}

bool ScpiDevice::isOpen() const {
    return fd_ >= 0;
}

const std::string &ScpiDevice::port() const {
    return port_;
}

const SerialOptions &ScpiDevice::options() const {
    return options_;
}

void ScpiDevice::writeCommand(const std::string &command) {
    if (!isOpen()) {
        throw std::logic_error("SCPI device is not open");
    }

    const std::string output = command + options_.lineEnding;
    std::size_t written = 0;
    while (written < output.size()) {
        pollfd descriptor{fd_, POLLOUT, 0};
        const int pollResult = ::poll(&descriptor, 1, timeoutMs(options_.writeTimeout));
        if (pollResult < 0) {
            throwSystemError("poll failed while writing to " + port_);
        }
        if (pollResult == 0) {
            throw std::runtime_error("timeout writing to " + port_);
        }

        const ssize_t result = ::write(fd_, output.data() + written, output.size() - written);
        if (result < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }
            throwSystemError("write failed for " + port_);
        }
        written += static_cast<std::size_t>(result);
    }
}

std::string ScpiDevice::query(const std::string &command) {
    const bool retryOnTimeout = firstCommandAfterOpen_;
    firstCommandAfterOpen_ = false;

    const int maxAttempts = retryOnTimeout ? 1 + firstQueryRetryCount() : 1;
    for (int attempt = 0; attempt < maxAttempts; ++attempt) {
        try {
            writeCommand(command);
            return readResponse();
        } catch (const std::runtime_error &error) {
            const std::string message = error.what();
            const bool retryable = message == "timeout reading from " + port_ ||
                                   message == "connection closed while reading from " + port_;
            if (attempt + 1 >= maxAttempts || !retryable) {
                throw;
            }

            close();
            open();
            firstCommandAfterOpen_ = false;
        }
    }

    throw std::logic_error("unreachable query retry state");
}

std::string ScpiDevice::identity() {
    return query("*IDN?");
}

void ScpiDevice::reset() {
    writeCommand("*RST");
}

void ScpiDevice::clearStatus() {
    writeCommand("*CLS");
}

void ScpiDevice::configureDvm(DvmFunction function) {
    writeCommand("CONF:" + toScpiFunction(function));
}

void ScpiDevice::configureDvm(DvmFunction function, const std::string &range) {
    writeCommand("CONF:" + toScpiFunction(function) + " " + range);
}

void ScpiDevice::configureDvm(DvmFunction function, double range, double resolution) {
    writeCommand(commandWithArguments("CONF:" + toScpiFunction(function), range, resolution));
}

void ScpiDevice::setDvmDisplay(DvmDisplay display, DvmFunction function) {
    const char *command = display == DvmDisplay::Main ? "FUNC" : "FUNC2";
    writeCommand(std::string(command) + " \"" + toScpiDisplayFunction(function) + "\"");
}

void ScpiDevice::disableSecondaryDvmDisplay() {
    writeCommand("FUNC2 \"NONE\"");
}

std::string ScpiDevice::read() {
    return query("READ?");
}

std::string ScpiDevice::measure(DvmFunction function) {
    return query("MEAS:" + toScpiFunction(function) + "?");
}

std::string ScpiDevice::measureDisplays() {
    return query("MEAS?");
}

std::string ScpiDevice::measureMainDisplay() {
    return query("MEAS1?");
}

std::string ScpiDevice::measureSecondaryDisplay() {
    return query("MEAS2?");
}

void ScpiDevice::initiate() {
    writeCommand("INIT");
}

std::string ScpiDevice::fetch() {
    return query("FETC?");
}

std::vector<std::string> ScpiDevice::listSerialByIdDevices(const std::string &directory) {
    std::vector<std::string> devices;
    std::error_code error;

    if (!std::filesystem::exists(directory, error)) {
        return devices;
    }

    for (const auto &entry : std::filesystem::directory_iterator(directory, error)) {
        if (error) {
            break;
        }
        devices.push_back(entry.path().string());
    }

    std::sort(devices.begin(), devices.end());
    return devices;
}

std::vector<IdentityResult> ScpiDevice::identifySerialByIdDevices(
    const SerialOptions &options,
    const std::string &directory) {
    std::vector<IdentityResult> results;
    const std::vector<int> baudRates{115200, 9600};

    for (const auto &port : listSerialByIdDevices(directory)) {
        IdentityResult result;
        result.port = port;

        for (const int baudRate : baudRates) {
            SerialOptions probeOptions = options;
            probeOptions.baudRate = baudRate;

            try {
                ScpiDevice device(port, probeOptions);
                device.open();
                result.identity = device.identity();
                result.baudRate = baudRate;
                if (!isValidScpiIdentity(result.identity)) {
                    throw std::runtime_error(
                        "invalid SCPI identity response at " + std::to_string(baudRate) +
                        " baud: " + result.identity);
                }
                result.success = true;
                result.error.clear();
                break;
            } catch (const std::exception &error) {
                result.success = false;
                result.baudRate = baudRate;
                result.error = error.what();
            }
        }

        results.push_back(std::move(result));
    }

    return results;
}

void ScpiDevice::configureSerialPort() {
    termios tty{};
    if (::tcgetattr(fd_, &tty) != 0) {
        throwSystemError("tcgetattr failed for " + port_);
    }

    ::cfmakeraw(&tty);
    tty.c_cflag |= static_cast<tcflag_t>(CLOCAL | CREAD);
    tty.c_cflag &= static_cast<tcflag_t>(~HUPCL);
    tty.c_cflag &= static_cast<tcflag_t>(~PARENB);
    tty.c_cflag &= static_cast<tcflag_t>(~CSTOPB);
    tty.c_cflag &= static_cast<tcflag_t>(~CSIZE);
    tty.c_cflag |= CS8;
    tty.c_cflag &= static_cast<tcflag_t>(~CRTSCTS);
    if (options_.blockingIo) {
        tty.c_cc[VMIN] = 0;
        tty.c_cc[VTIME] = timeoutDeciseconds(options_.readTimeout);
    } else {
        tty.c_cc[VMIN] = 0;
        tty.c_cc[VTIME] = 0;
    }

    const speed_t baud = baudToTermios(options_.baudRate);
    if (::cfsetispeed(&tty, baud) != 0 || ::cfsetospeed(&tty, baud) != 0) {
        throwSystemError("failed to set baud rate for " + port_);
    }

    if (::tcsetattr(fd_, TCSANOW, &tty) != 0) {
        throwSystemError("tcsetattr failed for " + port_);
    }

    ::tcflush(fd_, TCIFLUSH);
}

std::string ScpiDevice::readResponse() {
    if (!isOpen()) {
        throw std::logic_error("SCPI device is not open");
    }

    std::string response;
    char buffer[256]{};

    if (options_.blockingIo) {
        while (true) {
            const ssize_t count = ::read(fd_, buffer, sizeof(buffer));
            if (count < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throwSystemError("read failed for " + port_);
            }
            if (count == 0) {
                if (response.empty()) {
                    throw std::runtime_error("timeout reading from " + port_);
                }
                break;
            }

            response.append(buffer, static_cast<std::size_t>(count));
            if (response.find('\n') != std::string::npos) {
                break;
            }
        }

        return trimResponse(response);
    }

    while (true) {
        pollfd descriptor{fd_, POLLIN, 0};
        const int pollResult = ::poll(&descriptor, 1, timeoutMs(options_.readTimeout));
        if (pollResult < 0) {
            if (errno == EINTR) {
                continue;
            }
            throwSystemError("poll failed while reading from " + port_);
        }
        if (pollResult == 0) {
            if (response.empty()) {
                throw std::runtime_error("timeout reading from " + port_);
            }
            break;
        }
        if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 &&
            (descriptor.revents & POLLIN) == 0) {
            if (response.empty()) {
                throw std::runtime_error("connection closed while reading from " + port_);
            }
            break;
        }

        const ssize_t count = ::read(fd_, buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }
            throwSystemError("read failed for " + port_);
        }
        if (count == 0) {
            if (response.empty()) {
                throw std::runtime_error("connection closed while reading from " + port_);
            }
            break;
        }

        response.append(buffer, static_cast<std::size_t>(count));
        if (response.find('\n') != std::string::npos) {
            break;
        }
    }

    return trimResponse(response);
}

std::string toScpiFunction(DvmFunction function) {
    switch (function) {
    case DvmFunction::DcVoltage:
        return "VOLT:DC";
    case DvmFunction::AcVoltage:
        return "VOLT:AC";
    case DvmFunction::DcCurrent:
        return "CURR:DC";
    case DvmFunction::AcCurrent:
        return "CURR:AC";
    case DvmFunction::Resistance:
        return "RES";
    case DvmFunction::FourWireResistance:
        return "FRES";
    case DvmFunction::Frequency:
        return "FREQ";
    case DvmFunction::Period:
        return "PER";
    case DvmFunction::Capacitance:
        return "CAP";
    case DvmFunction::Continuity:
        return "CONT";
    case DvmFunction::Diode:
        return "DIOD";
    }

    throw std::invalid_argument("unknown DVM function");
}

std::string toScpiDisplayFunction(DvmFunction function) {
    switch (function) {
    case DvmFunction::DcVoltage:
        return "VOLT";
    case DvmFunction::AcVoltage:
        return "VOLT AC";
    case DvmFunction::DcCurrent:
        return "CURR";
    case DvmFunction::AcCurrent:
        return "CURR AC";
    case DvmFunction::Resistance:
        return "RES";
    case DvmFunction::FourWireResistance:
        return "FRES";
    case DvmFunction::Frequency:
        return "FREQ";
    case DvmFunction::Period:
        return "PER";
    case DvmFunction::Capacitance:
        return "CAP";
    case DvmFunction::Continuity:
        return "CONT";
    case DvmFunction::Diode:
        return "DIOD";
    }

    throw std::invalid_argument("unknown DVM function");
}

std::optional<ScpiIdentity> parseScpiIdentity(const std::string &identity) {
    std::vector<std::string> fields;
    std::size_t start = 0;

    while (start <= identity.size()) {
        const std::size_t end = identity.find(',', start);
        const std::string field = trimWhitespace(identity.substr(
            start,
            end == std::string::npos ? std::string::npos : end - start));

        if (field.empty()) {
            return std::nullopt;
        }

        fields.push_back(field);

        if (end == std::string::npos) {
            break;
        }

        start = end + 1;
    }

    if (fields.size() != 4 && fields.size() != 5) {
        return std::nullopt;
    }

    return ScpiIdentity{
        fields[0],
        fields[1],
        fields[2],
        fields[3],
        std::move(fields),
    };
}

bool isValidScpiIdentity(const std::string &identity) {
    return parseScpiIdentity(identity).has_value();
}

} // namespace scpi
