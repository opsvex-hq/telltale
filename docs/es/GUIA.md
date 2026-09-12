# Guía completa (español)

> **El `node_exporter` que no existe para microcontroladores.** Heap libre y fragmentación, cuenta de reinicios *con su causa*, calidad de enlace, uptime y holgura de stack por tarea — exportado desde un ESP32 en el formato que Prometheus y OpenTelemetry ya entienden.

Mantenido por [Opsvex](https://opsvex.com). El README está en inglés porque es el idioma del ecosistema; esta guía es la versión larga para quien lo va a operar.

---

## 1. El hueco que llena

Un servidor tiene `node_exporter`. Un contenedor tiene cAdvisor. Un microcontrolador en terreno tiene un puerto serial que nadie está mirando.

Cuando un gateway en una subestación se reinicia a las 03:40, tres preguntas deciden qué hacer, y ninguna se responde mirando el uptime:

- **¿Por qué se reinició?** Un panic es un bug de firmware. Un brownout es batería o regulador. Un power-on es el diferencial de alguien. El dispositivo lo sabe, y lo olvida al reiniciar si nadie lo registró.
- **¿Se está quedando sin memoria?** No "cuánta hay libre", sino cuánta hay libre **contigua**. Un equipo con 100 kB libres y un bloque máximo de 4 kB va a fallar la próxima asignación mientras todos los tableros lo muestran sano.
- **¿Escucha la red?** Un RSSI de -89 dBm con cuarenta reconexiones por hora es un equipo técnicamente en línea y prácticamente inútil.

### Qué existe hoy

Buscar `esp32 prometheus` y `opentelemetry embedded` devuelve proyectos de un solo autor, el mayor con 5 estrellas y sin tocar desde 2022. El propio `esp-insights` de Espressif tiene 147 estrellas contra 18.951 de `esp-idf`, y además es un agente de proveedor, no un formato abierto. **OpenTelemetry no tiene convenciones semánticas para dispositivos restringidos**: ni heap, ni causa de reinicio, ni stacks, ni calidad de enlace. Este repositorio incluye [una propuesta](../../semconv/iot-device.yaml) con [la evidencia que la respalda](../../semconv/RATIONALE.md).

## 2. Instalación

Con el Component Manager de ESP-IDF, en el `idf_component.yml` de su proyecto:

```yaml
dependencies:
  opsvex/telltale:
    version: "^0.1.0"
```

O como submódulo git en `components/`. Requiere ESP-IDF 5.0 o superior.

## 3. Uso mínimo

```c
#include "telltale.h"

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());   /* telltale guarda el contador de arranques */

    telltale_config_t config = TELLTALE_DEFAULT_CONFIG();
    config.service_name = "fleet-gateway";
    config.enable_http_endpoint = true;   /* GET /metrics */
    config.http_port = 8080;
    config.otlp_endpoint = "http://collector.lan:4318/v1/metrics";

    telltale_handle_t telltale;
    ESP_ERROR_CHECK(telltale_init(&config, &telltale));
    ESP_ERROR_CHECK(telltale_watch_task(telltale, NULL, "app_main"));
}
```

Esa es toda la integración. Una tarea de fondo muestrea en su intervalo, sirve el scrape y empuja a OTLP.

## 4. Las métricas y para qué sirve cada una

Referencia completa en [METRICS.md](../METRICS.md). Lo esencial:

### Memoria

`device.heap.free`, `device.heap.total`, `device.heap.min_free`, `device.heap.largest_free_block`, `device.heap.fragmentation`.

**Por qué cinco y no una.** El heap libre por sí solo no predice la falla. El par que sí lo hace es `free` contra `largest_free_block`: un equipo con 100 kB libres y bloque máximo de 4 kB va a fallar la próxima asignación de 8 kB mientras el tablero muestra margen de sobra. `fragmentation` es esa comparación precalculada, para que una alerta no tenga que hacer aritmética:

```promql
device_heap_fragmentation_ratio > 0.8
```

**`min_free` existe porque el muestreo se pierde los valles.** Un intervalo de 30 segundos no va a capturar la ráfaga que casi agotó el heap; el RTOS lleva el mínimo de forma continua, así que exportarlo convierte una métrica muestreada en una garantizada.

### Ciclo de vida

`device.uptime`, `device.boot.count`, `device.reset` (con etiqueta `reason`), `device.reset.fault`.

El contador de arranques **se persiste en NVS** y sobrevive al reinicio que está describiendo: eso es lo que hace visible un ciclo de reinicios en el backend y no solo en la consola serial.

La causa va como **etiqueta sobre un 1 constante** porque ningún backend de métricas acepta un string como valor. Valores: `power_on`, `external`, `software`, `panic`, `interrupt_watchdog`, `task_watchdog`, `other_watchdog`, `deep_sleep`, `brownout`, `unknown`.

Esa categoría es todo el diagnóstico: una flota que reinicia con `power_on` tiene problema de alimentación, una con `panic` tiene problema de firmware, una con `brownout` tiene problema de batería o regulador. Son tres equipos distintos, y el uptime no los distingue.

### Tareas

`device.task.stack.free`, etiquetada por tarea, solo para las registradas con `telltale_watch_task()`.

El valor es la **marca de agua** de FreeRTOS: el menor stack libre que esa tarea ha tenido nunca. El stack libre actual de una tarea dormida no dice nada; el mínimo es lo único que anticipa un desbordamiento. Y un desbordamiento de stack en ESP32 suele corromper al vecino en vez de fallar limpio, que es lo que lo hace carísimo de diagnosticar después.

### Enlace

`link.connected`, `link.rssi`, `link.channel`, `link.disconnect.count`.

**El RSSI está ausente cuando no hay asociación, no en cero.** 0 dBm se lee como una señal excelente; el hueco en la serie es la verdad, y `link.connected` lo dice explícitamente.

El contador de desconexiones lo alimenta **su aplicación**, llamando a `telltale_note_disconnect()` desde su propio handler de eventos. telltale no instala handlers a espaldas de nadie — ver [ADR 0003](../adr/0003-no-hidden-event-handlers.md).

### Energía

`device.battery.voltage`, solo si su aplicación entrega un callback `battery_provider`.

**Volts, no porcentaje.** La conversión de voltaje a carga restante depende de la química de la celda, la temperatura y la carga. Un equipo que reporta porcentaje ya botó ese contexto, y los porcentajes de dos fabricantes no son comparables. Convierta en el tablero, donde puede vivir la curva de *su* celda.

## 5. Decisiones con las que puede no estar de acuerdo

- **La causa de reinicio es etiqueta, no número.** Un enum entero empujaría la decodificación a cada tablero.
- **La batería es un callback suyo, no una lectura de ADC.** El divisor, el canal y la calibración son decisiones de su placa. Adivinar mal no produce una métrica faltante: produce un número plausible y equivocado.
- **telltale no instala handler de Wi-Fi.** Una aplicación que ya tiene uno no debería recibir un segundo, invisible.
- **Nada se asigna después de `telltale_init`.** Un buffer de render, un registro de capacidad fija, cero malloc en el colector, el handler HTTP o el push.
- **Un render truncado es un error explícito**, no un cuerpo más corto. Un scrape truncado es silenciosamente incorrecto, y un tablero lo grafica igual de contento.

## 6. Configuración

`idf.py menuconfig` -> *telltale — device health metrics*:

| Opción | Default | Qué hace |
|---|---|---|
| `TELLTALE_MAX_WATCHED_TASKS` | 4 | Tareas con holgura de stack reportada |
| `TELLTALE_TASK_STACK_SIZE` | 3072 | Stack de la tarea colectora, en bytes |
| `TELLTALE_ENABLE_WIFI_COLLECTOR` | y | RSSI, canal, asociación |
| `TELLTALE_ENABLE_HTTP_ENDPOINT` | y | GET /metrics |
| `TELLTALE_ENABLE_OTLP` | y | Push a un colector OTLP/HTTP |

Desactivar un colector elimina sus métricas **y su dependencia**.

## 7. Alertas para el primer día

```promql
# Ciclo de reinicios
increase(device_boot_count_total[1h]) > 3

# Se reinició por falla, no por actualización
device_reset_fault == 1 and increase(device_boot_count_total[15m]) > 0

# Ya no puede asignar bloques grandes
device_heap_fragmentation_ratio > 0.8

# Stack a punto de desbordar
device_task_stack_free_bytes < 512

# Dejó de reportar (la que todos olvidan)
time() - timestamp(device_uptime_seconds) > 600
```

La última es la importante: un equipo que deja de reportar no produce métrica alguna, así que ningún umbral sobre las otras va a dispararse por él.

## 8. Estado de validación — léalo antes de flashear una flota

| | Estado |
|---|---|
| Núcleo portable (registro, nombres, escapes, ambos serializadores) | **47 tests de host**, en gcc y clang, con sanitizers y build de 32 bits |
| Capa ESP-IDF | Compila en CI para `esp32` y `esp32c3`, en ESP-IDF 5.1 y 5.3, ambos ejemplos |
| **Hardware real** | **Todavía no.** Ninguna placa ha corrido esto. La lista de lo no verificado está en [HARDWARE-VALIDATION.md](../HARDWARE-VALIDATION.md) |
| Cifras de memoria | Objetivos de diseño, no mediciones |
| Payload OTLP | Forma verificada contra la especificación y afirmada en tests; **ningún colector ha respondido 200 todavía** |

Versión `0.1.0`. Los nombres de métricas siguen [la propuesta](../../semconv/iot-device.yaml) y **cambiarán si ella cambia upstream**: para eso sirve estar antes de 1.0.

Si usted lo flashea primero, habrá hecho algo que nosotros no. Un reporte, salga como salga, es la contribución más útil que puede hacer.

## 9. Open core

telltale es Apache-2.0 y se queda así. Lo que Opsvex vende encima es la capa de flota: aprovisionamiento masivo, orquestación de OTA por cohortes con rollback, rotación de certificados, retención agregada y alertas. La frontera está escrita en [OPEN-CORE.md](../OPEN-CORE.md) para que nadie tenga que adivinar de qué lado cae una contribución.

**Nada de lo que usted contribuya acá va a terminar detrás de un muro de pago después.** La licencia es Apache-2.0 y relicenciar requeriría el acuerdo de cada contribuyente — no hay CLA, precisamente porque no estamos juntando los derechos que ese cambio necesitaría.

## 10. Mantención

**Presupuesto: 2 a 4 horas al mes.** Los issues reciben respuesta en **5 días hábiles**. Es software de referencia de una firma de dos personas, no un producto con soporte.

Si a los seis meses no produjo una conversación real, se congela y queda como pieza de portafolio. Está escrito a propósito.

---

<div align="center">
<sub><a href="https://opsvex.com">opsvex.com</a> · <a href="mailto:hola@opsvex.com">hola@opsvex.com</a> · <a href="https://calendly.com/opsvex-hq/30min">Agendar 30 minutos</a></sub>
</div>
