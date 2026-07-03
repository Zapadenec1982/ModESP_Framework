/**
 * @file cloud_service.h
 * @brief ICloudService — спільний контракт cloud-бекендів (MQTT, AWS IoT, …).
 *
 * Бекенд обирається на етапі компіляції (Kconfig choice MODESP_CLOUD_*),
 * тож у прошивку лінкується РІВНО ОДИН. Раніше MqttService/AwsIotService були
 * duck-typed (однакові set_state/set_http_server без спільного типу), тому дрейф
 * інтерфейсу невибраного бекенда лишався непомітним. Цей інтерфейс формалізує
 * контракт: будь-який зібраний бекенд мусить його задовольнити (compile error
 * інакше), а main.cpp більше не називає конкретних класів.
 *
 * cloud_backend() ВИЗНАЧАЄТЬСЯ у компоненті, що злінкувався (modesp_mqtt /
 * modesp_aws), і повертає його singleton. main викликає її лише під
 * #if defined(CONFIG_MODESP_CLOUD_*), тож MODESP_CLOUD_NONE визначення не
 * потребує.
 */

#pragma once

#include "modesp/base_module.h"
#include "esp_http_server.h"

namespace modesp {

class SharedState;

class ICloudService : public BaseModule {
public:
    explicit ICloudService(const char* name,
                           ModulePriority priority = ModulePriority::HIGH)
        : BaseModule(name, priority) {}

    /// Джерело значень для публікації (SharedState).
    virtual void set_state(SharedState* s) = 0;

    /// HTTP-сервер для реєстрації cloud-config endpoints (/api/mqtt, /api/cloud).
    virtual void set_http_server(httpd_handle_t server) = 0;
};

/// Cloud-бекенд, вкомпільований у цю прошивку (єдиний, за Kconfig-choice).
/// Визначається у modesp_mqtt/modesp_aws; посилається лише main під
/// #if defined(CONFIG_MODESP_CLOUD_MQTT|AWS).
ICloudService* cloud_backend();

} // namespace modesp
