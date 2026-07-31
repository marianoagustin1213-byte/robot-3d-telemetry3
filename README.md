# robot-3d-telemetry3
# ESP32 5-Axis Servo Arm with 3D Web Visualizer

Controlador de brazo robótico de 5 servomotores con ESP32, servidor web local e interfaz gráfica 3D en tiempo real utilizando **Three.js**.

## 🚀 Características
- Control por potenciómetros analógicos (ADC1).
- Servidor Web integrado en la IP fija configurada.
- Visualización 3D interactiva e instantánea en el navegador con Three.js.
- Grabación de secuencias de movimiento en RAM y reproducción en bucle (Loop).

## 🛠️ Librerías requeridas
- `ESP32Servo`
- `WiFi` (incluida en el core de ESP32)
- `WebServer` (incluida en el core de ESP32)

## 📌 Asignación de Pines
- **Servos (PWM):** GPIO 18, 19, 21, 22, 23
- **Potenciómetros (ADC1):** GPIO 36, 39, 34, 35, 32
- **Botón Grabar:** GPIO 4
- **Botón Reproducir:** GPIO 17
- **LED Indicador:** GPIO 2

## ⚙️ Configuración Inicial (Credenciales Wi-Fi)

Por motivos de seguridad, las credenciales de Wi-Fi están separadas en el archivo `secrets.h` (incluido en `.gitignore`).

1. En la misma carpeta de tu proyecto, crea un archivo llamado **`secrets.h`**.
2. Añade las credenciales de tu red local:

```cpp
#ifndef SECRETS_H
#define SECRETS_H

const char* WIFI_SSID = "TU_NOMBRE_DE_RED";
const char* WIFI_PASSWORD = "TU_CONTRASEÑA";

#endif****
