// random_image_generator.cpp

// --- Inclusión de Librerías Estándar de C++ ---
#include <iostream>         // Para entrada/salida estándar (ej. std::cout, std::cerr)
#include <string>           // Para usar la clase std::string (cadenas de caracteres)
#include <queue>            // Para usar std::queue (estructura de datos tipo cola FIFO)
#include <mutex>            // Para std::mutex (mecanismo de exclusión mutua para proteger datos compartidos)
#include <condition_variable> // Para std::condition_variable (sincronización de hilos basada en condiciones)
#include <chrono>           // Para medir el tiempo (ej. std::chrono::steady_clock, std::chrono::duration)
#include <iomanip>          // Para manipuladores de flujo de E/S (ej. std::setprecision, std::fixed)
#include <filesystem>       // Para operaciones del sistema de archivos (ej. crear directorios, verificar existencia) (C++17)
#include <thread>           // Para usar std::thread (creación y manejo de hilos)
#include <vector>           // Para usar std::vector (similar a un array dinámico, aquí para guardar los hilos)
#include <atomic>           // Para usar std::atomic (operaciones atómicas en variables compartidas, seguras entre hilos)

// --- Inclusión de Librerías de OpenCV ---
// OpenCV es una biblioteca muy popular para visión por computadora y procesamiento de imágenes.
#include <opencv2/core.hpp>       // Funcionalidades básicas de OpenCV (ej. la clase cv::Mat para imágenes)
#include <opencv2/imgcodecs.hpp>  // Para leer y escribir archivos de imagen (ej. cv::imwrite)
#include <opencv2/imgproc.hpp>    // Para procesamiento de imágenes (ej. cv::randu para rellenar con valores aleatorios)

// --- Alias de Espacio de Nombres ---
// Crea un alias 'fs' para el espacio de nombres std::filesystem, facilitando su uso.
namespace fs = std::filesystem;

// --- Constantes Globales ---
// Define el número de hilos que se dedicarán a guardar imágenes en el disco.
const int NUM_SAVER_THREADS = 7;

// --- Estructuras de Datos ---

// Estructura para almacenar los datos de una imagen y su índice.
// Esta estructura se usará para pasar información entre el hilo generador y los hilos guardadores.
struct ImageData {
    cv::Mat image; // Objeto de OpenCV que contiene los datos de la imagen (píxeles).
    int index;     // Índice único para identificar la imagen (ej. para el nombre del archivo).
};

// Estructura para pasar los argumentos de la línea de comandos a los hilos.
struct ThreadArgs {
    int width;              // Ancho deseado para las imágenes.
    int height;             // Alto deseado para las imágenes.
    int totalImages;        // Número total de imágenes que el generador debe crear.
    double fps;             // Tasa de cuadros por segundo (FPS) objetivo para el generador.
    std::string image_extension; // Extensión para los archivos de imagen (ej. "jpg", "png").
    std::string output_directory; // Directorio donde se guardarán las imágenes.
};

// --- Variables Globales Compartidas entre Hilos ---
// Estas variables son accedidas por múltiples hilos, por lo que su acceso debe ser sincronizado.

// Cola (Queue) para almacenar objetos ImageData. El hilo generador añade imágenes aquí,
// y los hilos guardadores las sacan de aquí. Es una estructura FIFO (First-In, First-Out).
std::queue<ImageData> imageQueue;

// Mutex (Mutual Exclusion) para proteger el acceso a recursos compartidos.
// En este caso, protege 'imageQueue' y la bandera 'finishedGenerating'.
// Solo un hilo puede "poseer" (lock) el mutex a la vez, evitando condiciones de carrera.
std::mutex queueMutex;

// Variable de Condición (Condition Variable) para sincronizar hilos.
// Permite a los hilos esperar (wait) hasta que una condición específica se cumpla.
// Se usa junto con un mutex.
std::condition_variable queueCV;

// Bandera booleana para indicar si el hilo generador ha terminado de producir todas las imágenes.
// 'volatile' podría considerarse si no se usa mutex, pero con mutex y CV es seguro.
// No obstante, para este tipo de bandera simple entre productor/consumidor, un std::atomic<bool> sería más idiomático.
bool finishedGenerating = false;

