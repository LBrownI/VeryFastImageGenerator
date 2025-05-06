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

  for (int i = 0; i < args->totalImages; ++i) {
    cv::Mat img = generateRandomImage(args->width, args->height);
    if (img.empty()) {
      std::cerr << "Error generating image." << std::endl;
      break;
    }

    pthread_mutex_lock(&bufferMutex);
    imageBuffer.push_back(img.clone());
    pthread_cond_signal(&bufferCond);
    pthread_mutex_unlock(&bufferMutex);
  }

  auto end = std::chrono::high_resolution_clock::now();
  generationTime = end - start;

  pthread_mutex_lock(&bufferMutex);
  generationFinished = true;
  pthread_cond_signal(&bufferCond);
  pthread_mutex_unlock(&bufferMutex);

  pthread_exit(nullptr);
}

// --- Hilo que guarda imágenes ---
void* saveImages(void* arg) {
  int savedCount = 0;
  auto start = std::chrono::high_resolution_clock::now();

  while (true) {
    pthread_mutex_lock(&bufferMutex);

    while (imageBuffer.empty() && !generationFinished) {
      pthread_cond_wait(&bufferCond, &bufferMutex);
    }

    if (!imageBuffer.empty()) {
      cv::Mat img = imageBuffer.back();
      imageBuffer.pop_back();
      pthread_mutex_unlock(&bufferMutex);

      std::string filename = "image_" + std::to_string(savedCount) + ".png";
      cv::imwrite(filename, img);
      savedCount++;
    } else if (generationFinished) {
      pthread_mutex_unlock(&bufferMutex);
      break;
    } else {
      pthread_mutex_unlock(&bufferMutex);
    }
  }

  auto end = std::chrono::high_resolution_clock::now();
  savingTime = end - start;

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
