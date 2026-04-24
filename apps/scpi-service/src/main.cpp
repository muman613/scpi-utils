#include "scpi_device/scpi_device.h"
#include "scpi-service-adaptor.h"

#include <sdbus-c++/sdbus-c++.h>

#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr const char *kServiceName = "org.scpi";
constexpr const char *kObjectPath = "/org/scpi";
using VariantMap = std::map<std::string, sdbus::Variant>;
using DeviceRecord = sdbus::Struct<std::string, std::string, VariantMap>;
using ScanRecord = sdbus::Struct<std::string, std::string, bool, VariantMap>;

std::string lowerAscii(std::string value) {
    for (char &ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

VariantMap serialOptionsToVariantMap(const scpi::SerialOptions &options) {
    VariantMap values;
    values.emplace("baudRate", options.baudRate);
    values.emplace("readTimeoutMs", static_cast<std::int64_t>(options.readTimeout.count()));
    values.emplace("writeTimeoutMs", static_cast<std::int64_t>(options.writeTimeout.count()));
    values.emplace("lineEnding", options.lineEnding);
    return values;
}

scpi::SerialOptions serialOptionsFromVariantMap(const VariantMap &values) {
    scpi::SerialOptions options;

    if (const auto found = values.find("baudRate"); found != values.end()) {
        options.baudRate = found->second.get<std::int32_t>();
    }
    if (const auto found = values.find("readTimeoutMs"); found != values.end()) {
        options.readTimeout = std::chrono::milliseconds(found->second.get<std::int64_t>());
    }
    if (const auto found = values.find("writeTimeoutMs"); found != values.end()) {
        options.writeTimeout = std::chrono::milliseconds(found->second.get<std::int64_t>());
    }
    if (const auto found = values.find("lineEnding"); found != values.end()) {
        options.lineEnding = found->second.get<std::string>();
    }

    return options;
}

scpi::DvmFunction parseDvmFunction(const std::string &value) {
    const std::string function = lowerAscii(value);

    if (function == "vdc" || function == "dcv" || function == "dc-voltage" ||
        function == "voltage-dc" || function == "volt:dc") {
        return scpi::DvmFunction::DcVoltage;
    }
    if (function == "vac" || function == "acv" || function == "ac-voltage" ||
        function == "voltage-ac" || function == "volt:ac") {
        return scpi::DvmFunction::AcVoltage;
    }
    if (function == "idc" || function == "dci" || function == "dc-current" ||
        function == "current-dc" || function == "curr:dc") {
        return scpi::DvmFunction::DcCurrent;
    }
    if (function == "iac" || function == "aci" || function == "ac-current" ||
        function == "current-ac" || function == "curr:ac") {
        return scpi::DvmFunction::AcCurrent;
    }
    if (function == "res" || function == "resistance" || function == "ohm" ||
        function == "ohms") {
        return scpi::DvmFunction::Resistance;
    }
    if (function == "fres" || function == "4w-resistance" ||
        function == "four-wire-resistance") {
        return scpi::DvmFunction::FourWireResistance;
    }
    if (function == "freq" || function == "frequency") {
        return scpi::DvmFunction::Frequency;
    }
    if (function == "per" || function == "period") {
        return scpi::DvmFunction::Period;
    }
    if (function == "cap" || function == "capacitance") {
        return scpi::DvmFunction::Capacitance;
    }
    if (function == "cont" || function == "continuity") {
        return scpi::DvmFunction::Continuity;
    }
    if (function == "diod" || function == "diode") {
        return scpi::DvmFunction::Diode;
    }

    throw std::invalid_argument("unknown DVM function: " + value);
}

scpi::DvmDisplay parseDvmDisplay(const std::string &value) {
    const std::string display = lowerAscii(value);
    if (display == "main" || display == "1") {
        return scpi::DvmDisplay::Main;
    }
    if (display == "secondary" || display == "sub" || display == "2") {
        return scpi::DvmDisplay::Secondary;
    }

    throw std::invalid_argument("unknown DVM display: " + value);
}

void throwDbusError(const std::string &name, const std::string &message) {
    throw sdbus::Error(name, message.c_str());
}

bool isRetryableTransportError(const std::exception &error, const std::string &port) {
    const std::string message = error.what();
    return message == "timeout reading from " + port ||
           message == "connection closed while reading from " + port;
}

std::string sanitizeForDbus(const std::string &value) {
    std::ostringstream escaped;

    for (unsigned char ch : value) {
        if (ch >= 0x20 && ch <= 0x7e) {
            escaped << static_cast<char>(ch);
        } else if (ch == '\n') {
            escaped << "\\n";
        } else if (ch == '\r') {
            escaped << "\\r";
        } else if (ch == '\t') {
            escaped << "\\t";
        } else {
            escaped << "\\x";
            constexpr char digits[] = "0123456789ABCDEF";
            escaped << digits[(ch >> 4) & 0x0f] << digits[ch & 0x0f];
        }
    }

    return escaped.str();
}

class ScpiService
{
public:
    explicit ScpiService(sdbus::IConnection &connection)
        : object_(sdbus::createObject(connection, kObjectPath))
        , adaptor_(*this, *object_) {
        object_->finishRegistration();
    }

private:
    class ServiceAdaptor
        : public org::scpi::Registry_adaptor
        , public org::scpi::DeviceControl_adaptor {
    public:
        ServiceAdaptor(ScpiService &service, sdbus::IObject &object)
            : org::scpi::Registry_adaptor(object)
            , org::scpi::DeviceControl_adaptor(object)
            , service_(service) {}

    private:
        std::vector<DeviceRecord> ListDevices() override { return service_.ListDevices(); }
        DeviceRecord GetDevice(const std::string &name) override { return service_.GetDevice(name); }
        void AddOrUpdateDevice(
            const std::string &name,
            const std::string &port,
            const VariantMap &options) override {
            service_.AddOrUpdateDevice(name, port, options);
        }
        bool RemoveDevice(const std::string &name) override { return service_.RemoveDevice(name); }
        std::vector<ScanRecord> ScanSerialDevices() override { return service_.ScanSerialDevices(); }
        void Open(const std::string &name) override { service_.Open(name); }
        void Close(const std::string &name) override { service_.Close(name); }
        bool IsOpen(const std::string &name) override { return service_.IsOpen(name); }
        std::string QueryIdentity(const std::string &name) override { return service_.QueryIdentity(name); }
        void WriteCommand(const std::string &name, const std::string &command) override {
            service_.WriteCommand(name, command);
        }
        std::string Query(const std::string &name, const std::string &command) override {
            return service_.Query(name, command);
        }
        void Reset(const std::string &name) override { service_.Reset(name); }
        void ClearStatus(const std::string &name) override { service_.ClearStatus(name); }
        void ConfigureDvm(
            const std::string &name,
            const std::string &function,
            const std::string &range) override {
            service_.ConfigureDvm(name, function, range);
        }
        void SetDisplay(
            const std::string &name,
            const std::string &display,
            const std::string &function) override {
            service_.SetDisplay(name, display, function);
        }
        std::string ReadDisplay(const std::string &name, const std::string &display) override {
            return service_.ReadDisplay(name, display);
        }
        std::string ReadDisplays(const std::string &name) override { return service_.ReadDisplays(name); }

    public:
        using org::scpi::Registry_adaptor::emitDeviceAdded;
        using org::scpi::Registry_adaptor::emitDeviceRemoved;
        using org::scpi::Registry_adaptor::emitDeviceUpdated;
        using org::scpi::Registry_adaptor::emitRegistryChanged;
        using org::scpi::DeviceControl_adaptor::emitDeviceError;
        using org::scpi::DeviceControl_adaptor::emitDeviceStateChanged;

    private:
        ScpiService &service_;
    };

    struct ManagedDevice {
        scpi::RegisteredDevice registration;
        std::unique_ptr<scpi::ScpiDevice> device;
    };

    std::mutex mutex_;
    std::unique_ptr<sdbus::IObject> object_;
    ServiceAdaptor adaptor_;
    std::unordered_map<std::string, ManagedDevice> openDevices_;

    scpi::RegisteredDevice requireRegisteredDevice(const std::string &name) {
        scpi::DeviceRegistry registry;
        const auto device = registry.getDevice(name);
        if (!device) {
            throwDbusError("org.scpi.Error.NotFound", "device not registered: " + name);
        }
        return *device;
    }

    scpi::ScpiDevice &getOrOpenManagedDeviceLocked(const std::string &name) {
        const auto registered = requireRegisteredDevice(name);
        auto &entry = openDevices_[name];
        if (!entry.device) {
            entry.registration = registered;
            entry.device = std::make_unique<scpi::ScpiDevice>(registered.port, registered.options);
            entry.device->open();
            adaptor_.emitDeviceStateChanged(name, "open");
        }
        return *entry.device;
    }

    void dropManagedDeviceLocked(const std::string &name) {
        openDevices_.erase(name);
        adaptor_.emitDeviceStateChanged(name, "closed");
    }

    std::vector<DeviceRecord> ListDevices() {
        scpi::DeviceRegistry registry;
        std::vector<DeviceRecord> records;

        for (const auto &device : registry.devices()) {
            records.emplace_back(device.name, device.port, serialOptionsToVariantMap(device.options));
        }

        return records;
    }

    DeviceRecord GetDevice(const std::string &name) {
        const auto device = requireRegisteredDevice(name);
        return DeviceRecord(device.name, device.port, serialOptionsToVariantMap(device.options));
    }

    void AddOrUpdateDevice(
        const std::string &name,
        const std::string &port,
        const VariantMap &options) {
        scpi::DeviceRegistry registry;
        const bool existed = registry.getDevice(name).has_value();
        registry.setDevice(name, port, serialOptionsFromVariantMap(options));

        {
            std::lock_guard<std::mutex> lock(mutex_);
            openDevices_.erase(name);
        }

        adaptor_.emitRegistryChanged();
        if (existed) {
            adaptor_.emitDeviceUpdated(name);
        } else {
            adaptor_.emitDeviceAdded(name);
        }
    }

    bool RemoveDevice(const std::string &name) {
        scpi::DeviceRegistry registry;
        const bool removed = registry.removeDevice(name);
        if (!removed) {
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            openDevices_.erase(name);
        }

        adaptor_.emitRegistryChanged();
        adaptor_.emitDeviceRemoved(name);
        return true;
    }

    std::vector<ScanRecord> ScanSerialDevices() {
        const auto results = scpi::ScpiDevice::identifySerialByIdDevices();
        std::vector<ScanRecord> records;

        for (const auto &result : results) {
            VariantMap details;
            details.emplace("baudRate", result.baudRate);
            if (!result.error.empty()) {
                details.emplace("error", sanitizeForDbus(result.error));
            }
            records.emplace_back(
                result.port,
                sanitizeForDbus(result.identity),
                result.success,
                std::move(details));
        }

        return records;
    }

    void Open(const std::string &name) {
        std::lock_guard<std::mutex> lock(mutex_);
        try {
            (void)getOrOpenManagedDeviceLocked(name);
        } catch (const sdbus::Error &) {
            throw;
        } catch (const std::exception &error) {
            adaptor_.emitDeviceError(name, error.what());
            throwDbusError("org.scpi.Error.OpenFailed", error.what());
        }
    }

    void Close(const std::string &name) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = openDevices_.find(name);
        if (found == openDevices_.end()) {
            return;
        }

        openDevices_.erase(found);
        adaptor_.emitDeviceStateChanged(name, "closed");
    }

    bool IsOpen(const std::string &name) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = openDevices_.find(name);
        return found != openDevices_.end() && found->second.device && found->second.device->isOpen();
    }

    std::string QueryIdentity(const std::string &name) {
        return performOnDevice(name, [&](scpi::ScpiDevice &device) { return device.identity(); });
    }

    void WriteCommand(const std::string &name, const std::string &command) {
        performOnDevice(name, [&](scpi::ScpiDevice &device) {
            device.writeCommand(command);
            return 0;
        });
    }

    std::string Query(const std::string &name, const std::string &command) {
        return performOnDevice(name, [&](scpi::ScpiDevice &device) { return device.query(command); });
    }

    void Reset(const std::string &name) {
        performOnDevice(name, [&](scpi::ScpiDevice &device) {
            device.reset();
            return 0;
        });
    }

    void ClearStatus(const std::string &name) {
        performOnDevice(name, [&](scpi::ScpiDevice &device) {
            device.clearStatus();
            return 0;
        });
    }

    void ConfigureDvm(
        const std::string &name,
        const std::string &function,
        const std::string &range) {
        const scpi::DvmFunction mode = parseDvmFunction(function);
        performOnDevice(name, [&](scpi::ScpiDevice &device) {
            if (range.empty()) {
                device.configureDvm(mode);
            } else {
                device.configureDvm(mode, range);
            }
            device.setDvmDisplay(scpi::DvmDisplay::Main, mode);
            return 0;
        });
    }

    void SetDisplay(
        const std::string &name,
        const std::string &display,
        const std::string &function) {
        performOnDevice(name, [&](scpi::ScpiDevice &device) {
            const scpi::DvmDisplay target = parseDvmDisplay(display);
            if (lowerAscii(function) == "none") {
                if (target != scpi::DvmDisplay::Secondary) {
                    throw std::invalid_argument("only the secondary display can be disabled");
                }
                device.disableSecondaryDvmDisplay();
            } else {
                device.setDvmDisplay(target, parseDvmFunction(function));
            }
            return 0;
        });
    }

    std::string ReadDisplay(const std::string &name, const std::string &display) {
        return performOnDevice(name, [&](scpi::ScpiDevice &device) {
            const std::string normalized = lowerAscii(display);
            if (normalized == "main" || normalized == "1") {
                return device.measureMainDisplay();
            }
            if (normalized == "secondary" || normalized == "sub" || normalized == "2") {
                return device.measureSecondaryDisplay();
            }

            throw std::invalid_argument("unknown DVM read display: " + display);
        });
    }

    std::string ReadDisplays(const std::string &name) {
        return performOnDevice(name, [&](scpi::ScpiDevice &device) { return device.measureDisplays(); });
    }

    template <typename Fn>
    auto performOnDevice(const std::string &name, Fn &&fn) -> decltype(fn(std::declval<scpi::ScpiDevice &>())) {
        std::lock_guard<std::mutex> lock(mutex_);

        for (int attempt = 0; attempt < 2; ++attempt) {
            try {
                return fn(getOrOpenManagedDeviceLocked(name));
            } catch (const sdbus::Error &) {
                throw;
            } catch (const std::exception &error) {
                const auto found = openDevices_.find(name);
                const bool canRetry = attempt == 0 &&
                    found != openDevices_.end() &&
                    found->second.device &&
                    isRetryableTransportError(error, found->second.device->port());

                if (canRetry) {
                    dropManagedDeviceLocked(name);
                    adaptor_.emitDeviceStateChanged(name, "reconnecting");
                    continue;
                }

                adaptor_.emitDeviceError(name, error.what());
                throwDbusError("org.scpi.Error.IO", error.what());
            }
        }

        throw std::logic_error("unreachable device operation state");
    }
};

bool useSystemBus(int argc, char **argv) {
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--system") {
            return true;
        }
        if (arg == "--session") {
            return false;
        }
    }

    return false;
}

void printUsage(const char *programName) {
    std::cerr
        << "Usage:\n"
        << "  " << programName << " [--session|--system]\n";
}

} // namespace

int main(int argc, char **argv) {
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        }
        if (arg != "--session" && arg != "--system") {
            printUsage(argv[0]);
            return 2;
        }
    }

    try {
        auto connection = useSystemBus(argc, argv)
            ? sdbus::createSystemBusConnection(kServiceName)
            : sdbus::createSessionBusConnection(kServiceName);

        ScpiService service(*connection);
        connection->enterEventLoop();
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
