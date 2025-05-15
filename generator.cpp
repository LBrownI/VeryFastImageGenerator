// generator.cpp
#include <iostream>
#include <string>
#include <vector>
#include <chrono>       // Para medición de tiempo
#include <iomanip>      // Para std::fixed y std::setprecision
#include <filesystem>   // Para creación de directorios (C++17)
#include <pthread.h>    // Para hilos POSIX
#include <stdexcept>    // Para excepciones como std::stoi, std::stod
#include <algorithm>    // Para std::tolower, std::find
#include <thread>       // Para std::this_thread::sleep_for
#include <queue>        // Para std::queue (cola compartida)
#include <atomic>       // Para std::atomic<long long> y std::atomic<bool>
#include <numeric>      // Para std::accumulate (si fuera necesario)

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp> // Específicamente para cv::imwrite

namespace fs = std::filesystem;

// --- Configuración ---
const int NUM_CONSUMER_THREADS = 7;
const size_t MAX_QUEUE_SIZE = 99999; // Tamaño máximo de la cola intermedia (ajustable)

// --- Estructuras de Datos para Comunicación entre Hilos ---
struct QueuedImage {
    cv::Mat image_data;
    std::string filename;
    long long image_id;      // ID único secuencial para cada imagen producida
    bool is_poison_pill; // Señal para que los consumidores terminen

    QueuedImage(cv::Mat data = cv::Mat(), std::string fname = "", long long id = -1, bool poison = false)
        : image_data(std::move(data)), filename(std::move(fname)), image_id(id), is_poison_pill(poison) {}
};

std::queue<QueuedImage> image_queue;
pthread_mutex_t queue_mutex;
pthread_cond_t cond_queue_not_full;  // Señal cuando hay espacio en la cola
pthread_cond_t cond_queue_not_empty; // Señal cuando hay items en la cola

std::atomic<bool> producer_should_stop(false); // Señal global para que el productor pare
std::atomic<long long> total_bytes_written(0); // Contador global para bytes escritos
std::atomic<long long> images_produced_count(0); // Contador de imágenes producidas (exitosamente encoladas)
std::atomic<long long> images_saved_count(0);    // Contador de imágenes guardadas

// Argumentos para los hilos (compartidos o específicos)
struct ThreadArgs {
    int width;
    int height;
    double target_fps_producer; // FPS objetivo para el productor
    long duration_seconds;      // Duración total de la ejecución del productor
    std::string image_extension;
    std::string output_directory;

    // Métricas
    double actual_producer_generation_time_sec; // Tiempo puro de CPU generando imágenes
    double producer_operational_time_sec;     // Tiempo total del productor (incluye delays y esperas de cola)

    ThreadArgs() : actual_producer_generation_time_sec(0.0), producer_operational_time_sec(0.0) {}
};

// Función para generar una imagen aleatoria
cv::Mat generateRandomImage(int width, int height) {
    if (width <= 0 || height <= 0) {
        return cv::Mat();
    }
    cv::Mat randomImage(height, width, CV_8UC3); 
    cv::randu(randomImage, cv::Scalar(0, 0, 0), cv::Scalar(255, 255, 255));
    return randomImage;
}

