# READMEBLE — Servicio BLE de datos crudos PPG (HRS3300) en InfiniTime

Este documento describe **en detalle** la implementación del servicio BLE custom que expone datos crudos del sensor óptico HRS3300 (PPG/ALS), su arquitectura interna, formato de datos, comportamiento temporal y pasos de uso/diagnóstico.

---

## 1) Objetivo funcional

El objetivo de esta integración es publicar por BLE, de forma continua y en tiempo real:

- `hrs_raw`: señal óptica cruda usada por el pipeline de frecuencia cardiaca.
- `als_raw`: señal de luz ambiente (útil para depuración de contacto, luz externa y calidad de señal).

La exposición se realiza mediante un servicio GATT custom con característica `READ | NOTIFY`.

---

## 2) Alcance de la implementación (firmware)

### Componentes incorporados o modificados

1. **Nuevo servicio BLE**
   - `src/components/ble/RawPpgService.h`
   - `src/components/ble/RawPpgService.cpp`

2. **Integración con stack BLE central (NimBLE)**
   - `src/components/ble/NimbleController.h`
   - `src/components/ble/NimbleController.cpp`

3. **Puente de datos desde tarea HR al servicio BLE**
   - `src/components/heartrate/HeartRateController.h`
   - `src/components/heartrate/HeartRateController.cpp`
   - `src/heartratetask/HeartRateTask.cpp`

4. **Configuración temporal del pipeline PPG**
   - `src/components/heartrate/Ppg.h`

5. **Configuración de timing del sensor HRS3300**
   - `src/drivers/Hrs3300.cpp`

6. **Build system (inclusión del nuevo servicio)**
   - `src/CMakeLists.txt`

---

## 3) Especificación BLE del servicio Raw PPG

## 3.1 UUIDs

- **Service UUID (128-bit)**: `b5f90000-8456-4c84-a6b9-5f95a2f2f1f0`
- **Characteristic UUID (128-bit)**: `b5f90001-8456-4c84-a6b9-5f95a2f2f1f0`

## 3.2 Propiedades de característica

La característica se registró con:

- `BLE_GATT_CHR_F_READ`
- `BLE_GATT_CHR_F_NOTIFY`

Implicaciones:

- **READ**: permite leer una muestra instantánea (último valor almacenado).
- **NOTIFY**: permite streaming continuo cuando el cliente habilita CCCD.

## 3.3 Servicio custom (no SIG estándar)

Este servicio **no** reemplaza el Heart Rate Service estándar (`0x180D`) de BLE. Es un canal adicional de telemetría técnica para datos crudos.

---

## 4) Formato binario exacto del payload

Cada lectura/notificación envía **8 bytes** en **little-endian**:

1. `sample_counter` → `uint32` (bytes `0..3`)
2. `hrs_raw` → `uint16` (bytes `4..5`)
3. `als_raw` → `uint16` (bytes `6..7`)

### 4.1 Estructura lógica

```text
Offset  Tamaño  Tipo     Campo
0       4       uint32   sample_counter
4       2       uint16   hrs_raw
6       2       uint16   als_raw
```

### 4.2 Ejemplo de decodificación

```text
payload = 08 bytes
counter, hrs, als = unpack('<IHH', payload)
```

### 4.3 Semántica de `sample_counter`

- Incrementa en cada muestra publicada internamente al servicio.
- Permite detectar pérdida/retraso de notificaciones en el receptor.
- Si se observan saltos (`n -> n+2`, `n+3`, etc.), hubo pérdida de paquetes o backlog en host/transporte.

---

## 5) Flujo interno de datos (end-to-end)

## 5.1 Captura sensor

`HeartRateTask::HandleSensorData()` ejecuta una lectura:

- `sensorData = heartRateSensor.ReadHrsAls()`

## 5.2 Bridge hacia servicio BLE

Tras leer, la tarea envía los datos al controlador:

- `HeartRateController::UpdateRawValues(sensorData.hrs, sensorData.als)`

El controlador, si tiene servicio asociado (`SetRawPpgService`), delega en:

- `RawPpgService::OnNewRawPpgValue(hrs, als)`

## 5.3 Publicación BLE

Dentro de `OnNewRawPpgValue(...)`:

1. actualiza `lastHrs`, `lastAls`
2. incrementa `sampleCounter`
3. verifica si hay notificaciones habilitadas
4. empaqueta 8 bytes
5. envía `ble_gattc_notify_custom(connHandle, rawPpgMeasurementHandle, om)`

## 5.4 Lectura puntual (READ)

Cuando un cliente hace `READ`, `OnRawPpgRequested(...)` responde con:

- el último `sampleCounter`
- el último `lastHrs`
- el último `lastAls`

---

## 6) Gestión de suscripción (CCCD) y ciclo BLE

La suscripción se maneja en `NimbleController::OnGAPEvent()` con `BLE_GAP_EVENT_SUBSCRIBE`:

- alta de notify (`prev_notify=0, cur_notify=1`) -> `SubscribeNotification(handle)`
- baja de notify (`prev_notify=1, cur_notify=0`) -> `UnsubscribeNotification(handle)`
- terminación -> desuscribe

