#include "scpi_device/scpi_device.h"
#include "scpi-service-proxy.h"

#include <sdbus-c++/sdbus-c++.h>

#include <chrono>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {

constexpr const char *kServiceName = "org.scpi";
constexpr const char *kObjectPath = "/org/scpi";

using VariantMap = std::map<std::string, sdbus::Variant>;
using DeviceRecord = sdbus::Struct<std::string, std::string, VariantMap>;
using ScanRecord = sdbus::Struct<std::string, std::string, bool, VariantMap>;

enum class BusType {
    Session,
    System,
};

void printUsage() {
    std::cerr
        << "Usage:\n"
        << "  scpi-util [--session|--system] --version\n"
        << "  scpi-util [--session|--system] version\n"
        << "  scpi-util [--session|--system] [-v] add <device-name> <serial-port-device> [options]\n"
        << "  scpi-util [--session|--system] [-v] rm <device-name>\n"
        << "  scpi-util [--session|--system] [-v] info <device-name>\n"
        << "  scpi-util [--session|--system] [-v] devices\n"
        << "  scpi-util [--session|--system] [-v] list\n"
        << "  scpi-util [--session|--system] [-v] scan\n"
        << "  scpi-util [--session|--system] [-v] dvm <device-name> configure <function> [--range <value|AUTO|MIN|MAX|DEF>]\n"
        << "  scpi-util [--session|--system] [-v] dvm <device-name> display <main|secondary> <function|none>\n"
        << "  scpi-util [--session|--system] [-v] dvm <device-name> capture <function> [--range <value|AUTO|MIN|MAX|DEF>]\n"
        << "  scpi-util [--session|--system] [-v] dvm <device-name> read [main|secondary|both]\n"
        << "\n"
        << "Bus selection:\n"
        << "  --session  Use the session bus (default)\n"
        << "  --system   Use the system bus\n"
        << "\n"
        << "Add options:\n"
        << "  --baud <rate>\n"
        << "  --read-timeout <milliseconds>\n"
        << "  --write-timeout <milliseconds>\n"
        << "  --line-ending <lf|crlf|cr|literal>\n";
}

int printVersion() {
    std::cout << "scpi-util " << SCPI_UTIL_VERSION << '\n';
    std::cout << "scpi-device " << scpi::version() << '\n';
    return 0;
}

