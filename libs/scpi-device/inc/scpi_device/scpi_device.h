#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace scpi {

struct IdentityResult {
    std::string port;
    int baudRate = 0;
    bool success = false;
    std::string identity;
    std::string error;
};

struct ScpiIdentity {
    std::string manufacturer;
    std::string model;
    std::string softwareVersion;
    std::string firmwareVersion;
    std::vector<std::string> fields;
};

enum class DvmFunction {
    DcVoltage,
    AcVoltage,
    DcCurrent,
    AcCurrent,
    Resistance,
    FourWireResistance,
    Frequency,
    Period,
    Capacitance,
    Continuity,
    Diode,
};

enum class DvmDisplay {
    Main,
    Secondary,
};

struct SerialOptions {
    int baudRate = 9600;
    std::chrono::milliseconds readTimeout{1000};
    std::chrono::milliseconds writeTimeout{1000};
    std::string lineEnding = "\n";
};

struct RegisteredDevice {
    std::string name;
    std::string port;
    SerialOptions options;
};

class DeviceRegistry {
public:
    DeviceRegistry();
    explicit DeviceRegistry(std::filesystem::path storagePath);

    const std::filesystem::path &storagePath() const;

    std::vector<RegisteredDevice> devices() const;
    std::optional<RegisteredDevice> getDevice(const std::string &name) const;
    std::string getPort(const std::string &name) const;
    SerialOptions getOptions(const std::string &name) const;

    void setDevice(
        const std::string &name,
        const std::string &port,
        const SerialOptions &options = {});
    bool removeDevice(const std::string &name);

    void load();
    void save() const;

    static std::filesystem::path defaultStoragePath();

private:
    std::filesystem::path storagePath_;
    std::vector<RegisteredDevice> devices_;
};

class ScpiDevice {
public:
    ScpiDevice();
    explicit ScpiDevice(std::string port, SerialOptions options = {});
    explicit ScpiDevice(const RegisteredDevice &device);
    ~ScpiDevice();

    ScpiDevice(const ScpiDevice &) = delete;
    ScpiDevice &operator=(const ScpiDevice &) = delete;

    ScpiDevice(ScpiDevice &&other) noexcept;
    ScpiDevice &operator=(ScpiDevice &&other) noexcept;

    void open();
    void open(const std::string &port, SerialOptions options = {});
    void close();

    bool isOpen() const;
    const std::string &port() const;
    const SerialOptions &options() const;

    void writeCommand(const std::string &command);
    std::string query(const std::string &command);

    std::string identity();
    void reset();
    void clearStatus();

    void configureDvm(DvmFunction function);
    void configureDvm(DvmFunction function, const std::string &range);
    void configureDvm(DvmFunction function, double range, double resolution);
    void setDvmDisplay(DvmDisplay display, DvmFunction function);
    void disableSecondaryDvmDisplay();
    std::string read();
    std::string measure(DvmFunction function);
    std::string measureDisplays();
    std::string measureMainDisplay();
    std::string measureSecondaryDisplay();
    void initiate();
    std::string fetch();

    static std::vector<std::string> listSerialByIdDevices(
        const std::string &directory = "/dev/serial/by-id");

    static std::vector<IdentityResult> identifySerialByIdDevices(
        const SerialOptions &options = {},
        const std::string &directory = "/dev/serial/by-id");

private:
    int fd_ = -1;
    std::string port_;
    SerialOptions options_;
    bool firstCommandAfterOpen_ = false;

    void configureSerialPort();
    std::string readResponse();
};

std::string toScpiFunction(DvmFunction function);
std::string toScpiDisplayFunction(DvmFunction function);
std::optional<ScpiIdentity> parseScpiIdentity(const std::string &identity);
bool isValidScpiIdentity(const std::string &identity);
const char *version();

} // namespace scpi
