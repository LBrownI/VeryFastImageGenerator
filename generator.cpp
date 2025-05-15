#include <iostream>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <thread>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace fs = std::filesystem;

struct ImageData {
    cv::Mat image;
    int index;
};

struct ThreadArgs {
    int width;
    int height;
    int totalImages;
    double fps;
    std::string image_extension;
    std::string output_directory;
};

std::queue<ImageData> imageQueue;
std::mutex queueMutex;
std::condition_variable queueCV;
bool finishedGenerating = false;

cv::Mat generateRandomImage(int width, int height) {
    cv::Mat image(height, width, CV_8UC3);
    cv::randu(image, cv::Scalar(0, 0, 0), cv::Scalar(255, 255, 255));
    return image;
}

void imageGenerator(ThreadArgs args) {
    int images_generated = 0;
    auto start = std::chrono::steady_clock::now();
    std::chrono::duration<double> frame_duration(1.0 / args.fps);

    for (int i = 0; i < args.totalImages; ++i) {
        auto frame_start_time = std::chrono::steady_clock::now();

        cv::Mat image = generateRandomImage(args.width, args.height);

        {
            std::lock_guard<std::mutex> lock(queueMutex);
            imageQueue.push({image, i});
        }
        queueCV.notify_one();
        images_generated++;

        // CORREGIDO:
        auto next_frame_time = start + frame_duration * (i + 1);
        std::this_thread::sleep_until(next_frame_time);
    }

    finishedGenerating = true;
    queueCV.notify_one();

    double generation_time = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    double effective_fps = images_generated / generation_time;

    std::cout << "--- Resumen generación ---\n";
    std::cout << "Imágenes generadas: " << images_generated << "\n";
    std::cout << std::fixed << std::setprecision(2)
              << "FPS efectivo generación (reloj): " << effective_fps << "\n";
}


void imageSaver(ThreadArgs args) {
    int images_saved = 0;

    while (true) {
        std::unique_lock<std::mutex> lock(queueMutex);
        queueCV.wait(lock, []{ return !imageQueue.empty() || finishedGenerating; });

        while (!imageQueue.empty()) {
            ImageData imgData = imageQueue.front();
            imageQueue.pop();
            lock.unlock();

            std::string filename = args.output_directory + "/image_" + std::to_string(imgData.index) + "." + args.image_extension;
            cv::imwrite(filename, imgData.image);

            images_saved++;
            lock.lock();
        }

        if (finishedGenerating && imageQueue.empty())
            break;
    }

    std::cout << "Imágenes guardadas en disco: " << images_saved << "\n";
}

int main(int argc, char *argv[]) {
    if (argc != 6) {
        std::cerr << "Uso: " << argv[0] << " <ancho> <alto> <duracion_segundos> <fps> <extension>\n";
        return 1;
    }

    ThreadArgs args;
    args.width = std::stoi(argv[1]);
    args.height = std::stoi(argv[2]);
    int duration_seconds = std::stoi(argv[3]);
    args.fps = std::stod(argv[4]);
    args.totalImages = static_cast<int>(args.fps * duration_seconds);
    args.image_extension = argv[5];
    args.output_directory = "generated_images";

    if (!fs::exists(args.output_directory)) {
        fs::create_directories(args.output_directory);
    }

    auto start_global = std::chrono::steady_clock::now();

    std::thread generatorThread(imageGenerator, args);
    std::thread saverThread(imageSaver, args);

    generatorThread.join();
    saverThread.join();

    auto total_elapsed = std::chrono::steady_clock::now() - start_global;

    std::cout << "Tiempo total real: " << std::chrono::duration<double>(total_elapsed).count() << " segundos\n";

    return 0;
}