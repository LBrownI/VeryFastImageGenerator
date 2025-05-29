# 🖼️ Very Fast Image Generator

Este proyecto es una aplicación multihilo escrita en C++ que genera imágenes aleatorias de manera eficiente utilizando OpenCV. Un hilo productor crea imágenes al ritmo de un FPS objetivo, mientras varios hilos consumidores las guardan en disco. Es ideal para pruebas de rendimiento, generación de datos de entrenamiento artificiales o benchmarking de sistemas de I/O.

---

## 📦 Características

- Generación de imágenes aleatorias en formato RGB.
- Control preciso del FPS objetivo.
- Sistema de cola limitada con descarte de frames antiguos si hay sobrecarga.
- Múltiples hilos guardadores para maximizar el rendimiento de escritura.
- Métricas detalladas de rendimiento y pérdidas.
- Directorio de salida automático.

---

## 🔧 Requisitos

- **CMake ≥ 3.10**
- **g++ ≥ 11** (o cualquier compilador compatible con C++17 o superior)
- **OpenCV ≥ 4.0**
- **Linux o WSL (recomendado para pruebas de rendimiento con múltiples hilos)**

Instala dependencias en Ubuntu:
```bash
sudo apt update
sudo apt install build-essential cmake libopencv-dev
```

---

## 🚀 Compilación

```bash
mkdir build
cd build
cmake ..
make
```

---

## 🧪 Ejecución

```bash
./random_image_generator <ancho> <alto> <duración_segundos> <fps> <extensión>
```

### Parámetros

- `ancho`: Ancho de la imagen (ej: `1920`)
- `alto`: Alto de la imagen (ej: `1080`)
- `duración_segundos`: Cuánto tiempo generar imágenes (ej: `60`)
- `fps`: Cuántas imágenes por segundo (ej: `30`)
- `extensión`: Formato de salida (`jpg`, `png`, etc.)

### Ejemplo

```bash
./random_image_generator 640 480 10 30 png
```

Este comando genera imágenes de 640x480 durante 10 segundos a 30 fps en formato PNG. Los archivos se guardarán en la carpeta `generated_images/`.

---

## 📊 Ejemplo de Salida

```text
--- Resumen generación (hilo generador) ---
Imágenes objetivo a generar: 300
Imágenes realmente generadas y encoladas: 299
Imágenes descartadas por atraso (no encoladas): 1
Tiempo de generación del hilo: 10.01 segundos
FPS efectivo generación (reloj del hilo): 29.89

--- Resumen Global ---
Imágenes generadas (contador global): 299
Imágenes guardadas (contador global): 299
Tiempo total de ejecución: 10.21 segundos
FPS efectivo de guardado (global): 29.30
Imágenes perdidas por cola: 0
Imágenes perdidas por atraso: 1
TOTAL imágenes perdidas: 1
```

---

## 📂 Estructura del Proyecto

```
VeryFastImageGenerator/
├── CMakeLists.txt
├── random_image_generator.cpp
├── build/
└── generated_images/
```

---

## 🧠 Créditos y Autoría

Desarrollado por **Dante Quezada** como ejercicio avanzado de programación concurrente y procesamiento de imágenes en C++.  
Incluye manejo seguro de sincronización entre hilos con `std::mutex`, `std::deque` y `std::condition_variable`.
