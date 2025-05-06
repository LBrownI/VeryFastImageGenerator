#include <iostream>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <pthread.h>
#include <unistd.h>  // para usleep

cv::Mat generateRandomImage(int width, int height) {
  if (width <= 0 || height <= 0) {
    std::cerr << "Error: Image dimensions must be positive." << std::endl;
    return cv::Mat();
  }

  cv::Mat randomImage(height, width, CV_8UC3);
  cv::randu(randomImage, cv::Scalar(0, 0, 0), cv::Scalar(255, 255, 255));
  return randomImage;
}

// --- Estructura para pasar parámetros al hilo ---
struct ThreadArgs {
  int width;
  int height;
};

// --- Función que ejecutará el hilo ---
void* imageLoop(void* arg) {
  ThreadArgs* args = (ThreadArgs*)arg;

  while (true) {
    cv::Mat image = generateRandomImage(args->width, args->height);
    if (image.empty()) {
      std::cerr << "Error generating image in thread." << std::endl;
      break;
    }

    cv::imshow("Random Image", image);
    if (cv::waitKey(1) == 27) { // salir si se presiona ESC
      break;
    }

    usleep(20000); // Esperar 20 milisegundos ≈ 50 imágenes por segundo
  }

  return nullptr;
}

int main() {
  ThreadArgs args = {1920, 1280};
  pthread_t threadID;

  // Crear el hilo
  if (pthread_create(&threadID, nullptr, imageLoop, &args)) {
    std::cerr << "Error creating thread." << std::endl;
    return 1;
  }

  // Esperar a que el hilo termine
  pthread_join(threadID, nullptr);

  return 0;
}
