#include "scpi_device/scpi_device.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#include <fcntl.h>
#include <pty.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

using namespace std::chrono_literals;

class PtyPair {
public:
    PtyPair() {
        char slaveName[256]{};
        if (::openpty(&masterFd_, &slaveFd_, slaveName, nullptr, nullptr) != 0) {
            throw std::runtime_error("openpty failed");
        }
        slavePath_ = slaveName;
    }

    ~PtyPair() {
        if (masterFd_ >= 0) {
            ::close(masterFd_);
        }
        if (slaveFd_ >= 0) {
            ::close(slaveFd_);
        }
    }

    PtyPair(const PtyPair &) = delete;
    PtyPair &operator=(const PtyPair &) = delete;

    int releaseSlave() {
        const int fd = slaveFd_;
        slaveFd_ = -1;
        return fd;
    }

    int masterFd() const {
        return masterFd_;
    }

    const std::string &slavePath() const {
        return slavePath_;
    }

private:
    int masterFd_ = -1;
    int slaveFd_ = -1;
    std::string slavePath_;
};

std::string readUntilNewline(int fd) {
    std::string command;
    char ch = '\0';
    while (true) {
        const ssize_t count = ::read(fd, &ch, 1);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EIO) {
                std::this_thread::sleep_for(10ms);
                continue;
            }
            throw std::runtime_error("read failed");
        }
        if (count == 0) {
            throw std::runtime_error("unexpected EOF");
        }
        command.push_back(ch);
        if (ch == '\n') {
            return command;
        }
    }
}

void require(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testIdentityQueryRoundTrip() {
    PtyPair pty;
    const int heldSlave = pty.releaseSlave();
    ::close(heldSlave);

    std::thread deviceThread([masterFd = pty.masterFd()]() {
        const std::string command = readUntilNewline(masterFd);
        require(command == "*IDN?\n", "unexpected command: " + command);

        const std::string response = "OWON,XDM1041,24152470,V4.3.0,3\n";
        const ssize_t written = ::write(masterFd, response.data(), response.size());
        require(written == static_cast<ssize_t>(response.size()), "short write");
    });

    scpi::SerialOptions options;
    options.baudRate = 115200;
    options.readTimeout = 500ms;
    options.writeTimeout = 500ms;

    scpi::ScpiDevice device(pty.slavePath(), options);
    device.open();
    const std::string identity = device.identity();

    deviceThread.join();
    require(identity == "OWON,XDM1041,24152470,V4.3.0,3", "unexpected identity: " + identity);
}

void testOpenSettlesBeforeFirstWrite() {
    PtyPair pty;
    const int heldSlave = pty.releaseSlave();
    ::close(heldSlave);

    std::atomic<long long> firstByteDelayMs{-1};
    const auto started = std::chrono::steady_clock::now();

    std::thread deviceThread([masterFd = pty.masterFd(), &firstByteDelayMs, started]() {
        const std::string command = readUntilNewline(masterFd);
        require(command == "*IDN?\n", "unexpected command: " + command);

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        firstByteDelayMs.store(elapsed.count());
    });

    scpi::SerialOptions options;
    options.baudRate = 115200;
    options.readTimeout = 500ms;
    options.writeTimeout = 500ms;

    scpi::ScpiDevice device(pty.slavePath(), options);
    device.open();
    device.writeCommand("*IDN?");

    deviceThread.join();
    require(firstByteDelayMs.load() >= 200, "first command was sent before settle delay elapsed");
}

void testFirstQueryTimeoutIsRetriedAfterOpen() {
    PtyPair pty;
    const int heldSlave = pty.releaseSlave();
    ::close(heldSlave);

    std::thread deviceThread([masterFd = pty.masterFd()]() {
        const std::string firstCommand = readUntilNewline(masterFd);
        require(firstCommand == "*IDN?\n", "unexpected first command: " + firstCommand);

        const std::string secondCommand = readUntilNewline(masterFd);
        require(secondCommand == "*IDN?\n", "unexpected second command: " + secondCommand);

        const std::string response = "OWON,XDM1041,24152470,V4.3.0,3\n";
        const ssize_t written = ::write(masterFd, response.data(), response.size());
        require(written == static_cast<ssize_t>(response.size()), "short write");
    });

    scpi::SerialOptions options;
    options.baudRate = 115200;
    options.readTimeout = 200ms;
    options.writeTimeout = 500ms;

    scpi::ScpiDevice device(pty.slavePath(), options);
    device.open();
    const std::string identity = device.identity();

    deviceThread.join();
    require(identity == "OWON,XDM1041,24152470,V4.3.0,3", "unexpected identity after retry");
}

} // namespace

int main() {
    try {
        testIdentityQueryRoundTrip();
        testOpenSettlesBeforeFirstWrite();
        testFirstQueryTimeoutIsRetriedAfterOpen();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "test failure: " << error.what() << '\n';
        return 1;
    }
}