// Contadores atómicos para llevar la cuenta de imágenes generadas y guardadas.
// 'std::atomic' asegura que las operaciones de incremento sean seguras entre hilos
// sin necesidad de un mutex explícito para *estos contadores específicos*.
std::atomic<int> total_images_generated_count = 0;
std::atomic<int> total_images_saved_count = 0;

// --- Funciones ---

// Función para generar una imagen con píxeles de colores aleatorios.
// Recibe el ancho y alto deseados para la imagen.
// Devuelve un objeto cv::Mat que representa la imagen.
cv::Mat generateRandomImage(int width, int height) {
    // Crea un objeto cv::Mat (matriz) con el alto, ancho y tipo especificados.
    // CV_8UC3 significa:
    //   8U: Enteros sin signo de 8 bits (0-255) por cada componente de color.
    //   C3: 3 canales, típicamente para imágenes a color (Azul, Verde, Rojo - BGR en OpenCV por defecto).
    cv::Mat image(height, width, CV_8UC3);

    // Rellena la imagen 'image' con valores aleatorios.
    // cv::randu(matriz_destino, valor_minimo_aleatorio, valor_maximo_aleatorio);
    // cv::Scalar(0, 0, 0) es el negro (mínimo para cada canal BGR).
    // cv::Scalar(255, 255, 255) es el blanco (máximo para cada canal BGR).
    cv::randu(image, cv::Scalar(0, 0, 0), cv::Scalar(255, 255, 255));

    return image; // Devuelve la imagen generada.
}

// Función que será ejecutada por el hilo generador de imágenes (Productor).
// 'args' contiene los parámetros de configuración (ancho, alto, fps, etc.).
void imageGenerator(ThreadArgs args) {
    // Inicia un temporizador para medir el tiempo total de generación de este hilo.
    auto start_generation_timer = std::chrono::steady_clock::now();

    // Calcula la duración que debe tener cada "cuadro" (frame) para alcanzar los FPS objetivo.
    // Por ejemplo, si args.fps es 30, frame_duration será 1/30 = 0.0333... segundos.
    std::chrono::duration<double> frame_duration(1.0 / args.fps);

    // Bucle para generar el número total de imágenes especificado.
    for (int i = 0; i < args.totalImages; ++i) {
        // Llama a la función para crear una imagen aleatoria.
        cv::Mat image = generateRandomImage(args.width, args.height);

        // Bloque de código crítico: se accede a recursos compartidos (imageQueue).
        { // Este bloque define el alcance del std::lock_guard.
            // std::lock_guard adquiere el mutex 'queueMutex' al ser creado.
            // El mutex se libera automáticamente cuando 'lock' sale del alcance (al final del bloque {}).
            // Esto asegura que solo este hilo pueda modificar 'imageQueue' en este momento.
            std::lock_guard<std::mutex> lock(queueMutex);

            // Añade la imagen generada y su índice 'i' a la cola 'imageQueue'.
            // Se crea un objeto ImageData temporal y se mueve (o copia) a la cola.
            imageQueue.push({image, i});
        } // 'lock' se destruye aquí, liberando 'queueMutex'.

        // Notifica a TODOS los hilos que están esperando en 'queueCV'.
        // Esto despierta a los hilos guardadores, indicando que hay una nueva imagen en la cola.
        // Usar notify_all() es simple pero puede causar el "problema del trueno" (thundering herd)
        // si muchos hilos se despiertan y solo uno puede tomar el trabajo. Para este caso,
        // con múltiples imágenes siendo añadidas y múltiples consumidores, está bien.
        // notify_one() despertaría solo a un hilo guardador.
        queueCV.notify_all();

        // Incrementa el contador atómico de imágenes generadas.
        // fetch_add o ++ son operaciones atómicas.
        total_images_generated_count++;

        // Control de FPS: Pausa el hilo generador para mantener la tasa de cuadros deseada.
        // Calcula el momento exacto en el tiempo en que el siguiente cuadro debería, idealmente, comenzar a generarse.
        // Se basa en el tiempo de inicio del bucle de generación y el número de cuadros ya procesados.
        auto next_frame_time = start_generation_timer + frame_duration * (i + 1.0);

        // Pausa la ejecución del hilo actual hasta que se alcance 'next_frame_time'.
        // Si el tiempo actual ya pasó 'next_frame_time' (porque la generación y encolado tardaron mucho),
        // esta función retorna inmediatamente.
        std::this_thread::sleep_until(next_frame_time);
    }

    // Después de generar todas las imágenes, se actualiza la bandera 'finishedGenerating'.
    { // Bloque de lock para proteger la escritura de 'finishedGenerating'.
        std::lock_guard<std::mutex> lock(queueMutex);
        finishedGenerating = true; // Indica a los hilos guardadores que no se producirán más imágenes.
    }
    // Notifica nuevamente a todos los hilos guardadores.
    // Esto es importante para despertar a cualquier hilo guardador que podría estar esperando
    // en 'queueCV.wait()' y necesita ver que 'finishedGenerating' es true para poder terminar.
    queueCV.notify_all();

    // Calcula el tiempo total que tomó la generación de imágenes en este hilo.
    double generation_time_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_generation_timer).count();
    double effective_fps = 0;
    if (generation_time_seconds > 0) {
        // Calcula los FPS efectivos basados en las imágenes realmente generadas y el tiempo tomado.
        effective_fps = total_images_generated_count.load() / generation_time_seconds;
    }

    // Imprime un resumen desde el hilo generador.
    // Es buena práctica que los hilos no hagan demasiada E/S (como std::cout) para no interferir
    // con el rendimiento de otros hilos, pero para un resumen final está bien.
    std::cout << "--- Resumen generación (hilo generador) ---\n";
    std::cout << "Imágenes objetivo a generar: " << args.totalImages << "\n";
    std::cout << "Imágenes realmente generadas y encoladas: " << total_images_generated_count.load() << "\n";
    std::cout << std::fixed << std::setprecision(2) // Formatea la salida de números flotantes
              << "Tiempo de generación del hilo: " << generation_time_seconds << " segundos\n";
    std::cout << std::fixed << std::setprecision(2)
              << "FPS efectivo generación (reloj del hilo): " << effective_fps << "\n";
}

