#include "scpi_device/scpi_device.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstdint>
#include <ctime>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using Milliseconds = std::chrono::milliseconds;

volatile std::sig_atomic_t stopRequested = 0;

constexpr int defaultBaudRate = 115200;

struct Options {
    std::string port;
    scpi::SerialOptions serial;
    Milliseconds settle{500};
    Milliseconds interval{1000};
    std::optional<std::uint64_t> count;
    std::optional<Milliseconds> duration;
    int openRetries = 0;
    bool reopenOnError = false;
    bool stopOnError = false;

    Options() {
        serial.baudRate = defaultBaudRate;
    }
};

void handleSignal(int) {
    stopRequested = 1;
}

void printUsage() {
    std::cerr
        << "Usage:\n"
        << "  scpi-smoke --port <serial-port-device> [options]\n"
        << "\n"
        << "Options:\n"
        << "  --baud <rate>                  Default: " << defaultBaudRate << "\n"
        << "  --read-timeout <milliseconds>  Default: 1000\n"
        << "  --write-timeout <milliseconds> Default: 1000\n"
        << "  --line-ending <lf|crlf|cr|literal>\n"
        << "  --settle-ms <milliseconds>     Default: 500\n"
        << "  --interval-ms <milliseconds>   Default: 1000\n"
        << "  --count <queries>              Stop after this many queries\n"
        << "  --duration-ms <milliseconds>   Stop after this elapsed time\n"
        << "  --open-retries <retries>       First query retries after open. Default: 0\n"
        << "  --nonblocking-io               Use poll/nonblocking read calls\n"
        << "  --reopen-on-error              Close/open the port after a failed query\n"
        << "  --stop-on-error                Stop after the first failed query\n"
        << "  --version\n"
        << "  -h, --help\n";
}

int printVersion() {
    std::cout << "scpi-smoke " << SCPI_SMOKE_VERSION << '\n';
    std::cout << "scpi-device " << scpi::version() << '\n';
    return 0;
}

int parseIntOption(const std::string &name, const std::string &value) {
    std::size_t parsed = 0;
    const int result = std::stoi(value, &parsed);
    if (parsed != value.size()) {
        throw std::invalid_argument("invalid value for " + name + ": " + value);
    }
    return result;
}

std::uint64_t parseCountOption(const std::string &name, const std::string &value) {
    std::size_t parsed = 0;
    const unsigned long long result = std::stoull(value, &parsed);
    if (parsed != value.size()) {
        throw std::invalid_argument("invalid value for " + name + ": " + value);
    }
    return static_cast<std::uint64_t>(result);
}

Milliseconds parseMillisecondsOption(const std::string &name, const std::string &value) {
    const int milliseconds = parseIntOption(name, value);
    if (milliseconds < 0) {
        throw std::invalid_argument(name + " must be non-negative");
    }
    return Milliseconds(milliseconds);
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

std::string requireValue(
    const std::vector<std::string> &args,
    std::size_t &index,
    const std::string &option) {
    if (index + 1 >= args.size()) {
        throw std::invalid_argument("missing value for " + option);
    }
    ++index;
    return args[index];
}

Options parseArgs(int argc, char **argv) {
    Options options;
    const std::vector<std::string> args(argv + 1, argv + argc);

    for (std::size_t index = 0; index < args.size(); ++index) {
        const std::string &arg = args[index];
        if (arg == "-h" || arg == "--help") {
            printUsage();
            std::exit(0);
        }
        if (arg == "--version" || arg == "version") {
            std::exit(printVersion());
        }
        if (arg == "--port") {
            options.port = requireValue(args, index, arg);
        } else if (arg == "--baud") {
            options.serial.baudRate = parseIntOption(arg, requireValue(args, index, arg));
        } else if (arg == "--read-timeout") {
            options.serial.readTimeout = parseMillisecondsOption(arg, requireValue(args, index, arg));
        } else if (arg == "--write-timeout") {
            options.serial.writeTimeout = parseMillisecondsOption(arg, requireValue(args, index, arg));
        } else if (arg == "--line-ending") {
            options.serial.lineEnding = parseLineEnding(requireValue(args, index, arg));
        } else if (arg == "--settle-ms") {
            options.settle = parseMillisecondsOption(arg, requireValue(args, index, arg));
        } else if (arg == "--interval-ms") {
            options.interval = parseMillisecondsOption(arg, requireValue(args, index, arg));
        } else if (arg == "--count") {
            options.count = parseCountOption(arg, requireValue(args, index, arg));
        } else if (arg == "--duration-ms") {
            options.duration = parseMillisecondsOption(arg, requireValue(args, index, arg));
        } else if (arg == "--open-retries") {
            options.openRetries = parseIntOption(arg, requireValue(args, index, arg));
            if (options.openRetries < 0) {
                throw std::invalid_argument("--open-retries must be non-negative");
            }
        } else if (arg == "--nonblocking-io") {
            options.serial.blockingIo = false;
        } else if (arg == "--reopen-on-error") {
            options.reopenOnError = true;
        } else if (arg == "--stop-on-error") {
            options.stopOnError = true;
        } else {
            throw std::invalid_argument("unknown option: " + arg);
        }
    }

    if (options.port.empty()) {
        throw std::invalid_argument("--port is required");
    }
    if (!options.count && !options.duration) {
        options.count = std::numeric_limits<std::uint64_t>::max();
    }

    return options;
}

std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    const auto subsecond = std::chrono::duration_cast<Milliseconds>(now.time_since_epoch()) % 1000;

    std::tm localTime{};
    localtime_r(&seconds, &localTime);

    std::ostringstream output;
    output << std::put_time(&localTime, "%Y-%m-%dT%H:%M:%S") << '.'
           << std::setw(3) << std::setfill('0') << subsecond.count();
    return output.str();
}

