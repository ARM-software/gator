/* Copyright (C) 2025 by Arm Limited (or its affiliates). All rights reserved. */

#include "GpuTimelineLayerRunner.h"

#include "Logging.h"
#include "lib/FsEntry.h"
#include "lib/Popen.h"
#include "lib/Process.h"

#include <array>
#include <cerrno>
#include <optional>
#include <string>
#include <string_view>

#include <unistd.h>

namespace {
    constexpr std::string_view LAYER_DRIVER_LIB {"libVkLayerGPUTimeline.so"};
    constexpr std::string_view LAYER_DRIVER_NAME {"VK_LAYER_LGL_gpu_timeline"};
    constexpr std::size_t CMD_BUF_SIZE = 1024;

    /**
     * @brief  Runs a shell command and captures its output.
     *
     * @param command The command to run.
     * @param output Reference to a string where the command output will be stored.
     * @return true if the command was executed successfully and output was captured.
     * @return false if there was an error executing the command or capturing the output.
     */
    bool runCommandAndGetOutput(const std::string & command, std::string & output)
    {
        std::array<char, CMD_BUF_SIZE> buffer;
        output.clear();
        LOG_DEBUG("Running command: %s", command.c_str());

        auto result = lib::popen("sh", "-c", command.c_str());
        if (result.pid < 0) {
            LOG_ERROR("lib::popen failed for command '%s' (errno = %d)", command.c_str(), -result.pid);
            return false;
        }

        while (true) {
            ssize_t bytesRead = ::read(result.out, buffer.data(), buffer.size());
            if (bytesRead < 0) {
                if (errno == EINTR) {
                    continue;
                }
                LOG_ERROR("read failed for command '%s' (errno = %d)", command.c_str(), errno);
                lib::pclose(result);
                return false;
            }
            if (bytesRead == 0) {
                break;
            }
            output.append(buffer.data(), static_cast<size_t>(bytesRead));
        }

        // Remove trailing newline
        if (!output.empty() && output.back() == '\n') {
            output.pop_back();
        }

        LOG_DEBUG("Command output: %s", output.c_str());
        if (lib::pclose(result) < 0) {
            LOG_ERROR("lib::pclose failed for command '%s' (errno = %d)", command.c_str(), errno);
        }
        return true;
    }

    /**
     * @brief Sets a global Android setting.
     *
     * @param setting The name of the setting to set.
     * @param value The value to set the setting to.
     * @note This function uses the Android `settings` command to modify global settings.
    */
    void set_android_settings(const std::string & setting, const std::string & value)
    {
        auto cmd_result = gator::process::system("settings put global " + setting + " " + value);
        if (cmd_result != 0) {
            LOG_WARNING("Failed to set Android setting %s to %s", setting.c_str(), value.c_str());
        }
        LOG_DEBUG("Set Android setting %s to %s", setting.c_str(), value.c_str());
    }

    /**
     * @brief Gets a global Android setting.
     *
     * @param setting The name of the setting to retrieve.
     * @return std::optional<std::string> The value of the setting, or std::nullopt if the command failed.
     * @note This function uses the Android `settings` command to retrieve global settings.
     */
    std::optional<std::string> get_android_settings(const std::string & setting)
    {
        std::string cmd = "settings get global " + setting;
        std::string cmd_output;

        if (!runCommandAndGetOutput(cmd, cmd_output)) {
            LOG_WARNING("Failed to get Android setting %s", setting.c_str());
            return std::nullopt;
        }
        LOG_DEBUG("Got Android setting %s: %s", setting.c_str(), cmd_output.c_str());
        return cmd_output;
    }
}

namespace gator::android::timeline_layer {
    void deploy_to_package(const std::string & package)
    {
        const auto layer_driver_path = "/data/local/tmp/" + std::string(LAYER_DRIVER_LIB);
        const auto data_tmp = lib::FsEntry::create(layer_driver_path);
        if (!data_tmp.exists()) {
            LOG_WARNING("Couldn't find %s file", layer_driver_path.c_str());
            return;
        }
        const auto target_so_path = "/data/data/" + package + "/" + std::string(LAYER_DRIVER_LIB);
        const auto cmd = "run-as " + package + " cp -f " + data_tmp.path() + " " + target_so_path;
        const auto cmd_result = gator::process::system(cmd);

        if (cmd_result != 0) {
            LOG_WARNING("Failed to copy layer driver to %s. GPU timeline will not be activated",
                        target_so_path.c_str());
            return;
        }
        LOG_DEBUG("Layer driver copied to %s", target_so_path.c_str());

        std::string gpu_debug_layers_value = std::string(LAYER_DRIVER_NAME);
        auto existing_gpu_debug_layers = get_android_settings("gpu_debug_layers");

        // When a setting is not set in Android, the command `settings get global <setting>` returns an empty string or "null".
        if (existing_gpu_debug_layers
            && (*existing_gpu_debug_layers != "null" && !existing_gpu_debug_layers->empty())) {

            // If gpu_debug_layers doesn't contain the layer, Append the new layer to the existing layers
            if (existing_gpu_debug_layers->find(gpu_debug_layers_value) == std::string::npos) {
                LOG_DEBUG("gpu_debug_layers Android setting contains %s for package %s, %s is going to be "
                          "appended to the existing list",
                          existing_gpu_debug_layers->c_str(),
                          package.c_str(),
                          gpu_debug_layers_value.c_str());
                gpu_debug_layers_value = *existing_gpu_debug_layers + ":" + gpu_debug_layers_value;
            }
            else {
                LOG_DEBUG("gpu_debug_layers already contains %s for package %s, not appending again",
                          gpu_debug_layers_value.c_str(),
                          package.c_str());
                gpu_debug_layers_value = *existing_gpu_debug_layers;
            }
        }
        else {
            LOG_DEBUG("gpu_debug_layers Android setting is not set for package %s, setting it to %s",
                      package.c_str(),
                      gpu_debug_layers_value.c_str());
        }

        // Set the GPU debug layer settings
        set_android_settings("enable_gpu_debug_layers", "1");
        set_android_settings("gpu_debug_app", package);
        set_android_settings("gpu_debug_layer_app", package);
        set_android_settings("gpu_debug_layers", gpu_debug_layers_value);
    }
}