`RawPpgService` mantiene estado con un flag atómico:

- `rawPpgMeasurementNotificationEnabled`

Así se evita construir/enviar notificaciones cuando el cliente no está suscrito.

---

## 7) Temporización y frecuencia de muestreo

## 7.1 Frecuencia objetivo firmware

El pipeline PPG se configuró a **25 Hz**:

- `deltaTms = 40` ms (en `Ppg.h`)
- periodo nominal de muestra: 40 ms

## 7.2 Configuración del HRS3300

En `Hrs3300::Init()` se configuró `HWT = 0 ms` para minimizar espera entre ciclos ADC.

Contexto técnico:

- conversión ADC típica HRS ≈ 25 ms (según referencia de datasheet indicada)
- con 0 ms de espera adicional y loop de 40 ms, el sistema opera alineado a 25 Hz

## 7.3 Consideraciones de estabilidad real

La tasa efectiva observada puede variar por:

- scheduling FreeRTOS
- latencia de conexión BLE
- intervalos de conexión y MTU
- carga del host receptor

Por eso es importante validar continuidad con `sample_counter`.

---

## 8) Pasos de uso (cliente genérico BLE, sin script específico)

1. Flashear firmware con este servicio.
2. Encender BLE del reloj.
3. Conectar como central BLE.
4. Descubrir servicio `b5f90000-8456-4c84-a6b9-5f95a2f2f1f0`.
5. Descubrir característica `b5f90001-8456-4c84-a6b9-5f95a2f2f1f0`.
6. Habilitar CCCD para notificaciones.
7. Parsear payload como `<IHH` little-endian.

---

## 9) Validaciones recomendadas

## 9.1 Validación funcional mínima

- [ ] El servicio aparece en discovery GATT.
- [ ] La característica permite READ sin error ATT.
- [ ] Al habilitar notify, llegan tramas periódicas.
- [ ] `sample_counter` incrementa monótonamente.

## 9.2 Validación temporal

- [ ] Medir `delta_t` entre muestras en host.
- [ ] Promedio cercano a 40 ms (≈25 Hz).
- [ ] Revisar jitter y ráfagas.

## 9.3 Validación de pérdida

- [ ] Contador sin saltos en condiciones nominales.
- [ ] Cuantificar saltos cuando se degrada enlace (distancia/interferencias).

---

## 10) Troubleshooting detallado

## 10.1 No aparece el servicio

Posibles causas:

- firmware viejo/flasheo incompleto
- servicio no agregado al build
- BLE no inicializado correctamente

Chequeos:

- confirmar que `RawPpgService::Init()` se ejecuta desde `NimbleController::Init()`
- confirmar que `RawPpgService.cpp/.h` están en `src/CMakeLists.txt`

## 10.2 Se conecta pero no notifica

Posibles causas:

- CCCD no habilitado por el cliente
- suscripción sobre handle incorrecto
- stream bloqueado por estado de medición HR

Chequeos:

- verificar evento `BLE_GAP_EVENT_SUBSCRIBE`
- confirmar que `rawPpgMeasurementNotificationEnabled == true`
- validar que `HeartRateTask::HandleSensorData()` esté corriendo

## 10.3 Llegan datos pero con saltos de contador

Posibles causas:

- intervalos BLE no óptimos
- host lento procesando notificaciones
- interferencia RF / distancia / coexistencia 2.4GHz

Mitigaciones:

- mejorar condiciones de enlace
- reducir carga de procesamiento del host
- ajustar parámetros de conexión BLE en pruebas avanzadas

## 10.4 Valores crudos inestables o saturados

Posibles causas:

- mal contacto óptico
- luz ambiente intensa
- movimiento excesivo

Notas:

- `als_raw` ayuda a correlacionar degradación de `hrs_raw`
- esto es telemetría cruda, no señal filtrada final

---

## 11) Diseño y decisiones

1. **Se usó servicio custom 128-bit**
   - evita sobrecargar semántica del servicio estándar de HR.

2. **Se publicó muestra cruda en cada ciclo de adquisición**
   - prioriza latencia baja y observabilidad.

3. **Se añadió `sample_counter`**
   - imprescindible para diagnóstico de calidad de transporte BLE.

4. **Se mantuvo `READ` además de `NOTIFY`**
   - útil para pruebas rápidas de integración sin stream continuo.

---

## 12) Compatibilidad y expectativas

- Compatible con clientes BLE genéricos que soporten notificaciones GATT.
- Este canal está orientado a ingeniería/telemetría, no a UX final.
- La estabilidad final depende de firmware + radio + host + entorno RF.

---

## 13) Resumen ejecutivo

- Se añadió un servicio BLE custom para PPG crudo.
- El payload es fijo (8 bytes, `<IHH`).
- El flujo está conectado desde la tarea de HR hasta notificación BLE.
- La configuración temporal está alineada a 25 Hz con `deltaTms=40 ms` y `HWT=0 ms`.
- `sample_counter` permite auditar pérdidas y continuidad del stream.
