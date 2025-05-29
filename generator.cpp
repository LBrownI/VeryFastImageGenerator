#include <iostream>
#include <string>
#include <queue>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <thread>
#include <vector> // Para std::vector<std::thread>
#include <atomic> // Para std::atomic<int>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace fs = std::filesystem;

const int NUM_SAVER_THREADS = 7; // Número de hilos guardadores

struct ImageData
{
    cv::Mat image;
    int index; // Índice único para cada imagen generada
};

struct ThreadArgs
{
    int width;
    int height;
    int duration_seconds;
    double fps;
    std::string image_extension;
    std::string output_directory;
    int totalImages; // <- esto se calculará después
};

// Variables compartidas entre hilos
const size_t MAX_QUEUE_SIZE = 100; // o el valor que estimes
std::deque<ImageData> imageQueue;
std::mutex queueMutex;                             // Mutex para proteger el acceso a imageQueue y finishedGenerating
std::condition_variable queueCV;                   // Variable de condición para sincronizar hilos
bool finishedGenerating = false;                   // Bandera para indicar que la generación ha terminado
std::atomic<int> total_images_generated_count = 0; // Contador atómico para imágenes generadas
std::atomic<int> total_images_saved_count = 0;     // Contador atómico para imágenes guardadas
std::atomic<int> total_images_enqueued_count = 0;
std::atomic<int> total_images_dropped_due_to_delay = 0;

// Función para generar una imagen aleatoria
cv::Mat generateRandomImage(int width, int height)
{
    cv::Mat image(height, width, CV_8UC3);
    // Rellena la imagen con píxeles de colores aleatorios
    cv::randu(image, cv::Scalar(0, 0, 0), cv::Scalar(255, 255, 255));
    return image;
}

// Función ejecutada por el hilo generador de imágenes
void imageGenerator(ThreadArgs args)
{
    auto start_generation_timer = std::chrono::steady_clock::now();
    std::chrono::duration<double> frame_duration(1.0 / args.fps);

    int i = 0;
    auto start_time = std::chrono::steady_clock::now();
    auto end_time = start_time + std::chrono::seconds(args.duration_seconds);

    while (std::chrono::steady_clock::now() < end_time)
    {
        auto current_time = std::chrono::steady_clock::now();
        auto next_frame_time = start_time + std::chrono::duration<double>(1.0 / args.fps) * (i + 1);

        if (current_time > next_frame_time)
        {
            total_images_dropped_due_to_delay++;
            i++;
            continue;
        }

        std::this_thread::sleep_until(next_frame_time);
        cv::Mat image = generateRandomImage(args.width, args.height);

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (imageQueue.size() >= MAX_QUEUE_SIZE)
            {
                imageQueue.pop_front();
            }
            imageQueue.push_back({image, i});
            total_images_enqueued_count++;
        }

        queueCV.notify_all();
        total_images_generated_count++;
        i++;
    }

    { // Bloque de lock para proteger la escritura de finishedGenerating
        std::lock_guard<std::mutex> lock(queueMutex);
        finishedGenerating = true; // Indica que la generación ha finalizado
    }
    queueCV.notify_all(); // Notifica a todos los hilos guardadores que la generación terminó

    double generation_time_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_generation_timer).count();
    double effective_fps = 0;
    if (generation_time_seconds > 0)
    {
        effective_fps = total_images_enqueued_count.load() / generation_time_seconds;
    }

    // Imprime resumen de la generación desde el hilo generador
    std::cout << "--- Resumen generación (hilo generador) ---\n";
    std::cout << "Imágenes objetivo a generar: " << args.totalImages << "\n";
    std::cout << "Imágenes realmente generadas y encoladas: " << total_images_generated_count.load() << "\n";
    std::cout << "Imágenes descartadas por atraso (no encoladas): " << total_images_dropped_due_to_delay.load() << "\n";

    std::cout << std::fixed << std::setprecision(2)
              << "Tiempo de generación del hilo: " << generation_time_seconds << " segundos\n";
    std::cout << std::fixed << std::setprecision(2)
              << "FPS efectivo generación (reloj del hilo): " << effective_fps << "\n";
}

