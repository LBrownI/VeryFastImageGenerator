# VeryFastImageGenerator

This is a C++ application designed to generate and save images to disk at high speeds, leveraging multithreading for concurrent image generation and saving. It provides a command-line interface to configure image dimensions, generation duration, frames per second (FPS), and image format. The application outputs detailed statistics on generation and saving performance, including any images lost due to processing bottlenecks of the device.

## Features

*   Concurrent image generation and saving using C++ threads.
*   Configurable image width, height, generation duration, FPS, and output image extension.
*   Detailed performance summary, including effective FPS for generation and saving, and counts of lost images.
*   Uses OpenCV for image generation and saving.

## Dependencies

*   **C++ Compiler** (supporting C++17 or later for `std::filesystem`)
*   **CMake** (version 3.10 or later recommended)
*   **OpenCV** (version 4.x recommended)

## Building the Project

1.  **Clone the repository (if applicable) or ensure you have the source code.**
2.  **Create a build directory:**
    ```bash
    mkdir build
    cd build
    ```
3.  **Run CMake to configure the project:**
    ```bash
    cmake ..
    ```

4.  **Compile the project:**
    ```bash
    make -j
    ```
    The executable `random_image_generator` will be created in the `build` directory.

## Running the Application

Execute the compiled program from the `build` directory with the required command-line arguments:

```bash
./random_image_generator <width> <height> <duration_seconds> <fps> <extension>
```

**Arguments:**

*   `<width>`: Width of the images to generate (e.g., `1920`).
*   `<height>`: Height of the images to generate (e.g., `1080`).
*   `<duration_seconds>`: How long the image generation process should run (e.g., `300` for 5 minutes).
*   `<fps>`: Target frames per second for image generation (e.g., `50`).
*   `<extension>`: Image file extension for saving (e.g., `png`, `jpg`, `bmp`). OpenCV\'s default saving behavior for this extension will be used.

**Example:**

```bash
./random_image_generator 1920 1080 60 30 png
```
This command will generate 1920x1080 images for 60 seconds at a target of 30 FPS, saving them as PNG files in a directory named `generated_images`.

## Understanding the Output

The application will print two main summaries:

### Resumen generación (hilo generador)
This summary is printed by the image generator thread once it finishes its generation phase.
*   `Imágenes objetivo a generar`: The total number of images the generator aimed to produce based on the input duration and FPS.
*   `Imágenes realmente generadas y encoladas`: The actual number of images the generator thread successfully created and placed into the processing queue.
*   `Imágenes descartadas por atraso (no encoladas)`: The number of frames the generator skipped because it was falling behind the target FPS. This happens if generating and enqueuing an image takes longer than the time allocated per frame.
*   `Tiempo de generación del hilo`: The wall-clock time taken by the generator thread.
*   `FPS efectivo generación (reloj del hilo)`: The effective FPS achieved by the generator (`generadas y encoladas / tiempo de generación`).

### Resumen Global
This summary is printed at the very end of the program, after all saver threads have completed.
*   `Imágenes generadas (contador global)`: Re-states the total images generated and enqueued. This should match the generator\'s summary.
*   `Imágenes guardadas (contador global)`: The total number of images successfully written to disk by all saver threads.
*   `Tiempo total de ejecución`: The total wall-clock time from the start of the `main` function to the end.
*   `FPS efectivo de guardado (global, basado en tiempo total)`: The effective FPS for saving images, calculated as `imágenes guardadas / tiempo total de ejecución`.
*   `Imágenes perdidas por cola (no alcanzaron a guardarse)`: Images that were successfully generated and enqueued but were dropped from the queue because it reached its `MAX_QUEUE_SIZE` limit and the savers couldn't process them fast enough.
*   `Imágenes perdidas por atraso (ni siquiera generadas)`: Re-states the images the generator itself couldn't produce in time (same as "descartadas por atraso").
*   `TOTAL imágenes perdidas`: The sum of images lost from the queue and images lost due to generation delay.
*   `Imágenes verificadas en directorio`: An optional count of files found in the output directory. This can be a final check on the number of saved images.

## Notes

*   The application creates an output directory named `generated_images` in the current working directory (where the executable is run) if it doesn't already exist.
*   The number of saver threads is currently fixed at `NUM_SAVER_THREADS = 7`.
*   The maximum queue size between the generator and savers is defined by `MAX_QUEUE_SIZE`. If the queue is full, the generator will drop the oldest image to make space for a new one.

