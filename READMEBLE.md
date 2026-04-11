# BLE de datos crudos PPG (HRS3300) en InfiniTime

Este cambio agrega un **servicio BLE personalizado** para exponer en tiempo real las muestras crudas del sensor óptico (PPG) que usa InfiniTime (`Hrs3300`, frecuentemente referido como HRS/HALS3300).

## Qué se agregó

- Nuevo servicio BLE: `RawPpgService`.
- Nueva característica BLE de tipo **READ + NOTIFY** para streaming continuo.
- Envío de muestra cruda en cada ciclo de adquisición de `HeartRateTask`.
- Script Python de prueba con `bleak` + gráfico en vivo con `matplotlib`.

## UUIDs BLE del nuevo servicio

- **Service UUID**: `b5f90000-8456-4c84-a6b9-5f95a2f2f1f0`
- **Characteristic UUID**: `b5f90001-8456-4c84-a6b9-5f95a2f2f1f0`

## Formato del payload de la característica

Cada notificación (y cada lectura READ) entrega **8 bytes** en little-endian:

1. `sample_counter` (`uint32`, bytes 0..3)
2. `hrs_raw` (`uint16`, bytes 4..5)
3. `als_raw` (`uint16`, bytes 6..7)

> `hrs_raw` es la muestra óptica principal usada por el algoritmo de pulso.
> `als_raw` es la lectura de luz ambiente (útil para depuración/filtro por contacto/luz externa).

## Frecuencia de muestreo configurada

- El firmware quedó configurado para muestrear en **25 Hz** (`deltaTms = 40 ms`).
- Además, el `HRS Cycle Wait Time` se configura en **0 ms** para minimizar la espera entre conversiones.

## Flujo de datos

1. `HeartRateTask::HandleSensorData()` lee `ReadHrsAls()`.
2. El controlador de frecuencia cardiaca (`HeartRateController`) ahora reenvía esos datos crudos a `RawPpgService`.
3. Si el cliente BLE habilitó notificaciones, se emite una notificación inmediatamente.

## Cómo usarlo (alto nivel)

1. Compilar y flashear este firmware en tu Pinetime.
2. Encender BLE en el reloj.
3. Conectarte desde PC/móvil a la característica anterior.
4. Habilitar notificaciones.
5. Decodificar payload de 8 bytes (little-endian).

## Script de prueba en Python

Se incluye: `ppg_ble_plot.py`

### Requisitos

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install bleak matplotlib
```

### Ejecución

```bash
python ppg_ble_plot.py --name InfiniTime
```

Opcional: conectar por MAC/UUID del dispositivo (más robusto).

```bash
python ppg_ble_plot.py --address XX:XX:XX:XX:XX:XX
```

### Opciones útiles

- `--char-uuid`: por si cambias el UUID de característica.
- `--window-seconds`: ventana visible del gráfico.
- `--no-als`: oculta la curva ALS.

## Consideraciones importantes

- El stream depende de que la tarea de HR esté midiendo (normalmente cuando el modo HR está habilitado).
- Si no recibes datos:
  - revisa que BLE esté conectado,
  - que las notificaciones estén activas,
  - y que el reloj esté efectivamente capturando HR.
- El `sample_counter` permite detectar pérdida de paquetes/notificaciones.

## Compatibilidad

Este servicio es **custom** (no estándar GATT SIG), pensado para telemetría de desarrollo y experimentación de PPG crudo.
