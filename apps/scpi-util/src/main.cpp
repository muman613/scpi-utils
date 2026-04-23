#include "scpi_device/scpi_device.h"

#include <chrono>
#include <cctype>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void printUsage() {
    std::cerr
        << "Usage:\n"
        << "  scpi-util --version\n"
        << "  scpi-util version\n"
        << "  scpi-util [-v] add <device-name> <serial-port-device> [options]\n"
        << "  scpi-util [-v] rm <device-name>\n"
        << "  scpi-util [-v] info <device-name>\n"
        << "  scpi-util [-v] devices\n"
        << "  scpi-util [-v] list\n"
        << "  scpi-util [-v] scan\n"
        << "  scpi-util [-v] dvm <device-name> configure <function> [--range <value|AUTO|MIN|MAX|DEF>]\n"
        << "  scpi-util [-v] dvm <device-name> display <main|secondary> <function|none>\n"
        << "  scpi-util [-v] dvm <device-name> capture <function> [--range <value|AUTO|MIN|MAX|DEF>]\n"
        << "  scpi-util [-v] dvm <device-name> read [main|secondary|both]\n"
        << "\n"
        << "Add options:\n"
        << "  --baud <rate>\n"
        << "  --read-timeout <milliseconds>\n"
        << "  --write-timeout <milliseconds>\n"
        << "  --line-ending <lf|crlf|cr|literal>\n";
}

