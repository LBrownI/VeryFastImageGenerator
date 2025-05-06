#include <iostream>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <pthread.h>
#include <chrono>  // Para medir el tiempo

cv::Mat generateRandomImage(int width, int height) {
  if (width <= 0 || height <= 0) {
    std::cerr << "Error: Image dimensions must be positive." << std::endl;
    return cv::Mat();
  }

  cv::Mat randomImage(height, width, CV_8UC3);
  cv::randu(randomImage, cv::Scalar(0, 0, 0), cv::Scalar(255, 255, 255));
  return randomImage;
}

struct ThreadArgs {
  int width;
  int height;
  int totalImages;
};

void* imageLoop(void* arg) {
  ThreadArgs* args = (ThreadArgs*)arg;

  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < args->totalImages; ++i) {
    cv::Mat image = generateRandomImage(args->width, args->height);
    if (image.empty()) {
      std::cerr << "Error generating image." << std::endl;
      pthread_exit(nullptr);
    }

    // Opcional: mostrar o guardar cada imagen (comentado para velocidad)
    // cv::imshow("Random Image", image);
    // cv::waitKey(1); // Mostrar rápido
  }

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsedSeconds = end - start;

  std::cout << "Generated " << args->totalImages << " images in "
            << elapsedSeconds.count() << " seconds." << std::endl;

  pthread_exit(nullptr);
}

int main() {
  ThreadArgs args = {1920, 1280, 50};  // Cambia tamaño o cantidad si deseas
  pthread_t threadID;

  if (pthread_create(&threadID, nullptr, imageLoop, &args)) {
    std::cerr << "Error creating thread." << std::endl;
    return 1;
  }

  pthread_join(threadID, nullptr);
  return 0;
}