// Función que será ejecutada por cada uno de los hilos guardadores de imágenes (Consumidor).
// 'args' contiene los parámetros de configuración.
// 'saver_id' es un identificador para este hilo guardador específico (útil para depuración).
void imageSaver(ThreadArgs args, int saver_id) {
    // Bucle principal del hilo guardador. Continúa hasta que se cumpla la condición de salida.
    while (true) {
        // std::unique_lock es más flexible que std::lock_guard. Permite bloquear y desbloquear
        // el mutex manualmente, lo cual es necesario para usar con std::condition_variable.
        std::unique_lock<std::mutex> lock(queueMutex);

        // Espera en la variable de condición 'queueCV'.
        // El hilo se bloqueará aquí (liberando el mutex 'lock' mientras espera) hasta que:
        // 1. La cola 'imageQueue' NO esté vacía, O
        // 2. La bandera 'finishedGenerating' sea true.
        // La función lambda `[]{ return !imageQueue.empty() || finishedGenerating; }` es el predicado.
        // `wait` verifica el predicado. Si es falso, el hilo duerme. Si es verdadero (o después de ser notificado
        // y el predicado es verdadero), `wait` retorna y el hilo vuelve a adquirir el mutex.
        // Esto previene "despertares espurios".
        queueCV.wait(lock, []{ return !imageQueue.empty() || finishedGenerating; });

        // Una vez despierto y con el mutex adquirido, procesa todas las imágenes disponibles en la cola.
        // Esto es para que un hilo guardador pueda procesar múltiples imágenes si se acumularon
        // mientras otros hilos estaban ocupados o si el productor es muy rápido.
        while (!imageQueue.empty()) {
            // Obtiene el primer elemento (ImageData) de la cola.
            ImageData imgData = imageQueue.front();
            // Remueve el elemento del frente de la cola.
            imageQueue.pop();

            // Desbloquea el mutex ANTES de la operación costosa de guardar la imagen en disco.
            // Esto permite que otros hilos (el generador u otros guardadores) puedan acceder a la cola
            // mientras este hilo está ocupado con la E/S del disco.
            lock.unlock();

            // Construye el nombre del archivo para la imagen.
            std::string filename = args.output_directory + "/image_" + std::to_string(imgData.index) + "." + args.image_extension;

            // Intenta guardar la imagen en el disco usando cv::imwrite de OpenCV.
            // cv::imwrite(ruta_del_archivo, objeto_cv_mat_de_la_imagen);
            bool success = cv::imwrite(filename, imgData.image);

            if (success) {
                // Si se guardó con éxito, incrementa el contador global de imágenes guardadas.
                total_images_saved_count++;
            } else {
                // Si falla, imprime un mensaje de error.
                std::cerr << "Error: Hilo guardador " << saver_id << " no pudo guardar la imagen: " << filename << std::endl;
            }

            // Vuelve a adquirir el lock ANTES de verificar la condición del bucle 'while (!imageQueue.empty())'
            // y antes de la siguiente posible llamada a 'queueCV.wait()'.
            lock.lock();
        } // Fin del bucle 'while (!imageQueue.empty())'

        // Condición de terminación para el hilo guardador:
        // Si la generación ha terminado (finishedGenerating es true) Y la cola está vacía,
        // significa que no hay más trabajo por hacer, por lo que el hilo puede terminar.
        if (finishedGenerating && imageQueue.empty()) {
            break; // Sale del bucle 'while (true)'.
        }
        // Si la cola está vacía pero finishedGenerating es falso, el hilo volverá a esperar en queueCV.wait().
    }
}

