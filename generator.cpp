#include <iostream>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <pthread.h>
#include <chrono>
#include <vector>

cv::Mat generateRandomImage(int width, int height) {
  if (width <= 0 || height <= 0) {
    std::cerr << "Error: Image dimensions must be positive." << std::endl;
    return cv::Mat();
  }

  cv::Mat randomImage(height, width, CV_8UC3);
  cv::randu(randomImage, cv::Scalar(0, 0, 0), cv::Scalar(255, 255, 255));
  return randomImage;
}

// --- Estructuras y sincronización ---
struct ThreadArgs {
  int width;
  int height;
  int totalImages;
};

std::vector<cv::Mat> imageBuffer;
pthread_mutex_t bufferMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t bufferCond = PTHREAD_COND_INITIALIZER;
bool generationFinished = false;

std::chrono::duration<double> generationTime;
std::chrono::duration<double> savingTime;

// --- Hilo de generación de imágenes ---
void* generateImages(void* arg) { 
  ThreadArgs* args = (ThreadArgs*)arg;

  auto start = std::chrono::high_resolution_clock::now();

        // --- Image Generation ---
        auto gen_start_time = std::chrono::high_resolution_clock::now();
        cv::Mat image = generateRandomImage(args->width, args->height);
        auto gen_end_time = std::chrono::high_resolution_clock::now();
        total_generation_time_sec += std::chrono::duration<double>(gen_end_time - gen_start_time).count();

        if (image.empty()) {
            std::cerr << "Error: Failed to generate image " << i << "." << std::endl;
            continue; 
        }

        // --- Image Saving ---
        std::string filename = args->output_directory + "/image_" + std::to_string(i) + "." + args->image_extension;
        
        auto save_start_time = std::chrono::high_resolution_clock::now();
        bool saved_successfully = false;
        try {
            saved_successfully = cv::imwrite(filename, image);
        } catch (const cv::Exception& ex) {
            std::cerr << "OpenCV error while saving image " << filename << ": " << ex.what() << std::endl;
        }
        auto save_end_time = std::chrono::high_resolution_clock::now();

        if (saved_successfully) {
            total_saving_time_sec += std::chrono::duration<double>(save_end_time - save_start_time).count();
            images_successfully_saved++;
        } else {
            std::cerr << "Error: Failed to save image " << filename << std::endl;
            continue; // Continue to next image even if saving failed
        }

        // --- FPS Simulation (Generation Only) ---
        if (args->fps > 0) {
            auto gen_duration = std::chrono::duration<double>(gen_end_time - gen_start_time);
            if (gen_duration < target_frame_duration) {
                std::this_thread::sleep_for(target_frame_duration - gen_duration);
            }
        }
    }

    auto overall_thread_end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> overall_thread_elapsed_seconds = overall_thread_end_time - overall_thread_start_time;

    // Print summary
    std::cout << std::fixed << std::setprecision(3); // Set precision for floating point numbers that follow
    std::cout << "\n--- Image Processing Thread Summary ---" << std::endl;
    std::cout << "Requested images to generate: " << args->totalImages << std::endl;
    
    std::cout << "Target FPS (generation only): ";
    if (args->fps > 0) {
        std::cout << args->fps; // Uses the std::fixed and std::setprecision(3)
    } else {
        std::cout << "Max (no limit)";
    }
    std::cout << std::endl;

    std::cout << "Output format: ." << args->image_extension << std::endl;
    std::cout << "Images successfully generated and saved: " << images_successfully_saved << std::endl;
    std::cout << "Total time for image generation (pure): " << total_generation_time_sec << " seconds." << std::endl;
    std::cout << "Total time for image saving (pure): " << total_saving_time_sec << " seconds." << std::endl;
    std::cout << "Total operational time in thread (including any FPS delays): " << overall_thread_elapsed_seconds.count() << " seconds." << std::endl;
    
    if (images_successfully_saved > 0) {
        std::cout << "Average time per image (generation): " << total_generation_time_sec / images_successfully_saved << " seconds." << std::endl;
        std::cout << "Average time per image (saving): " << total_saving_time_sec / images_successfully_saved << " seconds." << std::endl;
        if (total_generation_time_sec > 0) {
            std::cout << "Effective FPS (generation only): " << images_successfully_saved / total_generation_time_sec << " FPS." << std::endl;
        } else {
            std::cout << "Effective FPS (generation only): N/A (total generation time was zero or too small)" << std::endl;
        }
    }
    std::cout << "-------------------------------------\n" << std::endl;

  pthread_exit(nullptr);
}

// --- Main ---
int main() {
  ThreadArgs args = {1920, 1280, 50};
  pthread_t generatorThread, saverThread;

  pthread_create(&generatorThread, nullptr, generateImages, &args);
  pthread_create(&saverThread, nullptr, saveImages, nullptr);

  pthread_join(generatorThread, nullptr);
  pthread_join(saverThread, nullptr);

  std::cout << "\n--- Resultados ---" << std::endl;
  std::cout << "Tiempo en generar imágenes : " << generationTime.count() << " segundos" << std::endl;
  std::cout << "Tiempo en guardar imágenes  : " << savingTime.count() << " segundos" << std::endl;

  return 0;
}