// --- Hilo Productor ---
void* producerLoop(void* arg) {
    ThreadArgs* args = static_cast<ThreadArgs*>(arg);
    double accumulated_pure_generation_time_sec = 0.0;
    long long next_image_id_to_generate = 0; 

    std::chrono::duration<double> target_frame_duration(0);
    if (args->target_fps_producer > 0) {
        target_frame_duration = std::chrono::duration<double>(1.0 / args->target_fps_producer);
    }

    auto producer_operational_start_time = std::chrono::high_resolution_clock::now();
    auto overall_execution_start_time = producer_operational_start_time; // Para la duración total

    while (true) {
        auto current_iteration_wall_time_start = std::chrono::high_resolution_clock::now();

        // Comprobar duración total de ejecución
        std::chrono::duration<double> elapsed_execution_time = current_iteration_wall_time_start - overall_execution_start_time;
        if (elapsed_execution_time.count() >= args->duration_seconds || producer_should_stop.load()) {
            producer_should_stop.store(true);
            break; 
        }

        // --- 1. Generación de Imagen (CPU-bound) ---
        auto pure_gen_start_time = std::chrono::high_resolution_clock::now();
        cv::Mat image = generateRandomImage(args->width, args->height);
        auto pure_gen_end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> pure_gen_duration_this_iteration = pure_gen_end_time - pure_gen_start_time;
        accumulated_pure_generation_time_sec += pure_gen_duration_this_iteration.count();

        // --- 2. Pacing del Productor (basado en su propia velocidad de generación) ---
        // El productor intenta iniciar una nueva generación de imagen según su target_fps.
        // Si la generación fue más rápida que el objetivo, duerme la diferencia.
        if (args->target_fps_producer > 0) {
            if (pure_gen_duration_this_iteration < target_frame_duration) {
                std::this_thread::sleep_for(target_frame_duration - pure_gen_duration_this_iteration);
            }
            // Si pure_gen_duration_this_iteration >= target_frame_duration,
            // la generación ya es más lenta o igual al objetivo, así que no se duerme.
        }
        // En este punto, el productor ha "gastado" tiempo (generando + durmiendo opcionalmente)
        // para alinearse con su `target_fps_producer` para la fase de *creación*.

        if (image.empty()) {
            std::cerr << "Productor: Error generando imagen ID " << next_image_id_to_generate << "." << std::endl;
            next_image_id_to_generate++; // Avanzar ID para marcar el intento fallido
            continue; // Continuar con la siguiente iteración (y su respectivo pacing)
        }

        std::string filename = args->output_directory + "/image_" + std::to_string(next_image_id_to_generate) + "." + args->image_extension;
        QueuedImage q_item(std::move(image), filename, next_image_id_to_generate, false);

        // --- 3. Encolar la Imagen (puede bloquear si la cola está llena) ---
        pthread_mutex_lock(&queue_mutex);
        while (image_queue.size() >= MAX_QUEUE_SIZE && !producer_should_stop.load()) {
            // Esperar si la cola está llena, a menos que se deba parar
            pthread_cond_wait(&cond_queue_not_full, &queue_mutex);
        }

        if (producer_should_stop.load()) { // Volver a comprobar después de esperar, antes de encolar
            pthread_mutex_unlock(&queue_mutex);
            // Si debemos parar y estábamos bloqueados, no encolar esta última imagen.
            // El ID `next_image_id_to_generate` para esta imagen no se contará como "producida".
            break; 
        }

        image_queue.push(std::move(q_item));
        images_produced_count.fetch_add(1); // Contar como producida solo si se encola exitosamente
        next_image_id_to_generate++;        // Preparar ID para la *siguiente* imagen
        
        pthread_cond_signal(&cond_queue_not_empty); // Señalar a los consumidores
        pthread_mutex_unlock(&queue_mutex);
    }

    producer_should_stop.store(true); // Asegurar que está marcado antes de enviar poison pills

    std::cout << "Productor: Producción finalizada. Enviando " << NUM_CONSUMER_THREADS << " poison pills." << std::endl;
    for (int i = 0; i < NUM_CONSUMER_THREADS; ++i) {
        pthread_mutex_lock(&queue_mutex);
        while (image_queue.size() >= MAX_QUEUE_SIZE) { 
            pthread_cond_wait(&cond_queue_not_full, &queue_mutex);
        }
        image_queue.push(QueuedImage(cv::Mat(), "", -1, true)); 
        pthread_cond_signal(&cond_queue_not_empty); 
        pthread_mutex_unlock(&queue_mutex);
    }

    auto producer_operational_end_time = std::chrono::high_resolution_clock::now();
    args->producer_operational_time_sec = std::chrono::duration<double>(producer_operational_end_time - producer_operational_start_time).count();
    args->actual_producer_generation_time_sec = accumulated_pure_generation_time_sec;

    std::cout << "Productor: Hilo finalizado." << std::endl;
    pthread_exit(nullptr);
    return nullptr;
}

