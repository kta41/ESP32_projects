# ESP32-S3 Máquina de Chistes Interactiva (LCD 1602 + Botón + CGRAM UTF-8)

Aplicación interactiva construida sobre ESP32-S3 que implementa una máquina de estados para visualización de textos en formato Pregunta/Respuesta con control por pulsador. Integra un decodificador UTF-8 en tiempo real y generación de mapas de bits en la memoria CGRAM del controlador HD44780 para admitir caracteres en castellano (ñ, Ñ, ¿, ¡, tildes) y desplazamiento tipo marquesina no bloqueante.

---

## 🛠️ Arquitectura de Hardware y Conexionado

### Mapeo de Pines

| Componente | Pin Módulo | Función | Pin ESP32-S3 / Alimentación |
| :--- | :--- | :--- | :--- |
| **LCD 1602** | `Pin 1 (VSS)` | Masa lógica | `GND` |
| | `Pin 2 (VDD)` | Alimentación | `3V3` |
| | `Pin 3 (V0)` | Contraste | `GND` |
| | `Pin 4 (RS)` | Registro Select | **GPIO 5** |
| | `Pin 5 (RW)` | Modo R/W | `GND` (Escritura) |
| | `Pin 6 (E)` | Enable / Reloj | **GPIO 6** |
| | `Pin 11 (D4)`| Bus de Datos D4 | **GPIO 7** |
| | `Pin 12 (D5)`| Bus de Datos D5 | **GPIO 15** |
| | `Pin 13 (D6)`| Bus de Datos D6 | **GPIO 16** |
| | `Pin 14 (D7)`| Bus de Datos D7 | **GPIO 17** |
| | `Pin 15 (A)` | Ánodo luz de fondo | `3V3` |
| | `Pin 16 (K)` | Cátodo luz de fondo | `GND` |
| **Pulsador** | Terminal 1 | Entrada digital | **GPIO 4** (Pull-Up interno activado) |
| | Terminal 2 | Retorno | `GND` |

---

## ⚙️ Arquitectura del Firmware

### Máquina de Estados (FreeRTOS Task)
* **ESTADO_INICIO:** Presenta carátula de bienvenida ("MÁQUINA DE / CHISTES BARATOS").
* **ESTADO_PREGUNTA:** Línea 1 muestra la pregunta con scroll si excede 16 caracteres; Línea 2 permanece vacía.
* **ESTADO_RESPUESTA:** Línea 1 fija el inicio de la pregunta; Línea 2 muestra la respuesta con scroll dinámico.
* **SIGUIENTE CHISTE:** Tras ver la respuesta, una nueva pulsación avanza de chiste en bucle circular (`(i + 1) % TOTAL`).

---

## 🧠 Características Técnicas Destacadas

### 1. Renderizado de Castellano vía CGRAM
El controlador HD44780 carece de caracteres en español en su tabla ROM estándar. El firmware inyecta mapas de bits personalizados de 5x8 píxeles en la memoria **CGRAM** durante el arranque:
* **Slot `0x00`:** `ñ` minúscula
* **Slot `0x01`:** `Ñ` mayúscula
* **Slot `0x02`:** `¿` apertura de interrogación
* **Slot `0x03`:** `¡` apertura de exclamación

### 2. Decodificación UTF-8 en Vuelo
Los literales de cadena en C generan secuencias multibyte (2 bytes) para caracteres especiales (ej: `ñ` -> `0xC3 0xB1`, `¿` -> `0xC2 0xBF`).
* `lcd_print()` intercepta los bytes de control `0xC2` y `0xC3`, enviando al display el slot CGRAM correspondiente o mapeando vocales con tilde a caracteres ASCII legibles.
* `utf8_visual_len()` y `utf8_visual_substr()` calculan la longitud visual real y extraen subcadenas seguras, impidiendo que el motor de marquesina corte caracteres multibyte por la mitad.

### 3. Marquesina Preemptible (Interrupción Inmediata)
Durante el retardo de cada paso de scroll (280 ms), la tarea de visualización sondea la cola de eventos `btn_queue`. Si el usuario presiona el botón a mitad de animación, la marquesina se interrumpe de inmediato para avanzar de estado sin latencia.

---

## 🚀 Configuración y Compilación (ESP-IDF)

### 1. `main/CMakeLists.txt`
    idf_component_register(SRCS "main.c"
                           INCLUDE_DIRS "."
                           REQUIRES driver esp_driver_gpio)

### 2. Compilación y Flasheo
    idf.py build flash monitor