int printVersion() {
    std::cout << "scpi-util " << scpi::version() << '\n';
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

int addDevice(const std::vector<std::string> &args) {
    if (args.size() < 4) {
        printUsage();
        return 2;
    }

    const std::string name = args[2];
    const std::string port = args[3];
    const scpi::SerialOptions options = parseSerialOptions(args, 4);

    scpi::DeviceRegistry registry;
    registry.setDevice(name, port, options);

    std::cout << "registered " << name << " at " << port << '\n';
    return 0;
}

int removeDevice(const std::vector<std::string> &args) {
    if (args.size() != 3) {
        printUsage();
        return 2;
    }

    const std::string name = args[2];
    scpi::DeviceRegistry registry;

    if (!registry.removeDevice(name)) {
        std::cerr << "device not registered: " << name << '\n';
        return 1;
    }

    std::cout << "removed " << name << '\n';
    return 0;
}

int infoDevice(const std::vector<std::string> &args) {
    if (args.size() != 3) {
        printUsage();
        return 2;
    }

    const std::string name = args[2];
    scpi::DeviceRegistry registry;
    const auto device = registry.getDevice(name);

    if (!device) {
        std::cerr << "device not registered: " << name << '\n';
        return 1;
    }

    std::cout << "name: " << device->name << '\n';
    std::cout << "port: " << device->port << '\n';
    std::cout << "baud-rate: " << device->options.baudRate << '\n';
    std::cout << "read-timeout-ms: " << device->options.readTimeout.count() << '\n';
    std::cout << "write-timeout-ms: " << device->options.writeTimeout.count() << '\n';

    try {
        scpi::ScpiDevice scpiDevice(*device);
        const std::string identity = scpiDevice.identity();
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

int devicesCommand() {
    const scpi::DeviceRegistry registry;
    const auto devices = registry.devices();
    if (devices.empty()) {
        std::cout << "no configured devices\n";
        return 0;
    }

    for (const auto &device : devices) {
        std::cout << device.name << '\n';
        std::cout << "  port: " << device.port << '\n';
        std::cout << "  baud-rate: " << device.options.baudRate << '\n';
        std::cout << "  read-timeout-ms: " << device.options.readTimeout.count() << '\n';
        std::cout << "  write-timeout-ms: " << device.options.writeTimeout.count() << '\n';
        std::cout << "  line-ending: ";
        if (device.options.lineEnding == "\n") {
            std::cout << "lf";
        } else if (device.options.lineEnding == "\r\n") {
            std::cout << "crlf";
        } else if (device.options.lineEnding == "\r") {
            std::cout << "cr";
        } else {
            std::cout << device.options.lineEnding;
        }
        std::cout << '\n';
    }

    return 0;
}

int listIdentities() {
    const scpi::DeviceRegistry registry;
    const auto devices = registry.devices();
    if (devices.empty()) {
        std::cout << "no configured devices\n";
        return 0;
    }

    for (const auto &device : devices) {
        std::cout << device.name << '\n';
        try {
            scpi::ScpiDevice scpiDevice(device);
            const std::string identity = scpiDevice.identity();
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
            std::cout << "  identity: " << identity << '\n';
        } catch (const std::exception &error) {
            std::cout << "  identity-error: " << error.what() << '\n';
        }
    }

    return 0;
}

int scanDevices(bool verbose) {
    const auto results = scpi::ScpiDevice::identifySerialByIdDevices();

    if (results.empty()) {
        std::cout << "no devices found in /dev/serial/by-id\n";
        return 0;
    }

    std::size_t successCount = 0;
    for (const auto &result : results) {
        if (result.success) {
            ++successCount;
            std::cout << result.port << '\n';
            std::cout << "  baud-rate: " << result.baudRate << '\n';
            std::cout << "  identity: " << result.identity << '\n';
        } else if (verbose) {
            std::cout << result.port << '\n';
            std::cout << "  error: " << result.error << '\n';
        }
    }

    if (successCount == 0 && !verbose) {
        std::cout << "no SCPI devices found in /dev/serial/by-id\n";
    }

    return 0;
}

scpi::ScpiDevice openRegisteredDevice(const std::string &name) {
    scpi::DeviceRegistry registry;
    const auto device = registry.getDevice(name);
    if (!device) {
        throw std::invalid_argument("device not registered: " + name);
    }
    return scpi::ScpiDevice(*device);
}

int dvmCommand(const std::vector<std::string> &args) {
    if (args.size() < 4) {
        printUsage();
        return 2;
    }

    const std::string deviceName = args[2];
    const std::string command = args[3];
    scpi::ScpiDevice device = openRegisteredDevice(deviceName);

    if (command == "configure") {
        if (args.size() < 5) {
            printUsage();
            return 2;
        }

        const scpi::DvmFunction function = parseDvmFunction(args[4]);
        const auto range = parseRangeOption(args, 5);
        if (range) {
            device.configureDvm(function, *range);
        } else {
            device.configureDvm(function);
        }
        device.setDvmDisplay(scpi::DvmDisplay::Main, function);
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
            device.disableSecondaryDvmDisplay();
        } else {
            device.setDvmDisplay(display, parseDvmFunction(args[5]));
        }
        std::cout << "display updated\n";
        return 0;
    }

    if (command == "capture") {
        if (args.size() < 5) {
            printUsage();
            return 2;
        }

        const scpi::DvmFunction function = parseDvmFunction(args[4]);
        const auto range = parseRangeOption(args, 5);
        if (range) {
            device.configureDvm(function, *range);
        } else {
            device.configureDvm(function);
        }
        device.setDvmDisplay(scpi::DvmDisplay::Main, function);
        std::cout << device.measureMainDisplay() << '\n';
        return 0;
    }

    if (command == "read") {
        const std::string display = args.size() >= 5 ? lowerAscii(args[4]) : "main";
        if (args.size() > 5) {
            printUsage();
            return 2;
        }

        if (display == "main" || display == "1") {
            std::cout << device.measureMainDisplay() << '\n';
        } else if (display == "secondary" || display == "sub" || display == "2") {
            std::cout << device.measureSecondaryDisplay() << '\n';
        } else if (display == "both" || display == "all") {
            std::cout << device.measureDisplays() << '\n';
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
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    args.emplace_back(argv[0]);

    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        if (arg == "-v" || arg == "--verbose") {
            verbose = true;
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

        if (command == "--version" || command == "version") {
            return printVersion();
        }
        if (command == "add") {
            return addDevice(args);
        }
        if (command == "rm") {
            return removeDevice(args);
        }
        if (command == "info") {
            return infoDevice(args);
        }
        if (command == "list") {
            return listIdentities();
        }
        if (command == "devices") {
            return devicesCommand();
        }
        if (command == "scan") {
            return scanDevices(verbose);
        }
        if (command == "dvm") {
            return dvmCommand(args);
        }

        printUsage();
        return 2;
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