// --- Hilo Consumidor ---
void* consumerLoop(void* arg) {
    ThreadArgs* common_args = static_cast<ThreadArgs*>(arg); 
    long long local_images_saved_this_thread = 0;

    while (true) {
        QueuedImage q_item;

        pthread_mutex_lock(&queue_mutex);
        while (image_queue.empty()) {
            if (producer_should_stop.load() && image_queue.empty()) { 
                pthread_mutex_unlock(&queue_mutex);
                pthread_exit(nullptr); 
                return nullptr;
            }
            pthread_cond_wait(&cond_queue_not_empty, &queue_mutex);
        }
        q_item = std::move(image_queue.front());
        image_queue.pop();
        pthread_cond_signal(&cond_queue_not_full); 
        pthread_mutex_unlock(&queue_mutex);

        if (q_item.is_poison_pill) {
            break; 
        }

        if (q_item.image_data.empty()) {
            std::cerr << "Consumidor: Imagen vacía dequeued (ID: " << q_item.image_id << "). Saltando." << std::endl;
            continue;
        }

        bool saved_successfully = false;
        long file_size = 0;
        try {
            std::vector<int> params;
            if (common_args->image_extension == "jpg" || common_args->image_extension == "jpeg") {
                params.push_back(cv::IMWRITE_JPEG_QUALITY);
                params.push_back(90); 
            }
            saved_successfully = cv::imwrite(q_item.filename, q_item.image_data, params);

            if (saved_successfully) {
                std::error_code ec;
                file_size = fs::file_size(q_item.filename, ec);
                if (ec) {
                    file_size = 0; 
                }
            }
        } catch (const cv::Exception& ex) {
            std::cerr << "Consumidor: Excepción de OpenCV al guardar imagen " << q_item.filename << " (ID: " << q_item.image_id << "): " << ex.what() << std::endl;
            saved_successfully = false;
        } catch (const std::exception& ex_std) {
            std::cerr << "Consumidor: Excepción genérica al guardar imagen " << q_item.filename << " (ID: " << q_item.image_id << "): " << ex_std.what() << std::endl;
            saved_successfully = false;
        }

        if (saved_successfully) {
            images_saved_count.fetch_add(1);
            total_bytes_written.fetch_add(file_size);
            local_images_saved_this_thread++;
        } else {
            std::cerr << "Consumidor: Error al guardar imagen " << q_item.filename << " (ID: " << q_item.image_id << ")" << std::endl;
        }
    }
    pthread_exit(nullptr);
    return nullptr;
}


void print_usage(const char* program_name) {
    std::cerr << "Uso: " << program_name << " <ancho> <alto> <fps_productor_objetivo> <duracion_minutos> <extension>" << std::endl;
    std::cerr << "  ancho:         Ancho de la imagen en píxeles (entero > 0)" << std::endl;
    std::cerr << "  alto:          Alto de la imagen en píxeles (entero > 0)" << std::endl;
    std::cerr << "  fps_objetivo:  FPS objetivo para el productor (double, ej. 50.0. Usar 0 para máx. velocidad)" << std::endl;
    std::cerr << "  duracion_min:  Duración de la ejecución en minutos (double, ej. 5.0)" << std::endl;
    std::cerr << "  extension:     Extensión del archivo de imagen (ej. png, jpg, bmp, tiff)" << std::endl;
    std::cerr << "Ejemplo: " << program_name << " 1920 1280 50 1.0 png" << std::endl;
}

std::string to_lower_str(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return s;
}