void sleepUntilOrStop(Clock::time_point deadline) {
    while (!stopRequested) {
        const auto now = Clock::now();
        if (now >= deadline) {
            return;
        }
        const auto remaining = std::chrono::duration_cast<Milliseconds>(deadline - now);
        std::this_thread::sleep_for(std::min(remaining, Milliseconds(100)));
    }
}

void openDevice(scpi::ScpiDevice &device) {
    device.open();
}

void configureOpenRetries(int retries) {
    if (::setenv("SCPI_DEVICE_OPEN_RETRIES", std::to_string(retries).c_str(), 1) != 0) {
        throw std::runtime_error("failed to configure SCPI_DEVICE_OPEN_RETRIES");
    }
}

} // namespace

int main(int argc, char **argv) {
    try {
        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);

        const Options options = parseArgs(argc, argv);
        configureOpenRetries(options.openRetries);
        scpi::ScpiDevice device(options.port, options.serial);

        std::cout << "opening " << options.port << " at " << options.serial.baudRate << " baud\n";
        std::cout << "io-mode: " << (options.serial.blockingIo ? "blocking" : "poll-nonblocking") << '\n';
        std::cout << "first-query-open-retries: " << options.openRetries << '\n';
        openDevice(device);

        if (options.settle.count() > 0) {
            std::cout << "settling for " << options.settle.count() << " ms\n";
            sleepUntilOrStop(Clock::now() + options.settle);
        }

        std::uint64_t successes = 0;
        std::uint64_t failures = 0;
        const auto started = Clock::now();
        auto nextQuery = Clock::now();

        for (std::uint64_t sequence = 1; !stopRequested; ++sequence) {
            if (options.count && sequence > *options.count) {
                break;
            }
            if (options.duration && Clock::now() - started >= *options.duration) {
                break;
            }

            const auto queryStarted = Clock::now();
            try {
                const std::string identity = device.identity();
                const auto latency = std::chrono::duration_cast<Milliseconds>(Clock::now() - queryStarted);
                ++successes;
                std::cout << std::setw(8) << std::setfill('0') << sequence << ' '
                          << timestamp() << " ok " << latency.count() << "ms "
                          << identity << '\n';
            } catch (const std::exception &error) {
                const auto latency = std::chrono::duration_cast<Milliseconds>(Clock::now() - queryStarted);
                ++failures;
                std::cout << std::setw(8) << std::setfill('0') << sequence << ' '
                          << timestamp() << " error " << latency.count() << "ms "
                          << error.what() << '\n';

                if (options.reopenOnError) {
                    try {
                        device.close();
                        openDevice(device);
                    } catch (const std::exception &reopenError) {
                        std::cout << std::setw(8) << std::setfill('0') << sequence << ' '
                                  << timestamp() << " reopen-error " << reopenError.what() << '\n';
                    }
                }
                if (options.stopOnError) {
                    break;
                }
            }

            nextQuery += options.interval;
            if (nextQuery < Clock::now()) {
                nextQuery = Clock::now() + options.interval;
            }
            sleepUntilOrStop(nextQuery);
        }

        std::cout << "summary: ok=" << successes << " errors=" << failures << '\n';
        return failures == 0 ? 0 : 2;
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << '\n';
        printUsage();
        return 1;
    }
}