std::string lowerAscii(std::string value) {
    for (char &ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string upperAscii(std::string value) {
    for (char &ch : value) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return value;
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
    if (function == "fres" || function == "4w-resistance" || function == "four-wire-resistance") {
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

std::optional<std::string> parseRangeOption(
    const std::vector<std::string> &args,
    std::size_t firstOptionIndex) {
    std::optional<std::string> range;

    for (std::size_t index = firstOptionIndex; index < args.size(); ++index) {
        const std::string &option = args[index];
        if (option != "--range") {
            throw std::invalid_argument("unknown DVM option: " + option);
        }
        if (index + 1 >= args.size()) {
            throw std::invalid_argument("missing value for " + option);
        }

        std::string value = args[++index];
        const std::string normalized = lowerAscii(value);
        if (normalized == "auto" || normalized == "min" || normalized == "minimum" ||
            normalized == "max" || normalized == "maximum" || normalized == "def" ||
            normalized == "default") {
            if (normalized == "minimum") {
                value = "MIN";
            } else if (normalized == "maximum") {
                value = "MAX";
            } else if (normalized == "default") {
                value = "DEF";
            } else {
                value = upperAscii(value);
            }
        }
        range = value;
    }

    return range;
}

int parseIntOption(const std::string &name, const std::string &value) {
    std::size_t parsed = 0;
    const int result = std::stoi(value, &parsed);
    if (parsed != value.size()) {
        throw std::invalid_argument("invalid value for " + name + ": " + value);
    }
    return result;
}

std::string parseLineEnding(const std::string &value) {
    if (value == "lf") {
        return "\n";
    }
    if (value == "crlf") {
        return "\r\n";
    }
    if (value == "cr") {
        return "\r";
    }
    return value;
}

std::string formatLineEnding(const std::string &value) {
    if (value == "\n") {
        return "lf";
    }
    if (value == "\r\n") {
        return "crlf";
    }
    if (value == "\r") {
        return "cr";
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

class ScpiClient
    : public sdbus::ProxyInterfaces<org::scpi::Registry_proxy, org::scpi::DeviceControl_proxy> {
public:
    explicit ScpiClient(BusType busType)
        : sdbus::ProxyInterfaces<org::scpi::Registry_proxy, org::scpi::DeviceControl_proxy>(
              createConnection(busType),
              kServiceName,
              kObjectPath) {
        registerProxy();
    }

    ~ScpiClient() {
        unregisterProxy();
    }

private:
    static std::unique_ptr<sdbus::IConnection> createConnection(BusType busType) {
        if (busType == BusType::System) {
            return sdbus::createSystemBusConnection();
        }
        return sdbus::createSessionBusConnection();
    }

    void onRegistryChanged() override {}
    void onDeviceAdded(const std::string &) override {}
    void onDeviceUpdated(const std::string &) override {}
    void onDeviceRemoved(const std::string &) override {}
    void onDeviceStateChanged(const std::string &, const std::string &) override {}
    void onDeviceError(const std::string &, const std::string &) override {}
};

scpi::SerialOptions parseSerialOptions(
    const std::vector<std::string> &args,
    std::size_t firstOptionIndex) {
    scpi::SerialOptions options;

    for (std::size_t index = firstOptionIndex; index < args.size(); ++index) {
        const std::string &option = args[index];
        if (index + 1 >= args.size()) {
            throw std::invalid_argument("missing value for " + option);
        }

        const std::string &value = args[++index];
        if (option == "--baud") {
            options.baudRate = parseIntOption(option, value);
        } else if (option == "--read-timeout") {
            options.readTimeout = std::chrono::milliseconds(parseIntOption(option, value));
        } else if (option == "--write-timeout") {
            options.writeTimeout = std::chrono::milliseconds(parseIntOption(option, value));
        } else if (option == "--line-ending") {
            options.lineEnding = parseLineEnding(value);
        } else {
            throw std::invalid_argument("unknown add option: " + option);
        }
    }

    return options;
}

int addDevice(ScpiClient &client, const std::vector<std::string> &args) {
    if (args.size() < 4) {
        printUsage();
        return 2;
    }

    const std::string name = args[2];
    const std::string port = args[3];
    const scpi::SerialOptions options = parseSerialOptions(args, 4);

    client.AddOrUpdateDevice(name, port, serialOptionsToVariantMap(options));

    std::cout << "registered " << name << " at " << port << '\n';
    return 0;
}

int removeDevice(ScpiClient &client, const std::vector<std::string> &args) {
    if (args.size() != 3) {
        printUsage();
        return 2;
    }

    const std::string name = args[2];

    if (!client.RemoveDevice(name)) {
        std::cerr << "device not registered: " << name << '\n';
        return 1;
    }

    std::cout << "removed " << name << '\n';
    return 0;
}

int infoDevice(ScpiClient &client, const std::vector<std::string> &args) {
    if (args.size() != 3) {
        printUsage();
        return 2;
    }

    const std::string name = args[2];
    const DeviceRecord device = client.GetDevice(name);
    const scpi::SerialOptions options = serialOptionsFromVariantMap(device.get<2>());

    std::cout << "name: " << device.get<0>() << '\n';
    std::cout << "port: " << device.get<1>() << '\n';
    std::cout << "baud-rate: " << options.baudRate << '\n';
    std::cout << "read-timeout-ms: " << options.readTimeout.count() << '\n';
    std::cout << "write-timeout-ms: " << options.writeTimeout.count() << '\n';
    std::cout << "line-ending: " << formatLineEnding(options.lineEnding) << '\n';

    try {
        const std::string identity = client.QueryIdentity(name);
        const auto parsed = scpi::parseScpiIdentity(identity);
        if (!parsed) {
            std::cout << "identity-error: invalid SCPI identity response: " << identity << '\n';
            return 1;
        }
        std::cout << "manufacturer: " << parsed->manufacturer << '\n';
        std::cout << "model: " << parsed->model << '\n';
        std::cout << "software-version: " << parsed->softwareVersion << '\n';
        std::cout << "firmware-version: " << parsed->firmwareVersion << '\n';
    } catch (const std::exception &error) {
        std::cout << "identity-error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}

int devicesCommand(ScpiClient &client) {
    const auto devices = client.ListDevices();
    if (devices.empty()) {
        std::cout << "no configured devices\n";
        return 0;
    }

    for (const auto &device : devices) {
        const scpi::SerialOptions options = serialOptionsFromVariantMap(device.get<2>());
        std::cout << device.get<0>() << '\n';
        std::cout << "  port: " << device.get<1>() << '\n';
        std::cout << "  baud-rate: " << options.baudRate << '\n';
        std::cout << "  read-timeout-ms: " << options.readTimeout.count() << '\n';
        std::cout << "  write-timeout-ms: " << options.writeTimeout.count() << '\n';
        std::cout << "  line-ending: " << formatLineEnding(options.lineEnding) << '\n';
    }

    return 0;
}

int listIdentities(ScpiClient &client) {
    const auto devices = client.ListDevices();
    if (devices.empty()) {
        std::cout << "no configured devices\n";
        return 0;
    }

    for (const auto &device : devices) {
        const std::string name = device.get<0>();
        std::cout << name << '\n';
        try {
            const std::string identity = client.QueryIdentity(name);
            const auto parsed = scpi::parseScpiIdentity(identity);
            if (!parsed) {
                std::cout << "  identity-error: invalid SCPI identity response: "
                          << identity << '\n';
                continue;
            }

            std::cout << "  manufacturer: " << parsed->manufacturer << '\n';
            std::cout << "  model: " << parsed->model << '\n';
            std::cout << "  software-version: " << parsed->softwareVersion << '\n';
            std::cout << "  firmware-version: " << parsed->firmwareVersion << '\n';
        } catch (const std::exception &error) {
            std::cout << "  identity-error: " << error.what() << '\n';
        }
    }

    return 0;
}

int scanDevices(ScpiClient &client, bool verbose) {
    const auto results = client.ScanSerialDevices();

    if (results.empty()) {
        std::cout << "no devices found in /dev/serial/by-id\n";
        return 0;
    }

    std::size_t successCount = 0;
    for (const auto &result : results) {
        const VariantMap details = result.get<3>();
        const int baudRate = details.at("baudRate").get<std::int32_t>();
        if (result.get<2>()) {
            ++successCount;
            std::cout << result.get<0>() << '\n';
            std::cout << "  baud-rate: " << baudRate << '\n';
            std::cout << "  identity: " << result.get<1>() << '\n';
        } else if (verbose) {
            std::cout << result.get<0>() << '\n';
            const auto found = details.find("error");
            if (found != details.end()) {
                std::cout << "  error: " << found->second.get<std::string>() << '\n';
            } else {
                std::cout << "  error: unknown error\n";
            }
        }
    }

    if (successCount == 0 && !verbose) {
        std::cout << "no SCPI devices found in /dev/serial/by-id\n";
    }

    return 0;
}

int dvmCommand(ScpiClient &client, const std::vector<std::string> &args) {
    if (args.size() < 4) {
        printUsage();
        return 2;
    }

    const std::string deviceName = args[2];
    const std::string command = args[3];

    if (command == "configure") {
        if (args.size() < 5) {
            printUsage();
            return 2;
        }

        (void)parseDvmFunction(args[4]);
        const auto range = parseRangeOption(args, 5);
        client.ConfigureDvm(deviceName, args[4], range.value_or(""));
        std::cout << "configured " << args[4] << " on main display\n";
        return 0;
    }

    if (command == "display") {
        if (args.size() != 6) {
            printUsage();
            return 2;
        }

        const scpi::DvmDisplay display = parseDvmDisplay(args[4]);
        if (lowerAscii(args[5]) == "none") {
            if (display != scpi::DvmDisplay::Secondary) {
                throw std::invalid_argument("only the secondary display can be disabled");
            }
        } else {
            (void)parseDvmFunction(args[5]);
        }
        client.SetDisplay(deviceName, args[4], args[5]);
        std::cout << "display updated\n";
        return 0;
    }

    if (command == "capture") {
        if (args.size() < 5) {
            printUsage();
            return 2;
        }

        (void)parseDvmFunction(args[4]);
        const auto range = parseRangeOption(args, 5);
        client.ConfigureDvm(deviceName, args[4], range.value_or(""));
        std::cout << client.ReadDisplay(deviceName, "main") << '\n';
        return 0;
    }

    if (command == "read") {
        const std::string display = args.size() >= 5 ? lowerAscii(args[4]) : "main";
        if (args.size() > 5) {
            printUsage();
            return 2;
        }

        if (display == "main" || display == "1") {
            std::cout << client.ReadDisplay(deviceName, "main") << '\n';
        } else if (display == "secondary" || display == "sub" || display == "2") {
            std::cout << client.ReadDisplay(deviceName, "secondary") << '\n';
        } else if (display == "both" || display == "all") {
            std::cout << client.ReadDisplays(deviceName) << '\n';
        } else {
            throw std::invalid_argument("unknown DVM read display: " + display);
        }
        return 0;
    }

    printUsage();
    return 2;
}

} // namespace

int main(int argc, char **argv) {
    bool verbose = false;
    BusType busType = BusType::Session;
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    args.emplace_back(argv[0]);

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "--system") {
            busType = BusType::System;
        } else if (arg == "--session") {
            busType = BusType::Session;
        } else {
            args.push_back(arg);
        }
    }

    if (args.size() < 2) {
        printUsage();
        return 2;
    }

    try {
        const std::string command = args[1];

        if (command == "--help" || command == "-h" || command == "help") {
            printUsage();
            return 0;
        }
        if (command == "--version" || command == "version") {
            return printVersion();
        }

        ScpiClient client(busType);

        if (command == "add") {
            return addDevice(client, args);
        }
        if (command == "rm") {
            return removeDevice(client, args);
        }
        if (command == "info") {
            return infoDevice(client, args);
        }
        if (command == "list") {
            return listIdentities(client);
        }
        if (command == "devices") {
            return devicesCommand(client);
        }
        if (command == "scan") {
            return scanDevices(client, verbose);
        }
        if (command == "dvm") {
            return dvmCommand(client, args);
        }

        printUsage();
        return 2;
    } catch (const sdbus::Error &error) {
        std::cerr << "error: " << error.getName() << ": " << error.getMessage() << '\n';
        return 1;
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