// Función ejecutada por cada uno de los hilos guardadores de imágenes
void imageSaver(ThreadArgs args, int saver_id)
{
    // El ID del guardador (saver_id) se pasa por si se necesita para depuración, no se usa activamente aquí.
    while (true)
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        // Espera hasta que la cola no esté vacía O la generación haya terminado
        queueCV.wait(lock, []
                     { return !imageQueue.empty() || finishedGenerating; });

        // Procesa todas las imágenes disponibles en la cola antes de reevaluar finishedGenerating
        while (!imageQueue.empty())
        {
            ImageData imgData = imageQueue.front(); // Toma la imagen del frente de la cola
            imageQueue.pop_front();                 // Remueve la imagen de la cola
            lock.unlock();                          // Desbloquea el mutex mientras se guarda la imagen (operación costosa)

            std::string filename = args.output_directory + "/image_" + std::to_string(imgData.index) + "." + args.image_extension;
            bool success = cv::imwrite(filename, imgData.image); // Guarda la imagen en disco

            if (success)
            {
                total_images_saved_count++; // Incrementa el contador global de imágenes guardadas
            }
            else
            {
                std::cerr << "Error: Hilo guardador " << saver_id << " no pudo guardar la imagen: " << filename << std::endl;
            }

            lock.lock(); // Re-bloquea el mutex para la condición del bucle y para pop/empty check
        }

        // Si la generación ha terminado Y la cola está vacía, el hilo guardador puede terminar
        if (finishedGenerating && imageQueue.empty())
        {
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc != 6)
    {
        std::cerr << "Uso: " << argv[0] << " <ancho> <alto> <duración_segundos> <fps> <extensión>\n";
        std::cerr << "Ejemplo: " << argv[0] << " 640 480 10 30 png\n";
        return 1;
    }

    ThreadArgs args;
    try
    {
        args.width = std::stoi(argv[1]);
        args.height = std::stoi(argv[2]);
        args.duration_seconds = std::stoi(argv[3]);
        args.fps = std::stod(argv[4]);
        args.image_extension = argv[5];
        args.totalImages = static_cast<int>(args.fps * args.duration_seconds);
    }
    catch (...)
    {
        std::cerr << "Error en los argumentos\n";
        return 1;
    }

    args.output_directory = "generated_images";

    if (args.width <= 0 || args.height <= 0 || args.fps <= 0 || args.totalImages < 0)
    {
        std::cerr << "Error: Ancho, alto y FPS deben ser positivos. El total de imágenes no puede ser negativo." << std::endl;
        return 1;
    }

    if (args.totalImages == 0)
    {
        std::cout << "Total de imágenes a generar es 0. No se realizará ninguna acción." << std::endl;
        return 0;
    }

    // Crea el directorio de salida si no existe
    if (!fs::exists(args.output_directory))
    {
        if (!fs::create_directories(args.output_directory))
        {
            std::cerr << "Error: No se pudo crear el directorio de salida: " << args.output_directory << std::endl;
            return 1;
        }
    }

    auto start_global = std::chrono::steady_clock::now(); // Tiempo de inicio global

    // Crea e inicia el hilo generador
    std::thread generatorThread(imageGenerator, args);

    // Crea e inicia los hilos guardadores
    std::vector<std::thread> saverThreads;
    for (int i = 0; i < NUM_SAVER_THREADS; ++i)
    {
        saverThreads.emplace_back(imageSaver, args, i); // Pasa args y un ID para cada hilo guardador
    }

    // Espera a que el hilo generador termine
    generatorThread.join();

    // Espera a que todos los hilos guardadores terminen
    for (int i = 0; i < NUM_SAVER_THREADS; ++i)
    {
        saverThreads[i].join();
    }

    auto end_global = std::chrono::steady_clock::now(); // Tiempo de finalización global
    std::chrono::duration<double> total_elapsed = end_global - start_global;

    // Imprime el resumen global de la ejecución
    std::cout << "\n--- Resumen Global ---\n";
    std::cout << "Imágenes generadas (contador global): " << total_images_generated_count.load() << "\n";
    std::cout << "Imágenes guardadas (contador global): " << total_images_saved_count.load() << "\n";
    std::cout << std::fixed << std::setprecision(2)
              << "Tiempo total de ejecución: " << total_elapsed.count() << " segundos\n";

    if (total_elapsed.count() > 0)
    {
        double overall_saving_fps = total_images_saved_count.load() / total_elapsed.count();
        std::cout << std::fixed << std::setprecision(2)
                  << "FPS efectivo de guardado (global, basado en tiempo total): " << overall_saving_fps << "\n";

        int lost_due_to_queue = total_images_enqueued_count.load() - total_images_saved_count.load();
        int lost_due_to_delay = total_images_dropped_due_to_delay.load();
        int total_lost_images = lost_due_to_queue + lost_due_to_delay;

        std::cout << "Imágenes perdidas por cola (no alcanzaron a guardarse): " << lost_due_to_queue << "\n";
        std::cout << "Imágenes perdidas por atraso (ni siquiera generadas): " << lost_due_to_delay << "\n";
        std::cout << "TOTAL imágenes perdidas: " << total_lost_images << "\n";
    }

    // Verificación opcional: contar archivos en el directorio de salida
    int files_in_directory = 0;
    try
    {
        for (const auto &entry : fs::directory_iterator(args.output_directory))
        {
            if (entry.is_regular_file())
            {
                files_in_directory++;
            }
        }
    }
    catch (const fs::filesystem_error &e)
    {
        // No es crítico si esta parte falla, pero es útil informar.
        std::cerr << "Advertencia: Error al contar archivos en el directorio de salida: " << e.what() << std::endl;
    }

    return 0;
}