# Simulador del Oscilador Armónico Cuántico 3D

## Descripción del Proyecto
Este proyecto es una simulación web interactiva que visualiza la densidad de probabilidad de un oscilador armónico cuántico tridimensional isótropo. Permite modificar los números cuánticos ($n_x$, $n_y$, $n_z$) en tiempo real y observar cómo se reestructura la nube de probabilidad (nodos y antinodos) mediante renderizado 3D con mezcla aditiva.

## Stack Tecnológico
* **Motor Físico y Algebraico:** C++17.
* **Renderizado Gráfico:** OpenGL (GLFW) para aceleración por hardware.
* **Puente Web:** Emscripten (compila C++ a WebAssembly / WebGL).
* **Interfaz:** HTML5, CSS y JavaScript estándar.

## Estructura de Archivos
* `main.cpp`: Contiene la lógica física (evaluación de los polinomios de Hermite, cálculo de la función de distribución acumulada y muestreo) y el pipeline gráfico de OpenGL (Shaders y buffers).
* `index.html`: Define la interfaz de usuario, los controles deslizantes y aloja el `<canvas>` donde se proyecta el entorno WebGL.
* `motor_cuantico.js` / `motor_cuantico.wasm`: Archivos autogenerados por Emscripten que ejecutan el motor en el navegador.

## Instrucciones de Ejecución

**1. Compilación del Motor en C++**
Abre una terminal con el entorno de Emscripten activado (`emsdk_env.bat`) en el directorio del proyecto y ejecuta:

```bash
emcc main.cpp -std=c++17 -O3 -s USE_GLFW=3 -s EXPORTED_RUNTIME_METHODS=ccall -o motor_cuantico.js