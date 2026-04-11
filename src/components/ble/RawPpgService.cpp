#include "components/ble/RawPpgService.h"
#include "components/ble/NimbleController.h"
#include "components/heartrate/HeartRateController.h"
#include <nrf_log.h>
#include <cstring>

using namespace Pinetime::Controllers;

namespace {
  int RawPpgServiceCallback(uint16_t /*conn_handle*/, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg) {
    auto* rawPpgService = static_cast<RawPpgService*>(arg);
    return rawPpgService->OnRawPpgRequested(attr_handle, ctxt);
  }
}

RawPpgService::RawPpgService(NimbleController& nimble, Controllers::HeartRateController& heartRateController)
  : nimble {nimble},
    heartRateController {heartRateController},
    characteristicDefinition {{.uuid = &rawPpgMeasurementUuid.u,
                               .access_cb = RawPpgServiceCallback,
                               .arg = this,
                               .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                               .val_handle = &rawPpgMeasurementHandle},
                              {0}},
    serviceDefinition {{.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &rawPpgServiceUuid.u, .characteristics = characteristicDefinition}, {0}} {
  heartRateController.SetRawPpgService(this);
}

void RawPpgService::Init() {
  int res = ble_gatts_count_cfg(serviceDefinition);
  ASSERT(res == 0);

  res = ble_gatts_add_svcs(serviceDefinition);
  ASSERT(res == 0);
}

int RawPpgService::OnRawPpgRequested(uint16_t attributeHandle, ble_gatt_access_ctxt* context) {
  if (attributeHandle == rawPpgMeasurementHandle) {
    uint8_t buffer[8];
    memcpy(&buffer[0], &sampleCounter, sizeof(sampleCounter));
    memcpy(&buffer[4], &lastHrs, sizeof(lastHrs));
    memcpy(&buffer[6], &lastAls, sizeof(lastAls));

    int res = os_mbuf_append(context->om, buffer, sizeof(buffer));
    return (res == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
  }
  return 0;
}

void RawPpgService::OnNewRawPpgValue(uint16_t hrs, uint16_t als) {
  lastHrs = hrs;
  lastAls = als;
  sampleCounter++;

  if (!rawPpgMeasurementNotificationEnabled) {
    return;
  }

  uint8_t buffer[8];
  memcpy(&buffer[0], &sampleCounter, sizeof(sampleCounter));
  memcpy(&buffer[4], &hrs, sizeof(hrs));
  memcpy(&buffer[6], &als, sizeof(als));

  auto* om = ble_hs_mbuf_from_flat(buffer, sizeof(buffer));

  uint16_t connectionHandle = nimble.connHandle();
  if (connectionHandle == 0 || connectionHandle == BLE_HS_CONN_HANDLE_NONE) {
    return;
  }

  ble_gattc_notify_custom(connectionHandle, rawPpgMeasurementHandle, om);
}

void RawPpgService::SubscribeNotification(uint16_t attributeHandle) {
  if (attributeHandle == rawPpgMeasurementHandle) {
    NRF_LOG_INFO("RAWPpg notify subscribed handle=%d", rawPpgMeasurementHandle);
    rawPpgMeasurementNotificationEnabled = true;
  }
}

void RawPpgService::UnsubscribeNotification(uint16_t attributeHandle) {
  if (attributeHandle == rawPpgMeasurementHandle) {
    rawPpgMeasurementNotificationEnabled = false;
  }
}