// --- Función Principal (main) ---
// Punto de entrada del programa.
int main(int argc, char *argv[]) {
    // Verifica que se haya proporcionado el número correcto de argumentos en la línea de comandos.
    // argc: contador de argumentos (incluyendo el nombre del programa).
    // argv: array de cadenas de caracteres (los argumentos).
    if (argc != 6) {
        std::cerr << "Uso: " << argv[0] << " <ancho> <alto> <duracion_segundos> <fps> <extension_imagen>\n";
        std::cerr << "Ejemplo: " << argv[0] << " 640 480 10 30 png\n";
        return 1; // Retorna 1 para indicar un error.
    }

    ThreadArgs args; // Crea una instancia de la estructura para almacenar los argumentos.
    try {
        // Convierte los argumentos de la línea de comandos (que son cadenas) a los tipos numéricos apropiados.
        // argv[0] es el nombre del programa.
        // argv[1] es el ancho, argv[2] es el alto, etc.
        args.width = std::stoi(argv[1]);             // string to int
        args.height = std::stoi(argv[2]);            // string to int
        int duration_seconds = std::stoi(argv[3]);   // string to int
        args.fps = std::stod(argv[4]);               // string to double
        // Calcula el número total de imágenes a generar basado en la duración y los FPS.
        args.totalImages = static_cast<int>(args.fps * duration_seconds);
        args.image_extension = argv[5];              // El último argumento es la extensión.
    } catch (const std::invalid_argument& ia) {
        // Captura excepciones si la conversión falla (ej. si se pasa "abc" donde se espera un número).
        std::cerr << "Error: Argumento inválido proporcionado. " << ia.what() << std::endl;
        return 1;
    } catch (const std::out_of_range& oor) {
        // Captura excepciones si el número convertido está fuera del rango representable por el tipo.
        std::cerr << "Error: Argumento fuera de rango. " << oor.what() << std::endl;
        return 1;
    }
    
    // Establece el directorio de salida por defecto.
    args.output_directory = "generated_images";

    // Validación adicional de los argumentos.
    if (args.width <= 0 || args.height <= 0 || args.fps <= 0 || args.totalImages < 0) {
        std::cerr << "Error: Ancho, alto y FPS deben ser positivos. El total de imágenes no puede ser negativo." << std::endl;
        return 1;
    }

    if (args.totalImages == 0) {
        std::cout << "Total de imágenes a generar es 0. No se realizará ninguna acción." << std::endl;
        return 0; // Termina el programa exitosamente si no hay nada que hacer.
    }

    // Crea el directorio de salida si no existe.
    if (!fs::exists(args.output_directory)) { // Verifica si el directorio existe.
        if (!fs::create_directories(args.output_directory)) { // Intenta crear el directorio (y padres si es necesario).
            std::cerr << "Error: No se pudo crear el directorio de salida: " << args.output_directory << std::endl;
            return 1;
        }
    }

    // Inicia un temporizador global para medir el tiempo total de ejecución del programa.
    auto start_global = std::chrono::steady_clock::now();

    // --- Creación e Inicio de Hilos ---

    // Crea un nuevo hilo que ejecutará la función 'imageGenerator' con los 'args' proporcionados.
    std::thread generatorThread(imageGenerator, args);

    // Crea un vector para almacenar los hilos guardadores.
    std::vector<std::thread> saverThreads;
    // Bucle para crear el número especificado de hilos guardadores (NUM_SAVER_THREADS).
    for (int i = 0; i < NUM_SAVER_THREADS; ++i) {
        // 'emplace_back' construye un nuevo hilo directamente en el vector.
        // Cada hilo guardador ejecutará la función 'imageSaver' con 'args' y un ID único 'i'.
        saverThreads.emplace_back(imageSaver, args, i);
    }

    // --- Espera (Join) a que los Hilos Terminen ---

    // El hilo principal (main) espera a que el 'generatorThread' complete su ejecución.
    // 'join()' bloquea el hilo main hasta que 'generatorThread' termine.
    // Es importante hacer 'join' en los hilos que se crean para asegurar que terminen
    // su trabajo antes de que el programa principal finalice, y para liberar recursos.
    generatorThread.join();

    // El hilo principal espera a que cada uno de los hilos guardadores termine.
    for (int i = 0; i < NUM_SAVER_THREADS; ++i) {
        saverThreads[i].join();
    }

    // Detiene el temporizador global.
    auto end_global = std::chrono::steady_clock::now();
    // Calcula la duración total de la ejecución.
    std::chrono::duration<double> total_elapsed = end_global - start_global;

    // --- Impresión del Resumen Global ---
    std::cout << "\n--- Resumen Global ---\n";
    std::cout << "Imágenes generadas (contador global): " << total_images_generated_count.load() << "\n";
    std::cout << "Imágenes guardadas (contador global): " << total_images_saved_count.load() << "\n";
    std::cout << std::fixed << std::setprecision(2) // Formatea la salida de números flotantes
              << "Tiempo total de ejecución: " << total_elapsed.count() << " segundos\n";
    
    if (total_elapsed.count() > 0) {
        // Calcula el FPS efectivo de guardado basado en el tiempo total y las imágenes guardadas.
        double overall_saving_fps = total_images_saved_count.load() / total_elapsed.count();
        std::cout << std::fixed << std::setprecision(2)
                  << "FPS efectivo de guardado (global, basado en tiempo total): " << overall_saving_fps << "\n";
    }

    // Verificación opcional: cuenta cuántos archivos hay realmente en el directorio de salida.
    // Esto puede ser útil para comparar con 'total_images_saved_count'.
    int files_in_directory = 0;
    try {
        // Itera sobre todos los elementos en el directorio de salida.
        for (const auto& entry : fs::directory_iterator(args.output_directory)) {
            if (entry.is_regular_file()) { // Si es un archivo regular (no un directorio, etc.)
                files_in_directory++;
            }
        }
        std::cout << "Imágenes verificadas en directorio: " << files_in_directory << "\n";
    } catch (const fs::filesystem_error& e) {
        // Esta parte no es crítica para la funcionalidad principal, así que solo se imprime una advertencia.
        std::cerr << "Advertencia: Error al contar archivos en el directorio de salida: " << e.what() << std::endl;
    }

    return 0; // El programa termina exitosamente.
}