int main(int argc, char *argv[]) {
    if (argc != 6) { 
        std::cerr << "Error: Número incorrecto de argumentos. Se esperaban 5, se recibieron " << argc - 1 << "." << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    ThreadArgs args; 
    args.output_directory = "generated_images_project"; 

    try {
        args.width = std::stoi(argv[1]);
        args.height = std::stoi(argv[2]);
        args.target_fps_producer = std::stod(argv[3]); 
        double duration_minutes = std::stod(argv[4]);
        args.duration_seconds = static_cast<long>(duration_minutes * 60.0);
        args.image_extension = to_lower_str(argv[5]);
    } catch (const std::invalid_argument& ia) {
        std::cerr << "Error: Argumento inválido. " << ia.what() << std::endl; print_usage(argv[0]); return 1;
    } catch (const std::out_of_range& oor) {
        std::cerr << "Error: Valor del argumento fuera de rango. " << oor.what() << std::endl; print_usage(argv[0]); return 1;
    }

    if (args.width <= 0 || args.height <= 0 || args.duration_seconds <= 0) {
        std::cerr << "Error: Ancho, alto y duración deben ser positivos." << std::endl; print_usage(argv[0]); return 1;
    }
     if (args.target_fps_producer < 0) {
        std::cerr << "Error: FPS objetivo no puede ser negativo (usar 0 para máximo)." << std::endl; print_usage(argv[0]); return 1;
    }
    
    const std::vector<std::string> supported_extensions = {"png", "jpg", "jpeg", "bmp", "tiff", "tif"};
    if (std::find(supported_extensions.begin(), supported_extensions.end(), args.image_extension) == supported_extensions.end()) {
        std::cerr << "Error: Extensión de imagen no soportada: '" << args.image_extension << "'." << std::endl; 
        std::cerr << "Soportadas: png, jpg, jpeg, bmp, tiff, tif." << std::endl; return 1;
    }

    if (!fs::exists(args.output_directory)) {
        std::cout << "Directorio de salida '" << args.output_directory << "' no existe. Creando..." << std::endl;
        try {
            if (!fs::create_directories(args.output_directory)) {
                std::cerr << "Error: No se pudo crear el directorio " << args.output_directory << "." << std::endl; return 1;
            }
        } catch (const fs::filesystem_error& e) {
            std::cerr << "Error de sistema de archivos al crear directorio: " << e.what() << std::endl; return 1;
        }
    } else {
        std::cout << "Directorio de salida '" << args.output_directory << "' ya existe." << std::endl;
    }

    pthread_mutex_init(&queue_mutex, nullptr);
    pthread_cond_init(&cond_queue_not_full, nullptr);
    pthread_cond_init(&cond_queue_not_empty, nullptr);
    producer_should_stop.store(false); 
    total_bytes_written.store(0);
    images_produced_count.store(0);
    images_saved_count.store(0);

    pthread_t producer_thread_id;
    pthread_t consumer_thread_ids[NUM_CONSUMER_THREADS];

    auto overall_process_start_time = std::chrono::high_resolution_clock::now();

    std::cout << "Iniciando proceso de generación de imágenes (1 productor, " << NUM_CONSUMER_THREADS << " consumidores)..." << std::endl;
    std::cout << std::fixed << std::setprecision(1); 
    std::cout << "Configuración: " << args.width << "x" << args.height << " imágenes, "
              << "Duración: " << (args.duration_seconds / 60.0) << " minutos, "
              << (args.target_fps_producer > 0 ? "Productor FPS Objetivo: " + std::to_string(args.target_fps_producer) + ", " : "Productor FPS Máx., ")
              << "Formato: ." << args.image_extension << ", Salida: '" << args.output_directory << "'" << std::endl;
    
    std::cout << std::setprecision(3); 

    if (pthread_create(&producer_thread_id, nullptr, producerLoop, &args)) {
        std::cerr << "Error al crear hilo productor." << std::endl; return 1;
    }
    for (int i = 0; i < NUM_CONSUMER_THREADS; ++i) {
        if (pthread_create(&consumer_thread_ids[i], nullptr, consumerLoop, &args)) {
            std::cerr << "Error al crear hilo consumidor " << i << "." << std::endl; 
            producer_should_stop.store(true); 
            if (producer_thread_id != 0) { // Asegurarse que el productor fue creado antes de unirse
                 pthread_join(producer_thread_id, nullptr);
            }
            for(int j=0; j<i; ++j) {
                if(consumer_thread_ids[j] !=0) pthread_join(consumer_thread_ids[j], nullptr);
            }
            return 1;
        }
    }

    if (producer_thread_id != 0) {
        pthread_join(producer_thread_id, nullptr);
    }
    std::cout << "Hilo productor principal unido. Esperando a los " << NUM_CONSUMER_THREADS << " hilos consumidores..." << std::endl;
    for (int i = 0; i < NUM_CONSUMER_THREADS; ++i) {
        if (consumer_thread_ids[i] != 0) {
            pthread_join(consumer_thread_ids[i], nullptr);
        }
    }
    std::cout << "Todos los hilos consumidores unidos." << std::endl;

    auto overall_process_end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> overall_elapsed_seconds = overall_process_end_time - overall_process_start_time;

    pthread_mutex_destroy(&queue_mutex);
    pthread_cond_destroy(&cond_queue_not_full);
    pthread_cond_destroy(&cond_queue_not_empty);

    long long final_images_produced = images_produced_count.load();
    long long final_images_saved = images_saved_count.load();
    long long final_total_bytes = total_bytes_written.load();

    std::cout << "\n--- Resumen General del Proceso ---" << std::endl;
    std::cout << "Duración configurada: " << std::fixed << std::setprecision(3) << (args.duration_seconds / 60.0) << " minutos." << std::endl;
    std::cout << "Tiempo total de ejecución del programa: " << overall_elapsed_seconds.count() << " segundos." << std::endl;

    std::cout << "\nMétricas del Productor:" << std::endl;
    std::cout << "  Imágenes producidas (encoladas exitosamente): " << final_images_produced << std::endl; 
    std::cout << "  FPS Objetivo para el productor: ";
    if (args.target_fps_producer > 0) std::cout << std::fixed << std::setprecision(1) << args.target_fps_producer; else std::cout << "Máximo";
    std::cout << std::endl;
    std::cout << std::setprecision(3); // Volver a 3 decimales para el resto
    std::cout << "  Tiempo puro de generación de CPU (productor): " << args.actual_producer_generation_time_sec << " segundos." << std::endl;
    if (final_images_produced > 0 && args.actual_producer_generation_time_sec > 0) {
        std::cout << "  FPS Potencial de Generación Pura (basado en tiempo CPU): " 
                  << final_images_produced / args.actual_producer_generation_time_sec << " FPS." << std::endl;
    }
    std::cout << "  Tiempo operacional del productor (incluye delays y esperas de cola): " << args.producer_operational_time_sec << " segundos." << std::endl;
    if (final_images_produced > 0 && args.producer_operational_time_sec > 0) {
        std::cout << "  FPS Efectivo del Productor (imágenes encoladas / tiempo op. productor): " 
                  << final_images_produced / args.producer_operational_time_sec << " FPS." << std::endl;
    }
    
    std::cout << "\nMétricas de los Consumidores:" << std::endl;
    std::cout << "  Imágenes guardadas exitosamente en disco: " << final_images_saved << std::endl;
    if (final_images_produced > 0) { // Calcular pérdida solo si algo se produjo
         double loss_percentage = 0.0;
         long long images_lost = 0;
         if (final_images_produced > final_images_saved) {
            images_lost = final_images_produced - final_images_saved;
            loss_percentage = static_cast<double>(images_lost) / final_images_produced * 100.0;
         }
        std::cout << "  Imágenes perdidas (producidas pero no guardadas): " << images_lost
                  << " (" << std::fixed << std::setprecision(2) << loss_percentage << "%)" << std::endl;
    }


    std::cout << "\nRendimiento General de Escritura en Disco:" << std::endl;
    std::cout << std::setprecision(3); // Asegurar 3 decimales para estas métricas
    std::cout << "  Total de bytes escritos en disco: " << final_total_bytes << " bytes ";
    double total_mb_written = static_cast<double>(final_total_bytes) / (1024.0 * 1024.0);
    std::cout << "(" << std::fixed << std::setprecision(2) << total_mb_written << " MB)." << std::endl;

    if (final_images_saved > 0 && overall_elapsed_seconds.count() > 0) {
        std::cout << std::setprecision(3); // Asegurar 3 decimales
        std::cout << "  FPS Efectivo General (imágenes guardadas / tiempo total del programa): " 
                  << final_images_saved / overall_elapsed_seconds.count() << " FPS." << std::endl;
        std::cout << "  Tasa de escritura promedio: " << std::fixed << std::setprecision(2) << total_mb_written / overall_elapsed_seconds.count() << " MB/s." << std::endl;
    }
    std::cout << "-------------------------------------\n" << std::endl;

    return 0;
}
